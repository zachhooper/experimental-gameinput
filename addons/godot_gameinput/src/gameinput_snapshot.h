#ifndef GODOT_GAMEINPUT_SNAPSHOT_H
#define GODOT_GAMEINPUT_SNAPSHOT_H

// Plain-data model of one device's input state. It depends on neither Godot
// nor the GameInput headers, so the merge, edge and delta rules below are
// unit-tested in tests/cpp/gameinput without a runtime. The singleton converts
// IGameInputReading data into this shape and GameInputReading exposes it to
// scripts.

#include <cstdint>
#include <cstring>

namespace gameinput_internal {

// Presence bits. Bits 0..6 equal the matching GameInput.DeviceKind bits. The
// three controller sub-kinds are tracked separately (bits 8..10) so each keeps
// its own timestamp; public_kinds_from_snapshot() folds them into
// DEVICE_CONTROLLER (bit 7).
enum SnapshotKind : uint32_t {
    SNAP_GAMEPAD           = 1u << 0,
    SNAP_KEYBOARD          = 1u << 1,
    SNAP_MOUSE             = 1u << 2,
    SNAP_ARCADE_STICK      = 1u << 3,
    SNAP_FLIGHT_STICK      = 1u << 4,
    SNAP_RACING_WHEEL      = 1u << 5,
    SNAP_SENSORS           = 1u << 6,
    SNAP_CONTROLLER_AXIS   = 1u << 8,
    SNAP_CONTROLLER_BUTTON = 1u << 9,
    SNAP_CONTROLLER_SWITCH = 1u << 10,
};

constexpr uint32_t kSnapshotKindCount = 10;
constexpr uint32_t SNAP_CONTROLLER_ANY =
        SNAP_CONTROLLER_AXIS | SNAP_CONTROLLER_BUTTON | SNAP_CONTROLLER_SWITCH;
constexpr uint32_t SNAP_TYPED_KINDS = 0x7Fu;
constexpr uint32_t SNAP_ALL = SNAP_TYPED_KINDS | SNAP_CONTROLLER_ANY;
constexpr uint32_t PUBLIC_CONTROLLER_BIT = 1u << 7;

// Fixed capacities keep the snapshot trivially copyable so it can live in a
// preallocated ring that the GameInput worker thread writes without
// allocating. The native counts are kept alongside so truncation is visible.
constexpr uint32_t kMaxKeys = 32;
constexpr uint32_t kMaxControllerAxes = 32;
constexpr uint32_t kMaxControllerButtons = 128;
constexpr uint32_t kMaxControllerSwitches = 8;

struct GamepadData {
    uint32_t buttons = 0;
    float left_trigger = 0.0f;
    float right_trigger = 0.0f;
    float left_x = 0.0f;
    float left_y = 0.0f;   // GameInput convention: up is positive.
    float right_x = 0.0f;
    float right_y = 0.0f;  // GameInput convention: up is positive.
};

struct ArcadeStickData {
    uint32_t buttons = 0;
};

struct FlightStickData {
    uint32_t buttons = 0;
    int32_t hat = 0;       // GameInputSwitchPosition
    float roll = 0.0f;
    float pitch = 0.0f;
    float yaw = 0.0f;
    float throttle = 0.0f;
};

struct RacingWheelData {
    uint32_t buttons = 0;
    int32_t gear = 0;
    float wheel = 0.0f;
    float throttle = 0.0f;
    float brake = 0.0f;
    float clutch = 0.0f;
    float handbrake = 0.0f;
};

struct MouseData {
    uint32_t buttons = 0;
    uint32_t positions = 0; // GameInputMousePositions flags
    int64_t x = 0;          // accumulated relative motion
    int64_t y = 0;
    int64_t absolute_x = 0; // device coordinates when the absolute flag is set
    int64_t absolute_y = 0;
    int64_t wheel_x = 0;    // accumulated wheel counts
    int64_t wheel_y = 0;
};

struct SensorsData {
    float acceleration_g[3] = {0.0f, 0.0f, 0.0f};
    float angular_velocity_rad[3] = {0.0f, 0.0f, 0.0f};
    float heading_degrees = 0.0f;
    int32_t heading_accuracy = 0;
    float orientation_wxyz[4] = {1.0f, 0.0f, 0.0f, 0.0f};
};

struct KeyData {
    uint32_t scan_code = 0;
    uint32_t code_point = 0;
    uint8_t virtual_key = 0;
    bool is_dead_key = false;
};

struct Snapshot {
    uint32_t kinds = 0;
    uint64_t timestamps[kSnapshotKindCount] = {};

    GamepadData gamepad;
    ArcadeStickData arcade_stick;
    FlightStickData flight_stick;
    RacingWheelData racing_wheel;
    MouseData mouse;
    SensorsData sensors;

    uint32_t key_count = 0;
    uint32_t key_count_native = 0;
    KeyData keys[kMaxKeys];

    uint32_t axis_count = 0;
    uint32_t axis_count_native = 0;
    float axes[kMaxControllerAxes] = {};

    uint32_t button_count = 0;
    uint32_t button_count_native = 0;
    uint64_t button_bits[kMaxControllerButtons / 64] = {};

    uint32_t switch_count = 0;
    uint32_t switch_count_native = 0;
    int32_t switches[kMaxControllerSwitches] = {};
};

inline uint32_t public_kinds_from_snapshot(uint32_t snap_kinds) {
    uint32_t out = snap_kinds & SNAP_TYPED_KINDS;
    if (snap_kinds & SNAP_CONTROLLER_ANY) {
        out |= PUBLIC_CONTROLLER_BIT;
    }
    return out;
}

inline uint32_t snapshot_kinds_from_public(uint32_t public_kinds) {
    uint32_t out = public_kinds & SNAP_TYPED_KINDS;
    if (public_kinds & PUBLIC_CONTROLLER_BIT) {
        out |= SNAP_CONTROLLER_ANY;
    }
    return out;
}

// Index into Snapshot::timestamps for a single presence bit; -1 otherwise.
inline int snapshot_kind_index(uint32_t bit) {
    switch (bit) {
        case SNAP_GAMEPAD:           return 0;
        case SNAP_KEYBOARD:          return 1;
        case SNAP_MOUSE:             return 2;
        case SNAP_ARCADE_STICK:      return 3;
        case SNAP_FLIGHT_STICK:      return 4;
        case SNAP_RACING_WHEEL:      return 5;
        case SNAP_SENSORS:           return 6;
        case SNAP_CONTROLLER_AXIS:   return 7;
        case SNAP_CONTROLLER_BUTTON: return 8;
        case SNAP_CONTROLLER_SWITCH: return 9;
        default:                     return -1;
    }
}

inline uint32_t snapshot_kind_bit(int index) {
    static const uint32_t bits[kSnapshotKindCount] = {
        SNAP_GAMEPAD, SNAP_KEYBOARD, SNAP_MOUSE, SNAP_ARCADE_STICK, SNAP_FLIGHT_STICK,
        SNAP_RACING_WHEEL, SNAP_SENSORS, SNAP_CONTROLLER_AXIS, SNAP_CONTROLLER_BUTTON,
        SNAP_CONTROLLER_SWITCH,
    };
    if (index < 0 || index >= (int)kSnapshotKindCount) return 0;
    return bits[index];
}

inline void mark_kind(Snapshot &s, uint32_t bit, uint64_t timestamp) {
    int idx = snapshot_kind_index(bit);
    if (idx < 0) return;
    s.kinds |= bit;
    s.timestamps[idx] = timestamp;
}

// Timestamp of the newest data for any presence bit in `public_or_snap_bits`.
// Accepts snapshot bits; callers holding a public DeviceKind mask convert with
// snapshot_kinds_from_public() first.
inline uint64_t kinds_timestamp(const Snapshot &s, uint32_t snap_bits) {
    uint64_t newest = 0;
    for (uint32_t i = 0; i < kSnapshotKindCount; ++i) {
        uint32_t bit = snapshot_kind_bit((int)i);
        if ((snap_bits & bit) && (s.kinds & bit) && s.timestamps[i] > newest) {
            newest = s.timestamps[i];
        }
    }
    return newest;
}

inline uint64_t newest_timestamp(const Snapshot &s) {
    return kinds_timestamp(s, SNAP_ALL);
}

// Copies every kind that is present in `src` and selected by `mask` into
// `dst`, leaving the other kinds of `dst` untouched. This is how per-kind
// polls and event readings build up one device-wide state.
inline void merge_kinds(Snapshot &dst, const Snapshot &src, uint32_t mask = SNAP_ALL) {
    uint32_t present = src.kinds & mask;
    if (present & SNAP_GAMEPAD)      dst.gamepad = src.gamepad;
    if (present & SNAP_ARCADE_STICK) dst.arcade_stick = src.arcade_stick;
    if (present & SNAP_FLIGHT_STICK) dst.flight_stick = src.flight_stick;
    if (present & SNAP_RACING_WHEEL) dst.racing_wheel = src.racing_wheel;
    if (present & SNAP_MOUSE)        dst.mouse = src.mouse;
    if (present & SNAP_SENSORS)      dst.sensors = src.sensors;
    if (present & SNAP_KEYBOARD) {
        dst.key_count = src.key_count;
        dst.key_count_native = src.key_count_native;
        std::memcpy(dst.keys, src.keys, sizeof(dst.keys));
    }
    if (present & SNAP_CONTROLLER_AXIS) {
        dst.axis_count = src.axis_count;
        dst.axis_count_native = src.axis_count_native;
        std::memcpy(dst.axes, src.axes, sizeof(dst.axes));
    }
    if (present & SNAP_CONTROLLER_BUTTON) {
        dst.button_count = src.button_count;
        dst.button_count_native = src.button_count_native;
        std::memcpy(dst.button_bits, src.button_bits, sizeof(dst.button_bits));
    }
    if (present & SNAP_CONTROLLER_SWITCH) {
        dst.switch_count = src.switch_count;
        dst.switch_count_native = src.switch_count_native;
        std::memcpy(dst.switches, src.switches, sizeof(dst.switches));
    }
    for (uint32_t i = 0; i < kSnapshotKindCount; ++i) {
        uint32_t bit = snapshot_kind_bit((int)i);
        if (present & bit) {
            dst.timestamps[i] = src.timestamps[i];
        }
    }
    dst.kinds |= present;
}

// True when any stored array was cut to its fixed capacity.
inline bool is_truncated(const Snapshot &s) {
    return s.key_count_native > s.key_count || s.axis_count_native > s.axis_count ||
           s.button_count_native > s.button_count || s.switch_count_native > s.switch_count;
}

// --- Bit-mask chords ---------------------------------------------------------
// A mask is a chord: it is "down" only when every bit is held. Masks that are
// empty or carry bits outside `valid` are never down, so typos fail closed.
inline bool chord_down(uint32_t held, uint32_t mask, uint32_t valid) {
    return mask != 0 && (mask & ~valid) == 0 && (held & mask) == mask;
}

// Chord became fully held. Without a previous sample a held chord counts as a
// fresh press, which matches the v1 reading semantics.
inline bool chord_pressed(uint32_t cur, uint32_t prev, bool has_prev, uint32_t mask,
                          uint32_t valid) {
    if (!chord_down(cur, mask, valid)) return false;
    return !has_prev || !chord_down(prev, mask, valid);
}

// Chord was fully held and no longer is. Needs a previous sample.
inline bool chord_released(uint32_t cur, uint32_t prev, bool has_prev, uint32_t mask,
                           uint32_t valid) {
    if (!has_prev) return false;
    return chord_down(prev, mask, valid) && !chord_down(cur, mask, valid);
}

// --- Mouse -------------------------------------------------------------------
// GameInput accumulates relative motion and wheel counts in int64 values that
// may wrap. Subtract as uint64 so a wrap yields the small true delta instead
// of signed-overflow UB.
inline int64_t wrapping_delta(int64_t cur, int64_t prev) {
    return (int64_t)((uint64_t)cur - (uint64_t)prev);
}

// --- Raw controller buttons --------------------------------------------------
inline bool controller_button(const Snapshot &s, uint32_t index) {
    if (index >= s.button_count || index >= kMaxControllerButtons) return false;
    return (s.button_bits[index / 64] >> (index % 64)) & 1u;
}

inline void set_controller_button(Snapshot &s, uint32_t index, bool down) {
    if (index >= kMaxControllerButtons) return;
    uint64_t bit = 1ull << (index % 64);
    if (down) {
        s.button_bits[index / 64] |= bit;
    } else {
        s.button_bits[index / 64] &= ~bit;
    }
}

// --- Keyboard ----------------------------------------------------------------
inline bool has_scan_code(const Snapshot &s, uint32_t scan_code) {
    for (uint32_t i = 0; i < s.key_count && i < kMaxKeys; ++i) {
        if (s.keys[i].scan_code == scan_code) return true;
    }
    return false;
}

} // namespace gameinput_internal

#endif // GODOT_GAMEINPUT_SNAPSHOT_H
