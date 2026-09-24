# GameInput GDExtension Spec

> **Status: v2 implemented (issue #97).** v1 shipped devices, polling,
> vibration, the action bridge, project settings, the EditorPlugin-installed
> bootstrap autoload, the standalone `sample/tutorial_gameinput/` sample, and
> headless tests under `tests/godot/gameinput/tests/`. v2 adds every
> GameInput reading kind (keyboard, mouse, motion sensors, arcade stick,
> flight stick, racing wheel, raw controller), event-driven readings, device
> status, Guide/Share and keyboard-layout signals, timed and trigger
> vibration, force feedback, haptic information, focus policy, aggregate
> devices, a debug-only mock backend, a device inspector, and a
> `--gameinput-selftest` integration check in both samples. A GameInput panel
> inside the tutorial sample tracks remains deferred.
>
> Deviations from the original sketch are listed in
> [§ Deviations](#deviations-from-the-original-sketch) and
> [§ v2 design decisions](#v2-design-decisions). Items still out of scope are
> in [§ Deferred](#deferred).

## Overview

This document defines a **GDScript-first** plan for the `godot_gameinput` Godot GDExtension plugin.

`godot_gameinput` owns device discovery, polling, callbacks, haptics, and an optional Godot action bridge. It is the companion input document to `gdext-gdk.md`, but it should be able to ship independently on Windows builds that want GameInput without the rest of the GDK service layer.

The core architectural rule is: **C++ is internal; GDScript is the primary public surface**.

## Design goals

1. **GDScript-first API**: snake_case methods, signals, Godot types, no raw native handles.
2. **GDExtension-only**: no custom Godot fork required.
3. **Standalone plugin**: no hard dependency on `godot_gdk`.
4. **Godot-native ergonomics**: wrappers, signals, scene-tree-friendly mapper nodes, optional `Resource` configs.
5. **Graceful failure in editor/non-target runtime**: no crashes if GameInput is unavailable.

## Scope

| Domain | Status | Notes |
| --- | --- | --- |
| Runtime init/shutdown | Shipped (v1) | `GameInputCreate`, callback registration, idempotent teardown |
| Device discovery/lifecycle | Shipped (v1), extended in v2 | connected/disconnected device cache, hot-plug signals on the main thread; v2 adds `device_status_changed` and `get_device_by_id()` |
| Polling API | Shipped (v1), extended in v2 | `GameInput.poll()` is per-frame idempotent; v2 reads every kind a device supports in one `GetCurrentReading` per kind group |
| Gamepad readings | Shipped (v1), extended in v2 | v2 adds the C/Z buttons, trigger and stick-direction buttons, and Elite paddles |
| Keyboard, mouse, motion sensors | Shipped (v2) | physical keys from scan codes, mouse deltas/wheel/absolute position, accelerometer/gyroscope/orientation/heading in SI units |
| Arcade stick, flight stick, racing wheel | Shipped (v2) | typed button masks, axes and hat/gear; mapper sources for all three |
| Raw controller readings | Shipped (v2) | axis, button and switch arrays for devices with no typed layout |
| Reading callbacks | Shipped (v2) | `set_reading_callback_kinds()` → `reading_received` + `get_buffered_readings()`; one fenced `RegisterReadingCallback` feeding a bounded drop-oldest ring |
| System buttons, keyboard layout, device status | Shipped (v2) | `system_buttons_changed`, `keyboard_layout_changed`, `device_status_changed` |
| Vibration/rumble | Shipped (v1), extended in v2 | `SetRumbleState`-backed; `supportedRumbleMotors` checked first; native failures are surfaced as `false` when the SDK reports them; v2 adds Godot-shaped `start_vibration()` with a duration, trigger rumble and vibration state getters |
| Force feedback | Shipped (v2) | `GameInputForceFeedbackEffect` over `IGameInputForceFeedbackEffect`; hardware sign-off pending (see [manual tests](../docs/gameinput/manual-tests.md)) |
| Haptic information | Shipped (v2) | `get_haptic_info()`; waveform playback deferred |
| Focus policy, aggregate devices, GameInput clock | Shipped (v2) | `set_focus_policy()`, `create_aggregate_device()`, `get_current_timestamp()` |
| Mock backend | Shipped (v2), debug builds only | `_test_*` seams under `#ifndef NDEBUG` drive the real pipeline for GUT and the sample self-test |
| Battery state | Removed | GameInput v3 SDK dropped the battery API (`IGameInputDevice::GetBatteryState`, `GameInputBatteryState`) — no replacement upstream |
| Device info | Shipped (v1), extended in v2 | `GameInputDevice.get_device_info()` (issue #23, device-info half); v2 adds family, status, app-local ids, rumble and system-button masks, per-kind capability dictionaries and force-feedback motors; keys are only ever added |
| Godot action bridge | Shipped (v1), extended in v2 | `GameInputMapper` + `GameInputActionMap` + `GameInputBinding`; v2 adds arcade/flight/wheel sources and paddle duplicate detection |
| Project Settings + bootstrap autoload | Shipped (v1) | EditorPlugin installs `GameInputBootstrap` autoload; v2 adds two runtime settings |
| Raw device reports, `IGameInputMapper`, dispatcher control, haptic waveform playback, XInput fallback | Deferred | see [§ Deferred](#deferred) |
| Dependency on `godot_gdk` | None | ships independently |

## Rationale and prior art

This spec borrows the Godot-facing integration patterns that already work well in platform plugins, especially singleton registration, project settings, callback dispatch, and optional Godot-native adapters. The main prior-art reference is [GodotSteam](https://godotsteam.com/) and its active source tree on [Codeberg](https://codeberg.org/godotsteam/godotsteam), which demonstrates the value of native singletons, project settings, callback dispatch, and optional Godot-facing wrapper types in a platform plugin.

### Why GDScript-first wrappers instead of raw native handles

Godot's scripting model is built around first-class [signals](https://docs.godotengine.org/en/stable/classes/class_signal.html), [`RefCounted`](https://docs.godotengine.org/en/stable/classes/class_refcounted.html), [`Node`](https://docs.godotengine.org/en/stable/classes/class_node.html), and [`Resource`](https://docs.godotengine.org/en/stable/classes/class_resource.html) objects. Exposing raw native handles such as `IGameInputDevice*` or `IGameInputReading*` directly to GDScript would fight both Godot ergonomics and Godot lifetime rules.

Wrapping native state in Godot objects such as `GameInputDevice` and `GameInputReading` keeps the public API aligned with normal GDScript usage patterns and hides COM-style lifetime management inside C++.

### Why polling and callbacks are both part of the public contract

[GameInput fundamentals](https://learn.microsoft.com/en-us/gaming/gdk/docs/features/common/input/overviews/input-fundamentals?view=gdk-2510) and [GameInput callbacks](https://learn.microsoft.com/en-us/gaming/gdk/docs/features/common/input/advanced/input-callbacks?view=gdk-2604) support both current-reading polling and event-driven updates. Godot gameplay code often wants both: deterministic per-frame sampling for movement/gameplay logic and connection or reading notifications for UX and device lifecycle.

The plugin should therefore expose both styles instead of forcing one. Polling covers the normal gameplay loop; callbacks keep device caches and connection state current.

### Why `GameInputMapper` exists

GameInput gives raw device state, but Godot gameplay code commonly goes through [`Input`](https://docs.godotengine.org/en/stable/classes/class_input.html) and [`InputMap`](https://docs.godotengine.org/en/stable/classes/class_inputmap.html). The optional `GameInputMapper` exists so GDScript-heavy projects can keep using `Input.is_action_pressed()` and project-defined actions while still benefiting from GameInput's device coverage and haptics.

## Public API conventions

### Global singleton

- `GameInput`

### Wrapper types exposed to GDScript

| Native concept | GDScript wrapper |
| --- | --- |
| `IGameInputDevice` | `GameInputDevice` |
| Input reading state | `GameInputReading` |
| `IGameInputForceFeedbackEffect` | `GameInputForceFeedbackEffect` (v2) |

### General rules

1. Public methods use snake_case and Godot-native types.
2. GDScript-facing values stay within Godot's type system: `bool`, `int`, `float`, `String`, `Dictionary`, `Array`, and `PackedByteArray`.
3. Long-lived script objects use `RefCounted`, `Resource`, or `Node` when lifecycle matters.
4. Raw handles, pointers, and native query structs stay internal to C++.

## Plugin spec

### Root singleton

#### Root API

```gdscript
GameInput.initialize() -> bool
GameInput.shutdown() -> void
GameInput.is_initialized() -> bool
GameInput.poll() -> void
GameInput.get_devices(kind_mask := DEVICE_GAMEPAD) -> Array[GameInputDevice]
GameInput.get_primary_device(kind_mask := DEVICE_GAMEPAD) -> GameInputDevice
GameInput.get_current_reading(device: GameInputDevice) -> GameInputReading
GameInput.set_vibration(device: GameInputDevice, low_freq: float, high_freq: float, left_trigger := 0.0, right_trigger := 0.0) -> bool
GameInput.stop_haptics(device: GameInputDevice) -> void
GameInput.get_connected_device_count(kind_mask := DEVICE_ALL) -> int

# v2
GameInput.get_device_by_id(device_id: int) -> GameInputDevice
GameInput.get_current_timestamp() -> int
GameInput.set_reading_callback_kinds(kind_mask: int) -> bool
GameInput.get_reading_callback_kinds() -> int
GameInput.get_buffered_readings(device: GameInputDevice) -> Array[GameInputReading]
GameInput.get_dropped_reading_count() -> int
GameInput.set_focus_policy(policy: int) -> void
GameInput.get_focus_policy() -> int
GameInput.create_aggregate_device(kind: int) -> String
GameInput.disable_aggregate_device(app_local_id: String) -> bool
```

`DeviceKind` adds `DEVICE_ARCADE_STICK` (8), `DEVICE_FLIGHT_STICK` (16),
`DEVICE_RACING_WHEEL` (32), `DEVICE_SENSORS` (64), `DEVICE_CONTROLLER` (128)
and `DEVICE_ANY` (255). `DEVICE_ALL` keeps its v1 value (7, gamepad |
keyboard | mouse), so `get_connected_device_count()` returns what it did in
v1, when the device cache only held those three kinds. `FocusPolicy` mirrors
`GameInputFocusPolicy`.

#### Signals

```gdscript
device_connected(device: GameInputDevice)
device_disconnected(device_id: int)

# v2
device_status_changed(device: GameInputDevice, status: int, previous_status: int, timestamp: int)
reading_received(device: GameInputDevice, reading: GameInputReading)
system_buttons_changed(device: GameInputDevice, buttons: int, previous_buttons: int, timestamp: int)
keyboard_layout_changed(device: GameInputDevice, layout: int, previous_layout: int, timestamp: int)
```

Every signal is emitted from `poll()` on the main thread. Worker callbacks
only enqueue.

#### `GameInputDevice`

```gdscript
get_device_id() -> int
get_display_name() -> String
get_kind_mask() -> int
is_connected() -> bool
supports_vibration() -> bool
supports_haptics() -> bool
get_device_info() -> Dictionary
button_to_source(button: int) -> int
axis_to_source(axis: int) -> int

# v2: identity and state
get_status() -> int
get_app_local_id() -> String
get_device_family() -> int
get_supported_rumble_motors() -> int
get_supported_system_buttons() -> int
get_system_buttons() -> int
get_keyboard_layout() -> int
get_button_label(source: int) -> String
get_haptic_info() -> Dictionary

# v2: vibration (Godot's Input.start_joy_vibration shape)
start_vibration(weak_magnitude: float, strong_magnitude: float, duration := 0.0, left_trigger := 0.0, right_trigger := 0.0) -> bool
stop_vibration() -> void
is_vibrating() -> bool
get_vibration_strength() -> Vector2
get_trigger_vibration_strength() -> Vector2
get_vibration_duration() -> float
get_vibration_remaining_duration() -> float

# v2: force feedback
get_force_feedback_motor_count() -> int
get_force_feedback_motor_info(motor_index: int) -> Dictionary
is_force_feedback_motor_powered_on(motor_index: int) -> bool
set_force_feedback_motor_gain(motor_index: int, gain: float) -> bool
create_force_feedback_effect(motor_index: int, params: Dictionary) -> GameInputForceFeedbackEffect

# v2: static helpers
scan_code_to_physical_key(scan_code: int) -> int
virtual_key_to_keycode(virtual_key: int) -> int
switch_position_to_vector(position: int) -> Vector2
```

v2 enums on `GameInputDevice`: `Button` (bits 14–29 added), `Axis` (6–14
added), `Source` (buttons 0–29, axes 100–114, arcade 200–213, flight 300–313,
wheel 400–413), `MouseButton`, `ArcadeStickButton`, `FlightStickButton`,
`RacingWheelButton`, `SensorKind`, `SensorAccuracy`, `DeviceFamily`,
`DeviceStatus`, `SystemButton`, `SwitchPosition`, `KeyboardKind` and
`RumbleMotor`.

#### `GameInputReading`

```gdscript
is_button_down(button: int) -> bool
was_button_pressed(button: int) -> bool
was_button_released(button: int) -> bool
get_axis(axis: int) -> float
get_buttons_mask() -> int
get_timestamp() -> int

# v2: metadata
get_device_id() -> int
get_kind_timestamp(kind: int) -> int
get_input_kinds() -> int
has_previous() -> bool
has_gap_before() -> bool
is_truncated() -> bool

# v2: unified sources (every GameInputDevice.Source value)
is_source_down(source: int) -> bool
was_source_pressed(source: int) -> bool
was_source_released(source: int) -> bool
get_source_value(source: int) -> float

# v2: keyboard
get_key_count() -> int
get_pressed_physical_keys() -> PackedInt64Array
is_physical_key_down(physical_key: int) -> bool
was_physical_key_pressed(physical_key: int) -> bool
was_physical_key_released(physical_key: int) -> bool
get_key_states() -> Array[Dictionary]

# v2: mouse
get_mouse_buttons() -> int
is_mouse_button_down(button: int) -> bool
was_mouse_button_pressed(button: int) -> bool
was_mouse_button_released(button: int) -> bool
get_mouse_delta() -> Vector2
get_mouse_wheel_delta() -> Vector2
has_mouse_absolute_position() -> bool
get_mouse_absolute_position() -> Vector2
get_mouse_state() -> Dictionary

# v2: motion sensors
get_sensor_kinds() -> int
get_accelerometer() -> Vector3
get_gyroscope() -> Vector3
get_heading_degrees() -> float
get_heading_accuracy() -> int
get_orientation() -> Quaternion

# v2: arcade stick, flight stick, racing wheel
get_arcade_stick_buttons() -> int
get_flight_stick_buttons() -> int
get_flight_stick_hat() -> int
get_flight_stick_hat_vector() -> Vector2
get_racing_wheel_buttons() -> int
get_racing_wheel_gear() -> int

# v2: raw controller
get_controller_axes() -> PackedFloat32Array
get_controller_buttons() -> Array[bool]
get_controller_switches() -> PackedInt32Array
get_controller_axis(index: int) -> float
is_controller_button_down(index: int) -> bool
was_controller_button_pressed(index: int) -> bool
was_controller_button_released(index: int) -> bool
get_controller_switch(index: int) -> int
```

`get_timestamp()` returns the reading's real GameInput timestamp in
microseconds (v1 always returned `0`). A reading is a value snapshot: it
holds no native pointer and keeps working after its device disconnects.

#### `GameInputForceFeedbackEffect` (v2)

```gdscript
is_valid() -> bool
get_device_id() -> int
get_motor_index() -> int
get_kind() -> int
start() -> bool
stop() -> bool
pause() -> bool
get_state() -> int
set_state(state: int) -> bool
get_gain() -> float
set_gain(gain: float) -> bool
get_params() -> Dictionary
set_params(params: Dictionary) -> bool
release() -> void
```

A `RefCounted` weak handle (an effect id) like `GameInputDevice`. The
singleton owns the native effect in a registry and releases it when the
wrapper is freed, `release()` is called, the device disconnects, or the
runtime shuts down; afterwards `is_valid()` is `false` and every method
returns a safe default. Parameters are a `Dictionary` with durations in
seconds and a required `kind`; a magnitude is a float (applied along every
axis the motor supports) or a per-axis `Dictionary` (`linear_x` …
`angular_z`, `normal`), and `sustain_duration < 0` plays until stopped.
Unknown keys, wrong types and non-finite numbers fail the call with a
warning. Enums: `EffectKind`, `EffectState`, `FeedbackAxis`.

#### Native API mapping

| Wrapper/API | Native API(s) | Notes |
| --- | --- | --- |
| `GameInput.initialize()` | `GameInputCreate`, `IGameInput::SetFocusPolicy`, `IGameInput::RegisterDeviceCallback`, `IGameInput::RegisterSystemButtonCallback`, `IGameInput::RegisterKeyboardLayoutCallback`, `IGameInput::RegisterReadingCallback` | Creates the root GameInput interface and primes the always-on device callback (every readable kind, `GameInputDeviceAnyStatus`) used by the cache, hot-plug and status signals. The system-button and keyboard-layout callbacks are optional: a host that refuses them keeps working without those signals. The reading callback is registered only while `reading_callback_kinds` is non-zero. |
| `GameInput.shutdown()` | `IGameInput::UnregisterCallback`, `IGameInput::Release` | Unregister callbacks first, then release the root interface and any cached COM-style objects. |
| `GameInput.poll()` | `IGameInput::GetCurrentReading` | Refreshes cached readings for tracked devices in polling mode: one call per kind group a device supports, so a device that reports several kinds gets all of them. Drains queued callback events first. |
| `GameInput.get_devices()` / `GameInput.get_primary_device()` | `IGameInput::RegisterDeviceCallback` | Build a device cache from the initial enumeration delivered by callback registration and keep it current with subsequent device-status callbacks. |
| `GameInput.get_connected_device_count()` | Device cache | Returns the current cached device count for diagnostics and sample UI. |
| `GameInput.get_current_reading()` | `IGameInput::GetCurrentReading` | Returns a wrapped `IGameInputReading` snapshot for the requested device. |
| `GameInput.set_vibration()` | `IGameInputDevice::SetRumbleState` | v1 haptics path is controller rumble, including trigger rumble when supported; returns `false` when preflight fails or an HRESULT-returning SDK reports a native failure. |
| `GameInput.stop_haptics()` | `IGameInputDevice::SetRumbleState` | Send a zeroed rumble state. Advanced force-feedback work can later layer on the force-feedback APIs. |
| `GameInputDevice` getters | `IGameInputDevice::GetDeviceInfo` | Cache display name, kind mask, and vibration/haptics capability flags from `GameInputDeviceInfo`. |
| `GameInputReading` getters | `IGameInputReading::GetGamepadState`, `IGameInputReading::GetKeyState`, `IGameInputReading::GetMouseState`, `IGameInputReading::GetTimestamp` | Normalize native readings into one Godot-facing wrapper without exposing raw GameInput structs. v2 adds `GetInputKind`, `GetKeyCount`, `GetSensorsState`, `GetArcadeStickState`, `GetFlightStickState`, `GetRacingWheelState` and the `GetController{Axis,Button,Switch}{Count,State}` pairs, copied into a fixed-size snapshot. |
| `reading_received` / `get_buffered_readings()` (v2) | `IGameInput::RegisterReadingCallback`, `IGameInputReading::GetDevice` | The worker callback copies each reading's state into a preallocated ring (512 entries shared by every device, drop-oldest, counted by `get_dropped_reading_count()`); `poll()` drains it and emits in arrival order. |
| `device_status_changed` (v2) | `IGameInput::RegisterDeviceCallback` (`GameInputDeviceAnyStatus`) | Emitted for status transitions other than connect/disconnect. |
| `system_buttons_changed` (v2) | `IGameInput::RegisterSystemButtonCallback` | Guide and Share. Shell-reserved on PC unless the focus policy opts out. |
| `keyboard_layout_changed` (v2) | `IGameInput::RegisterKeyboardLayoutCallback` | Carries the Windows keyboard layout id. |
| `GameInput.get_current_timestamp()` (v2) | `IGameInput::GetCurrentTimestamp` | Same microsecond clock as reading timestamps. |
| `GameInput.set_focus_policy()` (v2) | `IGameInput::SetFocusPolicy` | Also applied from the `focus_policy` setting at `initialize()`. |
| `GameInput.create_aggregate_device()` / `disable_aggregate_device()` (v2) | `IGameInput::CreateAggregateDevice`, `IGameInput::DisableAggregateDevice` | The aggregate surfaces later as an ordinary `device_connected` device whose `get_app_local_id()` matches the returned id. |
| `GameInputDevice.start_vibration()` (v2) | `IGameInputDevice::SetRumbleState` | Godot's `Input.start_joy_vibration` order (weak = high-frequency, strong = low-frequency); `poll()` stops it when the duration elapses. |
| `GameInputDevice.get_haptic_info()` (v2) | `IGameInputDevice::GetHapticInfo` | Audio endpoint id and haptic locations; waveform playback is not provided. |
| `GameInputDevice` force-feedback methods (v2) | `IGameInputDevice::CreateForceFeedbackEffect`, `IGameInputDevice::IsForceFeedbackMotorPoweredOn`, `IGameInputDevice::SetForceFeedbackMotorGain` | Motor info comes from `GameInputDeviceInfo::forceFeedbackMotorInfo`. |
| `GameInputForceFeedbackEffect` (v2) | `IGameInputForceFeedbackEffect::GetParams`, `SetParams`, `GetState`, `SetState`, `GetGain`, `SetGain` | Owned by a singleton registry keyed by effect id. |

### Action bridge

`GameInput` should expose raw input, but it also needs a Godot-native bridge.

#### Additional types

- `GameInputActionMap` (`Resource`)
- `GameInputMapper` (`Node`)

#### `GameInputMapper`

`GameInputMapper` is a `Node` intended to live in the scene tree. It polls `GameInput` or consumes device updates each frame, then emits synthetic `InputEventAction` events against a configured `GameInputActionMap`.

Use it when gameplay code already depends on `Input`, `InputMap`, and project-defined actions. Skip it when a system needs raw per-device state, custom deadzones, or device-specific UX. `GameInputActionMap` mutations, including contained `GameInputBinding` property edits, emit `changed` so active mappers can immediately release held actions before their index-keyed state becomes stale.

- polls `GameInput` each frame
- translates readings into `InputEventAction`
- releases every action it previously held when it stops driving a binding (tree exit, action-map swap or mutation, target-device retarget/loss, or a missing reading)
- refreshes the native-joypad suppression cache at least once per frame so runtime `InputMap` edits cannot stay stale indefinitely
- lets game code keep using:

```gdscript
Input.is_action_pressed("jump")
```

#### Rationale

Raw API is still the right fit for low-level systems. The mapper exists so GDScript-heavy projects can keep using Godot's normal action flow without having to re-author gameplay code around device-level readings.

## Plugin settings

### Runtime

| Setting | Default | Purpose |
| --- | --- | --- |
| `game_input/runtime/initialize_on_startup` | `false` | Bootstrap autoload calls `GameInput.initialize()` after the SceneTree is ready. |
| `game_input/runtime/auto_poll` | `true` | Bootstrap autoload calls `GameInput.poll()` from `_process` so apps don't have to. `GameInputMapper` nodes also call `poll()` defensively (idempotent). |
| `game_input/runtime/singleton_name` | `"GameInput"` | Name the extension registers its Engine singleton under. Read once at extension load, before any project script runs, so it must be set in `project.godot` (or an `override.cfg`) — runtime writes take effect on the next launch. Blank values, non-identifiers, and names that collide with an already-registered singleton are rejected with a warning and the default is kept, so the addon is never left unreachable. Every in-repo consumer (bootstrap autoload, C# facade, GUT test base) resolves the singleton through this setting, class-checks the result so a name colliding with an unrelated engine singleton is not mistaken for the runtime, and retries the default name, so a rename needs no further changes. |
| `game_input/runtime/reading_callback_kinds` | `0` (off) | v2. `DeviceKind` flags whose every reading is delivered through `reading_received`. Read by `initialize()`; `set_reading_callback_kinds()` overrides it at runtime. |
| `game_input/runtime/focus_policy` | `0` (default policy) | v2. `FocusPolicy` flags applied by `initialize()`; `set_focus_policy()` overrides it at runtime. |

### Mapper

| Setting | Default | Purpose |
| --- | --- | --- |
| `game_input/mapper/default_action_map` | `""` | When set to a `GameInputActionMap` resource path (e.g. `res://input/actions.tres`), the bootstrap autoload spawns a `GameInputMapper` named `DefaultMapper` as its child and assigns the loaded resource to it. Lets a project drive its `InputMap` from a GameInput action map without adding any nodes to its scenes. This applies only to the bootstrap-spawned `DefaultMapper`; a user-placed `GameInputMapper` whose `action_map` is null does **not** load this setting. |

> **Note:** `game_input/runtime/embed_dispatch` was dropped — GameInput
> dispatches callbacks on its own worker thread and we don't manually drive
> `IGameInputDispatcher`. `game_input/runtime/enable_device_callbacks` was
> also dropped: device callbacks are always-on after `initialize()` because
> the device cache and `get_devices()` depend on them.

## Build and packaging rules

1. **Plugin ships as its own `.gdextension`**
   - `godot_gameinput.gdextension`

2. **Can share internal support code with companion plugins**
   - string conversion
   - logging
   - optional common helper code if `godot_gdk` ships beside it

3. **Soft-fail outside supported runtimes**
   - editor should still load docs/classes
   - runtime-only methods return unavailable errors instead of crashing

4. **No hard dependency on `godot_gdk`**
   - ship together if needed
   - use separately if desired

## Rollout

| Step | Deliverable | Status |
| --- | --- | --- |
| 1 | `GameInput` raw polling + device callbacks + vibration | Shipped |
| 2 | `GameInputMapper` + action map resource | Shipped |
| 3 | Device info (issue #23) | Shipped (battery half removed — GameInput v3 dropped the API) |
| 4 | Sample integration | Historical `gdk_launch_point` panel + `multiplayer_pong` rumble/hot-plug shipped; both were removed in the tutorial-driven sample revamp. The standalone `sample/tutorial_gameinput/` action-bridge sample is shipped; a GameInput panel inside the tutorial sample tracks remains deferred. |
| 5 | Headless test suite + manual hardware checklist | Shipped |
| 6 | F1 doc XML + user docs + path-scoped instructions | Shipped |
| 7 | v2 (issue #97): every reading kind, event-driven readings, system-button/layout/status signals, timed and trigger vibration, force feedback, haptic info, focus policy, aggregate devices, mock backend, device inspector, `--gameinput-selftest` in both samples, virtual-gamepad runner | Implemented on `feat/gameinput-v2`; hardware sign-off items in [manual tests](../docs/gameinput/manual-tests.md) |

## Deviations from the original sketch

These are intentional design changes from the early draft of this spec, made
during implementation and validated through a rubber-duck design pass:

- **`GameInputMapper` is action-level, not physical-event-level.**
  The Mapper emits via `Input.action_press(action, strength)` /
  `Input.action_release(action)` keyed off `GameInputBinding` rows so polled
  consumers (paddle handlers reading
  `Input.is_action_pressed("move_up")`) keep working, and on every press /
  release transition it additionally pushes an `InputEventAction` through
  `Input.parse_input_event` so event-driven consumers — Viewport GUI focus
  traversal for `ui_*`, `_gui_input`, `_input` / `_unhandled_input` — see the
  action transition. It does **not** inject `InputEventJoypadButton` /
  `InputEventJoypadMotion` — that would create a split-brain where the same
  input flows through two paths.
  When Godot's built-in joypad backend is already wired to deliver the same
  action via a matching `InputEventJoypadButton` / `InputEventJoypadMotion` in
  the project's `InputMap` (e.g., the default `ui_accept` mapping that
  includes joypad button A), the Mapper detects the duplicate per binding,
  caches the result, and skips its own `InputEventAction` so menu actions
  fire exactly once per physical press instead of twice.
  Action names must already exist in `InputMap`; missing actions trigger a
  single per-instance `push_warning` (debounced via
  `HashSet<StringName> m_warned_missing_actions`).
- **Bootstrap autoload installed by `EditorPlugin`.**
  Project settings alone never bootstrap runtime logic. The
  `GameInputBootstrap` autoload is the single bootstrap surface;
  `editor/gameinput_editor_plugin.gd::_enable_plugin()` calls
  `add_autoload_singleton(...)`, and `_disable_plugin()` removes it. There is
  no orphaned state when the plugin is disabled.
- **Threading model is explicit.**
  GameInput device callbacks fire on a worker thread. They only push events
  into a mutex-protected queue and `AddRef` the device pointer. The main
  thread drains the queue inside `poll()` and emits Godot signals from there
  — no `call_deferred` dance.
- **`poll()` is per-frame idempotent.**
  Backed by `Engine::get_singleton()->get_process_frames()`. Multiple
  Mappers + the bootstrap autoload all call `poll()` defensively, but the
  real refresh runs only once per frame and `prev` button state stays correct
  for `was_button_pressed()`.
- **Device IDs are session-local monotonic, never recycled.**
  `GameInputDevice` wrappers hold only the id (a weak handle), never a raw
  `IGameInputDevice*`. After disconnect the id is retired, the wrapper stays
  alive as an inert RefCounted, and methods return safe defaults.
- **`GameInputBinding` is a Resource per binding** (not `Array[Dictionary]`).
  Inspector-friendly typed exports for `action`, `source`, `is_axis`,
  `axis_threshold`, `axis_invert`, `deadzone`. `GameInputActionMap.bindings`
  is a `TypedArray<GameInputBinding>` with `PROPERTY_HINT_ARRAY_TYPE` so the
  editor renders an inspector for resource children.
- **Single combined `Source` enum** for `GameInputBinding.source` covering
  buttons (0–13) and axes (100–105).
- **Soft-fail only — no compile-time platform guards.**
  This repo is Windows-only by mission. Every public method checks
  `_ensure_initialized()` and returns safe defaults (`false`, `null`, empty
  `Array`, `-1.0`) when conditions fail.
- **`embed_dispatch` and `enable_device_callbacks` settings dropped.**
  Documented above.
- **Doc XML race fix:** `addons/godot_gameinput/CMakeLists.txt` serializes
  `target_doc_sources` execution against the `godot_gdk` doc target via
  `add_dependencies`, sidestepping the MSB8065 "two writers race for the
  same gen/ output dir" failure.

## v2 design decisions

v2 (issue #97) keeps every v1 rule above: one `GameInput` singleton,
id-only `RefCounted` wrappers, worker callbacks that only enqueue behind the
callback fence, signals emitted from `poll()`, soft-fail everywhere. It adds:

- **Readings are value snapshots.** Every kind is copied into a fixed-size
  POD snapshot (`gameinput_snapshot.h`: 32 keys, 32 controller axes, 128
  controller buttons, 8 switches). A `GameInputReading` never holds an
  `IGameInputReading*`, so it survives disconnects and costs no COM
  reference.
- **One `GetCurrentReading()` per kind group.** A device that reports kinds
  in separate readings (a keyboard with a built-in pointer, a gamepad with
  motion sensors) keeps every kind current. `get_kind_timestamp()` says how
  fresh each kind is.
- **Event-driven readings are opt-in and bounded.** `reading_callback_kinds`
  defaults to `0`, so v1 projects pay nothing. When enabled, one fenced
  `RegisterReadingCallback` writes into a preallocated 512-entry ring shared
  by every device (drop-oldest, with a swap buffer, so the steady-state drain
  does not allocate). Readings and device events share a sequence number, so
  `poll()` emits them in the order GameInput reported them. A drop (or a
  re-registration) sets `has_gap_before()` on the device's next event
  reading, so a game can tell that its edges span missing readings.
- **Two vibration shapes.** `GameInputDevice.start_vibration(weak, strong,
  duration)` matches Godot's `Input.start_joy_vibration` (weak drives the
  high-frequency motor, strong the low-frequency motor) and `poll()` stops
  it when the duration elapses. v1's `GameInput.set_vibration(device,
  low_freq, high_freq, …)` keeps its argument order.
- **Force-feedback effects are id handles.** The singleton owns each
  `IGameInputForceFeedbackEffect` in a registry and releases it on
  `release()`, wrapper free, device disconnect and shutdown, so a script can
  never hold a dangling effect. Parameters are validated before they reach
  GameInput; a bad key or type fails the call with a warning.
- **Optional callbacks are non-fatal.** The system-button and
  keyboard-layout registrations only log (verbose) when a host refuses them;
  device callbacks stay mandatory.
- **`DEVICE_ALL` keeps its v1 value (7).** v1 only tracked gamepads,
  keyboards and mice, so `get_connected_device_count()` returns what it did
  in v1. `DEVICE_ANY` (255) covers the new kinds.
- **Aggregates get a readable name.** GameInput reports an aggregate as VID:PID
  `0000:0000` with no name; unnamed aggregate-family devices are called
  `GameInput Aggregate Device`. `disable_aggregate_device()` does not
  disconnect it: it stays enumerated, stops producing readings, and
  `create_aggregate_device()` for the same kind re-enables it under the same
  id (observed on a ViGEm pad and stated by the GDK reference).
- **Signal connections are dropped at engine shutdown.** A GDExtension
  main-loop shutdown callback shuts the runtime down and disconnects every
  connection on the singleton's signals before the script languages are
  torn down. Without it, a non-`self` GDScript lambda still connected at quit
  crashes Godot on exit with `0xC0000005`; v1 crashes the same way.
  `tests/godot/gameinput/tests/bootstrap/exit_with_connected_lambdas.gd`
  guards it.
- **A resting stick reads `+0.0`.** Thumbstick Y is flipped to Godot's
  down-is-positive convention by subtracting from `0.0` rather than negating,
  so a centred stick never prints `-0.00`.
- **Paddles dedupe against Godot's joypad buttons.** `GameInputMapper` maps
  the Elite paddles to `JOY_BUTTON_PADDLE1`–`PADDLE4` in SDL order (right
  upper, left upper, right lower, left lower) when it checks the `InputMap`
  for duplicate native bindings.
- **Physical keys come from scan codes.** Keyboard readings expose Godot
  physical `Key` values derived from scan codes (layout-independent, like
  `InputEventKey.physical_keycode`), plus the virtual-key and Unicode values
  GameInput reports.
- **Debug-only mock backend.** `_test_*` methods, compiled only when
  `NDEBUG` is not defined and absent from the doc XML, replace the native
  runtime with scripted devices that run through the same queues, snapshots,
  signals and vibration timers. The release DLL has none of them, so against
  it the GUT mock tests report pending and the samples' mock checks skip.

## Deferred

v2 shipped the v1 wishlist's reading callbacks, force feedback, arcade
stick, racing wheel and keyboard/mouse items. Still out of scope:

- **Raw device reports** (`IGameInputRawDeviceReport`).
- **`IGameInputMapper` label and mapping information** for remapping
  screens.
- **Dispatcher control** (`IGameInputDispatcher`); GameInput's own worker
  threads deliver callbacks.
- **Haptic waveform playback**, which GameInput routes through an audio
  endpoint; `get_haptic_info()` exposes the endpoint only.
- **Sub-frame taps inside `GameInputMapper`**: it reads the polled state, so
  use `reading_received` for inputs that must not miss a tap.
- **A GameInput panel inside the tutorial sample tracks.**
- **XInput fallback** for hosts where GameInput isn't available.
- **Linux/macOS native paths** — out of scope for the GDK-flavoured Windows
  mission of this repo.
- **Xbox Series X|S** — the repo's samples target XBOX on PC; console
  builds of this addon are untested.

