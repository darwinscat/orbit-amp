// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "CatpitArt.h"
#include "CatpitLayout.h"

#include <map>
#include <vector>

#ifndef CATPIT_ART_DIR
 #define CATPIT_ART_DIR ""
#endif

namespace orbitamp::catpit
{

namespace
{
    juce::Image load (const char* name)
    {
        const auto f = juce::File (CATPIT_ART_DIR).getChildFile (name);
        auto img = juce::ImageFileFormat::loadFrom (f);
        if (! img.isValid())
            DBG ("catpit: no art at " << f.getFullPathName());
        return img.isValid() ? img.convertedToFormat (juce::Image::ARGB) : img;
    }

    /** Scales a render down once so its longer side is `maxSide` — the size the window can need at its
        largest on a retina screen — instead of resampling a 1254-pixel source on every paint. */
    juce::Image capped (juce::Image img, int maxSide)
    {
        if (! img.isValid() || juce::jmax (img.getWidth(), img.getHeight()) <= maxSide)
            return img;
        const double k = (double) maxSide / (double) juce::jmax (img.getWidth(), img.getHeight());
        return img.rescaled (juce::roundToInt (img.getWidth() * k), juce::roundToInt (img.getHeight() * k),
                             juce::Graphics::highResamplingQuality);
    }

    /** THE WINDSHIELD BECOMES A HOLE. The render paints it pure black; the region connected to a seed in
        its middle that stays near-black is made transparent, so space can be drawn underneath and the
        frame sits over its edges. A flood fill rather than a colour key: the panels carry black too. */
    juce::Image cutWindshield (juce::Image img)
    {
        if (! img.isValid())
            return img;

        const int w = img.getWidth(), h = img.getHeight();
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::readWrite);

        const auto dark = [&bd] (int x, int y)
        {
            const auto c = bd.getPixelColour (x, y);
            return juce::jmax (c.getRed(), c.getGreen(), c.getBlue()) < 14;
        };

        const int sx = juce::roundToInt (layout::windshieldSeed.x / layout::width  * (float) w);
        const int sy = juce::roundToInt (layout::windshieldSeed.y / layout::height * (float) h);
        if (! dark (sx, sy))
            return img;

        std::vector<uint8_t> seen ((size_t) w * (size_t) h, 0);
        std::vector<juce::Point<int>> stack { { sx, sy } };
        seen[(size_t) sy * (size_t) w + (size_t) sx] = 1;

        while (! stack.empty())
        {
            const auto p = stack.back();
            stack.pop_back();
            bd.setPixelColour (p.x, p.y, juce::Colours::transparentBlack);

            const juce::Point<int> next[] { { p.x + 1, p.y }, { p.x - 1, p.y }, { p.x, p.y + 1 }, { p.x, p.y - 1 } };
            for (const auto& q : next)
            {
                if (q.x < 0 || q.y < 0 || q.x >= w || q.y >= h)
                    continue;
                auto& s = seen[(size_t) q.y * (size_t) w + (size_t) q.x];
                if (s == 0 && dark (q.x, q.y))
                {
                    s = 1;
                    stack.push_back (q);
                }
            }
        }
        return img;
    }

    /** The throttle render is a whole joystick; on a flat panel only its grip shows above the slot. */
    juce::Image gripOf (const juce::Image& lever)
    {
        if (! lever.isValid())
            return lever;
        const auto b = opaqueBounds (lever).toNearestInt();
        return lever.getClippedImage (b.withHeight (juce::roundToInt ((float) b.getHeight() * 0.40f))).createCopy();
    }
}

juce::Rectangle<float> opaqueBounds (const juce::Image& img)
{
    if (! img.isValid())
        return {};

    const juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly);
    int x0 = img.getWidth(), y0 = img.getHeight(), x1 = -1, y1 = -1;
    for (int y = 0; y < img.getHeight(); ++y)
        for (int x = 0; x < img.getWidth(); ++x)
            if (bd.getPixelColour (x, y).getAlpha() > 128)
            {
                x0 = juce::jmin (x0, x); y0 = juce::jmin (y0, y);
                x1 = juce::jmax (x1, x); y1 = juce::jmax (y1, y);
            }
    return x1 < 0 ? juce::Rectangle<float>()
                  : juce::Rectangle<float> ((float) x0, (float) y0, (float) (x1 - x0 + 1), (float) (y1 - y0 + 1));
}

void drawFitted (juce::Graphics& g, const juce::Image& img, juce::Rectangle<float> area, float alpha)
{
    if (! img.isValid() || alpha <= 0.0f)
        return;

    // The opaque box is measured once per image and remembered by the image's pixel data pointer.
    static std::map<const void*, juce::Rectangle<float>> boxes;
    auto it = boxes.find (img.getPixelData().get());
    if (it == boxes.end())
        it = boxes.emplace (img.getPixelData().get(), opaqueBounds (img)).first;

    const auto src = it->second;
    const float k  = juce::jmin (area.getWidth() / src.getWidth(), area.getHeight() / src.getHeight());
    const auto  dst = juce::Rectangle<float> (src.getWidth() * k, src.getHeight() * k).withCentre (area.getCentre());

    g.setOpacity (alpha);
    g.drawImageTransformed (img, juce::AffineTransform::translation (-src.getX(), -src.getY())
                                     .scaled (k)
                                     .translated (dst.getX(), dst.getY()));
    g.setOpacity (1.0f);
}

const Art& Art::get()
{
    static const Art art = []
    {
        Art a;
        a.bg          = cutWindshield (load ("bg.png"));
        a.knobDrive   = capped (load ("knob-drive.png"),   640);
        a.knobTone    = capped (load ("knob-tone.png"),    480);
        a.leverGrip   = capped (gripOf (load ("lever.png")), 420);
        a.guardClosed = capped (load ("guard-closed.png"), 560);
        a.guardOpen   = capped (load ("guard-open.png"),   560);
        a.guardOn     = capped (load ("guard-on.png"),     560);
        a.sliderCap   = capped (load ("slider-cap.png"),   360);
        return a;
    }();
    return art;
}

} // namespace orbitamp::catpit
