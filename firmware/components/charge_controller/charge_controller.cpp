#include "charge_controller.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

namespace esphome {
namespace charge_controller {

static const char *const TAG = "charge_controller";

void ChargeControllerComponent::setup() {
  this->relay_pin_->setup();
  this->open_relay_();

  this->boot_time_ = millis();
  this->boot_complete_ = false;
  this->state_ = ChargeState::STANDBY;
  this->state_enter_time_ = millis();

  for (auto &pack : this->packs_) {
    pack.last_update_ms = 0;

    pack.min_cell_voltage->add_on_state_callback([this, &pack](float val) {
      if (!std::isnan(val))
        pack.last_update_ms = millis();
    });
    pack.pack_voltage->add_on_state_callback([this, &pack](float val) {
      if (!std::isnan(val))
        pack.last_update_ms = millis();
    });
  }

  if (this->rect_voltage_sensor_ != nullptr) {
    this->rect_voltage_sensor_->add_on_state_callback([this](float val) {
      if (!std::isnan(val))
        this->rect_last_update_ms_ = millis();
    });
  }

  ESP_LOGI(TAG, "Charge controller initialized with %d packs, relay OPEN", this->packs_.size());
  this->publish_state_();
}

void ChargeControllerComponent::loop() {
  if (!this->boot_complete_) {
    if (millis() - this->boot_time_ > BOOT_GRACE_PERIOD_MS) {
      if (this->all_sensors_valid_()) {
        this->boot_complete_ = true;
        ESP_LOGI(TAG, "Boot grace period complete, all sensors valid");
      }
    }
    this->publish_state_();
    return;
  }

  this->check_safety_();

  if (this->state_ != ChargeState::ALARM) {
    this->update_state_machine_();
  }

  if (millis() - this->last_rectifier_command_ms_ > RECTIFIER_COMMAND_INTERVAL_MS) {
    this->last_rectifier_command_ms_ = millis();

    switch (this->state_) {
      case ChargeState::BULK: {
        float bulk_v = this->get_param_(this->bulk_voltage_num_, DEFAULT_BULK_VOLTAGE);
        float max_i = this->get_param_(this->max_charge_current_num_, DEFAULT_MAX_CHARGE_CURRENT);
        this->set_rectifier_voltage_(bulk_v);
        this->set_rectifier_current_(max_i);
        break;
      }
      case ChargeState::ABSORB: {
        float bulk_v = this->get_param_(this->bulk_voltage_num_, DEFAULT_BULK_VOLTAGE);
        float max_i = this->get_param_(this->max_charge_current_num_, DEFAULT_MAX_CHARGE_CURRENT);
        this->set_rectifier_voltage_(bulk_v);
        this->set_rectifier_current_(max_i);
        break;
      }
      case ChargeState::FLOAT: {
        float float_v = this->get_param_(this->float_voltage_num_, DEFAULT_FLOAT_VOLTAGE);
        float max_i = this->get_param_(this->max_charge_current_num_, DEFAULT_MAX_CHARGE_CURRENT);
        this->set_rectifier_voltage_(float_v);
        this->set_rectifier_current_(max_i);
        break;
      }
      default:
        break;
    }
  }

  this->publish_state_();
}

void ChargeControllerComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Charge Controller:");
  ESP_LOGCONFIG(TAG, "  Packs: %d", this->packs_.size());
  ESP_LOGCONFIG(TAG, "  Relay Pin: configured");
  ESP_LOGCONFIG(TAG, "  Bulk Voltage: %.1fV", this->get_param_(this->bulk_voltage_num_, DEFAULT_BULK_VOLTAGE));
  ESP_LOGCONFIG(TAG, "  Float Voltage: %.1fV", this->get_param_(this->float_voltage_num_, DEFAULT_FLOAT_VOLTAGE));
}

void ChargeControllerComponent::add_pack(int index, sensor::Sensor *min_cv,
                                          sensor::Sensor *max_cv,
                                          sensor::Sensor *pack_v,
                                          sensor::Sensor *pack_i,
                                          sensor::Sensor *soc,
                                          sensor::Sensor *max_t) {
  if ((size_t) index >= this->packs_.size())
    this->packs_.resize(index + 1);

  auto &pack = this->packs_[index];
  pack.min_cell_voltage = min_cv;
  pack.max_cell_voltage = max_cv;
  pack.pack_voltage = pack_v;
  pack.pack_current = pack_i;
  pack.soc = soc;
  pack.max_temperature = max_t;
}

void ChargeControllerComponent::set_rectifier(sensor::Sensor *voltage_sensor,
                                               sensor::Sensor *current_sensor,
                                               number::Number *voltage_number,
                                               number::Number *current_number,
                                               switch_::Switch *dc_switch) {
  this->rect_voltage_sensor_ = voltage_sensor;
  this->rect_current_sensor_ = current_sensor;
  this->rect_voltage_number_ = voltage_number;
  this->rect_current_number_ = current_number;
  this->rect_dc_switch_ = dc_switch;
}

void ChargeControllerComponent::check_safety_() {
  float comms_timeout_s = this->get_param_(this->comms_timeout_num_, DEFAULT_COMMS_TIMEOUT);
  uint32_t comms_timeout_ms = (uint32_t)(comms_timeout_s * 1000.0f);
  float alarm_cell_high = this->get_param_(this->alarm_cell_high_num_, DEFAULT_ALARM_CELL_HIGH);
  float alarm_cell_low = this->get_param_(this->alarm_cell_low_num_, DEFAULT_ALARM_CELL_LOW);
  float alarm_temp_high = this->get_param_(this->alarm_temp_high_num_, DEFAULT_ALARM_TEMP_HIGH);

  if (!this->all_packs_online_(comms_timeout_ms)) {
    this->alarm_reason_ = "BMS communication timeout";
    ESP_LOGW(TAG, "ALARM: %s", this->alarm_reason_.c_str());
    this->open_relay_();
    this->set_rectifier_dc_(false);
    this->transition_to_(ChargeState::ALARM);
    return;
  }

  if (!this->rectifier_online_(comms_timeout_ms)) {
    this->alarm_reason_ = "Rectifier communication timeout";
    ESP_LOGW(TAG, "ALARM: %s", this->alarm_reason_.c_str());
    this->open_relay_();
    this->transition_to_(ChargeState::ALARM);
    return;
  }

  float max_cv = this->get_max_cell_voltage_all_packs_();
  if (!std::isnan(max_cv) && max_cv > alarm_cell_high) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Cell overvoltage: %.3fV", max_cv);
    this->alarm_reason_ = buf;
    ESP_LOGW(TAG, "ALARM: %s", this->alarm_reason_.c_str());
    this->open_relay_();
    this->set_rectifier_dc_(false);
    this->transition_to_(ChargeState::ALARM);
    return;
  }

  float min_cv = this->get_min_cell_voltage_all_packs_();
  if (!std::isnan(min_cv) && min_cv < alarm_cell_low) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Cell undervoltage: %.3fV", min_cv);
    this->alarm_reason_ = buf;
    ESP_LOGW(TAG, "ALARM: %s", this->alarm_reason_.c_str());
    this->open_relay_();
    this->set_rectifier_dc_(false);
    this->transition_to_(ChargeState::ALARM);
    return;
  }

  float max_pack_v = this->get_max_pack_voltage_all_packs_();
  if (!std::isnan(max_pack_v) && max_pack_v > 57.0f) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Pack overvoltage: %.1fV", max_pack_v);
    this->alarm_reason_ = buf;
    ESP_LOGW(TAG, "ALARM: %s", this->alarm_reason_.c_str());
    this->open_relay_();
    this->set_rectifier_dc_(false);
    this->transition_to_(ChargeState::ALARM);
    return;
  }

  float max_temp = this->get_max_temperature_all_packs_();
  if (!std::isnan(max_temp) && max_temp > alarm_temp_high) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Over temperature: %.1f°C", max_temp);
    this->alarm_reason_ = buf;
    ESP_LOGW(TAG, "ALARM: %s", this->alarm_reason_.c_str());
    this->open_relay_();
    this->set_rectifier_dc_(false);
    this->transition_to_(ChargeState::ALARM);
    return;
  }

  float pack_delta = this->get_max_pack_voltage_delta_();
  if (!std::isnan(pack_delta) && pack_delta > 1.0f) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Pack voltage mismatch: %.1fV delta", pack_delta);
    this->alarm_reason_ = buf;
    ESP_LOGW(TAG, "ALARM: %s", this->alarm_reason_.c_str());
    this->open_relay_();
    this->set_rectifier_dc_(false);
    this->transition_to_(ChargeState::ALARM);
    return;
  }
}

void ChargeControllerComponent::update_state_machine_() {
  bool enabled = this->charging_switch_ != nullptr && this->charging_switch_->state;

  switch (this->state_) {
    case ChargeState::STANDBY: {
      if (!enabled)
        break;

      float avg_soc = this->get_avg_soc_all_packs_();
      float rebulk_soc = this->get_param_(this->rebulk_soc_num_, DEFAULT_REBULK_SOC);

      if (!std::isnan(avg_soc) && avg_soc < rebulk_soc) {
        this->set_rectifier_dc_(true);
        this->close_relay_();
        this->transition_to_(ChargeState::BULK);
        ESP_LOGI(TAG, "Starting charge cycle (SOC: %.0f%%, threshold: %.0f%%)", avg_soc, rebulk_soc);
      }
      break;
    }

    case ChargeState::BULK: {
      if (!enabled) {
        this->open_relay_();
        this->set_rectifier_dc_(false);
        this->transition_to_(ChargeState::STANDBY);
        break;
      }

      float max_pack_v = this->get_max_pack_voltage_all_packs_();
      float bulk_v = this->get_param_(this->bulk_voltage_num_, DEFAULT_BULK_VOLTAGE);

      if (!std::isnan(max_pack_v) && max_pack_v >= bulk_v) {
        this->transition_to_(ChargeState::ABSORB);
        ESP_LOGI(TAG, "Bulk -> Absorb (pack voltage: %.1fV >= %.1fV)", max_pack_v, bulk_v);
      }
      break;
    }

    case ChargeState::ABSORB: {
      if (!enabled) {
        this->open_relay_();
        this->set_rectifier_dc_(false);
        this->transition_to_(ChargeState::STANDBY);
        break;
      }

      float absorb_tail = this->get_param_(this->absorb_tail_current_num_, DEFAULT_ABSORB_TAIL_CURRENT);
      float absorb_max_time = this->get_param_(this->absorb_max_time_num_, DEFAULT_ABSORB_MAX_TIME);

      float rect_current = NAN;
      if (this->rect_current_sensor_ != nullptr && this->rect_current_sensor_->has_state())
        rect_current = this->rect_current_sensor_->state;

      bool current_tapered = !std::isnan(rect_current) && rect_current <= absorb_tail;
      bool time_expired = (millis() - this->state_enter_time_) > (uint32_t)(absorb_max_time * 1000.0f);

      if (current_tapered || time_expired) {
        this->transition_to_(ChargeState::FLOAT);
        if (current_tapered)
          ESP_LOGI(TAG, "Absorb -> Float (current tapered to %.1fA)", rect_current);
        else
          ESP_LOGI(TAG, "Absorb -> Float (max absorb time reached)");
      }
      break;
    }

    case ChargeState::FLOAT: {
      if (!enabled) {
        this->open_relay_();
        this->set_rectifier_dc_(false);
        this->transition_to_(ChargeState::STANDBY);
        break;
      }

      float avg_soc = this->get_avg_soc_all_packs_();
      float rebulk_soc = this->get_param_(this->rebulk_soc_num_, DEFAULT_REBULK_SOC);

      if (!std::isnan(avg_soc) && avg_soc < rebulk_soc) {
        this->transition_to_(ChargeState::BULK);
        ESP_LOGI(TAG, "Float -> Bulk (SOC dropped to %.0f%% < %.0f%%)", avg_soc, rebulk_soc);
      }
      break;
    }

    case ChargeState::ALARM: {
      if (!enabled) {
        this->transition_to_(ChargeState::STANDBY);
        this->alarm_reason_ = "";
        ESP_LOGI(TAG, "Alarm cleared by disable");
      }
      break;
    }
  }
}

void ChargeControllerComponent::open_relay_() {
  if (this->relay_pin_ != nullptr)
    this->relay_pin_->digital_write(false);
  this->relay_closed_ = false;
}

void ChargeControllerComponent::close_relay_() {
  if (this->relay_pin_ != nullptr)
    this->relay_pin_->digital_write(true);
  this->relay_closed_ = true;
}

void ChargeControllerComponent::transition_to_(ChargeState new_state) {
  if (this->state_ == new_state)
    return;

  ESP_LOGI(TAG, "State: %s -> %s", this->state_to_string_(this->state_),
           this->state_to_string_(new_state));
  this->state_ = new_state;
  this->state_enter_time_ = millis();
}

void ChargeControllerComponent::set_rectifier_voltage_(float voltage) {
  if (this->rect_voltage_number_ != nullptr) {
    auto call = this->rect_voltage_number_->make_call();
    call.set_value(voltage);
    call.perform();
  }
}

void ChargeControllerComponent::set_rectifier_current_(float percent) {
  if (this->rect_current_number_ != nullptr) {
    auto call = this->rect_current_number_->make_call();
    call.set_value(percent);
    call.perform();
  }
}

void ChargeControllerComponent::set_rectifier_dc_(bool on) {
  if (this->rect_dc_switch_ != nullptr) {
    if (on)
      this->rect_dc_switch_->turn_on();
    else
      this->rect_dc_switch_->turn_off();
  }
}

void ChargeControllerComponent::publish_state_() {
  uint32_t now = millis();
  if (now - this->last_publish_ms_ < PUBLISH_INTERVAL_MS)
    return;
  this->last_publish_ms_ = now;

  if (this->charge_state_ts_ != nullptr)
    this->charge_state_ts_->publish_state(this->state_to_string_(this->state_));

  if (this->alarm_reason_ts_ != nullptr)
    this->alarm_reason_ts_->publish_state(this->alarm_reason_);

  bool healthy = this->state_ != ChargeState::ALARM && this->boot_complete_;
  if (this->system_healthy_bs_ != nullptr)
    this->system_healthy_bs_->publish_state(healthy);

  if (this->relay_state_bs_ != nullptr)
    this->relay_state_bs_->publish_state(this->relay_closed_);
}

float ChargeControllerComponent::get_param_(ChargeControllerNumber *num, float default_val) {
  if (num != nullptr && num->has_state())
    return num->state;
  return default_val;
}

float ChargeControllerComponent::get_min_cell_voltage_all_packs_() {
  float min_v = NAN;
  for (auto &pack : this->packs_) {
    if (pack.min_cell_voltage != nullptr && pack.min_cell_voltage->has_state()) {
      float v = pack.min_cell_voltage->state;
      if (std::isnan(min_v) || v < min_v)
        min_v = v;
    }
  }
  return min_v;
}

float ChargeControllerComponent::get_max_cell_voltage_all_packs_() {
  float max_v = NAN;
  for (auto &pack : this->packs_) {
    if (pack.max_cell_voltage != nullptr && pack.max_cell_voltage->has_state()) {
      float v = pack.max_cell_voltage->state;
      if (std::isnan(max_v) || v > max_v)
        max_v = v;
    }
  }
  return max_v;
}

float ChargeControllerComponent::get_max_temperature_all_packs_() {
  float max_t = NAN;
  for (auto &pack : this->packs_) {
    if (pack.max_temperature != nullptr && pack.max_temperature->has_state()) {
      float t = pack.max_temperature->state;
      if (std::isnan(max_t) || t > max_t)
        max_t = t;
    }
  }
  return max_t;
}

float ChargeControllerComponent::get_max_pack_voltage_all_packs_() {
  float max_v = NAN;
  for (auto &pack : this->packs_) {
    if (pack.pack_voltage != nullptr && pack.pack_voltage->has_state()) {
      float v = pack.pack_voltage->state;
      if (std::isnan(max_v) || v > max_v)
        max_v = v;
    }
  }
  return max_v;
}

float ChargeControllerComponent::get_avg_soc_all_packs_() {
  float sum = 0.0f;
  int count = 0;
  for (auto &pack : this->packs_) {
    if (pack.soc != nullptr && pack.soc->has_state()) {
      sum += pack.soc->state;
      count++;
    }
  }
  if (count == 0)
    return NAN;
  return sum / count;
}

float ChargeControllerComponent::get_max_pack_voltage_delta_() {
  float min_v = NAN;
  float max_v = NAN;
  for (auto &pack : this->packs_) {
    if (pack.pack_voltage != nullptr && pack.pack_voltage->has_state()) {
      float v = pack.pack_voltage->state;
      if (std::isnan(min_v) || v < min_v)
        min_v = v;
      if (std::isnan(max_v) || v > max_v)
        max_v = v;
    }
  }
  if (std::isnan(min_v) || std::isnan(max_v))
    return NAN;
  return max_v - min_v;
}

bool ChargeControllerComponent::all_packs_online_(uint32_t timeout_ms) {
  uint32_t now = millis();
  for (auto &pack : this->packs_) {
    if (pack.last_update_ms == 0)
      return false;
    if (now - pack.last_update_ms > timeout_ms)
      return false;
  }
  return true;
}

bool ChargeControllerComponent::rectifier_online_(uint32_t timeout_ms) {
  if (this->rect_last_update_ms_ == 0)
    return false;
  return (millis() - this->rect_last_update_ms_) <= timeout_ms;
}

bool ChargeControllerComponent::all_sensors_valid_() {
  for (auto &pack : this->packs_) {
    if (pack.min_cell_voltage == nullptr || !pack.min_cell_voltage->has_state())
      return false;
    if (pack.max_cell_voltage == nullptr || !pack.max_cell_voltage->has_state())
      return false;
    if (pack.pack_voltage == nullptr || !pack.pack_voltage->has_state())
      return false;
    if (pack.soc == nullptr || !pack.soc->has_state())
      return false;
  }
  if (this->rect_voltage_sensor_ != nullptr && !this->rect_voltage_sensor_->has_state())
    return false;
  return true;
}

const char *ChargeControllerComponent::state_to_string_(ChargeState state) {
  switch (state) {
    case ChargeState::STANDBY:
      return "STANDBY";
    case ChargeState::BULK:
      return "BULK";
    case ChargeState::ABSORB:
      return "ABSORB";
    case ChargeState::FLOAT:
      return "FLOAT";
    case ChargeState::ALARM:
      return "ALARM";
    default:
      return "UNKNOWN";
  }
}

}  // namespace charge_controller
}  // namespace esphome
