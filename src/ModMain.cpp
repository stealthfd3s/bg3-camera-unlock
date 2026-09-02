#include "CameraHooks.hpp"
#include "CameraConfig.hpp"
#include "MachOImage.hpp"
#include "PatternScanner.hpp"
#include "Patterns.hpp"

#ifndef BG3_CAMERA_WITH_WASD
#define BG3_CAMERA_WITH_WASD 1
#endif
#ifndef BG3_CAMERA_VERSION
#define BG3_CAMERA_VERSION "development"
#endif
#ifndef BG3_CAMERA_FLAVOR
#define BG3_CAMERA_FLAVOR (BG3_CAMERA_WITH_WASD ? "camera-wasd" : "camera-only")
#endif

#include <atomic>
#include <chrono>
#include <dlfcn.h>
#include <mach-o/loader.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <span>
#include <string>
#include <thread>

namespace bg3cam {
namespace {

constexpr const char* kLogPath = "/tmp/bg3-camera-unlock.log";
constexpr const char* kLogPathEnvironment = "BG3_CAMERA_UNLOCK_LOG";
constexpr const char* kStatusPathEnvironment =
    "BG3_CAMERA_UNLOCK_STATUS_FILE";
constexpr const char* kConfigPathEnvironment =
    "BG3_CAMERA_UNLOCK_CONFIG";
constexpr auto kReadinessPollInterval = std::chrono::milliseconds(50);
constexpr auto kReadinessTimeout = std::chrono::seconds(120);
CameraHookSet gHooks;
bool gInstalled = false;
FILE* gLogFile = nullptr;
std::uint8_t* gPitchDefinitionSiteAddress = nullptr;
std::atomic<bool> gStopRequested{false};
std::thread gReadinessThread;

const char* GetLogPath() {
    const char* configuredPath = std::getenv(kLogPathEnvironment);
    if (configuredPath != nullptr && configuredPath[0] != '\0') {
        return configuredPath;
    }
    return kLogPath;
}

void ReportLauncherStatus(const char* status) {
    const char* statusPath = std::getenv(kStatusPathEnvironment);
    if (statusPath == nullptr || statusPath[0] == '\0') {
        return;
    }

    const std::string temporaryPath =
        std::string(statusPath) + ".tmp";
    FILE* statusFile = std::fopen(temporaryPath.c_str(), "w");
    if (statusFile == nullptr) {
        return;
    }

    std::fprintf(statusFile, "%s\n", status);
    std::fclose(statusFile);
    std::rename(temporaryPath.c_str(), statusPath);
}

void Log(FILE* file, const char* level, const char* message) {
    std::time_t now = std::time(nullptr);
    std::tm localTime{};
    localtime_r(&now, &localTime);

    char timeBuffer[32]{};
    std::strftime(
        timeBuffer,
        sizeof(timeBuffer),
        "%Y-%m-%d %H:%M:%S",
        &localTime);

    std::fprintf(file, "[%s] [%s] %s\n", timeBuffer, level, message);
    std::fflush(file);
}

// Says which build is actually running.
//
// Its absence is what let a session be tested against a stale copy of the
// dylib without anything in the log saying so: every line described the game,
// none described the mod. The path answers which file the loader picked, and
// the UUID answers which build that file is, independently of a version
// string that several builds can share.
void LogModuleIdentity(FILE* log) {
    Dl_info info{};
    if (dladdr(reinterpret_cast<const void*>(&LogModuleIdentity), &info) == 0 ||
        info.dli_fname == nullptr ||
        info.dli_fbase == nullptr) {
        std::fprintf(log, "[INFO] module identity unavailable\n");
        return;
    }

    char uuidText[37];
    std::snprintf(uuidText, sizeof(uuidText), "unavailable");

    const auto* header =
        static_cast<const mach_header_64*>(info.dli_fbase);
    if (header->magic == MH_MAGIC_64) {
        const auto* command =
            reinterpret_cast<const load_command*>(header + 1);
        for (std::uint32_t index = 0; index < header->ncmds; ++index) {
            if (command->cmd == LC_UUID) {
                const auto* uuidCommand =
                    reinterpret_cast<const uuid_command*>(command);
                const std::uint8_t* u = uuidCommand->uuid;
                std::snprintf(
                    uuidText,
                    sizeof(uuidText),
                    "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-"
                    "%02X%02X%02X%02X%02X%02X",
                    u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7],
                    u[8], u[9], u[10], u[11], u[12], u[13], u[14], u[15]);
                break;
            }
            command = reinterpret_cast<const load_command*>(
                reinterpret_cast<const std::uint8_t*>(command) +
                command->cmdsize);
        }
    }

    const char* version = std::getenv("BG3_CAMERA_UNLOCK_VERSION");
    std::fprintf(
        log,
        "[INFO] module=%s uuid=%s version=%s\n",
        info.dli_fname,
        uuidText,
        version != nullptr && version[0] != '\0' ? version : "unset");
    std::fflush(log);
}

void WaitForCameraSubsystemAndActivate() {
    FILE* log = gLogFile;
    if (log == nullptr || gPitchDefinitionSiteAddress == nullptr) {
        ReportLauncherStatus("abort: deferred activation state is incomplete");
        return;
    }

    Log(
        log,
        "DEFER",
        "Hooks are dormant; waiting for BG3 CameraDefinition initialization");
    ReportLauncherStatus("waiting");

    const auto startedAt = std::chrono::steady_clock::now();
    while (!gStopRequested.load(std::memory_order_acquire)) {
        const CameraHookReadiness readiness = CheckCameraHookReadiness(
            gPitchDefinitionSiteAddress,
            log);

        if (readiness == CameraHookReadiness::Invalid) {
            Log(
                log,
                "ABORT",
                "Deferred CameraDefinition instruction validation failed; "
                "hooks remain dormant");
            ReportLauncherStatus(
                "abort: deferred CameraDefinition validation failed");
            return;
        }

        if (readiness == CameraHookReadiness::Ready) {
            const auto elapsed = std::chrono::duration_cast<
                std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startedAt);
            std::fprintf(
                log,
                "[INFO] CameraDefinition became ready after %lld ms\n",
                static_cast<long long>(elapsed.count()));
            std::fflush(log);

            if (!ActivateCameraHooks(log)) {
                Log(
                    log,
                    "ABORT",
                    "Deferred CameraDefinition data validation failed; "
                    "hooks remain dormant");
                ReportLauncherStatus(
                    "abort: deferred CameraDefinition data validation failed");
                return;
            }

            ReportLauncherStatus("active");
            return;
        }

        if (std::chrono::steady_clock::now() - startedAt >=
            kReadinessTimeout) {
            Log(
                log,
                "ABORT",
                "Timed out waiting for CameraDefinition; hooks remain dormant");
            ReportLauncherStatus(
                "abort: timed out waiting for the BG3 camera subsystem");
            return;
        }

        std::this_thread::sleep_for(kReadinessPollInterval);
    }
}

void Initialize() {
    FILE* log = std::fopen(GetLogPath(), "a");
    if (log == nullptr) {
        ReportLauncherStatus("abort: could not open the mod log");
        return;
    }
    gLogFile = log;

    Log(log, "INFO", "BG3CameraUnlockMod dylib loaded");
    LogModuleIdentity(log);
    std::fprintf(
        log,
        "[INFO] build=%s flavor=%s (%s)\n",
        BG3_CAMERA_VERSION,
        BG3_CAMERA_FLAVOR,
        BG3_CAMERA_WITH_WASD ? "keyboard character movement enabled"
                             : "camera only, keyboard movement not touched");
    std::fflush(log);
    LoadCameraConfig(std::getenv(kConfigPathEnvironment), log);

    const auto text = FindMainExecutableText();
    if (!text.has_value()) {
        Log(log, "ABORT", "Could not locate main executable __TEXT,__text");
        ReportLauncherStatus("abort: could not locate BG3 executable code");
        std::fclose(log);
        gLogFile = nullptr;
        return;
    }

    std::fprintf(
        log,
        "[INFO] __text loaded=%p size=0x%zx slide=0x%llx\n",
        static_cast<const void*>(text->begin),
        text->size,
        static_cast<unsigned long long>(text->slide));

    const std::span<const std::uint8_t> executableText{
        text->begin,
        text->size,
    };

    bool allPatternsUnique = true;
    std::uint8_t* getPitchAddress = nullptr;
    std::uint8_t* onStickChangedAddress = nullptr;
    std::uint8_t* inputEventAddress = nullptr;
    std::uint8_t* fireInputEventsAddress = nullptr;
    std::uint8_t* cameraInputEventAddress = nullptr;
    std::uint8_t* updateGameCameraBehaviorAddress = nullptr;
    std::uint8_t* afterZoomSiteAddress = nullptr;
    std::uint8_t* preYawSiteAddress = nullptr;
    std::uint8_t* preGateSiteAddress = nullptr;
    std::uint8_t* pitchDefinitionSiteAddress = nullptr;
    std::uint8_t* collideWithObstaclesAddress = nullptr;
    std::uint8_t* restrictCamTargetDestHeightAddress = nullptr;
    std::uint8_t* keyboardMovementGuardAddress = nullptr;

    for (const NamedPattern& pattern : kRequiredPatterns) {
        const auto matches =
            FindAll(executableText, pattern.bytes, pattern.mask);

        std::fprintf(
            log,
            "[INFO] pattern=%s matches=%zu\n",
            pattern.name,
            matches.size());

        if (matches.size() == 1) {
            auto* runtimeMatch =
                const_cast<std::uint8_t*>(matches.front());
            const std::uintptr_t runtimeAddress =
                reinterpret_cast<std::uintptr_t>(runtimeMatch);
            const std::uintptr_t preferredAddress =
                runtimeAddress - static_cast<std::uintptr_t>(text->slide);

            std::fprintf(
                log,
                "[INFO] pattern=%s runtime=%p preferred=0x%llx\n",
                pattern.name,
                static_cast<void*>(runtimeMatch),
                static_cast<unsigned long long>(preferredAddress));

            if (std::strcmp(pattern.name, kGetPitchPattern.name) == 0) {
                getPitchAddress = runtimeMatch;
            } else if (
                std::strcmp(pattern.name, kOnStickChangedPattern.name) == 0) {
                onStickChangedAddress = runtimeMatch;
            } else if (
                std::strcmp(pattern.name, kInputEventPattern.name) == 0) {
                inputEventAddress = runtimeMatch;
            } else if (
                std::strcmp(
                    pattern.name,
                    kFireInputEventsPattern.name) == 0) {
                fireInputEventsAddress = runtimeMatch;
            } else if (
                std::strcmp(
                    pattern.name,
                    kCameraInputEventPattern.name) == 0) {
                cameraInputEventAddress = runtimeMatch;
            } else if (
                std::strcmp(
                    pattern.name,
                    kUpdateGameCameraBehaviorPattern.name) == 0) {
                updateGameCameraBehaviorAddress = runtimeMatch;
            } else if (
                std::strcmp(
                    pattern.name,
                    kAfterZoomSitePattern.name) == 0) {
                afterZoomSiteAddress = runtimeMatch;
            } else if (
                std::strcmp(
                    pattern.name,
                    kPreGateSitePattern.name) == 0) {
                preGateSiteAddress = runtimeMatch;
            } else if (
                std::strcmp(
                    pattern.name,
                    kPreYawSitePattern.name) == 0) {
                preYawSiteAddress = runtimeMatch;
            } else if (
                std::strcmp(
                    pattern.name,
                    kPitchDefinitionSitePattern.name) == 0) {
                pitchDefinitionSiteAddress = runtimeMatch;
            } else if (
                std::strcmp(
                    pattern.name,
                    kCollideWithObstaclesPattern.name) == 0) {
                collideWithObstaclesAddress = runtimeMatch;
            } else if (
                std::strcmp(
                    pattern.name,
                    kRestrictCamTargetDestHeightPattern.name) == 0) {
                restrictCamTargetDestHeightAddress = runtimeMatch;
            } else if (
                std::strcmp(
                    pattern.name,
                    kKeyboardMovementGuardPattern.name) == 0) {
                keyboardMovementGuardAddress = runtimeMatch;
            }
        } else {
            allPatternsUnique = false;
            std::fprintf(
                log,
                "[ABORT] pattern=%s expected exactly one match\n",
                pattern.name);
        }
    }

    if (!allPatternsUnique ||
        getPitchAddress == nullptr ||
        onStickChangedAddress == nullptr ||
        inputEventAddress == nullptr ||
        fireInputEventsAddress == nullptr ||
        cameraInputEventAddress == nullptr ||
        updateGameCameraBehaviorAddress == nullptr ||
        afterZoomSiteAddress == nullptr ||
        preYawSiteAddress == nullptr ||
        preGateSiteAddress == nullptr ||
        pitchDefinitionSiteAddress == nullptr ||
        collideWithObstaclesAddress == nullptr ||
        restrictCamTargetDestHeightAddress == nullptr ||
#if BG3_CAMERA_WITH_WASD
        keyboardMovementGuardAddress == nullptr ||
#endif
        false) {
        Log(log, "ABORT", "Pattern verification failed; no memory was modified");
        ReportLauncherStatus(
            "abort: this BG3 build does not match every required pattern");
        std::fclose(log);
        gLogFile = nullptr;
        return;
    }

    Log(
        log,
        "SAFE",
        "All patterns unique; installing dormant transactional hooks");
    gInstalled = InstallCameraHooks(
        gHooks,
        getPitchAddress,
        onStickChangedAddress,
        inputEventAddress,
        fireInputEventsAddress,
        cameraInputEventAddress,
        updateGameCameraBehaviorAddress,
        afterZoomSiteAddress,
        preYawSiteAddress,
        preGateSiteAddress,
        pitchDefinitionSiteAddress,
        collideWithObstaclesAddress,
        restrictCamTargetDestHeightAddress,
        keyboardMovementGuardAddress,
        log);

    if (!gInstalled) {
        Log(log, "ABORT", "Hook installation failed; mod remains inactive");
        ReportLauncherStatus("abort: transactional hook installation failed");
        std::fclose(log);
        gLogFile = nullptr;
        return;
    }

    gPitchDefinitionSiteAddress = pitchDefinitionSiteAddress;
    gStopRequested.store(false, std::memory_order_release);
    try {
        gReadinessThread = std::thread(&WaitForCameraSubsystemAndActivate);
    } catch (...) {
        Log(log, "ABORT", "Could not create deferred activation thread");
        ReportLauncherStatus("abort: could not create activation worker");
    }
}

void Shutdown() {
    gStopRequested.store(true, std::memory_order_release);
    if (gReadinessThread.joinable()) {
        gReadinessThread.join();
    }

    if (gInstalled) {
        RemoveCameraHooks(gHooks, gLogFile);
        gInstalled = false;

        if (gLogFile != nullptr) {
            Log(gLogFile, "INFO", "Hooks removed during dylib unload");
        }
    }

    if (gLogFile != nullptr) {
        std::fclose(gLogFile);
        gLogFile = nullptr;
    }
    gPitchDefinitionSiteAddress = nullptr;
}

}  // namespace
}  // namespace bg3cam

__attribute__((constructor))
static void BG3CameraUnlockEntryPoint() {
    bg3cam::Initialize();
}

__attribute__((destructor))
static void BG3CameraUnlockExitPoint() {
    bg3cam::Shutdown();
}
