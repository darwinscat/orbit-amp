#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

// THREE STAND-INS FOR THE AMPS, so the wrappers that hold several plug-ins are proven before an amp rides
// on them: each has one parameter, keeps it in its state, shows it in an editor — and plays a different
// sum, its default unlike the others', so one block of audio says which of them played it. Fed a signal
// that flips sign every sample, x = +a, -a, +a, ...:
//
//     OrbitAmp   gain 0.5            y = 0.5 x
//     CatpitAmp  tilt 0.5            y = x + 0.5 (x - previous x) / 2 = 1.5 x    (a first difference: highs
//                                    up; on the first sample after a prepare the previous x is 0, so 1.25 x)
//     RedPawAmp  phase flip, on      y = -x
//
// MultiwrapHost.cpp checks exactly those ratios, through VST3 and through CLAP.
namespace multiwrap
{

class ToyProcessor : public juce::AudioProcessor
{
public:
    ToyProcessor (juce::String nameIn, juce::RangedAudioParameter* parameterIn)
        : juce::AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                                 .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          name (std::move (nameIn)),
          parameter (parameterIn)
    {
        addParameter (parameter);
    }

    const juce::String getName() const override             { return name; }

    void prepareToPlay (double, int) override               { previous[0] = previous[1] = 0.0f; }
    void releaseResources() override                        {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
            && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const auto value = parameter->convertFrom0to1 (parameter->getValue());

        for (int channel = 0; channel < juce::jmin (2, buffer.getNumChannels()); ++channel)
        {
            auto* samples = buffer.getWritePointer (channel);

            for (int i = 0; i < buffer.getNumSamples(); ++i)
                samples[i] = play (samples[i], value, previous[channel]);
        }
    }

    bool hasEditor() const override                         { return true; }
    juce::AudioProcessorEditor* createEditor() override     { return new juce::GenericAudioProcessorEditor (*this); }

    double getTailLengthSeconds() const override            { return 0.0; }
    bool acceptsMidi() const override                       { return false; }
    bool producesMidi() const override                      { return false; }

    int getNumPrograms() override                           { return 1; }
    int getCurrentProgram() override                        { return 0; }
    void setCurrentProgram (int) override                   {}
    const juce::String getProgramName (int) override        { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    // The state names the plug-in it belongs to, and another plug-in's state is turned away: three
    // processors behind one binary must never take each other's.
    void getStateInformation (juce::MemoryBlock& data) override
    {
        juce::XmlElement state ("MultiwrapToy");
        state.setAttribute ("plugin", name);
        state.setAttribute ("value", (double) parameter->convertFrom0to1 (parameter->getValue()));
        copyXmlToBinary (state, data);
    }

    void setStateInformation (const void* data, int size) override
    {
        if (auto state = getXmlFromBinary (data, size); state != nullptr && state->getStringAttribute ("plugin") == name)
            parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) state->getDoubleAttribute ("value")));
    }

protected:
    virtual float play (float x, float value, float& previousX) const = 0;

private:
    const juce::String name;
    juce::RangedAudioParameter* const parameter;
    float previous[2] {};
};

struct Gain final : ToyProcessor
{
    Gain() : ToyProcessor ("OrbitAmp", new juce::AudioParameterFloat ({ "gain", 1 }, "Gain", 0.0f, 2.0f, 0.5f)) {}
    float play (float x, float gain, float&) const override { return gain * x; }
};

struct Tilt final : ToyProcessor
{
    Tilt() : ToyProcessor ("CatpitAmp", new juce::AudioParameterFloat ({ "tilt", 1 }, "Tilt", -1.0f, 1.0f, 0.5f)) {}

    float play (float x, float tilt, float& previousX) const override
    {
        const auto y = x + tilt * (x - previousX) * 0.5f;
        previousX = x;
        return y;
    }
};

struct PhaseFlip final : ToyProcessor
{
    PhaseFlip() : ToyProcessor ("RedPawAmp", new juce::AudioParameterBool ({ "flip", 1 }, "Phase flip", true)) {}
    float play (float x, float flip, float&) const override { return flip > 0.5f ? -x : x; }
};

} // namespace multiwrap
