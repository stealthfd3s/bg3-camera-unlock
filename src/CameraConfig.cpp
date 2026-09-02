#include "CameraConfig.hpp"

// Build flavor (1 = Camera + WASD, 0 = Camera Only). Set by CMake; default to
// the historically shipped flavor if a build forgets to define it.
#ifndef BG3_CAMERA_WITH_WASD
#define BG3_CAMERA_WITH_WASD 1
#endif

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <string_view>

namespace bg3cam {
namespace {

CameraConfig gConfig;

std::string_view Trim(std::string_view value) {
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1);
    }
    while (!value.empty() &&
           std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1);
    }
    return value;
}

bool EqualsIgnoringCase(
    const std::string_view left,
    const std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }

    for (std::size_t index = 0; index < left.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(left[index])) !=
            std::tolower(static_cast<unsigned char>(right[index]))) {
            return false;
        }
    }
    return true;
}

bool ParseFloat(const std::string_view text, float* result) {
    const std::string owned(Trim(text));
    if (owned.empty()) {
        return false;
    }

    errno = 0;
    char* end = nullptr;
    const float parsed = std::strtof(owned.c_str(), &end);
    if (errno == ERANGE || end == owned.c_str() ||
        end == nullptr || *end != '\0' || !std::isfinite(parsed)) {
        return false;
    }

    *result = parsed;
    return true;
}

bool ParseInt(const std::string_view text, int* result) {
    const std::string owned(Trim(text));
    if (owned.empty()) {
        return false;
    }

    errno = 0;
    char* end = nullptr;
    const long parsed = std::strtol(owned.c_str(), &end, 10);
    if (errno == ERANGE || end == owned.c_str() || end == nullptr ||
        *end != '\0' || parsed < 0 || parsed > 1000000) {
        return false;
    }

    *result = static_cast<int>(parsed);
    return true;
}

// Like ParseInt, but admits -1. Device ids start at 0, so "off" cannot be
// expressed as 0 without making the first device unselectable.
bool ParseDeviceId(const std::string_view text, int* result) {
    const std::string owned(Trim(text));
    if (owned.empty()) {
        return false;
    }

    errno = 0;
    char* end = nullptr;
    const long parsed = std::strtol(owned.c_str(), &end, 10);
    if (errno == ERANGE || end == owned.c_str() || end == nullptr ||
        *end != '\0' || parsed < -1 || parsed > 65535) {
        return false;
    }

    *result = static_cast<int>(parsed);
    return true;
}

bool ParseBool(const std::string_view text, bool* result) {
    const std::string_view value = Trim(text);
    if (EqualsIgnoringCase(value, "true") ||
        EqualsIgnoringCase(value, "yes") || value == "1" ||
        EqualsIgnoringCase(value, "on")) {
        *result = true;
        return true;
    }
    if (EqualsIgnoringCase(value, "false") ||
        EqualsIgnoringCase(value, "no") || value == "0" ||
        EqualsIgnoringCase(value, "off")) {
        *result = false;
        return true;
    }
    return false;
}

void LogInvalid(
    FILE* log,
    const std::size_t line,
    const std::string_view key,
    const char* expected) {
    std::fprintf(
        log,
        "[CONFIG] line=%zu key=%.*s ignored; expected %s\n",
        line,
        static_cast<int>(key.size()),
        key.data(),
        expected);
}

bool AssignFloat(
    const std::string_view value,
    float* destination,
    const float minimum,
    const float maximum) {
    float parsed{};
    if (!ParseFloat(value, &parsed) || parsed < minimum || parsed > maximum) {
        return false;
    }
    *destination = parsed;
    return true;
}

}  // namespace

const CameraConfig& GetCameraConfig() {
    return gConfig;
}

void LoadCameraConfig(const char* path, FILE* log) {
    gConfig = CameraConfig{};
#if !BG3_CAMERA_WITH_WASD
    gConfig.keyboardMovement = false;
#endif
    if (log == nullptr) {
        return;
    }

    if (path == nullptr || path[0] == '\0') {
        std::fprintf(
            log,
            "[CONFIG] no config path supplied; using safe defaults\n");
        std::fflush(log);
        return;
    }

    std::ifstream input(path);
    if (!input.is_open()) {
        std::fprintf(
            log,
            "[CONFIG] could not open %s; using safe defaults\n",
            path);
        std::fflush(log);
        return;
    }

    CameraConfig candidate;
    // Which canonical per-device sensitivity keys the file set explicitly. A
    // canonical key always wins over its legacy alias, whatever the line
    // order, so the alias is only folded in afterwards for the ones not seen.
    bool sawGamepadHorizontal = false;
    bool sawGamepadVertical = false;
    bool sawTrackpadHorizontal = false;
    bool sawTrackpadVertical = false;
    std::string lineText;
    std::size_t lineNumber = 0;
    while (std::getline(input, lineText)) {
        ++lineNumber;
        std::string_view line = Trim(lineText);
        if (line.empty() || line.front() == '#' || line.front() == ';' ||
            (line.front() == '[' && line.back() == ']')) {
            continue;
        }

        const std::size_t separator = line.find('=');
        if (separator == std::string_view::npos) {
            std::fprintf(
                log,
                "[CONFIG] line=%zu ignored; expected Key=Value\n",
                lineNumber);
            continue;
        }

        const std::string_view key = Trim(line.substr(0, separator));
        std::string_view value = Trim(line.substr(separator + 1));
        const std::size_t comment = value.find_first_of("#;");
        if (comment != std::string_view::npos) {
            value = Trim(value.substr(0, comment));
        }

        bool valid = true;
        const char* expected = nullptr;
        float parsedZoom = 0.0f;
        if (EqualsIgnoringCase(key, "ConfigVersion")) {
            expected = "a whole number, normally 1";
            valid = ParseInt(value, &candidate.configVersion);
        } else if (EqualsIgnoringCase(key, "PitchMinimumDegrees")) {
            expected = "a number from -89 to 88";
            valid = AssignFloat(value, &candidate.pitchMinimumDegrees, -89.0f, 88.0f);
        } else if (EqualsIgnoringCase(key, "PitchMaximumDegrees")) {
            expected = "a number from -88 to 89";
            valid = AssignFloat(value, &candidate.pitchMaximumDegrees, -88.0f, 89.0f);
        } else if (EqualsIgnoringCase(key, "HorizontalSensitivity")) {
            expected = "a number from 0.10 to 4.00";
            valid = AssignFloat(value, &candidate.horizontalSensitivity, 0.10f, 4.00f);
        } else if (EqualsIgnoringCase(key, "PointerRotateSensitivity")) {
            expected = "a number from 0.10 to 8.00";
            valid = AssignFloat(
                value, &candidate.pointerRotateSensitivity, 0.10f, 8.00f);
        } else if (EqualsIgnoringCase(key, "GamepadHorizontalSensitivity")) {
            expected = "a number from 0.10 to 4.00";
            valid = AssignFloat(
                value, &candidate.gamepadHorizontalSensitivity, 0.10f, 4.00f);
            if (valid) {
                sawGamepadHorizontal = true;
            }
        } else if (EqualsIgnoringCase(key, "GamepadVerticalSensitivity")) {
            expected = "a number from 0.10 to 3.00";
            valid = AssignFloat(
                value, &candidate.gamepadVerticalSensitivity, 0.10f, 3.00f);
            if (valid) {
                sawGamepadVertical = true;
            }
        } else if (EqualsIgnoringCase(key, "TrackpadHorizontalSensitivity")) {
            expected = "a number from 0.10 to 8.00";
            valid = AssignFloat(
                value, &candidate.trackpadHorizontalSensitivity, 0.10f, 8.00f);
            if (valid) {
                sawTrackpadHorizontal = true;
            }
        } else if (EqualsIgnoringCase(key, "TrackpadVerticalSensitivity")) {
            expected = "a number from 0.10 to 3.00";
            valid = AssignFloat(
                value, &candidate.trackpadVerticalSensitivity, 0.10f, 3.00f);
            if (valid) {
                sawTrackpadVertical = true;
            }
        } else if (EqualsIgnoringCase(key, "MouseHorizontalSensitivity")) {
            expected = "a number from 0.10 to 8.00";
            valid = AssignFloat(
                value, &candidate.mouseHorizontalSensitivity, 0.10f, 8.00f);
        } else if (EqualsIgnoringCase(key, "MouseVerticalSensitivity")) {
            expected = "a number from 0.10 to 3.00";
            valid = AssignFloat(
                value, &candidate.mouseVerticalSensitivity, 0.10f, 3.00f);
        } else if (EqualsIgnoringCase(key, "PointerRotationNative")) {
            expected = "true or false";
            valid = ParseBool(value, &candidate.pointerRotationNative);
        } else if (EqualsIgnoringCase(key, "PointerRotationHoldsHeading")) {
            expected = "true or false";
            valid = ParseBool(value, &candidate.pointerRotationHoldsHeading);
        } else if (
            EqualsIgnoringCase(key, "PointerRotateSmoothingMs")) {
            expected = "a number from 0 to 250";
            valid = AssignFloat(
                value,
                &candidate.pointerRotateSmoothingMilliseconds,
                0.0f,
                250.0f);
        } else if (
            EqualsIgnoringCase(key, "PointerRotationFollowHoldSeconds")) {
            expected = "a number from 0 to 3600";
            valid = AssignFloat(
                value,
                &candidate.pointerRotationFollowHoldSeconds,
                0.0f,
                3600.0f);
        } else if (EqualsIgnoringCase(key, "VerticalSensitivity")) {
            expected = "a number from 0.10 to 3.00";
            valid = AssignFloat(value, &candidate.verticalSensitivity, 0.10f, 3.00f);
        } else if (EqualsIgnoringCase(key, "ZoomSensitivity")) {
            expected = "a number from 0.05 to 3.00";
            valid = AssignFloat(value, &candidate.zoomSensitivity, 0.05f, 3.00f);
        } else if (EqualsIgnoringCase(key, "ZoomMinimumResponse")) {
            expected = "a number from 0.00 to 0.50";
            valid = AssignFloat(value, &candidate.zoomMinimumResponse, 0.00f, 0.50f);
        } else if (EqualsIgnoringCase(key, "PointerZoomModifierEvent")) {
            expected = "0 to disable, or an input event id from the log";
            valid = ParseInt(value, &candidate.pointerZoomModifierEvent) &&
                candidate.pointerZoomModifierEvent <= 65535;
        } else if (EqualsIgnoringCase(key, "PointerZoomModifierToggle")) {
            expected = "true or false";
            valid = ParseBool(value, &candidate.pointerZoomModifierToggle);
        } else if (EqualsIgnoringCase(key, "PointerZoomHoldButton")) {
            expected = "0 off, 1 left, 2 right, or 3 middle";
            valid = ParseInt(value, &candidate.pointerZoomHoldButton) &&
                candidate.pointerZoomHoldButton <= 3;
        } else if (EqualsIgnoringCase(key, "ZoomDeviceId")) {
            expected = "-1 to disable, or a device id from the log";
            valid = ParseDeviceId(value, &candidate.zoomDeviceId);
        } else if (EqualsIgnoringCase(key, "ZoomDeviceSensitivity")) {
            expected = "a number from 0.01 to 1.00";
            valid = AssignFloat(
                value, &candidate.zoomDeviceSensitivity, 0.01f, 1.00f);
        } else if (EqualsIgnoringCase(key, "ZoomForceActive")) {
            expected = "true or false";
            valid = ParseBool(value, &candidate.zoomForceActive);
        } else if (EqualsIgnoringCase(key, "ZoomResponseCurve")) {
            expected = "a number from 1.00 to 4.00";
            valid = AssignFloat(value, &candidate.zoomResponseCurve, 1.00f, 4.00f);
        } else if (EqualsIgnoringCase(key, "StickDeadzone")) {
            expected = "a number from 0.00 to 0.80";
            valid = AssignFloat(value, &candidate.stickDeadzone, 0.00f, 0.80f);
        } else if (EqualsIgnoringCase(key, "MinimumZoomDistance")) {
            expected = "0 to keep the game's own limit, or 0.05 to 50.00";
            valid = ParseFloat(value, &parsedZoom) &&
                (parsedZoom == 0.0f ||
                 (parsedZoom >= 0.05f && parsedZoom <= 50.0f));
            if (valid) {
                candidate.minimumZoomDistance = parsedZoom;
            }
        } else if (EqualsIgnoringCase(key, "MaximumZoomDistance")) {
            expected = "0 to keep the game's own limit, or 1.00 to 500.00";
            valid = ParseFloat(value, &parsedZoom) &&
                (parsedZoom == 0.0f ||
                 (parsedZoom >= 1.0f && parsedZoom <= 500.0f));
            if (valid) {
                candidate.maximumZoomDistance = parsedZoom;
            }
        } else if (EqualsIgnoringCase(key, "KeyboardMovement")) {
            expected = "true or false";
            valid = ParseBool(value, &candidate.keyboardMovement);
#if !BG3_CAMERA_WITH_WASD
            // Camera Only flavor: the key is still accepted so a config shared
            // between flavors does not error, but it has no effect and cannot
            // switch this build into keyboard-movement mode.
            if (valid && candidate.keyboardMovement) {
                std::fprintf(
                    log,
                    "[CONFIG] line=%zu KeyboardMovement ignored; this is the "
                    "Camera Only build\n",
                    lineNumber);
            }
            candidate.keyboardMovement = false;
#endif
        } else if (EqualsIgnoringCase(key, "VerboseLogging")) {
            expected = "true or false";
            valid = ParseBool(value, &candidate.verboseLogging);
        } else if (EqualsIgnoringCase(key, "ObstacleCollision")) {
            expected = "true or false";
            valid = ParseBool(value, &candidate.obstacleCollision);
        } else if (EqualsIgnoringCase(key, "FloorProtection")) {
            expected = "true or false";
            valid = ParseBool(value, &candidate.floorProtection);
        } else if (EqualsIgnoringCase(key, "CollisionSafetyMargin")) {
            expected = "a number from 0.00 to 2.00";
            valid = AssignFloat(value, &candidate.collisionSafetyMargin, 0.00f, 2.00f);
        } else if (EqualsIgnoringCase(key, "FloorSafetyOffset")) {
            expected = "a number from 0.00 to 3.00";
            valid = AssignFloat(value, &candidate.floorSafetyOffset, 0.00f, 3.00f);
        } else {
            std::fprintf(
                log,
                "[CONFIG] line=%zu unknown key=%.*s ignored\n",
                lineNumber,
                static_cast<int>(key.size()),
                key.data());
            continue;
        }

        if (!valid) {
            LogInvalid(log, lineNumber, key, expected);
        }
    }

    if (candidate.maximumZoomDistance != 0.0f &&
        candidate.minimumZoomDistance != 0.0f &&
        candidate.maximumZoomDistance <= candidate.minimumZoomDistance) {
        std::fprintf(
            log,
            "[CONFIG] zoom limits ignored because the maximum must exceed the "
            "minimum\n");
        const CameraConfig defaults;
        candidate.minimumZoomDistance = defaults.minimumZoomDistance;
        candidate.maximumZoomDistance = defaults.maximumZoomDistance;
    }

    // A file written by a newer build may use keys this build has never
    // heard of. Those were already skipped and logged individually above, so
    // the loaded settings are still coherent; say so plainly rather than
    // discarding a config the user deliberately wrote.
    if (candidate.configVersion > kCurrentConfigVersion) {
        std::fprintf(
            log,
            "[CONFIG] file declares version %d but this build understands %d; "
            "any key this build does not recognise was ignored\n",
            candidate.configVersion,
            kCurrentConfigVersion);
    }

    if (candidate.pitchMinimumDegrees >= candidate.pitchMaximumDegrees) {
        std::fprintf(
            log,
            "[CONFIG] pitch limits ignored because minimum must be below maximum\n");
        const CameraConfig defaults;
        candidate.pitchMinimumDegrees = defaults.pitchMinimumDegrees;
        candidate.pitchMaximumDegrees = defaults.pitchMaximumDegrees;
    }

    // Fold the legacy single-axis keys into the per-device fields for every
    // canonical key the file did not set. The legacy fields carry either the
    // user's old value or their own default, and those defaults already map to
    // the canonical defaults, so this is a no-op when nothing legacy was set.
    if (!sawGamepadHorizontal) {
        candidate.gamepadHorizontalSensitivity = candidate.horizontalSensitivity;
    }
    if (!sawGamepadVertical) {
        candidate.gamepadVerticalSensitivity = candidate.verticalSensitivity;
    }
    if (!sawTrackpadHorizontal) {
        candidate.trackpadHorizontalSensitivity =
            candidate.pointerRotateSensitivity / kTrackpadYawBaseCalibration;
    }
    if (!sawTrackpadVertical) {
        candidate.trackpadVerticalSensitivity = candidate.verticalSensitivity;
    }

#if !BG3_CAMERA_WITH_WASD
    // Camera Only flavor: keyboard character movement does not exist in this
    // build regardless of the file (including a file with no KeyboardMovement
    // line, whose struct default is true).
    candidate.keyboardMovement = false;
#endif

    gConfig = candidate;
    std::fprintf(
        log,
        "[CONFIG] loaded=%s version=%d pitch=[%.1f,%.1f] horizontal=%.2f "
        "pointerRotate=%.2f pointerSmoothMs=%.1f pointerNative=%d "
        "vertical=%.2f "
        "gamepadH=%.2f gamepadV=%.2f trackpadH=%.2f trackpadV=%.2f "
        "mouseH=%.2f mouseV=%.2f "
        "zoom=%.2f "
        "pointerZoomModifier=%d pointerZoomToggle=%d pointerHoldButton=%d "
        "zoomDevice=%d zoomDeviceSens=%.2f "
        "deadzone=%.2f zoomCurve=%.2f zoomFloor=%.2f zoomForceActive=%d zoomLimits=[%.2f,%.2f] verbose=%d "
        "obstacleCollision=%d floorProtection=%d "
        "collisionMargin=%.2f floorOffset=%.2f keyboardMovement=%d\n",
        path,
        gConfig.configVersion,
        gConfig.pitchMinimumDegrees,
        gConfig.pitchMaximumDegrees,
        gConfig.horizontalSensitivity,
        gConfig.pointerRotateSensitivity,
        gConfig.pointerRotateSmoothingMilliseconds,
        gConfig.pointerRotationNative ? 1 : 0,
        gConfig.verticalSensitivity,
        gConfig.gamepadHorizontalSensitivity,
        gConfig.gamepadVerticalSensitivity,
        gConfig.trackpadHorizontalSensitivity,
        gConfig.trackpadVerticalSensitivity,
        gConfig.mouseHorizontalSensitivity,
        gConfig.mouseVerticalSensitivity,
        gConfig.zoomSensitivity,
        gConfig.pointerZoomModifierEvent,
        gConfig.pointerZoomModifierToggle ? 1 : 0,
        gConfig.pointerZoomHoldButton,
        gConfig.zoomDeviceId,
        gConfig.zoomDeviceSensitivity,
        gConfig.stickDeadzone,
        gConfig.zoomResponseCurve,
        gConfig.zoomMinimumResponse,
        gConfig.zoomForceActive ? 1 : 0,
        gConfig.minimumZoomDistance,
        gConfig.maximumZoomDistance,
        gConfig.verboseLogging ? 1 : 0,
        gConfig.obstacleCollision ? 1 : 0,
        gConfig.floorProtection ? 1 : 0,
        gConfig.collisionSafetyMargin,
        gConfig.floorSafetyOffset,
        gConfig.keyboardMovement ? 1 : 0);
    std::fflush(log);
}

}  // namespace bg3cam
