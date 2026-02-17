#pragma once

#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/component.h"

#include <cstdint>
#include <initializer_list>
#include <vector>

#ifdef USE_ESP32

namespace esphome {
namespace bthome_mithermometer {

static const char *const TAG = "bthome_mithermometer";

struct BTHomeObject {
  uint8_t type = 0;
  const uint8_t *data = nullptr;
  size_t length = 0;
};

class BTHomePayloadIterator {
 public:
  class Iterator {
   public:
    Iterator(const uint8_t *ptr, size_t remaining);
    BTHomeObject operator*() const;
    Iterator &operator++();
    bool operator!=(const Iterator &other) const;

   private:
    void parse_next();
    const uint8_t *ptr_;
    size_t remaining_;
    BTHomeObject current_obj_{};
  };
  BTHomePayloadIterator(const uint8_t *payload, size_t size);

  Iterator begin() const;
  Iterator end() const;

 private:
  const uint8_t *payload_;
  size_t size_;
};

class BTHomeMiThermometer : public esp32_ble_tracker::ESPBTDeviceListener, public Component {
 public:
  void set_address(uint64_t address) { this->address_ = address; }
  void set_bindkey(std::initializer_list<uint8_t> bindkey);
  void set_signal_strength(sensor::Sensor *signal_strength) { this->signal_strength_ = signal_strength; }

  void dump_config() override;
  bool parse_device(const esp32_ble_tracker::ESPBTDevice &device) override;

 protected:
  bool handle_service_data_(const esp32_ble_tracker::ServiceData &service_data,
                            const esp32_ble_tracker::ESPBTDevice &device);
  bool decrypt_bthome_payload_(const std::vector<uint8_t> &data, uint64_t source_address,
                               std::vector<uint8_t> &payload) const;

  virtual bool on_payload(const uint8_t *payload, uint8_t size) = 0;

  uint64_t address_{0};
  optional<uint8_t> last_packet_id_{};
  bool has_bindkey_{false};
  uint8_t bindkey_[16];
  sensor::Sensor *signal_strength_{nullptr};
};

template<uint8_t NUM_TEMPERATURE, uint8_t NUM_HUMIDITY, uint8_t NUM_BATTERY_LEVEL, uint8_t NUM_BATTERY_VOLTAGE>
class BTHomeSensor : public BTHomeMiThermometer {
 public:
  template<uint8_t Index> void set_temperature(sensor::Sensor *temperature) {
    static_assert(Index < NUM_TEMPERATURE, "Temperature index out of bounds!");
    this->temperature_[Index] = temperature;
  }

  template<uint8_t Index> void set_humidity(sensor::Sensor *humidity) {
    static_assert(Index < NUM_HUMIDITY, "Humidity index out of bounds!");
    this->humidity_[Index] = humidity;
  }

  template<uint8_t Index> void set_battery_level(sensor::Sensor *battery_level) {
    static_assert(Index < NUM_BATTERY_LEVEL, "Battery Level index out of bounds!");
    this->battery_level_[Index] = battery_level;
  }

  template<uint8_t Index> void set_battery_voltage(sensor::Sensor *battery_voltage) {
    static_assert(Index < NUM_BATTERY_VOLTAGE, "Battery Voltage index out of bounds!");
    this->battery_voltage_[Index] = battery_voltage;
  }

 protected:
  bool on_payload(const uint8_t *payload, uint8_t size) {
    bool reported = false;

    uint8_t temperature_count = 0;
    uint8_t humidity_count = 0;
    uint8_t battery_level_count = 0;
    uint8_t battery_voltage_count = 0;

    BTHomePayloadIterator payload_iterator(payload, size);

    for (auto [obj_type, value, length] : payload_iterator) {
      switch (obj_type) {
        case 0x00: {  // packet id
          const uint8_t packet_id = value[0];
          if (this->last_packet_id_.has_value() && *this->last_packet_id_ == packet_id) {
            return reported;
          }
          this->last_packet_id_ = packet_id;
          break;
        }
        case 0x01: {  // battery percentage
          if (battery_level_count < NUM_BATTERY_LEVEL) {
            this->battery_level_[battery_level_count]->publish_state(value[0]);
            reported = true;
            battery_level_count++;
          }
          break;
        }
        case 0x02: {  // temperature
          if (temperature_count < NUM_TEMPERATURE) {
            const int16_t raw = encode_uint16(value[1], value[0]);
            this->temperature_[temperature_count]->publish_state(raw * 0.01f);
            reported = true;
            temperature_count++;
          }
          break;
        }
        case 0x03: {  // humidity
          if (humidity_count < NUM_HUMIDITY) {
            const uint16_t raw = encode_uint16(value[1], value[0]);
            this->humidity_[humidity_count]->publish_state(raw * 0.01f);
            reported = true;
            humidity_count++;
          }
          break;
        }
        case 0x0C: {  // battery voltage (mV)
          if (battery_voltage_count < NUM_BATTERY_VOLTAGE) {
            this->battery_voltage_[battery_voltage_count]->publish_state(value[0] * 0.001f);
            reported = true;
            battery_voltage_count++;
          }
          break;
        }
        default:
          break;
      }
    }
    return reported;
  }

  void dump_config() override {
    BTHomeMiThermometer::dump_config();
    for (sensor::Sensor *s : this->temperature_) {
      LOG_SENSOR("  ", "Temperature", s);
    }
    for (sensor::Sensor *s : this->humidity_) {
      LOG_SENSOR("  ", "Humidity", s);
    }
    for (sensor::Sensor *s : this->battery_level_) {
      LOG_SENSOR("  ", "Battery Level", s);
    }
    for (sensor::Sensor *s : this->battery_voltage_) {
      LOG_SENSOR("  ", "Battery Voltage", s);
    }
  }

  std::array<sensor::Sensor *, NUM_TEMPERATURE> temperature_{};
  std::array<sensor::Sensor *, NUM_HUMIDITY> humidity_{};
  std::array<sensor::Sensor *, NUM_BATTERY_LEVEL> battery_level_{};
  std::array<sensor::Sensor *, NUM_BATTERY_VOLTAGE> battery_voltage_{};
};

}  // namespace bthome_mithermometer
}  // namespace esphome

#endif
