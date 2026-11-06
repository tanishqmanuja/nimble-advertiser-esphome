#include "nimble_advertiser.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#ifdef USE_ESP32

#include <host/ble_hs_adv.h>
#include <host/ble_hs_id.h>
#include <host/ble_store.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <nvs_flash.h>

extern "C" void ble_store_config_init(void);

namespace esphome::nimble_advertiser {

ESPHOME_LOG_TAG(TAG, "nimble_advertiser");

static NimBLEAdvertiser *instance_{nullptr};

void NimBLEAdvertiser::dump_config() {
  ESP_LOGCONFIG(TAG, "NimBLE Advertiser:");
  ESP_LOGCONFIG(TAG, "  Min Interval: %ums, Max Interval: %ums, Default Duration: %dms, Own Addr Type: %u",
                this->min_interval_ms_, this->max_interval_ms_, this->default_duration_ms_,
                this->own_addr_type_);
#ifndef CONFIG_BT_CONTROLLER_DISABLED
  ESP_LOGCONFIG(TAG, "  TX Power: %ddBm", (this->tx_power_ * 3) - 12);
#endif
}

void NimBLEAdvertiser::host_task(void *arg) {
  nimble_port_run();
  nimble_port_freertos_deinit();
}

void NimBLEAdvertiser::on_sync() {
  if (instance_ == nullptr) {
    return;
  }
#ifndef CONFIG_BT_CONTROLLER_DISABLED
  // Controller is up only after sync; calling esp_ble_tx_power_set() in
  // setup() (before nimble_port_init()) pokes uninitialised controller
  // memory and panics. esp-nimble-cpp sets power after NimBLEDevice::init().
  esp_err_t err = esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, instance_->tx_power_);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "esp_ble_tx_power_set failed: %s", esp_err_to_name(err));
  }
#endif
  if (instance_->own_addr_type_ == BLE_OWN_ADDR_RANDOM) {
    ble_addr_t addr{};
    if (ble_hs_id_gen_rnd(1, &addr) == 0) {
      ble_hs_id_set_rnd(addr.val);
    }
  }
  instance_->synced_.store(true);
}

void NimBLEAdvertiser::on_reset(int reason) {
  ESP_LOGW(TAG, "NimBLE reset, reason: %d", reason);
  if (instance_ != nullptr) {
    instance_->synced_.store(false);
    instance_->advertising_.store(false);
    instance_->stop_requested_.store(false);
  }
}

int NimBLEAdvertiser::gap_event(struct ble_gap_event *event, void *arg) {
  if (event->type == BLE_GAP_EVENT_ADV_COMPLETE) {
    ESP_LOGD(TAG, "Advertising complete, reason: %d", event->adv_complete.reason);
    if (instance_ != nullptr) {
      instance_->advertising_.store(false);
    }
  }
  return 0;
}

void NimBLEAdvertiser::setup() {
  instance_ = this;
  if (this->initialized_) {
    return;
  }
  this->initialized_ = true;

  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    // No ESP_ERROR_CHECK here: an abort here becomes a panic-reboot loop.
    err = nvs_flash_erase();
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "nvs_flash_erase failed: %s", esp_err_to_name(err));
      return;
    }
    err = nvs_flash_init();
  }
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
    return;
  }

  ble_hs_cfg.sync_cb = on_sync;
  ble_hs_cfg.reset_cb = on_reset;
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

  err = nimble_port_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(err));
    return;
  }
  ble_store_config_init();
  nimble_port_freertos_init(host_task);
}

void NimBLEAdvertiser::advertise() {
  this->pending_duration_ms_ = this->pending_duration_ms_ != 0 ? this->pending_duration_ms_
                                                                 : this->default_duration_ms_;
  this->has_pending_.store(true);
  // Fast path: start synchronously when idle so is_advertising() is true
  // immediately after the action returns (ble_gap_adv_start is thread-safe
  // from any task). When already advertising, loop() does the deferred
  // stop->restart — back-to-back stop();start() in the host task is
  // forbidden (ble_gap.h:2482).
  if (this->synced_.load() && !this->advertising_.load() && ble_gap_adv_active() == 0) {
    this->start_advertising_();
  }
}

void NimBLEAdvertiser::stop_advertising() {
  this->has_pending_.store(false);
  this->stop_requested_.store(false);
  this->advertising_.store(false);
  if (this->synced_.load() && ble_gap_adv_active() != 0) {
    ble_gap_adv_stop();
  }
}

void NimBLEAdvertiser::start_advertising_() {
  if (this->pending_data_.empty() || this->pending_data_.size() > BLE_HS_ADV_MAX_SZ) {
    ESP_LOGW(TAG, "Dropping advertise: payload size %u (max %d)", static_cast<unsigned>(this->pending_data_.size()),
             BLE_HS_ADV_MAX_SZ);
    this->has_pending_.store(false);
    return;
  }

  int rc = ble_gap_adv_set_data(this->pending_data_.data(), static_cast<int>(this->pending_data_.size()));
  if (rc != 0) {
    ESP_LOGW(TAG, "ble_gap_adv_set_data failed: %d", rc);
    this->has_pending_.store(false);
    return;
  }

  struct ble_gap_adv_params params{};
  params.conn_mode = BLE_GAP_CONN_MODE_NON;
  params.disc_mode = BLE_GAP_DISC_MODE_GEN;
  params.itvl_min = static_cast<uint16_t>(this->min_interval_ms_ / 0.625f);
  params.itvl_max = static_cast<uint16_t>(this->max_interval_ms_ / 0.625f);
  params.channel_map = 0;  // 37,38,39
  params.filter_policy = BLE_HCI_ADV_FILT_NONE;
  params.high_duty_cycle = 0;

  int32_t duration = this->pending_duration_ms_ != 0 ? this->pending_duration_ms_ : this->default_duration_ms_;
  rc = ble_gap_adv_start(this->own_addr_type_, nullptr, duration, &params, gap_event, this);
  if (rc != 0) {
    ESP_LOGW(TAG, "ble_gap_adv_start failed: %d", rc);
    this->has_pending_.store(false);
    return;
  }
  this->has_pending_.store(false);
  this->stop_requested_.store(false);
  this->advertising_.store(true);
  this->adv_start_ms_ = millis();
  this->adv_duration_ms_ = duration;
}

void NimBLEAdvertiser::loop() {
  // Own duration timer: don't rely on the stack timer / ble_gap_adv_active()
  // op clearing — both were observed stuck, making wait_until eat its full
  // timeout. User stop is silent, so this loop is the reliable signal.
  if (this->advertising_.load() && this->adv_duration_ms_ != BLE_HS_FOREVER &&
      (millis() - this->adv_start_ms_) >= static_cast<uint32_t>(this->adv_duration_ms_)) {
    this->advertising_.store(false);
    if (ble_gap_adv_active() != 0) {
      ble_gap_adv_stop();
    }
  }
  if (!this->has_pending_.load() || !this->synced_.load()) {
    return;
  }
  if (this->advertising_.load() || ble_gap_adv_active() != 0) {
    // User stop is silent (no ADV_COMPLETE), so loop() poll is the
    // reliable idle signal before restarting with new data.
    if (!this->stop_requested_.load()) {
      this->stop_requested_.store(true);
      ble_gap_adv_stop();
    }
    return;
  }
  this->start_advertising_();
}

}  // namespace esphome::nimble_advertiser

#endif
