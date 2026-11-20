// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace orbitamp::california::cap
{

/** THE MAKER'S CAP, PAINTED ORANGE (the owner, 20.11: the cat dark on the cream cap read inverted). dashboard.webp has
    the octagonal cap on plate_maker in the van's cream; every reader of the picture repaints it, in the pixels, before
    it is drawn: the face's own (CaliforniaFaceArt, so the live face, its stills and the web kit's cabin.webp) and the
    world's (CaliforniaGl, so the app's live frames and the site's world with its cabin). The file stays as catbay
    rendered it.

    Each pixel of the cap keeps its light: its brightness against the cap's cream sets how far along a ramp from dark
    to the orange it goes, and past the cream's own brightness toward a warm white — so the bevel, the screws, the
    rim and the sheen are where they were, in orange. The cat is engraved over it in cream (CaliforniaEditor). */

/** The cap's face on the 1672 x 941 stage, the same octagon as layout::emblemFace (CaliforniaLayout.h; the face's
    art checks they agree). Clockwise from the top left. */
inline constexpr float octagon[8][2] {
    { 790.0f, 645.0f }, { 882.0f, 645.0f }, { 898.0f, 662.0f }, { 898.0f, 736.0f },
    { 881.0f, 753.0f }, { 791.0f, 753.0f }, { 774.0f, 736.0f }, { 774.0f, 662.0f } };

inline constexpr float stageWidth = 1672.0f, stageHeight = 941.0f;

/** The orange the cap is painted (straight sRGB), and the cream it was rendered in: the cap's face at its middle. */
inline constexpr std::uint8_t orange[3] { 0xd2, 0x6a, 0x2a };
inline constexpr float cream[3] { 237.0f, 227.0f, 216.0f };
inline constexpr std::uint8_t warmWhite[3] { 0xff, 0xf2, 0xdc };

namespace detail
{
    inline bool inside (float x, float y) noexcept
    {
        // Clockwise in y-down space: a point inside lies right of every edge (the cross product not negative).
        for (int i = 0; i < 8; ++i)
        {
            const auto* a = octagon[i];
            const auto* b = octagon[(i + 1) % 8];
            if ((b[0] - a[0]) * (y - a[1]) - (b[1] - a[1]) * (x - a[0]) < 0.0f)
                return false;
        }
        return true;
    }

    inline float luma (float r, float g, float b) noexcept { return 0.2126f * r + 0.7152f * g + 0.0722f * b; }
}

/** Repaints the cap in a picture of the whole stage, `width` x `height` pixels, the rows `lineStride` bytes apart and
    the pixels `pixelStride`, the red, green and blue at byte `r`, `g` and `b` of a pixel and its alpha at `a`. Only
    opaque pixels are touched (the cap is; a premultiplied picture is then the same as a straight one), and the cap's
    edge is covered in sixteenths, so it stays as smooth as it was rendered. */
inline void paint (std::uint8_t* pixels, int width, int height, std::ptrdiff_t lineStride, int pixelStride,
                   int r, int g, int b, int a) noexcept
{
    if (pixels == nullptr || width <= 0 || height <= 0)
        return;
    const float kx = (float) width / stageWidth, ky = (float) height / stageHeight;
    float left = octagon[0][0], right = left, top = octagon[0][1], bottom = top;
    for (const auto* p : octagon)
    {
        left = std::min (left, p[0]); right = std::max (right, p[0]);
        top = std::min (top, p[1]);   bottom = std::max (bottom, p[1]);
    }
    const int x0 = std::max (0, (int) (left * kx) - 1), x1 = std::min (width - 1, (int) (right * kx) + 1);
    const int y0 = std::max (0, (int) (top * ky) - 1), y1 = std::min (height - 1, (int) (bottom * ky) + 1);
    const float creamLuma = detail::luma (cream[0], cream[1], cream[2]);

    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
        {
            int covered = 0;
            for (int sy = 0; sy < 4; ++sy)
                for (int sx = 0; sx < 4; ++sx)
                    covered += detail::inside (((float) x + ((float) sx + 0.5f) * 0.25f) / kx,
                                               ((float) y + ((float) sy + 0.5f) * 0.25f) / ky) ? 1 : 0;
            auto* px = pixels + y * lineStride + x * pixelStride;
            if (covered == 0 || px[a] != 255)
                continue;
            const float cover = (float) covered / 16.0f;
            const float light = detail::luma (px[r], px[g], px[b]) / creamLuma;
            const int at[3] { r, g, b };
            for (int c = 0; c < 3; ++c)
            {
                const float painted = light <= 1.0f ? orange[c] * light
                                                    : orange[c] + (warmWhite[c] - orange[c]) * std::min (1.0f, (light - 1.0f) * 4.0f);
                const float mixed = px[at[c]] + (painted - px[at[c]]) * cover;
                px[at[c]] = (std::uint8_t) std::clamp ((int) (mixed + 0.5f), 0, 255);
            }
        }
}

/** The same over a straight RGBA picture, rows packed. */
inline void paintRgba (std::uint8_t* rgba, int width, int height) noexcept
{
    paint (rgba, width, height, (std::ptrdiff_t) width * 4, 4, 0, 1, 2, 3);
}

} // namespace orbitamp::california::cap
