#ifndef GODOT_GAMEINPUT_KEYMAP_H
#define GODOT_GAMEINPUT_KEYMAP_H

#include <cstdint>

namespace gameinput_internal {

// Translates GameInput keyboard data into Godot key codes. The tables mirror
// Godot's own Windows mapping (platform/windows/key_mapping_windows.cpp) so a
// key reported by GameInput compares equal to InputEventKey.physical_keycode /
// keycode for the same key.

// Godot physical key (a godot::Key value) for a GameInput scan code, or 0
// (KEY_NONE) when the code is unknown. Extended keys are accepted in both the
// 0xE0xx form and the 0x1xx form; 0xE1xx (the Pause sequence) maps to
// KEY_PAUSE.
int64_t scan_code_to_key(uint32_t scan_code);

// Godot key (a godot::Key value) for a Windows virtual-key code, or 0 when the
// code is unknown. Left/right modifier codes map to the plain modifier key.
int64_t virtual_key_to_key(uint32_t virtual_key);

// godot::KeyLocation for a scan code: 1 = left, 2 = right, 0 = unspecified.
int64_t scan_code_to_location(uint32_t scan_code);

// Splits a scan code into the base code and an "extended" flag.
bool split_scan_code(uint32_t scan_code, uint32_t *out_base, bool *out_extended,
                     bool *out_pause_sequence);

} // namespace gameinput_internal

#endif // GODOT_GAMEINPUT_KEYMAP_H
