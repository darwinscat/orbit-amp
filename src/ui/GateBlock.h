#pragma once

#include "../Parameters.h"
#include "BlockFrame.h"
#include "Knob.h"

#include <atomic>

namespace orbitamp
{

/** The noise gate, zoomed, indicated the way gates are: a live KEY-LEVEL meter with two runners
    on it — where the gate OPENS (the threshold, draggable) and where it CLOSES (the engine's
    hysteresis under it). The signal runs along the same scale the decision is made on, so setting
    the threshold is watching the level cross it. Pressure — how hard the gate is holding the
    signal down — is its own well at the side, because attenuation and key level are different
    facts that happen to share a unit.

    The threshold is the gate's one decision; everything else about the feel stays fixed in the
    engine, and a knob per fixed opinion would be nine knobs of noise. */
class GateBlock final : public BlockFrame,
                        private juce::Timer
{
public:
    GateBlock (juce::AudioProcessorValueTreeState& s,
               const std::atomic<float>& pressureDbSource, const std::atomic<float>& keyDbSource)
        : BlockFrame ("Gate", Kind::dsp), pressureDb (pressureDbSource), keyDb (keyDbSource)
    {
        threshold.textForValue = [] (double v) { return juce::String (juce::roundToInt (v)) + " dB"; };
        addAndMakeVisible (threshold);

        thresholdAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            s, params::gateThreshold, threshold);

        // The runner writes the same parameter the knob does; the attachments keep both true.
        runnerAtt = std::make_unique<juce::ParameterAttachment> (
            *s.getParameter (params::gateThreshold), [this] (float) { repaint(); });

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

    void timerCallback() override
    {
        // Peak-style ballistics for the level: jump up instantly, fall at a readable rate.
        const float now = keyDb.load();
        levelDb = now > levelDb ? now : juce::jmax (now, levelDb - releasePerTick);
        repaint();
    }

    void layOutContent (juce::Rectangle<int> area) override
    {
        pressureWell = area.removeFromRight (area.getWidth() / 5).reduced (area.getWidth() / 14, 8);

        keyMeter = area.removeFromBottom (64).reduced (18, 10);
        area.removeFromBottom (8);

        const int side = juce::jmin (200, juce::jmin (area.getWidth(), area.getHeight()));
        threshold.setBounds (area.withSizeKeepingCentre (side, side));
    }

    float dbToX (juce::Rectangle<float> r, float db) const
    {
        return r.getX() + r.getWidth() * (juce::jlimit (floorDb, 0.0f, db) - floorDb) / -floorDb;
    }

    float xToDb (juce::Rectangle<float> r, float x) const
    {
        return floorDb + juce::jlimit (0.0f, 1.0f, (x - r.getX()) / juce::jmax (1.0f, r.getWidth())) * -floorDb;
    }

    void paintContent (juce::Graphics& g) override
    {
        const bool closed = pressureDb.load() < -1.0f;

        // ---- the key meter: the level the gate decides by, with both decision marks on it ----
        {
            const auto r = keyMeter.toFloat();

            g.setColour (theme::bezel);
            g.fillRoundedRectangle (r, theme::radiusMd);

            g.setColour (theme::hair);
            for (float db = -60.0f; db < -0.5f; db += 20.0f)
                g.fillRect (dbToX (r, db), r.getY() + 4.0f, 1.0f, r.getHeight() - 8.0f);

            const float lx = dbToX (r, levelDb);
            if (lx > r.getX() + 1.0f)
            {
                // Dim while the gate is holding: the level is still THERE — the gate is a VCA,
                // not a truth about the input — but the face says it is not getting through.
                g.setColour (theme::violet.withAlpha (closed ? 0.30f : 0.75f));
                g.fillRoundedRectangle (r.withRight (lx).reduced (2.0f), theme::radiusSm);
            }

            const float openDb  = (float) threshold.getValue();
            const float openX   = dbToX (r, openDb);
            const float closeX  = dbToX (r, openDb - params::gateHysteresisDb);

            // The CLOSE mark first, under the OPEN one — dimmer, because it is derived.
            g.setColour (theme::lilac.withAlpha (0.45f));
            g.fillRect (closeX - 0.75f, r.getY(), 1.5f, r.getHeight());

            g.setColour (theme::lilac);
            g.fillRect (openX - 1.0f, r.getY(), 2.0f, r.getHeight());

            // The runner's grab handle, riding the top edge.
            juce::Path caret;
            caret.addTriangle (openX - 5.0f, r.getY() - 1.0f, openX + 5.0f, r.getY() - 1.0f,
                               openX, r.getY() + 7.0f);
            g.fillPath (caret);

            g.setColour (theme::hair2);
            g.drawRoundedRectangle (r.reduced (0.5f), theme::radiusMd, 1.0f);

            g.setColour (theme::txFaint);
            theme::drawTracked (g, "KEY", r.withY (r.getBottom() + 2.0f).withHeight (10.0f),
                                theme::displayFont (6.5f), 0.14f, juce::Justification::centredLeft);
        }

        // ---- the pressure well: how hard the gate is holding, from the top down ----
        {
            const auto r = pressureWell.toFloat();

            g.setColour (theme::bezel);
            g.fillRoundedRectangle (r, theme::radiusMd);

            const float depthDb = juce::jlimit (0.0f, -floorDb, -pressureDb.load());
            const float h = r.getHeight() * depthDb / -floorDb;

            if (h > 0.5f)
            {
                g.setColour (theme::violet.withAlpha (0.85f));
                g.fillRoundedRectangle (r.withHeight (h), theme::radiusSm);
            }

            g.setColour (theme::hair2);
            g.drawRoundedRectangle (r.reduced (0.5f), theme::radiusMd, 1.0f);

            g.setColour (theme::txFaint);
            theme::drawTracked (g, "GR", r.withY (r.getBottom() + 2.0f).withHeight (10.0f),
                                theme::displayFont (6.5f), 0.14f, juce::Justification::centred);
        }
    }

    //==============================================================================
    // The open runner IS a control: grab it — or anywhere on the meter — and the threshold
    // follows, as one undoable move.
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (keyMeter.contains (e.getPosition()))
        {
            runnerAtt->beginGesture();
            draggingRunner = true;
            dragRunner (e);
            return;
        }

        BlockFrame::mouseDown (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (draggingRunner)
            dragRunner (e);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (draggingRunner)
        {
            runnerAtt->endGesture();
            draggingRunner = false;
        }
    }

    void dragRunner (const juce::MouseEvent& e)
    {
        runnerAtt->setValueAsPartOfGesture (xToDb (keyMeter.toFloat(), e.position.x));
    }

    static constexpr float floorDb        = -80.0f;   // the meter's left edge — the threshold's own floor
    static constexpr float releasePerTick = 1.4f;     // ~42 dB/s at 30 Hz: readable, not sluggish

    const std::atomic<float>& pressureDb;
    const std::atomic<float>& keyDb;
    float levelDb = -90.0f;

    Knob threshold { "Threshold", theme::violet, 0 };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> thresholdAtt;
    std::unique_ptr<juce::ParameterAttachment> runnerAtt;
    bool draggingRunner = false;

    juce::Rectangle<int> keyMeter, pressureWell;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GateBlock)
};

} // namespace orbitamp
