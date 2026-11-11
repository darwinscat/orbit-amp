// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Gate for AN AMP'S CHANNELS WITH NO WINDOW (channels/Engine.h, over the product's lineup): the
// product's channels on its packs — the X88-IR's four in the cockpit, the Friedman IR-X's two on Mars,
// the ReVolt Guitar's three on the ice moon, the Victory V4's over Titan, the walker's B15 and SVT on the Ampeg
// SGT-DI — as many as it plays:
// GATE_CHANNELS, which the build says for
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
//   - a channel's tone knobs are its pack's tones, as many as it has, knob i its tone i — the Copper's
//     five, the ReVolt Clean's two — each turning its pack's own;
//   - where the channels go in pairs, the rover's gears: one channel of its pack with the Boost off and
//     on, on the IR-X, the ReVolt and the V4 alike — and TURBO is an option of the gear's, one memory;
//   - a switch a channel holds stands where the lineup puts it, whatever a host did to it; the pack's
//     other switches are each channel's own, and a channel never pulled opens them where the pack does;
//   - no product has PARK: every place of the rover's lever is a gear, and nothing of the product's
//     own takes the amp out of the sound;
//   - DRIVE, where its packs are captures shot at several hours of the gear's knob, stands on the one
//     its travel says;
//
//   - an IR a player adds is a cabinet of their own: after the build's, whatever it is called;
//   - a cabinet another product lays out in the family's folder is not one of the amp's;
//   - a channel never pulled opens on its own reverb, its own share of it and of delay;
//   - ...and each of its pack's tones where the pack starts it: a tone of positions on its reference one;
//   - a cabinet bypassed stays out of the sound when a window opens, until INIT.
//
// It reads the amp's packs from where the amp reads them, and lays the build's set out there first, as
// the amp does. Every amp over the channels' engine runs it as itself — CatpitAmp, RedPawAmp,
// FrostPawAmp, AmberPawAmp (CMakeLists.txt: amp_console), each as AmpGate.h knows it.

#include "GateMain.h"
#include "AmpGate.h"
#include "PluginProcessor.h"
#include "Parameters.h"
#include "device/FactoryShelf.h"
#include "device/IrLibrary.h"
#include "channels/Engine.h"
#include "channels/Presets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <vector>

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
}

int ORBITAMP_GATE_MAIN()
{
    using namespace orbitamp;
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto& product = gate::product();
    const auto  played  = gate::played();
    const juce::String ours (product.amp);
    const juce::String scratchName (juce::String (product.name).toLowerCase());
    gate::Checks check;

    // AN IR OF BEFORE, in the product's own folder, for the first start to gather into the family's — only
    // where the home is the gate's own (product.ownsHome), and as if this product had never gathered here — where
    // it has a first start that gathers them (product.gathers).
    const bool ownsHome = juce::SystemStats::getEnvironmentVariable (product.ownsHome, {}).isNotEmpty();
    const auto seedRun  = juce::String::toHexString (juce::Random::getSystemRandom().nextInt());
    const auto formerIr = device::IrLibrary::formerDirectory (ORBITAMP_PRODUCT_NAME).getChildFile ("Old Pack").getChildFile ("gathered-" + seedRun + ".wav");
    if (ownsHome && product.gathers)
    {
        // An IR of its own: the shelf's default, RED GIANT, with its last sample nudged, like no other here.
        const auto& shelf = device::shelf::irs (ORBITAMP_PRODUCT_NAME)[(size_t) params::cabIrDefault];
        juce::MemoryBlock bytes (shelf.data, (size_t) shelf.size);
        bytes[bytes.getSize() - 1] = (char) (bytes[bytes.getSize() - 1] ^ 4);
        markOfRun (bytes, seedRun);
        formerIr.getParentDirectory().createDirectory();
        formerIr.replaceWithData (bytes.getData(), bytes.getSize());
        prefs::setBool (ORBITAMP_GATE_PRODUCT(), prefs::irsGathered, false);
    }

    auto amp = std::make_unique<AmpProcessor> (ORBITAMP_GATE_PRODUCT());
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

    // THE TONE KNOBS ARE THE PACK'S TONES, as many as it lists and in its order, knob i on place i
    // (Channels::toneOf) — said here pack by pack, not read back off the packs: the X88-IR's and the
    // IR-X's Bass, Middle, Treble; the Sheriff's and the Kraken's those and Bright; the Copper's two Tone,
    // Treble, Middle, Bass, Bass cut; the ReVolt's Clean Bass and Treble, its Crunch and Lead Bass, Mid,
    // Treble. A knob past the list names nothing.
    // ...BUT ON THE BASS'S STANDARD (product.bass) the knobs are the pack's DIALS where it states a tone (the owner,
    // 03.11) — the SGT-DI's Bass, Mid and Treble, its places 0, 1 and 3 — with its frequency knob (Freq, place 2) and
    // its switches (Ultra Lo and Ultra Hi, places 4 and 5) asked for apart; on a pack with no tone of its own they are
    // the standard's three, the AMP's console's low shelf, first bell and high shelf, and none a tone of the pack's.
    {
        const auto listOf = [] (const juce::String& capture) -> juce::StringArray
        {
            if (capture.startsWith ("v4-copper"))                                    return { "Tone", "Treble", "Middle", "Bass", "Bass cut" };
            if (capture.startsWith ("v4-sheriff") || capture.startsWith ("v4-kraken")) return { "Bass", "Middle", "Treble", "Bright" };
            if (capture.startsWith ("revolt-clean"))                                 return { "Bass", "Treble" };
            if (capture.startsWith ("revolt-") || capture.startsWith ("sgt-di"))     return { "Bass", "Mid", "Treble" };
            return { "Bass", "Middle", "Treble" };
        };
        // On the bass: the places a pack's dials stand at, its frequency knob's and its switches' — said pack by pack.
        struct BassTone { std::vector<int> dials; int frequency = -1; std::vector<int> switches; };
        const auto bassToneOf = [] (const juce::String& capture) -> std::optional<BassTone>
        {
            if (capture.startsWith ("sgt-di")) return BassTone { { 0, 1, 3 }, 2, { 4, 5 } };
            return std::nullopt;     // no tone of its own: the bass's standard's three
        };
        const juce::StringArray standard { params::eqLoDb (0), params::eqBellDb (0, 0), params::eqHiDb (0) };
        bool found = true;
        for (int c = 0; c < channels.count(); ++c)
        {
            const juce::String capture (channels.lineup().channels[(size_t) c].capture);
            const auto own = product.bass ? bassToneOf (capture) : std::nullopt;
            const auto want = product.bass && ! own.has_value() ? juce::StringArray { "Bass", "Middle", "Treble" } : listOf (capture);
            found = found && channels.toneNamesOf (c) == want;
            for (int t = 0; t < channels::Channels::maxTones; ++t)
            {
                const auto id = t >= want.size() ? juce::String()
                              : own.has_value()  ? params::preampMeasured (own->dials[(size_t) t])
                              : product.bass     ? standard[t] : params::preampMeasured (t);
                found = found && channels.toneOf (c, t) == id;
            }
            if (product.bass)
            {
                found = found && channels.tonesOf (c).size() == (own.has_value() ? own->dials.size() : 0)
                              && channels.frequencyOf (c) == (own.has_value() ? params::preampMeasured (own->frequency) : juce::String());
                for (int w = 0; w < 3; ++w)
                    found = found && channels.toneSwitchOf (c, w) == (own.has_value() && w < (int) own->switches.size()
                                                                          ? params::preampMeasured (own->switches[(size_t) w]) : juce::String());
            }
        }
        check (found, product.tones);
    }

    // THE MIDI TONE CONTROLLERS FIND THEIR KNOBS BY NAME (Channels::toneNamed), as midi.txt lists them —
    // 24 the Bass, 25 the Middle or Mid, 26 the Treble — wherever the pack puts them: on the Copper its
    // fourth, third and second knobs; on the ReVolt's Clean the Middle is no knob at all.
    {
        using Band = channels::Channels::Band;
        const auto placesOf = [] (const juce::String& capture) -> std::array<int, 3>
        {
            if (capture.startsWith ("v4-copper"))    return { 3, 2, 1 };
            if (capture.startsWith ("revolt-clean")) return { 0, -1, 1 };
            return { 0, 1, 2 };
        };
        bool named = true;
        for (int c = 0; c < channels.count(); ++c)
        {
            const auto want = placesOf (channels.lineup().channels[(size_t) c].capture);
            named = named && channels.toneNamed (c, Band::bass)   == want[0]
                          && channels.toneNamed (c, Band::middle) == want[1]
                          && channels.toneNamed (c, Band::treble) == want[2];
        }
        check (named, "CC 24, 25, 26 turn the Bass, Middle and Treble knobs by name, wherever the pack lists them");
    }

    // GEARS OF TWO, where the product's channels go in pairs, as far as it goes, TURBO the second of
    // each, and the pair one memory (Lineup::pairs): one channel of a pack with the Boost held off, then
    // on — on Mars the IR-X's CRUNCH Ch1 and HI-GAIN Ch2; on the ice moon the ReVolt's CLEAN, CRUNCH and
    // HI-GAIN its Clean, Crunch and Lead; over Titan the V4's, its Copper, Sheriff and Kraken.
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

    // A processor just built: on the channel its lineup opens on (Lineup::opensAt) — the first, but the walker's SVT.
    const int opener = channels.lineup().opensAt;
    check (channels.adopted(), "a processor just built is the " + ours + "'s already");
    check (channels.active() == opener && asked() == opener,
           juce::String ("...on the channel its lineup opens on, ") + channels.lineup().channels[(size_t) opener].name + ", and CHANNEL says so");
    check (playing() == channels.packFor (opener), "...playing that channel's pack");
    ticks();
    check (! history.canUndo(), "...and none of that is an edit");

    // THE SHELF'S `Other` IS ORBITAMP'S (device/FactoryShelf.h): the amp plays Darwin's Cat's own cabinets
    // and not a byte of the third-party ones, whatever the factory folder holds — their places in
    // `cab_ir`'s order held empty — and a host that moves `cab_ir` to one of those places hears RED
    // GIANT, the default, not a cabinet that is not there. A BASS AMP plays none of the shelf at all — a guitar
    // amp's cabinets and a bass amp's never cross (Product::playsCabIr) — and the walker's are drawn
    // (Lineup::drawnCabinets): every place of the shelf held empty for it, no number of `cab_ir` a cabinet.
    if (product.bass)
    {
        bool none = ! device::shelf::irs (ORBITAMP_PRODUCT_NAME).empty();
        for (const auto& ir : device::shelf::irs (ORBITAMP_PRODUCT_NAME))
            none = none && ir.data == nullptr;
        for (int i = 0; i < params::cabIrNames.size(); ++i)
            none = none && AmpProcessor::cabIrBytes (ORBITAMP_GATE_PRODUCT(), i).data == nullptr;
        check (none, "the " + ours + " plays no cabinet of the shelf: every place of it empty, no number of `cab_ir` a cabinet, RED GIANT none");
        check.skip ("...and a number of Other's plays RED GIANT, the default", "a bass amp has no RED GIANT to fall to");
    }
    else
    {
        bool oursAlone = device::shelf::irs (ORBITAMP_PRODUCT_NAME).size() == (size_t) params::cabIrNames.size();
        for (const auto& ir : device::shelf::irs (ORBITAMP_PRODUCT_NAME))
            oursAlone = oursAlone && (ir.data != nullptr) == (juce::String (ir.folder) == device::shelf::ours);
        check (oursAlone, "the " + ours + " plays the shelf's Orange PPC212V and nothing of Other, every number in its place");
        check (AmpProcessor::cabIrBytes (ORBITAMP_GATE_PRODUCT(), 0).data == AmpProcessor::cabIrBytes (ORBITAMP_GATE_PRODUCT(), params::cabIrDefault).data,
               "...and a number of Other's plays RED GIANT, the default");
    }

    // NO PARK, on any product: the rover's lever is its gears and nothing more, and no switch of the
    // product's own takes the whole amp out of the sound.
    {
        bool none = true;
        for (auto* p : amp->getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p); ranged != nullptr && ranged->getParameterID().containsIgnoreCase ("park"))
                none = false;
        check (none, "the " + ours + " has no PARK: no parameter of its own takes the amp out of the sound");
    }
    ticks();

    // A host moves the parameter — to a channel the amp is not on: the last, or where the amp opens on the last (the
    // walker's SVT), the first.
    {
        const int from = channels.active(), target = from == last ? 0 : last;
        asHost (target);
        check (channels.active() == from, "CHANNEL moved by a host changes nothing until the engine's own tick");
        engine.pump();
        check (channels.active() == target && asked() == target, "...and then the channel is the one asked for");
        check (playing() == channels.packFor (target), "...with its pack");
    }
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

    // ON THE REVOLT, where the build plays it: the Clean has two tone knobs, its Bass and its Treble, the
    // Crunch three, Bass, Mid and Treble — and each knob turns its own pack's tone and nothing else.
    {
        int clean = -1, crunch = -1;
        for (int c = 0; c < (int) played.size(); ++c)
        {
            const juce::String capture (played[(size_t) c].capture);
            if (clean < 0 && capture.startsWith ("revolt-clean"))   clean = c;
            if (crunch < 0 && capture.startsWith ("revolt-crunch")) crunch = c;
        }

        if (clean >= 0 && crunch >= 0 && ! product.bass)     // on the bass's standard the knobs are no pack's tones
        {
            const auto everything = [&amp]
            {
                std::vector<float> v;
                for (auto* p : amp->getParameters())
                    v.push_back (p->getValue());
                return v;
            };
            // Each knob turned in turn: its pack's place moves to where it was turned, and nothing else does.
            const auto eachTurnsItsOwn = [&] (int c)
            {
                engine.go (c);
                ticks();
                bool own = true;
                const auto names = channels.toneNamesOf (c);
                for (int t = 0; t < names.size(); ++t)
                {
                    auto* p = amp->apvts.getParameter (channels.toneId (t));
                    if (p == nullptr || channels.toneId (t) != params::preampMeasured (t))
                        return false;
                    auto before = everything();
                    const float to = p->getValue() < 0.5f ? 0.8f : 0.2f;
                    p->setValueNotifyingHost (to);
                    auto after = everything();
                    const auto index = (size_t) amp->getParameters().indexOf (p);
                    own = own && std::abs (after[index] - to) < 1.0e-3f;
                    after[index] = before[index];
                    own = own && after == before;
                }
                return own && channels.toneId (names.size()).isEmpty();
            };

            check (channels.toneNamesOf (clean) == juce::StringArray { "Bass", "Treble" } && eachTurnsItsOwn (clean),
                   "the ReVolt's Clean has two tone knobs, BASS and TREBLE, each turning its own and nothing else");
            check (channels.toneNamesOf (crunch) == juce::StringArray { "Bass", "Mid", "Treble" } && eachTurnsItsOwn (crunch),
                   "...its Crunch three, BASS, MID and TREBLE, likewise");
            ticks();
        }
    }

    // A hand's switch: one step, and undone whole — from the third channel to the second, or on an amp of two
    // from the second to the first.
    {
    const int from = std::min (2, last), to = from == 1 ? 0 : 1;
    asHost (from);
    ticks();
    const int depth = history.undoDepth();
    check (engine.go (to), "a hand takes the lever to another channel");
    check (channels.active() == to && asked() == to, "...and CHANNEL goes with it at once");
    ticks();
    check (history.undoDepth() == depth + 1, "...as one step of the history");
    history.undo();
    ticks();
    check (channels.active() == from && asked() == from, "undo brings the channel back, parameter and sound together");
    check (playing() == channels.packFor (from), "...and its pack");
    check (history.undoDepth() == depth, "...leaving no step of its own behind");
    }

    // A state from before there was a parameter: it says its channel on the node, and nowhere else — a channel neither
    // the amp is on nor CHANNEL opens on, where the parameter, missing from the state, falls back to its default: the
    // last, or on the walker, which opens on its last, the first.
    {
        int said = last;
        while (said > 0 && (said == channels.active() || said == opener))
            --said;
        auto old = amp->apvts.copyState();
        if (auto word = old.getChildWithProperty ("id", juce::String (engine.channelId())); word.isValid())
            old.removeChild (word, nullptr);
        if (auto node = old.getChildWithName (channels.nodeType()); node.isValid())
            node.setProperty ("active", said, nullptr);

        const int before = history.undoDepth();
        history.applyEdit (history.active(), old, "an older state");
        check (said != opener && channels.active() == said && asked() != said, "an older state lands on a channel the parameter does not say");
        ticks();
        check (channels.active() == said && asked() == said, "...the parameter follows the state, not the other way round");
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
    // say nothing, in a folder of the gate's own laid out as a factory — the product's list there naming
    // them — are the channels' packs still.
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

        juce::StringArray listed;
        for (const auto& n : names)
            listed.addIfNotAlreadyThere (n);
        const auto list = renamed.getChildFile (device::FactorySet::listsName).getChildFile (juce::String (ORBITAMP_PRODUCT_NAME) + ".txt");
        copied = copied && list.getParentDirectory().createDirectory().wasOk() && list.replaceWithText (listed.joinIntoString ("\n") + "\n");

        amp->preamp.packs = device::DeviceLibrary::scan (ORBITAMP_PRODUCT_NAME, device::DeviceLibrary::Slot::preamp, true,
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
    // that folder is the player's. Not where the product's cabinets are DRAWN (Lineup::drawnCabinets, the walker's):
    // there they are the lineup's slots, the bass's dozen, set and fixed — no file of the build's, none of the player's.
    const bool drawn = channels.lineup().drawnCabinets;
    if (drawn)
    {
        bool named = channels.cabinetSlots() == (int) channels.lineup().cabinets.size() && channels.cabinetSlots() > 0;
        for (int i = 0; named && i < channels.cabinetSlots(); ++i)
            named = channels.slotName (i).isNotEmpty() && channels.slotFrom (i).isNotEmpty();
        check (named && channels.factoryCabinets() == 0 && channels.cabinets().isEmpty(),
               "the " + ours + "'s cabinets are drawn: " + juce::String (channels.cabinetSlots()) + " slots, each a preset by its name, "
                   "and no file of the build's or the player's");
        check.skip ("the player's own cabinets: two IRs taken in, renamed, kept by what they are",
                    "the " + ours + "'s cabinets are drawn, no file of the player's among them");
    }
    else if (ownsHome)
    {
        const int factory = channels.factoryCabinets();
        const int before  = channels.cabinets().size();
        check (factory > 0, "the build's own cabinets are there");

        // Another product's cabinet in the family's folder, beside the build's own, is not one of the
        // amp's: its set gate holds that (AmpSetTests), on a folder of its own — this one reads the
        // build's, and writes nothing there.

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
        check (channels.cabinets()[added.first].getParentDirectory() == device::DeviceLibrary::familyRoot().getChildFile (ORBITAMP_GATE_PRODUCT().irsFolder()),
               "...and what is taken in lands in the family's own IRs folder, beside Factory");

        // THE IR OF BEFORE, gathered at the first start: copied into the family's folder, in its own
        // folder, and still where it was — by a product that has IRs of before to gather (product.gathers).
        if (product.gathers)
        {
            const auto gathered = device::IrLibrary::directory (ORBITAMP_GATE_PRODUCT().irsFolder()).getChildFile ("Old Pack").getChildFile (formerIr.getFileName());
            check (gathered.existsAsFile() && formerIr.existsAsFile() && gathered.hasIdenticalContentTo (formerIr),
                   "the player's IRs of before are copied into the family's folder, folders and all, and left where they were");
            check (channels.cabinets().indexOf (gathered) >= factory, "...and are among the player's cabinets");
            check (prefs::getBool (ORBITAMP_GATE_PRODUCT(), prefs::irsGathered, false), "...once: the product remembers it has gathered them");
        }

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

        // ...and the channel mode where INIT has always put it, whatever a fresh instance stands on: the
        // engine's MONO, not the environment's (params::engineDefault).
        const auto* mode = amp->apvts.getParameter (params::stereoMode);
        check (mode != nullptr && juce::roundToInt (mode->convertFrom0to1 (mode->getValue())) == (int) params::StereoMode::mono,
               "INIT puts the channel mode on MONO, as it always has");

        // INIT opens on the channel the lineup opens on (Lineup::opensAt), the walker's SVT.
        const int opening = channels.defaultCabinet();
        bool each = channels.active() == opener && channels.count() <= (int) played.size(), onIt = true;
        juce::StringArray slots;
        for (int c = 0; each && c < channels.count(); ++c)
        {
            engine.go (c);
            const auto& o = played[(size_t) c];
            const bool opens = opensOn (o.reverb, o.wet, o.echo);
            if (o.opens != nullptr)
                check (opens, o.opens);
            each = each && opens;
            if (drawn)      // ...each on its own slot of the drawn ones, the preset the product's table says
            {
                const int slot = channels.cabinetSlot();
                const auto* preset = slot >= 0 && slot < channels.cabinetSlots() ? channels.lineup().cabinets[(size_t) slot].preset : nullptr;
                slots.add (juce::String (channels.lineup().channels[(size_t) c].name) + " " + juce::String (preset != nullptr ? preset : "none"));
                onIt = onIt && o.cabinet != nullptr && preset != nullptr && juce::String (preset) == o.cabinet;
            }
            else
                onIt = onIt && channels.cabinet() == opening;
        }
        check (each, "after INIT each channel opens on its own room, its own share of it and of delay");
        if (drawn)
            check (each && onIt, product.opening + juce::String (" (") + slots.joinIntoString (", ") + ")");
        else
            check (opening >= 0 && opening < channels.factoryCabinets()
                       && channels.cabinets()[opening].getFileName() == juce::String (catalog::irs[params::cabIrDefault].file) && onIt,
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

    // A SESSION OF BEFORE, when every channel opened on DARK MATTER (AmpProcessor::takeFormerCabinet): the
    // session as a build of then saved it — no note of its opening cabinet on any state — playing DARK
    // MATTER, with another memory's channel left on it too, opens on RED GIANT, and that channel comes back
    // on RED GIANT; one of before on another of the build's cabinets keeps it; and one saved now on DARK
    // MATTER, the player's pick by its note, keeps that. Where the cabinets are DRAWN there was never a DARK MATTER to
    // open on — what stands for it there: a session keeps each gear's slot, and brings every gear back on its own.
    if (drawn)
    {
        check.skip ("the build carries DARK MATTER, RED GIANT and PULSAR BEAM, and a session of before on DARK MATTER opens on RED GIANT",
                    "the " + ours + "'s cabinets are drawn and never opened on a file");
        int far = 1;
        while (far < last && channels.memoryOf (far) == channels.memoryOf (0))
            ++far;
        const int mine = 5, theirs = 9;     // two slots of the dozen neither gear opens on
        engine.go (far);
        channels.chooseSlot (theirs);
        ticks();
        engine.go (0);
        channels.chooseSlot (mine);
        ticks();
        juce::MemoryBlock session;
        amp->getStateInformation (session);
        channels.chooseSlot (0);
        engine.go (far);
        channels.chooseSlot (0);
        engine.go (0);
        ticks();
        amp->setStateInformation (session.getData(), (int) session.getSize());
        ticks();
        const int now = channels.cabinetSlot();
        engine.go (far);
        const int there = channels.cabinetSlot();
        engine.go (0);
        check (far != 0 && now == mine && there == theirs,
               "a session keeps each gear on the slot it was left on: " + juce::String (now) + " and " + juce::String (there)
                   + " (left on " + juce::String (mine) + " and " + juce::String (theirs) + ")");
        ticks();
    }
    else
    {
        const auto fileOf = [&channels] (const char* name)
        {
            const juce::String file (catalog::irs[params::cabIrNames.indexOf (name)].file);
            for (int i = 0; i < channels.factoryCabinets(); ++i)
                if (channels.cabinets()[i].getFileName() == file)
                    return i;
            return -1;
        };
        const int dark = fileOf ("DARK MATTER"), giant = fileOf ("RED GIANT"), pulsar = fileOf ("PULSAR BEAM");
        int far = 1;
        while (far < last && channels.memoryOf (far) == channels.memoryOf (0))
            ++far;

        const auto saved = [&] (int cabinet, bool before)
        {
            engine.go (far);
            channels.chooseCabinet (cabinet);
            ticks();
            engine.go (0);
            channels.chooseCabinet (cabinet);
            ticks();
            juce::MemoryBlock session;
            amp->getStateInformation (session);
            if (before)
            {
                auto tree = juce::ValueTree::fromXml (*juce::AudioProcessor::getXmlFromBinary (session.getData(), (int) session.getSize()));
                std::function<void (juce::ValueTree)> unnote = [&unnote] (juce::ValueTree t)
                {
                    t.removeProperty (AmpProcessor::cabOpeningId, nullptr);
                    for (auto child : t)
                        unnote (child);
                };
                unnote (tree);
                session.reset();
                juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), session);
            }
            channels.chooseCabinet (cabinet == pulsar ? dark : pulsar);    // elsewhere, for the session to take it back from
            ticks();
            amp->setStateInformation (session.getData(), (int) session.getSize());
            ticks();
            const int now = channels.cabinet();
            engine.go (far);
            const int there = channels.cabinet();
            engine.go (0);
            return std::pair { now, there };
        };

        check (dark >= 0 && giant >= 0 && pulsar >= 0 && far != 0, "the build carries DARK MATTER, RED GIANT and PULSAR BEAM");
        const auto oldDark = saved (dark, true), oldPulsar = saved (pulsar, true), newDark = saved (dark, false);
        check (oldDark.first == giant, "a session of before on DARK MATTER opens on RED GIANT");
        check (oldDark.second == giant, "...and so does the channel it left on DARK MATTER");
        check (oldPulsar.first == pulsar && oldPulsar.second == pulsar, "...one of before on another cabinet keeps it, every channel");
        check (newDark.first == dark && newDark.second == dark, "...and one saved now on DARK MATTER keeps that, every channel");
        ticks();
    }

    // EACH TONE WHERE ITS PACK STARTS IT (Channels::defaultOf, core::CapturedBlock::defaultNorm): after INIT
    // every channel opens a sweep at its reference and a tone of positions on its reference position, as the
    // block plays the knob's place (the position nearest it) — not on the parameter's middle, which on the
    // V4 Sheriff's and Kraken's three-way Bright is the second position, "-1", a treble cut. The Copper's
    // Bass cut, of two, opens Off.
    {
        orbitamp::channels::Presets presets (*amp, channels);
        presets.init();
        ticks();
        int positional = 0;
        juce::StringArray wrong;
        for (int c = 0; c < channels.count(); ++c)
        {
            engine.go (c);
            const auto tones = channels.tonesOf (c);
            for (size_t t = 0; t < tones.size(); ++t)
            {
                auto* knob = amp->apvts.getParameter (params::preampMeasured ((int) t));
                const auto& tone = tones[t];
                const float v = knob != nullptr ? knob->convertFrom0to1 (knob->getValue()) : -1.0f;
                juce::String at;
                bool right = false;
                if (! tone.positions.empty())
                {
                    ++positional;
                    const int n = (int) tone.positions.size();
                    const auto& heard = tone.positions[(size_t) juce::jlimit (0, n - 1, juce::roundToInt (v * (float) (n - 1)))].value;
                    right = heard == felitronics::rigplayer::toneStart (tone);
                    at = juce::String (heard);
                }
                else
                {
                    right = std::abs (v - (float) orbitamp::core::CapturedBlock::defaultNorm (tone)) < 1.0e-4f;
                    at = juce::String (v, 3);
                }
                if (! right)
                    wrong.add (juce::String (channels.lineup().channels[(size_t) c].name) + "'s " + juce::String (tone.name) + " on " + at);
            }
        }
        check (wrong.isEmpty(), "after INIT every channel opens each of its pack's tones where the pack starts it, a tone of positions on its reference position"
                                    + (positional > 0 ? " (" + juce::String (positional) + " such here)" : juce::String())
                                    + (wrong.isEmpty() ? juce::String() : ": " + wrong.joinIntoString (", ")));
        ticks();
    }

    // THE PACK'S SWITCHES (Channels::switchesOf), where a channel holds one — the IR-X's rover and the
    // ReVolt's. Every channel's Boost stands where the lineup puts it, and a host that throws it is
    // overruled the next time the channel is pulled; the pack's other switch, where it has one — the
    // IR-X's; the ReVolt's packs have the Boost and the gain dial, DRIVE's, and nothing else — is the
    // gear's, its TURBO's too — one memory — and a gear never pulled since INIT opens it where the pack
    // does.
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

        // Where the product's table says each channel holds it (AmpGate.h): the Boost off on a gear, on on its TURBO; the
        // walker's Voice, B15 on B15 and SVT on SVT.
        const auto heldAs = [&played] (int c) { return juce::String (played[(size_t) c].held).fromFirstOccurrenceOf ("=", false, false); };
        const auto heldName = juce::String (played[0].held).upToFirstOccurrenceOf ("=", false, false);
        bool held = true, opens = true, free = false;
        for (int c = 0; c < channels.count(); ++c)
        {
            engine.go (c);
            for (const auto& s : channels.switchesOf (c))
                if (s.held)
                    held = held && at (s.slot) == s.opening && s.values[s.opening] == heldAs (c);
                else
                {
                    free = true;
                    opens = opens && at (s.slot) == s.opening;
                }
        }
        check (held, product.gears != nullptr ? "every channel's Boost stands where the lineup puts it: off on a gear, on on its TURBO"
                                              : "every channel's " + heldName + " stands where the lineup puts it");
        if (! free)
        {
            // The Boost the only switch: none of them the hand's, and a Boost a host throws under TURBO is
            // the lineup's again the next time the gear and its TURBO are pulled.
            int boost = -1, switches = 0;
            for (int c = 0; c < channels.count(); ++c)
                for (const auto& s : channels.switchesOf (c))
                {
                    ++switches;
                    if (s.held && c == 0)
                        boost = s.slot;
                }
            check (boost >= 0 && switches == channels.count(), "...and its packs have no other switch: the " + heldName + " is each channel's one, and held");

            engine.go (0);
            const int onFirst = at (boost);
            engine.go (1);
            const int onSecond = at (boost);
            put (boost, 1 - onSecond);      // a host throws the second channel's Boost the other way — the TURBO's off
            engine.go (0);
            const bool firstsOwn = at (boost) == onFirst;
            engine.go (1);
            check (firstsOwn && at (boost) == onSecond,
                   product.gears != nullptr ? "...a Boost a host threw off under TURBO is off on the gear, and on again under TURBO pulled again"
                                            : "...a " + heldName + " a host threw on the second channel is where the lineup holds it on the first, and on the second pulled again");
        }
        else
        {
            check (opens, "...and its pack's other switch opens where the pack does");

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
        }
        ticks();
    }

    // THE BASS'S GEARS (product.bass), each an amp of its own (Lineup::kept): DRIVE, BLEND, BASS, MIDDLE, TREBLE and
    // MIDDLE's frequency, COMP's threshold, OCTAVE, ENVELOPE, CHORUS and DI/CAB come back on the gear they were left
    // on, and a gear never pulled since INIT opens on its own; the instrument and the hand — 5 STR / 4 STR, FINGERS /
    // SLAP — are the player's on every gear. The face coming up again and again (Engine::setUpChain, as the console
    // calls it each time) leaves the tone where the player put it. And INIT gives what the product holds
    // (Lineup::pinned) — COMP on, all of the signal through it at 4:1 on OPTO, OCTAVE, ENVELOPE and CHORUS in the
    // strip and on, both cuts on, no delay and no room — as the face coming up does after a host moved any of it.
    if (product.bass)
    {
        auto* hpf = amp->apvts.getParameter (params::bassHpf);
        auto* lpf = amp->apvts.getParameter (params::bassLpf);
        // ...its tones as its knobs turn them (Channels::toneOf): the pack's dials, its frequency knob and its switches where
        // it states a tone — the SGT-DI's BASS, MID, TREBLE, FREQ, ULTRA LO and ULTRA HI — or the standard's three and
        // MIDDLE's frequency on a pack with none.
        juce::StringArray kept { params::preampGain, params::ampBlend };
        for (int t = 0; t < 3; ++t)
            kept.add (channels.toneOf (0, t));
        kept.add (channels.frequencyOf (0).isNotEmpty() ? channels.frequencyOf (0) : juce::String (params::eqBellHz (0, 0)));
        for (int w = 0; w < 3; ++w)
            if (const auto id = channels.toneSwitchOf (0, w); id.isNotEmpty())
                kept.add (id);
        kept.addArray (juce::StringArray { params::compThreshold, params::octaveAmount, params::filterAmount, params::chorusAmount, params::cabMix });
        kept.removeEmptyStrings();
        jassert (kept.size() <= 13);    // left 0.07 apart from 0.12 and from 0.85, every one inside 0..1
        const auto at  = [&amp] (const juce::String& id) { auto* p = amp->apvts.getParameter (id); return p != nullptr ? p->getValue() : -1.0f; };
        const auto put = [&amp] (const juce::String& id, float to) { if (auto* p = amp->apvts.getParameter (id)) p->setValueNotifyingHost (to); };
        const auto plain = [&amp] (const juce::String& id) { auto* p = amp->apvts.getParameter (id); return p != nullptr ? p->convertFrom0to1 (p->getValue()) : -1000.0f; };
        const auto on    = [&plain] (const juce::String& id) { return plain (id) > 0.5f; };
        const auto leave = [&] (float from, float step)     // each kept control somewhere of its own, as it stands then
        {
            std::vector<float> left;
            for (int i = 0; i < kept.size(); ++i)
            {
                put (kept[i], from + step * (float) i);
                left.push_back (at (kept[i]));
            }
            return left;
        };
        const auto standsAs = [&] (const std::vector<float>& left)
        {
            bool same = hpf != nullptr && lpf != nullptr;
            for (int i = 0; i < kept.size(); ++i)
                same = same && std::abs (at (kept[i]) - left[(size_t) i]) < 1.0e-4f;
            return same;
        };
        const auto differsFrom = [&] (const std::vector<float>& left)
        {
            bool all = true;
            for (int i = 0; i < kept.size(); ++i)
                all = all && std::abs (at (kept[i]) - left[(size_t) i]) > 1.0e-3f;
            return all;
        };

        orbitamp::channels::Presets presets (*amp, channels);
        presets.init();
        ticks();

        engine.go (0);
        const auto first = leave (0.12f, 0.07f);
        put (params::bassHpf, 1.0f);        // 4 STR
        put (params::bassLpf, 1.0f);        // SLAP
        engine.go (1);
        check (differsFrom (first), "a bass's gear never pulled since INIT opens on its own DRIVE, BLEND, tones, MIDDLE's frequency, its pack's switches, COMP, effects and DI/CAB, not the other gear's");
        const auto second = leave (0.85f, -0.07f);
        engine.go (0);
        check (standsAs (first), "each gear comes back with the DRIVE, BLEND, BASS, MIDDLE, TREBLE, MIDDLE's frequency, ULTRA LO and HI, COMP threshold, OCTAVE, ENVELOPE, CHORUS and DI/CAB it was left with ("
                                     + kept.joinIntoString (" ") + ")");
        engine.go (1);
        check (standsAs (second), "...and so does the other");
        check (juce::roundToInt (plain (params::bassHpf)) == (int) params::BassHpf::four && juce::roundToInt (plain (params::bassLpf)) == (int) params::BassLpf::slap,
               "...while 5 STR / 4 STR and FINGERS / SLAP stand as the player threw them, on every gear");

        engine.setUpChain (channels.adopt());     // the face comes up — as the console does it — and again
        engine.setUpChain (channels.adopt());
        check (standsAs (second), "the face coming up, twice, leaves BASS, MIDDLE, TREBLE and every other control of the gear where it stood");

        presets.init();
        ticks();
        const auto holds = [&]
        {
            const auto* character = amp->apvts.getParameter (params::compCharacter);
            return on (params::compOn) && std::abs (plain (params::compAmount) - 0.75f) < 1.0e-4f && std::abs (plain (params::compRatio) - 4.0f) < 1.0e-3f
                && character != nullptr && character->getCurrentValueAsText() == "Opto"
                && on (params::octavePresent) && on (params::octaveOn)
                && on (params::filterPresent) && on (params::filterOn)
                && on (params::chorusPresent) && on (params::chorusOn)
                && on (params::bassHpfOn) && on (params::bassLpfOn)
                && ! on (params::delayPresent) && ! on (params::reverbPresent);
        };
        check (holds(), "INIT gives COMP on, three quarters of the signal through it at 4:1 on OPTO, OCTAVE, ENVELOPE and CHORUS in the strip and on, both cuts on, no delay and no room");
        // ...and every gear's tone flat: the standard's three at 0 dB, or the pack's dials where their travel is none — the
        // reference each band is flat at (felitronics::rigplayer::travelOf).
        bool flat = true;
        juce::StringArray tilted;
        for (int c = 0; c < channels.count(); ++c)
        {
            engine.go (c);
            const auto dials = channels.tonesOf (c);
            for (int t = 0; t < 3; ++t)
            {
                const float v = plain (channels.toneOf (c, t));
                const double off = t < (int) dials.size() ? felitronics::rigplayer::travelOf (dials[(size_t) t], (double) v) : (double) v;
                if (std::abs (off) >= 1.0e-4)
                    tilted.add (juce::String (channels.lineup().channels[(size_t) c].name) + " " + channels.toneNamesOf (c)[t] + " " + juce::String (off, 4));
                flat = flat && std::abs (off) < 1.0e-4;
            }
        }
        check (flat, "...and every gear's BASS, MIDDLE and TREBLE flat: 0 dB, at the reference of the pack's dials"
                         + (tilted.isEmpty() ? juce::String() : ": " + tilted.joinIntoString (", ")));

        put (params::compRatio, 0.9f);
        put (params::compAmount, 0.3f);
        put (params::chorusPresent, 0.0f);
        put (params::reverbPresent, 1.0f);
        engine.setUpChain (channels.adopt());
        check (holds(), "...and the face coming up gives them again, where a host left them anywhere else");
        ticks();
    }

    return check.finish();
}
