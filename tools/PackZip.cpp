// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// PACKZIP <list> <zip> — zips a pack the tree keeps as a folder (resources/set/Devices/<name>.orbitrig)
// into the .orbitrig.zip a product carries. The build runs it, from THE SET in CMakeLists.txt, which
// says why it is ours and not `cmake -E tar`; nobody else needs to.
//
// <list> names the pack's files, one path to the line, in the order they go in; each goes in under its
// own file name. Nothing of a file but its name and its bytes reaches the zip: every entry is dated
// 1980-01-01 00:00, the first day a zip can say, and carries no owner, permissions or times of its
// own — so the same files make the same bytes, on every build and every system. The models and the
// manifest are deflated; the picture is stored, as it is compressed already.

#include <juce_core/juce_core.h>

#include <cstdio>
#include <memory>

int main (int argc, char* argv[])
{
    if (argc != 3)
    {
        std::fprintf (stderr, "usage: packzip <list of files> <zip>\n");
        return 2;
    }

    const auto here   = juce::File::getCurrentWorkingDirectory();
    const auto list   = here.getChildFile (juce::String::fromUTF8 (argv[1]));
    const auto target = here.getChildFile (juce::String::fromUTF8 (argv[2]));

    const auto fail = [] (const juce::String& why)
    {
        std::fprintf (stderr, "packzip: %s\n", why.toRawUTF8());
        return 1;
    };

    juce::StringArray paths;
    paths.addLines (list.loadFileAsString());
    paths.removeEmptyStrings();

    if (paths.isEmpty())
        return fail ("nothing to zip in " + list.getFullPathName());

    // Read back in local time, which is how a zip's date is written: 00:00 on that day wherever it is built.
    const juce::Time dated (1980, 0, 1, 0, 0, 0, 0, true);

    juce::ZipFile::Builder zip;

    for (const auto& path : paths)
    {
        const juce::File file (path);
        auto in = std::make_unique<juce::FileInputStream> (file);

        if (! in->openedOk())
            return fail ("cannot read " + file.getFullPathName());

        zip.addEntry (std::move (in), file.hasFileExtension ("webp") ? 0 : 9, file.getFileName(), dated);
    }

    // Written aside and moved into place, so a zip that failed half-way is never taken for a made one.
    if (! target.getParentDirectory().createDirectory())
        return fail ("cannot make " + target.getParentDirectory().getFullPathName());

    juce::TemporaryFile temp (target);

    {
        juce::FileOutputStream out (temp.getFile());

        if (! out.openedOk() || ! zip.writeToStream (out, nullptr))
            return fail ("cannot write " + temp.getFile().getFullPathName());

        out.flush();

        if (out.getStatus().failed())
            return fail ("cannot write " + temp.getFile().getFullPathName());
    }

    if (! temp.overwriteTargetFileWithTemporary())
        return fail ("cannot move the zip to " + target.getFullPathName());

    return 0;
}
