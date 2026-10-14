// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Gate for THE COCKPIT'S CHANNELS WITH NO WINDOW (catpit/CatpitEngine.h). A plug-in's window is shut
// for most of its life, and what a host can reach then is the processor's parameters: so a channel
// has to be something the processor does by itself. No window is built here at all.
//
// What it holds the engine to:
//   - a processor just built IS the cockpit — chain, channel, pack — and that is not an edit;
//   - CHANNEL moved the way a host moves it switches the channel, pack and all;
//   - each channel comes back as it was left;
//   - a hand's switch is one step of the history, and undoing it brings parameter and sound back together;
//   - a state from before there was a parameter keeps its channel, and the parameter following it is
//     not a step of its own;
//   - a channel whose pack is not installed is refused, and the parameter says where the sound stayed.
//
//   - an IR a player adds is a cabinet of their own: after the build's, whatever it is called;
//   - a channel never pulled opens on its own reverb and its own share of delay.
//
// It reads the cockpit's packs from where the cockpit reads them, and lays the build's set out there
// first, as the cockpit does.

#include "PluginProcessor.h"
#include "Parameters.h"
#include "catpit/CatpitEngine.h"
#include "catpit/CatpitPresets.h"

#include <cmath>
#include <cstdio>

int main()
{
    using namespace orbitamp;
    const juce::ScopedJuceInitialiser_GUI juceInit;

    int failed = 0;
    const auto check = [&failed] (bool ok, const char* what)
    {
        std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what);
        if (! ok)
            ++failed;
    };

    auto amp = std::make_unique<AmpProcessor>();
    amp->inlineLoads = true;            // no message loop here: a model lands inside the pump

    auto& engine   = catpit::Engine::of (*amp);
    auto& channels = engine.channels;
    auto& history  = amp->history;
    auto* channel  = amp->apvts.getParameter (catpit::Engine::channelId);
    auto* drive    = amp->apvts.getParameter (params::preampGain);
    auto* device   = amp->apvts.getParameter (params::preampDevice);
    if (channel == nullptr || drive == nullptr || device == nullptr)
    {
        std::printf ("FAIL  the cockpit's processor has CHANNEL, DRIVE and a device to play\nFAILED\n");
        return 1;
    }

    const auto asked   = [channel] { return juce::roundToInt (channel->convertFrom0to1 (channel->getValue())); };
    const auto playing = [device]  { return juce::roundToInt (device->convertFrom0to1 (device->getValue())); };
    const auto asHost  = [channel] (int c) { channel->setValueNotifyingHost (channel->convertTo0to1 ((float) c)); };

    // What the processor's own timer does, as far as the channels go: the engine's half, then the
    // history's settle count. Forty of them is well past the moment a burst commits.
    const auto ticks = [&engine, &history] (int n = 40)
    {
        for (int i = 0; i < n; ++i)
        {
            engine.pump();
            history.tick();
        }
    };

    bool installed = true;
    for (int c = 0; c < catpit::Channels::count; ++c)
        installed = installed && channels.packFor (c) >= 0;
    check (installed, "all four channels' packs are installed");

    // A processor just built.
    check (channels.adopted(), "a processor just built is the cockpit's already");
    check (channels.active() == 0 && asked() == 0, "...on the first channel, and CHANNEL says so");
    check (playing() == channels.packFor (0), "...playing that channel's pack");
    ticks();
    check (! history.canUndo(), "...and none of that is an edit");

    // A host moves the parameter.
    asHost (2);
    check (channels.active() == 0, "CHANNEL moved by a host changes nothing until the engine's own tick");
    engine.pump();
    check (channels.active() == 2 && asked() == 2, "...and then the channel is the one asked for");
    check (playing() == channels.packFor (2), "...with its pack");

    // Each channel as it was left.
    const float first = drive->getValue();
    drive->setValueNotifyingHost (0.8f);
    asHost (0);
    engine.pump();
    check (std::abs (drive->getValue() - 0.8f) > 0.01f, "another channel has a DRIVE of its own");
    drive->setValueNotifyingHost (0.3f);
    asHost (2);
    engine.pump();
    check (std::abs (drive->getValue() - 0.8f) < 1.0e-4f, "a channel comes back with the DRIVE it was left with");
    asHost (0);
    engine.pump();
    check (std::abs (drive->getValue() - 0.3f) < 1.0e-4f, "...and so does the other");
    juce::ignoreUnused (first);

    // A hand's switch: one step, and undone whole.
    asHost (2);
    ticks();
    const int depth = history.undoDepth();
    check (engine.go (1), "a hand takes the lever to another channel");
    check (channels.active() == 1 && asked() == 1, "...and CHANNEL goes with it at once");
    ticks();
    check (history.undoDepth() == depth + 1, "...as one step of the history");
    history.undo();
    ticks();
    check (channels.active() == 2 && asked() == 2, "undo brings the channel back, parameter and sound together");
    check (playing() == channels.packFor (2), "...and its pack");
    check (history.undoDepth() == depth, "...leaving no step of its own behind");

    // A state from before there was a parameter: it says its channel on the node, and nowhere else.
    {
        auto old = amp->apvts.copyState();
        if (auto word = old.getChildWithProperty ("id", catpit::Engine::channelId); word.isValid())
            old.removeChild (word, nullptr);
        if (auto node = old.getChildWithName (catpit::Channels::nodeType); node.isValid())
            node.setProperty ("active", 3, nullptr);

        const int before = history.undoDepth();
        history.applyEdit (history.active(), old, "an older state");
        check (channels.active() == 3 && asked() != 3, "an older state lands on a channel the parameter does not say");
        ticks();
        check (channels.active() == 3 && asked() == 3, "...the parameter follows the state, not the other way round");
        check (history.undoDepth() == before + 1, "...and following it is not a step of its own");
    }

    // A channel whose pack is not installed.
    {
        const auto all = amp->preamp.packs;
        const int from = channels.active();
        const int gone = from == 1 ? 0 : 1;
        amp->preamp.packs.remove (channels.packFor (gone));
        const bool missing = channels.packFor (gone) < 0;

        asHost (gone);
        engine.pump();
        check (missing && channels.active() == from, "a channel whose pack is not installed is refused");
        check (asked() == from, "...and CHANNEL goes back to where the sound stayed");

        amp->preamp.packs = all;
    }

    // The capture DRIVE stands on says where the gear's own knob stood for it, as an hour.
    {
        asHost (0);
        ticks();

        const auto hourAt = [&amp, drive] (float travel)
        {
            drive->setValueNotifyingHost (travel);
            amp->pumpDeviceWork();
            return amp->preamp.captureOnDial().hour;
        };

        check (std::abs (hourAt (1.0f) - 5.0) < 1.0e-9, "the capture at the top of DRIVE was shot at five o'clock");
        check (std::abs (hourAt (0.5f) - 12.0) < 1.0e-9, "...the one in the middle at twelve");
        check (std::abs (hourAt (0.0f) - 9.0) < 1.0e-9, "...and the bottom plays the one shot at nine, fed softer");
        ticks();
    }

    // THE PLAYER'S OWN CABINETS — only where the gate has been told the home is its own to write in
    // (CATPIT_GATE_OWNS_HOME): this takes files into the player's IRs folder, and on a player's machine
    // that folder is the player's.
    if (juce::SystemStats::getEnvironmentVariable ("CATPIT_GATE_OWNS_HOME", {}).isNotEmpty())
    {
        const int factory = channels.factoryCabinets();
        const int before  = channels.cabinets().size();
        check (factory > 0, "the build's own cabinets are there");

        const auto run = juce::String::toHexString (juce::Random::getSystemRandom().nextInt());
        const auto scratch = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("catpit-channel-test-" + run);
        scratch.createDirectory();

        // Two IRs under names that would stand FIRST and last in one list, and one too big to keep.
        // Each is one of the build's with its last sample nudged: an IR of its own, like no other here.
        const auto early = scratch.getChildFile ("00-first-" + run + ".wav");
        const auto late  = scratch.getChildFile ("zz-last-" + run + ".wav");
        const auto big   = scratch.getChildFile ("too-big-" + run + ".wav");
        const auto nudged = [&channels] (const juce::File& to, char by)
        {
            juce::MemoryBlock bytes;
            channels.cabinets().getFirst().loadFileAsData (bytes);
            if (bytes.getSize() > 0)
                bytes[bytes.getSize() - 1] = (char) (bytes[bytes.getSize() - 1] ^ by);
            to.replaceWithData (bytes.getData(), bytes.getSize());
        };
        nudged (early, 1);
        nudged (late, 2);
        {
            juce::FileOutputStream out (big);
            out.setPosition (device::EmbeddedIrs::maxBytes);
            out.writeByte (0);
        }

        const auto added = channels.importCabinets ({ early.getFullPathName(), late.getFullPathName(), big.getFullPathName() });
        check (added.cabinets == 2 && channels.cabinets().size() == before + 2, "two IRs taken in are two more cabinets");
        check (channels.factoryCabinets() == factory, "...and the build's own are as many as they were");
        check (added.first >= factory && channels.cabinets()[added.first].getFileName() == early.getFileName(),
               "...a name that would sort first still stands after the build's own");
        check (added.refused.size() == 1 && added.refused[0] == big.getFileName(), "an IR too big for a session is refused, by name");

        channels.chooseCabinet (added.first);
        check (channels.cabinet() == added.first, "a cabinet taken in plays");

        // RENAMED, it is still that cabinet — to the channel that was left on it, and to a state kept
        // from before, which names it as it was called then and carries what it is.
        ticks();
        const int home  = channels.active();
        const int other = home == 0 ? 1 : 0;
        const auto kept = amp->apvts.copyState();           // this channel, on this cabinet, under its first name
        engine.go (other);
        const auto elsewhere = channels.cabinet();

        check (! channels.renameCabinet (0, "nobody"), "one of the build's own cannot be renamed");
        check (channels.renameCabinet (added.first, "mm-renamed-" + run), "one of the player's own can");

        int now = -1;
        for (int i = 0; i < channels.cabinets().size(); ++i)
            if (channels.cabinets().getReference (i).getFileNameWithoutExtension() == "mm-renamed-" + run)
                now = i;
        check (now >= factory, "...and is in the list under its new name, among the player's");
        check (channels.cabinet() == elsewhere, "...while the cabinet that is playing stays the one it was");

        engine.go (home);
        check (channels.cabinet() == now, "the channel left on it comes back on it");

        history.applyEdit (history.active(), kept, "a state from before the rename");
        check (channels.cabinet() == now, "a state that names it as it was called is on it too, found by what it is");
        check (channels.cabinetIsStale(), "...and that name is the stale part");
        channels.align();
        check (channels.cabinet() == now && ! channels.cabinetIsStale(), "...which settling puts right");
    }

    // WHAT EACH CHANNEL OPENS ON. INIT forgets every channel, so each of them is new again.
    {
        auto* room  = amp->apvts.getParameter (params::reverbType);
        auto* delay = amp->apvts.getParameter (params::delayMix);
        const auto opensOn = [room, delay] (const char* reverb, float mix)
        {
            return room != nullptr && delay != nullptr && room->getCurrentValueAsText() == reverb
                && std::abs (delay->convertFrom0to1 (delay->getValue()) - mix) < 1.0e-3f;
        };

        catpit::Presets presets (*amp, channels);
        presets.init();
        ticks();
        check (channels.active() == 0 && opensOn ("Hall", 15.0f), "after INIT the first channel stands in a hall, with a little delay");
        const int opening = channels.defaultCabinet();
        bool onIt = channels.cabinet() == opening;
        engine.go (1);
        check (opensOn ("Hall", 15.0f), "...and so does the second");
        onIt = onIt && channels.cabinet() == opening;
        engine.go (2);
        check (opensOn ("Spring", 0.0f), "the third opens on a spring, and no delay");
        onIt = onIt && channels.cabinet() == opening;
        engine.go (3);
        check (opensOn ("Ambience", 0.0f), "...the fourth on an ambience, and none either");
        onIt = onIt && channels.cabinet() == opening;
        check (opening == 0 && onIt, "...and all four on the first cabinet of the build's");

        delay->setValueNotifyingHost (delay->convertTo0to1 (40.0f));
        engine.go (0);
        engine.go (3);
        check (opensOn ("Ambience", 40.0f), "what a hand has set on a channel is kept over what it opens on");

        // The cabinet out of the sound: it stays out when a window opens, and INIT brings it back.
        channels.setCabinetOn (false);
        engine.setUpChain (false);
        check (! channels.cabinetOn(), "a cabinet bypassed stays bypassed when the face comes up");
        engine.go (0);
        check (! channels.cabinetOn(), "...and on every channel");
        presets.init();
        ticks();
        check (channels.cabinetOn(), "...until INIT, which plays it again");
        ticks();
    }

    std::printf ("%s\n", failed == 0 ? "PASSED" : "FAILED");
    return failed == 0 ? 0 : 1;
}
