#pragma once

#include "core/ToneStack.h"

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

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    /** The editor's zoom, 50-200%. It lives here rather than in the editor so it survives closing
        and reopening the window; it is message-thread only and never read by the audio path.
        Persisting it across sessions comes with the state work. */
    float getEditorScale() const noexcept { return editorScale; }
    void setEditorScale (float s) noexcept { editorScale = juce::jlimit (minScale, maxScale, s); }

    static constexpr float minScale = 0.5f;
    static constexpr float maxScale = 2.0f;

private:
    /** Reads the tone parameters into the stack's settings. Called per block from the audio thread;
        the stack only redesigns when something actually moved. */
    void updateToneSettings() noexcept;

    float editorScale = 1.0f;

    core::ToneStack tone;

    // Cached atomic parameter pointers — getRawParameterValue does a map lookup, which the audio
    // thread should not be doing per block.
    std::atomic<float>* eqOnParam    = nullptr;
    std::atomic<float>* eqLowParam   = nullptr;
    std::atomic<float>* eqMidParam   = nullptr;
    std::atomic<float>* eqHighParam  = nullptr;
    std::atomic<float>* eqHpfOnParam = nullptr;
    std::atomic<float>* eqHpfHzParam = nullptr;
    std::atomic<float>* eqLpfOnParam = nullptr;
    std::atomic<float>* eqLpfHzParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpProcessor)
};

} // namespace orbitamp
