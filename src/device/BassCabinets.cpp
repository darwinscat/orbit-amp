// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "BassCabinets.h"

#include "DeviceLibrary.h"

#include <BassDozen.h>      // the generated list of the dozen's presets (CMakeLists.txt: THE BASS'S DOZEN)
#include <BinaryData.h>
#include <panel/Panel.h>   // cabir::knownState: a cabinet of a box the table knows, or none

namespace orbitamp::device
{

namespace
{
    /** A preset's own name, `name = "ROCK · 8x10 STACK"`, as its first line has it — or another of its quoted keys. */
    juce::String nameIn (const juce::String& text, const char* key = "name")
    {
        return text.fromFirstOccurrenceOf (juce::String (key) + " = \"", false, false).upToFirstOccurrenceOf ("\"", false, false);
    }

    /** The cabinets' own preset, as cab-ir-generator writes one: its name and style, then the state. */
    juce::String presetText (const juce::String& name, const juce::String& style, const cabir::State& state)
    {
        return "name = \"" + name + "\"\nstyle = \"" + style + "\"\n\n" + juce::String (cabir::toToml (state));
    }

    /** One of the dozen's presets the binary carries, by its file's stem: its text. */
    juce::String carried (const char* stem)
    {
        const auto file = juce::String (stem) + ".toml";
        for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
            if (file == BinaryData::originalFilenames[i])
            {
                int size = 0;
                const char* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size);
                return juce::String::fromUTF8 (data, size);
            }
        return {};
    }
}

const std::vector<BassCabinets::Cabinet>& BassCabinets::dozen()
{
    static const std::vector<Cabinet> all = []
    {
        std::vector<Cabinet> out;
        for (const auto* stem : bassCabinets)
        {
            const auto text = carried (stem);
            const auto state = cabir::knownState (text.toStdString());
            if (! state)
                continue;
            // "ROCK · 8x10 STACK" said as "1: 8x10 STACK": its number, not its style (the owner, 06.11).
            const auto own = nameIn (text);
            const auto name = own.fromFirstOccurrenceOf (juce::String::fromUTF8 ("\xc2\xb7 "), false, false).trim();
            out.push_back ({ juce::String ((int) out.size() + 1) + ": " + (name.isNotEmpty() ? name : own),
                             *state, {} });
        }
        return out;
    }();
    return all;
}

juce::File BassCabinets::folder()
{
    auto dir = DeviceLibrary::familyRoot().getChildFile ("Bass Cabinets");
    if (! dir.isDirectory())
        dir.createDirectory();
    return dir;
}

std::vector<BassCabinets::Cabinet> BassCabinets::own()
{
    std::vector<Cabinet> out;
    for (const auto& file : folder().findChildFiles (juce::File::findFiles, false, "*.toml"))
    {
        const auto text = file.loadFileAsString();
        if (const auto state = cabir::knownState (text.toStdString()))
        {
            const auto name = nameIn (text);
            out.push_back ({ name.isNotEmpty() ? name : file.getFileNameWithoutExtension(), *state, file });
        }
    }
    std::sort (out.begin(), out.end(), [] (const Cabinet& a, const Cabinet& b) { return a.name.compareNatural (b.name) < 0; });
    return out;
}

int BassCabinets::dozenOf (const cabir::State& state)
{
    const auto& all = dozen();
    for (size_t i = 0; i < all.size(); ++i)
        if (all[i].state == state)
            return (int) i;
    return -1;
}

juce::String BassCabinets::nameOf (const cabir::State& state)
{
    if (const int at = dozenOf (state); at >= 0)
        return dozen()[(size_t) at].name;
    for (const auto& c : own())
        if (c.state == state)
            return c.name;
    return {};
}

juce::String BassCabinets::cleaned (const juce::String& name)
{
    return name.removeCharacters ("\"\\\r\n\t").trim();
}

juce::File BassCabinets::save (const juce::String& name, const cabir::State& state)
{
    const auto clean = cleaned (name);
    if (clean.isEmpty())
        return {};
    for (const auto& c : own())   // one of that name: replaced, under whatever file it had
        if (c.name == clean && c.file.existsAsFile())
            c.file.deleteFile();
    const auto file = folder().getChildFile (juce::File::createLegalFileName (clean) + ".toml");
    return file.replaceWithText (presetText (clean, "Custom", state), false, false, "\n") ? file : juce::File();
}

bool BassCabinets::keep (const juce::File& file, const cabir::State& state)
{
    if (! file.existsAsFile() || ! file.isAChildOf (folder()))
        return false;
    const auto text = file.loadFileAsString();
    const auto name = nameIn (text), style = nameIn (text, "style");
    return file.replaceWithText (presetText (name.isNotEmpty() ? name : file.getFileNameWithoutExtension(),
                                             style.isNotEmpty() ? style : juce::String ("Custom"), state),
                                 false, false, "\n");
}

bool BassCabinets::rename (const juce::File& file, const juce::String& name)
{
    const auto clean = cleaned (name);
    if (clean.isEmpty() || ! file.existsAsFile() || ! file.isAChildOf (folder()))
        return false;
    const auto state = cabir::knownState (file.loadFileAsString().toStdString());
    if (! state || save (clean, *state) == juce::File())
        return false;
    // The new one written first, the old file only then — unless the new name landed in the same file.
    if (file != folder().getChildFile (juce::File::createLegalFileName (clean) + ".toml"))
        file.deleteFile();
    return true;
}

bool BassCabinets::remove (const juce::File& file)
{
    return file.existsAsFile() && file.isAChildOf (folder()) && file.moveToTrash();
}

} // namespace orbitamp::device
