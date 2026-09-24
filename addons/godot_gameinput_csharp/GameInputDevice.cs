using System;
using Godot;
using GodotGameInput.Internal;

namespace GodotGameInput;

/// <summary>
/// Weak handle to a single GameInput device, mirroring the native
/// <c>GameInputDevice</c>. Stores only the session-local device id; all methods
/// resolve through the <see cref="GameInput"/> singleton and soft-fail with safe
/// defaults if the device has disconnected.
/// </summary>
public sealed class GameInputDevice : GameInputObject
{
    private const string NativeClass = "GameInputDevice";

    internal GameInputDevice(GodotObject o) : base(o)
    {
    }

    public static GameInputDevice From(GodotObject o) => o == null ? null : new GameInputDevice(o);

    public long DeviceId => Call("get_device_id").AsInt64();
    public string DisplayName => Call("get_display_name").AsString();
    public int KindMask => Call("get_kind_mask").AsInt32();
    public bool IsConnected => Call("is_connected").AsBool();
    public bool SupportsVibration => Call("supports_vibration").AsBool();
    public bool SupportsHaptics => Call("supports_haptics").AsBool();
    public Godot.Collections.Dictionary GetDeviceInfo() => Call("get_device_info").AsGodotDictionary();

    /// <summary>Translates a <see cref="Button"/> flag into a <see cref="Source"/> value.</summary>
    public int ButtonToSource(Button button) => Call("button_to_source", (int)button).AsInt32();

    /// <summary>Translates an <see cref="Axis"/> index into a <see cref="Source"/> value.</summary>
    public int AxisToSource(Axis axis) => Call("axis_to_source", (int)axis).AsInt32();

    // --- Status and identity ---

    /// <summary>Current <see cref="DeviceStatus"/> flags; <see cref="DeviceStatus.None"/> once disconnected.</summary>
    public DeviceStatus Status => (DeviceStatus)Call("get_status").AsInt64();

    /// <summary>
    /// The app-local device id as 64 hex characters. Unlike <see cref="DeviceId"/>
    /// it survives app restarts and reboots while the device stays on the same USB
    /// port, so it can key saved per-device settings.
    /// </summary>
    public string AppLocalId => Call("get_app_local_id").AsString();

    public DeviceFamily Family => (DeviceFamily)Call("get_device_family").AsInt32();

    public RumbleMotor SupportedRumbleMotors => (RumbleMotor)Call("get_supported_rumble_motors").AsInt32();

    public SystemButton SupportedSystemButtons => (SystemButton)Call("get_supported_system_buttons").AsInt32();

    /// <summary>System buttons (Guide, Share) currently held; see <see cref="GameInput.SystemButtonsChanged"/>.</summary>
    public SystemButton SystemButtons => (SystemButton)Call("get_system_buttons").AsInt32();

    /// <summary>Platform identifier of the keyboard's current layout, or 0 for non-keyboards.</summary>
    public long KeyboardLayout => Call("get_keyboard_layout").AsInt64();

    /// <summary>
    /// The label GameInput reports for the button behind <paramref name="source"/>,
    /// as a snake_case name such as <c>"xbox_a"</c>, or an empty string when the
    /// device has no label for it.
    /// </summary>
    public string GetButtonLabel(Source source) => Call("get_button_label", (int)source).AsString();

    public Godot.Collections.Dictionary GetHapticInfo() => Call("get_haptic_info").AsGodotDictionary();

    // --- Vibration (mirrors Godot's Input.start_joy_vibration) ---

    /// <summary>
    /// Starts rumble: <paramref name="weakMagnitude"/> drives the high-frequency
    /// motor and <paramref name="strongMagnitude"/> the low-frequency motor, like
    /// <c>Input.StartJoyVibration</c>. A <paramref name="duration"/> of 0 rumbles
    /// until <see cref="StopVibration"/>; otherwise <see cref="GameInput.Poll"/>
    /// stops it after that many seconds.
    /// </summary>
    public bool StartVibration(float weakMagnitude, float strongMagnitude, float duration = 0.0f,
        float leftTrigger = 0.0f, float rightTrigger = 0.0f) =>
        Call("start_vibration", weakMagnitude, strongMagnitude, duration, leftTrigger, rightTrigger).AsBool();

    public void StopVibration() => Call("stop_vibration");

    public bool IsVibrating => Call("is_vibrating").AsBool();

    /// <summary>Current rumble as (weak, strong), matching <c>Input.GetJoyVibrationStrength</c>.</summary>
    public Vector2 VibrationStrength => Call("get_vibration_strength").AsVector2();

    /// <summary>Current trigger rumble as (left, right).</summary>
    public Vector2 TriggerVibrationStrength => Call("get_trigger_vibration_strength").AsVector2();

    /// <summary>
    /// Duration, in seconds, passed to the <see cref="StartVibration"/> call that is
    /// playing; 0 for untimed rumble and when the device is not vibrating.
    /// </summary>
    public float VibrationDuration => (float)Call("get_vibration_duration").AsDouble();

    /// <summary>Seconds left before a timed vibration stops; -1 while vibrating without a duration, 0 when not vibrating.</summary>
    public float VibrationRemainingDuration => (float)Call("get_vibration_remaining_duration").AsDouble();

    // --- Force feedback ---

    public int ForceFeedbackMotorCount => Call("get_force_feedback_motor_count").AsInt32();

    /// <summary>
    /// <c>supported_axes</c> (<see cref="GameInputForceFeedbackEffect.FeedbackAxis"/> flags) and
    /// <c>supported_effects</c> (<see cref="GameInputForceFeedbackEffect.EffectKind"/> values) of one motor.
    /// </summary>
    public Godot.Collections.Dictionary GetForceFeedbackMotorInfo(int motorIndex) =>
        Call("get_force_feedback_motor_info", motorIndex).AsGodotDictionary();

    public bool IsForceFeedbackMotorPoweredOn(int motorIndex) =>
        Call("is_force_feedback_motor_powered_on", motorIndex).AsBool();

    public bool SetForceFeedbackMotorGain(int motorIndex, float gain) =>
        Call("set_force_feedback_motor_gain", motorIndex, gain).AsBool();

    /// <summary>
    /// Creates a stopped effect on <paramref name="motorIndex"/> from a parameter
    /// dictionary (see the native <c>GameInputForceFeedbackEffect</c> docs for the
    /// schema). Returns null when the device or GameInput refuses. Keep the
    /// returned effect and dispose it when done: the native effect lives until
    /// the managed wrapper is released.
    /// </summary>
    public GameInputForceFeedbackEffect CreateForceFeedbackEffect(int motorIndex,
        Godot.Collections.Dictionary parameters) =>
        GameInputForceFeedbackEffect.From(
            Call("create_force_feedback_effect", motorIndex, parameters).AsGodotObject());

    // --- Static helpers ---

    /// <summary>Converts a keyboard scan code (extended keys as <c>0xE0xx</c>) to Godot's physical <see cref="Key"/>.</summary>
    public static Key ScanCodeToPhysicalKey(long scanCode) =>
        (Key)ClassDB.ClassCallStatic(NativeClass, "scan_code_to_physical_key", scanCode).AsInt64();

    /// <summary>Converts a Windows virtual-key code (0-255) to Godot's <see cref="Key"/>.</summary>
    public static Key VirtualKeyToKeycode(int virtualKey) =>
        (Key)ClassDB.ClassCallStatic(NativeClass, "virtual_key_to_keycode", virtualKey).AsInt64();

    /// <summary>Converts a hat <see cref="SwitchPosition"/> to a direction in Godot's 2D convention (up is negative Y).</summary>
    public static Vector2 SwitchPositionToVector(SwitchPosition position) =>
        ClassDB.ClassCallStatic(NativeClass, "switch_position_to_vector", (int)position).AsVector2();

    /// <summary>Gamepad button flags (bitfield), matching native <c>GameInputDevice.Button</c>.</summary>
    [Flags]
    public enum Button
    {
        None = 0,
        Menu = 1,
        View = 2,
        A = 4,
        B = 8,
        X = 16,
        Y = 32,
        DpadUp = 64,
        DpadDown = 128,
        DpadLeft = 256,
        DpadRight = 512,
        LeftShoulder = 1024,
        RightShoulder = 2048,
        LeftThumb = 4096,
        RightThumb = 8192,
        C = 16384,
        Z = 32768,
        LeftTrigger = 65536,
        RightTrigger = 131072,
        LeftStickUp = 262144,
        LeftStickDown = 524288,
        LeftStickLeft = 1048576,
        LeftStickRight = 2097152,
        RightStickUp = 4194304,
        RightStickDown = 8388608,
        RightStickLeft = 16777216,
        RightStickRight = 33554432,
        PaddleLeft1 = 67108864,
        PaddleLeft2 = 134217728,
        PaddleRight1 = 268435456,
        PaddleRight2 = 536870912,
    }

    /// <summary>Axis indices, matching native <c>GameInputDevice.Axis</c>.</summary>
    public enum Axis
    {
        LeftX = 0,
        LeftY = 1,
        RightX = 2,
        RightY = 3,
        LeftTrigger = 4,
        RightTrigger = 5,
        Wheel = 6,
        Throttle = 7,
        Brake = 8,
        Clutch = 9,
        Handbrake = 10,
        FlightRoll = 11,
        FlightPitch = 12,
        FlightYaw = 13,
        FlightThrottle = 14,
    }

    /// <summary>
    /// Combined button/axis source namespace used by <see cref="GameInputBinding"/>
    /// and the <see cref="GameInputReading"/> source helpers, matching native
    /// <c>GameInputDevice.Source</c>: gamepad buttons 0-29, axes 100-114, arcade
    /// stick 200-213, flight stick 300-313, racing wheel 400-413.
    /// </summary>
    public enum Source
    {
        BtnMenu = 0,
        BtnView = 1,
        BtnA = 2,
        BtnB = 3,
        BtnX = 4,
        BtnY = 5,
        BtnDpadUp = 6,
        BtnDpadDown = 7,
        BtnDpadLeft = 8,
        BtnDpadRight = 9,
        BtnLeftShoulder = 10,
        BtnRightShoulder = 11,
        BtnLeftThumb = 12,
        BtnRightThumb = 13,
        BtnC = 14,
        BtnZ = 15,
        BtnLeftTrigger = 16,
        BtnRightTrigger = 17,
        BtnLeftStickUp = 18,
        BtnLeftStickDown = 19,
        BtnLeftStickLeft = 20,
        BtnLeftStickRight = 21,
        BtnRightStickUp = 22,
        BtnRightStickDown = 23,
        BtnRightStickLeft = 24,
        BtnRightStickRight = 25,
        BtnPaddleLeft1 = 26,
        BtnPaddleLeft2 = 27,
        BtnPaddleRight1 = 28,
        BtnPaddleRight2 = 29,
        AxisLeftX = 100,
        AxisLeftY = 101,
        AxisRightX = 102,
        AxisRightY = 103,
        AxisLeftTrigger = 104,
        AxisRightTrigger = 105,
        AxisWheel = 106,
        AxisThrottle = 107,
        AxisBrake = 108,
        AxisClutch = 109,
        AxisHandbrake = 110,
        AxisFlightRoll = 111,
        AxisFlightPitch = 112,
        AxisFlightYaw = 113,
        AxisFlightThrottle = 114,
        ArcadeMenu = 200,
        ArcadeView = 201,
        ArcadeUp = 202,
        ArcadeDown = 203,
        ArcadeLeft = 204,
        ArcadeRight = 205,
        ArcadeAction1 = 206,
        ArcadeAction2 = 207,
        ArcadeAction3 = 208,
        ArcadeAction4 = 209,
        ArcadeAction5 = 210,
        ArcadeAction6 = 211,
        ArcadeSpecial1 = 212,
        ArcadeSpecial2 = 213,
        FlightMenu = 300,
        FlightView = 301,
        FlightFirePrimary = 302,
        FlightFireSecondary = 303,
        FlightHatUp = 304,
        FlightHatDown = 305,
        FlightHatLeft = 306,
        FlightHatRight = 307,
        FlightA = 308,
        FlightB = 309,
        FlightX = 310,
        FlightY = 311,
        FlightLeftShoulder = 312,
        FlightRightShoulder = 313,
        WheelMenu = 400,
        WheelView = 401,
        WheelPreviousGear = 402,
        WheelNextGear = 403,
        WheelDpadUp = 404,
        WheelDpadDown = 405,
        WheelDpadLeft = 406,
        WheelDpadRight = 407,
        WheelA = 408,
        WheelB = 409,
        WheelX = 410,
        WheelY = 411,
        WheelLeftThumb = 412,
        WheelRightThumb = 413,
    }

    /// <summary>Arcade stick button flags, matching native <c>GameInputDevice.ArcadeStickButton</c>.</summary>
    [Flags]
    public enum ArcadeStickButton
    {
        None = 0,
        Menu = 1,
        View = 2,
        Up = 4,
        Down = 8,
        Left = 16,
        Right = 32,
        Action1 = 64,
        Action2 = 128,
        Action3 = 256,
        Action4 = 512,
        Action5 = 1024,
        Action6 = 2048,
        Special1 = 4096,
        Special2 = 8192,
    }

    /// <summary>Flight stick button flags, matching native <c>GameInputDevice.FlightStickButton</c>.</summary>
    [Flags]
    public enum FlightStickButton
    {
        None = 0,
        Menu = 1,
        View = 2,
        FirePrimary = 4,
        FireSecondary = 8,
        HatUp = 16,
        HatDown = 32,
        HatLeft = 64,
        HatRight = 128,
        A = 256,
        B = 512,
        X = 1024,
        Y = 2048,
        LeftShoulder = 4096,
        RightShoulder = 8192,
    }

    /// <summary>Racing wheel button flags, matching native <c>GameInputDevice.RacingWheelButton</c>.</summary>
    [Flags]
    public enum RacingWheelButton
    {
        None = 0,
        Menu = 1,
        View = 2,
        PreviousGear = 4,
        NextGear = 8,
        DpadUp = 16,
        DpadDown = 32,
        DpadLeft = 64,
        DpadRight = 128,
        A = 256,
        B = 512,
        X = 1024,
        Y = 2048,
        LeftThumb = 4096,
        RightThumb = 8192,
    }

    /// <summary>Mouse button flags, matching native <c>GameInputDevice.MouseButton</c>.</summary>
    [Flags]
    public enum MouseButton
    {
        None = 0,
        Left = 1,
        Right = 2,
        Middle = 4,
        XButton1 = 8,
        XButton2 = 16,
        WheelTiltLeft = 32,
        WheelTiltRight = 64,
    }

    /// <summary>Sensor flags, matching native <c>GameInputDevice.SensorKind</c>.</summary>
    [Flags]
    public enum SensorKind
    {
        None = 0,
        Accelerometer = 1,
        Gyrometer = 2,
        Compass = 4,
        Orientation = 8,
    }

    /// <summary>Compass heading accuracy, matching native <c>GameInputDevice.SensorAccuracy</c>.</summary>
    public enum SensorAccuracy
    {
        Unknown = 0,
        Unreliable = 1,
        Approximate = 2,
        High = 3,
    }

    /// <summary>Device family, matching native <c>GameInputDevice.DeviceFamily</c>.</summary>
    public enum DeviceFamily
    {
        Virtual = -1,
        Unknown = 0,
        XboxOne = 1,
        Xbox360 = 2,
        Hid = 3,
        I8042 = 4,
        Aggregate = 5,
    }

    /// <summary>Device status flags, matching native <c>GameInputDevice.DeviceStatus</c>.</summary>
    [Flags]
    public enum DeviceStatus
    {
        None = 0,
        Connected = 1,
        HapticInfoReady = 2097152,
    }

    /// <summary>System button flags, matching native <c>GameInputDevice.SystemButton</c>.</summary>
    [Flags]
    public enum SystemButton
    {
        None = 0,
        Guide = 1,
        Share = 2,
    }

    /// <summary>Hat switch positions, matching native <c>GameInputDevice.SwitchPosition</c>.</summary>
    public enum SwitchPosition
    {
        Center = 0,
        Up = 1,
        UpRight = 2,
        Right = 3,
        DownRight = 4,
        Down = 5,
        DownLeft = 6,
        Left = 7,
        UpLeft = 8,
    }

    /// <summary>Physical keyboard layout kind, matching native <c>GameInputDevice.KeyboardKind</c>.</summary>
    public enum KeyboardKind
    {
        Unknown = -1,
        Ansi = 0,
        Iso = 1,
        Ks = 2,
        Abnt = 3,
        Jis = 4,
    }

    /// <summary>Rumble motor flags, matching native <c>GameInputDevice.RumbleMotor</c>.</summary>
    [Flags]
    public enum RumbleMotor
    {
        None = 0,
        LowFrequency = 1,
        HighFrequency = 2,
        LeftTrigger = 4,
        RightTrigger = 8,
    }
}
