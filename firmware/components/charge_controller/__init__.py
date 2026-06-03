import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import sensor, switch as switch_ns, number as number_ns
from esphome.const import CONF_ID

DEPENDENCIES = []
AUTO_LOAD = ["sensor", "text_sensor", "binary_sensor", "switch", "number"]

CONF_RELAY_PIN = "relay_pin"
CONF_PACKS = "packs"
CONF_RECTIFIER = "rectifier"
CONF_MIN_CELL_VOLTAGE = "min_cell_voltage"
CONF_MAX_CELL_VOLTAGE = "max_cell_voltage"
CONF_PACK_VOLTAGE = "pack_voltage"
CONF_PACK_CURRENT = "pack_current"
CONF_SOC = "soc"
CONF_MAX_TEMPERATURE = "max_temperature"
CONF_VOLTAGE_SENSOR = "voltage_sensor"
CONF_CURRENT_SENSOR = "current_sensor"
CONF_VOLTAGE_NUMBER = "voltage_number"
CONF_CURRENT_NUMBER = "current_number"
CONF_DC_SWITCH = "dc_switch"

charge_controller_ns = cg.esphome_ns.namespace("charge_controller")
ChargeControllerComponent = charge_controller_ns.class_(
    "ChargeControllerComponent", cg.Component
)

PACK_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_MIN_CELL_VOLTAGE): cv.use_id(sensor.Sensor),
        cv.Required(CONF_MAX_CELL_VOLTAGE): cv.use_id(sensor.Sensor),
        cv.Required(CONF_PACK_VOLTAGE): cv.use_id(sensor.Sensor),
        cv.Required(CONF_PACK_CURRENT): cv.use_id(sensor.Sensor),
        cv.Required(CONF_SOC): cv.use_id(sensor.Sensor),
        cv.Required(CONF_MAX_TEMPERATURE): cv.use_id(sensor.Sensor),
    }
)

RECTIFIER_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_VOLTAGE_SENSOR): cv.use_id(sensor.Sensor),
        cv.Required(CONF_CURRENT_SENSOR): cv.use_id(sensor.Sensor),
        cv.Required(CONF_VOLTAGE_NUMBER): cv.use_id(number_ns.Number),
        cv.Required(CONF_CURRENT_NUMBER): cv.use_id(number_ns.Number),
        cv.Required(CONF_DC_SWITCH): cv.use_id(switch_ns.Switch),
    }
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(ChargeControllerComponent),
        cv.Required(CONF_RELAY_PIN): pins.gpio_output_pin_schema,
        cv.Required(CONF_PACKS): cv.All(
            cv.ensure_list(PACK_SCHEMA), cv.Length(min=1, max=4)
        ),
        cv.Required(CONF_RECTIFIER): RECTIFIER_SCHEMA,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    relay_pin = await cg.gpio_pin_expression(config[CONF_RELAY_PIN])
    cg.add(var.set_relay_pin(relay_pin))

    for i, pack_conf in enumerate(config[CONF_PACKS]):
        min_cv = await cg.get_variable(pack_conf[CONF_MIN_CELL_VOLTAGE])
        max_cv = await cg.get_variable(pack_conf[CONF_MAX_CELL_VOLTAGE])
        pack_v = await cg.get_variable(pack_conf[CONF_PACK_VOLTAGE])
        pack_i = await cg.get_variable(pack_conf[CONF_PACK_CURRENT])
        soc = await cg.get_variable(pack_conf[CONF_SOC])
        max_t = await cg.get_variable(pack_conf[CONF_MAX_TEMPERATURE])
        cg.add(var.add_pack(i, min_cv, max_cv, pack_v, pack_i, soc, max_t))

    rect = config[CONF_RECTIFIER]
    rect_v_sens = await cg.get_variable(rect[CONF_VOLTAGE_SENSOR])
    rect_i_sens = await cg.get_variable(rect[CONF_CURRENT_SENSOR])
    rect_v_num = await cg.get_variable(rect[CONF_VOLTAGE_NUMBER])
    rect_i_num = await cg.get_variable(rect[CONF_CURRENT_NUMBER])
    rect_dc_sw = await cg.get_variable(rect[CONF_DC_SWITCH])
    cg.add(var.set_rectifier(rect_v_sens, rect_i_sens, rect_v_num, rect_i_num, rect_dc_sw))
