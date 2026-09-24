// Doctest coverage for gameinput_internal::BoundedRing and merge_by_sequence,
// the containers that carry GameInput callback events from worker threads to
// the main thread. The singleton relies on three rules pinned here:
//   * the reading ring never grows and evicts the OLDEST entry when full, so
//     the caller can release what that entry owns;
//   * status events and readings are delivered in one global sequence order;
//   * a stopped drain reports exactly which entries were not delivered.

#include "doctest.h"

#include "gameinput_event_queue.h"

#include <cstdint>
#include <vector>

namespace gi = gameinput_internal;

TEST_CASE("BoundedRing: FIFO order below capacity") {
    gi::BoundedRing<int> ring(4);
    CHECK(ring.capacity() == 4u);
    CHECK(ring.empty());

    int evicted = -1;
    CHECK_FALSE(ring.push(1, &evicted));
    CHECK_FALSE(ring.push(2, &evicted));
    CHECK_FALSE(ring.push(3, &evicted));
    CHECK(evicted == -1);
    REQUIRE(ring.size() == 3u);
    CHECK(ring.at(0) == 1);
    CHECK(ring.at(1) == 2);
    CHECK(ring.at(2) == 3);
}

TEST_CASE("BoundedRing: full ring evicts the oldest entry") {
    gi::BoundedRing<int> ring(3);
    int evicted = -1;
    ring.push(1, &evicted);
    ring.push(2, &evicted);
    ring.push(3, &evicted);

    CHECK(ring.push(4, &evicted));
    CHECK(evicted == 1);
    CHECK(ring.push(5, &evicted));
    CHECK(evicted == 2);

    REQUIRE(ring.size() == 3u);
    CHECK(ring.at(0) == 3);
    CHECK(ring.at(1) == 4);
    CHECK(ring.at(2) == 5);
}

TEST_CASE("BoundedRing: keeps order across many wraps") {
    gi::BoundedRing<int> ring(5);
    int evicted = 0;
    int evictions = 0;
    for (int i = 0; i < 23; ++i) {
        if (ring.push(i, &evicted)) {
            CHECK(evicted == evictions);
            ++evictions;
        }
    }
    CHECK(evictions == 18);
    for (uint32_t i = 0; i < ring.size(); ++i) {
        CHECK(ring.at(i) == 18 + (int)i);
    }
}

TEST_CASE("BoundedRing: zero capacity hands the new entry back") {
    gi::BoundedRing<int> ring(0);
    int evicted = -1;
    CHECK(ring.push(42, &evicted));
    CHECK(evicted == 42);
    CHECK(ring.empty());
}

TEST_CASE("BoundedRing: clear and reset") {
    gi::BoundedRing<int> ring(2);
    int evicted = 0;
    ring.push(1, &evicted);
    ring.push(2, &evicted);
    ring.clear();
    CHECK(ring.empty());
    CHECK(ring.capacity() == 2u);
    ring.push(7, &evicted);
    CHECK(ring.at(0) == 7);

    ring.reset(8);
    CHECK(ring.empty());
    CHECK(ring.capacity() == 8u);
}

namespace {

struct Visit {
    char lane;
    uint32_t index;
};

} // namespace

TEST_CASE("merge_by_sequence: visits two sorted lanes in global order") {
    const std::vector<uint64_t> a = {1, 4, 5, 9};
    const std::vector<uint64_t> b = {2, 3, 6, 10, 11};
    std::vector<Visit> visits;

    gi::MergeCursor cursor = gi::merge_by_sequence(
            (uint32_t)a.size(), [&](uint32_t i) { return a[i]; },
            (uint32_t)b.size(), [&](uint32_t i) { return b[i]; },
            [&](uint32_t i) { visits.push_back({'a', i}); return true; },
            [&](uint32_t i) { visits.push_back({'b', i}); return true; });

    CHECK_FALSE(cursor.stopped);
    CHECK(cursor.next_a == a.size());
    CHECK(cursor.next_b == b.size());
    REQUIRE(visits.size() == a.size() + b.size());

    uint64_t last = 0;
    for (const Visit &v : visits) {
        uint64_t seq = v.lane == 'a' ? a[v.index] : b[v.index];
        CHECK(seq > last);
        last = seq;
    }
}

TEST_CASE("merge_by_sequence: a stopped walk reports the undelivered remainder") {
    const std::vector<uint64_t> a = {1, 4, 5};
    const std::vector<uint64_t> b = {2, 3, 6};
    int delivered = 0;

    // Stop while visiting seq 3 (b[1]), as a signal handler that calls
    // GameInput.shutdown() would.
    gi::MergeCursor cursor = gi::merge_by_sequence(
            (uint32_t)a.size(), [&](uint32_t i) { return a[i]; },
            (uint32_t)b.size(), [&](uint32_t i) { return b[i]; },
            [&](uint32_t) { ++delivered; return true; },
            [&](uint32_t i) { ++delivered; return b[i] != 3; });

    CHECK(cursor.stopped);
    CHECK(delivered == 3);      // seq 1, 2, 3
    CHECK(cursor.next_a == 1u); // a[1] (seq 4) onward was not delivered
    CHECK(cursor.next_b == 2u); // b[2] (seq 6) onward was not delivered
}

TEST_CASE("merge_by_sequence: empty lanes") {
    int visits = 0;
    gi::MergeCursor none = gi::merge_by_sequence(
            0, [](uint32_t) { return (uint64_t)0; },
            0, [](uint32_t) { return (uint64_t)0; },
            [&](uint32_t) { ++visits; return true; },
            [&](uint32_t) { ++visits; return true; });
    CHECK(visits == 0);
    CHECK_FALSE(none.stopped);

    const std::vector<uint64_t> b = {3, 8};
    gi::MergeCursor only_b = gi::merge_by_sequence(
            0, [](uint32_t) { return (uint64_t)0; },
            (uint32_t)b.size(), [&](uint32_t i) { return b[i]; },
            [&](uint32_t) { return true; },
            [&](uint32_t) { ++visits; return true; });
    CHECK(visits == 2);
    CHECK(only_b.next_b == 2u);
}
