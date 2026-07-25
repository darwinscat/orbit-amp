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

    // Tone. Corner frequencies are the stack's, not the user's — an amp tone control is three knobs.
    const juce::NormalisableRange<float> tone { -toneRangeDb, toneRangeDb, 0.1f };
    layout.add (std::make_unique<Float> (juce::ParameterID { eqLow,  1 }, "Low",  tone, 0.0f),
                std::make_unique<Float> (juce::ParameterID { eqMid,  1 }, "Mid",  tone, 0.0f),
                std::make_unique<Float> (juce::ParameterID { eqHigh, 1 }, "High", tone, 0.0f));

    // The cuts are off by default: they are for tightening a specific rig, not part of the voicing.
    // Skewed ranges so the useful end of each sweep gets the middle of the travel.
    auto hz = [] (float lo, float hi, float centre)
    {
        return juce::NormalisableRange<float> (lo, hi, 0.1f, std::log (0.5f) / std::log ((centre - lo) / (hi - lo)));
    };

    layout.add (std::make_unique<Bool>  (juce::ParameterID { eqHpfOn, 1 }, "HPF", false),
                std::make_unique<Float> (juce::ParameterID { eqHpfHz, 1 }, "HPF Freq", hz (20.0f, 500.0f, 80.0f), 80.0f),
                std::make_unique<Bool>  (juce::ParameterID { eqLpfOn, 1 }, "LPF", false),
                std::make_unique<Float> (juce::ParameterID { eqLpfHz, 1 }, "LPF Freq", hz (2000.0f, 20000.0f, 10000.0f), 10000.0f));

    return layout;
}

} // namespace orbitamp::params
