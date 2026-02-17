#pragma once
#include <cstdint>
#include <cstddef>
namespace esphome {
namespace bthome_mithermometer {

enum class BTHomeObjectType : uint8_t {
  ACCELERATION_MSS_X1000 = 0x51,
  ACCELERATION_UMSS_I32 = 0x63,
  BATTERY_PCT = 0x01,
  CHANNEL = 0x60,
  CO2_PPM = 0x12,
  CONDUCTIVITY_USCM = 0x56,
  COUNT_U8 = 0x09,
  COUNT_U16 = 0x3D,
  COUNT_U32 = 0x3E,
  COUNT_I8 = 0x59,
  COUNT_I16 = 0x5A,
  COUNT_I32 = 0x5B,
  CURRENT_MA = 0x43,
  CURRENT_MA_I16 = 0x5D,
  DEWPOINT_C_X100 = 0x08,
  DIRECTION_DEG_X100 = 0x5E,
  DISTANCE_MM = 0x40,
  DISTANCE_M_X10 = 0x41,
  DURATION_S_X1000 = 0x42,
  ENERGY_WH = 0x0A,
  ENERGY_WH_U32 = 0x4D,
  GAS_M3_U24_X1000 = 0x4B,
  GAS_M3_U32_X1000 = 0x4C,
  GYROSCOPE_DEGS_X1000 = 0x52,
  HUMIDITY_PCT_X100 = 0x03,
  HUMIDITY_PCT_U8 = 0x2E,
  ILLUMINANCE_LX_X100 = 0x05,
  MASS_KG_X100 = 0x06,
  MASS_LB_X100 = 0x07,
  MOISTURE_PCT_X100 = 0x14,
  MOISTURE_PCT_U8 = 0x2F,
  PACKET_ID = 0x00,
  PM10_UGM3 = 0x0E,
  PM25_UGM3 = 0x0D,
  POWER_W_X100 = 0x0B,
  POWER_W_I32_X100 = 0x5C,
  PRECIPITATION_MM_X10 = 0x5F,
  PRESSURE_PA = 0x04,
  RAW = 0x54,
  ROTATION_DEG_X10 = 0x3F,
  ROTATIONAL_SPEED_RPM = 0x61,
  SPEED_MS_X100 = 0x44,
  SPEED_UMS_I32 = 0x62,
  TEMPERATURE_C_X100 = 0x02,
  TEMPERATURE_C_X10 = 0x45,
  TEMPERATURE_C_I8 = 0x57,
  TEMPERATURE_C_I8_0_35 = 0x58,
  TEXT = 0x53,
  TIMESTAMP = 0x50,
  TVOC_UGM3 = 0x13,
  UV_INDEX_X10 = 0x46,
  VOLTAGE_MV = 0x0C,
  VOLTAGE_V_X10 = 0x4A,
  VOLUME_FLOW_M3HR_X1000 = 0x49,
  VOLUME_L_X10 = 0x47,
  VOLUME_ML = 0x48,
  VOLUME_ML_U32 = 0x4E,
  VOLUME_STORAGE_ML = 0x55,
  WATER_ML = 0x4F,

  // Binary sensors:

  BATTERY_CHARGING = 0x16,
  BATTERY_LOW = 0x15,
  CO_DETECTED = 0x17,
  COLD_DETECTED = 0x18,
  CONNECTIVITY_CONNECTED = 0x19,
  DOOR_OPEN = 0x1A,
  GARAGE_DOOR_OPEN = 0x1B,
  GAS_DETECTED = 0x1C,
  GENERIC_BOOLEAN = 0x0F,
  HEAT_DETECTED = 0x1D,
  LIGHT_DETECTED = 0x1E,
  LOCK_UNLOCKED = 0x1F,
  MOISTURE_WET = 0x20,
  MOTION_DETECTED = 0x21,
  MOVING_ACTIVE = 0x22,
  OCCUPANCY_DETECTED = 0x23,
  OPENING_OPEN = 0x11,
  PLUG_PLUGGED_IN = 0x24,
  POWER_ON = 0x10,
  PRESENCE_HOME = 0x25,
  PROBLEM_DETECTED = 0x26,
  RUNNING_ACTIVE = 0x27,
  SAFETY_SAFE = 0x28,
  SMOKE_DETECTED = 0x29,
  SOUND_DETECTED = 0x2A,
  TAMPER_ACTIVE = 0x2B,
  VIBRATION_DETECTED = 0x2C,
  WINDOW_OPEN = 0x2D,

};

struct BTHomeObject {
  BTHomeObjectType type = BTHomeObjectType::PACKET_ID;
  const uint8_t *data = nullptr;
  size_t length = 0;
};

class BTHomePayloadDecoder {
 public:
  class Iterator {
   public:
    Iterator(const uint8_t *ptr, size_t remaining);
    BTHomeObject operator*() const;
    Iterator &operator++();
    bool operator!=(const Iterator &other) const;

   private:
    void parse_next_();
    const uint8_t *ptr_;
    size_t remaining_;
    BTHomeObject current_obj_{};
  };
  BTHomePayloadDecoder(const uint8_t *payload, size_t size);

  Iterator begin() const;
  Iterator end() const;

 private:
  const uint8_t *payload_;
  size_t size_;
};
}  // namespace bthome_mithermometer
}  // namespace esphome
