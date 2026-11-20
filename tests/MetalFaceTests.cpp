// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Gate for METALAMP'S FACE (src/metal/MetalEditor.h), the ENGL E530 in the metal world's cabin, its window made and never
// shown, run as MetalAmp out of the build's factory folder:
//
//   - the lever: five stops, PARK over the four channels, CLEAN GAIN LO and HI, LEAD GAIN LO and HI. Each channel's stop
//     plays its own pack — the stand-in sound's Copper, Sheriff, Kraken and Kraken again — and LEAD GAIN HI opens on a
//     hotter Gain than LEAD GAIN LO (Lineup::Channel::drive). PARK is the amp's bypass and leaves the channel as it was;
//     a host's bypass moves the lever to PARK; the stop it lands on is named on the tuner's screen;
//   - the strips: as many tone knobs as the playing pack has tones, in the tone strip, and its switch's key in the key
//     strip;
//   - COMP, its fader the threshold alone, and what the cabin holds of it (Lineup::pinned) — in the strip and on, three
//     quarters through it at 4:1 on OPTO; FX 1 the delay's amount, REVERB the room's share, FX 2 dark;
//   - a still: the face renders whole, the cat in the maker's plate silver at rest, and crimson in flames under the hand.
//
// Each control is found by the name the face gives it, the knobs and the keys by the strip they stand in
// (metal/MetalLayout.h). With METAL_FACE_STILLS naming a folder, the stills are written there as PNG, at 1x and 2x, to be
// looked at. main() returns 0 when every check held.

#include "GateMain.h"
#include "PluginProcessor.h"
#include "Parameters.h"
#include "channels/Engine.h"
#include "face/Controls.h"
#include "metal/MetalControls.h"
#include "metal/MetalLayout.h"
#include "metal/MetalLineup.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <vector>

namespace
{
    juce::Component* named (juce::Component& in, const juce::String& id)
    {
        if (in.getComponentID() == id)
            return &in;
        for (auto* child : in.getChildren())
            if (auto* found = named (*child, id))
                return found;
        return nullptr;
    }

    /** Every component of a kind under `in` that is showing and stands, by its centre in its parent's space (the stage's),
        inside `area`. */
    template <typename Kind>
    std::vector<Kind*> showingIn (juce::Component& in, juce::Rectangle<float> area)
    {
        std::vector<Kind*> out;
        std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
        {
            if (auto* k = dynamic_cast<Kind*> (&c); k != nullptr && k->isVisible() && area.contains (k->getBounds().toFloat().getCentre()))
                out.push_back (k);
            for (auto* child : c.getChildren())
                walk (*child);
        };
        walk (in);
        return out;
    }

    /** How much red and how much blue a picture's area holds, summed over its pixels. */
    std::pair<double, double> redAndBlue (const juce::Image& img, juce::Rectangle<int> area)
    {
        double r = 0.0, b = 0.0;
        area = area.getIntersection (img.getBounds());
        for (int y = area.getY(); y < area.getBottom(); ++y)
            for (int x = area.getX(); x < area.getRight(); ++x)
            {
                const auto c = img.getPixelAt (x, y);
                r += c.getFloatRed() * c.getFloatAlpha();
                b += c.getFloatBlue() * c.getFloatAlpha();
            }
        return { r, b };
    }

    void writeStill (const juce::Image& img, const juce::String& name)
    {
        const auto dir = juce::SystemStats::getEnvironmentVariable ("METAL_FACE_STILLS", {});
        if (dir.isEmpty() || ! img.isValid())
            return;
        const auto file = juce::File (dir).getChildFile (name);
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat png;
        if (out.openedOk())
            png.writeImageToStream (img, out);
    }
}

int ORBITAMP_GATE_MAIN()
{
    using namespace orbitamp;
    namespace layout = metal::layout;
    const juce::ScopedJuceInitialiser_GUI juceInit;

    int failed = 0;
    const auto check = [&failed] (bool ok, const juce::String& what)
    {
        std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
        if (! ok)
            ++failed;
    };

    auto amp = std::make_unique<AmpProcessor> (ORBITAMP_GATE_PRODUCT());
    amp->inlineLoads = true;
    const auto plain = [&amp] (const juce::String& id) { auto* p = amp->apvts.getParameter (id); return p != nullptr ? p->convertFrom0to1 (p->getValue()) : -1000.0f; };
    const auto setPlain = [&amp] (const juce::String& id, float v) { if (auto* p = amp->apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v)); };
    const auto settle = [] { juce::MessageManager::getInstance()->runDispatchLoopUntil (100); };

    auto& engine   = channels::Engine::of (*amp);
    auto& channels = engine.channels;
    auto* channel  = amp->apvts.getParameter (engine.channelId());
    auto* device   = amp->apvts.getParameter (params::preampDevice);
    auto& bypass   = amp->bypassParameter();
    const auto asked   = [channel] { return juce::roundToInt (channel->convertFrom0to1 (channel->getValue())); };
    const auto playing = [device]  { return juce::roundToInt (device->convertFrom0to1 (device->getValue())); };
    const auto nameOf  = [&channels] (int c) { return juce::String (channels.lineup().channels[(size_t) c].name); };
    const auto out     = [&bypass] { return bypass.get(); };
    const auto rigOf   = [&amp] (int pack) { return pack >= 0 && pack < amp->preamp.packs.size() ? amp->preamp.packs.getReference (pack).rigId : juce::String(); };

    std::unique_ptr<juce::AudioProcessorEditor> face (amp->createEditor());
    const juce::StringArray own { "lever", "note", "comp", "squeeze", "fx1", "fx2", "reverb", "gate", "limiter", "cabinet", "screen", "logo" };
    bool all = face != nullptr;
    for (const auto& id : own)
        all = all && named (*face, id) != nullptr;
    check (all, "the E530's face comes up, its own controls on it: " + own.joinIntoString (", "));
    if (! all)
    {
        std::printf ("FAILED\n");
        return 1;
    }
    settle();

    // THE LEVER: PARK, then the four channels. A fresh one opens on CLEAN GAIN LO, the amp in.
    auto* lever = dynamic_cast<metal::GlyphLever*> (named (*face, "lever"));
    auto* note  = dynamic_cast<metal::TunerNote*> (named (*face, "note"));
    check (lever != nullptr && lever->numPlaces() == 5 && channels.count() == 4
               && nameOf (0) == "Clean Gain Lo" && nameOf (1) == "Clean Gain Hi" && nameOf (2) == "Lead Gain Lo" && nameOf (3) == "Lead Gain Hi",
           "the lever has five stops: PARK, then CLEAN GAIN LO, CLEAN GAIN HI, LEAD GAIN LO and LEAD GAIN HI");
    check (lever != nullptr && ! out() && channels.active() == 0 && lever->getPlace() == 1,
           "...and a fresh one stands on CLEAN GAIN LO, the amp in");

    const std::array<const char*, 4> captures { "v4-copper-4ae0", "v4-sheriff-ch1-82ab", "v4-kraken-ch1-f7fc", "v4-kraken-ch1-f7fc" };
    const auto pull = [&] (int place) { if (lever != nullptr) lever->setPlace (place, false, true); settle(); };
    for (const int c : { 1, 2, 3, 0 })      // CLEAN GAIN LO last: the lever starts on it, and comes back to it
    {
        if (lever == nullptr)
            break;
        pull (c + 1);
        check (! out() && channels.active() == c && asked() == c && playing() == channels.packFor (c) && rigOf (playing()) == captures[(size_t) c],
               "its stop " + juce::String (c + 1) + " plays " + nameOf (c) + ", its pack " + captures[(size_t) c] + " (" + rigOf (playing()) + ")");
        check (note != nullptr && note->isVisible() && note->says() == nameOf (c).toUpperCase(),
               "...and names it on the tuner's screen: " + (note != nullptr ? note->says() : juce::String()));
    }

    // LEAD GAIN HI: LEAD GAIN LO's pack, its Gain hotter where it opens.
    pull (3);
    const float leadLo = plain (params::preampGain);
    pull (4);
    const float leadHi = plain (params::preampGain);
    check (std::abs (leadLo - 5.0f) < 1.0e-3f && std::abs (leadHi - metal::leadHiDrive) < 1.0e-3f,
           "LEAD GAIN LO opens on Gain 5, LEAD GAIN HI on the same pack at " + juce::String (metal::leadHiDrive, 1)
               + " (" + juce::String (leadLo, 2) + ", " + juce::String (leadHi, 2) + ")");

    if (lever != nullptr)
    {
        pull (0);
        check (out() && channels.active() == 3 && asked() == 3 && lever->getPlace() == 0 && note != nullptr && note->says() == "PARK",
               "the lever pulled to PARK takes the amp out — its bypass on — leaves the channel on LEAD GAIN HI, and says PARK");
        pull (2);
        check (! out() && channels.active() == 1 && lever->getPlace() == 2, "...pulled to CLEAN GAIN HI brings the amp back in, on it");
        bypass.setValueNotifyingHost (1.0f);
        settle();
        check (lever->getPlace() == 0 && channels.active() == 1, "a host's bypass on moves the lever to PARK, the channel left");
        bypass.setValueNotifyingHost (0.0f);
        settle();
        check (lever->getPlace() == 2, "...and off puts it back on the channel that plays");
        face->keyPressed (juce::KeyPress ('3'));
        settle();
        check (channels.active() == 2 && lever->getPlace() == 3, "the 3 key pulls LEAD GAIN LO");
    }

    // THE STRIPS: the playing pack's tones, as many knobs, in the tone strip; its switch's key in the key strip.
    for (const int c : { 0, 2 })
    {
        pull (c + 1);
        const auto knobs = showingIn<face::ArtKnob> (*face, layout::toneStripBox);
        const auto keys  = showingIn<face::PushButton> (*face, layout::keyStripBox);
        const auto names = channels.toneNamesOf (c);
        check ((int) knobs.size() == names.size() && names.size() >= 3,
               nameOf (c) + "'s tone strip holds its pack's " + juce::String (names.size()) + " tones (" + names.joinIntoString (", ")
                   + "), " + juce::String ((int) knobs.size()) + " knobs");
        check (keys.size() == 1, "...and its key strip the pack's one switch, the V4's Boost (" + juce::String ((int) keys.size()) + " keys)");
    }

    // COMP: the threshold alone, and what the cabin holds of it.
    auto* comp = dynamic_cast<face::ColumnFader*> (named (*face, "comp"));
    const auto holds = [&]
    {
        const auto* character = amp->apvts.getParameter (params::compCharacter);
        return plain (params::compPresent) > 0.5f && plain (params::compOn) > 0.5f && std::abs (plain (params::compAmount) - 0.75f) < 1.0e-4f
            && std::abs (plain (params::compRatio) - 4.0f) < 1.0e-3f && character != nullptr && character->getCurrentValueAsText() == "Opto";
    };
    check (holds(), "COMP in the strip and on, three quarters of the signal through it at 4:1 on OPTO");
    if (comp != nullptr)
    {
        setPlain (params::compThreshold, -36.0f);
        check (std::abs (comp->getValue() - 0.75f) < 1.0e-3f, "COMP's fader stands three quarters up at -36 dB");
        comp->setValue (0.25f, true);
        check (std::abs (plain (params::compThreshold) + 12.0f) < 0.05f, "...a quarter up sets -12 dB");
    }

    // THE EFFECTS: FX 1 the delay's amount, REVERB the room's share, FX 2 dark.
    auto* fx1 = dynamic_cast<face::ColumnFader*> (named (*face, "fx1"));
    auto* fx2 = dynamic_cast<face::ColumnFader*> (named (*face, "fx2"));
    auto* reverb = dynamic_cast<face::ColumnFader*> (named (*face, "reverb"));
    if (fx1 != nullptr && reverb != nullptr)
    {
        fx1->setValue (0.4f, true);
        reverb->setValue (0.25f, true);
        check (std::abs (plain (params::delayMix) - 40.0f) < 0.1f && std::abs (plain (params::reverbMix) - 25.0f) < 0.1f,
               "FX 1 sets the delay's amount and REVERB the room's share (" + juce::String (plain (params::delayMix), 1) + ", "
                   + juce::String (plain (params::reverbMix), 1) + ")");
    }
    bool takes = true, children = true;
    if (fx2 != nullptr)
        fx2->getInterceptsMouseClicks (takes, children);
    check (fx2 != nullptr && ! fx2->isEnabled() && ! takes, "FX 2 is dark, and takes no hand");

    // A STILL: the face whole, the cat silver at rest and crimson in flames under the hand.
    auto* logo = dynamic_cast<metal::CatLogo*> (named (*face, "logo"));
    const auto stageScale = (float) face->getWidth() / layout::width;
    const auto logoArea = (layout::logoBox * stageScale).toNearestInt();
    const auto rest = face->createComponentSnapshot (face->getLocalBounds());
    const auto [restRed, restBlue] = redAndBlue (rest, logoArea);
    if (logo != nullptr)
        logo->force (true, 0.75);
    const auto hot = face->createComponentSnapshot (face->getLocalBounds());
    const auto [hotRed, hotBlue] = redAndBlue (hot, logoArea);
    check (rest.isValid() && rest.getWidth() > 900 && restRed > 50.0 && std::abs (restRed - restBlue) < 0.2 * restBlue,
           "a still of the face renders, the cat in the maker's plate silver: red " + juce::String (juce::roundToInt (restRed)) + ", blue " + juce::String (juce::roundToInt (restBlue)));
    check (hotRed > 1.5 * hotBlue, "...and under the hand crimson: red " + juce::String (juce::roundToInt (hotRed)) + ", blue " + juce::String (juce::roundToInt (hotBlue)));
    writeStill (face->createComponentSnapshot (face->getLocalBounds(), true, 1.0f), "metal-face-hot-1x.png");
    if (logo != nullptr)
        logo->force (false);
    writeStill (face->createComponentSnapshot (face->getLocalBounds(), true, 1.0f), "metal-face-1x.png");
    writeStill (face->createComponentSnapshot (face->getLocalBounds(), true, 2.0f), "metal-face-2x.png");

    face.reset();
    amp.reset();
    std::printf ("%s\n", failed == 0 ? "PASSED" : "FAILED");
    return failed == 0 ? 0 : 1;
}
