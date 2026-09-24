# Tutorial GameInput - standalone sample

This Godot 4.x project is the **end-state** of the
[GameInput action-bridge tutorial](../../docs/tutorials/gameinput-action-bridge.md),
the standalone track of the tutorial series. It does **not** depend
on XBOX sign-in or PlayFab — bring a wired or wireless XBOX
controller and you're ready to run.

It is also the `godot_gameinput` addon's integration harness: run it
with `--gameinput-selftest` and it checks the addon end to end, headless,
and exits with a status code. See [Integration self-test](#integration-self-test).

## What's in the window

* **Left: the tutorial.** Runtime status, the gamepad count, the device
  list, the hot-plug log, live action strengths, and a player that A
  jumps and the left stick moves. Each jump rumbles the pad (tutorial
  Step 7).
* **Right: the Device inspector.** Every GameInput device — gamepads,
  keyboards, mice, arcade sticks, flight sticks, racing wheels, sensor
  devices — with the selected device's family, ids, status, input kinds,
  rumble motors, system buttons, force-feedback motors, haptics and
  button label, and its live reading. Buttons rumble the device, pulse
  its impulse triggers and play a force-feedback pulse; toggles turn on
  event-driven readings (with a readings-per-second and dropped counter)
  and background input. Device events — connects, status changes, Guide
  and Share, keyboard layout changes — are logged below.

## Quick start

1. **Build the addon.** From the repo root:

   ```powershell
   cmake --preset default
   cmake --build build --preset debug
   ```

2. **Plug in a GameInput-supported gamepad.** XBOX controllers
   over USB or Bluetooth are the common case; any GameInput-
   compatible device works.

3. **Open the project in Godot and run `main.tscn`.** The
   `GameInputBootstrap` autoload (installed by the addon's editor
   plugin) initializes `GameInput` on `_ready` and pumps `poll()`
   every process frame — your action map starts firing as soon as
   the mapper picks up the device.

## Integration self-test

From the repository root, after building the addon:

```powershell
pwsh -NoProfile -File .\tools\run_gameinput_selftest.ps1
```

The runner finds Godot (`-Godot`, then `GODOT_CONSOLE` / `GODOT_BIN` /
`GODOT`), imports the project if the addon is not registered in it yet,
runs the self-test headless and exits with its exit code, once the report
agrees with it (see [Exit codes](#exit-codes)). The report and
Godot's output land in `build\selftest\gameinput\`. To run it without the
runner, import the project once, then:

```powershell
godot --headless --path sample/tutorial_gameinput -- --gameinput-selftest
```

The first headless import of a project that loads a GDExtension can crash
while the editor shuts down; the runner imports twice and only needs the
second to succeed.

### Checks

The checks run in order. The `sample` and `mock` groups shut the real
runtime down, drive the addon's debug-only mock backend with scripted
devices, and restore the real runtime at the end.

| Check | What it verifies |
| --- | --- |
| `api.singleton` | The `GameInput` singleton and the `GameInputBootstrap` autoload exist. |
| `api.classes` | All seven addon classes are registered. |
| `api.methods` | Every documented method is bound. |
| `api.signals` | All six signals exist. |
| `api.constants` | Key constants keep their documented values. |
| `api.settings` | The addon's Project Settings are registered, and the sample sets `initialize_on_startup`. |
| `api.static_helpers` | Scan-code, virtual-key, switch and source conversions. |
| `runtime.initialized` | The bootstrap initialized the real GameInput runtime. |
| `runtime.timestamp` | The GameInput clock advances. |
| `runtime.devices` | Every device has a unique id, input kinds, `STATUS_CONNECTED`, the core `get_device_info()` keys and a 64-character app-local id; the primary gamepad is the first one listed. |
| `runtime.readings` | Each device's reading carries its id, only kinds the device supports, and a timestamp that is not in the future. Skips when no device returned a reading. |
| `runtime.reading_callbacks` | Event-driven readings can be registered and restored. |
| `runtime.focus_policy` | The focus policy round-trips. |
| `runtime.aggregate_device` | An aggregate gamepad can be created, disabled and re-enabled under the same id. With a gamepad connected it must surface, named, and stay listed while disabled; with none it has no member, cannot surface, and the check skips. |
| `runtime.vibration` | A timed rumble starts and stops by itself on every device with rumble motors. |
| `runtime.force_feedback` | Force-feedback motors describe themselves. |
| `runtime.haptics` | Haptic devices describe themselves. |
| `vpad.present` | The ViGEm virtual pad enumerates (`-VirtualPad` only). |
| `vpad.info` | It reports the Xbox 360 family and both rumble motors. |
| `vpad.input` | Its A button and left stick arrive through polling, `reading_received` and the mapper. |
| `vpad.rumble` | A timed rumble reaches the virtual motors and stops on time, checked against the driver's log. |
| `vpad.aggregate_rumble` | An aggregate gamepad surfaces over the virtual pad, and rumble sent to it reaches the virtual pad's motors and stops on time. |
| `mock.session` | The mock backend takes over. |
| `sample.hotplug_ui` | A scripted pad reaches `device_connected`, the device list, the count and the hot-plug log on the next frame. |
| `sample.action_bridge` | A presses `jump` and `ui_accept`; the stick drives `move_left` / `move_right` with deadzone rescaling. |
| `sample.player_jump` | The player jumps with the Step 7 rumble (motors, duration, auto-stop) and moves. |
| `sample.inspector` | The inspector lists the pad and renders its live state, and its Rumble, Triggers and Stop buttons drive the pad's motors. |
| `sample.disconnect_releases` | Unplugging the pad mid-press releases the action and updates the UI. |
| `mock.device_kinds` | Keyboard, mouse, sensor, arcade stick, flight stick, racing wheel and raw controller readings. |
| `mock.event_readings` | A tap shorter than a frame produces press and release events and two buffered readings. |
| `mock.system_events` | Guide/Share, keyboard layout and status signals carry current and previous values. |
| `mock.vibration` | Weak/strong motor mapping, impulse triggers and timed auto-stop. |
| `mock.force_feedback` | An effect is created, started, re-gained, stopped and released. |
| `mock.restore` | The real runtime is back, with its devices re-enumerated. |

A check that cannot run on this machine is **skipped** with the reason —
for example `runtime.vibration` with no rumble-capable device connected,
`runtime.aggregate_device` with no gamepad connected, the `vpad` group
without `-VirtualPad`, or `vpad.input` in a locked or Remote Desktop
session, where GameInput delivers no input. A check never passes on nothing:
`runtime.readings` skips when no device returned a reading. Skips do not
fail the run unless you pass `-Strict`.

### Options

Runner parameters, with the self-test flag each one passes after `--`:

| Runner | Self-test flag | Effect |
| --- | --- | --- |
| `-Strict` | `--gameinput-strict` | A skipped check fails the run. |
| `-VirtualPad` | `--gameinput-virtual-pad`, `--gameinput-vpad-log=<path>` | Starts the virtual pad and runs the `vpad` group. |
| `-TimeoutSec <n>` | `--gameinput-timeout=<n>` | Watchdog; default 120 s. |
| `-OutDir <dir>` | `--gameinput-report=<dir>\gameinput-selftest.json` | Where the report and logs go. |
| (automatic) | `--gameinput-session-locked` | Passed when the Windows session is locked. |
| `-Project <dir>` | | The project to run; `sample\tutorial_gameinput_csharp` runs the [C# self-test](../tutorial_gameinput_csharp/README.md). |
| `-Godot <exe>` | | The Godot console executable. |

Without `--gameinput-report` the report goes to
`user://gameinput-selftest.json`.

### Exit codes

| Code | Meaning |
| --- | --- |
| `0` | Every check passed (skips allowed unless strict). |
| `1` | A check failed, or was skipped under strict. |
| `2` | The harness could not run or its result cannot be trusted: an unknown `--gameinput-*` flag or an unwritable report; from the runner, also no Godot, the addon not built, a failed build, import or vpad driver, anything that threw, or Godot exiting without a report or with a code its report disagrees with (a crash on the way out). |
| `3` | The watchdog expired before the checks finished; from the runner, also Godot still running 60 s after the watchdog, which the runner then kills. |

### Report

`gameinput-selftest.json` (schema `gameinput-selftest/1`) records the Godot
version, OS, the Windows session (name, remote, locked), the display, the
build (debug, mock seams), the options, a summary (`pass`, `fail`, `skip`,
`total`, `exit_code`) and one entry per check with its `id`, `group`,
`status`, `detail`, `duration_ms` and check-specific `data`, such as the
rumble values the virtual pad received.

## Virtual gamepad

`-VirtualPad` plugs in a virtual Xbox 360 pad through
[ViGEmBus](https://github.com/nefarius/ViGEmBus) and the
[vgamepad](https://pypi.org/project/vgamepad/) Python package, so the
`vpad` checks can test the real GameInput runtime without a controller.
Prerequisites:

1. Python 3 and `pip install vgamepad` (it offers the ViGEmBus installer on
   first install), or a vgamepad checkout passed with `-VgamepadPath`.
2. The ViGEmBus driver installed and running.

The driver, [`tools/virtual_gamepad/vpad_driver.py`](../../tools/virtual_gamepad/vpad_driver.py),
presses A and pushes the left stick right once a second and logs every
rumble command it receives. Run it by hand to watch the pad in the
inspector:

```powershell
python tools/virtual_gamepad/vpad_driver.py --log vpad.jsonl --duration 60
```

## Producing a packaged build

The sample includes a committed `export_presets.cfg` with a `Windows Desktop`
preset, so a clean clone can open **Project → Export** and edit or run the
export without first authoring a preset.

Install the Godot 4.6.1 export templates under
`%APPDATA%\Godot\export_templates\` before exporting.

Release exports use the addon's release DLL, which compiles out the mock
backend; the self-test then skips its `sample` and `mock` groups.

## See also

- [Tutorial — GameInput action bridge](../../docs/tutorials/gameinput-action-bridge.md)
- [GameInput plugin reference](../../docs/gameinput/plugin.md)
- [GameInput manual hardware checklist](../../docs/gameinput/manual-tests.md)
- C# version: [`sample/tutorial_gameinput_csharp/`](../tutorial_gameinput_csharp/README.md)
- GDK-only sample: [`sample/tutorial_gdk/`](../tutorial_gdk/README.md)
- PlayFab-only sample: [`sample/tutorial_playfab/`](../tutorial_playfab/README.md)
- Integrated GDK + PlayFab sample: [`sample/tutorial_integrated/`](../tutorial_integrated/README.md)
- Full game reference (external): [`sample/tutorial_netrumble/`](../tutorial_netrumble/README.md)
