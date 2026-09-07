# orbit-amp — design notes

> ## ⚠️ THIS IS A SOFT PLAN — read this first
>
> Everything below is a **snapshot of current thinking — not a contract, not a
> ratified spec.** Every decision here is provisional and *expected* to change.
>
> - New ideas and direction changes **override** this document.
> - This file must **never** be used to reject a change ("but the plan says…").
> - If reality and this plan disagree, **reality wins** — update the plan, don't
>   defend it.
>
> Treat it as a starting sketch to build from, and keep editing it freely.

---

## What orbit-amp is

A guitar tone plugin (VST3 / AU / CLAP / standalone; JUCE; macOS, Windows, Linux). It plays
captured neural voicings through a full chain — boost and preamp, each with its own EQ, then
delay, reverb and cabinet — on a compact, resizable faceplate. AGPL-3.0-or-later.

## Philosophy

- **Capture the voice, rebuild the rest.** A neural profile captures the static,
  nonlinear character of a preamp / amp / pedal. Everything linear or time-based —
  EQ, loudness, reverb — is rebuilt as honest DSP; the cabinet is an impulse
  response. We don't fake the nonlinearity, and we don't pretend DSP is a capture.
- **Recombine for originality.** A boost from one device + a preamp voicing from
  another = a new instrument. Curation and recombination, not clones of famous gear.
- **Curated taste over a catalogue.** A small set of voicings worth having rather
  than a museum of "legendary" heads.

## The unit is a voicing, not a channel

A hardware amp has "channels" because sharing one chassis / power section / cabinet
is cheaper than owning three amps. In software that reason disappears. Here the
addressable unit is a **voicing module**; one voicing plays at a time. Each block
offers one flat list of devices, ordered by character — a ramp from clean to
modern, green to red. "How many channels" a device has = how many voicings it
curates — one for a single, several for a set.

A subtle but load-bearing point: what gets captured already **isn't** the amp's
channel. The tone stack is pulled out to DSP; per-take level calibration flattens
the gain→loudness ramp; the gain knob becomes a set of discrete captured profiles.
The capture holds the nonlinear *voice*; loudness, EQ and reverb are our own
layers on top.

## The rig, and the two switches every link has

The chain is laid flat across the top of the window as a strip of arrows — one per link, in order,
the shape itself saying the sequence. **The strip is a picture of the RIG as a player sees it, not
a diagram of the pipeline**: its two ends, IN and OUT, are the way in and the way out rather than
links, which is why `… LIMIT · OUT` is honest even though the output volume is applied before the
limiter — a master that could push past the ceiling would not be a ceiling.

Every link has **three states**, and both switches are ordinary automatable parameters, so a rig
travels in the preset, the session, the A/B/C/D registers and undo without a line of code about
storage:

| | what you see | what it does |
|---|---|---|
| **ON** | standing, working | sounds |
| **STANDBY** | in the strip, arrow dark; on the panel either gone or dimmed, your choice | stops being FED — what it was already holding rings out |
| **OFF** | no row in the strip, nothing on the panel | not in the chain: nothing of it is applied, and it has no row in the cost list |

The arrow switches STANDBY ↔ ON. Setup's EDITOR page decides what the rig HAS at all.

**STANDBY is an insert's bypass, exactly as a DAW means it.** A link stops taking new signal but
keeps running, so a delay's repeats and a reverb's tail ring OUT instead of being chopped; a link
that *replaces* the signal rather than adding to it — boost, preamp, cabinet — crossfades to the
signal it was handed over fifteen milliseconds. Nothing is ever cut: switching, and leaving the rig,
both fade.

Two consequences worth stating out loud, because both are the model working rather than failing:

- **what a link standing by costs depends on what kind of link it is**, and the breakdown says which
  rather than making you guess. A link that ADDS — the delay, the room — keeps running, because
  that is what lets its tail finish, so it keeps costing what it costs. A link that REPLACES stops
  once its fifteen milliseconds are over: the model is not asked, and a preamp standing by is very
  nearly free. Both keep their ROW, and the row carries the real number, faintly — which is the
  useful truth in either direction. Taking a link out of the rig is what removes the row;
- **a bypassed link keeps whatever latency it had.** A rate-matching model reports one, and dropping
  it out of the path would make the chain arrive early for as long as it stayed bypassed, so the
  bypass path carries the same delay the model would have.

## Signal chain

```
IN → tuner → gate → boost → EQ → preamp (voicing) → EQ → delay → reverb → cabinet → limiter → OUT
```

- **IN / OUT** — the input's level and its volume, the output's mirrored: one rail each, one hand
  each. **Out of the rig by default**: the volumes that matter are the captured blocks' own IN trims
  — how hard each model is fed — and a global fader is what a player reaches for when a rig needs
  fixing, not what a rig needs to start with.
- **Tuner** — a listener: it taps the raw input, ahead of the gate, because a closed gate would
  blind the needle on a decaying note, which is exactly when you tune. MPM (McLeod) pitch. It is a
  link like any other — out of the rig it does not even listen — and it will grow a mute, at which
  point it stops being only a listener.
- **Noise gate** — `felitronics::dynamics::NoiseGate`, the engine OrbitCab ships:
  Schmitt + hold, transient-safe open, pop-free enable. Dual detection: it always
  KEYS off the raw guitar, and the MUTE lands where the player says — at the START
  of the chain, or at its END, which is the default: it falls after the preamp and
  its console, so the hiss the boost and the preamp ADD dies too, the clean key
  never pumps, and whatever the delay and the room are still holding rings out
  under it. Named for the chain rather than for one block, because a rig may have
  no reverb in it — it used to read "pre-reverb", which was a strange thing to see
  on a rig that has no reverb to be before.

  Its console is its own arrow in the strip: OFF, three named thresholds, LEARN (three seconds of
  listening that sets one for you), DECAY and where it mutes. It used to ride the IN column as a
  second runner on the same rail as the input volume, and a drag moved whichever runner was nearer
  the grab — two hands on one scale, told apart by proximity, which is not something anyone can aim.
  The menu's header carries the current number, because the threshold is continuous and three
  positions are three points on it. Off by default.
- **EQ** — DSP, and **part of the captured block, not a separate link**: each
  console sits right AFTER its block's nonlinearity, colouring what the device
  made — the boost's EQ feeds the preamp, and the preamp's sits where a real amplifier keeps its
  tone stack. It goes dark with its block. Its row of hands sits under the curve or over it, a
  choice in Setup. Two faces
  per console: DEVICE TONE — the pack's own measured knobs, and the one a block
  wears until you say otherwise — and UNIVERSAL EQ, our parametric in their place:
  two shelves with free corners, two tone bells (a third narrow one switches in),
  HPF/LPF with a real slope choice (6–48 dB/oct). A pack that measured nothing
  falls to UNIVERSAL on its own. The device's own knobs come first because a
  captured device that does not answer to its own controls is not the device.

  Wearing the device's own tone, its points on the curve are **markers, not handles**: they say
  where each measured knob acts hardest, and the knob below is the hand. They used to drag, and the
  drag converted decibels into knob travel through a single measured swing — honest as an indicator,
  not as a control, because a tone stack's response is not linear in its dial.
- **Boost** — a separate captured (neural) block in front.
- **Preamp** — the captured voicing. Gain 0–10 maps to the captured positions —
  SMOOTH crossfades between them, STEP lands the dial on them; the biggest knob
  (the hero). The tone console lives in the block — see the EQ entry above.
- **Delay** — the echo before the space: repeats of what the preamp made, free or
  host-clocked (divisions, with a BPM of its own when nothing conducts), a darkness
  on the repeats, a stereo offset that widens the back half — and the reverb then
  rooms what the echo made.
- **Reverb** — DSP (algorithmic). The character is the title — Ambience · Room ·
  Hall · Plate · Spring · Modulated — size and damping follow from it; Mix is the
  hero, DECAY scales the character's breath, PREDELAY keeps the attack dry, and the
  tail's own HPF is always in (a low tail into what follows is mud).
- **Cabinet** — one impulse response, drawn as its waveform; HPF / LPF / trim /
  phase are baked into the IR itself, so the convolution stays one clean pass.
- **Limiter** — the safety at the door on the way out: on by default, because protection you must
  remember to switch on protects nobody. Its console is its own arrow in the strip — OFF and three
  ceilings — and, like the gate's, its header carries the current number, because the ceiling is
  continuous and three positions are three points on it. The word on that item is OFF, and it
  means STANDBY in the table above: a guard has no tile to dim, so its arrow going dark IS the
  whole of it, and "off" is what a player calls a limiter that is not limiting. Taking it out of
  the rig — the row itself — is Setup's business, as it is for every other link. Letting it go is a crossfade, not a
  release: three decibels of grip handed back between two samples is a step, and a step is a click.

**Stereo, three ways**: MONO; STEREO (everything twice, each side through its own
amp); STEREO SPACE — mono where the sound is MADE, stereo from the delay on,
where the space is: the amp is paid for once, the delay's offset spreads the one
signal into two, and the room then works on a picture that is already wide. Until somebody chooses, the environment decides: the
standalone opens on STEREO SPACE, a plugin on a mono bus on MONO, on a stereo bus
on STEREO.

### Captured vs. rebuilt

| thing | how |
|---|---|
| boost / preamp nonlinearity | neural profile (captured) |
| gain positions | discrete captured profiles, 0–10 |
| tone / EQ | the device's measured knobs, or our parametric in their place |
| loudness-vs-gain | our own curve (capture normalises it away) |
| reverb | DSP |
| cabinet | impulse response (its post baked in) |

## Visual language (faceplate)

Futuristic, not retro — no grilles / tolex / glowing tubes; a dark, "instrument"
feel. Brand tokens: a violet accent (per-device overridable), an orange "spark"
constant for the captured neural core, the cat mark. Knobs are the heroes (value on
the face, 0–10 numeric with notches). The blocks are framed modules, colour-coded:
**orange = captured**, **violet = DSP**. Real device names title the captured blocks;
the Darwin's Cat voice name rides the paper line beneath.

**One layout — no zoom, no modes.** Everything is readable at 1×: the captured blocks with their
consoles in the upper row, what happens afterwards below. The chain strip runs across the top under
the chrome; the volume columns stand at the sides when the rig has them. A top chrome carries
undo / redo · A/B/C/D · presets · the gear; the footer states the facts of the run — stereo mode,
sample rate, DSP cost — and beside them the two pages that are only ever READ: the build stamp, and
DEVICES & TRADEMARKS. The whole editor scales 50–400% from a single factor.

An emptied row does not shrink the window by default: its height is handed to whoever is left, so
nothing under your hands moves while you put a block in and out to hear it. Both that and how a
standing-by link is shown — removed from the panel, or left in place and dark — are the eye's
business, so they live on the machine and never in a preset.

**Nothing a player reads is smaller than 13 px** at 1×. That covers every sentence, every caption
under a control, every row of a popover and every fact on the footer strip. The one exception is
the numerals and short labels that belong to an instrument's own scales — a meter's ladder, a
curve's axis, the names inside a strip miniature — where small type is a diagram rather than
prose, and making it bigger would make the diagram worse.

## Setup — one window, three pages

The gear opens a window, not a menu. It used to drop a popup carrying a door to Setup, two doors to
pages nobody sets anything on, and a handful of switches — which is how a settings menu becomes the
place things are hidden.

| page | what | whose life |
|---|---|---|
| **LIBRARY** | a sub-tab each for the preamp packs, the boost packs and the IRs, and the one switch that is about packs | folders are the machine's; the switch travels with the preset |
| **EDITOR** | which links the rig has at all — the chain in order, each row wearing its link's accent | **travels with the preset** |
| **VIEW** | what this window shows, and only that | **this machine only** |

Each page says which of those two lifetimes it has, quietly, beside its own tabs. Nobody should
have to discover it by watching their patch rearrange somebody else's window.

The pixel-level reference is a set of HTML mockups produced during design (kept
outside this repo). Rebuild the faceplate natively — the mockups are a spec, not
code to port.

## Portability / shared code

Reusable pieces — the pack player, the analysis taps, the shared views — live in
the `felitronics` libraries (a JUCE-free core plus an app-side kit), so orbit-amp
and OrbitCab consume the same code instead of copying it. orbit-amp itself is a
thin plugin shell over those + JUCE.

## Product scope (soft)

- Guitar. **Bass** is a strong, under-served direction and may become its own
  focused plugin (bass wants a parallel dirt blend and a crossover, rarely reverb —
  a different control set).
- Real device names on the face; the curated Darwin's Cat voice names beneath them.
  Recombination over cloning.
- Free / AGPL.
