// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "Theme.h"
#include "TwoLineBrand.h"

#include <felitronics/appkit/IconButton.h>
#include <felitronics/appkit/chrome/CompareCell.h>   // RegisterButton — the A/B/C/D button
#include <felitronics/appkit/chrome/PresetCell.h>

#include <memory>
#include <vector>

namespace orbitamp
{

class AmpProcessor;

/** The header, in two lines beside the brand's two marks:

        [cat] [mark]  OrbitAmp by Darwin's Cat  undo redo  A B C D  [ preset name ]  ⛶ ⚙
                      [ the chain strip — the editor's, standing on the second line ]

    The cat and the orbit mark stand both lines' height; the words and every control are the first
    line; the second line beside the marks is left to the editor, which stands the chain strip there
    (`secondLineX`). The widgets come from felitronics-appkit; the two-line brand is this repo's own
    (TwoLineBrand.h) until it settles.

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

    /** FULL SCREEN was pressed. What that means — the standalone's window growing, a hosted
        editor filling the display — is the editor's business. */
    std::function<void()> onFullScreen;

    void resized() override;

    // Two lines: the first the words and the controls, the second the chain strip's own height.
    static constexpr int firstLineH   = 34;
    static constexpr int designHeight = firstLineH + 34;   // 34: LayoutStrip::designHeight
    static constexpr int controlBand  = firstLineH;

    /** Where the second line starts: right of the brand's two marks. */
    int secondLineX() const { return juce::roundToInt (brand.marksRight()); }

    /** The first line's last two buttons, in this header's own x — the second line ends under them:
        the gear's button, and the corners FULL SCREEN draws (narrower than its button). */
    juce::Rectangle<int> gearBounds() const { return gear.getBounds(); }
    juce::Rectangle<int> fullScreenGlyph() const
    {
        return fullScreen.getBounds().toFloat()
                   .withSizeKeepingCentre (FullScreenIcon::glyphW, FullScreenIcon::glyphH).toNearestInt();
    }

private:
    /** Pushes the engine's state onto the buttons. Called on every history change. */
    void refreshModel();

    void showPresetMenu();
    void showRegisterMenu (int index);
    void savePreset (bool forceNewName);

    AmpProcessor& amp;

    felitronics::appkit::chrome::ChromeTheme theme;

    TwoLineBrand brand;
    felitronics::appkit::IconButton  undo   { felitronics::appkit::IconButton::Kind::undo };
    felitronics::appkit::IconButton  redo   { felitronics::appkit::IconButton::Kind::redo };
    felitronics::appkit::IconButton  gear   { felitronics::appkit::IconButton::Kind::settings };

    /** The full-screen button's face: the four corners of a frame, reaching out. Drawn here —
        this product's icon until a sibling wants it. */
    struct FullScreenIcon final : public juce::Button
    {
        FullScreenIcon() : juce::Button ("fullscreen")
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }

        juce::Colour colour;

        static constexpr float glyphW = 15.0f, glyphH = 13.0f;   // the four corners' box, centred

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            const auto r = getLocalBounds().toFloat().withSizeKeepingCentre (glyphW, glyphH);
            g.setColour (colour.withAlpha (down ? 1.0f : over ? 0.95f : 0.7f));

            const float arm = 4.5f, t = 1.4f;
            const auto corner = [&] (float x, float y, float dx, float dy)
            {
                juce::Path p;
                p.startNewSubPath (x + dx * arm, y);
                p.lineTo (x, y);
                p.lineTo (x, y + dy * arm);
                g.strokePath (p, juce::PathStrokeType (t, juce::PathStrokeType::mitered,
                                                       juce::PathStrokeType::rounded));
            };

            corner (r.getX(),     r.getY(),      1.0f,  1.0f);
            corner (r.getRight(), r.getY(),     -1.0f,  1.0f);
            corner (r.getX(),     r.getBottom(), 1.0f, -1.0f);
            corner (r.getRight(), r.getBottom(),-1.0f, -1.0f);
        }
    };

    FullScreenIcon fullScreen;

    std::vector<std::unique_ptr<felitronics::appkit::chrome::RegisterButton>> registers;
    felitronics::appkit::chrome::PresetCell preset;

    juce::String presetName { "Default" };

    friend class WebFaceplate;   // the site's kit reads where the parts stand (WebFaceplate.h)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Chrome)
};

} // namespace orbitamp
