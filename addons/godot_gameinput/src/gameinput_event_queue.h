#ifndef GODOT_GAMEINPUT_EVENT_QUEUE_H
#define GODOT_GAMEINPUT_EVENT_QUEUE_H

// Containers for callback events that cross from GameInput worker threads to
// the main thread. They carry no locking of their own: the singleton holds its
// event mutex around every call. Kept free of Godot and GameInput types so the
// ordering, overflow and release rules are unit-tested in tests/cpp/gameinput.

#include <cstdint>
#include <vector>

namespace gameinput_internal {

// Fixed-capacity FIFO. push() never allocates once reset() has run; when the
// ring is full the oldest element is evicted into *evicted so the caller can
// release whatever that element owns (for example an AddRef'd device).
template <typename T>
class BoundedRing {
public:
    BoundedRing() = default;
    explicit BoundedRing(uint32_t capacity) { reset(capacity); }

    void reset(uint32_t capacity) {
        m_slots.assign(capacity, T{});
        m_head = 0;
        m_count = 0;
    }

    uint32_t capacity() const { return (uint32_t)m_slots.size(); }
    uint32_t size() const { return m_count; }
    bool empty() const { return m_count == 0; }

    // Returns true when an element had to be evicted. With zero capacity the
    // new element itself is handed back through *evicted.
    bool push(const T &value, T *evicted) {
        const uint32_t cap = capacity();
        if (cap == 0) {
            if (evicted) *evicted = value;
            return true;
        }
        if (m_count == cap) {
            if (evicted) *evicted = m_slots[m_head];
            m_slots[m_head] = value;
            m_head = (m_head + 1) % cap;
            return true;
        }
        m_slots[(m_head + m_count) % cap] = value;
        ++m_count;
        return false;
    }

    // FIFO order: at(0) is the oldest element.
    T &at(uint32_t index) { return m_slots[(m_head + index) % capacity()]; }
    const T &at(uint32_t index) const { return m_slots[(m_head + index) % capacity()]; }

    void clear() {
        m_head = 0;
        m_count = 0;
    }

private:
    std::vector<T> m_slots;
    uint32_t m_head = 0;
    uint32_t m_count = 0;
};

struct MergeCursor {
    uint32_t next_a = 0;
    uint32_t next_b = 0;
    bool stopped = false;
};

// Walks two sequences that are each sorted by an ascending sequence number
// and visits every element in global order. `seq_a(i)` / `seq_b(i)` return the
// sequence number of element i. A visitor may return false to stop the walk
// (for example when a signal handler shut the runtime down); the returned
// cursor then points at the first element of each sequence that was NOT
// visited, so the caller can release exactly the undelivered remainder.
template <typename SeqA, typename SeqB, typename VisitA, typename VisitB>
MergeCursor merge_by_sequence(uint32_t count_a, SeqA seq_a, uint32_t count_b, SeqB seq_b,
                              VisitA visit_a, VisitB visit_b) {
    MergeCursor cursor;
    while (cursor.next_a < count_a || cursor.next_b < count_b) {
        bool take_a = cursor.next_b >= count_b ||
                      (cursor.next_a < count_a && seq_a(cursor.next_a) < seq_b(cursor.next_b));
        if (take_a) {
            uint32_t index = cursor.next_a++;
            if (!visit_a(index)) {
                cursor.stopped = true;
                break;
            }
        } else {
            uint32_t index = cursor.next_b++;
            if (!visit_b(index)) {
                cursor.stopped = true;
                break;
            }
        }
    }
    return cursor;
}

} // namespace gameinput_internal

#endif // GODOT_GAMEINPUT_EVENT_QUEUE_H
