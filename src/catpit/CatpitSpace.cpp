// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "CatpitSpace.h"

#include <felitronics/appkit/Brand.h>

#include <cmath>

namespace orbitamp::catpit
{

namespace
{
    // Ship speed at each detent, in depths of the starfield per second — a cruise, a push, a run, and
    // the edge of light. Between detents (the lever in motion) the speeds are interpolated.
    constexpr std::array<float, 4> detentSpeed { 0.05f, 0.16f, 0.42f, 1.6f };
    constexpr int numStars = 700;

    //--------------------------------------------------------------------------
    // A value-noise field, smooth and cheap — enough to paint a cloud once, at startup.
    struct Noise
    {
        std::array<float, 256> lattice {};
        explicit Noise (juce::Random& r) { for (auto& v : lattice) v = r.nextFloat(); }

        float at (int x, int y) const noexcept
        {
            const auto h = (unsigned) (x * 374761393 + y * 668265263);
            return lattice[(h ^ (h >> 13)) & 255u];
        }

        float sample (float x, float y) const noexcept
        {
            const int xi = (int) std::floor (x), yi = (int) std::floor (y);
            const float fx = x - (float) xi, fy = y - (float) yi;
            const float u = fx * fx * (3.0f - 2.0f * fx), v = fy * fy * (3.0f - 2.0f * fy);
            const float a = at (xi, yi), b = at (xi + 1, yi), c = at (xi, yi + 1), d = at (xi + 1, yi + 1);
            return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
        }

        float fbm (float x, float y) const noexcept
        {
            float sum = 0.0f, amp = 0.5f;
            for (int o = 0; o < 5; ++o, x *= 2.03f, y *= 2.03f, amp *= 0.5f)
                sum += amp * sample (x, y);
            return sum;
        }
    };

    float smooth (float a, float b, float x) noexcept
    {
        const float t = juce::jlimit (0.0f, 1.0f, (x - a) / (b - a));
        return t * t * (3.0f - 2.0f * t);
    }

    /** One nebula, painted once: a domain-warped cloud in the family's violet, deepening to magenta
        where it is thick, with a few orange embers — and fading to nothing at its edges, so that when
        it is blown up past the window no rim ever shows. */
    juce::Image paintNebula (juce::Random& r)
    {
        constexpr int w = 480, h = 240;
        juce::Image img (juce::Image::ARGB, w, h, true);
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);

        const Noise n (r);
        const float ox = r.nextFloat() * 50.0f, oy = r.nextFloat() * 50.0f;
        const juce::Colour deep (0xff2a1260), mid (0xff6a2bb8), hot (0xffb04aa8), ember (0xffff8a3d);

        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const float u = (float) x / (float) w * 3.2f + ox, v = (float) y / (float) h * 1.6f + oy;
                const float qx = n.fbm (u, v), qy = n.fbm (u + 5.2f, v + 1.3f);
                const float d  = n.fbm (u + 3.5f * qx, v + 3.5f * qy);

                const float ex = ((float) x / (float) w - 0.5f) * 2.0f, ey = ((float) y / (float) h - 0.5f) * 2.0f;
                const float edge = 1.0f - smooth (0.55f, 1.0f, std::sqrt (ex * ex + ey * ey));

                const float density = smooth (0.42f, 0.78f, d) * edge;
                auto c = deep.interpolatedWith (mid, smooth (0.45f, 0.7f, d)).interpolatedWith (hot, smooth (0.68f, 0.9f, d));
                c = c.interpolatedWith (ember, smooth (0.80f, 0.95f, n.fbm (u * 2.0f + 9.0f, v * 2.0f)) * 0.7f);
                bd.setPixelColour (x, y, c.withAlpha (density * 0.85f));
            }
        return img;
    }
}

Space::Space()
{
    setInterceptsMouseClicks (false, false);
    setOpaque (true);

    stars.resize ((size_t) numStars);
    for (auto& s : stars)
        respawn (s, true);

    for (auto& img : nebulae)
        img = paintNebula (rng);
    clouds[1].image = 1;
    clouds[1].phase = 0.5f;
    for (auto& c : clouds)
    {
        c.angle = rng.nextFloat() * juce::MathConstants<float>::twoPi;
        c.drift = { rng.nextFloat() - 0.5f, (rng.nextFloat() - 0.5f) * 0.4f };
    }
}

void Space::respawn (Star& s, bool anywhere)
{
    // Spread wide and low: the windshield is a letterbox, and stars outside it are wasted.
    s.x = (rng.nextFloat() * 2.0f - 1.0f) * 1.6f;
    s.y = (rng.nextFloat() * 2.0f - 1.0f) * 0.45f;
    s.z = anywhere ? 0.05f + rng.nextFloat() * 0.95f : 1.0f;
    s.tint = rng.nextFloat();
}

void Space::tick()
{
    if (! isVisible())           // the GPU sky is flying instead
        return;

    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    if (now - lastFrame < 1.0 / 62.0)       // a 120 Hz display would ask twice as often as the eye needs
        return;
    lastFrame = now;
    const float dt = last > 0.0 ? (float) juce::jlimit (0.0, 0.05, now - last) : 0.0f;
    last = now;

    // The speed the lever asks for, and the ship's response: it spools up slower than it brakes.
    const float at = throttle * 3.0f;
    const int i = juce::jlimit (0, 2, (int) at);
    const float want = detentSpeed[(size_t) i] + (detentSpeed[(size_t) i + 1] - detentSpeed[(size_t) i]) * (at - (float) i);
    const float tau = want > speed ? 0.9f : 0.5f;
    speed += (want - speed) * (1.0f - std::exp (-dt / tau));

    for (auto& s : stars)
    {
        s.z -= speed * dt;
        if (s.z <= 0.03f)
            respawn (s, false);
    }

    for (auto& c : clouds)
    {
        c.phase += dt * (0.025f + speed * 0.30f);
        if (c.phase >= 1.0f)
        {
            c.phase -= 1.0f;
            c.image = (c.image + 2) % (int) nebulae.size();
            c.angle = rng.nextFloat() * juce::MathConstants<float>::twoPi;
            c.drift = { rng.nextFloat() - 0.5f, (rng.nextFloat() - 0.5f) * 0.4f };
        }
    }

    repaint();
}

void Space::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const auto c = b.getCentre();
    const float f = b.getWidth() * 0.5f;
    const float warp = juce::jlimit (0.0f, 1.0f, (speed - 0.5f) / 1.1f);

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff070616), c, juce::Colour (0xff010104), b.getTopLeft(), true));
    g.fillAll();

    // The clouds: each swells from far to past the windshield while it brightens and then fades, two
    // half a cycle apart, so one is always arriving as the other leaves.
    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
    for (const auto& cl : clouds)
    {
        const auto& img = nebulae[(size_t) cl.image];
        const float scale = 0.7f + cl.phase * cl.phase * 3.2f;
        const float alpha = std::sin (cl.phase * juce::MathConstants<float>::pi) * 0.8f;
        const float k = b.getWidth() / (float) img.getWidth() * scale;
        const auto at = c + cl.drift * b.getWidth() * 0.35f * cl.phase;
        g.setOpacity (alpha);
        g.drawImageTransformed (img, juce::AffineTransform::translation (-(float) img.getWidth() * 0.5f, -(float) img.getHeight() * 0.5f)
                                         .rotated (cl.angle * 0.15f)
                                         .scaled (k)
                                         .translated (at));
    }
    g.setOpacity (1.0f);

    // At speed, the light ahead gathers into a glow — the window filling with where you are going.
    if (warp > 0.0f)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffcfc4ff).withAlpha (0.35f * warp), c,
                                                 juce::Colours::transparentBlack, c.translated (b.getHeight() * 1.4f, 0.0f), true));
        g.fillAll();
    }

    // The stars: each a streak from where it was a moment ago to where it is, so a slow ship shows
    // points and a fast one shows lines raking out from the centre.
    const float streak = 0.03f + 0.10f * juce::jlimit (0.0f, 1.0f, speed / 1.6f);
    for (const auto& s : stars)
    {
        const juce::Point<float> head { c.x + s.x / s.z * f, c.y + s.y / s.z * f };
        if (! b.expanded (4.0f).contains (head))
            continue;

        const float zt = s.z + speed * streak;
        const juce::Point<float> tail { c.x + s.x / zt * f, c.y + s.y / zt * f };

        const float near = 1.0f - s.z;
        const float bright = juce::jlimit (0.0f, 1.0f, near * 1.35f) * smooth (1.0f, 0.88f, s.z);
        const float size = 0.5f + near * near * 2.6f;

        auto col = s.tint < 0.12f ? juce::Colour (0xffb79cff)          // a violet one
                 : s.tint < 0.18f ? juce::Colour (0xffffc89a)          // a warm one
                                  : juce::Colour (0xffe8ecff);
        col = col.interpolatedWith (juce::Colour (0xffd6e2ff), warp * 0.6f);

        g.setColour (col.withAlpha (bright * (0.35f + 0.5f * warp)));
        g.drawLine ({ tail, head }, size * 0.8f);
        g.setColour (col.withAlpha (bright));
        g.fillEllipse (juce::Rectangle<float> (size, size).withCentre (head));
    }
}

} // namespace orbitamp::catpit
