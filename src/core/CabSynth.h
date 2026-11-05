// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

// CABSYNTH — a close-miked bass cabinet, synthesized: every cone a surface of patches, each moving as a piston below its
// breakup and as a sum of seeded modes above it, summed at the mic with its true path delays; the box's low end (sealed,
// vented, cone and port apart, or open at the back, the cones' backs wrapping round), the horn on its crossover, the
// baffle's edges, the box's cavity and panel modes, the
// mic's capsule, pattern and proximity. Frequency domain, one complex sum per bin, then one inverse FFT.
//
// The cabinets are a table, "[section] key = value" with `inherit` (tools/cabgen/cabinets.txt): the tool renders every
// section of it to files (tools/cabgen), the amp renders one cabinet with the mic wherever the player put it (renderAt).
// JUCE-free, on its way to felitronics-core.

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace orbitamp::core::cabsynth
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
using Table = std::vector<std::pair<std::string, Section>>;

/** The table out of its text. */
inline Table parseTable (const std::string& text)
{
    Table out;
    std::istringstream f (text);
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

inline Table readTable (const std::string& path)
{
    std::ifstream f (path);
    std::stringstream all;
    all << f.rdbuf();
    return parseTable (all.str());
}

inline double num (const Section& s, const std::string& k, double fallback)
{
    const auto it = s.find (k);
    return it == s.end() || it->second.empty() ? fallback : std::atof (it->second.c_str());
}

constexpr double fs = 48000.0, c0 = 343.0;

struct Xy { double x, y; };

inline std::vector<double> nums (const std::string& s)
{
    std::vector<double> v;
    std::string cur;
    for (char ch : s + ",")
    {
        if (ch == ',' || ch == '/' || ch == ' ')
        {
            if (! cur.empty()) v.push_back (std::atof (cur.c_str()));
            cur.clear();
        }
        else cur += ch;
    }
    return v;
}

/** "a/b/c, d/e/f" — tuples of three. */
inline std::vector<std::array<double, 3>> triples (const Section& s, const std::string& k)
{
    std::vector<std::array<double, 3>> out;
    const auto it = s.find (k);
    if (it == s.end() || it->second == "none") return out;
    const auto v = nums (it->second);
    for (size_t i = 0; i + 2 < v.size(); i += 3) out.push_back ({ v[i], v[i + 1], v[i + 2] });
    return out;
}

inline std::vector<Xy> points (const Section& s, const std::string& k)
{
    std::vector<Xy> out;
    const auto it = s.find (k);
    if (it == s.end()) return out;
    const auto v = nums (it->second);
    for (size_t i = 0; i + 1 < v.size(); i += 2) out.push_back ({ v[i], v[i + 1] });
    return out;
}

inline std::pair<double, double> range (const Section& s, const std::string& k, double lo, double hi)
{
    const auto it = s.find (k);
    if (it == s.end()) return { lo, hi };
    const auto dots = it->second.find ("..");
    if (dots == std::string::npos) { const double v = std::atof (it->second.c_str()); return { v, v }; }
    return { std::atof (it->second.substr (0, dots).c_str()), std::atof (it->second.substr (dots + 2).c_str()) };
}

inline std::string str (const Section& s, const std::string& k, const std::string& fallback = {})
{
    const auto it = s.find (k);
    return it == s.end() ? fallback : it->second;
}

/** A section with what it inherits laid under it, left to right, recursively. */
inline Section resolve (const std::vector<std::pair<std::string, Section>>& all, const std::string& name, int depth = 0)
{
    Section out;
    for (const auto& [n, s] : all)
        if (n == name)
        {
            const auto inh = str (s, "inherit");
            std::string cur;
            for (char ch : inh + ",")
            {
                if (ch == ',')
                {
                    const auto a = cur.find_first_not_of (' '), b = cur.find_last_not_of (' ');
                    if (a != std::string::npos && depth < 8)
                        for (auto& [k, v] : resolve (all, cur.substr (a, b - a + 1), depth + 1)) out[k] = v;
                    cur.clear();
                }
                else cur += ch;
            }
            for (auto& [k, v] : s) out[k] = v;
        }
    return out;
}

/** A unit-peak resonance at f0: the band-pass s(w0/Q) / (s^2 + s w0/Q + w0^2). */
inline cd resonance (double f, double f0, double q)
{
    const cd s (0, f / f0);
    return s / q / (s * s + s / q + 1.0);
}

inline cd highpass2 (double f, double f0, double q) { const cd s (0, f / f0); return s * s / (s * s + s / q + 1.0); }
inline cd lowpass2 (double f, double f0, double q)  { const cd s (0, f / f0); return 1.0 / (s * s + s / q + 1.0); }
inline cd lowpass1 (double f, double f0) { return 1.0 / cd (1, f / f0); }
inline cd highpass1 (double f, double f0) { const cd s (0, f / f0); return s / (1.0 + s); }

struct Mode { double f, q, residue; int radial, azimuthal; double phase, psi, eps; };

struct Cab
{
    // geometry
    std::vector<Xy> drivers;
    double radius = 0.105, depthFrac = 0.25, bw = 0.6, bh = 0.6;
    // low end
    bool vented = false;
    // Open at the back: each cone in free air, its back heard inverted round the box's side, dark above `openLp`, by
    // `openWrap` of it — whole: a box this small does not shadow a bass's wavelength.
    bool open = false;
    double openLp = 500, openWrap = 1.0;
    double fsd = 50, qts = 0.4, vas = 30, vb = 60, perChamber = 1, chambers = 1, fb = 45, ql = 8;
    bool portRear = false, hornAtMic = false;
    double fixedMass = 0, coilHz = 0, proxStrength = 1, proxCorner = 0, micTop = 12000, depth = 0.4;
    bool figure8 = false;   // a ribbon's: front and back alike, the back in the other polarity
    Xy port {};
    double portArea = 150, portLeakHz = 700;
    // cone
    double wooferLp = 5000, wooferQ = 0.707, decoupleHz = 1200;
    std::vector<std::vector<Mode>> modes;   // per driver
    std::vector<double> sens;               // per driver, linear
    // box
    std::vector<std::array<double, 3>> body, cavity, panel;
    // horn
    bool horn = false;
    Xy hornXy {};
    double hornXo = 4000, hornQ = 0.707, hornDb = 0, hornLp = 17000, hornDepth = 0.04;
    std::vector<std::array<double, 3>> hornEq;
    // diffraction
    double diffDb = -24, diffLp = 4000;
    // mic
    double micHp = 35, micHpQ = 0.707, micLp = 12000, proxHz = 140, prox50 = 2.5, proxMax = 6, offLoss = 4;
    std::vector<std::array<double, 3>> presence;
    double micD = 0.05, micOff = 0, micOffY = 0, tilt = 0;
    // Where the table's horn level (horn_at_mic_db) was taken: its section's own mic. A live mic (cabAt) moves off it,
    // and the horn's level follows the mic's real distance and angle to the horn from there.
    double hornRefD = 0.05, hornRefOff = 0, hornRefTilt = 0;
    double baffleStepDb = 0.5;
    // The floor the box stands on, floorGap under its bottom: what of a source's image under it comes back (0, no floor).
    double floorGap = 0, floorRefl = 0;
    double lengthMs = 200, fadeMs = 30;
    double refDelay = -1;   // the time zero, s from the sound leaving: below zero, the first arrival's own
};

inline Cab build (const Section& s)
{
    Cab c;
    c.drivers = points (s, "driver_xy");
    c.radius = num (s, "effective_radius_m", 0.105);
    c.depthFrac = num (s, "cone_depth_fraction_of_radius", 0.25);
    const auto baffle = nums (str (s, "baffle", "0.6,0.6,0.4"));
    c.bw = baffle.size() > 0 ? baffle[0] : 0.6;
    c.bh = baffle.size() > 1 ? baffle[1] : 0.6;

    c.vented = str (s, "alignment") == "ported";
    c.open = str (s, "alignment") == "open";
    c.openLp = num (s, "open_back_lowpass_hz", 500);
    c.openWrap = num (s, "open_back_wrap", 1.0);
    const auto ts = nums (str (s, "ts", "50,0.4,30"));
    c.fsd = ts[0]; c.qts = ts[1]; c.vas = ts[2];
    const auto ch = nums (str (s, "chamber", "60,1,1"));
    c.vb = ch[0]; c.perChamber = ch.size() > 1 ? ch[1] : 1; c.chambers = ch.size() > 2 ? ch[2] : 1;
    c.depth = baffle.size() > 2 ? baffle[2] : 0.4;
    c.portRear = str (s, "port_face") == "rear";
    c.fixedMass = num (s, "cone_fixed_mass_fraction", 0);
    c.coilHz = num (s, "coil_corner_hz", 0);
    c.proxStrength = num (s, "proximity_strength", 1);
    c.proxCorner = num (s, "proximity_corner_hz", 0);
    c.figure8 = str (s, "mic_pattern") == "figure8";
    c.micTop = num (s, "mic_top_corner_hz", 0);
    c.hornAtMic = s.count ("horn_at_mic_db") > 0;
    c.fb = num (s, "fb_hz", 45);
    c.ql = num (s, "port_loss_Q", 8);
    const auto ports = points (s, "equivalent_port_xy");
    if (! ports.empty()) c.port = ports[0];
    c.portArea = num (s, "equivalent_port_area_cm2", 150);
    c.portLeakHz = num (s, "port_high_frequency_leakage_lowpass_hz", 700);
    if (str (s, "port_face") == "rear") c.portArea *= 0.35;   // heard round the box: weaker at the front

    c.wooferLp = num (s, "woofer_lowpass_hz", 5000);
    c.wooferQ = num (s, "woofer_envelope_Q", 0.707);
    c.decoupleHz = num (s, "cone_decouple_hz", range (s, "breakup_hz", 1200, 7000).first);
    c.body = triples (s, "body_eq");
    c.cavity = triples (s, "cavity_modes");
    if (c.cavity.empty()) c.cavity = triples (s, "cavity_modes_per_chamber");
    c.panel = triples (s, "panel_modes");

    c.horn = str (s, "horn", "present") != "absent" && ! points (s, "horn_xy").empty();
    if (c.horn) c.hornXy = points (s, "horn_xy")[0];
    c.hornXo = num (s, "horn_crossover_hz", 4000);
    c.hornQ = num (s, "horn_filter_Q", 0.707);
    c.hornDb = c.hornAtMic ? num (s, "horn_at_mic_db", -10)
                           : num (s, "horn_sensitivity_relative_one_woofer_db", 6) + num (s, "horn_attenuator_db", -6);
    c.hornLp = num (s, "horn_lowpass_hz", 17000);
    c.hornDepth = num (s, "horn_extra_acoustic_depth_m", 0.04);
    c.hornEq = triples (s, "horn_response_eq");

    c.diffDb = num (s, "diffraction_total_amplitude_db", -24);
    c.diffLp = num (s, "diffraction_lowpass_hz", 4000);

    c.micHp = num (s, "mic_highpass_hz", 35);
    c.micLp = num (s, "mic_lowpass_hz", 12000);
    c.presence = triples (s, "mic_presence_eq");
    c.proxHz = num (s, "proximity_shelf_hz", 140);
    c.prox50 = num (s, "proximity_gain_at_50mm_db", 2.5);
    c.proxMax = num (s, "proximity_max_db", 6);
    c.offLoss = num (s, "mic_extra_loss_at_6k_45deg_db", 4);
    c.micD = num (s, "mic_distance_m", num (s, "mic_distance_from_cap_plane_m", 0.05));
    c.micOff = num (s, "mic_radial_offset_fraction", 0);
    c.tilt = num (s, "mic_axis_tilt_deg", 0);
    c.hornRefD = c.micD;
    c.hornRefOff = c.micOff;
    c.hornRefTilt = c.tilt;
    c.baffleStepDb = num (s, "baffle_step_depth_db", 0.5);
    c.floorGap = num (s, "floor_gap_m", 0);
    c.floorRefl = num (s, "floor_reflection", 0);
    c.lengthMs = num (s, "length_ms", 200);
    c.fadeMs = num (s, "tail_fade_ms", 30);

    // The modes: seeded per cabinet, the same for every position (a position changes their weights, not them).
    std::mt19937 rng ((unsigned) num (s, "seed", 1));
    std::uniform_real_distribution<double> u (0, 1);
    const auto [b0, b1] = range (s, "breakup_hz", 1200, 7000);
    const auto [q0, q1] = range (s, "mode_Q", 3, 9);
    const auto [r0, r1] = range (s, "mode_residue_abs", 0.08, 0.4);
    const int count = (int) num (s, "mode_count", 16);
    const double gain = num (s, "mode_gain", 1);
    const double jitter = num (s, "mode_spacing_jitter_fraction", 0.2);
    const bool relative = s.count ("mode_level_rel_piston_db_first3") > 0;
    const auto first3 = range (s, "mode_level_rel_piston_db_first3", 4, 8), rest = range (s, "mode_level_rel_piston_db_rest", -2, 4);
    const double axisym = num (s, "mode_axisymmetric_share", 0.25), eps = num (s, "mode_asymmetry_eps", 0);
    const auto [ds0, ds1] = range (s, "driver_mode_frequency_spread_percent", -1.5, 1.5);
    const auto [dr0, dr1] = range (s, "driver_mode_residue_spread_db", -1, 1);
    const auto [dv0, dv1] = range (s, "driver_sensitivity_spread_db", -0.5, 0.5);
    std::vector<Mode> base;
    const double step = std::log (b1 / b0) / std::max (1, count - 1);
    for (int m = 0; m < count; ++m)
    {
        Mode md;
        md.f = b0 * std::exp (step * (m + jitter * (2 * u (rng) - 1)));
        md.q = q0 * std::pow (q1 / q0, u (rng));
        md.residue = gain * (r0 + (r1 - r0) * u (rng)) * (u (rng) < 0.5 ? -1 : 1);
        md.radial = (int) (u (rng) * 6.999);
        md.azimuthal = (int) (u (rng) * 3.999);
        md.phase = 2 * pi * u (rng);
        md.psi = pi * u (rng);
        md.eps = 0;
        if (relative)
        {
            // A level against the piston at its peak, the first three the loudest; now and then a dip instead.
            const auto [l0, l1] = m < 3 ? first3 : rest;
            const double lv = l0 + (l1 - l0) * u (rng);
            md.residue = std::pow (10.0, lv / 20) - 1;
            if (lv > 0 && u (rng) < 0.3) md.residue = -md.residue;
            md.azimuthal = u (rng) < axisym ? 0 : 1 + (int) (u (rng) * 2.999);
            md.eps = eps * (u (rng) < 0.5 ? -1 : 1);
        }
        base.push_back (md);
    }
    for (size_t d = 0; d < c.drivers.size(); ++d)
    {
        auto mine = base;
        for (auto& md : mine)
        {
            md.f *= 1 + 0.01 * (ds0 + (ds1 - ds0) * u (rng));
            md.residue *= std::pow (10.0, (dr0 + (dr1 - dr0) * u (rng)) / 20);
        }
        c.modes.push_back (mine);
        c.sens.push_back (std::pow (10.0, (dv0 + (dv1 - dv0) * u (rng)) / 20));
    }
    return c;
}

/** The cone's and the port's shares of the box's low end, at f: sealed, the cone alone; vented, the two apart; open,
    the cone on no air but its own — its free-air resonance, Qts. */
inline void lowEnd (const Cab& c, double f, cd& cone, cd& port)
{
    const double alpha = c.perChamber * c.vas / c.vb;
    const cd s (0, 2 * pi * f);
    if (c.open)
    {
        const double ws = 2 * pi * c.fsd;
        cone = s * s / (s * s + s * ws / c.qts + ws * ws);
        port = 0;
        return;
    }
    if (! c.vented)
    {
        const double wc = 2 * pi * c.fsd * std::sqrt (1 + alpha), qtc = c.qts * std::sqrt (1 + alpha);
        cone = s * s / (s * s + s * wc / qtc + wc * wc);
        port = 0;
        return;
    }
    const double ws = 2 * pi * c.fsd, wb = 2 * pi * c.fb, h = c.fb / c.fsd, w0 = std::sqrt (ws * wb);
    const double ql = c.ql, qt = c.qts;
    const double a1 = (ql + h * qt) / (std::sqrt (h) * ql * qt);
    const double a2 = (h + (alpha + 1 + h * h) * ql * qt) / (h * ql * qt);
    const double a3 = (h * ql + qt) / (std::sqrt (h) * ql * qt);
    const cd d = s * s * s * s + a1 * w0 * s * s * s + a2 * w0 * w0 * s * s + a3 * w0 * w0 * w0 * s + w0 * w0 * w0 * w0;
    cone = s * s * (s * s + s * wb / ql + wb * wb) / d;
    port = -s * s * (s * wb / ql + wb * wb) / d;
}

struct Mic { double x, y, z, ax, ay, az; };

/** What the mic makes of a source at (dx,dy,dz) from it, at f: a cardioid, half pressure and half gradient — the
    gradient's 1/(jkr) its proximity, as strong as the mic is (proximity_strength), at the source's own distance — and the
    capsule's extra loss off its axis at the top (a dynamic's measured polar: 60 deg -2 dB @1k -5 @8k, 90 deg -6/-10). */
inline cd heard (const Cab& c, const Mic& m, double dx, double dy, double dz, double f)
{
    const double len = std::sqrt (dx * dx + dy * dy + dz * dz);
    const double cosT = std::clamp ((dx * m.ax + dy * m.ay + dz * m.az) / len, -1.0, 1.0);   // the axis points at the cab: in front, +1
    const double theta = std::acos (cosT) * 180 / pi;
    const double kr = 2 * pi * f / c0 * (len + 0.02);
    // Below its corner the proximity stops growing: there the capsule's own gradient path lets go.
    const cd prox = 0.55 * c.proxStrength / cd (0, kr) * (c.proxCorner > 0 ? highpass1 (f, c.proxCorner) : cd (1.0));
    if (c.figure8)   // all gradient: its proximity whole, no front-only loss at the top
        return cosT * (1.0 + prox);
    const cd pattern = 0.5 + 0.5 * cosT * (1.0 + prox);
    const double e = theta <= 60 ? 2.5 * theta / 60 : theta <= 90 ? 2.5 + 1.5 * (theta - 60) / 30 : 4 + 2 * (theta - 90) / 90;
    const double top = std::clamp (std::log2 (f / 1000) / 3, 0.0, 1.0);
    return pattern * std::pow (10.0, -e * top / 20);
}

inline std::vector<double> render (const Cab& c, size_t n)
{
    const size_t bins = n / 2 + 1;
    std::vector<cd> H (n, 0.0);

    // The mic, in front of the first cone, off its centre by a fraction of its radius, tilted toward +x.
    const Xy t = c.drivers.empty() ? Xy {} : c.drivers[0];
    const double tr = c.tilt * pi / 180;
    // Tilted toward the cap: off the cap, the axis leans back toward its centre; on it, toward -x.
    const double off = std::hypot (c.micOff, c.micOffY), ux = off > 1e-6 ? -c.micOff / off : -1.0, uy = off > 1e-6 ? -c.micOffY / off : 0.0;
    const Mic mic { t.x + c.micOff * c.radius, t.y + c.micOffY * c.radius, c.micD, std::sin (tr) * ux, std::sin (tr) * uy, -std::cos (tr) };

    // Each cone as patches: rings x spokes, the cone's depth behind the baffle growing toward its centre.
    constexpr int rings = 7, spokes = 16;
    struct Patch { double x, y, z, area, rho, phi; };
    std::vector<std::vector<Patch>> patches;
    for (const auto& d : c.drivers)
    {
        std::vector<Patch> ps;
        for (int r = 0; r < rings; ++r)
        {
            const double r0 = (double) r / rings, r1 = (double) (r + 1) / rings, rho = 0.5 * (r0 + r1);
            for (int k = 0; k < spokes; ++k)
            {
                const double phi = 2 * pi * (k + 0.5 * (r % 2)) / spokes;
                ps.push_back ({ d.x + rho * c.radius * std::cos (phi), d.y + rho * c.radius * std::sin (phi),
                                -c.depthFrac * c.radius * (1 - rho), (r1 * r1 - r0 * r0) / spokes, rho, phi });
            }
        }
        patches.push_back (ps);
    }

    // Each patch's path to the mic: its delay, its 1/r, the mic's pattern — and its modes' weights there.
    const auto pathTo = [&] (double x, double y, double z, double& r, double& dx, double& dy, double& dz) {
        dx = x - mic.x; dy = y - mic.y; dz = z - mic.z;
        r = std::sqrt (dx * dx + dy * dy + dz * dz);
    };

    // The floor: every cone, the port and the horn have their image under it, heard from below. Close to a cone the image
    // is far and faint; a hand's span back it combs the low mids, and the box's low end gains the floor.
    const double floorY = -c.bh / 2 - c.floorGap;
    const int images = c.floorRefl > 0 ? 2 : 1;
    const auto mirrored = [&] (int img, double y) { return img == 0 ? y : 2 * floorY - y; };

    // The four edges of the baffle, each a weak inverted source fed by the first cone, at the edge's nearest point.
    const Xy edges[4] { { -c.bw / 2, t.y }, { c.bw / 2, t.y }, { t.x, -c.bh / 2 }, { t.x, c.bh / 2 } };

    // Each mode's shape at each patch, once: a position weighs a mode, it does not move it.
    std::vector<std::vector<std::vector<double>>> shape (patches.size());
    for (size_t d = 0; d < patches.size(); ++d)
        for (const auto& md : c.modes[d])
        {
            std::vector<double> w;
            for (const auto& p : patches[d])
                w.push_back (std::cos (pi * md.radial * p.rho + md.psi)
                             * (md.azimuthal == 0 ? 1.0 : std::cos (md.azimuthal * p.phi + md.phase) + md.eps));
            shape[d].push_back (w);
        }

    double minDelay = 1e9;
    for (size_t d = 0; d < patches.size(); ++d)
        for (const auto& p : patches[d])
        {
            double r, dx, dy, dz;
            pathTo (p.x, p.y, p.z, r, dx, dy, dz);
            minDelay = std::min (minDelay, r / c0);
        }
    if (c.refDelay >= 0)
        minDelay = c.refDelay;   // two mics on one clock: the later one arrives late by its path

    // The coil's inductance: half a slope (-3 dB/oct) above its corner, minimum-phase — the woofer's alone.
    std::vector<cd> coil (n, 1.0);
    if (c.coilHz > 0)
    {
        std::vector<double> mag (n / 2 + 1);
        for (size_t k = 0; k <= n / 2; ++k) { const double f = fs * k / n; mag[k] = std::pow (1 + (f / c.coilHz) * (f / c.coilHz), -0.25); }
        coil = minimumPhase (mag, n);
    }
    // The target cone's own near-field sum, as the cones' sum is scaled below: what the port and the horn stand against.
    double near = 0;
    for (const auto& p : patches[0]) { double r, dx, dy, dz; pathTo (p.x, p.y, p.z, r, dx, dy, dz); near += p.area / r; }
    // The horn's level is the table's where the table's mic stood (hornRef*): the mic there, and the target cone's near sum
    // there — so a mic moved nearer the horn hears more of it, one moved away or back from the cone hears the balance shift.
    const double refTr = c.hornRefTilt * pi / 180, refUx = c.hornRefOff > 1e-6 ? -1.0 : c.hornRefOff < -1e-6 ? 1.0 : -1.0;
    const Mic micRef { t.x + c.hornRefOff * c.radius, t.y, c.hornRefD, std::sin (refTr) * refUx, 0.0, -std::cos (refTr) };
    double nearRef = 0;
    for (const auto& p : patches[0])
    {
        const double dx = p.x - micRef.x, dy = p.y - micRef.y, dz = p.z - micRef.z;
        nearRef += p.area / std::sqrt (dx * dx + dy * dy + dz * dz);
    }
    const double hdxR = c.hornXy.x - micRef.x, hdyR = c.hornXy.y - micRef.y, hdzR = -c.hornDepth - micRef.z;
    const double hrR = std::sqrt (hdxR * hdxR + hdyR * hdyR + hdzR * hdzR);
    // The port against the cone at a cone-close mic (Keele's near field against the port's far field): n Sd / (2 pi r Delta).
    const double dc = c.micD + c.depthFrac * c.radius, delta = std::sqrt (dc * dc + c.radius * c.radius) - dc;
    std::vector<Biquad> bodyEq, presence, hornEq;
    for (auto& e : c.body) bodyEq.push_back (Biquad::make ("peak", e[0], e[1], e[2], fs));
    for (auto& e : c.presence) presence.push_back (Biquad::make ("peak", e[0], e[1], e[2], fs));
    for (auto& e : c.hornEq) hornEq.push_back (Biquad::make ("peak", e[0], e[1], e[2], fs));
    const double stepDb = c.baffleStepDb, stepHz = 115.0 / c.bw;

    for (size_t k = 1; k < bins; ++k)
    {
        const double f = fs * (double) k / (double) n;
        cd cone, port;
        lowEnd (c, f, cone, port);

        // What the box adds to every cone's motion: its cavity and panel modes, then the voicing.
        cd box = 1.0;
        if (! c.open)   // no air shut in behind an open back: nothing of the cavity rings
            for (auto& m : c.cavity) box += m[2] * resonance (f, m[0], m[1]);
        for (auto& m : c.panel) box += m[2] * resonance (f, m[0], m[1]);
        for (auto& b : bodyEq) box *= b.at (f, fs);
        // The baffle step: the low end, radiating into the whole room, a few tenths of a dB under the top.
        box *= std::pow (10.0, -stepDb / 20 * (1 / (1 + (f / stepHz) * (f / stepHz))));

        const cd piston = lowpass2 (f, c.wooferLp, c.wooferQ);
        cd sum = 0.0;
        for (size_t d = 0; d < patches.size(); ++d)
        {
            // Each mode's resonance at f, once per cone.
            std::vector<cd> res (c.modes[d].size());
            for (size_t m = 0; m < res.size(); ++m) res[m] = c.modes[d][m].residue * resonance (f, c.modes[d][m].f, c.modes[d][m].q);
            // The part of the cone still moving, as an area: what lets go hands its share to the centre, which the coil
            // drives the harder for carrying less — the cone's output kept, its radiation drawn in toward the cap.
            double whole = 0, moving = 0;
            for (const auto& p : patches[d])
            {
                const double let = p.rho * f / c.decoupleHz;
                whole += p.area;
                moving += p.area / (1 + let * let);
            }
            // The coil and the cap keep their mass as the outer cone lets go (cone_fixed_mass_fraction).
            const cd held = c.fixedMass > 0 ? piston / (c.fixedMass + (1 - c.fixedMass) * moving / whole) : piston * (whole / moving);
            cd drv = 0.0;
            for (size_t pi_ = 0; pi_ < patches[d].size(); ++pi_)
            {
                const auto& p = patches[d][pi_];
                // Above its breakup the cone stops moving whole: its outer part lets go first, the centre keeps on.
                const double let = p.rho * f / c.decoupleHz;
                // The modes ride on what the cone carries there: above its breakup they are most of what it does.
                cd v = 1.0 / (1 + let * let);
                for (size_t m = 0; m < res.size(); ++m)
                    v += res[m] * shape[d][m][pi_];
                v *= held;
                cd at = 0.0;
                for (int img = 0; img < images; ++img)
                {
                    double r, dx, dy, dz;
                    pathTo (p.x, mirrored (img, p.y), p.z, r, dx, dy, dz);
                    at += (img == 0 ? 1.0 : c.floorRefl) * heard (c, mic, dx, dy, dz, f) / r * std::polar (1.0, -2 * pi * f * (r / c0 - minDelay));
                }
                drv += v * p.area * at;
            }
            sum += drv * c.sens[d] * cone * box * coil[k];
        }
        // Normalise the cones' sum to the nearest cone's 1/r at its centre, so the port and the horn sit against it.
        double r0, dx0, dy0, dz0;
        pathTo (t.x, t.y, -c.depthFrac * c.radius, r0, dx0, dy0, dz0);
        sum *= r0 / 1.0;

        if (c.vented)
            for (int img = 0; img < images; ++img)
            {
                const double back = img == 0 ? 1.0 : c.floorRefl;
                double r, dx, dy, dz;
                pathTo (c.port.x, mirrored (img, c.port.y), 0, r, dx, dy, dz);
                if (c.fixedMass > 0)
                {
                    // Round the box when it is at the back: the path wraps over the depth and half the width, and less arrives.
                    const double wrap = c.portRear ? c.depth + 0.5 * c.bw : 0, rp = r + wrap;
                    const double ratio = c.perChamber * c.chambers * pi * c.radius * c.radius / (2 * pi * rp * delta) * (c.portRear ? 0.35 : 1);
                    sum += back * port * coil[k] * ratio * (r0 * near) * lowpass2 (f, c.portLeakHz, 0.707) * heard (c, mic, dx, dy, dz, f)
                         * std::polar (1.0, -2 * pi * f * (rp / c0 - minDelay));
                }
                else
                {
                    const double area = c.portArea / (pi * c.radius * c.radius * 1e4);   // against one cone's
                    sum += back * port * area * c.perChamber * lowpass2 (f, c.portLeakHz, 0.707) * heard (c, mic, dx, dy, dz, f) * (r0 / r)
                         * std::polar (1.0, -2 * pi * f * (r / c0 - minDelay));
                }
            }
        // An open back: each cone's back, inverted, out of the back and round the box to the front — from the cone to the
        // back's rim on a side, forward along that side, then from the baffle's edge to the mic, by the side that makes it
        // shortest — by openWrap of it and dark (openLp); against the cone's near field as the port is (Keele).
        if (c.open)
            for (size_t d = 0; d < c.drivers.size(); ++d)
            {
                const auto& o = c.drivers[d];
                const Xy sides[4] { { -c.bw / 2, o.y }, { c.bw / 2, o.y }, { o.x, -c.bh / 2 }, { o.x, c.bh / 2 } };
                double rp = 1e9, ex = 0, ey = 0, ez = 0;
                for (const auto& e : sides)
                {
                    double r, dx, dy, dz;
                    pathTo (e.x, e.y, 0, r, dx, dy, dz);
                    const double way = std::hypot (c.depth, std::hypot (e.x - o.x, e.y - o.y)) + c.depth + r;
                    if (way < rp) { rp = way; ex = dx; ey = dy; ez = dz; }
                }
                const double ratio = pi * c.radius * c.radius / (2 * pi * rp * delta) * c.openWrap;
                sum -= cone * coil[k] * c.sens[d] * ratio * (r0 * near) * lowpass2 (f, c.openLp, 0.707) * heard (c, mic, ex, ey, ez, f)
                     * std::polar (1.0, -2 * pi * f * (rp / c0 - minDelay));
            }
        if (c.horn)
        {
            cd hn = highpass2 (f, c.hornXo, c.hornQ) * lowpass2 (f, c.hornLp, 0.707) * std::pow (10.0, c.hornDb / 20);
            for (auto& b : hornEq) hn *= b.at (f, fs);
            for (int img = 0; img < images; ++img)
            {
                const double back = img == 0 ? 1.0 : c.floorRefl;
                double r, dx, dy, dz;
                pathTo (c.hornXy.x, mirrored (img, c.hornXy.y), -c.hornDepth, r, dx, dy, dz);
                if (c.hornAtMic)   // its level against the cone's pass band where the table's mic stood, then as this mic hears it from there
                {
                    const double there = std::abs (heard (c, micRef, hdxR, hdyR, hdzR, f)) / hrR;
                    const double here = std::abs (heard (c, mic, dx, dy, dz, f)) / r;
                    sum += back * hn * (r0 * nearRef) * (here / there) * std::polar (1.0, -2 * pi * f * (r / c0 - minDelay));
                }
                else
                    sum += back * hn * heard (c, mic, dx, dy, dz, f) * (r0 / r) * std::polar (1.0, -2 * pi * f * (r / c0 - minDelay));
            }
        }
        // The baffle's edges: what the first cone sends along the baffle, inverted, coming back late and dark.
        {
            const double each = std::pow (10.0, c.diffDb / 20) / 4;
            for (const auto& e : edges)
            {
                const double toEdge = std::hypot (e.x - t.x, e.y - t.y);
                double r, dx, dy, dz;
                pathTo (e.x, e.y, 0, r, dx, dy, dz);
                sum -= each * cone * piston * coil[k] * lowpass2 (f, c.diffLp, 0.707) * heard (c, mic, dx, dy, dz, f)
                     * std::polar (1.0, -2 * pi * f * ((toEdge + r) / c0 - minDelay));
            }
        }

        // The mic: its capsule's own response, and the proximity it hears at this distance.
        cd capsule = highpass2 (f, c.micHp, 0.707) * (c.micTop > 0 ? lowpass1 (f, c.micTop) : lowpass2 (f, c.micLp, 0.707));
        for (auto& b : presence) capsule *= b.at (f, fs);
        H[k] = sum * capsule;
    }
    for (size_t k = 1; k < n / 2; ++k) H[n - k] = std::conj (H[k]);
    H[0] = 0;
    fft (H, true);

    const int len = (int) std::lround (c.lengthMs * fs / 1000), fade = (int) std::lround (c.fadeMs * fs / 1000);
    std::vector<double> ir (len);
    for (int i = 0; i < len; ++i)
    {
        double w = 1;
        if (i >= len - fade) w = 0.5 + 0.5 * std::cos (pi * (i - (len - fade)) / fade);
        ir[i] = H[i].real() * w;
    }
    return ir;
}

/** Loudness: the IR's energy under a bass programme's spectrum (pink, 40 Hz .. 5 kHz), as a gain to a common level. */
inline double loudness (const std::vector<double>& ir, size_t n)
{
    std::vector<cd> X (n, 0.0);
    for (size_t i = 0; i < ir.size() && i < n; ++i) X[i] = ir[i];
    fft (X, false);
    double e = 0;
    for (size_t k = 1; k < n / 2; ++k)
    {
        const double f = fs * k / n;
        if (f < 40 || f > 5000) continue;
        e += std::norm (X[k]) / f;
    }
    return std::sqrt (e);
}

/** WHERE THE MIC STANDS, for a cabinet rendered live: across the first speaker in its radii from the cap (x right,
    y up), and how far in front of the cap's plane; the horn on or off where the box has one, and its attenuator; and
    the box's back, as the table has it or as the player turns it. */
struct Spot
{
    double x = 0, y = 0, distance = 0.05;
    bool horn = true;
    std::string mic;    // a "mic-<name>" section of the table laid over the cabinet's own, or empty for its own
    double angle = 0;   // degrees the mic leans back toward the cap
    double hornDb = 0;  // the horn's level against the table's, its attenuator's step: 0, -6, -12
    int back = -1;      // -1 the table's; 0 closed, 1 ported, 2 open (Back)
};

/** THE BOX'S BACK, turned: closed, the cone on the air inside alone; ported, the port tuned as the table tunes it —
    or, on a box the table seals, at fb_hz's own 45 Hz in the middle of the baffle's foot (portAtFoot over its floor);
    open, each cone in free air, its back wrapping round the box to the mic. */
enum Back { closedBack = 0, portedBack = 1, openBack = 2 };
constexpr double portAtFoot = 0.045;

/** A cabinet of the table — its "<name> cap" section, the box as that section says, its mic or the one `spot` names
    laid over it — with the mic at `spot`, aimed straight at the baffle, its back as `spot` turns it. */
inline Cab cabAt (const Table& table, const std::string& cab, const Spot& spot)
{
    auto s = resolve (table, cab + " cap");
    if (! spot.mic.empty())
        for (auto& [k, v] : resolve (table, "mic-" + spot.mic))
            s[k] = v;
    auto c = build (s);
    c.micOff  = spot.x;
    c.micOffY = spot.y;
    c.micD    = spot.distance;
    c.tilt    = spot.angle;
    c.horn    = c.horn && spot.horn;
    c.hornDb += spot.hornDb;
    if (spot.back == closedBack || spot.back == openBack)
    {
        c.vented = false;
        c.open   = spot.back == openBack;
    }
    else if (spot.back == portedBack && ! c.vented)
    {
        c.vented   = true;
        c.open     = false;
        c.portRear = false;
        if (s.count ("equivalent_port_xy") == 0)
            c.port = { 0, -c.bh / 2 + portAtFoot };
    }
    return c;
}

/** When the first sound reaches the mic: the nearest patch of the nearest cone, s. */
inline double firstArrival (const Cab& c)
{
    if (c.drivers.empty()) return 0;
    const Xy t = c.drivers[0];
    const double mx = t.x + c.micOff * c.radius, my = t.y + c.micOffY * c.radius;
    double best = 1e9;
    for (const auto& d : c.drivers)
        for (int r = 0; r < 7; ++r)
            for (int k = 0; k < 16; ++k)
            {
                const double rho = (r + 0.5) / 7, phi = 2 * pi * (k + 0.5 * (r % 2)) / 16;
                const double x = d.x + rho * c.radius * std::cos (phi), y = d.y + rho * c.radius * std::sin (phi);
                const double z = -c.depthFrac * c.radius * (1 - rho);
                best = std::min (best, std::sqrt ((x - mx) * (x - mx) + (y - my) * (y - my) + (z - c.micD) * (z - c.micD)) / c0);
            }
    return best;
}

inline std::vector<float> peakAt (const std::vector<double>& ir, double to)
{
    double peak = 1e-12;
    for (double v : ir) peak = std::max (peak, std::abs (v));
    std::vector<float> out (ir.size());
    for (size_t i = 0; i < ir.size(); ++i) out[i] = (float) (ir[i] * to / peak);
    return out;
}

/** The cabinet with the mic at `spot`: the IR at 48 kHz, its peak at -6 dBFS (a player levels each IR itself). Any
    thread; a tenth of a second or so at the default FFT. */
inline std::vector<float> renderAt (const Table& table, const std::string& cab, const Spot& spot, size_t n = 1 << 14)
{
    return peakAt (render (cabAt (table, cab, spot), n), 0.5);
}

/** TWO MICS on one cabinet, as an engineer puts them up: each at its own spot, both on one clock — the farther heard
    late by its path, so the two comb where they meet — unless `align` slides the later one onto the first; each brought
    to the same level in the middle (500 Hz-2 kHz) before `blend` mixes them, the second turned over by `invertB`. The
    horn and the back are A's. Blend 0 is A alone. */
struct Pair
{
    Spot a, b;
    double blend = 0;
    bool align = false, invertB = false;
};

inline std::vector<float> renderPair (const Table& table, const std::string& cab, const Pair& p, size_t n = 1 << 14)
{
    if (p.blend <= 0)
        return renderAt (table, cab, p.a, n);

    auto b = p.b;
    b.horn = p.a.horn;
    b.hornDb = p.a.hornDb;
    b.back = p.a.back;
    auto ca = cabAt (table, cab, p.a), cb = cabAt (table, cab, b);
    const double da = firstArrival (ca), db = firstArrival (cb), first = std::min (da, db);
    ca.refDelay = p.align ? da : first;
    cb.refDelay = p.align ? db : first;
    const auto ia = render (ca, n), ib = render (cb, n);

    const auto middle = [n] (const std::vector<double>& ir) {
        std::vector<cd> x (n, 0.0);
        for (size_t i = 0; i < ir.size() && i < n; ++i) x[i] = ir[i];
        fft (x, false);
        double e = 1e-30;
        for (size_t k = (size_t) (500.0 * n / fs); k <= (size_t) (2000.0 * n / fs); ++k) e += std::norm (x[k]);
        return std::sqrt (e);
    };
    const double ga = (1 - p.blend) / middle (ia), gb = p.blend / middle (ib) * (p.invertB ? -1 : 1);
    std::vector<double> mix (std::min (ia.size(), ib.size()));
    for (size_t i = 0; i < mix.size(); ++i) mix[i] = ga * ia[i] + gb * ib[i];
    return peakAt (mix, 0.5);
}

/** The cabinets the table holds, by the names their cap sections give them, in the table's order. */
inline std::vector<std::string> cabinets (const Table& table)
{
    std::vector<std::string> out;
    for (const auto& [name, s] : table)
        if (name.size() > 4 && name.compare (name.size() - 4, 4, " cap") == 0)
            out.push_back (name.substr (0, name.size() - 4));
    return out;
}

/** A 24-bit mono WAV of `x`, as the bytes a file of it would hold. */
inline std::vector<unsigned char> wav24 (const std::vector<float>& x, int rate)
{
    std::vector<unsigned char> b;
    const auto u32 = [&b] (uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back ((unsigned char) (v >> (8 * i))); };
    const auto u16 = [&b] (uint16_t v) { b.push_back ((unsigned char) v); b.push_back ((unsigned char) (v >> 8)); };
    const auto tag = [&b] (const char* t) { b.insert (b.end(), t, t + 4); };
    const uint32_t data = (uint32_t) x.size() * 3;
    tag ("RIFF"); u32 (36 + data); tag ("WAVE"); tag ("fmt "); u32 (16); u16 (1); u16 (1); u32 ((uint32_t) rate);
    u32 ((uint32_t) rate * 3); u16 (3); u16 (24); tag ("data"); u32 (data);
    for (float v : x)
    {
        const auto s = (int32_t) std::lround (std::clamp ((double) v, -1.0, 1.0 - 1.0 / 8388608.0) * 8388608.0);
        b.push_back ((unsigned char) s); b.push_back ((unsigned char) (s >> 8)); b.push_back ((unsigned char) (s >> 16));
    }
    return b;
}

} // namespace orbitamp::core::cabsynth
