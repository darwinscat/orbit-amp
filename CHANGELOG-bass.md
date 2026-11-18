<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# Changelog

All notable changes to **Darwin's Cat Bass**, the bass amps released together from this repository under one
version of their own (installer/bass.ini; the guitar amps keep theirs in CHANGELOG.md), are documented here. The
format follows [Keep a Changelog](https://keepachangelog.com/), and the project uses
[Semantic Versioning](https://semver.org/).

## [0.3.0] — 2026-11-17 — DeepGrooveBass, the bathyscaphe, and BLEND's low end in phase

### Every amp

#### Added
- **Bypass, the host's.** The whole amp out and the bass or guitar through as it came in, crossfaded, held as late as the
  amp plays so nothing jumps, and the tuner still listening. It is the host's bypass, and no preset, A/B/C/D register or
  undo step carries it: a preset never switches the amp off. DeepGrooveBass's lever has it as its top place, PARK.
- **The standalone lists the interface's inputs by number, each with its meter.** A right click on the INPUT column (or
  the settings menu's INPUT, IN) shows IN 1 to IN n of the interface, and the one the amp plays is lit. A plucked
  string moves the meter of the input it is plugged into, so the guitar plays from IN 4 without repatching and without
  ticking inputs in Audio/MIDI Settings. A click plays that input. A Shift-click adds a second, and the amp plays the
  two together at half each, which combs a DI with a mic. In STEREO a click sets the left amp's input and a Shift-click
  the right one's. The menu stays open while you pluck. The choice is kept for each interface by its name, apart for
  STEREO, and comes back with the interface. An interface seen for the first time keeps playing what it played, and
  every one of its inputs opens for the list. Inside a host the menu keeps Left, Right and L+R, which the host's
  routing feeds.
- **Which input the amp plays: Left, Right or L+R.** With a DI in one input of the interface and a cabinet's mic in
  the other, the guitar plays from either without moving a cable. L+R plays the two at half each (a DI and a mic
  together comb). The choice is in the menu of the INPUT column, and in the settings menu's INPUT; in STEREO both
  inputs play, each through its own amp, as before. It belongs to the session, not to the sound: a session keeps
  it, a preset, the A/B registers and INIT never change it. A new instance plays the left, as every amp did.

#### Fixed
- **BLEND's low end no longer dips.** BLEND's clean leg is now lined up with each capture. Every bass capture carries how
  late it answers its input and whether it answers it inverted, and when a pack loads each capture's clean leg is also
  matched to the way its low end turns, read off the capture itself in the background, a tenth of a second a capture.
  Before, the low end thinned between the clean leg and the capture: at 50/50 on the ReVolt Bass Dirt by up to 16 dB
  around 35 Hz, on the SGT-DI by 2.5 dB at 30 Hz, on the ReVolt Bass Drive by 2 dB. Now BLEND sums its low end within
  half a decibel from 30 to 260 Hz.
- A cabinet IR longer than 1.41 s at 48 kHz, picked while the cabinet is out of the chain, now plays alone from
  the first sample when the cabinet comes back; it used to finish its crossfade from the cabinet before.

#### Dependencies
- **felitronics-appkit v0.25.0**: the input picker the standalones list their inputs with, moved there from the cab
  builders so that the builders and the amps share it.
- **felitronics-guitar-core v0.12.0** (from v0.8.0): the crossover blend's clean match. **namz v5.1.0** with it.
- **cab-ir-generator v0.5.0**, the builders' first release on JUCE 9: the cabinets, their panel and their presets
  are as they were.
- felitronics-core v0.67.0 (from v0.62.0). The cabinet's convolver keeps only the memory its IR needs instead of
  its whole 5 s, and an IR longer than 1.41 s at 48 kHz (0.70 s at 96 kHz) is convolved in smaller steps, which
  plays within float noise of before. The echo plays exactly as before; with REPEATS at 0 the plug-in no longer
  tells the host about echo tail that is not there.
- **JUCE 9.0.5**, up from 8.0.17: every amp, its plug-ins and its app are built on it.

### OrbitBass

#### Added
- **The Two Notes ReVolt Bass.** Its three channels, CLEAN, DIRT and DRIVE, a capture each, join the SGT-DI in the device
  list. OrbitBass still opens on the SGT-DI.

#### Removed
- **INVERT POLARITY in the AMP menu.** BLEND's clean leg now takes each capture's own polarity, so there is nothing left
  to flip. A session saved with it on plays as the capture says.

### RedGrooveBass

#### Added
- **BLEND's crossover on a right click.** Where BLEND's clean lows give way to the capture, 80 to 250 Hz, picked from
  BLEND's own menu, as on DeepGrooveBass.

### DeepGrooveBass

#### Added
- **DeepGrooveBass, the bathyscaphe.** A bass amp in a glass sphere over the floor of an alien ocean, jellyfish drifting
  past and answering the bass, the sea's light moving on the panel. Three gears on the lever — CLEAN, DIRT and DRIVE,
  the three channels of a Two Notes ReVolt Bass, a capture each — and PARK above them: the whole amp out, the sea parked.
  COMP is one fader, its threshold, its squeeze in a column beside it; DRIVE; BLEND on DIRT and DRIVE, keeping the low
  end clean under a crossover set from its menu (80–250 Hz), DIRT opening at 80/20 and DRIVE at 90/10; BASS and TREBLE
  on CLEAN, BASS, MID and TREBLE on the others; OCTAVE, CHORUS and a room; the bass's dozen of drawn cabinets on a
  slider, a cabinet per gear; 5 STR / 4 STR and FINGERS / SLAP; the tuner, GATE and LIMIT, the keys and A/B/C/D.

## [0.2.1] - 2026-11-12 - JUCE's security release

### Every amp

#### Fixed
- **Safer with the files it opens.** Built on JUCE 8.0.17, its security release: a WAV with a broken header no longer
  crashes the amp, a zip can no longer write outside its folder, and a preset or a session nested too deep is refused
  rather than run.
- **CLAP hosts.** A latency that changes while the plug-in starts is reported to the host, a project's transport is
  read right across split blocks, and the plug-in's text reaches the host whole.

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
