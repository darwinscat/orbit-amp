// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "Prefs.h"
#include "Theme.h"
#include "../Parameters.h"
#include "../PluginProcessor.h"
#include "../core/OctaveTap.h"
#include "../core/PitchTracker.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace orbitamp
{

/** OCTAVE'S PICTURE — "the note and its sub": a small oscilloscope beside OCTAVE's slider. The note going in, dim grey
    and translucent; the SUB the stage adds, a violet line over it with twice the note's period; HELD STILL — each
    frame starts just before a rising edge of the sub and is two sub periods across, so the note shows four cycles
    and the sub two, standing where they stood. While the octaver has not caught the note (Octaver::isLocked) there
    is no sub line, and the newest stretch of the input is shown as it comes. Above it a READING — the caught note and
    its octave down, "E1 → E0", named by the tuner's own reference (prefs::tunerReference) — written as text, no field:
    it is not a setting.

    The note is the sub's: the period between the sub's rising edges is two of the note's (the octaver flips its square
    on each edge it counts), so what the reading names is what the stage locked on — a double trigger reads as one.
    With the -2 playing too, the sub's edges come every two or every four note periods, and the input's own repeat
    says which.

    It reads the processor's tap (core::OctaveTap) on its own timer, and the audio thread writes the tap only while
    this is on screen (isShowing(): its own visibility, every ancestor's, the window's); it repaints only when the tap
    moved or the lock changed, and the tap writes no silence: in silence the picture stands still — and standing by or at
    0, when nothing is written, dimmed with its block (the owner, 31.10). A click on it is
    the block's (its menu). The reading is 13 px. */
class OctaveScope final : public juce::Component,
                          private juce::Timer
{
public:
    explicit OctaveScope (AmpProcessor& processor)
        : amp (processor),
          amountP (*processor.apvts.getParameter (params::octaveAmount)),
          sub2P (*processor.apvts.getParameter (params::octaveSub2)),
          dry ((size_t) core::OctaveTap::capacity),
          sub ((size_t) core::OctaveTap::capacity)
    {
        crossings.reserve (512);
        setInterceptsMouseClicks (false, false);   // the block's: a click anywhere on it is OCTAVE's menu
        startTimerHz (30);
    }

    ~OctaveScope() override { watch (false); }

    static constexpr float textHeight = 13.0f, readingH = 20.0f;

    /** What a frame shows, from the tap — the gates read it. Positions in the read's own samples, at `rateHz`. */
    struct Frame
    {
        int    count     = 0;       // samples held
        double rateHz    = 0.0;
        double start     = 0.0;     // the first sample drawn (fractional)
        double span      = 0.0;     // samples across
        double trigger   = -1.0;    // the sub's rising edge the frame stands on (-1: none)
        double subPeriod = 0.0;     // the -1's period, samples (0: no note caught)
        double noteHz    = 0.0;     // the caught note, Hz (0: none)
        bool   subShown  = false;
    };

    const Frame& frame() const noexcept { return shown; }

    /** The sub as the frame holds it, at a fractional sample (the gates compare two frames with it). */
    float subAt (double i) const noexcept { return sampleAt (sub, i); }
    float dryAt (double i) const noexcept { return sampleAt (dry, i); }

    /** The note names the reading writes: "E1", "B-1" — by the tuner's reference, A = `refHz`. */
    static juce::String noteText (double hz, double refHz)
    {
        // The octave by floor, not PitchTracker::noteOctave's truncation: the sub of a low note can sit under C-1's
        // MIDI 0, and B-2 is not B-1.
        const int midi = juce::roundToInt (69.0 + 12.0 * std::log2 (hz / refHz));
        const int octave = (int) std::floor ((double) midi / 12.0) - 1;
        return juce::String (core::PitchTracker::noteName (midi)) + juce::String (octave);
    }

    /** The reading's two names, empty while no note is caught. */
    std::pair<juce::String, juce::String> reading() const
    {
        if (! shown.subShown || shown.noteHz <= 0.0)
            return {};
        const double ref = (double) prefs::tunerReference (amp.product());
        return { noteText (shown.noteHz, ref), noteText (shown.noteHz * 0.5, ref) };
    }

    /** Whether the audio thread writes the tap for it now (AmpProcessor::octaveWatched). */
    bool watching() const noexcept { return watched; }

    /** What the tap holds, worked into a frame — the timer's work, and a picture's made off screen. */
    void pull()
    {
        // ITS BLOCK NOT AT WORK (standing by, at 0, out of the strip): the frame stands WHOLE — its samples, its lock, its
        // reading — as the block left it; nothing re-read, nothing re-analysed (review 9).
        if (! amp.octaveAtWork())
            return;
        std::uint32_t newest = 0;
        const int  n      = amp.octaveTap->read (dry.data(), sub.data(), core::OctaveTap::capacity, &newest);
        const bool locked = amp.effectLampDepth (0) > 0.5f;
        if (newest == lastNewest && locked == lastLocked && n == shown.count)
            return;   // nothing new: the picture stands still
        lastNewest = newest;
        lastLocked = locked;
        shown = analyse (n, locked, (double) amp.octaveTap->rateHz());

        float peak = 0.0f;
        for (int i = juce::jmax (0, (int) shown.start), e = juce::jmin (n, (int) (shown.start + shown.span) + 2); i < e; ++i)
            peak = juce::jmax (peak, std::abs (dry[(size_t) i]), shown.subShown ? std::abs (sub[(size_t) i]) : 0.0f);
        scalePeak = juce::jmax (1.0e-3f, peak, scalePeak * 0.8f);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        paintReading (g, b.removeFromTop (readingH));

        const auto w = b.reduced (1.0f);
        g.setColour (well);
        g.fillRoundedRectangle (w, 3.0f);
        g.setColour (theme::hair);
        g.fillRect (w.getX() + 2.0f, w.getCentreY() - 0.5f, w.getWidth() - 4.0f, 1.0f);

        if (shown.count < 2 || shown.span <= 1.0)
            return;

        const int   px   = juce::jmax (1, (int) w.getWidth() - 4);
        const float x0   = w.getX() + 2.0f, cy = w.getCentreY();
        const float gain = w.getHeight() * 0.44f / scalePeak;
        const auto  pos  = [&] (int x) { return shown.start + shown.span * (double) x / (double) px; };

        // THE NOTE: each pixel column the lowest and highest of the samples it covers, joined to the last one's, so the
        // trace is continuous whatever the zoom — one band, its top edge out along the highs and its bottom back along
        // the lows, at least a line thick; dim grey and translucent, the sub reads through it.
        std::vector<float>& top = bandTop;
        std::vector<float>& bottom = bandBottom;
        top.resize ((size_t) px);
        bottom.resize ((size_t) px);
        float prev = dryAt (pos (0));
        for (int x = 0; x < px; ++x)
        {
            const int a = juce::jlimit (0, shown.count - 1, (int) std::floor (pos (x)));
            const int e = juce::jlimit (a, shown.count - 1, (int) std::ceil (pos (x + 1)));
            float lo = prev, hi = prev;
            for (int i = a; i <= e; ++i)
            {
                lo = juce::jmin (lo, dry[(size_t) i]);
                hi = juce::jmax (hi, dry[(size_t) i]);
            }
            prev = dry[(size_t) e];
            const float yHi = cy - hi * gain, yLo = cy - lo * gain, grow = juce::jmax (0.0f, 1.4f - (yLo - yHi)) * 0.5f;
            top[(size_t) x]    = yHi - grow;
            bottom[(size_t) x] = yLo + grow;
        }
        juce::Path band;
        band.startNewSubPath (x0, top[0]);
        for (int x = 1; x < px; ++x)
            band.lineTo (x0 + (float) x, top[(size_t) x]);
        for (int x = px - 1; x >= 0; --x)
            band.lineTo (x0 + (float) x, bottom[(size_t) x]);
        band.closeSubPath();
        g.setColour (noteGrey);
        g.fillPath (band);

        // THE SUB, a violet line over it — none until the octaver has the note.
        if (shown.subShown)
        {
            juce::Path p;
            for (int x = 0; x <= px; ++x)
            {
                const juce::Point<float> at { x0 + (float) x, cy - subAt (pos (x)) * gain };
                if (x == 0)
                    p.startNewSubPath (at);
                else
                    p.lineTo (at);
            }
            g.setColour (theme::violet.withAlpha (0.9f));
            g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    void visibilityChanged() override      { watch (isShowing()); }
    void parentHierarchyChanged() override { watch (isShowing()); }

private:
    /** A frame from the `n` samples read: on the sub's rising edge while the octaver holds the note, else the newest. */
    Frame analyse (int n, bool locked, double rate)
    {
        Frame f;
        f.count  = n;
        f.rateHz = rate;
        if (n < 64 || rate <= 0.0)
            return f;

        // Two sub periods of the last note caught — of a low E before any.
        double span = lastSpan > 0.0 ? lastSpan : 4.0 * rate / 41.2;

        float subPeak = 0.0f;
        for (int i = 0; i < n; ++i)
            subPeak = juce::jmax (subPeak, std::abs (sub[(size_t) i]));

        if (locked && subPeak > core::OctaveTap::silence)
        {
            // The sub's rising edges, armed only once it has swung below a fifth of its peak: a ripple is not an edge.
            crossings.clear();
            const float h = 0.2f * subPeak;
            bool armed = false;
            for (int i = 1; i < n; ++i)
            {
                const float s0 = sub[(size_t) i - 1], s1 = sub[(size_t) i];
                armed = armed || s1 < -h;
                if (armed && s0 < 0.0f && s1 >= 0.0f)
                {
                    crossings.push_back ((double) (i - 1) + (double) (-s0 / (s1 - s0)));
                    armed = false;
                }
            }

            if (crossings.size() >= 3)
            {
                // The edges' spacing: the median of the newest four intervals.
                double iv[4] {};
                int    m = 0;
                for (size_t k = crossings.size() - 1; k > 0 && m < 4; --k)
                    iv[m++] = crossings[k] - crossings[k - 1];
                std::sort (iv, iv + m);
                const double spacing = m % 2 == 1 ? iv[m / 2] : 0.5 * (iv[m / 2 - 1] + iv[m / 2]);

                // The -1 flips on every edge of the note: its edges are two note periods apart. With the -2 under it the
                // sum's come every two or every four, and the note itself repeating at a quarter of them says four.
                double notePeriod = spacing * 0.5;
                if (sub2Plays() && repeatsAt (n, spacing * 0.25, spacing))
                    notePeriod = spacing * 0.25;

                const double hz = rate / notePeriod;
                if (hz >= 15.0 && hz <= 600.0)
                {
                    span = 2.0 * (2.0 * notePeriod);   // two periods of the -1
                    const double pre = span / 16.0;

                    // The edge to stand on: of the newest four whose frame fits, the one the sub rises highest after (with
                    // the -2 under it, the sum's edges alternate tall and short — always the tall one), the newest of a tie.
                    int    pick = -1;
                    double best = 0.0;
                    float  rise[4] {};
                    int    idx[4] {}, c = 0;
                    for (int k = (int) crossings.size() - 1; k >= 0 && c < 4; --k)
                    {
                        const double at = crossings[(size_t) k];
                        if (at - pre < 0.0)
                            break;
                        if (at - pre + span > (double) (n - 1))
                            continue;
                        float top = 0.0f;
                        for (int i = (int) at, e = juce::jmin (n - 1, (int) (at + notePeriod)); i <= e; ++i)
                            top = juce::jmax (top, sub[(size_t) i]);
                        rise[c] = top;
                        idx[c++] = k;
                        best = juce::jmax (best, (double) top);
                    }
                    for (int k = 0; k < c && pick < 0; ++k)
                        if ((double) rise[k] >= 0.9 * best)
                            pick = idx[k];

                    if (pick >= 0)
                    {
                        f.trigger   = crossings[(size_t) pick];
                        f.start     = f.trigger - pre;
                        f.span      = span;
                        f.subPeriod = 2.0 * notePeriod;
                        f.noteHz    = hz;
                        f.subShown  = true;
                        lastSpan    = span;
                        return f;
                    }
                }
            }
        }

        // No note caught: the newest stretch, as it comes.
        f.span  = juce::jmin (span, (double) (n - 1));
        f.start = (double) (n - 1) - f.span;
        return f;
    }

    /** Whether the input repeats after `lag` samples over the newest `len`: their mean difference small against their
        mean size. A note shifted half its period is the opposite of itself; shifted a whole one, itself. */
    bool repeatsAt (int n, double lag, double len) const
    {
        const int l = juce::roundToInt (lag), w = juce::jmax (16, juce::roundToInt (len));
        double diff = 0.0, size = 0.0;
        for (int t = juce::jmax (0, n - 1 - l - w); t < n - 1 - l; ++t)
        {
            diff += std::abs ((double) dry[(size_t) t] - (double) dry[(size_t) (t + l)]);
            size += std::abs ((double) dry[(size_t) t]) + std::abs ((double) dry[(size_t) (t + l)]);
        }
        return size > 0.0 && diff < 0.35 * size;
    }

    /** The -2 under the sub now: the menu has it at the slider's top, the slider is up and OCTAVE is on. */
    bool sub2Plays() const
    {
        return sub2P.getValue() > 0.5f && amountP.convertFrom0to1 (amountP.getValue()) > 0.0f;
    }

    float sampleAt (const std::vector<float>& v, double i) const noexcept
    {
        if (shown.count < 1)
            return 0.0f;
        const double c = juce::jlimit (0.0, (double) (shown.count - 1), i);
        const int    a = (int) c, e = juce::jmin (a + 1, shown.count - 1);
        const float  t = (float) (c - (double) a);
        return v[(size_t) a] + (v[(size_t) e] - v[(size_t) a]) * t;
    }

    void paintReading (juce::Graphics& g, juce::Rectangle<float> r) const
    {
        const auto [note, below] = reading();
        if (note.isEmpty())
            return;

        // "E1 → E0": the two names in the face's type, the arrow drawn between them (no glyph to miss in the font).
        const auto  font = theme::displayFont (textHeight);
        const float wa = theme::trackedWidth (note, font, 0.04f), wb = theme::trackedWidth (below, font, 0.04f);
        const float arrowW = 18.0f, gap = 6.0f;
        const float total = wa + gap + arrowW + gap + wb;
        float x = r.getCentreX() - total * 0.5f;
        const float cy = r.getCentreY();

        g.setColour (theme::tx);
        theme::drawTracked (g, note, { x, r.getY(), wa + 2.0f, r.getHeight() }, font, 0.04f, juce::Justification::centredLeft);
        x += wa + gap;
        g.setColour (theme::txDim);
        g.drawArrow ({ x, cy, x + arrowW, cy }, 1.4f, 6.0f, 6.0f);
        x += arrowW + gap;
        g.setColour (theme::violet.brighter (0.4f));
        theme::drawTracked (g, below, { x, r.getY(), wb + 2.0f, r.getHeight() }, font, 0.04f, juce::Justification::centredLeft);
    }

    void watch (bool on)
    {
        watched = on;
        amp.octaveWatched.store (on, std::memory_order_relaxed);
    }

    void timerCallback() override
    {
        // isShowing() answers for every ancestor and the window too: a hidden parent stops the taps within a tick.
        const bool on = isShowing();
        if (on != watched)
            watch (on);
        if (on)
            pull();
    }

    inline static const juce::Colour well { 0xff121218 }, noteGrey { juce::Colour (0xffb4b2c2).withAlpha (0.45f) };

    AmpProcessor& amp;
    juce::RangedAudioParameter& amountP;
    juce::RangedAudioParameter& sub2P;
    std::vector<float>  dry, sub;
    std::vector<double> crossings;
    std::vector<float>  bandTop, bandBottom;   // the note's band, per pixel column (paint's)
    Frame shown;
    std::uint32_t lastNewest = 0;
    bool   lastLocked = false, watched = false;
    double lastSpan   = 0.0;
    float  scalePeak  = 1.0e-3f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OctaveScope)
};

} // namespace orbitamp
