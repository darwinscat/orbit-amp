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
#include "device/EmbeddedIrs.h"
#include "device/IrLibrary.h"

#include <cstdio>
#include <cstring>

using orbitamp::device::DeviceLibrary;
using orbitamp::device::EmbeddedIrs;
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

    // ---- IR move: rearranging what is already on the shelf -------------------------------------
    {
        const auto root = work.getChildFile ("irs-move");
        makeWav (root.getChildFile ("loose.wav"));
        makeWav (root.getChildFile ("Mine/loose.wav"));
        makeWav (root.getChildFile ("Pack/Close/57.wav"));
        root.getChildFile ("Mine").createDirectory();

        const auto moved = IrLibrary::move (root, root.getChildFile ("Pack/Close/57.wav"), root.getChildFile ("Mine"));
        report ("a file moves into a folder",        moved == root.getChildFile ("Mine/57.wav") && moved.existsAsFile()
                                                       && ! root.getChildFile ("Pack/Close/57.wav").exists());
        report ("a taken name there gets numbered",  IrLibrary::move (root, root.getChildFile ("loose.wav"), root.getChildFile ("Mine"))
                                                       == root.getChildFile ("Mine/loose 2.wav"));
        report ("a folder moves, contents and all",  IrLibrary::move (root, root.getChildFile ("Pack/Close"), root.getChildFile ("Mine"))
                                                       .isDirectory());
        report ("...and back up to the root",        IrLibrary::move (root, root.getChildFile ("Mine/Close"), root)
                                                       == root.getChildFile ("Close"));
        report ("where it already is, it stays",     IrLibrary::move (root, root.getChildFile ("Close"), root)
                                                       == root.getChildFile ("Close"));
        report ("a folder into itself is refused",   IrLibrary::move (root, root.getChildFile ("Mine"), root.getChildFile ("Mine"))
                                                       == juce::File());

        root.getChildFile ("Mine/Deep").createDirectory();
        report ("...and into its own child",         IrLibrary::move (root, root.getChildFile ("Mine"), root.getChildFile ("Mine/Deep"))
                                                       == juce::File() && root.getChildFile ("Mine/Deep").isDirectory());
        report ("into a file is refused",            IrLibrary::move (root, root.getChildFile ("Mine/57.wav"), root.getChildFile ("Mine/loose.wav"))
                                                       == juce::File());
        report ("out of the library is refused",     IrLibrary::move (root, root.getChildFile ("Mine/57.wav"), work)
                                                       == juce::File() && root.getChildFile ("Mine/57.wav").existsAsFile());
        report ("a stranger's file is refused",      IrLibrary::move (root, work.getChildFile ("src/Cab 4x12.wav"), root)
                                                       == juce::File());
    }

    // ---- Embedded IRs: the bytes travel with a preset, not the path ----------------------------
    {
        const auto bytesOf = [] (const char* text) { return juce::MemoryBlock (text, std::strlen (text)); };

        EmbeddedIrs store;
        const auto keyA = store.add (bytesOf ("impulse A"));
        const auto keyB = store.add (bytesOf ("impulse B"));

        report ("a key is the content's",            keyA.isNotEmpty() && keyA == EmbeddedIrs::keyOf (bytesOf ("impulse A")));
        report ("other bytes, another key",          keyA != keyB);
        report ("nothing is not an IR",              store.add ({}).isEmpty());
        report ("too much is not an IR",             store.add (juce::MemoryBlock ((size_t) EmbeddedIrs::maxBytes + 1)).isEmpty());

        // A preset on its way to disk, through real XML, and back into a store that never saw it.
        juce::ValueTree preset ("PARAMETERS");
        preset.setProperty ("cab_ir_user", keyA, nullptr);

        if (auto node = store.pack ({ keyA, keyA, "missing-key" }); node.isValid())
            preset.appendChild (node, nullptr);

        report ("a repeat is embedded once",         preset.getChildWithName (EmbeddedIrs::treeType).getNumChildren() == 1);
        report ("nothing to carry, nothing packed",  ! store.pack ({ "missing-key" }).isValid());

        const auto xml = preset.createXml();
        auto loaded = juce::ValueTree::fromXml (*juce::XmlDocument::parse (xml->toString()));

        EmbeddedIrs elsewhere;
        elsewhere.unpack (loaded);

        const auto* back = elsewhere.find (keyA);
        report ("the bytes come back whole",         back != nullptr && *back == bytesOf ("impulse A"));
        report ("...and leave the tree plain",       ! loaded.getChildWithName (EmbeddedIrs::treeType).isValid()
                                                       && loaded.getProperty ("cab_ir_user").toString() == keyA);

        // A doctored file: the key it would have claimed is not the one its bytes are filed under.
        juce::ValueTree doctored ("PARAMETERS");
        juce::ValueTree node (EmbeddedIrs::treeType), ir (EmbeddedIrs::entryType);
        ir.setProperty ("key", keyA, nullptr);
        ir.setProperty ("data", juce::var (bytesOf ("impostor")), nullptr);
        node.appendChild (ir, nullptr);
        doctored.appendChild (node, nullptr);

        EmbeddedIrs fresh;
        fresh.unpack (doctored);
        report ("bytes are filed by what they are",  fresh.find (keyA) == nullptr
                                                       && fresh.find (EmbeddedIrs::keyOf (bytesOf ("impostor"))) != nullptr);
    }

    // ---- IR group move: a selection, planned before anything moves ------------------------------
    {
        const auto root = work.getChildFile ("irs-group");
        makeWav (root.getChildFile ("Pack/a.wav"));
        makeWav (root.getChildFile ("Pack/b.wav"));
        makeWav (root.getChildFile ("Pack/Deep/c.wav"));
        makeWav (root.getChildFile ("loose.wav"));
        root.getChildFile ("Dest").createDirectory();

        const auto pack = root.getChildFile ("Pack");
        const auto plan = IrLibrary::planMove (root, { pack, pack.getChildFile ("a.wav"), pack.getChildFile ("Deep/c.wav"),
                                                       root.getChildFile ("loose.wav") }, root.getChildFile ("Dest"));
        report ("a folder and what is inside it move once", plan.size() == 2 && plan.contains (pack)
                                                              && plan.contains (root.getChildFile ("loose.wav")),
                juce::String (plan.size()) + " moves");

        report ("what is already there stays",           IrLibrary::planMove (root, { root.getChildFile ("loose.wav") }, root).isEmpty());
        report ("a folder is not taken into itself",     IrLibrary::planMove (root, { pack }, pack.getChildFile ("Deep")).isEmpty());
        report ("...but its neighbour in the group goes", IrLibrary::planMove (root, { pack, root.getChildFile ("loose.wav") },
                                                                              pack.getChildFile ("Deep"))
                                                            == juce::Array<juce::File> { root.getChildFile ("loose.wav") });
        report ("out to the top level",                  IrLibrary::planMove (root, { pack.getChildFile ("b.wav") }, root)
                                                            == juce::Array<juce::File> { pack.getChildFile ("b.wav") });
        report ("a stranger's file is not planned",      IrLibrary::planMove (root, { work.getChildFile ("src/Cab 4x12.wav") }, root).isEmpty());
    }

    // ---- IRs of before: gathered into the family's folder, copied, never moved -----------------
    {
        const auto family = work.getChildFile ("gather/Darwin's Cat/IRs");
        const auto former = work.getChildFile ("gather/Darwin's Cat/RedPawAmp/IRs");
        const auto put = [] (const juce::File& f, const char* bytes)
        {
            f.getParentDirectory().createDirectory();
            f.replaceWithText (bytes);
            return f;
        };

        put (former.getChildFile ("Same.wav"), "one IR");
        put (former.getChildFile ("Clash.wav"), "the rover's own");
        put (former.getChildFile ("Vendor Pack/Close/57.wav"), "a pack's");
        put (former.getChildFile ("Twice.wav"), "the rover's twice");
        put (former.getChildFile ("readme.txt"), "not an IR");
        put (former.getChildFile (".hidden.wav"), "the Finder's");

        put (family.getChildFile ("Same.wav"), "one IR");                    // the same IR, there already
        put (family.getChildFile ("Clash.wav"), "the cockpit's own");        // another IR of the same name
        put (family.getChildFile ("Twice.wav"), "the cockpit's twice");
        put (family.getChildFile ("Twice (RedPawAmp).wav"), "yet another");  // ...and even the name it would take is taken

        const auto first = IrLibrary::gather (former, family, "RedPawAmp");
        const auto text = [] (const juce::File& f) { return f.existsAsFile() ? f.loadFileAsString() : juce::String ("(none)"); };

        report ("IRs of before: three copied, none failed",   first.copied == 3 && first.failed == 0,
                juce::String (first.copied) + " copied, " + juce::String (first.failed) + " failed");
        report ("...the same name with the same bytes skipped",
                text (family.getChildFile ("Same.wav")) == "one IR" && ! family.getChildFile ("Same (RedPawAmp).wav").exists());
        report ("...the same name with other bytes kept both",
                text (family.getChildFile ("Clash.wav")) == "the cockpit's own"
                    && text (family.getChildFile ("Clash (RedPawAmp).wav")) == "the rover's own");
        report ("...numbered when even that name is taken",
                text (family.getChildFile ("Twice (RedPawAmp).wav")) == "yet another"
                    && text (family.getChildFile ("Twice (RedPawAmp) 2.wav")) == "the rover's twice");
        report ("...its folders kept",                        text (family.getChildFile ("Vendor Pack/Close/57.wav")) == "a pack's");
        report ("...and what is not an IR left behind",       ! family.getChildFile ("readme.txt").exists()
                                                                  && ! family.getChildFile (".hidden.wav").exists());
        report ("nothing of before moved or changed",         text (former.getChildFile ("Same.wav")) == "one IR"
                                                                  && text (former.getChildFile ("Clash.wav")) == "the rover's own"
                                                                  && text (former.getChildFile ("Vendor Pack/Close/57.wav")) == "a pack's"
                                                                  && text (former.getChildFile ("Twice.wav")) == "the rover's twice");

        const auto again = IrLibrary::gather (former, family, "RedPawAmp");
        report ("a second gathering copies nothing",          again.copied == 0 && again.failed == 0
                                                                  && ! family.getChildFile ("Clash (RedPawAmp) 2.wav").exists()
                                                                  && ! family.getChildFile ("Twice (RedPawAmp) 3.wav").exists());
        report ("no folder of before, nothing gathered",      IrLibrary::gather (work.getChildFile ("gather/none"), family, "RedPawAmp").copied == 0);
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

    // ---- Devices: one capture, one install — by its rig_id --------------------------------------
    {
        const auto src     = work.getChildFile ("dup-src");
        const auto into    = work.getChildFile ("dup-devices");
        const auto factory = work.getChildFile ("dup-factory");
        into.createDirectory();
        factory.createDirectory();

        const auto pack = [&] (const juce::File& dir, const juce::String& name, const juce::String& rigId)
        {
            const auto folder = dir.getChildFile (name + ".orbitrig");
            folder.createDirectory();
            folder.getChildFile ("rig.json").replaceWithText (
                "{ \"format\": \"orbitrig\", \"schema\": 4, \"rig_id\": \"" + rigId + "\", \"name\": \"" + name + "\" }");
            return folder;
        };

        // The Trash is the player's machine's; here a retired copy is simply deleted.
        const auto retire = [] (const juce::File& f) { return f.deleteRecursively(); };

        const auto first = DeviceLibrary::importDevice (pack (src, "TS", "ts-mini-86a1"), into, factory, retire);
        report ("a pack's rig_id is read",               DeviceLibrary::rigIdOf (first) == "ts-mini-86a1");

        const auto again = DeviceLibrary::importDevice (pack (src.getChildFile ("v2"), "TS", "ts-mini-86a1"),
                                                        into, factory, retire);
        report ("the same capture again: one install",   again == into.getChildFile ("TS.orbitrig")
                                                           && ! into.getChildFile ("TS 2.orbitrig").exists());

        const auto renamed = DeviceLibrary::importDevice (pack (src, "Tube Screamer Mini", "ts-mini-86a1"),
                                                          into, factory, retire);
        report ("...under another name: still one",      renamed == into.getChildFile ("Tube Screamer Mini.orbitrig")
                                                           && ! into.getChildFile ("TS.orbitrig").exists());

        report ("re-importing the installed file is no-op",
                DeviceLibrary::importDevice (renamed, into, factory, retire) == renamed && renamed.isDirectory());

        const auto other = DeviceLibrary::importDevice (pack (src.getChildFile ("ch2"), "Tube Screamer Mini", "ts-mini-ch2-1111"),
                                                        into, factory, retire);
        report ("another capture with the same name: both", other == into.getChildFile ("Tube Screamer Mini 2.orbitrig")
                                                           && renamed.isDirectory());

        pack (factory, "RAT", "rat-2-3ab8");
        juce::String refused;
        report ("a capture the build ships is refused",  DeviceLibrary::importDevice (pack (src, "Rodent", "rat-2-3ab8"),
                                                                                     into, factory, retire, &refused) == juce::File()
                                                           && ! into.getChildFile ("Rodent.orbitrig").exists());
        report ("...and says which pack it already is",  refused == "RAT", refused);

        report ("a lone model has no rig_id to merge by", DeviceLibrary::rigIdOf (work.getChildFile ("dev-src/clean.nam")).isEmpty());

        // The selectors' list: copies already on disk show once.
        const auto entry = [] (const juce::String& name, const juce::String& rigId, bool bundled, int ageSeconds, bool loose = false)
        {
            DeviceLibrary::Pack p;
            p.name     = name;
            p.rigId    = rigId;
            p.bundled  = bundled;
            p.loose    = loose;
            p.modified = juce::Time (2026, 8, 1, 12, 0) - juce::RelativeTime::seconds (ageSeconds);
            return p;
        };

        juce::Array<DeviceLibrary::Pack> list;
        list.add (entry ("TS",        "ts",  false, 100));
        list.add (entry ("TS 2",      "ts",  false, 10));    // the newest copy
        list.add (entry ("RAT",       "rat", false, 5));
        list.add (entry ("RAT",       "rat", true,  500));   // the build's
        list.add (entry ("Mystery",   "",    false, 1));
        list.add (entry ("Mystery 2", "",    false, 2));
        list.add (entry ("clean",     "ts",  false, 1, true));

        DeviceLibrary::keepOnePerCapture (list);

        const auto has = [&list] (const juce::String& name, bool bundled)
        {
            for (const auto& p : list)
                if (p.name == name && p.bundled == bundled)
                    return true;
            return false;
        };

        report ("copies on disk: one entry per capture",  list.size() == 5, juce::String (list.size()) + " entries");
        report ("...the newest of the player's copies",   has ("TS 2", false) && ! has ("TS", false));
        report ("...the build's over the player's",       has ("RAT", true) && ! has ("RAT", false));
        report ("...and nothing without an id is merged", has ("Mystery", false) && has ("Mystery 2", false)
                                                            && has ("clean", false));
    }

    // ---- Devices: one family, one device, its packs its channels ------------------------------
    {
        using Shelf = DeviceLibrary::Shelf;

        const auto names = [] (const std::vector<DeviceLibrary::Device>& devices)
        {
            juce::StringArray out;
            for (const auto& d : devices)
                out.add (d.name);
            return out.joinIntoString (", ");
        };

        const auto named = [] (const std::vector<DeviceLibrary::Device>& devices, const juce::String& name)
            -> const DeviceLibrary::Device*
        {
            for (const auto& d : devices)
                if (d.name == name)
                    return &d;
            return nullptr;
        };

        const auto channels = [] (const DeviceLibrary::Device* d)
        {
            return d != nullptr ? d->channelNames.joinIntoString (", ") : juce::String ("(no device)");
        };

        const auto inOrder = [] (const Shelf& shelf, int count)
        {
            const auto order = shelf.order();
            bool same = order.size() == count;
            for (int i = 0; same && i < count; ++i)
                same = order[i] == i;
            return same;
        };

        // The set as the build carries it, read in place: every manifest is the real one.
        const juce::File set (ORBITAMP_SET_DIR);
        const auto none = work.getChildFile ("devices-none");
        const auto factory = DeviceLibrary::scan (DeviceLibrary::ampSlot, true, set.getChildFile ("Devices"), none);
        const auto shelf = DeviceLibrary::shelfOf (factory);

        report ("the set's packs make five devices, by name",  names (shelf.factory) == "IR-X, V4 Copper, V4 Kraken, V4 Sheriff, X88-IR",
                names (shelf.factory));
        report ("...a device is its packs' common words",      named (shelf.factory, "V4 Kraken") != nullptr
                                                                  && named (shelf.factory, "IR-X") != nullptr);
        report ("X88-IR: one alias, so the stems name them",   channels (named (shelf.factory, "X88-IR")) == "Normal, Altered, Crunch, Overdrive",
                channels (named (shelf.factory, "X88-IR")));
        report ("IR-X: distinct aliases name the channels",    channels (named (shelf.factory, "IR-X")) == "Channel 1, Channel 2",
                channels (named (shelf.factory, "IR-X")));
        report ("V4 Copper: crunch before hi-gain",            channels (named (shelf.factory, "V4 Copper")) == "Ember Mantra, Ember Anthem",
                channels (named (shelf.factory, "V4 Copper")));
        report ("V4 Kraken: one tone, the file names decide",  channels (named (shelf.factory, "V4 Kraken")) == "Monster Roar, Monster Fang",
                channels (named (shelf.factory, "V4 Kraken")));
        report ("the list is in the menu's order",             inOrder (shelf, factory.size()));

        // The player's own folder: devices at its top, and folders inside it mirrored as they lie.
        const auto own = work.getChildFile ("devices-own");
        const auto lay = [&set] (const juce::File& into, const juce::String& from, const juce::String& as = {},
                                 const juce::String& slot = {}, const juce::String& family = {})
        {
            auto manifest = set.getChildFile ("Devices/" + from + ".orbitrig/rig.json").loadFileAsString();

            if (slot.isNotEmpty())   // a booster of make-believe: a set pack's manifest, made a pedal of its own
                manifest = manifest.replace ("\"slot\": \"preamp\"", "\"slot\": \"" + slot + "\"")
                                   .replace ("\"family_id\": \"victory-v4-sheriff\"", "\"family_id\": \"" + family + "\"")
                                   .replace ("\"rig_id\": \"", "\"rig_id\": \"" + family + "-");

            const auto folder = into.getChildFile ((as.isNotEmpty() ? as : from) + ".orbitrig");
            folder.createDirectory();
            folder.getChildFile ("rig.json").replaceWithText (manifest);
        };

        lay (own, "IR-X Ch2 PowerAmp");
        lay (own, "IR-X Ch1 PowerAmp");
        lay (own.getChildFile ("OLD/preamps"), "V4 Kraken Ch2");
        lay (own.getChildFile ("OLD/preamps"), "V4 Kraken Ch1");
        lay (own.getChildFile ("OLD/boosters"), "V4 Sheriff Ch1", "TS Test", "pedal", "ts-test");
        own.getChildFile ("OLD/empty").createDirectory();

        const auto mine = DeviceLibrary::scan (DeviceLibrary::ampSlot, true, none, own);
        const auto ownShelf = DeviceLibrary::shelfOf (mine);
        const auto* old = ownShelf.folders.size() == 1 ? &ownShelf.folders.front() : nullptr;

        const auto folderNames = [] (const std::vector<DeviceLibrary::Folder>& folders)
        {
            juce::StringArray out;
            for (const auto& f : folders)
                out.add (f.name);
            return out.joinIntoString (", ");
        };

        report ("the top of the player's folder is grouped too", ownShelf.own.size() == 1
                                                                    && channels (named (ownShelf.own, "IR-X")) == "Channel 1, Channel 2",
                names (ownShelf.own));
        report ("a folder in it is a submenu, as on disk",      old != nullptr && old->name == "OLD" && old->devices.empty()
                                                                    && folderNames (old->folders) == "boosters, preamps",
                old != nullptr ? old->name + ": " + folderNames (old->folders) : folderNames (ownShelf.folders));

        const auto* preamps = old != nullptr && old->folders.size() == 2 ? &old->folders[1] : nullptr;
        const auto* boosters = old != nullptr && old->folders.size() == 2 ? &old->folders[0] : nullptr;
        bool loose = preamps != nullptr && names (preamps->devices) == "V4 Kraken Ch1, V4 Kraken Ch2";
        if (preamps != nullptr)
            for (const auto& d : preamps->devices)
                loose = loose && d.channels.size() == 1;

        report ("...its packs one per line, by file name",     loose, preamps != nullptr ? names (preamps->devices) : juce::String());
        report ("...a booster is on the AMP's list",            boosters != nullptr && names (boosters->devices) == "TS Test",
                boosters != nullptr ? names (boosters->devices) : juce::String());
        report ("...and a folder with nothing in it is not",    old != nullptr && old->folders.size() == 2);
        report ("devices first, then the folders, in order",    inOrder (ownShelf, mine.size()) && mine.size() == 5
                                                                    && mine.getReference (0).folder.isEmpty()
                                                                    && mine.getReference (4).folder.joinIntoString ("/") == "OLD/preamps",
                juce::String (mine.size()) + " packs");

        // What the set does not happen to show: the family, not the name, makes a device; a family
        // whose files share no first word is called by its id; a pack with no family is alone; and
        // two channels of one tone go by their file names as a person reads numbers.
        const auto pack = [] (const juce::String& name, const juce::String& family, const juce::String& alias, int character)
        {
            DeviceLibrary::Pack p;
            p.name = name;
            p.familyId = family;
            p.alias = alias;
            p.character = character;
            p.bundled = true;
            return p;
        };

        juce::Array<DeviceLibrary::Pack> made;
        made.add (pack ("Foo A",      "foo-a",  "Foo",   0));
        made.add (pack ("Foo B",      "foo-b",  "Foo",   0));
        made.add (pack ("Alpha One",  "greek",  "Same",  1));
        made.add (pack ("Beta Two",   "greek",  "Same",  0));
        made.add (pack ("Loner Amp",  "",       "",      2));
        made.add (pack ("Amp Ch10",   "numbers", "",     1));
        made.add (pack ("Amp Ch2",    "numbers", "",     1));

        const auto madeShelf = DeviceLibrary::shelfOf (made);

        report ("one name, two families: two devices",          named (madeShelf.factory, "Foo A") != nullptr
                                                                    && named (madeShelf.factory, "Foo B") != nullptr,
                names (madeShelf.factory));
        report ("no common word: the family's id names it",    channels (named (madeShelf.factory, "greek")) == "Beta Two, Alpha One",
                channels (named (madeShelf.factory, "greek")));
        report ("no family: a device of its own",               named (madeShelf.factory, "Loner Amp") != nullptr
                                                                    && named (madeShelf.factory, "Loner Amp")->channels.size() == 1);
        report ("one tone: Ch2 before Ch10",                    channels (named (madeShelf.factory, "Amp")) == "Ch2, Ch10",
                channels (named (madeShelf.factory, "Amp")));
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
