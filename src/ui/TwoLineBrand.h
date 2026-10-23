// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <felitronics/appkit/Brand.h>
#include <felitronics/appkit/BrandAssets.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstring>

namespace orbitamp
{

/** The brand for a header of TWO lines: the cat and the orbit mark stand the header's whole height,
    the wordmark and its byline on the FIRST line beside them, and the second line beside them is
    the owner's — the chain strip goes there.

    appkit's BrandHeader draws all four on one line, sized off its own height, which is right for a
    header of one line and has nowhere to put a second. This is the same family kit — the cat, the
    mark and the face appkit carries — laid out for two. Built here first; it moves to appkit if the
    two-line header stays.

    The link is what it was: the marks and the words open the product's page, with the same violet
    halo under them. The rest of the component is click-through, so the line under the words belongs
    to whatever the owner stands there. */
class TwoLineBrand final : public juce::Component
{
public:
    TwoLineBrand (juce::String productName, juce::String homeUrl)
        : logo (juce::Drawable::createFromImageData (felitronics::appkit::brand::assets::catSvg,
                                                     std::strlen (felitronics::appkit::brand::assets::catSvg))),
          wordmarkFace (felitronics::appkit::brand::wordmarkTypeface()),
          product (std::move (productName)), home (std::move (homeUrl)) {}

    int   firstLineH = 34;      // the owner's: how tall the line the words stand on is
    float wordmarkH  = 26.0f;
    float bylineH    = 16.0f;

    /** The cat's square, at the left, two units in — the site's kit reads it from here. */
    juce::Rectangle<float> catArea() const
    {
        const float h = (float) getHeight();
        return { 4.0f, 2.0f, h - 4.0f, h - 4.0f };
    }

    /** Where the two marks end: the words, and the owner's second line, start here. */
    float marksRight() const
    {
        const float h = (float) getHeight();
        return h + 6.0f + markD() + 14.0f;
    }

    /** Where the words end on the first line — the far end of the link. */
    float contentRight() const
    {
        return marksRight() + textWidth (wordmarkFont(), product) + 14.0f + textWidth (bylineFont(), byline) + 8.0f;
    }

    bool hitTest (int x, int y) override { return linkArea ({ (float) x, (float) y }); }

    void mouseEnter (const juce::MouseEvent& e) override { setHover (linkArea (e.position)); }
    void mouseMove  (const juce::MouseEvent& e) override { setHover (linkArea (e.position)); }
    void mouseExit  (const juce::MouseEvent&)   override { setHover (false); }
    void mouseUp    (const juce::MouseEvent& e) override
    {
        if (linkArea (e.position))
            juce::URL (home).launchInDefaultBrowser();
    }

    void paint (juce::Graphics& g) override
    {
        namespace brand = felitronics::appkit::brand;

        const float h  = (float) getHeight();
        const float cy = h * 0.5f;

        if (hover)
        {
            g.setColour (brand::violet.withAlpha (0.14f));
            g.fillRoundedRectangle (marksArea(), 8.0f);
            g.fillRoundedRectangle (wordsArea(), 8.0f);
        }

        if (logo != nullptr)
            logo->drawWithin (g, catArea(), juce::RectanglePlacement::centred, 1.0f);

        const float d = markD();
        brand::drawOrbit (g, h + 6.0f + d * 0.5f, cy, d, hover);

        // The words on the first line, on one baseline, as BrandHeader sets them on its one.
        float x = marksRight();
        const auto wf = wordmarkFont();
        const float baseline = (float) firstLineH * 0.5f + (wf.getAscent() - wf.getDescent()) * 0.5f;
        g.setFont (wf);
        g.setColour (hover ? juce::Colours::white : juce::Colour (0xffeef0f6));
        g.drawSingleLineText (product, juce::roundToInt (x), juce::roundToInt (baseline));

        x += textWidth (wf, product) + 14.0f;
        g.setFont (bylineFont());
        g.setColour (hover ? brand::violet.brighter (0.3f) : brand::violet);
        g.drawSingleLineText (byline, juce::roundToInt (x), juce::roundToInt (baseline));
    }

private:
    float markD() const { return (float) getHeight() * 0.86f; }

    juce::Font wordmarkFont() const { return juce::Font (juce::FontOptions().withHeight (wordmarkH).withTypeface (wordmarkFace)); }
    juce::Font bylineFont()   const { return juce::Font (juce::FontOptions().withHeight (bylineH).withTypeface (wordmarkFace)); }

    juce::Rectangle<float> marksArea() const { return { 0.0f, 0.0f, marksRight() - 8.0f, (float) getHeight() }; }
    juce::Rectangle<float> wordsArea() const
    {
        const float from = marksRight() - 6.0f;
        return { from, 0.0f, contentRight() - from, (float) firstLineH };
    }

    bool linkArea (juce::Point<float> p) const { return marksArea().contains (p) || wordsArea().contains (p); }

    void setHover (bool h)
    {
        setMouseCursor (h ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        if (h != hover) { hover = h; repaint(); }
    }

    static float textWidth (const juce::Font& f, const juce::String& s)
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText (f, s, 0.0f, 0.0f);
        return ga.getBoundingBox (0, -1, true).getWidth();
    }

    std::unique_ptr<juce::Drawable> logo;
    juce::Typeface::Ptr wordmarkFace;
    juce::String product, home;
    const juce::String byline { "by Darwin's Cat" };
    bool hover = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TwoLineBrand)
};

} // namespace orbitamp
