// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Gate for CALIFORNIAAMP'S FACE (src/california/CaliforniaEditor.h), its window made and never shown, run as the van out
// of the build's factory folder:
//
//   - the lever: PARK over the two channels, CLEAN and DIRTY. PARK is the amp's bypass (AmpProcessor::bypassParameter)
//     and leaves the channel as it was; a channel's stop brings the amp back in, on that channel, playing its pack — the
//     V4 Copper on CLEAN, the V4 Sheriff on DIRTY (the stand-in sound); a host's bypass moves the lever to PARK;
//   - BASS, MIDDLE and TREBLE: each socket's knob turns the playing channel's tone OF THAT NAME wherever the pack lists it
//     — on the Copper, whose first tone is TONE, the BASS socket turns Bass and leaves TONE alone;
//   - COMP's fader the threshold alone, DELAY's and REVERB's their shares; VOLUME and PHASER dark, taking no hand; what
//     the van holds (Lineup::pinned) — COMP in the strip and on at 3/4, 4:1, OPTO; the delay and the room on;
//   - every pictogram has its spot, which names it;
//   - a still of the face renders: the cream panel painted, a pictogram's ink on it;
//   - THE WORLD BEHIND THE GLASS is California (CaliforniaDrive), in the channel's gear — CLEAN 1, DIRTY 2 — and parked
//     at PARK; every file it reads lies in the factory folder's Faces/CaliforniaAmp as the build lays it out
//     (CaliforniaShipped.h, the check california_world_test makes of resources/california), and the face's pendant is
//     the world's, dashboard_pendant.webp.
//
// Each control is found by the name the face gives it. main() returns 0 when every check held.

#include "GateMain.h"
#include "PluginProcessor.h"
#include "Parameters.h"
#include "channels/Engine.h"
#include "device/DeviceLibrary.h"
#include "face/Controls.h"
#include "california/CaliforniaFaceArt.h"
#include "california/CaliforniaLayout.h"
#include "device/FactorySet.h"
#include "redpaw/view/Windscreen.h"
#include "CaliforniaShipped.h"

#include <cmath>
#include <cstdio>
#include <memory>

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

    bool takesHand (juce::Component* c)
    {
        bool self = false, children = false;
        if (c != nullptr)
            c->getInterceptsMouseClicks (self, children);
        return c != nullptr && c->isEnabled() && self;
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

    float brightness (juce::Colour c) { return (c.getFloatRed() + c.getFloatGreen() + c.getFloatBlue()) / 3.0f; }
}

int ORBITAMP_GATE_MAIN()
{
    using namespace orbitamp;
    namespace layout = california::layout;
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
    const auto rigOf   = [&amp, &playing]
    {
        const int pack = playing();
        return pack >= 0 && pack < amp->preamp.packs.size() ? device::DeviceLibrary::rigIdOf (amp->preamp.packs.getReference (pack).location)
                                                            : juce::String();
    };
    const auto nameOf  = [&channels] (int c) { return juce::String (channels.lineup().channels[(size_t) c].name); };
    const auto out     = [&bypass] { return bypass.get(); };

    std::unique_ptr<juce::AudioProcessorEditor> face (amp->createEditor());
    check (face != nullptr && named (*face, "lever") != nullptr && named (*face, "tone-bass") != nullptr,
           "the van's face comes up, its own controls on it");
    if (face == nullptr || named (*face, "lever") == nullptr || named (*face, "tone-bass") == nullptr)
    {
        std::printf ("FAILED\n");
        return 1;
    }
    settle();

    // THE LEVER: PARK, then CLEAN and DIRTY. A fresh van opens on CLEAN, the amp in.
    auto* lever = dynamic_cast<face::DetentLever*> (named (*face, "lever"));
    check (lever != nullptr && lever->numPlaces() == 3 && channels.count() == 2 && nameOf (0) == "CLEAN" && nameOf (1) == "DIRTY",
           "the lever has three places: PARK, then CLEAN and DIRTY");
    check (lever != nullptr && ! out() && channels.active() == 0 && lever->getPlace() == 1 && rigOf() == "v4-copper-4ae0",
           "...and a fresh one stands on CLEAN, the amp in, playing the V4 Copper (" + rigOf() + ")");
    if (lever != nullptr)
    {
        lever->setPlace (2, false, true);       // the hand's
        settle();
        check (! out() && channels.active() == 1 && asked() == 1 && playing() == channels.packFor (1) && rigOf() == "v4-sheriff-ch1-82ab",
               "the lever pulled to DIRTY plays DIRTY, the V4 Sheriff (" + rigOf() + "), and CHANNEL says so");
        lever->setPlace (0, false, true);
        settle();
        check (out() && channels.active() == 1 && lever->getPlace() == 0, "...pulled to PARK takes the amp out — its bypass on — and leaves the channel on DIRTY");
        lever->setPlace (1, false, true);
        settle();
        check (! out() && channels.active() == 0 && rigOf() == "v4-copper-4ae0" && lever->getPlace() == 1,
               "...pulled to CLEAN brings the amp back in, on CLEAN, the V4 Copper playing");

        bypass.setValueNotifyingHost (1.0f);    // the host's
        settle();
        check (lever->getPlace() == 0 && channels.active() == 0, "a host's bypass on moves the lever to PARK, the channel left on CLEAN");
        bypass.setValueNotifyingHost (0.0f);
        settle();
        check (lever->getPlace() == 1, "...and its bypass off puts the lever back on CLEAN");
        face->keyPressed (juce::KeyPress ('2'));
        settle();
        check (! out() && channels.active() == 1 && lever->getPlace() == 2, "the 2 key pulls DIRTY");
    }

    // THE WORLD BEHIND THE GLASS: California, in the channel's gear, parked at PARK.
    {
        auto* view = findOf<redpaw::Windscreen> (*face);
        const auto line = [view] { return view != nullptr ? view->statusLine() : juce::String ("no view"); };
        check (view != nullptr && line().startsWith ("GPU  California"), "the world behind the glass is California (" + line() + ")");
        if (lever != nullptr)
        {
            lever->setPlace (1, false, true);
            settle();
            check (line().contains ("gear 1") && ! line().contains ("PARK"), "...on CLEAN in its first gear (" + line() + ")");
            lever->setPlace (2, false, true);
            settle();
            check (line().contains ("gear 2") && ! line().contains ("PARK"), "...on DIRTY in its second (" + line() + ")");
            lever->setPlace (0, false, true);
            settle();
            check (line().contains ("gear 2 PARK"), "...and parked at PARK, the gear kept (" + line() + ")");
            lever->setPlace (2, false, true);
            settle();
        }

        const auto dir = device::FactorySet::location().getChildFile ("Faces").getChildFile (ORBITAMP_PRODUCT_NAME);
        const auto has = [&dir] (const std::string& rel) { return dir.getChildFile (rel).getSize() > 16; };
        const auto read = [&dir] (const std::string& rel)
        {
            juce::MemoryBlock bytes;
            dir.getChildFile (rel).loadFileAsData (bytes);
            return bytes.getSize() == 0 ? std::string() : std::string (static_cast<const char*> (bytes.getData()), bytes.getSize());
        };
        const auto lost = california::shipped::missing (has, read);
        juce::StringArray words;
        for (const auto& w : lost)
            words.add (w);
        check (lost.empty(), "the world finds every file it reads in " + dir.getFullPathName()
                                 + (lost.empty() ? juce::String() : ": not " + words.joinIntoString (", ")));
        check (california::FaceArt::of().pendant.isValid(), "the face's pendant is the world's, dashboard_pendant.webp, laid out beside it");
    }

    // THE TONES BY NAME: each socket's knob turns the playing channel's tone of its name, wherever its pack lists it.
    const auto socketTurns = [&] (const char* knobId, channels::Channels::Band band, const char* bandName)
    {
        auto* knob = dynamic_cast<face::ArtKnob*> (named (*face, knobId));
        const int c = channels.active();
        const int t = channels.toneNamed (c, band);
        auto* p = t >= 0 ? amp->apvts.getParameter (channels.toneOf (c, t)) : nullptr;
        if (knob == nullptr || p == nullptr || ! takesHand (knob))
            return false;
        knob->setValue (0.83f, true);
        const bool turned = std::abs (p->getValue() - 0.83f) < 1.0e-3f;
        knob->setValue (0.5f, true);
        check (turned, juce::String ("...the ") + bandName + " socket turns " + nameOf (c) + "'s " + channels.toneNamesOf (c)[t]
                           + ", its tone " + juce::String (t + 1) + " of " + juce::String (channels.toneNamesOf (c).size()));
        return turned;
    };
    if (lever != nullptr)
        lever->setPlace (1, false, true);       // CLEAN: the Copper, its tones Tone, Treble, Middle, Bass
    settle();
    {
        const auto names = channels.toneNamesOf (0);
        check (names.size() >= 4 && names[0] == "Tone", "CLEAN's pack lists TONE first, before its Bass (" + names.joinIntoString (", ") + ")");
        auto* first = amp->apvts.getParameter (channels.toneOf (0, 0));
        const float firstBefore = first != nullptr ? first->getValue() : -1.0f;
        socketTurns ("tone-bass", channels::Channels::Band::bass, "BASS");
        auto* knob = dynamic_cast<face::ArtKnob*> (named (*face, "tone-bass"));
        if (knob != nullptr)
            knob->setValue (0.91f, true);
        check (first != nullptr && std::abs (first->getValue() - firstBefore) < 1.0e-6f && channels.toneNamed (0, channels::Channels::Band::bass) != 0,
               "...and leaves CLEAN's TONE where it stood: the sockets go by the tones' names, not their order");
        if (knob != nullptr)
            knob->setValue (0.5f, true);
        socketTurns ("tone-middle", channels::Channels::Band::middle, "MIDDLE");
        socketTurns ("tone-treble", channels::Channels::Band::treble, "TREBLE");
    }
    if (lever != nullptr)
        lever->setPlace (2, false, true);       // DIRTY: the Sheriff, Bass, Middle, Treble
    settle();
    check (channels.active() == 1, "on DIRTY:");
    socketTurns ("tone-bass", channels::Channels::Band::bass, "BASS");
    socketTurns ("tone-middle", channels::Channels::Band::middle, "MIDDLE");
    socketTurns ("tone-treble", channels::Channels::Band::treble, "TREBLE");

    // THE FADERS.
    auto* comp   = dynamic_cast<face::ColumnFader*> (named (*face, "comp"));
    auto* delay  = dynamic_cast<face::ColumnFader*> (named (*face, "delay"));
    auto* reverb = dynamic_cast<face::ColumnFader*> (named (*face, "reverb"));
    if (comp != nullptr && delay != nullptr && reverb != nullptr && named (*face, "squeeze") != nullptr)
    {
        comp->setValue (0.25f, true);
        delay->setValue (0.4f, true);
        reverb->setValue (0.3f, true);
        check (std::abs (plain (params::compThreshold) + 12.0f) < 0.05f && std::abs (plain (params::delayMix) - 40.0f) < 0.05f
                   && std::abs (plain (params::reverbMix) - 30.0f) < 0.05f,
               "COMP's fader a quarter up sets -12 dB; DELAY's and REVERB's set their shares, 40% and 30%");
    }
    else
        check (false, "COMP and its squeeze, DELAY and REVERB are on the face");
    for (const char* dark : { "volume", "phaser" })
    {
        auto* f = named (*face, dark);
        check (f != nullptr && f->isVisible() && ! takesHand (f), juce::String (dark).toUpperCase() + " is a dark fader, taking no hand");
    }
    const auto* character = amp->apvts.getParameter (params::compCharacter);
    check (plain (params::compPresent) > 0.5f && plain (params::compOn) > 0.5f && std::abs (plain (params::compAmount) - 0.75f) < 1.0e-4f
               && std::abs (plain (params::compRatio) - 4.0f) < 1.0e-3f && character != nullptr && character->getCurrentValueAsText() == "Opto"
               && plain (params::delayOn) > 0.5f && plain (params::reverbOn) > 0.5f,
           "the van holds COMP in the strip and on, three quarters through it at 4:1 on OPTO, and the delay and the room on");

    // EVERY PICTOGRAM NAMED.
    {
        const char* signs[] { "bass", "middle", "treble", "gain", "volume", "output", "reverb", "delay", "phaser", "comp", "input",
                              "clean", "dirty", "gate", "limiter", "park" };
        juce::StringArray missing;
        for (const auto* s : signs)
            if (auto* c = named (*face, juce::String ("sign-") + s); c == nullptr || ! c->isVisible() || c->getTitle() != juce::String (s).toUpperCase())
                missing.add (s);
        check (missing.isEmpty(), "every pictogram has a spot that names it" + (missing.isEmpty() ? juce::String() : ": not " + missing.joinIntoString (", ")));
    }

    // A STILL: the face painted, its cream panel and a pictogram's ink on it.
    {
        face->setSize (1672, 941);
        settle();
        const auto still = face->createComponentSnapshot (face->getLocalBounds());
        const float k = (float) still.getWidth() / layout::width;
        const auto at = [&still, k] (float x, float y) { return still.getPixelAt (juce::roundToInt (x * k), juce::roundToInt (y * k)); };
        const auto panel = at (784.0f, 905.0f);                       // the tones' panel, under the knobs
        const auto wedge = at (layout::gain.x + 8.0f * layout::glyphScale, layout::gainGlyphY + 4.0f * layout::glyphScale);
        check (still.isValid() && brightness (panel) > 0.75f && brightness (wedge) < 0.35f,
               "a still of the face renders: the cream panel (" + panel.toDisplayString (false) + ") and GAIN's wedge on it in ink ("
                   + wedge.toDisplayString (false) + ")");
    }

    face.reset();
    amp.reset();
    std::printf ("%s\n", failed == 0 ? "PASSED" : "FAILED");
    return failed == 0 ? 0 : 1;
}
