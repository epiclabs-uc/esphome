from ast import literal_eval
from collections.abc import Iterator, Mapping
from itertools import chain, islice
import math
from types import GeneratorType
from typing import Any

import jinja2 as jinja
from jinja2.nativetypes import NativeCodeGenerator, NativeTemplate
from jinja2.runtime import missing as Missing
import voluptuous as vol

from esphome.config_helpers import merge_config
import esphome.config_validation as cv
from esphome.const import VALID_SUBSTITUTIONS_CHARACTERS
from esphome.yaml_util import ESPHomeDataBase, make_data_base

# Re-exported for backward compatibility — consumers import has_jinja from here
from esphome.expression import has_jinja  # noqa: F401  # pylint: disable=unused-import

TemplateError = jinja.TemplateError
TemplateSyntaxError = jinja.TemplateSyntaxError
TemplateRuntimeError = jinja.TemplateRuntimeError
UndefinedError = jinja.UndefinedError
Undefined = jinja.Undefined
# Sentinel key for resolver callback in ContextVars.
# Dots are invalid in substitution names so this can never collide with user keys.
Resolver = ".resolver"


CONF_PARAMETERS = make_data_base("parameters")
CONF_BODY = make_data_base("body")
CONF_RETURN = "return"
CONF_MACROS = "$macros"

def validate_identifier(value):
    value = cv.string(value)
    if not value:
        raise cv.Invalid("Identifier name must not be empty")
    if value[0].isdigit():
        raise cv.Invalid("First character in an identifier cannot be a digit.")
    for char in value:
        if char not in VALID_SUBSTITUTIONS_CHARACTERS:
            raise cv.Invalid(
                f"Jinja identifier names must only consist of upper/lowercase characters, the underscore and numbers. The character '{char}' cannot be used"
            )
    return value


def _fix_data_base(value: Any, source: Any) -> Any:
    if isinstance(source, ESPHomeDataBase):
        return make_data_base(value, source)
    return value


def _merge_return_into_body(macro_def):
    """
    Combines the value of "return" into the macro body
    """
    if CONF_PARAMETERS not in macro_def:
        macro_def[CONF_PARAMETERS] = {}

    if (ret := macro_def.pop(CONF_RETURN, None)) is not None:
        body = macro_def.get(CONF_BODY, "")
        # wrap the return value
        ret_stmt = _fix_data_base(f"${{{ret}}}", ret)
        macro_def[CONF_BODY] = (
            _fix_data_base(f"{body}\n{ret_stmt}", body) if body else ret_stmt
        )

    return macro_def


JINJA_MACROS_SCHEMA = cv.Schema(
    {
        validate_identifier: cv.All(
            {
                cv.Optional(str(CONF_PARAMETERS)): cv.ensure_schema(
                    cv.Schema({validate_identifier: object})
                ),
                cv.Optional(str(CONF_BODY), default=""): cv.string,
                cv.Optional(CONF_RETURN): cv.string,
            },
            _merge_return_into_body,
        )
    },
    extra=vol.PREVENT_EXTRA,
)


@jinja.pass_context
def jinja_map(parent_ctx: dict, func: Any, iterable: Any) -> list:
    return map(lambda x: func(parent_ctx, x), iterable)


# SAFE_GLOBALS defines a allowlist of built-in functions or modules that are considered safe to expose
# in Jinja templates or other sandboxed evaluation contexts. Only functions that do not allow
# arbitrary code execution, file access, or other security risks are included.
#
# The following functions are considered safe:
#   - math: The entire math module is injected, allowing access to mathematical functions like sin, cos, sqrt, etc.
#   - ord: Converts a character to its Unicode code point integer.
#   - chr: Converts an integer to its corresponding Unicode character.
#   - len: Returns the length of a sequence or collection.
#
# These functions were chosen because they are pure, have no side effects, and do not provide access
# to the file system, environment, or other potentially sensitive resources.
SAFE_GLOBALS = {
    "math": math,  # Inject entire math module
    "ord": ord,
    "chr": chr,
    "len": len,
    "map": jinja_map,
    "merge": merge_config,
}


class JinjaError(Exception):
    def __init__(self, context_trace: dict, expr: str):
        self.context_trace = context_trace
        self.eval_stack = [expr]

    def parent(self):
        return self.__context__

    def error_name(self):
        return type(self.parent()).__name__

    def context_trace_str(self):
        return "\n".join(
            f"  {k} = {repr(v)} ({type(v).__name__})"
            for k, v in self.context_trace.items()
        )

    def stack_trace_str(self):
        return "\n".join(
            f" {len(self.eval_stack) - i}: {expr}{i == 0 and ' <-- ' + self.error_name() or ''}"
            for i, expr in enumerate(self.eval_stack)
        )


class TrackerContext(jinja.runtime.Context):
    def resolve_or_missing(self, key):
        val = super().resolve_or_missing(key)
        if val is Missing:
            # Variable not in the template context — check if a resolver callback
            # was registered (by _push_context) to lazily resolve dependencies
            # between substitution variables in the same block.
            resolver = super().resolve_or_missing(Resolver)
            if resolver is not Missing:
                val = resolver(key)
        self.environment.context_trace[key] = val
        return val


def _concat_nodes_override(values: Iterator[Any]) -> Any:
    """
    This function customizes how Jinja preserves native types when concatenating
    multiple result nodes together. If the result is a single node, its value
    is returned. Otherwise, the nodes are concatenated as strings. If
    the result can be parsed with `ast.literal_eval`, the parsed
    value is returned. Otherwise, the string is returned.
    This helps preserve metadata such as ESPHomeDataBase from original values
    and mimicks how HomeAssistant deals with template evaluation and preserving
    the original datatype.
    """
    head: list[Any] = list(islice(values, 2))

    if not head:
        return None

    if len(head) == 1:
        raw = head[0]
        if not isinstance(raw, str):
            return raw
    else:
        if isinstance(values, GeneratorType):
            values = chain(head, values)
        raw = "".join([str(v) for v in values])

    result = None
    try:
        # Attempt to parse the concatenated string into a Python literal.
        # This allows expressions like "1 + 2" to be evaluated to the integer 3.
        # If the result is also a string or there is a parsing error,
        # fall back to returning the raw string. This is consistent with
        #  Home Assistant's behavior when evaluating templates
        result = literal_eval(raw)
    except (ValueError, SyntaxError, MemoryError, TypeError):
        pass
    else:
        if isinstance(result, set):
            # Sets are not supported, return raw string
            return raw

        if not isinstance(result, str):
            return result

    return raw


class Jinja(jinja.Environment):
    """Jinja environment configured for ESPHome substitution expressions."""

    # jinja environment customization overrides
    code_generator_class = NativeCodeGenerator
    concat = staticmethod(_concat_nodes_override)

    def __init__(self) -> None:
        super().__init__(
            trim_blocks=True,
            lstrip_blocks=True,
            block_start_string="<%",
            block_end_string="%>",
            line_statement_prefix="#",
            line_comment_prefix="##",
            variable_start_string="${",
            variable_end_string="}",
            undefined=jinja.StrictUndefined,
        )
        self.context_class = TrackerContext
        self.add_extension("jinja2.ext.do")
        self.context_trace = {}

        @jinja.pass_context
        def jinja_eval(parent_ctx: dict, expr: Any, ctx=None, list_item="item"):
            if isinstance(ctx, list):
                return [jinja_eval(parent_ctx, expr, c, list_item) for c in ctx]
            if ctx is not None and not isinstance(ctx, dict):
                ctx = {list_item: ctx}
            if isinstance(expr, dict):
                expr = dict(expr)
                for k, v in expr.items():
                    new_k = jinja_eval(parent_ctx, k, ctx, list_item)
                    v = jinja_eval(parent_ctx, v, ctx, list_item)
                    if new_k != k:
                        expr.pop(k)
                        k = new_k
                    expr[k] = v
                return expr
            if isinstance(expr, list):
                return [jinja_eval(parent_ctx, v, ctx, list_item) for v in expr]
            if not isinstance(expr, str) or not has_jinja(expr):
                return expr
            result = self.expand(
                expr, {**parent_ctx, **(ctx or {})}, self.strict_undefined
            )
            if isinstance(expr, ESPHomeDataBase):
                result = make_data_base(result, expr)
            return result

        self.globals = {**self.globals, **SAFE_GLOBALS}
        self.globals["eval"] = jinja_eval

    def load_macros(self, macro_definitions: dict):
        """
        Creates Jinja macros out of a simplified yaml syntax.
        Macros are registered as globals on this Environment, and
        they see the calling template's context (unless shadowed by
        their own parameters).
        """

        for name, macro in macro_definitions.items():
            # parameters contains a dict of parameter names to default values
            parameters = macro.get(CONF_PARAMETERS) or {}
            body = macro[CONF_BODY]
            template = self.from_string(body)

            def make_macro_func(template=template, parameters=parameters):
                param_names = tuple(parameters.keys())

                @jinja.pass_context
                def macro_func(context, *args, **kwargs):
                    # 1. Start from the calling template's context
                    #    `context` is a jinja2.runtime.Context
                    render_context = dict(context)

                    # 2. Compute the macro's own parameters (with defaults)
                    call_params = dict(parameters)  # copy defaults

                    #   2a. Positional args -> named parameters
                    for i, arg in enumerate(args):
                        if i < len(param_names):
                            call_params[param_names[i]] = arg

                    #   2b. Keyword args overwrite defaults (and positional)
                    for k, v in kwargs.items():
                        if k in call_params:
                            call_params[k] = v
                        else:
                            # Allow extra kwargs as normal variables
                            call_params[k] = v

                    # 3. Overlay macro parameters on top of the calling context
                    render_context.update(call_params)

                    # 4. Render the macro body with combined context
                    return template.render(render_context)

                macro_func.is_macro = True

                return macro_func

            # Register as a global in the environment
            self.globals[name] = make_macro_func()

    def clear_macros(self):
        """
        Removes all previously loaded macros from the environment.
        """
        for name in list(self.globals.keys()):
            if hasattr(self.globals[name], "is_macro"):
                del self.globals[name]

    def expand(
        self,
        content_str: str,
        context_vars: Mapping[str, Any],
        strict_undefined: bool = False,
    ) -> Any:
        """
        Renders a string that may contain Jinja expressions or statements
        Returns the resulting value if all variables and expressions could be resolved.
        """
        result = None

        old_trace = self.context_trace
        self.context_trace = {}
        try:
            template = self.from_string(content_str)
            result = template.render(context_vars)
            if isinstance(result, Undefined):
                str(result)  # force a UndefinedError exception
        except UndefinedError as err:
            raise err
        except JinjaError as err:
            err.context_trace = {**self.context_trace, **err.context_trace}
            err.eval_stack.append(content_str)
            raise err
        except (
            TemplateError,
            TemplateRuntimeError,
            RuntimeError,
            ArithmeticError,
            AttributeError,
            TypeError,
        ) as err:
            raise JinjaError(self.context_trace, content_str) from err
        finally:
            self.context_trace = old_trace

        return result


class JinjaTemplate(NativeTemplate):
    environment_class = Jinja


Jinja.template_class = JinjaTemplate
