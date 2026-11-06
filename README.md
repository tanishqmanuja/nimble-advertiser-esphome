# nimble-advertiser-esphome

ESPHome external component for connectionless BLE advertising with NimBLE
on ESP32. Broadcast-only (no peripheral/central/GATT).

## Usage

```yaml
external_components:
  - source:
      type: local
      path: ./components  # or: type: git, url: <this-repo-url>

nimble_advertiser:
  id: nimble_adv
  min_interval: 20ms
  max_interval: 40ms
  default_duration: 400ms
  own_addr_type: public  # public | random
  tx_power: "3dBm"       # -12 -9 -6 -3 0 3 6 9

button:
  - platform: template
    name: "Advertise once"
    on_press:
      - nimble_advertiser.advertise:
          duration: 400ms  # or "forever", or omit for default
          lambda: "return std::vector<uint8_t>{0x02,0x01,0x06};"
```

Actions: `nimble_advertiser.advertise` (required `lambda` returning
`std::vector<uint8_t>`, optional `duration`), `nimble_advertiser.stop`.

In lambda you can call `id(nimble_adv).set_tx_power_dbm(int)` and check
`is_advertising()` / `is_pending()`; see `example/nimble.local.yaml`.

## Example

See [`example/nimble.local.yaml`](example/nimble.local.yaml).

## Notes

- ESP32 + ESP-IDF only. `nimble_advertiser` claims the Bluetooth controller
  (BLE-only); don't combine with `esp32_ble_tracker` / `esp32_ble_client`.
- Payload must fit in `BLE_HS_ADV_MAX_SZ` (31 bytes for legacy advertising).
- Intervals: 20-10240 ms, `min_interval <= max_interval`.
