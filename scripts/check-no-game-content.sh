#!/bin/bash
#
# Fails if anything that looks like Baldur's Gate 3 content — or build output —
# is about to enter the source tree or a distributable archive.
#
# The single worst irreversible mistake this project can make is committing or
# shipping the ~233 MB game executable that the pattern work is derived from.
#
# Run modes:
#   check-no-game-content.sh                scan git-tracked files (CI)
#   check-no-game-content.sh --staged       scan staged files (pre-commit hook)
#   check-no-game-content.sh --tree DIR     scan every file under DIR, with no
#                                           reference to git at all — this is
#                                           what vets a source staging folder
#                                           or an unpacked archive before it
#                                           becomes a release asset
#   check-no-game-content.sh --binary DIR   as --tree, but for a binary package
#                                           (a signed .app / .dylib is allowed;
#                                           extracted game code and Windows
#                                           binaries still are not)
#
# Install as a hook with:
#   ln -s ../../scripts/check-no-game-content.sh .git/hooks/pre-commit

set -euo pipefail

# Nothing this project legitimately tracks in source comes close to this. The
# largest real source file is the GPL text at ~35 KB. Binary packages carry a
# multi-MB signed app, so --binary raises the ceiling.
readonly SOURCE_MAX_BYTES=$((2 * 1024 * 1024))
readonly BINARY_MAX_BYTES=$((64 * 1024 * 1024))

readonly RED=$'\033[31m'
readonly YELLOW=$'\033[33m'
readonly RESET=$'\033[0m'

mode="tracked"
scan_dir=""
max_bytes=$SOURCE_MAX_BYTES
allow_macho=0

case "${1:-}" in
    --staged) mode="staged" ;;
    --tree)
        mode="tree"
        scan_dir="${2:?--tree needs a directory}"
        ;;
    --binary)
        mode="tree"
        scan_dir="${2:?--binary needs a directory}"
        max_bytes=$BINARY_MAX_BYTES
        allow_macho=1
        ;;
    "" ) : ;;
    *) printf '%sunknown option: %s%s\n' "$RED" "$1" "$RESET" >&2; exit 2 ;;
esac

# Collect the file list. macOS ships bash 3.2 (no mapfile), so use a read loop.
files=()
if [[ "$mode" == "tree" ]]; then
    [[ -d "$scan_dir" ]] || {
        printf '%sno such directory: %s%s\n' "$RED" "$scan_dir" "$RESET" >&2
        exit 2
    }
    while IFS= read -r entry; do
        [[ -n "$entry" ]] && files+=("$entry")
    done < <(find "$scan_dir" -type f)
else
    if ! git rev-parse --git-dir >/dev/null 2>&1; then
        printf '%snot a git repository; run with --tree DIR to scan a path%s\n' \
            "$YELLOW" "$RESET" >&2
        exit 0
    fi
    cd "$(git rev-parse --show-toplevel)"
    while IFS= read -r entry; do
        [[ -n "$entry" ]] && files+=("$entry")
    done < <(
        if [[ "$mode" == "staged" ]]; then
            git diff --cached --name-only --diff-filter=ACM
        else
            git ls-files
        fi
    )
fi

failed=0
fail() {
    printf '%s✗ %s%s\n    %s\n' "$RED" "$1" "$RESET" "$2" >&2
    failed=1
}

if (( ${#files[@]} == 0 )); then
    printf "nothing to check\n"
    exit 0
fi

for file in "${files[@]}"; do
    [[ -f "$file" ]] || continue
    base="${file##*/}"

    size=$(wc -c <"$file" | tr -d '[:space:]')
    if (( size > max_bytes )); then
        fail "$file" "is $((size / 1024)) KB, over the $((max_bytes / 1024)) KB limit."
        continue
    fi

    magic=$(head -c 4 "$file" | xxd -p 2>/dev/null || true)
    case "$magic" in
        cffaedfe|cefaedfe|feedfacf|feedface|cafebabe|bebafeca)
            if (( allow_macho )); then
                # A Mach-O in a binary package is only allowed inside the app
                # bundle: the launcher executable and the injected dylib.
                case "$file" in
                    *.app/Contents/MacOS/*) ;;
                    *.app/Contents/Resources/BG3CameraUnlock.dylib) ;;
                    *)
                        fail "$file" "is a Mach-O binary outside the app bundle."
                        continue
                        ;;
                esac
            else
                fail "$file" "is a Mach-O binary. Build output and extracted game code must not be in the source tree."
                continue
            fi
            ;;
        fade0c00|fade0cc0|00c0defa)
            # Detached code-signature blob inside a signed bundle. Fine in a
            # binary package, never in source.
            if (( ! allow_macho )); then
                fail "$file" "is a code-signature blob; build output must not be in the source tree."
                continue
            fi
            ;;
    esac

    if [[ $(head -c 2 "$file") == "MZ" ]]; then
        fail "$file" "is a Windows PE binary (e.g. bink2w64.dll). This project ships no Windows binaries."
        continue
    fi

    case "$base" in
        *.pak|*.lsf|*.lsx|*.loca|*bg3-arm64*|*bink2w64*)
            fail "$file" "matches a Baldur's Gate 3 content pattern. This project redistributes no game files."
            ;;
    esac
done

if (( failed )); then
    cat >&2 <<'EOF'

Refusing to proceed. See NOTICE: this project redistributes no Baldur's
Gate 3 content. If a file above is legitimate, add a narrow exception to
this script rather than loosening the size or magic checks.
EOF
    exit 1
fi

printf 'no game content detected (%d files checked, mode=%s)\n' \
    "${#files[@]}" "$mode"
