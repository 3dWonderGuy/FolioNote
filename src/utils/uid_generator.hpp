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
    /**
     * @brief Generates the next unique ID.
     * @return The next unique ID.
     */
    [[nodiscard]] static uint32_t Next() noexcept {
        return nextUID.fetch_add(1, std::memory_order_relaxed);
    }

    /**
     * @brief Resets the UID generator to a specific value.
     * @param startValue The starting value for the UID generator.
     */
    static void Reset(uint32_t startValue = 1) noexcept {
        nextUID.store(startValue, std::memory_order_relaxed);
    }

    /**
     * @brief Returns the current value of the UID generator.
     * @return The current value of the UID generator.
     */
    [[nodiscard]] static uint32_t Peek() noexcept {
        return nextUID.load(std::memory_order_relaxed);
    }

    /**
     * @brief Ensures nextUID is strictly greater than minVal.
     * Used during binary deserialization to prevent newly drawn strokes
     * from colliding with objects loaded from disk.
     *
     * @param minVal The minimum value that nextUID must be greater than.
     */
    static void EnsureAtLeast(uint32_t minVal) noexcept {
        uint32_t current = nextUID.load(std::memory_order_relaxed);
        // Loop until nextUID is greater than or equal to minVal
        while (current < minVal) {
            // Attempt to update nextUID to minVal if it is still less than minVal
            if (nextUID.compare_exchange_weak(current, minVal, std::memory_order_relaxed)) {
                // If the update was successful, break out of the loop
                break;
            }
            // If the update failed, reload the current value of nextUID
            current = nextUID.load(std::memory_order_relaxed);
        }
    }
};
