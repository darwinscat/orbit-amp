// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Gate for DEEPGROOVEBASS'S FACE (src/underwater/UnderwaterEditor.h), its window made and never shown, run as the
// bathyscaphe out of the build's factory folder:
//
//   - the lever: PARK over the three gears, CLEAN, DIRT and DRIVE. PARK is the amp's bypass (AmpProcessor::bypassParameter)
//     and leaves the channel as it was; a gear brings the amp back in, on that channel; a host's bypass moves the lever
//     to PARK, and its CHANNEL while the amp is out leaves it there;
//   - BLEND: DIRT's opens at 80/20 and DRIVE's at 90/10 (Lineup::Channel::blend); CLEAN's stands at the capture alone,
//     whatever its record or a state says (Lineup::Channel::blendHeld, Channels::hold), its fader dark and out of the
//     hand's reach — and in it again on DIRT;
//   - COMP: its fader the threshold alone, 0 dB at its foot and -48 at its head, its squeeze in a column beside it; and
//     what the bathyscaphe holds of it (Lineup::pinned) — on, three quarters of the signal through it at 4:1 on OPTO,
//     BLEND's clean leg unflipped — where it holds them when the face comes up after a host moved them;
//   - CLEAN's two tones, BASS and TREBLE, in the outer sockets and the middle one bare, its frozen knob there out of the
//     hand's reach — on CLEAN alone; DIRT's and DRIVE's three in the three; and each socket's reading the knob that
//     stands in it, and no other.
//
// Each control is found by the name the face gives it; the tone knobs and their readings by the sockets they stand in
// (underwater/UnderwaterLayout.h). That the panels have no titles is the picture's to say, not this gate's. main()
// returns 0 when every check held.

#include "GateMain.h"
#include "PluginProcessor.h"
#include "Parameters.h"
#include "channels/Engine.h"
#include "face/Controls.h"
#include "redgroove/RedGrooveControls.h"
#include "underwater/UnderwaterLayout.h"

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

    /** Every component of a kind under `in`, wherever it stands. */
    template <typename Kind>
    void allOf (juce::Component& in, std::vector<Kind*>& out)
    {
        if (auto* k = dynamic_cast<Kind*> (&in))
            out.push_back (k);
        for (auto* child : in.getChildren())
            allOf (*child, out);
    }

    /** What a component of a kind stands at `centre` of the stage (its parent's space, the stage's px) — within a few px
        — and is showing, and the hand may turn or not as `live` says. */
    template <typename Kind>
    Kind* standingAt (juce::Component& in, juce::Point<float> centre, bool live)
    {
        std::vector<Kind*> all;
        allOf (in, all);
        for (auto* k : all)
            if (k->isVisible() && k->isEnabled() == live && k->getBounds().toFloat().getCentre().getDistanceFrom (centre) < 4.0f)
                return k;
        return nullptr;
    }

    /** Two pictures alike to the last pixel. */
    bool alike (const juce::Image& a, const juce::Image& b)
    {
        if (! a.isValid() || ! b.isValid() || a.getBounds() != b.getBounds())
            return false;
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
                if (a.getPixelAt (x, y) != b.getPixelAt (x, y))
                    return false;
        return true;
    }

    /** Whether a picture has anything painted in it at all. */
    bool painted (const juce::Image& a)
    {
        for (int y = 0; a.isValid() && y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
                if (a.getPixelAt (x, y).getAlpha() > 0)
                    return true;
        return false;
    }
}

int ORBITAMP_GATE_MAIN()
{
    using namespace orbitamp;
    namespace layout = underwater::layout;
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
    const auto settle = [] { juce::MessageManager::getInstance()->runDispatchLoopUntil (100); };     // whatever the face takes on its timer

    auto& engine   = channels::Engine::of (*amp);
    auto& channels = engine.channels;
    auto* channel  = amp->apvts.getParameter (engine.channelId());
    auto* device   = amp->apvts.getParameter (params::preampDevice);
    auto& bypass   = amp->bypassParameter();
    const auto asked   = [channel] { return juce::roundToInt (channel->convertFrom0to1 (channel->getValue())); };
    const auto playing = [device]  { return juce::roundToInt (device->convertFrom0to1 (device->getValue())); };
    const auto nameOf  = [&channels] (int c) { return juce::String (channels.lineup().channels[(size_t) c].name); };
    const auto out     = [&bypass] { return bypass.get(); };

    std::unique_ptr<juce::AudioProcessorEditor> face (amp->createEditor());
    check (face != nullptr && named (*face, "lever") != nullptr && named (*face, "comp") != nullptr,
           "the bathyscaphe's face comes up, its own controls on it");
    if (face == nullptr || named (*face, "lever") == nullptr || named (*face, "comp") == nullptr)
    {
        std::printf ("FAILED\n");
        return 1;
    }
    settle();

    // THE LEVER: PARK, then the three gears. A fresh bathyscaphe opens on DIRT (Lineup::opensAt), the amp in.
    auto* lever = dynamic_cast<face::DetentLever*> (named (*face, "lever"));
    check (lever != nullptr && lever->numPlaces() == 4 && channels.count() == 3
               && nameOf (0) == "CLEAN" && nameOf (1) == "DIRT" && nameOf (2) == "DRIVE",
           "the lever has four places: PARK, then the three gears, CLEAN, DIRT and DRIVE");
    check (lever != nullptr && ! out() && channels.active() == 1 && lever->getPlace() == 2,
           "...and a fresh one stands on DIRT, the gear it opens on, the amp in");
    if (lever != nullptr)
    {
        lever->setPlace (0, false, true);       // the hand's
        settle();
        check (out() && channels.active() == 1 && asked() == 1 && lever->getPlace() == 0,
               "the lever pulled to PARK takes the amp out — its bypass on — and leaves the channel on DIRT");
        lever->setPlace (3, false, true);
        settle();
        check (! out() && channels.active() == 2 && asked() == 2 && playing() == channels.packFor (2) && lever->getPlace() == 3,
               "...pulled to DRIVE brings the amp back in, on DRIVE, its pack playing, and CHANNEL says so");

        bypass.setValueNotifyingHost (1.0f);    // the host's
        settle();
        check (lever->getPlace() == 0 && channels.active() == 2, "a host's bypass on moves the lever to PARK, the channel left on DRIVE");
        channel->setValueNotifyingHost (channel->convertTo0to1 (1.0f));
        engine.pump();
        settle();
        check (out() && channels.active() == 1 && lever->getPlace() == 0, "...a host's CHANNEL while the amp is out moves the channel, not the lever off PARK");
        bypass.setValueNotifyingHost (0.0f);
        settle();
        check (lever->getPlace() == 2 && channels.active() == 1, "...and its bypass off puts the lever on the gear that plays, DIRT");

        bypass.setValueNotifyingHost (1.0f);
        settle();
        face->keyPressed (juce::KeyPress ('3'));
        settle();
        check (! out() && channels.active() == 2 && lever->getPlace() == 3, "the 3 key, the amp out, pulls DRIVE: the amp back in, on DRIVE");
    }

    // BLEND, each gear's own: where DIRT's and DRIVE's open (the owner, 17.11), and CLEAN's held at the capture alone.
    auto* blend = dynamic_cast<face::ColumnFader*> (named (*face, "blend"));
    const auto pull = [&] (int c) { if (lever != nullptr) lever->setPlace (c + 1, false, true); settle(); };
    const auto takesHand = [] (juce::Component* c)
    {
        bool self = false, children = false;
        if (c != nullptr)
            c->getInterceptsMouseClicks (self, children);
        return c != nullptr && c->isEnabled() && self;
    };
    check (blend != nullptr, "BLEND's fader is on the face");
    if (blend != nullptr)
    {
        pull (2);
        check (std::abs (plain (params::ampBlend) - 0.9f) < 1.0e-4f && std::abs (blend->getValue() - 0.9f) < 1.0e-3f && takesHand (blend),
               "DRIVE's BLEND opens at 90/10, its fader there and in the hand's reach (" + juce::String (plain (params::ampBlend), 3) + ")");
        pull (1);
        check (std::abs (plain (params::ampBlend) - 0.8f) < 1.0e-4f && std::abs (blend->getValue() - 0.8f) < 1.0e-3f && takesHand (blend),
               "DIRT's opens at 80/20, likewise (" + juce::String (plain (params::ampBlend), 3) + ")");
        pull (0);
        check (std::abs (plain (params::ampBlend) - 1.0f) < 1.0e-4f && std::abs (blend->getValue() - 1.0f) < 1.0e-3f,
               "CLEAN's stands at the capture alone, 100/0 (" + juce::String (plain (params::ampBlend), 3) + ")");
        check (! blend->isEnabled() && ! takesHand (blend), "...its fader dark and taking no click");

        // ...whatever its record says: a host moves CLEAN's BLEND while it plays, and CLEAN, left with it there, keeps it in
        // its record — in a session saved then too. Pulled again (Channels::hold), it stands at the capture alone.
        setPlain (params::ampBlend, 0.4f);
        settle();
        pull (1);
        juce::MemoryBlock session;
        amp->getStateInformation (session);     // a session on DIRT, CLEAN's record in it saying 40/60
        pull (0);
        check (std::abs (plain (params::ampBlend) - 1.0f) < 1.0e-4f,
               "CLEAN pulled again, its record saying 40/60, stands at 100/0 (" + juce::String (plain (params::ampBlend), 3) + ")");
        pull (1);
        amp->setStateInformation (session.getData(), (int) session.getSize());
        engine.pump();
        settle();
        pull (0);
        check (channels.active() == 0 && std::abs (plain (params::ampBlend) - 1.0f) < 1.0e-4f,
               "...and pulled from a session whose CLEAN record says 40/60, too (" + juce::String (plain (params::ampBlend), 3) + ")");
        pull (1);
        check (blend->isEnabled() && takesHand (blend) && std::abs (plain (params::ampBlend) - 0.8f) < 1.0e-4f,
               "DIRT's fader in the hand's reach again, on DIRT's own BLEND");
        blend->setValue (0.55f, true);
        check (std::abs (plain (params::ampBlend) - 0.55f) < 1.0e-4f, "...and it turns DIRT's BLEND");
    }

    // COMP: the threshold alone, turned round — up the fader the deeper — and its squeeze in a column beside it.
    auto* comp = dynamic_cast<face::ColumnFader*> (named (*face, "comp"));
    check (comp != nullptr && dynamic_cast<redgroove::GrColumn*> (named (*face, "squeeze")) != nullptr,
           "COMP is a fader, its squeeze a column beside it");
    if (comp != nullptr)
    {
        setPlain (params::compThreshold, -36.0f);
        check (std::abs (comp->getValue() - 0.75f) < 1.0e-3f, "COMP's fader stands three quarters up at -36 dB");
        comp->setValue (0.25f, true);
        check (std::abs (plain (params::compThreshold) + 12.0f) < 0.05f, "...a quarter up sets -12 dB");
        comp->setValue (1.0f, true);
        const float head = plain (params::compThreshold);
        comp->setValue (0.0f, true);
        const float foot = plain (params::compThreshold);
        check (std::abs (head + 48.0f) < 0.05f && std::abs (foot) < 0.05f,
               "...-48 dB at its head and 0 at its foot (" + juce::String (head, 1) + ", " + juce::String (foot, 1) + ")");
        check (std::abs (comp->defaultValue - 0.5f) < 1.0e-3f, "...and a double click puts it at -24 dB, half way");
    }

    // WHAT THE BATHYSCAPHE HOLDS OF COMP AND BLEND (Lineup::pinned): where it stands, and where it stands again when the
    // face comes up after a host moved it all.
    const auto holds = [&]
    {
        const auto* character = amp->apvts.getParameter (params::compCharacter);
        return plain (params::compOn) > 0.5f && std::abs (plain (params::compAmount) - 0.75f) < 1.0e-4f
            && std::abs (plain (params::compRatio) - 4.0f) < 1.0e-3f && character != nullptr && character->getCurrentValueAsText() == "Opto"
            && plain (params::ampBlendInvert) < 0.5f;
    };
    check (holds(), "COMP on, three quarters of the signal through it at 4:1 on OPTO, and BLEND's clean leg unflipped");
    setPlain (params::compOn, 0.0f);
    setPlain (params::compAmount, 0.3f);
    setPlain (params::compRatio, 9.0f);
    if (auto* character = amp->apvts.getParameter (params::compCharacter))      // any character but OPTO
        character->setValueNotifyingHost (character->convertTo0to1 (params::compCharacters.indexOf ("Opto") == 0 ? 1.0f : 0.0f));
    setPlain (params::ampBlendInvert, 1.0f);
    const bool moved = ! holds();
    face.reset();                   // shut first, as a host shuts one window before it opens the next
    face.reset (amp->createEditor());
    settle();
    check (moved && holds(), "...and the face coming up after a host moved every one of them gives them all again");
    lever = dynamic_cast<face::DetentLever*> (named (*face, "lever"));

    // THE TONES IN THEIR SOCKETS (Strip::spread): CLEAN's BASS and TREBLE in the outer two, the middle bare but for a
    // knob frozen at its noon that takes no hand; DIRT's and DRIVE's BASS, MID and TREBLE in the three. Each knob turns
    // the tone the channel has in its socket.
    const std::array<juce::Point<float>, 3> sockets {{ { layout::toneX[0], layout::toneY }, { layout::toneX[1], layout::toneY }, { layout::toneX[2], layout::toneY } }};
    const auto knobIn  = [&face, &sockets] (size_t s) { return standingAt<face::ArtKnob> (*face, sockets[s], true); };
    const auto frozen  = [&face, &sockets] { return standingAt<face::ArtKnob> (*face, sockets[1], false); };
    const auto turns   = [&amp] (face::ArtKnob* knob, const juce::String& id, float to)
    {
        auto* p = amp->apvts.getParameter (id);
        if (knob == nullptr || p == nullptr)
            return false;
        knob->setValue (to, true);
        return std::abs (p->getValue() - to) < 1.0e-3f;
    };
    // The reading under a socket, and its picture: a reading says its words in paint alone.
    const auto readingIn = [&face] (size_t s)
    {
        return standingAt<redgroove::Readout> (*face, { layout::toneX[s], layout::valueY }, true);
    };
    const auto shot = [] (juce::Component* c) { return c != nullptr ? c->createComponentSnapshot (c->getLocalBounds()) : juce::Image(); };
    const auto tone = [&amp] (const juce::String& id, float to) { if (auto* p = amp->apvts.getParameter (id)) p->setValueNotifyingHost (to); };

    if (lever != nullptr)
        lever->setPlace (1, false, true);       // CLEAN
    settle();
    {
        auto* frozenKnob = frozen();
        bool frozenTakes = true, frozenChildren = true;
        if (frozenKnob != nullptr)
            frozenKnob->getInterceptsMouseClicks (frozenTakes, frozenChildren);
        check (channels.active() == 0 && knobIn (0) != nullptr && knobIn (1) == nullptr && knobIn (2) != nullptr,
               "CLEAN's two tone knobs stand in the outer sockets, at " + juce::String (layout::toneX[0], 0) + " and "
                   + juce::String (layout::toneX[2], 0) + ", the middle one bare");
        check (turns (knobIn (0), channels.toneOf (0, 0), 0.8f) && turns (knobIn (2), channels.toneOf (0, 1), 0.3f)
                   && channels.toneNamesOf (0) == juce::StringArray { "Bass", "Treble" },
               "...the left turning CLEAN's BASS and the right its TREBLE");
        check (frozenKnob != nullptr && ! frozenTakes, "...and a frozen knob in the middle one, taking no hand");

        // The readings: the outer two each its socket's knob; the middle one no knob's.
        auto* left = readingIn (0);
        auto* middle = readingIn (1);
        auto* right = readingIn (2);
        tone (channels.toneOf (0, 0), 0.2f);
        tone (channels.toneOf (0, 1), 0.2f);
        const auto leftBefore = shot (left), middleBefore = shot (middle), rightBefore = shot (right);
        tone (channels.toneOf (0, 0), 0.9f);
        const auto leftAfterBass = shot (left), middleAfterBass = shot (middle), rightAfterBass = shot (right);
        tone (channels.toneOf (0, 1), 0.9f);
        const auto middleAfterTreble = shot (middle), rightAfterTreble = shot (right);
        check (left != nullptr && middle != nullptr && right != nullptr && painted (leftBefore) && painted (middleBefore),
               "a reading stands under each of the three sockets");
        check (! alike (leftBefore, leftAfterBass) && alike (rightBefore, rightAfterBass) && ! alike (rightAfterBass, rightAfterTreble),
               "...on CLEAN the left one reads BASS and the right one TREBLE, each its own socket's knob");
        check (alike (middleBefore, middleAfterBass) && alike (middleBefore, middleAfterTreble),
               "...and the middle one, its socket bare, neither");
    }

    if (lever != nullptr)
        lever->setPlace (2, false, true);       // DIRT
    settle();
    check (channels.active() == 1 && knobIn (0) != nullptr && knobIn (1) != nullptr && knobIn (2) != nullptr && frozen() == nullptr
               && channels.toneNamesOf (1) == juce::StringArray { "Bass", "Mid", "Treble" },
           "DIRT's three tone knobs, BASS, MID and TREBLE, stand in the three sockets, and the frozen one is gone");
    check (turns (knobIn (1), channels.toneOf (1, 1), 0.7f), "...the middle one turning DIRT's MID");
    {
        auto* middle = readingIn (1);
        tone (channels.toneOf (1, 1), 0.2f);
        const auto low = shot (middle);
        tone (channels.toneOf (1, 1), 0.8f);
        const auto high = shot (middle);
        tone (channels.toneOf (1, 0), 0.9f);
        tone (channels.toneOf (1, 2), 0.1f);
        const auto others = shot (middle);
        tone (channels.toneOf (1, 1), 0.2f);
        const auto back = shot (middle);
        check (middle != nullptr && painted (low) && ! alike (low, high) && alike (high, others) && alike (low, back),
               "...and the middle reading reads MID: it moves with MID, stands while BASS and TREBLE move, and comes back with it");
    }

    if (lever != nullptr)
        lever->setPlace (3, false, true);       // DRIVE
    settle();
    check (channels.active() == 2 && knobIn (0) != nullptr && knobIn (1) != nullptr && knobIn (2) != nullptr && frozen() == nullptr,
           "DRIVE's three likewise, and no frozen knob");

    face.reset();
    amp.reset();
    std::printf ("%s\n", failed == 0 ? "PASSED" : "FAILED");
    return failed == 0 ? 0 : 1;
}
