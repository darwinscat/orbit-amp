#pragma once

#include "../core/ScopeTap.h"
#include "../core/WaveRibbon.h"
#include "Theme.h"
#include "scope/EnvelopeView.h"
#include "scope/ShapeView.h"
#include "scope/ToneView.h"
#include "scope/TransferView.h"
#include "scope/WaveView.h"

#include <felitronics/appkit/DeviceGlyph.h>

#include <array>

namespace orbitamp
{

/** What the pedal does, drawn several ways.

    This is the WELL, not the drawing: it owns the recess, the mode, the frame rate, the glyph row,
    and reading the tap. Each way of looking lives in its own file under scope/ and is handed a
    rectangle and a window of audio — none of them knows it is in a plugin, and none of them can
    decide when to read.

    No axes and no numbers on any of them. This is the picture on a knob that reads 1 to 10: it
    exists so you can see what bends, not so you can measure it. The one exception is the tone curve,
    which is a measurement and says so by being drawn against the band it is trusted in. */
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

            const auto area = r.reduced (6.0f);
            const auto frame = fetch();

            switch (mode)
            {
                case Mode::shape:    scope::ShapeView::paint (g, area, frame); break;
                case Mode::envelope: scope::EnvelopeView::paint (g, area, frame); break;
                case Mode::transfer: scope::TransferView::paint (g, area, frame); break;
                case Mode::tone:     toneView.paint (g, area, toneDb, frame); break;
                case Mode::wave:     waveView.paint (g, area, ribbon); break;
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

    void timerCallback() override { repaint(); }

    /** The tap, copied out once per frame and handed to whichever view is showing. Empty when the tap
        has never been written. */
    scope::Frame fetch()
    {
        if (! tap.read (dry, wet))
            return {};

        return { dry.data(), wet.data(), core::ScopeTap::size };
    }

    static constexpr float glyphSize = 26.0f;   // was 18 in a row of its own, and small there

    const core::ScopeTap& tap;
    core::WaveRibbon& ribbon;
    std::function<double (double)> toneDb;
    felitronics::appkit::DeviceSpec spec;

    scope::ToneView toneView;
    scope::WaveView waveView;

    std::array<float, core::ScopeTap::size> dry {}, wet {};
    Mode mode = Mode::shape;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BoostScope)
};

} // namespace orbitamp
