extends "res://addons/godot_gdk_tests/gameinput_test_base.gd"
## GameInput v2 — system buttons, keyboard layout, focus policy, aggregate
## devices and timestamps through the mock backend.
##
## System-button and keyboard-layout notifications arrive on GameInput worker
## threads in native builds; like every other callback they are queued and
## emitted from poll() on the main thread, and only real changes emit.

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


func test_system_buttons_changed() -> void:
	_gi = begin_mock_session()
	if _gi == null:
		return
	_gi._test_set_time_override_usec(5_000_000)
	var guide := _d("SYSTEM_BUTTON_GUIDE")
	var share := _d("SYSTEM_BUTTON_SHARE")
	var pad = add_mock_device(_gi, {"system_buttons": guide | share})
	assert_eq(pad.get_supported_system_buttons(), guide | share, "supported system buttons")
	assert_eq(pad.get_system_buttons(), 0, "nothing held at connect")
	_log = watch_gameinput_signals(_gi)

	assert_true(_gi._test_push_system_buttons(pad.get_device_id(), guide), "queued")
	assert_eq(_log.named("system_buttons_changed").size(), 0, "nothing emits before poll()")
	_gi._test_force_poll()
	var events: Array = _log.named("system_buttons_changed")
	assert_eq(events.size(), 1, "one change")
	assert_eq(events[0][1], pad, "device")
	assert_eq(events[0][2], guide, "current buttons")
	assert_eq(events[0][3], 0, "previous buttons")
	assert_eq(events[0][4], 5_000_000, "timestamp in microseconds")
	assert_eq(pad.get_system_buttons(), guide, "held buttons tracked")

	_gi._test_push_system_buttons(pad.get_device_id(), guide)
	_gi._test_force_poll()
	assert_eq(_log.named("system_buttons_changed").size(), 1, "an unchanged value emits nothing")

	_gi._test_push_system_buttons(pad.get_device_id(), 0xFF)
	_gi._test_force_poll()
	events = _log.named("system_buttons_changed")
	assert_eq(events.size(), 2, "second change")
	assert_eq(events[1][2], guide | share, "only Guide and Share bits exist")
	assert_eq(events[1][3], guide, "previous is the last emitted value")

	_gi._test_push_system_buttons(pad.get_device_id(), 0)
	_gi._test_force_poll()
	assert_eq(_log.named("system_buttons_changed")[2][2], 0, "release reported")
	assert_eq(pad.get_system_buttons(), 0, "nothing held")


func test_keyboard_layout_changed() -> void:
	_gi = begin_mock_session()
	if _gi == null:
		return
	var kb = add_mock_device(_gi, {"kind_mask": _k("DEVICE_KEYBOARD"), "keyboard_layout": 0x0409})
	assert_eq(kb.get_keyboard_layout(), 0x0409, "layout at connect")
	assert_eq(kb.get_device_info()["keyboard"]["layout"], 0x0409, "info reports the layout")
	_log = watch_gameinput_signals(_gi)

	_gi._test_push_keyboard_layout(kb.get_device_id(), 0x040C)
	_gi._test_force_poll()
	var events: Array = _log.named("keyboard_layout_changed")
	assert_eq(events.size(), 1, "one layout change")
	assert_eq(events[0][1], kb, "device")
	assert_eq(events[0][2], 0x040C, "new layout")
	assert_eq(events[0][3], 0x0409, "previous layout")
	assert_eq(kb.get_keyboard_layout(), 0x040C, "layout tracked")
	assert_eq(kb.get_device_info()["keyboard"]["layout"], 0x040C, "info follows the change")

	_gi._test_push_keyboard_layout(kb.get_device_id(), 0x040C)
	_gi._test_force_poll()
	assert_eq(_log.named("keyboard_layout_changed").size(), 1, "an unchanged layout emits nothing")

	# IME and custom layouts set the top bit of the 32-bit KLID.
	var ime := 0xE0010409
	_gi._test_push_keyboard_layout(kb.get_device_id(), ime)
	_gi._test_force_poll()
	events = _log.named("keyboard_layout_changed")
	assert_eq(events.size(), 2, "the IME layout is a change")
	assert_eq(events[1][2], ime, "the signal carries the unsigned KLID")
	assert_eq(kb.get_keyboard_layout(), ime, "the accessor is not sign-extended")
	assert_eq(kb.get_device_info()["keyboard"]["layout"], ime, "nor is the info layout")


func test_events_keep_queue_order() -> void:
	_gi = begin_mock_session()
	if _gi == null:
		return
	var pad = add_mock_device(_gi, {"system_buttons": 3})
	_log = watch_gameinput_signals(_gi)
	_gi._test_push_system_buttons(pad.get_device_id(), 1)
	_gi._test_set_device_status(pad.get_device_id(), 0)
	_gi._test_force_poll()
	assert_eq(_log.names(), ["system_buttons_changed", "device_disconnected"],
			"events drain in the order they were queued")


func test_pushes_for_unknown_devices_are_rejected() -> void:
	_gi = begin_mock_session()
	if _gi == null:
		return
	assert_false(_gi._test_push_system_buttons(999_999, 1), "unknown device")
	assert_false(_gi._test_push_keyboard_layout(999_999, 0x0409), "unknown device")
	_gi.shutdown()
	assert_false(_gi._test_push_system_buttons(1, 1), "not initialized")


func test_focus_policy() -> void:
	var gi = get_gameinput()
	if gi == null:
		pending("GameInput singleton is not available in this host")
		return
	_gi = gi
	assert_eq(_k("FOCUS_POLICY_DEFAULT"), 0, "default policy")
	assert_eq(_k("FOCUS_POLICY_EXCLUSIVE_FOREGROUND_INPUT"), 0x002, "GameInputExclusiveForegroundInput")
	assert_eq(_k("FOCUS_POLICY_EXCLUSIVE_FOREGROUND_GUIDE_BUTTON"), 0x008, "GameInputExclusiveForegroundGuideButton")
	assert_eq(_k("FOCUS_POLICY_EXCLUSIVE_FOREGROUND_SHARE_BUTTON"), 0x020, "GameInputExclusiveForegroundShareButton")
	assert_eq(_k("FOCUS_POLICY_ENABLE_BACKGROUND_INPUT"), 0x040, "GameInputEnableBackgroundInput")
	assert_eq(_k("FOCUS_POLICY_ENABLE_BACKGROUND_GUIDE_BUTTON"), 0x080, "GameInputEnableBackgroundGuideButton")
	assert_eq(_k("FOCUS_POLICY_ENABLE_BACKGROUND_SHARE_BUTTON"), 0x100, "GameInputEnableBackgroundShareButton")
	assert_true(ProjectSettings.has_setting("game_input/runtime/focus_policy"), "setting registered")
	assert_eq(get_setting_default("game_input/runtime/focus_policy"), 0, "setting default")

	var policy: int = _k("FOCUS_POLICY_ENABLE_BACKGROUND_INPUT") | _k("FOCUS_POLICY_ENABLE_BACKGROUND_GUIDE_BUTTON")
	gi.set_focus_policy(policy)
	assert_eq(gi.get_focus_policy(), policy, "policy stored before initialize()")
	gi.set_focus_policy(policy | 0x1)
	assert_push_warning("ignored unknown policy bits")
	assert_eq(gi.get_focus_policy(), policy, "unknown bits are dropped")


func test_reading_callback_setting() -> void:
	if get_gameinput() == null:
		pending("GameInput singleton is not available in this host")
		return
	assert_true(ProjectSettings.has_setting("game_input/runtime/reading_callback_kinds"), "setting registered")
	assert_eq(get_setting_default("game_input/runtime/reading_callback_kinds"), 0, "off by default")


func test_aggregate_devices_are_native_only() -> void:
	_gi = begin_mock_session()
	if _gi == null:
		return
	assert_eq(_gi.create_aggregate_device(_k("DEVICE_GAMEPAD")), "", "mock backend has no aggregates")
	assert_false(_gi.disable_aggregate_device("00".repeat(32)), "nothing to disable")


func test_current_timestamp() -> void:
	_gi = begin_mock_session()
	if _gi == null:
		return
	var t0: int = _gi.get_current_timestamp()
	assert_gt(t0, 0, "monotonic microseconds")
	assert_true(_gi.get_current_timestamp() >= t0, "never goes backwards")
	_gi._test_set_time_override_usec(7_000_000)
	assert_eq(_gi.get_current_timestamp(), 7_000_000, "mock clock override")
	var pad = add_mock_device(_gi)
	var reading = mock_reading(_gi, pad, {"gamepad": {"buttons": 1}})
	assert_eq(reading.get_timestamp(), 7_000_000, "readings share the clock")
