# GameInput addon (`godot_gameinput`)

`godot_gameinput` is a standalone GDExtension addon that brings the Microsoft
GameInput API to Godot 4.x on Windows. It works independently of the
`godot_gdk` addon — you can ship one, both, or neither. Target machines still
need a compatible GameInput runtime/redist installed (for example
`GameInputRedist.msi`).

The addon gives GDScript first-class access to:

* The GameInput runtime (initialize, shutdown, per-frame poll) and its clock
* Every device kind GameInput reports — gamepads, keyboards, mice, arcade
  sticks, flight sticks, racing wheels, motion sensors and raw controllers —
  with device information, status and button labels for prompts
* Per-frame readings of every input kind except raw device reports, with
  edge-detected press / release, and optional event-driven readings that
  keep every sample between two frames
* Vibration in the shape of `Input.start_joy_vibration` (weak / strong motors,
  a duration, impulse triggers), force-feedback effects, and haptic
  information
* Guide and Share buttons, keyboard layout changes, device status changes,
  the focus policy and aggregate devices
* Hot-plug and every other GameInput event as signals on the main thread
* An inspector-friendly **action bridge** (`GameInputBinding` + `GameInputActionMap` +
  `GameInputMapper`) that drives Godot's `Input` / `InputMap` system from any
  GameInput device.

The C# facade over the same API is described in [`csharp.md`](csharp.md).

## Status

| Feature | Status |
| --- | --- |
| `GameInput` engine singleton | Shipped |
| Devices + readings + vibration | Shipped |
| `GameInputDevice.get_device_info()` | Shipped (issue #23, device-info half), extended in v2 |
| `GameInputBinding` / `GameInputActionMap` / `GameInputMapper` | Shipped; v2 adds arcade stick, flight stick and racing wheel sources |
| `EditorPlugin` autoload installer + Project Settings | Shipped |
| Keyboard, mouse, motion sensors, arcade stick, flight stick, racing wheel, raw controller readings | Shipped (v2) |
| Event-driven readings (`reading_received`, `get_buffered_readings()`) | Shipped (v2) |
| `start_vibration()` with a duration, trigger rumble, supported motors | Shipped (v2) |
| Force feedback (`GameInputForceFeedbackEffect`) | Shipped (v2); needs hardware sign-off, see [manual tests](manual-tests.md) |
| Haptic information (`get_haptic_info()`) | Shipped (v2); waveform playback not provided |
| Guide / Share buttons, keyboard layout, device status signals | Shipped (v2) |
| Focus policy, aggregate devices, GameInput clock | Shipped (v2) |
| Debug-only mock backend for tests | Shipped (v2); compiled out of release builds |
| Sample integration (`sample/tutorial_gameinput/`, `sample/tutorial_gameinput_csharp/`) | Shipped, with a live device inspector and a `--gameinput-selftest` integration check; tutorial-track panel deferred |
| Headless test suite | Shipped |
| Manual hardware checklist ([docs/gameinput/manual-tests.md](manual-tests.md)) | Shipped |
| Raw device reports, `IGameInputMapper` labels, dispatcher control, haptic waveform playback, XInput fallback | Not covered — see [Not covered yet](#not-covered-yet) |

## Building

```powershell
# Build only the GameInput addon
cmake --preset gameinput-only
cmake --build --preset debug-gameinput

# Or build everything (both addons)
cmake --preset default
cmake --build build --preset debug
```

DLLs land in `addons/godot_gameinput/bin/` and are copied into every sample
project's `addons/godot_gameinput/` by the build's sample-sync step.

## Adding the addon to your Godot project

1. Copy `addons/godot_gameinput/` into your project (or symlink it).
2. Open the project once — Godot will detect `plugin.cfg`.
3. Project → Project Settings → Plugins → enable **Godot GameInput**.

Enabling the plugin installs an autoload called `GameInputBootstrap` that
reads project settings and runs the lifecycle for you:

| Setting | Default | Behaviour |
| --- | --- | --- |
| `game_input/runtime/initialize_on_startup` | `false` | When `true`, the bootstrap calls `GameInput.initialize()` on `_ready`. |
| `game_input/runtime/auto_poll` | `true` | When `true`, the bootstrap calls `GameInput.poll()` from `_process`. |
| `game_input/runtime/singleton_name` | `"GameInput"` | Name the extension registers its Engine singleton under. See [Renaming the singleton](#renaming-the-singleton). |
| `game_input/runtime/reading_callback_kinds` | `0` (off) | `DeviceKind` flags whose every reading is delivered through `reading_received`. Read by `initialize()`; `set_reading_callback_kinds()` overrides it. See [Event-driven readings](#event-driven-readings). |
| `game_input/runtime/focus_policy` | `0` (default policy) | `FocusPolicy` flags applied by `initialize()`; `set_focus_policy()` overrides it. See [Focus policy](#focus-policy). |
| `game_input/mapper/default_action_map` | `""` | Path to a `.tres` `GameInputActionMap`. When set, the bootstrap spawns a `GameInputMapper` named `DefaultMapper` as its own child and assigns the loaded resource — so your project's `InputMap` can be driven from a GameInput action map without dropping a Mapper node into any scene. User-placed `GameInputMapper` nodes do not consult this setting; assign their `action_map` explicitly. |

Disabling the plugin removes the autoload — there is no orphaned state.

## Renaming the singleton

`game_input/runtime/singleton_name` controls the name the extension registers
its Engine singleton under, for titles that need to avoid a collision with an
existing global or prefer a project-specific name.

The extension reads the setting **once, at load time**, before any project
script runs — so it must already be in `project.godot` (or an `override.cfg`)
when Godot starts. Changing it at runtime has no effect until the next launch.

A configured name is rejected, with a warning, when it is blank, is not a valid
ASCII identifier, or collides with an already-registered singleton. In those
cases the extension stays registered as `GameInput` so the addon never becomes
unreachable.

Once renamed, the `GameInput` global no longer resolves in GDScript. Look the
singleton up by name instead:

```gdscript
var singleton_name: String = ProjectSettings.get_setting(
        "game_input/runtime/singleton_name", "GameInput")
var gi: Object = Engine.get_singleton(singleton_name)
gi.poll()
```

The bundled `GameInputBootstrap` autoload, the C# `GameInput` facade, and the
shared GUT test base all resolve the singleton this way, so they keep working
across a rename with no further changes. They also verify the resolved object is
really a `GameInput` instance, so a configured name that happens to match an
unrelated engine singleton (`Input`, say) falls back to the real runtime instead
of silently binding to the wrong object.

If you want full control instead, leave `initialize_on_startup` off and call
the lifecycle yourself; `GameInputMapper` nodes also call `poll()` defensively
(it is per-frame idempotent), so dropping a Mapper into a scene is enough even
without the autoload's `auto_poll`.

## Devices

The addon tracks every device GameInput reports and hands out
`GameInputDevice` wrappers. Filter them with the `GameInput.DeviceKind` flags:

| Flag | Value | Devices |
| --- | --- | --- |
| `DEVICE_GAMEPAD` | 1 | Gamepads (the default for `get_devices()` and `get_primary_device()`) |
| `DEVICE_KEYBOARD` | 2 | Keyboards |
| `DEVICE_MOUSE` | 4 | Mice |
| `DEVICE_ALL` | 7 | Gamepads, keyboards and mice — the original meaning, kept for existing projects (the default for `get_connected_device_count()`) |
| `DEVICE_ARCADE_STICK` | 8 | Arcade sticks |
| `DEVICE_FLIGHT_STICK` | 16 | Flight sticks |
| `DEVICE_RACING_WHEEL` | 32 | Racing wheels |
| `DEVICE_SENSORS` | 64 | Devices with motion sensors |
| `DEVICE_CONTROLLER` | 128 | Devices with raw controller axes, buttons and switches |
| `DEVICE_ANY` | 255 | Every kind |

A device matches a mask when it supports any kind in it, so a device that
reports both gamepad and raw controller input appears under
`DEVICE_GAMEPAD` and under `DEVICE_CONTROLLER`.

```gdscript
var gi = Engine.get_singleton("GameInput")
for device in gi.get_devices(GameInput.DEVICE_ANY):
    print("%s: kinds %d, family %d" % [device.get_display_name(),
            device.get_kind_mask(), device.get_device_family()])
var wheel: GameInputDevice = gi.get_primary_device(GameInput.DEVICE_RACING_WHEEL)
var pads: int = gi.get_connected_device_count(GameInput.DEVICE_GAMEPAD)
```

What a device can tell you:

* `get_device_info()` — a `Dictionary` with the vendor and product ids,
  family, supported input kinds, rumble motors and system buttons, app-local
  and container ids, PnP path, versions and, per kind, the keyboard layout and
  key counts, the mouse buttons and wheels, the sensors, the racing wheel's
  clutch, handbrake, shifter and wheel angle, and one entry per
  force-feedback motor. Keys are only ever added; the full list is in the
  class reference.
* `get_status()` and the `device_status_changed(device, status,
  previous_status, timestamp)` signal — `STATUS_CONNECTED` and
  `STATUS_HAPTIC_INFO_READY`. The signal only reports changes after the
  connect: flags that arrive with the connect are already in `get_status()`
  when `device_connected` fires, so check both places when waiting for
  `STATUS_HAPTIC_INFO_READY`.
* `get_button_label(source)` — the label GameInput reports for a button, as
  a snake_case name (`"xbox_a"`, `"icon_cross"`, `"letter_a"`, …), to pick
  the right button prompt.
* `get_supported_rumble_motors()`, `get_supported_system_buttons()`,
  `get_keyboard_layout()`, `get_app_local_id()`.

## Readings

`GameInput.poll()` refreshes every connected device once per frame with one
`GetCurrentReading()` per input kind the device supports.
`get_current_reading(device)` returns that state as a `GameInputReading`,
which also keeps the state it replaced, so the `was_*_pressed` /
`was_*_released` methods report edges. `get_input_kinds()` tells which kinds
a reading holds; methods for other kinds return `false`, `0` or empty values.

| Input kind | Reading methods |
| --- | --- |
| Gamepad | `is_button_down` / `was_button_pressed` / `was_button_released` (with `GameInputDevice.Button` masks; several bits are a chord), `get_axis` (`Axis`; stick Y is flipped so down is positive, like Godot), `get_buttons_mask`. `BUTTON_LEFT_STICK_UP` and friends turn stick directions into buttons; `BUTTON_PADDLE_*` are the Elite paddles. |
| Keyboard | `get_pressed_physical_keys()` (Godot `Key` values by position, like `InputEventKey.physical_keycode`), `is_physical_key_down` / `was_physical_key_pressed` / `was_physical_key_released`, `get_key_states()` (scan code, virtual key, character, dead key, location). |
| Mouse | `get_mouse_buttons()`, `is_mouse_button_down` and edges, `get_mouse_delta()`, `get_mouse_wheel_delta()`, `get_mouse_absolute_position()`, `get_mouse_state()`. |
| Motion sensors | `get_accelerometer()` (m/s²), `get_gyroscope()` (rad/s), `get_orientation()` (`Quaternion`), `get_heading_degrees()` and `get_heading_accuracy()`, `get_sensor_kinds()`. |
| Arcade stick | `get_arcade_stick_buttons()` (`ArcadeStickButton`). |
| Flight stick | `get_flight_stick_buttons()` (`FlightStickButton`), `get_flight_stick_hat()` / `get_flight_stick_hat_vector()`, and the `AXIS_FLIGHT_*` axes. |
| Racing wheel | `get_racing_wheel_buttons()` (`RacingWheelButton`), `get_racing_wheel_gear()`, and the `AXIS_WHEEL`, `AXIS_THROTTLE`, `AXIS_BRAKE`, `AXIS_CLUTCH`, `AXIS_HANDBRAKE` axes. |
| Raw controller | `get_controller_axes()`, `get_controller_buttons()`, `get_controller_switches()` and the per-index getters, for devices without a typed layout. |

`GameInputDevice.Source` values give one vocabulary for gamepad, arcade stick,
flight stick and racing wheel inputs: `is_source_down`, `was_source_pressed`,
`was_source_released` and `get_source_value` accept any of them, and they are
what `GameInputBinding.source` takes.

```gdscript
func _process(_delta: float) -> void:
    var gi = Engine.get_singleton("GameInput")
    var keyboard: GameInputDevice = gi.get_primary_device(GameInput.DEVICE_KEYBOARD)
    if keyboard:
        var reading: GameInputReading = gi.get_current_reading(keyboard)
        if reading and reading.was_physical_key_pressed(KEY_W):
            print("W position pressed, whatever the layout")
    var mouse: GameInputDevice = gi.get_primary_device(GameInput.DEVICE_MOUSE)
    if mouse:
        var look: Vector2 = gi.get_current_reading(mouse).get_mouse_delta()
```

Timestamps — `GameInputReading.get_timestamp()`, the `timestamp` argument of
the signals and `GameInput.get_current_timestamp()` — are microseconds on the
GameInput clock. Compare them with each other, not with
`Time.get_ticks_usec()`: `gi.get_current_timestamp() - reading.get_timestamp()`
is how old the input is.

## Event-driven readings

A polled reading is the latest state, so a button tapped and released between
two frames never shows as down. For inputs where that matters, turn on
reading callbacks for the device kinds you care about:

```gdscript
func _ready() -> void:
    var gi = Engine.get_singleton("GameInput")
    gi.set_reading_callback_kinds(GameInput.DEVICE_GAMEPAD)
    gi.reading_received.connect(_on_reading)

func _on_reading(device: GameInputDevice, reading: GameInputReading) -> void:
    if reading.was_button_pressed(GameInputDevice.BUTTON_A):
        print("A pressed at %d us" % reading.get_timestamp())
```

Or set `game_input/runtime/reading_callback_kinds` in Project Settings.
GameInput calls the addon on a worker thread for every reading; the addon
queues them in a ring of 512 shared by every device and `poll()` emits
`reading_received` for each, in order, before it refreshes the polled state.
Each event reading's previous state is the reading before it, so edges are
exact per event. `get_buffered_readings(device)` returns the same readings
for the last poll if you prefer to iterate instead of connecting. When the
game polls too rarely the oldest readings are dropped:
`get_dropped_reading_count()` counts them and the next reading of each device
that lost readings reports `has_gap_before()`. If more than 16 devices lose
readings between two polls, every device's next reading reports the gap.

Changing the mask, or turning callbacks off, discards readings that were
queued but not delivered yet, so no reading of an old registration arrives
after the change. That holds inside a `reading_received` handler as well:
the rest of that poll's readings are not emitted, while device events still
are. The next reading of every device reports `has_gap_before()`.

## Vibration and haptics

`GameInputDevice.start_vibration()` follows `Input.start_joy_vibration()`:

```gdscript
var pad: GameInputDevice = gi.get_primary_device()
if pad and pad.supports_vibration():
    # weak (high-frequency) 0.2, strong (low-frequency) 0.4, for 0.12 s
    pad.start_vibration(0.2, 0.4, 0.12)
    # impulse triggers on devices that have them
    pad.start_vibration(0.0, 0.0, 0.3, 0.5, 0.5)
```

A positive `duration` is stopped by `poll()` once it elapses; `0` vibrates
until `stop_vibration()` or the next call. `is_vibrating()`,
`get_vibration_strength()` (`Vector2(weak, strong)`, like
`Input.get_joy_vibration_strength()`), `get_trigger_vibration_strength()`,
`get_vibration_duration()` and `get_vibration_remaining_duration()` report the
current state, and `get_supported_rumble_motors()` lists the motors.
`GameInput.set_vibration(device, low_freq, high_freq, left_trigger,
right_trigger)` and `stop_haptics(device)` keep working; note that
`set_vibration` takes the low-frequency motor first.

`supports_haptics()` and `get_haptic_info()` report the haptic locations
(grips, triggers) and the audio endpoint GameInput plays haptic waveforms
through. The addon does not play waveforms.

## Force feedback

Racing wheels and flight sticks with force-feedback motors report them in
`get_force_feedback_motor_count()` and `get_force_feedback_motor_info(index)`
(supported axes and effect kinds). Create an effect with a parameter
`Dictionary`, keep a reference to it for as long as it should play, and
control it through `GameInputForceFeedbackEffect`:

```gdscript
var bump: GameInputForceFeedbackEffect

func play_bump(wheel: GameInputDevice) -> void:
    if wheel.get_force_feedback_motor_count() == 0:
        return
    bump = wheel.create_force_feedback_effect(0, {
        "kind": GameInputForceFeedbackEffect.EFFECT_CONSTANT,
        "magnitude": 0.4,
        "sustain_duration": 0.25,
    })
    if bump:
        bump.start()
```

Constant, ramp, the periodic waves (sine, square, triangle, sawtooth) and the
conditions (spring, friction, damper, inertia) are supported; the parameter
keys, their ranges and defaults are listed in the
`GameInputForceFeedbackEffect` class reference. Durations are in seconds, a
magnitude is a `float` applied along every axis the motor supports or a
per-axis `Dictionary`, and bad keys or values fail the call with a warning
that gives the reason. Conditions follow the GDK's signs: negative
coefficients resist the player, so a spring created with only its `kind`
pulls the wheel back to centre, and `max_negative_magnitude` is negative
(for example `-0.5`). An effect is released by `release()`, when its last
reference goes away, when its device disconnects and at shutdown.
`set_force_feedback_motor_gain()` and `is_force_feedback_motor_powered_on()`
work on the motor itself.

## Guide and Share buttons, keyboard layout

`system_buttons_changed(device, buttons, previous_buttons, timestamp)` fires
when the Guide or Share button is pressed or released (`SystemButton` flags),
and `GameInputDevice.get_system_buttons()` returns the current state.
`keyboard_layout_changed(device, layout, previous_layout, timestamp)` fires
when a keyboard's layout changes. Whether the game receives the system
buttons depends on the platform and the focus policy.

## Focus policy

By default the game receives input — and the Guide and Share buttons — only
while it has focus. `set_focus_policy()` or
`game_input/runtime/focus_policy` changes that with `FocusPolicy` flags:
`FOCUS_POLICY_ENABLE_BACKGROUND_INPUT` keeps input flowing while the window
is in the background, and the `FOCUS_POLICY_EXCLUSIVE_FOREGROUND_*` flags
keep other processes from seeing the game's input while it has focus.
GameInput applies focus policy on Windows only.

## Aggregate devices

`create_aggregate_device(kind)` asks GameInput for a virtual device that
combines every connected device of one kind, and returns its app-local id.
`kind` must be exactly one of `DEVICE_GAMEPAD`, `DEVICE_KEYBOARD`,
`DEVICE_MOUSE`, `DEVICE_ARCADE_STICK`, `DEVICE_FLIGHT_STICK` or
`DEVICE_RACING_WHEEL`; for anything else, `DEVICE_ALL` and other
combinations included, it warns and returns an empty string. Create one
aggregate per kind you need.
The aggregate arrives through `device_connected` like any device, with
`FAMILY_AGGREGATE` as its family and the name `GameInput Aggregate Device`
when GameInput gives it none. `disable_aggregate_device(id)` stops it
producing readings but leaves it in `get_devices()`; calling
`create_aggregate_device()` again for the same kind re-enables it under the
same id.

Rumble sent to an enabled aggregate reaches its member pads, and a timed
`start_vibration()` on the aggregate stops them on time; the self-test's
`vpad.aggregate_rumble` check verifies this against a virtual Xbox 360 pad.
Once the aggregate is disabled, rumble sent to it no longer reaches them.
The aggregate reports all four rumble motors (`get_supported_rumble_motors()`
returns `15`) even when its only member has just the low- and
high-frequency motors, so check a member pad's motors before you rely on
trigger rumble.

## Quick recipes

### Rumble a pad on a button press

```gdscript
extends Node

func _ready() -> void:
    var gi = Engine.get_singleton("GameInput")
    if not gi.is_initialized():
        gi.initialize()
    gi.device_connected.connect(func(device): print("connected: ", device.get_display_name()))

func _process(_delta: float) -> void:
    var gi = Engine.get_singleton("GameInput")
    if not gi.is_initialized():
        return
    gi.poll()
    var pad: GameInputDevice = gi.get_primary_device()
    if pad == null:
        return
    var reading: GameInputReading = gi.get_current_reading(pad)
    if reading != null and reading.was_button_pressed(GameInputDevice.BUTTON_A):
        pad.start_vibration(0.3, 0.6, 0.15)
```

### Drive Godot actions with a `GameInputMapper`

In the editor:

1. Right-click a folder → **Create New Resource** → `GameInputActionMap`.
2. Edit the map; add `GameInputBinding` rows. For each row:
   * `action` — a Godot action name that already exists in
     **Project Settings → Input Map** (e.g. `&"jump"`).
   * `source` — a `GameInputDevice.SRC_*` value
     (e.g. `SRC_BTN_A`, `SRC_AXIS_LEFT_X`, `SRC_WHEEL_NEXT_GEAR`,
     `SRC_AXIS_THROTTLE`).
   * `is_axis` — toggle on for thumbsticks / triggers.
   * `axis_threshold` — for axis-as-button, fire `action_press` when
     `|value| >= threshold`.
   * `axis_invert` — flip sign before evaluating. Handy for thumbstick Y in
     Godot's "down positive" convention.
   * `deadzone` — values within `[-deadzone, deadzone]` are clamped to 0.
3. Save the resource.
4. In your scene, add a `GameInputMapper` node and assign the action map to
   its `action_map` property — *or* set the project-wide
   `game_input/mapper/default_action_map` to your `.tres` and skip the node
   entirely (the bootstrap spawns a `DefaultMapper` for you).

`target_kind_mask` picks the device kinds a mapper follows
(`KIND_GAMEPAD` by default; `KIND_ARCADE_STICK`, `KIND_FLIGHT_STICK` and
`KIND_RACING_WHEEL` for the specialty devices) and `target_device_id` pins it
to one device.

The Mapper calls `Input.action_press(action, strength)` /
`Input.action_release(action)` each frame, so the rest of your code can stay
on Godot's standard `Input.is_action_pressed("jump")` / `Input.get_axis()`
APIs and gain GameInput device support transparently. If the mapper stops
driving a held binding — the node exits the tree, you swap or mutate the
active `GameInputActionMap`, the target device disappears, or the frame cannot
produce a fresh reading — it releases the actions it previously held before
clearing its per-binding cache. Runtime `InputMap` edits also refresh the
native-event suppression cache by the next frame. On every press / release
transition the Mapper also pushes an `InputEventAction` through
`Input.parse_input_event` so event-driven consumers — UI focus traversal for
`ui_*`, `_gui_input` listeners, `_input` / `_unhandled_input` handlers —
actually see the action change. When Godot's built-in joypad backend is
already wired to deliver the same action through a matching
`InputEventJoypadButton` / `InputEventJoypadMotion` in your `InputMap` (the
default project mapping for `ui_accept` etc.), the Mapper suppresses its own
`InputEventAction` for that binding so menu actions fire exactly once per
physical press instead of twice. For this check the Elite paddles match
Godot's `JOY_BUTTON_PADDLE1` to `JOY_BUTTON_PADDLE4`, and the digital trigger
buttons and thumbstick directions (`SRC_BTN_LEFT_TRIGGER`,
`SRC_BTN_LEFT_STICK_UP` and so on) match an `InputEventJoypadMotion` on the
same axis in the same direction. Stick Y is down-positive in Godot, so a
stick-up binding matches a negative `axis_value`, which is how the default
`ui_up` action lists the left stick. Arcade stick, flight stick and racing
wheel sources, including the wheel and flight axes, have no standard Godot
joypad equivalent, so the Mapper never suppresses them. If Godot's own joypad
backend also reports such a device, bind its actions through the Mapper only;
an action bound through both paths fires once from each.

### Soft-fail behaviour

`GameInput.set_vibration()` and `GameInputDevice.start_vibration()` return
`false` when preflight fails (null or disconnected device, no rumble motors,
uninitialized runtime) and also when an HRESULT-returning GameInput SDK
reports a native `SetRumbleState` failure, so callers can skip timers or
surface diagnostics.

Every public method on `GameInput`, `GameInputDevice`, and `GameInputReading`
returns a safe default and emits a single `push_warning` if called before
`initialize()`, after `shutdown()`, or on a host where GameInput is
unavailable. `GameInput.get_connected_device_count()` is the exception —
it reports the cached device count (or `0`) without warning, so polling it
from a HUD does not spam the log. Your scene won't crash if the addon
isn't ready yet — checks like `if gi.is_initialized():` are optional, just
preferred for clarity.

## Hot-plug / threading model

* GameInput reports device connections, status changes, Guide and Share
  presses, keyboard layout changes and, when enabled, readings on worker
  threads. The addon only enqueues them there, under a mutex, and `poll()`
  emits the Godot signals on the **main thread** in the order the events
  happened.
* `GameInputDevice` and `GameInputForceFeedbackEffect` wrappers hold a
  session-local monotonic id, **never** a raw GameInput pointer. Stale
  wrappers stay alive but `is_connected()` / `is_valid()` start returning
  `false` and other methods return safe defaults.
* Device ids are never recycled within a session.
* Shutdown uses `IGameInput::UnregisterCallback` rather than `StopCallback`:
  Microsoft documents `UnregisterCallback` as the point after which callback
  resources may be removed, while `StopCallback` only prevents future dispatch
  (see Microsoft Learn for `IGameInput::UnregisterCallback` and
  `IGameInput::StopCallback`). Every callback the addon registers goes
  through the same in-flight fence, which keeps the callback context, cached
  devices and pending-event queues alive until any in-flight GameInput worker
  callback has finished. If `UnregisterCallback` fails, the addon keeps the
  registration and tries again at the next reading-callback change and once
  more at shutdown, before it releases `IGameInput`. Late callbacks from it
  are ignored meanwhile. A registration GameInput still refuses to remove at
  shutdown is abandoned with a warning: its calls do nothing for the rest of
  the process, and the addon's DLL is pinned in memory so that GameInput never
  calls into unloaded code.
* `poll()` does not nest. Calling it from a handler of a signal that `poll()`
  is emitting does nothing, even if the handler restarted the runtime; the
  next `poll()` delivers whatever the handler queued.
* When the engine quits, the addon shuts the runtime down and disconnects
  every connection to its signals before the script languages are torn down,
  so a lambda that is still connected cannot crash the process on exit.

## Testing without hardware

Debug builds of the addon contain a mock backend that the GUT suites and the
sample self-test use to script devices: inject a gamepad, wheel or keyboard,
push readings, Guide presses and status changes, freeze the clock, and read
back the rumble the addon sent. It is reached through `_test_*` methods on
the singleton, which release builds compile out; they are not part of the
public API and are not in the class reference. The mock feeds the same
queues and signals as the real runtime, so the code under test is the code
that ships.

## In-editor docs

XML class documentation lives in `addons/godot_gameinput/doc_classes/` and is
wired into the addon's CMake `target_doc_sources`. Inside the editor, hover or
press F1 on any `GameInput*` symbol to see the full class reference.

## Sample integration

The standalone `sample/tutorial_gameinput/` project demonstrates the action
bridge end-to-end: a device list, a hot-plug event log, live action
strengths and a jumping player that rumbles the pad. Its **Inspector** panel
lists every GameInput device, shows the selected device's capabilities and
live reading, and has buttons for rumble, impulse triggers and force
feedback, plus toggles for event-driven readings and background input.

The same project is the addon's integration harness. Run it with
`-- --gameinput-selftest` (or through `tools\run_gameinput_selftest.ps1`) and
it checks the API surface, the real runtime and, through the mock backend,
the tutorial's own wiring and every v2 feature, then writes a JSON report and
exits non-zero on failure. With `-VirtualPad` the runner plugs in a ViGEm
virtual Xbox 360 pad (`tools\virtual_gamepad\`) and adds a rumble round trip
checked against what the virtual pad received.
`sample/tutorial_gameinput_csharp/` has the C# counterpart. See
[the sample README](../../sample/tutorial_gameinput/README.md) for the
checks and options.

The GameInput scenario panel inside the GDK/PlayFab tutorial tracks is
deferred to a follow-up PR; until then, the standalone sample and the
[Tutorial — GameInput action bridge](../tutorials/gameinput-action-bridge.md)
are the recommended entry points.

## Testing this addon

`godot_gameinput` is exercised by the `tests\godot\gameinput\` host. Coverage lives under `tests\godot\gameinput\tests\`: the original suites (`test_gameinput_core.gd`, `test_gameinput_device.gd`, `test_gameinput_reading.gd`, `test_gameinput_resource.gd`, `test_gameinput_mapper.gd`, `test_gameinput_mapper_extensions.gd`, `test_gameinput_threading_smoke.gd`, …) and the v2 suites, which drive the mock backend: `test_gameinput_v2_api.gd`, `test_gameinput_mock_devices.gd`, `test_gameinput_mock_readings.gd`, `test_gameinput_mock_keyboard_mouse.gd`, `test_gameinput_event_readings.gd`, `test_gameinput_system_events.gd`, `test_gameinput_haptics.gd`, `test_gameinput_force_feedback.gd` and `test_gameinput_mapper_v2.gd`. `test_gameinput_live_v2.gd` runs against the real runtime in the live tier. Bootstrap autoload checks live under `tests\godot\gameinput\tests\bootstrap\`.

GameInput headless tests are deterministic by default and do not require live XBOX or PlayFab credentials. Hardware-specific behavior such as real controllers, rumble feel, force feedback and hot-plug should still be checked with [`gameinput/manual-tests.md`](manual-tests.md).

Run the standard pipeline from the repository root:

```powershell
pwsh -NoLogo -NoProfile -ExecutionPolicy Bypass -File .\tools\run_all_tests.ps1
```

Run the sample self-test, with or without the virtual pad:

```powershell
pwsh -NoProfile -File .\tools\run_gameinput_selftest.ps1
pwsh -NoProfile -File .\tools\run_gameinput_selftest.ps1 -VirtualPad
```

See [`gdk/sample-and-tests.md`](../gdk/sample-and-tests.md) for the orchestrator stages, GUT layout, bootstrap mini-runners, baselines, and troubleshooting pointers.

## Not covered yet

* Raw HID device reports (`IGameInputRawDeviceReport`).
* `IGameInputMapper` label and mapping information for remapping screens.
* Dispatcher control (`IGameInputDispatcher`); GameInput's own worker threads
  deliver the callbacks.
* Haptic waveform playback, which GameInput routes through an audio endpoint.
* An XInput fallback for hosts without the GameInput runtime.
* Sub-frame taps inside `GameInputMapper`: it reads the polled state, so use
  `reading_received` for inputs that must not miss a tap.

## See also

* `addons/godot_gameinput/CMakeLists.txt` — the build target. Add new sources
  to `_GAMEINPUT_SRCS` and new sync files to `godot_addon_sync_files_to_sample`'s
  FILES list.
* `.github/instructions/godot-gameinput.instructions.md` — path-scoped
  Copilot guidance for changes inside the addon and its samples.
