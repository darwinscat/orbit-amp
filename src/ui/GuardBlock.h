// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "BlockFrame.h"
#include "GuardRail.h"

namespace orbitamp
{

/** A GUARD'S BLOCK — the narrow block a guard stands in on the panel: the gate's at the head of the
    first row, before the AMP, the limiter's at the end of the second, after the cabinet, so the
    panel reads in the strip's own order (Oleh, 24.10). Our own DSP, so violet.

    Inside, the guard's small rail — the threshold, the ceiling — at the block's edge toward the
    window's, its fixed scale beside it toward the centre (the gate's rail left, the limiter's right:
    `railOnLeft`), and its lamp under the rail, a square flooding while the guard presses, the orange
    dot when it worked unseen. Its name rides the
    border, centred, the way the wide blocks carry theirs. A click anywhere on it, either button, is
    the guard's menu; only a drag on the rail moves its hand (Oleh, 24.10). Its arrow in the strip
    switches it, and a guard switched off dims in place like every block.

    As narrow as its name on the border allows: the name and the gap the line keeps either side of
    it, between the frame's rounded corners.

    A guard's block stays in reach while off: its rail dragged is a way back on — the gate's out of
    its OFF stop, the limiter's anywhere. */
class GuardBlock final : public BlockFrame
{
public:
    GuardBlock (const juce::String& guardName, juce::RangedAudioParameter& valueParam,
                juce::RangedAudioParameter& onParam, float footValue, float headValue, bool footIsOff,
                std::function<float()> pressDepth, std::atomic<bool>& workedLatch)
        : BlockFrame (guardName, Kind::dsp),
          name (guardName.toUpperCase()),
          rail (valueParam, onParam, footValue, headValue, footIsOff),
          lamp ({}, onParam, std::move (pressDepth), workedLatch)
    {
        showTitle      = false;       // written centred on the border here, not at the frame's left
        shieldsWhenOff = false;       // dark, and still in reach: a drag switches it back on

        addAndMakeVisible (rail);
        addAndMakeVisible (lamp);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);

        rail.onMenu  = [this] (juce::Point<int> p) { openMenu (p); };
        lamp.onClick = [this] (juce::Point<int> p) { openMenu (p); };

        attachPower (onParam);
    }

    static constexpr int designWidth = 80;

    /** The guard's menu, at the mouse. */
    std::function<void (juce::Point<int> screenPos)> onMenu;

    /** The scale's marks, the labelled ones numbered — unsigned, the way the columns write theirs. */
    struct Mark { float value; bool labelled; };
    std::vector<Mark> marks;
    int  labelDecimals = 0;

    /** The rail at the block's left edge, the scale to its right — or mirrored. */
    bool railOnLeft = true;

    void mouseDown (const juce::MouseEvent& e) override { openMenu (e.getScreenPosition()); }

protected:
    void layOutContent (juce::Rectangle<int> r) override
    {
        // The name, centred on the top line; the frame opens the line under it.
        const auto box = boxArea();
        const int  w   = juce::roundToInt (theme::trackedWidth (name, theme::displayFont (titleHeight), 0.15f)) + 1;
        nameBox        = juce::Rectangle<int> (w, (int) titleHeight + 6).withCentre ({ box.getCentreX(), box.getY() });
        borderSlotUsed = nameBox;

        // The rail, and its lamp under it as wide as it is, at the outer edge; the scale beside them.
        auto col = railOnLeft ? r.removeFromLeft (GuardRail::designWidth) : r.removeFromRight (GuardRail::designWidth);
        lamp.setBounds (col.removeFromBottom (GuardRail::designWidth));
        col.removeFromBottom (lampGap);
        rail.setBounds (col);
        scaleBox = r.withTop (col.getY()).withBottom (col.getBottom());
    }

    void paintContent (juce::Graphics& g) override
    {
        g.setColour (theme::lilac);
        theme::drawTracked (g, name, nameBox.toFloat(), theme::displayFont (titleHeight), 0.15f,
                            juce::Justification::centred);

        // THE SCALE, fixed: a tick against the rail at every mark, the number beyond it.
        const auto s    = scaleBox.toFloat();
        const auto font = theme::displayFont (9.5f);

        for (const auto& m : marks)
        {
            const float y   = (float) rail.getY() + rail.yOfValue (m.value);
            const float len = m.labelled ? 5.0f : 3.0f;

            g.setColour (juce::Colours::white.withAlpha (m.labelled ? 0.45f : 0.28f));
            g.fillRect (railOnLeft ? s.getX() + 1.0f : s.getRight() - 1.0f - len, y - 0.5f, len, 1.0f);

            if (! m.labelled)
                continue;

            const auto text = labelDecimals == 0 ? juce::String (std::abs (juce::roundToInt (m.value)))
                                                 : juce::String (std::abs (m.value), labelDecimals);
            const auto box  = juce::Rectangle<float> (s.getX(), y - 6.0f, s.getWidth(), 12.0f);

            g.setColour (juce::Colours::white.withAlpha (0.5f));
            theme::drawTracked (g, text, railOnLeft ? box.withTrimmedLeft (8.0f) : box.withTrimmedRight (8.0f),
                                font, 0.02f, railOnLeft ? juce::Justification::centredLeft
                                                        : juce::Justification::centredRight);
        }
    }

private:
    void openMenu (juce::Point<int> screenPos)
    {
        if (onMenu != nullptr)
            onMenu (screenPos);
    }

    static constexpr int lampGap = 8;

    const juce::String name;
    GuardRail rail;
    GuardLamp lamp;
    juce::Rectangle<int> nameBox, scaleBox;

    friend class WebFaceplate;   // the site's kit reads where the parts stand (WebFaceplate.h)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GuardBlock)
};

} // namespace orbitamp
