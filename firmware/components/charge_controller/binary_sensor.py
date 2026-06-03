import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import CONF_ID
from . import ChargeControllerComponent

CONF_CHARGE_CONTROLLER_ID = "charge_controller_id"
CONF_SYSTEM_HEALTHY = "system_healthy"
CONF_RELAY_STATE = "relay_state"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_CHARGE_CONTROLLER_ID): cv.use_id(
            ChargeControllerComponent
        ),
        cv.Optional(CONF_SYSTEM_HEALTHY): binary_sensor.binary_sensor_schema(
            icon="mdi:check-circle",
            device_class="safety",
        ),
        cv.Optional(CONF_RELAY_STATE): binary_sensor.binary_sensor_schema(
            icon="mdi:electric-switch",
            device_class="power",
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_CHARGE_CONTROLLER_ID])

    if system_healthy := config.get(CONF_SYSTEM_HEALTHY):
        bs = await binary_sensor.new_binary_sensor(system_healthy)
        cg.add(parent.set_system_healthy_binary_sensor(bs))

    if relay_state := config.get(CONF_RELAY_STATE):
        bs = await binary_sensor.new_binary_sensor(relay_state)
        cg.add(parent.set_relay_state_binary_sensor(bs))
