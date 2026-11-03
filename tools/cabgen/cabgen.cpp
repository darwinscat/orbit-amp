// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// CABGEN — a close-miked bass cabinet, synthesized: every cone a surface of patches, each moving as a piston below its
// breakup and as a sum of seeded modes above it, summed at the mic with its true path delays; the box's low end (sealed
// or vented, cone and port apart), the horn on its crossover, the baffle's edges, the box's cavity and panel modes, the
// mic's capsule, pattern and proximity. Frequency domain, one complex sum per bin, then one inverse FFT.
//
//   cabgen <table.txt> <out dir> [section ...]       (no section: every one that names drivers)
//
// The bass's cabinets are tools/cabgen/cabinets.txt: one IR a section, 48 kHz, 24-bit, mono, 200 ms, the set at one
// gain (the player levels each IR itself). Built by hand, no JUCE:
//   clang++ -std=c++20 -O2 -o cabgen tools/cabgen/cabgen.cpp && ./cabgen tools/cabgen/cabinets.txt <out dir>
// irlook <ir.wav> ... reads them back: third-octave levels against the 500 Hz-2 kHz band, and how ragged the top is.

#include "dsp.h"

#include <filesystem>
#include <random>

using namespace gen;

namespace
{
constexpr double fs = 48000.0, c0 = 343.0;
constexpr size_t N = 1 << 15;

struct Xy { double x, y; };

std::vector<double> nums (const std::string& s)
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
std::vector<std::array<double, 3>> triples (const Section& s, const std::string& k)
{
    std::vector<std::array<double, 3>> out;
    const auto it = s.find (k);
    if (it == s.end() || it->second == "none") return out;
    const auto v = nums (it->second);
    for (size_t i = 0; i + 2 < v.size(); i += 3) out.push_back ({ v[i], v[i + 1], v[i + 2] });
    return out;
}

std::vector<Xy> points (const Section& s, const std::string& k)
{
    std::vector<Xy> out;
    const auto it = s.find (k);
    if (it == s.end()) return out;
    const auto v = nums (it->second);
    for (size_t i = 0; i + 1 < v.size(); i += 2) out.push_back ({ v[i], v[i + 1] });
    return out;
}

std::pair<double, double> range (const Section& s, const std::string& k, double lo, double hi)
{
    const auto it = s.find (k);
    if (it == s.end()) return { lo, hi };
    const auto dots = it->second.find ("..");
    if (dots == std::string::npos) { const double v = std::atof (it->second.c_str()); return { v, v }; }
    return { std::atof (it->second.substr (0, dots).c_str()), std::atof (it->second.substr (dots + 2).c_str()) };
}

std::string str (const Section& s, const std::string& k, const std::string& fallback = {})
{
    const auto it = s.find (k);
    return it == s.end() ? fallback : it->second;
}

/** A section with what it inherits laid under it, left to right, recursively. */
Section resolve (const std::vector<std::pair<std::string, Section>>& all, const std::string& name, int depth = 0)
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
cd resonance (double f, double f0, double q)
{
    const cd s (0, f / f0);
    return s / q / (s * s + s / q + 1.0);
}

cd highpass2 (double f, double f0, double q) { const cd s (0, f / f0); return s * s / (s * s + s / q + 1.0); }
cd lowpass2 (double f, double f0, double q)  { const cd s (0, f / f0); return 1.0 / (s * s + s / q + 1.0); }
cd lowpass1 (double f, double f0) { return 1.0 / cd (1, f / f0); }

struct Mode { double f, q, residue; int radial, azimuthal; double phase, psi, eps; };

struct Cab
{
    // geometry
    std::vector<Xy> drivers;
    double radius = 0.105, depthFrac = 0.25, bw = 0.6, bh = 0.6;
    // low end
    bool vented = false;
    double fsd = 50, qts = 0.4, vas = 30, vb = 60, perChamber = 1, chambers = 1, fb = 45, ql = 8;
    bool portRear = false, hornAtMic = false;
    double fixedMass = 0, coilHz = 0, proxStrength = 1, micTop = 12000, depth = 0.4;
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
    double micD = 0.05, micOff = 0, tilt = 0;
    double baffleStepDb = 0.5;
    double lengthMs = 200, fadeMs = 30;
};

Cab build (const Section& s)
{
    Cab c;
    c.drivers = points (s, "driver_xy");
    c.radius = num (s, "effective_radius_m", 0.105);
    c.depthFrac = num (s, "cone_depth_fraction_of_radius", 0.25);
    const auto baffle = nums (str (s, "baffle", "0.6,0.6,0.4"));
    c.bw = baffle.size() > 0 ? baffle[0] : 0.6;
    c.bh = baffle.size() > 1 ? baffle[1] : 0.6;

    c.vented = str (s, "alignment") == "ported";
    const auto ts = nums (str (s, "ts", "50,0.4,30"));
    c.fsd = ts[0]; c.qts = ts[1]; c.vas = ts[2];
    const auto ch = nums (str (s, "chamber", "60,1,1"));
    c.vb = ch[0]; c.perChamber = ch.size() > 1 ? ch[1] : 1; c.chambers = ch.size() > 2 ? ch[2] : 1;
    c.depth = baffle.size() > 2 ? baffle[2] : 0.4;
    c.portRear = str (s, "port_face") == "rear";
    c.fixedMass = num (s, "cone_fixed_mass_fraction", 0);
    c.coilHz = num (s, "coil_corner_hz", 0);
    c.proxStrength = num (s, "proximity_strength", 1);
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
    c.baffleStepDb = num (s, "baffle_step_depth_db", 0.5);
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

/** The cone's and the port's shares of the box's low end, at f: sealed, the cone alone; vented, the two apart. */
void lowEnd (const Cab& c, double f, cd& cone, cd& port)
{
    const double alpha = c.perChamber * c.vas / c.vb;
    const cd s (0, 2 * pi * f);
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
cd heard (const Cab& c, const Mic& m, double dx, double dy, double dz, double f)
{
    const double len = std::sqrt (dx * dx + dy * dy + dz * dz);
    const double cosT = std::clamp ((dx * m.ax + dy * m.ay + dz * m.az) / len, -1.0, 1.0);   // the axis points at the cab: in front, +1
    const double theta = std::acos (cosT) * 180 / pi;
    const double kr = 2 * pi * f / c0 * (len + 0.02);
    const cd pattern = 0.5 + 0.5 * cosT * (1.0 + 0.55 * c.proxStrength / cd (0, kr));
    const double e = theta <= 60 ? 2.5 * theta / 60 : theta <= 90 ? 2.5 + 1.5 * (theta - 60) / 30 : 4 + 2 * (theta - 90) / 90;
    const double top = std::clamp (std::log2 (f / 1000) / 3, 0.0, 1.0);
    return pattern * std::pow (10.0, -e * top / 20);
}

std::vector<double> render (const Cab& c)
{
    const size_t bins = N / 2 + 1;
    std::vector<cd> H (N, 0.0);

    // The mic, in front of the first cone, off its centre by a fraction of its radius, tilted toward +x.
    const Xy t = c.drivers.empty() ? Xy {} : c.drivers[0];
    const double tr = c.tilt * pi / 180;
    const Mic mic { t.x + c.micOff * c.radius, t.y, c.micD, -std::sin (tr), 0, -std::cos (tr) };

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

    // The coil's inductance: half a slope (-3 dB/oct) above its corner, minimum-phase — the woofer's alone.
    std::vector<cd> coil (N, 1.0);
    if (c.coilHz > 0)
    {
        std::vector<double> mag (N / 2 + 1);
        for (size_t k = 0; k <= N / 2; ++k) { const double f = fs * k / N; mag[k] = std::pow (1 + (f / c.coilHz) * (f / c.coilHz), -0.25); }
        coil = minimumPhase (mag, N);
    }
    // The target cone's own near-field sum, as the cones' sum is scaled below: what the port and the horn stand against.
    double near = 0;
    for (const auto& p : patches[0]) { double r, dx, dy, dz; pathTo (p.x, p.y, p.z, r, dx, dy, dz); near += p.area / r; }
    // The port against the cone at a cone-close mic (Keele's near field against the port's far field): N Sd / (2 pi r Delta).
    const double dc = c.micD + c.depthFrac * c.radius, delta = std::sqrt (dc * dc + c.radius * c.radius) - dc;
    std::vector<Biquad> bodyEq, presence, hornEq;
    for (auto& e : c.body) bodyEq.push_back (Biquad::make ("peak", e[0], e[1], e[2], fs));
    for (auto& e : c.presence) presence.push_back (Biquad::make ("peak", e[0], e[1], e[2], fs));
    for (auto& e : c.hornEq) hornEq.push_back (Biquad::make ("peak", e[0], e[1], e[2], fs));
    const double stepDb = c.baffleStepDb, stepHz = 115.0 / c.bw;

    for (size_t k = 1; k < bins; ++k)
    {
        const double f = fs * (double) k / (double) N;
        cd cone, port;
        lowEnd (c, f, cone, port);

        // What the box adds to every cone's motion: its cavity and panel modes, then the voicing.
        cd box = 1.0;
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
                double r, dx, dy, dz;
                pathTo (p.x, p.y, p.z, r, dx, dy, dz);
                drv += v * p.area * heard (c, mic, dx, dy, dz, f) / r * std::polar (1.0, -2 * pi * f * (r / c0 - minDelay));
            }
            sum += drv * c.sens[d] * cone * box * coil[k];
        }
        // Normalise the cones' sum to the nearest cone's 1/r at its centre, so the port and the horn sit against it.
        double r0, dx0, dy0, dz0;
        pathTo (t.x, t.y, -c.depthFrac * c.radius, r0, dx0, dy0, dz0);
        sum *= r0 / 1.0;

        if (c.vented)
        {
            double r, dx, dy, dz;
            pathTo (c.port.x, c.port.y, 0, r, dx, dy, dz);
            if (c.fixedMass > 0)
            {
                // Round the box when it is at the back: the path wraps over the depth and half the width, and less arrives.
                const double wrap = c.portRear ? c.depth + 0.5 * c.bw : 0, rp = r + wrap;
                const double ratio = c.perChamber * c.chambers * pi * c.radius * c.radius / (2 * pi * rp * delta) * (c.portRear ? 0.35 : 1);
                sum += port * coil[k] * ratio * (r0 * near) * lowpass2 (f, c.portLeakHz, 0.707) * heard (c, mic, dx, dy, dz, f)
                     * std::polar (1.0, -2 * pi * f * (rp / c0 - minDelay));
            }
            else
            {
                const double area = c.portArea / (pi * c.radius * c.radius * 1e4);   // against one cone's
                sum += port * area * c.perChamber * lowpass2 (f, c.portLeakHz, 0.707) * heard (c, mic, dx, dy, dz, f) * (r0 / r)
                     * std::polar (1.0, -2 * pi * f * (r / c0 - minDelay));
            }
        }
        if (c.horn)
        {
            double r, dx, dy, dz;
            pathTo (c.hornXy.x, c.hornXy.y, -c.hornDepth, r, dx, dy, dz);
            cd hn = highpass2 (f, c.hornXo, c.hornQ) * lowpass2 (f, c.hornLp, 0.707) * std::pow (10.0, c.hornDb / 20);
            for (auto& b : hornEq) hn *= b.at (f, fs);
            if (c.hornAtMic)   // its level at the mic against the cone's pass band, its angle to the mic in that figure
                sum += hn * (r0 * near) * std::polar (1.0, -2 * pi * f * (r / c0 - minDelay));
            else
                sum += hn * heard (c, mic, dx, dy, dz, f) * (r0 / r) * std::polar (1.0, -2 * pi * f * (r / c0 - minDelay));
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
    for (size_t k = 1; k < N / 2; ++k) H[N - k] = std::conj (H[k]);
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
double loudness (const std::vector<double>& ir)
{
    std::vector<cd> X (N, 0.0);
    for (size_t i = 0; i < ir.size() && i < N; ++i) X[i] = ir[i];
    fft (X, false);
    double e = 0;
    for (size_t k = 1; k < N / 2; ++k)
    {
        const double f = fs * k / N;
        if (f < 40 || f > 5000) continue;
        e += std::norm (X[k]) / f;
    }
    return std::sqrt (e);
}
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
        auto ir = render (build (s));
        const double l = loudness (ir);
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
