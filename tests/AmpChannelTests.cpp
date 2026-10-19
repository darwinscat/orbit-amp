// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Gate for AN AMP'S CHANNELS WITH NO WINDOW (channels/Engine.h, over the product's lineup): the
// product's channels on its packs — the X88-IR's four in the cockpit, the Friedman IR-X's two on Mars,
// the Victory V4's on the other rovers — as many as it plays: GATE_CHANNELS, which the build says for
// each (CMakeLists.txt: amp_console). A plug-in's window is shut for most of its life, and what a host
// can reach then is the processor's parameters: so a channel has to be something the processor does
// by itself. No window is built here at all.
//
// What it holds the engine to:
//   - a processor just built IS the amp — chain, channel, pack — and that is not an edit;
//   - CHANNEL moved the way a host moves it switches the channel, pack and all;
//   - each channel comes back as it was left;
//   - a hand's switch is one step of the history, and undoing it brings parameter and sound back together;
//   - a state from before there was a parameter keeps its channel, and the parameter following it is
//     not a step of its own;
//   - a channel whose pack is not installed is refused, and the parameter says where the sound stayed;
//   - a channel's pack is the capture it holds: renamed, the file is that channel's pack still;
//   - a channel's tone knobs are its pack's Bass, Middle and Treble, found by name — the Copper's
//     pack lists them in an order of its own;
//   - where the channels go in pairs, the rover's gears: on the V4 a gear's pair is its pedal's two
//     channels, on the IR-X one channel of it with its Boost off and on — and TURBO is an option of the
//     gear's, one memory;
//   - a switch a channel holds stands where the lineup puts it, whatever a host did to it; the pack's
//     other switches are each channel's own, and a channel never pulled opens them where the pack does;
//   - PARK, where the amp has it, takes the whole amp out of the sound: the guitar comes through dry,
//     the channel waits;
//   - DRIVE, where its packs are captures shot at several hours of the gear's knob, stands on the one
//     its travel says;
//
//   - an IR a player adds is a cabinet of their own: after the build's, whatever it is called;
//   - a cabinet another product lays out in the family's folder is not one of the amp's;
//   - a channel never pulled opens on its own reverb, its own share of it and of delay;
//   - a cabinet bypassed stays out of the sound when a window opens, until INIT.
//
// It reads the amp's packs from where the amp reads them, and lays the build's set out there first, as
// the amp does. Every amp over the channels' engine runs it as itself — CatpitAmp, RedPawAmp,
// FrostPawAmp, AmberPawAmp (CMakeLists.txt: amp_console), each as AmpGate.h knows it.

#include "AmpGate.h"
#include "PluginProcessor.h"
#include "Parameters.h"
#include "device/FactorySet.h"
#include "device/FactoryShelf.h"
#include "device/IrLibrary.h"
#include "channels/Engine.h"
#include "channels/Presets.h"

#include <cmath>
#include <cstdio>

namespace
{
    /** A run's own mark in an IR's last samples: the gates of every product share the family's IRs
        folder, and one IR left behind twice, by two runs, would be one sound under two names. */
    void markOfRun (juce::MemoryBlock& bytes, const juce::String& run)
    {
        const auto mark = (juce::uint32) run.getHexValue32();
        for (size_t b = 0; b < 4 && b + 2 <= bytes.getSize(); ++b)
            bytes[bytes.getSize() - 2 - b] = (char) (bytes[bytes.getSize() - 2 - b] ^ (char) (mark >> (8 * b)));
    }

    /** The channel a gear and TURBO make, where the channels go in pairs: a gear's two stand side by
        side in the lineup, TURBO the second (redpaw/RedPawLineup.h). */
    constexpr int channelOf (int gear, bool turbo) noexcept { return gear * 2 + (turbo ? 1 : 0); }
    constexpr bool turboOf (int channel) noexcept           { return channel % 2 == 1; }
}

int main()
{
    using namespace orbitamp;
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto& product = gate::product();
    const auto  played  = gate::played();
    const juce::String ours (product.amp);
    const juce::String scratchName (juce::String (product.name).toLowerCase());
    gate::Checks check;

    // AN IR OF BEFORE, in the product's own folder, for the first start to gather into the family's — only
    // where the home is the gate's own (product.ownsHome), and as if this product had never gathered here.
    const bool ownsHome = juce::SystemStats::getEnvironmentVariable (product.ownsHome, {}).isNotEmpty();
    const auto seedRun  = juce::String::toHexString (juce::Random::getSystemRandom().nextInt());
    const auto formerIr = device::IrLibrary::formerDirectory().getChildFile ("Old Pack").getChildFile ("gathered-" + seedRun + ".wav");
    if (ownsHome)
    {
        // An IR of its own: the shelf's DARK MATTER with its last sample nudged, like no other here.
        const auto& shelf = device::shelf::irs()[(size_t) params::cabIrDefault];
        juce::MemoryBlock bytes (shelf.data, (size_t) shelf.size);
        bytes[bytes.getSize() - 1] = (char) (bytes[bytes.getSize() - 1] ^ 4);
        markOfRun (bytes, seedRun);
        formerIr.getParentDirectory().createDirectory();
        formerIr.replaceWithData (bytes.getData(), bytes.getSize());
        prefs::setBool (prefs::irsGathered, false);
    }

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
        std::printf ("FAIL  the %s's processor has CHANNEL, DRIVE and a device to play\nFAILED\n", product.amp);
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
    const int last  = channels.count() - 1;
    const int gears = product.gears != nullptr ? channels.count() / 2 : 0;
    if (product.gears != nullptr)
        std::printf ("      %d channels, %d gears\n", channels.count(), gears);
    else
        std::printf ("      %d channels\n", channels.count());
    check (channels.count() == GATE_CHANNELS && channels.count() <= (int) product.channels.size() && installed, product.installed);

    // BASS, MIDDLE and TREBLE are found in each pack by their names. The X88-IR's, the IR-X's, the
    // Sheriff's and the Kraken's list them first, in that order; the Copper's two list Tone, Treble,
    // Middle, Bass — so there BASS is the fourth place, MIDDLE the third and TREBLE the second.
    {
        bool found = true;
        for (int c = 0; c < channels.count(); ++c)
        {
            const bool copper = juce::String (channels.lineup().channels[(size_t) c].capture).startsWith ("v4-copper");
            const int places[] { copper ? 3 : 0, copper ? 2 : 1, copper ? 1 : 2 };
            for (int t = 0; t < 3; ++t)
                found = found && channels.toneOf (c, t) == params::preampMeasured (places[t]);
        }
        check (found, product.tones);
    }

    // GEARS OF TWO, where the product's channels go in pairs, as far as it goes, TURBO the second of
    // each, and the pair one memory (Lineup::pairs). On the V4, a pedal's two channels: CLEAN the Copper
    // and the Copper TB, CRUNCH the Sheriff's two, HI-GAIN the Kraken's two. On the IR-X, one of its
    // channels with the Boost held off, then on: CRUNCH Ch1, HI-GAIN Ch2.
    if (product.gears != nullptr)
    {
        bool paired = channels.lineup().pairs && gears * 2 == GATE_CHANNELS && gears * 2 <= (int) product.channels.size();
        for (int g = 0; paired && g < gears; ++g)
            for (int t = 0; t < 2; ++t)
            {
                const int c = channelOf (g, t == 1);
                const auto& own = channels.lineup().channels[(size_t) c];
                const juce::String held = own.held.control == nullptr ? juce::String() : juce::String (own.held.control) + "=" + own.held.value;
                paired = paired && channels.memoryOf (c) == channelOf (g, false)
                              && juce::String (own.capture) == product.channels[(size_t) c].capture
                              && held == product.channels[(size_t) c].held;
            }
        check (paired, product.gears);
    }

    // A processor just built.
    check (channels.adopted(), "a processor just built is the " + ours + "'s already");
    check (channels.active() == 0 && asked() == 0, "...on the first channel, and CHANNEL says so");
    check (playing() == channels.packFor (0), "...playing that channel's pack");
    ticks();
    check (! history.canUndo(), "...and none of that is an edit");

    // THE SHELF'S `Other` IS ORBITAMP'S (device/FactoryShelf.h): the amp carries Darwin's Cat's own
    // cabinets and not a byte of the third-party ones — their places in `cab_ir`'s order held empty — and a
    // host that moves `cab_ir` to one of those places hears DARK MATTER, not a cabinet that is not there.
    {
        bool oursAlone = device::shelf::irs().size() == (size_t) params::cabIrNames.size();
        for (const auto& ir : device::shelf::irs())
            oursAlone = oursAlone && (ir.data != nullptr) == (juce::String (ir.folder) == device::shelf::ours);
        check (oursAlone, "the " + ours + " carries the shelf's Orange PPC212V and nothing of Other, every number in its place");
        check (AmpProcessor::cabIrBytes (0).data == AmpProcessor::cabIrBytes (params::cabIrDefault).data,
               "...and a number of Other's plays DARK MATTER");
    }

    // PARK, where the product has it: the lever's top end, the whole amp out of the sound — a switch
    // of the product's own. Parked, the guitar comes out as it went in, only as late as the chain always
    // is; the channel waits.
    {
        auto* park = engine.parkId() != nullptr ? amp->apvts.getParameter (engine.parkId()) : nullptr;
        check ((park != nullptr) == product.park && ! amp->isParked(),
               product.park ? "the " + ours + " has PARK, and a processor just built plays"
                            : "the " + ours + " has no PARK, and a processor just built plays");

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

            // ...through the player's own levels: a master turned down holds the dry guitar down too.
            if (auto* master = amp->apvts.getParameter (params::outTrim))
            {
                const float was = master->getValue();
                master->setValueNotifyingHost (master->convertTo0to1 (-12.0f));
                play();                              // the master's ramp settles
                play();
                const float g = juce::Decibels::decibelsToGain (-12.0f);
                float down = 0.0f;
                for (size_t n = (size_t) (block * 8); n < out.size(); ++n)
                    down = juce::jmax (down, std::abs (out[n] - g * in[n - (size_t) late]));
                std::printf ("      parked under OUT at -12 dB: the worst sample %.2g off the input 12 dB down\n", (double) down);
                check (down < 1.0e-3f, "parked, OUT still holds the level: at -12 dB the dry input comes out 12 dB down");
                master->setValueNotifyingHost (was);
                play();
            }

            park->setValueNotifyingHost (0.0f);
            check (! amp->isParked(), "...and the amp plays again when PARK is let go");
            amp->releaseResources();
        }
    }
    ticks();

    // A host moves the parameter.
    asHost (last);
    check (channels.active() == 0, "CHANNEL moved by a host changes nothing until the engine's own tick");
    engine.pump();
    check (channels.active() == last && asked() == last, "...and then the channel is the one asked for");
    check (playing() == channels.packFor (last), "...with its pack");
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

    // TURBO IS AN OPTION OF THE GEAR'S, where the channels go in pairs, as its tone is: a gear and its
    // TURBO are one memory. Thrown, only the pack changes — DRIVE, the delay and BASS stand as they were,
    // BASS on its knob in the other pack's place — and a gear pulled comes back with TURBO as it was left.
    if (product.gears != nullptr)
    {
        auto* echo = amp->apvts.getParameter (params::delayMix);
        const auto knob = [&amp, &channels] (int c, int t)
        {
            auto* p = amp->apvts.getParameter (channels.toneOf (c, t));
            return p != nullptr ? p->getValue() : -1.0f;
        };
        const int gear = 0, plain = channelOf (gear, false), turbo = channelOf (gear, true);

        engine.go (plain);
        drive->setValueNotifyingHost (0.62f);
        echo->setValueNotifyingHost (0.27f);
        if (auto* bass = amp->apvts.getParameter (channels.toneOf (plain, 0)))
            bass->setValueNotifyingHost (0.81f);
        engine.go (turbo);
        check (channels.active() == turbo && playing() == channels.packFor (turbo), "TURBO thrown plays the gear's TURBO pack");
        check (std::abs (drive->getValue() - 0.62f) < 1.0e-4f && std::abs (echo->getValue() - 0.27f) < 1.0e-4f,
               "...on the gear's DRIVE and delay, as they stood");
        check (std::abs (knob (turbo, 0) - 0.81f) < 1.0e-4f, "...and its BASS on the gear's, in the TURBO pack's own place");

        drive->setValueNotifyingHost (0.44f);
        engine.go (plain);
        check (std::abs (drive->getValue() - 0.44f) < 1.0e-4f && std::abs (knob (plain, 0) - 0.81f) < 1.0e-4f,
               "TURBO let go: what was turned under it stays, the pack the gear's again");

        engine.go (turbo);
        engine.go (channelOf (1, false));
        check (channels.leftOn (plain) == turbo, "a gear left under TURBO is left so");
        engine.go (channels.leftOn (plain));
        check (channels.active() == turbo && std::abs (drive->getValue() - 0.44f) < 1.0e-4f,
               "...and pulled again comes back under it, on what it was left with");
        engine.go (plain);
        engine.go (channelOf (1, false));
        check (channels.leftOn (plain) == plain, "...and a gear left without it, without it");
        ticks();
    }

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
            node.setProperty ("active", last, nullptr);

        const int before = history.undoDepth();
        history.applyEdit (history.active(), old, "an older state");
        check (channels.active() == last && asked() != last, "an older state lands on a channel the parameter does not say");
        ticks();
        check (channels.active() == last && asked() == last, "...the parameter follows the state, not the other way round");
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

    // A channel's pack is the capture it holds, not its file's name: the same packs under names that
    // say nothing, in a folder of the gate's own, are the channels' packs still.
    {
        const auto all = amp->preamp.packs;
        const auto renamed = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                 .getChildFile (scratchName + "-renamed-test-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt()));
        // One file to a pack — two channels may play the same one — named after the pack, not the channel.
        std::vector<juce::String> names;
        for (int c = 0; c < channels.count(); ++c)
            names.push_back ("Renamed " + juce::String (channels.packFor (c)) + ".orbitrig.zip");
        const auto nameOf = [&names] (int c) { return names[(size_t) c]; };

        bool copied = renamed.createDirectory().wasOk();
        for (int c = 0; c < channels.count(); ++c)
            copied = copied && channels.packFor (c) >= 0
                            && (renamed.getChildFile (nameOf (c)).existsAsFile()
                                || all.getReference (channels.packFor (c)).location.copyFileTo (renamed.getChildFile (nameOf (c))));

        amp->preamp.packs = device::DeviceLibrary::scan (device::DeviceLibrary::Slot::preamp, true,
                                                         renamed, renamed.getChildFile ("none"));
        bool found = copied;
        for (int c = 0; c < channels.count(); ++c)
            found = found && channels.packFor (c) >= 0
                          && amp->preamp.packs.getReference (channels.packFor (c)).location.getFileName() == nameOf (c);
        check (found, "a channel's pack is found by the capture it holds, whatever its file is called");

        amp->preamp.packs = all;
    }

    // The capture DRIVE stands on says where the gear's own knob stood for it, as an hour — where the
    // product's packs are captures shot at several hours of it.
    if (! product.dial.empty())
    {
        asHost (0);
        ticks();

        const auto hourAt = [&amp, drive] (float travel)
        {
            drive->setValueNotifyingHost (travel);
            amp->pumpDeviceWork();
            return amp->preamp.captureOnDial().hour;
        };

        for (const auto& mark : product.dial)
            check (std::abs (hourAt (mark.travel) - mark.hour) < 1.0e-9, mark.said);
        ticks();
    }

    // THE PLAYER'S OWN CABINETS — only where the gate has been told the home is its own to write in
    // (product.ownsHome): this takes files into the player's IRs folder, and on a player's machine
    // that folder is the player's.
    if (ownsHome)
    {
        const int factory = channels.factoryCabinets();
        const int before  = channels.cabinets().size();
        check (factory > 0, "the build's own cabinets are there");

        // ANOTHER PRODUCT'S CABINET in the family's folder, beside the build's own, under a name that
        // would stand first among them: it is not one of the amp's. It goes again with that
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
            check (notOurs, "a cabinet another product lays out beside the build's own is not one of the " + ours + "'s");

            device::FactorySet::layOut (family, "AnotherAmp", 1, {});
            channels.readCabinets();
        }

        const auto run = juce::String::toHexString (juce::Random::getSystemRandom().nextInt());
        const auto scratch = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (scratchName + "-channel-test-" + run);
        scratch.createDirectory();

        // Two IRs under names that would stand FIRST and last in one list, and one too big to keep.
        // Each is one of the build's with its last sample nudged: an IR of its own, like no other here.
        const auto early = scratch.getChildFile ("00-first-" + run + ".wav");
        const auto late  = scratch.getChildFile ("zz-last-" + run + ".wav");
        const auto big   = scratch.getChildFile ("too-big-" + run + ".wav");
        const auto nudged = [&channels, &run] (const juce::File& to, char by)
        {
            juce::MemoryBlock bytes;
            channels.cabinets().getFirst().loadFileAsData (bytes);
            if (bytes.getSize() > 0)
                bytes[bytes.getSize() - 1] = (char) (bytes[bytes.getSize() - 1] ^ by);
            markOfRun (bytes, run);
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
        check (channels.cabinets()[added.first].getParentDirectory() == device::DeviceLibrary::familyRoot().getChildFile ("IRs"),
               "...and what is taken in lands in the family's own IRs folder, beside Factory");

        // THE IR OF BEFORE, gathered at the first start: copied into the family's folder, in its own
        // folder, and still where it was.
        const auto gathered = device::IrLibrary::directory().getChildFile ("Old Pack").getChildFile (formerIr.getFileName());
        check (gathered.existsAsFile() && formerIr.existsAsFile() && gathered.hasIdenticalContentTo (formerIr),
               "the player's IRs of before are copied into the family's folder, folders and all, and left where they were");
        check (channels.cabinets().indexOf (gathered) >= factory, "...and are among the player's cabinets");
        check (prefs::getBool (prefs::irsGathered, false), "...once: the product remembers it has gathered them");

        channels.chooseCabinet (added.first);
        check (channels.cabinet() == added.first, "a cabinet taken in plays");

        // RENAMED, it is still that cabinet — to the channel that was left on it, and to a state kept
        // from before, which names it as it was called then and carries what it is. The other channel
        // is one of another memory: a TURBO shares its gear's cabinet.
        ticks();
        const int home = channels.active();
        int other = 0;
        while (other < last && channels.memoryOf (other) == channels.memoryOf (home))
            ++other;
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

    // WHAT EACH CHANNEL OPENS ON. INIT forgets every channel, so each of them is new again: each on the
    // room, the share of it and the delay the product's table says (AmpGate.h) — a TURBO the same as its
    // gear, the gear's own.
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

        const int opening = channels.defaultCabinet();
        bool each = channels.active() == 0 && channels.count() <= (int) played.size(), onIt = true;
        for (int c = 0; each && c < channels.count(); ++c)
        {
            engine.go (c);
            const auto& o = played[(size_t) c];
            const bool opens = opensOn (o.reverb, o.wet, o.echo);
            if (o.opens != nullptr)
                check (opens, o.opens);
            each = each && opens;
            onIt = onIt && channels.cabinet() == opening;
        }
        check (each, "after INIT each channel opens on its own room, its own share of it and of delay");
        check (opening >= 0 && opening < channels.factoryCabinets()
                   && channels.cabinetStem (channels.cabinets()[opening]) == "dark-matter" && onIt,
               product.opening);

        delay->setValueNotifyingHost (delay->convertTo0to1 (40.0f));
        engine.go (0);
        engine.go (last);
        const auto& lastOpens = played[std::min ((size_t) last, played.size() - 1)];
        check (opensOn (lastOpens.reverb, lastOpens.wet, 40.0f), "what a hand has set on a channel is kept over what it opens on");

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

    // THE PACK'S SWITCHES (Channels::switchesOf), where a channel holds one — the IR-X's rover. Every
    // channel's Boost stands where the lineup puts it, and a host that throws it is overruled the next
    // time the channel is pulled; the pack's other switch is the gear's, its TURBO's too — one memory —
    // and a gear never pulled since INIT opens it where the pack does.
    bool holds = false;
    for (const auto& c : played)
        holds = holds || juce::String (c.held).isNotEmpty();
    if (holds)
    {
        const auto selector = [&amp] (int slot) { return amp->apvts.getParameter (params::selectorId (params::preampId, slot)); };
        const auto at  = [&selector] (int slot) { auto* p = selector (slot); return p != nullptr ? juce::roundToInt (p->convertFrom0to1 (p->getValue())) : -1; };
        const auto put = [&selector] (int slot, int to) { if (auto* p = selector (slot)) p->setValueNotifyingHost (p->convertTo0to1 ((float) to)); };

        orbitamp::channels::Presets presets (*amp, channels);
        presets.init();
        ticks();

        bool held = true, opens = true, free = false;
        for (int c = 0; c < channels.count(); ++c)
        {
            engine.go (c);
            for (const auto& s : channels.switchesOf (c))
                if (s.held)
                    held = held && at (s.slot) == s.opening && s.values[s.opening] == (turboOf (c) ? "on" : "off");
                else
                {
                    free = true;
                    opens = opens && at (s.slot) == s.opening;
                }
        }
        check (held, "every channel's Boost stands where the lineup puts it: off on a gear, on on its TURBO");
        check (free && opens, "...and its pack's other switch opens where the pack does");

        // Ch1's: the Boost held, the three-way Bright the hand's.
        int boost = -1, bright = -1;
        for (const auto& s : channels.switchesOf (0))
            (s.held ? boost : bright) = s.slot;
        check (boost >= 0 && bright >= 0 && channels.switchesOf (0)[(size_t) bright].values.size() == 3,
               "Ch1 holds its Boost and leaves its three-way Bright to the hand");

        engine.go (0);
        put (bright, 2);                // BRIGHT 2 on the gear
        engine.go (1);
        const bool sharedOnTurbo = at (bright) == 2 && at (boost) == 1;
        put (bright, 1);                // CENTER under its TURBO...
        put (boost, 0);                 // ...and a host throws the TURBO's Boost off
        engine.go (0);
        const bool backOnGear = at (bright) == 1 && at (boost) == 0;
        engine.go (2);                  // another gear, and back
        engine.go (0);
        const bool keptOnGear = at (bright) == 1 && at (boost) == 0;
        check (sharedOnTurbo, "a switch moved on a gear is its TURBO's too: one memory, the Boost held on");
        check (backOnGear && keptOnGear, "...and moved under TURBO, the gear's, kept; the Boost a host threw stands where the lineup holds it again");
        ticks();
    }

    return check.finish();
}
