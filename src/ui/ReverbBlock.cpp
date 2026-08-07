#include "ReverbBlock.h"

#include "../Parameters.h"

namespace orbitamp
{

ReverbBlock::ReverbBlock (juce::AudioProcessorValueTreeState& s)
    : BlockFrame ("Reverb", BlockFrame::Kind::dsp), state (s)
{
    addAndMakeVisible (character);
    addAndMakeVisible (mix);
    addAndMakeVisible (tail);

    display.prepare (displayRate);
    mix.onValueChange = [this] { refreshTail(); };

    attachPower (*state.getParameter (params::reverbOn));

    character.setItems (params::reverbCharacters, 0);
    mix.textForValue = [] (double v) { return juce::String (juce::roundToInt (v)) + "%"; };

    characterAttachment = std::make_unique<juce::ParameterAttachment> (
        *state.getParameter (params::reverbType),
        [this] (float v)
        {
            character.setSelectedIndex (juce::roundToInt (v), juce::dontSendNotification);
            refreshTail();
        });

    character.onChange = [this] (int i) { characterAttachment->setValueAsCompleteGesture ((float) i); };

    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state, params::reverbMix, mix);

    characterAttachment->sendInitialUpdate();
}

ReverbBlock::~ReverbBlock() = default;

void ReverbBlock::refreshTail()
{
    const int index = juce::jlimit (0, params::reverbCharacters.size() - 1, character.getSelectedIndex());

    display.setCharacter (static_cast<core::ReverbStage::Character> (index));
    display.setMix ((float) mix.getValue() * 0.01f);
    display.reset();

    // juce::Reverb ramps its gains over ~10 ms, so a fresh setting has to be run in before the
    // impulse — otherwise the tail is measured mid-ramp and the picture lags the sound.
    const int n = (int) (displayRate * tailSeconds);
    std::vector<float> left ((size_t) n, 0.0f), right ((size_t) n, 0.0f);
    float* channels[2] = { left.data(), right.data() };

    display.process (channels, 2, juce::jmin (n, (int) (displayRate * 0.05)));

    std::fill (left.begin(), left.end(), 0.0f);
    std::fill (right.begin(), right.end(), 0.0f);
    left[0] = right[0] = 1.0f;
    display.process (channels, 2, n);

    // Peak per bucket, in dB — the shape of the decay, not its every wiggle.
    std::vector<float> env ((size_t) tailBuckets, -60.0f);
    const int per = juce::jmax (1, n / tailBuckets);

    for (int b = 0; b < tailBuckets; ++b)
    {
        float peak = 0.0f;
        for (int i = b * per; i < juce::jmin (n, (b + 1) * per); ++i)
            peak = juce::jmax (peak, std::abs (left[(size_t) i]));

        env[(size_t) b] = peak > 1.0e-5f ? juce::Decibels::gainToDecibels (peak) : -60.0f;
    }

    tail.setEnvelope (std::move (env), tailSeconds);
}

void ReverbBlock::layOutContent (juce::Rectangle<int> area)
{
    // Lower half is the illustration, as in the other blocks — here, the measured decay.
    tail.setBounds (area.removeFromBottom (tailHeight));
    area.removeFromBottom (tailGap);

    character.setBounds (area.removeFromTop (combosHeight));
    area.removeFromTop (knobGap);

    const int side = juce::jmin (area.getWidth(), area.getHeight());
    mix.setBounds (area.withSizeKeepingCentre (side, side));
}

} // namespace orbitamp
