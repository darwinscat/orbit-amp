// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "MetalFaceArt.h"

#include "../face/Renders.h"

namespace orbitamp::metal
{

namespace
{
    constexpr const char* product = "MetalAmp";
    constexpr const char* folderVariable = "METAL_ART_DIR";

    juce::Image load (const char* name)
    {
        return face::renders::load (product, name, folderVariable);
    }
}

const FaceArt& FaceArt::of()
{
    static const FaceArt art = []
    {
        FaceArt a;
        a.bg        = load ("dashboard.webp");
        a.knobDrive = load ("knob-drive.webp");
        a.knobTone  = load ("knob-tone.webp");
        a.faderCap  = load ("fader-cap.webp");
        return a;
    }();
    return art;
}

juce::String FaceArt::mark()
{
    return face::renders::text (product, "mark.svg", folderVariable);
}

juce::String FaceArt::midiCommands()
{
    return face::renders::text (product, "midi.txt", folderVariable);
}

bool FaceArt::present()
{
    return face::renders::has (product, "dashboard.webp", folderVariable);
}

} // namespace orbitamp::metal
