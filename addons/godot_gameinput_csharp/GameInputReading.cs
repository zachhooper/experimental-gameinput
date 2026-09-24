using System;
using System.Collections.Generic;
using Godot;
using GodotGameInput.Internal;

namespace GodotGameInput;

/// <summary>
/// Immutable snapshot of one device's input, mirroring the native
/// <c>GameInputReading</c>. Covers every input kind the device supports
/// (gamepad, keyboard, mouse, sensors, arcade stick, flight stick, racing wheel
/// and raw controller) and holds the state it replaced, so edge queries return
/// correct transitions. Methods for a kind the reading does not contain return
/// false, 0 or an empty value; check <see cref="InputKinds"/>.
/// </summary>
public sealed class GameInputReading : GameInputObject
{
    internal GameInputReading(GodotObject o) : base(o)
    {
    }

    public static GameInputReading From(GodotObject o) => o == null ? null : new GameInputReading(o);

    // --- Gamepad ---

    /// <summary>True when every bit of <paramref name="button"/> is held (a chord for several flags).</summary>
    public bool IsButtonDown(GameInputDevice.Button button) => Call("is_button_down", (int)button).AsBool();

    public bool IsButtonDown(int buttonMask) => Call("is_button_down", buttonMask).AsBool();

    public bool WasButtonPressed(GameInputDevice.Button button) =>
        Call("was_button_pressed", (int)button).AsBool();

    public bool WasButtonReleased(GameInputDevice.Button button) =>
        Call("was_button_released", (int)button).AsBool();

    /// <summary>Axis value; thumbstick Y is down-positive like Godot's joypad axes.</summary>
    public float GetAxis(GameInputDevice.Axis axis) => (float)Call("get_axis", (int)axis).AsDouble();

    public int GetButtonsMask() => Call("get_buttons_mask").AsInt32();

    // --- Metadata ---

    /// <summary>Microseconds on the GameInput clock; compare with <see cref="GameInput.CurrentTimestamp"/>.</summary>
    public long GetTimestamp() => Call("get_timestamp").AsInt64();

    public long DeviceId => Call("get_device_id").AsInt64();

    /// <summary>Timestamp of the part of the reading holding <paramref name="kind"/> (newest for a mask).</summary>
    public long GetKindTimestamp(GameInput.DeviceKind kind) => Call("get_kind_timestamp", (int)kind).AsInt64();

    /// <summary>The <see cref="GameInput.DeviceKind"/> flags of the input this reading contains.</summary>
    public GameInput.DeviceKind InputKinds => (GameInput.DeviceKind)Call("get_input_kinds").AsInt32();

    /// <summary>False for the first reading after a device connects: release edges need a previous state.</summary>
    public bool HasPrevious => Call("has_previous").AsBool();

    /// <summary>True when event-driven readings of this device were dropped just before this one.</summary>
    public bool HasGapBefore => Call("has_gap_before").AsBool();

    /// <summary>True when the device reported more keys, axes, buttons or switches than a reading holds.</summary>
    public bool IsTruncated => Call("is_truncated").AsBool();

    // --- Unified sources (any device kind) ---

    public bool IsSourceDown(GameInputDevice.Source source) => Call("is_source_down", (int)source).AsBool();

    public bool WasSourcePressed(GameInputDevice.Source source) =>
        Call("was_source_pressed", (int)source).AsBool();

    public bool WasSourceReleased(GameInputDevice.Source source) =>
        Call("was_source_released", (int)source).AsBool();

    /// <summary>Axis value for axis sources; 1 or 0 for button sources.</summary>
    public float GetSourceValue(GameInputDevice.Source source) =>
        (float)Call("get_source_value", (int)source).AsDouble();

    // --- Keyboard ---

    public int KeyCount => Call("get_key_count").AsInt32();

    /// <summary>Physical (position-based) <see cref="Key"/> values of the held keys, like <c>InputEventKey.PhysicalKeycode</c>.</summary>
    public Key[] GetPressedPhysicalKeys() =>
        Array.ConvertAll(Call("get_pressed_physical_keys").AsInt64Array(), k => (Key)k);

    public bool IsPhysicalKeyDown(Key physicalKey) => Call("is_physical_key_down", (long)physicalKey).AsBool();

    public bool WasPhysicalKeyPressed(Key physicalKey) =>
        Call("was_physical_key_pressed", (long)physicalKey).AsBool();

    public bool WasPhysicalKeyReleased(Key physicalKey) =>
        Call("was_physical_key_released", (long)physicalKey).AsBool();

    /// <summary>
    /// One dictionary per held key: <c>scan_code</c>, <c>virtual_key</c>,
    /// <c>code_point</c>, <c>is_dead_key</c>, <c>physical_keycode</c>,
    /// <c>keycode</c> and <c>location</c>.
    /// </summary>
    public IReadOnlyList<Godot.Collections.Dictionary> GetKeyStates()
    {
        var result = new List<Godot.Collections.Dictionary>();
        foreach (Variant state in Call("get_key_states").AsGodotArray())
        {
            result.Add(state.AsGodotDictionary());
        }

        return result;
    }

    // --- Mouse ---

    public GameInputDevice.MouseButton MouseButtons =>
        (GameInputDevice.MouseButton)Call("get_mouse_buttons").AsInt32();

    public bool IsMouseButtonDown(GameInputDevice.MouseButton button) =>
        Call("is_mouse_button_down", (int)button).AsBool();

    public bool WasMouseButtonPressed(GameInputDevice.MouseButton button) =>
        Call("was_mouse_button_pressed", (int)button).AsBool();

    public bool WasMouseButtonReleased(GameInputDevice.MouseButton button) =>
        Call("was_mouse_button_released", (int)button).AsBool();

    /// <summary>Mouse motion since the previous state, in device units (not screen pixels).</summary>
    public Vector2 MouseDelta => Call("get_mouse_delta").AsVector2();

    /// <summary>Horizontal (x) and vertical (y) wheel motion since the previous state.</summary>
    public Vector2 MouseWheelDelta => Call("get_mouse_wheel_delta").AsVector2();

    public bool HasMouseAbsolutePosition => Call("has_mouse_absolute_position").AsBool();

    public Vector2 MouseAbsolutePosition => Call("get_mouse_absolute_position").AsVector2();

    /// <summary>Raw accumulated mouse state (64-bit wrapping counters); prefer <see cref="MouseDelta"/>.</summary>
    public Godot.Collections.Dictionary GetMouseState() => Call("get_mouse_state").AsGodotDictionary();

    // --- Motion sensors ---

    public GameInputDevice.SensorKind SensorKinds => (GameInputDevice.SensorKind)Call("get_sensor_kinds").AsInt32();

    /// <summary>Acceleration in metres per second squared.</summary>
    public Vector3 Accelerometer => Call("get_accelerometer").AsVector3();

    /// <summary>Angular velocity in radians per second.</summary>
    public Vector3 Gyroscope => Call("get_gyroscope").AsVector3();

    public float HeadingDegrees => (float)Call("get_heading_degrees").AsDouble();

    public GameInputDevice.SensorAccuracy HeadingAccuracy =>
        (GameInputDevice.SensorAccuracy)Call("get_heading_accuracy").AsInt32();

    public Quaternion Orientation => Call("get_orientation").AsQuaternion();

    // --- Arcade stick, flight stick, racing wheel ---

    public GameInputDevice.ArcadeStickButton ArcadeStickButtons =>
        (GameInputDevice.ArcadeStickButton)Call("get_arcade_stick_buttons").AsInt32();

    public GameInputDevice.FlightStickButton FlightStickButtons =>
        (GameInputDevice.FlightStickButton)Call("get_flight_stick_buttons").AsInt32();

    public GameInputDevice.SwitchPosition FlightStickHat =>
        (GameInputDevice.SwitchPosition)Call("get_flight_stick_hat").AsInt32();

    public Vector2 FlightStickHatVector => Call("get_flight_stick_hat_vector").AsVector2();

    public GameInputDevice.RacingWheelButton RacingWheelButtons =>
        (GameInputDevice.RacingWheelButton)Call("get_racing_wheel_buttons").AsInt32();

    public int RacingWheelGear => Call("get_racing_wheel_gear").AsInt32();

    // --- Raw controller ---

    /// <summary>Every raw controller axis, normalized by GameInput to [0, 1]; the layout is device-specific.</summary>
    public float[] GetControllerAxes() => Call("get_controller_axes").AsFloat32Array();

    public bool[] GetControllerButtons()
    {
        Godot.Collections.Array buttons = Call("get_controller_buttons").AsGodotArray();
        var result = new bool[buttons.Count];
        for (int i = 0; i < result.Length; i++)
        {
            result[i] = buttons[i].AsBool();
        }

        return result;
    }

    public GameInputDevice.SwitchPosition[] GetControllerSwitches() =>
        Array.ConvertAll(Call("get_controller_switches").AsInt32Array(), s => (GameInputDevice.SwitchPosition)s);

    public float GetControllerAxis(int index) => (float)Call("get_controller_axis", index).AsDouble();

    public bool IsControllerButtonDown(int index) => Call("is_controller_button_down", index).AsBool();

    public bool WasControllerButtonPressed(int index) => Call("was_controller_button_pressed", index).AsBool();

    public bool WasControllerButtonReleased(int index) => Call("was_controller_button_released", index).AsBool();

    public GameInputDevice.SwitchPosition GetControllerSwitch(int index) =>
        (GameInputDevice.SwitchPosition)Call("get_controller_switch", index).AsInt32();
}
