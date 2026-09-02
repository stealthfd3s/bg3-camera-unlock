# BG3 Camera Unlock for macOS

[![version](https://img.shields.io/badge/version-1.0.0-C27B2B)](CHANGELOG.md)
[![platform](https://img.shields.io/badge/platform-macOS%2012%2B%20·%20Apple%20Silicon-lightgrey)](#requirements)
[![game build](https://img.shields.io/badge/BG3-4.1.1.7398727-8B2E1F)](#requirements)
[![license](https://img.shields.io/badge/license-GPL--3.0--or--later-blue)](LICENSE)

**Look up. Look down. Get close.** Baldur's Gate 3 on macOS locks the RPG
camera: the right stick only rotates, zoom is a fixed arm, and the camera
never comes near your character. This mod unlocks vertical look, a proper
zoom chord, a much closer arm, and — unlike earlier controller-only
versions — full **trackpad and mouse** camera control as well.

Tested in-game in single-player and multiplayer, and in RPG and tactical
modes. Some features are experimental; see [Known issues](#known-issues).

---

## Two flavors

Pick one. They come from one codebase; the only difference is keyboard
character movement.

| Flavor | Choose it if |
|---|---|
| **Camera Only** | You want every camera change and nothing else. Your **W/A/S/D keys and camera-pan bindings are untouched** — WASD still pans the camera. (Both flavors bind Q/E, PageUp/PageDown and the scroll-wheel tokens the camera actions need; see [The bindings the launcher writes](#the-bindings-the-launcher-writes).) |
| **Camera + WASD** | You also want W/A/S/D to move your character in keyboard UI mode. Camera panning moves to the arrow keys to make room. Controller play is identical to Camera Only. |

The flavor is in the app's name, in `launcher.log`, and in the first lines
of `mod.log`. Installing both is fine; run one at a time. See
[Switching flavor](#switching-flavor).

---

## What it does

| | Stock game | With this mod |
|---|---|---|
| Right stick / trackpad / mouse, vertical | No free vertical look | Pitch the camera, `-45°` to `+85°` |
| Pitch range | Fixed by the game | Configurable anywhere in `-89°` … `+89°` |
| Zoom | Fixed camera arm | L3 + right stick, or the scroll wheel; arm limits configurable |
| Closest approach | The game's own limit | Down to `0.5` world units — near enough for a face |
| Camera collision | — | Best-effort wall and floor protection |
| Trackpad two-finger swipe | Not bound to the camera | Rotates and pitches the camera |
| Middle-mouse drag | Not bound to the camera | Horizontal + vertical camera look |

- **Vertical camera control** on the right stick, the trackpad and the
  middle-mouse drag. Vertical speed derives from the camera's own angular
  rate, so it reads at the same speed as horizontal rotation. Pitch works in
  both RPG and tactical modes.
- **L3 + right stick zoom** that does *not* trigger a mode switch when you
  release L3. A short L3 click still toggles RPG / point-and-click as normal.
- **Six independent sensitivity settings** — horizontal and vertical, per
  device (gamepad / trackpad / mouse), so tuning one never moves another.
- **Pitch survives dialogue** and temporary camera changes.
- **The camera changes themselves are in-memory only** and the game's
  executable, app bundle and saves are never modified. The launcher does
  write its own config and logs, and edits your input-binding profile with a
  backup — see [What it writes](#what-it-writes).

---

## Requirements

- **Apple Silicon Mac** (M1 or newer). Intel and Rosetta are not supported.
- **macOS 12.0** or newer.
- **Baldur's Gate 3, Steam edition**, build `4.1.1.7398727`.
- A controller, a trackpad, or a mouse — any of the three drives the camera.

Not supported or untested: GOG and other non-Steam builds, split-screen,
multiple simultaneous controllers, Photo Mode, and unusual accessibility
remappings.

**No Script Extender, no Mod Fixer, no BG3 Mod Manager, and no `.pak` file
is involved.** Those are Windows data-mod tooling; this is a native macOS
launcher and does not interact with them.

---

## Install

1. **Quit Baldur's Gate 3 completely.**
2. Open the `.dmg` for the flavor you want, or double-click the Nexus `.zip`
   to extract it.
3. Drag **BG3 Camera Unlock (Camera Only)** or **BG3 Camera Unlock
   (Camera + WASD)** onto the **Applications** shortcut.
4. Make sure **Steam is running** and you are signed in.
5. Launch the game through that app — not through Steam's Play button.

The `Applications` item in the installer window is a shortcut, not a folder
on the disk image. The app is installed only once you drag it across. Running
it from the mounted `Install BG3 Camera Unlock` volume is not an
installation; eject the image once the copy finishes.

The launcher finds your Steam copy of BG3 automatically, reading
`libraryfolders.vdf` if the game lives in a custom Steam library. If it still
cannot find it, it will ask you to pick `Baldur's Gate 3.app` yourself.

### macOS will warn you on first launch

Releases are **ad-hoc signed, not Apple-notarized**, so macOS refuses the
first launch with a message about an unidentified developer.

**To open it (works on every macOS version):**

1. Double-click **BG3 Camera Unlock** once. macOS blocks it — that is
   expected, and it is the step that makes the button below appear.
2. Open **System Settings → Privacy & Security**.
3. Scroll to the **Security** section. You will see a line naming
   *BG3 Camera Unlock*, with an **Open Anyway** button. Click it.
4. Confirm with Touch ID or your password, then launch the app again.

You only do this once. macOS remembers.

> **On macOS 14 and earlier** you can instead right-click the app and choose
> **Open**. Apple removed that shortcut in macOS 15 (Sequoia), so on newer
> systems use the steps above.

**No Terminal command is needed.** The steps above go through the path Apple
provides for this; there is no need to run `xattr` or other commands to
strip quarantine flags.

**That warning is not a scan result.** Gatekeeper shows it for any app that
has not been notarized by Apple — notarization needs a paid Apple Developer
account, which this free mod does not have. It means Apple has not checked
this build; it does not mean anything was found, and the absence of the
warning would not by itself mean an app is safe.

If you would rather verify than trust — and for a mod that injects code
into your game, you probably should — **[SECURITY.md](SECURITY.md)** shows
you how to check, with commands you run yourself, what the mod links, what
files it touches while running, and whether it opens any network
connection. It does not modify the game executable, the app bundle, or your
saves; the files it does write are listed in [What it writes](#what-it-writes).

Every release also ships `SHA256SUMS.txt` so you can confirm your download
matches the checksum that was published (an integrity check, not a safety
guarantee):

```sh
shasum -a 256 ~/Downloads/BG3-Camera-Unlock-*-macOS-arm64-*.dmg
```

### Vortex and BG3 Mod Manager

Neither is supported, and neither is needed. Those tools install `.pak` data
mods into the game's mod folder. This mod is a standalone macOS application
that launches the game with a library injected into the new process — there
is nothing for a mod manager to install. Use **Manual Download** on Nexus.

---

## Configuration

The first launch creates:

```
~/Documents/BG3 Camera Unlock/config.ini
```

To open it: in Finder press **⇧⌘G**, paste that path, and open `config.ini`
in TextEdit. **Quit BG3 before editing, and restart it to load new values** —
there is no live reload.

The file lives outside the `.app` deliberately, so editing it neither alters
the bundle nor invalidates its signature.

### Options

| Option | Type | Default | Range | What it does |
|---|---|---|---|---|
| `ConfigVersion` | integer | `1` | — | Schema version. Leave alone unless a release note says otherwise. |
| `PitchMinimumDegrees` | float | `-45.0` | `-89` … `88` | How far down you can look. Must be below the maximum. |
| `PitchMaximumDegrees` | float | `85.0` | `-88` … `89` | How far up you can look. |
| `GamepadHorizontalSensitivity` | float | `2.0` | `0.10` … `4.00` | Gamepad yaw speed. `1.0` matches the game's own full-scale rate. |
| `GamepadVerticalSensitivity` | float | `1.0` | `0.10` … `3.00` | Gamepad pitch speed. |
| `TrackpadHorizontalSensitivity` | float | `1.0` | `0.10` … `8.00` | Trackpad two-finger yaw. `1.0` reproduces the current swipe exactly (normalized over the proven `3.0` base). |
| `TrackpadVerticalSensitivity` | float | `1.0` | `0.10` … `3.00` | Trackpad precise vertical swipe pitch. |
| `MouseHorizontalSensitivity` | float | `1.0` | `0.10` … `8.00` | Scales BG3's native middle-drag yaw. |
| `MouseVerticalSensitivity` | float | `1.0` | `0.10` … `3.00` | Vertical look from a middle-button drag up/down. |
| `HorizontalSensitivity` | float | `2.0` | `0.10` … `4.00` | Legacy alias for `GamepadHorizontalSensitivity`. |
| `PointerRotateSensitivity` | float | `3.0` | `0.10` … `8.00` | Legacy alias for `TrackpadHorizontalSensitivity` (normalized: `3.0` → `1.0`). |
| `VerticalSensitivity` | float | `1.0` | `0.10` … `3.00` | Legacy alias for `GamepadVerticalSensitivity`, and for `TrackpadVerticalSensitivity` when that canonical key is absent. |
| `ZoomSensitivity` | float | `0.6` | `0.05` … `3.00` | Top zoom speed at full stick deflection. Does not change distance limits. |
| `ZoomResponseCurve` | float | `2.0` | `1.00` … `4.00` | Stick response shape. `1.0` is linear; higher gives finer control near centre. |
| `ZoomMinimumResponse` | float | `0.12` | `0.00` … `0.50` | Zoom rate the instant the stick leaves the deadzone. Raise if zoom feels unresponsive near centre. |
| `ZoomForceActive` | bool | `true` | — | Treat the zoom axis as active past the deadzone. `false` restores the game's own behaviour. |
| `PointerRotationNative` | bool | `true` | — | Advanced. Hand a trackpad/mouse swipe to BG3's own pointer rotation channel. `true` is the tested path; `false` falls back to the older heading-writing behaviour. |
| `PointerRotationHoldsHeading` | bool | `true` | — | Advanced. Hold the per-frame follow correction still for the duration of a swipe so it is not partly undone in the same frame. |
| `PointerRotateSmoothingMs` | float | `30` | `0` … `250` | Advanced. How long unspent trackpad travel takes to become rotation. Native mode keeps a 30 ms stability floor. |
| `PointerRotationFollowHoldSeconds` | float | `3600` | `0` … `3600` | Advanced. How long after the last swipe the follow correction stays held. The default holds it for a whole swiping session; `0` holds it only during a swipe. |
| `PointerZoomModifierEvent` | integer | `0` | `0` … `65535` | Trackpad/mouse button that acts like L3: hold it and the vertical axis zooms instead of pitching. `0` disables. See [docs/CONFIG.md](docs/CONFIG.md) for how to find your button's id. |
| `PointerZoomModifierToggle` | bool | `false` | — | `false` = hold the button to zoom. `true` = each press flips between pitch and zoom, for buttons the game reports no release for. |
| `PointerZoomHoldButton` | integer | `0` | `0` … `3` | Physical mouse button held to zoom: `0` off, `1` left, `2` right, `3` middle. Reads the button's real state, so holding works even though the game reports press and release together. |
| `ZoomDeviceId` | integer | `-1` | `-1` … `65535` | Input device whose zoom action means **zoom** rather than pitch. Lets a keyboard key zoom while the trackpad keeps pitching, with no modifier to hold. `-1` disables; `0` is a real device. |
| `ZoomDeviceSensitivity` | float | `0.25` | `0.01` … `1.00` | Zoom step per key press from the zoom device. Lower is finer. A key has no travel, so this is the only thing that decides how coarse it feels. |
| `StickDeadzone` | float | `0.15` | `0.00` … `0.80` | Ignore small right-stick movement. |
| `MinimumZoomDistance` | float | `0.5` | `0` or `0.05` … `50.00` | Closest the camera may come, in world units. `0` keeps the game's limit. |
| `MaximumZoomDistance` | float | `0` | `0` or `1.00` … `500.00` | Furthest the camera may pull back. `0` keeps the game's limit. |
| `KeyboardMovement` | bool | `true` | — | **Camera + WASD only.** Allow bound WASD character movement in keyboard UI mode; does not alter the controller path. In **Camera Only** the key is parsed but forced off — it cannot enable WASD there. |
| `VerboseLogging` | bool | `false` | — | Adds per-event camera state to the log. Startup and verification are logged either way. |
| `ObstacleCollision` | bool | `true` | — | Experimental wall/prop sweep. |
| `FloorProtection` | bool | `true` | — | Experimental floor query. |
| `CollisionSafetyMargin` | float | `0.20` | `0.00` … `2.00` | Clearance kept from a wall, in world units. |
| `FloorSafetyOffset` | float | `1.00` | `0.00` … `3.00` | Clearance kept from the floor, in world units. |

Invalid or unknown values are **ignored and reported in the log**; the mod
keeps a safe default rather than failing. A bad config can never crash the
game. See [docs/CONFIG.md](docs/CONFIG.md) for tuning advice.

---

## Controls

Camera pitch works in both **RPG and tactical** modes. The **Camera + WASD**
edition also supports WASD character movement in both modes; **Camera Only**
does not add WASD character movement. Point-and-click mode is not a target of
this mod.

### Gamepad

| Input | Result |
|---|---|
| Right stick ←→ | Horizontal rotation |
| Right stick ↑↓ | **Camera pitch** |
| Hold **L3** + right stick ↑↓ | Zoom in / out, no mode switch on release |
| Short **L3** click | Normal RPG / point-and-click toggle |
| Left stick | Character move (RPG) / tactical pan — unchanged |

### Trackpad

| Input | Result |
|---|---|
| Two-finger swipe ←→ | Rotate the camera |
| Two-finger swipe ↑↓ | Pitch the camera |
| Two-finger swipe, diagonal | Locks to one axis for the gesture; no unwanted mixing |
| Lift fingers | Motion stops — momentum is discarded, no drift |

The horizontal swipe is read raw from macOS, before BG3 classifies it, and
scaled by BG3's own factor. Vertical scroll still zooms unless a swipe is in
progress.

### Mouse

| Input | Result |
|---|---|
| Hold middle button + drag ←→ | BG3's native horizontal camera drag |
| Hold middle button + drag ↑↓ | Vertical camera look |
| Release middle button | Pitch stops |
| Scroll wheel | Zoom (one discrete step per notch) |

### Keyboard

| Input | Result |
|---|---|
| **Q** / **E** | Rotate at BG3's original keyboard speed |
| **PageUp** / **PageDown** | Pitch / zoom (the mod's vertical axis) |
| Arrow keys | Camera pan (**Camera + WASD** flavor only) |
| **W A S D** | Move your character (**Camera + WASD** flavor only) |

### The bindings the launcher writes

BG3's engine already carries every action involved but ships them unbound for
these inputs, so the launcher writes them into your own profile's input
config on each start:

```
~/Documents/Larian Studios/Baldur's Gate 3/PlayerProfiles/Public/inputconfig_p1.json
```

- **Both flavors** bind `CameraRotateLeft` / `CameraRotateRight` (adding
  `q`, `e` and the horizontal `wheel_x` tokens a two-finger swipe needs) and
  `CameraZoomIn` / `CameraZoomOut` (adding `pageup`, `pagedown` and the
  vertical wheel).
- **Camera + WASD** additionally binds `CharacterMoveForward` and its three
  siblings to `w a s d`, and writes explicit arrow-key bindings for
  `CameraForward` / `CameraBackward` / `CameraLeft` / `CameraRight` — because
  leaving those actions absent makes BG3 silently restore its WASD defaults
  for camera panning.
- **Camera Only** never touches `CharacterMove*` or the `Camera*` pan
  actions, so your stock WASD camera pan is left exactly as you had it.

An entry in that file is positional by device, `[controller, keyboard,
mouse]`. The controller slot is not padding: the launcher keeps BG3's own
token there so controller play is unchanged. `rightstick_xneg` is the game's
*right* rotation, not its left.

The first modifying launch copies the original file beside itself
(`inputconfig_p1.json.bg3-camera-unlock-backup`) and keeps a small state file
recording, per action, what it wrote and what was there before. If a profile
does not exist yet, or the file cannot be read or backed up, the bindings are
skipped, the file is left alone, and the reason is in `launcher.log`.

### Switching flavor

Install the other flavor and launch it. On that launch it reverts the
bindings the previous flavor used **that the new flavor does not** — and
only entries that still hold exactly what the mod wrote, so anything you
changed yourself is kept — then applies the new flavor's bindings. The
camera bindings both flavors share (Q/E, PageUp/PageDown, wheel tokens for
zoom and rotate) stay in place across the switch; switching is not a full
uninstall of the binding changes. Switching back and forth does not
accumulate changes, backups, or duplicate bindings. For a full revert, see
[Uninstall](#uninstall).

## Is this safe?

**The game executable, its app bundle, and your saves are never modified.**
The launcher does not patch or replace anything inside the BG3 app bundle.
It does write the files listed under [What it writes](#what-it-writes).

What it actually does:

1. Temporarily sets BG3's own `NoLauncher` preference, supplies the real
   Steam App ID, and starts the game with `DYLD_INSERT_LIBRARIES` pointing at
   the bundled library. The preference is restored afterwards.
2. macOS loads that library before the game's `main`.
3. The library locates the running executable's `__TEXT,__text` section and
   pattern-scans a fixed set of required ARM64 code signatures — **14** for
   Camera Only, **15** for Camera + WASD (the extra one is the keyboard-mode
   guard that flavor patches).
4. It requires **exactly one match for every signature**. Not one missing,
   not one ambiguous.
5. It sanity-checks that the camera arm bounds still read like an ordered
   pair of plausible distances, and disables the zoom override if they do not.
6. Hooks are installed **transactionally but dormant**, then activated
   atomically only after the camera subsystem is validated.

No address is ever hardcoded. The camera changes are writes into the running
game process's memory; macOS discards that memory when BG3 exits.

**If a game update moves anything, the injected module fails closed:** the
camera overrides do not activate, BG3 runs unmodified, and the launcher
tells you. Note this is the *module's* behaviour — the launcher applies its
configuration and binding changes earlier, before the game starts, so those
may already have been written (with a backup) even when the module then
stands down. The compatibility checks can stop the overrides; they cannot
guarantee that every possible incompatibility is detected.

### What it writes

- `~/Documents/BG3 Camera Unlock/config.ini` — your settings, created on
  first launch from a bundled default. Persists until you delete it.
- `~/Documents/BG3 Camera Unlock/mod.log`, `launcher.log` — logs. Persist;
  appended to on each run.
- `~/Documents/BG3 Camera Unlock/inputconfig_p1.json` — a **symbolic link**
  to your BG3 profile below, created for convenience so both files sit in
  one folder. A link, not a copy; removing the mod folder removes only the
  link.
- `~/Documents/Larian Studios/Baldur's Gate 3/PlayerProfiles/Public/inputconfig_p1.json`
  — your input-binding profile, edited to add the camera bindings. The
  first modifying launch first copies it to
  `…json.bg3-camera-unlock-backup` and writes a `…json.bg3-camera-unlock-state`
  file recording what it changed. These persist until you restore or delete
  them (see [Uninstall](#uninstall)).
- One temporary status file in the system temp directory, which the
  launcher deletes when the game reports ready.
- `com.larian.bg3` `NoLauncher` preference — set to skip Larian's launcher
  window, restored when the launcher exits.
- The launcher's own preferences (`dev.andrii.bg3-camera-unlock.*`), where
  it remembers the path to your BG3 install.
- On the first run after upgrading from an older version, any leftover
  files in `~/Library/Logs/BG3 Camera Unlock/` and
  `~/Library/Application Support/BG3 Camera Unlock/` are moved into
  `~/Documents/BG3 Camera Unlock/`.

**Not convinced?** [SECURITY.md](SECURITY.md) has commands you can run to
check the linked libraries, watch file and network activity while it runs,
and build from source to use your own binary instead.

### Save safety

The mod stores nothing in your saves and changes no game state — it only
affects where the camera is. Saves made while it is active load normally
without it.

### Uninstall

1. Quit the game.
2. Move the app to the Trash.

**Deleting the app does not by itself restore your BG3 input bindings** — the
mod only changes them while the launcher runs, and the launcher is gone.
To restore your bindings:

- **Full restore** — put back the exact profile from before the mod's first
  run: in
  `~/Documents/Larian Studios/Baldur's Gate 3/PlayerProfiles/Public/`,
  replace `inputconfig_p1.json` with `…json.bg3-camera-unlock-backup` and
  delete `…json.bg3-camera-unlock-state`. This is the state as of when the
  backup was taken, so any keybinding changes you made afterwards are lost.
- **Full reset** — in BG3, Options → Keybindings → *Reset to Defaults*. This
  also clears any keybindings you set yourself.
- **Partial** — launching the *other* flavor once reverts only that flavor's
  exclusive bindings (`CharacterMove*` and the arrow-key camera pan); the
  shared camera bindings stay. It is a flavor switch, not an uninstall.

Optionally also remove `~/Documents/BG3 Camera Unlock/` (your config, logs,
and the `inputconfig_p1.json` convenience link). If a launch was interrupted
and the Larian launcher no longer appears, run once:
`defaults delete com.larian.bg3 NoLauncher`.

---

## Known issues

This mod pushes the camera into angles the game was never built for, so some
of these are inherent rather than bugs to be fixed.

- **Floor collision is not reliable everywhere.** BG3 can return a lower
  storey's floor, or none at all, for the camera probe. Stairs, slopes,
  bridges, and stacked interiors may still let the camera dip into the floor.
- **Walls and large props can still clip.** One camera sweep cannot cover
  every geometry, material, and streaming case.
- **Collision correction can visibly tighten or release zoom.** Stabilisation
  reduces jitter but does not eliminate it in difficult geometry.
- **Ceilings, roofs, upper floors, and distant room pieces may disappear.**
  This is BG3's own culling and level streaming, which assumes the original
  camera range. Higher graphics settings sometimes help; nothing fully fixes
  it.
- **Untested:** split-screen, multiple controllers, Photo Mode, unusual
  accessibility mappings, non-Steam builds.
- **Other native camera or input mods will likely conflict.** Do not run two
  mods that hook the same functions.

---

## Troubleshooting

**The camera doesn't move vertically.**
Check `~/Documents/BG3 Camera Unlock/mod.log`. An `[ACTIVE]` line means
every hook installed. An `[ABORT]` line means the fail-closed check stopped
it — the most likely cause is a game update that moved the signatures.

**macOS refuses to open the app.**
See [Gatekeeper](#gatekeeper) above.

**The launcher can't find Baldur's Gate 3.**
Start Steam and sign in first. If the game is in a custom library the
launcher reads `libraryfolders.vdf`; failing that it will prompt you to
select `Baldur's Gate 3.app` manually.

**The game starts but nothing changed.**
Make sure you launched via **BG3 Camera Unlock**, not Steam's Play button.
Steam's own launch path does not carry the injection.

**Zoom feels dead near the centre of the stick.**
Raise `ZoomMinimumResponse` (try `0.20`), or lower `ZoomResponseCurve`
toward `1.0`.

**The camera clips into the floor.**
Raise `FloorSafetyOffset`. If it persists, please report it with your
location — see [Reporting and contact](#reporting-and-contact).

### Logs

- Mod: `~/Documents/BG3 Camera Unlock/mod.log`
- Launcher: `~/Documents/BG3 Camera Unlock/launcher.log`

---

## Reporting and contact

For bug reports and compatibility issues, use GitHub Issues or email
[hello@stealth.vision](mailto:hello@stealth.vision). Please include your mod
version, game version, macOS version, and the relevant logs
(`~/Documents/BG3 Camera Unlock/mod.log`, and `launcher.log` if the game did
not start).

To report a **security** issue privately, email
[hello@stealth.vision](mailto:hello@stealth.vision) rather than opening a
public issue — see [SECURITY.md](SECURITY.md).

---

## FAQ

**Does this work on Windows or Intel Macs?**
No. It is Apple Silicon macOS only. The signatures are ARM64 machine code
from the macOS build; nothing here applies to the Windows executable.

**Do I need the Script Extender?**
No. This is not a Script Extender mod and does not interact with it.

**Does it work in multiplayer?**
Yes. The mod has been tested in multiplayer and works as it does in
single-player. It changes only your local camera, in your own process
memory, and does not add a network path.

**Will it work after a game update?**
An update does not necessarily break the mod; compatibility depends on what
changes in the game. If you hit a problem after an update, please report the
game version, mod version, symptoms and relevant logs — see
[Reporting and contact](#reporting-and-contact). I'll investigate and try to
resolve it where possible; there is no promise of a timeline or a guaranteed
fix. The injected module's own compatibility checks are described under
[Is this safe?](#is-this-safe).

**Can I use it with other mods?**
Data (`.pak`) mods, yes — different mechanism entirely. Other *native camera
or input* mods, no.

**Why does it need to launch the game?**
Injection has to happen before the game's `main` runs. Steam's Play button
cannot carry the environment variable that does it.

---

## Building from source

Requires the Xcode Command Line Tools and CMake 3.20+.

```sh
# Camera + WASD (default); pass -DBG3_CAMERA_WITH_WASD=OFF for Camera Only.
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBG3_CAMERA_WITH_WASD=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Build every release artifact for **both** flavors (2 DMGs, 2 Nexus zips, one
complete-source zip, checksums):

```sh
./scripts/package.sh
```

Set `BG3_GAME_BINARY=/path/to/bg3-arm64` to also run `pattern_test` during
packaging, or `FLAVOR=camera-only` to build just one.

Verify the signatures against a game binary you extracted yourself:

```sh
./build/pattern_test /path/to/bg3-arm64
```

Every pattern must report exactly one match. See
[CONTRIBUTING.md](CONTRIBUTING.md) for the full workflow.

---

## License

BG3 Camera Unlock is licensed under the **GNU General Public License,
version 3 or later** ([GPL-3.0-or-later](LICENSE)), with the additional
permission in [MODDING-EXCEPTION.txt](MODDING-EXCEPTION.txt).

You may use, modify and redistribute the project, including commercially,
under these terms. A redistribution must preserve the required copyright,
license and warranty notices and identify any modifications. A derivative
work covered by the GPL must comply with it, including making the
Corresponding Source available when you distribute the work in binary form.

The modding exception permits the interoperation described in that file. It
grants no rights to third-party game code, assets or trademarks.

The Corresponding Source for each release is published alongside the
binaries as `BG3-Camera-Unlock-macOS-arm64-<version>-source.zip`; one
archive builds both editions.

If you reuse this code in another project, a link back and a message to
[hello@stealth.vision](mailto:hello@stealth.vision) are appreciated. Neither
is required by the license, and you do not need the author's approval to
exercise its permissions.

---

## Credits

Developed by **stealthfd3s**. A native macOS / Apple Silicon camera mod for
Baldur's Gate 3.

---

## Disclaimer

**This is an unofficial, community-made project. It is not affiliated with,
authorized by, sponsored by, or endorsed by Larian Studios, Valve
Corporation, or Apple Inc.**

Baldur's Gate 3 and all related content are the property of Larian Studios.
This project ships no Baldur's Gate 3 assets — no artwork, icons, audio,
models, or data files — and installs into no game folder. It does contain a
few dozen short machine-code signatures copied from the game's executable so
it can locate the functions it hooks; see [NOTICE](NOTICE).

No warranty. Use at your own risk.
