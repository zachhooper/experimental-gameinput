extends SceneTree
## Exit regression: a callback registration that GameInput never removed must
## not crash Godot on the way out.
##
## When UnregisterCallback() still fails at shutdown, the addon abandons the
## registration: its callback gate is never freed and the addon DLL is pinned,
## so a late callback always lands in mapped code, finds the gate closed and
## returns. A pinned DLL is not unloaded when Godot releases the extension, so
## its static destructors run during process exit instead. The bootstrap stage
## of tools/run_all_tests.ps1 fails on any non-zero exit, so this script forces
## the abandon path, leaves a new session running and quits.
##
## Release builds have no failure seam; they only check that quitting works.


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	var gi: Object = Engine.get_singleton("GameInput") if Engine.has_singleton("GameInput") else null
	if gi == null:
		printerr("[exit_with_abandoned_callback] GameInput singleton is not registered")
		quit(1)
		return
	if not gi.has_method("_test_fail_next_unregisters"):
		print("[exit_with_abandoned_callback] release build: no failure seam, quitting")
		quit(0)
		return

	var gamepad: int = ClassDB.class_get_integer_constant("GameInput", "DEVICE_GAMEPAD")
	var seen: Array = []
	gi.reading_received.connect(func(d, r): seen.append([d, r]))

	gi.shutdown()
	gi.set_reading_callback_kinds(gamepad)
	gi._test_initialize_mock()
	var before: Dictionary = gi._test_get_unregister_state()
	gi._test_fail_next_unregisters(3)
	gi.shutdown()
	var after: Dictionary = gi._test_get_unregister_state()
	if after["abandoned"] - before["abandoned"] != 1 or not after["module_pinned"]:
		printerr("[exit_with_abandoned_callback] expected one abandoned registration and a pinned module, got %s" % after)
		quit(1)
		return

	# The shutdown hook owns this second session.
	gi._test_initialize_mock()
	var id: int = gi._test_inject_device({"name": "Abandoned callback pad"})
	gi._test_force_poll()
	gi._test_push_reading(id, {"gamepad": {"buttons": 1}})
	gi._test_force_poll()
	if seen.size() != 1:
		printerr("[exit_with_abandoned_callback] the new session delivered %d reading(s), expected 1" % seen.size())
		quit(1)
		return

	print("[exit_with_abandoned_callback] quitting with the module pinned and %d abandoned registration(s)" % after["abandoned"])
	quit(0)
