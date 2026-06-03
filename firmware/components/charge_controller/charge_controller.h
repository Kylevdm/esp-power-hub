#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/number/number.h"

#include <vector>
#include <string>
#include <cmath>

namespace esphome {
namespace charge_controller {

enum class ChargeState : uint8_t {
  STANDBY = 0,
  BULK = 1,
  ABSORB = 2,
  FLOAT = 3,
  ALARM = 4,
};

struct PackData {
  sensor::Sensor *min_cell_voltage{nullptr};
  sensor::Sensor *max_cell_voltage{nullptr};
  sensor::Sensor *pack_voltage{nullptr};
  sensor::Sensor *pack_current{nullptr};
  sensor::Sensor *soc{nullptr};
  sensor::Sensor *max_temperature{nullptr};
  uint32_t last_update_ms{0};
};

class ChargeControllerNumber : public number::Number, public Component {
 public:
  void setup() override {
    float val;
    if (!this->restore_value_) {
      val = this->initial_value_;
    } else {
      this->pref_ = global_preferences->make_preference<float>(this->get_object_id_hash());
      if (!this->pref_.load(&val))
        val = this->initial_value_;
    }
    this->publish_state(val);
  }
  void set_initial_value(float val) { this->initial_value_ = val; }
  void set_restore_value(bool restore) { this->restore_value_ = restore; }

 protected:
  void control(float value) override {
    this->publish_state(value);
    if (this->restore_value_)
      this->pref_.save(&value);
  }
  float initial_value_{0.0f};
  bool restore_value_{false};
  ESPPreferenceObject pref_;
};

class ChargeControllerSwitch : public switch_::Switch, public Component {
 public:
  void setup() override { this->publish_state(false); }

 protected:
  void write_state(bool state) override { this->publish_state(state); }
};

class ChargeControllerComponent : public Component {
 public:
  float get_setup_priority() const override { return setup_priority::LATE; }
  void setup() override;
  void loop() override;
  void dump_config() override;

  void set_relay_pin(GPIOPin *pin) { this->relay_pin_ = pin; }

  void add_pack(int index, sensor::Sensor *min_cv, sensor::Sensor *max_cv,
                sensor::Sensor *pack_v, sensor::Sensor *pack_i,
                sensor::Sensor *soc, sensor::Sensor *max_t);

  void set_rectifier(sensor::Sensor *voltage_sensor,
                     sensor::Sensor *current_sensor,
                     number::Number *voltage_number,
                     number::Number *current_number,
                     switch_::Switch *dc_switch);

  void set_charging_switch(ChargeControllerSwitch *sw) { this->charging_switch_ = sw; }
  void set_charge_state_text_sensor(text_sensor::TextSensor *ts) { this->charge_state_ts_ = ts; }
  void set_alarm_reason_text_sensor(text_sensor::TextSensor *ts) { this->alarm_reason_ts_ = ts; }
  void set_system_healthy_binary_sensor(binary_sensor::BinarySensor *bs) { this->system_healthy_bs_ = bs; }
  void set_relay_state_binary_sensor(binary_sensor::BinarySensor *bs) { this->relay_state_bs_ = bs; }

  void set_bulk_voltage_number(ChargeControllerNumber *n) { this->bulk_voltage_num_ = n; }
  void set_float_voltage_number(ChargeControllerNumber *n) { this->float_voltage_num_ = n; }
  void set_max_charge_current_number(ChargeControllerNumber *n) { this->max_charge_current_num_ = n; }
  void set_absorb_tail_current_number(ChargeControllerNumber *n) { this->absorb_tail_current_num_ = n; }
  void set_absorb_max_time_number(ChargeControllerNumber *n) { this->absorb_max_time_num_ = n; }
  void set_rebulk_soc_number(ChargeControllerNumber *n) { this->rebulk_soc_num_ = n; }
  void set_alarm_cell_high_number(ChargeControllerNumber *n) { this->alarm_cell_high_num_ = n; }
  void set_alarm_cell_low_number(ChargeControllerNumber *n) { this->alarm_cell_low_num_ = n; }
  void set_alarm_temp_high_number(ChargeControllerNumber *n) { this->alarm_temp_high_num_ = n; }
  void set_comms_timeout_number(ChargeControllerNumber *n) { this->comms_timeout_num_ = n; }

 protected:
  void check_safety_();
  void update_state_machine_();
  void open_relay_();
  void close_relay_();
  void transition_to_(ChargeState new_state);
  void set_rectifier_voltage_(float voltage);
  void set_rectifier_current_(float percent);
  void set_rectifier_dc_(bool on);
  void publish_state_();

  float get_param_(ChargeControllerNumber *num, float default_val);
  float get_min_cell_voltage_all_packs_();
  float get_max_cell_voltage_all_packs_();
  float get_max_temperature_all_packs_();
  float get_max_pack_voltage_all_packs_();
  float get_avg_soc_all_packs_();
  float get_max_pack_voltage_delta_();
  bool all_packs_online_(uint32_t timeout_ms);
  bool rectifier_online_(uint32_t timeout_ms);
  bool all_sensors_valid_();
  const char *state_to_string_(ChargeState state);

  GPIOPin *relay_pin_{nullptr};
  bool relay_closed_{false};

  std::vector<PackData> packs_;

  sensor::Sensor *rect_voltage_sensor_{nullptr};
  sensor::Sensor *rect_current_sensor_{nullptr};
  number::Number *rect_voltage_number_{nullptr};
  number::Number *rect_current_number_{nullptr};
  switch_::Switch *rect_dc_switch_{nullptr};
  uint32_t rect_last_update_ms_{0};

  ChargeControllerSwitch *charging_switch_{nullptr};
  text_sensor::TextSensor *charge_state_ts_{nullptr};
  text_sensor::TextSensor *alarm_reason_ts_{nullptr};
  binary_sensor::BinarySensor *system_healthy_bs_{nullptr};
  binary_sensor::BinarySensor *relay_state_bs_{nullptr};

  ChargeControllerNumber *bulk_voltage_num_{nullptr};
  ChargeControllerNumber *float_voltage_num_{nullptr};
  ChargeControllerNumber *max_charge_current_num_{nullptr};
  ChargeControllerNumber *absorb_tail_current_num_{nullptr};
  ChargeControllerNumber *absorb_max_time_num_{nullptr};
  ChargeControllerNumber *rebulk_soc_num_{nullptr};
  ChargeControllerNumber *alarm_cell_high_num_{nullptr};
  ChargeControllerNumber *alarm_cell_low_num_{nullptr};
  ChargeControllerNumber *alarm_temp_high_num_{nullptr};
  ChargeControllerNumber *comms_timeout_num_{nullptr};

  ChargeState state_{ChargeState::STANDBY};
  uint32_t state_enter_time_{0};
  std::string alarm_reason_;
  uint32_t last_publish_ms_{0};
  bool boot_complete_{false};
  uint32_t boot_time_{0};

  static constexpr float DEFAULT_BULK_VOLTAGE = 55.2f;
  static constexpr float DEFAULT_FLOAT_VOLTAGE = 54.0f;
  static constexpr float DEFAULT_MAX_CHARGE_CURRENT = 100.0f;
  static constexpr float DEFAULT_ABSORB_TAIL_CURRENT = 2.0f;
  static constexpr float DEFAULT_ABSORB_MAX_TIME = 3600.0f;
  static constexpr float DEFAULT_REBULK_SOC = 85.0f;
  static constexpr float DEFAULT_ALARM_CELL_HIGH = 3.65f;
  static constexpr float DEFAULT_ALARM_CELL_LOW = 2.5f;
  static constexpr float DEFAULT_ALARM_TEMP_HIGH = 45.0f;
  static constexpr float DEFAULT_COMMS_TIMEOUT = 30.0f;
  static constexpr uint32_t BOOT_GRACE_PERIOD_MS = 60000;
  static constexpr uint32_t PUBLISH_INTERVAL_MS = 1000;
  static constexpr uint32_t RECTIFIER_COMMAND_INTERVAL_MS = 5000;

  uint32_t last_rectifier_command_ms_{0};
};

}  // namespace charge_controller
}  // namespace esphome
