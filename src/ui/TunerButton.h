#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace orbitamp
{

/** The toolbar's tuning fork — IconButton's language (a stroked path in a 24×24 box, the same
    hover wash), local because appkit's Kind list doesn't have it yet. Built here first; it moves
    there as a Kind once it has earned its place, and this file goes with it. */
class TunerButton final : public juce::Button
{
public:
    TunerButton() : juce::Button ("tuner")
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    juce::Colour colour      { 0xffc0c0c8 };
    juce::Colour panelColour { 0xff1b1b1f };

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto r = getLocalBounds().toFloat();

        if (over || down)
        {
            g.setColour (juce::Colour (over ? 0x1effffff : 0x14ffffff));
            g.fillRoundedRectangle (r.reduced (1.0f), 4.0f);
        }

        const float pad = juce::jmin (r.getWidth(), r.getHeight()) * 0.20f;
        const auto  t   = juce::RectanglePlacement (juce::RectanglePlacement::centred)
                            .getTransformToFit (juce::Rectangle<float> (0.0f, 0.0f, 24.0f, 24.0f),
                                                r.reduced (pad));

        // Two prongs into a U, a stem, a small foot.
        juce::Path p;
        p.startNewSubPath (8.5f, 3.0f);  p.lineTo (8.5f, 10.0f);
        p.quadraticTo (8.5f, 13.5f, 12.0f, 13.5f);
        p.quadraticTo (15.5f, 13.5f, 15.5f, 10.0f);
        p.lineTo (15.5f, 3.0f);
        p.startNewSubPath (12.0f, 13.5f); p.lineTo (12.0f, 20.0f);
        p.startNewSubPath (9.5f, 21.0f);  p.lineTo (14.5f, 21.0f);

        g.setColour (colour.withMultipliedAlpha (isEnabled() ? (over ? 1.0f : 0.82f) : 0.35f));
        g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded), t);
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TunerButton)
};

} // namespace orbitamp
