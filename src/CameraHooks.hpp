#pragma once

#include "HookInstaller.hpp"

#include <cstdio>
#include <cstdint>

namespace bg3cam {

struct CameraHookSet {
    InlineHook inputEventHook;
    InlineHook fireInputEventsHook;
    InlineHook stickHook;
    InlineHook cameraInputEventHook;
    InlineHook updateGameCameraBehaviorHook;
    InlineHook afterZoomHook;
    InlineHook preYawHook;
    InlineHook preGateHook;
    InlineHook pitchHook;
    InstructionPatch keyboardMovementPatch;
};

enum class CameraHookReadiness {
    Waiting,
    Ready,
    Invalid,
};

// DYLD_INSERT_LIBRARIES runs our constructor before BG3's camera subsystem
// initializes its global CameraDefinition base. This check decodes the
// verified ADRP+LDR site and distinguishes that temporary null from a corrupt
// or unexpected instruction site.
CameraHookReadiness CheckCameraHookReadiness(
    std::uint8_t* pitchDefinitionSiteAddress,
    FILE* log);

// Enables already-installed dormant hooks after the camera definitions exist
// and their observed fields pass sanity validation.
bool ActivateCameraHooks(FILE* log);

bool InstallCameraHooks(
    CameraHookSet& hooks,
    std::uint8_t* getPitchAddress,
    std::uint8_t* onStickChangedAddress,
    std::uint8_t* inputEventAddress,
    std::uint8_t* fireInputEventsAddress,
    std::uint8_t* cameraInputEventAddress,
    std::uint8_t* updateGameCameraBehaviorAddress,
    std::uint8_t* afterZoomSiteAddress,
    std::uint8_t* preYawSiteAddress,
    std::uint8_t* preGateSiteAddress,
    std::uint8_t* pitchDefinitionSiteAddress,
    std::uint8_t* collideWithObstaclesAddress,
    std::uint8_t* restrictCamTargetDestHeightAddress,
    std::uint8_t* keyboardMovementGuardAddress,
    FILE* log);

void RemoveCameraHooks(CameraHookSet& hooks, FILE* log);

}  // namespace bg3cam
