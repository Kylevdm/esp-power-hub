import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import CONF_ID, UNIT_VOLT, UNIT_AMPERE, UNIT_CELSIUS, UNIT_PERCENT, UNIT_SECOND
from . import ChargeControllerComponent, charge_controller_ns

CONF_CHARGE_CONTROLLER_ID = "charge_controller_id"
CONF_BULK_VOLTAGE = "bulk_voltage"
CONF_FLOAT_VOLTAGE = "float_voltage"
CONF_MAX_CHARGE_CURRENT = "max_charge_current"
CONF_ABSORB_TAIL_CURRENT = "absorb_tail_current"
CONF_ABSORB_MAX_TIME = "absorb_max_time"
CONF_REBULK_SOC = "rebulk_soc"
CONF_ALARM_CELL_HIGH = "alarm_cell_high"
CONF_ALARM_CELL_LOW = "alarm_cell_low"
CONF_ALARM_TEMP_HIGH = "alarm_temp_high"
CONF_COMMS_TIMEOUT = "comms_timeout"

ChargeControllerNumber = charge_controller_ns.class_(
    "ChargeControllerNumber", number.Number, cg.Component
)

NUMBER_CONFIGS = {
    CONF_BULK_VOLTAGE: {
        "icon": "mdi:flash",
        "unit": UNIT_VOLT,
        "min": 50.0,
        "max": 57.0,
        "step": 0.1,
        "default": 55.2,
        "setter": "set_bulk_voltage_number",
    },
    CONF_FLOAT_VOLTAGE: {
        "icon": "mdi:flash-outline",
        "unit": UNIT_VOLT,
        "min": 48.0,
        "max": 55.0,
        "step": 0.1,
        "default": 54.0,
        "setter": "set_float_voltage_number",
    },
    CONF_MAX_CHARGE_CURRENT: {
        "icon": "mdi:current-dc",
        "unit": UNIT_PERCENT,
        "min": 10.0,
        "max": 121.0,
        "step": 1.0,
        "default": 100.0,
        "setter": "set_max_charge_current_number",
    },
    CONF_ABSORB_TAIL_CURRENT: {
        "icon": "mdi:current-ac",
        "unit": UNIT_AMPERE,
        "min": 0.5,
        "max": 10.0,
        "step": 0.5,
        "default": 2.0,
        "setter": "set_absorb_tail_current_number",
    },
    CONF_ABSORB_MAX_TIME: {
        "icon": "mdi:timer",
        "unit": UNIT_SECOND,
        "min": 300.0,
        "max": 7200.0,
        "step": 60.0,
        "default": 3600.0,
        "setter": "set_absorb_max_time_number",
    },
    CONF_REBULK_SOC: {
        "icon": "mdi:battery-arrow-down",
        "unit": UNIT_PERCENT,
        "min": 50.0,
        "max": 95.0,
        "step": 1.0,
        "default": 85.0,
        "setter": "set_rebulk_soc_number",
    },
    CONF_ALARM_CELL_HIGH: {
        "icon": "mdi:alert",
        "unit": UNIT_VOLT,
        "min": 3.5,
        "max": 3.7,
        "step": 0.01,
        "default": 3.65,
        "setter": "set_alarm_cell_high_number",
    },
    CONF_ALARM_CELL_LOW: {
        "icon": "mdi:alert-outline",
        "unit": UNIT_VOLT,
        "min": 2.0,
        "max": 2.8,
        "step": 0.01,
        "default": 2.5,
        "setter": "set_alarm_cell_low_number",
    },
    CONF_ALARM_TEMP_HIGH: {
        "icon": "mdi:thermometer-alert",
        "unit": UNIT_CELSIUS,
        "min": 35.0,
        "max": 55.0,
        "step": 1.0,
        "default": 45.0,
        "setter": "set_alarm_temp_high_number",
    },
    CONF_COMMS_TIMEOUT: {
        "icon": "mdi:timer-alert",
        "unit": UNIT_SECOND,
        "min": 10.0,
        "max": 120.0,
        "step": 5.0,
        "default": 30.0,
        "setter": "set_comms_timeout_number",
    },
}

schema_entries = {
    cv.GenerateID(CONF_CHARGE_CONTROLLER_ID): cv.use_id(ChargeControllerComponent),
}

for key, cfg in NUMBER_CONFIGS.items():
    schema_entries[cv.Optional(key)] = number.number_schema(
        ChargeControllerNumber,
        icon=cfg["icon"],
        unit_of_measurement=cfg["unit"],
    )

CONFIG_SCHEMA = cv.Schema(schema_entries)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_CHARGE_CONTROLLER_ID])

    for key, cfg in NUMBER_CONFIGS.items():
        if conf := config.get(key):
            num = await number.new_number(
                conf,
                min_value=cfg["min"],
                max_value=cfg["max"],
                step=cfg["step"],
            )
            await cg.register_component(num, conf)
            cg.add(num.set_initial_value(cfg["default"]))
            cg.add(num.set_restore_value(True))
            cg.add(getattr(parent, cfg["setter"])(num))
