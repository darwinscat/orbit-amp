#pragma once

#include "../Parameters.h"
#include "MeterRail.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace orbitamp
{

/** The OUT sliver, standing right of the faceplate — the IN column's mirror: the final level on
    the family meter rail, a latched clip cap, and the OUTPUT TRIM riding it as tabby's hollow
    sliding frame. Drag sets it, double-click is 0 dB, a click on a red cap clears the latch. */
class OutStrip final : public juce::Component,
                       private juce::Timer
{
public:
    OutStrip (const std::atomic<float>& outDbSource, std::atomic<bool>& clipLatch,
              juce::RangedAudioParameter& trimParam)
        : outDb (outDbSource), clip (clipLatch), trimP (trimParam)
    {
        trim = std::make_unique<juce::ParameterAttachment> (trimParam,
                                                            [this] (float) { refreshValue(); repaint(); });

        meterrail::initReadout (value, trimP, [this] { return trim.get(); });
        addAndMakeVisible (value);
        refreshValue();

        startTimerHz (30);
    }

    void resized() override
    {
        value.setBounds (getLocalBounds().removeFromBottom (26).removeFromTop (13));
    }

    void refreshValue()
    {
        if (! value.isBeingEdited())
            value.setText (meterrail::trimText (trimP.convertFrom0to1 (trimP.getValue())),
                           juce::dontSendNotification);
    }

    static constexpr int designWidth = 26;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();

        g.setColour (theme::bezel);
        g.fillRoundedRectangle (r, theme::radiusSm);

        const auto col = scaleArea();

        meterrail::paintFill (g, col, dbToY (col, levelDb));

        if (holdDb > floorDb + 0.5f)
            meterrail::paintHold (g, col, dbToY (col, holdDb));

        meterrail::paintClipCap (g, col, clip.load());

        meterrail::paintUnityNubs (g, r.reduced (0.0f, 2.0f), trimY (col, 0.0f));
        meterrail::paintGrip (g, r, trimY (col, trimP.convertFrom0to1 (trimP.getValue())),
                              dragging);

        g.setColour (theme::hair2);
        g.drawRoundedRectangle (r.reduced (0.5f), theme::radiusSm, 1.0f);

        g.setColour (theme::txDim);
        theme::drawTracked (g, "OUT", r.withTrimmedTop (r.getHeight() - 13.0f),
                            theme::displayFont (8.0f), 0.10f, juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        // The reset click must not turn into a fader drag.
        if (clip.load() && e.position.y <= scaleArea().getY() + 5.0f)
        {
            clip.store (false);
            swallow = true;
            repaint();
            return;
        }

        swallow  = false;
        dragging = true;
        trim->beginGesture();
        trim->setValueAsPartOfGesture (trimFromY (scaleArea(), e.position.y));
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! swallow)
            trim->setValueAsPartOfGesture (trimFromY (scaleArea(), e.position.y));
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (! swallow)
            trim->endGesture();

        dragging = false;
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        // Home is one knock away; the write rides the double-click's own open gesture.
        if (! swallow)
            trim->setValueAsPartOfGesture (0.0f);
    }

private:
    void timerCallback() override
    {
        const float now = outDb.load();
        levelDb = now > levelDb ? now : juce::jmax (now, levelDb - releasePerTick);

        if (now >= holdDb)
        {
            holdDb  = now;
            holdAge = 0;
        }
        else if (++holdAge > holdTicks)
        {
            holdDb = juce::jmax (floorDb, holdDb - holdReleasePerTick);
        }

        repaint();
    }

    juce::Rectangle<float> scaleArea() const
    {
        return getLocalBounds().toFloat().reduced (2.0f).withTrimmedBottom (26.0f);
    }

    float dbToY (juce::Rectangle<float> r, float db) const
    {
        return r.getBottom() - r.getHeight() * (juce::jlimit (floorDb, 0.0f, db) - floorDb) / -floorDb;
    }

    float trimY (juce::Rectangle<float> r, float trimDb) const
    {
        return r.getCentreY() - trimDb / params::inTrimRangeDb * (r.getHeight() * 0.5f - 6.0f);
    }

    float trimFromY (juce::Rectangle<float> r, float y) const
    {
        return juce::jlimit (-params::inTrimRangeDb, params::inTrimRangeDb,
                             (r.getCentreY() - y) / juce::jmax (1.0f, r.getHeight() * 0.5f - 6.0f)
                                 * params::inTrimRangeDb);
    }

    static constexpr float floorDb            = -80.0f;
    static constexpr float releasePerTick     = 1.4f;
    static constexpr int   holdTicks          = 60;
    static constexpr float holdReleasePerTick = 0.8f;

    const std::atomic<float>& outDb;
    std::atomic<bool>&        clip;
    juce::RangedAudioParameter& trimP;
    std::unique_ptr<juce::ParameterAttachment> trim;

    juce::Label value;

    float levelDb = -90.0f;
    float holdDb  = -90.0f;
    int   holdAge = 0;
    bool  dragging = false;
    bool  swallow  = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutStrip)
};

} // namespace orbitamp
