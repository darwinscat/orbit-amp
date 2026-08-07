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

    // Boost. The gain knob is stepped because it selects a capture, not a value — between the
    // captured positions there is no data. Tone is measured, so it is continuous.
    layout.add (std::make_unique<Float> (juce::ParameterID { boostGain, 1 }, "Boost Gain",
                                         juce::NormalisableRange<float> (0.0f, 10.0f, 0.5f), 5.0f),
                std::make_unique<Float> (juce::ParameterID { boostTone, 1 }, "Boost Tone",
                                         juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), 10.0f));

    layout.add (std::make_unique<Choice> (juce::ParameterID { boostType,  1 }, "Boost Type", typeNames, 0),
                std::make_unique<Int>    (juce::ParameterID { boostVoice, 1 }, "Boost Voice",
                                          0, maxVoicesPerType - 1, 0));

    layout.add (std::make_unique<Choice> (juce::ParameterID { preampType, 1 }, "Type", typeNames, 0),
                std::make_unique<Int>    (juce::ParameterID { preampVoice, 1 }, "Voice", 0, maxVoicesPerType - 1, 0));

    // The hero. 0-10 is the amp-panel scale, not a dB value — it maps onto the captured detents, so
    // the number on the face is the number the capture was taken at.
    layout.add (std::make_unique<Float> (juce::ParameterID { preampGain, 1 }, "Gain",
                                         juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), 5.0f));

    // Tone. Corner frequencies are the stack's, not the user's — an amp tone control is three knobs.
    const juce::NormalisableRange<float> tone { -toneRangeDb, toneRangeDb, 0.1f };
    layout.add (std::make_unique<Float> (juce::ParameterID { eqLow,      1 }, "Low",      tone, 0.0f),
                std::make_unique<Float> (juce::ParameterID { eqMid,      1 }, "Mid",      tone, 0.0f),
                std::make_unique<Float> (juce::ParameterID { eqHigh,     1 }, "High",     tone, 0.0f),
                std::make_unique<Float> (juce::ParameterID { eqPresence, 1 }, "Presence", tone, 0.0f));

    // Frequencies are skewed so the useful end of each sweep gets the middle of the travel.
    auto hz = [] (float lo, float hi, float centre)
    {
        return juce::NormalisableRange<float> (lo, hi, 0.1f, std::log (0.5f) / std::log ((centre - lo) / (hi - lo)));
    };

    // The bell is the one band whose centre moves — the shelves stay where the stack puts them.
    // 200 Hz - 2 kHz is the span the measured devices sat in (734, 775, 912).
    layout.add (std::make_unique<Float> (juce::ParameterID { eqMidHz, 1 }, "Mid Freq",
                                         hz (200.0f, 2000.0f, 600.0f), 600.0f));

    // The cuts are off by default: they are for tightening a specific rig, not part of the voicing.
    layout.add (std::make_unique<Bool>  (juce::ParameterID { eqHpfOn, 1 }, "HPF", false),
                std::make_unique<Float> (juce::ParameterID { eqHpfHz, 1 }, "HPF Freq", hz (20.0f, 500.0f, 80.0f), 80.0f),
                std::make_unique<Bool>  (juce::ParameterID { eqLpfOn, 1 }, "LPF", false),
                std::make_unique<Float> (juce::ParameterID { eqLpfHz, 1 }, "LPF Freq", hz (2000.0f, 20000.0f, 10000.0f), 10000.0f));

    // The cabinet. Position and distance are the two halves of one gesture on the grid; distance is
    // in centimetres, matching the grid's own ladder.
    layout.add (std::make_unique<Bool> (juce::ParameterID { cabOn, 1 }, "Cabinet", true));

    for (int i = 0; i < cabNumMics; ++i)
    {
        const auto n = juce::String (i + 1);

        // Mic 1 on, mic 2 off: one mic is the normal case and the second is the move you make on
        // purpose. They start apart — close on the cap edge, further out on the cone — so switching
        // the second on gives a blend rather than two mics in the same hole.
        layout.add (std::make_unique<Bool>   (juce::ParameterID { cabMicOn (i),   1 }, "Mic " + n,          i == 0),
                    std::make_unique<Choice> (juce::ParameterID { cabMicType (i), 1 }, "Mic " + n + " Type", cabMics, i),
                    std::make_unique<Choice> (juce::ParameterID { cabMicPos (i),  1 }, "Mic " + n + " Position",
                                              cabPositions, i == 0 ? 2 : 1),
                    std::make_unique<Float>  (juce::ParameterID { cabMicDist (i), 1 }, "Mic " + n + " Distance",
                                              juce::NormalisableRange<float> (0.0f, 15.0f, 0.1f), i == 0 ? 2.0f : 5.0f),
                    std::make_unique<Choice> (juce::ParameterID { cabMicAngle (i), 1 }, "Mic " + n + " Angle",
                                              cabAngles, 1));
    }

    // The power amp is off by default: it is most useful for leads and least for tight rhythm, so
    // it should be something you reach for rather than something you find already on.
    layout.add (std::make_unique<Bool>   (juce::ParameterID { powerOn,   1 }, "Power Amp", false),
                std::make_unique<Choice> (juce::ParameterID { powerType, 1 }, "Power Amp Type", powerTypes, 0));

    // Two knobs out of the stage's ten controls. The rest is what a power amp IS rather than what
    // you set on it, and belongs to the model. Continuous, not stepped: this is our own DSP, so
    // every position between two others is a real one — unlike a captured gain.
    layout.add (std::make_unique<Float> (juce::ParameterID { powerDrive, 1 }, "Power Drive",
                                         juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), 5.0f),
                std::make_unique<Float> (juce::ParameterID { powerSag, 1 }, "Power Sag",
                                         juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), 3.0f));

    // Which bottle and how many. Both are the amp rather than a setting on it, which is why they sit
    // with the model's name and not among the knobs.
    layout.add (std::make_unique<Choice> (juce::ParameterID { powerTube,  1 }, "Power Tube", powerTubes, 2),
                std::make_unique<Choice> (juce::ParameterID { powerCount, 1 }, "Power Tubes", powerCounts, 1));

    layout.add (std::make_unique<Choice> (juce::ParameterID { reverbType, 1 }, "Reverb", reverbCharacters, 0),
                std::make_unique<Float>  (juce::ParameterID { reverbMix, 1 }, "Mix",
                                          juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 20.0f));

    return layout;
}

} // namespace orbitamp::params
