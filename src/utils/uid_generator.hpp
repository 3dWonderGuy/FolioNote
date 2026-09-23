#pragma once
#include <cstdint>
#include <atomic>

/**
 * @brief Thread-safe, lock-free monotonic UID generator.
 * Zero represents an invalid or null object ID.
 */
class UIDGenerator {
private:
    inline static std::atomic<uint32_t> nextUID{ 1 };

public:
    [[nodiscard]] static uint32_t Next() noexcept {
        return nextUID.fetch_add(1, std::memory_order_relaxed);
    }

    static void Reset(uint32_t startValue = 1) noexcept {
        nextUID.store(startValue, std::memory_order_relaxed);
    }

    [[nodiscard]] static uint32_t Peek() noexcept {
        return nextUID.load(std::memory_order_relaxed);
    }

    /**
     * @brief Ensures nextUID is strictly greater than minVal.
     * Used during binary deserialization to prevent newly drawn strokes
     * from colliding with objects loaded from disk.
     */
    static void EnsureAtLeast(uint32_t minVal) noexcept {
        uint32_t current = nextUID.load(std::memory_order_relaxed);
        while (current < minVal && !nextUID.compare_exchange_weak(
                   current, minVal, std::memory_order_relaxed)) {
        }
    }
};