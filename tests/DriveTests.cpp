// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Gate for OrbitAmp's DRIVE (params::driveOn and its block, core/DrivePedal.h): a modest pedal before the AMP.
//
//   - NOT OFFERED, OrbitAmp plays what it played before the block existed, bit for bit on the compiler that recorded
//     it (tests/golden/orbitamp-before-drive.f32, written by the tree before the block), and so does a session saved by
//     that tree (tests/golden/orbitamp-before-drive.state) — every parameter it had comes back as it was;
//   - no other amp has a parameter of the block's;
//   - offered (its checklist, or SETUP — the same parameter), it stands in the strip before the AMP, its arrow its
//     STANDBY/ON, and it plays; out, it is not run at all;
//   - a session keeps its type, GAIN and the type's EQ.
//
// ORBITAMP_DRIVE_RECORD=<folder> writes the two golden files instead of checking them — run by the tree BEFORE the
// block, never by the tree under test. ORBITAMP_DRIVE_LISTEN=<riff.wav> with ORBITAMP_DRIVE_LISTEN_OUT=<folder> renders
// the riff through a fresh OrbitAmp — the block off, then each type at GAIN noon — for ears, and checks nothing; levels
// it matches, it matches in integrated loudness (BS.1770, tests/Loudness.h), never in RMS. ORBITAMP_DRIVE_COMPARE=<his
// capture's output on the reference> prints ours against it before and after the amp, 1/3 octave by 1/3 octave.
// main() returns 0 when every check held.

#include "GateMain.h"
#include "Loudness.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_events/juce_events.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ui/DriveBlock.h"
#include "ui/SettingsList.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#ifndef ORBITAMP_DRIVE_GOLDEN
 #error "the build says where the golden files are (CMakeLists.txt: gate_in_binary(drive ...))"
#endif

using namespace orbitamp;

namespace
{
    int failed = 0;

    void check (bool ok, const juce::String& what)
    {
        std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
        if (! ok)
            ++failed;
    }

    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;
    constexpr int goldenBlocks = 32;

    /** A product as it plays, without its first start: nothing gathered from the player's folders. */
    Product quiet (const Product& p)
    {
        auto q = p;
        q.firstStart = nullptr;
        return q;
    }

    const Product& orbitAmp() { static const Product p = quiet (products::orbitAmp()); return p; }

    std::unique_ptr<AmpProcessor> make (const Product& p)
    {
        auto amp = std::make_unique<AmpProcessor> (p);
        amp->inlineLoads = true;
        amp->prepareToPlay (sampleRate, blockSize);
        amp->pumpDeviceWork();
        return amp;
    }

    /** A guitar's phrase, the same every run: a plucked chord every quarter of a second, each string's partials dying
        away, peaking near -12 dBFS. */
    float phrase (long long n)
    {
        const double t = (double) n / sampleRate;
        const double pluck = std::fmod (t, 0.25);
        const int chord = (int) (t / 0.25) % 4;
        static constexpr double roots[4][3] = { { 82.41, 123.47, 164.81 }, { 110.0, 164.81, 220.0 },
                                                { 98.0, 146.83, 196.0 }, { 73.42, 110.0, 146.83 } };
        double s = 0.0;
        for (const double f : roots[chord])
            for (int h = 1; h <= 6; ++h)
                s += std::exp (-pluck * (4.0 + 3.0 * h)) / (double) h
                     * std::sin (juce::MathConstants<double>::twoPi * f * h * pluck);
        return (float) (0.08 * s);
    }

    /** What leaves, channel 0, for `blocks` blocks of the phrase — pumped every block, as the timer would. */
    std::vector<float> play (AmpProcessor& amp, int blocks, long long& at)
    {
        std::vector<float> out;
        juce::AudioBuffer<float> buf (2, blockSize);
        juce::MidiBuffer midi;
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < blockSize; ++i, ++at)
            {
                buf.setSample (0, i, phrase (at));
                buf.setSample (1, i, phrase (at));
            }
            amp.processBlock (buf, midi);
            amp.pumpDeviceWork();
            out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + blockSize);
        }
        return out;
    }

    std::vector<float> play (AmpProcessor& amp, int blocks)
    {
        long long at = 0;
        return play (amp, blocks, at);
    }

    float param (AmpProcessor& amp, const juce::String& id)
    {
        auto* p = amp.apvts.getParameter (id);
        return p->convertFrom0to1 (p->getValue());
    }

    void setParam (AmpProcessor& amp, const juce::String& id, float value)
    {
        auto* p = amp.apvts.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        amp.pumpDeviceWork();
    }

    const char* const driveIds[] = { params::drivePresent, params::driveOn, params::driveGain, params::driveTone, params::driveVolume };

    void walk (juce::Component& c, const std::function<void (juce::Component&)>& f)
    {
        f (c);
        for (auto* child : c.getChildren())
            walk (*child, f);
    }

    bool shownIn (juce::Component& c, juce::Component& top)
    {
        for (auto* p = &c; p != nullptr && p != &top; p = p->getParentComponent())
            if (! p->isVisible())
                return false;
        return true;
    }

    juce::File golden (const char* ext) { return juce::File (ORBITAMP_DRIVE_GOLDEN).withFileExtension (ext); }

    /** What the tree before the block played, and whether this binary is the compiler that recorded it. */
    struct Before
    {
        std::vector<float> played;
        juce::MemoryBlock state;
        bool recorder = false;
    };

    Before before()
    {
        Before b;
        juce::MemoryBlock bytes;
        if (golden ("f32").loadFileAsData (bytes))
        {
            juce::MemoryInputStream in (bytes, false);
            while (! in.isExhausted())
                b.played.push_back (in.readFloat());
        }
        (void) golden ("state").loadFileAsData (b.state);
       #if defined (__APPLE__) && defined (__aarch64__)
        b.recorder = golden ("compiler").loadFileAsString().trim() == juce::String (__VERSION__);
       #endif
        return b;
    }

    /** Bit for bit on the recording compiler; within 1e-5 (-100 dB) elsewhere, whose libm and vector code may move a
        last bit — NaN when the lengths differ or a sample is not finite. */
    float apart (const std::vector<float>& a, const std::vector<float>& b)
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

    bool matches (const Before& was, const std::vector<float>& now, const juce::String& what)
    {
        const float d = apart (was.played, now);
        const float allowed = was.recorder ? 0.0f : 1.0e-5f;
        check (std::isfinite (d) && d <= allowed,
               what + " (" + (was.recorder ? "bit for bit, the recording compiler" : "within 1e-5, another compiler")
                   + ": " + juce::String (d, 9) + " apart)");
        return std::isfinite (d) && d <= allowed;
    }

    /** The tree before the block writes what it plays and what it saves. */
    int record (const juce::File& folder)
    {
        auto amp = make (orbitAmp());
        const auto played = play (*amp, goldenBlocks);
        juce::MemoryOutputStream out;
        for (const float s : played)
            out.writeFloat (s);
        juce::MemoryBlock state;
        amp->getStateInformation (state);

        (void) folder.createDirectory();
        const auto name = juce::File (ORBITAMP_DRIVE_GOLDEN).getFileName();
        const bool ok = folder.getChildFile (name).withFileExtension ("f32").replaceWithData (out.getData(), out.getDataSize())
                     && folder.getChildFile (name).withFileExtension ("state").replaceWithData (state.getData(), state.getSize())
                     && folder.getChildFile (name).withFileExtension ("compiler").replaceWithText (juce::String (__VERSION__) + "\n");
        float peak = 0.0f;
        for (const float s : played)
            peak = std::max (peak, std::abs (s));
        std::printf ("%s %d samples (peak %.4f) and %d bytes of state into %s\n", ok ? "recorded" : "COULD NOT RECORD",
                     (int) played.size(), (double) peak, (int) state.getSize(), folder.getFullPathName().toRawUTF8());
        return ok && peak > 1.0e-3f ? 0 : 1;
    }
}

namespace
{
    /** For ears: the riff through a fresh OrbitAmp at its own rate — a second of silence first, for the AMP's model to
        land and warm, cut off again — the block off, then each type at GAIN noon, its EQ as it comes. */
    int listen (const juce::File& riffFile, const juce::File& folder)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (riffFile));
        if (reader == nullptr)
        {
            std::printf ("COULD NOT READ %s\n", riffFile.getFullPathName().toRawUTF8());
            return 1;
        }
        const double rate = reader->sampleRate;
        const int len = (int) reader->lengthInSamples;
        juce::AudioBuffer<float> riff (1, len);
        reader->read (&riff, 0, len, 0, true, false);
        (void) folder.createDirectory();

        // One take: a fresh OrbitAmp, the block off (type < 0) or the type at GAIN noon with the clip law `clip`.
        const auto renderFrom = [&] (const juce::AudioBuffer<float>& src, int type, int clip, float gain = 5.0f, float tone = 5.0f)
        {
            const int srcLen = src.getNumSamples();
            auto amp = std::make_unique<AmpProcessor> (orbitAmp());
            amp->inlineLoads = true;
            if (type >= 0)
            {
                setParam (*amp, params::drivePresent, 1.0f);
                setParam (*amp, params::driveGain, gain);
                setParam (*amp, params::driveTone, tone);
            }
            // ORBITAMP_DRIVE_LISTEN_NOGATE=1: the GATE off — it keys off the raw guitar ahead of the chain, so a pedal
            // played INTO the amp from a file and the DRIVE in front of it are gated on two different signals.
            if (juce::SystemStats::getEnvironmentVariable ("ORBITAMP_DRIVE_LISTEN_NOGATE", {}) == "1")
                setParam (*amp, params::gateOn, 0.0f);
            amp->prepareToPlay (rate, blockSize);
            amp->pumpDeviceWork();
            const int lead = (int) rate, tail = 2 * (int) rate, total = lead + srcLen + tail;
            float peakIn = 0.0f, peakOut = 0.0f;
            juce::AudioBuffer<float> out (2, srcLen + tail), buf (2, blockSize);
            juce::MidiBuffer midi;
            for (int at = 0; at < total; at += blockSize)
            {
                const int n = std::min (blockSize, total - at);
                buf.setSize (2, n, false, false, true);
                for (int i = 0; i < n; ++i)
                {
                    const int k = at + i - lead;
                    const float x = k >= 0 && k < srcLen ? src.getSample (0, k) : 0.0f;
                    buf.setSample (0, i, x);
                    buf.setSample (1, i, x);
                }
                amp->processBlock (buf, midi);
                amp->pumpDeviceWork();
                peakIn = std::max (peakIn, amp->driveInPeak.exchange (0.0f));
                peakOut = std::max (peakOut, amp->driveOutPeak.exchange (0.0f));
                for (int i = 0; i < n; ++i)
                    if (const int k = at + i - lead; k >= 0)
                        for (int ch = 0; ch < 2; ++ch)
                            out.setSample (ch, k, buf.getSample (ch, i));
            }
            if (type >= 0)
                std::printf ("     the DRIVE in the chain: in %.1f dBFS peak, out %.1f dBFS peak (the source %.1f)\n",
                             (double) juce::Decibels::gainToDecibels (peakIn), (double) juce::Decibels::gainToDecibels (peakOut),
                             (double) juce::Decibels::gainToDecibels (src.getMagnitude (0, 0, srcLen)));
            return out;
        };
        const auto render = [&] (int type, int clip) { return renderFrom (riff, type, clip); };

        // A TAKE THROWN AWAY FIRST: the first OrbitAmp of a process does not play what every one after it plays (what it
        // fits or loads off the audio thread lands late, then is shared), so the first take of a pair was never the
        // amp at its defaults — up to 13 dB apart at 10 kHz, 7 at 100 Hz, from the same input. Every take is a later one.
        (void) renderFrom (riff, -1, 0);
        const auto lufsOf = [&] (const juce::AudioBuffer<float>& b)
        {
            std::vector<const float*> chans;
            for (int ch = 0; ch < b.getNumChannels(); ++ch)
                chans.push_back (b.getReadPointer (ch));
            return loudness::integrated (chans, (size_t) b.getNumSamples(), rate);
        };
        const auto bringTo = [&] (juce::AudioBuffer<float>& b, double lufs)   // returns the gain, dB
        {
            const double db = lufs - lufsOf (b);
            b.applyGain ((float) std::pow (10.0, db / 20.0));
            return db;
        };
        const auto write = [&] (const juce::File& file, const juce::AudioBuffer<float>& out)
        {
            (void) file.getParentDirectory().createDirectory();
            (void) file.deleteFile();
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
            std::unique_ptr<juce::AudioFormatWriter> writer (stream == nullptr ? nullptr : wav.createWriterFor (stream.get(), rate, 2, 24, {}, 0));
            if (writer == nullptr)
                return false;
            (void) stream.release();
            writer->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
            std::printf ("%s: peak %.1f dBFS, RMS %.1f dBFS, %.1f LUFS\n", file.getFileName().toRawUTF8(),
                         (double) juce::Decibels::gainToDecibels (out.getMagnitude (0, 0, out.getNumSamples())),
                         (double) juce::Decibels::gainToDecibels (out.getRMSLevel (0, 0, out.getNumSamples())), lufsOf (out));
            return true;
        };

        // AFTER THE AMP, ours against his (ORBITAMP_DRIVE_COMPARE: the reference — the riff at -18 dBFS peak — through his
        // capture fed at its offset, written by orbitamp_drive_match --ref): his brought to the reference's loudness and
        // given the BOOSTER's TONE shelf (src/core/BoosterNam.h's fit, its path), ours the DRIVE at GAIN
        // ORBITAMP_DRIVE_COMPARE_GAIN, TONE ORBITAMP_DRIVE_COMPARE_TONE, VOLUME noon; both into a fresh OrbitAmp at its
        // defaults; the takes' long-term 1/3 octaves from 80 Hz to 10 kHz, levels aligned, and their loudness, compared.
        if (const auto his = juce::SystemStats::getEnvironmentVariable ("ORBITAMP_DRIVE_COMPARE", {}); his.isNotEmpty())
        {
            std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (juce::File (his)));
            if (r == nullptr)
                return 1;
            const float gain = (float) juce::SystemStats::getEnvironmentVariable ("ORBITAMP_DRIVE_COMPARE_GAIN", "5").getDoubleValue();
            const float tone = (float) juce::SystemStats::getEnvironmentVariable ("ORBITAMP_DRIVE_COMPARE_TONE", "5").getDoubleValue();
            juce::AudioBuffer<float> ref (riff);
            ref.applyGain (juce::Decibels::decibelsToGain (-18.0f) / std::max (1.0e-9f, ref.getMagnitude (0, 0, len)));
            juce::AudioBuffer<float> hisPedal (1, (int) r->lengthInSamples);
            r->read (&hisPedal, 0, hisPedal.getNumSamples(), 0, true, false);
            (void) bringTo (hisPedal, lufsOf (ref));
            {
                static constexpr double toneDb[11] = { -5.5, -5.0, -2.5, -1.0, -0.5, 0.0, 0.5, 2.0, 5.0, 10.0, 13.5 };
                static constexpr double toneHz[11] = { 476, 300, 300, 300, 300, 300, 300, 449, 1133, 3204, 5708 };
                const double k = juce::jlimit (0.0, 10.0, (double) tone);
                const int i = std::min (9, (int) k);
                const double f = k - i, db = toneDb[i] + (toneDb[i + 1] - toneDb[i]) * f;
                const double hz = toneHz[i] * std::pow (toneHz[i + 1] / toneHz[i], f), g = std::pow (10.0, db / 20.0);
                const double w = std::tan (juce::MathConstants<double>::pi * hz / rate), c = w / (1.0 + w);
                double st = 0.0;
                float* x = hisPedal.getWritePointer (0);
                for (int i2 = 0; i2 < hisPedal.getNumSamples(); ++i2)
                {
                    const double v = ((double) x[i2] - st) * c, lp = v + st;
                    st = lp + v;
                    x[i2] = (float) (lp + g * ((double) x[i2] - lp));
                }
            }
            if (juce::SystemStats::getEnvironmentVariable ("ORBITAMP_DRIVE_COMPARE_SELF", {}) == "1")   // the control: ours, from a file
            {
                hisPedal.makeCopyOf (ref);
                core::DrivePedal pedal;
                pedal.prepare (rate);
                core::DrivePedal::Settings ts;
                ts.gain = gain;
                ts.tone = tone;
                for (int at = 0; at < len; at += blockSize)
                    pedal.process (hisPedal.getWritePointer (0) + at, std::min (blockSize, len - at), ts);
            }
            const auto hisTake = renderFrom (hisPedal, -1, 0);
            const auto ourTake = renderFrom (ref, 0, 0, gain, tone);
            static constexpr double centres[22] = { 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000,
                                                    2500, 3150, 4000, 5000, 6300, 8000, 10000 };
            const auto thirds = [&] (const juce::AudioBuffer<float>& b)
            {
                constexpr int order = 13, n = 1 << order;
                juce::dsp::FFT fft (order);
                std::vector<float> buf ((size_t) n * 2);
                std::vector<double> power ((size_t) n / 2 + 1, 0.0);
                for (int at = 0; at + n <= b.getNumSamples(); at += n / 2)
                {
                    std::fill (buf.begin(), buf.end(), 0.0f);
                    for (int i = 0; i < n; ++i)
                        buf[(size_t) i] = b.getSample (0, at + i) * (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / n));
                    fft.performFrequencyOnlyForwardTransform (buf.data());
                    for (int k = 0; k <= n / 2; ++k)
                        power[(size_t) k] += (double) buf[(size_t) k] * buf[(size_t) k];
                }
                std::array<double, 22> out {};
                for (int c = 0; c < 22; ++c)
                {
                    double e = 1.0e-30;
                    for (int k = 1; k <= n / 2; ++k)
                        if (const double f = k * rate / n; f >= centres[c] / std::pow (2.0, 1.0 / 6.0) && f < centres[c] * std::pow (2.0, 1.0 / 6.0))
                            e += power[(size_t) k];
                    out[(size_t) c] = 10.0 * std::log10 (e);
                }
                return out;
            };
            // ...and before the amp, the two pedals' own outputs.
            {
                juce::AudioBuffer<float> ourPedal (ref);
                core::DrivePedal pedal;
                pedal.prepare (rate);
                core::DrivePedal::Settings ts;
                ts.gain = gain;
                ts.tone = tone;
                for (int at = 0; at < len; at += blockSize)
                    pedal.process (ourPedal.getWritePointer (0) + at, std::min (blockSize, len - at), ts);
                const auto a = thirds (ourPedal), b = thirds (hisPedal);
                double m = 0.0, w = 0.0;
                for (int c = 0; c < 22; ++c)
                    m += (a[(size_t) c] - b[(size_t) c]) / 22.0;
                std::printf ("before the amp, ours less his, %+.2f LU;\n   ", lufsOf (ourPedal) - lufsOf (hisPedal));
                for (int c = 0; c < 22; ++c)
                {
                    w = std::max (w, std::abs (a[(size_t) c] - b[(size_t) c] - m));
                    std::printf (" %g:%+.1f", centres[c], a[(size_t) c] - b[(size_t) c] - m);
                }
                std::printf ("\n   worst 1/3 octave %.1f dB\n", w);
            }
            const auto o = thirds (ourTake), h = thirds (hisTake);
            double mean = 0.0, worst = 0.0;
            for (int c = 0; c < 22; ++c)
                mean += (o[(size_t) c] - h[(size_t) c]) / 22.0;
            std::printf ("after the amp, DRIVE TS GAIN %.0f TONE %.0f against his TS through the booster's path at TONE %.0f: ours less his, "
                         "%+.2f LU;\n   ", (double) gain, (double) tone, (double) tone, lufsOf (ourTake) - lufsOf (hisTake));
            for (int c = 0; c < 22; ++c)
            {
                const double d = o[(size_t) c] - h[(size_t) c] - mean;
                worst = std::max (worst, std::abs (d));
                std::printf (" %g:%+.1f", centres[c], d);
            }
            std::printf ("\n   worst 1/3 octave %.1f dB\n", worst);
            return 0;
        }

        // THE BLIND PAIR: the DI through his TS-0003 (ORBITAMP_DRIVE_BLIND, written by orbitamp_drive_match, his capture
        // fed at its offset) into the amp with the block off, and the DI into the amp through our TS at noon — his
        // pedal's output brought to ours' loudness first, so the amp is pushed alike; the two takes then matched in
        // loudness; the order a coin's.
        if (const auto his = juce::SystemStats::getEnvironmentVariable ("ORBITAMP_DRIVE_BLIND", {}); his.isNotEmpty())
        {
            std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (juce::File (his)));
            if (r == nullptr)
                return 1;
            juce::AudioBuffer<float> hisPedal (1, (int) r->lengthInSamples);
            r->read (&hisPedal, 0, hisPedal.getNumSamples(), 0, true, false);
            juce::AudioBuffer<float> ourPedal (riff);
            core::DrivePedal pedal;
            pedal.prepare (rate);
            core::DrivePedal::Settings ts;
            const float gain = (float) juce::SystemStats::getEnvironmentVariable ("ORBITAMP_DRIVE_BLIND_GAIN", "5").getDoubleValue();
            const auto pair = juce::SystemStats::getEnvironmentVariable ("ORBITAMP_DRIVE_BLIND_NAME", "noon");
            const auto said = juce::SystemStats::getEnvironmentVariable ("ORBITAMP_DRIVE_BLIND_SAID", "his TS-0003.nam (Overdrive noon, Tone noon)");
            ts.gain = gain;
            for (int at = 0; at < len; at += blockSize)
                pedal.process (ourPedal.getWritePointer (0) + at, std::min (blockSize, len - at), ts);
            const double into = bringTo (hisPedal, lufsOf (ourPedal));
            auto hisTake = renderFrom (hisPedal, -1, 0);
            auto ourTake = renderFrom (riff, 0, 0, gain);
            const double after = bringTo (ourTake, lufsOf (hisTake));
            const bool hisFirst = juce::Random (juce::Time::currentTimeMillis()).nextBool();
            const auto dir = folder.getChildFile ("ts-blind");
            if (! write (dir.getChildFile (pair + "-A.wav"), hisFirst ? hisTake : ourTake) || ! write (dir.getChildFile (pair + "-B.wav"), hisFirst ? ourTake : hisTake))
                return 1;
            const auto ours = "our TS, GAIN " + juce::String (gain, 0) + ", TONE noon";
            (void) dir.getChildFile ("KEY.txt").appendText (pair + "-A: " + (hisFirst ? said : ours) + "\n" + pair + "-B: " + (hisFirst ? ours : said)
                + "\n" + pair + ": both into a fresh OrbitAmp at its defaults; his pedal's output brought to ours' loudness before the amp ("
                + juce::String (into, 1) + " dB), the takes matched in integrated loudness after it (ours " + juce::String (after, 1) + " dB).\n");
            return 0;
        }

        const std::pair<const char*, int> takes[] = { { "0-block-off", -1 }, { "1-ts-noon", 0 } };
        for (const auto& [name, type] : takes)
            if (! write (folder.getChildFile (juce::String (name) + ".wav"), render (type, 0)))
                return 1;
        return 0;
    }
}

int ORBITAMP_GATE_MAIN()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);     // a gate that dies says how far it got
    const juce::ScopedJuceInitialiser_GUI juce;

    if (const auto folder = juce::SystemStats::getEnvironmentVariable ("ORBITAMP_DRIVE_RECORD", {}); folder.isNotEmpty())
        return record (juce::File (folder));
    if (const auto riff = juce::SystemStats::getEnvironmentVariable ("ORBITAMP_DRIVE_LISTEN", {}); riff.isNotEmpty())
        return listen (juce::File (riff), juce::File (juce::SystemStats::getEnvironmentVariable ("ORBITAMP_DRIVE_LISTEN_OUT", {})));

    std::printf ("OrbitAmp's DRIVE\n");
    const auto was = before();
    check (was.played.size() == (size_t) (goldenBlocks * blockSize) && was.state.getSize() > 0,
           "the tree before the block left what it played and what it saved (tests/golden/orbitamp-before-drive.*)");

    // NOT OFFERED: what OrbitAmp played before the block, fresh and from a session of then.
    {
        auto amp = make (orbitAmp());
        matches (was, play (*amp, goldenBlocks), "a fresh OrbitAmp plays what it played before the block");
    }
    {
        auto amp = make (orbitAmp());
        amp->setStateInformation (was.state.getData(), (int) was.state.getSize());
        amp->pumpDeviceWork();
        amp->prepareToPlay (sampleRate, blockSize);
        amp->pumpDeviceWork();
        matches (was, play (*amp, goldenBlocks), "OrbitAmp on a session saved before the block plays what it played then");
    }

    // NO OTHER AMP HAS IT: not a parameter of the block's, not a place in its chain.
    {
        juce::StringArray had;
        for (const auto* each : { &products::catpitAmp(), &products::redPawAmp(), &products::frostPawAmp(), &products::amberPawAmp(),
                                  &products::orbitBass(), &products::redGrooveBass(), &products::deepGrooveBass() })
        {
            auto amp = make (quiet (*each));
            bool any = amp->chainHolds (params::rowDrive);
            for (const char* id : driveIds)
                any = any || amp->apvts.getParameter (id) != nullptr;
            for (auto* p : amp->getParameters())
                if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p); r != nullptr && r->getParameterID().startsWith ("drive"))
                    any = true;
            if (any)
                had.add (each->name);
        }
        check (had.isEmpty(), "no other amp has the block: no drive_ parameter, no DRIVE in its chain" + (had.isEmpty() ? juce::String() : " — " + had.joinIntoString (", ")));
    }

    // ORBITAMP: out of the strip out of the box, on and on TS at noon once there.
    {
        auto amp = make (orbitAmp());
        check (amp->chainHolds (params::rowDrive) && amp->linkRemovable (params::rowDrive) && ! amp->linkShown (params::rowDrive)
                   && param (*amp, params::driveOn) > 0.5f && juce::exactlyEqual (param (*amp, params::driveGain), 5.0f)
                   && amp->apvts.getParameter ("drive_type") == nullptr && amp->apvts.getParameter ("drive_clip_tmp") == nullptr,
               "OrbitAmp holds the DRIVE, out of its strip out of the box — ON, TS, GAIN 5 for when it comes in");
        check (juce::String (params::chainLinks[(size_t) params::rowDrive].onParam) == params::driveOn
                   && params::rowDrive + 1 == params::rowPreamp && params::rowDrive > params::rowComp,
               "in the chain after COMP, right before the AMP, its arrow writing drive_on");
    }

    // A DRIVE IN STANDBY is not run, and is as long as when it plays: offered and switched off, OrbitAmp plays what it
    // plays without it, 63 samples later — and tells the host so; out of the strip again, it does not. The GATE, the
    // REVERB and the LIMITER are off in both: the gate keys off the raw guitar ahead of the chain, the room's modulation
    // runs on the clock and the limiter on its grid, so with them on the two are not one signal shifted. Read from
    // the moment the AMP's model has landed and warmed (before it OrbitAmp is silent either way).
    {
        auto plain = make (orbitAmp());
        auto amp = make (orbitAmp());
        const int before = amp->getLatencySamples();
        for (auto* a : { plain.get(), amp.get() })
            for (const char* id : { params::gateOn, params::reverbOn, params::limiterOn })
                setParam (*a, id, 0.0f);
        setParam (*amp, params::drivePresent, 1.0f);
        setParam (*amp, params::driveOn, 0.0f);
        for (auto* a : { plain.get(), amp.get() })
        {
            a->prepareToPlay (sampleRate, blockSize);
            a->pumpDeviceWork();
        }
        const int offered = amp->getLatencySamples();
        const auto without = play (*plain, 3 * goldenBlocks);
        const auto now = play (*amp, 3 * goldenBlocks);
        const int lat = core::DrivePedal::latencySamples();
        size_t from = 0;
        while (from < without.size() && std::abs (without[from]) < 1.0e-3f)
            ++from;
        from += 9600;   // the model's warm and its fade in (core::CapturedBlock) are on the clock too
        float d = 0.0f, loud = 0.0f;
        for (size_t i = from; i + (size_t) lat < now.size(); ++i)
        {
            d = std::max (d, std::abs (now[i + (size_t) lat] - without[i]));
            loud = std::max (loud, std::abs (without[i]));
        }
        check (from < now.size() / 2 && loud > 0.05f && d < 1.0e-5f,
               "offered and in STANDBY: what OrbitAmp plays without it, 63 samples later (" + juce::String (d, 9) + " apart over "
                   + juce::String ((int) (now.size() - from)) + " samples)");
        setParam (*amp, params::drivePresent, 0.0f);
        check (offered == before + lat && amp->getLatencySamples() == before,
               "...the host told 63 samples more while it stands in the strip, and back when it leaves ("
                   + juce::String (before) + ", " + juce::String (offered) + ", " + juce::String (amp->getLatencySamples()) + ")");
    }

    // OFFERED AND ON, the TS plays — something else than before, and nothing that is not a number.
    for (int t = 0; t < params::driveTypes.size(); ++t)
    {
        auto amp = make (orbitAmp());
        setParam (*amp, params::drivePresent, 1.0f);
        amp->prepareToPlay (sampleRate, blockSize);
        amp->pumpDeviceWork();
        const auto now = play (*amp, goldenBlocks);
        float apartFrom = 0.0f;
        bool finite = true;
        for (size_t i = 0; i < now.size() && i < was.played.size(); ++i)
        {
            finite = finite && std::isfinite (now[i]);
            apartFrom = std::max (apartFrom, std::abs (now[i] - was.played[i]));
        }
        check (finite && apartFrom > 1.0e-3f, "offered, " + params::driveTypes[t] + " plays: the AMP hears the pedal ("
                                                   + juce::String (apartFrom, 4) + " apart from before)");
    }

    // A SESSION KEEPS IT: its place, its switch, GAIN, TONE and VOLUME.
    {
        auto a = make (orbitAmp());
        const std::pair<const char*, float> set[] = { { params::drivePresent, 1.0f }, { params::driveOn, 0.0f },
                                                      { params::driveGain, 7.3f }, { params::driveTone, 2.2f },
                                                      { params::driveVolume, -3.5f } };
        for (const auto& [id, v] : set)
            setParam (*a, id, v);
        juce::MemoryBlock m;
        a->getStateInformation (m);
        auto b = make (orbitAmp());
        b->setStateInformation (m.getData(), (int) m.getSize());
        b->pumpDeviceWork();
        bool kept = true;
        juce::String said;
        for (const auto& [id, v] : set)
        {
            const float back = param (*b, id);
            kept = kept && std::abs (back - v) < 0.006f;
            said << id << " " << juce::String (back, 2) << "  ";
        }
        check (kept, "a session keeps the DRIVE: " + said);
    }

    // A SESSION THAT NAMED ANOTHER TYPE (a drive_type of before, the 33) opens as the TS: no parameter takes the old
    // value, and the amp plays what a fresh one with the DRIVE offered plays, bit for bit.
    {
        auto fresh = make (orbitAmp());
        setParam (*fresh, params::drivePresent, 1.0f);
        juce::MemoryBlock m;
        fresh->getStateInformation (m);
        auto xml = juce::AudioProcessor::getXmlFromBinary (m.getData(), (int) m.getSize());
        // Every parameter tree in the session (the live one and the registers): a drive_type beside its drive_gain.
        int named = 0;
        std::function<void (juce::XmlElement&)> name = [&] (juce::XmlElement& e)
        {
            bool here = false;
            for (auto* c : e.getChildIterator())
                here = here || (c->hasTagName ("PARAM") && c->getStringAttribute ("id") == params::driveGain);
            if (here)
            {
                auto* p = e.createNewChildElement ("PARAM");
                p->setAttribute ("id", "drive_type");
                p->setAttribute ("value", 2.0);
                ++named;
            }
            for (auto* c : e.getChildIterator())
                if (! c->hasTagName ("PARAM"))
                    name (*c);
        };
        if (xml != nullptr)
            name (*xml);
        juce::MemoryBlock old;
        if (xml != nullptr)
            juce::AudioProcessor::copyXmlToBinary (*xml, old);
        auto opened = make (orbitAmp());
        opened->setStateInformation (old.getData(), (int) old.getSize());
        opened->pumpDeviceWork();
        for (auto* a : { fresh.get(), opened.get() })
        {
            a->prepareToPlay (sampleRate, blockSize);
            a->pumpDeviceWork();
        }
        const auto a = play (*fresh, goldenBlocks), b = play (*opened, goldenBlocks);
        float d = 0.0f;
        for (size_t i = 0; i < a.size() && i < b.size(); ++i)
            d = std::max (d, std::abs (a[i] - b[i]));
        check (named > 0 && opened->linkShown (params::rowDrive) && opened->apvts.getParameter ("drive_type") == nullptr && d <= 0.0f,
               "a session that named the 33 (drive_type 2) opens with the DRIVE offered, as the TS: what a fresh one plays ("
                   + juce::String (d, 9) + " apart)");
    }

    // OFFERED IN SETUP: its PEDALS page's DRIVE row, clicked, puts it in the strip — its arrow there, its tile on the face,
    // the window wider by it — as one undo step; the checklist lists it beside COMP.
    {
        auto amp = make (orbitAmp());
        std::unique_ptr<juce::AudioProcessorEditor> ed (amp->createEditor());
        auto* editor = dynamic_cast<AmpEditor*> (ed.get());
        const SettingsList::Row* row = nullptr;
        DriveBlock* tile = nullptr;
        if (editor != nullptr)
            walk (*editor, [&] (juce::Component& c)
                  {
                      if (auto* list = dynamic_cast<SettingsList*> (&c))
                          for (const auto& r : list->shownRows())
                              if (r.name == "DRIVE")
                                  row = &r;
                      if (auto* d = dynamic_cast<DriveBlock*> (&c))
                          tile = d;
                  });
        check (row != nullptr && tile != nullptr, "OrbitAmp's SETUP has a DRIVE row, and its face a DRIVE tile");
        if (editor != nullptr && row != nullptr && tile != nullptr)
        {
            const auto stripHas = [&]
            {
                for (const auto& r : AmpEditor::stripRows (*amp))
                    if (r.name == juce::String ("DRIVE"))
                        return r.present;
                return false;
            };
            const int least0 = editor->leastDesignWidth();
            const bool before = ! stripHas() && ! shownIn (*tile, *editor) && ! row->get();
            row->set (true);
            amp->pumpDeviceWork();
            const bool after = amp->linkShown (params::rowDrive) && stripHas() && shownIn (*tile, *editor) && row->get()
                            && editor->leastDesignWidth() == least0 + FaceplateView::driveWidth + FaceplateView::colGap;
            const auto knobs = tile->shownKnobs().joinIntoString (" ");
            // ORBITAMP_DRIVE_SHOT=<folder>: the tile as it stands, at 1x and 2x, for eyes.
            if (const auto shot = juce::SystemStats::getEnvironmentVariable ("ORBITAMP_DRIVE_SHOT", {}); shot.isNotEmpty())
                for (const int scale : { 1, 2 })
                {
                    juce::File out (juce::File (shot).getChildFile ("drive-ts-green-" + juce::String (scale) + "x.png"));
                    (void) out.getParentDirectory().createDirectory();
                    (void) out.deleteFile();
                    juce::FileOutputStream stream (out);
                    juce::PNGImageFormat png;
                    (void) png.writeImageToStream (tile->createComponentSnapshot (tile->getLocalBounds(), true, (float) scale), stream);
                }
            // VOLUME's hand on the OUT column follows the parameter, noon in the middle, up for louder.
            auto& columns = tile->levelColumns();
            const float noonY = columns.handY();
            setParam (*amp, params::driveVolume, 6.0f);
            const bool hand = std::abs (columns.value() - 6.0f) < 0.01f && columns.handY() < noonY - 10.0f;
            setParam (*amp, params::driveVolume, 0.0f);
            // ...and its number under the columns, typed there: "-4.5" writes drive_volume and moves the hand, "+20" is clamped.
            auto& number = tile->volumeReadout();
            const float noonAgain = columns.handY();
            number.setText ("-4.5", juce::sendNotificationSync);
            const bool typed = std::abs (param (*amp, params::driveVolume) + 4.5f) < 0.01f && columns.handY() > noonAgain + 5.0f
                            && number.shownText() == "-4.5 dB";
            number.setText ("+20", juce::sendNotificationSync);
            const bool clamped = std::abs (param (*amp, params::driveVolume) - 12.0f) < 0.01f && number.shownText() == "+12.0 dB";
            setParam (*amp, params::driveVolume, 0.0f);
            const bool noonText = number.shownText() == "+0.0 dB";
            juce::String list;
            for (const auto& [r, in] : editor->checklistRows())
                list << params::chainLinks[(size_t) r].name << (in ? "+ " : "- ");
            amp->history.undo();
            amp->pumpDeviceWork();
            const bool undone = ! amp->linkShown (params::rowDrive) && ! stripHas() && ! shownIn (*tile, *editor)
                             && editor->leastDesignWidth() == least0;
            check (before && after && undone && knobs == "GAIN TONE" && tile->typeShown() == "TS" && hand && list == "COMP- DRIVE+ ",
                   "SETUP's DRIVE row puts it in the strip and on the face, the window wider by it, one undo step back ("
                       + knobs + ", type " + tile->typeShown() + ", VOLUME's hand " + (hand ? "follows" : "DOES NOT follow")
                       + "; the checklist " + list + ")");
            check (typed && clamped && noonText, "VOLUME's number under the columns: typed -4.5 writes drive_volume and moves the hand, "
                                                 "+20 lands on +12.0 dB, noon reads +0.0 dB");
        }
    }

    std::printf ("%s\n", failed == 0 ? "PASSED" : "FAILED");
    return failed == 0 ? 0 : 1;
}
