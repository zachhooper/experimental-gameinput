extends "res://addons/godot_gdk_tests/gameinput_test_base.gd"
## GameInput v2 — force feedback effects through the mock backend.
##
## Effects are described with one Dictionary per effect kind (durations in
## seconds, `sustain_duration < 0` = forever, a float `magnitude` applied to
## every axis the motor drives or a per-axis Dictionary). Validation errors
## are warnings plus a null/false return, so a typo never silently does
## nothing and never crashes a game.

var _gi = null
var _pad = null


func after_each() -> void:
	_pad = null
	end_mock_session(_gi if _gi != null else get_gameinput())
	_gi = null


func _fx(constant: String) -> int:
	return ClassDB.class_get_integer_constant("GameInputForceFeedbackEffect", constant)


# Motor 0 drives linear X+Y and supports constant, sine and spring effects.
# Motor 1 uses the mock defaults: linear X and every effect kind.
func _start(motors: Array = []) -> bool:
	_gi = begin_mock_session()
	if _gi == null:
		return false
	if motors.is_empty():
		var linear_xy := _fx("FEEDBACK_AXIS_LINEAR_X") | _fx("FEEDBACK_AXIS_LINEAR_Y")
		var effects := (1 << _fx("EFFECT_CONSTANT")) | (1 << _fx("EFFECT_SINE_WAVE")) \
				| (1 << _fx("EFFECT_SPRING"))
		motors = [{"axes": linear_xy, "effects": effects}, {}]
	_pad = add_mock_device(_gi, {"ffb_motors": motors})
	return _pad != null


func test_motor_info() -> void:
	if not _start():
		return
	assert_eq(_pad.get_force_feedback_motor_count(), 2, "two motors")
	var m0: Dictionary = _pad.get_force_feedback_motor_info(0)
	assert_eq(m0["supported_axes"], 3, "motor 0 axes")
	assert_eq(m0["supported_effects"], [0, 2, 7], "motor 0 effect kinds")
	var m1: Dictionary = _pad.get_force_feedback_motor_info(1)
	assert_eq(m1["supported_effects"].size(), 11, "motor 1 supports every kind")
	assert_eq(_pad.get_force_feedback_motor_info(2), {}, "out-of-range motor info is empty")
	assert_true(_pad.is_force_feedback_motor_powered_on(0), "powered by default")
	assert_false(_pad.is_force_feedback_motor_powered_on(5), "out-of-range motor is not powered")
	assert_true(_pad.set_force_feedback_motor_gain(1, 0.5), "motor gain accepted")
	assert_false(_pad.set_force_feedback_motor_gain(9, 0.5), "out-of-range motor gain rejected")
	assert_eq(_pad.get_device_info()["force_feedback_motor_count"], 2, "info motor count")


func test_unpowered_motor() -> void:
	_gi = begin_mock_session()
	if _gi == null:
		return
	var wheel = add_mock_device(_gi, {"ffb_motors": [{}], "ffb_powered": 0})
	assert_false(wheel.is_force_feedback_motor_powered_on(0), "mock ffb_powered = 0")


func test_constant_effect_defaults_and_axes() -> void:
	if not _start():
		return
	var effect = _pad.create_force_feedback_effect(0, {"kind": _fx("EFFECT_CONSTANT"), "magnitude": 0.5})
	assert_not_null(effect, "constant effect created")
	assert_true(effect.is_valid(), "valid")
	assert_eq(effect.get_kind(), _fx("EFFECT_CONSTANT"), "kind")
	assert_eq(effect.get_motor_index(), 0, "motor index")
	assert_eq(effect.get_device_id(), _pad.get_device_id(), "device id")
	assert_eq(effect.get_state(), _fx("STATE_STOPPED"), "effects start stopped")
	assert_eq_approx(effect.get_gain(), 1.0, "full gain by default")
	var p: Dictionary = effect.get_params()
	assert_eq(p["kind"], _fx("EFFECT_CONSTANT"), "params kind")
	assert_eq_approx(p["magnitude"]["linear_x"], 0.5, "float magnitude drives linear X")
	assert_eq_approx(p["magnitude"]["linear_y"], 0.5, "and linear Y (the motor's axes)")
	assert_eq_approx(p["magnitude"]["linear_z"], 0.0, "but not axes the motor lacks")
	assert_eq(p["sustain_duration"], -1.0, "sustains forever by default")
	assert_eq(p["attack_duration"], 0.0, "no attack by default")
	assert_eq_approx(p["sustain_gain"], 1.0, "sustain gain")
	assert_eq(p["play_count"], 1, "plays once")
	assert_eq(_gi._test_get_effect_count(), 1, "one live effect")


func test_per_axis_magnitude() -> void:
	if not _start():
		return
	var effect = _pad.create_force_feedback_effect(0, {"kind": _fx("EFFECT_CONSTANT"),
			"magnitude": {"linear_y": -0.25, "normal": 2.0}})
	var m: Dictionary = effect.get_params()["magnitude"]
	assert_eq_approx(m["linear_x"], 0.0, "unset axes stay 0")
	assert_eq_approx(m["linear_y"], -0.25, "linear Y")
	assert_eq_approx(m["normal"], 1.0, "axis magnitudes clamp to -1..1")


func test_periodic_effect_round_trip() -> void:
	if not _start():
		return
	var effect = _pad.create_force_feedback_effect(0, {
		"kind": _fx("EFFECT_SINE_WAVE"), "magnitude": 0.8, "frequency": 20.0, "phase": 0.25,
		"bias": 2.0, "attack_duration": 0.1, "sustain_duration": 2.5, "release_duration": 0.2,
		"attack_gain": 0.5, "sustain_gain": 1.5, "release_gain": 0.0, "play_count": 3,
		"repeat_delay": 0.05,
	})
	assert_not_null(effect, "sine effect created")
	var p: Dictionary = effect.get_params()
	assert_eq_approx(p["frequency"], 20.0, "frequency")
	assert_eq_approx(p["phase"], 0.25, "phase")
	assert_eq_approx(p["bias"], 1.0, "bias clamps to 1")
	assert_eq_approx(p["attack_duration"], 0.1, "attack seconds")
	assert_eq_approx(p["sustain_duration"], 2.5, "sustain seconds")
	assert_eq_approx(p["release_duration"], 0.2, "release seconds")
	assert_eq_approx(p["attack_gain"], 0.5, "attack gain")
	assert_eq_approx(p["sustain_gain"], 1.0, "gains clamp to 1")
	assert_eq(p["play_count"], 3, "play count")
	assert_eq_approx(p["repeat_delay"], 0.05, "repeat delay")


func test_condition_effect_round_trip() -> void:
	if not _start():
		return
	var effect = _pad.create_force_feedback_effect(0, {
		"kind": _fx("EFFECT_SPRING"), "magnitude": 0.5, "positive_coefficient": -2.0,
		"negative_coefficient": 0.5, "max_positive_magnitude": 0.75, "dead_zone": 0.1,
		"bias": -0.2,
	})
	var p: Dictionary = effect.get_params()
	assert_false(p.has("sustain_duration"), "condition effects have no envelope")
	assert_eq_approx(p["positive_coefficient"], -1.0, "coefficients clamp to -1..1")
	assert_eq_approx(p["negative_coefficient"], 0.5, "negative coefficient")
	assert_eq_approx(p["max_positive_magnitude"], 0.75, "max positive")
	assert_eq_approx(p["max_negative_magnitude"], -1.0, "the negative cap defaults to full force that way")
	assert_eq_approx(p["dead_zone"], 0.1, "dead zone")
	assert_eq_approx(p["bias"], -0.2, "bias")
	# The GDK SimpleFFBWheel sample passes the negative cap as a negative value.
	var capped = _pad.create_force_feedback_effect(1, {
		"kind": _fx("EFFECT_DAMPER"), "max_positive_magnitude": 0.25,
		"max_negative_magnitude": -0.25,
	})
	assert_not_null(capped, "a signed negative cap is accepted")
	if capped != null:
		assert_eq_approx(capped.get_params()["max_negative_magnitude"], -0.25,
				"a negative cap round-trips with its sign")
		assert_true(capped.set_params({"max_negative_magnitude": -3.0}), "an update past -1 is accepted")
		assert_eq_approx(capped.get_params()["max_negative_magnitude"], -1.0, "and clamps to -1")


func test_every_kind_can_be_created() -> void:
	if not _start():
		return
	var live := []
	for kind in 11:
		var effect = _pad.create_force_feedback_effect(1, {"kind": kind})
		assert_not_null(effect, "kind %d created on an all-kinds motor" % kind)
		if effect == null:
			continue
		live.append(effect)
		assert_eq(effect.get_kind(), kind, "kind %d round-trips" % kind)
		var p: Dictionary = effect.get_params()
		assert_eq(p["kind"], kind, "params kind %d" % kind)
		if kind <= _fx("EFFECT_SAWTOOTH_DOWN"):
			assert_eq(p["sustain_duration"], -1.0, "kind %d sustains forever by default" % kind)
		else:
			# Learn: negative coefficients counter the player's motion.
			assert_eq_approx(p["positive_coefficient"], -1.0, "kind %d resists to the right by default" % kind)
			assert_eq_approx(p["negative_coefficient"], -1.0, "kind %d resists to the left by default" % kind)
			assert_eq_approx(p["max_positive_magnitude"], 1.0, "kind %d positive cap default" % kind)
			assert_eq_approx(p["max_negative_magnitude"], -1.0, "kind %d negative cap default" % kind)
	assert_eq(_gi._test_get_effect_count(), 11, "eleven live effects while referenced")
	live.clear()
	assert_eq(_gi._test_get_effect_count(), 0, "dropping the references releases them all")


func test_validation_warnings() -> void:
	if not _start():
		return
	var cases := [
		[0, {"magnitude": 0.5}, "missing 'kind'"],
		[0, {"kind": 1.0}, "'kind' must be a GameInputForceFeedbackEffect.EffectKind value"],
		[0, {"kind": 99}, "unsupported effect kind 99"],
		[0, {"kind": _fx("EFFECT_RAMP")}, "does not support effect kind"],
		[7, {"kind": _fx("EFFECT_CONSTANT")}, "is out of range"],
		[0, {"kind": _fx("EFFECT_CONSTANT"), "magnitud": 0.5}, "unknown key 'magnitud' for this effect kind"],
		[0, {"kind": _fx("EFFECT_CONSTANT"), "frequency": 5.0}, "unknown key 'frequency'"],
		[0, {"kind": _fx("EFFECT_SINE_WAVE"), "frequency": "fast"}, "'frequency' must be a number"],
		[0, {"kind": _fx("EFFECT_SINE_WAVE"), "frequency": -1.0}, "'frequency' must be >= 0"],
		[0, {"kind": _fx("EFFECT_CONSTANT"), "magnitude": INF}, "'magnitude' must be finite"],
		[0, {"kind": _fx("EFFECT_CONSTANT"), "attack_gain": NAN}, "'attack_gain' must be finite"],
		# Finite as a double but infinite as the float GameInput takes.
		[1, {"kind": _fx("EFFECT_CONSTANT"), "magnitude": 1e300}, "'magnitude' must be finite and within the 32-bit float range"],
		[1, {"kind": _fx("EFFECT_CONSTANT"), "magnitude": {"linear_x": -1e300}}, "axis 'linear_x' in 'magnitude' must be finite"],
		[1, {"kind": _fx("EFFECT_SINE_WAVE"), "frequency": 1e300}, "'frequency' must be finite"],
		[1, {"kind": _fx("EFFECT_SINE_WAVE"), "phase": -1e39}, "'phase' must be finite"],
		# A cap of the wrong sign would clamp to 0 and remove all force that way.
		[1, {"kind": _fx("EFFECT_SPRING"), "max_negative_magnitude": 0.5}, "'max_negative_magnitude' must be in [-1.0, 0.0]"],
		[1, {"kind": _fx("EFFECT_SPRING"), "max_positive_magnitude": -0.5}, "'max_positive_magnitude' must be in [0.0, 1.0]"],
		[0, {"kind": _fx("EFFECT_CONSTANT"), "attack_duration": -1.0}, "'attack_duration' must be >= 0 seconds"],
		[0, {"kind": _fx("EFFECT_CONSTANT"), "play_count": 1.5}, "'play_count' must be a whole number >= 0"],
		[0, {"kind": _fx("EFFECT_CONSTANT"), "magnitude": {"linear_w": 1.0}}, "unknown axis 'linear_w' in 'magnitude'"],
		[0, {"kind": _fx("EFFECT_CONSTANT"), "magnitude": [1.0]}, "'magnitude' must be a number or a Dictionary of axes"],
	]
	for c in cases:
		var effect = _pad.create_force_feedback_effect(c[0], c[1])
		assert_null(effect, "rejected: %s" % c[2])
		assert_push_warning(c[2])
	assert_eq(_gi._test_get_effect_count(), 0, "no effects were created")


func test_set_params_merges_and_locks_the_kind() -> void:
	if not _start():
		return
	var effect = _pad.create_force_feedback_effect(0, {"kind": _fx("EFFECT_SINE_WAVE"),
			"frequency": 10.0, "sustain_duration": 1.0})
	assert_true(effect.set_params({"magnitude": 0.9}), "partial update accepted")
	var p: Dictionary = effect.get_params()
	assert_eq_approx(p["magnitude"]["linear_x"], 0.9, "magnitude updated")
	assert_eq_approx(p["frequency"], 10.0, "absent keys keep their values")
	assert_eq_approx(p["sustain_duration"], 1.0, "sustain kept")
	assert_true(effect.set_params({"kind": _fx("EFFECT_SINE_WAVE"), "frequency": 30.0}),
			"repeating the same kind is fine")
	assert_false(effect.set_params({"kind": _fx("EFFECT_CONSTANT")}), "the kind cannot change")
	assert_push_warning("'kind' cannot change after the effect is created")
	assert_false(effect.set_params({"frequency": -3.0}), "invalid update rejected")
	assert_push_warning("'frequency' must be >= 0")
	assert_eq_approx(effect.get_params()["frequency"], 30.0, "a rejected update changes nothing")


func test_state_and_gain() -> void:
	if not _start():
		return
	var effect = _pad.create_force_feedback_effect(1, {"kind": _fx("EFFECT_DAMPER")})
	assert_true(effect.start(), "start")
	assert_eq(effect.get_state(), _fx("STATE_RUNNING"), "running")
	assert_true(effect.pause(), "pause")
	assert_eq(effect.get_state(), _fx("STATE_PAUSED"), "paused")
	assert_true(effect.stop(), "stop")
	assert_eq(effect.get_state(), _fx("STATE_STOPPED"), "stopped")
	assert_true(effect.set_state(_fx("STATE_RUNNING")), "set_state(RUNNING)")
	assert_false(effect.set_state(5), "unknown state rejected")
	assert_push_warning("is not an EffectState value")
	assert_eq(effect.get_state(), _fx("STATE_RUNNING"), "state unchanged")
	assert_true(effect.set_gain(2.0), "gain accepted")
	assert_eq_approx(effect.get_gain(), 1.0, "gain clamps to 1")
	effect.set_gain(0.25)
	assert_eq_approx(effect.get_gain(), 0.25, "gain")


func test_release_and_wrapper_lifetime() -> void:
	if not _start():
		return
	var effect = _pad.create_force_feedback_effect(1, {"kind": _fx("EFFECT_CONSTANT")})
	effect.release()
	assert_false(effect.is_valid(), "released effect is invalid")
	assert_eq(_gi._test_get_effect_count(), 0, "released from the registry")
	assert_false(effect.start(), "start() on a released effect is false")
	assert_eq(effect.get_kind(), -1, "kind -1")
	assert_eq(effect.get_params(), {}, "no params")
	effect.release()
	assert_eq(_gi._test_get_effect_count(), 0, "release() twice is a no-op")
	var dropped = _pad.create_force_feedback_effect(1, {"kind": _fx("EFFECT_CONSTANT")})
	assert_eq(_gi._test_get_effect_count(), 1, "one live effect")
	dropped = null
	assert_eq(_gi._test_get_effect_count(), 0, "freeing the last reference releases the effect")


func test_effects_die_with_their_device_and_on_shutdown() -> void:
	if not _start():
		return
	var effect = _pad.create_force_feedback_effect(1, {"kind": _fx("EFFECT_CONSTANT")})
	var other = add_mock_device(_gi, {"ffb_motors": [{}]})
	var survivor = other.create_force_feedback_effect(0, {"kind": _fx("EFFECT_CONSTANT")})
	_gi._test_remove_device(_pad.get_device_id())
	_gi._test_force_poll()
	assert_false(effect.is_valid(), "effect released when its device disconnects")
	assert_true(survivor.is_valid(), "other devices keep their effects")
	_gi.shutdown()
	assert_false(survivor.is_valid(), "shutdown releases every effect")
	assert_false(survivor.start(), "stale effect is inert")
