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

    Two runners share the column, told apart by side and colour: the lilac threshold caret on the
    left, the orange INPUT TRIM caret on the right — its own ±24 scale, unity notched at the
    middle. A drag moves whichever runner is nearer to the grab; a clean click opens the zoom. */
class GateStrip final : public juce::Component,
                        private juce::Timer
{
public:
    GateStrip (const std::atomic<float>& keyDbSource, const std::atomic<float>& pressureDbSource,
               juce::RangedAudioParameter& thresholdParam, juce::RangedAudioParameter& trimParam,
               juce::RangedAudioParameter& gateOnParam, juce::RangedAudioParameter& decayParam)
        : keyDb (keyDbSource), pressureDb (pressureDbSource), param (thresholdParam), trimP (trimParam),
          onP (gateOnParam), decayP (decayParam)
    {
        threshold = std::make_unique<juce::ParameterAttachment> (thresholdParam,
                                                                 [this] (float) { repaint(); });
        trim = std::make_unique<juce::ParameterAttachment> (trimParam,
                                                            [this] (float) { repaint(); });
        onAtt = std::make_unique<juce::ParameterAttachment> (gateOnParam,
                                                             [this] (float) { repaint(); });
        decayAtt = std::make_unique<juce::ParameterAttachment> (decayParam, [] (float) {});
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

        // ---- peak hold: where the last phrase actually reached — the gain-staging line ----
        if (holdDb > floorDb + 0.5f)
        {
            g.setColour (theme::tx.withAlpha (0.9f));
            g.fillRect (inCol.getX(), dbToY (inCol, holdDb) - 0.75f, inCol.getWidth(), 1.5f);
        }

        // ---- the threshold: a line across the scale and a NAIL from the right — the right side
        //      is the gate's own territory, so its handle hangs from there ----
        {
            const float openDb  = param.convertFrom0to1 (param.getValue());
            const float openY   = dbToY (inCol, openDb);
            const float closeY  = dbToY (inCol, openDb - params::gateHysteresisDb);

            g.setColour (theme::lilac.withAlpha (0.45f));
            g.fillRect (inCol.getX(), closeY - 0.5f, inCol.getWidth(), 1.0f);

            g.setColour (theme::lilac);
            g.fillRect (inCol.getX(), openY - 1.0f, scaleArea().getWidth(), 2.0f);

            juce::Path nail;
            const float bx = r.getRight() - 1.0f;
            nail.addTriangle (bx, openY - 4.5f, bx, openY + 4.5f, bx - 7.0f, openY);
            g.fillPath (nail);
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

        // ---- the trim: a slide-rule cursor — a frame crawling the scale, ears past both sides,
        //      the meter showing through its window ----
        {
            const auto  area = scaleArea();
            const float ty   = trimY (area, trimP.convertFrom0to1 (trimP.getValue()));

            // The unity notch — the frame's home.
            g.setColour (theme::orange.withAlpha (0.5f));
            g.fillRect (r.getX() + 0.5f, trimY (area, 0.0f) - 0.5f, 3.0f, 1.0f);
            g.fillRect (r.getRight() - 3.5f, trimY (area, 0.0f) - 0.5f, 3.0f, 1.0f);

            const auto frame = juce::Rectangle<float> (r.getX() + 0.75f, ty - 5.0f,
                                                       r.getWidth() - 1.5f, 10.0f);
            g.setColour (theme::orange);
            g.drawRoundedRectangle (frame, 2.0f, 1.6f);

            // The ears, thickened at both sides — the grip of the cursor.
            g.fillRect (frame.getX() - 0.5f,     ty - 3.5f, 2.5f, 7.0f);
            g.fillRect (frame.getRight() - 2.0f, ty - 3.5f, 2.5f, 7.0f);
        }

        g.setColour (theme::hair2);
        g.drawRoundedRectangle (r.reduced (0.5f), theme::radiusSm, 1.0f);

        g.setColour (theme::txDim);
        theme::drawTracked (g, "IN", r.withTrimmedTop (r.getHeight() - 13.0f),
                            theme::displayFont (8.0f), 0.10f, juce::Justification::centred);
    }

    //==============================================================================
    void mouseDown (const juce::MouseEvent& e) override
    {
        // Right-click: the gate presets — the whole gate in one pick for whoever never opens
        // the zoom. The release of that same button must NOT count as the click that opens the
        // lens, so it is swallowed whole.
        if (e.mods.isPopupMenu())
        {
            swallowUp = true;
            showPresetMenu (e.getScreenPosition());
            return;
        }

        swallowUp = false;
        dragging  = false;
        pressY    = e.position.y;
    }

    void showPresetMenu (juce::Point<int> screenPos)
    {
        const bool  isOn  = onP.getValue() > 0.5f;
        const float th    = param.convertFrom0to1 (param.getValue());
        const bool  metal = decayP.getValue() > 0.5f;

        const auto matches = [&] (float t, bool m)
        {
            return isOn && std::abs (th - t) < 0.5f && metal == m;
        };

        juce::PopupMenu m;
        m.addSectionHeader ("GATE");
        m.addItem (1, "OFF",         true, ! isOn);
        m.addItem (2, "SOFT   -60",  true, matches (-60.0f, false));
        m.addItem (3, "MEDIUM -50",  true, matches (-50.0f, false));
        m.addItem (4, "HARD   -40",  true, matches (-40.0f, true));

        // At the MOUSE, not at the component: a menu summoned from a sliver as tall as the panel
        // would otherwise land wherever the sliver ends.
        m.showMenuAsync (juce::PopupMenu::Options()
                             .withTargetScreenArea ({ screenPos.x, screenPos.y, 1, 1 }),
                         [safe = juce::Component::SafePointer<GateStrip> (this)] (int r)
                         {
                             if (safe != nullptr)
                                 safe->applyPreset (r);
                         });
    }

    void applyPreset (int choice)
    {
        if (choice == 0)
            return;

        if (choice == 1)
        {
            onAtt->setValueAsCompleteGesture (0.0f);
            return;
        }

        onAtt->setValueAsCompleteGesture (1.0f);
        threshold->setValueAsCompleteGesture (choice == 2 ? -60.0f : choice == 3 ? -50.0f : -40.0f);
        decayAtt->setValueAsCompleteGesture (choice == 4 ? 1.0f : 0.0f);   // HARD is the metal chop
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging && e.getDistanceFromDragStart() > 4)
        {
            dragging = true;

            // Whichever runner was nearer to the grab is the one that moves — on a sliver this
            // wide, the Y axis is the only aim anyone has.
            const auto  area = scaleArea();
            const float thY  = dbToY (area, param.convertFrom0to1 (param.getValue()));
            const float trY  = trimY (area, trimP.convertFrom0to1 (trimP.getValue()));

            grabbedTrim = std::abs (pressY - trY) < std::abs (pressY - thY);
            (grabbedTrim ? trim : threshold)->beginGesture();
        }

        if (dragging)
        {
            if (grabbedTrim)
                trim->setValueAsPartOfGesture (trimFromY (scaleArea(), e.position.y));
            else
                threshold->setValueAsPartOfGesture (yToDb (scaleArea(), e.position.y));
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (swallowUp)
        {
            swallowUp = false;
            return;
        }

        if (dragging)
        {
            (grabbedTrim ? trim : threshold)->endGesture();
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

        // The hold line: grabs every new maximum, keeps it steady for ~2 s, then lets go slowly —
        // long enough to read after the phrase, never stale enough to lie about the next one.
        if (now >= holdDb)
        {
            holdDb   = now;
            holdAge  = 0;
        }
        else if (++holdAge > holdTicks)
        {
            holdDb = juce::jmax (floorDb, holdDb - holdReleasePerTick);
        }

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
    static constexpr float releasePerTick     = 1.4f;   // ~42 dB/s at 30 Hz
    static constexpr int   holdTicks          = 60;     // 2 s of steady hold...
    static constexpr float holdReleasePerTick = 0.8f;   // ...then ~24 dB/s down

    // The scale's waypoint between violet and orange — the "yellow" of this face's meter — and
    // the ramp's own red for the gate's pressure.
    inline static const juce::Colour transition { 0xffc862b4 };
    inline static const juce::Colour gateRed    { 0xffe0503c };

    const std::atomic<float>& keyDb;
    const std::atomic<float>& pressureDb;
    juce::RangedAudioParameter& param;
    juce::RangedAudioParameter& trimP;
    juce::RangedAudioParameter& onP;
    juce::RangedAudioParameter& decayP;
    std::unique_ptr<juce::ParameterAttachment> threshold, trim, onAtt, decayAtt;

    float levelDb = -90.0f;
    float holdDb  = -90.0f;
    int   holdAge = 0;
    bool  dragging    = false;
    bool  grabbedTrim = false;
    bool  swallowUp   = false;
    float pressY      = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GateStrip)
};

} // namespace orbitamp
