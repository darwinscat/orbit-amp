// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Gate for ORBITBASS'S CHAIN (src/Product.h, Chain::bass), headless, run as OrbitBass:
//
//   where a fresh one stands — the gate off, COMP on at BLEND half, the AMP's BLEND 0 at 150 Hz, DI/CAB 30 % DI, its own HPF at
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
#include "core/FilterTap.h"
#include "device/DeviceLibrary.h"
#include "ui/LoadBreakdown.h"
#include "ui/GuardBlock.h"
#include "ui/CompBlock.h"
#include "ui/OctaveScope.h"
#include "core/OctaveTap.h"
#include "ui/FilterBlock.h"
#include <felitronics/envfilter/EnvelopeFilterLaw.h>
#include "ui/ChorusBlock.h"
#include "ui/SpaceBlocks.h"
#include "core/SpaceTap.h"
#include "core/ChorusTap.h"
#include "ui/FaceplateView.h"
#include "ui/GuardRail.h"
#include "ui/EqSection.h"
#include "ui/CapturedBlockPanel.h"
#include "ui/BassControls.h"
#include "ui/Selector.h"
#include "ui/VoicingSelector.h"
#include "channels/Engine.h"
#include "channels/Presets.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <thread>
#include <limits>
#include <map>
#include <string>
#include <tuple>
#include <type_traits>
#include <typeinfo>

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

    Out play (AmpProcessor& amp, int blocks, bool wide, bool throughGuitarCut = false)
    {
        Out out;
        juce::AudioBuffer<float> buf (2, blockSize);
        // `throughGuitarCut`: the phrase through the guitar amps' input cut (params::guitarInputCutHz) on its way in — what
        // every guitar chain does first — so the bass can be held against a guitar amp set as it is, bit for bit.
        core::BassCuts guitarCut;
        guitarCut.prepare (sampleRate);
        core::BassCuts::Settings cutAt;
        cutAt.hpfOn = true;
        cutAt.hpfHz = params::guitarInputCutHz;
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
            if (throughGuitarCut)
                guitarCut.process (buf.getArrayOfWritePointers(), 2, blockSize, cutAt);
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
        float allowed  = 1.0e-6f;
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
        // gcc read 6.7e-8 off the recording compiler. Since 02.11 the bass's AMP console is the bass's standard EQ (its MIDDLE
        // held to its span, the second bell and the scalpel out of its sound: AmpProcessor::updateEqSettings), whose stages at
        // 0 dB are not the step's to the last bit: 1.28e-6 measured on Apple clang here, with that one branch taken out 0.
        // Within 5e-6 (-106 dB) on every compiler.
        ref.allowed = 5.0e-6f;
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

    // STILLS FOR A LOOK, a dev tool (off unless ORBITBASS_STILLS names a folder): OrbitAmp's face as it opens, at the least
    // width and at 16:9, for a byte compare across a change that must leave the guitar alone.
    if (const auto stills = juce::SystemStats::getEnvironmentVariable ("ORBITBASS_STILLS", {}); stills.isNotEmpty())
    {
        const juce::File dir (stills);
        dir.createDirectory();
        for (const auto* g : { &products::orbitAmp(), &products::catpitAmp() })
        {
            auto t = make (*g);
            std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
            const int h = ed->getHeight();
            for (const int w : { ed->getWidth(), juce::roundToInt ((float) h * 16.0f / 9.0f) })
            {
                ed->setSize (w, h);
                juce::PNGImageFormat png;
                juce::FileOutputStream os (dir.getChildFile (juce::String (g->name) + "-" + juce::String (w) + ".png"));
                if (os.openedOk())
                {
                    os.setPosition (0);
                    os.truncate();
                    png.writeImageToStream (ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f), os);
                }
            }
        }
    }

    const Product& bass = ORBITAMP_GATE_PRODUCT();
    check (juce::String (bass.name) == "OrbitBass" && bass.chain == Chain::bass, "OrbitBass plays the bass's chain");

    // ---- WHERE A FRESH ONE STANDS ------------------------------------------------------------------
    auto amp = make (bass);
    {
        auto& a = *amp;
        check (declared (a, params::gateOn) < 0.5f && value (a, params::gateOn) < 0.5f, "the gate is off");
        check (value (a, params::bassHpfOn) > 0.5f && juce::roundToInt (value (a, params::bassHpf)) == (int) params::BassHpf::five
                   && juce::approximatelyEqual (value (a, params::bassHpfHz5), 22.0f)
                   && juce::approximatelyEqual (value (a, params::bassHpfHz4), 32.0f),
               "its HPF at 5, 22 Hz (4 at 32 Hz)");
        check (a.apvts.getParameter (params::bassHpfHz5)->getNormalisableRange().start <= 20.0f,
               "...in a range that reaches 20 Hz");
        check (value (a, params::bassLpfOn) < 0.5f && juce::roundToInt (value (a, params::bassLpf)) == (int) params::BassLpf::fingers
                   && juce::approximatelyEqual (value (a, params::bassLpfHzFingers), 4500.0f)
                   && juce::approximatelyEqual (value (a, params::bassLpfHzSlap), 9500.0f),
               "its LPF off (FINGERS at 4.5 kHz, SLAP at 9.5 kHz)");
        check (value (a, params::cabHpfOn) < 0.5f && value (a, params::cabLpfOn) < 0.5f,
               "the cabinet's own cuts off: its IR plays as it was shot");
        check (value (a, params::compOn) > 0.5f && juce::approximatelyEqual (value (a, params::compAmount), 0.5f)
                   && juce::roundToInt (value (a, params::compAttack)) == 1 && juce::roundToInt (value (a, params::compRelease)) == 0
                   && juce::roundToInt (value (a, params::compCharacter)) == params::compCharacters.indexOf ("Opto"),
               "COMP on, BLEND at half, MED attack, AUTO release, OPTO");
        check (juce::approximatelyEqual (value (a, params::ampBlend), 0.0f) && juce::approximatelyEqual (value (a, params::ampBlendHz), 150.0f)
                   && value (a, params::ampBlendInvert) < 0.5f,
               "BLEND 0, its crossover at 150 Hz, not inverted");
        check (juce::approximatelyEqual (value (a, params::diMix), 30.0f), "DI/CAB 30 % DI");
        check (declared (a, params::delayPresent) < 0.5f && declared (a, params::reverbPresent) < 0.5f
                   && value (a, params::delayPresent) < 0.5f && value (a, params::reverbPresent) < 0.5f
                   && value (a, params::delayOn) > 0.5f && value (a, params::reverbOn) > 0.5f
                   && ! a.linkInRig (params::rowDelay) && ! a.linkInRig (params::rowReverb),
               "DELAY and REVERB out of the strip (their switches on, so one added plays at once)");
        check (value (a, params::octavePresent) > 0.5f && value (a, params::filterPresent) > 0.5f && value (a, params::chorusPresent) > 0.5f
                   && a.linkInRig (params::rowOctave) && a.linkInRig (params::rowFilter) && a.linkInRig (params::rowChorus),
               "OCTAVE, ENVELOPE and CHORUS in the strip");
        check (juce::roundToInt (declared (a, params::stereoMode)) == (int) params::StereoMode::mono
                   && juce::roundToInt (value (a, params::stereoMode)) == (int) params::StereoMode::mono,
               "mono");
        check (value (a, params::preampOn) > 0.5f && value (a, params::cabOn) > 0.5f && value (a, params::limiterOn) > 0.5f,
               "the amp, the cabinet and the limiter on");

        const auto packs = device::DeviceLibrary::scan (bass.name, device::DeviceLibrary::ampSlot);
        check (packs.size() == 1 && packs.getReference (0).rigId == "revolt-clean-poweramp-bf62"
                   && packs.getReference (0).bundled && juce::roundToInt (value (a, params::preampDevice)) == 0,
               "it plays its one temporary pack, the ReVolt Clean (" + juce::String (packs.size()) + " in its list)");
        check (a.getTailLengthSeconds() >= 8.0 && a.getTailLengthSeconds() <= 30.0,
               "its tail counts the echo and the room, which a player may add while a host keeps the first answer ("
                   + juce::String (a.getTailLengthSeconds(), 2) + " s)");
    }

    // ---- ITS STRIP -----------------------------------------------------------------------------------
    auto guitar = make (products::orbitAmp());
    check (arrows (AmpEditor::stripRows (*amp)) == "GATE > COMP > OCTAVE > ENVELOPE > AMP > CHORUS > CAB IR > LIMIT",
           "OrbitBass's strip: " + arrows (AmpEditor::stripRows (*amp)));
    check (arrows (AmpEditor::stripRows (*guitar)) == "GATE > AMP > DELAY > REVERB > CAB IR > LIMIT",
           "OrbitAmp's, as it was: " + arrows (AmpEditor::stripRows (*guitar)));
    {
        // OrbitAmp's COMP (the owner, 02.11): the bass's, its parameters the same, out of the strip until the player adds
        // it, on OPTO; none of the bass's other links or parameters. The cockpit's face has no COMP at all.
        bool none = ! guitar->chainHolds (params::rowOctave) && ! guitar->chainHolds (params::rowFilter) && ! guitar->chainHolds (params::rowChorus);
        for (const char* id : { params::ampBlend, params::diMix, params::bassHpf, params::bassLpf,
                                params::octaveAmount, params::filterAmount, params::chorusAmount })
            none = none && guitar->apvts.getParameter (id) == nullptr;
        bool comp = guitar->chainHolds (params::rowComp) && guitar->linkRemovable (params::rowComp) && ! guitar->linkShown (params::rowComp);
        for (const char* id : { params::compOn, params::compAmount, params::compAttack, params::compRelease, params::compCharacter,
                                params::compThreshold, params::compRatio, params::compPresent })
            comp = comp && guitar->apvts.getParameter (id) != nullptr;
        comp = comp && juce::roundToInt (value (*guitar, params::compCharacter)) == params::compCharacters.indexOf ("Opto")
                    && value (*guitar, params::compPresent) < 0.5f;
        auto cockpit = make (products::catpitAmp());
        const bool cockpitNone = ! cockpit->chainHolds (params::rowComp) && cockpit->apvts.getParameter (params::compAmount) == nullptr;
        check (none && comp && cockpitNone,
               "OrbitAmp has a COMP — the bass's parameters, out of its strip, on OPTO — and none of the bass's others; the cockpit none");
    }

    // ---- ITS SOUND -----------------------------------------------------------------------------------
    constexpr int blocks = 250;   // 2.7 s: the phrase, and over a second of what rings after it
    const auto stepTwoB = stepTwoBReference();
    const auto plain = play (*amp, blocks, false);
    check (peak (plain.l) > 1.0e-3f, "it sounds (peak " + juce::String (peak (plain.l), 4) + ")");

    // The delay and the reverb OUT OF THE STRIP, switched on and all wet: nothing changes, bit for bit.
    {
        auto forced = make (bass);
        set (*forced, params::delayOn, 1.0f);
        set (*forced, params::reverbOn, 1.0f);
        set (*forced, params::delayMix, 100.0f);
        set (*forced, params::reverbMix, 100.0f);
        const auto heard = play (*forced, blocks, false);
        check (worstApart (heard.l, plain.l) == 0.0f && worstApart (heard.r, plain.r) == 0.0f,
               "the delay and the reverb out of the strip, on and all wet, change nothing, bit for bit (worst "
                   + juce::String (worstApart (heard.l, plain.l), 6) + ")");
    }

    // COMP standing by with its knob up — still fed, its detector kept warm — plays exactly what COMP at 0 plays:
    // nothing of it is in the sound; and switched in, it is heard.
    {
        auto off = make (bass);
        set (*off, params::compAmount, 0.6f);
        set (*off, params::compOn, 0.0f);
        off->prepareToPlay (sampleRate, blockSize);   // standing by from the start (a fresh one's BLEND is up: its link fades)
        off->pumpDeviceWork();
        const auto heard = play (*off, blocks, false);
        auto zero = make (bass);
        set (*zero, params::compAmount, 0.0f);
        zero->prepareToPlay (sampleRate, blockSize);
        zero->pumpDeviceWork();
        const auto none = play (*zero, blocks, false);
        check (worstApart (heard.l, none.l) == 0.0f, "COMP standing by at 0.6 plays as COMP at 0, bit for bit");

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

    // ---- ORBITAMP'S COMP (the owner, 02.11): the bass's, before the AMP; out of the strip it changes nothing, bit for bit,
    // whatever its BLEND; added, it squeezes — at STEREO both sides, the right through its own twin ----------------------
    {
        constexpr int blocks = 120;
        const auto run = [&] (bool present, float blend, bool stereo)
        {
            auto g = make (products::orbitAmp());
            set (*g, params::stereoMode, (float) (stereo ? params::StereoMode::stereo : params::StereoMode::mono));
            set (*g, params::compPresent, present ? 1.0f : 0.0f);
            set (*g, params::compAmount, blend);
            set (*g, params::compThreshold, -36.0f);
            g->prepareToPlay (sampleRate, blockSize);
            g->pumpDeviceWork();
            return play (*g, blocks, stereo);
        };
        const auto outFull = run (false, 1.0f, false), outNone = run (false, 0.0f, false), inFull = run (true, 1.0f, false);
        const auto wideOut = run (false, 1.0f, true), wideIn = run (true, 1.0f, true);
        const float standing = worstApart (outFull.l, outNone.l), heard = worstApart (inFull.l, outFull.l);
        const float left = worstApart (wideIn.l, wideOut.l), right = worstApart (wideIn.r, wideOut.r);
        check (standing == 0.0f && heard > 1.0e-3f && left > 1.0e-3f && right > 1.0e-3f && worstApart (wideIn.l, wideIn.r) > 1.0e-3f,
               "OrbitAmp's COMP: out of the strip at BLEND 100 % plays as at 0, bit for bit (" + juce::String (standing, 9)
                   + "); added, heard (" + juce::String (heard, 4) + "); at STEREO both sides squeezed (left " + juce::String (left, 4)
                   + ", right " + juce::String (right, 4) + "), still two");
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
            // The bass's DELAY and REVERB are out of its strip — which is why they are not in its sound: the OrbitAmp's are
            // switched off to match, the one difference the two may have (what each plays, below, says whether they are).
            const bool bassWithout = ! from.linkInRig (params::rowDelay) && ! from.linkInRig (params::rowReverb);
            set (*same, params::delayOn, 0.0f);
            set (*same, params::reverbOn, 0.0f);
            apart.removeString (params::delayOn);
            apart.removeString (params::reverbOn);
            check (apart.isEmpty() && bassWithout,
                   what + ": an OrbitAmp set as OrbitBass is, its delay and reverb off as the bass's are out of its strip ("
                       + (bassWithout ? "they are" : "they are NOT") + ")"
                       + (apart.isEmpty() ? juce::String() : " — apart: " + apart.joinIntoString (", ")));

            same->prepareToPlay (sampleRate, blockSize);
            same->pumpDeviceWork();
            return worstApart (play (*same, blocks, false).l, heard.l);
        };

        // OrbitBass as at step 1: COMP at 0, BLEND 0, its HPF off, the DI's share none.
        const auto asBefore = [&]
        {
            auto b = make (bass);
            set (*b, params::bassHpfOn, 0.0f);
            set (*b, params::diMix, 0.0f);
            set (*b, params::compAmount, 0.0f);   // COMP's BLEND at 0: no compressor, as at step 1
            return b;
        };

        // The cabinets out: OrbitAmp's links less the delay and the reverb, bit for bit — BLEND 0 is the
        // amp without it, COMP 0 no compressor, the cuts off a wire.
        {
            auto dry = asBefore();
            set (*dry, params::cabOn, 0.0f);
            const auto heard = play (*dry, blocks, false);
            // The guitar's one difference is its input cut (the owner, 02.11): always on its chain, never on the bass's —
            // the bass fed the phrase through the same cut plays what the guitar amp plays, bit for bit.
            auto dryCut = asBefore();
            set (*dryCut, params::cabOn, 0.0f);
            const auto heardCut = play (*dryCut, blocks, false, true);
            // ...within 5e-6: the two consoles are each its chain's standard EQ (02.11), at 0 dB not the same to the last bit
            // (4.1e-6 measured; 0 with the bass's standard's branch taken out of AmpProcessor::updateEqSettings).
            const float worst = twin (*dry, heardCut, "the cabinets out");
            const float uncut = worstApart (heard.l, heardCut.l);
            check (worst <= 5.0e-6f && uncut > 1.0e-4f,
                   "...plays what OrbitBass plays at BLEND 0 through the guitar's 50 Hz input cut, within 5e-6 (worst apart "
                       + juce::String (worst, 9) + "), the cut heard (" + juce::String (uncut, 6) + " from the bass uncut)");

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
            const auto heard = play (*cabOnly, blocks, false, true);   // through the guitar's input cut, as above
            const float worst = twin (*cabOnly, heard, "the cabinets in, DI/CAB 0 %");
            check (worst <= 5.0e-6f,   // each chain's standard EQ, as above
                   "...plays what OrbitBass plays through the guitar's input cut, the cabinet alone (worst apart " + juce::String (worst, 9) + " of peak " + juce::String (peak (heard.l), 4) + ")");
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
            set (*t, params::compAmount, 0.0f);
            set (*t, params::bassHpfOn, 0.0f);
            const bool hp = juce::String (id) == params::bassHpf;
            set (*t, hp ? params::bassHpfOn : params::bassLpfOn, position < 0 ? 0.0f : 1.0f);
            if (position >= 0)
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
        const double hOff = level (26.0f, params::bassHpf, -1);
        const double h5   = level (26.0f, params::bassHpf, (int) params::BassHpf::five);
        const double h4   = level (26.0f, params::bassHpf, (int) params::BassHpf::four);
        check (hOff > h5 + 0.5 && h5 > h4 + 2.0,
               "HPF on a 26 Hz tone: off " + juce::String (hOff, 1) + ", 5 " + juce::String (h5, 1) + ", 4 " + juce::String (h4, 1) + " dB");
        const double lOff = level (6000.0f, params::bassLpf, -1);
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

        // EACH CONTROL ONE JOB (the owner, 31.10): the threshold its own (-48..0 dB, -24 by default), the ratio its own
        // (1.5:1..10:1 since 01.11, 4:1), the knob BLEND (0..1, a half) — the defaults the processor's and the stage's alike.
        struct Curve { double thresholdDb, ratio, uncompressed; };
        const Curve top { BassComp::defaultThresholdDb, BassComp::defaultRatio, 0.0 };
        {
            auto* thr = amp->apvts.getParameter (params::compThreshold);
            auto* rat = amp->apvts.getParameter (params::compRatio);
            check (thr != nullptr && rat != nullptr && thr->getNormalisableRange().start == -48.0f && thr->getNormalisableRange().end == 0.0f
                       && value (*amp, params::compThreshold) == (float) BassComp::defaultThresholdDb && value (*amp, params::compRatio) == (float) BassComp::defaultRatio
                       && rat->getNormalisableRange().start == 1.5f && rat->getNormalisableRange().end == 10.0f && value (*amp, params::compAmount) == 0.5f
                       && BassComp::ratioMin == 1.5 && BassComp::ratioMax == 10.0,
                   "COMP's three: THRESHOLD -48..0 dB at -24, RATIO 1.5:1..10:1 at 4:1 (the parameter's and the stage's), BLEND at half");
        }

        // BLEND IS PARALLEL COMPRESSION, SAMPLE-ALIGNED: at half, what comes out is the dry and the all-compressed summed
        // half and half, sample for sample; and the all-compressed IS the dry times core's own squeeze and the make-up at
        // the same sample — no lookahead, no latency between the two.
        {
            struct Run { std::vector<float> out, gr, mk; };
            const auto runAt = [] (double blend)
            {
                Run r;
                BassComp c;
                (void) c.prepare (sampleRate, blockSize);
                c.snapAmount (blend);
                core::BypassFade link;
                link.prepare (sampleRate);
                link.snapTo (blend > 0.0);
                std::vector<float> x ((size_t) blockSize);
                long long n = 0;
                for (int b = 0; b < 100; ++b)
                {
                    for (int i = 0; i < blockSize; ++i, ++n)
                        x[(size_t) i] = (float) (0.5 * std::exp (-2.0 * std::fmod ((double) n / sampleRate, 0.6))
                                                 * std::sin (juce::MathConstants<double>::twoPi * 97.0 * (double) n / sampleRate));
                    // RELEASE SLOW: its make-up one number once snapped (AUTO's follows the hold, a glide within the block).
                    c.process (x.data(), blockSize, blend, link.advance (blockSize, blend > 0.0),
                               BassComp::Attack::med, BassComp::Release::slow, BassComp::Character::smooth);
                    r.out.insert (r.out.end(), x.begin(), x.end());
                    r.gr.insert (r.gr.end(), c.squeezeDb(), c.squeezeDb() + blockSize);
                    r.mk.insert (r.mk.end(), (size_t) blockSize, (float) c.makeupNow());   // snapped, its knobs still: one number
                }
                return r;
            };
            const auto dryRun = runAt (0.0), allRun = runAt (1.0), halfRun = runAt (0.5);
            const auto& dry = dryRun.out;
            const auto& all = allRun.out;
            const auto& half = halfRun.out;
            double worst = dry.size() == half.size() && dry.size() == all.size() ? 0.0 : 1.0e9, apart = 0.0, law = worst, mk = 0.0;
            for (size_t i = 0; i < half.size() && i < all.size() && i < dry.size(); ++i)
            {
                worst = std::max (worst, std::abs ((double) half[i] - ((double) dry[i] + 0.5 * ((double) all[i] - (double) dry[i]))));
                apart = std::max (apart, std::abs ((double) all[i] - (double) dry[i]));
                law   = std::max (law, std::abs ((double) all[i] - (double) dry[i] * std::pow (10.0, ((double) allRun.gr[i] + (double) allRun.mk[i]) / 20.0)));
                mk    = (double) allRun.mk[i];
            }
            check (worst < 1.0e-5 && law < 1.0e-5 && apart > 0.01,
                   "COMP's BLEND at half: the dry and the compressed summed sample for sample (worst " + juce::String (worst, 9)
                       + "); the compressed the dry times core's squeeze and the make-up (" + juce::String (mk, 2) + " dB) at the same sample (worst "
                       + juce::String (law, 9) + "; apart from the dry by " + juce::String (apart, 3) + ")");
        }

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

        const double at120 = squeeze (120.0, 1.0), want = (-6.0 - top.thresholdDb) * (1.0 / top.ratio - 1.0);   // the defaults: -24 dB, 4:1
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
            set (*t, params::bassHpfOn, 0.0f);
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
            const auto v = tone (*t, 26.0, -12.0f, len, at, [&] { set (*t, params::bassHpfOn, 0.0f); });
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
            set (*t, params::bassLpfOn, 1.0f);
            set (*t, params::bassLpf, (float) params::BassLpf::fingers);
            constexpr int off = 60, on = 140;
            const auto v = tone (*t, 3000.0, -6.0f, 160, off, [&] { set (*t, params::bassLpfOn, 0.0f); }, off);
            // ...the second move, by hand: run on from where the first left off.
            set (*t, params::bassLpfOn, 1.0f);
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
        check (amp->getTailLengthSeconds() >= 8.0,
               "the bass's tail is at least the cabinet's longest, 5 s, and the echo's floor, 8 (" + juce::String (amp->getTailLengthSeconds(), 2) + " s)");
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
            set (*moved, params::octaveToneHz, 400.0f);
            set (*moved, params::filterMode, 1.0f);
            set (*moved, params::filterResonance, 12.0f);
            set (*moved, params::filterKeepLows, 0.0f);
            set (*moved, params::chorusRateHz, 2.5f);
            set (*moved, params::chorusCrossoverHz, 500.0f);
            // The HPF's inner cuts are the chain's since 31.10 and not step 2b's: out here, measured on their own below.
            moved->innerCutsOut.store (true);
            set (*moved, params::compAmount, 0.0f);   // COMP at 0 as at step 2b (its BLEND is half by default since 31.10)
            auto freshAmp = make (bass);
            freshAmp->innerCutsOut.store (true);
            set (*freshAmp, params::compAmount, 0.0f);
            const auto heard = play (*moved, stepTwoB.blocks, false);
            const auto fresh = play (*freshAmp, stepTwoB.blocks, false);
            const float apartL = worstApart (heard.l, stepTwoB.was), apartR = worstApart (heard.r, stepTwoB.was);
            const float apartPlain = worstApart (fresh.l, stepTwoB.was);
            check (stepTwoB.was.size() == (size_t) (stepTwoB.blocks * blockSize) && apartL <= stepTwoB.allowed
                       && apartR <= stepTwoB.allowed && apartPlain <= stepTwoB.allowed,
                   "the three at 0, their menus moved — and at their defaults: what step 2b (78e037a) played, "
                       + juce::String ("within 5e-6") + ", both sides (worst "
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
                set (*t, params::bassHpfOn, 0.0f);
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
                set (*t, params::bassHpfOn, 0.0f);
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

        // A MENU FIELD OR A SIDE RAIL REACHES ITS MODULE with the slider up: LP against BP, OCTAVE's TONE, CLEAN LOWS.
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
            const float tone = apart (params::octaveAmount, params::octaveToneHz, 60.0f, 400.0f);
            const float keep = apart (params::filterAmount, params::filterKeepLows, 1.0f, 0.0f);
            check (mode > 1.0e-3f && tone > 1.0e-3f && keep > 1.0e-3f,
                   "the menus and the side rails reach the modules, sliders up: FILTER LP/BP apart by " + juce::String (mode, 4)
                       + ", OCTAVE's TONE 60/400 Hz by " + juce::String (tone, 4) + ", CLEAN LOWS by " + juce::String (keep, 4));
        }

        // THE ORDER: OCTAVE before FILTER and the AMP. FILTER at the top on BP, its lows not kept, sweeps the sub with the
        // note: 41 Hz under an E2 falls against the note's 82 Hz — had the sub come after FILTER (or after the AMP, past
        // it) the band would not touch it.
        {
            const auto subOverNote = [&] (bool bp)
            {
                auto t = make (bass);
                set (*t, params::cabOn, 0.0f);
                set (*t, params::bassHpfOn, 0.0f);
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

        // THE LINE IS THE THRESHOLD: dragged across every pixel row of its travel it sets the threshold where the line is,
        // drawn back where the hand is — one gesture for the whole drag; the threshold typed, the line follows.
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
            auto* amountP = t->apvts.getParameter (params::compThreshold);
            float worstPx = 1.0e9f;
            int rows = 0;
            if (graph != nullptr)
            {
                amountP->addListener (&gestures);
                worstPx = 0.0f;
                graph->beginLineDrag();
                for (float y = std::ceil (graph->yOfDb (-2.0)); y <= std::floor (graph->yOfDb (-46.0)); y += 1.0f, ++rows)
                {
                    graph->dragLineTo (y);
                    worstPx = std::max (worstPx, std::abs (graph->yOfDb (graph->thresholdDb()) - y));
                }
                graph->endLineDrag();
                amountP->removeListener (&gestures);
                set (*t, params::compThreshold, -24.0f);
            }
            check (graph != nullptr && block != nullptr && block->getWidth() == FaceplateView::compWidth && rows > 20 && worstPx <= 0.5f
                       && gestures.begun == 1 && gestures.ended == 1 && std::abs (graph->thresholdDb() + 24.0) < 1.0e-3,
                   "the line dragged over " + juce::String (rows) + " pixel rows: the threshold where the line says, the line back within "
                       + juce::String (worstPx, 3) + " px, " + juce::String (gestures.begun) + " gesture; the threshold set to -24, the line at "
                       + juce::String (graph != nullptr ? graph->thresholdDb() : 0.0, 2) + " dB");
        }

        // THE RATIO'S TWO CONTROLS: comp_ratio runs 1.5:1 .. 10:1; the combo over the square and the square show it; 7:1
        // picked from the combo's list holds 7:1 — the square with it, one gesture; the square dragged to 3.3:1's height
        // steps to the list's nearest, 3:1, and the combo follows, ticking it; set from elsewhere, both follow.
        {
            auto t = make (bass);
            set (*t, params::compAmount, 0.5f);
            std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
            auto* combo = findIn (*ed, (CompRatioCombo*) nullptr);
            auto* square = findIn (*ed, (CompTransfer*) nullptr);
            const auto range = t->apvts.getParameter (params::compRatio)->getNormalisableRange();
            struct Gestures final : juce::AudioProcessorParameter::Listener
            {
                int begun = 0, ended = 0;
                void parameterValueChanged (int, float) override {}
                void parameterGestureChanged (int, bool starting) override { ++(starting ? begun : ended); }
            } gestures;
            bool shown = false, heldSeven = false, followed = false, back = false;
            double atSeven = 0.0, atThree = 0.0;
            juce::String said;
            if (combo != nullptr && square != nullptr)
            {
                const double law = (double) value (*t, params::compRatio);   // the ratio, its own parameter
                shown = combo->shownRatio() == law && square->shownRatio() == law && combo->text() == "4:1";
                auto* ratioP = t->apvts.getParameter (params::compRatio);
                ratioP->addListener (&gestures);
                std::function<void (int)> answer;
                onfirst::catcher() = [&] (const juce::PopupMenu&, const std::function<void (int)>& a) { answer = a; return true; };
                combo->openList();
                onfirst::catcher() = nullptr;
                if (answer != nullptr)
                    answer (6);   // the sixth item, 7:1
                ratioP->removeListener (&gestures);
                atSeven = combo->shownRatio();
                heldSeven = atSeven == 7.0 && square->shownRatio() == atSeven && combo->text() == "7:1";
                square->beginRatioDrag();
                square->dragRatioTo (square->yOfRatio (3.3));
                square->endRatioDrag();
                atThree  = square->shownRatio();
                followed = atThree == 3.0 && combo->shownRatio() == atThree && combo->tickedItem() == 2 && combo->text() == "3:1";
                said = combo->text();
                set (*t, params::compRatio, (float) law);
                back = std::abs (combo->shownRatio() - law) < 1.0e-6 && std::abs (square->shownRatio() - law) < 1.0e-6;
            }
            check (range.start == 1.5f && range.end == 10.0f && shown && heldSeven && gestures.begun == 1 && gestures.ended == 1
                       && followed && back,
                   "COMP's ratio, 1.5:1 .. 10:1, its own: the combo and the square show it; 7:1 picked from the list sets "
                       + juce::String (atSeven, 2) + ":1 in " + juce::String (gestures.begun) + " gesture, the square with it; the square dragged to 3.3's height steps to "
                       + juce::String (atThree, 2) + ":1, the combo reading \"" + said + "\", ticked; set from elsewhere, both follow");
        }

        // THE RAILS' NUMBERS ARE TYPED, THE RATIO IS PICKED (the owner, 01.11): the threshold on its rail's grip (a dB,
        // clamped to its travel) and the fader's amount are fields, each one gesture; the ratio's combo types NOTHING — a
        // click on its number lists its ratios, no field opens, the ratio unmoved until one is picked.
        {
            auto t = make (bass);
            std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
            auto* block = findIn (*ed, (CompBlock*) nullptr);
            bool ok = block != nullptr;
            juce::String said;
            if (ok)
            {
                struct Gestures final : juce::AudioProcessorParameter::Listener
                {
                    int begun = 0;
                    void parameterValueChanged (int, float) override {}
                    void parameterGestureChanged (int, bool starting) override { begun += starting ? 1 : 0; }
                } gestures;
                auto* amountP = t->apvts.getParameter (params::compAmount);
                amountP->addListener (&gestures);
                block->threshold().openTyping();
                const bool lineField = block->threshold().isTyping();
                block->threshold().typeValue (-30.0f);
                const float atThirty = value (*t, params::compThreshold);
                block->threshold().typeValue (-60.0f);
                const float clamped = value (*t, params::compThreshold);
                block->fader().openTyping();
                const bool faderField = block->fader().isTyping();
                block->fader().typeValue (0.42f);
                const float typed = value (*t, params::compAmount);
                amountP->removeListener (&gestures);
                auto& combo = block->ratio();
                const float ratioBefore = value (*t, params::compRatio);
                int listed = 0;
                onfirst::catcher() = [&] (const juce::PopupMenu&, const std::function<void (int)>&) { ++listed; return true; };
                const auto number = combo.numberArea().getCentre().toFloat();
                const juce::MouseEvent click (juce::Desktop::getInstance().getMainMouseSource(), number, juce::ModifierKeys(), 0.0f,
                                              juce::MouseInputSource::defaultOrientation, juce::MouseInputSource::defaultRotation,
                                              juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY, &combo, &combo,
                                              juce::Time::getCurrentTime(), number, juce::Time::getCurrentTime(), 1, false);
                combo.mouseDown (click);
                combo.mouseUp (click);
                onfirst::catcher() = nullptr;
                const bool ratioField = findIn (combo, (juce::TextEditor*) nullptr) != nullptr;
                ok = lineField && faderField && ! ratioField && listed == 1 && std::abs (atThirty + 30.0f) < 0.01f && clamped == -48.0f
                  && std::abs (typed - 0.42f) < 0.001f && gestures.begun == 1 && value (*t, params::compRatio) == ratioBefore;
                said = ": -30 dB typed -> " + juce::String (atThirty, 3) + ", -60 -> " + juce::String (clamped, 3) + ", the fader 0.42 -> "
                     + juce::String (typed, 3) + " (" + juce::String (gestures.begun) + " gestures); a click on the ratio's number "
                     + (ratioField ? "opened a field" : "opened no field") + " and listed " + juce::String (listed) + " time(s), the ratio "
                     + juce::String (value (*t, params::compRatio), 2) + ":1";
            }
            check (ok, "COMP's numbers: the threshold and the fader are fields, the ratio a list, nothing typed" + said);
        }

        // THE RATIO IS ITS OWN: 8:1 stays 8:1 with BLEND moved; moved mid-note 10:1 -> 4:1 it glides, no burst, no step; a
        // session and a preset keep the threshold, the ratio and BLEND.
        {
            auto t = make (bass);
            set (*t, params::compRatio, 8.0f);
            (void) tone (*t, 97.0, -6.0f, 20);
            const double eight = t->compRatioNow();
            set (*t, params::compAmount, 0.8f);
            (void) tone (*t, 97.0, -6.0f, 10);
            const double stays = t->compRatioNow();
            check (std::abs (eight - 8.0) < 1.0e-3 && std::abs (stays - 8.0) < 1.0e-3,
                   "COMP's ratio its own: 8:1 set " + juce::String (eight, 2) + ":1, BLEND moved " + juce::String (stays, 2) + ":1");

            auto u = make (bass);
            set (*u, params::cabOn, 0.0f);
            set (*u, params::compRatio, 10.0f);
            const auto v = tone (*u, 97.0, -6.0f, 220, 120, [&] { set (*u, params::compRatio, 4.0f); });
            const auto w = judgeSwitch (v, 120, 30.0);
            check (w.burstDb <= 1.0 && w.stepRatio <= 3.0, "...10:1 to 4:1 mid-note, a glide: " + juce::String (w.burstDb, 1)
                                                               + " dB over steady, step x" + juce::String (w.stepRatio, 1));

            set (*t, params::compRatio, 7.5f);
            set (*t, params::compThreshold, -31.0f);
            set (*t, params::compAmount, 0.35f);
            juce::MemoryBlock session;
            t->getStateInformation (session);
            auto back = make (bass);
            back->setStateInformation (session.getData(), (int) session.getSize());
            const auto preset = t->presetForSaving();
            const auto inPreset = [&] (const char* id) { return (float) preset.getChildWithProperty ("id", id).getProperty ("value", -1.0f); };
            check (std::abs (value (*back, params::compRatio) - 7.5f) < 0.01f && std::abs (value (*back, params::compThreshold) + 31.0f) < 0.01f
                       && std::abs (value (*back, params::compAmount) - 0.35f) < 0.001f
                       && std::abs (inPreset (params::compRatio) - 7.5f) < 0.01f && std::abs (inPreset (params::compThreshold) + 31.0f) < 0.01f,
                   "a session and a preset keep COMP's threshold, ratio and BLEND");
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
            // The tail: until the squeeze has let go — OPTO's (the default since 02.11) is its memory's, up to 2 s a time
            // constant, so 16 s of silence.
            (void) tone (*t, 97.0, -6.0f, (int) std::ceil (16.0 * sampleRate / blockSize), -1, {}, 0);
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
                    // A voice that is there, and numbers: NaN compares false everywhere and a muted voice has no peak to find
                    // — either is a miss, not a pass.
                    double energy = 0.0;
                    for (int i = 16; i < 1000; ++i)
                    {
                        if (! std::isfinite (y[(size_t) i]))
                            return 1.0e9;
                        energy += (double) y[(size_t) i] * (double) y[(size_t) i];
                    }
                    if (! (energy > 1.0e-6))
                        return 1.0e9;
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
                    // Every number finite and the centroid's weight not zero: a NaN anywhere — the tap's phase included — is a
                    // miss, never a value std::max quietly drops.
                    const double err = std::abs (m / w - p);
                    if (! (std::abs (w) > 0.0) || ! std::isfinite (m) || ! std::isfinite (w) || ! std::isfinite (p) || ! std::isfinite (err))
                        return 1.0e9;
                    worst = std::max (worst, err);
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
                drawn = block->getWidth() == FaceplateView::fxRowWidth && pic.voices() == t->bassBackPlanes()
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

        // ITS CROSSOVER IS A LIST (the owner, 01.11): the five crossovers, nothing under 200 Hz, the one standing ticked; a
        // pick lands on its parameter, one step of the history, undone whole. Its RATE is a rail of its own at the right.
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
                said.clear();
                const auto list = pic.crossoverList();
                juce::StringArray items;
                int ticked = -1, n = 0;
                for (juce::PopupMenu::MenuItemIterator it (list); it.next(); ++n)
                {
                    items.add (it.getItem().text);
                    if (it.getItem().isTicked)
                        ticked = n;
                }
                ok = items.joinIntoString (",") == "200 Hz,250 Hz,300 Hz,400 Hz,500 Hz" && ticked == 0;
                said << "the list " << items.joinIntoString (",") << ", item " << ticked << " ticked; ";
                for (const int item : { 2, 4 })
                {
                    settle();
                    const float before = value (*t, params::chorusCrossoverHz);
                    const int   depth  = t->history.undoDepth();
                    pic.pickCrossover (item);
                    settle();
                    const float landed = value (*t, params::chorusCrossoverHz);
                    const int   steps  = t->history.undoDepth() - depth;
                    t->history.undo();
                    settle();
                    const float back = value (*t, params::chorusCrossoverHz);
                    ok = ok && std::abs (landed - params::chorusCrossoverItems[item]) < 1.0e-3f && steps == 1 && std::abs (back - before) < 1.0e-3f;
                    said << "picked " << juce::String (params::chorusCrossoverItems[item], 0) << " Hz -> " << juce::String (landed, 1) << " in "
                         << steps << " step(s), undone to " << juce::String (back, 1) << "; ";
                }
                auto& rate = block->rate();
                rate.typeValue (2.0f);
                settle();
                const bool rateRail = rate.isVisible() && rate.hasFaderLook() && std::abs (value (*t, params::chorusRateHz) - 2.0f) < 1.0e-3f;
                ok = ok && rateRail;
                said << "the RATE " << (rateRail ? "a fader of its own" : "NOT a fader on chorus_rate_hz");
            }
            check (ok, "CHORUS's crossover a list, its rate a rail: " + said);
        }
    }

    // ---- REVIEW ROUND 5 (of 317fbe1) AND THE OWNER'S LOOK (step 4b) --------------------------------------------
    {
        using core::BassComp;
        const auto findAll = [] (juce::Component& root, auto* none)
        {
            using T = std::remove_pointer_t<decltype (none)>;
            std::vector<T*> found;
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
            {
                if (auto* t = dynamic_cast<T*> (&c))
                    found.push_back (t);
                for (auto* child : c.getChildren())
                    walk (*child);
            };
            walk (root);
            return found;
        };
        const auto mouse = [] (juce::Component& c, juce::Point<float> at, juce::Point<float> down)
        {
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, juce::ModifierKeys(), 0.0f,
                                     juce::MouseInputSource::defaultOrientation, juce::MouseInputSource::defaultRotation,
                                     juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY,
                                     &c, &c, now, down, now, 1, at != down);
        };
        const auto settle = [] (AmpProcessor& a) { for (int i = 0; i < 40; ++i) a.history.tick(); };   // longer than the settle

        // ONE CONTROL, ONE SWITCH, ONE UNDO: COMP standing by, its line dragged — paused longer than the history settles —
        // switches it in; one undo gives back the knob AND the switch. The ratio square dragged, the same; and the ratio's
        // combo, a ratio picked from its list or typed in it.
        {
            auto t = make (bass);
            set (*t, params::compThreshold, -24.0f);
            set (*t, params::compOn, 0.0f);
            settle (*t);
            std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
            auto graphs = findAll (*ed, (CompGraph*) nullptr);
            auto squares = findAll (*ed, (CompTransfer*) nullptr);
            auto combos = findAll (*ed, (CompRatioCombo*) nullptr);
            juce::String said;
            bool ok = graphs.size() == 1 && squares.size() == 1 && combos.size() == 1;
            if (ok)
            {
                auto& g = *graphs[0];
                g.beginLineDrag();
                g.dragLineTo (g.yOfDb (-20.0));
                settle (*t);
                g.dragLineTo (g.yOfDb (-28.0));
                settle (*t);
                g.endLineDrag();
                settle (*t);
                const bool on = value (*t, params::compOn) > 0.5f;
                const float moved = value (*t, params::compThreshold);
                t->history.undo();
                const bool line = on && std::abs (moved + 24.0f) > 1.0f && std::abs (value (*t, params::compThreshold) + 24.0f) < 1.0e-3f
                               && value (*t, params::compOn) < 0.5f;
                said << "the line: on " << (on ? "yes" : "no") << ", undone to " << juce::String (value (*t, params::compThreshold), 2) + " dB";

                bool rest = true;
                for (const int way : { 0, 1, 2 })   // the square dragged, a ratio picked from the combo's list, one typed in it
                {
                    set (*t, params::compOn, 0.0f);
                    set (*t, params::compRatio, 6.0f);
                    settle (*t);
                    const float ratioBefore = value (*t, params::compRatio);
                    if (way == 0)
                    {
                        juce::Component* c = squares[0];
                        const auto start = c->getLocalBounds().toFloat().getCentre();
                        const auto end = start.translated (0.0f, 20.0f);
                        c->mouseDown (mouse (*c, start, start));
                        c->mouseDrag (mouse (*c, start.translated (0.0f, 10.0f), start));
                        settle (*t);
                        c->mouseDrag (mouse (*c, end, start));
                        c->mouseUp (mouse (*c, end, start));
                    }
                    else
                    {
                        std::function<void (int)> answer;
                        onfirst::catcher() = [&] (const juce::PopupMenu&, const std::function<void (int)>& a) { answer = a; return true; };
                        const auto at = (way == 1 ? combos[0]->chevronArea() : combos[0]->numberArea()).getCentre().toFloat();
                        combos[0]->mouseDown (mouse (*combos[0], at, at));
                        combos[0]->mouseUp (mouse (*combos[0], at, at));
                        onfirst::catcher() = nullptr;
                        if (answer != nullptr)
                            answer (way == 1 ? 2 : 7);   // 2:1 or 10:1
                    }
                    settle (*t);
                    const bool switched = value (*t, params::compOn) > 0.5f && std::abs (value (*t, params::compRatio) - ratioBefore) > 0.01f;
                    t->history.undo();
                    const bool gaveBack = std::abs (value (*t, params::compRatio) - ratioBefore) < 1.0e-3f;
                    rest = rest && switched && value (*t, params::compOn) < 0.5f && gaveBack;
                    said << (way == 0 ? "; the square " : way == 1 ? "; a pick by the chevron " : "; a pick by the number ") << (switched ? "switched in" : "did not switch in")
                         << ", one undo " << (gaveBack ? "gave it back" : "did not give it back");
                }
                ok = line && rest;
            }
            check (ok, "COMP's line and ratio switch it in, and one undo gives back all of a paused drag: " + said);
        }

        // A CLICK MOVES NOTHING: in the ratio square a press and a release where it was pressed change neither the ratio nor
        // whether it is held; on the combo a click on its number or on its chevron lists its ratios, no field — neither
        // moves the ratio until one is picked; a drag in the square takes the ratio from where the hand took it.
        {
            auto t = make (bass);
            set (*t, params::compAmount, 0.5f);
            std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
            auto squares = findAll (*ed, (CompTransfer*) nullptr);
            auto combos = findAll (*ed, (CompRatioCombo*) nullptr);
            bool ok = squares.size() == 1 && combos.size() == 1;
            double after = 0.0;
            set (*t, params::compRatio, 6.0f);
            const float ratioAtStart = value (*t, params::compRatio);
            bool field = false, listed = false;
            if (ok)
            {
                auto& sq = *squares[0];
                const auto at = sq.getLocalBounds().toFloat().getCentre().translated (7.0f, 23.0f);
                sq.mouseDown (mouse (sq, at, at));
                sq.mouseUp (mouse (sq, at, at));
                auto& combo = *combos[0];
                int caught = 0;
                onfirst::catcher() = [&] (const juce::PopupMenu&, const std::function<void (int)>&) { ++caught; return true; };
                const auto number = combo.numberArea().getCentre().toFloat();
                combo.mouseDown (mouse (combo, number, number));
                combo.mouseUp (mouse (combo, number, number));
                const auto chevron = combo.chevronArea().getCentre().toFloat();
                combo.mouseDown (mouse (combo, chevron, chevron));
                combo.mouseUp (mouse (combo, chevron, chevron));
                onfirst::catcher() = nullptr;
                field = ! findAll (combo, (juce::TextEditor*) nullptr).empty();
                listed = caught == 2;
                ok = ! field && listed && value (*t, params::compRatio) == ratioAtStart;
            }
            if (ok)
            {
                auto& sq = *squares[0];
                const double before = sq.shownRatio();
                const auto from = juce::Point<float> (sq.box().getCentreX(), sq.yOfRatio (before) + 15.0f);
                sq.mouseDown (mouse (sq, from, from));
                sq.mouseDrag (mouse (sq, from.translated (0.0f, 2.0f), from));   // under the threshold: nothing yet
                const bool still = value (*t, params::compRatio) == ratioAtStart;
                sq.mouseDrag (mouse (sq, from.translated (0.0f, -6.0f), from));
                sq.mouseUp (mouse (sq, from.translated (0.0f, -6.0f), from));
                after = sq.shownRatio();
                ok = still && after < before;   // up the square, a lower ratio — from where it stood
            }
            check (ok, juce::String ("a click in COMP's ratio square changes nothing; on the combo's number and on its chevron it lists (")
                           + (listed ? "listed both" : "not both") + (field ? ", a field opened" : ", no field")
                           + "), the ratio unmoved; a drag moves the ratio from where it was taken (now " + juce::String (after, 2) + ":1)");
        }

        // THE BASS'S RAILS — COMP's threshold a plain rail, its number on the grip at 13 px; every other a FADER (the owner,
        // 01.11: COMP's, OCTAVE's, ENVELOPE's and CHORUS's MIX, OCTAVE's TONE, ENVELOPE's RELEASE, CHORUS's RATE, BLEND and
        // DI/CAB), its number under it at 13 px with its unit; the guards' grips as they were. DELAY and REVERB are the
        // guitar's own blocks, their MIX the guitar's fader.
        {
            auto t = make (bass);
            set (*t, params::compAmount, 0.7f);
            set (*t, params::ampBlend, 0.4f);
            std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
            juce::StringArray wrong, seen;
            for (auto* r : findAll (*ed, (GuardRail*) nullptr))
            {
                seen.add (r->paramId());
                const bool plain = r->paramId() == params::compThreshold;   // the guards' are faders too, their numbers in their lamps
                if (plain == r->hasFaderLook())
                    wrong.add (r->paramId() + (plain ? " a fader" : " not a fader"));
                const float px = r->hasFaderLook() ? fader::textH : r->gripTextHeight;
                if (px < 13.0f)
                    wrong.add (r->paramId() + " " + juce::String (px, 1) + " px");
            }
            for (auto* r : findAll (*ed, (GuardRail*) nullptr))
            {
                if (r->paramId() == params::compAmount && r->gripText() != "70%")
                    wrong.add ("COMP reads " + r->gripText());
                if (r->paramId() == params::ampBlend && r->gripText() != "40%")
                    wrong.add ("BLEND reads " + r->gripText());
                if (r->paramId() == params::compThreshold && r->gripText() != "-24")
                    wrong.add ("COMP's threshold reads " + r->gripText());
                if (r->paramId() == params::diMix && r->gripText() != "30%")
                    wrong.add ("DI/CAB reads " + r->gripText());
                if (r->paramId() == params::octaveToneHz && r->gripText() != "120 Hz")
                    wrong.add ("TONE reads " + r->gripText());
            }
            check (wrong.isEmpty() && seen.contains (params::ampBlend) && seen.contains (params::compThreshold) && seen.contains (params::diMix)
                       && seen.contains (params::octaveToneHz) && seen.contains (params::filterReleaseMs) && seen.contains (params::chorusRateHz)
                       && seen.size() == 12,
                   "the bass's rails write their numbers at 13 px, the faders' with their units, COMP's threshold in dB on its grip; GATE and LIMIT faders, their numbers in their lamps ("
                       + juce::String (seen.size()) + " rails" + (wrong.isEmpty() ? juce::String() : ": " + wrong.joinIntoString (", ")) + ")");
        }

       #if JUCE_MAC || JUCE_WINDOWS
        // SHOWING FOLLOWS THE ANCESTORS: the window on the desktop, COMP's block hidden and shown again — the taps follow.
        {
            auto t = make (bass);
            std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
            ed->addToDesktop (0);
            ed->setVisible (true);
            auto graphs = findAll (*ed, (CompGraph*) nullptr);
            auto blocks = findAll (*ed, (CompBlock*) nullptr);
            bool shown = false, hidden = true, again = false;
            if (graphs.size() == 1 && blocks.size() == 1)
            {
                graphs[0]->refresh();
                shown = t->compWatched.load();
                blocks[0]->setVisible (false);
                graphs[0]->refresh();
                hidden = t->compWatched.load();
                blocks[0]->setVisible (true);
                graphs[0]->refresh();
                again = t->compWatched.load();
            }
            ed->removeFromDesktop();
            check (shown && ! hidden && again, juce::String ("COMP's taps follow its block hidden and shown: shown ") + (shown ? "on" : "off")
                                                   + ", hidden " + (hidden ? "on" : "off") + ", shown again " + (again ? "on" : "off"));
        }
       #endif

        // THE TRACE IS WHAT THE DETECTOR HEARS: a steady sine, its peak at -22 dB — SMOOTH's detector (RMS) reads it 3 dB
        // under, PUNCHY's at its peak; the input trace reads the same.
        {
            const auto traced = [&] (int character)
            {
                auto t = make (bass);
                set (*t, params::compAmount, 0.5f);
                set (*t, params::compCharacter, (float) character);
                t->compWatched.store (true);
                (void) tone (*t, 220.0, -22.0f, 60);
                std::vector<core::CompTap::Column> cols (50);
                const int got = t->compTap.read (cols.data(), 50);
                return got > 0 ? 20.0 * std::log10 ((double) cols[(size_t) got - 1].in + 1.0e-12) : -200.0;
            };
            const double smooth = traced (0), punchy = traced (1);
            check (std::abs (smooth + 25.0) < 0.6 && std::abs (punchy + 22.0) < 0.6,
                   "COMP's input trace is its detector's level: SMOOTH " + juce::String (smooth, 1) + " dB (RMS -25), PUNCHY "
                       + juce::String (punchy, 1) + " dB (peak -22)");
        }

        // COMP'S SLIDER SENT NaN keeps its last amount, as the effects' do.
        {
            const auto run = [&] (bool poison)
            {
                auto t = make (bass);
                set (*t, params::compAmount, 0.7f);
                return tone (*t, 97.0, -6.0f, 120, 40, [&]
                {
                    if (poison)
                        t->apvts.getRawParameterValue (params::compAmount)->store (std::numeric_limits<float>::quiet_NaN());
                });
            };
            const float apart = worstApart (run (true), run (false));
            check (apart == 0.0f, "COMP's slider sent NaN mid-note keeps its amount, bit for bit (worst " + juce::String (apart, 9) + ")");
        }

        // STANDING BY AFTER PLAYING IS SLIDER 0 IN THE SOUND: the three told up — playing on a silent input, so every
        // other link hears the same — their arrows off, a prepare; then the phrase plays, bit for bit, what it plays after
        // the same with the three at 0 (CHORUS used to arrive at its last knobs and fade out of them, wet).
        {
            const auto after = [&] (float amount)
            {
                auto t = make (bass);
                for (const char* id : { params::octaveAmount, params::filterAmount, params::chorusAmount })
                    set (*t, id, amount);
                (void) tone (*t, 82.41, -12.0f, 60, -1, {}, 0);
                for (const char* id : { params::octaveOn, params::filterOn, params::chorusOn })
                    set (*t, id, 0.0f);
                t->prepareToPlay (sampleRate, blockSize);
                t->pumpDeviceWork();
                return play (*t, 60, false);
            };
            const auto up = after (0.8f), zero = after (0.0f);
            const float apart = std::max (worstApart (up.l, zero.l), worstApart (up.r, zero.r));
            check (apart == 0.0f, "the three told up, standing by, prepared: the phrase as with the three at 0, bit for bit (worst "
                                      + juce::String (apart, 9) + ")");
        }

        // A GAP REACHES ALL THREE: playing up, their sliders to 0, then a gap — each ramps to idle through it.
        {
            auto t = make (bass);
            for (const char* id : { params::octaveAmount, params::filterAmount, params::chorusAmount })
                set (*t, id, 0.8f);
            (void) tone (*t, 82.41, -12.0f, 60);
            for (const char* id : { params::octaveAmount, params::filterAmount, params::chorusAmount })
                set (*t, id, 0.0f);
            juce::AudioBuffer<float> none (0, blockSize);
            juce::MidiBuffer midi;
            for (int b = 0; b < 40; ++b)
                t->processBlock (none, midi);
            check (t->effectIdle (0) && t->effectIdle (1) && t->effectIdle (2), "a gap reaches OCTAVE, FILTER and CHORUS: each idle after it");
        }

        // A CALL WIDER THAN PREPARED: a mono bus prepared, a host hands two channels — the back half stays one plane, both
        // outputs the same.
        {
            auto t = make (bass);
            t->setPlayConfigDetails (1, 1, sampleRate, blockSize);
            t->prepareToPlay (sampleRate, blockSize);
            t->pumpDeviceWork();
            set (*t, params::chorusAmount, 0.8f);
            std::vector<float> r;
            const auto l = tone (*t, 82.41, -12.0f, 60, -1, {}, 1 << 30, &r);
            check (t->bassBackPlanes() == 1 && worstApart (l, r) == 0.0f,
                   "a mono bus handed two channels: one plane, both outputs the same (planes " + juce::String (t->bassBackPlanes()) + ", worst "
                       + juce::String (worstApart (l, r), 9) + ")");
        }
    }

    // ---- STEP 5: OCTAVE'S PICTURE — the note it names the one played, the sub's period twice the note's, the frame held
    //      still on the sub's rising edge; no sub line while nothing is caught; watching it changes nothing heard; its
    //      taps idle while it is not on screen and still in silence; a read that never mixes two generations ---------------
    {
        const auto findScope = [] (juce::Component& root)
        {
            OctaveScope* found = nullptr;
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
            {
                if (auto* s = dynamic_cast<OctaveScope*> (&c); s != nullptr && found == nullptr)
                    found = s;
                for (auto* child : c.getChildren())
                    walk (*child);
            };
            walk (root);
            return found;
        };

        // A steady sine that goes on where it stopped — a phase jump would be a new note to the octaver.
        const auto player = [] (AmpProcessor& amp, double hz, float db)
        {
            return [&amp, hz, a = juce::Decibels::decibelsToGain ((double) db), n = 0LL] (int blocks, bool silent = false) mutable
            {
                juce::AudioBuffer<float> buf (2, blockSize);
                juce::MidiBuffer midi;
                for (int b = 0; b < blocks; ++b)
                {
                    for (int i = 0; i < blockSize; ++i, ++n)
                    {
                        const auto v = silent ? 0.0f : (float) (a * std::sin (juce::MathConstants<double>::twoPi * hz * (double) n / sampleRate));
                        buf.setSample (0, i, v);
                        buf.setSample (1, i, v);
                    }
                    amp.processBlock (buf, midi);
                }
            };
        };

        // ITS PLACE: OCTAVE 170 wide, ENVELOPE 170, CHORUS 200 — and THE AMP AS WIDE AS IT WAS beside narrow rails: the
        // face wider by what OCTAVE and ENVELOPE took (the AMP's section 2 is the real packs' lever, its EQ row's knobs
        // need their room), the guitar face's AMP and the BLEND column, at the window's least width. CHORUS's width
        // comes out of the cabinet's share. No lamps: each picture shows its effect at work.
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed (amp->createEditor());
            std::map<juce::String, int> widths;
            int ampW = -1, faceW = -1, lamps = 0;
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
            {
                if (auto* g = dynamic_cast<GuardBlock*> (&c))
                    widths[g->guardName()] = g->getWidth();
                if (auto* f = dynamic_cast<FilterBlock*> (&c))
                    widths["ENVELOPE"] = f->getWidth();
                if (auto* ch = dynamic_cast<ChorusBlock*> (&c))
                    widths["CHORUS"] = ch->getWidth();
                if (auto* p = dynamic_cast<CapturedBlockPanel*> (&c))
                    ampW = p->getWidth();
                if (auto* f = dynamic_cast<FaceplateView*> (&c))
                    faceW = f->getWidth();
                if (dynamic_cast<GuardLamp*> (&c) != nullptr && c.isVisible())
                    ++lamps;
                for (auto* child : c.getChildren())
                    walk (*child);
            };
            walk (*ed);
            auto* scope = findScope (*ed);
            // THREE ROWS (the owner, 31.10): OCTAVE and ENVELOPE share what row one leaves after the GATE and COMP, alike;
            // CHORUS at least its width; the AMP the whole width of its own row.
            check (std::abs (widths["OCTAVE"] - widths["ENVELOPE"]) <= 1 && widths["OCTAVE"] >= 170 && widths["CHORUS"] >= FaceplateView::fxRowWidth
                       && scope != nullptr && scope->getWidth() > 100 && scope->getHeight() > 90,
                   "OCTAVE's block " + juce::String (widths["OCTAVE"]) + " px wide with its picture "
                       + (scope != nullptr ? juce::String (scope->getWidth()) + " x " + juce::String (scope->getHeight()) : juce::String ("missing"))
                       + "; ENVELOPE " + juce::String (widths["ENVELOPE"]) + ", CHORUS " + juce::String (widths["CHORUS"]));
            check (faceW > 0 && ampW == faceW, "the AMP " + juce::String (ampW) + " px wide: the whole face's width (" + juce::String (faceW) + "), a row of its own");
            check (lamps == 2, "the bass's effects show no lamp, COMP, OCTAVE, ENVELOPE and CHORUS alike: the GATE's and the LIMIT's alone ("
                                   + juce::String (lamps) + " lamps)");
        }

        // THE MAPPING: four notes across the bass's range, the -1 alone (the -2 is gone, the owner 01.11) — the note named is the
        // one played and the sub an octave down (A = 440, the tuner's reference untouched), the sub's period twice the
        // note's, two of them across, the frame standing on the sub's rising edge and standing STILL: the same shape
        // 3584 samples later (no whole number of periods).
        for (const auto& [hz, note, below] : { std::tuple<double, const char*, const char*> { 30.87, "B0", "B-1" },
                                               { 41.20, "E1", "E0" }, { 55.00, "A1", "A0" }, { 98.00, "G2", "G1" } })
        {
            auto t = make (bass);
            set (*t, params::octaveAmount, 0.6f);
            std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
            auto* scope = findScope (*ed);
            if (scope == nullptr)
            {
                check (false, "OCTAVE's picture is on the bass's face");
                break;
            }
            t->octaveWatched.store (true);   // as if on screen
            auto play = player (*t, hz, -12.0f);
            play (60);
            scope->pull();
            const auto a = scope->frame();
            const auto [named, under] = scope->reading();
            std::array<float, 64> shape {};
            float peak = 0.0f;
            for (size_t k = 0; k < shape.size(); ++k)
                peak = std::max (peak, std::abs (shape[k] = scope->subAt (a.start + a.span * (double) k / 64.0)));
            const bool rising = a.subPeriod > 0.0 && scope->subAt (a.trigger - 0.25 * a.subPeriod) < 0.0f
                                && scope->subAt (a.trigger + 0.25 * a.subPeriod) > 0.0f;

            play (7);
            scope->pull();
            const auto b = scope->frame();
            float moved = 0.0f;
            for (size_t k = 0; k < shape.size(); ++k)
                moved = std::max (moved, std::abs (scope->subAt (b.start + b.span * (double) k / 64.0) - shape[k]));

            const double expected = 2.0 * a.rateHz / hz;   // the -1's period, samples
            const double off      = a.subPeriod > 0.0 ? std::abs (a.subPeriod / expected - 1.0) : 1.0;
            check (a.subShown && named == note && under == below && off < 0.005 && std::abs (a.span - 2.0 * a.subPeriod) < 1.0e-9
                       && rising && peak > 0.0f && moved < 0.1f * peak && std::abs (b.trigger - a.trigger) > 1.0,
                   juce::String (hz, 2) + " Hz reads " + named + " -> " + under
                       + " (" + note + " -> " + below + "), the sub's period " + juce::String (a.subPeriod, 1) + " samples ("
                       + juce::String (expected, 1) + "), " + juce::String (a.span / juce::jmax (1.0, a.subPeriod), 2)
                       + " of them across, " + (rising ? "on its rising edge" : "NOT on a rising edge") + ", moved "
                       + juce::String (peak > 0.0f ? moved / peak : 1.0f, 3) + " of its peak 3584 samples later (its edge "
                       + juce::String (a.trigger - b.trigger, 1) + " samples back in the ring)");
        }

        // THE NAMES: the octave by floor below MIDI 0 too — the sub of a 16 Hz note is C-1's octave down, not C-1 again.
        check (OctaveScope::noteText (16.35, 440.0) == "C0" && OctaveScope::noteText (8.18, 440.0) == "C-1"
                   && OctaveScope::noteText (7.72, 440.0) == "B-2" && OctaveScope::noteText (440.0, 440.0) == "A4"
                   && OctaveScope::noteText (432.0, 432.0) == "A4" && OctaveScope::noteText (30.87, 440.0) == "B0",
               "the reading's names: C0, C-1, B-2, A4 at 440 and at 432, B0; got " + OctaveScope::noteText (7.72, 440.0)
                   + " for 7.72 Hz");

        // NOTHING CAUGHT, NO SUB LINE: the slider at 0 — nothing written, the picture still (the owner, 31.10: this was
        // "the note drawn, no sub"), no reading; up, the sub; the note gone (the octaver lets go), no sub again.
        {
            auto t = make (bass);
            set (*t, params::octaveAmount, 0.0f);
            std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
            auto* scope = findScope (*ed);
            t->octaveWatched.store (true);
            auto play = player (*t, 41.2, -12.0f);
            play (60);
            bool atZero = false, up = false, gone = false;
            if (scope != nullptr)
            {
                scope->pull();
                atZero = ! scope->frame().subShown && scope->reading().first.isEmpty() && scope->frame().count == 0
                         && t->octaveTap->written() == 0;
                set (*t, params::octaveAmount, 0.6f);
                play (60);
                scope->pull();
                up = scope->frame().subShown;
                play (40, true);
                scope->pull();
                gone = ! scope->frame().subShown && scope->reading().first.isEmpty();
            }
            check (atZero && up && gone, juce::String ("no sub line at 0 (") + (atZero ? "yes" : "NO") + "), the sub at 0.6 ("
                                             + (up ? "yes" : "NO") + "), none once the note has gone (" + (gone ? "yes" : "NO") + ")");
        }

        // WATCHING CHANGES NOTHING HEARD: the stage run in pieces to keep its dry (the octaver's output does not depend on
        // how its calls are sliced) — a note, its end, bit for bit the same as with nobody watching.
        {
            auto quiet = make (bass), seen = make (bass);
            for (auto* t : { quiet.get(), seen.get() })
            {
                set (*t, params::octaveAmount, 0.7f);
            }
            seen->octaveWatched.store (true);
            std::vector<float> rq, rs;
            const auto vq = tone (*quiet, 41.2, -6.0f, 120, -1, {}, 80, &rq);
            const auto vs = tone (*seen, 41.2, -6.0f, 120, -1, {}, 80, &rs);
            check (vq.size() == vs.size() && std::memcmp (vq.data(), vs.data(), vq.size() * sizeof (float)) == 0
                       && rq.size() == rs.size() && std::memcmp (rq.data(), rs.data(), rq.size() * sizeof (float)) == 0
                       && seen->octaveTap->written() > 0 && quiet->octaveTap->written() == 0,
                   "OCTAVE watched plays bit for bit what it plays unwatched (" + juce::String ((int) seen->octaveTap->written())
                       + " samples into the tap)");
        }

        // THE TAPS: idle with no picture on screen — an editor up but not showing included; written while it is; still
        // once the playing stops.
        {
            auto t = make (bass);
            set (*t, params::octaveAmount, 0.6f);
            std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
            auto* scope = findScope (*ed);
            const bool off = scope != nullptr && ! scope->watching() && ! t->octaveWatched.load();
            (void) tone (*t, 41.2, -6.0f, 50);
            const auto hidden = t->octaveTap->written();
            t->octaveWatched.store (true);
            (void) tone (*t, 41.2, -6.0f, 100);
            const auto playing = t->octaveTap->written();
            (void) tone (*t, 41.2, -6.0f, 100, -1, {}, 0);
            const auto tail = t->octaveTap->written();
            (void) tone (*t, 41.2, -6.0f, 100, -1, {}, 0);
            const auto still = t->octaveTap->written();
            check (off && hidden == 0 && playing > 0 && tail > playing && still == tail,
                   "OCTAVE's taps: the picture not on screen does not ask (" + juce::String (off ? "yes" : "NO") + "), nothing written ("
                       + juce::String ((int) hidden) + "), " + juce::String ((int) playing) + " samples while it shows, "
                       + juce::String ((int) (tail - playing)) + " of the tail, then none in silence (" + juce::String ((int) (still - tail)) + ")");
        }

        // NO READ MIXES TWO GENERATIONS: a writer lapping the ring as fast as it can, a reader taking the whole ring again
        // and again — every read one unbroken run, the note and the sub of one sample together, the slots overtaken
        // during a read dropped (and some were: a run that never saw a drop proves nothing).
        {
            auto tap = std::make_unique<core::OctaveTap>();
            tap->prepare (sampleRate);
            std::atomic<bool> stop { false };
            std::thread writer ([&]
            {
                float d[64], w[64];
                std::uint32_t j = 0;
                while (! stop.load (std::memory_order_relaxed))
                {
                    for (int i = 0; i < 64; ++i, ++j)
                    {
                        d[i] = 1.0f + (float) (j % 4000000u);
                        w[i] = 2.0f * d[i];   // the sub: exactly the note again
                    }
                    tap->write (d, w, 64);
                }
            });
            std::vector<float> rd ((size_t) core::OctaveTap::capacity), rs ((size_t) core::OctaveTap::capacity);
            long reads = 0, torn = 0, dropped = 0;
            const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds (400);
            while (std::chrono::steady_clock::now() < until)
            {
                const int n = tap->read (rd.data(), rs.data(), core::OctaveTap::capacity);
                ++reads;
                dropped += n < core::OctaveTap::capacity && tap->written() >= (std::uint32_t) core::OctaveTap::capacity ? 1 : 0;
                for (int i = 0; i < n; ++i)
                    if (rs[(size_t) i] != rd[(size_t) i]
                        || (i > 0 && rd[(size_t) i] != rd[(size_t) i - 1] + 1.0f && ! (rd[(size_t) i] == 1.0f && rd[(size_t) i - 1] == 4000000.0f)))
                    {
                        ++torn;
                        break;
                    }
            }
            stop.store (true);
            writer.join();
            check (torn == 0 && dropped > 0 && reads > 50,
                   "OCTAVE's tap read " + juce::String ((int) reads) + " times under a lapping writer: " + juce::String ((int) torn)
                       + " torn, " + juce::String ((int) dropped) + " that dropped what was overtaken");
        }
    }

    // ---- ENVELOPE (FILTER), A BLOCK OF ITS OWN WITH ITS PICTURE — the curve the stage's, its marks where the sweep goes,
    //      its start corner a field, its taps idle while nobody looks, still when the sweep stands, never two laps mixed --
    {
        namespace ef = felitronics::envfilter;
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
        struct Gestures final : juce::AudioProcessorParameter::Listener
        {
            int begun = 0, ended = 0;
            void parameterValueChanged (int, float) override {}
            void parameterGestureChanged (int, bool starting) override { ++(starting ? begun : ended); }
        };

        // THE BLOCK: ENVELOPE, 170 wide after OCTAVE, the picture right of its slider.
        {
            auto t = make (bass);
            std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
            auto* blk = findIn (*ed, (FilterBlock*) nullptr);
            check (blk != nullptr && blk->getWidth() >= FaceplateView::filterWidth && blk->picture().getWidth() > 90
                       && blk->picture().getHeight() > 120,
                   "ENVELOPE's block " + juce::String (blk != nullptr ? blk->getWidth() : 0) + " wide, its picture "
                       + juce::String (blk != nullptr ? blk->picture().getWidth() : 0) + " x " + juce::String (blk != nullptr ? blk->picture().getHeight() : 0));
        }

        // THE CURVE IS THE STAGE'S: a quiet probe (-60 dBFS, under every sensitivity's floor, so it moves nothing) through
        // the module, the sweep at rest and held part-way open by a steady key; its settled gain at nine frequencies
        // against the picture's at the cutoff the module reports — both modes, the lows kept and not, mix 1 and 0.4.
        {
            const auto measure = [] (const ef::EnvelopeFilterParams& p, float keyLevel, double hz, double& cutoff)
            {
                ef::EnvelopeFilter f;
                (void) f.prepare (sampleRate, blockSize, 1);
                f.setParams (p);
                std::vector<float> x ((size_t) blockSize), key ((size_t) blockSize, keyLevel);
                constexpr double a = 1.0e-3;
                const int nBlocks = (int) (sampleRate / blockSize);
                double c = 0.0, sn = 0.0;
                long long n = 0, counted = 0;
                for (int b = 0; b < nBlocks; ++b)
                {
                    const long long first = n;
                    for (int i = 0; i < blockSize; ++i, ++n)
                        x[(size_t) i] = (float) (a * std::sin (juce::MathConstants<double>::twoPi * hz * (double) n / sampleRate));
                    float* io[] = { x.data() };
                    const float* k[] = { key.data() };
                    (void) f.process (io, 1, blockSize, keyLevel > 0.0f ? k : nullptr, keyLevel > 0.0f ? 1 : 0);
                    if (b >= nBlocks / 2)
                        for (int i = 0; i < blockSize; ++i, ++counted)
                        {
                            const double ph = juce::MathConstants<double>::twoPi * hz * (double) (first + i) / sampleRate;
                            c += x[(size_t) i] * std::cos (ph);
                            sn += x[(size_t) i] * std::sin (ph);
                        }
                }
                cutoff = (double) f.shownCutoffHz();
                return 20.0 * std::log10 (2.0 * std::sqrt (c * c + sn * sn) / (double) counted / a);
            };
            struct Case { ef::Mode mode; float keep; float amount; float q; int range; ef::Direction dir; };
            const Case cases[] { { ef::Mode::lowPass, 1.0f, 0.6f, 4.0f, 1, ef::Direction::up },
                                 { ef::Mode::lowPass, 0.0f, 0.2f, 12.0f, 1, ef::Direction::up },
                                 { ef::Mode::bandPass, 1.0f, 0.6f, 8.0f, 2, ef::Direction::down },
                                 { ef::Mode::bandPass, 0.0f, 0.2f, 2.0f, 0, ef::Direction::up } };
            double worst = 0.0;
            juce::String where;
            int probes = 0, moved = 0;
            for (const auto& cs : cases)
                for (const float keyLevel : { 0.0f, 0.1f })
                    for (const double hz : { 50.0, 90.0, 150.0, 250.0, 400.0, 700.0, 1200.0, 2500.0, 6000.0 })
                    {
                        ef::EnvelopeFilterParams menu;
                        menu.mode = cs.mode;
                        menu.keepLows = cs.keep;
                        menu.keepLowsHz = 150.0f;
                        menu.resonance = cs.q;
                        menu.sweepLoHz = params::filterRangeHz[cs.range][0];
                        menu.sweepHiHz = params::filterRangeHz[cs.range][1];
                        menu.direction = cs.dir;
                        const auto p = ef::paramsForAmount (cs.amount, menu);
                        double cutoff = 0.0;
                        const double heard = measure (p, keyLevel, hz, cutoff);
                        const double drawn = core::envelopeResponseDb (p, cutoff, hz, sampleRate);
                        ++probes;
                        if (keyLevel > 0.0f && std::abs (std::log2 (cutoff / core::envelopeRestHz (p))) > 0.5)
                            ++moved;   // the key held the sweep well away from its rest
                        if (std::abs (heard - drawn) > worst)
                        {
                            worst = std::abs (heard - drawn);
                            where = juce::String (cs.mode == ef::Mode::lowPass ? "LP" : "BP") + " at " + juce::String (hz, 0) + " Hz, cutoff "
                                    + juce::String (cutoff, 0) + ": heard " + juce::String (heard, 2) + ", drawn " + juce::String (drawn, 2);
                        }
                    }
            check (probes == 72 && moved == 36 && worst <= 0.2,
                   "the picture's curve is what the stage does, " + juce::String (probes) + " probes (" + juce::String (moved)
                       + " with the sweep held open) within " + juce::String (worst, 3) + " dB (worst: " + where + ")");
        }

        // THE MARKS ARE WHERE THE SWEEP GOES. The start: the amp in silence rests where the line stands, each range either
        // way. The reach: a fingered note (-18 dBFS) at the key takes the sweep to the reach mark at every slider — exactly
        // through the module with a steady key, and through the whole amp with a played square.
        {
            double worstRest = 0.0;
            for (const auto& [range, down] : { std::pair<int, bool> { 1, false }, { 2, true }, { 0, false }, { 0, true } })
            {
                auto t = make (bass);
                set (*t, params::filterAmount, 0.6f);
                set (*t, params::filterRange, (float) range);
                set (*t, params::filterDirection, down ? 1.0f : 0.0f);
                std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
                auto& pic = findIn (*ed, (FilterBlock*) nullptr)->picture();
                (void) tone (*t, 110.0, -6.0f, 40, -1, {}, 0);
                worstRest = std::max (worstRest, std::abs (std::log2 ((double) t->filterShownCutoffHz() / pic.restHz())));
            }
            double worstReach = 0.0;
            for (const float amount : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
            {
                ef::EnvelopeFilterParams menu;
                const auto p = ef::paramsForAmount (amount, menu);
                ef::EnvelopeFilter f;
                (void) f.prepare (sampleRate, blockSize, 1);
                f.setParams (p);
                std::vector<float> x ((size_t) blockSize, 0.0f), key ((size_t) blockSize, (float) juce::Decibels::decibelsToGain (FilterCurve::fingeredDb));
                float* io[] = { x.data() };
                const float* k[] = { key.data() };
                for (int b = 0; b < 40; ++b)
                    (void) f.process (io, 1, blockSize, k, 1);
                worstReach = std::max (worstReach, std::abs (std::log2 ((double) f.shownCutoffHz() / core::envelopeReachHz (p, FilterCurve::fingeredDb))));
            }
            double played = 0.0, drawnReach = 0.0;
            {
                auto t = make (bass);
                set (*t, params::filterAmount, 0.5f);
                std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
                auto& pic = findIn (*ed, (FilterBlock*) nullptr)->picture();
                juce::AudioBuffer<float> buf (2, blockSize);
                juce::MidiBuffer midi;
                const float a = (float) juce::Decibels::decibelsToGain (FilterCurve::fingeredDb);
                long long n = 0;
                for (int b = 0; b < 60; ++b)
                {
                    for (int i = 0; i < blockSize; ++i, ++n)
                        for (int ch = 0; ch < 2; ++ch)
                            buf.setSample (ch, i, (n / 109) % 2 == 0 ? a : -a);   // a 220 Hz square: |x| steady at the hit; the cuts in
                    t->processBlock (buf, midi);
                }
                played = (double) t->filterShownCutoffHz();
                drawnReach = pic.reachHz();
            }
            check (worstRest <= 0.005 && worstReach <= 0.005 && std::abs (std::log2 (played / drawnReach)) <= 0.07,
                   "ENVELOPE's marks: the start where the amp rests (worst " + juce::String (worstRest, 4) + " octave), the reach where a "
                       "fingered note takes the sweep (worst " + juce::String (worstReach, 4) + " octave); played through the amp at 0.5, "
                       + juce::String (played, 0) + " Hz against the mark's " + juce::String (drawnReach, 0) + " Hz");
        }

        // THE START CORNER IS THE RANGE'S LIST (the owner, 01.11): LOW, MID and HIGH, each named with the corner it rests at
        // — its low end going up, its high end going down — the one playing ticked; a pick is one gesture.
        {
            auto t = make (bass);
            std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
            auto& pic = findIn (*ed, (FilterBlock*) nullptr)->picture();
            auto* rangeP = t->apvts.getParameter (params::filterRange);
            const auto listed = [&]
            {
                juce::StringArray items;
                const auto m = pic.rangeList();
                for (juce::PopupMenu::MenuItemIterator it (m); it.next();)
                    items.add (it.getItem().text + (it.getItem().isTicked ? "*" : ""));
                return items.joinIntoString (",");
            };
            Gestures gestures;
            rangeP->addListener (&gestures);
            pic.pickRange (2);
            const int high = juce::roundToInt (value (*t, params::filterRange));
            const double highRest = pic.restHz();
            const auto upList = listed();
            pic.pickRange (0);
            const int low = juce::roundToInt (value (*t, params::filterRange));
            set (*t, params::filterDirection, 1.0f);
            pic.pickRange (1);
            const int mid = juce::roundToInt (value (*t, params::filterRange));
            const double downRest = pic.restHz();
            const auto downList = listed();
            rangeP->removeListener (&gestures);
            const auto word = [] (int i) { return params::filterRanges[i].toUpperCase() + "   "; };
            check (high == 2 && juce::approximatelyEqual (highRest, (double) params::filterRangeHz[2][0]) && low == 0 && mid == 1
                       && juce::approximatelyEqual (downRest, (double) params::filterRangeHz[1][1])
                       && upList.startsWith (word (0)) && upList.endsWith ("*") && downList.contains (word (1)) && downList.indexOfChar ('*') > 0
                       && gestures.begun == 3 && gestures.ended == 3,
                   "ENVELOPE's start corner a list: HIGH -> " + params::filterRanges[high] + " (" + juce::String (highRest, 0) + " Hz), LOW -> "
                       + params::filterRanges[low] + ", going down MID -> " + params::filterRanges[mid] + " (" + juce::String (downRest, 0)
                       + " Hz); listed up " + upList + ", down " + downList + "; " + juce::String (gestures.begun) + " gestures begun, "
                       + juce::String (gestures.ended) + " ended");
        }

        // THE TAPS: nothing written with no picture on screen — an editor up but not showing included; written while it
        // is, the newest column where the module says the sweep stands; still once the sweep has landed; a fresh look takes
        // the next column; and the picture's own tick lets go when it is not showing — a hidden parent tells no child.
        {
            auto t = make (bass);
            set (*t, params::filterAmount, 0.8f);
            std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
            auto& pic = findIn (*ed, (FilterBlock*) nullptr)->picture();
            (void) tone (*t, 41.2, -6.0f, 50);
            const auto hidden = t->filterTap.written();
            t->filterWatched.store (true);
            (void) tone (*t, 41.2, -6.0f, 60);
            const auto playing = t->filterTap.written();
            core::FilterTap::Column newest[1];
            const bool atModule = t->filterTap.read (newest, 1) == 1 && juce::exactlyEqual (newest[0].hz, t->filterShownCutoffHz());
            (void) tone (*t, 41.2, -6.0f, 150, -1, {}, 0);
            const auto tail = t->filterTap.written();
            (void) tone (*t, 41.2, -6.0f, 100, -1, {}, 0);
            const auto still = t->filterTap.written();
            t->filterWatched.store (false);
            (void) tone (*t, 41.2, -6.0f, 5, -1, {}, 0);
            t->filterWatched.store (true);
            (void) tone (*t, 41.2, -6.0f, 1, -1, {}, 0);
            const auto woken = t->filterTap.written();
            t->filterWatched.store (false);
            check (hidden == 0 && playing > 0 && atModule && tail > playing && still == tail && woken > still,
                   "ENVELOPE's taps: nothing written off screen (" + juce::String ((int) hidden) + "), " + juce::String ((int) playing)
                       + " columns playing" + (atModule ? ", the newest the module's own cutoff, " : ", NOT the module's cutoff, ")
                       + juce::String ((int) (tail - playing)) + " of the ride back, none once it stood (" + juce::String ((int) (still - tail))
                       + "), " + juce::String ((int) (woken - still)) + " on a fresh look");

            const bool pollsHidden = pic.polling() && ! pic.isWatching();
            pic.tick (true);                                   // as if it showed
            const bool watched = t->filterWatched.load() && pic.isWatching();
            pic.tick();                                        // the timer's tick: not showing (no screen here, as under a hidden parent)
            check (pollsHidden && watched && ! t->filterWatched.load() && ! pic.isWatching() && pic.polling(),
                   "ENVELOPE's picture asks isShowing() on every tick: watching while it shows, let go the tick it does not, polling still");
        }

        // NEVER TWO LAPS OF THE RING: a writer racing a reader; every column read is the one its number says, or dropped.
        {
            auto tap = std::make_unique<core::FilterTap>();
            tap->prepare (sampleRate);
            // A hundredth of an octave a column, never still, over a period prime to the ring's: the column a lap later in
            // the same slot holds another value — a pattern that repeated with the ring would make every mix-up look right.
            std::vector<float> hzOf (997);
            for (size_t i = 0; i < hzOf.size(); ++i)
                hzOf[i] = (float) (100.0 * std::exp2 ((double) i / 100.0));
            const int hop = (int) std::lround (sampleRate * core::FilterTap::hopSeconds);
            std::atomic<bool> stop { false };
            std::thread writer ([&]
            {
                for (std::uint32_t k = 0; ! stop.load (std::memory_order_relaxed); ++k)
                    tap->write (hzOf[k % hzOf.size()], hop);
            });
            std::vector<core::FilterTap::Column> cols ((size_t) core::FilterTap::columns);
            long long reads = 0, seen = 0, wrong = 0, dropped = 0;
            const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds (300);
            while (std::chrono::steady_clock::now() < until)
            {
                const auto want = (int) std::min<std::uint32_t> (tap->written(), (std::uint32_t) cols.size());
                const int got = tap->read (cols.data(), (int) cols.size());
                ++reads;
                dropped += std::max (0, want - got);
                for (int i = 0; i < got; ++i, ++seen)
                    if (! juce::exactlyEqual (cols[(size_t) i].hz, hzOf[cols[(size_t) i].index % hzOf.size()]))
                        ++wrong;
            }
            stop.store (true);
            writer.join();
            std::printf ("envelope tap race: %lld reads, %lld columns, %lld dropped mid-read, %lld wrong\n", reads, seen, dropped, wrong);
            check (reads > 100 && seen > 1000 && wrong == 0, "ENVELOPE's tap never mixes two laps: " + juce::String (seen) + " columns read under a racing writer, "
                                                                 + juce::String (wrong) + " wrong, " + juce::String (dropped) + " dropped as overwritten");
        }
    }

    // ---- ONE HPF ACROSS THE CHAIN, ONE LPF AT THE END, ONE PAIR ON THE FACE (the owner, 31.10) ----------------------
    {
        const auto param = [] (AmpProcessor& a, const juce::String& id) { return a.apvts.getParameter (id); };
        const auto put = [&] (AmpProcessor& a, const juce::String& id, float v)
        {
            if (auto* p = param (a, id))
            {
                p->setValueNotifyingHost (p->convertTo0to1 (v));
                a.pumpDeviceWork();
            }
        };
        const bool structured = param (*amp, "bass_hpf_on") != nullptr && param (*amp, "bass_lpf_on") != nullptr;

        // THE INNER CUTS COST THE LOWEST STRING ALMOST NOTHING: the whole chain with the HPF on, a low B at 5 STR and a low E
        // at 4 STR, against the END cut alone — within 0.5 dB.
        double lostB = 99.0, lostE = 99.0;
        {
            const auto level = [&] (double hz, int choice, bool inner)
            {
                auto t = make (bass);
                set (*t, params::cabOn, 0.0f);
                put (*t, "bass_hpf_on", 1.0f);
                put (*t, params::bassHpf, (float) choice);
                t->innerCutsOut.store (! inner);
                return levelAt (tone (*t, hz, -12.0f, 150), hz);
            };
            // EVERY frequency the menus offer, each choice's low string against the END cut alone.
            juce::String worstAt;
            lostB = lostE = 0.0;
            for (const bool five : { true, false })
                for (const float hz : five ? std::vector<float> (std::begin (params::bassHpfMenuHz5), std::end (params::bassHpfMenuHz5))
                                           : std::vector<float> (std::begin (params::bassHpfMenuHz4), std::end (params::bassHpfMenuHz4)))
                {
                    const auto at = [&] (bool inner)
                    {
                        auto t = make (bass);
                        set (*t, params::cabOn, 0.0f);
                        put (*t, "bass_hpf_on", 1.0f);
                        put (*t, params::bassHpf, (float) (five ? params::BassHpf::five : params::BassHpf::four));
                        put (*t, five ? params::bassHpfHz5 : params::bassHpfHz4, hz);
                        t->innerCutsOut.store (! inner);
                        const double note = five ? 30.9 : 41.2;
                        return levelAt (tone (*t, note, -12.0f, 150), note);
                    };
                    const double lost = at (false) - at (true);
                    // ...and the four inner cuts ALONE, a sine through them in series: what they take before the AMP (whose
                    // own curve then squeezes the difference — the chain's number is the smaller).
                    double alone = 0.0;
                    {
                        std::array<core::BassCuts, 4> cuts;
                        core::BassCuts::Settings in;
                        in.hpfOn = true;
                        in.hpfHz = (double) hz / 3.0;
                        const double note = five ? 30.9 : 41.2;
                        std::vector<float> x ((size_t) sampleRate), y ((size_t) sampleRate);
                        for (size_t i = 0; i < x.size(); ++i)
                            x[i] = y[i] = (float) (0.25 * std::sin (juce::MathConstants<double>::twoPi * note * (double) i / sampleRate));
                        for (auto& c : cuts)
                        {
                            c.prepare (sampleRate);
                            for (size_t i = 0; i < y.size(); i += (size_t) blockSize)
                            {
                                float* ch[] = { y.data() + i };
                                c.process (ch, 1, (int) std::min ((size_t) blockSize, y.size() - i), in);
                            }
                        }
                        alone = levelAt (x, note) - levelAt (y, note);
                    }
                    (five ? lostB : lostE) = std::max ({ five ? lostB : lostE, lost, alone });
                    worstAt << (five ? " 5@" : " 4@") << juce::String (hz, 0) << ": chain " << juce::String (lost, 2) << ", alone " << juce::String (alone, 3);
                }
            check (structured && lostB <= 0.5 && lostE <= 0.5 && lostB >= 0.0 && lostE >= 0.0,
                   "the inner cuts cost — through the chain and alone, the larger — the low B at 5 STR at most " + juce::String (lostB, 2) + " dB and the low E at 4 STR at most "
                       + juce::String (lostE, 2) + " dB against the END cut alone, every frequency of the menus (0.5 at most):" + worstAt);
        }

        // ...AND THEY WORK: a sub thump (a 4 Hz half-sine) reaches the AMP far lower, and a DC offset on the string comes
        // out of the AMP far lower, with the inner cuts than with the END cut alone.
        {
            const auto measure = [&] (bool inner, bool thump)
            {
                auto t = make (bass);
                set (*t, params::cabOn, 0.0f);
                t->innerCutsOut.store (! inner);
                t->probing.store (true);
                juce::AudioBuffer<float> buf (2, blockSize);
                juce::MidiBuffer midi;
                long long n = 0;
                for (int b = 0; b < 135; ++b)   // the window: blocks 60..134, 38400 samples — 48 whole cycles of a 60 Hz note
                {
                    if (b == 60)
                    {
                        t->probeAmpInPeak = 0.0f;
                        t->probeAmpOutSum = 0.0;
                        t->probeAmpOutCount = 0;
                    }
                    for (int i = 0; i < blockSize; ++i, ++n)
                    {
                        const double tt = (double) n / sampleRate;
                        const double note = 0.3 * std::sin (juce::MathConstants<double>::twoPi * 60.0 * tt);
                        const double bump = (tt >= 0.7 && tt < 0.825) ? 0.5 * std::sin (juce::MathConstants<double>::twoPi * 4.0 * (tt - 0.7)) : 0.0;
                        const auto v = (float) (thump ? bump : note + 0.2);
                        buf.setSample (0, i, v);
                        buf.setSample (1, i, v);
                    }
                    t->processBlock (buf, midi);
                    t->pumpDeviceWork();
                }
                return thump ? (double) t->probeAmpInPeak : std::abs (t->probeAmpOutSum / (double) juce::jmax (1LL, t->probeAmpOutCount));
            };
            const double thumpDb = juce::Decibels::gainToDecibels (measure (true, true) / juce::jmax (1.0e-9, measure (false, true)));
            const double dcIn = measure (true, false), dcOut = measure (false, false);
            const double dcDb = juce::Decibels::gainToDecibels (dcIn / juce::jmax (1.0e-9, dcOut));
            check (thumpDb < -12.0 && dcDb < -12.0, "the inner cuts at work: a 4 Hz thump into the AMP " + juce::String (thumpDb, 1)
                                                        + " dB, a string's DC offset out of the AMP " + juce::String (dcDb, 1) + " dB (" + juce::String (dcIn, 6) + " against "
                                                        + juce::String (dcOut, 6) + "), against the END cut alone");
        }

        // THE GUITAR'S CUTS ARE OUT OF THE BASS: the AMP EQ's HPF switched on at 200 Hz and its LPF at 1 kHz change
        // nothing, bit for bit.
        {
            auto plainAmp = make (bass);
            const auto before = tone (*plainAmp, 110.0, -12.0f, 80);
            auto cut = make (bass);
            put (*cut, params::eqHpfOn (0), 1.0f);
            put (*cut, params::eqHpfHz (0), 200.0f);
            put (*cut, params::eqLpfOn (0), 1.0f);
            put (*cut, params::eqLpfHz (0), 1000.0f);
            const float apart = worstApart (tone (*cut, 110.0, -12.0f, 80), before);
            check (apart == 0.0f, "the AMP EQ's guitar HPF and LPF switched on change nothing on the bass, bit for bit (worst "
                                      + juce::String (apart, 9) + ")");
        }

        // THE SWITCH REMEMBERS ITS CHOICE: 4 STR chosen, the HPF switched off and on — at 4 STR again; a session keeps both.
        {
            auto t = make (bass);
            put (*t, params::bassHpf, (float) params::BassHpf::four);
            put (*t, "bass_hpf_on", 0.0f);
            put (*t, "bass_hpf_on", 1.0f);
            const bool kept = structured && juce::roundToInt (value (*t, params::bassHpf)) == (int) params::BassHpf::four && param (*t, "bass_hpf_on") != nullptr && param (*t, "bass_hpf_on")->getValue() > 0.5f;
            check (kept, "the HPF switched off and on comes back at 4 STR, the choice kept apart from the switch");
        }

        // ONE PAIR ON THE FACE: the AMP block's HPF and LPF switches are the bass's cuts, the choices under them its
        // positions; the cabinet carries no HPF or LPF word.
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed (amp->createEditor());
            int cabWords = 0;
            bool bassPair = false;
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
            {
                if (auto* w = dynamic_cast<bass::WordSwitch*> (&c); w != nullptr && w->isVisible()
                        && (w->getButtonText().startsWith ("HPF") || w->getButtonText() == "FINGERS" || w->getButtonText() == "SLAP"
                            || w->getButtonText().startsWith ("LPF")))
                    ++cabWords;
                if (auto* panel = dynamic_cast<CapturedBlockPanel*> (&c); panel != nullptr && panel->console().cutsAreBass())
                    bassPair = true;
                for (auto* child : c.getChildren())
                    walk (*child);
            };
            walk (*ed);
            check (bassPair && cabWords == 0, "one pair on the face: the AMP's HPF and LPF are the bass's cuts ("
                                                  + juce::String (bassPair ? "yes" : "no") + "), no cut words on the cabinet (" + juce::String (cabWords) + ")");
        }
    }

    // ---- REVIEW ROUND 6 (of b45e462) -----------------------------------------------------------------------
    {
        const auto findAll = [] (juce::Component& root, auto* none)
        {
            using T = std::remove_pointer_t<decltype (none)>;
            std::vector<T*> found;
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
            {
                if (auto* t = dynamic_cast<T*> (&c))
                    found.push_back (t);
                for (auto* child : c.getChildren())
                    walk (*child);
            };
            walk (root);
            return found;
        };
        const auto mouse = [] (juce::Component& c, juce::Point<float> at, juce::Point<float> down)
        {
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, juce::ModifierKeys(), 0.0f,
                                     juce::MouseInputSource::defaultOrientation, juce::MouseInputSource::defaultRotation,
                                     juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY,
                                     &c, &c, now, down, now, 1, at != down);
        };
        const auto settle = [] (AmpProcessor& a) { for (int i = 0; i < 40; ++i) a.history.tick(); };

        // THE EFFECTS' RAILS UNDO AS COMP'S DOES: a drag paused longer than the history settles is ONE step; two typed
        // numbers in quick succession are TWO.
        {
            auto t = make (bass);
            settle (*t);
            std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
            juce::StringArray said;
            bool ok = true;
            for (const char* id : { params::octaveAmount, params::filterAmount, params::chorusAmount })
            {
                GuardRail* rail = nullptr;
                for (auto* r : findAll (*ed, (GuardRail*) nullptr))
                    if (r->paramId() == id)
                        rail = r;
                if (rail == nullptr)
                {
                    ok = false;
                    said.add (juce::String (id) + " no rail");
                    continue;
                }
                const float before = value (*t, id);
                const auto from = juce::Point<float> (rail->getWidth() * 0.5f, rail->getHeight() - 20.0f);
                rail->mouseDown (mouse (*rail, from, from));
                rail->mouseDrag (mouse (*rail, from.translated (0.0f, -30.0f), from));
                settle (*t);
                rail->mouseDrag (mouse (*rail, from.translated (0.0f, -60.0f), from));
                rail->mouseUp (mouse (*rail, from.translated (0.0f, -60.0f), from));
                settle (*t);
                const float dragged = value (*t, id);
                t->history.undo();
                const bool drag = std::abs (dragged - before) > 0.05f && std::abs (value (*t, id) - before) < 1.0e-3f;

                rail->typeValue (0.3f);
                rail->typeValue (0.6f);
                settle (*t);
                t->history.undo();
                const bool firstKept = std::abs (value (*t, id) - 0.3f) < 1.0e-3f;
                t->history.undo();
                const bool typed = firstKept && std::abs (value (*t, id) - before) < 1.0e-3f;
                ok = ok && drag && typed;
                said.add (juce::String (id) + (drag ? " drag 1 step" : " drag not 1 step") + (typed ? ", typed 2" : ", typed not 2"));
            }
            check (ok, "the effects' rails: a paused drag one undo step, two typed numbers two — " + said.joinIntoString ("; "));
        }

        // THE CUTS LIVE WITH THE AMP STANDING BY: they work across the whole chain, so their switches, words and choices do not
        // dim with the AMP.
        {
            auto t = make (bass);
            std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
            set (*t, params::preampOn, 0.0f);
            auto panels = findAll (*ed, (CapturedBlockPanel*) nullptr);
            const bool live = panels.size() == 1 && panels[0]->console().cutsLive();
            check (live, "the HPF and LPF stay live on the AMP's block with the AMP standing by");
        }

        // THE IN METER READS THE JACK: a DI's rumble over full scale — 4 Hz at 1.2 — latches IN's clip, the input's gentle cut
        // after the meter, not before it.
        {
            auto t = make (bass);
            t->inClip.store (false);
            (void) tone (*t, 4.0, juce::Decibels::gainToDecibels (1.2f), 60);
            check (t->inClip.load(), "a 4 Hz rumble at 1.2 latches IN's clip: the meter reads the jack, before the input's HPF");
        }

        // ENVELOPE'S PICTURE ON A SLOW SWEEP: 1 kHz to 2 kHz over 20 s at 512-sample blocks keeps writing — stillness is judged
        // against the last column PUBLISHED, so a slow climb that never moves a column's worth at once still gets there.
        {
            core::FilterTap tap;
            tap.prepare (sampleRate);
            const int blocksIn20s = (int) (20.0 * sampleRate / blockSize);
            for (int b = 0; b < blocksIn20s; ++b)
                tap.write ((float) (1000.0 * std::pow (2.0, (double) b / blocksIn20s)), blockSize);
            std::vector<core::FilterTap::Column> cols (4);
            const int got = tap.read (cols.data(), 4);
            const double last = got > 0 ? (double) cols[(size_t) got - 1].hz : 0.0;
            check (got > 0 && std::abs (std::log2 (last / 2000.0)) < 0.01,
                   "ENVELOPE's picture follows a 20 s sweep from 1 to 2 kHz: its newest column at " + juce::String (last, 0) + " Hz");
        }

        // ENVELOPE'S TRAIL, A COLUMN EACH: while watched the stage runs a column at a time, so a moving sweep's columns each
        // carry their own cutoff (not a block's two or three the same) — and the sound is the same, bit for bit.
        {
            const auto run = [&] (bool watched, std::vector<core::FilterTap::Column>* cols)
            {
                auto t = make (bass);
                set (*t, params::filterAmount, 0.8f);
                t->filterWatched.store (watched);
                const auto out = tone (*t, 110.0, -6.0f, 30, -1, {}, 12);   // a note, then its fall: the sweep moves
                if (cols != nullptr)
                {
                    cols->resize (core::FilterTap::columns);
                    cols->resize ((size_t) t->filterTap.read (cols->data(), core::FilterTap::columns));
                }
                return out;
            };
            std::vector<core::FilterTap::Column> cols;
            const auto seen = run (true, &cols), unseen = run (false, nullptr);
            int pairs = 0, same = 0;
            for (size_t i = 1; i + 1 < cols.size(); ++i)
                if (cols[i].index == cols[i - 1].index + 1 && cols[i + 1].index == cols[i].index + 1
                    && cols[i + 1].hz != cols[i - 1].hz)   // the sweep moving across these three columns
                {
                    ++pairs;
                    same += cols[i].hz == cols[i - 1].hz ? 1 : 0;
                }
            check (pairs > 20 && same * 10 < pairs && worstApart (seen, unseen) == 0.0f,
                   "ENVELOPE's trail a column each: " + juce::String (same) + " of " + juce::String (pairs) + " neighbouring columns the same; watched or not, the sound bit for bit (worst "
                       + juce::String (worstApart (seen, unseen), 9) + ")");
        }
    }

    // ---- RE-CHECK ROUND 7 (of 954fd16) -----------------------------------------------------------------------
    {
        const auto settle = [] (AmpProcessor& a) { for (int i = 0; i < 40; ++i) a.history.tick(); };
        const auto first = [] (juce::Component& root, auto* none)
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

        // THE PICTURES' LISTS ARE A STEP A PICK: two picks in quick succession on CHORUS's crossover and on ENVELOPE's
        // start corner (its RANGE) — two undo steps, not one burst.
        {
            auto t = make (bass);
            settle (*t);
            std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
            auto* chorusPic = first (*ed, (ChorusPicture*) nullptr);
            auto* curve     = first (*ed, (FilterCurve*) nullptr);
            juce::StringArray said;
            bool ok = chorusPic != nullptr && curve != nullptr;
            if (ok)
            {
                {
                    const float before = value (*t, params::chorusCrossoverHz);
                    chorusPic->pickCrossover (2);
                    chorusPic->pickCrossover (4);
                    settle (*t);
                    t->history.undo();
                    const bool one = std::abs (value (*t, params::chorusCrossoverHz) - params::chorusCrossoverItems[2]) < 0.5f;
                    t->history.undo();
                    const bool two = one && std::abs (value (*t, params::chorusCrossoverHz) - before) < 0.5f;
                    ok = ok && two;
                    said.add (juce::String ("CHORUS's crossover") + (two ? " two steps" : " not two steps"));
                }
                const float before = value (*t, params::filterRange);
                curve->pickRange (0);
                const float a = value (*t, params::filterRange);
                curve->pickRange (2);
                settle (*t);
                t->history.undo();
                const bool one = juce::approximatelyEqual (value (*t, params::filterRange), a);
                t->history.undo();
                const bool two = one && juce::approximatelyEqual (value (*t, params::filterRange), before) && ! juce::approximatelyEqual (a, before);
                ok = ok && two;
                said.add (juce::String ("ENVELOPE's start") + (two ? " two steps" : " not two steps"));
            }
            check (ok, "the pictures' lists, two quick picks two undo steps: " + said.joinIntoString ("; "));
        }

        // ENVELOPE'S PICTURE THROUGH A GAP: watched, a note sweeps it up; a gap of 40 blocks lets the sweep fall, and the
        // picture's newest column is where it fell to — not where the note left it.
        {
            auto t = make (bass);
            set (*t, params::filterAmount, 0.8f);
            t->filterWatched.store (true);
            (void) tone (*t, 110.0, -6.0f, 60);
            juce::AudioBuffer<float> none (0, blockSize);
            juce::MidiBuffer midi;
            for (int b = 0; b < 40; ++b)
                t->processBlock (none, midi);
            std::vector<core::FilterTap::Column> cols (4);
            const int got = t->filterTap.read (cols.data(), 4);
            const double shown = got > 0 ? (double) cols[(size_t) got - 1].hz : 0.0, now = (double) t->filterShownCutoffHz();
            // Within a column's own movement: the newest column is the last one closed, up to a column (5 ms) behind the sweep,
            // which is still falling.
            const double oneColumn = got > 1 ? std::abs (std::log2 ((double) cols[(size_t) got - 2].hz / shown)) : 0.0;
            check (got > 1 && std::abs (std::log2 (shown / now)) <= std::max (0.01, oneColumn),
                   "ENVELOPE's picture through a gap: its newest column " + juce::String (shown, 1) + " Hz, the sweep at " + juce::String (now, 1) + " Hz (columns "
                       + juce::String ((int) t->filterTap.written()) + ": " + juce::String (got > 3 ? cols[0].hz : 0.0f, 1) + " " + juce::String (got > 3 ? cols[1].hz : 0.0f, 1)
                       + " " + juce::String (got > 3 ? cols[2].hz : 0.0f, 1) + " at " + juce::String (got > 3 ? (int) cols[3].index : -1) + ")");
        }
    }

    // ---- STEP 8: COMP'S IN AND OUT MEASURED ALIKE; THE RATIO'S DETENTS; A GEAR AND A SETTINGS WINDOW ON EVERY BLOCK ---------
    {
        const auto first = [] (juce::Component& root, auto* none)
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

        // COMP AT 0, AND STANDING BY, WRITES NOTHING (the owner, 31.10 — this reverses step 9's "IN and OUT on each other"):
        // its picture stands still. Gated with the other five under STEP 10 below.

        // THE RATIO'S COMBO (the owner, 01.11): its list 1.5, 2, 3, 4, 5, 7, 10 : 1, the current one ticked, no ON over it
        // (a value list, though it opens inside COMP's block); ONLY THE LIST'S RATIOS — a click ANYWHERE on the cell (its
        // number, its chevron, its corners) lists them, no field ever opens; a pick is ONE undo step; and a drag in the
        // square STEPS through the same list — at every height the ratio whose segment end stands nearest, nothing between.
        {
            auto t = make (bass);
            std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
            auto* combo = first (*ed, (CompRatioCombo*) nullptr);
            auto* square = first (*ed, (CompTransfer*) nullptr);
            const auto settle = [&] { for (int i = 0; i < 40; ++i) t->history.tick(); };
            juce::StringArray wrong;
            if (combo == nullptr || square == nullptr)
                wrong.add ("no ratio controls");
            else
            {
                juce::StringArray lines;
                int ons = 0, lists = 0;
                std::function<void (int)> answer;
                onfirst::catcher() = [&] (const juce::PopupMenu& m, const std::function<void (int)>& a)
                {
                    lines.clear();
                    for (juce::PopupMenu::MenuItemIterator it (m); it.next();)
                    {
                        const auto& item = it.getItem();
                        ons += item.itemID == onfirst::item || item.text == "ON" ? 1 : 0;
                        lines.add (item.isSeparator ? juce::String ("-") : item.text + (item.isTicked ? " *" : ""));
                    }
                    answer = a;
                    ++lists;
                    return true;
                };
                const auto click = [&] (juce::Point<float> at)
                {
                    const juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), at, juce::ModifierKeys(), 0.0f,
                                              juce::MouseInputSource::defaultOrientation, juce::MouseInputSource::defaultRotation,
                                              juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY, combo, combo,
                                              juce::Time::getCurrentTime(), at, juce::Time::getCurrentTime(), 1, false);
                    combo->mouseDown (e);
                    combo->mouseUp (e);
                };
                const auto cell = combo->getLocalBounds().toFloat();
                const float ratioAtStart = value (*t, params::compRatio);
                for (const auto at : { combo->numberArea().getCentre().toFloat(), combo->chevronArea().getCentre().toFloat(),
                                       cell.getTopLeft().translated (1.5f, 1.5f), cell.getBottomRight().translated (-1.5f, -1.5f),
                                       cell.getBottomLeft().translated (1.5f, -1.5f) })
                {
                    const int before = lists;
                    click (at);
                    if (lists != before + 1)
                        wrong.add ("a click at " + at.toString() + " listed " + juce::String (lists - before) + " time(s)");
                }
                if (ons != 0 || lines.joinIntoString (",") != "1.5:1,2:1,3:1,4:1 *,5:1,7:1,10:1")
                    wrong.add ("the list reads " + lines.joinIntoString (",") + (ons != 0 ? " with ON" : ""));
                if (onfirst::switchOf (combo) == nullptr)
                    wrong.add ("the combo is not inside COMP's block (the list's want of ON proves nothing)");
                if (first (*combo, (juce::TextEditor*) nullptr) != nullptr || value (*t, params::compRatio) != ratioAtStart)
                    wrong.add ("the clicks typed or moved something: the ratio " + juce::String (value (*t, params::compRatio), 3));

                // A PICK, ONE UNDO STEP, from 3:1 to each end of the list.
                settle();
                for (const auto& [item, want, shown] : { std::tuple { 1, 1.5, "1.5:1" }, std::tuple { 7, 10.0, "10:1" }, std::tuple { 5, 5.0, "5:1" } })
                {
                    set (*t, params::compRatio, 3.0f);
                    settle();
                    answer = nullptr;
                    click (combo->numberArea().getCentre().toFloat());
                    if (answer != nullptr)
                        answer (item);
                    settle();
                    const double got = combo->shownRatio();
                    const auto text = combo->text();
                    const int ticked = combo->tickedItem();
                    t->history.undo();
                    const float undone = value (*t, params::compRatio);
                    if (got != want || text != shown || ticked != item - 1 || std::abs (undone - 3.0f) > 1.0e-4f)
                        wrong.add ("item " + juce::String (item) + " picked -> " + juce::String (got, 3) + " reading \"" + text + "\", ticked "
                                   + juce::String (ticked) + ", one undo back to " + juce::String (undone, 3));
                }
                onfirst::catcher() = nullptr;

                // THE SQUARE STEPS: at threshold -24 and -40, every height of the box lands on a list ratio, the nearest.
                for (const float thr : { -24.0f, -40.0f })
                {
                    set (*t, params::compThreshold, thr);
                    const auto b = square->box();
                    int off = 0;
                    for (float y = b.getY(); y <= b.getBottom(); y += 1.0f)
                    {
                        set (*t, params::compRatio, 6.0f);   // off the list: a drag never leaves it there
                        square->beginRatioDrag();
                        square->dragRatioTo (y);
                        square->endRatioDrag();
                        const double got = square->shownRatio();
                        double nearest = compRatioItems[0];
                        for (const double r : compRatioItems)
                            if (std::abs (square->yOfRatio (r) - y) < std::abs (square->yOfRatio (nearest) - y))
                                nearest = r;
                        off += got != nearest ? 1 : 0;
                    }
                    if (off != 0)
                        wrong.add (juce::String (off) + " heights at " + juce::String (thr, 0) + " dB off the nearest list ratio");
                }
                set (*t, params::compThreshold, -24.0f);
                set (*t, params::compRatio, 4.0f);
                square->beginRatioDrag();
                square->dragRatioTo (square->yOfRatio (3.0) + 2.0f);
                square->endRatioDrag();
                if (square->shownRatio() != 3.0)
                    wrong.add ("the square dragged 2 px off 3:1 lands at " + juce::String (square->shownRatio(), 3));
            }
            check (wrong.isEmpty(), "COMP's ratio combo: its list 1.5, 2, 3, 4, 5, 7, 10 : 1, the current ticked, no ON; a click anywhere on it "
                                    "lists, nothing typed; a pick one undo step; the square steps to the nearest list ratio"
                                    + (wrong.isEmpty() ? juce::String() : ": " + wrong.joinIntoString ("; ")));
        }

        // NO GEAR ON ANY FRAME (the owner, 01.11: the gears off the frames, all of them), on either face: nothing on them is
        // the gear's "Menu"; every OrbitBass block but the guards has its MENU on a right click, COMP's too (gated whole
        // under STEP 12's THE MENUS).
        {
            auto t = make (bass);
            std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
            auto g = make (products::orbitAmp());
            std::unique_ptr<juce::AudioProcessorEditor> ged (g->createEditor());
            int menus = 0, gears = 0;
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
            {
                if (auto* b = dynamic_cast<BlockFrame*> (&c); b != nullptr && b->isVisible() && b->hasBlockMenu())
                    ++menus;
                gears += c.getTitle() == "Menu" ? 1 : 0;   // the gear's own name, its accessible title
                for (auto* child : c.getChildren())
                    walk (*child);
            };
            walk (*ed);
            const int bassMenus = menus;
            menus = 0;
            walk (*ged);
            auto* face = first (*ed, (FaceplateView*) nullptr);
            auto* compBlock = first (*ed, (CompBlock*) nullptr);
            if (compBlock != nullptr)
            {
                const juce::Point<float> corner (4.0f, 4.0f);
                compBlock->mouseDown (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), corner,
                                                        juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier), 0.0f,
                                                        juce::MouseInputSource::defaultOrientation, juce::MouseInputSource::defaultRotation,
                                                        juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY,
                                                        compBlock, compBlock, juce::Time::getCurrentTime(), corner, juce::Time::getCurrentTime(), 1, false));
            }
            check (gears == 0 && bassMenus == 6 && menus == 0 && compBlock != nullptr && face != nullptr && face->lastMenu == "COMP",
                   "no frame wears a gear, on either face (" + juce::String (gears) + " found); every OrbitBass block but the guards has its menu on a "
                       "right click (" + juce::String (bassMenus) + " of 6, the guitar's " + juce::String (menus) + "); a right click on COMP opens its menu ("
                       + (face != nullptr ? face->lastMenu : juce::String ("no face")) + ")");
        }
    }

    // ---- STEP 9: DELAY AND REVERB BEFORE THE CABINET, OUT OF THE STRIP BY DEFAULT; FIVE EFFECTS REMOVABLE; THE GEARS ------
    {
        const auto settle = [] (AmpProcessor& a) { for (int i = 0; i < 40; ++i) a.history.tick(); };
        const auto walkAll = [] (juce::Component& root, const std::function<void (juce::Component&)>& visit)
        {
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
            {
                visit (c);
                for (auto* child : c.getChildren())
                    walk (*child);
            };
            walk (root);
        };
        const auto shownIn = [] (juce::Component& c, juce::Component& root)
        {
            for (auto* x = &c; x != nullptr && x != &root; x = x->getParentComponent())
                if (! x->isVisible())
                    return false;
            return true;
        };
        const auto label = [] (BlockFrame& b)
        {
            if (auto* g = dynamic_cast<GuardBlock*> (&b))
                return g->guardName();
            return b.getName().isNotEmpty() ? b.getName() : juce::String (typeid (b).name());
        };
        const auto both = [] (AmpProcessor& t, bool in)
        {
            set (t, params::delayPresent, in ? 1.0f : 0.0f);
            set (t, params::reverbPresent, in ? 1.0f : 0.0f);
        };

        // THE STRIP: DELAY and REVERB absent; added, after CHORUS in the signal's order; OCTAVE, ENVELOPE and CHORUS taken
        // out the same way. OrbitAmp's strip as it was.
        {
            auto t = make (bass);
            both (*t, true);
            const auto all = arrows (AmpEditor::stripRows (*t));
            set (*t, params::octavePresent, 0.0f);
            set (*t, params::filterPresent, 0.0f);
            set (*t, params::chorusPresent, 0.0f);
            const auto few = arrows (AmpEditor::stripRows (*t));
            check (all == "GATE > COMP > OCTAVE > ENVELOPE > AMP > CHORUS > DELAY > REVERB > CAB IR > LIMIT"
                       && few == "GATE > COMP > AMP > DELAY > REVERB > CAB IR > LIMIT",
                   "added, DELAY and REVERB stand after CHORUS (" + all + "); the three effects out: " + few);
        }

        // THE SOUND: out of the strip, an effect with its slider up plays as one at 0, bit for bit; DELAY and REVERB
        // added are heard.
        {
            juce::StringArray loud;
            for (const auto& [present, amount] : { std::pair<const char*, const char*> { params::octavePresent, params::octaveAmount },
                                                   { params::filterPresent, params::filterAmount }, { params::chorusPresent, params::chorusAmount } })
            {
                auto out = make (bass);
                set (*out, amount, 0.8f);
                set (*out, present, 0.0f);
                out->prepareToPlay (sampleRate, blockSize);
                out->pumpDeviceWork();
                auto zero = make (bass);
                set (*zero, amount, 0.0f);
                zero->prepareToPlay (sampleRate, blockSize);
                zero->pumpDeviceWork();
                if (worstApart (play (*out, 150, false).l, play (*zero, 150, false).l) != 0.0f)
                    loud.add (amount);
            }
            check (loud.isEmpty(), "OCTAVE, ENVELOPE and CHORUS out of the strip, their sliders at 0.8, play as at 0, bit for bit"
                                       + (loud.isEmpty() ? juce::String() : " — not: " + loud.joinIntoString (", ")));

            auto base = make (bass);
            const auto dry = play (*base, 200, false);
            float heard[2] {};
            for (int k = 0; k < 2; ++k)
            {
                auto t = make (bass);
                set (*t, k == 0 ? params::delayPresent : params::reverbPresent, 1.0f);
                set (*t, k == 0 ? params::delayMix : params::reverbMix, 50.0f);
                t->prepareToPlay (sampleRate, blockSize);
                t->pumpDeviceWork();
                heard[k] = worstApart (play (*t, 200, false).l, dry.l);
            }
            check (heard[0] > 1.0e-3f && heard[1] > 1.0e-3f, "DELAY and REVERB added are heard (worst apart " + juce::String (heard[0], 4)
                                                                 + ", " + juce::String (heard[1], 4) + ")");
        }

        // THE LOW CUT ON THE WET: what the stage ADDS to a held sine, against the dry at the same frequency — at 880 Hz the
        // repeat whole, at 55 Hz more than 20 dB under it (the bass's lows stay dry). A single repeat (REPS 0), limiter out.
        {
            const auto wetRel = [&] (bool reverbStage, double hz)
            {
                const auto run = [&] (bool in)
                {
                    auto t = make (bass);
                    set (*t, params::limiterOn, 0.0f);
                    set (*t, reverbStage ? params::reverbPresent : params::delayPresent, in ? 1.0f : 0.0f);
                    set (*t, reverbStage ? params::reverbMix : params::delayMix, 100.0f);
                    set (*t, params::delaySync, 0.0f);
                    set (*t, params::delayTimeMs, 100.0f);
                    set (*t, params::delayRepeats, 0.0f);
                    t->prepareToPlay (sampleRate, blockSize);
                    t->pumpDeviceWork();
                    return tone (*t, hz, -18.0f, 80);
                };
                const auto with = run (true), without = run (false);
                std::vector<float> added (with.size());
                for (size_t i = 0; i < with.size(); ++i)
                    added[i] = with[i] - without[i];
                return levelAt (added, hz) - levelAt (without, hz);
            };
            const double dLow = wetRel (false, 55.0), dHigh = wetRel (false, 880.0);
            const double rLow = wetRel (true, 55.0),  rHigh = wetRel (true, 880.0);
            check (dHigh > -3.0 && dLow < dHigh - 20.0 && rLow < rHigh - 20.0,
                   "the wet's low cut: DELAY's repeat " + juce::String (dHigh, 1) + " dB at 880 Hz, " + juce::String (dLow, 1)
                       + " dB at 55 Hz; REVERB's tail " + juce::String (rHigh, 1) + " dB at 880 Hz, " + juce::String (rLow, 1) + " dB at 55 Hz");
        }

        // OUT OF THE STRIP, FADED AND CLEARED: DELAY taken out mid-tail stops running once its fade is done; added again in
        // silence it brings none of its old repeats back.
        {
            auto t = make (bass);
            set (*t, params::delayPresent, 1.0f);
            set (*t, params::delayMix, 100.0f);
            set (*t, params::delayRepeats, 80.0f);
            set (*t, params::delaySync, 0.0f);
            set (*t, params::delayTimeMs, 200.0f);
            t->prepareToPlay (sampleRate, blockSize);
            t->pumpDeviceWork();
            (void) tone (*t, 220.0, -12.0f, 40);
            const bool ranBefore = t->spaceRan (false);
            // Out, and silence until the chain before it has nothing left to give: a repeat of that would be no fault.
            (void) tone (*t, 220.0, -12.0f, 30, 0, [&] { set (*t, params::delayPresent, 0.0f); }, 0);
            const bool ranAfter = t->spaceRan (false);
            const auto quiet = tone (*t, 220.0, -12.0f, 30, 0, [&] { set (*t, params::delayPresent, 1.0f); }, 0);
            // ...against the same amp that never had it.
            auto never = make (bass);
            (void) tone (*never, 220.0, -12.0f, 40);
            (void) tone (*never, 220.0, -12.0f, 30, -1, {}, 0);
            const auto alone = tone (*never, 220.0, -12.0f, 30, -1, {}, 0);
            float tail = 0.0f, loudT = 0.0f, loudN = 0.0f;
            for (size_t i = quiet.size() / 2; i < quiet.size(); ++i)
            {
                tail  = std::max (tail, std::abs (quiet[i] - alone[i]));
                loudT = std::max (loudT, std::abs (quiet[i]));
                loudN = std::max (loudN, std::abs (alone[i]));
            }
            check (ranBefore && ! ranAfter && t->spaceRan (false) && tail < 1.0e-3f,
                   "DELAY out of the strip stops running after its fade, added again it starts silent (apart from one that never had it by "
                       + juce::String (tail, 6) + " in silence; loudest " + juce::String (loudT, 6) + " and " + juce::String (loudN, 6) + ")");
        }

        // THE PICTURES' TAPS: written only while watched, and read whole.
        {
            auto t = make (bass);
            set (*t, params::delayPresent, 1.0f);
            set (*t, params::delayMix, 60.0f);
            set (*t, params::delayRepeats, 50.0f);
            set (*t, params::delaySync, 0.0f);
            set (*t, params::delayTimeMs, 250.0f);
            SpacePicture pic (*t, SpacePicture::Which::delay);
            pic.look();   // not on screen: not watched
            const auto w0 = t->delayTap.written();
            (void) tone (*t, 220.0, -12.0f, 30);
            const auto w1 = t->delayTap.written();
            t->delayWatched.store (true);
            (void) tone (*t, 220.0, -12.0f, 30);
            const auto w2 = t->delayTap.written();
            pic.pull();
            const auto marks = pic.lawMarks();
            check (w1 == w0 && w2 > w1 + 20 && pic.shownCount() > 20 && pic.newestDb() > -40.0f
                       && marks.size() >= 2 && std::abs (marks[0].x - 0.25f) < 0.01f && std::abs (marks[1].y - (marks[0].y - 6.02f)) < 0.1f,
                   "DELAY's picture: its tap written only while watched (" + juce::String ((int) (w1 - w0)) + " then "
                       + juce::String ((int) (w2 - w1)) + " entries), the newest at " + juce::String (pic.newestDb(), 1)
                       + " dB; its repeats every 250 ms, each 6 dB under the last at REPS 50");
        }

        // DELAY's and REVERB's settings but MIX: in their menus, gated under STEP 12's THE MENUS.

        // THE CHECKLIST: the five, ticked as they stand; a pick one undo step; the strip, the face and the window's least
        // width follow — DELAY and REVERB the guitar's own blocks, each fxRowWidth wide (the owner, 01.11). OrbitAmp's holds
        // its COMP alone (02.11), out until added, the least width growing by COMP's block with it.
        {
            auto t = make (bass);
            std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
            auto* editor = dynamic_cast<AmpEditor*> (ed.get());
            juce::String list;
            if (editor != nullptr)
                for (const auto& [row, in] : editor->checklistRows())
                    list << params::chainLinks[(size_t) row].name << (in ? "+ " : "- ");
            const int least0 = editor != nullptr ? editor->leastDesignWidth() : -1;
            bool follows = false;
            int least2 = -1, least1 = -1, w0 = ed->getWidth(), wBoth = -1;
            if (editor != nullptr)
            {
                settle (*t);
                editor->setLinkPresent (params::rowDelay, true);
                least1 = editor->leastDesignWidth();
                settle (*t);
                editor->setLinkPresent (params::rowReverb, true);
                least2 = editor->leastDesignWidth();
                wBoth  = ed->getWidth();
                int shownSpace = 0;
                walkAll (*ed, [&] (juce::Component& c)
                         {
                             if ((dynamic_cast<DelayBlock*> (&c) != nullptr || dynamic_cast<ReverbBlock*> (&c) != nullptr) && shownIn (c, *ed))
                                 ++shownSpace;
                         });
                settle (*t);
                t->history.undo();
                follows = shownSpace == 2 && ! t->linkShown (params::rowReverb) && t->linkShown (params::rowDelay)
                          && editor->leastDesignWidth() == least1 && wBoth > w0;
            }
            auto g = make (products::orbitAmp());
            std::unique_ptr<juce::AudioProcessorEditor> ged (g->createEditor());
            auto* gEditor = dynamic_cast<AmpEditor*> (ged.get());
            juce::String gList;
            int gLeast0 = -1, gLeast1 = -1;
            if (gEditor != nullptr)
            {
                for (const auto& [row, in] : gEditor->checklistRows())
                    gList << params::chainLinks[(size_t) row].name << (in ? "+ " : "- ");
                gLeast0 = gEditor->leastDesignWidth();
                gEditor->setLinkPresent (params::rowComp, true);
                gLeast1 = gEditor->leastDesignWidth();
            }
            check (list == "OCTAVE+ ENVELOPE+ CHORUS+ DELAY- REVERB- " && follows
                       && least0 == FaceplateView::bassLeastWidth (true, true, true, false, false, 0) + 2 * 48   // row one's (02.11)
                       && least1 >= least0 && least2 > least1   // one added: the cabinet's least may widen it (step 11)
                       && least2 == FaceplateView::bassLeastWidth (true, true, true, true, true, 0) + 2 * 48   // the face's row, and the IN and OUT columns
                       && gList == "COMP- " && gLeast0 == FaceplateView::designWidth
                       && gLeast1 == FaceplateView::designWidth + FaceplateView::compWidth + FaceplateView::colGap,
                   "the strip's checklist: " + list + "; a pick one undo step, the face and the least width following ("
                       + juce::String (least0) + ", one added " + juce::String (least1) + ", both " + juce::String (least2)
                       + "); OrbitAmp's " + gList + "(" + juce::String (gLeast0) + ", COMP added " + juce::String (gLeast1) + ")");
        }

        // THE FACE: row three in the signal's order at the least width and at 16:9, none overlapping, the cabinet never
        // under its least; every word of the pictures inside them; and NO GEAR on any frame — the right click is the menu,
        // on every block but the guards.
        {
            juce::StringArray bad;

            // THE CABINET'S IR LINE (review 8, both): appkit's IrWaveView writes it itself, from the IR — the same words for the
            // IR OrbitBass opens on, measured at 13 px, must fit the room under its waveform (16 px of its insets less).
            juce::String irLine;
            {
                auto probe = make (bass);
                const auto& bytes = AmpProcessor::cabIrBytes (bass, juce::roundToInt (value (*probe, params::cabIr)));
                juce::AudioFormatManager formats;
                formats.registerBasicFormats();
                if (bytes.data != nullptr)
                    if (std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (std::make_unique<juce::MemoryInputStream> (bytes.data, (size_t) bytes.size, false)));
                        r != nullptr && r->sampleRate > 0.0)
                    {
                        const int ch = (int) r->numChannels;
                        irLine = juce::String (juce::roundToInt ((double) r->lengthInSamples / r->sampleRate * 1000.0)) + "ms  |  "
                               + juce::String (r->sampleRate / 1000.0, 1) + "kHz  |  " + juce::String (r->bitsPerSample) + "-bit  |  "
                               + (ch == 1 ? "mono" : ch == 2 ? "stereo" : juce::String (ch) + "ch");
                    }
            }
            const bool irRead = irLine.isNotEmpty();
            // ...and the WORST line an IR the cabinet takes can give (review 9: a four-digit length, 192 kHz, 32-bit, stereo).
            const juce::String worstLine ("5000ms  |  192.0kHz  |  32-bit  |  stereo");   // the gate's own, not the face's probe
            const float irLineW = juce::jmax (juce::GlyphArrangement::getStringWidth (juce::Font (juce::FontOptions (13.0f)), irLine),
                                              juce::GlyphArrangement::getStringWidth (juce::Font (juce::FontOptions (13.0f)), worstLine));
            irLine << "\" and \"" << worstLine;

            for (const int mode : { 0, 1, 2 })   // default, both added, everything in and out
            {
                auto t = make (bass);
                if (mode >= 1)
                    both (*t, true);
                if (mode == 2)
                {
                    set (*t, params::octavePresent, 0.0f);
                    set (*t, params::chorusPresent, 0.0f);
                }
                std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
                auto* editor = dynamic_cast<AmpEditor*> (ed.get());
                if (editor == nullptr)
                {
                    bad.add ("no editor");
                    continue;
                }
                const int h = ed->getHeight();
                for (const int width : { -1, juce::roundToInt ((float) h * 16.0f / 9.0f), juce::roundToInt ((float) h * 2.2f) })
                {
                        ed->setSize (width < 0 ? 10 : width, h);   // narrower than the least: laid out at the least
                    const juce::String where = "mode " + juce::String (mode) + " at " + juce::String (ed->getWidth()) + " px: ";
                    std::vector<std::pair<int, juce::String>> row3;
                    juce::Component* face = nullptr;
                    walkAll (*ed, [&] (juce::Component& c)
                    {
                        if (dynamic_cast<FaceplateView*> (&c) != nullptr)
                            face = &c;
                        auto* b = dynamic_cast<BlockFrame*> (&c);
                        if (b == nullptr || ! shownIn (c, *ed) || face == nullptr || b->getParentComponent() != face)
                            return;
                        for (auto* child : b->getChildren())
                            if (child->getTitle() == "Menu")
                                bad.add (where + label (*b) + " wears a gear");
                        const auto* guard = dynamic_cast<GuardBlock*> (b);   // GATE and LIMIT: no picture (OCTAVE wears one)
                        if (b->hasBlockMenu() == (guard != nullptr && guard->pictureShown() == nullptr))   // the guards keep their own (the owner, 31.10)
                            bad.add (where + label (*b) + (b->hasBlockMenu() ? " — a guard — opens a block's menu" : " has no menu on its right click"));
                        if (b->getY() > 300 && face != nullptr && b->getParentComponent() == face)
                            row3.emplace_back (b->getX(), dynamic_cast<GuardBlock*> (b) != nullptr ? juce::String ("LIMIT")
                                                          : dynamic_cast<ChorusBlock*> (b) != nullptr ? juce::String ("CHORUS")
                                                          : dynamic_cast<CabinetBlock*> (b) != nullptr ? juce::String ("CAB")
                                                          : dynamic_cast<DelayBlock*> (b) != nullptr ? juce::String ("DELAY")
                                                          : dynamic_cast<ReverbBlock*> (b) != nullptr ? juce::String ("REVERB")
                                                          : juce::String ("?"));
                        // DELAY and REVERB are the guitar's own blocks (the owner, 01.11), fxRowWidth wide: never narrower.
                        if ((dynamic_cast<DelayBlock*> (b) != nullptr || dynamic_cast<ReverbBlock*> (b) != nullptr) && b->getWidth() != FaceplateView::fxRowWidth)
                            bad.add (where + label (*b) + " " + juce::String (b->getWidth()) + " wide, not " + juce::String (FaceplateView::fxRowWidth));
                        if (dynamic_cast<CabinetBlock*> (b) != nullptr && b->getWidth() < FaceplateView::bassCabLeast())
                            bad.add (where + "the cabinet " + juce::String (b->getWidth()) + " wide");
                        if (auto* cb = dynamic_cast<CabinetBlock*> (b); cb != nullptr && (! irRead || (float) (cb->waveView().getWidth() - 16) < irLineW))
                            bad.add (where + "the IR's line \"" + irLine + "\" (" + juce::String (irLineW, 1) + " px at 13 px) wider than the room under the waveform ("
                                     + juce::String (cb->waveView().getWidth() - 16) + " px, the cabinet " + juce::String (cb->getWidth()) + ")");
                        if (face != nullptr && b->getParentComponent() == face && ! face->getLocalBounds().contains (b->getBounds()))
                            bad.add (where + label (*b) + " outside the face");
                    });
                    std::sort (row3.begin(), row3.end());
                    juce::String order;
                    for (const auto& [x, n] : row3)
                        order << n << " ";
                    const juce::String want = mode == 0 ? "CHORUS CAB LIMIT " : mode == 1 ? "CHORUS DELAY REVERB CAB LIMIT " : "DELAY REVERB CAB LIMIT ";
                    if (order != want)
                        bad.add (where + "row three " + order);
                    for (size_t k = 1; k < row3.size(); ++k)
                        if (row3[k].first <= row3[k - 1].first)
                            bad.add (where + "row three overlaps");
                }
            }
            check (bad.isEmpty(), "row three in the signal's order — CHORUS, DELAY, REVERB, the cabinet, LIMIT — at the least width, 16:9 "
                                  "and 2.2:1, the cabinet never under its least, the IR's line \"" + irLine + "\" (" + juce::String (irLineW, 1)
                                  + " px at 13 px) under its waveform (the cabinet's least " + juce::String (FaceplateView::bassCabLeast())
                                  + ", the face's with both " + juce::String (FaceplateView::bassLeastWidth (true, true, true, true, true, 0)) + " + 2 x 48)"
                                  + ", the pictures' words inside them; no gear on any frame, a menu on every block's "
                                  "right click but the guards'" + (bad.isEmpty() ? juce::String() : ": " + bad.joinIntoString ("; ")));
        }
    }

    // ---- STEP 9b, NOW THE COMBO'S (step 14): EACH ITEM PICKED FROM ITS LIST, THE COMBO AND THE SQUARE LAND ON IT, TICKED --
    {
        auto t = make (bass);
        std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
        CompRatioCombo* combo = nullptr;
        CompTransfer* square = nullptr;
        std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
        {
            if (auto* s = dynamic_cast<CompRatioCombo*> (&c)) combo = s;
            if (auto* q = dynamic_cast<CompTransfer*> (&c))   square = q;
            for (auto* child : c.getChildren())
                walk (*child);
        };
        walk (*ed);
        juce::StringArray wrong;
        if (combo == nullptr || square == nullptr)
            wrong.add ("no ratio controls");
        else
            for (int k = 0; k < (int) std::size (compRatioItems); ++k)
            {
                const double d = compRatioItems[k];
                std::function<void (int)> answer;
                onfirst::catcher() = [&] (const juce::PopupMenu&, const std::function<void (int)>& a) { answer = a; return true; };
                combo->openList();
                onfirst::catcher() = nullptr;
                if (answer != nullptr)
                    answer (k + 1);
                const double onCombo = combo->shownRatio(), onSquare = square->shownRatio();
                juce::String ticked;
                const auto shownList = combo->list();
                for (juce::PopupMenu::MenuItemIterator it (shownList); it.next();)
                    if (it.getItem().isTicked)
                        ticked << it.getItem().text;
                if (onCombo != d || onSquare != d || combo->text() != CompRatioCombo::itemText (d) || ticked != CompRatioCombo::itemText (d))
                    wrong.add (CompRatioCombo::itemText (d) + " -> combo " + juce::String (onCombo, 2) + " \"" + combo->text() + "\", square "
                               + juce::String (onSquare, 2) + ", ticked " + ticked);
            }
        check (wrong.isEmpty(), "each of COMP's ratios picked from its list — 1.5, 2, 3, 4, 5, 7, 10 : 1 — the combo and the square land on it, "
                                "the list ticks it" + (wrong.isEmpty() ? juce::String() : ": " + wrong.joinIntoString ("; ")));
    }

    // ---- THE GUARDS KEEP THEIR SCALES AND THEIR MENUS (the owner, 31.10): no gear on GATE or LIMIT, their rails the
    //      guitar's less the 20 px the bass's rows are shorter, and their right click their own menus (LEARN), not a window.
    {
        const auto railOf = [] (juce::Component& root, const char* id)
        {
            GuardRail* found = nullptr;
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
            {
                if (auto* r = dynamic_cast<GuardRail*> (&c); r != nullptr && r->paramId() == id)
                    found = r;
                for (auto* child : c.getChildren())
                    walk (*child);
            };
            walk (root);
            return found;
        };
        auto b = make (bass);
        std::unique_ptr<juce::AudioProcessorEditor> bed (b->createEditor());
        auto g = make (products::orbitAmp());
        std::unique_ptr<juce::AudioProcessorEditor> ged (g->createEditor());
        FaceplateView* face = nullptr;
        std::function<void (juce::Component&)> find = [&] (juce::Component& c)
        {
            if (auto* f = dynamic_cast<FaceplateView*> (&c)) face = f;
            for (auto* child : c.getChildren())
                find (*child);
        };
        find (*bed);
        bool ok = face != nullptr && ! face->gateBlock().hasBlockMenu() && ! face->limitBlock().hasBlockMenu();
        juce::String said;
        for (const char* id : { params::gateThreshold, params::limiterCeiling })
        {
            auto* br = railOf (*bed, id);
            auto* gr = railOf (*ged, id);
            const int want = gr != nullptr ? gr->getHeight() - (FaceplateView::row1H - FaceplateView::bassFxRowH) : -1;
            ok = ok && br != nullptr && br->getHeight() == want;
            said << " " << id << " " << (br != nullptr ? br->getHeight() : -1) << " (" << want << ")";
        }
        if (face != nullptr && face->gateBlock().onMenu != nullptr)
            face->gateBlock().onMenu ({});
        ok = ok && face != nullptr && face->lastMenu.isEmpty();
        check (ok, "GATE and LIMIT without a block's menu, their rails at full height:" + said + "; GATE's right click its own menu, no block's");
    }

    // ---- REVIEW ROUND 8 (of steps 8-9) AND THE OWNER'S OUT ----------------------------------------------------
    {
        // THREE TRACES BEFORE THE MAKE-UP: at a steady squeeze AFTER COMP sits under BEFORE by the squeeze, and AFTER COMP +
        // BLEND is the blend's mix of the two — at 100 % and at half.
        for (const float blend : { 1.0f, 0.5f })
        {
            auto t = make (bass);
            set (*t, params::compAmount, blend);
            t->compWatched.store (true);
            (void) tone (*t, 120.0, -6.0f, 80);
            std::vector<core::CompTap::Column> cols (20);
            const int got = t->compTap.read (cols.data(), 20);
            double worstMid = got > 0 ? 0.0 : 99.0, worstOut = worstMid, gr = 0.0;
            for (int i = 0; i < got; ++i)
            {
                const auto& c = cols[(size_t) i];
                gr = (double) c.gr;
                const double mix = 20.0 * std::log10 ((1.0 - blend) + blend * std::pow (10.0, gr / 20.0));
                // A level of zero or not a number is no comparison: it counts as off by 99 dB (no std::max swallowing a NaN).
                const auto apartDb = [] (float num, float den, double want)
                {
                    const double d = 20.0 * std::log10 ((double) num / (double) den) - want;
                    return num > 0.0f && den > 0.0f && std::isfinite (d) ? std::abs (d) : 99.0;
                };
                worstMid = std::max (worstMid, apartDb (c.mid, c.in, gr));
                worstOut = std::max (worstOut, apartDb (c.out, c.in, mix));
            }
            check (worstMid <= 0.5 && worstOut <= 0.5, "COMP's three traces at BLEND " + juce::String (juce::roundToInt (blend * 100.0f)) + " %, "
                                                           + juce::String (gr, 1) + " dB squeezed: AFTER COMP under BEFORE by it within "
                                                           + juce::String (worstMid, 2) + " dB, AFTER + BLEND the mix within " + juce::String (worstOut, 2) + " dB");
        }

        // STANDING BY AT LOW NOTES (review 8's F3): nothing is written standing by any more — see STEP 10's six pictures.

        // THE THREE ON ONE BASIS (review 9): BEFORE is the signal COMP is handed, the full band as AFTER and AFTER + BLEND
        // — at B0 and E1, under the threshold (no squeeze), BLEND 100, either character, the three lie on each other.
        {
            double worst = 0.0;
            juce::String said;
            for (const double hz : { 30.9, 41.2 })
                for (const float character : { 0.0f, 1.0f })
                {
                    auto t = make (bass);
                    set (*t, params::compAmount, 1.0f);
                    set (*t, params::compCharacter, character);
                    t->compWatched.store (true);
                    (void) tone (*t, hz, -40.0f, 80);
                    std::vector<core::CompTap::Column> cols (20);
                    const int got = t->compTap.read (cols.data(), 20);
                    double here = got > 0 ? 0.0 : 99.0;
                    for (int i = 0; i < got; ++i)
                    {
                        const auto& c = cols[(size_t) i];
                        for (const float v : { c.mid, c.out })
                        {
                            const double d = 20.0 * std::log10 ((double) v / (double) c.in);
                            here = std::max (here, v > 0.0f && c.in > 0.0f && std::isfinite (d) && c.gr > -0.01f ? std::abs (d) : 99.0);
                        }
                    }
                    worst = std::max (worst, here);
                    said << " " << juce::String (hz, 1) << " Hz " << (character > 0.5f ? "PUNCHY " : "SMOOTH ") << juce::String (here, 3);
                }
            check (worst <= 0.1, "COMP's three traces on one basis: at B0 and E1 with no squeeze they lie on each other within 0.1 dB:" + said);
        }

        // THE MENU WORKS WITH ITS BLOCK STANDING BY: the AMP out, a right click on it — whatever is under the hand there, its
        // shield or its frame — still opens the AMP's menu.
        {
            auto t = make (bass);
            std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
            set (*t, params::preampOn, 0.0f);
            CapturedBlockPanel* panel = nullptr;
            FaceplateView* face = nullptr;
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
            {
                if (auto* p = dynamic_cast<CapturedBlockPanel*> (&c)) panel = p;
                if (auto* f = dynamic_cast<FaceplateView*> (&c)) face = f;
                for (auto* child : c.getChildren())
                    walk (*child);
            };
            walk (*ed);
            juce::Component* hit = nullptr;
            if (panel != nullptr && face != nullptr)
            {
                face->lastMenu = {};
                const juce::Point<int> corner (4, 4);
                hit = panel->getComponentAt (corner);
                if (hit != nullptr)
                {
                    const auto at = hit->getLocalPoint (panel, corner).toFloat();
                    hit->mouseDown (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at,
                                                      juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier), 0.0f,
                                                      juce::MouseInputSource::defaultOrientation, juce::MouseInputSource::defaultRotation,
                                                      juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY,
                                                      hit, hit, juce::Time::getCurrentTime(), at, juce::Time::getCurrentTime(), 1, false));
                }
            }
            check (hit != nullptr && face != nullptr && face->lastMenu == "AMP",
                   "the AMP standing by: a right click on it opens its menu (\"" + (face != nullptr ? face->lastMenu : juce::String()) + "\")");
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

    // ---- THE LOAD WINDOW: a guitar amp's as it was before the bass's COMP and a row for its own (02.11), the bass's four ---
    {
        LoadBreakdown gw (*guitar), bw (*amp);
        check (gw.getHeight() == 384 + 22 && bw.getHeight() == 384 + 4 * 22,
               "the load window: OrbitAmp's " + juce::String (gw.getHeight()) + " px (384 before, a row for its COMP), OrbitBass's " + juce::String (bw.getHeight())
                   + " (its DELAY and REVERB too, which a player may add; COMP, OCTAVE, ENVELOPE, CHORUS)");
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

    // ---- STEP 10: A PICTURE STANDS STILL WHEN ITS BLOCK IS OFF; BLEND LEVEL-NEUTRAL; REVIEW 8'S OPEN ITEMS ------------
    {
        using core::BassComp;
        const auto walk10 = [] (juce::Component& root, const std::function<void (juce::Component&)>& visit)
        {
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
            {
                visit (c);
                for (auto* child : c.getChildren())
                    walk (*child);
            };
            walk (root);
        };

        // A PICTURE STANDS STILL WHEN ITS BLOCK IS OFF (the owner, 31.10), all six: standing by (its strip arrow off) or
        // doing nothing (COMP at BLEND 0, an effect at 0) its tap writes nothing over 40 blocks of playing, and the picture
        // dims with its block; on again, alive. (This reverses step 9's "standing by, COMP's traces are the signal".)
        {
            struct Pic { const char* name; const char* on; const char* amount; float up; };
            const Pic pics[] = { { "COMP", params::compOn, params::compAmount, 0.6f },
                                 { "OCTAVE", params::octaveOn, params::octaveAmount, 0.6f },
                                 { "ENVELOPE", params::filterOn, params::filterAmount, 0.8f },
                                 { "CHORUS", params::chorusOn, params::chorusAmount, 0.6f } };
            // (The bass's DELAY and REVERB are the guitar's own blocks since 01.11, their pictures the guitar's.)
            juce::StringArray said;
            bool ok = true;
            for (const auto& b : pics)
            {
                auto t = make (bass);
                set (*t, params::delayPresent, 1.0f);
                set (*t, params::reverbPresent, 1.0f);
                set (*t, b.amount, b.up);
                set (*t, b.on, 1.0f);
                std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
                const juce::String name (b.name);
                juce::Component* pic = nullptr;
                walk10 (*ed, [&] (juce::Component& c)
                {
                    if (pic != nullptr)
                        return;
                    if ((name == "COMP" && dynamic_cast<CompGraph*> (&c) != nullptr) || (name == "OCTAVE" && dynamic_cast<OctaveScope*> (&c) != nullptr)
                        || (name == "ENVELOPE" && dynamic_cast<FilterCurve*> (&c) != nullptr)
                        || (name == "CHORUS" && dynamic_cast<ChorusPicture*> (&c) != nullptr))
                        pic = &c;
                });
                const auto alpha = [&]
                {
                    float a = 1.0f;
                    for (auto* c = pic; c != nullptr && dynamic_cast<BlockFrame*> (c) == nullptr; c = c->getParentComponent())
                        a *= c->getAlpha();
                    return pic != nullptr ? a : -1.0f;
                };
                const auto written = [&]
                {
                    return name == "COMP" ? t->compTap.written() : name == "OCTAVE" ? t->octaveTap->written()
                         : name == "ENVELOPE" ? t->filterTap.written() : name == "CHORUS" ? t->chorusTap.written()
                         : name == "DELAY" ? t->delayTap.written() : t->reverbTap.written();
                };
                for (auto* w : { &t->compWatched, &t->octaveWatched, &t->filterWatched, &t->chorusWatched, &t->delayWatched, &t->reverbWatched })
                    w->store (true);   // as on screen
                const auto over = [&] (int blocks)
                {
                    const auto w = written();
                    (void) play (*t, blocks, false);
                    return (int) (written() - w);
                };

                (void) over (20);
                const int   live = over (30);
                const float lit  = alpha();
                set (*t, b.on, 0.0f);
                (void) over (4);   // its fade out
                const int   standing = over (40);
                const float dimmed   = alpha();
                set (*t, b.on, 1.0f);
                (void) over (4);
                const int back = over (30);
                set (*t, b.amount, 0.0f);
                (void) over (4);
                const int   idle      = over (40);
                const float idleAlpha = alpha();   // doing nothing, switched on: dimmed as standing by (review 9)
                const bool fine = pic != nullptr && live > 0 && standing == 0 && back > 0 && idle == 0 && lit == 1.0f && dimmed < 1.0f
                                  && idleAlpha < 1.0f;
                ok = ok && fine;
                said.add (name + (fine ? "" : " (!)") + " " + juce::String (live) + " live, " + juce::String (standing) + " standing by (alpha "
                          + juce::String (dimmed, 2) + "), " + juce::String (back) + " on again, " + juce::String (idle) + " at 0 (alpha " + juce::String (idleAlpha, 2) + ")");
            }
            check (ok, "a picture stands still when its block is off — written while on, nothing standing by or at 0, dimmed with its "
                       "block: " + said.joinIntoString ("; "));
        }

        // BLEND IS LEVEL-NEUTRAL (the owner, 31.10: BLEND changes evenness, not loudness): a steady note at the reference,
        // -18 dBFS RMS, comes out at its own level at BLEND 0, 50 and 100 % in either character (PUNCHY's detector reading it
        // at its peak); a quieter one (-36) comes up, a louder one (-6) goes down — toward each other.
        {
            const auto gainDb = [] (double rmsDb, double blend, BassComp::Character ch, BassComp::Attack at, BassComp::Release rel, double hz,
                                    double thr = BassComp::defaultThresholdDb, double ratio = BassComp::defaultRatio)
            {
                BassComp c;
                (void) c.prepare (sampleRate, blockSize);
                c.snapAmount (blend);
                const double a = std::sqrt (2.0) * std::pow (10.0, rmsDb / 20.0);
                std::vector<float> x ((size_t) blockSize);
                double in2 = 0.0, out2 = 0.0;
                long long n = 0;
                for (int b = 0; b < 200; ++b)
                {
                    double sumIn = 0.0;
                    for (int i = 0; i < blockSize; ++i, ++n)
                    {
                        x[(size_t) i] = (float) (a * std::sin (juce::MathConstants<double>::twoPi * hz * (double) n / sampleRate));
                        sumIn += (double) x[(size_t) i] * (double) x[(size_t) i];
                    }
                    c.process (x.data(), blockSize, blend, core::BypassFade::Span { 1.0f, 1.0f, false, 0 }, at, rel, ch, thr, ratio);
                    if (b >= 100)
                    {
                        in2 += sumIn;
                        for (float v : x)
                            out2 += (double) v * (double) v;
                    }
                }
                return 10.0 * std::log10 (out2 / in2);
            };
            using C = BassComp::Character;
            using A = BassComp::Attack;
            using R = BassComp::Release;

            // THE WHOLE MATRIX (review 9): every CHARACTER x ATTACK x RELEASE, the reference note at A1..A4 within half a
            // decibel at BLEND 50 and 100 % (BLEND 0 is the signal bit for bit); B0 and E1 — the key's 30 Hz high-pass reads
            // them low — said, not judged.
            // Every character CompCharacters.h and core give COMP (the owner, 01.11: OPTO, FET and SQUASH beside SMOOTH and
            // PUNCHY), each its own row of BassComp::readsDb.
            static constexpr C everyCharacter[] { C::smooth, C::punchy, C::opto, C::fet, C::squash };
            static_assert (std::size (everyCharacter) == (size_t) BassComp::characterCount, "a character the matrix does not cover");
            const auto chName = [] (C c) { return c == C::smooth ? "SMOOTH" : c == C::punchy ? "PUNCHY" : c == C::opto ? "OPTO  " : c == C::fet ? "FET   " : "SQUASH"; };
            bool ok = true;
            double worst = 0.0, lowB = -99.0, lowE = -99.0;
            juce::String worstAt;
            std::printf ("      COMP, a -18 dBFS RMS note at BLEND 100 / 50, out dB:      31 Hz        41 Hz        55 Hz        82 Hz       110 Hz       220 Hz       440 Hz\n");
            for (const auto ch : everyCharacter)
                for (const auto at : { A::slow, A::med, A::fast })
                    for (const auto rel : { R::automatic, R::fast, R::slow })
                    {
                        const char* atName  = at == A::slow ? "SLOW" : at == A::med ? "MED " : "FAST";
                        const char* relName = rel == R::automatic ? "AUTO" : rel == R::fast ? "FAST" : "SLOW";
                        std::printf ("      %s attack %s release %s:", chName (ch), atName, relName);
                        for (const double hz : { 30.9, 41.2, 55.0, 82.4, 110.0, 220.0, 440.0 })
                        {
                            const double full = gainDb (-18.0, 1.0, ch, at, rel, hz), half = gainDb (-18.0, 0.5, ch, at, rel, hz);
                            std::printf ("  %+5.2f %+5.2f", full, half);
                            const bool finite = std::isfinite (full) && std::isfinite (half);
                            if (hz < 50.0)
                            {
                                (hz < 35.0 ? lowB : lowE) = std::max (hz < 35.0 ? lowB : lowE, finite ? std::max (std::abs (full), std::abs (half)) : 99.0);
                                continue;
                            }
                            const double off = finite ? std::max (std::abs (full), std::abs (half)) : 99.0;
                            if (off > worst)
                            {
                                worst   = off;
                                worstAt = juce::String (chName (ch)).trimEnd() + " " + atName + " attack " + relName + " release at "
                                        + juce::String (hz, 1) + " Hz";
                            }
                            ok = ok && off <= 0.5;
                        }
                        std::printf ("\n");
                    }

            // ...and where it leaves the rest: a quieter note comes up, a louder one goes down — toward each other.
            for (const auto ch : everyCharacter)
            {
                const double q50 = gainDb (-36.0, 0.5, ch, A::med, R::automatic, 110.0), q100 = gainDb (-36.0, 1.0, ch, A::med, R::automatic, 110.0);
                const double l50 = gainDb (-6.0, 0.5, ch, A::med, R::automatic, 110.0),  l100 = gainDb (-6.0, 1.0, ch, A::med, R::automatic, 110.0);
                ok = ok && q100 > q50 && q50 > 0.5 && l100 < l50 && l50 < -0.5;
            }
            check (ok, "COMP's BLEND level-neutral: a -18 dBFS RMS note at 55..440 Hz within 0.5 dB at BLEND 50 and 100 % for every "
                       "CHARACTER (all five) x ATTACK x RELEASE (worst " + juce::String (worst, 2) + " dB, " + worstAt + "); B0 off by up to "
                       + juce::String (lowB, 2) + " dB and E1 by " + juce::String (lowE, 2) + " (the key's 30 Hz high-pass, not judged); "
                       "a quiet note up, a loud one down");

            // ...AND ITS KNOWN LIMIT (BassComp::readsDb): the table is the defaults'; at the threshold and ratio's corners the
            // reference note drifts — printed, and held within 1.2 dB so a regression past that shows. The gentlest ratio,
            // 1.5:1 (step 14), at all three thresholds — and the stage works at it, not at 2:1.
            {
                bool held = true;
                double worstCorner = 0.0;
                juce::String worstCornerAt;
                for (const auto [thr, ratio] : { std::pair { -12.0, 2.0 }, std::pair { -24.0, 4.0 }, std::pair { -48.0, 10.0 },
                                                 std::pair { -12.0, 1.5 }, std::pair { -24.0, 1.5 }, std::pair { -48.0, 1.5 } })
                {
                    std::printf ("      COMP at %g dB, %g:1 — the reference note's worst at 55..440 Hz, BLEND 100 / 50, dB:\n", thr, ratio);
                    for (const auto ch : everyCharacter)
                    {
                        std::printf ("        %s", chName (ch));
                        for (const auto at : { A::slow, A::med, A::fast })
                            for (const auto rel : { R::automatic, R::fast, R::slow })
                            {
                                double full = 0.0, half = 0.0;
                                for (const double hz : { 55.0, 110.0, 440.0 })
                                {
                                    const double f = gainDb (-18.0, 1.0, ch, at, rel, hz, thr, ratio), h = gainDb (-18.0, 0.5, ch, at, rel, hz, thr, ratio);
                                    full = std::isfinite (f) && std::abs (f) > std::abs (full) ? f : (std::isfinite (f) ? full : 99.0);
                                    half = std::isfinite (h) && std::abs (h) > std::abs (half) ? h : (std::isfinite (h) ? half : 99.0);
                                }
                                std::printf ("  %c%c %+5.2f %+5.2f", "SMF"[(int) at], "AFS"[(int) rel], full, half);
                                const double off = std::max (std::abs (full), std::abs (half));
                                held = held && off <= 1.2;
                                if (off > worstCorner)
                                {
                                    worstCorner   = off;
                                    worstCornerAt = juce::String (thr, 0) + " dB " + juce::String (ratio, ratio == std::floor (ratio) ? 0 : 1) + ":1 "
                                                  + juce::String (chName (ch)).trimEnd();
                                }
                            }
                        std::printf ("\n");
                    }
                }
                BassComp gentle;
                (void) gentle.prepare (sampleRate, blockSize);
                gentle.snapAmount (1.0);
                std::vector<float> x ((size_t) blockSize, 0.1f);
                for (int b = 0; b < 20; ++b)
                    gentle.process (x.data(), blockSize, 1.0, core::BypassFade::Span { 1.0f, 1.0f, false, 0 }, A::med, R::automatic, C::smooth,
                                    -48.0, 1.5);
                const double atGentle = gentle.ratioNow();
                check (held && atGentle == 1.5 && gentle.makeupNow() > 0.0,
                       "COMP's BLEND at the threshold and ratio's corners (-12/2, -24/4, -48/10; -12, -24 and -48 at 1.5:1): the reference "
                       "note within 1.2 dB (worst " + juce::String (worstCorner, 2) + " dB, " + worstCornerAt + "; the table is the defaults'); "
                       "the stage at 1.5:1 works at " + juce::String (atGentle, 2) + ":1, its make-up " + juce::String (gentle.makeupNow(), 2) + " dB");
            }
        }

        // RE-ADDED ONE CALL AFTER ITS REMOVAL (review 2): a ringing DELAY, and a ringing REVERB, taken out of the strip in a
        // callback its 15 ms fade ends inside (1024 samples at 48 kHz), added again in the very next — each starts silent, its
        // old tail gone, as one that never had it. The chain before them a wire (the AMP standing by, COMP at 0, the cabinet
        // out), so a fresh stage has nothing left to repeat.
        {
            constexpr int big = 1024;
            juce::String said;
            bool ok = true;
            for (const bool rev : { false, true })
            {
                const char* present = rev ? params::reverbPresent : params::delayPresent;
                const auto run = [&] (bool with)
                {
                    auto t = make (bass);
                    set (*t, params::preampOn, 0.0f);
                    set (*t, params::compAmount, 0.0f);
                    set (*t, params::cabOn, 0.0f);
                    if (with)
                    {
                        set (*t, present, 1.0f);
                        set (*t, rev ? params::reverbMix : params::delayMix, 100.0f);
                        if (! rev)
                        {
                            set (*t, params::delayRepeats, 80.0f);
                            set (*t, params::delaySync, 0.0f);
                            set (*t, params::delayTimeMs, 200.0f);
                        }
                    }
                    t->prepareToPlay (sampleRate, big);
                    t->pumpDeviceWork();
                    juce::AudioBuffer<float> buf (2, big);
                    juce::MidiBuffer midi;
                    std::vector<float> out;
                    long long n = 0;
                    for (int b = 0; b < 71; ++b)
                    {
                        if (with && b == 40)
                            set (*t, present, 0.0f);   // out: the fade's 720 samples end inside this call
                        if (with && b == 41)
                            set (*t, present, 1.0f);   // ...and in again, the very next
                        for (int i = 0; i < big; ++i, ++n)
                        {
                            const auto v = b < 30 ? (float) (0.25 * std::sin (juce::MathConstants<double>::twoPi * 220.0 * (double) n / sampleRate)) : 0.0f;
                            buf.setSample (0, i, v);
                            buf.setSample (1, i, v);
                        }
                        t->processBlock (buf, midi);
                        t->pumpDeviceWork();
                        if (b >= 41)
                            out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + big);
                    }
                    return out;
                };
                // From the third call on: the first carries what the stages after it (the limiter's look-ahead) held of
                // the fade out; an old tail would be back within its first repeat (200 ms) or never gone (the room).
                const auto again = run (true), never = run (false);
                const float apart = worstApart (std::vector<float> (again.begin() + std::min<std::ptrdiff_t> (2 * big, (std::ptrdiff_t) again.size()), again.end()),
                                                std::vector<float> (never.begin() + std::min<std::ptrdiff_t> (2 * big, (std::ptrdiff_t) never.size()), never.end()));
                ok = ok && apart < 1.0e-4f;
                said << (rev ? "; REVERB " : " DELAY ") << juce::String (apart, 7);
            }
            check (ok, "re-added the call after its fade out ended: silent, no old tail (apart from one that never had it:" + said + ")");
        }

        // A GAP REACHES DELAY AND REVERB TOO (law 11c, review F4): ringing, then a gap of 40 blocks — each takes it as its own,
        // its channels stopped (core's law 11a): no old repeat after it. And one taken out of the strip just before a gap
        // fades through it and stops.
        {
            juce::String said;
            bool ok = true;
            for (const bool rev : { false, true })
            {
                const char* present = rev ? params::reverbPresent : params::delayPresent;
                const auto ringing = [&]
                {
                    auto t = make (bass);
                    set (*t, params::preampOn, 0.0f);
                    set (*t, params::compAmount, 0.0f);
                    set (*t, params::cabOn, 0.0f);
                    set (*t, present, 1.0f);
                    set (*t, rev ? params::reverbMix : params::delayMix, 100.0f);
                    if (! rev)
                    {
                        set (*t, params::delayRepeats, 80.0f);
                        set (*t, params::delaySync, 0.0f);
                        set (*t, params::delayTimeMs, 200.0f);
                    }
                    (void) tone (*t, 220.0, -12.0f, 30);
                    return t;
                };
                const auto gap = [] (AmpProcessor& a, int blocks)
                {
                    juce::AudioBuffer<float> none (0, blockSize);
                    juce::MidiBuffer midi;
                    for (int b = 0; b < blocks; ++b)
                        a.processBlock (none, midi);
                };
                auto t = ringing();
                const float before = peak (tone (*t, 220.0, -12.0f, 10, -1, {}, 0));
                gap (*t, 40);
                // (the first block after it carries what the stages after them — the limiter's look-ahead — held before it)
                const auto heard = tone (*t, 220.0, -12.0f, 30, -1, {}, 0);
                const float after = peakIn (heard, (size_t) 2 * blockSize, heard.size());

                auto u = ringing();
                const bool ranBefore = u->spaceRan (rev);
                set (*u, present, 0.0f);
                gap (*u, 4);
                const bool ranAfter = u->spaceRan (rev);
                ok = ok && before > 1.0e-2f && after < 1.0e-4f && ranBefore && ! ranAfter;
                said << (rev ? "; REVERB " : " DELAY ") << "ringing at " << juce::String (before, 4) << ", after the gap "
                     << juce::String (after, 7) << ", out of the strip through a gap " << (ranAfter ? "STILL RUNNING" : "stopped");
            }
            check (ok, "a gap reaches DELAY and REVERB:" + said);
        }

        // A RIGHT CLICK ON THE CABINET'S WAVEFORM IS ITS BLOCK'S MENU, AND ONLY THAT (review 8): the picture's own handler opens
        // the block's menu — no trim menu popping up beside it.
        {
            auto t = make (bass);
            std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
            CabinetBlock* cab = nullptr;
            FaceplateView* face = nullptr;
            walk10 (*ed, [&] (juce::Component& c)
            {
                if (auto* x = dynamic_cast<CabinetBlock*> (&c)) cab = x;
                if (auto* x = dynamic_cast<FaceplateView*> (&c)) face = x;
            });
            bool menu = false;
            if (cab != nullptr && face != nullptr)
            {
                // (an earlier gate's menus linger, dismissed or not, with no message loop to delete them: count windows)
                auto& wave = cab->waveView();
                const int windows = juce::Desktop::getInstance().getNumComponents();
                const auto at = wave.getLocalBounds().getCentre().toFloat();
                const auto now = juce::Time::getCurrentTime();
                wave.mouseDown (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at,
                                                  juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier), 0.0f,
                                                  juce::MouseInputSource::defaultOrientation, juce::MouseInputSource::defaultRotation,
                                                  juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY,
                                                  &wave, &wave, now, at, now, 1, false));
                menu = juce::Desktop::getInstance().getNumComponents() != windows;
            }
            check (face != nullptr && face->lastMenu == "CAB IR" && ! menu,
                   "a right click on the CAB's waveform opens the block's menu (" + (face != nullptr ? face->lastMenu : juce::String ("no face"))
                       + ")" + (menu ? ", and the trim menu beside it" : ", no trim menu beside it"));
        }
    }

    // ---- STEP 11: REVIEW ROUND 9 (of a60777c) ---------------------------------------------------------------------
    {
        const auto walk11 = [] (juce::Component& root, const std::function<void (juce::Component&)>& visit)
        {
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
            {
                visit (c);
                for (auto* child : c.getChildren())
                    walk (*child);
            };
            walk (root);
        };

        // NO CALLBACK REACHES THE PROCESSOR AFTER ITS FACE IS GONE (both reviewers): the load window let go, the processor
        // deleted — laid out, painted and clicked then, it does not touch the freed processor. (The settings windows are
        // gone, the owner, 01.11: a block's menu runs a pick only while its block stands — bass::show.)
        {
            auto t = make (bass);
            auto load = std::make_unique<LoadBreakdown> (*t);
            load->detach();
            t.reset();   // the processor gone
            juce::Image img (juce::Image::ARGB, 420, 420, true);
            {
                juce::Graphics g (img);
                load->paintEntireComponent (g, false);
            }
            const auto now = juce::Time::getCurrentTime();
            const juce::Point<float> at (20.0f, (float) load->getHeight() - 20.0f);
            load->mouseDown (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, juce::ModifierKeys(), 0.0f,
                                               juce::MouseInputSource::defaultOrientation, juce::MouseInputSource::defaultRotation,
                                               juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY,
                                               load.get(), load.get(), now, at, now, 1, false));
            check (load->isDetached(), "the load window lets go: painted and clicked after the processor went without reading it");
        }

        // SPACE SETTINGS THROUGH A GAP (review 5): DELAY's time moved 200 -> 400 ms and REVERB's decay moved just before a
        // long gap — each told at the gap, gliding through it as through silence of its length.
        {
            const auto after = [&] (bool gap)
            {
                auto t = make (bass);
                set (*t, params::delayPresent, 1.0f);
                set (*t, params::reverbPresent, 1.0f);
                set (*t, params::delaySync, 0.0f);
                set (*t, params::delayTimeMs, 200.0f);
                (void) tone (*t, 220.0, -12.0f, 20);
                set (*t, params::delayTimeMs, 400.0f);
                auto* decay = t->apvts.getParameter (params::reverbDecay);
                decay->setValueNotifyingHost (decay->getValue() > 0.5f ? 0.1f : 0.9f);
                t->pumpDeviceWork();
                if (gap)
                {
                    juce::AudioBuffer<float> none (0, blockSize);
                    juce::MidiBuffer midi;
                    for (int b = 0; b < 60; ++b)
                        t->processBlock (none, midi);
                }
                else
                {
                    (void) tone (*t, 220.0, -12.0f, 60, -1, {}, 0);
                }
                return std::pair<double, double> { (double) t->delayTaps().shownTimeMs(), (double) t->reverbTailSeconds() };
            };
            const auto [gapTime, gapTail]   = after (true);
            const auto [stillTime, stillTail] = after (false);
            check (stillTime > 210.0 && std::abs (gapTime - stillTime) < 1.0 && std::abs (gapTail - stillTail) <= 1.0e-3 * stillTail,
                   "DELAY's and REVERB's knobs reach them through a gap: the time gliding to 400 ms at " + juce::String (gapTime, 1)
                       + " ms (through silence " + juce::String (stillTime, 1) + "), the tail " + juce::String (gapTail, 2) + " s ("
                       + juce::String (stillTail, 2) + ")");
        }

        // OCTAVE'S FRAME FREEZES WHOLE (review 6): switched off on a held note, its frame — the samples, the lock, the reading —
        // stays exactly as it stood, however the tracker lets go after.
        {
            auto t = make (bass);
            set (*t, params::octaveAmount, 0.6f);
            std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
            OctaveScope* scope = nullptr;
            walk11 (*ed, [&] (juce::Component& c) { if (auto* s = dynamic_cast<OctaveScope*> (&c)) scope = s; });
            bool same = false;
            juce::String said = "no scope";
            if (scope != nullptr)
            {
                t->octaveWatched.store (true);
                (void) tone (*t, 41.2, -12.0f, 80);
                scope->pull();
                const auto a = scope->frame();
                const auto ra = scope->reading();
                set (*t, params::octaveOn, 0.0f);
                (void) tone (*t, 41.2, -12.0f, 60);
                (void) tone (*t, 41.2, -12.0f, 60, -1, {}, 0);   // ...and the note let go: the lock goes with it
                scope->pull();
                const auto b = scope->frame();
                const auto rb = scope->reading();
                same = a.subShown && a.count > 0 && a.count == b.count && juce::exactlyEqual (a.start, b.start)
                       && juce::exactlyEqual (a.span, b.span) && a.subShown == b.subShown && juce::exactlyEqual (a.noteHz, b.noteHz)
                       && ra == rb;
                said = "the sub " + juce::String (b.subShown ? "still drawn" : "GONE") + ", the reading " + rb.first + " / " + rb.second
                       + " (was " + ra.first + " / " + ra.second + "), " + juce::String (b.count) + " samples (" + juce::String (a.count) + ")";
            }
            check (same, "OCTAVE switched off on a held note: its frame stands whole — " + said);
        }

        // COMP'S TRACE BREAKS ACROSS STANDBY (review F6): playing, standing by, on again — the column after the gap starts a
        // new trace (two starts in all: the first column and the one after standby), nothing of the old hop in it.
        {
            auto t = make (bass);
            set (*t, params::compAmount, 0.6f);
            t->compWatched.store (true);
            (void) tone (*t, 110.0, -12.0f, 30);
            set (*t, params::compOn, 0.0f);
            (void) tone (*t, 110.0, -12.0f, 20);
            set (*t, params::compOn, 1.0f);
            (void) tone (*t, 110.0, -12.0f, 20);
            std::vector<core::CompTap::Column> cols (core::CompTap::columns);
            const int got = t->compTap.read (cols.data(), (int) cols.size());
            int starts = 0;
            for (int i = 0; i < got; ++i)
                starts += cols[(size_t) i].joined ? 0 : 1;
            check (got > 50 && (int) t->compTap.written() == got && starts == 2 && ! cols[0].joined,
                   "COMP's trace breaks across standby: " + juce::String (starts) + " starts in " + juce::String (got) + " columns");
        }
    }

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

    // ---- STEP 12: COMP REDESIGNED; NO SETTINGS WINDOWS — EVERY BLOCK'S GEAR AND RIGHT CLICK ITS MENU (the owner, 01.11) ----
    {
        const auto walk12 = [] (juce::Component& root, const std::function<void (juce::Component&)>& visit)
        {
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
            {
                visit (c);
                for (auto* child : c.getChildren())
                    walk (*child);
            };
            walk (root);
        };
        const auto settle = [] (AmpProcessor& a) { for (int i = 0; i < 40; ++i) a.history.tick(); };
        const auto mouse = [] (juce::Component& c, juce::Point<float> at, juce::Point<float> down, bool right = false)
        {
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at,
                                     right ? juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier) : juce::ModifierKeys(), 0.0f,
                                     juce::MouseInputSource::defaultOrientation, juce::MouseInputSource::defaultRotation,
                                     juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY,
                                     &c, &c, now, down, now, 1, at != down);
        };
        const auto compOf = [&] (juce::Component& root)
        {
            CompBlock* block = nullptr;
            walk12 (root, [&] (juce::Component& c) { if (auto* b = dynamic_cast<CompBlock*> (&c); b != nullptr && block == nullptr) block = b; });
            return block;
        };
        const auto railOf = [] (juce::Component& block, const char* id)
        {
            GuardRail* found = nullptr;
            for (auto* c : block.getChildren())
                if (auto* r = dynamic_cast<GuardRail*> (c); r != nullptr && r->paramId() == id)
                    found = r;
            return found;
        };
        // Columns written into COMP's tap as the audio thread writes them: `n` columns of each (in, mid, out) peak and squeeze.
        const auto writeColumns = [] (AmpProcessor& a, int n, float in, float mid, float out, float gr)
        {
            const int hop = (int) std::lround (sampleRate * core::CompTap::hopSeconds);
            std::vector<float> i ((size_t) hop, in), m ((size_t) hop, mid), o ((size_t) hop, out), g ((size_t) hop, gr);
            for (int k = 0; k < n; ++k)
                a.compTap.write (i.data(), m.data(), o.data(), g.data(), hop, false);
        };

        // THE PICTURE MOVES BY WHOLE PIXELS (step 13, the owner, 01.11: it flickered as it moved): a fixed run of columns, the
        // tap advanced 1 to 7 columns between frames — every complete pixel's values the same one whole shift later, at every
        // whole number of columns a pixel the widths give.
        juce::StringArray perSeen;
        for (const int width : { 64, 74, 86, 102, 127, 170, 252, 502 })
        {
            auto t = make (bass);
            CompGraph g (*t);
            g.setSize (width, 120);
            const int hop = (int) std::lround (sampleRate * core::CompTap::hopSeconds);
            int fed = 0;
            const auto feed = [&] (int n)
            {
                for (int k = 0; k < n; ++k, ++fed)
                {
                    const float v = 0.05f + 0.9f * (float) std::fmod (fed * 0.618034, 1.0);
                    std::vector<float> i ((size_t) hop, v), m ((size_t) hop, v * 0.5f), o ((size_t) hop, v * 0.25f), r ((size_t) hop, -24.0f * v);
                    t->compTap.write (i.data(), m.data(), o.data(), r.data(), hop, false);
                }
            };
            feed (900);
            g.pull();
            const int w = (int) g.plot().getWidth();
            auto before = g.bins (w);
            juce::StringArray wrong;
            int compared = 0;
            for (const int step : { 1, 2, 3, 4, 5, 6, 7, 6, 7, 3 })
            {
                const int was = fed;
                feed (step);
                g.pull();
                const auto after = g.bins (w);
                const int per = CompGraph::columnsPerPixel (w), shift = (fed - 1) / per - (was - 1) / per;
                int differ = 0;
                for (int x = shift; x < w - 1; ++x)   // the old newest pixel, still filling, may grow
                {
                    const auto& a = before[(size_t) x];
                    const auto& b = after[(size_t) (x - shift)];
                    ++compared;
                    if (! a.any || a.in != b.in || a.out != b.out || a.mid != b.mid || a.gr != b.gr || a.any != b.any || a.broken != b.broken)
                        ++differ;
                }
                if (differ > 0)
                    wrong.add ("+" + juce::String (step) + " columns: " + juce::String (differ) + " of " + juce::String (w - 1 - shift) + " pixels changed");
                before = after;
            }
            perSeen.add (juce::String (CompGraph::columnsPerPixel (w)));
            check (wrong.isEmpty() && compared > 300,
                   "COMP's picture moves by whole pixels: " + juce::String (CompGraph::columnsPerPixel (w)) + " columns a pixel at " + juce::String (w)
                   + " px, grouped from the tap's first, every complete pixel's values unchanged as it scrolls, 1 to 7 columns a frame ("
                   + juce::String (compared) + " compared" + (wrong.isEmpty() ? juce::String() : ": " + wrong.joinIntoString ("; ")) + ")");
        }
        check (perSeen.joinIntoString (",") == "8,7,6,5,4,3,2,1", "...at every whole number of columns a pixel, 8 down to 1 (" + perSeen.joinIntoString (",") + ")");

        // THE SPAN STAYS ABOUT 2.5 s (01.11): at the face's width as it opens, its least and its widest, the
        // plot's pixels times their columns — within what rounding the columns a pixel to a whole number can leave, half a
        // column a pixel.
        {
            auto t = make (bass);
            std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
            const int h = ed->getHeight();
            juce::StringArray spans;
            bool ok = true;
            for (const int width : { ed->getWidth(), 1, h * 10 })
            {
                ed->setSize (width, h);
                CompGraph* graph = nullptr;
                std::function<void (juce::Component&)> find = [&] (juce::Component& c)
                {
                    if (auto* x = dynamic_cast<CompGraph*> (&c)) graph = x;
                    for (auto* child : c.getChildren())
                        find (*child);
                };
                find (*ed);
                const int w = graph != nullptr ? (int) graph->plot().getWidth() : 0, per = CompGraph::columnsPerPixel (w), span = w * per;
                ok = ok && graph != nullptr && w > 0 && std::abs (span - CompGraph::spanColumns) * 2 <= w;
                spans.add (juce::String (w) + " px x " + juce::String (per) + " = " + juce::String (span * core::CompTap::hopSeconds, 2) + " s");
            }
            check (ok && CompGraph::spanColumns == 500, "COMP's picture shows about 2.5 s at every width: " + spans.joinIntoString (", "));
        }

        // THE LAYOUT, left to right: the threshold rail ("dB" over it), the picture, the ratio (its combo over the square), BLEND —
        // and nothing a player reads under 13 px; the threshold's number on its grip, the picture's orange box gone.
        {
            auto t = make (bass);
            std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
            auto* block = compOf (*ed);
            juce::StringArray wrong;
            if (block == nullptr)
                wrong.add ("no COMP");
            else
            {
                auto* thr = railOf (*block, params::compThreshold);
                auto* blend = railOf (*block, params::compAmount);
                auto& graph = block->picture();
                auto& combo = block->ratio();
                auto& square = block->square();
                juce::Label* unit = nullptr;
                for (auto* c : block->getChildren())
                    if (auto* l = dynamic_cast<juce::Label*> (c); l != nullptr && l->getText() == "dB")
                        unit = l;
                if (thr == nullptr || blend == nullptr || unit == nullptr)
                    wrong.add (juce::String ("missing:") + (thr == nullptr ? " the threshold rail" : "") + (blend == nullptr ? " BLEND" : "")
                               + (unit == nullptr ? " \"dB\"" : ""));
                else
                {
                    if (! (thr->getRight() <= graph.getX() && graph.getRight() <= combo.getX() && graph.getRight() <= square.getX()
                           && std::max (combo.getRight(), square.getRight()) <= blend->getX() && combo.getBottom() <= square.getY()))
                        wrong.add ("the order: threshold " + thr->getBounds().toString() + ", picture " + graph.getBounds().toString()
                                   + ", ratio " + combo.getBounds().toString() + " over the square " + square.getBounds().toString()
                                   + ", BLEND " + blend->getBounds().toString());
                    if (! (unit->getBottom() <= thr->getY() && unit->getX() <= thr->getX() && unit->getRight() >= thr->getRight()))
                        wrong.add ("\"dB\" " + unit->getBounds().toString() + " not over the rail " + thr->getBounds().toString());
                    // BLEND a fader (the owner, 01.11): its number under it at fader::textH, with its unit.
                    if (unit->getFont().getHeight() < 13.0f || thr->gripTextHeight < 13.0f || ! blend->hasFaderLook() || fader::textH < 13.0f
                        || CompGraph::textHeight < 13.0f || CompRatioCombo::textHeight < 13.0f)
                        wrong.add ("type under 13 px");
                    if (thr->gripText() != "-24" || blend->gripText() != "50%")
                        wrong.add ("the grips read " + thr->gripText() + " and " + blend->gripText());
                    if (thr->typeDecimals < 0)
                        wrong.add ("the threshold's number is not typed");
                    for (auto* c : block->getChildren())
                        if (c->isVisible() && ! block->getLocalBounds().contains (c->getBounds()))
                            wrong.add (c->getBounds().toString() + " outside the block");
                    // No orange on the picture with nothing played: the threshold's box has left it.
                    const auto img = graph.createComponentSnapshot (graph.getLocalBounds(), true, 1.0f);
                    int orange = 0;
                    for (int y = 0; y < img.getHeight(); ++y)
                        for (int x = 0; x < img.getWidth(); ++x)
                        {
                            const auto p = img.getPixelAt (x, y);
                            orange += std::abs (p.getRed() - theme::orange.getRed()) + std::abs (p.getGreen() - theme::orange.getGreen())
                                    + std::abs (p.getBlue() - theme::orange.getBlue()) < 90 ? 1 : 0;
                        }
                    if (orange > 0)
                        wrong.add (juce::String (orange) + " orange pixels on the silent picture");
                }
            }
            check (wrong.isEmpty(), "COMP left to right: the threshold rail under \"dB\", the picture, the ratio, BLEND; 13 px; the threshold's "
                                    "number on its grip, typed, the picture's box gone (the picture " + juce::String (block != nullptr ? block->picture().getWidth() : 0)
                                    + " px wide)" + (wrong.isEmpty() ? juce::String() : ": " + wrong.joinIntoString ("; ")));
        }

        // THE THRESHOLD RAIL AND THE LINE ARE ONE CONTROL, BOTH WAYS: the rail's hand at the line's height over the whole
        // scale; the rail dragged (paused past the settle) moves the line, switches COMP in, one undo gives both back; the
        // line dragged moves the rail's hand and number, one undo; a typed number one undo.
        {
            auto t = make (bass);
            set (*t, params::compThreshold, -24.0f);
            set (*t, params::compOn, 0.0f);
            settle (*t);
            std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
            auto* block = compOf (*ed);
            auto* thr = block != nullptr ? railOf (*block, params::compThreshold) : nullptr;
            juce::String said;
            bool ok = thr != nullptr;
            if (ok)
            {
                auto& g = block->picture();
                const auto railY = [&] (float db) { return (float) thr->getY() + thr->yOfValue (db); };
                const auto lineY = [&] { return (float) g.getY() + g.lineY(); };
                float worst = 0.0f;
                for (const float db : { -48.0f, -40.0f, -24.0f, -12.0f, -2.0f, 0.0f })
                    worst = std::max (worst, std::abs (railY (db) - ((float) g.getY() + g.yOfDb ((double) db))));

                const juce::Point<float> from ((float) thr->getWidth() * 0.5f, thr->yOfValue (-24.0f)), to (from.x, thr->yOfValue (-30.0f));
                thr->mouseDown (mouse (*thr, from, from));
                thr->mouseDrag (mouse (*thr, (from + to) * 0.5f, from));
                settle (*t);
                thr->mouseDrag (mouse (*thr, to, from));
                thr->mouseUp (mouse (*thr, to, from));
                settle (*t);
                const float byRail = value (*t, params::compThreshold);
                const bool railMoves = std::abs (byRail + 30.0f) < 0.2f && std::abs (lineY() - railY (byRail)) <= 0.5f && value (*t, params::compOn) > 0.5f;
                t->history.undo();
                const bool railUndone = std::abs (value (*t, params::compThreshold) + 24.0f) < 1.0e-3f && value (*t, params::compOn) < 0.5f;

                g.beginLineDrag();
                g.dragLineTo (g.yOfDb (-16.0));
                settle (*t);
                g.dragLineTo (g.yOfDb (-12.0));
                g.endLineDrag();
                settle (*t);
                const float byLine = value (*t, params::compThreshold);
                const bool lineMoves = std::abs (byLine + 12.0f) < 0.2f && thr->gripText() == "-12" && std::abs (lineY() - railY (byLine)) <= 0.5f;
                t->history.undo();
                const bool lineUndone = std::abs (value (*t, params::compThreshold) + 24.0f) < 1.0e-3f;

                thr->typeValue (-18.0f);
                settle (*t);
                const bool typed = std::abs (value (*t, params::compThreshold) + 18.0f) < 1.0e-3f;
                t->history.undo();
                const bool typedUndone = std::abs (value (*t, params::compThreshold) + 24.0f) < 1.0e-3f;
                ok = worst <= 0.5f && railMoves && railUndone && lineMoves && lineUndone && typed && typedUndone;
                said << ": the hand off the line by " << juce::String (worst, 2) << " px at worst; the rail dragged to " << juce::String (byRail, 2)
                     << " dB (" << (railMoves ? "the line with it, COMP in" : "NOT followed") << "), " << (railUndone ? "one undo back" : "NOT undone in one")
                     << "; the line dragged to " << juce::String (byLine, 2) << " (the grip reads " << thr->gripText() << ", "
                     << (lineMoves ? "followed" : "NOT followed") << "), " << (lineUndone ? "one undo back" : "NOT undone in one")
                     << "; typed -18 " << (typed && typedUndone ? "one undo step" : "NOT one undo step");
            }
            check (ok, "COMP's threshold rail and line are one control both ways" + said);
        }

        // THE THREE TRACES AND THE SQUEEZE'S LINE, PAINTED: AFTER COMP a solid grey line, no gap along it; the squeeze's line
        // coloured by its depth — light violet at 0, half way at -6, orange at -12 and at -20, as grColour says; its number's
        // plate centred on the strip's border.
        {
            auto t = make (bass);
            set (*t, params::compThreshold, -40.0f);
            std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
            auto* block = compOf (*ed);
            juce::StringArray wrong;
            const float gr[] { 0.0f, -6.0f, -12.0f, -20.0f };
            const float wantT[] { 0.0f, 0.5f, 1.0f, 1.0f };
            for (int k = 0; k < 4; ++k)
            {
                const auto want = theme::violet.brighter (0.3f).interpolatedWith (theme::orange, wantT[k]);
                if (CompGraph::grColour (gr[k]) != want)
                    wrong.add ("grColour (" + juce::String (gr[k], 0) + ") " + CompGraph::grColour (gr[k]).toDisplayString (false)
                               + ", not " + want.toDisplayString (false));
            }
            if (block == nullptr)
                wrong.add ("no COMP");
            else
            {
                auto& g = block->picture();
                // A picture full from its left edge (it shows ~2.5 s, whole columns a pixel): older columns first.
                writeColumns (*t, 300, 0.5f, 0.12589f, 0.0625f, 0.0f);
                writeColumns (*t, 300, 0.5f, 0.12589f, 0.0625f, 0.0f);   // -6, -18, -24 dB
                writeColumns (*t, 60, 0.5f, 0.12589f, 0.0625f, -6.0f);
                writeColumns (*t, 60, 0.5f, 0.12589f, 0.0625f, -12.0f);
                writeColumns (*t, 80, 0.5f, 0.12589f, 0.0625f, -20.0f);
                g.pull();
                const auto img = g.createComponentSnapshot (g.getLocalBounds(), true, 1.0f);
                const int w = (int) g.plot().getWidth();
                const auto plate = g.grNumberBox();
                const auto strip = g.grStrip();
                const juce::Colour well (0xff121218);
                const auto dist = [] (juce::Colour a, juce::Colour b)
                {
                    return (float) (juce::square (a.getRed() - b.getRed()) + juce::square (a.getGreen() - b.getGreen()) + juce::square (a.getBlue() - b.getBlue()));
                };
                // The squeeze's colour read off the line: the strongest pixel of the column, fitted as the line's colour at
                // some cover over the well.
                const int sampleAt[] { 150, 330, 390, 465 };
                for (int k = 0; k < 4; ++k)
                {
                    const int px = 1 + g.pixelOfAge (499 - sampleAt[k], w);
                    const int lowest = (float) px >= plate.getX() - 2.0f && (float) px <= plate.getRight() + 2.0f ? (int) plate.getY() - 1 : (int) strip.getBottom();
                    juce::Colour best = well;
                    for (int y = (int) strip.getY(); y < lowest; ++y)
                        if (dist (img.getPixelAt (px, y), well) > dist (best, well))
                            best = img.getPixelAt (px, y);
                    float bestErr = 1.0e9f, fitT = -1.0f;
                    for (int ti = 0; ti <= 50; ++ti)
                        for (int ai = 10; ai <= 50; ++ai)
                        {
                            const auto c = CompGraph::grColour (0.0f).interpolatedWith (CompGraph::grColour (-12.0f), (float) ti / 50.0f);
                            const auto mixed = well.interpolatedWith (c, (float) ai / 50.0f);
                            if (const float e = dist (mixed, best); e < bestErr)
                                bestErr = e, fitT = (float) ti / 50.0f;
                        }
                    if (std::abs (fitT - wantT[k]) > 0.12f || dist (best, well) < 900.0f)
                        wrong.add ("the line at " + juce::String (gr[k], 0) + " dB reads " + best.toDisplayString (false) + " (t " + juce::String (fitT, 2)
                                   + ", want " + juce::String (wantT[k], 2) + ") at x " + juce::String (px));
                }
                // AFTER COMP: at every pixel along it a grey one, at -18 dB — solid, no dash.
                const float yMid = g.yOfDb (-18.0);
                int holes = 0, firstHole = -1;
                for (int x = 3; x < w - 2; ++x)
                {
                    bool grey = false;
                    for (int y = (int) std::floor (yMid) - 1; y <= (int) std::floor (yMid) + 2; ++y)
                    {
                        const auto p = img.getPixelAt (x, y);
                        const int hi = std::max ({ p.getRed(), p.getGreen(), p.getBlue() }), lo = std::min ({ p.getRed(), p.getGreen(), p.getBlue() });
                        grey = grey || (hi >= 0x50 && hi - lo <= 30);
                    }
                    if (! grey && holes++ == 0)
                        firstHole = x;
                }
                if (holes > 0)
                    wrong.add ("AFTER COMP not a solid grey line: " + juce::String (holes) + " of " + juce::String (w - 5) + " pixels without it (first at x "
                               + juce::String (firstHole) + ")");
                if (std::abs (plate.getCentreY() - g.stripBorderY()) > 0.5f || plate.getY() >= strip.getBottom() || plate.getBottom() <= g.levels().getY())
                    wrong.add ("the squeeze's number " + plate.toString() + " not on the border at " + juce::String (g.stripBorderY(), 1));
                if (g.grText() != "-20 dB")
                    wrong.add ("the squeeze's number reads " + g.grText());
            }
            check (wrong.isEmpty(), "COMP's picture: AFTER COMP a solid grey line; the squeeze's line light violet at 0, toward orange at -6, orange at -12 "
                                    "and -20; its number on the strip's border" + (wrong.isEmpty() ? juce::String() : ": " + wrong.joinIntoString ("; ")));
        }

        // COMP'S MENU: exactly ATTACK, RELEASE and CHARACTER, each a submenu of its choices, each pick setting its parameter.
        {
            auto t = make (bass);
            auto m = bass::compMenu (t->apvts);
            // Its CHARACTER is the combo on its border since 02.11 (proved below), not in the menu.
            const std::pair<const char*, const char*> want[] { { "ATTACK", params::compAttack }, { "RELEASE", params::compRelease } };
            const juce::StringArray words[] { { "SLOW", "MED", "FAST" }, { "AUTO", "FAST", "SLOW" } };
            juce::StringArray wrong;
            int k = 0;
            for (juce::PopupMenu::MenuItemIterator it (m.items); it.next(); ++k)
            {
                const auto& item = it.getItem();
                if (k >= 2 || item.text != want[k].first || item.subMenu == nullptr)
                {
                    wrong.add ("item " + juce::String (k) + " \"" + item.text + "\"" + (item.subMenu == nullptr ? " (no submenu)" : ""));
                    continue;
                }
                int j = 0;
                for (juce::PopupMenu::MenuItemIterator sub (*item.subMenu); sub.next(); ++j)
                {
                    const auto& s = sub.getItem();
                    const bool known = j < words[k].size() && s.text == words[k][j] && s.itemID > 0 && s.itemID <= (int) m.acts.size();
                    if (known)
                        m.acts[(size_t) s.itemID - 1]();
                    if (! known || juce::roundToInt (value (*t, want[k].second)) != j)
                        wrong.add (juce::String (want[k].first) + " > " + s.text);
                }
                if (j != words[k].size())
                    wrong.add (juce::String (want[k].first) + " has " + juce::String (j) + " items");
            }
            check (wrong.isEmpty() && k == 2, "COMP's menu: exactly ATTACK and RELEASE, submenus of their choices, each pick its parameter ("
                                                  + juce::String (k) + " items" + (wrong.isEmpty() ? juce::String() : ": " + wrong.joinIntoString ("; ")) + ")");
        }

        // COMP'S CHARACTER ON ITS BORDER (the owner, 02.11): "COMP —— OPTO ⌄", a combo of every character, OPTO standing;
        // a pick one undo step, the sound's parameter with it; set from elsewhere, the combo follows.
        {
            auto t = make (bass);
            for (int i = 0; i < 40; ++i)
                t->history.tick();
            std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
            VoicingSelector* combo = nullptr;
            if (auto* block = compOf (*ed))
                walk12 (*block, [&] (juce::Component& c) { if (auto* v = dynamic_cast<VoicingSelector*> (&c); v != nullptr && combo == nullptr) combo = v; });
            juce::String said = "no combo on COMP's border";
            bool ok = false;
            if (combo != nullptr && combo->onPick != nullptr)
            {
                const int opto = params::compCharacters.indexOf ("Opto"), fet = params::compCharacters.indexOf ("FET");
                const int startsAt = combo->getSelection();
                const int depth = t->history.undoDepth();
                combo->onPick (fet);
                for (int i = 0; i < 40; ++i)
                    t->history.tick();
                const int picked = juce::roundToInt (value (*t, params::compCharacter));
                const int steps  = t->history.undoDepth() - depth;
                t->history.undo();
                for (int i = 0; i < 40; ++i)
                    t->history.tick();
                const int undone = juce::roundToInt (value (*t, params::compCharacter));
                set (*t, params::compCharacter, (float) params::compCharacters.indexOf ("Squash"));
                const int follows = combo->getSelection();
                ok = combo->getCount() == params::compCharacters.size() && startsAt == opto && picked == fet && steps == 1 && undone == opto
                     && follows == params::compCharacters.indexOf ("Squash");
                said = juce::String (combo->getCount()) + " characters, standing on " + juce::String (startsAt) + "; FET picked -> "
                       + juce::String (picked) + " in " + juce::String (steps) + " step(s), undone to " + juce::String (undone)
                       + "; SQUASH set elsewhere, the combo on " + juce::String (follows);
            }
            check (ok, "COMP's character a combo on its border, OPTO by default: " + said);
        }

        // THE MENUS: no window anywhere, no gear on any frame — a right click on a block (and on COMP's every control) opens
        // its menu; every pick sets what it says, one undo step; together a block's picks reach exactly what its
        // window held, and a value off the steps shows ticked as "(custom)".
        {
            auto t = make (bass);
            set (*t, params::delayPresent, 1.0f);
            set (*t, params::reverbPresent, 1.0f);
            settle (*t);
            std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
            FaceplateView* face = nullptr;
            walk12 (*ed, [&] (juce::Component& c) { if (auto* f = dynamic_cast<FaceplateView*> (&c)) face = f; });
            juce::StringArray wrong, opened;
            if (face == nullptr)
                wrong.add ("no face");
            else
                for (auto* c : face->getChildren())
                {
                    auto* b = dynamic_cast<BlockFrame*> (c);
                    if (b == nullptr || ! b->isVisible() || (dynamic_cast<GuardBlock*> (b) != nullptr && dynamic_cast<GuardBlock*> (b)->pictureShown() == nullptr))
                        continue;   // GATE and LIMIT keep their own menus
                    for (auto* child : b->getChildren())
                        if (child->getTitle() == "Menu")
                            wrong.add (b->getName() + " wears a gear");
                    face->lastMenu = {};
                    const juce::Point<float> corner (4.0f, 4.0f);
                    b->mouseDown (mouse (*b, corner, corner, true));
                    if (face->lastMenu.isEmpty())
                        wrong.add (b->getName() + ": a right click opened no menu");
                    opened.add (face->lastMenu);
                }
            if (auto* block = compOf (*ed); block != nullptr && face != nullptr)
                for (juce::Component* c : { (juce::Component*) &block->picture(), (juce::Component*) &block->ratio(), (juce::Component*) &block->square(),
                                            (juce::Component*) &block->threshold(), (juce::Component*) &block->fader() })
                {
                    face->lastMenu = {};
                    const auto at = c->getLocalBounds().getCentre().toFloat();
                    c->mouseDown (mouse (*c, at, at, true));
                    c->mouseUp (mouse (*c, at, at, true));
                    if (face->lastMenu != "COMP")
                        wrong.add ("a right click on COMP's " + juce::String (typeid (*c).name()) + " opened \"" + face->lastMenu + "\"");
                }
            opened.sort (false);
            if (opened.joinIntoString (",") != "AMP,CAB IR,CHORUS,COMP,DELAY,ENVELOPE,OCTAVE,REVERB")
                wrong.add ("the right clicks opened " + opened.joinIntoString (","));

            // ...and its ON at the top (step 13).
            const std::map<juce::String, juce::StringArray> held {
                // COMP's CHARACTER is the combo on its border (02.11); OCTAVE's TONE, ENVELOPE's RELEASE and CHORUS's RATE are
                // rails on their blocks, ENVELOPE's RANGE and CHORUS's crossover lists on their pictures (01.11).
                { "COMP",     { params::compOn, params::compAttack, params::compRelease } },
                { "OCTAVE",   { params::octaveOn } },
                { "ENVELOPE", { params::filterOn, params::filterMode, params::filterDirection, params::filterResonance,
                                params::filterKeepLows, params::filterKeepHz } },
                { "CHORUS",   { params::chorusOn } },
                { "DELAY",    { params::delayOn, params::delaySync, params::delayDiv, params::delayTimeMs, params::delayHostTempo, params::delayBpm,
                                params::delayRepeats, params::delayDark, params::delayOffset } },
                { "REVERB",   { params::reverbOn, params::reverbType, params::reverbDecay, params::reverbPredelay } },
                { "AMP",      { params::preampOn, params::ampBlendHz, params::ampBlendInvert, params::bassHpfOn, params::bassHpf, params::bassHpfHz5,
                                params::bassHpfHz4, params::bassLpfOn, params::bassLpf, params::bassLpfHzFingers, params::bassLpfHzSlap } },
                { "CAB IR",   { params::cabOn, params::diMix, params::cabTrimOn, params::cabTrim } } };
            const auto& all = t->getParameters();
            const auto snapshot = [&all]
            {
                std::vector<float> v;
                for (auto* p : all)
                    v.push_back (p->getValue());
                return v;
            };
            int picks = 0;
            {
                auto fresh = make (bass);   // every default among the steps: nothing custom on a fresh one
                for (const auto& [name, ids] : held)
                {
                    auto m = bass::blockMenu (fresh->apvts, name);
                    for (juce::PopupMenu::MenuItemIterator it (m.items, true); it.next();)
                        if (it.getItem().text.endsWith ("(custom)"))
                            wrong.add (name + " shows its default as " + it.getItem().text);
                }
            }
            for (const auto& [name, ids] : held)
            {
                auto m = bass::blockMenu (t->apvts, name);
                juce::StringArray reached;
                for (juce::PopupMenu::MenuItemIterator it (m.items, true); it.next();)
                {
                    const auto& item = it.getItem();
                    if (item.subMenu != nullptr || item.itemID <= 0 || ! item.isEnabled)
                        continue;
                    if (item.text == "RESET")   // every parameter of the block's: proved on its own below
                        continue;
                    const auto before = snapshot();
                    if (item.itemID > (int) m.acts.size() || ! m.acts[(size_t) item.itemID - 1])
                    {
                        wrong.add (name + " > " + item.text + " does nothing");
                        continue;
                    }
                    m.acts[(size_t) item.itemID - 1]();
                    ++picks;
                    settle (*t);
                    juce::StringArray moved;
                    for (int i = 0; i < all.size(); ++i)
                        if (all[i]->getValue() != before[(size_t) i])
                            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (all[i]))
                                moved.add (withId->getParameterID());
                    reached.mergeArray (moved);
                    if (moved.isEmpty() && ! item.isTicked)
                        wrong.add (name + " > " + item.text + " moved nothing");
                    if (! moved.isEmpty())
                    {
                        t->history.undo();
                        const auto after = snapshot();
                        for (int i = 0; i < all.size(); ++i)
                            if (std::abs (after[(size_t) i] - before[(size_t) i]) > 1.0e-6f)
                            {
                                wrong.add (name + " > " + item.text + ": one undo left " + all[i]->getName (40) + " moved");
                                break;
                            }
                    }
                }
                reached.sort (false);
                auto want = ids;
                want.sort (false);
                if (reached != want)
                    wrong.add (name + " reaches " + reached.joinIntoString (" ") + " (its window held " + want.joinIntoString (" ") + ")");
            }
            set (*t, params::filterResonance, 5.0f);
            bool custom = false;
            {
                auto m = bass::filterMenu (t->apvts);
                for (juce::PopupMenu::MenuItemIterator it (m.items, true); it.next();)
                    custom = custom || (it.getItem().text == "Q 5 (custom)" && it.getItem().isTicked && ! it.getItem().isEnabled);
            }
            if (! custom)
                wrong.add ("ENVELOPE's resonance at Q 5 not shown as custom");

            // RESET AT EVERY MENU'S FOOT (the owner, 01.11): every parameter of the block back to its default in one
            // instant, one undo step — its switch, its place in the strip and the AMP's channel kept.
            for (const auto& [name, ids] : held)
            {
                auto r = make (bass);
                settle (*r);
                const auto resetIds = bass::resetIdsOf (r->apvts, name);
                if (resetIds.isEmpty())
                {
                    wrong.add (name + "'s RESET sets nothing back");
                    continue;
                }
                for (const auto& id : resetIds)
                    if (id.endsWith ("_on") || id.endsWith ("_present") || id.endsWith ("_device"))
                        wrong.add (name + "'s RESET takes " + id);
                std::map<juce::String, float> moved;
                for (const auto& id : resetIds)
                {
                    auto* p = r->apvts.getParameter (id);
                    p->setValueNotifyingHost (p->getDefaultValue() < 0.5f ? 0.9f : 0.1f);
                }
                const char* onId = bass::onParamOf (name);
                auto* on = onId != nullptr ? r->apvts.getParameter (onId) : nullptr;
                const float onBefore = on != nullptr ? on->getValue() : 0.0f;
                settle (*r);
                for (const auto& id : resetIds)   // as they stand once settled: what one undo must bring back
                    moved[id] = r->apvts.getParameter (id)->getValue();
                const int depth = r->history.undoDepth();
                auto m = bass::blockMenu (r->apvts, name);
                int resetId = 0;
                for (juce::PopupMenu::MenuItemIterator it (m.items); it.next();)
                    if (it.getItem().text == "RESET")
                        resetId = it.getItem().itemID;
                if (resetId <= 0 || resetId > (int) m.acts.size())
                {
                    wrong.add (name + "'s menu has no RESET");
                    continue;
                }
                m.acts[(size_t) resetId - 1]();
                settle (*r);
                juce::StringArray off;
                for (const auto& id : resetIds)
                    if (auto* p = r->apvts.getParameter (id); std::abs (p->getValue() - p->getDefaultValue()) > 1.0e-6f)
                        off.add (id);
                const int steps = r->history.undoDepth() - depth;
                r->history.undo();
                settle (*r);
                int back = 0;
                juce::StringArray stuck;
                for (const auto& id : resetIds)
                    if (auto* p = r->apvts.getParameter (id);
                        p->isDiscrete() ? p->getText (p->getValue(), 64) != p->getText (moved[id], 64)   // a switch's 0.9 is its 1
                                        : std::abs (p->getValue() - moved[id]) > 1.0e-4f)
                    {
                        ++back;
                        stuck.add (id + " " + juce::String (moved[id], 3) + " -> " + juce::String (r->apvts.getParameter (id)->getValue(), 3));
                    }
                if (! off.isEmpty() || steps != 1 || back != 0 || (on != nullptr && on->getValue() != onBefore))
                    wrong.add (name + "'s RESET: " + (off.isEmpty() ? juce::String ("all back") : "left " + off.joinIntoString (" ")) + ", "
                               + juce::String (steps) + " undo step(s), " + juce::String (back) + " not undone"
                               + (stuck.isEmpty() ? juce::String() : " (" + stuck.joinIntoString (", ") + ")"));
            }
            check (wrong.isEmpty() && picks > 100, "no windows, no gears: a right click opens every block's menu; " + juce::String (picks)
                                                       + " picks, each its own and one undo step; each menu what its window held; a value off the steps ticked as custom"
                                                       + (wrong.isEmpty() ? juce::String() : ": " + wrong.joinIntoString ("; ")));
        }
    }

    // ---- GATE'S OFF IS ITS RAIL'S END STOP (the owner, 01.11): no OFF zone under the scale — OrbitAmp's and OrbitBass's ----
    {
        juce::StringArray wrong;
        for (const auto* p : { &products::orbitAmp(), &bass })
        {
            auto t = make (*p);
            set (*t, params::gateOn, 0.0f);
            set (*t, params::gateThreshold, -50.0f);
            for (int i = 0; i < 40; ++i)
                t->history.tick();
            std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
            GuardRail* rail = nullptr;
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
            {
                if (auto* r = dynamic_cast<GuardRail*> (&c); r != nullptr && r->paramId() == params::gateThreshold)
                    rail = r;
                for (auto* child : c.getChildren())
                    walk (*child);
            };
            walk (*ed);
            const juce::String who (p->name);
            if (rail == nullptr)
            {
                wrong.add (who + ": no GATE rail");
                continue;
            }
            // The scale over the whole travel: the lowest value's place is the travel's end, half a hand off the rail's foot.
            // The guard's rail wears the fader's look since 01.11 (no name, its number in the lamp): its hand travels to half
            // a fader's grip off its fader area's foot (fader::travel; the area stands 4 px in from the rail's ends, as GuardRail::
            // faderArea draws it), the plain rail's to half its grip over its 2 px lip.
            const float stop = rail->yOfValue (-80.0f),
                        foot = (float) rail->getHeight() - (rail->hasFaderLook() ? 4.0f + fader::gripH * 0.5f : 2.0f + meterrail::gripH * 0.5f);
            if (std::abs (stop - foot) > 0.5f)
                wrong.add (who + ": the lowest threshold at " + juce::String (stop, 1) + ", the travel's end at " + juce::String (foot, 1));
            const auto now = juce::Time::getCurrentTime();
            const auto ev = [&] (float y, float downY)
            {
                const juce::Point<float> at ((float) rail->getWidth() * 0.5f, y), down ((float) rail->getWidth() * 0.5f, downY);
                return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, juce::ModifierKeys(), 0.0f,
                                         juce::MouseInputSource::defaultOrientation, juce::MouseInputSource::defaultRotation,
                                         juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY,
                                         rail, rail, now, down, now, 1, at != down);
            };
            // Lifted off the stop: on at the lowest threshold; pushed back onto it: OFF; up again: on — one drag, one undo.
            const float lowest = -80.0f + 4.0f * 50.0f / (stop - rail->yOfValue (-30.0f));   // the rail's value 4 px off the stop
            rail->mouseDown (ev (stop, stop));
            rail->mouseDrag (ev (stop - 4.0f, stop));
            const float liftedTo = value (*t, params::gateThreshold);
            const float step = t->apvts.getParameter (params::gateThreshold)->getNormalisableRange().interval;   // its snap
            const bool liftedOn = value (*t, params::gateOn) > 0.5f && std::abs (liftedTo - lowest) <= std::max (0.2f, step * 0.5f + 1.0e-3f);
            rail->mouseDrag (ev (stop + 8.0f, stop));
            const bool parked = value (*t, params::gateOn) < 0.5f;
            rail->mouseDrag (ev (rail->yOfValue (-60.0f), stop));
            rail->mouseUp (ev (rail->yOfValue (-60.0f), stop));
            for (int i = 0; i < 40; ++i)
                t->history.tick();
            const bool movedOn = value (*t, params::gateOn) > 0.5f && std::abs (value (*t, params::gateThreshold) + 60.0f) <= std::max (0.3f, step * 0.5f + 1.0e-3f);
            t->history.undo();
            const bool undone = value (*t, params::gateOn) < 0.5f && std::abs (value (*t, params::gateThreshold) + 50.0f) < 1.0e-3f;
            if (! (liftedOn && parked && movedOn && undone))
                wrong.add (who + juce::String (": lifted off the stop ") + (liftedOn ? "on at the lowest" : "NOT on at the lowest") + " ("
                           + juce::String (liftedTo, 2) + " dB, the rail's " + juce::String (lowest, 2) + ", the rail " + juce::String (rail->getHeight()) + " px), pushed onto it "
                           + (parked ? "OFF" : "NOT off") + ", moved up " + (movedOn ? "on at -60" : "NOT on at -60") + ", one undo "
                           + (undone ? "back" : "NOT back"));
        }
        check (wrong.isEmpty(), "GATE's OFF is its rail's end stop, no zone of its own: the scale the whole travel, lifted off the stop on at the lowest "
                                "threshold, one drag one undo step (OrbitAmp and OrbitBass)" + (wrong.isEmpty() ? juce::String() : ": " + wrong.joinIntoString ("; ")));
    }

    // ---- STEP 13: ON FIRST, RULED OFF, IN EVERY MENU OF ORBITBASS'S BLOCKS (the owner, 01.11) — the eight blocks' and the guards';
    // the rest of each as it was; the guitar's guard menus as they were ----
    {
        const auto settle = [] (AmpProcessor& a) { for (int i = 0; i < 40; ++i) a.history.tick(); };
        // A menu as its words in order: "-" a rule, [a header], "NAME >" a submenu with its own words indented under it, " *" ticked.
        std::function<juce::StringArray (const juce::PopupMenu&)> read = [&read] (const juce::PopupMenu& menu)
        {
            juce::StringArray out;
            for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
            {
                const auto& i = it.getItem();
                out.add (i.isSeparator ? juce::String ("-") : i.isSectionHeader ? "[" + i.text + "]"
                                                             : i.text + (i.subMenu != nullptr ? " >" : "") + (i.isTicked ? " *" : ""));
                if (i.subMenu != nullptr)
                    for (const auto& line : read (*i.subMenu))
                        out.add ("  " + line);
            }
            return out;
        };
        const auto from = [] (const juce::StringArray& lines, int first)
        {
            juce::StringArray out;
            for (int k = first; k < lines.size(); ++k)
                out.add (lines[k]);
            return out;
        };
        const auto top = [] (const juce::StringArray& lines)
        {
            juce::StringArray out;
            for (const auto& l : lines)
                if (! l.startsWith (" "))
                    out.add (l.endsWith (" *") ? l.dropLastCharacters (2) : l);
            return out;
        };
        const auto snapshot = [] (AmpProcessor& a)
        {
            std::vector<float> v;
            for (auto* p : a.getParameters())
                v.push_back (p->getValue());
            return v;
        };
        const auto movedSince = [] (AmpProcessor& a, const std::vector<float>& before)
        {
            juce::StringArray moved;
            const auto& all = a.getParameters();
            for (int i = 0; i < all.size(); ++i)
                if (std::abs (all[i]->getValue() - before[(size_t) i]) > 1.0e-6f)
                    if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (all[i]))
                        moved.add (withId->getParameterID());
            return moved;
        };
        const auto onItemOf = [] (const juce::PopupMenu& menu)   // item 1's id when it is ON, a plain item, else 0
        {
            juce::PopupMenu::MenuItemIterator it (menu);
            if (! it.next())
                return 0;
            const auto& i = it.getItem();
            return i.text == "ON" && i.subMenu == nullptr && i.isEnabled && ! i.isSeparator && ! i.isSectionHeader ? i.itemID : 0;
        };

        // THE EIGHT BLOCKS: each its strip arrow's switch, the rest of its menu what step 12's held (its words, and the builder's own).
        struct Block { const char* name; const char* on; std::function<bass::Menu (juce::AudioProcessorValueTreeState&)> alone; juce::StringArray words; };
        const std::vector<Block> blocks {
            // 01.11: OCTAVE's and CHORUS's menus hold nothing past ON (their TONE and RATE rails on the blocks), ENVELOPE's
            // RANGE is its picture's list, its RELEASE a rail; 02.11: COMP's CHARACTER is the combo on its border. RESET at
            // every menu's foot (checked as `withReset` below).
            { "COMP",     params::compOn,   [] (auto& s) { return bass::compMenu (s); },   { "ATTACK >", "RELEASE >" } },
            { "OCTAVE",   params::octaveOn, [] (auto& s) { return bass::octaveMenu (s); }, {} },
            { "ENVELOPE", params::filterOn, [] (auto& s) { return bass::filterMenu (s); },
              { "MODE >", "DIRECTION >", "RESONANCE >", "CLEAN LOWS >" } },
            { "AMP",      params::preampOn, [] (auto& s) { return bass::ampMenu (s); },    { "BLEND CROSSOVER >", "INVERT POLARITY", "HPF >", "LPF >" } },
            { "CHORUS",   params::chorusOn, [] (auto& s) { return bass::chorusMenu (s); }, {} },
            { "DELAY",    params::delayOn,  [] (auto& s) { return bass::delayMenu (s); },
              { "SYNC", "DIVISION >", "TIME (FREE) >", "HOST TEMPO", "BPM >", "REPEATS >", "DARK >", "OFFSET >" } },
            { "REVERB",   params::reverbOn, [] (auto& s) { return bass::reverbMenu (s); }, { "TYPE >", "DECAY >", "PRE-DELAY >" } },
            { "CAB IR",   params::cabOn,    [] (auto& s) { return bass::cabMenu (s); },    { "DI / CAB >", "TRIM", "TRIM KEEPS >" } } };
        juce::StringArray wrong;
        int menus = 0, picks = 0;
        // RESET at the foot of every OrbitBass menu (the owner, 01.11), ruled off from what stands over it.
        const auto withReset = [] (juce::StringArray a)
        {
            if (! a.isEmpty())
                a.add ("-");
            a.add ("RESET");
            return a;
        };
        {
            auto t = make (bass);
            for (const auto& b : blocks)
            {
                bool tableSays = false;
                for (const auto& link : params::chainLinks)
                    tableSays = tableSays || (juce::String (link.name) == b.name && link.onParam != nullptr && juce::String (link.onParam) == b.on);
                if (! tableSays)
                    wrong.add (juce::String (b.name) + ": the chain table's switch is not " + b.on);
                for (const float was : { 1.0f, 0.0f })
                {
                    set (*t, b.on, was);
                    settle (*t);
                    auto m = bass::blockMenu (t->apvts, b.name);
                    const auto lines = read (m.items);
                    const int  id    = onItemOf (m.items);
                    const auto rest  = from (lines, 2);
                    ++menus;
                    if (id == 0 || lines[0] != (was > 0.5f ? "ON *" : "ON") || lines[1] != "-")
                        wrong.add (juce::String (b.name) + (was > 0.5f ? " on" : " off") + ": opens with \"" + lines[0] + "\", \"" + lines[1] + "\"");
                    if (top (rest) != withReset (b.words) || rest != withReset (read (b.alone (t->apvts).items)))
                        wrong.add (juce::String (b.name) + ": the rest reads " + top (rest).joinIntoString (", "));
                    if (id <= 0 || id > (int) m.acts.size() || ! m.acts[(size_t) id - 1])
                        continue;
                    const auto before = snapshot (*t);
                    m.acts[(size_t) id - 1]();
                    ++picks;
                    settle (*t);
                    const auto moved = movedSince (*t, before);
                    if (moved != juce::StringArray (b.on) || std::abs (value (*t, b.on) - (1.0f - was)) > 1.0e-6f)
                        wrong.add (juce::String (b.name) + ": ON picked moved " + moved.joinIntoString (" "));
                    t->history.undo();
                    if (! movedSince (*t, before).isEmpty())
                        wrong.add (juce::String (b.name) + ": one undo left " + movedSince (*t, before).joinIntoString (" ") + " moved");
                }
            }
        }

        // THE GUARDS, through the editor as their blocks open them: the menus' words, both amps; OrbitBass's ON first.
        for (const auto* p : { &products::orbitAmp(), &bass })
        {
            const bool isBass = p == &bass;
            const juce::String who (p->name);
            auto t = make (*p);
            set (*t, params::gateDecay, 0.0f);
            set (*t, params::gatePos, 0.0f);
            set (*t, params::gateThreshold, -50.0f);
            set (*t, params::limiterCeiling, -1.0f);
            settle (*t);
            std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
            auto* editor = dynamic_cast<AmpEditor*> (ed.get());
            if (editor == nullptr)
            {
                wrong.add (who + ": no AmpEditor");
                continue;
            }
            for (const float was : { 1.0f, 0.0f })
            {
                set (*t, params::gateOn, was);
                set (*t, params::limiterOn, was);
                settle (*t);
                const bool on = was > 0.5f;
                juce::StringArray gate { "LEARN", "-", "SOFT   -60", on ? "MEDIUM -50 *" : "MEDIUM -50", "HARD   -40", "DECAY >", "  NORMAL *", "  METAL",
                                         "MUTES AT >", "  " + params::gatePositions[0].toUpperCase() + " *", "  " + params::gatePositions[1].toUpperCase() };
                juce::StringArray limit { "SAFETY  -0.3", on ? "NORMAL  -1.0 *" : "NORMAL  -1.0", "TIGHT   -3.0" };
                // Step 12's head was the header, OFF under it while on; now, both amps (the owner, 01.11: every menu): ON first and
                // ruled off, the header, no OFF.
                const juce::StringArray gateHead  { on ? "ON *" : "ON", "-", on ? "[GATE  -50 DB]" : "[GATE  OFF]" };
                const juce::StringArray limitHead { on ? "ON *" : "ON", "-", on ? "[LIMITER  -1.0 DB]" : "[LIMITER  OFF]" };
                gate  = [&] { auto a = gateHead;  a.addArray (gate);  return a; }();
                limit = [&] { auto a = limitHead; a.addArray (limit); return a; }();
                if (isBass)   // RESET at the foot of OrbitBass's guards' menus too (01.11); OrbitAmp's as they were
                {
                    gate  = withReset (gate);
                    limit = withReset (limit);
                }
                ++menus;
                ++menus;
                if (const auto g = read (editor->gateMenu()); g != gate)
                    wrong.add (who + " GATE " + (on ? "on" : "off") + " reads " + g.joinIntoString (" | "));
                if (const auto l = read (editor->limiterMenu()); l != limit)
                    wrong.add (who + " LIMIT " + (on ? "on" : "off") + " reads " + l.joinIntoString (" | "));

                // LIMIT's ON: its strip arrow's switch, turned over, the ceiling kept — one undo step.
                if (const int id = onItemOf (editor->limiterMenu()); id > 0)
                {
                    const auto before = snapshot (*t);
                    editor->pickLimiter (id);
                    ++picks;
                    settle (*t);
                    const auto moved = movedSince (*t, before);
                    if (moved != juce::StringArray (params::limiterOn) || (value (*t, params::limiterOn) > 0.5f) == on)
                        wrong.add (who + " LIMIT's ON picked moved " + moved.joinIntoString (" "));
                    t->history.undo();
                    if (! movedSince (*t, before).isEmpty())
                        wrong.add (who + " LIMIT's ON: one undo did not bring it back");
                }
            }
            // A ceiling picked switches the limiter on, as it did.
            set (*t, params::limiterOn, 0.0f);
            editor->pickLimiter (4);
            if (value (*t, params::limiterOn) < 0.5f || std::abs (value (*t, params::limiterCeiling) + 3.0f) > 1.0e-3f)
                wrong.add (who + ": the limiter's TIGHT not as it was");
            juce::ignoreUnused (isBass);
        }

        // GATE'S ON AND ITS RAIL'S END STOP AGREE, both ways (OrbitBass): the menu's OFF what parking the hand on the stop does, its ON what
        // lifting it off does; the stop parked or lifted, the menu's ON ticked to match.
        {
            struct Face { std::unique_ptr<AmpProcessor> t; std::unique_ptr<juce::AudioProcessorEditor> ed; AmpEditor* editor = nullptr; GuardRail* rail = nullptr; };
            const auto open = [&] (bool on)
            {
                Face f;
                f.t = make (bass);
                set (*f.t, params::gateThreshold, -50.0f);
                set (*f.t, params::gateOn, on ? 1.0f : 0.0f);
                settle (*f.t);
                f.ed.reset (f.t->createEditor());
                f.editor = dynamic_cast<AmpEditor*> (f.ed.get());
                std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
                {
                    if (auto* face = dynamic_cast<FaceplateView*> (&c))
                        f.rail = &face->gateBlock().valueRail();
                    for (auto* child : c.getChildren())
                        walk (*child);
                };
                walk (*f.ed);
                return f;
            };
            const auto drag = [] (GuardRail& rail, float fromY, float toY)
            {
                const auto now = juce::Time::getCurrentTime();
                const auto ev = [&] (float y)
                {
                    const juce::Point<float> at ((float) rail.getWidth() * 0.5f, y), down ((float) rail.getWidth() * 0.5f, fromY);
                    return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, juce::ModifierKeys(), 0.0f,
                                             juce::MouseInputSource::defaultOrientation, juce::MouseInputSource::defaultRotation,
                                             juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY,
                                             &rail, &rail, now, down, now, 1, at != down);
                };
                rail.mouseDown (ev (fromY));
                rail.mouseDrag (ev (toY));
                rail.mouseUp (ev (toY));
            };
            const auto ticked = [&] (Face& f)
            {
                const auto lines = read (f.editor->gateMenu());
                return lines.isEmpty() ? juce::String ("nothing") : lines[0];
            };
            auto byMenuOff = open (true), byStopOff = open (true), byMenuOn = open (false), byStopOn = open (false);
            if (byMenuOff.editor == nullptr || byMenuOff.rail == nullptr || byStopOn.rail == nullptr || byStopOff.rail == nullptr || byMenuOn.editor == nullptr)
                wrong.add ("no GATE rail on OrbitBass's face");
            else
            {
                const float stop = byStopOff.rail->yOfValue (-80.0f);
                if (std::abs (byMenuOn.rail->lowestValue() + 80.0f) > 1.0e-4f)
                    wrong.add ("the rail's lowest " + juce::String (byMenuOn.rail->lowestValue(), 2));
                // OFF: the menu's pick, and the hand pushed onto the stop.
                const auto beforeOff = snapshot (*byMenuOff.t);
                byMenuOff.editor->pickGate (onItemOf (byMenuOff.editor->gateMenu()));
                settle (*byMenuOff.t);
                drag (*byStopOff.rail, byStopOff.rail->yOfValue (-50.0f), stop + 8.0f);
                settle (*byStopOff.t);
                const auto offMoved = movedSince (*byMenuOff.t, beforeOff);
                const bool offAgree = value (*byMenuOff.t, params::gateOn) < 0.5f && value (*byStopOff.t, params::gateOn) < 0.5f
                                   && std::abs (value (*byMenuOff.t, params::gateThreshold) + 50.0f) < 1.0e-3f
                                   && std::abs (value (*byStopOff.t, params::gateThreshold) + 50.0f) < 1.0e-3f
                                   && offMoved == juce::StringArray (params::gateOn) && byMenuOff.rail->gripText() == byStopOff.rail->gripText();
                byMenuOff.t->history.undo();
                const bool offUndone = movedSince (*byMenuOff.t, beforeOff).isEmpty();
                // ON: the menu's pick, and the hand lifted 4 px off the stop.
                const auto beforeOn = snapshot (*byMenuOn.t);
                byMenuOn.editor->pickGate (onItemOf (byMenuOn.editor->gateMenu()));
                settle (*byMenuOn.t);
                drag (*byStopOn.rail, stop, stop - 4.0f);
                settle (*byStopOn.t);
                const float px = 50.0f / (stop - byStopOn.rail->yOfValue (-30.0f));   // dB a pixel of the rail
                const float menuTh = value (*byMenuOn.t, params::gateThreshold), stopTh = value (*byStopOn.t, params::gateThreshold);
                const bool onAgree = value (*byMenuOn.t, params::gateOn) > 0.5f && value (*byStopOn.t, params::gateOn) > 0.5f
                                  && std::abs (menuTh + 80.0f) < 1.0e-3f && stopTh >= menuTh - 1.0e-3f && stopTh - menuTh <= 4.0f * px + 0.6f;
                byMenuOn.t->history.undo();
                const bool onUndone = movedSince (*byMenuOn.t, beforeOn).isEmpty();
                // The stop's way: the menu reads what the rail did.
                const auto afterPark = ticked (byStopOff), afterLift = ticked (byStopOn);
                if (! (offAgree && offUndone && onAgree && onUndone && afterPark == "ON" && afterLift == "ON *"))
                    wrong.add (juce::String ("GATE: the menu's OFF ") + (offAgree ? "parks as the stop" : "NOT as the stop") + " (moved "
                               + offMoved.joinIntoString (" ") + ")" + (offUndone ? "" : ", not one undo") + "; its ON " + (onAgree ? "lifts as the stop" : "NOT as the stop")
                               + " (" + juce::String (menuTh, 2) + " dB, the rail's lift " + juce::String (stopTh, 2) + ")" + (onUndone ? "" : ", not one undo")
                               + "; parked the menu reads \"" + afterPark + "\", lifted \"" + afterLift + "\"");
                picks += 2;
            }
        }
        // THE CUTS' MENUS UNDER THEIR SWITCHES (the owner, 01.11: every menu): ON first — the cut's own switch — ruled off, then
        // the frequencies as they were; ON picked turns the switch over, one undo step.
        {
            auto t = make (bass);
            struct Cut { const char* on; std::function<bass::Menu (juce::AudioProcessorValueTreeState&)> menu;
                         std::function<void (bass::Menu&, juce::AudioProcessorValueTreeState&)> alone; };
            const Cut cuts[] { { params::bassHpfOn, [] (auto& s) { return bass::hpfMenu (s); }, [] (bass::Menu& m, auto& s) { bass::addHpf (m, m.items, s); } },
                               { params::bassLpfOn, [] (auto& s) { return bass::lpfMenu (s); }, [] (bass::Menu& m, auto& s) { bass::addLpf (m, m.items, s); } } };
            for (const auto& c : cuts)
                for (const float was : { 1.0f, 0.0f })
                {
                    set (*t, c.on, was);
                    settle (*t);
                    auto m = c.menu (t->apvts);
                    bass::Menu old;
                    c.alone (old, t->apvts);
                    const auto lines = read (m.items), before = read (old.items);
                    const int  id    = onItemOf (m.items);
                    ++menus;
                    if (id == 0 || lines[0] != (was > 0.5f ? "ON *" : "ON") || lines[1] != "-" || from (lines, 2) != withReset (before) || before.size() < 12)
                        wrong.add (juce::String (c.on) + "'s menu reads " + lines.joinIntoString (" | "));
                    if (id <= 0 || id > (int) m.acts.size() || ! m.acts[(size_t) id - 1])
                        continue;
                    const auto snap = snapshot (*t);
                    m.acts[(size_t) id - 1]();
                    ++picks;
                    settle (*t);
                    if (movedSince (*t, snap) != juce::StringArray (c.on) || (value (*t, c.on) > 0.5f) == (was > 0.5f))
                        wrong.add (juce::String (c.on) + "'s ON moved " + movedSince (*t, snap).joinIntoString (" "));
                    t->history.undo();
                    if (! movedSince (*t, snap).isEmpty())
                        wrong.add (juce::String (c.on) + "'s ON: one undo did not bring it back");
                }
        }
        check (wrong.isEmpty() && menus == 28 && picks >= 24,
               "ON first, ruled off, in OrbitBass's every block menu — COMP, OCTAVE, ENVELOPE, AMP, CHORUS, DELAY, REVERB, CAB IR, GATE and LIMIT — "
               "ticked by the chain table's switch, a pick turning it over in one undo step, the rest as it was; the HPF's and LPF's under their "
               "switches the same; GATE's ON and OFF its rail's stop "
               "both ways; OrbitAmp's guard menus the same (" + juce::String (menus) + " menus, " + juce::String (picks) + " picks"
               + (wrong.isEmpty() ? juce::String() : ": " + wrong.joinIntoString ("; ")) + ")");

        // THE GUITAR'S BLOCKS TOO (the owner, 01.11: every menu): every menu a block on OrbitAmp's face opens — a right click on
        // it or on anything in it, a selector's click — ON first and ruled off, ticked by the block's chain switch; ON picked
        // turns that switch over, as the block's strip arrow does, in one undo step.
        {
            auto t = make (products::orbitAmp());
            for (const char* id : { params::preampOn, params::delayOn, params::reverbOn, params::cabOn })
                set (*t, id, 1.0f);
            settle (*t);
            std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
            const auto linkOf = [] (const juce::RangedAudioParameter* on) -> juce::String
            {
                for (const auto& link : params::chainLinks)
                    if (on != nullptr && link.onParam != nullptr && on->paramID == link.onParam)
                        return link.name;
                return {};
            };
            struct Caught { juce::String block; const char* on; juce::StringArray lines; std::function<void (int)> answer; };
            std::vector<Caught> caught;
            BlockFrame* clicking = nullptr;
            onfirst::catcher() = [&] (const juce::PopupMenu& m, const std::function<void (int)>& answer)
            {
                auto* sw = clicking != nullptr ? clicking->blockSwitch() : nullptr;
                caught.push_back ({ linkOf (sw), sw != nullptr ? sw->paramID.toRawUTF8() : "", read (m), answer });
                return true;
            };
            std::vector<std::pair<BlockFrame*, juce::Component*>> targets;
            std::function<void (juce::Component&, BlockFrame*)> walk = [&] (juce::Component& c, BlockFrame* in)
            {
                if (dynamic_cast<GuardBlock*> (&c) != nullptr || ! c.isVisible())
                    return;   // the guards' menus are the editor's, read above
                if (auto* frame = dynamic_cast<BlockFrame*> (&c))
                    in = frame;
                if (in != nullptr)
                    targets.push_back ({ in, &c });
                for (auto* child : c.getChildren())
                    walk (*child, in);
            };
            for (auto* child : ed->getChildren())   // the editor itself is on no screen here
                walk (*child, nullptr);
            const auto now = juce::Time::getCurrentTime();
            for (auto [frame, c] : targets)
            {
                clicking = frame;
                const auto at = c->getLocalBounds().getCentre().toFloat();
                for (const bool right : { true, false })
                {
                    if (! right && dynamic_cast<VoicingSelector*> (c) == nullptr && dynamic_cast<Selector*> (c) == nullptr)
                        continue;   // a left click only where it opens a menu
                    const juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), at,
                                              right ? juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier) : juce::ModifierKeys(), 0.0f,
                                              juce::MouseInputSource::defaultOrientation, juce::MouseInputSource::defaultRotation,
                                              juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY, c, c, now, at, now, 1, false);
                    c->mouseDown (e);
                    c->mouseUp (e);
                }
            }
            clicking = nullptr;
            onfirst::catcher() = nullptr;
            juce::PopupMenu::dismissAllActiveMenus();
            settle (*t);   // what the clicks themselves switched (SMOOTH's tag takes any click) its own undo step, before the picks
            juce::StringArray reached, seen, bad;
            for (const auto& m : caught)
            {
                seen.addIfNotAlreadyThere (m.block + " " + (m.lines.size() > 2 ? m.lines[2].trim() : juce::String ("?")));
                if (m.lines.size() < 2 || m.lines[0] != "ON *" || m.lines[1] != "-" || m.block.isEmpty())
                    bad.addIfNotAlreadyThere (m.block + " opens with \"" + m.lines[0] + "\"");
                else
                    reached.addIfNotAlreadyThere (m.block);
            }
            // ON picked, once a block: its switch off, the block gone as by its arrow; one undo, back.
            for (const auto& name : reached)
                for (const auto& m : caught)
                    if (m.block == name)
                    {
                        const auto before = snapshot (*t);
                        m.answer (onfirst::item);
                        settle (*t);
                        const auto moved = movedSince (*t, before);
                        if (moved != juce::StringArray (m.on) || value (*t, m.on) > 0.5f)
                            bad.add (name + "'s ON moved " + moved.joinIntoString (" "));
                        t->history.undo();
                        if (const auto left = movedSince (*t, before); ! left.isEmpty())
                            bad.add (name + "'s ON: one undo left " + left.joinIntoString (" ") + " moved");
                        break;
                    }
            reached.sort (false);
            check (bad.isEmpty() && reached.joinIntoString (",") == "AMP,CAB IR,DELAY,REVERB",
                   "OrbitAmp's block menus open with ON, ruled off — " + juce::String ((int) caught.size()) + " menus from "
                   + reached.joinIntoString (", ") + " (" + seen.joinIntoString ("; ") + ")" + (bad.isEmpty() ? juce::String() : ": " + bad.joinIntoString ("; ")));
        }

        // HINTS OVER THE SLIDERS (the owner, 01.11: what is this, BLEND?): every rail and slider on OrbitBass's face — COMP's
        // threshold, ratio and BLEND, OCTAVE, ENVELOPE, CHORUS, DELAY's and REVERB's MIX, the AMP's BLEND, DI/CAB, GATE, LIMIT —
        // a tooltip, its name in capitals first, then what it mixes with what, or nothing more ("THRESHOLD"); SHORT (the owner,
        // 01.11: shorter — "BLEND: compressed with dry"), 30 characters at most; the window's TooltipWindow shows it at 13 px.
        {
            auto t = make (bass);
            set (*t, params::delayPresent, 1.0f);
            set (*t, params::reverbPresent, 1.0f);
            settle (*t);
            std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
            juce::StringArray bad, seen;
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
            {
                if (! c.isVisible())
                    return;
                juce::SettableTooltipClient* tip = nullptr;
                if (auto* r = dynamic_cast<GuardRail*> (&c))
                    tip = r;
                else if (auto* s = dynamic_cast<CompRatioCombo*> (&c))
                    tip = s;
                if (tip != nullptr)
                {
                    const auto text = tip->getTooltip();
                    const auto name = text.upToFirstOccurrenceOf (": ", false, false);   // a bare name is its whole hint
                    if (name.isEmpty() || name != name.toUpperCase() || ! juce::CharacterFunctions::isLetter (name[0]) || text.length() > 30)
                        bad.add (juce::String (typeid (c).name()) + " \"" + text + "\"");
                    else
                        seen.add (name);
                }
                for (auto* child : c.getChildren())
                    walk (*child);
            };
            for (auto* child : ed->getChildren())
                walk (*child);
            seen.sort (false);
            check (bad.isEmpty() && seen.joinIntoString (",") == "BLEND,BLEND,CHORUS,DI/CAB,ENVELOPE,GATE,LIMIT,OCTAVE,RATE,RATIO,RELEASE,THRESHOLD,TONE" && juce::TooltipWindow().getLookAndFeel().getTooltipBounds ("BLEND: a hint", {}, { 0, 0, 2000, 2000 }).getHeight() >= 13,
                   "every rail and slider on OrbitBass's face has a short hint, its name first: " + seen.joinIntoString (", ")
                   + (bad.isEmpty() ? juce::String() : "; without: " + bad.joinIntoString ("; ")));
        }

        // STEP 14: COMP'S PICTURE, ITS SQUEEZE'S COLUMN AND ITS IN/OUT BOX STAND ON ONE SCALE (the owner, 01.11). The GR
        // column, between the picture and the ratio, runs from the picture's plot top to its levels' foot over 24 dB, so
        // -6, -12 and -24 dB fill a quarter, a half and all of it (and deeper stays full); the box under the combo spans the
        // levels exactly — its curve starts at their foot, its knee at the threshold line's height, every segment end at
        // the levels' own height for its dB; nothing a player reads under 13 px, and the widest ratio, "10.0:1", fits the
        // combo's number area.
        {
            auto t = make (bass);
            std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
            CompBlock* block = nullptr;
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
            {
                if (auto* b = dynamic_cast<CompBlock*> (&c); b != nullptr && block == nullptr)
                    block = b;
                for (auto* child : c.getChildren())
                    walk (*child);
            };
            walk (*ed);
            juce::StringArray wrong, depths;
            if (block == nullptr)
                wrong.add ("no COMP block");
            else
            {
                auto& graph  = block->picture();
                auto& bar    = block->grBar();
                auto& combo  = block->ratio();
                auto& square = block->square();
                const float plotTop    = (float) graph.getY() + graph.plot().getY();
                const float levelsTop  = (float) graph.getY() + graph.levels().getY();
                const float levelsFoot = (float) graph.getY() + graph.levels().getBottom();

                if (std::abs ((float) bar.getY() - plotTop) > 0.5f || std::abs ((float) bar.getBottom() - levelsFoot) > 0.5f
                    || graph.getRight() > bar.getX() || bar.getRight() > combo.getX() || bar.getRight() > square.getX())
                    wrong.add ("the column " + bar.getBounds().toString() + " for the plot's top " + juce::String (plotTop, 1)
                               + " and the levels' foot " + juce::String (levelsFoot, 1) + ", between the picture "
                               + graph.getBounds().toString() + " and the ratio " + combo.getBounds().toString());

                // The fill, read off the column painted: the rows lit at its centre, from its top.
                const auto litRows = [&]
                {
                    const auto img = bar.createComponentSnapshot (bar.getLocalBounds(), false, 1.0f);
                    const int x = img.getWidth() / 2;
                    int lit = 0;
                    for (int y = 0; y < img.getHeight(); ++y)
                        if (const auto c = img.getPixelAt (x, y); std::max ({ c.getRed(), c.getGreen(), c.getBlue() }) > 0x50)
                            lit = y + 1;
                    return lit;
                };
                for (const float db : { 0.0f, -6.0f, -12.0f, -24.0f, -30.0f })
                {
                    bar.showDb (db);
                    const int lit = litRows();
                    const float want = (float) bar.getHeight() * std::min (24.0f, -db) / 24.0f;
                    depths.add (juce::String (db, 0) + " dB " + juce::String (lit) + "/" + juce::String (bar.getHeight()) + " px");
                    if (std::abs ((float) lit - want) > 1.0f)
                        wrong.add ("the column at " + juce::String (db, 0) + " dB fills " + juce::String (lit) + " px, not "
                                   + juce::String (want, 1) + " of " + juce::String (bar.getHeight()));
                }
                bar.showDb (0.0f);

                const auto box = square.box().translated ((float) square.getX(), (float) square.getY());
                if (std::abs (box.getY() - levelsTop) > 0.5f || std::abs (box.getBottom() - levelsFoot) > 0.5f)
                    wrong.add ("the box spans " + juce::String (box.getY(), 1) + ".." + juce::String (box.getBottom(), 1) + ", the levels "
                               + juce::String (levelsTop, 1) + ".." + juce::String (levelsFoot, 1));
                for (const float thr : { -12.0f, -24.0f, -40.0f })
                {
                    set (*t, params::compThreshold, thr);
                    // the box's output axis, -48 dB at its foot to 0 at its top: the curve's start and its knee
                    const auto yInBox = [&] (double db) { return box.getBottom() - (float) ((db + 48.0) / 48.0) * box.getHeight(); };
                    const float knee = yInBox (thr), line = (float) graph.getY() + graph.yOfDb (thr);
                    if (std::abs (yInBox (-48.0) - levelsFoot) > 0.5f || std::abs (knee - line) > 0.5f)
                        wrong.add ("at " + juce::String (thr, 0) + " dB the knee at " + juce::String (knee, 1) + ", the line at " + juce::String (line, 1));
                    // ...each ratio's segment end where the CHARACTER's own curve (BassComp::staticDeltaDb, the owner 01.11 —
                    // OPTO's fed back, its knee the wider) puts 0 dB in.
                    const auto character = static_cast<core::BassComp::Character> (juce::roundToInt (value (*t, params::compCharacter)));
                    for (const double r : compRatioItems)
                        if (const float end = (float) square.getY() + square.yOfRatio (r),
                                        level = (float) graph.getY() + graph.yOfDb (core::BassComp::staticDeltaDb (character, 0.0, (double) thr, r));
                            std::abs (end - level) > 0.5f)
                            wrong.add ("at " + juce::String (thr, 0) + " dB " + CompRatioCombo::itemText (r) + "'s segment end at "
                                       + juce::String (end, 1) + ", its level on the picture at " + juce::String (level, 1));
                }

                const auto font = theme::displayFont (CompRatioCombo::textHeight);
                float widest = 0.0f;
                for (const double r : compRatioItems)
                    widest = std::max (widest, theme::trackedWidth (juce::String (r, 1) + ":1", font, 0.02f));
                const float room = (float) combo.numberArea().getWidth() - 4.0f;   // 2 px clear each side
                if (theme::trackedWidth ("10.0:1", font, 0.02f) > room || widest > room
                    || combo.cell().getHeight() < CompRatioCombo::textHeight + 4.0f)
                    wrong.add ("\"10.0:1\" " + juce::String (theme::trackedWidth ("10.0:1", font, 0.02f), 1) + " px in "
                               + juce::String (room, 1) + " px, the cell " + juce::String (combo.cell().getHeight(), 1) + " px high");
                if (CompRatioCombo::textHeight < 13.0f || CompGraph::textHeight < 13.0f || block->thresholdUnit().getFont().getHeight() < 13.0f
                    || block->threshold().gripTextHeight < 13.0f || (block->fader().hasFaderLook() ? fader::textH : block->fader().gripTextHeight) < 13.0f
                    || ed->getLookAndFeel().getPopupMenuFont().getHeight() < 13.0f)
                    wrong.add ("type under 13 px");
            }
            check (wrong.isEmpty(), "COMP's GR column from the plot's top to the levels' foot, -6/-12/-24 dB a quarter/half/all of it; the "
                                    "IN/OUT box on the levels' span, its curve from their foot, its knee at the line; 13 px and up, \"10.0:1\" "
                                    "fits: read " + depths.joinIntoString (", ") + (wrong.isEmpty() ? juce::String() : "; WRONG: " + wrong.joinIntoString ("; ")));
        }

        // STEP 14: A DOUBLE CLICK IS THE DEFAULT (the owner, 01.11). On every GuardRail — COMP's THRESHOLD (on its grip, where
        // a single click types) and its BLEND, the GATE (where a click is its menu) — and on COMP's picture, a double click
        // sets the parameter's default back in ONE undo step, the switch on, and opens no menu and no field. A SINGLE click
        // still opens them, but only once the double click's time has passed — nothing at once. One rail on the guitar face
        // too, OrbitAmp's GATE: the rail is shared code.
        {
            const int wait = juce::jmin (300, juce::MouseEvent::getDoubleClickTimeout());
            // The message loop the gates do not run: the timers due, called until `done` or `ms` have passed.
            const auto pump = [] (const std::function<bool()>& done, int ms)
            {
                const auto until = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
                while (juce::Time::getMillisecondCounter() < until)
                {
                    juce::Thread::sleep (10);
                    juce::Timer::callPendingTimersSynchronously();
                    if (done != nullptr && done())
                        return true;
                }
                return done != nullptr && done();
            };
            const auto event = [] (juce::Component& c, juce::Point<float> at, int clicks)
            {
                const auto now = juce::Time::getCurrentTime();
                return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, juce::ModifierKeys(), 0.0f,
                                         juce::MouseInputSource::defaultOrientation, juce::MouseInputSource::defaultRotation,
                                         juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY,
                                         &c, &c, now, at, now, clicks, false);
            };
            const auto doubleClick = [&] (juce::Component& c, juce::Point<float> at)
            {
                c.mouseDown (event (c, at, 1));
                c.mouseUp (event (c, at, 1));
                c.mouseDown (event (c, at, 2));
                c.mouseDoubleClick (event (c, at, 2));
                c.mouseUp (event (c, at, 2));
            };
            const auto settle = [] (AmpProcessor& a) { for (int i = 0; i < 40; ++i) a.history.tick(); };
            const auto railOf = [] (juce::Component& root, const char* id)
            {
                GuardRail* found = nullptr;
                std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
                {
                    if (auto* r = dynamic_cast<GuardRail*> (&c); r != nullptr && r->paramId() == id && found == nullptr)
                        found = r;
                    for (auto* child : c.getChildren())
                        walk (*child);
                };
                walk (root);
                return found;
            };
            juce::StringArray wrong, said;

            struct Case { const Product* product; const char* id; const char* on; float moved; bool onGrip; };
            for (const auto& k : { Case { &bass, params::compThreshold, params::compOn, -36.0f, true },
                                   Case { &bass, params::compAmount, params::compOn, 0.8f, false },
                                   Case { &bass, params::gateThreshold, params::gateOn, -64.0f, false },
                                   Case { &products::orbitAmp(), params::gateThreshold, params::gateOn, -64.0f, false } })
            {
                auto t = make (*k.product);
                std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
                auto* rail = railOf (*ed, k.id);
                const juce::String name = juce::String (k.product == &bass ? "OrbitBass's " : "OrbitAmp's ") + k.id;
                if (rail == nullptr)
                {
                    wrong.add (name + ": no rail");
                    continue;
                }
                int menus = 0;
                rail->onMenu = [&menus] (juce::Point<int>) { ++menus; };
                auto* valueP = t->apvts.getParameter (k.id);
                const float fallback = valueP->convertFrom0to1 (valueP->getDefaultValue());
                set (*t, k.id, k.moved);
                set (*t, k.on, 0.0f);
                settle (*t);
                const float at = rail->yOfValue (k.moved), h = (float) rail->getHeight();
                // Off the grip: an end of the rail — but a FADER's foot is its number, a field (the owner, 01.11): on a fader
                // its middle or its name's row, clear of the grip.
                const float off = rail->hasFaderLook() ? (at > h * 0.5f ? 6.0f : h * 0.5f) : (at > h * 0.5f ? 6.0f : h - 6.0f);
                const auto where = juce::Point<float> ((float) rail->getWidth() * 0.5f, k.onGrip ? at : off);

                // A single click: nothing at once; once the double click's time is up, the field on the grip, else the menu.
                rail->mouseDown (event (*rail, where, 1));
                rail->mouseUp (event (*rail, where, 1));
                const bool nothingAtOnce = menus == 0 && ! rail->isTyping();
                const bool landed = pump ([&] { return menus > 0 || rail->isTyping(); }, 1500);
                const bool rightOne = k.onGrip ? rail->isTyping() && menus == 0 : menus == 1 && ! rail->isTyping();
                const bool unmoved = std::abs (value (*t, k.id) - k.moved) < 1.0e-4f && value (*t, k.on) < 0.5f;
                if (! (nothingAtOnce && landed && rightOne && unmoved))
                    wrong.add (name + ": a single click " + (nothingAtOnce ? "waited" : "opened at once") + ", then "
                               + (rail->isTyping() ? "a field" : juce::String (menus) + " menu(s)") + (unmoved ? "" : ", and moved it"));
                ed.reset();   // the field closed with its face
                ed.reset (t->createEditor());
                rail = railOf (*ed, k.id);
                menus = 0;
                rail->onMenu = [&menus] (juce::Point<int>) { ++menus; };

                // A double click: the default, the switch on, one undo step; no menu, no field — not even after the wait.
                doubleClick (*rail, where);
                (void) pump (nullptr, wait + 300);
                settle (*t);
                const float reset = value (*t, k.id);
                const bool on = value (*t, k.on) > 0.5f;
                const bool quiet = menus == 0 && ! rail->isTyping();
                t->history.undo();
                const bool undone = std::abs (value (*t, k.id) - k.moved) < 1.0e-4f && value (*t, k.on) < 0.5f;
                said.add (name + " " + juce::String (k.moved, 2) + " -> " + juce::String (reset, 2));
                if (std::abs (reset - fallback) > 1.0e-4f || ! on || ! quiet || ! undone)
                    wrong.add (name + ": a double click set " + juce::String (reset, 3) + " (its default " + juce::String (fallback, 3) + "), "
                               + (on ? "on" : "off") + ", " + juce::String (menus) + " menu(s)" + (rail->isTyping() ? ", a field" : "")
                               + ", one undo " + (undone ? "gave it back" : "did not give it back"));
            }

            // COMP's picture: a double click sets the threshold's default, COMP on, one undo step; no menu.
            {
                auto t = make (bass);
                std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
                CompGraph* graph = nullptr;
                std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
                {
                    if (auto* g = dynamic_cast<CompGraph*> (&c); g != nullptr && graph == nullptr)
                        graph = g;
                    for (auto* child : c.getChildren())
                        walk (*child);
                };
                walk (*ed);
                if (graph == nullptr)
                    wrong.add ("no COMP picture");
                else
                {
                    int menus = 0;
                    graph->onMenu = [&menus] (juce::Point<int>) { ++menus; };
                    set (*t, params::compThreshold, -40.0f);
                    set (*t, params::compOn, 0.0f);
                    settle (*t);
                    doubleClick (*graph, graph->getLocalBounds().getCentre().toFloat());
                    (void) pump (nullptr, wait + 300);
                    settle (*t);
                    const float reset = value (*t, params::compThreshold);
                    const bool on = value (*t, params::compOn) > 0.5f;
                    t->history.undo();
                    const bool undone = value (*t, params::compThreshold) == -40.0f && value (*t, params::compOn) < 0.5f;
                    said.add ("COMP's picture -40 -> " + juce::String (reset, 2));
                    if (std::abs (reset - params::compThresholdDefault) > 1.0e-4f || ! on || menus != 0 || ! undone)
                        wrong.add ("COMP's picture: a double click set " + juce::String (reset, 2) + " dB, " + (on ? "on" : "off") + ", "
                                   + juce::String (menus) + " menu(s), one undo " + (undone ? "gave it back" : "did not give it back"));
                }
            }
            check (wrong.isEmpty(), "a double click is the default — COMP's THRESHOLD and BLEND, the GATE on both amps, COMP's picture — "
                                    "one undo step, no menu, no field; a single click opens them after the double click's time: "
                                    + said.joinIntoString (", ") + (wrong.isEmpty() ? juce::String() : "; WRONG: " + wrong.joinIntoString ("; ")));
        }

        // STEP 14: THE CAB'S DI/CAB IS A RAIL (the owner, 01.11: as every other BLEND and MIX): a GuardRail at the block's
        // right edge over the content's whole height, the waveform taking the room up to it; its orange grip writes "30" at
        // 13 px, a click on the grip types it, a typed number ONE undo step, a double click its default (one step); a right
        // click on it is the CAB's menu; no knob left in the block; the guitar's CAB has no rail.
        {
            auto t = make (bass);
            std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
            auto g = make (products::orbitAmp());
            std::unique_ptr<juce::AudioProcessorEditor> ged (g->createEditor());
            const auto settle = [] (AmpProcessor& a) { for (int i = 0; i < 40; ++i) a.history.tick(); };
            const auto find = [] (juce::Component& root, auto* none)
            {
                using T = std::remove_pointer_t<decltype (none)>;
                std::vector<T*> found;
                std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
                {
                    if (auto* x = dynamic_cast<T*> (&c))
                        found.push_back (x);
                    for (auto* child : c.getChildren())
                        walk (*child);
                };
                walk (root);
                return found;
            };
            const auto event = [] (juce::Component& c, juce::Point<float> at, int clicks, bool right)
            {
                const auto now = juce::Time::getCurrentTime();
                return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at,
                                         right ? juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier) : juce::ModifierKeys(), 0.0f,
                                         juce::MouseInputSource::defaultOrientation, juce::MouseInputSource::defaultRotation,
                                         juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY,
                                         &c, &c, now, at, now, clicks, false);
            };
            const auto cabs = find (*ed, (CabinetBlock*) nullptr);
            const auto gcabs = find (*ged, (CabinetBlock*) nullptr);
            const auto faces = find (*ed, (FaceplateView*) nullptr);
            juce::StringArray wrong;
            juce::String said;
            if (cabs.size() != 1 || gcabs.size() != 1 || faces.size() != 1)
                wrong.add ("no cabinet or no face");
            else
            {
                auto& cab = *cabs[0];
                auto* rail = cab.diRail();
                if (gcabs[0]->diRail() != nullptr || ! find (*gcabs[0], (GuardRail*) nullptr).empty())
                    wrong.add ("the guitar's CAB wears a rail");
                if (const auto knobs = find (cab, (Knob*) nullptr); ! knobs.empty())
                    wrong.add (juce::String ((int) knobs.size()) + " knob(s) left in the CAB");
                if (rail == nullptr || rail->getParentComponent() != &cab)
                    wrong.add ("no DI/CAB rail in the CAB");
                else
                {
                    const auto& wave = cab.waveView();
                    said = "the rail " + rail->getBounds().toString() + ", the waveform " + wave.getBounds().toString() + " in "
                         + cab.getLocalBounds().toString();
                    if (rail->getRight() != cab.getWidth() - BlockFrame::contentInsetX() || rail->getWidth() != rail->columnWidth
                        || rail->getY() != wave.getY() || rail->getBottom() < wave.getBottom() + 20 || rail->getBottom() > cab.getHeight()
                        || wave.getRight() > rail->getX() || rail->getX() - wave.getRight() > 10)
                        wrong.add ("the layout: " + said);
                    // A FADER as every BLEND and MIX on the bass's face (the owner, 01.11): violet, its percent under it at 13 px.
                    if (rail->paramId() != params::diMix || rail->gripText() != "30%" || ! rail->hasFaderLook() || fader::textH < 13.0f
                        || rail->columnWidth != fader::columnW || rail->typeDecimals < 0 || rail->getTooltip() != "DI/CAB: DI with cabinet")
                        wrong.add ("the rail reads \"" + rail->gripText() + "\" at " + juce::String (rail->gripTextHeight, 0) + " px, hint \""
                                   + rail->getTooltip() + "\"");

                    settle (*t);
                    rail->typeValue (55.0f);
                    settle (*t);
                    const float typed = value (*t, params::diMix);
                    t->history.undo();
                    if (std::abs (typed - 55.0f) > 1.0e-3f || std::abs (value (*t, params::diMix) - 30.0f) > 1.0e-3f)
                        wrong.add ("typed 55 -> " + juce::String (typed, 2) + ", one undo -> " + juce::String (value (*t, params::diMix), 2));

                    set (*t, params::diMix, 70.0f);
                    settle (*t);
                    const juce::Point<float> grip ((float) rail->getWidth() * 0.5f, rail->yOfValue (70.0f));
                    rail->mouseDown (event (*rail, grip, 1, false));
                    rail->mouseUp (event (*rail, grip, 1, false));
                    rail->mouseDown (event (*rail, grip, 2, false));
                    rail->mouseDoubleClick (event (*rail, grip, 2, false));
                    rail->mouseUp (event (*rail, grip, 2, false));
                    const auto until = juce::Time::getMillisecondCounter() + (juce::uint32) (juce::jmin (300, juce::MouseEvent::getDoubleClickTimeout()) + 300);
                    while (juce::Time::getMillisecondCounter() < until)
                    {
                        juce::Thread::sleep (10);
                        juce::Timer::callPendingTimersSynchronously();
                    }
                    settle (*t);
                    const float reset = value (*t, params::diMix);
                    const bool field = rail->isTyping();
                    t->history.undo();
                    if (std::abs (reset - params::diMixDefault) > 1.0e-3f || field || std::abs (value (*t, params::diMix) - 70.0f) > 1.0e-3f)
                        wrong.add ("a double click at 70 -> " + juce::String (reset, 2) + (field ? ", a field" : "") + ", one undo -> "
                                   + juce::String (value (*t, params::diMix), 2));

                    auto& face = *faces[0];
                    face.lastMenu = {};
                    const auto mid = rail->getLocalBounds().getCentre().toFloat();
                    rail->mouseDown (event (*rail, mid, 1, true));
                    rail->mouseUp (event (*rail, mid, 1, true));
                    if (face.lastMenu != "CAB IR")
                        wrong.add ("a right click on the rail opened \"" + face.lastMenu + "\"");
                }
            }
            check (wrong.isEmpty(), "the CAB's DI/CAB a rail at its right edge, the content's height, the waveform up to it; a fader, \"30%\" "
                                    "under it at 13 px, typed one undo step, a double click its default, a right click the CAB's menu; no knob; "
                                    "none on the guitar's: " + said + (wrong.isEmpty() ? juce::String() : "; WRONG: " + wrong.joinIntoString ("; ")));
        }
    }

    // STILLS FOR A LOOK (ORBITBASS_STILLS, as above): OrbitBass's face as it opens, with DELAY and REVERB added at the least
    // width and at 16:9 4K, a phrase played through it, its pictures fed; DELAY's and REVERB's windows.
    if (const auto stills = juce::SystemStats::getEnvironmentVariable ("ORBITBASS_STILLS", {}); stills.isNotEmpty())
    {
        const juce::File dir (stills);
        const auto write = [&dir] (const juce::String& name, const juce::Image& image)
        {
            juce::PNGImageFormat png;
            juce::FileOutputStream os (dir.getChildFile (name));
            if (os.openedOk())
            {
                os.setPosition (0);
                os.truncate();
                png.writeImageToStream (image, os);
            }
        };
        const auto feed = [] (juce::Component& root)
        {
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
            {
                if (auto* p = dynamic_cast<SpacePicture*> (&c))  p->pull();
                if (auto* p = dynamic_cast<ChorusPicture*> (&c)) p->pull();
                if (auto* p = dynamic_cast<OctaveScope*> (&c))   p->pull();
                if (auto* p = dynamic_cast<FilterCurve*> (&c))   p->pull();
                if (auto* p = dynamic_cast<CompGraph*> (&c))     p->pull();
                for (auto* child : c.getChildren())
                    walk (*child);
            };
            walk (root);
        };
        for (const int mode : { 0, 1 })
        {
            auto t = make (bass);
            if (mode == 1)
            {
                set (*t, params::delayPresent, 1.0f);
                set (*t, params::reverbPresent, 1.0f);
                set (*t, params::delayMix, 35.0f);
                set (*t, params::delayRepeats, 45.0f);
                set (*t, params::delayDiv, 7.0f);   // 1/8.
            }
            set (*t, params::chorusAmount, 0.5f);
            set (*t, params::compAmount, 0.6f);
            std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
            for (auto* w : { &t->compWatched, &t->octaveWatched, &t->filterWatched, &t->chorusWatched, &t->delayWatched, &t->reverbWatched })
                w->store (true);
            (void) play (*t, 150, false);
            feed (*ed);
            const int h = ed->getHeight();   // it opens at its least width, as its strip stands
            write (mode == 0 ? "orbitbass-default.png" : "orbitbass-space-least.png", ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f));
            if (mode == 1)
            {
                ed->setSize (juce::roundToInt ((float) h * 16.0f / 9.0f), h);
                write ("orbitbass-space-16x9-4k.png", ed->createComponentSnapshot (ed->getLocalBounds(), true, 2160.0f / (float) h));
            }
        }

        // STEP 10: COMP squeezing at BLEND 100, its block close; then OCTAVE and CHORUS stood by and played on — their
        // pictures still where they stood, dimmed with their blocks.
        {
            auto t = make (bass);
            set (*t, params::compAmount, 1.0f);
            set (*t, params::compThreshold, -30.0f);
            set (*t, params::octaveAmount, 0.6f);
            set (*t, params::chorusAmount, 0.5f);
            std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
            for (auto* w : { &t->compWatched, &t->octaveWatched, &t->filterWatched, &t->chorusWatched, &t->delayWatched, &t->reverbWatched })
                w->store (true);
            (void) play (*t, 120, false);
            feed (*ed);
            std::function<void (juce::Component&)> findComp = [&] (juce::Component& c)
            {
                if (auto* cb = dynamic_cast<CompBlock*> (&c))
                    write ("comp-squeeze.png", cb->createComponentSnapshot (cb->getLocalBounds(), true, 3.0f));
                for (auto* child : c.getChildren())
                    findComp (*child);
            };
            findComp (*ed);
            write ("orbitbass-playing.png", ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f));
            set (*t, params::octaveOn, 0.0f);
            set (*t, params::chorusOn, 0.0f);
            (void) play (*t, 120, false);
            feed (*ed);
            write ("orbitbass-standing-by.png", ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f));
        }

        // STEP 11: COMP at BLEND 100 on a low E under its line (no squeeze) — its three traces on each other; and at BLEND 0,
        // still and dimmed.
        for (const float blend : { 1.0f, 0.0f })
        {
            auto t = make (bass);
            set (*t, params::compAmount, blend);
            std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
            t->compWatched.store (true);
            if (blend > 0.0f)
                (void) tone (*t, 41.2, -40.0f, 150);
            else
            {
                set (*t, params::compAmount, 1.0f);
                (void) tone (*t, 41.2, -12.0f, 150);
                set (*t, params::compAmount, 0.0f);
                (void) tone (*t, 41.2, -12.0f, 60);
            }
            feed (*ed);
            std::function<void (juce::Component&)> findComp = [&] (juce::Component& c)
            {
                if (auto* cb = dynamic_cast<CompBlock*> (&c))
                    write (blend > 0.0f ? "comp-low-e-no-squeeze.png" : "comp-blend-0-still.png",
                           cb->createComponentSnapshot (cb->getLocalBounds(), true, 3.0f));
                for (auto* child : c.getChildren())
                    findComp (*child);
            };
            findComp (*ed);
        }

        // STEP 12: COMP squeezing at BLEND 60 (its three traces apart), its block close; its menu open with ATTACK's submenu, and DELAY's with its TIME; the whole
        // face; GATE before (ORBITBASS_STILLS_BASE: the old stills, cropped where the GATE stands) and after, both amps.
        {
            auto t = make (bass);
            set (*t, params::compAmount, 0.6f);
            set (*t, params::compThreshold, -30.0f);
            set (*t, params::delayPresent, 1.0f);
            std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
            for (auto* w : { &t->compWatched, &t->octaveWatched, &t->filterWatched, &t->chorusWatched, &t->delayWatched, &t->reverbWatched })
                w->store (true);
            (void) play (*t, 120, false);
            feed (*ed);
            CompBlock* cb = nullptr;
            juce::Component* delayBlock = nullptr;
            std::function<void (juce::Component&)> find = [&] (juce::Component& c)
            {
                if (auto* x = dynamic_cast<CompBlock*> (&c)) cb = x;
                if (auto* x = dynamic_cast<DelayBlock*> (&c)) delayBlock = x;
                for (auto* child : c.getChildren())
                    find (*child);
            };
            find (*ed);
            if (cb != nullptr)
                write ("comp-squeeze.png", cb->createComponentSnapshot (cb->getLocalBounds(), true, 3.0f));
            write ("orbitbass-face.png", ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f));
            // A menu laid over the face, its first item's submenu opened as the keyboard opens it.
            const auto menuShot = [&] (juce::Component* block, bass::Menu m, int down, const juce::String& file)
            {
                if (block == nullptr)
                    return;
                const int before = ed->getNumChildComponents();
                m.items.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (block).withParentComponent (ed.get()));
                if (ed->getNumChildComponents() > before)
                {
                    auto* window = ed->getChildComponent (ed->getNumChildComponents() - 1);
                    for (int k = 0; k < down; ++k)
                        window->keyPressed (juce::KeyPress (juce::KeyPress::downKey));
                    if (down >= 0)   // -1: the menu alone, no submenu opened
                        window->keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
                    auto area = block->getBounds().getUnion (window->getBounds());
                    for (int i = before; i < ed->getNumChildComponents(); ++i)
                        area = area.getUnion (ed->getChildComponent (i)->getBounds());
                    write (file, ed->createComponentSnapshot (area.expanded (8).getIntersection (ed->getLocalBounds()), true, 2.0f));
                }
                juce::PopupMenu::dismissAllActiveMenus();
            };
            menuShot (cb, bass::compMenu (t->apvts), 1, "comp-menu-attack.png");
            menuShot (delayBlock, bass::delayMenu (t->apvts), 3, "delay-menu-time.png");
            // STEP 13: COMP's menu and GATE's, ON first and ruled off — GATE off as it opens, then on at -50.
            menuShot (cb, bass::blockMenu (t->apvts, "COMP"), -1, "step13-comp-menu.png");
            FaceplateView* face = nullptr;
            std::function<void (juce::Component&)> findFace = [&] (juce::Component& c)
            {
                if (auto* x = dynamic_cast<FaceplateView*> (&c)) face = x;
                for (auto* child : c.getChildren())
                    findFace (*child);
            };
            findFace (*ed);
            if (auto* editor = dynamic_cast<AmpEditor*> (ed.get()); editor != nullptr && face != nullptr)
            {
                bass::Menu off;
                off.items = editor->gateMenu();
                menuShot (&face->gateBlock(), std::move (off), -1, "step13-gate-menu-off.png");
                set (*t, params::gateThreshold, -50.0f);
                set (*t, params::gateOn, 1.0f);
                bass::Menu on;
                on.items = editor->gateMenu();
                menuShot (&face->gateBlock(), std::move (on), -1, "step13-gate-menu-on.png");
            }
        }
        // STEP 14: COMP squeezing at BLEND 60 with its ratio's combo closed, then its list open over the face; and at 1.5:1,
        // the gentlest, its square's curve.
        {
            auto t = make (bass);
            set (*t, params::compAmount, 0.6f);
            set (*t, params::compThreshold, -30.0f);
            std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
            t->compWatched.store (true);
            (void) play (*t, 120, false);
            feed (*ed);
            CompBlock* cb = nullptr;
            std::function<void (juce::Component&)> find = [&] (juce::Component& c)
            {
                if (auto* x = dynamic_cast<CompBlock*> (&c)) cb = x;
                for (auto* child : c.getChildren())
                    find (*child);
            };
            find (*ed);
            if (cb != nullptr)
            {
                const auto blockArea = ed->getLocalArea (cb, cb->getLocalBounds());
                write ("step14-comp-combo-closed.png", ed->createComponentSnapshot (blockArea, true, 3.0f));
                auto& combo = cb->ratio();
                const int before = ed->getNumChildComponents();
                combo.list().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&combo).withMinimumWidth (combo.getWidth())
                                                                      .withParentComponent (ed.get()));
                if (ed->getNumChildComponents() > before)
                {
                    auto area = blockArea;
                    for (int i = before; i < ed->getNumChildComponents(); ++i)
                        area = area.getUnion (ed->getChildComponent (i)->getBounds());
                    write ("step14-comp-combo-open.png", ed->createComponentSnapshot (area.expanded (8).getIntersection (ed->getLocalBounds()), true, 3.0f));
                }
                juce::PopupMenu::dismissAllActiveMenus();
                set (*t, params::compRatio, 1.5f);
                (void) play (*t, 120, false);
                feed (*ed);
                write ("step14-comp-ratio-1.5.png", ed->createComponentSnapshot (blockArea, true, 3.0f));
            }
        }
        // STEP 14: the CAB block with DI/CAB its rail, a phrase played through it.
        {
            auto t = make (bass);
            std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
            (void) play (*t, 120, false);
            feed (*ed);
            CabinetBlock* cab = nullptr;
            std::function<void (juce::Component&)> find = [&] (juce::Component& c)
            {
                if (auto* x = dynamic_cast<CabinetBlock*> (&c)) cab = x;
                for (auto* child : c.getChildren())
                    find (*child);
            };
            find (*ed);
            if (cab != nullptr)
                write ("step14-cab-rail.png", ed->createComponentSnapshot (ed->getLocalArea (cab, cab->getLocalBounds()), true, 3.0f));
        }
        for (const auto* g : { &products::orbitAmp(), &bass })
        {
            auto t = make (*g);
            std::unique_ptr<juce::AudioProcessorEditor> ed (t->createEditor());
            const auto tag = juce::String (g->name).toLowerCase();
            GuardBlock* gate = nullptr;
            std::function<void (juce::Component&)> find = [&] (juce::Component& c)
            {
                if (auto* x = dynamic_cast<FaceplateView*> (&c)) gate = &x->gateBlock();
                for (auto* child : c.getChildren())
                    find (*child);
            };
            find (*ed);
            if (gate == nullptr)
                continue;
            // THE GUITAR'S FACE AS IT WAS, BUT ITS GATE (ORBITBASS_STILLS_BASE): every pixel outside the GATE's block the old
            // still's, at each width the stills are written at (CatpitAmp's own still, written above, compares as a file).
            if (const auto base = juce::SystemStats::getEnvironmentVariable ("ORBITBASS_STILLS_BASE", {}); base.isNotEmpty() && g != &bass)
            {
                const int h = ed->getHeight(), w0 = ed->getWidth();
                for (const int width : { w0, juce::roundToInt ((float) h * 16.0f / 9.0f) })
                {
                    ed->setSize (width, h);
                    const auto now = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
                    const auto was = juce::ImageFileFormat::loadFrom (juce::File (base).getChildFile ("OrbitAmp-" + juce::String (width) + ".png"));
                    const auto gateArea = ed->getLocalArea (gate, gate->getLocalBounds());
                    int outside = 0, inside = 0;
                    for (int y = 0; was.isValid() && y < now.getHeight(); ++y)
                        for (int x = 0; x < now.getWidth(); ++x)
                            if (now.getPixelAt (x, y) != was.getPixelAt (x, y))
                                ++(gateArea.contains (x, y) ? inside : outside);
                    std::printf ("stills: OrbitAmp at %d px — %d pixels differ outside the GATE, %d inside it%s\n", width, outside, inside,
                                 was.isValid() ? "" : " (no old still)");
                }
                ed->setSize (w0, h);
            }
            const float scale = g == &bass ? 2.0f : 1.0f;   // as the old stills were written
            const auto area = ed->getLocalArea (gate, gate->getLocalBounds());
            write (tag + "-gate-after-off.png", ed->createComponentSnapshot (area, true, 3.0f));
            set (*t, params::gateOn, 1.0f);
            set (*t, params::gateThreshold, -80.0f);
            write (tag + "-gate-after-on-lowest.png", ed->createComponentSnapshot (area, true, 3.0f));
            if (const auto base = juce::SystemStats::getEnvironmentVariable ("ORBITBASS_STILLS_BASE", {}); base.isNotEmpty())
            {
                const auto old = juce::ImageFileFormat::loadFrom (juce::File (base).getChildFile (g == &bass ? "orbitbass-default.png"
                                                                                                                 : "OrbitAmp-" + juce::String (ed->getWidth()) + ".png"));
                if (old.isValid())
                    write (tag + "-gate-before-off.png", old.getClippedImage ((area.toFloat() * scale).toNearestInt()).rescaled (
                                                             juce::roundToInt ((float) area.getWidth() * 3.0f), juce::roundToInt ((float) area.getHeight() * 3.0f)));
            }
        }
    }

    // ---- A PACK'S TONE SWITCHES AS CELLS OF THE AMP'S TONE ROW (the owner, 02.11): ULTRA LO and ULTRA HI, the SGT-DI's —
    //      HPF, ULTRA LO, BASS, MIDDLE, TREBLE, ULTRA HI, LPF — and no longer under GAIN: inside the AMP and clear of all
    //      else at the least width and at 16:9, the well and the dials as with no switch; a pack with none, the row's five
    //      cells as they were; the bass's GAIN and lever sections narrower, the well wider by them; each a hand's toggle on
    //      its selector's parameter, a click or a drag one position and one gesture; a position called off written 0; and
    //      a guitar amp's picks under its GAIN, its sections as wide as ever. ----
    {
        // A FAKE PACK: the set's ReVolt Clean with Ultra Lo (-, off, +) and Ultra Hi (off, +) in place of its Boost, every
        // combination on a capture it has — Ultra Hi's + on the Boost's; or, `switches` false, its Gain alone.
        const auto fake = [] (AmpProcessor& t, bool switches)
        {
            int at = -1;
            for (int i = 0; i < t.preamp.packs.size() && at < 0; ++i)
                if (t.preamp.packs.getReference (i).location.getFileName().startsWith ("Two Notes ReVolt Clean"))
                    at = i;
            if (at < 0)
                return false;
            auto pack = t.preamp.packs.getReference (at);
            auto* stage = const_cast<namz::rig::Stage*> (pack.rig.firstKnown());
            if (stage == nullptr)
                return false;
            auto& dev = stage->device;
            std::vector<namz::rig::Control> controls;
            if (switches)
            {
                controls.push_back ({ "Ultra Lo", namz::rig::Role::Generic, { "-", "off", "+" }, {}, 0 });
                controls.push_back ({ "Ultra Hi", namz::rig::Role::Generic, { "off", "+" }, {}, 0 });
            }
            for (const auto& c : dev.controls)
                if (c.role == namz::rig::Role::Gain)
                    controls.push_back (c);
            std::vector<namz::rig::FileEntry> files;
            for (const auto& f : dev.files)
            {
                const auto boost = f.settings.find ("Boost");
                const bool on = boost != f.settings.end() && boost->second == "on";
                for (const char* lo : { "-", "off", "+" })
                    for (const char* hi : { "off", "+" })
                        if ((std::string (hi) == "+") == on && (switches || (std::string (lo) == "-" && ! on)))
                        {
                            auto g = f;
                            g.settings.erase ("Boost");
                            if (switches)
                            {
                                g.settings["Ultra Lo"] = lo;
                                g.settings["Ultra Hi"] = hi;
                            }
                            files.push_back (g);
                        }
            }
            dev.controls = controls;
            dev.files = files;
            t.preamp.packs.set (at, pack);
            set (t, params::preampDevice, (float) at);
            t.preamp.select (at);
            t.pumpDeviceWork();
            return t.preamp.selectors().size() == (switches ? 2 : 0);
        };

        const auto panelIn = [] (juce::Component& root)
        {
            CapturedBlockPanel* found = nullptr;
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
            {
                if (auto* p = dynamic_cast<CapturedBlockPanel*> (&c); p != nullptr && found == nullptr)
                    found = p;
                for (auto* child : c.getChildren())
                    walk (*child);
            };
            walk (root);
            return found;
        };

        // What one AMP shows: its toggles and the tone row's dials and cut switches left to right, its picks (any VSwitch),
        // the picture well, GAIN (its tallest knob), SMOOTH and BLEND.
        struct Look
        {
            CapturedBlockPanel* panel = nullptr;
            std::vector<ToggleSwitch*> toggles;
            std::vector<VSwitch*> picks;
            std::vector<juce::Rectangle<int>> dials, cuts;
            juce::Rectangle<int> well, gain, smooth, blend;
        };
        const auto look = [&] (juce::Component& ed)
        {
            Look l;
            l.panel = panelIn (ed);
            if (l.panel == nullptr)
                return l;
            for (auto* c : l.panel->getChildren())
                if (dynamic_cast<DeviceScope*> (c) != nullptr)
                    l.well = c->getBounds();
            std::vector<juce::Rectangle<int>> knobs;
            for (auto* c : l.panel->getChildren())
            {
                if (! c->isVisible())
                    continue;
                if (auto* s = dynamic_cast<ToggleSwitch*> (c))
                    l.toggles.push_back (s);
                if (auto* v = dynamic_cast<VSwitch*> (c))
                    l.picks.push_back (v);
                if (dynamic_cast<Knob*> (c) != nullptr)
                    knobs.push_back (c->getBounds());
                if (dynamic_cast<ZoneSwitch*> (c) != nullptr && c->getY() >= l.well.getBottom())
                    l.cuts.push_back (c->getBounds());
                if (auto* t = dynamic_cast<juce::SettableTooltipClient*> (c); t != nullptr && (t->getTooltip().startsWith ("SMOOTH:") || t->getTooltip().startsWith ("STEP:")))
                    l.smooth = c->getBounds();
                if (dynamic_cast<GuardRail*> (c) != nullptr)
                    l.blend = c->getBounds();
            }
            for (const auto& k : knobs)
            {
                if (k.getHeight() > l.gain.getHeight())
                    l.gain = k;
                if (k.getY() >= l.well.getBottom())
                    l.dials.push_back (k);
            }
            const auto byX = [] (const auto& a, const auto& b) { return a.getX() < b.getX(); };
            std::sort (l.dials.begin(), l.dials.end(), byX);
            std::sort (l.cuts.begin(), l.cuts.end(), byX);
            std::sort (l.toggles.begin(), l.toggles.end(), [] (auto* a, auto* b) { return a->getX() < b->getX(); });
            return l;
        };
        const auto mouse = [] (juce::Component& c, juce::Point<float> at, juce::Point<float> down)
        {
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, juce::ModifierKeys(), 0.0f,
                                     juce::MouseInputSource::defaultOrientation, juce::MouseInputSource::defaultRotation,
                                     juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY,
                                     &c, &c, now, down, now, 1, at != down);
        };
        struct Gestures final : juce::AudioProcessorParameter::Listener
        {
            int begun = 0;
            void parameterValueChanged (int, float) override {}
            void parameterGestureChanged (int, bool starting) override { begun += starting ? 1 : 0; }
        };
        static_assert (ToggleSwitch::fontH >= 13.0f, "a toggle's words at reading size");
        const float middleW = theme::trackedWidth ("MIDDLE", theme::displayFont (13.0f), 0.15f);   // the widest dial's name

        auto withSwitches = make (bass), plainPack = make (bass), stock = make (bass);
        const bool faked = fake (*withSwitches, true) && fake (*plainPack, false);
        std::unique_ptr<juce::AudioProcessorEditor> ed (withSwitches->createEditor()), plainEd (plainPack->createEditor()),
                                                    stockEd (stock->createEditor());
        const int h = ed->getHeight();
        juce::StringArray inRow, asBefore, narrower;
        juce::String cells;   // the row's cells at the least width, for the line
        for (const int width : { -1, juce::roundToInt ((float) h * 16.0f / 9.0f) })
        {
            for (auto* e : { ed.get(), plainEd.get(), stockEd.get() })
                e->setSize (width < 0 ? 10 : width, h);   // narrower than the least: laid out at the least
            const juce::String where = "at " + juce::String (ed->getWidth()) + " px: ";
            const auto l = look (*ed), plain = look (*plainEd), bare = look (*stockEd);
            if (l.panel == nullptr || plain.panel == nullptr || bare.panel == nullptr)
            {
                inRow.add (where + "no AMP");
                continue;
            }

            // WITH SWITCHES: HPF, ULTRA LO, the three dials, ULTRA HI, LPF, in that order, nothing over anything.
            if (l.toggles.size() != 2 || l.dials.size() != 3 || l.cuts.size() != 2 || ! l.picks.empty())
            {
                inRow.add (where + juce::String ((int) l.toggles.size()) + " toggles, " + juce::String ((int) l.dials.size()) + " dials, "
                           + juce::String ((int) l.cuts.size()) + " cuts, " + juce::String ((int) l.picks.size()) + " picks");
                continue;
            }
            std::vector<juce::Rectangle<int>> row { l.cuts[0], l.toggles[0]->getBounds(), l.dials[0], l.dials[1], l.dials[2],
                                                    l.toggles[1]->getBounds(), l.cuts[1] };
            for (size_t k = 1; k < row.size(); ++k)
                if (row[k].getX() < row[k - 1].getRight())
                    inRow.add (where + "cell " + juce::String ((int) k) + " " + row[k].toString() + " left of " + row[k - 1].toString());
            for (size_t k = 0; k < 2; ++k)
            {
                auto* s = l.toggles[k];
                const auto b = s->getBounds();
                const juce::String which = s->getTooltip().upToFirstOccurrenceOf (":", false, false);
                if (which != (k == 0 ? "ULTRA LO" : "ULTRA HI"))
                    inRow.add (where + "toggle " + juce::String ((int) k) + " is " + which);
                if (b != l.panel->console().innerCell ((int) k) || b.getY() != l.dials[0].getY() || b.getBottom() != l.cuts[0].getBottom())
                    inRow.add (where + which + " at " + b.toString() + ": not from the names' line " + juce::String (l.dials[0].getY())
                               + " to the cuts' foot " + juce::String (l.cuts[0].getBottom()));
                if (b.getWidth() < s->idealWidth() || b.getWidth() < EqSection::cutW)
                    inRow.add (where + which + " " + juce::String (b.getWidth()) + " wide, its words " + juce::String (s->idealWidth()));
                if (! l.panel->sections()[2].contains (b) || ! l.panel->getLocalBounds().reduced (1).contains (b))
                    inRow.add (where + which + " outside the AMP's tone row");
                for (auto* c : l.panel->getChildren())
                    if (c != s && c->isVisible() && c->getBounds().intersects (b))
                        inRow.add (where + which + " over " + juce::String (typeid (*c).name()) + " " + c->getBounds().toString());
            }
            for (size_t k = 0; k < 3 && k < plain.dials.size(); ++k)
                if (l.dials[k].getY() != plain.dials[k].getY() || l.dials[k].getHeight() != plain.dials[k].getHeight()
                    || (float) l.dials[k].getWidth() < juce::jmax ((float) Knob::smallSide, middleW))
                    inRow.add (where + "dial " + juce::String ((int) k) + " " + l.dials[k].toString() + ", with no switch " + plain.dials[k].toString());
            if (l.well != plain.well)
                inRow.add (where + "the well " + l.well.toString() + ", with no switch " + plain.well.toString());
            if (width < 0)
                cells << "HPF 64, ULTRA LO " << l.toggles[0]->getWidth() << ", dials " << l.dials[0].getWidth() << ", ULTRA HI "
                      << l.toggles[1]->getWidth() << ", LPF 64, the well " << l.well.getWidth() << " x " << l.well.getHeight();

            // WITHOUT: the five cells as the row always laid them — the cuts a cut's width each side of three equal dials
            // edge to edge — the same for a pack of no switch and ReVolt's, whose only one is BOOST, none on the bass.
            for (const auto* p : { &plain, &bare })
            {
                const juce::String who = p == &plain ? "no switch: " : "ReVolt's: ";
                if (! p->toggles.empty() || p->dials.size() != 3 || p->cuts.size() != 2 || ! p->picks.empty()
                    || ! p->panel->console().innerCell (0).isEmpty() || ! p->panel->console().innerCell (1).isEmpty())
                {
                    asBefore.add (where + who + juce::String ((int) p->toggles.size()) + " toggles, " + juce::String ((int) p->dials.size()) + " dials");
                    continue;
                }
                const auto s3 = p->panel->sections()[2];
                const int knobW = (s3.getWidth() - 2 * EqSection::cutW) / 3;
                for (int k = 0; k < 3; ++k)
                    if (p->dials[(size_t) k] != juce::Rectangle<int> (s3.getX() + EqSection::cutW + k * knobW, p->dials[0].getY(), knobW, p->dials[0].getHeight()))
                        asBefore.add (where + who + "dial " + juce::String (k) + " at " + p->dials[(size_t) k].toString());
                if (std::abs (p->cuts[0].getCentreX() - (s3.getX() + EqSection::cutW / 2)) > 1
                    || std::abs (p->cuts[1].getCentreX() - (s3.getRight() - EqSection::cutW / 2)) > 1)
                    asBefore.add (where + who + "the cuts' switches at " + p->cuts[0].toString() + " and " + p->cuts[1].toString());
            }
            if (plain.dials != bare.dials || plain.well != bare.well)
                asBefore.add (where + "no switch and ReVolt's apart");

            // THE BASS'S NARROWER SECTIONS: GAIN's column as wide as its dial or SMOOTH, BLEND right after it, the lever's 80
            // (the owner, 02.11); the well and its row of hands take the rest.
            for (const auto* p : { &l, &plain })
            {
                const auto& s = p->panel->sections();
                const int gainW = juce::jmax (p->gain.getWidth(), p->smooth.getWidth());
                if (s[1].getWidth() != 80 || s[0].getWidth() != gainW + CapturedBlockPanel::bassSideW || p->smooth.isEmpty()
                    || p->blend.getX() < s[0].getRight() - CapturedBlockPanel::bassSideW || p->blend.getRight() > s[0].getRight()
                    || p->well.getX() != s[2].getX() || p->well.getRight() != s[2].getRight())
                    narrower.add (where + "GAIN's section " + s[0].toString() + " (its dial " + p->gain.toString() + ", SMOOTH " + p->smooth.toString()
                                  + ", BLEND " + p->blend.toString() + "), the lever's " + s[1].toString() + ", the well " + p->well.toString()
                                  + " in " + s[2].toString());
            }
        }
        check (faked && inRow.isEmpty(), "a pack's tone switches are cells of the AMP's tone row (HPF, ULTRA LO, BASS, MIDDLE, TREBLE, ULTRA HI, "
                                         "LPF) at the least width and 16:9: from the names' line to the cuts' foot, as wide as their words, "
                                         "inside the AMP and over nothing, no pick under GAIN; the well and the dials as with no switch ("
                                         + cells + ")" + (inRow.isEmpty() ? juce::String() : ": " + inRow.joinIntoString ("; ")));
        check (faked && asBefore.isEmpty(), "with no switch the tone row is its five cells as before: HPF, three equal dials edge to edge, LPF"
                                            + (asBefore.isEmpty() ? juce::String() : ": " + asBefore.joinIntoString ("; ")));
        check (faked && narrower.isEmpty(), "the bass's GAIN section as wide as its dial or SMOOTH, BLEND right after it, the lever's section "
                                            "80, the well and its row of hands the rest" + (narrower.isEmpty() ? juce::String() : ": " + narrower.joinIntoString ("; ")));

        // Each moves its selector: a click one position toward where it lands, back from the end it is against; a drag
        // one position its way; one gesture each; the parameter moved from elsewhere moves the dot; the hint all of it.
        {
            juce::StringArray wrong;
            const auto l = look (*ed);
            for (size_t k = 0; k < l.toggles.size(); ++k)
            {
                auto& s = *l.toggles[k];
                const auto id = params::selectorId (params::preampId, (int) k);
                auto* p = withSwitches->apvts.getParameter (id);
                Gestures gestures;
                p->addListener (&gestures);
                const auto at = [&] { return juce::roundToInt (value (*withSwitches, id.toRawUTF8())); };
                const float lineY = (float) s.getHeight() - (float) ToggleSwitch::ovalH * 0.5f;
                const auto click = [&] (float x)
                {
                    const juce::Point<float> pt (x, lineY);
                    s.mouseDown (mouse (s, pt, pt));
                    s.mouseUp (mouse (s, pt, pt));
                    withSwitches->pumpDeviceWork();
                };
                const auto drag = [&] (float dx)
                {
                    const juce::Point<float> from ((float) s.getWidth() * 0.5f, lineY);
                    s.mouseDown (mouse (s, from, from));
                    s.mouseDrag (mouse (s, from.translated (dx * 0.5f, 0.0f), from));
                    s.mouseDrag (mouse (s, from.translated (dx, 0.0f), from));
                    s.mouseUp (mouse (s, from.translated (dx, 0.0f), from));
                    withSwitches->pumpDeviceWork();
                };
                const float right = (float) s.getWidth() - 2.0f;
                juce::String path (at());
                click (right);              // one up
                path << " " << at();
                drag (-40.0f);              // one down, however far
                path << " " << at();
                click (right);
                click (right);              // to the last, or back from it
                path << " " << at();
                const juce::String want = s.count() == 3 ? "0 1 0 2" : "0 1 0 0";
                if (path != want || gestures.begun != 4)
                    wrong.add (s.getTooltip() + ": " + path + " (want " + want + "), " + juce::String (gestures.begun) + " gestures");
                set (*withSwitches, id, 1.0f);
                if (s.selectedIndex() != 1)
                    wrong.add (s.getTooltip() + ": the parameter at 1, the dot at " + juce::String (s.selectedIndex()));
                const auto hint = s.getTooltip();
                for (const char* word : { "ULTRA", "-", "OFF", "+" })
                    if (! hint.contains (word) && ! (k == 1 && juce::String (word) == "-"))
                        wrong.add ("the hint \"" + hint + "\" names no " + word);
                p->removeListener (&gestures);
            }
            check (l.toggles.size() == 2 && wrong.isEmpty(), "each tone switch moves its selector, a click or a drag one position "
                                                             "and one gesture, the parameter its dot; its hint names it and every "
                                                             "position" + (wrong.isEmpty() ? juce::String() : ": " + wrong.joinIntoString ("; ")));
        }

        // A POSITION CALLED OFF IS WRITTEN 0, as the pedal marks it (the owner, 02.11), in any case, and so is a 0; every
        // other word as the pack has it. On the face ULTRA HI, whose pack says off, draws as a switch whose pack said 0.
        {
            juce::StringArray wrong;
            for (const auto& [in, out] : std::vector<std::pair<juce::String, juce::String>> {
                     { "OFF", "0" }, { "off", "0" }, { "Off", "0" }, { "0", "0" }, { "+", "+" }, { "-", "-" }, { "OFFSET", "OFFSET" }, { "ON", "ON" } })
                if (ToggleSwitch::markFor (in) != out)
                    wrong.add (in + " -> " + ToggleSwitch::markFor (in));
            const auto l = look (*ed);
            if (l.toggles.size() == 2)
            {
                auto& hi = *l.toggles[1];
                const auto shot = [] (juce::Component& c) { return c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f); };
                const auto same = [] (const juce::Image& a, const juce::Image& b)
                {
                    if (a.getBounds() != b.getBounds())
                        return false;
                    for (int y = 0; y < a.getHeight(); ++y)
                        for (int x = 0; x < a.getWidth(); ++x)
                            if (a.getPixelAt (x, y) != b.getPixelAt (x, y))
                                return false;
                    return true;
                };
                set (*withSwitches, params::selectorId (params::preampId, 1), 0.0f);   // at off, lit as the mark it is
                ToggleSwitch zero;   // a pack that says 0
                zero.setItems ("ULTRA HI", { "0", "+" }, 0);
                zero.setBounds (hi.getLocalBounds());
                if (! same (shot (hi), shot (zero)))
                    wrong.add ("ULTRA HI on the face does not read 0 ( ) +");
            }
            else
                wrong.add ("no ULTRA HI");
            check (wrong.isEmpty(), "a tone switch's position called off is written 0, in any case; every other word as the pack has it"
                                    + (wrong.isEmpty() ? juce::String() : ": " + wrong.joinIntoString ("; ")));
        }

        // A GUITAR AMP'S stay picks under its GAIN dial: no toggle, the same pack's switches under the dial in its section,
        // the tone row and the well as with no switch, its sections as wide as ever — 160 and 150.
        {
            juce::StringArray wrong;
            auto guitar = make (products::orbitAmp()), guitarPlain = make (products::orbitAmp());
            const bool fakedGuitar = fake (*guitar, true) && fake (*guitarPlain, false);
            std::unique_ptr<juce::AudioProcessorEditor> ge (guitar->createEditor()), gp (guitarPlain->createEditor());
            const int gh = ge->getHeight();
            for (const int width : { -1, juce::roundToInt ((float) gh * 16.0f / 9.0f) })
            {
                ge->setSize (width < 0 ? 10 : width, gh);
                gp->setSize (width < 0 ? 10 : width, gh);
                const auto l = look (*ge), plain = look (*gp);
                const juce::String where = "at " + juce::String (ge->getWidth()) + " px: ";
                if (l.panel == nullptr || plain.panel == nullptr || ! l.toggles.empty() || l.picks.size() != 2)
                {
                    wrong.add (where + juce::String ((int) l.toggles.size()) + " toggles, " + juce::String ((int) l.picks.size()) + " picks");
                    continue;
                }
                const auto& s = l.panel->sections();
                for (auto* v : l.picks)
                    if (v->getY() < l.gain.getBottom() || ! s[0].contains (v->getBounds()))
                        wrong.add (where + v->getTooltip().upToFirstOccurrenceOf (":", false, false) + " at " + v->getBounds().toString()
                                   + ", GAIN at " + l.gain.toString());
                if (s[0].getWidth() != 160 || s[1].getWidth() != 150 || plain.panel->sections() != s)
                    wrong.add (where + "sections " + s[0].toString() + ", " + s[1].toString() + ", " + s[2].toString());
                if (l.well != plain.well || l.dials != plain.dials || l.cuts != plain.cuts
                    || ! l.panel->console().innerCell (0).isEmpty() || ! l.panel->console().innerCell (1).isEmpty())
                    wrong.add (where + "the well " + l.well.toString() + " against " + plain.well.toString() + ", or the tone row moved");
            }
            check (fakedGuitar && wrong.isEmpty(), "OrbitAmp: the same pack's switches stay picks under GAIN, the tone row and the well as "
                                                   "without them, its sections 160 and 150 as ever" + (wrong.isEmpty() ? juce::String() : ": " + wrong.joinIntoString ("; ")));
        }
    }

    std::printf ("%s\n", failed == 0 ? "PASSED" : "FAILED");
    return failed == 0 ? 0 : 1;
}
