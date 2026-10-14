// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "CatpitArt.h"
#include "CatpitLayout.h"

#include <CatpitData.h>

#include <cmath>
#include <map>
#include <vector>

namespace orbitamp::catpit
{

namespace
{
    /** A render by its file name: the copy built into the binary — or, when CATPIT_ART_DIR names a
        folder that has one, the file from there, so a new render can be tried by restarting rather
        than rebuilding. */
    /** A picture as it was made — whatever format it decodes to. */
    juce::Image decode (const char* name)
    {
        juce::Image img;

        if (const auto dir = juce::SystemStats::getEnvironmentVariable ("CATPIT_ART_DIR", {}); dir.isNotEmpty())
            img = juce::ImageFileFormat::loadFrom (juce::File (dir).getChildFile (name));

        for (int i = 0; ! img.isValid() && i < CatpitData::namedResourceListSize; ++i)
            if (juce::String (CatpitData::getNamedResourceOriginalFilename (CatpitData::namedResourceList[i])) == name)
            {
                int size = 0;
                if (const auto* data = CatpitData::getNamedResource (CatpitData::namedResourceList[i], size))
                    img = juce::ImageFileFormat::loadFrom (data, (size_t) size);
            }

        jassert (img.isValid());   // a name that is neither on disk nor in the binary is a typo
        return img;
    }

    juce::Image load (const char* name)
    {
        const auto img = decode (name);
        return img.isValid() ? img.convertedToFormat (juce::Image::ARGB) : img;
    }

    /** Scales an image down once so its longer side is `maxSide` — the size the window can need at its
        largest on a retina screen — instead of resampling a bigger one on every paint. */
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

    /** THE TONE SOCKETS MOVE UP. The render painted the three rings lower than the cockpit wants them
        now — their tops level with DRIVE's, a row of keys under them — and until the console is rendered
        again the picture is corrected here: the bare plate from just under each ring is laid over it,
        the ring is put back `toneLift` higher as a soft-edged disc, and the divider between MIDDLE and
        TREBLE, which would run down through the keys, is cut short. Numbers are in the render's own
        1672-wide space, measured off it. */
    juce::Image raiseTones (juce::Image img)
    {
        if (! img.isValid())
            return img;

        const float k = (float) img.getWidth() / layout::width;
        const auto original = img.createCopy();
        const juce::Image::BitmapData src (original, juce::Image::BitmapData::readOnly);
        juce::Image::BitmapData dst (img, juce::Image::BitmapData::readWrite);
        const int w = img.getWidth(), h = img.getHeight();
        const auto inside = [w, h] (int x, int y) { return x >= 0 && y >= 0 && x < w && y < h; };

        const int lift = juce::roundToInt (layout::toneLift * k);
        const int cleanRows = juce::jmax (2, juce::roundToInt (16.0f * k));
        const float feather = 3.0f * k;

        for (const auto& socket : layout::tones)
        {
            const float cx = socket.centre.x * k, cy = (socket.centre.y + layout::toneLift) * k;   // where it was painted
            const float R = (socket.radius + 8.0f) * k;                                           // the ring and its glow
            const int x0 = (int) std::floor (cx - R), x1 = (int) std::ceil (cx + R);
            const int y0 = (int) std::floor (cy - R), y1 = (int) std::ceil (cy + R);
            const int clean0 = y1 + 2;                                                            // bare plate from here down

            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x)
                    if (inside (x, y) && std::hypot ((float) x - cx, (float) y - cy) <= R)
                    {
                        // the strip under the ring, mirrored back and forth so no row meets a stranger
                        const int m = (clean0 - 1 - y) % (2 * cleanRows);
                        const int row = clean0 + (m < cleanRows ? m : 2 * cleanRows - 1 - m);
                        if (inside (x, row))
                            dst.setPixelColour (x, y, src.getPixelColour (x, row));
                    }

            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x)
                {
                    const float a = juce::jlimit (0.0f, 1.0f, (R - std::hypot ((float) x - cx, (float) y - cy)) / feather);
                    if (a > 0.0f && inside (x, y) && inside (x, y - lift))
                        dst.setPixelColour (x, y - lift, dst.getPixelColour (x, y - lift)
                                                             .interpolatedWith (src.getPixelColour (x, y), a));
                }
        }

        // The MIDDLE | TREBLE divider below the knobs: the plate beside it, copied over it.
        const int dx0 = juce::roundToInt (1281.0f * k), dx1 = juce::roundToInt (1288.0f * k), from = juce::roundToInt (9.0f * k);
        for (int y = juce::roundToInt (548.0f * k); y <= juce::roundToInt (584.0f * k); ++y)
            for (int x = dx0; x <= dx1; ++x)
                if (inside (x - from, y) && inside (x, y))
                    dst.setPixelColour (x, y, dst.getPixelColour (x - from, y));

        return img;
    }

    /** THE TURBO FRAME MOVES UP, its name with it. The name stands in the frame's own top line, and
        that line was painted lower than the row the name is read in, beside DRIVE's and the tones'.
        Until the console is rendered again the picture is corrected here, the way the tone sockets are:
        the frame's box — the tab of its backplate, the brackets and their glow — is put back
        `turboLift` higher as a soft-edged rectangle, the bolt that stood in its way goes under bare
        plate, and so does the band the frame leaves at the bottom. The lift is as far as it can go:
        the tab's top comes to 3 under the plate's own edge. Numbers are in the render's 1672-wide
        space, measured off it. */
    juce::Image raiseTurbo (juce::Image img)
    {
        if (! img.isValid())
            return img;

        const float k = (float) img.getWidth() / layout::width;
        const auto original = img.createCopy();
        const juce::Image::BitmapData src (original, juce::Image::BitmapData::readOnly);
        juce::Image::BitmapData dst (img, juce::Image::BitmapData::readWrite);
        const int w = img.getWidth(), h = img.getHeight();
        const auto inside = [w, h] (int x, int y) { return x >= 0 && y >= 0 && x < w && y < h; };

        const int lift = juce::roundToInt (layout::turboLift * k);
        const int x0 = juce::roundToInt (793.0f * k), x1 = juce::roundToInt (971.0f * k);     // the box as painted
        const int y0 = juce::roundToInt (399.0f * k), y1 = juce::roundToInt (599.0f * k);
        const int clean0 = y1 + 1, cleanRows = juce::jmax (2, juce::roundToInt (5.0f * k));   // bare plate under it
        const float feather = 2.5f * k;

        const auto edge = [feather] (int x, int y, int left, int top, int right, int bottom)
        {
            return juce::jlimit (0.0f, 1.0f, (float) juce::jmin (x - left, right - x, y - top, bottom - y) / feather);
        };

        // The bolt above the frame's right shoulder: the plate beside it, laid over it.
        const float bx = 956.7f * k, by = 389.3f * k, br = 6.5f * k;
        const int from = juce::roundToInt (16.0f * k);
        for (int y = (int) std::floor (by - br); y <= (int) std::ceil (by + br); ++y)
            for (int x = (int) std::floor (bx - br); x <= (int) std::ceil (bx + br); ++x)
            {
                const float a = juce::jlimit (0.0f, 1.0f, (br - std::hypot ((float) x - bx, (float) y - by)) / feather);
                if (a > 0.0f && inside (x, y) && inside (x - from, y))
                    dst.setPixelColour (x, y, dst.getPixelColour (x, y).interpolatedWith (src.getPixelColour (x - from, y), a));
            }

        // Bare plate where the frame was: the strip under it, mirrored back and forth.
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
            {
                const int m = (clean0 - 1 - y) % (2 * cleanRows);
                const int row = clean0 + (m < cleanRows ? m : 2 * cleanRows - 1 - m);
                const float a = edge (x, y, x0, y0 - lift, x1, y1);
                if (a > 0.0f && inside (x, y) && inside (x, row))
                    dst.setPixelColour (x, y, dst.getPixelColour (x, y).interpolatedWith (src.getPixelColour (x, row), a));
            }

        // The frame, `lift` higher.
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
            {
                const float a = edge (x, y, x0, y0, x1, y1);
                if (a > 0.0f && inside (x, y) && inside (x, y - lift))
                    dst.setPixelColour (x, y - lift, dst.getPixelColour (x, y - lift)
                                                         .interpolatedWith (src.getPixelColour (x, y), a));
            }

        return img;
    }

    /** THE NAME PLATES LOSE THEIR SCREWS. The plates over the slanted lamps carry GATE and LIMITER now
        (layout::gatePlate, limiterPlate), and LIMITER, at its 13 px, is as wide as the room between its
        plate's two screws. All four go under bare plate — the plate beside each, toward the middle,
        laid over it as a soft-edged disc — so both words sit free and the two sides stay a pair.
        Numbers are in the render's 1672-wide space, measured off it. */
    juce::Image clearNamePlates (juce::Image img)
    {
        if (! img.isValid())
            return img;

        const float k = (float) img.getWidth() / layout::width;
        const auto original = img.createCopy();
        const juce::Image::BitmapData src (original, juce::Image::BitmapData::readOnly);
        juce::Image::BitmapData dst (img, juce::Image::BitmapData::readWrite);
        const int w = img.getWidth(), h = img.getHeight();
        const auto inside = [w, h] (int x, int y) { return x >= 0 && y >= 0 && x < w && y < h; };

        struct Screw { float x, y; int from; };     // `from`: where the plate is taken, beside it
        const Screw screws[] { { 25.0f, 805.0f, 22 }, { 125.0f, 805.0f, -22 }, { 1545.8f, 805.0f, 22 }, { 1645.0f, 804.5f, -22 } };
        const float r = 10.0f * k, feather = 2.5f * k;

        for (const auto& s : screws)
        {
            const float sx = s.x * k, sy = s.y * k;
            const int from = juce::roundToInt ((float) s.from * k);
            for (int y = (int) std::floor (sy - r); y <= (int) std::ceil (sy + r); ++y)
                for (int x = (int) std::floor (sx - r); x <= (int) std::ceil (sx + r); ++x)
                {
                    const float a = juce::jlimit (0.0f, 1.0f, (r - std::hypot ((float) x - sx, (float) y - sy)) / feather);
                    if (a > 0.0f && inside (x, y) && inside (x + from, y))
                        dst.setPixelColour (x, y, dst.getPixelColour (x, y).interpolatedWith (src.getPixelColour (x + from, y), a));
                }
        }
        return img;
    }

    /** THE LAMPS GO OUT. The render paints lit the lamps the cockpit now lights itself (layout::lamps).
        Each is taken out as it was painted, glow and all, to be laid back over at the strength it burns;
        in the picture it is put out — how much of a pixel is the lamp's orange is how far it is taken
        down to the dark warm grey of a lamp that is off, so the glow on the plate round it goes too.
        What the lamp did not touch stays as it was, in the picture and out of the copy. */
    juce::Image putOutLamps (juce::Image img, std::array<juce::Image, layout::numLamps>& lit)
    {
        if (! img.isValid())
            return img;

        const float k = (float) img.getWidth() / layout::width;
        for (size_t i = 0; i < layout::lamps.size(); ++i)
        {
            const auto box = (layout::lamps[i] * k).getSmallestIntegerContainer().getIntersection (img.getBounds());
            lit[i] = juce::Image (juce::Image::ARGB, juce::jmax (1, box.getWidth()), juce::jmax (1, box.getHeight()), true);
            if (box.isEmpty())
                continue;

            juce::Image::BitmapData bd (img, box.getX(), box.getY(), box.getWidth(), box.getHeight(), juce::Image::BitmapData::readWrite);
            juce::Image::BitmapData out (lit[i], juce::Image::BitmapData::writeOnly);
            for (int y = 0; y < box.getHeight(); ++y)
                for (int x = 0; x < box.getWidth(); ++x)
                {
                    const auto c = bd.getPixelColour (x, y);
                    const float r = c.getFloatRed(), g = c.getFloatGreen(), b = c.getFloatBlue();
                    const float s = juce::jlimit (0.0f, 1.0f, (r - b - 0.025f) / 0.22f);
                    if (s <= 0.0f)
                        continue;

                    const float lum = 0.3f * r + 0.59f * g + 0.11f * b;
                    const auto off = juce::Colour::fromFloatRGBA (lum * 0.30f + 0.03f, lum * 0.25f + 0.028f, lum * 0.24f + 0.03f, c.getFloatAlpha());
                    out.setPixelColour (x, y, c);
                    bd.setPixelColour (x, y, c.interpolatedWith (off, s));
                }
        }
        return img;
    }

    /** THE THROTTLE'S GRIP, OFF ITS CENTRE. The render is a whole joystick with a symmetrical T on top;
        on the flat panel only the T shows, and the cockpit wants it bigger without lying over the channel
        names — so it is rebuilt with its stem where it was and the bar reaching further to the LEFT of it
        than to the right: the left cap, a longer run of the knurled sleeve (the clean stretch beside the
        yoke, mirrored out), the middle with the yoke, a shorter run on the right, the right cap. Columns
        and rows are the render's own, 1086 px wide; `axis` is where the stem ends up across the result. */
    juce::Image offCentreGrip (const juce::Image& lever, float& axis)
    {
        if (! lever.isValid())
            return lever;

        const float s = (float) lever.getWidth() / 1086.0f;
        const auto at = [s] (int v) { return juce::roundToInt ((float) v * s); };

        const int top = at (73), barBottom = at (392), sleeveBottom = at (361), bottom = at (589);
        const int capL0 = at (49), sleeve0 = at (295), midEnd = at (648), capR0 = at (790), capR1 = at (1038);
        const int clean0 = at (300), cleanW = at (75), extra = at (101);
        const int yoke0 = at (385), yoke1 = at (710), stem = at (545);

        const int capLW = sleeve0 - capL0, midW = midEnd - sleeve0, capRW = capR1 - capR0;
        const int w = capLW + extra + midW + capRW, h = bottom - top;
        const int stemX = capLW + extra + (stem - sleeve0);
        if (w <= 0 || h <= 0 || capR1 > lever.getWidth() || bottom > lever.getHeight())
            return lever;

        juce::Image out (juce::Image::ARGB, w, h, true);
        {
            const juce::Image::BitmapData src (lever, juce::Image::BitmapData::readOnly);
            juce::Image::BitmapData dst (out, juce::Image::BitmapData::readWrite);

            const auto column = [&] (int srcX, int dstX, int y0, int y1, bool over)
            {
                if (dstX < 0 || dstX >= w)
                    return;
                for (int y = y0; y < y1; ++y)
                {
                    const auto c = src.getPixelColour (srcX, y);
                    if (! over)
                        dst.setPixelColour (dstX, y - top, c);
                    else if (c.getAlpha() > 0)
                        dst.setPixelColour (dstX, y - top, dst.getPixelColour (dstX, y - top).overlaidWith (c));
                }
            };

            for (int i = 0; i < capLW; ++i)
                column (capL0 + i, i, top, barBottom, false);
            for (int i = 0; i < extra; ++i)
            {
                const int m = (extra - 1 - i) % (2 * cleanW);      // mirrored, ending on the sleeve's first column
                column (clean0 + (m < cleanW ? m : 2 * cleanW - 1 - m), capLW + i, top, barBottom, false);
            }
            for (int i = 0; i < midW; ++i)
                column (sleeve0 + i, capLW + extra + i, top, barBottom, false);
            for (int i = 0; i < capRW; ++i)
                column (capR0 + i, capLW + extra + midW + i, top, barBottom, false);

            // The yoke under the bar, where the stem is — its right shoulder now passes under the cap.
            for (int x = yoke0; x < yoke1; ++x)
                column (x, stemX + (x - stem), x < midEnd ? barBottom : sleeveBottom, bottom, true);
        }

        axis = (float) stemX / (float) w;
        return out;
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

juce::Image stillSky (int channel)
{
    static constexpr const char* names[] { "sky-clean.jpg", "sky-edge.jpg", "sky-crunch.jpg", "sky-hi-gain.jpg" };
    return decode (names[juce::jlimit (0, 3, channel)]);
}

const Art& Art::get()
{
    static const Art art = []
    {
        Art a;
        a.bg          = putOutLamps (clearNamePlates (cutWindshield (raiseTurbo (raiseTones (load ("cockpit.png"))))), a.lampLit);
        a.knobDrive   = load ("knob-drive.png");
        a.knobTone    = load ("knob-tone.png");
        a.leverGrip   = capped (offCentreGrip (load ("throttle-grip.png"), a.leverAxis), 480);
        a.guardClosed = load ("guard-closed.png");
        a.guardOpen   = load ("guard-open.png");
        a.guardOn     = load ("guard-on.png");
        a.sliderCap   = load ("fader-cap.png");
        return a;
    }();
    return art;
}

} // namespace orbitamp::catpit
