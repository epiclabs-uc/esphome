from typing import Any

from esphome import core
import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_BATTERY_LEVEL,
    CONF_BATTERY_VOLTAGE,
    CONF_HUMIDITY,
    CONF_ID,
    CONF_SIGNAL_STRENGTH,
    CONF_TEMPERATURE,
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_HUMIDITY,
    DEVICE_CLASS_SIGNAL_STRENGTH,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_VOLTAGE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    UNIT_CELSIUS,
    UNIT_DECIBEL_MILLIWATT,
    UNIT_PERCENT,
    UNIT_VOLT,
)
from esphome.cpp_generator import TemplateArguments

from . import BTHomeSensor, bthome_mithermometer_base_schema, setup_bthome_mithermometer

CODEOWNERS = ["@nagyrobi"]

DEPENDENCIES = ["esp32_ble_tracker"]

CONFIG_SCHEMA = bthome_mithermometer_base_schema(
    {
        cv.Optional(CONF_TEMPERATURE): cv.ensure_list(
            sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                accuracy_decimals=2,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
            )
        ),
        cv.Optional(CONF_HUMIDITY): cv.ensure_list(
            sensor.sensor_schema(
                unit_of_measurement=UNIT_PERCENT,
                accuracy_decimals=2,
                device_class=DEVICE_CLASS_HUMIDITY,
                state_class=STATE_CLASS_MEASUREMENT,
            )
        ),
        cv.Optional(CONF_BATTERY_LEVEL): cv.ensure_list(
            sensor.sensor_schema(
                unit_of_measurement=UNIT_PERCENT,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_BATTERY,
                state_class=STATE_CLASS_MEASUREMENT,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            )
        ),
        cv.Optional(CONF_BATTERY_VOLTAGE): cv.ensure_list(
            sensor.sensor_schema(
                unit_of_measurement=UNIT_VOLT,
                accuracy_decimals=3,
                device_class=DEVICE_CLASS_VOLTAGE,
                state_class=STATE_CLASS_MEASUREMENT,
                icon="mdi:battery-plus",
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            )
        ),
        cv.Optional(CONF_SIGNAL_STRENGTH): sensor.sensor_schema(
            unit_of_measurement=UNIT_DECIBEL_MILLIWATT,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_SIGNAL_STRENGTH,
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    }
)


async def to_code(config):

    temp_sens: list[Any] = config.get(CONF_TEMPERATURE, [])
    humi_sens: list[Any] = config.get(CONF_HUMIDITY, [])
    batl_sens: list[Any] = config.get(CONF_BATTERY_LEVEL, [])
    batv_sens: list[Any] = config.get(CONF_BATTERY_VOLTAGE, [])

    var = cg.new_Pvariable(
        core.ID(str(config[CONF_ID]), False, BTHomeSensor),
        TemplateArguments(
            len(temp_sens),
            len(humi_sens),
            len(batl_sens),
            len(batv_sens),
        ),
    )
    await setup_bthome_mithermometer(var, config)

    for index, sens in enumerate(temp_sens):
        sens = await sensor.new_sensor(sens)
        cg.add(var.set_temperature(TemplateArguments(index), sens))

    for index, sens in enumerate(humi_sens):
        sens = await sensor.new_sensor(sens)
        cg.add(var.set_humidity(TemplateArguments(index), sens))

    for index, sens in enumerate(batl_sens):
        sens = await sensor.new_sensor(sens)
        cg.add(var.set_battery_level(TemplateArguments(index), sens))

    for index, sens in enumerate(batv_sens):
        sens = await sensor.new_sensor(sens)
        cg.add(var.set_battery_voltage(TemplateArguments(index), sens))

    if sgnl_sens := config.get(CONF_SIGNAL_STRENGTH):
        sens = await sensor.new_sensor(sgnl_sens)
        cg.add(var.set_signal_strength(sens))
