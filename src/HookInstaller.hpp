#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace bg3cam {

struct InlineHook {
    static constexpr std::size_t kPatchSize = 16;

    void* target{};
    void* trampoline{};
    std::size_t trampolineSize{};
    std::array<std::uint8_t, kPatchSize> originalBytes{};
    bool installed{};
};

struct InstructionPatch {
    static constexpr std::size_t kPatchSize = sizeof(std::uint32_t);

    void* target{};
    std::array<std::uint8_t, kPatchSize> originalBytes{};
    bool installed{};
};

// Installs a 16-byte absolute ARM64 jump:
//
//   ldr x16, [pc, #8]
//   br  x16
//   .quad replacement
//
// If createTrampoline is true, the first 16 original bytes are copied to an
// executable trampoline followed by a jump back to target + 16. The caller
// must ensure those four displaced instructions are safe to relocate.
bool InstallInlineHook(
    InlineHook& hook,
    void* target,
    void* replacement,
    bool createTrampoline,
    std::string& error,
    void** trampolinePublication = nullptr);

bool RemoveInlineHook(InlineHook& hook, std::string& error);

// Replaces one aligned ARM64 instruction and remembers the exact original
// bytes so the change can participate in the same transactional rollback as
// the inline hooks.
bool InstallInstructionPatch(
    InstructionPatch& patch,
    void* target,
    std::uint32_t replacement,
    std::string& error);

bool RemoveInstructionPatch(InstructionPatch& patch, std::string& error);

}  // namespace bg3cam
