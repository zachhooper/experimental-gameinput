extends "res://addons/godot_gdk_tests/gameinput_test_base.gd"
## GameInput v2 — rumble, timed vibration and haptic info through the mock.
##
## `GameInputDevice.start_vibration(weak, strong, duration)` follows Godot's
## `Input.start_joy_vibration()`: weak drives the high-frequency motor, strong
## the low-frequency motor, and a positive duration stops the rumble from
## `poll()` once it elapses. Time is pinned with the debug clock override so
## the auto-stop is deterministic.

const ALL_MOTORS := 0xF

var _gi = null


func after_each() -> void:
	end_mock_session(_gi if _gi != null else get_gameinput())
	_gi = null


func _d(constant: String) -> int:
	return ClassDB.class_get_integer_constant("GameInputDevice", constant)


func _fx(constant: String) -> int:
	return ClassDB.class_get_integer_constant("GameInputForceFeedbackEffect", constant)


func _pad(info: Dictionary = {"rumble_motors": ALL_MOTORS}):
	_gi = begin_mock_session()
	if _gi == null:
		return null
	return add_mock_device(_gi, info)


func _rumble(device) -> Dictionary:
	return _gi._test_get_last_rumble(device.get_device_id())


func test_weak_and_strong_map_like_godot_joypads() -> void:
	var pad = _pad()
	if pad == null:
		return
	assert_true(pad.start_vibration(0.25, 0.75), "vibration starts")
	var sent := _rumble(pad)
	assert_eq_approx(sent["low"], 0.75, "strong magnitude drives the low-frequency motor")
	assert_eq_approx(sent["high"], 0.25, "weak magnitude drives the high-frequency motor")
	assert_eq(pad.get_vibration_strength(), Vector2(0.25, 0.75),
			"get_vibration_strength() is (weak, strong) like Input.get_joy_vibration_strength()")
	assert_true(pad.is_vibrating(), "is_vibrating()")
	assert_eq(pad.get_vibration_duration(), 0.0, "no duration means indefinite")
	assert_eq(pad.get_vibration_remaining_duration(), -1.0, "indefinite rumble reports -1")
	pad.stop_vibration()
	assert_false(pad.is_vibrating(), "stopped")
	assert_eq(pad.get_vibration_remaining_duration(), 0.0, "stopped rumble reports 0")
	assert_eq_approx(_rumble(pad)["low"], 0.0, "motors zeroed")


func test_duration_auto_stops_in_poll() -> void:
	var pad = _pad()
	if pad == null:
		return
	_gi._test_set_time_override_usec(1000000)
	assert_true(pad.start_vibration(1.0, 1.0, 0.5), "timed vibration")
	assert_eq_approx(pad.get_vibration_duration(), 0.5, "duration")
	assert_eq(_rumble(pad)["end_usec"], 1500000, "ends 0.5 s after start")
	_gi._test_set_time_override_usec(1200000)
	assert_eq_approx(pad.get_vibration_remaining_duration(), 0.3, "remaining duration")
	_gi._test_set_time_override_usec(1499999)
	_gi._test_force_poll()
	assert_true(pad.is_vibrating(), "still vibrating 1 us before the end")
	var applies: int = _rumble(pad)["apply_count"]
	_gi._test_set_time_override_usec(1500000)
	_gi._test_force_poll()
	assert_false(pad.is_vibrating(), "poll() stops the rumble when the duration elapses")
	assert_eq(_rumble(pad)["apply_count"], applies + 1, "one stop was sent to the device")
	assert_eq_approx(_rumble(pad)["high"], 0.0, "motors zeroed")
	assert_eq(pad.get_vibration_duration(), 0.0, "duration cleared")


func test_restart_replaces_the_timer() -> void:
	var pad = _pad()
	if pad == null:
		return
	_gi._test_set_time_override_usec(0)
	pad.start_vibration(0.5, 0.5, 0.1)
	pad.start_vibration(0.5, 0.5, 0.0)
	_gi._test_set_time_override_usec(1000000)
	_gi._test_force_poll()
	assert_true(pad.is_vibrating(), "an indefinite restart cancels the earlier timer")
	pad.start_vibration(0.5, 0.5, INF)
	assert_eq(pad.get_vibration_remaining_duration(), -1.0, "INF duration is indefinite")


func test_values_are_clamped_and_zero_is_not_vibrating() -> void:
	var pad = _pad()
	if pad == null:
		return
	pad.start_vibration(2.0, -1.0)
	assert_eq(pad.get_vibration_strength(), Vector2(1.0, 0.0), "magnitudes clamp to 0..1")
	assert_true(pad.start_vibration(0.0, 0.0, 5.0), "zero magnitudes are accepted")
	assert_false(pad.is_vibrating(), "zero magnitudes are not vibrating")
	assert_eq(_rumble(pad)["end_usec"], 0, "no timer for a silent rumble")


func test_trigger_rumble() -> void:
	var pad = _pad()
	if pad == null:
		return
	pad.start_vibration(0.0, 0.0, 0.0, 0.3, 0.7)
	assert_eq_approx(pad.get_trigger_vibration_strength().x, 0.3, "left trigger")
	assert_eq_approx(pad.get_trigger_vibration_strength().y, 0.7, "right trigger")
	assert_eq_approx(_rumble(pad)["right_trigger"], 0.7, "right trigger sent")
	assert_true(pad.is_vibrating(), "trigger-only rumble counts as vibrating")


func test_motors_the_device_lacks_are_zeroed() -> void:
	var pad = _pad({"rumble_motors": _d("RUMBLE_LOW_FREQUENCY")})
	if pad == null:
		return
	assert_eq(pad.get_supported_rumble_motors(), _d("RUMBLE_LOW_FREQUENCY"), "one motor")
	pad.start_vibration(1.0, 1.0, 0.0, 1.0, 1.0)
	assert_eq(pad.get_vibration_strength(), Vector2(0.0, 1.0), "weak motor absent -> 0")
	assert_eq(pad.get_trigger_vibration_strength(), Vector2.ZERO, "no trigger motors")


func test_device_without_rumble() -> void:
	var pad = _pad({})
	if pad == null:
		return
	assert_false(pad.supports_vibration(), "no rumble motors")
	assert_false(pad.start_vibration(1.0, 1.0), "start_vibration() returns false")
	assert_false(_gi.set_vibration(pad, 1.0, 1.0), "set_vibration() returns false")
	assert_false(pad.is_vibrating(), "not vibrating")


func test_v1_set_vibration_clears_the_timer() -> void:
	var pad = _pad()
	if pad == null:
		return
	_gi._test_set_time_override_usec(0)
	pad.start_vibration(0.2, 0.2, 1.0)
	assert_true(_gi.set_vibration(pad, 0.6, 0.4), "v1 set_vibration(low, high)")
	assert_eq(pad.get_vibration_duration(), 0.0, "set_vibration() is indefinite")
	assert_eq(pad.get_vibration_strength(), Vector2(0.4, 0.6),
			"set_vibration(low, high) maps low -> strong, high -> weak")
	_gi._test_set_time_override_usec(5000000)
	_gi._test_force_poll()
	assert_true(pad.is_vibrating(), "no auto-stop after set_vibration()")


func test_stop_haptics_stops_rumble_and_force_feedback() -> void:
	var pad = _pad({"rumble_motors": ALL_MOTORS, "ffb_motors": [{}]})
	if pad == null:
		return
	var effect = pad.create_force_feedback_effect(0, {"kind": _fx("EFFECT_CONSTANT"), "magnitude": 0.5})
	assert_not_null(effect, "effect created")
	assert_true(effect.start(), "effect running")
	pad.start_vibration(1.0, 1.0)
	_gi.stop_haptics(pad)
	assert_false(pad.is_vibrating(), "rumble stopped")
	assert_eq(effect.get_state(), _fx("STATE_STOPPED"), "force feedback stopped")
	assert_true(effect.is_valid(), "the effect itself survives stop_haptics()")


func test_haptic_info() -> void:
	var pad = _pad({"haptic_locations": ["grip_left", "trigger_right"],
			"haptic_audio_endpoint_id": "{0.0.0.00000000}.{mock}",
			"status": _d("STATUS_HAPTIC_INFO_READY")})
	if pad == null:
		return
	assert_true(pad.supports_haptics(), "haptic locations mean haptics")
	var info: Dictionary = pad.get_haptic_info()
	for key in ["ready", "supported", "audio_endpoint_id", "locations", "location_guids"]:
		assert_true(info.has(key), "haptic info has '%s'" % key)
	assert_true(info["ready"], "status bit HAPTIC_INFO_READY")
	assert_true(info["supported"], "supported")
	assert_eq(info["locations"], PackedStringArray(["grip_left", "trigger_right"]), "locations")
	assert_eq(info["audio_endpoint_id"], "{0.0.0.00000000}.{mock}", "audio endpoint")


func test_haptic_info_without_haptics() -> void:
	var pad = _pad({})
	if pad == null:
		return
	assert_false(pad.supports_haptics(), "no haptics")
	var info: Dictionary = pad.get_haptic_info()
	assert_false(info["supported"], "not supported")
	assert_false(info["ready"], "not ready")
	assert_eq(info["locations"].size(), 0, "no locations")
