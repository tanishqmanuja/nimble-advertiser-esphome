import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components.esp32 import add_idf_sdkconfig_option, request_bluetooth
from esphome.const import CONF_ID, CONF_LAMBDA, CONF_TX_POWER
from esphome.core import TimePeriod
from esphome.types import ConfigType

CODEOWNERS = []
DEPENDENCIES = ["esp32"]
DOMAIN = "nimble_advertiser"

nimble_advertiser_ns = cg.esphome_ns.namespace("nimble_advertiser")
NimBLEAdvertiser = nimble_advertiser_ns.class_("NimBLEAdvertiser", cg.Component)

CONF_MIN_INTERVAL = "min_interval"
CONF_MAX_INTERVAL = "max_interval"
CONF_DEFAULT_DURATION = "default_duration"
CONF_DURATION = "duration"
CONF_OWN_ADDR_TYPE = "own_addr_type"

BLE_HS_FOREVER = 2147483647  # INT32_MAX, host/ble_hs.h

esp_power_level_t = cg.global_ns.enum("esp_power_level_t")
TX_POWER_LEVELS = {
    -12: esp_power_level_t.ESP_PWR_LVL_N12,
    -9: esp_power_level_t.ESP_PWR_LVL_N9,
    -6: esp_power_level_t.ESP_PWR_LVL_N6,
    -3: esp_power_level_t.ESP_PWR_LVL_N3,
    0: esp_power_level_t.ESP_PWR_LVL_N0,
    3: esp_power_level_t.ESP_PWR_LVL_P3,
    6: esp_power_level_t.ESP_PWR_LVL_P6,
    9: esp_power_level_t.ESP_PWR_LVL_P9,
}

OWN_ADDR_TYPES = {"public": 0, "random": 1}


def _duration_ms(value):
    if isinstance(value, str) and value.lower() == "forever":
        return BLE_HS_FOREVER
    period = cv.positive_time_period_milliseconds(value)
    ms = period.total_milliseconds
    if ms == 0:
        raise cv.Invalid("duration must be > 0ms or 'forever'")
    return ms


def _validate_intervals(config: ConfigType) -> ConfigType:
    if config[CONF_MIN_INTERVAL] > config.get(CONF_MAX_INTERVAL):
        raise cv.Invalid("min_interval must be <= max_interval")
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(NimBLEAdvertiser),
            cv.Optional(CONF_MIN_INTERVAL, default="20ms"): cv.All(
                cv.positive_time_period_milliseconds,
                cv.Range(
                    min=TimePeriod(milliseconds=20), max=TimePeriod(milliseconds=10240)
                ),
            ),
            cv.Optional(CONF_MAX_INTERVAL, default="40ms"): cv.All(
                cv.positive_time_period_milliseconds,
                cv.Range(
                    min=TimePeriod(milliseconds=20), max=TimePeriod(milliseconds=10240)
                ),
            ),
            cv.Optional(CONF_DEFAULT_DURATION, default="400ms"): _duration_ms,
            cv.Optional(CONF_OWN_ADDR_TYPE, default="public"): cv.enum(
                OWN_ADDR_TYPES, lower=True
            ),
            cv.Optional(CONF_TX_POWER, default="3dBm"): cv.All(
                cv.decibel, cv.enum(TX_POWER_LEVELS, int=True)
            ),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _validate_intervals,
)


async def to_code(config: ConfigType) -> None:
    cg.add_define("USE_NIMBLE_ADVERTISER")

    add_idf_sdkconfig_option("CONFIG_BT_ENABLED", True)
    add_idf_sdkconfig_option("CONFIG_BT_BLUEDROID_ENABLED", False)
    add_idf_sdkconfig_option("CONFIG_BT_NIMBLE_ENABLED", True)
    add_idf_sdkconfig_option("CONFIG_BT_NIMBLE_ROLE_BROADCASTER", True)
    add_idf_sdkconfig_option("CONFIG_BT_NIMBLE_ROLE_PERIPHERAL", False)
    add_idf_sdkconfig_option("CONFIG_BT_NIMBLE_ROLE_CENTRAL", False)
    add_idf_sdkconfig_option("CONFIG_BT_NIMBLE_GATT_CLIENT", False)
    add_idf_sdkconfig_option("CONFIG_BT_NIMBLE_GATT_SERVER", False)
    add_idf_sdkconfig_option("CONFIG_BT_NIMBLE_MAX_CONNECTIONS", 1)
    add_idf_sdkconfig_option("CONFIG_BT_NIMBLE_MSYS_1_BLOCK_COUNT", 10)
    add_idf_sdkconfig_option("CONFIG_BT_NIMBLE_EXT_ADV", False)
    add_idf_sdkconfig_option("CONFIG_BT_CONTROLLER_ENABLED", True)
    add_idf_sdkconfig_option("CONFIG_BTDM_CTRL_MODE_BLE_ONLY", True)
    add_idf_sdkconfig_option("CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY", False)
    add_idf_sdkconfig_option("CONFIG_BTDM_CTRL_MODE_BTDM", False)

    request_bluetooth()

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_min_interval(config[CONF_MIN_INTERVAL].total_milliseconds))
    cg.add(var.set_max_interval(config[CONF_MAX_INTERVAL].total_milliseconds))
    cg.add(var.set_default_duration(config[CONF_DEFAULT_DURATION]))
    cg.add(var.set_own_addr_type(config[CONF_OWN_ADDR_TYPE]))
    cg.add(var.set_tx_power(config[CONF_TX_POWER]))


ADVERTISE_ACTION_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(NimBLEAdvertiser),
        cv.Required(CONF_LAMBDA): cv.lambda_,
        # Templatable: static "400ms"/"forever" (via _duration_ms -> int32 ms)
        # or a lambda returning int32 ms, e.g. reading a number entity.
        cv.Optional(CONF_DURATION): cv.templatable(_duration_ms),
    }
)

automation.register_apply_action(
    "nimble_advertiser.advertise",
    ADVERTISE_ACTION_SCHEMA,
    automation.ApplyField(
        CONF_LAMBDA, "set_advertise_data", cg.std_vector.template(cg.uint8)
    ),
    automation.ApplyField(CONF_DURATION, "set_advertise_duration", cg.int32),
    automation.ApplyCall("advertise()"),
)

STOP_ACTION_SCHEMA = automation.maybe_simple_id(
    cv.Schema({cv.GenerateID(): cv.use_id(NimBLEAdvertiser)})
)

automation.register_apply_action(
    "nimble_advertiser.stop",
    STOP_ACTION_SCHEMA,
    automation.ApplyCall("stop_advertising()"),
)
