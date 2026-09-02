#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#import "InputBindings.hpp"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <map>
#include <spawn.h>
#include <string>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char** environ;

#ifndef BG3_CAMERA_VERSION
#define BG3_CAMERA_VERSION "development"
#endif
#ifndef BG3_CAMERA_FLAVOR
#define BG3_CAMERA_FLAVOR "camera-wasd"
#endif
#ifndef BG3_CAMERA_FLAVOR_NAME
#define BG3_CAMERA_FLAVOR_NAME "Camera + WASD"
#endif
#ifndef BG3_CAMERA_WITH_WASD
#define BG3_CAMERA_WITH_WASD 1
#endif
// Basename (no extension) of this flavor's default config resource in the
// bundle, e.g. "BG3CameraUnlock-camera-wasd".
#ifndef BG3_CAMERA_DEFAULT_CONFIG_RESOURCE
#define BG3_CAMERA_DEFAULT_CONFIG_RESOURCE "BG3CameraUnlock-camera-wasd"
#endif

namespace {

NSString* const kGameBundleIdentifier = @"com.larian.bg3";
NSString* const kSteamBundleIdentifier = @"com.valvesoftware.steam";
NSString* const kStoredGamePathKey = @"BG3GameApplicationPath";
CFStringRef const kNoLauncherPreference = CFSTR("NoLauncher");

struct PreferenceSnapshot {
    CFPropertyListRef value = nullptr;
};

void AppendLauncherLog(NSString* message);

void ShowAlert(NSString* title, NSString* message, NSAlertStyle style) {
    [NSApp activateIgnoringOtherApps:YES];
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = title;
    alert.informativeText = message;
    alert.alertStyle = style;
    [alert addButtonWithTitle:@"OK"];
    [alert runModal];
}

// One visible folder for everything the user is expected to open or send.
//
// The config and the logs used to sit in ~/Library, in two different places -
// correct by Apple's conventions, and useless in practice: the folder is
// hidden in Finder, and asking someone to find a file there is asking them to
// paste a path they cannot see.
NSString* ModSupportDirectory() {
    NSArray<NSString*>* paths = NSSearchPathForDirectoriesInDomains(
        NSDocumentDirectory,
        NSUserDomainMask,
        YES);
    if (paths.count == 0) {
        return [@"~/Documents/BG3 Camera Unlock" stringByExpandingTildeInPath];
    }
    return [paths.firstObject stringByAppendingPathComponent:
        @"BG3 Camera Unlock"];
}

NSString* LogsDirectory() {
    return ModSupportDirectory();
}

NSString* ApplicationSupportDirectory() {
    return ModSupportDirectory();
}

NSString* InputConfigPath();

// Puts a link to the game's input bindings beside this mod's own config, so
// both files are reachable from one folder instead of one of them living
// several levels down inside Documents.
void LinkInputConfig() {
    NSString* target = InputConfigPath();
    if (target == nil) {
        return;
    }

    NSFileManager* files = [NSFileManager defaultManager];
    NSString* link = [ModSupportDirectory()
        stringByAppendingPathComponent:@"inputconfig_p1.json"];

    // A stale link is replaced; a real file at that path is left alone, since
    // it would be somebody's own and not ours to remove.
    NSString* existing =
        [files destinationOfSymbolicLinkAtPath:link error:nil];
    if (existing != nil) {
        if ([existing isEqualToString:target]) {
            return;
        }
        [files removeItemAtPath:link error:nil];
    } else if ([files fileExistsAtPath:link]) {
        return;
    }

    if (![files fileExistsAtPath:target]) {
        return;
    }

    NSError* linkError = nil;
    if ([files createSymbolicLinkAtPath:link
                    withDestinationPath:target
                                  error:&linkError]) {
        AppendLauncherLog(@"Linked the game's input bindings into the mod folder");
    } else {
        AppendLauncherLog([NSString stringWithFormat:
            @"Could not link the input bindings (%@)",
            linkError.localizedDescription]);
    }
}

// Moves anything left in the old ~/Library locations into the new folder.
//
// Copied rather than deleted where a copy already exists: an install that has
// been run before keeps whatever it is using now, and nothing is lost either
// way. Runs once per launch and is silent when there is nothing to move.
void MigrateLegacyFiles() {
    NSFileManager* files = [NSFileManager defaultManager];
    NSString* destination = ModSupportDirectory();
    if (![files createDirectoryAtPath:destination
          withIntermediateDirectories:YES
                           attributes:nil
                                error:nil]) {
        return;
    }

    NSArray<NSString*>* libraryPaths = NSSearchPathForDirectoriesInDomains(
        NSLibraryDirectory, NSUserDomainMask, YES);
    NSArray<NSString*>* supportPaths = NSSearchPathForDirectoriesInDomains(
        NSApplicationSupportDirectory, NSUserDomainMask, YES);
    if (libraryPaths.count == 0 || supportPaths.count == 0) {
        return;
    }

    NSArray<NSString*>* sources = @[
        [libraryPaths.firstObject stringByAppendingPathComponent:
            @"Logs/BG3 Camera Unlock"],
        [supportPaths.firstObject stringByAppendingPathComponent:
            @"BG3 Camera Unlock"],
    ];

    for (NSString* source in sources) {
        if (![files fileExistsAtPath:source]) {
            continue;
        }
        NSArray<NSString*>* names =
            [files contentsOfDirectoryAtPath:source error:nil];
        for (NSString* name in names) {
            if ([name hasPrefix:@"."]) {
                continue;
            }
            NSString* from = [source stringByAppendingPathComponent:name];
            NSString* to = [destination stringByAppendingPathComponent:name];
            if ([files fileExistsAtPath:to]) {
                continue;
            }
            [files moveItemAtPath:from toPath:to error:nil];
        }
    }
}

NSString* EnsureUserConfig() {
    NSString* directory = ApplicationSupportDirectory();
    NSError* directoryError = nil;
    if (![[NSFileManager defaultManager]
            createDirectoryAtPath:directory
            withIntermediateDirectories:YES
            attributes:nil
            error:&directoryError]) {
        AppendLauncherLog([NSString stringWithFormat:
            @"Config directory creation failed: %@",
            directoryError.localizedDescription]);
        return nil;
    }

    NSString* userConfig =
        [directory stringByAppendingPathComponent:@"config.ini"];
    if ([[NSFileManager defaultManager] fileExistsAtPath:userConfig]) {
        return userConfig;
    }

    NSString* bundledConfig = [[NSBundle mainBundle]
        pathForResource:@BG3_CAMERA_DEFAULT_CONFIG_RESOURCE
        ofType:@"ini"];
    if (bundledConfig == nil) {
        AppendLauncherLog(@"Bundled default config is missing");
        return nil;
    }

    NSError* copyError = nil;
    if (![[NSFileManager defaultManager]
            copyItemAtPath:bundledConfig
            toPath:userConfig
            error:&copyError]) {
        AppendLauncherLog([NSString stringWithFormat:
            @"Default config copy failed: %@",
            copyError.localizedDescription]);
        return nil;
    }

    AppendLauncherLog([NSString stringWithFormat:
        @"Created default config at %@",
        userConfig]);
    return userConfig;
}

void AppendLauncherLog(NSString* message) {
    NSString* directory = LogsDirectory();
    [[NSFileManager defaultManager]
        createDirectoryAtPath:directory
        withIntermediateDirectories:YES
        attributes:nil
        error:nil];

    NSString* path = [directory stringByAppendingPathComponent:@"launcher.log"];
    NSDateFormatter* formatter = [[NSDateFormatter alloc] init];
    formatter.locale = [NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"];
    formatter.dateFormat = @"yyyy-MM-dd HH:mm:ss";
    NSString* line = [NSString stringWithFormat:
        @"[%@] %@\n",
        [formatter stringFromDate:[NSDate date]],
        message];
    NSData* data = [line dataUsingEncoding:NSUTF8StringEncoding];

    if (![[NSFileManager defaultManager] fileExistsAtPath:path]) {
        [data writeToFile:path atomically:YES];
        return;
    }

    NSFileHandle* handle = [NSFileHandle fileHandleForWritingAtPath:path];
    [handle seekToEndOfFile];
    [handle writeData:data];
    [handle closeFile];
}

bool IsRunningUnderRosetta() {
    int translated = 0;
    std::size_t size = sizeof(translated);
    return sysctlbyname(
               "sysctl.proc_translated",
               &translated,
               &size,
               nullptr,
               0) == 0 &&
        translated == 1;
}

bool ResolveGameExecutable(NSString* appPath, NSString** executablePath) {
    NSBundle* bundle = [NSBundle bundleWithPath:appPath];
    if (bundle == nil ||
        ![bundle.bundleIdentifier isEqualToString:kGameBundleIdentifier]) {
        return false;
    }

    NSString* executable = bundle.executablePath;
    if (executable.length == 0 ||
        ![[NSFileManager defaultManager] isExecutableFileAtPath:executable]) {
        return false;
    }

    if (executablePath != nullptr) {
        *executablePath = executable;
    }
    return true;
}

void AddCandidate(
    NSMutableArray<NSString*>* candidates,
    NSMutableSet<NSString*>* seen,
    NSString* path) {
    NSString* standardized = path.stringByStandardizingPath;
    if (standardized.length == 0 || [seen containsObject:standardized]) {
        return;
    }
    [seen addObject:standardized];
    [candidates addObject:standardized];
}

NSArray<NSString*>* SteamLibraryRoots() {
    NSString* steamRoot = [@"~/Library/Application Support/Steam"
        stringByExpandingTildeInPath];
    NSMutableArray<NSString*>* roots = [NSMutableArray arrayWithObject:steamRoot];

    NSString* vdfPath = [steamRoot
        stringByAppendingPathComponent:@"steamapps/libraryfolders.vdf"];
    NSError* readError = nil;
    NSString* vdf = [NSString
        stringWithContentsOfFile:vdfPath
        encoding:NSUTF8StringEncoding
        error:&readError];
    if (vdf == nil) {
        return roots;
    }

    NSRegularExpression* regex = [NSRegularExpression
        regularExpressionWithPattern:@"\\\"path\\\"\\s+\\\"([^\\\"]+)\\\""
        options:0
        error:nil];
    NSArray<NSTextCheckingResult*>* matches = [regex
        matchesInString:vdf
        options:0
        range:NSMakeRange(0, vdf.length)];

    for (NSTextCheckingResult* match in matches) {
        NSString* root = [vdf substringWithRange:[match rangeAtIndex:1]];
        root = [root stringByReplacingOccurrencesOfString:@"\\\\" withString:@"\\"];
        if (root.length != 0 && ![roots containsObject:root]) {
            [roots addObject:root];
        }
    }
    return roots;
}

NSString* AskUserForGame() {
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.title = @"Locate Baldur's Gate 3";
    panel.message = @"Select the original Baldur's Gate 3.app installed by Steam.";
    panel.prompt = @"Select Game";
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;
    panel.resolvesAliases = YES;
    panel.allowedContentTypes = @[UTTypeApplication];

    [NSApp activateIgnoringOtherApps:YES];
    if ([panel runModal] != NSModalResponseOK) {
        return nil;
    }

    NSString* selected = panel.URL.path;
    if (!ResolveGameExecutable(selected, nullptr)) {
        ShowAlert(
            @"That is not Baldur's Gate 3",
            @"Choose the Steam Baldur's Gate 3.app bundle. No files were changed.",
            NSAlertStyleWarning);
        return nil;
    }
    return selected;
}

NSString* FindGameApplication() {
    NSMutableArray<NSString*>* candidates = [NSMutableArray array];
    NSMutableSet<NSString*>* seen = [NSMutableSet set];

    NSString* stored = [[NSUserDefaults standardUserDefaults]
        stringForKey:kStoredGamePathKey];
    if (stored != nil) {
        AddCandidate(candidates, seen, stored);
    }

    for (NSString* root in SteamLibraryRoots()) {
        NSString* candidate = [root stringByAppendingPathComponent:
            @"steamapps/common/Baldurs Gate 3/Baldur's Gate 3.app"];
        AddCandidate(candidates, seen, candidate);
    }

    for (NSString* candidate in candidates) {
        if (ResolveGameExecutable(candidate, nullptr)) {
            [[NSUserDefaults standardUserDefaults]
                setObject:candidate
                forKey:kStoredGamePathKey];
            return candidate;
        }
    }

    NSString* selected = AskUserForGame();
    if (selected != nil) {
        [[NSUserDefaults standardUserDefaults]
            setObject:selected
            forKey:kStoredGamePathKey];
    }
    return selected;
}

bool EnsureSteamIsRunning() {
    if ([NSRunningApplication
            runningApplicationsWithBundleIdentifier:kSteamBundleIdentifier].count != 0) {
        return true;
    }

    NSArray<NSString*>* candidates = @[
        @"/Applications/Steam.app",
        [@"~/Applications/Steam.app" stringByExpandingTildeInPath],
    ];
    NSURL* steamURL = nil;
    for (NSString* candidate in candidates) {
        if ([[NSFileManager defaultManager] fileExistsAtPath:candidate]) {
            steamURL = [NSURL fileURLWithPath:candidate];
            break;
        }
    }
    if (steamURL == nil) {
        return false;
    }

    NSWorkspaceOpenConfiguration* configuration =
        [NSWorkspaceOpenConfiguration configuration];
    configuration.activates = YES;
    [[NSWorkspace sharedWorkspace]
        openApplicationAtURL:steamURL
        configuration:configuration
        completionHandler:^(NSRunningApplication*, NSError* error) {
            if (error != nil) {
                AppendLauncherLog([NSString stringWithFormat:
                    @"Steam launch error: %@",
                    error.localizedDescription]);
            }
        }];

    NSDate* deadline = [NSDate dateWithTimeIntervalSinceNow:20.0];
    while ([deadline timeIntervalSinceNow] > 0.0) {
        if ([NSRunningApplication
                runningApplicationsWithBundleIdentifier:kSteamBundleIdentifier].count != 0) {
            return true;
        }
        [[NSRunLoop currentRunLoop]
            runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
    }
    return false;
}

NSString* InputConfigPath() {
    NSArray<NSString*>* documents = NSSearchPathForDirectoriesInDomains(
        NSDocumentDirectory, NSUserDomainMask, YES);
    if (documents.count == 0) {
        return nil;
    }

    return [documents.firstObject stringByAppendingPathComponent:
        @"Larian Studios/Baldur's Gate 3/PlayerProfiles/Public/"
        @"inputconfig_p1.json"];
}

NSString* BindingStatePath(NSString* profilePath) {
    return [profilePath stringByAppendingPathExtension:
        @"bg3-camera-unlock-state"];
}

NSString* JoinActions(NSArray<NSString*>* actions) {
    return actions.count == 0 ? @"none"
                              : [actions componentsJoinedByString:@", "];
}

void WriteBindingState(NSString* statePath, NSDictionary* state) {
    NSError* error = nil;
    NSData* data = [NSJSONSerialization dataWithJSONObject:state
                                                  options:NSJSONWritingPrettyPrinted
                                                    error:&error];
    if (data == nil ||
        ![data writeToFile:statePath options:NSDataWritingAtomic error:&error]) {
        AppendLauncherLog([NSString stringWithFormat:
            @"Could not save the binding-restore state (%@); the next launch "
             "will re-evaluate from the profile", error.localizedDescription]);
    }
}

// Applies this flavor's bindings to the player's input config, and backs out
// any bindings a previous flavor left that this one does not use.
//
// Never fatal. A missing profile means the game has not been run yet; an
// unreadable one is the player's file to keep rather than ours to overwrite.
// The merge/restore logic itself lives in InputBindings.mm and is unit
// tested; this function only reads and writes the two files.
void ApplyInputBindings() {
    NSString* path = InputConfigPath();
    if (path == nil) {
        AppendLauncherLog(@"Could not locate the BG3 profile directory");
        return;
    }

    NSFileManager* files = [NSFileManager defaultManager];
    if (![files fileExistsAtPath:path]) {
        AppendLauncherLog([NSString stringWithFormat:
            @"No input config at %@; bindings skipped. Run BG3 once so it "
             "creates the profile.", path]);
        return;
    }

    NSData* data = [NSData dataWithContentsOfFile:path];
    NSDictionary* profile = nil;
    if (data.length > 0) {
        NSError* error = nil;
        id parsed = [NSJSONSerialization JSONObjectWithData:data
                                                    options:0
                                                      error:&error];
        if (![parsed isKindOfClass:[NSDictionary class]]) {
            AppendLauncherLog([NSString stringWithFormat:
                @"Input config is not readable JSON (%@); bindings skipped "
                 "and the file was left alone", error.localizedDescription]);
            return;
        }
        profile = parsed;
    } else {
        profile = @{};
    }

    NSString* statePath = BindingStatePath(path);
    NSDictionary* priorState = nil;
    NSData* stateData = [NSData dataWithContentsOfFile:statePath];
    if (stateData.length > 0) {
        id parsed = [NSJSONSerialization JSONObjectWithData:stateData
                                                    options:0
                                                      error:nil];
        if ([parsed isKindOfClass:[NSDictionary class]]) {
            priorState = parsed;
        }
    }

    BG3BindingPlan* plan = bg3cam::ComputeBindingPlan(
        profile, priorState, bg3cam::BuiltBindingFlavor());

    if (plan.profileToWrite == nil) {
        if (plan.stateChanged) {
            WriteBindingState(statePath, plan.stateToWrite);
        }
        AppendLauncherLog([NSString stringWithFormat:
            @"Input bindings already correct for the %s flavor",
            BG3_CAMERA_FLAVOR]);
        return;
    }

    // One full snapshot of the untouched profile, taken once, ever. It is the
    // ground truth a manual "defaults restore" falls back to; the per-action
    // state file above is what the automatic restore uses.
    NSString* backup = [path stringByAppendingPathExtension:
        @"bg3-camera-unlock-backup"];
    BOOL backupInPlace = [files fileExistsAtPath:backup];
    if (!backupInPlace) {
        NSError* copyError = nil;
        if ([files copyItemAtPath:path toPath:backup error:&copyError]) {
            backupInPlace = YES;
            AppendLauncherLog([NSString stringWithFormat:
                @"Saved a snapshot of the original input bindings to %@",
                backup]);
        } else {
            AppendLauncherLog([NSString stringWithFormat:
                @"Could not back up the input config (%@); bindings were not "
                 "changed", copyError.localizedDescription]);
            return;
        }
    }

    if (!bg3cam::ShouldWriteProfile(plan, backupInPlace)) {
        AppendLauncherLog(@"Input bindings not changed (no backup in place)");
        return;
    }

    NSError* writeError = nil;
    NSData* output = [NSJSONSerialization dataWithJSONObject:plan.profileToWrite
                                                     options:NSJSONWritingPrettyPrinted
                                                       error:&writeError];
    if (output == nil ||
        ![output writeToFile:path options:NSDataWritingAtomic error:&writeError]) {
        AppendLauncherLog([NSString stringWithFormat:
            @"Could not write the input config (%@); the game still starts",
            writeError.localizedDescription]);
        return;
    }

    // Persist the restore state only after the profile write succeeds, so the
    // two files never disagree about what the mod has done.
    WriteBindingState(statePath, plan.stateToWrite);

    AppendLauncherLog([NSString stringWithFormat:
        @"Input bindings updated for the %s flavor — applied: %@; restored: "
         "%@; kept your changes to: %@",
        BG3_CAMERA_FLAVOR,
        JoinActions(plan.appliedActions),
        JoinActions(plan.restoredActions),
        JoinActions(plan.keptUserEdits)]);
}

bool EnableTemporaryNoLauncher(PreferenceSnapshot* snapshot) {
    snapshot->value = CFPreferencesCopyValue(
        kNoLauncherPreference,
        (__bridge CFStringRef)kGameBundleIdentifier,
        kCFPreferencesCurrentUser,
        kCFPreferencesAnyHost);

    CFPreferencesSetValue(
        kNoLauncherPreference,
        kCFBooleanTrue,
        (__bridge CFStringRef)kGameBundleIdentifier,
        kCFPreferencesCurrentUser,
        kCFPreferencesAnyHost);

    const bool synchronized = CFPreferencesSynchronize(
        (__bridge CFStringRef)kGameBundleIdentifier,
        kCFPreferencesCurrentUser,
        kCFPreferencesAnyHost);
    AppendLauncherLog([NSString stringWithFormat:
        @"Temporary NoLauncher override %@; previous value %@",
        synchronized ? @"enabled" : @"failed to synchronize",
        snapshot->value == nullptr
            ? @"was absent"
            : (__bridge id)snapshot->value]);
    return synchronized;
}

bool RestoreNoLauncher(PreferenceSnapshot* snapshot) {
    CFPreferencesSetValue(
        kNoLauncherPreference,
        snapshot->value,
        (__bridge CFStringRef)kGameBundleIdentifier,
        kCFPreferencesCurrentUser,
        kCFPreferencesAnyHost);

    const bool synchronized = CFPreferencesSynchronize(
        (__bridge CFStringRef)kGameBundleIdentifier,
        kCFPreferencesCurrentUser,
        kCFPreferencesAnyHost);
    AppendLauncherLog([NSString stringWithFormat:
        @"Temporary NoLauncher override removed; restoration %@",
        synchronized ? @"succeeded" : @"failed"]);

    if (snapshot->value != nullptr) {
        CFRelease(snapshot->value);
        snapshot->value = nullptr;
    }
    return synchronized;
}

std::vector<std::string> BuildEnvironment(
    NSString* dylibPath,
    NSString* logPath,
    NSString* statusPath,
    NSString* configPath) {
    std::map<std::string, std::string> values;
    for (char** entry = environ; entry != nullptr && *entry != nullptr; ++entry) {
        std::string item(*entry);
        const std::size_t separator = item.find('=');
        if (separator != std::string::npos) {
            values[item.substr(0, separator)] = item.substr(separator + 1);
        }
    }

    std::string injectedPath(dylibPath.fileSystemRepresentation);
    const auto existing = values.find("DYLD_INSERT_LIBRARIES");
    if (existing != values.end() && !existing->second.empty()) {
        injectedPath += ":" + existing->second;
    }

    values["DYLD_INSERT_LIBRARIES"] = injectedPath;
    values["BG3_CAMERA_UNLOCK_LOG"] = logPath.fileSystemRepresentation;
    values["BG3_CAMERA_UNLOCK_STATUS_FILE"] = statusPath.fileSystemRepresentation;
    values["BG3_CAMERA_UNLOCK_CONFIG"] = configPath.fileSystemRepresentation;
    values["BG3_CAMERA_UNLOCK_VERSION"] = BG3_CAMERA_VERSION;
    // BG3 calls SteamAPI_RestartAppIfNecessary during startup. When the game
    // executable is spawned directly, libsteam_api otherwise asks the already
    // running Steam client to start a second, clean process that cannot inherit
    // this launcher's DYLD_INSERT_LIBRARIES. The shipped libsteam_api checks
    // SteamAppId first and skips that restart when it contains a non-zero ID.
    values["SteamAppId"] = "1086940";
    values["SteamGameId"] = "1086940";

    std::vector<std::string> result;
    result.reserve(values.size());
    for (const auto& [key, value] : values) {
        result.push_back(key + "=" + value);
    }
    return result;
}

int LaunchGame(
    NSString* executablePath,
    NSString* dylibPath,
    NSString* statusPath,
    NSString* configPath,
    pid_t* childPid) {
    NSString* logs = LogsDirectory();
    [[NSFileManager defaultManager]
        createDirectoryAtPath:logs
        withIntermediateDirectories:YES
        attributes:nil
        error:nil];
    NSString* modLog = [logs stringByAppendingPathComponent:@"mod.log"];

    std::vector<std::string> environment =
        BuildEnvironment(dylibPath, modLog, statusPath, configPath);
    std::vector<char*> environmentPointers;
    environmentPointers.reserve(environment.size() + 1);
    for (std::string& entry : environment) {
        environmentPointers.push_back(entry.data());
    }
    environmentPointers.push_back(nullptr);

    std::string executable(executablePath.fileSystemRepresentation);
    // NSUserDefaults also exposes command-line pairs through NSArgumentDomain.
    // The persisted preference above is the confirmed macOS mechanism; this
    // process-local copy makes the intent explicit without changing game files.
    char noLauncherKey[] = "-NoLauncher";
    char trueValue[] = "YES";
    char* arguments[] = {
        executable.data(),
        noLauncherKey,
        trueValue,
        nullptr,
    };

    posix_spawn_file_actions_t actions;
    int result = posix_spawn_file_actions_init(&actions);
    if (result != 0) {
        return result;
    }

    NSString* workingDirectory = executablePath.stringByDeletingLastPathComponent;

    // macOS 26 deprecated the _np spelling in favour of
    // posix_spawn_file_actions_addchdir(), but that replacement only exists
    // from macOS 26 onwards. This app deploys to macOS 12 (see
    // LSMinimumSystemVersion), so calling the replacement would break every
    // supported release before 26 in exchange for silencing a warning on the
    // newest one. The _np symbol is still present and still correct.
    //
    // Revisit only if the deployment target ever rises to macOS 26.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    result = posix_spawn_file_actions_addchdir_np(
        &actions,
        workingDirectory.fileSystemRepresentation);
#pragma clang diagnostic pop
    if (result == 0) {
        result = posix_spawn(
            childPid,
            executable.c_str(),
            &actions,
            nullptr,
            arguments,
            environmentPointers.data());
    }
    posix_spawn_file_actions_destroy(&actions);
    return result;
}

NSString* WaitForModStatus(NSString* statusPath, pid_t gamePid) {
    NSDate* deadline = [NSDate dateWithTimeIntervalSinceNow:125.0];
    while ([deadline timeIntervalSinceNow] > 0.0) {
        NSString* status = [NSString
            stringWithContentsOfFile:statusPath
            encoding:NSUTF8StringEncoding
            error:nil];
        if (status != nil) {
            NSString* trimmed = [status stringByTrimmingCharactersInSet:
                NSCharacterSet.whitespaceAndNewlineCharacterSet];
            if ([trimmed isEqualToString:@"active"] ||
                [trimmed hasPrefix:@"abort:"]) {
                return trimmed;
            }
        }

        int childStatus = 0;
        if (waitpid(gamePid, &childStatus, WNOHANG) == gamePid) {
            return @"abort: BG3 exited before the mod reported ready";
        }
        [[NSRunLoop currentRunLoop]
            runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
    }
    return @"abort: timed out while waiting for the mod to initialize";
}

}  // namespace


int main(int argc, const char* argv[]) {
    (void)argc;
    (void)argv;

    @autoreleasepool {
        [NSApplication sharedApplication];
        AppendLauncherLog([NSString stringWithFormat:
            @"Launcher %@ started — flavor: %@ (%@)",
            @BG3_CAMERA_VERSION,
            @BG3_CAMERA_FLAVOR_NAME,
            @BG3_CAMERA_FLAVOR]);

#if !defined(__arm64__)
        ShowAlert(
            @"Apple Silicon is required",
            @"This beta contains ARM64 hooks and cannot run on an Intel Mac.",
            NSAlertStyleCritical);
        return 1;
#endif

        if (IsRunningUnderRosetta()) {
            ShowAlert(
                @"Rosetta mode is not supported",
                @"Disable “Open using Rosetta” for this launcher and run the native ARM64 version of BG3.",
                NSAlertStyleCritical);
            return 1;
        }

        if ([NSRunningApplication
                runningApplicationsWithBundleIdentifier:kGameBundleIdentifier].count != 0) {
            ShowAlert(
                @"Baldur's Gate 3 is already running",
                @"Quit the game completely, then open BG3 Camera Unlock. The dylib must be loaded before the game starts.",
                NSAlertStyleWarning);
            return 1;
        }

        NSString* gameApp = FindGameApplication();
        if (gameApp == nil) {
            AppendLauncherLog(@"Game selection cancelled or invalid");
            return 1;
        }

        NSString* gameExecutable = nil;
        if (!ResolveGameExecutable(gameApp, &gameExecutable)) {
            ShowAlert(
                @"Could not validate Baldur's Gate 3",
                @"The selected app has the wrong bundle identifier or no executable. No files were changed.",
                NSAlertStyleCritical);
            return 1;
        }

        NSString* dylibPath = [[NSBundle mainBundle]
            pathForResource:@"BG3CameraUnlock"
            ofType:@"dylib"];
        if (dylibPath == nil ||
            ![[NSFileManager defaultManager] fileExistsAtPath:dylibPath]) {
            ShowAlert(
                @"The mod library is missing",
                @"Reinstall the launcher from the original DMG. The game was not started.",
                NSAlertStyleCritical);
            return 1;
        }

        MigrateLegacyFiles();
        LinkInputConfig();

        NSString* configPath = EnsureUserConfig();
        if (configPath == nil) {
            ShowAlert(
                @"Could not prepare the configuration",
                @"The launcher could not create ~/Documents/BG3 Camera Unlock/config.ini. The game was not started; see launcher.log for the macOS error.",
                NSAlertStyleCritical);
            return 1;
        }

        if (!EnsureSteamIsRunning()) {
            ShowAlert(
                @"Steam is not running",
                @"Start Steam, sign in, then open BG3 Camera Unlock again.",
                NSAlertStyleWarning);
            return 1;
        }

        NSString* statusPath = [NSTemporaryDirectory()
            stringByAppendingPathComponent:[NSString stringWithFormat:
                @"bg3-camera-unlock-%@.status",
                NSUUID.UUID.UUIDString]];
        [[NSFileManager defaultManager] removeItemAtPath:statusPath error:nil];

        // Before the game starts: BG3 reads its input config at startup and
        // rewrites it on exit, so this is the only point at which a change
        // both takes effect and survives.
        ApplyInputBindings();

        PreferenceSnapshot noLauncherSnapshot;
        if (!EnableTemporaryNoLauncher(&noLauncherSnapshot)) {
            RestoreNoLauncher(&noLauncherSnapshot);
            ShowAlert(
                @"Could not bypass the Larian Launcher",
                @"macOS would not synchronize BG3's temporary NoLauncher preference. No game files were changed.",
                NSAlertStyleCritical);
            return 1;
        }

        pid_t gamePid = 0;
        const int launchResult = LaunchGame(
            gameExecutable,
            dylibPath,
            statusPath,
            configPath,
            &gamePid);
        if (launchResult != 0) {
            RestoreNoLauncher(&noLauncherSnapshot);
            NSString* reason = [NSString stringWithUTF8String:
                std::strerror(launchResult)];
            AppendLauncherLog([NSString stringWithFormat:
                @"BG3 launch failed: %@",
                reason]);
            ShowAlert(
                @"Could not start Baldur's Gate 3",
                [NSString stringWithFormat:
                    @"macOS reported: %@. No game files were changed.",
                    reason],
                NSAlertStyleCritical);
            return 1;
        }

        AppendLauncherLog([NSString stringWithFormat:
            @"BG3 spawned with pid %d",
            gamePid]);
        NSString* status = WaitForModStatus(statusPath, gamePid);
        const bool preferenceRestored =
            RestoreNoLauncher(&noLauncherSnapshot);
        [[NSFileManager defaultManager] removeItemAtPath:statusPath error:nil];

        if (![status isEqualToString:@"active"]) {
            AppendLauncherLog(status);
            ShowAlert(
                @"BG3 started, but the camera mod is inactive",
                [NSString stringWithFormat:
                    @"%@\n\nThe game itself was not patched and may be played normally. See ~/Documents/BG3 Camera Unlock/mod.log.",
                    status],
                NSAlertStyleWarning);
            return 2;
        }

        if (!preferenceRestored) {
            ShowAlert(
                @"The camera mod is active",
                @"BG3 started with the mod, but macOS could not restore the temporary Larian Launcher preference. To restore it manually later, run: defaults delete com.larian.bg3 NoLauncher",
                NSAlertStyleWarning);
        }

        AppendLauncherLog(@"Mod reported active; launcher exiting");
        return 0;
    }
}
