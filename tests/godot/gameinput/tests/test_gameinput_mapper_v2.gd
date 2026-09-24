extends "res://addons/godot_gdk_tests/gameinput_test_base.gd"
## GameInput v2 — GameInputMapper with the new sources: racing-wheel axes,
## arcade-stick and paddle buttons, and explicit target devices that are not
## gamepads. The mapper is driven synchronously with NOTIFICATION_PROCESS
## after each mock reading, which is exactly what its _process() does.

var _gi = null
var _mapper = null
var _actions: Array[StringName] = []


func after_each() -> void:
	if _mapper != null:
		_mapper.free()
		_mapper = null
	for action in _actions:
		Input.action_release(action)
		if InputMap.has_action(action):
			InputMap.erase_action(action)
	_actions.clear()
	end_mock_session(_gi if _gi != null else get_gameinput())
	_gi = null


func _k(constant: String) -> int:
	return ClassDB.class_get_integer_constant("GameInput", constant)


func _d(constant: String) -> int:
	return ClassDB.class_get_integer_constant("GameInputDevice", constant)


func _m(constant: String) -> int:
	return ClassDB.class_get_integer_constant("GameInputMapper", constant)


func _action(action: StringName, events: Array = []) -> void:
	if not InputMap.has_action(action):
		InputMap.add_action(action)
		_actions.append(action)
	for ev in events:
		InputMap.action_add_event(action, ev)
	Input.action_release(action)


func _binding(action: StringName, source: int, is_axis := false):
	var b = ClassDB.instantiate("GameInputBinding")
	b.set("action", action)
	b.set("source", source)
	b.set("is_axis", is_axis)
	return b


func _new_mapper(bindings: Array):
	var mapper = ClassDB.instantiate("GameInputMapper")
	if not mapper.has_method("_test_native_handles_binding"):
		mapper.free()
		pending("GameInputMapper test hooks are debug-only")
		return null
	var map = ClassDB.instantiate("GameInputActionMap")
	map.set("bindings", bindings)
	mapper.set("action_map", map)
	_mapper = mapper
	return mapper


func _joy_button(index: int) -> InputEventJoypadButton:
	var ev := InputEventJoypadButton.new()
	ev.button_index = index
	return ev


func _joy_motion(axis: int, value: float) -> InputEventJoypadMotion:
	var ev := InputEventJoypadMotion.new()
	ev.axis = axis
	ev.axis_value = value
	return ev


func _tick() -> void:
	_mapper.notification(Node.NOTIFICATION_PROCESS)


func test_kind_flags_cover_specialty_devices() -> void:
	assert_eq(_m("KIND_GAMEPAD"), _k("DEVICE_GAMEPAD"), "gamepad flag matches DeviceKind")
	assert_eq(_m("KIND_ARCADE_STICK"), _k("DEVICE_ARCADE_STICK"), "arcade stick flag matches DeviceKind")
	assert_eq(_m("KIND_FLIGHT_STICK"), _k("DEVICE_FLIGHT_STICK"), "flight stick flag matches DeviceKind")
	assert_eq(_m("KIND_RACING_WHEEL"), _k("DEVICE_RACING_WHEEL"), "racing wheel flag matches DeviceKind")


func test_paddles_dedupe_against_godot_paddle_buttons() -> void:
	# Godot's JoyButton 16..19 follow SDL: PADDLE1 = right upper (P1),
	# PADDLE2 = left upper (P3), PADDLE3 = right lower (P2), PADDLE4 = left lower (P4).
	var cases := [
		["SRC_BTN_PADDLE_RIGHT_1", 16],
		["SRC_BTN_PADDLE_LEFT_1", 17],
		["SRC_BTN_PADDLE_RIGHT_2", 18],
		["SRC_BTN_PADDLE_LEFT_2", 19],
	]
	if _new_mapper([]) == null:
		return
	for c in cases:
		var action := StringName("test_gi_v2_%s" % String(c[0]).to_lower())
		_action(action, [_joy_button(c[1])])
		var b = _binding(action, _d(c[0]))
		assert_true(_mapper._test_native_handles_binding(b),
				"%s is already delivered by Godot as JoyButton %d" % [c[0], c[1]])
		var other := StringName("%s_other" % action)
		_action(other, [_joy_button(c[1] + 4)])
		assert_false(_mapper._test_native_handles_binding(_binding(other, _d(c[0]))),
				"%s does not match JoyButton %d" % [c[0], c[1] + 4])
	var trigger := &"test_gi_v2_trigger_button"
	_action(trigger, [_joy_button(16)])
	assert_false(_mapper._test_native_handles_binding(_binding(trigger, _d("SRC_BTN_LEFT_TRIGGER"))),
			"a trigger button does not match a joypad button")


func test_trigger_and_stick_direction_buttons_dedupe_against_godot_axes() -> void:
	# GameInput reports the trigger buttons and stick directions as buttons;
	# Godot delivers the same controls as joypad axis motion, Y down-positive.
	var cases := [
		["SRC_BTN_LEFT_TRIGGER", JOY_AXIS_TRIGGER_LEFT, 1.0],
		["SRC_BTN_RIGHT_TRIGGER", JOY_AXIS_TRIGGER_RIGHT, 1.0],
		["SRC_BTN_LEFT_STICK_UP", JOY_AXIS_LEFT_Y, -1.0],
		["SRC_BTN_LEFT_STICK_DOWN", JOY_AXIS_LEFT_Y, 1.0],
		["SRC_BTN_LEFT_STICK_LEFT", JOY_AXIS_LEFT_X, -1.0],
		["SRC_BTN_LEFT_STICK_RIGHT", JOY_AXIS_LEFT_X, 1.0],
		["SRC_BTN_RIGHT_STICK_UP", JOY_AXIS_RIGHT_Y, -1.0],
		["SRC_BTN_RIGHT_STICK_DOWN", JOY_AXIS_RIGHT_Y, 1.0],
		["SRC_BTN_RIGHT_STICK_LEFT", JOY_AXIS_RIGHT_X, -1.0],
		["SRC_BTN_RIGHT_STICK_RIGHT", JOY_AXIS_RIGHT_X, 1.0],
	]
	if _new_mapper([]) == null:
		return
	for c in cases:
		var action := StringName("test_gi_v2_%s" % String(c[0]).to_lower())
		_action(action, [_joy_motion(c[1], c[2])])
		assert_true(_mapper._test_native_handles_binding(_binding(action, _d(c[0]))),
				"%s is already delivered by Godot as axis %d, direction %+.0f" % [c[0], c[1], c[2]])
		var opposite := StringName("%s_opposite" % action)
		_action(opposite, [_joy_motion(c[1], -c[2])])
		assert_false(_mapper._test_native_handles_binding(_binding(opposite, _d(c[0]))),
				"%s does not match axis %d in the other direction" % [c[0], c[1]])
		var other_axis := StringName("%s_other_axis" % action)
		_action(other_axis, [_joy_motion((c[1] + 1) % 6, c[2])])
		assert_false(_mapper._test_native_handles_binding(_binding(other_axis, _d(c[0]))),
				"%s does not match another axis" % c[0])


func test_racing_wheel_axis_drives_an_action() -> void:
	_gi = begin_mock_session()
	if _gi == null:
		return
	var action := &"test_gi_v2_steer_right"
	_action(action)
	var b = _binding(action, _d("SRC_AXIS_WHEEL"), true)
	if _new_mapper([b]) == null:
		return
	_mapper.set("target_kind_mask", _m("KIND_RACING_WHEEL"))
	var wheel = add_mock_device(_gi, {"kind_mask": _k("DEVICE_RACING_WHEEL")})
	push_mock_reading(_gi, wheel, {"racing_wheel": {"wheel": 0.8}})
	_tick()
	assert_true(Input.is_action_pressed(action), "wheel past the threshold presses")
	assert_almost_eq(Input.get_action_strength(action), 0.75, 0.001,
			"strength rescales past the 0.2 deadzone")
	push_mock_reading(_gi, wheel, {"racing_wheel": {"wheel": -0.8}})
	_tick()
	assert_false(Input.is_action_pressed(action), "the other direction releases")
	b.set("axis_invert", true)
	push_mock_reading(_gi, wheel, {"racing_wheel": {"wheel": -0.9}})
	_tick()
	assert_true(Input.is_action_pressed(action), "axis_invert binds the left direction")


func test_racing_wheel_pedal_and_button() -> void:
	_gi = begin_mock_session()
	if _gi == null:
		return
	var gas := &"test_gi_v2_gas"
	var shift := &"test_gi_v2_shift_up"
	_action(gas)
	_action(shift)
	if _new_mapper([_binding(gas, _d("SRC_AXIS_THROTTLE"), true),
			_binding(shift, _d("SRC_WHEEL_NEXT_GEAR"))]) == null:
		return
	_mapper.set("target_kind_mask", _m("KIND_RACING_WHEEL"))
	var wheel = add_mock_device(_gi, {"kind_mask": _k("DEVICE_RACING_WHEEL")})
	push_mock_reading(_gi, wheel, {"racing_wheel": {
		"throttle": 1.0, "buttons": _d("RACING_WHEEL_NEXT_GEAR")}})
	_tick()
	assert_true(Input.is_action_pressed(gas), "throttle pedal")
	assert_almost_eq(Input.get_action_strength(gas), 1.0, 0.001, "full throttle")
	assert_true(Input.is_action_pressed(shift), "next-gear paddle")
	assert_eq(_mapper.get_active_binding_count(), 2, "two held bindings")
	push_mock_reading(_gi, wheel, {"racing_wheel": {}})
	_tick()
	assert_false(Input.is_action_pressed(gas), "pedal released")
	assert_false(Input.is_action_pressed(shift), "paddle released")


func test_target_device_id_reaches_specialty_only_devices() -> void:
	_gi = begin_mock_session()
	if _gi == null:
		return
	var punch := &"test_gi_v2_punch"
	_action(punch)
	if _new_mapper([_binding(punch, _d("SRC_ARCADE_ACTION_1"))]) == null:
		return
	var pad = add_mock_device(_gi, {"kind_mask": _k("DEVICE_GAMEPAD")})
	var stick = add_mock_device(_gi, {"kind_mask": _k("DEVICE_ARCADE_STICK")})
	assert_not_null(pad, "a gamepad is also connected")
	_mapper.set("target_device_id", stick.get_device_id())
	push_mock_reading(_gi, stick, {"arcade_stick": {"buttons": _d("ARCADE_STICK_ACTION_1")}})
	_tick()
	assert_true(Input.is_action_pressed(punch),
			"an arcade-stick-only device is found by id even though DEVICE_ALL excludes it")
	_gi._test_remove_device(stick.get_device_id())
	_gi._test_force_poll()
	_tick()
	assert_false(Input.is_action_pressed(punch), "disconnecting the target releases held actions")
	assert_eq(_mapper.get_active_binding_count(), 0, "nothing held")


func test_target_kind_mask_picks_the_primary_specialty_device() -> void:
	_gi = begin_mock_session()
	if _gi == null:
		return
	var fire := &"test_gi_v2_fire"
	_action(fire)
	if _new_mapper([_binding(fire, _d("SRC_FLIGHT_FIRE_PRIMARY"))]) == null:
		return
	add_mock_device(_gi, {"kind_mask": _k("DEVICE_GAMEPAD")})
	var hotas = add_mock_device(_gi, {"kind_mask": _k("DEVICE_FLIGHT_STICK")})
	_mapper.set("target_kind_mask", _m("KIND_FLIGHT_STICK"))
	push_mock_reading(_gi, hotas, {"flight_stick": {"buttons": _d("FLIGHT_STICK_FIRE_PRIMARY")}})
	_tick()
	assert_true(Input.is_action_pressed(fire), "flight stick trigger drives the action")
	_mapper.set("target_kind_mask", _m("KIND_GAMEPAD"))
	_tick()
	assert_false(Input.is_action_pressed(fire), "retargeting to gamepads releases it")


func test_gamepad_bindings_ignore_other_kinds() -> void:
	_gi = begin_mock_session()
	if _gi == null:
		return
	var jump := &"test_gi_v2_jump"
	_action(jump)
	if _new_mapper([_binding(jump, _d("SRC_BTN_A"))]) == null:
		return
	var wheel = add_mock_device(_gi, {"kind_mask": _k("DEVICE_RACING_WHEEL")})
	_mapper.set("target_device_id", wheel.get_device_id())
	push_mock_reading(_gi, wheel, {"racing_wheel": {"buttons": 0x3FFF}})
	_tick()
	assert_false(Input.is_action_pressed(jump), "gamepad sources read as up on a wheel")


func test_specialty_sources_are_never_suppressed() -> void:
	# Godot has no standard joypad mapping for arcade stick, flight stick or
	# racing wheel controls, so the mapper keeps its own events for them even
	# when the action also lists every SDL joypad button and axis direction.
	if _new_mapper([]) == null:
		return
	var action := &"test_gi_v2_every_joypad_event"
	var events: Array = []
	for i in range(JOY_BUTTON_SDL_MAX):
		events.append(_joy_button(i))
	for axis in range(JOY_AXIS_SDL_MAX):
		events.append(_joy_motion(axis, 1.0))
		events.append(_joy_motion(axis, -1.0))
	_action(action, events)
	assert_true(_mapper._test_native_handles_binding(_binding(action, _d("SRC_BTN_A"))),
			"control: a gamepad button on the same action is suppressed")
	assert_true(_mapper._test_native_handles_binding(_binding(action, _d("SRC_AXIS_LEFT_X"), true)),
			"control: a gamepad axis on the same action is suppressed")
	var buttons := 0
	for first in [_d("SRC_ARCADE_MENU"), _d("SRC_FLIGHT_MENU"), _d("SRC_WHEEL_MENU")]:
		for source in range(first, first + 14):
			buttons += 1
			assert_false(_mapper._test_native_handles_binding(_binding(action, source)),
					"specialty button source %d is not suppressed" % source)
	assert_eq(buttons, 42, "every arcade, flight and wheel button source was checked")
	for source in range(_d("SRC_AXIS_WHEEL"), _d("SRC_AXIS_FLIGHT_THROTTLE") + 1):
		for invert in [false, true]:
			var b = _binding(action, source, true)
			b.set("axis_invert", invert)
			assert_false(_mapper._test_native_handles_binding(b),
					"specialty axis source %d (invert %s) is not suppressed" % [source, invert])
