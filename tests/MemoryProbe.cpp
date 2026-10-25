// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Probe: what the cockpit holds in memory when nobody is looking at it. A plug-in's window is open
// for minutes and shut for hours, and a session carries several of it — so the figure that matters in
// a host is the one with the window SHUT. The standalone cannot give it: its window is always open,
// and most of what it holds is the picture.
//
// One process, read step by step with the system's own figure (core/ProcessMemory.h):
//
//   MemoryProbe [state]             the processor, then its window — built, never shown, so not a
//                                   pixel of it costs graphics memory — then playing, then the window
//                                   shut and still playing: a host's "editor closed". With a file
//                                   named, the state the window left is written there.
//   MemoryProbe --no-window state   a session reopened and the window never opened: the processor
//                                   takes that state and plays.
//
// It reads the cockpit's own packs and cabinets from where the cockpit reads them. Diagnostic, not a
// gate.

#include "PluginProcessor.h"
#include "core/ProcessMemory.h"

#include <cmath>
#include <cstdio>

int main (int argc, char** argv)
{
    using namespace orbitamp;
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::StringArray args (argv + 1, argc - 1);
    const bool noWindow = args.contains ("--no-window");
    juce::File stateFile;
    for (const auto& a : args)
        if (! a.startsWith ("--"))
            stateFile = juce::File::getCurrentWorkingDirectory().getChildFile (a);

    const auto megabytes = [] { return (double) core::processMemoryBytes() / (1024.0 * 1024.0); };
    double last = megabytes();
    std::printf ("%-36s %7.1f MB\n", "the bare process", last);

    const auto read = [&] (const char* what)
    {
        const double now = megabytes();
        std::printf ("%-36s %7.1f MB   (%+.1f)\n", what, now, now - last);
        last = now;
    };

    auto amp = std::make_unique<AmpProcessor> (ORBITAMP_GATE_PRODUCT());
    amp->inlineLoads = true;            // no message loop here: a model lands inside the pump
    read ("the processor built");

    std::unique_ptr<juce::AudioProcessorEditor> window;
    if (noWindow)
    {
        juce::MemoryBlock state;
        if (! stateFile.loadFileAsData (state))
        {
            std::printf ("no state to take: name the file a run with the window wrote\n");
            return 2;
        }
        amp->setStateInformation (state.getData(), (int) state.getSize());
        read ("the session's state taken");
    }
    else
    {
        // The window, as a host opens it: it sets the cockpit's chain up and asks for the channel's pack.
        window.reset (amp->createEditor());
        read ("its window built, never shown");
    }

    constexpr double rate = 48000.0;
    constexpr int block = 512;
    amp->setPlayConfigDetails (2, 2, rate, block);
    amp->prepareToPlay (rate, block);
    read ("prepared to play");

    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    double phase = 0.0;

    // A plain tone through the whole chain, the device work pumped as the plug-in's own timer pumps it.
    const auto play = [&] (double seconds, const char* what)
    {
        float peak = 0.0f;
        for (int b = 0; b < (int) (seconds * rate / block); ++b)
        {
            for (int i = 0; i < block; ++i)
            {
                const float s = 0.25f * (float) std::sin (phase);
                phase += juce::MathConstants<double>::twoPi * 110.0 / rate;
                buffer.setSample (0, i, s);
                buffer.setSample (1, i, s);
            }

            amp->processBlock (buffer, midi);
            amp->pumpDeviceWork();
            juce::Thread::sleep (2);    // the convolver loads its kernel on a thread of its own
            peak = juce::jmax (peak, buffer.getMagnitude (0, block));
        }

        read (what);
        std::printf ("%-36s %7.3f      %s\n", "  peak at the output", (double) peak,
                     peak > 1.0e-4f ? "" : "SILENT: did the pack load?");
    };

    if (noWindow)
    {
        play (6.0, "playing, the window never opened");
        return 0;
    }

    play (6.0, "playing, the window built");

    if (stateFile != juce::File())
    {
        juce::MemoryBlock state;
        amp->getStateInformation (state);
        stateFile.replaceWithData (state.getData(), state.getSize());
    }

    window.reset();
    play (6.0, "the window shut, still playing");
    return 0;
}
