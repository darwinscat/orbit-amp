// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "Bake.h"

#include "face/Stage.h"

#include <array>
#include <cmath>
#include <vector>

/** THE COCKPIT'S PICTURES (src/catpit): its render corrected to stand the way the cockpit stands now, until
    the console is rendered again, and its throttle grip rebuilt. Numbers are in the render's own 1672-wide
    space, measured off it — the space catpit/CatpitLayout.h lays the controls out in. */
namespace orbitamp::bake
{

namespace
{
    using face::stage::width;
    using face::stage::height;

    /** WHAT THE COCKPIT'S RENDER NEEDS. */
    struct Cockpit
    {
        /** A point in the middle of the windshield, which the render paints pure black. */
        juce::Point<float> windshieldSeed;

        /** The tone knobs stand with their top edges level with DRIVE's, `toneLift` higher than the render
            painted their sockets: each socket where it stands now (CatpitLayout.h's tones) and its ring. */
        float toneLift;
        struct Socket { juce::Point<float> centre; float radius; };
        std::array<Socket, 3> tones;

        /** The TURBO frame stands `turboLift` higher than the render painted it, so that its name, printed
            in its top line, is read beside DRIVE's and the tones'. */
        float turboLift;

        /** The lamps, each in its box (CatpitLayout.h's lamps): how much of a pixel is a lit lamp. */
        std::array<juce::Rectangle<float>, 4> lamps;
        float (*lampness) (juce::Colour);
    };

    /** On the cockpit's violet panel a lamp is whatever is warmer than it is blue: how far its red stands
        over its blue says how much of a pixel is the lamp's. */
    float violetLampness (juce::Colour c)
    {
        return (c.getFloatRed() - c.getFloatBlue() - 0.025f) / 0.22f;
    }

    const Cockpit cockpit {
        { 836.0f, 164.0f },
        24.5f,
        {{
            { { 1060.3f, 476.9f }, 62.2f },    // BASS
            { { 1209.6f, 477.0f }, 62.4f },    // MIDDLE
            { { 1358.7f, 476.9f }, 61.9f },    // TREBLE
        }},
        14.0f,
        {{
            { 81.0f,   325.0f, 66.0f, 38.0f },    // the bar 93..134 x 334..340, and its glow on the seam below
            { 1525.0f, 325.0f, 67.0f, 38.0f },    // the bar 1537..1579 x 334..340, and its glow on the seam below
            { 62.0f,   824.0f, 54.0f, 49.0f },    // the slant 74..103 x 836..860
            { 1556.0f, 824.0f, 55.0f, 48.0f },    // the slant 1568..1598 x 836..859
        }},
        violetLampness,
    };

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

        const int sx = juce::roundToInt (cockpit.windshieldSeed.x / width  * (float) w);
        const int sy = juce::roundToInt (cockpit.windshieldSeed.y / height * (float) h);
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
        TREBLE, which would run down through the keys, is cut short. */
    juce::Image raiseTones (juce::Image img)
    {
        if (! img.isValid())
            return img;

        const float k = (float) img.getWidth() / width;
        const auto original = img.createCopy();
        const juce::Image::BitmapData src (original, juce::Image::BitmapData::readOnly);
        juce::Image::BitmapData dst (img, juce::Image::BitmapData::readWrite);
        const int w = img.getWidth(), h = img.getHeight();
        const auto inside = [w, h] (int x, int y) { return x >= 0 && y >= 0 && x < w && y < h; };

        const int lift = juce::roundToInt (cockpit.toneLift * k);
        const int cleanRows = juce::jmax (2, juce::roundToInt (16.0f * k));
        const float feather = 3.0f * k;

        for (const auto& socket : cockpit.tones)
        {
            const float cx = socket.centre.x * k, cy = (socket.centre.y + cockpit.toneLift) * k;   // where it was painted
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
        the tab's top comes to 3 under the plate's own edge. */
    juce::Image raiseTurbo (juce::Image img)
    {
        if (! img.isValid())
            return img;

        const float k = (float) img.getWidth() / width;
        const auto original = img.createCopy();
        const juce::Image::BitmapData src (original, juce::Image::BitmapData::readOnly);
        juce::Image::BitmapData dst (img, juce::Image::BitmapData::readWrite);
        const int w = img.getWidth(), h = img.getHeight();
        const auto inside = [w, h] (int x, int y) { return x >= 0 && y >= 0 && x < w && y < h; };

        const int lift = juce::roundToInt (cockpit.turboLift * k);
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
        (CatpitLayout.h's gatePlate, limiterPlate), and LIMITER, at its 13 px, is as wide as the room
        between its plate's two screws. All four go under bare plate — the plate beside each, toward the
        middle, laid over it as a soft-edged disc — so both words sit free and the two sides stay a pair. */
    juce::Image clearNamePlates (juce::Image img)
    {
        if (! img.isValid())
            return img;

        const float k = (float) img.getWidth() / width;
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

Pictures bakeCockpit (const Sources& sources, float& gripAxis)
{
    Pictures out;

    auto bg = clearNamePlates (cutWindshield (raiseTurbo (raiseTones (sources.load ("cockpit.png")))));
    std::array<juce::Image, 4> lit;
    if (bg.isValid())
        for (size_t i = 0; i < lit.size(); ++i)
            lit[i] = putOutLamp (bg, cockpit.lamps[i], width, cockpit.lampness);
    out.push_back ({ "cockpit.webp", bg });
    for (size_t i = 0; i < lit.size(); ++i)
        out.push_back ({ "lamp-" + juce::String ((int) i) + ".webp", lit[i] });

    gripAxis = 0.5f;
    out.push_back ({ "throttle-grip.webp", capped (offCentreGrip (sources.load ("throttle-grip.png"), gripAxis), 480) });

    for (const char* name : { "knob-drive", "knob-tone", "guard-closed", "guard-open", "guard-on", "fader-cap" })
        out.push_back ({ juce::String (name) + ".webp", sources.load (juce::String (name) + ".png") });

    return out;
}

} // namespace orbitamp::bake
