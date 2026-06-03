import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import CONF_ID
from . import ChargeControllerComponent

CONF_CHARGE_CONTROLLER_ID = "charge_controller_id"
CONF_CHARGE_STATE = "charge_state"
CONF_ALARM_REASON = "alarm_reason"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_CHARGE_CONTROLLER_ID): cv.use_id(
            ChargeControllerComponent
        ),
        cv.Optional(CONF_CHARGE_STATE): text_sensor.text_sensor_schema(
            icon="mdi:battery-sync",
        ),
        cv.Optional(CONF_ALARM_REASON): text_sensor.text_sensor_schema(
            icon="mdi:alert-circle",
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_CHARGE_CONTROLLER_ID])

    if charge_state := config.get(CONF_CHARGE_STATE):
        ts = await text_sensor.new_text_sensor(charge_state)
        cg.add(parent.set_charge_state_text_sensor(ts))

    if alarm_reason := config.get(CONF_ALARM_REASON):
        ts = await text_sensor.new_text_sensor(alarm_reason)
        cg.add(parent.set_alarm_reason_text_sensor(ts))
