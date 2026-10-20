// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Gate for EVERY AMP IN ONE BINARY (src/Product.h): the sources of all five amps linked into this one
// executable — which they could not be while each amp's file defined its face, its first start, its
// parameters, its engine and its skin under the same names as the others' — and played side by side, in
// one process, headless, out of the build's factory folder (CMakeLists.txt: gate_reads_factory). What a
// plug-in of several amps will be, and a host running two of them at once already is:
//
//   - a processor of each product says its own name, and plays the devices its own list in the factory
//     folder names, and no other;
//   - a product with channels opens on its own, each channel on the pack its lineup says, the first one
//     playing; OrbitAmp has none;
//   - a fresh instance of each holds every parameter at the default it declares, and being prepared and
//     played moves none — what a CLAP host holds a new instance to (clap-validator's param-default-values
//     and param-set-wrong-namespace) — the channel mode standing where the environment puts it;
//   - each product reads the cabinet shelf it plays — OrbitAmp the whole of it, a face Darwin's Cat's own;
//   - two products at once, and two processors of one: each keeps what it was made as;
//   - each product keeps its settings and its update badge in a store of its own, in a folder of its own,
//     shared by its processors and by nobody else's.
//
// It touches the machine's settings only in the home run-gates.sh made for it. main() returns 0 when
// every check held.

#include <juce_events/juce_events.h>

#include "PluginProcessor.h"
#include "channels/Engine.h"
#include "catpit/CatpitLineup.h"
#include "device/DeviceLibrary.h"
#include "device/FactorySet.h"
#include "device/FactoryShelf.h"
#include "redpaw/RedPawSkin.h"

#include <cstdio>
#include <memory>
#include <set>
#include <vector>

using namespace orbitamp;

namespace
{
    int failed = 0;

    void check (bool ok, const juce::String& what)
    {
        std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
        if (! ok)
            ++failed;
    }

    /** The family, each with the lineup it plays where it has channels. */
    struct Amp
    {
        const Product& product;
        const channels::Lineup* lineup;
    };

    std::vector<Amp> family()
    {
        return {
            { products::orbitAmp(),    nullptr },
            { products::catpitAmp(),   &catpit::lineup() },
            { products::redPawAmp(),   &redpaw::skinOf (products::redPawAmp()).lineup },
            { products::frostPawAmp(), &redpaw::skinOf (products::frostPawAmp()).lineup },
            { products::amberPawAmp(), &redpaw::skinOf (products::amberPawAmp()).lineup },
        };
    }

    /** Whether every device the processor offers out of the box is one its own list names. */
    bool playsItsOwnList (const AmpProcessor& amp, int& bundled)
    {
        const auto own = device::FactorySet::ownList (amp.product().name);
        bool ownOnly = ! own.isEmpty();
        bundled = 0;
        for (const auto& pack : amp.preamp.packs)
            if (pack.bundled)
            {
                ++bundled;
                ownOnly = ownOnly && own.contains (pack.location.getFileName());
            }
        return ownOnly && bundled > 0;
    }

    /** The parameters whose value is not `against`'s for them — each "name: value, not expected", by
        their normalised values, within the ten-thousandth of the range a CLAP host allows (clap-validator,
        param_compare_approx); a stepped one only when it rounds elsewhere. Empty where all agree. */
    juce::StringArray movedFrom (const AmpProcessor& amp, const std::vector<float>& against)
    {
        juce::StringArray moved;
        const auto& all = amp.getParameters();
        for (int i = 0; i < all.size(); ++i)
        {
            const float now = all[i]->getValue(), was = against[(size_t) i];
            const bool same = all[i]->isBoolean() ? juce::roundToInt (now) == juce::roundToInt (was)
                                                  : std::abs (now - was) <= 1.0e-4f;
            if (! same)
                moved.add (all[i]->getName (64) + ": " + juce::String (now, 4) + ", not " + juce::String (was, 4));
        }
        return moved;
    }

    std::vector<float> valuesOf (const AmpProcessor& amp)
    {
        std::vector<float> v;
        for (const auto* p : amp.getParameters())
            v.push_back (p->getValue());
        return v;
    }

    std::vector<float> defaultsOf (const AmpProcessor& amp)
    {
        std::vector<float> v;
        for (const auto* p : amp.getParameters())
            v.push_back (p->getDefaultValue());
        return v;
    }

    /** The shelf's places a product has a cabinet in. */
    int cabinetsOn (const Product& p)
    {
        int held = 0;
        for (const auto& ir : device::shelf::irs (p.name))
            held += ir.data != nullptr ? 1 : 0;
        return held;
    }
}

int main()
{
    const juce::ScopedJuceInitialiser_GUI juce;

    const auto factory = device::FactorySet::location();
    std::printf ("Every amp in one binary, out of the factory folder at %s\n", factory.getFullPathName().toRawUTF8());
    if (! factory.isDirectory())
    {
        std::printf ("FAIL  the build's factory folder is there\nFAILED\n");
        return 1;
    }

    // EACH ON ITS OWN, one after the other.
    for (const auto& [product, lineup] : family())
    {
        const juce::String name (product.name);
        auto amp = std::make_unique<AmpProcessor> (product);
        amp->inlineLoads = true;            // no message loop here: a model lands inside the pump

        check (amp->getName() == name && &amp->product() == &product, name + "'s processor says its own name");

        int bundled = 0;
        check (playsItsOwnList (*amp, bundled),
               name + " offers the devices its own list in the factory folder names, and no other (" + juce::String (bundled) + ")");

        if (lineup == nullptr)
        {
            check (amp->productEngine == nullptr && amp->apvts.getParameter ("catpit_channel") == nullptr
                       && amp->apvts.getParameter ("rover_channel") == nullptr,
                   name + " has no channels");
            continue;
        }

        auto& engine = channels::Engine::of (*amp);
        auto& channels = engine.channels;
        check (&channels.lineup() == lineup && channels.count() == (int) lineup->channels.size()
                   && amp->apvts.getParameter (lineup->parameterId) != nullptr,
               name + " plays its own channels: " + juce::String (channels.count()) + " of them, on " + lineup->parameterId);

        bool ownPacks = true;
        for (int c = 0; c < channels.count(); ++c)
        {
            const int pack = channels.packFor (c);
            ownPacks = ownPacks && pack >= 0 && pack < amp->preamp.packs.size()
                    && device::DeviceLibrary::rigIdOf (amp->preamp.packs.getReference (pack).location)
                           == lineup->channels[(size_t) c].capture;
        }
        check (ownPacks, "...each on the pack its lineup says");

        const auto* device = amp->apvts.getParameter (params::preampDevice);
        check (channels.active() == 0 && device != nullptr
                   && juce::roundToInt (device->convertFrom0to1 (device->getValue())) == channels.packFor (0),
               "...and it opens on the first, playing");
    }

    // WHAT A HOST IS TOLD OF A FRESH AMP. The default each parameter declares is what the host shows,
    // resets to, and holds a new instance to (clap-validator's param-default-values): a fresh instance
    // holds every one of them. And it holds them on: being prepared and playing its first blocks, with
    // no host's hand on anything, moves none (param-set-wrong-namespace reads the values around one
    // block of events the amp must not take).
    for (const auto& [product, lineup] : family())
    {
        const juce::String name (product.name);
        auto amp = std::make_unique<AmpProcessor> (product);
        amp->inlineLoads = true;

        const auto atDefaults = movedFrom (*amp, defaultsOf (*amp));
        check (atDefaults.isEmpty(), name + ", made, holds every parameter at its declared default"
                                         + (atDefaults.isEmpty() ? juce::String() : ": " + atDefaults.joinIntoString ("; ")));

        const auto made = valuesOf (*amp);
        constexpr double rate = 48000.0;
        constexpr int block = 256;
        amp->prepareToPlay (rate, block);
        juce::MidiBuffer none;
        juce::Random noise (7);
        for (int b = 0; b < 8; ++b)
        {
            juce::AudioBuffer<float> buf (2, block);
            for (int ch = 0; ch < 2; ++ch)
                for (int n = 0; n < block; ++n)
                    buf.setSample (ch, n, 0.25f * (noise.nextFloat() * 2.0f - 1.0f));
            amp->processBlock (buf, none);
            amp->pumpDeviceWork();
        }
        amp->releaseResources();

        const auto playing = movedFrom (*amp, made);
        check (playing.isEmpty(), "...and prepared and playing, with no hand on it, moves none"
                                      + (playing.isEmpty() ? juce::String() : ": " + playing.joinIntoString ("; ")));
    }

    // ...and the channel mode where the environment puts it: STEREO in a plug-in on its stereo bus (above),
    // MONO once a host settles a mono one — before it prepares, so that preparing still moves nothing —
    // and STEREO SPACE in the standalone, declared so.
    {
        const auto modeOf = [] (const AmpProcessor& amp, bool declared)
        {
            const auto* p = amp.apvts.getParameter (params::stereoMode);
            return juce::roundToInt (p->convertFrom0to1 (declared ? p->getDefaultValue() : p->getValue()));
        };

        auto amp = std::make_unique<AmpProcessor> (products::orbitAmp());
        amp->inlineLoads = true;
        check (modeOf (*amp, false) == (int) params::StereoMode::stereo && modeOf (*amp, true) == (int) params::StereoMode::stereo,
               "a plug-in on a stereo bus opens on STEREO, and declares it");

        juce::AudioProcessor::BusesLayout mono;
        mono.inputBuses.add (juce::AudioChannelSet::mono());
        mono.outputBuses.add (juce::AudioChannelSet::mono());
        check (amp->setBusesLayout (mono) && modeOf (*amp, false) == (int) params::StereoMode::mono,
               "...on MONO once the host settles a mono bus");

        const auto settled = valuesOf (*amp);
        amp->prepareToPlay (48000.0, 256);
        juce::AudioBuffer<float> buf (1, 256);
        buf.clear();
        juce::MidiBuffer none;
        amp->processBlock (buf, none);
        amp->releaseResources();
        const auto moved = movedFrom (*amp, settled);
        check (moved.isEmpty(), "...and preparing it on that bus moves nothing" + (moved.isEmpty() ? juce::String() : ": " + moved.joinIntoString ("; ")));

        juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Standalone);
        auto standalone = std::make_unique<AmpProcessor> (products::orbitAmp());
        juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
        check (modeOf (*standalone, false) == (int) params::StereoMode::stereoSpace
                   && modeOf (*standalone, true) == (int) params::StereoMode::stereoSpace,
               "the standalone opens on STEREO SPACE, and declares it");
    }

    // THE SHELF: each product's own, read for it.
    check (cabinetsOn (products::orbitAmp()) > cabinetsOn (products::catpitAmp()) && cabinetsOn (products::catpitAmp()) > 0,
           "OrbitAmp plays the whole cabinet shelf (" + juce::String (cabinetsOn (products::orbitAmp())) + "), the cockpit Darwin's Cat's own ("
               + juce::String (cabinetsOn (products::catpitAmp())) + ")");
    check (AmpProcessor::cabIrBytes (products::orbitAmp(), 0).data != AmpProcessor::cabIrBytes (products::catpitAmp(), 0).data,
           "...and the cabinet each opens its shelf on is its own");

    // TWO AT ONCE — and every one of them at once, the cockpit twice.
    {
        const auto amps = family();
        std::vector<std::unique_ptr<AmpProcessor>> all;
        for (const auto& amp : amps)
            all.push_back (std::make_unique<AmpProcessor> (amp.product));
        all.push_back (std::make_unique<AmpProcessor> (products::catpitAmp()));

        bool named = true, ownLists = true, ownChannels = true;
        for (size_t i = 0; i < all.size(); ++i)
        {
            const auto& expected = i < amps.size() ? amps[i] : amps[1];     // the last, the second cockpit
            int bundled = 0;
            named    = named && all[i]->getName() == juce::String (expected.product.name);
            ownLists = ownLists && playsItsOwnList (*all[i], bundled);
            if (expected.lineup != nullptr)
                ownChannels = ownChannels && &channels::Engine::of (*all[i]).channels.lineup() == expected.lineup;
        }
        check (named, "all five side by side, and a second cockpit, each says its own name");
        check (ownLists, "...each offers its own list's devices");
        check (ownChannels, "...each plays its own channels, the two cockpits the same");
        check (all[0]->getParameters().size() != all[1]->getParameters().size(),
               "...and OrbitAmp's parameters are not the cockpit's");
    }

    // THE SETTINGS, the update badge and the switches the faces cache: each product's own.
    {
        juce::StringArray settings, updates;
        std::set<const void*> caches;
        for (const auto& amp : family())
        {
            settings.addIfNotAlreadyThere (prefs::store (amp.product).file().getFullPathName());
            const auto update = prefs::UpdateStore::of (amp.product);
            updates.addIfNotAlreadyThere (update->file()->getFile().getFullPathName());
            caches.insert (&prefs::spectraCache (amp.product));
            std::printf ("      %s: %s\n", amp.product.name, prefs::store (amp.product).file().getFullPathName().toRawUTF8());
        }
        check (settings.size() == 5, "each product keeps its settings in a file of its own");
        check (updates.size() == 5, "...and its update badge in a file of its own");
        check (caches.size() == 5, "...and the switches the faces cache are its own");

        const auto a = prefs::UpdateStore::of (products::redPawAmp());
        const auto b = prefs::UpdateStore::of (products::redPawAmp());
        check (a == b && a != prefs::UpdateStore::of (products::frostPawAmp()),
               "two processors of one product share its update store, and no other product's");

        // A switch thrown in one product is not thrown in another.
        prefs::setTunerFloor (products::catpitAmp(), -80);
        prefs::setTunerFloor (products::redPawAmp(), -50);
        check (prefs::tunerFloor (products::catpitAmp()) == -80 && prefs::tunerFloor (products::redPawAmp()) == -50,
               "a switch thrown in the cockpit stays the cockpit's");
    }

    std::printf ("%s\n", failed == 0 ? "PASSED" : "FAILED");
    return failed == 0 ? 0 : 1;
}
