import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import CONF_ID
from . import ChargeControllerComponent, charge_controller_ns

CONF_CHARGE_CONTROLLER_ID = "charge_controller_id"
CONF_CHARGING_ENABLED = "charging_enabled"

ChargeControllerSwitch = charge_controller_ns.class_(
    "ChargeControllerSwitch", switch.Switch, cg.Component
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_CHARGE_CONTROLLER_ID): cv.use_id(
            ChargeControllerComponent
        ),
        cv.Optional(CONF_CHARGING_ENABLED): switch.switch_schema(
            ChargeControllerSwitch,
            icon="mdi:battery-charging",
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_CHARGE_CONTROLLER_ID])

    if charging_enabled := config.get(CONF_CHARGING_ENABLED):
        sw = await switch.new_switch(charging_enabled)
        cg.add(parent.set_charging_switch(sw))
