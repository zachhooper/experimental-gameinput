# GameInput v2: review overview

This branch implements much more of GameInput in the `godot_gameinput` addon
for [issue #97](https://github.com/microsoft/XBOX-Godot-Sample/issues/97), for
both GDScript and C#. It also turns `sample/tutorial_gameinput` into an
automated integration check.

The branch is `feat/gameinput-v2`, based on `55735f7`. Nothing has been
pushed to microsoft/XBOX-Godot-Sample and no pull request was opened there.
The evidence behind every statement here is in the
[development log](v2-dev-log.md).

## Suggested reading order

1. [plugin.md](plugin.md): the GDScript guide, with every new feature
2. [csharp.md](csharp.md): the same from C#
3. [The sample README](../../sample/tutorial_gameinput/README.md): the
   inspector, the self-test and the virtual pad
4. [manual-tests.md](manual-tests.md): what needs a person and hardware
5. [spec/gdext-gameinput.md](../../spec/gdext-gameinput.md): scope, design
   decisions and what was deferred
6. [v2-dev-log.md](v2-dev-log.md): decisions, experiments and test evidence

## What was added

**Every structured input kind.** Beyond gamepads, the addon now reads
keyboards (physical keys and key details), mice (buttons, movement, wheel),
motion sensors, arcade sticks, flight sticks, racing wheels and raw
controller state. Raw device reports (HID) are the one kind left out
([Not in this branch](#not-in-this-branch)). Gamepads gain the trigger
buttons, stick directions and Elite paddles. One set of `Source` helpers
covers gamepads, arcade sticks, flight sticks and racing wheels:

- `is_source_down()`, `was_source_pressed()` and `was_source_released()`
  for digital input
- `get_source_value()` for analog input

**Events.** Four new signals, each with a timestamp:

- `device_status_changed`
- `reading_received`: opt in to catch presses shorter than a frame
- `system_buttons_changed`: Guide and Share
- `keyboard_layout_changed`

**Haptics.**

- `start_vibration(weak, strong, duration)` has the shape of Godot's
  `Input.start_joy_vibration`. It adds impulse-trigger rumble and stops on
  its own when the duration ends.
- Force-feedback effects: all 11 kinds, as a `GameInputForceFeedbackEffect`
  handle.
- Haptic device information.

**Devices and runtime.**

- Device family, status, button labels and ids
- Background input (focus policy)
- Aggregate devices, which merge every device of one kind (for example every gamepad) into one
- The GameInput clock
- Two project settings

**Threading.** GameInput calls the addon on its own threads. Those
callbacks only queue work, and `poll()` emits every signal on the main
thread. Each callback registration has its own gate, so shutting down or
calling `set_reading_callback_kinds()` cannot race a late callback, and a
reading from an old registration is dropped.

**Action bridge.** `GameInputMapper` binds arcade, flight-stick and wheel
controls. Stick-direction and trigger bindings no longer fire twice
alongside Godot's default `ui_*` actions.

**C#.** Every new member has a typed C# counterpart. The parity tests now
also check enum values in both directions, and that subscribing to an
event is enough to receive it. Handlers can be added and removed from any
thread.

**Testing.**

- A debug-only mock backend
- 134 new GUT tests in 11 new suites, one of which fails when any suite
  does not compile, and 34 C++ doctest cases
- A virtual-gamepad driver (ViGEm), and a self-test runner that accepts
  only a report carrying its own run id

**Sample.** The tutorial gains:

- a live inspector panel
- a rumble when the player jumps
- `-- --gameinput-selftest`, which checks the whole integration, prints
  PASS, FAIL or SKIP for each check, writes a JSON report and exits 0, 1,
  2 or 3

The C# sample has its own self-test.

| Measure | Before | After |
| --- | --- | --- |
| Classes / methods / signals / constants | 6 / 50 / 2 / 49 | 7 / 143 / 6 / 274 |
| GUT tests run (GameInput host) | 60 of 66 | 200 of 200 |
| C++ doctest cases | 16 | 50 |
| C# facade tests | 112 | 119 |

Without a live device, 16 of the 200 GUT tests are pending. With the
virtual pad, 2 are: force feedback, which the pad does not have, and a
test that needs a GameInput keyboard.

All automated checks pass. They are the debug, release and NuGet builds,
the C++ doctests, GUT on Godot 4.5.1, 4.6.1 and 4.7.1, the live tier with a
virtual pad, the C# tests, the three self-tests, a parse check of every
GDScript block in the docs and a check of every relative link
([dev log §11](v2-dev-log.md#11-final-validation)).

## What hit issues

| Issue | What happened |
| --- | --- |
| Godot crashed on exit when a lambda was still connected to a GameInput signal | Existing bug (v1 too). Fixed: the addon disconnects everything at engine shutdown. |
| Calling `StopCallback` before `UnregisterCallback` made unregistering fail 6-10% of the time | Not adopted. Teardown uses `UnregisterCallback` alone, which the GDK reference supports. |
| The NuGet GameInput header did not link | Fixed. The haptic location GUIDs are no longer taken from the header. |
| Stick-direction bindings fired twice with Godot's `ui_*` actions | Fixed in the Mapper. |
| Aggregates: most kinds return `E_NOTIMPL`, and disabling one does not remove it | The addon now refuses unsupported kinds and documents the disable behaviour. |
| Remote Desktop and locked sessions get no GameInput input | Live input is skipped, with the reason in the report. Rumble is still checked end to end. |
| The first headless import of a project can crash | Worked around by importing twice. Root cause unknown. |
| `device.is_connected()` fails to parse when the variable is typed `Object` | Existing v1 name, left as is and noted in the log. |
| Four background reviews did not return in time | The plan review was audited item by item instead, then a final synchronous review ran. The four arrived later with 29 findings, and a fifth agent checked the final review's fixes. Each finding was fixed or answered ([dev log §10](v2-dev-log.md#late-reviews)). |
| A registration GameInput would not release could call into a freed singleton | Found by the final review, and again by the late reviews and the verifier after the first fix. Fixed: each registration has its own gate, and one that is never released is abandoned safely. |
| Readings from before a reading-callback change could still arrive, and a `poll()` inside a signal handler could nest and leak device references | Found by the late reviews and the verifier. Fixed: readings carry their registration's epoch, `poll()` does not nest, and a drain that a handler stops releases everything it holds. |
| A full reading buffer flagged a gap on every device | Found by the final review. Fixed: gaps are tracked per device. |
| Spring, damper, friction and inertia effects defaulted to the opposite sign from the GDK, and a negative cap was clamped to 0 | Found by the late reviews. Fixed to match the GDK's SimpleFFBWheel sample. The direction on a real wheel still needs a check. |
| Keyboard layouts of 0x80000000 and above came back negative | Found by the late reviews. Fixed, and `get_device_info()` now reports the current layout. |
| C# event handlers heard nothing when only GDScript had touched GameInput | Existing v1 behaviour, found by the final review. Fixed: adding a handler connects the C# events. |
| That C# fix was not thread-safe | Found by the verifier. Fixed: handlers are added atomically, and the events connect once. |
| An existing test suite never compiled, and GUT skipped it without failing | Existing bug, found through the verifier's test count. Fixed, and a new suite fails when any suite does not compile. |
| The tutorial's per-player recipe could put two players on one pad, and its player had no gravity | Existing tutorial bug, found by the late reviews. Fixed. |
| The self-test runner could return the wrong exit code or trust an old report, and some self-test checks could pass without testing what they name | Found by the final and late reviews. Fixed: a hang returns 3, and a crash or a missing report returns 2. The runner accepts only a report carrying its run id, those checks fail or skip with a reason, and an aborted check cannot leave state behind. |
| C#'s `ButtonToSource()` and `AxisToSource()` are instance methods, although they are static natively | Kept, because v1 shipped them that way. Documented, with the workaround: name the `Source` value directly. |

## What needs manual testing

None of these could run on this machine. Each links to its checklist.

- [ ] A pad at a physical, unlocked console: live input reaching the game
      ([checklist](manual-tests.md#virtual-gamepad-in-a-local-session))
- [ ] Force feedback on a real wheel or flight stick, including which way a
      default spring pulls
      ([checklist](manual-tests.md#force-feedback))
- [ ] Haptics on a haptics-capable controller
      ([checklist](manual-tests.md#haptics))
- [ ] Guide and Share buttons
      ([checklist](manual-tests.md#guide-and-share-buttons))
- [ ] Elite paddles, flight-stick axis signs, wheel units, motion sensors
      ([checklist](manual-tests.md#arcade-stick-flight-stick-racing-wheel-motion-sensors))
- [ ] Keyboard extended keys, a layout switch, and layouts of 0x80000000
      and above
      ([checklist](manual-tests.md#keyboard))
- [ ] An aggregate over two real pads
      ([checklist](manual-tests.md#aggregate-devices))
- [ ] Mapper duplicate suppression in a real game loop
      ([checklist](manual-tests.md#mapper--inputis_action_pressed-integration))
- [ ] The release DLL in a running game (built and inspected only)
- [ ] **Xbox Series X|S.** Nothing ran on a console. The repository's
      presets build Windows x64 only.

## Not in this branch

These are listed in [plugin.md](plugin.md#not-covered-yet) and the
[spec](../../spec/gdext-gameinput.md#deferred):

- raw HID reports
- remapping labels
- dispatcher control
- haptic waveform playback
- an XInput fallback
- sub-frame taps inside `GameInputMapper` (use `reading_received`)

The Xbox and PlayFab C# facades still connect their signals through an
unlocked flag, as the GameInput facade did before this branch fixed it.
They are outside this change and were left as they are
([dev log §14](v2-dev-log.md#14-not-verified-here)).

## Changed docs

- [plugin.md](plugin.md), [csharp.md](csharp.md) and
  [manual-tests.md](manual-tests.md)
- [The action-bridge tutorial](../tutorials/gameinput-action-bridge.md) and
  the [tutorials index](../tutorials/README.md)
- The [GDScript sample README](../../sample/tutorial_gameinput/README.md) and
  the new [C# sample README](../../sample/tutorial_gameinput_csharp/README.md)
- [spec/gdext-gameinput.md](../../spec/gdext-gameinput.md) and
  [spec/gdext-csharp.md](../../spec/gdext-csharp.md)
- [The Copilot instructions for the addon](../../.github/instructions/godot-gameinput.instructions.md)
- The class reference in `addons/godot_gameinput/doc_classes/`, which shows
  in the Godot editor's help
