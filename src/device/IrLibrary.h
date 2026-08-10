#pragma once

#include "DeviceLibrary.h"   // appDataRoot — the IRs live beside the Devices folder

#include <juce_core/juce_core.h>

#include <vector>

namespace orbitamp::device
{

/** The user's cabinet IRs on disk: a folder tree under `.../OrbitAmp/IRs`.

    There is no pack format here — a vendor "pack" is just a folder of wavs, or the zip of one, so
    importing either makes a plain folder in the tree and the folder IS the pack. That also means
    folders are the user's to rename and rearrange; nothing depends on their names. Nothing here
    reads audio: the manager curates files, and whoever plays an IR reads it when it plays.

    Every operation takes the root it works under and refuses to touch anything outside it, so a
    stray path can never rename or trash something that is not the library's. */
class IrLibrary
{
public:
    /** Where the user's IRs live. Created on first look, so there is a folder to drop into. */
    static juce::File directory()
    {
        auto dir = DeviceLibrary::appDataRoot().getChildFile ("IRs");

        if (! dir.isDirectory())
            dir.createDirectory();

        return dir;
    }

    /** What counts as an IR on disk. Vendors ship wav almost always, aiff sometimes; everything
        else in a pack (readme, license, artwork) is not an IR and stays out of the tree. */
    static bool isAudioFile (const juce::File& f)
    {
        return f.existsAsFile() && f.hasFileExtension ("wav;aif;aiff");
    }

    /** What an import will take: an IR itself, a zip of them, or a folder holding some. */
    static bool looksLikeImport (const juce::File& f)
    {
        return isAudioFile (f) || f.isDirectory() || (f.existsAsFile() && f.hasFileExtension ("zip"));
    }

    struct Node
    {
        juce::File        file;
        bool              folder = false;
        std::vector<Node> children;   // folders first, each group in natural order

        juce::String name() const { return file.getFileName(); }
    };

    /** The tree as it stands. The returned node is the root itself; only its children show. */
    static Node scan (const juce::File& root = directory())
    {
        Node n;
        n.file   = root;
        n.folder = true;

        for (const auto& f : root.findChildFiles (juce::File::findFilesAndDirectories, false))
        {
            if (f.isHidden())
                continue;

            if (f.isDirectory())
                n.children.push_back (scan (f));
            else if (isAudioFile (f))
                n.children.push_back ({ f, false, {} });
        }

        std::sort (n.children.begin(), n.children.end(), [] (const Node& a, const Node& b)
        {
            if (a.folder != b.folder)
                return a.folder;
            return a.name().compareNatural (b.name()) < 0;
        });

        return n;
    }

    /** Imports whatever `src` is — an audio file, a folder, or a zip — into `into` (a folder in
        the library). A folder copies filtered to audio; a zip unpacks the same way into a new
        folder. Returns how many IR files landed, so a zip of readmes honestly counts zero. */
    static int importPath (const juce::File& src, const juce::File& into)
    {
        if (isAudioFile (src))
            return src.copyFileTo (uniqueIn (into, src.getFileName())) ? 1 : 0;

        if (src.isDirectory())
            return copyAudioTree (src, uniqueIn (into, src.getFileName()));

        if (src.existsAsFile() && src.hasFileExtension ("zip"))
            return unpackZip (src, into);

        return 0;
    }

    /** A new empty folder under `parent` (a folder in the library, or the root itself) — the one
        thing made in place rather than imported, so a collection can be shaped before anything
        fills it. A taken name gets numbered, same as an import. Invalid when parent is neither
        the root nor the library's. */
    static juce::File createFolder (const juce::File& root, const juce::File& parent,
                                    const juce::String& name)
    {
        if (parent != root && ! isManaged (root, parent))
            return {};

        const auto legal = juce::File::createLegalFileName (name.trim());
        if (legal.isEmpty() || ! parent.isDirectory())
            return {};

        const auto dir = uniqueIn (parent, legal);
        return dir.createDirectory() ? dir : juce::File();
    }

    /** Renames a file or folder in place. The extension is the file's identity, not its name, so a
        file keeps its own extension whatever the new name says. False when the name is taken, the
        item is not the library's, or the item is the root itself. */
    static bool rename (const juce::File& root, const juce::File& item, const juce::String& newName)
    {
        if (! isManaged (root, item))
            return false;

        auto legal = juce::File::createLegalFileName (newName.trim());
        if (legal.isEmpty())
            return false;

        if (! item.isDirectory())
            legal = legal.upToLastOccurrenceOf (".", false, false) + item.getFileExtension();

        const auto target = item.getSiblingFile (legal);
        if (target == item)
            return true;

        if (target.exists())
            return false;

        return item.moveFileTo (target);
    }

    /** To the Trash, not gone — a slip of the mouse should cost a trip to the bin, not a pack. */
    static bool remove (const juce::File& root, const juce::File& item)
    {
        return isManaged (root, item) && item.moveToTrash();
    }

private:
    /** True only for something that actually sits inside the library — the one gate every
        destructive operation goes through. */
    static bool isManaged (const juce::File& root, const juce::File& item)
    {
        return item.exists() && item != root && item.isAChildOf (root);
    }

    /** DeviceLibrary's numbered-name helper, shared — one naming rule for both libraries. */
    static juce::File uniqueIn (const juce::File& into, const juce::String& wantedName)
    {
        return DeviceLibrary::uniqueIn (into, wantedName);
    }

    /** Copies only the audio out of a folder, keeping its shape. Folders with no audio anywhere
        below them are not copied at all — an empty folder in the tree would be a shelf with
        nothing on it. */
    static int copyAudioTree (const juce::File& srcDir, const juce::File& dstDir)
    {
        int landed = 0;

        for (const auto& f : srcDir.findChildFiles (juce::File::findFilesAndDirectories, false))
        {
            if (f.isHidden())
                continue;

            if (f.isDirectory())
                landed += copyAudioTree (f, dstDir.getChildFile (f.getFileName()));
            else if (isAudioFile (f))
            {
                if (dstDir.createDirectory() && f.copyFileTo (dstDir.getChildFile (f.getFileName())))
                    ++landed;
            }
        }

        return landed;
    }

    /** Unpacks a zip's audio into a new folder under `into`.

        The folder takes the vendor's own name when the zip has one — a single top-level folder
        holding everything — and the zip's filename when the entries sit at the root. Entry paths
        are re-rooted and sanitised segment by segment, so a hostile "../" in a zip lands inside
        the library or not at all. */
    static int unpackZip (const juce::File& zipFile, const juce::File& into)
    {
        juce::ZipFile zip (zipFile);

        // The audio entries, as sanitised segment lists. Anything hidden (`__MACOSX`, dotfiles),
        // non-audio, or path-hostile disappears here, before any name decisions are made.
        struct Entry { int index; juce::StringArray segments; };
        std::vector<Entry> entries;

        for (int i = 0; i < zip.getNumEntries(); ++i)
        {
            const auto* e = zip.getEntry (i);
            if (e == nullptr || e->filename.endsWithChar ('/'))
                continue;

            const auto lower = e->filename.toLowerCase();
            if (! (lower.endsWith (".wav") || lower.endsWith (".aif") || lower.endsWith (".aiff")))
                continue;

            juce::StringArray raw;
            raw.addTokens (e->filename.replaceCharacter ('\\', '/'), "/", {});

            juce::StringArray clean;
            for (const auto& s : raw)
            {
                const auto legal = juce::File::createLegalFileName (s.trim());
                if (legal.isEmpty() || legal.startsWithChar ('.'))
                    { clean.clear(); break; }
                clean.add (legal);
            }

            if (! clean.isEmpty())
                entries.push_back ({ i, std::move (clean) });
        }

        if (entries.empty())
            return 0;

        // One shared top-level folder → that name is the pack's; otherwise the zip's own.
        juce::String sharedTop;
        for (const auto& e : entries)
        {
            if (e.segments.size() < 2)                        // a root-level file has no top
                { sharedTop.clear(); break; }
            if (sharedTop.isEmpty())
                sharedTop = e.segments[0];
            else if (sharedTop != e.segments[0])
                { sharedTop.clear(); break; }
        }

        const auto zipStem = zipFile.getFileName().dropLastCharacters (4);   // ".zip"
        const auto folder  = uniqueIn (into, sharedTop.isNotEmpty() ? sharedTop
                                                                    : juce::File::createLegalFileName (zipStem));

        int landed = 0;

        for (const auto& e : entries)
        {
            auto dst = folder;
            for (int s = sharedTop.isNotEmpty() ? 1 : 0; s < e.segments.size(); ++s)
                dst = dst.getChildFile (e.segments[s]);

            if (! dst.getParentDirectory().createDirectory())
                continue;

            if (auto stream = std::unique_ptr<juce::InputStream> (zip.createStreamForEntry (e.index)))
            {
                juce::FileOutputStream out (dst);
                if (out.openedOk() && out.writeFromInputStream (*stream, -1) >= 0)
                    ++landed;
            }
        }

        return landed;
    }
};

} // namespace orbitamp::device
