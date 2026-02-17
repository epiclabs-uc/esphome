#include "bthome_decoder.h"

#include "esphome/core/log.h"

namespace esphome {
namespace bthome_mithermometer {

static const char *const TAG = "bthome_mithermometer";

static bool get_bthome_value_length(uint8_t obj_type, size_t &value_length) {
  switch (obj_type) {
    case 0x00:  // packet id
    case 0x01:  // battery
    case 0x09:  // count (uint8)
    case 0x0F:  // generic boolean
    case 0x10:  // power (bool)
    case 0x11:  // opening
    case 0x15:  // battery low
    case 0x16:  // battery charging
    case 0x17:  // carbon monoxide
    case 0x18:  // cold
    case 0x19:  // connectivity
    case 0x1A:  // door
    case 0x1B:  // garage door
    case 0x1C:  // gas
    case 0x1D:  // heat
    case 0x1E:  // light
    case 0x1F:  // lock
    case 0x20:  // moisture
    case 0x21:  // motion
    case 0x22:  // moving
    case 0x23:  // occupancy
    case 0x24:  // plug
    case 0x25:  // presence
    case 0x26:  // problem
    case 0x27:  // running
    case 0x28:  // safety
    case 0x29:  // smoke
    case 0x2A:  // sound
    case 0x2B:  // tamper
    case 0x2C:  // vibration
    case 0x2D:  // water leak
    case 0x2E:  // humidity (uint8)
    case 0x2F:  // moisture (uint8)
    case 0x46:  // UV index
    case 0x57:  // temperature (sint8)
    case 0x58:  // temperature (0.35C step)
    case 0x59:  // count (sint8)
    case 0x60:  // channel
      value_length = 1;
      return true;
    case 0x02:  // temperature (0.01C)
    case 0x03:  // humidity
    case 0x06:  // mass (kg)
    case 0x07:  // mass (lb)
    case 0x08:  // dewpoint
    case 0x0C:  // voltage (mV)
    case 0x0D:  // pm2.5
    case 0x0E:  // pm10
    case 0x12:  // CO2
    case 0x13:  // TVOC
    case 0x14:  // moisture
    case 0x3D:  // count (uint16)
    case 0x3F:  // rotation
    case 0x40:  // distance (mm)
    case 0x41:  // distance (m)
    case 0x43:  // current (A)
    case 0x44:  // speed
    case 0x45:  // temperature (0.1C)
    case 0x47:  // volume (L)
    case 0x48:  // volume (mL)
    case 0x49:  // volume flow rate
    case 0x4A:  // voltage (0.1V)
    case 0x51:  // acceleration
    case 0x52:  // gyroscope
    case 0x56:  // conductivity
    case 0x5A:  // count (sint16)
    case 0x5D:  // current (sint16)
    case 0x5E:  // direction
    case 0x5F:  // precipitation
    case 0x61:  // rotational speed
    case 0xF0:  // button event
      value_length = 2;
      return true;
    case 0x04:  // pressure
    case 0x05:  // illuminance
    case 0x0A:  // energy
    case 0x0B:  // power
    case 0x42:  // duration
    case 0x4B:  // gas (uint24)
    case 0xF2:  // firmware version (uint24)
      value_length = 3;
      return true;
    case 0x3E:  // count (uint32)
    case 0x4C:  // gas (uint32)
    case 0x4D:  // energy (uint32)
    case 0x4E:  // volume (uint32)
    case 0x4F:  // water (uint32)
    case 0x50:  // timestamp
    case 0x55:  // volume storage
    case 0x5B:  // count (sint32)
    case 0x5C:  // power (sint32)
    case 0x62:  // speed (sint32)
    case 0x63:  // acceleration (sint32)
    case 0xF1:  // firmware version (uint32)
      value_length = 4;
      return true;
    default:
      return false;
  }
}

BTHomePayloadDecoder::Iterator::Iterator(const uint8_t *ptr, size_t remaining) : ptr_(ptr), remaining_(remaining) {
  this->parse_next_();
}

BTHomeObject BTHomePayloadDecoder::Iterator::operator*() const { return current_obj_; }

BTHomePayloadDecoder::Iterator &BTHomePayloadDecoder::Iterator::operator++() {
  this->parse_next_();
  return *this;
}

bool BTHomePayloadDecoder::Iterator::operator!=(const Iterator &other) const { return ptr_ != other.ptr_; }

void BTHomePayloadDecoder::Iterator::parse_next_() {
  if (remaining_ == 0) {
    ptr_ = nullptr;
    return;
  }

  const uint8_t *start = ptr_;
  uint8_t obj_type = *ptr_++;
  remaining_--;

  size_t value_length = 0;
  if (obj_type == 0x53) {  // Text objects
    if (remaining_ == 0) {
      ptr_ = nullptr;
      remaining_ = 0;
      return;
    }
    value_length = *ptr_++;
    remaining_--;
  } else {
    if (!get_bthome_value_length(obj_type, value_length)) {
      ptr_ = nullptr;  // Invalid type, stop iteration
      remaining_ = 0;
      return;
    }
  }

  if (remaining_ < value_length || value_length == 0) {
    ptr_ = nullptr;
    remaining_ = 0;
    return;
  }

  if (obj_type < current_obj_.type) {
    ESP_LOGVV(TAG, "BTHome objects not in ascending order");
  }

  current_obj_ = {obj_type, ptr_, value_length};
  ptr_ += value_length;
  remaining_ -= value_length;
}

BTHomePayloadDecoder::BTHomePayloadDecoder(const uint8_t *payload, size_t size) : payload_(payload), size_(size) {}

BTHomePayloadDecoder::Iterator BTHomePayloadDecoder::begin() const { return Iterator(payload_, size_); }
BTHomePayloadDecoder::Iterator BTHomePayloadDecoder::end() const { return Iterator(nullptr, 0); }

}  // namespace bthome_mithermometer
}  // namespace esphome
