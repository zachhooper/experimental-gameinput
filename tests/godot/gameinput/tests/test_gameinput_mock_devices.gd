extends "res://addons/godot_gdk_tests/gameinput_test_base.gd"
## GameInput v2 — device lifecycle through the debug-only mock backend.
##
## The mock feeds the same main-thread queue as the native device callback,
## so these tests pin the drain contract without hardware:
##   * connects surface on the next poll() as `device_connected(device)`;
##   * ids start at 1, grow monotonically and are never recycled;
##   * non-connect transitions surface as
##     `device_status_changed(device, status, previous_status, timestamp)`;
##   * disconnects emit `device_disconnected(device_id)` and stale wrappers
##     fall back to safe defaults;
##   * kind masks filter get_devices / get_primary_device / counts, with
##     DEVICE_ALL keeping its v1 meaning (gamepad | keyboard | mouse).

var _gi = null
var _log = null


func before_each() -> void:
	_gi = null
	_log = null


func after_each() -> void:
	if _log != null:
		_log.stop()
	end_mock_session(_gi if _gi != null else get_gameinput())


func _k(constant: String) -> int:
	return ClassDB.class_get_integer_constant("GameInput", constant)


func _d(constant: String) -> int:
	return ClassDB.class_get_integer_constant("GameInputDevice", constant)


func _start() -> bool:
	_gi = begin_mock_session()
	if _gi == null:
		return false
	_log = watch_gameinput_signals(_gi)
	return true


func test_mock_session_initializes() -> void:
	if not _start():
		return
	assert_true(_gi.is_initialized(), "mock session is initialized")
	assert_eq(_gi._test_get_backend(), MOCK_BACKEND, "backend reports mock")
	assert_eq(_gi.get_devices(_k("DEVICE_ANY")), [], "no devices yet")
	assert_eq(_gi.get_connected_device_count(), 0, "count is 0")


func test_connect_is_queued_until_poll() -> void:
	if not _start():
		return
	var id: int = _gi._test_inject_device({"name": "Pad"})
	assert_gt(id, 0, "inject returns a positive id")
	assert_eq(_log.events.size(), 0, "nothing emitted before poll()")
	assert_null(_gi.get_device_by_id(id), "device not visible before poll()")
	_gi._test_force_poll()
	var connects: Array = _log.named("device_connected")
	assert_eq(connects.size(), 1, "one device_connected after poll()")
	var device = connects[0][1]
	assert_eq(device.get_device_id(), id, "signal carries the injected device")
	assert_true(device.is_connected(), "device reports connected")
	assert_eq(device.get_display_name(), "Pad", "mock display name")
	assert_eq(device.get_kind_mask(), _k("DEVICE_GAMEPAD"), "mock defaults to a gamepad")
	assert_eq(device.get_device_family(), _d("FAMILY_VIRTUAL"), "mock family defaults to virtual")
	assert_true(device.get_status() & _d("STATUS_CONNECTED") != 0, "status has CONNECTED")


func test_ids_are_monotonic_and_never_recycled() -> void:
	if not _start():
		return
	var a = add_mock_device(_gi)
	var b = add_mock_device(_gi)
	assert_eq(b.get_device_id(), a.get_device_id() + 1, "ids increase by one")
	var removed_id: int = a.get_device_id()
	_gi._test_remove_device(removed_id)
	_gi._test_force_poll()
	var c = add_mock_device(_gi)
	assert_gt(c.get_device_id(), b.get_device_id(), "a new device never reuses an old id")
	assert_null(_gi.get_device_by_id(removed_id), "removed id resolves to null")


func test_disconnect_emits_id_and_invalidates_wrapper() -> void:
	if not _start():
		return
	var device = add_mock_device(_gi, {"rumble_motors": 3})
	var id: int = device.get_device_id()
	_log.clear()
	assert_true(_gi._test_remove_device(id), "remove queues a disconnect")
	assert_true(device.is_connected(), "wrapper stays connected until the drain")
	_gi._test_force_poll()
	assert_eq(_log.names(), ["device_disconnected"], "only device_disconnected emitted")
	assert_eq(_log.events[0][1], id, "disconnect carries the id")
	assert_false(device.is_connected(), "stale wrapper reports disconnected")
	assert_eq(device.get_kind_mask(), 0, "stale wrapper kind mask is 0")
	assert_false(device.start_vibration(1.0, 1.0), "stale wrapper cannot vibrate")
	assert_eq(device.get_device_info(), {}, "stale wrapper info is empty")
	assert_null(_gi.get_current_reading(device), "stale wrapper has no reading")


func test_remove_before_drain_suppresses_both_signals() -> void:
	if not _start():
		return
	var id: int = _gi._test_inject_device({})
	assert_true(_gi._test_remove_device(id), "remove before drain succeeds")
	_gi._test_force_poll()
	assert_eq(_log.events.size(), 0, "no connect or disconnect for a device nobody saw")
	assert_eq(_gi.get_connected_device_count(_k("DEVICE_ANY")), 0, "count stays 0")


func test_status_change_signal() -> void:
	if not _start():
		return
	var device = add_mock_device(_gi)
	_log.clear()
	_gi._test_set_time_override_usec(123456)
	var connected := _d("STATUS_CONNECTED")
	var ready := _d("STATUS_HAPTIC_INFO_READY")
	assert_true(_gi._test_set_device_status(device.get_device_id(), connected | ready),
			"status change queued")
	_gi._test_force_poll()
	var changes: Array = _log.named("device_status_changed")
	assert_eq(changes.size(), 1, "one status change")
	assert_eq(changes[0][1], device, "status change carries the device")
	assert_eq(changes[0][2], connected | ready, "new status")
	assert_eq(changes[0][3], connected, "previous status")
	assert_eq(changes[0][4], 123456, "timestamp comes from the GameInput clock")
	assert_eq(device.get_status(), connected | ready, "device status updated")
	assert_true(device.get_haptic_info()["ready"], "haptic info ready follows status")
	_log.clear()
	_gi._test_set_device_status(device.get_device_id(), connected | ready)
	_gi._test_force_poll()
	assert_eq(_log.events.size(), 0, "an unchanged status emits nothing")


func test_status_bits_that_arrive_with_the_connect_emit_no_status_change() -> void:
	if not _start():
		return
	var connected := _d("STATUS_CONNECTED")
	var ready := _d("STATUS_HAPTIC_INFO_READY")
	var seen: Array = []
	var on_connected := func(device) -> void: seen.append(device.get_status())
	_gi.device_connected.connect(on_connected)
	var id: int = _gi._test_inject_device({"status": connected | ready})
	_gi._test_force_poll()
	_gi.device_connected.disconnect(on_connected)
	assert_eq(_log.names(), ["device_connected"], "the connect is the only signal")
	assert_eq(seen, [connected | ready], "get_status() already has the bit inside device_connected")
	assert_true(_gi.get_device_by_id(id).get_haptic_info()["ready"], "haptic info is ready from the start")


func test_status_without_connected_bit_is_a_disconnect() -> void:
	if not _start():
		return
	var device = add_mock_device(_gi)
	_log.clear()
	_gi._test_set_device_status(device.get_device_id(), 0)
	_gi._test_force_poll()
	assert_eq(_log.names(), ["device_disconnected"], "status 0 disconnects")


func test_kind_masks_filter_queries() -> void:
	if not _start():
		return
	var pad = add_mock_device(_gi, {"kind_mask": _k("DEVICE_GAMEPAD")})
	var keyboard = add_mock_device(_gi, {"kind_mask": _k("DEVICE_KEYBOARD")})
	var wheel = add_mock_device(_gi, {"kind_mask": _k("DEVICE_RACING_WHEEL")})
	var combo = add_mock_device(_gi, {"kind_mask": _k("DEVICE_GAMEPAD") | _k("DEVICE_ARCADE_STICK")})
	assert_eq(_gi.get_devices(), [pad, combo], "get_devices() defaults to gamepads")
	assert_eq(_gi.get_devices(_k("DEVICE_KEYBOARD")), [keyboard], "keyboard filter")
	assert_eq(_gi.get_devices(_k("DEVICE_RACING_WHEEL")), [wheel], "wheel filter")
	assert_eq(_gi.get_devices(_k("DEVICE_ARCADE_STICK")), [combo], "arcade filter")
	assert_eq(_gi.get_devices(_k("DEVICE_ALL")).size(), 3,
			"DEVICE_ALL keeps its v1 meaning and skips the wheel-only device")
	assert_eq(_gi.get_devices(_k("DEVICE_ANY")).size(), 4, "DEVICE_ANY sees every device")
	assert_eq(_gi.get_connected_device_count(), 3, "count defaults to DEVICE_ALL")
	assert_eq(_gi.get_connected_device_count(_k("DEVICE_ANY")), 4, "count of every device")
	assert_eq(_gi.get_primary_device(_k("DEVICE_RACING_WHEEL")), wheel, "primary wheel")
	assert_eq(_gi.get_primary_device(), pad, "primary gamepad is the first connected")
	assert_null(_gi.get_primary_device(_k("DEVICE_FLIGHT_STICK")), "no flight stick")


func test_device_info_shape() -> void:
	if not _start():
		return
	var device = add_mock_device(_gi, {
		"name": "Info Pad", "vendor_id": 0x045E, "product_id": 0x0B13, "revision": 7,
		"family": _d("FAMILY_XBOX_ONE"), "rumble_motors": 0xF, "system_buttons": 3,
		"app_local_id": "ab".repeat(32),
	})
	var info: Dictionary = device.get_device_info()
	for key in ["name", "vendor_id", "product_id", "revision", "device_family",
			"supported_input_kinds", "supported_rumble_motors", "supported_system_buttons",
			"supports_vibration", "status", "app_local_id", "is_mock", "gamepad"]:
		assert_true(info.has(key), "get_device_info() has '%s'" % key)
	assert_eq(info["name"], "Info Pad", "info name")
	assert_eq(info["vendor_id"], 0x045E, "info vendor")
	assert_eq(info["product_id"], 0x0B13, "info product")
	assert_eq(info["device_family"], _d("FAMILY_XBOX_ONE"), "info family")
	assert_true(info["supports_vibration"], "rumble motors mean vibration")
	assert_true(info["is_mock"], "mock devices are flagged")
	assert_eq(device.get_app_local_id(), "ab".repeat(32), "app-local id is 64 hex chars")
	assert_eq(device.get_supported_rumble_motors(), 0xF, "all four rumble motors")
	assert_eq(device.get_supported_system_buttons(), 3, "guide and share")
	assert_true(device.supports_vibration(), "supports_vibration()")


func test_button_labels() -> void:
	if not _start():
		return
	var device = add_mock_device(_gi, {"labels": {_d("SRC_BTN_A"): 7, _d("SRC_BTN_B"): 25}})
	assert_eq(device.get_button_label(_d("SRC_BTN_A")), "xbox_a", "label for A")
	assert_eq(device.get_button_label(_d("SRC_BTN_B")), "letter_a", "label for B")
	assert_eq(device.get_button_label(_d("SRC_BTN_X")), "", "unlabeled source")


func test_invalid_mock_info_is_rejected() -> void:
	if not _start():
		return
	assert_eq(_gi._test_inject_device({"bogus": 1}), 0, "unknown key rejected")
	assert_push_error("unknown mock device key 'bogus'")
	assert_eq(_gi._test_inject_device({"name": 5}), 0, "non-string name rejected")
	assert_push_error("must be a String")
	assert_eq(_gi._test_inject_device({"labels": {"a": 1}}), 0, "string label key rejected")
	assert_push_error("labels")
	_gi._test_force_poll()
	assert_eq(_log.events.size(), 0, "rejected devices never connect")


func test_shutdown_clears_devices_and_restart_keeps_ids_growing() -> void:
	if not _start():
		return
	var device = add_mock_device(_gi)
	var old_id: int = device.get_device_id()
	_gi.shutdown()
	assert_false(device.is_connected(), "shutdown disconnects every wrapper")
	_gi._test_initialize_mock()
	var fresh = add_mock_device(_gi)
	assert_gt(fresh.get_device_id(), old_id, "ids keep growing across sessions")


func test_signal_emitted_for_each_connect_in_order() -> void:
	if not _start():
		return
	var ids := []
	for i in 3:
		ids.append(_gi._test_inject_device({"name": "Pad %d" % i}))
	_gi._test_force_poll()
	var seen: Array = _log.named("device_connected").map(func(e): return e[1].get_device_id())
	assert_eq(seen, ids, "connects are delivered in arrival order")
