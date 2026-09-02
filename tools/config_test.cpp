// Config parser tests. Runs without the game.
//
//   ctest -R config_test --output-on-failure
//
// Built once per flavor (BG3_CAMERA_WITH_WASD) so the KeyboardMovement /
// Camera Only exception is exercised on the build that enforces it.

#include "CameraConfig.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <unistd.h>

#ifndef BG3_CAMERA_WITH_WASD
#define BG3_CAMERA_WITH_WASD 1
#endif

namespace {

int gChecks = 0;
int gFailures = 0;

void Check(bool condition, const char* what) {
    ++gChecks;
    if (!condition) {
        ++gFailures;
        std::fprintf(stderr, "FAIL: %s\n", what);
    }
}

#define CHECK(cond) Check((cond), #cond)

bool NearlyEqual(const float left, const float right) {
    return std::abs(left - right) < 0.0001f;
}

std::string ScratchPath() {
    return "/tmp/bg3-camera-unlock-config-test-" +
        std::to_string(static_cast<long long>(getpid())) + ".ini";
}

bool WriteText(const std::string& path, const std::string& text) {
    FILE* file = std::fopen(path.c_str(), "w");
    if (file == nullptr) {
        return false;
    }
    std::fputs(text.c_str(), file);
    return std::fclose(file) == 0;
}

// Load `body` and return the resulting config.
bg3cam::CameraConfig Load(FILE* log, const std::string& body) {
    const std::string path = ScratchPath();
    WriteText(path, body);
    bg3cam::LoadCameraConfig(path.c_str(), log);
    std::remove(path.c_str());
    return bg3cam::GetCameraConfig();
}

}  // namespace

int main() {
    FILE* log = std::tmpfile();
    if (log == nullptr) {
        return 1;
    }

    // ---- Defaults ----------------------------------------------------------
    bg3cam::LoadCameraConfig(nullptr, log);
    const bg3cam::CameraConfig d = bg3cam::GetCameraConfig();
    CHECK(NearlyEqual(d.pitchMinimumDegrees, -45.0f));
    CHECK(NearlyEqual(d.pitchMaximumDegrees, 85.0f));
    CHECK(d.configVersion == bg3cam::kCurrentConfigVersion);
    CHECK(d.obstacleCollision && d.floorProtection);
    CHECK(NearlyEqual(d.gamepadHorizontalSensitivity, 2.0f));
    CHECK(NearlyEqual(d.gamepadVerticalSensitivity, 1.0f));
    CHECK(NearlyEqual(d.trackpadHorizontalSensitivity, 1.0f));
    CHECK(NearlyEqual(d.trackpadVerticalSensitivity, 1.0f));
    CHECK(NearlyEqual(d.mouseHorizontalSensitivity, 1.0f));
    CHECK(NearlyEqual(d.mouseVerticalSensitivity, 1.0f));

    // ---- The six per-device sensitivities are independent -----------------
    {
        const bg3cam::CameraConfig c = Load(log,
            "GamepadHorizontalSensitivity=3.1\n"
            "GamepadVerticalSensitivity=2.2\n"
            "TrackpadHorizontalSensitivity=4.3\n"
            "TrackpadVerticalSensitivity=2.4\n"
            "MouseHorizontalSensitivity=5.5\n"
            "MouseVerticalSensitivity=2.6\n");
        CHECK(NearlyEqual(c.gamepadHorizontalSensitivity, 3.1f));
        CHECK(NearlyEqual(c.gamepadVerticalSensitivity, 2.2f));
        CHECK(NearlyEqual(c.trackpadHorizontalSensitivity, 4.3f));
        CHECK(NearlyEqual(c.trackpadVerticalSensitivity, 2.4f));
        CHECK(NearlyEqual(c.mouseHorizontalSensitivity, 5.5f));
        CHECK(NearlyEqual(c.mouseVerticalSensitivity, 2.6f));

        // Setting one leaves the other five at their defaults.
        const bg3cam::CameraConfig one = Load(log,
            "MouseHorizontalSensitivity=6.0\n");
        CHECK(NearlyEqual(one.mouseHorizontalSensitivity, 6.0f));
        CHECK(NearlyEqual(one.gamepadHorizontalSensitivity, 2.0f));
        CHECK(NearlyEqual(one.gamepadVerticalSensitivity, 1.0f));
        CHECK(NearlyEqual(one.trackpadHorizontalSensitivity, 1.0f));
        CHECK(NearlyEqual(one.trackpadVerticalSensitivity, 1.0f));
        CHECK(NearlyEqual(one.mouseVerticalSensitivity, 1.0f));
    }

    // ---- Legacy aliases --------------------------------------------------
    {
        // HorizontalSensitivity -> gamepad H; VerticalSensitivity -> gamepad V
        // and trackpad V; PointerRotateSensitivity -> trackpad H, normalised
        // by the 3.0 base (so 3.0 -> 1.0, 6.0 -> 2.0).
        const bg3cam::CameraConfig c = Load(log,
            "HorizontalSensitivity=2.5\n"
            "VerticalSensitivity=1.7\n"
            "PointerRotateSensitivity=6.0\n");
        CHECK(NearlyEqual(c.gamepadHorizontalSensitivity, 2.5f));
        CHECK(NearlyEqual(c.gamepadVerticalSensitivity, 1.7f));
        CHECK(NearlyEqual(c.trackpadVerticalSensitivity, 1.7f));
        CHECK(NearlyEqual(c.trackpadHorizontalSensitivity, 2.0f));
    }

    // ---- Canonical wins over alias, in BOTH line orders -----------------
    {
        const bg3cam::CameraConfig canonicalFirst = Load(log,
            "GamepadHorizontalSensitivity=3.0\n"
            "HorizontalSensitivity=1.0\n"
            "TrackpadHorizontalSensitivity=2.0\n"
            "PointerRotateSensitivity=3.0\n"
            "TrackpadVerticalSensitivity=2.5\n"
            "VerticalSensitivity=1.0\n");
        CHECK(NearlyEqual(canonicalFirst.gamepadHorizontalSensitivity, 3.0f));
        CHECK(NearlyEqual(canonicalFirst.trackpadHorizontalSensitivity, 2.0f));
        CHECK(NearlyEqual(canonicalFirst.trackpadVerticalSensitivity, 2.5f));

        const bg3cam::CameraConfig aliasFirst = Load(log,
            "HorizontalSensitivity=1.0\n"
            "GamepadHorizontalSensitivity=3.0\n"
            "PointerRotateSensitivity=3.0\n"
            "TrackpadHorizontalSensitivity=2.0\n"
            "VerticalSensitivity=1.0\n"
            "TrackpadVerticalSensitivity=2.5\n");
        CHECK(NearlyEqual(aliasFirst.gamepadHorizontalSensitivity, 3.0f));
        CHECK(NearlyEqual(aliasFirst.trackpadHorizontalSensitivity, 2.0f));
        CHECK(NearlyEqual(aliasFirst.trackpadVerticalSensitivity, 2.5f));
    }

    // ---- Missing / malformed / non-finite / out of range ----------------
    {
        const bg3cam::CameraConfig c = Load(log,
            "GamepadHorizontalSensitivity=\n"          // empty
            "GamepadVerticalSensitivity=nan\n"          // non-finite
            "TrackpadHorizontalSensitivity=inf\n"       // non-finite
            "TrackpadVerticalSensitivity=1e40\n"        // overflow
            "MouseHorizontalSensitivity=99\n"           // over range
            "MouseVerticalSensitivity=-3\n"             // under range
            "PitchMinimumDegrees=not-a-number\n"
            "StickDeadzone=0.15\n");                    // one good line
        CHECK(NearlyEqual(c.gamepadHorizontalSensitivity, 2.0f));
        CHECK(NearlyEqual(c.gamepadVerticalSensitivity, 1.0f));
        CHECK(NearlyEqual(c.trackpadHorizontalSensitivity, 1.0f));
        CHECK(NearlyEqual(c.trackpadVerticalSensitivity, 1.0f));
        CHECK(NearlyEqual(c.mouseHorizontalSensitivity, 1.0f));
        CHECK(NearlyEqual(c.mouseVerticalSensitivity, 1.0f));
        CHECK(NearlyEqual(c.pitchMinimumDegrees, -45.0f));
        CHECK(NearlyEqual(c.stickDeadzone, 0.15f));     // good line still took
    }

    // ---- Cross-field: inverted pitch limits are rejected as a pair ------
    {
        const bg3cam::CameraConfig c = Load(log,
            "PitchMinimumDegrees=80\n"
            "PitchMaximumDegrees=20\n");
        CHECK(NearlyEqual(c.pitchMinimumDegrees, -45.0f));
        CHECK(NearlyEqual(c.pitchMaximumDegrees, 85.0f));
    }

    // ---- Forward-compatible: unknown keys skipped, known keys apply -----
    {
        const bg3cam::CameraConfig c = Load(log,
            "ConfigVersion=99\n"
            "SomeKeyFromTheFuture=1.0\n"
            "MouseVerticalSensitivity=2.0\n");
        CHECK(c.configVersion == 99);
        CHECK(NearlyEqual(c.mouseVerticalSensitivity, 2.0f));
        CHECK(NearlyEqual(c.pitchMinimumDegrees, -45.0f));
    }

    // ---- KeyboardMovement, per flavor ----------------------------------
    {
        const bg3cam::CameraConfig on = Load(log, "KeyboardMovement=true\n");
        const bg3cam::CameraConfig off = Load(log, "KeyboardMovement=false\n");
        const bg3cam::CameraConfig absent = Load(log, "StickDeadzone=0.15\n");
#if BG3_CAMERA_WITH_WASD
        CHECK(on.keyboardMovement == true);
        CHECK(off.keyboardMovement == false);
        CHECK(absent.keyboardMovement == true);   // struct default
#else
        // Camera Only: forced false no matter what the file says, including a
        // file with no KeyboardMovement line at all.
        CHECK(on.keyboardMovement == false);
        CHECK(off.keyboardMovement == false);
        CHECK(absent.keyboardMovement == false);
#endif
    }

    std::fclose(log);
    std::fprintf(stderr, "%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
