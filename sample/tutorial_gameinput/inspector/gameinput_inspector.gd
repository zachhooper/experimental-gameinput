extends VBoxContainer

## Live GameInput device inspector for the tutorial sample.
##
## Lists every device GameInput reports (keyboards, mice, sensors, arcade and
## flight sticks, racing wheels and raw controllers as well as gamepads), shows
## the selected device's capabilities and its live reading, and has buttons to
## try rumble, impulse triggers and force feedback. The two toggles switch the
## runtime to event-driven readings and to background input.
##
## Everything here goes through the public GDScript API; see
## docs/gameinput/plugin.md for the calls it makes. The UI is built in code so
## the scene stays a single node.

const AddonApi = preload("res://addon_api.gd")

const LOG_LINES := 50
const RESYNC_SEC := 0.5
const RUMBLE_SEC := 0.5
const FFB_PULSE_SEC := 0.3

var _gi: Object = null
var _device_list: ItemList
var _info: Label
var _live: Label
var _rumble_button: Button
var _trigger_button: Button
var _stop_button: Button
var _ffb_button: Button
var _events_toggle: CheckBox
var _background_toggle: CheckBox
var _event_stats: Label
var _log: RichTextLabel

var _listed_ids: Array = []
var _selected_id: int = -1
var _log_lines: Array = []
var _effect = null
var _effect_release_msec: int = 0
var _events_this_window: int = 0
var _events_per_second: int = 0
var _window_started_msec: int = 0
var _resync_left: float = 0.0
var _names: Dictionary = {}


func _ready() -> void:
	_build_ui()
	_gi = AddonApi.singleton("GameInput")
	if _gi == null:
		_info.text = "GameInput singleton missing. Build the addon first."
		_set_actions_enabled(false)
		set_process(false)
		return
	_names = {
		"button": _enum_names("GameInputDevice", "Button", "BUTTON_"),
		"mouse": _enum_names("GameInputDevice", "MouseButton", "MOUSE_"),
		"arcade": _enum_names("GameInputDevice", "ArcadeStickButton", "ARCADE_STICK_"),
		"flight": _enum_names("GameInputDevice", "FlightStickButton", "FLIGHT_STICK_"),
		"wheel": _enum_names("GameInputDevice", "RacingWheelButton", "RACING_WHEEL_"),
		"family": _enum_names("GameInputDevice", "DeviceFamily", "FAMILY_", true),
		"status": _enum_names("GameInputDevice", "DeviceStatus", "STATUS_", true),
		"rumble": _enum_names("GameInputDevice", "RumbleMotor", "RUMBLE_", true),
		"system": _enum_names("GameInputDevice", "SystemButton", "SYSTEM_BUTTON_", true),
		"kind": _enum_names("GameInput", "DeviceKind", "DEVICE_", true),
	}
	_gi.device_connected.connect(_on_device_connected)
	_gi.device_disconnected.connect(_on_device_disconnected)
	_gi.device_status_changed.connect(_on_device_status_changed)
	_gi.reading_received.connect(_on_reading_received)
	_gi.system_buttons_changed.connect(_on_system_buttons_changed)
	_gi.keyboard_layout_changed.connect(_on_keyboard_layout_changed)
	_window_started_msec = Time.get_ticks_msec()
	_refresh_list()
	_sync_toggles()


func _exit_tree() -> void:
	if _effect != null:
		_effect.release()
		_effect = null
	if _gi == null or not is_instance_valid(_gi):
		return
	for entry in [
		["device_connected", _on_device_connected],
		["device_disconnected", _on_device_disconnected],
		["device_status_changed", _on_device_status_changed],
		["reading_received", _on_reading_received],
		["system_buttons_changed", _on_system_buttons_changed],
		["keyboard_layout_changed", _on_keyboard_layout_changed],
	]:
		if _gi.is_connected(entry[0], entry[1]):
			_gi.disconnect(entry[0], entry[1])


func _process(delta: float) -> void:
	var now := Time.get_ticks_msec()
	if now - _window_started_msec >= 1000:
		_events_per_second = _events_this_window
		_events_this_window = 0
		_window_started_msec = now
	if _effect != null and now >= _effect_release_msec:
		_effect.release()
		_effect = null
	_resync_left -= delta
	if _resync_left <= 0.0:
		_resync_left = RESYNC_SEC
		_sync_toggles()
		if _current_ids() != _listed_ids:
			_refresh_list()
		else:
			_refresh_info()
	if _gi.is_initialized():
		_event_stats.text = "events/s %d  dropped %d" % [_events_per_second, _gi.get_dropped_reading_count()]
	_live.text = _describe_live(_selected_device())


# ── Accessors (used by selftest/gameinput_selftest.gd) ──────────────────

## Device ids in list order.
func get_listed_device_ids() -> Array:
	return _listed_ids.duplicate()


## Selects the device with [param device_id]; returns false when it is not listed.
func select_device(device_id: int) -> bool:
	var index := _listed_ids.find(device_id)
	if index < 0:
		return false
	_device_list.select(index)
	_on_device_selected(index)
	return true


## The live-state text shown for the selected device.
func get_live_text() -> String:
	return _live.text


## The action button named [param action]: "rumble", "triggers", "stop" or
## "force_feedback". Returns null for any other name.
func get_action_button(action: String) -> Button:
	match action:
		"rumble":
			return _rumble_button
		"triggers":
			return _trigger_button
		"stop":
			return _stop_button
		"force_feedback":
			return _ffb_button
	return null


## The check box named [param toggle]: "events" or "background". Returns null
## for any other name.
func get_toggle(toggle: String) -> CheckBox:
	match toggle:
		"events":
			return _events_toggle
		"background":
			return _background_toggle
	return null


## The force-feedback effect the Force feedback pulse button started, or null
## once it has been released.
func get_active_effect():
	return _effect


## The event log, oldest line first.
func get_log_lines() -> Array:
	return _log_lines.duplicate()


# ── UI ───────────────────────────────────────────────────────────────────

func _build_ui() -> void:
	add_theme_constant_override("separation", 6)
	var title := Label.new()
	title.text = "Device inspector"
	title.add_theme_font_size_override("font_size", 20)
	add_child(title)

	_device_list = ItemList.new()
	_device_list.custom_minimum_size = Vector2(0, 96)
	_device_list.item_selected.connect(_on_device_selected)
	add_child(_device_list)

	_info = Label.new()
	_info.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_info.text = "(no device selected)"
	add_child(_info)

	var live_header := Label.new()
	live_header.text = "Live reading:"
	add_child(live_header)
	_live = Label.new()
	_live.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_live.custom_minimum_size = Vector2(0, 60)
	add_child(_live)

	var actions := HFlowContainer.new()
	_rumble_button = _add_button(actions, "Rumble %.1f s" % RUMBLE_SEC, _on_rumble_pressed)
	_trigger_button = _add_button(actions, "Triggers %.1f s" % RUMBLE_SEC, _on_triggers_pressed)
	_stop_button = _add_button(actions, "Stop", _on_stop_pressed)
	_ffb_button = _add_button(actions, "Force feedback pulse", _on_ffb_pressed)
	add_child(actions)

	var toggles := HFlowContainer.new()
	_events_toggle = CheckBox.new()
	_events_toggle.text = "Event-driven readings"
	_events_toggle.tooltip_text = "GameInput.set_reading_callback_kinds(DEVICE_ANY): every reading arrives through reading_received."
	_events_toggle.toggled.connect(_on_events_toggled)
	toggles.add_child(_events_toggle)
	_background_toggle = CheckBox.new()
	_background_toggle.text = "Background input"
	_background_toggle.tooltip_text = "GameInput.set_focus_policy(FOCUS_POLICY_ENABLE_BACKGROUND_INPUT): keep reading input while the window is unfocused."
	_background_toggle.toggled.connect(_on_background_toggled)
	toggles.add_child(_background_toggle)
	_event_stats = Label.new()
	toggles.add_child(_event_stats)
	add_child(toggles)

	var log_header := Label.new()
	log_header.text = "Device events:"
	add_child(log_header)
	# A scrolling RichTextLabel takes whatever height is left instead of
	# growing the panel past the window as events accumulate.
	_log = RichTextLabel.new()
	_log.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_log.custom_minimum_size = Vector2(0, 80)
	_log.scroll_following = true
	add_child(_log)


func _add_button(parent: Container, text: String, handler: Callable) -> Button:
	var button := Button.new()
	button.text = text
	button.pressed.connect(handler)
	parent.add_child(button)
	return button


func _set_actions_enabled(enabled: bool) -> void:
	for button in [_rumble_button, _trigger_button, _stop_button, _ffb_button]:
		button.disabled = not enabled
	_events_toggle.disabled = not enabled
	_background_toggle.disabled = not enabled


func _current_ids() -> Array:
	var ids := []
	if _gi.is_initialized():
		for device in _gi.get_devices(_c("GameInput", "DEVICE_ANY")):
			ids.append(device.get_device_id())
	return ids


func _refresh_list() -> void:
	_device_list.clear()
	_listed_ids.clear()
	if _gi.is_initialized():
		for device in _gi.get_devices(_c("GameInput", "DEVICE_ANY")):
			_listed_ids.append(device.get_device_id())
			_device_list.add_item("%s  [%s]" % [device.get_display_name(), _flags(device.get_kind_mask(), "kind")])
	if not _listed_ids.has(_selected_id):
		_selected_id = _listed_ids[0] if not _listed_ids.is_empty() else -1
	if _selected_id >= 0:
		_device_list.select(_listed_ids.find(_selected_id))
	_refresh_info()


func _on_device_selected(index: int) -> void:
	if index >= 0 and index < _listed_ids.size():
		_selected_id = _listed_ids[index]
	_refresh_info()


func _selected_device():
	if _selected_id < 0 or not _gi.is_initialized():
		return null
	return _gi.get_device_by_id(_selected_id)


func _refresh_info() -> void:
	var device = _selected_device()
	if device == null:
		_info.text = "No GameInput devices." if _listed_ids.is_empty() else "(no device selected)"
		_set_actions_enabled(_gi.is_initialized())
		_rumble_button.disabled = true
		_trigger_button.disabled = true
		_stop_button.disabled = true
		_ffb_button.disabled = true
		return
	var info: Dictionary = device.get_device_info()
	var rumble: int = device.get_supported_rumble_motors()
	var ffb_motors: int = device.get_force_feedback_motor_count()
	var haptics: Dictionary = device.get_haptic_info()
	var lines := PackedStringArray()
	lines.append("%s (id %d)%s" % [device.get_display_name(), device.get_device_id(),
			"  [mock]" if info.get("is_mock", false) else ""])
	lines.append("family %s  |  VID:PID %04X:%04X  |  status %s" % [
			_flag_name(device.get_device_family(), "family"), int(info.get("vendor_id", 0)),
			int(info.get("product_id", 0)), _flags(device.get_status(), "status")])
	lines.append("kinds %s  |  app-local id %s…" % [_flags(device.get_kind_mask(), "kind"),
			String(device.get_app_local_id()).left(16)])
	lines.append("rumble %s  |  system buttons %s  |  force-feedback motors %d  |  haptics %s" % [
			_flags(rumble, "rumble"), _flags(device.get_supported_system_buttons(), "system"), ffb_motors,
			", ".join(PackedStringArray(haptics.get("locations", []))) if haptics.get("supported", false) else "none"])
	if info.has("keyboard"):
		lines.append("keyboard layout 0x%04X" % device.get_keyboard_layout())
	var a_label: String = device.get_button_label(_c("GameInputDevice", "SRC_BTN_A"))
	if a_label != "":
		lines.append("A button label: %s" % a_label)
	_info.text = "\n".join(lines)
	_set_actions_enabled(true)
	var low_high := _c("GameInputDevice", "RUMBLE_LOW_FREQUENCY") | _c("GameInputDevice", "RUMBLE_HIGH_FREQUENCY")
	var triggers := _c("GameInputDevice", "RUMBLE_LEFT_TRIGGER") | _c("GameInputDevice", "RUMBLE_RIGHT_TRIGGER")
	_rumble_button.disabled = rumble & low_high == 0
	_trigger_button.disabled = rumble & triggers == 0
	_stop_button.disabled = rumble == 0 and ffb_motors == 0
	_ffb_button.disabled = ffb_motors == 0


func _describe_live(device) -> String:
	if device == null:
		return "-"
	var reading = _gi.get_current_reading(device)
	if reading == null:
		return "(no reading yet)"
	var kinds: int = reading.get_input_kinds()
	var lines := PackedStringArray()
	if kinds & _c("GameInput", "DEVICE_GAMEPAD"):
		lines.append("buttons: %s" % _flags(reading.get_buttons_mask(), "button", "-"))
		lines.append("LS (%+.2f, %+.2f)  RS (%+.2f, %+.2f)  LT %.2f  RT %.2f" % [
				_axis(reading, "AXIS_LEFT_X"), _axis(reading, "AXIS_LEFT_Y"),
				_axis(reading, "AXIS_RIGHT_X"), _axis(reading, "AXIS_RIGHT_Y"),
				_axis(reading, "AXIS_LEFT_TRIGGER"), _axis(reading, "AXIS_RIGHT_TRIGGER")])
	if kinds & _c("GameInput", "DEVICE_KEYBOARD"):
		var keys := PackedStringArray()
		for key in reading.get_pressed_physical_keys():
			keys.append(OS.get_keycode_string(key))
		lines.append("keys: %s" % (" ".join(keys) if not keys.is_empty() else "-"))
	if kinds & _c("GameInput", "DEVICE_MOUSE"):
		var mouse := "mouse: %s  delta %s  wheel %s" % [_flags(reading.get_mouse_buttons(), "mouse", "-"),
				reading.get_mouse_delta(), reading.get_mouse_wheel_delta()]
		if reading.has_mouse_absolute_position():
			mouse += "  absolute %s" % reading.get_mouse_absolute_position()
		lines.append(mouse)
	if kinds & _c("GameInput", "DEVICE_SENSORS"):
		lines.append("accel %s m/s²  gyro %s rad/s  heading %.0f°" % [
				_vec3(reading.get_accelerometer()), _vec3(reading.get_gyroscope()), reading.get_heading_degrees()])
	if kinds & _c("GameInput", "DEVICE_ARCADE_STICK"):
		lines.append("arcade: %s" % _flags(reading.get_arcade_stick_buttons(), "arcade", "-"))
	if kinds & _c("GameInput", "DEVICE_FLIGHT_STICK"):
		lines.append("flight: %s  hat %s  roll %+.2f pitch %+.2f yaw %+.2f throttle %.2f" % [
				_flags(reading.get_flight_stick_buttons(), "flight", "-"), reading.get_flight_stick_hat_vector(),
				_axis(reading, "AXIS_FLIGHT_ROLL"), _axis(reading, "AXIS_FLIGHT_PITCH"),
				_axis(reading, "AXIS_FLIGHT_YAW"), _axis(reading, "AXIS_FLIGHT_THROTTLE")])
	if kinds & _c("GameInput", "DEVICE_RACING_WHEEL"):
		lines.append("wheel %+.2f  throttle %.2f  brake %.2f  clutch %.2f  handbrake %.2f  gear %d  %s" % [
				_axis(reading, "AXIS_WHEEL"), _axis(reading, "AXIS_THROTTLE"), _axis(reading, "AXIS_BRAKE"),
				_axis(reading, "AXIS_CLUTCH"), _axis(reading, "AXIS_HANDBRAKE"), reading.get_racing_wheel_gear(),
				_flags(reading.get_racing_wheel_buttons(), "wheel", "")])
	if kinds & _c("GameInput", "DEVICE_CONTROLLER"):
		var axes := PackedStringArray()
		for value in reading.get_controller_axes():
			axes.append("%.2f" % value)
			if axes.size() == 8:
				break
		var pressed := PackedStringArray()
		var buttons: Array = reading.get_controller_buttons()
		for i in buttons.size():
			if buttons[i]:
				pressed.append(str(i))
		lines.append("raw axes [%s]  buttons [%s]  switches %s" % [", ".join(axes), " ".join(pressed),
				Array(reading.get_controller_switches())])
	if lines.is_empty():
		return "(reading has no state for this device's kinds)"
	return "\n".join(lines)


func _sync_toggles() -> void:
	if not _gi.is_initialized():
		return
	_events_toggle.set_pressed_no_signal(_gi.get_reading_callback_kinds() != 0)
	_background_toggle.set_pressed_no_signal(
			_gi.get_focus_policy() & _c("GameInput", "FOCUS_POLICY_ENABLE_BACKGROUND_INPUT") != 0)


# ── Actions ──────────────────────────────────────────────────────────────

func _on_rumble_pressed() -> void:
	var device = _selected_device()
	if device != null:
		var sent: bool = device.start_vibration(0.5, 0.5, RUMBLE_SEC)
		_append_log("rumble 0.5 / 0.5 for %.1f s on %s%s"
				% [RUMBLE_SEC, device.get_display_name(), "" if sent else " (refused)"])


func _on_triggers_pressed() -> void:
	var device = _selected_device()
	if device != null:
		var sent: bool = device.start_vibration(0.0, 0.0, RUMBLE_SEC, 0.6, 0.6)
		_append_log("impulse triggers 0.6 / 0.6 for %.1f s on %s%s"
				% [RUMBLE_SEC, device.get_display_name(), "" if sent else " (refused)"])


func _on_stop_pressed() -> void:
	var device = _selected_device()
	if device != null:
		device.stop_vibration()
	if _effect != null:
		_effect.release()
		_effect = null
	_append_log("stopped")


func _on_ffb_pressed() -> void:
	var device = _selected_device()
	if device == null or device.get_force_feedback_motor_count() == 0:
		return
	var constant := _c("GameInputForceFeedbackEffect", "EFFECT_CONSTANT")
	if not device.get_force_feedback_motor_info(0).get("supported_effects", []).has(constant):
		_append_log("motor 0 has no constant-force effect")
		return
	if _effect != null:
		_effect.release()
	_effect = device.create_force_feedback_effect(0, {
		"kind": constant, "magnitude": 0.3, "sustain_duration": FFB_PULSE_SEC,
	})
	if _effect == null:
		_append_log("GameInput refused the force-feedback effect")
		return
	if not _effect.start():
		_append_log("GameInput refused to start the force-feedback effect")
		_effect.release()
		_effect = null
		return
	_effect_release_msec = Time.get_ticks_msec() + int((FFB_PULSE_SEC + 0.2) * 1000)
	_append_log("constant force 0.3 for %.1f s on motor 0" % FFB_PULSE_SEC)


func _on_events_toggled(pressed: bool) -> void:
	if _gi.set_reading_callback_kinds(_c("GameInput", "DEVICE_ANY") if pressed else 0):
		_append_log("event-driven readings %s" % ("on" if pressed else "off"))
		return
	# The kinds read back as requested even when registration failed, so turn
	# them off again rather than leave a toggle on that delivers nothing.
	_gi.set_reading_callback_kinds(0)
	_events_toggle.set_pressed_no_signal(false)
	_append_log("GameInput refused event-driven readings (RegisterReadingCallback failed)")


func _on_background_toggled(pressed: bool) -> void:
	var background := _c("GameInput", "FOCUS_POLICY_ENABLE_BACKGROUND_INPUT")
	var policy: int = _gi.get_focus_policy()
	_gi.set_focus_policy(policy | background if pressed else policy & ~background)
	_append_log("background input %s" % ("on" if pressed else "off"))


# ── Signal handlers ──────────────────────────────────────────────────────

func _on_device_connected(device) -> void:
	_append_log("connected: %s (id %d)" % [device.get_display_name(), device.get_device_id()])
	_refresh_list()


func _on_device_disconnected(device_id: int) -> void:
	_append_log("disconnected: id %d" % device_id)
	_refresh_list()


func _on_device_status_changed(device, status: int, previous: int, _timestamp: int) -> void:
	_append_log("status: %s %s -> %s" % [device.get_display_name(), _flags(previous, "status"), _flags(status, "status")])
	if device.get_device_id() == _selected_id:
		_refresh_info()


func _on_reading_received(_device, _reading) -> void:
	_events_this_window += 1


func _on_system_buttons_changed(device, buttons: int, _previous: int, _timestamp: int) -> void:
	_append_log("system buttons: %s %s" % [device.get_display_name(), _flags(buttons, "system", "released")])


func _on_keyboard_layout_changed(device, layout: int, previous: int, _timestamp: int) -> void:
	_append_log("keyboard layout: %s 0x%04X -> 0x%04X" % [device.get_display_name(), previous, layout])


# ── Formatting ───────────────────────────────────────────────────────────

func _append_log(line: String) -> void:
	_log_lines.append(line)
	while _log_lines.size() > LOG_LINES:
		_log_lines.pop_front()
	_log.text = "\n".join(PackedStringArray(_log_lines))


func _c(native_class: String, constant_name: String) -> int:
	return AddonApi.constant(native_class, constant_name)


func _axis(reading, axis_name: String) -> float:
	return reading.get_axis(_c("GameInputDevice", axis_name))


func _vec3(v: Vector3) -> String:
	return "(%.2f, %.2f, %.2f)" % [v.x, v.y, v.z]


## Maps each value of a native enum to its constant name without [param prefix].
func _enum_names(native_class: String, enum_name: String, prefix: String, lower: bool = false) -> Dictionary:
	var names := {}
	if not ClassDB.class_exists(native_class):
		return names
	for constant_name in ClassDB.class_get_enum_constants(native_class, enum_name, true):
		var value: int = ClassDB.class_get_integer_constant(native_class, constant_name)
		if not names.has(value):
			var short: String = constant_name.trim_prefix(prefix)
			names[value] = short.to_lower() if lower else short
	return names


func _flag_name(value: int, table: String) -> String:
	return _names.get(table, {}).get(value, str(value))


## Names the set bits of [param mask] using an enum table, lowest bit first.
func _flags(mask: int, table: String, empty: String = "none") -> String:
	var names: Dictionary = _names.get(table, {})
	var parts := PackedStringArray()
	for bit in 32:
		var flag := 1 << bit
		if mask & flag and names.has(flag):
			parts.append(names[flag])
	if parts.is_empty():
		return empty if mask == 0 else "0x%X" % mask
	return " ".join(parts)
