# orbit-amp

A curated guitar & bass tone plugin — VST3 / AU / standalone. It plays a captured
neural voicing through a boost, a measured tone/EQ stage and reverb, on a compact,
resizable faceplate.

Part of the Darwin's Cat tone toolkit (alongside the OrbitCab cabinet plugin). Free
software under the **AGPL-3.0-or-later**.

> **Status — design phase.** No build yet. The design lives in
> [`docs/DESIGN.md`](docs/DESIGN.md) and is deliberately a **soft** starting point,
> not a fixed spec — see the note at the top of that file.

## Idea in one line

Capture the nonlinear *voice* of an amp/preamp/pedal as a neural profile; rebuild
everything linear or time-based (tone, EQ, loudness, reverb) as honest DSP; hand the
cabinet off to a dedicated impulse-response stage. Curate a small set of great
voicings instead of cloning famous gear.

## Layout

```
boost → preamp (voicing) → EQ → reverb → [ power amp → cabinet ]
```

Boost and preamp are captured (neural); EQ and reverb are DSP; the power amp and
cabinet live downstream in OrbitCab.

## Building

Not yet — scaffolding is the next step. It will use JUCE and the shared `felitronics`
libraries via CMake FetchContent, macOS-first.

## License

AGPL-3.0-or-later — see [`LICENSE`](LICENSE).
