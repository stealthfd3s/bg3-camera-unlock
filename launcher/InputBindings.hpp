#pragma once

#import <Foundation/Foundation.h>

// Pure logic for merging this mod's input bindings into BG3's
// inputconfig_p1.json, and for backing those changes out again when the
// player switches flavor. No AppKit, no file I/O, no globals — everything
// here is a deterministic function of its arguments so it can be unit tested
// without the game (see tools/bindings_test.mm).

// What a launch should do to the profile. Declared at global scope because
// Objective-C interfaces cannot live inside a C++ namespace.
@interface BG3BindingPlan : NSObject
// Profile dictionary to write back, or nil when no profile write is needed.
@property(nonatomic, strong) NSDictionary* profileToWrite;
// Sidecar state to persist. Records, per mod-managed action, the value the
// mod wrote and the value that was there before it.
@property(nonatomic, strong) NSDictionary* stateToWrite;
// Whether stateToWrite differs from the prior state and should be saved.
@property(nonatomic, assign) BOOL stateChanged;
// Diagnostics for the launcher log.
@property(nonatomic, copy) NSArray<NSString*>* appliedActions;
@property(nonatomic, copy) NSArray<NSString*>* restoredActions;
@property(nonatomic, copy) NSArray<NSString*>* keptUserEdits;
@end

namespace bg3cam {

enum class BindingFlavor {
    CameraOnly,   // BG3_CAMERA_WITH_WASD == 0
    CameraWasd,   // BG3_CAMERA_WITH_WASD == 1
};

// The flavor this launcher was compiled for.
BindingFlavor BuiltBindingFlavor();

// "camera-only" / "camera-wasd".
NSString* BindingFlavorIdentifier(BindingFlavor flavor);

// The bindings this flavor writes into the profile, keyed by BG3 action name.
// Each value is positional by device slot: [controller, keyboard, mouse].
// The controller slot always carries BG3's own token so controller play is
// unaffected. Actions absent from this map keep their game defaults.
NSDictionary<NSString*, NSArray<NSString*>*>* ModInputBindings(
    BindingFlavor flavor);

// Compute the plan.
//
//   currentProfile : parsed inputconfig_p1.json (an NSDictionary), or nil.
//   priorState     : the sidecar this launcher last wrote, or nil on a first
//                    run / after a manual delete.
//   flavor         : the target flavor (normally BuiltBindingFlavor()).
//
// Rules:
//   * Apply this flavor's bindings for every action whose current profile
//     value is not already exactly what we would write.
//   * For any action the mod previously managed but this flavor does not,
//     restore it — to the recorded previous value, or by removing the key
//     when there was no previous — but only if the profile still holds
//     exactly what the mod last wrote. If the player has since changed it,
//     leave their value alone and stop managing it.
//   * Never read or write any other key. Third-party bindings are untouched.
//
// Idempotent: ComputeBindingPlan(plan.profileToWrite, plan.stateToWrite,
// flavor) reports nothing to do.
BG3BindingPlan* ComputeBindingPlan(
    NSDictionary* currentProfile,
    NSDictionary* priorState,
    BindingFlavor flavor);

// The profile should be written only when the plan changed it AND a full
// one-time snapshot backup of the original file either already exists or was
// just taken. A failed backup means the profile is left untouched.
BOOL ShouldWriteProfile(BG3BindingPlan* plan, BOOL backupInPlace);

}  // namespace bg3cam
