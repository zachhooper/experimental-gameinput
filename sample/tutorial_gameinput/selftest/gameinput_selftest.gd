extends Node

## GameInput integration self-test for the tutorial sample.
##
## main.gd adds this node when the command line carries `--gameinput-selftest`
## after Godot's `--` separator:
##
##     godot --headless --path sample/tutorial_gameinput -- --gameinput-selftest
##
## It drives the addon through the same singleton, GameInputMapper and signal
## handlers the tutorial uses, prints one line per check, writes a JSON report
## and quits with:
##
##     0  every check passed (skips allowed unless --gameinput-strict)
##     1  a check failed, or was skipped under --gameinput-strict
##     2  the harness could not run (unknown --gameinput-* flag, report not writable)
##     3  the watchdog expired before the checks finished
##
## Options (all after `--`):
##     --gameinput-report=<path>     JSON report path (default user://gameinput-selftest.json)
##     --gameinput-strict            treat skipped checks as failures
##     --gameinput-virtual-pad       expect a ViGEm Xbox 360 pad (tools/virtual_gamepad)
##     --gameinput-vpad-log=<path>   vpad driver log, for the rumble round trip
##     --gameinput-session-locked    the runner saw a locked session (no input expected)
##     --gameinput-timeout=<sec>     watchdog, default 120
##
## Check groups run in order: api, runtime (the real GameInput runtime), vpad
## (only with --gameinput-virtual-pad), then sample and mock. The last two swap
## the runtime for the addon's debug-only mock backend, drive the tutorial's own
## wiring (hot-plug log, action bridge, jumping player, inspector) and every v2
## feature with scripted devices, then restore the real runtime.
##
## Parse-safe: native classes are only reached through addon_api.gd and
## ClassDB, so the parse gate can load this file before the addon is built.

const AddonApi = preload("res://addon_api.gd")

const SCHEMA := "gameinput-selftest/1"
const FLAG_RUN := "--gameinput-selftest"
const DEFAULT_REPORT_PATH := "user://gameinput-selftest.json"
const DEFAULT_TIMEOUT_SEC := 120.0

const EXIT_PASS := 0
const EXIT_FAIL := 1
const EXIT_HARNESS_ERROR := 2
const EXIT_WATCHDOG := 3

# vgamepad's Xbox 360 target enumerates as a wired Xbox 360 controller.
const VPAD_VENDOR_ID := 0x045E
const VPAD_PRODUCT_ID := 0x028E
const VPAD_WAIT_SEC := 10.0
const VPAD_INPUT_WAIT_SEC := 8.0
# start_vibration(weak, strong): strong drives the low-frequency (large) motor
# and weak the high-frequency (small) one. XUSB reports each motor as 0-255.
const VPAD_RUMBLE_WEAK := 0.25
const VPAD_RUMBLE_STRONG := 0.75
const VPAD_RUMBLE_SEC := 0.3
const VPAD_EXPECT_LARGE := 191
const VPAD_EXPECT_SMALL := 64
const VPAD_RUMBLE_TOLERANCE := 8
const VPAD_RUMBLE_WAIT_SEC := 3.0

const CLASSES := [
	"GameInput", "GameInputDevice", "GameInputReading", "GameInputForceFeedbackEffect",
	"GameInputMapper", "GameInputActionMap", "GameInputBinding",
]

# Every method in doc_classes/*.xml. A build that is missing one is not the v2 addon.
const METHODS := {
	"GameInput": [
		"initialize", "shutdown", "is_initialized", "poll", "get_devices", "get_primary_device",
		"get_current_reading", "set_vibration", "stop_haptics", "get_connected_device_count",
		"get_device_by_id", "get_current_timestamp", "set_reading_callback_kinds",
		"get_reading_callback_kinds", "get_buffered_readings", "get_dropped_reading_count",
		"set_focus_policy", "get_focus_policy", "create_aggregate_device",
		"disable_aggregate_device",
	],
	"GameInputDevice": [
		"get_device_id", "get_display_name", "get_kind_mask", "is_connected", "supports_vibration",
		"supports_haptics", "get_device_info", "button_to_source", "axis_to_source", "get_status",
		"get_app_local_id", "get_device_family", "get_supported_rumble_motors",
		"get_supported_system_buttons", "get_system_buttons", "get_keyboard_layout",
		"get_button_label", "get_haptic_info", "start_vibration", "stop_vibration", "is_vibrating",
		"get_vibration_strength", "get_trigger_vibration_strength", "get_vibration_duration",
		"get_vibration_remaining_duration", "get_force_feedback_motor_count",
		"get_force_feedback_motor_info", "is_force_feedback_motor_powered_on",
		"set_force_feedback_motor_gain", "create_force_feedback_effect", "scan_code_to_physical_key",
		"virtual_key_to_keycode", "switch_position_to_vector",
	],
	"GameInputReading": [
		"is_button_down", "was_button_pressed", "was_button_released", "get_axis",
		"get_buttons_mask", "get_timestamp", "get_device_id", "get_kind_timestamp",
		"get_input_kinds", "has_previous", "has_gap_before", "is_truncated", "is_source_down",
		"was_source_pressed", "was_source_released", "get_source_value", "get_key_count",
		"get_pressed_physical_keys", "is_physical_key_down", "was_physical_key_pressed",
		"was_physical_key_released", "get_key_states", "get_mouse_buttons", "is_mouse_button_down",
		"was_mouse_button_pressed", "was_mouse_button_released", "get_mouse_delta",
		"get_mouse_wheel_delta", "has_mouse_absolute_position", "get_mouse_absolute_position",
		"get_mouse_state", "get_sensor_kinds", "get_accelerometer", "get_gyroscope",
		"get_heading_degrees", "get_heading_accuracy", "get_orientation", "get_arcade_stick_buttons",
		"get_flight_stick_buttons", "get_flight_stick_hat", "get_flight_stick_hat_vector",
		"get_racing_wheel_buttons", "get_racing_wheel_gear", "get_controller_axes",
		"get_controller_buttons", "get_controller_switches", "get_controller_axis",
		"is_controller_button_down", "was_controller_button_pressed",
		"was_controller_button_released", "get_controller_switch",
	],
	"GameInputForceFeedbackEffect": [
		"is_valid", "get_device_id", "get_motor_index", "get_kind", "start", "stop", "pause",
		"get_state", "set_state", "get_gain", "set_gain", "get_params", "set_params", "release",
	],
	"GameInputMapper": [
		"get_action_map", "set_action_map", "get_target_kind_mask", "set_target_kind_mask",
		"get_target_device_id", "set_target_device_id", "get_active_binding_count",
	],
	"GameInputActionMap": [
		"get_bindings", "set_bindings", "get_binding_count", "get_binding", "add_binding", "clear",
	],
	"GameInputBinding": [
		"get_action", "set_action", "get_source", "set_source", "get_is_axis", "set_is_axis",
		"get_axis_threshold", "set_axis_threshold", "get_axis_invert", "set_axis_invert",
		"get_deadzone", "set_deadzone",
	],
}

const SIGNALS := [
	"device_connected", "device_disconnected", "device_status_changed", "reading_received",
	"system_buttons_changed", "keyboard_layout_changed",
]

# Values games persist or compare against, so they must never move.
const CONSTANTS := [
	["GameInput", "DEVICE_GAMEPAD", 1], ["GameInput", "DEVICE_ALL", 7],
	["GameInput", "DEVICE_ARCADE_STICK", 8], ["GameInput", "DEVICE_FLIGHT_STICK", 16],
	["GameInput", "DEVICE_RACING_WHEEL", 32], ["GameInput", "DEVICE_SENSORS", 64],
	["GameInput", "DEVICE_CONTROLLER", 128], ["GameInput", "DEVICE_ANY", 255],
	["GameInput", "FOCUS_POLICY_DEFAULT", 0], ["GameInput", "FOCUS_POLICY_ENABLE_BACKGROUND_INPUT", 64],
	["GameInputDevice", "BUTTON_A", 4], ["GameInputDevice", "BUTTON_PADDLE_RIGHT_2", 1 << 29],
	["GameInputDevice", "SRC_BTN_A", 2], ["GameInputDevice", "SRC_BTN_PADDLE_RIGHT_2", 29],
	["GameInputDevice", "SRC_AXIS_LEFT_X", 100], ["GameInputDevice", "SRC_AXIS_FLIGHT_THROTTLE", 114],
	["GameInputDevice", "SRC_ARCADE_MENU", 200], ["GameInputDevice", "SRC_FLIGHT_MENU", 300],
	["GameInputDevice", "SRC_WHEEL_MENU", 400], ["GameInputDevice", "AXIS_FLIGHT_THROTTLE", 14],
	["GameInputDevice", "FAMILY_VIRTUAL", -1], ["GameInputDevice", "FAMILY_AGGREGATE", 5],
	["GameInputDevice", "STATUS_CONNECTED", 1], ["GameInputDevice", "RUMBLE_LOW_FREQUENCY", 1],
	["GameInputDevice", "RUMBLE_HIGH_FREQUENCY", 2], ["GameInputDevice", "SWITCH_UP", 1],
	["GameInputDevice", "SYSTEM_BUTTON_GUIDE", 1],
	["GameInputForceFeedbackEffect", "EFFECT_CONSTANT", 0],
	["GameInputForceFeedbackEffect", "STATE_RUNNING", 1],
	["GameInputMapper", "KIND_GAMEPAD", 1], ["GameInputMapper", "KIND_RACING_WHEEL", 32],
]

const SETTINGS := [
	"game_input/runtime/initialize_on_startup", "game_input/runtime/auto_poll",
	"game_input/runtime/singleton_name", "game_input/runtime/reading_callback_kinds",
	"game_input/runtime/focus_policy", "game_input/mapper/default_action_map",
]

const KIND_NAMES := {
	1: "gamepad", 2: "keyboard", 4: "mouse", 8: "arcade_stick", 16: "flight_stick",
	32: "racing_wheel", 64: "sensors", 128: "controller",
}

## Set by main.gd before the node enters the tree.
var sample: Node = null

var _options := {
	"report": DEFAULT_REPORT_PATH,
	"strict": false,
	"virtual_pad": false,
	"vpad_log": "",
	"session_locked": false,
	"timeout_sec": DEFAULT_TIMEOUT_SEC,
}
var _results: Array = []
var _cur: Dictionary = {}
var _started_ms: int = 0
var _started_utc: String = ""
var _finished: bool = false
var _gi: Object = null
var _has_seams: bool = false
var _runtime_ready: bool = false
var _native_device_count: int = -1
var _vpad = null
var _mock_ready: bool = false
var _mock_saved: Dictionary = {}
var _mock_pad_id: int = -1
var _events: Array = []
var _const_cache: Dictionary = {}
var _display: Dictionary = {}


static func is_requested() -> bool:
	return OS.get_cmdline_user_args().has(FLAG_RUN)


func _ready() -> void:
	_started_ms = Time.get_ticks_msec()
	_started_utc = _utc_now()
	_gi = AddonApi.singleton("GameInput")
	if _gi != null and not _gi.is_class("GameInput"):
		_gi = null
	_has_seams = _gi != null and _gi.has_method("_test_initialize_mock")
	_connect_signals()
	_run.call_deferred()


func _exit_tree() -> void:
	_disconnect_signals()


# ── Orchestration ────────────────────────────────────────────────────────

func _run() -> void:
	var arg_error := _parse_options()
	_arm_watchdog()
	print("[selftest] GameInput self-test: Godot %s, %s build, mock seams %s, options %s" % [
		Engine.get_version_info().get("string", "?"), "debug" if OS.is_debug_build() else "release",
		"available" if _has_seams else "unavailable", JSON.stringify(_options)])
	if arg_error != "":
		_finish(EXIT_HARNESS_ERROR, arg_error)
		return
	# Give the bootstrap autoload a few polls to deliver the startup device list.
	await _frames(10)
	_size_headless_window()

	await _check("api.singleton", _check_api_singleton)
	await _check("api.classes", _check_api_classes)
	await _check("api.methods", _check_api_methods)
	await _check("api.signals", _check_api_signals)
	await _check("api.constants", _check_api_constants)
	await _check("api.settings", _check_api_settings)
	await _check("api.static_helpers", _check_api_static_helpers)

	await _check("runtime.initialized", _check_runtime_initialized)
	await _check("runtime.timestamp", _check_runtime_timestamp)
	await _check("runtime.devices", _check_runtime_devices)
	await _check("runtime.readings", _check_runtime_readings)
	await _check("runtime.reading_callbacks", _check_runtime_reading_callbacks)
	await _check("runtime.focus_policy", _check_runtime_focus_policy)
	await _check("runtime.aggregate_device", _check_runtime_aggregate_device)
	await _check("runtime.vibration", _check_runtime_vibration)
	await _check("runtime.force_feedback", _check_runtime_force_feedback)
	await _check("runtime.haptics", _check_runtime_haptics)

	if _options.virtual_pad:
		await _check("vpad.present", _check_vpad_present)
		await _check("vpad.info", _check_vpad_info)
		await _check("vpad.input", _check_vpad_input)
		await _check("vpad.rumble", _check_vpad_rumble)

	await _check("mock.session", _check_mock_session)
	await _check("sample.hotplug_ui", _check_sample_hotplug_ui)
	await _check("sample.action_bridge", _check_sample_action_bridge)
	await _check("sample.player_jump", _check_sample_player_jump)
	await _check("sample.inspector", _check_sample_inspector)
	await _check("sample.disconnect_releases", _check_sample_disconnect_releases)
	await _check("mock.device_kinds", _check_mock_device_kinds)
	await _check("mock.event_readings", _check_mock_event_readings)
	await _check("mock.system_events", _check_mock_system_events)
	await _check("mock.vibration", _check_mock_vibration)
	await _check("mock.force_feedback", _check_mock_force_feedback)
	await _check("mock.restore", _check_mock_restore)

	_finish(-1, "")


func _parse_options() -> String:
	var unknown := PackedStringArray()
	for arg in OS.get_cmdline_user_args():
		if arg == FLAG_RUN:
			continue
		elif arg == "--gameinput-strict":
			_options.strict = true
		elif arg == "--gameinput-virtual-pad":
			_options.virtual_pad = true
		elif arg == "--gameinput-session-locked":
			_options.session_locked = true
		elif arg.begins_with("--gameinput-report="):
			_options.report = arg.substr("--gameinput-report=".length())
		elif arg.begins_with("--gameinput-vpad-log="):
			_options.vpad_log = arg.substr("--gameinput-vpad-log=".length())
		elif arg.begins_with("--gameinput-timeout="):
			var value := arg.substr("--gameinput-timeout=".length())
			if not value.is_valid_float() or value.to_float() <= 0.0:
				unknown.append(arg)
			else:
				_options.timeout_sec = value.to_float()
		elif arg.begins_with("--gameinput-"):
			unknown.append(arg)
	if String(_options.report).strip_edges().is_empty():
		_options.report = DEFAULT_REPORT_PATH
		return "--gameinput-report needs a path"
	if not unknown.is_empty():
		return "unknown or malformed option(s): %s" % ", ".join(unknown)
	return ""


## The headless display server has no real window: after the first frame the
## root viewport shrinks to 64x64, which leaves the tutorial's player no room to
## move. Give it the size the project's window would open with.
func _size_headless_window() -> void:
	var root := get_tree().root
	if DisplayServer.get_name() == "headless":
		root.size = Vector2i(
				int(ProjectSettings.get_setting("display/window/size/viewport_width", 1152)),
				int(ProjectSettings.get_setting("display/window/size/viewport_height", 648)))
	_display = {"server": DisplayServer.get_name(), "viewport": [root.size.x, root.size.y]}


func _arm_watchdog() -> void:
	var timer := Timer.new()
	timer.name = "Watchdog"
	timer.one_shot = true
	timer.wait_time = _options.timeout_sec
	timer.timeout.connect(_on_watchdog)
	add_child(timer)
	timer.start()


func _on_watchdog() -> void:
	if _finished:
		return
	if not _cur.is_empty():
		_cur.failures.append("watchdog expired during this check")
		_close_check(_cur.started_usec)
	_finish(EXIT_WATCHDOG, "watchdog expired after %.0f s" % _options.timeout_sec)


## Runs one check. The check returns true when it reached its end; a script
## error aborts it early and returns null, which is reported as a failure.
func _check(id: String, fn: Callable) -> void:
	if _finished:
		return
	_cur = {"id": id, "failures": [], "skip": "", "detail": "", "data": {},
			"started_usec": Time.get_ticks_usec()}
	var completed = await fn.call()
	if _finished or _cur.is_empty():
		return
	if completed != true:
		_cur.failures.append("check aborted before completing (script error, see the log above)")
	_close_check(_cur.started_usec)


func _close_check(started_usec: int) -> void:
	var status := "pass"
	var detail: String = _cur.detail
	if not _cur.failures.is_empty():
		status = "fail"
		detail = "; ".join(PackedStringArray(_cur.failures))
	elif _cur.skip != "":
		status = "skip"
		detail = _cur.skip
	var entry := {
		"id": _cur.id,
		"group": String(_cur.id).get_slice(".", 0),
		"status": status,
		"detail": detail,
		"duration_ms": int((Time.get_ticks_usec() - started_usec) / 1000),
		"data": _cur.data,
	}
	_results.append(entry)
	print("[selftest] %s %s (%d ms)%s" % [status.to_upper().rpad(4), entry.id, entry.duration_ms,
			(" - " + detail) if detail != "" else ""])
	_cur = {}


func _finish(forced_exit: int, message: String) -> void:
	if _finished:
		return
	_finished = true
	var counts := {"pass": 0, "fail": 0, "skip": 0}
	for entry in _results:
		counts[entry.status] += 1
	var exit_code := forced_exit
	if exit_code < 0:
		if counts.fail > 0 or (_options.strict and counts.skip > 0):
			exit_code = EXIT_FAIL
		else:
			exit_code = EXIT_PASS
	var report := {
		"schema": SCHEMA,
		"started_utc": _started_utc,
		"finished_utc": _utc_now(),
		"duration_ms": Time.get_ticks_msec() - _started_ms,
		"godot_version": Engine.get_version_info().get("string", ""),
		"os": OS.get_name(),
		"project": ProjectSettings.get_setting("application/config/name", ""),
		"build": {
			"debug": OS.is_debug_build(),
			"mock_seams": _has_seams,
			"singleton": _gi != null,
		},
		"display": _display,
		"options": _options,
		"summary": {},
		"checks": _results,
	}
	var path := _resolve_path(_options.report)
	report.summary = _summary(counts, exit_code, message)
	var write_error := _write_text(path, JSON.stringify(report, "  "))
	if write_error != "":
		printerr("[selftest] could not write the report: %s" % write_error)
		exit_code = EXIT_HARNESS_ERROR
	var summary: Dictionary = _summary(counts, exit_code, message)
	print("[selftest] RESULT %s: %d passed, %d failed, %d skipped (exit %d)%s" % [
		String(summary.result).to_upper(), counts.pass, counts.fail, counts.skip, exit_code,
		(" - " + message) if message != "" else ""])
	if write_error == "":
		print("[selftest] report: %s" % path)
	get_tree().quit(exit_code)


func _summary(counts: Dictionary, exit_code: int, message: String) -> Dictionary:
	var result := "pass"
	match exit_code:
		EXIT_FAIL:
			result = "fail"
		EXIT_HARNESS_ERROR:
			result = "error"
		EXIT_WATCHDOG:
			result = "timeout"
	return {
		"result": result,
		"exit_code": exit_code,
		"message": message,
		"total": _results.size(),
		"pass": counts.pass,
		"fail": counts.fail,
		"skip": counts.skip,
	}


# ── Check helpers ────────────────────────────────────────────────────────

func _expect(condition: bool, what: String) -> bool:
	if not condition:
		_cur.failures.append(what)
	return condition


func _expect_near(actual: float, expected: float, what: String, tolerance: float = 0.001) -> bool:
	return _expect(absf(actual - expected) <= tolerance, "%s: expected %.4f, got %.4f" % [what, expected, actual])


func _skip(reason: String) -> void:
	_cur.skip = reason


func _pass_detail(detail: String) -> void:
	_cur.detail = detail


func _note(key: String, value) -> void:
	_cur.data[key] = value


func _c(native_class: String, constant_name: String) -> int:
	var key := native_class + "." + constant_name
	if not _const_cache.has(key):
		if ClassDB.class_exists(native_class) and ClassDB.class_has_integer_constant(native_class, constant_name):
			_const_cache[key] = ClassDB.class_get_integer_constant(native_class, constant_name)
		else:
			_const_cache[key] = 0
	return _const_cache[key]


func _k(constant_name: String) -> int:
	return _c("GameInput", constant_name)


func _d(constant_name: String) -> int:
	return _c("GameInputDevice", constant_name)


func _frames(count: int) -> void:
	for i in count:
		await get_tree().process_frame


func _physics_frames(count: int) -> void:
	for i in count:
		await get_tree().physics_frame


func _seconds(duration: float) -> void:
	await get_tree().create_timer(duration, true, false, true).timeout


func _kinds_to_string(mask: int) -> String:
	var names := PackedStringArray()
	for bit in KIND_NAMES:
		if mask & bit:
			names.append(KIND_NAMES[bit])
	return "+".join(names) if not names.is_empty() else "none"


func _hex4(value: int) -> String:
	return "%04X" % (value & 0xFFFF)


func _utc_now() -> String:
	return Time.get_datetime_string_from_system(true) + "Z"


func _resolve_path(path: String) -> String:
	if path.begins_with("user://") or path.begins_with("res://"):
		return ProjectSettings.globalize_path(path)
	return path


func _write_text(path: String, text: String) -> String:
	var dir := path.get_base_dir()
	if dir != "" and not DirAccess.dir_exists_absolute(dir):
		var mk := DirAccess.make_dir_recursive_absolute(dir)
		if mk != OK:
			return "cannot create %s (%s)" % [dir, error_string(mk)]
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		return "cannot open %s (%s)" % [path, error_string(FileAccess.get_open_error())]
	file.store_string(text + "\n")
	file.close()
	return ""


func _require_runtime() -> bool:
	if _gi == null:
		_cur.failures.append("GameInput singleton is not registered")
		return false
	if not _runtime_ready:
		_skip("the GameInput runtime is not initialized on this host")
		return false
	return true


func _require_mock() -> bool:
	if not _mock_ready:
		_skip("the mock backend is not available (release build or mock.session failed)")
		return false
	return true


func _require_mock_pad() -> bool:
	if not _require_mock():
		return false
	if _mock_pad_id <= 0 or sample == null:
		_skip("needs the mock pad from sample.hotplug_ui and the sample scene")
		return false
	return true


# ── Signal recording ─────────────────────────────────────────────────────

func _signal_table() -> Array:
	return [
		["device_connected", _on_device_connected],
		["device_disconnected", _on_device_disconnected],
		["device_status_changed", _on_device_status_changed],
		["reading_received", _on_reading_received],
		["system_buttons_changed", _on_system_buttons_changed],
		["keyboard_layout_changed", _on_keyboard_layout_changed],
	]


func _connect_signals() -> void:
	if _gi == null:
		return
	for entry in _signal_table():
		if _gi.has_signal(entry[0]) and not _gi.is_connected(entry[0], entry[1]):
			_gi.connect(entry[0], entry[1])


func _disconnect_signals() -> void:
	if _gi == null or not is_instance_valid(_gi):
		return
	for entry in _signal_table():
		if _gi.has_signal(entry[0]) and _gi.is_connected(entry[0], entry[1]):
			_gi.disconnect(entry[0], entry[1])


func _record(entry: Array) -> void:
	if _events.size() < 8192:
		_events.append(entry)


func _named(signal_name: String) -> Array:
	return _events.filter(func(e): return e[0] == signal_name)


func _on_device_connected(device) -> void:
	_record(["device_connected", device])


func _on_device_disconnected(device_id: int) -> void:
	_record(["device_disconnected", device_id])


func _on_device_status_changed(device, status: int, previous: int, timestamp: int) -> void:
	_record(["device_status_changed", device, status, previous, timestamp])


func _on_reading_received(device, reading) -> void:
	_record(["reading_received", device, reading])


func _on_system_buttons_changed(device, buttons: int, previous: int, timestamp: int) -> void:
	_record(["system_buttons_changed", device, buttons, previous, timestamp])


func _on_keyboard_layout_changed(device, layout: int, previous: int, timestamp: int) -> void:
	_record(["keyboard_layout_changed", device, layout, previous, timestamp])


# ── api ──────────────────────────────────────────────────────────────────

func _check_api_singleton() -> bool:
	if not _expect(_gi != null, "the GameInput singleton is not registered (build the addon and enable the plugin)"):
		return true
	_note("class", _gi.get_class())
	var boot := get_tree().root.get_node_or_null("GameInputBootstrap")
	_expect(boot != null, "the GameInputBootstrap autoload is not in the tree")
	_pass_detail("singleton '%s' and the bootstrap autoload are present" % _gi.get_class())
	return true


func _check_api_classes() -> bool:
	var missing := PackedStringArray()
	for native_class in CLASSES:
		if not ClassDB.class_exists(native_class):
			missing.append(native_class)
	_note("missing", Array(missing))
	_expect(missing.is_empty(), "missing classes: %s" % ", ".join(missing))
	_pass_detail("%d classes registered" % CLASSES.size())
	return true


func _check_api_methods() -> bool:
	var missing := PackedStringArray()
	var total := 0
	for native_class in METHODS:
		for method in METHODS[native_class]:
			total += 1
			if not ClassDB.class_exists(native_class) or not ClassDB.class_has_method(native_class, method, true):
				missing.append("%s.%s" % [native_class, method])
	_note("checked", total)
	_note("missing", Array(missing))
	_expect(missing.is_empty(), "missing methods: %s" % ", ".join(missing))
	_pass_detail("%d documented methods bound" % total)
	return true


func _check_api_signals() -> bool:
	var missing := PackedStringArray()
	for signal_name in SIGNALS:
		if not ClassDB.class_exists("GameInput") or not ClassDB.class_has_signal("GameInput", signal_name):
			missing.append(signal_name)
	_note("missing", Array(missing))
	_expect(missing.is_empty(), "missing signals: %s" % ", ".join(missing))
	_pass_detail("%d signals" % SIGNALS.size())
	return true


func _check_api_constants() -> bool:
	var wrong := PackedStringArray()
	for entry in CONSTANTS:
		var native_class: String = entry[0]
		var constant_name: String = entry[1]
		if not ClassDB.class_exists(native_class) or not ClassDB.class_has_integer_constant(native_class, constant_name):
			wrong.append("%s.%s missing" % [native_class, constant_name])
			continue
		var value: int = ClassDB.class_get_integer_constant(native_class, constant_name)
		if value != entry[2]:
			wrong.append("%s.%s = %d, expected %d" % [native_class, constant_name, value, entry[2]])
	_note("problems", Array(wrong))
	_expect(wrong.is_empty(), ", ".join(wrong))
	_pass_detail("%d stable constants" % CONSTANTS.size())
	return true


func _check_api_settings() -> bool:
	var missing := PackedStringArray()
	var values := {}
	for setting in SETTINGS:
		if ProjectSettings.has_setting(setting):
			values[setting] = ProjectSettings.get_setting(setting)
		else:
			missing.append(setting)
	_note("values", values)
	_expect(missing.is_empty(), "unregistered settings: %s" % ", ".join(missing))
	_expect(bool(values.get("game_input/runtime/initialize_on_startup", false)),
			"the sample must set game_input/runtime/initialize_on_startup=true")
	_pass_detail("%d settings registered" % SETTINGS.size())
	return true


func _check_api_static_helpers() -> bool:
	if not ClassDB.class_exists("GameInputDevice"):
		_cur.failures.append("GameInputDevice is not registered")
		return true
	var s := func(method: String, arg: int): return ClassDB.class_call_static("GameInputDevice", method, arg)
	_expect(s.call("scan_code_to_physical_key", 0x1E) == KEY_A, "scan code 0x1E is KEY_A")
	_expect(s.call("scan_code_to_physical_key", 0xE048) == KEY_UP, "extended scan code 0xE048 is KEY_UP")
	_expect(s.call("virtual_key_to_keycode", 0x41) == KEY_A, "virtual key 0x41 is KEY_A")
	_expect(s.call("switch_position_to_vector", _d("SWITCH_UP")) == Vector2(0, -1), "SWITCH_UP is Vector2(0, -1)")
	_expect(s.call("button_to_source", _d("BUTTON_A")) == _d("SRC_BTN_A"), "BUTTON_A maps to SRC_BTN_A")
	_expect(s.call("axis_to_source", _d("AXIS_WHEEL")) == _d("SRC_AXIS_WHEEL"), "AXIS_WHEEL maps to SRC_AXIS_WHEEL")
	_pass_detail("key, switch and source conversions")
	return true


# ── runtime (the real GameInput runtime on this machine) ────────────────

func _check_runtime_initialized() -> bool:
	if _gi == null:
		_cur.failures.append("GameInput singleton is not registered")
		return true
	_runtime_ready = _gi.is_initialized()
	if _has_seams:
		_note("backend", _gi._test_get_backend())
	if not _runtime_ready:
		_skip("GameInput is not initialized on this host (GameInputCreate failed or the runtime is missing); runtime checks are skipped")
		return true
	if _has_seams:
		_expect(_gi._test_get_backend() == 1, "the bootstrap initialized a backend other than native GameInput")
	_pass_detail("the bootstrap autoload initialized native GameInput")
	return true


func _check_runtime_timestamp() -> bool:
	if not _require_runtime():
		return true
	var first: int = _gi.get_current_timestamp()
	await _frames(2)
	var second: int = _gi.get_current_timestamp()
	_note("first_usec", first)
	_note("second_usec", second)
	_expect(first > 0, "get_current_timestamp() returned %d" % first)
	_expect(second > first, "the GameInput clock did not advance across two frames")
	_pass_detail("GameInput clock advances (%d us over two frames)" % (second - first))
	return true


func _check_runtime_devices() -> bool:
	if not _require_runtime():
		return true
	var devices: Array = _gi.get_devices(_k("DEVICE_ANY"))
	_native_device_count = devices.size()
	_expect(_gi.get_connected_device_count(_k("DEVICE_ANY")) == devices.size(),
			"get_connected_device_count(DEVICE_ANY) disagrees with get_devices(DEVICE_ANY)")
	var summaries := []
	var labels := PackedStringArray()
	var ids := {}
	for device in devices:
		var id: int = device.get_device_id()
		var info: Dictionary = device.get_device_info()
		var summary := {
			"id": id,
			"name": device.get_display_name(),
			"vid_pid": "%s:%s" % [_hex4(info.get("vendor_id", 0)), _hex4(info.get("product_id", 0))],
			"kinds": _kinds_to_string(device.get_kind_mask()),
			"family": device.get_device_family(),
			"status": device.get_status(),
			"rumble_motors": device.get_supported_rumble_motors(),
			"force_feedback_motors": device.get_force_feedback_motor_count(),
			"haptics": device.supports_haptics(),
		}
		summaries.append(summary)
		labels.append("%s %s [%s]" % [summary.name, summary.vid_pid, summary.kinds])
		_expect(id > 0 and not ids.has(id), "device id %d is not a unique positive id" % id)
		ids[id] = true
		_expect(device.is_connected(), "device %d is listed but not connected" % id)
		_expect(device.get_kind_mask() != 0, "device %d reports no kinds" % id)
		_expect(device.get_status() & _d("STATUS_CONNECTED") != 0, "device %d status lacks STATUS_CONNECTED" % id)
		var again = _gi.get_device_by_id(id)
		_expect(again != null and again.get_device_id() == id, "get_device_by_id(%d) does not return the device" % id)
		for key in ["name", "vendor_id", "product_id", "device_family", "supported_input_mask", "status", "app_local_id"]:
			_expect(info.has(key), "device %d info lacks '%s'" % [id, key])
		_expect(String(device.get_app_local_id()).length() == 64,
				"device %d app-local id is not 64 hex characters" % id)
	var pads: Array = _gi.get_devices(_k("DEVICE_GAMEPAD"))
	var primary = _gi.get_primary_device(_k("DEVICE_GAMEPAD"))
	if pads.is_empty():
		_expect(primary == null, "get_primary_device returned a device while no gamepad is listed")
	else:
		_expect(primary != null and primary.get_device_id() == pads[0].get_device_id(),
				"the primary gamepad is not the first listed gamepad")
	_note("devices", summaries)
	_pass_detail("%d device(s)%s" % [devices.size(), (": " + ", ".join(labels)) if not labels.is_empty() else ""])
	return true


func _check_runtime_readings() -> bool:
	if not _require_runtime():
		return true
	var devices: Array = _gi.get_devices(_k("DEVICE_ANY"))
	if devices.is_empty():
		_skip("no GameInput devices are connected")
		return true
	var now: int = _gi.get_current_timestamp()
	var with_reading := 0
	for device in devices:
		var reading = _gi.get_current_reading(device)
		if reading == null:
			continue
		with_reading += 1
		var id: int = device.get_device_id()
		_expect(reading.get_device_id() == id, "reading of device %d carries id %d" % [id, reading.get_device_id()])
		_expect(reading.get_input_kinds() & ~device.get_kind_mask() == 0,
				"device %d reading kinds %s exceed the device kinds %s" % [id,
				_kinds_to_string(reading.get_input_kinds()), _kinds_to_string(device.get_kind_mask())])
		_expect(reading.get_timestamp() <= now + 1000000,
				"device %d reading is timestamped in the future" % id)
	_note("devices_with_reading", with_reading)
	_pass_detail("%d of %d device(s) returned a consistent reading" % [with_reading, devices.size()])
	return true


func _check_runtime_reading_callbacks() -> bool:
	if not _require_runtime():
		return true
	var previous: int = _gi.get_reading_callback_kinds()
	_expect(_gi.set_reading_callback_kinds(_k("DEVICE_GAMEPAD")), "RegisterReadingCallback failed")
	_expect(_gi.get_reading_callback_kinds() == _k("DEVICE_GAMEPAD"), "callback kinds did not read back")
	await _frames(3)
	var dropped: int = _gi.get_dropped_reading_count()
	_expect(dropped >= 0, "negative dropped-reading count")
	_gi.set_reading_callback_kinds(previous)
	_expect(_gi.get_reading_callback_kinds() == previous, "callback kinds were not restored")
	_note("dropped", dropped)
	_pass_detail("reading callback registered and restored")
	return true


func _check_runtime_focus_policy() -> bool:
	if not _require_runtime():
		return true
	var previous: int = _gi.get_focus_policy()
	var background := _k("FOCUS_POLICY_ENABLE_BACKGROUND_INPUT")
	_gi.set_focus_policy(background)
	_expect(_gi.get_focus_policy() == background, "background focus policy did not read back")
	_gi.set_focus_policy(previous)
	_expect(_gi.get_focus_policy() == previous, "focus policy was not restored")
	_pass_detail("focus policy round trip")
	return true


func _check_runtime_aggregate_device() -> bool:
	if not _require_runtime():
		return true
	var id: String = _gi.create_aggregate_device(_k("DEVICE_GAMEPAD"))
	_note("app_local_id", id)
	if not _expect(id.length() == 64 and id.is_valid_hex_number(), "create_aggregate_device returned '%s'" % id):
		return true
	var surfaced := false
	var deadline := Time.get_ticks_msec() + 1500
	while Time.get_ticks_msec() < deadline and not surfaced:
		await _frames(1)
		for device in _gi.get_devices(_k("DEVICE_ANY")):
			if device.get_device_family() == _d("FAMILY_AGGREGATE"):
				surfaced = true
	_note("surfaced", surfaced)
	_expect(_gi.disable_aggregate_device(id), "disable_aggregate_device rejected the id it was given")
	_pass_detail("aggregate gamepad created and disabled (%s)" %
			("surfaced as a device" if surfaced else "no aggregate device surfaced within 1.5 s"))
	return true


func _check_runtime_vibration() -> bool:
	if not _require_runtime():
		return true
	var rumblers := []
	for device in _gi.get_devices(_k("DEVICE_ANY")):
		if device.get_supported_rumble_motors() != 0:
			rumblers.append(device)
	if rumblers.is_empty():
		_skip("no connected device has rumble motors")
		return true
	# A short, gentle pulse: a developer holding the controller feels one tick.
	var names := PackedStringArray()
	for device in rumblers:
		names.append(device.get_display_name())
		_expect(device.start_vibration(0.2, 0.2, 0.15), "start_vibration failed on %s" % device.get_display_name())
		_expect(device.is_vibrating(), "%s does not report vibrating" % device.get_display_name())
	await _seconds(0.4)
	for device in rumblers:
		_expect(not device.is_vibrating(), "poll() did not stop the timed vibration on %s" % device.get_display_name())
	_note("devices", Array(names))
	_pass_detail("timed rumble started and auto-stopped on %d device(s)" % rumblers.size())
	return true


func _check_runtime_force_feedback() -> bool:
	if not _require_runtime():
		return true
	var motors := []
	for device in _gi.get_devices(_k("DEVICE_ANY")):
		for motor in device.get_force_feedback_motor_count():
			var info: Dictionary = device.get_force_feedback_motor_info(motor)
			_expect(info.has("supported_axes") and info.has("supported_effects"),
					"%s motor %d info is incomplete" % [device.get_display_name(), motor])
			motors.append({"device": device.get_display_name(), "motor": motor, "info": info,
					"powered": device.is_force_feedback_motor_powered_on(motor)})
	if motors.is_empty():
		_skip("no connected device has force-feedback motors")
		return true
	# Effects are never played on real hardware from here: a wheel can move on its own.
	_note("motors", motors)
	_pass_detail("%d force-feedback motor(s) described" % motors.size())
	return true


func _check_runtime_haptics() -> bool:
	if not _require_runtime():
		return true
	var devices := []
	for device in _gi.get_devices(_k("DEVICE_ANY")):
		if device.supports_haptics():
			var info: Dictionary = device.get_haptic_info()
			_expect(info.get("supported", false), "%s haptic info says unsupported" % device.get_display_name())
			devices.append({"device": device.get_display_name(), "locations": Array(info.get("locations", []))})
	if devices.is_empty():
		_skip("no connected device reports haptics")
		return true
	_note("devices", devices)
	_pass_detail("%d haptic device(s) described" % devices.size())
	return true


# ── vpad (a ViGEm Xbox 360 pad driven by tools/virtual_gamepad) ─────────

func _find_vpad():
	for device in _gi.get_devices(_k("DEVICE_ANY")):
		var info: Dictionary = device.get_device_info()
		if info.get("vendor_id", 0) == VPAD_VENDOR_ID and info.get("product_id", 0) == VPAD_PRODUCT_ID \
				and device.get_kind_mask() & _k("DEVICE_GAMEPAD") != 0:
			return device
	return null


func _check_vpad_present() -> bool:
	if not _require_runtime():
		return true
	var deadline := Time.get_ticks_msec() + int(VPAD_WAIT_SEC * 1000)
	_vpad = _find_vpad()
	while _vpad == null and Time.get_ticks_msec() < deadline:
		await _frames(5)
		_vpad = _find_vpad()
	if not _expect(_vpad != null, "no %s:%s gamepad appeared within %.0f s (is the vpad driver running?)" % [
			_hex4(VPAD_VENDOR_ID), _hex4(VPAD_PRODUCT_ID), VPAD_WAIT_SEC]):
		return true
	_note("device_id", _vpad.get_device_id())
	_pass_detail("virtual pad is device %d (%s)" % [_vpad.get_device_id(), _vpad.get_display_name()])
	return true


func _check_vpad_info() -> bool:
	if _vpad == null:
		_skip("the virtual pad is not present")
		return true
	var rumble: int = _vpad.get_supported_rumble_motors()
	var family: int = _vpad.get_device_family()
	_note("family", family)
	_note("rumble_motors", rumble)
	_note("system_buttons", _vpad.get_supported_system_buttons())
	_expect(family == _d("FAMILY_XBOX_360"), "family is %d, expected FAMILY_XBOX_360" % family)
	var both := _d("RUMBLE_LOW_FREQUENCY") | _d("RUMBLE_HIGH_FREQUENCY")
	_expect(rumble & both == both, "rumble motors are %d, expected low and high frequency" % rumble)
	_expect(_vpad.supports_vibration(), "supports_vibration() is false")
	_pass_detail("Xbox 360 family with low and high-frequency rumble")
	return true


func _check_vpad_input() -> bool:
	if _vpad == null:
		_skip("the virtual pad is not present")
		return true
	var mapper = sample.get_mapper() if sample != null else null
	var previous_policy: int = _gi.get_focus_policy()
	var previous_kinds: int = _gi.get_reading_callback_kinds()
	var previous_target: int = mapper.target_device_id if mapper != null else -1
	# Headless Godot has no window, so ask for input while unfocused.
	_gi.set_focus_policy(_k("FOCUS_POLICY_ENABLE_BACKGROUND_INPUT"))
	_gi.set_reading_callback_kinds(_k("DEVICE_GAMEPAD"))
	if mapper != null:
		mapper.target_device_id = _vpad.get_device_id()
	_events.clear()
	var a := _d("BUTTON_A")
	var seen := {"a_down": false, "stick_right": false, "jump": false, "move_right": false, "event_press": false}
	var first_timestamp := -1
	var timestamp_changed := false
	var event_count := 0
	var deadline := Time.get_ticks_msec() + int(VPAD_INPUT_WAIT_SEC * 1000)
	while Time.get_ticks_msec() < deadline and seen.values().has(false):
		await _frames(1)
		var reading = _gi.get_current_reading(_vpad)
		if reading != null:
			if first_timestamp < 0:
				first_timestamp = reading.get_timestamp()
			elif reading.get_timestamp() != first_timestamp:
				timestamp_changed = true
			if reading.is_button_down(a):
				seen.a_down = true
			if reading.get_axis(_d("AXIS_LEFT_X")) > 0.5:
				seen.stick_right = true
		if Input.is_action_pressed("jump"):
			seen.jump = true
		if Input.get_action_strength("move_right") > 0.5:
			seen.move_right = true
		for e in _named("reading_received"):
			if e[1] != null and e[1].get_device_id() == _vpad.get_device_id():
				event_count += 1
				if e[2].was_button_pressed(a):
					seen.event_press = true
		_events.clear()
	if mapper != null:
		mapper.target_device_id = previous_target
	_gi.set_reading_callback_kinds(previous_kinds)
	_gi.set_focus_policy(previous_policy)
	_note("seen", seen)
	_note("timestamp_changed", timestamp_changed)
	_note("reading_events", event_count)
	if not timestamp_changed and not seen.values().has(true):
		var why := "the session is locked" if _options.session_locked else "the process had no input focus or the session is locked"
		_skip("GameInput delivered no input from the virtual pad in %.0f s (%s); rumble is still checked" % [VPAD_INPUT_WAIT_SEC, why])
		return true
	for key in seen:
		_expect(seen[key], "never observed '%s' while the vpad driver cycled A and the left stick" % key)
	_pass_detail("A and left stick arrived through polling, %d reading_received events and the mapper (jump, move_right)" % event_count)
	return true


func _check_vpad_rumble() -> bool:
	if _vpad == null:
		_skip("the virtual pad is not present")
		return true
	# Park the mapper on a device id that never exists, so the driver's A
	# presses cannot make the player jump and send the sample's own rumble.
	var mapper = sample.get_mapper() if sample != null else null
	var previous_target: int = mapper.target_device_id if mapper != null else -1
	if mapper != null:
		mapper.target_device_id = 0x7FFFFFFF
	await _frames(2)
	var completed = await _vpad_rumble_round_trip()
	if mapper != null:
		mapper.target_device_id = previous_target
	return completed == true


func _vpad_rumble_round_trip() -> bool:
	var sent_at := Time.get_unix_time_from_system()
	_expect(_vpad.start_vibration(VPAD_RUMBLE_WEAK, VPAD_RUMBLE_STRONG, VPAD_RUMBLE_SEC), "start_vibration failed")
	_expect(_vpad.is_vibrating(), "is_vibrating() is false right after start_vibration")
	_expect_near(_vpad.get_vibration_strength().y, VPAD_RUMBLE_STRONG, "strong strength")
	await _seconds(VPAD_RUMBLE_SEC + 0.3)
	_expect(not _vpad.is_vibrating(), "poll() did not stop the timed vibration")
	if _options.vpad_log == "":
		_pass_detail("rumble started and stopped (pass --gameinput-vpad-log to cross-check the driver)")
		return true
	# The driver appends to its log from another process: re-read until the
	# stop shows up or the wait runs out.
	var matched := {}
	var stopped := {}
	var deadline := Time.get_ticks_msec() + int(VPAD_RUMBLE_WAIT_SEC * 1000)
	while true:
		matched = {}
		stopped = {}
		for entry in _read_vpad_log():
			if entry.get("event", "") != "rumble" or float(entry.get("t", 0.0)) < sent_at - 0.25:
				continue
			var large := int(entry.get("large", -1))
			var small := int(entry.get("small", -1))
			if matched.is_empty():
				if absi(large - VPAD_EXPECT_LARGE) <= VPAD_RUMBLE_TOLERANCE \
						and absi(small - VPAD_EXPECT_SMALL) <= VPAD_RUMBLE_TOLERANCE:
					matched = entry
			elif large == 0 and small == 0:
				stopped = entry
				break
		if not stopped.is_empty() or Time.get_ticks_msec() >= deadline:
			break
		await _seconds(0.1)
	_note("driver_rumble", matched)
	_note("driver_stop", stopped)
	if not _expect(not matched.is_empty(), "the vpad driver never logged large=%d small=%d (strong -> low-frequency motor)" % [
			VPAD_EXPECT_LARGE, VPAD_EXPECT_SMALL]):
		return true
	if not _expect(not stopped.is_empty(), "the vpad driver never logged the auto-stop (large=0 small=0)"):
		return true
	var held := float(stopped.t) - float(matched.t)
	_note("held_sec", held)
	_expect(held >= VPAD_RUMBLE_SEC * 0.5 and held <= VPAD_RUMBLE_SEC + 1.5,
			"rumble lasted %.3f s at the driver, expected about %.2f s" % [held, VPAD_RUMBLE_SEC])
	_pass_detail("driver saw large=%d small=%d, then the auto-stop %.2f s later" % [
			int(matched.large), int(matched.small), held])
	return true


func _read_vpad_log() -> Array:
	var path := _resolve_path(_options.vpad_log)
	if not FileAccess.file_exists(path):
		return []
	var entries := []
	for line in FileAccess.get_file_as_string(path).split("\n", false):
		var parsed = JSON.parse_string(line)
		if parsed is Dictionary:
			entries.append(parsed)
	return entries


# ── mock + sample (debug builds: scripted devices through the real pipeline)

func _check_mock_session() -> bool:
	if _gi == null:
		_cur.failures.append("GameInput singleton is not registered")
		return true
	if not _has_seams:
		_skip("mock seams are compiled out of release builds; sample and mock checks are skipped")
		return true
	_mock_saved = {
		"initialized": _gi.is_initialized(),
		"callback_kinds": _gi.get_reading_callback_kinds(),
		"focus_policy": _gi.get_focus_policy(),
	}
	_gi.shutdown()
	_gi.set_reading_callback_kinds(0)
	_gi.set_focus_policy(0)
	_expect(_gi._test_initialize_mock(), "_test_initialize_mock() failed")
	_expect(_gi._test_get_backend() == 2, "the mock backend is not active")
	_expect(_gi.is_initialized(), "the mock runtime does not report initialized")
	_expect(_gi.get_devices(_k("DEVICE_ANY")).is_empty(), "the mock runtime did not start empty")
	_mock_ready = _cur.failures.is_empty()
	_pass_detail("native runtime shut down, mock backend active")
	return true


func _check_sample_hotplug_ui() -> bool:
	if not _require_mock():
		return true
	_events.clear()
	_mock_pad_id = _gi._test_inject_device({"name": "Selftest Pad", "rumble_motors": 0xF})
	if not _expect(_mock_pad_id > 0, "_test_inject_device rejected a gamepad"):
		return true
	# No forced poll: the bootstrap autoload's per-frame poll must deliver it.
	await _frames(2)
	var connected := _named("device_connected")
	_expect(connected.size() == 1 and connected[0][1].get_device_id() == _mock_pad_id,
			"device_connected did not fire once for the injected pad")
	if sample != null:
		_expect(sample.get_devices_text().contains("Selftest Pad"), "the sample's device list does not show the pad")
		_expect(sample.get_device_count_text().ends_with(": 1"),
				"the sample's gamepad count reads '%s'" % sample.get_device_count_text())
		_expect(sample.get_hotplug_text().contains("connected: id=%d" % _mock_pad_id),
				"the sample's hot-plug log does not show the connect")
	_pass_detail("injected pad reached device_connected and the sample UI on the next frame")
	return true


func _check_sample_action_bridge() -> bool:
	if not _require_mock_pad():
		return true
	var mapper = sample.get_mapper()
	if not _expect(mapper != null, "the sample has no GameInputMapper"):
		return true
	_gi._test_push_reading(_mock_pad_id, {"gamepad": {"buttons": _d("BUTTON_A")}})
	await _frames(2)
	_expect(Input.is_action_pressed("jump"), "A did not press 'jump'")
	_expect(Input.is_action_pressed("ui_accept"), "A did not press 'ui_accept'")
	_expect(mapper.get_active_binding_count() == 2, "%d bindings active while A is held, expected 2" % mapper.get_active_binding_count())
	_gi._test_push_reading(_mock_pad_id, {"gamepad": {"buttons": 0, "left_x": -1.0}})
	await _frames(2)
	_expect(not Input.is_action_pressed("jump"), "'jump' stayed pressed after A was released")
	_expect_near(Input.get_action_strength("move_left"), 1.0, "full left stick -> move_left", 0.01)
	_expect_near(Input.get_action_strength("move_right"), 0.0, "full left stick -> move_right", 0.01)
	_gi._test_push_reading(_mock_pad_id, {"gamepad": {"left_x": 0.6}})
	await _frames(2)
	# Axis bindings rescale past the 0.2 deadzone: (0.6 - 0.2) / 0.8.
	_expect_near(Input.get_action_strength("move_right"), 0.5, "left stick 0.6 -> move_right", 0.01)
	_expect(not Input.is_action_pressed("move_left"), "'move_left' stayed pressed")
	_gi._test_push_reading(_mock_pad_id, {"gamepad": {}})
	await _frames(2)
	_expect(not Input.is_action_pressed("move_right"), "'move_right' stayed pressed at rest")
	_pass_detail("A -> jump + ui_accept, left stick -> move_left / move_right with deadzone rescaling")
	return true


func _check_sample_player_jump() -> bool:
	if not _require_mock_pad():
		return true
	# Land first: an earlier vpad check may have left the player mid-jump.
	var land_deadline := Time.get_ticks_msec() + 2000
	while not sample.is_player_on_floor() and Time.get_ticks_msec() < land_deadline:
		await _physics_frames(1)
	var floor_y: float = sample.get_player_position().y
	# Freeze the addon's clock so the jump rumble cannot time out before it is read.
	var frozen_usec := Time.get_ticks_usec()
	_gi._test_set_time_override_usec(frozen_usec)
	var rumbles_before: int = _gi._test_get_last_rumble(_mock_pad_id).apply_count
	_gi._test_push_reading(_mock_pad_id, {"gamepad": {"buttons": _d("BUTTON_A")}})
	var rose := false
	for i in 30:
		await _physics_frames(1)
		if sample.get_player_position().y < floor_y - 1.0:
			rose = true
			break
	_gi._test_push_reading(_mock_pad_id, {"gamepad": {}})
	_expect(rose, "the player did not leave the floor within 30 physics frames of pressing A")
	var rumble: Dictionary = _gi._test_get_last_rumble(_mock_pad_id)
	_note("jump_rumble", rumble)
	if _expect(rumble.apply_count > rumbles_before, "jumping did not rumble the pad (Step 7)"):
		_expect(rumble.active, "the jump rumble is not active")
		_expect_near(rumble.low, sample.JUMP_RUMBLE_STRONG, "jump rumble strong (low-frequency) motor")
		_expect_near(rumble.high, sample.JUMP_RUMBLE_WEAK, "jump rumble weak (high-frequency) motor")
		_expect(rumble.end_usec == frozen_usec + int(round(sample.JUMP_RUMBLE_SEC * 1000000.0)),
				"the jump rumble does not end %.2f s after it started" % sample.JUMP_RUMBLE_SEC)
		_gi._test_set_time_override_usec(rumble.end_usec)
		_gi._test_force_poll()
		_expect(not _gi._test_get_last_rumble(_mock_pad_id).active, "the jump rumble did not stop on time")
	_gi._test_set_time_override_usec(-1)
	await _frames(2)
	var max_x: float = sample.get_player_max_x()
	_note("play_area_max_x", max_x)
	if not _expect(max_x >= 8.0, "the play area is too narrow to move in (max x %.1f)" % max_x):
		return true
	# Push towards the side with room, so a player parked at a wall cannot pass vacuously.
	var start_x: float = sample.get_player_position().x
	var direction := 1.0 if start_x < max_x * 0.5 else -1.0
	var side := "right" if direction > 0.0 else "left"
	_gi._test_push_reading(_mock_pad_id, {"gamepad": {"left_x": direction}})
	await _physics_frames(6)
	_gi._test_push_reading(_mock_pad_id, {"gamepad": {}})
	var moved: float = sample.get_player_position().x - start_x
	_note("moved_px", moved)
	_expect(moved * direction > 1.0, "the player did not move %s with the stick (moved %.1f px)" % [side, moved])
	await _frames(2)
	_pass_detail("gameplay code reacted: the player jumped (with a %.2f s rumble) and moved %.0f px %s" % [
			sample.JUMP_RUMBLE_SEC, absf(moved), side])
	return true


func _check_sample_inspector() -> bool:
	if not _require_mock_pad():
		return true
	var inspector = sample.get_inspector()
	if not _expect(inspector != null, "the sample has no inspector panel"):
		return true
	_expect(inspector.get_listed_device_ids().has(_mock_pad_id), "the inspector does not list the mock pad")
	inspector.select_device(_mock_pad_id)
	_gi._test_push_reading(_mock_pad_id, {"gamepad": {"buttons": _d("BUTTON_B"), "left_trigger": 0.5}})
	await _frames(3)
	var live: String = inspector.get_live_text()
	_note("live_text", live)
	_expect(live.contains("buttons: B"), "the inspector's live view does not show B")
	_expect(live.contains("LT 0.50"), "the inspector's live view does not show the left trigger")
	_gi._test_push_reading(_mock_pad_id, {"gamepad": {}})
	await _frames(1)
	_pass_detail("inspector lists the pad and renders its live state")
	return true


func _check_sample_disconnect_releases() -> bool:
	if not _require_mock_pad():
		return true
	_gi._test_push_reading(_mock_pad_id, {"gamepad": {"buttons": _d("BUTTON_A")}})
	await _frames(2)
	_expect(Input.is_action_pressed("jump"), "A did not press 'jump'")
	_events.clear()
	_expect(_gi._test_remove_device(_mock_pad_id), "_test_remove_device failed")
	await _frames(2)
	var gone := _named("device_disconnected")
	_expect(gone.size() == 1 and gone[0][1] == _mock_pad_id, "device_disconnected did not fire once")
	_expect(not Input.is_action_pressed("jump"), "'jump' stayed pressed after the pad was unplugged mid-press")
	_expect(sample.get_hotplug_text().contains("disconnected: id=%d" % _mock_pad_id),
			"the sample's hot-plug log does not show the disconnect")
	_expect(not sample.get_devices_text().contains("Selftest Pad"), "the sample still lists the pad")
	_mock_pad_id = -1
	_pass_detail("unplugging mid-press released the action and updated the UI")
	return true


func _mock_device(kind: String, extra: Dictionary = {}):
	var info := {"kind_mask": _k(kind)}
	info.merge(extra, true)
	var id: int = _gi._test_inject_device(info)
	if id <= 0:
		return null
	_gi._test_force_poll()
	return _gi.get_device_by_id(id)


func _mock_reading(device, state: Dictionary):
	_gi._test_push_reading(device.get_device_id(), state)
	_gi._test_force_poll()
	return _gi.get_current_reading(device)


func _check_mock_device_kinds() -> bool:
	if not _require_mock():
		return true
	var verified := PackedStringArray()

	var kb = _mock_device("DEVICE_KEYBOARD", {"keyboard_layout": 0x0409})
	if _expect(kb != null, "keyboard injection failed"):
		var r = _mock_reading(kb, {"keyboard": {"keys": [0x11, 0x1E, 0xE048]}})
		_expect(r.is_physical_key_down(KEY_W) and r.is_physical_key_down(KEY_A), "keyboard: W and A")
		_expect(r.is_physical_key_down(KEY_UP), "keyboard: extended Up arrow")
		_expect(r.was_physical_key_pressed(KEY_W), "keyboard: W press edge")
		verified.append("keyboard")

	var mouse = _mock_device("DEVICE_MOUSE")
	if _expect(mouse != null, "mouse injection failed"):
		_mock_reading(mouse, {"mouse": {"x": 100, "y": 50}})
		var r = _mock_reading(mouse, {"mouse": {"buttons": _d("MOUSE_LEFT"), "x": 110, "y": 45, "wheel_y": 120}})
		_expect(r.get_mouse_delta() == Vector2(10, -5), "mouse: delta %s" % str(r.get_mouse_delta()))
		_expect(r.was_mouse_button_pressed(_d("MOUSE_LEFT")), "mouse: left press edge")
		_expect(r.get_mouse_wheel_delta().y == 120.0, "mouse: wheel delta")
		verified.append("mouse")

	var sensors = _mock_device("DEVICE_SENSORS")
	if _expect(sensors != null, "sensor injection failed"):
		var r = _mock_reading(sensors, {"sensors": {"acceleration_g": Vector3(0, -1, 0),
				"angular_velocity": Vector3(0, 0, 0.5), "heading": 90.0}})
		_expect_near(r.get_accelerometer().y, -9.80665, "sensors: 1 g in m/s^2")
		_expect_near(r.get_gyroscope().z, 0.5, "sensors: gyroscope rad/s")
		_expect_near(r.get_heading_degrees(), 90.0, "sensors: heading")
		verified.append("sensors")

	var arcade = _mock_device("DEVICE_ARCADE_STICK")
	if _expect(arcade != null, "arcade stick injection failed"):
		var r = _mock_reading(arcade, {"arcade_stick": {"buttons": _d("ARCADE_STICK_ACTION_1")}})
		_expect(r.is_source_down(_d("SRC_ARCADE_ACTION_1")), "arcade: action 1 source")
		verified.append("arcade_stick")

	var flight = _mock_device("DEVICE_FLIGHT_STICK")
	if _expect(flight != null, "flight stick injection failed"):
		var r = _mock_reading(flight, {"flight_stick": {"hat": _d("SWITCH_UP"), "throttle": 0.6,
				"buttons": _d("FLIGHT_STICK_FIRE_PRIMARY")}})
		_expect(r.get_flight_stick_hat_vector() == Vector2(0, -1), "flight: hat up is -Y")
		_expect_near(r.get_axis(_d("AXIS_FLIGHT_THROTTLE")), 0.6, "flight: throttle")
		_expect(r.is_source_down(_d("SRC_FLIGHT_FIRE_PRIMARY")), "flight: primary fire source")
		verified.append("flight_stick")

	var wheel = _mock_device("DEVICE_RACING_WHEEL")
	if _expect(wheel != null, "racing wheel injection failed"):
		var r = _mock_reading(wheel, {"racing_wheel": {"wheel": -0.25, "throttle": 0.9, "gear": 3}})
		_expect_near(r.get_axis(_d("AXIS_WHEEL")), -0.25, "wheel: steering")
		_expect_near(r.get_axis(_d("AXIS_THROTTLE")), 0.9, "wheel: throttle")
		_expect(r.get_racing_wheel_gear() == 3, "wheel: gear")
		verified.append("racing_wheel")

	var raw = _mock_device("DEVICE_CONTROLLER")
	if _expect(raw != null, "raw controller injection failed"):
		var r = _mock_reading(raw, {"controller": {"axes": [0.5, -0.5], "buttons": [false, true],
				"switches": [_d("SWITCH_LEFT")]}})
		_expect_near(r.get_controller_axis(1), -0.5, "controller: axis 1")
		_expect(r.is_controller_button_down(1), "controller: button 1")
		_expect(r.get_controller_switch(0) == _d("SWITCH_LEFT"), "controller: switch 0")
		verified.append("controller")

	_note("verified", Array(verified))
	_pass_detail("readings for %s" % ", ".join(verified))
	return true


func _check_mock_event_readings() -> bool:
	if not _require_mock():
		return true
	_expect(_gi.set_reading_callback_kinds(_k("DEVICE_GAMEPAD")), "set_reading_callback_kinds failed")
	var pad = _mock_device("DEVICE_GAMEPAD", {"name": "Tap Pad"})
	if not _expect(pad != null, "gamepad injection failed"):
		return true
	var a := _d("BUTTON_A")
	_events.clear()
	# A full tap between two polls: a polled game would miss it entirely.
	_gi._test_push_reading(pad.get_device_id(), {"timestamp": 1000, "gamepad": {"buttons": a}})
	_gi._test_push_reading(pad.get_device_id(), {"timestamp": 2000, "gamepad": {"buttons": 0}})
	_gi._test_force_poll()
	var readings := _named("reading_received").filter(
			func(e): return e[1].get_device_id() == pad.get_device_id()).map(func(e): return e[2])
	if _expect(readings.size() == 2, "expected 2 reading_received, got %d" % readings.size()):
		_expect(readings[0].was_button_pressed(a), "first event reading is the press")
		_expect(readings[1].was_button_released(a), "second event reading is the release")
		_expect(readings[1].get_timestamp() == 2000, "event readings keep their timestamps")
	_expect(_gi.get_buffered_readings(pad).size() == 2, "get_buffered_readings does not hold both readings")
	_expect(not _gi.get_current_reading(pad).is_button_down(a), "the polled reading should show A up")
	_gi.set_reading_callback_kinds(0)
	_gi._test_remove_device(pad.get_device_id())
	_gi._test_force_poll()
	_pass_detail("a sub-frame tap produced press and release events and two buffered readings")
	return true


func _check_mock_system_events() -> bool:
	if not _require_mock():
		return true
	var guide := _d("SYSTEM_BUTTON_GUIDE")
	var pad = _mock_device("DEVICE_GAMEPAD", {"name": "System Pad", "system_buttons": guide | _d("SYSTEM_BUTTON_SHARE")})
	var kb = _mock_device("DEVICE_KEYBOARD", {"name": "Layout Keyboard", "keyboard_layout": 0x0409})
	if not _expect(pad != null and kb != null, "device injection failed"):
		return true
	_events.clear()
	_gi._test_push_system_buttons(pad.get_device_id(), guide)
	_gi._test_push_keyboard_layout(kb.get_device_id(), 0x0407)
	var ready := _d("STATUS_CONNECTED") | _d("STATUS_HAPTIC_INFO_READY")
	_gi._test_set_device_status(pad.get_device_id(), ready)
	_gi._test_force_poll()
	var sys := _named("system_buttons_changed")
	if _expect(sys.size() == 1, "expected one system_buttons_changed, got %d" % sys.size()):
		_expect(sys[0][2] == guide and sys[0][3] == 0, "Guide press: buttons %d previous %d" % [sys[0][2], sys[0][3]])
	_expect(pad.get_system_buttons() == guide, "get_system_buttons() does not report Guide")
	var layout := _named("keyboard_layout_changed")
	if _expect(layout.size() == 1, "expected one keyboard_layout_changed, got %d" % layout.size()):
		_expect(layout[0][2] == 0x0407 and layout[0][3] == 0x0409, "layout 0x0409 -> 0x0407")
	var status := _named("device_status_changed")
	if _expect(status.size() == 1, "expected one device_status_changed, got %d" % status.size()):
		_expect(status[0][2] == ready and status[0][3] == _d("STATUS_CONNECTED"), "status change carries the previous status")
	_gi._test_remove_device(pad.get_device_id())
	_gi._test_force_poll()
	_pass_detail("system button, keyboard layout and status signals carry current and previous values")
	return true


func _check_mock_vibration() -> bool:
	if not _require_mock():
		return true
	var pad = _mock_device("DEVICE_GAMEPAD", {"name": "Rumble Pad", "rumble_motors": 0xF})
	if not _expect(pad != null, "gamepad injection failed"):
		return true
	var id: int = pad.get_device_id()
	_expect(pad.start_vibration(0.25, 0.75), "start_vibration failed")
	var rumble: Dictionary = _gi._test_get_last_rumble(id)
	_expect_near(rumble.low, 0.75, "strong magnitude drives the low-frequency motor")
	_expect_near(rumble.high, 0.25, "weak magnitude drives the high-frequency motor")
	_expect(pad.get_vibration_remaining_duration() == -1.0, "untimed vibration reports -1 remaining")
	_expect(pad.start_vibration(0.0, 0.0, 0.0, 0.3, 0.6), "trigger vibration failed")
	rumble = _gi._test_get_last_rumble(id)
	_expect_near(rumble.left_trigger, 0.3, "left impulse trigger")
	_expect_near(rumble.right_trigger, 0.6, "right impulse trigger")
	_gi._test_set_time_override_usec(1000000)
	_expect(pad.start_vibration(1.0, 1.0, 0.5), "timed vibration failed")
	_gi._test_set_time_override_usec(1499999)
	_gi._test_force_poll()
	_expect(pad.is_vibrating(), "stopped before the duration elapsed")
	_gi._test_set_time_override_usec(1500000)
	_gi._test_force_poll()
	_expect(not pad.is_vibrating(), "poll() did not stop the vibration when the duration elapsed")
	_expect_near(_gi._test_get_last_rumble(id).low, 0.0, "the stop reached the device")
	_gi._test_set_time_override_usec(-1)
	_gi._test_remove_device(id)
	_gi._test_force_poll()
	_pass_detail("weak/strong motor mapping, impulse triggers and timed auto-stop")
	return true


func _check_mock_force_feedback() -> bool:
	if not _require_mock():
		return true
	var wheel = _mock_device("DEVICE_RACING_WHEEL", {"name": "FFB Wheel", "ffb_motors": [{}]})
	if not _expect(wheel != null, "wheel injection failed"):
		return true
	var constant := _c("GameInputForceFeedbackEffect", "EFFECT_CONSTANT")
	_expect(wheel.get_force_feedback_motor_count() == 1, "one force-feedback motor")
	_expect(wheel.get_force_feedback_motor_info(0).get("supported_effects", []).has(constant),
			"motor 0 does not list the constant effect")
	var effect = wheel.create_force_feedback_effect(0, {"kind": constant, "magnitude": 0.5, "sustain_duration": 0.25})
	if not _expect(effect != null, "create_force_feedback_effect returned null"):
		return true
	_expect(effect.is_valid(), "the new effect is not valid")
	_expect(effect.get_state() == _c("GameInputForceFeedbackEffect", "STATE_STOPPED"), "effects start stopped")
	effect.start()
	_expect(effect.get_state() == _c("GameInputForceFeedbackEffect", "STATE_RUNNING"), "start() did not run the effect")
	effect.set_gain(0.5)
	_expect_near(effect.get_gain(), 0.5, "effect gain")
	_expect_near(effect.get_params().get("sustain_duration", 0.0), 0.25, "sustain duration round trip")
	effect.stop()
	_expect(effect.get_state() == _c("GameInputForceFeedbackEffect", "STATE_STOPPED"), "stop() did not stop the effect")
	effect.release()
	_expect(not effect.is_valid(), "release() left the effect valid")
	_expect(_gi._test_get_effect_count() == 0, "a released effect is still registered")
	_pass_detail("constant effect created, started, re-gained, stopped and released")
	return true


func _check_mock_restore() -> bool:
	if not _has_seams or _mock_saved.is_empty():
		_skip("the mock session never started")
		return true
	_gi.shutdown()
	_gi.set_reading_callback_kinds(_mock_saved.callback_kinds)
	_gi.set_focus_policy(_mock_saved.focus_policy)
	_mock_ready = false
	if not _mock_saved.initialized:
		_expect(not _gi.is_initialized(), "the runtime should stay shut down, as it was before the mock session")
		_pass_detail("runtime left shut down, as it was before the mock session")
		return true
	_expect(_gi.initialize(), "re-initializing native GameInput failed")
	_expect(_gi._test_get_backend() == 1, "the native backend is not active again")
	if _native_device_count >= 0:
		var deadline := Time.get_ticks_msec() + 2000
		var count := -1
		while Time.get_ticks_msec() < deadline:
			await _frames(2)
			count = _gi.get_devices(_k("DEVICE_ANY")).size()
			if count >= _native_device_count:
				break
		_note("devices_after_restore", count)
		_expect(count >= _native_device_count, "%d device(s) came back after re-initializing, expected %d" % [
				count, _native_device_count])
	_pass_detail("native GameInput re-initialized and its devices re-enumerated")
	return true
