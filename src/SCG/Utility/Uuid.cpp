#include <cstdint>
#include <format>
#include <random>
#include "Uuid.hpp"

namespace Sagax::SCG {

    // 122 random bits. the version nibble is 4 and the top two bits of the variant byte are 10
    std::string GenerateUuid() {
        thread_local std::mt19937_64 generator{ std::random_device{}() };
        std::uint64_t high = generator();
        std::uint64_t low = generator();
        high = (high & 0xffffffffffff0fffull) | 0x0000000000004000ull;
        low = (low & 0x3fffffffffffffffull) | 0x8000000000000000ull;
        return std::format(
            "{:08x}-{:04x}-{:04x}-{:04x}-{:012x}",
            std::uint32_t(high >> 32),
            std::uint32_t((high >> 16) & 0xffff),
            std::uint32_t(high & 0xffff),
            std::uint32_t(low >> 48),
            low & 0xffffffffffffull
        );
    }

}
