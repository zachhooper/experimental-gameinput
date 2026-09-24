// Doctest coverage for gameinput_internal:: snapshot rules (presence bits,
// per-kind merge, chords, wrapping deltas, raw controller bits).
//
// gameinput_snapshot.h depends on neither Godot nor the GameInput headers, so
// the rules GameInputReading and the singleton's poll/event lanes rely on are
// pinned here without a runtime. See addons/godot_gameinput/src/gameinput_snapshot.h.

#include "doctest.h"

#include "gameinput_snapshot.h"

#include <cstdint>
#include <limits>
#include <type_traits>

namespace gi = gameinput_internal;

static_assert(std::is_trivially_copyable<gi::Snapshot>::value,
              "Snapshot must stay trivially copyable: it lives in a preallocated ring "
              "written from GameInput worker threads");

TEST_CASE("snapshot kinds: public DeviceKind bits round-trip") {
    SUBCASE("typed kinds keep their bit values") {
        for (uint32_t bit = 1; bit <= 0x40u; bit <<= 1) {
            CHECK(gi::snapshot_kinds_from_public(bit) == bit);
            CHECK(gi::public_kinds_from_snapshot(bit) == bit);
        }
    }

    SUBCASE("DEVICE_CONTROLLER expands to all three raw controller sub-kinds") {
        CHECK(gi::snapshot_kinds_from_public(gi::PUBLIC_CONTROLLER_BIT) == gi::SNAP_CONTROLLER_ANY);
    }

    SUBCASE("any raw controller sub-kind folds back to DEVICE_CONTROLLER") {
        CHECK(gi::public_kinds_from_snapshot(gi::SNAP_CONTROLLER_AXIS) == gi::PUBLIC_CONTROLLER_BIT);
        CHECK(gi::public_kinds_from_snapshot(gi::SNAP_CONTROLLER_BUTTON) == gi::PUBLIC_CONTROLLER_BIT);
        CHECK(gi::public_kinds_from_snapshot(gi::SNAP_CONTROLLER_SWITCH) == gi::PUBLIC_CONTROLLER_BIT);
        CHECK(gi::public_kinds_from_snapshot(gi::SNAP_ALL) == 0xFFu);
    }

    SUBCASE("unknown public bits are dropped") {
        CHECK(gi::snapshot_kinds_from_public(0xFF00u) == 0u);
    }
}

TEST_CASE("snapshot kinds: index and bit tables agree") {
    for (int i = 0; i < (int)gi::kSnapshotKindCount; ++i) {
        uint32_t bit = gi::snapshot_kind_bit(i);
        CHECK(bit != 0u);
        CHECK(gi::snapshot_kind_index(bit) == i);
    }
    CHECK(gi::snapshot_kind_bit(-1) == 0u);
    CHECK(gi::snapshot_kind_bit((int)gi::kSnapshotKindCount) == 0u);
    CHECK(gi::snapshot_kind_index(gi::SNAP_GAMEPAD | gi::SNAP_MOUSE) == -1);
    CHECK(gi::snapshot_kind_index(gi::PUBLIC_CONTROLLER_BIT) == -1);
}

TEST_CASE("mark_kind and timestamps") {
    gi::Snapshot s;
    gi::mark_kind(s, gi::SNAP_GAMEPAD, 100);
    gi::mark_kind(s, gi::SNAP_MOUSE, 250);
    gi::mark_kind(s, gi::SNAP_GAMEPAD | gi::SNAP_MOUSE, 999); // not a single bit: ignored

    CHECK(s.kinds == (gi::SNAP_GAMEPAD | gi::SNAP_MOUSE));
    CHECK(gi::kinds_timestamp(s, gi::SNAP_GAMEPAD) == 100u);
    CHECK(gi::kinds_timestamp(s, gi::SNAP_MOUSE) == 250u);
    CHECK(gi::kinds_timestamp(s, gi::SNAP_KEYBOARD) == 0u);
    CHECK(gi::newest_timestamp(s) == 250u);
    CHECK(gi::kinds_timestamp(s, gi::SNAP_GAMEPAD | gi::SNAP_KEYBOARD) == 100u);
}

TEST_CASE("merge_kinds copies only present, selected kinds") {
    gi::Snapshot dst;
    dst.gamepad.buttons = 0x4;
    dst.mouse.x = 7;
    gi::mark_kind(dst, gi::SNAP_GAMEPAD, 10);
    gi::mark_kind(dst, gi::SNAP_MOUSE, 10);

    gi::Snapshot src;
    src.gamepad.buttons = 0x8;
    src.mouse.x = 99;
    src.key_count = 1;
    src.keys[0].scan_code = 0x1E;
    gi::mark_kind(src, gi::SNAP_GAMEPAD, 20);
    gi::mark_kind(src, gi::SNAP_KEYBOARD, 30);
    // src.mouse is NOT marked present, so it must not overwrite dst.mouse.

    SUBCASE("default mask merges every present kind") {
        gi::merge_kinds(dst, src);
        CHECK(dst.gamepad.buttons == 0x8u);
        CHECK(dst.mouse.x == 7);
        CHECK(dst.key_count == 1u);
        CHECK(dst.keys[0].scan_code == 0x1Eu);
        CHECK(dst.kinds == (gi::SNAP_GAMEPAD | gi::SNAP_MOUSE | gi::SNAP_KEYBOARD));
        CHECK(gi::kinds_timestamp(dst, gi::SNAP_GAMEPAD) == 20u);
        CHECK(gi::kinds_timestamp(dst, gi::SNAP_MOUSE) == 10u);
        CHECK(gi::kinds_timestamp(dst, gi::SNAP_KEYBOARD) == 30u);
    }

    SUBCASE("mask limits which kinds are merged") {
        gi::merge_kinds(dst, src, gi::SNAP_KEYBOARD);
        CHECK(dst.gamepad.buttons == 0x4u);
        CHECK(gi::kinds_timestamp(dst, gi::SNAP_GAMEPAD) == 10u);
        CHECK(dst.key_count == 1u);
    }
}

TEST_CASE("merge_kinds carries raw controller arrays") {
    gi::Snapshot src;
    src.axis_count = 2;
    src.axis_count_native = 2;
    src.axes[1] = 0.5f;
    src.button_count = 70;
    src.button_count_native = 70;
    gi::set_controller_button(src, 65, true);
    src.switch_count = 1;
    src.switch_count_native = 1;
    src.switches[0] = 3;
    gi::mark_kind(src, gi::SNAP_CONTROLLER_AXIS, 5);
    gi::mark_kind(src, gi::SNAP_CONTROLLER_BUTTON, 5);
    gi::mark_kind(src, gi::SNAP_CONTROLLER_SWITCH, 5);

    gi::Snapshot dst;
    gi::merge_kinds(dst, src);
    CHECK(dst.axis_count == 2u);
    CHECK(dst.axes[1] == doctest::Approx(0.5f));
    CHECK(gi::controller_button(dst, 65));
    CHECK_FALSE(gi::controller_button(dst, 64));
    CHECK(dst.switches[0] == 3);
    CHECK(gi::public_kinds_from_snapshot(dst.kinds) == gi::PUBLIC_CONTROLLER_BIT);
}

TEST_CASE("is_truncated reports arrays cut to capacity") {
    gi::Snapshot s;
    CHECK_FALSE(gi::is_truncated(s));
    s.key_count = gi::kMaxKeys;
    s.key_count_native = gi::kMaxKeys + 3;
    CHECK(gi::is_truncated(s));

    gi::Snapshot b;
    b.button_count = gi::kMaxControllerButtons;
    b.button_count_native = gi::kMaxControllerButtons + 1;
    CHECK(gi::is_truncated(b));
}

TEST_CASE("chords fail closed and need every bit") {
    const uint32_t valid = 0x3FFFu;

    SUBCASE("empty mask is never down") {
        CHECK_FALSE(gi::chord_down(0xFFFFu, 0, valid));
    }
    SUBCASE("bits outside the valid mask are never down") {
        CHECK_FALSE(gi::chord_down(0xFFFFFFFFu, 1u << 20, valid));
        CHECK_FALSE(gi::chord_down(0xFFFFFFFFu, 0x4u | (1u << 20), valid));
    }
    SUBCASE("partial chord is not down") {
        CHECK_FALSE(gi::chord_down(0x4u, 0x4u | 0x8u, valid));
    }
    SUBCASE("full chord is down") {
        CHECK(gi::chord_down(0x4u | 0x8u | 0x10u, 0x4u | 0x8u, valid));
    }
}

TEST_CASE("chord edges") {
    const uint32_t valid = 0xFFu;

    SUBCASE("a held chord without a previous sample counts as pressed") {
        CHECK(gi::chord_pressed(0x4u, 0, false, 0x4u, valid));
        CHECK_FALSE(gi::chord_released(0, 0x4u, false, 0x4u, valid));
    }
    SUBCASE("held in both samples is not a new press") {
        CHECK_FALSE(gi::chord_pressed(0x4u, 0x4u, true, 0x4u, valid));
    }
    SUBCASE("completing a chord is a press") {
        CHECK(gi::chord_pressed(0xCu, 0x4u, true, 0xCu, valid));
    }
    SUBCASE("breaking a chord is a release") {
        CHECK(gi::chord_released(0x4u, 0xCu, true, 0xCu, valid));
        CHECK_FALSE(gi::chord_released(0xCu, 0xCu, true, 0xCu, valid));
    }
}

TEST_CASE("wrapping_delta survives int64 wrap-around") {
    CHECK(gi::wrapping_delta(15, 10) == 5);
    CHECK(gi::wrapping_delta(10, 15) == -5);
    const int64_t max = std::numeric_limits<int64_t>::max();
    const int64_t min = std::numeric_limits<int64_t>::min();
    CHECK(gi::wrapping_delta(min + 5, max - 4) == 10);
    CHECK(gi::wrapping_delta(max - 4, min + 5) == -10);
}

TEST_CASE("raw controller button bits respect bounds") {
    gi::Snapshot s;
    s.button_count = 3;
    gi::set_controller_button(s, 2, true);
    CHECK(gi::controller_button(s, 2));
    CHECK_FALSE(gi::controller_button(s, 1));

    SUBCASE("indices past button_count read as up even when the bit is set") {
        gi::set_controller_button(s, 5, true);
        CHECK_FALSE(gi::controller_button(s, 5));
    }
    SUBCASE("indices past capacity are ignored") {
        gi::set_controller_button(s, gi::kMaxControllerButtons, true);
        CHECK_FALSE(gi::controller_button(s, gi::kMaxControllerButtons));
    }
    SUBCASE("clearing a bit") {
        gi::set_controller_button(s, 2, false);
        CHECK_FALSE(gi::controller_button(s, 2));
    }
}

TEST_CASE("has_scan_code scans only the stored keys") {
    gi::Snapshot s;
    s.key_count = 1;
    s.keys[0].scan_code = 0x1E;
    s.keys[1].scan_code = 0x30; // beyond key_count
    CHECK(gi::has_scan_code(s, 0x1E));
    CHECK_FALSE(gi::has_scan_code(s, 0x30));
}
