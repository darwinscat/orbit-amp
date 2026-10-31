// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Gate for ORBITBASS'S CHAIN (src/Product.h, Chain::bass), headless, run as OrbitBass:
//
//   where a fresh one stands — the gate off, the cabinet's HPF on at 25 Hz in a range that reaches 20, the
//   LPF off, no delay and no reverb, mono, on its one temporary pack;
//   its strip — GATE, AMP, CAB, LIMIT, in that order — while OrbitAmp's keeps the delay and the reverb;
//   its sound — the delay and the reverb never in it, whatever their switches say; one signal to both
//   outputs, whatever the mode says; and OrbitAmp's links in OrbitAmp's order less those two: an OrbitAmp
//   set as it is, on its pack, with its delay and reverb off, plays what it plays;
//   its tuner — down to 25 Hz, while every guitar amp's stays at 40.

#include "GateMain.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Parameters.h"
#include "device/DeviceLibrary.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

namespace
{
    using namespace orbitamp;

    constexpr double sampleRate = 48000.0;
    constexpr int    blockSize  = 512;

    /** A product as itself, less its first start: a gate does not gather a player's files. */
    const Product& quiet (const Product& p)
    {
        static std::map<std::string, Product> made;
        auto [it, fresh] = made.try_emplace (p.name, p);
        it->second.firstStart = nullptr;
        return it->second;
    }

    std::unique_ptr<AmpProcessor> make (const Product& p)
    {
        auto amp = std::make_unique<AmpProcessor> (quiet (p));
        amp->inlineLoads = true;            // no message loop here: a model lands inside the pump
        amp->prepareToPlay (sampleRate, blockSize);
        amp->pumpDeviceWork();
        return amp;
    }

    float value (AmpProcessor& amp, const char* id)
    {
        auto* p = amp.apvts.getParameter (id);
        return p->convertFrom0to1 (p->getValue());
    }

    float declared (AmpProcessor& amp, const char* id)
    {
        auto* p = amp.apvts.getParameter (id);
        return p->convertFrom0to1 (p->getDefaultValue());
    }

    void set (AmpProcessor& amp, const juce::String& id, float v)
    {
        auto* p = amp.apvts.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (v));
        amp.pumpDeviceWork();
    }

    /** A bass's phrase, a different one on each side when `wide`: notes on E1 and A1 with their
        harmonics, each ringing a quarter second, then silence — where a room or an echo would still be
        heard. Both channels out, `blocks` blocks of it. */
    struct Out { std::vector<float> l, r; };

    Out play (AmpProcessor& amp, int blocks, bool wide)
    {
        Out out;
        juce::AudioBuffer<float> buf (2, blockSize);
        juce::MidiBuffer midi;
        const double tau = juce::MathConstants<double>::twoPi;
        long long at = 0;

        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < blockSize; ++i, ++at)
            {
                const double t = (double) at / sampleRate;
                const double note = std::fmod (t, 0.5);
                const double f = std::fmod (t, 1.0) < 0.5 ? 41.2 : 55.0;
                const double env = t < 1.5 && note < 0.25 ? std::exp (-6.0 * note) : 0.0;
                const auto s = (float) (0.25 * env * (std::sin (tau * f * t) + 0.5 * std::sin (tau * 2.0 * f * t)
                                                      + 0.25 * std::sin (tau * 3.0 * f * t)));
                buf.setSample (0, i, s);
                buf.setSample (1, i, wide ? (float) (0.25 * env * std::sin (tau * 3.7 * f * t)) : s);
            }
            amp.processBlock (buf, midi);
            amp.pumpDeviceWork();
            out.l.insert (out.l.end(), buf.getReadPointer (0), buf.getReadPointer (0) + blockSize);
            out.r.insert (out.r.end(), buf.getReadPointer (1), buf.getReadPointer (1) + blockSize);
        }
        return out;
    }

    float worstApart (const std::vector<float>& a, const std::vector<float>& b)
    {
        float worst = 0.0f;
        for (size_t i = 0; i < a.size() && i < b.size(); ++i)
            worst = std::max (worst, std::abs (a[i] - b[i]));
        return worst;
    }

    float peak (const std::vector<float>& a)
    {
        float p = 0.0f;
        for (float v : a)
            p = std::max (p, std::abs (v));
        return p;
    }

    juce::String arrows (const std::vector<LayoutStrip::Row>& rows)
    {
        juce::StringArray said;
        for (const auto& row : rows)
            if (row.present)
                said.add (row.name);
        return said.joinIntoString (" > ");
    }
}

int ORBITAMP_GATE_MAIN()
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    int failed = 0;
    const auto check = [&failed] (bool ok, const juce::String& what)
    {
        std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
        if (! ok)
            ++failed;
    };

    const Product& bass = ORBITAMP_GATE_PRODUCT();
    check (juce::String (bass.name) == "OrbitBass" && bass.chain == Chain::bass, "OrbitBass plays the bass's chain");

    // ---- WHERE A FRESH ONE STANDS ------------------------------------------------------------------
    auto amp = make (bass);
    {
        auto& a = *amp;
        check (declared (a, params::gateOn) < 0.5f && value (a, params::gateOn) < 0.5f, "the gate is off");
        check (declared (a, params::cabHpfOn) > 0.5f && value (a, params::cabHpfOn) > 0.5f
                   && juce::approximatelyEqual (declared (a, params::cabHpfHz), 25.0f)
                   && juce::approximatelyEqual (value (a, params::cabHpfHz), 25.0f),
               "the HPF is on at 25 Hz (" + juce::String (value (a, params::cabHpfHz)) + ")");
        check (a.apvts.getParameter (params::cabHpfHz)->getNormalisableRange().start <= 20.0f,
               "...in a range that reaches 20 Hz");
        check (declared (a, params::cabLpfOn) < 0.5f && value (a, params::cabLpfOn) < 0.5f, "the LPF is off");
        check (declared (a, params::delayOn) < 0.5f && declared (a, params::reverbOn) < 0.5f
                   && value (a, params::delayOn) < 0.5f && value (a, params::reverbOn) < 0.5f,
               "no delay and no reverb");
        check (juce::roundToInt (declared (a, params::stereoMode)) == (int) params::StereoMode::mono
                   && juce::roundToInt (value (a, params::stereoMode)) == (int) params::StereoMode::mono,
               "mono");
        check (value (a, params::preampOn) > 0.5f && value (a, params::cabOn) > 0.5f && value (a, params::limiterOn) > 0.5f,
               "the amp, the cabinet and the limiter on");

        const auto packs = device::DeviceLibrary::scan (bass.name, device::DeviceLibrary::ampSlot);
        check (packs.size() == 1 && packs.getReference (0).rigId == "revolt-clean-poweramp-bf62"
                   && packs.getReference (0).bundled && juce::roundToInt (value (a, params::preampDevice)) == 0,
               "it plays its one temporary pack, the ReVolt Clean (" + juce::String (packs.size()) + " in its list)");
        check (a.getTailLengthSeconds() < 8.0, "its tail is the cabinet's, no echo's or room's ("
                                                   + juce::String (a.getTailLengthSeconds(), 2) + " s)");
    }

    // ---- ITS STRIP -----------------------------------------------------------------------------------
    auto guitar = make (products::orbitAmp());
    check (arrows (AmpEditor::stripRows (*amp)) == "GATE > AMP > CAB IR > LIMIT",
           "OrbitBass's strip: " + arrows (AmpEditor::stripRows (*amp)));
    check (arrows (AmpEditor::stripRows (*guitar)) == "GATE > AMP > DELAY > REVERB > CAB IR > LIMIT",
           "OrbitAmp's, as it was: " + arrows (AmpEditor::stripRows (*guitar)));

    // ---- ITS SOUND -----------------------------------------------------------------------------------
    constexpr int blocks = 250;   // 2.7 s: the phrase, and over a second of what rings after it
    const auto plain = play (*amp, blocks, false);
    check (peak (plain.l) > 1.0e-3f, "it sounds (peak " + juce::String (peak (plain.l), 4) + ")");

    // The delay and the reverb switched on, all wet: nothing changes, bit for bit.
    {
        auto forced = make (bass);
        set (*forced, params::delayOn, 1.0f);
        set (*forced, params::reverbOn, 1.0f);
        set (*forced, params::delayMix, 100.0f);
        set (*forced, params::reverbMix, 100.0f);
        const auto heard = play (*forced, blocks, false);
        check (worstApart (heard.l, plain.l) == 0.0f && worstApart (heard.r, plain.r) == 0.0f,
               "the delay and the reverb switched on change nothing, bit for bit (worst "
                   + juce::String (worstApart (heard.l, plain.l), 6) + ")");
    }

    // One signal, both outputs: two different inputs, the mode at STEREO — OrbitBass leaves the left on both;
    // OrbitAmp, at the same mode, two (the gate hears the difference).
    {
        auto wide = make (bass);
        set (*wide, params::stereoMode, (float) params::StereoMode::stereo);
        const auto heard = play (*wide, blocks, true);
        check (worstApart (heard.l, heard.r) == 0.0f && worstApart (heard.l, plain.l) == 0.0f,
               "a stereo input at STEREO plays the left alone, to both outputs, as at MONO");

        auto twin = make (products::orbitAmp());
        set (*twin, params::stereoMode, (float) params::StereoMode::stereo);
        const auto apart = play (*twin, blocks, true);
        check (worstApart (apart.l, apart.r) > 1.0e-3f, "...where OrbitAmp at STEREO plays two");
    }

    // OrbitAmp's links in OrbitAmp's order, less the delay and the reverb: an OrbitAmp on the same pack,
    // every other parameter as OrbitBass's, its delay and reverb off, plays what OrbitBass plays.
    {
        int place = -1;
        const auto list = device::DeviceLibrary::scan (products::orbitAmp().name, device::DeviceLibrary::ampSlot);
        for (int i = 0; i < list.size() && place < 0; ++i)
            if (list.getReference (i).rigId == "revolt-clean-poweramp-bf62")
                place = i;
        check (place >= 0, "OrbitAmp plays the ReVolt Clean too (place " + juce::String (place) + ")");

        // Each product reads its shelf into memory of its own: the same IR is the same bytes, not the same address.
        const int  ir    = juce::roundToInt (value (*amp, params::cabIr));
        const auto& his  = AmpProcessor::cabIrBytes (quiet (products::orbitAmp()), ir);
        const auto& ours = AmpProcessor::cabIrBytes (bass, ir);
        check (his.data != nullptr && ours.data != nullptr && his.size == ours.size && his.size > 0
                   && std::memcmp (his.data, ours.data, (size_t) ours.size) == 0,
               "...and the cabinet OrbitBass opens on at the same number (" + juce::String (ours.size) + " bytes)");

        // An OrbitAmp on the same pack, every other parameter as `from`'s, prepared again once it stands so —
        // every link starting from silence, as OrbitBass's did; what it plays, against what `heard` did.
        const auto twin = [&] (AmpProcessor& from, const Out& heard, const juce::String& what)
        {
            auto same = make (products::orbitAmp());
            set (*same, params::preampDevice, (float) place);
            for (int pass = 0; pass < 2; ++pass)   // a device that lands may move its own; the second pass holds
                for (auto* p : from.getParameters())
                    if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p);
                        ranged != nullptr && ranged->paramID != params::preampDevice)
                        if (auto* there = same->apvts.getParameter (ranged->paramID))
                        {
                            there->setValueNotifyingHost (ranged->getValue());
                            same->pumpDeviceWork();
                        }

            juce::StringArray apart;
            for (auto* p : from.getParameters())
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p); ranged != nullptr && ranged->paramID != params::preampDevice)
                    if (auto* there = same->apvts.getParameter (ranged->paramID);
                        there == nullptr || ! juce::approximatelyEqual (there->getValue(), ranged->getValue()))
                        apart.add (ranged->paramID);
            check (apart.isEmpty() && value (*same, params::delayOn) < 0.5f && value (*same, params::reverbOn) < 0.5f,
                   what + ": an OrbitAmp set as OrbitBass is, its delay and reverb off"
                       + (apart.isEmpty() ? juce::String() : " — apart: " + apart.joinIntoString (", ")));

            same->prepareToPlay (sampleRate, blockSize);
            same->pumpDeviceWork();
            return worstApart (play (*same, blocks, false).l, heard.l);
        };

        // The cabinets out: OrbitAmp's links less the delay and the reverb, bit for bit.
        {
            auto dry = make (bass);
            set (*dry, params::cabOn, 0.0f);
            const auto heard = play (*dry, blocks, false);
            const float worst = twin (*dry, heard, "the cabinets out");
            check (worst == 0.0f, "...plays what OrbitBass plays, bit for bit (worst apart " + juce::String (worst, 9) + ")");
        }

        // The cabinets in, on the same IR's bytes: the same, a residue under -90 dB apart — each product's
        // cabinet is its own, read off its own shelf. A link out of its place would be the whole signal.
        {
            const float worst = twin (*amp, plain, "the cabinets in");
            check (worst <= 3.0e-5f * peak (plain.l),
                   "...plays what OrbitBass plays (worst apart " + juce::String (worst, 9) + " of peak " + juce::String (peak (plain.l), 4) + ")");
        }
    }

    // ---- ITS TUNER -----------------------------------------------------------------------------------
    check (juce::approximatelyEqual (bass.tunerLowestHz, core::PitchTracker::bassLowestHz)
               && juce::approximatelyEqual (core::PitchTracker::bassLowestHz, 25.0),
           "OrbitBass's tuner looks down to 25 Hz");
    bool guitars = true;
    for (const auto* g : { &products::orbitAmp(), &products::catpitAmp(), &products::redPawAmp(),
                           &products::frostPawAmp(), &products::amberPawAmp() })
        guitars = guitars && g->chain == Chain::guitar
                  && juce::approximatelyEqual (g->tunerLowestHz, core::PitchTracker::guitarLowestHz);
    check (guitars && juce::approximatelyEqual (core::PitchTracker::guitarLowestHz, 40.0),
           "every guitar amp's to 40, as before, on the guitar's chain");

    std::printf ("%s\n", failed == 0 ? "PASSED" : "FAILED");
    return failed == 0 ? 0 : 1;
}
