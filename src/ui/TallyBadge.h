#pragma once

#include "BlockFrame.h"

#include <atomic>

namespace orbitamp
{

/** A tally badge: a small framed WORD that floods brand orange while its device is working —
    GATE left of the tuner, LIMIT right of it, the two guards flanking the needle. A click opens
    whatever `onOpen` says (the gate's zoom); right-click keeps the power menu.

    `fullDepthDb` is how many dB of pressure paint the flood solid — a gate leans on tens of dB,
    a limiter earns full orange after a few. */
class TallyBadge final : public BlockFrame,
                         private juce::Timer
{
public:
    TallyBadge (const juce::String& word, juce::RangedAudioParameter& onParam,
                const std::atomic<float>& pressureDbSource, float fullDepthDbIn)
        : BlockFrame (word, Kind::dsp, false /*no switch — the menu still has the power*/),
          label (word.toUpperCase()), pressureDb (pressureDbSource), fullDepthDb (fullDepthDbIn)
    {
        showTitle   = false;   // the name goes INSIDE the box, not on the border
        borderAlpha = 0.45f;   // a quiet frame at all times — the word and the flood do the talking
        attachPower (onParam);
        startTimerHz (30);
    }

    /** The click, with where it landed on screen — the owner opens its menu there. */
    std::function<void (juce::Point<int>)> onOpen;

    /** Optional latched mark: "worked since you last looked". The click that opens the menu is
        the look, so it clears it. */
    std::atomic<bool>* latch = nullptr;

    /** The flood's colour — violet for both guards, matching their lilac runners: the flood
        says WORKING, the orange corner dot alone says something needs a look. */
    juce::Colour flood = theme::violet;

    static constexpr int designWidth = 82;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu())
        {
            // The dot's own corner answers by itself: a click there ACKNOWLEDGES — no menu.
            // Anywhere else opens the menu (and counts as the look too).
            if (latch != nullptr && latch->load() && dotArea().contains (e.getPosition()))
            {
                latch->store (false);
                repaint();
                return;
            }

            if (latch != nullptr)
                latch->store (false);   // seen

            if (onOpen != nullptr)
                onOpen (e.getScreenPosition());
            return;
        }

        BlockFrame::mouseDown (e);
    }

private:
    void paintContent (juce::Graphics& g) override
    {
        const float depth = juce::jlimit (0.0f, 1.0f, -pressureDb.load() / fullDepthDb);

        if (depth > 0.01f)
        {
            // SOLID, interpolated toward the flood colour by depth — never a translucent wash
            // (orange rots to brick on this panel); the fill is opaque at every depth.
            g.setColour (theme::dspTop.interpolatedWith (flood, depth));
            g.fillRoundedRectangle (boxArea().toFloat().reduced (theme::blockBorder + 1.0f),
                                    theme::radiusMd - 2.0f);
        }

        // The latched mark: a solid orange dot in the corner — it worked while you were away.
        if (latch != nullptr && latch->load())
        {
            const auto d = dotArea().toFloat();
            g.setColour (theme::orange);
            g.fillEllipse (d.withSizeKeepingCentre (6.0f, 6.0f));
        }

        // The name, in the middle of the box — the badge IS the word. On the orange flood the
        // lilac drowns, so the letters whiten with the depth.
        g.setColour (theme::lilac.interpolatedWith (juce::Colours::white, depth));
        theme::drawTracked (g, label, boxArea().toFloat(), theme::displayFont (17.0f), 0.12f,
                            juce::Justification::centred);
    }

    /** The dot's corner, generous enough to hit: its click acknowledges without the menu. */
    juce::Rectangle<int> dotArea() const
    {
        return boxArea().removeFromTop (20).removeFromRight (20);
    }

    void timerCallback() override
    {
        const float depth = juce::jlimit (0.0f, 1.0f, -pressureDb.load() / fullDepthDb);
        if (std::abs (depth - shownDepth) > 0.01f)
        {
            shownDepth = depth;
            repaint();
        }
    }

    const juce::String label;
    const std::atomic<float>& pressureDb;
    const float fullDepthDb;
    float shownDepth = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TallyBadge)
};

} // namespace orbitamp
