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
#include "ui/LoadBreakdown.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <thread>
#include <limits>
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

    /** The largest difference, sample for sample — infinite where the two are not the same length or either
        holds a sample that is not a number: a comparison that can be blind is no comparison. */
    float worstApart (const std::vector<float>& a, const std::vector<float>& b)
    {
        if (a.size() != b.size() || a.empty())
            return std::numeric_limits<float>::infinity();
        float worst = 0.0f;
        for (size_t i = 0; i < a.size(); ++i)
        {
            if (! std::isfinite (a[i]) || ! std::isfinite (b[i]))
                return std::numeric_limits<float>::infinity();
            worst = std::max (worst, std::abs (a[i] - b[i]));
        }
        return worst;
    }

    /** `blocks` blocks of a steady sine at `hz`, peak `db` dBFS (or silence, from block `quietFrom` on), into
        `amp`; before block `at`, `move` runs — a hand, a recall, automation. Channel 0 out. */
    std::vector<float> tone (AmpProcessor& amp, double hz, float db, int blocks, int at = -1,
                             const std::function<void()>& move = {}, int quietFrom = 1 << 30, std::vector<float>* right = nullptr)
    {
        std::vector<float> out;
        juce::AudioBuffer<float> buf (2, blockSize);
        juce::MidiBuffer midi;
        const double a = juce::Decibels::decibelsToGain ((double) db);
        long long n = 0;
        for (int b = 0; b < blocks; ++b)
        {
            if (b == at && move)
                move();
            for (int i = 0; i < blockSize; ++i, ++n)
            {
                const auto v = b >= quietFrom ? 0.0f
                                              : (float) (a * std::sin (juce::MathConstants<double>::twoPi * hz * (double) n / sampleRate));
                buf.setSample (0, i, v);
                buf.setSample (1, i, v);
            }
            amp.processBlock (buf, midi);
            amp.pumpDeviceWork();
            out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + blockSize);
            if (right != nullptr)
                right->insert (right->end(), buf.getReadPointer (1), buf.getReadPointer (1) + blockSize);
        }
        return out;
    }

    /** The level of `v`'s component at `hz`, dB, over its second half — a single DFT bin. */
    double levelAt (const std::vector<float>& v, double hz)
    {
        double c = 0.0, sn = 0.0;
        for (size_t i = v.size() / 2; i < v.size(); ++i)
        {
            const double ph = juce::MathConstants<double>::twoPi * hz * (double) i / sampleRate;
            c += v[i] * std::cos (ph);
            sn += v[i] * std::sin (ph);
        }
        return 10.0 * std::log10 (c * c + sn * sn + 1.0e-30);
    }

    float peakIn (const std::vector<float>& v, size_t from, size_t to)
    {
        float p = 0.0f;
        for (size_t i = from; i < std::min (to, v.size()); ++i)
            p = std::max (p, std::abs (v[i]));
        return p;
    }

    float stepIn (const std::vector<float>& v, size_t from, size_t to)
    {
        float p = 0.0f;
        for (size_t i = std::max<size_t> (from, 1); i < std::min (to, v.size()); ++i)
            p = std::max (p, std::abs (v[i] - v[i - 1]));
        return p;
    }

    /** A switch at block `at` of `v`: the peak in the `ms` after it against the steady peaks a second before and
        after, in dB over the larger; and its largest sample-to-sample step against the steady ones', as a ratio. */
    struct Switch { double burstDb; double stepRatio; };

    Switch judgeSwitch (const std::vector<float>& v, int at, double ms)
    {
        const auto s0 = (size_t) at * blockSize;
        const auto w  = (size_t) (ms * 0.001 * sampleRate);
        const auto sec = (size_t) sampleRate / 2;
        const float steadyPeak = std::max (peakIn (v, s0 - sec, s0), peakIn (v, v.size() - sec, v.size()));
        const float steadyStep = std::max (stepIn (v, s0 - sec, s0), stepIn (v, v.size() - sec, v.size()));
        return { juce::Decibels::gainToDecibels ((double) peakIn (v, s0, s0 + w) / std::max (1.0e-9, (double) steadyPeak)),
                 (double) stepIn (v, s0, s0 + w + 16) / std::max (1.0e-9, (double) steadyStep) };
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
    check (arrows (AmpEditor::stripRows (*amp)) == "GATE > COMP > OCTAVE > FILTER > AMP > CHORUS > CAB IR > LIMIT",
           "OrbitBass's strip: " + arrows (AmpEditor::stripRows (*amp)));
    check (arrows (AmpEditor::stripRows (*guitar)) == "GATE > AMP > DELAY > REVERB > CAB IR > LIMIT",
           "OrbitAmp's, as it was: " + arrows (AmpEditor::stripRows (*guitar)));
    {
        bool none = ! guitar->chainHolds (params::rowComp) && ! guitar->chainHolds (params::rowOctave)
                 && ! guitar->chainHolds (params::rowFilter) && ! guitar->chainHolds (params::rowChorus);
        for (const char* id : { params::compOn, params::compAmount, params::ampBlend, params::diMix, params::bassHpf, params::bassLpf,
                                params::octaveAmount, params::filterAmount, params::chorusAmount })
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

    // COMP standing by with its knob up — still fed, its detector kept warm — plays exactly what COMP at 0 plays:
    // nothing of it is in the sound; and switched in, it is heard.
    {
        auto off = make (bass);
        set (*off, params::compAmount, 0.6f);
        set (*off, params::compOn, 0.0f);
        const auto heard = play (*off, blocks, false);
        check (worstApart (heard.l, plain.l) == 0.0f, "COMP standing by at 0.6 plays as COMP at 0, bit for bit");

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

            // BLEND's three reach the player — by the message thread alone: a pump from another thread (a host's
            // prepare elsewhere) leaves them to the next tick.
            auto blended = asBefore();
            set (*blended, params::cabOn, 0.0f);
            auto* blendP  = blended->apvts.getParameter (params::ampBlend);
            auto* hzP     = blended->apvts.getParameter (params::ampBlendHz);
            auto* invertP = blended->apvts.getParameter (params::ampBlendInvert);
            blendP->setValueNotifyingHost (blendP->convertTo0to1 (0.7f));
            hzP->setValueNotifyingHost (hzP->convertTo0to1 (250.0f));
            invertP->setValueNotifyingHost (1.0f);
            std::thread elsewhere ([&] { blended->pumpDeviceWork(); });
            elsewhere.join();
            check (juce::approximatelyEqual (blended->preamp.crossoverAmountTold(), 0.0f),
                   "BLEND is not told from another thread's pump");
            blended->pumpDeviceWork();
            check (juce::approximatelyEqual (blended->preamp.crossoverAmountTold(), 0.7f)
                       && std::abs (blended->preamp.crossoverHzTold() - 250.0f) < 0.5f && blended->preamp.crossoverInvertTold(),
                   "...the message thread's tells the player all three: 70 %, 250 Hz, inverted");
            const auto hi = play (*blended, blocks, false);
            check (worstApart (hi.l, heard.l) > 1.0e-3f, "...and BLEND turned up is heard");
            set (*blended, params::ampBlendHz, 80.0f);
            const auto lo = play (*blended, blocks, false);
            set (*blended, params::ampBlendInvert, 0.0f);
            const auto straight = play (*blended, blocks, false);
            check (worstApart (lo.l, hi.l) > 1.0e-3f && worstApart (straight.l, lo.l) > 1.0e-3f,
                   "...and its frequency and its polarity are heard too");
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

        // 30 % is 0.7 of the cabinet and 0.3 of the DI, sample for sample.
        auto cabOnly = make (bass);
        set (*cabOnly, params::diMix, 0.0f);
        const auto c = play (*cabOnly, blocks, false);
        float worst = 0.0f;
        for (size_t i = 0; i < c.l.size(); ++i)
        {
            // The cuts after the mix are linear: the mix of the cut signals is the cut mix.
            const float want = 0.7f * c.l[i] + 0.3f * a.l[i];
            worst = std::max (worst, std::abs (want - plain.l[i]));
        }
        check (worst <= 1.0e-5f, "DI/CAB at 30 % plays 0.7 x the cabinet + 0.3 x the DI (worst apart " + juce::String (worst, 8) + ")");
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

        // THE COMPRESSOR AS THE CHAIN RUNS IT, standing alone: the knob, and the strip's STANDBY.
        struct Rig
        {
            BassComp c;
            core::BypassFade link;
            bool first = true;
            void prepare (BassComp::KeyCut cut = {}) { (void) c.prepare (sampleRate, 4096, cut); link.prepare (sampleRate); }
            void run (float* x, int n, double amount, bool on, BassComp::Release r = BassComp::Release::automatic,
                      BassComp::Character ch = BassComp::Character::smooth)
            {
                if (first)   // as a prepare stands it: where the session is, no glide
                {
                    link.snapTo (on && amount > 0.0);
                    c.snapAmount (amount);
                    first = false;
                }
                c.process (x, n, amount, link.advance (n, on && amount > 0.0), BassComp::Attack::med, r, ch);
            }
        };

        // A signal, cut into host blocks of `block`; the knob and the switch as `at` sets them, sample by sample.
        const auto drive = [] (Rig& rig, const std::vector<float>& in, int block,
                               const std::function<std::pair<double, bool> (size_t)>& at,
                               BassComp::Release r = BassComp::Release::automatic,
                               BassComp::Character ch = BassComp::Character::smooth)
        {
            std::vector<float> out = in;
            for (size_t i = 0; i < out.size(); i += (size_t) block)
            {
                const int n = (int) std::min ((size_t) block, out.size() - i);
                const auto [amount, on] = at (i);
                rig.run (out.data() + i, n, amount, on, r, ch);
            }
            return out;
        };
        const auto sine = [] (double hz, double peakDb, double seconds)
        {
            std::vector<float> v ((size_t) (seconds * sampleRate));
            const double a = juce::Decibels::decibelsToGain (peakDb);
            for (size_t i = 0; i < v.size(); ++i)
                v[i] = (float) (a * std::sin (juce::MathConstants<double>::twoPi * hz * (double) i / sampleRate));
            return v;
        };
        // A bass line: plucks of a stiff string, E1 A1 D2 E1, each with its harmonics and its own decay.
        const auto plucks = []
        {
            std::vector<float> v ((size_t) (4.0 * sampleRate), 0.0f);
            const double notes[] = { 41.2, 55.0, 73.4, 41.2, 55.0, 98.0, 41.2, 61.7 };
            for (int k = 0; k < 8; ++k)
            {
                const auto start = (size_t) (k * 0.5 * sampleRate);
                for (size_t i = 0; i < (size_t) (0.5 * sampleRate) && start + i < v.size(); ++i)
                {
                    const double t = (double) i / sampleRate;
                    double y = 0.0;
                    for (int h = 1; h <= 8; ++h)
                        y += std::exp (-t * (2.0 + 1.5 * h)) / h * std::sin (juce::MathConstants<double>::twoPi * notes[k] * h * t);
                    v[start + i] = (float) (0.5 * y * (k % 3 == 1 ? 0.5 : 1.0));
                }
            }
            return v;
        }();
        const auto rmsDb = [] (const std::vector<float>& v, size_t from, size_t to)
        {
            double sum = 0.0;
            for (size_t i = from; i < to && i < v.size(); ++i)
                sum += (double) v[i] * v[i];
            return 10.0 * std::log10 (sum / (double) std::max<size_t> (1, std::min (to, v.size()) - from) + 1.0e-30);
        };
        const size_t sw = (size_t) sampleRate;   // the switch, a second in
        const size_t ms15 = (size_t) (0.015 * sampleRate);

        // Switched in from STANDBY at the top, on a steady 97 Hz at -6 dBFS: no burst of make-up.
        {
            const auto in = sine (97.0, -6.0, 2.0);
            Rig rig;
            rig.prepare();
            const auto out = drive (rig, in, 512, [&] (size_t i) { return std::pair<double, bool> { 1.0, i >= sw }; });
            const double burst = juce::Decibels::gainToDecibels ((double) peakIn (out, sw, sw + ms15) / (double) peakIn (in, 0, in.size()));
            check (burst <= 1.0, "COMP switched in at the top: its first 15 ms peak " + juce::String (burst, 1) + " dB over the input");
        }
        // ...switched out, and recalled 0 -> 0.8: no step, no burst.
        {
            const auto in = sine (97.0, -6.0, 2.0);
            Rig rig;
            rig.prepare();
            const auto out = drive (rig, in, 512, [&] (size_t i) { return std::pair<double, bool> { 1.0, i < sw }; });
            const double steady = std::max (stepIn (out, sw - 4800, sw), stepIn (out, out.size() - 4800, out.size()));
            const double ratio  = stepIn (out, sw, sw + 2 * ms15) / steady;
            check (ratio <= 3.0, "COMP switched out at the top: its largest step x" + juce::String (ratio, 1) + " the steady");

            Rig again;
            again.prepare();
            const auto rec = drive (again, in, 512, [&] (size_t i) { return std::pair<double, bool> { i >= sw ? 0.8 : 0.0, true }; });
            const double burst = juce::Decibels::gainToDecibels ((double) peakIn (rec, sw, sw + 2 * ms15) / (double) peakIn (in, 0, in.size()));
            check (burst <= 1.0, "COMP recalled 0 -> 0.8: its first 30 ms peak " + juce::String (burst, 1) + " dB over the input");
        }
        // The knob's zero is no cliff: 0.1 % plays a quiet note within 0.1 dB of what went in.
        {
            const auto in = sine (97.0, -40.0, 1.0);
            Rig rig;
            rig.prepare();
            const auto out = drive (rig, in, 512, [] (size_t) { return std::pair<double, bool> { 0.001, true }; });
            const double lift = rmsDb (out, in.size() / 2, in.size()) - rmsDb (in, in.size() / 2, in.size());
            check (std::abs (lift) <= 0.1, "COMP at 0.1 % lifts a quiet note " + juce::String (lift, 2) + " dB");
        }
        // Its time is its own: AUTO on a bass line plays the same samples at 32, 512 and 4096 a block.
        {
            std::vector<std::vector<float>> outs;
            for (const int block : { 32, 512, 4096 })
            {
                Rig rig;
                rig.prepare();
                outs.push_back (drive (rig, plucks, block, [] (size_t) { return std::pair<double, bool> { 0.8, true }; }));
            }
            const float a = worstApart (outs[0], outs[1]), b = worstApart (outs[1], outs[2]);
            check (a <= 1.0e-6f && b <= 1.0e-6f, "COMP AUTO, host blocks 32 / 512 / 4096 apart: " + juce::String (a, 9) + ", " + juce::String (b, 9));
        }
        // PUNCHY and SMOOTH play the bass line about as loud.
        for (const double amount : { 0.5, 1.0 })
        {
            Rig s1, p1;
            s1.prepare();
            p1.prepare();
            const auto so = drive (s1, plucks, 512, [amount] (size_t) { return std::pair<double, bool> { amount, true }; },
                                   BassComp::Release::automatic, BassComp::Character::smooth);
            const auto po = drive (p1, plucks, 512, [amount] (size_t) { return std::pair<double, bool> { amount, true }; },
                                   BassComp::Release::automatic, BassComp::Character::punchy);
            const double d = rmsDb (po, 0, po.size()) - rmsDb (so, 0, so.size());
            check (std::abs (d) <= 1.5, "PUNCHY against SMOOTH on the bass line at " + juce::String (amount, 1) + ": " + juce::String (d, 1) + " dB");
        }
        // Per note, a steady tone at -6 dBFS RMS: what each string comes out at, with the key's first high-pass (40 Hz,
        // 24 dB/oct) and its present one (30 Hz, 12 dB/oct) — read by the owner, not judged, but for one thing: the
        // low B (31 Hz) comes out no hotter against the string an octave up than it did.
        {
            double spread[2][2] {};
            std::printf ("      COMP per note, out dB (a -6 dBFS RMS tone):    25 Hz   31 Hz   41 Hz   55 Hz   62 Hz   98 Hz  250 Hz\n");
            for (int version = 0; version < 2; ++version)
                for (int a = 0; a < 2; ++a)
                {
                    const double amount = a == 0 ? 0.5 : 1.0;
                    std::printf ("      key %s, amount %.1f         ", version == 0 ? "40 Hz 24 dB/oct" : "30 Hz 12 dB/oct", amount);
                    double at31 = 0.0, at62 = 0.0;
                    for (const double hz : { 25.0, 31.0, 41.0, 55.0, 62.0, 98.0, 250.0 })
                    {
                        const auto in = sine (hz, -6.0 + 3.0103, 2.0);
                        Rig rig;
                        rig.prepare (version == 0 ? BassComp::KeyCut { 40.0, 2 } : BassComp::KeyCut {});
                        const auto out = drive (rig, in, 512, [amount] (size_t) { return std::pair<double, bool> { amount, true }; });
                        const double db = rmsDb (out, out.size() / 2, out.size());
                        at31 = hz == 31.0 ? db : at31;
                        at62 = hz == 62.0 ? db : at62;
                        std::printf (" %6.1f ", db);
                    }
                    spread[version][a] = at31 - at62;
                    std::printf ("  (31 over 62: %+.1f dB)\n", at31 - at62);
                }
            check (spread[1][0] < spread[0][0] && spread[1][1] < spread[0][1],
                   "the low B no longer rides up as COMP squeezes: 31 over 62 Hz at 1, " + juce::String (spread[0][1], 1)
                       + " dB before, " + juce::String (spread[1][1], 1) + " dB now");
        }

        // Crest of the bass line the PUNCHY offset is measured on: its peaks over its 10 ms RMS.
        {
            double worst = 0.0, sum = 0.0;
            int counted = 0;
            const auto w = (size_t) (0.010 * sampleRate);
            for (size_t i = 0; i + w < plucks.size(); i += w)
            {
                const double r = std::pow (10.0, rmsDb (plucks, i, i + w) / 20.0);
                if (r < 1.0e-3)
                    continue;
                const double c = juce::Decibels::gainToDecibels ((double) peakIn (plucks, i, i + w) / r);
                worst = std::max (worst, c);
                sum += c;
                ++counted;
            }
            std::printf ("      the bass line's crest over 10 ms RMS: %.1f dB mean, %.1f dB worst\n", sum / std::max (1, counted), worst);
        }

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
                c.process (x.data(), blockSize, amount, core::BypassFade::Span { 1.0f, 1.0f, false, 0 },
                           BassComp::Attack::med, BassComp::Release::automatic, BassComp::Character::smooth);
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
                c.process (y.data(), blockSize, 0.0, core::BypassFade::Span { 1.0f, 1.0f, false, 0 },
                           BassComp::Attack::med, BassComp::Release::automatic, BassComp::Character::smooth);
                same = same && y == x;
            }
            check (same, "...at 0, no compressor: loud noise passes bit for bit");
        }

        const double at120 = squeeze (120.0, 1.0), want = (-6.0 + 36.0) * (1.0 / 6.0 - 1.0);
        check (std::abs (at120 - want) < 2.0, "...at the top, 120 Hz at -6 dBFS squeezed " + juce::String (at120, 1)
                                                  + " dB, the curve says " + juce::String (want, 1));
        const double at25 = squeeze (25.0, 1.0);
        check (at25 > at120 + 3.0, "...a 25 Hz tone as loud squeezed " + juce::String (at25, 1) + " dB: the detector hears the lowest notes less");
    }

    // ---- TRANSITIONS (review round 1): every move a player, a recall or a host makes is a glide or a fade ------
    {
        // A processor as the transitions are heard: the cabinet and the limiter out, so the amp's own output is
        // what is judged and no ceiling hides a burst; its HPF off.
        const auto bare = [&]
        {
            auto t = make (bass);
            set (*t, params::cabOn, 0.0f);
            set (*t, params::limiterOn, 0.0f);
            set (*t, params::bassHpf, (float) params::BassHpf::off);
            return t;
        };
        constexpr int at = 120, len = 220;
        const auto say = [] (const Switch& w) { return juce::String (w.burstDb, 1) + " dB over steady, step x" + juce::String (w.stepRatio, 1); };

        // COMP switched in from STANDBY, its knob at the top: no burst of make-up into the amp, no step.
        {
            auto t = bare();
            set (*t, params::compAmount, 1.0f);
            set (*t, params::compOn, 0.0f);
            const auto v = tone (*t, 97.0, -6.0f, len, at, [&] { set (*t, params::compOn, 1.0f); });
            const auto w = judgeSwitch (v, at, 15.0);
            check (w.burstDb <= 1.0 && w.stepRatio <= 3.0, "COMP switched in at the top: " + say (w));
        }
        {
            auto t = bare();
            set (*t, params::compAmount, 1.0f);
            const auto v = tone (*t, 97.0, -6.0f, len, at, [&] { set (*t, params::compOn, 0.0f); });
            const auto w = judgeSwitch (v, at, 20.0);
            check (w.burstDb <= 1.0 && w.stepRatio <= 3.0, "COMP switched out at the top: " + say (w));
        }
        // A recall moves the knob 0 -> 0.8 in one block: a glide.
        {
            auto t = bare();
            const auto v = tone (*t, 97.0, -6.0f, len, at, [&] { set (*t, params::compAmount, 0.8f); });
            const auto w = judgeSwitch (v, at, 30.0);
            check (w.burstDb <= 1.0 && w.stepRatio <= 3.0, "COMP recalled 0 -> 0.8: " + say (w));
        }
        // The knob's zero is no cliff: 0.001 plays a quiet note within 0.1 dB of 0.
        {
            const auto quiet = [&] (float amount)
            {
                auto t = bare();
                set (*t, params::compAmount, amount);
                const auto v = tone (*t, 97.0, -40.0f, 120);
                double sum = 0.0;
                for (size_t i = v.size() / 2; i < v.size(); ++i)
                    sum += (double) v[i] * v[i];
                return 10.0 * std::log10 (sum / (double) (v.size() / 2) + 1.0e-30);
            };
            const double l0 = quiet (0.0f), l1 = quiet (0.001f), l5 = quiet (0.025f);
            check (std::abs (l1 - l0) <= 0.1 && std::abs (l5 - l0) <= 2.0,
                   "COMP's knob from its zero: 0.1 % plays " + juce::String (l1 - l0, 2) + " dB, 2.5 % " + juce::String (l5 - l0, 2) + " dB");
        }
        // DI/CAB moved 30 -> 80 % in one block: a glide, not a step of the difference.
        {
            auto t = bare();
            set (*t, params::cabOn, 1.0f);
            const auto v = tone (*t, 97.0, -12.0f, len, at, [&] { set (*t, params::diMix, 80.0f); });
            const auto w = judgeSwitch (v, at, 30.0);
            check (w.stepRatio <= 1.5, "DI/CAB moved 30 -> 80 %: " + say (w));
        }
        // The HPF switched off on a 26 Hz tone, its cut's phase near the corner: a crossfade.
        {
            auto t = bare();
            set (*t, params::bassHpf, (float) params::BassHpf::five);
            const auto v = tone (*t, 26.0, -12.0f, len, at, [&] { set (*t, params::bassHpf, (float) params::BassHpf::off); });
            const auto w = judgeSwitch (v, at, 20.0);
            check (w.stepRatio <= 1.5, "HPF 5 switched off on 26 Hz: " + say (w));
        }
        {
            auto t = bare();
            set (*t, params::bassHpf, (float) params::BassHpf::five);
            const auto v = tone (*t, 26.0, -12.0f, len, at, [&] { set (*t, params::bassHpf, (float) params::BassHpf::four); });
            const auto w = judgeSwitch (v, at, 40.0);
            check (w.stepRatio <= 1.5, "HPF 5 -> 4 on 26 Hz: " + say (w));
        }
        // An LPF switched off mid-note, then back on after silence: nothing of the note before comes back.
        {
            auto t = bare();
            set (*t, params::bassLpf, (float) params::BassLpf::fingers);
            constexpr int off = 60, on = 140;
            const auto v = tone (*t, 3000.0, -6.0f, 160, off, [&] { set (*t, params::bassLpf, (float) params::BassLpf::off); }, off);
            // ...the second move, by hand: run on from where the first left off.
            set (*t, params::bassLpf, (float) params::BassLpf::fingers);
            const auto after = tone (*t, 3000.0, -6.0f, 10, -1, {}, 0);
            const float before = peakIn (v, (size_t) (on - 10) * blockSize, v.size());
            const float replay = peakIn (after, 0, after.size());
            check (replay <= before + 1.0e-5f, "an LPF back on after silence replays nothing (" + juce::String (replay, 6)
                                                   + " against " + juce::String (before, 6) + ")");
        }
        // The cabinet's own cuts, hidden on the bass, do nothing there whatever their parameters say.
        {
            auto hidden = make (bass);
            set (*hidden, params::cabHpfOn, 1.0f);
            set (*hidden, params::cabHpfHz, 400.0f);
            set (*hidden, params::cabLpfOn, 1.0f);
            set (*hidden, params::cabLpfHz, 1500.0f);
            check (worstApart (play (*hidden, blocks, false).l, plain.l) == 0.0f, "the cabinet's own cuts, hidden, cut nothing on the bass");
        }
        // No BOOST on OrbitBass: its lift is not in the chain.
        {
            auto boosted = make (bass);
            set (*boosted, params::boostLevel, 6.0f);
            set (*boosted, params::preampBoost, 1.0f);
            const auto list = boosted->preamp.selectors();
            for (int i = 0; i < list.size() && i < params::numSelectors; ++i)
                if (list.getReference (i).name.trim().equalsIgnoreCase ("boost"))
                    set (*boosted, params::selectorId (params::preampId, i), 1.0f);
            auto level = make (bass);
            set (*level, params::boostLevel, 0.0f);
            const auto list2 = level->preamp.selectors();
            for (int i = 0; i < list2.size() && i < params::numSelectors; ++i)
                if (list2.getReference (i).name.trim().equalsIgnoreCase ("boost"))
                    set (*level, params::selectorId (params::preampId, i), 1.0f);
            set (*level, params::preampBoost, 1.0f);
            check (worstApart (play (*boosted, blocks, false).l, play (*level, blocks, false).l) == 0.0f,
                   "no BOOST lift on the bass: its level at +6 dB plays as at 0");
        }
        check (amp->getTailLengthSeconds() >= 5.0 && amp->getTailLengthSeconds() < 8.0,
               "the bass's tail is at least the cabinet's longest, 5 s (" + juce::String (amp->getTailLengthSeconds(), 2) + " s)");
    }

    // ---- THE THREE EFFECTS (step 3) ----------------------------------------------------------------------
    {
        check (value (*amp, params::octaveOn) > 0.5f && value (*amp, params::filterOn) > 0.5f && value (*amp, params::chorusOn) > 0.5f
                   && value (*amp, params::octaveAmount) == 0.0f && value (*amp, params::filterAmount) == 0.0f
                   && value (*amp, params::chorusAmount) == 0.0f,
               "OCTAVE, FILTER and CHORUS stand in the strip, their sliders at 0");

        // At 0, whatever their menus say, each is the dry bit for bit — both outputs.
        {
            auto moved = make (bass);
            set (*moved, params::octaveSub2, 1.0f);
            set (*moved, params::octaveToneHz, 400.0f);
            set (*moved, params::filterMode, 1.0f);
            set (*moved, params::filterResonance, 12.0f);
            set (*moved, params::filterKeepLows, 0.0f);
            set (*moved, params::chorusRateHz, 2.5f);
            set (*moved, params::chorusCentreMs, 20.0f);
            const auto heard = play (*moved, blocks, false);
            check (worstApart (heard.l, plain.l) == 0.0f && worstApart (heard.r, plain.r) == 0.0f,
                   "the three at 0, their menus moved: the step-2b output, bit for bit, both sides");
        }
        // Each turned up is heard.
        for (const auto& [id, name] : { std::pair<const char*, const char*> { params::octaveAmount, "OCTAVE" },
                                        { params::filterAmount, "FILTER" }, { params::chorusAmount, "CHORUS" } })
        {
            auto up = make (bass);
            set (*up, id, 0.8f);
            const auto heard = play (*up, blocks, false);
            check (worstApart (heard.l, plain.l) > 1.0e-3f, juce::String (name) + " at 0.8 is heard");
        }

        // One signal before CHORUS, two after it, the lows one in the centre. At 0 both outputs are the same samples.
        check (worstApart (plain.l, plain.r) == 0.0f, "before CHORUS one signal: both outputs the same, bit for bit");
        {
            auto wide = make (bass);
            set (*wide, params::chorusAmount, 1.0f);
            const auto heard = play (*wide, blocks, false);
            check (worstApart (heard.l, heard.r) > 1.0e-3f, "CHORUS turned up: two sides");
            // The lows in the centre: an E1, 41 Hz, through it — the sides' difference at 41 Hz against their sum.
            std::vector<float> r;
            const auto l = tone (*wide, 41.2, -12.0f, 150, -1, {}, 1 << 30, &r);
            std::vector<float> side (l.size()), mid (l.size());
            for (size_t i = 0; i < l.size(); ++i)
            {
                side[i] = l[i] - r[i];
                mid[i]  = l[i] + r[i];
            }
            const double apart = levelAt (side, 41.2) - levelAt (mid, 41.2);
            check (apart < -40.0, "...the lows in the centre: at 41 Hz the sides' difference " + juce::String (apart, 1) + " dB under their sum");
        }

        // THE ORDER. OCTAVE's sub reaches the AMP's output: an E2's octave down, at 41 Hz, comes up through the amp.
        {
            const auto at = [] (const std::vector<float>& v, double hz)
            {
                double c = 0.0, sn = 0.0;
                for (size_t i = v.size() / 2; i < v.size(); ++i)
                {
                    const double ph = juce::MathConstants<double>::twoPi * hz * (double) i / sampleRate;
                    c += v[i] * std::cos (ph);
                    sn += v[i] * std::sin (ph);
                }
                return 10.0 * std::log10 (c * c + sn * sn + 1.0e-30);
            };
            const auto sub = [&] (float amount)
            {
                auto t = make (bass);
                set (*t, params::cabOn, 0.0f);
                set (*t, params::bassHpf, (float) params::BassHpf::off);
                set (*t, params::octaveAmount, amount);
                return at (tone (*t, 82.41, -12.0f, 150), 41.2);
            };
            const double off = sub (0.0f), on = sub (1.0f);
            check (on > off + 20.0, "OCTAVE's sub comes out of the AMP: 41 Hz under an E2 up " + juce::String (on - off, 1) + " dB");
        }

        // FILTER's key is the gated input BEFORE COMP: the sweep follows the string, so COMP squeezing — and lifting a
        // quiet note by its make-up — moves it not at all; a louder string does.
        {
            const auto cutoff = [&] (float comp, float db)
            {
                auto t = make (bass);
                set (*t, params::cabOn, 0.0f);
                set (*t, params::compAmount, comp);
                set (*t, params::filterAmount, 0.6f);
                (void) tone (*t, 110.0, db, 100);
                return (double) t->filterShownCutoffHz();
            };
            const double plainQuiet = cutoff (0.0f, -36.0f), squeezed = cutoff (1.0f, -36.0f), loud = cutoff (0.0f, -6.0f);
            check (std::abs (squeezed - plainQuiet) <= 0.001 * plainQuiet && loud > plainQuiet * 1.5,
                   "FILTER keyed before COMP: a quiet string's sweep at " + juce::String (plainQuiet, 0) + " Hz, COMP at the top "
                       + juce::String (squeezed, 0) + " Hz; a loud one " + juce::String (loud, 0) + " Hz");
        }
    }

    // ---- WHOSE STATE IT IS ----------------------------------------------------------------------------
    {
        auto source = make (bass);
        set (*source, params::inTrim, 7.0f);
        juce::MemoryBlock bassState;
        source->getStateInformation (bassState);

        auto g = make (products::orbitAmp());
        g->setStateInformation (bassState.getData(), (int) bassState.getSize());
        check (std::abs (value (*g, params::inTrim)) < 0.01f, "OrbitAmp refuses OrbitBass's session");

        auto gsource = make (products::orbitAmp());
        set (*gsource, params::inTrim, 5.0f);
        juce::MemoryBlock guitarState;
        gsource->getStateInformation (guitarState);
        auto b = make (bass);
        b->setStateInformation (guitarState.getData(), (int) guitarState.getSize());
        check (std::abs (value (*b, params::inTrim)) < 0.01f, "OrbitBass refuses OrbitAmp's session");

        auto g2 = make (products::orbitAmp());
        g2->setStateInformation (guitarState.getData(), (int) guitarState.getSize());
        check (juce::approximatelyEqual (value (*g2, params::inTrim), 5.0f), "...which OrbitAmp loads, as before");

        auto b2 = make (bass);
        b2->setStateInformation (bassState.getData(), (int) bassState.getSize());
        check (juce::approximatelyEqual (value (*b2, params::inTrim), 7.0f), "...and OrbitBass loads its own");

        // A preset: the bass's is the bass's alone; one with no stamp — written before there was a bass — is a guitar
        // amp's, which the guitar amps load as before and the bass refuses.
        const auto bassPreset = source->presetForSaving();
        auto legacy = gsource->apvts.copyState();
        check (source->belongs (bassPreset) && ! gsource->belongs (bassPreset), "a bass preset is the bass's alone");
        check (gsource->belongs (legacy) && ! source->belongs (legacy), "a preset with no stamp is a guitar amp's");
        auto catpit = make (products::catpitAmp());
        check (catpit->belongs (legacy) && catpit->belongs (gsource->presetForSaving()) && ! catpit->belongs (bassPreset),
               "...for every guitar amp, the cockpit too");
    }

    // ---- THE LOAD WINDOW: a guitar amp's as it was before the bass's COMP, the bass's a row shorter ----------
    {
        LoadBreakdown gw (*guitar), bw (*amp);
        check (gw.getHeight() == 384 && bw.getHeight() == 384 + 2 * 22,
               "the load window: OrbitAmp's " + juce::String (gw.getHeight()) + " px (384 before), OrbitBass's " + juce::String (bw.getHeight())
                   + " (no DELAY, no REVERB; COMP, OCTAVE, FILTER, CHORUS)");
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
