#pragma once
#include <cstdint>
#include <atomic>

class UIDGenerator {
private:

    // few personal notes, inline means creating easy function in header file
    // avoidaing recreation areas by having only one version of this function compiled
    // atomic - make sure threads don't crash into one another
    // it is a varaible initilized at 1, o for rare cases
    inline static std::atomic<uint32_t> nextUID{ 1 };

public:
    // Generate the next unique 32-bit ID. [[nodiscard]] for dev to no leave empty return function
    // plus some memory optimizing magic for speed
    [[nodiscard]] static uint32_t Next() noexcept {
        return nextUID.fetch_add(1, std::memory_order_relaxed);
    }

    // Reset UID sequence (e.g., when reloading or clearing workspace sessions)
    static void Reset(uint32_t startValue = 1) noexcept {
        nextUID.store(startValue, std::memory_order_relaxed);
    }

    
    // Find next increment without actualy creating it, used for rare dev cases
    [[nodiscard]] static uint32_t Peek() noexcept {
        return nextUID.load(std::memory_order_relaxed);
    }
};