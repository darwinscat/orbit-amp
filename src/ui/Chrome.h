#pragma once

#include "Theme.h"

#include <felitronics/appkit/chrome/BrandBlister.h>
#include <felitronics/appkit/chrome/ChromeBar.h>
#include <felitronics/appkit/chrome/ChromeUnderline.h>
#include <felitronics/appkit/chrome/CompareCell.h>
#include <felitronics/appkit/chrome/PresetCell.h>

namespace orbitamp
{

class AmpProcessor;

/** The toolbar: brand badge · undo/redo · A/B/C/D · preset name.

    Every piece of it comes from felitronics-appkit — this class is wiring, not widgets. What is
    local is the theme (seeded from the device palette, never from the family brand values, so the
    active-register frame matches this product's face) and the gestures' meaning. */
class Chrome final : public juce::Component
{
public:
    explicit Chrome (AmpProcessor&);
    ~Chrome() override;

    void resized() override;

    // The blister overhangs the flat bar, so the strip is as tall as the badge.
    static constexpr int designHeight = 46;

private:
    /** Pushes the engine's state into the compare cell. Called on every history change. */
    void refreshModel();

    void showPresetMenu();
    void showRegisterMenu (int index);
    void savePresetAs();

    /** The orbit mark, in the badge. Drawn by appkit's brand kit — the mark is the family's. */
    struct OrbitMark final : felitronics::appkit::chrome::BlisterMark
    {
        int preferredContentWidth (int blisterHeight) const override { return blisterHeight; }
        void paint (juce::Graphics&) override;
    };

    AmpProcessor& amp;

    felitronics::appkit::chrome::ChromeMetrics metrics {};
    felitronics::appkit::chrome::ChromeTheme   theme;

    OrbitMark                                   mark;
    felitronics::appkit::chrome::BrandBlister   blister;
    felitronics::appkit::chrome::CompareCell    compare;
    felitronics::appkit::chrome::PresetCell     preset;
    felitronics::appkit::chrome::ChromeUnderline underline;
    felitronics::appkit::chrome::ChromeBar      bar;

    juce::String presetName { "Default" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Chrome)
};

} // namespace orbitamp
