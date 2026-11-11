<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# Changelog

All notable changes to **Darwin's Cat Bass**, the bass amps released together from this repository under one
version of their own (installer/bass.ini; the guitar amps keep theirs in CHANGELOG.md), are documented here. The
format follows [Keep a Changelog](https://keepachangelog.com/), and the project uses
[Semantic Versioning](https://semver.org/).

## [0.2.0] - 2026-11-11 - OrbitBass's CAB on two sliders, the SGT-DI at new levels

### Every amp

#### Changed
- **The Ampeg SGT-DI at new levels.** The pack is fed 16 dB softer, at -20 dB where it was fed at -4, and leaves 3 dB
  under its capture, where it left at the capture's own level; the captures are the same. A session or a preset plays
  at the new levels.
- **COMP's MIX at three quarters.** RedGrooveBass holds three quarters of the bass through COMP, where it put all of it
  through; a fresh OrbitBass, INIT and a host's reset open its MIX there, where it opened at half. An OrbitBass session
  or preset keeps its own.
- **Two of the dozen with their mics where an engineer puts them up**: 7: 1x15 THUMP's second mic is a D112 close to
  the speaker, where an RE20 stood 20 cm out, and 9: 1x12 SQUAT's mic an RE20 at the cap, where an SM57 stood off it.
  RedGrooveBass's B15 opens on the new 1x15 THUMP; an OrbitBass session or preset saved on one of the two keeps the
  cabinet it saved.

#### Dependencies
- **cab-ir-generator v0.4.0**: Orbit Bass Cab Builder's forty presets in the CAB's BUILDER list, and the two cabinets
  above.
- **felitronics-appkit v0.23.0**: the CAB picture's short line.

### OrbitBass

#### Added
- **IRs dropped on the CAB play.** A file, a folder of them or a zip, dropped on the block, goes into your own IRs,
  `Darwin's Cat/Bass IRs`, comes onto their slider at once, and the first of them plays.
- **Keys to hear one sound against another.** While OrbitBass's window has the keyboard, A, B, C and D, or 1, 2, 3 and
  4, switch the registers as their buttons do (the same keys on a Russian layout), and LEFT and RIGHT step the
  cabinet that plays to the one before or after it. Only without a modifier, and a field being typed in keeps its
  keys.

#### Changed
- **The CAB works as OrbitAmp's.** The dozen stand on their slider under the picture; your own IRs, in
  `Darwin's Cat/Bass IRs`, have a slider of their own on a row above it, there only while the folder holds any. The
  slider whose list plays shows its cap, and a small triangle at each end of a slider steps one back or on, as LEFT
  and RIGHT do on the keyboard for the slider that plays.
- **The name on the border opens the whole list**, where it only said what plays: ON, the dozen, then BUILDER, with
  Orbit Bass Cab Builder's forty presets by style (BLUES, METAL, PUNK and ROCK) and, once you have saved some in the
  builder app, its MY PRESETS; then your own IRs, folders first.
- **The picture** says the IR's length, rate and bits in a short line, "100ms 48/24", and shows the drawn cabinet's
  box at its right, as tall as the picture, with its mics.
- **MID FREQ opens at 1 kHz**, as the walker's: a fresh OrbitBass, INIT and a double click on it put it there, where
  it stood at 400 Hz. A session or a preset keeps its own.

#### Removed
- **CABINETS and the cabinets' window.** No CABINETS menu with ALL, EDIT, SAVE AS, RENAME and DELETE, no MY IRS, and
  no window to set a box and its mics, from the menu or a click on the picture: Orbit Bass Cab Builder makes
  cabinets, and its MY PRESETS are in BUILDER. The cabinets you kept under MY CABINETS stay as files in
  `Darwin's Cat/Bass Cabinets` and are no longer listed; a session or a preset that plays one still plays it, under
  its name.

### RedGrooveBass

#### Changed
- **CABINET's title carries the slot's number**, CABINET 4, and the line under it the name alone, 4x10 SHEEN.

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
