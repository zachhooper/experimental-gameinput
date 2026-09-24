extends "res://addons/godot_gdk_tests/gameinput_test_base.gd"
## GameInput v2 — live runtime checks (Tier=live_read).
##
## Skipped without LIVE_TESTS=1 (`tools\run_all_tests.ps1 -Live`). They need
## the GameInput runtime and, for the device checks, a connected controller;
## a ViGEm virtual pad started by tools\virtual_gamepad\vpad_driver.py counts.
## Button and stick readings are deliberately not asserted: a locked or
## remote session receives no GameInput readings at all, so input delivery is
## covered by the mock suites and by the manual checklist instead.

var _gi = null


func after_each() -> void:
	if _gi != null:
		_gi.set_reading_callback_kinds(0)
		_gi.set_focus_policy(0)
		_gi.shutdown()
	_gi = null


func _k(constant: String) -> int:
	return ClassDB.class_get_integer_constant("GameInput", constant)


func _d(constant: String) -> int:
	return ClassDB.class_get_integer_constant("GameInputDevice", constant)


func _fx(constant: String) -> int:
	return ClassDB.class_get_integer_constant("GameInputForceFeedbackEffect", constant)


func _start_native() -> bool:
	if pending_unless_live():
		return false
	if pending_unless_runtime_available():
		return false
	var gi = get_gameinput()
	gi.shutdown()
	if not gi.initialize():
		pending("GameInput.initialize() returned false (no GameInput runtime on this host)")
		return false
	_gi = gi
	if gi.has_method("_test_get_backend"):
		assert_eq(gi._test_get_backend(), 1, "the native backend is active")
	return true


func _settle() -> void:
	_gi.poll()
	await get_tree().process_frame
	_gi.poll()


func _rumble_pad():
	for device in _gi.get_devices(_k("DEVICE_ANY")):
		if device.get_supported_rumble_motors() != 0:
			return device
	return null


func test_live_timestamp_is_monotonic() -> void:
	if not _start_native():
		return
	var t0: int = _gi.get_current_timestamp()
	assert_gt(t0, 0, "GetCurrentTimestamp() returns microseconds")
	await get_tree().create_timer(0.02).timeout
	assert_gt(_gi.get_current_timestamp(), t0, "the clock advances")


func test_live_device_info_is_consistent() -> void:
	if not _start_native():
		return
	await _settle()
	var devices: Array = _gi.get_devices(_k("DEVICE_ANY"))
	if devices.is_empty():
		pending("No live GameInput devices are connected")
		return
	var hex := RegEx.create_from_string("^[0-9a-f]{64}$")
	for device in devices:
		var info: Dictionary = device.get_device_info()
		var label := "%s (%04X:%04X)" % [info.get("name", "?"), info.get("vendor_id", 0), info.get("product_id", 0)]
		for key in ["name", "vendor_id", "product_id", "device_family", "supported_input_kinds",
				"status", "app_local_id", "supported_rumble_motors", "supported_system_buttons",
				"force_feedback_motor_count", "is_mock"]:
			assert_true(info.has(key), "%s: info has '%s'" % [label, key])
		assert_false(info["is_mock"], "%s: native device" % label)
		assert_true((device.get_status() & _d("STATUS_CONNECTED")) != 0, "%s: connected status bit" % label)
		assert_not_null(hex.search(device.get_app_local_id()), "%s: 64-hex app-local id" % label)
		assert_eq(device.get_supported_rumble_motors(), info["supported_rumble_motors"], "%s: rumble motors" % label)
		assert_eq(device.supports_vibration(), device.get_supported_rumble_motors() != 0,
				"%s: supports_vibration() follows the motors" % label)
		assert_eq(device.get_force_feedback_motor_count(), info["force_feedback_motor_count"],
				"%s: force feedback motor count" % label)
		for motor in device.get_force_feedback_motor_count():
			var motor_info: Dictionary = device.get_force_feedback_motor_info(motor)
			assert_true(motor_info.has("supported_effects"), "%s: motor %d info" % [label, motor])
		assert_typeof(device.get_haptic_info(), TYPE_DICTIONARY, "%s: haptic info" % label)
		gut.p("live device: %s kinds=0x%X family=%d rumble=0x%X ffb=%d" % [label,
				info["supported_input_kinds"], info["device_family"],
				info["supported_rumble_motors"], info["force_feedback_motor_count"]])


func test_live_reading_callbacks_register_and_unregister() -> void:
	if not _start_native():
		return
	var kinds := _k("DEVICE_GAMEPAD") | _k("DEVICE_KEYBOARD") | _k("DEVICE_MOUSE")
	assert_true(_gi.set_reading_callback_kinds(kinds), "RegisterReadingCallback succeeds")
	assert_eq(_gi.get_reading_callback_kinds(), kinds, "kinds stored")
	for i in 3:
		await _settle()
	assert_true(_gi.set_reading_callback_kinds(0), "unregistering succeeds")
	assert_eq(_gi.get_reading_callback_kinds(), 0, "callbacks off")


func test_live_focus_policy_applies_while_running() -> void:
	if not _start_native():
		return
	var policy := _k("FOCUS_POLICY_ENABLE_BACKGROUND_INPUT")
	_gi.set_focus_policy(policy)
	assert_eq(_gi.get_focus_policy(), policy, "SetFocusPolicy applied")
	_gi.set_focus_policy(0)
	assert_eq(_gi.get_focus_policy(), 0, "restored")


func test_live_vibration_duration_auto_stops() -> void:
	if not _start_native():
		return
	await _settle()
	var pad = _rumble_pad()
	if pad == null:
		pending("No live GameInput device with rumble motors is connected")
		return
	assert_true(pad.start_vibration(0.25, 0.5, 0.15), "SetRumbleState accepted")
	assert_true(pad.is_vibrating(), "vibrating")
	assert_eq(pad.get_vibration_strength(), Vector2(0.25, 0.5), "weak/strong reported back")
	await get_tree().create_timer(0.3).timeout
	_gi.poll()
	assert_false(pad.is_vibrating(), "poll() stops the rumble after its duration")
	pad.stop_vibration()


func test_live_force_feedback_round_trip() -> void:
	if not _start_native():
		return
	await _settle()
	var target = null
	for device in _gi.get_devices(_k("DEVICE_ANY")):
		if device.get_force_feedback_motor_count() > 0:
			target = device
			break
	if target == null:
		pending("No live GameInput device with force feedback motors is connected")
		return
	var motor_info: Dictionary = target.get_force_feedback_motor_info(0)
	var kinds: Array = motor_info["supported_effects"]
	if kinds.is_empty():
		pending("Motor 0 reports no supported effect kinds")
		return
	var effect = target.create_force_feedback_effect(0, {"kind": kinds[0]})
	assert_not_null(effect, "CreateForceFeedbackEffect succeeds")
	if effect == null:
		return
	assert_true(effect.set_gain(0.2), "gain")
	assert_true(effect.start(), "start")
	await get_tree().create_timer(0.05).timeout
	assert_true(effect.stop(), "stop")
	effect.release()
	assert_false(effect.is_valid(), "released")


func test_live_aggregate_device_round_trip() -> void:
	if not _start_native():
		return
	var id: String = _gi.create_aggregate_device(_k("DEVICE_GAMEPAD"))
	if id.is_empty():
		pending("CreateAggregateDevice is not available on this GameInput runtime")
		return
	assert_eq(id.length(), 64, "aggregate app-local id")
	assert_true(_gi.disable_aggregate_device(id), "DisableAggregateDevice succeeds")
