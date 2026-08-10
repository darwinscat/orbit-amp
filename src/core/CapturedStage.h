#pragma once

#include "../device/DeviceLibrary.h"

#include <felitronics/nam/NamStage.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>

namespace orbitamp::core
{

/** One captured block — a pedal or a preamp — playing the `.namz` that matches where its knobs are.

    A captured control SELECTS a file: turning Drive does not change a parameter inside the model, it
    picks a different model. So the work here is choosing the right file when a knob moves, loading
    it off the audio thread, and swapping it in without a click. felitronics::nam::NamStage owns the
    swap and the deferred free; this owns the choosing.

    Loading happens on the message thread. The audio thread only ever sees a model that is already
    in memory, or none. */
class CapturedStage
{
public:
    void prepare (double sampleRate, int maxBlock)
    {
        stage.prepare (sampleRate, maxBlock);
        stage.reset();
    }

    void reset() { stage.reset(); }

    /** Point the stage at a pack. Clears whatever was loaded — a different device is a different
        set of files, and keeping the old model alive would be playing the previous pedal. */
    void setPack (const device::DeviceLibrary::Pack* newPack)
    {
        pack = newPack;
        loadedFile.clear();
        stage.clearModel();
        ready.store (false);
        inputGain.store (1.0f);

        // The device's own default position for every selecting control. resolve() needs somewhere to
        // start: the gain knob names one setting, and whatever else the device has keeps the value it
        // was captured at until something moves it.
        settings = firstNam() != nullptr ? namz::rig::defaultSettings (firstNam()->device)
                                         : namz::rig::Settings {};

        loadOnlyFile();
    }

    /** The captured positions of the control marked `role: gain`, in the order the pack lists them —
        the real angles the device was captured at, which is what the knob's detents are. */
    juce::StringArray gainPositions() const
    {
        juce::StringArray out;

        if (const auto* stageDef = firstNam())
            for (const auto& c : stageDef->device.controls)
                if (c.role == namz::rig::Role::Gain)
                    for (const auto& v : c.values)
                        out.add (juce::String (v));

        return out;
    }

    /** The device's OTHER selecting controls — everything that picks a file and is not the gain axis.

        Fur Coat is the case that exposed this: a Fuzz dial of twenty-one positions AND an Octave
        switch, forty-two files, two for every dial position. Reading only the gain control meant half
        the pack was unreachable — the player silently got whichever side `resolve` fell back to.

        Returned as name plus its values, in the order the pack lists them, because that order is the
        one the device's own panel had. */
    struct Selector { juce::String name; juce::StringArray values; };

    juce::Array<Selector> selectors() const
    {
        juce::Array<Selector> out;

        if (const auto* stageDef = firstNam())
            for (const auto& c : stageDef->device.controls)
                if (c.role != namz::rig::Role::Gain && c.values.size() > 1)
                {
                    Selector s { juce::String (c.name), {} };
                    for (const auto& v : c.values)
                        s.values.add (juce::String (v));

                    out.add (std::move (s));
                }

        return out;
    }

    /** Turn one of them, and load whatever that combination resolves to. Message thread. */
    void selectValue (const juce::String& controlName, const juce::String& value)
    {
        const auto* stageDef = firstNam();
        if (pack == nullptr || stageDef == nullptr || controlName.isEmpty())
            return;

        const auto* entry = namz::rig::resolve (stageDef->device, settings,
                                                controlName.toStdString(), value.toStdString());
        if (entry == nullptr)
            return;

        inputGain.store (juce::Decibels::decibelsToGain ((float) -entry->inputDb));
        load (juce::String (entry->id));
    }

    /** A device with no gain axis has exactly one model, and nothing will ever ask for it by
        position — so it loads when the device does. Without this a lone .nam sits silent: the gain
        knob is what triggers a load, and this kind of device does not have one. */
    void loadOnlyFile()
    {
        const auto* stageDef = firstNam();
        if (pack == nullptr || stageDef == nullptr)
            return;

        if (! gainPositions().isEmpty() || stageDef->device.files.size() != 1)
            return;

        load (juce::String (stageDef->device.files.front().id));
    }

    /** Whether the loaded device's gain is a knob at all. */
    bool hasGainAxis() const { return ! gainPositions().isEmpty(); }

    /** Load the model for a gain index. Message thread: it reads a file and decodes.

        namz picks the file, not us. `resolve` knows what our own search did not: that a combination
        with no capture of its own can be an ALIAS for a neighbour played with less signal going in,
        and that when several controls select, the one just turned is law while the rest fall back to
        the nearest match. A hand-rolled lookup on one control gets the right answer only while the
        device has exactly one. */
    void selectGainIndex (int index)
    {
        const auto* stageDef = firstNam();
        if (pack == nullptr || stageDef == nullptr)
            return;

        const auto positions = gainPositions();
        if (! juce::isPositiveAndBelow (index, positions.size()))
            return;

        juce::String gainName;
        for (const auto& c : stageDef->device.controls)
            if (c.role == namz::rig::Role::Gain)
                gainName = juce::String (c.name);

        if (gainName.isEmpty())
            return;

        const auto* entry = namz::rig::resolve (stageDef->device, settings,
                                                gainName.toStdString(),
                                                positions[index].toStdString());
        if (entry == nullptr)
            return;

        // HOW MUCH SOFTER the signal reaching this model is. Into the model, never out of it: less
        // signal is also less drive, which is the whole point — the bottom of a gain dial fades
        // instead of falling into a hole where nothing was captured. The capture side decided this
        // and wrote it down; a player only applies it.
        inputGain.store (juce::Decibels::decibelsToGain ((float) -entry->inputDb));

        load (juce::String (entry->id));
    }

    /** Reads one named model in and plays it. Message thread. */
    void load (const juce::String& wanted)
    {
        if (pack == nullptr || wanted.isEmpty() || wanted == loadedFile)
            return;

        const auto bytes = device::DeviceLibrary::readBinaryEntry (*pack, wanted);
        if (bytes.getSize() == 0)
            return;

        if (stage.loadModelFromMemory (bytes.getData(), bytes.getSize()))
        {
            loadedFile = wanted;
            ready.store (true);
        }
    }

    /** The pack's measured controls — the ones a player computes rather than selects. */
    const std::vector<namz::rig::Measured>* measured() const
    {
        const auto* s = firstNam();
        return s != nullptr ? &s->measured : nullptr;
    }

    /** The device spec string for the glyph row, e.g. "ic:2,diode:4". Empty when the pack is silent
        about the circuit. */
    juce::String circuit() const
    {
        const auto* s = firstNam();
        return s != nullptr ? juce::String (s->circuit) : juce::String();
    }

    juce::String deviceName() const
    {
        const auto* s = firstNam();
        if (s == nullptr)
            return {};

        const auto make = juce::String (s->make), model = juce::String (s->model);
        return make.isNotEmpty() ? make + " " + model : model;
    }

    /** Frees models the swap retired. Message thread, once the audio has moved past them. */
    void collectGarbage() { stage.collectGarbage(); }

    bool isReady() const noexcept { return ready.load(); }

    /** How hard to drive the model, for a device with no captured gain axis.

        A capture taken at one setting has no dial of its own — but it still has a nonlinearity, and
        what decides how hard that is hit is how much signal arrives. So the gain knob keeps working:
        where a pack offers positions it SELECTS one, and where it does not it DRIVES. Same knob, same
        gesture, and the block is never left with a control that does nothing. */
    void setDrive (float linearGain) noexcept { drive.store (linearGain); }

    void process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        if (! ready.load())
            return;

        if (const float g = inputGain.load() * drive.load(); ! juce::approximatelyEqual (g, 1.0f))
            for (int ch = 0; ch < numChannels; ++ch)
                juce::FloatVectorOperations::multiply (channels[ch], g, numSamples);

        stage.process (channels, numChannels, numSamples, true);
    }

private:
    const namz::rig::Stage* firstNam() const
    {
        if (pack == nullptr)
            return nullptr;

        for (const auto& s : pack->rig.chain)
            if (s.kind == namz::rig::StageKind::Nam)
                return &s;

        return nullptr;
    }

    /** The file whose settings put the gain control at this value. */

    felitronics::nam::NamStage stage;
    const device::DeviceLibrary::Pack* pack = nullptr;
    juce::String loadedFile;
    std::atomic<float> inputGain { 1.0f };
    std::atomic<float> drive { 1.0f };
    namz::rig::Settings settings;
    std::atomic<bool> ready { false };
};

} // namespace orbitamp::core
