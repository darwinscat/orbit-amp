// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "RedPawArt.h"
#include "RedPawSkin.h"

#include "../Features.h"
#include "../face/Renders.h"

namespace orbitamp::redpaw
{

namespace
{
    /** A render of the product's own, by the rover's file name (RedPawSkin.h). */
    juce::Image load (const Skin& skin, const char* name)
    {
        return face::renders::load (skin.product, name, skin.rendersFolderVariable);
    }

    /** A render only some rovers carry: none is no image, not a typo. */
    juce::Image loadOwn (const Skin& skin, const juce::String& name)
    {
        return face::renders::has (skin.product, name.toRawUTF8(), skin.rendersFolderVariable) ? load (skin, name.toRawUTF8()) : juce::Image();
    }

    /** HUM'S HOLE, PLATE (layout::Plan::learnPlate): the dashboard with the plate laid over the hole HUM's
        key would fill — the plan's stretch of plate, then the same stretch upside down, centred on the
        key and cut to a round whose last two pixels fade — or the dashboard as it is, where the plan
        names nothing to cover. The plan is in the picture's half-size coordinates, the picture as
        large as it is drawn from. */
    juce::Image coverHumHole (const juce::Image& bg, const layout::Plan& plan)
    {
        if (! bg.isValid() || plan.learnPlate.isEmpty() || plan.learnHoleSize <= 0.0f)
            return bg;

        const float k      = (float) bg.getWidth() / layout::width;
        const auto  from   = (plan.learnPlate * k).toNearestInt();
        const auto  centre = plan.learnKeys[1] * k;
        const float radius = 0.5f * plan.learnHoleSize * k;
        const int   w = from.getWidth(), h = from.getHeight();
        const int   left = juce::roundToInt (centre.x - 0.5f * (float) w), top = juce::roundToInt (centre.y - (float) h);

        auto out = bg.createCopy();
        for (int j = 0; j < 2 * h; ++j)
            for (int i = 0; i < w; ++i)
            {
                const int x = left + i, y = top + j;
                const float d = juce::Point<float> ((float) x + 0.5f, (float) y + 0.5f).getDistanceFrom (centre);
                const float a = juce::jlimit (0.0f, 1.0f, 0.5f * (radius - d));
                if (a <= 0.0f || ! out.getBounds().contains (x, y))
                    continue;
                const auto plate = bg.getPixelAt (from.getX() + i, from.getY() + (j < h ? j : 2 * h - 1 - j));
                out.setPixelAt (x, y, bg.getPixelAt (x, y).interpolatedWith (plate, a));
            }
        return out;
    }
}

const Art& Art::of (const Skin& skin)
{
    static PerProduct<const Art> each;
    return each.of (skin.product, [&skin]
    {
        const auto load    = [&skin] (const char* name) { return redpaw::load (skin, name); };
        const auto loadOwn = [&skin] (const juce::String& name) { return redpaw::loadOwn (skin, name); };
        Art a;
        a.bg         = load ("dashboard.webp");
        if constexpr (! features::hum)      // no HUM in the build (Features.h): no LEARN HUM key, and no hole for one
            a.bg = coverHumHole (a.bg, skin.plan);
        for (size_t i = 0; i < a.lampLit.size(); ++i)
            a.lampLit[i] = loadOwn ("lamp-" + juce::String ((int) i) + ".webp");
        a.knobDrive  = load ("knob-drive.webp");
        a.knobTone   = load ("knob-tone.webp");
        a.socket     = load ("socket.webp");
        a.keyFrameLeft  = load ("key-frame-left.webp");
        a.keyFrameCell  = load ("key-frame-cell.webp");
        a.keyFrameRight = load ("key-frame-right.webp");
        a.lever      = load ("lever.webp");
        a.leverGlow  = load ("lever-glow.webp");
        a.sliderCap  = load ("slider-cap.webp");
        a.faderCap   = load ("fader-cap.webp");
        a.faderCapLight = load ("fader-cap-light.webp");
        a.toggleUp   = load ("toggle-up.webp");
        a.toggleDown = load ("toggle-down.webp");
        a.keyUp      = load ("key-up.webp");
        a.keyDown    = load ("key-down.webp");
        a.turboUp    = load ("turbo-up.webp");
        a.turboDown  = load ("turbo-down.webp");
        a.emblem     = loadOwn ("emblem.webp");
        return a;
    });
}

juce::Image Art::still (const Skin& skin)
{
    return loadOwn (skin, "still.jpg");
}

juce::String Art::mark (const Skin& skin)
{
    return face::renders::text (skin.product, "mark.svg", skin.rendersFolderVariable);
}

} // namespace orbitamp::redpaw
