// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once


#include <juce_core/juce_core.h>

namespace orbitamp::device
{

/** THE FACTORY FOLDER: every file of data a Darwin's Cat amp plays — the packs, the cabinet shelf, each
    face's pictures — ONE copy of each on the machine, laid out by the family's installer as a part of it
    the player cannot leave out (CMakeLists.txt: THE FACTORY FOLDER). No amp carries any of it in its
    binary: three amps in four formats and two architectures made the same 26 MB of data into a release
    of 369 MB. What every amp still carries is the code, its shaders and the legal text.

    Inside it: the packs at its top, `<pack>.orbitrig.zip`; the cabinet shelf in `IRs/<folder>/<file>`, as
    the cabinet catalog has it (FactoryShelf.h); each face's pictures in `Faces/<product>/<file>`
    (face/Renders.h).

    EACH PRODUCT SEES ONLY WHAT ITS OWN LIST NAMES, `sets/<product>.txt` in the folder: one path inside it
    to the line. The folder is the family's — what every amp of the release plays, each file once — and a
    product plays the files its list names and passes over every other one as if it were not there. The
    build writes the lists, beside the files (CMakeLists.txt: family_factory); the code only reads them. */
struct FactorySet
{
    /** Where the lists lie in the folder, one file a product. */
    static constexpr const char* listsName = "sets";

    /** WHERE THE INSTALLER LAYS THE FOLDER on this system, in the order they are looked in:

          macOS    /Library/Application Support/Darwin's Cat/Factory
          Windows  C:\ProgramData\Darwin's Cat\Factory
          Linux    ~/.local/share/darwinscat-amps/Factory   (install.sh, for the player alone)
                   /usr/local/share/darwinscat-amps/Factory (install.sh --system, for every user)

        JUCE's commonApplicationDataDirectory is /Library on macOS, not /Library/Application Support — as
        DeviceLibrary::familyRoot says of the user's own. On Linux the player's own comes first, where
        install.sh puts its DATA: under XDG_DATA_HOME when that names a folder, as the script reads it. */
    static juce::Array<juce::File> installedPlaces()
    {
        juce::Array<juce::File> places;

       #if JUCE_MAC
        places.add (juce::File::getSpecialLocation (juce::File::commonApplicationDataDirectory)
                        .getChildFile ("Application Support").getChildFile ("Darwin's Cat").getChildFile ("Factory"));
       #elif JUCE_WINDOWS
        places.add (juce::File::getSpecialLocation (juce::File::commonApplicationDataDirectory)
                        .getChildFile ("Darwin's Cat").getChildFile ("Factory"));
       #else
        const auto xdg = juce::SystemStats::getEnvironmentVariable ("XDG_DATA_HOME", {});
        const auto share = xdg.isNotEmpty() && juce::File::isAbsolutePath (xdg)
                               ? juce::File (xdg)
                               : juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".local/share");
        places.add (share.getChildFile ("darwinscat-amps").getChildFile ("Factory"));
        places.add (juce::File ("/usr/local/share/darwinscat-amps/Factory"));
       #endif

        return places;
    }

    /** THE FOLDER THIS BUILD READS. A gate reads the one it was built to read (ORBITAMP_GATE_FACTORY) —
        the build's own <build>/Factory, or a folder that is not there — and never the machine's. A build
        of the tree reads its <build>/Factory first, where that is there (ORBITAMP_DEV_FACTORY), so an amp
        built here plays the files built with it; a release is never built so. Then where the installer
        lays it. Where none is there, the first place it belongs — which every reader takes for a folder
        with nothing in it. */
    static juce::File location()
    {
       #if defined (ORBITAMP_GATE_FACTORY)
        return juce::File (juce::CharPointer_UTF8 (ORBITAMP_GATE_FACTORY));
       #else
        #if defined (ORBITAMP_DEV_FACTORY)
         if (const juce::File dev (juce::CharPointer_UTF8 (ORBITAMP_DEV_FACTORY)); dev.isDirectory())
             return dev;
        #endif

        const auto places = installedPlaces();
        for (const auto& place : places)
            if (place.isDirectory())
                return place;

        return places.getFirst();
       #endif
    }

    /** Whether the folder this build reads is there at all. */
    static bool isThere() { return location().isDirectory(); }

    /** What `product`'s own list in `factory` names: paths inside the folder, as the list has them. A line
        that starts with `#` says something about the list rather than naming a file. */
    static juce::StringArray listOf (const juce::File& factory, const juce::String& product)
    {
        juce::StringArray lines, paths;
        lines.addLines (factory.getChildFile (listsName)
                               .getChildFile (juce::File::createLegalFileName (product) + ".txt")
                               .loadFileAsString());

        for (const auto& line : lines)
            if (const auto path = line.trim(); path.isNotEmpty() && ! path.startsWithChar ('#'))
                paths.add (path);

        return paths;
    }

    /** ...and `product`'s own, in the folder this build reads. */
    static juce::StringArray ownList (const juce::String& product)
    {
        return listOf (location(), product);
    }

};

} // namespace orbitamp::device
