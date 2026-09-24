extends "res://addons/godot_gdk_tests/gameinput_test_base.gd"
## GameInput v2 — event-driven readings (`reading_received`).
##
## With `set_reading_callback_kinds(mask)` non-zero the runtime registers one
## GameInput reading callback. The worker thread only copies each reading into
## a fixed ring; `poll()` drains the ring on the main thread, in arrival order
## merged with device events, and emits one `reading_received` per reading.
## Each event reading's previous sample is the previous *event*, so sub-frame
## taps that a once-per-frame poll would miss are still visible.

var _gi = null
var _log = null


func after_each() -> void:
	if _log != null:
		_log.stop()
		_log = null
	end_mock_session(_gi if _gi != null else get_gameinput())
	_gi = null


func _k(constant: String) -> int:
	return ClassDB.class_get_integer_constant("GameInput", constant)


func _d(constant: String) -> int:
	return ClassDB.class_get_integer_constant("GameInputDevice", constant)


func _start(callback_kinds: int, device_info: Dictionary = {}):
	_gi = begin_mock_session()
	if _gi == null:
		return null
	_gi.set_reading_callback_kinds(callback_kinds)
	var device = add_mock_device(_gi, device_info)
	_log = watch_gameinput_signals(_gi)
	return device


func _push(device, state: Dictionary) -> void:
	assert_true(_gi._test_push_reading(device.get_device_id(), state), "push accepted")


func _readings() -> Array:
	return _log.named("reading_received").map(func(e): return e[2])


func test_off_by_default() -> void:
	var pad = _start(0)
	if pad == null:
		return
	assert_eq(_gi.get_reading_callback_kinds(), 0, "callbacks are off by default")
	_push(pad, {"gamepad": {"buttons": _d("BUTTON_A")}})
	_gi._test_force_poll()
	assert_eq(_log.named("reading_received").size(), 0, "no reading_received while off")
	assert_eq(_gi.get_buffered_readings(pad), [], "nothing buffered")
	assert_true(_gi.get_current_reading(pad).is_button_down(_d("BUTTON_A")),
			"polling still works")


func test_one_signal_per_reading_in_order_with_per_event_edges() -> void:
	var pad = _start(_k("DEVICE_GAMEPAD"))
	if pad == null:
		return
	var a := _d("BUTTON_A")
	_push(pad, {"timestamp": 10, "gamepad": {"buttons": a}})
	_push(pad, {"timestamp": 20, "gamepad": {"buttons": 0}})
	_push(pad, {"timestamp": 30, "gamepad": {"buttons": a}})
	assert_eq(_log.events.size(), 0, "nothing is emitted before poll()")
	_gi._test_force_poll()
	var readings := _readings()
	assert_eq(readings.size(), 3, "one reading_received per reading")
	assert_eq(readings.map(func(r): return r.get_timestamp()), [10, 20, 30], "arrival order")
	assert_eq(_log.named("reading_received")[0][1], pad, "signal carries the device")
	assert_false(readings[0].has_previous(), "first event has no previous event")
	assert_true(readings[0].was_button_pressed(a), "tap press")
	assert_true(readings[1].has_previous(), "second event has a previous event")
	assert_true(readings[1].was_button_released(a), "sub-frame release is visible")
	assert_true(readings[2].was_button_pressed(a), "second press is visible")
	var polled = _gi.get_current_reading(pad)
	assert_true(polled.is_button_down(a), "polled reading sees only the final state")
	assert_eq(_gi.get_buffered_readings(pad), readings, "buffered readings match the signals")


func test_buffered_readings_are_per_poll() -> void:
	var pad = _start(_k("DEVICE_GAMEPAD"))
	if pad == null:
		return
	_push(pad, {"gamepad": {"buttons": 0}})
	_gi._test_force_poll()
	assert_eq(_gi.get_buffered_readings(pad).size(), 1, "one reading this poll")
	_gi._test_force_poll()
	assert_eq(_gi.get_buffered_readings(pad).size(), 0, "cleared on the next poll")
	assert_eq(_gi.get_buffered_readings(null), [], "null device -> empty")


func test_kind_filter() -> void:
	var device = _start(_k("DEVICE_KEYBOARD"),
			{"kind_mask": _k("DEVICE_GAMEPAD") | _k("DEVICE_KEYBOARD")})
	if device == null:
		return
	_push(device, {"gamepad": {"buttons": _d("BUTTON_B")}})
	_gi._test_force_poll()
	assert_eq(_log.named("reading_received").size(), 0, "gamepad input is filtered out")
	_push(device, {"gamepad": {"buttons": 0}, "keyboard": {"keys": [0x1E]}})
	_gi._test_force_poll()
	var readings := _readings()
	assert_eq(readings.size(), 1, "keyboard input is delivered")
	assert_eq(readings[0].get_input_kinds(), _k("DEVICE_KEYBOARD"),
			"the event carries only the registered kinds")
	assert_true(readings[0].is_physical_key_down(KEY_A), "keyboard payload")
	assert_eq(readings[0].get_buttons_mask(), 0, "no gamepad payload")


func test_registration_change_marks_a_gap() -> void:
	var pad = _start(_k("DEVICE_GAMEPAD"))
	if pad == null:
		return
	_push(pad, {"gamepad": {"buttons": 0}})
	_gi._test_force_poll()
	assert_false(_readings()[0].has_gap_before(), "no gap on the first reading")
	assert_true(_gi.set_reading_callback_kinds(_k("DEVICE_GAMEPAD") | _k("DEVICE_MOUSE")),
			"re-registration succeeds")
	_log.clear()
	_push(pad, {"gamepad": {"buttons": _d("BUTTON_X")}})
	_push(pad, {"gamepad": {"buttons": 0}})
	_gi._test_force_poll()
	var readings := _readings()
	assert_true(readings[0].has_gap_before(), "readings may be missing across re-registration")
	assert_false(readings[1].has_gap_before(), "the gap flag applies to one reading")


func test_turning_callbacks_off_stops_delivery() -> void:
	var pad = _start(_k("DEVICE_GAMEPAD"))
	if pad == null:
		return
	_gi.set_reading_callback_kinds(0)
	assert_true(_gi._test_push_reading(pad.get_device_id(), {"gamepad": {"buttons": 1}}),
			"the polled state still accepts input")
	_gi._test_force_poll()
	assert_eq(_log.named("reading_received").size(), 0, "no events once turned off")
	assert_true(_gi.get_current_reading(pad).is_button_down(1), "polling still sees the input")


func test_kinds_set_before_initialize_apply_on_initialize() -> void:
	var gi = begin_mock_session()
	if gi == null:
		return
	_gi = gi
	gi.shutdown()
	assert_true(gi.set_reading_callback_kinds(_k("DEVICE_GAMEPAD")), "stored while shut down")
	gi._test_initialize_mock()
	var pad = add_mock_device(gi)
	_log = watch_gameinput_signals(gi)
	_push(pad, {"gamepad": {"buttons": 0}})
	gi._test_force_poll()
	assert_eq(_log.named("reading_received").size(), 1, "registration applied by initialize")


func test_ring_overflow_drops_oldest_and_flags_the_gap() -> void:
	var pad = _start(_k("DEVICE_GAMEPAD"))
	if pad == null:
		return
	var total := 600
	for i in total:
		_gi._test_push_reading(pad.get_device_id(), {"timestamp": 1000 + i, "gamepad": {"buttons": 0}})
	assert_eq(_gi.get_dropped_reading_count(), total - 512, "readings beyond 512 are dropped")
	_gi._test_force_poll()
	var readings := _readings()
	assert_eq(readings.size(), 512, "the ring keeps the newest 512")
	assert_eq(readings[0].get_timestamp(), 1000 + total - 512, "oldest readings were dropped")
	assert_eq(readings[-1].get_timestamp(), 1000 + total - 1, "newest reading kept")
	assert_true(readings[0].has_gap_before(), "the first reading after an overflow has a gap")
	assert_false(readings[1].has_gap_before(), "later readings do not")


func test_readings_and_device_events_share_one_order() -> void:
	var pad = _start(_k("DEVICE_GAMEPAD"))
	if pad == null:
		return
	var id: int = pad.get_device_id()
	_push(pad, {"gamepad": {"buttons": 0}})
	_gi._test_set_device_status(id, _d("STATUS_CONNECTED") | _d("STATUS_HAPTIC_INFO_READY"))
	_push(pad, {"gamepad": {"buttons": _d("BUTTON_Y")}})
	_gi._test_remove_device(id)
	_gi._test_force_poll()
	assert_eq(_log.names(), ["reading_received", "device_status_changed", "reading_received",
			"device_disconnected"], "delivery follows arrival order")


func test_handler_shutdown_stops_the_drain() -> void:
	var pad = _start(_k("DEVICE_GAMEPAD"))
	if pad == null:
		return
	for i in 3:
		_push(pad, {"gamepad": {"buttons": 0}})
	var gi = _gi
	var shutdown_once := func(_device, _reading): gi.shutdown()
	gi.reading_received.connect(shutdown_once)
	gi._test_force_poll()
	gi.reading_received.disconnect(shutdown_once)
	assert_eq(_log.named("reading_received").size(), 1,
			"no reading is delivered after a handler shuts the runtime down")
	assert_false(gi.is_initialized(), "runtime is shut down")
