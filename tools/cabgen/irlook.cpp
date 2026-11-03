// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// An IR's response in third octaves, 31.5 Hz .. 16 kHz, in dB against its own 500 Hz-2 kHz average; and how ragged
// its top is (the spread of 1/24-octave levels around their 1/3-octave trend, 1.5-6 kHz).
//   irlook <ir.wav> ...
#include "dsp.h"

#include <cstring>

using namespace gen;

static bool readWav (const char* path, std::vector<double>& out, double& sr)
{
    std::ifstream f (path, std::ios::binary);
    std::vector<unsigned char> b ((std::istreambuf_iterator<char> (f)), {});
    if (b.size() < 12 || std::memcmp (b.data(), "RIFF", 4)) return false;
    auto u16 = [&] (size_t p) { return (unsigned) b[p] | (unsigned) b[p + 1] << 8; };
    auto u32 = [&] (size_t p) { return u16 (p) | u16 (p + 2) << 16; };
    int ch = 1, bits = 16, fmt = 1;
    for (size_t p = 12; p + 8 <= b.size();)
    {
        const unsigned len = u32 (p + 4);
        if (! std::memcmp (b.data() + p, "fmt ", 4)) { fmt = (int) u16 (p + 8); ch = (int) u16 (p + 10); sr = u32 (p + 12); bits = (int) u16 (p + 22); if (fmt == 0xFFFE) fmt = (int) u16 (p + 32); }
        else if (! std::memcmp (b.data() + p, "data", 4))
        {
            const int bps = bits / 8;
            const size_t n = std::min ((size_t) len, b.size() - p - 8) / (size_t) (bps * ch);
            for (size_t i = 0; i < n; ++i)
            {
                const unsigned char* s = b.data() + p + 8 + i * (size_t) (bps * ch);
                double v = 0;
                if (fmt == 3 && bits == 32) { float x; std::memcpy (&x, s, 4); v = x; }
                else if (bits == 24) { int x = s[0] | s[1] << 8 | s[2] << 16; if (x & 0x800000) x -= 1 << 24; v = x / 8388608.0; }
                else if (bits == 16) { v = (int16_t) (s[0] | s[1] << 8) / 32768.0; }
                else if (bits == 32) { int32_t x; std::memcpy (&x, s, 4); v = x / 2147483648.0; }
                out.push_back (v);
            }
            return true;
        }
        p += 8 + len + (len & 1);
    }
    return false;
}

int main (int argc, char** argv)
{
    const double bands[] { 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600,
                           2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000 };
    std::printf ("%-34s", "Hz");
    for (double b : bands) std::printf ("%5s", b < 1000 ? std::to_string ((int) b).c_str() : (std::to_string ((int) (b / 100) / 10.0).substr (0, 4) + "k").c_str());
    std::printf ("  rag\n");
    for (int a = 1; a < argc; ++a)
    {
        std::vector<double> x; double sr = 48000;
        if (! readWav (argv[a], x, sr)) { std::printf ("%s: unreadable\n", argv[a]); continue; }
        const size_t n = 1 << 16;
        std::vector<cd> X (n, 0.0);
        for (size_t i = 0; i < x.size() && i < n; ++i) X[i] = x[i];
        fft (X, false);
        const auto level = [&] (double lo, double hi) {
            double e = 0; int c = 0;
            for (size_t k = (size_t) (lo * n / sr); k <= (size_t) (hi * n / sr) && k < n / 2; ++k) { e += std::norm (X[k]); ++c; }
            return 10 * std::log10 (std::max (e / std::max (c, 1), 1e-30));
        };
        const double ref = level (500, 2000);
        std::string name = argv[a];
        name = name.substr (name.find_last_of ('/') + 1);
        if (name.size() > 33) name = name.substr (0, 33);
        std::printf ("%-34s", name.c_str());
        for (double b : bands) std::printf ("%5.0f", level (b / std::pow (2, 1.0 / 6), b * std::pow (2, 1.0 / 6)) - ref);
        // Raggedness: 1/24-octave levels against the 1/3-octave level they sit in, 1.5-6 kHz.
        double s2 = 0; int m = 0;
        for (double f = 1500; f < 6000; f *= std::pow (2, 1.0 / 24))
        {
            const double fine = level (f / std::pow (2, 1.0 / 48), f * std::pow (2, 1.0 / 48));
            const double wide = level (f / std::pow (2, 1.0 / 6), f * std::pow (2, 1.0 / 6));
            s2 += (fine - wide) * (fine - wide); ++m;
        }
        std::printf ("  %4.1f\n", std::sqrt (s2 / m));
    }
}
