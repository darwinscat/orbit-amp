#pragma once

#include "Theme.h"

namespace orbitamp
{

/** A faceplate knob. The value is drawn ON the face — no separate readout box — because the number
    is the amp-panel setting, not a parameter dump: the knob answers "where is it set" at a glance.

    A juce::Slider underneath, so host automation, keyboard, and APVTS attachments come for free;
    only the drawing is ours. Double-click returns to the parameter's default. */
class Knob : public juce::Slider
{
public:
    Knob (juce::String knobLabel, juce::Colour knobAccent, int notchCount = 11)
        : label (std::move (knobLabel)), accent (knobAccent), notches (notchCount)
    {
        setSliderStyle (juce::Slider::RotaryVerticalDrag);
        setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        setRotaryParameters (startAngle, endAngle, true);
        setVelocityBasedMode (false);
    }

    /** How the number on the face is written. Defaults to one decimal — the 0-10 amp scale. */
    std::function<juce::String (double)> textForValue = [] (double v) { return juce::String (v, 1); };

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();

        g.setColour (theme::txDim);
        theme::drawTracked (g, label.toUpperCase(), r.removeFromTop (labelHeight).toFloat(),
                            theme::displayFont (8.0f), 0.15f, juce::Justification::centred);

        const float d = (float) juce::jmin (r.getWidth(), r.getHeight());
        const juce::Point<float> c { (float) r.getCentreX(), (float) r.getCentreY() };
        const float ringR = d * 0.42f;
        const float bodyR = d * 0.33f;

        const float pos   = (float) valueToProportionOfLength (getValue());
        const float angle = startAngle + pos * (endAngle - startAngle);

        // Notches first, outside everything — the detent marks that make 0-10 readable without a dial.
        if (notches > 1)
        {
            for (int i = 0; i < notches; ++i)
            {
                const float a  = startAngle + (endAngle - startAngle) * (float) i / (float) (notches - 1);
                const float t0 = ringR + d * 0.045f;
                const float t1 = t0 + d * 0.05f;
                const juce::Point<float> p0 { c.x + std::sin (a) * t0, c.y - std::cos (a) * t0 };
                const juce::Point<float> p1 { c.x + std::sin (a) * t1, c.y - std::cos (a) * t1 };

                g.setColour (a <= angle + 1.0e-4f ? accent.withAlpha (0.75f) : theme::hair2);
                g.drawLine ({ p0, p1 }, d * 0.018f);
            }
        }

        const float arcW = d * 0.055f;

        juce::Path track;
        track.addCentredArc (c.x, c.y, ringR, ringR, 0.0f, startAngle, endAngle, true);
        g.setColour (theme::hair2);
        g.strokePath (track, juce::PathStrokeType (arcW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (angle > startAngle)
        {
            juce::Path filled;
            filled.addCentredArc (c.x, c.y, ringR, ringR, 0.0f, startAngle, angle, true);
            g.setColour (accent);
            g.strokePath (filled, juce::PathStrokeType (arcW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // The body: a dark disc lit from above, so the face reads as a physical cap.
        juce::ColourGradient body (juce::Colour (0xff23222d), c.x, c.y - bodyR,
                                   juce::Colour (0xff0e0e14), c.x, c.y + bodyR, false);
        g.setGradientFill (body);
        g.fillEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
        g.setColour (theme::hair2);
        g.drawEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);

        // Pointer.
        const juce::Point<float> pa { c.x + std::sin (angle) * bodyR * 0.55f, c.y - std::cos (angle) * bodyR * 0.55f };
        const juce::Point<float> pb { c.x + std::sin (angle) * bodyR * 0.94f, c.y - std::cos (angle) * bodyR * 0.94f };
        g.setColour (accent.brighter (0.3f));
        g.drawLine ({ pa, pb }, d * 0.035f);

        g.setColour (juce::Colour (0xfff6f4fd));
        const auto valueFont = theme::displayFont (d * 0.19f);
        const auto text = textForValue (getValue());
        theme::drawTracked (g, text, juce::Rectangle<float> (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f),
                            valueFont, 0.01f, juce::Justification::centred);
    }

private:
    static constexpr int   labelHeight = 11;
    static constexpr float startAngle  = juce::MathConstants<float>::pi * 1.25f;
    static constexpr float endAngle    = juce::MathConstants<float>::pi * 2.75f;

    juce::String label;
    juce::Colour accent;
    int notches;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
};

} // namespace orbitamp
