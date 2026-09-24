#include "gameinput_force_feedback_effect.h"

#include "gameinput_singleton.h"

namespace godot {

void GameInputForceFeedbackEffect::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_valid"), &GameInputForceFeedbackEffect::is_valid);
    ClassDB::bind_method(D_METHOD("get_device_id"), &GameInputForceFeedbackEffect::get_device_id);
    ClassDB::bind_method(D_METHOD("get_motor_index"),
                         &GameInputForceFeedbackEffect::get_motor_index);
    ClassDB::bind_method(D_METHOD("get_kind"), &GameInputForceFeedbackEffect::get_kind);
    ClassDB::bind_method(D_METHOD("start"), &GameInputForceFeedbackEffect::start);
    ClassDB::bind_method(D_METHOD("stop"), &GameInputForceFeedbackEffect::stop);
    ClassDB::bind_method(D_METHOD("pause"), &GameInputForceFeedbackEffect::pause);
    ClassDB::bind_method(D_METHOD("get_state"), &GameInputForceFeedbackEffect::get_state);
    ClassDB::bind_method(D_METHOD("set_state", "state"), &GameInputForceFeedbackEffect::set_state);
    ClassDB::bind_method(D_METHOD("get_gain"), &GameInputForceFeedbackEffect::get_gain);
    ClassDB::bind_method(D_METHOD("set_gain", "gain"), &GameInputForceFeedbackEffect::set_gain);
    ClassDB::bind_method(D_METHOD("get_params"), &GameInputForceFeedbackEffect::get_params);
    ClassDB::bind_method(D_METHOD("set_params", "params"),
                         &GameInputForceFeedbackEffect::set_params);
    ClassDB::bind_method(D_METHOD("release"), &GameInputForceFeedbackEffect::release);

    BIND_ENUM_CONSTANT(EFFECT_CONSTANT);
    BIND_ENUM_CONSTANT(EFFECT_RAMP);
    BIND_ENUM_CONSTANT(EFFECT_SINE_WAVE);
    BIND_ENUM_CONSTANT(EFFECT_SQUARE_WAVE);
    BIND_ENUM_CONSTANT(EFFECT_TRIANGLE_WAVE);
    BIND_ENUM_CONSTANT(EFFECT_SAWTOOTH_UP);
    BIND_ENUM_CONSTANT(EFFECT_SAWTOOTH_DOWN);
    BIND_ENUM_CONSTANT(EFFECT_SPRING);
    BIND_ENUM_CONSTANT(EFFECT_FRICTION);
    BIND_ENUM_CONSTANT(EFFECT_DAMPER);
    BIND_ENUM_CONSTANT(EFFECT_INERTIA);

    BIND_ENUM_CONSTANT(STATE_STOPPED);
    BIND_ENUM_CONSTANT(STATE_RUNNING);
    BIND_ENUM_CONSTANT(STATE_PAUSED);

    BIND_ENUM_CONSTANT(FEEDBACK_AXIS_NONE);
    BIND_ENUM_CONSTANT(FEEDBACK_AXIS_LINEAR_X);
    BIND_ENUM_CONSTANT(FEEDBACK_AXIS_LINEAR_Y);
    BIND_ENUM_CONSTANT(FEEDBACK_AXIS_LINEAR_Z);
    BIND_ENUM_CONSTANT(FEEDBACK_AXIS_ANGULAR_X);
    BIND_ENUM_CONSTANT(FEEDBACK_AXIS_ANGULAR_Y);
    BIND_ENUM_CONSTANT(FEEDBACK_AXIS_ANGULAR_Z);
    BIND_ENUM_CONSTANT(FEEDBACK_AXIS_NORMAL);
}

GameInputForceFeedbackEffect::~GameInputForceFeedbackEffect() {
    if (m_id == 0) return;
    GameInput *gi = GameInput::get_singleton();
    if (gi) gi->effect_release(m_id);
    m_id = 0;
}

bool GameInputForceFeedbackEffect::is_valid() const {
    GameInput *gi = GameInput::get_singleton();
    return gi && m_id != 0 && gi->effect_is_valid(m_id);
}

int64_t GameInputForceFeedbackEffect::get_device_id() const {
    GameInput *gi = GameInput::get_singleton();
    return gi ? gi->effect_get_device_id(m_id) : 0;
}

int GameInputForceFeedbackEffect::get_motor_index() const {
    GameInput *gi = GameInput::get_singleton();
    return gi ? gi->effect_get_motor_index(m_id) : -1;
}

int GameInputForceFeedbackEffect::get_kind() const {
    GameInput *gi = GameInput::get_singleton();
    return gi ? gi->effect_get_kind(m_id) : -1;
}

bool GameInputForceFeedbackEffect::start() {
    return set_state(STATE_RUNNING);
}

bool GameInputForceFeedbackEffect::stop() {
    return set_state(STATE_STOPPED);
}

bool GameInputForceFeedbackEffect::pause() {
    return set_state(STATE_PAUSED);
}

int GameInputForceFeedbackEffect::get_state() const {
    GameInput *gi = GameInput::get_singleton();
    return gi ? gi->effect_get_state(m_id) : (int)STATE_STOPPED;
}

bool GameInputForceFeedbackEffect::set_state(int state) {
    GameInput *gi = GameInput::get_singleton();
    return gi ? gi->effect_set_state(m_id, state) : false;
}

float GameInputForceFeedbackEffect::get_gain() const {
    GameInput *gi = GameInput::get_singleton();
    return gi ? gi->effect_get_gain(m_id) : 0.0f;
}

bool GameInputForceFeedbackEffect::set_gain(float gain) {
    GameInput *gi = GameInput::get_singleton();
    return gi ? gi->effect_set_gain(m_id, gain) : false;
}

Dictionary GameInputForceFeedbackEffect::get_params() const {
    GameInput *gi = GameInput::get_singleton();
    return gi ? gi->effect_get_params(m_id) : Dictionary();
}

bool GameInputForceFeedbackEffect::set_params(const Dictionary &params) {
    GameInput *gi = GameInput::get_singleton();
    return gi ? gi->effect_set_params(m_id, params) : false;
}

void GameInputForceFeedbackEffect::release() {
    if (m_id == 0) return;
    GameInput *gi = GameInput::get_singleton();
    if (gi) gi->effect_release(m_id);
    m_id = 0;
}

} // namespace godot
