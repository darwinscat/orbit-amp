// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// The generator's plumbing: an FFT, a magnitude made minimum-phase, a 24-bit WAV, a "[section] key = value" table.
#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace gen
{
using cd = std::complex<double>;
constexpr double pi = 3.14159265358979323846;

/** In place, n a power of two; inverse scales by 1/n. */
inline void fft (std::vector<cd>& a, bool inverse)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = 2 * pi / (double) len * (inverse ? 1 : -1);
        const cd w (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            cd wk (1);
            for (size_t k = 0; k < len / 2; ++k, wk *= w)
            {
                const cd u = a[i + k], v = a[i + k + len / 2] * wk;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
            }
        }
    }
    if (inverse)
        for (auto& x : a) x /= (double) n;
}

/** The minimum-phase spectrum of a magnitude given on bins 0..n/2 (folded cepstrum). */
inline std::vector<cd> minimumPhase (const std::vector<double>& mag, size_t n)
{
    std::vector<cd> c (n);
    for (size_t k = 0; k < n; ++k)
    {
        const size_t b = k <= n / 2 ? k : n - k;
        c[k] = std::log (std::max (mag[b], 1e-9));
    }
    fft (c, true);
    for (size_t k = 1; k < n / 2; ++k) { c[k] *= 2.0; c[n - k] = 0.0; }
    fft (c, false);
    for (auto& x : c) x = std::exp (x);
    return c;
}

/** RBJ biquad's response at f (Hz), as a complex gain. */
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    cd at (double f, double fs) const
    {
        const cd z1 = std::polar (1.0, -2 * pi * f / fs), z2 = z1 * z1;
        return (b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2);
    }
    static Biquad make (const std::string& type, double f0, double q, double db, double fs)
    {
        const double A = std::pow (10.0, db / 40.0), w = 2 * pi * f0 / fs, cw = std::cos (w), sw = std::sin (w),
                     al = sw / (2 * q);
        double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
        if (type == "hp")      { b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = b0; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; }
        else if (type == "lp") { b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = b0; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; }
        else if (type == "peak")
        { b0 = 1 + al * A; b1 = -2 * cw; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * cw; a2 = 1 - al / A; }
        else if (type == "lowshelf" || type == "highshelf")
        {
            const double s = 2 * std::sqrt (A) * al, sgn = type == "lowshelf" ? 1 : -1;
            b0 = A * ((A + 1) - sgn * (A - 1) * cw + s);
            b1 = sgn * 2 * A * ((A - 1) - sgn * (A + 1) * cw);
            b2 = A * ((A + 1) - sgn * (A - 1) * cw - s);
            a0 = (A + 1) + sgn * (A - 1) * cw + s;
            a1 = -sgn * 2 * ((A - 1) + sgn * (A + 1) * cw);
            a2 = (A + 1) + sgn * (A - 1) * cw - s;
        }
        return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
    }
};

inline void writeWav24 (const std::string& path, const std::vector<double>& x, int fs)
{
    std::ofstream f (path, std::ios::binary);
    const uint32_t n = (uint32_t) x.size(), data = n * 3;
    auto u32 = [&] (uint32_t v) { f.put ((char) v); f.put ((char) (v >> 8)); f.put ((char) (v >> 16)); f.put ((char) (v >> 24)); };
    auto u16 = [&] (uint16_t v) { f.put ((char) v); f.put ((char) (v >> 8)); };
    f.write ("RIFF", 4); u32 (36 + data); f.write ("WAVEfmt ", 8); u32 (16); u16 (1); u16 (1); u32 ((uint32_t) fs);
    u32 ((uint32_t) fs * 3); u16 (3); u16 (24); f.write ("data", 4); u32 (data);
    for (double v : x)
    {
        const int32_t s = (int32_t) std::lround (std::clamp (v, -1.0, 1.0 - 1.0 / 8388608.0) * 8388608.0);
        f.put ((char) s); f.put ((char) (s >> 8)); f.put ((char) (s >> 16));
    }
}

/** "[name]" opens a section; "key = value" lines fill it; '#' starts a comment. */
using Section = std::map<std::string, std::string>;
inline std::vector<std::pair<std::string, Section>> readTable (const std::string& path)
{
    std::vector<std::pair<std::string, Section>> out;
    std::ifstream f (path);
    std::string line;
    const auto trim = [] (std::string s) {
        const auto a = s.find_first_not_of (" \t\r"), b = s.find_last_not_of (" \t\r");
        return a == std::string::npos ? std::string() : s.substr (a, b - a + 1);
    };
    while (std::getline (f, line))
    {
        line = trim (line.substr (0, line.find ('#')));
        if (line.empty()) continue;
        if (line.front() == '[' && line.back() == ']') { out.push_back ({ trim (line.substr (1, line.size() - 2)), {} }); continue; }
        const auto eq = line.find ('=');
        if (eq == std::string::npos || out.empty()) continue;
        out.back().second[trim (line.substr (0, eq))] = trim (line.substr (eq + 1));
    }
    return out;
}

inline double num (const Section& s, const std::string& k, double fallback)
{
    const auto it = s.find (k);
    return it == s.end() || it->second.empty() ? fallback : std::atof (it->second.c_str());
}
}
