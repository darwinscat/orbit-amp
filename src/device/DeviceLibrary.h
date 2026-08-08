#pragma once

#include <namz.h>
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
        juce::File    location;   // the folder, the .orbitrig zip, or a lone .nam/.namz
        bool          zipped = false;
        bool          loose  = false;   // a single model file the user dropped in, not a pack
        bool          bundled = false;  // shipped with the plugin rather than added by the user
        int           character = 0;    // place on the ramp, from the stage's tone_type
        namz::rig::Rig rig;

        bool isValid() const { return ! rig.chain.empty(); }
    };

    /** The ordered gain scale namz states per stage. Green to red, and the order IS the sort.

        Unstated lands at the clean end rather than nowhere: a device that did not say is far more
        often a clean one than a metal one, and it has to sort somewhere. */
    static int characterFromToneType (const std::string& toneType)
    {
        const auto s = juce::String (toneType).trim().toLowerCase();

        if (s == "clean")                       return 0;
        if (s == "edge")                        return 1;
        if (s == "crunch")                      return 2;
        if (s == "hi-gain" || s == "high-gain") return 3;
        if (s == "modern")                      return 4;
        return 0;
    }

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

    /** Where the plugin's own devices live, inside the bundle. Absent during development, which is
        the honest state of it: nothing ships curated yet. */
    static juce::File bundledDirectory()
    {
        return juce::File::getSpecialLocation (juce::File::currentApplicationFile)
                 .getChildFile ("Contents").getChildFile ("Resources").getChildFile ("Devices");
    }

    /** Everything loadable, ordered green to red by how much gain it is.

        Bundled devices first, then whatever the user added — packs and lone models alike, since a
        `.nam` someone dropped in is theirs exactly as much as an `.orbitrig` is. Within each of the
        two, the character ramp orders the list and the name settles ties. */
    static juce::Array<Pack> scan()
    {
        juce::Array<Pack> packs;

        scanFolder (bundledDirectory(), true, packs);
        scanFolder (directory(), false, packs);

        std::sort (packs.begin(), packs.end(), [] (const Pack& a, const Pack& b)
        {
            if (a.bundled != b.bundled)       return a.bundled;
            if (a.character != b.character)   return a.character < b.character;
            return a.name < b.name;
        });

        return packs;
    }

    /** A named entry's bytes — the model files, or the manifest itself. Empty when missing. */
    static juce::MemoryBlock readBinaryEntry (const Pack& pack, const juce::String& entryName)
    {
        juce::MemoryBlock out;

        // A lone model is its own only entry: whatever is asked for, there is one file to give.
        if (pack.loose)
        {
            pack.location.loadFileAsData (out);
            return out;
        }

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
    static void scanFolder (const juce::File& dir, bool bundled, juce::Array<Pack>& out)
    {
        if (! dir.isDirectory())
            return;

        for (const auto& f : dir.findChildFiles (juce::File::findFilesAndDirectories, false))
        {
            const bool zipped = f.existsAsFile() && f.getFileName().endsWithIgnoreCase (".orbitrig.zip");
            const bool folder = f.isDirectory() && f.getChildFile ("rig.json").existsAsFile();

            if (zipped || folder)
            {
                Pack p;
                p.location = f;
                p.zipped   = zipped;
                p.bundled  = bundled;

                const auto manifest = readEntry (p, "rig.json");
                if (manifest.isEmpty())
                    continue;

                bool ok = false;
                p.rig  = namz::rig::loadRigManifest (manifest.toStdString(), &ok);
                p.name = p.rig.name.empty() ? f.getFileNameWithoutExtension() : juce::String (p.rig.name);

                if (const auto* s = p.rig.firstKnown())
                    p.character = characterFromToneType (s->toneType);

                if (ok && p.isValid())
                    out.add (std::move (p));

                continue;
            }

            // A lone model is ONE device with no knobs. Not a family to be grouped: grouping is what a
            // pack's manifest is for, and inferring it from filenames would be guessing at a matrix
            // nobody captured. One file in, one entry in the list, plays as it is.
            if (f.existsAsFile() && (f.hasFileExtension ("nam") || f.hasFileExtension ("namz")))
            {
                const auto meta = readModelMeta (f);
                const auto value = [&meta] (const char* key) -> std::string
                {
                    const auto it = meta.find (key);
                    return it != meta.end() ? it->second : std::string();
                };

                Pack p;
                p.location = f;
                p.loose    = true;
                p.bundled  = bundled;
                p.character = characterFromToneType (value ("tone_type"));

                const auto modelled = value ("gear_model");
                p.name = modelled.empty() ? f.getFileNameWithoutExtension() : juce::String (modelled);

                namz::rig::Stage stage;
                stage.kind     = namz::rig::StageKind::Nam;
                stage.toneType = value ("tone_type");
                stage.circuit  = value ("device");
                stage.device.family = p.name.toStdString();
                stage.device.files.push_back ({ f.getFileName().toStdString(), {}, 0.0 });

                p.rig.name = p.name.toStdString();
                p.rig.chain.push_back (std::move (stage));

                out.add (std::move (p));
            }
        }
    }

    /** A model's header. `.namz` carries it in the container; a bare `.nam` is JSON with a metadata
        object, which is JUCE's job to read rather than namz's — namz owns the container, not the
        format NAM itself writes. */
    static std::map<std::string, std::string> readModelMeta (const juce::File& f)
    {
        std::map<std::string, std::string> meta;

        if (f.hasFileExtension ("namz"))
        {
            juce::MemoryBlock mb;
            if (f.loadFileAsData (mb))
                meta = namz::readMeta (mb.getData(), mb.getSize());

            return meta;
        }

        const auto json = juce::JSON::parse (f.loadFileAsString());
        if (const auto* obj = json.getDynamicObject())
            if (const auto m = obj->getProperty ("metadata"); const auto* mo = m.getDynamicObject())
                for (const auto& prop : mo->getProperties())
                    meta[prop.name.toString().toStdString()] = prop.value.toString().toStdString();

        return meta;
    }

    static juce::String readEntry (const Pack& pack, const juce::String& entryName)
    {
        const auto data = readBinaryEntry (pack, entryName);
        return data.getSize() > 0 ? juce::String::fromUTF8 ((const char*) data.getData(), (int) data.getSize())
                                  : juce::String();
    }
};

} // namespace orbitamp::device
