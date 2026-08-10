#pragma once

#include "Parameters.h"
#include "core/CapturedBlock.h"
#include "core/DemoPlayer.h"
#include "core/MeasuredFilter.h"
#include "core/ScopeTap.h"
#include "core/WaveRibbon.h"
#include "core/PowerAmp.h"
#include "core/ReverbStage.h"
#include "core/ToneStack.h"

#include <felitronics/appkit/CompareHistory.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

namespace orbitamp
{

/** The plugin shell. Deliberately thin: the chain (eq1 -> boost -> eq2 -> preamp -> reverb ->
    power amp -> cabinet) lands in src/core/ behind small engines, and this class only pumps
    buffers into them and owns state. */
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
    static constexpr float maxScale = 4.0f;

    /** What a window OPENS at, before the screen has its say. Separate from editorScale on purpose:
        editorScale follows the window, and the window is set by whoever hosts it — so an editor that
        opened from it was reading back whatever the last host had shrunk it to, and once anything
        shrank it the value stuck. A wanted size and a current size are two different facts. */
    static constexpr float preferredScale = 1.0f;

private:
    /** Pumps the history's settle timer — a burst of edits that has been quiet for a moment commits
        as ONE undo step, so dragging a knob is not fifty of them. */
    void timerCallback() override
    {
        history.tick();
        pumpDeviceWork();
        applyOversamplingIfChanged();
    }

    /** Re-preparing is a message-thread job, so the footer's oversampling choice is picked up here
        rather than in the audio callback. */
    void applyOversamplingIfChanged();

public:
    /** Everything a moved control needs done OFF the audio thread, for both captured blocks: loading
        the capture a gain knob now points at, designing the measured filters, retiring models nobody
        is playing.

        The timer calls this thirty times a second. It is public because it is the plugin's only
        message-thread heartbeat, and something that is not a host — a test — has to be able to drive
        it; without a driver a knob moves a parameter and nothing ever reads it, which is exactly the
        bug this became. */
    void pumpDeviceWork();

private:
    /** Reads each EQ link's parameters into its stack. Called per block from the audio thread;
        a stack only redesigns when something actually moved. */
    void updateEqSettings() noexcept;

    /** Same, for the reverb: character and mix, applied only when they move. */
    void updateReverbSettings() noexcept;

    float editorScale = preferredScale;

    std::array<core::ToneStack, params::numEqLinks> eqLinks;
    core::ReverbStage reverb;
    core::PowerAmp    power;

public:
    /** The captured pedal in front, and the captured preamp after it. Each is a device list, the
        stage playing what is chosen from it, that device's measured controls as filters, and the taps
        its pictures read. The library and the loading live on the message thread; the audio thread
        only ever meets a model that is already in memory. */
    core::CapturedBlock<params::boostNumMeasured>  boost  { device::DeviceLibrary::Slot::pedal };
    core::CapturedBlock<params::preampNumMeasured> preamp { device::DeviceLibrary::Slot::preamp };

    /** Re-scans the devices folder and loads whatever the device parameters point at. Message
        thread. */
    void rescanDevices();

    /** TEMPORARY — the audition loop player. Goes with the demo strip it belongs to. */
    core::DemoPlayer demo;
    void selectDemoLoop (int index);

private:

    // Cached atomic parameter pointers — getRawParameterValue does a map lookup, which the audio
    // thread should not be doing per block.
    struct EqLinkParams
    {
        std::atomic<float>* on    = nullptr;
        std::atomic<float>* low   = nullptr;
        std::atomic<float>* mid   = nullptr;
        std::atomic<float>* high  = nullptr;
        std::atomic<float>* pres  = nullptr;
        std::atomic<float>* midHz = nullptr;
        std::atomic<float>* hpfOn = nullptr;
        std::atomic<float>* hpfHz = nullptr;
        std::atomic<float>* lpfOn = nullptr;
        std::atomic<float>* lpfHz = nullptr;
    };

    std::array<EqLinkParams, params::numEqLinks> eqParams;

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
    std::atomic<float>* preampOnParam   = nullptr;
    std::atomic<float>* preampGainParam = nullptr;
    juce::AudioBuffer<float> scopeDry;   // a block's input, kept for its pictures

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
