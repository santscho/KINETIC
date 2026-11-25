#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <type_traits>

namespace kinetic
{

/**
    Single-producer / single-consumer lock-free queue.

    Used for the UI -> audio direction (commands) so neither thread ever blocks or allocates.
    Counters are unsigned and allowed to wrap; the difference between them stays correct
    modulo 2^32, which is far more items than a plug-in instance will ever queue.

    Producer: push() from exactly one thread (the message thread).
    Consumer: pop() from exactly one thread (the audio thread).
*/
template <typename T, std::uint32_t capacity>
class SpscQueue
{
public:
    static_assert (capacity > 1, "capacity must leave room for at least one item");
    static_assert (std::is_trivially_copyable_v<T>, "items must be trivially copyable");
    static_assert (std::is_default_constructible_v<T>, "items must be default constructible");

    /** Returns false when the queue is full; the item is dropped rather than blocking. */
    bool push (const T& item) noexcept
    {
        const auto w = writeIndex.load (std::memory_order_relaxed);
        const auto r = readIndex.load (std::memory_order_acquire);

        if (w - r >= capacity)
            return false;

        storage[static_cast<std::size_t> (w % capacity)] = item;
        writeIndex.store (w + 1, std::memory_order_release);
        return true;
    }

    /** Returns false when the queue is empty. */
    bool pop (T& destination) noexcept
    {
        const auto r = readIndex.load (std::memory_order_relaxed);

        if (r == writeIndex.load (std::memory_order_acquire))
            return false;

        destination = storage[static_cast<std::size_t> (r % capacity)];
        readIndex.store (r + 1, std::memory_order_release);
        return true;
    }

    bool isEmpty() const noexcept
    {
        return size() == 0;
    }

    int size() const noexcept
    {
        const auto w = writeIndex.load (std::memory_order_acquire);
        const auto r = readIndex.load (std::memory_order_acquire);
        return static_cast<int> (w - r);
    }

    /** Discards everything. Only call while no other thread is touching the queue. */
    void clear() noexcept
    {
        readIndex.store (writeIndex.load (std::memory_order_acquire), std::memory_order_release);
    }

private:
    std::array<T, capacity> storage {};
    alignas (64) std::atomic<std::uint32_t> writeIndex { 0 };
    alignas (64) std::atomic<std::uint32_t> readIndex  { 0 };
};

} // namespace kinetic
