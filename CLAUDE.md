# ESP Power Hub

ESPHome-based firmware for managing a 48V LiFePO4 battery system (2x 16S packs with Seplos V3 BMS units) charged by an Emerson/Vertiv R48-3000e3 rectifier. Phase 1 targets LilyGo T-CAN485 dev board.

## Build & Validate

```bash
pip install esphome
cd firmware
esphome config esp-power-hub.yaml   # Validate YAML config
esphome compile esp-power-hub.yaml  # Compile firmware (requires ESP-IDF toolchain)
```

A `firmware/secrets.yaml` file is required (gitignored). Template:
```yaml
wifi_ssid: "your_ssid"
wifi_password: "your_password"
ap_password: "fallback_ap_password"
ota_password: "ota_password"
api_key: "<base64-encoded 32-byte key>"
```

Generate an API key: `python3 -c "import base64, os; print(base64.b64encode(os.urandom(32)).decode())"`

## Architecture

### Project Structure

```
firmware/
  esp-power-hub.yaml                    # Main ESPHome config (T-CAN485)
  secrets.yaml                          # Credentials (gitignored)
  packages/
    bms_pack.yaml                       # Reusable BMS sensor template (parameterized)
  components/
    charge_controller/                  # Custom ESPHome component (C++ + Python)
      __init__.py                       # Component config schema & code generation
      charge_controller.h               # C++ header: state machine, safety, aggregation
      charge_controller.cpp             # C++ implementation
      switch.py                         # charging_enabled switch platform
      text_sensor.py                    # charge_state, alarm_reason platforms
      binary_sensor.py                  # system_healthy, relay_state platforms
      number.py                         # 10 configurable parameter number entities
```

### Key Components

**BMS Communication**: Uses ESPHome's built-in `modbus_controller` with Seplos V3 register map (from syssi/esphome-seplos-bms). `register_type: read` (Modbus function code 0x04, input registers - NOT `holding`). UART at 19200 baud. Temperature conversion: offset -2731.5, multiply 0.1 (raw is 0.1 Kelvin).

**Rectifier**: External component at `github://Kylevdm/esphome-emerson-vertiv-r48`. CAN bus at 125kbps. 30-second TTL failsafe (rectifier reverts to safe defaults if ESP stops communicating).

**Charge Controller** (`charge_controller`): The only custom component. Handles:
- Cross-pack BMS aggregation (min/max cell voltage, max temp, avg SOC, pack delta)
- Charging state machine: STANDBY -> BULK -> ABSORB -> FLOAT (+ ALARM)
- Safety checks in `loop()` (~16ms cycle, cannot be overridden by HA)
- Relay control (defaults OPEN; GPIO LOW = open)
- Rectifier control via ESPHome entity API (number::make_call, switch::turn_on/off)

### Safety Design

Relay defaults OPEN (GPIO LOW on boot). Safety checks run every `loop()` iteration:
1. BMS comms timeout (any pack sensor stale > 30s)
2. Rectifier comms timeout (> 30s)
3. Cell overvoltage (any cell > 3.65V)
4. Cell undervoltage (any cell < 2.5V)
5. Pack overvoltage (> 57V hard-coded)
6. Over temperature (> 45C)
7. Pack-to-pack voltage mismatch (> 1V delta)

Any violation -> ALARM state, relay OPEN, rectifier DC OFF. Manual reset via HA.

### T-CAN485 Pin Map

| Function | GPIO | Notes |
|----------|------|-------|
| CAN TX/RX | 27/26 | SN65HVD231 transceiver |
| RS485 TX/RX | 22/21 | MAX13487E auto-direction |
| RS485 SE | 17 | Inverted; LOW = auto-dir active |
| RS485 ~RE | 19 | Inverted |
| RS485 5V | 16 | Inverted; LOW = enabled |
| Relay | 32 | External relay module |
| WS2812 LED | 4 | Status indicator |

### Inter-Component Communication

The charge controller references BMS sensors and rectifier entities by ID (entity API pattern - no header coupling). Pack sensors are passed via `add_pack()`, rectifier entities via `set_rectifier()`. All wiring happens in ESPHome's generated `main.cpp`.

## Code Conventions

- ESPHome component pattern: Python config (`__init__.py`) + C++ implementation
- ESP-IDF framework (not Arduino)
- YAML package template with `!include` and `vars` substitutions for reusable BMS config
- Data-driven Python config (see `number.py` NUMBER_CONFIGS dict pattern)
- Safety-critical code in C++ `loop()`, not ESPHome automations or lambdas

## Known Issues

- `emerson_r48` external component has bugs: `switch/__init__.py` and `number/__init__.py` use `config[KEY]` instead of `config.get(KEY)` for optional keys (`fan_sw`, `led_sw`, `max_input_current`). Workaround: always declare these entities in YAML even if unused.

## Phase 2 Notes

Migration from T-CAN485 to custom PCB requires only pin reassignment and adding `flow_control_pin: GPIO27` for manual RS485 DE/RE control. All component logic stays identical.
