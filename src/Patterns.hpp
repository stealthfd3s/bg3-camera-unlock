#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace bg3cam {

struct NamedPattern {
    const char* name;
    std::span<const std::uint8_t> bytes;
    // Empty means every byte must match.
    std::span<const std::uint8_t> mask{};
};

// Builds a mask for an ARM64 instruction sequence: every instruction must
// match exactly except the listed ones, whose four bytes are ignored.
//
// The wildcards are always operand fields the linker rewrites when the game
// binary is laid out again - BL/B targets, ADRP page offsets, and the LDR
// immediate that follows an ADRP. Their opcodes stay pinned, so a signature
// still identifies one exact instruction sequence; only the addresses baked
// into it are allowed to move. Without this, a signature spanning any such
// instruction breaks on every game patch even when the code is unchanged.
template <std::size_t Instructions, std::size_t Wildcards>
constexpr std::array<std::uint8_t, Instructions * 4> MakeInstructionMask(
    const std::array<std::size_t, Wildcards>& wildcardInstructions) {
    std::array<std::uint8_t, Instructions * 4> mask{};
    for (std::uint8_t& byte : mask) {
        byte = 0xff;
    }

    for (const std::size_t instruction : wildcardInstructions) {
        for (std::size_t byte = 0; byte < 4; ++byte) {
            mask[instruction * 4 + byte] = 0x00;
        }
    }

    return mask;
}

// ecl::GameCameraBehavior::GetCameraPitchDegrees(bool, bool) const
//
// ldrb w8, [x0, #0x1e4]
// cbz  w8, ...
// tbnz w1, #0, ...
// ldr  s0, [x0, #0x1e0]
// ret
inline constexpr std::array<std::uint8_t, 28> kGetPitchBytes{
    0x08, 0x90, 0x47, 0x39,
    0x88, 0x00, 0x00, 0x34,
    0x61, 0x00, 0x00, 0x37,
    0x00, 0xe0, 0x41, 0xbd,
    0xc0, 0x03, 0x5f, 0xd6,
    0x0b, 0x3c, 0x41, 0xb9,
    0x09, 0xa0, 0x42, 0x39,
};

// cocoa::MFIInputDevice::OnStickChanged(int, float)
//
// The first instructions build a 0xa0-byte stack frame and save the
// floating-point and general-purpose callee-saved registers.
inline constexpr std::array<std::uint8_t, 48> kOnStickChangedBytes{
    0xff, 0x83, 0x02, 0xd1,
    0xed, 0x33, 0x01, 0x6d,
    0xeb, 0x2b, 0x02, 0x6d,
    0xe9, 0x23, 0x03, 0x6d,
    0xfc, 0x6f, 0x04, 0xa9,
    0xfa, 0x67, 0x05, 0xa9,
    0xf8, 0x5f, 0x06, 0xa9,
    0xf6, 0x57, 0x07, 0xa9,
    0xf4, 0x4f, 0x08, 0xa9,
    0xfd, 0x7b, 0x09, 0xa9,
    0xfd, 0x43, 0x02, 0x91,
    0x08, 0x4c, 0x21, 0x8b,
};

// ecl::InputController::OnInputEvent(ls::InputEvent const&)
//
// At entry:
//   x0 = InputController*
//   x1 = InputEvent*
//
// The first four instructions only create the stack frame and save
// callee-saved registers, so they can safely be copied to a trampoline.
inline constexpr std::array<std::uint8_t, 48> kInputEventBytes{
    0xfa, 0x67, 0xbb, 0xa9,
    0xf8, 0x5f, 0x01, 0xa9,
    0xf6, 0x57, 0x02, 0xa9,
    0xf4, 0x4f, 0x03, 0xa9,
    0xfd, 0x7b, 0x04, 0xa9,
    0xfd, 0x03, 0x01, 0x91,
    0xf4, 0x03, 0x01, 0xaa,
    0xf3, 0x03, 0x00, 0xaa,
    0x37, 0x00, 0x40, 0xb9,
    0x35, 0x70, 0x40, 0x39,
    0x01, 0x80, 0x40, 0xa9,
    0x36, 0x1e, 0x73, 0x97,  // BL, target wildcarded
};

inline constexpr auto kInputEventMask =
    MakeInstructionMask<12>(std::array<std::size_t, 1>{11});

// ls::InputManager::FireInputEvents(
//     ls::Span<ls::input::FireEventDesc const> const&, bool)
//
// This is the first common dispatch point at which ToggleInputMode still has
// its registered input ID (0xC0). The first 16 bytes only allocate the stack
// frame and save registers, so they are safe to relocate into a trampoline.
inline constexpr std::array<std::uint8_t, 48> kFireInputEventsBytes{
    0xff, 0x03, 0x04, 0xd1,
    0xfc, 0x6f, 0x0a, 0xa9,
    0xfa, 0x67, 0x0b, 0xa9,
    0xf8, 0x5f, 0x0c, 0xa9,
    0xf6, 0x57, 0x0d, 0xa9,
    0xf4, 0x4f, 0x0e, 0xa9,
    0xfd, 0x7b, 0x0f, 0xa9,
    0xfd, 0xc3, 0x03, 0x91,
    0xe0, 0x0f, 0x00, 0xf9,
    0x48, 0x24, 0x40, 0xa9,
    0x1f, 0x01, 0x09, 0xeb,
    0x60, 0x39, 0x00, 0x54,
};

// ecl::CameraSystem::OnInputEvent(
//     ecs::EntityRef const&, ls::InputEvent const&)
//
// At entry:
//   x0 = CameraSystem*
//   x1 = EntityRef*
//   x2 = InputEvent*
//
// The first 16 bytes are position-independent stack/register saves and are
// therefore safe to relocate into the trampoline.
inline constexpr std::array<std::uint8_t, 44> kCameraInputEventBytes{
    0xff, 0xc3, 0x04, 0xd1,
    0xe9, 0x23, 0x0c, 0x6d,
    0xfc, 0x6f, 0x0d, 0xa9,
    0xfa, 0x67, 0x0e, 0xa9,
    0xf8, 0x5f, 0x0f, 0xa9,
    0xf6, 0x57, 0x10, 0xa9,
    0xf4, 0x4f, 0x11, 0xa9,
    0xfd, 0x7b, 0x12, 0xa9,
    0xfd, 0x83, 0x04, 0x91,
    0xf4, 0x03, 0x01, 0xaa,
    0xf5, 0x03, 0x00, 0xaa,
};

// ecl::CameraSystem::UpdateGameCameraBehavior(...)
//
// We wrap this whole function while the three pitch approach rates are
// temporarily raised so our pitch lands within the frame. Its first 16 bytes
// are position-independent register saves.
inline constexpr std::array<std::uint8_t, 64> kUpdateGameCameraBehaviorBytes{
    0xef, 0x3b, 0xb6, 0x6d,
    0xed, 0x33, 0x01, 0x6d,
    0xeb, 0x2b, 0x02, 0x6d,
    0xe9, 0x23, 0x03, 0x6d,
    0xfc, 0x6f, 0x04, 0xa9,
    0xfa, 0x67, 0x05, 0xa9,
    0xf8, 0x5f, 0x06, 0xa9,
    0xf6, 0x57, 0x07, 0xa9,
    0xf4, 0x4f, 0x08, 0xa9,
    0xfd, 0x7b, 0x09, 0xa9,
    0xfd, 0x43, 0x02, 0x91,
    0xff, 0xc3, 0x14, 0xd1,
    0xf7, 0x03, 0x03, 0xaa,
    0xf4, 0x03, 0x02, 0xaa,
    0xfc, 0x03, 0x01, 0xaa,
    0xe0, 0x97, 0x00, 0xf9,
};

// Inside UpdateGameCameraBehavior, this site resolves the global block that
// owns the three CameraDefinition objects used by the pitch update. The first
// ADRP+LDR pair is decoded at runtime; no absolute address is embedded.
inline constexpr std::array<std::uint8_t, 48> kPitchDefinitionSiteBytes{
    0x28, 0xbf, 0x02, 0xd0,  // ADRP, page offset wildcarded
    0x08, 0xa1, 0x47, 0xf9,  // LDR, immediate wildcarded
    0x49, 0x6c, 0x82, 0x52,
    0x09, 0x69, 0x69, 0x38,
    0x69, 0x00, 0x00, 0x34,
    0x15, 0x01, 0x32, 0x91,
    0x07, 0x00, 0x00, 0x14,
    0xe9, 0x7b, 0x40, 0xf9,
    0x29, 0x01, 0x40, 0x39,
    0x0a, 0x11, 0x1f, 0x91,
    0x08, 0x61, 0x25, 0x91,
    0x3f, 0x01, 0x00, 0x72,
};

// The two wildcards are decoded at run time by the ADRP+LDR reader, so their
// operands are recovered rather than assumed. The mov/add immediates around
// them - 0x1362, 0xc80, 0x7c4, 0x958 - stay required, which is what keeps the
// structure offsets this mod relies on verified rather than trusted.
inline constexpr auto kPitchDefinitionSiteMask =
    MakeInstructionMask<12>(std::array<std::size_t, 2>{0, 1});

// Verification-only site inside cocoa::MFIInputDevice::UpdateControls().
// It polls extendedGamepad.leftThumbstickButton.isPressed and stores the
// resulting 32-bit state at MFIInputDevice + 0xbc. The hook reads that field,
// so this pattern makes a layout change fail closed after a game update.
inline constexpr std::array<std::uint8_t, 48> kL3PollingSiteBytes{
    0x60, 0xbe, 0x40, 0xf9,
    0x2a, 0x50, 0xb0, 0x95,  // BL, target wildcarded
    0x29, 0x53, 0xb0, 0x95,  // BL, target wildcarded
    0xb8, 0x52, 0xb0, 0x95,  // BL, target wildcarded
    0x68, 0xbe, 0x40, 0xb9,  // ldr w8, [x19, #0xbc]
    0x1f, 0x01, 0x00, 0x6b,
    0x60, 0x02, 0x00, 0x54,
    0x28, 0x00, 0x80, 0x52,
    0x68, 0xe2, 0x02, 0x39,
    0x60, 0xbe, 0x00, 0xb9,  // str w0, [x19, #0xbc]
    0x48, 0xfb, 0x03, 0x90,  // ADRP, page offset wildcarded
    0x08, 0x85, 0x46, 0xf9,  // LDR, immediate wildcarded
};

inline constexpr auto kL3PollingSiteMask =
    MakeInstructionMask<12>(std::array<std::size_t, 5>{1, 2, 3, 10, 11});

// ecl::CameraHelpers::CollideWithObstacles(...)
//
// This is BG3's own camera-aware segment raycast. At the machine-code ABI
// level x0 is the returned CollisionResult storage, followed by:
//   x1 = CameraBlocker/Scenery/Terrain WorldView
//   x2 = PhysicsSceneBase
//   w3 = collision mode flag
//   x4 = segment start Vector3f
//   x5 = segment delta Vector3f
//
// The prologue and argument moves contain no PC-relative instructions, and
// the longer signature deliberately makes a game-version mismatch fail
// closed instead of calling an uncertain function address.
inline constexpr std::array<std::uint8_t, 64>
    kCollideWithObstaclesBytes{
        0xff, 0xc3, 0x03, 0xd1,
        0xfa, 0x67, 0x0a, 0xa9,
        0xf8, 0x5f, 0x0b, 0xa9,
        0xf6, 0x57, 0x0c, 0xa9,
        0xf4, 0x4f, 0x0d, 0xa9,
        0xfd, 0x7b, 0x0e, 0xa9,
        0xfd, 0x83, 0x03, 0x91,
        0xf4, 0x03, 0x05, 0xaa,
        0xf7, 0x03, 0x04, 0xaa,
        0xf5, 0x03, 0x03, 0xaa,
        0xf8, 0x03, 0x02, 0xaa,
        0xf6, 0x03, 0x01, 0xaa,
        0xf3, 0x03, 0x00, 0xaa,
        0x1f, 0x7c, 0x00, 0xa9,
        0x1f, 0x08, 0x00, 0xf9,
        0x08, 0x10, 0xb0, 0x12,
    };

// ecl::CameraHelpers::RestrictCamTargetDestHeight(...)
//
// Despite the broad C++ name, this is the floor-level query used directly by
// UpdateGameCameraBehavior. LTO reduced its machine ABI to the five arguments
// the implementation actually consumes; it returns the 12-byte floor result
// in x0/x1.
inline constexpr std::array<std::uint8_t, 64>
    kRestrictCamTargetDestHeightBytes{
        0xff, 0x03, 0x06, 0xd1,
        0xe9, 0x23, 0x11, 0x6d,
        0xfc, 0x6f, 0x12, 0xa9,
        0xfa, 0x67, 0x13, 0xa9,
        0xf8, 0x5f, 0x14, 0xa9,
        0xf6, 0x57, 0x15, 0xa9,
        0xf4, 0x4f, 0x16, 0xa9,
        0xfd, 0x7b, 0x17, 0xa9,
        0xfd, 0xc3, 0x05, 0x91,
        0xa8, 0x89, 0x02, 0xb0,  // ADRP, page offset wildcarded
        0x08, 0x61, 0x46, 0xf9,  // LDR, immediate wildcarded
        0x08, 0x01, 0x40, 0xf9,
        0xa8, 0x03, 0x19, 0xf8,
        0x88, 0x48, 0x40, 0xf9,
        0xe8, 0x0f, 0x00, 0xb4,
        0x18, 0x41, 0x40, 0xf9,
    };

inline constexpr auto kRestrictCamTargetDestHeightMask =
    MakeInstructionMask<16>(std::array<std::size_t, 2>{9, 10});

// Mid-function site inside CameraSystem::UpdateGameCameraBehavior, after the
// game has finalized currentZoomA/currentZoomB and immediately before it
// advances to the camera+0x254 phase. The first 16 bytes only copy the camera
// root position and are position-independent, so they are safe to relocate.
//
// This is the first point at which the frame's final camera arm is known and
// can still be shortened before the transform is built.
inline constexpr std::array<std::uint8_t, 48> kAfterZoomSiteBytes{
    0x48, 0x43, 0x42, 0xf8,
    0x48, 0x0f, 0x00, 0xf9,
    0x48, 0x2f, 0x40, 0xb9,
    0x48, 0x23, 0x00, 0xb9,
    0x48, 0x53, 0x49, 0x39,
    0x28, 0xbe, 0x00, 0x35,
    0x48, 0x13, 0x49, 0x39,
    0xe8, 0xbd, 0x00, 0x35,
    0xf4, 0x32, 0x40, 0xf9,
    0xf3, 0x02, 0x40, 0xf9,
    0xe8, 0x83, 0x10, 0x91,
    0xe0, 0x03, 0x1c, 0xaa,
};

// Verification-only site inside ecl::CameraSystem::UpdateGameCameraBehavior.
//
// This is the yaw follow servo's rate lookup, and it is the reason the
// suppression in CameraHooks.cpp can name field offsets at all: every offset
// that code writes to appears literally in these instructions.
//
//   ldr  x8, [sp, #0xf0]
//   ldrb w8, [x8]
//   add  x9,  x20, #0x7c4      // controller CameraDefinition
//   add  x10, x20, #0x958      // default CameraDefinition
//   tst  w8, #0x1
//   csel x11, x9, x10, eq
//   ldr  s10, [x11, #0x130]    // maximum follow rate
//   ldr  s11, [x11, #0x12c]    // minimum follow rate
//   tst  w8, #0x1
//   csel x8, x9, x10, eq
//   ldr  s1,  [x8, #0x128]     // follow curve exponent
//
// No operand here is linker-relocated, so the sequence needs no wildcards and
// pins 0x7c4, 0x958, 0x128, 0x12c and 0x130 exactly. If a game update moves
// the follow rates, this stops matching and the mod refuses to install rather
// than zeroing two fields whose meaning it no longer knows.
inline constexpr std::array<std::uint8_t, 44> kYawFollowRateSiteBytes{
    0xe8, 0x7b, 0x40, 0xf9,  // ldr  x8, [sp, #0xf0]
    0x08, 0x01, 0x40, 0x39,  // ldrb w8, [x8]
    0x89, 0x12, 0x1f, 0x91,  // add  x9, x20, #0x7c4
    0x8a, 0x62, 0x25, 0x91,  // add  x10, x20, #0x958
    0x1f, 0x01, 0x00, 0x72,  // tst  w8, #0x1
    0x2b, 0x01, 0x8a, 0x9a,  // csel x11, x9, x10, eq
    0x6a, 0x31, 0x41, 0xbd,  // ldr  s10, [x11, #0x130]
    0x6b, 0x2d, 0x41, 0xbd,  // ldr  s11, [x11, #0x12c]
    0x1f, 0x01, 0x00, 0x72,  // tst  w8, #0x1
    0x28, 0x01, 0x8a, 0x9a,  // csel x8, x9, x10, eq
    0x01, 0x29, 0x41, 0xbd,  // ldr  s1, [x8, #0x128]
};

// ecl::CharacterTask_MoveController::CanExecute(), immediately around the
// input-mode guard that prevents the game's existing CharacterMoveForward,
// CharacterMoveBackward, CharacterMoveLeft and CharacterMoveRight actions
// from reaching character movement while keyboard UI mode is active.
//
// The patch replaces only the CBZ at +8 with an ARM64 NOP. In controller mode
// the condition already falls through, so removing the branch changes only
// the keyboard path. The surrounding object offsets are pinned to make a game
// update fail closed rather than patching an uncertain branch.
inline constexpr std::array<std::uint8_t, 40> kKeyboardMovementGuardBytes{
    0x08, 0xb7, 0x02, 0xb0,  // adrp x8, input-mode global
    0x08, 0xc1, 0x75, 0x39,  // ldrb w8, [x8, #0xd70]
    0xe8, 0x16, 0x00, 0x34,  // cbz w8, return false (patched at +8)
    0x68, 0x36, 0x40, 0xf9,  // ldr x8, [x19, #0x68]
    0x09, 0x29, 0x41, 0xb9,  // ldr w9, [x8, #0x128]
    0x3f, 0x41, 0x40, 0x31,  // cmn w9, #0x10, lsl #12
    0x40, 0x16, 0x00, 0x54,  // b.eq ...
    0x16, 0x51, 0x40, 0xf9,  // ldr x22, [x8, #0xa0]
    0xc8, 0x22, 0x41, 0x39,  // ldrb w8, [x22, #0x48]
    0xc8, 0x01, 0x00, 0x36,  // tbz w8, #0, ...
};

inline constexpr auto kKeyboardMovementGuardMask =
    MakeInstructionMask<10>(std::array<std::size_t, 4>{0, 2, 6, 9});

// CameraSystem::UpdateGameCameraBehavior, immediately before the update
// selects and reads its rotation input channel:
//
//   add   x8,  x26, #0x9c      // stick channel, currentAngleDelta
//   ldrb  w9,  [x26, #0xa9]    // kMouseRotation flag byte
//   add   x10, x26, #0xa0      // pointer channel, mouseRotationDelta
//   tst   w9,  #0x1
//   csel  x8,  x8, x10
//   ldr   s9,  [x8]            // the channel is consumed here
//
// Only the first four instructions are displaced by the 16-byte patch. The
// remaining two are carried in the signature as context: the csel and the load
// are what make this site the channel read rather than some other use of the
// same three offsets, and a game update that reorders them must fail to match
// rather than patch a site whose meaning has changed.
//
// No operand here is linker-relocated, so the sequence needs no wildcards and
// pins 0x9c, 0xa9 and 0xa0 exactly.
inline constexpr std::array<std::uint8_t, 24> kPreYawSiteBytes{
    0x48, 0x73, 0x02, 0x91,  // add  x8,  x26, #0x9c
    0x49, 0xa7, 0x42, 0x39,  // ldrb w9,  [x26, #0xa9]
    0x4a, 0x83, 0x02, 0x91,  // add  x10, x26, #0xa0
    0x3f, 0x01, 0x00, 0x72,  // tst  w9,  #0x1
    0x08, 0x01, 0x8a, 0x9a,  // csel x8,  x8, x10, eq
    0x09, 0x01, 0x40, 0xbd,  // ldr  s9,  [x8]
};

// CameraSystem::UpdateGameCameraBehavior, at the dominator of the gate that
// decides whether the rotation input channel is read at all.
//
//   ldr  x20, [x23, #0x00]     <- displaced by the 16-byte patch
//   ldr  x19, [x23, #0x30]     <- displaced
//   ldr  x21, [x23, #0x60]     <- displaced
//   ldr  s0,  [x26, #0xe0]     <- displaced
//   fcmp s0,  #0.0             ) context only, never patched
//   b.hi <forward>             ) its offset is relative and internal, so it
//   ldr  s0,  [x26, #0xdc]     ) survives a relayout that leaves the function
//   fcmp s0,  #0.0             ) itself unchanged
//
// The two camera-relative loads are what tie this signature to the object
// rather than to a stack frame: a changed camera layout stops the match
// instead of patching a site whose meaning moved. No operand is linker
// relocated, so no wildcards are needed.
inline constexpr std::array<std::uint8_t, 32> kPreGateSiteBytes{
    0xf4, 0x02, 0x40, 0xf9,  // ldr  x20, [x23, #0x00]
    0xf3, 0x1a, 0x40, 0xf9,  // ldr  x19, [x23, #0x30]
    0xf5, 0x32, 0x40, 0xf9,  // ldr  x21, [x23, #0x60]
    0x40, 0xe3, 0x40, 0xbd,  // ldr  s0,  [x26, #0xe0]
    0x08, 0x20, 0x20, 0x1e,  // fcmp s0,  #0.0
    0x88, 0x04, 0x00, 0x54,  // b.hi
    0x40, 0xdf, 0x40, 0xbd,  // ldr  s0,  [x26, #0xdc]
    0x08, 0x20, 0x20, 0x1e,  // fcmp s0,  #0.0
};

inline constexpr NamedPattern kGetPitchPattern{
    "GetCameraPitchDegrees",
    kGetPitchBytes,
};

inline constexpr NamedPattern kOnStickChangedPattern{
    "MFIInputDevice::OnStickChanged",
    kOnStickChangedBytes,
};

inline constexpr NamedPattern kInputEventPattern{
    "InputController::OnInputEvent",
    kInputEventBytes,
    kInputEventMask,
};

inline constexpr NamedPattern kL3PollingSitePattern{
    "MFIInputDevice::L3PollingSite",
    kL3PollingSiteBytes,
    kL3PollingSiteMask,
};

inline constexpr NamedPattern kFireInputEventsPattern{
    "InputManager::FireInputEvents",
    kFireInputEventsBytes,
};

inline constexpr NamedPattern kCameraInputEventPattern{
    "CameraSystem::OnInputEvent",
    kCameraInputEventBytes,
};

inline constexpr NamedPattern kUpdateGameCameraBehaviorPattern{
    "CameraSystem::UpdateGameCameraBehavior",
    kUpdateGameCameraBehaviorBytes,
};

inline constexpr NamedPattern kPitchDefinitionSitePattern{
    "CameraDefinition resolution site",
    kPitchDefinitionSiteBytes,
    kPitchDefinitionSiteMask,
};

inline constexpr NamedPattern kCollideWithObstaclesPattern{
    "CameraHelpers::CollideWithObstacles",
    kCollideWithObstaclesBytes,
};

inline constexpr NamedPattern kRestrictCamTargetDestHeightPattern{
    "CameraHelpers::RestrictCamTargetDestHeight",
    kRestrictCamTargetDestHeightBytes,
    kRestrictCamTargetDestHeightMask,
};

inline constexpr NamedPattern kAfterZoomSitePattern{
    "CameraSystem::UpdateGameCameraBehavior post-zoom site",
    kAfterZoomSiteBytes,
};

inline constexpr NamedPattern kYawFollowRateSitePattern{
    "CameraSystem::UpdateGameCameraBehavior yaw follow rate site",
    kYawFollowRateSiteBytes,
};

inline constexpr NamedPattern kPreGateSitePattern{
    "CameraSystem::UpdateGameCameraBehavior pre-gate site",
    kPreGateSiteBytes,
};

inline constexpr NamedPattern kPreYawSitePattern{
    "CameraSystem::UpdateGameCameraBehavior pre-yaw channel site",
    kPreYawSiteBytes,
};

// Only required by the Camera + WASD flavor: it is the site the keyboard-mode
// character-movement guard is NOPed at. The Camera Only flavor never touches
// it, so it is not part of that build's activation contract.
inline constexpr NamedPattern kKeyboardMovementGuardPattern{
    "CharacterTask_MoveController keyboard-mode guard",
    kKeyboardMovementGuardBytes,
    kKeyboardMovementGuardMask,
};

// Fall back to the historically shipped flavor if a translation unit includes
// this header without the build define (it should not, but a silent drop of
// WASD support would be worse than a harmless default).
#ifndef BG3_CAMERA_WITH_WASD
#define BG3_CAMERA_WITH_WASD 1
#endif

inline constexpr std::array<NamedPattern,
#if BG3_CAMERA_WITH_WASD
    15
#else
    14
#endif
> kRequiredPatterns{
    kGetPitchPattern,
    kOnStickChangedPattern,
    kInputEventPattern,
    kFireInputEventsPattern,
    kL3PollingSitePattern,
    kCameraInputEventPattern,
    kUpdateGameCameraBehaviorPattern,
    kPitchDefinitionSitePattern,
    kCollideWithObstaclesPattern,
    kRestrictCamTargetDestHeightPattern,
    kAfterZoomSitePattern,
    kPreGateSitePattern,
    kPreYawSitePattern,
    kYawFollowRateSitePattern,
#if BG3_CAMERA_WITH_WASD
    kKeyboardMovementGuardPattern,
#endif
};

}  // namespace bg3cam
