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
//
// Checks 1-5 set the mode through the cached atomic the engine reads (prefs::humCache) and leave the
// settings file alone. Checks 6 and 7 write the file, so the gate first moves HOME into a folder of
// its own; where the store does not land there (a platform that does not take its settings from HOME),
// they say so and skip rather than rewrite the choice of the machine they run on.

#include <juce_events/juce_events.h>

#include "PluginProcessor.h"

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

        bool humming = true;    // false: the input carries nothing (the demo loop replaces it)

        /** A restart, the way a host makes one: the trim's ramp is seeded from its parameter again. */
        void prepare()
        {
            amp.prepareToPlay (sampleRate, blockSize);
            lastGain = trimTarget();
        }

        /** Plays `seconds` more and returns where they start in `heard`. */
        size_t play (double seconds, Heard& heard)
        {
            const size_t start = heard.size();
            const int blocks = (int) std::ceil (seconds * sampleRate / blockSize);
            juce::AudioBuffer<float> buf (2, blockSize);
            juce::MidiBuffer midi;

            for (int b = 0; b < blocks; ++b)
            {
                const float target = trimTarget();
                const float step   = (target - lastGain) / (float) blockSize;
                float g = lastGain;

                for (int i = 0; i < blockSize; ++i, ++n)
                {
                    double hum[2] = { 0.0, 0.0 };
                    if (humming)
                        for (int k = 1; k <= harmonics; ++k)
                        {
                            const double ph = 2.0 * pi * k * mainsHz * (double) n / sampleRate + 0.7 * k;
                            hum[0] += fundamental / k * std::sin (ph);
                            hum[1] += 0.8 * fundamental / k * std::sin (ph + 0.3);
                        }

                    // A uniform hiss from a fixed seed: the same every run, √3 over its RMS at the peak.
                    seed = seed * 1664525u + 1013904223u;
                    const double hiss = humming ? hissRms * std::sqrt (3.0) * ((double) (seed >> 8) / 8388608.0 - 1.0) : 0.0;

                    buf.setSample (0, i, (float) (hum[0] + hiss));
                    buf.setSample (1, i, (float) (hum[1] + hiss));
                    heard.in.push_back (buf.getSample (0, i));
                    heard.hum.push_back ((float) hum[0]);
                    heard.hiss.push_back ((float) hiss);
                    heard.gain.push_back (juce::exactlyEqual (target, lastGain) ? (double) lastGain : (double) g);
                    g += step;
                }
                lastGain = target;

                amp.processBlock (buf, midi);
                amp.tunerTap.read (*tap);
                heard.out.insert (heard.out.end(), tap->end() - blockSize, tap->end());
            }
            return start;
        }

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
        report ("the menus' lines", humLabel (Hum::off, 50) == "Off" && humLabel (Hum::hz50, 60) == "50 Hz"
                                    && humLabel (Hum::hz60, 0) == "60 Hz" && humLabel (Hum::automatic, 0) == "Auto"
                                    && humLabel (Hum::automatic, 50) == autoFifty && humLabel (Hum::automatic, 60) == autoSixty,
                "Off / 50 Hz / 60 Hz / Auto, and " + autoSixty);

        machine (Hum::hz60);
        stream.play (0.1, heard);
        report ("...AUTO's decision is not named under another mode", amp.humHeard() == 0,
                juce::String::formatted ("heard %d under 60 Hz", amp.humHeard()));
    }

    machine (orbitamp::prefs::humFromWord (orbitamp::prefs::store().get (orbitamp::prefs::humMains, "auto").toString()));

    if (failures == 0)
        std::printf ("\nall hum checks passed\n");
    else
        std::printf ("\n%d hum check(s) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
