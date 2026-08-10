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

    /** The tuner's own rule: it has no controls, so a click ANYWHERE on the open lens puts it
        away — you looked, you are done. */
    std::function<void()> onDismiss;

    void mouseDown (const juce::MouseEvent&) override
    {
        if (onDismiss != nullptr)
            onDismiss();
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

        // ---- the numbers along the bottom, at a size worth the panel ----
        const auto numbers = r.removeFromBottom (44).toFloat();
        if (live)
        {
            const auto cents = juce::String (juce::roundToInt (note.cents));
            g.setColour (theme::txDim);
            theme::drawTracked (g,
                                juce::String (ear.hz(), 1) + " HZ    "
                                    + (note.cents >= 0.5f ? "+" : "") + cents + " C",
                                numbers, theme::displayFont (18.0f), 0.15f,
                                juce::Justification::centred);
        }

        // ---- the note: the golden share of what is left ----
        auto noteArea = r.removeFromTop (juce::roundToInt ((float) r.getHeight() / 1.618f)).toFloat();
        if (live)
        {
            const auto name   = juce::String (core::PitchTracker::noteName (note.midi));
            const auto octave = juce::String (core::PitchTracker::noteOctave (note.midi));
            const auto font   = theme::displayFont (juce::jmin (150.0f, noteArea.getHeight() * 0.7f));

            // A soft radial bloom behind the letter — green when home, violet on the way.
            {
                const auto glow = (green ? inTune : theme::violet).withAlpha (green ? 0.28f : 0.16f);
                const float rad = noteArea.getHeight() * 0.75f;

                juce::ColourGradient bloom (glow, noteArea.getCentreX(), noteArea.getCentreY(),
                                            glow.withAlpha (0.0f),
                                            noteArea.getCentreX(), noteArea.getCentreY() - rad, true);
                g.setGradientFill (bloom);
                g.fillEllipse (noteArea.getCentreX() - rad, noteArea.getCentreY() - rad,
                               rad * 2.0f, rad * 2.0f);
            }

            const float w = theme::trackedWidth (name, font, 0.05f);
            g.setColour (green ? inTune : theme::tx);
            theme::drawTracked (g, name,
                                noteArea.withWidth (w).withX (noteArea.getCentreX() - w * 0.5f - 16.0f),
                                font, 0.05f, juce::Justification::centred);

            g.setColour (theme::txDim);
            theme::drawTracked (g, octave,
                                noteArea.withWidth (48.0f).withX (noteArea.getCentreX() + w * 0.5f)
                                        .withTrimmedTop (noteArea.getHeight() * 0.5f),
                                theme::displayFont (40.0f), 0.05f, juce::Justification::centredLeft);
        }
        else
        {
            // Not a glyph — the display face owes us no dash, and a missing one reads as garbage.
            g.setColour (theme::txFaint);
            g.fillRoundedRectangle (noteArea.getCentreX() - 28.0f, noteArea.getCentreY() - 3.5f,
                                    56.0f, 7.0f, 3.5f);
        }

        // ---- the ruler: -50..+50 cents across the whole face, labelled like an instrument ----
        const auto ruler    = r.reduced (24, 6).toFloat();
        const float baseY   = ruler.getBottom() - 22.0f;   // the tick baseline; labels sit under it
        const auto  centsX  = [&ruler] (float cents)
        {
            return ruler.getX() + ruler.getWidth() * (juce::jlimit (-50.0f, 50.0f, cents) + 50.0f) / 100.0f;
        };

        // The scale wears its meaning: orange warmth burning in from the out-of-tune edges, a
        // green heart at zero.
        {
            const float bandH = ruler.getHeight() - 26.0f;
            const auto  band  = juce::Rectangle<float> (ruler.getX(), baseY - bandH, ruler.getWidth(), bandH);

            juce::ColourGradient heat (theme::orange.withAlpha (0.20f), band.getX(), band.getCentreY(),
                                       theme::orange.withAlpha (0.20f), band.getRight(), band.getCentreY(), false);
            heat.addColour (0.35, theme::orange.withAlpha (0.0f));
            heat.addColour (0.65, theme::orange.withAlpha (0.0f));
            g.setGradientFill (heat);
            g.fillRect (band);

            const float cx = centsX (0.0f);
            juce::ColourGradient heart (inTune.withAlpha (0.22f), cx, band.getCentreY(),
                                        inTune.withAlpha (0.0f), cx + ruler.getWidth() * 0.07f,
                                        band.getCentreY(), true);
            g.setGradientFill (heart);
            g.fillRect (band);
        }

        for (int c = -50; c <= 50; c += 5)
        {
            const float x = centsX ((float) c);
            const float h = c == 0 ? ruler.getHeight() - 30.0f : (c % 25 == 0 ? 44.0f : 24.0f);

            g.setColour (c == 0 ? theme::lilac
                                : (c % 25 == 0 ? theme::orange.withAlpha (0.6f)
                                               : theme::violet.withAlpha (0.35f)));
            g.fillRect (juce::Rectangle<float> (x - (c == 0 ? 1.0f : 0.5f), baseY - h,
                                                c == 0 ? 2.0f : 1.0f, h));
        }

        // The zero mark glows — it is the destination.
        g.setColour (theme::lilac.withAlpha (0.22f));
        g.fillRoundedRectangle (centsX (0.0f) - 3.0f, baseY - (ruler.getHeight() - 30.0f), 6.0f,
                                ruler.getHeight() - 30.0f, 3.0f);

        for (int c = -50; c <= 50; c += 25)
        {
            g.setColour (c == 0 ? theme::txDim
                                : theme::orange.withAlpha (juce::jmax (0.35f, (float) std::abs (c) / 50.0f * 0.7f)));
            theme::drawTracked (g, (c > 0 ? "+" : "") + juce::String (c),
                                juce::Rectangle<float> (centsX ((float) c) - 30.0f, baseY + 6.0f, 60.0f, 14.0f),
                                theme::displayFont (12.0f), 0.10f, juce::Justification::centred);
        }

        if (live)
        {
            // The needle says HOW off with its colour: green at home, orange by 25 cents out —
            // and glows, because it is the one moving thing on the face.
            const float x = centsX (ear.needle());
            const float top = baseY - (ruler.getHeight() - 18.0f);
            const float len = ruler.getHeight() - 12.0f;
            const auto  needle = green ? inTune
                                       : inTune.interpolatedWith (theme::orange,
                                             juce::jlimit (0.0f, 1.0f, std::abs (ear.needle()) / 25.0f));

            g.setColour (needle.withAlpha (0.16f));
            g.fillRoundedRectangle (x - 7.0f, top - 2.0f, 14.0f, len + 4.0f, 7.0f);
            g.setColour (needle.withAlpha (0.4f));
            g.fillRoundedRectangle (x - 3.5f, top - 1.0f, 7.0f, len + 2.0f, 3.5f);
            g.setColour (needle);
            g.fillRoundedRectangle (x - 2.0f, top, 4.0f, len, 2.0f);
        }
    }

    inline static const juce::Colour inTune { 0xff5fc97a };   // the character ramp's clean green

    const core::TunerEar& ear;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TunerBlock)
};

} // namespace orbitamp
