#include "gameinput_singleton.h"

#include "gameinput_device.h"
#include "gameinput_force_feedback_effect.h"
#include "gameinput_labels.h"
#include "gameinput_reading.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/quaternion.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>
#include <type_traits>

namespace godot {

namespace gi = gameinput_internal;

static constexpr const char *kReadingCallbackKindsSetting =
        "game_input/runtime/reading_callback_kinds";
static constexpr const char *kFocusPolicySetting = "game_input/runtime/focus_policy";

// vcpkg's `gameinput` port ships the GameInput v3 redistributable, which
// returns device info via an HRESULT + out-param instead of the v1 direct
// pointer return. This helper bridges the v3 shape back to the v1-style
// `nullptr-on-failure` ergonomics used throughout this file.
static inline const GameInputDeviceInfo *_get_device_info(IGameInputDevice *device) {
    if (!device) return nullptr;
    const GameInputDeviceInfo *info = nullptr;
    HRESULT hr = device->GetDeviceInfo(&info);
    return SUCCEEDED(hr) ? info : nullptr;
}

template <typename DeviceT>
static inline HRESULT _set_rumble_state_checked_impl(DeviceT *device,
                                                     const GameInputRumbleParams *params) {
    using ReturnT = decltype(device->SetRumbleState(params));
    if constexpr (std::is_same_v<ReturnT, void>) {
        device->SetRumbleState(params);
        return S_OK;
    } else {
        ReturnT result = device->SetRumbleState(params);
        if constexpr (std::is_same_v<ReturnT, HRESULT>) {
            return result;
        } else if constexpr (std::is_same_v<ReturnT, bool>) {
            return result ? S_OK : E_FAIL;
        } else {
            return (HRESULT)result;
        }
    }
}

static inline HRESULT _set_rumble_state_checked(IGameInputDevice *device,
                                                const GameInputRumbleParams *params) {
    if (!device || !params) {
        return E_POINTER;
    }
    return _set_rumble_state_checked_impl(device, params);
}

namespace {

// Every kind the addon reads. Raw device reports are deliberately excluded
// (see spec/gdext-gameinput.md, "Deferred").
constexpr uint32_t kNativeReadableKinds =
        (uint32_t)GameInputKindControllerAxis | (uint32_t)GameInputKindControllerButton |
        (uint32_t)GameInputKindControllerSwitch | (uint32_t)GameInputKindKeyboard |
        (uint32_t)GameInputKindMouse | (uint32_t)GameInputKindSensors |
        (uint32_t)GameInputKindArcadeStick | (uint32_t)GameInputKindFlightStick |
        (uint32_t)GameInputKindGamepad | (uint32_t)GameInputKindRacingWheel;

constexpr uint32_t kNativeControllerKinds = (uint32_t)GameInputKindControllerAxis |
                                            (uint32_t)GameInputKindControllerButton |
                                            (uint32_t)GameInputKindControllerSwitch;

struct KindPair {
    uint32_t native;
    uint32_t snap;
};

const KindPair kKindPairs[] = {
    {(uint32_t)GameInputKindGamepad, gi::SNAP_GAMEPAD},
    {(uint32_t)GameInputKindKeyboard, gi::SNAP_KEYBOARD},
    {(uint32_t)GameInputKindMouse, gi::SNAP_MOUSE},
    {(uint32_t)GameInputKindArcadeStick, gi::SNAP_ARCADE_STICK},
    {(uint32_t)GameInputKindFlightStick, gi::SNAP_FLIGHT_STICK},
    {(uint32_t)GameInputKindRacingWheel, gi::SNAP_RACING_WHEEL},
    {(uint32_t)GameInputKindSensors, gi::SNAP_SENSORS},
    {(uint32_t)GameInputKindControllerAxis, gi::SNAP_CONTROLLER_AXIS},
    {(uint32_t)GameInputKindControllerButton, gi::SNAP_CONTROLLER_BUTTON},
    {(uint32_t)GameInputKindControllerSwitch, gi::SNAP_CONTROLLER_SWITCH},
};

uint32_t snap_kinds_from_native(uint32_t native) {
    uint32_t out = 0;
    for (const KindPair &p : kKindPairs) {
        if (native & p.native) out |= p.snap;
    }
    return out;
}

uint32_t native_kinds_from_snap(uint32_t snap) {
    uint32_t out = 0;
    for (const KindPair &p : kKindPairs) {
        if (snap & p.snap) out |= p.native;
    }
    return out;
}

// Each group is read with its own GetCurrentReading() call so a device that
// reports kinds in separate readings (a keyboard with a built-in pointer, a
// gamepad with motion sensors) keeps every kind current.
const uint32_t kPollGroups[] = {
    (uint32_t)GameInputKindGamepad,
    (uint32_t)GameInputKindKeyboard,
    (uint32_t)GameInputKindMouse,
    (uint32_t)GameInputKindSensors,
    (uint32_t)GameInputKindArcadeStick,
    (uint32_t)GameInputKindFlightStick,
    (uint32_t)GameInputKindRacingWheel,
    kNativeControllerKinds,
};

// NaN maps to 0 so a bad value can never leave a motor running.
float clamp01(float v) {
    if (!(v > 0.0f)) return 0.0f;
    return v < 1.0f ? v : 1.0f;
}

float clamp_signed(float v) {
    if (std::isnan(v)) return 0.0f;
    return std::max(-1.0f, std::min(1.0f, v));
}

String hresult_to_string(HRESULT hr) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08lX", (unsigned long)(uint32_t)hr);
    return String(buf);
}

String guid_to_string(const GUID &g) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
                  (unsigned long)g.Data1, (unsigned)g.Data2, (unsigned)g.Data3,
                  (unsigned)g.Data4[0], (unsigned)g.Data4[1], (unsigned)g.Data4[2],
                  (unsigned)g.Data4[3], (unsigned)g.Data4[4], (unsigned)g.Data4[5],
                  (unsigned)g.Data4[6], (unsigned)g.Data4[7]);
    return String(buf);
}

String version_to_string(const GameInputVersion &v) {
    return String::num_int64(v.major) + "." + String::num_int64(v.minor) + "." +
           String::num_int64(v.build) + "." + String::num_int64(v.revision);
}

String app_local_id_to_string(const APP_LOCAL_DEVICE_ID &id) {
    static const char *kHex = "0123456789abcdef";
    char buf[sizeof(id.value) * 2 + 1];
    for (size_t i = 0; i < sizeof(id.value); ++i) {
        buf[i * 2] = kHex[(id.value[i] >> 4) & 0xF];
        buf[i * 2 + 1] = kHex[id.value[i] & 0xF];
    }
    buf[sizeof(id.value) * 2] = '\0';
    return String(buf);
}

int hex_digit(char32_t c) {
    if (c >= '0' && c <= '9') return (int)(c - '0');
    if (c >= 'a' && c <= 'f') return (int)(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return (int)(c - 'A' + 10);
    return -1;
}

bool app_local_id_from_string(const String &text, APP_LOCAL_DEVICE_ID &out) {
    String s = text.strip_edges();
    if (s.length() != (int64_t)(sizeof(out.value) * 2)) return false;
    for (size_t i = 0; i < sizeof(out.value); ++i) {
        int hi = hex_digit(s[(int64_t)(i * 2)]);
        int lo = hex_digit(s[(int64_t)(i * 2 + 1)]);
        if (hi < 0 || lo < 0) return false;
        out.value[i] = (BYTE)((hi << 4) | lo);
    }
    return true;
}

const char *haptic_location_name(const GUID &g) {
    return gameinput_internal::haptic_location_name(g.Data1, g.Data2, g.Data3, g.Data4);
}

// --- Force feedback parameter schema ---------------------------------------

constexpr int kEffectKindCount = 11;
constexpr uint64_t kForeverUsec = UINT64_MAX;

const GameInputForceFeedbackEffectKind kNativeEffectKinds[kEffectKindCount] = {
    GameInputForceFeedbackConstant,         GameInputForceFeedbackRamp,
    GameInputForceFeedbackSineWave,         GameInputForceFeedbackSquareWave,
    GameInputForceFeedbackTriangleWave,     GameInputForceFeedbackSawtoothUpWave,
    GameInputForceFeedbackSawtoothDownWave, GameInputForceFeedbackSpring,
    GameInputForceFeedbackFriction,         GameInputForceFeedbackDamper,
    GameInputForceFeedbackInertia,
};

int effect_kind_from_native(GameInputForceFeedbackEffectKind kind) {
    for (int i = 0; i < kEffectKindCount; ++i) {
        if (kNativeEffectKinds[i] == kind) return i;
    }
    return -1;
}

uint32_t effect_bits_from_motor(const GameInputForceFeedbackMotorInfo &m) {
    const bool supported[kEffectKindCount] = {
        m.isConstantEffectSupported,     m.isRampEffectSupported,
        m.isSineWaveEffectSupported,     m.isSquareWaveEffectSupported,
        m.isTriangleWaveEffectSupported, m.isSawtoothUpWaveEffectSupported,
        m.isSawtoothDownWaveEffectSupported, m.isSpringEffectSupported,
        m.isFrictionEffectSupported,     m.isDamperEffectSupported,
        m.isInertiaEffectSupported,
    };
    uint32_t bits = 0;
    for (int i = 0; i < kEffectKindCount; ++i) {
        if (supported[i]) bits |= 1u << i;
    }
    return bits;
}

bool effect_kind_is_periodic(int kind) { return kind >= 2 && kind <= 6; }
bool effect_kind_is_condition(int kind) { return kind >= 7 && kind <= 10; }
bool effect_kind_has_envelope(int kind) { return kind >= 0 && kind <= 6; }

GameInputForceFeedbackPeriodicParams *periodic_of(GameInputForceFeedbackParams &p) {
    switch (p.kind) {
        case GameInputForceFeedbackSineWave: return &p.data.sineWave;
        case GameInputForceFeedbackSquareWave: return &p.data.squareWave;
        case GameInputForceFeedbackTriangleWave: return &p.data.triangleWave;
        case GameInputForceFeedbackSawtoothUpWave: return &p.data.sawtoothUpWave;
        case GameInputForceFeedbackSawtoothDownWave: return &p.data.sawtoothDownWave;
        default: return nullptr;
    }
}

GameInputForceFeedbackConditionParams *condition_of(GameInputForceFeedbackParams &p) {
    switch (p.kind) {
        case GameInputForceFeedbackSpring: return &p.data.spring;
        case GameInputForceFeedbackFriction: return &p.data.friction;
        case GameInputForceFeedbackDamper: return &p.data.damper;
        case GameInputForceFeedbackInertia: return &p.data.inertia;
        default: return nullptr;
    }
}

GameInputForceFeedbackEnvelope *envelope_of(GameInputForceFeedbackParams &p) {
    switch (p.kind) {
        case GameInputForceFeedbackConstant: return &p.data.constant.envelope;
        case GameInputForceFeedbackRamp: return &p.data.ramp.envelope;
        default: {
            GameInputForceFeedbackPeriodicParams *periodic = periodic_of(p);
            return periodic ? &periodic->envelope : nullptr;
        }
    }
}

GameInputForceFeedbackParams default_ffb_params(int kind) {
    GameInputForceFeedbackParams p;
    std::memset(&p, 0, sizeof(p));
    p.kind = kNativeEffectKinds[kind];
    if (GameInputForceFeedbackEnvelope *env = envelope_of(p)) {
        env->sustainDuration = kForeverUsec;
        env->attackGain = 1.0f;
        env->sustainGain = 1.0f;
        env->releaseGain = 1.0f;
        env->playCount = 1;
    }
    if (GameInputForceFeedbackPeriodicParams *periodic = periodic_of(p)) {
        periodic->frequency = 1.0f;
    }
    if (GameInputForceFeedbackConditionParams *condition = condition_of(p)) {
        condition->positiveCoefficient = 1.0f;
        condition->negativeCoefficient = 1.0f;
        condition->maxPositiveMagnitude = 1.0f;
        condition->maxNegativeMagnitude = 1.0f;
    }
    return p;
}

float *magnitude_axis(GameInputForceFeedbackMagnitude &m, const String &name) {
    if (name == "linear_x") return &m.linearX;
    if (name == "linear_y") return &m.linearY;
    if (name == "linear_z") return &m.linearZ;
    if (name == "angular_x") return &m.angularX;
    if (name == "angular_y") return &m.angularY;
    if (name == "angular_z") return &m.angularZ;
    if (name == "normal") return &m.normal;
    return nullptr;
}

// Variant conversions silently coerce mismatched types (an int key becomes
// "0", an Array becomes an empty Dictionary), so script data is type-checked
// first and reported as an error instead of being guessed at.
bool variant_is_string(const Variant &v) {
    return v.get_type() == Variant::STRING || v.get_type() == Variant::STRING_NAME;
}

bool variant_is_number(const Variant &v) {
    return v.get_type() == Variant::INT || v.get_type() == Variant::FLOAT;
}

// Reads one parameter dictionary against the schema for one effect kind.
// Keys that are absent keep their current value; unknown keys, wrong types and
// non-finite numbers are errors so typos never silently do nothing.
class FfbParamReader {
public:
    FfbParamReader(const Dictionary &d, uint32_t motor_axes) : m_d(d), m_axes(motor_axes) {}

    bool ok() const { return m_error.is_empty(); }
    const String &error() const { return m_error; }

    void allow(const char *key) { m_allowed.push_back(String(key)); }

    void number(const char *key, float &inout, float lo, float hi) {
        double v = 0.0;
        if (!_fetch(key, v)) return;
        inout = (float)std::max((double)lo, std::min((double)hi, v));
    }

    void unbounded(const char *key, float &inout) {
        double v = 0.0;
        if (!_fetch(key, v)) return;
        inout = (float)v;
    }

    void non_negative(const char *key, float &inout) {
        double v = 0.0;
        if (!_fetch(key, v)) return;
        if (v < 0.0) {
            _fail(String("'") + key + "' must be >= 0");
            return;
        }
        inout = (float)v;
    }

    void seconds(const char *key, uint64_t &inout, bool negative_is_forever) {
        double v = 0.0;
        if (!_fetch(key, v)) return;
        if (v < 0.0) {
            if (negative_is_forever) {
                inout = kForeverUsec;
                return;
            }
            _fail(String("'") + key + "' must be >= 0 seconds");
            return;
        }
        double usec = v * 1000000.0;
        inout = usec >= 1.8e19 ? kForeverUsec : (uint64_t)std::llround(usec);
    }

    void count(const char *key, uint32_t &inout) {
        double v = 0.0;
        if (!_fetch(key, v)) return;
        if (v < 0.0 || v > 4294967295.0 || v != std::floor(v)) {
            _fail(String("'") + key + "' must be a whole number >= 0");
            return;
        }
        inout = (uint32_t)v;
    }

    void magnitude(const char *key, GameInputForceFeedbackMagnitude &inout) {
        allow(key);
        if (!ok() || !m_d.has(key)) return;
        Variant v = m_d[key];
        if (v.get_type() == Variant::INT || v.get_type() == Variant::FLOAT) {
            double x = (double)v;
            if (!std::isfinite(x)) {
                _fail(String("'") + key + "' must be finite");
                return;
            }
            float m = clamp_signed((float)x);
            GameInputForceFeedbackMagnitude out;
            std::memset(&out, 0, sizeof(out));
            uint32_t axes = m_axes ? m_axes : (uint32_t)GameInputFeedbackAxisLinearX;
            if (axes & GameInputFeedbackAxisLinearX) out.linearX = m;
            if (axes & GameInputFeedbackAxisLinearY) out.linearY = m;
            if (axes & GameInputFeedbackAxisLinearZ) out.linearZ = m;
            if (axes & GameInputFeedbackAxisAngularX) out.angularX = m;
            if (axes & GameInputFeedbackAxisAngularY) out.angularY = m;
            if (axes & GameInputFeedbackAxisAngularZ) out.angularZ = m;
            if (axes & GameInputFeedbackAxisNormal) out.normal = m;
            inout = out;
            return;
        }
        if (v.get_type() != Variant::DICTIONARY) {
            _fail(String("'") + key + "' must be a number or a Dictionary of axes");
            return;
        }
        Dictionary axes_dict = v;
        GameInputForceFeedbackMagnitude out = inout;
        Array keys = axes_dict.keys();
        for (int i = 0; i < keys.size(); ++i) {
            if (!variant_is_string(keys[i])) {
                _fail(String("axis names in '") + key + "' must be Strings");
                return;
            }
            String axis = keys[i];
            float *slot = magnitude_axis(out, axis);
            if (!slot) {
                _fail(String("unknown axis '") + axis + "' in '" + key + "'");
                return;
            }
            Variant av = axes_dict[keys[i]];
            if (av.get_type() != Variant::INT && av.get_type() != Variant::FLOAT) {
                _fail(String("axis '") + axis + "' in '" + key + "' must be a number");
                return;
            }
            double x = (double)av;
            if (!std::isfinite(x)) {
                _fail(String("axis '") + axis + "' in '" + key + "' must be finite");
                return;
            }
            *slot = clamp_signed((float)x);
        }
        inout = out;
    }

    void finish() {
        if (!ok()) return;
        Array keys = m_d.keys();
        for (int i = 0; i < keys.size(); ++i) {
            if (!variant_is_string(keys[i])) {
                _fail("parameter names must be Strings");
                return;
            }
            String k = keys[i];
            bool known = false;
            for (uint32_t j = 0; j < m_allowed.size(); ++j) {
                if (m_allowed[j] == k) {
                    known = true;
                    break;
                }
            }
            if (!known) {
                _fail(String("unknown key '") + k + "' for this effect kind");
                return;
            }
        }
    }

private:
    bool _fetch(const char *key, double &out) {
        allow(key);
        if (!ok() || !m_d.has(key)) return false;
        Variant v = m_d[key];
        if (v.get_type() != Variant::INT && v.get_type() != Variant::FLOAT) {
            _fail(String("'") + key + "' must be a number");
            return false;
        }
        out = (double)v;
        if (!std::isfinite(out)) {
            _fail(String("'") + key + "' must be finite");
            return false;
        }
        return true;
    }

    void _fail(const String &message) {
        if (m_error.is_empty()) m_error = message;
    }

    const Dictionary &m_d;
    uint32_t m_axes = 0;
    LocalVector<String> m_allowed;
    String m_error;
};

// Parses `d` into `out`. When `base` is non-null the call updates an existing
// effect: the kind is fixed and absent keys keep the values from `base`.
bool parse_ffb_params(const Dictionary &d, uint32_t motor_axes, int fixed_kind,
                      const GameInputForceFeedbackParams *base,
                      GameInputForceFeedbackParams &out, int &out_kind, String &err) {
    int kind = fixed_kind;
    if (d.has("kind")) {
        Variant kv = d["kind"];
        if (kv.get_type() != Variant::INT) {
            err = "'kind' must be a GameInputForceFeedbackEffect.EffectKind value";
            return false;
        }
        int requested = (int)(int64_t)kv;
        if (fixed_kind >= 0 && requested != fixed_kind) {
            err = "'kind' cannot change after the effect is created";
            return false;
        }
        kind = requested;
    }
    if (kind < 0) {
        err = "missing 'kind'";
        return false;
    }
    if (kind >= kEffectKindCount) {
        err = String("unsupported effect kind ") + String::num_int64(kind);
        return false;
    }

    out = base ? *base : default_ffb_params(kind);
    FfbParamReader r(d, motor_axes);
    r.allow("kind");

    if (GameInputForceFeedbackEnvelope *env = envelope_of(out)) {
        r.seconds("attack_duration", env->attackDuration, false);
        r.seconds("sustain_duration", env->sustainDuration, true);
        r.seconds("release_duration", env->releaseDuration, false);
        r.number("attack_gain", env->attackGain, 0.0f, 1.0f);
        r.number("sustain_gain", env->sustainGain, 0.0f, 1.0f);
        r.number("release_gain", env->releaseGain, 0.0f, 1.0f);
        r.count("play_count", env->playCount);
        r.seconds("repeat_delay", env->repeatDelay, false);
    }
    if (out.kind == GameInputForceFeedbackConstant) {
        r.magnitude("magnitude", out.data.constant.magnitude);
    } else if (out.kind == GameInputForceFeedbackRamp) {
        r.magnitude("start_magnitude", out.data.ramp.startMagnitude);
        r.magnitude("end_magnitude", out.data.ramp.endMagnitude);
    } else if (GameInputForceFeedbackPeriodicParams *periodic = periodic_of(out)) {
        r.magnitude("magnitude", periodic->magnitude);
        r.non_negative("frequency", periodic->frequency);
        r.unbounded("phase", periodic->phase);
        r.number("bias", periodic->bias, -1.0f, 1.0f);
    } else if (GameInputForceFeedbackConditionParams *condition = condition_of(out)) {
        r.magnitude("magnitude", condition->magnitude);
        r.number("positive_coefficient", condition->positiveCoefficient, -1.0f, 1.0f);
        r.number("negative_coefficient", condition->negativeCoefficient, -1.0f, 1.0f);
        r.number("max_positive_magnitude", condition->maxPositiveMagnitude, 0.0f, 1.0f);
        r.number("max_negative_magnitude", condition->maxNegativeMagnitude, 0.0f, 1.0f);
        r.number("dead_zone", condition->deadZone, 0.0f, 1.0f);
        r.number("bias", condition->bias, -1.0f, 1.0f);
    }
    r.finish();
    if (!r.ok()) {
        err = r.error();
        return false;
    }
    out_kind = kind;
    return true;
}

double usec_to_seconds(uint64_t usec, bool forever_is_negative) {
    if (forever_is_negative && usec == kForeverUsec) return -1.0;
    return (double)usec / 1000000.0;
}

Dictionary magnitude_to_dict(const GameInputForceFeedbackMagnitude &m) {
    Dictionary d;
    d["linear_x"] = m.linearX;
    d["linear_y"] = m.linearY;
    d["linear_z"] = m.linearZ;
    d["angular_x"] = m.angularX;
    d["angular_y"] = m.angularY;
    d["angular_z"] = m.angularZ;
    d["normal"] = m.normal;
    return d;
}

Dictionary serialize_ffb_params(const GameInputForceFeedbackParams &params) {
    GameInputForceFeedbackParams p = params;
    Dictionary d;
    int kind = effect_kind_from_native(p.kind);
    d["kind"] = kind;
    if (GameInputForceFeedbackEnvelope *env = envelope_of(p)) {
        d["attack_duration"] = usec_to_seconds(env->attackDuration, false);
        d["sustain_duration"] = usec_to_seconds(env->sustainDuration, true);
        d["release_duration"] = usec_to_seconds(env->releaseDuration, false);
        d["attack_gain"] = env->attackGain;
        d["sustain_gain"] = env->sustainGain;
        d["release_gain"] = env->releaseGain;
        d["play_count"] = (int64_t)env->playCount;
        d["repeat_delay"] = usec_to_seconds(env->repeatDelay, false);
    }
    if (p.kind == GameInputForceFeedbackConstant) {
        d["magnitude"] = magnitude_to_dict(p.data.constant.magnitude);
    } else if (p.kind == GameInputForceFeedbackRamp) {
        d["start_magnitude"] = magnitude_to_dict(p.data.ramp.startMagnitude);
        d["end_magnitude"] = magnitude_to_dict(p.data.ramp.endMagnitude);
    } else if (GameInputForceFeedbackPeriodicParams *periodic = periodic_of(p)) {
        d["magnitude"] = magnitude_to_dict(periodic->magnitude);
        d["frequency"] = periodic->frequency;
        d["phase"] = periodic->phase;
        d["bias"] = periodic->bias;
    } else if (GameInputForceFeedbackConditionParams *condition = condition_of(p)) {
        d["magnitude"] = magnitude_to_dict(condition->magnitude);
        d["positive_coefficient"] = condition->positiveCoefficient;
        d["negative_coefficient"] = condition->negativeCoefficient;
        d["max_positive_magnitude"] = condition->maxPositiveMagnitude;
        d["max_negative_magnitude"] = condition->maxNegativeMagnitude;
        d["dead_zone"] = condition->deadZone;
        d["bias"] = condition->bias;
    }
    return d;
}

Dictionary motor_info_to_dict(uint32_t axes, uint32_t effect_bits) {
    Dictionary d;
    d["supported_axes"] = (int64_t)axes;
    Array effects;
    for (int i = 0; i < kEffectKindCount; ++i) {
        if (effect_bits & (1u << i)) effects.push_back(i);
    }
    d["supported_effects"] = effects;
    return d;
}

// Source -> GameInputLabel lookup. Returns false when the source has no label
// field on this device.
bool label_for_source(const GameInputDeviceInfo *info, int source, int32_t &out) {
    using GD = GameInputDevice;
    if (!info) return false;
    if (source >= GD::SRC_BTN_MENU && source <= GD::SRC_BTN_Z) {
        const GameInputGamepadInfo *g = info->gamepadInfo;
        if (!g) return false;
        switch (source) {
            case GD::SRC_BTN_MENU: out = (int32_t)g->menuButtonLabel; return true;
            case GD::SRC_BTN_VIEW: out = (int32_t)g->viewButtonLabel; return true;
            case GD::SRC_BTN_A: out = (int32_t)g->aButtonLabel; return true;
            case GD::SRC_BTN_B: out = (int32_t)g->bButtonLabel; return true;
            case GD::SRC_BTN_X: out = (int32_t)g->xButtonLabel; return true;
            case GD::SRC_BTN_Y: out = (int32_t)g->yButtonLabel; return true;
            case GD::SRC_BTN_DPAD_UP: out = (int32_t)g->dpadUpLabel; return true;
            case GD::SRC_BTN_DPAD_DOWN: out = (int32_t)g->dpadDownLabel; return true;
            case GD::SRC_BTN_DPAD_LEFT: out = (int32_t)g->dpadLeftLabel; return true;
            case GD::SRC_BTN_DPAD_RIGHT: out = (int32_t)g->dpadRightLabel; return true;
            case GD::SRC_BTN_LEFT_SHOULDER: out = (int32_t)g->leftShoulderButtonLabel; return true;
            case GD::SRC_BTN_RIGHT_SHOULDER: out = (int32_t)g->rightShoulderButtonLabel; return true;
            case GD::SRC_BTN_LEFT_THUMB: out = (int32_t)g->leftThumbstickButtonLabel; return true;
            case GD::SRC_BTN_RIGHT_THUMB: out = (int32_t)g->rightThumbstickButtonLabel; return true;
            case GD::SRC_BTN_C: out = (int32_t)g->cButtonLabel; return true;
            case GD::SRC_BTN_Z: out = (int32_t)g->zButtonLabel; return true;
            default: return false;
        }
    }
    if (source >= GD::SRC_ARCADE_MENU && source <= GD::SRC_ARCADE_SPECIAL_2) {
        const GameInputArcadeStickInfo *a = info->arcadeStickInfo;
        if (!a) return false;
        const GameInputLabel labels[] = {
            a->menuButtonLabel,    a->viewButtonLabel,    a->stickUpLabel,
            a->stickDownLabel,     a->stickLeftLabel,     a->stickRightLabel,
            a->actionButton1Label, a->actionButton2Label, a->actionButton3Label,
            a->actionButton4Label, a->actionButton5Label, a->actionButton6Label,
            a->specialButton1Label, a->specialButton2Label,
        };
        out = (int32_t)labels[source - GD::SRC_ARCADE_MENU];
        return true;
    }
    if (source >= GD::SRC_FLIGHT_MENU && source <= GD::SRC_FLIGHT_RIGHT_SHOULDER) {
        const GameInputFlightStickInfo *f = info->flightStickInfo;
        if (!f) return false;
        const GameInputLabel labels[] = {
            f->menuButtonLabel,          f->viewButtonLabel,
            f->firePrimaryButtonLabel,   f->fireSecondaryButtonLabel,
            f->hatSwitchUpLabel,         f->hatSwitchDownLabel,
            f->hatSwitchLeftLabel,       f->hatSwitchRightLabel,
            f->aButtonLabel,             f->bButtonLabel,
            f->xButtonLabel,             f->yButtonLabel,
            f->leftShoulderButtonLabel,  f->rightShoulderButtonLabel,
        };
        out = (int32_t)labels[source - GD::SRC_FLIGHT_MENU];
        return true;
    }
    if (source >= GD::SRC_WHEEL_MENU && source <= GD::SRC_WHEEL_RIGHT_THUMB) {
        const GameInputRacingWheelInfo *w = info->racingWheelInfo;
        if (!w) return false;
        const GameInputLabel labels[] = {
            w->menuButtonLabel,           w->viewButtonLabel,
            w->previousGearButtonLabel,   w->nextGearButtonLabel,
            w->dpadUpLabel,               w->dpadDownLabel,
            w->dpadLeftLabel,             w->dpadRightLabel,
            w->aButtonLabel,              w->bButtonLabel,
            w->xButtonLabel,              w->yButtonLabel,
            w->leftThumbstickButtonLabel, w->rightThumbstickButtonLabel,
        };
        out = (int32_t)labels[source - GD::SRC_WHEEL_MENU];
        return true;
    }
    return false;
}

// --- Mock reading schema (debug seams) ---------------------------------------

float dict_float(const Dictionary &d, const char *key, float fallback) {
    if (!d.has(key)) return fallback;
    Variant v = d[key];
    if (!variant_is_number(v)) return fallback;
    double x = (double)v;
    return std::isfinite(x) ? (float)x : fallback;
}

int64_t dict_int(const Dictionary &d, const char *key, int64_t fallback) {
    if (!d.has(key)) return fallback;
    Variant v = d[key];
    if (v.get_type() == Variant::INT) return (int64_t)v;
    if (v.get_type() == Variant::FLOAT) return (int64_t)(double)v;
    if (v.get_type() == Variant::BOOL) return (bool)v ? 1 : 0;
    return fallback;
}

Array variant_to_array(const Variant &v) {
    switch (v.get_type()) {
        case Variant::ARRAY: {
            Array a = v;
            return a;
        }
        case Variant::PACKED_FLOAT32_ARRAY: {
            PackedFloat32Array p = v;
            return Array(p);
        }
        case Variant::PACKED_FLOAT64_ARRAY: {
            PackedFloat64Array p = v;
            return Array(p);
        }
        case Variant::PACKED_INT32_ARRAY: {
            PackedInt32Array p = v;
            return Array(p);
        }
        case Variant::PACKED_INT64_ARRAY: {
            PackedInt64Array p = v;
            return Array(p);
        }
        case Variant::PACKED_BYTE_ARRAY: {
            PackedByteArray p = v;
            return Array(p);
        }
        case Variant::PACKED_STRING_ARRAY: {
            PackedStringArray p = v;
            return Array(p);
        }
        default: return Array();
    }
}

bool dict_vector3(const Dictionary &d, const char *key, Vector3 &out, String &err) {
    if (!d.has(key)) return true;
    Variant v = d[key];
    if (v.get_type() != Variant::VECTOR3) {
        err = String("'") + key + "' must be a Vector3";
        return false;
    }
    out = v;
    return true;
}

// Converts a mock state dictionary (native GameInput conventions: stick up is
// positive, mouse positions accumulate) into snapshot data. Sections for kinds
// the device does not support are ignored, like a real device would never
// report them.
bool mock_snapshot_from_dict(const Dictionary &state, uint32_t supported, uint64_t ts,
                             gi::Snapshot &out, String &err) {
    static const char *kSections[] = {
        "timestamp", "gamepad", "keyboard", "mouse", "sensors", "arcade_stick",
        "flight_stick", "racing_wheel", "controller",
    };
    Array keys = state.keys();
    for (int i = 0; i < keys.size(); ++i) {
        if (!variant_is_string(keys[i])) {
            err = "mock reading section names must be Strings";
            return false;
        }
        String k = keys[i];
        bool known = false;
        for (const char *s : kSections) {
            if (k == s) {
                known = true;
                break;
            }
        }
        if (!known) {
            err = String("unknown mock reading section '") + k + "'";
            return false;
        }
    }

    auto section = [&](const char *name, uint32_t kind, Dictionary &sec) -> bool {
        if (!state.has(name)) return false;
        Variant v = state[name];
        if (v.get_type() != Variant::DICTIONARY) {
            err = String("'") + name + "' must be a Dictionary";
            return false;
        }
        if (!(supported & kind)) return false;
        sec = v;
        return true;
    };

    Dictionary sec;
    if (section("gamepad", gi::SNAP_GAMEPAD, sec)) {
        out.gamepad.buttons =
                (uint32_t)dict_int(sec, "buttons", 0) & GameInputDevice::kGamepadButtonMask;
        out.gamepad.left_x = clamp_signed(dict_float(sec, "left_x", 0.0f));
        out.gamepad.left_y = clamp_signed(dict_float(sec, "left_y", 0.0f));
        out.gamepad.right_x = clamp_signed(dict_float(sec, "right_x", 0.0f));
        out.gamepad.right_y = clamp_signed(dict_float(sec, "right_y", 0.0f));
        out.gamepad.left_trigger = clamp01(dict_float(sec, "left_trigger", 0.0f));
        out.gamepad.right_trigger = clamp01(dict_float(sec, "right_trigger", 0.0f));
        gi::mark_kind(out, gi::SNAP_GAMEPAD, ts);
    }
    if (section("keyboard", gi::SNAP_KEYBOARD, sec)) {
        Array list = variant_to_array(sec.get("keys", Array()));
        out.key_count_native = (uint32_t)list.size();
        out.key_count = std::min<uint32_t>((uint32_t)list.size(), gi::kMaxKeys);
        for (uint32_t i = 0; i < out.key_count; ++i) {
            gi::KeyData key;
            Variant entry = list[(int64_t)i];
            if (entry.get_type() == Variant::DICTIONARY) {
                Dictionary kd = entry;
                key.scan_code = (uint32_t)dict_int(kd, "scan_code", 0);
                key.code_point = (uint32_t)dict_int(kd, "code_point", 0);
                key.virtual_key = (uint8_t)dict_int(kd, "virtual_key", 0);
                key.is_dead_key = dict_int(kd, "is_dead_key", 0) != 0;
            } else if (entry.get_type() == Variant::INT) {
                key.scan_code = (uint32_t)(int64_t)entry;
            } else {
                err = "keyboard 'keys' entries must be scan-code ints or Dictionaries";
                return false;
            }
            out.keys[i] = key;
        }
        gi::mark_kind(out, gi::SNAP_KEYBOARD, ts);
    }
    if (section("mouse", gi::SNAP_MOUSE, sec)) {
        out.mouse.buttons = (uint32_t)dict_int(sec, "buttons", 0) & 0x7Fu;
        out.mouse.x = dict_int(sec, "x", 0);
        out.mouse.y = dict_int(sec, "y", 0);
        out.mouse.wheel_x = dict_int(sec, "wheel_x", 0);
        out.mouse.wheel_y = dict_int(sec, "wheel_y", 0);
        out.mouse.absolute_x = dict_int(sec, "absolute_x", 0);
        out.mouse.absolute_y = dict_int(sec, "absolute_y", 0);
        out.mouse.positions = (uint32_t)dict_int(
                sec, "positions", (int64_t)GameInputMouseRelativePosition);
        gi::mark_kind(out, gi::SNAP_MOUSE, ts);
    }
    if (section("sensors", gi::SNAP_SENSORS, sec)) {
        Vector3 accel;
        Vector3 gyro;
        Quaternion q;
        if (!dict_vector3(sec, "acceleration_g", accel, err) ||
                !dict_vector3(sec, "angular_velocity", gyro, err)) {
            return false;
        }
        if (sec.has("orientation")) {
            Variant qv = sec["orientation"];
            if (qv.get_type() != Variant::QUATERNION) {
                err = "'orientation' must be a Quaternion";
                return false;
            }
            q = qv;
        }
        out.sensors.acceleration_g[0] = (float)accel.x;
        out.sensors.acceleration_g[1] = (float)accel.y;
        out.sensors.acceleration_g[2] = (float)accel.z;
        out.sensors.angular_velocity_rad[0] = (float)gyro.x;
        out.sensors.angular_velocity_rad[1] = (float)gyro.y;
        out.sensors.angular_velocity_rad[2] = (float)gyro.z;
        out.sensors.heading_degrees = dict_float(sec, "heading", 0.0f);
        out.sensors.heading_accuracy = (int32_t)dict_int(sec, "heading_accuracy", 0);
        out.sensors.orientation_wxyz[0] = (float)q.w;
        out.sensors.orientation_wxyz[1] = (float)q.x;
        out.sensors.orientation_wxyz[2] = (float)q.y;
        out.sensors.orientation_wxyz[3] = (float)q.z;
        gi::mark_kind(out, gi::SNAP_SENSORS, ts);
    }
    if (section("arcade_stick", gi::SNAP_ARCADE_STICK, sec)) {
        out.arcade_stick.buttons = (uint32_t)dict_int(sec, "buttons", 0) & 0x3FFFu;
        gi::mark_kind(out, gi::SNAP_ARCADE_STICK, ts);
    }
    if (section("flight_stick", gi::SNAP_FLIGHT_STICK, sec)) {
        out.flight_stick.buttons = (uint32_t)dict_int(sec, "buttons", 0) & 0x3FFFu;
        out.flight_stick.hat = (int32_t)dict_int(sec, "hat", 0);
        out.flight_stick.roll = clamp_signed(dict_float(sec, "roll", 0.0f));
        out.flight_stick.pitch = clamp_signed(dict_float(sec, "pitch", 0.0f));
        out.flight_stick.yaw = clamp_signed(dict_float(sec, "yaw", 0.0f));
        out.flight_stick.throttle = clamp01(dict_float(sec, "throttle", 0.0f));
        gi::mark_kind(out, gi::SNAP_FLIGHT_STICK, ts);
    }
    if (section("racing_wheel", gi::SNAP_RACING_WHEEL, sec)) {
        out.racing_wheel.buttons = (uint32_t)dict_int(sec, "buttons", 0) & 0x3FFFu;
        out.racing_wheel.gear = (int32_t)dict_int(sec, "gear", 0);
        out.racing_wheel.wheel = clamp_signed(dict_float(sec, "wheel", 0.0f));
        out.racing_wheel.throttle = clamp01(dict_float(sec, "throttle", 0.0f));
        out.racing_wheel.brake = clamp01(dict_float(sec, "brake", 0.0f));
        out.racing_wheel.clutch = clamp01(dict_float(sec, "clutch", 0.0f));
        out.racing_wheel.handbrake = clamp01(dict_float(sec, "handbrake", 0.0f));
        gi::mark_kind(out, gi::SNAP_RACING_WHEEL, ts);
    }
    if (state.has("controller")) {
        Variant v = state["controller"];
        if (v.get_type() != Variant::DICTIONARY) {
            err = "'controller' must be a Dictionary";
            return false;
        }
        Dictionary c = v;
        if (c.has("axes") && (supported & gi::SNAP_CONTROLLER_AXIS)) {
            Array axes = variant_to_array(c["axes"]);
            out.axis_count_native = (uint32_t)axes.size();
            out.axis_count = std::min<uint32_t>((uint32_t)axes.size(), gi::kMaxControllerAxes);
            for (uint32_t i = 0; i < out.axis_count; ++i) {
                Variant av = axes[(int64_t)i];
                out.axes[i] = variant_is_number(av) ? (float)(double)av : 0.0f;
            }
            gi::mark_kind(out, gi::SNAP_CONTROLLER_AXIS, ts);
        }
        if (c.has("buttons") && (supported & gi::SNAP_CONTROLLER_BUTTON)) {
            Array buttons = variant_to_array(c["buttons"]);
            out.button_count_native = (uint32_t)buttons.size();
            out.button_count =
                    std::min<uint32_t>((uint32_t)buttons.size(), gi::kMaxControllerButtons);
            for (uint32_t i = 0; i < out.button_count; ++i) {
                Variant bv = buttons[(int64_t)i];
                bool down = bv.get_type() == Variant::BOOL ? (bool)bv
                                                           : variant_is_number(bv) && (double)bv != 0.0;
                gi::set_controller_button(out, i, down);
            }
            gi::mark_kind(out, gi::SNAP_CONTROLLER_BUTTON, ts);
        }
        if (c.has("switches") && (supported & gi::SNAP_CONTROLLER_SWITCH)) {
            Array switches = variant_to_array(c["switches"]);
            out.switch_count_native = (uint32_t)switches.size();
            out.switch_count =
                    std::min<uint32_t>((uint32_t)switches.size(), gi::kMaxControllerSwitches);
            for (uint32_t i = 0; i < out.switch_count; ++i) {
                Variant sv = switches[(int64_t)i];
                out.switches[i] = variant_is_number(sv) ? (int32_t)(int64_t)sv : 0;
            }
            gi::mark_kind(out, gi::SNAP_CONTROLLER_SWITCH, ts);
        }
    }
    return err.is_empty();
}

} // namespace

GameInput *GameInput::singleton = nullptr;

GameInput *GameInput::get_singleton() {
    return singleton;
}

GameInput::GameInput() {
    ERR_FAIL_COND(singleton != nullptr);
    singleton = this;
}

GameInput::~GameInput() {
    if (m_initialized || m_game_input || m_backend != Backend::None) {
        shutdown();
    }
    if (singleton == this) {
        singleton = nullptr;
    }
}

void GameInput::_bind_methods() {
    ClassDB::bind_method(D_METHOD("initialize"), &GameInput::initialize);
    ClassDB::bind_method(D_METHOD("shutdown"), &GameInput::shutdown);
    ClassDB::bind_method(D_METHOD("is_initialized"), &GameInput::is_initialized);
    ClassDB::bind_method(D_METHOD("poll"), &GameInput::poll);
    ClassDB::bind_method(D_METHOD("get_devices", "kind_mask"),
                         &GameInput::get_devices, DEFVAL((int)DEVICE_GAMEPAD));
    ClassDB::bind_method(D_METHOD("get_primary_device", "kind_mask"),
                         &GameInput::get_primary_device, DEFVAL((int)DEVICE_GAMEPAD));
    ClassDB::bind_method(D_METHOD("get_device_by_id", "device_id"),
                         &GameInput::get_device_by_id);
    ClassDB::bind_method(D_METHOD("get_current_reading", "device"),
                         &GameInput::get_current_reading);
    ClassDB::bind_method(D_METHOD("set_vibration", "device", "low_freq", "high_freq",
                                  "left_trigger", "right_trigger"),
                         &GameInput::set_vibration, DEFVAL(0.0f), DEFVAL(0.0f));
    ClassDB::bind_method(D_METHOD("stop_haptics", "device"), &GameInput::stop_haptics);
    ClassDB::bind_method(D_METHOD("get_connected_device_count", "kind_mask"),
                         &GameInput::get_connected_device_count, DEFVAL((int)DEVICE_ALL));
    ClassDB::bind_method(D_METHOD("get_current_timestamp"), &GameInput::get_current_timestamp);
    ClassDB::bind_method(D_METHOD("set_reading_callback_kinds", "kind_mask"),
                         &GameInput::set_reading_callback_kinds);
    ClassDB::bind_method(D_METHOD("get_reading_callback_kinds"),
                         &GameInput::get_reading_callback_kinds);
    ClassDB::bind_method(D_METHOD("get_buffered_readings", "device"),
                         &GameInput::get_buffered_readings);
    ClassDB::bind_method(D_METHOD("get_dropped_reading_count"),
                         &GameInput::get_dropped_reading_count);
    ClassDB::bind_method(D_METHOD("set_focus_policy", "policy"), &GameInput::set_focus_policy);
    ClassDB::bind_method(D_METHOD("get_focus_policy"), &GameInput::get_focus_policy);
    ClassDB::bind_method(D_METHOD("create_aggregate_device", "kind"),
                         &GameInput::create_aggregate_device);
    ClassDB::bind_method(D_METHOD("disable_aggregate_device", "app_local_id"),
                         &GameInput::disable_aggregate_device);

    ADD_SIGNAL(MethodInfo("device_connected",
                          PropertyInfo(Variant::OBJECT, "device", PROPERTY_HINT_RESOURCE_TYPE,
                                       "GameInputDevice")));
    ADD_SIGNAL(MethodInfo("device_disconnected",
                          PropertyInfo(Variant::INT, "device_id")));
    ADD_SIGNAL(MethodInfo("device_status_changed",
                          PropertyInfo(Variant::OBJECT, "device", PROPERTY_HINT_RESOURCE_TYPE,
                                       "GameInputDevice"),
                          PropertyInfo(Variant::INT, "status"),
                          PropertyInfo(Variant::INT, "previous_status"),
                          PropertyInfo(Variant::INT, "timestamp")));
    ADD_SIGNAL(MethodInfo("reading_received",
                          PropertyInfo(Variant::OBJECT, "device", PROPERTY_HINT_RESOURCE_TYPE,
                                       "GameInputDevice"),
                          PropertyInfo(Variant::OBJECT, "reading", PROPERTY_HINT_RESOURCE_TYPE,
                                       "GameInputReading")));
    ADD_SIGNAL(MethodInfo("system_buttons_changed",
                          PropertyInfo(Variant::OBJECT, "device", PROPERTY_HINT_RESOURCE_TYPE,
                                       "GameInputDevice"),
                          PropertyInfo(Variant::INT, "buttons"),
                          PropertyInfo(Variant::INT, "previous_buttons"),
                          PropertyInfo(Variant::INT, "timestamp")));
    ADD_SIGNAL(MethodInfo("keyboard_layout_changed",
                          PropertyInfo(Variant::OBJECT, "device", PROPERTY_HINT_RESOURCE_TYPE,
                                       "GameInputDevice"),
                          PropertyInfo(Variant::INT, "layout"),
                          PropertyInfo(Variant::INT, "previous_layout"),
                          PropertyInfo(Variant::INT, "timestamp")));

    BIND_ENUM_CONSTANT(DEVICE_UNKNOWN);
    BIND_ENUM_CONSTANT(DEVICE_GAMEPAD);
    BIND_ENUM_CONSTANT(DEVICE_KEYBOARD);
    BIND_ENUM_CONSTANT(DEVICE_MOUSE);
    BIND_ENUM_CONSTANT(DEVICE_ALL);
    BIND_ENUM_CONSTANT(DEVICE_ARCADE_STICK);
    BIND_ENUM_CONSTANT(DEVICE_FLIGHT_STICK);
    BIND_ENUM_CONSTANT(DEVICE_RACING_WHEEL);
    BIND_ENUM_CONSTANT(DEVICE_SENSORS);
    BIND_ENUM_CONSTANT(DEVICE_CONTROLLER);
    BIND_ENUM_CONSTANT(DEVICE_ANY);

    BIND_ENUM_CONSTANT(FOCUS_POLICY_DEFAULT);
    BIND_ENUM_CONSTANT(FOCUS_POLICY_EXCLUSIVE_FOREGROUND_INPUT);
    BIND_ENUM_CONSTANT(FOCUS_POLICY_EXCLUSIVE_FOREGROUND_GUIDE_BUTTON);
    BIND_ENUM_CONSTANT(FOCUS_POLICY_EXCLUSIVE_FOREGROUND_SHARE_BUTTON);
    BIND_ENUM_CONSTANT(FOCUS_POLICY_ENABLE_BACKGROUND_INPUT);
    BIND_ENUM_CONSTANT(FOCUS_POLICY_ENABLE_BACKGROUND_GUIDE_BUTTON);
    BIND_ENUM_CONSTANT(FOCUS_POLICY_ENABLE_BACKGROUND_SHARE_BUTTON);

#ifndef NDEBUG
    ClassDB::bind_method(D_METHOD("_test_initialize_mock"), &GameInput::_test_initialize_mock);
    ClassDB::bind_method(D_METHOD("_test_get_backend"), &GameInput::_test_get_backend);
    ClassDB::bind_method(D_METHOD("_test_inject_device", "info"), &GameInput::_test_inject_device);
    ClassDB::bind_method(D_METHOD("_test_remove_device", "device_id"),
                         &GameInput::_test_remove_device);
    ClassDB::bind_method(D_METHOD("_test_set_device_status", "device_id", "status"),
                         &GameInput::_test_set_device_status);
    ClassDB::bind_method(D_METHOD("_test_push_reading", "device_id", "state"),
                         &GameInput::_test_push_reading);
    ClassDB::bind_method(D_METHOD("_test_push_system_buttons", "device_id", "buttons"),
                         &GameInput::_test_push_system_buttons);
    ClassDB::bind_method(D_METHOD("_test_push_keyboard_layout", "device_id", "layout"),
                         &GameInput::_test_push_keyboard_layout);
    ClassDB::bind_method(D_METHOD("_test_get_last_rumble", "device_id"),
                         &GameInput::_test_get_last_rumble);
    ClassDB::bind_method(D_METHOD("_test_set_time_override_usec", "usec"),
                         &GameInput::_test_set_time_override_usec);
    ClassDB::bind_method(D_METHOD("_test_get_effect_count"), &GameInput::_test_get_effect_count);
    ClassDB::bind_method(D_METHOD("_test_force_poll"), &GameInput::_test_force_poll);
#endif
}

// --- Callback fence ----------------------------------------------------------

// Holds an in-flight reference for the duration of a callback so shutdown()
// can wait until the callback is out of the singleton's data before releasing
// IGameInput or freeing the singleton. A successful UnregisterCallback already
// makes that safe; the count keeps it safe when UnregisterCallback fails.
bool GameInput::_enter_callback() {
    m_callbacks_in_flight.fetch_add(1, std::memory_order_acquire);
    if (!m_accepting_callbacks.load(std::memory_order_acquire) ||
            m_shutting_down.load(std::memory_order_acquire)) {
        m_callbacks_in_flight.fetch_sub(1, std::memory_order_release);
        return false;
    }
    return true;
}

void GameInput::_leave_callback() {
    m_callbacks_in_flight.fetch_sub(1, std::memory_order_release);
}

void GameInput::_wait_for_callbacks() {
    // After UnregisterCallback returns no NEW callback starts, so this spin
    // is bounded by the longest in-flight callback body.
    while (m_callbacks_in_flight.load(std::memory_order_acquire) != 0) {
        std::this_thread::yield();
    }
}

void GameInput::_unregister_callback(GameInputCallbackToken &token, const char *what) {
    if (!token) return;
    // UnregisterCallback alone: calling StopCallback first makes it fail
    // intermittently on the GameInput 3.3 runtime, and a failed unregister
    // does not fence the callback.
    if (m_game_input && !m_game_input->UnregisterCallback(token)) {
        UtilityFunctions::push_warning("GameInput: UnregisterCallback() failed for the ", what,
                                       " callback; late callbacks will be ignored.");
    }
    token = 0;
}

bool GameInput::_queue_event(PendingEvent &ev) {
    std::lock_guard<std::mutex> lock(m_event_mutex);
    if (!m_accepting_callbacks.load(std::memory_order_acquire)) {
        return false;
    }
    ev.seq = m_next_event_seq++;
    m_pending_events.push_back(ev);
    return true;
}

bool GameInput::_queue_reading(ReadingEvent &ev) {
    ReadingEvent evicted;
    bool did_evict = false;
    {
        std::lock_guard<std::mutex> lock(m_event_mutex);
        if (!m_accepting_callbacks.load(std::memory_order_acquire) ||
                !m_accepting_readings.load(std::memory_order_acquire) || !m_reading_ring) {
            return false;
        }
        ev.seq = m_next_event_seq++;
        did_evict = m_reading_ring->push(ev, &evicted);
        if (did_evict) {
            m_reading_ring_overflowed = true;
        }
    }
    if (did_evict) {
        if (evicted.native_device) {
            evicted.native_device->Release();
        }
        m_dropped_reading_count.fetch_add(1, std::memory_order_relaxed);
    }
    return true;
}

void CALLBACK GameInput::_on_device_callback(
        GameInputCallbackToken /*token*/, void *context,
        IGameInputDevice *device, uint64_t timestamp,
        GameInputDeviceStatus current_status,
        GameInputDeviceStatus previous_status) {
    auto *self = static_cast<GameInput *>(context);
    if (!self || !device) {
        return;
    }
    if (!self->_enter_callback()) {
        return;
    }
    if (current_status != previous_status) {
        PendingEvent ev;
        ev.kind = PendingEventKind::DeviceStatus;
        ev.native_device = device;
        ev.timestamp = timestamp;
        ev.current = (uint32_t)current_status;
        ev.previous = (uint32_t)previous_status;
        device->AddRef();
        if (!self->_queue_event(ev)) {
            device->Release();
        }
    }
    self->_leave_callback();
}

void CALLBACK GameInput::_on_reading_callback(GameInputCallbackToken token, void *context,
                                              IGameInputReading *reading) {
    auto *self = static_cast<GameInput *>(context);
    if (!self || !reading) {
        return;
    }
    if (!self->_enter_callback()) {
        return;
    }
    const uint64_t active = self->m_active_reading_token.load(std::memory_order_acquire);
    if (self->m_accepting_readings.load(std::memory_order_acquire) &&
            (active == 0 || active == (uint64_t)token)) {
        IGameInputDevice *device = nullptr;
        reading->GetDevice(&device); // AddRef'd by GameInput
        if (device) {
            ReadingEvent ev;
            ev.native_device = device;
            _fill_snapshot_from_reading(reading, self->m_reading_snap_kinds, ev.snapshot);
            if (ev.snapshot.kinds == 0 || !self->_queue_reading(ev)) {
                device->Release();
            }
        }
    }
    self->_leave_callback();
}

void CALLBACK GameInput::_on_system_button_callback(GameInputCallbackToken /*token*/,
                                                    void *context, IGameInputDevice *device,
                                                    uint64_t timestamp,
                                                    GameInputSystemButtons current_buttons,
                                                    GameInputSystemButtons previous_buttons) {
    auto *self = static_cast<GameInput *>(context);
    if (!self || !device) {
        return;
    }
    if (!self->_enter_callback()) {
        return;
    }
    PendingEvent ev;
    ev.kind = PendingEventKind::SystemButtons;
    ev.native_device = device;
    ev.timestamp = timestamp;
    ev.current = (uint32_t)current_buttons;
    ev.previous = (uint32_t)previous_buttons;
    device->AddRef();
    if (!self->_queue_event(ev)) {
        device->Release();
    }
    self->_leave_callback();
}

void CALLBACK GameInput::_on_keyboard_layout_callback(GameInputCallbackToken /*token*/,
                                                      void *context, IGameInputDevice *device,
                                                      uint64_t timestamp, uint32_t current_layout,
                                                      uint32_t previous_layout) {
    auto *self = static_cast<GameInput *>(context);
    if (!self || !device) {
        return;
    }
    if (!self->_enter_callback()) {
        return;
    }
    PendingEvent ev;
    ev.kind = PendingEventKind::KeyboardLayout;
    ev.native_device = device;
    ev.timestamp = timestamp;
    ev.current = current_layout;
    ev.previous = previous_layout;
    device->AddRef();
    if (!self->_queue_event(ev)) {
        device->Release();
    }
    self->_leave_callback();
}

// --- Reading conversion (worker thread safe: touches only its arguments) -----

void GameInput::_fill_snapshot_from_reading(IGameInputReading *reading, uint32_t wanted,
                                            gi::Snapshot &out) {
    if (!reading) return;
    const uint32_t present = snap_kinds_from_native((uint32_t)reading->GetInputKind()) & wanted;
    if (!present) return;
    const uint64_t ts = reading->GetTimestamp();

    if (present & gi::SNAP_GAMEPAD) {
        GameInputGamepadState s{};
        if (reading->GetGamepadState(&s)) {
            out.gamepad.buttons = (uint32_t)s.buttons & GameInputDevice::kGamepadButtonMask;
            out.gamepad.left_trigger = s.leftTrigger;
            out.gamepad.right_trigger = s.rightTrigger;
            out.gamepad.left_x = s.leftThumbstickX;
            out.gamepad.left_y = s.leftThumbstickY;
            out.gamepad.right_x = s.rightThumbstickX;
            out.gamepad.right_y = s.rightThumbstickY;
            gi::mark_kind(out, gi::SNAP_GAMEPAD, ts);
        }
    }
    if (present & gi::SNAP_KEYBOARD) {
        GameInputKeyState keys[gi::kMaxKeys];
        const uint32_t native_count = reading->GetKeyCount();
        const uint32_t written = reading->GetKeyState(gi::kMaxKeys, keys);
        out.key_count_native = std::max(native_count, written);
        out.key_count = std::min<uint32_t>(written, gi::kMaxKeys);
        for (uint32_t i = 0; i < out.key_count; ++i) {
            out.keys[i].scan_code = keys[i].scanCode;
            out.keys[i].code_point = keys[i].codePoint;
            out.keys[i].virtual_key = keys[i].virtualKey;
            out.keys[i].is_dead_key = keys[i].isDeadKey;
        }
        gi::mark_kind(out, gi::SNAP_KEYBOARD, ts);
    }
    if (present & gi::SNAP_MOUSE) {
        GameInputMouseState m{};
        if (reading->GetMouseState(&m)) {
            out.mouse.buttons = (uint32_t)m.buttons;
            out.mouse.positions = (uint32_t)m.positions;
            out.mouse.x = m.positionX;
            out.mouse.y = m.positionY;
            out.mouse.absolute_x = m.absolutePositionX;
            out.mouse.absolute_y = m.absolutePositionY;
            out.mouse.wheel_x = m.wheelX;
            out.mouse.wheel_y = m.wheelY;
            gi::mark_kind(out, gi::SNAP_MOUSE, ts);
        }
    }
    if (present & gi::SNAP_SENSORS) {
        GameInputSensorsState s{};
        if (reading->GetSensorsState(&s)) {
            out.sensors.acceleration_g[0] = s.accelerationInGX;
            out.sensors.acceleration_g[1] = s.accelerationInGY;
            out.sensors.acceleration_g[2] = s.accelerationInGZ;
            out.sensors.angular_velocity_rad[0] = s.angularVelocityInRadPerSecX;
            out.sensors.angular_velocity_rad[1] = s.angularVelocityInRadPerSecY;
            out.sensors.angular_velocity_rad[2] = s.angularVelocityInRadPerSecZ;
            out.sensors.heading_degrees = s.headingInDegreesFromMagneticNorth;
            out.sensors.heading_accuracy = (int32_t)s.headingAccuracy;
            out.sensors.orientation_wxyz[0] = s.orientationW;
            out.sensors.orientation_wxyz[1] = s.orientationX;
            out.sensors.orientation_wxyz[2] = s.orientationY;
            out.sensors.orientation_wxyz[3] = s.orientationZ;
            gi::mark_kind(out, gi::SNAP_SENSORS, ts);
        }
    }
    if (present & gi::SNAP_ARCADE_STICK) {
        GameInputArcadeStickState s{};
        if (reading->GetArcadeStickState(&s)) {
            out.arcade_stick.buttons = (uint32_t)s.buttons;
            gi::mark_kind(out, gi::SNAP_ARCADE_STICK, ts);
        }
    }
    if (present & gi::SNAP_FLIGHT_STICK) {
        GameInputFlightStickState s{};
        if (reading->GetFlightStickState(&s)) {
            out.flight_stick.buttons = (uint32_t)s.buttons;
            out.flight_stick.hat = (int32_t)s.hatSwitch;
            out.flight_stick.roll = s.roll;
            out.flight_stick.pitch = s.pitch;
            out.flight_stick.yaw = s.yaw;
            out.flight_stick.throttle = s.throttle;
            gi::mark_kind(out, gi::SNAP_FLIGHT_STICK, ts);
        }
    }
    if (present & gi::SNAP_RACING_WHEEL) {
        GameInputRacingWheelState s{};
        if (reading->GetRacingWheelState(&s)) {
            out.racing_wheel.buttons = (uint32_t)s.buttons;
            out.racing_wheel.gear = s.patternShifterGear;
            out.racing_wheel.wheel = s.wheel;
            out.racing_wheel.throttle = s.throttle;
            out.racing_wheel.brake = s.brake;
            out.racing_wheel.clutch = s.clutch;
            out.racing_wheel.handbrake = s.handbrake;
            gi::mark_kind(out, gi::SNAP_RACING_WHEEL, ts);
        }
    }
    if (present & gi::SNAP_CONTROLLER_AXIS) {
        const uint32_t native_count = reading->GetControllerAxisCount();
        const uint32_t written = reading->GetControllerAxisState(gi::kMaxControllerAxes, out.axes);
        out.axis_count_native = std::max(native_count, written);
        out.axis_count = std::min<uint32_t>(written, gi::kMaxControllerAxes);
        gi::mark_kind(out, gi::SNAP_CONTROLLER_AXIS, ts);
    }
    if (present & gi::SNAP_CONTROLLER_BUTTON) {
        bool buttons[gi::kMaxControllerButtons] = {};
        const uint32_t native_count = reading->GetControllerButtonCount();
        const uint32_t written =
                reading->GetControllerButtonState(gi::kMaxControllerButtons, buttons);
        out.button_count_native = std::max(native_count, written);
        out.button_count = std::min<uint32_t>(written, gi::kMaxControllerButtons);
        std::memset(out.button_bits, 0, sizeof(out.button_bits));
        for (uint32_t i = 0; i < out.button_count; ++i) {
            gi::set_controller_button(out, i, buttons[i]);
        }
        gi::mark_kind(out, gi::SNAP_CONTROLLER_BUTTON, ts);
    }
    if (present & gi::SNAP_CONTROLLER_SWITCH) {
        GameInputSwitchPosition switches[gi::kMaxControllerSwitches] = {};
        const uint32_t native_count = reading->GetControllerSwitchCount();
        const uint32_t written =
                reading->GetControllerSwitchState(gi::kMaxControllerSwitches, switches);
        out.switch_count_native = std::max(native_count, written);
        out.switch_count = std::min<uint32_t>(written, gi::kMaxControllerSwitches);
        for (uint32_t i = 0; i < gi::kMaxControllerSwitches; ++i) {
            out.switches[i] = i < out.switch_count ? (int32_t)switches[i] : 0;
        }
        gi::mark_kind(out, gi::SNAP_CONTROLLER_SWITCH, ts);
    }
}

// --- Pending event housekeeping ---------------------------------------------

void GameInput::_release_pending_events_locked() {
    for (uint32_t i = 0; i < m_pending_events.size(); ++i) {
        if (m_pending_events[i].native_device) {
            m_pending_events[i].native_device->Release();
        }
    }
    m_pending_events.clear();
}

void GameInput::_clear_pending_events() {
    std::lock_guard<std::mutex> lock(m_event_mutex);
    _release_pending_events_locked();
}

void GameInput::_ensure_reading_rings() {
    // The main thread is the only writer of both pointers, so reading them
    // here without the lock is safe; publishing the active ring needs it.
    if (!m_reading_ring) {
        std::unique_ptr<ReadingRing> ring = std::make_unique<ReadingRing>(kReadingRingCapacity);
        std::lock_guard<std::mutex> lock(m_event_mutex);
        m_reading_ring = std::move(ring);
    }
    if (!m_reading_ring_spare) {
        m_reading_ring_spare = std::make_unique<ReadingRing>(kReadingRingCapacity);
    }
}

void GameInput::_load_settings() {
    ProjectSettings *ps = ProjectSettings::get_singleton();
    if (!ps) return;
    if (!m_reading_callback_kinds_overridden && ps->has_setting(kReadingCallbackKindsSetting)) {
        m_reading_callback_kinds =
                (int)(int64_t)ps->get_setting(kReadingCallbackKindsSetting, 0) & DEVICE_ANY;
    }
    if (!m_focus_policy_overridden && ps->has_setting(kFocusPolicySetting)) {
        m_focus_policy = (int)(int64_t)ps->get_setting(kFocusPolicySetting, 0) & kFocusPolicyMask;
    }
}

bool GameInput::_apply_reading_callback_registration() {
    const bool had_live = m_accepting_readings.load(std::memory_order_acquire);
    m_accepting_readings.store(false, std::memory_order_release);
    m_active_reading_token.store(0, std::memory_order_release);
    _unregister_callback(m_reading_callback_token, "reading");
    _wait_for_callbacks();
    if (had_live) {
        // Readings between the old and the new registration are not seen.
        for (int i = 0; i < m_devices.size(); ++i) {
            m_devices.write[i].event_gap_pending = true;
        }
    }

    m_reading_snap_kinds = gi::snapshot_kinds_from_public((uint32_t)m_reading_callback_kinds);
    if (m_reading_snap_kinds == 0 || !m_initialized) {
        return true;
    }
    _ensure_reading_rings();

    if (m_backend == Backend::Mock) {
        m_accepting_readings.store(true, std::memory_order_release);
        return true;
    }
    if (!m_game_input) {
        return false;
    }

    m_accepting_readings.store(true, std::memory_order_release);
    GameInputCallbackToken token = 0;
    HRESULT hr = m_game_input->RegisterReadingCallback(
        nullptr, (GameInputKind)native_kinds_from_snap(m_reading_snap_kinds), this,
        &GameInput::_on_reading_callback, &token);
    if (FAILED(hr)) {
        m_accepting_readings.store(false, std::memory_order_release);
        UtilityFunctions::push_warning("GameInput: RegisterReadingCallback failed (hr=",
                                       hresult_to_string(hr),
                                       "); reading_received will not fire.");
        return false;
    }
    m_reading_callback_token = token;
    m_active_reading_token.store((uint64_t)token, std::memory_order_release);
    return true;
}

// --- Main-thread drain -------------------------------------------------------

void GameInput::_drain_callback_events() {
    if (m_reading_ring && !m_reading_ring_spare) {
        m_reading_ring_spare = std::make_unique<ReadingRing>(kReadingRingCapacity);
    }

    LocalVector<PendingEvent> events;
    std::unique_ptr<ReadingRing> readings;
    bool overflowed = false;
    {
        std::lock_guard<std::mutex> lock(m_event_mutex);
        events = m_pending_events;
        m_pending_events.clear();
        if (m_reading_ring && !m_reading_ring->empty() && m_reading_ring_spare) {
            readings = std::move(m_reading_ring);
            m_reading_ring = std::move(m_reading_ring_spare);
        }
        overflowed = m_reading_ring_overflowed;
        m_reading_ring_overflowed = false;
    }

    for (int i = 0; i < m_devices.size(); ++i) {
        DeviceEntry &e = m_devices.write[i];
        e.frame_readings = Array();
        if (overflowed) {
            e.event_gap_pending = true;
        }
    }

    const uint64_t gen = m_generation;
    const uint32_t reading_count = readings ? readings->size() : 0;
    ReadingRing *ring = readings.get();
    gi::MergeCursor cursor = gi::merge_by_sequence(
        events.size(), [&](uint32_t i) { return events[i].seq; },
        reading_count, [&](uint32_t i) { return ring->at(i).seq; },
        [&](uint32_t i) {
            _handle_pending_event(events[i]);
            return m_initialized && m_generation == gen;
        },
        [&](uint32_t i) {
            _handle_reading_event(ring->at(i));
            return m_initialized && m_generation == gen;
        });

    // A handler shut the runtime down: release what was never delivered.
    for (uint32_t i = cursor.next_a; i < events.size(); ++i) {
        if (events[i].native_device) {
            events[i].native_device->Release();
            events[i].native_device = nullptr;
        }
    }
    if (ring) {
        for (uint32_t i = cursor.next_b; i < reading_count; ++i) {
            ReadingEvent &r = ring->at(i);
            if (r.native_device) {
                r.native_device->Release();
                r.native_device = nullptr;
            }
        }
        ring->clear();
        if (m_initialized && m_reading_ring && !m_reading_ring_spare) {
            m_reading_ring_spare = std::move(readings);
        }
    }
}

void GameInput::_handle_pending_event(PendingEvent &ev) {
    if (ev.kind == PendingEventKind::DeviceStatus) {
        _handle_device_status(ev);
        return;
    }

    const int idx = _find_index_for_event(ev.native_device, ev.mock_device_id);
    if (ev.native_device) {
        ev.native_device->Release();
        ev.native_device = nullptr;
    }
    if (idx < 0) {
        return;
    }
    DeviceEntry &e = m_devices.write[idx];
    if (ev.kind == PendingEventKind::SystemButtons) {
        const uint32_t previous = e.system_buttons;
        if (previous == ev.current) return;
        e.system_buttons = ev.current;
        Ref<GameInputDevice> w = e.wrapper;
        emit_signal("system_buttons_changed", w, (int)ev.current, (int)previous,
                    (int64_t)ev.timestamp);
    } else {
        const uint32_t previous = e.keyboard_layout;
        if (previous == ev.current) return;
        e.keyboard_layout = ev.current;
        Ref<GameInputDevice> w = e.wrapper;
        emit_signal("keyboard_layout_changed", w, (int64_t)ev.current, (int64_t)previous,
                    (int64_t)ev.timestamp);
    }
}

void GameInput::_handle_device_status(PendingEvent &ev) {
    IGameInputDevice *native = ev.native_device; // this handler owns the event's ref
    ev.native_device = nullptr;
    const bool connected = (ev.current & GameInputDeviceConnected) != 0;
    const int idx = _find_index_for_event(native, ev.mock_device_id);

    if (idx < 0) {
        if (!connected) {
            if (native) {
                native->Release();
            } else {
                m_mock_pending_infos.erase(ev.mock_device_id);
            }
            return;
        }

        DeviceEntry entry;
        if (native) {
            entry.id = m_next_device_id.fetch_add(1);
            entry.native = native; // takes ownership of the AddRef
            _fill_caps_from_native(native, entry.caps);
            const GameInputDeviceInfo *info = _get_device_info(native);
            if (info && info->keyboardInfo) {
                entry.keyboard_layout = info->keyboardInfo->layout;
            }
        } else {
            const Dictionary *pending = m_mock_pending_infos.getptr(ev.mock_device_id);
            if (!pending) {
                return; // removed again before the connect was drained
            }
            entry.id = ev.mock_device_id;
            entry.is_mock = true;
            entry.mock_info = *pending;
            m_mock_pending_infos.erase(ev.mock_device_id);
            _fill_caps_from_mock(entry.mock_info, entry.caps);
            entry.keyboard_layout = (uint32_t)dict_int(entry.mock_info, "keyboard_layout", 0);
        }
        entry.kind_mask = _kind_mask_from_supported(entry.caps.native_supported_input);
        entry.status = ev.current;
        entry.wrapper = _make_wrapper(entry.id);
        m_devices.push_back(entry);

        Ref<GameInputDevice> w = entry.wrapper;
        UtilityFunctions::print(
            "GameInput: device connected id=", entry.id,
            " kind_mask=", entry.kind_mask,
            " name=", device_get_display_name(entry.id));
        emit_signal("device_connected", w);
        return;
    }

    if (native) {
        native->Release(); // the cache entry already holds a reference
    }
    DeviceEntry &e = m_devices.write[idx];
    if (connected) {
        const uint32_t previous = e.status;
        if (previous == ev.current) return;
        e.status = ev.current;
        _emit_status_change(e, ev.current, previous, ev.timestamp);
        return;
    }

    const int64_t id = e.id;
    _release_effects_for_device(id, false);
    _remove_device_at(idx);
    UtilityFunctions::print("GameInput: device disconnected id=", id);
    emit_signal("device_disconnected", (int64_t)id);
}

void GameInput::_emit_status_change(const DeviceEntry &entry, uint32_t current,
                                    uint32_t previous, uint64_t timestamp) {
    Ref<GameInputDevice> w = entry.wrapper;
    emit_signal("device_status_changed", w, (int64_t)current, (int64_t)previous,
                (int64_t)timestamp);
}

void GameInput::_handle_reading_event(ReadingEvent &ev) {
    const int idx = _find_index_for_event(ev.native_device, ev.mock_device_id);
    if (ev.native_device) {
        ev.native_device->Release();
        ev.native_device = nullptr;
    }
    if (idx < 0) {
        return;
    }
    DeviceEntry &e = m_devices.write[idx];
    const bool gap = e.event_gap_pending;
    e.event_gap_pending = false;
    Ref<GameInputReading> reading =
            _make_reading(e, ev.snapshot, e.event_state, e.event_state.kinds, gap);
    gi::merge_kinds(e.event_state, ev.snapshot);
    e.frame_readings.push_back(reading);
    Ref<GameInputDevice> w = e.wrapper;
    emit_signal("reading_received", w, reading);
}

void GameInput::_remove_device_at(int idx) {
    DeviceEntry &e = m_devices.write[idx];
    if (e.native) {
        e.native->Release();
        e.native = nullptr;
    }
    m_devices.remove_at(idx);
}

// --- Per-frame poll ----------------------------------------------------------

void GameInput::_poll_native_device(DeviceEntry &entry) {
    if (!m_game_input || !entry.native) return;
    for (uint32_t group : kPollGroups) {
        const uint32_t kinds = group & entry.caps.native_supported_input;
        if (!kinds) continue;
        IGameInputReading *reading = nullptr;
        HRESULT hr = m_game_input->GetCurrentReading((GameInputKind)kinds, entry.native, &reading);
        if (FAILED(hr) || !reading) continue;
        _fill_snapshot_from_reading(reading, snap_kinds_from_native(kinds), entry.poll_cur);
        reading->Release();
    }
}

void GameInput::_real_poll() {
    for (int i = 0; i < m_devices.size(); ++i) {
        DeviceEntry &e = m_devices.write[i];
        if (e.poll_has_cur) {
            e.poll_prev = e.poll_cur;
        }
        if (e.is_mock) {
            gi::merge_kinds(e.poll_cur, e.mock_state);
        } else {
            _poll_native_device(e);
        }
        e.poll_has_cur = e.poll_cur.kinds != 0;
    }
}

void GameInput::_update_vibration_timers() {
    uint64_t now = 0;
    bool have_now = false;
    for (int i = 0; i < m_devices.size(); ++i) {
        DeviceEntry &e = m_devices.write[i];
        if (!e.rumble_active || e.rumble_end_usec == 0) continue;
        if (!have_now) {
            now = _now_usec();
            have_now = true;
        }
        if (now < e.rumble_end_usec) continue;
        const float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        _apply_rumble(e, zero);
        e.rumble_end_usec = 0;
        e.rumble_duration = 0.0;
    }
}

bool GameInput::initialize() {
    if (m_initialized) {
        return true;
    }
    m_accepting_callbacks.store(false, std::memory_order_release);
    m_accepting_readings.store(false, std::memory_order_release);
    HRESULT hr = ::GameInputCreate(&m_game_input);
    if (FAILED(hr) || !m_game_input) {
        if (!m_warned_create_failed) {
            UtilityFunctions::push_warning(
                "GameInput: GameInputCreate() failed (hr=", (int64_t)hr,
                "). The runtime will operate in disabled mode.");
            m_warned_create_failed = true;
        }
        m_game_input = nullptr;
        return false;
    }

    m_backend = Backend::Native;
    m_generation++;
    _load_settings();
    m_game_input->SetFocusPolicy((GameInputFocusPolicy)m_focus_policy);

    m_accepting_callbacks.store(true, std::memory_order_release);
    hr = m_game_input->RegisterDeviceCallback(
        nullptr,
        (GameInputKind)kNativeReadableKinds,
        GameInputDeviceAnyStatus,
        GameInputBlockingEnumeration,
        this,
        &GameInput::_on_device_callback,
        &m_device_callback_token);
    if (FAILED(hr)) {
        m_accepting_callbacks.store(false, std::memory_order_release);
        UtilityFunctions::push_warning(
            "GameInput: RegisterDeviceCallback failed (hr=", (int64_t)hr, ")");
        _wait_for_callbacks();
        _clear_pending_events();
        m_game_input->Release();
        m_game_input = nullptr;
        m_device_callback_token = 0;
        m_backend = Backend::None;
        return false;
    }

    // Optional callbacks. Hosts without the corresponding feature report
    // E_NOTIMPL; the rest of the runtime works without them.
    hr = m_game_input->RegisterSystemButtonCallback(
        nullptr,
        (GameInputSystemButtons)(GameInputSystemButtonGuide | GameInputSystemButtonShare),
        this, &GameInput::_on_system_button_callback, &m_system_button_callback_token);
    if (FAILED(hr)) {
        m_system_button_callback_token = 0;
        UtilityFunctions::print_verbose("GameInput: RegisterSystemButtonCallback unavailable (hr=",
                                        hresult_to_string(hr),
                                        "); system_buttons_changed will not fire.");
    }
    hr = m_game_input->RegisterKeyboardLayoutCallback(
        nullptr, this, &GameInput::_on_keyboard_layout_callback,
        &m_keyboard_layout_callback_token);
    if (FAILED(hr)) {
        m_keyboard_layout_callback_token = 0;
        UtilityFunctions::print_verbose(
            "GameInput: RegisterKeyboardLayoutCallback unavailable (hr=", hresult_to_string(hr),
            "); keyboard_layout_changed will not fire.");
    }

    m_initialized = true;
    m_warned_uninitialized = false;
    m_dropped_reading_count.store(0, std::memory_order_relaxed);
    _apply_reading_callback_registration();
    UtilityFunctions::print("GameInput: initialized");
    return true;
}

void GameInput::shutdown() {
    if (!m_initialized && !m_game_input && m_backend == Backend::None) return;

    // 1. Signal "no new work" so any callback that observes this bails
    //    before touching singleton state.
    m_accepting_readings.store(false, std::memory_order_release);
    m_accepting_callbacks.store(false, std::memory_order_release);
    m_active_reading_token.store(0, std::memory_order_release);
    m_shutting_down.store(true, std::memory_order_release);

    // 2. Unregister. GameInput v3 dropped the timeout parameter that v1
    //    accepted here; m_callbacks_in_flight below also covers an unregister
    //    that fails.
    _unregister_callback(m_reading_callback_token, "reading");
    _unregister_callback(m_system_button_callback_token, "system button");
    _unregister_callback(m_keyboard_layout_callback_token, "keyboard layout");
    _unregister_callback(m_device_callback_token, "device");

    // 3. Wait for any callback currently inside the body to exit.
    _wait_for_callbacks();

    _release_all_effects();

    // Stop rumble + release every device cleanly.
    for (int i = 0; i < m_devices.size(); ++i) {
        DeviceEntry &e = m_devices.write[i];
        if (e.native) {
            if (e.caps.rumble_motors != GameInputRumbleNone) {
                GameInputRumbleParams zero{};
                _set_rumble_state_checked(e.native, &zero);
            }
            e.native->Release();
            e.native = nullptr;
        }
    }
    m_devices.clear();

    // Drain any leftover queued events to release their AddRef.
    _clear_pending_events();
    {
        std::lock_guard<std::mutex> lock(m_event_mutex);
        if (m_reading_ring) {
            for (uint32_t i = 0; i < m_reading_ring->size(); ++i) {
                ReadingEvent &r = m_reading_ring->at(i);
                if (r.native_device) {
                    r.native_device->Release();
                    r.native_device = nullptr;
                }
            }
            m_reading_ring.reset();
        }
        m_reading_ring_overflowed = false;
    }
    m_reading_ring_spare.reset();
    m_mock_pending_infos.clear();

    if (m_game_input) {
        m_game_input->Release();
        m_game_input = nullptr;
    }

    m_initialized = false;
    m_backend = Backend::None;
    m_last_polled_frame = UINT64_MAX;
    m_generation++;
    m_time_override_usec = -1;
    m_reading_snap_kinds = 0;
    m_shutting_down.store(false, std::memory_order_release);
    UtilityFunctions::print("GameInput: shutdown");
}

void GameInput::_on_engine_shutdown() {
    shutdown();

    // The singleton outlives ScriptServer::finish_languages(): it is deleted
    // at SCENE deinitialization. A GDScript lambda still connected to one of
    // its signals at that point is destroyed after GDScript has gone and
    // crashes the process on exit (0xC0000005). This hook runs from
    // Main::cleanup() before the main loop and the languages are torn down,
    // so drop every connection while their targets are still valid.
    const TypedArray<Dictionary> signals = get_signal_list();
    for (int i = 0; i < signals.size(); ++i) {
        const StringName signal_name = Dictionary(signals[i]).get("name", StringName());
        const TypedArray<Dictionary> connections = get_signal_connection_list(signal_name);
        for (int j = 0; j < connections.size(); ++j) {
            const Callable callable = Dictionary(connections[j]).get("callable", Callable());
            if (is_connected(signal_name, callable)) {
                disconnect(signal_name, callable);
            }
        }
    }
}

bool GameInput::is_initialized() const {
    return m_initialized;
}

void GameInput::poll() {
    if (!m_initialized || (!m_game_input && m_backend != Backend::Mock)) {
        return;
    }
    Engine *engine = Engine::get_singleton();
    uint64_t frame = engine ? engine->get_process_frames() : 0;
    if (frame == m_last_polled_frame && frame != UINT64_MAX) {
        return; // already polled this frame
    }
    m_last_polled_frame = frame;

    // Callback events first (connects, status, readings, in arrival order),
    // then the per-frame refresh, so a device that connected this frame
    // already has a current reading when the frame's scripts run.
    const uint64_t gen = m_generation;
    _drain_callback_events();
    if (!m_initialized || m_generation != gen) {
        return;
    }
    _real_poll();
    _update_vibration_timers();
}

// --- Lookup helpers -------------------------------------------------------------

bool GameInput::_ensure_initialized() {
    if (m_initialized && (m_game_input != nullptr || m_backend == Backend::Mock)) {
        return true;
    }
    if (!m_warned_uninitialized) {
        UtilityFunctions::push_warning(
            "GameInput: method called before initialize() succeeded — returning safe default. "
            "Call GameInput.initialize() once at startup.");
        m_warned_uninitialized = true;
    }
    return false;
}

int GameInput::_kind_mask_from_supported(uint32_t native_kinds) const {
    return (int)gi::public_kinds_from_snapshot(snap_kinds_from_native(native_kinds));
}

int GameInput::_find_index_by_id(int64_t id) const {
    for (int i = 0; i < m_devices.size(); ++i) {
        if (m_devices[i].id == id) return i;
    }
    return -1;
}

int GameInput::_find_index_by_native(IGameInputDevice *native) const {
    if (!native) return -1;
    for (int i = 0; i < m_devices.size(); ++i) {
        if (m_devices[i].native == native) return i;
    }
    return -1;
}

int GameInput::_find_index_for_event(IGameInputDevice *native, int64_t mock_id) const {
    return native ? _find_index_by_native(native) : _find_index_by_id(mock_id);
}

Ref<GameInputDevice> GameInput::_make_wrapper(int64_t id) {
    Ref<GameInputDevice> w;
    w.instantiate();
    w->_set_device_id(id);
    return w;
}

Ref<GameInputReading> GameInput::_make_reading(const DeviceEntry &entry, const gi::Snapshot &cur,
                                               const gi::Snapshot &prev, uint32_t prev_kinds,
                                               bool gap_before) const {
    Ref<GameInputReading> r;
    r.instantiate();
    r->_set_snapshot(entry.id, cur, prev, prev_kinds, gap_before, entry.caps.sensors);
    return r;
}

uint64_t GameInput::_now_usec() const {
    if (m_time_override_usec >= 0) {
        return (uint64_t)m_time_override_usec;
    }
    Time *time = Time::get_singleton();
    return time ? time->get_ticks_usec() : 0;
}

void GameInput::_fill_caps_from_native(IGameInputDevice *native, DeviceCaps &caps) const {
    const GameInputDeviceInfo *info = _get_device_info(native);
    if (!info) return;
    caps.native_supported_input = (uint32_t)info->supportedInput;
    caps.rumble_motors = (uint32_t)info->supportedRumbleMotors;
    caps.system_buttons = (uint32_t)info->supportedSystemButtons;
    caps.sensors = info->sensorsInfo ? (uint32_t)info->sensorsInfo->supportedSensors : 0u;
    caps.family = (int32_t)info->deviceFamily;
    caps.ffb_motor_axes.clear();
    caps.ffb_motor_effects.clear();
    if (info->forceFeedbackMotorInfo) {
        for (uint32_t i = 0; i < info->forceFeedbackMotorCount; ++i) {
            caps.ffb_motor_axes.push_back((uint32_t)info->forceFeedbackMotorInfo[i].supportedAxes);
            caps.ffb_motor_effects.push_back(
                    effect_bits_from_motor(info->forceFeedbackMotorInfo[i]));
        }
    }
}

void GameInput::_fill_caps_from_mock(const Dictionary &info, DeviceCaps &caps) const {
    const uint32_t kinds = (uint32_t)dict_int(info, "kind_mask", DEVICE_GAMEPAD) & DEVICE_ANY;
    caps.native_supported_input = native_kinds_from_snap(gi::snapshot_kinds_from_public(kinds));
    caps.rumble_motors = (uint32_t)dict_int(info, "rumble_motors", 0) & 0xFu;
    caps.system_buttons = (uint32_t)dict_int(info, "system_buttons", 0) & 0x3u;
    caps.sensors = (uint32_t)dict_int(info, "sensors", (kinds & DEVICE_SENSORS) ? 0xF : 0) & 0xFu;
    caps.family = (int32_t)dict_int(info, "family", (int64_t)GameInputFamilyVirtual);
    caps.ffb_motor_axes.clear();
    caps.ffb_motor_effects.clear();
    Array motors = info.get("ffb_motors", Array());
    for (int i = 0; i < motors.size(); ++i) {
        Dictionary motor = motors[i];
        caps.ffb_motor_axes.push_back(
                (uint32_t)dict_int(motor, "axes", (int64_t)GameInputFeedbackAxisLinearX) & 0x7Fu);
        uint32_t effects = (1u << kEffectKindCount) - 1u;
        if (motor.has("effects")) {
            Variant ev = motor["effects"];
            if (ev.get_type() == Variant::INT) {
                effects = (uint32_t)(int64_t)ev;
            } else {
                effects = 0;
                Array list = variant_to_array(ev);
                for (int j = 0; j < list.size(); ++j) {
                    int kind = (int)(int64_t)list[j];
                    if (kind >= 0 && kind < kEffectKindCount) effects |= 1u << kind;
                }
            }
        }
        caps.ffb_motor_effects.push_back(effects & ((1u << kEffectKindCount) - 1u));
    }
}

// --- Public API ------------------------------------------------------------------

Array GameInput::get_devices(int kind_mask) {
    Array out;
    if (!_ensure_initialized()) {
        return out;
    }
    for (int i = 0; i < m_devices.size(); ++i) {
        if (m_devices[i].kind_mask & kind_mask) {
            out.push_back(m_devices[i].wrapper);
        }
    }
    return out;
}

Ref<GameInputDevice> GameInput::get_primary_device(int kind_mask) {
    if (!_ensure_initialized()) {
        return Ref<GameInputDevice>();
    }
    for (int i = 0; i < m_devices.size(); ++i) {
        if (m_devices[i].kind_mask & kind_mask) {
            return m_devices[i].wrapper;
        }
    }
    return Ref<GameInputDevice>();
}

Ref<GameInputDevice> GameInput::get_device_by_id(int64_t device_id) {
    if (!_ensure_initialized()) {
        return Ref<GameInputDevice>();
    }
    int idx = _find_index_by_id(device_id);
    return idx < 0 ? Ref<GameInputDevice>() : m_devices[idx].wrapper;
}

Ref<GameInputReading> GameInput::get_current_reading(const Ref<GameInputDevice> &device) {
    Ref<GameInputReading> r;
    if (!_ensure_initialized() || device.is_null()) {
        return r;
    }
    int idx = _find_index_by_id(device->get_device_id());
    if (idx < 0) {
        return r;
    }
    DeviceEntry &e = m_devices.write[idx];
    if (!e.poll_has_cur) {
        // Not polled since the device connected: fetch on demand. The
        // previous sample stays empty, so the first reading has no edges.
        if (e.is_mock) {
            gi::merge_kinds(e.poll_cur, e.mock_state);
        } else {
            _poll_native_device(e);
        }
        e.poll_has_cur = e.poll_cur.kinds != 0;
        if (!e.poll_has_cur) {
            return r;
        }
    }
    return _make_reading(e, e.poll_cur, e.poll_prev, e.poll_prev.kinds, false);
}

int GameInput::get_connected_device_count(int kind_mask) const {
    int count = 0;
    for (int i = 0; i < m_devices.size(); ++i) {
        if (m_devices[i].kind_mask & kind_mask) {
            ++count;
        }
    }
    return count;
}

bool GameInput::_apply_rumble(DeviceEntry &entry, const float values[4]) {
    const uint32_t motors = entry.caps.rumble_motors;
    if (motors == GameInputRumbleNone) {
        return false;
    }
    float v[4] = {clamp01(values[0]), clamp01(values[1]), clamp01(values[2]), clamp01(values[3])};
    if (!(motors & GameInputRumbleLowFrequency)) v[0] = 0.0f;
    if (!(motors & GameInputRumbleHighFrequency)) v[1] = 0.0f;
    if (!(motors & GameInputRumbleLeftTrigger)) v[2] = 0.0f;
    if (!(motors & GameInputRumbleRightTrigger)) v[3] = 0.0f;

    if (entry.native) {
        GameInputRumbleParams params{};
        params.lowFrequency = v[0];
        params.highFrequency = v[1];
        params.leftTrigger = v[2];
        params.rightTrigger = v[3];
        HRESULT hr = _set_rumble_state_checked(entry.native, &params);
        if (FAILED(hr)) {
            UtilityFunctions::push_warning(
                "GameInput: SetRumbleState failed (hr=", (int64_t)hr, ")");
            return false;
        }
    }
    for (int i = 0; i < 4; ++i) {
        entry.rumble[i] = v[i];
    }
    entry.rumble_active = v[0] > 0.0f || v[1] > 0.0f || v[2] > 0.0f || v[3] > 0.0f;
    entry.rumble_apply_count++;
    return true;
}

bool GameInput::set_vibration(const Ref<GameInputDevice> &device, float low_freq, float high_freq,
                              float left_trigger, float right_trigger) {
    if (!_ensure_initialized() || device.is_null()) {
        return false;
    }
    int idx = _find_index_by_id(device->get_device_id());
    if (idx < 0) return false;
    DeviceEntry &e = m_devices.write[idx];
    const float values[4] = {low_freq, high_freq, left_trigger, right_trigger};
    if (!_apply_rumble(e, values)) {
        return false;
    }
    e.rumble_end_usec = 0;
    e.rumble_duration = 0.0;
    return true;
}

void GameInput::stop_haptics(const Ref<GameInputDevice> &device) {
    if (!_ensure_initialized() || device.is_null()) {
        return;
    }
    const int64_t id = device->get_device_id();
    int idx = _find_index_by_id(id);
    if (idx < 0) return;
    DeviceEntry &e = m_devices.write[idx];
    const float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    _apply_rumble(e, zero);
    e.rumble_end_usec = 0;
    e.rumble_duration = 0.0;
    for (KeyValue<int64_t, EffectEntry> &kv : m_effects) {
        EffectEntry &fx = kv.value;
        if (fx.device_id != id) continue;
        if (fx.native) {
            fx.native->SetState(GameInputFeedbackStopped);
        } else {
            fx.mock_state = GameInputForceFeedbackEffect::STATE_STOPPED;
        }
    }
}

int64_t GameInput::get_current_timestamp() {
    if (!_ensure_initialized()) {
        return 0;
    }
    if (m_backend == Backend::Native && m_game_input) {
        return (int64_t)m_game_input->GetCurrentTimestamp();
    }
    return (int64_t)_now_usec();
}

bool GameInput::set_reading_callback_kinds(int kind_mask) {
    const int masked = kind_mask & DEVICE_ANY;
    if (masked != kind_mask) {
        UtilityFunctions::push_warning(
            "GameInput: set_reading_callback_kinds() ignored bits outside DEVICE_ANY (",
            (int64_t)kind_mask, ").");
    }
    const bool unchanged = masked == m_reading_callback_kinds;
    m_reading_callback_kinds = masked;
    m_reading_callback_kinds_overridden = true;
    if (!m_initialized) {
        return true; // applied by initialize()
    }
    const bool want_live = masked != 0;
    if (unchanged && want_live == m_accepting_readings.load(std::memory_order_acquire)) {
        return true;
    }
    return _apply_reading_callback_registration();
}

int GameInput::get_reading_callback_kinds() const {
    return m_reading_callback_kinds;
}

Array GameInput::get_buffered_readings(const Ref<GameInputDevice> &device) {
    if (!_ensure_initialized() || device.is_null()) {
        return Array();
    }
    int idx = _find_index_by_id(device->get_device_id());
    return idx < 0 ? Array() : m_devices[idx].frame_readings.duplicate();
}

int64_t GameInput::get_dropped_reading_count() const {
    return (int64_t)m_dropped_reading_count.load(std::memory_order_relaxed);
}

void GameInput::set_focus_policy(int policy) {
    const int masked = policy & kFocusPolicyMask;
    if (masked != policy) {
        UtilityFunctions::push_warning(
            "GameInput: set_focus_policy() ignored unknown policy bits (", (int64_t)policy, ").");
    }
    m_focus_policy = masked;
    m_focus_policy_overridden = true;
    if (m_initialized && m_backend == Backend::Native && m_game_input) {
        m_game_input->SetFocusPolicy((GameInputFocusPolicy)masked);
    }
}

int GameInput::get_focus_policy() const {
    return m_focus_policy;
}

String GameInput::create_aggregate_device(int kind) {
    if (!_ensure_initialized()) {
        return String();
    }
    if (m_backend != Backend::Native || !m_game_input) {
        return String();
    }
    const uint32_t native =
            native_kinds_from_snap(gi::snapshot_kinds_from_public((uint32_t)kind & DEVICE_ANY));
    if (!native) {
        UtilityFunctions::push_warning(
            "GameInput: create_aggregate_device() needs at least one DeviceKind bit.");
        return String();
    }
    APP_LOCAL_DEVICE_ID id{};
    HRESULT hr = m_game_input->CreateAggregateDevice((GameInputKind)native, &id);
    if (FAILED(hr)) {
        UtilityFunctions::push_warning("GameInput: CreateAggregateDevice failed (hr=",
                                       hresult_to_string(hr), ")");
        return String();
    }
    return app_local_id_to_string(id);
}

bool GameInput::disable_aggregate_device(const String &app_local_id) {
    if (!_ensure_initialized()) {
        return false;
    }
    if (m_backend != Backend::Native || !m_game_input) {
        return false;
    }
    APP_LOCAL_DEVICE_ID id{};
    if (!app_local_id_from_string(app_local_id, id)) {
        UtilityFunctions::push_warning(
            "GameInput: disable_aggregate_device() expects the 64-character hex id returned "
            "by create_aggregate_device().");
        return false;
    }
    HRESULT hr = m_game_input->DisableAggregateDevice(&id);
    if (FAILED(hr)) {
        UtilityFunctions::push_warning("GameInput: DisableAggregateDevice failed (hr=",
                                       hresult_to_string(hr), ")");
        return false;
    }
    return true;
}

// --- Device helpers (called by GameInputDevice) ----------------------------------

bool GameInput::device_lookup(int64_t id, IGameInputDevice **out_native, int *out_kind_mask) {
    int idx = _find_index_by_id(id);
    if (idx < 0) return false;
    if (out_native) *out_native = m_devices[idx].native;
    if (out_kind_mask) *out_kind_mask = m_devices[idx].kind_mask;
    return true;
}

String GameInput::device_get_display_name(int64_t id) {
    int idx = _find_index_by_id(id);
    if (idx < 0) {
        return String();
    }
    const DeviceEntry &e = m_devices[idx];
    if (e.is_mock) {
        String name = e.mock_info.get("name", String());
        return name.is_empty() ? String("GameInput Mock Device") : name;
    }
    if (!e.native) {
        return String();
    }
    const GameInputDeviceInfo *info = _get_device_info(e.native);
    // GameInput v3 ships `displayName` as a plain null-terminated `const char *`
    // (v1 wrapped it in a `GameInputString` struct with `.data` + `.sizeInBytes`).
    // Many HID keyboards and mice report an empty name rather than null; both
    // fall back to the documented "GameInput Device VVVV:PPPP" form. Aggregates
    // report 0000:0000, so they get a name that says what they are instead.
    if (info && info->displayName && info->displayName[0] != '\0') {
        return String::utf8(info->displayName);
    }
    if (info && info->deviceFamily == GameInputFamilyAggregate) {
        return String("GameInput Aggregate Device");
    }
    if (info) {
        return String("GameInput Device ") +
               String::num_int64(info->vendorId, 16).to_upper().lpad(4, "0") + String(":") +
               String::num_int64(info->productId, 16).to_upper().lpad(4, "0");
    }
    return String("GameInput Device");
}

bool GameInput::device_is_connected(int64_t id) {
    return _find_index_by_id(id) >= 0;
}

Dictionary GameInput::device_get_device_info(int64_t id) {
    Dictionary out;
    int idx = _find_index_by_id(id);
    if (idx < 0) {
        return out;
    }
    const DeviceEntry &e = m_devices[idx];
    if (e.is_mock) {
        return _build_mock_device_info(e);
    }
    const GameInputDeviceInfo *info = _get_device_info(e.native);
    if (!info) {
        return out;
    }

    out["name"] = device_get_display_name(id);
    out["vendor_id"] = (int)info->vendorId;
    out["product_id"] = (int)info->productId;
    out["revision"] = (int)info->revisionNumber;
    out["device_family"] = (int)info->deviceFamily;
    out["supported_input_kinds"] = (int)info->supportedInput;
    out["supported_input_mask"] = (int)e.kind_mask;
    out["supports_vibration"] = info->supportedRumbleMotors != GameInputRumbleNone;
    // Controller axis/button counts moved under `controllerInfo` in v3; the
    // pointer is null for non-controller devices.
    out["controller_axis_count"] =
        (int)(info->controllerInfo ? info->controllerInfo->controllerAxisCount : 0u);
    out["controller_button_count"] =
        (int)(info->controllerInfo ? info->controllerInfo->controllerButtonCount : 0u);
    out["force_feedback_motor_count"] = (int)info->forceFeedbackMotorCount;

    out["controller_switch_count"] =
        (int)(info->controllerInfo ? info->controllerInfo->controllerSwitchCount : 0u);
    out["status"] = (int64_t)e.status;
    out["app_local_id"] = app_local_id_to_string(info->deviceId);
    out["root_app_local_id"] = app_local_id_to_string(info->deviceRootId);
    out["container_id"] = guid_to_string(info->containerId);
    out["pnp_path"] = info->pnpPath ? String::utf8(info->pnpPath) : String();
    out["hardware_version"] = version_to_string(info->hardwareVersion);
    out["firmware_version"] = version_to_string(info->firmwareVersion);
    out["usage_page"] = (int)info->usage.page;
    out["usage_id"] = (int)info->usage.id;
    out["supported_rumble_motors"] = (int)info->supportedRumbleMotors;
    out["supported_system_buttons"] = (int)info->supportedSystemButtons;
    out["is_mock"] = false;

    if (const GameInputKeyboardInfo *k = info->keyboardInfo) {
        Dictionary d;
        d["kind"] = (int)k->kind;
        d["layout"] = (int64_t)k->layout;
        d["key_count"] = (int64_t)k->keyCount;
        d["function_key_count"] = (int64_t)k->functionKeyCount;
        d["max_simultaneous_keys"] = (int64_t)k->maxSimultaneousKeys;
        d["platform_type"] = (int64_t)k->platformType;
        d["platform_subtype"] = (int64_t)k->platformSubtype;
        out["keyboard"] = d;
    }
    if (const GameInputMouseInfo *m = info->mouseInfo) {
        Dictionary d;
        d["supported_buttons"] = (int)m->supportedButtons;
        d["sample_rate"] = (int64_t)m->sampleRate;
        d["has_wheel_x"] = m->hasWheelX;
        d["has_wheel_y"] = m->hasWheelY;
        out["mouse"] = d;
    }
    if (const GameInputSensorsInfo *s = info->sensorsInfo) {
        Dictionary d;
        d["supported_sensors"] = (int)s->supportedSensors;
        out["sensors"] = d;
    }
    if (const GameInputGamepadInfo *g = info->gamepadInfo) {
        Dictionary d;
        d["supported_buttons"] = (int)((uint32_t)g->supportedLayout & GameInputDevice::kGamepadButtonMask);
        d["extra_button_count"] = (int64_t)g->extraButtonCount;
        d["extra_axis_count"] = (int64_t)g->extraAxisCount;
        out["gamepad"] = d;
    }
    if (const GameInputArcadeStickInfo *a = info->arcadeStickInfo) {
        Dictionary d;
        d["extra_button_count"] = (int64_t)a->extraButtonCount;
        d["extra_axis_count"] = (int64_t)a->extraAxisCount;
        out["arcade_stick"] = d;
    }
    if (const GameInputFlightStickInfo *f = info->flightStickInfo) {
        Dictionary d;
        d["extra_button_count"] = (int64_t)f->extraButtonCount;
        d["extra_axis_count"] = (int64_t)f->extraAxisCount;
        out["flight_stick"] = d;
    }
    if (const GameInputRacingWheelInfo *w = info->racingWheelInfo) {
        Dictionary d;
        d["has_clutch"] = w->hasClutch;
        d["has_handbrake"] = w->hasHandbrake;
        d["has_pattern_shifter"] = w->hasPatternShifter;
        d["min_pattern_shifter_gear"] = (int)w->minPatternShifterGear;
        d["max_pattern_shifter_gear"] = (int)w->maxPatternShifterGear;
        d["max_wheel_angle"] = w->maxWheelAngle;
        d["extra_button_count"] = (int64_t)w->extraButtonCount;
        d["extra_axis_count"] = (int64_t)w->extraAxisCount;
        out["racing_wheel"] = d;
    }
    Array motors;
    for (uint32_t i = 0; i < e.caps.ffb_motor_axes.size(); ++i) {
        motors.push_back(motor_info_to_dict(e.caps.ffb_motor_axes[i], e.caps.ffb_motor_effects[i]));
    }
    out["force_feedback_motors"] = motors;
    return out;
}

Dictionary GameInput::_build_mock_device_info(const DeviceEntry &e) const {
    const Dictionary &m = e.mock_info;
    Dictionary out;
    String name = m.get("name", String());
    out["name"] = name.is_empty() ? String("GameInput Mock Device") : name;
    out["vendor_id"] = (int)dict_int(m, "vendor_id", 0);
    out["product_id"] = (int)dict_int(m, "product_id", 0);
    out["revision"] = (int)dict_int(m, "revision", 0);
    out["device_family"] = (int)e.caps.family;
    out["supported_input_kinds"] = (int)e.caps.native_supported_input;
    out["supported_input_mask"] = (int)e.kind_mask;
    out["supports_vibration"] = e.caps.rumble_motors != 0;
    out["controller_axis_count"] = (int)dict_int(m, "controller_axis_count", 0);
    out["controller_button_count"] = (int)dict_int(m, "controller_button_count", 0);
    out["force_feedback_motor_count"] = (int)e.caps.ffb_motor_axes.size();

    out["controller_switch_count"] = (int)dict_int(m, "controller_switch_count", 0);
    out["status"] = (int64_t)e.status;
    String app_local_id = m.get("app_local_id", String());
    if (app_local_id.is_empty()) {
        app_local_id = String::num_int64(e.id, 16).lpad(64, "0");
    }
    out["app_local_id"] = app_local_id;
    out["root_app_local_id"] = app_local_id;
    out["container_id"] = String("{00000000-0000-0000-0000-000000000000}");
    out["pnp_path"] = String();
    out["hardware_version"] = String("0.0.0.0");
    out["firmware_version"] = String("0.0.0.0");
    out["usage_page"] = 0;
    out["usage_id"] = 0;
    out["supported_rumble_motors"] = (int)e.caps.rumble_motors;
    out["supported_system_buttons"] = (int)e.caps.system_buttons;
    out["is_mock"] = true;

    if (e.kind_mask & DEVICE_KEYBOARD) {
        Dictionary d;
        d["kind"] = (int)dict_int(m, "keyboard_kind", (int64_t)GameInputAnsiKeyboard);
        d["layout"] = (int64_t)e.keyboard_layout;
        d["key_count"] = dict_int(m, "key_count", 0);
        d["function_key_count"] = (int64_t)0;
        d["max_simultaneous_keys"] = dict_int(m, "max_simultaneous_keys", (int64_t)gi::kMaxKeys);
        d["platform_type"] = (int64_t)0;
        d["platform_subtype"] = (int64_t)0;
        out["keyboard"] = d;
    }
    if (e.kind_mask & DEVICE_MOUSE) {
        Dictionary d;
        d["supported_buttons"] = (int)dict_int(m, "mouse_buttons", 0x1F);
        d["sample_rate"] = dict_int(m, "sample_rate", 0);
        d["has_wheel_x"] = dict_int(m, "has_wheel_x", 0) != 0;
        d["has_wheel_y"] = dict_int(m, "has_wheel_y", 1) != 0;
        out["mouse"] = d;
    }
    if (e.caps.sensors) {
        Dictionary d;
        d["supported_sensors"] = (int)e.caps.sensors;
        out["sensors"] = d;
    }
    if (e.kind_mask & DEVICE_GAMEPAD) {
        Dictionary d;
        d["supported_buttons"] = (int)((uint32_t)dict_int(m, "gamepad_buttons", 0x3FFF) &
                                       GameInputDevice::kGamepadButtonMask);
        d["extra_button_count"] = (int64_t)0;
        d["extra_axis_count"] = (int64_t)0;
        out["gamepad"] = d;
    }
    if (e.kind_mask & DEVICE_RACING_WHEEL) {
        Dictionary d;
        d["has_clutch"] = dict_int(m, "has_clutch", 0) != 0;
        d["has_handbrake"] = dict_int(m, "has_handbrake", 0) != 0;
        d["has_pattern_shifter"] = dict_int(m, "has_pattern_shifter", 0) != 0;
        d["min_pattern_shifter_gear"] = (int)dict_int(m, "min_pattern_shifter_gear", 0);
        d["max_pattern_shifter_gear"] = (int)dict_int(m, "max_pattern_shifter_gear", 0);
        d["max_wheel_angle"] = dict_float(m, "max_wheel_angle", 0.0f);
        d["extra_button_count"] = (int64_t)0;
        d["extra_axis_count"] = (int64_t)0;
        out["racing_wheel"] = d;
    }
    if (e.kind_mask & DEVICE_ARCADE_STICK) {
        Dictionary d;
        d["extra_button_count"] = (int64_t)0;
        d["extra_axis_count"] = (int64_t)0;
        out["arcade_stick"] = d;
    }
    if (e.kind_mask & DEVICE_FLIGHT_STICK) {
        Dictionary d;
        d["extra_button_count"] = (int64_t)0;
        d["extra_axis_count"] = (int64_t)0;
        out["flight_stick"] = d;
    }
    Array motors;
    for (uint32_t i = 0; i < e.caps.ffb_motor_axes.size(); ++i) {
        motors.push_back(motor_info_to_dict(e.caps.ffb_motor_axes[i], e.caps.ffb_motor_effects[i]));
    }
    out["force_feedback_motors"] = motors;
    return out;
}

bool GameInput::device_supports_vibration(int64_t id) {
    int idx = _find_index_by_id(id);
    return idx >= 0 && m_devices[idx].caps.rumble_motors != GameInputRumbleNone;
}

bool GameInput::device_supports_haptics(int64_t id) {
    int idx = _find_index_by_id(id);
    if (idx < 0) return false;
    const DeviceEntry &e = m_devices[idx];
    if (e.is_mock) {
        return variant_to_array(e.mock_info.get("haptic_locations", Array())).size() > 0;
    }
    if (!e.native) return false;
    // GameInput v3 dropped `GameInputDeviceInfo::hapticFeedbackMotorCount` and
    // moved haptic capability behind a dedicated `GetHapticInfo` call. A device
    // is considered haptics-capable if the call succeeds and reports at least
    // one haptic location.
    GameInputHapticInfo haptic{};
    HRESULT hr = e.native->GetHapticInfo(&haptic);
    return SUCCEEDED(hr) && haptic.locationCount > 0;
}

int GameInput::device_get_status(int64_t id) {
    int idx = _find_index_by_id(id);
    return idx < 0 ? 0 : (int)m_devices[idx].status;
}

String GameInput::device_get_app_local_id(int64_t id) {
    int idx = _find_index_by_id(id);
    if (idx < 0) return String();
    const DeviceEntry &e = m_devices[idx];
    if (e.is_mock) {
        return _build_mock_device_info(e)["app_local_id"];
    }
    const GameInputDeviceInfo *info = _get_device_info(e.native);
    return info ? app_local_id_to_string(info->deviceId) : String();
}

int GameInput::device_get_family(int64_t id) {
    int idx = _find_index_by_id(id);
    return idx < 0 ? (int)GameInputFamilyUnknown : (int)m_devices[idx].caps.family;
}

int GameInput::device_get_supported_rumble_motors(int64_t id) {
    int idx = _find_index_by_id(id);
    return idx < 0 ? 0 : (int)m_devices[idx].caps.rumble_motors;
}

int GameInput::device_get_supported_system_buttons(int64_t id) {
    int idx = _find_index_by_id(id);
    return idx < 0 ? 0 : (int)m_devices[idx].caps.system_buttons;
}

int GameInput::device_get_system_buttons(int64_t id) {
    int idx = _find_index_by_id(id);
    return idx < 0 ? 0 : (int)m_devices[idx].system_buttons;
}

int GameInput::device_get_keyboard_layout(int64_t id) {
    int idx = _find_index_by_id(id);
    return idx < 0 ? 0 : (int)m_devices[idx].keyboard_layout;
}

String GameInput::device_get_button_label(int64_t id, int source) {
    int idx = _find_index_by_id(id);
    if (idx < 0) return String();
    const DeviceEntry &e = m_devices[idx];
    int32_t label = 0;
    if (e.is_mock) {
        Dictionary labels = e.mock_info.get("labels", Dictionary());
        if (!labels.has(source)) return String();
        label = (int32_t)(int64_t)labels[source];
    } else if (!label_for_source(_get_device_info(e.native), source, label)) {
        return String();
    }
    return String(gi::label_name(label));
}

Dictionary GameInput::device_get_haptic_info(int64_t id) {
    Dictionary out;
    int idx = _find_index_by_id(id);
    if (idx < 0) return out;
    const DeviceEntry &e = m_devices[idx];
    PackedStringArray names;
    PackedStringArray guids;
    out["ready"] = (e.status & GameInputDeviceHapticInfoReady) != 0;
    if (e.is_mock) {
        Array locations = variant_to_array(e.mock_info.get("haptic_locations", Array()));
        for (int i = 0; i < locations.size(); ++i) {
            names.push_back(String(locations[i]));
        }
        out["supported"] = names.size() > 0;
        out["audio_endpoint_id"] = e.mock_info.get("haptic_audio_endpoint_id", String());
    } else {
        GameInputHapticInfo haptic{};
        HRESULT hr = e.native ? e.native->GetHapticInfo(&haptic) : E_POINTER;
        const bool ok = SUCCEEDED(hr);
        out["supported"] = ok && haptic.locationCount > 0;
        if (ok) {
            haptic.audioEndpointId[GAMEINPUT_HAPTIC_MAX_AUDIO_ENDPOINT_ID_SIZE - 1] = L'\0';
            out["audio_endpoint_id"] = String(haptic.audioEndpointId);
            const uint32_t count = std::min(haptic.locationCount, GAMEINPUT_HAPTIC_MAX_LOCATIONS);
            for (uint32_t i = 0; i < count; ++i) {
                names.push_back(String(haptic_location_name(haptic.locations[i])));
                guids.push_back(guid_to_string(haptic.locations[i]));
            }
        } else {
            out["audio_endpoint_id"] = String();
        }
    }
    out["locations"] = names;
    out["location_guids"] = guids;
    return out;
}

bool GameInput::device_start_vibration(int64_t id, float weak, float strong, float duration,
                                       float left_trigger, float right_trigger) {
    if (!_ensure_initialized()) {
        return false;
    }
    int idx = _find_index_by_id(id);
    if (idx < 0) return false;
    DeviceEntry &e = m_devices.write[idx];
    // Godot's joypad convention: the weak magnitude drives the high-frequency
    // (small) motor and the strong magnitude the low-frequency (large) motor.
    const float values[4] = {strong, weak, left_trigger, right_trigger};
    if (!_apply_rumble(e, values)) {
        return false;
    }
    if (!(duration > 0.0f) || !std::isfinite(duration) || !e.rumble_active) {
        e.rumble_end_usec = 0;
        e.rumble_duration = 0.0;
        return true;
    }
    e.rumble_duration = (double)duration;
    e.rumble_end_usec = _now_usec() + (uint64_t)std::llround((double)duration * 1000000.0);
    return true;
}

void GameInput::device_stop_vibration(int64_t id) {
    if (!_ensure_initialized()) {
        return;
    }
    int idx = _find_index_by_id(id);
    if (idx < 0) return;
    DeviceEntry &e = m_devices.write[idx];
    const float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    _apply_rumble(e, zero);
    e.rumble_end_usec = 0;
    e.rumble_duration = 0.0;
}

bool GameInput::device_is_vibrating(int64_t id) {
    int idx = _find_index_by_id(id);
    return idx >= 0 && m_devices[idx].rumble_active;
}

Vector2 GameInput::device_get_vibration_strength(int64_t id) {
    int idx = _find_index_by_id(id);
    if (idx < 0) return Vector2();
    const DeviceEntry &e = m_devices[idx];
    return Vector2(e.rumble[1], e.rumble[0]); // (weak, strong) like Input.get_joy_vibration_strength()
}

Vector2 GameInput::device_get_trigger_vibration_strength(int64_t id) {
    int idx = _find_index_by_id(id);
    if (idx < 0) return Vector2();
    const DeviceEntry &e = m_devices[idx];
    return Vector2(e.rumble[2], e.rumble[3]);
}

float GameInput::device_get_vibration_duration(int64_t id) {
    int idx = _find_index_by_id(id);
    return idx < 0 ? 0.0f : (float)m_devices[idx].rumble_duration;
}

float GameInput::device_get_vibration_remaining_duration(int64_t id) {
    int idx = _find_index_by_id(id);
    if (idx < 0) return 0.0f;
    const DeviceEntry &e = m_devices[idx];
    if (!e.rumble_active) return 0.0f;
    if (e.rumble_end_usec == 0) return -1.0f;
    const uint64_t now = _now_usec();
    if (now >= e.rumble_end_usec) return 0.0f;
    return (float)((double)(e.rumble_end_usec - now) / 1000000.0);
}

// --- Force feedback ------------------------------------------------------------------

int GameInput::device_get_ffb_motor_count(int64_t id) {
    int idx = _find_index_by_id(id);
    return idx < 0 ? 0 : (int)m_devices[idx].caps.ffb_motor_axes.size();
}

Dictionary GameInput::device_get_ffb_motor_info(int64_t id, int motor_index) {
    int idx = _find_index_by_id(id);
    if (idx < 0) return Dictionary();
    const DeviceCaps &c = m_devices[idx].caps;
    if (motor_index < 0 || motor_index >= (int)c.ffb_motor_axes.size()) return Dictionary();
    return motor_info_to_dict(c.ffb_motor_axes[motor_index], c.ffb_motor_effects[motor_index]);
}

bool GameInput::device_is_ffb_motor_powered_on(int64_t id, int motor_index) {
    int idx = _find_index_by_id(id);
    if (idx < 0) return false;
    const DeviceEntry &e = m_devices[idx];
    if (motor_index < 0 || motor_index >= (int)e.caps.ffb_motor_axes.size()) return false;
    if (e.native) {
        return e.native->IsForceFeedbackMotorPoweredOn((uint32_t)motor_index);
    }
    return dict_int(e.mock_info, "ffb_powered", 1) != 0;
}

bool GameInput::device_set_ffb_motor_gain(int64_t id, int motor_index, float gain) {
    int idx = _find_index_by_id(id);
    if (idx < 0) return false;
    const DeviceEntry &e = m_devices[idx];
    if (motor_index < 0 || motor_index >= (int)e.caps.ffb_motor_axes.size()) return false;
    if (e.native) {
        e.native->SetForceFeedbackMotorGain((uint32_t)motor_index, clamp01(gain));
    }
    return true;
}

Ref<GameInputForceFeedbackEffect> GameInput::device_create_ffb_effect(int64_t id, int motor_index,
                                                                      const Dictionary &params) {
    Ref<GameInputForceFeedbackEffect> out;
    int idx = _find_index_by_id(id);
    if (idx < 0) return out;
    DeviceEntry &e = m_devices.write[idx];
    const DeviceCaps &c = e.caps;
    if (motor_index < 0 || motor_index >= (int)c.ffb_motor_axes.size()) {
        UtilityFunctions::push_warning(
            "GameInput: create_force_feedback_effect(): motor_index ", motor_index,
            " is out of range (the device has ", (int64_t)c.ffb_motor_axes.size(),
            " force feedback motors).");
        return out;
    }
    GameInputForceFeedbackParams native_params;
    int kind = -1;
    String err;
    if (!parse_ffb_params(params, c.ffb_motor_axes[motor_index], -1, nullptr, native_params, kind,
                          err)) {
        UtilityFunctions::push_warning("GameInput: create_force_feedback_effect(): ", err);
        return out;
    }
    if (!(c.ffb_motor_effects[motor_index] & (1u << kind))) {
        UtilityFunctions::push_warning("GameInput: create_force_feedback_effect(): motor ",
                                       motor_index, " does not support effect kind ", kind, ".");
        return out;
    }

    EffectEntry fx;
    fx.id = m_next_effect_id.fetch_add(1);
    fx.device_id = id;
    fx.motor_index = (uint32_t)motor_index;
    fx.kind = kind;
    fx.params = native_params;
    if (e.native) {
        IGameInputForceFeedbackEffect *native_fx = nullptr;
        HRESULT hr = e.native->CreateForceFeedbackEffect((uint32_t)motor_index, &native_params,
                                                         &native_fx);
        if (FAILED(hr) || !native_fx) {
            UtilityFunctions::push_warning("GameInput: CreateForceFeedbackEffect failed (hr=",
                                           hresult_to_string(hr), ")");
            return out;
        }
        fx.native = native_fx;
    }
    m_effects.insert(fx.id, fx);
    out.instantiate();
    out->_set_effect_id(fx.id);
    return out;
}

GameInput::EffectEntry *GameInput::_find_effect(int64_t effect_id) {
    return m_effects.getptr(effect_id);
}

void GameInput::_release_effects_for_device(int64_t device_id, bool stop_first) {
    LocalVector<int64_t> ids;
    for (const KeyValue<int64_t, EffectEntry> &kv : m_effects) {
        if (kv.value.device_id == device_id) {
            ids.push_back(kv.key);
        }
    }
    for (uint32_t i = 0; i < ids.size(); ++i) {
        EffectEntry *fx = m_effects.getptr(ids[i]);
        if (!fx) continue;
        if (fx->native) {
            if (stop_first) {
                fx->native->SetState(GameInputFeedbackStopped);
            }
            fx->native->Release();
            fx->native = nullptr;
        }
        m_effects.erase(ids[i]);
    }
}

void GameInput::_release_all_effects() {
    for (KeyValue<int64_t, EffectEntry> &kv : m_effects) {
        if (kv.value.native) {
            kv.value.native->SetState(GameInputFeedbackStopped);
            kv.value.native->Release();
            kv.value.native = nullptr;
        }
    }
    m_effects.clear();
}

bool GameInput::effect_is_valid(int64_t effect_id) {
    return _find_effect(effect_id) != nullptr;
}

int64_t GameInput::effect_get_device_id(int64_t effect_id) {
    EffectEntry *fx = _find_effect(effect_id);
    return fx ? fx->device_id : 0;
}

int GameInput::effect_get_motor_index(int64_t effect_id) {
    EffectEntry *fx = _find_effect(effect_id);
    return fx ? (int)fx->motor_index : -1;
}

int GameInput::effect_get_kind(int64_t effect_id) {
    EffectEntry *fx = _find_effect(effect_id);
    return fx ? fx->kind : -1;
}

int GameInput::effect_get_state(int64_t effect_id) {
    EffectEntry *fx = _find_effect(effect_id);
    if (!fx) return GameInputForceFeedbackEffect::STATE_STOPPED;
    return fx->native ? (int)fx->native->GetState() : fx->mock_state;
}

bool GameInput::effect_set_state(int64_t effect_id, int state) {
    EffectEntry *fx = _find_effect(effect_id);
    if (!fx) return false;
    if (state < GameInputForceFeedbackEffect::STATE_STOPPED ||
            state > GameInputForceFeedbackEffect::STATE_PAUSED) {
        UtilityFunctions::push_warning("GameInput: force feedback effect state ", state,
                                       " is not an EffectState value.");
        return false;
    }
    if (fx->native) {
        fx->native->SetState((GameInputFeedbackEffectState)state);
    } else {
        fx->mock_state = state;
    }
    return true;
}

float GameInput::effect_get_gain(int64_t effect_id) {
    EffectEntry *fx = _find_effect(effect_id);
    if (!fx) return 0.0f;
    return fx->native ? fx->native->GetGain() : fx->mock_gain;
}

bool GameInput::effect_set_gain(int64_t effect_id, float gain) {
    EffectEntry *fx = _find_effect(effect_id);
    if (!fx) return false;
    const float g = clamp01(gain);
    if (fx->native) {
        fx->native->SetGain(g);
    } else {
        fx->mock_gain = g;
    }
    return true;
}

Dictionary GameInput::effect_get_params(int64_t effect_id) {
    EffectEntry *fx = _find_effect(effect_id);
    if (!fx) return Dictionary();
    GameInputForceFeedbackParams p = fx->params;
    if (fx->native) {
        fx->native->GetParams(&p);
    }
    return serialize_ffb_params(p);
}

bool GameInput::effect_set_params(int64_t effect_id, const Dictionary &params) {
    EffectEntry *fx = _find_effect(effect_id);
    if (!fx) return false;
    uint32_t axes = 0;
    int idx = _find_index_by_id(fx->device_id);
    if (idx >= 0 && fx->motor_index < m_devices[idx].caps.ffb_motor_axes.size()) {
        axes = m_devices[idx].caps.ffb_motor_axes[fx->motor_index];
    }
    GameInputForceFeedbackParams p;
    int kind = fx->kind;
    String err;
    if (!parse_ffb_params(params, axes, fx->kind, &fx->params, p, kind, err)) {
        UtilityFunctions::push_warning("GameInput: GameInputForceFeedbackEffect.set_params(): ",
                                       err);
        return false;
    }
    if (fx->native && !fx->native->SetParams(&p)) {
        UtilityFunctions::push_warning(
            "GameInput: GameInputForceFeedbackEffect.set_params(): the device rejected the "
            "parameters.");
        return false;
    }
    fx->params = p;
    return true;
}

void GameInput::effect_release(int64_t effect_id) {
    EffectEntry *fx = _find_effect(effect_id);
    if (!fx) return;
    if (fx->native) {
        fx->native->SetState(GameInputFeedbackStopped);
        fx->native->Release();
        fx->native = nullptr;
    }
    m_effects.erase(effect_id);
}

// --- Debug-only mock runtime -------------------------------------------------------

#ifndef NDEBUG

namespace {

const char *kMockInfoKeys[] = {
    "kind_mask", "name", "vendor_id", "product_id", "revision", "family", "status",
    "rumble_motors", "system_buttons", "sensors", "keyboard_layout", "keyboard_kind",
    "key_count", "max_simultaneous_keys", "mouse_buttons", "sample_rate", "has_wheel_x",
    "has_wheel_y", "gamepad_buttons", "has_clutch", "has_handbrake", "has_pattern_shifter",
    "min_pattern_shifter_gear", "max_pattern_shifter_gear", "max_wheel_angle", "labels",
    "ffb_motors", "ffb_powered", "haptic_locations", "haptic_audio_endpoint_id",
    "app_local_id", "controller_axis_count", "controller_button_count",
    "controller_switch_count",
};

bool mock_info_value_is_valid(const String &key, const Variant &v, String &err) {
    if (key == "name" || key == "haptic_audio_endpoint_id" || key == "app_local_id") {
        if (!variant_is_string(v)) {
            err = String("mock device key '") + key + "' must be a String";
            return false;
        }
        return true;
    }
    if (key == "labels") {
        if (v.get_type() != Variant::DICTIONARY) {
            err = "mock device key 'labels' must be a Dictionary of Source -> label int";
            return false;
        }
        Dictionary labels = v;
        Array lk = labels.keys();
        for (int i = 0; i < lk.size(); ++i) {
            if (lk[i].get_type() != Variant::INT || labels[lk[i]].get_type() != Variant::INT) {
                err = "mock device 'labels' must map Source ints to label ints";
                return false;
            }
        }
        return true;
    }
    if (key == "ffb_motors") {
        if (v.get_type() != Variant::ARRAY) {
            err = "mock device key 'ffb_motors' must be an Array of Dictionaries";
            return false;
        }
        Array motors = v;
        for (int i = 0; i < motors.size(); ++i) {
            if (motors[i].get_type() != Variant::DICTIONARY) {
                err = "mock device 'ffb_motors' entries must be Dictionaries";
                return false;
            }
            Dictionary m = motors[i];
            Array mk = m.keys();
            for (int j = 0; j < mk.size(); ++j) {
                String name = variant_is_string(mk[j]) ? String(mk[j]) : String();
                if ((name != "axes" && name != "effects") || m[mk[j]].get_type() != Variant::INT) {
                    err = "mock device 'ffb_motors' entries accept only int 'axes' and 'effects'";
                    return false;
                }
            }
        }
        return true;
    }
    if (key == "haptic_locations") {
        Array names = variant_to_array(v);
        if (v.get_type() != Variant::ARRAY && v.get_type() != Variant::PACKED_STRING_ARRAY) {
            err = "mock device key 'haptic_locations' must be an Array of Strings";
            return false;
        }
        for (int i = 0; i < names.size(); ++i) {
            if (!variant_is_string(names[i])) {
                err = "mock device key 'haptic_locations' must be an Array of Strings";
                return false;
            }
        }
        return true;
    }
    if (!variant_is_number(v) && v.get_type() != Variant::BOOL) {
        err = String("mock device key '") + key + "' must be a number or bool";
        return false;
    }
    return true;
}

bool mock_info_is_valid(const Dictionary &info, String &err) {
    Array keys = info.keys();
    for (int i = 0; i < keys.size(); ++i) {
        if (!variant_is_string(keys[i])) {
            err = "mock device keys must be Strings";
            return false;
        }
        String key = keys[i];
        bool known = false;
        for (const char *k : kMockInfoKeys) {
            if (key == k) {
                known = true;
                break;
            }
        }
        if (!known) {
            err = String("unknown mock device key '") + key + "'";
            return false;
        }
        if (!mock_info_value_is_valid(key, info[keys[i]], err)) {
            return false;
        }
    }
    return true;
}

} // namespace

bool GameInput::_test_initialize_mock() {
    if (m_initialized || m_game_input || m_backend != Backend::None) {
        shutdown();
    }
    m_backend = Backend::Mock;
    m_generation++;
    _load_settings();
    m_accepting_callbacks.store(true, std::memory_order_release);
    m_initialized = true;
    m_warned_uninitialized = false;
    m_dropped_reading_count.store(0, std::memory_order_relaxed);
    _apply_reading_callback_registration();
    UtilityFunctions::print("GameInput: initialized (mock backend)");
    return true;
}

int GameInput::_test_get_backend() const {
    switch (m_backend) {
        case Backend::Native: return 1;
        case Backend::Mock: return 2;
        default: return 0;
    }
}

int64_t GameInput::_test_inject_device(const Dictionary &info) {
    if (!m_initialized || m_backend != Backend::Mock) {
        UtilityFunctions::push_error(
            "GameInput: _test_inject_device() needs _test_initialize_mock() first.");
        return 0;
    }
    String err;
    if (!mock_info_is_valid(info, err)) {
        UtilityFunctions::push_error("GameInput: _test_inject_device(): ", err);
        return 0;
    }
    const int64_t id = m_next_device_id.fetch_add(1);
    m_mock_pending_infos.insert(id, info.duplicate(true));
    PendingEvent ev;
    ev.kind = PendingEventKind::DeviceStatus;
    ev.mock_device_id = id;
    ev.timestamp = _now_usec();
    ev.current = (uint32_t)dict_int(info, "status", (int64_t)GameInputDeviceConnected) |
                 (uint32_t)GameInputDeviceConnected;
    ev.previous = 0;
    if (!_queue_event(ev)) {
        m_mock_pending_infos.erase(id);
        return 0;
    }
    return id;
}

bool GameInput::_test_remove_device(int64_t device_id) {
    if (!m_initialized || m_backend != Backend::Mock) return false;
    if (m_mock_pending_infos.has(device_id)) {
        // The queued connect finds no pending info and is ignored.
        m_mock_pending_infos.erase(device_id);
        return true;
    }
    int idx = _find_index_by_id(device_id);
    if (idx < 0) return false;
    PendingEvent ev;
    ev.kind = PendingEventKind::DeviceStatus;
    ev.mock_device_id = device_id;
    ev.timestamp = _now_usec();
    ev.current = 0;
    ev.previous = m_devices[idx].status;
    return _queue_event(ev);
}

bool GameInput::_test_set_device_status(int64_t device_id, int status) {
    if (!m_initialized || m_backend != Backend::Mock) return false;
    int idx = _find_index_by_id(device_id);
    if (idx < 0) return false;
    PendingEvent ev;
    ev.kind = PendingEventKind::DeviceStatus;
    ev.mock_device_id = device_id;
    ev.timestamp = _now_usec();
    ev.current = (uint32_t)status;
    ev.previous = m_devices[idx].status;
    return _queue_event(ev);
}

bool GameInput::_test_push_reading(int64_t device_id, const Dictionary &state) {
    if (!m_initialized || m_backend != Backend::Mock) return false;
    int idx = _find_index_by_id(device_id);
    if (idx < 0) {
        UtilityFunctions::push_error("GameInput: _test_push_reading(): device ", device_id,
                                     " is not connected (poll() after _test_inject_device()).");
        return false;
    }
    DeviceEntry &e = m_devices.write[idx];
    const uint64_t ts = state.has("timestamp") ? (uint64_t)dict_int(state, "timestamp", 0)
                                               : _now_usec();
    gi::Snapshot snap;
    String err;
    const uint32_t supported = snap_kinds_from_native(e.caps.native_supported_input);
    if (!mock_snapshot_from_dict(state, supported, ts, snap, err)) {
        UtilityFunctions::push_error("GameInput: _test_push_reading(): ", err);
        return false;
    }
    if (snap.kinds == 0) {
        return false; // nothing this device can report
    }
    gi::merge_kinds(e.mock_state, snap);
    if (m_accepting_readings.load(std::memory_order_acquire) && (snap.kinds & m_reading_snap_kinds)) {
        ReadingEvent ev;
        ev.mock_device_id = device_id;
        gi::merge_kinds(ev.snapshot, snap, m_reading_snap_kinds);
        _queue_reading(ev);
    }
    return true;
}

bool GameInput::_test_push_system_buttons(int64_t device_id, int buttons) {
    if (!m_initialized || m_backend != Backend::Mock) return false;
    int idx = _find_index_by_id(device_id);
    if (idx < 0) return false;
    PendingEvent ev;
    ev.kind = PendingEventKind::SystemButtons;
    ev.mock_device_id = device_id;
    ev.timestamp = _now_usec();
    ev.current = (uint32_t)buttons & 0x3u;
    ev.previous = m_devices[idx].system_buttons;
    return _queue_event(ev);
}

bool GameInput::_test_push_keyboard_layout(int64_t device_id, int layout) {
    if (!m_initialized || m_backend != Backend::Mock) return false;
    int idx = _find_index_by_id(device_id);
    if (idx < 0) return false;
    PendingEvent ev;
    ev.kind = PendingEventKind::KeyboardLayout;
    ev.mock_device_id = device_id;
    ev.timestamp = _now_usec();
    ev.current = (uint32_t)layout;
    ev.previous = m_devices[idx].keyboard_layout;
    return _queue_event(ev);
}

Dictionary GameInput::_test_get_last_rumble(int64_t device_id) {
    Dictionary d;
    int idx = _find_index_by_id(device_id);
    if (idx < 0) return d;
    const DeviceEntry &e = m_devices[idx];
    d["low"] = e.rumble[0];
    d["high"] = e.rumble[1];
    d["left_trigger"] = e.rumble[2];
    d["right_trigger"] = e.rumble[3];
    d["active"] = e.rumble_active;
    d["apply_count"] = (int64_t)e.rumble_apply_count;
    d["end_usec"] = (int64_t)e.rumble_end_usec;
    return d;
}

void GameInput::_test_set_time_override_usec(int64_t usec) {
    m_time_override_usec = usec;
}

int GameInput::_test_get_effect_count() const {
    return (int)m_effects.size();
}

void GameInput::_test_force_poll() {
    m_last_polled_frame = UINT64_MAX;
    poll();
}

#endif // NDEBUG

} // namespace godot
