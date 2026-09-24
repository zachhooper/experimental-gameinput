---
description: Godot GameInput addon architecture, threading model, action-bridge conventions, and sample workflow
applyTo: "addons/godot_gameinput/**, addons/godot_gameinput_csharp/**, tests/godot/gameinput/**, sample/tutorial_gameinput/**, sample/tutorial_gameinput_csharp/**, docs/gameinput/**, docs/tutorials/gameinput-action-bridge.md, spec/gdext-gameinput.md, tools/run_gameinput_selftest.ps1, tools/virtual_gamepad/**"
---

# Godot GameInput Addon Instructions

## Public Architecture

- `GameInput` is the only engine singleton registered by this addon. Access
  via `Engine.get_singleton("GameInput")` (or just `GameInput` once cached
  from the singleton).
- Wrapper classes — `GameInputDevice`, `GameInputReading`,
  `GameInputForceFeedbackEffect`, `GameInputBinding`, `GameInputActionMap`,
  `GameInputMapper` — are part of the public Godot-facing contract. Treat
  their inspector-visible properties, signals and constant values as stable
  surfaces; add enum values and dictionary keys, never renumber or remove
  them.
- The addon is standalone: there is no build-time or runtime dependency on
  `godot_gdk`.

## Threading & Lifecycle

- `IGameInput` callbacks (device, reading, system-button and keyboard-layout)
  fire on GameInput-owned worker threads. Those threads MUST NOT mutate the
  device cache, emit Godot signals, or call into godot-cpp APIs. Worker
  callbacks may only push events into the mutex-protected pending queue (or,
  for readings, the preallocated reading ring) and `AddRef` the native device
  pointer.
- Every callback runs inside the callback fence: return early unless
  `_enter_callback()` succeeds, and call `_leave_callback()` on every path
  after it. `shutdown()` clears `m_accepting_callbacks`, unregisters, then
  `_wait_for_callbacks()` drains `m_callbacks_in_flight` before releasing
  anything. A new callback that skips the fence is a use-after-free at
  shutdown.
- Unregister with `UnregisterCallback` only. Calling `StopCallback` first
  makes `UnregisterCallback` fail intermittently on the GameInput 3.3
  runtime, and a failed unregister does not fence the callback.
- Never forget a token whose unregister failed. `_unregister_callback()`
  keeps it in `m_unresolved_callback_tokens`;
  `_apply_reading_callback_registration()` retries the list and
  `shutdown()` makes a final attempt while `m_game_input` is still alive.
- Ring evictions mark only the device that lost the reading (`GapMark`,
  fixed table under `m_event_mutex`; past `kMaxGapMarks`, every device). Do
  not reintroduce a global overflow flag: it flags devices that lost
  nothing and misses devices that connect in the same drain.
- The main thread drains the pending queue inside `GameInput::poll()`. Signals
  are emitted from there — no `call_deferred` plumbing needed. Device events
  and event readings share a sequence number; keep them merged in that order.
- `GameInput::poll()` is per-frame idempotent. The check uses
  `Engine::get_singleton()->get_process_frames()`. Real refresh runs at most
  once per frame regardless of how many `GameInputMapper` nodes (or the
  bootstrap autoload) call `poll()` defensively.
- `prev` button state for `GameInputReading::was_button_pressed()` /
  `was_button_released()` is updated only on the real refresh. Multiple
  `poll()` calls in the same frame won't drop edges.
- Signal connections are dropped by the GDExtension main-loop shutdown
  callback, before the script languages are torn down. Keep it: without it a
  non-`self` GDScript lambda still connected at quit crashes Godot on exit
  (`tests/godot/gameinput/tests/bootstrap/exit_with_connected_lambdas.gd`).

## Readings

- A reading is a fixed-size POD snapshot (`gameinput_snapshot.h`). Never keep
  an `IGameInputReading*` past the call that produced it; copy what you need
  into the snapshot.
- `_real_poll()` issues one `GetCurrentReading()` per kind group
  (`kPollGroups`). A new reading kind needs a snapshot field, a
  `_fill_snapshot_from_reading()` branch, a poll group, a mock path in
  `_test_push_reading()`, reading getters, doc XML, C# facade members and a
  GUT suite.
- Thumbstick Y follows Godot's convention (down is positive). Flip signs by
  subtracting from `0.0`, not by negating, so a resting axis reads `+0.0`.

## Soft-Fail Conventions

- Every public method on `GameInput`, `GameInputDevice`, `GameInputReading`,
  and `GameInputMapper` must return a safe default (`false`, `0`, `-1.0`,
  empty `Array`, `null`, etc.) and emit at most one `push_warning` when the
  runtime is uninitialized, the addon is not loaded, or a referenced device
  has been disconnected. Crashes from calling into a stale or absent runtime
  are bugs.
- `_ensure_initialized()` is the standard helper in `gameinput_singleton.cpp`.
  Use it in every new public method.
- `GameInputMapper` debounces missing-action warnings via
  `m_warned_missing_actions`; do not spam `push_warning` per-frame.

## Device IDs

- `int64_t` device ids are session-local monotonic and **never recycled**.
  After disconnect the id is retired permanently. New devices get fresh ids.
- `GameInputDevice` wrappers hold only the id (a weak handle), never a raw
  `IGameInputDevice*`. This is what makes stale wrappers safe — they become
  inert once the underlying device is gone.
- `GameInputForceFeedbackEffect` follows the same rule: it holds an effect id,
  and the singleton's registry owns the native effect, releasing it on
  `release()`, wrapper free, device disconnect and shutdown.

## Action Bridge Rules

- `GameInputMapper` emits **actions only** (`Input.action_press` /
  `Input.action_release`). Do not also synthesize `InputEventJoypadButton` /
  `InputEventJoypadMotion` — that creates a split-brain.
- Actions referenced by a `GameInputBinding` must already exist in Godot's
  `InputMap`. Mapper warns once per missing action and skips it.
- `GameInputBinding` is a `Resource` with typed exports. New binding fields
  must keep the existing inspector-friendly pattern (typed `@export`, hint
  ranges for floats, dropdowns for enums).
- `GameInputActionMap.bindings` is a `TypedArray<GameInputBinding>`; preserve
  the `PROPERTY_HINT_ARRAY_TYPE` registration so the editor renders an
  inspector for resource children.

## C++ and Registration Conventions

- Every header that includes Windows / GameInput APIs must define
  `WIN32_LEAN_AND_MEAN` and include `<windows.h>` before Godot or GameInput
  headers.
- **Never name a local header the same as an SDK header (case-insensitively).**
  Windows file systems are case-insensitive; `gameinput.h` will silently
  shadow `<GameInput.h>` and break the build with hundreds of cryptic
  errors. The singleton header is named `gameinput_singleton.h` for that
  reason. Keep new file names disambiguated.
- Code must build against both GameInput headers the repo can pick: the
  vcpkg port (3.3.195 at the current baseline) and the NuGet pinned for
  `GAMEINPUT_SOURCE=nuget` (`installed-gdk` presets, 3.1.26100.6879). The
  pinned header declares the `GAMEINPUT_HAPTIC_LOCATION_*` GUIDs with
  `DEFINE_GUID` and `GameInput.lib` does not define them, so naming them
  fails to link there (LNK2001). Use the copies in `gameinput_labels.h`. To
  check, configure a separate build with
  `cmake --preset gameinput-only -B build/gi-nuget-check -DGAMEINPUT_SOURCE=nuget -DGDK_BUILD_TESTS=OFF`
  and build its `godot_gameinput` target.
- Register new native classes in
  `addons\godot_gameinput\src\register_types.cpp`. Add new
  implementation files to the `_GAMEINPUT_SRCS` list in
  `addons\godot_gameinput\CMakeLists.txt`.
- When exposing object-returning properties from C++, set the `PropertyInfo`
  class name (e.g. `GameInputActionMap`) so Godot does not instantiate
  anonymous object defaults.
- Every project setting introduced by the addon goes through
  `_register_setting()` in `register_types.cpp`. Always gate writes with
  `has_setting()` so editor reloads stay clean, then call
  `set_initial_value()` and `add_property_info()`.

## EditorPlugin / Bootstrap

- The single bootstrap surface is the `GameInputBootstrap` autoload installed
  by `editor/gameinput_editor_plugin.gd::_enable_plugin()`. Project settings
  alone never bootstrap runtime logic.
- The autoload only calls `shutdown()` if it owned the `initialize()` (the
  `_initialized_here` flag). This keeps editor reloads and tests that drive
  the runtime themselves working correctly.

## Sample Integration

- `sample/tutorial_gameinput/` ships as the standalone GameInput action-bridge
  sample. It uses the `GameInputBootstrap` autoload, displays connected-device
  count and hot-plug events, and is the canonical manual host for mapper,
  device discovery, and hot-plug checks. Its right half is the device
  inspector (`inspector/gameinput_inspector.gd`): live per-kind state, an
  event log, and rumble, trigger and force-feedback buttons, which the
  [GameInput manual-test checklist](../../docs/gameinput/manual-tests.md) is
  written around.
- Both samples run an integration self-test with `--gameinput-selftest`
  (`selftest/gameinput_selftest.gd`, `SelfTest/GameInputSelfTest.cs`). Every
  check reports PASS, FAIL or SKIP with a reason into a JSON report; exit code
  0 means no failures (and, with `--gameinput-strict`, no skips). Drive it
  with `tools/run_gameinput_selftest.ps1` (`-VirtualPad` adds the
  `tools/virtual_gamepad/vpad_driver.py` ViGEm pad; `-Project
  sample\tutorial_gameinput_csharp` runs the C# sample). When you add public
  API, add or extend a self-test check, and keep the check tables in both
  sample READMEs current.
- Live GameInput input needs an unlocked, local interactive session. A locked
  or disconnected Remote Desktop session receives no readings, so
  input-dependent checks SKIP there rather than FAIL.
- A GameInput scenario panel inside the tutorial sample tracks is not present
  yet.
- The headless test entry point for GameInput is the repo-root orchestrator:

```powershell
pwsh -NoLogo -NoProfile -ExecutionPolicy Bypass -File .\tools\run_all_tests.ps1
```

  GameInput suites run in the `gut:tests/godot/gameinput` stage; bootstrap-autoload coverage runs in the `bootstrap:tests/godot/gameinput:*` stages. To iterate on the GameInput host alone:

```powershell
cd tests\godot\gameinput
..\..\..\sample\Godot_v4.6.1-stable_win64_console.exe --headless -s res://addons/gut/gut_cmdln.gd -gdir=res://tests -ginclude_subdirs -gexit
```

  GameInput suites live in `tests/godot/gameinput/tests/` and `extends "res://addons/godot_gdk_tests/gameinput_test_base.gd"` (mirrored from `addons\godot_gdk\tests_support\bases\gameinput_test_base.gd`). Add new suites as `test_*.gd` files in that directory — GUT discovers them automatically via `-gdir` + `-ginclude_subdirs`; there is no central registration script. Bootstrap-autoload scenarios go under `tests\godot\gameinput\tests\bootstrap\` as one-shot scripts the orchestrator launches in fresh Godot processes.

## GDScript Conventions

- snake_case for methods and properties; `&"action_name"` for action `StringName`s.
- Avoid `:=` when the right-hand side comes from a Variant-returning engine
  API (e.g. `gi.initialize()`) — the parser cannot infer the type. Use
  `var x: bool = gi.initialize()` instead.
- For float comparisons in tests, use `assert_eq_approx` (a GUT built-in
  assertion) — C++ float properties round-trip through 32-bit storage and
  won't equal 64-bit double literals exactly.

## Documentation & Specs

- F1 in-editor docs live in `addons/godot_gameinput/doc_classes/`. Wire new
  classes through `target_doc_sources` in the CMakeLists.
- `docs/gameinput/plugin.md` is the user-facing reference. Update it when
  public API or sample workflow changes.
- `spec/gdext-gameinput.md` is the source of truth for design decisions and
  deferred work. Mark sections shipped or note deviations there when
  scope changes.

## Keeping Surfaces in Sync

A public API change is not done until all of these agree:

1. The binding in `_bind_methods()` and its doc XML entry (doc XML is what
   users read; check examples, key names and limits against the code).
2. The C# facade in `addons/godot_gameinput_csharp/` — a wrapper for every
   method, property and signal, and an enum member with the same value for
   every constant. `tools/run_csharp_tests.ps1` (FacadeParity.Tests) fails on
   a missing wrapper or a mismatched value, and the C# sample's `api.facade`
   self-test check repeats the value check at runtime.
3. A GUT suite under `tests/godot/gameinput/tests/`, using the mock backend
   when the behaviour needs a device.
4. The sample self-tests and inspector, where the feature is user-visible.
5. `docs/gameinput/plugin.md`, `docs/gameinput/csharp.md`,
   `docs/gameinput/manual-tests.md` (for anything a mock can't prove) and this
   spec.

## Test Seams

- Test-only methods are named `_test_*` and are bound under `#ifndef NDEBUG`
  in `_bind_methods()`, so the release DLL does not contain them. They are
  not documented in doc XML, so the C# parity test never sees them.
- The mock backend replaces the native runtime with scripted devices but
  feeds the same queues, snapshots, signals and timers. A seam must never
  bypass the drain in `poll()`, or the tests stop covering the real path.
- GUT suites start and end mock sessions with `begin_mock_session()` /
  `end_mock_session()` from `gameinput_test_base.gd`; against a release DLL
  `begin_mock_session()` marks the test pending instead of failing it.
