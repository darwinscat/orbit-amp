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

    // The character IS the block's name: PLATE or HALL on the border where REVERB stood, set like
    // a name and in the block's colour. A quarter-width block has no room for both, and the
    // colour already says which kind of block this is.
    showTitle = false;
    {
        juce::Array<VoicingSelector::Entry> entries;
        for (const auto& name : params::reverbCharacters)
            entries.add ({ name, 0, false });
        character.setEntries (std::move (entries));
    }
    character.fontHeight = 16.0f;
    character.tracking   = 0.15f;
    character.boxed      = false;
    character.tint       = theme::lilac;
    mix.textForValue = [] (double v) { return juce::String (juce::roundToInt (v)) + "%"; };

    characterAttachment = std::make_unique<juce::ParameterAttachment> (
        *state.getParameter (params::reverbType),
        [this] (float v)
        {
            character.setSelection (juce::roundToInt (v));
            refreshTail();
            resized();   // the name on the border is as wide as the name
        });

    character.onPick = [this] (int i) { characterAttachment->setValueAsCompleteGesture ((float) i); };

    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        state, params::reverbMix, mix);

    characterAttachment->sendInitialUpdate();
}

ReverbBlock::~ReverbBlock() = default;

void ReverbBlock::refreshTail()
{
    const int index = juce::jlimit (0, params::reverbCharacters.size() - 1, character.getSelection());

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
    // The character on the border where the name would stand: at the left, sized to itself.
    {
        const auto slot = borderSlotArea();
        character.setBounds (slot.withWidth (juce::jmin (slot.getWidth(), character.idealWidth())));
        borderSlotUsed = character.getBounds();
    }

    // The dial on the left, the decay beside it taking the rest — a picture of time passing, as
    // wide as the box lets it be, and the dial as tall as the box.
    const int side = juce::jmin (maxKnobSide, juce::jmin (area.getWidth() / 2, area.getHeight()));
    mix.setBounds (area.removeFromLeft (side).withSizeKeepingCentre (side, side));
    area.removeFromLeft (knobGap);

    tail.setBounds (area);
}

} // namespace orbitamp
