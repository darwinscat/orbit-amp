#pragma once

#include "../../device/DeviceLibrary.h"

#include <felitronics/appkit/chrome/FlatButtons.h>

#include <memory>
#include <vector>

namespace orbitamp
{

/** One Setup tab of USER devices: the packs and lone files whose metadata puts them in front of
    this view's slot. A file that names no slot shows on every device tab — same rule as the
    selectors, where `any` plays everywhere — and says so with a tag, so seeing it thrice reads
    as a fact rather than a bug.

    The bundled factory devices are deliberately not here: this panel edits the layer on TOP of
    factory, and a row is only shown if every action on it is real — a factory row would be a
    Remove that has to refuse. The selectors still show factory and user together; curating and
    choosing are different lists.

    Dumb view over DeviceLibrary: it scans, imports, and removes, and announces every change
    through `onChanged`; which block reloads what is the editor's business. The library is ONE
    folder whatever tab imports into it — the file's own metadata decides where it then shows. */
class DeviceListView final : public juce::Component,
                             public juce::FileDragAndDropTarget
{
public:
    explicit DeviceListView (device::DeviceLibrary::Slot wanted);
    ~DeviceListView() override;

    /** Re-reads the library and re-renders. Called on open and after every change. */
    void rebuild();

    /** The library moved under the blocks — imported or removed. */
    std::function<void()> onChanged;

    void resized() override;
    void paint (juce::Graphics&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

    static constexpr int toolbarH = 26;

private:
    struct Row;

    void addClicked();
    void importPaths (const juce::StringArray&);
    void removePack (int index);

    /** After the current event returns — a Remove click must not free the very row (and the
        std::function) still on the call stack. */
    void changedLater();

    const device::DeviceLibrary::Slot slot;

    juce::Viewport  viewport;
    juce::Component content;
    std::vector<std::unique_ptr<Row>> rows;

    felitronics::appkit::chrome::FlatItem addButton    { juce::String::fromUTF8 ("Add\xe2\x80\xa6") },
                                          revealButton { "Reveal folder" };

    std::unique_ptr<juce::FileChooser> chooser;
    bool dragOver = false;

    static constexpr int rowH = 26;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeviceListView)
};

} // namespace orbitamp
