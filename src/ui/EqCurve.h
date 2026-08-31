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
        juce::Colour tint = juce::Colour (0xffb39bff);

        /** The dot sits ON the composite at its frequency instead of at its own `db` — for a
            handle whose gain is not a parameter of its own (a device knob's point): the curve is
            the only truth about where it stands, so the dot rides it. */
        bool rideCurve = false;
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

    /** Discrete vertical-drag notches on a line handle (slope ladder): index, +-steps. */
    std::function<void (int index, int steps)> onHandleStep;

    /** Double-clicks: on empty curve — create (the surgical bell lands here); on a handle — the
        owner may take it away again. */
    std::function<void (double hz)> onCurveDoubleClick;
    std::function<void (int index)> onHandleDoubleClick;

    /** The live spectrum, painted first inside the well — behind the walls, behind the curve:
        the signal is the ground the response stands on. Unset = no spectrum. */
    std::function<void (juce::Graphics&, juce::Rectangle<float>)> paintSpectrum;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();

        g.setColour (theme::bezel);
        g.fillRoundedRectangle (r, theme::radiusMd);

        {
            // tabby's vignette: centre lifted a touch, corners deepened — the plot breathes
            // instead of sitting flat.
            const juce::Graphics::ScopedSaveState clipped (g);
            juce::Path well;
            well.addRoundedRectangle (r, theme::radiusMd);
            g.reduceClipRegion (well);

            juce::ColourGradient vg (theme::bezel.brighter (0.18f),
                                     r.getCentreX(), r.getCentreY() - r.getHeight() * 0.06f,
                                     theme::bezel.darker (0.55f),
                                     r.getX(), r.getBottom(), true);
            g.setGradientFill (vg);
            g.fillRect (r);
        }

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

            // One point per pixel column — cheaper and smoother than a coarse grid interpolated up.
            // The fill path is the same trace closed along the 0 dB line: what is painted is the
            // DEVIATION from flat, tabby's grammar — not any single filter's opinion.
            const float y0 = dbToY (r, 0.0f);
            juce::Path curve, fill;
            fill.startNewSubPath (r.getX(), y0);

            for (int x = 0; x < w; ++x)
            {
                const float px = (float) x + r.getX();
                const float y  = dbToY (r, (float) magnitudeDb (xToHz (r, px)));

                if (x == 0) curve.startNewSubPath (px, y);
                else        curve.lineTo (px, y);
                fill.lineTo (px, y);
            }

            fill.lineTo (r.getRight(), y0);
            fill.closeSubPath();

            // Brand violet both sides, densest at the 0 dB line, fading to nothing at the edges —
            // one fill path, clipped to each half (tabby's exact recipe).
            {
                const juce::Graphics::ScopedSaveState half (g);
                g.reduceClipRegion ({ (int) r.getX(), (int) r.getY(),
                                      (int) r.getWidth(), juce::jmax (0, (int) (y0 - r.getY())) });
                g.setGradientFill (juce::ColourGradient (theme::violet.withAlpha (0.0f),  0.0f, r.getY(),
                                                         theme::violet.withAlpha (0.40f), 0.0f, y0, false));
                g.fillPath (fill);
            }
            {
                const juce::Graphics::ScopedSaveState half (g);
                g.reduceClipRegion ({ (int) r.getX(), (int) y0,
                                      (int) r.getWidth(), juce::jmax (0, (int) (r.getBottom() - y0)) });
                g.setGradientFill (juce::ColourGradient (theme::violet.withAlpha (0.0f),  0.0f, r.getBottom(),
                                                         theme::violet.withAlpha (0.40f), 0.0f, y0, false));
                g.fillPath (fill);
            }

            // The composite is brand orange with a faint glow — stacked strokes, no real blur.
            g.setColour (theme::orange.withAlpha (0.12f));
            g.strokePath (curve, juce::PathStrokeType (5.0f));
            g.setColour (theme::orange.withAlpha (0.22f));
            g.strokePath (curve, juce::PathStrokeType (2.5f));
            g.setColour (theme::orange);
            g.strokePath (curve, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        g.setColour (theme::hair2);
        g.drawRoundedRectangle (r.reduced (0.5f), theme::radiusMd, 1.0f);

        paintHandles (g, r);
    }

    //==============================================================================
    /** A right-click on the curve is the console's own menu — the same grammar the block and the
        picture already use: right-click a thing, get that thing's choices. */
    std::function<void (juce::Point<int>)> onContextMenu;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            if (onContextMenu != nullptr)
                onContextMenu (e.getScreenPosition());

            return;
        }

        dragging   = handleAt (e.position);
        stepAnchor = e.position.y;
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

        // A line has no gain to give the vertical axis, so the vertical axis works the ladder:
        // every stepPx of travel is one slope notch, down = steeper — the cut digs in as the hand
        // digs down. Same message as the wheel.
        if (h.freedom == Handle::Freedom::freq && onHandleStep != nullptr)
        {
            constexpr float stepPx = 28.0f;
            const int steps = (int) ((e.position.y - stepAnchor) / stepPx);
            if (steps != 0)
            {
                stepAnchor += (float) steps * stepPx;
                onHandleStep (dragging, steps);
            }
        }

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

            // A line handle answers along its whole height — distance is horizontal only.
            if (h.freedom == Handle::Freedom::freq)
            {
                const double dx = std::abs (p.x - hzToX (r, h.hz));
                const double d  = dx * dx * 4.0;   // narrower grab than a dot, or it shadows nodes
                if (d < bestD)
                {
                    bestD = d;
                    best  = i;
                }
                continue;
            }

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
        // floating at 0 dB where nothing is happening. A rideCurve dot does the same by request.
        const float db = h.freedom == Handle::Freedom::freq || h.rideCurve
                             ? (float) magnitudeDb (h.hz) : (float) h.db;
        return { hzToX (r, h.hz), dbToY (r, db) };
    }

    void paintHandles (juce::Graphics& g, juce::Rectangle<float> r) const
    {
        for (int i = 0; i < handles.size(); ++i)
        {
            const auto& h = handles.getReference (i);
            if (! h.visible)
                continue;

            const bool lit = (i == hovered || i == dragging);

            // A cut is a PLACE, not an amount — its handle is the whole vertical line, grabbable
            // anywhere along its height.
            if (h.freedom == Handle::Freedom::freq)
            {
                const float x = hzToX (r, h.hz);
                const float dashes[] = { 5.0f, 4.0f };
                g.setColour (h.tint.withAlpha (lit ? 1.0f : 0.65f));
                g.drawDashedLine ({ x, r.getY() + 2.0f, x, r.getBottom() - 2.0f },
                                  dashes, 2, lit ? 2.2f : 1.4f);
                continue;
            }

            const auto c   = handlePos (r, h);
            const float rad = handleRadius * (lit ? 1.25f : 1.0f);

            g.setColour (theme::bezel);
            g.fillEllipse (c.x - rad, c.y - rad, rad * 2.0f, rad * 2.0f);
            g.setColour (lit ? h.tint.brighter (0.4f) : h.tint);
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
        // The log grid the way EQs have always drawn it: the 1-2-5 series carries the weight
        // and the numbers, every other step stays hair-thin between them — the shrinking
        // spacing IS the logarithm, visible.
        for (double decade : { 10.0, 100.0, 1000.0, 10000.0 })
            for (int mult = 1; mult <= 9; ++mult)
            {
                const double hz = decade * mult;
                if (hz < 20.0 || hz > 20000.0)
                    continue;

                const bool major = mult == 1 || mult == 2 || mult == 5;
                g.setColour (major ? theme::hair2.withAlpha (0.20f) : theme::hair.withAlpha (0.07f));
                g.fillRect (hzToX (r, hz), r.getY() + 4.0f, 1.0f, r.getHeight() - 8.0f);
            }

        g.setColour (theme::hair);
        for (float db : { -12.0f, -6.0f, 6.0f, 12.0f })
            g.fillRect (r.getX() + 4.0f, dbToY (r, db), r.getWidth() - 8.0f, 1.0f);

        // The 0 dB line, tabby's exact recipe: a soft violet glow band under a translucent
        // white hairline — the reference reads without shouting.
        const float zy = dbToY (r, 0.0f);
        g.setColour (theme::lilac.withAlpha (0.05f));
        g.fillRect (r.getX() + 4.0f, zy - 2.5f, r.getWidth() - 8.0f, 5.0f);
        g.setColour (juce::Colour (0x40ffffff));
        g.fillRect (r.getX() + 4.0f, zy, r.getWidth() - 8.0f, 1.0f);

        // The numbers on the scales: an instrument, not a sketch.
        g.setColour (theme::txFaint);

        const auto hzLabel = [&] (double hz, const char* text)
        {
            theme::drawTracked (g, text,
                                { hzToX (r, hz) - 24.0f, r.getBottom() - 18.0f, 48.0f, 13.0f },
                                theme::displayFont (12.0f), 0.08f, juce::Justification::centred);
        };
        hzLabel (20.0, "20");
        hzLabel (50.0, "50");
        hzLabel (100.0, "100");
        hzLabel (200.0, "200");
        hzLabel (500.0, "500");
        hzLabel (1000.0, "1K");
        hzLabel (2000.0, "2K");
        hzLabel (5000.0, "5K");
        hzLabel (10000.0, "10K");
        hzLabel (20000.0, "20K");

        for (float db : { -12.0f, -6.0f, 6.0f, 12.0f })
            theme::drawTracked (g, (db > 0 ? "+" : "") + juce::String ((int) db),
                                { r.getX() + 6.0f, dbToY (r, db) - 15.0f, 40.0f, 13.0f },
                                theme::displayFont (12.0f), 0.08f, juce::Justification::centredLeft);
    }

    std::function<double (double)> magnitudeDb;

    juce::Array<Handle> handles;
    int hovered  = -1;
    int dragging = -1;
    float stepAnchor = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqCurve)
};

} // namespace orbitamp
