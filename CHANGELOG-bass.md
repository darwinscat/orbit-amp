<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# Changelog

All notable changes to **Darwin's Cat Bass**, the bass amps released together from this repository under one
version of their own (installer/bass.ini; the guitar amps keep theirs in CHANGELOG.md), are documented here. The
format follows [Keep a Changelog](https://keepachangelog.com/), and the project uses
[Semantic Versioning](https://semver.org/).

## [0.1.1] - 2026-11-08 - the cabinets' new window, the walker's cabinet switch

### OrbitBass

#### Changed
- **The cabinets' window is Orbit Bass Cab Builder 0.2.0's**: in 16:9, the backs as pictures, the builder's name
  across the response, a mic's distance and angle staying out while the hand goes to them, and a "?" that explains
  what a comb is and why the line turns red. The cabinets themselves sound exactly as before.

### RedGrooveBass

#### Added
- **CABINET's own switch**, in its title row: the cabinet out of the sound, the DI alone, and back. While it is out
  the cabinet's name says OFF, its slider dims, and CAB / DI is held where it stands, there being nothing to mix.

#### Fixed
- **The cabinet's slider runs its slot and no further**: it used to keep the room of a gear that is gone, its groove
  running past the slot. The slot now spans the cabinet's screen, the name centred over it.

## [0.1.0] - 2026-11-06 - the first bass amps: OrbitBass and RedGrooveBass

### Every amp

#### Added
- **Darwin's Cat Bass, a package of its own.** The bass amps come apart from the guitar amps: one installer for
  macOS, one for Windows and an archive for Linux, in which the player picks the amps and the formats. The
  plug-ins are one bundle per format, Darwin's Cat Bass (VST3, CLAP, and AU on a Mac), holding every bass amp;
  each amp is also an app of its own.
- **The amp is a captured Ampeg SGT-DI**, its two voices, B15 and SVT.
- **The cabinets come from Cab IR Generator** and are synthesised as they play: no IR file is installed. A
  dozen of its presets, the most unlike of its punk, rock, metal and modern ones, each named by its number and
  its box, "7: 1x15 THUMP".
- **The data is installed once**, in the same folder as the guitar amps' (/Library/Application Support/Darwin's
  Cat/Factory on a Mac, C:\ProgramData\Darwin's Cat\Factory on Windows, ~/.local/share/darwinscat-amps/Factory
  on Linux): each package adds its own files and takes away only its own.

### OrbitBass

#### Added
- **The bass's whole chain**, the dozen of cabinets on a slider of twelve notches, and **CABINETS**: the dozen,
  ALL of Cab IR Generator's presets and its typical cabinets by style and by size (190), and the player's own.
- **A cabinet of the player's own**: NEW FROM a factory one copies it under a name; EDIT opens it in the
  cabinets' window (the box, its back, its horn, two mics, FIND) and keeps every change in its file; SAVE AS,
  RENAME and DELETE. The player's own IRs too, under MY IRS.

### RedGrooveBass

#### Added
- **Two gears, B15 and SVT**, each a voice of the SGT-DI with a cabinet of its own and its own BLEND, MIDDLE's
  frequency, compressor, OCTAVE, ENVELOPE and CHORUS; 5 STR / 4 STR and FINGERS / SLAP are the player's on both.
