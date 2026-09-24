extends GutTest
## Shared GUT base for the `godot_gameinput` coverage suite.
##
## DOES NOT extend `XboxTestBase`. The GameInput addon is standalone — no
## build-time or runtime dependency on `godot_gdk` (per
## `.github/instructions/godot-gameinput.instructions.md`). Pulling in the
## GDK base would force every gameinput test host to also resolve the GDK
## addon, which violates that contract.
##
## Wave 3 GameInput tests should
## `extends "res://addons/godot_gdk_tests/gameinput_test_base.gd"`.

const TestEnv = preload("res://addons/godot_gdk_tests/test_env.gd")

const FLOAT_EPSILON := 0.0001

const GAMEINPUT_SINGLETON_NAME_SETTING := "game_input/runtime/singleton_name"
const GAMEINPUT_DEFAULT_SINGLETON_NAME := "GameInput"
# Native class the singleton must be an instance of. The singleton *name* is
# configurable; the class it resolves to is not.
const GAMEINPUT_SINGLETON_CLASS_NAME := "GameInput"


# ── Singleton helpers ────────────────────────────────────────────────────

## Returns the Engine singleton name configured by
## `game_input/runtime/singleton_name`, falling back to `"GameInput"`. Kept in
## sync with the resolution in the addon's `register_types.cpp` and
## `gameinput_bootstrap.gd`.
func gameinput_singleton_name() -> String:
	var configured := str(
			ProjectSettings.get_setting(
					GAMEINPUT_SINGLETON_NAME_SETTING, GAMEINPUT_DEFAULT_SINGLETON_NAME)).strip_edges()
	if configured.is_empty() or not configured.is_valid_ascii_identifier():
		return GAMEINPUT_DEFAULT_SINGLETON_NAME
	return configured


# Resolves the singleton by configured name, retrying under the default name
# because the C++ side falls back to "GameInput" when the configured name is
# unusable. Candidates are class-checked so a configured name that collides
# with an unrelated engine singleton (say `Input`) is not mistaken for the
# runtime.
func get_gameinput():
	var configured := gameinput_singleton_name()
	if Engine.has_singleton(configured):
		var candidate: Object = Engine.get_singleton(configured)
		if candidate != null and candidate.is_class(GAMEINPUT_SINGLETON_CLASS_NAME):
			return candidate
	if configured != GAMEINPUT_DEFAULT_SINGLETON_NAME and Engine.has_singleton(GAMEINPUT_DEFAULT_SINGLETON_NAME):
		var fallback: Object = Engine.get_singleton(GAMEINPUT_DEFAULT_SINGLETON_NAME)
		if fallback != null and fallback.is_class(GAMEINPUT_SINGLETON_CLASS_NAME):
			return fallback
	return null


## Returns a project setting's registered default (its revert value) rather than
## its current value, so registration contracts can be asserted even when a test
## or a crashed earlier run left the live value changed. Mirrors the helper on
## `gdk_test_base.gd`, which this base deliberately does not extend.
func get_setting_default(setting_name: String):
	if ProjectSettings.property_can_revert(setting_name):
		return ProjectSettings.property_get_revert(setting_name)
	return null


# Pending the current test if the GameInput singleton is unavailable.
# Returns true when the runtime is missing (caller should `return` after).
func pending_unless_runtime_available() -> bool:
	if get_gameinput() == null:
		pending("GameInput singleton is not available in this host")
		return true
	return false


# ── Mock backend (debug builds only) ─────────────────────────────────────
# `GameInput._test_*` seams exist only in debug builds of the addon (they sit
# under `#ifndef NDEBUG`, like the GameInputMapper seams) and are not part of
# the documented API. They swap the native runtime for a scripted one that
# feeds the same queues, so connect/disconnect, readings, rumble and force
# feedback run deterministically on any host — including CI runners and
# locked sessions, where GameInput delivers no input at all.

const MOCK_BACKEND := 2

## Starts a clean mock session and returns the singleton. Returns null after
## marking the test pending when the host has no singleton or loaded a
## release build (no seams). Runtime overrides from earlier tests are reset.
func begin_mock_session():
	var gi = get_gameinput()
	if gi == null:
		pending("GameInput singleton is not available in this host")
		return null
	if not gi.has_method("_test_initialize_mock"):
		pending("GameInput mock seams are debug-only; this host loaded a release build")
		return null
	gi.shutdown()
	gi.set_reading_callback_kinds(0)
	gi.set_focus_policy(0)
	gi._test_initialize_mock()
	return gi


## Tears a mock session down and clears the runtime overrides it may have set.
func end_mock_session(gi) -> void:
	if gi == null:
		return
	gi.shutdown()
	gi.set_reading_callback_kinds(0)
	gi.set_focus_policy(0)


## Injects a mock device and drains its connect event. Returns the
## GameInputDevice wrapper, or null when the info was rejected.
func add_mock_device(gi, info: Dictionary = {}):
	var id: int = gi._test_inject_device(info)
	if id <= 0:
		return null
	gi._test_force_poll()
	return gi.get_device_by_id(id)


## Pushes one mock reading (native GameInput conventions: stick up is
## positive, mouse positions accumulate) and runs a poll so the polled
## reading and any reading_received signals reflect it.
func push_mock_reading(gi, device, state: Dictionary) -> bool:
	var ok: bool = gi._test_push_reading(device.get_device_id(), state)
	gi._test_force_poll()
	return ok


## Pushes a mock reading and returns the polled GameInputReading after it.
func mock_reading(gi, device, state: Dictionary):
	push_mock_reading(gi, device, state)
	return gi.get_current_reading(device)


## Records every GameInput signal in emission order as
## `[signal_name, arg0, arg1, ...]`. Call `stop()` when done.
class GameInputSignalLog:
	var events: Array = []
	var _gi: Object
	var _connections: Array = []

	func _init(gi: Object) -> void:
		_gi = gi
		_watch("device_connected", func(d): events.append(["device_connected", d]))
		_watch("device_disconnected", func(id): events.append(["device_disconnected", id]))
		_watch("device_status_changed", func(d, s, p, t): events.append(["device_status_changed", d, s, p, t]))
		_watch("reading_received", func(d, r): events.append(["reading_received", d, r]))
		_watch("system_buttons_changed", func(d, b, p, t): events.append(["system_buttons_changed", d, b, p, t]))
		_watch("keyboard_layout_changed", func(d, l, p, t): events.append(["keyboard_layout_changed", d, l, p, t]))

	func _watch(signal_name: String, callable: Callable) -> void:
		_gi.connect(signal_name, callable)
		_connections.append([signal_name, callable])

	func named(signal_name: String) -> Array:
		return events.filter(func(e): return e[0] == signal_name)

	func names() -> Array:
		return events.map(func(e): return e[0])

	func clear() -> void:
		events.clear()

	func stop() -> void:
		for c in _connections:
			if _gi.is_connected(c[0], c[1]):
				_gi.disconnect(c[0], c[1])
		_connections.clear()


func watch_gameinput_signals(gi) -> GameInputSignalLog:
	return GameInputSignalLog.new(gi)


# ── Float comparison sugar ───────────────────────────────────────────────
# C++ float properties round-trip through 32-bit storage and won't equal
# 64-bit double literals exactly. This is the canonical `assert_eq_approx`
# referenced in `.github/instructions/godot-gameinput.instructions.md`.

func assert_eq_approx(actual: float, expected: float, name: String, eps: float = FLOAT_EPSILON) -> void:
	if absf(actual - expected) <= eps:
		assert_true(true, "%s ≈ %s" % [name, str(expected)])
	else:
		assert_true(false, "%s expected ≈ %s, got %s" % [name, str(expected), str(actual)])


# ── Reflection / class-introspection sugar ───────────────────────────────

func assert_has_method_named(obj: Object, method_name: String, test_name: String = "") -> void:
	var label := test_name if test_name else "%s.%s() exists" % [obj.get_class(), method_name]
	assert_true(obj.has_method(method_name), label)


func assert_has_signal_named(obj: Object, signal_name: String, test_name: String = "") -> void:
	var label := test_name if test_name else "%s.%s signal exists" % [obj.get_class(), signal_name]
	assert_true(obj.has_signal(signal_name), label)


# ── TestEnv convenience wrappers ─────────────────────────────────────────

# Tier=live_read. Returns true when LIVE_TESTS=1 is set (the test should
# proceed). Returns false when not set (and marks the current test pending
# with a clear reason — caller should `return` immediately after).
func requires_live() -> bool:
	if not TestEnv.live_tests_enabled():
		pending("Tier=live_read: skipped without LIVE_TESTS=1. Run via `tools\\run_all_tests.ps1 -Live`.")
		return false
	return true


# Tier=live_write. Returns true when both LIVE_TESTS=1 and LIVE_WRITE_TESTS=1
# are set. Tests that write state persisting in the live PlayFab title
# (create lobby, post leaderboard entry, save Game Save, …) MUST gate on
# this rather than on requires_live(), so they only run when the developer
# has explicitly opted into the live-write tier against a sandbox title.
func requires_live_write() -> bool:
	if not TestEnv.live_tests_enabled():
		pending("Tier=live_write: skipped without LIVE_TESTS=1.")
		return false
	if not TestEnv.live_write_tests_enabled():
		pending("Tier=live_write: skipped without LIVE_WRITE_TESTS=1. Run via `tools\\run_all_tests.ps1 -Live -AllowLiveWrites -PlayFabTitleId <sandbox-title>` against a sandbox title.")
		return false
	return true


func pending_unless_live() -> bool:
	return not requires_live()


func pending_unless_live_write() -> bool:
	return not requires_live_write()



func with_unique_id(prefix: String) -> String:
	return prefix + "-" + TestEnv.unique_run_id()
