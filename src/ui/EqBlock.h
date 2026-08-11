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
    here, where the section's whole console face gets the panel. */
class EqBlock final : public BlockFrame
{
public:
    EqBlock (juce::AudioProcessorValueTreeState& s, int link, const std::atomic<float>& outDb)
        : BlockFrame ("EQ " + juce::String (link + 1), Kind::dsp), eq (s, link, outDb)
    {
        eq.addTo (*this);
        attachPower (*s.getParameter (params::eqOn (link)));
    }

private:
    void layOutContent (juce::Rectangle<int> area) override
    {
        eq.layOut (area);
    }

    EqSection eq;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqBlock)
};

} // namespace orbitamp
