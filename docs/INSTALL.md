# BG3 Camera Unlock — install & uninstall

A native macOS camera/input mod for **Baldur's Gate 3, Steam edition, on
Apple Silicon**. It is not a `.pak` mod and does nothing on Windows or Intel.

## Which flavor

There are two builds. Pick one — installing both is fine, they are separate
apps, but only run one at a time.

| Flavor | What it does |
|---|---|
| **Camera Only** | All the camera changes (vertical look, zoom, trackpad/mouse rotation, collision help). Your W/A/S/D keys and camera-pan bindings are untouched — WASD still pans the camera. Both flavors add the Q/E, PageUp/PageDown and scroll-wheel bindings the camera actions need. |
| **Camera + WASD** | Everything in Camera Only, **plus** W/A/S/D move your character while keyboard UI mode is active. To make room, camera panning moves to the arrow keys. |

The flavor is shown in the app's name, in `launcher.log`, and in the first
lines of `mod.log`.

## Requirements

- Apple Silicon Mac (M1 or newer). Intel and Rosetta are not supported.
- macOS 12.0 or newer.
- Baldur's Gate 3, **Steam** edition. Verified against game build
  `4.1.1.7398727`.
- Steam installed and signed in.

## Install

1. Open the `.dmg` and drag **BG3 Camera Unlock (…)** into **Applications**.
2. The app is ad-hoc signed, **not** Apple-notarized, so macOS blocks the
   first launch. On macOS 14 and earlier: **right-click the app → Open →
   Open**. On macOS 15 and later, that shortcut is gone — double-click once
   to get the block, then **System Settings → Privacy & Security → Open
   Anyway**. You only do this once.
3. Launch **BG3 Camera Unlock**, not Steam's Play button. It finds the game,
   applies the input bindings it needs, starts BG3 with the mod injected, and
   quits itself once the game reports the mod active.

On the first run it creates:

- `~/Documents/BG3 Camera Unlock/config.ini` — your settings.
- `~/Documents/BG3 Camera Unlock/mod.log`, `launcher.log` — logs a bug report
  needs.
- A one-time snapshot of your BG3 input bindings next to the game profile
  (`inputconfig_p1.json.bg3-camera-unlock-backup`), plus a small state file it
  uses to undo only its own binding changes later.

## Switching flavor

Install the other flavor and launch it. On that launch the launcher reverts
the previous flavor's *exclusive* bindings — entries the new flavor does not
itself use, and only where the profile still holds exactly what the mod
wrote, so anything you changed yourself is left alone — then applies the new
flavor's bindings. The camera bindings both flavors share (Q/E,
PageUp/PageDown, wheel tokens) stay in place. Switching back and forth does
not pile up changes or backups. This is a flavor switch, not a full
uninstall of the binding changes — for that, see below.

## Uninstall

1. Quit BG3.
2. Drag the app to the Trash.
3. Removing the app **does not by itself restore your BG3 input bindings.**
   To restore them:
   - **Full restore** — in
     `~/Documents/Larian Studios/Baldur's Gate 3/PlayerProfiles/Public/`,
     replace `inputconfig_p1.json` with
     `inputconfig_p1.json.bg3-camera-unlock-backup` and delete
     `inputconfig_p1.json.bg3-camera-unlock-state`. This is your profile as
     of when the backup was taken; keybinding changes you made afterwards
     are lost.
   - **Full reset** — in BG3, Options → Keybindings → *Reset to Defaults*.
     This also clears keybindings you set yourself.
   - **Partial** — launching the other flavor once reverts only that
     flavor's exclusive bindings, as above.
4. Optionally delete `~/Documents/BG3 Camera Unlock/` (config, logs, and the
   `inputconfig_p1.json` convenience symlink).
5. If a launch was interrupted and the Larian launcher no longer appears,
   run once in Terminal: `defaults delete com.larian.bg3 NoLauncher`.

Nothing is installed into the game folder; the game executable, its app
bundle and your saves are never modified. Once your bindings are restored,
BG3 launched from Steam is back to stock.

## Reporting and contact

For bug reports and compatibility issues, use GitHub Issues or email
[hello@stealth.vision](mailto:hello@stealth.vision). Include your mod
version, game version, macOS version, and the logs from
`~/Documents/BG3 Camera Unlock/`.
