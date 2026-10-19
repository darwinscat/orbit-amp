// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Gate for AN AMP WITH NO FACTORY FOLDER (ui/FactoryMissing.h) — copied off a zip without it, or the
// folder deleted. The amp is compiled again to read a folder that is not there (CMakeLists.txt:
// amp_console NO_FACTORY), and must: hold every place of its cabinet shelf empty, DARK MATTER's too; find
// no pack of its own; play a guitar through for as long as a few seconds of blocks, and through every
// channel it has, without a crash and without a sample that is not a number; and open a window that says
// the folder is missing and where it belongs — a face in place of its whole window, in type a player
// reads. OrbitAmp, the cockpit and the rovers' dashboard each run it as themselves.

#include "PluginProcessor.h"
#include "Parameters.h"
#include "Product.h"
#include "device/DeviceLibrary.h"
#include "device/FactoryShelf.h"
#include "ui/FactoryMissing.h"

#include <cmath>
#include <cstdio>

int main()
{
    using namespace orbitamp;
    const juce::ScopedJuceInitialiser_GUI juceInit;

    int failed = 0;
    const auto check = [&failed] (bool ok, const juce::String& what)
    {
        std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
        if (! ok)
            ++failed;
    };
    const juce::String amp (product::name);

    const auto factory = device::FactorySet::location();
    std::printf ("%s, its factory folder at %s\n", product::name, factory.getFullPathName().toRawUTF8());
    check (! factory.exists(), "the factory folder is not there");

    // THE SHELF: every place empty, and the cabinet every amp opens on with them.
    {
        bool empty = device::shelf::irs().size() == (size_t) params::cabIrNames.size();
        for (const auto& ir : device::shelf::irs())
            empty = empty && ir.data == nullptr && ir.size == 0 && ir.file.isEmpty();
        check (empty, "every place of the cabinet shelf is held empty");
        check (AmpProcessor::cabIrBytes (params::cabIrDefault).data == nullptr
                   && AmpProcessor::cabIrBytes (0).data == nullptr,
               "...DARK MATTER's too, and a number of Other's plays no bytes rather than a stray pointer");
    }

    // THE PACKS: none of the factory's — whatever the player's own Devices folder holds is his.
    {
        int factoryPacks = 0;
        for (const auto& p : device::DeviceLibrary::scan())
            factoryPacks += p.bundled ? 1 : 0;
        check (factoryPacks == 0, "no pack of the factory's is offered");
    }

    // THE AMP PLAYS: a few seconds of a guitar's worth of noise, through every channel it has.
    auto processor = std::make_unique<AmpProcessor>();
    processor->inlineLoads = true;            // no message loop here: a model would land inside the pump
    constexpr double rate = 48000.0;
    constexpr int block = 512;
    processor->prepareToPlay (rate, block);

    juce::RangedAudioParameter* channel = nullptr;
    for (auto* p : processor->getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p); ranged != nullptr && ranged->getParameterID().endsWith ("_channel"))
            channel = ranged;

    const auto pump = [&processor]
    {
        if (processor->product != nullptr)
            processor->product->pump();
        processor->pumpDeviceWork();
        processor->history.tick();
    };

    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    juce::Random random (20261019);
    bool finite = true;
    const int channels = channel != nullptr ? juce::roundToInt (channel->getNormalisableRange().end) + 1 : 1;
    for (int c = 0; c < channels; ++c)
    {
        if (channel != nullptr)
            channel->setValueNotifyingHost (channel->convertTo0to1 ((float) c));

        for (int b = 0; b < 100; ++b)
        {
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                for (int i = 0; i < block; ++i)
                    buffer.setSample (ch, i, (random.nextFloat() * 2.0f - 1.0f) * 0.3f);

            processor->processBlock (buffer, midi);

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                for (int i = 0; i < block; ++i)
                    finite = finite && std::isfinite (buffer.getSample (ch, i));

            if (b % 4 == 0)
                pump();
        }
    }
    check (finite, "the " + amp + " plays " + juce::String (channels) + " channel(s) of noise through, every sample a number");

    // THE WINDOW. A face has nothing to draw: its whole window is the notice. OrbitAmp still plays what
    // the player adds, and says it where its empty device block stands — its whole face, which a Linux
    // runner with no display is not asked to build.
   #if JUCE_LINUX
    if (juce::String (product::name) == "OrbitAmp")
        std::printf ("skip  OrbitAmp's window, on a system that may have no display\n");
    else
   #endif
    {
        std::unique_ptr<juce::AudioProcessorEditor> window (processor->createEditor());
        const auto* notice = dynamic_cast<ui::FactoryMissingEditor*> (window.get());
        const auto said = ui::FactoryMissingEditor::text();

        check (window != nullptr, "the " + amp + " opens a window");
        if (juce::String (product::name) == "OrbitAmp")
            check (notice == nullptr, "...OrbitAmp's own, which still plays what the player adds");
        else
            check (notice != nullptr, "...which is the notice, not an empty face");

        bool placed = true;
        for (const auto& place : device::FactorySet::installedPlaces())
            placed = placed && said.contains (place.getFullPathName()) && ui::factoryMissingLine().contains (place.getFullPathName());
        check (placed && said.contains (amp) && said.containsIgnoreCase ("installer") && said.contains ("Factory"),
               "...which names the amp, where the folder belongs, and how to put it there");
        check (ui::FactoryMissingEditor::textPx >= 13.0f && ui::FactoryMissingEditor::headingPx >= 13.0f,
               "...in type a player reads, 13 px or more");

        if (window != nullptr)
        {
            const auto shot = window->createComponentSnapshot (window->getLocalBounds());
            check (shot.isValid() && shot.getWidth() > 0, "...and it paints");
        }
    }

    processor->releaseResources();
    std::printf ("%s\n", failed == 0 ? "PASSED" : "FAILED");
    return failed == 0 ? 0 : 1;
}
