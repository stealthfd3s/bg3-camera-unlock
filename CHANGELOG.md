# Changelog

All notable changes to this project are documented here. The format is based
on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project
follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.0.0] - 2026-09-03

First public release.

- Native macOS / Apple Silicon camera unlock for the Steam edition of
  Baldur's Gate 3.
- Vertical camera control (pitch) on the right stick, a trackpad two-finger
  swipe, and a middle-mouse drag, in both RPG and tactical modes.
- Configurable pitch range; per-device horizontal and vertical sensitivity
  for gamepad, trackpad and mouse; an L3 + right-stick zoom chord with a
  scroll-wheel alternative.
- Configurable camera-arm distance limits and experimental wall/floor
  collision assistance.
- Two editions from one codebase: **Camera Only**, and **Camera + WASD**,
  which additionally enables WASD character movement in keyboard UI mode (RPG
  and tactical).
- Plain-text configuration file with validated values; an invalid entry is
  logged and the safe default is kept.
- The launcher injects the mod into the game process at launch and adds the
  camera key bindings to your input profile, keeping a backup. It does not
  modify the game executable, the app bundle, or your saves.
- Tested in-game in single-player and multiplayer, and in RPG and tactical
  modes. See the README for known limitations.

Licensed under GPL-3.0-or-later with the additional permission in
`MODDING-EXCEPTION.txt`.
