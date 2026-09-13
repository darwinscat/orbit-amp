// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "IrTreeView.h"

#include "../../device/IrLibrary.h"
#include "../Theme.h"
#include "MiniClose.h"
#include "MiniPlus.h"

#include <felitronics/appkit/TextPrompt.h>

namespace orbitamp
{

namespace
{
    int filesBeneath (const device::IrLibrary::Node& n)
    {
        if (! n.folder)
            return 1;

        int total = 0;
        for (const auto& c : n.children)
            total += filesBeneath (c);
        return total;
    }

    /** What a row being dragged inside the tree says it is — the prefix keeps it from being taken
        for anything else that is dragged in this window. */
    const juce::String dragPrefix = "orbitamp-ir:";
}

//==============================================================================
/** One node of the tree. The row itself is a component (below), so a folder's name can turn into
    an editor on a double-click — a paint routine cannot do that. */
class IrTreeView::Item final : public juce::TreeViewItem
{
public:
    Item (IrTreeView& v, device::IrLibrary::Node n) : view (v), node (std::move (n))
    {
        for (auto& c : node.children)
            addSubItem (new Item (view, c));   // the tree item owns its children
    }

    bool mightContainSubItems() override      { return node.folder; }
    juce::String getUniqueName() const override { return node.file.getFullPathName(); }
    int getItemHeight() const override        { return 24; }

    // ---- moving what is already in the library: drag a row onto a folder ----
    // The tree runs the drag, so the row's mouse goes to the tree as well as to the row. Nothing is
    // selected (there is nothing to do with a selection here), and a double-click is the name's —
    // it renames — rather than the tree's open-and-close.
    bool customComponentUsesTreeViewMouseHandler() const override { return true; }
    bool canBeSelected() const override                            { return false; }
    void itemDoubleClicked (const juce::MouseEvent&) override      {}

    juce::var getDragSourceDescription() override
    {
        // Not while the name is being typed: a drag there is selecting text, not moving the row.
        if (editing)
            return {};

        // Nor when the press was on the row's + or ✕ — a hand that wobbles while clicking one
        // meant the button. The tree hears those presses too (it listens to the whole row).
        const auto& mouse = juce::Desktop::getInstance().getMainMouseSource();
        if (dynamic_cast<juce::Button*> (juce::Desktop::getInstance().findComponentAt (
                mouse.getLastMouseDownPosition().roundToInt())) != nullptr)
            return {};

        return dragPrefix + node.file.getFullPathName();
    }

    // WHERE A ROW LANDS. A folder row takes the drop itself (see RowComponent) — JUCE's own insert
    // geometry reads an open folder's row as "between its siblings" and would hand the drop to the
    // folder's PARENT. What reaches these two is a drop between rows or below them: it goes into
    // the folder those rows sit in, the root for the top level.
    bool isInterestedInDragSource (const juce::DragAndDropTarget::SourceDetails& d) override
    {
        return node.folder && acceptsMove (node.file, sourceOf (d));
    }

    void itemDropped (const juce::DragAndDropTarget::SourceDetails& d, int) override
    {
        moveInto (d);
    }

    /** The default plus-minus square is a stranger on this face — a small chevron instead. */
    void paintOpenCloseButton (juce::Graphics& g, const juce::Rectangle<float>& area,
                               juce::Colour, bool over) override
    {
        juce::Path p;
        const auto r = area.reduced (area.getWidth() * 0.32f);

        if (isOpen())
        {
            p.startNewSubPath (r.getTopLeft());
            p.lineTo (r.getCentreX(), r.getBottom());
            p.lineTo (r.getTopRight());
        }
        else
        {
            p.startNewSubPath (r.getTopLeft());
            p.lineTo (r.getRight(), r.getCentreY());
            p.lineTo (r.getBottomLeft());
        }

        g.setColour (over ? theme::tx : theme::txDim);
        g.strokePath (p, juce::PathStrokeType (1.4f));
    }

    std::unique_ptr<juce::Component> createItemComponent() override
    {
        return std::make_unique<RowComponent> (*this);
    }

private:
    static juce::File sourceOf (const juce::DragAndDropTarget::SourceDetails& d)
    {
        const auto s = d.description.toString();
        return s.startsWith (dragPrefix) ? juce::File (s.substring (dragPrefix.length())) : juce::File();
    }

    /** Where it already is, into itself, or beneath itself — none of those is a move. */
    static bool acceptsMove (const juce::File& folder, const juce::File& source)
    {
        return source != juce::File() && source.getParentDirectory() != folder && source != folder
               && ! folder.isAChildOf (source);
    }

    void moveInto (const juce::DragAndDropTarget::SourceDetails& d)
    {
        if (device::IrLibrary::move (device::IrLibrary::directory(), sourceOf (d), node.file) != juce::File())
            view.changedLater();
    }

    bool editing = false;   // the name's editor is open — see getDragSourceDescription

    /** The row: an editable name for a folder, a plain one for a file, the folder's IR count, a
        folder's + and the ✕. The label commits a rename to disk; a name the disk refuses snaps back.

        A folder is where things go, and it says so in plain sight: its + adds IRs INTO it, files
        dropped ON it import into it, and its right-click names all of it — rename, add, a folder
        inside. The row listens on the label too, so a click is the row's wherever it lands. */
    struct RowComponent final : public juce::Component,
                                public juce::FileDragAndDropTarget,
                                public juce::DragAndDropTarget,
                                private juce::Label::Listener
    {
        explicit RowComponent (Item& i) : item (i)
        {
            name.setText (item.node.name(), juce::dontSendNotification);
            name.setFont (juce::FontOptions (12.5f));
            name.setColour (juce::Label::textColourId, item.node.folder ? theme::tx : theme::txDim);
            name.setColour (juce::Label::textWhenEditingColourId, theme::tx);
            name.setColour (juce::TextEditor::highlightColourId, theme::violet.withAlpha (0.4f));
            name.setBorderSize ({ 1, 2, 1, 2 });

            if (item.node.folder)
            {
                name.setEditable (false, true, false);   // double-click renames
                name.addListener (this);
                name.addMouseListener (this, false);     // the right-click is the row's, label or not
            }
            else
            {
                name.setInterceptsMouseClicks (false, false);   // clicks fall through to the tree row
            }

            addAndMakeVisible (name);

            x.colour  = juce::Colour (0xffe06a6a);
            x.onClick = [this]
            {
                if (device::IrLibrary::remove (device::IrLibrary::directory(), item.node.file))
                    item.view.changedLater();
            };
            addAndMakeVisible (x);

            if (item.node.folder)
            {
                plus.setTooltip ("Add IRs into this folder");
                plus.onClick = [&owner = item.view, folder = item.node.file] { owner.addClicked (folder); };
                addAndMakeVisible (plus);
            }
        }

        void resized() override
        {
            auto r = getLocalBounds();
            x.setBounds (r.removeFromRight (24).reduced (2, 4));

            if (item.node.folder)
            {
                plus.setBounds (r.removeFromRight (24).reduced (2, 4));
                r.removeFromRight (34);   // the count column
            }

            name.setBounds (r);
        }

        void paint (juce::Graphics& g) override
        {
            if (! item.node.folder)
                return;

            if (dragOver)   // this exact folder is about to receive the drop
            {
                g.setColour (theme::orange.withAlpha (0.16f));
                g.fillRoundedRectangle (getLocalBounds().toFloat(), 3.0f);
                g.setColour (theme::orange);
                g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 3.0f, 1.0f);
            }

            auto count = getLocalBounds();
            count.removeFromRight (48);   // the + and the ✕

            g.setColour (theme::txFaint);
            g.setFont (juce::FontOptions (9.5f));
            g.drawText (juce::String (filesBeneath (item.node)),
                        count.removeFromRight (34).reduced (0, 2),
                        juce::Justification::centredRight);
        }

        void labelTextChanged (juce::Label* l) override
        {
            if (device::IrLibrary::rename (device::IrLibrary::directory(), item.node.file, l->getText()))
                item.view.changedLater();
            else
                l->setText (item.node.name(), juce::dontSendNotification);
        }

        void editorShown (juce::Label*, juce::TextEditor&) override { item.editing = true; }
        void editorHidden (juce::Label*, juce::TextEditor&) override { item.editing = false; }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (! item.node.folder || ! e.mods.isPopupMenu())
                return;

            // The menu's actions reach the row through a SafePointer: the tree can rebuild — and free
            // this row — while the menu is up, when a drop or an import elsewhere lands.
            juce::PopupMenu menu;
            menu.addItem ("Rename", [safe = juce::Component::SafePointer<RowComponent> (this)]
            {
                if (safe != nullptr)
                    safe->name.showEditor();
            });
            // ...and the view through one of its own: the whole window can go while a menu is up.
            const juce::Component::SafePointer<IrTreeView> owner (&item.view);

            menu.addItem (juce::String::fromUTF8 ("Add IRs here\xe2\x80\xa6"), [owner, folder = item.node.file]
            {
                if (owner != nullptr)
                    owner->addClicked (folder);
            });
            menu.addItem (juce::String::fromUTF8 ("New folder inside\xe2\x80\xa6"), [owner, parent = item.node.file]
            {
                if (owner != nullptr)
                    owner->newFolderPrompt (parent);
            });
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
        }

        // ---- a folder row takes drops for itself; file rows leave them to the tree (→ root) ----
        bool isInterestedInFileDrag (const juce::StringArray& files) override
        {
            if (! item.node.folder)
                return false;

            for (const auto& f : files)
                if (device::IrLibrary::looksLikeImport (juce::File (f)))
                    return true;

            return false;
        }

        void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true;  repaint(); }
        void fileDragExit  (const juce::StringArray&)           override { dragOver = false; repaint(); }

        void filesDropped (const juce::StringArray& files, int, int) override
        {
            dragOver = false;
            item.view.importPaths (files, item.node.file);
        }

        // ---- ...and rows dragged within the tree, the same way: ON the folder is INTO it ----
        bool isInterestedInDragSource (const SourceDetails& d) override
        {
            return item.node.folder && acceptsMove (item.node.file, sourceOf (d));
        }

        void itemDragEnter (const SourceDetails&) override { dragOver = true;  repaint(); }
        void itemDragExit  (const SourceDetails&) override { dragOver = false; repaint(); }

        void itemDropped (const SourceDetails& d) override
        {
            dragOver = false;
            item.moveInto (d);
        }

        Item& item;
        juce::Label name;
        MiniClose x;
        MiniPlus plus;
        bool dragOver = false;
    };

    IrTreeView& view;
    device::IrLibrary::Node node;
};

//==============================================================================
IrTreeView::IrTreeView()
{
    tree.setRootItemVisible (false);
    tree.setIndentSize (14);
    tree.setDefaultOpenness (false);
    tree.setColour (juce::TreeView::dragAndDropIndicatorColourId, theme::orange);   // where a moved row lands
    addAndMakeVisible (tree);

    for (auto* b : { &addButton, &folderButton, &revealButton })
    {
        b->theme.text    = theme::tx;
        b->theme.textDim = theme::txDim;
        addAndMakeVisible (*b);
    }

    addButton.onClick    = [this] { addClicked (device::IrLibrary::directory()); };
    folderButton.onClick = [this] { newFolderPrompt (device::IrLibrary::directory()); };
    revealButton.onClick = [] { device::IrLibrary::directory().revealToUser(); };
}

IrTreeView::~IrTreeView()
{
    tree.setRootItem (nullptr);
}

void IrTreeView::rebuild()
{
    // Which branches were open, by path — a rebuild is a refresh, not a reset.
    const auto openness = tree.getOpennessState (true);

    tree.setRootItem (nullptr);
    rootItem = std::make_unique<Item> (*this, device::IrLibrary::scan());
    tree.setRootItem (rootItem.get());
    rootItem->setOpen (true);

    if (openness != nullptr)
        tree.restoreOpennessState (*openness, true);

    anythingYet = rootItem->getNumSubItems() > 0;
    repaint();
}

void IrTreeView::resized()
{
    auto r = getLocalBounds();

    auto toolbar = r.removeFromBottom (toolbarH);
    addButton.setBounds (toolbar.removeFromLeft (56));
    toolbar.removeFromLeft (6);
    folderButton.setBounds (toolbar.removeFromLeft (96));
    toolbar.removeFromLeft (6);
    revealButton.setBounds (toolbar.removeFromLeft (92));

    r.removeFromBottom (6);
    tree.setBounds (r.reduced (4));

    rebuild();
}

void IrTreeView::paint (juce::Graphics& g)
{
    const auto well = tree.getBounds().expanded (4).toFloat();
    g.setColour (theme::bezel);
    g.fillRoundedRectangle (well, theme::radiusSm);
    g.setColour (dragOver ? theme::orange : theme::hair2);
    g.drawRoundedRectangle (well.reduced (0.5f), theme::radiusSm, dragOver ? 1.5f : 1.0f);

    if (! anythingYet)
    {
        g.setColour (theme::txFaint);
        g.setFont (juce::FontOptions (11.5f));
        g.drawText (juce::String::fromUTF8 ("No IRs yet \xe2\x80\x94 Add\xe2\x80\xa6 "
                                            "or drop wavs, folders, or zips here."),
                    tree.getBounds(), juce::Justification::centred);
    }
}

//==============================================================================
bool IrTreeView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (device::IrLibrary::looksLikeImport (juce::File (f)))
            return true;

    return false;
}

void IrTreeView::fileDragEnter (const juce::StringArray&, int, int) { dragOver = true;  repaint(); }
void IrTreeView::fileDragExit  (const juce::StringArray&)           { dragOver = false; repaint(); }

void IrTreeView::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    importPaths (files, device::IrLibrary::directory());
    repaint();
}

//==============================================================================
void IrTreeView::addClicked (const juce::File& into)
{
    // Same lifetime contract as everywhere: the chooser is a member, so its destructor unhooks
    // the callback before this view can die.
    chooser = std::make_unique<juce::FileChooser> (
        juce::String::fromUTF8 ("Add IRs \xe2\x80\x94 files, folders, or zips"),
        juce::File(), "*.wav;*.aif;*.aiff;*.zip");

    chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectDirectories
                              | juce::FileBrowserComponent::canSelectMultipleItems,
        [this, into] (const juce::FileChooser& fc)
        {
            juce::StringArray paths;
            for (const auto& f : fc.getResults())
                paths.add (f.getFullPathName());
            importPaths (paths, into);
        });
}

void IrTreeView::importPaths (const juce::StringArray& paths, const juce::File& into)
{
    int landed = 0;

    for (const auto& p : paths)
        landed += device::IrLibrary::importPath (juce::File (p), into);

    if (landed > 0)
        changedLater();
}

void IrTreeView::newFolderPrompt (const juce::File& parent)
{
    // The prompt is modal and this view is a guest of a closable window — guard the return.
    juce::Component::SafePointer<IrTreeView> self (this);

    felitronics::appkit::textPrompt ("New folder", {}, [self, parent] (juce::String name)
    {
        if (self != nullptr
            && device::IrLibrary::createFolder (device::IrLibrary::directory(), parent, name) != juce::File())
            self->changedLater();
    });
}

void IrTreeView::changedLater()
{
    juce::Component::SafePointer<IrTreeView> self (this);
    juce::MessageManager::callAsync ([self]
    {
        if (self != nullptr)
            self->rebuild();
    });
}

} // namespace orbitamp
