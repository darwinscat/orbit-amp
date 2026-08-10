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

A guitar & bass tone plugin (VST3 / AU / standalone; JUCE; macOS-first, other
platforms later). It plays a curated *voicing* through a boost, a tone/EQ stage and
reverb, rendered on a compact, resizable faceplate. AGPL-3.0-or-later.

## Philosophy

- **Capture the voice, rebuild the rest.** A neural profile captures the static,
  nonlinear character of a preamp / amp / pedal. Everything linear or time-based —
  EQ, loudness, reverb — is rebuilt as honest DSP; the cabinet is an impulse
  response. We don't fake the nonlinearity, and we don't pretend DSP is a capture.
- **Recombine for originality.** A boost from one device + a preamp voicing from
  another = a new instrument. Curation and recombination, not clones of famous gear.
- **Curated taste over a catalogue.** A small set of voicings worth having — guitar
  and bass — rather than a museum of "legendary" heads.

## The unit is a voicing, not a channel

A hardware amp has "channels" because sharing one chassis / power section / cabinet
is cheaper than owning three amps. In software that reason disappears. Here the
addressable unit is a **voicing module**; one voicing plays at a time. You pick a
**type** (clean · edge · crunch · high-gain · modern) and a **voice** within it.
"How many channels" a device has = how many voicings it curates — one for a single,
several for a set.

A subtle but load-bearing point: what gets captured already **isn't** the amp's
channel. The tone stack is pulled out to DSP; per-take level calibration flattens
the gain→loudness ramp; the gain knob becomes a set of discrete captured profiles.
The capture holds the nonlinear *voice*; loudness, EQ, sag and reverb are our own
layers on top.

## Signal chain

```
tuner → gate → eq1 → boost → eq2 → preamp (voicing) → reverb → power amp → cabinet
```

- **Tuner** — a listener, not a processor: it taps the raw input and never touches
  the signal, so it has no switch. It sits FIRST because that is what it hears —
  and it must stay ahead of the gate, or a closed gate blinds the needle on a
  decaying note, which is exactly when you tune. MPM (McLeod) pitch, ±half a cent.
- **Noise gate** — `felitronics::dynamics::NoiseGate`, the engine OrbitCab ships:
  Schmitt + hold, transient-safe open, pop-free enable. Dual detection: it always
  KEYS off the raw guitar, and the MUTE lands where the player says — at the
  start, or pre-reverb (the default: the hiss the boost and preamp ADD dies too,
  the clean key never pumps, the reverb tail rings out). Indicated the way gates
  are: a live key-level meter with both decision marks on it — OPEN (the
  threshold, draggable) and CLOSE (the engine's hysteresis under it) — plus a
  pressure well. LEARN measures the noise floor instead of asking the player to
  guess it. One feel control — Decay (30–500 ms), the close ramp: a metal chop at
  the fast end, a natural die-away at the default; attack, hold and hysteresis
  stay the engine's. Off by default.
- **EQ links (eq1, eq2)** — DSP, and **links of the chain, not sections of any
  block**. Exactly two, fixed (a host needs an unchanging parameter list): eq1
  ahead of the boost decides what reaches the first nonlinearity — it changes the
  *kind* of distortion; eq2 between boost and preamp colours what the boost made.
  A link's place in the chain answers "pre or post", so there is no placement
  switch. Low/High are **shelves**, Mid is a peak; HPF/LPF are optional cuts
  (12 dB/oct). Both links ship off and flat. An FX link will slot in between later.
- **Boost** — a separate captured (neural) block in front. Toggleable.
- **Preamp** — the captured voicing. Gain 0–10 maps to the captured detents; the
  biggest knob (the hero). Only the device's own (measured) controls live in the
  block — our EQ does not.
- **Reverb** — DSP (algorithmic); Mix only in the simple case.
- **Power amp** — optional; simulation (white-box tube stage) or a captured slot.
- **Cabinet** — IR module (linear), mic placement on the grille.

### Captured vs. rebuilt

| thing | how |
|---|---|
| preamp / boost nonlinearity | neural profile (captured) |
| gain positions | discrete captured profiles, 0–10 |
| tone / EQ (shelves, peak, HPF/LPF) | measured DSP |
| loudness-vs-gain | our own curve (capture normalises it away) |
| sag | not captured (dynamic) — a thin DSP layer if wanted, or omit |
| reverb | DSP |
| cabinet | impulse response |

## Visual language (faceplate)

Futuristic, not retro — no grilles / tolex / glowing tubes; a dark, "instrument"
feel. Brand tokens: a violet accent (per-device overridable), an orange "spark"
constant for the captured neural core, the cat mark. Knobs are the heroes (value on
the face, 0–10 numeric with notches). The blocks are framed, toggleable modules,
colour-coded: **orange = captured**, **violet = DSP**. Golden-ratio proportions
across the overview row (the preamp is the wider anchor). The whole editor scales
50–200% from a single factor.

**The chain strip** runs across the top of the panel: every link in signal order as
a miniature — its switch, a live preview (the tuner's needle, the gate's pressure,
EQ curves, gain + tone for the captured blocks, mics on the grille for the
cabinet), and what is loaded into it. It is the map of the signal. Service links
(tuner, gate) take half a lane — the map saying "plumbing, not voice". **Clicking a
thumb zooms**: that link opens across the whole faceplate — a magnifying glass, not
a mode; the overview returns when the lit thumb is clicked again. Tightness is
cured by the zoom, never by hiding controls.

The pixel-level reference is a set of HTML mockups produced during design (kept
outside this repo). Rebuild the faceplate natively — the mockups are a spec, not
code to port.

## Portability / shared code

Reusable pieces — the faceplate render kit and the device-descriptor format — belong
in the shared `felitronics` libraries (a JUCE-free core plus an app-side kit), so
orbit-amp and OrbitCab consume the same code instead of copying it. orbit-amp itself
is a thin plugin shell over those + JUCE.

## Product scope (soft)

- Guitar first; **bass** is a strong, under-served direction and may become its own
  focused plugin (bass wants a parallel dirt blend and a crossover, rarely reverb — a
  different control set).
- Opaque, curated names; no "legendary" clones.
- Free / AGPL.

## Release chrome (later — not designed yet)

A top header + toolbar (undo / redo · A/B/C/D · presets) and a footer for power-amp /
cabinet selection, styled to match the sibling projects.
