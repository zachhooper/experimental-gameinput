#ifndef GODOT_GAMEINPUT_READING_H
#define GODOT_GAMEINPUT_READING_H

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_int64_array.hpp>
#include <godot_cpp/variant/quaternion.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include "gameinput_snapshot.h"

namespace godot {

// Immutable snapshot of one device's input. GameInput.get_current_reading()
// returns the state refreshed by the last poll; the reading_received signal
// carries one reading per GameInput callback. Both keep the previous sample of
// every kind so the was_*_pressed / was_*_released edges are exact.
class GameInputReading : public RefCounted {
    GDCLASS(GameInputReading, RefCounted);

private:
    gameinput_internal::Snapshot m_cur;
    gameinput_internal::Snapshot m_prev;
    uint32_t m_prev_kinds = 0; // snapshot kind bits that have a previous sample
    bool m_gap_before = false;
    int64_t m_device_id = 0;
    uint32_t m_sensor_kinds = 0;

    bool _has_prev_for(uint32_t snap_bit) const { return (m_prev_kinds & snap_bit) != 0; }
    bool _source_down_in(const gameinput_internal::Snapshot &s, int source) const;
    uint32_t _source_snap_kind(int source) const;
    float _axis_in(const gameinput_internal::Snapshot &s, int axis) const;
    int _find_key(const gameinput_internal::Snapshot &s, int64_t physical_key) const;

protected:
    static void _bind_methods();

public:
    GameInputReading() = default;
    ~GameInputReading() = default;

    void _set_snapshot(int64_t device_id, const gameinput_internal::Snapshot &cur,
                       const gameinput_internal::Snapshot &prev, uint32_t prev_kinds,
                       bool gap_before, uint32_t sensor_kinds);
    const gameinput_internal::Snapshot &_get_current_snapshot() const { return m_cur; }

    // --- General ---------------------------------------------------------
    bool is_button_down(int button) const;
    bool was_button_pressed(int button) const;
    bool was_button_released(int button) const;
    float get_axis(int axis) const;
    int get_buttons_mask() const;
    int64_t get_timestamp() const;
    int64_t get_kind_timestamp(int kind) const;
    int64_t get_device_id() const { return m_device_id; }
    int get_input_kinds() const;
    bool has_previous() const;
    bool has_gap_before() const { return m_gap_before; }
    bool is_truncated() const;

    // --- Sources (GameInputDevice.Source) ---------------------------------
    bool is_source_down(int source) const;
    bool was_source_pressed(int source) const;
    bool was_source_released(int source) const;
    float get_source_value(int source) const;

    // --- Keyboard ----------------------------------------------------------
    int get_key_count() const;
    PackedInt64Array get_pressed_physical_keys() const;
    bool is_physical_key_down(int64_t physical_key) const;
    bool was_physical_key_pressed(int64_t physical_key) const;
    bool was_physical_key_released(int64_t physical_key) const;
    Array get_key_states() const;

    // --- Mouse -------------------------------------------------------------
    int get_mouse_buttons() const;
    bool is_mouse_button_down(int button) const;
    bool was_mouse_button_pressed(int button) const;
    bool was_mouse_button_released(int button) const;
    Vector2 get_mouse_delta() const;
    Vector2 get_mouse_wheel_delta() const;
    Vector2 get_mouse_absolute_position() const;
    bool has_mouse_absolute_position() const;
    Dictionary get_mouse_state() const;

    // --- Sensors -----------------------------------------------------------
    int get_sensor_kinds() const;
    Vector3 get_accelerometer() const;
    Vector3 get_gyroscope() const;
    float get_heading_degrees() const;
    int get_heading_accuracy() const;
    Quaternion get_orientation() const;

    // --- Arcade stick, flight stick, racing wheel ----------------------------
    int get_arcade_stick_buttons() const;
    int get_flight_stick_buttons() const;
    int get_flight_stick_hat() const;
    Vector2 get_flight_stick_hat_vector() const;
    int get_racing_wheel_buttons() const;
    int get_racing_wheel_gear() const;

    // --- Raw controller ------------------------------------------------------
    PackedFloat32Array get_controller_axes() const;
    Array get_controller_buttons() const;
    PackedInt32Array get_controller_switches() const;
    float get_controller_axis(int index) const;
    bool is_controller_button_down(int index) const;
    bool was_controller_button_pressed(int index) const;
    bool was_controller_button_released(int index) const;
    int get_controller_switch(int index) const;
};

} // namespace godot

#endif // GODOT_GAMEINPUT_READING_H
