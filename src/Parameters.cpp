#include "Parameters.h"

namespace orbitamp::params
{

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using Bool   = juce::AudioParameterBool;
    using Choice = juce::AudioParameterChoice;
    using Float  = juce::AudioParameterFloat;
    using Int    = juce::AudioParameterInt;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Block power. A boost is an addition to the sound, so it starts off; the rest are the sound.
    layout.add (std::make_unique<Bool> (juce::ParameterID { boostOn,  1 }, "Boost",  false),
                std::make_unique<Bool> (juce::ParameterID { preampOn, 1 }, "Preamp", true),
                std::make_unique<Bool> (juce::ParameterID { eqOn,     1 }, "EQ",     true),
                std::make_unique<Bool> (juce::ParameterID { reverbOn, 1 }, "Reverb", true));

    layout.add (std::make_unique<Choice> (juce::ParameterID { preampType, 1 }, "Type", typeNames, 0),
                std::make_unique<Int>    (juce::ParameterID { preampVoice, 1 }, "Voice", 0, maxVoicesPerType - 1, 0));

    // The hero. 0-10 is the amp-panel scale, not a dB value — it maps onto the captured detents, so
    // the number on the face is the number the capture was taken at.
    layout.add (std::make_unique<Float> (juce::ParameterID { preampGain, 1 }, "Gain",
                                         juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), 5.0f));

    return layout;
}

} // namespace orbitamp::params
