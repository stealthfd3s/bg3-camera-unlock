# Media placeholders

The README and the Nexus page reference these files by exact name. Until
they exist, the README's hero image renders as a broken image on GitHub.

Capture these yourself from your own game — do not source them from
promotional material or from other people's uploads.

| Filename | Where it is used | What it should show | Spec |
|---|---|---|---|
| `hero.png` | README banner, Nexus header | The unlocked camera at high pitch, in an attractive location. This is the first thing anyone sees. | 1600×900, PNG |
| `pitch-comparison.png` | README "What it does" | Side-by-side: stock camera angle vs unlocked. Label both halves. | 1600×900, PNG |
| `zoom-close.png` | Nexus gallery | Camera at `MinimumZoomDistance=0.5` — a character's face. | 1600×900, PNG |
| `zoom-out.png` | Nexus gallery | Camera pulled back to show the wider range. | 1600×900, PNG |
| `pitch-demo.gif` | Nexus gallery | 5–8 s loop of the right stick sweeping the full pitch range. | 800×450, under 5 MB |
| `config-example.png` | docs/CONFIG.md | `config.ini` open in TextEdit. | 1200×800, PNG |

## Guidance

- **Do not use a clipping or culling failure as the main image.** Pick scenes
  that show the feature working.
- Shoot at a consistent resolution and graphics preset.
- Keep the HUD visible so the shots read as real gameplay.
- Compress PNGs before committing; Nexus and GitHub both dislike large files.
- Check `scripts/check-no-game-content.sh` still passes — the 2 MB per-file
  ceiling applies to screenshots too. Compress, or host large media on the
  Nexus page rather than in git.

## Also needed

| Filename | Where | What it should show | Spec |
|---|---|---|---|
| `gatekeeper.png` | README install section, Nexus | **System Settings → Privacy & Security** with the *Open Anyway* button visible next to "BG3 Camera Unlock". Crop to the Security section — no need for the whole window. | 1200×600, PNG |

This one prevents more support comments than any other screenshot. Most
people have never used that panel and will not find it from a text
description alone.
