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
                                                            [this] (float) { repaint(); });
        startTimerHz (30);
    }

    static constexpr int designWidth = 32;

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

        meterrail::paintDbScale (g, r.reduced (0.0f, 2.0f),
                                 [&] (float db) { return dbToY (col, db); }, floorDb);
        meterrail::paintUnityNubs (g, r.reduced (0.0f, 2.0f), trimY (col, 0.0f));
        {
            const float v = trimP.convertFrom0to1 (trimP.getValue());
            meterrail::paintGrip (g, r, trimY (col, v), meterrail::trimText (v), dragging);
        }

        g.setColour (theme::hair2);
        g.drawRoundedRectangle (r.reduced (0.5f), theme::radiusSm, 1.0f);

        meterrail::paintName (g, col, "OUT");
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

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (swallow)
            return;

        // On the grip: type the number. Anywhere else on the column: home.
        if (gripRect().contains (e.position))
        {
            gripEd.open (*this, gripRect().toNearestInt(),
                         trimP.convertFrom0to1 (trimP.getValue()),
                         [this] (float v) { trim->setValueAsCompleteGesture (v); });
            return;
        }

        trim->setValueAsPartOfGesture (0.0f);
    }

    juce::Rectangle<float> gripRect() const
    {
        const float y = trimY (scaleArea(), trimP.convertFrom0to1 (trimP.getValue()));
        return { 0.0f, y - meterrail::gripH * 0.5f, (float) getWidth(), meterrail::gripH };
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
        return getLocalBounds().toFloat().reduced (2.0f);
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

    meterrail::GripEditor gripEd;

    float levelDb = -90.0f;
    float holdDb  = -90.0f;
    int   holdAge = 0;
    bool  dragging = false;
    bool  swallow  = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutStrip)
};

} // namespace orbitamp
