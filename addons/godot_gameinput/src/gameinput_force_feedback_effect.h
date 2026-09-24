#ifndef GODOT_GAMEINPUT_FORCE_FEEDBACK_EFFECT_H
#define GODOT_GAMEINPUT_FORCE_FEEDBACK_EFFECT_H

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/dictionary.hpp>

namespace godot {

// Weak handle to an IGameInputForceFeedbackEffect owned by the GameInput
// singleton. Created by GameInputDevice.create_force_feedback_effect(). The
// native effect is released when this wrapper is freed, when release() is
// called, when its device disconnects, or on GameInput.shutdown(); after that
// every method returns a safe default.
class GameInputForceFeedbackEffect : public RefCounted {
    GDCLASS(GameInputForceFeedbackEffect, RefCounted);

public:
    enum EffectKind {
        EFFECT_CONSTANT      = 0,
        EFFECT_RAMP          = 1,
        EFFECT_SINE_WAVE     = 2,
        EFFECT_SQUARE_WAVE   = 3,
        EFFECT_TRIANGLE_WAVE = 4,
        EFFECT_SAWTOOTH_UP   = 5,
        EFFECT_SAWTOOTH_DOWN = 6,
        EFFECT_SPRING        = 7,
        EFFECT_FRICTION      = 8,
        EFFECT_DAMPER        = 9,
        EFFECT_INERTIA       = 10,
    };

    enum EffectState {
        STATE_STOPPED = 0,
        STATE_RUNNING = 1,
        STATE_PAUSED  = 2,
    };

    enum FeedbackAxis {
        FEEDBACK_AXIS_NONE      = 0,
        FEEDBACK_AXIS_LINEAR_X  = 1 << 0,
        FEEDBACK_AXIS_LINEAR_Y  = 1 << 1,
        FEEDBACK_AXIS_LINEAR_Z  = 1 << 2,
        FEEDBACK_AXIS_ANGULAR_X = 1 << 3,
        FEEDBACK_AXIS_ANGULAR_Y = 1 << 4,
        FEEDBACK_AXIS_ANGULAR_Z = 1 << 5,
        FEEDBACK_AXIS_NORMAL    = 1 << 6,
    };

private:
    int64_t m_id = 0;

protected:
    static void _bind_methods();

public:
    GameInputForceFeedbackEffect() = default;
    ~GameInputForceFeedbackEffect();

    // Internal: set by GameInput when constructing the wrapper.
    void _set_effect_id(int64_t id) { m_id = id; }
    int64_t _get_effect_id() const { return m_id; }

    bool is_valid() const;
    int64_t get_device_id() const;
    int get_motor_index() const;
    int get_kind() const;

    bool start();
    bool stop();
    bool pause();
    int get_state() const;
    bool set_state(int state);

    float get_gain() const;
    bool set_gain(float gain);

    Dictionary get_params() const;
    bool set_params(const Dictionary &params);

    void release();
};

} // namespace godot

VARIANT_ENUM_CAST(godot::GameInputForceFeedbackEffect::EffectKind);
VARIANT_ENUM_CAST(godot::GameInputForceFeedbackEffect::EffectState);
VARIANT_ENUM_CAST(godot::GameInputForceFeedbackEffect::FeedbackAxis);

#endif // GODOT_GAMEINPUT_FORCE_FEEDBACK_EFFECT_H
