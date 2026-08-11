#pragma once

#include "Theme.h"

namespace orbitamp
{

/** The tone-stack response, drawn on a log frequency axis.

    It asks the owner for dB at a frequency rather than holding filters of its own — the curve is
    read from the coefficients the audio thread actually runs, so what is drawn can never drift from
    what is heard. A dumb view: a callback in, pixels out. */
class EqCurve : public juce::Component
{
public:
    /** A band you can grab. `freedom` says which axes it answers to — a shelf only moves in gain,
        a cut only in frequency, the bell in both. */
    struct Handle
    {
        enum class Freedom { gain, freq, both };

        double  hz = 1000.0;
        double  db = 0.0;
        Freedom freedom = Freedom::gain;
        bool    visible = true;
    };

    explicit EqCurve (std::function<double (double)> magnitudeDbAt)
        : magnitudeDb (std::move (magnitudeDbAt))
    {
    }

    void setHandles (juce::Array<Handle> newHandles)
    {
        handles = std::move (newHandles);
        repaint();
    }

    /** A handle was dragged to (hz, db). The owner decides what that means for its parameters and
        pushes the handles back — the curve never edits anything itself. */
    std::function<void (int index, double hz, double db)> onHandleDrag;
    std::function<void (int index, bool active)>          onDragActive;

    /** The wheel over a handle — Q on a bell, slope on a wall; the owner knows which. */
    std::function<void (int index, float delta)> onHandleWheel;

    /** Double-clicks: on empty curve — create (the surgical bell lands here); on a handle — the
        owner may take it away again. */
    std::function<void (double hz)> onCurveDoubleClick;
    std::function<void (int index)> onHandleDoubleClick;

    /** The cut filters' own response, drawn as red WALLS under the curve — what is removed,
        separated from how the rest is coloured. Unset = no walls. */
    std::function<double (double)> filterMagnitudeDb;

    /** The live spectrum, painted first inside the well — behind the walls, behind the curve:
        the signal is the ground the response stands on. Unset = no spectrum. */
    std::function<void (juce::Graphics&, juce::Rectangle<float>)> paintSpectrum;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();

        g.setColour (theme::bezel);
        g.fillRoundedRectangle (r, theme::radiusMd);

        drawGrid (g, r);

        {
            // Clipped to the well so the dive ends at the scope's edge instead of painting over the
            // block underneath — the curve leaves the display, it does not spill onto the face.
            const juce::Graphics::ScopedSaveState clipped (g);

            juce::Path well;
            well.addRoundedRectangle (r, theme::radiusMd);
            g.reduceClipRegion (well);

            const int w = juce::jmax (2, getWidth());

            if (paintSpectrum != nullptr)
                paintSpectrum (g, r);

            // The walls first: the area between 0 dB and the filters' own response, filled in the
            // cut's red — solid-dark over the near-black well, so it stays red instead of mudding.
            // Zero height in the passband, rising exactly where the filters bite.
            if (filterMagnitudeDb != nullptr)
            {
                juce::Path walls;
                walls.startNewSubPath (r.getX(), dbToY (r, 0.0f));

                for (int x = 0; x < w; ++x)
                {
                    const float px = (float) x + r.getX();
                    walls.lineTo (px, dbToY (r, (float) filterMagnitudeDb (xToHz (r, px))));
                }

                walls.lineTo (r.getRight(), dbToY (r, 0.0f));
                walls.closeSubPath();

                g.setColour (wallRed.withAlpha (0.30f));
                g.fillPath (walls);
                g.setColour (wallRed.withAlpha (0.75f));
                g.strokePath (walls, juce::PathStrokeType (1.2f));
            }

            // One point per pixel column — cheaper and smoother than a coarse grid interpolated up.
            juce::Path curve;

            for (int x = 0; x < w; ++x)
            {
                const float px = (float) x + r.getX();
                const float y  = dbToY (r, (float) magnitudeDb (xToHz (r, px)));

                if (x == 0) curve.startNewSubPath (px, y);
                else        curve.lineTo (px, y);
            }

            g.setColour (theme::violet);
            g.strokePath (curve, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        g.setColour (theme::hair2);
        g.drawRoundedRectangle (r.reduced (0.5f), theme::radiusMd, 1.0f);

        paintHandles (g, r);
    }

    //==============================================================================
    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = handleAt (e.position);
        if (dragging >= 0 && onDragActive)
            onDragActive (dragging, true);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging < 0 || onHandleDrag == nullptr)
            return;

        const auto r = getLocalBounds().toFloat();
        const auto& h = handles.getReference (dragging);

        const double hz = h.freedom == Handle::Freedom::gain ? h.hz : xToHz (r, e.position.x);
        const double db = h.freedom == Handle::Freedom::freq ? h.db : yToDb (r, e.position.y);

        onHandleDrag (dragging, hz, db);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging >= 0 && onDragActive)
            onDragActive (dragging, false);

        dragging = -1;
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int over = handleAt (e.position);
        if (over != hovered)
        {
            hovered = over;
            setMouseCursor (over >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (hovered >= 0)
        {
            hovered = -1;
            repaint();
        }
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (onHandleWheel == nullptr)
            return;

        if (const int over = handleAt (e.position); over >= 0)
            onHandleWheel (over, wheel.deltaY);
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (const int over = handleAt (e.position); over >= 0)
        {
            if (onHandleDoubleClick != nullptr)
                onHandleDoubleClick (over);
            return;
        }

        if (onCurveDoubleClick != nullptr)
            onCurveDoubleClick (xToHz (getLocalBounds().toFloat(), e.position.x));
    }

private:
    static constexpr float handleRadius = 5.0f;
    static constexpr float grabRadius   = 11.0f;   // generous: the dot is small, the target is not

    inline static const juce::Colour wallRed { 0xffe0503c };   // the ramp's red: removed, like GR

    int handleAt (juce::Point<float> p) const
    {
        const auto r = getLocalBounds().toFloat();

        int    best = -1;
        double bestD = grabRadius * grabRadius;

        for (int i = 0; i < handles.size(); ++i)
        {
            const auto& h = handles.getReference (i);
            if (! h.visible)
                continue;

            const auto c = handlePos (r, h);
            const double d = (double) p.getDistanceSquaredFrom (c);
            if (d < bestD)
            {
                bestD = d;
                best  = i;
            }
        }

        return best;
    }

    juce::Point<float> handlePos (juce::Rectangle<float> r, const Handle& h) const
    {
        // A cut has no gain of its own, so its handle rides the curve it produces rather than
        // floating at 0 dB where nothing is happening.
        const float db = h.freedom == Handle::Freedom::freq ? (float) magnitudeDb (h.hz) : (float) h.db;
        return { hzToX (r, h.hz), dbToY (r, db) };
    }

    void paintHandles (juce::Graphics& g, juce::Rectangle<float> r) const
    {
        for (int i = 0; i < handles.size(); ++i)
        {
            const auto& h = handles.getReference (i);
            if (! h.visible)
                continue;

            const auto c   = handlePos (r, h);
            const bool lit = (i == hovered || i == dragging);
            const float rad = handleRadius * (lit ? 1.25f : 1.0f);

            g.setColour (theme::bezel);
            g.fillEllipse (c.x - rad, c.y - rad, rad * 2.0f, rad * 2.0f);
            g.setColour (lit ? theme::lilac : theme::violet);
            g.drawEllipse (c.x - rad, c.y - rad, rad * 2.0f, rad * 2.0f, 1.6f);
        }
    }

    static float yToDb (juce::Rectangle<float> r, float y)
    {
        const float half = r.getHeight() * 0.5f - 6.0f;
        return (r.getCentreY() - y) / juce::jmax (1.0f, half) * rangeDb;
    }

    static constexpr double minHz  = 20.0;
    static constexpr double maxHz  = 20000.0;
    static constexpr float  rangeDb = 15.0f;   // a touch past the +-12 dB controls, so the curve never clips

    static double xToHz (juce::Rectangle<float> r, float x)
    {
        const double t = juce::jlimit (0.0, 1.0, (double) ((x - r.getX()) / juce::jmax (1.0f, r.getWidth())));
        return minHz * std::pow (maxHz / minHz, t);
    }

    static float hzToX (juce::Rectangle<float> r, double hz)
    {
        const double t = std::log (hz / minHz) / std::log (maxHz / minHz);
        return r.getX() + (float) t * r.getWidth();
    }

    static float dbToY (juce::Rectangle<float> r, float db)
    {
        // NOT clamped. A steep cut must keep diving and leave the scope through the floor — flooring
        // it into a horizontal line would draw a brickwall shelf that the filter does not have, and
        // would hide how steep the cut actually is. The well's clip is what ends the line.
        return r.getCentreY() - db / rangeDb * (r.getHeight() * 0.5f - 6.0f);
    }

    void drawGrid (juce::Graphics& g, juce::Rectangle<float> r) const
    {
        g.setColour (theme::hair);

        for (double hz : { 100.0, 1000.0, 10000.0 })
            g.fillRect (hzToX (r, hz), r.getY() + 4.0f, 1.0f, r.getHeight() - 8.0f);

        for (float db : { -12.0f, -6.0f, 6.0f, 12.0f })
            g.fillRect (r.getX() + 4.0f, dbToY (r, db), r.getWidth() - 8.0f, 1.0f);

        // The 0 dB line reads brighter — it is the reference the curve is judged against.
        g.setColour (theme::hair2);
        g.fillRect (r.getX() + 4.0f, dbToY (r, 0.0f), r.getWidth() - 8.0f, 1.0f);
    }

    std::function<double (double)> magnitudeDb;

    juce::Array<Handle> handles;
    int hovered  = -1;
    int dragging = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqCurve)
};

} // namespace orbitamp
