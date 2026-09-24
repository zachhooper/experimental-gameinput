#include "gameinput_reading.h"

#include "gameinput_device.h"
#include "gameinput_keymap.h"
#include "gameinput_singleton.h"

#include <cmath>

namespace godot {

namespace gi = gameinput_internal;

namespace {

constexpr uint32_t kMouseButtonMask = 0x7Fu;
constexpr uint32_t kSpecialtyButtonMask = 0x3FFFu;
constexpr float kAxisSourceThreshold = 0.5f;
constexpr float kStandardGravity = 9.80665f;

int source_bit_index(int source, int base, int count) {
    if (source < base || source >= base + count) return -1;
    return source - base;
}

} // namespace

void GameInputReading::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_button_down", "button"), &GameInputReading::is_button_down);
    ClassDB::bind_method(D_METHOD("was_button_pressed", "button"),
                         &GameInputReading::was_button_pressed);
    ClassDB::bind_method(D_METHOD("was_button_released", "button"),
                         &GameInputReading::was_button_released);
    ClassDB::bind_method(D_METHOD("get_axis", "axis"), &GameInputReading::get_axis);
    ClassDB::bind_method(D_METHOD("get_buttons_mask"), &GameInputReading::get_buttons_mask);
    ClassDB::bind_method(D_METHOD("get_timestamp"), &GameInputReading::get_timestamp);
    ClassDB::bind_method(D_METHOD("get_kind_timestamp", "kind"),
                         &GameInputReading::get_kind_timestamp);
    ClassDB::bind_method(D_METHOD("get_device_id"), &GameInputReading::get_device_id);
    ClassDB::bind_method(D_METHOD("get_input_kinds"), &GameInputReading::get_input_kinds);
    ClassDB::bind_method(D_METHOD("has_previous"), &GameInputReading::has_previous);
    ClassDB::bind_method(D_METHOD("has_gap_before"), &GameInputReading::has_gap_before);
    ClassDB::bind_method(D_METHOD("is_truncated"), &GameInputReading::is_truncated);

    ClassDB::bind_method(D_METHOD("is_source_down", "source"), &GameInputReading::is_source_down);
    ClassDB::bind_method(D_METHOD("was_source_pressed", "source"),
                         &GameInputReading::was_source_pressed);
    ClassDB::bind_method(D_METHOD("was_source_released", "source"),
                         &GameInputReading::was_source_released);
    ClassDB::bind_method(D_METHOD("get_source_value", "source"),
                         &GameInputReading::get_source_value);

    ClassDB::bind_method(D_METHOD("get_key_count"), &GameInputReading::get_key_count);
    ClassDB::bind_method(D_METHOD("get_pressed_physical_keys"),
                         &GameInputReading::get_pressed_physical_keys);
    ClassDB::bind_method(D_METHOD("is_physical_key_down", "physical_key"),
                         &GameInputReading::is_physical_key_down);
    ClassDB::bind_method(D_METHOD("was_physical_key_pressed", "physical_key"),
                         &GameInputReading::was_physical_key_pressed);
    ClassDB::bind_method(D_METHOD("was_physical_key_released", "physical_key"),
                         &GameInputReading::was_physical_key_released);
    ClassDB::bind_method(D_METHOD("get_key_states"), &GameInputReading::get_key_states);

    ClassDB::bind_method(D_METHOD("get_mouse_buttons"), &GameInputReading::get_mouse_buttons);
    ClassDB::bind_method(D_METHOD("is_mouse_button_down", "button"),
                         &GameInputReading::is_mouse_button_down);
    ClassDB::bind_method(D_METHOD("was_mouse_button_pressed", "button"),
                         &GameInputReading::was_mouse_button_pressed);
    ClassDB::bind_method(D_METHOD("was_mouse_button_released", "button"),
                         &GameInputReading::was_mouse_button_released);
    ClassDB::bind_method(D_METHOD("get_mouse_delta"), &GameInputReading::get_mouse_delta);
    ClassDB::bind_method(D_METHOD("get_mouse_wheel_delta"),
                         &GameInputReading::get_mouse_wheel_delta);
    ClassDB::bind_method(D_METHOD("get_mouse_absolute_position"),
                         &GameInputReading::get_mouse_absolute_position);
    ClassDB::bind_method(D_METHOD("has_mouse_absolute_position"),
                         &GameInputReading::has_mouse_absolute_position);
    ClassDB::bind_method(D_METHOD("get_mouse_state"), &GameInputReading::get_mouse_state);

    ClassDB::bind_method(D_METHOD("get_sensor_kinds"), &GameInputReading::get_sensor_kinds);
    ClassDB::bind_method(D_METHOD("get_accelerometer"), &GameInputReading::get_accelerometer);
    ClassDB::bind_method(D_METHOD("get_gyroscope"), &GameInputReading::get_gyroscope);
    ClassDB::bind_method(D_METHOD("get_heading_degrees"), &GameInputReading::get_heading_degrees);
    ClassDB::bind_method(D_METHOD("get_heading_accuracy"),
                         &GameInputReading::get_heading_accuracy);
    ClassDB::bind_method(D_METHOD("get_orientation"), &GameInputReading::get_orientation);

    ClassDB::bind_method(D_METHOD("get_arcade_stick_buttons"),
                         &GameInputReading::get_arcade_stick_buttons);
    ClassDB::bind_method(D_METHOD("get_flight_stick_buttons"),
                         &GameInputReading::get_flight_stick_buttons);
    ClassDB::bind_method(D_METHOD("get_flight_stick_hat"), &GameInputReading::get_flight_stick_hat);
    ClassDB::bind_method(D_METHOD("get_flight_stick_hat_vector"),
                         &GameInputReading::get_flight_stick_hat_vector);
    ClassDB::bind_method(D_METHOD("get_racing_wheel_buttons"),
                         &GameInputReading::get_racing_wheel_buttons);
    ClassDB::bind_method(D_METHOD("get_racing_wheel_gear"),
                         &GameInputReading::get_racing_wheel_gear);

    ClassDB::bind_method(D_METHOD("get_controller_axes"), &GameInputReading::get_controller_axes);
    ClassDB::bind_method(D_METHOD("get_controller_buttons"),
                         &GameInputReading::get_controller_buttons);
    ClassDB::bind_method(D_METHOD("get_controller_switches"),
                         &GameInputReading::get_controller_switches);
    ClassDB::bind_method(D_METHOD("get_controller_axis", "index"),
                         &GameInputReading::get_controller_axis);
    ClassDB::bind_method(D_METHOD("is_controller_button_down", "index"),
                         &GameInputReading::is_controller_button_down);
    ClassDB::bind_method(D_METHOD("was_controller_button_pressed", "index"),
                         &GameInputReading::was_controller_button_pressed);
    ClassDB::bind_method(D_METHOD("was_controller_button_released", "index"),
                         &GameInputReading::was_controller_button_released);
    ClassDB::bind_method(D_METHOD("get_controller_switch", "index"),
                         &GameInputReading::get_controller_switch);
}

void GameInputReading::_set_snapshot(int64_t device_id, const gi::Snapshot &cur,
                                     const gi::Snapshot &prev, uint32_t prev_kinds,
                                     bool gap_before, uint32_t sensor_kinds) {
    m_device_id = device_id;
    m_cur = cur;
    m_prev = prev;
    m_prev_kinds = prev_kinds & prev.kinds;
    m_gap_before = gap_before;
    m_sensor_kinds = sensor_kinds;
}

// --- General ---------------------------------------------------------------

bool GameInputReading::is_button_down(int button) const {
    return gi::chord_down(m_cur.gamepad.buttons, (uint32_t)button,
                          GameInputDevice::kGamepadButtonMask);
}

bool GameInputReading::was_button_pressed(int button) const {
    return gi::chord_pressed(m_cur.gamepad.buttons, m_prev.gamepad.buttons,
                             _has_prev_for(gi::SNAP_GAMEPAD), (uint32_t)button,
                             GameInputDevice::kGamepadButtonMask);
}

bool GameInputReading::was_button_released(int button) const {
    return gi::chord_released(m_cur.gamepad.buttons, m_prev.gamepad.buttons,
                              _has_prev_for(gi::SNAP_GAMEPAD), (uint32_t)button,
                              GameInputDevice::kGamepadButtonMask);
}

float GameInputReading::_axis_in(const gi::Snapshot &s, int axis) const {
    using GD = GameInputDevice;
    switch (axis) {
        case GD::AXIS_LEFT_X:          return s.gamepad.left_x;
        // Godot Y axis convention: down is positive; GameInput thumbstick Y up is positive.
        // 0 - y rather than -y so a resting stick reads 0.0, not -0.0 ("-0.00" when printed).
        case GD::AXIS_LEFT_Y:          return 0.0f - s.gamepad.left_y;
        case GD::AXIS_RIGHT_X:         return s.gamepad.right_x;
        case GD::AXIS_RIGHT_Y:         return 0.0f - s.gamepad.right_y;
        case GD::AXIS_LEFT_TRIGGER:    return s.gamepad.left_trigger;
        case GD::AXIS_RIGHT_TRIGGER:   return s.gamepad.right_trigger;
        case GD::AXIS_WHEEL:           return s.racing_wheel.wheel;
        case GD::AXIS_THROTTLE:        return s.racing_wheel.throttle;
        case GD::AXIS_BRAKE:           return s.racing_wheel.brake;
        case GD::AXIS_CLUTCH:          return s.racing_wheel.clutch;
        case GD::AXIS_HANDBRAKE:       return s.racing_wheel.handbrake;
        case GD::AXIS_FLIGHT_ROLL:     return s.flight_stick.roll;
        case GD::AXIS_FLIGHT_PITCH:    return s.flight_stick.pitch;
        case GD::AXIS_FLIGHT_YAW:      return s.flight_stick.yaw;
        case GD::AXIS_FLIGHT_THROTTLE: return s.flight_stick.throttle;
        default:                       return 0.0f;
    }
}

float GameInputReading::get_axis(int axis) const {
    return _axis_in(m_cur, axis);
}

int GameInputReading::get_buttons_mask() const {
    return (int)m_cur.gamepad.buttons;
}

int64_t GameInputReading::get_timestamp() const {
    return (int64_t)gi::newest_timestamp(m_cur);
}

int64_t GameInputReading::get_kind_timestamp(int kind) const {
    return (int64_t)gi::kinds_timestamp(m_cur, gi::snapshot_kinds_from_public((uint32_t)kind));
}

int GameInputReading::get_input_kinds() const {
    return (int)gi::public_kinds_from_snapshot(m_cur.kinds);
}

bool GameInputReading::has_previous() const {
    return (m_prev_kinds & m_cur.kinds) != 0;
}

bool GameInputReading::is_truncated() const {
    return gi::is_truncated(m_cur);
}

// --- Sources -----------------------------------------------------------------

uint32_t GameInputReading::_source_snap_kind(int source) const {
    if (source >= GameInputDevice::SRC_BTN_MENU && source <= GameInputDevice::SRC_BTN_PADDLE_RIGHT_2) {
        return gi::SNAP_GAMEPAD;
    }
    if (source >= GameInputDevice::SRC_AXIS_LEFT_X && source <= GameInputDevice::SRC_AXIS_RIGHT_TRIGGER) {
        return gi::SNAP_GAMEPAD;
    }
    if (source >= GameInputDevice::SRC_AXIS_WHEEL && source <= GameInputDevice::SRC_AXIS_HANDBRAKE) {
        return gi::SNAP_RACING_WHEEL;
    }
    if (source >= GameInputDevice::SRC_AXIS_FLIGHT_ROLL &&
            source <= GameInputDevice::SRC_AXIS_FLIGHT_THROTTLE) {
        return gi::SNAP_FLIGHT_STICK;
    }
    if (source >= GameInputDevice::SRC_ARCADE_MENU && source <= GameInputDevice::SRC_ARCADE_SPECIAL_2) {
        return gi::SNAP_ARCADE_STICK;
    }
    if (source >= GameInputDevice::SRC_FLIGHT_MENU &&
            source <= GameInputDevice::SRC_FLIGHT_RIGHT_SHOULDER) {
        return gi::SNAP_FLIGHT_STICK;
    }
    if (source >= GameInputDevice::SRC_WHEEL_MENU && source <= GameInputDevice::SRC_WHEEL_RIGHT_THUMB) {
        return gi::SNAP_RACING_WHEEL;
    }
    return 0;
}

bool GameInputReading::_source_down_in(const gi::Snapshot &s, int source) const {
    int bit = source_bit_index(source, GameInputDevice::SRC_BTN_MENU, 30);
    if (bit >= 0) return ((s.gamepad.buttons >> bit) & 1u) != 0;
    bit = source_bit_index(source, GameInputDevice::SRC_ARCADE_MENU, 14);
    if (bit >= 0) return ((s.arcade_stick.buttons >> bit) & 1u) != 0;
    bit = source_bit_index(source, GameInputDevice::SRC_FLIGHT_MENU, 14);
    if (bit >= 0) return ((s.flight_stick.buttons >> bit) & 1u) != 0;
    bit = source_bit_index(source, GameInputDevice::SRC_WHEEL_MENU, 14);
    if (bit >= 0) return ((s.racing_wheel.buttons >> bit) & 1u) != 0;
    int axis = source_bit_index(source, GameInputDevice::SRC_AXIS_LEFT_X, 15);
    if (axis >= 0) return std::fabs(_axis_in(s, axis)) >= kAxisSourceThreshold;
    return false;
}

bool GameInputReading::is_source_down(int source) const {
    if (!(m_cur.kinds & _source_snap_kind(source))) return false;
    return _source_down_in(m_cur, source);
}

bool GameInputReading::was_source_pressed(int source) const {
    uint32_t kind = _source_snap_kind(source);
    if (!(m_cur.kinds & kind) || !_source_down_in(m_cur, source)) return false;
    if (!_has_prev_for(kind)) return true;
    return !_source_down_in(m_prev, source);
}

bool GameInputReading::was_source_released(int source) const {
    uint32_t kind = _source_snap_kind(source);
    if (!(m_cur.kinds & kind) || !_has_prev_for(kind)) return false;
    return _source_down_in(m_prev, source) && !_source_down_in(m_cur, source);
}

float GameInputReading::get_source_value(int source) const {
    uint32_t kind = _source_snap_kind(source);
    if (!(m_cur.kinds & kind)) return 0.0f;
    int axis = source_bit_index(source, GameInputDevice::SRC_AXIS_LEFT_X, 15);
    if (axis >= 0) return _axis_in(m_cur, axis);
    return _source_down_in(m_cur, source) ? 1.0f : 0.0f;
}

// --- Keyboard ----------------------------------------------------------------

int GameInputReading::get_key_count() const {
    return (m_cur.kinds & gi::SNAP_KEYBOARD) ? (int)m_cur.key_count : 0;
}

int GameInputReading::_find_key(const gi::Snapshot &s, int64_t physical_key) const {
    if (physical_key == 0 || !(s.kinds & gi::SNAP_KEYBOARD)) return -1;
    for (uint32_t i = 0; i < s.key_count && i < gi::kMaxKeys; ++i) {
        if (gi::scan_code_to_key(s.keys[i].scan_code) == physical_key) return (int)i;
    }
    return -1;
}

PackedInt64Array GameInputReading::get_pressed_physical_keys() const {
    PackedInt64Array out;
    if (!(m_cur.kinds & gi::SNAP_KEYBOARD)) return out;
    for (uint32_t i = 0; i < m_cur.key_count && i < gi::kMaxKeys; ++i) {
        int64_t key = gi::scan_code_to_key(m_cur.keys[i].scan_code);
        if (key != 0 && !out.has(key)) out.push_back(key);
    }
    return out;
}

bool GameInputReading::is_physical_key_down(int64_t physical_key) const {
    return _find_key(m_cur, physical_key) >= 0;
}

bool GameInputReading::was_physical_key_pressed(int64_t physical_key) const {
    if (_find_key(m_cur, physical_key) < 0) return false;
    if (!_has_prev_for(gi::SNAP_KEYBOARD)) return true;
    return _find_key(m_prev, physical_key) < 0;
}

bool GameInputReading::was_physical_key_released(int64_t physical_key) const {
    if (!(m_cur.kinds & gi::SNAP_KEYBOARD) || !_has_prev_for(gi::SNAP_KEYBOARD)) return false;
    return _find_key(m_prev, physical_key) >= 0 && _find_key(m_cur, physical_key) < 0;
}

Array GameInputReading::get_key_states() const {
    Array out;
    if (!(m_cur.kinds & gi::SNAP_KEYBOARD)) return out;
    for (uint32_t i = 0; i < m_cur.key_count && i < gi::kMaxKeys; ++i) {
        const gi::KeyData &k = m_cur.keys[i];
        Dictionary d;
        d["scan_code"] = (int64_t)k.scan_code;
        d["virtual_key"] = (int64_t)k.virtual_key;
        d["code_point"] = (int64_t)k.code_point;
        d["is_dead_key"] = k.is_dead_key;
        d["physical_keycode"] = gi::scan_code_to_key(k.scan_code);
        d["keycode"] = gi::virtual_key_to_key(k.virtual_key);
        d["location"] = gi::scan_code_to_location(k.scan_code);
        out.push_back(d);
    }
    return out;
}

// --- Mouse -------------------------------------------------------------------

int GameInputReading::get_mouse_buttons() const {
    return (int)m_cur.mouse.buttons;
}

bool GameInputReading::is_mouse_button_down(int button) const {
    return gi::chord_down(m_cur.mouse.buttons, (uint32_t)button, kMouseButtonMask);
}

bool GameInputReading::was_mouse_button_pressed(int button) const {
    return gi::chord_pressed(m_cur.mouse.buttons, m_prev.mouse.buttons,
                             _has_prev_for(gi::SNAP_MOUSE), (uint32_t)button, kMouseButtonMask);
}

bool GameInputReading::was_mouse_button_released(int button) const {
    return gi::chord_released(m_cur.mouse.buttons, m_prev.mouse.buttons,
                              _has_prev_for(gi::SNAP_MOUSE), (uint32_t)button, kMouseButtonMask);
}

Vector2 GameInputReading::get_mouse_delta() const {
    if (!(m_cur.kinds & gi::SNAP_MOUSE) || !_has_prev_for(gi::SNAP_MOUSE)) return Vector2();
    return Vector2((real_t)gi::wrapping_delta(m_cur.mouse.x, m_prev.mouse.x),
                   (real_t)gi::wrapping_delta(m_cur.mouse.y, m_prev.mouse.y));
}

Vector2 GameInputReading::get_mouse_wheel_delta() const {
    if (!(m_cur.kinds & gi::SNAP_MOUSE) || !_has_prev_for(gi::SNAP_MOUSE)) return Vector2();
    return Vector2((real_t)gi::wrapping_delta(m_cur.mouse.wheel_x, m_prev.mouse.wheel_x),
                   (real_t)gi::wrapping_delta(m_cur.mouse.wheel_y, m_prev.mouse.wheel_y));
}

bool GameInputReading::has_mouse_absolute_position() const {
    return (m_cur.kinds & gi::SNAP_MOUSE) && (m_cur.mouse.positions & 0x1u);
}

Vector2 GameInputReading::get_mouse_absolute_position() const {
    if (!has_mouse_absolute_position()) return Vector2();
    return Vector2((real_t)m_cur.mouse.absolute_x, (real_t)m_cur.mouse.absolute_y);
}

Dictionary GameInputReading::get_mouse_state() const {
    Dictionary d;
    if (!(m_cur.kinds & gi::SNAP_MOUSE)) return d;
    d["buttons"] = (int64_t)m_cur.mouse.buttons;
    d["positions"] = (int64_t)m_cur.mouse.positions;
    d["x"] = m_cur.mouse.x;
    d["y"] = m_cur.mouse.y;
    d["absolute_x"] = m_cur.mouse.absolute_x;
    d["absolute_y"] = m_cur.mouse.absolute_y;
    d["wheel_x"] = m_cur.mouse.wheel_x;
    d["wheel_y"] = m_cur.mouse.wheel_y;
    return d;
}

// --- Sensors -----------------------------------------------------------------

int GameInputReading::get_sensor_kinds() const {
    return (m_cur.kinds & gi::SNAP_SENSORS) ? (int)m_sensor_kinds : 0;
}

Vector3 GameInputReading::get_accelerometer() const {
    if (!(m_cur.kinds & gi::SNAP_SENSORS)) return Vector3();
    const float *a = m_cur.sensors.acceleration_g;
    return Vector3(a[0] * kStandardGravity, a[1] * kStandardGravity, a[2] * kStandardGravity);
}

Vector3 GameInputReading::get_gyroscope() const {
    if (!(m_cur.kinds & gi::SNAP_SENSORS)) return Vector3();
    const float *w = m_cur.sensors.angular_velocity_rad;
    return Vector3(w[0], w[1], w[2]);
}

float GameInputReading::get_heading_degrees() const {
    return (m_cur.kinds & gi::SNAP_SENSORS) ? m_cur.sensors.heading_degrees : 0.0f;
}

int GameInputReading::get_heading_accuracy() const {
    return (m_cur.kinds & gi::SNAP_SENSORS) ? (int)m_cur.sensors.heading_accuracy : 0;
}

Quaternion GameInputReading::get_orientation() const {
    if (!(m_cur.kinds & gi::SNAP_SENSORS)) return Quaternion();
    const float *q = m_cur.sensors.orientation_wxyz;
    return Quaternion(q[1], q[2], q[3], q[0]);
}

// --- Arcade stick, flight stick, racing wheel ----------------------------------

int GameInputReading::get_arcade_stick_buttons() const {
    return (int)(m_cur.arcade_stick.buttons & kSpecialtyButtonMask);
}

int GameInputReading::get_flight_stick_buttons() const {
    return (int)(m_cur.flight_stick.buttons & kSpecialtyButtonMask);
}

int GameInputReading::get_flight_stick_hat() const {
    return (int)m_cur.flight_stick.hat;
}

Vector2 GameInputReading::get_flight_stick_hat_vector() const {
    return GameInputDevice::switch_position_to_vector(m_cur.flight_stick.hat);
}

int GameInputReading::get_racing_wheel_buttons() const {
    return (int)(m_cur.racing_wheel.buttons & kSpecialtyButtonMask);
}

int GameInputReading::get_racing_wheel_gear() const {
    return (int)m_cur.racing_wheel.gear;
}

// --- Raw controller --------------------------------------------------------------

PackedFloat32Array GameInputReading::get_controller_axes() const {
    PackedFloat32Array out;
    if (!(m_cur.kinds & gi::SNAP_CONTROLLER_AXIS)) return out;
    for (uint32_t i = 0; i < m_cur.axis_count && i < gi::kMaxControllerAxes; ++i) {
        out.push_back(m_cur.axes[i]);
    }
    return out;
}

Array GameInputReading::get_controller_buttons() const {
    Array out;
    if (!(m_cur.kinds & gi::SNAP_CONTROLLER_BUTTON)) return out;
    for (uint32_t i = 0; i < m_cur.button_count && i < gi::kMaxControllerButtons; ++i) {
        out.push_back(gi::controller_button(m_cur, i));
    }
    return out;
}

PackedInt32Array GameInputReading::get_controller_switches() const {
    PackedInt32Array out;
    if (!(m_cur.kinds & gi::SNAP_CONTROLLER_SWITCH)) return out;
    for (uint32_t i = 0; i < m_cur.switch_count && i < gi::kMaxControllerSwitches; ++i) {
        out.push_back(m_cur.switches[i]);
    }
    return out;
}

float GameInputReading::get_controller_axis(int index) const {
    if (!(m_cur.kinds & gi::SNAP_CONTROLLER_AXIS) || index < 0 ||
            (uint32_t)index >= m_cur.axis_count || (uint32_t)index >= gi::kMaxControllerAxes) {
        return 0.0f;
    }
    return m_cur.axes[index];
}

bool GameInputReading::is_controller_button_down(int index) const {
    if (!(m_cur.kinds & gi::SNAP_CONTROLLER_BUTTON) || index < 0) return false;
    return gi::controller_button(m_cur, (uint32_t)index);
}

bool GameInputReading::was_controller_button_pressed(int index) const {
    if (!is_controller_button_down(index)) return false;
    if (!_has_prev_for(gi::SNAP_CONTROLLER_BUTTON)) return true;
    return !gi::controller_button(m_prev, (uint32_t)index);
}

bool GameInputReading::was_controller_button_released(int index) const {
    if (!(m_cur.kinds & gi::SNAP_CONTROLLER_BUTTON) || index < 0 ||
            !_has_prev_for(gi::SNAP_CONTROLLER_BUTTON)) {
        return false;
    }
    return gi::controller_button(m_prev, (uint32_t)index) &&
           !gi::controller_button(m_cur, (uint32_t)index);
}

int GameInputReading::get_controller_switch(int index) const {
    if (!(m_cur.kinds & gi::SNAP_CONTROLLER_SWITCH) || index < 0 ||
            (uint32_t)index >= m_cur.switch_count || (uint32_t)index >= gi::kMaxControllerSwitches) {
        return 0;
    }
    return m_cur.switches[index];
}

} // namespace godot
