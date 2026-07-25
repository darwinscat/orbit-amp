#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace orbitamp
{

/** The plugin shell. Deliberately thin: the chain (boost -> preamp -> EQ -> reverb) lands in
    src/core/ behind a single engine, and this class only pumps buffers into it and owns state.
    Passthrough for now — the scaffold exists to be loaded in a host, not to make sound yet. */
class AmpProcessor final : public juce::AudioProcessor
{
public:
    AmpProcessor();
    ~AmpProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                          { return true; }

    const juce::String getName() const override              { return "OrbitAmp"; }
    bool acceptsMidi() const override                        { return false; }
    bool producesMidi() const override                       { return false; }
    bool isMidiEffect() const override                       { return false; }
    double getTailLengthSeconds() const override             { return 0.0; }

    int getNumPrograms() override                            { return 1; }
    int getCurrentProgram() override                         { return 0; }
    void setCurrentProgram (int) override                    {}
    const juce::String getProgramName (int) override         { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override   {}
    void setStateInformation (const void*, int) override     {}

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpProcessor)
};

} // namespace orbitamp
