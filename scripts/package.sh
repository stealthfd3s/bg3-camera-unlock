#!/bin/zsh
#
# Builds the distributable BG3 Camera Unlock release artifacts for BOTH
# flavors from the one source tree.
#
# Produces, in dist/:
#   BG3-Camera-Unlock-CameraOnly-macOS-arm64-<version>.dmg
#   BG3-Camera-Unlock-CameraOnly-macOS-arm64-<version>-nexus.zip
#   BG3-Camera-Unlock-CameraWASD-macOS-arm64-<version>.dmg
#   BG3-Camera-Unlock-CameraWASD-macOS-arm64-<version>-nexus.zip
#   BG3-Camera-Unlock-macOS-arm64-<version>-source.zip   (builds both flavors)
#   SHA256SUMS.txt
#
# The version is read from CMakeLists.txt and is never duplicated here. The
# flavor is a build-time switch (BG3_CAMERA_WITH_WASD); each flavor gets its
# own clean build directory and CMake cache.
#
# Environment:
#   FLAVOR=camera-only|camera-wasd   build just that flavor
#   BG3_GAME_BINARY=/path/to/bg3-arm64   also run pattern_test against it
#   KEEP_BUILD=1     keep the per-flavor build trees
#   OUTPUT_DIR=...   write artifacts somewhere other than dist/
#   DMG_ONLY=1       build only the DMGs — skip the Nexus zips, the
#                    corresponding-source zip and the checksums. For local
#                    iteration only: a public GPL binary release still has to
#                    ship the corresponding source alongside it, so a full
#                    run (source zip included) is what a release uses.

set -euo pipefail

SCRIPT_DIR=${0:A:h}
PROJECT_DIR=${SCRIPT_DIR:h}

die() {
    print -u2 ""
    print -u2 "error: $1"
    [[ -n "${2:-}" ]] && print -u2 "       $2"
    print -u2 ""
    exit 1
}

need() {
    whence -p "$1" >/dev/null 2>&1 || die \
        "required tool '$1' was not found on PATH" "$2"
}

need cmake     "Install with: brew install cmake"
need iconutil  "Part of the Xcode Command Line Tools: xcode-select --install"
need codesign  "Part of the Xcode Command Line Tools: xcode-select --install"
need hdiutil   "hdiutil ships with macOS; a missing one means a broken system."
need ditto     "ditto ships with macOS; a missing one means a broken system."
need shasum    "shasum ships with macOS; a missing one means a broken system."
need cmp       "cmp ships with macOS; a missing one means a broken system."

[[ "$(uname -s)" == "Darwin" ]] || die \
    "this packaging script only runs on macOS" \
    "BG3 Camera Unlock is a macOS-only mod."

[[ "$(uname -m)" == "arm64" ]] || print -u2 \
    "warning: building on $(uname -m); the result targets arm64 only."

# ---------------------------------------------------------------------------
# Version and layout
# ---------------------------------------------------------------------------

CMAKE_LISTS="$PROJECT_DIR/CMakeLists.txt"
[[ -f "$CMAKE_LISTS" ]] || die "CMakeLists.txt not found at $CMAKE_LISTS"

VERSION=$(sed -n 's/^set(BG3_CAMERA_VERSION "\(.*\)")$/\1/p' "$CMAKE_LISTS")
[[ -n "$VERSION" ]] || die \
    "could not read BG3_CAMERA_VERSION from CMakeLists.txt" \
    "Expected: set(BG3_CAMERA_VERSION \"1.2.3\")"

DIST_DIR="${OUTPUT_DIR:-$PROJECT_DIR/dist}"
CHECKSUM_PATH="$DIST_DIR/SHA256SUMS.txt"
cmake -E make_directory "$DIST_DIR"

typeset -a FLAVORS
if [[ -n "${FLAVOR:-}" ]]; then
    FLAVORS=("$FLAVOR")
else
    FLAVORS=(camera-only camera-wasd)
fi

print "Packaging BG3 Camera Unlock $VERSION"
print "Flavors  : ${FLAVORS[*]}"
print "Output   : $DIST_DIR"
print ""

typeset -a PRODUCED

# ---------------------------------------------------------------------------
# Per-flavor build + package
# ---------------------------------------------------------------------------

package_flavor() {
    local flavor="$1"
    local wasd_opt slug human volume patterns_expected
    case "$flavor" in
        camera-only)
            wasd_opt=OFF; slug=CameraOnly; human="Camera Only"
            patterns_expected=14 ;;
        camera-wasd)
            wasd_opt=ON;  slug=CameraWASD; human="Camera + WASD"
            patterns_expected=15 ;;
        *) die "unknown flavor: $flavor" ;;
    esac

    local build_dir="$PROJECT_DIR/build-release-$flavor"
    local app_name="BG3 Camera Unlock ($human).app"
    local app_path="$build_dir/$app_name"
    local core_dylib="$build_dir/libBG3CameraUnlockMod.dylib"
    local app_dylib="$app_path/Contents/Resources/BG3CameraUnlock.dylib"
    local base="BG3-Camera-Unlock-$slug-macOS-arm64-$VERSION"
    local dmg_path="$DIST_DIR/$base.dmg"
    local nexus_zip="$DIST_DIR/$base-nexus.zip"
    local stage_dir="$DIST_DIR/stage-$flavor"

    print "=============================================================="
    print " $human  ($flavor)"
    print "=============================================================="

    # -- Configure from a clean tree, build, test ---------------------------
    cmake -E rm -rf "$build_dir"
    cmake -S "$PROJECT_DIR" -B "$build_dir" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBG3_CAMERA_WERROR=ON \
        -DBG3_CAMERA_WITH_WASD=$wasd_opt
    cmake --build "$build_dir" --parallel

    ctest --test-dir "$build_dir" --output-on-failure || die \
        "tests failed for $flavor; refusing to package"

    [[ -d "$app_path" ]] || die "app bundle not produced at $app_path"
    [[ -f "$core_dylib" ]] || die "mod library not produced at $core_dylib"

    # -- Pattern verification against the real game binary, if provided ----
    if [[ -n "${BG3_GAME_BINARY:-}" ]]; then
        [[ -f "$BG3_GAME_BINARY" ]] || die \
            "BG3_GAME_BINARY is set but $BG3_GAME_BINARY does not exist"
        local pt_out
        pt_out=$("$build_dir/pattern_test" "$BG3_GAME_BINARY")
        print "$pt_out"
        local n_lines n_unique
        n_lines=$(print "$pt_out" | grep -c 'match(es)')
        n_unique=$(print "$pt_out" | grep -c '1 match(es)')
        [[ "$n_lines" == "$n_unique" ]] || die \
            "pattern_test: not every pattern matched exactly once"
        [[ "$n_lines" == "$patterns_expected" ]] || die \
            "pattern_test: expected $patterns_expected patterns for $flavor, got $n_lines"
        print "pattern_test: $n_lines/$n_lines patterns unique ✓"
    else
        print "note: BG3_GAME_BINARY not set; skipping pattern_test for $flavor"
    fi

    # -- Freshly built dylib into the bundle, proven byte-for-byte --------
    cmake -E copy_if_different "$core_dylib" "$app_dylib"
    cmp -s "$core_dylib" "$app_dylib" || die \
        "app bundle carries a stale mod library" \
        "Expected $app_dylib to match $core_dylib byte-for-byte."

    # -- Capture identity BEFORE signing --------------------------------
    local uuid_before text_before
    uuid_before=$(otool -l "$app_dylib" | awk '$1=="uuid"{print $2; exit}')
    text_before=$(otool -l "$app_dylib" \
        | awk '/sectname __text/{f=1} f && $1=="size"{print $2; f=0}')

    # -- Sign (ad-hoc only; not notarized) ------------------------------
    codesign --force --sign - --timestamp=none "$app_dylib"
    codesign --force --deep --sign - --timestamp=none "$app_path"
    codesign --verify --deep --strict --verbose=2 "$app_path"

    # -- The signature must not have relinked or rebuilt anything ------
    local uuid_after text_after
    uuid_after=$(otool -l "$app_dylib" | awk '$1=="uuid"{print $2; exit}')
    text_after=$(otool -l "$app_dylib" \
        | awk '/sectname __text/{f=1} f && $1=="size"{print $2; f=0}')
    [[ "$uuid_before" == "$uuid_after" ]] || die \
        "dylib LC_UUID changed during signing ($uuid_before -> $uuid_after)"
    [[ "$text_before" == "$text_after" ]] || die \
        "dylib __text size changed during signing ($text_before -> $text_after)"
    print "dylib identity stable across signing: uuid=$uuid_after __text=$text_after"

    # -- Stage the installer image ------------------------------------
    cmake -E rm -rf "$stage_dir"
    cmake -E make_directory "$stage_dir" "$stage_dir/Documentation"
    COPYFILE_DISABLE=1 ditto --norsrc --noextattr \
        "$app_path" "$stage_dir/$app_name"

    local doc
    for doc in README.md LICENSE MODDING-EXCEPTION.txt NOTICE CHANGELOG.md \
               SECURITY.md docs/INSTALL.md docs/CONFIG.md; do
        [[ -f "$PROJECT_DIR/$doc" ]] && \
            cp "$PROJECT_DIR/$doc" "$stage_dir/Documentation/${doc##*/}"
    done

    # -- Nothing resembling game content leaves the machine ---------
    bash "$PROJECT_DIR/scripts/check-no-game-content.sh" --binary "$stage_dir" \
        || die "game-content check failed for the $flavor package"

    # -- Nexus zip: built BEFORE the Applications symlink ----------
    if [[ -z "${DMG_ONLY:-}" ]]; then
        rm -f "$nexus_zip"
        COPYFILE_DISABLE=1 ditto -c -k --norsrc --noextattr \
            "$stage_dir" "$nexus_zip"
        PRODUCED+=("$nexus_zip")
    fi

    # -- DMG: built AFTER the Applications symlink ----------------
    ln -s /Applications "$stage_dir/Applications"
    rm -f "$dmg_path"
    hdiutil create \
        -volname "Install BG3 Camera Unlock ($human)" \
        -srcfolder "$stage_dir" \
        -ov -format UDZO \
        "$dmg_path"
    codesign --force --sign - --timestamp=none "$dmg_path"
    hdiutil verify "$dmg_path" || die "DMG failed its integrity check: $dmg_path"
    PRODUCED+=("$dmg_path")

    # -- Verify the app AS SHIPPED, from inside the mounted image ----
    local mount_point
    mount_point=$(mktemp -d "/tmp/bg3cu-verify-$flavor.XXXXXX")
    hdiutil attach -readonly -nobrowse -mountpoint "$mount_point" "$dmg_path" \
        >/dev/null
    codesign --verify --deep --strict --verbose=2 "$mount_point/$app_name" \
        || { hdiutil detach "$mount_point" >/dev/null; \
             die "the app inside $dmg_path fails signature verification"; }
    local shipped_flavor
    shipped_flavor=$(plutil -extract BG3CameraUnlockFlavor raw \
        "$mount_point/$app_name/Contents/Info.plist" 2>/dev/null || echo "?")
    [[ "$shipped_flavor" == "$flavor" ]] || {
        hdiutil detach "$mount_point" >/dev/null
        die "the app inside $dmg_path reports flavor '$shipped_flavor', expected '$flavor'"
    }
    hdiutil detach "$mount_point" >/dev/null
    rmdir "$mount_point" 2>/dev/null || true
    print "verified shipped app: flavor=$shipped_flavor signature OK"

    cmake -E rm -rf "$stage_dir"
    print ""
}

for flavor in "${FLAVORS[@]}"; do
    package_flavor "$flavor"
done

# ---------------------------------------------------------------------------
# Complete corresponding source (GPL-3.0 section 6) — one archive, builds
# BOTH flavors.
# ---------------------------------------------------------------------------

if [[ -z "${DMG_ONLY:-}" ]]; then
    SOURCE_STAGE="$DIST_DIR/source-stage"
    SOURCE_DIR="$SOURCE_STAGE/BG3-Camera-Unlock-Source-$VERSION"
    SOURCE_ZIP="$DIST_DIR/BG3-Camera-Unlock-macOS-arm64-$VERSION-source.zip"
    cmake -E rm -rf "$SOURCE_STAGE"
    cmake -E make_directory "$SOURCE_DIR"

    for doc in CMakeLists.txt README.md LICENSE MODDING-EXCEPTION.txt NOTICE \
               SECURITY.md CHANGELOG.md CONTRIBUTING.md \
               .gitignore .gitattributes .editorconfig .clang-format; do
        [[ -f "$PROJECT_DIR/$doc" ]] && cp "$PROJECT_DIR/$doc" "$SOURCE_DIR/$doc"
    done
    for dir in src launcher scripts tools assets docs .github; do
        [[ -d "$PROJECT_DIR/$dir" ]] && COPYFILE_DISABLE=1 ditto \
            --norsrc --noextattr "$PROJECT_DIR/$dir" "$SOURCE_DIR/$dir"
    done

    # docs/dev/ holds internal maintainer notes (the pre-1.0.0 changelog
    # history, consolidation and diagnostic reports). They are not part of
    # the corresponding source required to build, and are not published.
    rm -rf "$SOURCE_DIR/docs/dev"

    bash "$PROJECT_DIR/scripts/check-no-game-content.sh" --tree "$SOURCE_DIR" \
        || die "game-content check failed for the source archive"

    rm -f "$SOURCE_ZIP"
    # --keepParent so the archive unpacks into one BG3-Camera-Unlock-Source-*
    # folder rather than scattering files into the current directory.
    COPYFILE_DISABLE=1 ditto -c -k --norsrc --noextattr --keepParent \
        "$SOURCE_DIR" "$SOURCE_ZIP"
    PRODUCED+=("$SOURCE_ZIP")
    cmake -E rm -rf "$SOURCE_STAGE"
fi

# ---------------------------------------------------------------------------
# Checksums
# ---------------------------------------------------------------------------

if [[ -z "${DMG_ONLY:-}" ]]; then
    (
        cd "$DIST_DIR"
        typeset -a names
        for p in "${PRODUCED[@]}"; do names+=("${p:t}"); done
        shasum -a 256 "${names[@]}" > "${CHECKSUM_PATH:t}"
    )
    PRODUCED+=("$CHECKSUM_PATH")
fi

if [[ -z "${KEEP_BUILD:-}" ]]; then
    for flavor in "${FLAVORS[@]}"; do
        cmake -E rm -rf "$PROJECT_DIR/build-release-$flavor"
    done
fi

print ""
print "Version : $VERSION"
print "Artifacts in $DIST_DIR:"
for p in "${PRODUCED[@]}"; do print "  ${p:t}"; done
if [[ -z "${DMG_ONLY:-}" ]]; then
    print ""
    cat "$CHECKSUM_PATH"
fi
