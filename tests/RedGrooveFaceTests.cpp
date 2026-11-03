// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Gate for REDGROOVEBASS'S FACE (src/redgroove/RedGrooveEditor.h), its window made and never shown, run as the walker
// out of the build's factory folder:
//
//   - the face coming up, and coming up again, leaves BASS, MIDDLE and TREBLE where the player put them;
//   - COMP's knob is its threshold turned round: at the top of its travel -48 dB, at the bottom 0, a double click -24;
//   - MIDDLE's frequency lies across on a logarithmic travel, 200 Hz at its foot, 3 kHz at its head, opening at 1 kHz;
//   - BLEND, DI/CAB and the three effects' faders turn their parameters;
//   - the channel lever: a host's CHANNEL moves it and the sound, the number keys and the lever itself move both;
//   - the cuts' switches stand where their parameters do and throw them;
//   - the cabinet's screen names the cabinet that plays, and OFF while it is out.
//
// Each control is found by the name the face gives it. main() returns 0 when every check held.

#include "GateMain.h"
#include "PluginProcessor.h"
#include "Parameters.h"
#include "channels/Engine.h"
#include "face/Controls.h"
#include "redgroove/RedGrooveControls.h"

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
}

int ORBITAMP_GATE_MAIN()
{
    using namespace orbitamp;
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

    // MIDDLE'S FREQUENCY OPENS AT 1 kHz on the walker (the owner, 02.11): a fresh one stands there.
    check (std::abs (plain (params::eqBellHz (0, 0)) - 1000.0f) < 0.5f, "a fresh walker's MIDDLE frequency stands at 1 kHz");

    // THE TONE, LEFT AS IT STANDS when the face comes up — twice.
    setPlain (params::eqLoDb (0), 6.0f);
    setPlain (params::eqBellDb (0, 0), -3.0f);
    setPlain (params::eqHiDb (0), 2.5f);
    const auto toneStands = [&plain]
    {
        return std::abs (plain (params::eqLoDb (0)) - 6.0f) < 0.05f && std::abs (plain (params::eqBellDb (0, 0)) + 3.0f) < 0.05f
            && std::abs (plain (params::eqHiDb (0)) - 2.5f) < 0.05f;
    };
    std::unique_ptr<juce::AudioProcessorEditor> face (amp->createEditor());
    check (face != nullptr && named (*face, "comp") != nullptr, "the walker's face comes up, its own controls on it");
    if (face == nullptr || named (*face, "comp") == nullptr)
    {
        std::printf ("FAILED\n");
        return 1;
    }
    check (toneStands(), "the face coming up leaves BASS +6, MIDDLE -3 and TREBLE +2.5 dB where they stood");
    face.reset();                   // shut first, as a host shuts one window before it opens the next
    face.reset (amp->createEditor());
    check (toneStands(), "...and coming up again, too");

    // COMP: the threshold turned round — clockwise the deeper.
    auto* comp = dynamic_cast<face::ArtKnob*> (named (*face, "comp"));
    setPlain (params::compThreshold, -36.0f);
    check (comp != nullptr && std::abs (comp->getValue() - 0.75f) < 1.0e-3f, "COMP's knob stands three quarters round at -36 dB");
    if (comp != nullptr)
        comp->setValue (0.25f, true);
    check (std::abs (plain (params::compThreshold) + 12.0f) < 0.05f, "...a quarter round sets -12 dB");
    if (comp != nullptr)
        comp->setValue (1.0f, true);
    check (std::abs (plain (params::compThreshold) + 48.0f) < 0.05f, "...all the way round -48 dB");
    check (comp != nullptr && std::abs (comp->defaultValue - 0.5f) < 1.0e-3f, "...and a double click puts it at -24 dB, half way");

    // MIDDLE'S FREQUENCY, across: 200 Hz to 3 kHz on a logarithmic travel.
    auto* mid = dynamic_cast<face::ColumnFader*> (named (*face, "midFreq"));
    if (mid != nullptr)
        mid->setValue (0.5f, true);
    const float half = params::bassMidMinHz * std::sqrt (params::bassMidMaxHz / params::bassMidMinHz);
    check (mid != nullptr && std::abs (plain (params::eqBellHz (0, 0)) - half) < 1.0f,
           "MIDDLE's frequency half way across is " + juce::String (half, 1) + " Hz, the span's middle on a logarithmic travel");
    setPlain (params::eqBellHz (0, 0), 3000.0f);
    const bool atHead = mid != nullptr && std::abs (mid->getValue() - 1.0f) < 1.0e-3f;
    setPlain (params::eqBellHz (0, 0), 100.0f);
    check (atHead && mid != nullptr && mid->getValue() < 1.0e-3f, "...3 kHz at its head, and a frequency under 200 Hz at its foot");
    if (mid != nullptr)
        mid->setValue (mid->defaultValue, true);    // what a double click on it does (face::ColumnFader)
    check (mid != nullptr && std::abs (plain (params::eqBellHz (0, 0)) - 1000.0f) < 1.0f, "...and a double click puts it back at 1 kHz");

    // BLEND, DI/CAB and the effects turn their parameters.
    const auto turns = [&face, &plain] (const char* name, float travel, const char* id, float want)
    {
        bool ok = false;
        if (auto* knob = dynamic_cast<face::ArtKnob*> (named (*face, name)))
        {
            knob->setValue (travel, true);
            ok = true;
        }
        else if (auto* fader = dynamic_cast<face::ColumnFader*> (named (*face, name)))
        {
            fader->setValue (travel, true);
            ok = true;
        }
        return ok && std::abs (plain (id) - want) < 1.0e-2f;
    };
    check (turns ("blend", 0.62f, params::ampBlend, 0.62f) && turns ("diCab", 0.4f, params::cabMix, 40.0f),
           "BLEND turns the capture's share and CAB / DI the cabinet's");
    check (turns ("octave", 0.3f, params::octaveAmount, 0.3f) && turns ("envelope", 0.5f, params::filterAmount, 0.5f)
               && turns ("chorus", 0.7f, params::chorusAmount, 0.7f),
           "OCTAVE, ENVELOPE and CHORUS each turn their amount");

    // THE LEVER, moved by a host and by a hand.
    auto& engine = channels::Engine::of (*amp);
    auto& channels = engine.channels;
    auto* channel = amp->apvts.getParameter (engine.channelId());
    auto* device  = amp->apvts.getParameter (params::preampDevice);
    auto* lever   = dynamic_cast<face::DetentLever*> (named (*face, "lever"));
    const auto asked   = [channel] { return juce::roundToInt (channel->convertFrom0to1 (channel->getValue())); };
    const auto playing = [device]  { return juce::roundToInt (device->convertFrom0to1 (device->getValue())); };
    check (lever != nullptr && lever->numPlaces() == 2 && lever->getPlace() == channels.active(),
           "the lever has the walker's two gears and stands on the one that plays");
    if (lever != nullptr)
    {
        channel->setValueNotifyingHost (channel->convertTo0to1 (1.0f));
        engine.pump();
        check (lever->getPlace() == 1 && channels.active() == 1 && playing() == channels.packFor (1),
               "a host's CHANNEL on SVT moves the lever there, and the sound with it");
        face->keyPressed (juce::KeyPress ('1'));
        check (lever->getPlace() == 0 && channels.active() == 0 && asked() == 0 && playing() == channels.packFor (0),
               "the 1 key pulls B15: the lever, the sound and CHANNEL");
        lever->setPlace (1, false, true);       // the hand's
        check (channels.active() == 1 && asked() == 1, "the lever pulled to SVT plays SVT, and CHANNEL says so");
    }

    // THE CUTS' SWITCHES.
    auto* lowCut  = dynamic_cast<redgroove::SlideSwitch*> (named (*face, "lowCut"));
    auto* highCut = dynamic_cast<redgroove::SlideSwitch*> (named (*face, "highCut"));
    setPlain (params::bassHpf, 1.0f);
    setPlain (params::bassLpf, 0.0f);
    check (lowCut != nullptr && highCut != nullptr && lowCut->position() == 1 && highCut->position() == 0,
           "the switches stand where the cuts do: 4 STR, FINGERS");
    if (lowCut != nullptr && highCut != nullptr)
    {
        lowCut->setPosition (0, true);
        highCut->setPosition (1, true);
    }
    check (juce::roundToInt (plain (params::bassHpf)) == (int) params::BassHpf::five && juce::roundToInt (plain (params::bassLpf)) == (int) params::BassLpf::slap,
           "...and thrown, throw them: 5 STR, SLAP");

    // THE CABINET'S SCREEN.
    auto* screen = dynamic_cast<redgroove::CabinetScreen*> (named (*face, "cabinet"));
    const int cab = channels.cabinet();
    if (screen != nullptr)
        screen->refresh();
    check (screen != nullptr && cab >= 0 && screen->says() == channels::Channels::cabinetName (channels.cabinets()[cab]).toUpperCase(),
           "the cabinet's screen names the cabinet that plays" + (screen != nullptr ? ": " + screen->says() : juce::String()));
    channels.setCabinetOn (false);
    if (screen != nullptr)
        screen->refresh();
    check (screen != nullptr && screen->says() == "OFF", "...and says OFF while the cabinet is out of the sound");
    channels.setCabinetOn (true);

    face.reset();
    amp.reset();
    std::printf ("%s\n", failed == 0 ? "PASSED" : "FAILED");
    return failed == 0 ? 0 : 1;
}
