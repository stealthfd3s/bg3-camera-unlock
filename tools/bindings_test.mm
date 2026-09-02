// Unit tests for the launcher's pure input-binding merge / restore logic.
// Foundation only — no AppKit, no game, no files.
//
//   ctest -R bindings_test --output-on-failure

#import <Foundation/Foundation.h>

#import "InputBindings.hpp"

using bg3cam::BindingFlavor;
using bg3cam::ComputeBindingPlan;
using bg3cam::ModInputBindings;
using bg3cam::ShouldWriteProfile;

static int gFailures = 0;
static int gChecks = 0;

static void Check(bool condition, const char* what) {
    ++gChecks;
    if (!condition) {
        ++gFailures;
        fprintf(stderr, "FAIL: %s\n", what);
    }
}

#define CHECK(cond) Check((cond), #cond)

// Apply a plan's profile output the way the launcher would, so the next
// ComputeBindingPlan sees the state a real second launch would.
static NSDictionary* ApplyPlan(NSDictionary* profile, BG3BindingPlan* plan) {
    return plan.profileToWrite != nil ? plan.profileToWrite : profile;
}

static bool Contains(NSArray<NSString*>* array, NSString* value) {
    return [array containsObject:value];
}

int main() {
    @autoreleasepool {
        NSArray<NSString*>* thirdParty = @[@"key:space", @"key:space", @"key:space"];

        // --- 1. Clean profile, Camera + WASD: everything applied, idempotent
        {
            NSDictionary* clean = @{@"Jump": thirdParty};
            BG3BindingPlan* p1 = ComputeBindingPlan(clean, nil, BindingFlavor::CameraWasd);
            CHECK(p1.profileToWrite != nil);
            CHECK(p1.appliedActions.count == ModInputBindings(BindingFlavor::CameraWasd).count);
            CHECK(p1.restoredActions.count == 0);
            CHECK([p1.profileToWrite[@"Jump"] isEqual:thirdParty]);
            CHECK([p1.profileToWrite[@"CharacterMoveForward"] isEqual:
                   ModInputBindings(BindingFlavor::CameraWasd)[@"CharacterMoveForward"]]);

            NSDictionary* after = ApplyPlan(clean, p1);
            BG3BindingPlan* p2 = ComputeBindingPlan(after, p1.stateToWrite, BindingFlavor::CameraWasd);
            CHECK(p2.profileToWrite == nil);       // idempotent
            CHECK(p2.stateChanged == NO);
            CHECK(p2.appliedActions.count == 0);
        }

        // --- 2. Clean profile, Camera Only: only the four common actions
        {
            NSDictionary* clean = @{@"Jump": thirdParty};
            BG3BindingPlan* p = ComputeBindingPlan(clean, nil, BindingFlavor::CameraOnly);
            CHECK(p.appliedActions.count == 4);
            CHECK(p.profileToWrite[@"CameraRotateLeft"] != nil);
            CHECK(p.profileToWrite[@"CharacterMoveForward"] == nil);
            CHECK(p.profileToWrite[@"CameraForward"] == nil);
            CHECK([p.profileToWrite[@"Jump"] isEqual:thirdParty]);
        }

        // --- 3. Transition Camera + WASD -> Camera Only
        {
            NSDictionary* clean = @{@"Jump": thirdParty};
            BG3BindingPlan* wasd = ComputeBindingPlan(clean, nil, BindingFlavor::CameraWasd);
            NSDictionary* afterWasd = ApplyPlan(clean, wasd);

            BG3BindingPlan* only = ComputeBindingPlan(afterWasd, wasd.stateToWrite, BindingFlavor::CameraOnly);
            CHECK(only.profileToWrite != nil);
            // CharacterMove* + Camera* pan had no previous on a clean profile,
            // so they are removed, i.e. handed back to the game default.
            CHECK(only.profileToWrite[@"CharacterMoveForward"] == nil);
            CHECK(only.profileToWrite[@"CameraForward"] == nil);
            CHECK(Contains(only.restoredActions, @"CharacterMoveForward"));
            CHECK(Contains(only.restoredActions, @"CameraLeft"));
            // The four common actions are still there.
            CHECK(only.profileToWrite[@"CameraRotateRight"] != nil);
            CHECK([only.profileToWrite[@"Jump"] isEqual:thirdParty]);

            // Idempotent afterwards.
            NSDictionary* afterOnly = ApplyPlan(afterWasd, only);
            BG3BindingPlan* again = ComputeBindingPlan(afterOnly, only.stateToWrite, BindingFlavor::CameraOnly);
            CHECK(again.profileToWrite == nil);
            CHECK(again.stateChanged == NO);

            // --- 4. Reverse transition Camera Only -> Camera + WASD, twice
            BG3BindingPlan* back = ComputeBindingPlan(afterOnly, only.stateToWrite, BindingFlavor::CameraWasd);
            CHECK(back.profileToWrite[@"CharacterMoveForward"] != nil);
            CHECK(back.restoredActions.count == 0);
            NSDictionary* afterBack = ApplyPlan(afterOnly, back);
            BG3BindingPlan* backAgain = ComputeBindingPlan(afterBack, back.stateToWrite, BindingFlavor::CameraWasd);
            CHECK(backAgain.profileToWrite == nil);   // no accumulation
        }

        // --- 5. User edits a mod-managed action, then switches flavor
        {
            NSDictionary* clean = @{};
            BG3BindingPlan* wasd = ComputeBindingPlan(clean, nil, BindingFlavor::CameraWasd);
            NSMutableDictionary* edited = [ApplyPlan(clean, wasd) mutableCopy];
            edited[@"CharacterMoveForward"] = @[@"c:leftstick_ypos", @"key:x"];  // player changed W -> X

            BG3BindingPlan* only = ComputeBindingPlan(edited, wasd.stateToWrite, BindingFlavor::CameraOnly);
            // The edited action is left exactly as the player set it.
            CHECK([only.profileToWrite[@"CharacterMoveForward"] isEqual:
                   (@[@"c:leftstick_ypos", @"key:x"])]);
            CHECK(Contains(only.keptUserEdits, @"CharacterMoveForward"));
            CHECK(!Contains(only.restoredActions, @"CharacterMoveForward"));
            // A sibling the player did not touch is still restored.
            CHECK(only.profileToWrite[@"CharacterMoveBackward"] == nil);
            CHECK(Contains(only.restoredActions, @"CharacterMoveBackward"));
        }

        // --- 6. A pre-existing value for a mod action is remembered and restored
        {
            NSDictionary* preExisting = @{
                @"CharacterMoveForward": @[@"c:leftstick_ypos", @"key:z"],
                @"Jump": thirdParty,
            };
            BG3BindingPlan* wasd = ComputeBindingPlan(preExisting, nil, BindingFlavor::CameraWasd);
            CHECK([wasd.profileToWrite[@"CharacterMoveForward"] isEqual:
                   ModInputBindings(BindingFlavor::CameraWasd)[@"CharacterMoveForward"]]);
            NSDictionary* afterWasd = ApplyPlan(preExisting, wasd);

            BG3BindingPlan* only = ComputeBindingPlan(afterWasd, wasd.stateToWrite, BindingFlavor::CameraOnly);
            // Restored to the player's own prior value, not removed.
            CHECK([only.profileToWrite[@"CharacterMoveForward"] isEqual:
                   (@[@"c:leftstick_ypos", @"key:z"])]);
            CHECK([only.profileToWrite[@"Jump"] isEqual:thirdParty]);
        }

        // --- 7. Backup-failure contract
        {
            BG3BindingPlan* p = ComputeBindingPlan(@{}, nil, BindingFlavor::CameraWasd);
            CHECK(p.profileToWrite != nil);
            CHECK(ShouldWriteProfile(p, NO) == NO);   // no backup -> do not write
            CHECK(ShouldWriteProfile(p, YES) == YES);
            BG3BindingPlan* noop = ComputeBindingPlan(ApplyPlan(@{}, p), p.stateToWrite, BindingFlavor::CameraWasd);
            CHECK(ShouldWriteProfile(noop, YES) == NO);  // nothing to write
        }

        // --- 8. Empty / nil profile is handled
        {
            BG3BindingPlan* p = ComputeBindingPlan(nil, nil, BindingFlavor::CameraOnly);
            CHECK(p.profileToWrite != nil);
            CHECK(p.appliedActions.count == 4);
        }

        fprintf(stderr, "%d checks, %d failures\n", gChecks, gFailures);
        return gFailures == 0 ? 0 : 1;
    }
}
