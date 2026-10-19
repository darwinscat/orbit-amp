// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "RedPawArt.h"
#include "RedPawSkin.h"

#include "../face/Renders.h"

namespace orbitamp::redpaw
{

namespace
{
    /** A render of the product's own, by the rover's file name (RedPawSkin.h). */
    juce::Image load (const char* name) { return face::renders::load (name, skin().rendersFolderVariable); }

    /** A render only some rovers carry: none is no image, not a typo. */
    juce::Image loadOwn (const juce::String& name)
    {
        return face::renders::has (name.toRawUTF8(), skin().rendersFolderVariable) ? load (name.toRawUTF8()) : juce::Image();
    }
}

const Art& Art::get()
{
    static const Art art = []
    {
        Art a;
        a.bg         = load ("dashboard.webp");
        for (size_t i = 0; i < a.lampLit.size(); ++i)
            a.lampLit[i] = loadOwn ("lamp-" + juce::String ((int) i) + ".webp");
        a.knobDrive  = load ("knob-drive.webp");
        a.knobTone   = load ("knob-tone.webp");
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
        a.turboLid   = loadOwn ("turbo-lid.webp");
        a.emblem     = loadOwn ("emblem.webp");
        return a;
    }();
    return art;
}

juce::Image Art::still()
{
    return loadOwn ("still.jpg");
}

juce::String Art::mark()
{
    return face::renders::text ("mark.svg", skin().rendersFolderVariable);
}

} // namespace orbitamp::redpaw
