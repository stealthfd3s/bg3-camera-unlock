#!/bin/bash
#
# Bumps the project version in every place it appears, in one command.
#
#   tools/bump-version.sh 1.0.1
#   tools/bump-version.sh 1.1.0 --date 2026-09-10
#   tools/bump-version.sh 1.1.0-rc.1        (a pre-release suffix is allowed)
#
# Updates:
#   CMakeLists.txt   BG3_CAMERA_VERSION  (the single source of truth)
#   CHANGELOG.md     promotes [Unreleased] to the new version, dated
#   README.md        the version badge
#
# Info.plist.in and scripts/package.sh derive their version from
# CMakeLists.txt and so need no edit here — that is the point of them
# reading it rather than repeating it.
#
# Does not commit, tag, or push. Review the diff, then tag yourself.

set -euo pipefail

readonly RED=$'\033[31m'
readonly GREEN=$'\033[32m'
readonly RESET=$'\033[0m'

die() {
    printf '%serror: %s%s\n' "$RED" "$1" "$RESET" >&2
    exit 1
}

usage() {
    sed -n '3,18p' "$0" | sed 's|^# \{0,1\}||'
    exit "${1:-0}"
}

[[ $# -ge 1 ]] || usage 1
[[ "$1" == "-h" || "$1" == "--help" ]] && usage 0

VERSION="$1"
shift

RELEASE_DATE="$(date +%Y-%m-%d)"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --date) RELEASE_DATE="${2:-}"; shift 2 ;;
        *) die "unknown argument: $1" ;;
    esac
done

# Semver with an optional pre-release suffix (e.g. 1.1.0 or 1.1.0-rc.1).
[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.]+)?$ ]] || die \
    "version must be MAJOR.MINOR.PATCH or MAJOR.MINOR.PATCH-PRERELEASE (got '$VERSION')"

[[ "$RELEASE_DATE" =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}$ ]] || die \
    "date must be YYYY-MM-DD (got '$RELEASE_DATE')"

cd "$(dirname "$0")/.."

[[ -f CMakeLists.txt ]] || die "run this from inside the repository"

CURRENT=$(sed -n 's/^set(BG3_CAMERA_VERSION "\(.*\)")$/\1/p' CMakeLists.txt)
[[ -n "$CURRENT" ]] || die "could not read the current version from CMakeLists.txt"

if [[ "$CURRENT" == "$VERSION" ]]; then
    die "already at version $VERSION"
fi

# Refuse to go backwards; sort -V puts the lower version first.
LOWEST=$(printf '%s\n%s\n' "$CURRENT" "$VERSION" | sort -V | head -1)
if [[ "$LOWEST" == "$VERSION" ]]; then
    die "$VERSION is older than the current $CURRENT; refusing to go backwards"
fi

printf 'Bumping %s -> %s (release date %s)\n\n' \
    "$CURRENT" "$VERSION" "$RELEASE_DATE"

# --- CMakeLists.txt --------------------------------------------------------
sed -i '' "s|^set(BG3_CAMERA_VERSION \".*\")$|set(BG3_CAMERA_VERSION \"$VERSION\")|" \
    CMakeLists.txt
printf '  %s✓%s CMakeLists.txt\n' "$GREEN" "$RESET"

# --- CHANGELOG.md ----------------------------------------------------------
if [[ -f CHANGELOG.md ]]; then
    if ! grep -q '^## \[Unreleased\]' CHANGELOG.md; then
        die "CHANGELOG.md has no '## [Unreleased]' section to promote"
    fi

    # Promote Unreleased to the new version and open a fresh Unreleased.
    python3 - "$VERSION" "$RELEASE_DATE" <<'PYTHON'
import re, sys
version, date = sys.argv[1], sys.argv[2]
text = open('CHANGELOG.md').read()
text = text.replace(
    '## [Unreleased]',
    f'## [Unreleased]\n\n## [{version}] - {date}',
    1)
open('CHANGELOG.md', 'w').write(text)
PYTHON
    printf '  %s✓%s CHANGELOG.md\n' "$GREEN" "$RESET"
else
    printf '  - CHANGELOG.md not found, skipped\n'
fi

# --- README.md badge -----------------------------------------------------
# shields.io escapes a literal '-' in the badge path as '--'.
if [[ -f README.md ]]; then
    BADGE_VERSION="${VERSION//-/--}"
    sed -i '' \
        "s|badge/version-[0-9][0-9A-Za-z.-]*-C27B2B|badge/version-$BADGE_VERSION-C27B2B|g" \
        README.md
    printf '  %s✓%s README.md\n' "$GREEN" "$RESET"
fi

printf '\nDone. Review, then:\n\n'
printf '  git add -A && git commit -m "chore: release v%s"\n' "$VERSION"
printf '  git tag -a v%s -m "v%s"\n' "$VERSION" "$VERSION"
printf '  git push origin main --follow-tags\n\n'
printf 'Pushing the tag is what triggers the release workflow.\n'
