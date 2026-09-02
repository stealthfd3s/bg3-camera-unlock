#include "PatternScanner.hpp"

#include <algorithm>

namespace bg3cam {
namespace {

bool MatchesAt(
    const std::uint8_t* const candidate,
    const std::span<const std::uint8_t> pattern,
    const std::span<const std::uint8_t> mask) {
    for (std::size_t index = 0; index < pattern.size(); ++index) {
        if (mask[index] == 0) {
            continue;
        }
        if (candidate[index] != pattern[index]) {
            return false;
        }
    }

    return true;
}

}  // namespace

std::vector<const std::uint8_t*> FindAll(
    const std::span<const std::uint8_t> memory,
    const std::span<const std::uint8_t> pattern,
    const std::span<const std::uint8_t> mask) {
    std::vector<const std::uint8_t*> matches;

    if (pattern.empty() || pattern.size() > memory.size()) {
        return matches;
    }

    if (mask.empty()) {
        auto cursor = memory.begin();
        while (cursor != memory.end()) {
            cursor = std::search(
                cursor, memory.end(), pattern.begin(), pattern.end());
            if (cursor == memory.end()) {
                break;
            }

            matches.push_back(memory.data() + (cursor - memory.begin()));
            ++cursor;
        }

        return matches;
    }

    if (mask.size() != pattern.size()) {
        return matches;
    }

    // Anchor the scan on the first byte the mask actually requires, so the
    // common case still skips ahead instead of testing every offset.
    std::size_t anchor = 0;
    while (anchor < mask.size() && mask[anchor] == 0) {
        ++anchor;
    }

    const std::size_t last = memory.size() - pattern.size();
    for (std::size_t offset = 0; offset <= last; ++offset) {
        if (anchor < pattern.size() &&
            memory[offset + anchor] != pattern[anchor]) {
            continue;
        }

        if (MatchesAt(memory.data() + offset, pattern, mask)) {
            matches.push_back(memory.data() + offset);
        }
    }

    return matches;
}

}  // namespace bg3cam
