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
              juce::RangedAudioParameter& trimParam, juce::RangedAudioParameter& ceilingParam)
        : outDb (outDbSource), clip (clipLatch), trimP (trimParam), ceilP (ceilingParam)
    {
        trim = std::make_unique<juce::ParameterAttachment> (trimParam,
                                                            [this] (float) { repaint(); });
        ceil = std::make_unique<juce::ParameterAttachment> (ceilingParam,
                                                            [this] (float) { repaint(); });
        startTimerHz (30);
    }

    static constexpr int designWidth = 38;

    /** The trim runner entered/left the hand — the editor slides the drag ruler out beside us. */
    std::function<void (bool)> onTrimDrag;

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

        // The ghost: the peak-hold's future under the trim in hand — the consequence, on the grid.
        if (dragging && ! grabbedCeil)
        {
            const float delta = trimP.convertFrom0to1 (trimP.getValue()) - trimStartDb;
            if (holdAtGrab > floorDb + 0.5f && std::abs (delta) > 0.05f)
            {
                const float gy = dbToY (col, holdAtGrab + delta);
                const float dashes[] = { 4.0f, 3.0f };
                g.setColour (juce::Colours::white.withAlpha (0.55f));
                g.drawDashedLine ({ col.getX(), gy, col.getRight(), gy }, dashes, 2, 1.4f);
            }
        }

        meterrail::paintDbScale (g, r.reduced (0.0f, 2.0f),
                                 [&] (float db) { return dbToY (col, db); }, floorDb);
        meterrail::paintUnityNubs (g, r.reduced (0.0f, 2.0f), trimY (col, 0.0f));
        // The ceiling first (under the trim in z): the limiter's runner in the gate's lilac,
        // living where ceilings live — near the top of the rail.
        {
            const float v = ceilP.convertFrom0to1 (ceilP.getValue());
            meterrail::paintGrip (g, r, ceilY (col, v), juce::String (v, 1), theme::lilac,
                                  dragging && grabbedCeil);
        }

        {
            const float v = trimP.convertFrom0to1 (trimP.getValue());
            meterrail::paintGrip (g, r, trimY (col, v), meterrail::trimText (v), theme::orange,
                                  dragging && ! grabbedCeil);
        }

        g.setColour (theme::hair2);
        g.drawRoundedRectangle (r.reduced (0.5f), theme::radiusSm, 1.0f);

        meterrail::paintName (g, col, "OUT");
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        // A click anywhere while the grip editor is open COMMITS it first — the field convention.
        if (gripEd.isOpen())
        {
            gripEd.takeAndHide();
            swallow = true;
            return;
        }

        // The reset click must not turn into a fader drag.
        if (clip.load() && e.position.y <= scaleArea().getY() + 5.0f)
        {
            clip.store (false);
            swallow = true;
            repaint();
            return;
        }

        swallow  = false;
        dragging = false;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (swallow)
            return;

        if (! dragging && e.getDistanceFromDragStart() > 4)
        {
            dragging = true;

            // Whichever runner was nearer to the grab moves — same law as the IN column.
            const auto col = scaleArea();
            const float trY = trimY (col, trimP.convertFrom0to1 (trimP.getValue()));
            const float ceY = ceilY (col, ceilP.convertFrom0to1 (ceilP.getValue()));
            grabbedCeil = std::abs (e.getMouseDownPosition().toFloat().y - ceY)
                        < std::abs (e.getMouseDownPosition().toFloat().y - trY);

            (grabbedCeil ? ceil : trim)->beginGesture();
            trimStartDb = trimP.convertFrom0to1 (trimP.getValue());
            holdAtGrab  = holdDb;

            if (! grabbedCeil && onTrimDrag != nullptr)
                onTrimDrag (true);
        }

        if (dragging)
        {
            if (grabbedCeil)
                ceil->setValueAsPartOfGesture (ceilFromY (scaleArea(), e.position.y));
            else
                trim->setValueAsPartOfGesture (trimFromY (scaleArea(), e.position.y));
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (swallow)
            return;

        if (dragging)
        {
            (grabbedCeil ? ceil : trim)->endGesture();
            if (! grabbedCeil && onTrimDrag != nullptr)
                onTrimDrag (false);
            dragging = false;
            repaint();
        }
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (swallow)
            return;

        // The convention: DOUBLE-click a grip to type its number; elsewhere — home.
        if (gripRect().contains (e.position))
        {
            gripEd.open (*this, gripRect().toNearestInt(),
                         trimP.convertFrom0to1 (trimP.getValue()), theme::orange,
                         [this] (float v) { trim->setValueAsCompleteGesture (v); });
            return;
        }

        if (ceilRect().contains (e.position))
        {
            gripEd.open (*this, ceilRect().toNearestInt(),
                         ceilP.convertFrom0to1 (ceilP.getValue()), theme::lilac,
                         [this] (float v) { ceil->setValueAsCompleteGesture (v); });
            return;
        }

        trim->setValueAsCompleteGesture (0.0f);
    }

    juce::Rectangle<float> gripRect() const
    {
        const float y = trimY (scaleArea(), trimP.convertFrom0to1 (trimP.getValue()));
        return { 0.0f, y - meterrail::gripH * 0.5f, (float) getWidth(), meterrail::gripH };
    }

    juce::Rectangle<float> ceilRect() const
    {
        const float y = ceilY (scaleArea(), ceilP.convertFrom0to1 (ceilP.getValue()));
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

    /** The ceiling lives in the rail's top third — the neighbourhood its dBFS values name —
        loudest lid at the top, deepest at the third's floor. */
    float ceilY (juce::Rectangle<float> r, float v) const
    {
        const float t = (params::limiterCeilingMax - v)
                      / (params::limiterCeilingMax - params::limiterCeilingMin);
        return r.getY() + 14.0f + t * (r.getHeight() / 3.0f);
    }

    float ceilFromY (juce::Rectangle<float> r, float y) const
    {
        const float t = juce::jlimit (0.0f, 1.0f, (y - r.getY() - 14.0f) / (r.getHeight() / 3.0f));
        return params::limiterCeilingMax - t * (params::limiterCeilingMax - params::limiterCeilingMin);
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
    juce::RangedAudioParameter& ceilP;
    std::unique_ptr<juce::ParameterAttachment> trim, ceil;

    meterrail::GripEditor gripEd;

    float levelDb = -90.0f;
    float holdDb  = -90.0f;
    int   holdAge = 0;
    bool  dragging    = false;
    bool  swallow     = false;
    bool  grabbedCeil = false;
    float trimStartDb = 0.0f;
    float holdAtGrab  = -90.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutStrip)
};

} // namespace orbitamp
