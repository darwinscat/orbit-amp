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

A guitar tone plugin (VST3 / AU / standalone; JUCE; macOS-first, other platforms
later). It plays captured neural voicings through a full chain — boost and preamp,
each with its own EQ, then reverb, power amp and cabinet — on a compact, resizable
faceplate. AGPL-3.0-or-later.

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

## Signal chain

```
tuner → gate → boost → EQ → preamp (voicing) → EQ → reverb → power amp → cabinet → limiter
```

- **Tuner** — a listener, not a processor: it taps the raw input and never touches
  the signal, so it has no switch. It sits FIRST because that is what it hears —
  and it must stay ahead of the gate, or a closed gate blinds the needle on a
  decaying note, which is exactly when you tune. MPM (McLeod) pitch.
- **Noise gate** — `felitronics::dynamics::NoiseGate`, the engine OrbitCab ships:
  Schmitt + hold, transient-safe open, pop-free enable. Dual detection: it always
  KEYS off the raw guitar, and the MUTE lands where the player says — at the
  start, or pre-reverb (the default: the hiss the boost and preamp ADD dies too,
  the clean key never pumps, the reverb tail rings out). Indicated the way gates
  are: a live key-level meter with both decision marks on it — OPEN (the
  threshold, draggable) and CLOSE (the engine's hysteresis under it). One feel
  control — Decay, the close ramp: Normal is a natural die-away, Metal is the
  chop. Attack, hold and hysteresis stay the engine's. Off by default.
- **EQ** — DSP, and **part of the captured block, not a separate link**: each
  console sits right AFTER its block's nonlinearity, colouring what the device
  made — the boost's EQ feeds the preamp, the preamp's feeds the power amp, where
  a real amplifier keeps its tone stack. It goes dark with its block. Two faces
  per console: DEVICE TONE — the pack's own measured knobs — and UNIVERSAL EQ
  (the default) — our parametric in their place: two shelves with free corners,
  two tone bells (a third narrow one switches in), HPF/LPF with a real slope
  choice (6–48 dB/oct). A pack that measured nothing falls to UNIVERSAL on its own.
- **Boost** — a separate captured (neural) block in front. Toggleable.
- **Preamp** — the captured voicing. Gain 0–10 maps to the captured positions —
  SMOOTH crossfades between them, STEP lands the dial on them; the biggest knob
  (the hero). The tone console lives in the block — see the EQ entry above.
- **Reverb** — DSP (algorithmic). The character is the title — Room · Hall ·
  Plate · Spring — size and damping follow from it; Mix is the one knob.
- **Power amp** — optional, a captured slot like the boost and the preamp; off and
  hidden behind the gear until asked for, and a hidden block does not colour the
  sound.
- **Cabinet** — one impulse response, drawn as its waveform; HPF / LPF / trim /
  phase are baked into the IR itself, so the convolution stays one clean pass.
- **Limiter** — the safety at the door on the way out: on by default, because
  protection you must remember to switch on protects nobody.

**Stereo, three ways**: MONO; STEREO (everything twice, each side through its own
amp); STEREO SPACE — mono where the sound is made, stereo from the reverb on,
where the space is. Until somebody chooses, the environment decides: the
standalone opens on STEREO SPACE, a plugin on a mono bus on MONO, on a stereo bus
on STEREO.

### Captured vs. rebuilt

| thing | how |
|---|---|
| boost / preamp / power amp nonlinearity | neural profile (captured) |
| gain positions | discrete captured profiles, 0–10 |
| tone / EQ | the device's measured knobs, or our parametric in their place |
| loudness-vs-gain | our own curve (capture normalises it away) |
| reverb | DSP |
| cabinet | impulse response (its post baked in) |

## Visual language (faceplate)

Futuristic, not retro — no grilles / tolex / glowing tubes; a dark, "instrument"
feel. Brand tokens: a violet accent (per-device overridable), an orange "spark"
constant for the captured neural core, the cat mark. Knobs are the heroes (value on
the face, 0–10 numeric with notches). The blocks are framed, toggleable modules,
colour-coded: **orange = captured**, **violet = DSP**. Real device names title the
captured blocks; the Darwin's Cat voice name rides the paper line beneath.

**One layout — no zoom, no modes.** Everything is readable at 1×: the captured
blocks with their consoles in the upper row, reverb and cabinet below, the power
amp joining when shown. IN and OUT meter rails flank the face; the gate and the
limiter sit as badges at the bottom with the wave ribbon between them. A top
chrome carries undo / redo · A/B/C/D · presets · the gear; the footer states the
facts of the run — stereo mode, sample rate, DSP cost. The whole editor scales
50–400% from a single factor.

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
