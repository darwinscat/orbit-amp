#pragma once

#include "BlockFrame.h"
#include "Knob.h"
#include "Selector.h"

namespace orbitamp
{

/** The preamp — the captured voicing, and the hero of the faceplate.

    You pick a TYPE (clean through modern) and then a VOICE within it; the addressable unit is the
    voicing, not a channel. GAIN is the big one: 0-10 on the amp-panel scale, mapping onto the
    detents the profiles were captured at. */
class PreampBlock final : public BlockFrame
{
public:
    explicit PreampBlock (juce::AudioProcessorValueTreeState&);
    ~PreampBlock() override;

private:
    void layOutContent (juce::Rectangle<int>) override;

    /** Repopulates the voice list for the current type and pulls the stored index into range — a
        saved session may name a voice this type no longer offers. */
    void refreshVoices (int typeIndex);

    static constexpr int combosHeight = 26;
    static constexpr int typeWidth    = 76;
    static constexpr int combosGap    = 7;
    static constexpr int knobGap      = 10;

    juce::AudioProcessorValueTreeState& state;

    Selector type  { theme::orange, false };   // the five types are a short, fixed list — no stepping
    Selector voice { theme::orange, true };    // stepping through voices is the audition gesture
    Knob     gain  { "Gain", theme::orange };

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment;
    std::unique_ptr<juce::ParameterAttachment> typeAttachment, voiceAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PreampBlock)
};

} // namespace orbitamp
