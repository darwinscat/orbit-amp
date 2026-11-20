// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

// The JS's own arithmetic as io-world.js and io-gl.js use it, for IoWorld's files: Math.min and Math.max
// with their NaN, `%` on doubles, `x || 1`, Math.sign, the bitwise operators' ToInt32 and ToUint32, and V8's sin,
// cos, exp, atan2, hypot and round (the walker's Js). Math.pow and Math.atan are the C library's (as the walker leaves
// Math.pow): they part from V8's in the last bit at most, which the doubles' tolerance covers. Not a public header.

#include "../../redgroove/view/WalkerWorld.h"     // Js: V8's sin, cos, exp, atan2, hypot and round

#include <cmath>
#include <cstdint>
#include <limits>

namespace orbitamp::io::js
{
    using Js = redgroove::WalkerWorld::Js;
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();

    inline double jsMin (double a, double b) noexcept { return (a != a || b != b) ? nan : (a < b ? a : b); }
    inline double jsMax (double a, double b) noexcept { return (a != a || b != b) ? nan : (a > b ? a : b); }
    inline double clamp (double v, double lo, double hi) noexcept { return jsMin (hi, jsMax (lo, v)); }
    inline double smoothstep (double a, double b, double x) noexcept { const double t = clamp ((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }
    inline double lerp (double a, double b, double t) noexcept { return a + (b - a) * t; }
    inline double ease (double dt, double tau) noexcept { return 1 - Js::exp (-dt / tau); }
    inline double fract (double x) noexcept { return x - std::floor (x); }
    inline float f32 (double x) noexcept { return (float) x; }
    /** JS's `%` on doubles: the C library's fmod, which is exact. */
    inline double mod (double a, double b) noexcept { return std::fmod (a, b); }
    /** `x || 1` for a length: 1 where it is 0 or NaN. */
    inline double orOne (double x) noexcept { return std::abs (x) > 0 ? x : 1; }
    inline double sign (double x) noexcept { return x > 0 ? 1 : (x < 0 ? -1 : x); }
    inline double jsPow (double x, double y) noexcept { return std::pow (x, y); }
    inline double jsAtan (double x) noexcept { return std::atan (x); }
    /** Math.asin and Math.log: the C library's, as Math.pow (the last bit at most, under the doubles' tolerance). */
    inline double jsAsin (double x) noexcept { return std::asin (x); }
    inline double jsLog (double x) noexcept { return std::log (x); }
    /** `x ** 2`: V8's pow is fdlibm's, which returns x * x for an exponent of 2. */
    inline double sq (double x) noexcept { return x * x; }
    /** ToUint32: what Math.imul and the bitwise operators make of a number (modulo 2^32; NaN and the infinities 0). */
    inline uint32_t toU32 (double x) noexcept
    {
        if (! std::isfinite (x)) return 0;
        double m = std::fmod (std::trunc (x), 4294967296.0);
        if (m < 0) m += 4294967296.0;
        return (uint32_t) m;
    }
    inline int32_t toI32 (double x) noexcept { return (int32_t) toU32 (x); }
    /** How far apart two arclengths are round a loop of length L. */
    inline double cyc (double a, double b, double L) noexcept { return std::abs (mod (a - b + L * 1.5, L) - L / 2); }
}
