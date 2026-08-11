#pragma once

#include "../Parameters.h"
#include "BlockFrame.h"
#include "MeterRail.h"
#include "Knob.h"
#include "StepSwitch.h"

#include <atomic>
#include <functional>

namespace orbitamp
{

/** The noise gate, zoomed — and SIZED like a zoom: the whole panel is this block's, so every
    control is at reading size.

    The face has three regions, golden-ratio split: the control column (LEARN over DECAY over
    MUTE POSITION, one width), the hero threshold knob in the column's complement, and the two
    METER COLUMNS on the right — KEY and GR side by side, named in letters sized to their width.
    KEY is the level the gate decides by, drawn on the scale the decision is made on: the OPEN
    runner rides it (draggable), the CLOSE mark sits the engine's hysteresis under it, and the
    fill dims while the gate holds. GR is how hard the gate is pressing, top down.

    LEARN measures the noise floor instead of asking you to guess it: it listens for a moment —
    ignoring the edges of its own window, where the button click itself lives — and parks the
    threshold the full hysteresis over the loudest thing it heard. Progress runs across the
    button itself. */
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

        // Reading size, text-on-fill contrast — violet on violet is how the first pass of this
        // face became illegible.
        const auto readable = [] (StepSwitch& sw)
        {
            sw.accent       = theme::violet;
            sw.fontHeight   = 14.0f;
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

        addAndMakeVisible (learn);
        learn.onClick = [this] { startLearning(); };

        // Lost once in a whole-file rewrite, and the frame's switch became a free-floating local
        // toggle — flipping pixels while the parameter, the thumb and the engine lived a
        // different life. The strip's caption was honest the whole time; this face lied.
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

        // Switched off mid-measurement: the block is read-only now, so the measurement dies
        // rather than writing a threshold into a dark block.
        if (learning && ! isBlockOn())
        {
            learning = false;
            learn.trace.clear();
        }

        learn.lit = learning;

        if (learning)
        {
            ++learnTicks;
            learn.trace.push_back (now);   // the waveform on the button IS the progress

            // The window's edges are thrown away: the button click itself, and the hand leaving
            // the mouse, are not the noise floor.
            if (learnTicks > learnEdgeTicks && learnTicks <= learnTotalTicks - learnEdgeTicks)
            {
                learnPeak    = juce::jmax (learnPeak, now);
                learn.peakDb = learnPeak;
            }

            if (learnTicks >= learnTotalTicks)
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
                    const float th = juce::jlimit (-80.0f, -10.0f,
                                                   learnPeak + params::gateHysteresisDb);
                    runnerAtt->setValueAsCompleteGesture (th);
                    say ("SET " + juce::String (juce::roundToInt (th)) + " DB");
                }
            }
        }

        if (messageTicks > 0 && --messageTicks == 0)
        {
            learn.message.clear();
            learn.trace.clear();   // the verdict took its record with it
        }

        repaint();
    }

    void startLearning()
    {
        if (learning)
            return;

        // The demo replaces the input — learning from it would file music under noise.
        if (demoPlaying != nullptr && demoPlaying())
        {
            say ("STOP THE DEMO");
            return;
        }

        learning   = true;
        learnPeak  = -90.0f;
        learnTicks = 0;

        learn.trace.clear();
        learn.trace.reserve ((size_t) learnTotalTicks);
        learn.totalTicks = learnTotalTicks;
        learn.edgeTicks  = learnEdgeTicks;
        learn.peakDb     = -90.0f;
    }

    /** The verdict lands ON the button — the one place the eye already is. */
    void say (const juce::String& text)
    {
        learn.message = text;
        messageTicks  = 90;   // ~3 s at 30 Hz
    }

    void layOutContent (juce::Rectangle<int> area) override
    {
        // The meter columns flank the face the way the signal flows: KEY — what comes IN, the
        // decision's own scale — on the left, GR — what the gate is doing to it — on the right.
        // Each wide enough that its NAME can be set in real letters.
        const int wellW = juce::jmin (96, area.getWidth() / 6);
        keyWell = area.removeFromLeft (wellW).reduced (0, 4);
        area.removeFromLeft (18);
        pressureWell = area.removeFromRight (wellW).reduced (0, 4);
        area.removeFromRight (18);

        // The hero on top, every button UNDER it: LEARN over DECAY over MUTE POSITION, one
        // width — the golden share of the face — centred beneath the knob.
        const int stackH = learnH + stackGap + headingRow + switchH + stackGap + headingRow + switchH;
        auto stack = area.removeFromBottom (stackH)
                         .withSizeKeepingCentre (juce::roundToInt ((float) area.getWidth() / goldenRatio),
                                                 stackH);

        learn.setBounds (stack.removeFromTop (learnH));
        stack.removeFromTop (stackGap);
        decayArea = stack.removeFromTop (headingRow + switchH);
        decayMode.setBounds (decayArea.withTrimmedTop (headingRow));
        stack.removeFromTop (stackGap);
        muteArea = stack.removeFromTop (headingRow + switchH);
        mutePos.setBounds (muteArea.withTrimmedTop (headingRow));

        area.removeFromBottom (columnGap);

        // What is left above belongs to the hero, name included.
        const int side = juce::jmin (area.getWidth(), area.getHeight());
        threshold.setBounds (area.withSizeKeepingCentre (side, side));
    }

    float dbToY (juce::Rectangle<float> r, float db) const
    {
        return r.getBottom() - r.getHeight() * (juce::jlimit (floorDb, 0.0f, db) - floorDb) / -floorDb;
    }

    float yToDb (juce::Rectangle<float> r, float y) const
    {
        return floorDb + juce::jlimit (0.0f, 1.0f, (r.getBottom() - y) / juce::jmax (1.0f, r.getHeight())) * -floorDb;
    }

    void heading (juce::Graphics& g, juce::Rectangle<int> box, const juce::String& text)
    {
        g.setColour (theme::txDim);
        theme::drawTracked (g, text, box.withHeight (headingRow).toFloat(),
                            theme::displayFont (12.0f), 0.14f, juce::Justification::centredLeft);
    }

    /** The wells' letters, sized by the wells: as big as "KEY" fits, and GR in the same font so
        the pair reads as one instrument. */
    float wellLabelFont() const
    {
        const float atTen = theme::trackedWidth ("KEY", theme::displayFont (10.0f), 0.10f);
        return juce::jlimit (12.0f, 30.0f, 10.0f * ((float) keyWell.getWidth() - 12.0f) / atTen);
    }

    void paintContent (juce::Graphics& g) override
    {
        const float labelFont = wellLabelFont();

        heading (g, decayArea, "DECAY");
        heading (g, muteArea,  "MUTE POSITION");

        // ---- KEY: the level the gate decides by, with both decision marks on it ----
        {
            const auto r = keyWell.toFloat();

            g.setColour (theme::bezel);
            g.fillRoundedRectangle (r, theme::radiusMd);

            g.setColour (theme::hair);
            for (float db = -60.0f; db < -0.5f; db += 20.0f)
                g.fillRect (r.getX() + 4.0f, dbToY (r, db), r.getWidth() - 8.0f, 1.0f);

            // The family rail, same as the sliver: dB-anchored violet-to-orange, and the gate's
            // hold told by DRAINING THE COLOUR — the level is still THERE (the gate is a VCA,
            // not a truth about the input), but it goes grey while it is not getting through.
            meterrail::paintFill (g, r.reduced (2.0f), dbToY (r, levelDb),
                                  juce::jlimit (0.0f, 1.0f, -pressureDb.load() / 40.0f));

            // While learning, the running maximum walks the scale in the tuner's green — the
            // threshold-to-be, being measured.
            if (learning)
            {
                g.setColour (juce::Colour (0xff5fc97a));
                g.fillRect (r.getX(), dbToY (r, learnPeak) - 0.75f, r.getWidth(), 1.5f);
            }

            const float openDb = (float) threshold.getValue();
            const float openY  = dbToY (r, openDb);
            const float closeY = dbToY (r, openDb - params::gateHysteresisDb);

            // The CLOSE mark first, under the OPEN one — dimmer, because it is derived.
            g.setColour (theme::lilac.withAlpha (0.45f));
            g.fillRect (r.getX(), closeY - 0.75f, r.getWidth(), 1.5f);

            g.setColour (theme::lilac);
            g.fillRect (r.getX(), openY - 1.0f, r.getWidth(), 2.0f);

            // The runner's grab handle, riding the left edge.
            juce::Path caret;
            caret.addTriangle (r.getX() - 1.0f, openY - 6.0f, r.getX() - 1.0f, openY + 6.0f,
                               r.getX() + 8.0f, openY);
            g.fillPath (caret);

            g.setColour (theme::hair2);
            g.drawRoundedRectangle (r.reduced (0.5f), theme::radiusMd, 1.0f);

            g.setColour (theme::tx);
            theme::drawTracked (g, "KEY", r.withTrimmedBottom (10.0f),
                                theme::displayFont (labelFont), 0.10f,
                                juce::Justification::centredBottom);
        }

        // ---- GR: how hard the gate is holding, from the top down ----
        {
            const auto r = pressureWell.toFloat();

            g.setColour (theme::bezel);
            g.fillRoundedRectangle (r, theme::radiusMd);

            const float depthDb = juce::jlimit (0.0f, -floorDb, -pressureDb.load());
            const float h = r.getHeight() * depthDb / -floorDb;

            if (h > 0.5f)
            {
                // Orange, not violet: pressure is the gate ACTING, and on this face orange is the
                // colour of action — the same one LEARN wears.
                g.setColour (theme::orange.withAlpha (0.85f));
                g.fillRoundedRectangle (r.withHeight (h), theme::radiusSm);
            }

            g.setColour (theme::hair2);
            g.drawRoundedRectangle (r.reduced (0.5f), theme::radiusMd, 1.0f);

            g.setColour (theme::tx);
            theme::drawTracked (g, "GR", r.withTrimmedBottom (10.0f),
                                theme::displayFont (labelFont), 0.10f,
                                juce::Justification::centredBottom);
        }
    }

    //==============================================================================
    // The open runner IS a control: grab it — or anywhere on the KEY column — and the threshold
    // follows, as one undoable move.
    void mouseDown (const juce::MouseEvent& e) override
    {
        // Read-only while the block is off, and right-clicks belong to the frame's power menu —
        // the runner only answers a live left-button grab.
        if (isBlockOn() && ! e.mods.isPopupMenu() && keyWell.contains (e.getPosition()))
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
        // The power check repeats per event: a drag that started on a live block must not keep
        // writing after the block goes dark under it.
        if (draggingRunner && isBlockOn())
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
        runnerAtt->setValueAsPartOfGesture (yToDb (keyWell.toFloat(), e.position.y));
    }

    //==============================================================================
    /** The one momentary control on the face — a BUTTON, sized like one, in the capture orange
        that says "this one acts". While it listens, what it HEARS runs across it: the key level
        as a violet waveform on a dB scale — logarithmic, because on a linear one a -70 dB noise
        floor is three pixels of nothing and the whole point is SEEING the floor. Its growth is
        the progress; the window's thrown-away edges draw dimmer; the measured peak is the green
        line. The verdict lands on the button too, over the trace that produced it. */
    struct LearnButton final : public juce::Component
    {
        std::function<void()> onClick;
        bool  lit = false;
        juce::String message;

        std::vector<float> trace;         // key dB per tick, the measurement's own record
        int   totalTicks = 1;
        int   edgeTicks  = 0;
        float peakDb     = -90.0f;

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            const float corner = 8.0f;

            g.setColour (theme::orange.withAlpha (over && ! lit ? 0.35f : 0.22f));
            g.fillRoundedRectangle (r, corner);

            if (! trace.empty())
            {
                const juce::Graphics::ScopedSaveState clipped (g);
                juce::Path p;
                p.addRoundedRectangle (r, corner);
                g.reduceClipRegion (p);

                // Mirrored around the centre line, half-height by dB: a waveform of the floor.
                const auto halfH = [&r] (float db)
                {
                    return (juce::jlimit (traceFloorDb, 0.0f, db) - traceFloorDb) / -traceFloorDb
                         * (r.getHeight() * 0.5f - 2.0f);
                };

                const float cy   = r.getCentreY();
                const float step = r.getWidth() / (float) totalTicks;

                for (int i = 0; i < (int) trace.size(); ++i)
                {
                    const bool counted = i >= edgeTicks && i < totalTicks - edgeTicks;
                    const float h = juce::jmax (0.75f, halfH (trace[(size_t) i]));

                    g.setColour (theme::violet.withAlpha (counted ? 0.9f : 0.35f));
                    g.fillRect (r.getX() + step * (float) i, cy - h, juce::jmax (1.0f, step - 0.5f), h * 2.0f);
                }

                // The measured floor itself — the number the verdict is made of.
                if (peakDb > traceFloorDb + 0.5f)
                {
                    g.setColour (juce::Colour (0xff5fc97a));
                    g.fillRect (r.getX() + step * (float) edgeTicks, cy - halfH (peakDb) - 0.75f,
                                step * (float) juce::jmax (1, (int) trace.size() - edgeTicks * 2), 1.5f);
                }
            }

            g.setColour (theme::orange);
            g.drawRoundedRectangle (r, corner, 1.5f);

            const auto text = message.isNotEmpty() ? message : juce::String (lit ? "LISTENING" : "LEARN");
            g.setColour (theme::tx);
            theme::drawTracked (g, text, r, theme::displayFont (15.0f), 0.14f,
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
        static constexpr float traceFloorDb = -80.0f;   // the KEY scale's floor, same story

        bool over = false;
    };

    static constexpr float goldenRatio    = 1.618f;
    static constexpr float floorDb        = -80.0f;   // the KEY scale's bottom — the threshold's own floor
    static constexpr float releasePerTick = 1.4f;     // ~42 dB/s at 30 Hz: readable, not sluggish
    static constexpr int   learnTotalTicks = 90;      // 3 s at 30 Hz
    static constexpr int   learnEdgeTicks  = 15;      // half a second each end, thrown away
    static constexpr int   headingRow     = 24;
    static constexpr int   learnH         = 54;
    static constexpr int   switchH        = 46;
    static constexpr int   stackGap       = 18;
    static constexpr int   columnGap      = 20;
    static constexpr int   wellGap        = 14;

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
    int   messageTicks = 0;

    juce::Rectangle<int> keyWell, pressureWell, decayArea, muteArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GateBlock)
};

} // namespace orbitamp
