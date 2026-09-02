// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "Theme.h"
#include "library/MiniClose.h"

#include <felitronics/appkit/BrandHeader.h>

#include <BinaryData.h>

namespace orbitamp
{

/** ABOUT — what this is, whose it is, where it lives, and under what terms the names of other
    people's equipment appear on its faces.

    ITS OWN WINDOW, OPENED FROM THE GEAR. It was a tab inside SETUP for one build, and two levels
    down is not a place a notice can live: nobody opens a library manager to read a legal line, and
    SETUP is not even really setup — it is the pack manager, and it will be reworked. A door off the
    gear is one click from anywhere.

    THE NOTICE IS THE POINT OF THE PAGE. Every captured device on this face is named by its real
    maker and model — «Electro-Harmonix», «Big Muff Pi» — because that is the honest way to say
    which physical box was recorded, and because a player looking for a Big Muff should find one
    rather than a riddle. Naming somebody else's mark is allowed exactly when it identifies THEIR
    goods and cannot be read as a claim about ours, so this page says so in as many words, in the
    one place a player is certain to be able to find it. It is not decoration and it is not a
    formality: it is the sentence that makes the names on the tiles a description instead of a badge.

    English only, and one canonical wording — the same string the repository's README carries. A
    notice that exists in three translations has three slightly different meanings, and the one that
    matters is whichever a reader saw last.

    An overlay rather than a desktop window, for the reason SETUP is one: a plugin editor is a
    guest, and hosts reparent, hide and destroy it freely. */
class AboutPanel final : public juce::Component
{
public:
    AboutPanel()
        : brand (BinaryData::catlogo_svg, (size_t) BinaryData::catlogo_svgSize,
                 BinaryData::MichromaRegular_ttf, (size_t) BinaryData::MichromaRegular_ttfSize,
                 "OrbitAmp", home)
    {
        setWantsKeyboardFocus (true);

        // The cat and the orbit at a size worth looking at, rather than the toolbar's thumbnail —
        // this is the page where the product is allowed to say its own name out loud. Clicking the
        // run opens the site: the header already owns that behaviour, and a second link beside it
        // would be two doors to one room.
        brand.wordmarkScale = 0.40f;
        brand.bylineScale   = 0.22f;
        addAndMakeVisible (brand);

        closeButton.onClick = [this] { setVisible (false); };
        addAndMakeVisible (closeButton);
    }

    void open()
    {
        setVisible (true);
        toFront (false);
        grabKeyboardFocus();
    }

    /** The wording, in one place, so the page, the README, the website and a store listing can
        never drift apart. */
    static juce::String notice()
    {
        return "The names of manufacturers and products are used only for descriptive "
               "identification of the physical equipment from which the included models were "
               "captured.\n\n"
               "No affiliation, sponsorship, endorsement, or licensing by the respective "
               "manufacturers is implied.\n\n"
               "All trademarks are property of their respective owners.";
    }

    void resized() override
    {
        panel = getLocalBounds().withSizeKeepingCentre (panelW, panelH);

        auto r = panel.reduced (26, 22);
        closeButton.setBounds (r.removeFromTop (24).removeFromRight (24).reduced (1));

        brand.setBounds (panel.getX() + 26, panel.getY() + 26, panelW - 52, brandH);
        brand.clickRight = juce::roundToInt (brand.contentRight());
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::ground.withAlpha (0.78f));

        const auto p = panel.toFloat();
        g.setColour (theme::panel);
        g.fillRoundedRectangle (p, theme::radiusLg);
        g.setColour (theme::hair2);
        g.drawRoundedRectangle (p.reduced (0.75f), theme::radiusLg, 1.5f);

        auto r = panel.reduced (26, 22).toFloat();
        r.removeFromTop ((float) brandH + 16.0f);   // the header is a child and paints itself

        g.setColour (theme::txDim);
        theme::drawTracked (g, juce::String ("VERSION ") + ORBITAMP_VERSION
                                + juce::String::fromUTF8 ("  \xc2\xb7  AGPL-3.0-OR-LATER"),
                            r.removeFromTop (18.0f), theme::displayFont (11.0f), 0.08f,
                            juce::Justification::centredLeft);

        // The address in plain sight as well as under the cat: a link you can read is a link you
        // can type on another machine, and this one is the page every pack is documented on.
        g.setColour (theme::violet);
        theme::drawTracked (g, "DARWINSCAT.COM/ORBITAMP", r.removeFromTop (18.0f),
                            theme::displayFont (11.0f), 0.08f, juce::Justification::centredLeft);

        r.removeFromTop (20.0f);
        g.setColour (theme::hair2);
        g.fillRect (r.removeFromTop (1.0f));
        r.removeFromTop (18.0f);

        // A PARAGRAPH, so a paragraph face. The display font is uppercase and widely tracked — it
        // makes a label read as an instrument and a sentence read as a ransom note.
        g.setColour (theme::txDim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText (notice(), r.toNearestInt(), juce::Justification::topLeft, 12);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! panel.contains (e.getPosition()))
            setVisible (false);
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::escapeKey)
        {
            setVisible (false);
            return true;
        }

        return false;
    }

private:
    static constexpr int panelW = 560;
    static constexpr int panelH = 340;
    static constexpr int brandH = 64;   // the toolbar wears it at ~40; here it is the page's face

    static constexpr const char* home =
        "https://darwinscat.com/orbitamp?utm_source=orbitamp&utm_medium=plugin&utm_content=about";

    felitronics::appkit::BrandHeader brand;
    MiniClose closeButton;
    juce::Rectangle<int> panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AboutPanel)
};

} // namespace orbitamp
