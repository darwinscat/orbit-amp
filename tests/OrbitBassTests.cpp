// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Gate for ORBITBASS'S CHAIN (src/Product.h, Chain::bass), headless, run as OrbitBass:
//
//   where a fresh one stands — the gate off, COMP on at 0, BLEND 0 at 150 Hz, DI/CAB 30 % DI, its own HPF at
//   5 (22 Hz) and LPF off, the cabinet's own cuts off, no delay and no reverb, mono, on its one temporary pack;
//   its strip — GATE, COMP, AMP, CAB, LIMIT, in that order — while OrbitAmp's keeps the delay and the reverb
//   and has no COMP, nor any of the bass's parameters;
//   its sound — the delay and the reverb never in it, whatever their switches say; one signal to both
//   outputs, whatever the mode says; COMP at 0 no compressor, bit for bit; BLEND 0 and its own links
//   standing aside, OrbitAmp's links in OrbitAmp's order less those two — an OrbitAmp set as it is, on its
//   pack, with its delay and reverb off, plays what it plays, as at step 1; DI/CAB at 100 % no IR at all and at
//   0 % the cabinet alone; its cuts by their positions;
//   COMP's law — its ends, its squeeze where the curve says, a detector that does not hear the lowest notes;
//   its tuner — down to 25 Hz, while every guitar amp's stays at 40.

#include "GateMain.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Parameters.h"
#include "core/BassComp.h"
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
        check (juce::roundToInt (value (a, params::bassHpf)) == (int) params::BassHpf::five
                   && juce::approximatelyEqual (value (a, params::bassHpfHz5), 22.0f)
                   && juce::approximatelyEqual (value (a, params::bassHpfHz4), 32.0f),
               "its HPF at 5, 22 Hz (4 at 32 Hz)");
        check (a.apvts.getParameter (params::bassHpfHz5)->getNormalisableRange().start <= 20.0f,
               "...in a range that reaches 20 Hz");
        check (juce::roundToInt (value (a, params::bassLpf)) == (int) params::BassLpf::off
                   && juce::approximatelyEqual (value (a, params::bassLpfHzFingers), 4500.0f)
                   && juce::approximatelyEqual (value (a, params::bassLpfHzSlap), 9500.0f),
               "its LPF off (FINGERS at 4.5 kHz, SLAP at 9.5 kHz)");
        check (value (a, params::cabHpfOn) < 0.5f && value (a, params::cabLpfOn) < 0.5f,
               "the cabinet's own cuts off: its IR plays as it was shot");
        check (value (a, params::compOn) > 0.5f && juce::approximatelyEqual (value (a, params::compAmount), 0.0f)
                   && juce::roundToInt (value (a, params::compAttack)) == 1 && juce::roundToInt (value (a, params::compRelease)) == 0
                   && juce::roundToInt (value (a, params::compCharacter)) == 0,
               "COMP on at 0, MED attack, AUTO release, SMOOTH");
        check (juce::approximatelyEqual (value (a, params::ampBlend), 0.0f) && juce::approximatelyEqual (value (a, params::ampBlendHz), 150.0f)
                   && value (a, params::ampBlendInvert) < 0.5f,
               "BLEND 0, its crossover at 150 Hz, not inverted");
        check (juce::approximatelyEqual (value (a, params::diMix), 30.0f), "DI/CAB 30 % DI");
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
    check (arrows (AmpEditor::stripRows (*amp)) == "GATE > COMP > AMP > CAB IR > LIMIT",
           "OrbitBass's strip: " + arrows (AmpEditor::stripRows (*amp)));
    check (arrows (AmpEditor::stripRows (*guitar)) == "GATE > AMP > DELAY > REVERB > CAB IR > LIMIT",
           "OrbitAmp's, as it was: " + arrows (AmpEditor::stripRows (*guitar)));
    {
        bool none = ! guitar->chainHolds (params::rowComp);
        for (const char* id : { params::compOn, params::compAmount, params::ampBlend, params::diMix, params::bassHpf, params::bassLpf })
            none = none && guitar->apvts.getParameter (id) == nullptr;
        check (none, "OrbitAmp has no COMP and none of the bass's parameters");
    }

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

    // COMP at 0 is no compressor: the link in STANDBY plays the same, bit for bit; and turned up, it is heard.
    {
        auto off = make (bass);
        set (*off, params::compOn, 0.0f);
        const auto heard = play (*off, blocks, false);
        check (worstApart (heard.l, plain.l) == 0.0f, "COMP at 0 plays as COMP standing by, bit for bit");

        auto up = make (bass);
        set (*up, params::compAmount, 1.0f);
        const auto squeezed = play (*up, blocks, false);
        check (worstApart (squeezed.l, plain.l) > 1.0e-3f, "...and COMP turned up is heard");
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
                        if (auto* there = same->apvts.getParameter (ranged->paramID))   // the bass's own, it has none of
                        {
                            there->setValueNotifyingHost (ranged->getValue());
                            same->pumpDeviceWork();
                        }

            juce::StringArray apart;
            for (auto* p : from.getParameters())
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p); ranged != nullptr && ranged->paramID != params::preampDevice)
                    if (auto* there = same->apvts.getParameter (ranged->paramID);
                        there != nullptr && ! juce::approximatelyEqual (there->getValue(), ranged->getValue()))
                        apart.add (ranged->paramID);
            check (apart.isEmpty() && value (*same, params::delayOn) < 0.5f && value (*same, params::reverbOn) < 0.5f,
                   what + ": an OrbitAmp set as OrbitBass is, its delay and reverb off"
                       + (apart.isEmpty() ? juce::String() : " — apart: " + apart.joinIntoString (", ")));

            same->prepareToPlay (sampleRate, blockSize);
            same->pumpDeviceWork();
            return worstApart (play (*same, blocks, false).l, heard.l);
        };

        // OrbitBass as at step 1: COMP at 0, BLEND 0, its HPF off, the DI's share none.
        const auto asBefore = [&]
        {
            auto b = make (bass);
            set (*b, params::bassHpf, (float) params::BassHpf::off);
            set (*b, params::diMix, 0.0f);
            return b;
        };

        // The cabinets out: OrbitAmp's links less the delay and the reverb, bit for bit — BLEND 0 is the
        // amp without it, COMP 0 no compressor, the cuts off a wire.
        {
            auto dry = asBefore();
            set (*dry, params::cabOn, 0.0f);
            const auto heard = play (*dry, blocks, false);
            const float worst = twin (*dry, heard, "the cabinets out");
            check (worst == 0.0f, "...plays what OrbitBass plays at BLEND 0, bit for bit (worst apart " + juce::String (worst, 9) + ")");

            auto blended = asBefore();
            set (*blended, params::cabOn, 0.0f);
            set (*blended, params::ampBlend, 1.0f);
            check (worstApart (play (*blended, blocks, false).l, heard.l) > 1.0e-3f, "...and BLEND turned up is heard");
        }

        // The cabinets in, on the same IR's bytes, at DI/CAB 0 %: the cabinet alone, bit for bit.
        {
            auto cabOnly = asBefore();
            const auto heard = play (*cabOnly, blocks, false);
            const float worst = twin (*cabOnly, heard, "the cabinets in, DI/CAB 0 %");
            check (worst == 0.0f,
                   "...plays what OrbitBass plays, the cabinet alone (worst apart " + juce::String (worst, 9) + " of peak " + juce::String (peak (heard.l), 4) + ")");
        }
    }

    // DI/CAB at 100 %: no IR at all — what the cabinet was handed, bit for bit as with the cabinet out.
    {
        auto allDi = make (bass);
        set (*allDi, params::diMix, 100.0f);
        auto noCab = make (bass);
        set (*noCab, params::cabOn, 0.0f);
        const auto a = play (*allDi, blocks, false), b = play (*noCab, blocks, false);
        check (worstApart (a.l, b.l) == 0.0f, "DI/CAB at 100 % plays no IR, as the cabinet out, bit for bit");
        check (worstApart (a.l, plain.l) > 1.0e-3f, "...and 30 % is not that");
    }

    // ITS CUTS BY THEIR POSITIONS — tones the amp passes, the cabinet out: the HPF takes more of a 26 Hz tone at 4
    // than at 5 and none off; the LPF more of a 6 kHz tone at FINGERS than at SLAP and none off.
    {
        const auto level = [&] (float hzTone, const char* id, int position)
        {
            auto t = make (bass);
            set (*t, params::cabOn, 0.0f);
            set (*t, params::bassHpf, (float) params::BassHpf::off);
            set (*t, id, (float) position);
            juce::AudioBuffer<float> buf (2, blockSize);
            juce::MidiBuffer midi;
            double sum = 0.0;
            long long at = 0, counted = 0;
            for (int b = 0; b < 200; ++b)
            {
                for (int i = 0; i < blockSize; ++i, ++at)
                {
                    const auto v = (float) (0.1 * std::sin (juce::MathConstants<double>::twoPi * hzTone * (double) at / sampleRate));
                    buf.setSample (0, i, v);
                    buf.setSample (1, i, v);
                }
                t->processBlock (buf, midi);
                t->pumpDeviceWork();
                if (b >= 100)
                    for (int i = 0; i < blockSize; ++i, ++counted)
                        sum += (double) buf.getSample (0, i) * buf.getSample (0, i);
            }
            return 10.0 * std::log10 (sum / (double) counted + 1.0e-30);
        };
        const double hOff = level (26.0f, params::bassHpf, (int) params::BassHpf::off);
        const double h5   = level (26.0f, params::bassHpf, (int) params::BassHpf::five);
        const double h4   = level (26.0f, params::bassHpf, (int) params::BassHpf::four);
        check (hOff > h5 + 0.5 && h5 > h4 + 2.0,
               "HPF on a 26 Hz tone: off " + juce::String (hOff, 1) + ", 5 " + juce::String (h5, 1) + ", 4 " + juce::String (h4, 1) + " dB");
        const double lOff = level (6000.0f, params::bassLpf, (int) params::BassLpf::off);
        const double lS   = level (6000.0f, params::bassLpf, (int) params::BassLpf::slap);
        const double lF   = level (6000.0f, params::bassLpf, (int) params::BassLpf::fingers);
        check (lOff > lS + 0.2 && lS > lF + 3.0,
               "LPF on a 6 kHz tone: off " + juce::String (lOff, 1) + ", SLAP " + juce::String (lS, 1) + ", FINGERS " + juce::String (lF, 1) + " dB");
    }

    // ---- COMP'S LAW -------------------------------------------------------------------------------------
    {
        using core::BassComp;
        const auto bottom = BassComp::lawAt (0.0), top = BassComp::lawAt (1.0);
        check (bottom.thresholdDb == -12.0 && bottom.ratio == 2.0 && bottom.uncompressed == 0.0
                   && top.thresholdDb == -36.0 && top.ratio == 6.0 && top.uncompressed == 0.25,
               "COMP's law: from -12 dB at 2:1 to -36 dB at 6:1, a quarter uncompressed at the top");

        // A steady tone 30 dB over the top's threshold (RMS -6 dBFS): squeezed as the curve says, (L - T)(1/R - 1).
        const auto squeeze = [] (double hzTone, double amount)
        {
            BassComp c;
            (void) c.prepare (sampleRate, blockSize);
            std::vector<float> x ((size_t) blockSize);
            long long at = 0;
            for (int b = 0; b < 200; ++b)
            {
                for (int i = 0; i < blockSize; ++i, ++at)
                    x[(size_t) i] = (float) (std::sqrt (2.0) * 0.5012 * std::sin (juce::MathConstants<double>::twoPi * hzTone * (double) at / sampleRate));
                c.process (x.data(), blockSize, amount, BassComp::Attack::med, BassComp::Release::automatic, BassComp::Character::smooth);
            }
            return (double) c.grDb.load();
        };
        // At 0, nothing at all: a block of noise comes back as it went in, bit for bit.
        {
            BassComp c;
            (void) c.prepare (sampleRate, blockSize);
            juce::Random random (31);
            bool same = true;
            for (int b = 0; b < 20; ++b)
            {
                std::vector<float> x ((size_t) blockSize);
                for (auto& v : x)
                    v = random.nextFloat() * 1.6f - 0.8f;
                auto y = x;
                c.process (y.data(), blockSize, 0.0, BassComp::Attack::med, BassComp::Release::automatic, BassComp::Character::smooth);
                same = same && y == x;
            }
            check (same, "...at 0, no compressor: loud noise passes bit for bit");
        }

        const double at120 = squeeze (120.0, 1.0), want = (-6.0 + 36.0) * (1.0 / 6.0 - 1.0);
        check (std::abs (at120 - want) < 2.0, "...at the top, 120 Hz at -6 dBFS squeezed " + juce::String (at120, 1)
                                                  + " dB, the curve says " + juce::String (want, 1));
        const double at25 = squeeze (25.0, 1.0);
        check (at25 > at120 + 8.0, "...a 25 Hz tone as loud squeezed " + juce::String (at25, 1) + " dB: the detector does not hear the lowest notes");
        const double low = squeeze (120.0, 0.0001);
        check (low > -12.0 && low < 0.0, "...at the bottom, gently (" + juce::String (low, 1) + " dB)");
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
