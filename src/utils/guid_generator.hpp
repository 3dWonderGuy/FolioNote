#pragma once
#include <string>
#include <random>
#include <chrono>
#include <cstdint>
#include <cstdio>

class GUIDGenerator {
public:
    /**
     * @brief Generates a standard RFC 4122 compliant UUID v4 string.
     * Example: "c4a760a8-dbcf-4e62-9783-26b6831e508c"
     */
    [[nodiscard]] static std::string GenerateV4() {
        // Robust multi-source entropy seeding to avoid deterministic fallback traps
        thread_local std::mt19937_64 engine([]() {
            std::random_device rd;
            uint64_t seed = (static_cast<uint64_t>(rd()) << 32) ^ rd();
            seed ^= static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
            return seed;
        }());

        thread_local std::uniform_int_distribution<uint64_t> dist;

        uint64_t data0 = dist(engine);
        uint64_t data1 = dist(engine);

        // Version 4: Set bits 12-15 of time_hi_and_version to 0100
        data0 = (data0 & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;

        // RFC 4122 Variant: Set bits 6-7 of clock_seq_hi_and_reserved to 10
        data1 = (data1 & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

        // Zero-overhead stack buffer formatting (36 characters + null terminator)
        char buf[37];
        std::snprintf(buf, sizeof(buf),
            "%08x-%04x-%04x-%04x-%012llx",
            static_cast<uint32_t>(data0 >> 32),
            static_cast<uint16_t>((data0 >> 16) & 0xFFFF),
            static_cast<uint16_t>(data0 & 0xFFFF),
            static_cast<uint16_t>(data1 >> 48),
            static_cast<unsigned long long>(data1 & 0xFFFFFFFFFFFFULL)
        );

        return std::string(buf, 36);
    }
};