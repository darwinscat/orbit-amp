#pragma once

#include "Parameters.h"
#include "core/CapturedStage.h"
#include "core/DemoPlayer.h"
#include "core/MeasuredFilter.h"
#include "core/ScopeTap.h"
#include "core/PowerAmp.h"
#include "core/ReverbStage.h"
#include "core/ToneStack.h"

#include <felitronics/appkit/CompareHistory.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

namespace orbitamp
{

/** The plugin shell. Deliberately thin: the chain (boost -> preamp -> EQ -> reverb) lands in
    src/core/ behind a single engine, and this class only pumps buffers into it and owns state.
    Passthrough for now — the scaffold exists to be loaded in a host, not to make sound yet. */
class AmpProcessor final : public juce::AudioProcessor,
                           private juce::Timer
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

    /** Undo/redo + the A/B/C/D registers, from felitronics-appkit. It lives on the PROCESSOR, not
        the editor: the history is part of the session, so it has to survive closing the window and
        be saved with the plugin state.

        PerRegister topology — each register carries its own undo history and switching registers is
        NOT an undo step (its inverse is simply selecting the other slot). That is the deliberate
        opposite of OrbitCab, whose whole workspace is one timeline. */
    felitronics::appkit::CompareHistory history;

    /** The editor's zoom, 50-200%. It lives here rather than in the editor so it survives closing
        and reopening the window; it is message-thread only and never read by the audio path.
        Persisting it across sessions comes with the state work. */
    float getEditorScale() const noexcept { return editorScale; }
    void setEditorScale (float s) noexcept { editorScale = juce::jlimit (minScale, maxScale, s); }

    static constexpr float minScale = 0.5f;
    static constexpr float maxScale = 2.0f;

private:
    /** Pumps the history's settle timer — a burst of edits that has been quiet for a moment commits
        as ONE undo step, so dragging a knob is not fifty of them. */
    void timerCallback() override
    {
        history.tick();
        applyOversamplingIfChanged();
    }

    /** Re-preparing is a message-thread job, so the footer's oversampling choice is picked up here
        rather than in the audio callback. */
    void applyOversamplingIfChanged();

    /** The boost's gain knob SELECTS a capture, so a move means loading a different file. Message
        thread, off the timer — the audio thread never touches a disk. */
    void loadBoostModelIfChanged();

    /** Re-designs a measured filter when its knob moved. Message thread — it builds an FIR. */
    void updateBoostToneIfChanged();

    std::array<float, (size_t) params::boostNumMeasured> lastBoostTone { -1.0f, -1.0f, -1.0f };

    /** Reads the tone parameters into the stack's settings. Called per block from the audio thread;
        the stack only redesigns when something actually moved. */
    void updateToneSettings() noexcept;

    /** Same, for the reverb: character and mix, applied only when they move. */
    void updateReverbSettings() noexcept;

    float editorScale = 1.0f;

    core::ToneStack   tone;
    core::ReverbStage reverb;
    core::PowerAmp    power;

public:
    /** The captured pedal in front. The library and the loading live on the message thread; the
        audio thread only ever meets a model that is already in memory. */
    core::CapturedStage boost;

    /** What went into the boost and what came out — the boost block's pictures read this. */
    core::ScopeTap boostScope;

    /** The pedal's measured controls, playing. One per slot the pack fills. */
    std::array<core::MeasuredFilter, (size_t) params::boostNumMeasured> boostTone;
    juce::Array<device::DeviceLibrary::Pack> devicePacks;

    /** Re-scans the devices folder and points the boost at the first pack. Message thread. */
    void rescanDevices();

    /** TEMPORARY — the audition loop player. Goes with the demo strip it belongs to. */
    core::DemoPlayer demo;
    void selectDemoLoop (int index);

private:

    // Cached atomic parameter pointers — getRawParameterValue does a map lookup, which the audio
    // thread should not be doing per block.
    std::atomic<float>* eqOnParam    = nullptr;
    std::atomic<float>* eqLowParam   = nullptr;
    std::atomic<float>* eqMidParam   = nullptr;
    std::atomic<float>* eqHighParam  = nullptr;
    std::atomic<float>* eqPresParam  = nullptr;
    std::atomic<float>* eqMidHzParam = nullptr;
    std::atomic<float>* eqHpfOnParam = nullptr;
    std::atomic<float>* eqHpfHzParam = nullptr;
    std::atomic<float>* eqLpfOnParam = nullptr;
    std::atomic<float>* eqLpfHzParam = nullptr;

    std::atomic<float>* reverbOnParam   = nullptr;
    std::atomic<float>* reverbTypeParam = nullptr;
    std::atomic<float>* reverbMixParam  = nullptr;

    std::atomic<float>* powerOnParam    = nullptr;
    std::atomic<float>* powerDriveParam = nullptr;
    std::atomic<float>* powerSagParam   = nullptr;
    std::atomic<float>* powerTubeParam  = nullptr;
    std::atomic<float>* powerCountParam = nullptr;
    std::atomic<float>* oversampleParam = nullptr;
    std::atomic<float>* boostOnParam    = nullptr;
    std::atomic<float>* boostGainParam  = nullptr;
    int lastBoostGainIndex = -1;
    juce::AudioBuffer<float> scopeDry;   // the boost's input, kept for its pictures

public:
    /** What the footer reports: the run's own facts, not the sound's. */
    double currentSampleRate() const noexcept { return getSampleRate(); }
    float  dspLoadPercent() const noexcept    { return dspLoad.load(); }

private:
    std::atomic<float> dspLoad { 0.0f };
    int lastOversample = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmpProcessor)
};

} // namespace orbitamp
