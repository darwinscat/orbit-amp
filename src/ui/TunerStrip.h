#pragma once

#include "../core/TunerEar.h"
#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace orbitamp
{

/** The tuner as a STRIP: full width, always on screen, above the footer — the needle you glance
    at between phrases without opening anything. The note reads at the left, the cents ruler runs
    the width, the distance reads as a number at the right; green means in tune, silence shows a
    dash and a dim scale.

    It reads the same TunerEar every other needle reads, on its own repaint clock — the ear is
    pumped by the processor, so this is drawing only. */
class TunerStrip final : public juce::Component,
                         private juce::Timer
{
public:
    explicit TunerStrip (const core::TunerEar& tunerEar) : ear (tunerEar)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        startTimerHz (30);
    }

    /** A click opens the big tuner — the strip is the glance, the zoom is the look. */
    std::function<void()> onClick;

    void mouseDown (const juce::MouseEvent&) override
    {
        if (onClick != nullptr)
            onClick();
    }

    static constexpr int designHeight = 44;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();

        g.setColour (theme::panel);
        g.fillRoundedRectangle (r, theme::radiusMd);
        g.setColour (theme::hair2);
        g.drawRoundedRectangle (r.reduced (0.5f), theme::radiusMd, 1.0f);

        const bool live  = ear.live();
        const auto note  = ear.nearestNote();
        const bool green = ear.green();

        auto inner = r.reduced (14.0f, 6.0f);

        // ---- the note, left ----
        auto noteArea = inner.removeFromLeft (96.0f);
        if (live)
        {
            const auto name = juce::String (core::PitchTracker::noteName (note.midi));
            const auto font = theme::displayFont (24.0f);
            const float w   = theme::trackedWidth (name, font, 0.05f);

            g.setColour (green ? inTune : theme::tx);
            theme::drawTracked (g, name, noteArea.withWidth (w), font, 0.05f,
                                juce::Justification::centredLeft);

            g.setColour (theme::txDim);
            theme::drawTracked (g, juce::String (core::PitchTracker::noteOctave (note.midi)),
                                noteArea.withTrimmedLeft (w + 6.0f).withTrimmedTop (noteArea.getHeight() * 0.35f),
                                theme::displayFont (13.0f), 0.05f, juce::Justification::centredLeft);
        }
        else
        {
            g.setColour (theme::txFaint);
            g.fillRoundedRectangle (noteArea.getX(), noteArea.getCentreY() - 2.0f, 30.0f, 4.0f, 2.0f);
        }

        // ---- the cents, right ----
        auto centsArea = inner.removeFromRight (96.0f);
        if (live)
        {
            const auto cents = juce::String (juce::roundToInt (note.cents));
            g.setColour (theme::txDim);
            theme::drawTracked (g, (note.cents >= 0.5f ? "+" : "") + cents + " C", centsArea,
                                theme::displayFont (13.0f), 0.12f, juce::Justification::centredRight);
        }

        // ---- the ruler between them ----
        const auto ruler = inner.reduced (18.0f, 0.0f);
        const float midY = ruler.getCentreY();
        const auto centsX = [&ruler] (float cents)
        {
            return ruler.getX() + ruler.getWidth() * (juce::jlimit (-50.0f, 50.0f, cents) + 50.0f) / 100.0f;
        };

        for (int c = -50; c <= 50; c += 5)
        {
            const float h = c == 0 ? 24.0f : (c % 25 == 0 ? 14.0f : 8.0f);
            g.setColour (c == 0 ? theme::lilac.withAlpha (0.8f) : (c % 25 == 0 ? theme::hair2 : theme::hair));
            g.fillRect (juce::Rectangle<float> (centsX ((float) c) - 0.5f, midY - h * 0.5f,
                                                c == 0 ? 2.0f : 1.0f, h));
        }

        if (live)
        {
            const float x = centsX (ear.needle());
            g.setColour (green ? inTune : theme::tx);
            g.fillRoundedRectangle (x - 1.5f, midY - 15.0f, 3.0f, 30.0f, 1.5f);
        }
    }

private:
    void timerCallback() override { repaint(); }

    inline static const juce::Colour inTune { 0xff5fc97a };   // the character ramp's clean green

    const core::TunerEar& ear;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TunerStrip)
};

} // namespace orbitamp
