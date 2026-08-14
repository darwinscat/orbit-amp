#pragma once

#include "../Parameters.h"
#include "MeterRail.h"
#include "Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace orbitamp
{

/** A block's gain staging, lying down: a thin horizontal meter with the hand that moves it riding
    along the same bar.

    Two of these sit under a captured block's device combo — IN, what the model is being fed, and
    OUT, what leaves the block. They are a PAIR and that is the whole point of them being here: a
    captured device answers to what goes into it, so a block with only an output volume lets you fix
    the loudness while leaving the drive wherever it happened to land.

    TWO SCALES ON ONE BAR, which is the same trick the gate's sliver plays and works for the same
    reason: they are told apart by what they are drawn as. The FILL is the signal, on a dBFS scale
    from the floor to zero. The GRIP is a gain, on its own scale, unity notched. They meet because
    moving the grip moves the fill — drag, watch the level walk into the green, let go. That loop
    is the only reason a trim and a meter should ever share a bar.

    THE GREEN ZONE is where a captured device likes to be fed. It is drawn into the track rather
    than onto the fill: a wash under the signal reads as a place, a wash over it reads as a state.
    See `params::captureColdDb` for why the numbers are a convention rather than a measurement. */
class BlockMeter final : public juce::Component,
                         private juce::Timer
{
public:
    /** `showZone` is for the OUT meter, which has no such thing to say: what leaves a block is
        whatever the next block wants, and there is no window that is right for it. */
    BlockMeter (juce::String meterName, const std::atomic<float>& levelSource,
                juce::RangedAudioParameter& trimParam, bool showZone)
        : name (std::move (meterName)), levelDb (levelSource), param (trimParam), zone (showZone)
    {
        trim = std::make_unique<juce::ParameterAttachment> (trimParam, [this] (float) { repaint(); });
        setRepaintsOnMouseActivity (true);
        startTimerHz (30);
    }

    /** The grip is in the hand — the owner may want to show a ladder beside it. */
    std::function<void (bool)> onDrag;

    static constexpr int designHeight = 26;

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();

        g.setColour (theme::bezel);
        g.fillRoundedRectangle (r, theme::radiusSm);

        const auto track = trackArea();

        // ---- the lane itself. Drawn even when nothing is playing, because a bar you cannot see
        //      until it has a signal in it is a bar nobody knows they can grab. ----
        g.setColour (theme::hair2.withAlpha (0.45f));
        g.drawRoundedRectangle (track.reduced (0.5f), 2.0f, 1.0f);

        // ---- the signal ----
        paintFill (g, track);

        // ---- the green zone: where this block wants to be fed.
        //
        //      A band along the LANE'S EDGE rather than a wash across it, and after the fill rather
        //      than under it. Drawn as a wash it lost twice over — the signal painted across it and
        //      the grip's frame parked on top of it, which is where the trim's unity happens to
        //      sit. A mark on the rail is read the way a mark on a ruler is: it belongs to the
        //      scale, not to what is being measured against it. ----
        if (zone)
        {
            const float a = xOfLevel (track, params::captureColdDb);
            const float b = xOfLevel (track, params::captureHotDb);

            g.setColour (inRange.withAlpha (0.85f));
            g.fillRect (a, track.getBottom() - 3.0f, b - a, 2.5f);
            g.setColour (inRange.withAlpha (0.30f));
            g.fillRect (a, track.getY() + 0.5f, b - a, 1.5f);
        }

        if (hold > floorDb + 0.5f)
        {
            const float x = xOfLevel (track, hold);
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.fillRect (x - 0.75f, track.getY(), 1.5f, track.getHeight());
        }

        // ---- zero on the SIGNAL's scale: not the same place as the hand's unity, and the one
        //      line on this bar that means "full" ----
        {
            const float z = xOfLevel (track, 0.0f);
            g.setColour (juce::Colours::white.withAlpha (0.22f));
            g.fillRect (z - 0.5f, track.getY() + 1.0f, 1.0f, track.getHeight() - 2.0f);
        }

        // ---- the name, inside the column at its head, like the vertical rails do ----
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        theme::drawTracked (g, name, r.withTrimmedLeft (6.0f).withWidth (nameW - 8.0f),
                            theme::displayFont (10.0f), 0.08f, juce::Justification::centredLeft);

        // ---- unity, and the hand ----
        const float unityX = xOfTrim (track, 0.0f);
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.fillRect (unityX - 1.0f, track.getY(), 2.0f, 5.0f);
        g.fillRect (unityX - 1.0f, track.getBottom() - 5.0f, 2.0f, 5.0f);

        paintGrip (g, track);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            // The one thing a trim is always asked for: put it back.
            trim->setValueAsCompleteGesture (0.0f);
            return;
        }

        dragging = true;
        param.beginChangeGesture();
        if (onDrag != nullptr)
            onDrag (true);

        writeFromX ((float) e.position.x);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging)
            writeFromX ((float) e.position.x);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (! dragging)
            return;

        dragging = false;
        param.endChangeGesture();
        if (onDrag != nullptr)
            onDrag (false);
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        trim->setValueAsCompleteGesture (0.0f);
    }

private:
    void timerCallback() override
    {
        const float now = levelDb.load();

        // The peak hold, decaying: it is here to let a phrase be READ, not to latch forever.
        if (now > hold)  hold = now;
        else             hold = juce::jmax (now, hold - 0.6f);

        repaint();
    }

    juce::Rectangle<float> trackArea() const
    {
        return getLocalBounds().toFloat().reduced (2.0f, 2.0f).withTrimmedLeft (nameW);
    }

    static constexpr float nameW = 28.0f;

    // The scale, and it is not the usual sixty decibels. Two of these live side by side inside half
    // a block, so the bar is short and every unit of it has to be worth something: a floor at -60
    // spends four fifths of the width on "far too quiet" and squeezes the window that matters into
    // the last inch. -42 to +6 puts the green zone in the middle of the bar where it can be aimed
    // at, and keeps a little room above zero so a clip is visible rather than merely full.
    static constexpr float floorDb = -42.0f;
    static constexpr float ceilDb  =   6.0f;

    static float xOfLevel (juce::Rectangle<float> t, float db)
    {
        const float u = juce::jlimit (0.0f, 1.0f, (db - floorDb) / (ceilDb - floorDb));
        return t.getX() + u * t.getWidth();
    }

    static float xOfTrim (juce::Rectangle<float> t, float db)
    {
        const float u = juce::jlimit (0.0f, 1.0f, (db - params::blockTrimMinDb)
                                                    / (params::blockTrimMaxDb - params::blockTrimMinDb));
        return t.getX() + u * t.getWidth();
    }

    float trimDb() const { return param.convertFrom0to1 (param.getValue()); }

    void writeFromX (float x)
    {
        const auto t = trackArea();
        const float u = juce::jlimit (0.0f, 1.0f, (x - t.getX()) / juce::jmax (1.0f, t.getWidth()));
        trim->setValueAsPartOfGesture (params::blockTrimMinDb
                                       + u * (params::blockTrimMaxDb - params::blockTrimMinDb));
    }

    /** The family gradient, lying down: mostly violet, warming to orange only near the top, and
        the level CLIPS it — so a given length always reads the same colour. */
    void paintFill (juce::Graphics& g, juce::Rectangle<float> t) const
    {
        const float x = xOfLevel (t, levelDb.load());

        if (x <= t.getX() + 1.0f)
            return;

        juce::ColourGradient grad (theme::violet.withAlpha (0.50f), t.getX(), 0.0f,
                                   theme::orange, t.getRight(), 0.0f, false);
        grad.addColour (0.58, theme::violet.withAlpha (0.60f));

        const juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion ((int) std::floor (t.getX()), (int) std::floor (t.getY()),
                            (int) std::ceil (x - t.getX()), (int) std::ceil (t.getHeight()) + 1);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (t, 2.0f);
    }

    /** A hollow sliding frame with a sight, carrying its own number — the same hand the vertical
        rails use, turned on its side. Orange: it is the player's, not the signal's. */
    void paintGrip (juce::Graphics& g, juce::Rectangle<float> t) const
    {
        constexpr float w = 34.0f;

        const float x = xOfTrim (t, trimDb());
        const juce::Rectangle<float> frame (juce::jlimit (t.getX(), t.getRight() - w, x - w * 0.5f),
                                            t.getY() + 0.75f, w, t.getHeight() - 1.5f);

        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.drawRoundedRectangle (frame, 2.5f, 3.0f);
        g.setColour (dragging || isMouseOver() ? theme::orange.brighter (0.25f) : theme::orange);
        g.drawRoundedRectangle (frame, 2.5f, 1.4f);

        // The sight: two nubs biting in from top and bottom at the exact value, because a frame
        // this wide cannot say a number precisely on its own.
        g.fillRect (x - 0.75f, frame.getY(), 1.5f, 3.5f);
        g.fillRect (x - 0.75f, frame.getBottom() - 3.5f, 1.5f, 3.5f);

        theme::drawTracked (g, meterrail::trimText (trimDb()), frame, theme::displayFont (10.0f),
                            0.0f, juce::Justification::centred);
    }

    inline static const juce::Colour inRange { 0xff5fc97a };   // the character ramp's clean green

    juce::String name;
    const std::atomic<float>& levelDb;
    juce::RangedAudioParameter& param;
    bool zone = true;

    std::unique_ptr<juce::ParameterAttachment> trim;
    float hold = -120.0f;
    bool  dragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BlockMeter)
};

} // namespace orbitamp
