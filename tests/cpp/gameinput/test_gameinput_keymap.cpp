// Doctest coverage for the GameInput keyboard tables and label names.
//
// gameinput_keymap.cpp mirrors Godot's own Windows tables
// (platform/windows/key_mapping_windows.cpp) so a key reported by GameInput
// compares equal to InputEventKey.physical_keycode / keycode for the same key.
// gameinput_labels.h names GameInputLabel values for glyph selection.

#include "doctest.h"

#include "gameinput_keymap.h"
#include "gameinput_labels.h"

#include <godot_cpp/classes/global_constants.hpp>

#include <cstring>

namespace gi = gameinput_internal;
using namespace godot;

TEST_CASE("scan_code_to_key: base scan codes") {
    CHECK(gi::scan_code_to_key(0x1E) == KEY_A);
    CHECK(gi::scan_code_to_key(0x2C) == KEY_Z);
    CHECK(gi::scan_code_to_key(0x02) == KEY_1);
    CHECK(gi::scan_code_to_key(0x1C) == KEY_ENTER);
    CHECK(gi::scan_code_to_key(0x39) == KEY_SPACE);
    CHECK(gi::scan_code_to_key(0x01) == KEY_ESCAPE);
    CHECK(gi::scan_code_to_key(0x3B) == KEY_F1);
    CHECK(gi::scan_code_to_key(0x58) == KEY_F12);
    CHECK(gi::scan_code_to_key(0x48) == KEY_KP_8);
    CHECK(gi::scan_code_to_key(0x45) == KEY_NUMLOCK);
}

TEST_CASE("scan_code_to_key: extended keys in both encodings") {
    SUBCASE("0xE0xx form") {
        CHECK(gi::scan_code_to_key(0xE01C) == KEY_KP_ENTER);
        CHECK(gi::scan_code_to_key(0xE048) == KEY_UP);
        CHECK(gi::scan_code_to_key(0xE050) == KEY_DOWN);
        CHECK(gi::scan_code_to_key(0xE04B) == KEY_LEFT);
        CHECK(gi::scan_code_to_key(0xE04D) == KEY_RIGHT);
        CHECK(gi::scan_code_to_key(0xE053) == KEY_DELETE);
        CHECK(gi::scan_code_to_key(0xE01D) == KEY_CTRL);
        CHECK(gi::scan_code_to_key(0xE038) == KEY_ALT);
    }
    SUBCASE("0x1xx form") {
        CHECK(gi::scan_code_to_key(0x11C) == KEY_KP_ENTER);
        CHECK(gi::scan_code_to_key(0x148) == KEY_UP);
    }
    SUBCASE("unlisted extended codes fall back to the base table") {
        CHECK(gi::scan_code_to_key(0xE01E) == KEY_A);
    }
}

TEST_CASE("scan_code_to_key: pause sequence and unknown codes") {
    CHECK(gi::scan_code_to_key(0xE11D45) == KEY_PAUSE);
    CHECK(gi::scan_code_to_key(0xE11D) == KEY_PAUSE);
    CHECK(gi::scan_code_to_key(0) == KEY_NONE);
    CHECK(gi::scan_code_to_key(0x7F1E) == KEY_NONE); // unknown prefix
    CHECK(gi::scan_code_to_key(0xFF) == KEY_NONE);
}

TEST_CASE("split_scan_code") {
    uint32_t base = 0;
    bool extended = false;
    bool pause = false;

    CHECK(gi::split_scan_code(0xE048, &base, &extended, &pause));
    CHECK(base == 0x48u);
    CHECK(extended);
    CHECK_FALSE(pause);

    CHECK(gi::split_scan_code(0x1E, &base, &extended, &pause));
    CHECK(base == 0x1Eu);
    CHECK_FALSE(extended);

    CHECK_FALSE(gi::split_scan_code(0x7F1E, &base, &extended, &pause));
    CHECK(gi::split_scan_code(0x1E, nullptr, nullptr, nullptr));
}

TEST_CASE("scan_code_to_location: sided modifiers") {
    CHECK(gi::scan_code_to_location(0x2A) == KEY_LOCATION_LEFT);   // left shift
    CHECK(gi::scan_code_to_location(0x36) == KEY_LOCATION_RIGHT);  // right shift
    CHECK(gi::scan_code_to_location(0x1D) == KEY_LOCATION_LEFT);   // left ctrl
    CHECK(gi::scan_code_to_location(0xE01D) == KEY_LOCATION_RIGHT);
    CHECK(gi::scan_code_to_location(0x38) == KEY_LOCATION_LEFT);   // left alt
    CHECK(gi::scan_code_to_location(0xE038) == KEY_LOCATION_RIGHT);
    CHECK(gi::scan_code_to_location(0xE05B) == KEY_LOCATION_LEFT); // left meta
    CHECK(gi::scan_code_to_location(0xE05C) == KEY_LOCATION_RIGHT);
    CHECK(gi::scan_code_to_location(0x1E) == KEY_LOCATION_UNSPECIFIED);
}

TEST_CASE("virtual_key_to_key") {
    CHECK(gi::virtual_key_to_key(0x41) == KEY_A);
    CHECK(gi::virtual_key_to_key(0x5A) == KEY_Z);
    CHECK(gi::virtual_key_to_key(0x30) == KEY_0);
    CHECK(gi::virtual_key_to_key(0x39) == KEY_9);
    CHECK(gi::virtual_key_to_key(0x60) == KEY_KP_0);
    CHECK(gi::virtual_key_to_key(0x70) == KEY_F1);
    CHECK(gi::virtual_key_to_key(0x87) == KEY_F24);
    CHECK(gi::virtual_key_to_key(0x0D) == KEY_ENTER);
    CHECK(gi::virtual_key_to_key(0x26) == KEY_UP);
    CHECK(gi::virtual_key_to_key(0xA0) == KEY_SHIFT);
    CHECK(gi::virtual_key_to_key(0xA3) == KEY_CTRL);
    CHECK(gi::virtual_key_to_key(0xA4) == KEY_ALT);
    CHECK(gi::virtual_key_to_key(0xBA) == KEY_SEMICOLON);
    CHECK(gi::virtual_key_to_key(0x00) == KEY_NONE);
    CHECK(gi::virtual_key_to_key(0xFF) == KEY_NONE);
}

TEST_CASE("label_name follows the GameInputLabel numbering") {
    CHECK(std::strcmp(gi::label_name(-1), "unknown") == 0);
    CHECK(std::strcmp(gi::label_name(0), "none") == 0);
    CHECK(std::strcmp(gi::label_name(5), "xbox_view") == 0);
    // gameinput.h skips 6.
    CHECK(std::strcmp(gi::label_name(6), "unknown") == 0);
    CHECK(std::strcmp(gi::label_name(7), "xbox_a") == 0);
    CHECK(std::strcmp(gi::label_name(25), "letter_a") == 0);
    CHECK(std::strcmp(gi::label_name(78), "icon_cross") == 0);
    CHECK(std::strcmp(gi::label_name(124), "paddle_right_2") == 0);
    CHECK(std::strcmp(gi::label_name(125), "unknown") == 0);
}

TEST_CASE("haptic_location_name follows the GameInput location GUIDs") {
    // GAMEINPUT_HAPTIC_LOCATION_* in gameinput.h (Microsoft.GameInput 3.5.274).
    const uint8_t none[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    const uint8_t grip_left[8] = { 0xa8, 0x4a, 0xdf, 0xe0, 0x85, 0x12, 0x0a, 0x92 };
    const uint8_t grip_right[8] = { 0x86, 0x90, 0xb6, 0xd4, 0x11, 0x26, 0xdf, 0xc1 };
    const uint8_t trigger_left[8] = { 0x86, 0xe5, 0x17, 0x24, 0xcc, 0x07, 0xc6, 0xbc };
    const uint8_t trigger_right[8] = { 0x8b, 0x0f, 0x55, 0x5a, 0x2d, 0x92, 0xa2, 0x20 };
    CHECK(std::strcmp(gi::haptic_location_name(0x00000000, 0x0000, 0x0000, none), "none") == 0);
    CHECK(std::strcmp(gi::haptic_location_name(0x08c707c2, 0x66bb, 0x406c, grip_left), "grip_left") == 0);
    CHECK(std::strcmp(gi::haptic_location_name(0x155a0b77, 0x8bb2, 0x40db, grip_right), "grip_right") == 0);
    CHECK(std::strcmp(gi::haptic_location_name(0x8de4d896, 0x5559, 0x4081, trigger_left), "trigger_left") == 0);
    CHECK(std::strcmp(gi::haptic_location_name(0xff0cb557, 0x3af5, 0x406b, trigger_right), "trigger_right") == 0);
    // Any other field value is a different GUID.
    const uint8_t last_byte_off[8] = { 0xa8, 0x4a, 0xdf, 0xe0, 0x85, 0x12, 0x0a, 0x93 };
    CHECK(std::strcmp(gi::haptic_location_name(0x08c707c2, 0x66bb, 0x406c, last_byte_off), "unknown") == 0);
    CHECK(std::strcmp(gi::haptic_location_name(0x08c707c2, 0x66bb, 0x406d, grip_left), "unknown") == 0);
    CHECK(std::strcmp(gi::haptic_location_name(0x08c707c3, 0x66bb, 0x406c, grip_left), "unknown") == 0);
}
