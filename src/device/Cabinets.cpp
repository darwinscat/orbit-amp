// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "Cabinets.h"

#include "DeviceLibrary.h"

#include <BassDozen.h>      // the generated lists of the bass's presets (CMakeLists.txt: THE BASS'S DOZEN)
#include <GuitarDozen.h>    // ...and of the guitar's (THE GUITAR'S CABINETS)
#include <BinaryData.h>
#include <GuitarCabData.h>

#include <algorithm>
#include <iterator>

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

    /** One of a builder's presets the binary carries, by its file's stem: its text — the bass's in BinaryData, the
        guitar's in GuitarCabData (their typical ones share files' names). */
    template <typename Find>
    juce::String carriedIn (const char* stem, int count, const char* const* names, const char* const* files, Find find)
    {
        const auto file = juce::String (stem) + ".toml";
        for (int i = 0; i < count; ++i)
            if (file == files[i])
            {
                int size = 0;
                const char* data = find (names[i], size);
                return juce::String::fromUTF8 (data, size);
            }
        return {};
    }

    juce::String carried (CabKind kind, const char* stem)
    {
        if (kind == CabKind::guitar)
            return carriedIn (stem, GuitarCabData::namedResourceListSize, GuitarCabData::namedResourceList, GuitarCabData::originalFilenames,
                              [] (const char* n, int& s) { return GuitarCabData::getNamedResource (n, s); });
        return carriedIn (stem, BinaryData::namedResourceListSize, BinaryData::namedResourceList, BinaryData::originalFilenames,
                          [] (const char* n, int& s) { return BinaryData::getNamedResource (n, s); });
    }

    /** The twelve's stems, and all of them, as the build lists them. */
    std::vector<const char*> dozenStems (CabKind kind)
    {
        return kind == CabKind::guitar ? std::vector<const char*> (std::begin (guitarCabinets), std::end (guitarCabinets))
                                       : std::vector<const char*> (std::begin (bassCabinets), std::end (bassCabinets));
    }

    std::vector<const char*> allStems (CabKind kind)
    {
        return kind == CabKind::guitar ? std::vector<const char*> (std::begin (guitarPresets), std::end (guitarPresets))
                                       : std::vector<const char*> (std::begin (bassPresets), std::end (bassPresets));
    }

    std::vector<Cabinets::Cabinet> dozenOfKind (CabKind kind)
    {
        std::vector<Cabinets::Cabinet> out;
        for (const auto* stem : dozenStems (kind))
        {
            const auto text = carried (kind, stem);
            const auto state = knownCabinet (text.toStdString());
            if (! state)
                continue;
            // "ROCK · 8x10 STACK" or "TOP 12: 4x12 ANVIL" said as "1: 8x10 STACK": its number, not its style (the
            // owner, 06.11).
            const auto own = nameIn (text);
            auto name = own.fromFirstOccurrenceOf (juce::String::fromUTF8 ("\xc2\xb7 "), false, false).trim();
            if (name.isEmpty())
                name = own.fromFirstOccurrenceOf (": ", false, false).trim();
            out.push_back ({ juce::String ((int) out.size() + 1) + ": " + (name.isNotEmpty() ? name : own), *state, {}, {} });
        }
        return out;
    }

    std::vector<Cabinets::Cabinet> allOfKind (CabKind kind)
    {
        std::vector<Cabinets::Cabinet> out;
        for (const auto* stem : allStems (kind))
        {
            const auto text = carried (kind, stem);
            if (const auto state = knownCabinet (text.toStdString()))
                out.push_back ({ nameIn (text), *state, {}, nameIn (text, "style") });
        }
        return out;
    }
}

const Cabinets& Cabinets::of (CabKind kind)
{
    static const Cabinets bass { CabKind::bass }, guitar { CabKind::guitar };
    return kind == CabKind::guitar ? guitar : bass;
}

const std::vector<Cabinets::Cabinet>& Cabinets::dozen() const
{
    static const auto bass = dozenOfKind (CabKind::bass);
    static const auto guitar = dozenOfKind (CabKind::guitar);
    return kind == CabKind::guitar ? guitar : bass;
}

const std::vector<Cabinets::Cabinet>& Cabinets::all() const
{
    static const auto bass = allOfKind (CabKind::bass);
    static const auto guitar = allOfKind (CabKind::guitar);
    return kind == CabKind::guitar ? guitar : bass;
}

int Cabinets::allOf (const cabir::State& state) const
{
    const auto& every = all();
    for (size_t i = 0; i < every.size(); ++i)
        if (every[i].state == state)
            return (int) i;
    return -1;
}

juce::File Cabinets::folder() const
{
    auto dir = DeviceLibrary::familyRoot().getChildFile (kind == CabKind::guitar ? "Guitar Cabinets" : "Bass Cabinets");
    if (! dir.isDirectory())
        dir.createDirectory();
    return dir;
}

std::vector<Cabinets::Cabinet> Cabinets::own() const
{
    std::vector<Cabinet> out;
    for (const auto& file : folder().findChildFiles (juce::File::findFiles, false, "*.toml"))
    {
        const auto text = file.loadFileAsString();
        // A file of the other builder's in this folder is not this instrument's cabinet.
        if (const auto state = knownCabinet (text.toStdString()); state && kindOf (state->box) == kind)
        {
            const auto name = nameIn (text);
            out.push_back ({ name.isNotEmpty() ? name : file.getFileNameWithoutExtension(), *state, file, {} });
        }
    }
    std::sort (out.begin(), out.end(), [] (const Cabinet& a, const Cabinet& b) { return a.name.compareNatural (b.name) < 0; });
    return out;
}

int Cabinets::dozenOf (const cabir::State& state) const
{
    const auto& twelve = dozen();
    for (size_t i = 0; i < twelve.size(); ++i)
        if (twelve[i].state == state)
            return (int) i;
    return -1;
}

juce::String Cabinets::nameOf (const cabir::State& state) const
{
    if (const int at = dozenOf (state); at >= 0)
        return dozen()[(size_t) at].name;
    if (const int at = allOf (state); at >= 0)
        return all()[(size_t) at].name;
    for (const auto& c : own())
        if (c.state == state)
            return c.name;
    return {};
}

juce::String Cabinets::cleaned (const juce::String& name)
{
    return name.removeCharacters ("\"\\\r\n\t").trim();
}

juce::File Cabinets::save (const juce::String& name, const cabir::State& state) const
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

bool Cabinets::keep (const juce::File& file, const cabir::State& state) const
{
    if (! file.existsAsFile() || ! file.isAChildOf (folder()))
        return false;
    const auto text = file.loadFileAsString();
    const auto name = nameIn (text), style = nameIn (text, "style");
    return file.replaceWithText (presetText (name.isNotEmpty() ? name : file.getFileNameWithoutExtension(),
                                             style.isNotEmpty() ? style : juce::String ("Custom"), state),
                                 false, false, "\n");
}

bool Cabinets::rename (const juce::File& file, const juce::String& name) const
{
    const auto clean = cleaned (name);
    if (clean.isEmpty() || ! file.existsAsFile() || ! file.isAChildOf (folder()))
        return false;
    const auto state = knownCabinet (file.loadFileAsString().toStdString());
    if (! state || save (clean, *state) == juce::File())
        return false;
    // The new one written first, the old file only then — unless the new name landed in the same file.
    if (file != folder().getChildFile (juce::File::createLegalFileName (clean) + ".toml"))
        file.deleteFile();
    return true;
}

bool Cabinets::remove (const juce::File& file) const
{
    return file.existsAsFile() && file.isAChildOf (folder()) && file.moveToTrash();
}

} // namespace orbitamp::device
