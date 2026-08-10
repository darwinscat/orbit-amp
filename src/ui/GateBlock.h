#pragma once

#include "../Parameters.h"
#include "BlockFrame.h"
#include "Knob.h"

#include <atomic>

namespace orbitamp
{

/** The noise gate, zoomed: the threshold — the one decision the gate asks of you — and a live
    gain-reduction meter, so setting it is watching it work. Everything else about the gate's feel
    is fixed in the engine, and a knob per fixed opinion would be nine knobs of noise. */
class GateBlock final : public BlockFrame,
                        private juce::Timer
{
public:
    GateBlock (juce::AudioProcessorValueTreeState& s, const std::atomic<float>& meterDbSource)
        : BlockFrame ("Gate", Kind::dsp), meterDb (meterDbSource)
    {
        threshold.textForValue = [] (double v) { return juce::String (juce::roundToInt (v)) + " dB"; };
        addAndMakeVisible (threshold);

        thresholdAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            s, params::gateThreshold, threshold);

        attachPower (*s.getParameter (params::gateOn));
    }

private:
    void visibilityChanged() override
    {
        if (isVisible())
            startTimerHz (30);
        else
            stopTimer();
    }

    void timerCallback() override { repaint(); }

    void layOutContent (juce::Rectangle<int> area) override
    {
        meterWell = area.removeFromRight (area.getWidth() / 4).reduced (area.getWidth() / 12, 8);

        const int side = juce::jmin (200, juce::jmin (area.getWidth(), area.getHeight()));
        threshold.setBounds (area.withSizeKeepingCentre (side, side));
    }

    void paintContent (juce::Graphics& g) override
    {
        // The meter: attenuation from the top down, the way a gate actually presses. Idle at the
        // bottom of the well means wide open.
        auto r = meterWell.toFloat();

        g.setColour (theme::bezel);
        g.fillRoundedRectangle (r, theme::radiusMd);

        const float depthDb = juce::jlimit (0.0f, meterRangeDb, -meterDb.load());
        const float h = r.getHeight() * depthDb / meterRangeDb;

        if (h > 0.5f)
        {
            g.setColour (theme::violet.withAlpha (0.85f));
            g.fillRoundedRectangle (r.withHeight (h), theme::radiusSm);
        }

        g.setColour (theme::hair2);
        g.drawRoundedRectangle (r.reduced (0.5f), theme::radiusMd, 1.0f);

        g.setColour (theme::txDim);
        theme::drawTracked (g, "GR", r.withTrimmedTop (r.getHeight()).withHeight (12.0f).translated (0.0f, 2.0f),
                            theme::displayFont (7.0f), 0.14f, juce::Justification::centred);
    }

    static constexpr float meterRangeDb = 60.0f;

    const std::atomic<float>& meterDb;

    Knob threshold { "Threshold", theme::violet, 0 };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> thresholdAtt;

    juce::Rectangle<int> meterWell;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GateBlock)
};

} // namespace orbitamp
