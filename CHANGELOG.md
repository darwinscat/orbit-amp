<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->
# Changelog

All notable changes to **OrbitAmp** are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/), and the project uses
[Semantic Versioning](https://semver.org/).

## [0.5.1] — 2026-09-07 — the plugin stops guessing how long it rings

Two things 0.5.0 was not telling the truth about, both found by reading it back the morning after.

### Fixed
- **The declared tail was a flat eight seconds, and eight is wrong in both directions.** A host asks
  how long this keeps sounding after the input goes quiet — for an offline bounce, for a freeze,
  for deciding when it may stop calling us — and it asks about every silence, not only about a
  block standing by. Eight was too SHORT: a HALL at double decay rings for nearly ten, and a
  two-second echo at 95% repeats takes over four minutes to fall to a thousandth of itself, so a
  bounce could end mid-tail. It was also too LONG: with the room and the echo out of the rig there
  is nothing here that rings at all, and every bounce still had eight seconds of silence rendered
  onto the end of it. The two links that have tails are asked what theirs currently is, and only
  while they are in the rig — capped at thirty seconds, because a delay at a hundred per cent
  repeats never decays and no honest number exists for it.
- **The plug-ins in the macOS .zip carried no stapled notarization ticket.** They were notarized —
  they went up with the installer — but only the standalone was submitted on its own and so only
  the standalone came back with a ticket attached, and stapling the .pkg staples the package, not
  what is inside it. Gatekeeper would fetch the verdict over the network on first load; a stapled
  ticket is that same verdict already in the bundle, which is what a machine with no network needs.
  One submission now carries all four, and each is stapled before anything is packed.

## [0.5.0] — 2026-09-07 — a rig you assemble, and a bypass that behaves like an insert

The case this one exists for: a player could switch a block off, but not decide which blocks their
instrument HAS — and every switch in the window meant something slightly different depending on
what it sat next to. There are two switches now, the same two on every link, and the strip is a
picture of the rig rather than a list of things that happen to be here.

### Added
- **Three states on every link — ON, STANDBY, OFF.** Two ordinary automatable parameters,
  `*_present` and `*_on`, so a rig travels in the preset, the session, the A/B/C/D registers and
  undo with no code about storage. The arrow switches standby and on; Setup's EDITOR page decides
  what the rig has at all. Two switches rather than one three-position control because the two
  actions are not the same weight: bypassing is something players automate, removing is not, and on
  one lane aiming at the middle of three and landing on the end takes a block out of the rig when
  you meant to step it aside.
- **Standby is an insert's bypass, as a DAW means it.** A link stops taking new signal but keeps
  running, so a delay's repeats and a reverb's tail ring OUT instead of being chopped. Links that
  replace the signal — boost, preamp, cabinet — crossfade to what they were handed over 15 ms.
  Nothing is cut any more: switching, letting the safety go, and leaving the rig all fade.
- **Setup is a window with three pages** — LIBRARY, EDITOR, VIEW — opened by the gear, which used
  to drop a menu carrying a door to Setup, two doors to pages nobody sets anything on, and four
  switches. Each page says whether it travels with the preset or stays on this machine.
- **The chain strip is one grammar.** The end caps became rows like any other, so IN and OUT answer
  to the same two switches as everything else, and the gate and the limiter have consoles on their
  own arrows instead of riding somebody else's rail.
- **DEVICES & TRADEMARKS moved to the footer**, beside the build stamp: the two pages that are only
  ever read, together, reached from what they are about.
- **`KEEP WINDOW HEIGHT`, `STANDBY KEEPS ITS PLACE`, `EQ HANDS OVER THE CURVE`** — an emptied row
  can hand its height to whoever is left rather than shrink the window; a link standing by can keep
  its place and go dark; the EQ console's row of hands can sit over its curve. All three are about
  the eye, so all three stay on the machine and never enter a preset.

### Changed
- **The power amp is gone from the code.** It had no button — no arrow, no tile, no tab — because
  no pack ever shipped for it, and a block a player cannot see is a block that is not in the
  instrument. The history has it.
- **IN and OUT start out of the rig.** The volumes that matter are the captured blocks' own IN
  trims; a global fader is what a player reaches for when a rig needs fixing.
- **The gate's mute position is named for the chain** — START and END. "Pre-Reverb" was a strange
  thing to read on a rig that may have no reverb in it. The position itself has not moved: END
  falls after the preamp and its console, so the hiss those two add dies with it and whatever the
  delay and the room are holding rings out underneath.
- **The footer stands at the house floor of 13 px** and grew four pixels to hold it. The four facts
  on that strip — the build, what is installed, the host's rate, the cost — are read as sentences,
  and they were set at 12, with the cost breakdown behind them at 11 and its heading at 10.
- **A device's tone points are markers, not handles.** They say where each measured knob acts
  hardest; the knob below is the hand. Their drag converted decibels into knob travel through a
  single measured swing, which is honest as an indicator and was not as a control.
- **A bypassed link keeps its latency**, deliberately, because that is what a bypassed insert does:
  a rate-matching model reports a delay the host compensates for, so the bypass path carries the
  same delay rather than letting the chain arrive early.
- **What standing by COSTS depends on the kind of link, and the breakdown says which.** A link that
  adds — the delay, the room — keeps running, because that is what lets its tail finish, so it goes
  on costing what it costs. A link that replaces stops once its fifteen milliseconds are over: the
  model is not asked, and a preamp standing by is very nearly free. Both keep their row and both
  read their real number, faintly.
- **The cost list is the rig.** A link out of it has no row, because it is not in the chain.

### Fixed
- **The plugin declared no tail** while a room and an echo now ring on purpose — a host told there
  is no tail may cut an offline render exactly where we started holding one.
- **The cabinet kept its convolution across standby**, so the first IR-length after it came back
  convolved what was played before it stood down.
- **A room out of the rig cleared itself every block** — about 150 KB, some 120 MB/s at 64 samples,
  untimed and invisible because its row was not in the list.
- **The OUT cost row carried the limiter's time as well as its own**, so the breakdown did not add
  up — which is the one thing a breakdown is for.
- **Fifteen milliseconds means fifteen milliseconds.** The fade interpolated across the whole block,
  so at 4096 samples it took eighty-five.
- **A pack naming a slot this instrument does not have is offered nowhere.** Saying nothing and
  saying something we do not have had been given one answer, so a pack calling itself a power amp
  turned up in the boost list.
- **A device and its switches were remembered by NUMBER.** A device's parameter is an index into
  the scanned packs, and the scan sorts bundled-first and then along the gain ramp — so dropping one
  new pack into the folder renumbered every device after it, and every session and preset that named
  one by number named a different one. Its switches had the same disease a floor down: a fraction is
  an index into the pack's own position list, so a device repacked one position shorter reopened
  every old session on a different position. Both now carry the NAME beside the number, and the name
  decides on the way back — the device first, because the positions belong to its pack.
- **The names were written at save time, into the live tree.** That edited the state behind the
  player: the settle timer saw a change nobody had made and committed an undo step for it, so the
  next Cmd-Z moved a switch on the device. And saving a preset never goes through the host at all,
  so a preset carried either no names or the ones left from the last project save. They are written
  when a switch MOVES now, which leaves nothing to fix at save time.
- **A settings page could not see a switch move.** It read every row's state when it painted and
  nothing asked it to paint, so automation, an undo, a preset or a second editor left the toggle
  showing yesterday's answer.
- **Setup's LIBRARY page said nothing about its own lifetime** while both of its neighbours did.
  It is the one page with two answers — the folders are this machine's, the switch on it rides in
  the preset — and saying nothing read as a page nobody had thought about.
- **Two editors shared one LEARN.** The compare history had a single after-apply hook, and a host
  may open two windows on the same plugin — so whichever closed first silently disarmed the other's
  measurement. The gate console watches its own three parameters instead, which catches more than
  the hook did: a register recall, a preset, an undo, a hand on the switch, host automation.
- **Hiding a column no longer destroys its trim.** It used to write the value to zero so an unseen
  hand could not keep pressing; the chain simply does not apply a volume that is standing by, so the
  number stays where the player left it.
- Two menus outlived their windows; a column going dark under a held mouse left an automation
  gesture open; the drag rulers had lost their parent; Setup's sub-tabs could not be clicked and its
  small print was under the type floor.

## [0.4.0] — 2026-09-06 — a pack states how hard it is fed and how loud it leaves

The case this release exists for: a boost was too hot for the preamp it fed, and the only thing to
fix it with was the block's IN hand — which is a DRIVE control, so correcting a volume changed a
tone. A pack now carries its own two levels and the player applies both, so balancing one device
against the next costs nothing in character.

### Added
- **The pack's own levels** — `chain[].input_db` and `chain[].output_db` (namz schema 4), applied by
  felitronics::rigplayer with no switch anywhere. Nothing in this plugin implements them: the pin
  brought them, and what this release adds is the honesty around them.
- **The IN wall shows both sides of that level.** The bar fills to what the network is actually
  eating; a thin line marks what entered the block. Between the two stand the pack's `input_db`, the
  chain trim past the ends of the captured range, the active slot's alias trim, and an uncaptured
  device's drive — on a linked notch usually six decibels of daylight between the number drawn and
  the number meant. At 0 the two marks coincide, which is the honest picture of a pack that states
  no level. Between two DIFFERENT captures there is no single "network input" at all, and the panel
  says so rather than averaging two into a number true of neither.
- **The model's loudness tag, under every captured block** — the number when it carries one, and a
  warning in the hand's own colour when it does not, because such a model plays raw, some 8–10 dB
  from its neighbours, and nothing else on the face would report it.

### Fixed
- **The loudness tag named the wrong model.** It read slot 0 always, and slots go by knot parity: on
  an odd capture the whole sound comes out of slot 1, so the face named a silent neighbour's tag —
  or called a tagged model untagged. It now names what is sounding, and says "of two" mid-crossfade.
- **A silent block could draw a level.** The meter tap is clamped at −90 in the audio thread and the
  offset was added after that clamp, so a positive one lifted silence onto the scale and drew a
  sliver of fill and a peak-hold line on a block that was not playing.
- **Text that had become false.** `captureHotDb` said there was nothing in the pack to read about
  level; `blockIn` implied the only loudness fix between blocks is a drive change; `Pack Level Comp`
  claimed to be about the pack's level story when it bypasses the ALIAS trims and nothing else.

### Changed
- felitronics-core **v0.29.0**, namz **v4.1.0**. Normalizing by a model's loudness tag is on by
  default in the player and there is no switch here — it is a contract, not a listener's option.

## [0.3.0] — 2026-09-03 — the papers, and the devices they name

The plugin learns to say what it is: which build is running, what it was built against, whose
equipment it names and on what terms. It also learns the rig format's new shape — a knob that
clicks can carry bands — and ships with an empty factory, so what a player hears is what a player
put there.

### Added
- **The rig format's clicking knob** (namz v4.0.0, felitronics-core v0.25.0). A switch position now
  states the filter it IS, with no law of travel, because a switch has no rotation for a gain to
  ride. Schema 4 is a breaking read, so the pins move before the capture app writes it: a session
  saves a switch by NAME rather than by its place in the list, a switch is judged by the absence of
  rotation rather than by letters in its legends, and a switch that carries bands counts as a tone
  stack the face may offer.
- **The engine the build actually compiled.** The local NeuralAmpModelerCore copy is a shortcut, and
  a shortcut that changes what you build is not one: it is taken only when it sits on the commit
  felitronics-core pins, and a build directory holding a stale cached pin heals itself instead of
  quietly compiling the engine we replaced. The version window names what was compiled, from the
  same directory the compiler read.
- **An empty factory.** This release ships no packs at all: the device list says so, and every
  device a player drops into the Devices folder is theirs and named as such.
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
- **DEVICES & TRADEMARKS**, from the gear: the full third-party hardware notice above, and below it
  every device the installed packs name — maker, model, slot, points on the gain dial, year, serial
  and where it was built — with what shipped in the installer kept apart from what a player added.
  Each half scrolls on its own, and the list is re-read on every open.
- **One window for the product's papers.** The gear's ABOUT and a click on the footer's stamp open
  the same panel: the build stamp, the dependency rows, the licence, the opt-in update check, the
  tip jar — and now the trademark notice, carried at its foot from `resources/notice.txt`. The
  plugin's own About overlay is gone; there is one place a player looks and one text to keep right.
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
