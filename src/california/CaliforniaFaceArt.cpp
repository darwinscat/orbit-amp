// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "CaliforniaFaceArt.h"

#include "CaliforniaLayout.h"
#include "view/CaliforniaCap.h"
#include "../face/Renders.h"

namespace orbitamp::california
{

namespace
{
    constexpr const char* product = "PeacePhaseAmp";
    constexpr const char* folderVariable = "CALIFORNIA_ART_DIR";

    juce::Image load (const char* name)
    {
        return face::renders::load (product, name, folderVariable);
    }

    static_assert ([]
    {
        const auto apart = [] (float a, float b) { return a - b > 1.0e-3f || b - a > 1.0e-3f; };
        for (size_t i = 0; i < layout::emblemFace.size(); ++i)
            if (apart (layout::emblemFace[i].x, cap::octagon[i][0]) || apart (layout::emblemFace[i].y, cap::octagon[i][1]))
                return false;
        return layout::emblemFace.size() == 8;
    }(), "the cap painted orange is the maker's plate's octagon");

    /** The dashboard with the maker's cap painted orange (view/CaliforniaCap.h). */
    juce::Image withOrangeCap (juce::Image dashboard)
    {
        if (! dashboard.isValid() || dashboard.getFormat() != juce::Image::ARGB)
            return dashboard;
        dashboard.duplicateIfShared();
        juce::Image::BitmapData pixels (dashboard, juce::Image::BitmapData::readWrite);
        cap::paint (pixels.data, pixels.width, pixels.height, pixels.lineStride, pixels.pixelStride,
                    juce::PixelARGB::indexR, juce::PixelARGB::indexG, juce::PixelARGB::indexB, juce::PixelARGB::indexA);
        return dashboard;
    }
}

const FaceArt& FaceArt::of()
{
    static const FaceArt art = []
    {
        FaceArt a;
        a.bg        = withOrangeCap (load ("dashboard.webp"));
        a.pendant   = load ("dashboard_pendant.webp");
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

} // namespace orbitamp::california
