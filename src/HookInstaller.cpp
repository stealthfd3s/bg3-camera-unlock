#include "HookInstaller.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <sstream>
#include <sys/mman.h>
#include <unistd.h>

namespace bg3cam {
namespace {

constexpr std::uint32_t kLdrX16LiteralPlus8 = 0x58000050;
constexpr std::uint32_t kBranchX16 = 0xd61f0200;

std::array<std::uint8_t, InlineHook::kPatchSize> MakeAbsoluteJump(
    const void* destination) {
    std::array<std::uint8_t, InlineHook::kPatchSize> jump{};
    const std::uintptr_t address =
        reinterpret_cast<std::uintptr_t>(destination);

    std::memcpy(jump.data(), &kLdrX16LiteralPlus8, sizeof(kLdrX16LiteralPlus8));
    std::memcpy(
        jump.data() + sizeof(kLdrX16LiteralPlus8),
        &kBranchX16,
        sizeof(kBranchX16));
    std::memcpy(jump.data() + 8, &address, sizeof(address));
    return jump;
}

bool SetCodeProtection(
    void* address,
    const std::size_t length,
    const vm_prot_t protection,
    std::string& error) {
    const std::size_t pageSize = static_cast<std::size_t>(getpagesize());
    const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(address);
    const std::uintptr_t pageBegin = begin & ~(pageSize - 1);
    const std::uintptr_t pageEnd =
        (begin + length + pageSize - 1) & ~(pageSize - 1);

    const kern_return_t result = mach_vm_protect(
        mach_task_self(),
        static_cast<mach_vm_address_t>(pageBegin),
        static_cast<mach_vm_size_t>(pageEnd - pageBegin),
        false,
        protection);

    if (result != KERN_SUCCESS) {
        std::ostringstream stream;
        stream << "mach_vm_protect failed: "
               << mach_error_string(result)
               << " (" << result << ')';
        error = stream.str();
        return false;
    }

    return true;
}

bool WriteCode(
    void* destination,
    const void* source,
    const std::size_t length,
    std::string& error) {
    if (!SetCodeProtection(
            destination,
            length,
            VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY,
            error)) {
        return false;
    }

    std::memcpy(destination, source, length);
    __builtin___clear_cache(
        static_cast<char*>(destination),
        static_cast<char*>(destination) + length);

    if (!SetCodeProtection(
            destination,
            length,
            VM_PROT_READ | VM_PROT_EXECUTE,
            error)) {
        return false;
    }

    return true;
}

void DestroyTrampoline(InlineHook& hook) {
    if (hook.trampoline != nullptr && hook.trampolineSize != 0) {
        munmap(hook.trampoline, hook.trampolineSize);
    }
    hook.trampoline = nullptr;
    hook.trampolineSize = 0;
}

bool CreateTrampoline(InlineHook& hook, std::string& error) {
    const std::size_t pageSize = static_cast<std::size_t>(getpagesize());
    void* memory = mmap(
        nullptr,
        pageSize,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANON,
        -1,
        0);

    if (memory == MAP_FAILED) {
        std::ostringstream stream;
        stream << "mmap for trampoline failed: " << std::strerror(errno);
        error = stream.str();
        return false;
    }

    hook.trampoline = memory;
    hook.trampolineSize = pageSize;

    std::memcpy(
        hook.trampoline,
        hook.originalBytes.data(),
        hook.originalBytes.size());

    const auto jumpBack = MakeAbsoluteJump(
        static_cast<std::uint8_t*>(hook.target) + InlineHook::kPatchSize);
    std::memcpy(
        static_cast<std::uint8_t*>(hook.trampoline) + InlineHook::kPatchSize,
        jumpBack.data(),
        jumpBack.size());

    __builtin___clear_cache(
        static_cast<char*>(hook.trampoline),
        static_cast<char*>(hook.trampoline) +
            InlineHook::kPatchSize + jumpBack.size());

    if (mprotect(hook.trampoline, pageSize, PROT_READ | PROT_EXEC) != 0) {
        std::ostringstream stream;
        stream << "mprotect trampoline RX failed: " << std::strerror(errno);
        error = stream.str();
        DestroyTrampoline(hook);
        return false;
    }

    return true;
}

}  // namespace

bool InstallInlineHook(
    InlineHook& hook,
    void* target,
    void* replacement,
    const bool createTrampoline,
    std::string& error,
    void** trampolinePublication) {
    if (hook.installed || target == nullptr || replacement == nullptr) {
        error = "invalid hook state or null address";
        return false;
    }

    hook.target = target;
    std::memcpy(
        hook.originalBytes.data(),
        hook.target,
        hook.originalBytes.size());

    if (createTrampoline && !CreateTrampoline(hook, error)) {
        hook.target = nullptr;
        return false;
    }

    if (trampolinePublication != nullptr) {
        if (!createTrampoline || hook.trampoline == nullptr) {
            error = "trampoline publication requested without trampoline";
            DestroyTrampoline(hook);
            hook.target = nullptr;
            return false;
        }
        *trampolinePublication = hook.trampoline;
    }

    const auto jump = MakeAbsoluteJump(replacement);
    if (!WriteCode(hook.target, jump.data(), jump.size(), error)) {
        if (trampolinePublication != nullptr) {
            *trampolinePublication = nullptr;
        }
        DestroyTrampoline(hook);
        hook.target = nullptr;
        return false;
    }

    hook.installed = true;
    return true;
}

bool RemoveInlineHook(InlineHook& hook, std::string& error) {
    if (!hook.installed) {
        DestroyTrampoline(hook);
        return true;
    }

    if (!WriteCode(
            hook.target,
            hook.originalBytes.data(),
            hook.originalBytes.size(),
            error)) {
        return false;
    }

    hook.installed = false;
    hook.target = nullptr;
    DestroyTrampoline(hook);
    return true;
}

bool InstallInstructionPatch(
    InstructionPatch& patch,
    void* target,
    const std::uint32_t replacement,
    std::string& error) {
    if (patch.installed || target == nullptr ||
        reinterpret_cast<std::uintptr_t>(target) % alignof(std::uint32_t) != 0) {
        error = "invalid instruction patch state or address";
        return false;
    }

    patch.target = target;
    std::memcpy(
        patch.originalBytes.data(),
        patch.target,
        patch.originalBytes.size());

    if (!WriteCode(
            patch.target,
            &replacement,
            sizeof(replacement),
            error)) {
        patch.target = nullptr;
        return false;
    }

    patch.installed = true;
    return true;
}

bool RemoveInstructionPatch(InstructionPatch& patch, std::string& error) {
    if (!patch.installed) {
        return true;
    }

    if (!WriteCode(
            patch.target,
            patch.originalBytes.data(),
            patch.originalBytes.size(),
            error)) {
        return false;
    }

    patch.installed = false;
    patch.target = nullptr;
    return true;
}

}  // namespace bg3cam
