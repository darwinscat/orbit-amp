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

    The shelf is shaped here too, not in Finder: `New folder…` makes one at the top, a folder's
    right-click makes one inside it, and dropping files ONTO a folder row imports into that
    folder — a drop on the tree at large still lands at the root.

    Dumb view over IrLibrary. Nothing plays from here yet — the cabinet's DSP meets these files in
    its own step; this is the shelf, kept orderly. */
class IrTreeView final : public juce::Component,
                         public juce::FileDragAndDropTarget
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

    void addClicked();
    void importPaths (const juce::StringArray&, const juce::File& into);

    /** Prompts for a name and makes the folder under `parent`. */
    void newFolderPrompt (const juce::File& parent);

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
