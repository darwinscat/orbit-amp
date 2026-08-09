#pragma once

#include "../core/ScopeTap.h"
#include "../core/WaveRibbon.h"
#include "Theme.h"

#include <felitronics/appkit/DeviceGlyph.h>

#include <array>

namespace orbitamp
{

/** What the pedal does, drawn four ways. The signal is level-matched before it gets here, so every
    mode shows what the pedal DID rather than that it got louder.

    No axes and no numbers on any of them. This is the picture on a knob that reads 1 to 10 — it
    exists so you can see what bends, not so you can measure it. */
class BoostScope final : public juce::Component,
                         private juce::Timer
{
public:
    enum class Mode { shape, envelope, transfer, tone, wave };

    explicit BoostScope (const core::ScopeTap& source, core::WaveRibbon& ribbonSource,
                         std::function<double (double)> toneDbAt)
        : tap (source), ribbon (ribbonSource), toneDb (std::move (toneDbAt))
    {
        setInterceptsMouseClicks (false, false);
        startTimerHz (60);   // the ribbon slides; 24 reads as a slideshow
    }

    void setMode (Mode m)
    {
        if (m != mode)
        {
            mode = m;
            repaint();
        }
    }

    Mode getMode() const noexcept { return mode; }

    /** What is inside the device, drawn over the picture. It belongs to the picture rather than to a
        row of its own: the row cost a line of height and the glyphs were small in it, and what is in
        the box is a caption on what the box does, not a separate fact. */
    void setSpec (felitronics::appkit::DeviceSpec s)
    {
        if (s != spec)
        {
            spec = std::move (s);
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();

        g.setColour (theme::bezel);
        g.fillRoundedRectangle (r, theme::radiusMd);

        {
            const juce::Graphics::ScopedSaveState clipped (g);
            juce::Path well;
            well.addRoundedRectangle (r, theme::radiusMd);
            g.reduceClipRegion (well);

            switch (mode)
            {
                case Mode::shape:    paintShape (g, r.reduced (6.0f)); break;
                case Mode::envelope: paintEnvelope (g, r.reduced (6.0f)); break;
                case Mode::transfer: paintTransfer (g, r.reduced (6.0f)); break;
                case Mode::tone:     paintTone (g, r.reduced (6.0f)); break;
                case Mode::wave:     paintWave (g, r.reduced (6.0f)); break;
            }

            paintSpec (g, r.reduced (5.0f));
        }

        g.setColour (theme::hair2);
        g.drawRoundedRectangle (r.reduced (0.5f), theme::radiusMd, 1.0f);
    }

private:
    /** The glyph row, over the picture's top-left corner. A running waveform passes straight through
        thin strokes, so the glyphs sit on a scrim — barely there against the well, enough that they
        never have to compete with what is moving behind them. */
    void paintSpec (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const int n = felitronics::appkit::deviceSpecCount (spec);
        if (n <= 0)
            return;

        auto row = r.removeFromTop (glyphSize).removeFromLeft (glyphSize * (float) n);

        g.setColour (theme::bezel.withAlpha (0.72f));
        g.fillRoundedRectangle (row.expanded (3.0f, 2.0f), theme::radiusSm);

        felitronics::appkit::drawDeviceSpecStatic (g, row, spec);
    }

    void timerCallback() override
    {
        if (mode != Mode::tone)
            repaint();   // the live modes move; the tone curve only changes when a knob does
    }

    bool fetch() const
    {
        return tap.read (const_cast<std::array<float, core::ScopeTap::size>&> (dry),
                         const_cast<std::array<float, core::ScopeTap::size>&> (wet));
    }

    static void trace (juce::Graphics& g, juce::Rectangle<float> r, const float* data, int n,
                       juce::Colour colour, float thickness)
    {
        if (n < 2)
            return;

        juce::Path p;
        for (int i = 0; i < n; ++i)
        {
            const float x = r.getX() + (float) i / (float) (n - 1) * r.getWidth();
            const float y = r.getCentreY() - juce::jlimit (-1.0f, 1.0f, data[i]) * r.getHeight() * 0.5f;

            if (i == 0) p.startNewSubPath (x, y);
            else        p.lineTo (x, y);
        }

        g.setColour (colour);
        g.strokePath (p, juce::PathStrokeType (thickness));
    }

    /** One cycle, big. A sine going square says "hard"; a lopsided one says the halves are treated
        differently, which is the warm-versus-angry difference without a word of explanation. */
    void paintShape (juce::Graphics& g, juce::Rectangle<float> r)
    {
        if (! fetch())
            return;

        // Find a rising zero crossing near the end and take a window back from it, so the drawing
        // sits still instead of sliding.
        int end = core::ScopeTap::size - 1;
        for (int i = core::ScopeTap::size - 2; i > core::ScopeTap::size / 2; --i)
            if (dry[(size_t) i] <= 0.0f && dry[(size_t) (i + 1)] > 0.0f) { end = i; break; }

        const int n = juce::jmin (600, end);
        const int from = end - n;

        trace (g, r, dry.data() + from, n, theme::txFaint.withAlpha (0.55f), 1.0f);
        trace (g, r, wet.data() + from, n, theme::orange, 1.6f);
    }

    /** The peak over time, dry against wet. The picked attack stands up on the dry and is flattened
        on the wet, with the tail carried further — squashing, drawn. */
    void paintEnvelope (juce::Graphics& g, juce::Rectangle<float> r)
    {
        if (! fetch())
            return;

        constexpr int buckets = 128;
        std::array<float, buckets> de {}, we {};
        const int per = core::ScopeTap::size / buckets;

        for (int b = 0; b < buckets; ++b)
            for (int i = b * per; i < (b + 1) * per; ++i)
            {
                de[(size_t) b] = juce::jmax (de[(size_t) b], std::abs (dry[(size_t) i]));
                we[(size_t) b] = juce::jmax (we[(size_t) b], std::abs (wet[(size_t) i]));
            }

        auto fill = [&g, r] (const std::array<float, buckets>& e, juce::Colour c, float alpha)
        {
            juce::Path p;
            p.startNewSubPath (r.getX(), r.getBottom());
            for (int b = 0; b < buckets; ++b)
                p.lineTo (r.getX() + (float) b / (buckets - 1) * r.getWidth(),
                          r.getBottom() - juce::jlimit (0.0f, 1.0f, e[(size_t) b]) * r.getHeight());
            p.lineTo (r.getRight(), r.getBottom());
            p.closeSubPath();

            g.setColour (c.withAlpha (alpha));
            g.fillPath (p);
            g.setColour (c);
            g.strokePath (p, juce::PathStrokeType (1.2f));
        };

        fill (de, theme::txFaint, 0.15f);
        fill (we, theme::orange,  0.22f);
    }

    /** Output against input. A clean path is a diagonal; soft clipping bends it into an S; hard
        clipping flattens its ends. A device that reacts to frequency draws a loop instead of a line,
        and the loop's width is that reaction. */
    void paintTransfer (juce::Graphics& g, juce::Rectangle<float> r)
    {
        if (! fetch())
            return;

        const float side = juce::jmin (r.getWidth(), r.getHeight());
        const auto box = r.withSizeKeepingCentre (side, side);

        g.setColour (theme::hair);
        g.drawLine (box.getX(), box.getBottom(), box.getRight(), box.getY(), 1.0f);   // unity

        juce::Path p;
        bool started = false;

        for (int i = core::ScopeTap::size / 2; i < core::ScopeTap::size; ++i)
        {
            const float x = box.getCentreX() + juce::jlimit (-1.0f, 1.0f, dry[(size_t) i]) * side * 0.5f;
            const float y = box.getCentreY() - juce::jlimit (-1.0f, 1.0f, wet[(size_t) i]) * side * 0.5f;

            if (! started) { p.startNewSubPath (x, y); started = true; }
            else           p.lineTo (x, y);
        }

        g.setColour (theme::orange.withAlpha (0.85f));
        g.strokePath (p, juce::PathStrokeType (1.0f));
    }

    /** Amplitude, bent so quiet is visible.

        Linear, a picked note spends most of its life in the bottom tenth of the well and the picture
        is a flat line with a spike on it. This lifts the small end without a scale break at zero: at
        full deflection it is unchanged, at a tenth it is a quarter, at a hundredth nearly a sixth.
        The picture is not a meter — see the four modes it lives among — and legibility beats a
        proportion nobody is going to measure. */
    static float shape (float a)
    {
        return juce::jlimit (-1.0f, 1.0f,
                             (a < 0.0f ? -1.0f : 1.0f) * std::pow (std::abs (a), 0.7f));
    }

    /** Both waveforms over a few seconds, one on top of the other and matched for level.

        Orange is the input, filled. Violet is the output — the deeper accent rather than the lilac
        tint, because the line spends most of its length ON the orange and has to hold against it —
        drawn over the fill as an OUTLINE — so the whole
        picture is where the violet line runs against the orange body: inside it at the pick attack,
        where the peaks were cut, and outside it through the sustain, where the tail was lifted.

        An outline rather than a second filled shape, because a solid one hid the difference exactly
        where there was one to see, which is most of a note. */
    void paintWave (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const int pixels = juce::jmax (2, (int) r.getWidth());
        ribbon.setResolution (pixels);

        int count = 0;
        float phase = 0.0f;
        if (! ribbon.read (columns, count, phase) || count < 2)
            return;

        const float mid = r.getCentreY();
        const float half = r.getHeight() * 0.5f;

        // One column per pixel, so no grouping is done here at all — grouping is what shimmered.
        auto extremes = [this, count, pixels] (auto pick, int x, bool)
        {
            return pick (juce::jlimit (0, count - 1, x * count / juce::jmax (1, pixels)));
        };

        // Per PIXEL, not per bucket. There are more buckets than pixels, so drawing one rectangle per
        // bucket at a fractional step aliased into visible bands — four of them, and they showed up
        // even over silence, where the picture should have been a line. A pixel takes the extremes of
        // every bucket that lands in it, which is what a waveform display does.
        // ONE filled path, not a rectangle per pixel. A rectangle exactly one unit wide lands on
        // fractional device pixels once the editor is scaled, and the antialiased seams between them
        // read as a picket fence — the gaps were the rasteriser, not the audio.
        auto band = [&] (auto lo, auto hi, juce::Colour c, bool filled)
        {
            juce::Path p;
            p.startNewSubPath (r.getX() - phase, mid - shape (extremes (hi, 0, true)) * half);

            // Started explicitly. A path whose first instruction is lineTo begins at the origin, so
            // every frame drew a stroke down from the well's top-left corner — a diagonal that had
            // nothing to do with the audio and was there even in silence.
            // One past the right edge: the whole path slides left by up to a pixel, and without it a
            // sliver of well would blink at the edge every column.
            for (int x = 1; x <= pixels; ++x)
                p.lineTo (r.getX() + (float) x - phase, mid - shape (extremes (hi, x, true)) * half);

            for (int x = pixels + 1; --x >= 0;)
                p.lineTo (r.getX() + (float) x - phase, mid - shape (extremes (lo, x, false)) * half);

            p.closeSubPath();

            g.setColour (c);

            if (filled)
                g.fillPath (p);

            // Stroked either way: the path collapses to a line where the signal is silent, and a line
            // with no thickness disappears. The approach to a note is worth seeing.
            g.strokePath (p, juce::PathStrokeType (filled ? 1.0f : 1.4f));
        };

        band ([this] (int i) { return columns[(size_t) i].dryLo; },
              [this] (int i) { return columns[(size_t) i].dryHi; },
              theme::orange.withAlpha (0.5f), true);

        // The output as an OUTLINE over the filled input. A solid shape on top hid the very thing the
        // picture is about — how the two differ — everywhere they overlap, which is most of a note.
        band ([this] (int i) { return columns[(size_t) i].wetLo; },
              [this] (int i) { return columns[(size_t) i].wetHi; }, theme::violet, false);
    }

    /** The measured tone control at wherever its knob sits. */
    void paintTone (juce::Graphics& g, juce::Rectangle<float> r)
    {
        if (toneDb == nullptr)
            return;

        constexpr double lo = 20.0, hi = 20000.0;
        constexpr float range = 20.0f;

        juce::Path p;
        const int w = juce::jmax (2, (int) r.getWidth());

        for (int x = 0; x < w; ++x)
        {
            const double t  = (double) x / (w - 1);
            const double hz = lo * std::pow (hi / lo, t);
            const float  y  = r.getCentreY() - juce::jlimit (-range, range, (float) toneDb (hz))
                                                 / range * r.getHeight() * 0.5f;

            if (x == 0) p.startNewSubPath (r.getX() + (float) x, y);
            else        p.lineTo (r.getX() + (float) x, y);
        }

        g.setColour (theme::hair);
        g.fillRect (r.getX(), r.getCentreY(), r.getWidth(), 1.0f);

        g.setColour (theme::orange);
        g.strokePath (p, juce::PathStrokeType (1.6f));
    }

    static constexpr float glyphSize = 26.0f;   // was 18 in a row of its own, and small there

    const core::ScopeTap& tap;
    core::WaveRibbon& ribbon;
    std::function<double (double)> toneDb;
    felitronics::appkit::DeviceSpec spec;

    mutable std::array<float, core::ScopeTap::size> dry {}, wet {};
    mutable std::array<core::WaveRibbon::Column, core::WaveRibbon::buckets> columns {};
    Mode mode = Mode::shape;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BoostScope)
};

} // namespace orbitamp
