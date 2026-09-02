#import "InputBindings.hpp"

#ifndef BG3_CAMERA_WITH_WASD
#define BG3_CAMERA_WITH_WASD 1
#endif

@implementation BG3BindingPlan
@end

namespace bg3cam {

namespace {

NSString* const kStateSchemaKey = @"schema";
NSString* const kStateFlavorKey = @"flavor";
NSString* const kStateManagedKey = @"managed";
NSString* const kRecordAppliedKey = @"applied";
NSString* const kRecordHadPreviousKey = @"hadPrevious";
NSString* const kRecordPreviousKey = @"previous";
constexpr NSInteger kStateSchema = 1;

// Positional device slots for a binding value: [controller, keyboard, mouse].
//
// The two left-stick families do not share a Y convention, and both halves of
// that were established by running the game rather than by reading a token
// name:
//
//   CharacterMoveForward -> c:leftstick_ypos   (push away walks forward)
//   CameraForward        -> c:leftstick_yneg   (written ypos, tactical pan
//                                               came out inverted)
//
// Neither is derivable from the other, so neither may be "corrected" to
// match its neighbour.
NSDictionary<NSString*, NSArray<NSString*>*>* CommonBindings() {
    return @{
        // Vertical scroll keeps the zoom action; the mod turns it into pitch
        // itself. The mouse wheel and PageUp/PageDown are added so trackpad
        // and keyboard reach the same action.
        @"CameraZoomIn":
            @[@"c:rightstick_ypos", @"key:pageup", @"mouse:wheel_yneg"],
        @"CameraZoomOut":
            @[@"c:rightstick_yneg", @"key:pagedown", @"mouse:wheel_ypos"],
        // rightstick_xneg is BG3's own "rotate right", not left. A horizontal
        // two-finger swipe only rotates once wheel_xpos/wheel_xneg are bound
        // to these actions.
        @"CameraRotateLeft":
            @[@"c:rightstick_xpos", @"key:e", @"mouse:wheel_xpos"],
        @"CameraRotateRight":
            @[@"c:rightstick_xneg", @"key:q", @"mouse:wheel_xneg"],
    };
}

// Camera + WASD only. Panning is written out in full because leaving these
// actions absent makes BG3 silently restore its WASD defaults for them —
// which is exactly what would put WASD back on the camera. The controller
// slot keeps the left stick so tactical panning still has something to drive
// it. One token feeds several actions at once, so keeping the left stick here
// costs the character movement below nothing.
NSDictionary<NSString*, NSArray<NSString*>*>* WasdBindings() {
    return @{
        @"CameraForward": @[@"c:leftstick_yneg", @"key:up", @"key:up"],
        @"CameraBackward": @[@"c:leftstick_ypos", @"key:down", @"key:down"],
        @"CameraLeft": @[@"c:leftstick_xneg", @"key:left", @"key:left"],
        @"CameraRight": @[@"c:leftstick_xpos", @"key:right", @"key:right"],
        @"CharacterMoveForward": @[@"c:leftstick_ypos", @"key:w"],
        @"CharacterMoveBackward": @[@"c:leftstick_yneg", @"key:s"],
        @"CharacterMoveLeft": @[@"c:leftstick_xneg", @"key:a"],
        @"CharacterMoveRight": @[@"c:leftstick_xpos", @"key:d"],
    };
}

}  // namespace

BindingFlavor BuiltBindingFlavor() {
#if BG3_CAMERA_WITH_WASD
    return BindingFlavor::CameraWasd;
#else
    return BindingFlavor::CameraOnly;
#endif
}

NSString* BindingFlavorIdentifier(BindingFlavor flavor) {
    return flavor == BindingFlavor::CameraWasd ? @"camera-wasd" : @"camera-only";
}

NSDictionary<NSString*, NSArray<NSString*>*>* ModInputBindings(
    BindingFlavor flavor) {
    NSMutableDictionary* bindings = [CommonBindings() mutableCopy];
    if (flavor == BindingFlavor::CameraWasd) {
        [bindings addEntriesFromDictionary:WasdBindings()];
    }
    return bindings;
}

BG3BindingPlan* ComputeBindingPlan(
    NSDictionary* currentProfile,
    NSDictionary* priorState,
    BindingFlavor flavor) {
    NSDictionary* desired = ModInputBindings(flavor);

    NSDictionary* priorManaged = nil;
    if ([priorState isKindOfClass:[NSDictionary class]]) {
        id managed = priorState[kStateManagedKey];
        if ([managed isKindOfClass:[NSDictionary class]]) {
            priorManaged = managed;
        }
    }

    NSMutableDictionary* profile = [(currentProfile
        ? currentProfile
        : @{}) mutableCopy];
    NSMutableDictionary* newManaged = [NSMutableDictionary dictionary];
    NSMutableArray<NSString*>* applied = [NSMutableArray array];
    NSMutableArray<NSString*>* restored = [NSMutableArray array];
    NSMutableArray<NSString*>* keptUserEdits = [NSMutableArray array];
    BOOL profileChanged = NO;

    // --- Restore pass: actions the mod used to manage but this flavor does not
    for (NSString* action in priorManaged) {
        if (desired[action] != nil) {
            continue;
        }
        NSDictionary* record = priorManaged[action];
        if (![record isKindOfClass:[NSDictionary class]]) {
            continue;
        }
        id modWrote = record[kRecordAppliedKey];
        id current = profile[action];
        BOOL profileStillHoldsModValue =
            (current != nil) && [current isEqual:modWrote];

        if (!profileStillHoldsModValue) {
            // The player has changed this since; it is theirs now.
            if (current != nil) {
                [keptUserEdits addObject:action];
            }
            continue;
        }

        BOOL hadPrevious = [record[kRecordHadPreviousKey] boolValue];
        if (hadPrevious) {
            id previous = record[kRecordPreviousKey];
            if (previous != nil && ![current isEqual:previous]) {
                profile[action] = previous;
                profileChanged = YES;
            }
        } else if (profile[action] != nil) {
            [profile removeObjectForKey:action];
            profileChanged = YES;
        }
        [restored addObject:action];
    }

    // --- Apply pass: this flavor's bindings
    for (NSString* action in desired) {
        id want = desired[action];
        id current = profile[action];

        // Record the true pre-mod value. If we already managed this action,
        // carry the original previous forward rather than recording our own
        // applied value as the "previous".
        BOOL hadPrevious;
        id previous;
        NSDictionary* priorRecord = priorManaged[action];
        if ([priorRecord isKindOfClass:[NSDictionary class]]) {
            hadPrevious = [priorRecord[kRecordHadPreviousKey] boolValue];
            previous = priorRecord[kRecordPreviousKey];
        } else {
            id fromFile = currentProfile[action];
            hadPrevious = (fromFile != nil);
            previous = fromFile;
        }

        NSMutableDictionary* record = [NSMutableDictionary dictionary];
        record[kRecordAppliedKey] = want;
        record[kRecordHadPreviousKey] = @(hadPrevious);
        if (hadPrevious && previous != nil) {
            record[kRecordPreviousKey] = previous;
        }
        newManaged[action] = record;

        if (current == nil || ![current isEqual:want]) {
            profile[action] = want;
            profileChanged = YES;
            [applied addObject:action];
        }
    }

    NSDictionary* newState = @{
        kStateSchemaKey: @(kStateSchema),
        kStateFlavorKey: BindingFlavorIdentifier(flavor),
        kStateManagedKey: newManaged,
    };

    BG3BindingPlan* plan = [[BG3BindingPlan alloc] init];
    plan.profileToWrite = profileChanged ? profile : nil;
    plan.stateToWrite = newState;
    plan.stateChanged =
        (priorState == nil) || ![priorState isEqualToDictionary:newState];
    plan.appliedActions = applied;
    plan.restoredActions = restored;
    plan.keptUserEdits = keptUserEdits;
    return plan;
}

BOOL ShouldWriteProfile(BG3BindingPlan* plan, BOOL backupInPlace) {
    return plan != nil && plan.profileToWrite != nil && backupInPlace;
}

}  // namespace bg3cam
