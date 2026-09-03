<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# Changelog

All notable changes to **OrbitAmp** are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/), and the project uses
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- **The build stamp, at the end of the footer.** `V0.2.0 · STANDALONE` in the strip's own voice,
  beside the sample rate and the DSP figure — a fact about the run, like everything else on that
  line. Click it and appkit's version popover steps out: the whole stamp (commit, build number,
  the machine it was built on), what it was built against — felitronics-core, felitronics-appkit
  and JUCE, each naming its tag, whether it came from a sibling checkout or the pin, and its
  commit — the licence, and the links home.
- **namz and NeuralAmpModelerCore in the dependency rows** — the codec the packs are written in and
  the engine that runs them, beside felitronics-core, felitronics-appkit and JUCE. namz's tag is
  matched as `v[0-9]*` on purpose: it carries per-language release tags for its ports, and "the
  nearest tag" would name one of those as the codec version. The engine is a pinned commit of
  somebody else's repository, so its row says where that pin sits between releases — `v0.5.3+11 ·
  pin · gb5a68c3`.
- **The title row leads in two directions.** The product's mark and name open OrbitAmp's page; the
  cat and "by Darwin's Cat" open darwinscat.com. Both carry the plugin's campaign tag, and the tip
  jar carries it too — beside the `from` / `platform` / `format` signature appkit adds for the
  access log (felitronics-appkit v0.13.0).
- **The cat in the popover.** The maker's mark stands beside the product's on the title row, the way
  the window header carries both — the same embedded SVG, not a second drawing of it.
- **An update check that asks first.** The popover's button queries this repository's latest
  release; a switch beside it lets the check run once a day on its own, off until it is ticked.
  Nothing reaches us either way: the request goes to GitHub, and the versions are compared on the
  machine. When a release seen by a check is newer than the running build, an orange dot lights
  beside the stamp and stays lit across sessions until the build catches up.
- **Feed the Cat**, at the foot of the same popover — the suite's one tip jar, reached through
  darwinscat.com's steerable hop, signed `?from=orbitamp&platform=<os>` so the access log can tell
  which app and which machine fed the cat. Neither parameter reaches the payment page.
- **`OrbitAmpVersion.h`**, baked on every build (`cmake/GenerateOrbitAmpVersion.cmake`): git
  describe, short hash, dirty flag, commits past the tag, a 14-digit UTC build number, the build
  OS/arch/builder, and the resolved dependency rows. End users have no repository; the binary
  carries its own stamp.

## [0.2.0] — 2026-09-02 — the box on the page

The device pages learn what a pack actually ships, the two tone stacks stop playing at once, and the
repository gets its first green build.

### Added
- **The BOX.** A pack ships a photograph of the thing it was captured off — namz's `picture`, a WebP
  cut-out in the pack root — and nothing read it. PHOTO is a page of its own beside the five scopes
  and the paper: the box alone, filling the tile.
- **The CARD.** Where the room seats them — a lone block's wide tile, a thrown-open face, the whole
  monitor — the box and its paper become one page, side by side, with the circuit standing between
  them as the divider. The tile keeps two pages, because 106 points cannot hold both.
- **What the paper says.** The MAKER instead of the voice's alias, and on the card the box's own
  history: the year it was built, the serial number stamped on it, where it was designed and made,
  and who made the capture. All from `gear` (namz v3.3.0); a pack that does not say shows nothing.
- **ABOUT**, from the gear menu — the product, its version and licence, the cat at a size worth
  looking at, the link home, and the trademark notice in one canonical wording the README carries
  verbatim. Names of other people's equipment identify the equipment; this page says so.
- **The corner glyphs come with the hand** and fade when it leaves, so a photograph is a photograph.
  The whole-screen view keeps a door in words at the top of the picture menu.
- **The spectrum reads in BANDS** on a picture that owns the face: `MultiResSpectrumPane`
  (felitronics-core v0.22) feeds three window lengths from one frame and reports a twenty-fourth of
  an octave — fifty-six readings between 20 and 100 Hz where a 2048-point window gave three and a
  half. Only there: it costs about fifteen times the classic pane, and the face runs six to eight.

### Changed
- **The two tone stacks are alternatives, and now they behave like it.** Choosing the device's own
  parked ours in name only — our bands went on filtering with no controls on the face to reach them.
  Only the tone sections step aside now; the cut filters and the level belong to the block whichever
  tone is playing. Where a device measured nothing there is no choice, so there is no chooser.
- **Every picture draws what the block does.** The tile was handed the device's measured curve alone
  and drew a flat line under a console showing a mountain. Both call one function now.
- The circuit symbols are drawn PER PART — four diodes are four diodes, which is what the line under
  them says.

### Fixed
- **The Windows gate had never run.** `AmpProcessor` is 1.5 MB and stood on the stack; Windows gives
  a main thread one megabyte where macOS gives eight, so the gate died in `main`'s prologue on every
  CI run this repository has ever had — reported as `127`, the low byte of `STATUS_STACK_OVERFLOW`,
  which reads as "command not found" and points the other way. The processors move to the heap, and
  the gates now say what they found and what they printed.
- The DEVICE page overflowed its own tile, printing its last two lines on top of each other.
- The whole-screen view opened blank on the two pages that carry no audio.

### Build
- libwebp builds for a universal macOS binary (felitronics-appkit v0.11.2): its SIMD probe compiles,
  and a two-architecture compile answered with one voice.
- Pins: felitronics-core v0.23.0, felitronics-appkit v0.11.2, namz v3.3.0.

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
