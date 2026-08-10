#pragma once

#include "../Parameters.h"
#include "BlockFrame.h"
#include "Knob.h"
#include "StepSwitch.h"

#include <atomic>
#include <functional>

namespace orbitamp
{

/** The noise gate, zoomed — and SIZED like a zoom: the whole panel is this block's, so every
    control is at reading size. The tightness the strip lives with has no business here.

    Indicated the way gates are: a live KEY-LEVEL meter with two runners on it — where the gate
    OPENS (the threshold, draggable) and where it CLOSES (the engine's hysteresis under it). The
    signal runs along the same scale the decision is made on, so setting the threshold is watching
    the level cross it. Pressure — how hard the gate is holding the signal down — is its own well
    at the side, because attenuation and key level are different facts that share a unit.

    Three decisions and no more: the threshold (knob, runner, or LEARN — which measures your noise
    floor instead of asking you to guess it), the DECAY way (normal / metal chop), and WHERE the
    mute lands. The key never moves — it is always the raw input. */
class GateBlock final : public BlockFrame,
                        private juce::Timer
{
public:
    GateBlock (juce::AudioProcessorValueTreeState& s,
               const std::atomic<float>& pressureDbSource, const std::atomic<float>& keyDbSource,
               std::function<bool()> demoIsPlaying)
        : BlockFrame ("Gate", Kind::dsp), pressureDb (pressureDbSource), keyDb (keyDbSource),
          demoPlaying (std::move (demoIsPlaying))
    {
        // The hero: its NAME is part of the face at this size.
        threshold.textForValue    = [] (double v) { return juce::String (juce::roundToInt (v)) + " dB"; };
        threshold.labelFontHeight = 22.0f;
        threshold.labelRowHeight  = 30;
        addAndMakeVisible (threshold);

        thresholdAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            s, params::gateThreshold, threshold);

        // The runner writes the same parameter the knob does; the attachments keep both true.
        runnerAtt = std::make_unique<juce::ParameterAttachment> (
            *s.getParameter (params::gateThreshold), [this] (float) { repaint(); });

        // Both switches read at reading size, in text-on-fill contrast — violet on violet is how
        // the first pass of this face became illegible.
        const auto readable = [] (StepSwitch& sw)
        {
            sw.accent       = theme::violet;
            sw.fontHeight   = 12.0f;
            sw.highContrast = true;
        };

        // How the gate lets go: a note's natural die-away, or the chop.
        readable (decayMode);
        addAndMakeVisible (decayMode);
        decayMode.setItems ({ "NORMAL", "METAL" }, 0);

        decayAtt = std::make_unique<juce::ParameterAttachment> (
            *s.getParameter (params::gateDecay),
            [this] (float v) { decayMode.setSelectedIndex (v > 0.5f ? 1 : 0, juce::dontSendNotification); });
        decayMode.onChange = [this] (int i) { decayAtt->setValueAsCompleteGesture ((float) i); };
        decayAtt->sendInitialUpdate();

        // Where the mute lands. The key is not a choice, so the switch only says WHERE.
        readable (mutePos);
        addAndMakeVisible (mutePos);
        mutePos.setItems ({ "AT START", "PRE-REVERB" }, 1);

        mutePosAtt = std::make_unique<juce::ParameterAttachment> (
            *s.getParameter (params::gatePos),
            [this] (float v) { mutePos.setSelectedIndex (v > 0.5f ? 1 : 0, juce::dontSendNotification); });
        mutePos.onChange = [this] (int i) { mutePosAtt->setValueAsCompleteGesture ((float) i); };
        mutePosAtt->sendInitialUpdate();

        // LEARN: the threshold is a fact about YOUR noise floor, and facts are measured. Don't
        // play; the button listens to the key for a moment and parks the threshold just over the
        // loudest thing it heard.
        addAndMakeVisible (learn);
        learn.onClick = [this] { startLearning(); };
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

        learn.lit = learning;

        if (learning)
        {
            learnPeak = juce::jmax (learnPeak, now);

            if (++learnTicks >= learnTotalTicks)
            {
                learning = false;

                // Nothing arrived: a muted input teaches nothing, and parking the threshold at
                // the floor would be pretending it did.
                if (learnPeak < -75.0f)
                {
                    say ("NOTHING HEARD");
                }
                else
                {
                    // The full hysteresis over the measured floor: the floor then sits under the
                    // CLOSE level too, with the same margin.
                    const float th = juce::jlimit (-80.0f, -20.0f,
                                                   learnPeak + params::gateHysteresisDb);
                    runnerAtt->setValueAsCompleteGesture (th);
                    say ("SET " + juce::String (juce::roundToInt (th)) + " DB");
                }
            }
        }

        if (messageTicks > 0)
            --messageTicks;

        repaint();
    }

    void startLearning()
    {
        if (learning)
            return;

        // The demo replaces the input — learning from it would file music under noise.
        if (demoPlaying != nullptr && demoPlaying())
        {
            say ("STOP THE DEMO FIRST");
            return;
        }

        learning   = true;
        learnPeak  = -90.0f;
        learnTicks = 0;
    }

    void say (const juce::String& text)
    {
        message      = text;
        messageTicks = 90;   // ~3 s at 30 Hz
    }

    void layOutContent (juce::Rectangle<int> area) override
    {
        // A fifth of the face, and its name lives inside it — a well is its own label plate.
        pressureWell = area.removeFromRight (area.getWidth() / 5).reduced (10, 12);
        area.removeFromRight (4);

        // The key meter along the bottom, generous: it is the face's instrument panel.
        keyMeter = area.removeFromBottom (72).reduced (12, 10);

        // The controls row belongs to the two labeled ways alone — LEARN moves up beside the
        // hero, where the face has a whole empty flank to give it.
        auto row = area.removeFromBottom (72).reduced (12, 6);
        decayArea = row.removeFromLeft ((row.getWidth() - rowGap) * 2 / 5);
        row.removeFromLeft (rowGap);
        muteArea = row;
        decayMode.setBounds (decayArea.withTrimmedTop (headingRow));
        mutePos.setBounds (muteArea.withTrimmedTop (headingRow));

        area.removeFromBottom (6);

        // The hero in the middle, name included; LEARN sized like the button it is, centred on
        // the left flank the knob does not use.
        const int side = juce::jmin (230, juce::jmin (area.getWidth(), area.getHeight()));
        const auto hero = area.withSizeKeepingCentre (side, side);
        threshold.setBounds (hero);

        auto flank = area.withRight (hero.getX()).reduced (16, 0);
        learn.setBounds (flank.withSizeKeepingCentre (juce::jmin (180, flank.getWidth()), 48));
    }

    float dbToX (juce::Rectangle<float> r, float db) const
    {
        return r.getX() + r.getWidth() * (juce::jlimit (floorDb, 0.0f, db) - floorDb) / -floorDb;
    }

    float xToDb (juce::Rectangle<float> r, float x) const
    {
        return floorDb + juce::jlimit (0.0f, 1.0f, (x - r.getX()) / juce::jmax (1.0f, r.getWidth())) * -floorDb;
    }

    void heading (juce::Graphics& g, juce::Rectangle<int> box, const juce::String& text)
    {
        g.setColour (theme::txDim);
        theme::drawTracked (g, text, box.withHeight (headingRow).toFloat(),
                            theme::displayFont (10.0f), 0.14f, juce::Justification::centredLeft);
    }

    void paintContent (juce::Graphics& g) override
    {
        const bool closed = pressureDb.load() < -1.0f;

        heading (g, decayArea, "DECAY");
        heading (g, muteArea,  "MUTE POSITION");

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

            // While learning, the running maximum walks the scale in the tuner's green — the
            // threshold-to-be, being measured.
            if (learning)
            {
                g.setColour (juce::Colour (0xff5fc97a));
                g.fillRect (dbToX (r, learnPeak) - 0.75f, r.getY(), 1.5f, r.getHeight());
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
            caret.addTriangle (openX - 6.0f, r.getY() - 1.0f, openX + 6.0f, r.getY() - 1.0f,
                               openX, r.getY() + 8.0f);
            g.fillPath (caret);

            g.setColour (theme::hair2);
            g.drawRoundedRectangle (r.reduced (0.5f), theme::radiusMd, 1.0f);

            // The name lives INSIDE the well, on whatever is behind it.
            const auto inner = r.reduced (10.0f, 6.0f);
            g.setColour (theme::tx.withAlpha (0.8f));
            theme::drawTracked (g, "KEY", inner.withTrimmedTop (inner.getHeight() - 12.0f),
                                theme::displayFont (9.5f), 0.14f, juce::Justification::bottomLeft);

            // What LEARN has to say — a measurement's verdict, or why there is none.
            if (messageTicks > 0)
            {
                g.setColour (theme::tx);
                theme::drawTracked (g, message, inner.withTrimmedTop (inner.getHeight() - 12.0f),
                                    theme::displayFont (9.5f), 0.14f, juce::Justification::bottomRight);
            }
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

            // Inside, over fill or bezel alike — the well is its own label plate.
            g.setColour (theme::tx.withAlpha (0.85f));
            theme::drawTracked (g, "GR", r.reduced (0.0f, 8.0f),
                                theme::displayFont (11.0f), 0.14f, juce::Justification::centredBottom);
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

    //==============================================================================
    /** The one momentary control on the face — a BUTTON, sized like one, in the capture orange
        that says "this one acts". Lit while the measurement runs. */
    struct LearnButton final : public juce::Component
    {
        std::function<void()> onClick;
        bool lit = false;

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            const float corner = 8.0f;

            g.setColour (lit ? theme::orange : theme::orange.withAlpha (over ? 0.35f : 0.22f));
            g.fillRoundedRectangle (r, corner);
            g.setColour (theme::orange);
            g.drawRoundedRectangle (r, corner, 1.5f);

            g.setColour (lit ? juce::Colour (0xff17120c) : theme::tx);
            theme::drawTracked (g, lit ? "LISTENING" : "LEARN", r, theme::displayFont (13.0f), 0.14f,
                                juce::Justification::centred);
        }

        void mouseEnter (const juce::MouseEvent&) override { over = true;  repaint(); }
        void mouseExit  (const juce::MouseEvent&) override { over = false; repaint(); }

        void mouseDown (const juce::MouseEvent&) override
        {
            if (onClick != nullptr)
                onClick();
        }

    private:
        bool over = false;
    };

    static constexpr float floorDb        = -80.0f;   // the meter's left edge — the threshold's own floor
    static constexpr float releasePerTick = 1.4f;     // ~42 dB/s at 30 Hz: readable, not sluggish
    static constexpr int   learnTotalTicks = 45;      // 1.5 s at 30 Hz — enough for hum to show its worst
    static constexpr int   headingRow     = 20;
    static constexpr int   rowGap         = 28;

    const std::atomic<float>& pressureDb;
    const std::atomic<float>& keyDb;
    std::function<bool()> demoPlaying;
    float levelDb = -90.0f;

    Knob threshold { "Threshold", theme::violet, 0 };
    StepSwitch decayMode, mutePos;
    LearnButton learn;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> thresholdAtt;
    std::unique_ptr<juce::ParameterAttachment> runnerAtt, decayAtt, mutePosAtt;
    bool draggingRunner = false;

    bool  learning   = false;
    float learnPeak  = -90.0f;
    int   learnTicks = 0;

    juce::String message;
    int messageTicks = 0;

    juce::Rectangle<int> keyMeter, pressureWell, decayArea, muteArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GateBlock)
};

} // namespace orbitamp
