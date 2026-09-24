using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Godot;
using GodotGameInput;

namespace TutorialGameInputCSharp.SelfTest;

/// <summary>
/// GameInput integration self-test for the C# tutorial sample, the managed
/// counterpart of <c>sample/tutorial_gameinput/selftest/gameinput_selftest.gd</c>.
///
/// <see cref="Main"/> adds it when the command line carries
/// <c>--gameinput-selftest</c> after Godot's <c>--</c> separator:
///
/// <code>godot --headless --path sample/tutorial_gameinput_csharp -- --gameinput-selftest</code>
///
/// The GDScript harness covers the addon; this one covers what the
/// <c>GodotGameInputCSharp</c> facade adds on top of it: its enums against the
/// extension that is actually loaded, its typed events, its wrappers, and the
/// C# tutorial's own wiring (hot-plug UI, action bridge, jump rumble, and
/// unsubscribing from the static events when the scene leaves the tree). The
/// sample and mock checks swap the runtime for the addon's debug-only mock
/// backend, then restore the real runtime.
///
/// It writes the same JSON report (schema <c>gameinput-selftest/1</c>, with
/// <c>"harness": "csharp"</c>) and quits with the same codes: 0 pass (skips
/// allowed), 1 a check failed or skipped under <c>--gameinput-strict</c>,
/// 2 the harness could not run, 3 the watchdog expired.
///
/// Options (after <c>--</c>): <c>--gameinput-report=&lt;path&gt;</c>,
/// <c>--gameinput-strict</c>, <c>--gameinput-session-locked</c>,
/// <c>--gameinput-timeout=&lt;sec&gt;</c>. The virtual-pad checks live in the
/// GDScript harness only.
/// </summary>
public partial class GameInputSelfTest : Node
{
    public const string FlagRun = "--gameinput-selftest";

    private const string Schema = "gameinput-selftest/1";
    private const string DefaultReportPath = "user://gameinput-selftest.json";
    private const double DefaultTimeoutSec = 120.0;

    private const int ExitPass = 0;
    private const int ExitFail = 1;
    private const int ExitHarnessError = 2;
    private const int ExitWatchdog = 3;

    // Values of the addon's debug-only _test_get_backend() seam.
    private const int BackendNative = 1;
    private const int BackendMock = 2;

    // Native classes whose enums the facade mirrors, with their managed types.
    private static readonly (string Native, Type Managed)[] MirroredClasses =
    {
        ("GameInput", typeof(GameInput)),
        ("GameInputDevice", typeof(GameInputDevice)),
        ("GameInputForceFeedbackEffect", typeof(GameInputForceFeedbackEffect)),
        ("GameInputMapper", typeof(GameInputMapper)),
    };

    /// <summary>Set by <see cref="Main"/> before the node enters the tree.</summary>
    public Main Sample { get; set; }

    private readonly Godot.Collections.Array _results = new();
    private readonly List<(string Name, object[] Args)> _events = new();

    private string _reportPath = DefaultReportPath;
    private bool _strict;
    private bool _sessionLocked;
    private double _timeoutSec = DefaultTimeoutSec;

    private Check _cur;
    private ulong _startedMs;
    private string _startedUtc = "";
    private bool _finished;
    private GodotObject _gi;
    private bool _hasSeams;
    private bool _runtimeReady;
    private int _nativeDeviceCount = -1;
    private bool _mockReady;
    private bool _mockStarted;
    private bool _savedInitialized;
    private GameInput.DeviceKind _savedCallbackKinds;
    private GameInput.FocusPolicy _savedFocusPolicy;
    private long _mockPadId = -1;
    private Godot.Collections.Dictionary _display = new();

    private sealed class Check
    {
        public string Id = "";
        public readonly List<string> Failures = new();
        public string Skip = "";
        public string Detail = "";
        public readonly Godot.Collections.Dictionary Data = new();
        public ulong StartedUsec;
    }

    public static bool IsRequested() => OS.GetCmdlineUserArgs().Contains(FlagRun);

    public override void _EnterTree()
    {
        GameInput.DeviceConnected += OnDeviceConnected;
        GameInput.DeviceDisconnected += OnDeviceDisconnected;
        GameInput.DeviceStatusChanged += OnDeviceStatusChanged;
        GameInput.ReadingReceived += OnReadingReceived;
        GameInput.SystemButtonsChanged += OnSystemButtonsChanged;
        GameInput.KeyboardLayoutChanged += OnKeyboardLayoutChanged;
    }

    public override void _ExitTree()
    {
        GameInput.DeviceConnected -= OnDeviceConnected;
        GameInput.DeviceDisconnected -= OnDeviceDisconnected;
        GameInput.DeviceStatusChanged -= OnDeviceStatusChanged;
        GameInput.ReadingReceived -= OnReadingReceived;
        GameInput.SystemButtonsChanged -= OnSystemButtonsChanged;
        GameInput.KeyboardLayoutChanged -= OnKeyboardLayoutChanged;
    }

    public override void _Ready()
    {
        _startedMs = Time.GetTicksMsec();
        _startedUtc = UtcNow();
        _gi = ResolveNative();
        _hasSeams = _gi != null && _gi.HasMethod("_test_initialize_mock");
        _ = RunAsync();
    }

    // ── Orchestration ─────────────────────────────────────────────────────

    private async Task RunAsync()
    {
        try
        {
            string argError = ParseOptions();
            ArmWatchdog();
            GD.Print($"[selftest] GameInput C# self-test: Godot {Engine.GetVersionInfo()["string"]}, " +
                     $".NET {System.Environment.Version}, {(OS.IsDebugBuild() ? "debug" : "release")} build, " +
                     $"mock seams {(_hasSeams ? "available" : "unavailable")}, options {Json.Stringify(Options())}");
            if (argError != "")
            {
                Finish(ExitHarnessError, argError);
                return;
            }

            // Give the GameInputRuntime autoload a few polls to deliver the startup devices.
            await Frames(10);
            SizeHeadlessWindow();

            await RunCheck("api.facade", CheckApiFacade);
            await RunCheck("runtime.initialized", CheckRuntimeInitialized);
            await RunCheck("runtime.timestamp", CheckRuntimeTimestamp);
            await RunCheck("runtime.devices", CheckRuntimeDevices);

            await RunCheck("mock.session", CheckMockSession);
            await RunCheck("sample.hotplug_ui", CheckSampleHotplugUi);
            await RunCheck("sample.action_bridge", CheckSampleActionBridge);
            await RunCheck("sample.player_jump", CheckSamplePlayerJump);
            await RunCheck("sample.disconnect_releases", CheckSampleDisconnectReleases);
            await RunCheck("mock.typed_events", CheckMockTypedEvents);
            await RunCheck("mock.vibration", CheckMockVibration);
            await RunCheck("mock.force_feedback", CheckMockForceFeedback);
            await RunCheck("sample.exit_tree", CheckSampleExitTree);
            await RunCheck("mock.restore", CheckMockRestore);

            Finish(-1, "");
        }
        catch (Exception e)
        {
            GD.PushError($"[selftest] harness exception: {e}");
            Finish(ExitHarnessError, $"harness exception: {e.GetType().Name}: {e.Message}");
        }
    }

    private string ParseOptions()
    {
        var bad = new List<string>();
        foreach (string arg in OS.GetCmdlineUserArgs())
        {
            if (arg == FlagRun)
            {
                continue;
            }

            if (arg == "--gameinput-strict")
            {
                _strict = true;
            }
            else if (arg == "--gameinput-session-locked")
            {
                _sessionLocked = true;
            }
            else if (arg.StartsWith("--gameinput-report=", StringComparison.Ordinal))
            {
                _reportPath = arg.Substring("--gameinput-report=".Length);
            }
            else if (arg.StartsWith("--gameinput-timeout=", StringComparison.Ordinal))
            {
                string value = arg.Substring("--gameinput-timeout=".Length);
                if (double.TryParse(value, System.Globalization.NumberStyles.Float,
                        System.Globalization.CultureInfo.InvariantCulture, out double seconds) && seconds > 0.0)
                {
                    _timeoutSec = seconds;
                }
                else
                {
                    bad.Add(arg);
                }
            }
            else if (arg == "--gameinput-virtual-pad" || arg.StartsWith("--gameinput-vpad-log=", StringComparison.Ordinal))
            {
                bad.Add(arg + " (the virtual-pad checks are in the GDScript sample)");
            }
            else if (arg.StartsWith("--gameinput-", StringComparison.Ordinal))
            {
                bad.Add(arg);
            }
        }

        if (string.IsNullOrWhiteSpace(_reportPath))
        {
            _reportPath = DefaultReportPath;
            return "--gameinput-report needs a path";
        }

        return bad.Count == 0 ? "" : "unknown or malformed option(s): " + string.Join(", ", bad);
    }

    private Godot.Collections.Dictionary Options() => new()
    {
        { "report", _reportPath },
        { "strict", _strict },
        { "session_locked", _sessionLocked },
        { "timeout_sec", _timeoutSec },
    };

    // The headless display server shrinks the root viewport to 64x64 after the
    // first frame, which leaves the player no room to move. Give it the size
    // the project's window would open with.
    private void SizeHeadlessWindow()
    {
        Window root = GetTree().Root;
        if (DisplayServer.GetName() == "headless")
        {
            root.Size = new Vector2I(
                ProjectSettings.GetSetting("display/window/size/viewport_width", 1152).AsInt32(),
                ProjectSettings.GetSetting("display/window/size/viewport_height", 648).AsInt32());
        }

        _display = new Godot.Collections.Dictionary
        {
            { "server", DisplayServer.GetName() },
            { "viewport", new Godot.Collections.Array { root.Size.X, root.Size.Y } },
        };
    }

    private void ArmWatchdog()
    {
        SceneTreeTimer timer = GetTree().CreateTimer(_timeoutSec, true, false, true);
        timer.Timeout += () =>
        {
            if (_finished)
            {
                return;
            }

            if (_cur != null)
            {
                _cur.Failures.Add("watchdog expired during this check");
                CloseCheck();
            }

            string seconds = _timeoutSec.ToString("0.0", System.Globalization.CultureInfo.InvariantCulture);
            Finish(ExitWatchdog, $"watchdog expired after {seconds} s");
        };
    }

    private async Task RunCheck(string id, Func<Task> body)
    {
        if (_finished)
        {
            return;
        }

        _cur = new Check { Id = id, StartedUsec = Time.GetTicksUsec() };
        try
        {
            await body();
        }
        catch (Exception e)
        {
            _cur?.Failures.Add($"threw {e.GetType().Name}: {e.Message}");
        }

        if (!_finished && _cur != null)
        {
            CloseCheck();
        }
    }

    private void CloseCheck()
    {
        string status = "pass";
        string detail = _cur.Detail;
        if (_cur.Failures.Count > 0)
        {
            status = "fail";
            detail = string.Join("; ", _cur.Failures);
        }
        else if (_cur.Skip != "")
        {
            status = "skip";
            detail = _cur.Skip;
        }

        long durationMs = (long)((Time.GetTicksUsec() - _cur.StartedUsec) / 1000);
        _results.Add(new Godot.Collections.Dictionary
        {
            { "id", _cur.Id },
            { "group", _cur.Id.Split('.')[0] },
            { "status", status },
            { "detail", detail },
            { "duration_ms", durationMs },
            { "data", _cur.Data },
        });
        GD.Print($"[selftest] {status.ToUpperInvariant(),-4} {_cur.Id} ({durationMs} ms)" +
                 (detail != "" ? " - " + detail : ""));
        _cur = null;
    }

    private void Finish(int forcedExit, string message)
    {
        if (_finished)
        {
            return;
        }

        _finished = true;
        int pass = 0, fail = 0, skip = 0;
        foreach (Variant entry in _results)
        {
            switch (entry.AsGodotDictionary()["status"].AsString())
            {
                case "pass": pass++; break;
                case "fail": fail++; break;
                default: skip++; break;
            }
        }

        int exitCode = forcedExit >= 0 ? forcedExit : (fail > 0 || (_strict && skip > 0) ? ExitFail : ExitPass);
        string sessionName = OS.GetEnvironment("SESSIONNAME");
        var report = new Godot.Collections.Dictionary
        {
            { "schema", Schema },
            { "harness", "csharp" },
            { "started_utc", _startedUtc },
            { "finished_utc", UtcNow() },
            { "duration_ms", (long)(Time.GetTicksMsec() - _startedMs) },
            { "godot_version", Engine.GetVersionInfo()["string"] },
            { "dotnet_version", System.Environment.Version.ToString() },
            { "os", OS.GetName() },
            { "project", ProjectSettings.GetSetting("application/config/name", "") },
            {
                "build", new Godot.Collections.Dictionary
                {
                    { "debug", OS.IsDebugBuild() },
                    { "mock_seams", _hasSeams },
                    { "singleton", _gi != null },
                }
            },
            { "display", _display },
            {
                "session", new Godot.Collections.Dictionary
                {
                    { "name", sessionName },
                    { "remote", sessionName.ToUpperInvariant().StartsWith("RDP-", StringComparison.Ordinal) },
                    { "locked", _sessionLocked },
                }
            },
            { "options", Options() },
            { "summary", Summary(exitCode, message, pass, fail, skip) },
            { "checks", _results },
        };

        string path = ResolvePath(_reportPath);
        string writeError = WriteText(path, Json.Stringify(report, "  "));
        if (writeError != "")
        {
            GD.PrintErr($"[selftest] could not write the report: {writeError}");
            exitCode = ExitHarnessError;
        }

        string result = Summary(exitCode, message, pass, fail, skip)["result"].AsString();
        GD.Print($"[selftest] RESULT {result.ToUpperInvariant()}: {pass} passed, {fail} failed, {skip} skipped " +
                 $"(exit {exitCode}){(message != "" ? " - " + message : "")}");
        if (writeError == "")
        {
            GD.Print($"[selftest] report: {path}");
        }

        GetTree().Quit(exitCode);
    }

    private Godot.Collections.Dictionary Summary(int exitCode, string message, int pass, int fail, int skip) => new()
    {
        {
            "result", exitCode switch
            {
                ExitFail => "fail",
                ExitHarnessError => "error",
                ExitWatchdog => "timeout",
                _ => "pass",
            }
        },
        { "exit_code", exitCode },
        { "message", message },
        { "total", _results.Count },
        { "pass", pass },
        { "fail", fail },
        { "skip", skip },
    };

    // ── api + runtime ─────────────────────────────────────────────────────

    // The facade's enums are hand-written copies of the extension's constants.
    // Compare every constant the loaded extension registers, not the doc XML,
    // so a stale DLL or a mistyped value both show up here.
    private Task CheckApiFacade()
    {
        if (!Expect(GameInput.IsAvailable, "GameInput.IsAvailable is false: the godot_gameinput extension is not loaded"))
        {
            return Task.CompletedTask;
        }

        Expect(_gi != null && _gi.IsClass("GameInput"), "the engine singleton is not a GameInput");
        Note("singleton_name", GameInput.SingletonName);
        int compared = 0;
        foreach ((string native, Type managed) in MirroredClasses)
        {
            if (!Expect(ClassDB.ClassExists(native), $"the extension does not register {native}"))
            {
                continue;
            }

            foreach (string constant in ClassDB.ClassGetIntegerConstantList(native, true))
            {
                string enumName = ClassDB.ClassGetIntegerConstantEnum(native, constant, true);
                long value = ClassDB.ClassGetIntegerConstant(native, constant);
                Type enumType = string.IsNullOrEmpty(enumName) ? null : managed.GetNestedType(enumName);
                if (!Expect(enumType != null && enumType.IsEnum,
                        $"{native}.{constant}: {managed.Name} has no nested enum '{enumName}'"))
                {
                    continue;
                }

                // The longest managed name that ends the native one is the match:
                // SRC_BTN_A is Source.BtnA.
                string norm = constant.Replace("_", "").ToLowerInvariant();
                string match = Enum.GetNames(enumType)
                    .Where(n => norm.EndsWith(n.ToLowerInvariant(), StringComparison.Ordinal))
                    .OrderByDescending(n => n.Length)
                    .FirstOrDefault();
                if (Expect(match != null, $"{native}.{constant}: no member of {managed.Name}.{enumName}"))
                {
                    long managedValue = Convert.ToInt64(Enum.Parse(enumType, match));
                    Expect(managedValue == value,
                        $"{native}.{constant} is {value} but {managed.Name}.{enumName}.{match} is {managedValue}");
                    compared++;
                }
            }
        }

        Note("constants_compared", compared);
        Expect(compared > 0, "no constants were compared");

        // The static helpers go through ClassDB.ClassCallStatic and need no device.
        Expect(GameInputDevice.ScanCodeToPhysicalKey(0x1E) == Key.A, "scan code 0x1E is not Key.A");
        Expect(GameInputDevice.ScanCodeToPhysicalKey(0xE048) == Key.Up, "extended scan code 0xE048 is not Key.Up");
        Expect(GameInputDevice.VirtualKeyToKeycode(0x41) == Key.A, "virtual key 0x41 is not Key.A");
        Expect(GameInputDevice.SwitchPositionToVector(GameInputDevice.SwitchPosition.Up) == new Vector2(0, -1),
            "SwitchPosition.Up is not Vector2(0, -1)");
        PassDetail($"facade loaded; all {compared} native constants match the C# enums; static helpers convert");
        return Task.CompletedTask;
    }

    private Task CheckRuntimeInitialized()
    {
        if (!RequireSingleton())
        {
            return Task.CompletedTask;
        }

        _runtimeReady = GameInput.IsInitialized;
        if (_hasSeams)
        {
            Note("backend", _gi.Call("_test_get_backend"));
        }

        if (!_runtimeReady)
        {
            Skip("GameInput is not initialized on this host (GameInputCreate failed or the runtime is missing); runtime checks are skipped");
            return Task.CompletedTask;
        }

        if (_hasSeams)
        {
            Expect(_gi.Call("_test_get_backend").AsInt32() == BackendNative,
                "the GameInputRuntime autoload initialized a backend other than native GameInput");
        }

        PassDetail("the C# GameInputRuntime autoload initialized native GameInput");
        return Task.CompletedTask;
    }

    private async Task CheckRuntimeTimestamp()
    {
        if (!RequireRuntime())
        {
            return;
        }

        long first = GameInput.CurrentTimestamp;
        await Frames(2);
        long second = GameInput.CurrentTimestamp;
        Note("first_usec", first);
        Note("second_usec", second);
        Expect(first > 0, $"GameInput.CurrentTimestamp returned {first}");
        Expect(second > first, "the GameInput clock did not advance across two frames");
        PassDetail($"GameInput clock advances ({second - first} us over two frames)");
    }

    private Task CheckRuntimeDevices()
    {
        if (!RequireRuntime())
        {
            return Task.CompletedTask;
        }

        IReadOnlyList<GameInputDevice> devices = GameInput.GetDevices(GameInput.DeviceKind.Any);
        _nativeDeviceCount = devices.Count;
        Expect(GameInput.GetConnectedDeviceCount(GameInput.DeviceKind.Any) == devices.Count,
            "GetConnectedDeviceCount(Any) disagrees with GetDevices(Any)");
        var ids = new HashSet<long>();
        var names = new List<string>();
        foreach (GameInputDevice device in devices)
        {
            Expect(device.DeviceId > 0 && ids.Add(device.DeviceId), $"device id {device.DeviceId} is not a unique positive id");
            Expect(device.IsConnected, $"{device.DisplayName} is listed but not connected");
            Expect(!string.IsNullOrEmpty(device.DisplayName), $"device {device.DeviceId} has no display name");
            names.Add(device.DisplayName);
        }

        Note("devices", new Godot.Collections.Array(names.Select(n => Variant.From(n))));
        PassDetail(devices.Count == 0
            ? "no devices connected (plug in a gamepad to exercise enumeration)"
            : $"{devices.Count} device(s) wrapped: {string.Join(", ", names)}");
        return Task.CompletedTask;
    }

    // ── mock + sample (debug builds: scripted devices through the real pipeline)

    private Task CheckMockSession()
    {
        if (!RequireSingleton())
        {
            return Task.CompletedTask;
        }

        if (!_hasSeams)
        {
            Skip("mock seams are compiled out of release builds; sample and mock checks are skipped");
            return Task.CompletedTask;
        }

        _savedInitialized = GameInput.IsInitialized;
        _savedCallbackKinds = GameInput.ReadingCallbackKinds;
        _savedFocusPolicy = GameInput.GetFocusPolicy();
        _mockStarted = true;
        GameInput.Shutdown();
        GameInput.SetReadingCallbackKinds(GameInput.DeviceKind.Unknown);
        GameInput.SetFocusPolicy(GameInput.FocusPolicy.Default);
        Expect(_gi.Call("_test_initialize_mock").AsBool(), "_test_initialize_mock() failed");
        Expect(_gi.Call("_test_get_backend").AsInt32() == BackendMock, "the mock backend is not active");
        Expect(GameInput.IsInitialized, "the mock runtime does not report initialized");
        Expect(GameInput.GetDevices(GameInput.DeviceKind.Any).Count == 0, "the mock runtime did not start empty");
        _mockReady = _cur.Failures.Count == 0;
        PassDetail("native runtime shut down, mock backend active");
        return Task.CompletedTask;
    }

    private async Task CheckSampleHotplugUi()
    {
        if (!RequireMock() || !RequireSample())
        {
            return;
        }

        _events.Clear();
        _mockPadId = InjectDevice(new Godot.Collections.Dictionary { { "name", "Selftest Pad" }, { "rumble_motors", 0xF } });
        if (!Expect(_mockPadId > 0, "_test_inject_device rejected a gamepad"))
        {
            return;
        }

        // No forced poll: the GameInputRuntime autoload's per-frame poll must deliver it.
        await Frames(2);
        List<object[]> connected = Named("device_connected");
        if (Expect(connected.Count == 1, $"DeviceConnected fired {connected.Count} time(s), expected 1"))
        {
            var device = connected[0][0] as GameInputDevice;
            Expect(device != null && device.DeviceId == _mockPadId, "DeviceConnected did not carry the injected pad");
            Expect(device?.DisplayName == "Selftest Pad", $"the wrapper reads the name '{device?.DisplayName}'");
            // v1 shape: instance methods over the static natives.
            Expect(device?.ButtonToSource(GameInputDevice.Button.A) == (int)GameInputDevice.Source.BtnA,
                "ButtonToSource(Button.A) is not Source.BtnA");
            Expect(device?.AxisToSource(GameInputDevice.Axis.Wheel) == (int)GameInputDevice.Source.AxisWheel,
                "AxisToSource(Axis.Wheel) is not Source.AxisWheel");
        }

        Expect(Sample.DevicesText.Contains("Selftest Pad"), "the sample's device list does not show the pad");
        Expect(Sample.DeviceCountText.EndsWith(": 1", StringComparison.Ordinal),
            $"the sample's gamepad count reads '{Sample.DeviceCountText}'");
        Expect(Sample.HotplugText.Contains($"connected: id={_mockPadId}"), "the sample's hot-plug log does not show the connect");
        PassDetail("injected pad reached GameInput.DeviceConnected and the sample UI on the next frame");
    }

    private async Task CheckSampleActionBridge()
    {
        if (!RequireMockPad())
        {
            return;
        }

        GameInputMapper mapper = Sample.Mapper;
        if (!Expect(mapper != null, "the sample has no GameInputMapper"))
        {
            return;
        }

        PushReading(_mockPadId, Gamepad(("buttons", (int)GameInputDevice.Button.A)));
        await Frames(2);
        Expect(Input.IsActionPressed("jump"), "A did not press 'jump'");
        Expect(Input.IsActionPressed("ui_accept"), "A did not press 'ui_accept'");
        Expect(mapper.ActiveBindingCount == 2, $"{mapper.ActiveBindingCount} bindings active while A is held, expected 2");
        PushReading(_mockPadId, Gamepad(("buttons", 0), ("left_x", -1.0)));
        await Frames(2);
        Expect(!Input.IsActionPressed("jump"), "'jump' stayed pressed after A was released");
        ExpectNear(Input.GetActionStrength("move_left"), 1.0, "full left stick -> move_left", 0.01);
        ExpectNear(Input.GetActionStrength("move_right"), 0.0, "full left stick -> move_right", 0.01);
        PushReading(_mockPadId, Gamepad(("left_x", 0.6)));
        await Frames(2);
        // Axis bindings rescale past the 0.2 deadzone: (0.6 - 0.2) / 0.8.
        ExpectNear(Input.GetActionStrength("move_right"), 0.5, "left stick 0.6 -> move_right", 0.01);
        Expect(!Input.IsActionPressed("move_left"), "'move_left' stayed pressed");
        PushReading(_mockPadId, Gamepad());
        await Frames(2);
        Expect(!Input.IsActionPressed("move_right"), "'move_right' stayed pressed at rest");
        PassDetail("A -> jump + ui_accept, left stick -> move_left / move_right with deadzone rescaling");
    }

    private async Task CheckSamplePlayerJump()
    {
        if (!RequireMockPad())
        {
            return;
        }

        ulong landDeadline = Time.GetTicksMsec() + 2000;
        while (!Sample.IsPlayerOnFloor && Time.GetTicksMsec() < landDeadline)
        {
            await PhysicsFrames(1);
        }

        float floorY = Sample.PlayerPosition.Y;
        // Freeze the addon's clock so the jump rumble cannot time out before it is read.
        long frozenUsec = (long)Time.GetTicksUsec();
        _gi.Call("_test_set_time_override_usec", frozenUsec);
        long rumblesBefore = LastRumble(_mockPadId)["apply_count"].AsInt64();
        PushReading(_mockPadId, Gamepad(("buttons", (int)GameInputDevice.Button.A)));
        bool rose = false;
        for (int i = 0; i < 30 && !rose; i++)
        {
            await PhysicsFrames(1);
            rose = Sample.PlayerPosition.Y < floorY - 1.0f;
        }

        PushReading(_mockPadId, Gamepad());
        Expect(rose, "the player did not leave the floor within 30 physics frames of pressing A");
        Godot.Collections.Dictionary rumble = LastRumble(_mockPadId);
        Note("jump_rumble", rumble);
        if (Expect(rumble["apply_count"].AsInt64() > rumblesBefore, "jumping did not rumble the pad (Step 7)"))
        {
            Expect(rumble["active"].AsBool(), "the jump rumble is not active");
            ExpectNear(rumble["low"].AsDouble(), Main.JumpRumbleStrong, "jump rumble strong (low-frequency) motor");
            ExpectNear(rumble["high"].AsDouble(), Main.JumpRumbleWeak, "jump rumble weak (high-frequency) motor");
            long endUsec = rumble["end_usec"].AsInt64();
            Expect(endUsec == frozenUsec + (long)Math.Round(Main.JumpRumbleSec * 1000000.0),
                $"the jump rumble does not end {Main.JumpRumbleSec:0.00} s after it started");
            _gi.Call("_test_set_time_override_usec", endUsec);
            _gi.Call("_test_force_poll");
            Expect(!LastRumble(_mockPadId)["active"].AsBool(), "the jump rumble did not stop on time");
        }

        _gi.Call("_test_set_time_override_usec", -1);
        await Frames(2);
        float maxX = Sample.PlayerMaxX;
        Note("play_area_max_x", maxX);
        if (!Expect(maxX >= 8.0f, $"the play area is too narrow to move in (max x {maxX:0.0})"))
        {
            return;
        }

        // Push towards the side with room, so a player parked at a wall cannot pass vacuously.
        float startX = Sample.PlayerPosition.X;
        float direction = startX < maxX * 0.5f ? 1.0f : -1.0f;
        string side = direction > 0.0f ? "right" : "left";
        PushReading(_mockPadId, Gamepad(("left_x", (double)direction)));
        await PhysicsFrames(6);
        PushReading(_mockPadId, Gamepad());
        float moved = Sample.PlayerPosition.X - startX;
        Note("moved_px", moved);
        Expect(moved * direction > 1.0f, $"the player did not move {side} with the stick (moved {moved:0.0} px)");
        await Frames(2);
        PassDetail($"gameplay code reacted: the player jumped (with a {Main.JumpRumbleSec:0.00} s rumble) " +
                   $"and moved {Math.Abs(moved):0} px {side}");
    }

    private async Task CheckSampleDisconnectReleases()
    {
        if (!RequireMockPad())
        {
            return;
        }

        PushReading(_mockPadId, Gamepad(("buttons", (int)GameInputDevice.Button.A)));
        await Frames(2);
        Expect(Input.IsActionPressed("jump"), "A did not press 'jump'");
        _events.Clear();
        Expect(_gi.Call("_test_remove_device", _mockPadId).AsBool(), "_test_remove_device failed");
        await Frames(2);
        List<object[]> gone = Named("device_disconnected");
        Expect(gone.Count == 1 && (long)gone[0][0] == _mockPadId,
            "GameInput.DeviceDisconnected did not fire once with the pad's id");
        Expect(!Input.IsActionPressed("jump"), "'jump' stayed pressed after the pad was unplugged mid-press");
        Expect(Sample.HotplugText.Contains($"disconnected: id={_mockPadId}"),
            "the sample's hot-plug log does not show the disconnect");
        Expect(!Sample.DevicesText.Contains("Selftest Pad"), "the sample still lists the pad");
        _mockPadId = -1;
        PassDetail("unplugging mid-press released the action and updated the UI");
    }

    // The facade connects each native signal to a Callable.From lambda; a
    // wrong parameter list only fails when the signal is emitted.
    private Task CheckMockTypedEvents()
    {
        if (!RequireMock())
        {
            return Task.CompletedTask;
        }

        var verified = new List<string>();
        const GameInputDevice.Button a = GameInputDevice.Button.A;
        Expect(GameInput.SetReadingCallbackKinds(GameInput.DeviceKind.Gamepad), "SetReadingCallbackKinds(Gamepad) failed");
        GameInputDevice tap = MockDevice(GameInput.DeviceKind.Gamepad, ("name", "Tap Pad"));
        if (Expect(tap != null, "gamepad injection failed"))
        {
            _events.Clear();
            // A full tap between two polls: a polled game would miss it entirely.
            PushReading(tap.DeviceId, Gamepad(("buttons", (int)a)).With("timestamp", 1000));
            PushReading(tap.DeviceId, Gamepad(("buttons", 0)).With("timestamp", 2000));
            _gi.Call("_test_force_poll");
            List<GameInputReading> readings = Named("reading_received")
                .Where(e => (e[0] as GameInputDevice)?.DeviceId == tap.DeviceId)
                .Select(e => e[1] as GameInputReading)
                .ToList();
            if (Expect(readings.Count == 2, $"expected 2 ReadingReceived events, got {readings.Count}"))
            {
                Expect(readings[0] != null && readings[0].WasButtonPressed(a), "the first event reading is the press");
                Expect(readings[1] != null && readings[1].WasButtonReleased(a), "the second event reading is the release");
                Expect(readings[1]?.GetTimestamp() == 2000, "event readings keep their timestamps");
                verified.Add("ReadingReceived");
            }

            Expect(GameInput.GetBufferedReadings(tap).Count == 2, "GetBufferedReadings does not hold both readings");
            Expect(!GameInput.GetCurrentReading(tap).IsButtonDown(a), "the polled reading should show A up");
            RemoveDevice(tap);
        }

        GameInput.SetReadingCallbackKinds(GameInput.DeviceKind.Unknown);
        const GameInputDevice.SystemButton guide = GameInputDevice.SystemButton.Guide;
        GameInputDevice pad = MockDevice(GameInput.DeviceKind.Gamepad, ("name", "System Pad"),
            ("system_buttons", (int)(guide | GameInputDevice.SystemButton.Share)));
        GameInputDevice keyboard = MockDevice(GameInput.DeviceKind.Keyboard, ("name", "Layout Keyboard"),
            ("keyboard_layout", 0x0409));
        if (Expect(pad != null && keyboard != null, "device injection failed"))
        {
            _events.Clear();
            const GameInputDevice.DeviceStatus ready =
                GameInputDevice.DeviceStatus.Connected | GameInputDevice.DeviceStatus.HapticInfoReady;
            _gi.Call("_test_push_system_buttons", pad.DeviceId, (int)guide);
            _gi.Call("_test_push_keyboard_layout", keyboard.DeviceId, 0x0407);
            _gi.Call("_test_set_device_status", pad.DeviceId, (long)ready);
            _gi.Call("_test_force_poll");

            List<object[]> system = Named("system_buttons_changed");
            if (Expect(system.Count == 1, $"expected one SystemButtonsChanged, got {system.Count}"))
            {
                Expect((system[0][0] as GameInputDevice)?.DeviceId == pad.DeviceId, "SystemButtonsChanged carries the wrong device");
                Expect((GameInputDevice.SystemButton)system[0][1] == guide &&
                       (GameInputDevice.SystemButton)system[0][2] == GameInputDevice.SystemButton.None,
                    $"Guide press: buttons {system[0][1]} previous {system[0][2]}");
                verified.Add("SystemButtonsChanged");
            }

            Expect(pad.SystemButtons == guide, "GameInputDevice.SystemButtons does not report Guide");
            List<object[]> layout = Named("keyboard_layout_changed");
            if (Expect(layout.Count == 1, $"expected one KeyboardLayoutChanged, got {layout.Count}"))
            {
                Expect((long)layout[0][1] == 0x0407 && (long)layout[0][2] == 0x0409, "layout 0x0409 -> 0x0407");
                verified.Add("KeyboardLayoutChanged");
            }

            List<object[]> status = Named("device_status_changed");
            if (Expect(status.Count == 1, $"expected one DeviceStatusChanged, got {status.Count}"))
            {
                Expect((GameInputDevice.DeviceStatus)status[0][1] == ready &&
                       (GameInputDevice.DeviceStatus)status[0][2] == GameInputDevice.DeviceStatus.Connected,
                    $"status {status[0][1]} previous {status[0][2]}");
                verified.Add("DeviceStatusChanged");
            }

            Expect(pad.Status == ready, $"GameInputDevice.Status reads {pad.Status}");
            RemoveDevice(pad);
            RemoveDevice(keyboard);
        }

        Note("verified", new Godot.Collections.Array(verified.Select(v => Variant.From(v))));
        PassDetail($"typed events carry current and previous values: {string.Join(", ", verified)}");
        return Task.CompletedTask;
    }

    private Task CheckMockVibration()
    {
        if (!RequireMock())
        {
            return Task.CompletedTask;
        }

        GameInputDevice pad = MockDevice(GameInput.DeviceKind.Gamepad, ("name", "Rumble Pad"), ("rumble_motors", 0xF));
        if (!Expect(pad != null, "gamepad injection failed"))
        {
            return Task.CompletedTask;
        }

        long id = pad.DeviceId;
        Expect(pad.SupportsVibration, "the rumble pad does not report SupportsVibration");
        Expect(pad.StartVibration(0.25f, 0.75f), "StartVibration failed");
        Godot.Collections.Dictionary rumble = LastRumble(id);
        ExpectNear(rumble["low"].AsDouble(), 0.75, "strong magnitude drives the low-frequency motor");
        ExpectNear(rumble["high"].AsDouble(), 0.25, "weak magnitude drives the high-frequency motor");
        ExpectNear(pad.VibrationStrength.X, 0.25, "VibrationStrength.X is the weak magnitude");
        ExpectNear(pad.VibrationStrength.Y, 0.75, "VibrationStrength.Y is the strong magnitude");
        Expect(pad.VibrationRemainingDuration == -1.0f, "untimed vibration reports -1 remaining");
        Expect(pad.StartVibration(0.0f, 0.0f, 0.0f, 0.3f, 0.6f), "trigger vibration failed");
        rumble = LastRumble(id);
        ExpectNear(rumble["left_trigger"].AsDouble(), 0.3, "left impulse trigger");
        ExpectNear(rumble["right_trigger"].AsDouble(), 0.6, "right impulse trigger");
        _gi.Call("_test_set_time_override_usec", 1000000);
        Expect(pad.StartVibration(1.0f, 1.0f, 0.5f), "timed vibration failed");
        _gi.Call("_test_set_time_override_usec", 1499999);
        _gi.Call("_test_force_poll");
        Expect(pad.IsVibrating, "stopped before the duration elapsed");
        _gi.Call("_test_set_time_override_usec", 1500000);
        _gi.Call("_test_force_poll");
        Expect(!pad.IsVibrating, "Poll() did not stop the vibration when the duration elapsed");
        ExpectNear(LastRumble(id)["low"].AsDouble(), 0.0, "the stop reached the device");
        _gi.Call("_test_set_time_override_usec", -1);
        RemoveDevice(pad);
        PassDetail("StartVibration maps weak/strong like Input.StartJoyVibration, drives impulse triggers and auto-stops");
        return Task.CompletedTask;
    }

    private Task CheckMockForceFeedback()
    {
        if (!RequireMock())
        {
            return Task.CompletedTask;
        }

        GameInputDevice wheel = MockDevice(GameInput.DeviceKind.RacingWheel, ("name", "FFB Wheel"),
            ("ffb_motors", new Godot.Collections.Array { new Godot.Collections.Dictionary() }));
        if (!Expect(wheel != null, "wheel injection failed"))
        {
            return Task.CompletedTask;
        }

        const GameInputForceFeedbackEffect.EffectKind constant = GameInputForceFeedbackEffect.EffectKind.Constant;
        Expect(wheel.ForceFeedbackMotorCount == 1, "one force-feedback motor");
        Godot.Collections.Dictionary motor = wheel.GetForceFeedbackMotorInfo(0);
        Expect(motor.ContainsKey("supported_effects") && motor["supported_effects"].AsGodotArray().Contains((int)constant),
            "motor 0 does not list the constant effect");
        GameInputForceFeedbackEffect effect = wheel.CreateForceFeedbackEffect(0, new Godot.Collections.Dictionary
        {
            { "kind", (int)constant },
            { "magnitude", 0.5 },
            { "sustain_duration", 0.25 },
        });
        if (!Expect(effect != null, "CreateForceFeedbackEffect returned null"))
        {
            RemoveDevice(wheel);
            return Task.CompletedTask;
        }

        Expect(effect.IsValid, "the new effect is not valid");
        Expect(effect.Kind == constant, $"the effect reads kind {effect.Kind}");
        Expect(effect.State == GameInputForceFeedbackEffect.EffectState.Stopped, "effects start stopped");
        effect.Start();
        Expect(effect.State == GameInputForceFeedbackEffect.EffectState.Running, "Start() did not run the effect");
        effect.SetGain(0.5f);
        ExpectNear(effect.Gain, 0.5, "effect gain");
        Godot.Collections.Dictionary parameters = effect.GetParams();
        ExpectNear(parameters.ContainsKey("sustain_duration") ? parameters["sustain_duration"].AsDouble() : -1.0, 0.25,
            "sustain duration round trip");
        effect.Stop();
        Expect(effect.State == GameInputForceFeedbackEffect.EffectState.Stopped, "Stop() did not stop the effect");
        effect.Dispose();
        Expect(!effect.IsValid, "Dispose() left the effect valid");
        Expect(_gi.Call("_test_get_effect_count").AsInt32() == 0, "a disposed effect is still registered");
        RemoveDevice(wheel);
        PassDetail("constant effect created, started, re-gained, stopped and disposed through the C# wrapper");
        return Task.CompletedTask;
    }

    // Main subscribes to GameInput's static events while it is in the tree.
    // Out of the tree it must not hear them; back in, it must again.
    private async Task CheckSampleExitTree()
    {
        if (!RequireMock() || !RequireSample())
        {
            return;
        }

        Node parent = Sample.GetParent();
        int handled = Sample.HotplugEventsHandled;
        parent.RemoveChild(Sample);
        _events.Clear();
        long id = InjectDevice(new Godot.Collections.Dictionary { { "name", "Late Pad" } });
        await Frames(2);
        Expect(Named("device_connected").Count == 1, "GameInput.DeviceConnected did not fire for the late pad");
        int handledOut = Sample.HotplugEventsHandled;
        Expect(handledOut == handled,
            "Main handled a hot-plug event after leaving the tree: _ExitTree did not unsubscribe");
        parent.AddChild(Sample);
        await Frames(1);
        Expect(_gi.Call("_test_remove_device", id).AsBool(), "_test_remove_device failed");
        await Frames(2);
        Expect(Sample.HotplugEventsHandled == handledOut + 1,
            "Main did not hear the disconnect after re-entering the tree: _EnterTree did not subscribe again");
        PassDetail("Main stops handling GameInput events out of the tree and resumes when it re-enters");
    }

    private async Task CheckMockRestore()
    {
        if (!_hasSeams || !_mockStarted)
        {
            Skip("the mock session never started");
            return;
        }

        GameInput.Shutdown();
        GameInput.SetReadingCallbackKinds(_savedCallbackKinds);
        GameInput.SetFocusPolicy(_savedFocusPolicy);
        _mockReady = false;
        if (!_savedInitialized)
        {
            Expect(!GameInput.IsInitialized, "the runtime should stay shut down, as it was before the mock session");
            PassDetail("runtime left shut down, as it was before the mock session");
            return;
        }

        Expect(GameInput.Initialize(), "re-initializing native GameInput failed");
        Expect(_gi.Call("_test_get_backend").AsInt32() == BackendNative, "the native backend is not active again");
        if (_nativeDeviceCount >= 0)
        {
            ulong deadline = Time.GetTicksMsec() + 2000;
            int count = -1;
            while (Time.GetTicksMsec() < deadline)
            {
                await Frames(2);
                count = GameInput.GetDevices(GameInput.DeviceKind.Any).Count;
                if (count >= _nativeDeviceCount)
                {
                    break;
                }
            }

            Note("devices_after_restore", count);
            Expect(count >= _nativeDeviceCount,
                $"{count} device(s) came back after re-initializing, expected {_nativeDeviceCount}");
        }

        PassDetail("native GameInput re-initialized and its devices re-enumerated");
    }

    // ── Event recording ───────────────────────────────────────────────────

    private void OnDeviceConnected(GameInputDevice device) => _events.Add(("device_connected", new object[] { device }));

    private void OnDeviceDisconnected(long deviceId) => _events.Add(("device_disconnected", new object[] { deviceId }));

    private void OnDeviceStatusChanged(GameInputDevice device, GameInputDevice.DeviceStatus status,
        GameInputDevice.DeviceStatus previous, long timestamp) =>
        _events.Add(("device_status_changed", new object[] { device, status, previous, timestamp }));

    private void OnReadingReceived(GameInputDevice device, GameInputReading reading) =>
        _events.Add(("reading_received", new object[] { device, reading }));

    private void OnSystemButtonsChanged(GameInputDevice device, GameInputDevice.SystemButton buttons,
        GameInputDevice.SystemButton previous, long timestamp) =>
        _events.Add(("system_buttons_changed", new object[] { device, buttons, previous, timestamp }));

    private void OnKeyboardLayoutChanged(GameInputDevice device, long layout, long previous, long timestamp) =>
        _events.Add(("keyboard_layout_changed", new object[] { device, layout, previous, timestamp }));

    private List<object[]> Named(string name) => _events.Where(e => e.Name == name).Select(e => e.Args).ToList();

    // ── Mock helpers ──────────────────────────────────────────────────────

    private long InjectDevice(Godot.Collections.Dictionary info) => _gi.Call("_test_inject_device", info).AsInt64();

    private GameInputDevice MockDevice(GameInput.DeviceKind kind, params (string Key, Variant Value)[] extra)
    {
        var info = new Godot.Collections.Dictionary { { "kind_mask", (int)kind } };
        foreach ((string key, Variant value) in extra)
        {
            info[key] = value;
        }

        long id = InjectDevice(info);
        if (id <= 0)
        {
            return null;
        }

        _gi.Call("_test_force_poll");
        return GameInput.GetDeviceById(id);
    }

    private void RemoveDevice(GameInputDevice device)
    {
        _gi.Call("_test_remove_device", device.DeviceId);
        _gi.Call("_test_force_poll");
    }

    private void PushReading(long deviceId, Godot.Collections.Dictionary state) =>
        _gi.Call("_test_push_reading", deviceId, state);

    private static Godot.Collections.Dictionary Gamepad(params (string Key, Variant Value)[] fields)
    {
        var gamepad = new Godot.Collections.Dictionary();
        foreach ((string key, Variant value) in fields)
        {
            gamepad[key] = value;
        }

        return new Godot.Collections.Dictionary { { "gamepad", gamepad } };
    }

    private Godot.Collections.Dictionary LastRumble(long deviceId) =>
        _gi.Call("_test_get_last_rumble", deviceId).AsGodotDictionary();

    // ── Check helpers ─────────────────────────────────────────────────────

    private bool Expect(bool condition, string what)
    {
        if (!condition)
        {
            _cur.Failures.Add(what);
        }

        return condition;
    }

    private bool ExpectNear(double actual, double expected, string what, double tolerance = 0.001) =>
        Expect(Math.Abs(actual - expected) <= tolerance, $"{what}: expected {expected:0.0000}, got {actual:0.0000}");

    private void Skip(string reason) => _cur.Skip = reason;

    private void PassDetail(string detail) => _cur.Detail = detail;

    private void Note(string key, Variant value) => _cur.Data[key] = value;

    private bool RequireSingleton()
    {
        if (_gi == null)
        {
            _cur.Failures.Add("GameInput singleton is not registered");
            return false;
        }

        return true;
    }

    private bool RequireRuntime()
    {
        if (!RequireSingleton())
        {
            return false;
        }

        if (!_runtimeReady)
        {
            Skip("the GameInput runtime is not initialized on this host");
            return false;
        }

        return true;
    }

    private bool RequireMock()
    {
        if (!_hasSeams)
        {
            Skip("mock seams are compiled out of release builds");
            return false;
        }

        if (!_mockReady)
        {
            _cur.Failures.Add("the mock session is not active (see mock.session)");
            return false;
        }

        return true;
    }

    private bool RequireSample()
    {
        if (Sample == null || !IsInstanceValid(Sample))
        {
            _cur.Failures.Add("the self-test has no sample scene to drive");
            return false;
        }

        return true;
    }

    private bool RequireMockPad()
    {
        if (!RequireMock() || !RequireSample())
        {
            return false;
        }

        if (_mockPadId <= 0)
        {
            _cur.Failures.Add("the mock pad from sample.hotplug_ui is not connected");
            return false;
        }

        return true;
    }

    private async Task Frames(int count)
    {
        for (int i = 0; i < count; i++)
        {
            await ToSignal(GetTree(), SceneTree.SignalName.ProcessFrame);
        }
    }

    private async Task PhysicsFrames(int count)
    {
        for (int i = 0; i < count; i++)
        {
            await ToSignal(GetTree(), SceneTree.SignalName.PhysicsFrame);
        }
    }

    private static GodotObject ResolveNative()
    {
        foreach (string name in new[] { GameInput.SingletonName, "GameInput" })
        {
            if (Engine.HasSingleton(name))
            {
                GodotObject candidate = Engine.GetSingleton(name);
                if (candidate != null && candidate.IsClass("GameInput"))
                {
                    return candidate;
                }
            }
        }

        return null;
    }

    private static string UtcNow() => Time.GetDatetimeStringFromSystem(true) + "Z";

    private static string ResolvePath(string path) =>
        path.StartsWith("user://", StringComparison.Ordinal) || path.StartsWith("res://", StringComparison.Ordinal)
            ? ProjectSettings.GlobalizePath(path)
            : path;

    private static string WriteText(string path, string text)
    {
        string dir = path.GetBaseDir();
        if (dir != "" && !DirAccess.DirExistsAbsolute(dir))
        {
            Error mk = DirAccess.MakeDirRecursiveAbsolute(dir);
            if (mk != Error.Ok)
            {
                return $"cannot create {dir} ({mk})";
            }
        }

        using FileAccess file = FileAccess.Open(path, FileAccess.ModeFlags.Write);
        if (file == null)
        {
            return $"cannot open {path} ({FileAccess.GetOpenError()})";
        }

        file.StoreString(text + "\n");
        return "";
    }
}

internal static class SelfTestDictionaryExtensions
{
    /// <summary>Sets <paramref name="key"/> and returns the same dictionary, for building mock readings inline.</summary>
    public static Godot.Collections.Dictionary With(this Godot.Collections.Dictionary dictionary, string key, Variant value)
    {
        dictionary[key] = value;
        return dictionary;
    }
}
