// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <juce_graphics/juce_graphics.h>

#include <cmath>
#include <vector>

namespace orbitamp::face
{

/** A ROW OF AS MANY AS THE PACK HAS: the room on a face where the playing channel's tone knobs stand, or
    its keys — as many as its pack has tones, or switches a hand may throw, and so another count on
    another channel. A face's plan says the room — where the painted ones stood before the picture lost
    them — and the row is laid in it by one rule, for every face and for the site (lay):

    - the items stand in one line along the room's middle, `pitch` apart, centred in it;
    - where that row would be wider than the room, every item and every gap shrinks alike until it fits;
    - but never under `minScale` of their size: past that, the row keeps that size, and the items that
      would not fit any more are not shown — the first so many are.

    `size` is how wide one item is at its nominal size, and what the row's ends are measured to — a
    knob's socket; a key's cell of the frame, with the frame's caps. */
struct Strip
{
    juce::Rectangle<float> area;    // the room, the row along its middle
    float pitch = 0.0f;             // centre to centre, at the nominal size
    float size = 0.0f;              // one item's width at the nominal size
    float minScale = 1.0f;          // the smallest the row may shrink to, as a share of nominal

    /** How many it would show of `count` at the smallest it may be. */
    int fitsAtMost() const noexcept
    {
        if (pitch <= 0.0f || size <= 0.0f)
            return 0;
        const float s = minScale;
        return juce::jmax (0, 1 + (int) std::floor ((area.getWidth() / s - size) / pitch + 1.0e-4f));
    }
};

/** The row as laid: the centre of each item that is shown, in order, and the scale they are drawn at. */
struct Laid
{
    std::vector<juce::Point<float>> centres;
    float scale = 1.0f;
};

/** THE RULE (Strip): `count` items in `strip`. Dumb — the plan and a count in, the places out; the face
    draws there, takes the hand there and tells the site the same places. */
inline Laid lay (const Strip& strip, int count)
{
    Laid laid;
    if (count <= 0 || strip.pitch <= 0.0f || strip.size <= 0.0f)
        return laid;

    const auto span = [&strip] (int n, float s) { return s * ((float) (n - 1) * strip.pitch + strip.size); };
    const float room = strip.area.getWidth();

    // A row that fits at its nominal size, to a hair, is drawn at it: a plan's room is measured to its row.
    float scale = span (count, 1.0f) <= room + 1.0e-3f ? 1.0f : room / span (count, 1.0f);
    int shown = count;
    if (scale < strip.minScale)
    {
        scale = strip.minScale;
        shown = juce::jmin (count, strip.fitsAtMost());
    }

    const float step = strip.pitch * scale;
    const float first = strip.area.getCentreX() - 0.5f * step * (float) (shown - 1);
    for (int i = 0; i < shown; ++i)
        laid.centres.push_back ({ first + step * (float) i, strip.area.getCentreY() });
    laid.scale = scale;
    return laid;
}

/** THE PLAYING CHANNEL'S ROWS AS THE FACE SHOWS THEM (Console::layStrips): each tone knob's box and the
    box its name is written in, the pack's tone it plays and the parameter that is; and every key under
    them, the two filters first, then the right group — each key's box, its name's box, its name, and
    whether it answers the hand — and the frame the right group stands in, where the face draws one.
    What the picture paints, what the controls stand on and what the site is told are all read off this. */
struct Strips
{
    struct Tone
    {
        juce::Rectangle<float> knob, label;
        juce::String name;              // the pack's own word for it, as written on the face
        int tone = 0;                   // its place in the pack's list
        juce::String param;             // the parameter it turns
        std::vector<float> curve;       // its sign: the tone's response, 0..1 foot to top, left to right (Console::toneCurve)
        bool operator== (const Tone&) const = default;
    };
    struct Key
    {
        juce::Rectangle<float> box, label;
        juce::String name;
        bool live = true;
        bool onKey = false;             // its name on its face rather than round it
        bool operator== (const Key&) const = default;
    };

    std::vector<Tone> tones;
    float toneScale = 1.0f;
    float toneSignHeight = 16.0f;       // how tall each tone's sign is drawn (face::drawToneSign)
    std::vector<Key> keys;
    int firstRightKey = 2;              // the right group's first, in keys
    juce::Rectangle<float> keyFrame;    // round the right group; empty where there is none to draw
    float keyScale = 1.0f;
    bool operator== (const Strips&) const = default;
};

} // namespace orbitamp::face
