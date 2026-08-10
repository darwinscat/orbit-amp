#pragma once

#include "../Parameters.h"
#include "Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace orbitamp
{

/** The gate as a sliver: a thin IN meter standing left of the faceplate with the gate's whole
    story on it.

    The input climbs a FIXED meter scale — violet through magenta into orange, the classic
    green-to-red told in this face's colours; the bar reveals the scale rather than carrying its
    own. The threshold runner rides the same scale and DRAGS — it writes the same parameter every
    other gate control writes. The gate's pressure descends the right lane in the ramp's red,
    solid — the one colour family this panel lets be a fill — meeting the input it is squeezing.

    A drag moves the threshold; a clean click opens the gate's zoom. */
class GateStrip final : public juce::Component,
                        private juce::Timer
{
public:
    GateStrip (const std::atomic<float>& keyDbSource, const std::atomic<float>& pressureDbSource,
               juce::RangedAudioParameter& thresholdParam)
        : keyDb (keyDbSource), pressureDb (pressureDbSource), param (thresholdParam)
    {
        threshold = std::make_unique<juce::ParameterAttachment> (thresholdParam,
                                                                 [this] (float) { repaint(); });
        startTimerHz (30);
    }

    /** A clean click (no drag) opens the big gate — the sliver is the glance. */
    std::function<void()> onClick;

    static constexpr int designWidth = 26;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();

        g.setColour (theme::bezel);
        g.fillRoundedRectangle (r, theme::radiusSm);

        auto lanes = scaleArea();
        auto inCol = lanes.removeFromLeft (lanes.getWidth() * 0.62f);
        lanes.removeFromLeft (2.0f);
        const auto grCol = lanes;

        // ---- IN: the input revealing a fixed scale, quiet violet to hot orange ----
        if (const float ly = dbToY (inCol, levelDb); ly < inCol.getBottom() - 1.0f)
        {
            juce::ColourGradient scale (theme::violet, inCol.getX(), inCol.getBottom(),
                                        theme::orange, inCol.getX(), inCol.getY(), false);
            scale.addColour (0.55, transition);
            g.setGradientFill (scale);
            g.fillRoundedRectangle (inCol.withTop (ly), 2.0f);
        }

        // ---- the threshold runner, dragged on the very scale it judges ----
        {
            const float openDb  = param.convertFrom0to1 (param.getValue());
            const float openY   = dbToY (inCol, openDb);
            const float closeY  = dbToY (inCol, openDb - params::gateHysteresisDb);

            g.setColour (theme::lilac.withAlpha (0.45f));
            g.fillRect (inCol.getX(), closeY - 0.5f, inCol.getWidth(), 1.0f);

            g.setColour (theme::lilac);
            g.fillRect (inCol.getX(), openY - 1.0f, inCol.getWidth(), 2.0f);

            juce::Path caret;
            caret.addTriangle (inCol.getX() - 1.0f, openY - 4.0f, inCol.getX() - 1.0f, openY + 4.0f,
                               inCol.getX() + 5.0f, openY);
            g.fillPath (caret);
        }

        // ---- GR: the gate pressing down the right lane, in solid red ----
        {
            const float depth = juce::jlimit (0.0f, -floorDb, -pressureDb.load());

            if (const float h = grCol.getHeight() * depth / -floorDb; h > 0.5f)
            {
                g.setColour (gateRed);
                g.fillRoundedRectangle (grCol.withHeight (h), 2.0f);
            }
        }

        g.setColour (theme::hair2);
        g.drawRoundedRectangle (r.reduced (0.5f), theme::radiusSm, 1.0f);

        g.setColour (theme::txDim);
        theme::drawTracked (g, "IN", r.withTrimmedTop (r.getHeight() - 13.0f),
                            theme::displayFont (8.0f), 0.10f, juce::Justification::centred);
    }

    //==============================================================================
    void mouseDown (const juce::MouseEvent&) override { dragging = false; }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging && e.getDistanceFromDragStart() > 4)
        {
            dragging = true;
            threshold->beginGesture();
        }

        if (dragging)
            threshold->setValueAsPartOfGesture (yToDb (scaleArea(), e.position.y));
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging)
        {
            threshold->endGesture();
            dragging = false;
            return;
        }

        if (onClick != nullptr)
            onClick();
    }

private:
    void timerCallback() override
    {
        // Peak-style ballistics: jump up instantly, fall at a readable rate.
        const float now = keyDb.load();
        levelDb = now > levelDb ? now : juce::jmax (now, levelDb - releasePerTick);
        repaint();
    }

    juce::Rectangle<float> scaleArea() const
    {
        return getLocalBounds().toFloat().reduced (2.0f).withTrimmedBottom (13.0f);
    }

    float dbToY (juce::Rectangle<float> r, float db) const
    {
        return r.getBottom() - r.getHeight() * (juce::jlimit (floorDb, 0.0f, db) - floorDb) / -floorDb;
    }

    float yToDb (juce::Rectangle<float> r, float y) const
    {
        return floorDb
             + juce::jlimit (0.0f, 1.0f, (r.getBottom() - y) / juce::jmax (1.0f, r.getHeight())) * -floorDb;
    }

    static constexpr float floorDb        = -80.0f;
    static constexpr float releasePerTick = 1.4f;   // ~42 dB/s at 30 Hz

    // The scale's waypoint between violet and orange — the "yellow" of this face's meter — and
    // the ramp's own red for the gate's pressure.
    inline static const juce::Colour transition { 0xffc862b4 };
    inline static const juce::Colour gateRed    { 0xffe0503c };

    const std::atomic<float>& keyDb;
    const std::atomic<float>& pressureDb;
    juce::RangedAudioParameter& param;
    std::unique_ptr<juce::ParameterAttachment> threshold;

    float levelDb = -90.0f;
    bool  dragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GateStrip)
};

} // namespace orbitamp
