#pragma once

// In-process macOS scroll- and mouse-event monitor.
//
// It reads the raw horizontal two-finger trackpad delta *before* BG3 turns it
// into its 107/108 camera actions, applies BG3's own x0.1 scale, runs a
// gesture lifecycle (feed while held, stop on lift) and a gesture-level axis
// lock, then feeds the result into the pointer-X accumulator / pre-gate
// transport. It also forwards middle-mouse-button drag for vertical look.
//
// This is a normal part of every build. The event is never modified and is
// always returned, so UI scrolling and BG3's own handling are untouched.

namespace bg3cam {

// Installs the NSEvent scrollWheel monitor on the main thread. Called once,
// right after gHooksEnabled is set. Idempotent.
void StartRawTrackpadMonitor();

// Removes the monitor on the main thread and marks it inactive. Called from
// RemoveCameraHooks. Idempotent.
void StopRawTrackpadMonitor();

// True once the monitor is installed and the raw path is live. Branch C in
// CameraInputEventHook consults this to stop double-counting 107/108.
bool RawTrackpadMonitorActive();

// Implemented in CameraHooks.cpp next to the other pointer-X transport. Called
// on the main thread for every precise-scroll event, including dx=0 events at
// phase Ended/Cancelled. dx/dy are raw NSEvent.scrollingDelta{X,Y}; phase and
// momentumPhase are the raw NSEventPhase option-set bitmasks.
void RawTrackpadScroll(
    double deltaX,
    double deltaY,
    unsigned long phase,
    unsigned long momentumPhase);

// Sets the active flag. Implemented in CameraHooks.cpp so the flag lives with
// the transport it gates; called by StartRawTrackpadMonitor / Stop.
void SetRawTrackpadMonitorActive(bool active);

// Mouse middle-drag vertical look. Called on the main thread from the NSEvent
// monitor for OtherMouseDragged / OtherMouseUp with buttonNumber == 2 only.
// MouseMiddleDragged takes the vertical delta (horizontal is left to BG3's
// native mouse yaw); MouseMiddleUp clears the accumulator on release.
void MouseMiddleDragged(double deltaX, double deltaY);
void MouseMiddleUp();

}  // namespace bg3cam
