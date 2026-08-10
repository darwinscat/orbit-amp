#pragma once

#include "../core/TunerEar.h"
#include "BlockFrame.h"

namespace orbitamp
{

/** The tuner, zoomed: the strip's needle at reading size. A note letter with its octave, the cents
    ruler with the needle, the numbers underneath; green means play the next string.

    It has no switch, because there is nothing to switch — the tuner does nothing to the signal, it
    only listens to the raw input. All the listening lives in the processor's TunerEar; this face
    only draws it, on its own repaint clock, running only while the block is actually on screen. */
class TunerBlock final : public BlockFrame,
                         private juce::Timer
{
public:
    explicit TunerBlock (const core::TunerEar& tunerEar)
        : BlockFrame ("Tuner", Kind::dsp, false), ear (tunerEar)
    {
    }

private:
    void visibilityChanged() override
    {
        if (isVisible())
            startTimerHz (30);
        else
            stopTimer();
    }

    void timerCallback() override { repaint(); }

    void paintContent (juce::Graphics& g) override
    {
        auto r = contentArea();

        const bool live  = ear.live();
        const auto note  = ear.nearestNote();
        const bool green = ear.green();

        // ---- the note: one big letter, its octave small beside it ----
        auto noteArea = r.removeFromTop (r.getHeight() / 2).toFloat();
        if (live)
        {
            const auto name   = juce::String (core::PitchTracker::noteName (note.midi));
            const auto octave = juce::String (core::PitchTracker::noteOctave (note.midi));
            const auto font   = theme::displayFont (juce::jmin (96.0f, noteArea.getHeight() * 0.8f));

            const float w = theme::trackedWidth (name, font, 0.05f);
            g.setColour (green ? inTune : theme::tx);
            theme::drawTracked (g, name,
                                noteArea.withWidth (w).withX (noteArea.getCentreX() - w * 0.5f - 10.0f),
                                font, 0.05f, juce::Justification::centred);

            g.setColour (theme::txDim);
            theme::drawTracked (g, octave,
                                noteArea.withWidth (26.0f).withX (noteArea.getCentreX() + w * 0.5f)
                                        .withTrimmedTop (noteArea.getHeight() * 0.5f),
                                theme::displayFont (22.0f), 0.05f, juce::Justification::centredLeft);
        }
        else
        {
            // Not a glyph — the display face owes us no dash, and a missing one reads as garbage.
            g.setColour (theme::txFaint);
            g.fillRoundedRectangle (noteArea.getCentreX() - 20.0f, noteArea.getCentreY() - 2.5f,
                                    40.0f, 5.0f, 2.5f);
        }

        // ---- the ruler: -50..+50 cents, the centre tick tallest ----
        const auto ruler = r.removeFromTop (64).reduced (r.getWidth() / 6, 0).toFloat();
        const float mid  = ruler.getY() + 40.0f;

        for (int c = -50; c <= 50; c += 5)
        {
            const float x = ruler.getX() + ruler.getWidth() * (float) (c + 50) / 100.0f;
            const float h = c == 0 ? 24.0f : (c % 10 == 0 ? 14.0f : 8.0f);
            g.setColour (c == 0 ? theme::hair2 : theme::hair);
            g.fillRect (juce::Rectangle<float> (x - 0.5f, mid - h, 1.0f, h));
        }

        if (live)
        {
            const float x = ruler.getX()
                          + ruler.getWidth() * (juce::jlimit (-50.0f, 50.0f, ear.needle()) + 50.0f) / 100.0f;
            g.setColour (green ? inTune : theme::tx);
            g.fillRoundedRectangle (x - 1.5f, mid - 32.0f, 3.0f, 40.0f, 1.5f);
        }

        // ---- the numbers, for whoever wants them ----
        if (live)
        {
            const auto cents = juce::String (juce::roundToInt (note.cents));
            g.setColour (theme::txDim);
            theme::drawTracked (g,
                                juce::String (ear.hz(), 1) + " HZ   "
                                    + (note.cents >= 0.5f ? "+" : "") + cents + " C",
                                r.removeFromTop (26).toFloat(), theme::displayFont (12.0f), 0.15f,
                                juce::Justification::centred);
        }
    }

    inline static const juce::Colour inTune { 0xff5fc97a };   // the character ramp's clean green

    const core::TunerEar& ear;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TunerBlock)
};

} // namespace orbitamp
