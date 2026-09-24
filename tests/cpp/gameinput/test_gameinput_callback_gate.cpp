// Doctest coverage for gameinput_internal::CallbackGate, the context every
// GameInput callback registration hands to the runtime. The singleton relies
// on four rules pinned here:
//   * a gate admits callers only while it is open;
//   * a closed gate never reaches its owner, even when a newer gate for the
//     same owner is open (a late call from an old registration);
//   * wait_idle() returns only after every caller that entered has left;
//   * after close() and wait_idle() no caller is inside.

#include "doctest.h"

#include "gameinput_callback_gate.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

namespace gi = gameinput_internal;

namespace {
struct Owner {
    int calls = 0;
};
using Gate = gi::CallbackGate<Owner>;
} // namespace

TEST_CASE("CallbackGate: a new gate is closed") {
    Owner owner;
    Gate gate(&owner);
    CHECK_FALSE(gate.is_open());
    CHECK_FALSE(gate.enter());
    CHECK(gate.in_flight() == 0);
    CHECK(gate.owner() == &owner);
}

TEST_CASE("CallbackGate: an open gate admits and counts callers") {
    Owner owner;
    Gate gate(&owner);
    gate.open();
    REQUIRE(gate.enter());
    REQUIRE(gate.enter());
    CHECK(gate.in_flight() == 2);
    gate.leave();
    gate.leave();
    CHECK(gate.in_flight() == 0);
}

TEST_CASE("CallbackGate: close turns later callers away without a count") {
    Owner owner;
    Gate gate(&owner);
    gate.open();
    gate.close();
    CHECK_FALSE(gate.is_open());
    CHECK_FALSE(gate.enter());
    // REQUIRE ends the case here if the closed gate admitted the caller, since
    // wait_idle() would then wait for a leave() that never comes.
    REQUIRE(gate.in_flight() == 0);
    gate.wait_idle();
}

TEST_CASE("CallbackGate: Scope leaves on every path") {
    Owner owner;
    Gate gate(&owner);

    {
        Gate::Scope scope(nullptr);
        CHECK_FALSE(static_cast<bool>(scope));
        CHECK(scope.owner() == nullptr);
    }
    {
        Gate::Scope scope(&gate);
        CHECK_FALSE(static_cast<bool>(scope));
        CHECK(scope.owner() == nullptr);
    }
    CHECK(gate.in_flight() == 0);

    gate.open();
    {
        Gate::Scope scope(&gate);
        REQUIRE(static_cast<bool>(scope));
        CHECK(scope.owner() == &owner);
        CHECK(gate.in_flight() == 1);
    }
    CHECK(gate.in_flight() == 0);
}

TEST_CASE("CallbackGate: an old gate stays closed while a new one is open") {
    // Re-registering a callback while an old registration could not be
    // removed: both gates name the same owner, and only the new one may
    // reach it.
    Owner owner;
    Gate old_gate(&owner);
    old_gate.open();
    old_gate.close();

    Gate new_gate(&owner);
    new_gate.open();

    Gate::Scope late(&old_gate);
    CHECK_FALSE(static_cast<bool>(late));
    Gate::Scope current(&new_gate);
    CHECK(static_cast<bool>(current));
    CHECK(current.owner() == &owner);
    CHECK(old_gate.in_flight() == 0);
    CHECK(new_gate.in_flight() == 1);
}

TEST_CASE("CallbackGate: wait_idle waits for a caller that entered first") {
    Owner owner;
    Gate gate(&owner);
    gate.open();

    std::atomic<bool> entered{ false };
    std::atomic<bool> release{ false };
    std::atomic<bool> waited{ false };

    std::thread caller([&] {
        Gate::Scope scope(&gate);
        entered.store(true);
        while (!release.load()) {
            std::this_thread::yield();
        }
    });
    while (!entered.load()) {
        std::this_thread::yield();
    }

    gate.close();
    CHECK(gate.in_flight() == 1);

    std::thread waiter([&] {
        gate.wait_idle();
        waited.store(true);
    });
    // The caller is still inside, so wait_idle() cannot have returned.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK_FALSE(waited.load());

    release.store(true);
    caller.join();
    waiter.join();
    CHECK(waited.load());
    CHECK(gate.in_flight() == 0);
}

TEST_CASE("CallbackGate: no caller is inside after close and wait_idle") {
    // A smoke test of the concurrent contract, not a proof: several threads
    // hammer the gate while the main thread closes it and waits.
    Owner owner;
    Gate gate(&owner);
    gate.open();

    std::atomic<bool> stop{ false };
    std::atomic<bool> idle{ false };
    std::atomic<int64_t> admitted{ 0 };
    std::atomic<int64_t> admitted_after_idle{ 0 };

    std::vector<std::thread> callers;
    for (int i = 0; i < 4; ++i) {
        callers.emplace_back([&] {
            while (!stop.load()) {
                Gate::Scope scope(&gate);
                if (scope) {
                    admitted.fetch_add(1);
                    if (idle.load()) {
                        admitted_after_idle.fetch_add(1);
                    }
                }
            }
        });
    }

    while (admitted.load() < 1000) {
        std::this_thread::yield();
    }
    gate.close();
    // Waited for on another thread with a deadline, so a gate that keeps
    // admitting callers after close() fails here instead of hanging.
    std::atomic<bool> waited{ false };
    std::thread waiter([&] {
        gate.wait_idle();
        waited.store(true);
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!waited.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    const bool went_idle = waited.load();
    idle.store(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    stop.store(true);
    for (std::thread &caller : callers) {
        caller.join();
    }
    waiter.join();

    CHECK(went_idle);
    CHECK(admitted.load() >= 1000);
    CHECK(admitted_after_idle.load() == 0);
    CHECK(gate.in_flight() == 0);
}
