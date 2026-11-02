// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Every expression keeps the JS's order of operations: (a * b) / c stays so, never a * (b / c), and what the JS
// computes in doubles from f32 values is computed here in doubles from the floats. Build with -ffp-contract=off.
// The JS's Math functions are V8's own below (fdlibm, its hypot, its round); only Math.pow is the C library's.

#include "WalkerWorld.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace orbitamp::redgroove
{

namespace
{
    constexpr double pi = WalkerWorld::pi;
    constexpr double inf = std::numeric_limits<double>::infinity();
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();

    //==========================================================================
    // Math.sin, cos, exp, atan2 and acos as V8 computes them: fdlibm, as V8 carries it (src/base/ieee754.cc). The
    // C libraries part from these in the last bit of 3 - 18 % of arguments, which is enough for a long walk to
    // choose another way round a stalk. Beyond 2^19 pi/2 (two days of steps at gear 3) sin and cos fall back to the
    // C library's, rather than carry fdlibm's large reduction.
    //
    // The fdlibm functions below carry this notice:
    //   Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
    //   Developed at SunPro, a Sun Microsystems, Inc. business.
    //   Permission to use, copy, modify, and distribute this software is freely granted, provided that this notice
    //   is preserved.
    // V8's hypot and round below follow V8's own: Copyright the V8 project authors, BSD-style licence.
    inline int32_t hiWord (double x) noexcept { uint64_t u; std::memcpy (&u, &x, 8); return (int32_t) (uint32_t) (u >> 32); }
    inline uint32_t loWord (double x) noexcept { uint64_t u; std::memcpy (&u, &x, 8); return (uint32_t) u; }
    inline double fromWords (uint32_t hi, uint32_t lo) noexcept { const uint64_t u = ((uint64_t) hi << 32) | lo; double x; std::memcpy (&x, &u, 8); return x; }

    double kernelSin (double x, double y, int iy) noexcept
    {
        constexpr double half = 5.00000000000000000000e-01, S1 = -1.66666666666666324348e-01, S2 = 8.33333333332248946124e-03,
                         S3 = -1.98412698298579493134e-04, S4 = 2.75573137070700676789e-06, S5 = -2.50507602534068634195e-08,
                         S6 = 1.58969099521155010221e-10;
        const int32_t ix = hiWord (x) & 0x7fffffff;
        if (ix < 0x3e400000 && (int) x == 0) return x;   // |x| < 2^-27
        const double z = x * x, v = z * x, r = S2 + z * (S3 + z * (S4 + z * (S5 + z * S6)));
        if (iy == 0) return x + v * (S1 + z * r);
        return x - ((z * (half * y - v * r) - y) - v * S1);
    }

    double kernelCos (double x, double y) noexcept
    {
        constexpr double one = 1.0, C1 = 4.16666666666666019037e-02, C2 = -1.38888888888741095749e-03, C3 = 2.48015872894767294178e-05,
                         C4 = -2.75573143513906633035e-07, C5 = 2.08757232129817482790e-09, C6 = -1.13596475577881948265e-11;
        const int32_t ix = hiWord (x) & 0x7fffffff;
        if (ix < 0x3e400000 && (int) x == 0) return one;
        const double z = x * x, r = z * (C1 + z * (C2 + z * (C3 + z * (C4 + z * (C5 + z * C6)))));
        if (ix < 0x3fd33333) return one - (0.5 * z - (z * r - x * y));   // |x| < 0.3
        const double qx = ix > 0x3fe90000 ? 0.28125 : fromWords ((uint32_t) (ix - 0x00200000), 0);   // x / 4
        const double iz = 0.5 * z - qx, a = one - qx;
        return a - (iz - (z * r - x * y));
    }

    /** x reduced by n pi/2 into y[0] + y[1]; false for |x| > 2^19 pi/2, which this does not reduce. */
    bool remPio2 (double x, double* y, int& n) noexcept
    {
        static constexpr int32_t npio2hw[32] {
            0x3FF921FB, 0x400921FB, 0x4012D97C, 0x401921FB, 0x401F6A7A, 0x4022D97C, 0x4025FDBB, 0x402921FB,
            0x402C463A, 0x402F6A7A, 0x4031475C, 0x4032D97C, 0x40346B9C, 0x4035FDBB, 0x40378FDB, 0x403921FB,
            0x403AB41B, 0x403C463A, 0x403DD85A, 0x403F6A7A, 0x40407E4C, 0x4041475C, 0x4042106C, 0x4042D97C,
            0x4043A28C, 0x40446B9C, 0x404534AC, 0x4045FDBB, 0x4046C6CB, 0x40478FDB, 0x404858EB, 0x404921FB };
        constexpr double half = 0.5, invpio2 = 6.36619772367581382433e-01, pio2_1 = 1.57079632673412561417e+00,
                         pio2_1t = 6.07710050650619224932e-11, pio2_2 = 6.07710050630396597660e-11, pio2_2t = 2.02226624879595063154e-21,
                         pio2_3 = 2.02226624871116645580e-21, pio2_3t = 8.47842766036889956997e-32;
        const int32_t hx = hiWord (x), ix = hx & 0x7fffffff;
        if (ix <= 0x3fe921fb) { y[0] = x; y[1] = 0; n = 0; return true; }
        if (ix < 0x4002d97c)                                       // |x| < 3 pi/4: n = +-1
        {
            if (hx > 0)
            {
                double z = x - pio2_1;
                if (ix != 0x3ff921fb) { y[0] = z - pio2_1t; y[1] = (z - y[0]) - pio2_1t; }
                else { z -= pio2_2; y[0] = z - pio2_2t; y[1] = (z - y[0]) - pio2_2t; }
                n = 1;
            }
            else
            {
                double z = x + pio2_1;
                if (ix != 0x3ff921fb) { y[0] = z + pio2_1t; y[1] = (z - y[0]) + pio2_1t; }
                else { z += pio2_2; y[0] = z + pio2_2t; y[1] = (z - y[0]) + pio2_2t; }
                n = -1;
            }
            return true;
        }
        if (ix > 0x413921fb) return false;
        double t = std::fabs (x);
        const int k = (int) (t * invpio2 + half);
        const double fn = (double) k;
        double r = t - fn * pio2_1, w = fn * pio2_1t;
        if (k < 32 && ix != npio2hw[k - 1]) y[0] = r - w;
        else
        {
            const int32_t j = ix >> 20;
            y[0] = r - w;
            int32_t i = j - ((hiWord (y[0]) >> 20) & 0x7ff);
            if (i > 16)
            {
                t = r; w = fn * pio2_2; r = t - w; w = fn * pio2_2t - ((t - r) - w); y[0] = r - w;
                i = j - ((hiWord (y[0]) >> 20) & 0x7ff);
                if (i > 49) { t = r; w = fn * pio2_3; r = t - w; w = fn * pio2_3t - ((t - r) - w); y[0] = r - w; }
            }
        }
        y[1] = (r - y[0]) - w;
        if (hx < 0) { y[0] = -y[0]; y[1] = -y[1]; n = -k; } else n = k;
        return true;
    }

    double jsSin (double x) noexcept
    {
        const int32_t ix = hiWord (x) & 0x7fffffff;
        if (ix <= 0x3fe921fb) return kernelSin (x, 0, 0);
        if (ix >= 0x7ff00000) return x - x;
        double y[2]; int n;
        if (! remPio2 (x, y, n)) return std::sin (x);
        switch (n & 3)
        {
            case 0: return kernelSin (y[0], y[1], 1);
            case 1: return kernelCos (y[0], y[1]);
            case 2: return -kernelSin (y[0], y[1], 1);
            default: return -kernelCos (y[0], y[1]);
        }
    }

    double jsCos (double x) noexcept
    {
        const int32_t ix = hiWord (x) & 0x7fffffff;
        if (ix <= 0x3fe921fb) return kernelCos (x, 0);
        if (ix >= 0x7ff00000) return x - x;
        double y[2]; int n;
        if (! remPio2 (x, y, n)) return std::cos (x);
        switch (n & 3)
        {
            case 0: return kernelCos (y[0], y[1]);
            case 1: return -kernelSin (y[0], y[1], 1);
            case 2: return -kernelCos (y[0], y[1]);
            default: return kernelSin (y[0], y[1], 1);
        }
    }

    double jsExp (double x) noexcept
    {
        constexpr double one = 1.0, halF[2] { 0.5, -0.5 }, o_threshold = 7.09782712893383973096e+02, u_threshold = -7.45133219101941108420e+02,
                         ln2HI[2] { 6.93147180369123816490e-01, -6.93147180369123816490e-01 },
                         ln2LO[2] { 1.90821492927058770002e-10, -1.90821492927058770002e-10 }, invln2 = 1.44269504088896338700e+00,
                         P1 = 1.66666666666666019037e-01, P2 = -2.77777777770155933842e-03, P3 = 6.61375632143793436117e-05,
                         P4 = -1.65339022054652515390e-06, P5 = 4.13813679705723846039e-08, E = 2.718281828459045,
                         huge = 1.0e+300, twom1000 = 9.33263618503218878990e-302, two1023 = 8.988465674311579539e307;
        double hi = 0, lo = 0;
        int32_t k = 0;
        uint32_t hx = (uint32_t) hiWord (x);
        const int xsb = (int) ((hx >> 31) & 1);
        hx &= 0x7fffffff;
        if (hx >= 0x40862E42)
        {
            if (hx >= 0x7ff00000) return ((hx & 0xfffff) | loWord (x)) != 0 ? x + x : (xsb == 0 ? x : 0.0);
            if (x > o_threshold) return huge * huge;
            if (x < u_threshold) return twom1000 * twom1000;
        }
        if (hx > 0x3fd62e42)                                       // |x| > 0.5 ln2
        {
            if (hx < 0x3ff0a2b2)                                   // and |x| < 1.5 ln2
            {
                if (x == 1.0) return E;
                hi = x - ln2HI[xsb]; lo = ln2LO[xsb]; k = 1 - xsb - xsb;
            }
            else
            {
                k = (int32_t) (invln2 * x + halF[xsb]);
                const double t = k;
                hi = x - t * ln2HI[0];
                lo = t * ln2LO[0];
            }
            x = hi - lo;
        }
        else if (hx < 0x3e300000) { if (huge + x > one) return one + x; }
        else k = 0;
        const double t = x * x;
        const double twopk = k >= -1021 ? fromWords ((uint32_t) (0x3ff00000 + (k << 20)), 0) : fromWords ((uint32_t) (0x3ff00000 + ((k + 1000) << 20)), 0);
        const double c = x - t * (P1 + t * (P2 + t * (P3 + t * (P4 + t * P5))));
        if (k == 0) return one - ((x * c) / (c - 2.0) - x);
        const double y = one - ((lo - (x * c) / (2.0 - c)) - hi);
        if (k >= -1021) return k == 1024 ? y * 2.0 * two1023 : y * twopk;
        return y * twopk * twom1000;
    }

    double jsAtan (double x) noexcept
    {
        static constexpr double atanhi[4] { 4.63647609000806093515e-01, 7.85398163397448278999e-01, 9.82793723247329054082e-01, 1.57079632679489655800e+00 };
        static constexpr double atanlo[4] { 2.26987774529616870924e-17, 3.06161699786838301793e-17, 1.39033110312309984516e-17, 6.12323399573676603587e-17 };
        static constexpr double aT[11] { 3.33333333333329318027e-01, -1.99999999998764832476e-01, 1.42857142725034663711e-01,
                                         -1.11111104054623557880e-01, 9.09088713343650656196e-02, -7.69187620504482999495e-02,
                                         6.66107313738753120669e-02, -5.83357013379057348645e-02, 4.97687799461593236017e-02,
                                         -3.65315727442169155270e-02, 1.62858201153657823623e-02 };
        constexpr double one = 1.0, huge = 1.0e300;
        const int32_t hx = hiWord (x), ix = hx & 0x7fffffff;
        int id;
        if (ix >= 0x44100000)                                      // |x| >= 2^66
        {
            if (ix > 0x7ff00000 || (ix == 0x7ff00000 && loWord (x) != 0)) return x + x;
            return hx > 0 ? atanhi[3] + atanlo[3] : -atanhi[3] - atanlo[3];
        }
        if (ix < 0x3fdc0000)                                       // |x| < 0.4375
        {
            if (ix < 0x3e400000 && huge + x > one) return x;       // |x| < 2^-27
            id = -1;
        }
        else
        {
            x = std::fabs (x);
            if (ix < 0x3ff30000)
            {
                if (ix < 0x3fe60000) { id = 0; x = (2.0 * x - one) / (2.0 + x); }
                else { id = 1; x = (x - one) / (x + one); }
            }
            else
            {
                if (ix < 0x40038000) { id = 2; x = (x - 1.5) / (one + 1.5 * x); }
                else { id = 3; x = -1.0 / x; }
            }
        }
        const double z = x * x, w = z * z;
        const double s1 = z * (aT[0] + w * (aT[2] + w * (aT[4] + w * (aT[6] + w * (aT[8] + w * aT[10])))));
        const double s2 = w * (aT[1] + w * (aT[3] + w * (aT[5] + w * (aT[7] + w * aT[9]))));
        if (id < 0) return x - x * (s1 + s2);
        const double zz = atanhi[id] - ((x * (s1 + s2) - atanlo[id]) - x);
        return hx < 0 ? -zz : zz;
    }

    double jsAtan2 (double y, double x) noexcept
    {
        constexpr double tiny = 1.0e-300, pi_o_4 = 7.8539816339744827900E-01, pi_o_2 = 1.5707963267948965580E+00,
                         piC = 3.1415926535897931160E+00, pi_lo = 1.2246467991473531772E-16;
        const int32_t hx = hiWord (x), ix = hx & 0x7fffffff, hy = hiWord (y), iy = hy & 0x7fffffff;
        const uint32_t lx = loWord (x), ly = loWord (y);
        if (x != x || y != y) return x + y;
        if (((uint32_t) (hx - 0x3ff00000) | lx) == 0) return jsAtan (y);   // x = 1.0
        int m = ((hy >> 31) & 1) | ((hx >> 30) & 2);              // 2 * sign (x) + sign (y)
        if ((iy | (int32_t) ly) == 0)
        {
            switch (m) { case 0: case 1: return y; case 2: return piC + tiny; default: return -piC - tiny; }
        }
        if ((ix | (int32_t) lx) == 0) return hy < 0 ? -pi_o_2 - tiny : pi_o_2 + tiny;
        if (ix == 0x7ff00000)
        {
            if (iy == 0x7ff00000)
                switch (m) { case 0: return pi_o_4 + tiny; case 1: return -pi_o_4 - tiny; case 2: return 3.0 * pi_o_4 + tiny; default: return -3.0 * pi_o_4 - tiny; }
            switch (m) { case 0: return 0.0; case 1: return -0.0; case 2: return piC + tiny; default: return -piC - tiny; }
        }
        if (iy == 0x7ff00000) return hy < 0 ? -pi_o_2 - tiny : pi_o_2 + tiny;
        const int32_t k = (iy - ix) >> 20;
        double z;
        if (k > 60) { z = pi_o_2 + 0.5 * pi_lo; m &= 1; }
        else if (hx < 0 && k < -60) z = 0.0;
        else z = jsAtan (std::fabs (y / x));
        switch (m)
        {
            case 0: return z;
            case 1: return -z;
            case 2: return piC - (z - pi_lo);
            default: return (z - pi_lo) - piC;
        }
    }

    double jsAcos (double x) noexcept
    {
        constexpr double one = 1.0, piC = 3.14159265358979311600e+00, pio2_hi = 1.57079632679489655800e+00, pio2_lo = 6.12323399573676603587e-17,
                         pS0 = 1.66666666666666657415e-01, pS1 = -3.25565818622400915405e-01, pS2 = 2.01212532134862925881e-01,
                         pS3 = -4.00555345006794114027e-02, pS4 = 7.91534994289814532176e-04, pS5 = 3.47933107596021167570e-05,
                         qS1 = -2.40339491173441421878e+00, qS2 = 2.02094576023350569471e+00, qS3 = -6.88283971605453293030e-01,
                         qS4 = 7.70381505559019352791e-02;
        const int32_t hx = hiWord (x), ix = hx & 0x7fffffff;
        if (ix >= 0x3ff00000)
        {
            if (((uint32_t) (ix - 0x3ff00000) | loWord (x)) == 0) return hx > 0 ? 0.0 : piC + 2.0 * pio2_lo;
            return std::numeric_limits<double>::quiet_NaN();
        }
        if (ix < 0x3fe00000)                                       // |x| < 0.5
        {
            if (ix <= 0x3c600000) return pio2_hi + pio2_lo;
            const double z = x * x;
            const double p = z * (pS0 + z * (pS1 + z * (pS2 + z * (pS3 + z * (pS4 + z * pS5)))));
            const double q = one + z * (qS1 + z * (qS2 + z * (qS3 + z * qS4)));
            return pio2_hi - (x - (pio2_lo - x * (p / q)));
        }
        if (hx < 0)                                                // x < -0.5
        {
            const double z = (one + x) * 0.5;
            const double p = z * (pS0 + z * (pS1 + z * (pS2 + z * (pS3 + z * (pS4 + z * pS5)))));
            const double q = one + z * (qS1 + z * (qS2 + z * (qS3 + z * qS4)));
            const double s = std::sqrt (z), r = p / q, w = r * s - pio2_lo;
            return piC - 2.0 * (s + w);
        }
        const double z = (one - x) * 0.5, s = std::sqrt (z);      // x > 0.5
        const double df = fromWords ((uint32_t) hiWord (s), 0);
        const double c = (z - df * df) / (s + df);
        const double p = z * (pS0 + z * (pS1 + z * (pS2 + z * (pS3 + z * (pS4 + z * pS5)))));
        const double q = one + z * (qS1 + z * (qS2 + z * (qS3 + z * qS4)));
        const double r = p / q, w = r * s + c;
        return 2.0 * (df + w);
    }

    //==========================================================================
    // The JS built-ins, as JS has them: NaN goes through Math.min / Math.max.
    inline double jsMin (double a, double b) noexcept { return (a != a || b != b) ? nan : (a < b ? a : b); }
    inline double jsMax (double a, double b) noexcept { return (a != a || b != b) ? nan : (a > b ? a : b); }
    inline double clamp (double v, double lo, double hi) noexcept { return jsMin (hi, jsMax (lo, v)); }
    inline double smoothstep (double a, double b, double x) noexcept { const double t = clamp ((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }
    inline double fract (double x) noexcept { return x - std::floor (x); }
    inline double quintic (double t) noexcept { return t * t * t * (t * (t * 6 - 15) + 10); }

    /** Math.round as V8 has it: the integer nearest, halves toward +infinity. */
    inline double jsRound (double x) noexcept
    {
        const double r = std::ceil (x);
        return r - 0.5 <= x ? r : r - 1.0;
    }

    /** Math.hypot as V8 has it (not the C library's): each value over the largest, the squares summed with a
        Kahan compensation, the root times the largest. */
    template <int N>
    double jsHypot (const double (&v)[N]) noexcept
    {
        double a[N] {};
        bool isNan = false;
        double max = 0;
        for (int i = 0; i < N; ++i)
        {
            if (v[i] != v[i]) { isNan = true; continue; }
            a[i] = std::fabs (v[i]);
            if (a[i] > max) max = a[i];
        }
        if (max == inf) return inf;
        if (isNan) return nan;
        if (max == 0) return 0;
        double sum = 0, compensation = 0;
        for (int i = 0; i < N; ++i)
        {
            const double n = a[i] / max, summand = n * n - compensation, preliminary = sum + summand;
            compensation = (preliminary - sum) - summand;
            sum = preliminary;
        }
        return std::sqrt (sum) * max;
    }
    inline double hypot2 (double x, double y) noexcept { const double v[2] { x, y }; return jsHypot (v); }
    inline double hypot3 (double x, double y, double z) noexcept { const double v[3] { x, y, z }; return jsHypot (v); }

    inline double wrapAngle (double a) noexcept { return a - 2 * pi * jsRound (a / (2 * pi)); }
    inline double jsSign (double x) noexcept { return x > 0 ? 1.0 : x < 0 ? -1.0 : x; }

    /** Value noise on a wrapping lattice of `cells` across the unit square, at (u, v) in 0..1: -1..1. */
    double lattice (double u, double v, int cells, int seed) noexcept
    {
        const double x = u * cells, y = v * cells, x0 = std::floor (x), y0 = std::floor (y);
        const double fx = quintic (x - x0), fy = quintic (y - y0);
        const int ix = (int) x0, iy = (int) y0;
        const int i0 = ((ix % cells) + cells) % cells, i1 = (i0 + 1) % cells, j0 = ((iy % cells) + cells) % cells, j1 = (j0 + 1) % cells;
        const double a = WalkerWorld::hcell (i0, j0, seed), b = WalkerWorld::hcell (i1, j0, seed);
        const double c = WalkerWorld::hcell (i0, j1, seed), d = WalkerWorld::hcell (i1, j1, seed);
        return ((a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy) * 2 - 1;
    }

    /** Fractal noise: `octaves` of the lattice from `cells0` cells, `gain` each: about -1..1. */
    double fbm (double u, double v, int cells0, int octaves, int seed, double gain = 0.5) noexcept
    {
        double sum = 0, amp = 1, norm = 0;
        int cells = cells0;
        for (int o = 0; o < octaves; ++o) { sum += amp * lattice (u, v, cells, seed + o * 7); norm += amp; amp *= gain; cells *= 2; }
        return sum / norm;
    }

    /** A value stored into a Uint8Array: modulo 256. */
    inline uint8_t toUint8 (double v) noexcept { const long long i = (long long) v; return (uint8_t) (((i % 256) + 256) % 256); }

    /** A ring of a solid of revolution; `part` < 0 leaves the part out (the lathe's 8 floats a vertex). */
    int ring (std::vector<float>& verts, int stride, int segs, double r, double y, double nr, double ny, double u, double part)
    {
        const int base = (int) (verts.size() / (size_t) stride);
        for (int j = 0; j <= segs; ++j)
        {
            const double a = 2 * pi * j / segs, ca = jsCos (a), sa = jsSin (a);
            double len = hypot2 (nr, ny);
            if (len == 0 || len != len) len = 1;
            verts.push_back ((float) (r * ca)); verts.push_back ((float) y); verts.push_back ((float) (r * sa));
            verts.push_back ((float) (nr / len * ca)); verts.push_back ((float) (ny / len)); verts.push_back ((float) (nr / len * sa));
            verts.push_back ((float) u); verts.push_back ((float) ((double) j / segs));
            if (part >= 0) verts.push_back ((float) part);
        }
        return base;
    }
    inline void push3 (std::vector<uint16_t>& idx, int a, int b, int c) { idx.push_back ((uint16_t) a); idx.push_back ((uint16_t) b); idx.push_back ((uint16_t) c); }
}

//==============================================================================
double WalkerWorld::Js::sin (double x) noexcept { return jsSin (x); }
double WalkerWorld::Js::cos (double x) noexcept { return jsCos (x); }
double WalkerWorld::Js::exp (double x) noexcept { return jsExp (x); }
double WalkerWorld::Js::atan2 (double y, double x) noexcept { return jsAtan2 (y, x); }
double WalkerWorld::Js::acos (double x) noexcept { return jsAcos (x); }
double WalkerWorld::Js::hypot (double x, double y) noexcept { return hypot2 (x, y); }
double WalkerWorld::Js::hypot (double x, double y, double z) noexcept { return hypot3 (x, y, z); }
double WalkerWorld::Js::round (double x) noexcept { return jsRound (x); }

//==============================================================================
uint32_t WalkerWorld::ih (uint32_t x) noexcept
{
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}

double WalkerWorld::hcell (int32_t cx, int32_t cy, int32_t s) noexcept
{
    const uint32_t inner = ih (((uint32_t) cy * 0x85EBCA77u) ^ (uint32_t) s);
    return (double) (float) ((double) ih (((uint32_t) cx * 0x9E3779B1u) ^ inner) / 4294967296.0);
}

//==============================================================================
std::vector<float> WalkerWorld::buildHeight()
{
    const int n = heightTex;
    std::vector<float> px ((size_t) n * (size_t) n * 4);
    for (int y = 0; y < n; ++y)
    {
        const double v = (double) y / n;
        for (int x = 0; x < n; ++x)
        {
            const double u = (double) x / n;
            const double hills = fbm (u, v, 8, 5, 1101, 0.48);           // 128 m .. 8 m wavelengths
            const double swell = lattice (u, v, 3, 2202);                 // the broad lie of the land
            const double ridge = 1 - std::fabs (lattice (u, v, 6, 3303)); // soft ridges
            const double h = 7.5 * hills + 9.0 * swell + 3.5 * ridge * ridge;
            const double dens = smoothstep (-0.55, 0.25, fbm (u, v, 4, 3, 4404));   // groves and clearings
            const double moss = 0.5 + 0.5 * fbm (u, v, 32, 2, 5505);
            const size_t i = ((size_t) y * (size_t) n + (size_t) x) * 4;
            px[i] = (float) h; px[i + 1] = (float) dens; px[i + 2] = (float) moss; px[i + 3] = 1;
        }
    }
    return px;
}

std::vector<std::vector<float>> WalkerWorld::mipChain (const std::vector<float>& px, int n)
{
    std::vector<std::vector<float>> out;
    out.push_back (px);
    for (int size = n; size > 1; size >>= 1)
    {
        const std::vector<float>& cur = out.back();
        const int s2 = size >> 1;
        std::vector<float> nxt ((size_t) s2 * (size_t) s2 * 4);
        for (int y = 0; y < s2; ++y)
            for (int x = 0; x < s2; ++x)
                for (int c = 0; c < 4; ++c)
                {
                    const double a = cur[(size_t) (((2 * y) * size + 2 * x) * 4 + c)], b = cur[(size_t) (((2 * y) * size + 2 * x + 1) * 4 + c)];
                    const double cc = cur[(size_t) (((2 * y + 1) * size + 2 * x) * 4 + c)], d = cur[(size_t) (((2 * y + 1) * size + 2 * x + 1) * 4 + c)];
                    nxt[(size_t) ((y * s2 + x) * 4 + c)] = (float) (0.25 * (a + b + cc + d));
                }
        out.push_back (std::move (nxt));
    }
    return out;
}

std::vector<uint8_t> WalkerWorld::buildDetail()
{
    const int d = detailTex, m = d - 1;
    std::vector<float> v ((size_t) (d * d));
    std::vector<uint8_t> px ((size_t) (d * d * 4));
    for (int y = 0; y < d; ++y)
        for (int x = 0; x < d; ++x)
            v[(size_t) (y * d + x)] = (float) (0.5 + 0.5 * fbm ((double) x / d, (double) y / d, 4, 3, 6606));
    for (int y = 0; y < d; ++y)
        for (int x = 0; x < d; ++x)
        {
            const double gx = ((double) v[(size_t) (y * d + ((x + 1) & m))] - (double) v[(size_t) (y * d + ((x - 1) & m))]) * 3;
            const double gy = ((double) v[(size_t) (((y + 1) & m) * d + x)] - (double) v[(size_t) (((y - 1) & m) * d + x)]) * 3;
            const size_t i = (size_t) ((y * d + x) * 4);
            px[i] = toUint8 (std::trunc (128 + clamp (gx * 127, -127, 127)));
            px[i + 1] = toUint8 (std::trunc (128 + clamp (gy * 127, -127, 127)));
            px[i + 2] = toUint8 (std::trunc ((double) v[(size_t) (y * d + x)] * 255));
            px[i + 3] = 255;
        }
    return px;
}

//==============================================================================
const WalkerWorld::Profile& WalkerWorld::profileOf (int variant) noexcept
{
    static const Profile V[variants] {
        { 0.74, 0.46, { { 1.00, -0.03 }, { 0.96, 0.04 }, { 0.84, 0.13 }, { 0.62, 0.21 }, { 0.34, 0.255 }, { 0.00, 0.27 } },
                      { { 1.00, -0.03 }, { 0.72, 0.00 }, { 0.40, 0.012 }, { 0.0, 0.01 } } },    // the parasol
        { 0.62, 0.33, { { 1.00, -0.12 }, { 0.97, -0.02 }, { 0.85, 0.12 }, { 0.62, 0.26 }, { 0.32, 0.35 }, { 0.00, 0.38 } },
                      { { 1.00, -0.12 }, { 0.75, -0.06 }, { 0.45, -0.02 }, { 0.0, 0.00 } } },   // the bell
        { 0.80, 0.62, { { 1.00, 0.00 }, { 0.94, 0.045 }, { 0.72, 0.10 }, { 0.42, 0.135 }, { 0.18, 0.15 }, { 0.00, 0.155 } },
                      { { 1.00, 0.00 }, { 0.70, 0.025 }, { 0.38, 0.035 }, { 0.0, 0.02 } } },    // the shelf
    };
    return V[variant % variants];
}

double WalkerWorld::stalkRadius (int variant, double t) noexcept
{
    static constexpr double r0[variants] { 0.075, 0.07, 0.085 };
    const double bulb = 1 + 0.9 * std::pow (1 - t, 3);
    return r0[variant % variants] * bulb * (1 - 0.18 * t);
}

WalkerWorld::Mesh WalkerWorld::mushroomMesh (int variant, int segs)
{
    const Profile& pr = profileOf (variant);
    Mesh out;
    auto& verts = out.verts;
    auto& idx = out.idx;
    const auto band = [&] (int b0, int b1)
    {
        for (int j = 0; j < segs; ++j)
        {
            push3 (idx, b0 + j, b1 + j, b1 + j + 1);
            push3 (idx, b0 + j, b1 + j + 1, b0 + j + 1);
        }
    };
    constexpr int rings = 7;
    int prev = -1;
    for (int i = 0; i < rings; ++i)
    {
        const double t = (double) i / (rings - 1), y = -0.06 + (pr.capY + 0.06) * t;
        const double r = stalkRadius (variant, t);
        const double dr = (stalkRadius (variant, jsMin (1, t + 0.01)) - stalkRadius (variant, jsMax (0, t - 0.01))) / (0.02 * (pr.capY + 0.06));
        const int b = ring (verts, 9, segs, r, y, 1, -dr, t, 0);
        if (prev >= 0) band (prev, b);
        prev = b;
    }
    const auto& top = pr.top;
    const int nt = (int) top.size();
    prev = -1;
    for (int i = 0; i < nt; ++i)
    {
        const double r = top[(size_t) i][0] * pr.R, y = pr.capY + top[(size_t) i][1];
        const auto& p0 = top[(size_t) std::max (0, i - 1)];
        const auto& p1 = top[(size_t) std::min (nt - 1, i + 1)];
        const double dr = (p1[0] - p0[0]) * pr.R, dy = p1[1] - p0[1];
        const int b = ring (verts, 9, segs, r, y, -dy, dr, (double) i / (nt - 1), 1);
        if (prev >= 0) band (b, prev);
        prev = b;
    }
    const auto& g = pr.gills;
    const int ng = (int) g.size();
    prev = -1;
    const double rs = stalkRadius (variant, 1);
    for (int i = 0; i < ng; ++i)
    {
        const double r = i == ng - 1 ? rs : g[(size_t) i][0] * pr.R, y = pr.capY + g[(size_t) i][1];
        const auto& p0 = g[(size_t) std::max (0, i - 1)];
        const auto& p1 = g[(size_t) std::min (ng - 1, i + 1)];
        const double dr = (p1[0] - p0[0]) * pr.R, dy = p1[1] - p0[1];
        const int b = ring (verts, 9, segs, r, y, dy, -dr, (double) i / (ng - 1), 2);
        if (prev >= 0) band (prev, b);
        prev = b;
    }
    return out;
}

WalkerWorld::Mesh WalkerWorld::lathe (const std::vector<std::array<double, 2>>& profile, int segs)
{
    Mesh out;
    auto& verts = out.verts;
    auto& idx = out.idx;
    const int n = (int) profile.size();
    int prev = ring (verts, 8, segs, 0, profile[0][1], 0, -1, 0, -1);
    for (int i = 0; i < n; ++i)
    {
        const double r = profile[(size_t) i][0], y = profile[(size_t) i][1];
        const auto& p0 = profile[(size_t) std::max (0, i - 1)];
        const auto& p1 = profile[(size_t) std::min (n - 1, i + 1)];
        const double dr = p1[0] - p0[0], dy = p1[1] - p0[1];
        const int b = ring (verts, 8, segs, r, y, dy, -dr, (double) (i + 1) / (n + 1), -1);
        for (int j = 0; j < segs; ++j) { push3 (idx, prev + j, b + j, b + j + 1); push3 (idx, prev + j, b + j + 1, prev + j + 1); }
        prev = b;
    }
    const int top = ring (verts, 8, segs, 0, profile[(size_t) (n - 1)][1], 0, 1, 1, -1);
    for (int j = 0; j < segs; ++j) { push3 (idx, prev + j, top + j, top + j + 1); push3 (idx, prev + j, top + j + 1, prev + j + 1); }
    return out;
}

WalkerWorld::Mesh WalkerWorld::polarGrid (int rings, int spokes, double nearR, double farR)
{
    Mesh out;
    out.verts.assign ((size_t) ((rings * spokes + 1) * 2), 0.0f);
    for (int i = 0; i < rings; ++i)
    {
        const double r = nearR * std::pow (farR / nearR, (double) i / (rings - 1));
        for (int j = 0; j < spokes; ++j)
        {
            const double a = 2 * pi * j / spokes;
            const int k = 1 + i * spokes + j;
            out.verts[(size_t) (k * 2)] = (float) (r * jsCos (a)); out.verts[(size_t) (k * 2 + 1)] = (float) (r * jsSin (a));
        }
    }
    for (int j = 0; j < spokes; ++j) push3 (out.idx, 0, 1 + j, 1 + (j + 1) % spokes);
    for (int i = 0; i + 1 < rings; ++i)
        for (int j = 0; j < spokes; ++j)
        {
            const int a = 1 + i * spokes + j, b = 1 + i * spokes + (j + 1) % spokes, c = a + spokes, d = b + spokes;
            push3 (out.idx, a, c, d); push3 (out.idx, a, d, b);
        }
    return out;
}

//==============================================================================
double WalkerWorld::stepRise (double u) noexcept { return std::pow (jsSin (pi * u), 1.3); }
double WalkerWorld::stepThud (double u) noexcept { const double k = u / 0.1; return k * jsExp (1 - k); }

std::array<double, 3> WalkerWorld::kneeOf (const std::array<double, 3>& h, const std::array<double, 3>& f,
                                           double lf, double lt, const std::array<double, 3>& pole) noexcept
{
    double ax = f[0] - h[0], ay = f[1] - h[1], az = f[2] - h[2];
    double d = hypot3 (ax, ay, az);
    const double dmax = (lf + lt) * 0.995;
    if (d > dmax) d = dmax;
    if (d < 1e-4) return { h[0], h[1] + lf, h[2] };
    ax /= d; ay /= d; az /= d;
    const double a = clamp ((lf * lf - lt * lt + d * d) / (2 * d), -lf, lf);
    const double hgt = std::sqrt (jsMax (0, lf * lf - a * a));
    const double dot = pole[0] * ax + pole[1] * ay + pole[2] * az;
    double px = pole[0] - dot * ax, py = pole[1] - dot * ay, pz = pole[2] - dot * az;
    double pl = hypot3 (px, py, pz);
    if (pl == 0 || pl != pl) pl = 1;
    px /= pl; py /= pl; pz /= pl;
    return { h[0] + ax * a + px * hgt, h[1] + ay * a + py * hgt, h[2] + az * a + pz * hgt };
}

//==============================================================================
WalkerWorld::WalkerWorld() noexcept
{
    s.legs[0].side = -1; s.legs[0].off = 0;
    s.legs[1].side = 1; s.legs[1].off = 1;
    for (int i = 0; i < maxBursts; ++i) s.bursts[(size_t) (i * 8 + 3)] = -100;
    for (int i = 0; i < maxPrints; ++i) s.prints[(size_t) (i * 4 + 2)] = -100;
    deadEnds.fill (-1e9);
    discs.assign ((size_t) maxDiscs * 3, 0.0);
    for (auto* v : { &ox, &oz, &q2, &capAt, &gives, &k0, &k1 }) v->assign ((size_t) maxDiscs, 0.0);
    nearIdx.assign ((size_t) maxDiscs, 0);
    touched.assign ((size_t) maxDiscs, 0);
}

void WalkerWorld::startAt (double x, double z) noexcept
{
    s.x = s.eyeX = x;
    s.z = s.eyeZ = z;
}

void WalkerWorld::build()
{
    if (built) return;
    mips = mipChain (buildHeight(), heightTex);
    detailPx = buildDetail();
    built = true;
    settle();
    step (0);
}

double WalkerWorld::tap (int ch, double px, double pz) const noexcept
{
    const int n = heightTex, m = n - 1;
    const double x = px / heightPeriod * n - 0.5, y = pz / heightPeriod * n - 0.5, x0 = std::floor (x), y0 = std::floor (y);
    const double fx = x - x0, fy = y - y0;
    const int ix = (int) x0, iy = (int) y0;
    const int i0 = ix & m, i1 = (ix + 1) & m, j0 = iy & m, j1 = (iy + 1) & m;
    const std::vector<float>& height = mips[0];
    const double a = height[(size_t) ((j0 * n + i0) * 4 + ch)], b = height[(size_t) ((j0 * n + i1) * 4 + ch)];
    const double c = height[(size_t) ((j1 * n + i0) * 4 + ch)], d = height[(size_t) ((j1 * n + i1) * 4 + ch)];
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy;
}

double WalkerWorld::groundH (double x, double z) const noexcept { return built ? tap (0, x, z) : 0; }
double WalkerWorld::density (double x, double z) const noexcept { return built ? tap (1, x, z) : 1; }

bool WalkerWorld::mushroomAt (int k, int ci, int cj, Mushroom& m) const noexcept
{
    const MushroomClass& C = classes[(size_t) k];
    const int M = C.m;
    const int cx = ((ci % M) + M) % M, cz = ((cj % M) + M) % M, S = 100 + k * 16;
    const double x = (ci + 0.12 + 0.76 * hcell (cx, cz, S + 1)) * C.cell, z = (cj + 0.12 + 0.76 * hcell (cx, cz, S + 2)) * C.cell;
    if (hcell (cx, cz, S) > C.prob * density (x, z)) return false;
    const double t = hcell (cx, cz, S + 3);
    const double size = C.smin + (C.smax - C.smin) * t * t;
    const int variant = (int) jsMin (variants - 1, std::floor (hcell (cx, cz, S + 4) * variants));
    const double hr = hcell (cx, cz, S + 5);
    m.x = x; m.z = z; m.size = size; m.variant = variant;
    m.hue = hr < 0.42 ? 0 : hr < 0.70 ? 1 : hr < 0.94 ? 2 : 3;
    m.phase = hcell (cx, cz, S + 6) * 2 * pi;
    m.rot = hcell (cx, cz, S + 7) * 2 * pi;
    m.lean = (hcell (cx, cz, S + 8) - 0.5) * 0.28;
    m.glow = (0.72 + 0.5 * hcell (cx, cz, S + 9)) * (0.6 + 0.4 * jsMin (1, size / 3));
    m.stalkR = stalkRadius (variant, 0.3) * size;
    m.clear = stalkRadius (variant, 0.3) * size + (C.cell > 16 ? 9.0 : 3.2);   // the walker keeps this far from the stalk
    m.capR = profileOf (variant).R * size;
    m.capY = profileOf (variant).capY * size;
    return true;
}

void WalkerWorld::instancesOf (int k, int bx, int bz, std::array<std::vector<float>, variants>& lists) const
{
    const MushroomClass& C = classes[(size_t) k];
    for (auto& l : lists) l.clear();
    Mushroom m;
    for (int j = 0; j < C.w; ++j)
        for (int i = 0; i < C.w; ++i)
            if (mushroomAt (k, bx + i, bz + j, m))
            {
                auto& l = lists[(size_t) m.variant];
                for (double v : { m.x, m.z, m.size, m.rot, (double) m.hue, m.phase, m.glow, m.lean }) l.push_back ((float) v);
            }
}

//==============================================================================
// THE DRIVE.

void WalkerWorld::gatherDiscs()
{
    const int ci = (int) std::floor (s.x / classes[1].cell), cj = (int) std::floor (s.z / classes[1].cell);
    if (discsGathered && ci == discCi && cj == discCj) return;
    discsGathered = true; discCi = ci; discCj = cj; nDiscs = 0;
    Mushroom m;
    for (int k = 1; k < numClasses; ++k)
    {
        const MushroomClass& C = classes[(size_t) k];
        const int cells = (int) std::ceil (lookReach / C.cell), bi = (int) std::floor (s.x / C.cell), bj = (int) std::floor (s.z / C.cell);
        for (int j = bj - cells; j <= bj + cells; ++j)
            for (int i = bi - cells; i <= bi + cells; ++i)
            {
                if (! mushroomAt (k, i, j, m) || nDiscs >= maxDiscs) continue;
                const Profile& pr = profileOf (m.variant);
                const double rim = (pr.capY + pr.top.front()[1]) * m.size, top = (pr.capY + pr.top.back()[1]) * m.size;
                const size_t n = (size_t) nDiscs;
                capAt[n] = rim < domeHeight + 1.5 && top > domeHeight - 1.5 ? m.capR * 1.05 + std::fabs (m.lean) * pr.capY * pr.capY * m.size + 0.5 : 0;
                discs[n * 3] = m.x; discs[n * 3 + 1] = m.z; discs[n * 3 + 2] = jsMax (m.clear, capAt[n]);
                gives[n] = k == 1 ? 1 : 0;                                   // a forest ring gives; a tower's never
                ++nDiscs;
            }
    }
}

void WalkerWorld::nearDiscs (double far)
{
    gatherDiscs();
    // The dead ends it remembers go in after the stalks, as discs only the way ahead sees (they shrink away).
    int n = nDiscs;
    for (int m = 0; m < maxDeadEnds && n < maxDiscs; ++m)
    {
        const double r = deadEndR * (1 - (s.time - deadEnds[(size_t) (m * 3 + 2)]) / deadEndSeconds);
        if (r <= 0) continue;
        discs[(size_t) (n * 3)] = deadEnds[(size_t) (m * 3)]; discs[(size_t) (n * 3 + 1)] = deadEnds[(size_t) (m * 3 + 1)]; discs[(size_t) (n * 3 + 2)] = r;
        gives[(size_t) n] = 0; capAt[(size_t) n] = 0; ++n;
    }
    nNear = 0;
    for (int i = 0; i < n; ++i)
    {
        const double dx = discs[(size_t) (i * 3)] - s.x, dz = discs[(size_t) (i * 3 + 1)] - s.z, r = far + discs[(size_t) (i * 3 + 2)] + lookMargin;
        if (dx * dx + dz * dz < r * r) nearIdx[(size_t) nNear++] = i;
    }
}

void WalkerWorld::nearestCaps (int count, std::vector<Mushroom>& out) const
{
    struct Found { Mushroom m; double score; };
    std::vector<Found> found;
    const double fx = jsSin (s.heading), fz = -jsCos (s.heading);
    static constexpr double reach[numClasses] { 14, 46, 80 };
    Mushroom m;
    for (int k = 0; k < numClasses; ++k)
    {
        const MushroomClass& C = classes[(size_t) k];
        const int cells = (int) std::ceil (reach[k] / C.cell);
        const int ci = (int) std::floor (s.x / C.cell), cj = (int) std::floor (s.z / C.cell);
        for (int j = cj - cells; j <= cj + cells; ++j)
            for (int i = ci - cells; i <= ci + cells; ++i)
            {
                if (! mushroomAt (k, i, j, m)) continue;
                const double dx = m.x - s.x, dz = m.z - s.z, d = hypot2 (dx, dz);
                if (d > reach[k] || d < 1.5) continue;
                const double ahead = (dx * fx + dz * fz) / d;
                const double score = d / (1 + 0.6 * m.size) * (1.3 - 0.7 * ahead) * (k == 0 ? 2.0 : 1.0);   // the ones in the window first
                found.push_back ({ m, score });
            }
    }
    std::stable_sort (found.begin(), found.end(), [] (const Found& a, const Found& b) { return a.score < b.score; });   // Array.sort is stable
    out.clear();
    for (size_t i = 0; i < found.size() && (int) i < count; ++i) out.push_back (found[i].m);
}

void WalkerWorld::prepFrom (double px, double pz, double graze, bool touch)
{
    nTouch = 0;
    for (int n = 0; n < nNear; ++n)
    {
        const int id = nearIdx[(size_t) n];
        const size_t i = (size_t) id * 3;
        const double dx = discs[i] - px, dz = discs[i + 1] - pz, d2 = dx * dx + dz * dz;
        const double c = discs[i + 2] - s.squeeze * gives[(size_t) id], c1 = jsMax (c + lookMargin, capAt[(size_t) id] - s.squeeze * gives[(size_t) id]);
        ox[(size_t) n] = dx; oz[(size_t) n] = dz; q2[(size_t) n] = d2;
        const double inner = d2 < c1 * c1 ? jsMin (c, std::sqrt (d2)) - graze : 0;
        k0[(size_t) n] = id >= nDiscs ? 0 : d2 >= c * c ? c : inner;     // a remembered dead end only counts for the roomy way
        k1[(size_t) n] = d2 >= c1 * c1 ? c1 : inner;
        if (touch && id < nDiscs && d2 < (c + lookTouch) * (c + lookTouch)) touched[(size_t) nTouch++] = n;   // a stalk, not a dead end
    }
}

void WalkerWorld::runsAlong (double h, double far)
{
    double fx = jsSin (h), fz = -jsCos (h), worth = 1;
    for (int pass = 0; pass < 2 && nTouch > 0; ++pass)
        for (int t = 0; t < nTouch; ++t)
        {
            const size_t n = (size_t) touched[(size_t) t];
            const double d = std::sqrt (q2[n]), nx = -ox[n] / d, nz = -oz[n] / d, into = fx * nx + fz * nz;
            if (into >= -1e-3) continue;
            if (pass > 0) { ran[0] = ran[1] = 0; return; }                 // still into a stalk: a corner
            fx -= into * nx; fz -= into * nz;
            const double l = hypot2 (fx, fz);
            if (l < 1e-3) { ran[0] = ran[1] = 0; return; }
            worth *= l; fx /= l; fz /= l;
        }
    double r0 = far, r1 = far;
    for (int n = 0; n < nNear; ++n)
    {
        const size_t u = (size_t) n;
        const double along = ox[u] * fx + oz[u] * fz, a = k0[u], b = k1[u];
        if (along <= 0 || along - b >= r1) continue;                      // k1 >= k0 nearly always, r1 <= r0
        const double lat2 = q2[u] - along * along;
        if (lat2 < b * b) r1 = jsMin (r1, jsMax (0, along - std::sqrt (b * b - lat2)));
        if (lat2 < a * a) r0 = jsMin (r0, jsMax (0, along - std::sqrt (a * a - lat2)));
    }
    ran[0] = r0 * worth; ran[1] = r1 * worth;
}

void WalkerWorld::turnsRound (int side, double rad)
{
    const double fx = jsSin (s.heading), fz = -jsCos (s.heading), rx = -fz, rz = fx, graze = jsMin (lookGraze, 2 * rad);
    double t0 = inf, t1 = inf;
    for (int n = 0; n < nNear; ++n)
    {
        // On the circle at turn phi: |p - o|^2 = rad^2 + |q|^2 - 2 rad (A cos phi + B sin phi), q = o - centre. The
        // walker stands outside the radius, so the circle's one arc inside the disc starts at phi = th - w (mod 2 pi).
        const size_t u = (size_t) n;
        const int id = nearIdx[u];
        const double qx = ox[u] - side * rad * rx, qz = oz[u] - side * rad * rz;
        const double A = -side * (qx * rx + qz * rz), B = qx * fx + qz * fz, M = hypot2 (A, B);
        if (M < 1e-9) continue;
        const double th = jsAtan2 (B, A), q = qx * qx + qz * qz;
        for (int m = id >= nDiscs ? 1 : 0; m < 2; ++m)
        {
            double r = m ? k1[u] : k0[u];
            if (r < discs[(size_t) id * 3 + 2] - s.squeeze * gives[(size_t) id]) r += 1e-6 - graze;   // standing within it: graze
            const double c = (rad * rad + q - r * r) / (2 * rad);
            if (c >= M) continue;                                           // the circle misses this disc
            const double w = c <= -M ? pi : jsAcos (c / M);
            double enter = th - w;
            enter -= 2 * pi * std::floor (enter / (2 * pi));                 // the first phi >= 0 inside
            if (m) t1 = jsMin (t1, enter); else t0 = jsMin (t0, enter);
        }
    }
    ran[0] = t0; ran[1] = t1;
}

double WalkerWorld::wayAhead (double want, double keen)
{
    const double full = s.rate * stepFor();                                 // the pace the gear asks for
    const double far = clamp (full * lookSeconds, lookMin, lookMax);
    nearDiscs (far);
    const double rad = jsMax (0.05, s.pace / yawMax);                       // the turning circle at the pace it makes
    constexpr int N = 2 * lookFan;
    const double stepA = pi / lookFan, fx = jsSin (s.heading), fz = -jsCos (s.heading);
    prepFrom (s.x, s.z, 1e-6, true);
    turnsRound (1, rad);
    const double tR0 = ran[0], tR1 = ran[1];
    turnsRound (-1, rad);
    const double tL0 = ran[0], tL1 = ran[1];
    for (int j = 0; j < N; ++j)                                             // the turn on the spot, from where it stands
    {
        runsAlong (s.heading + wrapAngle (want + (j - lookFan + 1) * stepA), far);
        runs[(size_t) j] = lookSpot * ran[0]; roomy[(size_t) j] = lookSpot * ran[1];
    }
    for (int j = 0; j < N; ++j)                                             // round the turning circle, then straight
    {
        const double o = wrapAngle (want + (j - lookFan + 1) * stepA), a = std::fabs (o);
        const int side = o >= 0 ? 1 : -1;
        const double ta0 = side > 0 ? tR0 : tL0, ta1 = side > 0 ? tR1 : tL1, arc = rad * a;
        double h0 = rad * jsMin (a, ta0), h1 = rad * jsMin (a, ta1);
        if ((a <= ta0 || a <= ta1) && arc < far)
        {
            const double px = s.x + rad * (jsSin (a) * fx - side * (1 - jsCos (a)) * fz);
            const double pz = s.z + rad * (jsSin (a) * fz + side * (1 - jsCos (a)) * fx);
            prepFrom (px, pz, 1e-6, false);
            runsAlong (s.heading + o, far - arc);
            if (a <= ta0) h0 = arc + ran[0];
            if (a <= ta1) h1 = arc + ran[1];
        }
        runs[(size_t) j] = jsMin (far, jsMax (runs[(size_t) j], h0)); roomy[(size_t) j] = jsMin (far, jsMax (roomy[(size_t) j], h1));
    }
    double longest = 0;
    for (int j = 0; j < N; ++j)
    {
        const double r3 = jsMin (jsMin (roomy[(size_t) j], roomy[(size_t) ((j + 1) % N)]), roomy[(size_t) ((j + N - 1) % N)]);
        wide[(size_t) j] = 0.5 * runs[(size_t) j] + 0.5 * r3;
        longest = jsMax (longest, wide[(size_t) j]);
    }
    const double norm = jsMax (longest, 1.0);
    double best = -inf;
    int bj = lookFan - 1;
    for (int k = 0; k < N; ++k)
    {
        const int j = lookFan - 1 + ((k + 1) >> 1) * (k & 1 ? 1 : -1);      // outward from `want`: 0, +1, -1, +2, ...
        const double turn = (j - lookFan + 1) * stepA, o = wrapAngle (want + turn);
        const double score = wide[(size_t) j] / norm - keen * std::fabs (turn) / pi - lookHold * std::fabs (wrapAngle (s.heading + o - aim)) / pi;
        if (score > best) { best = score; bj = j; }
    }
    const double off = wrapAngle (want + (bj - lookFan + 1) * stepA);
    aim = s.heading + off;
    s.brake = brakeMin + (1 - brakeMin) * smoothstep (brakeFrom, 1, wide[(size_t) bj] / jsMax (1e-3, full * brakeSeconds));
    return off;
}

//==============================================================================
int WalkerWorld::g1() const noexcept { return (int) clamp (gearWanted, 1, 3) - 1; }
double WalkerWorld::rateFor() const noexcept { return s.bpm > 0 ? s.bpm / 60 * beatSteps[(size_t) g1()] : gearRate[(size_t) g1()] * (turbo ? turboRate : 1); }
double WalkerWorld::stepFor() const noexcept { return gearStep[(size_t) g1()] * (turbo ? turboStep : 1); }
double WalkerWorld::sporesFor() const noexcept { return (turbo ? turboSpores : gearSpores)[(size_t) g1()]; }
double WalkerWorld::stormFor() const noexcept { return turbo ? turboStorm[(size_t) g1()] : 0; }

void WalkerWorld::settle() noexcept
{
    s.rate = rateFor(); s.stepLen = stepFor(); s.speed = s.rate * s.stepLen;
    s.spores = sporesFor(); s.storm = stormFor();
    s.breath = bassIn;
}

void WalkerWorld::setGear (int g) noexcept { gearWanted = (int) clamp (g == 0 ? 1 : g, 1, 3); }
void WalkerWorld::setSteer (double v) noexcept { steer = clamp (v != v ? 0 : v, -1, 1); }
void WalkerWorld::setBass (double v) noexcept { bassIn = clamp (v != v ? 0 : v, 0, 1); }
void WalkerWorld::setLevel (double v) noexcept { levelIn = clamp (v != v ? 0 : v, 0, 1); }
void WalkerWorld::setBpm (double v) noexcept { s.bpm = clamp (v != v ? 0 : v, 0, 300); }

void WalkerWorld::pushBurst (double x, double y, double z, double kind, double strength, double hue, double radius) noexcept
{
    float* b = &s.bursts[(size_t) (s.burstAt * 8)];
    s.burstAt = (s.burstAt + 1) % maxBursts;
    b[0] = (float) x; b[1] = (float) y; b[2] = (float) z; b[3] = (float) s.time;
    b[4] = (float) kind; b[5] = (float) strength; b[6] = (float) hue; b[7] = (float) radius;
}

void WalkerWorld::footfall (Leg& L) noexcept
{
    float* p = &s.prints[(size_t) (s.printAt * 4)];
    s.printAt = (s.printAt + 1) % maxPrints;
    p[0] = (float) L.foot[0]; p[1] = (float) L.foot[2]; p[2] = (float) s.time; p[3] = (float) (footR * 1.6);
    const double hueR = fract (L.foot[0] * 0.37 + L.foot[2] * 0.53);
    const double hue = hueR < 0.5 ? 0 : hueR < 0.8 ? 2 : 1;
    pushBurst (L.foot[0], L.foot[1], L.foot[2], 0, (0.55 + 0.12 * s.stepLen) * (1 + 0.9 * s.storm), hue, footR);
    s.footfall = s.time;
}

void WalkerWorld::hit (double strength)
{
    strength = clamp (strength, 0, 1);
    s.hit = jsMax (s.hit, strength); s.hitGlow = jsMax (s.hitGlow, strength);
    std::vector<Mushroom> caps;
    nearestCaps (10, caps);
    for (const Mushroom& m : caps)
        pushBurst (m.x, groundH (m.x, m.z) + m.capY, m.z, 1, strength * (0.55 + 0.45 * jsMin (1, m.size / 5)), m.hue, m.capR);
}

void WalkerWorld::restSpot (const Leg& L, double ahead, std::array<double, 3>& out) const noexcept
{
    const double fx = jsSin (s.heading), fz = -jsCos (s.heading), rx = -fz, rz = fx;
    out[0] = s.x + fx * ahead + rx * L.side * footSide;
    out[2] = s.z + fz * ahead + rz * L.side * footSide;
    out[1] = groundH (out[0], out[2]);
}

void WalkerWorld::shiftWorld (double dx, double dz) noexcept
{
    for (Leg& L : s.legs)
        for (auto* p : { &L.foot, &L.from, &L.to }) { (*p)[0] += dx; (*p)[2] += dz; }
    for (int i = 0; i < maxBursts; ++i)
    {
        float& bx = s.bursts[(size_t) (i * 8)];
        float& bz = s.bursts[(size_t) (i * 8 + 2)];
        bx = (float) ((double) bx + dx); bz = (float) ((double) bz + dz);
    }
    for (int i = 0; i < maxPrints; ++i)
    {
        float& px = s.prints[(size_t) (i * 4)];
        float& pz = s.prints[(size_t) (i * 4 + 1)];
        px = (float) ((double) px + dx); pz = (float) ((double) pz + dz);
    }
    for (int i = 0; i < maxDeadEnds; ++i) { deadEnds[(size_t) (i * 3)] += dx; deadEnds[(size_t) (i * 3 + 1)] += dz; }
}

void WalkerWorld::step (double dt)
{
    s.time += dt;
    const auto ease = [dt] (double tau) { return 1 - jsExp (-dt / tau); };
    // The pace eases; the spores and the storm too.
    s.rate += (rateFor() - s.rate) * ease (paceSeconds);
    s.stepLen += (stepFor() * s.brake - s.stepLen) * ease (paceSeconds);
    s.speed = s.rate * s.stepLen;
    s.spores += (sporesFor() - s.spores) * ease (sporeEaseSeconds);
    s.storm += (stormFor() - s.storm) * ease (stormEaseSeconds);
    // The bass: the low note's level breathes slowly; an attack is a flash and a slower glow.
    s.bass = jsMax (bassIn, levelIn);
    s.breath += (s.bass - s.breath) * ease (s.bass > s.breath ? breathAttack : breathRelease);
    if (dt > 0)
    {
        // An onset in the level input is an attack.
        const double jump = levelIn - levelLP;
        if (jump > 0.28 && s.time - lastOnset > 0.14) { lastOnset = s.time; hit (clamp (jump * 1.6, 0.3, 1)); }
        levelLP += (levelIn - levelLP) * ease (0.06);
        s.hit *= jsExp (-dt / hitFlash); s.hitGlow *= jsExp (-dt / hitGlow);
    }

    // The step phase: integer = a footfall (even: the left, odd: the right). In the tempo lock a phase-locked
    // loop pulls the footfalls onto the beats.
    double dpsi = s.rate * dt;
    if (s.bpm > 0)
    {
        s.beat = s.time * s.bpm / 60;
        const double want = s.beat * beatSteps[(size_t) g1()];
        double err = s.psi - want;
        err -= jsRound (err);
        dpsi -= clamp (err * 3.0 * dt, -0.5 * dpsi, 0.5 * dpsi);
    }
    psiPrev = s.psi; s.psi += dpsi;

    // The heave: a broad rise between footfalls, a thud just after each; sway over the standing leg; a nod. A
    // short period means a smaller heave.
    const double u = fract (s.psi), T = 1 / jsMax (s.rate, 0.05), wt = jsMin (1, (T / 0.7) * (T / 0.7));
    const double rise = jsMin (0.70, 0.13 * s.stepLen) * wt, thud = 0.42 * rise;
    s.heave = rise * stepRise (u) - thud * stepThud (u);
    const double swayA = 0.062 * s.stepLen * wt;
    s.sway = -swayA * jsSin (pi * (s.psi - 0.25));
    s.roll = 1.8 * pi / 180 * (s.stepLen / 3.9) * wt * jsSin (pi * (s.psi - 0.42));
    s.nod = (1.2 * stepRise (u) - 1.6 * stepThud (u)) * pi / 180 * jsMin (1.4, s.stepLen / 3.9) * wt;

    // The heading: the hand on the glass turns the walker; a meander, a pull back toward the sun. The way ahead
    // bends that toward a free run; turned right back, it remembers the dead end for a while.
    double want, keen;
    const bool hand = std::fabs (steer) > 0.001;
    if (hand) { want = steer * 14 * pi / 180; keen = handKeen; lastSteerTime = s.time; }
    else
    {
        const double fadeIn = smoothstep (1.5, 4.5, s.time - lastSteerTime);
        const double home = -0.6 * wrapAngle (s.heading - s.heading0);
        want = fadeIn * (3.0 * pi / 180 * jsSin (s.time * 0.11) + clamp (home, -0.25, 0.25));
        keen = freeKeen;
    }
    double off = wayAhead (want * aimSeconds, keen);
    if (! hand && std::fabs (wrapAngle (off - want * aimSeconds)) > deadEndTurn && s.time - lastDeadEnd > deadEndGap)
    {
        const double fx = jsSin (s.heading), fz = -jsCos (s.heading);
        const size_t m = (size_t) (deadEndAt * 3);
        deadEnds[m] = s.x + fx * 0.7 * deadEndR; deadEnds[m + 1] = s.z + fz * 0.7 * deadEndR; deadEnds[m + 2] = s.time;
        deadEndAt = (deadEndAt + 1) % maxDeadEnds; lastDeadEnd = s.time;
    }
    // Held, turning on the spot toward an aim nearly behind: it keeps turning the way it already turns.
    if (s.pace < squeezeHeld * s.speed && std::fabs (off) > 2 * pi / 3 && off * yawRate < 0 && std::fabs (yawRate) > 3 * pi / 180)
        off -= jsSign (off) * 2 * pi;
    const double yawTarget = clamp (off / aimSeconds, -yawMax, yawMax);
    yawRate += (yawTarget - yawRate) * ease (0.8);
    s.heading += yawRate * dt;

    const double fx = jsSin (s.heading), fz = -jsCos (s.heading), rx = -fz, rz = fx;
    const double x0 = s.x, z0 = s.z;
    s.x += fx * s.speed * dt; s.z += fz * s.speed * dt; s.travel += s.speed * dt;
    // Never through a stalk: pressed against one, it slides round it. Held fast, the keep-clear rings give a
    // little, slowly, until it squeezes past; then they ease back.
    heldFor = s.pace < squeezeHeld * s.speed ? heldFor + dt : 0;
    freeFor = s.pace > squeezeFree * s.speed ? freeFor + dt : 0;
    squeezing = squeezing ? freeFor < squeezeAfter : heldFor > squeezeAfter;
    s.squeeze = squeezing ? jsMin (squeezeMax, s.squeeze + squeezeRate * dt) : jsMax (0, s.squeeze - 0.5 * squeezeRate * dt);
    for (int pass = 0; pass < 4; ++pass)
    {
        bool moved = false;
        for (int n = 0; n < nNear; ++n)
        {
            const int id = nearIdx[(size_t) n];
            if (id >= nDiscs) continue;                                     // a remembered dead end: no stalk there
            const size_t i = (size_t) id * 3;
            const double dx = s.x - discs[i], dz = s.z - discs[i + 1], d = hypot2 (dx, dz), rr = discs[i + 2] - s.squeeze * gives[(size_t) id];
            if (d < rr - 1e-9 && d > 1e-6) { s.x = discs[i] + dx / d * rr; s.z = discs[i + 1] + dz / d * rr; moved = true; }
        }
        if (! moved) break;
    }
    if (dt > 0) s.pace += (jsMax (0, (s.x - x0) * fx + (s.z - z0) * fz) / dt - s.pace) * ease (0.4);   // its way along its heading
    if (s.x >= worldPeriod) { s.x -= worldPeriod; shiftWorld (-worldPeriod, 0); } else if (s.x < 0) { s.x += worldPeriod; shiftWorld (worldPeriod, 0); }
    if (s.z >= worldPeriod) { s.z -= worldPeriod; shiftWorld (0, -worldPeriod); } else if (s.z < 0) { s.z += worldPeriod; shiftWorld (0, worldPeriod); }

    // The body's height over the ground: the ground under the feet, low-passed; the terrain's pitch.
    const double g0 = groundH (s.x, s.z), gA = groundH (s.x + fx * 4, s.z + fz * 4), gB = groundH (s.x - fx * 3, s.z - fz * 3);
    const double targetY = (g0 * 2 + gA + gB) * 0.25;
    if (! started || dt == 0) s.groundY = targetY;
    s.groundY += (targetY - s.groundY) * ease (0.6);
    s.groundY = jsMax (s.groundY, g0 - 1.5);
    const double gb = groundH (s.x - fx * 5, s.z - fz * 5), gf = groundH (s.x + fx * 10, s.z + fz * 10);
    const double pitchT = clamp (jsAtan2 (gf - gb, 15) * 0.6, -0.10, 0.10);
    if (! started || dt == 0) s.pitch = pitchT; else s.pitch += (pitchT - s.pitch) * ease (1.2);
    s.camY = s.groundY + domeHeight + s.heave;
    s.eyeX = s.x + rx * s.sway; s.eyeZ = s.z + rz * s.sway;

    // The legs. A leg's cycle c (0..1, two steps long): on the ground to `stance`, then a swing to the next spot.
    const double tSwing = (1 - stance) * 2 / jsMax (s.rate, 0.05);
    for (Leg& L : s.legs)
    {
        const double c = fract ((s.psi + L.off) * 0.5), cp = fract ((psiPrev + L.off) * 0.5);
        if (! started)
        {
            // The first stand: each foot where it would be at this point of its cycle.
            const double along = c < stance ? (0.5 - c / stance) * 2 * stance * s.stepLen : 0;
            restSpot (L, footAhead + along, L.foot); L.swing = false;
        }
        if (! L.swing && c >= stance && cp < stance)
        {
            L.swing = true;
            L.from = L.foot;
            // A long stride lands a little less far ahead, so the leg still reaches.
            restSpot (L, footAhead - 0.3 * jsMax (0, s.stepLen - 3.9) + stance * s.stepLen + s.speed * tSwing, L.to);
            L.lift = 1.0 + 0.1 * s.stepLen;
        }
        if (L.swing && (c < cp || c < stance))
        {
            L.swing = false;
            L.foot = L.to;
            footfall (L);
        }
        if (L.swing)
        {
            const double cs = clamp ((c - stance) / (1 - stance), 0, 1), e = cs * cs * (3 - 2 * cs);
            L.pos[0] = L.from[0] + (L.to[0] - L.from[0]) * e;
            L.pos[2] = L.from[2] + (L.to[2] - L.from[2]) * e;
            L.pos[1] = L.from[1] + (L.to[1] - L.from[1]) * e + L.lift * jsSin (pi * cs);
        }
        else L.pos = L.foot;
        // The hip in the world (relative to the eye's xz), the knee from the IK: pulled up and a little out.
        const double bodyY = s.groundY + s.heave;
        L.hip[0] = (s.x - s.eyeX) + rx * (hip[0] * L.side) + fx * hip[2];
        L.hip[1] = bodyY + hip[1];
        L.hip[2] = (s.z - s.eyeZ) + rz * (hip[0] * L.side) + fz * hip[2];
        const std::array<double, 3> ankleAt { L.pos[0] - s.eyeX, L.pos[1] + ankle, L.pos[2] - s.eyeZ };
        L.knee = kneeOf (L.hip, ankleAt, femur, tibia, { rx * L.side * 0.15 + fx, 0.55, rz * L.side * 0.15 + fz });
    }
    started = true;
}

} // namespace orbitamp::redgroove
