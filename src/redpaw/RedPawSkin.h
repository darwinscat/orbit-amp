// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../face/Controls.h"
#include "../face/Monitor.h"
#include "../face/Renders.h"
#include "view/Windscreen.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace orbitamp::redpaw
{

/** WHAT MAKES ONE ROVER NOT ANOTHER. The rover's dashboard is worn by more than one product — RedPawAmp
    on Mars, FrostPawAmp on an ice moon: the same controls in the same places, the same editor
    (RedPawEditor.h) — each over a picture of its own, with its own renders, its own light, its own world
    out of the window, and its own channels and so its own number of gears (RedPawLineup.h).

    What is a product's own is said in a file of its own — redpaw/RedPawSkin.cpp, frostpaw/FrostPawSkin.cpp
    — with its channels and where its picture puts each thing (RedPawLayout.h), and a target compiles
    exactly one, as it compiles exactly one makeEditor. How its picture was corrected to stand that way is
    the bake's (tools/bake-art), done once. */
struct Skin
{
    /** The environment variable that names a folder to try new renders from (face/Renders.h). Its own
        renders lie in its folder of the factory, Faces/<product>, every one under the rover's file name
        (RedPawArt.h). */
    const char* rendersFolderVariable;

    /** The light of the rings its picture paints, a shade brighter: the knobs' rings, and the light
        round the lever. */
    juce::Colour ringLight;

    /** THE INK ITS WORDS ARE WRITTEN IN on its panel (face::Ink) — the family's lilac on a dark one, its
        own dark on bright metal — and what lies under the words printed largest, DRIVE and the tones,
        INPUT, OUTPUT and TURBO: a shadow; none if transparent. */
    face::Ink ink;
    juce::Colour printShadow;

    /** What its MIDI window lists. */
    juce::String midiCommands;

    /** THE WORLD OUT OF ITS WINDOW: the windscreen it drives (view/Windscreen.h), made once for each
        editor, and what the three ways of showing it are called in the menu, in the order of
        Windscreen::Mode. */
    std::function<std::unique_ptr<Windscreen>()> makeView;
    juce::StringArray landscapeNames;

    /** WHAT ITS DRIVE KNOB IS CALLED, over it. */
    const char* driveName = "DRIVE";

    /** THE MAKER'S MARK on the emblem plate — engraved, or raised where it brings a render of it. A
        picture with no place for one yet says so: nothing is cut there, and nothing there answers the
        hand. */
    bool mark = true;

    /** WHAT THE EDGE COLUMNS ARE CALLED, under them. */
    const char* inputName = "INPUT";
    const char* outputName = "OUTPUT";

    /** THE NAV SCREEN: whether its top line names the pack playing beside the gear — a rover's does not:
        its channels are its favourite sounds, not a maker's boxes, and it names no device anywhere — and
        the picture it shows until the player picks another. */
    bool packOnScreen = false;
    face::Monitor::View firstScreen = face::Monitor::View::shape;      // the family's first picture: what the amp does to the sound
    bool parkSignOnTopLine = true;      // parked, the warning on the top line too, beside the large one
};

/** The product's own. */
const Skin& skin();

} // namespace orbitamp::redpaw
