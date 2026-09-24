# Tutorial GameInput (C#) - standalone sample

The C# version of the [GameInput sample](../tutorial_gameinput/README.md):
the end-state of the
[GameInput action-bridge tutorial](../../docs/tutorials/gameinput-action-bridge.md)
written against the `GodotGameInputCSharp` facade
([C# guide](../../docs/gameinput/csharp.md)). It does **not** depend on XBOX
sign-in or PlayFab.

* [`Main.cs`](Main.cs) builds the tutorial's `GameInputActionMap` in code,
  drives a `GameInputMapper`, shows the gamepad count, device list, hot-plug
  log and live action strengths, and moves a player that jumps with A and
  rumbles the pad on each jump (tutorial Step 7). It subscribes to the
  facade's static events in `_EnterTree()` and unsubscribes in
  `_ExitTree()`.
* [`Autoload/GameInputBootstrap.cs`](Autoload/GameInputBootstrap.cs)
  subclasses the facade's `GameInputRuntime`, which initializes GameInput on
  startup and polls it every frame from the `game_input/*` project settings.
* [`SelfTest/GameInputSelfTest.cs`](SelfTest/GameInputSelfTest.cs) is the
  integration self-test.

## Quick start

1. **Build the addon** from the repo root:

   ```powershell
   cmake --preset default
   cmake --build build --preset debug
   ```

2. **Build the C# project** with the .NET 8 SDK or later:

   ```powershell
   dotnet build sample\tutorial_gameinput_csharp\TutorialGameInputCSharp.csproj
   ```

3. **Open the project in a .NET build of Godot 4.7** (the project uses
   `Godot.NET.Sdk/4.7.1`) and run `main.tscn`, with a GameInput-supported
   gamepad plugged in.

## Integration self-test

```powershell
$env:GODOT_MONO = '<path to a Godot .NET console executable>'
pwsh -NoProfile -File .\tools\run_gameinput_selftest.ps1 -Project sample\tutorial_gameinput_csharp
```

The runner builds the project with `dotnet build`, imports it if the addon
is not registered in it yet, runs the self-test headless and exits with its
exit code; the report, Godot's output and the build log land in
`build\selftest\gameinput\`. It needs a .NET build of Godot — `GODOT_MONO`
or `-Godot` — and refuses a standard build.

The GDScript sample's self-test covers the addon itself. This one covers
what the facade adds on top of it, and the C# tutorial's wiring:

| Check | What it verifies |
| --- | --- |
| `api.facade` | Every facade enum member has the value of the native constant in the loaded extension. |
| `runtime.initialized` | The bootstrap initialized the real GameInput runtime. |
| `runtime.timestamp` | `GameInput.CurrentTimestamp` advances. |
| `runtime.devices` | The facade wraps every connected device with a unique id, a name and `IsConnected`, and `GetConnectedDeviceCount` agrees with `GetDevices`. |
| `mock.session` | The addon's debug-only mock backend takes over. |
| `sample.hotplug_ui` | A scripted pad reaches `DeviceConnected`, the device list, the count and the hot-plug log. |
| `sample.action_bridge` | A presses `jump` and `ui_accept`; the stick drives `move_left` / `move_right`. |
| `sample.player_jump` | The player jumps with the Step 7 rumble and moves. |
| `sample.disconnect_releases` | Unplugging the pad mid-press releases the action and updates the UI. |
| `mock.typed_events` | `DeviceStatusChanged`, `SystemButtonsChanged`, `KeyboardLayoutChanged` and `ReadingReceived` deliver typed arguments. |
| `mock.vibration` | `StartVibration` maps weak and strong like `Input.StartJoyVibration` (checked through `VibrationStrength`), drives the impulse triggers and stops on time. |
| `mock.force_feedback` | An effect is created, started, re-gained, stopped and disposed through the facade. |
| `sample.exit_tree` | Leaving the tree unsubscribes `Main` from the static events, and re-entering subscribes it again. |
| `mock.restore` | The real runtime is back. |

It takes the same flags after `--` as the GDScript self-test —
`--gameinput-report=<path>`, `--gameinput-strict`,
`--gameinput-session-locked` and `--gameinput-timeout=<sec>` — writes the same
report (schema `gameinput-selftest/1`, with `"harness": "csharp"`) and uses
the same exit codes: `0` pass, `1` a check failed (or skipped under strict),
`2` the harness could not run, `3` the watchdog expired. The virtual-pad
checks are in the GDScript self-test only.

## See also

- [Using the GameInput addon from C#](../../docs/gameinput/csharp.md)
- [GameInput plugin reference](../../docs/gameinput/plugin.md)
- GDScript version: [`sample/tutorial_gameinput/`](../tutorial_gameinput/README.md)
