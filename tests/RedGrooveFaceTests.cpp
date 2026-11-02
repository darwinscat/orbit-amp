// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Gate for REDGROOVEBASS'S FACE (src/redgroove/RedGrooveEditor.h), its window made and never shown, run as the walker
// out of the build's factory folder:
//
//   - the face coming up, and coming up again, leaves BASS, MIDDLE and TREBLE where the player put them;
//   - COMP's knob is its threshold turned round: at the top of its travel -48 dB, at the bottom 0, a double click -24;
//   - MIDDLE's frequency lies across on a logarithmic travel, 200 Hz at its foot, 3 kHz at its head;
//   - BLEND, DI/CAB and the three effects' faders turn their parameters.
//
// Each control is found by the name the face gives it. main() returns 0 when every check held.

#include "GateMain.h"
#include "PluginProcessor.h"
#include "Parameters.h"
#include "channels/Engine.h"
#include "face/Controls.h"

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
    check (turns ("blend", 0.62f, params::ampBlend, 0.62f) && turns ("diCab", 0.4f, params::diMix, 40.0f),
           "BLEND turns its share and DI/CAB the DI's");
    check (turns ("octave", 0.3f, params::octaveAmount, 0.3f) && turns ("envelope", 0.5f, params::filterAmount, 0.5f)
               && turns ("chorus", 0.7f, params::chorusAmount, 0.7f),
           "OCTAVE, ENVELOPE and CHORUS each turn their amount");

    face.reset();
    amp.reset();
    std::printf ("%s\n", failed == 0 ? "PASSED" : "FAILED");
    return failed == 0 ? 0 : 1;
}
