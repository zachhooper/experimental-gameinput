"""Virtual Xbox 360 pad for the GameInput self-test.

Plugs in a ViGEm Xbox 360 target through vgamepad, cycles the A button and
the left stick so the self-test sees live input, and logs every rumble
notification ViGEm hands back as a JSON line, so the self-test can check
GameInputDevice.start_vibration() all the way to the (virtual) motors.

tools/run_gameinput_selftest.ps1 -VirtualPad starts and stops this script;
run it by hand to watch the pad in the tutorial sample's inspector:

    python tools/virtual_gamepad/vpad_driver.py --log vpad.jsonl --duration 60

Requirements: Windows, the ViGEmBus driver, and `pip install vgamepad`
(the vgamepad wheel bundles the ViGEmClient DLL and offers the ViGEmBus
installer on first install).

Log format: one JSON object per line, schema "vpad-log/1", with "event" set
to "start", "ready" (the pad is plugged in), "input" (the state the driver
just sent), "rumble" (a notification from the game: large and small motor,
0-255), "error" or "stop". Every line carries "t", seconds since the Unix
epoch, so readers can line it up with their own wall clock.

Choreography, repeated every second while running: A held for 0.3 s, then
the left stick pushed fully right from 0.4 s to 0.7 s, otherwise at rest.

Exit codes: 0 stopped cleanly, 2 vgamepad could not be imported, 3 the
virtual pad could not be created (ViGEmBus missing or not running).
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import threading
import time
from pathlib import Path

SCHEMA = "vpad-log/1"
# vgamepad's Xbox 360 target enumerates as a wired Xbox 360 controller.
VENDOR_ID = 0x045E
PRODUCT_ID = 0x028E
CYCLE_SEC = 1.0
A_UNTIL = 0.3
STICK_FROM = 0.4
STICK_UNTIL = 0.7
TICK_SEC = 0.005


class JsonLog:
    """Appends JSON lines from the main thread and ViGEm's callback thread."""

    def __init__(self, path: Path) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        self._file = path.open("a", encoding="utf-8", newline="\n")
        self._lock = threading.Lock()

    def write(self, event: str, **fields: object) -> None:
        entry = {"schema": SCHEMA, "event": event, "t": time.time(), **fields}
        line = json.dumps(entry, separators=(",", ":"))
        with self._lock:
            if self._file.closed:
                return
            self._file.write(line + "\n")
            self._file.flush()

    def close(self) -> None:
        with self._lock:
            self._file.close()


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--log", required=True, type=Path, help="JSON-lines log to append to")
    parser.add_argument(
        "--duration",
        type=float,
        default=300.0,
        help="stop after this many seconds (default 300, a safety net for the runner)",
    )
    parser.add_argument(
        "--stop-file",
        type=Path,
        default=None,
        help="stop as soon as this file exists",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    log = JsonLog(args.log)
    log.write("start", pid=os.getpid(), python=sys.version.split()[0])
    try:
        return run(args, log)
    finally:
        log.close()


def run(args: argparse.Namespace, log: JsonLog) -> int:
    try:
        import vgamepad as vg
    except Exception as exc:  # ImportError, or the bundled ViGEmClient DLL failing to load
        log.write("error", stage="import", message=str(exc))
        print(f"vpad_driver: cannot import vgamepad ({exc}); pip install vgamepad", file=sys.stderr)
        return 2

    try:
        pad = vg.VX360Gamepad()
    except Exception as exc:
        log.write("error", stage="create", message=str(exc))
        print(f"vpad_driver: cannot create the virtual pad ({exc}); is ViGEmBus installed?", file=sys.stderr)
        return 3

    def on_rumble(client, target, large_motor, small_motor, led_number, user_data):
        log.write("rumble", large=int(large_motor), small=int(small_motor), led=int(led_number))

    pad.register_notification(callback_function=on_rumble)
    log.write("ready", vendor_id=VENDOR_ID, product_id=PRODUCT_ID)
    print("vpad_driver: virtual Xbox 360 pad plugged in", flush=True)

    reason = "duration"
    started = time.monotonic()
    sent = None
    try:
        while time.monotonic() - started < args.duration:
            if args.stop_file is not None and args.stop_file.exists():
                reason = "stop-file"
                break
            phase = (time.monotonic() - started) % CYCLE_SEC
            state = (phase < A_UNTIL, 1.0 if STICK_FROM <= phase < STICK_UNTIL else 0.0)
            if state != sent:
                a_down, left_x = state
                if a_down:
                    pad.press_button(button=vg.XUSB_BUTTON.XUSB_GAMEPAD_A)
                else:
                    pad.release_button(button=vg.XUSB_BUTTON.XUSB_GAMEPAD_A)
                pad.left_joystick_float(x_value_float=left_x, y_value_float=0.0)
                pad.update()
                log.write("input", a=a_down, left_x=left_x)
                sent = state
            time.sleep(TICK_SEC)
    except KeyboardInterrupt:
        reason = "interrupted"
    finally:
        pad.reset()
        pad.update()
        pad.unregister_notification()
        log.write("stop", reason=reason)
        del pad
    print(f"vpad_driver: stopped ({reason})", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
