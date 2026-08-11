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
        : BlockFrame ("Gate", Kind::dsp), pressureDb (pressureDbSource)
    {
        titleHeight = 11.0f;
        attachPower (gateOnParam);
        startTimerHz (30);
    }

    /** The click that is the zoom's — everything else (switch, menu) is the frame's. */
    std::function<void()> onOpen;

    static constexpr int designWidth = 110;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu() && ! switchArea().contains (e.getPosition()))
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
            g.setColour (juce::Colour (0xffe0503c).withAlpha (0.85f * depth));
            g.fillRoundedRectangle (boxArea().toFloat().reduced (theme::blockBorder + 1.0f),
                                    theme::radiusMd - 2.0f);
        }
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
