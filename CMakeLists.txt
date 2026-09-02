cmake_minimum_required(VERSION 3.20)

# ---------------------------------------------------------------------------
# Version
#
# The single source of truth for the release version. tools/bump-version.sh
# rewrites this line, and everything else derives from it: the bundle plist,
# the packaging script, and the version the launcher and the injected library
# report. Do not hardcode the version anywhere else.
#
# The flavor (Camera Only vs Camera + WASD) is orthogonal to this: one
# version ships in two flavors, selected by BG3_CAMERA_WITH_WASD below.
# ---------------------------------------------------------------------------
set(BG3_CAMERA_VERSION "1.0.0")

# project() VERSION takes numeric components only; BG3_CAMERA_VERSION carries
# the full label (which may include a pre-release suffix) that names the
# compile define, the bundle strings and the packaged artifacts.
project(BG3CameraUnlock
    VERSION 1.0.0
    LANGUAGES CXX OBJCXX ASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OSX_ARCHITECTURES arm64)

if(NOT APPLE)
    message(FATAL_ERROR
        "BG3 Camera Unlock targets macOS on Apple Silicon only. "
        "It injects a dylib into the macOS ARM64 build of the game and has "
        "no meaning on other platforms.")
endif()

# CI builds with this on so a new warning cannot land unnoticed. Local
# builds leave it off, so a warning never blocks someone mid-change.
option(BG3_CAMERA_WERROR "Treat compiler warnings as errors" OFF)

set(BG3_CAMERA_WARNINGS -Wall -Wextra -Wpedantic)
if(BG3_CAMERA_WERROR)
    list(APPEND BG3_CAMERA_WARNINGS -Werror)
endif()

# ---------------------------------------------------------------------------
# Build flavor: Camera Only vs Camera + WASD
#
# ON  (default): "Camera + WASD". The injected library disables BG3's
#     keyboard-mode character-movement guard, and the launcher binds W/A/S/D
#     to the character-movement actions and moves camera panning onto the
#     arrow keys. This is the historically shipped behaviour.
#
# OFF: "Camera Only". The keyboard-movement guard is never touched (its
#     ARM64 pattern is not even required for activation), the launcher never
#     binds W/A/S/D and never rebinds camera panning, so the game's stock
#     WASD camera pan is left exactly as the player had it. KeyboardMovement
#     in the config file has no effect in this flavor.
#
# This is the only intended functional difference between the two builds.
# One source tree, two build directories / CMake caches.
# ---------------------------------------------------------------------------
option(BG3_CAMERA_WITH_WASD
    "Build the Camera + WASD flavor (OFF builds Camera Only)" ON)

if(BG3_CAMERA_WITH_WASD)
    set(BG3_CAMERA_FLAVOR "camera-wasd")
    set(BG3_CAMERA_FLAVOR_NAME "Camera + WASD")
    set(BG3_CAMERA_BUNDLE_NAME "BG3 Camera Unlock (Camera + WASD)")
    set(BG3_CAMERA_BUNDLE_ID "dev.andrii.bg3-camera-unlock.camera-wasd")
    set(BG3_CAMERA_WITH_WASD_DEFINE 1)
else()
    set(BG3_CAMERA_FLAVOR "camera-only")
    set(BG3_CAMERA_FLAVOR_NAME "Camera Only")
    set(BG3_CAMERA_BUNDLE_NAME "BG3 Camera Unlock (Camera Only)")
    set(BG3_CAMERA_BUNDLE_ID "dev.andrii.bg3-camera-unlock.camera-only")
    set(BG3_CAMERA_WITH_WASD_DEFINE 0)
endif()

message(STATUS
    "BG3 Camera Unlock ${BG3_CAMERA_VERSION} — flavor: ${BG3_CAMERA_FLAVOR_NAME}")

# ---------------------------------------------------------------------------
# Injected mod library
# ---------------------------------------------------------------------------

add_library(BG3CameraUnlockCore SHARED
    src/AfterZoomDetour.S
    src/PreYawDetour.S
    src/PreGateDetour.S
    src/CameraConfig.cpp
    src/CameraHooks.cpp
    src/HookInstaller.cpp
    src/MachOImage.cpp
    src/ModMain.cpp
    src/PatternScanner.cpp
    src/RawTrackpadMonitor.mm
)

target_include_directories(BG3CameraUnlockCore PRIVATE src)
target_compile_options(BG3CameraUnlockCore PRIVATE ${BG3_CAMERA_WARNINGS})
target_compile_definitions(BG3CameraUnlockCore PRIVATE
    BG3_CAMERA_VERSION="${BG3_CAMERA_VERSION}"
    BG3_CAMERA_FLAVOR="${BG3_CAMERA_FLAVOR}"
    BG3_CAMERA_WITH_WASD=${BG3_CAMERA_WITH_WASD_DEFINE})

# RawTrackpadMonitor.mm is the only Objective-C++ translation unit in the
# injected library: an in-process NSEvent monitor that reads raw trackpad
# precise-scroll and middle-mouse-drag deltas before BG3 classifies them into
# its 107/108 camera actions. It needs ARC and AppKit; the rest of the
# library needs neither. This path is a normal part of every build.
set_source_files_properties(src/RawTrackpadMonitor.mm PROPERTIES
    COMPILE_OPTIONS "-fobjc-arc")

# CoreGraphics supplies CGEventSourceButtonState, which reports whether a
# mouse button is physically held. AppKit supplies the NSEvent monitor above.
target_link_libraries(BG3CameraUnlockCore PRIVATE
    "-framework CoreGraphics"
    "-framework AppKit")

set_target_properties(BG3CameraUnlockCore PROPERTIES
    OUTPUT_NAME "BG3CameraUnlockMod"
)

# ---------------------------------------------------------------------------
# Application icon
#
# The icon is generated from vector primitives in tools/make_app_icon.mm at
# build time rather than committed as an image. That is deliberate: the icon
# ships in every release archive, so it must contain no third-party artwork,
# and generating it from source makes that verifiable instead of asserted.
# ---------------------------------------------------------------------------

add_executable(make_app_icon tools/make_app_icon.mm)
target_compile_options(make_app_icon PRIVATE ${BG3_CAMERA_WARNINGS} -fobjc-arc)
target_link_libraries(make_app_icon PRIVATE
    "-framework AppKit"
    "-framework Foundation"
)

set(BG3_CAMERA_ICONSET "${CMAKE_CURRENT_BINARY_DIR}/BG3CameraUnlock.iconset")
set(BG3_CAMERA_ICON "${CMAKE_CURRENT_BINARY_DIR}/BG3CameraUnlock.icns")

add_custom_command(
    OUTPUT "${BG3_CAMERA_ICON}"
    COMMAND "$<TARGET_FILE:make_app_icon>" "${BG3_CAMERA_ICONSET}"
    COMMAND iconutil -c icns "${BG3_CAMERA_ICONSET}" -o "${BG3_CAMERA_ICON}"
    DEPENDS make_app_icon
    COMMENT "Generating application icon from vector source"
    VERBATIM
)

set_source_files_properties("${BG3_CAMERA_ICON}" PROPERTIES
    GENERATED TRUE
    MACOSX_PACKAGE_LOCATION "Resources"
)

# The default config shipped inside the bundle is flavor-specific: the two
# files differ only in comments and in the KeyboardMovement line.
if(BG3_CAMERA_WITH_WASD)
    set(BG3_CAMERA_DEFAULT_CONFIG
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/BG3CameraUnlock-camera-wasd.ini")
else()
    set(BG3_CAMERA_DEFAULT_CONFIG
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/BG3CameraUnlock-camera-only.ini")
endif()
set_source_files_properties("${BG3_CAMERA_DEFAULT_CONFIG}" PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources")
get_filename_component(BG3_CAMERA_DEFAULT_CONFIG_STEM
    "${BG3_CAMERA_DEFAULT_CONFIG}" NAME_WE)

# The GPL requires the license text to travel with the program. package.sh
# also stages these in the DMG's Documentation folder, but dragging the .app
# to /Applications leaves that behind, so they are placed inside the bundle
# as well. They are plain text resources and do not affect the executable.
set(BG3_CAMERA_LICENSE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    "${CMAKE_CURRENT_SOURCE_DIR}/NOTICE"
    "${CMAKE_CURRENT_SOURCE_DIR}/MODDING-EXCEPTION.txt")
set_source_files_properties(${BG3_CAMERA_LICENSE_FILES} PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources")

# ---------------------------------------------------------------------------
# Launcher application
# ---------------------------------------------------------------------------

add_executable(BG3CameraUnlockLauncher MACOSX_BUNDLE
    launcher/main.mm
    launcher/InputBindings.mm
    "${BG3_CAMERA_ICON}"
    "${BG3_CAMERA_DEFAULT_CONFIG}"
    ${BG3_CAMERA_LICENSE_FILES}
)

target_compile_options(BG3CameraUnlockLauncher PRIVATE
    ${BG3_CAMERA_WARNINGS}
    -fobjc-arc
)

target_compile_definitions(BG3CameraUnlockLauncher PRIVATE
    BG3_CAMERA_VERSION="${BG3_CAMERA_VERSION}"
    BG3_CAMERA_FLAVOR="${BG3_CAMERA_FLAVOR}"
    BG3_CAMERA_FLAVOR_NAME="${BG3_CAMERA_FLAVOR_NAME}"
    BG3_CAMERA_WITH_WASD=${BG3_CAMERA_WITH_WASD_DEFINE}
    BG3_CAMERA_DEFAULT_CONFIG_RESOURCE="${BG3_CAMERA_DEFAULT_CONFIG_STEM}"
)

target_link_libraries(BG3CameraUnlockLauncher PRIVATE
    "-framework AppKit"
    "-framework Foundation"
    "-framework UniformTypeIdentifiers"
)

set_target_properties(BG3CameraUnlockLauncher PROPERTIES
    OUTPUT_NAME "${BG3_CAMERA_BUNDLE_NAME}"
    MACOSX_BUNDLE_BUNDLE_NAME "${BG3_CAMERA_BUNDLE_NAME}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${BG3_CAMERA_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${BG3_CAMERA_VERSION}"
    MACOSX_BUNDLE_GUI_IDENTIFIER "${BG3_CAMERA_BUNDLE_ID}"
    MACOSX_BUNDLE_ICON_FILE "BG3CameraUnlock.icns"
    MACOSX_BUNDLE_INFO_PLIST
        "${CMAKE_CURRENT_SOURCE_DIR}/launcher/Info.plist.in"
)

add_dependencies(BG3CameraUnlockLauncher BG3CameraUnlockCore)

# The mod library is a resource of the launcher, not a linked dependency:
# the launcher never loads it, it only points the game's loader at it.
# Copy after either target changes. A POST_BUILD step on the launcher alone
# does not rerun when only the dylib is relinked, which can leave an incremental
# app bundle carrying the previous build of the mod.
add_custom_command(TARGET BG3CameraUnlockCore POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_BUNDLE_DIR:BG3CameraUnlockLauncher>/Contents/Resources"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:BG3CameraUnlockCore>"
        "$<TARGET_BUNDLE_DIR:BG3CameraUnlockLauncher>/Contents/Resources/BG3CameraUnlock.dylib"
    VERBATIM
)

add_custom_command(TARGET BG3CameraUnlockLauncher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_BUNDLE_DIR:BG3CameraUnlockLauncher>/Contents/Resources"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:BG3CameraUnlockCore>"
        "$<TARGET_BUNDLE_DIR:BG3CameraUnlockLauncher>/Contents/Resources/BG3CameraUnlock.dylib"
    VERBATIM
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

enable_testing()

# Verifies the config parser: defaults, the six independent per-device
# sensitivities, legacy aliasing in both line orders, range clamping,
# malformed / non-finite input, and the cross-field checks. Runs without the
# game. Built once per flavor so the KeyboardMovement / NO-WASD exception is
# covered on the build that actually enforces it.
add_executable(config_test
    tools/config_test.cpp
    src/CameraConfig.cpp
)
target_include_directories(config_test PRIVATE src)
target_compile_options(config_test PRIVATE ${BG3_CAMERA_WARNINGS})
target_compile_definitions(config_test PRIVATE
    BG3_CAMERA_WITH_WASD=${BG3_CAMERA_WITH_WASD_DEFINE})
add_test(NAME config_test COMMAND config_test)

# Verifies the launcher's pure input-binding merge/restore logic: both
# flavors on a clean profile, both transition directions, repeated launches,
# a simulated backup failure, and preservation of third-party and
# user-edited bindings. Uses Foundation JSON, no AppKit, no game.
add_executable(bindings_test
    tools/bindings_test.mm
    launcher/InputBindings.mm
)
target_include_directories(bindings_test PRIVATE launcher)
target_compile_options(bindings_test PRIVATE ${BG3_CAMERA_WARNINGS} -fobjc-arc)
target_compile_definitions(bindings_test PRIVATE
    BG3_CAMERA_WITH_WASD=${BG3_CAMERA_WITH_WASD_DEFINE})
target_link_libraries(bindings_test PRIVATE "-framework Foundation")
add_test(NAME bindings_test COMMAND bindings_test)

# Verifies every ARM64 signature against a real game binary. Not registered
# as a CTest case because it needs the user's own copy of the game, which is
# never distributed with this project:
#   ./build/pattern_test /path/to/bg3-arm64
add_executable(pattern_test
    tools/pattern_test.cpp
    src/PatternScanner.cpp
)
target_include_directories(pattern_test PRIVATE src)
target_compile_options(pattern_test PRIVATE ${BG3_CAMERA_WARNINGS})
target_compile_definitions(pattern_test PRIVATE
    BG3_CAMERA_WITH_WASD=${BG3_CAMERA_WITH_WASD_DEFINE})
