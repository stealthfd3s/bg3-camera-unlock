#include "MachOImage.hpp"

#include <cstring>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>

namespace bg3cam {
namespace {

bool MachONameEquals(const char field[16], const char* expected) {
    return std::strncmp(field, expected, 16) == 0;
}

std::optional<LoadedTextSection> FindTextSection(
    const mach_header_64* header,
    const std::intptr_t slide) {
    const auto* command = reinterpret_cast<const load_command*>(header + 1);

    for (std::uint32_t index = 0; index < header->ncmds; ++index) {
        if (command->cmdsize < sizeof(load_command)) {
            return std::nullopt;
        }

        if (command->cmd == LC_SEGMENT_64) {
            const auto* segment =
                reinterpret_cast<const segment_command_64*>(command);

            if (MachONameEquals(segment->segname, "__TEXT")) {
                const auto* section =
                    reinterpret_cast<const section_64*>(segment + 1);

                for (std::uint32_t sectionIndex = 0;
                     sectionIndex < segment->nsects;
                     ++sectionIndex) {
                    if (MachONameEquals(section[sectionIndex].sectname, "__text")) {
                        const auto loadedAddress =
                            static_cast<std::uintptr_t>(
                                section[sectionIndex].addr + slide);

                        return LoadedTextSection{
                            reinterpret_cast<const std::uint8_t*>(loadedAddress),
                            static_cast<std::size_t>(section[sectionIndex].size),
                            slide,
                        };
                    }
                }
            }
        }

        command = reinterpret_cast<const load_command*>(
            reinterpret_cast<const std::uint8_t*>(command) + command->cmdsize);
    }

    return std::nullopt;
}

}  // namespace

std::optional<LoadedTextSection> FindMainExecutableText() {
    const std::uint32_t imageCount = _dyld_image_count();

    for (std::uint32_t index = 0; index < imageCount; ++index) {
        const mach_header* genericHeader = _dyld_get_image_header(index);
        if (genericHeader == nullptr ||
            genericHeader->magic != MH_MAGIC_64 ||
            genericHeader->filetype != MH_EXECUTE) {
            continue;
        }

        const auto* header =
            reinterpret_cast<const mach_header_64*>(genericHeader);
        const std::intptr_t slide = _dyld_get_image_vmaddr_slide(index);
        return FindTextSection(header, slide);
    }

    return std::nullopt;
}

}  // namespace bg3cam
