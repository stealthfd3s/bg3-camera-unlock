# Contributing

Thanks for looking. This is a reverse-engineered native mod, so the workflow
is a little different from a typical project — most notably, **you cannot
test it without your own copy of the game**, and **no game file may ever
enter this repository**.

---

## Ground rules

1. **Never commit game content.** No extracted executables, no `.pak`, no
   Larian artwork, no icons taken from the installed game. A pre-commit hook
   and CI both enforce this; do not work around them.
2. **Never commit build output.** `build/`, `dist/`, `.icns`, or any compiled
   binary.
3. **The app icon must stay original.** It is generated from vector
   primitives in `tools/make_app_icon.mm` at build time, specifically so that
   "contains no third-party artwork" is verifiable. Do not replace it with an
   image file, and do not source artwork from the game.
4. **Do not change camera tuning defaults casually.** They were matched by
   hand against the real game. Propose a change as its own commit with a
   reason, not folded into unrelated work.
5. **Do not change the bundle identifier** (`dev.andrii.bg3-camera-unlock`)
   or config key names without a migration.

Install the hook once after cloning:

```sh
ln -sf ../../scripts/check-no-game-content.sh .git/hooks/pre-commit
```

---

## Building

Requires Xcode Command Line Tools and CMake 3.20+. Apple Silicon only.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Build the way CI does, with warnings fatal. The flavor is a build-time
switch; CI builds both:

```sh
# Camera + WASD (default)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBG3_CAMERA_WERROR=ON \
  -DBG3_CAMERA_WITH_WASD=ON
cmake --build build --parallel

# Camera Only — use a separate build directory / CMake cache
cmake -S . -B build-camera-only -DCMAKE_BUILD_TYPE=Release \
  -DBG3_CAMERA_WERROR=ON -DBG3_CAMERA_WITH_WASD=OFF
cmake --build build-camera-only --parallel
```

Full release artifacts for **both** flavors:

```sh
./scripts/package.sh          # KEEP_BUILD=1 keeps the build trees;
                              # BG3_GAME_BINARY=... also runs pattern_test;
                              # FLAVOR=camera-only builds just one
```

Anything that changes user-facing behaviour or the input bindings must be
tested and reasoned about for **both** flavors. The only intended difference
between them is keyboard character movement (`BG3_CAMERA_WITH_WASD`).

---

## Testing

### 1. Offline — signature verification

The single most valuable check, and it needs no running game. Extract the
ARM64 executable from your own installed copy:

```sh
./build/pattern_test /path/to/bg3-arm64
```

**Every required pattern must report exactly one match** (14 for the Camera Only flavor, 15 for Camera + WASD). Zero
matches means the signature broke; more than one means it is ambiguous and
is no longer safe to hook. Both are release blockers.

The path you pass is yours and stays on your machine. `.gitignore` and the
pre-commit hook are set up to stop that binary being committed by accident.

### 2. Offline — config parser

```sh
ctest --test-dir build --output-on-failure
```

Covers defaults, range clamping, malformed values, contradictory pairs,
forward-compatible `ConfigVersion`, and malformed `ConfigVersion`.

### 3. In-game

There is no automated coverage past this point. Build, then launch the game
through the built app:

```sh
# the bundle name carries the flavor: "(Camera + WASD)" or "(Camera Only)"
open "build/BG3 Camera Unlock (Camera + WASD).app"
```

Watch `~/Documents/BG3 Camera Unlock/mod.log`. You want an `[ACTIVE]`
line. An `[ABORT]` line means a fail-closed check fired — the camera
overrides did not activate and the game is running unmodified.

Set `VerboseLogging=true` while investigating.

**Manual checklist before any release:**

- [ ] Right stick pitches the camera in **RPG mode**, full configured range
- [ ] Right stick pitches the camera in **tactical mode**, full configured
      range
- [ ] Horizontal rotation still feels like the stock game at `1.0`
- [ ] L3 + right stick zooms; releasing L3 does **not** switch mode
- [ ] A short L3 click **does** toggle RPG / point-and-click
- [ ] **Camera + WASD:** W/A/S/D move the character in both RPG and tactical
      modes; arrow keys pan the camera
- [ ] **Camera Only:** no WASD character movement; stock WASD camera pan is
      unchanged
- [ ] Switching between RPG and tactical mode does not leave the camera
      stuck or the pitch reset
- [ ] Pitch is preserved across a dialogue and back
- [ ] Camp, combat, and cutscene transitions do not leave the camera stuck
- [ ] Floor protection holds on stairs and in a stacked interior
- [ ] Quitting the game leaves no residue; relaunching via Steam is vanilla
- [ ] A deliberately corrupt `config.ini` logs errors and still launches

Report which locations you tested. "Works for me" is not useful on its own.

---

## After a game update

A BG3 update usually moves the signatures. The mod fails closed, so nothing
breaks — it simply stops applying.

1. Extract the new ARM64 binary.
2. Run `pattern_test` against it and note which patterns fail.
3. Disassemble around the old site and rebuild the signature in
   `src/Patterns.hpp`.
4. Prefer **wildcarding operand fields** over shortening a signature.
   `MakeInstructionMask` exists for this: BL/B targets and ADRP page offsets
   get rewritten by the linker on every build, while the opcodes stay pinned.
   A signature that wildcards those survives a rebuild that changed nothing
   meaningful; a shortened one becomes ambiguous and unsafe.
5. Re-run `pattern_test` until every pattern is unique again.
6. Update the tested build number in `README.md`, `docs/NEXUS.md`, and the
   badge.

`tools/lldb_toggle_probe.py` helps trace input events live if you need to
re-derive an action ID.

---

## Code style

- **C++20**, 4-space indent, 80-column limit. `.clang-format` and
  `.editorconfig` are authoritative — run `clang-format -i` on files you
  touch.
- **Warnings are errors in CI.** Build with `-DBG3_CAMERA_WERROR=ON` before
  submitting.
- Anonymous namespace for internal linkage; nothing global that need not be.
- `constexpr` for every magic number, named, at the top of the file.
- Early returns over nesting.

### Comments explain *why*, never *what*

This is the rule that matters most here. A reverse-engineered constant is
meaningless without the reason it holds. Compare:

```cpp
// Bad — restates the code.
constexpr std::size_t kCameraRotationSpeedOffset = 0xc0;  // offset 0xc0

// Good — says why this offset and not the neighbouring one.
// UpdateGameCameraBehavior's ARM64 yaw path loads the angular speed from
// GameCameraBehavior + 0xc0:
//
//   ldr   s1, [x26, #0xc0]
//   fmadd s8, s0, s1, s2
//
// +0xc4 is the changing horizontal heading, not a speed. Reading it made
// the pitch rate depend on the direction the camera happened to face.
constexpr std::size_t kCameraRotationSpeedOffset = 0xc0;
```

Include the disassembly you derived it from. The next person after a game
update is probably you, a year later.

### Fail closed, always

Any new hook must verify its assumption and **disable itself** if the check
fails. Never write to an address that has not been validated in the running
process. A mod that quietly does nothing is fine; one that writes somewhere
unknown is not.

---

## Documentation

**`README.md` and `docs/NEXUS.md` must be updated together.** They describe
the same mod to two audiences and drift apart immediately if edited
separately — that is exactly how this project ended up shipping a stale game
build number on its Nexus page.

Any change to features, options, requirements, install steps, known issues,
or permissions means editing **both**, plus `docs/CONFIG.md` if the config
surface changed. A PR touching one and not the other will be asked to fix it.

`docs/NEXUS.md` is **BBCode**, not Markdown — it is pasted directly into
Nexus's description field.

---

## Commits and pull requests

[Conventional Commits](https://www.conventionalcommits.org/):

```
feat:     a new user-visible capability
fix:      a bug fix
perf:     a performance change
refactor: no behaviour change
docs:     documentation only
build:    build system, packaging, CI
chore:    everything else
```

Example:

```
fix: keep pitch stable across dialogue transitions

The camera behaviour object is swapped during dialogue, so the cached
pointer went stale and the integrator restarted from zero on return.
Key the cache on the object seen this update instead.

Verified in Act 1 (Druid Grove, Goblin Camp) across 20 transitions.
```

Keep commits small and reviewable. Work on a branch, never on `main`.

**In your PR, state:**

- what you changed and why
- the game build you tested against
- your macOS version and Mac model
- `pattern_test` output (every pattern unique, for the flavor you built)
- which manual checklist items you exercised, and where

---

## Reporting bugs

Use the GitHub issue templates, or email
[hello@stealth.vision](mailto:hello@stealth.vision) for bug reports and
compatibility issues. A useful report includes the mod version, game build,
macOS version, Mac chip, exact location, camera mode (RPG or tactical),
reproduction steps, and `mod.log`. A report saying only "camera clipping"
cannot be acted on.

For a security issue, email
[hello@stealth.vision](mailto:hello@stealth.vision) rather than opening a
public issue — see [SECURITY.md](SECURITY.md).

---

## License

Contributions offered for inclusion in the project are accepted under
**GPL-3.0-or-later** with the additional permission in
`MODDING-EXCEPTION.txt` — the same terms as the rest of the project.

- You keep the copyright in your contribution.
- You need the right to provide it under these terms.
- If your contribution includes third-party code, or material under other
  license terms, say so in the pull request before it is merged.

There is no CLA and no transfer or assignment of rights. Opening a pull
request is not, by itself, a grant of any rights in third-party code it may
reference.
