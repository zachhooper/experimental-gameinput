extends GutTest

## GUT drops a suite that does not compile with one "Ignoring script" line and
## still exits 0, so a broken suite silently leaves every run.
## test_gameinput_mapper_stuck_actions.gd did, from the day it was added.
## This suite fails instead: every res://tests/test_*.gd must compile and
## extend GutTest. The bootstrap/ mini-runners extend SceneTree and
## run_all_tests.ps1 runs them separately, so they are not checked here.

const SUITE_DIR := "res://tests"


func test_every_suite_compiles_and_extends_gut_test() -> void:
	var files := DirAccess.get_files_at(SUITE_DIR)
	assert_has(files, get_script().resource_path.get_file(), "lists the suite directory")
	var broken := PackedStringArray()
	for file in files:
		if not file.begins_with("test_") or file.get_extension() != "gd":
			continue
		var script = load(SUITE_DIR.path_join(file))
		if not (script is GDScript) or not script.can_instantiate():
			broken.append("%s does not compile" % file)
		elif not _extends_gut_test(script):
			broken.append("%s does not extend GutTest" % file)
	assert_eq(broken, PackedStringArray(), "every suite compiles and extends GutTest")


func _extends_gut_test(script: Script) -> bool:
	var current := script
	while current != null:
		if current.get_global_name() == &"GutTest":
			return true
		current = current.get_base_script()
	return false
