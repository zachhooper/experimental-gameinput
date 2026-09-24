#include "gameinput_keymap.h"

#include <godot_cpp/classes/global_constants.hpp>

namespace gameinput_internal {

using namespace godot;

bool split_scan_code(uint32_t scan_code, uint32_t *out_base, bool *out_extended,
                     bool *out_pause_sequence) {
    uint32_t prefix = (scan_code >> 8) & 0xFFu;
    bool extended = false;
    bool pause = false;
    uint32_t base = scan_code & 0xFFu;
    if (scan_code > 0xFFFFu) {
        // Three-byte form such as 0xE11D45 (Pause).
        pause = ((scan_code >> 16) & 0xFFu) == 0xE1u;
    } else if (prefix == 0xE0u || prefix == 0x01u) {
        extended = true;
    } else if (prefix == 0xE1u) {
        pause = true;
    } else if (prefix != 0) {
        base = 0x100u; // Unknown prefix: force a miss in the tables.
    }
    if (out_base) *out_base = base;
    if (out_extended) *out_extended = extended;
    if (out_pause_sequence) *out_pause_sequence = pause;
    return prefix == 0 || extended || pause;
}

static int64_t _base_scan_to_key(uint32_t code) {
    switch (code) {
        case 0x01: return KEY_ESCAPE;
        case 0x02: return KEY_1;
        case 0x03: return KEY_2;
        case 0x04: return KEY_3;
        case 0x05: return KEY_4;
        case 0x06: return KEY_5;
        case 0x07: return KEY_6;
        case 0x08: return KEY_7;
        case 0x09: return KEY_8;
        case 0x0A: return KEY_9;
        case 0x0B: return KEY_0;
        case 0x0C: return KEY_MINUS;
        case 0x0D: return KEY_EQUAL;
        case 0x0E: return KEY_BACKSPACE;
        case 0x0F: return KEY_TAB;
        case 0x10: return KEY_Q;
        case 0x11: return KEY_W;
        case 0x12: return KEY_E;
        case 0x13: return KEY_R;
        case 0x14: return KEY_T;
        case 0x15: return KEY_Y;
        case 0x16: return KEY_U;
        case 0x17: return KEY_I;
        case 0x18: return KEY_O;
        case 0x19: return KEY_P;
        case 0x1A: return KEY_BRACKETLEFT;
        case 0x1B: return KEY_BRACKETRIGHT;
        case 0x1C: return KEY_ENTER;
        case 0x1D: return KEY_CTRL;
        case 0x1E: return KEY_A;
        case 0x1F: return KEY_S;
        case 0x20: return KEY_D;
        case 0x21: return KEY_F;
        case 0x22: return KEY_G;
        case 0x23: return KEY_H;
        case 0x24: return KEY_J;
        case 0x25: return KEY_K;
        case 0x26: return KEY_L;
        case 0x27: return KEY_SEMICOLON;
        case 0x28: return KEY_APOSTROPHE;
        case 0x29: return KEY_QUOTELEFT;
        case 0x2A: return KEY_SHIFT;
        case 0x2B: return KEY_BACKSLASH;
        case 0x2C: return KEY_Z;
        case 0x2D: return KEY_X;
        case 0x2E: return KEY_C;
        case 0x2F: return KEY_V;
        case 0x30: return KEY_B;
        case 0x31: return KEY_N;
        case 0x32: return KEY_M;
        case 0x33: return KEY_COMMA;
        case 0x34: return KEY_PERIOD;
        case 0x35: return KEY_SLASH;
        case 0x36: return KEY_SHIFT;
        case 0x37: return KEY_KP_MULTIPLY;
        case 0x38: return KEY_ALT;
        case 0x39: return KEY_SPACE;
        case 0x3A: return KEY_CAPSLOCK;
        case 0x3B: return KEY_F1;
        case 0x3C: return KEY_F2;
        case 0x3D: return KEY_F3;
        case 0x3E: return KEY_F4;
        case 0x3F: return KEY_F5;
        case 0x40: return KEY_F6;
        case 0x41: return KEY_F7;
        case 0x42: return KEY_F8;
        case 0x43: return KEY_F9;
        case 0x44: return KEY_F10;
        case 0x45: return KEY_NUMLOCK;
        case 0x46: return KEY_SCROLLLOCK;
        case 0x47: return KEY_KP_7;
        case 0x48: return KEY_KP_8;
        case 0x49: return KEY_KP_9;
        case 0x4A: return KEY_KP_SUBTRACT;
        case 0x4B: return KEY_KP_4;
        case 0x4C: return KEY_KP_5;
        case 0x4D: return KEY_KP_6;
        case 0x4E: return KEY_KP_ADD;
        case 0x4F: return KEY_KP_1;
        case 0x50: return KEY_KP_2;
        case 0x51: return KEY_KP_3;
        case 0x52: return KEY_KP_0;
        case 0x53: return KEY_KP_PERIOD;
        case 0x56: return KEY_SECTION;
        case 0x57: return KEY_F11;
        case 0x58: return KEY_F12;
        case 0x5B: return KEY_META;
        case 0x5C: return KEY_META;
        case 0x5D: return KEY_MENU;
        case 0x64: return KEY_F13;
        case 0x65: return KEY_F14;
        case 0x66: return KEY_F15;
        case 0x67: return KEY_F16;
        case 0x68: return KEY_F17;
        case 0x69: return KEY_F18;
        case 0x6A: return KEY_F19;
        case 0x6B: return KEY_F20;
        case 0x6C: return KEY_F21;
        case 0x6D: return KEY_F22;
        case 0x6E: return KEY_F23;
        case 0x76: return KEY_F24;
        default:   return KEY_NONE;
    }
}

static int64_t _extended_scan_to_key(uint32_t code) {
    switch (code) {
        case 0x10: return KEY_MEDIAPREVIOUS;
        case 0x19: return KEY_MEDIANEXT;
        case 0x1C: return KEY_KP_ENTER;
        case 0x1D: return KEY_CTRL;
        case 0x20: return KEY_VOLUMEMUTE;
        case 0x21: return KEY_LAUNCH1;
        case 0x22: return KEY_MEDIAPLAY;
        case 0x24: return KEY_MEDIASTOP;
        case 0x2E: return KEY_VOLUMEDOWN;
        case 0x30: return KEY_VOLUMEUP;
        case 0x32: return KEY_HOMEPAGE;
        case 0x35: return KEY_KP_DIVIDE;
        case 0x37: return KEY_PRINT;
        case 0x38: return KEY_ALT;
        case 0x45: return KEY_NUMLOCK;
        case 0x47: return KEY_HOME;
        case 0x48: return KEY_UP;
        case 0x49: return KEY_PAGEUP;
        case 0x4B: return KEY_LEFT;
        case 0x4D: return KEY_RIGHT;
        case 0x4F: return KEY_END;
        case 0x50: return KEY_DOWN;
        case 0x51: return KEY_PAGEDOWN;
        case 0x52: return KEY_INSERT;
        case 0x53: return KEY_DELETE;
        case 0x5B: return KEY_META;
        case 0x5C: return KEY_META;
        case 0x5D: return KEY_MENU;
        case 0x5F: return KEY_STANDBY;
        case 0x65: return KEY_SEARCH;
        case 0x66: return KEY_FAVORITES;
        case 0x67: return KEY_REFRESH;
        case 0x68: return KEY_STOP;
        case 0x69: return KEY_FORWARD;
        case 0x6A: return KEY_BACK;
        case 0x6B: return KEY_LAUNCH0;
        case 0x6C: return KEY_LAUNCHMAIL;
        case 0x6D: return KEY_LAUNCHMEDIA;
        case 0x78: return KEY_MEDIARECORD;
        default:   return KEY_NONE;
    }
}

int64_t scan_code_to_key(uint32_t scan_code) {
    uint32_t base = 0;
    bool extended = false;
    bool pause = false;
    if (!split_scan_code(scan_code, &base, &extended, &pause)) {
        return KEY_NONE;
    }
    if (pause) {
        return KEY_PAUSE;
    }
    if (extended) {
        int64_t key = _extended_scan_to_key(base);
        if (key != KEY_NONE) return key;
        // Unlisted extended codes fall through to the base table, as Godot does.
    }
    return _base_scan_to_key(base);
}

int64_t scan_code_to_location(uint32_t scan_code) {
    uint32_t base = 0;
    bool extended = false;
    bool pause = false;
    if (!split_scan_code(scan_code, &base, &extended, &pause) || pause) {
        return KEY_LOCATION_UNSPECIFIED;
    }
    switch (base) {
        case 0x2A: return extended ? KEY_LOCATION_UNSPECIFIED : KEY_LOCATION_LEFT;  // Shift
        case 0x36: return extended ? KEY_LOCATION_UNSPECIFIED : KEY_LOCATION_RIGHT; // Shift
        case 0x1D:                                                                  // Ctrl
        case 0x38:                                                                  // Alt
            return extended ? KEY_LOCATION_RIGHT : KEY_LOCATION_LEFT;
        case 0x5B: return KEY_LOCATION_LEFT;  // Meta
        case 0x5C: return KEY_LOCATION_RIGHT; // Meta
        default:   return KEY_LOCATION_UNSPECIFIED;
    }
}

int64_t virtual_key_to_key(uint32_t vk) {
    if (vk >= 0x30 && vk <= 0x39) return KEY_0 + (int64_t)(vk - 0x30);
    if (vk >= 0x41 && vk <= 0x5A) return KEY_A + (int64_t)(vk - 0x41);
    if (vk >= 0x60 && vk <= 0x69) return KEY_KP_0 + (int64_t)(vk - 0x60);
    if (vk >= 0x70 && vk <= 0x87) return KEY_F1 + (int64_t)(vk - 0x70);
    switch (vk) {
        case 0x08: return KEY_BACKSPACE;
        case 0x09: return KEY_TAB;
        case 0x0C: return KEY_CLEAR;
        case 0x0D: return KEY_ENTER;
        case 0x10: return KEY_SHIFT;
        case 0x11: return KEY_CTRL;
        case 0x12: return KEY_ALT;
        case 0x13: return KEY_PAUSE;
        case 0x14: return KEY_CAPSLOCK;
        case 0x1B: return KEY_ESCAPE;
        case 0x20: return KEY_SPACE;
        case 0x21: return KEY_PAGEUP;
        case 0x22: return KEY_PAGEDOWN;
        case 0x23: return KEY_END;
        case 0x24: return KEY_HOME;
        case 0x25: return KEY_LEFT;
        case 0x26: return KEY_UP;
        case 0x27: return KEY_RIGHT;
        case 0x28: return KEY_DOWN;
        case 0x2A: return KEY_PRINT;
        case 0x2C: return KEY_PRINT;
        case 0x2D: return KEY_INSERT;
        case 0x2E: return KEY_DELETE;
        case 0x2F: return KEY_HELP;
        case 0x5B: return KEY_META;
        case 0x5C: return KEY_META;
        case 0x5D: return KEY_MENU;
        case 0x5F: return KEY_STANDBY;
        case 0x6A: return KEY_KP_MULTIPLY;
        case 0x6B: return KEY_KP_ADD;
        case 0x6C: return KEY_KP_PERIOD;
        case 0x6D: return KEY_KP_SUBTRACT;
        case 0x6E: return KEY_KP_PERIOD;
        case 0x6F: return KEY_KP_DIVIDE;
        case 0x90: return KEY_NUMLOCK;
        case 0x91: return KEY_SCROLLLOCK;
        case 0x92: return KEY_EQUAL;
        // Godot maps VK_LMENU/VK_RMENU to KEY_MENU; GameInput reports the
        // sided codes for Alt, so they map to KEY_ALT here.
        case 0xA0: return KEY_SHIFT;
        case 0xA1: return KEY_SHIFT;
        case 0xA2: return KEY_CTRL;
        case 0xA3: return KEY_CTRL;
        case 0xA4: return KEY_ALT;
        case 0xA5: return KEY_ALT;
        case 0xA6: return KEY_BACK;
        case 0xA7: return KEY_FORWARD;
        case 0xA8: return KEY_REFRESH;
        case 0xA9: return KEY_STOP;
        case 0xAA: return KEY_SEARCH;
        case 0xAB: return KEY_FAVORITES;
        case 0xAC: return KEY_HOMEPAGE;
        case 0xAD: return KEY_VOLUMEMUTE;
        case 0xAE: return KEY_VOLUMEDOWN;
        case 0xAF: return KEY_VOLUMEUP;
        case 0xB0: return KEY_MEDIANEXT;
        case 0xB1: return KEY_MEDIAPREVIOUS;
        case 0xB2: return KEY_MEDIASTOP;
        case 0xB3: return KEY_MEDIAPLAY;
        case 0xB4: return KEY_LAUNCHMAIL;
        case 0xB5: return KEY_LAUNCHMEDIA;
        case 0xB6: return KEY_LAUNCH0;
        case 0xB7: return KEY_LAUNCH1;
        case 0xBA: return KEY_SEMICOLON;
        case 0xBB: return KEY_EQUAL;
        case 0xBC: return KEY_COMMA;
        case 0xBD: return KEY_MINUS;
        case 0xBE: return KEY_PERIOD;
        case 0xBF: return KEY_SLASH;
        case 0xC0: return KEY_QUOTELEFT;
        case 0xDB: return KEY_BRACKETLEFT;
        case 0xDC: return KEY_BACKSLASH;
        case 0xDD: return KEY_BRACKETRIGHT;
        case 0xDE: return KEY_APOSTROPHE;
        case 0xE2: return KEY_BAR;
        case 0xE3: return KEY_HELP;
        case 0xE6: return KEY_CLEAR;
        case 0xFA: return KEY_MEDIAPLAY;
        case 0xFE: return KEY_CLEAR;
        default:   return KEY_NONE;
    }
}

} // namespace gameinput_internal
