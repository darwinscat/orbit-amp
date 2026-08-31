#pragma once

#include "Theme.h"

#include <felitronics/appkit/BrandHeader.h>
#include <felitronics/appkit/IconButton.h>
#include <felitronics/appkit/chrome/CompareCell.h>   // RegisterButton — the A/B/C/D button
#include <felitronics/appkit/chrome/PresetCell.h>

#include <memory>
#include <vector>

namespace orbitamp
{

class AmpProcessor;

/** The header, laid out as OrbitCab's:

        [cat + mark + OrbitAmp by Darwin's Cat]  undo redo ....  A B C D  [preset]  save saveAs trash

    Brand hugging its content on the left, undo/redo in the gap after it, and a right cluster of
    registers, the preset name, and the file actions. Every piece comes from felitronics-appkit —
    OrbitCab's own copies stay in OrbitCab; what is shared here is the arrangement, not the widgets.

    It is its own DragAndDropContainer so dragging one register onto another (copy A to C) never
    leaks out into the rest of the window. */
class Chrome final : public juce::Component,
                     public juce::DragAndDropContainer
{
public:
    explicit Chrome (AmpProcessor&);
    ~Chrome() override;

    /** The gear was pressed, at this screen position. What it offers — the Setup window and the
        window's own switches — is the editor's business; the header only announces, like every
        other widget here. */
    std::function<void (juce::Point<int>)> onGear;

    void resized() override;
    void paintOverChildren (juce::Graphics&) override;

    // OrbitCab's header proportions: a 50-tall strip whose small controls live in a 44-tall band
    // centred in it, so only the brand grows with the strip.
    static constexpr int designHeight  = 60;   // the brand — cat, mark, wordmark, byline — grows with it
    static constexpr int controlBand   = 44;

private:
    /** Pushes the engine's state onto the buttons. Called on every history change. */
    void refreshModel();

    void showPresetMenu();
    void showRegisterMenu (int index);
    void savePreset (bool forceNewName);

    AmpProcessor& amp;

    felitronics::appkit::chrome::ChromeTheme theme;

    felitronics::appkit::BrandHeader brand;
    std::unique_ptr<juce::Drawable>  mark;   // OrbitAmp's own — painted over the header's shared one
    felitronics::appkit::IconButton  undo   { felitronics::appkit::IconButton::Kind::undo };
    felitronics::appkit::IconButton  redo   { felitronics::appkit::IconButton::Kind::redo };
    felitronics::appkit::IconButton  save   { felitronics::appkit::IconButton::Kind::save };
    felitronics::appkit::IconButton  saveAs { felitronics::appkit::IconButton::Kind::saveAs };
    felitronics::appkit::IconButton  trash  { felitronics::appkit::IconButton::Kind::trash };
    felitronics::appkit::IconButton  gear   { felitronics::appkit::IconButton::Kind::settings };

    std::vector<std::unique_ptr<felitronics::appkit::chrome::RegisterButton>> registers;
    felitronics::appkit::chrome::PresetCell preset;

    juce::String presetName { "Default" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Chrome)
};

} // namespace orbitamp
