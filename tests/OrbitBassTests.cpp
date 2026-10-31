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
#include "ui/GuardBlock.h"
#include "ui/CompBlock.h"
#include "ui/ChorusBlock.h"
#include "core/ChorusTap.h"
#include "ui/FaceplateView.h"
#include "channels/Engine.h"
#include "channels/Presets.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <thread>
#include <atomic>
#include <chrono>
#include <tuple>
#include <limits>
#include <map>
#include <string>
#include <type_traits>

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

    /** The largest difference, sample for sample — NOT A NUMBER where the two are not the same length or either holds
        a sample that is not a number, so that every comparison with it fails, "the same" and "different" alike: a
        comparison that can be blind is no comparison. */
    float worstApart (const std::vector<float>& a, const std::vector<float>& b)
    {
        if (a.size() != b.size() || a.empty())
            return std::numeric_limits<float>::quiet_NaN();
        float worst = 0.0f;
        for (size_t i = 0; i < a.size(); ++i)
        {
            if (! std::isfinite (a[i]) || ! std::isfinite (b[i]))
                return std::numeric_limits<float>::quiet_NaN();
            worst = std::max (worst, std::abs (a[i] - b[i]));
        }
        return worst;
    }

    /** WHAT STEP 2b PLAYED (78e037a, before the three effects): the first `blocks` blocks of `play`'s phrase, channel 0,
        on a fresh OrbitBass at its defaults — recorded by that tree's own build (a console linking 78e037a's sources
        against the same libraries) as little-endian float32, tests/golden/orbitbass-step2b.f32. Bit for bit on the
        compiler that recorded it; within 1e-4 on any other, whose libm and vector code may move a last bit. */
    struct StepTwoB
    {
        static constexpr int blocks = 40;
        std::vector<float> was;
        bool  recorder = false;
        float allowed  = 1.0e-4f;
    };

    StepTwoB stepTwoBReference()
    {
        StepTwoB ref;
        juce::MemoryBlock bytes;
        if (juce::File (ORBITBASS_STEP2B).loadFileAsData (bytes))
        {
            juce::MemoryInputStream in (bytes, false);
            while (! in.isExhausted())
                ref.was.push_back (in.readFloat());
        }
       #if defined (__APPLE__) && defined (__aarch64__)
        ref.recorder = juce::String (__VERSION__) == "Apple LLVM 21.0.0 (clang-2100.1.1.101)";
       #endif
        if (ref.recorder)
            ref.allowed = 0.0f;
        return ref;
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
    const auto stepTwoB = stepTwoBReference();
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
            BassComp::Attack attack = BassComp::Attack::med;
            void run (float* x, int n, double amount, bool on, BassComp::Release r = BassComp::Release::automatic,
                      BassComp::Character ch = BassComp::Character::smooth)
            {
                if (first)   // as a prepare stands it: where the session is, no glide
                {
                    link.snapTo (on && amount > 0.0);
                    c.snapAmount (amount);
                    first = false;
                }
                c.process (x, n, amount, link.advance (n, on && amount > 0.0), attack, r, ch);
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

    // ---- REVIEW ROUND 2 -----------------------------------------------------------------------------------
    {
        using core::BassComp;
        using core::BassCuts;
        const auto sine = [] (double hz, double peakDb, double seconds, double phase = 0.0)
        {
            std::vector<float> v ((size_t) (seconds * sampleRate));
            const double a = juce::Decibels::decibelsToGain (peakDb);
            for (size_t i = 0; i < v.size(); ++i)
                v[i] = (float) (a * std::sin (juce::MathConstants<double>::twoPi * hz * (double) i / sampleRate + phase));
            return v;
        };
        const auto rmsDb = [] (const std::vector<float>& v, size_t from, size_t to)
        {
            double sum = 0.0;
            for (size_t i = from; i < to && i < v.size(); ++i)
                sum += (double) v[i] * v[i];
            return 10.0 * std::log10 (sum / (double) std::max<size_t> (1, std::min (to, v.size()) - from) + 1.0e-30);
        };
        // The cuts through a list of blocks, the settings switching at the sample counts `at` says.
        const auto cuts = [] (BassCuts& c, std::vector<float> v, const std::vector<int>& blocks,
                              const std::function<BassCuts::Settings (size_t)>& at)
        {
            size_t i = 0;
            for (size_t b = 0; i < v.size(); ++b)
            {
                const int n = (int) std::min ((size_t) blocks[b % blocks.size()], v.size() - i);
                float* ch[] = { v.data() + i };
                c.process (ch, 1, n, at (i));
                i += (size_t) n;
            }
            return v;
        };

        // 1. A cut moved while OFF comes back where its word says: HPF 22 Hz, off, 28 Hz, on — as a fresh one at 28.
        {
            const auto in = sine (26.0, -12.0, 3.0);
            BassCuts moved, fresh;
            moved.prepare (sampleRate);
            fresh.prepare (sampleRate);
            const auto a = cuts (moved, in, { 512 }, [] (size_t i)
            {
                BassCuts::Settings s;
                s.hpfOn = i < 48000 || i >= 96000;
                s.hpfHz = i < 72000 ? 22.0 : 28.0;
                return s;
            });
            const auto b = cuts (fresh, in, { 512 }, [] (size_t) { BassCuts::Settings s; s.hpfOn = true; s.hpfHz = 28.0; return s; });
            const double la = rmsDb (a, 120000, 144000), lb = rmsDb (b, 120000, 144000);
            check (std::abs (la - lb) < 0.05, "a cut moved while off, 22 -> 28 Hz, comes back at 28: " + juce::String (la, 2)
                                                  + " dB on 26 Hz, a fresh 28 Hz " + juce::String (lb, 2));
        }
        // 2. A cut's glide keeps audio time, whatever the host's blocks: 22 -> 32 Hz the same samples at 16, 512, and
        // an irregular partition.
        {
            const auto in = sine (26.0, -12.0, 1.0);
            // The move lands at sample 1 in every partition (each starts with a one-sample block, the snap at 22 Hz).
            const auto move = [] (size_t i) { BassCuts::Settings s; s.hpfOn = true; s.hpfHz = i < 1 ? 22.0 : 32.0; return s; };
            std::vector<std::vector<float>> outs;
            for (const auto& parts : { std::vector<int> { 1, 512 }, std::vector<int> { 1, 16 }, std::vector<int> { 1, 7, 33, 100, 5, 61, 256 } })
            {
                BassCuts c;
                c.prepare (sampleRate);
                outs.push_back (cuts (c, in, parts, move));
            }
            check (worstApart (outs[0], outs[1]) == 0.0f && worstApart (outs[0], outs[2]) == 0.0f,
                   "a cut's glide, host blocks 512 / 16 / irregular: " + juce::String (worstApart (outs[0], outs[1]), 9) + ", "
                       + juce::String (worstApart (outs[0], outs[2]), 9) + " apart");
        }
        // 3. COMP recalled cold, every attack, on low notes at three phases, the whole settling: no burst.
        {
            double worst = -100.0;
            juce::String where;
            for (const auto attack : { BassComp::Attack::slow, BassComp::Attack::med, BassComp::Attack::fast })
                for (const double hz : { 31.0, 41.2, 55.0 })
                    for (const double phase : { 0.0, 2.1, 4.2 })
                    {
                        const auto in = sine (hz, -6.0, 2.0, phase);
                        BassComp c;
                        (void) c.prepare (sampleRate, 512);
                        core::BypassFade link;
                        link.prepare (sampleRate);
                        link.snapTo (false);
                        auto out = in;
                        const size_t sw = (size_t) sampleRate;
                        for (size_t i = 0; i < out.size(); i += 512)
                        {
                            const double amount = i >= sw ? 0.8 : 0.0;
                            c.process (out.data() + i, (int) std::min<size_t> (512, out.size() - i), amount,
                                       link.advance (512, amount > 0.0), attack, BassComp::Release::automatic, BassComp::Character::smooth);
                        }
                        const double burst = juce::Decibels::gainToDecibels ((double) peakIn (out, sw, out.size()) / (double) peakIn (in, 0, in.size()));
                        if (burst > worst)
                        {
                            worst = burst;
                            where = juce::String ((int) attack) + " attack, " + juce::String (hz, 1) + " Hz";
                        }
                    }
            check (worst <= 1.0, "COMP recalled cold 0 -> 0.8, every attack, 31/41/55 Hz, three phases, a second after: worst peak "
                                     + juce::String (worst, 2) + " dB over the input (" + where + ")");
        }
        // 4. A prepare with COMP up plays it from the first sample: a note from sample 0 is what a COMP that waited in
        // silence hears when the note comes — no step 35 ms in.
        {
            const auto note = sine (97.0, -6.0, 0.5);
            std::vector<float> late ((size_t) (sampleRate / 2), 0.0f);
            late.insert (late.end(), note.begin(), note.end());
            const auto run = [] (std::vector<float> v)
            {
                BassComp c;
                (void) c.prepare (sampleRate, 512);
                c.snapAmount (0.8);
                core::BypassFade link;
                link.prepare (sampleRate);
                link.snapTo (true);
                for (size_t i = 0; i < v.size(); i += 512)
                    c.process (v.data() + i, (int) std::min<size_t> (512, v.size() - i), 0.8, link.advance (512, true),
                               BassComp::Attack::med, BassComp::Release::automatic, BassComp::Character::smooth);
                return v;
            };
            const auto first = run (note), waited = run (late);
            const std::vector<float> tail (waited.begin() + (long) (sampleRate / 2), waited.end());
            check (worstApart (first, tail) <= 1.0e-6f, "a prepare with COMP up plays it from the first sample, as one that waited: "
                                                             + juce::String (worstApart (first, tail), 9) + " apart");
        }
        // 5. COMP turned down mid-note, 1 -> 0.3: the make-up comes down as the squeeze lets go — no duck under where
        // the note stood or where it settles.
        {
            const auto in = sine (97.0, -6.0, 4.0);
            BassComp c;
            (void) c.prepare (sampleRate, 512);
            c.snapAmount (1.0);
            core::BypassFade link;
            link.prepare (sampleRate);
            link.snapTo (true);
            auto out = in;
            const size_t sw = (size_t) (2.0 * sampleRate);
            for (size_t i = 0; i < out.size(); i += 512)
                c.process (out.data() + i, (int) std::min<size_t> (512, out.size() - i), i >= sw ? 0.3 : 1.0, link.advance (512, true),
                           BassComp::Attack::med, BassComp::Release::automatic, BassComp::Character::smooth);
            const auto win = (size_t) (0.010 * sampleRate);
            const double before = rmsDb (out, sw - 10 * win, sw), after = rmsDb (out, out.size() - 10 * win, out.size());
            double lowest = 100.0;
            for (size_t i = sw; i + win <= out.size(); i += win)
                lowest = std::min (lowest, rmsDb (out, i, i + win));
            check (lowest >= std::min (before, after) - 1.0, "COMP 1 -> 0.3 mid-note: the lowest 10 ms " + juce::String (lowest, 1)
                                                                 + " dB, before " + juce::String (before, 1) + ", settled " + juce::String (after, 1));
        }
        // 6. BLEND as the state has it after a prepare on a host's own thread, rendered at once, before any tick.
        {
            auto b = make (bass);
            auto* blendP = b->apvts.getParameter (params::ampBlend);
            auto* hzP    = b->apvts.getParameter (params::ampBlendHz);
            blendP->setValueNotifyingHost (blendP->convertTo0to1 (0.6f));
            hzP->setValueNotifyingHost (hzP->convertTo0to1 (200.0f));
            std::thread host ([&] { b->prepareToPlay (sampleRate, blockSize); });
            host.join();
            check (juce::approximatelyEqual (b->preamp.crossoverAmountTold(), 0.6f) && std::abs (b->preamp.crossoverHzTold() - 200.0f) < 0.5f,
                   "BLEND after a prepare on the host's thread: told 60 %, 200 Hz before the first block ("
                       + juce::String (b->preamp.crossoverAmountTold(), 2) + ")");
        }
        // 7. A bass preset renamed into the cockpit's folder is refused by the cockpit's own loader; its own loads.
        {
            auto cockpit = make (products::catpitAmp());
            auto& engine = channels::Engine::of (*cockpit);
            channels::Presets presets (*cockpit, engine.channels);
            auto source = make (bass);
            set (*source, params::inTrim, 6.0f);
            const bool wrote = presets.store().write ("From the bass", source->presetForSaving());
            const float trimBefore = value (*cockpit, params::inTrim);
            const bool loaded = presets.load ("From the bass");
            check (wrote && ! loaded && std::abs (value (*cockpit, params::inTrim) - trimBefore) < 0.01f,
                   "the cockpit's own loader refuses a bass preset renamed into its folder");
            (void) presets.store().remove ("From the bass");
            check (presets.save ("Its own") && presets.load ("Its own"), "...and loads its own");
            (void) presets.store().remove ("Its own");
        }
        // 8. COMP standing by, fed, shows no squeeze.
        {
            auto t = make (bass);
            set (*t, params::compAmount, 0.8f);
            set (*t, params::compOn, 0.0f);
            (void) tone (*t, 97.0, -6.0f, 60);
            check (t->compGrDb.load() == 0.0f, "COMP standing by shows no squeeze (" + juce::String (t->compGrDb.load(), 1) + " dB)");
        }
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
            const auto heard = play (*moved, stepTwoB.blocks, false);
            const auto fresh = play (*make (bass), stepTwoB.blocks, false);
            const float apartL = worstApart (heard.l, stepTwoB.was), apartR = worstApart (heard.r, stepTwoB.was);
            const float apartPlain = worstApart (fresh.l, stepTwoB.was);
            check (stepTwoB.was.size() == (size_t) (stepTwoB.blocks * blockSize) && apartL <= stepTwoB.allowed
                       && apartR <= stepTwoB.allowed && apartPlain <= stepTwoB.allowed,
                   "the three at 0, their menus moved — and at their defaults: what step 2b (78e037a) played, "
                       + juce::String (stepTwoB.recorder ? "bit for bit" : "within 1e-4") + ", both sides (worst "
                       + juce::String (apartL, 9) + " / " + juce::String (apartR, 9) + ", defaults " + juce::String (apartPlain, 9) + ")");
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

    // ---- REVIEW ROUND 3 (step 3's): the adapter, the key, the planes, the face ----------------------------------
    {
        struct Fx { const char* amount; const char* on; const char* name; };
        const Fx fx[] { { params::octaveAmount, params::octaveOn, "OCTAVE" }, { params::filterAmount, params::filterOn, "FILTER" },
                        { params::chorusAmount, params::chorusOn, "CHORUS" } };
        const auto again = [] (AmpProcessor& a) { a.prepareToPlay (sampleRate, blockSize); a.pumpDeviceWork(); };
        const auto say = [] (const Switch& w) { return juce::String (w.burstDb, 1) + " dB over steady, step x" + juce::String (w.stepRatio, 1); };

        // A PREPARE TELLS EACH EFFECT WHERE IT STANDS before its first sample: standing by with its slider up it is a
        // slider at 0 from the first sample on — nothing of it, bit for bit, both sides (CHORUS's stage used to arrive at
        // its own defaults, half wet, and fade out over 20 ms) — and up, it is told once, at the prepare, not again by
        // the first block.
        {
            auto zero = make (bass);
            again (*zero);
            const auto quiet = play (*zero, 60, false);

            auto standing = make (bass);
            for (const auto& f : fx)
            {
                set (*standing, f.amount, 0.8f);
                set (*standing, f.on, 0.0f);
            }
            again (*standing);
            const auto heard = play (*standing, 60, false);
            check (worstApart (heard.l, quiet.l) == 0.0f && worstApart (heard.r, quiet.r) == 0.0f
                       && standing->effectIdle (0) && standing->effectIdle (1) && standing->effectIdle (2),
                   "the three standing by from a prepare, their sliders at 0.8: the three at 0, bit for bit, both sides, idle (worst "
                       + juce::String (worstApart (heard.l, quiet.l), 9) + " / " + juce::String (worstApart (heard.r, quiet.r), 9) + ")");

            // ...and after they have played: up, then standing by, then a prepare — each idle from its first block, not
            // arriving at its last knobs and fading out of them.
            auto played = make (bass);
            for (const auto& f : fx)
                set (*played, f.amount, 0.8f);
            (void) tone (*played, 82.41, -12.0f, 60);
            for (const auto& f : fx)
                set (*played, f.on, 0.0f);
            again (*played);
            (void) tone (*played, 82.41, -12.0f, 1);
            check (played->effectIdle (0) && played->effectIdle (1) && played->effectIdle (2),
                   "...and after playing up, standing by and a prepare: idle from the first block (OCTAVE "
                       + juce::String (played->effectIdle (0) ? "idle" : "working") + ", FILTER " + (played->effectIdle (1) ? "idle" : "working")
                       + ", CHORUS " + (played->effectIdle (2) ? "idle" : "working") + ")");

            auto up = make (bass);
            for (const auto& f : fx)
                set (*up, f.amount, 0.8f);
            const int before[] { up->effectTells (0), up->effectTells (1), up->effectTells (2) };
            again (*up);
            const int atPrepare[] { up->effectTells (0), up->effectTells (1), up->effectTells (2) };
            (void) tone (*up, 82.41, -12.0f, 1);
            bool once = true;
            for (int k = 0; k < 3; ++k)
                once = once && atPrepare[k] == before[k] + 1 && up->effectTells (k) == atPrepare[k];
            check (once, "a prepare with the three up tells each its knobs once, before the first sample; the first block tells nothing more");

            // ...and nothing moved, nothing told: a hundred blocks, not one more setParams.
            const int settled[] { up->effectTells (0), up->effectTells (1), up->effectTells (2) };
            (void) tone (*up, 82.41, -12.0f, 100);
            check (up->effectTells (0) == settled[0] && up->effectTells (1) == settled[1] && up->effectTells (2) == settled[2],
                   "...and 100 blocks with nothing moved tell nothing (" + juce::String (up->effectTells (0) - settled[0]) + ", "
                       + juce::String (up->effectTells (1) - settled[1]) + ", " + juce::String (up->effectTells (2) - settled[2]) + ")");
        }

        // A SLIDER THAT IS NOT A NUMBER keeps the last one that was: the three up, a host's automation sends NaN to all
        // three — they play on as they were, bit for bit (it used to read as their default, 0: all three switched off).
        {
            const auto run = [&] (bool poison)
            {
                auto t = make (bass);
                set (*t, params::octaveAmount, 0.8f);
                set (*t, params::filterAmount, 0.6f);
                set (*t, params::chorusAmount, 0.7f);
                return tone (*t, 82.41, -12.0f, 120, 40, [&]
                {
                    if (poison)
                        for (const auto& f : fx)
                            t->apvts.getRawParameterValue (f.amount)->store (std::numeric_limits<float>::quiet_NaN());
                });
            };
            const float apart = worstApart (run (true), run (false));
            check (apart == 0.0f, "the three's sliders sent NaN mid-note keep their last amounts, bit for bit (worst " + juce::String (apart, 9) + ")");
        }

        // STANDBY IS SLIDER 0: the three up, playing, then their arrows off — each ramps to idle and computes nothing.
        {
            auto t = make (bass);
            for (const auto& f : fx)
                set (*t, f.amount, 0.8f);
            (void) tone (*t, 82.41, -12.0f, 60);
            const bool working = ! t->effectIdle (0) && ! t->effectIdle (1) && ! t->effectIdle (2);
            for (const auto& f : fx)
                set (*t, f.on, 0.0f);
            (void) tone (*t, 82.41, -12.0f, 100);
            check (working && t->effectIdle (0) && t->effectIdle (1) && t->effectIdle (2),
                   "the three up work; their arrows off, each ramps to idle and stops");
        }

        // TRANSITIONS THROUGH THE PROCESSOR, mid-note: a slider leaving 0, an arrow switched out with the slider up.
        for (const auto& f : fx)
        {
            constexpr int at = 120, len = 220;
            const auto bare = [&]
            {
                auto t = make (bass);
                set (*t, params::cabOn, 0.0f);
                set (*t, params::bassHpf, (float) params::BassHpf::off);
                return t;
            };
            {
                auto t = bare();
                const auto v = tone (*t, 82.41, -12.0f, len, at, [&] { set (*t, f.amount, 0.8f); });
                const auto w = judgeSwitch (v, at, 30.0);
                check (w.burstDb <= 1.0 && w.stepRatio <= 3.0, juce::String (f.name) + "'s slider leaving 0 mid-note: " + say (w));
            }
            {
                auto t = bare();
                set (*t, f.amount, 0.8f);
                const auto v = tone (*t, 82.41, -12.0f, len, at, [&] { set (*t, f.on, 0.0f); });
                const auto w = judgeSwitch (v, at, 30.0);
                check (w.burstDb <= 1.0 && w.stepRatio <= 3.0, juce::String (f.name) + "'s arrow switched out mid-note, its slider up: " + say (w));
            }
        }

        // A MENU FIELD REACHES ITS MODULE with the slider up: LP against BP, the -2 at the top, KEEP LOWS.
        {
            const auto apart = [&] (const char* amount, const char* field, float a, float b)
            {
                const auto with = [&] (float v)
                {
                    auto t = make (bass);
                    set (*t, amount, 0.8f);
                    set (*t, field, v);
                    return tone (*t, 82.41, -12.0f, 100);
                };
                return worstApart (with (a), with (b));
            };
            const float mode = apart (params::filterAmount, params::filterMode, 0.0f, 1.0f);
            const float sub2 = apart (params::octaveAmount, params::octaveSub2, 0.0f, 1.0f);
            const float keep = apart (params::filterAmount, params::filterKeepLows, 1.0f, 0.0f);
            check (mode > 1.0e-3f && sub2 > 1.0e-3f && keep > 1.0e-3f,
                   "the menus reach the modules, sliders up: FILTER LP/BP apart by " + juce::String (mode, 4) + ", OCTAVE's -2 by "
                       + juce::String (sub2, 4) + ", KEEP LOWS by " + juce::String (keep, 4));
        }

        // THE ORDER: OCTAVE before FILTER and the AMP. FILTER at the top on BP, its lows not kept, sweeps the sub with the
        // note: 41 Hz under an E2 falls against the note's 82 Hz — had the sub come after FILTER (or after the AMP, past
        // it) the band would not touch it.
        {
            const auto subOverNote = [&] (bool bp)
            {
                auto t = make (bass);
                set (*t, params::cabOn, 0.0f);
                set (*t, params::bassHpf, (float) params::BassHpf::off);
                set (*t, params::octaveAmount, 1.0f);
                if (bp)
                {
                    set (*t, params::filterAmount, 1.0f);
                    set (*t, params::filterMode, 1.0f);
                    set (*t, params::filterKeepLows, 0.0f);
                }
                const auto v = tone (*t, 82.41, -12.0f, 150);
                return levelAt (v, 41.2) - levelAt (v, 82.41);
            };
            const double plainRatio = subOverNote (false), swept = subOverNote (true);
            check (swept < plainRatio - 6.0, "OCTAVE before FILTER and the AMP: the sub against the note "
                                                 + juce::String (plainRatio, 1) + " dB, through FILTER's band " + juce::String (swept, 1) + " dB");
        }

        // FILTER'S KEY IS GATED WHEREVER THE GATE MUTES: a string under the gate's threshold moves the sweep no more with
        // the gate at the END (the default) than at the START, where the key is taken after the mute.
        {
            const auto cutoffWith = [&] (bool gated, int pos)
            {
                auto t = make (bass);
                set (*t, params::cabOn, 0.0f);
                set (*t, params::gateOn, gated ? 1.0f : 0.0f);
                set (*t, params::gateThreshold, -30.0f);
                set (*t, params::gatePos, (float) pos);
                set (*t, params::filterAmount, 1.0f);
                (void) tone (*t, 110.0, -34.0f, 100);
                return (double) t->filterShownCutoffHz();
            };
            const double open = cutoffWith (false, 1), atStart = cutoffWith (true, 0), atEnd = cutoffWith (true, 1);
            check (open > 1.2 * atStart && std::abs (atEnd - atStart) <= 1.0e-3 * atStart,
                   "FILTER's key gated with the gate at the END: a string under the threshold sweeps it to " + juce::String (atEnd, 1)
                       + " Hz, as at the START (" + juce::String (atStart, 1) + " Hz); ungated it would to " + juce::String (open, 1) + " Hz");
        }

        // 2 -> 1 -> 2: A HOST NARROWS ITS CALLS AND WIDENS THEM AGAIN, no prepare between. The back half stays the
        // prepared bus's width — the cuts' coefficients follow a move made while narrow, no plane keeps stale samples,
        // the limiter keeps its topology — so the left plays, and the right comes back, exactly as through calls that
        // never narrowed. CHORUS up (two different planes), the HPF moved while narrow; LIMIT in and out.
        for (const bool limit : { true, false })
        {
            const auto run = [&] (bool narrow)
            {
                auto t = make (bass);
                set (*t, params::chorusAmount, 0.6f);
                set (*t, params::limiterOn, limit ? 1.0f : 0.0f);
                Out out;
                juce::AudioBuffer<float> two (2, blockSize), one (1, blockSize);
                juce::MidiBuffer midi;
                long long n = 0;
                for (int b = 0; b < 200; ++b)
                {
                    const bool thin = narrow && b >= 60 && b < 100;
                    if (b == 80)
                        set (*t, params::bassHpf, (float) params::BassHpf::four);
                    auto& buf = thin ? one : two;
                    for (int i = 0; i < blockSize; ++i, ++n)
                    {
                        const double tt = (double) n / sampleRate;
                        const auto v = (float) (0.5 * std::exp (-2.0 * std::fmod (tt, 0.8)) * std::sin (juce::MathConstants<double>::twoPi * 41.2 * tt));
                        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                            buf.setSample (ch, i, v);
                    }
                    t->processBlock (buf, midi);
                    t->pumpDeviceWork();
                    out.l.insert (out.l.end(), buf.getReadPointer (0), buf.getReadPointer (0) + blockSize);
                    if (b >= 100)
                        out.r.insert (out.r.end(), buf.getReadPointer (1), buf.getReadPointer (1) + blockSize);
                }
                return out;
            };
            const auto wide = run (false), thin = run (true);
            const std::vector<float> wideLate (wide.l.end() - (std::ptrdiff_t) wide.r.size(), wide.l.end());   // the left, where the right is kept
            check (worstApart (thin.l, wide.l) == 0.0f && worstApart (thin.r, wide.r) == 0.0f && worstApart (wideLate, wide.r) > 1.0e-4f,
                   juce::String ("2 -> 1 -> 2 calls, LIMIT ") + (limit ? "in" : "out") + ": the left as through stereo calls, bit for bit (worst "
                       + juce::String (worstApart (thin.l, wide.l), 9) + "), the returning right too (" + juce::String (worstApart (thin.r, wide.r), 9) + ")");
        }

        // THE LAMPS: each effect's lights while it works — OCTAVE while its sub plays (locked), FILTER and CHORUS while
        // not idle — and goes dark when it stops.
        {
            auto t = make (bass);
            for (const auto& f : fx)
                set (*t, f.amount, 0.8f);
            (void) tone (*t, 82.41, -12.0f, 60);
            const bool lit = t->effectLampDepth (0) > 0.5f && t->effectLampDepth (1) > 0.5f && t->effectLampDepth (2) > 0.5f;
            for (const auto& f : fx)
                set (*t, f.amount, 0.0f);
            (void) tone (*t, 82.41, -12.0f, 100, -1, {}, 0);
            const bool dark = t->effectLampDepth (0) < 0.5f && t->effectLampDepth (1) < 0.5f && t->effectLampDepth (2) < 0.5f;
            check (lit && dark, "the effects' lamps light while each works and go dark at 0");
        }

        // 13 PX: the effects' rails read nothing smaller — their scales carry no numbers (the grip shows the value); the
        // guards' scales are as they were.
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed (amp->createEditor());
            juce::StringArray small, guards;
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
            {
                if (auto* g = dynamic_cast<GuardBlock*> (&c))
                {
                    const bool guard = g->guardName() == "GATE" || g->guardName() == "LIMIT";
                    if (guard)
                        guards.add (g->guardName());
                    else if (g->numeralHeight() > 0.0f && g->numeralHeight() < 13.0f)
                        small.add (g->guardName() + " " + juce::String (g->numeralHeight(), 1) + " px");
                }
                for (auto* child : c.getChildren())
                    walk (*child);
            };
            walk (*ed);
            check (small.isEmpty() && guards.size() == 2, "the effects' rails write no number under 13 px"
                                                              + (small.isEmpty() ? juce::String() : ": " + small.joinIntoString (", ")));
        }
    }

    // ---- STEP 4: COMP, A BLOCK OF ITS OWN — its line and its slider one control, its ratio held and given back, its
    //      taps idle while nobody looks and still in silence ------------------------------------------------------------
    {
        using core::BassComp;
        const auto findIn = [] (juce::Component& root, auto* none)
        {
            using T = std::remove_pointer_t<decltype (none)>;
            T* found = nullptr;
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
            {
                if (auto* t = dynamic_cast<T*> (&c); t != nullptr && found == nullptr)
                    found = t;
                for (auto* child : c.getChildren())
                    walk (*child);
            };
            walk (root);
            return found;
        };

        // THE LAW BOTH WAYS: every knob position the parameter holds (its step, 0.001) to its threshold and back,
        // either character; the ends where the law puts them.
        {
            double worst = 0.0;
            for (const auto c : { BassComp::Character::smooth, BassComp::Character::punchy })
                for (int k = 0; k <= 1000; ++k)
                {
                    const double a = k * 0.001;
                    worst = std::max (worst, std::abs (BassComp::amountForThreshold (BassComp::thresholdAt (a, c), c) - a));
                }
            const auto s = BassComp::Character::smooth;
            check (worst < 1.0e-12 && BassComp::thresholdAt (0.0, s) == -12.0 && BassComp::thresholdAt (1.0, s) == -36.0
                       && BassComp::amountForThreshold (-60.0, s) == 1.0 && BassComp::amountForThreshold (0.0, s) == 0.0
                       && BassComp::thresholdAt (0.5, BassComp::Character::punchy) == -24.0 + BassComp::punchyOffsetDb,
                   "COMP's line and slider, the law both ways: knob -> threshold -> knob, worst " + juce::String (worst, 15));
        }

        // ...AND ON THE FACE: the line dragged across every pixel row of the law's span moves the slider to the knob whose
        // threshold it is, the line drawn back where the hand is — one gesture for the whole drag; the slider moved, the
        // line follows.
        {
            auto t = make (bass);
            std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
            auto* graph = findIn (*ed, (CompGraph*) nullptr);
            auto* block = findIn (*ed, (CompBlock*) nullptr);
            struct Gestures final : juce::AudioProcessorParameter::Listener
            {
                int begun = 0, ended = 0;
                void parameterValueChanged (int, float) override {}
                void parameterGestureChanged (int, bool starting) override { ++(starting ? begun : ended); }
            } gestures;
            auto* amountP = t->apvts.getParameter (params::compAmount);
            float worstPx = 1.0e9f;
            int rows = 0;
            if (graph != nullptr)
            {
                amountP->addListener (&gestures);
                worstPx = 0.0f;
                graph->beginLineDrag();
                for (float y = std::ceil (graph->yOfDb (-12.0)); y <= std::floor (graph->yOfDb (-36.0)); y += 1.0f, ++rows)
                {
                    graph->dragLineTo (y);
                    worstPx = std::max (worstPx, std::abs (graph->yOfDb (graph->thresholdDb()) - y));
                }
                graph->endLineDrag();
                amountP->removeListener (&gestures);
                set (*t, params::compAmount, 0.5f);
            }
            check (graph != nullptr && block != nullptr && block->getWidth() == FaceplateView::compWidth && rows > 20 && worstPx <= 0.5f
                       && gestures.begun == 1 && gestures.ended == 1 && std::abs (graph->thresholdDb() + 24.0) < 1.0e-3,
                   "the line dragged over " + juce::String (rows) + " pixel rows: the slider where the line says, the line back within "
                       + juce::String (worstPx, 3) + " px, " + juce::String (gestures.begun) + " gesture; the slider at 0.5, the line at "
                       + juce::String (graph != nullptr ? graph->thresholdDb() : 0.0, 2) + " dB");
        }

        // THE RATIO HELD AND GIVEN BACK: the law's at the knob; held at 8:1 it stays 8:1 with the slider moved; "RATIO: BY
        // THE SLIDER" and it is the law's again, exactly — gliding there mid-note, no burst, no step.
        {
            auto t = make (bass);
            set (*t, params::compAmount, 0.5f);
            (void) tone (*t, 97.0, -6.0f, 40);
            const double law = t->compRatioNow();
            set (*t, params::compRatio, 8.0f);
            set (*t, params::compRatioHeld, 1.0f);
            (void) tone (*t, 97.0, -6.0f, 10);
            const double held = t->compRatioNow();
            set (*t, params::compAmount, 0.8f);
            (void) tone (*t, 97.0, -6.0f, 10);
            const double heldMoved = t->compRatioNow();
            set (*t, params::compRatioHeld, 0.0f);
            (void) tone (*t, 97.0, -6.0f, 10);
            const double given = t->compRatioNow();
            check (law == BassComp::lawAt ((double) 0.5f).ratio
                       && std::abs (held - 8.0) < 1.0e-3 && std::abs (heldMoved - 8.0) < 1.0e-3
                       && given == BassComp::lawAt ((double) value (*t, params::compAmount)).ratio,
                   "COMP's ratio: the law's " + juce::String (law, 2) + ":1, held " + juce::String (held, 2) + ":1, the slider moved "
                       + juce::String (heldMoved, 2) + ":1, given back " + juce::String (given, 2) + ":1");

            auto u = make (bass);
            set (*u, params::cabOn, 0.0f);
            set (*u, params::compAmount, 0.5f);
            set (*u, params::compRatio, 10.0f);
            set (*u, params::compRatioHeld, 1.0f);
            const auto v = tone (*u, 97.0, -6.0f, 220, 120, [&] { set (*u, params::compRatioHeld, 0.0f); });
            const auto w = judgeSwitch (v, 120, 30.0);
            check (w.burstDb <= 1.0 && w.stepRatio <= 3.0, "...given back mid-note at 10:1, a glide: " + juce::String (w.burstDb, 1)
                                                               + " dB over steady, step x" + juce::String (w.stepRatio, 1));

            // A session and a preset keep both.
            set (*t, params::compRatio, 7.5f);
            set (*t, params::compRatioHeld, 1.0f);
            juce::MemoryBlock session;
            t->getStateInformation (session);
            auto back = make (bass);
            back->setStateInformation (session.getData(), (int) session.getSize());
            const auto preset = t->presetForSaving();
            const auto inPreset = [&] (const char* id) { return (float) preset.getChildWithProperty ("id", id).getProperty ("value", -1.0f); };
            check (std::abs (value (*back, params::compRatio) - 7.5f) < 0.01f && value (*back, params::compRatioHeld) > 0.5f
                       && std::abs (inPreset (params::compRatio) - 7.5f) < 0.01f && inPreset (params::compRatioHeld) > 0.5f,
                   "a session and a preset keep the held ratio and that it is held");
        }

        // THE TAPS: idle with no picture on screen — an editor up but not showing included; written while it is; still
        // once the playing stops — the picture freezes in silence instead of scrolling an empty floor.
        {
            auto t = make (bass);
            set (*t, params::compAmount, 0.6f);
            std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
            (void) tone (*t, 97.0, -6.0f, 50);
            const auto hidden = t->compTap.written();
            t->compWatched.store (true);
            (void) tone (*t, 97.0, -6.0f, 100);
            const auto playing = t->compTap.written();
            (void) tone (*t, 97.0, -6.0f, 200, -1, {}, 0);
            const auto tail = t->compTap.written();
            (void) tone (*t, 97.0, -6.0f, 100, -1, {}, 0);
            const auto still = t->compTap.written();
            check (hidden == 0 && playing > 0 && tail > playing && still == tail,
                   "COMP's taps: nothing written with the picture off screen (" + juce::String ((int) hidden) + "), "
                       + juce::String ((int) playing) + " columns while it shows, " + juce::String ((int) (tail - playing))
                       + " of the tail, then none in silence (" + juce::String ((int) (still - tail)) + ")");
        }

        // ---- CHORUS, A BLOCK WITH ITS PICTURE: its two voices on a circle, where the stage's LFO is ---------------------
        // THE PHASE IS THE STAGE'S: the tap derives it (the stage keeps its LFO to itself), so it is checked against the
        // stage's own output — a click into a stage that has run a while, whose voice comes back where its heads stand
        // (head = centre + swing · sin 2π·phase on the left, · cos on the right). Where the tap's phase puts the echo and
        // where it comes, in samples: a phase a hundredth of a turn off misses by up to 9.
        {
            using felitronics::chorus::BassChorus;
            using felitronics::chorus::BassChorusParams;
            struct Call { int n; float rate; float mix; bool gap; };
            const auto echoMiss = [] (const std::vector<Call>& calls) -> double
            {
                constexpr double fs = 48000.0;
                const auto knobs = [] (const Call& c) { return BassChorusParams { 3.0f, c.mix, c.rate, 100.0f, 8.0f }; };
                BassChorus st;
                core::ChorusTap tap;
                st.setParams (knobs (calls.front()));
                if (! st.prepare (fs, 2048, 2))
                    return 1.0e9;
                tap.prepare (fs, st);
                std::vector<float> l (2048), r (2048);
                float* io[] { l.data(), r.data() };
                for (const auto& c : calls)
                {
                    std::fill (l.begin(), l.end(), 0.0f);
                    std::fill (r.begin(), r.end(), 0.0f);
                    st.setParams (knobs (c));
                    const bool was = st.isIdle();
                    const bool ok  = st.process (c.gap ? nullptr : io, c.gap ? 0 : 2, c.n);
                    tap.follow (st, was, ok ? c.n : 0, false);
                }
                const double phi0 = tap.phase(), rate = (double) calls.back().rate;
                std::fill (l.begin(), l.end(), 0.0f);
                std::fill (r.begin(), r.end(), 0.0f);
                l[0] = r[0] = 1.0f;
                (void) st.process (io, 2, 1024);
                l[0] -= 1.0f;   // the dry is the input's own, bit for bit: what is left is the voice
                r[0] -= 1.0f;
                const double centre = 8.0 * fs / 1000.0;
                const double swing  = felitronics::chorus::detail::smoothMin (3.0 * fs / 1000.0, centre - fs / 1000.0, fs / 1000.0);
                double worst = 0.0;
                for (int v = 0; v < 2; ++v)
                {
                    const auto& y = v == 0 ? l : r;
                    int at = 16;
                    for (int i = 16; i < 1000; ++i)
                        if (y[(size_t) i] > y[(size_t) at])
                            at = i;
                    double m = 0.0, w = 0.0;
                    for (int i = at - 2; i <= at + 2; ++i)
                    {
                        m += (double) i * (double) y[(size_t) i];
                        w += (double) y[(size_t) i];
                    }
                    // Sample i's phase is phi0 + (i + 1)·rate/fs (the stage steps its clock, then reads); the echo comes
                    // where i is its own head.
                    double p = centre;
                    for (int k = 0; k < 30; ++k)
                    {
                        const double a = juce::MathConstants<double>::twoPi * (phi0 + (p + 1.0) * rate / fs);
                        p = centre + swing * (v == 0 ? std::sin (a) : std::cos (a));
                    }
                    worst = std::max (worst, std::abs (m / w - p));
                }
                return worst;
            };
            std::vector<Call> steady (140, Call { 512, 0.7f, 1.0f, false });
            std::vector<Call> moved (30, Call { 333, 0.3f, 1.0f, false });   // the rate moved: the stage's glide, odd blocks, gaps
            for (int k = 0; k < 60; ++k)
                moved.push_back ({ std::array<int, 4> { 37, 480, 1024, 7 }[(size_t) k % 4], 2.5f, 1.0f, k < 20 && k % 7 == 3 });
            std::vector<Call> woken (50, Call { 480, 1.5f, 1.0f, false });   // playing, then idle, then woken: a restart
            for (int k = 0; k < 50; ++k)
                woken.push_back ({ 480, 1.5f, 0.0f, false });
            for (int k = 0; k < 37; ++k)
                woken.push_back ({ 480, 1.5f, 1.0f, false });
            const double a = echoMiss (steady), b = echoMiss (moved), c = echoMiss (woken);
            check (a < 0.5 && b < 0.5 && c < 0.5, "CHORUS's dots where the stage's heads are: the echo of a click where the tap's phase "
                                                  "puts it — steady " + juce::String (a, 3) + " samples, the rate moved mid-way "
                                                  + juce::String (b, 3) + ", woken from idle " + juce::String (c, 3));
        }

        // ...AND THE PICTURE DRAWS THE TAP: its dots at the newest phase — the left at 2π·phase, the right a quarter turn on —
        // on the radius the slider gives (the law's swing over its swing at the top), in its block 200 wide; and nothing
        // is written while it is off screen, a hidden block included, nor while the stage idles — yet it runs in silence.
        {
            auto t = make (bass);
            set (*t, params::chorusAmount, 0.6f);
            set (*t, params::chorusRateHz, 1.37f);
            std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
            auto* block = findIn (*ed, (ChorusBlock*) nullptr);
            juce::AudioBuffer<float> buf (2, 480);   // two hops a call: the newest entry is the call's end
            juce::MidiBuffer midi;
            const auto run = [&] (int calls)
            {
                for (int k = 0; k < calls; ++k)
                {
                    buf.clear();   // silence: a chorus runs on without a note
                    t->processBlock (buf, midi);
                }
            };
            run (40);
            const auto hidden = t->chorusTap.written();
            bool drawn = false;
            juce::String said = "no CHORUS block";
            if (block != nullptr)
            {
                auto& pic = block->picture();
                t->chorusWatched.store (true);   // as on screen
                run (60);
                pic.pull();
                const float want = (float) t->chorusTap.phase();
                const auto  o = pic.centre();
                const float rr = pic.reach() * pic.radiusFraction();
                const auto angle = [&] (juce::Point<float> d) { return std::atan2 ((double) (o.y - d.y), (double) (d.x - o.x)); };
                const auto turn  = [] (double x) { x = std::fmod (x, juce::MathConstants<double>::twoPi); return std::min (std::abs (x), juce::MathConstants<double>::twoPi - std::abs (x)); };
                const double offL = turn (angle (pic.dotAt (0)) - juce::MathConstants<double>::twoPi * (double) want);
                const double offR = turn (angle (pic.dotAt (1)) - angle (pic.dotAt (0)) - juce::MathConstants<double>::halfPi);
                const float  lawFrac = felitronics::chorus::paramsForAmount (0.6f).depthMs / felitronics::chorus::kDepthMsAtFull;
                drawn = block->getWidth() == FaceplateView::chorusWidth && pic.voices() == t->bassBackPlanes()
                        && std::abs (pic.shownPhase() - want) < 1.0e-6f && offL < 1.0e-4 && offR < 1.0e-4
                        && std::abs (pic.radiusFraction() - lawFrac) < 1.0e-6f && std::abs (lawFrac - 0.6f) < 1.0e-6f
                        && std::abs (pic.dotAt (0).getDistanceFrom (o) - rr) < 0.01f && rr > 10.0f;
                said = "block " + juce::String (block->getWidth()) + " px, " + juce::String (pic.voices()) + " voices, phase "
                       + juce::String (pic.shownPhase(), 4) + " (the tap's " + juce::String (want, 4) + "), off " + juce::String (offL, 6)
                       + " and a quarter turn off by " + juce::String (offR, 6) + " rad, radius " + juce::String (pic.radiusFraction(), 3)
                       + " of the reach (" + juce::String (rr, 1) + " px)";
            }
            check (drawn, "CHORUS's picture draws the tap: " + said);

            const auto shownW = t->chorusTap.written();
            set (*t, params::chorusAmount, 0.0f);
            run (20);   // the voice fades, the stage idles
            const auto idleW = t->chorusTap.written();
            run (60);
            const auto stillW = t->chorusTap.written();
            set (*t, params::chorusAmount, 0.6f);
            if (block != nullptr)
            {
                block->setVisible (false);    // its block hidden: the picture is not on screen, whatever its own flag says
                block->picture().look();      // ...as its timer's next tick finds
            }
            run (60);
            const auto hiddenW = t->chorusTap.written();
            check (hidden == 0 && shownW > 100 && stillW == idleW && hiddenW == stillW && ! t->chorusWatched.load(),
                   "CHORUS's taps: nothing written off screen (" + juce::String ((int) hidden) + "), " + juce::String ((int) shownW)
                       + " hops shown in silence, none more once idle (" + juce::String ((int) (stillW - idleW)) + ") and none with its block hidden ("
                       + juce::String ((int) (hiddenW - stillW)) + ")");
        }

        // ITS RING READ WHOLE OR NOT AT ALL: a writer lapping the reader as fast as it can, the reader taking the whole
        // ring — every entry it takes whole is its own number's, never its successor's: two in a row differ by one.
        {
            core::ChorusTap tap;
            std::atomic<bool> stop { false };
            std::thread writer ([&]
            {
                for (std::uint32_t k = 0; ! stop.load (std::memory_order_relaxed); ++k)
                    tap.pushForTest ((float) (k & 0xFFFFFu), true);
            });
            std::array<core::ChorusTap::Entry, core::ChorusTap::slots> got {};
            long long whole = 0, dropped = 0, mixed = 0;
            const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds (150);
            while (std::chrono::steady_clock::now() < until)
            {
                const int have = tap.read (got.data(), (int) got.size());
                for (int i = 0; i < have; ++i)
                {
                    (got[(size_t) i].ok ? whole : dropped)++;
                    if (i > 0 && got[(size_t) i].ok && got[(size_t) i - 1].ok)
                    {
                        const auto a = (std::uint32_t) got[(size_t) i - 1].phase, b = (std::uint32_t) got[(size_t) i].phase;
                        mixed += ((a + 1u) & 0xFFFFFu) != b ? 1 : 0;
                    }
                }
            }
            stop.store (true);
            writer.join();
            check (mixed == 0 && whole > 1000 && dropped > 0,
                   "CHORUS's ring under a writer lapping it: " + juce::String (whole) + " entries taken whole, " + juce::String (dropped)
                       + " lapped and dropped, " + juce::String (mixed) + " mixed with another's");
        }

        // ITS NUMBERS ARE FIELDS: the RATE and the CROSSOVER typed in land on their parameters, each one step of the
        // history, undone whole.
        {
            auto t = make (bass);
            std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
            auto* block = findIn (*ed, (ChorusBlock*) nullptr);
            const auto settle = [&] { for (int i = 0; i < 40; ++i) t->history.tick(); };
            juce::String said = "no CHORUS block";
            bool ok = false;
            if (block != nullptr)
            {
                auto& pic = block->picture();
                ok = true;
                said.clear();
                for (const auto& [f, id, typed, want] : { std::tuple { ChorusPicture::Field::rate, params::chorusRateHz, "1.35 Hz", 1.35f },
                                                          std::tuple { ChorusPicture::Field::crossover, params::chorusCrossoverHz, "310", 310.0f } })
                {
                    settle();
                    const float before = value (*t, id);
                    const int   depth  = t->history.undoDepth();
                    pic.openField (f);
                    const bool  opened = pic.fieldEditor().isOpen();
                    pic.fieldEditor().ed.setText (typed, false);
                    pic.fieldEditor().takeAndHide();
                    settle();
                    const float landed = value (*t, id);
                    const int   steps  = t->history.undoDepth() - depth;
                    t->history.undo();
                    settle();
                    const float back = value (*t, id);
                    ok = ok && opened && std::abs (landed - want) < 1.0e-3f && steps == 1 && std::abs (back - before) < 1.0e-3f;
                    said << id << " " << juce::String (before, 2) << " -> typed " << typed << " -> " << juce::String (landed, 2) << " in "
                         << steps << " step(s), undone to " << juce::String (back, 2) << "; ";
                }
            }
            check (ok, "CHORUS's numbers are fields: " + said);
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

    // A GAP — a call of positive length that carries no channel (law 11c) — reaches the three: FILTER's follower lets go
    // through it as through silence, its sweep falling back as far. Last, for an amp that falls over on one.
    {
        const auto after = [&] (bool gap)
        {
            auto t = make (bass);
            set (*t, params::cabOn, 0.0f);
            set (*t, params::filterAmount, 0.8f);
            (void) tone (*t, 110.0, -6.0f, 60);
            const double loud = t->filterShownCutoffHz();
            if (gap)
            {
                juce::AudioBuffer<float> none (0, blockSize);
                juce::MidiBuffer midi;
                for (int b = 0; b < 40; ++b)
                    t->processBlock (none, midi);
            }
            else
            {
                (void) tone (*t, 110.0, -6.0f, 40, -1, {}, 0);
            }
            return std::pair<double, double> { loud, (double) t->filterShownCutoffHz() };
        };
        const auto [loud, viaGap] = after (true);
        const auto silent = after (false).second;
        check (viaGap < 0.8 * loud && std::abs (viaGap - silent) <= 1.0e-3 * silent,
               "a gap of 40 blocks lets FILTER's sweep fall as silence does: " + juce::String (loud, 0) + " Hz to " + juce::String (viaGap, 0)
                   + " (silence: " + juce::String (silent, 0) + ")");
    }

    std::printf ("%s\n", failed == 0 ? "PASSED" : "FAILED");
    return failed == 0 ? 0 : 1;
}
