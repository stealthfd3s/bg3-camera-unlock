#include "CameraHooks.hpp"
#include "CameraConfig.hpp"
#include "RawTrackpadMonitor.hpp"

// Build flavor. Set by CMake (1 = Camera + WASD, 0 = Camera Only). Fall back
// to the historically shipped flavor if a build forgets to define it.
#ifndef BG3_CAMERA_WITH_WASD
#define BG3_CAMERA_WITH_WASD 1
#endif
#ifndef BG3_CAMERA_VERSION
#define BG3_CAMERA_VERSION "development"
#endif
#ifndef BG3_CAMERA_FLAVOR
#define BG3_CAMERA_FLAVOR (BG3_CAMERA_WITH_WASD ? "camera-wasd" : "camera-only")
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <CoreGraphics/CoreGraphics.h>
#include <mach/mach_time.h>
#include <mutex>
#include <string>

extern "C" {
void* BG3AfterZoomTrampoline = nullptr;
void BG3AfterZoomDetour();
// Deliberately a second, separately named pair. The two detours sit at
// different points of the same update - one before the rotation channel is
// read, one after the zoom fields are final - and sharing a trampoline symbol
// between them would silently send one site's displaced instructions to the
// other's return address.
void* BG3PreYawTrampoline = nullptr;
void BG3PreYawDetour();
// Third, again separately named. Three detours sit at three points of the
// same update; a shared trampoline symbol would send one site's displaced
// instructions to another site's return address.
void* BG3PreGateTrampoline = nullptr;
void BG3PreGateDetour();
}

namespace bg3cam {
namespace {

constexpr float kFallbackPitchSpeedDegreesPerSecond = 90.0f;
constexpr float kMaximumGameDeltaSeconds = 0.25f;
constexpr std::size_t kGameTimeDeltaOffset = 0x8;
// macOS emits 0x140 when the L3 chord starts. The registered low-level
// ToggleInputMode action is 0xc0 and is dispatched on release. Event 0xac is
// the later InputController event we use to track a mode change that was
// actually allowed through.
constexpr std::uint32_t kL3ChordStartEvent = 0x140;
constexpr std::uint32_t kToggleInputModeEvent = 0xc0;
constexpr std::uint32_t kInputModeChangedEvent = 0xac;
constexpr std::uint32_t kZoomInEvent = 104;
constexpr std::uint32_t kZoomOutEvent = 105;
constexpr std::uint32_t kRotateLeftEvent = 107;
constexpr std::uint32_t kRotateRightEvent = 108;
// BG3's native mouse yaw arms: 109 turns one way, 110 the other, both reading
// their delta from InputEvent+0x14 (not +0x18). Left as the game's own yaw
// source; only that one float is optionally scaled.
constexpr std::uint32_t kMouseNativeYawNegEvent = 109;
constexpr std::uint32_t kMouseNativeYawPosEvent = 110;
constexpr std::size_t kInputEventPressedOffset = 0x1c;
constexpr std::size_t kInputEventValueOffset = 0x18;
constexpr std::size_t kInputEventNativeYawOffset = 0x14;
constexpr std::size_t kInputEventSize = 0x20;
constexpr std::size_t kFireEventDescSize = 0x38;
constexpr std::size_t kFireEventPhaseOffset = 0x13;
constexpr std::size_t kFireEventDeviceOffset = 0x30;
constexpr std::uint8_t kInputPhaseRelease = 2;
constexpr std::uint16_t kGamepadDevice = 3;
constexpr std::size_t kMaximumFilteredFireEvents = 32;
constexpr std::size_t kL3PressedOffset = 0xbc;
// UpdateGameCameraBehavior's ARM64 yaw path loads the angular speed from
// GameCameraBehavior + 0xc0:
//
//   ldr   s1, [x26, #0xc0]
//   fmadd s8, s0, s1, s2
//
// +0xc4 is the changing horizontal heading, not a speed. Reading it made the
// pitch rate depend on the direction the camera happened to face.
constexpr std::size_t kCameraRotationSpeedOffset = 0xc0;
// Trackpad input is displacement, and the output angle is proportional to it.
//
// This is the one property everything before it lacked. A sample says how far
// the fingers moved, so the total rotation of a gesture is the total of those
// samples times a sensitivity - and nothing else. Not the frame rate, not
// where the samples happened to land relative to a frame boundary, not how
// many arrived in any particular frame.
//
// Every earlier version failed that test. They read an instantaneous sample
// as a speed, so the result depended on how often a frame happened to catch
// one - which is why the sensitivity changed with the resolution: a different
// frame rate catches a different fraction of the samples. It is also why the
// motion kept hesitating, since the fraction varies from frame to frame.
//
// Accumulating instead makes both go away by construction. No filter, no hold
// window, no idle timeout, no quantisation - each of those was compensating
// for a measurement this model does not take.
constexpr float kDegreesPerTravelUnit = 1.0f;
// The input device the trackpad reports as on this build.
//
// Named rather than derived from "not the gamepad", because that test also
// catches the keyboard: Q/E arrive as the same two actions at a magnitude of
// exactly 1.0, and routing them through a pointer feeder is what once made
// them three times too fast. Confirmed over several sessions and thousands of
// events - 107, 108, 104 and 105 all arrive from device 1 with the trackpad's
// 0.1 magnitude grid, while the keyboard is device 0 and the gamepad is 3.
constexpr std::uint16_t kPointerDevice = 1;
// Ceiling on the rate, not on the per-frame angle.
//
// A per-frame ceiling is meaningless when the update runs anywhere from 3 ms
// to 67 ms apart, which is what this build's own telemetry measured: the same
// thirty degrees is a gentle turn in one frame and a teleport in another. The
// old fixed 30 deg/frame allowed 2600 deg/s at a typical frame and therefore
// never bound anything - a runtime capture recorded a single frame turning
// 16.9 degrees with that limit in place.
//
// 540 deg/s is an isolated experiment, not a settled value. In the 2.6.4
// runtime capture a 270 deg/s ceiling bound the pointer-native yaw injector
// often enough to produce long consecutive cap-runs on fast swipes, and a
// follow-up non-verbose test still reported fast swipes as subjectively too
// slow. Doubling the ceiling is the single change under test here, meant to
// let the accumulator drain faster and carry less backlog; whether it removes
// the slow feel or reintroduces teleport-feel is not yet known. What the cap
// holds back is still not discarded: it stays in the accumulator and arrives
// over the following frames.
constexpr float kMaximumRotationDegreesPerSecond = 540.0f;
// Used only by the Camera + WASD keyboard-movement guard patch.
[[maybe_unused]] constexpr std::uint32_t kArm64Nop = 0xd503201f;
[[maybe_unused]] constexpr std::uint32_t kArm64CbzW8Mask = 0xff00001f;
[[maybe_unused]] constexpr std::uint32_t kArm64CbzW8 = 0x34000008;
// The heading the yaw path accumulates into, read out of the disassembly
// rather than inferred from being next to the speed:
//
//   ldr   s1, [x26, #0xc0]   // rotationSpeed
//   ldr   s2, [x26, #0xac]   // heading
//   fmadd s8, s0, s1, s2     // heading + input * speed * dt
//   str   s8, [x26, #0xac]   // written back
//
// It was previously taken to be +0xc4 on the reasoning that the heading must
// sit one field along from the speed. It does not: +0xc4 is only ever loaded,
// never stored, so writing there changed nothing a frame later read.
//
// Because the update reads this field and writes it back, a value written
// before the update is the base the game accumulates onto - which is what
// lets rotation be owned here the way pitch already is.
constexpr std::size_t kCameraHeadingOffset = 0xac;
// Note on +0x164, so it is not mistaken for the answer a second time.
//
// The yaw the picture is built from is not +0xac. +0xac is the desired yaw,
// and +0x164 is the one the look direction is actually made from:
//
//   str  s0, [x21, #0x1e0]    // actual pitch
//   ldr  s9, [x21, #0xac]     // desired yaw
//   ldr  s0, [x21, #0x164]    // actual yaw
//   fmul s0, s0, #0.017453
//   bl   sincos               // -> look direction
//
// +0x164 approaches +0xac at a rate from the CameraDefinition, which looks
// exactly like the missing piece and is not one: those rates are +0xf0 and
// +0xf4, and ScopedApproachRate already holds them at a within-frame value for
// the whole update, so the approach converges every frame and is not what
// stands between the input and the camera. Writing +0x164 directly was tried
// and made the camera far worse - unsurprisingly, since it sets an angle whose
// companion state, the direction at +0x70 and the arm built from it, is left
// describing somewhere else.
// The game's own channel for pointer-driven rotation, and the flag that opens
// it.
//
//   +0x9c  currentAngleDelta   - the stick channel, gated at 0.65
//   +0xa0  mouseRotationDelta  - the pointer channel, gated only at 1e-7
//
// kMouseRotation, bit 8 of the flags at +0xa8, selects which the update reads:
//
//   add   x8,  x26, #0x9c
//   ldrb  w9,  [x26, #0xa9]
//   add   x10, x26, #0xa0
//   csel  x8,  x8, x10, eq
//   ldr   s9,  [x8]
//   ...
//   fneg  s0, s9
//   ldr   s1, [x27, #0x8]     // frame delta
//   ldr   s1, [x26, #0xc0]    // rotationSpeed
//   fmadd s8, s0, s1, s2      // heading += -delta * dt * rotationSpeed
//
// The field is a rate, so a fixed angle is asked for by dividing it by both
// factors. Both the field and flag are restored to their exact prior values
// immediately after the wrapped update consumes them, so no trackpad state
// leaks into an idle frame or a different GameCameraBehavior object.
constexpr std::size_t kMouseRotationDeltaOffset = 0xa0;
constexpr std::size_t kMouseRotationFlagByteOffset = 0xa9;
constexpr std::uint8_t kMouseRotationFlagBit = 0x1;
// Deliberately not used: the pointer input channel.
//
// BG3 carries two rotation inputs on GameCameraBehavior - currentAngleDelta at
// +0x9c for the stick, gated at 0.65, and mouseRotationDelta at +0xa0 for a
// pointer, gated only at 1e-7 - and a flag, kMouseRotation, bit 8 of the flags
// at +0xa8, selects which the update reads. Feeding the pointer field looks
// like the natural answer and was tried; it is recorded here so it is not
// tried again.
//
// It does not help, because that channel only produces a *desired* yaw. The
// update turns it into heading += -delta * dt * rotationSpeed at +0xac, and
// +0xac is still separated from the camera by the approach into +0x164. The
// channel was never the problem. The gap between desired and actual was.
// The heading is not the mod's to write alone: the same update runs a follow
// servo over it, and that servo is why a swipe used to lag.
//
// Read off UpdateGameCameraBehavior. The branch is taken on the forced-camera
// flag the mod already knows, and both sides land on the same servo:
//
//   ldrb  w8, [x20, #0x1362]      // forced-definition flag
//   cbz   w8, servo_from_0x7c4_or_0x958
//   ldr   s10, [x20, #0xdb0]      // 0xc80 + 0x130, maximum rate
//   ldr   s11, [x20, #0xdac]      // 0xc80 + 0x12c, minimum rate
//   ...
//   ldr   s1, [x8, #0x128]        // curve exponent
//   fdiv  s0, s8, #180.0          // |error| normalised over half a turn
//   bl    powf
//   fmadd s0, s0, (s10 - s11), s11 // rate = lerp(min, max, curved error)
//   ldr   s1, [x27, #0x8]         // the same GameTime delta this mod reads
//   fmul  s2, s1, s0              // step = rate * dt
//   fcsel s0, s9, s0, gt          // never overshoot the error
//   ldr   s1, [x26, #0xac]
//   fadd  s8, s1, s0              // heading += step
//   str   s8, [x26, #0xac]
//
// It runs every frame, after the input path, and the camera transform is
// built from the result - the later loads of +0xac all sit past this store.
// So a heading written before the update is not the heading the frame draws:
// the servo takes part of it back on the way through, which is exactly what a
// swipe that lags feels like.
//
// Pitch never had this problem, and not because it is better behaved. It is
// delivered through GetCameraPitchDegrees, the one point the game reads pitch
// from, and this mod owns that function. Nothing downstream can drag a value
// the game has to ask for. Rotation has no such getter, so the equivalent has
// to be built: hold the servo still while the mod is driving, the way
// ScopedApproachRate already holds the pitch approach still.
constexpr std::size_t kYawFollowCurveOffset = 0x128;
constexpr std::array<std::size_t, 2> kYawFollowRateOffsets{
    0x12c,
    0x130,
};
// A rate is degrees per second. Zero is legitimate - it is what suppression
// writes - so only the upper end and the ordering are checked. A field that
// is not this pair would have to pass both across all three definitions.
constexpr float kMaximumPlausibleYawFollowRate = 4096.0f;
// The horizontal wheel arrives in quantized bursts. The latest runtime capture
// at roughly 100 fps contains per-frame travel from 0.1 to 1.7 followed by
// frames with none, which turned into an alternating 0..480 degrees/second
// request when PointerRotateSmoothingMs was zero. Native pointer mode therefore
// always spreads travel over at least three typical frames. Longer configured
// values remain available, but zero now means the shortest stable response
// rather than an unfiltered impulse.
constexpr float kMinimumNativeYawSmoothingSeconds = 0.030f;
// The camera action handler discards any magnitude at or below 0.65 and
// rescales the remainder over (0.65, 1.0]. Both numbers are read off the
// handler itself and are used to invert that gate, never to reproduce it.
constexpr float kGameStickGateThreshold = 0.65f;
// The definitions hold a per-second approach rate that the update combines
// with the frame delta into a fractional step. Solving rate * dt >= 1 gives
// the smallest rate that finishes the approach inside the current frame; the
// factor adds headroom in case the update is exponential rather than linear.
// The ceiling keeps the written field finite for a degenerate short frame.
constexpr float kIntraFrameConvergenceFactor = 8.0f;
constexpr float kMaximumApproachRate = 4096.0f;
constexpr std::size_t kDesiredCameraRootPositionOffset = 0x24;
constexpr std::size_t kCurrentZoomAOffset = 0x54;
constexpr std::size_t kCurrentZoomBOffset = 0x58;
constexpr std::size_t kDesiredZoomOffset = 0x5c;
// The three camera-arm direction floats live at +0x70:
//
//   ldp s1, s2, [x26, #0x70]  // direction.x, direction.y
//   ldr s3,     [x26, #0x78]  // direction.z
//
// Reading from +0x78 instead captured direction.z followed by two unrelated
// fields, so the floor query tested a fictitious camera position while the
// real camera could pass underground.
constexpr std::size_t kCameraDirectionOffset = 0x70;
constexpr std::size_t kCameraSystemStateOffset = 0x110;
constexpr std::size_t kPhysicsOwnerOffset = 0x90;
constexpr std::size_t kPhysicsSceneOffset = 0x30;
// Runtime captures from this build show rooms where the camera is still
// below floor + 0.1 at half a world unit, so the floor for the correction has
// to sit well under that. Keep a small non-zero arm to avoid a degenerate
// camera transform while allowing the response to approach the root closely.
constexpr float kMinimumCollisionZoom = 0.05f;
constexpr float kCollisionFallbackStep = 0.10f;
constexpr float kMaximumSaneZoom = 100.0f;
// A 0.10 world-unit clearance keeps the mathematical camera origin above the
// floor, but runtime captures in Moonrise Towers show the origin only ~0.34
// units above the surface. The camera's near clipping plane then crosses the
// floor even though the point test technically passes, exposing the underside
// of the room. Reserve one full world unit for the camera volume/near plane.
// This remains a floor-query-based limit: it does not impose a hard address or
// a location-specific height.
// A bisection over the arm length needs a fixed, small probe budget: each
// step halves the remaining interval, so 14 probes resolve a 20-unit arm to
// better than 0.002 world units.
constexpr std::size_t kFloorSearchProbes = 14;
constexpr float kFloorSearchTolerance = 0.002f;
constexpr std::size_t kFloorClearFramesBeforeRelease = 8;
constexpr float kFloorReleaseSpeedPerSecond = 1.5f;
constexpr std::size_t kCameraModeFlagsOffset = 0xa8;
constexpr std::size_t kForcedCameraDefinitionFlagOffset = 0x1362;
constexpr double kMaximumRecentMfiSampleSeconds = 0.25;
constexpr std::array<std::size_t, 3> kCameraDefinitionOffsets{
    0xc80,
    0x7c4,
    0x958,
};
// The camera arm length the game will accept is bounded by a max/min float
// pair inside each definition. Two pairs are relevant: the one the default
// camera uses and the one the controller camera uses. These are widened while
// the update runs, then put back, so nothing is left modified.
struct ZoomBoundsField {
    std::size_t maximumOffset;
    std::size_t minimumOffset;
};

constexpr std::array<ZoomBoundsField, 2> kZoomBoundsFields{{
    {0x28, 0x2c},
    {0x30, 0x34},
}};

// A pair only counts as the arm bounds if it still reads like an ordered pair
// of plausible world distances. An unrelated field landing inside this window
// by accident would almost never satisfy all of it, so a wrong assumption
// disables the override instead of writing somewhere unknown.
constexpr float kMinimumPlausibleZoomBound = 0.05f;
constexpr float kMaximumPlausibleZoomBound = 500.0f;

constexpr std::array<std::size_t, 3> kPitchApproachRateOffsets{
    0x48,
    0xf0,
    0xf4,
};

using OnStickChangedFunction = void (*)(void*, int, float);
using InputEventFunction = std::uint64_t (*)(void*, const void*);
struct FireEventSpan {
    const std::uint8_t* begin;
    const std::uint8_t* end;
};
using FireInputEventsFunction =
    void (*)(void*, void*, const FireEventSpan*, bool);
using CameraInputEventFunction =
    std::uint64_t (*)(void*, const void*, const void*);
using UpdateGameCameraBehaviorFunction =
    void (*)(void*, void*, void*, void*);
using PitchTailFunction = float (*)(void*, bool);

struct Vector3f {
    float x{};
    float y{};
    float z{};
};

// Reverse-engineered from CameraHelpers::CollideWithObstacles. The function
// initializes this 0x30-byte return buffer through x0. A miss leaves
// hitDistance at FLT_MAX; a hit fills the point, normal and obstacle fields.
struct CameraCollisionResult {
    Vector3f hitPosition;       // +0x00
    Vector3f hitNormal;         // +0x0c
    float hitDistance;          // +0x18
    std::uint32_t hitFlagsA;    // +0x1c
    std::uint32_t hitFlagsB;    // +0x20
    std::uint32_t padding;      // +0x24
    void* obstacle;             // +0x28
};

static_assert(offsetof(CameraCollisionResult, hitDistance) == 0x18);
static_assert(offsetof(CameraCollisionResult, obstacle) == 0x28);
static_assert(sizeof(CameraCollisionResult) == 0x30);

// This declaration describes the observed ARM64 register ABI, including the
// return-storage pointer in x0. It intentionally does not pretend that we
// know the original C++ return type.
using CollideWithObstaclesFunction = void (*)(
    CameraCollisionResult*,
    void*,
    void*,
    bool,
    const Vector3f*,
    const Vector3f*);

// The 12-byte result is returned in x0/x1. The layout is read off the
// instructions at the call sites, which take w0 as a float, x0 >> 32 as
// flag bytes, and x1 & 0xff as the "no floor found" indicator.
struct FloorLevelResult {
    float floorLevel;           // x0 bits 0..31, struct +0x00
    std::uint8_t flag04;        // x0 bits 32..39
    std::uint8_t flag05;
    std::uint16_t flags06;
    std::uint8_t noFloor;       // x1 bits 0..7, struct +0x08
    std::array<std::uint8_t, 3> padding;
};

static_assert(offsetof(FloorLevelResult, floorLevel) == 0x00);
static_assert(offsetof(FloorLevelResult, noFloor) == 0x08);
static_assert(sizeof(FloorLevelResult) == 0x0c);

// Observed post-LTO ARM64 ABI:
//   x0 WorldView, w1 mode flag, x2 CameraDefinition,
//   x3 position, x4 LevelManager; result in x0/x1.
using FloorLevelFunction = FloorLevelResult (*)(
    void*,
    bool,
    void*,
    const Vector3f*,
    void*);

std::atomic<float> gRightStickY{0.0f};
// Vertical travel accumulated since the last frame, the counterpart of
// gWheelRotateTravel. gRightStickY is kept alongside it for the controller,
// whose stick reports a deflection rather than travel.
std::atomic<float> gPitchTravel{0.0f};
std::atomic<bool> gRpgCameraModeActive{true};
std::atomic<bool> gHooksEnabled{false};
std::atomic<void*> gLastMfiInputDevice{nullptr};
std::atomic<std::uint64_t> gLastMfiSampleTime{0};
std::atomic<bool> gMfiL3Pressed{false};
// The pointer-device equivalent of L3: held, the camera's vertical axis
// zooms instead of pitching. Driven by whichever input event id the user
// configured, because that id is assigned by the game and cannot be guessed.
std::atomic<bool> gPointerZoomModifierHeld{false};

// Discovery aid. The id of a trackpad or mouse button is not documented and
// differs between input maps, so instead of guessing one, the mod records
// each distinct id it sees and the user reads the right one off the log.
//
// Both phases are recorded, because the id alone does not say whether a
// tap-versus-hold split is possible. The deferred-toggle trick that lets L3
// both toggle camera mode and act as a zoom chord works only because BG3
// dispatches ToggleInputMode on release: the mod learns whether the button
// was used for zoom before the action happens. A button whose action fires
// on press cannot be deferred that way, and this log distinguishes the two.
//
// Bounded and de-duplicated: a busy session must not flood the log.
constexpr std::size_t kMaximumDiscoveredEvents = 256;
std::mutex gDiscoveredEventMutex;
std::array<std::uint64_t, kMaximumDiscoveredEvents> gDiscoveredEvents{};
std::size_t gDiscoveredEventCount = 0;
// Every action the input manager dispatches, and every action that reaches
// the camera handler. Separate tables so neither can crowd the other out,
// and separate from gDiscoveredEvents because those are InputController
// events, which demonstrably never carry the camera actions.
constexpr std::size_t kMaximumDispatchActions = 256;
std::mutex gDispatchActionMutex;
std::array<std::uint64_t, kMaximumDispatchActions> gDispatchActions{};
std::size_t gDispatchActionCount = 0;
constexpr std::size_t kMaximumCameraHandlerActions = 128;
std::mutex gCameraHandlerActionMutex;
std::array<std::uint64_t, kMaximumCameraHandlerActions>
    gCameraHandlerActions{};
std::size_t gCameraHandlerActionCount = 0;
std::atomic<bool> gCollisionClampActive{false};
std::atomic<bool> gFloorClampActive{false};
std::atomic<bool> gZoomBoundsUsable{false};
// Diagnostics only. The payload floor is tracked per action, because the four
// camera actions are produced by different parts of the game's input mapping
// and need not share a threshold: zoom is what a dead area is reported on,
// rotation is what it is reported absent on. One combined figure cannot tell
// those apart, so each is measured on its own. The zoom actions are split by
// whether L3 was held, since that is the chord the complaint is about.
// Diagnostic: what we last handed the zoom handler, and what the game's own
// zoom target did in response. Shaping the payload changed nothing the player
// could see, so the transfer function has to be measured rather than assumed.
std::atomic<float> gLastZoomInput{0.0f};
std::atomic<float> gLastZoomPayload{0.0f};
std::atomic<unsigned> gLastZoomPressed{0};
std::atomic<float> gLastRotateNormalized{0.0f};
std::atomic<int> gCameraTraceBudget{600};
float gPreviousHeading = 0.0f;
float gPreviousTracePitch = 0.0f;
std::atomic<int> gZoomTraceBudget{600};
float gPreviousDesiredZoom = 0.0f;
// Raw 107/108 telemetry. Larger than the budgets above because the question
// it answers is the distribution of pointer magnitudes rather than whether
// one ever arrived, and a single swipe can carry a hundred samples.
constexpr int kInitialRotateTraceBudget = 4000;
std::atomic<int> gRotateTraceBudget{kInitialRotateTraceBudget};
// The vertical counterpart. Sized the same: a cross-axis question can only
// be answered if both axes are sampled over the same span of the session.
constexpr int kInitialXFeedTraceBudget = 3000;
std::atomic<int> gXFeedTraceBudget{kInitialXFeedTraceBudget};
constexpr int kInitialPointerVerticalTraceBudget = 4000;
std::atomic<int> gPointerVerticalTraceBudget{
    kInitialPointerVerticalTraceBudget};
// Small on purpose: this one answers a yes/no question that a couple of
// hundred frames settle - is the pointer non-null, is the entry inside the
// update, what does the frame delta look like - and the detour runs on every
// camera update.
constexpr int kInitialPreYawTraceBudget = 200;
std::atomic<int> gPreYawTraceBudget{kInitialPreYawTraceBudget};
// Fixed, un-rearmed: 109/110 scaling lines. A few hundred covers a session
// and MMB yaw only fires while the button is held.
constexpr int kInitialMouseYawScaleTraceBudget = 400;
std::atomic<int> gMouseYawScaleTraceBudget{
    kInitialMouseYawScaleTraceBudget};
// Read-only diagnostic (2.6.5-diag1): classify the native mouse-rotation
// input arms. Actions 109/110 write GameCameraBehavior+0xa0 from
// InputEvent+0x14; action 98 toggles the +0xa9 channel bit and pushes a
// forced cursor via ecl::CursorControl. Nothing here drives or alters that -
// the two InputEvent floats at +0x14/+0x18 are logged as raw, unestablished
// fields so a later capture can pin their meaning. Pointer motion runs before
// and between test blocks, so a plain budget would drain before the trackpad
// blocks: instead a burst of kNativeArmBurstSize events re-arms after any
// event-free gap of at least kNativeArmRearmGapSeconds, capped at
// kNativeArmMaxBursts bursts for the whole session. A test pause re-arms the
// burst only if no 98/109/110 arrive during it - that is, only while hands
// are off the mouse and trackpad.
constexpr int kNativeArmBurstSize = 64;
constexpr int kNativeArmMaxBursts = 64;
constexpr double kNativeArmRearmGapSeconds = 3.0;
std::atomic<std::uint64_t> gNativeArmLastEventTick{0};
std::atomic<int> gNativeArmBurstRemaining{0};
std::atomic<unsigned> gNativeArmBurstCount{0};
// Fixed and not re-armed: button/gate actions 1/3/15/98 are rare presses, a
// couple of hundred lines covers a whole session.
constexpr int kInitialButtonActionTraceBudget = 256;
std::atomic<int> gButtonActionTraceBudget{kInitialButtonActionTraceBudget};
// Entries that arrived outside a camera update. Counted rather than logged,
// because the budgeted line above stops after the first couple of seconds and
// this has to hold for the whole session.
std::atomic<unsigned long> gPreYawOutsideUpdateCount{0};

// Identity telemetry state.
//
// Not budgeted: a budget spends itself on the first seconds of a session,
// which is menu and loading, and the contexts worth checking - a camera
// transition, a mode switch, a different GameCameraBehavior - all arrive
// later. Logged on change, on anything other than a plain single-hit MATCH,
// and otherwise as a heartbeat, so a whole session costs a handful of lines
// while the running totals still account for every update.
//
// Written only from the camera update thread, like gPreviousHeading and
// gPreviousDesiredZoom beside it.
constexpr double kIdentityHeartbeatSeconds = 5.0;
void* gLastLoggedPreYawCamera{};
void* gLastLoggedAfterZoomCamera{};
std::uint64_t gLastIdentityLogTime{};
unsigned long gIdentityMatchCount{};
unsigned long gIdentityMismatchCount{};
unsigned long gIdentityNoPreYawCount{};
unsigned long gIdentityMultiHitCount{};

// Pre-gate verifier counters.
//
// Atomic because the pre-gate helper can in principle be entered from a
// thread that is not inside a camera update, and that case has to be counted
// rather than assumed away. Relaxed ordering: these are tallies, nothing
// branches on them.
std::atomic<unsigned long> gPreGateUpdatesObserved{0};
std::atomic<unsigned long> gPreGateEntries{0};
std::atomic<unsigned long> gPreGateOutsideUpdate{0};
std::atomic<unsigned long> gPreGateMissing{0};
std::atomic<unsigned long> gPreGateMultiHit{0};
std::atomic<unsigned long> gPointerMismatchBeforeConsume{0};
std::atomic<unsigned long> gDeltaChangedBeforeConsume{0};
std::atomic<unsigned long> gFlagsChangedBeforeConsume{0};
std::atomic<unsigned long> gPointerMismatchAfterConsume{0};
std::atomic<unsigned long> gDeltaChangedAfterConsume{0};
std::atomic<unsigned long> gFlagsChangedAfterConsume{0};
std::atomic<unsigned long> gConditionalHits{0};
std::atomic<unsigned long> gNoConditionalHit{0};
// Simultaneous native pointer input, measured only. No policy is applied.
std::atomic<unsigned long> gNativeFlagAlreadySet{0};
std::atomic<unsigned long> gNativeDeltaAlreadyNonZero{0};
std::atomic<unsigned long> gNativeBothAlready{0};

// Functional X-axis counters.
std::atomic<unsigned long> gXFeedEvents{0};
std::atomic<float> gXFeedTravel{0.0f};
std::atomic<unsigned long> gDriveRequested{0};
std::atomic<unsigned long> gDriveVerified{0};
std::atomic<unsigned long> gDriveMissedConditional{0};
std::atomic<unsigned long> gDrivePointerMismatch{0};
std::atomic<unsigned long> gDriveFieldMismatch{0};
std::atomic<unsigned long> gDriveCommitted{0};
std::atomic<unsigned long> gRestoreMismatch{0};
std::atomic<unsigned long> gCappedFrames{0};
std::atomic<unsigned long> gNativeConflictDrops{0};
std::atomic<float> gDroppedConflictTravel{0.0f};
std::atomic<float> gPendingPeak{0.0f};
std::atomic<unsigned long> gResidualTailFrames{0};
std::atomic<float> gResidualTailMilliseconds{0.0f};

// Adds to an atomic float from whichever thread reaches it first.
void AtomicFloatAdd(std::atomic<float>& target, const float amount) {
    if (!std::isfinite(amount)) {
        return;
    }
    float previous = target.load(std::memory_order_relaxed);
    for (;;) {
        const float updated = previous + amount;
        if (!std::isfinite(updated)) {
            return;
        }
        if (target.compare_exchange_weak(
                previous,
                updated,
                std::memory_order_relaxed,
                std::memory_order_relaxed)) {
            return;
        }
    }
}

void AtomicFloatMax(std::atomic<float>& target, const float value) {
    if (!std::isfinite(value)) {
        return;
    }
    float previous = target.load(std::memory_order_relaxed);
    while (value > previous) {
        if (target.compare_exchange_weak(
                previous,
                value,
                std::memory_order_relaxed,
                std::memory_order_relaxed)) {
            return;
        }
    }
}
// Distinct non-zero +0xa0 bit patterns seen, so the observed values are on
// record rather than summarised away.
constexpr std::size_t kMaximumObservedNativeDeltas = 16;
std::mutex gObservedNativeDeltaMutex;
std::array<std::uint32_t, kMaximumObservedNativeDeltas> gObservedNativeDeltas{};
std::size_t gObservedNativeDeltaCount = 0;
// Logging policy state, owned by the camera update thread.
constexpr unsigned long kPreGateVerboseUpdates = 5;
constexpr double kPreGateHeartbeatSeconds = 5.0;
std::uint64_t gPreGateLastLogTime{};
void* gPreGateLastLoggedCamera{};

std::atomic<float> gSmallestRawStick{1.0e9f};
std::atomic<float> gSmallestZoomInChord{1.0e9f};
std::atomic<float> gSmallestZoomOutChord{1.0e9f};
std::atomic<float> gSmallestZoomInPitch{1.0e9f};
std::atomic<float> gSmallestZoomOutPitch{1.0e9f};
std::atomic<float> gSmallestRotateLeft{1.0e9f};
std::atomic<float> gSmallestRotateRight{1.0e9f};
OnStickChangedFunction gOriginalOnStickChanged{};
InputEventFunction gOriginalInputEvent{};
FireInputEventsFunction gOriginalFireInputEvents{};
CameraInputEventFunction gOriginalCameraInputEvent{};
UpdateGameCameraBehaviorFunction gOriginalUpdateGameCameraBehavior{};
PitchTailFunction gPitchTail{};
CollideWithObstaclesFunction gCollideWithObstacles{};
FloorLevelFunction gGetFloorLevel{};

struct FloorConstraintState {
    void* cameraBehavior{};
    bool active{};
    float limitedZoom{};
    std::size_t clearFrames{};
};

FloorConstraintState gFloorConstraint;

std::mutex gPitchStateMutex;
std::mutex gDeferredToggleMutex;
std::mutex gRuntimeLogMutex;
std::mutex gCameraDefinitionMutex;
void* gLastCameraBehavior{};
bool gPitchInitialized{};
float gCurrentPitch{};
mach_timebase_info_data_t gTimebase{};
std::uint64_t gRuntimeLogEpoch{};
FILE* gRuntimeLog{};
void** gCameraDefinitionsBasePointer{};

// UpdateGameCameraBehavior receives ls::GameTime in x2. GetCameraPitchDegrees
// is called synchronously from that update, so thread-local state lets the
// pitch hook consume the exact game delta once without sharing it across
// unrelated calls or threads.
thread_local bool gInsideGameCameraUpdate{};
thread_local bool gPitchIntegratedThisUpdate{};
// Device that produced the batch of input events currently being dispatched.
//
// The camera event hook receives only an event type, a magnitude and a
// pressed byte - nothing that says where the input came from. The device is
// known one level up, in FireEventDesc + 0x30, so it is captured there and
// read back here. Without this the mod cannot tell a controller stick from a
// scroll wheel, and the two need opposite treatment: a stick rests near zero
// and drifts, so small magnitudes must be ignored; a wheel has no rest
// position and every notch arrives at one fixed magnitude.
//
// Set only when every descriptor in the batch agrees, so a mixed batch
// reports unknown rather than guessing.
constexpr std::uint16_t kUnknownDevice = 0xffff;
thread_local std::uint16_t gDispatchDevice{kUnknownDevice};

// Signed trackpad travel accumulated since the last frame. Samples add to it,
// the frame update takes all of it. Nothing else is kept, because nothing else
// is needed: the total is the whole signal.
std::atomic<float> gWheelRotateTravel{0.0f};
// When the last non-zero horizontal sample arrived. The drain uses it to tell
// a gap inside a gesture from the end of one: a trackpad goes quiet for tens
// of milliseconds mid-swipe, so a gesture is over when nothing has arrived
// for longer than the smoothing window, not when a single frame is empty.
std::atomic<std::uint64_t> gLastWheelTravelTime{0};

// Signed accumulation from the input thread.
//
// A compare-exchange loop rather than load-then-store: the two threads that
// touch this are the input thread and the camera update thread, and a plain
// read-modify-write would lose a sample whenever the update's exchange landed
// between them. The vertical feeder still uses load+store because it has only
// ever had one writer; that is not a pattern to copy.
void AccumulateWheelTravel(const float amount) {
    if (!std::isfinite(amount) || amount == 0.0f) {
        return;
    }

    float previous = gWheelRotateTravel.load(std::memory_order_relaxed);
    for (;;) {
        const float updated = previous + amount;
        if (!std::isfinite(updated)) {
            // Saturated. Dropping the accumulator is the only safe answer;
            // carrying a non-finite value forward would poison every later
            // frame's arithmetic.
            gWheelRotateTravel.store(0.0f, std::memory_order_relaxed);
            return;
        }
        if (gWheelRotateTravel.compare_exchange_weak(
                previous,
                updated,
                std::memory_order_relaxed,
                std::memory_order_relaxed)) {
            return;
        }
    }
}



// Which device's rotation needs smoothing.
//
// "Not the gamepad" is not the same question: the keyboard is not the gamepad
// either, and a key must keep the game's own handling - it is already a clean
// press and release, and rerouting it through a ramp only added a tail. A
// wheel is told apart by what it reports: a key can only ever send a full
// 1.0, so the first magnitude that is neither 0 nor exactly 1 identifies an
// analogue pointer. Until that is seen the device keeps the untouched path,
// so an unrecognised device behaves exactly as it did before.
constexpr int kNoWheelDevice = -1;
std::atomic<int> gWheelRotateDevice{kNoWheelDevice};

// The heading the mod owns while it is driving, and when it last drove.
//
// The unspent horizontal travel, and the lock that owns it.
//
// Nothing here holds an angle any more. The mod used to keep its own heading
// and write it back every frame, which is what put it in a fight with the
// camera's own follow correction; the pointer channel needs none of that,
// because the game does the turning from a rate the mod hands it once per
// update. What is left is a signed displacement waiting to be spent, and it
// deliberately has no camera attached: a displacement is valid against
// whatever object the next update happens to use.
std::mutex gYawStateMutex;
// Travel that has arrived but not yet been turned into rotation.
//
// Accumulating travel fixed how *much* a gesture turns the camera; it did
// nothing for how evenly. A frame's step was however many samples happened to
// land inside it, and a ~116 Hz trackpad against a ~120 fps frame beats
// between none, one and two - so the camera stepped, stepped, held, then
// double-stepped. The total was right and the motion was not.
//
// The servo used to hide this, being a rate-limited approach and therefore a
// filter. Holding it still to stop it eating the input took the filtering with
// it, which is why the judder only became visible once rotation started
// working at all.
//
// Draining this buffer over a time constant decouples the two: travel is
// banked as it arrives and spent as a continuous function of frame time, so
// nothing depends on where a sample fell relative to a frame boundary. The
// total still leaves the buffer in full, so the property 2.0.0 established is
// kept - a gesture is worth the same rotation at any frame rate.
float gPendingYawTravel{};
// The camera the held angle belongs to.
//
// A heading is absolute, so unlike the pitch target it cannot be carried
// across a change of GameCameraBehavior - BG3 passes several of those through
// during a transition, and writing an angle read from one onto another would
// snap the camera. The held angle is therefore rebased whenever the object
// changes, which costs at most part of one swipe during a transition.

std::atomic<std::uint64_t> gLastYawTravelTime{0};
std::atomic<bool> gYawFollowRatesUsable{false};

thread_local float gGameDeltaSeconds{};
thread_local void* gCameraBehaviorSeenThisUpdate{};
thread_local void* gCameraSystemForUpdate{};
thread_local void* gWorldViewForUpdate{};
thread_local bool gAfterZoomAppliedThisUpdate{};
// The GameCameraBehavior the pre-yaw detour was handed this update, taken from
// the register the update itself is about to read the rotation channel from.
//
// Read-only for now: nothing consumes it except the identity telemetry below.
// It exists so that the question "is this the same object the post-zoom detour
// sees?" can be answered from a log rather than argued from a disassembly.
thread_local void* gPreYawCameraSeenThisUpdate{};
// How many times the pre-yaw site was entered during the current update.
//
// The drain this telemetry is preparing for spends the accumulator once per
// update. If the site runs twice, spending it on entry would debit the same
// travel twice, so the multiplicity has to be a logged number rather than
// something inferred from the order two budgeted lines happened to land in.
thread_local unsigned gPreYawHitsThisUpdate{};
// Pre-gate snapshot for the current update.
//
// Taken at the dominator of the gate, before the game decides whether to read
// the rotation channel at all, and compared later at the two points that
// matter: immediately before the channel is consumed, and after the update
// has finished. The whole point is to establish - from the running game
// rather than from a disassembly - that the object and the two fields the
// feeder will write are still the same ones when the game reads them.
//
// Read-only in this build: nothing here is written back to the camera.
thread_local unsigned gPreGateHitsThisUpdate{};
thread_local void* gPreGateCamera{};
thread_local std::uint32_t gPreGateDeltaBits{};
thread_local std::uint8_t gPreGateFlagByte{};
thread_local bool gPreGateSeen{};

// Everything the injection needs to undo itself, scoped to one update.
//
// It exists because the write and the restore happen at two different points
// - inside the update at the pre-gate site, and after it returns - and the
// only thing that may connect them is state belonging to this thread and this
// update. No camera pointer survives past the update: the pointer is recorded
// here to be checked, never to be dereferenced on a later frame.
struct NativeInjection {
    bool active;
    void* camera;
    std::uint32_t previousDeltaBits;
    std::uint8_t previousFlagByte;
    std::uint32_t injectedDeltaBits;
    std::uint8_t injectedFlagByte;
    float proposedSpendTravel;
    float askedDegrees;
    bool conditionalVerified;
    bool afterZoomVerified;
    bool nativeConflict;
    bool restored;
};

thread_local NativeInjection gInjection{};

struct DeferredToggle {
    bool pending{};
    bool usedForZoom{};
};

DeferredToggle gDeferredToggle;

double SecondsBetween(std::uint64_t previous, std::uint64_t current);

void RuntimeLog(const char* format, ...) {
    if (!GetCameraConfig().verboseLogging) {
        return;
    }

    std::lock_guard lock(gRuntimeLogMutex);
    if (gRuntimeLog == nullptr) {
        return;
    }

    // Milliseconds since the mod loaded. Timing questions - how far apart a
    // trackpad's samples really are, how long a gesture lasts - cannot be
    // answered from an unstamped log, and they are the questions this input
    // path keeps raising.
    std::fprintf(
        gRuntimeLog,
        "[RUNTIME %8.3f] ",
        SecondsBetween(gRuntimeLogEpoch, mach_continuous_time()) * 1000.0);
    va_list arguments;
    va_start(arguments, format);
    std::vfprintf(gRuntimeLog, format, arguments);
    va_end(arguments);
    std::fputc('\n', gRuntimeLog);
    std::fflush(gRuntimeLog);
}

// True while the configured physical mouse button is actually held down.
//
// Asks the window server for the button's real state instead of tracking
// game events. The game reports a mouse button as a press immediately
// followed by a release even while it stays physically down, so an
// event-driven hold covers microseconds and no scroll lands inside it.
//
// This mirrors how L3 is handled: that is polled from the controller's
// button field rather than reconstructed from events, for the same reason.
bool IsPointerHoldButtonDown() {
    const int button = GetCameraConfig().pointerZoomHoldButton;
    if (button <= 0 || button > 3) {
        return false;
    }

    // Config numbers the buttons from 1 so that 0 can mean off; CGMouseButton
    // numbers them from 0.
    return CGEventSourceButtonState(
               kCGEventSourceStateCombinedSessionState,
               static_cast<CGMouseButton>(button - 1)) != 0;
}

// Records each (device, camera action) pair the first time it is seen, so
// the device id to put in ZoomDeviceId can be read off the log. Without it
// there is no way to tell which id belongs to the keyboard and which to the
// trackpad, and both are simply "not the gamepad".
constexpr std::size_t kMaximumDiscoveredDevices = 32;
std::mutex gDiscoveredDeviceMutex;
std::array<std::uint32_t, kMaximumDiscoveredDevices> gDiscoveredDevices{};
std::size_t gDiscoveredDeviceCount = 0;

void NoteCameraEventDevice(
    const std::uint16_t device,
    const std::uint32_t eventType,
    const float magnitude) {
    if (!GetCameraConfig().verboseLogging) {
        return;
    }

    const std::uint32_t key =
        (static_cast<std::uint32_t>(device) << 16) | eventType;
    {
        std::lock_guard lock(gDiscoveredDeviceMutex);
        for (std::size_t index = 0; index < gDiscoveredDeviceCount; ++index) {
            if (gDiscoveredDevices[index] == key) {
                return;
            }
        }
        if (gDiscoveredDeviceCount >= kMaximumDiscoveredDevices) {
            return;
        }
        gDiscoveredDevices[gDiscoveredDeviceCount] = key;
        ++gDiscoveredDeviceCount;
    }

    RuntimeLog(
        "camera action %u arrived from device=%u magnitude=%.4f; set "
        "ZoomDeviceId to %u to make that device zoom instead of pitch",
        eventType,
        static_cast<unsigned>(device),
        static_cast<double>(magnitude),
        static_cast<unsigned>(device));
}

// Records an input event id the first time it is seen pressed, so the user
// can identify the button they want to use as the pointer zoom modifier.
// Only runs with verbose logging on, and only for ids this mod does not
// already handle, so the log stays readable.
void NoteDiscoveredEvent(
    const std::uint16_t device,
    const std::uint32_t eventType,
    const std::uint8_t pressed) {
    const CameraConfig& config = GetCameraConfig();
    if (!config.verboseLogging) {
        return;
    }
    if (config.pointerZoomModifierEvent != 0 &&
        eventType ==
            static_cast<std::uint32_t>(config.pointerZoomModifierEvent)) {
        return;
    }

    // One slot per (device, id, phase). The phase split answers whether a
    // button can be deferred; the device belongs in the key because one
    // action id arrives from several of them - a left stick and W both send
    // CharacterMoveForward - and without it the first device to send an id
    // silences every other one for the rest of the session.
    const std::uint64_t key =
        (static_cast<std::uint64_t>(device) << 32) |
        (static_cast<std::uint64_t>(eventType) << 1) |
        (pressed != 0 ? 1u : 0u);

    {
        std::lock_guard lock(gDiscoveredEventMutex);
        for (std::size_t index = 0; index < gDiscoveredEventCount; ++index) {
            if (gDiscoveredEvents[index] == key) {
                return;
            }
        }
        if (gDiscoveredEventCount >= kMaximumDiscoveredEvents) {
            return;
        }
        gDiscoveredEvents[gDiscoveredEventCount] = key;
        ++gDiscoveredEventCount;
    }

    RuntimeLog(
        "input event id=%u (0x%x) device=%u phase=%s; set "
        "PointerZoomModifierEvent to %u to use it as the zoom modifier",
        eventType,
        eventType,
        static_cast<unsigned>(device),
        pressed != 0 ? "press" : "release",
        eventType);
}

// Bounded, de-duplicated recorder for the two discovery logs below. Returns
// true only the first time a key is seen, so a busy session cannot flood the
// log and a full table stops recording rather than overwriting.
bool NoteOnce(
    std::mutex& mutex,
    std::uint64_t* table,
    const std::size_t capacity,
    std::size_t& count,
    const std::uint64_t key) {
    std::lock_guard lock(mutex);
    for (std::size_t index = 0; index < count; ++index) {
        if (table[index] == key) {
            return false;
        }
    }
    if (count >= capacity) {
        return false;
    }

    table[count] = key;
    ++count;
    return true;
}

// Every action the input manager dispatches, once per (device, action,
// phase).
//
// The device comes from the descriptor itself, not from gDispatchDevice: the
// latter is a per-batch summary that collapses to unknown the moment one
// batch mixes two devices, which is precisely the case this log exists to
// inspect. This point also sits above every downstream handler, so an action
// that never reaches the camera hook is still recorded here.
void NoteDispatchAction(
    const std::uint16_t device,
    const std::uint32_t actionType,
    const std::uint8_t phase) {
    if (!GetCameraConfig().verboseLogging) {
        return;
    }

    const std::uint64_t key =
        (static_cast<std::uint64_t>(device) << 40) |
        (static_cast<std::uint64_t>(actionType) << 8) |
        static_cast<std::uint64_t>(phase);
    if (!NoteOnce(
            gDispatchActionMutex,
            gDispatchActions.data(),
            gDispatchActions.size(),
            gDispatchActionCount,
            key)) {
        return;
    }

    RuntimeLog(
        "dispatch action id=%u (0x%x) device=%u phase=%u",
        actionType,
        actionType,
        static_cast<unsigned>(device),
        static_cast<unsigned>(phase));
}

// Every action that reaches CameraSystem::OnInputEvent, recorded above the
// filter that keeps only the four this mod handles. Without it an action the
// hook forwards untouched is returned before anything logs it, so "the mod
// never saw it" and "the mod saw it and passed it on" are indistinguishable.
//
// The device is the batch summary, because this hook is handed no device of
// its own; a mixed batch therefore reports 65535 rather than guessing.
void NoteCameraHandlerAction(
    const std::uint16_t device,
    const std::uint32_t actionType) {
    if (!GetCameraConfig().verboseLogging) {
        return;
    }

    const std::uint64_t key =
        (static_cast<std::uint64_t>(device) << 32) |
        static_cast<std::uint64_t>(actionType);
    if (!NoteOnce(
            gCameraHandlerActionMutex,
            gCameraHandlerActions.data(),
            gCameraHandlerActions.size(),
            gCameraHandlerActionCount,
            key)) {
        return;
    }

    RuntimeLog(
        "camera handler action id=%u device=%u",
        actionType,
        static_cast<unsigned>(device));
}

// Maps a raw stick magnitude onto [0, 1] across the span that survives the
// configured deadzone. Anything at or below the deadzone reports no motion.
float NormalizeOutsideDeadzone(const float magnitude) {
    const float deadzone = GetCameraConfig().stickDeadzone;
    if (!(magnitude > deadzone)) {
        return 0.0f;
    }

    return std::clamp(
        (magnitude - deadzone) / (1.0f - deadzone),
        0.0f,
        1.0f);
}

void NoteSmallestMagnitude(
    std::atomic<float>& smallest,
    const char* stage,
    const float magnitude) {
    if (!(magnitude > 0.0f)) {
        return;
    }

    float previous = smallest.load(std::memory_order_relaxed);
    while (magnitude < previous) {
        if (smallest.compare_exchange_weak(
                previous,
                magnitude,
                std::memory_order_relaxed)) {
            RuntimeLog("smallest %s magnitude so far: %.4f", stage, magnitude);
            return;
        }
    }
}

float ApplyAxialDeadzone(const float value) {
    const float normalized = NormalizeOutsideDeadzone(std::abs(value));
    if (normalized == 0.0f) {
        return 0.0f;
    }

    return std::copysign(normalized, value);
}

// The camera action handler re-normalizes the magnitude it is handed as
//
//     g(x) = (x - kGameStickGateThreshold) / (1 - kGameStickGateThreshold)
//
// and treats anything at or below the threshold as no input. To make that
// handler act on a magnitude of our choosing, solve g(x) = magnitude for x.
// The equation has a single solution, so this transform is forced rather
// than chosen.
float PreImageOfGameStickGate(const float magnitude) {
    return kGameStickGateThreshold +
        (1.0f - kGameStickGateThreshold) * magnitude;
}

// Zoom goes to the game's own zoom handler, which applies the same gate as the
// rotation path. Left alone it ignores every deflection below the gate and
// then starts at the gate's own magnitude, which is a step rather than a ramp.
// Scaling the payload down does not soften that - a scaled value simply falls
// under the gate and is dropped, so a lower sensitivity only widens the dead
// band. Normalising out of the configured deadzone first, shaping the result,
// then mapping it back through the gate gives a continuous ramp that begins
// exactly where the deadzone ends.
float ScaleZoomStickInput(const float value, const float discreteStep) {
    const CameraConfig& config = GetCameraConfig();

    // A scroll wheel is not an analogue axis. Every notch arrives at the same
    // fixed magnitude - 0.1 on this build - which sits below a deadzone tuned
    // for stick drift, so normalising it against that deadzone yields exactly
    // zero. Because the hook replaces the event with its own payload, that
    // zero did not fall back to the game's behaviour: it removed zoom
    // altogether while the button was held.
    //
    // A discrete input wants a discrete response, so a notch produces one
    // step at the configured sensitivity rather than a ramp shaped out of a
    // travel range it does not have.
    if (discreteStep > 0.0f) {
        if (value == 0.0f) {
            return 0.0f;
        }

        const float step = std::clamp(discreteStep, 0.0f, 1.0f);
        return std::copysign(PreImageOfGameStickGate(step), value);
    }

    const float magnitude = NormalizeOutsideDeadzone(std::abs(value));
    if (magnitude == 0.0f) {
        return 0.0f;
    }

    // An exponent above 1 spends more of the stick's travel on the slow end,
    // which is the half that actually picks a distance.
    const float shaped = std::pow(magnitude, config.zoomResponseCurve);

    // Measured against the rotation action, which shares this deadzone and is
    // not reported as having a dead band, zoom was arriving about three times
    // weaker at every small deflection. The band was never a threshold - the
    // game delivers zoom payloads well below the deadzone - it was rate: the
    // camera moved too slowly to read as moving. Starting the ramp at a
    // visible rate fixes that without raising the top speed, which is the part
    // that made choosing a distance hard.
    const float span = std::max(
        config.zoomSensitivity - config.zoomMinimumResponse,
        0.0f);
    const float scaled = std::clamp(
        config.zoomMinimumResponse + span * shaped,
        0.0f,
        1.0f);

    return std::copysign(PreImageOfGameStickGate(scaled), value);
}

// Horizontal rotation is handed straight to the game's own yaw path, so the
// only work here is undoing that gate and applying the user's sensitivity. A
// sensitivity of 1.0 reproduces the handler's unmodified full-scale rate.
float ScaleHorizontalStickInput(const float value) {
    const float magnitude = NormalizeOutsideDeadzone(value);
    if (magnitude == 0.0f) {
        return 0.0f;
    }

    const CameraConfig& config = GetCameraConfig();

    // The product is deliberately not clamped to 1.0. Both this transform and
    // the handler's own re-normalisation are affine, so a magnitude above
    // full scale is meaningful rather than undefined: it asks the handler for
    // proportionally more rotation, which is precisely what a sensitivity
    // above 1.0 means. Clamping here silently capped every sensitivity at
    // 1.0, which is what made rotation feel slow no matter how the setting
    // was raised.
    return PreImageOfGameStickGate(
        magnitude * config.gamepadHorizontalSensitivity);
}

// ZoomIn and ZoomOut are the game's two halves of one signed axis.
float AxisSignForZoomEvent(const std::uint32_t eventType) {
    return eventType == kZoomInEvent ? -1.0f : 1.0f;
}

// Every payload rewrite in this translation unit goes through here: the event
// is copied by value and only the magnitude field is replaced, so the game
// never observes a mutated copy of its own event storage.
std::array<std::uint8_t, kInputEventSize> CloneEventWithValue(
    const void* inputEvent,
    const float value) {
    std::array<std::uint8_t, kInputEventSize> clone{};
    std::memcpy(clone.data(), inputEvent, clone.size());
    std::memcpy(
        clone.data() + kInputEventValueOffset,
        &value,
        sizeof(value));
    return clone;
}

// Copy the event by value and scale only the native-yaw float at +0x14. Event
// type, +0x18, the pressed byte and everything else are left exactly as the
// game wrote them; a zero stays zero, so a release still reads as a release.
std::array<std::uint8_t, kInputEventSize> CloneEventWithScaledNativeYaw(
    const void* inputEvent,
    const float scale) {
    std::array<std::uint8_t, kInputEventSize> clone{};
    std::memcpy(clone.data(), inputEvent, clone.size());
    float field{};
    std::memcpy(
        &field, clone.data() + kInputEventNativeYawOffset, sizeof(field));
    if (std::isfinite(field)) {
        field *= scale;
        std::memcpy(
            clone.data() + kInputEventNativeYawOffset,
            &field,
            sizeof(field));
    }
    return clone;
}

// Smallest approach rate that finishes inside a frame of the given length.
// See kIntraFrameConvergenceFactor for the derivation.
float IntraFrameApproachRate(const float deltaSeconds) {
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f) {
        return kMaximumApproachRate;
    }

    const float rate = kIntraFrameConvergenceFactor / deltaSeconds;
    if (!std::isfinite(rate)) {
        return kMaximumApproachRate;
    }

    return std::min(rate, kMaximumApproachRate);
}

// UpdateGameCameraBehavior receives ls::GameTime; the frame delta sits at a
// fixed offset inside it. An out-of-range value is reported as zero so every
// caller can treat "no usable delta" as one condition.
float ReadGameDeltaSeconds(const void* gameTime) {
    if (gameTime == nullptr) {
        return 0.0f;
    }

    float deltaSeconds{};
    std::memcpy(
        &deltaSeconds,
        static_cast<const std::uint8_t*>(gameTime) + kGameTimeDeltaOffset,
        sizeof(deltaSeconds));

    if (!std::isfinite(deltaSeconds) ||
        deltaSeconds <= 0.0f ||
        deltaSeconds > kMaximumGameDeltaSeconds) {
        return 0.0f;
    }

    return deltaSeconds;
}

double SecondsBetween(
    const std::uint64_t previous,
    const std::uint64_t current) {
    if (previous == 0 || current <= previous || gTimebase.denom == 0) {
        return 0.0;
    }

    const long double ticks =
        static_cast<long double>(current - previous);
    const long double nanoseconds =
        ticks * static_cast<long double>(gTimebase.numer) /
        static_cast<long double>(gTimebase.denom);
    return static_cast<double>(nanoseconds / 1'000'000'000.0L);
}

float CallOriginalPitch(void* cameraBehavior, const bool modeFlag) {
    const auto base =
        reinterpret_cast<const std::uint8_t*>(cameraBehavior);

    std::uint8_t overrideEnabled{};
    std::memcpy(
        &overrideEnabled,
        base + 0x1e4,
        sizeof(overrideEnabled));

    if (overrideEnabled != 0 && !modeFlag) {
        float overridePitch{};
        std::memcpy(
            &overridePitch,
            base + 0x1e0,
            sizeof(overridePitch));
        return overridePitch;
    }

    return gPitchTail(cameraBehavior, modeFlag);
}

template <typename T>
T ReadCameraField(void* cameraBehavior, const std::size_t offset) {
    T value{};
    const auto* address =
        static_cast<const std::uint8_t*>(cameraBehavior) + offset;
    std::memcpy(&value, address, sizeof(value));
    return value;
}

template <typename T>
void WriteCameraField(
    void* cameraBehavior,
    const std::size_t offset,
    const T& value) {
    auto* address =
        static_cast<std::uint8_t*>(cameraBehavior) + offset;
    std::memcpy(address, &value, sizeof(value));
}

bool IsFiniteVector(const Vector3f& value) {
    return std::isfinite(value.x) &&
        std::isfinite(value.y) &&
        std::isfinite(value.z);
}

void* ResolvePhysicsScene(void* cameraSystem) {
    if (cameraSystem == nullptr) {
        return nullptr;
    }

    // UpdateGameCameraBehavior performs the same three loads before calling
    // CameraHelpers::CollideCircle:
    //   state        = *(CameraSystem + 0x110)
    //   physicsOwner = *(state + 0x90)
    //   physicsScene = *(physicsOwner + 0x30)
    void* state = ReadCameraField<void*>(
        cameraSystem,
        kCameraSystemStateOffset);
    if (state == nullptr) {
        return nullptr;
    }

    void* physicsOwner = ReadCameraField<void*>(
        state,
        kPhysicsOwnerOffset);
    if (physicsOwner == nullptr) {
        return nullptr;
    }

    return ReadCameraField<void*>(
        physicsOwner,
        kPhysicsSceneOffset);
}

void ApplyCameraCollisionLimit(
    void* cameraSystem,
    void* collisionWorldView,
    void* cameraBehavior) {
    if (gCollideWithObstacles == nullptr ||
        collisionWorldView == nullptr ||
        cameraBehavior == nullptr) {
        return;
    }

    void* physicsScene = ResolvePhysicsScene(cameraSystem);
    if (physicsScene == nullptr) {
        return;
    }

    const Vector3f root = ReadCameraField<Vector3f>(
        cameraBehavior,
        kDesiredCameraRootPositionOffset);
    const Vector3f direction = ReadCameraField<Vector3f>(
        cameraBehavior,
        kCameraDirectionOffset);
    const float currentZoom = ReadCameraField<float>(
        cameraBehavior,
        kCurrentZoomBOffset);
    const float desiredZoom = ReadCameraField<float>(
        cameraBehavior,
        kDesiredZoomOffset);

    if (!IsFiniteVector(root) ||
        !IsFiniteVector(direction) ||
        !std::isfinite(currentZoom) ||
        !std::isfinite(desiredZoom) ||
        currentZoom <= 0.0f ||
        currentZoom > kMaximumSaneZoom ||
        desiredZoom <= 0.0f ||
        desiredZoom > kMaximumSaneZoom) {
        return;
    }

    // Probe the whole distance the player asked for, not merely the current
    // (possibly already collision-clamped) distance. Otherwise a shortened
    // ray misses on the next frame and the camera oscillates between hit and
    // clear while vanilla tries to restore the zoom.
    const float probeZoom = std::max(currentZoom, desiredZoom);

    const float directionLengthSquared =
        direction.x * direction.x +
        direction.y * direction.y +
        direction.z * direction.z;
    if (!std::isfinite(directionLengthSquared) ||
        directionLengthSquared < 0.5f ||
        directionLengthSquared > 1.5f) {
        return;
    }

    // The sampled vector is normalized in the current build. Normalize once
    // more defensively so the ray length remains exactly equal to zoom even
    // if floating-point drift changes its length slightly.
    const float inverseDirectionLength =
        1.0f / std::sqrt(directionLengthSquared);
    const Vector3f segmentDelta{
        direction.x * inverseDirectionLength * probeZoom,
        direction.y * inverseDirectionLength * probeZoom,
        direction.z * inverseDirectionLength * probeZoom,
    };

    CameraCollisionResult collision{};
    gCollideWithObstacles(
        &collision,
        collisionWorldView,
        physicsScene,
        false,
        &root,
        &segmentDelta);

    const bool hit =
        std::isfinite(collision.hitDistance) &&
        collision.hitDistance >= 0.0f &&
        collision.hitDistance < std::numeric_limits<float>::max();
    if (!hit) {
        if (gCollisionClampActive.exchange(
                false,
                std::memory_order_relaxed)) {
            RuntimeLog(
                "camera collision cleared; vanilla zoom may restore toward %.3f",
                desiredZoom);
        }
        return;
    }

    // Runtime evidence from this build shows hitDistance in [0, 1]: it is the
    // normalized position along the ray, not a distance in world units.
    // Retain geometric/world-distance fallbacks so a small representation
    // change does not turn into an unsafe large zoom value.
    float safeZoom = currentZoom - kCollisionFallbackStep;
    float worldHitDistance = std::numeric_limits<float>::quiet_NaN();
    const float safetyMargin =
        GetCameraConfig().collisionSafetyMargin;
    if (collision.hitDistance <= 1.0001f) {
        worldHitDistance = collision.hitDistance * probeZoom;
    } else if (collision.hitDistance <=
        probeZoom + safetyMargin) {
        worldHitDistance = collision.hitDistance;
    } else if (IsFiniteVector(collision.hitPosition)) {
        const float dx = collision.hitPosition.x - root.x;
        const float dy = collision.hitPosition.y - root.y;
        const float dz = collision.hitPosition.z - root.z;
        worldHitDistance =
            std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    if (std::isfinite(worldHitDistance) &&
        worldHitDistance >= 0.0f &&
        worldHitDistance <= probeZoom + safetyMargin) {
        safeZoom = worldHitDistance - safetyMargin;
    }

    safeZoom = std::clamp(
        safeZoom,
        kMinimumCollisionZoom,
        probeZoom);
    const float appliedZoom = std::min(currentZoom, safeZoom);
    WriteCameraField(cameraBehavior, kCurrentZoomAOffset, appliedZoom);
    WriteCameraField(cameraBehavior, kCurrentZoomBOffset, appliedZoom);

    if (!gCollisionClampActive.exchange(
            true,
            std::memory_order_relaxed)) {
        RuntimeLog(
            "camera collision clamp active current=%.3f applied=%.3f "
            "desired=%.3f probe=%.3f rawHit=%.3f worldHit=%.3f "
            "obstacle=%p",
            currentZoom,
            appliedZoom,
            desiredZoom,
            probeZoom,
            collision.hitDistance,
            worldHitDistance,
            collision.obstacle);
    }
}

void* ResolveCurrentCameraDefinition(void* cameraBehavior) {
    if (cameraBehavior == nullptr ||
        gCameraDefinitionsBasePointer == nullptr) {
        return nullptr;
    }

    void* definitionsBase{};
    std::memcpy(
        &definitionsBase,
        gCameraDefinitionsBasePointer,
        sizeof(definitionsBase));
    if (definitionsBase == nullptr) {
        return nullptr;
    }

    const std::uint8_t forcedDefinition =
        ReadCameraField<std::uint8_t>(
            definitionsBase,
            kForcedCameraDefinitionFlagOffset);
    std::size_t definitionOffset = kCameraDefinitionOffsets[0];
    if (forcedDefinition == 0) {
        const std::uint8_t cameraModeFlags =
            ReadCameraField<std::uint8_t>(
                cameraBehavior,
                kCameraModeFlagsOffset);
        definitionOffset = (cameraModeFlags & 1u) == 0
            ? kCameraDefinitionOffsets[1]
            : kCameraDefinitionOffsets[2];
    }

    return static_cast<std::uint8_t*>(definitionsBase) +
        definitionOffset;
}

void ResetFloorConstraint() {
    gFloorConstraint = {};
    gFloorClampActive.store(false, std::memory_order_relaxed);
}

void ApplyFloorClearanceLimit(
    void* cameraSystem,
    void* collisionWorldView,
    void* cameraBehavior) {
    if (gGetFloorLevel == nullptr ||
        cameraSystem == nullptr ||
        collisionWorldView == nullptr ||
        cameraBehavior == nullptr) {
        return;
    }

    if (gFloorConstraint.cameraBehavior != cameraBehavior) {
        ResetFloorConstraint();
        gFloorConstraint.cameraBehavior = cameraBehavior;
    }

    void* levelManager = ReadCameraField<void*>(
        cameraSystem,
        kCameraSystemStateOffset);
    void* cameraDefinition =
        ResolveCurrentCameraDefinition(cameraBehavior);
    if (levelManager == nullptr || cameraDefinition == nullptr) {
        return;
    }

    const Vector3f root = ReadCameraField<Vector3f>(
        cameraBehavior,
        kDesiredCameraRootPositionOffset);
    const Vector3f direction = ReadCameraField<Vector3f>(
        cameraBehavior,
        kCameraDirectionOffset);
    const float currentZoom = ReadCameraField<float>(
        cameraBehavior,
        kCurrentZoomBOffset);
    const float desiredZoom = ReadCameraField<float>(
        cameraBehavior,
        kDesiredZoomOffset);

    if (!IsFiniteVector(root) ||
        !IsFiniteVector(direction) ||
        !std::isfinite(currentZoom) ||
        !std::isfinite(desiredZoom) ||
        currentZoom <= 0.0f ||
        desiredZoom <= 0.0f ||
        currentZoom > kMaximumSaneZoom ||
        desiredZoom > kMaximumSaneZoom) {
        return;
    }

    const float directionLengthSquared =
        direction.x * direction.x +
        direction.y * direction.y +
        direction.z * direction.z;
    if (!std::isfinite(directionLengthSquared) ||
        directionLengthSquared < 0.5f ||
        directionLengthSquared > 1.5f) {
        return;
    }

    const float inverseDirectionLength =
        1.0f / std::sqrt(directionLengthSquared);
    const Vector3f unitDirection{
        direction.x * inverseDirectionLength,
        direction.y * inverseDirectionLength,
        direction.z * inverseDirectionLength,
    };

    // One evaluation of the clearance predicate at a given arm length.
    struct ArmProbe {
        bool clear;
        bool haveFloor;
        float cameraY;
        float probeY;
        float floorY;
    };

    const float safetyOffset = GetCameraConfig().floorSafetyOffset;
    const auto probeArm = [&](const float armLength) -> ArmProbe {
        const Vector3f candidatePosition{
            root.x + unitDirection.x * armLength,
            root.y + unitDirection.y * armLength,
            root.z + unitDirection.z * armLength,
        };

        // A point query taken from an already-underground position tunnels
        // between stacked floors: once past the upper room's floor the query
        // starts reporting the room below and the position looks valid again.
        // Keep the candidate's X/Z but never sample below the camera root,
        // which follows the controlled character's storey, so the query keeps
        // seeing the floor the arm is actually trying to cross.
        const Vector3f floorProbePosition{
            candidatePosition.x,
            std::max(candidatePosition.y, root.y),
            candidatePosition.z,
        };

        const FloorLevelResult floor = gGetFloorLevel(
            collisionWorldView,
            false,
            cameraDefinition,
            &floorProbePosition,
            levelManager);

        if (floor.noFloor != 0 || !std::isfinite(floor.floorLevel)) {
            // Nothing to stay above, so this length imposes no constraint.
            return ArmProbe{
                true,
                false,
                candidatePosition.y,
                floorProbePosition.y,
                std::numeric_limits<float>::quiet_NaN(),
            };
        }

        return ArmProbe{
            candidatePosition.y >= floor.floorLevel + safetyOffset,
            true,
            candidatePosition.y,
            floorProbePosition.y,
            floor.floorLevel,
        };
    };

    bool adjusted = false;
    bool haveValidFloor = false;
    float lastCameraY = std::numeric_limits<float>::quiet_NaN();
    float lastFloorProbeY = std::numeric_limits<float>::quiet_NaN();
    float lastFloorY = std::numeric_limits<float>::quiet_NaN();
    std::size_t probes = 0;

    const auto record = [&](const ArmProbe& probe) {
        ++probes;
        haveValidFloor = haveValidFloor || probe.haveFloor;
        lastCameraY = probe.cameraY;
        lastFloorProbeY = probe.probeY;
        if (probe.haveFloor) {
            lastFloorY = probe.floorY;
        }
    };

    float finalZoom = std::clamp(
        currentZoom,
        kMinimumCollisionZoom,
        kMaximumSaneZoom);

    // Clearance is monotonic in arm length: shortening the arm can only move
    // its endpoint toward the root, and the root is the reference the query is
    // anchored to. So the longest acceptable arm can be bracketed between a
    // known-good and a known-bad length and bisected, which costs a fixed
    // handful of queries instead of one per unit of travel.
    const ArmProbe atCurrent = probeArm(finalZoom);
    record(atCurrent);

    if (!atCurrent.clear) {
        adjusted = true;

        const ArmProbe atShortest = probeArm(kMinimumCollisionZoom);
        record(atShortest);

        if (!atShortest.clear) {
            // Even the shortest arm sits below the floor, so no longer arm
            // can help and the shortest is the best available answer.
            finalZoom = kMinimumCollisionZoom;
        } else {
            float accepted = kMinimumCollisionZoom;
            float rejected = finalZoom;

            for (std::size_t step = 0;
                 step < kFloorSearchProbes &&
                     rejected - accepted > kFloorSearchTolerance;
                 ++step) {
                const float midpoint =
                    accepted + (rejected - accepted) * 0.5f;
                const ArmProbe atMidpoint = probeArm(midpoint);
                record(atMidpoint);

                if (atMidpoint.clear) {
                    accepted = midpoint;
                } else {
                    rejected = midpoint;
                }
            }

            finalZoom = accepted;
        }
    }

    const float releaseStep =
        std::max(gGameDeltaSeconds, 0.0f) *
        kFloorReleaseSpeedPerSecond;

    if (adjusted) {
        const float measuredLimit = std::min(currentZoom, finalZoom);
        const float previousLimit = gFloorConstraint.limitedZoom;

        if (!gFloorConstraint.active) {
            gFloorConstraint.active = true;
            gFloorConstraint.limitedZoom = measuredLimit;
        } else if (measuredLimit < gFloorConstraint.limitedZoom) {
            // Moving into geometry must react immediately.
            gFloorConstraint.limitedZoom = measuredLimit;
        } else {
            // A less restrictive measurement may be a neighbouring stair
            // returning a different floor. Release toward it over time.
            gFloorConstraint.limitedZoom = std::min(
                measuredLimit,
                gFloorConstraint.limitedZoom + releaseStep);
        }
        gFloorConstraint.clearFrames = 0;

        if (previousLimit > 0.0f &&
            gFloorConstraint.limitedZoom < previousLimit - 0.25f) {
            RuntimeLog(
                "floor constraint tightened old=%.3f new=%.3f "
                "measured=%.3f cameraY=%.3f probeY=%.3f floorY=%.3f",
                previousLimit,
                gFloorConstraint.limitedZoom,
                measuredLimit,
                lastCameraY,
                lastFloorProbeY,
                lastFloorY);
        }
    } else if (gFloorConstraint.active) {
        ++gFloorConstraint.clearFrames;

        if (gFloorConstraint.clearFrames >
            kFloorClearFramesBeforeRelease) {
            gFloorConstraint.limitedZoom = std::min(
                desiredZoom,
                gFloorConstraint.limitedZoom + releaseStep);

            if (gFloorConstraint.limitedZoom >= desiredZoom - 0.001f) {
                RuntimeLog(
                    "floor constraint released after stable clear "
                    "validFloor=%d cameraY=%.3f probeY=%.3f floorY=%.3f",
                    haveValidFloor ? 1 : 0,
                    lastCameraY,
                    lastFloorProbeY,
                    lastFloorY);
                ResetFloorConstraint();
                return;
            }
        }
    } else {
        return;
    }

    const float appliedZoom = std::min(
        currentZoom,
        gFloorConstraint.limitedZoom);
    WriteCameraField(cameraBehavior, kCurrentZoomAOffset, appliedZoom);
    WriteCameraField(cameraBehavior, kCurrentZoomBOffset, appliedZoom);

    if (!gFloorClampActive.exchange(
            true,
            std::memory_order_relaxed)) {
        RuntimeLog(
            "floor constraint active current=%.3f applied=%.3f "
            "measured=%.3f desired=%.3f cameraY=%.3f probeY=%.3f "
            "floorY=%.3f "
            "offset=%.2f probes=%zu holdFrames=%zu releaseSpeed=%.2f",
            currentZoom,
            appliedZoom,
            finalZoom,
            desiredZoom,
            lastCameraY,
            lastFloorProbeY,
            lastFloorY,
            safetyOffset,
            probes,
            kFloorClearFramesBeforeRelease,
            kFloorReleaseSpeedPerSecond);
    }
}

void ResetPitchState() {
    std::lock_guard lock(gPitchStateMutex);
    gLastCameraBehavior = nullptr;
    gPitchInitialized = false;
    // A camera-mode change must not leave a swipe still turning: the frame
    // update that would have wound it down does not run outside RPG mode.
    gWheelRotateTravel.store(0.0f, std::memory_order_relaxed);
    // Ownership of the heading ends with it. The mod must not carry a held
    // angle across a mode change and write it onto whatever camera the game
    // switched to.
    gLastYawTravelTime.store(0, std::memory_order_release);
    std::lock_guard yawLock(gYawStateMutex);
    gPendingYawTravel = 0.0f;
}

class GameCameraUpdateScope {
public:
    GameCameraUpdateScope(
        void* cameraSystem,
        void* worldView,
        const float deltaSeconds)
        : previousInside_(gInsideGameCameraUpdate),
          previousIntegrated_(gPitchIntegratedThisUpdate),
          previousDelta_(gGameDeltaSeconds),
          previousCameraBehavior_(gCameraBehaviorSeenThisUpdate),
          previousCameraSystem_(gCameraSystemForUpdate),
          previousWorldView_(gWorldViewForUpdate),
          previousAfterZoomApplied_(gAfterZoomAppliedThisUpdate),
          previousPreYawCamera_(gPreYawCameraSeenThisUpdate),
          previousPreYawHits_(gPreYawHitsThisUpdate),
          previousPreGateHits_(gPreGateHitsThisUpdate),
          previousPreGateCamera_(gPreGateCamera),
          previousPreGateDeltaBits_(gPreGateDeltaBits),
          previousPreGateFlagByte_(gPreGateFlagByte),
          previousPreGateSeen_(gPreGateSeen),
          previousInjection_(gInjection) {
        gInsideGameCameraUpdate = true;
        gPitchIntegratedThisUpdate = false;
        gGameDeltaSeconds = deltaSeconds;
        gCameraBehaviorSeenThisUpdate = nullptr;
        gCameraSystemForUpdate = cameraSystem;
        gWorldViewForUpdate = worldView;
        gAfterZoomAppliedThisUpdate = false;
        gPreYawCameraSeenThisUpdate = nullptr;
        gPreYawHitsThisUpdate = 0;
        gPreGateHitsThisUpdate = 0;
        gPreGateCamera = nullptr;
        gPreGateDeltaBits = 0;
        gPreGateFlagByte = 0;
        gPreGateSeen = false;
        gInjection = NativeInjection{};
    }

    ~GameCameraUpdateScope() {
        gInsideGameCameraUpdate = previousInside_;
        gPitchIntegratedThisUpdate = previousIntegrated_;
        gGameDeltaSeconds = previousDelta_;
        gCameraBehaviorSeenThisUpdate = previousCameraBehavior_;
        gCameraSystemForUpdate = previousCameraSystem_;
        gWorldViewForUpdate = previousWorldView_;
        gAfterZoomAppliedThisUpdate = previousAfterZoomApplied_;
        gPreYawCameraSeenThisUpdate = previousPreYawCamera_;
        gPreYawHitsThisUpdate = previousPreYawHits_;
        gPreGateHitsThisUpdate = previousPreGateHits_;
        gPreGateCamera = previousPreGateCamera_;
        gPreGateDeltaBits = previousPreGateDeltaBits_;
        gPreGateFlagByte = previousPreGateFlagByte_;
        gPreGateSeen = previousPreGateSeen_;
        gInjection = previousInjection_;
    }

    GameCameraUpdateScope(const GameCameraUpdateScope&) = delete;
    GameCameraUpdateScope& operator=(const GameCameraUpdateScope&) = delete;

private:
    bool previousInside_;
    bool previousIntegrated_;
    float previousDelta_;
    void* previousCameraBehavior_;
    void* previousCameraSystem_;
    void* previousWorldView_;
    bool previousAfterZoomApplied_;
    void* previousPreYawCamera_;
    unsigned previousPreYawHits_;
    unsigned previousPreGateHits_;
    void* previousPreGateCamera_;
    std::uint32_t previousPreGateDeltaBits_;
    std::uint8_t previousPreGateFlagByte_;
    bool previousPreGateSeen_;
    NativeInjection previousInjection_;
};

// Holds every pitch approach rate at a within-frame value for as long as it
// is in scope and restores the previous values on the way out, including if
// the game's update unwinds. The rate itself is derived per frame rather than
// pinned to a sentinel, so the field keeps a physically meaningful magnitude.
class ScopedApproachRate {
public:
    ScopedApproachRate(void* definitionsBase, const float rate)
        : definitionsBase_(definitionsBase) {
        std::size_t slot = 0;
        for (const std::size_t definitionOffset : kCameraDefinitionOffsets) {
            for (const std::size_t rateOffset : kPitchApproachRateOffsets) {
                saved_[slot++] = ReadCameraField<float>(
                    definitionsBase_,
                    definitionOffset + rateOffset);
                WriteCameraField(
                    definitionsBase_,
                    definitionOffset + rateOffset,
                    rate);
            }
        }
    }

    ~ScopedApproachRate() {
        std::size_t slot = 0;
        for (const std::size_t definitionOffset : kCameraDefinitionOffsets) {
            for (const std::size_t rateOffset : kPitchApproachRateOffsets) {
                WriteCameraField(
                    definitionsBase_,
                    definitionOffset + rateOffset,
                    saved_[slot++]);
            }
        }
    }

    ScopedApproachRate(const ScopedApproachRate&) = delete;
    ScopedApproachRate& operator=(const ScopedApproachRate&) = delete;

private:
    void* definitionsBase_;
    std::array<
        float,
        kCameraDefinitionOffsets.size() *
            kPitchApproachRateOffsets.size()> saved_{};
};


// The follow-rate pair has to read like an authored pair of angular rates
// before anything is written to it, for the same reason the arm bounds do: a
// wrong offset must disable the override rather than write somewhere unknown.
bool YawFollowRatesLookAuthored(void* definitionsBase) {
    for (const std::size_t definitionOffset : kCameraDefinitionOffsets) {
        const float curve = ReadCameraField<float>(
            definitionsBase,
            definitionOffset + kYawFollowCurveOffset);
        if (!std::isfinite(curve) || curve < 0.0f || curve > 64.0f) {
            return false;
        }

        float previous = -1.0f;
        for (const std::size_t rateOffset : kYawFollowRateOffsets) {
            const float rate = ReadCameraField<float>(
                definitionsBase,
                definitionOffset + rateOffset);
            if (!std::isfinite(rate) ||
                rate < 0.0f ||
                rate > kMaximumPlausibleYawFollowRate ||
                rate < previous) {
                return false;
            }

            previous = rate;
        }
    }

    return true;
}


// Puts the pointer channel back exactly as it was found.
//
// Declared after GameCameraUpdateScope at the call site so that it is
// destroyed first: the fields have to be restored while the thread-local
// record of what to restore is still alive, and that has to hold on an
// unwind as well as on a normal return.
//
// The restore cannot happen any earlier. Both fields are read a dozen more
// times further down UpdateGameCameraBehavior after the channel is consumed,
// so putting them back mid-update would show those reads something the
// vanilla game would never have shown them.
class NativeInjectionRestoreScope {
public:
    NativeInjectionRestoreScope() = default;

    ~NativeInjectionRestoreScope() {
        if (!gInjection.active || gInjection.restored ||
            gInjection.camera == nullptr) {
            return;
        }

        WriteCameraField(
            gInjection.camera,
            kMouseRotationDeltaOffset,
            gInjection.previousDeltaBits);
        WriteCameraField(
            gInjection.camera,
            kMouseRotationFlagByteOffset,
            gInjection.previousFlagByte);
        gInjection.restored = true;

        // Checked bit for bit, because "restored" is the one claim this mod
        // makes about the game's own state surviving a frame.
        //
        // The fence is not decoration. Without it the compiler sees a memcpy
        // to an address followed by a memcpy back from the same address,
        // concludes the comparison below can only be equal, and deletes the
        // check together with the string it logs - which is exactly what the
        // first build of this did, and the missing literal in the binary is
        // how it was caught. A signal fence costs no instruction and stops
        // the fold.
        std::atomic_signal_fence(std::memory_order_seq_cst);
        const auto deltaBits = ReadCameraField<std::uint32_t>(
            gInjection.camera, kMouseRotationDeltaOffset);
        const auto flagByte = ReadCameraField<std::uint8_t>(
            gInjection.camera, kMouseRotationFlagByteOffset);
        if (deltaBits != gInjection.previousDeltaBits ||
            flagByte != gInjection.previousFlagByte) {
            gRestoreMismatch.fetch_add(1, std::memory_order_relaxed);
            RuntimeLog(
                "x-restore MISMATCH camera=%p wantDelta=0x%08x gotDelta=0x%08x "
                "wantFlag=0x%02x gotFlag=0x%02x",
                gInjection.camera,
                gInjection.previousDeltaBits,
                deltaBits,
                static_cast<unsigned>(gInjection.previousFlagByte),
                static_cast<unsigned>(flagByte));
        }
    }

    NativeInjectionRestoreScope(const NativeInjectionRestoreScope&) = delete;
    NativeInjectionRestoreScope& operator=(
        const NativeInjectionRestoreScope&) = delete;
};

// Drops everything the feeder has banked.
//
// Used where the travel is deliberately abandoned rather than deferred: a
// frame the native mouse owns, a mode reset, an uninstall. Deferring instead
// is what would turn an overlapping gesture into rotation that arrives after
// the player has stopped touching anything.
void DiscardWheelTravel() {
    gWheelRotateTravel.store(0.0f, std::memory_order_relaxed);
    gLastWheelTravelTime.store(0, std::memory_order_relaxed);
    std::lock_guard lock(gYawStateMutex);
    gPendingYawTravel = 0.0f;
}

// Raw-trackpad transport state. The scroll monitor in RawTrackpadMonitor.mm
// calls RawTrackpadScroll (defined below, outside this anonymous namespace so
// RawTrackpadMonitor.mm can link to it) on the main thread for every
// precise-scroll event. That function scales by BG3's own x0.1, runs the
// gesture lifecycle and the axis lock, and only then feeds the same
// accumulator / timestamp branch C uses - the drain, pre-gate, cap and
// smoothing downstream are untouched. The calibration base is the value
// proven in the 2.6.6 build (see kTrackpadYawBaseCalibration).
std::atomic<bool> gRawTrackpadMonitorActive{false};

// BG3's measured conversion from NSEvent.scrollingDeltaX to a 107/108 action
// value: dx 2.0 -> 0.2, dx 12.0 -> 1.2, dx 1.0 -> 0.1, dx 5.0 -> 0.5.
constexpr double kRawScrollToActionScale = 0.1;

// Axis lock. Decided once per gesture from the cumulative absolute travel, not
// per sample - a per-sample x>y test flips mid-gesture on a diagonal.
constexpr double kRawAxisDecideThreshold = 4.0;   // pre-scale cumulative abs
constexpr double kRawAxisDominanceRatio = 1.5;
enum class RawAxisLock { Undecided, Horizontal, Vertical };
RawAxisLock gRawAxisLock = RawAxisLock::Undecided;
double gRawCumulativeAbsX = 0.0;
double gRawCumulativeAbsY = 0.0;
double gRawBankedX = 0.0;  // signed, pre-scale, held while Undecided

// Thread-safe mirrors of the gesture state above. The state itself is written
// only on the main thread (RawTrackpadScroll); the pointer 104/105 branch
// reads these from the game's input thread to tell a precise trackpad gesture
// from a physical mouse wheel and to suppress vertical leakage while a
// horizontal/ambiguous swipe is in progress.
std::atomic<bool> gRawGestureActive{false};
std::atomic<int> gRawAxisLockPublished{
    static_cast<int>(RawAxisLock::Undecided)};

void PublishRawGesture(bool active, RawAxisLock axis) {
    gRawGestureActive.store(active, std::memory_order_release);
    gRawAxisLockPublished.store(
        static_cast<int>(axis), std::memory_order_release);
}

bool RawTrackpadGestureActive() {
    return gRawGestureActive.load(std::memory_order_acquire);
}

RawAxisLock RawTrackpadAxisLock() {
    return static_cast<RawAxisLock>(
        gRawAxisLockPublished.load(std::memory_order_acquire));
}

// When the monitor last saw any precise-scroll event - a real sample, the
// end-of-gesture event, or a macOS inertia (momentum) event. For a second or
// two after a fast flick the OS keeps sending decaying scroll events and BG3
// turns them into 104/105 the same as a real swipe, which is what kept a
// sharp vertical flick pitching after release. This timestamp lets the
// pointer 104/105 branch neutralize that whole tail - phase-agnostic, so it
// works whether or not BG3 preserves the momentum-phase bits - while a
// physical mouse wheel (which the monitor never sees) still reaches native
// zoom because no precise event was recent.
constexpr double kRawTrackpadTailSeconds = 1.5;
std::atomic<std::uint64_t> gRawLastPreciseTick{0};

bool RawTrackpadInTail() {
    const std::uint64_t tick =
        gRawLastPreciseTick.load(std::memory_order_acquire);
    return tick != 0 &&
        SecondsBetween(tick, mach_continuous_time()) <
            kRawTrackpadTailSeconds;
}

// NSEventPhase / NSEventMomentumPhase bits (option sets). Mirrored here so
// CameraHooks.cpp needs no AppKit include.
constexpr unsigned long kRawPhaseBegan = 1UL << 0;      // 0x01
constexpr unsigned long kRawPhaseEnded = 1UL << 3;      // 0x08
constexpr unsigned long kRawPhaseCancelled = 1UL << 4;  // 0x10

std::atomic<unsigned long> gRawTrackpadFedEvents{0};
std::atomic<float> gRawTrackpadSignedTravel{0.0f};
std::atomic<float> gRawTrackpadAbsTravel{0.0f};
std::atomic<unsigned long> gRawTrackpadMomentumEvents{0};
std::atomic<unsigned long> gRawTrackpadGestureEnds{0};
std::atomic<float> gRawTrackpadDiscardedPending{0.0f};

// Bounded logging: precise scroll can exceed a hundred events per second, so
// the same burst budget the diagnostic build used - the first
// kRawTrackpadBurstSize events after any event-free gap of at least
// kRawTrackpadRearmGapSeconds, at most kRawTrackpadMaxBursts bursts.
constexpr int kRawTrackpadBurstSize = 64;
constexpr int kRawTrackpadMaxBursts = 64;
constexpr double kRawTrackpadRearmGapSeconds = 3.0;
std::atomic<std::uint64_t> gRawTrackpadLastEventTick{0};
std::atomic<int> gRawTrackpadBurstRemaining{0};
std::atomic<unsigned> gRawTrackpadBurstCount{0};

// Mouse middle-drag vertical look. Its own accumulator, drained in the pitch
// getter and scaled only by MouseVerticalSensitivity - never mixed with the
// trackpad's gPitchTravel or the gamepad stick. Fed from NSEvent
// OtherMouseDragged (buttonNumber == 2) on the main thread, cleared on
// OtherMouseUp. Native BG3 98/109/110 still own the mouse's yaw; only Y is
// taken here, and diagonal middle-drag drives both at once.
std::atomic<float> gMousePitchTravel{0.0f};
std::atomic<unsigned long> gMousePitchEvents{0};
std::atomic<float> gMousePitchSignedTravel{0.0f};
std::atomic<std::uint64_t> gMousePitchLastEventTick{0};
std::atomic<int> gMousePitchBurstRemaining{0};
std::atomic<unsigned> gMousePitchBurstCount{0};

// The arm bounds have to read the same to every part of the game, not just to
// the update. The zoom step the input handler computes is derived from the
// same range the update clamps against, so applying the widened bounds only
// around the update made the game size each step against one range and clamp
// it against another - the zoom then advanced unevenly instead of gliding.
//
// So the authored values are captured once, the configured ones are written
// for as long as the mod is active, and the originals go back when the hooks
// come out. Nothing is left changed after unload.
std::array<
    float,
    kCameraDefinitionOffsets.size() * kZoomBoundsFields.size() * 2>
    gAuthoredZoomBounds{};
bool gZoomBoundsCaptured = false;

void CaptureZoomBounds(void* definitionsBase) {
    std::size_t slot = 0;
    for (const std::size_t definitionOffset : kCameraDefinitionOffsets) {
        for (const ZoomBoundsField& field : kZoomBoundsFields) {
            gAuthoredZoomBounds[slot++] = ReadCameraField<float>(
                definitionsBase, definitionOffset + field.maximumOffset);
            gAuthoredZoomBounds[slot++] = ReadCameraField<float>(
                definitionsBase, definitionOffset + field.minimumOffset);
        }
    }

    gZoomBoundsCaptured = true;
}

// Idempotent, and cheap enough to repeat every update so that a definition
// the game re-authors on a zone change is brought back into line.
void ApplyZoomBounds(void* definitionsBase, const CameraConfig& config) {
    for (const std::size_t definitionOffset : kCameraDefinitionOffsets) {
        for (const ZoomBoundsField& field : kZoomBoundsFields) {
            if (config.maximumZoomDistance != 0.0f) {
                WriteCameraField(
                    definitionsBase,
                    definitionOffset + field.maximumOffset,
                    std::max(
                        config.maximumZoomDistance,
                        config.minimumZoomDistance));
            }
            if (config.minimumZoomDistance != 0.0f) {
                WriteCameraField(
                    definitionsBase,
                    definitionOffset + field.minimumOffset,
                    config.minimumZoomDistance);
            }
        }
    }
}

void RestoreZoomBounds(void* definitionsBase) {
    if (!gZoomBoundsCaptured || definitionsBase == nullptr) {
        return;
    }

    std::size_t slot = 0;
    for (const std::size_t definitionOffset : kCameraDefinitionOffsets) {
        for (const ZoomBoundsField& field : kZoomBoundsFields) {
            WriteCameraField(
                definitionsBase,
                definitionOffset + field.maximumOffset,
                gAuthoredZoomBounds[slot++]);
            WriteCameraField(
                definitionsBase,
                definitionOffset + field.minimumOffset,
                gAuthoredZoomBounds[slot++]);
        }
    }

    gZoomBoundsCaptured = false;
}

bool ZoomBoundsLookAuthored(void* definitionsBase) {
    for (const std::size_t definitionOffset : kCameraDefinitionOffsets) {
        for (const ZoomBoundsField& field : kZoomBoundsFields) {
            const float maximum = ReadCameraField<float>(
                definitionsBase, definitionOffset + field.maximumOffset);
            const float minimum = ReadCameraField<float>(
                definitionsBase, definitionOffset + field.minimumOffset);

            if (!std::isfinite(maximum) || !std::isfinite(minimum) ||
                minimum < kMinimumPlausibleZoomBound ||
                maximum > kMaximumPlausibleZoomBound ||
                minimum >= maximum) {
                return false;
            }
        }
    }

    return true;
}

std::int64_t SignExtend(const std::uint64_t value, const unsigned bits) {
    const std::uint64_t signBit = std::uint64_t{1} << (bits - 1);
    return static_cast<std::int64_t>((value ^ signBit) - signBit);
}

bool DecodeCameraDefinitionsBasePointer(
    std::uint8_t* pitchDefinitionSite,
    FILE* log,
    void*** decodedPointer) {
    if (pitchDefinitionSite == nullptr ||
        log == nullptr ||
        decodedPointer == nullptr) {
        return false;
    }

    std::uint32_t adrpInstruction{};
    std::uint32_t ldrInstruction{};
    std::memcpy(
        &adrpInstruction,
        pitchDefinitionSite,
        sizeof(adrpInstruction));
    std::memcpy(
        &ldrInstruction,
        pitchDefinitionSite + 4,
        sizeof(ldrInstruction));

    if ((adrpInstruction & 0x9f000000u) != 0x90000000u ||
        (ldrInstruction & 0xffc00000u) != 0xf9400000u) {
        std::fprintf(
            log,
            "[ABORT] CameraDefinition site is not expected ADRP+LDR\n");
        return false;
    }

    const std::uint64_t immLo = (adrpInstruction >> 29) & 0x3u;
    const std::uint64_t immHi = (adrpInstruction >> 5) & 0x7ffffu;
    const std::int64_t pageDelta =
        SignExtend((immHi << 2) | immLo, 21) << 12;
    const std::uintptr_t instructionAddress =
        reinterpret_cast<std::uintptr_t>(pitchDefinitionSite);
    const std::uintptr_t pageAddress = instructionAddress & ~0xfffull;
    const std::size_t ldrOffset =
        static_cast<std::size_t>((ldrInstruction >> 10) & 0xfffu) << 3;

    *decodedPointer = reinterpret_cast<void**>(
        static_cast<std::uintptr_t>(
            static_cast<std::int64_t>(pageAddress) + pageDelta) +
        ldrOffset);

    return true;
}

bool ResolveCameraDefinitionsBasePointer(
    std::uint8_t* pitchDefinitionSite,
    FILE* log) {
    void** decodedPointer = nullptr;
    if (!DecodeCameraDefinitionsBasePointer(
            pitchDefinitionSite,
            log,
            &decodedPointer)) {
        return false;
    }

    gCameraDefinitionsBasePointer = decodedPointer;

    void* definitionsBase{};
    std::memcpy(
        &definitionsBase,
        gCameraDefinitionsBasePointer,
        sizeof(definitionsBase));
    if (definitionsBase == nullptr) {
        std::fprintf(
            log,
            "[DEFER] CameraDefinition pointer=%p resolved; base is not "
            "initialized yet\n",
            static_cast<void*>(gCameraDefinitionsBasePointer));
        return true;
    }

    for (const std::size_t definitionOffset : kCameraDefinitionOffsets) {
        for (const std::size_t rateOffset : kPitchApproachRateOffsets) {
            const float rate = ReadCameraField<float>(
                definitionsBase,
                definitionOffset + rateOffset);
            if (!std::isfinite(rate) ||
                rate < 0.0f ||
                rate > kMaximumApproachRate) {
                std::fprintf(
                    log,
                    "[ABORT] implausible CameraDefinition approach rate "
                    "definition=0x%zx field=0x%zx value=%f\n",
                    definitionOffset,
                    rateOffset,
                    rate);
                gCameraDefinitionsBasePointer = nullptr;
                return false;
            }
        }
    }

    std::fprintf(
        log,
        "[INFO] CameraDefinition base pointer=%p base=%p\n",
        static_cast<void*>(gCameraDefinitionsBasePointer),
        definitionsBase);
    return true;
}

bool IsL3Pressed(const void* mfiInputDevice) {
    std::uint32_t pressed{};
    const auto* bytes =
        static_cast<const std::uint8_t*>(mfiInputDevice);
    std::memcpy(&pressed, bytes + kL3PressedOffset, sizeof(pressed));
    return pressed != 0;
}

bool IsRecentMfiL3Pressed() {
    const std::uint64_t sampledAt =
        gLastMfiSampleTime.load(std::memory_order_acquire);
    void* inputDevice =
        gLastMfiInputDevice.load(std::memory_order_acquire);

    if (sampledAt == 0 || inputDevice == nullptr) {
        return false;
    }

    const double age = SecondsBetween(
        sampledAt,
        mach_continuous_time());
    if (age < 0.0 || age > kMaximumRecentMfiSampleSeconds) {
        return false;
    }

    // UpdateControls stores the new L3 state at +0xbc before injecting the
    // ToggleInputMode action. The device pointer comes from the preceding
    // OnStickChanged frame and is accepted only while it is recent.
    return IsL3Pressed(inputDevice);
}

void ToggleTrackedCameraMode(const char* reason) {
    const bool wasRpg =
        gRpgCameraModeActive.load(std::memory_order_relaxed);
    gRpgCameraModeActive.store(!wasRpg, std::memory_order_release);
    gRightStickY.store(0.0f, std::memory_order_relaxed);
    gCollisionClampActive.store(false, std::memory_order_relaxed);
    ResetFloorConstraint();
    ResetPitchState();

    RuntimeLog(
        "camera mode %s -> %s (%s)",
        wasRpg ? "RPG" : "point-click",
        wasRpg ? "point-click" : "RPG",
        reason);
}

void MarkDeferredToggleUsedForZoom() {
    bool firstZoomSample = false;
    {
        std::lock_guard lock(gDeferredToggleMutex);
        if (gDeferredToggle.pending &&
            !gDeferredToggle.usedForZoom) {
            gDeferredToggle.usedForZoom = true;
            firstZoomSample = true;
        }
    }

    if (firstZoomSample) {
        RuntimeLog("L3+camera Zoom action recognized as zoom chord");
    }
}

bool IsGamepadToggleRelease(const std::uint8_t* descriptor) {
    const void* definition{};
    std::memcpy(&definition, descriptor, sizeof(definition));
    if (definition == nullptr) {
        return false;
    }

    std::uint32_t eventType{};
    std::uint8_t phase{};
    std::uint16_t device{};
    std::memcpy(&eventType, definition, sizeof(eventType));
    std::memcpy(
        &phase,
        descriptor + kFireEventPhaseOffset,
        sizeof(phase));
    std::memcpy(
        &device,
        descriptor + kFireEventDeviceOffset,
        sizeof(device));

    return eventType == kToggleInputModeEvent &&
        phase == kInputPhaseRelease &&
        device == kGamepadDevice;
}

bool ConsumeZoomToggleSuppression() {
    std::lock_guard lock(gDeferredToggleMutex);
    if (!gDeferredToggle.pending || !gDeferredToggle.usedForZoom) {
        return false;
    }

    gDeferredToggle = {};
    return true;
}

void FireInputEventsHook(
    void* output,
    void* inputManager,
    const FireEventSpan* events,
    const bool dispatchFlag) {
    if (!gHooksEnabled.load(std::memory_order_acquire) ||
        events == nullptr ||
        events->begin == nullptr ||
        events->end == nullptr) {
        gOriginalFireInputEvents(
            output,
            inputManager,
            events,
            dispatchFlag);
        return;
    }

    const std::uintptr_t begin =
        reinterpret_cast<std::uintptr_t>(events->begin);
    const std::uintptr_t end =
        reinterpret_cast<std::uintptr_t>(events->end);
    if (end <= begin || (end - begin) % kFireEventDescSize != 0) {
        gOriginalFireInputEvents(
            output,
            inputManager,
            events,
            dispatchFlag);
        return;
    }

    const std::size_t eventCount = (end - begin) / kFireEventDescSize;
    if (eventCount > kMaximumFilteredFireEvents) {
        gOriginalFireInputEvents(
            output,
            inputManager,
            events,
            dispatchFlag);
        return;
    }

    // Record the device for this batch so the camera hook downstream can
    // tell which kind of input it is reacting to.
    {
        std::uint16_t batchDevice = kUnknownDevice;
        bool consistent = true;
        for (std::size_t index = 0; index < eventCount; ++index) {
            const auto* descriptor =
                reinterpret_cast<const std::uint8_t*>(begin) +
                index * kFireEventDescSize;
            std::uint16_t device{};
            std::memcpy(
                &device,
                descriptor + kFireEventDeviceOffset,
                sizeof(device));
            if (index == 0) {
                batchDevice = device;
            } else if (device != batchDevice) {
                consistent = false;
                break;
            }
        }
        gDispatchDevice = consistent ? batchDevice : kUnknownDevice;
    }

    // Observational pass. Reads the descriptors and logs; nothing below
    // depends on it, and the verbose check skips the loop entirely when the
    // log is off.
    if (GetCameraConfig().verboseLogging) {
        for (std::size_t index = 0; index < eventCount; ++index) {
            const auto* descriptor =
                events->begin + index * kFireEventDescSize;
            const void* definition{};
            std::memcpy(&definition, descriptor, sizeof(definition));
            if (definition == nullptr) {
                continue;
            }

            std::uint32_t actionType{};
            std::uint8_t phase{};
            std::uint16_t device{};
            std::memcpy(&actionType, definition, sizeof(actionType));
            std::memcpy(
                &phase,
                descriptor + kFireEventPhaseOffset,
                sizeof(phase));
            std::memcpy(
                &device,
                descriptor + kFireEventDeviceOffset,
                sizeof(device));
            NoteDispatchAction(device, actionType, phase);

            // Read-only diagnostic (2.6.5-diag1): per-occurrence trace of the
            // button/gate actions that could open the native mouse-rotation
            // channel. Only the descriptor fields already proven here are
            // read; nothing is written or dispatched.
            if ((actionType == 1u || actionType == 3u ||
                 actionType == 15u || actionType == 98u) &&
                gButtonActionTraceBudget.fetch_sub(
                    1, std::memory_order_relaxed) > 0) {
                RuntimeLog(
                    "fire gate action=%u device=%u phase=%u",
                    actionType,
                    static_cast<unsigned>(device),
                    static_cast<unsigned>(phase));
            }
        }
    }

    bool containsToggleRelease = false;
    for (std::size_t index = 0; index < eventCount; ++index) {
        const auto* descriptor =
            events->begin + index * kFireEventDescSize;
        if (IsGamepadToggleRelease(descriptor)) {
            containsToggleRelease = true;
            break;
        }
    }

    if (!containsToggleRelease || !ConsumeZoomToggleSuppression()) {
        gOriginalFireInputEvents(
            output,
            inputManager,
            events,
            dispatchFlag);
        return;
    }

    alignas(16) std::array<
        std::uint8_t,
        kMaximumFilteredFireEvents * kFireEventDescSize> filteredStorage{};
    std::size_t keptCount = 0;
    for (std::size_t index = 0; index < eventCount; ++index) {
        const auto* descriptor =
            events->begin + index * kFireEventDescSize;
        if (IsGamepadToggleRelease(descriptor)) {
            continue;
        }

        std::memcpy(
            filteredStorage.data() + keptCount * kFireEventDescSize,
            descriptor,
            kFireEventDescSize);
        ++keptCount;
    }

    const FireEventSpan filteredEvents{
        filteredStorage.data(),
        filteredStorage.data() + keptCount * kFireEventDescSize,
    };
    RuntimeLog(
        "ToggleInputMode 0xc0 release suppressed after L3+zoom "
        "events=%zu kept=%zu",
        eventCount,
        keptCount);
    gOriginalFireInputEvents(
        output,
        inputManager,
        &filteredEvents,
        dispatchFlag);
}

std::uint64_t InputEventHook(void* self, const void* inputEvent) {
    if (!gHooksEnabled.load(std::memory_order_acquire)) {
        return gOriginalInputEvent(self, inputEvent);
    }

    std::uint32_t eventType{};
    const auto* bytes = static_cast<const std::uint8_t*>(inputEvent);

    std::memcpy(&eventType, bytes, sizeof(eventType));

    // The pointer zoom modifier. Its state is tracked and the event is then
    // passed through untouched: it is somebody's real binding, and swallowing
    // it would break whatever it normally does.
    {
        const int configuredModifier =
            GetCameraConfig().pointerZoomModifierEvent;
        std::uint8_t modifierPressed{};
        std::memcpy(
            &modifierPressed,
            bytes + kInputEventPressedOffset,
            sizeof(modifierPressed));

        if (configuredModifier != 0 &&
            eventType == static_cast<std::uint32_t>(configuredModifier)) {
            if (GetCameraConfig().pointerZoomModifierToggle) {
                // Press-only button: there is no release to end the zoom on,
                // so each press flips the axis instead. Holding would latch
                // on and never clear.
                if (modifierPressed != 0) {
                    const bool active = !gPointerZoomModifierHeld.load(
                        std::memory_order_acquire);
                    gPointerZoomModifierHeld.store(
                        active, std::memory_order_release);
                    RuntimeLog(
                        "pointer zoom modifier toggled %s",
                        active ? "on; vertical axis now zooms"
                               : "off; vertical axis back to pitch");
                }
            } else {
                const bool held = modifierPressed != 0;
                if (gPointerZoomModifierHeld.exchange(
                        held, std::memory_order_acq_rel) != held) {
                    RuntimeLog(
                        "pointer zoom modifier %s",
                        held ? "held; vertical axis now zooms"
                             : "released; vertical axis back to pitch");
                }
            }
        } else {
            NoteDiscoveredEvent(
                gDispatchDevice, eventType, modifierPressed);
        }
    }

    if (eventType == kL3ChordStartEvent) {
        if (IsRecentMfiL3Pressed()) {
            bool started = false;
            {
                std::lock_guard lock(gDeferredToggleMutex);
                // Some controller stacks may repeat the pressed event while
                // the button is held. Do not erase usedForZoom on a repeat.
                if (!gDeferredToggle.pending) {
                    gDeferredToggle.pending = true;
                    gDeferredToggle.usedForZoom = false;
                    started = true;
                }
            }

            if (started) {
                RuntimeLog("ToggleInputMode press captured while L3 is held");
            }
            return 0;
        }

        return gOriginalInputEvent(self, inputEvent);
    }

    if (eventType == kInputModeChangedEvent) {
        DeferredToggle deferred;
        {
            std::lock_guard lock(gDeferredToggleMutex);
            if (!gDeferredToggle.pending) {
                return gOriginalInputEvent(self, inputEvent);
            }

            deferred = gDeferredToggle;
            gDeferredToggle = {};
        }

        // 0xac is downstream of the low-level 0xc0 release. If 0xc0 was
        // suppressed, this branch is never reached. If it arrives, BG3
        // really changed mode and our tracked state must follow it.
        if (deferred.usedForZoom) {
            RuntimeLog("unexpected downstream 0xac after zoom chord suppressed");
            return 0;
        }

        const std::uint64_t result =
            gOriginalInputEvent(self, inputEvent);
        ToggleTrackedCameraMode("plain L3 ToggleInputMode event delivered");
        RuntimeLog(
            "plain L3 ToggleInputMode result=0x%llx",
            static_cast<unsigned long long>(result));
        return result;
    }

    return gOriginalInputEvent(self, inputEvent);
}

std::uint64_t CameraInputEventHook(
    void* self,
    const void* entityRef,
    const void* inputEvent) {
    if (!gHooksEnabled.load(std::memory_order_acquire)) {
        return gOriginalCameraInputEvent(self, entityRef, inputEvent);
    }

    std::uint32_t eventType{};
    std::memcpy(&eventType, inputEvent, sizeof(eventType));

    NoteCameraHandlerAction(gDispatchDevice, eventType);

    // Read-only diagnostic (2.6.5-diag1). Runs before the early return below
    // and does not change it. 98 = channel/forced-cursor gate; 109/110 =
    // native +0xa0 rotation arms. f14/f18 are raw InputEvent floats of
    // unestablished meaning (the game reads +0x14 for these arms and +0x18
    // for 104/105/107/108). Nothing is written back or re-dispatched. Burst
    // budget: see the note by kNativeArmBurstSize.
    {
        constexpr std::uint32_t kMouseChannelSelectEvent = 98;
        constexpr std::uint32_t kMouseRotateNegEvent = 109;
        constexpr std::uint32_t kMouseRotatePosEvent = 110;
        if ((eventType == kMouseChannelSelectEvent ||
             eventType == kMouseRotateNegEvent ||
             eventType == kMouseRotatePosEvent) &&
            GetCameraConfig().verboseLogging) {
            const std::uint64_t now = mach_continuous_time();
            const std::uint64_t prev = gNativeArmLastEventTick.exchange(
                now, std::memory_order_relaxed);
            const bool newBurst =
                prev == 0 ||
                SecondsBetween(prev, now) >= kNativeArmRearmGapSeconds;
            if (newBurst &&
                gNativeArmBurstCount.load(std::memory_order_relaxed) <
                    static_cast<unsigned>(kNativeArmMaxBursts)) {
                const unsigned burst = gNativeArmBurstCount.fetch_add(
                    1, std::memory_order_relaxed) + 1;
                gNativeArmBurstRemaining.store(
                    kNativeArmBurstSize, std::memory_order_relaxed);
                RuntimeLog(
                    "native mouse arm burst=%u (event-free gap >= %.0fs)",
                    burst,
                    kNativeArmRearmGapSeconds);
            }
            if (gNativeArmBurstRemaining.fetch_sub(
                    1, std::memory_order_relaxed) > 0) {
                const auto* raw =
                    static_cast<const std::uint8_t*>(inputEvent);
                std::uint32_t f14{};
                std::uint32_t f18{};
                std::uint8_t p1c{};
                std::memcpy(&f14, raw + 0x14, sizeof(f14));
                std::memcpy(&f18, raw + 0x18, sizeof(f18));
                std::memcpy(&p1c, raw + 0x1c, sizeof(p1c));
                float v14{};
                float v18{};
                std::memcpy(&v14, &f14, sizeof(v14));
                std::memcpy(&v18, &f18, sizeof(v18));
                RuntimeLog(
                    "native mouse arm burst=%u event=%u device=%u "
                    "f14=0x%08x v14=%.5f f18=0x%08x v18=%.5f pressed=%u",
                    gNativeArmBurstCount.load(std::memory_order_relaxed),
                    eventType,
                    static_cast<unsigned>(gDispatchDevice),
                    static_cast<unsigned>(f14),
                    static_cast<double>(v14),
                    static_cast<unsigned>(f18),
                    static_cast<double>(v18),
                    static_cast<unsigned>(p1c));
            }
        }
    }

    // Mouse horizontal sensitivity. BG3's native 109/110 stay the yaw source;
    // only the +0x14 float is scaled, and only for the pointer family. Action
    // 98 and the forced-cursor lifecycle are not touched. With the default
    // 1.0 the event is forwarded exactly as it arrived.
    if (eventType == kMouseNativeYawNegEvent ||
        eventType == kMouseNativeYawPosEvent) {
        const float mouseHorizontal =
            GetCameraConfig().mouseHorizontalSensitivity;
        if (gDispatchDevice == kPointerDevice && mouseHorizontal != 1.0f) {
            const auto scaled = CloneEventWithScaledNativeYaw(
                inputEvent, mouseHorizontal);
            if (GetCameraConfig().verboseLogging &&
                gMouseYawScaleTraceBudget.fetch_sub(
                    1, std::memory_order_relaxed) > 0) {
                float before{};
                float after{};
                std::memcpy(
                    &before,
                    static_cast<const std::uint8_t*>(inputEvent) +
                        kInputEventNativeYawOffset,
                    sizeof(before));
                std::memcpy(
                    &after,
                    scaled.data() + kInputEventNativeYawOffset,
                    sizeof(after));
                RuntimeLog(
                    "mouse-yaw-scale event=%u device=%u f14In=%.5f "
                    "f14Out=%.5f sens=%.3f",
                    eventType,
                    static_cast<unsigned>(gDispatchDevice),
                    static_cast<double>(before),
                    static_cast<double>(after),
                    static_cast<double>(mouseHorizontal));
            }
            return gOriginalCameraInputEvent(
                self, entityRef, scaled.data());
        }
        return gOriginalCameraInputEvent(self, entityRef, inputEvent);
    }

    if (eventType != kZoomInEvent &&
        eventType != kZoomOutEvent &&
        eventType != kRotateLeftEvent &&
        eventType != kRotateRightEvent) {
        return gOriginalCameraInputEvent(self, entityRef, inputEvent);
    }

    float inputValue{};
    std::uint8_t pressed{};
    const auto* bytes = static_cast<const std::uint8_t*>(inputEvent);
    std::memcpy(
        &inputValue,
        bytes + kInputEventValueOffset,
        sizeof(inputValue));
    std::memcpy(
        &pressed,
        bytes + kInputEventPressedOffset,
        sizeof(pressed));

    if (!std::isfinite(inputValue)) {
        inputValue = 0.0f;
    }

    // The vertical counterpart of the rotate line further down: inputValue
    // exactly as it arrived, before any deadzone, accumulation,
    // normalization or payload rewrite. Logged for every device rather than
    // for one assumed to be the trackpad, so the analysis filters instead of
    // the code deciding. Purely observational.
    if ((eventType == kZoomInEvent || eventType == kZoomOutEvent) &&
        GetCameraConfig().verboseLogging &&
        gPointerVerticalTraceBudget.fetch_sub(
            1, std::memory_order_relaxed) > 0) {
        RuntimeLog(
            "pointer vertical raw device=%u event=%u value=%.4f pressed=%u",
            static_cast<unsigned>(gDispatchDevice),
            eventType,
            static_cast<double>(inputValue),
            static_cast<unsigned>(pressed));
    }

    // Either modifier puts the vertical axis into zoom: L3 for a controller,
    // the configured event for a trackpad or mouse. Everything downstream is
    // identical, so the two paths differ only in what sets the flag.
    NoteCameraEventDevice(gDispatchDevice, eventType, inputValue);

    // A device nominated as the zoom device needs no modifier at all: the
    // same action means zoom when it comes from there and pitch everywhere
    // else. That is the whole point of knowing the device - one game action
    // can serve both jobs without a chord.
    const bool pointerSourced =
        gDispatchDevice != kUnknownDevice &&
        gDispatchDevice != kGamepadDevice;

    const int zoomDevice = GetCameraConfig().zoomDeviceId;
    const bool fromZoomDevice =
        zoomDevice >= 0 &&
        gDispatchDevice != kUnknownDevice &&
        gDispatchDevice == static_cast<std::uint16_t>(zoomDevice);

    const bool zoomChordHeld =
        fromZoomDevice ||
        gMfiL3Pressed.load(std::memory_order_acquire) ||
        gPointerZoomModifierHeld.load(std::memory_order_acquire) ||
        IsPointerHoldButtonDown();

    {
        const bool chord = zoomChordHeld;
        std::atomic<float>* smallest = nullptr;
        const char* stage = nullptr;
        switch (eventType) {
        case kZoomInEvent:
            smallest = chord ? &gSmallestZoomInChord : &gSmallestZoomInPitch;
            stage = chord ? "L3+stick ZoomIn 104" : "stick ZoomIn 104";
            break;
        case kZoomOutEvent:
            smallest = chord ? &gSmallestZoomOutChord : &gSmallestZoomOutPitch;
            stage = chord ? "L3+stick ZoomOut 105" : "stick ZoomOut 105";
            break;
        case kRotateLeftEvent:
            smallest = &gSmallestRotateLeft;
            stage = "RotateLeft 107";
            break;
        default:
            smallest = &gSmallestRotateRight;
            stage = "RotateRight 108";
            break;
        }

        NoteSmallestMagnitude(*smallest, stage, std::abs(inputValue));
    }

    if (eventType == kRotateLeftEvent ||
        eventType == kRotateRightEvent) {
        // Both axes have to cross the same gate. Left untouched, horizontal
        // kept the handler's own threshold while vertical already ran on the
        // configured deadzone, and the two axes felt unrelated.
        gLastRotateNormalized.store(
            NormalizeOutsideDeadzone(inputValue),
            std::memory_order_relaxed);
        const float rawMagnitude = std::abs(inputValue);

        // What the device actually sent, before any branch below looks at
        // it. The RuntimeLog prefix carries the timestamp. Purely
        // observational: nothing reads this, and the verbose check
        // short-circuits the budget so a quiet session never spends it.
        if (GetCameraConfig().verboseLogging &&
            gRotateTraceBudget.fetch_sub(1, std::memory_order_relaxed) > 0) {
            RuntimeLog(
                "rotate raw device=%u event=%u value=%.4f pressed=%u",
                static_cast<unsigned>(gDispatchDevice),
                eventType,
                static_cast<double>(inputValue),
                static_cast<unsigned>(pressed));
        }

        // Four devices, four named cases. "Not the gamepad" is not one of
        // them: the keyboard sends these same two actions at a magnitude of
        // exactly 1.0, and treating that as pointer input is what once made
        // Q/E three times too fast.

        // A. The controller. Unchanged, including the two lines above and
        //    below this comment - this is the regression baseline.
        if (gDispatchDevice == kGamepadDevice) {
            if (rawMagnitude > 0.0f) {
                gLastYawTravelTime.store(0, std::memory_order_release);
            }

            const auto rotationEvent = CloneEventWithValue(
                inputEvent,
                ScaleHorizontalStickInput(inputValue));
            return gOriginalCameraInputEvent(
                self,
                entityRef,
                rotationEvent.data());
        }

        // C. The trackpad. Direction lives in the action id, never in the
        //    sign of the payload: every one of the thousands of samples
        //    captured from this device was non-negative, and 107 turning one
        //    way against 108 turning the other was confirmed against the
        //    camera's own heading rate, 48 observations without a
        //    contradiction.
        if (gDispatchDevice == kPointerDevice &&
            GetCameraConfig().pointerRotationNative) {
            const bool carriesTravel =
                pressed != 0 && std::isfinite(inputValue) &&
                inputValue != 0.0f;
            if (carriesTravel) {
                // Prototype: horizontal travel now enters through the macOS
                // scroll monitor, ahead of this action. The 107/108 event is
                // still neutralized so the stick channel does not turn from
                // it, but it is no longer added to the accumulator - that
                // would double the X. 104/105 are not reached here and stay
                // byte-identical.
                if (RawTrackpadMonitorActive()) {
                    const auto neutralized =
                        CloneEventWithValue(inputEvent, 0.0f);
                    return gOriginalCameraInputEvent(
                        self, entityRef, neutralized.data());
                }
                const float signedTravel =
                    eventType == kRotateLeftEvent ? inputValue : -inputValue;
                AccumulateWheelTravel(signedTravel);
                gLastWheelTravelTime.store(
                    mach_continuous_time(), std::memory_order_release);
                gXFeedEvents.fetch_add(1, std::memory_order_relaxed);
                AtomicFloatAdd(gXFeedTravel, std::abs(signedTravel));

                if (GetCameraConfig().verboseLogging &&
                    gXFeedTraceBudget.fetch_sub(
                        1, std::memory_order_relaxed) > 0) {
                    RuntimeLog(
                        "x-feed event=%u device=%u signed=%.4f accumulated=%.4f",
                        eventType,
                        static_cast<unsigned>(gDispatchDevice),
                        static_cast<double>(signedTravel),
                        static_cast<double>(gWheelRotateTravel.load(
                            std::memory_order_relaxed)));
                }

                // The magnitude has been taken as travel, so the handler
                // receives the same event carrying none. Without this the
                // stick channel would turn the camera a second time from the
                // same sample.
                const auto neutralized =
                    CloneEventWithValue(inputEvent, 0.0f);
                return gOriginalCameraInputEvent(
                    self, entityRef, neutralized.data());
            }

            // A release. Forwarded untouched, and deliberately not allowed to
            // clear anything: the trackpad alternates a sample with a release
            // roughly one for one, so clearing here would throw away half of
            // every gesture.
            return gOriginalCameraInputEvent(self, entityRef, inputEvent);
        }

        // B. The keyboard, and D. anything unidentified. Both keep BG3's own
        //    handling untouched. Neither is allowed to disturb the
        //    accumulator, which belongs to the pointer.
        return gOriginalCameraInputEvent(self, entityRef, inputEvent);
    }

    if (!gRpgCameraModeActive.load(std::memory_order_acquire)) {
        gRightStickY.store(0.0f, std::memory_order_relaxed);
        return gOriginalCameraInputEvent(self, entityRef, inputEvent);
    }

    // 104/105 from the pointer family split three ways once the raw trackpad
    // monitor is live. device=1 alone cannot tell a mouse from a trackpad -
    // the gesture state can.
    if (gDispatchDevice == kPointerDevice && RawTrackpadMonitorActive()) {
        if (!RawTrackpadGestureActive()) {
            if (RawTrackpadInTail()) {
                // The inertia tail of a just-finished trackpad gesture (macOS
                // momentum, or BG3's own scroll decay): the fingers are gone.
                // Neutralize so it drives neither pitch nor zoom - the camera
                // stops when the fingers stop, exactly like the horizontal
                // path, where every post-gesture 107/108 is dead too.
                const auto neutralized = CloneEventWithValue(inputEvent, 0.0f);
                return gOriginalCameraInputEvent(
                    self, entityRef, neutralized.data());
            }
            // No precise gesture and none recently: a physical mouse wheel or
            // other non-precise scroll. Hand BG3 its own event untouched so
            // the wheel drives native zoom again.
            return gOriginalCameraInputEvent(self, entityRef, inputEvent);
        }
        const RawAxisLock axis = RawTrackpadAxisLock();
        if (axis != RawAxisLock::Vertical) {
            // A horizontal or still-undecided precise swipe: neutralize the
            // vertical action in the camera handler so it cannot leak into
            // pitch. The established vertical path is only taken once the
            // gesture has locked Vertical.
            const auto neutralized = CloneEventWithValue(inputEvent, 0.0f);
            return gOriginalCameraInputEvent(
                self, entityRef, neutralized.data());
        }
        // axis == Vertical: fall through to the existing trackpad pitch /
        // zoom-modifier path, byte-identical apart from the sensitivity split
        // applied later in the pitch getter.
    }

    // Work with the processed ZoomIn/ZoomOut payload at +0x18 rather than the
    // raw MFI stick sample, so the game's own controller response curve has
    // already been applied by the time it reaches here.
    if (zoomChordHeld) {
        gRightStickY.store(0.0f, std::memory_order_relaxed);

        // The chord has to be recognised from the same point the zoom starts
        // responding. Keyed off the game's gate instead, a gentle zoom would
        // not register as a chord and releasing L3 would flip camera mode.
        if (pressed != 0 &&
            NormalizeOutsideDeadzone(std::abs(inputValue)) > 0.0f) {
            MarkDeferredToggleUsedForZoom();
        }

        // A key has no travel, so it gets its own step size; a scroll notch
        // keeps the general zoom sensitivity; a stick keeps the analogue
        // ramp.
        const CameraConfig& zoomConfig = GetCameraConfig();
        float discreteStep = 0.0f;
        if (fromZoomDevice) {
            discreteStep = zoomConfig.zoomDeviceSensitivity;
        } else if (pointerSourced) {
            discreteStep = zoomConfig.zoomSensitivity;
        }

        const float zoomPayload =
            ScaleZoomStickInput(inputValue, discreteStep);
        RuntimeLog(
            "zoom chord device=%u pointer=%d in=%.4f payload=%.4f",
            static_cast<unsigned>(gDispatchDevice),
            pointerSourced ? 1 : 0,
            inputValue,
            zoomPayload);
        gLastZoomInput.store(std::abs(inputValue), std::memory_order_relaxed);
        gLastZoomPayload.store(zoomPayload, std::memory_order_relaxed);
        gLastZoomPressed.store(pressed, std::memory_order_relaxed);

        auto zoomEvent = CloneEventWithValue(inputEvent, zoomPayload);

        // Marking the axis active for any deflection past our own deadzone.
        // The magnitude still decides the speed; this only decides that the
        // action is considered to be happening at all.
        if (GetCameraConfig().zoomForceActive && zoomPayload != 0.0f) {
            const std::uint8_t active = 1;
            std::memcpy(
                zoomEvent.data() + kInputEventPressedOffset,
                &active,
                sizeof(active));
        }

        return gOriginalCameraInputEvent(
            self,
            entityRef,
            zoomEvent.data());
    }

    // The two zoom actions are the game's encoding of a single signed axis:
    // the event type carries the sign, the payload carries the magnitude.
    // Accumulated, not overwritten. Storing the latest sample made the pitch
    // of a gesture depend on how many of its samples a frame happened to
    // catch, which changes with the frame rate - and so changed the effective
    // sensitivity when the resolution did. Summing the travel and consuming
    // the total makes a gesture worth the same pitch at any frame rate.
    // The same rule that identifies a wheel on the rotation path: a key can
    // only report a full 1.0, so anything else marks an analogue pointer.
    // Applied here too, so the vertical axis does not have to wait for a
    // horizontal swipe before it recognises the device.
    {
        const float zoomMagnitude = std::abs(inputValue);
        if (pointerSourced &&
            zoomMagnitude > 0.0f &&
            zoomMagnitude != 1.0f &&
            gWheelRotateDevice.load(std::memory_order_relaxed) !=
                static_cast<int>(gDispatchDevice)) {
            gWheelRotateDevice.store(
                static_cast<int>(gDispatchDevice),
                std::memory_order_relaxed);
            RuntimeLog(
                "device %u identified as a wheel from zoom (magnitude %.4f)",
                static_cast<unsigned>(gDispatchDevice),
                static_cast<double>(zoomMagnitude));
        }
    }

    if (gWheelRotateDevice.load(std::memory_order_relaxed) ==
        static_cast<int>(gDispatchDevice)) {
        const float sample =
            ApplyAxialDeadzone(inputValue * AxisSignForZoomEvent(eventType));
        if (sample != 0.0f) {
            const float total =
                gPitchTravel.load(std::memory_order_relaxed) + sample;
            gPitchTravel.store(
                std::isfinite(total) ? total : 0.0f,
                std::memory_order_relaxed);
        }
    }
    gRightStickY.store(
        ApplyAxialDeadzone(inputValue * AxisSignForZoomEvent(eventType)),
        std::memory_order_relaxed);

    // The magnitude has been consumed as pitch, so the handler receives the
    // same event carrying none. Forwarding rather than swallowing keeps the
    // handler's own bookkeeping and any other listener intact.
    const auto neutralizedEvent = CloneEventWithValue(inputEvent, 0.0f);
    return gOriginalCameraInputEvent(
        self,
        entityRef,
        neutralizedEvent.data());
}


void UpdateGameCameraBehaviorHook(
    void* argument0,
    void* argument1,
    void* gameTime,
    void* argument3) {
    if (!gHooksEnabled.load(std::memory_order_acquire) ||
        !gRpgCameraModeActive.load(std::memory_order_acquire) ||
        gCameraDefinitionsBasePointer == nullptr) {
        gOriginalUpdateGameCameraBehavior(
            argument0,
            argument1,
            gameTime,
            argument3);
        return;
    }

    std::lock_guard lock(gCameraDefinitionMutex);

    void* definitionsBase{};
    std::memcpy(
        &definitionsBase,
        gCameraDefinitionsBasePointer,
        sizeof(definitionsBase));
    if (definitionsBase == nullptr) {
        gOriginalUpdateGameCameraBehavior(
            argument0,
            argument1,
            gameTime,
            argument3);
        return;
    }

    // The pitch this frame is our own value, returned from the pitch hook.
    // Left at their authored rates the definitions would approach it over
    // several frames and visibly lag the stick, so for the duration of the
    // update they carry a rate that lands within this frame.
    const CameraConfig& config = GetCameraConfig();
    if (gZoomBoundsUsable.load(std::memory_order_acquire) &&
        (config.minimumZoomDistance != 0.0f ||
         config.maximumZoomDistance != 0.0f)) {
        ApplyZoomBounds(definitionsBase, config);
    }

    const float deltaSeconds = ReadGameDeltaSeconds(gameTime);

    // Nothing is read from gLastCameraBehavior here any more. The pointer
    // channel is written from inside the update, at the pre-gate site, using
    // the object the update itself is about to read it from - a pointer from
    // the previous frame has no business being dereferenced at all.
    {
        ScopedApproachRate approachRate(
            definitionsBase,
            IntraFrameApproachRate(deltaSeconds));
        GameCameraUpdateScope updateScope(
            argument0,
            argument1,
            deltaSeconds);
        // Declared after the update scope so that its destructor runs first:
        // the fields have to go back before the thread-local state that
        // records what to put back is torn down, and that has to hold when
        // the game's update unwinds as well as when it returns.
        NativeInjectionRestoreScope restoreScope;
        gOriginalUpdateGameCameraBehavior(
            argument0,
            argument1,
            gameTime,
            argument3);
    }
}

void OnStickChangedHook(void* self, const int axis, const float value) {
    if (!gHooksEnabled.load(std::memory_order_acquire)) {
        gOriginalOnStickChanged(self, axis, value);
        return;
    }

    gLastMfiInputDevice.store(self, std::memory_order_relaxed);
    gLastMfiSampleTime.store(
        mach_continuous_time(),
        std::memory_order_release);

    NoteSmallestMagnitude(
        gSmallestRawStick,
        "raw MFI stick",
        std::abs(value));

    const bool l3Pressed = IsL3Pressed(self);
    gMfiL3Pressed.store(l3Pressed, std::memory_order_release);

    // Raw MFI data is no longer converted to pitch here. Always let it reach
    // InputManager; CameraInputEventHook handles the processed camera action.
    gOriginalOnStickChanged(self, axis, value);
}

float GetCameraPitchDegreesHook(
    void* cameraBehavior,
    const bool modeFlag) {
    const float vanillaPitch =
        CallOriginalPitch(cameraBehavior, modeFlag);

    if (!gHooksEnabled.load(std::memory_order_acquire)) {
        return vanillaPitch;
    }

    // The game calls this function with both modeFlag values in the same
    // update. Calls with modeFlag=true are unrelated to the pitch we replace.
    // They must not erase the stick sample needed by the later false call.
    if (modeFlag) {
        return vanillaPitch;
    }

    if (!gRpgCameraModeActive.load(std::memory_order_acquire)) {
        gRightStickY.store(0.0f, std::memory_order_relaxed);
        ResetPitchState();
        return vanillaPitch;
    }

    if (gInsideGameCameraUpdate) {
        gCameraBehaviorSeenThisUpdate = cameraBehavior;
    }

    std::lock_guard lock(gPitchStateMutex);

    if (!gPitchInitialized || !std::isfinite(gCurrentPitch)) {
        gLastCameraBehavior = cameraBehavior;
        gCurrentPitch = vanillaPitch;
        gPitchInitialized = true;
        RuntimeLog(
            "player pitch initialized camera=%p pitch=%.3f",
            cameraBehavior,
            gCurrentPitch);
        return gCurrentPitch;
    }

    // Pitch is tracked per player rather than per GameCameraBehavior: BG3 may
    // pass several of those objects through this function during a single
    // transition, so a changed pointer must not reinitialize the target.
    gLastCameraBehavior = cameraBehavior;

    // Travel is spent in full, with no frame-length term: the angle a gesture
    // is worth is fixed by how far the input moved, not by how many frames it
    // spanned. Two independent accumulators feed here - the trackpad's
    // precise-vertical swipe and the mouse's middle-drag Y - each drained
    // every call and each scaled by its own device sensitivity. They do not
    // mix: a value in one never picks up the other's multiplier.
    const CameraConfig& travelConfig = GetCameraConfig();
    const float trackpadTravel =
        gPitchTravel.exchange(0.0f, std::memory_order_relaxed);
    const float mouseTravel =
        gMousePitchTravel.exchange(0.0f, std::memory_order_relaxed);
    const float scaledTrackpadTravel =
        std::isfinite(trackpadTravel)
            ? trackpadTravel * travelConfig.trackpadVerticalSensitivity
            : 0.0f;
    const float scaledMouseTravel =
        std::isfinite(mouseTravel)
            ? mouseTravel * travelConfig.mouseVerticalSensitivity
            : 0.0f;
    const float combinedTravel = scaledTrackpadTravel + scaledMouseTravel;
    if (combinedTravel != 0.0f &&
        std::isfinite(combinedTravel) &&
        gInsideGameCameraUpdate &&
        !gPitchIntegratedThisUpdate) {
        gPitchIntegratedThisUpdate = true;
        gCurrentPitch += combinedTravel * kDegreesPerTravelUnit;
        gCurrentPitch = std::clamp(
            gCurrentPitch,
            travelConfig.pitchMinimumDegrees,
            travelConfig.pitchMaximumDegrees);
        RuntimeLog(
            "pitch travel trackpad=%.3f mouse=%.3f pitch=%.2f",
            static_cast<double>(scaledTrackpadTravel),
            static_cast<double>(scaledMouseTravel),
            static_cast<double>(gCurrentPitch));
        return gCurrentPitch;
    }

    // The controller's stick reports a deflection, not travel, so it keeps
    // the rate-times-frame-length integration that suits one.
    const float axis =
        gRightStickY.load(std::memory_order_relaxed);

    if (axis == 0.0f ||
        !gInsideGameCameraUpdate ||
        gPitchIntegratedThisUpdate ||
        gGameDeltaSeconds <= 0.0f) {
        // Keep the last target. The previous implementation copied the
        // lagging actual pitch back into the target whenever an axis sample
        // crossed zero; that discontinuity was the visible diagonal bounce.
        return gCurrentPitch;
    }

    gPitchIntegratedThisUpdate = true;

    float rotationSpeed = ReadCameraField<float>(
        cameraBehavior,
        kCameraRotationSpeedOffset);
    if (!std::isfinite(rotationSpeed) || rotationSpeed <= 0.0f) {
        rotationSpeed = kFallbackPitchSpeedDegreesPerSecond;
    }

    // axis is the deadzone-normalized stick magnitude on [-1, 1], and
    // rotationSpeed is the camera's own angular rate in degrees per second,
    // so the product integrates directly into degrees over the frame. The
    // stick is the gamepad, so it takes the gamepad vertical sensitivity -
    // never the trackpad's or the mouse's.
    const CameraConfig& config = GetCameraConfig();
    gCurrentPitch +=
        axis *
        rotationSpeed *
        config.gamepadVerticalSensitivity *
        gGameDeltaSeconds;
    gCurrentPitch = std::clamp(
        gCurrentPitch,
        config.pitchMinimumDegrees,
        config.pitchMaximumDegrees);

    // Deliberately the same shape as the wheel-rotation line, so the two axes
    // can be compared directly. The open question this settles is whether the
    // vertical axis is smooth because it is handled better, or because the
    // trackpad simply reports cleaner samples on it - macOS tunes vertical
    // scrolling far more than horizontal, and if that is the difference then
    // no amount of work on the rotation path can close it.
    RuntimeLog(
        "pitch frame axis=%.3f dt=%.1fms",
        static_cast<double>(axis),
        static_cast<double>(gGameDeltaSeconds) * 1000.0);

    return gCurrentPitch;
}

void LogHookError(
    FILE* log,
    const char* operation,
    const std::string& error) {
    std::fprintf(
        log,
        "[ABORT] %s: %s\n",
        operation,
        error.c_str());
    std::fflush(log);
}

}  // namespace

// Raw-trackpad transport entry points. External linkage so
// RawTrackpadMonitor.mm can call these; they still see the anonymous-namespace
// helpers above (same translation unit). Every function here runs only on the
// main thread (the NSEvent monitor), so the gesture-lifecycle state
// (gRawAxisLock, gRawCumulative*, gRawBankedX) needs no lock; only the shared
// accumulator it ultimately writes is cross-thread, and that already has its
// own mechanism.
void SetRawTrackpadMonitorActive(bool active) {
    gRawTrackpadMonitorActive.store(active, std::memory_order_release);
}

bool RawTrackpadMonitorActive() {
    return gRawTrackpadMonitorActive.load(std::memory_order_acquire);
}

namespace {

const char* RawAxisName(RawAxisLock axis) {
    switch (axis) {
    case RawAxisLock::Horizontal:
        return "H";
    case RawAxisLock::Vertical:
        return "V";
    default:
        return "undecided";
    }
}

// One bounded-burst telemetry line. note carries the lifecycle decision.
void RawTrackpadLog(
    double dx,
    double dy,
    double scaledX,
    unsigned long phase,
    unsigned long momentumPhase,
    const char* note) {
    if (!GetCameraConfig().verboseLogging) {
        return;
    }
    const std::uint64_t now = mach_continuous_time();
    const std::uint64_t prev =
        gRawTrackpadLastEventTick.exchange(now, std::memory_order_relaxed);
    const bool newBurst =
        prev == 0 ||
        SecondsBetween(prev, now) >= kRawTrackpadRearmGapSeconds;
    if (newBurst &&
        gRawTrackpadBurstCount.load(std::memory_order_relaxed) <
            static_cast<unsigned>(kRawTrackpadMaxBursts)) {
        const unsigned burst =
            gRawTrackpadBurstCount.fetch_add(1, std::memory_order_relaxed) + 1;
        gRawTrackpadBurstRemaining.store(
            kRawTrackpadBurstSize, std::memory_order_relaxed);
        RuntimeLog(
            "raw-trackpad burst=%u (event-free gap >= %.0fs)",
            burst,
            kRawTrackpadRearmGapSeconds);
    }
    if (gRawTrackpadBurstRemaining.fetch_sub(
            1, std::memory_order_relaxed) > 0) {
        RuntimeLog(
            "raw-trackpad burst=%u dx=%.4f dy=%.4f scaledX=%.4f phase=0x%lx "
            "momentum=0x%lx axis=%s note=%s",
            gRawTrackpadBurstCount.load(std::memory_order_relaxed),
            dx,
            dy,
            scaledX,
            phase,
            momentumPhase,
            RawAxisName(gRawAxisLock),
            note);
    }
}

// Push one already-scaled horizontal value into the same accumulator,
// timestamp and counters branch C uses.
void RawTrackpadPushScaledX(double scaledX) {
    if (!std::isfinite(scaledX) || scaledX == 0.0) {
        return;
    }
    const float signedTravel = static_cast<float>(scaledX);
    AccumulateWheelTravel(signedTravel);
    gLastWheelTravelTime.store(
        mach_continuous_time(), std::memory_order_release);
    gRawTrackpadFedEvents.fetch_add(1, std::memory_order_relaxed);
    AtomicFloatAdd(gRawTrackpadSignedTravel, signedTravel);
    AtomicFloatAdd(gRawTrackpadAbsTravel, std::abs(signedTravel));
}

// End of gesture: clear everything the just-finished swipe could still be
// driving pitch or yaw from, so the camera stops the instant the fingers
// lift instead of coasting on a backlog:
//   - the raw-trackpad X accumulator and pending yaw (DiscardWheelTravel),
//   - gPitchTravel, the pointer vertical travel banked but not yet spent,
//   - gRightStickY, but only if the pointer is the identified vertical
//     source. That field is also the gamepad stick's; a swipe's last 104/105
//     leaves it non-zero, and the pitch getter's stick path then integrates
//     it every frame until something zeroes it - which is the run-on. Guarded
//     so a gamepad-only session is never touched; the worst case if both are
//     in use at once is one frame, restored by the next stick sample.
// Mouse 98/109/110, mouse middle-drag pitch (gMousePitchTravel) and Q/E are
// untouched. Returns the |pending| yaw that was dropped, for telemetry.
float RawTrackpadEndGesture() {
    float dropped = 0.0f;
    {
        std::lock_guard lock(gYawStateMutex);
        dropped = std::abs(gPendingYawTravel);
    }
    DiscardWheelTravel();
    gPitchTravel.store(0.0f, std::memory_order_relaxed);
    if (gWheelRotateDevice.load(std::memory_order_relaxed) ==
        static_cast<int>(kPointerDevice)) {
        gRightStickY.store(0.0f, std::memory_order_relaxed);
    }
    gRawAxisLock = RawAxisLock::Undecided;
    gRawCumulativeAbsX = 0.0;
    gRawCumulativeAbsY = 0.0;
    gRawBankedX = 0.0;
    PublishRawGesture(false, RawAxisLock::Undecided);
    gRawTrackpadGestureEnds.fetch_add(1, std::memory_order_relaxed);
    AtomicFloatAdd(gRawTrackpadDiscardedPending, dropped);
    return dropped;
}

}  // namespace

// Called from the NSEvent scroll monitor (main thread) for every precise
// scroll event. Applies BG3's x0.1 scale, the gesture lifecycle and the axis
// lock; only Horizontal-locked, non-momentum travel reaches the accumulator.
void RawTrackpadScroll(
    double deltaX,
    double deltaY,
    unsigned long phase,
    unsigned long momentumPhase) {
    if (!gHooksEnabled.load(std::memory_order_acquire)) {
        return;
    }

    // Every precise event - sample, end, or inertia - refreshes the tail
    // window the 104/105 branch uses to keep neutralizing after the fingers
    // lift. A physical mouse wheel never reaches this function, so it never
    // opens the window.
    gRawLastPreciseTick.store(
        mach_continuous_time(), std::memory_order_release);

    // Inertia tail after the fingers lift. Counted, never fed anywhere; the
    // 104/105 neutralization is what actually stops the camera (see the tail
    // window above).
    if (momentumPhase != 0UL) {
        gRawTrackpadMomentumEvents.fetch_add(1, std::memory_order_relaxed);
        RawTrackpadLog(deltaX, deltaY, 0.0, phase, momentumPhase,
                       "momentum-skip");
        return;
    }

    if ((phase & kRawPhaseBegan) != 0UL) {
        gRawAxisLock = RawAxisLock::Undecided;
        gRawCumulativeAbsX = 0.0;
        gRawCumulativeAbsY = 0.0;
        gRawBankedX = 0.0;
        PublishRawGesture(true, RawAxisLock::Undecided);
        RawTrackpadLog(deltaX, deltaY, 0.0, phase, momentumPhase, "began");
        // A Began event can also carry a delta - fall through.
    }

    if ((phase & (kRawPhaseEnded | kRawPhaseCancelled)) != 0UL) {
        const float dropped = RawTrackpadEndGesture();
        RawTrackpadLog(deltaX, deltaY, 0.0, phase, momentumPhase,
                       dropped > 0.0f ? "ended-discardX" : "ended");
        return;
    }

    if (!std::isfinite(deltaX) || !std::isfinite(deltaY)) {
        return;
    }

    switch (gRawAxisLock) {
    case RawAxisLock::Horizontal: {
        const double scaledX = deltaX * kRawScrollToActionScale;
        RawTrackpadPushScaledX(scaledX);
        RawTrackpadLog(deltaX, deltaY, scaledX, phase, momentumPhase, "H-feed");
        break;
    }
    case RawAxisLock::Vertical:
        // X of this gesture was discarded at the lock; nothing to do. 104/105
        // still carry the vertical unchanged.
        RawTrackpadLog(deltaX, deltaY, 0.0, phase, momentumPhase, "V-skipX");
        break;
    case RawAxisLock::Undecided: {
        gRawCumulativeAbsX += std::abs(deltaX);
        gRawCumulativeAbsY += std::abs(deltaY);
        gRawBankedX += deltaX;
        if (gRawCumulativeAbsX >= kRawAxisDecideThreshold &&
            gRawCumulativeAbsX >=
                kRawAxisDominanceRatio * gRawCumulativeAbsY) {
            gRawAxisLock = RawAxisLock::Horizontal;
            PublishRawGesture(true, RawAxisLock::Horizontal);
            const double scaledBanked = gRawBankedX * kRawScrollToActionScale;
            gRawBankedX = 0.0;
            RawTrackpadPushScaledX(scaledBanked);
            RawTrackpadLog(deltaX, deltaY, scaledBanked, phase, momentumPhase,
                           "lock=H");
        } else if (gRawCumulativeAbsY >= kRawAxisDecideThreshold &&
                   gRawCumulativeAbsY >=
                       kRawAxisDominanceRatio * gRawCumulativeAbsX) {
            gRawAxisLock = RawAxisLock::Vertical;
            PublishRawGesture(true, RawAxisLock::Vertical);
            gRawBankedX = 0.0;  // X of this gesture is dropped for good
            RawTrackpadLog(deltaX, deltaY, 0.0, phase, momentumPhase,
                           "lock=V");
        } else {
            RawTrackpadLog(deltaX, deltaY, 0.0, phase, momentumPhase,
                           "undecided");
        }
        break;
    }
    }
}

// Called from the NSEvent monitor (main thread) for OtherMouseDragged with
// buttonNumber == 2. Only the vertical delta is taken; horizontal is left to
// BG3's native 98/109/110 mouse yaw. The sensitivity is applied later in the
// pitch getter.
void MouseMiddleDragged(double deltaX, double deltaY) {
    if (!gHooksEnabled.load(std::memory_order_acquire)) {
        return;
    }
    const bool fed = std::isfinite(deltaY) && deltaY != 0.0;
    if (fed) {
        const float sample = static_cast<float>(deltaY);
        float previous =
            gMousePitchTravel.load(std::memory_order_relaxed);
        for (;;) {
            const float updated = previous + sample;
            if (!std::isfinite(updated)) {
                gMousePitchTravel.store(0.0f, std::memory_order_relaxed);
                break;
            }
            if (gMousePitchTravel.compare_exchange_weak(
                    previous, updated,
                    std::memory_order_relaxed,
                    std::memory_order_relaxed)) {
                break;
            }
        }
        gMousePitchEvents.fetch_add(1, std::memory_order_relaxed);
        AtomicFloatAdd(gMousePitchSignedTravel, sample);
    }

    if (!GetCameraConfig().verboseLogging) {
        return;
    }
    const std::uint64_t now = mach_continuous_time();
    const std::uint64_t prev =
        gMousePitchLastEventTick.exchange(now, std::memory_order_relaxed);
    const bool newBurst =
        prev == 0 ||
        SecondsBetween(prev, now) >= kRawTrackpadRearmGapSeconds;
    if (newBurst &&
        gMousePitchBurstCount.load(std::memory_order_relaxed) <
            static_cast<unsigned>(kRawTrackpadMaxBursts)) {
        const unsigned burst =
            gMousePitchBurstCount.fetch_add(
                1, std::memory_order_relaxed) + 1;
        gMousePitchBurstRemaining.store(
            kRawTrackpadBurstSize, std::memory_order_relaxed);
        RuntimeLog("mmb-pitch burst=%u (event-free gap >= %.0fs)",
                   burst, kRawTrackpadRearmGapSeconds);
    }
    if (gMousePitchBurstRemaining.fetch_sub(
            1, std::memory_order_relaxed) > 0) {
        RuntimeLog(
            "mmb-pitch burst=%u dx=%.4f dy=%.4f fedY=%.4f "
            "mouseVerticalSensitivity=%.3f note=%s",
            gMousePitchBurstCount.load(std::memory_order_relaxed),
            deltaX,
            deltaY,
            fed ? deltaY : 0.0,
            static_cast<double>(
                GetCameraConfig().mouseVerticalSensitivity),
            fed ? "drag" : "drag-zeroY");
    }
}

// OtherMouseUp with buttonNumber == 2: drop the mouse pitch accumulator so
// the camera does not keep pitching after the button is released.
void MouseMiddleUp() {
    gMousePitchTravel.store(0.0f, std::memory_order_relaxed);
    if (GetCameraConfig().verboseLogging) {
        RuntimeLog("mmb-pitch reset (middle button up)");
    }
}

CameraHookReadiness CheckCameraHookReadiness(
    std::uint8_t* pitchDefinitionSiteAddress,
    FILE* log) {
    void** definitionsBasePointer = nullptr;
    if (!DecodeCameraDefinitionsBasePointer(
            pitchDefinitionSiteAddress,
            log,
            &definitionsBasePointer)) {
        return CameraHookReadiness::Invalid;
    }

    void* definitionsBase = nullptr;
    std::memcpy(
        &definitionsBase,
        definitionsBasePointer,
        sizeof(definitionsBase));
    return definitionsBase == nullptr
        ? CameraHookReadiness::Waiting
        : CameraHookReadiness::Ready;
}

bool ActivateCameraHooks(FILE* log) {
    if (log == nullptr || gCameraDefinitionsBasePointer == nullptr) {
        return false;
    }

    std::lock_guard lock(gCameraDefinitionMutex);

    void* definitionsBase = nullptr;
    std::memcpy(
        &definitionsBase,
        gCameraDefinitionsBasePointer,
        sizeof(definitionsBase));
    if (definitionsBase == nullptr) {
        return false;
    }

    for (const std::size_t definitionOffset : kCameraDefinitionOffsets) {
        for (const std::size_t rateOffset : kPitchApproachRateOffsets) {
            const float rate = ReadCameraField<float>(
                definitionsBase,
                definitionOffset + rateOffset);
            if (!std::isfinite(rate) ||
                rate < 0.0f ||
                rate > kMaximumApproachRate) {
                std::fprintf(
                    log,
                    "[ABORT] implausible deferred CameraDefinition approach rate "
                    "definition=0x%zx field=0x%zx value=%f\n",
                    definitionOffset,
                    rateOffset,
                    rate);
                std::fflush(log);
                return false;
            }
        }
    }

    const CameraConfig& config = GetCameraConfig();

    // Checked once, here, rather than every frame: the fields are authored
    // data that does not change while the game runs, and a frame update is
    // not the place to discover a layout change. A failure disables only the
    // suppression - rotation still applies, it is simply subject to the
    // follow servo again, which is the behaviour before this was found.
    const bool yawFollowUsable = YawFollowRatesLookAuthored(definitionsBase);
    gYawFollowRatesUsable.store(yawFollowUsable, std::memory_order_release);
    if (yawFollowUsable) {
        std::fprintf(
            log,
            "[INFO] yaw follow servo rates authored by this build: "
            "default=[%.2f, %.2f] controller=[%.2f, %.2f] forced=[%.2f, %.2f]\n",
            ReadCameraField<float>(
                definitionsBase,
                kCameraDefinitionOffsets[1] + kYawFollowRateOffsets[0]),
            ReadCameraField<float>(
                definitionsBase,
                kCameraDefinitionOffsets[1] + kYawFollowRateOffsets[1]),
            ReadCameraField<float>(
                definitionsBase,
                kCameraDefinitionOffsets[2] + kYawFollowRateOffsets[0]),
            ReadCameraField<float>(
                definitionsBase,
                kCameraDefinitionOffsets[2] + kYawFollowRateOffsets[1]),
            ReadCameraField<float>(
                definitionsBase,
                kCameraDefinitionOffsets[0] + kYawFollowRateOffsets[0]),
            ReadCameraField<float>(
                definitionsBase,
                kCameraDefinitionOffsets[0] + kYawFollowRateOffsets[1]));
    } else {
        std::fprintf(
            log,
            "[WARN] yaw follow servo rates did not validate; pointer rotation "
            "stays subject to the camera's own follow correction\n");
    }

    const bool zoomBoundsUsable = ZoomBoundsLookAuthored(definitionsBase);
    gZoomBoundsUsable.store(zoomBoundsUsable, std::memory_order_release);
    if (zoomBoundsUsable) {
        CaptureZoomBounds(definitionsBase);
        // Reported before the override is written, otherwise the line shows
        // our own configured minimum back to us instead of the game's.
        std::fprintf(
            log,
            "[INFO] camera arm bounds authored by this build: "
            "default=[%.2f, %.2f] controller=[%.2f, %.2f]\n",
            ReadCameraField<float>(
                definitionsBase,
                kCameraDefinitionOffsets[1] + kZoomBoundsFields[0].minimumOffset),
            ReadCameraField<float>(
                definitionsBase,
                kCameraDefinitionOffsets[1] + kZoomBoundsFields[0].maximumOffset),
            ReadCameraField<float>(
                definitionsBase,
                kCameraDefinitionOffsets[1] + kZoomBoundsFields[1].minimumOffset),
            ReadCameraField<float>(
                definitionsBase,
                kCameraDefinitionOffsets[1] + kZoomBoundsFields[1].maximumOffset));
        ApplyZoomBounds(definitionsBase, config);
    } else {
        std::fprintf(
            log,
            "[WARN] camera arm bounds did not validate; the zoom limit "
            "override is disabled and vanilla zoom range is kept\n");
    }

    std::fprintf(
        log,
        "[INFO] deferred CameraDefinition ready pointer=%p base=%p\n",
        static_cast<void*>(gCameraDefinitionsBasePointer),
        definitionsBase);
    gHooksEnabled.store(true, std::memory_order_release);
    // Install the macOS scroll monitor now that the hooks are live. It marks
    // itself active on the main thread once the NSEvent monitor is in place.
    StartRawTrackpadMonitor();
    std::fprintf(
        log,
        "[INFO] build=%s flavor=%s; trackpad scroll monitor install requested\n",
        BG3_CAMERA_VERSION,
        BG3_CAMERA_FLAVOR);
    std::fprintf(
        log,
        "[ACTIVE] initialMode=RPG(assumed) "
        "pitchEvents=[%u,%u] rotateEvents=[%u,%u] "
        "l3StartEvent=0x%x toggleEvent=0x%x modeChangedEvent=0x%x "
        "deferredL3=enabled "
        "deadzone=%.2f horizontal=%.2f vertical=%.2f zoom=%.2f curve=%.2f "
        "zoomFloor=%.2f "
        "zoomLimits=[%.2f,%.2f] boundsUsable=%d verbose=%d "
        "deltaTime=GameTime+0x%zx maxDelta=%.2f "
        "approachRate=intra-frame(x%.0f, cap %.0f) limits=[%.1f, %.1f] "
        "collision=physics-sweep+floor-bisection enabled=[%d,%d] "
        "directionOffset=0x%zx sweepMargin=%.2f "
        "minZoom=%.2f "
        "floor=bisection-stabilized-multistorey "
        "probeY=max(cameraY,rootY) probes=%zu tolerance=%.3f offset=%.2f "
        "holdFrames=%zu releaseSpeed=%.2f\n",
        kZoomInEvent,
        kZoomOutEvent,
        kRotateLeftEvent,
        kRotateRightEvent,
        kL3ChordStartEvent,
        kToggleInputModeEvent,
        kInputModeChangedEvent,
        config.stickDeadzone,
        config.horizontalSensitivity,
        config.verticalSensitivity,
        config.zoomSensitivity,
        config.zoomResponseCurve,
        config.zoomMinimumResponse,
        config.minimumZoomDistance,
        config.maximumZoomDistance,
        zoomBoundsUsable ? 1 : 0,
        config.verboseLogging ? 1 : 0,
        kGameTimeDeltaOffset,
        kMaximumGameDeltaSeconds,
        kIntraFrameConvergenceFactor,
        kMaximumApproachRate,
        config.pitchMinimumDegrees,
        config.pitchMaximumDegrees,
        config.obstacleCollision ? 1 : 0,
        config.floorProtection ? 1 : 0,
        kCameraDirectionOffset,
        config.collisionSafetyMargin,
        kMinimumCollisionZoom,
        kFloorSearchProbes,
        kFloorSearchTolerance,
        config.floorSafetyOffset,
        kFloorClearFramesBeforeRelease,
        kFloorReleaseSpeedPerSecond);
    std::fflush(log);
    return true;
}

// Prepares and writes one frame of pointer rotation.
//
// Called once per update from the pre-gate site, with the camera the update
// itself is about to read the channel from. Every step that could produce a
// non-finite number is checked before anything is written, and the buffer is
// not debited here at all - that happens only once the update has been
// observed to consume what was written.
void TryInjectNativeRotation(void* camera) {
    const CameraConfig& config = GetCameraConfig();

    std::lock_guard lock(gYawStateMutex);

    if (!config.pointerRotationNative) {
        // Turned off mid-session. Whatever was banked belongs to a mode that
        // is no longer running, so it is dropped rather than saved up.
        gWheelRotateTravel.store(0.0f, std::memory_order_relaxed);
        gLastWheelTravelTime.store(0, std::memory_order_relaxed);
        gPendingYawTravel = 0.0f;
        return;
    }

    // Taken exactly once per update, before anything can return early: the
    // input thread must never see two updates competing for the same samples.
    const float arrived =
        gWheelRotateTravel.exchange(0.0f, std::memory_order_relaxed);
    if (std::isfinite(arrived) && arrived != 0.0f) {
        gPendingYawTravel += arrived;
    }
    if (!std::isfinite(gPendingYawTravel)) {
        // A non-finite pending is unusable and cannot be repaired. Note that
        // an invalid arrived does not reach here: it is skipped above, so it
        // cannot destroy a valid buffer.
        gPendingYawTravel = 0.0f;
        return;
    }
    if (gPendingYawTravel == 0.0f) {
        return;
    }
    AtomicFloatMax(gPendingPeak, std::abs(gPendingYawTravel));

    // Arbitration. The channel belongs to the game first: if it has both
    // selected the pointer and put a value there, this frame is the mouse's
    // and the overlapping travel is dropped outright. Deferring it instead
    // would turn an overlap into rotation arriving after the player stopped
    // moving anything, and that is worse than losing it.
    const bool selectorSet =
        (gPreGateFlagByte & kMouseRotationFlagBit) != 0;
    // Bitwise, so -0.0 counts as absent while a NaN or an infinity counts as
    // present: a channel holding either of those is one to leave alone.
    const bool nativeDeltaPresent =
        (gPreGateDeltaBits & 0x7fffffffu) != 0;
    if (selectorSet && nativeDeltaPresent) {
        const float dropped = std::abs(gPendingYawTravel);
        gPendingYawTravel = 0.0f;
        gWheelRotateTravel.store(0.0f, std::memory_order_relaxed);
        gLastWheelTravelTime.store(0, std::memory_order_relaxed);
        gInjection.nativeConflict = true;
        gNativeConflictDrops.fetch_add(1, std::memory_order_relaxed);
        AtomicFloatAdd(gDroppedConflictTravel, dropped);
        RuntimeLog(
            "x native-conflict-drop camera=%p delta=0x%08x flag=0x%02x "
            "droppedTravel=%.4f",
            camera,
            gPreGateDeltaBits,
            static_cast<unsigned>(gPreGateFlagByte),
            static_cast<double>(dropped));
        return;
    }

    const float deltaSeconds = gGameDeltaSeconds;
    // The proven proto2 yaw base times the normalized trackpad multiplier -
    // not a single shared pointer key. 1.0 reproduces proto2 exactly.
    const float degreesPerTravel =
        kTrackpadYawBaseCalibration *
        config.trackpadHorizontalSensitivity *
        kDegreesPerTravelUnit;
    float rotationSpeed = ReadCameraField<float>(
        camera, kCameraRotationSpeedOffset);
    if (!std::isfinite(rotationSpeed) || rotationSpeed <= 0.0f) {
        rotationSpeed = kFallbackPitchSpeedDegreesPerSecond;
    }
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f ||
        !std::isfinite(degreesPerTravel) || degreesPerTravel <= 0.0f) {
        // Nothing is written and nothing is spent; the buffer keeps its
        // travel for a frame that can use it.
        return;
    }

    // Smoothing while the fingers are still moving, completion once they are
    // not. A trackpad falls silent for tens of milliseconds mid-gesture, so a
    // single empty frame means nothing; a gap longer than the smoothing
    // window does.
    const float timeConstant = std::max(
        config.pointerRotateSmoothingMilliseconds / 1000.0f,
        kMinimumNativeYawSmoothingSeconds);
    const std::uint64_t lastTravel =
        gLastWheelTravelTime.load(std::memory_order_acquire);
    const bool finishing =
        lastTravel == 0 ||
        SecondsBetween(lastTravel, mach_continuous_time()) >=
            static_cast<double>(timeConstant);

    float spend = finishing
        ? gPendingYawTravel
        : gPendingYawTravel * std::clamp(
              1.0f - std::exp(-deltaSeconds / timeConstant), 0.0f, 1.0f);

    // The ceiling is a rate, and it is applied to the travel about to be
    // spent rather than to the angle that comes out of it, so what it holds
    // back stays in the buffer instead of being destroyed.
    const float maximumTravelThisFrame =
        (kMaximumRotationDegreesPerSecond * deltaSeconds) / degreesPerTravel;
    if (std::abs(spend) > maximumTravelThisFrame) {
        spend = std::copysign(maximumTravelThisFrame, spend);
        gCappedFrames.fetch_add(1, std::memory_order_relaxed);
    }
    if (spend == 0.0f) {
        return;
    }
    if (finishing) {
        gResidualTailFrames.fetch_add(1, std::memory_order_relaxed);
        AtomicFloatAdd(gResidualTailMilliseconds, deltaSeconds * 1000.0f);
    }

    const float degrees = spend * degreesPerTravel;
    // The field is a rate the update multiplies by the frame length and the
    // camera's own angular speed, and negates on the way. Asking for a fixed
    // angle therefore means dividing by both and inverting the sign.
    const float delta = -degrees / (deltaSeconds * rotationSpeed);
    if (!std::isfinite(delta)) {
        return;
    }

    std::uint32_t injectedBits{};
    std::memcpy(&injectedBits, &delta, sizeof(injectedBits));
    const auto injectedFlag = static_cast<std::uint8_t>(
        gPreGateFlagByte | kMouseRotationFlagBit);

    gInjection.active = true;
    gInjection.camera = camera;
    gInjection.previousDeltaBits = gPreGateDeltaBits;
    gInjection.previousFlagByte = gPreGateFlagByte;
    gInjection.injectedDeltaBits = injectedBits;
    gInjection.injectedFlagByte = injectedFlag;
    gInjection.proposedSpendTravel = spend;
    gInjection.askedDegrees = degrees;

    WriteCameraField(camera, kMouseRotationDeltaOffset, injectedBits);
    WriteCameraField(camera, kMouseRotationFlagByteOffset, injectedFlag);
    gDriveRequested.fetch_add(1, std::memory_order_relaxed);
}

// One line carrying every pre-gate counter. Emitted for the first few
// updates, on any anomaly, when the camera changes, on a heartbeat, and once
// more when the hooks come out.
void LogPreGateTotals(const char* reason) {
    RuntimeLog(
        "pre-gate totals reason=%s updates=%lu hits=%lu missing=%lu multi=%lu "
        "beforePtrMismatch=%lu beforeDeltaChanged=%lu beforeFlagsChanged=%lu "
        "afterPtrMismatch=%lu afterDeltaChanged=%lu afterFlagsChanged=%lu "
        "conditionalHits=%lu noConditional=%lu outsideUpdate=%lu "
        "nativeFlagSet=%lu nativeDeltaNonZero=%lu nativeBoth=%lu | "
        "xFeedEvents=%lu xFeedTravel=%.3f driveRequested=%lu driveVerified=%lu "
        "driveMissedConditional=%lu drivePointerMismatch=%lu "
        "driveFieldMismatch=%lu driveCommitted=%lu restoreMismatch=%lu "
        "cappedFrames=%lu nativeConflictDrops=%lu droppedConflictTravel=%.3f "
        "pendingPeak=%.3f residualTailFrames=%lu residualTailMs=%.1f",
        reason,
        gPreGateUpdatesObserved.load(std::memory_order_relaxed),
        gPreGateEntries.load(std::memory_order_relaxed),
        gPreGateMissing.load(std::memory_order_relaxed),
        gPreGateMultiHit.load(std::memory_order_relaxed),
        gPointerMismatchBeforeConsume.load(std::memory_order_relaxed),
        gDeltaChangedBeforeConsume.load(std::memory_order_relaxed),
        gFlagsChangedBeforeConsume.load(std::memory_order_relaxed),
        gPointerMismatchAfterConsume.load(std::memory_order_relaxed),
        gDeltaChangedAfterConsume.load(std::memory_order_relaxed),
        gFlagsChangedAfterConsume.load(std::memory_order_relaxed),
        gConditionalHits.load(std::memory_order_relaxed),
        gNoConditionalHit.load(std::memory_order_relaxed),
        gPreGateOutsideUpdate.load(std::memory_order_relaxed),
        gNativeFlagAlreadySet.load(std::memory_order_relaxed),
        gNativeDeltaAlreadyNonZero.load(std::memory_order_relaxed),
        gNativeBothAlready.load(std::memory_order_relaxed),
        gXFeedEvents.load(std::memory_order_relaxed),
        static_cast<double>(gXFeedTravel.load(std::memory_order_relaxed)),
        gDriveRequested.load(std::memory_order_relaxed),
        gDriveVerified.load(std::memory_order_relaxed),
        gDriveMissedConditional.load(std::memory_order_relaxed),
        gDrivePointerMismatch.load(std::memory_order_relaxed),
        gDriveFieldMismatch.load(std::memory_order_relaxed),
        gDriveCommitted.load(std::memory_order_relaxed),
        gRestoreMismatch.load(std::memory_order_relaxed),
        gCappedFrames.load(std::memory_order_relaxed),
        gNativeConflictDrops.load(std::memory_order_relaxed),
        static_cast<double>(
            gDroppedConflictTravel.load(std::memory_order_relaxed)),
        static_cast<double>(gPendingPeak.load(std::memory_order_relaxed)),
        gResidualTailFrames.load(std::memory_order_relaxed),
        static_cast<double>(
            gResidualTailMilliseconds.load(std::memory_order_relaxed)));
}

// Read-only observer at the dominator of the gate.
//
// It takes the one snapshot the two verifiers downstream are measured
// against: which GameCameraBehavior this update is working on, and the exact
// bits of the two fields the future feeder will write. Nothing is written to
// the camera, no accumulator is touched and no rotation is driven - this
// build only establishes that the site behaves the way the static analysis
// says it does.
extern "C" void BG3PreGateApply(void* cameraBehavior) {
    if (!gHooksEnabled.load(std::memory_order_acquire)) {
        return;
    }

    if (!gInsideGameCameraUpdate) {
        gPreGateOutsideUpdate.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    ++gPreGateHitsThisUpdate;
    gPreGateEntries.fetch_add(1, std::memory_order_relaxed);

    // A repeat entry is counted and nothing else: the snapshot has to stay the
    // one the gate will act on, and overwriting it would hide exactly the
    // multiplicity this counter exists to detect.
    if (gPreGateSeen || cameraBehavior == nullptr) {
        return;
    }

    gPreGateSeen = true;
    gPreGateCamera = cameraBehavior;
    gPreGateDeltaBits = ReadCameraField<std::uint32_t>(
        cameraBehavior, kMouseRotationDeltaOffset);
    gPreGateFlagByte = ReadCameraField<std::uint8_t>(
        cameraBehavior, kMouseRotationFlagByteOffset);

    // Simultaneous native pointer input, measured only. Both halves of "the
    // game is already using this channel" are counted separately because they
    // can occur apart: a set flag with a zero delta is an idle mouse, a
    // non-zero delta with a clear flag is a value the update will ignore.
    const bool flagSet =
        (gPreGateFlagByte & kMouseRotationFlagBit) != 0;
    // Bitwise, so that -0.0 counts as zero and a NaN counts as non-zero
    // without either going through a float comparison.
    const bool deltaNonZero =
        (gPreGateDeltaBits & 0x7fffffffu) != 0;
    if (flagSet) {
        gNativeFlagAlreadySet.fetch_add(1, std::memory_order_relaxed);
    }
    if (deltaNonZero) {
        gNativeDeltaAlreadyNonZero.fetch_add(1, std::memory_order_relaxed);

        bool firstSighting = false;
        {
            std::lock_guard lock(gObservedNativeDeltaMutex);
            firstSighting = true;
            for (std::size_t index = 0;
                 index < gObservedNativeDeltaCount;
                 ++index) {
                if (gObservedNativeDeltas[index] == gPreGateDeltaBits) {
                    firstSighting = false;
                    break;
                }
            }
            if (firstSighting) {
                if (gObservedNativeDeltaCount <
                    gObservedNativeDeltas.size()) {
                    gObservedNativeDeltas[gObservedNativeDeltaCount] =
                        gPreGateDeltaBits;
                    ++gObservedNativeDeltaCount;
                } else {
                    firstSighting = false;
                }
            }
        }

        if (firstSighting) {
            float value{};
            std::memcpy(&value, &gPreGateDeltaBits, sizeof(value));
            RuntimeLog(
                "pre-gate native delta already present bits=0x%08x value=%.6f "
                "flag=%u",
                gPreGateDeltaBits,
                static_cast<double>(value),
                static_cast<unsigned>(gPreGateFlagByte));
        }
    }
    if (flagSet && deltaNonZero) {
        gNativeBothAlready.fetch_add(1, std::memory_order_relaxed);
    }

    // The one point in the whole mod that writes the pointer channel, with
    // the object the update is about to read it from.
    TryInjectNativeRotation(cameraBehavior);
}

// Read-only. Records which GameCameraBehavior the update is about to read the
// rotation channel from, and says so in the log.
//
// It writes nothing to the camera, accumulates no input and drives no
// rotation: the X-axis feeder stays off until this pointer's identity has been
// confirmed at runtime. Keeping the first step inert is the point - the site
// is new, and a detour that only observes can be judged on its own.
extern "C" void BG3PreYawApply(void* cameraBehavior) {
    if (!gHooksEnabled.load(std::memory_order_acquire)) {
        return;
    }

    const bool insideUpdate = gInsideGameCameraUpdate;
    if (insideUpdate) {
        gPreYawCameraSeenThisUpdate = cameraBehavior;
        ++gPreYawHitsThisUpdate;
    } else {
        gPreYawOutsideUpdateCount.fetch_add(1, std::memory_order_relaxed);
    }

    // Verifier, immediately before the four displaced instructions select the
    // channel and the instruction after them consumes it. This is the last
    // moment at which the two fields can still be compared with what the
    // pre-gate site saw, which is what closes the question of whether any of
    // the calls in between disturbs them. Read-only: a mismatch is counted
    // and nothing else.
    if (insideUpdate) {
        gConditionalHits.fetch_add(1, std::memory_order_relaxed);

        if (!gPreGateSeen) {
            gPreGateMissing.fetch_add(1, std::memory_order_relaxed);
        } else if (cameraBehavior != nullptr) {
            if (gPreGateCamera != cameraBehavior) {
                gPointerMismatchBeforeConsume.fetch_add(
                    1, std::memory_order_relaxed);
            }
            // Compared as bits, never as floats: a float comparison would
            // call -0.0 equal to +0.0 and every NaN unequal to itself, and
            // both of those are exactly the changes worth catching.
            const auto deltaBits = ReadCameraField<std::uint32_t>(
                cameraBehavior, kMouseRotationDeltaOffset);
            const auto flagByte = ReadCameraField<std::uint8_t>(
                cameraBehavior, kMouseRotationFlagByteOffset);
            if (deltaBits != gPreGateDeltaBits) {
                gDeltaChangedBeforeConsume.fetch_add(
                    1, std::memory_order_relaxed);
            }
            if (flagByte != gPreGateFlagByte && !gInjection.active) {
                gFlagsChangedBeforeConsume.fetch_add(
                    1, std::memory_order_relaxed);
            }
        }

        // With an injection in flight the comparison is against what was
        // written, not against what was there before it.
        if (gInjection.active && cameraBehavior != nullptr) {
            const auto deltaBits = ReadCameraField<std::uint32_t>(
                cameraBehavior, kMouseRotationDeltaOffset);
            const auto flagByte = ReadCameraField<std::uint8_t>(
                cameraBehavior, kMouseRotationFlagByteOffset);
            const bool pointerOk =
                gPreGateSeen && gPreGateHitsThisUpdate == 1 &&
                gInjection.camera == cameraBehavior;
            const bool fieldsOk =
                deltaBits == gInjection.injectedDeltaBits &&
                flagByte == gInjection.injectedFlagByte;
            if (!pointerOk) {
                gDrivePointerMismatch.fetch_add(
                    1, std::memory_order_relaxed);
            }
            if (!fieldsOk) {
                gDriveFieldMismatch.fetch_add(1, std::memory_order_relaxed);
            }
            gInjection.conditionalVerified =
                pointerOk && fieldsOk && gPreYawHitsThisUpdate == 1;
        }
    }

    if (GetCameraConfig().verboseLogging &&
        gPreYawTraceBudget.fetch_sub(1, std::memory_order_relaxed) > 0) {
        RuntimeLog(
            "pre-yaw camera=%p insideUpdate=%d dt=%.4f",
            cameraBehavior,
            insideUpdate ? 1 : 0,
            static_cast<double>(gGameDeltaSeconds));
    }
}

extern "C" void BG3AfterZoomApply(void* cameraBehavior) {
    if (!gHooksEnabled.load(std::memory_order_acquire) ||
        !gRpgCameraModeActive.load(std::memory_order_acquire) ||
        !gInsideGameCameraUpdate ||
        gAfterZoomAppliedThisUpdate ||
        cameraBehavior == nullptr) {
        return;
    }

    // The detour runs once the update has finalized the zoom fields. Mark the
    // frame before doing anything so an unexpected re-entry cannot apply the
    // constraint twice.
    gAfterZoomAppliedThisUpdate = true;

    // End-of-update verifier. Same comparison as the one before the channel
    // is consumed, one stage later, so a field disturbed after consumption is
    // distinguishable from one disturbed before it.
    {
        gPreGateUpdatesObserved.fetch_add(1, std::memory_order_relaxed);

        bool anomaly = false;
        if (!gPreGateSeen) {
            // The pre-gate site dominates the gate, so an update that reached
            // this point without entering it contradicts the static analysis.
            // Unlike a missing conditional hit, this is never legitimate.
            gPreGateMissing.fetch_add(1, std::memory_order_relaxed);
            anomaly = true;
        } else {
            if (gPreGateHitsThisUpdate != 1) {
                gPreGateMultiHit.fetch_add(1, std::memory_order_relaxed);
                anomaly = true;
            }
            if (gPreGateCamera != cameraBehavior) {
                gPointerMismatchAfterConsume.fetch_add(
                    1, std::memory_order_relaxed);
                anomaly = true;
            }
            const auto deltaBits = ReadCameraField<std::uint32_t>(
                cameraBehavior, kMouseRotationDeltaOffset);
            const auto flagByte = ReadCameraField<std::uint8_t>(
                cameraBehavior, kMouseRotationFlagByteOffset);
            if (deltaBits != gPreGateDeltaBits) {
                gDeltaChangedAfterConsume.fetch_add(
                    1, std::memory_order_relaxed);
                anomaly = true;
            }
            if (flagByte != gPreGateFlagByte) {
                gFlagsChangedAfterConsume.fetch_add(
                    1, std::memory_order_relaxed);
                anomaly = true;
            }
        }

        // A conditional site that did not run is legitimate on an idle
        // frame: with nothing injected the gate only routes there when
        // vanilla control flow already had rotation input to consume. With an
        // injection in flight it is a functional failure, because the pre-gate
        // site set the selector precisely so that the gate would route there.
        if (gPreYawHitsThisUpdate == 0) {
            gNoConditionalHit.fetch_add(1, std::memory_order_relaxed);
            if (gInjection.active) {
                gDriveMissedConditional.fetch_add(
                    1, std::memory_order_relaxed);
                anomaly = true;
            }
        }

        // The injection is only spent once the update has been seen to
        // consume it. Anything short of that leaves the travel in the buffer
        // for the next frame to try again with.
        if (gInjection.active) {
            const auto deltaBits = ReadCameraField<std::uint32_t>(
                cameraBehavior, kMouseRotationDeltaOffset);
            const auto flagByte = ReadCameraField<std::uint8_t>(
                cameraBehavior, kMouseRotationFlagByteOffset);
            gInjection.afterZoomVerified =
                gInjection.camera == cameraBehavior &&
                deltaBits == gInjection.injectedDeltaBits &&
                flagByte == gInjection.injectedFlagByte;

            const bool verified =
                gInjection.conditionalVerified &&
                gInjection.afterZoomVerified;
            if (verified) {
                gDriveVerified.fetch_add(1, std::memory_order_relaxed);
                float pendingBefore = 0.0f;
                float pendingAfter = 0.0f;
                {
                    std::lock_guard lock(gYawStateMutex);
                    pendingBefore = gPendingYawTravel;
                    gPendingYawTravel -= gInjection.proposedSpendTravel;
                    if (!std::isfinite(gPendingYawTravel) ||
                        std::abs(gPendingYawTravel) < 1.0e-6f) {
                        // An exponential drain never reaches zero on its own;
                        // the completion rule above spends the remainder in
                        // full, and this keeps the arithmetic from leaving a
                        // speck behind that would hold the gesture open.
                        gPendingYawTravel = 0.0f;
                    }
                    pendingAfter = gPendingYawTravel;
                }
                gDriveCommitted.fetch_add(1, std::memory_order_relaxed);

                // gPreviousHeading still holds the previous frame's value
                // here: the trace block further down is what advances it.
                float applied = ReadCameraField<float>(
                    cameraBehavior, kCameraHeadingOffset) - gPreviousHeading;
                if (applied > 180.0f) {
                    applied -= 360.0f;
                } else if (applied < -180.0f) {
                    applied += 360.0f;
                }
                RuntimeLog(
                    "native check asked=%.3fdeg applied=%.3fdeg ratio=%.3f "
                    "pendingBefore=%.4f proposed=%.4f pendingAfter=%.4f "
                    "capped=%lu dt=%.4f speed=%.1f",
                    static_cast<double>(gInjection.askedDegrees),
                    static_cast<double>(applied),
                    gInjection.askedDegrees != 0.0f
                        ? static_cast<double>(
                              applied / gInjection.askedDegrees)
                        : 0.0,
                    static_cast<double>(pendingBefore),
                    static_cast<double>(gInjection.proposedSpendTravel),
                    static_cast<double>(pendingAfter),
                    gCappedFrames.load(std::memory_order_relaxed),
                    static_cast<double>(gGameDeltaSeconds),
                    static_cast<double>(ReadCameraField<float>(
                        cameraBehavior, kCameraRotationSpeedOffset)));
            } else {
                anomaly = true;
                RuntimeLog(
                    "x drive UNVERIFIED camera=%p conditional=%d afterZoom=%d "
                    "condHits=%u proposed=%.4f",
                    cameraBehavior,
                    gInjection.conditionalVerified ? 1 : 0,
                    gInjection.afterZoomVerified ? 1 : 0,
                    gPreYawHitsThisUpdate,
                    static_cast<double>(gInjection.proposedSpendTravel));
            }
        }
        if (gPointerMismatchBeforeConsume.load(std::memory_order_relaxed) +
                gDeltaChangedBeforeConsume.load(std::memory_order_relaxed) +
                gFlagsChangedBeforeConsume.load(std::memory_order_relaxed) >
            0) {
            anomaly = anomaly || gPreYawHitsThisUpdate != 0;
        }

        if (GetCameraConfig().verboseLogging) {
            const std::uint64_t now = mach_continuous_time();
            const unsigned long updates =
                gPreGateUpdatesObserved.load(std::memory_order_relaxed);
            const bool early = updates <= kPreGateVerboseUpdates;
            const bool cameraChanged =
                gPreGateCamera != gPreGateLastLoggedCamera;
            const bool heartbeat =
                gPreGateLastLogTime == 0 ||
                SecondsBetween(gPreGateLastLogTime, now) >=
                    kPreGateHeartbeatSeconds;

            if (anomaly || early || cameraChanged || heartbeat) {
                gPreGateLastLogTime = now;
                gPreGateLastLoggedCamera = gPreGateCamera;
                RuntimeLog(
                    "pre-gate update camera=%p afterZoom=%p hits=%u "
                    "conditionalHits=%u deltaBits=0x%08x flag=0x%02x%s",
                    gPreGateCamera,
                    cameraBehavior,
                    gPreGateHitsThisUpdate,
                    gPreYawHitsThisUpdate,
                    gPreGateDeltaBits,
                    static_cast<unsigned>(gPreGateFlagByte),
                    anomaly ? " ANOMALY" : "");
                LogPreGateTotals(
                    anomaly ? "anomaly"
                            : (early ? "early"
                                     : (cameraChanged ? "camera-change"
                                                      : "heartbeat")));
            }
        }
    }

    // Telemetry, not a gate: nothing below reads the result, and a mismatch
    // changes no behaviour. It answers whether the object the pre-yaw site
    // hands over is the object this site is given - the one fact the X-axis
    // feeder will depend on, and the one the disassembly could not settle.
    {
        void* const preYawCamera = gPreYawCameraSeenThisUpdate;
        const unsigned hits = gPreYawHitsThisUpdate;

        const char* verdict = nullptr;
        if (preYawCamera == nullptr) {
            ++gIdentityNoPreYawCount;
            verdict = "NO-PRE-YAW";
        } else if (preYawCamera == cameraBehavior) {
            ++gIdentityMatchCount;
            verdict = "MATCH";
        } else {
            ++gIdentityMismatchCount;
            verdict = "MISMATCH";
        }
        if (hits > 1) {
            ++gIdentityMultiHitCount;
        }

        // Counted above on every update; only the printing is selective.
        const std::uint64_t now = mach_continuous_time();
        const bool changed =
            preYawCamera != gLastLoggedPreYawCamera ||
            cameraBehavior != gLastLoggedAfterZoomCamera;
        const bool notable = preYawCamera != cameraBehavior || hits != 1;
        const bool heartbeat =
            gLastIdentityLogTime == 0 ||
            SecondsBetween(gLastIdentityLogTime, now) >=
                kIdentityHeartbeatSeconds;

        if (GetCameraConfig().verboseLogging &&
            (changed || notable || heartbeat)) {
            gLastLoggedPreYawCamera = preYawCamera;
            gLastLoggedAfterZoomCamera = cameraBehavior;
            gLastIdentityLogTime = now;
            RuntimeLog(
                "pre-yaw identity preYaw=%p afterZoom=%p hits=%u %s | "
                "totals match=%lu mismatch=%lu noPreYaw=%lu multiHit=%lu "
                "outsideUpdate=%lu",
                preYawCamera,
                cameraBehavior,
                hits,
                verdict,
                gIdentityMatchCount,
                gIdentityMismatchCount,
                gIdentityNoPreYawCount,
                gIdentityMultiHitCount,
                gPreYawOutsideUpdateCount.load(std::memory_order_relaxed));
        }
    }

    {
        // desiredZoom is the target the game's own zoom code moves toward, so
        // its per-frame change is the game's actual response to our payload.
        const float desired = ReadCameraField<float>(
            cameraBehavior, kDesiredZoomOffset);
        const float delta = desired - gPreviousDesiredZoom;
        const float input = gLastZoomInput.load(std::memory_order_relaxed);
        // Logged whether or not the frame moved: the frames that did not move
        // are the ones that locate the threshold.
        if (std::isfinite(desired) && input > 0.0f &&
            gZoomTraceBudget.fetch_sub(1, std::memory_order_relaxed) > 0) {
            RuntimeLog(
                "zoom trace input=%.4f pressed=%u payload=%.4f "
                "desiredZoom=%.4f delta=%.5f dt=%.4f",
                input,
                gLastZoomPressed.load(std::memory_order_relaxed),
                gLastZoomPayload.load(std::memory_order_relaxed),
                desired,
                delta,
                gGameDeltaSeconds);
        }
        gPreviousDesiredZoom = desired;
    }

    {
        // Both axes in one line, in degrees per second, so the ratio between
        // them can be read straight off instead of inferred.
        const float heading = ReadCameraField<float>(
            cameraBehavior, kCameraHeadingOffset);
        const float speed = ReadCameraField<float>(
            cameraBehavior, kCameraRotationSpeedOffset);
        float headingDelta = heading - gPreviousHeading;
        if (headingDelta > 180.0f) {
            headingDelta -= 360.0f;
        } else if (headingDelta < -180.0f) {
            headingDelta += 360.0f;
        }

        float pitchNow = 0.0f;
        {
            std::lock_guard lock(gPitchStateMutex);
            pitchNow = gCurrentPitch;
        }
        const float pitchDelta = pitchNow - gPreviousTracePitch;

        const float rotateInput =
            gLastRotateNormalized.load(std::memory_order_relaxed);
        const float pitchInput = gRightStickY.load(std::memory_order_relaxed);

        if (gGameDeltaSeconds > 0.0f &&
            (std::abs(headingDelta) > 0.0005f ||
             std::abs(pitchDelta) > 0.0005f) &&
            gCameraTraceBudget.fetch_sub(1, std::memory_order_relaxed) > 0) {
            RuntimeLog(
                "camera trace hIn=%.4f hRate=%.3f | vIn=%.4f vRate=%.3f "
                "rotationSpeed=%.3f dt=%.4f",
                rotateInput,
                headingDelta / gGameDeltaSeconds,
                pitchInput,
                pitchDelta / gGameDeltaSeconds,
                speed,
                gGameDeltaSeconds);
        }

        gPreviousHeading = heading;
        gPreviousTracePitch = pitchNow;
    }
    // Sweep the whole requested camera arm through the physics scene first.
    // Unlike a floor query this tests whether the arm crosses real geometry,
    // so it cannot quietly switch to a lower storey once the camera has
    // passed through an upper floor.
    const CameraConfig& config = GetCameraConfig();
    if (config.obstacleCollision) {
        ApplyCameraCollisionLimit(
            gCameraSystemForUpdate,
            gWorldViewForUpdate,
            cameraBehavior);
    }
    // The floor query then acts as a secondary guard for terrain and stairs,
    // consuming whatever shortened arm the sweep above may have written.
    if (config.floorProtection) {
        ApplyFloorClearanceLimit(
            gCameraSystemForUpdate,
            gWorldViewForUpdate,
            cameraBehavior);
    }
}

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
    FILE* log) {
    gHooksEnabled.store(false, std::memory_order_release);
    gRightStickY.store(0.0f, std::memory_order_relaxed);
    gRpgCameraModeActive.store(true, std::memory_order_relaxed);
    gLastMfiInputDevice.store(nullptr, std::memory_order_relaxed);
    gLastMfiSampleTime.store(0, std::memory_order_relaxed);
    gMfiL3Pressed.store(false, std::memory_order_relaxed);
    gPointerZoomModifierHeld.store(false, std::memory_order_relaxed);
    gCollisionClampActive.store(false, std::memory_order_relaxed);
    gZoomBoundsUsable.store(false, std::memory_order_relaxed);
    gSmallestRawStick.store(1.0e9f, std::memory_order_relaxed);
    for (std::atomic<float>* slot : {
             &gSmallestZoomInChord, &gSmallestZoomOutChord,
             &gSmallestZoomInPitch, &gSmallestZoomOutPitch,
             &gSmallestRotateLeft, &gSmallestRotateRight}) {
        slot->store(1.0e9f, std::memory_order_relaxed);
    }
    ResetFloorConstraint();
    {
        std::lock_guard lock(gDeferredToggleMutex);
        gDeferredToggle = {};
    }
    gLastCameraBehavior = nullptr;
    gPitchInitialized = false;
    gCurrentPitch = 0.0f;
    gRuntimeLog = log;
    gCameraDefinitionsBasePointer = nullptr;
    gCollideWithObstacles = nullptr;
    gGetFloorLevel = nullptr;
    DiscardWheelTravel();
    // Every telemetry tally starts from zero for this installation. 2.6.3
    // left these accumulating, which would have made a second install in the
    // same process report totals that silently included the first one's.
    for (std::atomic<unsigned long>* slot : {
             &gPreGateUpdatesObserved, &gPreGateEntries, &gPreGateOutsideUpdate,
             &gPreGateMissing, &gPreGateMultiHit,
             &gPointerMismatchBeforeConsume, &gDeltaChangedBeforeConsume,
             &gFlagsChangedBeforeConsume, &gPointerMismatchAfterConsume,
             &gDeltaChangedAfterConsume, &gFlagsChangedAfterConsume,
             &gConditionalHits, &gNoConditionalHit,
             &gNativeFlagAlreadySet, &gNativeDeltaAlreadyNonZero,
             &gNativeBothAlready,
             &gXFeedEvents, &gDriveRequested, &gDriveVerified,
             &gDriveMissedConditional, &gDrivePointerMismatch,
             &gDriveFieldMismatch, &gDriveCommitted, &gRestoreMismatch,
             &gCappedFrames, &gNativeConflictDrops, &gResidualTailFrames}) {
        slot->store(0, std::memory_order_relaxed);
    }
    for (std::atomic<float>* slot : {
             &gXFeedTravel, &gDroppedConflictTravel, &gPendingPeak,
             &gResidualTailMilliseconds}) {
        slot->store(0.0f, std::memory_order_relaxed);
    }
    gIdentityMatchCount = 0;
    gIdentityMismatchCount = 0;
    gIdentityNoPreYawCount = 0;
    gIdentityMultiHitCount = 0;
    gPreYawOutsideUpdateCount.store(0, std::memory_order_relaxed);
    gPreGateLastLogTime = 0;
    gPreGateLastLoggedCamera = nullptr;
    gLastIdentityLogTime = 0;
    gLastLoggedPreYawCamera = nullptr;
    gLastLoggedAfterZoomCamera = nullptr;
    {
        std::lock_guard lock(gObservedNativeDeltaMutex);
        gObservedNativeDeltaCount = 0;
    }
    gRotateTraceBudget.store(
        kInitialRotateTraceBudget, std::memory_order_relaxed);
    gPointerVerticalTraceBudget.store(
        kInitialPointerVerticalTraceBudget, std::memory_order_relaxed);
    gXFeedTraceBudget.store(
        kInitialXFeedTraceBudget, std::memory_order_relaxed);
    gPreYawTraceBudget.store(
        kInitialPreYawTraceBudget, std::memory_order_relaxed);
    gButtonActionTraceBudget.store(
        kInitialButtonActionTraceBudget, std::memory_order_relaxed);
    gNativeArmLastEventTick.store(0, std::memory_order_relaxed);
    gNativeArmBurstRemaining.store(0, std::memory_order_relaxed);
    gNativeArmBurstCount.store(0, std::memory_order_relaxed);

#if BG3_CAMERA_WITH_WASD
    const bool keyboardMovementRequested = GetCameraConfig().keyboardMovement;
#else
    // Camera Only flavor: the keyboard-movement guard is never patched and its
    // ARM64 pattern is not required, so a null address here is expected.
    (void)keyboardMovementGuardAddress;
    constexpr bool keyboardMovementRequested = false;
#endif

    if (afterZoomSiteAddress == nullptr ||
        preYawSiteAddress == nullptr ||
        preGateSiteAddress == nullptr ||
        collideWithObstaclesAddress == nullptr ||
        restrictCamTargetDestHeightAddress == nullptr ||
        (keyboardMovementRequested &&
         keyboardMovementGuardAddress == nullptr)) {
        std::fprintf(
            log,
            "[ABORT] required hook, patch, or helper address is null\n");
        return false;
    }

    gCollideWithObstacles =
        reinterpret_cast<CollideWithObstaclesFunction>(
            collideWithObstaclesAddress);
    gGetFloorLevel =
        reinterpret_cast<FloorLevelFunction>(
            restrictCamTargetDestHeightAddress);

    if (mach_timebase_info(&gTimebase) != KERN_SUCCESS) {
        std::fprintf(log, "[ABORT] mach_timebase_info failed\n");
        return false;
    }

    // Log timestamps are relative to this point, which is the first moment
    // the timebase they are computed from is usable.
    gRuntimeLogEpoch = mach_continuous_time();

    if (!ResolveCameraDefinitionsBasePointer(
            pitchDefinitionSiteAddress,
            log)) {
        return false;
    }

    std::string error;
    auto rollback = [&]() {
        std::string rollbackError;
        if (!RemoveInstructionPatch(
                hooks.keyboardMovementPatch,
                rollbackError)) {
            LogHookError(
                log,
                "rollback keyboard movement patch",
                rollbackError);
        }
        rollbackError.clear();
        if (!RemoveInlineHook(hooks.pitchHook, rollbackError)) {
            LogHookError(log, "rollback GetCameraPitchDegrees hook", rollbackError);
        }
        rollbackError.clear();
        if (!RemoveInlineHook(hooks.preGateHook, rollbackError)) {
            LogHookError(
                log,
                "rollback pre-gate mid-hook",
                rollbackError);
        } else {
            BG3PreGateTrampoline = nullptr;
        }
        rollbackError.clear();
        if (!RemoveInlineHook(hooks.preYawHook, rollbackError)) {
            LogHookError(
                log,
                "rollback pre-yaw mid-hook",
                rollbackError);
        } else {
            BG3PreYawTrampoline = nullptr;
        }
        rollbackError.clear();
        if (!RemoveInlineHook(hooks.afterZoomHook, rollbackError)) {
            LogHookError(
                log,
                "rollback post-zoom mid-hook",
                rollbackError);
        } else {
            BG3AfterZoomTrampoline = nullptr;
        }
        rollbackError.clear();
        if (!RemoveInlineHook(
                hooks.updateGameCameraBehaviorHook,
                rollbackError)) {
            LogHookError(
                log,
                "rollback UpdateGameCameraBehavior hook",
                rollbackError);
        }
        rollbackError.clear();
        if (!RemoveInlineHook(hooks.cameraInputEventHook, rollbackError)) {
            LogHookError(log, "rollback CameraSystem::OnInputEvent hook", rollbackError);
        }
        rollbackError.clear();
        if (!RemoveInlineHook(hooks.stickHook, rollbackError)) {
            LogHookError(log, "rollback OnStickChanged hook", rollbackError);
        }
        rollbackError.clear();
        if (!RemoveInlineHook(hooks.inputEventHook, rollbackError)) {
            LogHookError(
                log,
                "rollback InputController::OnInputEvent hook",
                rollbackError);
        }
        rollbackError.clear();
        if (!RemoveInlineHook(
                hooks.fireInputEventsHook,
                rollbackError)) {
            LogHookError(
                log,
                "rollback InputManager::FireInputEvents hook",
                rollbackError);
        }

        gOriginalInputEvent = nullptr;
        gOriginalFireInputEvents = nullptr;
        gOriginalOnStickChanged = nullptr;
        gOriginalCameraInputEvent = nullptr;
        gOriginalUpdateGameCameraBehavior = nullptr;
        gPitchTail = nullptr;
        gCameraDefinitionsBasePointer = nullptr;
        gCollideWithObstacles = nullptr;
        gGetFloorLevel = nullptr;
        gCollisionClampActive.store(false, std::memory_order_relaxed);
        ResetFloorConstraint();
    };

    if (!InstallInlineHook(
            hooks.inputEventHook,
            inputEventAddress,
            reinterpret_cast<void*>(&InputEventHook),
            true,
            error)) {
        LogHookError(log, "install InputController::OnInputEvent hook", error);
        rollback();
        return false;
    }

    gOriginalInputEvent =
        reinterpret_cast<InputEventFunction>(
            hooks.inputEventHook.trampoline);

    if (!InstallInlineHook(
            hooks.fireInputEventsHook,
            fireInputEventsAddress,
            reinterpret_cast<void*>(&FireInputEventsHook),
            true,
            error)) {
        LogHookError(log, "install InputManager::FireInputEvents hook", error);
        rollback();
        return false;
    }

    gOriginalFireInputEvents =
        reinterpret_cast<FireInputEventsFunction>(
            hooks.fireInputEventsHook.trampoline);

    if (!InstallInlineHook(
            hooks.stickHook,
            onStickChangedAddress,
            reinterpret_cast<void*>(&OnStickChangedHook),
            true,
            error)) {
        LogHookError(log, "install OnStickChanged hook", error);
        rollback();
        return false;
    }

    gOriginalOnStickChanged =
        reinterpret_cast<OnStickChangedFunction>(
            hooks.stickHook.trampoline);

    if (!InstallInlineHook(
            hooks.cameraInputEventHook,
            cameraInputEventAddress,
            reinterpret_cast<void*>(&CameraInputEventHook),
            true,
            error)) {
        LogHookError(log, "install CameraSystem::OnInputEvent hook", error);
        rollback();
        return false;
    }

    gOriginalCameraInputEvent =
        reinterpret_cast<CameraInputEventFunction>(
            hooks.cameraInputEventHook.trampoline);

    if (!InstallInlineHook(
            hooks.updateGameCameraBehaviorHook,
            updateGameCameraBehaviorAddress,
            reinterpret_cast<void*>(&UpdateGameCameraBehaviorHook),
            true,
            error)) {
        LogHookError(log, "install UpdateGameCameraBehavior hook", error);
        rollback();
        return false;
    }

    gOriginalUpdateGameCameraBehavior =
        reinterpret_cast<UpdateGameCameraBehaviorFunction>(
            hooks.updateGameCameraBehaviorHook.trampoline);

    if (!InstallInlineHook(
            hooks.afterZoomHook,
            afterZoomSiteAddress,
            reinterpret_cast<void*>(&BG3AfterZoomDetour),
            true,
            error,
            &BG3AfterZoomTrampoline)) {
        LogHookError(
            log,
            "install post-zoom mid-hook",
            error);
        rollback();
        return false;
    }

    if (!InstallInlineHook(
            hooks.preYawHook,
            preYawSiteAddress,
            reinterpret_cast<void*>(&BG3PreYawDetour),
            true,
            error,
            &BG3PreYawTrampoline)) {
        LogHookError(
            log,
            "install pre-yaw mid-hook",
            error);
        rollback();
        return false;
    }

    if (!InstallInlineHook(
            hooks.preGateHook,
            preGateSiteAddress,
            reinterpret_cast<void*>(&BG3PreGateDetour),
            true,
            error,
            &BG3PreGateTrampoline)) {
        LogHookError(
            log,
            "install pre-gate mid-hook",
            error);
        rollback();
        return false;
    }

    // The original entry has a 20-byte fast path. Starting at +20 enters the
    // unchanged vanilla calculation without executing our patched entry.
    gPitchTail = reinterpret_cast<PitchTailFunction>(
        getPitchAddress + 20);

    if (!InstallInlineHook(
            hooks.pitchHook,
            getPitchAddress,
            reinterpret_cast<void*>(&GetCameraPitchDegreesHook),
            false,
            error)) {
        LogHookError(log, "install GetCameraPitchDegrees hook", error);
        rollback();
        return false;
    }

#if BG3_CAMERA_WITH_WASD
    if (keyboardMovementRequested) {
        std::uint32_t guardInstruction{};
        std::memcpy(
            &guardInstruction,
            keyboardMovementGuardAddress + 8,
            sizeof(guardInstruction));
        if ((guardInstruction & kArm64CbzW8Mask) != kArm64CbzW8) {
            std::fprintf(
                log,
                "[ABORT] keyboard movement guard is not CBZ W8: 0x%08x\n",
                guardInstruction);
            rollback();
            return false;
        }

        error.clear();
        if (!InstallInstructionPatch(
                hooks.keyboardMovementPatch,
                keyboardMovementGuardAddress + 8,
                kArm64Nop,
                error)) {
            LogHookError(log, "install keyboard movement patch", error);
            rollback();
            return false;
        }
        std::fprintf(
            log,
            "[INFO] keyboard movement guard disabled at %p; controller path "
            "unchanged\n",
            static_cast<void*>(keyboardMovementGuardAddress + 8));
    }
#endif  // BG3_CAMERA_WITH_WASD

    std::fprintf(
        log,
        "[ARMED] transactional hooks installed dormant; waiting for "
        "CameraDefinition initialization\n");
    std::fflush(log);
    return true;
}

void RemoveCameraHooks(CameraHookSet& hooks, FILE* log) {
    gHooksEnabled.store(false, std::memory_order_release);

    StopRawTrackpadMonitor();
    if (log != nullptr) {
        std::fprintf(
            log,
            "[INFO] raw-trackpad prototype: fedEvents=%lu signedTravel=%.3f "
            "absTravel=%.3f momentumEvents=%lu gestureEnds=%lu "
            "discardedPending=%.3f mmbPitchEvents=%lu mmbPitchSignedTravel=%.3f"
            "\n",
            gRawTrackpadFedEvents.load(std::memory_order_relaxed),
            static_cast<double>(
                gRawTrackpadSignedTravel.load(std::memory_order_relaxed)),
            static_cast<double>(
                gRawTrackpadAbsTravel.load(std::memory_order_relaxed)),
            gRawTrackpadMomentumEvents.load(std::memory_order_relaxed),
            gRawTrackpadGestureEnds.load(std::memory_order_relaxed),
            static_cast<double>(
                gRawTrackpadDiscardedPending.load(
                    std::memory_order_relaxed)),
            gMousePitchEvents.load(std::memory_order_relaxed),
            static_cast<double>(
                gMousePitchSignedTravel.load(std::memory_order_relaxed)));
    }

    if (gCameraDefinitionsBasePointer != nullptr) {
        std::lock_guard lock(gCameraDefinitionMutex);
        void* definitionsBase = nullptr;
        std::memcpy(
            &definitionsBase,
            gCameraDefinitionsBasePointer,
            sizeof(definitionsBase));
        RestoreZoomBounds(definitionsBase);
    }

    std::string error;
    if (!RemoveInstructionPatch(hooks.keyboardMovementPatch, error) &&
        log != nullptr) {
        LogHookError(log, "remove keyboard movement patch", error);
    }

    error.clear();
    if (!RemoveInlineHook(hooks.pitchHook, error) && log != nullptr) {
        LogHookError(log, "remove GetCameraPitchDegrees hook", error);
    }

    error.clear();
    if (!RemoveInlineHook(hooks.preGateHook, error)) {
        if (log != nullptr) {
            LogHookError(
                log,
                "remove pre-gate mid-hook",
                error);
        }
    } else {
        BG3PreGateTrampoline = nullptr;
    }

    error.clear();
    if (!RemoveInlineHook(hooks.preYawHook, error)) {
        if (log != nullptr) {
            LogHookError(
                log,
                "remove pre-yaw mid-hook",
                error);
        }
    } else {
        BG3PreYawTrampoline = nullptr;
    }

    error.clear();
    if (!RemoveInlineHook(hooks.afterZoomHook, error)) {
        if (log != nullptr) {
            LogHookError(
                log,
                "remove post-zoom mid-hook",
                error);
        }
    } else {
        BG3AfterZoomTrampoline = nullptr;
    }

    error.clear();
    if (!RemoveInlineHook(
            hooks.updateGameCameraBehaviorHook,
            error) &&
        log != nullptr) {
        LogHookError(log, "remove UpdateGameCameraBehavior hook", error);
    }

    error.clear();
    if (!RemoveInlineHook(hooks.cameraInputEventHook, error) && log != nullptr) {
        LogHookError(log, "remove CameraSystem::OnInputEvent hook", error);
    }

    error.clear();
    if (!RemoveInlineHook(hooks.stickHook, error) && log != nullptr) {
        LogHookError(log, "remove OnStickChanged hook", error);
    }

    error.clear();
    if (!RemoveInlineHook(hooks.inputEventHook, error) && log != nullptr) {
        LogHookError(
            log,
            "remove InputController::OnInputEvent hook",
            error);
    }

    error.clear();
    if (!RemoveInlineHook(hooks.fireInputEventsHook, error) && log != nullptr) {
        LogHookError(
            log,
            "remove InputManager::FireInputEvents hook",
            error);
    }

    gOriginalOnStickChanged = nullptr;
    gOriginalInputEvent = nullptr;
    gOriginalFireInputEvents = nullptr;
    gOriginalCameraInputEvent = nullptr;
    gOriginalUpdateGameCameraBehavior = nullptr;
    gPitchTail = nullptr;
    gRightStickY.store(0.0f, std::memory_order_relaxed);
    gLastMfiInputDevice.store(nullptr, std::memory_order_relaxed);
    gLastMfiSampleTime.store(0, std::memory_order_relaxed);
    gMfiL3Pressed.store(false, std::memory_order_relaxed);
    gPointerZoomModifierHeld.store(false, std::memory_order_relaxed);
    gCollisionClampActive.store(false, std::memory_order_relaxed);
    DiscardWheelTravel();
    ResetFloorConstraint();
    {
        std::lock_guard lock(gDeferredToggleMutex);
        gDeferredToggle = {};
    }
    gCameraDefinitionsBasePointer = nullptr;
    gCollideWithObstacles = nullptr;
    gGetFloorLevel = nullptr;

    // The totals are the point of this telemetry; printing them once here
    // means a session that never triggered a heartbeat still reports what it
    // observed.
    LogPreGateTotals("shutdown");
    if (log != nullptr) {
        std::fprintf(
            log,
            "[INFO] pre-yaw identity totals match=%lu mismatch=%lu "
            "noPreYaw=%lu multiHit=%lu outsideUpdate=%lu\n",
            gIdentityMatchCount,
            gIdentityMismatchCount,
            gIdentityNoPreYawCount,
            gIdentityMultiHitCount,
            gPreYawOutsideUpdateCount.load(std::memory_order_relaxed));
        std::fflush(log);
    }

    gRuntimeLog = nullptr;
}

}  // namespace bg3cam
