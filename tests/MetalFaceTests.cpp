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
//   - THE WORLD BEHIND THE GLASS is the metal world (MetalDrive), in the channel's gear — CLEAN GAIN LO 1, CLEAN GAIN HI 2,
//     LEAD GAIN LO 3, LEAD GAIN HI 4, the storm — and parked at PARK; every file it reads lies in the factory folder's
//     Faces/MetalAmp as the build lays it out (MetalShipped.h, the check metal_world_test makes of resources/metal);
//   - a still: the face renders whole, the cat in the maker's plate silver at rest, and crimson in flames under the hand;
//   - the site's kit (ORBITAMP_WEB_EXPORT): places.json's "world" in the walker's words — the glass the whole stage, the
//     world's four gears under the four channels, the lever's five stops PARK first, the columns — and a poster a stop.
//
// Each control is found by the name the face gives it, the knobs and the keys by the strip they stand in
// (metal/MetalLayout.h). With METAL_FACE_STILLS naming a folder, the stills are written there as PNG, at 1x and 2x, to be
// looked at. main() returns 0 when every check held.

#include "GateMain.h"
#include "PluginProcessor.h"
#include "Parameters.h"
#include "channels/Engine.h"
#include "device/FactorySet.h"
#include "face/Console.h"
#include "face/Controls.h"
#include "face/Monitor.h"
#include "face/Sheet.h"
#include "redgroove/RedGrooveControls.h"
#include "metal/MetalControls.h"
#include "metal/MetalLayout.h"
#include "metal/MetalLineup.h"
#include "metal/view/MetalArt.h"
#include "redpaw/view/Windscreen.h"
#include "MetalShipped.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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

    template <typename T>
    T* findOf (juce::Component& in)
    {
        if (auto* t = dynamic_cast<T*> (&in))
            return t;
        for (auto* child : in.getChildren())
            if (auto* found = findOf<T> (*child))
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

    /** ...and every one placed there, showing or not: MEM is hidden in a host, where the process's memory is the host's. */
    template <typename Kind>
    std::vector<Kind*> placedIn (juce::Component& in, juce::Rectangle<float> area)
    {
        std::vector<Kind*> out;
        std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
        {
            if (auto* k = dynamic_cast<Kind*> (&c); k != nullptr && area.contains (k->getBounds().toFloat().getCentre()))
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

    /** Sets a variable of this process's environment, empty to clear it — for the window made next to read. */
    void setEnvironment (const char* name, const juce::String& value)
    {
       #if JUCE_WINDOWS
        _putenv_s (name, value.toRawUTF8());
       #else
        setenv (name, value.toRawUTF8(), 1);
       #endif
    }

    juce::Rectangle<float> rectOf (const juce::var& v)
    {
        return { (float) v["x"], (float) v["y"], (float) v["w"], (float) v["h"] };
    }

    bool isRect (const juce::var& v)
    {
        return v.isObject() && v.hasProperty ("x") && v.hasProperty ("y") && v.hasProperty ("w") && v.hasProperty ("h");
    }

    /** Every object and array under `v`, `v` first. */
    void visitAll (const juce::var& v, const std::function<void (const juce::var&)>& visit)
    {
        visit (v);
        if (const auto* a = v.getArray())
            for (const auto& item : *a)
                visitAll (item, visit);
        else if (auto* o = v.getDynamicObject())
            for (const auto& p : o->getProperties())
                visitAll (p.value, visit);
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
    const juce::StringArray own { "lever", "note", "comp", "squeeze", "fx1", "fx2", "reverb", "gate", "limiter", "cabinet", "logo" };
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

    // THE WORLD BEHIND THE GLASS: the metal world, a gear a channel, parked at PARK.
    {
        auto* view = findOf<redpaw::Windscreen> (*face);
        const auto line = [view] { return view != nullptr ? view->statusLine() : juce::String ("no view"); };
        check (view != nullptr && line().startsWith ("GPU  Metal"), "the world behind the glass is the metal one (" + line() + ")");
        for (const int gear : { 1, 2, 3, 4 })
        {
            pull (gear);
            check (line().contains ("gear " + juce::String (gear)) && ! line().contains ("PARK") && ! line().contains ("TURBO"),
                   "..." + nameOf (gear - 1) + " in its gear " + juce::String (gear) + (gear == 4 ? ", the storm" : "") + " (" + line() + ")");
        }
        pull (0);
        check (line().contains ("gear 4 PARK"), "...and parked at PARK, the gear kept (" + line() + ")");
        pull (3);

        const auto dir = device::FactorySet::location().getChildFile ("Faces").getChildFile (ORBITAMP_PRODUCT_NAME);
        const auto lost = metal::shipped::missing (metal::shippedArt());
        juce::StringArray words;
        for (const auto& w : lost)
            words.add (w);
        check (lost.empty(), "the world finds every file it reads in " + dir.getFullPathName()
                                 + (lost.empty() ? juce::String() : ": not " + words.joinIntoString (", ")));
    }

    // THE STRIPS: the playing pack's tones, as many knobs, in the tone strip; in the key strip the E530's BRIGHT and
    // CONTOUR, on every channel, laid as for two, both dark on the V4's packs — which have neither switch — and saying so
    // under the hand; no key for the V4's Boost.
    auto* tag = findOf<face::NameTag> (*face);
    for (const int c : { 0, 1, 2, 3 })
    {
        pull (c + 1);
        const auto knobs = showingIn<face::ArtKnob> (*face, layout::toneStripBox);
        auto keys = showingIn<face::PushButton> (*face, layout::keyStripBox);
        std::sort (keys.begin(), keys.end(), [] (auto* a, auto* b) { return a->getX() < b->getX(); });
        const auto names = channels.toneNamesOf (c);
        check ((int) knobs.size() == names.size() && names.size() >= 3,
               nameOf (c) + "'s tone strip holds its pack's " + juce::String (names.size()) + " tones (" + names.joinIntoString (", ")
                   + "), " + juce::String ((int) knobs.size()) + " knobs");
        const float half = 0.5f * layout::keyPitch;
        bool two = keys.size() == 2;
        juce::StringArray said;
        for (size_t k = 0; two && k < 2; ++k)
        {
            const auto centre = keys[k]->getBounds().toFloat().getCentre();
            two = std::abs (centre.x - (layout::keyStripBox.getCentreX() + (k == 0 ? -half : half))) < 1.0f && std::abs (centre.y - layout::keyY) < 1.0f;
            if (keys[k]->onHover && tag != nullptr)
            {
                keys[k]->onHover (true);
                said.add (tag->says());
                keys[k]->onHover (false);
            }
        }
        check (two, "...its key strip two keys, laid as for two: " + juce::String ((int) keys.size()) + " keys");
        check (two && ! keys[0]->isEnabled() && ! keys[1]->isEnabled() && ! keys[0]->isOn() && ! keys[1]->isOn()
                   && keys[0]->getMouseCursor() == juce::MouseCursor::NormalCursor,
               "...both dark: drawn dim, unlit, no hand's");
        check (said.size() == 2 && said[0] == "BRIGHT  WAITS FOR THE E530 CAPTURES" && said[1] == "CONTOUR  WAITS FOR THE E530 CAPTURES",
               "...BRIGHT and CONTOUR, in that order, each saying under the hand that it waits for the E530's captures: "
                   + said.joinIntoString (" | "));
    }

    // THE V4'S BOOST, held off on every channel (Lineup::Channel::held): off where the channel stands, and a host that throws
    // it on is overruled the next time the channel is pulled.
    {
        bool heldOff = true;
        juce::StringArray wrong;
        for (int c = 0; c < channels.count(); ++c)
        {
            pull (c + 1);
            const auto sw = channels.switchesOf (c);
            const auto boost = std::find_if (sw.begin(), sw.end(), [] (const auto& s) { return s.name == "Boost"; });
            if (boost == sw.end() || ! boost->held || boost->values[boost->opening] != "off")
            {
                heldOff = false;
                wrong.add (nameOf (c) + (boost == sw.end() ? " has no Boost" : " does not hold it off"));
                continue;
            }
            const auto id = params::selectorId (params::preampId, boost->slot);
            const bool offThere = juce::roundToInt (plain (id)) == boost->opening;
            setPlain (id, (float) (1 - boost->opening));    // a host throws it on
            pull (c == 0 ? 2 : 1);
            pull (c + 1);
            const bool offAgain = juce::roundToInt (plain (id)) == boost->opening;
            if (! offThere || ! offAgain)
                wrong.add (nameOf (c) + (offThere ? " keeps the host's Boost on" : " stands with the Boost on"));
            heldOff = heldOff && offThere && offAgain;
        }
        check (heldOff, "every channel holds the V4's Boost off, a host's on overruled when the channel is pulled again"
                            + (wrong.isEmpty() ? juce::String() : ": " + wrong.joinIntoString (", ")));
    }

    // A PACK WITH A BRIGHT SWITCH (one of the gate's own making: the V4's packs have none) makes that key live — one key,
    // off to on, under the face's name, whatever case the pack writes it in — the CONTOUR it lacks dark, and its Boost,
    // held, no key; the very reading the cabin lays its strip with (Console::rightKeysOf).
    {
        std::vector<channels::Channels::PackSwitch> pack (2);
        pack[0].slot = 0; pack[0].name = "Boost";  pack[0].values = juce::StringArray { "off", "on" }; pack[0].held = true;
        pack[1].slot = 1; pack[1].name = "BRIGHT"; pack[1].values = juce::StringArray { "off", "on" };
        const auto laid = face::Console::keysNamed ({ "Bright", "Contour" }, pack, { 2, 3, 4, 5 });
        check (laid.size() == 2 && ! laid[0].dark && laid[0].button == 2 && laid[0].slot == 1 && laid[0].position == 1 && laid[0].off == 0
                   && laid[0].name == "BRIGHT" && laid[1].dark && laid[1].slot < 0 && laid[1].button == 3 && laid[1].name == "CONTOUR",
               "a pack with a Bright switch makes BRIGHT live, its off/on key; CONTOUR, which it lacks, stays dark; its held Boost has no key");
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

    // THE CABINET'S SCREEN: the playing cabinet's name alone, no CAB:, in the gap over the slider; the picture under it no
    // spectrum but the service readings, PRESET and the version over MEM and CPU, two lines centred in its glass; nothing of
    // them left round the maker's plate.
    {
        const auto names = showingIn<redgroove::Readout> (*face, layout::cabinetScreen.withY (layout::cabinetScreen.getY() - 40.0f).withHeight (40.0f));
        auto expected = channels.slotName (channels.cabinetSlot());
        expected = expected.fromFirstOccurrenceOf (": ", false, false);
        const auto said = names.size() == 1 ? names[0]->says() : juce::String();
        check (names.size() == 1 && said == expected && ! said.containsIgnoreCase ("CAB:")
                   && names[0]->getBounds().toFloat().getBottom() <= layout::cabinetScreen.getY() + 1.0f,
               "the cabinet's name alone over the slider, no CAB: \"" + said + "\" (" + expected + ")");

        auto readings = placedIn<face::Legend> (*face, layout::cabinetPicture);
        std::sort (readings.begin(), readings.end(), [] (auto* a, auto* b) { return a->getY() != b->getY() ? a->getY() < b->getY() : a->getX() < b->getX(); });
        juce::Rectangle<float> spread;
        for (auto* r : readings)
            spread = spread.isEmpty() ? r->getBounds().toFloat() : spread.getUnion (r->getBounds().toFloat());
        const bool twoLines = readings.size() == 4 && readings[0]->getY() == readings[1]->getY() && readings[2]->getY() == readings[3]->getY()
                           && readings[2]->getY() - readings[0]->getY() >= 30 && layout::cabinetPicture.contains (spread)
                           && std::abs (spread.getCentreY() - layout::cabinetPicture.getCentreY()) < 2.0f;
        check (twoLines, "...the picture under it four readings in two lines centred in its glass: " + juce::String ((int) readings.size()));
        check (findOf<face::Monitor> (*face) == nullptr, "...and no spectrum on it, nor any view of the amp at work");
        const juce::Rectangle<float> rail { 540.0f, 640.0f, 600.0f, 60.0f };
        check (placedIn<face::Legend> (*face, rail).empty(), "...and none of the readings round the maker's plate any more");
    }

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

    // ...and the two strips at twice the stage, the keys at rest and BRIGHT under the hand, its tag up.
    {
        const auto area = layout::toneStripBox.getUnion (layout::keyStripBox).expanded (24.0f, 18.0f);
        const auto shoot = [&] (const char* name, juce::Rectangle<float> box)
        {
            writeStill (face->createComponentSnapshot ((box * stageScale).toNearestInt(), true, 2.0f / stageScale), name);
        };
        shoot ("metal-face-keys-2x.png", area);
        auto keys = showingIn<face::PushButton> (*face, layout::keyStripBox);
        std::sort (keys.begin(), keys.end(), [] (auto* a, auto* b) { return a->getX() < b->getX(); });
        if (! keys.empty() && keys[0]->onHover)
        {
            keys[0]->onHover (true);
            shoot ("metal-face-keys-hover-2x.png", tag != nullptr ? area.getUnion (tag->getBounds().toFloat().expanded (12.0f)) : area);
            keys[0]->onHover (false);
        }
    }

    // ...and, where the stills are asked for, the window as the standalone shows it — MEM read beside CPU on the cabinet's
    // picture, which a host's window leaves out (Console::timerCallback) — from an amp made as the standalone's: the face at
    // 1x, and the cabinet's screen at twice the stage.
    if (juce::SystemStats::getEnvironmentVariable ("METAL_FACE_STILLS", {}).isNotEmpty())
    {
        juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Standalone);
        auto alone = std::make_unique<AmpProcessor> (ORBITAMP_GATE_PRODUCT());
        juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
        alone->inlineLoads = true;
        std::unique_ptr<juce::AudioProcessorEditor> window (alone->createEditor());
        for (int i = 0; i < 15; ++i)
            settle();
        if (window != nullptr)
        {
            const auto s = (float) window->getWidth() / layout::width;
            const auto cab = layout::cabinetName.getUnion (layout::cabinetPicture).expanded (24.0f, 14.0f);
            writeStill (window->createComponentSnapshot (window->getLocalBounds(), true, 1.0f), "metal-face-standalone-1x.png");
            writeStill (window->createComponentSnapshot ((cab * s).toNearestInt(), true, 2.0f / s), "metal-face-cab-2x.png");
        }
        window.reset();
        alone.reset();
    }

    face.reset();

    // THE SITE'S KIT: a window that comes up with ORBITAMP_WEB_EXPORT naming a folder writes it there (Console::exportForWeb),
    // and what is the E530's own is under places.json's "world", in the walker's words (MetalEditor::webExtras).
    {
        const auto kit = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("MetalWebKit-" + juce::String (juce::Time::currentTimeMillis()));
        setEnvironment ("ORBITAMP_WEB_EXPORT", kit.getFullPathName());
        face.reset (amp->createEditor());
        setEnvironment ("ORBITAMP_WEB_EXPORT", {});
        for (int waited = 0; waited < 1800 && ! kit.getChildFile ("done.txt").existsAsFile(); ++waited)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        const auto json = juce::JSON::parse (kit.getChildFile ("places.json"));
        check (json.isObject() && kit.getChildFile ("done.txt").existsAsFile(), "the site's kit is written, and places.json reads");

        const juce::Rectangle<float> whole { 0.0f, 0.0f, layout::width, layout::height };
        const auto& world = json["world"];
        check (isRect (json["window"]) && rectOf (json["window"]) == whole && isRect (world["dome"]) && rectOf (world["dome"]) == whole
                   && json["stills"].size() == 1 && json["stills"][0].toString() == "still.webp",
               "...the glass the whole stage, the world under it, its still named");
        check ((int) world["gears"] == 4 && world["channelGears"].size() == 4 && (int) world["channelGears"][0] == 1
                   && (int) world["channelGears"][1] == 2 && (int) world["channelGears"][2] == 3 && (int) world["channelGears"][3] == 4,
               "...the world's four gears under the four channels, a gear a channel, LEAD GAIN HI the storm");

        const auto& lever = world["lever"];
        bool stops = lever["stops"].size() == (int) layout::leverStops.size();
        for (int i = 0; stops && i < (int) layout::leverStops.size(); ++i)
            stops = std::abs ((float) lever["stops"][i] - layout::leverStops[(size_t) i]) < 0.01f;
        bool names = lever["names"].size() == 5 && lever["names"][0]["name"].toString() == "PARK" && isRect (lever["box"]);
        for (int i = 0; names && i < 5; ++i)
        {
            const auto& n = lever["names"][i];
            names = (i == 0 || n["name"].toString() == nameOf (i - 1).toUpperCase())
                 && rectOf (lever["box"]).contains ((float) n["at"]["x"], (float) n["at"]["y"]);
        }
        check (stops && names, "...the lever's five stops where the plate has them, PARK first and then the channels, each named where its glyph stands");
        check (json["posters"].size() == 5, "...a poster for each stop, PARK's among them: " + juce::String (json["posters"].size()));

        const juce::StringArray columnIds { "comp", "fx1", "fx2", "reverb" };
        bool columns = world["columns"].size() == columnIds.size();
        for (int i = 0; columns && i < columnIds.size(); ++i)
        {
            const auto& c = world["columns"][i];
            const bool dark = c["id"].toString() == "fx2";
            columns = c["id"].toString() == columnIds[i] && isRect (c["box"])
                   && (dark ? c["param"].isVoid() || c["param"].toString().isEmpty() : amp->apvts.getParameter (c["param"].toString()) != nullptr);
        }
        check (columns, "...the columns COMP, FX 1, FX 2 and REVERB, each with the parameter it moves, FX 2 none");

        bool onStage = true, named = true;
        visitAll (world, [&] (const juce::var& v)
        {
            if (isRect (v))
                onStage = onStage && whole.contains (rectOf (v)) && ! rectOf (v).isEmpty();
            if (v.isObject() && v.hasProperty ("param") && v["param"].toString().isNotEmpty())
                named = named && amp->apvts.getParameter (v["param"].toString()) != nullptr;
        });
        check (onStage && named, "...and every box in the world on the stage, every parameter it names the amp's");

        // ...and on every channel its key strip BRIGHT and CONTOUR, both dark, no key for the Boost, which the channel holds off.
        const auto& line = json["lineup"]["channels"];
        bool kitKeys = line.size() == channels.count();
        juce::StringArray kitWrong;
        for (int c = 0; kitKeys && c < line.size(); ++c)
        {
            const auto& keys = line[c]["face"]["keys"];
            bool ok = keys.size() == 2 && keys[0]["text"].toString() == "BRIGHT" && keys[1]["text"].toString() == "CONTOUR";
            for (int k = 0; ok && k < 2; ++k)
                ok = (bool) keys[k]["dark"] && ! keys[k].hasProperty ("param") && ! keys[k].hasProperty ("control")
                  && keys[k]["says"].toString().endsWith ("WAITS FOR THE E530 CAPTURES");
            bool boostHeldOff = false;
            if (const auto* sw = line[c]["switches"].getArray())
                for (const auto& w : *sw)
                    if (w["name"].toString() == "Boost")
                        boostHeldOff = (bool) w["held"] && w["values"][(int) w["opening"]].toString() == "off";
            if (! ok || ! boostHeldOff)
                kitWrong.add (nameOf (c) + (ok ? ": the Boost not held off" : ": keys " + juce::JSON::toString (keys, true)));
            kitKeys = kitKeys && ok && boostHeldOff;
        }
        check (kitKeys, "...every channel's key strip in the kit BRIGHT and CONTOUR, both dark, no Boost key, the Boost held off"
                            + (kitWrong.isEmpty() ? juce::String() : ": " + kitWrong.joinIntoString ("; ")));

        face.reset();
        kit.deleteRecursively();
    }

    amp.reset();
    std::printf ("%s\n", failed == 0 ? "PASSED" : "FAILED");
    return failed == 0 ? 0 : 1;
}
