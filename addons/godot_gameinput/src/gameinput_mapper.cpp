#include "gameinput_mapper.h"

#include "gameinput_singleton.h"
#include "gameinput_action_map.h"
#include "gameinput_binding.h"
#include "gameinput_device.h"
#include "gameinput_reading.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/input_event_action.hpp>
#include <godot_cpp/classes/input_event_joypad_button.hpp>
#include <godot_cpp/classes/input_event_joypad_motion.hpp>
#include <godot_cpp/classes/input_map.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cmath>

namespace godot {

GameInputMapper::GameInputMapper() {
    set_process(true);
}

GameInputMapper::~GameInputMapper() {
    _release_held_actions();
    _disconnect_action_map_changed(m_action_map);
}

void GameInputMapper::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_action_map", "map"), &GameInputMapper::set_action_map);
    ClassDB::bind_method(D_METHOD("get_action_map"), &GameInputMapper::get_action_map);
    ClassDB::bind_method(D_METHOD("set_target_kind_mask", "mask"),
                         &GameInputMapper::set_target_kind_mask);
    ClassDB::bind_method(D_METHOD("get_target_kind_mask"),
                         &GameInputMapper::get_target_kind_mask);
    ClassDB::bind_method(D_METHOD("set_target_device_id", "id"),
                         &GameInputMapper::set_target_device_id);
    ClassDB::bind_method(D_METHOD("get_target_device_id"),
                         &GameInputMapper::get_target_device_id);
    ClassDB::bind_method(D_METHOD("get_active_binding_count"),
                         &GameInputMapper::get_active_binding_count);
#ifndef NDEBUG
    ClassDB::bind_method(D_METHOD("_test_mark_binding_pressed", "binding_index"),
                         &GameInputMapper::_test_mark_binding_pressed);
    ClassDB::bind_method(D_METHOD("_test_prime_native_handles_cache", "binding_index", "native_handles"),
                         &GameInputMapper::_test_prime_native_handles_cache);
    ClassDB::bind_method(D_METHOD("_test_get_native_handles_cache_count"),
                         &GameInputMapper::_test_get_native_handles_cache_count);
    ClassDB::bind_method(D_METHOD("_test_native_handles_binding", "binding"),
                         &GameInputMapper::_test_native_handles_binding);
#endif

    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "action_map",
                              PROPERTY_HINT_RESOURCE_TYPE, "GameInputActionMap"),
                 "set_action_map", "get_action_map");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "target_kind_mask",
                              PROPERTY_HINT_FLAGS,
                              "Gamepad,Keyboard,Mouse,Arcade Stick,Flight Stick,Racing Wheel"),
                 "set_target_kind_mask", "get_target_kind_mask");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "target_device_id"),
                 "set_target_device_id", "get_target_device_id");

    BIND_ENUM_CONSTANT(KIND_GAMEPAD);
    BIND_ENUM_CONSTANT(KIND_KEYBOARD);
    BIND_ENUM_CONSTANT(KIND_MOUSE);
    BIND_ENUM_CONSTANT(KIND_ARCADE_STICK);
    BIND_ENUM_CONSTANT(KIND_FLIGHT_STICK);
    BIND_ENUM_CONSTANT(KIND_RACING_WHEEL);
}

void GameInputMapper::_notification(int p_what) {
    if (p_what == NOTIFICATION_PROCESS) {
        _process_bindings();
    } else if (p_what == NOTIFICATION_EXIT_TREE) {
        _release_held_actions();
    }
}

void GameInputMapper::set_action_map(const Ref<GameInputActionMap> &map) {
    if (m_action_map == map) {
        return;
    }

    _release_held_actions();
    _disconnect_action_map_changed(m_action_map);
    m_action_map = map;
    _connect_action_map_changed(m_action_map);
    _forget_pressed_state();
    _invalidate_native_handles_cache();
    m_warned_missing_actions.clear();
}

Ref<GameInputActionMap> GameInputMapper::get_action_map() const {
    return m_action_map;
}

void GameInputMapper::set_target_kind_mask(int mask) {
    if (m_target_kind_mask == mask) {
        return;
    }
    _release_held_actions();
    m_target_kind_mask = mask;
}
int GameInputMapper::get_target_kind_mask() const { return m_target_kind_mask; }

void GameInputMapper::set_target_device_id(int64_t id) {
    if (m_target_device_id == id) {
        return;
    }
    _release_held_actions();
    m_target_device_id = id;
}
int64_t GameInputMapper::get_target_device_id() const { return m_target_device_id; }

int GameInputMapper::get_active_binding_count() const {
    int n = 0;
    for (const KeyValue<int, bool> &kv : m_prev_pressed) {
        if (kv.value) n++;
    }
    return n;
}

#ifndef NDEBUG
void GameInputMapper::_test_mark_binding_pressed(int binding_index) {
    if (m_action_map.is_null()) {
        return;
    }
    Ref<GameInputBinding> binding = m_action_map->get_binding(binding_index);
    if (binding.is_null()) {
        return;
    }
    StringName action = binding->get_action();
    if (action == StringName()) {
        return;
    }
    Input *input = Input::get_singleton();
    if (input) {
        input->action_press(action, 1.0f);
    }
    m_prev_pressed[binding_index] = true;
    m_prev_actions[binding_index] = action;
}

void GameInputMapper::_test_prime_native_handles_cache(int binding_index, bool native_handles) {
    m_native_handles_cache[binding_index] = native_handles;
}

int GameInputMapper::_test_get_native_handles_cache_count() const {
    return (int)m_native_handles_cache.size();
}

bool GameInputMapper::_test_native_handles_binding(const Ref<GameInputBinding> &binding) const {
    if (binding.is_null()) {
        return false;
    }
    return _native_path_handles_binding(binding, binding->get_action());
}
#endif

void GameInputMapper::_release_held_action_for(int binding_index) {
    bool was_pressed = false;
    if (HashMap<int, bool>::Iterator it = m_prev_pressed.find(binding_index)) {
        was_pressed = it->value;
    }

    if (was_pressed) {
        StringName action;
        if (HashMap<int, StringName>::Iterator ait = m_prev_actions.find(binding_index)) {
            action = ait->value;
        }
        if (action == StringName() && m_action_map.is_valid()) {
            Ref<GameInputBinding> binding = m_action_map->get_binding(binding_index);
            if (binding.is_valid()) {
                action = binding->get_action();
            }
        }

        Input *input = Input::get_singleton();
        InputMap *imap = InputMap::get_singleton();
        if (input && action != StringName()) {
            input->action_release(action);
            if (imap && imap->has_action(action)) {
                Ref<InputEventAction> evt;
                evt.instantiate();
                evt->set_action(action);
                evt->set_pressed(false);
                input->parse_input_event(evt);
            }
        }
    }

    m_prev_pressed.erase(binding_index);
    m_prev_actions.erase(binding_index);
}

void GameInputMapper::_release_held_actions() {
    Vector<int> keys;
    for (const KeyValue<int, bool> &kv : m_prev_pressed) {
        keys.push_back(kv.key);
    }
    for (int i = 0; i < keys.size(); ++i) {
        _release_held_action_for(keys[i]);
    }
}

void GameInputMapper::_forget_pressed_state() {
    m_prev_pressed.clear();
    m_prev_actions.clear();
}

void GameInputMapper::_invalidate_native_handles_cache() {
    m_native_handles_cache.clear();
    m_native_handles_cache_frame = UINT64_MAX;
}

void GameInputMapper::_connect_action_map_changed(const Ref<GameInputActionMap> &map) {
    if (map.is_null()) {
        return;
    }
    Callable callback = callable_mp(this, &GameInputMapper::_on_action_map_changed);
    if (!map->is_connected("changed", callback)) {
        map->connect("changed", callback);
    }
}

void GameInputMapper::_disconnect_action_map_changed(const Ref<GameInputActionMap> &map) {
    if (map.is_null()) {
        return;
    }
    Callable callback = callable_mp(this, &GameInputMapper::_on_action_map_changed);
    if (map->is_connected("changed", callback)) {
        map->disconnect("changed", callback);
    }
}

void GameInputMapper::_on_action_map_changed() {
    _release_held_actions();
    _forget_pressed_state();
    _invalidate_native_handles_cache();
    m_warned_missing_actions.clear();
}

bool GameInputMapper::_is_pressed_for(int source, float &out_strength,
                                      GameInputReading *reading) const {
    using GD = GameInputDevice;
    if (!reading) {
        out_strength = 0.0f;
        return false;
    }

    // Axes need binding-level interpretation (deadzone, invert, threshold);
    // the caller handles them, so an axis source never reports "pressed" here.
    if (source >= GD::SRC_AXIS_LEFT_X && source <= GD::SRC_AXIS_FLIGHT_THROTTLE) {
        out_strength = reading->get_source_value(source);
        return false;
    }

    // Gamepad, arcade stick, flight stick and racing wheel buttons. Unknown
    // sources and sources for kinds the device did not report read as up.
    bool down = reading->is_source_down(source);
    out_strength = down ? 1.0f : 0.0f;
    return down;
}

void GameInputMapper::_process_bindings() {
    if (m_action_map.is_null()) {
        _release_held_actions();
        return;
    }

    Engine *engine = Engine::get_singleton();
    uint64_t frame = engine ? engine->get_process_frames() : 0;
    if (frame != m_native_handles_cache_frame) {
        m_native_handles_cache.clear();
        m_native_handles_cache_frame = frame;
    }

    GameInput *gi = GameInput::get_singleton();
    if (!gi || !gi->is_initialized()) {
        _release_held_actions();
        return;
    }

    gi->poll(); // idempotent per frame

    Ref<GameInputDevice> device;
    if (m_target_device_id >= 0) {
        // Look the id up directly: DEVICE_ALL keeps its v1 meaning (gamepad,
        // keyboard, mouse), so a racing-wheel-only or arcade-stick-only
        // device would never be found by filtering get_devices(DEVICE_ALL).
        device = gi->get_device_by_id(m_target_device_id);
    } else {
        device = gi->get_primary_device(m_target_kind_mask);
    }

    if (device.is_null() || !device->is_connected()) {
        _release_held_actions();
        return;
    }

    Ref<GameInputReading> reading = gi->get_current_reading(device);
    if (reading.is_null()) {
        _release_held_actions();
        return;
    }

    Input *input = Input::get_singleton();
    InputMap *imap = InputMap::get_singleton();
    if (!input || !imap) {
        _release_held_actions();
        return;
    }

    int n = m_action_map->get_binding_count();
    Vector<int> stale_binding_indices;
    for (const KeyValue<int, bool> &kv : m_prev_pressed) {
        if (kv.key < 0 || kv.key >= n) {
            stale_binding_indices.push_back(kv.key);
        }
    }
    for (int i = 0; i < stale_binding_indices.size(); ++i) {
        _release_held_action_for(stale_binding_indices[i]);
    }

    for (int i = 0; i < n; ++i) {
        Ref<GameInputBinding> b = m_action_map->get_binding(i);
        if (b.is_null()) {
            _release_held_action_for(i);
            continue;
        }

        StringName action = b->get_action();
        if (action == StringName()) {
            _release_held_action_for(i);
            continue;
        }

        bool was_pressed = false;
        if (HashMap<int, bool>::Iterator it = m_prev_pressed.find(i)) {
            was_pressed = it->value;
        }
        if (was_pressed) {
            StringName prev_action;
            if (HashMap<int, StringName>::Iterator ait = m_prev_actions.find(i)) {
                prev_action = ait->value;
            }
            if (prev_action != StringName() && prev_action != action) {
                _release_held_action_for(i);
                was_pressed = false;
            }
        }

        if (!imap->has_action(action)) {
            _release_held_action_for(i);
            if (!m_warned_missing_actions.has(action)) {
                m_warned_missing_actions.insert(action);
                UtilityFunctions::push_warning(
                    "GameInputMapper: action '", String(action),
                    "' is not in the project's InputMap. The binding will be ignored.");
            }
            continue;
        }

        bool is_pressed = false;
        float strength = 0.0f;

        if (b->get_is_axis()) {
            float raw = 0.0f;
            _is_pressed_for(b->get_source(), raw, reading.ptr());
            if (b->get_axis_invert()) raw = -raw;
            float deadzone = b->get_deadzone();
            float magnitude = std::fabs(raw);
            if (magnitude > deadzone) {
                strength = (magnitude - deadzone) / std::max(0.0001f, 1.0f - deadzone);
                if (strength > 1.0f) strength = 1.0f;
                // For axis-as-button: only fire if direction matches a positive value.
                // We treat positive axis (after invert) above threshold as "pressed".
                float threshold = b->get_axis_threshold();
                is_pressed = (raw > 0.0f) && (magnitude > threshold);
            }
        } else {
            float s = 0.0f;
            is_pressed = _is_pressed_for(b->get_source(), s, reading.ptr());
            strength = s;
        }

        if (is_pressed) {
            // Keep polled state fresh every frame so analog strength tracks (e.g.,
            // paddle handlers reading Input.is_action_pressed / get_action_strength).
            input->action_press(action, strength);
        } else if (was_pressed) {
            input->action_release(action);
        }

        // Event-based handlers (Viewport GUI focus traversal for ui_*, Control
        // _gui_input listeners, Node _input/_unhandled_input handlers) listen
        // to InputEvent objects and don't see Input.action_press alone. Emit a
        // transition InputEventAction so ui_up / ui_down / ui_accept / etc.
        // actually drive Godot's UI nav.
        //
        // Skip the emit when Godot's built-in joypad backend is already wired
        // to fire the same action (e.g., default project bindings have
        // ui_accept ← JoypadButton(A) ← physical A press): in that case the
        // engine emits its own equivalent InputEvent and double-firing makes
        // menu items unselectable. The check is per-binding and cached so the
        // hot path stays cheap.
        if (is_pressed != was_pressed) {
            bool native_handles = false;
            if (HashMap<int, bool>::Iterator hit = m_native_handles_cache.find(i)) {
                native_handles = hit->value;
            } else {
                native_handles = _native_path_handles_binding(b, action);
                m_native_handles_cache[i] = native_handles;
            }

            if (!native_handles) {
                Ref<InputEventAction> evt;
                evt.instantiate();
                evt->set_action(action);
                evt->set_pressed(is_pressed);
                evt->set_strength(is_pressed ? strength : 0.0f);
                input->parse_input_event(evt);
            }
        }

        m_prev_pressed[i] = is_pressed;
        if (is_pressed) {
            m_prev_actions[i] = action;
        } else {
            m_prev_actions.erase(i);
        }
    }
}

int GameInputMapper::_source_to_joy_button(int source) {
    using GD = GameInputDevice;
    // Maps a GameInput Source enum value to Godot's JoyButton enum integer
    // (matches godot-cpp's JoyButton). Returns -1 for axis sources or unknowns.
    switch (source) {
        case GD::SRC_BTN_A:              return 0;  // JOY_BUTTON_A
        case GD::SRC_BTN_B:              return 1;  // JOY_BUTTON_B
        case GD::SRC_BTN_X:              return 2;  // JOY_BUTTON_X
        case GD::SRC_BTN_Y:              return 3;  // JOY_BUTTON_Y
        case GD::SRC_BTN_VIEW:           return 4;  // JOY_BUTTON_BACK
        case GD::SRC_BTN_MENU:           return 6;  // JOY_BUTTON_START
        case GD::SRC_BTN_LEFT_THUMB:     return 7;  // JOY_BUTTON_LEFT_STICK
        case GD::SRC_BTN_RIGHT_THUMB:    return 8;  // JOY_BUTTON_RIGHT_STICK
        case GD::SRC_BTN_LEFT_SHOULDER:  return 9;  // JOY_BUTTON_LEFT_SHOULDER
        case GD::SRC_BTN_RIGHT_SHOULDER: return 10; // JOY_BUTTON_RIGHT_SHOULDER
        case GD::SRC_BTN_DPAD_UP:        return 11; // JOY_BUTTON_DPAD_UP
        case GD::SRC_BTN_DPAD_DOWN:      return 12; // JOY_BUTTON_DPAD_DOWN
        case GD::SRC_BTN_DPAD_LEFT:      return 13; // JOY_BUTTON_DPAD_LEFT
        case GD::SRC_BTN_DPAD_RIGHT:     return 14; // JOY_BUTTON_DPAD_RIGHT
        // Godot follows SDL's paddle order: right upper, left upper, right
        // lower, left lower (Xbox Elite P1, P3, P2, P4).
        case GD::SRC_BTN_PADDLE_RIGHT_1: return 16; // JOY_BUTTON_PADDLE1
        case GD::SRC_BTN_PADDLE_LEFT_1:  return 17; // JOY_BUTTON_PADDLE2
        case GD::SRC_BTN_PADDLE_RIGHT_2: return 18; // JOY_BUTTON_PADDLE3
        case GD::SRC_BTN_PADDLE_LEFT_2:  return 19; // JOY_BUTTON_PADDLE4
        default: return -1;
    }
}

int GameInputMapper::_source_to_joy_axis(int source) {
    using GD = GameInputDevice;
    // Maps a GameInput Source enum value to Godot's JoyAxis enum integer.
    // Returns -1 for button sources or unknowns.
    switch (source) {
        case GD::SRC_AXIS_LEFT_X:        return 0;  // JOY_AXIS_LEFT_X
        case GD::SRC_AXIS_LEFT_Y:        return 1;  // JOY_AXIS_LEFT_Y
        case GD::SRC_AXIS_RIGHT_X:       return 2;  // JOY_AXIS_RIGHT_X
        case GD::SRC_AXIS_RIGHT_Y:       return 3;  // JOY_AXIS_RIGHT_Y
        case GD::SRC_AXIS_LEFT_TRIGGER:  return 4;  // JOY_AXIS_TRIGGER_LEFT
        case GD::SRC_AXIS_RIGHT_TRIGGER: return 5;  // JOY_AXIS_TRIGGER_RIGHT
        default: return -1;
    }
}

bool GameInputMapper::_source_to_joy_axis_direction(int source, int &r_axis, float &r_sign) {
    using GD = GameInputDevice;
    // GameInput reports the trigger buttons and thumbstick directions as
    // buttons; Godot reports the same controls as one direction of a JoyAxis.
    // Stick Y is down-positive in Godot, so "up" is the negative direction.
    switch (source) {
        case GD::SRC_BTN_LEFT_TRIGGER:      r_axis = 4; r_sign = 1.0f;  return true; // JOY_AXIS_TRIGGER_LEFT
        case GD::SRC_BTN_RIGHT_TRIGGER:     r_axis = 5; r_sign = 1.0f;  return true; // JOY_AXIS_TRIGGER_RIGHT
        case GD::SRC_BTN_LEFT_STICK_UP:     r_axis = 1; r_sign = -1.0f; return true; // JOY_AXIS_LEFT_Y
        case GD::SRC_BTN_LEFT_STICK_DOWN:   r_axis = 1; r_sign = 1.0f;  return true;
        case GD::SRC_BTN_LEFT_STICK_LEFT:   r_axis = 0; r_sign = -1.0f; return true; // JOY_AXIS_LEFT_X
        case GD::SRC_BTN_LEFT_STICK_RIGHT:  r_axis = 0; r_sign = 1.0f;  return true;
        case GD::SRC_BTN_RIGHT_STICK_UP:    r_axis = 3; r_sign = -1.0f; return true; // JOY_AXIS_RIGHT_Y
        case GD::SRC_BTN_RIGHT_STICK_DOWN:  r_axis = 3; r_sign = 1.0f;  return true;
        case GD::SRC_BTN_RIGHT_STICK_LEFT:  r_axis = 2; r_sign = -1.0f; return true; // JOY_AXIS_RIGHT_X
        case GD::SRC_BTN_RIGHT_STICK_RIGHT: r_axis = 2; r_sign = 1.0f;  return true;
        default: return false;
    }
}

namespace {

// True when `events` holds an InputEventJoypadMotion on `axis` in the
// `pressed_sign` direction. An axis_value of 0 is a wildcard so
// unconfigured-direction events still suppress the mapper's emit.
bool has_joy_motion(const TypedArray<Ref<InputEvent>> &events, int axis, float pressed_sign) {
    for (int i = 0; i < events.size(); ++i) {
        Ref<InputEvent> ev = events[i];
        if (ev.is_null()) continue;
        Ref<InputEventJoypadMotion> motion = ev;
        if (motion.is_null()) continue;
        if ((int)motion->get_axis() != axis) continue;
        float ev_val = motion->get_axis_value();
        if (ev_val == 0.0f || ev_val * pressed_sign > 0.0f) {
            return true;
        }
    }
    return false;
}

} // namespace

bool GameInputMapper::_native_path_handles_binding(
        const Ref<GameInputBinding> &binding,
        const StringName &action) const {
    // Returns true when the project's InputMap already contains an
    // InputEventJoypadButton / InputEventJoypadMotion that matches this
    // binding's source. In that case Godot's built-in joypad backend will
    // emit an InputEvent that resolves to `action` on its own, and the
    // mapper must NOT emit its own synthetic InputEventAction or the action
    // will fire twice per physical press.
    if (binding.is_null() || action == StringName()) {
        return false;
    }
    InputMap *imap = InputMap::get_singleton();
    if (!imap || !imap->has_action(action)) {
        return false;
    }
    TypedArray<Ref<InputEvent>> events = imap->action_get_events(action);

    if (binding->get_is_axis()) {
        int target_axis = _source_to_joy_axis(binding->get_source());
        if (target_axis < 0) return false;
        // Sign of the axis value when this binding considers itself "pressed":
        // _process_bindings flips the raw value via axis_invert and treats the
        // flipped positive direction as the press, so the matching native
        // event's axis_value sign equals -1 when axis_invert is true and +1
        // otherwise.
        float pressed_sign = binding->get_axis_invert() ? -1.0f : 1.0f;
        return has_joy_motion(events, target_axis, pressed_sign);
    }

    int direction_axis = -1;
    float direction_sign = 1.0f;
    if (_source_to_joy_axis_direction(binding->get_source(), direction_axis, direction_sign)) {
        return has_joy_motion(events, direction_axis, direction_sign);
    }

    int target_button = _source_to_joy_button(binding->get_source());
    if (target_button < 0) return false;
    for (int i = 0; i < events.size(); ++i) {
        Ref<InputEvent> ev = events[i];
        if (ev.is_null()) continue;
        Ref<InputEventJoypadButton> btn = ev;
        if (btn.is_null()) continue;
        if ((int)btn->get_button_index() == target_button) {
            return true;
        }
    }
    return false;
}

} // namespace godot
