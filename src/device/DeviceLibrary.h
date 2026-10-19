// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../Product.h"

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
    /** Which block a device belongs in front of.

        A pack names it (`pedal`, `preamp`, `amp`), and a lone .nam carries the same word in its
        header. Without this every list shows every device: pedals offered as preamps, preamps as
        pedals.

        SAYING NOTHING and SAYING SOMETHING WE DO NOT HAVE are different answers, and conflating
        them is how a pack that calls itself a power amp turned up in the BOOST list. `any` is for
        a file that names no slot at all — a model somebody dropped in is a model they want to
        hear, so it is offered everywhere. `none` is for a file that names a slot this instrument
        does not have: it is not ours to place, so it is offered nowhere. */
    enum class Slot { pedal, preamp, any, none };

    /** WHAT THE AMP PLAYS: every pack whose slot this instrument has — preamps, amps, and the
        boosters too, since the AMP is the one captured block there is and a pedal's capture is a
        sound a player put in their folder to hear. */
    static constexpr Slot ampSlot = Slot::any;

    static Slot slotFromString (const std::string& s)
    {
        const auto l = juce::String (s).trim().toLowerCase();

        if (l.isEmpty())                     return Slot::any;
        if (l == "pedal")                    return Slot::pedal;
        if (l == "preamp" || l == "amp")     return Slot::preamp;

        return Slot::none;
    }

    struct Pack
    {
        juce::String  name;       // the device as the pack file names it — "IR-X Ch1", "Guitar Butler Clean"
        juce::String  alias;      // the name the voice goes out under (the manifest's `name`), when it has one
        juce::File    location;   // the folder, the .orbitrig zip, or a lone .nam/.namz
        bool          zipped = false;
        bool          loose  = false;   // a single model file the user dropped in, not a pack
        bool          bundled = false;  // shipped with the plugin rather than added by the user
        int           character = 0;    // place on the ramp, from the stage's tone_type
        Slot          slot = Slot::any; // which block it belongs in front of
        juce::String  rigId;            // the manifest's `rig_id` — the capture's identity; empty for a lone model
        juce::String  familyId;         // the manifest's `family_id` — the DEVICE this pack is a channel of
        juce::StringArray folder;       // the folders of the player's Devices folder it lies in, outermost first
        juce::Time    modified;         // when the file on disk last changed — the tie-break between copies
        namz::rig::Rig rig;

        /** What a player sees: the DEVICE, by the name the pack file carries. The manifest's own
            `name` is the voice's alias and rides along on the paper; two channels of one preamp share
            a make and a model, and only the file tells them apart. */
        juce::String displayName() const { return name.isNotEmpty() ? name : alias; }

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

    /** The FAMILY's folder, `Darwin's Cat`: every product's own folder is in it, and the set they share.
        JUCE's userApplicationDataDirectory is ~/Library on macOS, not ~/Library/Application Support —
        without this the folder lands one level too high and nothing a user drops in is ever found. */
    static juce::File familyRoot()
    {
        auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);

       #if JUCE_MAC
        dir = dir.getChildFile ("Application Support");
       #endif

        return dir.getChildFile ("Darwin's Cat");
    }

    /** The PRODUCT's folder: two faces of one engine each keep the devices the PLAYER adds, and neither
        reads the other's. The player's IRs are the family's (IrLibrary). For OrbitAmp this is the folder
        it has always been. */
    static juce::File appDataRoot()
    {
        return familyRoot().getChildFile (product::name);
    }

    static juce::File directory()
    {
        auto dir = appDataRoot().getChildFile ("Devices");

        if (! dir.isDirectory())
            dir.createDirectory();

        return dir;
    }

    /** THE FAMILY'S `Factory`, beside every product's folder: where each product lays out the set it
        carries in its binary when it starts (FactorySet.h) — packs at its top, cabinets in `IRs` — so a
        pack two products carry is one file, and every product's list shows every pack the family has
        laid out. Not the player's to edit, and not made here — with no build that carries a set, it is
        absent. */
    static juce::File factoryDirectory()
    {
        return familyRoot().getChildFile ("Factory");
    }

    /** Where a build from before the family shared one laid its set out: the product's own `Factory`.
        Not read any more — only emptied, once (FactorySet::retire). */
    static juce::File formerFactoryDirectory()
    {
        return appDataRoot().getChildFile ("Factory");
    }

    /** Where the devices the builds carry live: the factory folder their sets are laid out in, one for
        the family. The factory LAYER is a fact of the product — the manager edits only what sits on top
        of it, so something has to say which is which. It used to be looked for inside the Mac's bundle
        first; the set is in the binary now, on every system, and is laid out in one place. */
    static juce::File bundledDirectory()
    {
        return factoryDirectory();
    }

    /** Everything loadable, in the order the AMP's menu lists it (see `Shelf`).

        Bundled devices first, then whatever the user added at the top of their folder — packs and
        lone models alike, since a `.nam` someone dropped in is theirs exactly as much as an
        `.orbitrig` is — then what lies in the folders inside it. The player's folder is read to any
        depth; the factory one only at its top, where the build lays its packs. The two folders are
        the machine's unless a caller — a gate, in a folder of its own — names others. */
    static juce::Array<Pack> scan (Slot wanted = Slot::any, bool onePerCapture = true,
                                   const juce::File& bundled = bundledDirectory(),
                                   const juce::File& own = directory())
    {
        juce::Array<Pack> packs;

        scanFolder (bundled, true, packs, {}, 0);
        scanFolder (own, false, packs, {}, maxFolderDepth);

        // A pack that names a slot we do not have belongs in no list at all, whatever is being
        // asked for — including a scan that asks for `any`.
        packs.removeIf ([] (const Pack& p) { return p.slot == Slot::none; });

        // One capture, one entry — see `keepOnePerCapture`. The library manager asks for every
        // file instead: a copy it hid would be a copy nobody could remove.
        if (onePerCapture)
            keepOnePerCapture (packs);

        if (wanted != Slot::any)
            packs.removeIf ([wanted] (const Pack& p)
                            { return p.slot != wanted && p.slot != Slot::any; });

        arrange (packs);
        return packs;
    }

    //==============================================================================
    /** A DEVICE: one manifest `family_id`, and its packs are its CHANNELS — the X88-IR's four, the
        IR-X's two. A pack that names no family, a lone model, and every pack inside a folder of the
        player's are devices of one channel each.

        Its NAME is what the file names of its packs share, word by word from the front: "IR-X Ch1
        PowerAmp" and "IR-X Ch2 PowerAmp" are the IR-X, "V4 Kraken Ch1" and "Ch2" the V4 Kraken, and a
        device of one pack is called what its file is. Files that share no first word fall back to the
        family's id.

        A CHANNEL is called by its pack's alias, the manifest's `name` — "Channel 1", "Ember Mantra" —
        where the aliases tell the channels apart. Where they do not (all four X88-IR packs say
        "X88-IR") it is called by its file's name with the device's cut off: "X88-IR Crunch" is the
        X88-IR's CRUNCH. The channels stand clean to hi-gain, as their packs' tone types say, and
        file names settle ties. */
    struct Device
    {
        juce::String      name;
        juce::Array<int>  channels;       // the packs, as indices into the list it was read from, in channel order
        juce::StringArray channelNames;   // in the same order
    };

    /** A folder inside the player's Devices folder, as the disk has it: its packs one per line by
        file name — a folder is where a player keeps what they put away, and it is listed, not
        grouped — and the folders inside it. Folders with no pack anywhere in them are not here. */
    struct Folder
    {
        juce::String        name;
        std::vector<Device> devices;
        std::vector<Folder> folders;
    };

    /** WHAT THE AMP'S MENU LISTS: the family's devices from Factory, the devices at the top of the
        player's own folder, and each folder in it, mirrored. Each part is in the order of its names. */
    struct Shelf
    {
        std::vector<Device> factory;
        std::vector<Device> own;
        std::vector<Folder> folders;

        /** The device the pack at `index` is a channel of; nullptr for an index the shelf does not hold. */
        const Device* deviceOf (int index) const
        {
            for (const auto* part : { &factory, &own })
                for (const auto& d : *part)
                    if (d.channels.contains (index))
                        return &d;

            return deviceIn (folders, index);
        }

        /** Every pack, in the order the menu shows it — a device's channels together, in channel order. */
        juce::Array<int> order() const
        {
            juce::Array<int> out;

            for (const auto* part : { &factory, &own })
                for (const auto& d : *part)
                    out.addArray (d.channels);

            walk (folders, out);
            return out;
        }

    private:
        static const Device* deviceIn (const std::vector<Folder>& in, int index)
        {
            for (const auto& f : in)
            {
                for (const auto& d : f.devices)
                    if (d.channels.contains (index))
                        return &d;

                if (const auto* d = deviceIn (f.folders, index))
                    return d;
            }

            return nullptr;
        }

        static void walk (const std::vector<Folder>& in, juce::Array<int>& out)
        {
            for (const auto& f : in)
            {
                for (const auto& d : f.devices)
                    out.addArray (d.channels);

                walk (f.folders, out);
            }
        }
    };

    /** The shelf a list of packs makes. Indices in it are into `packs`. */
    static Shelf shelfOf (const juce::Array<Pack>& packs)
    {
        Shelf shelf;

        for (const bool bundled : { true, false })
        {
            // One family, one device — within its place: the build's and the player's are two lists.
            juce::StringArray keys;
            std::vector<juce::Array<int>> members;

            for (int i = 0; i < packs.size(); ++i)
            {
                const auto& p = packs.getReference (i);

                if (p.bundled != bundled || ! p.folder.isEmpty())
                    continue;

                // No family, no one to share a device with: a key nothing else can have.
                const auto key = p.familyId.isNotEmpty() ? p.familyId : "\x01" + juce::String (i);

                int k = keys.indexOf (key);
                if (k < 0)
                {
                    keys.add (key);
                    members.emplace_back();
                    k = keys.size() - 1;
                }

                members[(size_t) k].add (i);
            }

            auto& part = bundled ? shelf.factory : shelf.own;

            for (size_t k = 0; k < members.size(); ++k)
                part.push_back (deviceFrom (packs, members[k]));

            sortByName (part);
        }

        for (int i = 0; i < packs.size(); ++i)
        {
            const auto& p = packs.getReference (i);

            if (p.folder.isEmpty())
                continue;

            auto* level = &shelf.folders;
            Folder* here = nullptr;

            for (const auto& name : p.folder)
            {
                auto it = std::find_if (level->begin(), level->end(),
                                        [&name] (const Folder& f) { return f.name == name; });
                if (it == level->end())
                {
                    level->push_back ({ name, {}, {} });
                    it = level->end() - 1;
                }

                here  = &*it;
                level = &here->folders;
            }

            Device d;
            d.name = p.displayName();
            d.channels.add (i);
            d.channelNames.add (p.displayName());
            here->devices.push_back (std::move (d));
        }

        sortFolders (shelf.folders);
        return shelf;
    }

    /** Puts `packs` in the shelf's order — the menu's, so a host stepping the device parameter walks
        the list the player sees. */
    static void arrange (juce::Array<Pack>& packs)
    {
        juce::Array<Pack> out;

        for (const int i : shelfOf (packs).order())
            out.add (packs.getReference (i));

        packs = std::move (out);
    }

    /** The words two or more names start with in common — "V4 Kraken" of "V4 Kraken Ch1" and "V4
        Kraken Ch2"; all of a lone name; nothing when the first words differ. */
    static juce::StringArray commonLeadingWords (const juce::StringArray& names)
    {
        if (names.isEmpty())
            return {};

        auto common = juce::StringArray::fromTokens (names[0], false);

        for (int n = 1; n < names.size(); ++n)
        {
            const auto words = juce::StringArray::fromTokens (names[n], false);
            int same = 0;

            while (same < common.size() && same < words.size() && common[same] == words[same])
                ++same;

            common.removeRange (same, common.size() - same);
        }

        common.removeEmptyStrings();
        return common;
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

    /** Whether a path is something the Devices folder can hold: a lone model, an `.orbitrig.zip`,
        or an unpacked pack folder — its `rig.json` is the tell, same as the scanner's. */
    static bool looksLikeDevice (const juce::File& f)
    {
        if (f.isDirectory())
            return f.getChildFile ("rig.json").existsAsFile();

        return f.existsAsFile()
            && (f.hasFileExtension ("nam;namz") || f.getFileName().endsWithIgnoreCase (".orbitrig.zip"));
    }

    /** A pack's capture identity: its manifest's `rig_id`. Empty for a lone model, a pack with no
        manifest, or one whose manifest does not say. */
    static juce::String rigIdOf (const juce::File& packFileOrFolder)
    {
        Pack p;
        p.location = packFileOrFolder;
        p.zipped   = packFileOrFolder.existsAsFile()
                  && packFileOrFolder.getFileName().endsWithIgnoreCase (".orbitrig.zip");

        if (! p.zipped && ! packFileOrFolder.getChildFile ("rig.json").existsAsFile())
            return {};

        const auto manifest = readEntry (p, "rig.json");
        if (manifest.isEmpty())
            return {};

        bool ok = false;
        return juce::String (namz::rig::loadRigManifest (manifest.toStdString(), &ok).rigId).trim();
    }

    /** ONE CAPTURE, ONE ENTRY. A pack's `rig_id` is the capture's identity — two channels of one
        preamp are two ids, the same pack imported twice is one — so packs sharing an id are copies,
        and a list that showed both would offer the same sound twice under two names. The copy that
        stays: a bundled one over the player's (the build's layer is the floor everything else
        stands on), then the newest file, then the name. Lone models and packs that state no id
        are never merged: without an identity, two files are two sounds. */
    static void keepOnePerCapture (juce::Array<Pack>& packs)
    {
        const auto better = [] (const Pack& a, const Pack& b)
        {
            if (a.bundled != b.bundled)   return a.bundled;
            if (a.modified != b.modified) return a.modified > b.modified;
            return a.name < b.name;
        };

        juce::Array<Pack> kept;

        for (const auto& p : packs)
        {
            if (p.loose || p.rigId.isEmpty())
            {
                kept.add (p);
                continue;
            }

            int same = -1;
            for (int i = 0; i < kept.size(); ++i)
                if (! kept.getReference (i).loose && kept.getReference (i).rigId == p.rigId)
                    { same = i; break; }

            if (same < 0)
                kept.add (p);
            else if (better (p, kept.getReference (same)))
                kept.set (same, p);
        }

        packs = std::move (kept);
    }

    /** Copies a device into the user folder, name kept, collisions numbered — two files with one
        name are two different sounds until somebody has listened. Returns the installed location;
        invalid when `src` is not a device, the copy failed, or it was refused.

        A PACK THAT IS ALREADY HERE is not installed beside itself. Its `rig_id` says whether it is:
        a copy the player installed before is RETIRED — to the Trash by default, so a re-import is an
        update and nothing is lost — and the new one takes its place; a pack the build ships is not
        the player's to replace, and the import is refused — `refused`, when given, is then set to
        the name of the pack the build ships, so the player can be told why nothing arrived.
        Re-importing the very file that is installed changes nothing. */
    static juce::File importDevice (const juce::File& src, const juce::File& into = directory(),
                                    const juce::File& bundled = bundledDirectory(),
                                    const std::function<bool (const juce::File&)>& retire
                                        = [] (const juce::File& f) { return f.moveToTrash(); },
                                    juce::String* refused = nullptr)
    {
        if (! looksLikeDevice (src))
            return {};

        const auto rigId = rigIdOf (src);
        juce::Array<juce::File> copies;

        if (rigId.isNotEmpty())
        {
            if (const auto shipped = installedWithRigId (bundled, rigId); ! shipped.isEmpty())
            {
                if (refused != nullptr)
                    *refused = shipped.getFirst().getFileName().upToFirstOccurrenceOf (".", false, false);
                return {};
            }

            copies = installedWithRigId (into, rigId);

            if (copies.contains (src))
                return src;
        }

        const auto dst = uniqueIn (into, src.getFileName());

        const bool ok = src.isDirectory() ? src.copyDirectoryTo (dst)
                                          : src.copyFileTo (dst);
        if (! ok)
            return {};

        // Copied first, retired after: a failed copy must not cost the player the pack they had.
        for (const auto& old : copies)
            retire (old);

        // The new copy was numbered only because the old one still stood on its name. It has gone:
        // the name is free again, and the pack takes it back.
        if (const auto wanted = into.getChildFile (src.getFileName()); dst != wanted && ! wanted.exists())
            if (dst.moveFileTo (wanted))
                return wanted;

        return dst;
    }

    /** A user device out of the library, to the Trash — recoverable, unlike a delete. Bundled
        entries are the build's, not the user's: refused here, hidden as an option in the UI. */
    static bool removeDevice (const Pack& p)
    {
        return ! p.bundled && p.location.isAChildOf (directory()) && p.location.moveToTrash();
    }

    /** A landing name in `into` that collides with nothing. "Name 2" by hand rather than JUCE's
        getNonexistentSibling, whose number lands before the LAST dot — turning a multi-dot name
        like `amp.orbitrig.zip` into one the scanner no longer recognises. The split is at the
        FIRST dot, so whatever extension chain the name carries survives intact. */
    static juce::File uniqueIn (const juce::File& into, const juce::String& wantedName)
    {
        auto candidate = into.getChildFile (wantedName);
        if (! candidate.exists())
            return candidate;

        const auto name = wantedName.upToFirstOccurrenceOf (".", false, false);
        const auto ext  = wantedName.substring (name.length());

        for (int i = 2;; ++i)
        {
            candidate = into.getChildFile (name + " " + juce::String (i) + ext);
            if (! candidate.exists())
                return candidate;
        }
    }

private:
    /** How deep into the player's folder the scanner goes. A folder linked into itself is a loop the
        disk can hold, and this is where it stops. */
    static constexpr int maxFolderDepth = 8;

    /** The device a family's packs make — see `Device`. */
    static Device deviceFrom (const juce::Array<Pack>& packs, juce::Array<int> members)
    {
        std::sort (members.begin(), members.end(), [&packs] (int a, int b)
        {
            const auto& pa = packs.getReference (a);
            const auto& pb = packs.getReference (b);

            if (pa.character != pb.character)
                return pa.character < pb.character;

            return pa.name.compareNatural (pb.name) < 0;
        });

        juce::StringArray stems, aliases;

        for (const int i : members)
        {
            stems.add (packs.getReference (i).displayName());
            aliases.add (packs.getReference (i).alias);
        }

        const auto common = commonLeadingWords (stems);
        const auto& first = packs.getReference (members.getFirst());

        Device d;
        d.channels = members;
        d.name = ! common.isEmpty()         ? common.joinIntoString (" ")
               : first.familyId.isNotEmpty() ? first.familyId
                                             : first.displayName();

        // The aliases name the channels only when they tell every one of them apart.
        auto distinct = aliases;
        distinct.removeDuplicates (true);
        const bool byAlias = ! aliases.contains ({}) && distinct.size() == aliases.size();

        for (int c = 0; c < members.size(); ++c)
        {
            if (byAlias)
            {
                d.channelNames.add (aliases[c]);
                continue;
            }

            auto words = juce::StringArray::fromTokens (stems[c], false);
            words.removeEmptyStrings();
            words.removeRange (0, common.size());

            d.channelNames.add (words.isEmpty() ? stems[c] : words.joinIntoString (" "));
        }

        return d;
    }

    static void sortByName (std::vector<Device>& devices)
    {
        std::stable_sort (devices.begin(), devices.end(), [] (const Device& a, const Device& b)
                          { return a.name.compareNatural (b.name) < 0; });
    }

    static void sortFolders (std::vector<Folder>& folders)
    {
        std::stable_sort (folders.begin(), folders.end(), [] (const Folder& a, const Folder& b)
                          { return a.name.compareNatural (b.name) < 0; });

        for (auto& f : folders)
        {
            sortByName (f.devices);
            sortFolders (f.folders);
        }
    }

    /** The packs directly in `dir` whose manifest names this `rig_id`. */
    static juce::Array<juce::File> installedWithRigId (const juce::File& dir, const juce::String& rigId)
    {
        juce::Array<juce::File> found;

        if (dir.isDirectory())
            for (const auto& f : dir.findChildFiles (juce::File::findFilesAndDirectories, false))
            {
                const bool pack = (f.existsAsFile() && f.getFileName().endsWithIgnoreCase (".orbitrig.zip"))
                               || (f.isDirectory() && f.getChildFile ("rig.json").existsAsFile());

                if (pack && rigIdOf (f) == rigId)
                    found.add (f);
            }

        return found;
    }

    /** The packs in `dir`, and — `depth` levels down — in the folders inside it, each pack knowing
        which folders it was found in. */
    static void scanFolder (const juce::File& dir, bool bundled, juce::Array<Pack>& out,
                            const juce::StringArray& within, int depth)
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
                p.folder   = within;
                p.modified = f.getLastModificationTime();

                const auto manifest = readEntry (p, "rig.json");
                if (manifest.isEmpty())
                    continue;

                bool ok = false;
                p.rig  = namz::rig::loadRigManifest (manifest.toStdString(), &ok);

                // The file's name up to its first dot — "IR-X Ch1" from "IR-X Ch1.orbitrig.zip", the
                // folder's own name for a folder — the same cut `uniqueIn` makes when it numbers one.
                p.name  = f.getFileName().upToFirstOccurrenceOf (".", false, false).trim();
                p.alias = juce::String (p.rig.name).trim();
                p.rigId = juce::String (p.rig.rigId).trim();
                p.familyId = juce::String (p.rig.familyId).trim();
                if (p.name.isEmpty())
                    p.name = p.alias;

                if (const auto* s = p.rig.firstKnown())
                {
                    p.character = characterFromToneType (s->toneType);
                    p.slot = slotFromString (s->slot);
                }

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
                p.folder   = within;
                p.character = characterFromToneType (value ("tone_type"));
                p.slot = slotFromString (! value ("slot").empty() ? value ("slot")
                                                                 : value ("gear_type"));

                const auto modelled = value ("gear_model");
                p.name  = modelled.empty() ? f.getFileNameWithoutExtension() : juce::String (modelled);
                p.alias = juce::String (value ("alias")).trim();

                namz::rig::Stage stage;
                stage.kind     = namz::rig::StageKind::Nam;
                stage.toneType = value ("tone_type");
                stage.circuit  = value ("device");
                stage.device.family = p.displayName().toStdString();
                stage.device.files.push_back ({ f.getFileName().toStdString(), {}, 0.0, {} });

                p.rig.name = p.displayName().toStdString();
                p.rig.chain.push_back (std::move (stage));

                out.add (std::move (p));
                continue;
            }

            // Any other folder is one of the player's own, and what lies in it is theirs too.
            if (f.isDirectory() && depth > 0)
            {
                auto inner = within;
                inner.add (f.getFileName());
                scanFolder (f, bundled, out, inner, depth - 1);
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
