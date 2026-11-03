// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// CABGEN <table.txt> <out dir> [section ...] — every section of the table (none named: every one that names drivers)
// rendered to a file by src/core/CabSynth.h: 48 kHz, 24-bit, mono, 200 ms, the set at one gain (the player levels each
// IR itself). The bass's cabinets are tools/cabgen/cabinets.txt. Built by hand, no JUCE:
//   clang++ -std=c++20 -O2 -o cabgen tools/cabgen/cabgen.cpp && ./cabgen tools/cabgen/cabinets.txt <out dir>
// irlook <ir.wav> ... reads them back: third-octave levels against the 500 Hz-2 kHz band, and how ragged the top is.

#include "../../src/core/CabSynth.h"

#include <filesystem>

using namespace orbitamp::core::cabsynth;

namespace
{
constexpr size_t N = 1 << 15;
}

int main (int argc, char** argv)
{
    if (argc < 3) { std::fprintf (stderr, "cabgen <table.txt> <out dir> [section ...]\n"); return 2; }
    const auto all = readTable (argv[1]);
    std::vector<std::string> wanted;
    for (int i = 3; i < argc; ++i) wanted.push_back (argv[i]);
    if (wanted.empty())
        for (auto& [n, s] : all)
            if (resolve (all, n).count ("driver_xy")) wanted.push_back (n);

    std::filesystem::create_directories (argv[2]);
    std::vector<std::pair<std::string, std::vector<double>>> made;
    double ref = 0;
    for (auto& name : wanted)
    {
        const auto s = resolve (all, name);
        auto ir = render (build (s), N);
        const double l = loudness (ir, N);
        if (ref == 0) ref = l;
        for (auto& v : ir) v *= ref / l;   // every one as loud as the first under the programme
        made.push_back ({ str (s, "file", name), ir });
    }
    double peak = 0;
    for (auto& [n, ir] : made) for (double v : ir) peak = std::max (peak, std::abs (v));
    for (auto& [n, ir] : made)
    {
        for (auto& v : ir) v *= 0.5 / peak;   // one gain for the set: the loudest peak at -6 dBFS
        std::string file = n;
        for (auto& ch : file) if (ch == '/') ch = '-';
        writeWav24 (std::string (argv[2]) + "/" + file + ".wav", ir, (int) fs);
        std::printf ("%s\n", file.c_str());
    }
    return 0;
}
