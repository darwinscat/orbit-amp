// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <felitronics/appkit/chrome/FlatButtons.h>

#include <memory>

namespace orbitamp
{

/** The IR tab of the Setup window: the IR folder as a tree, because that is what an IR collection
    IS — vendors ship folders (or the zip of one), and the folder names are the only taxonomy the
    files have. So the tree shows disk truth, folders rename in place (double-click), and an
    imported zip or folder lands as a folder node.

    The shelf is shaped here too, not in Finder: `New folder…` makes one at the top; a folder row's +
    adds IRs into it, and its right-click renames it, adds into it or makes a folder inside it;
    dropping files ONTO a folder row imports into that folder — a drop on the tree at large still
    lands at the root. What is already on the shelf moves as a SELECTION (click, ⌘-click, ⇧-click):
    dragged onto a folder, or dropped below the rows for the top level, or sent by a row's
    right-click, Move to — the top level or any folder.

    Dumb view over IrLibrary. Nothing plays from here — the cabinet picks from this shelf in its own
    menu and takes the file's bytes with it, so nothing done here can silence a preset; this is the
    shelf, kept orderly. */
class IrTreeView final : public juce::Component,
                         public juce::FileDragAndDropTarget,
                         public juce::DragAndDropContainer   // the tree's own row drags need a container
{
public:
    IrTreeView();
    ~IrTreeView() override;

    /** Re-reads the folder and re-renders, keeping which branches were open. */
    void rebuild();

    void resized() override;
    void paint (juce::Graphics&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

    static constexpr int toolbarH = 26;

private:
    class Item;
    friend class Item;

    /** Asks for files, folders or zips and imports them into `into`. */
    void addClicked (const juce::File& into);
    void importPaths (const juce::StringArray&, const juce::File& into);

    /** Prompts for a name and makes the folder under `parent`. */
    void newFolderPrompt (const juce::File& parent);

    /** The rows selected, as files — what a drag or a Move to carries. */
    juce::Array<juce::File> selectedFiles() const;

    /** Every folder in the library, depth first — Move to's destinations. */
    juce::Array<juce::File> allFolders() const;

    /** Moves the group into `into` as IrLibrary::planMove plans it, and rebuilds once. */
    void moveFiles (const juce::Array<juce::File>& sources, const juce::File& into);

    /** Rebuild AFTER the current event returns — a rename or remove reaches here from a component
        that the rebuild would free while it is still on the call stack. */
    void changedLater();

    juce::TreeView tree;
    std::unique_ptr<Item> rootItem;

    felitronics::appkit::chrome::FlatItem addButton    { juce::String::fromUTF8 ("Add\xe2\x80\xa6") },
                                          folderButton { juce::String::fromUTF8 ("New folder\xe2\x80\xa6") },
                                          revealButton { "Reveal folder" };

    std::unique_ptr<juce::FileChooser> chooser;
    bool dragOver = false;
    bool anythingYet = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IrTreeView)
};

} // namespace orbitamp
