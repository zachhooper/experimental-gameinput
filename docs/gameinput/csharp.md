# Using the GameInput addon from C# (`godot_gameinput_csharp`)

`godot_gameinput_csharp` is the managed facade over the native `godot_gameinput`
GDExtension. Unlike the GDK and PlayFab facades, the GameInput API is **fully
synchronous** (poll-based) and is shaped to integrate with Godot's
`Input`/`InputMap` flow rather than an async result model — so there is no
`Task`/`Result` bridge here, just typed wrappers, events, and an action-map
bridge.

Everything in [`plugin.md`](plugin.md) applies; this page covers what is
different in C#.

## Setup

1. Build the native addon (`cmake --build build --preset debug`).
2. Reference the facade:

   ```xml
   <ItemGroup>
     <ProjectReference Include="addons/godot_gameinput_csharp/GodotGameInputCSharp.csproj" />
   </ItemGroup>
   ```

3. Add a solution file next to `project.godot`. Godot's .NET export plugin
   **requires** `<assembly_name>.sln` in the project root — without it every
   export fails with *"This project contains C# files but no solution file was
   found"*. Include the facade project(s) so solution builds use the same
   `Debug` / `ExportDebug` / `ExportRelease` configuration. See
   [`../gdk/csharp.md`](../gdk/csharp.md#setup) and the `.sln` files in
   `sample/tutorial_gameinput_csharp/` for the expected shape.

4. (Optional) C# bootstrap autoload — initializes on startup, polls each frame,
   and (when `game_input/mapper/default_action_map` points at a
   `GameInputActionMap`) spawns a default `GameInputMapper` child, driven by the
   same `game_input/*` project settings the GDScript bootstrap reads:

   ```csharp
   // Autoload/GameInputBootstrap.cs
   public partial class GameInputBootstrap : GodotGameInput.Runtime.GameInputRuntime { }
   ```

   ```ini
   [autoload]
   GameInputRuntime="*res://Autoload/GameInputBootstrap.cs"
   ```

## Naming

The facade follows the GDScript API with C# names:

* Methods are PascalCase: `get_current_reading()` is
  `GameInput.GetCurrentReading()`, `start_vibration()` is
  `GameInputDevice.StartVibration()`.
* Getters without arguments are properties: `get_status()` is
  `GameInputDevice.Status`, `get_mouse_delta()` is
  `GameInputReading.MouseDelta`, `get_current_timestamp()` is
  `GameInput.CurrentTimestamp`, `has_gap_before()` is
  `GameInputReading.HasGapBefore`.
* Enums are nested and typed: `GameInput.DeviceKind`, `GameInput.FocusPolicy`,
  `GameInputDevice.Button`, `GameInputDevice.Axis`, `GameInputDevice.Source`,
  `GameInputDevice.DeviceStatus`, `GameInputDevice.SystemButton`,
  `GameInputDevice.MouseButton`, `GameInputForceFeedbackEffect.EffectKind`
  and the rest. Their values are the native constants.
* Signals are static events with typed arguments (see [Events](#events)).

## Devices, readings, and haptics

```csharp
using Godot;
using GodotGameInput;

if (!GameInput.IsAvailable) { return; }

int pads = GameInput.GetConnectedDeviceCount(GameInput.DeviceKind.Gamepad);

GameInputDevice pad = GameInput.GetPrimaryDevice(GameInput.DeviceKind.Gamepad);
if (pad != null)
{
    GameInputReading reading = GameInput.GetCurrentReading(pad);
    if (reading.WasButtonPressed(GameInputDevice.Button.A) && pad.SupportsVibration)
    {
        // weak 0.2, strong 0.4, stopped by Poll() after 0.12 s
        pad.StartVibration(0.2f, 0.4f, 0.12f);
    }
    float moveX = reading.GetAxis(GameInputDevice.Axis.LeftX);
}

GameInputDevice keyboard = GameInput.GetPrimaryDevice(GameInput.DeviceKind.Keyboard);
if (keyboard != null && GameInput.GetCurrentReading(keyboard).WasPhysicalKeyPressed(Key.W))
{
    GD.Print("W position pressed, whatever the layout");
}

GameInputDevice mouse = GameInput.GetPrimaryDevice(GameInput.DeviceKind.Mouse);
Vector2 look = mouse != null ? GameInput.GetCurrentReading(mouse).MouseDelta : Vector2.Zero;
```

`GameInput.GetConnectedDeviceCount()` defaults to `DeviceKind.All`, which
also counts keyboards and mice; pass `DeviceKind.Gamepad` to count pads.
`GameInput.SetVibration(pad, lowFreq, highFreq)` still works and takes the
low-frequency motor first; `StartVibration` takes the weak (high-frequency)
motor first, like Godot's `Input.StartJoyVibration`.

## Events

The facade raises the native signals as static events, on the main thread,
from `GameInput.Poll()`:

| Event | Arguments |
| --- | --- |
| `DeviceConnected` | `GameInputDevice device` |
| `DeviceDisconnected` | `long deviceId` |
| `DeviceStatusChanged` | `GameInputDevice device, DeviceStatus status, DeviceStatus previous, long timestamp` |
| `ReadingReceived` | `GameInputDevice device, GameInputReading reading` |
| `SystemButtonsChanged` | `GameInputDevice device, SystemButton buttons, SystemButton previous, long timestamp` |
| `KeyboardLayoutChanged` | `GameInputDevice device, long layout, long previous, long timestamp` |

The facade connects to the native signals the first time a call reaches the
singleton — the bootstrap autoload's first `Initialize()` or `Poll()` does
that — so handlers added earlier start receiving events from then on.

Because the events are static, a handler keeps its node reachable and keeps
being called after the node leaves the tree. Subscribe in `_EnterTree()` and
unsubscribe in `_ExitTree()`, with a method rather than a lambda so the
unsubscribe matches:

```csharp
public override void _EnterTree()
{
    GameInput.DeviceConnected += OnDeviceConnected;
    GameInput.DeviceDisconnected += OnDeviceDisconnected;
}

public override void _ExitTree()
{
    GameInput.DeviceConnected -= OnDeviceConnected;
    GameInput.DeviceDisconnected -= OnDeviceDisconnected;
}

private void OnDeviceConnected(GameInputDevice device) => GD.Print($"connected: {device.DisplayName}");
private void OnDeviceDisconnected(long deviceId) => GD.Print($"disconnected: {deviceId}");
```

## Event-driven readings

```csharp
public override void _EnterTree()
{
    GameInput.SetReadingCallbackKinds(GameInput.DeviceKind.Gamepad);
    GameInput.ReadingReceived += OnReading;
}

public override void _ExitTree() => GameInput.ReadingReceived -= OnReading;

private void OnReading(GameInputDevice device, GameInputReading reading)
{
    if (reading.WasButtonPressed(GameInputDevice.Button.A))
    {
        GD.Print($"A pressed at {reading.GetTimestamp()} us");
    }
}
```

`GameInput.GetBufferedReadings(device)` returns the same readings for the last
poll, and `GameInput.DroppedReadingCount` counts the readings dropped because
the game polled too rarely. The details are in
[plugin.md](plugin.md#event-driven-readings).

## Force feedback

`GameInputForceFeedbackEffect` wraps the native effect. The native effect is
released when the last reference to it goes away, and a C# reference lasts
until the garbage collector runs, so release it explicitly: call `Release()`
or `Dispose()`, or use `using`.

```csharp
private GameInputForceFeedbackEffect _bump;

private void PlayBump(GameInputDevice wheel)
{
    if (wheel.ForceFeedbackMotorCount == 0) { return; }
    _bump?.Dispose();
    _bump = wheel.CreateForceFeedbackEffect(0, new Godot.Collections.Dictionary
    {
        { "kind", (int)GameInputForceFeedbackEffect.EffectKind.Constant },
        { "magnitude", 0.4f },
        { "sustain_duration", 0.25f },
    });
    _bump?.Start();
}

public override void _ExitTree() => _bump?.Dispose();
```

The parameter keys are the ones listed in the `GameInputForceFeedbackEffect`
class reference.

## The action-map bridge

`GameInputActionMap` + `GameInputBinding` + `GameInputMapper` wrap the native
authoring types. Build a map in code and let the mapper drive Godot's
`InputMap` every frame so the rest of your game keeps using
`Input.IsActionPressed("jump")`:

```csharp
GameInputActionMap map = GameInputActionMap.Create();

GameInputBinding jump = GameInputBinding.Create();
jump.Action = "jump";
jump.Source = GameInputDevice.Source.BtnA;
map.AddBinding(jump);

GameInputBinding left = GameInputBinding.Create();
left.Action = "move_left";
left.Source = GameInputDevice.Source.AxisLeftX;
left.IsAxis = true;
left.AxisInvert = true;
map.AddBinding(left);

GameInputMapper mapper = GameInputMapper.Create();
mapper.ActionMap = map;
AddChild(mapper.Node);   // add the underlying node to the scene tree
```

The actions you bind to (`"jump"`, `"move_left"`, …) must already exist in the
project's `InputMap`; the mapper additively refreshes both polled state and
`InputEventAction` delivery.

## Sample and self-test

[`sample/tutorial_gameinput_csharp`](../../sample/tutorial_gameinput_csharp)
is the C# version of the tutorial: action bridge, hot-plug log, gamepad count
and rumble on jump. It also has a `--gameinput-selftest` mode that checks the
facade against the native API, and the sample's wiring and the facade's
events, vibration and force feedback against the mock backend, then writes a
JSON report and exits non-zero on failure. Run it with a .NET build of Godot:

```powershell
$env:GODOT_MONO = 'D:\Godot\Godot_v4.7.1-stable_mono_win64\Godot_v4.7.1-stable_mono_win64_console.exe'
pwsh -NoProfile -File .\tools\run_gameinput_selftest.ps1 -Project sample\tutorial_gameinput_csharp
```

The runner builds the project with `dotnet build`, imports it when needed and
prints a summary. See
[the sample README](../../sample/tutorial_gameinput_csharp/README.md).

## Parity guarantee

`tests/csharp/FacadeParity.Tests` asserts that every method, property and
signal in the native class reference has a wrapper, and that every native
constant has a facade enum member with the same value. Run via
`tools/run_csharp_tests.ps1`. The sample self-test repeats the constant check
at run time against the loaded extension.
