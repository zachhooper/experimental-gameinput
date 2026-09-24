#ifndef GODOT_GAMEINPUT_SINGLETON_H
#define GODOT_GAMEINPUT_SINGLETON_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2.hpp>

#include <GameInput.h>

// vcpkg's `gameinput` port ships the GameInput v3 redistributable, which puts
// every public symbol inside `namespace GameInput::v3`. The addon was written
// against the GDK's v1 header where the same symbols sit at global scope. Pull
// the v3 namespace into the global scope so the addon source compiles unchanged.
using namespace ::GameInput::v3;

#include "gameinput_event_queue.h"
#include "gameinput_snapshot.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

namespace godot {

class GameInputDevice;
class GameInputForceFeedbackEffect;
class GameInputReading;

// Engine singleton. Owns GameInput lifetime, the device cache, and main-thread
// poll dispatch. All public methods are GDScript-safe: they soft-fail (return
// safe defaults + push_warning once) when GameInput is not initialized or not
// available on this host.
//
// Threading contract:
//   * IGameInput callbacks (device status, readings, system buttons, keyboard
//     layout) run on GameInput-owned worker threads. They ONLY copy data into
//     the pending queues under m_event_mutex, AddRef any IGameInputDevice*
//     they keep, and stamp each event with one global sequence number.
//   * Status, system-button and layout events go into an unbounded list and
//     are never dropped. Readings go into a preallocated ring that drops the
//     oldest entry when full (counted by get_dropped_reading_count()).
//   * The main thread drains both inside poll(), in sequence order, and is
//     the only thread that mutates m_devices or emits signals.
//   * GameInput::poll() is per-frame idempotent: a counter check against
//     Engine::get_singleton()->get_process_frames() ensures the real refresh
//     only runs once per frame regardless of how many GameInputMapper nodes
//     call poll() defensively.
class GameInput : public Object {
    GDCLASS(GameInput, Object);

public:
    // DEVICE_ALL keeps its v1 value (gamepad | keyboard | mouse) so existing
    // projects see the same devices. DEVICE_ANY selects every kind.
    enum DeviceKind {
        DEVICE_UNKNOWN      = 0,
        DEVICE_GAMEPAD      = 1 << 0,
        DEVICE_KEYBOARD     = 1 << 1,
        DEVICE_MOUSE        = 1 << 2,
        DEVICE_ALL          = DEVICE_GAMEPAD | DEVICE_KEYBOARD | DEVICE_MOUSE,
        DEVICE_ARCADE_STICK = 1 << 3,
        DEVICE_FLIGHT_STICK = 1 << 4,
        DEVICE_RACING_WHEEL = 1 << 5,
        DEVICE_SENSORS      = 1 << 6,
        DEVICE_CONTROLLER   = 1 << 7,
        DEVICE_ANY          = 0xFF,
    };

    // Values equal GameInputFocusPolicy so they pass straight through.
    enum FocusPolicy {
        FOCUS_POLICY_DEFAULT                           = 0x000,
        FOCUS_POLICY_EXCLUSIVE_FOREGROUND_INPUT        = 0x002,
        FOCUS_POLICY_EXCLUSIVE_FOREGROUND_GUIDE_BUTTON = 0x008,
        FOCUS_POLICY_EXCLUSIVE_FOREGROUND_SHARE_BUTTON = 0x020,
        FOCUS_POLICY_ENABLE_BACKGROUND_INPUT           = 0x040,
        FOCUS_POLICY_ENABLE_BACKGROUND_GUIDE_BUTTON    = 0x080,
        FOCUS_POLICY_ENABLE_BACKGROUND_SHARE_BUTTON    = 0x100,
    };
    static constexpr int kFocusPolicyMask = 0x1EA;

    static constexpr uint32_t kReadingRingCapacity = 512;

private:
    static GameInput *singleton;

    enum class Backend { None, Native, Mock };

    IGameInput *m_game_input = nullptr;
    Backend m_backend = Backend::None;
    GameInputCallbackToken m_device_callback_token = 0;
    GameInputCallbackToken m_reading_callback_token = 0;
    GameInputCallbackToken m_system_button_callback_token = 0;
    GameInputCallbackToken m_keyboard_layout_callback_token = 0;
    // Token of the live reading registration; callbacks carrying any other
    // token (a late call from an earlier registration) are ignored.
    std::atomic<uint64_t> m_active_reading_token{0};
    std::atomic_bool m_accepting_callbacks{false};
    std::atomic_bool m_accepting_readings{false};
    bool m_initialized = false;
    bool m_warned_uninitialized = false;
    bool m_warned_create_failed = false;

    std::atomic<bool> m_shutting_down{false};
    std::atomic<int32_t> m_callbacks_in_flight{0};
    std::atomic<int64_t> m_next_device_id{1};
    std::atomic<int64_t> m_next_effect_id{1};
    uint64_t m_last_polled_frame = UINT64_MAX;
    // Bumped by initialize() and shutdown(). The drain compares it after every
    // signal emission so a handler that restarts the runtime stops delivery of
    // events that belong to the old session.
    uint64_t m_generation = 0;
    // Snapshot kinds captured by the reading callback. Written only while no
    // reading registration is live.
    uint32_t m_reading_snap_kinds = 0;

    // Requested configuration. Kept across shutdown()/initialize(); the
    // project settings apply at initialize() unless a setter overrode them.
    int m_reading_callback_kinds = 0;
    bool m_reading_callback_kinds_overridden = false;
    int m_focus_policy = FOCUS_POLICY_DEFAULT;
    bool m_focus_policy_overridden = false;

    // --- Worker -> main thread queues -------------------------------------
    enum class PendingEventKind { DeviceStatus, SystemButtons, KeyboardLayout };
    struct PendingEvent {
        PendingEventKind kind = PendingEventKind::DeviceStatus;
        uint64_t seq = 0;
        IGameInputDevice *native_device = nullptr; // AddRef'd; null for mock devices
        int64_t mock_device_id = 0;
        uint64_t timestamp = 0;
        uint32_t current = 0;
        uint32_t previous = 0;
    };
    struct ReadingEvent {
        uint64_t seq = 0;
        IGameInputDevice *native_device = nullptr; // AddRef'd; null for mock devices
        int64_t mock_device_id = 0;
        gameinput_internal::Snapshot snapshot;
    };
    using ReadingRing = gameinput_internal::BoundedRing<ReadingEvent>;

    std::mutex m_event_mutex;
    uint64_t m_next_event_seq = 1;                      // guarded by m_event_mutex
    LocalVector<PendingEvent> m_pending_events;         // guarded by m_event_mutex
    std::unique_ptr<ReadingRing> m_reading_ring;        // guarded by m_event_mutex
    std::unique_ptr<ReadingRing> m_reading_ring_spare;  // main thread only
    bool m_reading_ring_overflowed = false;             // guarded by m_event_mutex
    std::atomic<uint64_t> m_dropped_reading_count{0};

    // --- Main-thread device cache ------------------------------------------
    // Immutable facts cached at connect time (GameInputDeviceInfo does not
    // change while a device stays connected).
    struct DeviceCaps {
        uint32_t native_supported_input = 0;
        uint32_t rumble_motors = 0;
        uint32_t system_buttons = 0;
        uint32_t sensors = 0;
        int32_t family = 0;
        LocalVector<uint32_t> ffb_motor_axes;
        LocalVector<uint32_t> ffb_motor_effects; // bit n = EffectKind n supported
    };

    struct DeviceEntry {
        int64_t id = 0;
        IGameInputDevice *native = nullptr; // AddRef'd; Released on disconnect
        bool is_mock = false;
        Ref<GameInputDevice> wrapper;       // shared with GDScript
        int kind_mask = DEVICE_UNKNOWN;
        uint32_t status = 0;
        uint32_t system_buttons = 0;
        uint32_t keyboard_layout = 0;
        DeviceCaps caps;
        Dictionary mock_info;

        // Poll lane: refreshed once per real poll, read by get_current_reading().
        bool poll_has_cur = false;
        gameinput_internal::Snapshot poll_cur;
        gameinput_internal::Snapshot poll_prev; // .kinds = kinds with a previous sample
        // Mock input waiting for the next real poll.
        gameinput_internal::Snapshot mock_state;

        // Event lane: the state after the last reading_received emission.
        gameinput_internal::Snapshot event_state;
        bool event_gap_pending = false;
        Array frame_readings;

        // Vibration requested through set_vibration()/start_vibration().
        float rumble[4] = {0.0f, 0.0f, 0.0f, 0.0f}; // low, high, left trigger, right trigger
        bool rumble_active = false;
        uint64_t rumble_end_usec = 0; // 0 = until stopped
        double rumble_duration = 0.0;
        uint64_t rumble_apply_count = 0;
    };
    Vector<DeviceEntry> m_devices;

    struct EffectEntry {
        int64_t id = 0;
        int64_t device_id = 0;
        uint32_t motor_index = 0;
        int kind = 0;
        IGameInputForceFeedbackEffect *native = nullptr; // owned; null for mock devices
        int mock_state = 0;
        float mock_gain = 1.0f;
        GameInputForceFeedbackParams params{};
    };
    HashMap<int64_t, EffectEntry> m_effects;

    // Mock devices announced through _test_inject_device() whose connect
    // event has not been drained yet.
    HashMap<int64_t, Dictionary> m_mock_pending_infos;
    int64_t m_time_override_usec = -1;

    // --- Internal helpers --------------------------------------------------
    bool _enter_callback();
    void _leave_callback();
    bool _queue_event(PendingEvent &ev);
    bool _queue_reading(ReadingEvent &ev);
    void _load_settings();
    void _release_pending_events_locked();
    void _clear_pending_events();
    void _ensure_reading_rings();
    bool _apply_reading_callback_registration();
    void _unregister_callback(GameInputCallbackToken &token, const char *what);
    void _wait_for_callbacks();
    void _drain_callback_events();
    void _handle_pending_event(PendingEvent &ev);
    void _handle_device_status(PendingEvent &ev);
    void _handle_reading_event(ReadingEvent &ev);
    void _remove_device_at(int idx);
    void _real_poll();
    void _poll_native_device(DeviceEntry &entry);
    void _update_vibration_timers();
    int _find_index_by_id(int64_t id) const;
    int _find_index_by_native(IGameInputDevice *native) const;
    int _find_index_for_event(IGameInputDevice *native, int64_t mock_id) const;
    Ref<GameInputDevice> _make_wrapper(int64_t id);
    bool _ensure_initialized();
    int _kind_mask_from_supported(uint32_t native_kinds) const;
    uint64_t _now_usec() const;
    bool _apply_rumble(DeviceEntry &entry, const float values[4]);
    void _release_effects_for_device(int64_t device_id, bool stop_first);
    void _release_all_effects();
    EffectEntry *_find_effect(int64_t effect_id);
    Dictionary _build_mock_device_info(const DeviceEntry &entry) const;
    void _fill_caps_from_native(IGameInputDevice *native, DeviceCaps &caps) const;
    void _fill_caps_from_mock(const Dictionary &info, DeviceCaps &caps) const;
    Ref<GameInputReading> _make_reading(const DeviceEntry &entry,
                                        const gameinput_internal::Snapshot &cur,
                                        const gameinput_internal::Snapshot &prev,
                                        uint32_t prev_kinds, bool gap_before) const;
    void _emit_status_change(const DeviceEntry &entry, uint32_t current, uint32_t previous,
                             uint64_t timestamp);

    static void _fill_snapshot_from_reading(IGameInputReading *reading, uint32_t wanted_snap_kinds,
                                            gameinput_internal::Snapshot &out);

protected:
    static void _bind_methods();

public:
    static GameInput *get_singleton();

    GameInput();
    ~GameInput();

    bool initialize();
    void shutdown();
    bool is_initialized() const;
    void poll();

    // Engine-exit hook called from the GDExtension main-loop shutdown callback
    // (register_types.cpp). Shuts the runtime down and disconnects every signal
    // connection while script languages are still alive. Not bound.
    void _on_engine_shutdown();

    Array get_devices(int kind_mask = DEVICE_GAMEPAD);
    Ref<GameInputDevice> get_primary_device(int kind_mask = DEVICE_GAMEPAD);
    Ref<GameInputDevice> get_device_by_id(int64_t device_id);
    Ref<GameInputReading> get_current_reading(const Ref<GameInputDevice> &device);
    int get_connected_device_count(int kind_mask = DEVICE_ALL) const;

    bool set_vibration(const Ref<GameInputDevice> &device, float low_freq, float high_freq,
                       float left_trigger = 0.0f, float right_trigger = 0.0f);
    void stop_haptics(const Ref<GameInputDevice> &device);

    int64_t get_current_timestamp();

    bool set_reading_callback_kinds(int kind_mask);
    int get_reading_callback_kinds() const;
    Array get_buffered_readings(const Ref<GameInputDevice> &device);
    int64_t get_dropped_reading_count() const;

    void set_focus_policy(int policy);
    int get_focus_policy() const;

    String create_aggregate_device(int kind);
    bool disable_aggregate_device(const String &app_local_id);

    // === Internal helpers used by GameInputDevice / GameInputReading / effects ===
    // Return true if the id is still connected. Out-params filled when true.
    bool device_lookup(int64_t id, IGameInputDevice **out_native, int *out_kind_mask);
    String device_get_display_name(int64_t id);
    bool device_is_connected(int64_t id);
    Dictionary device_get_device_info(int64_t id);
    bool device_supports_vibration(int64_t id);
    bool device_supports_haptics(int64_t id);
    int device_get_status(int64_t id);
    String device_get_app_local_id(int64_t id);
    int device_get_family(int64_t id);
    int device_get_supported_rumble_motors(int64_t id);
    int device_get_supported_system_buttons(int64_t id);
    int device_get_system_buttons(int64_t id);
    int device_get_keyboard_layout(int64_t id);
    String device_get_button_label(int64_t id, int source);
    Dictionary device_get_haptic_info(int64_t id);

    bool device_start_vibration(int64_t id, float weak, float strong, float duration,
                                float left_trigger, float right_trigger);
    void device_stop_vibration(int64_t id);
    bool device_is_vibrating(int64_t id);
    Vector2 device_get_vibration_strength(int64_t id);
    Vector2 device_get_trigger_vibration_strength(int64_t id);
    float device_get_vibration_duration(int64_t id);
    float device_get_vibration_remaining_duration(int64_t id);

    int device_get_ffb_motor_count(int64_t id);
    Dictionary device_get_ffb_motor_info(int64_t id, int motor_index);
    bool device_is_ffb_motor_powered_on(int64_t id, int motor_index);
    bool device_set_ffb_motor_gain(int64_t id, int motor_index, float gain);
    Ref<GameInputForceFeedbackEffect> device_create_ffb_effect(int64_t id, int motor_index,
                                                               const Dictionary &params);

    bool effect_is_valid(int64_t effect_id);
    int64_t effect_get_device_id(int64_t effect_id);
    int effect_get_motor_index(int64_t effect_id);
    int effect_get_kind(int64_t effect_id);
    int effect_get_state(int64_t effect_id);
    bool effect_set_state(int64_t effect_id, int state);
    float effect_get_gain(int64_t effect_id);
    bool effect_set_gain(int64_t effect_id, float gain);
    Dictionary effect_get_params(int64_t effect_id);
    bool effect_set_params(int64_t effect_id, const Dictionary &params);
    void effect_release(int64_t effect_id);

#ifndef NDEBUG
    // Debug-only seams for the GUT suites and the sample self-test. They drive
    // the same queues and drain path as the native callbacks, so everything
    // downstream of the worker threads is exercised without hardware.
    bool _test_initialize_mock();
    int _test_get_backend() const;
    int64_t _test_inject_device(const Dictionary &info);
    bool _test_remove_device(int64_t device_id);
    bool _test_set_device_status(int64_t device_id, int status);
    bool _test_push_reading(int64_t device_id, const Dictionary &state);
    bool _test_push_system_buttons(int64_t device_id, int buttons);
    bool _test_push_keyboard_layout(int64_t device_id, int layout);
    Dictionary _test_get_last_rumble(int64_t device_id);
    void _test_set_time_override_usec(int64_t usec);
    int _test_get_effect_count() const;
    void _test_force_poll();
#endif

    // Static C callbacks (invoked from GameInput worker threads).
    static void CALLBACK _on_device_callback(GameInputCallbackToken token, void *context,
                                             IGameInputDevice *device, uint64_t timestamp,
                                             GameInputDeviceStatus current_status,
                                             GameInputDeviceStatus previous_status);
    static void CALLBACK _on_reading_callback(GameInputCallbackToken token, void *context,
                                              IGameInputReading *reading);
    static void CALLBACK _on_system_button_callback(GameInputCallbackToken token, void *context,
                                                    IGameInputDevice *device, uint64_t timestamp,
                                                    GameInputSystemButtons current_buttons,
                                                    GameInputSystemButtons previous_buttons);
    static void CALLBACK _on_keyboard_layout_callback(GameInputCallbackToken token, void *context,
                                                      IGameInputDevice *device, uint64_t timestamp,
                                                      uint32_t current_layout,
                                                      uint32_t previous_layout);
};

} // namespace godot

VARIANT_ENUM_CAST(godot::GameInput::DeviceKind);
VARIANT_ENUM_CAST(godot::GameInput::FocusPolicy);

#endif // GODOT_GAMEINPUT_SINGLETON_H
