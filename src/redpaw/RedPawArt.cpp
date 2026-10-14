// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "RedPawArt.h"

#include "../face/Renders.h"

#include <RedPawData.h>

namespace orbitamp::redpaw
{

namespace
{
    const face::renders::Binary binary { RedPawData::namedResourceList, RedPawData::namedResourceListSize,
                                         RedPawData::getNamedResourceOriginalFilename, RedPawData::getNamedResource };

    juce::Image load (const char* name) { return face::renders::load (name, binary, "REDPAW_ART_DIR"); }

    /** THE LAMPS GO OUT (face/Renders.h). On a rusted dashboard warm is everywhere: a lamp is what is
        both warm AND bright — its red well over its blue, and the whole of it lit — so the rust and the
        dust round it stay as they were painted. */
    float lampness (juce::Colour c)
    {
        const float r = c.getFloatRed(), g = c.getFloatGreen(), b = c.getFloatBlue();
        const float warm   = (r - b - 0.25f) / 0.25f;
        const float bright = (0.3f * r + 0.59f * g + 0.11f * b - 0.38f) / 0.18f;
        return juce::jmin (juce::jlimit (0.0f, 1.0f, warm), juce::jlimit (0.0f, 1.0f, bright));
    }
}

const Art& Art::get()
{
    static const Art art = []
    {
        Art a;
        a.bg         = face::renders::putOutLamps (load ("dashboard.png"), layout::lamps, layout::width, lampness, a.lampLit);
        a.knobDrive  = load ("knob-drive.png");
        a.knobTone   = load ("knob-tone.png");
        a.lever      = load ("lever.png");
        a.sliderCap  = load ("slider-cap.png");
        a.faderCap   = load ("fader-cap.png");
        a.toggleUp   = load ("toggle-up.png");
        a.toggleDown = load ("toggle-down.png");
        a.keyUp      = load ("key-up.png");
        a.keyDown    = load ("key-down.png");
        a.turboUp    = load ("turbo-up.png");
        a.turboDown  = load ("turbo-down.png");
        return a;
    }();
    return art;
}

} // namespace orbitamp::redpaw
