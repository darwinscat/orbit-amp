// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// THE DRIVE'S TS AGAINST THE OWNER'S OWN — not a gate, a measuring instrument. One DI through his trained captures of
// his pedal and through core::DrivePedal's TS, the long-term spectra compared 1/3 octave by 1/3 octave from 80 Hz to
// 6.3 kHz (levels aligned: shapes only), at three settings: GAIN 2 against his Overdrive 60, GAIN 5 against 150
// (noon), GAIN 8 against 240, TONE noon throughout as his captures are.
//
//     orbitamp_drive_match <di.wav> <TS-0001.nam> <TS-0003.nam> <TS-0005.nam> [search] [out.wav]
//
// Prints the table for the shipping voicing; `search` walks the voicing's numbers (core::DrivePedal::Voicing) for the
// least worst band and prints the table again; `out.wav` writes the DI through TS-0003, for a blind listen.

#include "core/DrivePedal.h"

#include <felitronics/nam/NamStage.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

using orbitamp::core::DrivePedal;

namespace
{
    constexpr double rate = 48000.0;
    constexpr int bands = 20;
    constexpr double centres[bands] = { 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000,
                                        2500, 3150, 4000, 5000, 6300 };

    using Spectrum = std::array<double, bands>;

    /** The long-term power in each 1/3 octave, dB: Hann windows of 8192, half overlapped. */
    Spectrum thirds (const std::vector<float>& x)
    {
        constexpr int order = 13, n = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> buf ((size_t) n * 2);
        std::vector<double> power ((size_t) n / 2 + 1, 0.0);
        for (size_t at = 0; at + (size_t) n <= x.size(); at += n / 2)
        {
            std::fill (buf.begin(), buf.end(), 0.0f);
            for (int i = 0; i < n; ++i)
                buf[(size_t) i] = x[at + (size_t) i] * (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / n));
            fft.performFrequencyOnlyForwardTransform (buf.data());
            for (int k = 0; k <= n / 2; ++k)
                power[(size_t) k] += (double) buf[(size_t) k] * buf[(size_t) k];
        }
        Spectrum s {};
        for (int b = 0; b < bands; ++b)
        {
            const double lo = centres[b] / std::pow (2.0, 1.0 / 6.0), hi = centres[b] * std::pow (2.0, 1.0 / 6.0);
            double e = 1.0e-30;
            for (int k = 1; k <= n / 2; ++k)
                if (const double f = k * rate / n; f >= lo && f < hi)
                    e += power[(size_t) k];
            s[(size_t) b] = 10.0 * std::log10 (e);
        }
        return s;
    }

    std::vector<float> throughNam (const juce::File& file, const std::vector<float>& in)
    {
        juce::MemoryBlock bytes;
        file.loadFileAsData (bytes);
        felitronics::nam::NamStage stage;
        stage.prepare (rate, 512);
        if (! stage.loadModelFromMemory (bytes.getData(), bytes.getSize()))
        {
            std::printf ("model refused: %s\n", file.getFullPathName().toRawUTF8());
            return {};
        }
        std::vector<float> out (in);
        for (size_t at = 0; at < out.size(); at += 512)
        {
            float* io[1] = { out.data() + at };
            stage.process (io, 1, (int) std::min<size_t> (512, out.size() - at), true);
        }
        return out;
    }

    std::vector<float> throughPedal (const std::vector<float>& in, float gain, const DrivePedal::Voicing& v)
    {
        DrivePedal p;
        p.voicing = v;
        p.prepare (rate);
        DrivePedal::Settings s;
        s.type = DrivePedal::Type::ts;
        s.gain = gain;
        s.tone = 5.0f;
        std::vector<float> out (in);
        for (size_t at = 0; at < out.size(); at += 512)
            p.process (out.data() + at, (int) std::min<size_t> (512, out.size() - at), s);
        return out;
    }

    /** Ours less his per band, the mean taken out (levels aligned); and the worst band. */
    double apart (const Spectrum& ours, const Spectrum& his, Spectrum* each = nullptr)
    {
        double mean = 0.0;
        for (int b = 0; b < bands; ++b)
            mean += ours[(size_t) b] - his[(size_t) b];
        mean /= bands;
        double worst = 0.0;
        for (int b = 0; b < bands; ++b)
        {
            const double d = ours[(size_t) b] - his[(size_t) b] - mean;
            if (each != nullptr)
                (*each)[(size_t) b] = d;
            worst = std::max (worst, std::abs (d));
        }
        return worst;
    }
}

int main (int argc, char** argv)
{
    if (argc < 5)
    {
        std::printf ("usage: orbitamp_drive_match <di.wav> <TS-0001.nam> <TS-0003.nam> <TS-0005.nam> [search] [out.wav]\n");
        return 1;
    }
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (juce::File (argv[1])));
    if (reader == nullptr)
        return 1;
    juce::AudioBuffer<float> file (1, (int) reader->lengthInSamples);
    reader->read (&file, 0, file.getNumSamples(), 0, true, false);

    // The DI at two levels a DI peaks at: -12 and -18 dBFS.
    std::vector<std::vector<float>> dis;
    const float peak = file.getMagnitude (0, 0, file.getNumSamples());
    for (const float db : { -12.0f, -18.0f })
    {
        std::vector<float> v ((size_t) file.getNumSamples());
        const float g = juce::Decibels::decibelsToGain (db) / std::max (peak, 1.0e-6f);
        for (size_t i = 0; i < v.size(); ++i)
            v[i] = file.getSample (0, (int) i) * g;
        dis.push_back (std::move (v));
    }

    const float gains[3] = { 2.0f, 5.0f, 8.0f };
    const char* names[3] = { "GAIN 2 / Overdrive 60", "GAIN 5 / Overdrive 150", "GAIN 8 / Overdrive 240" };
    Spectrum his[2][3];
    for (int l = 0; l < 2; ++l)
        for (int k = 0; k < 3; ++k)
            his[l][k] = thirds (throughNam (juce::File (argv[2 + k]), dis[(size_t) l]));

    // HIS SWEEPS' SHAPE at the -24 dB rung (Overdrive noon, Tone noon; ~/CLAUDE_FILES/drive-ref): against the peak,
    // 100 Hz -6.0, 300 Hz about -1.5, 714 Hz 0, 1.2 kHz about -1, 3 kHz -5.8, 6 kHz -11.1 — ours at the same level, a
    // sine's fundamental at each, GAIN noon. Weighed with the spectra, so the hump is not traded away for them.
    const double shapeHz[6] = { 100, 300, 714, 1200, 3000, 6000 }, shapeDb[6] = { -6.0, -1.5, 0.0, -1.0, -5.8, -11.1 };
    const auto shapeOff = [&] (const DrivePedal::Voicing& v, bool print)
    {
        double got[6], top = -200.0;
        for (int k = 0; k < 6; ++k)
        {
            std::vector<float> sine (24000);
            for (size_t i = 0; i < sine.size(); ++i)
                sine[i] = 0.0316f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * shapeHz[k] * (double) i / rate);
            const auto o = throughPedal (sine, 5.0f, v);
            double c = 0.0, s = 0.0;
            for (size_t i = 12000; i < 24000; ++i)
            {
                c += o[i] * std::cos (2.0 * juce::MathConstants<double>::pi * shapeHz[k] * (double) i / rate);
                s += o[i] * std::sin (2.0 * juce::MathConstants<double>::pi * shapeHz[k] * (double) i / rate);
            }
            got[k] = 10.0 * std::log10 (c * c + s * s + 1.0e-30);
            top = std::max (top, got[k]);
        }
        double worst = 0.0;
        if (print)
            std::printf ("  sweep shape at -30 dBFS, GAIN noon (his in brackets):");
        for (int k = 0; k < 6; ++k)
        {
            worst = std::max (worst, std::abs (got[k] - top - shapeDb[k]));
            if (print)
                std::printf (" %g:%+.1f(%+.1f)", shapeHz[k], got[k] - top, shapeDb[k]);
        }
        if (print)
            std::printf ("\n");
        return worst;
    };

    const auto score = [&] (const DrivePedal::Voicing& v, bool print)
    {
        double worst = shapeOff (v, print);
        for (int l = 0; l < 2; ++l)
            for (int k = 0; k < 3; ++k)
            {
                Spectrum each {};
                const double w = apart (thirds (throughPedal (dis[(size_t) l], gains[k], v)), his[l][k], &each);
                worst = std::max (worst, w);
                if (print)
                {
                    std::printf ("  %s, DI %s dBFS: worst %.1f dB\n   ", names[k], l == 0 ? "-12" : "-18", w);
                    for (int b = 0; b < bands; ++b)
                        std::printf (" %g:%+.1f", centres[b], each[(size_t) b]);
                    std::printf ("\n");
                }
            }
        return worst;
    };

    // How hard each clips: a 400 Hz sine at -24 and -12 dBFS through his TS-0003 and through ours at noon — the 2nd to
    // 5th harmonics against the fundamental. A long-term spectrum cannot tell a clean EQ from a distortion; this can.
    for (const float db : { -24.0f, -12.0f })
    {
        std::vector<float> sine (48000);
        for (size_t i = 0; i < sine.size(); ++i)
            sine[i] = juce::Decibels::decibelsToGain (db) * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 400.0 * (double) i / rate);
        const auto level = [] (const std::vector<float>& x, double hz)
        {
            double c = 0.0, s = 0.0;
            for (size_t i = 24000; i < 48000; ++i)
            {
                c += x[i] * std::cos (2.0 * juce::MathConstants<double>::pi * hz * (double) i / rate);
                s += x[i] * std::sin (2.0 * juce::MathConstants<double>::pi * hz * (double) i / rate);
            }
            return 10.0 * std::log10 (c * c + s * s + 1.0e-30);
        };
        const auto hisOut = throughNam (juce::File (argv[3]), sine);
        const auto ours = throughPedal (sine, 5.0f, DrivePedal::Voicing {});
        std::printf ("400 Hz at %.0f dBFS, harmonics 2..5 re the fundamental:  his", (double) db);
        for (int h = 2; h <= 5; ++h)
            std::printf (" %.0f", level (hisOut, 400.0 * h) - level (hisOut, 400.0));
        std::printf ("   ours");
        for (int h = 2; h <= 5; ++h)
            std::printf (" %.0f", level (ours, 400.0 * h) - level (ours, 400.0));
        std::printf (" dB\n");
    }

    DrivePedal::Voicing v;
    std::printf ("THE SHIPPING VOICING (volt %.2f, post %.0f Hz, shelf at noon %+.1f dB, leg %g..%g dB, top %.0f Hz)\n",
                 v.tsVolt, v.tsPostHz, v.tsShelfNoonDb, v.tsLegLoDb, v.tsLegHiDb, v.tsLpHz);
    std::printf ("worst band over all: %.1f dB\n", score (v, true));

    if (argc > 5 && juce::String (argv[5]) == "search")
    {
        auto best = v;
        double bestScore = score (v, false);
        const auto tryOne = [&] (DrivePedal::Voicing c)
        {
            if (const double sc = score (c, false); sc < bestScore)
            {
                bestScore = sc;
                best = c;
            }
        };
        // Coordinate passes, coarse to fine: the scale, the corners and the shelf, then the leg's law.
        for (int pass = 0; pass < 3; ++pass)
        {
            const double k = pass == 0 ? 1.0 : pass == 1 ? 0.5 : 0.25;
            for (const double m : { 0.35, 0.5, 0.7, 1.0, 1.4, 2.0, 2.8 })
                { auto c = best; c.tsVolt = best.tsVolt * std::pow (m, k); tryOne (c); }
            for (const double hz : { 0.7, 0.85, 1.0, 1.2, 1.45 })
                { auto c = best; c.tsPostHz = best.tsPostHz * std::pow (hz, k); tryOne (c); }
            for (const double db : { -3.0, -1.5, 1.5, 3.0, 4.5, 6.0 })
                { auto c = best; c.tsShelfNoonDb = best.tsShelfNoonDb + db * k; tryOne (c); }
            for (const double hz : { 0.6, 0.8, 1.25, 1.6 })
                { auto c = best; c.tsLpHz = best.tsLpHz * std::pow (hz, k); tryOne (c); }
            for (const double lo : { -6.0, -3.0, 3.0, 6.0 })
                { auto c = best; c.tsLegLoDb = best.tsLegLoDb + lo * k; tryOne (c); }
            for (const double hi : { -8.0, -4.0, 4.0, 8.0 })
                { auto c = best; c.tsLegHiDb = best.tsLegHiDb + hi * k; tryOne (c); }
            std::printf ("pass %d: worst %.2f dB (volt %.3f, post %.0f, shelf %+.2f, top %.0f, leg %.1f..%.1f)\n", pass, bestScore,
                         best.tsVolt, best.tsPostHz, best.tsShelfNoonDb, best.tsLpHz, best.tsLegLoDb, best.tsLegHiDb);
        }
        std::printf ("THE MATCHED VOICING (volt %.3f, post %.0f Hz, shelf at noon %+.2f dB, leg %.1f..%.1f dB, top %.0f Hz)\n",
                     best.tsVolt, best.tsPostHz, best.tsShelfNoonDb, best.tsLegLoDb, best.tsLegHiDb, best.tsLpHz);
        std::printf ("worst band over all: %.1f dB\n", score (best, true));
    }

    if (argc > 6)
    {
        // The DI as it is (its own level) through TS-0003, for the blind pair.
        std::vector<float> raw ((size_t) file.getNumSamples());
        for (size_t i = 0; i < raw.size(); ++i)
            raw[i] = file.getSample (0, (int) i);
        const auto out = throughNam (juce::File (argv[3]), raw);
        juce::AudioBuffer<float> b (1, (int) out.size());
        b.copyFrom (0, 0, out.data(), (int) out.size());
        juce::File dst (argv[6]);
        (void) dst.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (dst.createOutputStream());
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (stream.get(), rate, 1, 32, {}, 0));
        if (w == nullptr)
            return 1;
        (void) stream.release();
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        std::printf ("wrote %s (peak %.1f dBFS)\n", dst.getFullPathName().toRawUTF8(),
                     (double) juce::Decibels::gainToDecibels (b.getMagnitude (0, 0, b.getNumSamples())));
    }
    return 0;
}
