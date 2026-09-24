extends SceneTree
## Exit regression: GDScript lambdas still connected to GameInput signals at
## quit must not crash Godot.
##
## The singleton is deleted at SCENE deinitialization, after
## ScriptServer::finish_languages(). A lambda that does not capture `self`
## has no target object, so nothing disconnects it before then. Before the
## addon registered a main-loop shutdown callback, destroying that connection
## after GDScript had shut down crashed the process on exit with 0xC0000005
## (the v1 DLL does the same). The bootstrap stage of tools/run_all_tests.ps1
## fails on any non-zero exit, so this script only has to leave its lambdas
## connected and quit.
##
## Lambdas that capture `self` are disconnected when their object is freed,
## so they never reproduced the crash; the ones below capture locals only.
##
## Debug builds route a device through the mock backend so the lambdas have
## run and hold device wrappers before quit. Release builds have no mock
## seams; connecting alone reproduces the original crash.


func _initialize() -> void:
	_run.call_deferred()


func _run() -> void:
	var gi: Object = Engine.get_singleton("GameInput") if Engine.has_singleton("GameInput") else null
	if gi == null:
		printerr("[exit_with_connected_lambdas] GameInput singleton is not registered")
		quit(1)
		return

	var seen: Array = []
	# Deliberately never disconnected.
	gi.device_connected.connect(func(d): seen.append(["device_connected", d]))
	gi.device_disconnected.connect(func(id): seen.append(["device_disconnected", id]))
	gi.device_status_changed.connect(func(d, s, p, t): seen.append(["device_status_changed", d, s, p, t]))
	gi.reading_received.connect(func(d, r): seen.append(["reading_received", d, r]))
	gi.system_buttons_changed.connect(func(d, b, p, t): seen.append(["system_buttons_changed", d, b, p, t]))
	gi.keyboard_layout_changed.connect(func(d, l, p, t): seen.append(["keyboard_layout_changed", d, l, p, t]))

	if gi.has_method("_test_initialize_mock"):
		gi.shutdown()
		gi._test_initialize_mock()
		var id: int = gi._test_inject_device({"name": "Exit regression pad"})
		gi._test_force_poll()
		if id <= 0 or seen.is_empty():
			printerr("[exit_with_connected_lambdas] mock device_connected did not reach the lambda")
			quit(1)
			return
		# The mock session is left running too: the shutdown hook owns it.
	else:
		gi.initialize()
		for i in 3:
			await process_frame
			gi.poll()

	print("[exit_with_connected_lambdas] quitting with %d connection(s) and %d event(s) captured" % [
		_connection_count(gi), seen.size()])
	quit(0)


func _connection_count(gi: Object) -> int:
	var total := 0
	for s in gi.get_signal_list():
		total += gi.get_signal_connection_list(s["name"]).size()
	return total
