#pragma once
#include <string>
#include <random>
#include <chrono>
#include <cstdint>
#include <cstdio>

/**
 * @brief Utility class for generating RFC 4122 compliant UUID v4 strings.
 * 
 * HOW IT WORKS:
 *   UUID v4 consists of 128 bits of random data with two specific bit fields:
 *     - Version field (bits 12-15 of the third group): fixed to 0b0100 (= 4)
 *     - Variant field (bits 6-7 of the fourth group): fixed to 0b10xx (RFC 4122)
 *   The remaining 122 bits are random, giving the output format:
 *     xxxxxxxx-xxxx-4xxx-[89ab]xxx-xxxxxxxxxxxx
 * 
 * SEEDING STRATEGY (multi-source entropy):
 *   std::random_device alone can fall back to a deterministic PRNG on some platforms
 *   (e.g., MinGW on Windows). To prevent identical seeds across threads or runs:
 *     1. Two calls to random_device are XOR-combined into a 64-bit seed.
 *     2. A high-resolution timestamp is XOR'd in to ensure per-thread uniqueness
 *        even if random_device is non-entropic on the current platform.
 *   The resulting engine is stored as thread_local, so each thread has its own
 *   independent PRNG state with no locking or contention.
 * 
 * PERFORMANCE:
 *   - thread_local: PRNG engine and distribution are initialized once per thread,
 *     not per call. Subsequent calls are pure PRNG generation with no heap allocation.
 *   - Stack buffer formatting via snprintf avoids heap allocation of std::ostringstream.
 */
class GUIDGenerator {
public:
    /**
     * @brief Generates a standard RFC 4122 compliant UUID v4 string.
     * 
     * Output format: "xxxxxxxx-xxxx-4xxx-[89ab]xxx-xxxxxxxxxxxx"  (36 chars, lowercase hex)
     * Example:       "c4a760a8-dbcf-4e62-9783-26b6831e508c"
     * 
     * @return std::string containing the UUID (always exactly 36 characters, no null terminator).
     */
    [[nodiscard]] static std::string GenerateV4() {
        // Multi-source entropy seeding: XOR two random_device outputs with a timestamp
        // to guard against deterministic fallback on platforms where random_device is weak.
        // thread_local ensures each thread seeds independently with no contention.
        thread_local std::mt19937_64 engine([]() {
            std::random_device rd;
            uint64_t seed = (static_cast<uint64_t>(rd()) << 32) ^ rd();
            seed ^= static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
            return seed;
        }());

        // thread_local distribution: reused across calls with no re-construction cost.
        thread_local std::uniform_int_distribution<uint64_t> dist;

        // Generate 128 bits of random data as two 64-bit words.
        uint64_t data0 = dist(engine);
        uint64_t data1 = dist(engine);

        // RFC 4122 Version 4: force bits 12-15 of the third UUID group to 0b0100.
        // Mask clears those 4 bits, OR sets them to 4 (0x4000).
        // data0 layout: [time_low(32)] [time_mid(16)] [time_hi_and_version(16)]
        data0 = (data0 & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;

        // RFC 4122 Variant (10xx): force the two most significant bits of the fourth group
        // to 0b10. Mask clears the top 2 bits, OR sets them to 10xx (0x8000...).
        // data1 layout: [clock_seq_hi_and_reserved(8)] [clock_seq_low(8)] [node(48)]
        data1 = (data1 & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

        // Format into a 36-char UUID string on the stack (no heap allocation).
        // Output groups: 8-4-4-4-12 hex digits separated by hyphens.
        char buf[37]; // 36 UUID chars + null terminator
        std::snprintf(buf, sizeof(buf),
            "%08x-%04x-%04x-%04x-%012llx",
            static_cast<uint32_t>(data0 >> 32),          // 8 hex: time_low
            static_cast<uint16_t>((data0 >> 16) & 0xFFFF), // 4 hex: time_mid
            static_cast<uint16_t>(data0 & 0xFFFF),          // 4 hex: time_hi_and_version (version=4)
            static_cast<uint16_t>(data1 >> 48),             // 4 hex: clock_seq (variant bits set)
            static_cast<unsigned long long>(data1 & 0xFFFFFFFFFFFFULL) // 12 hex: node
        );

        // Construct string from exactly 36 chars, excluding the null terminator.
        return std::string(buf, 36);
    }
};