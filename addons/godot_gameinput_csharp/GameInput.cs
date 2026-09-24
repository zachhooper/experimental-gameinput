using System;
using System.Collections.Generic;
using Godot;

namespace GodotGameInput;

/// <summary>
/// Static entry point to the Microsoft GameInput runtime, mirroring the native
/// <c>GameInput</c> engine singleton. Unlike the GDK / PlayFab facades this API
/// is fully synchronous (poll-based) and integrates with Godot's
/// <c>Input</c>/<c>InputMap</c> flow rather than exposing an async result model.
/// </summary>
public static class GameInput
{
    private static GodotObject _singleton;
    private static bool _signalsConnected;

    /// <summary>
    /// Device-kind bit flags, matching native <c>GameInput.DeviceKind</c>.
    /// <see cref="All"/> is gamepads, keyboards and mice (the v1 set);
    /// <see cref="Any"/> is every kind.
    /// </summary>
    [Flags]
    public enum DeviceKind
    {
        Unknown = 0,
        Gamepad = 1,
        Keyboard = 2,
        Mouse = 4,
        All = 7,
        ArcadeStick = 8,
        FlightStick = 16,
        RacingWheel = 32,
        Sensors = 64,
        Controller = 128,
        Any = 255,
    }

    /// <summary>
    /// Focus-policy flags, matching native <c>GameInput.FocusPolicy</c> and
    /// <c>GameInputFocusPolicy</c>. GameInput applies them on Windows only.
    /// </summary>
    [Flags]
    public enum FocusPolicy
    {
        Default = 0,
        ExclusiveForegroundInput = 2,
        ExclusiveForegroundGuideButton = 8,
        ExclusiveForegroundShareButton = 32,
        EnableBackgroundInput = 64,
        EnableBackgroundGuideButton = 128,
        EnableBackgroundShareButton = 256,
    }

    /// <summary>True when the <c>godot_gameinput</c> GDExtension is loaded.</summary>
    public static bool IsAvailable => ResolveSingleton() != null;

    private const string SingletonNameSetting = "game_input/runtime/singleton_name";
    private const string DefaultSingletonName = "GameInput";

    // Native class the singleton must be an instance of. The singleton *name*
    // is configurable; the class it resolves to is not.
    private const string SingletonClassName = "GameInput";

    /// <summary>
    /// Engine singleton name configured by
    /// <c>game_input/runtime/singleton_name</c>, falling back to
    /// <c>GameInput</c>. Mirrors the resolution in the addon's
    /// <c>register_types.cpp</c>.
    /// </summary>
    public static string SingletonName
    {
        get
        {
            string configured = ProjectSettings
                .GetSetting(SingletonNameSetting, DefaultSingletonName)
                .AsString()
                .Trim();
            return string.IsNullOrEmpty(configured) ? DefaultSingletonName : configured;
        }
    }

    // Resolves the singleton by configured name, retrying under the default
    // name because the native side falls back to "GameInput" when the
    // configured name is unusable (for example when it collides with an
    // existing singleton). Candidates are class-checked: a configured name that
    // collides with an unrelated engine singleton (say `Input`) still resolves
    // through Engine.HasSingleton, and returning it would report
    // IsAvailable = true while handing callers the wrong object.
    private static GodotObject ResolveSingleton()
    {
        string configured = SingletonName;
        if (Engine.HasSingleton(configured))
        {
            GodotObject candidate = Engine.GetSingleton(configured);
            if (candidate != null && candidate.IsClass(SingletonClassName))
            {
                return candidate;
            }
        }

        if (configured != DefaultSingletonName && Engine.HasSingleton(DefaultSingletonName))
        {
            GodotObject fallback = Engine.GetSingleton(DefaultSingletonName);
            if (fallback != null && fallback.IsClass(SingletonClassName))
            {
                return fallback;
            }
        }

        return null;
    }

    internal static GodotObject Singleton
    {
        get
        {
            if (_singleton != null && GodotObject.IsInstanceValid(_singleton))
            {
                return _singleton;
            }

            // Re-resolving: the previous singleton is gone (first access or an
            // extension/editor reload). Clear the connected flag so the new
            // singleton's device signals get reconnected below.
            _signalsConnected = false;
            _singleton = ResolveSingleton();
            EnsureSignalsConnected();
            return _singleton;
        }
    }

    private static GodotObject Require()
    {
        return Singleton
            ?? throw new InvalidOperationException(
                "GameInput singleton is not registered. Is the godot_gameinput GDExtension built and loaded?");
    }

    // --- Lifecycle ---
    public static bool Initialize() => Require().Call("initialize").AsBool();

    public static void Shutdown() => Singleton?.Call("shutdown");

    public static bool IsInitialized => Singleton != null && Singleton.Call("is_initialized").AsBool();

    public static void Poll() => Singleton?.Call("poll");

    // --- Devices ---
    public static IReadOnlyList<GameInputDevice> GetDevices(DeviceKind kindMask = DeviceKind.Gamepad)
    {
        var result = new List<GameInputDevice>();
        if (Singleton == null)
        {
            return result;
        }

        Godot.Collections.Array devices = Singleton.Call("get_devices", (int)kindMask).AsGodotArray();
        foreach (Variant device in devices)
        {
            GameInputDevice wrapped = GameInputDevice.From(device.AsGodotObject());
            if (wrapped != null)
            {
                result.Add(wrapped);
            }
        }

        return result;
    }

    public static GameInputDevice GetPrimaryDevice(DeviceKind kindMask = DeviceKind.Gamepad) =>
        Singleton == null
            ? null
            : GameInputDevice.From(Singleton.Call("get_primary_device", (int)kindMask).AsGodotObject());

    public static GameInputReading GetCurrentReading(GameInputDevice device) =>
        Singleton == null || device == null
            ? null
            : GameInputReading.From(Singleton.Call("get_current_reading", device.Raw).AsGodotObject());

    /// <summary>Connected gamepads, keyboards and mice; see <see cref="GetConnectedDeviceCount"/> for other kinds.</summary>
    public static int ConnectedDeviceCount =>
        Singleton == null ? 0 : Singleton.Call("get_connected_device_count").AsInt32();

    /// <summary>Connected devices whose kind mask intersects <paramref name="kindMask"/>.</summary>
    public static int GetConnectedDeviceCount(DeviceKind kindMask = DeviceKind.All) =>
        Singleton == null ? 0 : Singleton.Call("get_connected_device_count", (int)kindMask).AsInt32();

    /// <summary>The connected device with this id, of any kind, or null.</summary>
    public static GameInputDevice GetDeviceById(long deviceId) =>
        Singleton == null
            ? null
            : GameInputDevice.From(Singleton.Call("get_device_by_id", deviceId).AsGodotObject());

    /// <summary>Current GameInput clock time in microseconds; 0 when not initialized.</summary>
    public static long CurrentTimestamp =>
        Singleton == null ? 0 : Singleton.Call("get_current_timestamp").AsInt64();

    // --- Event-driven readings ---

    /// <summary>
    /// Delivers every reading of these kinds through <see cref="ReadingReceived"/>
    /// and <see cref="GetBufferedReadings"/>. <see cref="DeviceKind.Unknown"/>
    /// (0) turns reading callbacks off. Returns false only when GameInput refused
    /// the reading callback.
    /// </summary>
    public static bool SetReadingCallbackKinds(DeviceKind kindMask) =>
        Require().Call("set_reading_callback_kinds", (int)kindMask).AsBool();

    public static DeviceKind ReadingCallbackKinds =>
        Singleton == null ? DeviceKind.Unknown : (DeviceKind)Singleton.Call("get_reading_callback_kinds").AsInt32();

    /// <summary>Readings the callback delivered for <paramref name="device"/> during the last <see cref="Poll"/>, oldest first.</summary>
    public static IReadOnlyList<GameInputReading> GetBufferedReadings(GameInputDevice device)
    {
        var result = new List<GameInputReading>();
        if (Singleton == null || device == null)
        {
            return result;
        }

        foreach (Variant reading in Singleton.Call("get_buffered_readings", device.Raw).AsGodotArray())
        {
            GameInputReading wrapped = GameInputReading.From(reading.AsGodotObject());
            if (wrapped != null)
            {
                result.Add(wrapped);
            }
        }

        return result;
    }

    /// <summary>Readings dropped since <see cref="Initialize"/> because the reading ring was full.</summary>
    public static long DroppedReadingCount =>
        Singleton == null ? 0 : Singleton.Call("get_dropped_reading_count").AsInt64();

    // --- Focus policy ---

    public static void SetFocusPolicy(FocusPolicy policy) => Require().Call("set_focus_policy", (int)policy);

    public static FocusPolicy GetFocusPolicy() =>
        Singleton == null ? FocusPolicy.Default : (FocusPolicy)Singleton.Call("get_focus_policy").AsInt32();

    // --- Aggregate devices ---

    /// <summary>
    /// Asks GameInput for a virtual device combining every connected device of
    /// one kind: exactly one of <see cref="DeviceKind.Gamepad"/>,
    /// <see cref="DeviceKind.Keyboard"/>, <see cref="DeviceKind.Mouse"/>,
    /// <see cref="DeviceKind.ArcadeStick"/>, <see cref="DeviceKind.FlightStick"/>
    /// or <see cref="DeviceKind.RacingWheel"/>. Returns its app-local id
    /// (64 hex characters), or an empty string for any other kind or on
    /// failure. The aggregate arrives through <see cref="DeviceConnected"/>.
    /// </summary>
    public static string CreateAggregateDevice(DeviceKind kind) =>
        Singleton == null ? string.Empty : Singleton.Call("create_aggregate_device", (int)kind).AsString();

    /// <summary>
    /// Disables an aggregate. The device stays connected but stops producing
    /// readings; <see cref="CreateAggregateDevice"/> with the same kind
    /// re-enables it and returns the same id.
    /// </summary>
    public static bool DisableAggregateDevice(string appLocalId) =>
        Singleton != null && Singleton.Call("disable_aggregate_device", appLocalId).AsBool();

    // --- Haptics ---
    public static bool SetVibration(GameInputDevice device, float lowFreq, float highFreq,
        float leftTrigger = 0.0f, float rightTrigger = 0.0f)
    {
        if (Singleton == null || device == null)
        {
            return false;
        }

        return Singleton.Call("set_vibration", device.Raw, lowFreq, highFreq, leftTrigger, rightTrigger).AsBool();
    }

    public static void StopHaptics(GameInputDevice device)
    {
        if (Singleton != null && device != null)
        {
            Singleton.Call("stop_haptics", device.Raw);
        }
    }

    // --- Signals (main-thread, connected once on first singleton resolution) ---
    public static event Action<GameInputDevice> DeviceConnected;
    public static event Action<long> DeviceDisconnected;

    /// <summary>
    /// (device, status, previousStatus, timestamp) when a connected device's status flags change while it stays
    /// connected. Flags that arrive with the connect are already in <see cref="GameInputDevice.Status"/> when
    /// <see cref="DeviceConnected"/> fires and do not raise this event.
    /// </summary>
    public static event Action<GameInputDevice, GameInputDevice.DeviceStatus, GameInputDevice.DeviceStatus, long>
        DeviceStatusChanged;

    /// <summary>(device, reading) for each event-driven reading; see <see cref="SetReadingCallbackKinds"/>.</summary>
    public static event Action<GameInputDevice, GameInputReading> ReadingReceived;

    /// <summary>(device, buttons, previousButtons, timestamp) when Guide or Share is pressed or released.</summary>
    public static event Action<GameInputDevice, GameInputDevice.SystemButton, GameInputDevice.SystemButton, long>
        SystemButtonsChanged;

    /// <summary>(device, layout, previousLayout, timestamp) when a keyboard's layout changes.</summary>
    public static event Action<GameInputDevice, long, long, long> KeyboardLayoutChanged;

    private static void EnsureSignalsConnected()
    {
        if (_signalsConnected || _singleton == null)
        {
            return;
        }

        _signalsConnected = true;
        _singleton.Connect("device_connected",
            Callable.From((GodotObject device) => DeviceConnected?.Invoke(GameInputDevice.From(device))));
        _singleton.Connect("device_disconnected",
            Callable.From((long deviceId) => DeviceDisconnected?.Invoke(deviceId)));
        _singleton.Connect("device_status_changed",
            Callable.From((GodotObject device, long status, long previous, long timestamp) =>
                DeviceStatusChanged?.Invoke(GameInputDevice.From(device), (GameInputDevice.DeviceStatus)status,
                    (GameInputDevice.DeviceStatus)previous, timestamp)));
        _singleton.Connect("reading_received",
            Callable.From((GodotObject device, GodotObject reading) =>
                ReadingReceived?.Invoke(GameInputDevice.From(device), GameInputReading.From(reading))));
        _singleton.Connect("system_buttons_changed",
            Callable.From((GodotObject device, long buttons, long previous, long timestamp) =>
                SystemButtonsChanged?.Invoke(GameInputDevice.From(device), (GameInputDevice.SystemButton)buttons,
                    (GameInputDevice.SystemButton)previous, timestamp)));
        _singleton.Connect("keyboard_layout_changed",
            Callable.From((GodotObject device, long layout, long previous, long timestamp) =>
                KeyboardLayoutChanged?.Invoke(GameInputDevice.From(device), layout, previous, timestamp)));
    }
}
