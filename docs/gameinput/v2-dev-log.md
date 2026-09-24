# GameInput v2: development log

This is the working log for
[issue #97](https://github.com/microsoft/XBOX-Godot-Sample/issues/97), which
asks for more of GameInput in the `godot_gameinput` addon. It records what was
decided, why, and the evidence for each claim. The short version for review
is [v2-review-overview.md](v2-review-overview.md).

Each claim that something works comes with a way to prove it wrong: the
command that was run, what it printed and, for new tests, a deliberate break
(a mutation) that made the test fail. Anything that could not be checked on
this machine says so and points to [manual-tests.md](manual-tests.md).

| | |
| --- | --- |
| Branch | `feat/gameinput-v2`, in a fresh clone of microsoft/XBOX-Godot-Sample |
| Base | `55735f7` (origin/main when the work started) |
| Commits | 38, listed in [§7](#7-commits-and-their-evidence). The last one, `docs(gameinput): add the v2 dev log and review overview`, adds only this log and the overview, so it can be dropped before pushing. |
| Pushed | Not to microsoft/XBOX-Godot-Sample, and no pull request was opened there. |

## Contents

1. [Environment](#1-environment)
2. [Baseline](#2-baseline)
3. [References](#3-references)
4. [Probes before coding](#4-probes-before-coding)
5. [Plan and plan review](#5-plan-and-plan-review)
6. [Design decisions](#6-design-decisions)
7. [Commits and their evidence](#7-commits-and-their-evidence)
8. [Experiments](#8-experiments)
9. [Bugs found](#9-bugs-found)
10. [Reviews](#10-reviews)
11. [Final validation](#11-final-validation)
12. [How to reproduce](#12-how-to-reproduce)
13. [Gotchas](#13-gotchas)
14. [Not verified here](#14-not-verified-here)

## 1. Environment

| Item | Value |
| --- | --- |
| OS | Windows 11 Pro for Workstations, Insider build 10.0.26220 |
| Session | Remote Desktop (`RDP-Tcp#0`, session 2). The session was locked (LogonUI running) for part of the work. Both conditions stop GameInput input; see [E10](#e10-remote-and-locked-sessions-get-no-gameinput-input). |
| GPU | AMD Radeon RX 5700 XT |
| Compiler | MSVC from Visual Studio 2022 Enterprise (`vcvars64.bat`), CMake from Visual Studio 18 |
| GameInput header and lib | vcpkg port `gameinput` 3.3.195 (default presets) |
| GameInput NuGet | `Microsoft.GameInput` 3.1.26100.6879, the version `cmake/GDKDependencies.cmake:139` pins for `GAMEINPUT_SOURCE=nuget`. 3.5.274 was downloaded to compare headers. |
| GameInput runtime | 3.3.221.0 (what every probe and live test ran against) |
| Godot | 4.6.1-stable (`14d19694e`, main host), 4.7.1-stable (`a13da4feb`), 4.5.1-stable (`f62fdbde1`), 4.7.1-stable .NET for C# |
| GUT | `third_party/Gut` 9.6.0; `third_party/Gut-4.5` (9.5.0 plus #778) for Godot 4.5, picked by `tools/run_all_tests.ps1` |
| Python | 3.12.10, with vgamepad from a source checkout on `PYTHONPATH` and the ViGEmBus driver |
| .NET | SDK for `net8.0` (C# parity tests and the C# sample) |

## 2. Baseline

Measured on `55735f7` before any change.

| Measure | Base | This branch |
| --- | --- | --- |
| GUT (GameInput host) | 12 scripts with 66 tests on disk. GUT ran 11 scripts and 60 tests: 53 passed, 7 pending (`Tier=live_read`, skipped without `LIVE_TESTS=1`), 0 failed. The twelfth script never compiled, and GUT skipped it without failing ([E19](#e19-a-suite-that-never-ran)). | 23 scripts and 200 tests, all of which run; see [§11](#11-final-validation) |
| Doctest | 16 cases, none for GameInput | 50 cases, 34 of them for GameInput |
| C# facade tests | 112 passed | 119 passed |
| Doc classes | 6 classes, 50 methods, 2 signals, 49 constants, 10 properties | 7 classes, 143 methods, 6 signals, 274 constants, 10 properties |
| Project settings | 4 under `game_input/` | 6 (adds `runtime/focus_policy` and `runtime/reading_callback_kinds`) |

The doc-class counts come from parsing `addons/godot_gameinput/doc_classes/*.xml`
at each revision (`git show <rev>:<file>`); the settings from the
`"game_input/..."` strings in `addons/godot_gameinput/src`. The base DLL also
crashes on exit when a non-`self` lambda is still connected to a GameInput
signal ([E2](#e2-exit-crash-with-connected-lambdas)).

## 3. References

### GDK documentation and packages

- [Input overview](https://learn.microsoft.com/en-us/gaming/gdk/docs/features/common/input/overviews/input-overview?view=gdk-2604),
  [GameInput fundamentals](https://learn.microsoft.com/en-us/gaming/gdk/docs/features/common/input/overviews/input-fundamentals?view=gdk-2510)
  and [GameInput callbacks](https://learn.microsoft.com/en-us/gaming/gdk/docs/features/common/input/advanced/input-callbacks?view=gdk-2604).
- [IGameInput::GetCurrentReading](https://learn.microsoft.com/en-us/gaming/gdk/docs/reference/input/gameinput/interfaces/igameinput/methods/igameinput_getcurrentreading?view=gdk-2604):
  one call returns one reading for the kinds you pass, which is why polling
  makes one call per kind group (plan item 1): each typed kind on its own,
  and the three raw-controller kinds (axes, buttons and switches) together
  (`kPollGroups` in `gameinput_singleton.cpp`).
- [IGameInput::CreateAggregateDevice](https://learn.microsoft.com/en-us/gaming/gdk/docs/reference/input/gameinput/interfaces/igameinput/methods/igameinput_createaggregatedevice?view=gdk-2604):
  one kind, and only keyboard, mouse, arcade stick, flight stick, gamepad
  and racing wheel ([E9](#e9-which-aggregate-kinds-the-runtime-accepts)).
- [IGameInput::UnregisterCallback](https://learn.microsoft.com/en-us/gaming/gdk/docs/reference/input/gameinput/interfaces/igameinput/methods/igameinput_unregistercallback?view=gdk-2604):
  a callback's resources may be freed once `UnregisterCallback` completes
  ([E1](#e1-stopcallback-makes-unregistercallback-fail)).
- [Microsoft.GameInput on NuGet](https://www.nuget.org/packages/Microsoft.GameInput):
  the header behind `GAMEINPUT_SOURCE=nuget`
  ([E6](#e6-the-nuget-header-does-not-link)).

### GDK samples

From a local clone of the public GDK samples. Every line below was re-read
while writing this log.

| Sample and line | What it shows | What the addon took from it |
| --- | --- | --- |
| `GamepadVibration.cpp:401-406` | `GameInputRumbleParams` with `lowFrequency`, `highFrequency`, `leftTrigger`, `rightTrigger`, then `SetRumbleState` | `start_vibration(weak, strong, duration, left_trigger, right_trigger)`: strong drives the low-frequency motor and weak the high-frequency one |
| `SimpleFFBWheel.cpp:34-66`, `:153`, `:158` | Force-feedback parameter structs, `CreateForceFeedbackEffect(motorIndex, ...)`, then `SetState(GameInputFeedbackRunning)` | `GameInputDevice.create_force_feedback_effect(motor, params)` and `GameInputForceFeedbackEffect.start()` |
| `SimpleFFBWheel.cpp:39-40`, `:185-190` | Condition caps of +0.5 and -0.5, and coefficients set at run time to `clamp(speed / -50, -1, 0)` and `clamp(..., -1, -0.05)` | Condition effects default to negative coefficients and a negative `max_negative_magnitude` ([D18](#d18-condition-effects-use-the-gdks-signs)) |
| `GameInputSequential.cpp:302` | `GetNextReading(last, kind, device, &next)` walk | Considered for `reading_received` and rejected ([D3](#d3-event-readings-come-from-a-reading-callback)) |
| `Gamepad.cpp:120-121` | `GetDevice` into a `ComPtr` | `GetDevice` returns an AddRef'd device; the reading ring releases it |
| `GameInputInterfacing.cpp:121`, `:181`, `:209`, `:237` | Device callback, then `GetControllerAxisState`, `ButtonState`, `SwitchState` with counts | Raw controller arrays plus the native counts and `is_truncated()` |
| `GamepadKeyboardMouse.cpp:263-299` | `GameInputKeyState[16]`, printing scan code and virtual key | Key dictionaries carry `scan_code` and `virtual_key` (32 keys) |
| `MouseInput.cpp:117`, `:160` | `GetMouseState`, delta from the previous position | Mouse delta from successive positions, wrap-safe |
| `Haptics/.../WASAPIManager.cpp:130` | `InitializeDevice(endpoint, locationCount, locations)` | `get_haptic_info()` returns the endpoint id and location names; audio playback is deferred |

### Godot source (4.6.1)

| Source | What it shows | Where it mattered |
| --- | --- | --- |
| `core/input/input_map.cpp:465-486` | `ui_left`, `ui_right`, `ui_up` and `ui_down` list the D-pad button and `InputEventJoypadMotion(JoyAxis::LEFT_X or LEFT_Y, ±1.0)` | Mapper double-fire for stick-direction bindings ([E5](#e5-mapper-double-fire-and-specialty-sources)) |
| `core/input/input_event.cpp:1134` | `same_direction` treats an `axis_value` of 0 as either direction | The Mapper's motion match copies that rule |
| `core/input/input_enums.h:98-101` | `JoyButton::PADDLE1..PADDLE4` = 16..19 | Elite paddles dedupe against native paddle events |
| `main/main.cpp` (`Main::cleanup` from line 5084) and `core/extension/gdextension_manager.cpp:439-445` | Extension shutdown callbacks run (line 5097) before the main loop is deleted (5122), script languages finish (5133) and SCENE-level extensions deinitialize (5158) | The exit-crash fix ([E2](#e2-exit-crash-with-connected-lambdas)) |

Godot conventions the API follows: Y down-positive, `JoyButton` and `JoyAxis`
equivalents for duplicate detection, `Input.start_joy_vibration(device, weak,
strong, duration)` for the vibration signature and its strength and duration
queries, `Key` values from scan codes for physical keys, and `Quaternion` for
orientation.

### Repository conventions

James Lenell wrote 115 of the 159 commits reachable from the base. The
addon's own rules, which this branch keeps:

- one root singleton
- `RefCounted` wrappers that hold an `int64` id that is never reused
- worker-thread callbacks that only enqueue, with `poll()` on the main
  thread draining the queue and emitting signals
- soft-fail through `_ensure_initialized()` with one `push_warning`
- debug-only test seams under `#ifndef NDEBUG`
- the doc XML, `plugin.md`, the spec, the instructions file, the sample,
  the tests and the C# facade all change together

Commit prefixes follow the history: `fix`, `feat`, `docs`, `test` and friends,
plus scoped forms like `addon(playfab):` from #179. This branch uses
`addon(gameinput):`, `sample(gameinput):`, `tools(gameinput):`,
`test(gameinput):`, `test(csharp):` and `docs(gameinput):`.

## 4. Probes before coding

Throwaway C++ and Python programs, kept in the session folder rather than the
repository, answered what the GDK docs do not say:

- `probe.cpp` registered device, reading and system-button callbacks, with
  optional background input, and called `SetRumbleState(0.75, 0.25)` after
  2 s.
- `drive_pad.py` drove a ViGEm Xbox 360 pad: three slow taps, five 2 ms
  taps, stick moves.
- `xprobe.cpp` read the same pad through XInput as a control.

Findings:

1. The ViGEm pad enumerates under GameInput as `045E:028E`, family
   Xbox 360, rumble motors low and high, with a Guide button.
2. `SetRumbleState(low 0.75, high 0.25)` reaches ViGEm as large 191,
   small 64 (0.75 × 255 and 0.25 × 255, rounded). This works in a locked
   session. Two of three rumble runs logged it; the third logged only zeros,
   and why was not established. Every automated rumble check since has
   passed ([§11](#11-final-validation)).
3. In a locked session GameInput delivers no readings: 0 reading callbacks
   and a frozen polled state, even with background input enabled. XInput
   sees the same input. Live input therefore became a manual test or a
   skip ([E10](#e10-remote-and-locked-sessions-get-no-gameinput-input)).
4. `IGameInputReading::GetDevice` returns an AddRef'd device, and device
   objects live for the whole process, so pointer equality identifies a
   device.

## 5. Plan and plan review

The plan extended the v1 architecture instead of replacing it. Its phases
were:

- a per-device reading snapshot for every structured input kind (raw
  device reports stay deferred)
- reading accessors and enums
- event-driven readings
- system buttons, keyboard layout and focus policy
- haptics and force feedback
- aggregate devices
- a debug-only mock backend
- tests, the C# facade, the sample harness, the virtual pad and docs

A synchronous rubber-duck review of the plan returned 18 items. Each is
listed below with what happened to it.

| # | Review finding | Outcome | Evidence |
| --- | --- | --- | --- |
| 1 | A multi-kind `GetCurrentReading` returns the newest reading of any kind, and the kind mask had undocumented bits | Done. Polling makes one call per kind group the device supports (each typed kind alone; the three raw-controller kinds share one call), shifts the previous snapshot once per poll and keeps a timestamp per kind (`GameInputReading.get_kind_timestamp`). | `test_gameinput_mock_readings` |
| 2 | Readings and device events need one chronological order | Done. One sequence number orders both. | `test_readings_and_device_events_share_one_order` |
| 3 | A late callback can use freed memory; use `StopCallback` | Done another way. `StopCallback` was measured and rejected ([E1](#e1-stopcallback-makes-unregistercallback-fail)). Each registration has its own gate as its callback context, closed before `UnregisterCallback()`. A failed unregister keeps its registration and is retried (`d3f1343`); one that fails the final attempt at shutdown is abandoned, with its gate never freed and the module pinned (`0e78e52`). | E1, [E14](#e14-failed-unregisters-and-per-device-gaps), [E18](#e18-callback-gates-and-abandoned-registrations) |
| 4 | Keep separate poll and event lanes, a first-event baseline and overflow gaps | Done: `has_gap_before()` (per device since `d3f1343`), `get_dropped_reading_count()` | `test_gameinput_event_readings` |
| 5 | Fixed arrays truncate | Partly. Capacities are 32 keys, 32 axes, 128 buttons and 8 switches (`gameinput_snapshot.h:42-45`). The native counts are kept, and `is_truncated()` reports a cut. | `test_gameinput_mock_readings` |
| 6 | The mock contradicts `_ensure_initialized` | Done. The backend is `None`, `Native` or `Mock`, and mocks are found by id. | `test_gameinput_v2_api` |
| 7 | Force-feedback params need a schema; there is no native motor-gain getter; C# needs `IDisposable` | Done. Validation is per effect kind and rejects non-finite numbers and keys the kind does not use. Motor gain is set-only, effect gain is read from GameInput, and the C# effect is `IDisposable`. | `test_gameinput_force_feedback` |
| 8 | Device-kind masks and counts | Done. `DEVICE_ALL` stays 7, `DEVICE_ANY` is 255, and `get_connected_device_count(kind)` was added. | `test_gameinput_v2_api` |
| 9 | Physical keys and key fields | Done. `is_physical_key_down()` was added, and key dictionaries carry `scan_code`, `virtual_key`, `code_point`, `is_dead_key`, `physical_keycode`, `keycode` and `location`. Extended scan codes need hardware. | `test_gameinput_mock_keyboard_mouse` |
| 10 | Source helpers, sensors and mouse details | Done. Source helpers are digital only, with `get_source_value()` for analog. Orientation is `Quaternion(x, y, z, w)`, and `heading_degrees` was added. Position flags gate fields, and deltas wrap. | `test_gameinput_mock_readings` |
| 11 | Timestamps on signals, haptic readiness, focus policy caching, connect-time status | Done; connect-time status in `44ea079` ([E13](#e13-status-flags-that-arrive-with-a-connect)) | `test_gameinput_system_events`, `test_gameinput_mock_devices` |
| 12 | C# facade parity | Partly. Enum values are now checked against the doc XML (`8a65289`), and flag enums are `[Flags]`. Declined: static-versus-instance shape checks, arity checks and typed records. The final review raised one shape case, the instance `ButtonToSource` and `AxisToSource`; they keep their v1 shape ([E15](#e15-c-events-and-the-native-bridge)). | C# parity tests |
| 13 | Mapper duplicates with Godot's joypad events, and specialty sources | Done. Trigger and stick-direction buttons dedupe (`a871812`). Specialty sources are never suppressed (`fd504e1`). | [E5](#e5-mapper-double-fire-and-specialty-sources) |
| 14 | Aggregate kinds and ids | Done. Exactly one supported kind is required (`31d95d2`), and devices expose `get_app_local_id()`, 64 lowercase hex characters. | [E9](#e9-which-aggregate-kinds-the-runtime-accepts) |
| 15 | Pure C++ tests, an injectable clock, observed skips | Partly. 34 of the 50 doctest cases cover GameInput, 7 of them the callback gate. The clock can be injected (`_test_set_time_override_usec`), and `vpad.input` skips after an 8 s wait. COM lifetimes are covered by GUT mocks and the probes, not by pure C++ tests. | doctest, self-test report |
| 16 | The ring must not allocate on the worker | Done. A 512-entry ring (`kReadingRingCapacity`) lives under the event mutex, and a spare ring is swapped in at drain. | `test_gameinput_event_readings` |
| 17 | The spec needs status, scope, rollout and deferrals | Done: the spec's Scope, Rollout step 7, v2 design decisions and Deferred sections | [spec](../../spec/gdext-gameinput.md) |
| 18 | The self-test report needs a schema and context | Done: schema `gameinput-selftest/1`, a `build` block, a `session` block and a reason on every skip | sample README |

The review also suggested five scope cuts: force-feedback and haptic info,
aggregates, variable-size raw arrays, specialty sources in the Mapper, and
the full inspector plus the C# self-test. Four were not cut; all of them
ship here. Raw arrays stay fixed-size, with native counts (item 5).

## 6. Design decisions

### D1. Keep the v1 contract

There is still one `GameInput` singleton, the wrappers are still weak
`RefCounted` handles, callbacks still only enqueue, and `poll()` still drains
the queue on the main thread. Existing v1 calls behave as before; this is
checked by the base tests, which pass unchanged. One base script was
edited: `test_gameinput_mapper_stuck_actions.gd` had never compiled, so its
six tests had never run ([E19](#e19-a-suite-that-never-ran)). `a180c21`
makes it compile and adds two frame waits where a Mapper added late in a
frame has not processed yet; five tests pass and one is pending without a
keyboard. The shared GUT base only gained helpers, and the C# parity
checker gained the enum checks.

### D2. The snapshot is per kind

Each poll calls `GetCurrentReading` once per kind group the device supports
(each typed kind alone; a raw controller's axes, buttons and switches
together), and fills a fixed-size snapshot with a timestamp per kind. The
previous snapshot shifts once per poll, so `was_*_pressed` edges are exact per
frame.

### D3. Event readings come from a reading callback

`reading_received` is fed by `RegisterReadingCallback`, not by a main-thread
`GetNextReading` walk like `GameInputSequential`. GameInput keeps about half a
second of reading history per device, and a walk filtered to one device fails
once that device disconnects, so readings from just before an unplug would be
lost. It is off by default (`reading_callback_kinds = 0`), so v1 projects pay
nothing.

### D4. One ordering and no allocation on the worker

One sequence number orders readings and device events. The ring is
preallocated. When full it drops the oldest reading, counts the drop
(`get_dropped_reading_count()`) and marks a gap on that device only: the
device's first kept reading reports `has_gap_before()`, or its next reading
when none was kept. The marks sit in a fixed 16-entry table under the event
mutex; when more than 16 devices lose readings before a poll, every device is
flagged instead ([E14](#e14-failed-unregisters-and-per-device-gaps)).

### D5. Teardown uses `UnregisterCallback` alone

See [E1](#e1-stopcallback-makes-unregistercallback-fail). Each
registration's gate turns late calls away, and the singleton's accepting
flags and in-flight counter cover a callback that is already running when
teardown starts ([D16](#d16-each-callback-registration-has-its-own-gate)).

### D6. Signal connections are dropped at engine shutdown

See [E2](#e2-exit-crash-with-connected-lambdas).

### D7. Godot conventions over GameInput conventions

- Stick Y is down-positive and reads +0.0 at rest ([E3](#e3-a-resting-stick-read-negative-zero)).
- Vibration follows `Input.start_joy_vibration` and auto-stops in `poll()`.
- Keys map to Godot `Key` values. Elite paddles map to `JoyButton` 16..19.

### D8. Device-kind masks stay compatible

`DEVICE_ALL` keeps its v1 value, 7 (gamepad, keyboard and mouse), so existing
calls do not start returning wheels or sensors. `DEVICE_ANY` (255) covers
every kind.

### D9. The mock backend is debug-only

The `_test_*` methods drive the same queues as the native callbacks and exist
only in debug builds. The release DLL contains none of them
([E7](#e7-test-seams-are-absent-from-release)). They are described in the
instructions file, not in the class reference.

### D10. Force feedback is a handle

`GameInputForceFeedbackEffect` is a `RefCounted` handle whose native effect
lives in the singleton. It is released by `release()`, by freeing the
wrapper, by disconnecting the device and by shutdown. Its parameters are a
validated Dictionary.

### D11. Aggregates

- The kind must be exactly one of six ([E9](#e9-which-aggregate-kinds-the-runtime-accepts)).
- Disabling an aggregate leaves it listed.
- Creating it again returns the same id ([E8](#e8-aggregate-semantics-and-aggregate-rumble)).

### D12. The Mapper suppresses only what Godot also sees

A binding is skipped only when the `InputMap` already lists a native Godot
joypad event for the same control. Specialty sources (arcade, flight stick,
wheel) are never suppressed ([E5](#e5-mapper-double-fire-and-specialty-sources)).

### D13. The C# facade is one-to-one

Every doc-XML member has a typed C# member, and the parity tests check names
and enum values. They also check that adding a handler to any event connects
the native bridge, so a C# node that only subscribes still hears events
([E15](#e15-c-events-and-the-native-bridge)).

### D14. The sample is the integration check

`--gameinput-selftest` exits 0 (pass), 1 (fail, or skip under
`--gameinput-strict`), 2 (harness error) or 3 (watchdog), and writes a JSON
report. It swaps in the mock backend to drive the tutorial's own wiring, and
with `--gameinput-virtual-pad` it round-trips rumble through a ViGEm pad. The
runner keeps the same codes when Godot hangs, crashes or writes no report
([E16](#e16-runner-exit-codes)), and a check that observed nothing skips or
fails instead of passing ([E17](#e17-self-test-checks-that-passed-on-nothing)).
A check that stops on a script error fails, and the global state it may
have changed is put back before the next check runs
([E23](#e23-self-test-hardening)).

### D15. Status flags that arrive with a connect are silent

They are readable in the `device_connected` handler, and
`device_status_changed` covers later changes only
([E13](#e13-status-flags-that-arrive-with-a-connect)).

### D16. Each callback registration has its own gate

GameInput may call a registration until `UnregisterCallback()` succeeds,
and nothing the callback uses may be freed before then. Each registration
therefore hands GameInput a small `CallbackGate`
(`gameinput_callback_gate.h`) as its context instead of the singleton. The
gate is closed before the unregister, so a late call stops at the gate.
A registration that still cannot be removed at shutdown is abandoned: its
closed gate is leaked on purpose, and the module is pinned
(`GET_MODULE_HANDLE_EX_FLAG_PIN`), so a late call still finds mapped code
and returns. The cost is one small object per registration GameInput
refused to remove, which never happened on this machine
([E18](#e18-callback-gates-and-abandoned-registrations)).

### D17. The reading drain is fenced by an epoch and does not nest

A `reading_received` handler can change the reading mask while the drain
still holds readings of the old registration. Each registration change
bumps an epoch, and the drain drops and releases readings of an older
epoch. `poll()` also refuses to nest, even when a signal handler restarts
the runtime, so a second drain cannot take the first one's gap marks
([E20](#e20-reading-epochs-and-poll-re-entry)).

### D18. Condition effects use the GDK's signs

Spring, damper, friction and inertia default to coefficients of -1 and a
`max_negative_magnitude` of -1, the signs the GDK's SimpleFFBWheel sample
uses ([§3](#3-references)). A cap of the wrong sign is rejected rather
than clamped to 0
([E21](#e21-condition-effect-signs-and-the-float-range)).

### D19. Keyboard layouts are unsigned

GameInput's keyboard layout is a `uint32_t` KLID. Every getter returns it
in a 64-bit integer, so ids of 0x80000000 and above stay positive, and
`get_device_info()` reports the layout the keyboard-layout callback last
reported, not the one GameInput gave at connect
([E22](#e22-keyboard-layouts-of-0x80000000-and-above)).

### D20. The runner trusts only a report with its run id

`tools/run_gameinput_selftest.ps1` passes a fresh 32-digit hex
`--gameinput-run-id`, and both self-tests copy it into the report. A
report without it, or with another run's id, is a harness error (exit 2),
so a stale or concurrent report cannot turn into a pass. A report left by
an earlier run that cannot be deleted is also exit 2
([E25](#e25-the-runner-and-the-virtual-pad-driver)).

### D21. The C# events are thread-safe

The event accessors use the compiler's own `Interlocked.CompareExchange`
loop, and the native signals are connected once, under a lock
([E24](#e24-c-event-accessors-across-threads)).

## 7. Commits and their evidence

Oldest first, with each subject shown without its `area(scope):` prefix.
Each commit message carries the full evidence; this table summarises it.

| Commit | Subject | Evidence |
| --- | --- | --- |
| `595cbbd` | add GameInput v2 core runtime (#97) | Build; doctests for the snapshot, queue and keymap helpers |
| `68b07fa` | mock-backed GUT coverage for GameInput v2 (#97) | Ten new suites. They found two bugs: the Mapper's `target_device_id` lookup, and empty device names. |
| `e17537a` | drop signal connections at engine shutdown | [E2](#e2-exit-crash-with-connected-lambdas): exit code -1073741819 without the callback, 0 with it |
| `690739c` | document the v2 API surface | Signatures from `--doctool`; the v1 Binding Y-sign note corrected |
| `1228bc7` | mirror the v2 API in the C# facade | Parity tests 113/113 |
| `d87bbdc` | read a resting stick's Y axis as +0.0 | [E3](#e3-a-resting-stick-read-negative-zero) |
| `0593754` | turn tutorial_gameinput into an integration harness (#97) | Self-test, inspector, rumble on jump; [E4](#e4-the-headless-viewport-is-64-by-64) |
| `517cb04` | name unnamed aggregates and document disable semantics | [E8](#e8-aggregate-semantics-and-aggregate-rumble) |
| `20a1762` | virtual-pad driver and self-test runner | 30/0/3 with a ViGEm pad; the driver saw large=191 small=64 and the auto-stop |
| `8a65289` | check the facade's enum values against the doc XML | 117/117. With `Button.B` = 9: "Button.BUTTON_B is 8 natively but 9 in C#". |
| `99c9ca0` | C# sample rumble, event hygiene and self-test | 14/0/0; three mutations each fail a named check |
| `e3805d3` | run the C# self-test from run_gameinput_selftest.ps1 | 14/0/0 from a clean `.godot`; wrong-Godot cases exit 2 |
| `52c2a39` | press the inspector's rumble buttons in the self-test | 26/0/3. A mutated Rumble handler fails "expected 0.5000, got 0.0000". |
| `36da8ab` | document v2 input kinds, events, haptics and self-test | Every GDScript snippet passes `--check-only`, and every C# snippet compiles; mutations fail |
| `f4fa507` | keep UnregisterCallback-only teardown and say why | [E1](#e1-stopcallback-makes-unregistercallback-fail) |
| `a871812` | dedupe trigger and stick-direction buttons | [E5](#e5-mapper-double-fire-and-specialty-sources) |
| `ccc51e0` | say why event readings use a callback | [D3](#d3-event-readings-come-from-a-reading-callback) |
| `73f2d40` | link against the pinned GameInput NuGet header | [E6](#e6-the-nuget-header-does-not-link) |
| `e1eef1a` | check that aggregate rumble reaches the pad | [E8](#e8-aggregate-semantics-and-aggregate-rumble): 31/0/3; two mutations fail |
| `fd504e1` | say the mapper never suppresses specialty sources | [E5](#e5-mapper-double-fire-and-specialty-sources) |
| `31d95d2` | refuse unsupported aggregate kinds up front | [E9](#e9-which-aggregate-kinds-the-runtime-accepts) |
| `44ea079` | say status flags that arrive with a connect are silent | [E13](#e13-status-flags-that-arrive-with-a-connect) |
| `d3f1343` | retry failed unregisters, gap only lossy devices | [E14](#e14-failed-unregisters-and-per-device-gaps): eight new tests; four mutant builds fail 3, 3, 5 and 1 of them |
| `32e2387` | connect C# events when a handler is added | [E15](#e15-c-events-and-the-native-bridge): probe went from `connected=0 readings=0` to `connected=1 readings=1`; parity tests 118/118; field-like events fail the new test, naming all six |
| `c82cc6f` | make the self-test runner's exit code trustworthy | [E16](#e16-runner-exit-codes): ten fake-Godot cases, six of which changed |
| `4b99359` | stop self-test checks passing on nothing | [E17](#e17-self-test-checks-that-passed-on-nothing): a mutated virtual-pad run went from PASS 30/0/4 to FAIL 28/2/4 |
| `8faa9a0` | say how C# names a source without a device | Docs only ([E15](#e15-c-events-and-the-native-bridge)) |
| `b0c18e5` | say readings leave out raw device reports | Docs only, but the DLL embeds the class reference, so it was rebuilt and re-tested |
| `0e78e52` | gate each callback and fence the reading drain | [E18](#e18-callback-gates-and-abandoned-registrations), [E20](#e20-reading-epochs-and-poll-re-entry): seven gate doctests, new GUT tests and a bootstrap runner; mutations M1-M3, M5, M8 and M10 each fail |
| `20fb105` | use the GDK's signs for condition effects | [E21](#e21-condition-effect-signs-and-the-float-range): M6 and M7 fail |
| `d9dac24` | report keyboard layouts as unsigned KLIDs | [E22](#e22-keyboard-layouts-of-0x80000000-and-above): M9 fails |
| `dc6917d` | fix the per-player recipe in the action-bridge tutorial | A new Mapper test for the three behaviours the recipe relies on; every GDScript block in the tutorial passes `--check-only` |
| `a180c21` | run the stuck-actions suite and fail on broken suites | [E19](#e19-a-suite-that-never-ran): the new meta-test fails on a broken suite (M13) |
| `082b8ff` | harden the self-tests and drive the inspector toggles | [E23](#e23-self-test-hardening): without the reset, an aborted check fails three more |
| `a58d16d` | make C# events thread-safe and check enums both ways | [E24](#e24-c-event-accessors-across-threads): M11 kept 487 to 722 of 3,200 handlers; M12 left one behind |
| `df28372` | harden the self-test runner and the vpad driver | [E25](#e25-the-runner-and-the-virtual-pad-driver): R1-R5, T1-T6 and three driver tests |
| `2808758` | describe the callback gates and add manual checks | Docs only. The instructions file, `csharp.md` and two new manual checks; every GDScript block in `plugin.md`, the tutorial and `manual-tests.md` passes `--check-only` (15 of 15) |
| last | add the v2 dev log and review overview | Every relative link and anchor in the changed Markdown resolves ([§11](#11-final-validation)) |

The subjects of `0e78e52` to `2808758` were reworded to the branch's
`area(gameinput):` style after the final validation ran. Later, before the
branch was pushed for review, the author and committer email of all 38
commits was changed to the pushing account's GitHub noreply address,
because GitHub refuses a push that would publish a private email (GH007).
Neither step changed a tree, a commit body, an author name or a date, and
this log cites the final hashes. The one exception is `3b9f052` in
[§11](#11-final-validation), the commit the validation ran on.

## 8. Experiments

### E1. StopCallback makes UnregisterCallback fail

**Question.** Should teardown call `StopCallback` before `UnregisterCallback`,
so dispatch stops even when unregistering fails?

**Method.** A probe ran 20 initialize and shutdown cycles, re-registering the
reading callback four times per cycle, which is 100 unregisters per run. It
ran three times with `StopCallback` first and three times without it.

**Result.** With `StopCallback` first, `UnregisterCallback` failed 6, 10 and 7
times. Without it, 0, 0 and 0 times.

**Decision.** Teardown keeps `UnregisterCallback` alone (`f4fa507`). The
instructions file warns against adding `StopCallback`, and the GDK reference
confirms that resources may be freed once `UnregisterCallback` succeeds. The
failure warning now names which callback failed.

### E2. Exit crash with connected lambdas

**Symptom.** A GDScript lambda that does not capture `self`, still connected
to a GameInput signal at quit, crashed Godot with 0xC0000005. The v1 DLL
crashes the same way.

**Cause.** The singleton is deleted when SCENE-level extensions
deinitialize, which `Main::cleanup()` does after
`ScriptServer::finish_languages()`, so the lambda outlived GDScript.

**Fix.** A GDExtension main-loop shutdown callback runs before the main loop
and the script languages are torn down. It shuts the runtime down and
disconnects every connection on the singleton's signals.

**Falsified by.** `tests/godot/gameinput/tests/bootstrap/exit_with_connected_lambdas.gd`
exits -1073741819 with the callback commented out and 0 with it. The
bootstrap stage of `run_all_tests.ps1` fails on a non-zero exit.

### E3. A resting stick read negative zero

`get_axis()` flipped Y with a unary minus, so a centred stick printed
`-0.00`. It now subtracts from 0.0. `test_axes_use_godot_conventions`
formats both resting Y axes with `%+.2f` and fails against the old DLL
(`"-0.00"` expected to equal `"+0.00"`).

### E4. The headless viewport is 64 by 64

Under `--headless` the root viewport is 64 × 64, so the tutorial's layout
checks saw overlapping nodes. The self-test resizes the viewport to the
project's window size before its gameplay checks.

### E5. Mapper double-fire and specialty sources

**Double-fire.** Godot's default `ui_up` already lists
`InputEventJoypadMotion(JOY_AXIS_LEFT_Y, -1.0)` (`input_map.cpp:479`), so
`ui_up -> SRC_BTN_LEFT_STICK_UP` fired twice. Trigger and stick-direction
buttons now match a motion event on the same axis in the same direction.
The new test checks ten sources, 30 asserts. Flipping the `LEFT_STICK_UP`
sign fails 2 asserts; removing the branch fails all 10 match asserts.

**Specialty sources.** `test_specialty_sources_are_never_suppressed` puts
every SDL button and both directions of every axis on one action. It checks
that a gamepad button and axis are suppressed, and that 42 specialty buttons
and 9 specialty axes (both invert settings) are not. Suite at `fd504e1`:
9/9 tests, 123/123 asserts. Mapping `SRC_ARCADE_ACTION_1` to `JoyButton` 0 and
`SRC_AXIS_WHEEL` to `JoyAxis` 0 gives 8/9 and 120/123. Restored, the source
hash matches the backup, and the suite is 9/9 and 123/123 again.

### E6. The NuGet header does not link

On the `GAMEINPUT_SOURCE=nuget` path the addon failed to link, with
`LNK2001: unresolved external symbol GAMEINPUT_HAPTIC_LOCATION_NONE` and four
more. The 3.1.26100.6879 header declares these GUIDs with `DEFINE_GUID`, and
its `GameInput.lib` does not define them. The vcpkg header declares them as
`constexpr`. Apart from those five declarations the two headers are
token-identical once comments and whitespace are removed. The location names
now come from a table in `gameinput_labels.h` with a doctest; changing one
byte of `grip_left` fails 2 checks. The NuGet build went from exit 1 to
exit 0, and [§11](#11-final-validation) rebuilds it.

### E7. Test seams are absent from release

The debug DLL contains 18 distinct `_test_*` names. The release DLL
(`release-gameinput` preset) contains none. The count is a regex over the
DLL bytes; see [§12](#12-how-to-reproduce).

### E8. Aggregate semantics and aggregate rumble

GameInput reports an aggregate as `0000:0000` with no name, so unnamed
aggregates are now named "GameInput Aggregate Device".
`DisableAggregateDevice` does not disconnect the aggregate. It stays listed,
stops producing readings, and creating the same kind again returns the same
id. This was observed on a ViGEm pad and matches the GDK reference.

`vpad.aggregate_rumble` sends `start_vibration(0.4, 0.6, 0.5)` to the
aggregate only, and requires the driver to log large=153 small=102, then the
auto-stop. Two mutations fail it: expecting large=191, and disabling the
aggregate first. The second shows that a disabled aggregate forwards no
rumble.

### E9. Which aggregate kinds the runtime accepts

A probe against runtime 3.3.221 gave these results:

| Kind passed | Result |
| --- | --- |
| Keyboard, mouse, arcade stick, flight stick, gamepad, racing wheel | Id returned |
| `DEVICE_SENSORS`, `DEVICE_CONTROLLER`, `DEVICE_GAMEPAD \| DEVICE_KEYBOARD`, `DEVICE_ALL`, `DEVICE_ANY` | `hr=0x80004001` (`E_NOTIMPL`) |
| `DEVICE_GAMEPAD \| 256` (old code) | Masked to gamepad and silently created |

`create_aggregate_device()` now refuses anything that is not exactly one of
the six, with a warning that lists them. The live test that checks this
failed 1 of 164 asserts on the old DLL and passes 164/164 on the new one.

### E10. Remote and locked sessions get no GameInput input

| Session | GameInput readings from the ViGEm pad | Rumble to the pad |
| --- | --- | --- |
| Locked (LogonUI in the session) | None; 0 callbacks and frozen polled state; XInput still sees input | Works |
| Unlocked Remote Desktop, headless or windowed | None within 8 s | Works |

The self-test therefore marks `vpad.input` as SKIP with the reason in the
report, and still checks rumble. The runner passes
`--gameinput-session-locked` when LogonUI owns the session. Live input at a
physical console is a
[manual test](manual-tests.md#virtual-gamepad-in-a-local-session).

### E11. The first headless import can crash

The first `--headless --import` of a project that loads the extension can
exit with 0xC0000005 during teardown. A second import succeeds, so the runner
imports twice when the project has not registered the extension. The root
cause was not investigated ([§14](#14-not-verified-here)).

### E12. `is_connected()` and `Object.is_connected`

`GameInputDevice.is_connected()` (v1) shares its name with
`Object.is_connected(signal, callable)`. Untyped and `GameInputDevice`-typed
calls work. When the variable is typed `Object`, the GDScript analyzer picks
the `Object` method and reports "Too few arguments for "is_connected()" call.
Expected at least 2 but received 0." (a probe via `GDScript.reload()` returned
43, `ERR_PARSE_ERROR`). This is v1 behaviour, and renaming it would break
existing projects, so it is left as is and noted here.

### E13. Status flags that arrive with a connect

When the first device callback carries `STATUS_CONNECTED` together with
`STATUS_HAPTIC_INFO_READY`, the entry keeps both, so `get_status()` already
has haptic readiness in the `device_connected` handler, and no
`device_status_changed` fires. The docs now say so.
`test_status_bits_that_arrive_with_the_connect_emit_no_status_change`:

| Build | Tests | Asserts |
| --- | --- | --- |
| As committed | 14/14 | 89/89 |
| Mutation: store only `STATUS_CONNECTED` at connect | 13/14 | 87/89 |
| Mutation: also emit a status change for the connect | 13/14 | 88/89 |
| Restored (hash matches the backup) | 14/14 | 89/89 |

### E14. Failed unregisters and per-device gaps

Found by the final review ([§10](#10-reviews), items 1 and 2).

**Unregister.** A failed `UnregisterCallback()` used to forget its token. If
GameInput kept that registration alive, its callback could run after
`shutdown()` had released `IGameInput`. The token is now kept and retried at
the next reading-callback change, and once more at shutdown while the
`IGameInput` that issued it is still alive. Two new debug seams,
`_test_fail_next_unregisters()` and `_test_get_unregister_state()`, drive
this path.

**Gaps.** A full ring used to flag `has_gap_before()` on every device that
existed before the drain. Devices that lost nothing were flagged, and a
device that connected in the same poll was missed. Now each eviction records
its device ([D4](#d4-one-ordering-and-no-allocation-on-the-worker)).

**Falsified by.** Eight new tests in `test_gameinput_event_readings` and four
mutant builds of `gameinput_singleton.cpp`, each restoring old behaviour.
The suite has 18 tests. A failing assert can skip later ones, so the assert
totals differ between builds.

| Build | Mutation | Tests passed | Asserts passed | Failing tests |
| --- | --- | --- | --- | --- |
| As committed | None | 18 of 18 | 128 of 128 | None |
| A | A failed unregister forgets its token | 15 of 18 | 123 of 127 | `test_a_failed_unregister_is_retried_on_the_next_change`, `test_shutdown_retries_a_failed_unregister`, `test_shutdown_gives_up_after_its_final_attempt` |
| B | Overflow flags every device | 15 of 18 | 123 of 127 | `test_overflow_flags_only_the_device_that_lost_readings`, `test_overflow_before_a_new_device_is_drained_flags_its_gap`, `test_a_device_whose_readings_were_all_dropped_gets_the_gap_next_time` |
| C | No shutdown retry, no carry-over when a device kept no reading, no fallback past 16 devices, and the old mock | 13 of 18 | 120 of 127 | The two shutdown tests, `..._gets_the_gap_next_time`, `test_more_devices_losing_readings_than_marks_flags_every_quiet_device` and `test_input_pushed_before_the_connect_drains_reaches_the_polled_state` |
| D | The old mock alone: input pushed before a connect drains is dropped | 17 of 18 | 126 of 127 | `test_input_pushed_before_the_connect_drains_reaches_the_polled_state` |

The last row is mock fidelity, not a product bug. A native device can report
input before the poll that drains its connect, and the mock now allows the
same. After each mutant the source was restored, and its hash matched the
backup.

### E15. C# events and the native bridge

Found by the final review (item 3). The C# facade connected the native
signals to its events only when something first read `GameInput.Singleton`.
The GDScript bootstrap autoload calls `initialize()` and `poll()` on the
native singleton directly, so a C# node that only subscribed heard nothing.
The lazy connect pre-dates this branch. v1's two events and the Xbox and
PlayFab facades use it too, but this branch added four events, including
`ReadingReceived`. Each event now has an `add` accessor that resolves the
singleton, which connects the bridge.

**Falsified by.**

- A probe project, in which GDScript drives a mock pad and C# only
  subscribes to `DeviceConnected` and `ReadingReceived`, printed
  `connected=0 readings=0` and exited 1 before the fix. After it, it printed
  `connected=1 readings=1` and exited 0.
- `AddingAnEventHandlerConnectsTheNativeBridge` scans each `add` accessor's
  IL for the call that connects the bridge. It passes as committed
  (118/118). With the old field-like events it fails, naming all six; with
  only `ReadingReceived`'s connect removed, it names only `ReadingReceived`.

`IsAvailable` still only checks that the native singleton exists, like the
Xbox and PlayFab facades, and no longer gates the bridge.

**`ButtonToSource` and `AxisToSource`** (review item 6). The native methods
are static; the C# ones have been instance methods returning `int` since v1.
Making them static would break existing callers (CS0176), and so would
returning `Source` (CS0266). A probe on Godot 4.7.1 .NET showed that the
instance call reaches the static method: `button_to_source(BUTTON_A)`
returned 2 (`SRC_BTN_A`) and `axis_to_source(AXIS_WHEEL)` returned 106
(`SRC_AXIS_WHEEL`). They stay as they are. The cost is that C# needs a device
to call them; code without one can name the `Source` value directly, and
[csharp.md](csharp.md) now says both (`8faa9a0`). The C# self-test checks
both methods, plus the static key and switch helpers. It passes 14/14. A
mutant with `ButtonToSource` off by one, and the scan-code helper swapped for
the virtual-key one, fails two named checks (12/14, exit 1).

### E16. Runner exit codes

Found by the final review (item 4). A fake Godot drove
`tools/run_gameinput_selftest.ps1` through ten cases. It is a `.cmd` that
runs a PowerShell script, which picks its behaviour from an environment
variable and writes to the report path it was given.

| Case | Before | After |
| --- | --- | --- |
| Godot outlives the watchdog and is killed | 2 | 3 |
| Godot exits 1 without a report | 1 | 2 |
| Godot crashes (0xC0000005) without a report | -1073741819 | 2 |
| Godot exits 0 without a report | 2 | 2 |
| Report says pass, Godot exits 0 | 0 | 0 |
| Report says fail, Godot exits 1 | 1 | 1 |
| Report says watchdog, Godot exits 3 | 3 | 3 |
| Report says 0, Godot exits 1 | 1 | 2 |
| Report is not JSON, Godot exits 0 | 0 | 2 |
| `-VirtualPad -Python C:\missing\python.exe` | 1 | 2 |

The self-test writes its report before every exit, so a missing report means
the harness failed. A script-level `trap` turns an exception into exit 2
after the `finally` block has stopped the virtual-pad driver.

### E17. Self-test checks that passed on nothing

Found by the final review (item 5). Two mutations went into the self-test
script: every reading forced to null, and the wait for the aggregate to
surface cut to zero (the messages still say 1.5 s). The virtual-pad tier
then reported:

| Check | Before the fix | After the fix |
| --- | --- | --- |
| `runtime.readings` | PASS: "0 of 8 device(s) returned a consistent reading" | SKIP: "none of the 8 connected device(s) returned a reading, so there was nothing to check" |
| `runtime.aggregate_device` | PASS: "aggregate gamepad created, disabled and re-enabled under the same id (no aggregate device surfaced within 1.5 s)" | FAIL: "the aggregate gamepad did not surface within 1.5 s although 1 gamepad(s) are connected" |
| `vpad.aggregate_rumble` | SKIP: "the aggregate gamepad did not surface within 1.5 s" | FAIL: "the aggregate gamepad did not surface within 1.5 s although the virtual pad is connected" |
| Summary | PASS, 30/0/4, exit 0 | FAIL, 28/2/4, exit 1 |

With no gamepad connected, the aggregate check skips, because an aggregate
with no member never surfaces. Without the mutations, after the fix, the
virtual-pad tier gives 31/0/3 and the mock tier 25/0/4, both exit 0.

### E18. Callback gates and abandoned registrations

Found by the late reviews and the verifier
([Late reviews](#late-reviews)). A registration whose last
unregister failed was still freed at shutdown, so a late call could reach
a freed singleton. During a reading-callback change, a call from the old
registration was also accepted while the new token was not yet known.

**Change.** Every registration gets its own gate
([D16](#d16-each-callback-registration-has-its-own-gate)). Two debug
seams, `_test_fail_next_unregisters()` and `_test_get_unregister_state()`,
drive the retry and abandon paths, and `_test_get_unregister_state()` now
also reports `abandoned` and `module_pinned`.

**Falsified by.** Seven doctest cases for the gate
(`tests/cpp/gameinput/test_gameinput_callback_gate.cpp`), GUT tests for the
retry and abandon paths, and a bootstrap runner,
`exit_with_abandoned_callback.gd`, which quits Godot with an abandoned
registration and must exit 0. Each mutation was built, run and then
restored, and the restored file's hash matched the original.

| Mutation | Result |
| --- | --- |
| M1: the gate's `enter()` ignores the closed flag | The doctest binary hung and was killed after about 20 minutes |
| M1, with the committed test | Exit 1 in 0.3 s: 5 of 7 gate cases fail |
| M2: `wait_idle()` returns at once | 2 of 7 gate cases fail: "no caller is inside after close and wait_idle" and "wait_idle waits for a caller that entered first" |
| M3: the final failure deletes the gate | GUT fails "the registration is abandoned and its gate is never freed" and "the module is pinned so a late call finds mapped code". The bootstrap runner exits 1 with `{"unresolved":0,"abandoned":0,"module_pinned":false,"failures_left":0,"attempts":3}`. |

M1's hang was instructive. I first blamed the concurrent smoke case and
bounded it with a deadline, but the run still timed out after 120 s. The
hang was in "close turns later callers away without a count": under the
mutant the closed gate let in a caller that never leaves, and the case
then called `wait_idle()`. The committed case now has
`REQUIRE(gate.in_flight() == 0)` before that wait, which fails at once.
doctest buffers its output, so a hung case prints nothing. After the
batch, doctest gave 50/50 cases and 328/328 assertions.

### E19. A suite that never ran

The verifier ([§10](#10-reviews)) counted 66 test methods in the base but
only 60 in GUT's totals. The difference is
`test_gameinput_mapper_stuck_actions.gd`. It had never compiled, so GUT
printed "Ignoring script" for it, ran the other eleven scripts and exited
0. The verifier named the wrong suite; GUT's own log shows the resource
suite running and the stuck-actions suite ignored.

**Change.** `a180c21` makes the suite compile, and adds two frame waits
where a Mapper added late in a frame has not processed yet. A new
meta-test, `test_gameinput_suites_compile.gd`, loads every suite and fails
when one does not compile or does not extend `GutTest`.

**Falsified by.**

- The stuck-actions suite alone: 6 tests, 5 passed and 1 pending ("No
  GameInput keyboard device available to exercise null-reading path"), 34
  asserts.
- M13 added two scratch suites, one with a syntax error and one that does
  not extend `GutTest`. The meta-test failed, naming both:
  "test_zz_tmp_broken.gd does not compile" and "test_zz_tmp_not_gut.gd
  does not extend GutTest". It passes 1/1 before and after.

### E20. Reading epochs and poll re-entry

Found by the late reviews and the verifier. Readings buffered under an old
registration could be delivered after a `reading_received` handler changed
the reading mask. A handler could also shut the runtime down, start it
again and call `poll()`. That started a second drain, which took over the
first drain's gap marks, so the first could no longer release their device
references.

**Change.** An epoch and a non-nesting `poll()`
([D17](#d17-the-reading-drain-is-fenced-by-an-epoch-and-does-not-nest)).
The guard is checked before anything else in `poll()`, and `shutdown()`
does not clear it. When a handler stops the drain, `_drain_callback_events()`
releases every event and reading it did not deliver, then every gap mark's
device reference.

**Falsified by.** Mutations of `gameinput_singleton.cpp`, each restored
afterwards:

| Mutation | Result |
| --- | --- |
| M5: the drain ignores the epoch | Fails "no reading from the old registration is delivered after the change" |
| M8: a registration change discards nothing | 3 tests fail, including "readings queued under the old registration are discarded" and "the new registration delivers" |
| M10: the poll guard removed | 22 pass and 1 fails: "a poll() from a handler of the drain does nothing" |

### E21. Condition-effect signs and the float range

Found by the late reviews. Condition effects clamped a negative
`max_negative_magnitude` to 0, which removed the force in that direction,
and a finite double beyond the 32-bit float range passed validation and
became an infinite float.

**Change.** The GDK's signs
([D18](#d18-condition-effects-use-the-gdks-signs)), and every number must
fit a 32-bit float.

**Falsified by.**

| Mutation | Result |
| --- | --- |
| M6: a cap of the wrong sign is clamped again | Fails "'max_negative_magnitude' must be in [-1.0, 0.0]" and "'max_positive_magnitude' must be in [0.0, 1.0]" |
| M7: validation checks only that numbers are finite | Fails "'magnitude' must be finite and within the 32-bit float range" |

### E22. Keyboard layouts of 0x80000000 and above

Found by the late reviews. The layout was returned as a signed 32-bit
value, so a KLID of 0x80000000 or more came back negative. On a native
device, `get_device_info()` also reported the layout from connect time
after the layout had changed.

**Change.** Unsigned KLIDs everywhere
([D19](#d19-keyboard-layouts-are-unsigned)).

**Falsified by.** M9 sign-extends the accessor again. The test fails with
"[-536804343] expected to equal [3758162953]: the accessor is not
sign-extended" (0xE0010409). The native `get_device_info()` path cannot be
driven by the mock, so it is a manual check
([Keyboard](manual-tests.md#keyboard)).

### E23. Self-test hardening

Found by the late reviews: some checks could pass without the sample,
match the wrong virtual pad, or leave global state behind when they
stopped on a script error. `082b8ff` fixes each and makes the self-test
press every inspector toggle. A check that stops early now fails with
"check aborted before completing (script error, see the log above)", and
the global state it may have changed (the reading callback kinds, the
focus policy, the sample Mapper's target, a mock clock override and
running vibration) is put back before the next check.

**Falsified by.** Four mutant runs with a script error injected into one
check:

| Run | Result |
| --- | --- |
| R6: error in `mock.vibration`, with the reset | Exit 1, 24/1/4. The report records `reset_after_abort` = `time_override`, `vibration:20`, and the later checks pass. |
| R6b: the same, without the reset | Identical, 24/1/4. Here the reset is not load-bearing: `mock.restore` restarts the runtime and clears the override. |
| R6c: error in `vpad.rumble` after it sets the Mapper's target, with the reset | Exit 1, 30/1/3. `reset_after_abort` = `time_override`, `mapper_target`; only `vpad.rumble` fails. |
| R6d: the same, without the reset | Exit 1, 27/4/3. `sample.action_bridge` ("A did not press 'jump'"), `sample.player_jump` and `sample.disconnect_releases` fail too. |

R6d is the case the reset exists for: a Mapper left pinned to the virtual
pad makes three unrelated checks fail.

### E24. C# event accessors across threads

Found by the verifier, and the enum check by the samples review
([Late reviews](#late-reviews)). The custom `add` and `remove` accessors
did a plain `+=` and `-=` on the backing delegate, which can lose a handler
added on another thread at the same moment. Field-like events had been
atomic. Two first subscribers could also connect the native signals
twice.

**Change.** [D21](#d21-the-c-events-are-thread-safe). The parity check now
also fails on a managed enum member with no native constant.

**Falsified by.** A new test adds and removes 3,200 handlers from eight
threads.

| Mutation | Result |
| --- | --- |
| M11: a plain `+=` in the add accessor | 3 runs kept 487, 660 and 722 of 3,200 handlers |
| M12: a plain `-=` in the remove accessor | 3 of 3 runs left a handler behind instead of `null` |
| A managed `DeviceFamily.StaleMember` | "DeviceFamily.StaleMember has no native constant" |

### E25. The runner and the virtual-pad driver

Found by the late reviews and the verifier. The runner could read a
report left by an earlier run. When it was stopped it blocked in
`WaitForExit` until Godot exited by itself. It exited 1 for bad arguments
and quoted arguments that end in a backslash wrongly, and it stopped
watching the driver once the pad was ready.

**Change.** A run id
([D20](#d20-the-runner-trusts-only-a-report-with-its-run-id)), and a
report that cannot be deleted is exit 2. Stopping the runner (Ctrl+C or a
CI cancel) kills Godot. Bad arguments are exit 2, arguments are quoted the
way `CommandLineToArgvW` reads them, and a driver that exits, fails or
will not stop during the run is a harness error. The driver logs a
failure as an error event with its stage, still logs its stop event, and
exits 4. One case stays at exit 1: a named parameter with no value fails
in PowerShell's parameter binder before the script starts, and the
runner's help says so.

**Falsified by.**

| Test | Result |
| --- | --- |
| R1: two mock runs | Exit 0 both times, with two different 32-digit hex ids. The id on Godot's command line equals the report's. |
| R2: the self-test writes its id with `-other` appended | Exit 2: "The report's run_id is '…-other', not this run's …" |
| R3: the self-test writes no id | Exit 2: "The report's run_id is '', not this run's …" |
| R4 and R5: an empty `--gameinput-run-id=` to the GDScript and C# self-tests | Exit 2, "unknown or malformed option(s)" |
| T1: normal mock run | Exit 0, 25/0/4 |
| T2: the old report is locked by another process | Exit 2: "Could not delete … from an earlier run" |
| T4: stop the runner 3 s into a run | Returned in 0.4 s with Godot killed. The old runner took 1.6 s, because it waited for Godot to finish. |
| T5: kill the driver during a virtual-pad run | Exit 2: "The vpad driver exited -1 (no stop event) before the self-test finished" |
| T6: normal virtual-pad run | Exit 0, 31/0/3; the driver's last event is `stop` |
| Bad arguments: `-Bogus`, `-Bogus 1`, a stray word, and `-TimeoutSec` of `nope`, 0, -5, 86401 or 1.5 | Exit 2 for all eight, each with a message |
| A trailing `-TimeoutSec` with no value | Exit 1: "Missing an argument for parameter 'TimeoutSec'", from the binder, with no report |
| Quoting 12 awkward values through to Python | 12 of 12 round-trip; the old quoting managed 1 of 12 |
| Fake vgamepad: no error, an error while running, an error at teardown | Driver exits 0, 4 and 4, and the log ends with `stop` each time |

T3 tested an earlier version that compared the report's `started_utc` with
the run's start. The run id replaced it.

## 9. Bugs found

| Bug | Origin | Fixed in |
| --- | --- | --- |
| Exit crash with non-`self` lambdas connected | Base (v1 crashes too) | `e17537a` |
| Resting stick Y read -0.0 | Base | `d87bbdc` |
| Callback-fence comment said `UnregisterCallback` "does NOT fence" | Base (comment only) | `f4fa507` |
| Tutorial counted keyboards and mice as "Connected gamepads" | Base | `0593754` |
| Binding doc note had the Y sign backwards | Base (docs) | `690739c` |
| Devices with an empty display name (Remote Desktop keyboards and mice) | Base | `68b07fa` |
| Mapper's `target_device_id` lookup missed wheel, arcade and flight devices | Latent in base, exposed by the new kinds | `68b07fa` |
| Trigger and stick-direction bindings fired twice | This branch (new sources) | `a871812` |
| Class reference said 1024 ring entries and used a wrong label key | This branch (docs) | `36da8ab` |
| NuGet build failed to link | This branch (`get_haptic_info`) | `73f2d40` |
| Unnamed aggregates looked like Remote Desktop devices | This branch | `517cb04` |
| Aggregate kind masked instead of validated | This branch | `31d95d2` |
| Runner skipped the import after a C# build | This branch (tooling) | `e3805d3` |
| Connect-time status flags undocumented | This branch (docs) | `44ea079` |
| A failed callback unregister forgot its token | This branch (re-registration is new); v1 also dropped a token that failed at shutdown | `d3f1343` |
| A full reading ring flagged a gap on every device and missed one connected in the same poll | This branch | `d3f1343` |
| C# events stayed silent until something read `GameInput.Singleton` | Base (v1's two events); this branch added four more | `32e2387` |
| Runner exit codes: a hang returned 2, a crash passed its code through, an exception returned 1 | This branch (tooling) | `c82cc6f` |
| Self-test checks passed without observing a reading or the aggregate | This branch (sample) | `4b99359` |
| The class reference, `plugin.md` and the spec said readings cover every input kind; raw device reports are never read | This branch (docs) | `b0c18e5` |
| A registration whose final unregister failed was still freed at shutdown, so a late call could reach a freed singleton | This branch (`d3f1343` retried it, then freed it) | `0e78e52` |
| During a reading-callback change, a call from the old registration was accepted | This branch | `0e78e52` |
| Readings buffered under an old registration were delivered after a mask change | This branch | `0e78e52` |
| A `poll()` from a signal handler could run a second drain over the first one's gap marks | This branch | `0e78e52` |
| Condition effects clamped the GDK's negative `max_negative_magnitude` to 0, which removed all force that way | This branch | `20fb105` |
| A finite number beyond the 32-bit float range became an infinite float | This branch | `20fb105` |
| Keyboard layouts of 0x80000000 and above came back negative | This branch | `d9dac24` |
| A native keyboard's `get_device_info()` kept its connect-time layout | This branch | `d9dac24` |
| The tutorial gave a player to keyboards and mice, could put two players on one pad, and its player had no gravity | Base (docs) | `dc6917d` |
| `test_gameinput_mapper_stuck_actions.gd` never compiled, so its six tests never ran | Base | `a180c21` |
| Self-test gaps: `sample.*` checks skipped when `main.gd` had not handed over its scene, the virtual-pad checks could pick the wrong pad, an aborted check left global state behind, and a truncated report went unnoticed | This branch (sample) | `082b8ff` |
| C# event accessors could lose a handler added on another thread, and two first subscribers could connect the signals twice | This branch (`32e2387` added the accessors) | `a58d16d` |
| The C# enum check missed a managed member with no native constant | This branch (tooling) | `a58d16d` |
| Runner: a stale report was read, a stop waited for Godot, bad arguments exited 1, a trailing backslash was misquoted, and the driver went unwatched after it was ready | This branch (tooling) | `df28372` |
| The instructions file predated the gates, the epoch and abandoned registrations | This branch (docs) | `2808758` |

## 10. Reviews

- **Plan.** A synchronous rubber-duck review returned the 18 items in
  [§5](#5-plan-and-plan-review).
- **Implementation and sample.** Four reviews started in the background
  (the callback lifecycle, the C++ core, the samples and harness, and the
  GDScript harness) had not returned when the implementation was done.
  Instead each of the 18 plan items was audited against the code; that
  audit produced `e1eef1a`, `fd504e1`, `31d95d2` and `44ea079`. The four
  returned later, and their findings are in [Late reviews](#late-reviews).
- **Final review.** A synchronous rubber-duck review of the whole branch at
  `44ea079` returned nine items: two blocking, six non-blocking and one
  suggestion. Five were fixed in code, one was declined with evidence, and
  three were documentation fixes.

| # | Finding | Outcome | Evidence |
| --- | --- | --- | --- |
| 1 | A failed re-registration loses the old token, so a late callback can outlive `IGameInput` | Fixed in `d3f1343`: the token is kept and retried | [E14](#e14-failed-unregisters-and-per-device-gaps) |
| 2 | Ring-overflow gaps are global, not per device | Fixed in `d3f1343`: per-device gap marks | E14 |
| 3 | C# events never connect the bridge when a node only subscribes | Fixed in `32e2387`. Pre-dates v2. `IsAvailable` was left as is. | [E15](#e15-c-events-and-the-native-bridge) |
| 4 | The runner's exit codes break their contract when Godot hangs or crashes | Fixed in `c82cc6f` | [E16](#e16-runner-exit-codes) |
| 5 | Three self-test checks can pass having observed nothing | Fixed in `4b99359` | [E17](#e17-self-test-checks-that-passed-on-nothing) |
| 6 | The C# `ButtonToSource` and `AxisToSource` are instance methods returning `int` | Declined: changing either breaks v1 callers. Documented and self-tested instead (`32e2387`, `8faa9a0`). | E15 |
| 7 | The overview said "every GameInput input kind", but raw device reports are deferred | Fixed in the overview. The class reference, `plugin.md` and the spec said the same of readings (`b0c18e5`). | This commit |
| 8 | The log claimed a docs commit that did not exist yet and still had placeholders | Fixed: placeholders filled, and both documents committed last | This commit |
| 9 | The log said polling asks "once per readable kind"; the raw-controller kinds share one call | Fixed in [§3](#3-references), [§5](#5-plan-and-plan-review) and [D2](#d2-the-snapshot-is-per-kind) | This commit |

### Late reviews

The four background reviews were triaged after the final review, and a
fifth agent checked the nine final-review fixes at `b0c18e5`. Each finding
was checked against the code before it was fixed or declined. The fixes
are the eight commits from `0e78e52` to `df28372`.

Callback lifecycle, 2 findings:

| # | Finding | Outcome |
| --- | --- | --- |
| 1 | A registration whose last unregister failed is forgotten at shutdown, so a late call reaches a freed singleton | Fixed in `0e78e52` ([E18](#e18-callback-gates-and-abandoned-registrations)) |
| 2 | During a reading-callback change, `active == 0` accepts a call from an old registration | Fixed in `0e78e52`: the wildcard is gone, and the old registration's gate is closed first (E18) |

C++ core, 6 findings:

| # | Finding | Outcome |
| --- | --- | --- |
| 1 | The use-after-free of lifecycle finding 1 | Fixed in `0e78e52`. Its suggested fix, `StopCallback` before retrying, was declined: that call is what makes `UnregisterCallback` fail ([E1](#e1-stopcallback-makes-unregistercallback-fail)). |
| 2 | Condition effects clamp a negative `max_negative_magnitude` to 0 | Fixed in `20fb105` ([E21](#e21-condition-effect-signs-and-the-float-range)) |
| 3 | A reading-callback change does not fence readings already buffered, and a nested drain can consume gap marks | Fixed in `0e78e52` ([E20](#e20-reading-epochs-and-poll-re-entry)) |
| 4 | A native keyboard's `get_device_info()` keeps its connect-time layout | Fixed in `d9dac24` ([E22](#e22-keyboard-layouts-of-0x80000000-and-above)) |
| 5 | Layouts of 0x80000000 and above are sign-extended | Fixed in `d9dac24` (E22) |
| 6 | A finite value beyond the float range becomes an infinite float | Fixed in `20fb105` (E21) |

Samples and harness, 12 findings:

| # | Finding | Outcome |
| --- | --- | --- |
| 1 | `sample.hotplug_ui` passes without the sample | Fixed in `082b8ff` ([E23](#e23-self-test-hardening)) |
| 2 | The virtual pad is picked by VID:PID, which a physical Xbox 360 pad shares | Fixed in `082b8ff`: more than one match fails (E23) |
| 3 | The C# `api.facade` check compares enums one way | Fixed in `082b8ff`, and in the facade's parity test in `a58d16d` ([E24](#e24-c-event-accessors-across-threads)) |
| 4 | The C# `mock.typed_events` check ignores the device two events carry | Fixed in `082b8ff` |
| 5 | The C# self-test checks restoration by device count | Fixed in `082b8ff`: by app-local id |
| 6 | The tutorial's jump has no gravity | Fixed in `dc6917d` |
| 7 | The tutorial's disconnect fallback aliases another player's pad | Fixed in `dc6917d`: a freed slot goes to the next pad that connects |
| 8 | A stale report can pass an incomplete run | Fixed in `df28372` ([E25](#e25-the-runner-and-the-virtual-pad-driver)) |
| 9 | Ctrl+C can leave a child process running | Fixed in `df28372` (E25) |
| 10 | Bad arguments exit 1 before the trap is set | Fixed in `df28372`: exit 2 (E25) |
| 11 | A path that ends in `\` is quoted wrongly | Fixed in `df28372` (E25) |
| 12 | The driver is not watched after it is ready | Fixed in `df28372` (E25) |

GDScript harness, 9 findings (it read `0593754`):

| # | Finding | Outcome |
| --- | --- | --- |
| 1 | `runtime.readings` passes when no device returns a reading | Already fixed in `4b99359` ([E17](#e17-self-test-checks-that-passed-on-nothing)). `082b8ff` also requires input kinds and a timestamp. |
| 2 | The aggregate check accepts an aggregate that never surfaces | Already fixed in `4b99359` and `e1eef1a` (E17) |
| 3 | Without `main.gd`'s scene the `sample.*` checks skip | Fixed in `082b8ff`: they fail |
| 4 | `runtime.reading_callbacks` ignores the result of the restore | Fixed in `082b8ff`. Delivery needs live input, which `vpad.input` checks. |
| 5 | `sample.inspector` presses none of the inspector's controls | Partly fixed already in `52c2a39` (the rumble buttons). `082b8ff` adds the event toggle and the FFB pulse. |
| 6 | An aborted check leaves global state behind | Fixed in `082b8ff`: `reset_after_abort` (E23) |
| 7 | `mock.restore` compares device counts, and the setters' override flags stay set | Fixed in `082b8ff`: returning devices are matched by app-local id and the callback kinds are checked. The override flags were left as they are, since the process exits after its report. |
| 8 | The inspector logs success when GameInput refuses a call | Partly fixed already in `52c2a39` (rumble and FFB). In `082b8ff` the event toggle turns itself off when refused. |
| 9 | A truncated report still exits 0 | Fixed in `082b8ff`: the report is read back |

Verifier, the nine final-review fixes at `b0c18e5`:

| # | Status | Outcome |
| --- | --- | --- |
| 1 | Not resolved: the final retry still forgot the token | Fixed in `0e78e52` (E18) |
| 2 | Regressed: a handler that restarts the runtime can nest a drain, which overwrites the outer drain's gap marks and so leaks their device references | Fixed in `0e78e52`: `poll()` does not nest, and the outer drain releases every reference it still holds when a handler restarts the runtime (E20) |
| 3 | Regressed: the custom C# accessors lost the atomicity of field-like events | Fixed in `a58d16d` (E24) |
| 4 | Not resolved: a stale or concurrent report can still pass | Fixed in `df28372` with a run id (E25). The schema check and atomic publish it also suggested were not added: a report from any other run fails the run-id check, and the self-test reads its report back after writing it. |
| 5 to 9 | Resolved | None needed |

The verifier also counted 66 test methods in the base where the log said
60. The count was right, but the suite it named does run; the missing six
were a suite that never compiled
([E19](#e19-a-suite-that-never-ran)).

## 11. Final validation

Run on `3b9f052` in a locked Remote Desktop session. That commit is an
earlier version of `df28372` with the same tree (`eb5924a1`). Only its
subject and then its email changed afterwards
([§7](#7-commits-and-their-evidence)). The commits after
`df28372` change only Markdown, so the DLL does not change. The matrix
script, `gi_final_matrix.ps1`, writes one log per step. It and the other
helpers named in this log (the link checker, the snippet checker, the
mutation scripts and the fake Godot) were written for this validation and
are kept with the probes ([§4](#4-probes-before-coding)), outside the
repository.

| Check | Result |
| --- | --- |
| Debug build (`--preset debug`) | Exit 0, no warnings from addon sources. The four copies of the debug DLL (addon, both samples, test host) have one SHA-256. |
| NuGet header build ([E6](#e6-the-nuget-header-does-not-link)) | Exit 0. Its only warnings are eight C4146 from godot-cpp (`rendering_server.hpp`, `error_macros.cpp`). It overwrites the debug DLL, which was then relinked: the four copies are all `CD3C39614F47978C…`, and the mock self-test on them gave 25/0/4. |
| Release build (`release-gameinput`) | Exit 0 |
| Test seams ([E7](#e7-test-seams-are-absent-from-release)) | Debug DLL 4,422,144 bytes with 18 `_test_*` names; release DLL 629,760 bytes with 0 |
| GDScript parse gate | Exit 0 |
| Doctest | 50/50 test cases, 328/328 assertions |
| GUT on Godot 4.6.1 | 200 tests: 184 passed, 16 pending, 0 failed, 11,979/11,979 asserts; 5/5 bootstrap runners. The pending tests are the 15 `Tier=live_read` tests and the stuck-actions keyboard test ("No GameInput keyboard device available to exercise null-reading path"). |
| GUT on Godot 4.7.1 | Same as 4.6.1 |
| GUT on Godot 4.5.1 (`Gut-4.5`) | Same as 4.6.1 |
| GUT live tier with a ViGEm pad, Godot 4.6.1 | 200 tests: 198 passed, 2 pending, 0 failed, 13,641/13,641 asserts; 5/5 bootstrap runners. The pending tests are `test_live_force_feedback_round_trip` (no device has force feedback) and the stuck-actions keyboard test. The driver logged the live rumble test's pulses, 255/0 and 128/64. |
| C# facade tests | 119/119 |
| Sample self-test, mock | 25 passed, 0 failed, 4 skipped; exit 0. Skips: `runtime.aggregate_device` ("no gamepad is connected, so the aggregate had no member and did not surface"), and vibration, force feedback and haptics (no device has them). |
| Sample self-test, virtual pad | 31 passed, 0 failed, 3 skipped; exit 0. `vpad.rumble`: large=191 small=64, auto-stop 0.30 s later. `vpad.aggregate_rumble`: large=153 small=102, auto-stop 0.50 s later. Skips: force feedback, haptics, and `vpad.input` ("GameInput delivered no input from the virtual pad in 8 s (the session is locked; this is a Remote Desktop session (RDP-Tcp#0); Godot runs headless, with no focused window); rumble is still checked"). |
| C# sample self-test, Godot 4.7.1 .NET | 14 passed, 0 failed, 0 skipped; exit 0 |
| Doc snippets | All 15 GDScript blocks (7 in `plugin.md`, 6 in the tutorial, 2 in `manual-tests.md`) pass `--check-only` inside the sample (`gi_docsnip_check.ps1`). An undeclared identifier added to one block fails. |
| Markdown links | All 262 relative links and anchors resolve in the 12 Markdown files this branch changes or adds, these two included (`gi_linkcheck.py`). A broken anchor and a missing file added to the overview were both reported, with exit 1. |

Every report carries `"schema": "gameinput-selftest/1"` and the runner's
`run_id`. The session block is
`{"locked":true,"name":"RDP-Tcp#0","remote":true}`, and the build block is
`{"debug":true,"mock_seams":true,"singleton":true}`. Earlier runs on
`44ea079`, in an unlocked Remote Desktop session, and on `b0c18e5` gave the
same results, apart from the tests and checks added since.

## 12. How to reproduce

Run from a Visual Studio x64 developer prompt, or call `vcvars64.bat` in the
same `cmd` first, at the repository root.

```powershell
# Build (debug is what the tests load; the release preset checks the seams)
cmake --preset default
cmake --build --preset debug
cmake --build --preset release-gameinput

# The NuGet header path (E6)
cmake --preset gameinput-only -B build/gi-nuget-check -DGAMEINPUT_SOURCE=nuget -DGDK_BUILD_TESTS=OFF
cmake --build build/gi-nuget-check --config Debug --target godot_gameinput
# It writes the same debug DLL: delete it, then rebuild the default preset
# so the DLL is relinked and copied again.

# GDScript parse gate
pwsh tools/check_gd_scripts_headless.ps1

# GUT host (doctest runs first) on the Godot in GODOT_CONSOLE
pwsh tools/run_all_tests.ps1 -SkipBuild -Hosts tests/godot/gameinput -SkipOrchestrator

# Live tier: start the virtual pad first (see the sample README), then
pwsh tools/run_all_tests.ps1 -SkipBuild -Live -Hosts tests/godot/gameinput -SkipOrchestrator

# C# facade tests
pwsh tools/run_csharp_tests.ps1

# Sample self-tests: mock, virtual pad, C#
pwsh tools/run_gameinput_selftest.ps1
pwsh tools/run_gameinput_selftest.ps1 -VirtualPad
pwsh tools/run_gameinput_selftest.ps1 -Project sample/tutorial_gameinput_csharp -Godot <Godot .NET console>
```

Count the test seams in a built DLL (E7):

```powershell
$dll = 'addons\godot_gameinput\bin\godot_gameinput.windows.release.x86_64.dll'
$text = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($dll))
@([regex]::Matches($text, '_test_[a-z_]+') | ForEach-Object Value | Sort-Object -Unique).Count
```

To repeat a mutation, edit the line named in the experiment, rebuild, run
the one suite, restore the file from a backup, and set its modification time
to now so the build notices.

## 13. Gotchas

- The DLL embeds the doc XML. Rebuild after editing `doc_classes`, or the
  editor shows the old text and the parity tests read stale data.
- A file restored from a backup can keep an older timestamp than its object
  file, and the build then keeps the mutated code. Touch the file after
  restoring it.
- The NuGet check build writes the same debug DLL as the default build.
  Delete `addons/godot_gameinput/bin/godot_gameinput.windows.debug.x86_64.dll`
  and rebuild the default preset, then check that the four copies have one
  hash.
- Import a project twice after deleting `.godot`
  ([E11](#e11-the-first-headless-import-can-crash)).
- Under `--headless` the root viewport is 64 × 64
  ([E4](#e4-the-headless-viewport-is-64-by-64)).
- Remote Desktop and locked sessions get no GameInput input, but rumble
  still works ([E10](#e10-remote-and-locked-sessions-get-no-gameinput-input)).
- Running GUT directly (without `run_all_tests.ps1`) prints a harmless
  `SCRIPT ERROR` from `gut_loader.gd:35`.
- Godot 4.5 needs the older GUT copy, which `run_all_tests.ps1` picks.
- Type `GameInputDevice` variables as `GameInputDevice`, not `Object`
  ([E12](#e12-is_connected-and-objectis_connected)).
- GUT prints only the assert total when every assert passes (`Asserts 128`)
  and passed/total otherwise (`123/127`). A failing assert can skip later
  ones, so the totals differ between runs.
- `pwsh -File script.ps1 --option=C:\path` splits the argument at the drive
  colon. The fake Godot in [E16](#e16-runner-exit-codes) reads its report path
  from `[Environment]::CommandLine` instead.
- GUT prints "Ignoring script" for a suite that does not compile, runs the
  rest and still exits 0. `test_gameinput_suites_compile.gd` now fails in
  that case ([E19](#e19-a-suite-that-never-ran)).
- doctest buffers its output, so a hung case prints nothing. Run it with a
  timeout ([E18](#e18-callback-gates-and-abandoned-registrations)).
- `$env:TEMP` on this machine is an 8.3 short path
  (`C:\Users\ZACHHO~1.RED\…`), but Godot and `Win32_Process` report long
  paths. Expand the short prefix before comparing paths.
- The Visual Studio generator writes no `compile_commands.json`. The
  compiler switches are in
  `build/addons/godot_gameinput/godot_gameinput.dir/Debug/godot_gameinput.tlog/CL.command.1.tlog`,
  which is UTF-16.

## 14. Not verified here

Nothing below ran on this machine. Most have a section in
[manual-tests.md](manual-tests.md).

- Force feedback on a real wheel or joystick, including which way a
  default spring pulls: [Force feedback](manual-tests.md#force-feedback)
- Haptics on a haptics-capable controller:
  [Haptics](manual-tests.md#haptics)
- Live input from a pad at a physical, unlocked console:
  [Virtual gamepad in a local session](manual-tests.md#virtual-gamepad-in-a-local-session)
- Guide and Share buttons:
  [Guide and Share buttons](manual-tests.md#guide-and-share-buttons)
- Elite paddles, flight-stick axis signs, wheel units and motion sensors:
  [Arcade stick, flight stick, racing wheel, motion sensors](manual-tests.md#arcade-stick-flight-stick-racing-wheel-motion-sensors)
- Extended scan codes, a switch of input language, and layouts of
  0x80000000 and above: [Keyboard](manual-tests.md#keyboard)
- An aggregate over two real pads:
  [Aggregate devices](manual-tests.md#aggregate-devices)
- The Mapper's duplicate suppression in a real `_input` loop:
  [Mapper](manual-tests.md#mapper--inputis_action_pressed-integration)
- The release DLL at run time. It was built and inspected, not run.
- The `installed-gdk` presets end to end. Only the NuGet header path was
  built.
- A real `UnregisterCallback` failure. GameInput never refused one here,
  so only the debug seam `_test_fail_next_unregisters()` drives the retry
  and abandon paths.
- The Xbox and PlayFab C# facades still connect their signals through an
  unlocked `_signalsConnected` flag, as the GameInput facade did before
  `a58d16d`. They are outside this branch and were not changed.
- C# `Callable.From` handlers still connected at exit (the GDScript case is
  E2).
- The root cause of the first-import crash (E11).
- **Xbox Series X|S.** Nothing ran on a console. The CMake presets build
  Windows x64 only, so the console build of this addon is not set up in this
  repository.
