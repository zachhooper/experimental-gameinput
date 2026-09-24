extends "res://addons/godot_gdk_tests/gameinput_test_base.gd"
## GameInput v2 — keyboard and mouse readings through the mock backend.
##
## Keyboard state is a set of held keys identified by scan code. Readings map
## them to Godot physical keys (layout independent, the same space as
## InputEventKey.physical_keycode) and expose Windows virtual keys as Godot
## keycodes. Mouse positions and wheel counts accumulate on the device;
## readings turn them into per-poll deltas.

var _gi = null


func after_each() -> void:
	end_mock_session(_gi if _gi != null else get_gameinput())
	_gi = null


func _k(constant: String) -> int:
	return ClassDB.class_get_integer_constant("GameInput", constant)


func _d(constant: String) -> int:
	return ClassDB.class_get_integer_constant("GameInputDevice", constant)


func _device(kind_constant: String, extra: Dictionary = {}):
	_gi = begin_mock_session()
	if _gi == null:
		return null
	var info := {"kind_mask": _k(kind_constant)}
	info.merge(extra, true)
	return add_mock_device(_gi, info)


# ── Keyboard ─────────────────────────────────────────────────────────────

func test_physical_keys_from_scan_codes() -> void:
	var kb = _device("DEVICE_KEYBOARD")
	if kb == null:
		return
	# W (0x11), A (0x1E), Left Shift (0x2A), Up arrow (extended 0xE048).
	var r = mock_reading(_gi, kb, {"keyboard": {"keys": [0x11, 0x1E, 0x2A, 0xE048]}})
	assert_eq(r.get_key_count(), 4, "four keys held")
	assert_true(r.is_physical_key_down(KEY_W), "W down")
	assert_true(r.is_physical_key_down(KEY_A), "A down")
	assert_true(r.is_physical_key_down(KEY_SHIFT), "Shift down")
	assert_true(r.is_physical_key_down(KEY_UP), "extended Up arrow down")
	assert_false(r.is_physical_key_down(KEY_S), "S up")
	assert_eq(r.get_pressed_physical_keys(), PackedInt64Array([KEY_W, KEY_A, KEY_SHIFT, KEY_UP]),
			"pressed keys in report order")
	assert_true(r.was_physical_key_pressed(KEY_W), "first reading counts held keys as pressed")


func test_key_edges() -> void:
	var kb = _device("DEVICE_KEYBOARD")
	if kb == null:
		return
	mock_reading(_gi, kb, {"keyboard": {"keys": []}})
	var r = mock_reading(_gi, kb, {"keyboard": {"keys": [0x39]}})
	assert_true(r.was_physical_key_pressed(KEY_SPACE), "space pressed")
	_gi._test_force_poll()
	assert_false(_gi.get_current_reading(kb).was_physical_key_pressed(KEY_SPACE),
			"space held is not pressed again")
	r = mock_reading(_gi, kb, {"keyboard": {"keys": []}})
	assert_true(r.was_physical_key_released(KEY_SPACE), "space released")
	assert_eq(r.get_key_count(), 0, "no keys held")
	assert_eq(r.get_pressed_physical_keys().size(), 0, "empty pressed list")


func test_left_and_right_modifiers_share_a_key_and_differ_by_location() -> void:
	var kb = _device("DEVICE_KEYBOARD")
	if kb == null:
		return
	var r = mock_reading(_gi, kb, {"keyboard": {"keys": [0x1D, 0xE01D, 0x36]}})
	assert_eq(r.get_pressed_physical_keys(), PackedInt64Array([KEY_CTRL, KEY_SHIFT]),
			"both Ctrl keys report one physical key")
	var states: Array = r.get_key_states()
	assert_eq(states.size(), 3, "one state per held scan code")
	assert_eq(states[0]["location"], KEY_LOCATION_LEFT, "0x1D is left Ctrl")
	assert_eq(states[1]["location"], KEY_LOCATION_RIGHT, "0xE01D is right Ctrl")
	assert_eq(states[2]["location"], KEY_LOCATION_RIGHT, "0x36 is right Shift")


func test_key_state_dictionaries() -> void:
	var kb = _device("DEVICE_KEYBOARD")
	if kb == null:
		return
	var r = mock_reading(_gi, kb, {"keyboard": {"keys": [
		{"scan_code": 0x10, "virtual_key": 0x41, "code_point": 0x61},
		{"scan_code": 0x1A, "virtual_key": 0xDD, "is_dead_key": true},
	]}})
	var states: Array = r.get_key_states()
	assert_eq(states.size(), 2, "two key states")
	var q: Dictionary = states[0]
	for key in ["scan_code", "virtual_key", "code_point", "is_dead_key", "physical_keycode",
			"keycode", "location"]:
		assert_true(q.has(key), "key state has '%s'" % key)
	# AZERTY-style: the physical Q position produces 'a'.
	assert_eq(q["physical_keycode"], KEY_Q, "physical key follows the scan code")
	assert_eq(q["keycode"], KEY_A, "keycode follows the virtual key")
	assert_eq(q["code_point"], 0x61, "code point")
	assert_false(q["is_dead_key"], "not a dead key")
	assert_true(states[1]["is_dead_key"], "dead key flag")


func test_pause_and_unknown_scan_codes() -> void:
	var kb = _device("DEVICE_KEYBOARD")
	if kb == null:
		return
	var r = mock_reading(_gi, kb, {"keyboard": {"keys": [0xE11D45, 0x7F]}})
	assert_true(r.is_physical_key_down(KEY_PAUSE), "three-byte Pause sequence")
	assert_eq(r.get_pressed_physical_keys(), PackedInt64Array([KEY_PAUSE]),
			"unmapped scan codes are skipped in the pressed list")
	assert_eq(r.get_key_states().size(), 2, "but still reported in key states")
	assert_eq(r.get_key_states()[1]["physical_keycode"], KEY_NONE, "unmapped -> KEY_NONE")


func test_many_keys_are_capped_and_flagged() -> void:
	var kb = _device("DEVICE_KEYBOARD")
	if kb == null:
		return
	var keys := []
	for code in range(0x02, 0x02 + 40):
		keys.append(code)
	var r = mock_reading(_gi, kb, {"keyboard": {"keys": keys}})
	assert_eq(r.get_key_count(), 32, "key storage is capped at 32")
	assert_true(r.is_truncated(), "truncation is reported")


func test_keyboard_info() -> void:
	var kb = _device("DEVICE_KEYBOARD", {"keyboard_kind": _d("KEYBOARD_ISO"),
			"keyboard_layout": 0x0409, "key_count": 105})
	if kb == null:
		return
	var info: Dictionary = kb.get_device_info()["keyboard"]
	assert_eq(info["kind"], _d("KEYBOARD_ISO"), "keyboard kind")
	assert_eq(info["layout"], 0x0409, "layout")
	assert_eq(info["key_count"], 105, "key count")
	assert_eq(kb.get_keyboard_layout(), 0x0409, "device layout accessor")


# ── Mouse ────────────────────────────────────────────────────────────────

func test_mouse_buttons_and_edges() -> void:
	var mouse = _device("DEVICE_MOUSE")
	if mouse == null:
		return
	var left := _d("MOUSE_LEFT")
	var right := _d("MOUSE_RIGHT")
	var r = mock_reading(_gi, mouse, {"mouse": {"buttons": left}})
	assert_true(r.is_mouse_button_down(left), "left down")
	assert_true(r.was_mouse_button_pressed(left), "first reading press")
	r = mock_reading(_gi, mouse, {"mouse": {"buttons": left | right}})
	assert_true(r.was_mouse_button_pressed(right), "right press")
	assert_false(r.was_mouse_button_pressed(left), "left held")
	assert_true(r.is_mouse_button_down(left | right), "chord")
	r = mock_reading(_gi, mouse, {"mouse": {"buttons": right}})
	assert_true(r.was_mouse_button_released(left), "left release")
	assert_eq(r.get_mouse_buttons(), right, "raw mouse buttons")
	assert_false(r.is_mouse_button_down(1 << 9), "unknown mouse bits fail closed")


func test_mouse_motion_and_wheel_deltas() -> void:
	var mouse = _device("DEVICE_MOUSE")
	if mouse == null:
		return
	var first = mock_reading(_gi, mouse, {"mouse": {"x": 100, "y": 50, "wheel_y": 120}})
	assert_eq(first.get_mouse_delta(), Vector2.ZERO, "no delta without a previous sample")
	var r = mock_reading(_gi, mouse, {"mouse": {"x": 110, "y": 45, "wheel_y": 240, "wheel_x": -120}})
	assert_eq(r.get_mouse_delta(), Vector2(10, -5), "motion delta")
	assert_eq(r.get_mouse_wheel_delta(), Vector2(-120, 120), "wheel delta (raw counts)")
	var state: Dictionary = r.get_mouse_state()
	for key in ["buttons", "positions", "x", "y", "absolute_x", "absolute_y", "wheel_x", "wheel_y"]:
		assert_true(state.has(key), "mouse state has '%s'" % key)
	assert_eq(state["x"], 110, "accumulated x")
	_gi._test_force_poll()
	assert_eq(_gi.get_current_reading(mouse).get_mouse_delta(), Vector2.ZERO,
			"a poll with no motion has no delta")


func test_mouse_delta_survives_int64_wrap() -> void:
	var mouse = _device("DEVICE_MOUSE")
	if mouse == null:
		return
	var near_max := 9223372036854775807 - 2
	mock_reading(_gi, mouse, {"mouse": {"x": near_max}})
	var r = mock_reading(_gi, mouse, {"mouse": {"x": -9223372036854775807 - 1 + 2}})
	assert_eq(r.get_mouse_delta().x, 5.0, "accumulator wrap yields the small true delta")


func test_mouse_absolute_position_needs_the_flag() -> void:
	var mouse = _device("DEVICE_MOUSE")
	if mouse == null:
		return
	var r = mock_reading(_gi, mouse, {"mouse": {"absolute_x": 640, "absolute_y": 360}})
	assert_false(r.has_mouse_absolute_position(), "relative-only by default")
	assert_eq(r.get_mouse_absolute_position(), Vector2.ZERO, "no absolute position")
	r = mock_reading(_gi, mouse, {"mouse": {"absolute_x": 640, "absolute_y": 360, "positions": 3}})
	assert_true(r.has_mouse_absolute_position(), "absolute flag set")
	assert_eq(r.get_mouse_absolute_position(), Vector2(640, 360), "absolute position")


func test_mouse_info() -> void:
	var mouse = _device("DEVICE_MOUSE", {"mouse_buttons": 0x7F, "has_wheel_x": true,
			"sample_rate": 1000})
	if mouse == null:
		return
	var info: Dictionary = mouse.get_device_info()["mouse"]
	assert_eq(info["supported_buttons"], 0x7F, "supported buttons")
	assert_true(info["has_wheel_x"], "horizontal wheel")
	assert_true(info["has_wheel_y"], "vertical wheel by default")
	assert_eq(info["sample_rate"], 1000, "sample rate")


func test_readings_of_other_kinds_are_empty() -> void:
	var kb = _device("DEVICE_KEYBOARD")
	if kb == null:
		return
	var r = mock_reading(_gi, kb, {"keyboard": {"keys": [0x1E]}})
	assert_eq(r.get_mouse_state(), {}, "keyboard reading has no mouse state")
	assert_eq(r.get_mouse_delta(), Vector2.ZERO, "no mouse delta")
	assert_eq(r.get_accelerometer(), Vector3.ZERO, "no sensors")
	assert_eq(r.get_controller_axes().size(), 0, "no raw axes")
