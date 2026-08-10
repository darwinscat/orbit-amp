#include "DeviceListView.h"

#include "../Theme.h"
#include "MiniClose.h"

namespace orbitamp
{

//==============================================================================
/** One device: its character as the dot, its public name, what kind of thing it is on disk, and
    the slot it belongs in front of. Every row is the user's, so every row removes. */
struct DeviceListView::Row final : public juce::Component
{
    Row (device::DeviceLibrary::Pack p, std::function<void()> onRemove) : pack (std::move (p))
    {
        x.colour  = juce::Colour (0xffe06a6a);   // destructive, and says so before the click
        x.onClick = std::move (onRemove);
        addAndMakeVisible (x);
    }

    void resized() override
    {
        x.setBounds (getLocalBounds().removeFromRight (24).reduced (2, 4));
    }

    void paint (juce::Graphics& g) override
    {
        if (hover)
        {
            g.setColour (juce::Colour (0x10ffffff));
            g.fillRect (getLocalBounds());
        }

        auto body = getLocalBounds().reduced (8, 0);
        body.removeFromRight (24);   // the ✕ column

        // Right-to-left: what it is on disk, and — for a file that named no slot — the fact that
        // it shows on every device tab, so meeting it again elsewhere is expected.
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        g.setColour (theme::txFaint);
        g.drawText (pack.loose ? "NAM" : "PACK", body.removeFromRight (56), juce::Justification::centredRight);
        if (pack.slot == device::DeviceLibrary::Slot::any)
            g.drawText ("ANY", body.removeFromRight (44), juce::Justification::centredRight);

        auto dot = body.removeFromLeft (14).toFloat();
        g.setColour (theme::characterColour (pack.character));
        g.fillEllipse (dot.withSizeKeepingCentre (6.0f, 6.0f));

        g.setColour (theme::tx);
        g.setFont (juce::FontOptions (12.5f));
        g.drawText (pack.displayName(), body.withTrimmedLeft (4), juce::Justification::centredLeft, true);
    }

    void mouseEnter (const juce::MouseEvent&) override { hover = true;  repaint(); }
    void mouseExit  (const juce::MouseEvent&) override { hover = false; repaint(); }

    device::DeviceLibrary::Pack pack;
    bool hover = false;
    MiniClose x;
};

//==============================================================================
DeviceListView::DeviceListView (device::DeviceLibrary::Slot wanted) : slot (wanted)
{
    viewport.setScrollBarsShown (true, false);
    viewport.setViewedComponent (&content, false);
    addAndMakeVisible (viewport);

    for (auto* b : { &addButton, &revealButton })
    {
        b->theme.text    = theme::tx;
        b->theme.textDim = theme::txDim;
        addAndMakeVisible (*b);
    }

    addButton.onClick    = [this] { addClicked(); };
    revealButton.onClick = [] { device::DeviceLibrary::directory().revealToUser(); };
}

DeviceListView::~DeviceListView() = default;

void DeviceListView::rebuild()
{
    rows.clear();
    content.removeAllChildren();

    // Only the user's layer — the factory devices are the build's, not this panel's to edit —
    // and only this tab's slot, the same cut the block's own selector makes.
    auto packs = device::DeviceLibrary::scan (slot);
    packs.removeIf ([] (const device::DeviceLibrary::Pack& p) { return p.bundled; });

    for (int i = 0; i < packs.size(); ++i)
    {
        auto row = std::make_unique<Row> (packs[i], [this, i] { removePack (i); });
        content.addAndMakeVisible (*row);
        rows.push_back (std::move (row));
    }

    // Lay the rows out against the viewport as it is NOW — rebuild() runs on open and after
    // imports, when resized() will not be coming to do it.
    const int contentH = (int) rows.size() * rowH;
    const bool bar     = contentH > viewport.getHeight();
    const int w        = juce::jmax (0, viewport.getWidth() - (bar ? viewport.getScrollBarThickness() : 0));

    content.setSize (w, contentH);
    for (int i = 0; i < (int) rows.size(); ++i)
        rows[(size_t) i]->setBounds (0, i * rowH, w, rowH);

    repaint();
}

void DeviceListView::resized()
{
    auto r = getLocalBounds();

    auto toolbar = r.removeFromBottom (toolbarH);
    addButton.setBounds (toolbar.removeFromLeft (56));
    toolbar.removeFromLeft (6);
    revealButton.setBounds (toolbar.removeFromLeft (92));

    r.removeFromBottom (6);
    viewport.setBounds (r);

    rebuild();
}

void DeviceListView::paint (juce::Graphics& g)
{
    // The list well — framed so an empty library still reads as somewhere to drop things.
    const auto well = viewport.getBounds().toFloat();
    g.setColour (theme::bezel);
    g.fillRoundedRectangle (well, theme::radiusSm);
    g.setColour (dragOver ? theme::orange : theme::hair2);
    g.drawRoundedRectangle (well.reduced (0.5f), theme::radiusSm, dragOver ? 1.5f : 1.0f);

    if (rows.empty())
    {
        g.setColour (theme::txFaint);
        g.setFont (juce::FontOptions (11.5f));
        g.drawText (juce::String::fromUTF8 ("No devices yet \xe2\x80\x94 Add\xe2\x80\xa6 "
                                            "or drop .nam / .namz / .orbitrig packs here."),
                    viewport.getBounds(), juce::Justification::centred);
    }
}

//==============================================================================
bool DeviceListView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (device::DeviceLibrary::looksLikeDevice (juce::File (f)))
            return true;

    return false;
}

void DeviceListView::fileDragEnter (const juce::StringArray&, int, int) { dragOver = true;  repaint(); }
void DeviceListView::fileDragExit  (const juce::StringArray&)           { dragOver = false; repaint(); }

void DeviceListView::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    importPaths (files);
    repaint();
}

//==============================================================================
void DeviceListView::addClicked()
{
    // The chooser's lifetime bounds the callback's: it is a member, and its destructor clears the
    // pending callback — the same contract the sibling products rely on.
    chooser = std::make_unique<juce::FileChooser> (
        juce::String::fromUTF8 ("Add devices \xe2\x80\x94 models, packs, or pack folders"),
        juce::File(), "*.nam;*.namz;*.zip");

    chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectDirectories
                              | juce::FileBrowserComponent::canSelectMultipleItems,
        [this] (const juce::FileChooser& fc)
        {
            juce::StringArray paths;
            for (const auto& f : fc.getResults())
                paths.add (f.getFullPathName());
            importPaths (paths);
        });
}

void DeviceListView::importPaths (const juce::StringArray& paths)
{
    bool any = false;

    for (const auto& p : paths)
        if (device::DeviceLibrary::importDevice (juce::File (p)) != juce::File())
            any = true;

    if (any)
        changedLater();
}

void DeviceListView::removePack (int index)
{
    if (juce::isPositiveAndBelow (index, (int) rows.size())
        && device::DeviceLibrary::removeDevice (rows[(size_t) index]->pack))
        changedLater();
}

void DeviceListView::changedLater()
{
    juce::Component::SafePointer<DeviceListView> self (this);
    juce::MessageManager::callAsync ([self]
    {
        if (self == nullptr)
            return;

        if (self->onChanged)
            self->onChanged();

        self->rebuild();
    });
}

} // namespace orbitamp
