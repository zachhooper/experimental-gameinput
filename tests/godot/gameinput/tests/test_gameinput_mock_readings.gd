extends "res://addons/godot_gdk_tests/gameinput_test_base.gd"
## GameInput v2 — polled readings for every typed input kind, via the mock.
##
## Mock state uses native GameInput conventions (stick up is +Y); readings
## must hand back Godot conventions (down is +Y) exactly like the native path,
## because both go through the same snapshot -> GameInputReading code.

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


# ── Gamepad ──────────────────────────────────────────────────────────────

func test_first_reading_has_no_previous_and_held_counts_as_pressed() -> void:
	var pad = _device("DEVICE_GAMEPAD")
	if pad == null:
		return
	assert_null(_gi.get_current_reading(pad), "no reading before any input")
	var r = mock_reading(_gi, pad, {"gamepad": {"buttons": _d("BUTTON_A")}})
	assert_not_null(r, "reading after the first push")
	assert_false(r.has_previous(), "first reading has no previous sample")
	assert_true(r.is_button_down(_d("BUTTON_A")), "A down")
	assert_true(r.was_button_pressed(_d("BUTTON_A")), "held without a previous counts as pressed (v1)")
	assert_false(r.was_button_released(_d("BUTTON_A")), "no release without a previous sample")
	assert_eq(r.get_device_id(), pad.get_device_id(), "reading carries the device id")


func test_edges_across_polls() -> void:
	var pad = _device("DEVICE_GAMEPAD")
	if pad == null:
		return
	var a := _d("BUTTON_A")
	mock_reading(_gi, pad, {"gamepad": {"buttons": 0}})
	var pressed = mock_reading(_gi, pad, {"gamepad": {"buttons": a}})
	assert_true(pressed.has_previous(), "second reading has a previous sample")
	assert_true(pressed.was_button_pressed(a), "0 -> A is a press")
	_gi._test_force_poll()
	var held = _gi.get_current_reading(pad)
	assert_true(held.is_button_down(a), "A still held")
	assert_false(held.was_button_pressed(a), "no second press while held")
	var released = mock_reading(_gi, pad, {"gamepad": {"buttons": 0}})
	assert_true(released.was_button_released(a), "A -> 0 is a release")
	assert_false(released.is_button_down(a), "A up")


func test_masks_are_chords_and_fail_closed() -> void:
	var pad = _device("DEVICE_GAMEPAD")
	if pad == null:
		return
	var lb := _d("BUTTON_LEFT_SHOULDER")
	var rb := _d("BUTTON_RIGHT_SHOULDER")
	var r = mock_reading(_gi, pad, {"gamepad": {"buttons": lb}})
	assert_true(r.is_button_down(lb), "LB alone")
	assert_false(r.is_button_down(lb | rb), "LB|RB needs both")
	r = mock_reading(_gi, pad, {"gamepad": {"buttons": lb | rb}})
	assert_true(r.is_button_down(lb | rb), "LB|RB chord down")
	assert_true(r.was_button_pressed(lb | rb), "chord completes on this reading")
	assert_false(r.is_button_down(0), "an empty mask is never down")
	assert_false(r.is_button_down(lb | (1 << 30)), "bits outside the Button range fail closed")
	assert_eq(r.get_buttons_mask(), lb | rb, "raw mask")


func test_v2_gamepad_buttons_round_trip() -> void:
	var pad = _device("DEVICE_GAMEPAD")
	if pad == null:
		return
	for name in ["BUTTON_C", "BUTTON_Z", "BUTTON_LEFT_TRIGGER", "BUTTON_RIGHT_TRIGGER",
			"BUTTON_LEFT_STICK_UP", "BUTTON_RIGHT_STICK_RIGHT", "BUTTON_PADDLE_LEFT_1",
			"BUTTON_PADDLE_LEFT_2", "BUTTON_PADDLE_RIGHT_1", "BUTTON_PADDLE_RIGHT_2"]:
		var bit := _d(name)
		var r = mock_reading(_gi, pad, {"gamepad": {"buttons": bit}})
		assert_true(r.is_button_down(bit), "%s down" % name)
		var src: int = GameInputDevice.button_to_source(bit)
		assert_true(r.is_source_down(src), "%s source down" % name)
		assert_eq(r.get_source_value(src), 1.0, "%s source value" % name)


func test_axes_use_godot_conventions() -> void:
	var pad = _device("DEVICE_GAMEPAD")
	if pad == null:
		return
	var r = mock_reading(_gi, pad, {"gamepad": {
		"left_x": 0.25, "left_y": 0.75, "right_x": -0.5, "right_y": -1.0,
		"left_trigger": 0.4, "right_trigger": 1.0,
	}})
	assert_eq_approx(r.get_axis(_d("AXIS_LEFT_X")), 0.25, "left x")
	assert_eq_approx(r.get_axis(_d("AXIS_LEFT_Y")), -0.75, "stick up reads as -Y (Godot)")
	assert_eq_approx(r.get_axis(_d("AXIS_RIGHT_X")), -0.5, "right x")
	assert_eq_approx(r.get_axis(_d("AXIS_RIGHT_Y")), 1.0, "stick down reads as +Y (Godot)")
	assert_eq_approx(r.get_axis(_d("AXIS_LEFT_TRIGGER")), 0.4, "left trigger")
	assert_eq_approx(r.get_axis(_d("AXIS_RIGHT_TRIGGER")), 1.0, "right trigger")
	assert_eq_approx(r.get_axis(99), 0.0, "unknown axis reads 0")
	var clamped = mock_reading(_gi, pad, {"gamepad": {"left_x": 3.0, "left_trigger": -2.0}})
	assert_eq_approx(clamped.get_axis(_d("AXIS_LEFT_X")), 1.0, "stick values clamp to 1")
	assert_eq_approx(clamped.get_axis(_d("AXIS_LEFT_TRIGGER")), 0.0, "trigger values clamp to 0")


func test_axis_sources_use_half_travel_threshold() -> void:
	var pad = _device("DEVICE_GAMEPAD")
	if pad == null:
		return
	var src := _d("SRC_AXIS_LEFT_TRIGGER")
	var r = mock_reading(_gi, pad, {"gamepad": {"left_trigger": 0.49}})
	assert_false(r.is_source_down(src), "0.49 is below the 0.5 source threshold")
	assert_eq_approx(r.get_source_value(src), 0.49, "source value is the axis value")
	r = mock_reading(_gi, pad, {"gamepad": {"left_trigger": 0.5}})
	assert_true(r.is_source_down(src), "0.5 counts as down")
	assert_true(r.was_source_pressed(src), "crossing the threshold is a press")
	r = mock_reading(_gi, pad, {"gamepad": {"left_y": -0.8}})
	assert_true(r.is_source_down(_d("SRC_AXIS_LEFT_Y")), "|axis| >= 0.5 in either direction")
	assert_true(r.was_source_released(src), "trigger released below threshold")
	assert_eq_approx(r.get_source_value(_d("SRC_AXIS_LEFT_Y")), 0.8,
			"source value uses the Godot sign (stick down is +)")


func test_sources_ignore_kinds_the_reading_does_not_carry() -> void:
	var pad = _device("DEVICE_GAMEPAD")
	if pad == null:
		return
	var r = mock_reading(_gi, pad, {"gamepad": {"buttons": _d("BUTTON_A")}})
	assert_false(r.is_source_down(_d("SRC_WHEEL_A")), "a gamepad reading has no wheel sources")
	assert_eq(r.get_source_value(_d("SRC_AXIS_WHEEL")), 0.0, "wheel axis source reads 0")
	assert_false(r.is_source_down(9999), "unknown source is never down")


# ── Arcade stick, flight stick, racing wheel ────────────────────────────

func test_arcade_stick() -> void:
	var stick = _device("DEVICE_ARCADE_STICK")
	if stick == null:
		return
	var action := _d("ARCADE_STICK_ACTION_1") | _d("ARCADE_STICK_UP")
	var r = mock_reading(_gi, stick, {"arcade_stick": {"buttons": action}})
	assert_eq(r.get_input_kinds(), _k("DEVICE_ARCADE_STICK"), "arcade reading kind")
	assert_eq(r.get_arcade_stick_buttons(), action, "arcade buttons")
	assert_true(r.is_source_down(_d("SRC_ARCADE_ACTION_1")), "action 1 source")
	assert_true(r.is_source_down(_d("SRC_ARCADE_UP")), "up source")
	assert_false(r.is_source_down(_d("SRC_ARCADE_SPECIAL_2")), "special 2 up")
	assert_false(r.is_button_down(_d("BUTTON_A")), "no gamepad buttons on an arcade stick")
	var r2 = mock_reading(_gi, stick, {"arcade_stick": {"buttons": 0}})
	assert_true(r2.was_source_released(_d("SRC_ARCADE_ACTION_1")), "action 1 release")


func test_flight_stick() -> void:
	var stick = _device("DEVICE_FLIGHT_STICK")
	if stick == null:
		return
	var r = mock_reading(_gi, stick, {"flight_stick": {
		"buttons": _d("FLIGHT_STICK_FIRE_PRIMARY"), "hat": _d("SWITCH_UP_RIGHT"),
		"roll": -0.5, "pitch": 0.25, "yaw": 0.75, "throttle": 0.6,
	}})
	assert_eq(r.get_flight_stick_buttons(), _d("FLIGHT_STICK_FIRE_PRIMARY"), "buttons")
	assert_true(r.is_source_down(_d("SRC_FLIGHT_FIRE_PRIMARY")), "fire source")
	assert_eq(r.get_flight_stick_hat(), _d("SWITCH_UP_RIGHT"), "hat position")
	var hat: Vector2 = r.get_flight_stick_hat_vector()
	assert_eq_approx(hat.x, sqrt(0.5), "hat vector x")
	assert_eq_approx(hat.y, -sqrt(0.5), "hat vector is y-down (up is -Y)")
	assert_eq_approx(r.get_axis(_d("AXIS_FLIGHT_ROLL")), -0.5, "roll")
	assert_eq_approx(r.get_axis(_d("AXIS_FLIGHT_PITCH")), 0.25, "pitch keeps the native sign")
	assert_eq_approx(r.get_axis(_d("AXIS_FLIGHT_YAW")), 0.75, "yaw")
	assert_eq_approx(r.get_axis(_d("AXIS_FLIGHT_THROTTLE")), 0.6, "throttle")
	assert_true(r.is_source_down(_d("SRC_AXIS_FLIGHT_YAW")), "yaw axis source")


func test_racing_wheel() -> void:
	var wheel = _device("DEVICE_RACING_WHEEL", {"has_clutch": true, "max_wheel_angle": 900.0})
	if wheel == null:
		return
	var r = mock_reading(_gi, wheel, {"racing_wheel": {
		"buttons": _d("RACING_WHEEL_NEXT_GEAR"), "gear": 3, "wheel": -0.25,
		"throttle": 0.9, "brake": 0.1, "clutch": 0.5, "handbrake": 1.0,
	}})
	assert_eq(r.get_racing_wheel_buttons(), _d("RACING_WHEEL_NEXT_GEAR"), "wheel buttons")
	assert_true(r.is_source_down(_d("SRC_WHEEL_NEXT_GEAR")), "next-gear source")
	assert_eq(r.get_racing_wheel_gear(), 3, "gear")
	assert_eq_approx(r.get_axis(_d("AXIS_WHEEL")), -0.25, "wheel")
	assert_eq_approx(r.get_axis(_d("AXIS_THROTTLE")), 0.9, "throttle")
	assert_eq_approx(r.get_axis(_d("AXIS_BRAKE")), 0.1, "brake")
	assert_eq_approx(r.get_axis(_d("AXIS_CLUTCH")), 0.5, "clutch")
	assert_eq_approx(r.get_axis(_d("AXIS_HANDBRAKE")), 1.0, "handbrake")
	assert_true(r.is_source_down(_d("SRC_AXIS_THROTTLE")), "throttle source")
	assert_false(r.is_source_down(_d("SRC_AXIS_BRAKE")), "light brake is not down")
	var info: Dictionary = wheel.get_device_info()["racing_wheel"]
	assert_true(info["has_clutch"], "racing wheel info has_clutch")
	assert_eq_approx(info["max_wheel_angle"], 900.0, "racing wheel info max angle")


# ── Sensors and raw controller ───────────────────────────────────────────

func test_sensors_convert_units() -> void:
	var device = _device("DEVICE_SENSORS")
	if device == null:
		return
	var q := Quaternion(Vector3.UP, PI / 2.0)
	var r = mock_reading(_gi, device, {"sensors": {
		"acceleration_g": Vector3(0.0, -1.0, 0.5),
		"angular_velocity": Vector3(0.1, 0.2, 0.3),
		"orientation": q, "heading": 270.0, "heading_accuracy": 3,
	}})
	assert_eq(r.get_sensor_kinds(), 0xF, "all four sensors")
	var accel: Vector3 = r.get_accelerometer()
	assert_eq_approx(accel.y, -9.80665, "1 g is standard gravity in m/s^2")
	assert_eq_approx(accel.z, 4.903325, "0.5 g")
	var gyro: Vector3 = r.get_gyroscope()
	assert_eq_approx(gyro.z, 0.3, "gyroscope stays in rad/s")
	var o: Quaternion = r.get_orientation()
	assert_eq_approx(o.y, q.y, "orientation y")
	assert_eq_approx(o.w, q.w, "orientation w")
	assert_eq_approx(r.get_heading_degrees(), 270.0, "heading")
	assert_eq(r.get_heading_accuracy(), 3, "heading accuracy")


func test_raw_controller_arrays_and_truncation() -> void:
	var device = _device("DEVICE_CONTROLLER")
	if device == null:
		return
	var r = mock_reading(_gi, device, {"controller": {
		"axes": [0.5, -0.5, 1.0], "buttons": [true, false, 1], "switches": [_d("SWITCH_LEFT")],
	}})
	assert_eq(r.get_input_kinds(), _k("DEVICE_CONTROLLER"), "controller reading kind")
	assert_eq(r.get_controller_axes().size(), 3, "three axes")
	assert_eq_approx(r.get_controller_axis(1), -0.5, "axis 1")
	assert_eq_approx(r.get_controller_axis(7), 0.0, "axis out of range reads 0")
	assert_eq(r.get_controller_buttons(), [true, false, true], "buttons as bools")
	assert_true(r.is_controller_button_down(2), "button 2 down")
	assert_true(r.was_controller_button_pressed(0), "first reading counts held buttons as pressed")
	assert_eq(r.get_controller_switches(), PackedInt32Array([_d("SWITCH_LEFT")]), "switches")
	assert_eq(r.get_controller_switch(5), 0, "switch out of range reads center")
	assert_false(r.is_truncated(), "small arrays are not truncated")
	var r2 = mock_reading(_gi, device, {"controller": {"buttons": [false, false, true]}})
	assert_true(r2.was_controller_button_released(0), "button 0 released")
	assert_false(r2.was_controller_button_pressed(2), "button 2 still held")
	var many := []
	many.resize(200)
	many.fill(true)
	var r3 = mock_reading(_gi, device, {"controller": {"buttons": many}})
	assert_eq(r3.get_controller_buttons().size(), 128, "buttons capped at 128")
	assert_true(r3.is_truncated(), "reading reports truncation")


# ── Timestamps, kinds and schema errors ─────────────────────────────────

func test_timestamps_and_input_kinds() -> void:
	var device = _device("DEVICE_GAMEPAD",
			{"kind_mask": _k("DEVICE_GAMEPAD") | _k("DEVICE_ARCADE_STICK")})
	if device == null:
		return
	_gi._test_push_reading(device.get_device_id(), {"timestamp": 1000, "gamepad": {"buttons": 0}})
	var r = mock_reading(_gi, device, {"timestamp": 2500, "arcade_stick": {"buttons": 1}})
	assert_eq(r.get_input_kinds(), _k("DEVICE_GAMEPAD") | _k("DEVICE_ARCADE_STICK"),
			"polled reading merges every kind the device reported")
	assert_eq(r.get_kind_timestamp(_k("DEVICE_GAMEPAD")), 1000, "gamepad timestamp")
	assert_eq(r.get_kind_timestamp(_k("DEVICE_ARCADE_STICK")), 2500, "arcade timestamp")
	assert_eq(r.get_timestamp(), 2500, "reading timestamp is the newest kind")
	assert_eq(r.get_kind_timestamp(_k("DEVICE_MOUSE")), 0, "absent kind has no timestamp")


func test_sections_for_unsupported_kinds_are_ignored() -> void:
	var pad = _device("DEVICE_GAMEPAD")
	if pad == null:
		return
	assert_false(_gi._test_push_reading(pad.get_device_id(), {"mouse": {"buttons": 1}}),
			"a gamepad cannot report mouse input")
	_gi._test_force_poll()
	assert_null(_gi.get_current_reading(pad), "nothing was recorded")


func test_invalid_reading_sections_push_errors() -> void:
	var pad = _device("DEVICE_GAMEPAD")
	if pad == null:
		return
	assert_false(_gi._test_push_reading(pad.get_device_id(), {"gamepda": {}}), "typo rejected")
	assert_push_error("unknown mock reading section 'gamepda'")
	assert_false(_gi._test_push_reading(pad.get_device_id(), {"gamepad": 5}), "non-dict rejected")
	assert_push_error("'gamepad' must be a Dictionary")
	assert_false(_gi._test_push_reading(pad.get_device_id() + 1000, {"gamepad": {}}),
			"unknown device rejected")
	assert_push_error("is not connected")
