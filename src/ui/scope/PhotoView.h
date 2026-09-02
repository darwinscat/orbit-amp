// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../Theme.h"

namespace orbitamp::scope
{

/** The BOX — the photograph the pack ships of the thing being played.

    The only view here that draws no measurement. Everything else in this folder answers "what did
    it DO"; this one answers "what IS it", which the eye settles in a glance the paper page needs
    four lines for. A pack states its picture in `rig.json` and ships it in the root — a cut-out
    with the background gone and the alpha kept, so it needs no frame, no card and no drop shadow:
    the well behind it IS the background.

    NOTHING ELSE. The name stood over it for one build and was struck: the combo on the block's
    border already says which device this is, in bigger type, two inches away — a caption that
    repeats the label above it buys nothing and costs the picture a fifth of its height. The whole
    tile is the box: its long side meets the room's long side and the short one keeps the aspect,
    so a tall pedal and a wide head each fill what they can without being cropped or stretched.

    Two images, not one. The source is decoded once per device; the scaled copy is rebuilt only
    when the room changes size. The tile repaints sixty times a second, and resampling a 600-pixel
    photograph on every one of them would cost more than the whole rest of the face. */
class PhotoView
{
public:
    /** The decoded photograph, or an invalid image when the pack ships none. */
    void setPicture (juce::Image img)
    {
        source = std::move (img);
        scaled = {};
    }

    const juce::Image& picture() const noexcept { return source; }
    bool has() const noexcept { return source.isValid(); }

    void paint (juce::Graphics& g, juce::Rectangle<float> r)
    {
        if (! source.isValid() || r.getWidth() < 4.0f || r.getHeight() < 4.0f)
            return;

        const auto sw = (float) source.getWidth();
        const auto sh = (float) source.getHeight();
        const float k = juce::jmin (r.getWidth() / sw, r.getHeight() / sh);
        const auto box = juce::Rectangle<float> (sw * k, sh * k).withCentre (r.getCentre());

        // PHYSICAL pixels, not logical: a retina panel draws two of the first for every one of the
        // second, and a copy scaled to the logical size would be blown back up to reach them —
        // a photograph resampled twice, once down and once up, which is how a sharp cut-out turns
        // to mush on the machines that show it best.
        const float dpi = juce::jlimit (1.0f, 4.0f,
                                        g.getInternalContext().getPhysicalPixelScaleFactor());
        const int   pw  = juce::jmax (1, juce::roundToInt (box.getWidth()  * dpi));
        const int   ph  = juce::jmax (1, juce::roundToInt (box.getHeight() * dpi));

        // Down only. Asked for MORE pixels than the photograph has — the theatre, on a big screen —
        // there is nothing to gain from a bigger copy of it, so the source goes straight out and
        // the renderer does the one scale there is.
        if (pw < source.getWidth())
        {
            if (scaled.getWidth() != pw || scaled.getHeight() != ph)
                scaled = source.rescaled (pw, ph, juce::Graphics::highResamplingQuality);

            g.drawImage (scaled, box, juce::RectanglePlacement::stretchToFit);
            return;
        }

        g.drawImage (source, box, juce::RectanglePlacement::stretchToFit);
    }

private:
    juce::Image source, scaled;
};

} // namespace orbitamp::scope
