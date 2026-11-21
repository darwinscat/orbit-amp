// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <cmath>
#include <vector>

namespace orbitamp::loudness
{

/** INTEGRATED LOUDNESS, ITU-R BS.1770-4 / EBU R128, LUFS — for the tools and gates that match levels by ear's measure
    rather than by RMS. JUCE-free.

    K-weighting (the head's shelf at 1.68 kHz, +4 dB, and the 38 Hz high-pass, their coefficients derived for any rate
    as libebur128 derives them), 400 ms blocks a 100 ms step apart, the absolute gate at -70 LUFS and the relative gate
    10 LU under the absolute-gated mean. Channels weigh 1 each (left, right, or one alone): a mono signal reads what
    ffmpeg's ebur128 reads of it, a stereo pair of two equal channels 3 LU more. */
inline double integrated (const std::vector<const float*>& channels, size_t n, double rate)
{
    constexpr double pi = 3.14159265358979323846;
    struct Biquad
    {
        double b0, b1, b2, a1, a2, z1 = 0.0, z2 = 0.0;
        double run (double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };
    const auto shelf = [&]
    {
        const double f0 = 1681.974450955533, g = 3.999843853973347, q = 0.7071752369554196;
        const double k = std::tan (pi * f0 / rate), vh = std::pow (10.0, g / 20.0), vb = std::pow (vh, 0.4996667741545416);
        const double a0 = 1.0 + k / q + k * k;
        return Biquad { (vh + vb * k / q + k * k) / a0, 2.0 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0,
                        2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0 };
    };
    const auto highpass = [&]
    {
        const double f0 = 38.13547087602444, q = 0.5003270373238773;
        const double k = std::tan (pi * f0 / rate), a0 = 1.0 + k / q + k * k;
        return Biquad { 1.0, -2.0, 1.0, 2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0 };
    };

    // The K-weighted power of each 100 ms step, summed over the channels.
    const size_t step = (size_t) std::lround (rate * 0.1);
    if (step == 0 || n < 4 * step)
        return -200.0;
    std::vector<double> power (n / step, 0.0);
    for (const float* x : channels)
    {
        auto a = shelf();
        auto b = highpass();
        for (size_t i = 0; i < power.size() * step; ++i)
        {
            const double y = b.run (a.run ((double) x[i]));
            power[i / step] += y * y;
        }
    }

    // 400 ms blocks, 75 % overlapped: four steps each.
    std::vector<double> blocks;
    for (size_t j = 0; j + 4 <= power.size(); ++j)
        blocks.push_back ((power[j] + power[j + 1] + power[j + 2] + power[j + 3]) / (4.0 * (double) step));
    const auto lufs = [] (double z) { return -0.691 + 10.0 * std::log10 (z + 1.0e-300); };
    const auto meanAbove = [&] (double gate)
    {
        double sum = 0.0;
        int count = 0;
        for (const double z : blocks)
            if (lufs (z) > gate)
            {
                sum += z;
                ++count;
            }
        return count > 0 ? sum / count : 0.0;
    };
    const double absolute = meanAbove (-70.0);
    if (absolute <= 0.0)
        return -200.0;
    const double relative = meanAbove (lufs (absolute) - 10.0);
    return relative > 0.0 ? lufs (relative) : -200.0;
}

/** One channel's. */
inline double integrated (const std::vector<float>& x, double rate)
{
    return integrated (std::vector<const float*> { x.data() }, x.size(), rate);
}

} // namespace orbitamp::loudness
