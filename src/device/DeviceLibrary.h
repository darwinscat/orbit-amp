#pragma once

#include <namz_rig.h>
#include <namz_rig_load.h>

#include <juce_core/juce_core.h>

namespace orbitamp::device
{

/** The captured devices on disk: `.orbitrig` packs, either as a folder or as the zip they ship in.

    A pack is a manifest plus the `.namz` models it names. namz owns the manifest's meaning — this
    only finds packs, hands namz the text, and fetches model bytes by name. Nothing here parses the
    format; a second reader would be a second opinion about what a device is. */
class DeviceLibrary
{
public:
    struct Pack
    {
        juce::String  name;       // the rig's display name
        juce::File    location;   // the folder, or the .orbitrig zip
        bool          zipped = false;
        namz::rig::Rig rig;

        bool isValid() const { return ! rig.chain.empty(); }
    };

    /** Where a user drops packs. Created on first look, so the folder exists to be dropped into. */
    /** JUCE's userApplicationDataDirectory is ~/Library on macOS, not ~/Library/Application Support —
        without this the folder lands one level too high and nothing a user drops in is ever found. */
    static juce::File appDataRoot()
    {
        auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);

       #if JUCE_MAC
        dir = dir.getChildFile ("Application Support");
       #endif

        return dir.getChildFile ("Darwin's Cat").getChildFile ("OrbitAmp");
    }

    static juce::File directory()
    {
        auto dir = appDataRoot().getChildFile ("Devices");

        if (! dir.isDirectory())
            dir.createDirectory();

        return dir;
    }

    /** Every pack found, in name order. Folders and zips both count — a pack being unzipped is a
        convenience during development, not a different kind of thing. */
    static juce::Array<Pack> scan()
    {
        juce::Array<Pack> packs;

        for (const auto& f : directory().findChildFiles (juce::File::findFilesAndDirectories, false))
        {
            const bool zipped = f.existsAsFile() && f.getFileName().endsWithIgnoreCase (".orbitrig.zip");
            const bool folder = f.isDirectory() && f.getChildFile ("rig.json").existsAsFile();

            if (! zipped && ! folder)
                continue;

            Pack p;
            p.location = f;
            p.zipped   = zipped;

            const auto manifest = readEntry (p, "rig.json");
            if (manifest.isEmpty())
                continue;

            bool ok = false;
            p.rig  = namz::rig::loadRigManifest (manifest.toStdString(), &ok);
            p.name = p.rig.name.empty() ? f.getFileNameWithoutExtension() : juce::String (p.rig.name);

            if (ok && p.isValid())
                packs.add (std::move (p));
        }

        std::sort (packs.begin(), packs.end(),
                   [] (const Pack& a, const Pack& b) { return a.name < b.name; });

        return packs;
    }

    /** A named entry's bytes — the model files, or the manifest itself. Empty when missing. */
    static juce::MemoryBlock readBinaryEntry (const Pack& pack, const juce::String& entryName)
    {
        juce::MemoryBlock out;

        if (! pack.zipped)
        {
            pack.location.getChildFile (entryName).loadFileAsData (out);
            return out;
        }

        juce::ZipFile zip (pack.location);
        const int index = zip.getIndexOfFileName (entryName, true);
        if (index < 0)
            return out;

        if (auto stream = std::unique_ptr<juce::InputStream> (zip.createStreamForEntry (index)))
            stream->readIntoMemoryBlock (out);

        return out;
    }

private:
    static juce::String readEntry (const Pack& pack, const juce::String& entryName)
    {
        const auto data = readBinaryEntry (pack, entryName);
        return data.getSize() > 0 ? juce::String::fromUTF8 ((const char*) data.getData(), (int) data.getSize())
                                  : juce::String();
    }
};

} // namespace orbitamp::device
