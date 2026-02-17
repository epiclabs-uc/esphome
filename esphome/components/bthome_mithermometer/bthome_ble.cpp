#include "bthome_ble.h"

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <span>

#ifdef USE_ESP32

#include "mbedtls/ccm.h"

namespace esphome {
namespace bthome_mithermometer {

static constexpr size_t BTHOME_BINDKEY_SIZE = 16;
static constexpr size_t BTHOME_NONCE_SIZE = 13;
static constexpr size_t BTHOME_MIC_SIZE = 4;
static constexpr size_t BTHOME_COUNTER_SIZE = 4;

static const char *format_mac_address(std::span<char, MAC_ADDRESS_PRETTY_BUFFER_SIZE> buffer, uint64_t address) {
  std::array<uint8_t, MAC_ADDRESS_SIZE> mac{};
  for (size_t i = 0; i < MAC_ADDRESS_SIZE; i++) {
    mac[i] = (address >> ((MAC_ADDRESS_SIZE - 1 - i) * 8)) & 0xFF;
  }

  format_mac_addr_upper(mac.data(), buffer.data());
  return buffer.data();
}

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

BTHomePayloadIterator::Iterator::Iterator(const uint8_t *ptr, size_t remaining) : ptr_(ptr), remaining_(remaining) {
  this->parse_next();
}

BTHomeObject BTHomePayloadIterator::Iterator::operator*() const { return current_obj_; }

BTHomePayloadIterator::Iterator &BTHomePayloadIterator::Iterator::operator++() {
  this->parse_next();
  return *this;
}

bool BTHomePayloadIterator::Iterator::operator!=(const Iterator &other) const { return ptr_ != other.ptr_; }

void BTHomePayloadIterator::Iterator::parse_next() {
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

BTHomePayloadIterator::BTHomePayloadIterator(const uint8_t *payload, size_t size) : payload_(payload), size_(size) {}

BTHomePayloadIterator::Iterator BTHomePayloadIterator::begin() const { return Iterator(payload_, size_); }
BTHomePayloadIterator::Iterator BTHomePayloadIterator::end() const { return Iterator(nullptr, 0); }

void BTHomeMiThermometer::dump_config() {
  char addr_buf[MAC_ADDRESS_PRETTY_BUFFER_SIZE];
  ESP_LOGCONFIG(TAG, "BTHome MiThermometer");
  ESP_LOGCONFIG(TAG, "  MAC Address: %s", format_mac_address(addr_buf, this->address_));
  if (this->has_bindkey_) {
    char bindkey_hex[format_hex_pretty_size(BTHOME_BINDKEY_SIZE)];
    ESP_LOGCONFIG(TAG, "  Bindkey: %s", format_hex_pretty_to(bindkey_hex, this->bindkey_, BTHOME_BINDKEY_SIZE, '.'));
  }
  LOG_SENSOR("  ", "Signal Strength", this->signal_strength_);
}

bool BTHomeMiThermometer::parse_device(const esp32_ble_tracker::ESPBTDevice &device) {
  bool matched = false;
  for (auto &service_data : device.get_service_datas()) {
    if (this->handle_service_data_(service_data, device)) {
      matched = true;
    }
  }
  if (matched && this->signal_strength_ != nullptr) {
    this->signal_strength_->publish_state(device.get_rssi());
  }
  return matched;
}

void BTHomeMiThermometer::set_bindkey(std::initializer_list<uint8_t> bindkey) {
  if (bindkey.size() != sizeof(this->bindkey_)) {
    ESP_LOGW(TAG, "BTHome bindkey size mismatch: %zu", bindkey.size());
    return;
  }
  std::copy(bindkey.begin(), bindkey.end(), this->bindkey_);
  this->has_bindkey_ = true;
}

bool BTHomeMiThermometer::decrypt_bthome_payload_(const std::vector<uint8_t> &data, uint64_t source_address,
                                                  std::vector<uint8_t> &payload) const {
  if (data.size() <= 1 + BTHOME_COUNTER_SIZE + BTHOME_MIC_SIZE) {
    ESP_LOGVV(TAG, "Encrypted BTHome payload too short: %zu", data.size());
    return false;
  }

  const size_t ciphertext_size = data.size() - 1 - BTHOME_COUNTER_SIZE - BTHOME_MIC_SIZE;
  payload.resize(ciphertext_size);

  std::array<uint8_t, MAC_ADDRESS_SIZE> mac{};
  for (size_t i = 0; i < MAC_ADDRESS_SIZE; i++) {
    mac[i] = (source_address >> ((MAC_ADDRESS_SIZE - 1 - i) * 8)) & 0xFF;
  }

  std::array<uint8_t, BTHOME_NONCE_SIZE> nonce{};
  memcpy(nonce.data(), mac.data(), mac.size());
  nonce[6] = 0xD2;
  nonce[7] = 0xFC;
  nonce[8] = data[0];
  memcpy(nonce.data() + 9, &data[data.size() - BTHOME_COUNTER_SIZE - BTHOME_MIC_SIZE], BTHOME_COUNTER_SIZE);

  const uint8_t *ciphertext = data.data() + 1;
  const uint8_t *mic = data.data() + data.size() - BTHOME_MIC_SIZE;

  mbedtls_ccm_context ctx;
  mbedtls_ccm_init(&ctx);

  int ret = mbedtls_ccm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, this->bindkey_, BTHOME_BINDKEY_SIZE * 8);
  if (ret) {
    ESP_LOGVV(TAG, "mbedtls_ccm_setkey() failed.");
    mbedtls_ccm_free(&ctx);
    return false;
  }

  ret = mbedtls_ccm_auth_decrypt(&ctx, ciphertext_size, nonce.data(), nonce.size(), nullptr, 0, ciphertext,
                                 payload.data(), mic, BTHOME_MIC_SIZE);
  mbedtls_ccm_free(&ctx);
  if (ret) {
    ESP_LOGVV(TAG, "BTHome decryption failed (ret=%d).", ret);
    return false;
  }
  return true;
}

bool BTHomeMiThermometer::handle_service_data_(const esp32_ble_tracker::ServiceData &service_data,
                                               const esp32_ble_tracker::ESPBTDevice &device) {
  if (!service_data.uuid.contains(0xD2, 0xFC)) {
    return false;
  }

  const auto &data = service_data.data;
  if (data.size() < 2) {
    ESP_LOGVV(TAG, "BTHome data too short: %zu", data.size());
    return false;
  }

  const uint8_t adv_info = data[0];
  const bool is_encrypted = adv_info & 0x01;
  const bool mac_included = adv_info & 0x02;
  const bool is_trigger_based = adv_info & 0x04;
  const uint8_t version = (adv_info >> 5) & 0x07;

  if (version != 0x02) {
    ESP_LOGVV(TAG, "Unsupported BTHome version %u", version);
    return false;
  }

  uint64_t source_address = device.address_uint64();
  bool address_matches = source_address == this->address_;
  if (!is_encrypted && mac_included && data.size() >= 7) {
    uint64_t advertised_address = 0;
    for (int i = 5; i >= 0; i--) {
      advertised_address = (advertised_address << 8) | data[1 + i];
    }
    address_matches = address_matches || advertised_address == this->address_;
  }

  if (is_encrypted && !this->has_bindkey_) {
    if (address_matches) {
      char addr_buf[MAC_ADDRESS_PRETTY_BUFFER_SIZE];
      ESP_LOGE(TAG, "Encrypted BTHome frame received but no bindkey configured for %s",
               device.address_str_to(addr_buf));
    }
    return false;
  }

  if (!is_encrypted && this->has_bindkey_) {
    if (address_matches) {
      char addr_buf[MAC_ADDRESS_PRETTY_BUFFER_SIZE];
      ESP_LOGE(TAG, "Unencrypted BTHome frame received with bindkey configured for %s",
               device.address_str_to(addr_buf));
    }
    return false;
  }
  std::vector<uint8_t> decrypted_payload;
  const uint8_t *payload = nullptr;
  size_t payload_size = 0;

  if (is_encrypted) {
    if (!this->decrypt_bthome_payload_(data, source_address, decrypted_payload)) {
      char addr_buf[MAC_ADDRESS_PRETTY_BUFFER_SIZE];
      ESP_LOGVV(TAG, "Failed to decrypt BTHome frame from %s", device.address_str_to(addr_buf));
      return false;
    }
    payload = decrypted_payload.data();
    payload_size = decrypted_payload.size();
  } else {
    payload = data.data() + 1;
    payload_size = data.size() - 1;
  }

  if (mac_included) {
    if (payload_size < 6) {
      ESP_LOGVV(TAG, "BTHome payload missing MAC address");
      return false;
    }
    source_address = 0;
    for (int i = 5; i >= 0; i--) {
      source_address = (source_address << 8) | payload[i];
    }
    payload += 6;
    payload_size -= 6;
  }

  char addr_buf[MAC_ADDRESS_PRETTY_BUFFER_SIZE];
  if (source_address != this->address_) {
    ESP_LOGVV(TAG, "BTHome frame from unexpected device %s", format_mac_address(addr_buf, source_address));
    return false;
  }

  if (payload_size == 0) {
    ESP_LOGVV(TAG, "BTHome payload empty after header");
    return false;
  }

  bool reported = on_payload(payload, payload_size);
  if (reported) {
    ESP_LOGD(TAG, "BTHome data%sfrom %s", is_trigger_based ? " (triggered) " : " ", device.address_str_to(addr_buf));
  }

  return reported;
}

}  // namespace bthome_mithermometer
}  // namespace esphome

#endif
