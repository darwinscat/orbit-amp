// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Gate for the library manager's FILE WORK — the part that can quietly eat a collection. The
// manager window is dumb views over these calls, so what is tested here is the whole risk: an
// import that lands where it said, a zip that unpacks to a sane folder and nowhere else, a rename
// that cannot leave the library, a numbered collision instead of an overwrite.
//
// Everything runs in a temp root passed in explicitly — the machine's real library is never read
// or written. The Trash itself is JUCE's; what is ours (and tested) is the refusal to trash
// anything the library does not own.

#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "device/DeviceLibrary.h"
#include "device/IrLibrary.h"

#include <cstdio>

using orbitamp::device::DeviceLibrary;
using orbitamp::device::IrLibrary;

namespace
{
    int failures = 0;

    void report (const char* what, bool ok, const juce::String& detail = {})
    {
        if (! ok)
            ++failures;

        std::printf ("%-58s %s  %s\n", what, ok ? "ok" : "FAIL", detail.toRawUTF8());
    }

    juce::File makeWav (const juce::File& f)
    {
        f.getParentDirectory().createDirectory();
        f.replaceWithText ("RIFF-not-really-audio");   // the libraries never read audio, only names
        return f;
    }

    int countFiles (const IrLibrary::Node& n)
    {
        if (! n.folder)
            return 1;

        int total = 0;
        for (const auto& c : n.children)
            total += countFiles (c);
        return total;
    }

    const IrLibrary::Node* childNamed (const IrLibrary::Node& n, const juce::String& name)
    {
        for (const auto& c : n.children)
            if (c.name() == name)
                return &c;
        return nullptr;
    }
}

int main()
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const auto work = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("orbitamp-library-tests").getNonexistentSibling();
    work.createDirectory();

    // ---- IR import: lone files ----------------------------------------------------------------
    {
        const auto root = work.getChildFile ("irs-lone");
        root.createDirectory();
        const auto src = makeWav (work.getChildFile ("src/Cab 4x12.wav"));

        report ("a wav imports into the tree",       IrLibrary::importPath (src, root) == 1);
        report ("...and scan sees it",               countFiles (IrLibrary::scan (root)) == 1);
        report ("the same wav again lands as ' 2'",  IrLibrary::importPath (src, root) == 1
                                                       && root.getChildFile ("Cab 4x12 2.wav").existsAsFile());
        report ("a readme is not an IR",             IrLibrary::importPath (
                                                         makeWav (work.getChildFile ("src/readme.txt")), root) == 0);
    }

    // ---- IR import: a vendor folder ------------------------------------------------------------
    {
        const auto root = work.getChildFile ("irs-folder");
        root.createDirectory();

        const auto pack = work.getChildFile ("src/Vendor Pack");
        makeWav (pack.getChildFile ("Close/57.wav"));
        makeWav (pack.getChildFile ("Close/121.wav"));
        makeWav (pack.getChildFile ("Room/room.aiff"));
        makeWav (pack.getChildFile ("license.txt"));
        pack.getChildFile ("Empty").createDirectory();

        report ("a folder imports its audio",        IrLibrary::importPath (pack, root) == 3);

        const auto tree = IrLibrary::scan (root);
        const auto* copied = childNamed (tree, "Vendor Pack");
        report ("...as one folder, shape kept",      copied != nullptr && copied->folder
                                                       && childNamed (*copied, "Close") != nullptr);
        report ("...without the license file",       ! root.getChildFile ("Vendor Pack/license.txt").exists());
        report ("...without the empty folder",       ! root.getChildFile ("Vendor Pack/Empty").exists());
    }

    // ---- IR import: zips -----------------------------------------------------------------------
    {
        const auto root = work.getChildFile ("irs-zip");
        root.createDirectory();

        // A vendor zip: everything under one top folder, plus macOS droppings and a hostile path.
        const auto zipSrc = work.getChildFile ("src/zipped");
        const auto a = makeWav (zipSrc.getChildFile ("a.wav"));
        const auto b = makeWav (zipSrc.getChildFile ("b.wav"));

        const auto zip1 = work.getChildFile ("src/Neon Pack.zip");
        {
            juce::ZipFile::Builder builder;
            builder.addFile (a, 5, "NeonCabs/Close/a.wav");
            builder.addFile (b, 5, "NeonCabs/Room/b.wav");
            builder.addFile (b, 5, "__MACOSX/NeonCabs/Close/._a.wav");
            builder.addFile (b, 5, "../escape.wav");
            builder.addFile (b, 5, "NeonCabs/notes.txt");

            juce::FileOutputStream out (zip1);
            builder.writeToStream (out, nullptr);
        }

        report ("a zip unpacks its audio",           IrLibrary::importPath (zip1, root) == 2);
        report ("...under the vendor's own folder",  root.getChildFile ("NeonCabs/Close/a.wav").existsAsFile()
                                                       && root.getChildFile ("NeonCabs/Room/b.wav").existsAsFile());
        report ("...macOS droppings skipped",        ! root.getChildFile ("__MACOSX").exists());
        report ("...a hostile path stays inside",    ! root.getParentDirectory()
                                                           .getChildFile ("escape.wav").exists()
                                                       && ! root.getChildFile ("escape.wav").exists());

        // A flat zip: no shared top folder, so the zip's own name becomes the folder.
        const auto zip2 = work.getChildFile ("src/Loose IRs.zip");
        {
            juce::ZipFile::Builder builder;
            builder.addFile (a, 5, "one.wav");
            builder.addFile (b, 5, "deep/two.wav");

            juce::FileOutputStream out (zip2);
            builder.writeToStream (out, nullptr);
        }

        report ("a flat zip is named after itself",  IrLibrary::importPath (zip2, root) == 2
                                                       && root.getChildFile ("Loose IRs/one.wav").existsAsFile()
                                                       && root.getChildFile ("Loose IRs/deep/two.wav").existsAsFile());
    }

    // ---- IR rename -----------------------------------------------------------------------------
    {
        const auto root = work.getChildFile ("irs-rename");
        makeWav (root.getChildFile ("Pack/one.wav"));
        makeWav (root.getChildFile ("Other/two.wav"));

        report ("a folder renames",                  IrLibrary::rename (root, root.getChildFile ("Pack"), "My Cabs")
                                                       && root.getChildFile ("My Cabs/one.wav").existsAsFile());
        report ("a taken name is refused",           ! IrLibrary::rename (root, root.getChildFile ("My Cabs"), "Other"));
        report ("the root itself is refused",        ! IrLibrary::rename (root, root, "Nope"));
        report ("a stranger's path is refused",      ! IrLibrary::rename (root, work.getChildFile ("irs-zip"), "Hijack"));
        report ("a file keeps its extension",        IrLibrary::rename (root, root.getChildFile ("My Cabs/one.wav"), "Bright")
                                                       && root.getChildFile ("My Cabs/Bright.wav").existsAsFile());
    }

    // ---- IR folders made in place --------------------------------------------------------------
    {
        const auto root = work.getChildFile ("irs-mkdir");
        root.createDirectory();

        const auto made = IrLibrary::createFolder (root, root, "My Cabs");
        report ("a folder is made at the root",     made == root.getChildFile ("My Cabs") && made.isDirectory());
        report ("...and nested inside another",      IrLibrary::createFolder (root, made, "Close")
                                                       == made.getChildFile ("Close"));
        report ("a taken name gets numbered",        IrLibrary::createFolder (root, root, "My Cabs")
                                                       == root.getChildFile ("My Cabs 2"));
        report ("an illegal name is refused",        IrLibrary::createFolder (root, root, "   ") == juce::File());
        report ("a parent outside the root refused", IrLibrary::createFolder (root, work.getChildFile ("irs-zip"),
                                                                              "Hijack") == juce::File());
    }

    // ---- IR remove: only the guard is ours -----------------------------------------------------
    {
        const auto root = work.getChildFile ("irs-remove");
        makeWav (root.getChildFile ("keep.wav"));

        report ("removing outside the root refused", ! IrLibrary::remove (root, work.getChildFile ("irs-rename/My Cabs")));
        report ("removing the root itself refused",  ! IrLibrary::remove (root, root));
    }

    // ---- Devices: what counts as one -----------------------------------------------------------
    {
        const auto src = work.getChildFile ("dev-src");
        const auto nam = makeWav (src.getChildFile ("clean.nam"));
        makeWav (src.getChildFile ("crunch.namz"));
        makeWav (src.getChildFile ("amp.orbitrig.zip"));
        makeWav (src.getChildFile ("random.zip"));
        const auto pack = src.getChildFile ("MyAmp.orbitrig");
        makeWav (pack.getChildFile ("rig.json"));
        src.getChildFile ("plain-folder").createDirectory();

        report ("a .nam is a device",                DeviceLibrary::looksLikeDevice (nam));
        report ("a .namz is a device",               DeviceLibrary::looksLikeDevice (src.getChildFile ("crunch.namz")));
        report ("an .orbitrig.zip is a device",      DeviceLibrary::looksLikeDevice (src.getChildFile ("amp.orbitrig.zip")));
        report ("a plain zip is not",                ! DeviceLibrary::looksLikeDevice (src.getChildFile ("random.zip")));
        report ("a pack folder is a device",         DeviceLibrary::looksLikeDevice (pack));
        report ("a plain folder is not",             ! DeviceLibrary::looksLikeDevice (src.getChildFile ("plain-folder")));
    }

    // ---- Devices: import -----------------------------------------------------------------------
    {
        const auto into = work.getChildFile ("devices");
        into.createDirectory();
        const auto src = work.getChildFile ("dev-src");

        report ("a model imports",                   DeviceLibrary::importDevice (src.getChildFile ("clean.nam"), into)
                                                       == into.getChildFile ("clean.nam"));
        report ("its double lands as ' 2'",          DeviceLibrary::importDevice (src.getChildFile ("clean.nam"), into)
                                                       == into.getChildFile ("clean 2.nam"));

        DeviceLibrary::importDevice (src.getChildFile ("amp.orbitrig.zip"), into);
        const auto second = DeviceLibrary::importDevice (src.getChildFile ("amp.orbitrig.zip"), into);
        report ("a doubled pack zip stays a pack",   second.getFileName() == "amp 2.orbitrig.zip"
                                                       && second.getFileName().endsWithIgnoreCase (".orbitrig.zip"));

        report ("a pack folder imports whole",       DeviceLibrary::importDevice (src.getChildFile ("MyAmp.orbitrig"), into)
                                                       .getChildFile ("rig.json").existsAsFile());
        report ("a non-device is refused",           DeviceLibrary::importDevice (src.getChildFile ("random.zip"), into)
                                                       == juce::File());
    }

    // ---- Devices: remove refuses what is not the user's ----------------------------------------
    {
        DeviceLibrary::Pack bundled;
        bundled.bundled  = true;
        bundled.location = work.getChildFile ("devices/clean.nam");

        DeviceLibrary::Pack stranger;
        stranger.location = work.getChildFile ("devices/clean.nam");   // exists, but not in the library dir

        report ("removing a bundled device refused", ! DeviceLibrary::removeDevice (bundled));
        report ("removing outside the library refused", ! DeviceLibrary::removeDevice (stranger));
    }

    work.deleteRecursively();

    std::printf ("\n%s\n", failures != 0 ? "FAILURES" : "all checks passed");
    return failures;
}
