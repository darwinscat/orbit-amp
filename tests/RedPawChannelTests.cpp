// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Gate for THE ROVER'S CHANNELS WITH NO WINDOW (channels/Engine.h, over redpaw/RedPawLineup.h): six
// channels on the Victory V4's six packs. A plug-in's window is shut for most of its life, and what a
// host can reach then is the processor's parameters: so a channel has to be something the processor
// does by itself. No window is built here at all.
//
// What it holds the engine to:
//   - a processor just built IS the rover — chain, channel, pack — and that is not an edit;
//   - CHANNEL moved the way a host moves it switches the channel, pack and all;
//   - each channel comes back as it was left;
//   - a hand's switch is one step of the history, and undoing it brings parameter and sound back together;
//   - a state from before there was a parameter keeps its channel, and the parameter following it is
//     not a step of its own;
//   - a channel whose pack is not installed is refused, and the parameter says where the sound stayed;
//   - a channel's pack is the capture it holds: renamed, the file is that channel's pack still;
//   - a channel's tone knobs are its pack's Bass, Middle and Treble, found by name — the Copper's
//     pack lists them in an order of its own;
//   - the six channels are three gears of two: a gear's pair is its pedal's two channels;
//   - PARK takes the whole amp out of the sound: the guitar comes through dry, the channel waits;
//
//   - an IR a player adds is a cabinet of their own: after the build's, whatever it is called;
//   - a cabinet another product lays out in the family's folder is not one of the rover's;
//   - a channel never pulled opens on its own reverb, its own share of it and of delay.
//
// It reads the rover's packs from where the rover reads them, and lays the build's set out there
// first, as the rover does.

#include "PluginProcessor.h"
#include "Parameters.h"
#include "device/FactorySet.h"
#include "channels/Engine.h"
#include "channels/Presets.h"
#include "redpaw/RedPawLineup.h"

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

    auto& engine   = channels::Engine::of (*amp);
    auto& channels = engine.channels;
    auto& history  = amp->history;
    auto* channel  = amp->apvts.getParameter (engine.channelId());
    auto* drive    = amp->apvts.getParameter (params::preampGain);
    auto* device   = amp->apvts.getParameter (params::preampDevice);
    if (channel == nullptr || drive == nullptr || device == nullptr)
    {
        std::printf ("FAIL  the rover's processor has CHANNEL, DRIVE and a device to play\nFAILED\n");
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
    for (int c = 0; c < channels.count(); ++c)
        installed = installed && channels.packFor (c) >= 0;
    check (channels.count() == 6 && installed, "all six channels' packs are installed");

    // BASS, MIDDLE and TREBLE are found in each pack by their names. The Sheriff's and the Kraken's list
    // them first, in that order; the Copper's two list Tone, Treble, Middle, Bass — so there BASS is the
    // fourth place, MIDDLE the third and TREBLE the second.
    {
        bool found = true;
        for (int c = 0; c < channels.count(); ++c)
        {
            const bool copper = redpaw::gearOf (c) == 0;
            const int places[] { copper ? 3 : 0, copper ? 2 : 1, copper ? 1 : 2 };
            for (int t = 0; t < 3; ++t)
                found = found && channels.toneOf (c, t) == params::preampMeasured (places[t]);
        }
        check (found, "every channel's BASS, MIDDLE and TREBLE are its pack's own, found by name wherever its list has them");
    }

    // THREE GEARS OF TWO: CLEAN the Copper and the Copper TB, CRUNCH the Sheriff's two, HI-GAIN the
    // Kraken's two — TURBO the second of each.
    {
        const char* pairs[][2] { { "v4-copper-4ae0", "v4-copper-tb-5841" }, { "v4-sheriff-ch1-82ab", "v4-sheriff-ch2-2f2f" },
                                 { "v4-kraken-ch1-f7fc", "v4-kraken-ch2-5f71" } };
        bool paired = true;
        for (int g = 0; g < redpaw::numGears; ++g)
            for (int t = 0; t < 2; ++t)
            {
                const int c = redpaw::channelOf (g, t == 1);
                paired = paired && redpaw::gearOf (c) == g && redpaw::turboOf (c) == (t == 1)
                              && juce::String (redpaw::lineup().channels[(size_t) c].capture) == pairs[g][t];
            }
        check (paired, "each gear is a pedal's two channels, TURBO the second");
    }

    // A processor just built.
    check (channels.adopted(), "a processor just built is the rover's already");
    check (channels.active() == 0 && asked() == 0, "...on the first channel, and CHANNEL says so");
    check (playing() == channels.packFor (0), "...playing that channel's pack");
    ticks();
    check (! history.canUndo(), "...and none of that is an edit");

    // PARK: the lever's fourth position, the whole amp out of the sound —
    // a switch of the rover's own. Parked, the guitar comes out as it went in, only as late as the
    // chain always is; the channel waits.
    {
        auto* park = amp->apvts.getParameter (engine.parkId());
        check (park != nullptr && ! amp->isParked(), "the rover has PARK, and a processor just built plays");

        if (park != nullptr)
        {
            constexpr double rate = 44100.0;       // not the packs' own: the captures are resampled, and the chain is late
            constexpr int block = 512, blocks = 24;
            amp->prepareToPlay (rate, block);
            for (int i = 0; i < 10; ++i)        // the captures land, resampled to the session's rate
            {
                amp->pumpDeviceWork();
                juce::Thread::sleep (5);
            }
            int late = amp->getLatencySamples();

            // No room and no echo, so that nothing rings on into the parked blocks.
            for (const auto* id : { params::reverbMix, params::delayMix })
                if (auto* p = amp->apvts.getParameter (id))
                    p->setValueNotifyingHost (0.0f);

            std::vector<float> in ((size_t) (block * blocks)), out (in.size());
            for (size_t n = 0; n < in.size(); ++n)
                in[n] = 0.25f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * (double) n / rate);

            juce::MidiBuffer none;
            const auto play = [&]
            {
                for (int b = 0; b < blocks; ++b)
                {
                    juce::AudioBuffer<float> buf (2, block);
                    for (int ch = 0; ch < 2; ++ch)
                        buf.copyFrom (ch, 0, in.data() + b * block, block);
                    amp->processBlock (buf, none);
                    std::copy (buf.getReadPointer (0), buf.getReadPointer (0) + block, out.begin() + b * block);
                }

                // Past the first blocks, which the fades and the wires settle in.
                float worst = 0.0f;
                for (size_t n = (size_t) (block * 8); n < out.size(); ++n)
                    worst = juce::jmax (worst, std::abs (out[n] - in[n - (size_t) late]));
                return worst;
            };

            const float playing = play();
            check (playing > 0.01f, "playing, the amp is in the sound: what comes out is not what went in");
            amp->pumpDeviceWork();
            late = amp->getLatencySamples();       // the captures have landed by now, at the session's rate

            const int was = channels.active();
            park->setValueNotifyingHost (1.0f);
            check (amp->isParked() && channels.active() == was, "PARK thrown, the processor is parked and the channel waits");
            amp->pumpDeviceWork();
            check (amp->getLatencySamples() == late, "...as late as it was playing");

            const float worst = play();
            std::printf ("      parked: %d samples late, the worst sample %.2g off the input\n", late, (double) worst);
            check (worst < 1.0e-3f, "parked, the input comes out as it went in, as late as the chain reports");

            park->setValueNotifyingHost (0.0f);
            check (! amp->isParked(), "...and the amp plays again when PARK is let go");
            amp->releaseResources();
        }
    }
    ticks();

    // A host moves the parameter.
    asHost (5);
    check (channels.active() == 0, "CHANNEL moved by a host changes nothing until the engine's own tick");
    engine.pump();
    check (channels.active() == 5 && asked() == 5, "...and then the channel is the one asked for");
    check (playing() == channels.packFor (5), "...with its pack");
    asHost (2);
    engine.pump();

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
        if (auto word = old.getChildWithProperty ("id", juce::String (engine.channelId())); word.isValid())
            old.removeChild (word, nullptr);
        if (auto node = old.getChildWithName (channels.nodeType()); node.isValid())
            node.setProperty ("active", 4, nullptr);

        const int before = history.undoDepth();
        history.applyEdit (history.active(), old, "an older state");
        check (channels.active() == 4 && asked() != 4, "an older state lands on a channel the parameter does not say");
        ticks();
        check (channels.active() == 4 && asked() == 4, "...the parameter follows the state, not the other way round");
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

    // A channel's pack is the capture it holds, not its file's name: the same six under names that
    // say nothing, in a folder of the gate's own, are the six channels' packs still.
    {
        const auto all = amp->preamp.packs;
        const auto renamed = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                 .getChildFile ("redpaw-renamed-test-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt()));
        const auto nameOf = [] (int c) { return "Renamed " + juce::String (c) + ".orbitrig.zip"; };

        bool copied = renamed.createDirectory().wasOk();
        for (int c = 0; c < channels.count(); ++c)
            copied = copied && channels.packFor (c) >= 0
                            && all.getReference (channels.packFor (c)).location.copyFileTo (renamed.getChildFile (nameOf (c)));

        amp->preamp.packs = device::DeviceLibrary::scan (device::DeviceLibrary::Slot::preamp, true,
                                                         renamed, renamed.getChildFile ("none"));
        bool found = copied;
        for (int c = 0; c < channels.count(); ++c)
            found = found && channels.packFor (c) >= 0
                          && amp->preamp.packs.getReference (channels.packFor (c)).location.getFileName() == nameOf (c);
        check (found, "a channel's pack is found by the capture it holds, whatever its file is called");

        amp->preamp.packs = all;
    }

    // THE PLAYER'S OWN CABINETS — only where the gate has been told the home is its own to write in
    // (REDPAW_GATE_OWNS_HOME): this takes files into the player's IRs folder, and on a player's machine
    // that folder is the player's.
    if (juce::SystemStats::getEnvironmentVariable ("REDPAW_GATE_OWNS_HOME", {}).isNotEmpty())
    {
        const int factory = channels.factoryCabinets();
        const int before  = channels.cabinets().size();
        check (factory > 0, "the build's own cabinets are there");

        // ANOTHER PRODUCT'S CABINET in the family's folder, beside the build's own, under a name that
        // would stand first among them: it is not one of the rover's. It goes again with that
        // product's own list.
        {
            const juce::String bytes ("another product's cabinet");
            const auto family = device::DeviceLibrary::factoryDirectory();
            device::FactorySet::layOut (family, "AnotherAmp", 1, { { "IRs/00-another.wav", bytes.toRawUTF8(), (size_t) bytes.length() } });
            channels.readCabinets();

            bool notOurs = family.getChildFile ("IRs/00-another.wav").existsAsFile() && channels.factoryCabinets() == factory
                        && channels.cabinets().size() == before;
            for (const auto& c : channels.cabinets())
                notOurs = notOurs && c.getFileName() != "00-another.wav";
            check (notOurs, "a cabinet another product lays out beside the build's own is not one of the rover's");

            device::FactorySet::layOut (family, "AnotherAmp", 1, {});
            channels.readCabinets();
        }

        const auto run = juce::String::toHexString (juce::Random::getSystemRandom().nextInt());
        const auto scratch = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("redpaw-channel-test-" + run);
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

    // WHAT EACH CHANNEL OPENS ON. INIT forgets every channel, so each of them is new again: a pedal's
    // first channel a little of its room and as much delay, its TURBO twice as much of both — the
    // Copper in a hall, the Sheriff on a plate, the Kraken in an ambience with no delay.
    {
        auto* room  = amp->apvts.getParameter (params::reverbType);
        auto* mix   = amp->apvts.getParameter (params::reverbMix);
        auto* delay = amp->apvts.getParameter (params::delayMix);
        const auto opensOn = [room, mix, delay] (const char* reverb, float wet, float echo)
        {
            const auto plain = [] (juce::RangedAudioParameter* p) { return p->convertFrom0to1 (p->getValue()); };
            return room != nullptr && mix != nullptr && delay != nullptr && room->getCurrentValueAsText() == reverb
                && std::abs (plain (mix) - wet) < 1.0e-3f && std::abs (plain (delay) - echo) < 1.0e-3f;
        };

        orbitamp::channels::Presets presets (*amp, channels);
        presets.init();
        ticks();

        struct Opening { const char* reverb; float wet, echo; };
        const Opening openings[] { { "Hall", 15.0f, 15.0f }, { "Hall", 30.0f, 30.0f }, { "Plate", 15.0f, 15.0f },
                                   { "Plate", 30.0f, 30.0f }, { "Ambience", 15.0f, 0.0f }, { "Ambience", 30.0f, 0.0f } };
        const int opening = channels.defaultCabinet();
        bool each = channels.active() == 0, onIt = true;
        for (int c = 0; c < channels.count(); ++c)
        {
            engine.go (c);
            each = each && opensOn (openings[c].reverb, openings[c].wet, openings[c].echo);
            onIt = onIt && channels.cabinet() == opening;
        }
        check (each, "after INIT each of the six opens on its own room, its own share of it and of delay");
        check (opening >= 0 && opening < channels.factoryCabinets()
                   && channels.cabinetStem (channels.cabinets()[opening]) == "dark-matter" && onIt,
               "...and all six on the build's DARK MATTER");

        delay->setValueNotifyingHost (delay->convertTo0to1 (40.0f));
        engine.go (0);
        engine.go (5);
        check (opensOn ("Ambience", 30.0f, 40.0f), "what a hand has set on a channel is kept over what it opens on");
        ticks();
    }

    std::printf ("%s\n", failed == 0 ? "PASSED" : "FAILED");
    return failed == 0 ? 0 : 1;
}
