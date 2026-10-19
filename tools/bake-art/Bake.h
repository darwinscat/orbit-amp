// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <utility>
#include <vector>

/** BAKE-ART'S FACES (BakeArt.cpp says what the tool is for): what a face's bake reads, and what it
    makes of it. Each face's corrections are said in a file of its own — Rover.cpp for the rovers,
    Cockpit.cpp for the cockpit — with the numbers they were measured to. */
namespace orbitamp::bake
{

/** A face's finished pictures, each under the file name it ships as. */
using Pictures = std::vector<std::pair<juce::String, juce::Image>>;

/** Where a face's source renders are: its own folder, and the folder a render it has none of yet comes
    from — RedPawAmp's for the rovers, as their builds take it; none for the cockpit. */
struct Sources
{
    juce::File own, fallback;

    /** Whether its own folder has the render: a render only some faces carry is never another's. */
    bool has (const juce::String& name) const;

    /** A render decoded the way the faces decoded their renders when they made these pictures
        themselves, as ARGB; null where neither folder has it. */
    juce::Image load (const juce::String& name) const;
};

/** THE ROVERS: `face` is redpaw, frostpaw or amberpaw. */
bool isRover (const juce::String& face);
Pictures bakeRover (const juce::String& face, const Sources&);

/** THE COCKPIT; `gripAxis` comes back as where the throttle grip's stem stands across the grip, 0 its
    left end — the number catpit::layout::gripAxis carries. */
Pictures bakeCockpit (const Sources&, float& gripAxis);

/** THE LAMPS GO OUT. A render paints lit the lamps a face lights itself. Each, in its box (`box`, in the
    face's 1672-wide stage), is taken out as it was painted, glow and all — to be laid back over at the
    strength it burns; in the picture it is put out: how much of a pixel is the lamp's (`lampness`, 0..1)
    is how far it is taken down to the dark warm grey of a lamp that is off, so the glow on the plate
    round it goes too. What the lamp did not touch stays as it was, in the picture and out of the copy. */
juce::Image putOutLamp (juce::Image& img, juce::Rectangle<float> box, float stageWidth,
                        const std::function<float (juce::Colour)>& lampness);

} // namespace orbitamp::bake
