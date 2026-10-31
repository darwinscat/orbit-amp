// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "PitchTracker.h"
#include "TunerEar.h"
#include "TunerTap.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace orbitamp::core
{

/** THE STROBE — the tuner's other way of showing a note: a real phase strobe, as Peterson's, not stripes
    pushed along by the cents a pitch tracker reads.

    It holds the sound against the EXACT frequency of the note being tuned: each band mixes the input with
    a complex oscillator at that frequency (or a partial of it), low-passes what comes out, and takes its
    phase. A string at the note's frequency leaves that phase standing; a string off it turns it at exactly
    the difference, in turns per second — read from the sound itself, sample by sample, finer than any cent.
    The stripes on a face move by that phase.

    THE BANDS are the fundamental, its octave and two octaves — partials 1, 2 and 4: the octaves a bass and
    a guitar both ring strongest on (a bass's 2nd and 4th often over its 1st, a wound guitar string's 2nd
    over its 1st), and the same three a mechanical multi-band strobe shows. Each band's phase is counted in
    turns OF THE FUNDAMENTAL (its own over its partial), so a perfectly harmonic note moves all three the
    same, and a partial that runs sharp — a stiff string's do — drifts against the fundamental where the
    player can see it.

    THE NOTE it holds to is the ear's nearest, with hysteresis: a reading has to stand `holdCents` from it
    before the strobe takes the next — so a string sitting between two names, or wavering round the middle,
    never flips it, and the stripes never jump. A note heard again after silence takes its own nearest.
    The oscillators stay continuous in sample time through all of it, whatever the ticks' lengths.

    WHERE IT RUNS: the message thread, on the processor's tuner pump, after the ear — it reads the tap's new
    samples since its last tick (TunerTap::readSince), so the audio thread pays nothing. It allocates
    nothing. Not pumped (NEEDLE, or nobody watching), it costs nothing and the ear is exactly as before.

    JUCE-free like the ear: the whole strobe is testable headless. */
class TunerStrobe
{
public:
    static constexpr int numBands = 3;
    static constexpr std::array<int, numBands> partials { 1, 2, 4 };

    /** The target note changes only when the ear's reading stands this far from it — 15 cents past the
        middle between two notes. */
    static constexpr float holdCents = 65.0f;

    /** How fast the stripes move: every note's as fast as A4's own would — 0.254 stripes a second a cent —
        so a low E moves its stripes for a cent as visibly as a high one. A clock, not a gain: the stripes
        still move by the sound's own phase, only on a common time scale. */
    static constexpr double speedHz = 440.0;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        reset();
    }

    /** Forget the note, the filters and where it read the tap: the next tick starts from now. */
    void reset() noexcept
    {
        fresh  = true;
        heard  = false;
        target = -1;
        a4Used = 0.0f;
        noteHz = 0.0;
        count  = 0;
        for (auto& b : band)
            b = Band {};
    }

    /** NOT PUMPED THIS TICK — the tuner shows its needle, or it is off, or nobody is watching. `shown`: the
        player still reads by the strobe (the tuner is only off), so the faces keep its bands, empty, rather
        than the needle's ruler. The next update() starts afresh, from the tap's newest sample. */
    void stop (bool shown = false) noexcept
    {
        running = shown;
        fresh   = true;
    }

    /** One tick in STROBE mode: pick (or keep) the note from the ear, then demodulate every sample the tap
        has had since the last tick against it. Message thread; allocates nothing. */
    void update (const TunerTap& tap, const TunerEar& ear)
    {
        running = true;
        if (sr <= 0.0)
            return;

        if (fresh)
        {
            reset();
            cursor = tap.writtenCount();   // from now: the ring's past belongs to whatever came before
            fresh  = false;
        }

        follow (ear);
        listen (tap);
    }

    /** The note — the target — as the ear's reading `hz` chooses it, with the hysteresis above: `live` is
        whether the ear hears a note at all. Public for the tests; update() calls it with the ear's own. */
    void follow (bool live, float hz, float a4)
    {
        if (! live || hz <= 0.0f)
        {
            heard = false;   // the next note, whenever it comes, takes its own nearest
            return;
        }

        const int nearest = PitchTracker::nearestNote (hz, a4).midi;
        const bool keep = heard && target >= 0
                          && std::abs (1200.0 * std::log2 ((double) hz / noteHzOf (target, a4))) <= (double) holdCents;
        heard = true;
        hold (keep ? target : nearest, a4);
    }

    /** Hold the strobe to MIDI note `midi` measured from an A4 of `a4` Hz. The oscillators keep their
        phase — they only change speed; the filters start again when the note changes. */
    void hold (int midi, float a4)
    {
        if (midi == target && std::abs (a4 - a4Used) < 1.0e-3f)
            return;

        target = midi;
        a4Used = a4;
        noteHz = noteHzOf (midi, a4);

        const double twoPi = 2.0 * pi;
        const double fc    = std::clamp (noteHz / 8.0, 2.0, 12.0);   // under the note's spacing by 8x
        coeff = 1.0 - std::exp (-twoPi * fc / sr);

        for (int k = 0; k < numBands; ++k)
        {
            auto& b = band[(size_t) k];
            const double f = noteHz * partials[(size_t) k];
            b.on  = f < 0.45 * sr;
            b.inc = b.on ? f / sr : 0.0;
            b.wr  = std::cos (twoPi * b.inc);
            b.wi  = -std::sin (twoPi * b.inc);
            b.cr  = std::cos (twoPi * b.phase);
            b.ci  = -std::sin (twoPi * b.phase);
            b.zr.fill (0.0);
            b.zi.fill (0.0);
            b.pr = b.pi = 0.0;
            b.mag = 0.0;
        }
    }

    /** Demodulate the tap's new samples against the note held. Public for the tests (with hold()). */
    void listen (const TunerTap& tap)
    {
        unsigned skipped = 0;
        const int n = tap.readSince (cursor, chunk.data(), (int) chunk.size(), skipped);
        if (target < 0)
        {
            count += skipped + (unsigned) n;
            return;
        }

        if (skipped > 0)
            jump (skipped);

        const std::array<double, numBands> before { band[0].turns, band[1].turns, band[2].turns };

        for (int i = 0; i < n; ++i)
            step ((double) chunk[(size_t) i]);

        // Strength — each band against the strongest — and speed, smoothed over a few ticks.
        double strongest = 0.0;
        for (const auto& b : band)
            strongest = std::max (strongest, b.mag);

        const double seconds = (double) n / sr;
        for (int k = 0; k < numBands; ++k)
        {
            auto& b = band[(size_t) k];
            b.strength = b.on && strongest > 0.0 ? (float) (b.mag / strongest) : 0.0f;
            if (seconds > 0.0)
            {
                const double now = (b.turns - before[(size_t) k]) * speedHz / noteHz / seconds;
                b.speed += 0.4 * (now - b.speed);
            }
        }
    }

    bool on() const noexcept        { return running; }       // the faces draw the strobe, not the needle
    int  note() const noexcept      { return target; }        // MIDI note it is held to, -1 none yet
    double noteFrequency() const noexcept { return noteHz; }  // in Hz, 0 none

    /** The ear's reading `hz` in cents from the note held — the number a face writes beside the stripes. */
    float cents (float hz) const noexcept
    {
        return noteHz > 0.0 && hz > 0.0f ? (float) (1200.0 * std::log2 ((double) hz / noteHz)) : 0.0f;
    }

    /** Band `k`'s phase, unwrapped, in turns of the fundamental: how far the sound has run ahead of (or
        behind) the note since the strobe started. Its rate is the fundamental's error in Hz. */
    double turns (int k) const noexcept   { return band[(size_t) k].turns; }

    /** Where band `k`'s stripes stand, 0..1 of a stripe of the FUNDAMENTAL's band — band k has `partials[k]`
        stripes in that width, all moving by the same distance: draw the pattern shifted by this much. */
    float stripes (int k) const noexcept  { return (float) band[(size_t) k].stripes; }

    /** How fast they move, in fundamental stripes a second — for a face that must blur what its frame rate
        cannot show. Signed: sharp moves forward. */
    float speed (int k) const noexcept    { return (float) band[(size_t) k].speed; }

    /** How strongly band `k`'s partial is there, 0..1 against the strongest band's — how much contrast its
        stripes are drawn with. */
    float strength (int k) const noexcept { return band[(size_t) k].strength; }

    static double noteHzOf (int midi, float a4) noexcept
    {
        return (double) a4 * std::pow (2.0, (midi - 69) / 12.0);
    }

private:
    static constexpr double pi = 3.14159265358979323846;
    static constexpr int poles = 4;           // one-pole stages in each band's low-pass
    static constexpr unsigned decimate = 16;  // the phase is read every this many samples...
    static constexpr unsigned resync  = 256;  // ...the oscillator re-drawn from its exact phase every this many

    struct Band
    {
        bool   on = false;
        double phase = 0.0, inc = 0.0;          // the oscillator, in turns
        double cr = 1.0, ci = 0.0, wr = 1.0, wi = 0.0;
        std::array<double, poles> zr {}, zi {};
        double pr = 0.0, pi = 0.0;              // the low-pass's output when the phase was last read
        double mag = 0.0;
        double turns = 0.0, stripes = 0.0, speed = 0.0;
        float  strength = 0.0f;
    };

    /** One sample through every band. Time is the absolute sample count, so where the phase is read and
        the oscillators re-drawn never depends on how the ticks cut the input. */
    void step (double x)
    {
        ++count;
        const bool read   = count % decimate == 0;
        const bool redraw = count % resync == 0;

        for (int k = 0; k < numBands; ++k)
        {
            auto& b = band[(size_t) k];
            if (! b.on)
                continue;

            double re = x * b.cr, im = x * b.ci;
            for (int s = 0; s < poles; ++s)
            {
                b.zr[(size_t) s] += coeff * (re - b.zr[(size_t) s]);
                b.zi[(size_t) s] += coeff * (im - b.zi[(size_t) s]);
                re = b.zr[(size_t) s];
                im = b.zi[(size_t) s];
            }

            b.phase += b.inc;
            if (b.phase >= 1.0)
                b.phase -= 1.0;

            if (redraw)
            {
                b.cr = std::cos (2.0 * pi * b.phase);
                b.ci = -std::sin (2.0 * pi * b.phase);
            }
            else
            {
                const double r = b.cr * b.wr - b.ci * b.wi;
                b.ci = b.cr * b.wi + b.ci * b.wr;
                b.cr = r;
            }

            if (read)
            {
                if (b.pr != 0.0 || b.pi != 0.0)
                {
                    // The turn since the last reading: the angle of now times the conjugate of then.
                    const double d = std::atan2 (im * b.pr - re * b.pi, re * b.pr + im * b.pi) / (2.0 * pi)
                                     / (double) partials[(size_t) k];
                    b.turns   += d;
                    b.stripes += d * speedHz / noteHz;
                    b.stripes -= std::floor (b.stripes);
                }
                b.pr  = re;
                b.pi  = im;
                b.mag = std::sqrt (re * re + im * im);
            }
        }
    }

    /** Samples the tap had and this never saw: the oscillators run on through them, the filters start again. */
    void jump (unsigned samples)
    {
        count += samples;
        for (auto& b : band)
        {
            if (! b.on)
                continue;
            const double turnsGone = std::fmod ((double) samples * b.inc, 1.0);
            b.phase += turnsGone;
            b.phase -= std::floor (b.phase);
            b.cr = std::cos (2.0 * pi * b.phase);
            b.ci = -std::sin (2.0 * pi * b.phase);
            b.zr.fill (0.0);
            b.zi.fill (0.0);
            b.pr = b.pi = 0.0;
        }
    }

    void follow (const TunerEar& ear) { follow (ear.live(), ear.hz(), ear.referenceHz()); }

    double sr = 0.0, coeff = 0.0, noteHz = 0.0;
    int    target = -1;
    float  a4Used = 0.0f;
    bool   heard = false, fresh = true, running = false;
    unsigned cursor = 0, count = 0;
    std::array<Band, numBands> band {};
    std::array<float, TunerTap::size> chunk {};
};

} // namespace orbitamp::core
