// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// THE DRIVE'S TS AGAINST THE OWNER'S OWN — not a gate, a measuring instrument. One DI through his trained captures of
// his pedal and through core::DrivePedal's TS at three settings: GAIN 2 against his Overdrive 60, GAIN 5 against 150
// (noon), GAIN 8 against 240, TONE noon throughout as his captures are. Compared:
//
//   - the long-term spectra 1/3 octave by 1/3 octave from 80 Hz to 6.3 kHz (levels aligned: shapes only);
//   - THE DENSITY: the crest factor on the DI, peak less RMS and the 99.9th percentile of |x| less RMS;
//   - HOW HARD EACH CLIPS: a 400 Hz sine at -30, -24, -18 and -12 dBFS, the 3rd and 5th harmonics re the fundamental
//     (weighed where either stands within 40 dB of it: under that a NAM's floor and our solver's say nothing);
//   - the level each hands on, ours less his, RMS on the DI.
//
// OURS STAYS AT THE GUITAR'S LEVEL (1 V = 0.5 in the float, a DI at -12 dBFS a humbucker's half volt); HIS ARE MOVED.
// A NAM has no absolute level — the reamp chain of the day said which float meant which volt at his pedal — so his
// captures are fed X dB off the DI (--nam-db=X), one X for all three, the X at which they clip as ours does.
//
//     orbitamp_drive_match <di.wav> <TS-0001.nam> <TS-0003.nam> <TS-0005.nam> [trims] [offset] [search] [--nam-db=X] [--out=<prefix>]
//
// Prints the cards — round two's TS against his captures as they come, then ours as it ships against his fed X —;
// `trims` fits every type's trim at VOLUME noon (out as loud as in, BS.1770, on the DI at -18 dBFS peak) and prints the
// table for core::DrivePedal::Voicing::trimDb; the cards end with the level each hands the amp, ours against his fed X
// and brought to the DI's loudness; `offset` walks X alone, half a dB at a time, a line per X and per capture; `search` walks ours' voicing numbers
// (core::DrivePedal::Voicing) and X together for the least worst of the three measures and prints the cards again; --out writes the
// DI at its own level, fed X, through TS-0001/0003/0005 for the blind pairs: <prefix>-60.wav, -150.wav, -240.wav.

#include "core/DrivePedal.h"
#include "Loudness.h"

#include <felitronics/nam/NamStage.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
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

    std::vector<float> throughPedal (const std::vector<float>& in, float gain, const DrivePedal::Voicing& v,
                                     DrivePedal::Type type = DrivePedal::Type::ts)
    {
        DrivePedal p;
        p.voicing = v;
        p.prepare (rate);
        DrivePedal::Settings s;
        s.type = type;
        s.gain = gain;
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

    /** THE DENSITY, dB: the peak and the 99.9th percentile of |x|, each less the RMS — past the first 0.1 s. */
    struct Crest { double peak = 0.0, p999 = 0.0, rmsDb = -200.0; };
    Crest crestOf (const std::vector<float>& x)
    {
        const size_t from = std::min<size_t> (4800, x.size());
        std::vector<float> a;
        a.reserve (x.size() - from);
        double e = 0.0;
        for (size_t i = from; i < x.size(); ++i)
        {
            a.push_back (std::abs (x[i]));
            e += (double) x[i] * x[i];
        }
        if (a.empty())
            return {};
        const double rms = 10.0 * std::log10 (e / (double) a.size() + 1.0e-30);
        const double top = 20.0 * std::log10 ((double) *std::max_element (a.begin(), a.end()) + 1.0e-15);
        const auto q = a.begin() + (std::ptrdiff_t) (0.999 * (double) (a.size() - 1));
        std::nth_element (a.begin(), q, a.end());
        return { top - rms, 20.0 * std::log10 ((double) *q + 1.0e-15) - rms, rms };
    }

    /** A 400 Hz sine at `db` dBFS, a second of it. */
    std::vector<float> sineAt (double db)
    {
        std::vector<float> sine (48000);
        const double a = std::pow (10.0, db / 20.0);
        for (size_t i = 0; i < sine.size(); ++i)
            sine[i] = (float) (a * std::sin (2.0 * juce::MathConstants<double>::pi * 400.0 * (double) i / rate));
        return sine;
    }

    /** The 2nd to 5th harmonics of the 400 Hz sine re its fundamental, dB, over the second half. */
    std::array<double, 4> harmonicsOf (const std::vector<float>& x)
    {
        const auto level = [&] (double hz)
        {
            double c = 0.0, s = 0.0;
            for (size_t i = 24000; i < 48000; ++i)
            {
                c += x[i] * std::cos (2.0 * juce::MathConstants<double>::pi * hz * (double) i / rate);
                s += x[i] * std::sin (2.0 * juce::MathConstants<double>::pi * hz * (double) i / rate);
            }
            return 10.0 * std::log10 (c * c + s * s + 1.0e-30);
        };
        const double f = level (400.0);
        return { level (800.0) - f, level (1200.0) - f, level (1600.0) - f, level (2000.0) - f };
    }
}

int main (int argc, char** argv)
{
    if (argc < 5)
    {
        std::printf ("usage: orbitamp_drive_match <di.wav> <TS-0001.nam> <TS-0003.nam> <TS-0005.nam> [trims] [offset] [search] [--nam-db=X] [--out=<prefix>]\n");
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
    constexpr int levels = 4;
    const double sineDb[levels] = { -30.0, -24.0, -18.0, -12.0 };
    const auto scaled = [] (std::vector<float> x, double db)
    {
        const float g = (float) std::pow (10.0, db / 20.0);
        for (auto& s : x)
            s *= g;
        return x;
    };

    /** WHAT HIS PEDAL DOES with his captures fed `namDb` hotter than the DI: each one's spectra and density on the two
        DI levels, and its harmonics at each sine level. */
    struct His
    {
        Spectrum thirds[2][3];
        Crest crest[2][3];
        std::array<double, 4> harm[3][levels];
    };
    const auto hisAt = [&] (double namDb)
    {
        His h;
        for (int k = 0; k < 3; ++k)
        {
            for (int l = 0; l < 2; ++l)
            {
                const auto out = throughNam (juce::File (argv[2 + k]), scaled (dis[(size_t) l], namDb));
                h.thirds[l][k] = thirds (out);
                h.crest[l][k] = crestOf (out);
            }
            for (int d = 0; d < levels; ++d)
                h.harm[k][d] = harmonicsOf (throughNam (juce::File (argv[2 + k]), sineAt (sineDb[d] + namDb)));
        }
        return h;
    };

    /** OURS for a voicing, the same measures. */
    const auto oursAt = [&] (const DrivePedal::Voicing& v)
    {
        His o;
        for (int k = 0; k < 3; ++k)
        {
            for (int l = 0; l < 2; ++l)
            {
                const auto out = throughPedal (dis[(size_t) l], gains[k], v);
                o.thirds[l][k] = thirds (out);
                o.crest[l][k] = crestOf (out);
            }
            for (int d = 0; d < levels; ++d)
                o.harm[k][d] = harmonicsOf (throughPedal (sineAt (sineDb[d]), gains[k], v));
        }
        return o;
    };

    // HIS SWEEPS' SHAPE at the -24 dB rung (Overdrive noon, Tone noon; ~/CLAUDE_FILES/drive-ref): against the peak,
    // 100 Hz -6.0, 300 Hz about -1.5, 714 Hz 0, 1.2 kHz about -1, 3 kHz -5.8, 6 kHz -11.1 — ours at the same level, a
    // sine's fundamental at each, GAIN noon. Printed, not weighed: the sweeps' level was never written down, and at
    // Overdrive noon even the -24 dB rung clips (the files say so), so the shape is a level's, and which one is unknown.
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

    /** THE CARD, per GAIN (k): the worst 1/3-octave band, the crest's worst and mean difference (ours less his), the 3rd
        and 5th harmonics' worst difference, the level ours less his (RMS on the DI). */
    struct Card { double band = 0.0, crestWorst = 0.0, crestMean = 0.0, harmWorst = 0.0, levelMean = 0.0; };
    const auto card = [&] (const His& o, const His& h, int k, bool print)
    {
        Card c;
        for (int l = 0; l < 2; ++l)
        {
            Spectrum each {};
            const double w = apart (o.thirds[l][k], h.thirds[l][k], &each);
            c.band = std::max (c.band, w);
            const auto& a = o.crest[l][k];
            const auto& b = h.crest[l][k];
            for (const double d : { a.peak - b.peak, a.p999 - b.p999 })
            {
                c.crestWorst = std::max (c.crestWorst, std::abs (d));
                c.crestMean += d / 4.0;
            }
            c.levelMean += (a.rmsDb - b.rmsDb) / 2.0;
            if (print)
            {
                std::printf ("  %s, DI %s dBFS: worst band %.1f dB; crest peak %.1f (his %.1f), p99.9 %.1f (his %.1f); "
                             "RMS ours less his %+.1f dB\n   ", names[k], l == 0 ? "-12" : "-18", w, a.peak, b.peak,
                             a.p999, b.p999, a.rmsDb - b.rmsDb);
                for (int band = 0; band < bands; ++band)
                    std::printf (" %g:%+.1f", centres[band], each[(size_t) band]);
                std::printf ("\n");
            }
        }
        for (int d = 0; d < levels; ++d)
        {
            for (const int x : { 1, 3 })   // the 3rd and the 5th, where either stands 40 dB under the fundamental or less
                if (std::max (o.harm[k][d][(size_t) x], h.harm[k][d][(size_t) x]) > -40.0)
                    c.harmWorst = std::max (c.harmWorst, std::abs (o.harm[k][d][(size_t) x] - h.harm[k][d][(size_t) x]));
            if (print)
                std::printf ("  %s, 400 Hz at %+.0f dBFS, H2..H5 re the fundamental: ours %.0f %.0f %.0f %.0f   his %.0f %.0f %.0f %.0f\n",
                             names[k], sineDb[d], o.harm[k][d][0], o.harm[k][d][1], o.harm[k][d][2], o.harm[k][d][3],
                             h.harm[k][d][0], h.harm[k][d][1], h.harm[k][d][2], h.harm[k][d][3]);
        }
        if (print)
            std::printf ("  CARD %s: worst band %.1f dB, crest worst %.1f mean %+.1f dB, H3/H5 worst %.1f dB, level ours less his %+.1f dB\n",
                         names[k], c.band, c.crestWorst, c.crestMean, c.harmWorst, c.levelMean);
        return c;
    };
    const auto cards = [&] (const DrivePedal::Voicing& v, const His& h, const char* title)
    {
        std::printf ("%s (volt %.4f, post %.0f Hz, shelf at noon %+.2f dB, leg %.1f..%.1f dB, top %.0f Hz)\n", title, v.volt,
                     v.tsPostHz, v.tsShelfNoonDb, v.tsLegLoDb, v.tsLegHiDb, v.tsLpHz);
        (void) shapeOff (v, true);
        const auto o = oursAt (v);
        for (int k = 0; k < 3; ++k)
            (void) card (o, h, k, true);
    };

    // The options: `offset` walks his captures' input; `search` retunes ours against them; --nam-db=<X> feeds his
    // captures X dB hotter than the DI (0 unless said); --out=<prefix> writes the DI through each of his three.
    bool walk = false, search = false, fit = false;
    double namDb = 0.0;
    juce::String outPrefix;
    for (int a = 5; a < argc; ++a)
    {
        const juce::String arg (argv[a]);
        if (arg == "offset") walk = true;
        else if (arg == "search") search = true;
        else if (arg == "trims") fit = true;
        else if (arg.startsWith ("--nam-db=")) namDb = arg.fromFirstOccurrenceOf ("=", false, false).getDoubleValue();
        else if (arg.startsWith ("--out=")) outPrefix = arg.fromFirstOccurrenceOf ("=", false, false);
    }

    // BEFORE: round two's TS, a DI driving it as hard as his captures seemed to (1 V = 0.043), against them as they come.
    DrivePedal::Voicing before;
    before.volt = 0.043;
    before.tsPostHz = 1160.0; before.tsShelfNoonDb = 9.0; before.tsLegLoDb = 15.5; before.tsLegHiDb = 37.0; before.tsLpHz = 5500.0;
    before.trimDb = {};   // no trim then
    const auto hisAsTheyCome = hisAt (0.0);
    cards (before, hisAsTheyCome, "BEFORE: round two's TS (1 V = 0.043) against his captures as they come");

    DrivePedal::Voicing v;
    const auto his = hisAt (namDb);
    std::printf ("\nHIS CAPTURES FED %+.1f dB\n", namDb);
    cards (v, his, "THE SHIPPING VOICING");

    // THE LEVEL EACH HANDS THE AMP, on THE REFERENCE: the DI at -18 dBFS peak, the one level every block before the amp
    // is calibrated at (a pedal that clips is at unity at one input level only): ours at VOLUME noon, and his fed X with
    // its output brought to the DI's loudness — the rule a block before the amp keeps (the BOOSTER's path: its capture
    // fed X, then trimmed the same way). Integrated loudness and peak, ours less his.
    const auto& di = dis[1];
    const double diLufs = orbitamp::loudness::integrated (di, rate);
    std::printf ("\nAT THE AMP'S INPUT, the reference DI at -18 dBFS peak (%.1f LUFS); his fed %+.1f dB, then trimmed to the DI's loudness\n",
                 diLufs, namDb);
    for (int k = 0; k < 3; ++k)
    {
        const auto ours = throughPedal (di, gains[k], v);
        auto hisOut = throughNam (juce::File (argv[2 + k]), scaled (di, namDb));
        hisOut = scaled (hisOut, diLufs - orbitamp::loudness::integrated (hisOut, rate));
        const auto peakDb = [] (const std::vector<float>& x)
        {
            float m = 0.0f;
            for (const float s : x)
                m = std::max (m, std::abs (s));
            return 20.0 * std::log10 ((double) m + 1.0e-15);
        };
        const double lo = orbitamp::loudness::integrated (ours, rate), lh = orbitamp::loudness::integrated (hisOut, rate);
        std::printf ("  %s: ours %.2f LUFS, peak %.1f dBFS   his %.2f LUFS, peak %.1f dBFS   ours less his %+.2f LU, %+.1f dB of peak\n",
                     names[k], lo, peakDb (ours), lh, peakDb (hisOut), lo - lh, peakDb (ours) - peakDb (hisOut));
    }

    // THE TRIMS: every type at GAIN 0, 1, .. 10, VOLUME noon, the other knobs at noon, as loud out as the reference DI
    // (-18 dBFS peak) went in. Fitted three rounds from the shipping table (the guard is a curve too), then checked at
    // every step and half step — and at -12 dBFS peak, where a pedal that compresses comes out under.
    if (fit)
    {
        static const char* const typeNames[] = { "TS", "PRECISION", "33", "SD-1" };
        auto t = v;
        const auto off = [&] (int type, float gain)
        {
            return orbitamp::loudness::integrated (throughPedal (di, gain, t, (DrivePedal::Type) type), rate) - diLufs;
        };
        for (int round = 0; round < 3; ++round)
            for (int type = 0; type < (int) DrivePedal::Type::count; ++type)
                for (int g = 0; g <= 10; ++g)
                    t.trimDb[(size_t) type][(size_t) g] -= off (type, (float) g);
        for (int type = 0; type < (int) DrivePedal::Type::count; ++type)
        {
            double worst = 0.0, worstHalf = 0.0;
            for (int g = 0; g <= 10; ++g)
                worst = std::max (worst, std::abs (off (type, (float) g)));
            for (int g = 0; g < 10; ++g)
                worstHalf = std::max (worstHalf, std::abs (off (type, (float) g + 0.5f)));
            std::printf ("  trim %-9s", typeNames[type]);
            for (int g = 0; g <= 10; ++g)
                std::printf (" %+.2f", t.trimDb[(size_t) type][(size_t) g]);
            std::printf ("   (out less in: worst %.2f LU at the steps, %.2f between)\n   at -12 dBFS peak, out less in:", worst, worstHalf);
            const double hotLufs = orbitamp::loudness::integrated (dis[0], rate);
            for (int g = 0; g <= 10; g += 2)
                std::printf (" GAIN %d %+.2f", g, orbitamp::loudness::integrated (throughPedal (dis[0], (float) g, t, (DrivePedal::Type) type), rate) - hotLufs);
            std::printf (" LU\n");
        }
        std::printf ("  as C++:\n");
        for (int type = 0; type < (int) DrivePedal::Type::count; ++type)
        {
            std::printf ("            {{");
            for (int g = 0; g <= 10; ++g)
                std::printf ("%s %.2f", g == 0 ? "" : ",", t.trimDb[(size_t) type][(size_t) g]);
            std::printf (" }},   // %s\n", typeNames[type]);
        }
    }

    // HIS CAPTURES' INPUT ALONE, against ours as it ships: X dB hotter into each NAM, a line per X and per capture.
    if (walk)
    {
        const auto o = oursAt (v);
        for (double x = -18.0; x <= -6.0 + 0.01; x += 0.5)
        {
            const auto h = hisAt (x);
            std::printf ("X %+5.1f dB:", x);
            for (int k = 0; k < 3; ++k)
            {
                const auto c = card (o, h, k, false);
                std::printf ("   [GAIN %.0f] band %.1f crest %.1f/%+.1f H3/H5 %.1f (%.2f)", (double) gains[k], c.band, c.crestWorst, c.crestMean,
                             c.harmWorst, std::max ({ c.band / 2.0, c.crestWorst / 0.5, c.harmWorst / 1.0 }));
            }
            std::printf ("\n");
        }
    }

    // THE JOINT SEARCH: ours' voicing numbers and his captures' X together, for the least worst of the three measures
    // each over its own tolerance — 2 dB a band, 0.5 dB of crest, 1 dB of the 3rd and 5th — one X for all three
    // captures, tried at the best X so far and half a dB and a dB either side of it.
    if (search)
    {
        std::map<int, His> hisCache;
        const auto hisHalf = [&] (int half) -> const His&
        {
            auto it = hisCache.find (half);
            if (it == hisCache.end())
                it = hisCache.emplace (half, hisAt (half * 0.5)).first;
            return it->second;
        };
        int bestX = (int) std::lround (namDb * 2.0);
        const auto score = [&] (const DrivePedal::Voicing& c, int& x)
        {
            const auto o = oursAt (c);
            double best = 1.0e9;
            for (int half = bestX - 2; half <= bestX + 2; ++half)
            {
                double worst = 0.0;
                for (int k = 0; k < 3; ++k)
                {
                    const auto r = card (o, hisHalf (half), k, false);
                    worst = std::max ({ worst, r.band / 2.0, r.crestWorst / 0.5, r.harmWorst / 1.0 });
                }
                if (worst < best)
                {
                    best = worst;
                    x = half;
                }
            }
            return best;
        };
        auto best = v;
        double bestScore = score (v, bestX);
        const auto tryOne = [&] (DrivePedal::Voicing c)
        {
            int x = bestX;
            if (const double sc = score (c, x); sc < bestScore)
            {
                bestScore = sc;
                best = c;
                bestX = x;
            }
        };
        // Coordinate passes, coarse to fine: the corners and the shelf, then the leg's law — the scale held.
        for (int pass = 0; pass < 3; ++pass)
        {
            const double k = pass == 0 ? 1.0 : pass == 1 ? 0.5 : 0.25;
            for (const double hz : { 0.7, 0.85, 1.2, 1.45 })
                { auto c = best; c.tsPostHz = best.tsPostHz * std::pow (hz, k); tryOne (c); }
            for (const double db : { -3.0, -1.5, 1.5, 3.0 })
                { auto c = best; c.tsShelfNoonDb = best.tsShelfNoonDb + db * k; tryOne (c); }
            for (const double hz : { 0.6, 0.8, 1.25, 1.6 })
                { auto c = best; c.tsLpHz = best.tsLpHz * std::pow (hz, k); tryOne (c); }
            for (const double lo : { -6.0, -3.0, 3.0, 6.0 })
                { auto c = best; c.tsLegLoDb = best.tsLegLoDb + lo * k; tryOne (c); }
            for (const double hi : { -8.0, -4.0, 4.0, 8.0 })
                { auto c = best; c.tsLegHiDb = best.tsLegHiDb + hi * k; tryOne (c); }
            std::printf ("pass %d: worst %.2f of the tolerances at X %+.1f dB (post %.0f, shelf %+.2f, top %.0f, leg %.1f..%.1f)\n",
                         pass, bestScore, bestX * 0.5, best.tsPostHz, best.tsShelfNoonDb, best.tsLpHz, best.tsLegLoDb, best.tsLegHiDb);
        }
        std::printf ("\nHIS CAPTURES FED %+.1f dB\n", bestX * 0.5);
        cards (best, hisHalf (bestX), "THE MATCHED VOICING");
    }

    // The DI at its own level, fed X, through each of his three, for the blind pairs: <prefix>-60.wav, -150.wav, -240.wav.
    for (int k = 0; outPrefix.isNotEmpty() && k < 3; ++k)
    {
        std::vector<float> raw ((size_t) file.getNumSamples());
        for (size_t i = 0; i < raw.size(); ++i)
            raw[i] = file.getSample (0, (int) i);
        const auto out = throughNam (juce::File (argv[2 + k]), scaled (raw, namDb));
        juce::AudioBuffer<float> b (1, (int) out.size());
        b.copyFrom (0, 0, out.data(), (int) out.size());
        static const char* const hours[3] = { "-60.wav", "-150.wav", "-240.wav" };
        juce::File dst (outPrefix + hours[k]);
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
