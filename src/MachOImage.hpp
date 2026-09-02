#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace bg3cam {

struct LoadedTextSection {
    const std::uint8_t* begin;
    std::size_t size;
    std::intptr_t slide;
};

// Finds the __TEXT,__text section of the loaded MH_EXECUTE image.
std::optional<LoadedTextSection> FindMainExecutableText();

}  // namespace bg3cam
