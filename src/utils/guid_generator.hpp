#pragma once
#include <string>
#include <random>
#include <sstream>
#include <iomanip>
#include <cstdint>

class GUIDGenerator {
public:
    // Generates a standard RFC 4122 compliant UUID v4 string (e.g., "c4a760a8-dbcf-4e62-9783-26b6831e508c")
    // thread_local is for initializing on stack per thread.
    // mt19937_64 is 64-bit Mersenne Twister pseudo-random number generator
    // uniform_int_distribution is for generating random numbers in a range
    // rd is for seeding the engine, making each program run have different random numbers
    [[nodiscard]] static std::string GenerateV4() {
        thread_local std::random_device rd;
        thread_local std::mt19937_64 engine(rd());
        thread_local std::uniform_int_distribution<uint64_t> dist;

        // we generate two 64 bit strings
        uint64_t data0 = dist(engine);
        uint64_t data1 = dist(engine);

        // Set UUID version to 4 (0100 in binary) -> bits 12-15 of time_hi_and_version
        data0 = (data0 & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;

        // Set UUID variant to RFC 4122 (10xx in binary) -> bits 6-7 of clock_seq_hi_and_reserved
        data1 = (data1 & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

        std::ostringstream ss;
        ss << std::hex << std::setfill('0');

        // Output format: 8-4-4-4-12
        ss << std::setw(8) << (data0 >> 32) << '-'
           << std::setw(4) << ((data0 >> 16) & 0xFFFF) << '-'
           << std::setw(4) << (data0 & 0xFFFF) << '-'
           << std::setw(4) << (data1 >> 48) << '-'
           << std::setw(12) << (data1 & 0xFFFFFFFFFFFFULL);

        return ss.str();
    }
};