// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "CatpitSpace.h"

#include "CatpitArt.h"

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

    // A turn's sweep at full lock, in the stars' own measure a second — the windshield's half-width
    // at a depth of 1: a slide at the cruise, nearly four times that at the edge of light. A star is
    // swept across the glass as much faster as it is nearer. The clouds are swept by this share of it,
    // a near one, larger, faster than a far one.
    constexpr float slideCruise = 0.06f, slidePerSpeed = 0.1f;
    constexpr float cloudSlide = 0.8f;
    // The stars are spread this far either side; one swept past it comes back in on the other side,
    // well out of sight at any depth.
    constexpr float starsReach = 1.6f;

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
    s.x = (rng.nextFloat() * 2.0f - 1.0f) * starsReach;
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

    // ...and the helm's: the turn sweeps the stars and the clouds the other way.
    helm.step (steer, dt);
    slide = helm.turn * (slideCruise + slidePerSpeed * speed);

    for (auto& s : stars)
    {
        s.z -= speed * dt;
        s.x -= slide * dt;
        if (s.x < -starsReach)     s.x += 2.0f * starsReach;
        else if (s.x > starsReach) s.x -= 2.0f * starsReach;
        if (s.z <= 0.03f)
            respawn (s, false);
    }

    for (auto& c : clouds)
    {
        c.phase += dt * (0.025f + speed * 0.30f);
        c.sweep -= slide * cloudSlide * (0.7f + c.phase * c.phase * 3.2f) * dt;    // as near as it is large: see paint
        if (c.phase >= 1.0f)
        {
            c.phase -= 1.0f;
            c.sweep = 0.0f;
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
    const auto w = window.isEmpty() ? b : window;      // what the picture is composed for; `b` is what is painted
    const auto c = w.getCentre().translated (helm.focus() * w.getHeight(), 0.0f);   // where the stars stream out of
    const float f = w.getWidth() * 0.5f;
    const float warp = juce::jlimit (0.0f, 1.0f, (speed - 0.5f) / 1.1f);

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff070616), c, juce::Colour (0xff010104), c + (w.getTopLeft() - w.getCentre()), true));
    g.fillAll();

    // The bank: everything from here on is the sky turned against it, about where the ship is headed.
    const auto banked = juce::AffineTransform::rotation (-helm.roll(), c.x, c.y);
    g.addTransform (banked);

    // The clouds: each swells from far to past the windshield while it brightens and then fades, two
    // half a cycle apart, so one is always arriving as the other leaves.
    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
    for (const auto& cl : clouds)
    {
        const auto& img = nebulae[(size_t) cl.image];
        const float scale = 0.7f + cl.phase * cl.phase * 3.2f;
        const float alpha = std::sin (cl.phase * juce::MathConstants<float>::pi) * 0.8f;
        const float k = w.getWidth() / (float) img.getWidth() * scale;
        const auto at = c + cl.drift * w.getWidth() * 0.35f * cl.phase + juce::Point<float> (cl.sweep * f, 0.0f);
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
                                                 juce::Colours::transparentBlack, c.translated (w.getHeight() * 1.4f, 0.0f), true));
        g.fillAll();
    }

    // The stars: each a streak from where it was a moment ago to where it is, so a slow ship shows
    // points and a fast one shows lines raking out from the centre — and, in a turn, swept sideways.
    const float streak = 0.03f + 0.10f * juce::jlimit (0.0f, 1.0f, speed / 1.6f);
    for (const auto& s : stars)
    {
        const juce::Point<float> head { c.x + s.x / s.z * f, c.y + s.y / s.z * f };
        if (! b.expanded (4.0f).contains (head.transformedBy (banked)))
            continue;

        const float zt = s.z + speed * streak;
        const juce::Point<float> tail { c.x + (s.x + slide * streak) / zt * f, c.y + s.y / zt * f };

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

//==============================================================================
StillSky::StillSky()
{
    setInterceptsMouseClicks (false, false);
    setOpaque (true);
}

void StillSky::setChannel (int c)
{
    if (c == channel)
        return;
    channel = c;
    repaint();
}

void StillSky::visibilityChanged()
{
    if (! isVisible())
    {
        picture = {};
        fitted = {};
        pictureOf = -1;
    }
}

void StillSky::paint (juce::Graphics& g)
{
    // The sky's own darkest, under the picture: the picture is set on whole pixels, which can leave a
    // hair of the glass's edge to it.
    g.fillAll (juce::Colour (0xff010104));

    if (pictureOf != channel)
    {
        picture = stillSky (channel);
        pictureOf = channel;
        fitted = {};
    }
    if (! picture.isValid())
        return;

    // Fitted once to the pixels the glass has on this screen, then copied: a picture resampled on every
    // paint would cost what a still sky is there to save. And copied ONTO the pixel grid — the glass
    // stands wherever the stage's scale puts it, and half a pixel off the grid the copy would be a
    // blur of two rows.
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();      // pixels to a unit of ours
    const int w = juce::roundToInt ((float) getWidth() * scale), h = juce::roundToInt ((float) getHeight() * scale);
    if (w <= 0 || h <= 0)
        return;
    if (fitted.getWidth() != w || fitted.getHeight() != h)
        fitted = picture.rescaled (w, h, juce::Graphics::highResamplingQuality);

    const auto* top = getTopLevelComponent();
    const auto at = top->getLocalPoint (this, juce::Point<float>());
    const float unit = top->getLocalPoint (this, juce::Point<float> (1.0f, 0.0f)).x - at.x;   // the window's points to a unit of ours
    const float perPoint = unit > 0.0f ? scale / unit : 1.0f;                                 // ...and pixels to a point
    const juce::Point<float> onGrid { std::round (at.x * perPoint) - at.x * perPoint,
                                      std::round (at.y * perPoint) - at.y * perPoint };

    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
    g.drawImage (fitted, juce::Rectangle<float> (onGrid.x, onGrid.y, (float) w, (float) h) / scale);
}

} // namespace orbitamp::catpit
