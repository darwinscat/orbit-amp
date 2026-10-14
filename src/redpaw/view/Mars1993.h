// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace orbitamp::redpaw
{

/** THE 1993 LANDSCAPE. Tim J. Clarke's MARS.EXE, transliterated from its disassembly routine by
    routine: the same plasma fractal and random generator, the same blur taps, the same 96-entry
    palette, the same sky, perspective and far-to-near vertical-line renderer, drawn on the CPU into an
    8-bit indexed framebuffer. Where the program's own prose and its code disagree, the code wins.

    The frame is a pure function of (seed, position, rows): nothing is kept between frames but the
    buffers. The original screen is 256 x 200; here the frame has `rows` rows and 5.345 times as many
    columns, every screen constant of the original scaled by rows / 200, so at 200 rows the 256 columns
    in the middle run the original's arithmetic and the wings are the same projection carried on.

    No JUCE in here: a component shows the picture, this makes it. The integer semantics are the
    binary's — map addresses are 16-bit and wrap, row-internal wraps are 8-bit, the noise is a signed
    byte — spelled with fixed-width unsigned types so they wrap by definition, never by luck. */
class Mars1993 final
{
public:
    /** Makes the sky and the terrain from a seed, as the program did at start — a 15-bit number it
        took from the clock. About a millisecond. */
    void makeMaps (uint16_t seed);

    /** Sets the frame's height; the width follows (5.345 : 1, even). Allocates the buffers. */
    void setRows (int rows);

    /** Draws one frame from a 12.4 fixed-point map position. */
    void render (uint16_t camX, uint16_t camY);

    int width() const noexcept   { return w; }
    int height() const noexcept  { return h; }

    /** The frame, row-major palette indices, width() * height() of them. */
    const uint8_t* indices() const noexcept   { return frame.data(); }

    /** The palette, 256 colours as 0xAARRGGBB; the program's 96 and the white of its initials. */
    const uint32_t* palette() const noexcept  { return pal.data(); }

    /** Bilinear ground height under a position plus the rover's clearance, in 8.8 — the camera's
        altitude, which rides the terrain. */
    uint16_t altitudeAt (uint16_t camX, uint16_t camY) const noexcept;

private:
    std::array<uint8_t, 65536> heights {}, colours {}, sky {};
    std::array<uint32_t, 256> pal {};
    std::vector<uint8_t> frame;
    std::vector<int32_t> yBuf;
    std::vector<int32_t> cBuf;
    int w = 0, h = 0;
    double P = 1.0;          // framebuffer pixels per pixel of the original's 256 x 200 screen
    int G = 0;               // the horizon's row: the middle
    int skyRows = 0, hazeRows = 0;

    void drawSky (uint16_t alt, uint16_t camX, uint16_t camY);
    void drawGround (uint16_t alt, uint16_t camX, uint16_t camY);
};

} // namespace orbitamp::redpaw
