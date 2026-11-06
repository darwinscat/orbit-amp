// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "Bake.h"

#include "face/Stage.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

/** THE WALKER'S PICTURE (src/redgroove): its dashboard under the glass dome, painted with every socket, slot and hole
    empty. What the face draws over it that the picture paints too comes out of it here: the chrome rims round the
    knobs' sockets — DRIVE's, COMP's, BLEND's, DI/CAB's and the three tones' — for the face draws a ring of its own round
    every knob (face::KnobRing), and a painted rim beside it is a second one. The sockets' recesses stay: the bass's
    standard always has its three tones, and its knobs stand in them. The slots stay as painted. Numbers are in the
    stage's 1672-wide space, measured off the picture. */
namespace orbitamp::bake
{

namespace
{
    using face::stage::width;

    /** A SOCKET AS PAINTED: roughly where its middle is and how far its dark recess reaches — the circle its rim is
        fitted to from there (fitRecess) — and how wide its chrome rim is, from where the fit finds the recess's edge
        outward, its lit lip and its outer slope included, as far as the dark groove round it. */
    struct Socket
    {
        const char* name;
        juce::Point<float> near;
        float recess, rim;
    };

    const std::array<Socket, 7> sockets {{
        { "drive",  { 418.5f,  792.0f }, 54.75f, 11.0f },
        { "comp",   { 288.8f,  794.5f }, 32.0f,  8.0f },
        { "blend",  { 532.0f,  794.0f }, 32.0f,  8.0f },
        { "diCab",  { 1281.0f, 828.0f }, 30.75f, 7.0f },
        { "bass",   { 646.8f,  766.0f }, 32.0f,  7.5f },
        { "middle", { 753.8f,  766.0f }, 32.0f,  7.5f },
        { "treble", { 860.0f,  766.0f }, 32.0f,  7.5f },
    }};

    /** Chrome on dark gunmetal is what is bright. */
    float brightness (juce::Colour c)
    {
        return 0.3f * c.getFloatRed() + 0.59f * c.getFloatGreen() + 0.11f * c.getFloatBlue();
    }

    /** The picture's brightness at a point, between its pixels. */
    float brightnessAt (const juce::Image::BitmapData& d, juce::Point<float> p)
    {
        const auto at = [&d] (int x, int y) { return brightness (d.getPixelColour (juce::jlimit (0, d.width - 1, x), juce::jlimit (0, d.height - 1, y))); };
        const int x = (int) std::floor (p.x), y = (int) std::floor (p.y);
        const float fx = p.x - (float) x, fy = p.y - (float) y;
        const float top = at (x, y) + (at (x + 1, y) - at (x, y)) * fx;
        const float bottom = at (x, y + 1) + (at (x + 1, y + 1) - at (x, y + 1)) * fx;
        return top + (bottom - top) * fy;
    }

    /** THE RECESS'S EDGE, FITTED: round the socket, a ray every half degree from the middle as last fitted, the first
        point out of the recess's dark — brighter by `step` than the recess just inside it — where the rim's lip
        begins; and the circle nearest all of them (least squares, Kasa's). Twice, the second from the first's middle.
        In the stage's space; `k` is the picture's pixels to it. */
    struct Circle { juce::Point<float> centre; float radius; };

    Circle fitRecess (const juce::Image& img, const Socket& s, float k)
    {
        const juce::Image::BitmapData d (img, juce::Image::BitmapData::readOnly);
        constexpr float step = 0.12f;
        Circle fit { s.near, s.recess };
        for (int pass = 0; pass < 2; ++pass)
        {
            const auto c = fit.centre * k;
            const float r0 = fit.radius * k;
            double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0, sxz = 0, syz = 0, sz = 0;
            int n = 0;
            for (int i = 0; i < 720; ++i)
            {
                const float t = juce::MathConstants<float>::twoPi * (float) i / 720.0f;
                const juce::Point<float> u { std::cos (t), std::sin (t) };
                const float inside = brightnessAt (d, c + u * (r0 - 5.0f * k));
                for (float r = r0 - 4.0f * k; r <= r0 + 4.0f * k; r += 0.25f)
                    if (brightnessAt (d, c + u * r) > inside + step)
                    {
                        // Relative to the middle as last fitted, so the sums stay small.
                        const double x = (double) (u.x * r), y = (double) (u.y * r), z = x * x + y * y;
                        sx += x; sy += y; sxx += x * x; syy += y * y; sxy += x * y; sxz += x * z; syz += y * z; sz += z;
                        ++n;
                        break;
                    }
            }
            if (n < 90)
                break;      // too few points to say where it is: as it was given

            // x^2 + y^2 + D x + E y + F = 0, its D, E, F by Cramer's rule.
            const double a[3][3] { { sxx, sxy, sx }, { sxy, syy, sy }, { sx, sy, (double) n } };
            const double b[3] { -sxz, -syz, -sz };
            const auto det = [] (const double m[3][3])
            {
                return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
                     + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
            };
            const double whole = det (a);
            if (std::abs (whole) < 1.0e-9)
                break;
            double v[3];
            for (int col = 0; col < 3; ++col)
            {
                double m[3][3];
                for (int r = 0; r < 3; ++r)
                    for (int q = 0; q < 3; ++q)
                        m[r][q] = q == col ? b[r] : a[r][q];
                v[col] = det (m) / whole;
            }
            const double cx = -0.5 * v[0], cy = -0.5 * v[1];
            fit.centre = (c + juce::Point<float> ((float) cx, (float) cy)) / k;
            fit.radius = (float) std::sqrt (cx * cx + cy * cy - v[2]) / k;
        }
        return fit;
    }
}

bool isWalker (const juce::String& face)
{
    return face == "redgroove";
}

Pictures bakeWalker (const Sources& sources, std::vector<std::pair<juce::String, juce::Point<float>>>& found)
{
    Pictures out;
    auto dashboard = sources.load ("dashboard.png");
    found.clear();
    if (dashboard.isValid())
    {
        const float k = (float) dashboard.getWidth() / width;
        for (const auto& s : sockets)
        {
            // Fitted on the picture as painted, every rim still in it; then its rim is taken out, from a pixel of the
            // stage inside the fitted edge — the fit finds the lip, which begins a hair out of the dark — to a quarter
            // past its outer slope: the band across it becomes the recess's dark inside and the groove's outside.
            const auto recess = fitRecess (dashboard, s, k);
            const float from = recess.radius - 1.0f, to = recess.radius + s.rim + 0.25f;
            found.push_back ({ s.name, recess.centre });
            std::printf ("redgroove socket %-8s its recess %.2f across its middle, the rim out to %.2f\n", s.name,
                         (double) recess.radius, (double) (recess.radius + s.rim));
            dashboard = eraseRing (dashboard, { recess.centre, 0.5f * (from + to), 0.5f, 0.5f * (to - from) }, k, brightness);
        }
    }
    out.push_back ({ "dashboard.webp", dashboard });
    return out;
}

} // namespace orbitamp::bake
