<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# Changelog

All notable changes to **OrbitAmp** are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/), and the project uses
[Semantic Versioning](https://semver.org/).

## [0.1.0] — 2026-08-31 — the player leaves the bench

First release. A curated guitar tone player — VST3 / AU / CLAP / Standalone; macOS, Windows, Linux.

### Added
- **The chain**: boost → EQ → preamp → EQ → delay → reverb → power amp → cabinet, on one compact,
  resizable faceplate. The boost, preamp and power amp are captured neural voices; everything
  linear or time-based around them is honest DSP.
- **Captured voices** ride in `.orbitrig` device packs (namz schema 3), played through
  `felitronics::rigplayer`: a continuous gain dial over the captured positions — SMOOTH
  crossfades between them, STEP lands on the knots — and per-block tone controls that follow
  the device's own knobs (Native) or park them and hand the shaping to the player's EQ (OURS).
- **The library**: packs dropped into the user's `Devices` folder appear in the block's list;
  a factory layer inside the bundle sits underneath it. Files in a folder — no ceremony.
- **The cabinet**: a single impulse response with HPF / LPF / trim / phase baked into it,
  drawn as the IR's waveform. 21 cabinet IRs ship in the box.
- **The echo before the space**: a delay ahead of the reverb — repeats of what the preamp
  made, free or host-clocked, with a darkness on the repeats and a stereo offset that widens
  the back half. The reverb speaks six characters — Ambience · Room · Hall · Plate · Spring ·
  Modulated — with DECAY, PREDELAY and an always-in tail HPF as its late refinements.
- **Stereo** three ways: MONO, STEREO (everything twice), STEREO SPACE — mono where the sound
  is made, stereo from the reverb on, where the space is. A fresh instance follows its bus.
- **Around the sound**: input gate, tuner, output limiter (opens on SAFETY), A/B/C/D compare
  slots, scope with IN/OUT metering.
