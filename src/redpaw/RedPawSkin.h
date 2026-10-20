// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "RedPawLayout.h"
#include "RedPawLineup.h"
#include "../Product.h"
#include "../face/Controls.h"
#include "../face/Monitor.h"
#include "../face/Renders.h"
#include "view/Windscreen.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>

namespace orbitamp::redpaw
{

/** WHAT MAKES ONE ROVER NOT ANOTHER. The rover's dashboard is worn by more than one product — RedPawAmp
    on Mars, FrostPawAmp on an ice moon: the same controls in the same places, the same editor
    (RedPawEditor.h) — each over a picture of its own, with its own renders, its own light, its own world
    out of the window, and its own channels and so its own number of gears (RedPawLineup.h).

    What is a product's own is said in a file of its own — redpaw/RedPawSkin.cpp, frostpaw/FrostPawSkin.cpp,
    amberpaw/AmberPawSkin.cpp — with its channels and where its picture puts each thing (RedPawLayout.h),
    and the product it makes (Product.h, rover) wears it: the face reads the skin of the processor it is
    the face of, so every rover links into one binary. How its picture was corrected to stand that way is
    the bake's (tools/bake-art), done once. */
struct Skin
{
    /** The product that wears it, by its name: its renders lie in its folder of the factory, Faces/<product>. */
    const char* product;

    /** ITS CHANNELS (RedPawLineup.h), and so its gears. */
    const channels::Lineup& lineup;

    /** ITS PICTURE, MEASURED (RedPawLayout.h): where everything sits on its dashboard. */
    const layout::Plan& plan;

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

    /** THE WORLD OUT OF ITS WINDOW: the drive its windscreen hosts (view/Drive.h), made at its start — once
        for each editor's windscreen, and once more for each poster of the site's whose window shows the
        view's first frame — and what the three ways of showing it are called in the menu, in the order of
        Windscreen::Mode. */
    std::function<std::unique_ptr<Drive>()> makeDrive;
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

    /** THE BADGE, where the skin wears one: the family's badge (face::paintBadge, the cockpit's) painted over
        the picture's emblem plate, where the plan puts it (Plan::badgeFace, badgeScrews, badgeMark) — the
        plate, its screws, the mark engraved light in it — in the skin's own colours. The plate is then the
        face's emblem: the hand's target and the mark's box. None, and the picture's own plate shows, the
        mark on it as `mark` and the render say. */
    std::optional<face::BadgeLook> badge {};

    /** THE LAMP UNDER THE MARK where the skin wears no badge: the cat, cut straight into the picture's metal,
        lit under the hand all the same (Emblem) — a badge's lamp is its own (BadgeLook::lamp). None if
        transparent. */
    juce::Colour markLamp {};
};

/** The skin a rover's processor wears (Product::skin). */
inline const Skin& skinOf (const Product& p)
{
    jassert (p.skin != nullptr);
    return *p.skin;
}

/** A ROVER, as a product: its name and its skin, and what every rover is beside them — the dashboard
    (RedPawEditor.h), the engine over the skin's channels (channels/Engine.h), and the first start. */
Product rover (const Skin&);

/** The rovers' face — or, with its pictures missing, the notice that says where they belong. */
juce::AudioProcessorEditor* makeEditor (AmpProcessor&);


} // namespace orbitamp::redpaw
