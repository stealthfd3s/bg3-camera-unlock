#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace bg3cam {

// Returns every address at which the pattern occurs.
//
// An empty mask requires an exact byte-for-byte match. Otherwise the mask runs
// parallel to the pattern: a 0xff byte must match, a 0x00 byte is ignored.
// Wildcards exist so that a signature can skip the operand fields that the
// linker rewrites whenever the game binary is laid out again, while still
// pinning every opcode around them.
std::vector<const std::uint8_t*> FindAll(
    std::span<const std::uint8_t> memory,
    std::span<const std::uint8_t> pattern,
    std::span<const std::uint8_t> mask = {});

}  // namespace bg3cam
