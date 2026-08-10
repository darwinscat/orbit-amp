#pragma once

#include "../Parameters.h"
#include "BlockFrame.h"
#include "EqSection.h"

namespace orbitamp
{

/** An EQ link's face. It exists twice — eq1 ahead of the boost, eq2 between boost and preamp —
    and deliberately as the SAME face: the controls are learned once, and which link you are on is
    said by the title and by where the thumb you clicked sat in the chain.

    Lives in the zoom: the strip carries the link's curve and switch, and opening the thumb lands
    here, where the knobs and the draggable curve get the whole panel. */
class EqBlock final : public BlockFrame
{
public:
    EqBlock (juce::AudioProcessorValueTreeState& s, int link)
        : BlockFrame ("EQ " + juce::String (link + 1), Kind::dsp), eq (s, link)
    {
        eq.addTo (*this);
        attachPower (*s.getParameter (params::eqOn (link)));
    }

private:
    void layOutContent (juce::Rectangle<int> area) override
    {
        // The curve is the point of the zoomed view — the lower half, never less than the height
        // it had when it lived inside the preamp.
        eq.layOutCurve (area.removeFromBottom (juce::jmax (EqSection::designHeight, area.getHeight() / 2)));
        area.removeFromBottom (gap);

        // The knob cluster does not need the whole width of a zoomed panel to say four values.
        eq.layOutKnobs (area.reduced (area.getWidth() / 6, 0));
    }

    static constexpr int gap = 12;

    EqSection eq;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqBlock)
};

} // namespace orbitamp
