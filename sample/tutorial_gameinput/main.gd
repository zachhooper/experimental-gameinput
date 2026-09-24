extends Control

const AddonApi = preload("res://addon_api.gd")
const SelfTest = preload("res://selftest/gameinput_selftest.gd")

## GameInput action bridge — standalone tutorial sample.
##
## Builds a GameInputActionMap programmatically (matching the Step 2
## table in the tutorial), attaches a GameInputMapper that polls every
## frame, and renders live action state + device hot-plug events.
## Jumping sends a short rumble to the gamepad (Step 7), and the
## Inspector panel on the right shows every GameInput device live.
##
## Run with `-- --gameinput-selftest` to check the whole integration
## automatically instead (see selftest/gameinput_selftest.gd).
##
## Independent of GDK / PlayFab — no sign-in flow.
##
## Source: docs/tutorials/gameinput-action-bridge.md

@onready var _runtime_status: Label = $Root/RuntimeStatus
@onready var _device_count: Label = $Root/DeviceCount
@onready var _devices: Label = $Root/Devices
@onready var _action_state: Label = $Root/ActionState
@onready var _hotplug_log: RichTextLabel = $Root/HotplugLog
@onready var _floor: ColorRect = $Floor
@onready var _player: ColorRect = $Player
@onready var _inspector: Control = $Inspector

const PLAYER_SPEED := 240.0
const PLAYER_JUMP_VELOCITY := -480.0
const PLAYER_GRAVITY := 1200.0

# Step 7: a short thump on the gamepad when the player jumps.
const JUMP_RUMBLE_WEAK := 0.2
const JUMP_RUMBLE_STRONG := 0.4
const JUMP_RUMBLE_SEC := 0.12

var _player_velocity_y: float = 0.0
var _mapper = null

func _ready() -> void:
	if SelfTest.is_requested():
		var self_test := SelfTest.new()
		self_test.name = "SelfTest"
		self_test.sample = self
		add_child(self_test)

	if not Engine.has_singleton("GameInput"):
		_runtime_status.text = "GameInput singleton missing. Build the addon (cmake --build build --preset debug)."
		_device_count.text = ""
		_devices.text = ""
		_action_state.text = ""
		return

	if not AddonApi.singleton("GameInput").is_initialized():
		push_warning("[Pad] GameInput runtime not available — gamepad input disabled.")
		_runtime_status.text = "GameInput runtime NOT initialized (set game_input/runtime/initialize_on_startup=true)."
	else:
		_runtime_status.text = "GameInput runtime initialized."

	_mapper = AddonApi.instantiate("GameInputMapper")
	_mapper.name = "GamepadMapper"
	_mapper.action_map = _build_default_map()
	add_child(_mapper)

	AddonApi.singleton("GameInput").device_connected.connect(_on_device_connected)
	AddonApi.singleton("GameInput").device_disconnected.connect(_on_device_disconnected)

	# Seed the UI with whatever was connected before _ready.
	_refresh_devices()
	_append_hotplug("Seeded with %d gamepad(s) at startup" % AddonApi.singleton("GameInput").get_connected_device_count(
			AddonApi.constant("GameInput", "DEVICE_GAMEPAD")))

	# Position the player on the floor, in the middle of the left (play) half.
	_player.position = Vector2(_play_area_width() * 0.5, _floor_y())

func _build_default_map():
	var map := AddonApi.instantiate("GameInputActionMap")

	var accept := AddonApi.instantiate("GameInputBinding")
	accept.action = &"ui_accept"
	accept.source = AddonApi.constant("GameInputDevice", "SRC_BTN_A")
	map.add_binding(accept)

	var jump := AddonApi.instantiate("GameInputBinding")
	jump.action = &"jump"
	jump.source = AddonApi.constant("GameInputDevice", "SRC_BTN_A")
	map.add_binding(jump)

	var left := AddonApi.instantiate("GameInputBinding")
	left.action = &"move_left"
	left.source = AddonApi.constant("GameInputDevice", "SRC_AXIS_LEFT_X")
	left.is_axis = true
	left.axis_invert = true
	map.add_binding(left)

	var right := AddonApi.instantiate("GameInputBinding")
	right.action = &"move_right"
	right.source = AddonApi.constant("GameInputDevice", "SRC_AXIS_LEFT_X")
	right.is_axis = true
	map.add_binding(right)

	return map

func _physics_process(delta: float) -> void:
	if not Engine.has_singleton("GameInput"):
		return
	var direction: float = Input.get_action_strength("move_right") - Input.get_action_strength("move_left")
	_player.position.x += direction * PLAYER_SPEED * delta

	if Input.is_action_just_pressed("jump") and is_player_on_floor():
		_player_velocity_y = PLAYER_JUMP_VELOCITY
		_rumble_pad()

	_player_velocity_y += PLAYER_GRAVITY * delta
	_player.position.y += _player_velocity_y * delta
	if _player.position.y >= _floor_y():
		_player.position.y = _floor_y()
		_player_velocity_y = 0.0

	_player.position.x = clamp(_player.position.x, 0.0, get_player_max_x())

func _process(_delta: float) -> void:
	if not Engine.has_singleton("GameInput"):
		return
	_action_state.text = (
			"move_left=%.2f  move_right=%.2f  jump=%s  ui_accept=%s"
			% [
				Input.get_action_strength("move_left"),
				Input.get_action_strength("move_right"),
				str(Input.is_action_pressed("jump")),
				str(Input.is_action_pressed("ui_accept")),
			]
	)

func _on_device_connected(device) -> void:
	_append_hotplug("connected: id=%d (%s)" % [device.get_device_id(), device.get_display_name()])
	_refresh_devices()

func _on_device_disconnected(device_id: int) -> void:
	_append_hotplug("disconnected: id=%d" % device_id)
	_refresh_devices()

func _refresh_devices() -> void:
	var count: int = AddonApi.singleton("GameInput").get_connected_device_count(
			AddonApi.constant("GameInput", "DEVICE_GAMEPAD"))
	_device_count.text = "Connected gamepads: %d" % count

	var lines := PackedStringArray()
	for device in AddonApi.singleton("GameInput").get_devices(AddonApi.constant("GameInput", "DEVICE_GAMEPAD")):
		lines.append("- id=%d %s" % [device.get_device_id(), device.get_display_name()])
	if lines.is_empty():
		lines.append("- (none — plug in a gamepad)")
	_devices.text = "\n".join(lines)

func _append_hotplug(line: String) -> void:
	_hotplug_log.append_text(line + "\n")

## Step 7: rumble the gamepad that drives the mapper. The duration makes
## GameInput.poll() stop the motors, so there is no timer to manage here.
func _rumble_pad() -> void:
	var pad = _mapper_device()
	if pad != null and pad.supports_vibration():
		pad.start_vibration(JUMP_RUMBLE_WEAK, JUMP_RUMBLE_STRONG, JUMP_RUMBLE_SEC)

func _mapper_device():
	var game_input := AddonApi.singleton("GameInput")
	if _mapper == null or game_input == null or not game_input.is_initialized():
		return null
	if _mapper.target_device_id >= 0:
		return game_input.get_device_by_id(_mapper.target_device_id)
	return game_input.get_primary_device(AddonApi.constant("GameInput", "DEVICE_GAMEPAD"))

# Read-only views for selftest/gameinput_selftest.gd.

func get_mapper():
	return _mapper

func get_inspector() -> Control:
	return _inspector

func get_devices_text() -> String:
	return _devices.text

func get_device_count_text() -> String:
	return _device_count.text

func get_hotplug_text() -> String:
	return _hotplug_log.get_parsed_text()

func is_player_on_floor() -> bool:
	return _player.position.y >= _floor_y()

func get_player_position() -> Vector2:
	return _player.position

func get_player_max_x() -> float:
	return _play_area_width() - _player.size.x

# The Inspector panel covers the right half of the window.
func _play_area_width() -> float:
	return get_viewport_rect().size.x * 0.5

# The player stands on the Floor line under the text column.
func _floor_y() -> float:
	return _floor.position.y - _player.size.y
