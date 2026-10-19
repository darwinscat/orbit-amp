// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "CatpitArt.h"
#include "../face/Renders.h"

#include <CatpitData.h>

namespace orbitamp::catpit
{

namespace
{
    const face::renders::Binary binary { CatpitData::namedResourceList, CatpitData::namedResourceListSize,
                                         CatpitData::getNamedResourceOriginalFilename, CatpitData::getNamedResource };

    /** A render by its file name: the copy built into the binary — or, when CATPIT_ART_DIR names a
        folder that has one, the file from there (face/Renders.h). */
    juce::Image decode (const char* name) { return face::renders::decode (name, binary, "CATPIT_ART_DIR"); }
    juce::Image load (const char* name)   { return face::renders::load (name, binary, "CATPIT_ART_DIR"); }
}

juce::Image stillSky (int channel)
{
    static constexpr const char* names[] { "sky-clean.jpg", "sky-edge.jpg", "sky-crunch.jpg", "sky-hi-gain.jpg" };
    return decode (names[juce::jlimit (0, 3, channel)]);
}

const Art& Art::get()
{
    static const Art art = []
    {
        Art a;
        a.bg          = load ("cockpit.webp");
        for (size_t i = 0; i < a.lampLit.size(); ++i)
            a.lampLit[i] = load (("lamp-" + juce::String ((int) i) + ".webp").toRawUTF8());
        a.knobDrive   = load ("knob-drive.webp");
        a.knobTone    = load ("knob-tone.webp");
        a.leverGrip   = load ("throttle-grip.webp");
        a.guardClosed = load ("guard-closed.webp");
        a.guardOpen   = load ("guard-open.webp");
        a.guardOn     = load ("guard-on.webp");
        a.sliderCap   = load ("fader-cap.webp");
        return a;
    }();
    return art;
}

} // namespace orbitamp::catpit
