#pragma once

#include <cstdio>

namespace bg3cam {

// Schema version of the on-disk configuration file.
//
// Bump this only when a key is renamed or its meaning changes, never for a
// new key: unknown keys are already ignored, and a missing key already
// falls back to its default, so purely additive changes need no migration.
//
// A file with no ConfigVersion line is treated as version 1, which is what
// every config written before this field existed effectively is.
inline constexpr int kCurrentConfigVersion = 1;

// The proven trackpad-yaw calibration the drain applies to accumulated raw
// travel (2.6.6 proto2). TrackpadHorizontalSensitivity is a normalized
// multiplier over this base, so 1.0 reproduces proto2 exactly and the legacy
// PointerRotateSensitivity=3.0 maps to 1.0.
inline constexpr float kTrackpadYawBaseCalibration = 3.0f;

struct CameraConfig {
    int configVersion = kCurrentConfigVersion;
    float pitchMinimumDegrees = -45.0f;
    float pitchMaximumDegrees = 85.0f;
    // Legacy single-axis keys. Still parsed, still the on-disk fallback, but
    // the runtime now reads the per-device fields below. HorizontalSensitivity
    // aliases gamepad H, VerticalSensitivity aliases gamepad V (and, when no
    // canonical trackpad V is given, trackpad V too), PointerRotateSensitivity
    // aliases trackpad H (normalized by kTrackpadYawBaseCalibration).
    float horizontalSensitivity = 2.0f;
    // Horizontal rotation speed for pointer devices - trackpad and mouse
    // wheel - kept separate from the stick's.
    //
    // The two do not arrive in the same units. A stick reports a deflection:
    // it spans 0..1 and rests at zero. A trackpad reports how far the fingers
    // travelled since the last sample, which is a speed, and runtime captures
    // put a swipe anywhere from 0.1 to beyond 4. One multiplier across both
    // forces a compromise: tuned for the stick, a swipe crawls; tuned for the
    // swipe, the stick spins.
    //
    // Scales the normalized swipe speed before it reaches the game's own
    // rotation handler, so 1.0 asks for the game's full-scale rate at a brisk
    // swipe and the default matches horizontalSensitivity's.
    float pointerRotateSensitivity = 3.0f;
    float verticalSensitivity = 1.0f;
    // Canonical per-device sensitivities. Each defaults so that, with no keys
    // in the file at all, behaviour is unchanged: gamepad 2.0/1.0 (its proven
    // runtime baseline), trackpad 1.0/1.0 (proto2), mouse 1.0/1.0. A canonical
    // key always wins over its legacy alias regardless of line order.
    float gamepadHorizontalSensitivity = 2.0f;
    float gamepadVerticalSensitivity = 1.0f;
    float trackpadHorizontalSensitivity = 1.0f;
    float trackpadVerticalSensitivity = 1.0f;
    float mouseHorizontalSensitivity = 1.0f;
    float mouseVerticalSensitivity = 1.0f;
    // Whether pointer rotation holds the camera's heading for the duration of
    // a swipe.
    //
    // The camera update runs a follow servo over the heading that turns it
    // toward a target every frame, after the input path and before the
    // transform is built. A heading written from outside that pipeline is
    // therefore partly taken back in the same frame it is applied, which is
    // what made a trackpad swipe lag while the identical pitch gesture did
    // not - pitch is delivered through a getter this mod owns, so nothing
    // downstream can drag it.
    //
    // With this on, the servo's two rate fields are held at zero for the
    // duration of the update while a swipe is in progress, and the mod's
    // angle is written back afterwards so the next frame builds on it. The
    // suppression lasts only as long as the gesture: outside it the camera's
    // own follow behaviour is completely untouched, as is the controller,
    // which never went through this path.
    //
    // Turn it off to get the previous behaviour back.
    // Whether trackpad rotation uses the game's own pointer channel.
    //
    // BG3 carries two rotation inputs. The analogue stick writes
    // currentAngleDelta and is gated at 0.65; the pointer writes
    // mouseRotationDelta and is not gated at all, and a flag on the camera
    // tells the update to read it. The controller was always flawless because
    // it uses its channel as intended. This mod never used the pointer's: it
    // applied the stick gate's inverse to a pointer - adding 0.65 to every
    // swipe - and then, when that felt wrong, stopped using the pipeline and
    // wrote the camera's heading directly instead.
    //
    // Writing the heading is what put it at odds with the camera's follow
    // correction, which owns that field, and every symptom since - the lag,
    // the judder, the lurch and the drag - came from that fight rather than
    // from the input. On the pointer channel there is nothing to fight: the
    // game does the turning and knows the player is driving.
    //
    // Set false to fall back to the previous heading-writing behaviour, which
    // the settings below only apply to.
    bool pointerRotationNative = true;
    bool pointerRotationHoldsHeading = true;
    // How long unspent trackpad travel takes to become rotation, in
    // milliseconds. Native mode always keeps a 30 ms stability floor, so 0
    // selects that shortest response rather than passing quantized impulses
    // straight through.
    //
    // Accumulating travel fixed how much a gesture turns the camera. It did
    // nothing for how evenly, because a frame's step was still however many
    // samples happened to land inside it - and a ~116 Hz trackpad against a
    // ~120 fps frame beats between none, one and two of them. The total was
    // right and the motion was not.
    //
    // Draining the travel over a time constant separates the two: it arrives
    // when the fingers move and is spent as a continuous function of frame
    // time, so nothing depends on where a sample fell relative to a frame
    // boundary. All of it still leaves the buffer, so a gesture remains worth
    // the same rotation at any frame rate.
    //
    // Higher is smoother and softer; lower is sharper and closer to the
    // fingers.
    float pointerRotateSmoothingMilliseconds = 30.0f;
    // How long after the last swipe the camera's follow correction stays held
    // still, in seconds.
    //
    // The correction is a debt, not a per-frame nuisance. Its error is the
    // angle between where the camera looks and where the game wants it to
    // look, so every degree a swipe turns away from that adds to it, and the
    // rate it is collected at climbs with it - from nothing at no error up to
    // ninety degrees a second at a large one. Releasing it between swipes does
    // not avoid that, it defers it: the bill arrives at the next pause, as a
    // lurch followed by a drag, and it is larger the longer the swipe was.
    //
    // The default therefore holds it for as long as a session of swiping
    // lasts, so the camera stays where it was put. The cost is that the camera
    // no longer drifts to follow the character on its own while you are using
    // the trackpad to aim it. Lower this to get that back between swipes, or
    // set 0 to hold it only while a swipe is actually in progress.
    //
    // Rotating with a controller or a key clears it immediately, whatever this
    // is set to, so those paths keep the game's behaviour exactly.
    float pointerRotationFollowHoldSeconds = 3600.0f;
    float zoomSensitivity = 0.6f;
    float zoomResponseCurve = 2.0f;
    // The rate produced the moment the stick leaves the deadzone. Ramping
    // from zero instead leaves a band that is moving, but too slowly to see,
    // which reads as a deadzone no matter how small the real one is.
    float zoomMinimumResponse = 0.12f;
    // The event carries an "axis is active" byte beside its magnitude. The
    // game appears to consult that byte, not the magnitude, when deciding
    // whether the zoom action runs at all - which is why reshaping the
    // magnitude alone never moved the point where zoom starts responding.
    bool zoomForceActive = true;
    // Input event id that acts as the zoom modifier for pointer devices -
    // trackpad and mouse - the way L3 does for a controller. Held, the
    // camera's vertical axis zooms; released, it pitches.
    //
    // 0 disables it. There is no safe default: the id of a mouse button is
    // assigned by the game's own input map, is not documented anywhere, and
    // differs from the controller ids this mod already knows. Turn on
    // VerboseLogging and the mod prints each id it sees pressed, so the right
    // one can be read off the log rather than guessed.
    int pointerZoomModifierEvent = 0;
    // How that event is interpreted.
    //
    // false: the button is held. Requires the game to report both a press and
    // a release for it, because the release is what ends the zoom.
    //
    // true: each press flips between pitch and zoom. This is the mode to use
    // for a button the game reports only a press for - holding cannot work
    // there, since nothing would ever turn the zoom back off.
    //
    // Discovery logging prints the phase of every id it sees, so which mode a
    // button needs is read off the log rather than assumed.
    bool pointerZoomModifierToggle = false;
    // Physical mouse button held to zoom: 0 off, 1 left, 2 right, 3 middle.
    //
    // This reads the button's real state from the OS rather than waiting for
    // the game to report one. It exists because the game dispatches a mouse
    // button as a press and a release back to back, even while the button is
    // physically down - so an event-driven hold is active for microseconds
    // and no scroll ever lands inside it.
    //
    // The same problem was already solved this way for the controller: L3 is
    // read by polling MFIInputDevice's button field, not from events.
    int pointerZoomHoldButton = 0;
    // Input device whose CameraZoomIn/CameraZoomOut events mean zoom rather
    // than pitch. -1 disables it.
    //
    // Not 0 for "off": 0 is a real device id on this build, and using it as
    // a sentinel would make the keyboard unselectable.
    //
    // The game has one action for both: the mod turns it into pitch, which is
    // what makes two-finger scroll and the right stick pitch the camera. Bind
    // that same action to a key as well, name the keyboard here, and the key
    // zooms while the trackpad keeps pitching - no modifier to hold.
    //
    // Device ids come from the game and are logged with VerboseLogging on.
    int zoomDeviceId = -1;
    // Zoom step produced by one press from the zoom device. A key is digital:
    // it is either down or not, so there is no travel to modulate and the
    // step size is the only thing that decides how coarse zooming feels.
    // Lower than zoomSensitivity because a stick can be eased and a key
    // cannot.
    float zoomDeviceSensitivity = 0.25f;
    float stickDeadzone = 0.15f;
    // 0 keeps whatever bound the game itself authored for the camera arm.
    float minimumZoomDistance = 0.5f;
    float maximumZoomDistance = 0.0f;
    // Lets the game's existing character-movement task consume the bound WASD
    // actions in keyboard UI mode. The controller-mode path already passes the
    // same guard and is unchanged.
    //
    // Flavor exception: in the Camera Only build (BG3_CAMERA_WITH_WASD == 0)
    // LoadCameraConfig forces this to false whatever the file says, and the
    // keyboard-mode guard is never patched. The key is still parsed so a
    // config shared between the two flavors does not error.
    bool keyboardMovement = true;
    bool verboseLogging = false;
    bool obstacleCollision = true;
    bool floorProtection = true;
    float collisionSafetyMargin = 0.20f;
    float floorSafetyOffset = 1.00f;
};

const CameraConfig& GetCameraConfig();

// Loads a deliberately small key=value format. Invalid values never become
// active: the parser logs the problem and retains a safe default instead.
void LoadCameraConfig(const char* path, FILE* log);

}  // namespace bg3cam
