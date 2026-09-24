#ifndef GODOT_GAMEINPUT_DEVICE_H
#define GODOT_GAMEINPUT_DEVICE_H

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2.hpp>

namespace godot {

class GameInputForceFeedbackEffect;

// GDScript-facing wrapper around an IGameInputDevice. Holds ONLY a
// session-local monotonic device id (a weak handle). All methods resolve
// through the GameInput singleton; if the device has been disconnected,
// methods return safe defaults so user code holding a stale reference does
// not crash.
class GameInputDevice : public RefCounted {
    GDCLASS(GameInputDevice, RefCounted);

public:
    // Gamepad buttons. Values equal the GameInputGamepadButtons bits, so a
    // mask of several buttons is a chord for GameInputReading.is_button_down().
    enum Button {
        BUTTON_NONE               = 0,
        BUTTON_MENU               = 1 << 0,
        BUTTON_VIEW               = 1 << 1,
        BUTTON_A                  = 1 << 2,
        BUTTON_B                  = 1 << 3,
        BUTTON_X                  = 1 << 4,
        BUTTON_Y                  = 1 << 5,
        BUTTON_DPAD_UP            = 1 << 6,
        BUTTON_DPAD_DOWN          = 1 << 7,
        BUTTON_DPAD_LEFT          = 1 << 8,
        BUTTON_DPAD_RIGHT         = 1 << 9,
        BUTTON_LEFT_SHOULDER      = 1 << 10,
        BUTTON_RIGHT_SHOULDER     = 1 << 11,
        BUTTON_LEFT_THUMB         = 1 << 12,
        BUTTON_RIGHT_THUMB        = 1 << 13,
        BUTTON_C                  = 1 << 14,
        BUTTON_Z                  = 1 << 15,
        BUTTON_LEFT_TRIGGER       = 1 << 16,
        BUTTON_RIGHT_TRIGGER      = 1 << 17,
        BUTTON_LEFT_STICK_UP      = 1 << 18,
        BUTTON_LEFT_STICK_DOWN    = 1 << 19,
        BUTTON_LEFT_STICK_LEFT    = 1 << 20,
        BUTTON_LEFT_STICK_RIGHT   = 1 << 21,
        BUTTON_RIGHT_STICK_UP     = 1 << 22,
        BUTTON_RIGHT_STICK_DOWN   = 1 << 23,
        BUTTON_RIGHT_STICK_LEFT   = 1 << 24,
        BUTTON_RIGHT_STICK_RIGHT  = 1 << 25,
        BUTTON_PADDLE_LEFT_1      = 1 << 26,
        BUTTON_PADDLE_LEFT_2      = 1 << 27,
        BUTTON_PADDLE_RIGHT_1     = 1 << 28,
        BUTTON_PADDLE_RIGHT_2     = 1 << 29,
    };
    static constexpr uint32_t kGamepadButtonMask = 0x3FFFFFFFu;

    enum Axis {
        AXIS_LEFT_X          = 0,
        AXIS_LEFT_Y          = 1,
        AXIS_RIGHT_X         = 2,
        AXIS_RIGHT_Y         = 3,
        AXIS_LEFT_TRIGGER    = 4,
        AXIS_RIGHT_TRIGGER   = 5,
        AXIS_WHEEL           = 6,
        AXIS_THROTTLE        = 7,
        AXIS_BRAKE           = 8,
        AXIS_CLUTCH          = 9,
        AXIS_HANDBRAKE       = 10,
        AXIS_FLIGHT_ROLL     = 11,
        AXIS_FLIGHT_PITCH    = 12,
        AXIS_FLIGHT_YAW      = 13,
        AXIS_FLIGHT_THROTTLE = 14,
    };

    // Source ids used by GameInputBinding.source and the GameInputReading
    // *_source helpers. Gamepad buttons are 0-29 (the bit index of the Button
    // value), axes are 100 + Axis, and the arcade stick, flight stick and
    // racing wheel buttons are 200 / 300 / 400 + the bit index of their
    // button enums. Kept as one namespace so an inspector dropdown can show a
    // single combined list.
    enum Source {
        SRC_BTN_MENU               = 0,
        SRC_BTN_VIEW               = 1,
        SRC_BTN_A                  = 2,
        SRC_BTN_B                  = 3,
        SRC_BTN_X                  = 4,
        SRC_BTN_Y                  = 5,
        SRC_BTN_DPAD_UP            = 6,
        SRC_BTN_DPAD_DOWN          = 7,
        SRC_BTN_DPAD_LEFT          = 8,
        SRC_BTN_DPAD_RIGHT         = 9,
        SRC_BTN_LEFT_SHOULDER      = 10,
        SRC_BTN_RIGHT_SHOULDER     = 11,
        SRC_BTN_LEFT_THUMB         = 12,
        SRC_BTN_RIGHT_THUMB        = 13,
        SRC_BTN_C                  = 14,
        SRC_BTN_Z                  = 15,
        SRC_BTN_LEFT_TRIGGER       = 16,
        SRC_BTN_RIGHT_TRIGGER      = 17,
        SRC_BTN_LEFT_STICK_UP      = 18,
        SRC_BTN_LEFT_STICK_DOWN    = 19,
        SRC_BTN_LEFT_STICK_LEFT    = 20,
        SRC_BTN_LEFT_STICK_RIGHT   = 21,
        SRC_BTN_RIGHT_STICK_UP     = 22,
        SRC_BTN_RIGHT_STICK_DOWN   = 23,
        SRC_BTN_RIGHT_STICK_LEFT   = 24,
        SRC_BTN_RIGHT_STICK_RIGHT  = 25,
        SRC_BTN_PADDLE_LEFT_1      = 26,
        SRC_BTN_PADDLE_LEFT_2      = 27,
        SRC_BTN_PADDLE_RIGHT_1     = 28,
        SRC_BTN_PADDLE_RIGHT_2     = 29,

        SRC_AXIS_LEFT_X            = 100,
        SRC_AXIS_LEFT_Y            = 101,
        SRC_AXIS_RIGHT_X           = 102,
        SRC_AXIS_RIGHT_Y           = 103,
        SRC_AXIS_LEFT_TRIGGER      = 104,
        SRC_AXIS_RIGHT_TRIGGER     = 105,
        SRC_AXIS_WHEEL             = 106,
        SRC_AXIS_THROTTLE          = 107,
        SRC_AXIS_BRAKE             = 108,
        SRC_AXIS_CLUTCH            = 109,
        SRC_AXIS_HANDBRAKE         = 110,
        SRC_AXIS_FLIGHT_ROLL       = 111,
        SRC_AXIS_FLIGHT_PITCH      = 112,
        SRC_AXIS_FLIGHT_YAW        = 113,
        SRC_AXIS_FLIGHT_THROTTLE   = 114,

        SRC_ARCADE_MENU            = 200,
        SRC_ARCADE_VIEW            = 201,
        SRC_ARCADE_UP              = 202,
        SRC_ARCADE_DOWN            = 203,
        SRC_ARCADE_LEFT            = 204,
        SRC_ARCADE_RIGHT           = 205,
        SRC_ARCADE_ACTION_1        = 206,
        SRC_ARCADE_ACTION_2        = 207,
        SRC_ARCADE_ACTION_3        = 208,
        SRC_ARCADE_ACTION_4        = 209,
        SRC_ARCADE_ACTION_5        = 210,
        SRC_ARCADE_ACTION_6        = 211,
        SRC_ARCADE_SPECIAL_1       = 212,
        SRC_ARCADE_SPECIAL_2       = 213,

        SRC_FLIGHT_MENU            = 300,
        SRC_FLIGHT_VIEW            = 301,
        SRC_FLIGHT_FIRE_PRIMARY    = 302,
        SRC_FLIGHT_FIRE_SECONDARY  = 303,
        SRC_FLIGHT_HAT_UP          = 304,
        SRC_FLIGHT_HAT_DOWN        = 305,
        SRC_FLIGHT_HAT_LEFT        = 306,
        SRC_FLIGHT_HAT_RIGHT       = 307,
        SRC_FLIGHT_A               = 308,
        SRC_FLIGHT_B               = 309,
        SRC_FLIGHT_X               = 310,
        SRC_FLIGHT_Y               = 311,
        SRC_FLIGHT_LEFT_SHOULDER   = 312,
        SRC_FLIGHT_RIGHT_SHOULDER  = 313,

        SRC_WHEEL_MENU             = 400,
        SRC_WHEEL_VIEW             = 401,
        SRC_WHEEL_PREVIOUS_GEAR    = 402,
        SRC_WHEEL_NEXT_GEAR        = 403,
        SRC_WHEEL_DPAD_UP          = 404,
        SRC_WHEEL_DPAD_DOWN        = 405,
        SRC_WHEEL_DPAD_LEFT        = 406,
        SRC_WHEEL_DPAD_RIGHT       = 407,
        SRC_WHEEL_A                = 408,
        SRC_WHEEL_B                = 409,
        SRC_WHEEL_X                = 410,
        SRC_WHEEL_Y                = 411,
        SRC_WHEEL_LEFT_THUMB       = 412,
        SRC_WHEEL_RIGHT_THUMB      = 413,
    };

    enum ArcadeStickButton {
        ARCADE_STICK_NONE      = 0,
        ARCADE_STICK_MENU      = 1 << 0,
        ARCADE_STICK_VIEW      = 1 << 1,
        ARCADE_STICK_UP        = 1 << 2,
        ARCADE_STICK_DOWN      = 1 << 3,
        ARCADE_STICK_LEFT      = 1 << 4,
        ARCADE_STICK_RIGHT     = 1 << 5,
        ARCADE_STICK_ACTION_1  = 1 << 6,
        ARCADE_STICK_ACTION_2  = 1 << 7,
        ARCADE_STICK_ACTION_3  = 1 << 8,
        ARCADE_STICK_ACTION_4  = 1 << 9,
        ARCADE_STICK_ACTION_5  = 1 << 10,
        ARCADE_STICK_ACTION_6  = 1 << 11,
        ARCADE_STICK_SPECIAL_1 = 1 << 12,
        ARCADE_STICK_SPECIAL_2 = 1 << 13,
    };

    enum FlightStickButton {
        FLIGHT_STICK_NONE           = 0,
        FLIGHT_STICK_MENU           = 1 << 0,
        FLIGHT_STICK_VIEW           = 1 << 1,
        FLIGHT_STICK_FIRE_PRIMARY   = 1 << 2,
        FLIGHT_STICK_FIRE_SECONDARY = 1 << 3,
        FLIGHT_STICK_HAT_UP         = 1 << 4,
        FLIGHT_STICK_HAT_DOWN       = 1 << 5,
        FLIGHT_STICK_HAT_LEFT       = 1 << 6,
        FLIGHT_STICK_HAT_RIGHT      = 1 << 7,
        FLIGHT_STICK_A              = 1 << 8,
        FLIGHT_STICK_B              = 1 << 9,
        FLIGHT_STICK_X              = 1 << 10,
        FLIGHT_STICK_Y              = 1 << 11,
        FLIGHT_STICK_LEFT_SHOULDER  = 1 << 12,
        FLIGHT_STICK_RIGHT_SHOULDER = 1 << 13,
    };

    enum RacingWheelButton {
        RACING_WHEEL_NONE          = 0,
        RACING_WHEEL_MENU          = 1 << 0,
        RACING_WHEEL_VIEW          = 1 << 1,
        RACING_WHEEL_PREVIOUS_GEAR = 1 << 2,
        RACING_WHEEL_NEXT_GEAR     = 1 << 3,
        RACING_WHEEL_DPAD_UP       = 1 << 4,
        RACING_WHEEL_DPAD_DOWN     = 1 << 5,
        RACING_WHEEL_DPAD_LEFT     = 1 << 6,
        RACING_WHEEL_DPAD_RIGHT    = 1 << 7,
        RACING_WHEEL_A             = 1 << 8,
        RACING_WHEEL_B             = 1 << 9,
        RACING_WHEEL_X             = 1 << 10,
        RACING_WHEEL_Y             = 1 << 11,
        RACING_WHEEL_LEFT_THUMB    = 1 << 12,
        RACING_WHEEL_RIGHT_THUMB   = 1 << 13,
    };

    enum MouseButton {
        MOUSE_NONE             = 0,
        MOUSE_LEFT             = 1 << 0,
        MOUSE_RIGHT            = 1 << 1,
        MOUSE_MIDDLE           = 1 << 2,
        MOUSE_XBUTTON1         = 1 << 3,
        MOUSE_XBUTTON2         = 1 << 4,
        MOUSE_WHEEL_TILT_LEFT  = 1 << 5,
        MOUSE_WHEEL_TILT_RIGHT = 1 << 6,
    };

    enum SensorKind {
        SENSOR_NONE          = 0,
        SENSOR_ACCELEROMETER = 1 << 0,
        SENSOR_GYROMETER     = 1 << 1,
        SENSOR_COMPASS       = 1 << 2,
        SENSOR_ORIENTATION   = 1 << 3,
    };

    enum SensorAccuracy {
        SENSOR_ACCURACY_UNKNOWN     = 0,
        SENSOR_ACCURACY_UNRELIABLE  = 1,
        SENSOR_ACCURACY_APPROXIMATE = 2,
        SENSOR_ACCURACY_HIGH        = 3,
    };

    enum DeviceFamily {
        FAMILY_VIRTUAL   = -1,
        FAMILY_UNKNOWN   = 0,
        FAMILY_XBOX_ONE  = 1,
        FAMILY_XBOX_360  = 2,
        FAMILY_HID       = 3,
        FAMILY_I8042     = 4,
        FAMILY_AGGREGATE = 5,
    };

    enum DeviceStatus {
        STATUS_NONE              = 0,
        STATUS_CONNECTED         = 0x00000001,
        STATUS_HAPTIC_INFO_READY = 0x00200000,
    };

    enum SystemButton {
        SYSTEM_BUTTON_NONE  = 0,
        SYSTEM_BUTTON_GUIDE = 1 << 0,
        SYSTEM_BUTTON_SHARE = 1 << 1,
    };

    enum SwitchPosition {
        SWITCH_CENTER     = 0,
        SWITCH_UP         = 1,
        SWITCH_UP_RIGHT   = 2,
        SWITCH_RIGHT      = 3,
        SWITCH_DOWN_RIGHT = 4,
        SWITCH_DOWN       = 5,
        SWITCH_DOWN_LEFT  = 6,
        SWITCH_LEFT       = 7,
        SWITCH_UP_LEFT    = 8,
    };

    enum KeyboardKind {
        KEYBOARD_UNKNOWN = -1,
        KEYBOARD_ANSI    = 0,
        KEYBOARD_ISO     = 1,
        KEYBOARD_KS      = 2,
        KEYBOARD_ABNT    = 3,
        KEYBOARD_JIS     = 4,
    };

    enum RumbleMotor {
        RUMBLE_NONE           = 0,
        RUMBLE_LOW_FREQUENCY  = 1 << 0,
        RUMBLE_HIGH_FREQUENCY = 1 << 1,
        RUMBLE_LEFT_TRIGGER   = 1 << 2,
        RUMBLE_RIGHT_TRIGGER  = 1 << 3,
    };

private:
    int64_t m_id = 0;

protected:
    static void _bind_methods();

public:
    GameInputDevice() = default;
    ~GameInputDevice() = default;

    // Internal: set by GameInput when constructing the wrapper.
    void _set_device_id(int64_t id) { m_id = id; }

    int64_t get_device_id() const { return m_id; }
    String get_display_name() const;
    int get_kind_mask() const;
    bool is_connected() const;
    bool supports_vibration() const;
    bool supports_haptics() const;
    Dictionary get_device_info() const;

    int get_status() const;
    String get_app_local_id() const;
    int get_device_family() const;
    int get_supported_rumble_motors() const;
    int get_supported_system_buttons() const;
    int get_system_buttons() const;
    int64_t get_keyboard_layout() const;
    String get_button_label(int source) const;
    Dictionary get_haptic_info() const;

    bool start_vibration(float weak_magnitude, float strong_magnitude, float duration = 0.0f,
                         float left_trigger = 0.0f, float right_trigger = 0.0f);
    void stop_vibration();
    bool is_vibrating() const;
    Vector2 get_vibration_strength() const;
    Vector2 get_trigger_vibration_strength() const;
    float get_vibration_duration() const;
    float get_vibration_remaining_duration() const;

    int get_force_feedback_motor_count() const;
    Dictionary get_force_feedback_motor_info(int motor_index) const;
    bool is_force_feedback_motor_powered_on(int motor_index) const;
    bool set_force_feedback_motor_gain(int motor_index, float gain);
    Ref<GameInputForceFeedbackEffect> create_force_feedback_effect(int motor_index,
                                                                   const Dictionary &params);

    // Helpers used internally + exposed to GDScript for convenience.
    static int button_to_source(int button);
    static int axis_to_source(int axis);
    static int64_t scan_code_to_physical_key(int64_t scan_code);
    static int64_t virtual_key_to_keycode(int64_t virtual_key);
    static Vector2 switch_position_to_vector(int position);
};

} // namespace godot

VARIANT_ENUM_CAST(godot::GameInputDevice::Button);
VARIANT_ENUM_CAST(godot::GameInputDevice::Axis);
VARIANT_ENUM_CAST(godot::GameInputDevice::Source);
VARIANT_ENUM_CAST(godot::GameInputDevice::ArcadeStickButton);
VARIANT_ENUM_CAST(godot::GameInputDevice::FlightStickButton);
VARIANT_ENUM_CAST(godot::GameInputDevice::RacingWheelButton);
VARIANT_ENUM_CAST(godot::GameInputDevice::MouseButton);
VARIANT_ENUM_CAST(godot::GameInputDevice::SensorKind);
VARIANT_ENUM_CAST(godot::GameInputDevice::SensorAccuracy);
VARIANT_ENUM_CAST(godot::GameInputDevice::DeviceFamily);
VARIANT_ENUM_CAST(godot::GameInputDevice::DeviceStatus);
VARIANT_ENUM_CAST(godot::GameInputDevice::SystemButton);
VARIANT_ENUM_CAST(godot::GameInputDevice::SwitchPosition);
VARIANT_ENUM_CAST(godot::GameInputDevice::KeyboardKind);
VARIANT_ENUM_CAST(godot::GameInputDevice::RumbleMotor);

#endif // GODOT_GAMEINPUT_DEVICE_H
