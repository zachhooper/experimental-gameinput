#include "gameinput_device.h"

#include "gameinput_force_feedback_effect.h"
#include "gameinput_keymap.h"
#include "gameinput_singleton.h"

namespace godot {

void GameInputDevice::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_device_id"), &GameInputDevice::get_device_id);
    ClassDB::bind_method(D_METHOD("get_display_name"), &GameInputDevice::get_display_name);
    ClassDB::bind_method(D_METHOD("get_kind_mask"), &GameInputDevice::get_kind_mask);
    ClassDB::bind_method(D_METHOD("is_connected"), &GameInputDevice::is_connected);
    ClassDB::bind_method(D_METHOD("supports_vibration"), &GameInputDevice::supports_vibration);
    ClassDB::bind_method(D_METHOD("supports_haptics"), &GameInputDevice::supports_haptics);
    ClassDB::bind_method(D_METHOD("get_device_info"), &GameInputDevice::get_device_info);

    ClassDB::bind_method(D_METHOD("get_status"), &GameInputDevice::get_status);
    ClassDB::bind_method(D_METHOD("get_app_local_id"), &GameInputDevice::get_app_local_id);
    ClassDB::bind_method(D_METHOD("get_device_family"), &GameInputDevice::get_device_family);
    ClassDB::bind_method(D_METHOD("get_supported_rumble_motors"),
                         &GameInputDevice::get_supported_rumble_motors);
    ClassDB::bind_method(D_METHOD("get_supported_system_buttons"),
                         &GameInputDevice::get_supported_system_buttons);
    ClassDB::bind_method(D_METHOD("get_system_buttons"), &GameInputDevice::get_system_buttons);
    ClassDB::bind_method(D_METHOD("get_keyboard_layout"), &GameInputDevice::get_keyboard_layout);
    ClassDB::bind_method(D_METHOD("get_button_label", "source"), &GameInputDevice::get_button_label);
    ClassDB::bind_method(D_METHOD("get_haptic_info"), &GameInputDevice::get_haptic_info);

    ClassDB::bind_method(D_METHOD("start_vibration", "weak_magnitude", "strong_magnitude",
                                  "duration", "left_trigger", "right_trigger"),
                         &GameInputDevice::start_vibration, DEFVAL(0.0f), DEFVAL(0.0f),
                         DEFVAL(0.0f));
    ClassDB::bind_method(D_METHOD("stop_vibration"), &GameInputDevice::stop_vibration);
    ClassDB::bind_method(D_METHOD("is_vibrating"), &GameInputDevice::is_vibrating);
    ClassDB::bind_method(D_METHOD("get_vibration_strength"),
                         &GameInputDevice::get_vibration_strength);
    ClassDB::bind_method(D_METHOD("get_trigger_vibration_strength"),
                         &GameInputDevice::get_trigger_vibration_strength);
    ClassDB::bind_method(D_METHOD("get_vibration_duration"),
                         &GameInputDevice::get_vibration_duration);
    ClassDB::bind_method(D_METHOD("get_vibration_remaining_duration"),
                         &GameInputDevice::get_vibration_remaining_duration);

    ClassDB::bind_method(D_METHOD("get_force_feedback_motor_count"),
                         &GameInputDevice::get_force_feedback_motor_count);
    ClassDB::bind_method(D_METHOD("get_force_feedback_motor_info", "motor_index"),
                         &GameInputDevice::get_force_feedback_motor_info);
    ClassDB::bind_method(D_METHOD("is_force_feedback_motor_powered_on", "motor_index"),
                         &GameInputDevice::is_force_feedback_motor_powered_on);
    ClassDB::bind_method(D_METHOD("set_force_feedback_motor_gain", "motor_index", "gain"),
                         &GameInputDevice::set_force_feedback_motor_gain);
    ClassDB::bind_method(D_METHOD("create_force_feedback_effect", "motor_index", "params"),
                         &GameInputDevice::create_force_feedback_effect);

    ClassDB::bind_static_method("GameInputDevice",
                                D_METHOD("button_to_source", "button"),
                                &GameInputDevice::button_to_source);
    ClassDB::bind_static_method("GameInputDevice",
                                D_METHOD("axis_to_source", "axis"),
                                &GameInputDevice::axis_to_source);
    ClassDB::bind_static_method("GameInputDevice",
                                D_METHOD("scan_code_to_physical_key", "scan_code"),
                                &GameInputDevice::scan_code_to_physical_key);
    ClassDB::bind_static_method("GameInputDevice",
                                D_METHOD("virtual_key_to_keycode", "virtual_key"),
                                &GameInputDevice::virtual_key_to_keycode);
    ClassDB::bind_static_method("GameInputDevice",
                                D_METHOD("switch_position_to_vector", "position"),
                                &GameInputDevice::switch_position_to_vector);

    BIND_ENUM_CONSTANT(BUTTON_NONE);
    BIND_ENUM_CONSTANT(BUTTON_MENU);
    BIND_ENUM_CONSTANT(BUTTON_VIEW);
    BIND_ENUM_CONSTANT(BUTTON_A);
    BIND_ENUM_CONSTANT(BUTTON_B);
    BIND_ENUM_CONSTANT(BUTTON_X);
    BIND_ENUM_CONSTANT(BUTTON_Y);
    BIND_ENUM_CONSTANT(BUTTON_DPAD_UP);
    BIND_ENUM_CONSTANT(BUTTON_DPAD_DOWN);
    BIND_ENUM_CONSTANT(BUTTON_DPAD_LEFT);
    BIND_ENUM_CONSTANT(BUTTON_DPAD_RIGHT);
    BIND_ENUM_CONSTANT(BUTTON_LEFT_SHOULDER);
    BIND_ENUM_CONSTANT(BUTTON_RIGHT_SHOULDER);
    BIND_ENUM_CONSTANT(BUTTON_LEFT_THUMB);
    BIND_ENUM_CONSTANT(BUTTON_RIGHT_THUMB);
    BIND_ENUM_CONSTANT(BUTTON_C);
    BIND_ENUM_CONSTANT(BUTTON_Z);
    BIND_ENUM_CONSTANT(BUTTON_LEFT_TRIGGER);
    BIND_ENUM_CONSTANT(BUTTON_RIGHT_TRIGGER);
    BIND_ENUM_CONSTANT(BUTTON_LEFT_STICK_UP);
    BIND_ENUM_CONSTANT(BUTTON_LEFT_STICK_DOWN);
    BIND_ENUM_CONSTANT(BUTTON_LEFT_STICK_LEFT);
    BIND_ENUM_CONSTANT(BUTTON_LEFT_STICK_RIGHT);
    BIND_ENUM_CONSTANT(BUTTON_RIGHT_STICK_UP);
    BIND_ENUM_CONSTANT(BUTTON_RIGHT_STICK_DOWN);
    BIND_ENUM_CONSTANT(BUTTON_RIGHT_STICK_LEFT);
    BIND_ENUM_CONSTANT(BUTTON_RIGHT_STICK_RIGHT);
    BIND_ENUM_CONSTANT(BUTTON_PADDLE_LEFT_1);
    BIND_ENUM_CONSTANT(BUTTON_PADDLE_LEFT_2);
    BIND_ENUM_CONSTANT(BUTTON_PADDLE_RIGHT_1);
    BIND_ENUM_CONSTANT(BUTTON_PADDLE_RIGHT_2);

    BIND_ENUM_CONSTANT(AXIS_LEFT_X);
    BIND_ENUM_CONSTANT(AXIS_LEFT_Y);
    BIND_ENUM_CONSTANT(AXIS_RIGHT_X);
    BIND_ENUM_CONSTANT(AXIS_RIGHT_Y);
    BIND_ENUM_CONSTANT(AXIS_LEFT_TRIGGER);
    BIND_ENUM_CONSTANT(AXIS_RIGHT_TRIGGER);
    BIND_ENUM_CONSTANT(AXIS_WHEEL);
    BIND_ENUM_CONSTANT(AXIS_THROTTLE);
    BIND_ENUM_CONSTANT(AXIS_BRAKE);
    BIND_ENUM_CONSTANT(AXIS_CLUTCH);
    BIND_ENUM_CONSTANT(AXIS_HANDBRAKE);
    BIND_ENUM_CONSTANT(AXIS_FLIGHT_ROLL);
    BIND_ENUM_CONSTANT(AXIS_FLIGHT_PITCH);
    BIND_ENUM_CONSTANT(AXIS_FLIGHT_YAW);
    BIND_ENUM_CONSTANT(AXIS_FLIGHT_THROTTLE);

    BIND_ENUM_CONSTANT(SRC_BTN_MENU);
    BIND_ENUM_CONSTANT(SRC_BTN_VIEW);
    BIND_ENUM_CONSTANT(SRC_BTN_A);
    BIND_ENUM_CONSTANT(SRC_BTN_B);
    BIND_ENUM_CONSTANT(SRC_BTN_X);
    BIND_ENUM_CONSTANT(SRC_BTN_Y);
    BIND_ENUM_CONSTANT(SRC_BTN_DPAD_UP);
    BIND_ENUM_CONSTANT(SRC_BTN_DPAD_DOWN);
    BIND_ENUM_CONSTANT(SRC_BTN_DPAD_LEFT);
    BIND_ENUM_CONSTANT(SRC_BTN_DPAD_RIGHT);
    BIND_ENUM_CONSTANT(SRC_BTN_LEFT_SHOULDER);
    BIND_ENUM_CONSTANT(SRC_BTN_RIGHT_SHOULDER);
    BIND_ENUM_CONSTANT(SRC_BTN_LEFT_THUMB);
    BIND_ENUM_CONSTANT(SRC_BTN_RIGHT_THUMB);
    BIND_ENUM_CONSTANT(SRC_BTN_C);
    BIND_ENUM_CONSTANT(SRC_BTN_Z);
    BIND_ENUM_CONSTANT(SRC_BTN_LEFT_TRIGGER);
    BIND_ENUM_CONSTANT(SRC_BTN_RIGHT_TRIGGER);
    BIND_ENUM_CONSTANT(SRC_BTN_LEFT_STICK_UP);
    BIND_ENUM_CONSTANT(SRC_BTN_LEFT_STICK_DOWN);
    BIND_ENUM_CONSTANT(SRC_BTN_LEFT_STICK_LEFT);
    BIND_ENUM_CONSTANT(SRC_BTN_LEFT_STICK_RIGHT);
    BIND_ENUM_CONSTANT(SRC_BTN_RIGHT_STICK_UP);
    BIND_ENUM_CONSTANT(SRC_BTN_RIGHT_STICK_DOWN);
    BIND_ENUM_CONSTANT(SRC_BTN_RIGHT_STICK_LEFT);
    BIND_ENUM_CONSTANT(SRC_BTN_RIGHT_STICK_RIGHT);
    BIND_ENUM_CONSTANT(SRC_BTN_PADDLE_LEFT_1);
    BIND_ENUM_CONSTANT(SRC_BTN_PADDLE_LEFT_2);
    BIND_ENUM_CONSTANT(SRC_BTN_PADDLE_RIGHT_1);
    BIND_ENUM_CONSTANT(SRC_BTN_PADDLE_RIGHT_2);
    BIND_ENUM_CONSTANT(SRC_AXIS_LEFT_X);
    BIND_ENUM_CONSTANT(SRC_AXIS_LEFT_Y);
    BIND_ENUM_CONSTANT(SRC_AXIS_RIGHT_X);
    BIND_ENUM_CONSTANT(SRC_AXIS_RIGHT_Y);
    BIND_ENUM_CONSTANT(SRC_AXIS_LEFT_TRIGGER);
    BIND_ENUM_CONSTANT(SRC_AXIS_RIGHT_TRIGGER);
    BIND_ENUM_CONSTANT(SRC_AXIS_WHEEL);
    BIND_ENUM_CONSTANT(SRC_AXIS_THROTTLE);
    BIND_ENUM_CONSTANT(SRC_AXIS_BRAKE);
    BIND_ENUM_CONSTANT(SRC_AXIS_CLUTCH);
    BIND_ENUM_CONSTANT(SRC_AXIS_HANDBRAKE);
    BIND_ENUM_CONSTANT(SRC_AXIS_FLIGHT_ROLL);
    BIND_ENUM_CONSTANT(SRC_AXIS_FLIGHT_PITCH);
    BIND_ENUM_CONSTANT(SRC_AXIS_FLIGHT_YAW);
    BIND_ENUM_CONSTANT(SRC_AXIS_FLIGHT_THROTTLE);
    BIND_ENUM_CONSTANT(SRC_ARCADE_MENU);
    BIND_ENUM_CONSTANT(SRC_ARCADE_VIEW);
    BIND_ENUM_CONSTANT(SRC_ARCADE_UP);
    BIND_ENUM_CONSTANT(SRC_ARCADE_DOWN);
    BIND_ENUM_CONSTANT(SRC_ARCADE_LEFT);
    BIND_ENUM_CONSTANT(SRC_ARCADE_RIGHT);
    BIND_ENUM_CONSTANT(SRC_ARCADE_ACTION_1);
    BIND_ENUM_CONSTANT(SRC_ARCADE_ACTION_2);
    BIND_ENUM_CONSTANT(SRC_ARCADE_ACTION_3);
    BIND_ENUM_CONSTANT(SRC_ARCADE_ACTION_4);
    BIND_ENUM_CONSTANT(SRC_ARCADE_ACTION_5);
    BIND_ENUM_CONSTANT(SRC_ARCADE_ACTION_6);
    BIND_ENUM_CONSTANT(SRC_ARCADE_SPECIAL_1);
    BIND_ENUM_CONSTANT(SRC_ARCADE_SPECIAL_2);
    BIND_ENUM_CONSTANT(SRC_FLIGHT_MENU);
    BIND_ENUM_CONSTANT(SRC_FLIGHT_VIEW);
    BIND_ENUM_CONSTANT(SRC_FLIGHT_FIRE_PRIMARY);
    BIND_ENUM_CONSTANT(SRC_FLIGHT_FIRE_SECONDARY);
    BIND_ENUM_CONSTANT(SRC_FLIGHT_HAT_UP);
    BIND_ENUM_CONSTANT(SRC_FLIGHT_HAT_DOWN);
    BIND_ENUM_CONSTANT(SRC_FLIGHT_HAT_LEFT);
    BIND_ENUM_CONSTANT(SRC_FLIGHT_HAT_RIGHT);
    BIND_ENUM_CONSTANT(SRC_FLIGHT_A);
    BIND_ENUM_CONSTANT(SRC_FLIGHT_B);
    BIND_ENUM_CONSTANT(SRC_FLIGHT_X);
    BIND_ENUM_CONSTANT(SRC_FLIGHT_Y);
    BIND_ENUM_CONSTANT(SRC_FLIGHT_LEFT_SHOULDER);
    BIND_ENUM_CONSTANT(SRC_FLIGHT_RIGHT_SHOULDER);
    BIND_ENUM_CONSTANT(SRC_WHEEL_MENU);
    BIND_ENUM_CONSTANT(SRC_WHEEL_VIEW);
    BIND_ENUM_CONSTANT(SRC_WHEEL_PREVIOUS_GEAR);
    BIND_ENUM_CONSTANT(SRC_WHEEL_NEXT_GEAR);
    BIND_ENUM_CONSTANT(SRC_WHEEL_DPAD_UP);
    BIND_ENUM_CONSTANT(SRC_WHEEL_DPAD_DOWN);
    BIND_ENUM_CONSTANT(SRC_WHEEL_DPAD_LEFT);
    BIND_ENUM_CONSTANT(SRC_WHEEL_DPAD_RIGHT);
    BIND_ENUM_CONSTANT(SRC_WHEEL_A);
    BIND_ENUM_CONSTANT(SRC_WHEEL_B);
    BIND_ENUM_CONSTANT(SRC_WHEEL_X);
    BIND_ENUM_CONSTANT(SRC_WHEEL_Y);
    BIND_ENUM_CONSTANT(SRC_WHEEL_LEFT_THUMB);
    BIND_ENUM_CONSTANT(SRC_WHEEL_RIGHT_THUMB);

    BIND_ENUM_CONSTANT(ARCADE_STICK_NONE);
    BIND_ENUM_CONSTANT(ARCADE_STICK_MENU);
    BIND_ENUM_CONSTANT(ARCADE_STICK_VIEW);
    BIND_ENUM_CONSTANT(ARCADE_STICK_UP);
    BIND_ENUM_CONSTANT(ARCADE_STICK_DOWN);
    BIND_ENUM_CONSTANT(ARCADE_STICK_LEFT);
    BIND_ENUM_CONSTANT(ARCADE_STICK_RIGHT);
    BIND_ENUM_CONSTANT(ARCADE_STICK_ACTION_1);
    BIND_ENUM_CONSTANT(ARCADE_STICK_ACTION_2);
    BIND_ENUM_CONSTANT(ARCADE_STICK_ACTION_3);
    BIND_ENUM_CONSTANT(ARCADE_STICK_ACTION_4);
    BIND_ENUM_CONSTANT(ARCADE_STICK_ACTION_5);
    BIND_ENUM_CONSTANT(ARCADE_STICK_ACTION_6);
    BIND_ENUM_CONSTANT(ARCADE_STICK_SPECIAL_1);
    BIND_ENUM_CONSTANT(ARCADE_STICK_SPECIAL_2);

    BIND_ENUM_CONSTANT(FLIGHT_STICK_NONE);
    BIND_ENUM_CONSTANT(FLIGHT_STICK_MENU);
    BIND_ENUM_CONSTANT(FLIGHT_STICK_VIEW);
    BIND_ENUM_CONSTANT(FLIGHT_STICK_FIRE_PRIMARY);
    BIND_ENUM_CONSTANT(FLIGHT_STICK_FIRE_SECONDARY);
    BIND_ENUM_CONSTANT(FLIGHT_STICK_HAT_UP);
    BIND_ENUM_CONSTANT(FLIGHT_STICK_HAT_DOWN);
    BIND_ENUM_CONSTANT(FLIGHT_STICK_HAT_LEFT);
    BIND_ENUM_CONSTANT(FLIGHT_STICK_HAT_RIGHT);
    BIND_ENUM_CONSTANT(FLIGHT_STICK_A);
    BIND_ENUM_CONSTANT(FLIGHT_STICK_B);
    BIND_ENUM_CONSTANT(FLIGHT_STICK_X);
    BIND_ENUM_CONSTANT(FLIGHT_STICK_Y);
    BIND_ENUM_CONSTANT(FLIGHT_STICK_LEFT_SHOULDER);
    BIND_ENUM_CONSTANT(FLIGHT_STICK_RIGHT_SHOULDER);

    BIND_ENUM_CONSTANT(RACING_WHEEL_NONE);
    BIND_ENUM_CONSTANT(RACING_WHEEL_MENU);
    BIND_ENUM_CONSTANT(RACING_WHEEL_VIEW);
    BIND_ENUM_CONSTANT(RACING_WHEEL_PREVIOUS_GEAR);
    BIND_ENUM_CONSTANT(RACING_WHEEL_NEXT_GEAR);
    BIND_ENUM_CONSTANT(RACING_WHEEL_DPAD_UP);
    BIND_ENUM_CONSTANT(RACING_WHEEL_DPAD_DOWN);
    BIND_ENUM_CONSTANT(RACING_WHEEL_DPAD_LEFT);
    BIND_ENUM_CONSTANT(RACING_WHEEL_DPAD_RIGHT);
    BIND_ENUM_CONSTANT(RACING_WHEEL_A);
    BIND_ENUM_CONSTANT(RACING_WHEEL_B);
    BIND_ENUM_CONSTANT(RACING_WHEEL_X);
    BIND_ENUM_CONSTANT(RACING_WHEEL_Y);
    BIND_ENUM_CONSTANT(RACING_WHEEL_LEFT_THUMB);
    BIND_ENUM_CONSTANT(RACING_WHEEL_RIGHT_THUMB);

    BIND_ENUM_CONSTANT(MOUSE_NONE);
    BIND_ENUM_CONSTANT(MOUSE_LEFT);
    BIND_ENUM_CONSTANT(MOUSE_RIGHT);
    BIND_ENUM_CONSTANT(MOUSE_MIDDLE);
    BIND_ENUM_CONSTANT(MOUSE_XBUTTON1);
    BIND_ENUM_CONSTANT(MOUSE_XBUTTON2);
    BIND_ENUM_CONSTANT(MOUSE_WHEEL_TILT_LEFT);
    BIND_ENUM_CONSTANT(MOUSE_WHEEL_TILT_RIGHT);

    BIND_ENUM_CONSTANT(SENSOR_NONE);
    BIND_ENUM_CONSTANT(SENSOR_ACCELEROMETER);
    BIND_ENUM_CONSTANT(SENSOR_GYROMETER);
    BIND_ENUM_CONSTANT(SENSOR_COMPASS);
    BIND_ENUM_CONSTANT(SENSOR_ORIENTATION);

    BIND_ENUM_CONSTANT(SENSOR_ACCURACY_UNKNOWN);
    BIND_ENUM_CONSTANT(SENSOR_ACCURACY_UNRELIABLE);
    BIND_ENUM_CONSTANT(SENSOR_ACCURACY_APPROXIMATE);
    BIND_ENUM_CONSTANT(SENSOR_ACCURACY_HIGH);

    BIND_ENUM_CONSTANT(FAMILY_VIRTUAL);
    BIND_ENUM_CONSTANT(FAMILY_UNKNOWN);
    BIND_ENUM_CONSTANT(FAMILY_XBOX_ONE);
    BIND_ENUM_CONSTANT(FAMILY_XBOX_360);
    BIND_ENUM_CONSTANT(FAMILY_HID);
    BIND_ENUM_CONSTANT(FAMILY_I8042);
    BIND_ENUM_CONSTANT(FAMILY_AGGREGATE);

    BIND_ENUM_CONSTANT(STATUS_NONE);
    BIND_ENUM_CONSTANT(STATUS_CONNECTED);
    BIND_ENUM_CONSTANT(STATUS_HAPTIC_INFO_READY);

    BIND_ENUM_CONSTANT(SYSTEM_BUTTON_NONE);
    BIND_ENUM_CONSTANT(SYSTEM_BUTTON_GUIDE);
    BIND_ENUM_CONSTANT(SYSTEM_BUTTON_SHARE);

    BIND_ENUM_CONSTANT(SWITCH_CENTER);
    BIND_ENUM_CONSTANT(SWITCH_UP);
    BIND_ENUM_CONSTANT(SWITCH_UP_RIGHT);
    BIND_ENUM_CONSTANT(SWITCH_RIGHT);
    BIND_ENUM_CONSTANT(SWITCH_DOWN_RIGHT);
    BIND_ENUM_CONSTANT(SWITCH_DOWN);
    BIND_ENUM_CONSTANT(SWITCH_DOWN_LEFT);
    BIND_ENUM_CONSTANT(SWITCH_LEFT);
    BIND_ENUM_CONSTANT(SWITCH_UP_LEFT);

    BIND_ENUM_CONSTANT(KEYBOARD_UNKNOWN);
    BIND_ENUM_CONSTANT(KEYBOARD_ANSI);
    BIND_ENUM_CONSTANT(KEYBOARD_ISO);
    BIND_ENUM_CONSTANT(KEYBOARD_KS);
    BIND_ENUM_CONSTANT(KEYBOARD_ABNT);
    BIND_ENUM_CONSTANT(KEYBOARD_JIS);

    BIND_ENUM_CONSTANT(RUMBLE_NONE);
    BIND_ENUM_CONSTANT(RUMBLE_LOW_FREQUENCY);
    BIND_ENUM_CONSTANT(RUMBLE_HIGH_FREQUENCY);
    BIND_ENUM_CONSTANT(RUMBLE_LEFT_TRIGGER);
    BIND_ENUM_CONSTANT(RUMBLE_RIGHT_TRIGGER);
}

String GameInputDevice::get_display_name() const {
    GameInput *gi = GameInput::get_singleton();
    if (!gi) return String();
    return gi->device_get_display_name(m_id);
}

int GameInputDevice::get_kind_mask() const {
    GameInput *gi = GameInput::get_singleton();
    int mask = (int)GameInput::DEVICE_UNKNOWN;
    if (gi) gi->device_lookup(m_id, nullptr, &mask);
    return mask;
}

bool GameInputDevice::is_connected() const {
    GameInput *gi = GameInput::get_singleton();
    return gi ? gi->device_is_connected(m_id) : false;
}

bool GameInputDevice::supports_vibration() const {
    GameInput *gi = GameInput::get_singleton();
    return gi ? gi->device_supports_vibration(m_id) : false;
}

bool GameInputDevice::supports_haptics() const {
    GameInput *gi = GameInput::get_singleton();
    return gi ? gi->device_supports_haptics(m_id) : false;
}

Dictionary GameInputDevice::get_device_info() const {
    GameInput *gi = GameInput::get_singleton();
    return gi ? gi->device_get_device_info(m_id) : Dictionary();
}

int GameInputDevice::get_status() const {
    GameInput *gi = GameInput::get_singleton();
    return gi ? gi->device_get_status(m_id) : (int)STATUS_NONE;
}

String GameInputDevice::get_app_local_id() const {
    GameInput *gi = GameInput::get_singleton();
    return gi ? gi->device_get_app_local_id(m_id) : String();
}

int GameInputDevice::get_device_family() const {
    GameInput *gi = GameInput::get_singleton();
    return gi ? gi->device_get_family(m_id) : (int)FAMILY_UNKNOWN;
}

int GameInputDevice::get_supported_rumble_motors() const {
    GameInput *gi = GameInput::get_singleton();
    return gi ? gi->device_get_supported_rumble_motors(m_id) : (int)RUMBLE_NONE;
}

int GameInputDevice::get_supported_system_buttons() const {
    GameInput *gi = GameInput::get_singleton();
    return gi ? gi->device_get_supported_system_buttons(m_id) : (int)SYSTEM_BUTTON_NONE;
}

int GameInputDevice::get_system_buttons() const {
    GameInput *gi = GameInput::get_singleton();
    return gi ? gi->device_get_system_buttons(m_id) : (int)SYSTEM_BUTTON_NONE;
}

int64_t GameInputDevice::get_keyboard_layout() const {
    GameInput *gi = GameInput::get_singleton();
    return gi ? gi->device_get_keyboard_layout(m_id) : 0;
}

String GameInputDevice::get_button_label(int source) const {
    GameInput *gi = GameInput::get_singleton();
    return gi ? gi->device_get_button_label(m_id, source) : String();
}

Dictionary GameInputDevice::get_haptic_info() const {
    GameInput *gi = GameInput::get_singleton();
    return gi ? gi->device_get_haptic_info(m_id) : Dictionary();
}

bool GameInputDevice::start_vibration(float weak_magnitude, float strong_magnitude, float duration,
                                      float left_trigger, float right_trigger) {
    GameInput *gi = GameInput::get_singleton();
    return gi ? gi->device_start_vibration(m_id, weak_magnitude, strong_magnitude, duration,
                                           left_trigger, right_trigger)
              : false;
}

void GameInputDevice::stop_vibration() {
    GameInput *gi = GameInput::get_singleton();
    if (gi) gi->device_stop_vibration(m_id);
}

bool GameInputDevice::is_vibrating() const {
    GameInput *gi = GameInput::get_singleton();
    return gi ? gi->device_is_vibrating(m_id) : false;
}

Vector2 GameInputDevice::get_vibration_strength() const {
    GameInput *gi = GameInput::get_singleton();
    return gi ? gi->device_get_vibration_strength(m_id) : Vector2();
}

Vector2 GameInputDevice::get_trigger_vibration_strength() const {
    GameInput *gi = GameInput::get_singleton();
    return gi ? gi->device_get_trigger_vibration_strength(m_id) : Vector2();
}

float GameInputDevice::get_vibration_duration() const {
    GameInput *gi = GameInput::get_singleton();
    return gi ? gi->device_get_vibration_duration(m_id) : 0.0f;
}

float GameInputDevice::get_vibration_remaining_duration() const {
    GameInput *gi = GameInput::get_singleton();
    return gi ? gi->device_get_vibration_remaining_duration(m_id) : 0.0f;
}

int GameInputDevice::get_force_feedback_motor_count() const {
    GameInput *gi = GameInput::get_singleton();
    return gi ? gi->device_get_ffb_motor_count(m_id) : 0;
}

Dictionary GameInputDevice::get_force_feedback_motor_info(int motor_index) const {
    GameInput *gi = GameInput::get_singleton();
    return gi ? gi->device_get_ffb_motor_info(m_id, motor_index) : Dictionary();
}

bool GameInputDevice::is_force_feedback_motor_powered_on(int motor_index) const {
    GameInput *gi = GameInput::get_singleton();
    return gi ? gi->device_is_ffb_motor_powered_on(m_id, motor_index) : false;
}

bool GameInputDevice::set_force_feedback_motor_gain(int motor_index, float gain) {
    GameInput *gi = GameInput::get_singleton();
    return gi ? gi->device_set_ffb_motor_gain(m_id, motor_index, gain) : false;
}

Ref<GameInputForceFeedbackEffect> GameInputDevice::create_force_feedback_effect(
        int motor_index, const Dictionary &params) {
    GameInput *gi = GameInput::get_singleton();
    return gi ? gi->device_create_ffb_effect(m_id, motor_index, params)
              : Ref<GameInputForceFeedbackEffect>();
}

int GameInputDevice::button_to_source(int button) {
    // Button values are single bits and Source ids 0-29 are their bit index.
    const uint32_t bits = (uint32_t)button;
    if (bits != 0 && (bits & ~kGamepadButtonMask) == 0 && (bits & (bits - 1)) == 0) {
        int index = 0;
        while (((bits >> index) & 1u) == 0) {
            ++index;
        }
        return index;
    }
    return SRC_BTN_A; // safe default
}

int GameInputDevice::axis_to_source(int axis) {
    if (axis >= AXIS_LEFT_X && axis <= AXIS_FLIGHT_THROTTLE) {
        return SRC_AXIS_LEFT_X + axis;
    }
    return SRC_AXIS_LEFT_X;
}

int64_t GameInputDevice::scan_code_to_physical_key(int64_t scan_code) {
    if (scan_code < 0 || scan_code > 0xFFFFFFFFll) return 0;
    return gameinput_internal::scan_code_to_key((uint32_t)scan_code);
}

int64_t GameInputDevice::virtual_key_to_keycode(int64_t virtual_key) {
    if (virtual_key < 0 || virtual_key > 0xFF) return 0;
    return gameinput_internal::virtual_key_to_key((uint32_t)virtual_key);
}

Vector2 GameInputDevice::switch_position_to_vector(int position) {
    constexpr real_t d = (real_t)0.70710678118654752440;
    switch (position) {
        case SWITCH_UP:         return Vector2(0, -1);
        case SWITCH_UP_RIGHT:   return Vector2(d, -d);
        case SWITCH_RIGHT:      return Vector2(1, 0);
        case SWITCH_DOWN_RIGHT: return Vector2(d, d);
        case SWITCH_DOWN:       return Vector2(0, 1);
        case SWITCH_DOWN_LEFT:  return Vector2(-d, d);
        case SWITCH_LEFT:       return Vector2(-1, 0);
        case SWITCH_UP_LEFT:    return Vector2(-d, -d);
        default:                return Vector2();
    }
}

} // namespace godot
