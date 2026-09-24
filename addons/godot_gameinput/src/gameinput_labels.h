#ifndef GODOT_GAMEINPUT_LABELS_H
#define GODOT_GAMEINPUT_LABELS_H

#include <cstdint>

namespace gameinput_internal {

// Stable snake_case name for a GameInputLabel value (gameinput.h, API v3),
// used for glyph selection in UI: "xbox_a", "icon_cross", "letter_a", ...
// Hand-maintained; update it when the GameInput header adds labels.
inline const char *label_name(int32_t label) {
    switch (label) {
        case  -1: return "unknown";
        case   0: return "none";
        case   1: return "xbox_guide";
        case   2: return "xbox_back";
        case   3: return "xbox_start";
        case   4: return "xbox_menu";
        case   5: return "xbox_view";
        case   7: return "xbox_a";
        case   8: return "xbox_b";
        case   9: return "xbox_x";
        case  10: return "xbox_y";
        case  11: return "xbox_dpad_up";
        case  12: return "xbox_dpad_down";
        case  13: return "xbox_dpad_left";
        case  14: return "xbox_dpad_right";
        case  15: return "xbox_left_shoulder";
        case  16: return "xbox_left_trigger";
        case  17: return "xbox_left_stick_button";
        case  18: return "xbox_right_shoulder";
        case  19: return "xbox_right_trigger";
        case  20: return "xbox_right_stick_button";
        case  21: return "xbox_paddle_1";
        case  22: return "xbox_paddle_2";
        case  23: return "xbox_paddle_3";
        case  24: return "xbox_paddle_4";
        case  25: return "letter_a";
        case  26: return "letter_b";
        case  27: return "letter_c";
        case  28: return "letter_d";
        case  29: return "letter_e";
        case  30: return "letter_f";
        case  31: return "letter_g";
        case  32: return "letter_h";
        case  33: return "letter_i";
        case  34: return "letter_j";
        case  35: return "letter_k";
        case  36: return "letter_l";
        case  37: return "letter_m";
        case  38: return "letter_n";
        case  39: return "letter_o";
        case  40: return "letter_p";
        case  41: return "letter_q";
        case  42: return "letter_r";
        case  43: return "letter_s";
        case  44: return "letter_t";
        case  45: return "letter_u";
        case  46: return "letter_v";
        case  47: return "letter_w";
        case  48: return "letter_x";
        case  49: return "letter_y";
        case  50: return "letter_z";
        case  51: return "number_0";
        case  52: return "number_1";
        case  53: return "number_2";
        case  54: return "number_3";
        case  55: return "number_4";
        case  56: return "number_5";
        case  57: return "number_6";
        case  58: return "number_7";
        case  59: return "number_8";
        case  60: return "number_9";
        case  61: return "arrow_up";
        case  62: return "arrow_up_right";
        case  63: return "arrow_right";
        case  64: return "arrow_down_right";
        case  65: return "arrow_down";
        case  66: return "arrow_down_left";
        case  67: return "arrow_left";
        case  68: return "arrow_up_left";
        case  69: return "arrow_up_down";
        case  70: return "arrow_left_right";
        case  71: return "arrow_up_down_left_right";
        case  72: return "arrow_clockwise";
        case  73: return "arrow_counter_clockwise";
        case  74: return "arrow_return";
        case  75: return "icon_branding";
        case  76: return "icon_home";
        case  77: return "icon_menu";
        case  78: return "icon_cross";
        case  79: return "icon_circle";
        case  80: return "icon_square";
        case  81: return "icon_triangle";
        case  82: return "icon_star";
        case  83: return "icon_dpad_up";
        case  84: return "icon_dpad_down";
        case  85: return "icon_dpad_left";
        case  86: return "icon_dpad_right";
        case  87: return "icon_dial_clockwise";
        case  88: return "icon_dial_counter_clockwise";
        case  89: return "icon_slider_left_right";
        case  90: return "icon_slider_up_down";
        case  91: return "icon_wheel_up_down";
        case  92: return "icon_plus";
        case  93: return "icon_minus";
        case  94: return "icon_suspension";
        case  95: return "home";
        case  96: return "guide";
        case  97: return "mode";
        case  98: return "select";
        case  99: return "menu";
        case 100: return "view";
        case 101: return "back";
        case 102: return "start";
        case 103: return "options";
        case 104: return "share";
        case 105: return "up";
        case 106: return "down";
        case 107: return "left";
        case 108: return "right";
        case 109: return "lb";
        case 110: return "lt";
        case 111: return "lsb";
        case 112: return "l1";
        case 113: return "l2";
        case 114: return "l3";
        case 115: return "rb";
        case 116: return "rt";
        case 117: return "rsb";
        case 118: return "r1";
        case 119: return "r2";
        case 120: return "r3";
        case 121: return "paddle_left_1";
        case 122: return "paddle_left_2";
        case 123: return "paddle_right_1";
        case 124: return "paddle_right_2";
        default:  return "unknown";
    }
}

} // namespace gameinput_internal

#endif // GODOT_GAMEINPUT_LABELS_H
