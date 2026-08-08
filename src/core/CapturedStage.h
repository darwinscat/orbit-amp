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

    /** Load the model for a gain index. Message thread: it reads a file and decodes. */
    void selectGainIndex (int index)
    {
        const auto* stageDef = firstNam();
        if (pack == nullptr || stageDef == nullptr)
            return;

        const auto positions = gainPositions();
        if (! juce::isPositiveAndBelow (index, positions.size()))
            return;

        load (fileForGain (*stageDef, positions[index]));
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

    void process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        if (ready.load())
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
    static juce::String fileForGain (const namz::rig::Stage& stageDef, const juce::String& value)
    {
        juce::String gainName;
        for (const auto& c : stageDef.device.controls)
            if (c.role == namz::rig::Role::Gain)
                gainName = juce::String (c.name);

        for (const auto& f : stageDef.device.files)
        {
            const auto it = f.settings.find (gainName.toStdString());
            if (it != f.settings.end() && juce::String (it->second) == value)
                return juce::String (f.id);   // namz stores the manifest's "file" as the entry id
        }

        return {};
    }

    felitronics::nam::NamStage stage;
    const device::DeviceLibrary::Pack* pack = nullptr;
    juce::String loadedFile;
    std::atomic<bool> ready { false };
};

} // namespace orbitamp::core
