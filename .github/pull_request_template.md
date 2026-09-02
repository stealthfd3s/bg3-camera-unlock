## What does this change?

<!-- One or two sentences. Link the issue if there is one. -->

## Why?

<!-- The reasoning. For a reverse-engineered constant, include the
     disassembly you derived it from. -->

## Type of change

- [ ] `fix` — bug fix
- [ ] `feat` — new capability
- [ ] `perf` — performance
- [ ] `refactor` — no behaviour change
- [ ] `docs` — documentation only
- [ ] `build` — build system, packaging, CI
- [ ] `chore` — other

---

## Testing

**Game build tested against:**
**macOS version:**
**Mac model and chip:**

### Offline

- [ ] `ctest --test-dir build --output-on-failure` passes
- [ ] `./build/pattern_test /path/to/bg3-arm64` reports **every pattern with
      exactly one match** (14 Camera Only / 15 Camera + WASD)
- [ ] Builds clean with `-DBG3_CAMERA_WERROR=ON` for **both** `-DBG3_CAMERA_WITH_WASD=ON` and `=OFF`

<details>
<summary>pattern_test output</summary>

```
paste here
```

</details>

### In-game

- [ ] `mod.log` shows `[ACTIVE]`
- [ ] Right stick pitches through the full configured range in **RPG mode**
- [ ] Right stick pitches through the full configured range in **tactical
      mode**
- [ ] Horizontal rotation unchanged at `HorizontalSensitivity=1.0`
- [ ] L3 + right stick zooms; releasing L3 does **not** switch mode
- [ ] Short L3 click **does** toggle RPG / point-and-click
- [ ] **Camera + WASD:** WASD moves the character in RPG and tactical modes;
      **Camera Only:** no WASD character movement, stock camera pan intact
- [ ] Switching RPG ⇄ tactical does not leave the camera stuck or reset pitch
- [ ] Pitch preserved across a dialogue and back
- [ ] Quitting leaves no residue; launching via Steam is vanilla

**Locations tested:**

---

## Checklist

- [ ] No game content committed — no extracted binaries, `.pak`, or Larian
      artwork. The pre-commit hook passed.
- [ ] No build output committed
- [ ] Camera tuning defaults unchanged, **or** the change is its own commit
      with a stated reason
- [ ] Config key names unchanged, **or** a migration ships with it
- [ ] Comments on any new constant explain **why** that value, not what it is
- [ ] New hooks verify their assumptions and **fail closed**
- [ ] `README.md` **and** `docs/NEXUS.md` both updated if user-facing
      behaviour changed
- [ ] `docs/CONFIG.md` updated if the config surface changed
- [ ] `CHANGELOG.md` updated under `[Unreleased]`
