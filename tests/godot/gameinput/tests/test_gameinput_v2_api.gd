extends "res://addons/godot_gdk_tests/gameinput_test_base.gd"
## GameInput v2 — API surface contract (issue #97).
##
## Pure reflection plus the uninitialized soft-fail path, so it runs on every
## host (release builds included). Pins:
##   * enum values that mirror gameinput.h bit-for-bit (DeviceKind, Button,
##     FocusPolicy, RumbleMotor, ...), because scripts combine them as masks;
##   * the v2 signal argument shapes;
##   * the two new project settings and their registered defaults;
##   * GameInputForceFeedbackEffect registration and bare-wrapper defaults;
##   * the static helpers on GameInputDevice.


func after_each() -> void:
	var gi = get_gameinput()
	if gi != null:
		gi.shutdown()


func _c(cls: String, constant: String) -> int:
	return ClassDB.class_get_integer_constant(cls, constant)


func _assert_constants(cls: String, expected: Dictionary) -> void:
	for constant in expected:
		assert_true(ClassDB.class_has_integer_constant(cls, constant),
				"%s.%s is bound" % [cls, constant])
		assert_eq(_c(cls, constant), expected[constant], "%s.%s value" % [cls, constant])


func _signal_args(obj: Object, signal_name: String) -> Array:
	for s in obj.get_signal_list():
		if s["name"] == signal_name:
			return s["args"].map(func(a): return a["name"])
	return []


# ── Enums ────────────────────────────────────────────────────────────────

func test_device_kind_values() -> void:
	_assert_constants("GameInput", {
		"DEVICE_UNKNOWN": 0,
		"DEVICE_GAMEPAD": 1,
		"DEVICE_KEYBOARD": 2,
		"DEVICE_MOUSE": 4,
		"DEVICE_ALL": 7,
		"DEVICE_ARCADE_STICK": 8,
		"DEVICE_FLIGHT_STICK": 16,
		"DEVICE_RACING_WHEEL": 32,
		"DEVICE_SENSORS": 64,
		"DEVICE_CONTROLLER": 128,
		"DEVICE_ANY": 255,
	})


func test_focus_policy_values_match_gameinput_h() -> void:
	_assert_constants("GameInput", {
		"FOCUS_POLICY_DEFAULT": 0,
		"FOCUS_POLICY_EXCLUSIVE_FOREGROUND_INPUT": 0x2,
		"FOCUS_POLICY_EXCLUSIVE_FOREGROUND_GUIDE_BUTTON": 0x8,
		"FOCUS_POLICY_EXCLUSIVE_FOREGROUND_SHARE_BUTTON": 0x20,
		"FOCUS_POLICY_ENABLE_BACKGROUND_INPUT": 0x40,
		"FOCUS_POLICY_ENABLE_BACKGROUND_GUIDE_BUTTON": 0x80,
		"FOCUS_POLICY_ENABLE_BACKGROUND_SHARE_BUTTON": 0x100,
	})


func test_button_bits_cover_gameinput_gamepad_buttons() -> void:
	var names := [
		"BUTTON_MENU", "BUTTON_VIEW", "BUTTON_A", "BUTTON_B", "BUTTON_X", "BUTTON_Y",
		"BUTTON_DPAD_UP", "BUTTON_DPAD_DOWN", "BUTTON_DPAD_LEFT", "BUTTON_DPAD_RIGHT",
		"BUTTON_LEFT_SHOULDER", "BUTTON_RIGHT_SHOULDER", "BUTTON_LEFT_THUMB",
		"BUTTON_RIGHT_THUMB", "BUTTON_C", "BUTTON_Z", "BUTTON_LEFT_TRIGGER",
		"BUTTON_RIGHT_TRIGGER", "BUTTON_LEFT_STICK_UP", "BUTTON_LEFT_STICK_DOWN",
		"BUTTON_LEFT_STICK_LEFT", "BUTTON_LEFT_STICK_RIGHT", "BUTTON_RIGHT_STICK_UP",
		"BUTTON_RIGHT_STICK_DOWN", "BUTTON_RIGHT_STICK_LEFT", "BUTTON_RIGHT_STICK_RIGHT",
		"BUTTON_PADDLE_LEFT_1", "BUTTON_PADDLE_LEFT_2", "BUTTON_PADDLE_RIGHT_1",
		"BUTTON_PADDLE_RIGHT_2",
	]
	var expected := {"BUTTON_NONE": 0}
	for i in names.size():
		expected[names[i]] = 1 << i
	_assert_constants("GameInputDevice", expected)


func test_axis_values() -> void:
	_assert_constants("GameInputDevice", {
		"AXIS_LEFT_X": 0, "AXIS_LEFT_Y": 1, "AXIS_RIGHT_X": 2, "AXIS_RIGHT_Y": 3,
		"AXIS_LEFT_TRIGGER": 4, "AXIS_RIGHT_TRIGGER": 5, "AXIS_WHEEL": 6,
		"AXIS_THROTTLE": 7, "AXIS_BRAKE": 8, "AXIS_CLUTCH": 9, "AXIS_HANDBRAKE": 10,
		"AXIS_FLIGHT_ROLL": 11, "AXIS_FLIGHT_PITCH": 12, "AXIS_FLIGHT_YAW": 13,
		"AXIS_FLIGHT_THROTTLE": 14,
	})


func test_source_ranges() -> void:
	_assert_constants("GameInputDevice", {
		"SRC_BTN_MENU": 0, "SRC_BTN_A": 2, "SRC_BTN_RIGHT_THUMB": 13, "SRC_BTN_C": 14,
		"SRC_BTN_PADDLE_RIGHT_2": 29, "SRC_AXIS_LEFT_X": 100, "SRC_AXIS_RIGHT_TRIGGER": 105,
		"SRC_AXIS_WHEEL": 106, "SRC_AXIS_FLIGHT_THROTTLE": 114, "SRC_ARCADE_MENU": 200,
		"SRC_ARCADE_SPECIAL_2": 213, "SRC_FLIGHT_MENU": 300, "SRC_FLIGHT_RIGHT_SHOULDER": 313,
		"SRC_WHEEL_MENU": 400, "SRC_WHEEL_RIGHT_THUMB": 413,
	})
	# Every gamepad Button bit index is its Source id.
	for bit in 30:
		assert_eq(GameInputDevice.button_to_source(1 << bit), bit,
				"button_to_source(1 << %d) == %d" % [bit, bit])
	for axis in 15:
		assert_eq(GameInputDevice.axis_to_source(axis), 100 + axis,
				"axis_to_source(%d) == %d" % [axis, 100 + axis])


func test_specialty_button_enums_are_contiguous_bits() -> void:
	var families := {
		"ARCADE_STICK_": ["MENU", "VIEW", "UP", "DOWN", "LEFT", "RIGHT", "ACTION_1",
				"ACTION_2", "ACTION_3", "ACTION_4", "ACTION_5", "ACTION_6", "SPECIAL_1",
				"SPECIAL_2"],
		"FLIGHT_STICK_": ["MENU", "VIEW", "FIRE_PRIMARY", "FIRE_SECONDARY", "HAT_UP",
				"HAT_DOWN", "HAT_LEFT", "HAT_RIGHT", "A", "B", "X", "Y", "LEFT_SHOULDER",
				"RIGHT_SHOULDER"],
		"RACING_WHEEL_": ["MENU", "VIEW", "PREVIOUS_GEAR", "NEXT_GEAR", "DPAD_UP",
				"DPAD_DOWN", "DPAD_LEFT", "DPAD_RIGHT", "A", "B", "X", "Y", "LEFT_THUMB",
				"RIGHT_THUMB"],
	}
	for prefix in families:
		var expected := {prefix + "NONE": 0}
		var list: Array = families[prefix]
		for i in list.size():
			expected[prefix + list[i]] = 1 << i
		_assert_constants("GameInputDevice", expected)


func test_device_metadata_enums() -> void:
	_assert_constants("GameInputDevice", {
		"MOUSE_LEFT": 1, "MOUSE_RIGHT": 2, "MOUSE_MIDDLE": 4, "MOUSE_XBUTTON1": 8,
		"MOUSE_XBUTTON2": 16, "MOUSE_WHEEL_TILT_LEFT": 32, "MOUSE_WHEEL_TILT_RIGHT": 64,
		"SENSOR_ACCELEROMETER": 1, "SENSOR_GYROMETER": 2, "SENSOR_COMPASS": 4,
		"SENSOR_ORIENTATION": 8, "SENSOR_ACCURACY_HIGH": 3,
		"FAMILY_VIRTUAL": -1, "FAMILY_UNKNOWN": 0, "FAMILY_XBOX_ONE": 1,
		"FAMILY_XBOX_360": 2, "FAMILY_HID": 3, "FAMILY_I8042": 4, "FAMILY_AGGREGATE": 5,
		"STATUS_NONE": 0, "STATUS_CONNECTED": 0x1, "STATUS_HAPTIC_INFO_READY": 0x200000,
		"SYSTEM_BUTTON_GUIDE": 1, "SYSTEM_BUTTON_SHARE": 2,
		"SWITCH_CENTER": 0, "SWITCH_UP": 1, "SWITCH_RIGHT": 3, "SWITCH_DOWN": 5,
		"SWITCH_LEFT": 7, "SWITCH_UP_LEFT": 8,
		"KEYBOARD_UNKNOWN": -1, "KEYBOARD_ANSI": 0, "KEYBOARD_ISO": 1, "KEYBOARD_JIS": 4,
		"RUMBLE_LOW_FREQUENCY": 1, "RUMBLE_HIGH_FREQUENCY": 2, "RUMBLE_LEFT_TRIGGER": 4,
		"RUMBLE_RIGHT_TRIGGER": 8,
	})


func test_mapper_kind_flags_share_device_kind_bits() -> void:
	_assert_constants("GameInputMapper", {
		"KIND_GAMEPAD": _c("GameInput", "DEVICE_GAMEPAD"),
		"KIND_KEYBOARD": _c("GameInput", "DEVICE_KEYBOARD"),
		"KIND_MOUSE": _c("GameInput", "DEVICE_MOUSE"),
		"KIND_ARCADE_STICK": _c("GameInput", "DEVICE_ARCADE_STICK"),
		"KIND_FLIGHT_STICK": _c("GameInput", "DEVICE_FLIGHT_STICK"),
		"KIND_RACING_WHEEL": _c("GameInput", "DEVICE_RACING_WHEEL"),
	})


# ── Force feedback class ─────────────────────────────────────────────────

func test_force_feedback_effect_class_registered() -> void:
	assert_true(ClassDB.class_exists("GameInputForceFeedbackEffect"),
			"GameInputForceFeedbackEffect registered")
	assert_true(ClassDB.is_parent_class("GameInputForceFeedbackEffect", "RefCounted"),
			"GameInputForceFeedbackEffect extends RefCounted")
	_assert_constants("GameInputForceFeedbackEffect", {
		"EFFECT_CONSTANT": 0, "EFFECT_RAMP": 1, "EFFECT_SINE_WAVE": 2,
		"EFFECT_SQUARE_WAVE": 3, "EFFECT_TRIANGLE_WAVE": 4, "EFFECT_SAWTOOTH_UP": 5,
		"EFFECT_SAWTOOTH_DOWN": 6, "EFFECT_SPRING": 7, "EFFECT_FRICTION": 8,
		"EFFECT_DAMPER": 9, "EFFECT_INERTIA": 10,
		"STATE_STOPPED": 0, "STATE_RUNNING": 1, "STATE_PAUSED": 2,
		"FEEDBACK_AXIS_LINEAR_X": 1, "FEEDBACK_AXIS_LINEAR_Y": 2,
		"FEEDBACK_AXIS_LINEAR_Z": 4, "FEEDBACK_AXIS_ANGULAR_X": 8,
		"FEEDBACK_AXIS_ANGULAR_Y": 16, "FEEDBACK_AXIS_ANGULAR_Z": 32,
		"FEEDBACK_AXIS_NORMAL": 64,
	})


func test_bare_force_feedback_effect_is_inert() -> void:
	var fx = ClassDB.instantiate("GameInputForceFeedbackEffect")
	assert_not_null(fx, "bare effect instantiates")
	assert_false(fx.is_valid(), "bare effect is not valid")
	assert_eq(fx.get_device_id(), 0, "bare effect has no device")
	assert_eq(fx.get_motor_index(), -1, "bare effect has no motor")
	assert_eq(fx.get_kind(), -1, "bare effect has no kind")
	assert_false(fx.start(), "start() on a bare effect fails")
	assert_false(fx.stop(), "stop() on a bare effect fails")
	assert_false(fx.pause(), "pause() on a bare effect fails")
	assert_eq(fx.get_state(), _c("GameInputForceFeedbackEffect", "STATE_STOPPED"),
			"bare effect reports STATE_STOPPED")
	assert_false(fx.set_gain(0.5), "set_gain() on a bare effect fails")
	assert_eq_approx(fx.get_gain(), 0.0, "bare effect gain")
	assert_eq(fx.get_params(), {}, "bare effect params are empty")
	assert_false(fx.set_params({"magnitude": 1.0}), "set_params() on a bare effect fails")
	fx.release()
	assert_false(fx.is_valid(), "release() on a bare effect is a no-op")


# ── Singleton surface ────────────────────────────────────────────────────

func test_v2_methods_exist() -> void:
	if pending_unless_runtime_available():
		return
	var gi = get_gameinput()
	for m in ["get_current_timestamp", "set_reading_callback_kinds",
			"get_reading_callback_kinds", "get_buffered_readings", "get_dropped_reading_count",
			"set_focus_policy", "get_focus_policy", "create_aggregate_device",
			"disable_aggregate_device"]:
		assert_has_method_named(gi, m)


func test_signal_shapes() -> void:
	if pending_unless_runtime_available():
		return
	var gi = get_gameinput()
	assert_eq(_signal_args(gi, "device_connected"), ["device"], "device_connected args")
	assert_eq(_signal_args(gi, "device_disconnected"), ["device_id"], "device_disconnected args")
	assert_eq(_signal_args(gi, "device_status_changed"),
			["device", "status", "previous_status", "timestamp"], "device_status_changed args")
	assert_eq(_signal_args(gi, "reading_received"), ["device", "reading"],
			"reading_received args")
	assert_eq(_signal_args(gi, "system_buttons_changed"),
			["device", "buttons", "previous_buttons", "timestamp"], "system_buttons_changed args")
	assert_eq(_signal_args(gi, "keyboard_layout_changed"),
			["device", "layout", "previous_layout", "timestamp"], "keyboard_layout_changed args")


func test_v2_settings_registered_with_defaults() -> void:
	for setting in ["game_input/runtime/reading_callback_kinds", "game_input/runtime/focus_policy"]:
		assert_true(ProjectSettings.has_setting(setting), "%s is registered" % setting)
		assert_eq(get_setting_default(setting), 0, "%s defaults to 0" % setting)
		var found := false
		for p in ProjectSettings.get_property_list():
			if p["name"] == setting:
				found = true
				assert_eq(p["type"], TYPE_INT, "%s is an int" % setting)
				assert_eq(p["hint"], PROPERTY_HINT_FLAGS, "%s uses a flags hint" % setting)
		assert_true(found, "%s has property info" % setting)


func test_new_singleton_methods_soft_fail_before_initialize() -> void:
	if pending_unless_runtime_available():
		return
	var gi = get_gameinput()
	gi.shutdown()
	assert_eq(gi.get_current_timestamp(), 0, "get_current_timestamp() == 0 before init")
	assert_eq(gi.get_buffered_readings(null), [], "get_buffered_readings(null) == []")
	assert_eq(gi.get_dropped_reading_count(), 0, "no readings dropped before init")
	assert_eq(gi.create_aggregate_device(_c("GameInput", "DEVICE_GAMEPAD")), "",
			"create_aggregate_device() == \"\" before init")
	assert_false(gi.disable_aggregate_device("00"), "disable_aggregate_device() fails before init")
	assert_eq(gi.get_devices(_c("GameInput", "DEVICE_ANY")), [], "no devices before init")


func test_reading_callback_kinds_masks_and_persists_before_initialize() -> void:
	if pending_unless_runtime_available():
		return
	var gi = get_gameinput()
	gi.shutdown()
	assert_true(gi.set_reading_callback_kinds(_c("GameInput", "DEVICE_GAMEPAD")),
			"set_reading_callback_kinds() before init succeeds")
	assert_eq(gi.get_reading_callback_kinds(), _c("GameInput", "DEVICE_GAMEPAD"),
			"callback kinds stored before init")
	gi.set_reading_callback_kinds(0x1FF)
	assert_push_warning("ignored bits outside DEVICE_ANY")
	assert_eq(gi.get_reading_callback_kinds(), 0xFF, "bits outside DEVICE_ANY are dropped")
	gi.set_reading_callback_kinds(0)


func test_focus_policy_masks_unknown_bits() -> void:
	if pending_unless_runtime_available():
		return
	var gi = get_gameinput()
	var bg := _c("GameInput", "FOCUS_POLICY_ENABLE_BACKGROUND_INPUT")
	gi.set_focus_policy(bg)
	assert_eq(gi.get_focus_policy(), bg, "focus policy round-trips")
	gi.set_focus_policy(bg | 0x1)
	assert_push_warning("ignored unknown policy bits")
	assert_eq(gi.get_focus_policy(), bg, "unknown focus bits are dropped")
	gi.set_focus_policy(0)


# ── GameInputDevice surface ──────────────────────────────────────────────

func test_bare_device_v2_defaults() -> void:
	var device = ClassDB.instantiate("GameInputDevice")
	assert_eq(device.get_status(), 0, "bare device status")
	assert_eq(device.get_app_local_id(), "", "bare device app-local id")
	assert_eq(device.get_device_family(), _c("GameInputDevice", "FAMILY_UNKNOWN"),
			"bare device family")
	assert_eq(device.get_supported_rumble_motors(), 0, "bare device rumble motors")
	assert_eq(device.get_supported_system_buttons(), 0, "bare device system buttons")
	assert_eq(device.get_system_buttons(), 0, "bare device held system buttons")
	assert_eq(device.get_button_label(_c("GameInputDevice", "SRC_BTN_A")), "",
			"bare device label")
	assert_eq(device.get_haptic_info(), {}, "bare device haptic info")
	assert_false(device.is_vibrating(), "bare device is not vibrating")
	assert_eq(device.get_vibration_strength(), Vector2.ZERO, "bare device vibration strength")
	assert_eq(device.get_trigger_vibration_strength(), Vector2.ZERO,
			"bare device trigger vibration strength")
	assert_eq_approx(device.get_vibration_duration(), 0.0, "bare device vibration duration")
	assert_eq_approx(device.get_vibration_remaining_duration(), 0.0,
			"bare device remaining vibration")
	assert_eq(device.get_force_feedback_motor_count(), 0, "bare device has no FFB motors")
	assert_eq(device.get_force_feedback_motor_info(0), {}, "bare device motor info")
	assert_false(device.is_force_feedback_motor_powered_on(0), "bare device motor power")
	assert_false(device.set_force_feedback_motor_gain(0, 1.0), "bare device motor gain")
	assert_null(device.create_force_feedback_effect(0, {"kind": 0}), "bare device creates no effect")


func test_static_helpers() -> void:
	assert_eq(GameInputDevice.scan_code_to_physical_key(0x1E), KEY_A, "scan 0x1E -> KEY_A")
	assert_eq(GameInputDevice.scan_code_to_physical_key(0xE01C), KEY_KP_ENTER,
			"extended scan 0xE01C -> KEY_KP_ENTER")
	assert_eq(GameInputDevice.scan_code_to_physical_key(-1), 0, "negative scan code -> 0")
	assert_eq(GameInputDevice.virtual_key_to_keycode(0x41), KEY_A, "VK 'A' -> KEY_A")
	assert_eq(GameInputDevice.virtual_key_to_keycode(0x0D), KEY_ENTER, "VK_RETURN -> KEY_ENTER")
	assert_eq(GameInputDevice.virtual_key_to_keycode(0x100), 0, "VK out of range -> 0")
	assert_eq(GameInputDevice.switch_position_to_vector(_c("GameInputDevice", "SWITCH_UP")),
			Vector2(0, -1), "SWITCH_UP points up in Godot's y-down space")
	assert_eq(GameInputDevice.switch_position_to_vector(_c("GameInputDevice", "SWITCH_RIGHT")),
			Vector2(1, 0), "SWITCH_RIGHT")
	var diag: Vector2 = GameInputDevice.switch_position_to_vector(
			_c("GameInputDevice", "SWITCH_DOWN_LEFT"))
	assert_eq_approx(diag.length(), 1.0, "diagonal switch vector is normalized")
	assert_true(diag.x < 0.0 and diag.y > 0.0, "SWITCH_DOWN_LEFT points down-left")
	assert_eq(GameInputDevice.switch_position_to_vector(99), Vector2.ZERO,
			"unknown switch position -> zero")


func test_reading_v2_defaults() -> void:
	var r = ClassDB.instantiate("GameInputReading")
	assert_eq(r.get_input_kinds(), 0, "bare reading has no kinds")
	assert_false(r.has_previous(), "bare reading has no previous sample")
	assert_false(r.has_gap_before(), "bare reading has no gap")
	assert_false(r.is_truncated(), "bare reading is not truncated")
	assert_eq(r.get_device_id(), 0, "bare reading has no device")
	assert_false(r.is_source_down(_c("GameInputDevice", "SRC_BTN_A")), "source up")
	assert_eq_approx(r.get_source_value(_c("GameInputDevice", "SRC_AXIS_WHEEL")), 0.0,
			"source value 0")
	assert_eq(r.get_key_count(), 0, "no keys")
	assert_eq(r.get_pressed_physical_keys(), PackedInt64Array(), "no pressed keys")
	assert_eq(r.get_key_states(), [], "no key states")
	assert_eq(r.get_mouse_delta(), Vector2.ZERO, "no mouse delta")
	assert_eq(r.get_mouse_state(), {}, "no mouse state")
	assert_eq(r.get_accelerometer(), Vector3.ZERO, "no accelerometer")
	assert_eq(r.get_orientation(), Quaternion(), "identity orientation")
	assert_eq(r.get_controller_axes(), PackedFloat32Array(), "no raw axes")
	assert_eq(r.get_controller_buttons(), [], "no raw buttons")
	assert_eq(r.get_controller_switches(), PackedInt32Array(), "no raw switches")
	assert_eq(r.get_flight_stick_hat_vector(), Vector2.ZERO, "centered hat")
