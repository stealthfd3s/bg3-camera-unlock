#include "PatternScanner.hpp"
#include "Patterns.hpp"

#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: pattern_test /path/to/bg3-arm64\n";
        return 2;
    }

    const std::string path = argv[1];
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        std::cerr << "could not open: " << path << '\n';
        return 2;
    }

    const std::vector<std::uint8_t> fileBytes{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>(),
    };

    bool allUnique = true;

    for (const bg3cam::NamedPattern& pattern : bg3cam::kRequiredPatterns) {
        const auto matches = bg3cam::FindAll(
            std::span<const std::uint8_t>{fileBytes},
            pattern.bytes,
            pattern.mask);

        std::cout << pattern.name << ": " << matches.size() << " match(es)";

        if (matches.size() == 1) {
            const auto offset =
                static_cast<std::size_t>(matches.front() - fileBytes.data());
            std::cout << ", file offset 0x"
                      << std::hex << offset << std::dec;
        } else {
            allUnique = false;
        }

        std::cout << '\n';
    }

    return allUnique ? 0 : 1;
}
