// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "RedGrooveArt.h"

#include "../face/Renders.h"

namespace orbitamp::redgroove
{

namespace
{
    constexpr const char* product = "RedGrooveBass";
    constexpr const char* folderVariable = "REDGROOVE_ART_DIR";

    juce::Image load (const char* name)
    {
        return face::renders::load (product, name, folderVariable);
    }
}

const Art& Art::of()
{
    static const Art art = []
    {
        Art a;
        a.bg        = load ("dashboard.webp");
        a.knobDrive = load ("knob-drive.webp");
        a.knobTone  = load ("knob-tone.webp");
        a.faderCap  = load ("fader-cap.webp");
        return a;
    }();
    return art;
}

juce::Image Art::still()
{
    return face::renders::has (product, "still.jpg", folderVariable) ? load ("still.jpg") : juce::Image();
}

juce::String Art::mark()
{
    return face::renders::text (product, "mark.svg", folderVariable);
}

juce::String Art::midiCommands()
{
    return face::renders::text (product, "midi.txt", folderVariable);
}

bool Art::present()
{
    return face::renders::has (product, "dashboard.webp", folderVariable);
}

} // namespace orbitamp::redgroove
