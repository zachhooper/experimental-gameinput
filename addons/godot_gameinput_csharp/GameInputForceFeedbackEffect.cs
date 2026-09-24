using System;
using Godot;
using GodotGameInput.Internal;

namespace GodotGameInput;

/// <summary>
/// A force-feedback effect playing on one motor of a <see cref="GameInputDevice"/>,
/// mirroring the native <c>GameInputForceFeedbackEffect</c>. Create it with
/// <see cref="GameInputDevice.CreateForceFeedbackEffect"/>. The native effect
/// is stopped and released when its last reference goes away; a managed wrapper
/// holds that reference until it is garbage collected, so call
/// <see cref="Dispose"/> (or <see cref="Release"/>) when the effect should end.
/// </summary>
public sealed class GameInputForceFeedbackEffect : GameInputObject, IDisposable
{
    internal GameInputForceFeedbackEffect(GodotObject o) : base(o)
    {
    }

    public static GameInputForceFeedbackEffect From(GodotObject o) =>
        o == null ? null : new GameInputForceFeedbackEffect(o);

    /// <summary>True while the effect exists: not released and its device still connected.</summary>
    public bool IsValid => IsLive && Call("is_valid").AsBool();

    /// <summary>Id of the device the effect plays on, or 0 once invalid.</summary>
    public long DeviceId => Call("get_device_id").AsInt64();

    /// <summary>Index of the motor the effect plays on, or -1 once invalid.</summary>
    public int MotorIndex => Call("get_motor_index").AsInt32();

    /// <summary>The effect kind; <c>(EffectKind)(-1)</c> once the effect is no longer valid.</summary>
    public EffectKind Kind => (EffectKind)Call("get_kind").AsInt32();

    /// <summary>Starts or resumes the effect.</summary>
    public bool Start() => Call("start").AsBool();

    /// <summary>Stops the effect; starting it again plays from the beginning.</summary>
    public bool Stop() => Call("stop").AsBool();

    /// <summary>Pauses the effect; starting it again resumes where it paused.</summary>
    public bool Pause() => Call("pause").AsBool();

    public EffectState State => (EffectState)Call("get_state").AsInt32();

    public bool SetState(EffectState state) => Call("set_state", (int)state).AsBool();

    /// <summary>Gain that scales the effect's magnitudes, in [0, 1].</summary>
    public float Gain => (float)Call("get_gain").AsDouble();

    public bool SetGain(float gain) => Call("set_gain", gain).AsBool();

    /// <summary>The parameters in the form <see cref="SetParams"/> accepts; durations in seconds.</summary>
    public Godot.Collections.Dictionary GetParams() => Call("get_params").AsGodotDictionary();

    /// <summary>Changes parameters, including while playing. Missing keys keep their current values.</summary>
    public bool SetParams(Godot.Collections.Dictionary parameters) => Call("set_params", parameters).AsBool();

    /// <summary>Stops and releases the native effect now. Safe to call more than once.</summary>
    public void Release()
    {
        if (IsLive)
        {
            Call("release");
        }
    }

    /// <summary>Releases the native effect and drops this wrapper's reference to it.</summary>
    public void Dispose()
    {
        Release();
        if (IsLive)
        {
            _o.Dispose();
        }
    }

    /// <summary>Effect kinds, matching native <c>GameInputForceFeedbackEffect.EffectKind</c>.</summary>
    public enum EffectKind
    {
        Constant = 0,
        Ramp = 1,
        SineWave = 2,
        SquareWave = 3,
        TriangleWave = 4,
        SawtoothUp = 5,
        SawtoothDown = 6,
        Spring = 7,
        Friction = 8,
        Damper = 9,
        Inertia = 10,
    }

    /// <summary>Playback states, matching native <c>GameInputForceFeedbackEffect.EffectState</c>.</summary>
    public enum EffectState
    {
        Stopped = 0,
        Running = 1,
        Paused = 2,
    }

    /// <summary>Motor axis flags, matching native <c>GameInputForceFeedbackEffect.FeedbackAxis</c>.</summary>
    [Flags]
    public enum FeedbackAxis
    {
        None = 0,
        LinearX = 1,
        LinearY = 2,
        LinearZ = 4,
        AngularX = 8,
        AngularY = 16,
        AngularZ = 32,
        Normal = 64,
    }
}
