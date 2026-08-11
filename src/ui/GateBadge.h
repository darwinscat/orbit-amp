#pragma once

#include "BlockFrame.h"

#include <atomic>

namespace orbitamp
{

/** The gate's door, standing left of the tuner: a small framed GATE with its switch on the
    border, zoom-style. A click on it — and only on it — opens the gate's zoom. While the gate
    presses, the inside floods red by depth: the badge is the tally light of the whole device. */
class GateBadge final : public BlockFrame,
                        private juce::Timer
{
public:
    GateBadge (juce::RangedAudioParameter& gateOnParam, const std::atomic<float>& pressureDbSource)
        : BlockFrame ("Gate", Kind::dsp, false /*no switch — the menu still has the power*/),
          pressureDb (pressureDbSource)
    {
        showTitle = false;   // the name goes INSIDE the box, not on the border
        attachPower (gateOnParam);
        startTimerHz (30);
    }

    /** The click that is the zoom's — everything else (switch, menu) is the frame's. */
    std::function<void()> onOpen;

    static constexpr int designWidth = 110;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu())
        {
            if (onOpen != nullptr)
                onOpen();
            return;
        }

        BlockFrame::mouseDown (e);
    }

private:
    void paintContent (juce::Graphics& g) override
    {
        // The red rises with the pressure — the same depth law as the rail's desaturation,
        // told in the one colour this face reserves for "being held down".
        const float depth = juce::jlimit (0.0f, 1.0f, -pressureDb.load() / 40.0f);

        if (depth > 0.01f)
        {
            // SOLID, interpolated toward the brand orange by depth — never a translucent orange
            // wash (that rots to brick on this panel); the fill is opaque at every depth.
            g.setColour (theme::dspTop.interpolatedWith (theme::orange, depth));
            g.fillRoundedRectangle (boxArea().toFloat().reduced (theme::blockBorder + 1.0f),
                                    theme::radiusMd - 2.0f);
        }

        // The name, in the middle of the box — the badge IS the word.
        g.setColour (theme::lilac);
        theme::drawTracked (g, "GATE", boxArea().toFloat(), theme::displayFont (13.0f), 0.15f,
                            juce::Justification::centred);
    }

    void timerCallback() override
    {
        const float depth = juce::jlimit (0.0f, 1.0f, -pressureDb.load() / 40.0f);
        if (std::abs (depth - shownDepth) > 0.01f)
        {
            shownDepth = depth;
            repaint();
        }
    }

    const std::atomic<float>& pressureDb;
    float shownDepth = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GateBadge)
};

} // namespace orbitamp
