# GameInput Manual Hardware Test Checklist

The headless test suite under `tests/godot/gameinput/tests/` and the sample
self-test cover everything that can be verified without a person at a real
controller. This document covers the pieces that need a human + hardware in
the loop.

> Recommended manual-test host: `sample/tutorial_gameinput/`. It is the
> standalone GameInput sample: the left half is the tutorial (hot-plug log,
> gamepad count, action strengths, a jumping player that rumbles the pad) and
> the right half is the **Device inspector**, which lists every GameInput
> device, shows the selected device's capabilities and live reading, and has
> buttons for rumble, impulse triggers and force feedback. If you prefer a
> minimal custom host, wire one up with
> [Tutorial — GameInput action bridge](../tutorials/gameinput-action-bridge.md).

Run the checks in a **local, unlocked** Windows session. GameInput delivers
no input to a locked session or a Remote Desktop session — the self-test
reports those runs as skipped — though rumble output still works there.

## Setup

1. Build the addon:

    ```powershell
    cmake --preset default
    cmake --build build --preset debug
    ```

2. Run the automated checks first; they should pass before you start:

    ```powershell
    pwsh -NoProfile -File .\tools\run_gameinput_selftest.ps1
    ```

   With the controllers you are about to test plugged in, the `runtime.*`
   checks exercise them: `runtime.vibration`, `runtime.force_feedback` and
   `runtime.haptics` pass instead of skipping when a connected device has
   rumble motors, force-feedback motors or haptics.

3. Open `sample/tutorial_gameinput/` in the Godot editor and run it (F5).

## Per-feature checklist

### Initialize / shutdown lifecycle

- [ ] The status line reads `GameInput runtime initialized.`
- [ ] Closing the window exits cleanly: no crash dialog, and no `ERROR:`
  lines mentioning `gameinput` in the output.
- [ ] Close the window while a pad is connected and the inspector's
  **Event-driven readings** toggle is on: the process still exits cleanly.

### Device discovery (one controller)

Plug in any GameInput-compatible gamepad (XBOX Series, XBOX One, Elite,
Razer Wolverine, etc.) **before** starting the sample.

- [ ] The hot-plug log starts with `Seeded with 1 gamepad(s) at startup`
  and the count reads `Connected gamepads: 1`.
- [ ] The inspector lists the pad with a sensible name. Keyboards and mice
  are listed too; they do not change the gamepad count.
- [ ] Selecting the pad shows its family, `VID:PID`, status, input kinds,
  rumble motors and system buttons, and `A button label: xbox_a` for an
  Xbox pad.

### Hot-plug (connect mid-frame)

Start with no controller plugged in, then plug one in.

- [ ] The hot-plug log shows `connected: id=<id> (<Name>)` and the count
  goes to `Connected gamepads: 1`.
- [ ] The inspector's device events log shows `connected: <Name> (id <id>)`
  and the list updates.

### Hot-plug (disconnect mid-frame)

With a controller connected, unplug it.

- [ ] The count drops by 1 and the hot-plug log shows
  `disconnected: id=<id>`.
- [ ] Re-plugging assigns a **new** device id (ids are session-local
  monotonic; never recycled).

### Gamepad reading and the action bridge

- [ ] The inspector's live reading follows the sticks and triggers: stick
  **down** reads positive Y, like Godot's joypad axes; triggers read 0 to 1.
- [ ] Held buttons appear in `buttons:`; on an Elite pad the paddles appear
  as `PADDLE_*`.
- [ ] A jumps and the left stick moves the player; the action strengths
  follow the stick.
- [ ] Each jump gives a short, light rumble (weak 0.2, strong 0.4 for
  0.12 s).

### Vibration

With a vibration-capable controller selected in the inspector:

- [ ] **Rumble 0.5 s** vibrates both motors for about half a second and stops
  on its own.
- [ ] **Stop** ends a rumble immediately.
- [ ] **Triggers 0.5 s** pulses the impulse triggers on XBOX One and Series
  pads, and does nothing on pads without trigger motors.
- [ ] With two pads connected, the buttons rumble only the selected pad.
- [ ] Disconnect the selected pad during a rumble: nothing is left running
  when it is plugged back in.

### Event-driven readings

- [ ] Turn on **Event-driven readings**. `events/s` rises while a stick
  moves and drops back when the pad is left alone; `dropped` stays at 0.
- [ ] Turn it off: `events/s` falls to 0.

### Background input and the focus policy

- [ ] With **Background input** off, click another window: the live reading
  stops following the pad.
- [ ] Turn **Background input** on and click another window: the live
  reading keeps following the pad.

### Guide and Share buttons

- [ ] Press Guide (and Share on a Series pad) with the sample focused. Note
  whether `system buttons:` lines appear in the device events log; on
  Windows the Game Bar usually takes Guide. Record the result, with and
  without background input, in the PR.

### Keyboard

Select the keyboard in the inspector.

- [ ] `keys:` lists held keys by position (`W` for the key left of E, on any
  layout), including extended keys: arrows, right Ctrl, right Alt, numpad
  Enter, Insert/Delete/Home/End.
- [ ] Switch the keyboard layout (Win+Space). The device events log shows
  `keyboard layout: <Name> 0x… -> 0x…`, and `keys:` still reports keys by
  position.

### Mouse

Select the mouse in the inspector.

- [ ] `mouse:` shows the held buttons (including the side buttons), `delta`
  follows movement and `wheel` follows the wheel.

### Arcade stick, flight stick, racing wheel, motion sensors

For each device you have:

- [ ] **Arcade stick:** `arcade:` lists the held buttons and stick
  directions.
- [ ] **Flight stick:** `flight:` shows the held buttons and hat; roll,
  pitch and yaw are signed and centered at 0; throttle covers its full
  range. Record the direction each sign means.
- [ ] **Racing wheel:** `wheel:` goes negative one way and positive the
  other; throttle, brake, clutch and handbrake cover 0 to 1; `gear` follows
  the shifter. Record the direction each sign means.
- [ ] **Motion sensors:** accelerometer (m/s², about 9.8 at rest),
  gyroscope (rad/s, about 0 at rest) and heading follow the device.

### Force feedback

With a force-feedback wheel or flight stick selected:

- [ ] The info line shows at least one force-feedback motor.
- [ ] **Force feedback pulse** gives a 0.3-strength constant force for
  0.3 s, then stops.
- [ ] **Stop** during the pulse ends it immediately.

### Haptics

With a controller that reports haptics:

- [ ] Once the device reports `STATUS_HAPTIC_INFO_READY`, the info line
  lists its haptic locations after `haptics` (for example
  `grip_left, grip_right`) instead of `none`.

### Aggregate devices

With two pads connected, call
`GameInput.create_aggregate_device(GameInput.DEVICE_GAMEPAD)` from a script
— a temporary line in `main.gd`'s `_ready()` is enough:

- [ ] A device named `GameInput Aggregate Device` with family `AGGREGATE`
  appears in the inspector, and its live reading follows both pads.
- [ ] Rumble sent to the aggregate reaches both pads. (The self-test's
  `vpad.aggregate_rumble` check covers one virtual member pad; two real
  pads have not been tried.)

### Mapper — `Input.is_action_pressed` integration

The Mapper is exercised by any project that wires a `GameInputMapper` node
to a `GameInputActionMap`. For a focused unit, drop a `GameInputMapper`
node into a scene and assign a small `GameInputActionMap` with one binding.

- [ ] With a project that maps `move_up` / `move_down` to controller axes,
  the actions drive your gameplay. (This works through Godot's standard
  joypad mapping; the Mapper layer adds GameInput devices that aren't
  recognized by Godot's built-in joypad enum.)
- [ ] When you create a custom `GameInputBinding` that targets an action
  **not** present in `InputMap`, the Mapper logs **one** warning per missing
  action (not per frame).
- [ ] Hold a mapped action, then hot-swap the mapper's `action_map`, call
  `set_bindings()` / `add_binding()` / `clear()` on the active map, and remove
  the mapper from the tree. After each stop-driving path,
  `Input.is_action_pressed(action)` returns `false`.
- [ ] Hold a mapped action and unplug the target controller. The action is
  released on the next frame and does not remain stuck in Godot's `InputMap`.
- [ ] Bind `ui_accept` to an Elite paddle (`SRC_BTN_PADDLE_RIGHT_1`) and map
  `JOY_BUTTON_PADDLE1` to `ui_accept` in the `InputMap`: one paddle press
  presses a focused button once, not twice.
- [ ] Bind `ui_down` to `SRC_BTN_LEFT_STICK_DOWN` and keep Godot's default
  `ui_down` events (they include the left stick pushed down): in a column of
  buttons, one flick of the stick moves focus one step, not two.
- [ ] With `target_kind_mask = KIND_RACING_WHEEL`, a binding to
  `SRC_AXIS_WHEEL` drives its action from the wheel.

### Virtual gamepad in a local session

The self-test's `vpad.input` check skips in locked and Remote Desktop
sessions. In a local, unlocked session with ViGEmBus installed:

- [ ] `pwsh -NoProfile -File .\tools\run_gameinput_selftest.ps1 -VirtualPad`
  passes, with `vpad.input` reported as `pass` rather than `skip`.

## Regression smoke

After any change to the GameInput addon C++:

- [ ] Headless tests still pass:
  ```powershell
  pwsh -NoLogo -NoProfile -ExecutionPolicy Bypass -File .\tools\run_all_tests.ps1 -Hosts @('tests\godot\gameinput')
  ```
  Expected output ends with `Overall: pass`.
- [ ] The sample self-test still passes:
  ```powershell
  pwsh -NoProfile -File .\tools\run_gameinput_selftest.ps1
  pwsh -NoProfile -File .\tools\run_gameinput_selftest.ps1 -Project sample\tutorial_gameinput_csharp
  ```
- [ ] Any GameInput-using Godot project (your own or `sample/tutorial_gameinput/`)
  launches without `ERROR:` lines mentioning `gameinput` in editor output.
- [ ] `GameInput` singleton appears in **Project → Project Settings →
  Globals** (or wherever Godot lists engine singletons in your version).

If a row above fails, capture the editor output and the failing scenario
notes in the related issue or PR.
