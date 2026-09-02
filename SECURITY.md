# Security, and why macOS warns you

**Short version:** this mod is open source. It changes the game's camera by
rewriting instructions in the running game process, writes its own config
and logs plus your input-binding profile (with a backup), and opens no
network connection in normal use. macOS warns about it because it is not
notarized by Apple, not because a scan found anything. Everything below is
something you can check yourself, and each check says what it does and does
not prove.

---

## Why macOS says "cannot be opened" or "unverified developer"

Releases are **ad-hoc signed but not Apple-notarized**.

Notarization means uploading each build to Apple, who scan it automatically
and issue a ticket. It requires a paid Apple Developer Program membership
($99/year). This is a free mod, so it is not notarized.

**What the warning means:** Apple has not checked this build.

**What it does not mean:** that a scan found something. Gatekeeper shows the
same warning for every unnotarized app. It is not a scan result — and its
absence would not, by itself, tell you an app is safe either.

Ad-hoc signing is also not notarization: it lets `codesign` confirm the
bundle has not been altered since it was signed, but it carries no
developer identity and is not vouched for by Apple.

To open it anyway: on macOS 14 and earlier, right-click the app → **Open** →
**Open**. On macOS 15 and later, use **System Settings → Privacy & Security
→ Open Anyway** after the first blocked launch. Only do this for software
you have a reason to trust — which is what the rest of this page is for.

---

## Why it looks the way it does to a scanner

This mod does three things that also describe how a malicious injector
works:

1. It **launches another program** rather than running on its own.
2. It **injects a library** into that program's process.
3. It **rewrites instructions in that process's memory** at runtime.

There is no other way to change the camera in a game with no mod API. Some
antivirus tools flag any code-injecting program on that technique alone,
regardless of intent. That is a reason to check rather than to assume in
either direction.

---

## What it links

A program can only call into a library it can reach. Checking what the
binaries link is a useful first look — though not an absolute proof about
networking, because AppKit (linked for the input monitor below) itself sits
on a large system dependency graph.

### The injected library

```bash
otool -L "/Applications/BG3 Camera Unlock (Camera Only).app/Contents/Resources/BG3CameraUnlock.dylib"
```

You should see, besides its own install name:

- `CoreGraphics` — for one function, `CGEventSourceButtonState`, which
  reports whether a mouse button is physically held down. The game reports a
  mouse button as a press immediately followed by a release even while it
  stays down, so a held zoom modifier cannot be read from the game's own
  events.
- `AppKit` (and `libobjc`) — for an in-process `NSEvent` monitor that reads
  raw trackpad precise-scroll and middle-mouse-drag deltas before the game
  classifies them into its camera actions.
- `libc++` and `libSystem` — the C++ and system runtimes.

No `CFNetwork`, no `Security.framework`, no `Network.framework` in that
list. To spot-check the symbols the library actually needs resolved:

```bash
nm -u "/Applications/BG3 Camera Unlock (Camera Only).app/Contents/Resources/BG3CameraUnlock.dylib" \
  | grep -iE "socket|connect|getaddrinfo|CFNetwork|NSURL|NSStream"
```

Expected output: **nothing.** The only unusual symbols it imports are
`_CGEventSourceButtonState` and the AppKit `NSEvent` class. This shows the
library does not directly reference a networking API; it is not, by itself,
a proof that no code path anywhere in the loaded process could reach one.

### The launcher

```bash
otool -L "/Applications/BG3 Camera Unlock (Camera Only).app/Contents/MacOS/BG3 Camera Unlock (Camera Only)"
```

It links AppKit, Foundation, UniformTypeIdentifiers and CoreFoundation —
because it draws a window, reads files, and shows a file picker. It links no
networking framework either.

---

## Watch it at runtime

Inference from the link list is weaker than watching the real process.
These observe one run on your machine:

```bash
# Every file the launcher and the game touch:
sudo fs_usage -w -f filesys 2>/dev/null | grep -i "BG3 Camera Unlock"

# Network endpoints held by the mod's own processes (the game and Steam
# have their own, unrelated to this mod):
lsof -i -a -c "BG3 Camera Unlock"
```

`lsof` on the launcher process should show no network sockets of its own.

---

## What it writes

The camera changes are writes into the running game process's memory and are
gone when the game exits. On disk, in normal use:

| Path | What | Lifetime |
|---|---|---|
| `~/Documents/BG3 Camera Unlock/config.ini` | your settings, seeded from a bundled default | until you delete it |
| `~/Documents/BG3 Camera Unlock/mod.log`, `launcher.log` | logs (see "diagnostics" below) | persist, appended each run |
| `~/Documents/BG3 Camera Unlock/inputconfig_p1.json` | a **symlink** to the profile below, for convenience | a link only |
| `…/Larian Studios/Baldur's Gate 3/PlayerProfiles/Public/inputconfig_p1.json` | your input-binding profile, edited to add the camera bindings | persists; `…-backup` and `…-state` files are written beside it first |
| system temp dir | one short status file the launcher reads then deletes | deleted on exit |
| `com.larian.bg3` `NoLauncher` preference | set to skip Larian's launcher window | restored when the launcher exits |
| `dev.andrii.bg3-camera-unlock.*` preferences | the launcher remembers your BG3 install path | persist |

The first run after an upgrade also moves any leftover files from the old
`~/Library/Logs/BG3 Camera Unlock/` and
`~/Library/Application Support/BG3 Camera Unlock/` locations into
`~/Documents/BG3 Camera Unlock/`.

It does **not** modify the game executable, the game app bundle, or any save
file. It requires no admin password and installs no launch agent, daemon, or
login item.

### Local diagnostics vs. telemetry

`mod.log` and `launcher.log` record what the mod did: pattern matches, hook
state, the resolved configuration, and — with `VerboseLogging=true` —
per-event camera and input state. That is diagnostic data, written to a
local file you can read and delete. Nothing is transmitted anywhere; there
is no analytics, no account, and no reporting endpoint.

If a flavor switch or restore leaves your bindings wrong, the `…-backup`
file beside your profile is the untouched original.

---

## Verify your download

Every release publishes `SHA256SUMS.txt`.

```bash
shasum -a 256 ~/Downloads/BG3-Camera-Unlock-*-macOS-arm64-*.dmg
```

The result must match the line in `SHA256SUMS.txt` on the release page. This
confirms the file you have is the file that was published — it does not, on
its own, tell you anything about what that file does.

```bash
codesign --verify --deep --strict --verbose=2 "/Applications/BG3 Camera Unlock (Camera Only).app"
```

This confirms the bundle has not been altered since it was ad-hoc signed. It
does not establish who signed it.

---

## Build from source and run your own

The strongest option, and it costs only time. The Corresponding Source is
published alongside each release as `…-source.zip`.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBG3_CAMERA_WITH_WASD=ON
cmake --build build --parallel
```

An app you compiled from source you have read is not something you have to
take on trust. Note that building the same source does not by itself prove
that the DMG on the release page is byte-identical to your build — the DMG
container is not reproducible run to run. Use your own build if that matters
to you.

The repository includes a GitHub Actions workflow
(`.github/workflows/release.yml`) that builds a release from its tagged
commit; where that workflow is used, its log shows the commands that
produced the artifacts.

---

## How activation works

1. The launcher finds your Steam copy of the game and starts it with the mod
   library in `DYLD_INSERT_LIBRARIES`.
2. macOS loads the library before the game's `main`.
3. The library scans the running executable for a fixed set of required
   ARM64 instruction sequences (14 for Camera Only, 15 for Camera + WASD).
4. It requires **exactly one match for each**. Zero matches or two matches
   both abort.
5. It sanity-checks that the camera data it found still looks like camera
   data.
6. Hooks are installed transactionally but stay **dormant**.
7. Only after the game's camera subsystem initialises and validates are they
   activated, atomically.

If any of those checks fails, the injected module disables itself and the
game runs with its original camera. No address is hardcoded. The
launcher's configuration and binding changes, however, run **before** the
game starts, so they may already be on disk (with a backup) even when the
module then stands down. These checks can stop the camera overrides; they
are not a guarantee that every possible incompatibility is caught.

You can read the decision trail in `~/Documents/BG3 Camera Unlock/mod.log`:
`[ACTIVE]` means it worked, `[ABORT]` means it stood down.

---

## Reporting a security issue

If you find something that looks wrong — unexpected file access, a network
connection, anything that contradicts this page — email
[hello@stealth.vision](mailto:hello@stealth.vision). For a security issue,
prefer email over a public GitHub issue, and please do not post sensitive
details in a public thread.

Please include your macOS version, the mod version, and how you observed it.
