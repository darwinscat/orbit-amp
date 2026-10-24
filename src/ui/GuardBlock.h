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

    Inside, the guard's small rail — the threshold, the ceiling — and its lamp under it, a square
    flooding while the guard presses, the orange dot when it worked unseen. Its name rides the
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

    /** The rail, for the ruler its owner summons beside it. */
    GuardRail& slider() noexcept { return rail; }

    void mouseDown (const juce::MouseEvent& e) override { openMenu (e.getScreenPosition()); }

protected:
    void layOutContent (juce::Rectangle<int> r) override
    {
        // The name, centred on the top line; the frame opens the line under it.
        const auto box = boxArea();
        const int  w   = juce::roundToInt (theme::trackedWidth (name, theme::displayFont (titleHeight), 0.15f)) + 1;
        nameBox        = juce::Rectangle<int> (w, (int) titleHeight + 6).withCentre ({ box.getCentreX(), box.getY() });
        borderSlotUsed = nameBox;

        // The rail, and its lamp under it as wide as it is: one column, centred.
        auto col = r.withSizeKeepingCentre (GuardRail::designWidth, r.getHeight());
        lamp.setBounds (col.removeFromBottom (GuardRail::designWidth));
        col.removeFromBottom (lampGap);
        rail.setBounds (col);
    }

    void paintContent (juce::Graphics& g) override
    {
        g.setColour (theme::lilac);
        theme::drawTracked (g, name, nameBox.toFloat(), theme::displayFont (titleHeight), 0.15f,
                            juce::Justification::centred);
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
    juce::Rectangle<int> nameBox;

    friend class WebFaceplate;   // the site's kit reads where the parts stand (WebFaceplate.h)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GuardBlock)
};

} // namespace orbitamp
