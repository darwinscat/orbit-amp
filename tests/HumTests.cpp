// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Gate for the hum at the input — the stage that takes mains hum off the signal before the INPUT
// trim, heard through the processor itself rather than the stage on its own: the stage has its own
// gate in felitronics-core, and none of that proves where the processor put it, what it is handed,
// or that a mode chosen on the machine ever reaches it.
//
// What is listened to is the TUNER'S TAP — the processor's own copy of its input after the trim,
// which is the input path and nothing else: no pack, no block, no latency. The rig is a hum the way a
// guitar rig picks one up (a mains a little off 50 Hz, ten harmonics) with a hiss far under it, and
// the hum and the hiss are kept apart, so what is left of the hum is read exactly: the tap, less the
// hiss at the gain the trim gave it, against the hum at that gain.
//
//   1. AUTO takes it off — at least 40 dB, once the stage has locked.
//   2. An INPUT trim moved 6 dB mid-stream does not bring it back, not for one 20 ms window. This is
//      why the stage sits ahead of the trim: behind it, it would meet a hum 6 dB louder than the one
//      it learned and hand the difference on until it learned again.
//   3. OFF hands the input on bit for bit — switched off mid-stream, and from a fresh start.
//   4. The demo loop, which stands in for the input and has no hum in it, passes untouched.
//   5. The mode is the machine's: a session going out and coming back neither carries nor moves it.
//   6. A choice made the way the menus make it (prefs::setHum) is written to the machine's settings
//      and followed by every engine of the product from its next block — two instances here.
//   7. The menus' lines: AUTO's names what it decided, once it has, and only while it is the mode.
//   8. HUM'S OWN LEARN, apart from the gate's: a cold one (AUTO undecided, the stage starting from
//      nothing) and a warm one each end on the stage's own timeline and keep the hum they heard; a
//      cancelled one throws its learn away and the stage goes back to its model; a string struck in the
//      quiet is DISTURBED and keeps nothing; and the words the glass says.
//   9. OFF IS A BYPASS THAT KEEPS WHAT WAS LEARNED. AUTO locked, OFF for two seconds, AUTO again: the
//      hum is 40 dB down within 50 ms of the way back, AUTO's decision never moved, and neither switch
//      bends the signal — the correction's second difference stays under -60 dBFS both ways.
//  10. The faces' quick A/B (the H key): OFF from a mode, and from OFF back to that mode — kept on the
//      machine beside the mode — with the words the face's tag says for a moment.
//  11. AUTO, undecided, says it is listening — in the menus' line and in the tag.
//  12. Every menu's HUM is a switch and a mode: "On" ticked unless OFF; the tick on the mode in force,
//      or under OFF on the mode OFF bypasses; a mode picked under OFF switches the hum on in it.
//  13. A second input the chain never hears — MONO and STEREO SPACE play channel 1 alone — does not
//      keep AUTO from deciding: a -60 dBFS hum beside an empty input with its gain up, hissing at -50.
//  14. HOW HARD IT WORKS — the temporary tuning: "Up to" 1.2 kHz with the default widths is the stage as it
//      ran before, bit for bit (the stage of 64 harmonics, its own params, beside the product's of 128), the
//      defaults are 2 kHz and the widths as they were, and every choice of the three reaches the stage's params.
//
// Checks 1-5 set the mode through the cached atomic the engine reads (prefs::humCache) and leave the
// settings file alone. Checks 6 and 7 write the file, so the gate first moves HOME into a folder of
// its own; where the store does not land there (a platform that does not take its settings from HOME),
// they say so and skip rather than rewrite the choice of the machine they run on.
//
// HUM IS SWITCHED OFF in every amp for now (src/Features.h). The checks above are the stage's, and run
// in the gate built with the switch thrown (orbitamp_hum_test). Built as the products are, with HUM off
// (orbitamp_hum_off_test), the gate holds the switch instead, and nothing else: the machine set to AUTO
// — as a machine that chose it before keeps it — and still the stage is never prepared and never runs,
// the mode in force is OFF with no LEARN of its own, and the hummed input passes bit for bit.

#include "GateMain.h"
#include <juce_events/juce_events.h>

#include "PluginProcessor.h"
#include "ui/HumLearner.h"
#include "ui/HumMenu.h"

// OrbitAmp's first start (OrbitAmpStart.cpp) is NOT made from here: it would gather IRs in the machine's
// real folders.
// OrbitAmp as this gate plays it: its own product with no first start, so the gate never reaches the
// machine's real folders (Product::firstStart).
static const orbitamp::Product& gateAmp()
{
    static const orbitamp::Product quiet = []
    {
        auto p = orbitamp::products::orbitAmp();
        p.firstStart = nullptr;
        return p;
    }();
    return quiet;
}

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#if ! defined(_WIN32)
  #include <unistd.h>      // mkdtemp
#endif

namespace
{
    int failures = 0;

    void report (const char* what, bool ok, const juce::String& detail = {})
    {
        if (! ok)
            ++failures;

        std::printf ("%-52s %s  %s\n", what, ok ? "ok" : "FAIL", detail.toRawUTF8());
    }

    constexpr double sampleRate = 48000.0;
    constexpr int    blockSize  = 512;
    constexpr int    window     = 960;     // 20 ms: what "the hum came back" is read over
    constexpr double pi         = juce::MathConstants<double>::pi;

    using orbitamp::AmpProcessor;
    using Hum = orbitamp::prefs::Hum;
    namespace params = orbitamp::params;

    void set (AmpProcessor& amp, const char* id, float value)
    {
        auto* p = amp.apvts.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    void machine (Hum h) { orbitamp::prefs::humCache(gateAmp()).store ((int) h, std::memory_order_relaxed); }

    Hum inForce (const AmpProcessor& amp) { return (Hum) amp.humInForce.load(); }

    const char* name (Hum h)
    {
        switch (h)
        {
            case Hum::off:       return "OFF";
            case Hum::hz50:      return "50 Hz";
            case Hum::hz60:      return "60 Hz";
            case Hum::automatic: break;
        }
        return "AUTO";
    }

    /** One stretch of stream as the gate heard it, channel 0: the input as it went in, its hum and its
        hiss apart, the gain the trim gave each sample (the processor's own ramp, mirrored), and what
        the tap heard. */
    struct Heard
    {
        std::vector<float>  in, hum, hiss, out;
        std::vector<double> gain;

        size_t size() const { return out.size(); }
    };

    /** A rig with mains hum on its input, played into the processor block by block. */
    class Stream
    {
    public:
        explicit Stream (AmpProcessor& a) : amp (a), tap (std::make_unique<std::array<float, orbitamp::core::TunerTap::size>>()) {}

        static constexpr double mainsHz   = 50.03;   // a little off nominal, so the tracker has work
        static constexpr int    harmonics = 10;
        static constexpr double fundamental = 0.03;  // about -30 dBFS of hum in all
        static constexpr double hissRms     = 1.0e-5; // -100 dBFS: far under anything measured here

        bool   humming = true;      // false: the input carries nothing (the demo loop replaces it)
        double humGain = 1.0;       // 0: the hiss alone
        double hiss    = hissRms;   // the hiss's RMS
        std::function<double (long long)> extra;   // anything else on the input, by sample
        double secondHiss = -1.0;   // >= 0: channel 2 carries only a hiss of this RMS, a hum of its own none

        /** A restart, the way a host makes one: the trim's ramp is seeded from its parameter again. */
        void prepare()
        {
            amp.prepareToPlay (sampleRate, blockSize);
            lastGain = trimTarget();
        }

        /** Plays `seconds` more, in whole blocks, and returns where they start in `heard`. */
        size_t play (double seconds, Heard& heard)
        {
            return playSamples ((long long) std::ceil (seconds * sampleRate / blockSize) * blockSize, heard);
        }

        /** Plays `count` samples more, in blocks of at most `most`. */
        size_t playSamples (long long count, Heard& heard, int most = blockSize)
        {
            const size_t start = heard.size();
            juce::AudioBuffer<float> whole (2, blockSize);
            juce::MidiBuffer midi;

            for (long long done = 0; done < count;)
            {
                const int m = (int) std::min<long long> (std::min (most, blockSize), count - done);
                juce::AudioBuffer<float> buf (whole.getArrayOfWritePointers(), 2, m);
                const float target = trimTarget();
                const float step   = (target - lastGain) / (float) m;
                float g = lastGain;

                for (int i = 0; i < m; ++i, ++n)
                {
                    double hum[2] = { 0.0, 0.0 };
                    if (humming)
                        for (int k = 1; k <= harmonics; ++k)
                        {
                            const double ph = 2.0 * pi * k * mainsHz * (double) n / sampleRate + 0.7 * k;
                            hum[0] += humGain * fundamental / k * std::sin (ph);
                            hum[1] += humGain * 0.8 * fundamental / k * std::sin (ph + 0.3);
                        }

                    // A uniform hiss from a fixed seed: the same every run, √3 over its RMS at the peak.
                    seed = seed * 1664525u + 1013904223u;
                    double h = humming ? hiss * std::sqrt (3.0) * ((double) (seed >> 8) / 8388608.0 - 1.0) : 0.0;
                    if (extra != nullptr)
                        h += extra (n);

                    buf.setSample (0, i, (float) (hum[0] + h));
                    if (secondHiss >= 0.0)
                    {
                        seed2 = seed2 * 1664525u + 1013904223u;
                        buf.setSample (1, i, (float) (secondHiss * std::sqrt (3.0) * ((double) (seed2 >> 8) / 8388608.0 - 1.0)));
                    }
                    else
                        buf.setSample (1, i, (float) (hum[1] + h));
                    heard.in.push_back (buf.getSample (0, i));
                    heard.hum.push_back ((float) hum[0]);
                    heard.hiss.push_back ((float) h);
                    heard.gain.push_back (juce::exactlyEqual (target, lastGain) ? (double) lastGain : (double) g);
                    g += step;
                }
                lastGain = target;

                amp.processBlock (buf, midi);
                amp.tunerTap.read (*tap);
                heard.out.insert (heard.out.end(), tap->end() - m, tap->end());
                done += m;
            }
            return start;
        }

        long long now() const noexcept { return n; }

    private:
        float trimTarget() const
        {
            return juce::Decibels::decibelsToGain (amp.apvts.getRawParameterValue (params::inTrim)->load());
        }

        AmpProcessor& amp;
        std::unique_ptr<std::array<float, orbitamp::core::TunerTap::size>> tap;
        long long     n = 0;
        std::uint32_t seed = 12345u, seed2 = 777u;
        float         lastGain = 1.0f;
    };

    /** What is left of the hum over [from, to), in dB re the hum — both at the gain the trim gave them. */
    double leftDb (const Heard& h, size_t from, size_t to)
    {
        double left = 0.0, hum = 0.0;
        for (size_t i = from; i < to; ++i)
        {
            const double r = (double) h.out[i] - h.gain[i] * h.hiss[i];
            const double s = h.gain[i] * h.hum[i];
            left += r * r;
            hum  += s * s;
        }
        return 10.0 * std::log10 (std::max (left, 1.0e-30) / std::max (hum, 1.0e-30));
    }

    /** The worst 20 ms window over [from, to). */
    double worstDb (const Heard& h, size_t from, size_t to)
    {
        double worst = -300.0;
        for (size_t w = from; w + window <= to; w += window)
            worst = std::max (worst, leftDb (h, w, w + window));
        return worst;
    }

    /** What the tap heard over [from, to), dBFS rms. */
    double tapLevelDb (const Heard& h, size_t from, size_t to)
    {
        double s = 0.0;
        for (size_t i = from; i < to; ++i)
            s += (double) h.out[i] * h.out[i];
        return 10.0 * std::log10 (std::max (s / (double) std::max<size_t> (1, to - from), 1.0e-30));
    }

    /** The first sample over [from, to) where `a` and `b` differ in any bit, or -1. */
    long long firstDifference (const std::vector<float>& a, const std::vector<float>& b, size_t from, size_t to)
    {
        for (size_t i = from; i < to; ++i)
            if (std::memcmp (&a[i], &b[i], sizeof (float)) != 0)
                return (long long) i;
        return -1;
    }

    /** A demo loop with no hum in it: plucked notes down the low strings and back, every half a second,
        each a stack of partials dying away, and the last half second digital silence — a loop as the
        demo player meets one, written as a plain 16-bit WAV. */
    juce::File writeLoop (const juce::File& file)
    {
        constexpr int rate = 48000;
        const int total = rate * 9 / 2;
        const double notes[] = { 82.41, 110.0, 146.83, 196.0, 246.94, 329.63, 110.0, 82.41 };

        std::vector<std::int16_t> pcm ((size_t) total, 0);
        for (int i = 0; i < rate * 4; ++i)
        {
            double s = 0.0;
            for (int p = 0; p < 8; ++p)
            {
                const double t = (double) (i - p * rate / 2) / rate;
                if (t < 0.0)
                    continue;
                const double attack = std::min (1.0, t / 0.002);
                for (int h = 1; h <= 8; ++h)
                    s += 0.12 / h * attack * std::exp (-t / 0.35) * std::sin (2.0 * pi * h * notes[p] * t);
            }
            pcm[(size_t) i] = (std::int16_t) std::lround (juce::jlimit (-1.0, 1.0, s) * 32767.0);
        }

        file.deleteFile();
        juce::FileOutputStream out (file);
        const int bytes = total * 2;
        out.write ("RIFF", 4);  out.writeInt (36 + bytes);  out.write ("WAVE", 4);
        out.write ("fmt ", 4);  out.writeInt (16);  out.writeShort (1);  out.writeShort (1);
        out.writeInt (rate);    out.writeInt (rate * 2);  out.writeShort (2);  out.writeShort (16);
        out.write ("data", 4);  out.writeInt (bytes);
        for (const auto s : pcm)
            out.writeShort (s);
        out.flush();
        return file;
    }
}

int ORBITAMP_GATE_MAIN()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    std::printf ("orbitamp hum gate\n");

    // A HOME OF THE GATE'S OWN, before anything has asked where home is: macOS reads it once
    // (CFFIXED_USER_HOME), and the settings store, the device library and everything else follow it.
    std::string ownHome;
  #if ! defined(_WIN32)
    {
        const char* tmp = std::getenv ("TMPDIR");
        std::string pattern = std::string (tmp != nullptr && *tmp != 0 ? tmp : "/tmp") + "/orbitamp-hum-gate-XXXXXX";
        if (const char* made = mkdtemp (pattern.data()); made != nullptr)
        {
            ownHome = made;
            setenv ("HOME", made, 1);
            setenv ("CFFIXED_USER_HOME", made, 1);
        }
    }
  #endif

    const juce::ScopedJuceInitialiser_GUI juceInit;

    // On the heap, as a host holds it: the processor is far bigger than a Windows main thread's stack.
    const auto ampOwned = std::make_unique<AmpProcessor> (gateAmp());
    auto& amp = *ampOwned;

    // The input path the tap hears: the tuner listening (the INPUT's trim always applies).
    set (amp, params::tunerOn, 1.0f);
    set (amp, params::inTrim, 0.0f);

    Stream stream (amp);
    stream.prepare();

    // HUM SWITCHED OFF (Features.h) — the products' build. The machine on AUTO, through the cached atomic
    // the engine reads, and a restart, the way a host opens a session on a machine that chose it: no
    // stage, OFF in force, no LEARN, and four seconds of a hummed input on the tap bit for bit.
    if constexpr (! orbitamp::features::hum)
    {
        machine (Hum::automatic);
        stream.prepare();
        Heard heard;
        stream.play (4.0, heard);
        const long long d = firstDifference (heard.out, heard.in, 0, heard.size());
        report ("HUM off in the build: AUTO on the machine runs no stage",
                amp.humRuns.load() == -1 && inForce (amp) == Hum::off && ! amp.humLearn.on.load(),
                juce::String ("runs: ") + juce::String (amp.humRuns.load()) + ", in force: " + name (inForce (amp)));
        report ("...and the hummed input passes bit for bit", d < 0,
                d < 0 ? juce::String ("4 s identical, from the first sample") : juce::String::formatted ("differs at %.3f s", (double) d / sampleRate));

        if (failures == 0)
            std::printf ("\nall hum-off checks passed\n");
        else
            std::printf ("\n%d hum-off check(s) FAILED\n", failures);
        return failures == 0 ? 0 : 1;
    }

    // 0 · OFF BY DEFAULT — on a machine that has never said otherwise, with AUTO the mode it bypasses.
    {
        static_assert (orbitamp::prefs::humDefault == Hum::off && orbitamp::prefs::humFirstOn == Hum::automatic);
        if (orbitamp::prefs::store(gateAmp()).get (orbitamp::prefs::humMains).isVoid())
            report ("a machine that never chose: OFF, and its first On is AUTO",
                    inForce (amp) == Hum::off && orbitamp::prefs::humFlip(gateAmp()) == Hum::automatic && amp.humRuns.load() == (int) Hum::automatic,
                    juce::String ("in force: ") + name (inForce (amp)) + ", first On: " + name (orbitamp::prefs::humFlip(gateAmp())));
        else
            std::printf ("%-52s skip  this machine has chosen: %s\n", "a machine that never chose: OFF",
                         name (orbitamp::prefs::hum(gateAmp())));
    }

    // 1 · AUTO TAKES THE HUM OFF, once locked.
    Heard heard;
    machine (Hum::automatic);
    stream.prepare();
    {
        double lockedAt = -1.0;
        for (int b = 0; b < 16 && lockedAt < 0.0; ++b)    // up to 8 s, half a second at a time
        {
            stream.play (0.5, heard);
            if (amp.humLocked.load() && amp.humFamily.load() == 50)
                lockedAt = (double) heard.size() / sampleRate;
        }
        report ("AUTO decides 50 Hz and locks", lockedAt > 0.0,
                juce::String::formatted ("locked by %.1f s, tracking %.3f Hz, family %d, hum %.1f dB",
                                         lockedAt, amp.humHz.load(), amp.humFamily.load(), amp.humDb.load()));

        const size_t from = stream.play (1.0, heard);
        const double left = leftDb (heard, from, heard.size());
        const double worst = worstDb (heard, from, heard.size());
        report ("AUTO: the hum is 40 dB down after lock", left <= -40.0 && worst <= -40.0,
                juce::String::formatted ("%.1f dB over 1 s, worst 20 ms %.1f dB", left, worst));

        const auto line = orbitamp::prefs::humLabel (Hum::automatic, amp.humHeard());
        report ("the menus' AUTO line names what it decided", line == juce::String::fromUTF8 ("Auto \xe2\x80\x94 50 Hz"),
                "\"" + line + "\"");
    }

    // 2 · THE TRIM MOVES 6 dB AND THE HUM STAYS OFF.
    {
        const size_t before = stream.play (0.5, heard);
        set (amp, params::inTrim, 6.0f);
        const size_t from = stream.play (2.0, heard);
        // Read off the tap itself, not off the gain this gate mirrors: what is left there is the hiss,
        // 30 dB over what is left of the hum, so it rises by what the trim really did.
        const double rise = tapLevelDb (heard, from + (size_t) blockSize, heard.size()) - tapLevelDb (heard, before, from);
        const double worst = worstDb (heard, before, heard.size());
        report ("the trim reached the audio (+6 dB)", std::abs (rise - 6.0) < 0.2,
                juce::String::formatted ("the tap rose %.2f dB", rise));
        report ("INPUT +6 dB mid-stream: the hum stays 40 dB down", worst <= -40.0,
                juce::String::formatted ("worst 20 ms across the move %.1f dB", worst));
        set (amp, params::inTrim, 0.0f);
        stream.play (0.1, heard);
    }

    // 3 · OFF IS THE INPUT, BIT FOR BIT — switched off under a running stage, and from a fresh start.
    {
        machine (Hum::off);
        stream.play (0.1, heard);                       // the stage ramps its correction out over 20 ms
        const size_t from = stream.play (2.0, heard);
        const long long diff = firstDifference (heard.out, heard.in, from, heard.size());
        report ("OFF under a running stage: the input, bit for bit", diff < 0 && inForce (amp) == Hum::off,
                diff < 0 ? juce::String ("2 s identical") : juce::String::formatted ("differs at %.3f s", (double) (diff - (long long) from) / sampleRate));

        Heard fresh;
        stream.prepare();
        stream.play (4.0, fresh);
        const long long d = firstDifference (fresh.out, fresh.in, 0, fresh.size());
        report ("OFF from a fresh start: the input, bit for bit", d < 0,
                d < 0 ? juce::String ("4 s identical, from the first sample") : juce::String::formatted ("differs at %.3f s", (double) d / sampleRate));
    }

    // 4 · THE DEMO LOOP, WITH NO HUM IN IT, PASSES UNTOUCHED — the same loop through OFF and AUTO.
    {
        const juce::TemporaryFile temp (".wav");
        amp.demo.setLoop (writeLoop (temp.getFile()), sampleRate);
        amp.demo.setPlaying (true);
        stream.humming = false;

        Heard off, automatic;
        machine (Hum::off);
        stream.prepare();
        stream.play (9.0, off);

        machine (Hum::automatic);
        stream.prepare();
        stream.play (9.0, automatic);
        amp.demo.setPlaying (false);
        stream.humming = true;

        const long long d = firstDifference (off.out, automatic.out, 0, off.size());
        report ("the demo loop through AUTO: untouched", d < 0 && amp.humFamily.load() == 0 && juce::exactlyEqual (amp.humHz.load(), 0.0f),
                d < 0 ? juce::String::formatted ("9 s identical to OFF, family %d", amp.humFamily.load())
                      : juce::String::formatted ("differs at %.3f s", (double) d / sampleRate));
        const double loud = tapLevelDb (off, 0, off.size());
        report ("...and the loop really played", loud > -40.0, juce::String::formatted ("%.1f dBFS rms at the tap", loud));
    }

    // 5 · A SESSION NEITHER CARRIES THE MODE NOR MOVES IT.
    {
        machine (Hum::hz60);
        juce::MemoryBlock session;
        amp.getStateInformation (session);
        const auto xml = juce::AudioProcessor::getXmlFromBinary (session.getData(), (int) session.getSize());
        const bool carried = xml == nullptr || xml->toString().contains (orbitamp::prefs::humMains.toString());
        machine (Hum::hz50);
        amp.setStateInformation (session.getData(), (int) session.getSize());
        stream.play (0.1, heard);
        report ("a session neither carries the mode nor moves it", ! carried && orbitamp::prefs::hum(gateAmp()) == Hum::hz50 && inForce (amp) == Hum::hz50,
                juce::String ("in force after the session came back: ") + name (inForce (amp)));
    }

    // 6 · A CHOICE MADE THE WAY THE MENUS MAKE IT: on the machine's disk, and in every engine.
    {
        const auto file = orbitamp::prefs::store(gateAmp()).file();
        if (ownHome.empty() || ! file.isAChildOf (juce::File (juce::String (ownHome))))
        {
            std::printf ("%-52s skip  the settings are not in a home of the gate's own: %s\n",
                         "a menu's choice is kept and every engine follows it", file.getFullPathName().toRawUTF8());
        }
        else
        {
            // A second instance of the product in the same process, as a host holds two.
            const auto otherOwned = std::make_unique<AmpProcessor> (gateAmp());
            auto& other = *otherOwned;
            other.prepareToPlay (sampleRate, blockSize);
            juce::AudioBuffer<float> quiet (2, blockSize);
            juce::MidiBuffer midi;

            bool kept = true, followed = true;
            juce::String said;
            for (const auto h : orbitamp::prefs::humChoices)
            {
                orbitamp::prefs::setHum (gateAmp(), h);
                const auto onDisk = juce::JSON::parse (file).getProperty (orbitamp::prefs::humMains, {}).toString();
                kept = kept && onDisk == orbitamp::prefs::humWord (h);

                stream.play ((double) blockSize / sampleRate, heard);
                quiet.clear();
                other.processBlock (quiet, midi);
                followed = followed && inForce (amp) == h && inForce (other) == h;
                said << name (h) << " -> \"" << onDisk << "\" " << name (inForce (amp)) << "/" << name (inForce (other)) << "; ";
            }
            report ("a menu's choice is written to the machine", kept, said);
            report ("...and both instances' engines follow it", followed);
        }
    }

    // 7 · THE MENUS' LINES.
    {
        using orbitamp::prefs::humLabel;
        const auto autoFifty = juce::String::fromUTF8 ("Auto \xe2\x80\x94 50 Hz");
        const auto autoSixty = juce::String::fromUTF8 ("Auto \xe2\x80\x94 60 Hz");
        const auto listening = juce::String::fromUTF8 ("Auto \xe2\x80\x94 listening");
        report ("the menus' lines", humLabel (Hum::off, 50) == "Off" && humLabel (Hum::hz50, 60) == "50 Hz"
                                    && humLabel (Hum::hz60, 0) == "60 Hz" && humLabel (Hum::automatic, -1) == "Auto"
                                    && humLabel (Hum::automatic, 0) == listening
                                    && humLabel (Hum::automatic, 50) == autoFifty && humLabel (Hum::automatic, 60) == autoSixty,
                "Off / 50 Hz / 60 Hz / Auto, " + listening + ", " + autoSixty);

        machine (Hum::hz60);
        stream.play (0.1, heard);
        report ("...AUTO's state is not named under another mode", amp.humHeard() == -1,
                juce::String::formatted ("heard %d under 60 Hz", amp.humHeard()));
    }

    // 8 · HUM'S OWN LEARN.
    {
        using orbitamp::core::HumLearnTake;
        using orbitamp::HumLearner;
        using Link = orbitamp::core::HumLearnLink;
        constexpr long long tick = (long long) sampleRate / HumLearnTake::ticksPerSecond;   // 1600 samples

        // A take as HumLearner runs one: a tick's worth of audio — four blocks of 400, as a host might
        // send them — then the input's level the tick reads. Returns when it ended, in seconds.
        const auto learn = [&] (HumLearnTake& take)
        {
            take.begin (amp.humTuned.quietHz.load());
            int t = 0;
            for (; t < 400; ++t)
            {
                stream.playSamples (tick, heard, 400);
                if (take.hear (amp.gateKeyDb.load()))
                    break;
            }
            return (double) (t + 1) / HumLearnTake::ticksPerSecond;
        };

        stream.hiss = 3.16e-4;       // -70 dBFS of hiss under the hum
        set (amp, params::inTrim, 0.0f);

        // Cold: a fresh start in AUTO, nothing decided.
        machine (Hum::automatic);
        stream.prepare();
        HumLearnTake cold (amp.humLearn);
        const double coldEnds = learn (cold);
        const auto a = cold.answer();
        report ("HUM LEARN, cold: the hum learned and kept", a.verdict == Link::learned && a.kept && a.family == 50,
                juce::String::formatted ("verdict %d, kept %d, %d Hz, %.1f dB; settled at %.0f ms, ended at %.2f s",
                                         a.verdict, (int) a.kept, a.family, a.humDb, a.settledAtMs, coldEnds));
        {
            const size_t from = stream.play (0.5, heard);
            const double left = leftDb (heard, from, heard.size());
            report ("...and the stage runs what it learned", left <= -40.0, juce::String::formatted ("%.1f dB re the hum", left));
        }

        // Warm: the stage has its model — two and a half seconds.
        HumLearnTake warm (amp.humLearn);
        const double warmEnds = learn (warm);
        report ("HUM LEARN, warm: two and a half seconds, kept", warm.answer().kept && warmEnds < 2.8 && warmEnds >= 2.5,
                juce::String::formatted ("ended at %.2f s, settled at %.0f ms", warmEnds, warm.answer().settledAtMs));

        // Cancelled a third of the way in: the stage goes back to the model it had.
        {
            HumLearnTake cut (amp.humLearn);
            cut.begin (amp.humTuned.quietHz.load());
            for (int t = 0; t < 30; ++t) { stream.playSamples (tick, heard, 400); cut.hear (amp.gateKeyDb.load()); }
            const unsigned answersBefore = amp.humLearn.answers.load();
            cut.stop();
            stream.play (0.1, heard);
            const size_t from = stream.play (0.5, heard);
            const double left = leftDb (heard, from, heard.size());
            report ("a cancelled HUM LEARN: thrown away", amp.humLearn.learnMs.load() < 0.0f && amp.humLearn.answers.load() == answersBefore + 1
                                                            && amp.humLearn.verdict.load() != Link::learned && left <= -40.0,
                    juce::String::formatted ("verdict %d, %.1f dB re the hum after", amp.humLearn.verdict.load(), left));
        }

        // A string struck a second in: not a quiet, and nothing is kept.
        {
            const long long at = stream.now() + (long long) sampleRate;
            stream.extra = [at] (long long n)
            {
                if (n < at || n >= at + (long long) sampleRate / 2)
                    return 0.0;
                const double t = (double) (n - at) / sampleRate;
                return 0.2 * std::exp (-t / 0.15) * std::sin (2.0 * pi * 196.0 * t);
            };
            HumLearnTake struck (amp.humLearn);
            learn (struck);
            stream.extra = nullptr;
            report ("a string in the quiet: DISTURBED, nothing kept", struck.answer().verdict == Link::disturbed && ! struck.answer().kept,
                    juce::String::formatted ("verdict %d", struck.answer().verdict));
        }

        // Picked under OFF, it switches the hum on first, in the mode OFF bypasses.
        if (const auto file = orbitamp::prefs::store(gateAmp()).file(); ! ownHome.empty() && file.isAChildOf (juce::File (juce::String (ownHome))))
        {
            orbitamp::prefs::setHum (gateAmp(), Hum::hz50);
            orbitamp::prefs::setHum (gateAmp(), Hum::off);
            HumLearner learner (gateAmp(), amp.humLearn, amp.gateKeyDb, amp.humTuned.quietHz);
            learner.start();
            const Hum after = orbitamp::prefs::hum(gateAmp());
            learner.cancel();
            stream.play (0.1, heard);
            report ("HUM LEARN under OFF switches the hum on first", after == Hum::hz50, juce::String ("now ") + name (after));
            orbitamp::prefs::setHum (gateAmp(), Hum::automatic);      // AUTO again, and the mode OFF bypasses with it
        }

        // The words.
        const HumLearner::Take kept { HumLearner::Outcome::done, { Link::learned, true, 50, -61.6f, 1750.0f, -90.0f }, 0 };
        const HumLearner::Take other { HumLearner::Outcome::done, { Link::learned, false, 60, -40.0f, 1000.0f, -70.0f }, 50 };
        const HumLearner::Take none { HumLearner::Outcome::done, { Link::nothingHeard, false, 0, -200.0f, -1.0f, -200.0f }, 0 };
        const HumLearner::Take knock { HumLearner::Outcome::done, { Link::disturbed, false, 50, -40.0f, -1.0f, -200.0f }, 0 };
        report ("HUM LEARN's words", HumLearner::words (kept) == "HUM 50 Hz -62 dB" && HumLearner::words (other) == "HUM 60 Hz heard, set to 50 Hz"
                                      && HumLearner::words (none) == "no hum" && HumLearner::words (knock) == "DISTURBED"
                                      && HumLearner::words ({ HumLearner::Outcome::cancelled }) == "CANCELLED",
                HumLearner::words (kept) + " / " + HumLearner::words (other) + " / " + HumLearner::words (none) + " / " + HumLearner::words (knock));

        stream.hiss = Stream::hissRms;
    }

    // 9 · OFF AND BACK: A/B, NOT A RESTART.
    {
        // The correction the input path took off, sample by sample (the trim is at 0 dB here), and the
        // worst second difference of it over [from, to).
        const auto bend = [&heard] (size_t from, size_t to)
        {
            double worst = 0.0;
            for (size_t i = std::max<size_t> (from, 1); i + 1 < to; ++i)
            {
                const double c0 = (double) heard.in[i - 1] - heard.out[i - 1];
                const double c1 = (double) heard.in[i]     - heard.out[i];
                const double c2 = (double) heard.in[i + 1] - heard.out[i + 1];
                worst = std::max (worst, std::abs (c2 - 2.0 * c1 + c0));
            }
            return 20.0 * std::log10 (std::max (worst, 1.0e-12));
        };
        const size_t ms = (size_t) (sampleRate / 1000.0);

        set (amp, params::inTrim, 0.0f);
        machine (Hum::automatic);
        stream.prepare();
        stream.play (4.0, heard);
        const int decided = amp.humFamily.load();
        const float tracked = amp.humHz.load();

        machine (Hum::off);
        const size_t offAt = stream.play (2.0, heard);
        const double bendOff = bend (offAt - 10 * ms, offAt + 100 * ms);
        const long long dry = firstDifference (heard.out, heard.in, offAt + 50 * ms, heard.size());
        const int heldOff = amp.humFamily.load();
        const auto offLine = orbitamp::prefs::humLabel (Hum::automatic, amp.humHeard());

        machine (Hum::automatic);
        const size_t backAt = stream.play (1.0, heard);
        const double bendBack = bend (backAt - 10 * ms, backAt + 100 * ms);
        const double back = worstDb (heard, backAt + 50 * ms, heard.size());

        report ("OFF and back: the hum 40 dB down within 50 ms", decided == 50 && back <= -40.0,
                juce::String::formatted ("worst 20 ms from 50 ms on %.1f dB re the hum", back));
        report ("...AUTO's decision kept through OFF", heldOff == 50 && amp.humFamily.load() == 50
                                                         && std::abs (amp.humHz.load() - tracked) < 0.01f,
                juce::String::formatted ("%d Hz -> %d -> %d, tracking %.3f -> %.3f Hz", decided, heldOff,
                                         amp.humFamily.load(), tracked, amp.humHz.load()));
        report ("...under OFF, AUTO's line still names its decision", offLine == juce::String::fromUTF8 ("Auto \xe2\x80\x94 50 Hz"),
                "\"" + offLine + "\"");
        report ("...OFF is the input bit for bit once it has faded", dry < 0,
                dry < 0 ? juce::String ("identical from 50 ms on") : juce::String::formatted ("differs at %.3f s", (double) (dry - (long long) offAt) / sampleRate));
        report ("...neither switch bends the signal", bendOff <= -60.0 && bendBack <= -60.0,
                juce::String::formatted ("second difference %.1f dBFS off, %.1f dBFS back", bendOff, bendBack));
    }

    // 10 · THE QUICK A/B.
    {
        namespace prefs = orbitamp::prefs;
        const auto file = prefs::store(gateAmp()).file();
        if (ownHome.empty() || ! file.isAChildOf (juce::File (juce::String (ownHome))))
        {
            std::printf ("%-52s skip  the settings are not in a home of the gate's own\n", "H: OFF and back to the mode it bypasses");
        }
        else
        {
            prefs::setHum (gateAmp(), Hum::hz50);
            const Hum first = prefs::humFlip(gateAmp());
            prefs::setHum (gateAmp(), first);
            stream.play (0.1, heard);
            const Hum offInForce = inForce (amp);
            const Hum second = prefs::humFlip(gateAmp());
            prefs::setHum (gateAmp(), second);
            stream.play (0.1, heard);
            const auto lastOnDisk = juce::JSON::parse (file).getProperty (prefs::humLastOnKey, {}).toString();
            report ("H: OFF and back to the mode it bypasses", first == Hum::off && offInForce == Hum::off
                                                                 && second == Hum::hz50 && inForce (amp) == Hum::hz50 && lastOnDisk == "50",
                    juce::String ("50 Hz -> ") + name (first) + " -> " + name (second) + ", kept on the machine as \"" + lastOnDisk + "\"");
        }
        report ("...and the tag's words", prefs::humTag (Hum::off, 50) == "HUM OFF"
                                            && prefs::humTag (Hum::hz50, -1) == juce::String::fromUTF8 ("HUM ON \xe2\x80\x94 50 Hz")
                                            && prefs::humTag (Hum::automatic, -1) == juce::String::fromUTF8 ("HUM ON \xe2\x80\x94 Auto"),
                prefs::humTag (Hum::hz50, -1) + " / " + prefs::humTag (Hum::automatic, -1) + " / " + prefs::humTag (Hum::off, 50));
    }

    // 11 · AUTO, STILL LISTENING, SAYS SO.
    {
        machine (Hum::automatic);
        stream.humGain = 0.0;               // a rig with no hum: AUTO listens, and decides nothing
        stream.prepare();
        stream.play (2.0, heard);
        const auto line = orbitamp::prefs::humLabel (Hum::automatic, amp.humHeard());
        const auto tag  = orbitamp::prefs::humTag (Hum::automatic, amp.humHeard());
        report ("AUTO undecided: \"listening\", in the menus and the tag",
                amp.humHeard() == 0 && line == juce::String::fromUTF8 ("Auto \xe2\x80\x94 listening")
                    && tag == juce::String::fromUTF8 ("HUM ON \xe2\x80\x94 Auto, listening")
                    && orbitamp::prefs::humTag (Hum::automatic, 50) == juce::String::fromUTF8 ("HUM ON \xe2\x80\x94 Auto, 50 Hz"),
                "\"" + line + "\" / \"" + tag + "\"");
        stream.humGain = 1.0;
    }

    // 12 · A SWITCH, THEN THE MODE.
    {
        namespace prefs = orbitamp::prefs;
        const auto file = prefs::store(gateAmp()).file();
        if (ownHome.empty() || ! file.isAChildOf (juce::File (juce::String (ownHome))))
        {
            std::printf ("%-52s skip  the settings are not in a home of the gate's own\n", "HUM in a menu: a switch, then the mode");
        }
        else
        {
            const auto ticks = [] (int autoState)
            {
                juce::String t;
                for (const auto& line : prefs::humMenu (gateAmp(), autoState))
                    t << (line.ticked ? "[x] " : "[ ] ") << line.text << "; ";
                return t;
            };
            prefs::setHum (gateAmp(), Hum::hz60);
            const auto on = ticks (-1);
            prefs::setHum (gateAmp(), Hum::off);
            const auto off = ticks (-1);
            const auto offState = prefs::humState (gateAmp(), 0);
            prefs::humPick (gateAmp(), prefs::humMenu (gateAmp(), -1)[4]);       // AUTO, picked under OFF
            const Hum picked = prefs::hum(gateAmp());
            prefs::humPick (gateAmp(), prefs::humMenu (gateAmp(), -1)[1]);       // the switch: OFF...
            const Hum flipped = prefs::hum(gateAmp());
            prefs::humPick (gateAmp(), prefs::humMenu (gateAmp(), -1)[1]);       // ...and back, to the mode
            const Hum back = prefs::hum(gateAmp());
            report ("HUM in a menu: a switch, then the mode",
                    on == "[ ] Learn; [x] On; [ ] 50 Hz; [x] 60 Hz; [ ] Auto; " && off == "[ ] Learn; [ ] On; [ ] 50 Hz; [x] 60 Hz; [ ] Auto; "
                        && offState == "Off" && prefs::humState (gateAmp(), 0) == juce::String::fromUTF8 ("Auto \xe2\x80\x94 listening"),
                    "60 Hz: " + on + " OFF: " + off);
            report ("...a mode picked under OFF switches it on; the switch flips", picked == Hum::automatic
                                                                                  && flipped == Hum::off && back == Hum::automatic,
                    juce::String ("picked ") + name (picked) + ", flipped " + name (flipped) + ", back " + name (back));
        }
    }

    // 13 · THE CHANNELS THE CHAIN PLAYS, AND NO OTHER.
    {
        set (amp, params::inTrim, 0.0f);
        stream.humGain    = 0.03;       // a hum of -60 dBFS, as a quiet pickup's is...
        stream.secondHiss = 3.16e-3;    // ...beside an empty input hissing at -50 dBFS
        for (const auto mode : { orbitamp::params::StereoMode::stereoSpace, orbitamp::params::StereoMode::mono })
        {
            set (amp, params::stereoMode, (float) mode);
            machine (Hum::automatic);
            stream.prepare();
            double decidedAt = -1.0;
            for (int b = 1; b <= 8 && decidedAt < 0.0; ++b)
            {
                stream.play (0.5, heard);
                if (amp.humFamily.load() == 50)
                    decidedAt = 0.5 * b;
            }
            stream.play (2.0, heard);       // ...and two seconds to learn, as AUTO gets anywhere
            const size_t from = stream.play (1.0, heard);
            const double left = leftDb (heard, from, heard.size());
            report (mode == orbitamp::params::StereoMode::mono ? "MONO: a hissing second input, and AUTO decides"
                                                               : "STEREO SPACE: a hissing second input, and AUTO decides",
                    decidedAt > 0.0 && left <= -40.0,
                    juce::String::formatted ("decided by %.1f s; 2 s on, %.1f dB re the hum", decidedAt, left));
        }
        stream.secondHiss = -1.0;
        stream.humGain    = 1.0;
        set (amp, params::stereoMode, (float) orbitamp::params::StereoMode::stereo);
    }

    // 14 · THE TUNING.
    {
        namespace tune = orbitamp::prefs::humtune;
        const felitronics::dehum::HumRemoverParams own;
        machine (Hum::automatic);
        set (amp, params::inTrim, 0.0f);
        set (amp, params::stereoMode, (float) orbitamp::params::StereoMode::stereo);

        // The defaults: the stage's own widths, and up to 2 kHz — forty harmonics at 50 Hz.
        amp.prepareToPlay (sampleRate, blockSize);
        stream.play (3.0, heard);
        report ("the tuning's defaults: own widths, up to 2 kHz",
                juce::exactlyEqual (amp.humTuned.playHz.load(), (float) own.bandwidthHz)
                    && juce::exactlyEqual (amp.humTuned.quietHz.load(), (float) own.learnBandwidthHz)
                    && amp.humTuned.harmonics.load() == AmpProcessor::HumStage::kMaxHarmonics && amp.humTuned.active.load() == 40,
                juce::String::formatted ("%.2f / %.0f Hz, %d harmonics under %.0f Hz, %d running", amp.humTuned.playHz.load(),
                                         amp.humTuned.quietHz.load(), amp.humTuned.harmonics.load(), amp.humTuned.maxHz.load(),
                                         amp.humTuned.active.load()));

        const auto file = orbitamp::prefs::store(gateAmp()).file();
        const bool ownSettings = ! ownHome.empty() && file.isAChildOf (juce::File (juce::String (ownHome)));
        if (! ownSettings)
            std::printf ("%-52s skip  the settings are not in a home of the gate's own\n", "the tuning, chosen");

        // 1.2 kHz, against the stage as it ran before: its own params, its own 64 harmonics.
        if (ownSettings)
        {
        tune::setUpTo (gateAmp(), tune::upToOwn);
        amp.prepareToPlay (sampleRate, blockSize);
        auto before = std::make_unique<felitronics::dehum::HumRemover<64>>();
        before->setParams (own);
        const bool ready = before->prepare (sampleRate, blockSize, 2);
        juce::AudioBuffer<float> buf (2, blockSize), was (2, blockSize);
        juce::MidiBuffer midi;
        auto tap = std::make_unique<std::array<float, orbitamp::core::TunerTap::size>>();
        long long differs = -1, n = 0;
        std::uint32_t seed = 4242u;
        for (int b = 0; b < (int) (5.0 * sampleRate) / blockSize; ++b)
        {
            for (int i = 0; i < blockSize; ++i, ++n)
            {
                double h0 = 0.0, h1 = 0.0;
                for (int k = 1; k <= 10; ++k)
                {
                    const double ph = 2.0 * pi * k * 50.03 * (double) n / sampleRate + 0.7 * k;
                    h0 += 0.03 / k * std::sin (ph);
                    h1 += 0.024 / k * std::sin (ph + 0.3);
                }
                seed = seed * 1664525u + 1013904223u;
                const double hiss = 1.0e-5 * ((double) (seed >> 8) / 8388608.0 - 1.0);
                buf.setSample (0, i, (float) (h0 + hiss));
                buf.setSample (1, i, (float) (h1 + hiss));
            }
            was.makeCopyOf (buf, true);
            amp.processBlock (buf, midi);
            (void) before->process (was.getArrayOfWritePointers(), 2, blockSize);
            amp.tunerTap.read (*tap);
            for (int i = 0; i < blockSize && differs < 0; ++i)
                if (std::memcmp (&(*tap)[(size_t) (orbitamp::core::TunerTap::size - blockSize + i)], was.getReadPointer (0) + i, sizeof (float)) != 0)
                    differs = n - blockSize + i;
        }
        report ("up to 1.2 kHz is the stage as it ran",
                ready && differs < 0 && juce::exactlyEqual (amp.humTuned.playHz.load(), (float) own.bandwidthHz)
                    && juce::exactlyEqual (amp.humTuned.quietHz.load(), (float) own.learnBandwidthHz)
                    && amp.humTuned.harmonics.load() == own.harmonics && juce::exactlyEqual (amp.humTuned.maxHz.load(), (float) own.maxHz)
                    && amp.humTuned.active.load() == 24,
                differs < 0 ? juce::String::formatted ("5 s bit for bit; %.2f / %.0f Hz, %d harmonics under %.0f Hz, %d running",
                                                       amp.humTuned.playHz.load(), amp.humTuned.quietHz.load(), amp.humTuned.harmonics.load(),
                                                       amp.humTuned.maxHz.load(), amp.humTuned.active.load())
                            : juce::String::formatted ("differs at sample %lld", differs));
        }

        // Every choice, read back off what the stage was handed.
        if (ownSettings)
        {
            bool widths = true, reach = true;
            juce::String said;
            for (const float v : tune::playWidths)
            {
                tune::setPlay (gateAmp(), v);
                stream.play (2.0 * blockSize / sampleRate, heard);
                widths = widths && juce::exactlyEqual (amp.humTuned.playHz.load(), v);
            }
            for (const float v : tune::quietWidths)
            {
                tune::setQuiet (gateAmp(), v);
                stream.play (2.0 * blockSize / sampleRate, heard);
                widths = widths && juce::exactlyEqual (amp.humTuned.quietHz.load(), v);
            }
            const int harmonicsAt50[] = { 24, 40, 64, 96, 128 };
            for (size_t i = 0; i < std::size (tune::upTos); ++i)
            {
                tune::setUpTo (gateAmp(), tune::upTos[i]);
                stream.play (4.0 * blockSize / sampleRate, heard);
                reach = reach && amp.humTuned.active.load() == harmonicsAt50[i];
                said << tune::kHz (tune::upTos[i]) << " -> " << amp.humTuned.active.load() << "; ";
            }
            tune::setPlay (gateAmp(), tune::playDefault);
            tune::setQuiet (gateAmp(), tune::quietDefault);
            tune::setUpTo (gateAmp(), tune::upToDefault);
            stream.play (2.0 * blockSize / sampleRate, heard);
            report ("every tuning choice reaches the stage", widths && reach && amp.humTuned.harmonics.load() == AmpProcessor::HumStage::kMaxHarmonics,
                    "harmonics running at 50 Hz: " + said);
            report ("...and the tuning's words", tune::hz (0.25f) == "0.25 Hz" && tune::hz (0.5f) == "0.5 Hz" && tune::hz (3.0f) == "3 Hz"
                                                   && tune::kHz (1200.0f) == "1.2 kHz" && tune::kHz (2000.0f) == "2 kHz",
                    tune::hz (0.25f) + " / " + tune::hz (3.0f) + " / " + tune::kHz (1200.0f) + " / " + tune::kHz (6400.0f));
        }
    }

    machine (orbitamp::prefs::humFromWord (orbitamp::prefs::store(gateAmp()).get (orbitamp::prefs::humMains, "auto").toString()));

    if (failures == 0)
        std::printf ("\nall hum checks passed\n");
    else
        std::printf ("\n%d hum check(s) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
