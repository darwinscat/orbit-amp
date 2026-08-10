#pragma once

#include "../device/DeviceLibrary.h"
#include "CapturedStage.h"
#include "MeasuredFilter.h"
#include "ScopeTap.h"
#include "VoiceEq.h"
#include "WaveRibbon.h"

#include <array>

namespace orbitamp::core
{

/** Everything one captured block needs to play and to be looked at.

    A block is a device list, the stage playing whatever is chosen from it, that device's measured
    controls as filters, and the two taps its pictures read. The boost was all of this spelled out in
    the processor; the preamp needs the same set and the power amp will too, and a second copy of a
    dozen members is how the three quietly drift apart.

    Nothing here knows which block it is. What differs between them is the SLOT they scan for and the
    parameters they are wired to — both handed in from outside. */
template <int NumMeasured>
class CapturedBlock
{
public:
    static constexpr int numMeasured = NumMeasured;

    explicit CapturedBlock (device::DeviceLibrary::Slot deviceSlot) : slot (deviceSlot) {}

    void prepare (double sampleRate, int maxBlock, int numChannels)
    {
        stage.prepare (sampleRate, maxBlock);
        ribbon.prepare (sampleRate);

        for (auto& f : tone)
            f.prepare (sampleRate, maxBlock, numChannels);

        eq.prepare (sampleRate, numChannels);

        lastTone.fill (-1.0f);
        lastGainIndex = -1;
    }

    /** Re-reads the folder and loads whatever `index` now points at. Message thread. */
    void rescan (int index)
    {
        packs = device::DeviceLibrary::scan (slot);
        select (index);
    }

    /** Loads the device at `index`. A saved session names a device by position in a list that is
        whatever is on disk today; if it is gone, the first one stands in rather than nothing loading,
        since silence is a worse answer than the wrong device and the name says which it is. */
    void select (int index)
    {
        stage.setPack (juce::isPositiveAndBelow (index, packs.size())
                         ? &packs.getReference (index)
                         : (packs.isEmpty() ? nullptr : &packs.getReference (0)));

        lastGainIndex = -1;
        lastTone.fill (-1.0f);
    }

    /** The gain knob SELECTS a capture, so a move means loading a file — message thread work, off the
        timer, because the audio thread never touches a disk. `gain` is the 0..10 panel scale. */
    void loadIfGainMoved (float gain)
    {
        const auto positions = stage.gainPositions();
        if (positions.isEmpty())
            return;

        // The knob reads 0..10; the captures sit at whatever angles the device was taken at, evenly
        // spaced across that travel. Nearest position wins — between two captures there is nothing.
        const float t = juce::jlimit (0.0f, 1.0f, gain * 0.1f);
        const int index = juce::jlimit (0, positions.size() - 1,
                                        juce::roundToInt (t * (float) (positions.size() - 1)));

        if (index == lastGainIndex)
            return;

        lastGainIndex = index;
        stage.selectGainIndex (index);
    }

    /** Re-designs a measured filter when its knob moved. Message thread — it builds an FIR. */
    void updateToneIfMoved (const std::array<float, (size_t) NumMeasured>& values)
    {
        const auto* measured = stage.measured();

        for (int i = 0; i < NumMeasured; ++i)
        {
            auto& filter = tone[(size_t) i];

            if (measured == nullptr || i >= (int) measured->size())
            {
                filter.clear();
                continue;
            }

            if (juce::approximatelyEqual (values[(size_t) i], lastTone[(size_t) i]))
                continue;

            lastTone[(size_t) i] = values[(size_t) i];
            filter.setPosition ((*measured)[(size_t) i], (double) values[(size_t) i]);
        }
    }

    void collectGarbage() { stage.collectGarbage(); }

    /** Plays the block and feeds its pictures. `dry` is scratch the caller owns, big enough for the
        buffer — kept so a picture can show what went in, not only what came out. */
    void process (juce::AudioBuffer<float>& buffer, juce::AudioBuffer<float>& dry) noexcept
    {
        const int numSamples = buffer.getNumSamples();

        dry.setSize (1, numSamples, false, false, true);
        dry.copyFrom (0, 0, buffer, 0, 0, numSamples);

        auto* const* channels = buffer.getArrayOfWritePointers();
        const int numChannels = buffer.getNumChannels();

        // BEFORE the capture, when it is set there — that is the whole reason the placement exists.
        // What reaches a nonlinearity decides what kind of distortion comes out of it.
        if (eq.isPre())
            eq.process (channels, numChannels, numSamples);

        stage.process (channels, numChannels, numSamples);

        // The measured controls sit AFTER the capture — `placement: post` — because that is where
        // they sit in the device.
        for (auto& f : tone)
            f.process (buffer);

        if (! eq.isPre())
            eq.process (channels, numChannels, numSamples);

        scope.write (dry.getReadPointer (0), buffer.getReadPointer (0), numSamples);
        ribbon.write (dry.getReadPointer (0), buffer.getReadPointer (0), numSamples);
    }

    // What a block's face asks about the device it is playing. Straight through to the stage: a view
    // should not have to know that a block is a stage plus four other things.
    const std::vector<namz::rig::Measured>* measured() const { return stage.measured(); }
    juce::StringArray gainPositions() const                  { return stage.gainPositions(); }
    juce::String circuit() const                             { return stage.circuit(); }
    juce::String deviceName() const                          { return stage.deviceName(); }
    bool isReady() const noexcept                            { return stage.isReady(); }

    /** Ours, not the device's — see core::VoiceEq. Public because the block's face draws it and the
        processor feeds it from parameters. */
    VoiceEq eq;

    CapturedStage stage;
    ScopeTap scope;
    WaveRibbon ribbon;
    juce::Array<device::DeviceLibrary::Pack> packs;
    std::array<MeasuredFilter, (size_t) NumMeasured> tone;

private:
    device::DeviceLibrary::Slot slot;
    std::array<float, (size_t) NumMeasured> lastTone { };
    int lastGainIndex = -1;
};

} // namespace orbitamp::core
