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
//   8. One LEARN teaches both. A COLD learn — AUTO undecided, the stage starting from nothing and
//      letting the hum through while it learns — on a hum over a hiss sets the gate by the hiss, not by
//      the hum, and keeps a second of floor after the stage settles; a warm one keeps its three
//      seconds; a cancelled take throws the hum's learn away and the stage goes back to its model; a
//      string struck in the quiet sets nothing; with OFF the take is the gate's alone. And the hum's
//      line under the verdict says what was learned.
//   9. OFF IS A BYPASS THAT KEEPS WHAT WAS LEARNED. AUTO locked, OFF for two seconds, AUTO again: the
//      hum is 40 dB down within 50 ms of the way back, AUTO's decision never moved, and neither switch
//      bends the signal — the correction's second difference stays under -60 dBFS both ways.
//  10. The faces' quick A/B (the H key): OFF from a mode, and from OFF back to that mode — kept on the
//      machine beside the mode — with the words the face's tag says for a moment.
//  11. AUTO, undecided, says it is listening — in the menus' line and in the tag.
//  12. Every menu's HUM is a switch and a mode: "On" ticked unless OFF; the tick on the mode in force,
//      or under OFF on the mode OFF bypasses; a mode picked under OFF switches the hum on in it.
//
// Checks 1-5 set the mode through the cached atomic the engine reads (prefs::humCache) and leave the
// settings file alone. Checks 6 and 7 write the file, so the gate first moves HOME into a folder of
// its own; where the store does not land there (a platform that does not take its settings from HOME),
// they say so and skip rather than rewrite the choice of the machine they run on.

#include <juce_events/juce_events.h>

#include "PluginProcessor.h"
#include "ui/GateLearner.h"
#include "ui/HumMenu.h"

// OrbitAmp's set (OrbitAmpSet.cpp) is NOT laid out from here: the processor would write it into the
// machine's real Factory folder.
void orbitamp::layOutFactorySet() {}

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

    void machine (Hum h) { orbitamp::prefs::humCache().store ((int) h, std::memory_order_relaxed); }

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
        std::uint32_t seed = 12345u;
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

int main()
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
    const auto ampOwned = std::make_unique<AmpProcessor>();
    auto& amp = *ampOwned;

    // The input path the tap hears: the INPUT row working (its trim applies) and the tuner listening.
    for (const auto row : { params::rowIn, params::rowTuner })
    {
        set (amp, params::chainLinks[row].onParam, 1.0f);
        set (amp, params::chainLinks[row].presentParam, 1.0f);
    }
    set (amp, params::inTrim, 0.0f);

    Stream stream (amp);
    stream.prepare();

    // 0 · AUTO BY DEFAULT — on a machine that has never said otherwise.
    {
        static_assert (orbitamp::prefs::humDefault == Hum::automatic);
        if (orbitamp::prefs::store().get (orbitamp::prefs::humMains).isVoid())
            report ("a machine that never chose runs AUTO", inForce (amp) == Hum::automatic,
                    juce::String ("in force: ") + name (inForce (amp)));
        else
            std::printf ("%-52s skip  this machine has chosen: %s\n", "a machine that never chose runs AUTO",
                         name (orbitamp::prefs::hum()));
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
        report ("a session neither carries the mode nor moves it", ! carried && orbitamp::prefs::hum() == Hum::hz50 && inForce (amp) == Hum::hz50,
                juce::String ("in force after the session came back: ") + name (inForce (amp)));
    }

    // 6 · A CHOICE MADE THE WAY THE MENUS MAKE IT: on the machine's disk, and in every engine.
    {
        const auto file = orbitamp::prefs::store().file();
        if (ownHome.empty() || ! file.isAChildOf (juce::File (juce::String (ownHome))))
        {
            std::printf ("%-52s skip  the settings are not in a home of the gate's own: %s\n",
                         "a menu's choice is kept and every engine follows it", file.getFullPathName().toRawUTF8());
        }
        else
        {
            // A second instance of the product in the same process, as a host holds two.
            const auto otherOwned = std::make_unique<AmpProcessor>();
            auto& other = *otherOwned;
            other.prepareToPlay (sampleRate, blockSize);
            juce::AudioBuffer<float> quiet (2, blockSize);
            juce::MidiBuffer midi;

            bool kept = true, followed = true;
            juce::String said;
            for (const auto h : orbitamp::prefs::humChoices)
            {
                orbitamp::prefs::setHum (h);
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

    // 8 · ONE LEARN TEACHES THE HUM AND THE GATE.
    {
        using orbitamp::core::GateLearn;
        using orbitamp::GateLearner;
        using Link = orbitamp::core::HumLearnLink;
        constexpr long long tick = (long long) sampleRate / GateLearn::ticksPerSecond;   // 1600 samples

        // A take as GateLearner runs one: a tick's worth of audio — four blocks of 400, as a host might
        // send them — then the key level the tick reads.
        const auto learn = [&] (GateLearn& take)
        {
            take.begin();
            for (int t = 0; t < 400; ++t)
            {
                stream.playSamples (tick, heard, 400);
                if (take.hear (amp.gateKeyDb.load()))
                    break;
            }
            return take.resultDb();
        };

        stream.hiss = 3.16e-4;       // -70 dBFS of hiss: what a gate learned in quiet should sit above
        set (amp, params::inTrim, 0.0f);

        // Where the gate belongs — the hiss alone — and where the hum would put it.
        machine (Hum::off);
        stream.humGain = 0.0;
        stream.prepare();
        GateLearn alone (params::gateHysteresisDb, &amp.humLearn);
        const float byHiss = learn (alone);
        stream.humGain = 1.0;
        GateLearn raw (params::gateHysteresisDb, &amp.humLearn);
        const float byHum = learn (raw);

        // The cold learn: a fresh start in AUTO, nothing decided.
        machine (Hum::automatic);
        stream.prepare();
        GateLearn cold (params::gateHysteresisDb, &amp.humLearn);
        const float th = learn (cold);
        const auto a = cold.humAnswer();
        const double floorAfter = (double) (cold.ticksInTake() - GateLearn::edgeTicks) / GateLearn::ticksPerSecond
                                - a.settledAtMs / 1000.0;
        report ("cold LEARN: the gate sits by the hiss, not the hum", std::abs (th - byHiss) <= 6.0f && th < byHum - 25.0f,
                juce::String::formatted ("%.1f dB (the hiss alone %.1f, the hum %.1f; the stage's own floor %.1f dBFS)",
                                         th, byHiss, byHum, a.floorAfterDb));
        report ("...the hum learned and kept", cold.learnsHum() && a.verdict == Link::learned && a.kept && a.family == 50,
                juce::String::formatted ("verdict %d, kept %d, %d Hz, %.1f dB, settled at %.0f ms",
                                         a.verdict, (int) a.kept, a.family, a.humDb, a.settledAtMs));
        report ("...a second of floor after it settled", cold.ticksInTake() == GateLearn::coldTicks && floorAfter >= 0.95,
                juce::String::formatted ("%d ticks, %.2f s trusted after the settle", cold.ticksInTake(), floorAfter));
        {
            const size_t from = stream.play (0.5, heard);
            const double left = leftDb (heard, from, heard.size());
            report ("...and the stage runs what it learned", left <= -40.0, juce::String::formatted ("%.1f dB re the hum", left));
        }

        GateLearner::Take shown { GateLearner::Outcome::set, th, true, a, 0 };
        report ("the hum's line under the verdict", GateLearner::humLine (shown) == "HUM 50 Hz " + juce::String (juce::roundToInt (a.humDb)) + " dB",
                "\"" + GateLearner::humLine (shown) + "\"");
        GateLearner::Take other { GateLearner::Outcome::set, th, true, { Link::learned, false, 60, -40.0f, 1000.0f, -70.0f }, 50 };
        GateLearner::Take none { GateLearner::Outcome::set, th, true, { Link::nothingHeard, false, 0, -200.0f, -1.0f, -200.0f }, 0 };
        GateLearner::Take gateOnly { GateLearner::Outcome::set, th };
        report ("...the other mains heard; no hum; no hum learn",
                GateLearner::humLine (other) == "HUM 60 Hz heard, set to 50 Hz" && GateLearner::humLine (none) == "no hum"
                    && GateLearner::humLine (gateOnly).isEmpty(),
                "\"" + GateLearner::humLine (other) + "\" / \"" + GateLearner::humLine (none) + "\"");

        // Warm: the stage has its model, so the take keeps its three seconds.
        GateLearn warm (params::gateHysteresisDb, &amp.humLearn);
        const float thWarm = learn (warm);
        report ("warm LEARN: three seconds, the gate by the hiss", warm.ticksInTake() == GateLearn::totalTicks && warm.humAnswer().kept
                                                                     && std::abs (thWarm - byHiss) <= 3.0f,
                juce::String::formatted ("%d ticks, %.1f dB, settled at %.0f ms", warm.ticksInTake(), thWarm, warm.humAnswer().settledAtMs));

        // Cancelled a third of the way in: the stage goes back to the model it had.
        {
            GateLearn cut (params::gateHysteresisDb, &amp.humLearn);
            cut.begin();
            for (int t = 0; t < 30; ++t) { stream.playSamples (tick, heard, 400); cut.hear (amp.gateKeyDb.load()); }
            const unsigned answersBefore = amp.humLearn.answers.load();
            cut.stop();
            stream.play (0.1, heard);
            const size_t from = stream.play (0.5, heard);
            const double left = leftDb (heard, from, heard.size());
            report ("a cancelled take: the hum's learn thrown away", amp.humLearn.learnMs.load() < 0.0f
                                                                      && amp.humLearn.answers.load() == answersBefore + 1
                                                                      && amp.humLearn.verdict.load() != Link::learned && left <= -40.0,
                    juce::String::formatted ("verdict %d, %.1f dB re the hum after", amp.humLearn.verdict.load(), left));
        }

        // A string struck two seconds in: not a quiet, and nothing is set.
        {
            const long long at = stream.now() + 2 * (long long) sampleRate;
            stream.extra = [at] (long long n)
            {
                if (n < at || n >= at + (long long) sampleRate / 2)
                    return 0.0;
                const double t = (double) (n - at) / sampleRate;
                return 0.2 * std::exp (-t / 0.15) * std::sin (2.0 * pi * 196.0 * t);
            };
            GateLearn struck (params::gateHysteresisDb, &amp.humLearn);
            const float thStruck = learn (struck);
            stream.extra = nullptr;
            report ("a string in the quiet: DISTURBED, nothing set", struck.disturbed() && thStruck <= GateLearn::nothing,
                    juce::String::formatted ("verdict %d", struck.humAnswer().verdict));
        }

        // OFF: the gate's take alone, as it always was.
        {
            machine (Hum::off);
            stream.play (0.1, heard);
            GateLearn plain (params::gateHysteresisDb, &amp.humLearn);
            plain.begin();
            report ("OFF: the take is the gate's alone", ! plain.learnsHum() && plain.ticksInTake() == GateLearn::totalTicks
                                                          && amp.humLearn.ask.load() == Link::none);
            plain.stop();
        }

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
        const auto file = prefs::store().file();
        if (ownHome.empty() || ! file.isAChildOf (juce::File (juce::String (ownHome))))
        {
            std::printf ("%-52s skip  the settings are not in a home of the gate's own\n", "H: OFF and back to the mode it bypasses");
        }
        else
        {
            prefs::setHum (Hum::hz50);
            const Hum first = prefs::humFlip();
            prefs::setHum (first);
            stream.play (0.1, heard);
            const Hum offInForce = inForce (amp);
            const Hum second = prefs::humFlip();
            prefs::setHum (second);
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
        const auto file = prefs::store().file();
        if (ownHome.empty() || ! file.isAChildOf (juce::File (juce::String (ownHome))))
        {
            std::printf ("%-52s skip  the settings are not in a home of the gate's own\n", "HUM in a menu: a switch, then the mode");
        }
        else
        {
            const auto ticks = [] (int autoState)
            {
                juce::String t;
                for (const auto& line : prefs::humMenu (autoState))
                    t << (line.ticked ? "[x] " : "[ ] ") << line.text << "; ";
                return t;
            };
            prefs::setHum (Hum::hz60);
            const auto on = ticks (-1);
            prefs::setHum (Hum::off);
            const auto off = ticks (-1);
            const auto offState = prefs::humState (0);
            prefs::humPick (prefs::humMenu (-1)[3]);       // AUTO, picked under OFF
            const Hum picked = prefs::hum();
            prefs::humPick (prefs::humMenu (-1)[0]);       // the switch: OFF...
            const Hum flipped = prefs::hum();
            prefs::humPick (prefs::humMenu (-1)[0]);       // ...and back, to the mode
            const Hum back = prefs::hum();
            report ("HUM in a menu: a switch, then the mode",
                    on == "[x] On; [ ] 50 Hz; [x] 60 Hz; [ ] Auto; " && off == "[ ] On; [ ] 50 Hz; [x] 60 Hz; [ ] Auto; "
                        && offState == "Off" && prefs::humState (0) == juce::String::fromUTF8 ("Auto \xe2\x80\x94 listening"),
                    "60 Hz: " + on + " OFF: " + off);
            report ("...a mode picked under OFF switches it on; the switch flips", picked == Hum::automatic
                                                                                  && flipped == Hum::off && back == Hum::automatic,
                    juce::String ("picked ") + name (picked) + ", flipped " + name (flipped) + ", back " + name (back));
        }
    }

    machine (orbitamp::prefs::humFromWord (orbitamp::prefs::store().get (orbitamp::prefs::humMains, "auto").toString()));

    if (failures == 0)
        std::printf ("\nall hum checks passed\n");
    else
        std::printf ("\n%d hum check(s) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
