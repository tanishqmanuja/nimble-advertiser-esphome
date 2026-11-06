#pragma once

#include "esphome/core/component.h"

#ifdef USE_ESP32

#include <atomic>
#include <cstdint>
#include <vector>

#ifndef CONFIG_BT_CONTROLLER_DISABLED
#include <esp_bt.h>
#endif
#include <host/ble_gap.h>
#include <host/ble_hs.h>

namespace esphome::nimble_advertiser {

class NimBLEAdvertiser final : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::BLUETOOTH; }

  void set_min_interval(uint16_t ms) { this->min_interval_ms_ = ms; }
  void set_max_interval(uint16_t ms) { this->max_interval_ms_ = ms; }
  void set_default_duration(int32_t ms) { this->default_duration_ms_ = ms; }
  void set_own_addr_type(uint8_t type) { this->own_addr_type_ = type; }
#ifndef CONFIG_BT_CONTROLLER_DISABLED
  void set_tx_power(esp_power_level_t level) { this->tx_power_ = level; }
  // dBm entry point for user-facing entities (select/number): quantised
  // to the 8 ESP levels, applied immediately when already synced.
  void set_tx_power_dbm(int dbm) {
    esp_power_level_t level = ESP_PWR_LVL_P3;
    switch (dbm) {
      case -12: level = ESP_PWR_LVL_N12; break;
      case -9: level = ESP_PWR_LVL_N9; break;
      case -6: level = ESP_PWR_LVL_N6; break;
      case -3: level = ESP_PWR_LVL_N3; break;
      case 0: level = ESP_PWR_LVL_N0; break;
      case 3: level = ESP_PWR_LVL_P3; break;
      case 6: level = ESP_PWR_LVL_P6; break;
      case 9: level = ESP_PWR_LVL_P9; break;
      default: break;
    }
    this->tx_power_ = level;
    if (this->synced_.load()) {
      esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, level);
    }
  }
#endif

  // Action fields (set by ApplyField, then advertise() via ApplyCall).
  void set_advertise_data(std::vector<uint8_t> data) {
    this->pending_data_ = std::move(data);
    this->pending_duration_ms_ = 0;  // 0 = use default; clears stale duration
    this->has_pending_.store(true);
  }
  void set_advertise_duration(int32_t ms) { this->pending_duration_ms_ = ms; }
  void advertise();
  void stop_advertising();

  // Cached state, not a ble_gap_adv_active() poll: the stack's op flag
  // and duration timer were observed to never clear (wait_until ate the
  // full timeout). Loop() owns the duration timer, gap_event covers the
  // stack auto-stop — either one clears this.
  bool is_advertising() const { return this->advertising_.load(); }
  // True while an advertise() is queued but not yet started (e.g. waiting
  // for sync, or a stop->restart in loop()). Use with is_advertising() in a
  // while loop so it doesn't exit before the burst even starts.
  bool is_pending() const { return this->has_pending_.load(); }

 protected:
  static void host_task(void *arg);
  static void on_sync();
  static void on_reset(int reason);
  static int gap_event(struct ble_gap_event *event, void *arg);
  void start_advertising_();

  uint16_t min_interval_ms_{20};
  uint16_t max_interval_ms_{40};
  int32_t default_duration_ms_{400};
  uint8_t own_addr_type_{BLE_OWN_ADDR_PUBLIC};
#ifndef CONFIG_BT_CONTROLLER_DISABLED
  esp_power_level_t tx_power_{ESP_PWR_LVL_P3};
#endif

  std::vector<uint8_t> pending_data_;
  int32_t pending_duration_ms_{400};
  std::atomic<bool> has_pending_{false};
  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> synced_{false};
  std::atomic<bool> advertising_{false};
  uint32_t adv_start_ms_{0};
  int32_t adv_duration_ms_{0};
  bool initialized_{false};
};

}  // namespace esphome::nimble_advertiser

#endif
