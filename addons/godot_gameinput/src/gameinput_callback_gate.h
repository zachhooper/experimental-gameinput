#ifndef GODOT_GAMEINPUT_CALLBACK_GATE_H
#define GODOT_GAMEINPUT_CALLBACK_GATE_H

// The context each GameInput callback registration hands to the runtime.
//
// GameInput may call a registration until UnregisterCallback() succeeds, and
// the GDK says nothing the callback uses may be freed before then, including
// the DLL that hosts it. So every registration gets its own gate instead of the
// singleton's `this`: callbacks enter through the gate, close() turns every
// later call away before it reaches the owner, and wait_idle() waits for the
// calls that entered first. A gate whose registration was never removed is
// kept alive rather than deleted, so a late call only ever touches the gate.
// Kept free of Godot and GameInput types so it is unit-tested in
// tests/cpp/gameinput.

#include <atomic>
#include <cstdint>
#include <thread>

namespace gameinput_internal {

template <typename Owner>
class CallbackGate {
public:
    explicit CallbackGate(Owner *owner) : m_owner(owner) {}
    CallbackGate(const CallbackGate &) = delete;
    CallbackGate &operator=(const CallbackGate &) = delete;

    Owner *owner() const { return m_owner; }

    void open() { m_open.store(true); }
    // Later enter() calls fail; calls already inside are not waited for.
    void close() { m_open.store(false); }
    bool is_open() const { return m_open.load(); }

    // Both sides are sequentially consistent, so a caller whose enter() saw the
    // gate open is counted by a wait_idle() that runs after close().
    bool enter() {
        m_in_flight.fetch_add(1);
        if (!m_open.load()) {
            m_in_flight.fetch_sub(1);
            return false;
        }
        return true;
    }
    void leave() { m_in_flight.fetch_sub(1); }

    int32_t in_flight() const { return m_in_flight.load(); }

    // Returns once every caller that entered has left. Callers turned away by
    // a closed gate only hold the count for the instant they check it.
    void wait_idle() const {
        while (m_in_flight.load() != 0) {
            std::this_thread::yield();
        }
    }

    // Enters the gate passed as a callback context and leaves on every return
    // path.
    class Scope {
    public:
        explicit Scope(void *context) :
                m_gate(static_cast<CallbackGate *>(context)),
                m_entered(m_gate != nullptr && m_gate->enter()) {}
        ~Scope() {
            if (m_entered) {
                m_gate->leave();
            }
        }
        Scope(const Scope &) = delete;
        Scope &operator=(const Scope &) = delete;

        explicit operator bool() const { return m_entered; }
        Owner *owner() const { return m_entered ? m_gate->owner() : nullptr; }

    private:
        CallbackGate *m_gate;
        bool m_entered;
    };

private:
    Owner *const m_owner;
    std::atomic<bool> m_open{false};
    std::atomic<int32_t> m_in_flight{0};
};

} // namespace gameinput_internal

#endif // GODOT_GAMEINPUT_CALLBACK_GATE_H
