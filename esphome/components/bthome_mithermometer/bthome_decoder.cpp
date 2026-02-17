#include "bthome_decoder.h"

#include "esphome/core/log.h"

namespace esphome {
namespace bthome_mithermometer {

static const char *const TAG = "bthome_mithermometer";

static size_t get_bthome_value_length(BTHomeObjectType obj_type) {
  switch (obj_type) {
    // 1 Byte (uint8 / sint8)
    case BTHomeObjectType::PACKET_ID:
    case BTHomeObjectType::BATTERY_PCT:
    case BTHomeObjectType::COUNT_U8:
    case BTHomeObjectType::HUMIDITY_PCT_U8:
    case BTHomeObjectType::MOISTURE_PCT_U8:
    case BTHomeObjectType::UV_INDEX_X10:
    case BTHomeObjectType::TEMPERATURE_C_I8:
    case BTHomeObjectType::TEMPERATURE_C_I8_0_35:
    case BTHomeObjectType::COUNT_I8:
    case BTHomeObjectType::CHANNEL:

    // Binary sensors:
    case BTHomeObjectType::GENERIC_BOOLEAN:
    case BTHomeObjectType::POWER_ON:
    case BTHomeObjectType::OPENING_OPEN:
    case BTHomeObjectType::BATTERY_LOW:
    case BTHomeObjectType::BATTERY_CHARGING:
    case BTHomeObjectType::CO_DETECTED:
    case BTHomeObjectType::COLD_DETECTED:
    case BTHomeObjectType::CONNECTIVITY_CONNECTED:
    case BTHomeObjectType::DOOR_OPEN:
    case BTHomeObjectType::GARAGE_DOOR_OPEN:
    case BTHomeObjectType::GAS_DETECTED:
    case BTHomeObjectType::HEAT_DETECTED:
    case BTHomeObjectType::LIGHT_DETECTED:
    case BTHomeObjectType::LOCK_UNLOCKED:
    case BTHomeObjectType::MOISTURE_WET:
    case BTHomeObjectType::MOTION_DETECTED:
    case BTHomeObjectType::MOVING_ACTIVE:
    case BTHomeObjectType::OCCUPANCY_DETECTED:
    case BTHomeObjectType::PLUG_PLUGGED_IN:
    case BTHomeObjectType::PRESENCE_HOME:
    case BTHomeObjectType::PROBLEM_DETECTED:
    case BTHomeObjectType::RUNNING_ACTIVE:
    case BTHomeObjectType::SAFETY_SAFE:
    case BTHomeObjectType::SMOKE_DETECTED:
    case BTHomeObjectType::SOUND_DETECTED:
    case BTHomeObjectType::TAMPER_ACTIVE:
    case BTHomeObjectType::VIBRATION_DETECTED:
    case BTHomeObjectType::WINDOW_OPEN:
      return 1;

    // 2 Bytes (uint16 / sint16)
    case BTHomeObjectType::TEMPERATURE_C_X100:
    case BTHomeObjectType::HUMIDITY_PCT_X100:
    case BTHomeObjectType::MASS_KG_X100:
    case BTHomeObjectType::MASS_LB_X100:
    case BTHomeObjectType::DEWPOINT_C_X100:
    case BTHomeObjectType::VOLTAGE_MV:
    case BTHomeObjectType::PM25_UGM3:
    case BTHomeObjectType::PM10_UGM3:
    case BTHomeObjectType::CO2_PPM:
    case BTHomeObjectType::TVOC_UGM3:
    case BTHomeObjectType::MOISTURE_PCT_X100:
    case BTHomeObjectType::COUNT_U16:
    case BTHomeObjectType::ROTATION_DEG_X10:
    case BTHomeObjectType::DISTANCE_MM:
    case BTHomeObjectType::DISTANCE_M_X10:
    case BTHomeObjectType::CURRENT_MA:
    case BTHomeObjectType::SPEED_MS_X100:
    case BTHomeObjectType::TEMPERATURE_C_X10:
    case BTHomeObjectType::VOLUME_L_X10:
    case BTHomeObjectType::VOLUME_ML:
    case BTHomeObjectType::VOLUME_FLOW_M3HR_X1000:
    case BTHomeObjectType::VOLTAGE_V_X10:
    case BTHomeObjectType::ACCELERATION_MSS_X1000:
    case BTHomeObjectType::GYROSCOPE_DEGS_X1000:
    case BTHomeObjectType::CONDUCTIVITY_USCM:
    case BTHomeObjectType::COUNT_I16:
    case BTHomeObjectType::CURRENT_MA_I16:
    case BTHomeObjectType::DIRECTION_DEG_X100:
    case BTHomeObjectType::PRECIPITATION_MM_X10:
    case BTHomeObjectType::ROTATIONAL_SPEED_RPM:
      return 2;

    // 3 Bytes (uint24)
    case BTHomeObjectType::PRESSURE_PA:
    case BTHomeObjectType::ILLUMINANCE_LX_X100:
    case BTHomeObjectType::ENERGY_WH:
    case BTHomeObjectType::POWER_W_X100:
    case BTHomeObjectType::DURATION_S_X1000:
    case BTHomeObjectType::GAS_M3_U24_X1000:
      return 3;

    // 4 Bytes (uint32 / sint32)
    case BTHomeObjectType::COUNT_U32:
    case BTHomeObjectType::GAS_M3_U32_X1000:
    case BTHomeObjectType::ENERGY_WH_U32:
    case BTHomeObjectType::VOLUME_ML_U32:
    case BTHomeObjectType::WATER_ML:
    case BTHomeObjectType::TIMESTAMP:
    case BTHomeObjectType::VOLUME_STORAGE_ML:
    case BTHomeObjectType::COUNT_I32:
    case BTHomeObjectType::POWER_W_I32_X100:
    case BTHomeObjectType::SPEED_UMS_I32:
    case BTHomeObjectType::ACCELERATION_UMSS_I32:
      return 4;

    // Variable length or Unknown
    case BTHomeObjectType::TEXT:
    case BTHomeObjectType::RAW:
    default:
      return 0;
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
  BTHomeObjectType obj_type = static_cast<BTHomeObjectType>(*ptr_++);
  remaining_--;

  size_t value_length = 0;
  if (obj_type == BTHomeObjectType::TEXT || obj_type == BTHomeObjectType::RAW) {  // variable-size objects
    if (remaining_ == 0) {
      ptr_ = nullptr;
      remaining_ = 0;
      return;
    }
    value_length = *ptr_++;
    remaining_--;
  } else {
    value_length = get_bthome_value_length(obj_type);
    if (value_length == 0) {
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
