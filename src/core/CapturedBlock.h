#pragma once

#include "../Parameters.h"
#include "../device/DeviceLibrary.h"
#include "CapturedStage.h"
#include "MeasuredFilter.h"
#include "ScopeTap.h"
#include "WaveRibbon.h"

#include <felitronics/lineareq/MagnitudeCurve.h>

#include <array>
#include <vector>

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
        stage.prepare (sampleRate, maxBlock, numChannels);
        ribbon.prepare (sampleRate);

        for (auto& f : tone)
            f.prepare (sampleRate, maxBlock, numChannels);

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
        lastSelected = index;

        stage.setPack (juce::isPositiveAndBelow (index, packs.size())
                         ? &packs.getReference (index)
                         : (packs.isEmpty() ? nullptr : &packs.getReference (0)));

        lastGainIndex = -1;
        lastTone.fill (-1.0f);
    }

    /** Loads what the device parameter now points at, when it moved. This is how a restored session
        or a host automating the parameter actually lands: replaceState changes the number and calls
        nothing, so the pump watches it — the same arrangement every other loading knob has. */
    void selectIfMoved (int index)
    {
        if (index != lastSelected)
            select (index);
    }

    /** Turn a device's other selecting control — the octave switch, a mode, whatever the pack has.
        Loading is a file read, so this is message-thread work like the gain knob's. */
    void applySelectors (const std::array<int, (size_t) params::numSelectors>& indices)
    {
        const auto list = stage.selectors();

        for (int i = 0; i < params::numSelectors && i < list.size(); ++i)
        {
            const auto& sel = list.getReference (i);
            const int index = juce::jlimit (0, sel.values.size() - 1, indices[(size_t) i]);

            if (index == lastSelector[(size_t) i])
                continue;

            lastSelector[(size_t) i] = index;
            stage.selectValue (sel.name, sel.values[index]);
        }
    }

    /** The gain knob SELECTS a capture, so a move means loading a file — message thread work, off the
        timer, because the audio thread never touches a disk. `gain` is the 0..10 panel scale. */
    void loadIfGainMoved (float gain)
    {
        const auto positions = stage.gainPositions();

        // No captured axis: the knob drives instead of selecting. 5 is unity, so the middle of the
        // dial is the capture as it was taken and either side of it is a decision.
        if (positions.isEmpty())
        {
            stage.setDrive (juce::Decibels::decibelsToGain ((gain - 5.0f) * driveDbPerStep));
            return;
        }

        stage.setDrive (1.0f);

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

    /** Whether the pack's own measured curves are bypassed — see `params::blockEqMode`. Raw is
        what OURS means: our parametric is standing where the device's tone stack used to, and a
        device wearing both at once is a decibel nobody can trace. */
    void setRaw (bool shouldBeRaw)
    {
        if (raw == shouldBeRaw)
            return;

        raw = shouldBeRaw;
        lastTone.fill (-1.0f);   // every filter changes state at once
    }

    bool isRaw() const noexcept { return raw; }

    void updateToneIfMoved (const std::array<float, (size_t) NumMeasured>& values)
    {
        const auto* measured = stage.measured();

        for (int i = 0; i < NumMeasured; ++i)
        {
            auto& filter = tone[(size_t) i];

            if (raw || measured == nullptr || i >= (int) measured->size())
            {
                filter.clear();
                curves[(size_t) i] = ToneCurve {};
                continue;
            }

            if (juce::approximatelyEqual (values[(size_t) i], lastTone[(size_t) i]))
                continue;

            lastTone[(size_t) i] = values[(size_t) i];
            filter.setPosition ((*measured)[(size_t) i], (double) values[(size_t) i]);
            rebuildCurve (i, (double) values[(size_t) i]);
        }
    }

    /** What the measured controls are doing as a whole, in dB at a frequency — the block's own
        answer for any picture that wants to draw its tone, from the block's face to a chain
        miniature. Message thread, off the same data the filters were designed from: the same band
        decision the sound makes, so the picture never promises what the audio does not play. */
    double toneDb (double freqHz) const
    {
        // Every measured control at once. They sit in series in the pedal — SM7's two EQ knobs and
        // its Sharp/Smooth switch all act on the same signal — so the picture is their sum, not
        // whichever one happens to be first.
        double sum = 0.0;
        for (const auto& c : curves)
            sum += felitronics::lineareq::curveDbAt (c.db, c.hz, freqHz);

        return sum;
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

        stage.process (channels, numChannels, numSamples);

        // The measured controls sit AFTER the capture — `placement: post` — because that is where
        // they sit in the device.
        for (auto& f : tone)
            f.process (buffer);

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

    CapturedStage stage;
    ScopeTap scope;
    WaveRibbon ribbon;
    juce::Array<device::DeviceLibrary::Pack> packs;
    std::array<MeasuredFilter, (size_t) NumMeasured> tone;

private:
    // Four decibels a step: the full dial spans forty, which is enough to take a capture from barely
    // breaking up to thoroughly into it without leaving the range a model behaves in.
    static constexpr float driveDbPerStep = 4.0f;

    /** A measured control's curve at wherever its knob sits, resolved once per move rather than once
        per pixel. Message-thread data, like everything else the pictures read. */
    struct ToneCurve { std::vector<double> db, hz; };

    void rebuildCurve (int i, double t)
    {
        auto& c = curves[(size_t) i];
        c = ToneCurve {};

        const auto* measured = stage.measured();
        if (measured == nullptr || i >= (int) measured->size())
            return;

        const auto& m = (*measured)[(size_t) i];
        if (m.positions.size() < 2 || m.grid.points < 2)
            return;

        std::vector<std::vector<double>> pos;
        std::vector<double> norms;
        for (const auto& p : m.positions)
        {
            pos.push_back (p.db);
            norms.push_back (p.norm);
        }

        c.hz = felitronics::lineareq::logFreqGrid (m.grid.fLo, m.grid.fHi, m.grid.points);

        const auto band = MeasuredFilter::bandFor (m);
        c.db = felitronics::lineareq::heldOutsideBand (
            felitronics::lineareq::curveAtPosition (pos, norms, juce::jlimit (0.0, 1.0, t)),
            c.hz, band.lo, band.hi);
    }

    std::array<ToneCurve, (size_t) NumMeasured> curves;

    device::DeviceLibrary::Slot slot;
    std::array<float, (size_t) NumMeasured> lastTone { };
    std::array<int, (size_t) params::numSelectors> lastSelector { -1, -1 };
    int lastGainIndex = -1;
    int lastSelected  = -1;
    bool raw = false;
};

} // namespace orbitamp::core
