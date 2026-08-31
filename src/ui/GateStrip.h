// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../Parameters.h"
#include "MeterRail.h"
#include "Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <vector>

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
               std::atomic<bool>& clipLatch,
               juce::RangedAudioParameter& thresholdParam, juce::RangedAudioParameter& trimParam,
               juce::RangedAudioParameter& gateOnParam, juce::RangedAudioParameter& decayParam,
               juce::RangedAudioParameter& posParam)
        : keyDb (keyDbSource), pressureDb (pressureDbSource), clip (clipLatch),
          param (thresholdParam), trimP (trimParam), onP (gateOnParam), decayP (decayParam),
          posP (posParam)
    {
        posAtt = std::make_unique<juce::ParameterAttachment> (posParam, [] (float) {});

        threshold = std::make_unique<juce::ParameterAttachment> (thresholdParam,
                                                                 [this] (float) { repaint(); });
        trim = std::make_unique<juce::ParameterAttachment> (trimParam,
                                                            [this] (float) { repaint(); });
        onAtt = std::make_unique<juce::ParameterAttachment> (gateOnParam,
                                                             [this] (float) { repaint(); });
        decayAtt = std::make_unique<juce::ParameterAttachment> (decayParam, [] (float) {});

        setRepaintsOnMouseActivity (true);   // the runners fade in under the mouse
        startTimerHz (30);
    }

    /** A clean click (no drag) opens the big gate — the sliver is the glance. */
    std::function<void()> onClick;

    /** The trim runner entered/left the hand — the editor slides the drag ruler out beside us. */
    std::function<void (bool)> onTrimDrag;

    /** The big LEARN overlay's hooks: measurement started; measurement spoke its verdict. */
    std::function<void()>             onLearnBegin;
    std::function<void (juce::String)> onLearnDone;

    const std::vector<float>& learnTraceRef() const { return learnTrace; }

    /** The threshold LEARN would set if it ended now — the overlay's live dashed line.
        -999 while the trusted window has heard nothing yet. */
    float learnPendingDb() const
    {
        if (! learning || learnPeak <= -119.0f)
            return -999.0f;

        return juce::jlimit (-80.0f, -10.0f, learnPeak + params::gateHysteresisDb);
    }

    static constexpr int learnTotalTicks = 90;     // 3 s at 30 Hz

    static constexpr int designWidth = 38;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();

        g.setColour (theme::bezel);
        g.fillRoundedRectangle (r, theme::radiusSm);

        const auto inCol = columnArea();

        // ---- IN: the family meter rail — dB-anchored, white hold, clip cap — wearing the whole
        //      thermometer (the GAIN dial's ramp on a pole): dark violet floor, corporate violet,
        //      the traffic middle, orange, red. The gate's pressure lives on the BADGE (red by
        //      depth); draining the colour here hit the eyes and retired. ----
        meterrail::paintFill (g, inCol, dbToY (inCol, levelDb), 0.0f, true);

        if (holdDb > floorDb + 0.5f)
            meterrail::paintHold (g, inCol, dbToY (inCol, holdDb));

        meterrail::paintClipCap (g, inCol, clip.load());

        // ---- the ghost: while the trim is in hand, the peak-hold's FUTURE — where the last
        //      phrase's peak will land with the new gain — walks the scale as a dashed line.
        //      This is what puts the runner IN the column's grid: you see the consequence. ----
        if (dragging && grabbedTrim)
        {
            const float delta = trimP.convertFrom0to1 (trimP.getValue()) - trimStartDb;
            if (holdAtGrab > floorDb + 0.5f && std::abs (delta) > 0.05f)
            {
                const float gy = dbToY (inCol, holdAtGrab + delta);
                const float dashes[] = { 4.0f, 3.0f };
                g.setColour (juce::Colours::white.withAlpha (0.55f));
                g.drawDashedLine ({ inCol.getX(), gy, inCol.getRight(), gy }, dashes, 2, 1.4f);
            }
        }

        // ---- learning: an orange fuse burning up the left edge — the measurement's progress ----
        if (learning)
        {
            const float frac = (float) learnTicks / (float) learnTotalTicks;
            g.setColour (theme::orange);
            g.fillRect (inCol.getX(), inCol.getBottom() - inCol.getHeight() * frac,
                        2.0f, inCol.getHeight() * frac);
        }

        // ---- the threshold: the same grip instrument as the trim, in the gate's violet — its
        //      number aboard, the derived CLOSE line the hysteresis under it. A switched-off
        //      gate is read-only here like everywhere: its runner dims and will not answer.
        // The runners live half-ghosted until the hand comes near: the column is a METER first,
        // its controls surface when wanted. (His own ask — the no-hover law covers lighting,
        // not decluttering.)
        const float hoverA = isMouseOverOrDragging (true) ? 1.0f : 0.3f;

        {
            const float dimmed  = (onP.getValue() > 0.5f ? 1.0f : 0.35f) * hoverA;
            const float openDb  = param.convertFrom0to1 (param.getValue());
            const float openY   = dbToY (inCol, openDb);
            const float closeY  = dbToY (inCol, openDb - params::gateHysteresisDb);

            g.setColour (theme::lilac.withAlpha (0.45f * dimmed));
            g.fillRect (inCol.getX(), closeY - 0.5f, inCol.getWidth(), 1.0f);

            meterrail::paintGrip (g, r, openY, juce::String (juce::roundToInt (openDb)),
                                  theme::lilac.withMultipliedAlpha (dimmed),
                                  dragging && ! grabbedTrim);
        }

        // ---- the trim: tabby's hollow sliding frame with its sight, riding the whole rail ----
        {
            const auto area = scaleArea();
            // No ticks on the window's edge — the marks live on the inner side only.
            meterrail::paintDbScale (g, r.reduced (0.0f, 2.0f),
                                     [&] (float db) { return dbToY (area, db); }, floorDb,
                                     false, true, dbToY (area, levelDb));
            meterrail::paintUnityNubs (g, r.reduced (0.0f, 2.0f), trimY (area, 0.0f), false, true);
            const float v = trimP.convertFrom0to1 (trimP.getValue());
            meterrail::paintGrip (g, r, trimY (area, v), meterrail::trimText (v),
                                  theme::orange.withMultipliedAlpha (hoverA),
                                  dragging && grabbedTrim);
        }

        meterrail::paintName (g, inCol, "IN");
    }

    //==============================================================================
    void mouseDown (const juce::MouseEvent& e) override
    {
        // A click anywhere while the grip editor is open COMMITS it first — the field convention.
        // Without this the abandoned editor sat at its old spot wearing a grip's face.
        if (gripEd.isOpen())
        {
            gripEd.takeAndHide();
            swallowUp = true;
            return;
        }

        // Right-click: the gate presets — the whole gate in one pick for whoever never opens
        // the zoom. The release of that same button must NOT count as the click that opens the
        // lens, so it is swallowed whole.
        if (e.mods.isPopupMenu())
        {
            swallowUp = true;
            showPresetMenu (e.getScreenPosition());
            return;
        }

        if (clip.load() && e.position.y <= scaleArea().getY() + 5.0f)
        {
            clip.store (false);
            swallowUp = true;
            repaint();
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
        m.addItem (5, "LEARN",       true, learning);

        juce::PopupMenu decay;
        decay.addItem (10, "NORMAL", true, ! metal);
        decay.addItem (11, "METAL",  true, metal);
        m.addSubMenu ("DECAY", decay);

        // WHERE the gate mutes. It always keys off the raw input; the VCA can stand at the front or
        // after the preamp. This lived on the gate's big face, and the big face is gone — a control
        // with no door is a control that does not exist, so it moves in here.
        const bool preReverb = posP.getValue() > 0.5f;

        juce::PopupMenu where;
        where.addItem (12, params::gatePositions[0].toUpperCase(), true, ! preReverb);
        where.addItem (13, params::gatePositions[1].toUpperCase(), true, preReverb);
        m.addSubMenu ("MUTES AT", where);

        m.addSeparator();
        m.addSectionHeader ("VOLUME");
        m.addItem (7, "RESET");

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

        // OFF is a TOGGLE, not a one-way door: picking it on a silent gate turns the gate on —
        // the item must be un-pickable or the menu can only ever kill.
        if (choice == 1)
        {
            onAtt->setValueAsCompleteGesture (onP.getValue() > 0.5f ? 0.0f : 1.0f);
            return;
        }

        if (choice == 12 || choice == 13)
        {
            posAtt->setValueAsCompleteGesture (choice == 13 ? 1.0f : 0.0f);
            return;
        }

        if (choice == 10 || choice == 11)
        {
            decayAtt->setValueAsCompleteGesture (choice == 11 ? 1.0f : 0.0f);
            return;
        }

        if (choice == 7)
        {
            trim->setValueAsCompleteGesture (0.0f);
            return;
        }

        if (choice == 5)
        {
            // The measurement the zoom's LEARN makes, from the sliver: stay quiet, the fuse burns
            // up the edge, and the threshold lands the full hysteresis above the measured floor.
            learning   = true;
            learnTicks = 0;
            learnPeak  = -120.0f;
            learnTrace.clear();
            learnTrace.reserve ((size_t) learnTotalTicks);
            if (onLearnBegin != nullptr)
                onLearnBegin();
            return;
        }

        onAtt->setValueAsCompleteGesture (1.0f);
        threshold->setValueAsCompleteGesture (choice == 2 ? -60.0f : choice == 3 ? -50.0f : -40.0f);
        decayAtt->setValueAsCompleteGesture (choice == 4 ? 1.0f : 0.0f);   // HARD is the metal chop
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        // The convention: DOUBLE-click a grip to type its number; a double anywhere else sends
        // the volume runner home. The release must not read as another click.
        swallowUp = true;

        if (gripRect().contains (e.position))
        {
            gripEd.open (*this, gripRect().toNearestInt(),
                         trimP.convertFrom0to1 (trimP.getValue()), theme::orange,
                         [this] (float v) { trim->setValueAsCompleteGesture (v); });
            return;
        }

        if (onP.getValue() > 0.5f && thGripRect().contains (e.position))
        {
            gripEd.open (*this, thGripRect().toNearestInt(),
                         param.convertFrom0to1 (param.getValue()), theme::lilac,
                         [this] (float v) { threshold->setValueAsCompleteGesture (v); });
            return;
        }

        trim->setValueAsCompleteGesture (0.0f);
    }

    juce::Rectangle<float> gripRect() const
    {
        const auto area = scaleArea();
        const float y   = trimY (area, trimP.convertFrom0to1 (trimP.getValue()));
        return { 0.0f, y - meterrail::gripH * 0.5f, (float) getWidth(), meterrail::gripH };
    }

    juce::Rectangle<float> thGripRect() const
    {
        const auto area = scaleArea();
        const float y   = dbToY (area, param.convertFrom0to1 (param.getValue()));
        return { 0.0f, y - meterrail::gripH * 0.5f, (float) getWidth(), meterrail::gripH };
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

            // A dark gate's runner does not answer — the grab falls through to the trim,
            // which is the input's own and never sleeps.
            grabbedTrim = onP.getValue() <= 0.5f
                       || std::abs (pressY - trY) < std::abs (pressY - thY);
            (grabbedTrim ? trim : threshold)->beginGesture();

            // The ghost's anchors: the trim AND the hold as they stood when the hand closed —
            // a hold that keeps decaying under the drag would melt the ghost mid-thought.
            trimStartDb = trimP.convertFrom0to1 (trimP.getValue());
            holdAtGrab  = holdDb;

            if (grabbedTrim && onTrimDrag != nullptr)
                onTrimDrag (true);
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
            if (grabbedTrim && onTrimDrag != nullptr)
                onTrimDrag (false);
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

        if (learning)
        {
            ++learnTicks;
            learnTrace.push_back (now);   // the waveform on the overlay IS the progress

            // The window's edges are thrown away: the menu click itself, and the hand leaving the
            // mouse, are not the noise floor.
            if (learnTicks > learnEdgeTicks && learnTicks <= learnTotalTicks - learnEdgeTicks)
                learnPeak = juce::jmax (learnPeak, now);

            if (learnTicks >= learnTotalTicks)
            {
                learning = false;

                // Nothing arrived: a muted input teaches nothing.
                juce::String verdict = "NOTHING HEARD";

                if (learnPeak >= -75.0f)
                {
                    const float th = juce::jlimit (-80.0f, -10.0f,
                                                   learnPeak + params::gateHysteresisDb);
                    threshold->setValueAsCompleteGesture (th);
                    onAtt->setValueAsCompleteGesture (1.0f);
                    verdict = "SET " + juce::String (juce::roundToInt (th)) + " DB";
                }

                if (onLearnDone != nullptr)
                    onLearnDone (verdict);
            }
        }

        repaint();
    }

    juce::Rectangle<float> scaleArea() const
    {
        return getLocalBounds().toFloat().reduced (2.0f);
    }

    /** The COLUMN, narrower than the rail it stands in and not moved: cut hard on the window's
        edge (left, for IN) and a little on the inner side — the grips keep the full rail width,
        so they now visibly stick out toward the edge, a handle past its slot. */
    juce::Rectangle<float> columnArea() const
    {
        return scaleArea().withTrimmedLeft (10.0f).withTrimmedRight (4.0f);
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
    static constexpr int   learnEdgeTicks     = 15;     // half a second each end, thrown away

    const std::atomic<float>& keyDb;
    const std::atomic<float>& pressureDb;
    std::atomic<bool>&        clip;
    juce::RangedAudioParameter& param;
    juce::RangedAudioParameter& trimP;
    juce::RangedAudioParameter& onP;
    juce::RangedAudioParameter& decayP;
    juce::RangedAudioParameter& posP;
    std::unique_ptr<juce::ParameterAttachment> threshold, trim, onAtt, decayAtt, posAtt;

    meterrail::GripEditor gripEd;

    float levelDb = -90.0f;
    float holdDb  = -90.0f;
    int   holdAge = 0;
    bool  learning   = false;
    int   learnTicks = 0;
    float learnPeak  = -120.0f;
    std::vector<float> learnTrace;
    bool  dragging    = false;
    bool  grabbedTrim = false;
    float trimStartDb = 0.0f;
    float holdAtGrab  = -90.0f;
    bool  swallowUp   = false;
    float pressY      = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GateStrip)
};

} // namespace orbitamp
