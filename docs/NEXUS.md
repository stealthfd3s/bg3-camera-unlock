# Nexus Mods description (BBCode)

Paste everything between the markers into the Nexus **Description** field.

**This file and `README.md` must be kept factually in sync** — see
`CONTRIBUTING.md`. Nexus uses BBCode, not Markdown.

---

<!-- ============ BEGIN BBCODE — PASTE BELOW THIS LINE ============ -->

[size=5][b]BG3 Camera Unlock for macOS[/b][/size]

[b]Look up. Look down. Get close.[/b]

Baldur's Gate 3 on macOS locks the RPG camera: the right stick only rotates, zoom is a fixed arm, and the camera never comes near your character. This mod unlocks vertical look, a proper zoom chord, and a much closer arm — and, unlike earlier controller-only versions, it drives the camera from the [b]trackpad and mouse[/b] as well.

[b]Apple Silicon Macs only.[/b] This is a native ARM64 mod for the macOS Steam build. It is not a [font=Courier New].pak[/font] mod and does not work on Windows.

Tested in-game in single-player and multiplayer, and in RPG and tactical modes. Some features are experimental — see [b]Known issues[/b] below.

[line]

[size=4][b]Two flavors — pick one[/b][/size]

[list]
[*][b]Camera Only[/b] — every camera change and nothing else. Your W/A/S/D keys and camera-pan bindings are untouched; WASD still pans the camera. (Both flavors bind Q/E, PageUp/PageDown and the scroll-wheel tokens the camera actions need.)
[*][b]Camera + WASD[/b] — the same, plus W/A/S/D move your character in keyboard UI mode. Camera panning moves to the arrow keys to make room. Controller play is identical to Camera Only.
[/list]

Installing both is fine; run one at a time. Switching flavor reverts the previous flavor's exclusive binding changes (only the ones you did not change yourself) and applies the new flavor's; the camera bindings both flavors share stay in place. It is a flavor switch, not a full uninstall of the binding changes.

[line]

[size=4][b]Features[/b][/size]

[list]
[*][b]Vertical camera control in RPG and tactical modes[/b] — pitch from [font=Courier New]-45°[/font] to [font=Courier New]+85°[/font] by default, configurable anywhere within [font=Courier New]-89°[/font] to [font=Courier New]+89°[/font], on the right stick, the trackpad, and the middle-mouse drag.
[*][b]Trackpad camera control[/b] — a two-finger swipe rotates and pitches, with a per-gesture axis lock, no momentum drift, and no teleporting.
[*][b]Mouse camera control[/b] — middle-button drag for horizontal and vertical look; the scroll wheel zooms.
[*][b]Matched rotation feel[/b] — vertical speed derives from the camera's own angular rate, so it reads at the same speed as horizontal rotation.
[*][b]L3 + right stick zoom[/b] that does not trigger a mode switch when you release L3. A short L3 click still toggles RPG / point-and-click as normal.
[*][b]Six independent sensitivity settings[/b] — horizontal and vertical, separately for gamepad, trackpad and mouse, so tuning one never moves another.
[*][b]Get close[/b] — the camera arm can shorten to 0.5 world units, near enough to fill the frame with a character's face.
[*][b]Experimental collision help[/b] — best-effort wall and floor protection at angles the base game never uses.
[*][b]Works in RPG and tactical modes[/b] — camera pitch in both; the Camera + WASD edition also moves your character with WASD in both. Point-and-click mode is not a target of this mod.
[*][b]Pitch survives dialogue[/b] and temporary camera changes.
[*][b]Fully configurable[/b] — over 30 validated settings in a plain text file.
[/list]

[line]

[size=4][b]Requirements[/b][/size]

[list]
[*][b]Apple Silicon Mac[/b] (M1 or newer) — Intel and Rosetta are not supported
[*][b]macOS 12.0[/b] or newer
[*][b]Baldur's Gate 3, Steam edition[/b], build [font=Courier New]4.1.1.7398727[/font]
[*][b]A controller, a trackpad, or a mouse[/b] — any of the three drives the camera
[/list]

[b]No Script Extender, no Mod Fixer, no BG3 Mod Manager, and no [font=Courier New].pak[/font] file is involved.[/b] Those are Windows data-mod tooling; this is a native macOS launcher and does not interact with them.

Not supported or untested: GOG and other non-Steam builds, split-screen, multiple simultaneous controllers, Photo Mode, and unusual accessibility remappings.

[line]

[size=4][b]Installation[/b][/size]

Use [b]Manual Download[/b]. This is a macOS application, not a Vortex or BG3 Mod Manager installer — there is nothing for a mod manager to install.

[list=1]
[*][b]Quit Baldur's Gate 3 completely.[/b]
[*]Open the [font=Courier New].dmg[/font], or double-click the ZIP to extract it.
[*]Drag [b]BG3 Camera Unlock (Camera Only)[/b] or [b]BG3 Camera Unlock (Camera + WASD)[/b] onto the [b]Applications[/b] shortcut.
[*]Make sure [b]Steam is running[/b] and you are signed in.
[*]Launch the game through that app — not through Steam's Play button.
[/list]

The [font=Courier New]Applications[/font] item in the installer window is a shortcut, not a folder on the disk image. The app is installed only once you drag it across.

The launcher finds your Steam copy automatically, reading [font=Courier New]libraryfolders.vdf[/font] if the game is in a custom library. If it still cannot find it, it will ask you to pick [font=Courier New]Baldur's Gate 3.app[/font] yourself.

[b][color=#ffcc44]macOS will block the first launch. This is expected.[/color][/b] Releases are ad-hoc signed, not Apple-notarized, so macOS refuses to open the app until you allow it once:

[list=1]
[*]Double-click [b]BG3 Camera Unlock[/b]. macOS blocks it — this step is what makes the button below appear.
[*]Open [b]System Settings → Privacy & Security[/b].
[*]Scroll to [b]Security[/b]. You will see a line naming [i]BG3 Camera Unlock[/i] with an [b]Open Anyway[/b] button. Click it.
[*]Confirm with Touch ID or your password, then launch the app again.
[/list]

You only do this once. On macOS 14 and earlier you can instead right-click the app and choose [b]Open[/b]; Apple removed that shortcut in macOS 15, so on newer systems use the steps above.

[b]No Terminal command is needed.[/b] The steps above use the path Apple provides; there is no need to run [font=Courier New]xattr[/font] or other commands to strip quarantine flags.

[b]Uninstall:[/b] quit the game and move the app to the Trash. The game executable, its app bundle and your saves are never modified. Deleting the app does [b]not[/b] by itself restore your BG3 input bindings, because the mod only changes them while the launcher runs. For a full restore, put back the [font=Courier New]inputconfig_p1.json.bg3-camera-unlock-backup[/font] file saved next to your profile (and delete the [font=Courier New]…-state[/font] file), or use [i]Reset to Defaults[/i] in the game's Keybindings screen. Launching the other flavor once only reverts that flavor's exclusive bindings, not all of them.

[line]

[size=4][b]Configuration[/b][/size]

The first launch creates:

[font=Courier New]~/Documents/BG3 Camera Unlock/config.ini[/font]

In Finder press [b]Shift-Cmd-G[/b], paste that path, and open it in TextEdit. [b]Quit BG3 before editing and restart it afterwards[/b] — there is no live reload.

[list]
[*][font=Courier New]PitchMinimumDegrees[/font] / [font=Courier New]PitchMaximumDegrees[/font] — vertical look limits. Default [font=Courier New]-45.0[/font] / [font=Courier New]85.0[/font], within [font=Courier New]-89..89[/font].
[*][b]Per-device look sensitivity[/b] — horizontal and vertical, set separately for each device so tuning one never moves another: [font=Courier New]GamepadHorizontalSensitivity[/font] / [font=Courier New]GamepadVerticalSensitivity[/font] (default [font=Courier New]2.0[/font] / [font=Courier New]1.0[/font]), [font=Courier New]TrackpadHorizontalSensitivity[/font] / [font=Courier New]TrackpadVerticalSensitivity[/font] (default [font=Courier New]1.0[/font] / [font=Courier New]1.0[/font]), [font=Courier New]MouseHorizontalSensitivity[/font] / [font=Courier New]MouseVerticalSensitivity[/font] (default [font=Courier New]1.0[/font] / [font=Courier New]1.0[/font]). The old [font=Courier New]HorizontalSensitivity[/font], [font=Courier New]VerticalSensitivity[/font] and [font=Courier New]PointerRotateSensitivity[/font] keys still work as aliases; a canonical key always wins.
[*][font=Courier New]KeyboardMovement[/font] — [b]Camera + WASD only[/b]. Default [font=Courier New]true[/font] there; set [font=Courier New]false[/font] to play with camera control and no keyboard character movement. In Camera Only it is parsed but forced off.
[*][font=Courier New]ZoomSensitivity[/font] — top zoom speed. Default [font=Courier New]0.6[/font], range [font=Courier New]0.05..3.00[/font].
[*][font=Courier New]ZoomResponseCurve[/font] — stick response shape. Default [font=Courier New]2.0[/font], range [font=Courier New]1.00..4.00[/font].
[*][font=Courier New]ZoomMinimumResponse[/font] — zoom rate the instant the stick leaves the deadzone. Default [font=Courier New]0.12[/font], range [font=Courier New]0.00..0.50[/font].
[*][font=Courier New]ZoomForceActive[/font] — default [font=Courier New]true[/font]. Set [font=Courier New]false[/font] to restore the game's own zoom gating.
[*][font=Courier New]PointerZoomModifierEvent[/font] — trackpad/mouse button that acts like L3. Hold it and the vertical axis zooms instead of pitching. Default [font=Courier New]0[/font] (off); see the config guide for how to find your button's id.
[*][font=Courier New]PointerZoomModifierToggle[/font] — [font=Courier New]false[/font] holds the button to zoom; [font=Courier New]true[/font] makes each press flip between pitch and zoom, for buttons that report no release.
[*][font=Courier New]PointerZoomHoldButton[/font] — physical mouse button held to zoom: [font=Courier New]0[/font] off, [font=Courier New]1[/font] left, [font=Courier New]2[/font] right, [font=Courier New]3[/font] middle. Hold it and scroll to zoom; release and the same scroll pitches again.
[*][font=Courier New]ZoomDeviceId[/font] — input device whose zoom action means zoom rather than pitch. Lets a keyboard key zoom while the trackpad keeps pitching, with no modifier held. Default [font=Courier New]-1[/font] (off).
[*][font=Courier New]ZoomDeviceSensitivity[/font] — zoom step per key press from the zoom device. Default [font=Courier New]0.25[/font], range [font=Courier New]0.01..1.00[/font]. Lower it if keyboard zoom feels too coarse.
[*][font=Courier New]StickDeadzone[/font] — default [font=Courier New]0.15[/font], range [font=Courier New]0.00..0.80[/font].
[*][font=Courier New]MinimumZoomDistance[/font] — closest approach in world units. Default [font=Courier New]0.5[/font]; [font=Courier New]0[/font] keeps the game's limit.
[*][font=Courier New]MaximumZoomDistance[/font] — furthest pull-back. Default [font=Courier New]0[/font] (the game's own limit).
[*][font=Courier New]ObstacleCollision[/font] / [font=Courier New]FloorProtection[/font] — experimental collision layers, both [font=Courier New]true[/font].
[*][font=Courier New]CollisionSafetyMargin[/font] — wall clearance. Default [font=Courier New]0.20[/font], range [font=Courier New]0.00..2.00[/font].
[*][font=Courier New]FloorSafetyOffset[/font] — floor clearance. Default [font=Courier New]1.00[/font], range [font=Courier New]0.00..3.00[/font].
[*][font=Courier New]VerboseLogging[/font] — default [font=Courier New]false[/font].
[*][font=Courier New]ConfigVersion[/font] — schema version. Leave it alone.
[/list]

[b]Invalid or unknown values are ignored and reported in the log[/b]; the mod keeps a safe default instead. A bad config can never crash the game.

[b]Tuning tips:[/b] if zoom feels dead near the stick centre, raise [font=Courier New]ZoomMinimumResponse[/font] to [font=Courier New]0.20[/font]. If the camera dips through floors, raise [font=Courier New]FloorSafetyOffset[/font] to [font=Courier New]1.50[/font].

[line]

[size=4][b]Controls[/b][/size]

Camera pitch works in both RPG and tactical modes. The Camera + WASD edition also supports WASD character movement in both. Point-and-click mode is not a target of this mod.

[b]Gamepad[/b]
[list]
[*][b]Right stick left/right[/b] — horizontal rotation
[*][b]Right stick up/down[/b] — camera pitch
[*][b]Hold L3 + right stick up/down[/b] — zoom, without switching mode on release
[*][b]Short L3 click[/b] — normal RPG / point-and-click toggle
[/list]

[b]Trackpad[/b]
[list]
[*][b]Two-finger swipe[/b] — rotate (horizontal) and pitch (vertical); a diagonal locks to one axis for the gesture
[*][b]Lift fingers[/b] — motion stops, momentum discarded, no drift
[/list]

[b]Mouse[/b]
[list]
[*][b]Hold middle button + drag[/b] — horizontal and vertical camera look
[*][b]Scroll wheel[/b] — zoom, one step per notch
[/list]

[b]Keyboard[/b]
[list]
[*][b]Q / E[/b] — rotate at BG3's original keyboard speed
[*][b]PageUp / PageDown[/b] — the mod's vertical axis (pitch / zoom)
[*][b]W A S D[/b] and the arrow keys — character move / camera pan, [b]Camera + WASD flavor only[/b]
[/list]

The launcher writes the bindings BG3 ships unbound (the [font=Courier New]wheel_x[/font] rotation tokens, [font=Courier New]q[/font]/[font=Courier New]e[/font], [font=Courier New]pageup[/font]/[font=Courier New]pagedown[/font], and in the WASD flavor [font=Courier New]w a s d[/font] plus arrow-key camera pan) into your own profile, backing up the original first.

[line]

[size=4][b]Is this safe?[/b][/size]

[b]The game executable, its app bundle and your saves are never modified.[/b] The launcher does write its own config and logs under [font=Courier New]~/Documents/BG3 Camera Unlock/[/font], and edits your input-binding profile — copying the original to a [font=Courier New]…-backup[/font] file first.

The bundled library scans a fixed set of required ARM64 code signatures in the running process — [b]14[/b] for Camera Only, [b]15[/b] for Camera + WASD — and requires [b]exactly one match for each[/b]. Hooks are installed transactionally but dormant, then activated atomically only after the camera subsystem is validated. No address is ever hardcoded. The camera changes are writes into the game's process memory and are gone when BG3 exits.

[b]If a game update moves anything, the injected module fails closed:[/b] the camera overrides do not activate and BG3 runs with its original camera. The launcher's configuration and binding changes run before the game starts, so those may already be written (with a backup) even then. These checks can stop the overrides; they do not guarantee every possible incompatibility is caught.

Saves made while the mod is active load normally without it.

[b]Multiplayer:[/b] tested in multiplayer and works as it does in single-player. It changes only your local camera, in your own process memory, and adds no network path.

[line]

[size=4][b]"Is this a virus?" — a fair question[/b][/size]

[b]Here is how to check rather than take my word for it.[/b]

macOS will warn you on first launch. That warning is [b]not a scan result[/b] — Gatekeeper shows it for every app that has not been notarized by Apple, which needs a paid developer account this free mod does not have. It means Apple has not checked this build; it does not mean anything was found, and its absence would not prove an app safe.

This mod also does three things that resemble how a malicious injector works: it launches another program, injects a library into it, and rewrites instructions in that process's memory. There is no other way to change the camera in a game with no mod API. So instead of a description:

[list]
[*][b]What it links.[/b] The injected library links [font=Courier New]CoreGraphics[/font] (one call that reads whether a mouse button is held), [font=Courier New]AppKit[/font] (an in-process event monitor for raw trackpad and middle-mouse deltas), and the [font=Courier New]libc++[/font] / [font=Courier New]libSystem[/font] runtimes — no CFNetwork, Security or Network framework in that list. Check with [font=Courier New]otool -L[/font] on the dylib inside the app. This shows the library references no networking API directly; it is not on its own a proof no code path could reach one.
[*][b]No telemetry, no analytics, no accounts.[/b] The mod writes diagnostics to a local log file you can read and delete; nothing is transmitted anywhere.
[*][b]The game executable, app bundle and saves are never modified.[/b] Camera changes live in the game's process memory and are gone when it exits. The mod does edit your own [font=Courier New]inputconfig_p1.json[/font] key-binding profile to add the camera bindings — it copies the original to a [font=Courier New]…-backup[/font] file first and records what it changed.
[*][b]It also writes[/b] its config and logs in [font=Courier New]~/Documents/BG3 Camera Unlock/[/font], a convenience symlink in that folder, a short-lived status file in the temp directory, and one BG3 preference it restores on exit.
[*][b]No admin password, no launch agent, no login item, no background process.[/b]
[*][b]Open source under GPL-3.0-or-later.[/b] The Corresponding Source ZIP is published alongside the downloads on this page. Build it yourself and run your own binary if you prefer.
[*][b]Checksums published[/b] with every release, so you can confirm your download matches what was published (an integrity check, not a safety guarantee).
[/list]

Some antivirus tools flag [i]any[/i] code-injection program on technique alone, regardless of intent. If one flags this, that is what it is reacting to — a reason to run the checks above rather than to guess either way.

Full verification guide, with the exact commands and what each one does and does not prove: see [font=Courier New]SECURITY.md[/font] in the download or on the GitHub repository.

[line]

[size=4][b]Known issues[/b][/size]

This mod points the camera at angles the game was never built for, so some of these are inherent.

[list]
[*][b]Floor collision is not reliable everywhere.[/b] Stairs, slopes, bridges, and stacked interiors may still let the camera dip into the floor.
[*][b]Walls and large props can still clip.[/b]
[*][b]Collision correction can visibly tighten or release zoom[/b] in difficult geometry.
[*][b]Ceilings, roofs, upper floors, and distant room pieces may disappear.[/b] This is BG3's own culling and level streaming, which assumes the original camera range. Higher graphics settings sometimes help; nothing fully fixes it.
[*][b]Other native camera or input mods will likely conflict.[/b] Do not run two mods that hook the same functions.
[*][b]A game update may affect compatibility.[/b] Whether it does depends on what the update changes. If the injected module's checks do not pass, the camera overrides simply do not activate and the game runs with its original camera. Report it (see below) and I'll look into it.
[/list]

[line]

[size=4][b]Reporting a bug[/b][/size]

Please include:

[list=1]
[*]Game build, macOS version, and Mac model/chip
[*]Exact location, and whether you were in RPG, tactical, combat, dialogue, or camp mode
[*]Exact reproduction steps
[*]A screenshot or short video if the issue is visual
[*]Your log: [font=Courier New]~/Documents/BG3 Camera Unlock/mod.log[/font]
[/list]

[b]A useful report looks like:[/b]
[quote]Moonrise Towers, Main Floor, RPG mode. Walk down the north stairs, hold the right stick upward, then rotate right. Camera enters the floor on the third step. Reproduces 3/3 times.[/quote]

Reports saying only "it does not work" cannot be acted on.

For bug reports and compatibility issues, use GitHub Issues or email hello@stealth.vision. Please include your mod version, game version and relevant logs. For a [b]security[/b] issue, email hello@stealth.vision rather than posting details in a public thread.

[line]

[size=4][b]License[/b][/size]

BG3 Camera Unlock is licensed under the [b]GNU General Public License, version 3 or later[/b] (GPL-3.0-or-later), with the additional permission in [font=Courier New]MODDING-EXCEPTION.txt[/font].

You may use, modify and redistribute the project, including commercially, under these terms. A redistribution must preserve the required copyright, license and warranty notices and identify any modifications. A derivative work covered by the GPL must comply with it, including making the Corresponding Source available when you distribute the work in binary form.

The modding exception permits the interoperation described in that file. It grants no rights to third-party game code, assets or trademarks.

The Corresponding Source for this release is the [font=Courier New]BG3-Camera-Unlock-macOS-arm64-<version>-source.zip[/font] published alongside the downloads on this page; one archive builds both editions.

If you reuse this code in another project, a link back and a message to hello@stealth.vision are appreciated. Neither is required by the license, and you do not need the author's approval to exercise its permissions.

[line]

[size=4][b]Credits[/b][/size]

Developed by [b]stealthfd3s[/b]. A native macOS / Apple Silicon camera mod for Baldur's Gate 3. Contact: hello@stealth.vision.

[line]

[size=4][b]Disclaimer[/b][/size]

[b]This is an unofficial, community-made project. It is not affiliated with, authorized by, sponsored by, or endorsed by Larian Studios, Valve Corporation, or Apple Inc.[/b]

Baldur's Gate 3 and all related content are the property of Larian Studios. This project ships [b]no[/b] Baldur's Gate 3 assets — no artwork, icons, audio, models, or data files — and installs into no game folder. It does contain a few dozen short machine-code signatures copied from the game's executable so it can locate the functions it hooks; see the NOTICE file.

No warranty. Use at your own risk.

<!-- ============ END BBCODE — PASTE ABOVE THIS LINE ============ -->

---

## Upload checklist

### Files

| Slot | File |
|---|---|
| **Main File** — Camera Only | `BG3-Camera-Unlock-CameraOnly-macOS-arm64-<version>.dmg` |
| **Main File** — Camera + WASD | `BG3-Camera-Unlock-CameraWASD-macOS-arm64-<version>.dmg` |
| **Optional / Miscellaneous** | `BG3-Camera-Unlock-CameraOnly-macOS-arm64-<version>-nexus.zip` |
| **Optional / Miscellaneous** | `BG3-Camera-Unlock-CameraWASD-macOS-arm64-<version>-nexus.zip` |
| **Miscellaneous** — label `Exact corresponding source` | `BG3-Camera-Unlock-macOS-arm64-<version>-source.zip` |

One source archive covers both flavors — it builds either with a single CMake
switch. Uploading it beside the binaries is how the GPL's Corresponding
Source requirement is met for this distribution. **It is not optional.** Keep
its version label the same as the DMGs.

### Page setup

1. Title: `BG3 Camera Unlock for macOS (Apple Silicon)`
2. Summary: `Native Apple Silicon camera unlock for the macOS Steam build of Baldur's Gate 3: vertical look, a proper zoom chord, and trackpad and mouse camera control, plus experimental collision help. Two flavors — Camera Only and Camera + WASD.`
3. Category: **Utilities** or **Gameplay**.
4. Tags: `macOS`, `Apple Silicon`, `camera`, `controller`, `gamepad`, `trackpad`, `mouse`, `utility`.
5. Paste the BBCode block above into **Description**.
6. Set **Requirements** to note Apple Silicon + Steam + the tested build.
7. Enable comments and the bug tracker.
8. Upload at least one honest in-game screenshot. **Do not use a clipping or
   culling failure as the promotional image.**

### Nexus permission toggles

The project's own code and content are under GPL-3.0-or-later, so set the
toggles to allow reuse on the GPL's terms. Nexus's preset labels do not map
cleanly onto the GPL; where a preset would be more restrictive than the
license, the license still applies to any copy already distributed.

| Nexus option | Setting |
|---|---|
| Users can upload this file to other sites | **Yes** — on the GPL's terms (keep the notices, provide the source) |
| Users can convert this file | **Yes** |
| Users can use assets without permission | **Yes** — on the GPL's terms |
| Asset use in mods that are sold | **Yes** — the GPL permits it; the source and the same rights go with every copy |
| Asset use in mods on paid platforms | **Yes** — same terms |
| Users can modify and release their own version | **Yes** — under GPL-3.0-or-later, with the source |

Do not select "no redistribution" or "no modification" — those contradict
the GPL, which cannot be revoked for copies already distributed. These
toggles cover this project's own material only; they grant no rights in
Baldur's Gate 3 or any third party's assets.

### Before every update

- [ ] `tools/bump-version.sh <version>` — bumps CMake, CHANGELOG, and badge
- [ ] `./scripts/package.sh` — builds both flavors and checksums every artifact
- [ ] `pattern_test` reports all patterns unique for each flavor (14 for Camera
      Only, 15 for Camera + WASD) on the current build
- [ ] `README.md` and this file agree on flavors, features, options, and game build
- [ ] Known Issues updated honestly
- [ ] Archive old files rather than replacing them, so version history stays

### Official Nexus guidance

- <https://help.nexusmods.com/article/28-file-submission-guidelines>
- <https://help.nexusmods.com/article/136-best-practices-for-mod-authors>
- <https://help.nexusmods.com/article/117-why-has-my-mod-been-quarantined>
