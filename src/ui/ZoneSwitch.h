#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Theme.h"

#include <memory>

namespace orbitamp
{

/** The small switch that turns a ZONE of a block on and off.

    A block has three of them at most: the block itself, in the frame's top border; the device's own
    controls; and our EQ. Deliberately the same object in all three places, smaller here — a player
    learns one shape and then knows what any of them does, and what it does is always the same thing:
    take this part out of the signal without taking anything else with it.

    It is a bypass, not a tab. Switching the device's controls off means hearing the capture with
    nothing measured applied to it, which is a comparison worth making; switching our EQ off means
    hearing the device without us. Neither hides a control — everything stays on the panel, greyed,
    because a control you cannot see is a control you forget you set. */
class ZoneSwitch : public juce::Component
{
public:
    ZoneSwitch() { setInterceptsMouseClicks (true, false); }

    void attach (juce::RangedAudioParameter& param)
    {
        attachment = std::make_unique<juce::ParameterAttachment> (param,
            [this] (float v) { setOn (v > 0.5f, false); });
        attachment->sendInitialUpdate();
    }

    bool isOn() const noexcept { return on; }

    void setOn (bool shouldBeOn, bool notify = true)
    {
        if (on == shouldBeOn)
            return;

        on = shouldBeOn;
        repaint();

        if (notify && attachment != nullptr)
            attachment->setValueAsCompleteGesture (on ? 1.0f : 0.0f);

        if (onChange != nullptr)
            onChange (on);
    }

    std::function<void (bool)> onChange;
    juce::Colour accent = theme::orange;

    void paint (juce::Graphics& g) override
    {
        auto sw = getLocalBounds().toFloat().reduced (0.5f);
        const float r = sw.getHeight() * 0.5f;

        g.setColour (on ? accent.withAlpha (0.5f).overlaidWith (juce::Colour (0xff26262f))
                        : juce::Colour (0xff26262f));
        g.fillRoundedRectangle (sw, r);

        // The pill in the accent, near full strength — a hairline here left only the knob dot
        // readable on a dark thumb, and a switch is an OVAL with a dot, not a dot.
        g.setColour (accent.withAlpha (0.85f));
        g.drawRoundedRectangle (sw, r, 1.2f);

        const float knob = sw.getHeight() - 4.0f;
        const float kx   = on ? sw.getRight() - knob - 2.0f : sw.getX() + 2.0f;

        g.setColour (on ? juce::Colours::white : juce::Colour (0xff8a8a96));
        g.fillEllipse (kx, sw.getCentreY() - knob * 0.5f, knob, knob);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        // Left toggles; right belongs to whoever owns the popup menu around this switch.
        if (! e.mods.isPopupMenu())
            setOn (! on);
    }

    static constexpr int designWidth  = 20;
    static constexpr int designHeight = 11;

private:
    std::unique_ptr<juce::ParameterAttachment> attachment;
    bool on = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZoneSwitch)
};

} // namespace orbitamp
