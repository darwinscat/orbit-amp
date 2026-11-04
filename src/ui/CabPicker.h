// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "MicGlyphs.h"
#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace orbitamp
{

/** THE BASS'S CABINETS, DRAWN — two views in one place.

    THE ROW: the boxes to scale, each with its speakers, its horn and its port where it has them, one family a row (SVT,
    VENTURE); on the chosen box's first speaker the mic. A click on a box picks it, the mic at its cap; a click on the
    chosen box opens its speaker; a click on its horn turns the horn off and on.

    THE SPEAKER: the chosen box's first speaker, big, its neighbours at its edge — the mic dragged anywhere across it and a
    little past it, the distance on a rail at the right, 1 to 30 cm. What is set is sent when the hand lets go (onSpot):
    a render takes a tenth of a second, and a cabinet changing under a moving hand would only stutter.

    A dumb view: the cabinets in (cabs(), geometry from tools/cabgen/cabinets.txt — metres, the baffle's centre the
    origin, up positive), what plays in (show), a pick out (onSpot). */
class CabPicker final : public juce::Component,
                        public juce::SettableTooltipClient
{
public:
    CabPicker() = default;

    struct Spot
    {
        float x = 0.0f, y = 0.0f, distance = 0.05f;   // across the first speaker in radii from the cap; in front of it, m
        bool horn = true;
        juce::String mic;                             // one of mics(), or empty for the cabinet's own
        float angle = 0.0f;                           // degrees it leans back toward the cap
    };

    /** Two mics on the box (AmpProcessor::CabSetup): A, and B mixed in by `blend` — 0 is A alone — on one clock unless
        `align`, B turned over by `invert`. The horn is A's. */
    struct Setup
    {
        Spot a, b { 0.0f, 0.0f, 0.25f, true, "u87" };
        float blend = 0.0f;
        bool align = false, invert = false;
    };

    /** The mics to choose from, as the table names them (its "mic-<name>" sections), and as the row writes them. */
    static const juce::StringArray& mics()      { static const juce::StringArray m { "md421", "sm57", "re20", "d112", "u87", "ribbon" }; return m; }
    static const juce::StringArray& micWords()  { static const juce::StringArray m { "MD421", "SM57", "RE20", "D112", "U87", "RIBBON" }; return m; }

    struct Cab
    {
        juce::String name;                       // as the table names it: "SVT-410HE"
        int family = 0;                          // 0 SVT, 1 VENTURE
        float w = 0.6f, h = 0.6f;                // the baffle
        std::vector<juce::Point<float>> drivers; // the first is the one the mic stands at
        float radius = 0.105f;                   // a cone's radiating radius
        bool horn = false;
        juce::Point<float> hornAt;
        bool port = false;
        juce::Point<float> portAt;
        juce::String mic = "md421";              // the one its table's cap section stands
    };

    static const std::vector<Cab>& cabs()
    {
        using P = juce::Point<float>;
        static const std::vector<Cab> all {
            { "SVT-15E",    0, 0.61f, 0.56f, { P (0, 0.02f) }, 0.165f, false, {}, true, P (0, -0.22f), "d112" },
            { "SVT-210AV",  0, 0.33f, 0.61f, { P (0, 0.14f), P (0, -0.14f) }, 0.105f, false, {}, false, {} },
            { "SVT-210HE",  0, 0.61f, 0.46f, { P (-0.14f, 0), P (0.14f, 0) }, 0.105f, true, P (0, 0.16f), false, {} },
            { "SVT-410HE",  0, 0.61f, 0.66f, { P (-0.145f, 0.145f), P (0.145f, 0.145f), P (-0.145f, -0.145f), P (0.145f, -0.145f) },
              0.105f, true, P (0, 0), false, {} },
            { "SVT-410HLF", 0, 0.61f, 0.76f, { P (-0.145f, 0.19f), P (0.145f, 0.19f), P (-0.145f, -0.10f), P (0.145f, -0.10f) },
              0.105f, true, P (0, 0.045f), true, P (0, -0.31f) },
            { "SVT-610HLF", 0, 0.61f, 1.02f, { P (-0.145f, 0.32f), P (0.145f, 0.32f), P (-0.145f, 0.03f), P (0.145f, 0.03f),
                                                P (-0.145f, -0.26f), P (0.145f, -0.26f) },
              0.105f, true, P (0, 0.175f), true, P (0, -0.45f) },
            { "SVT-810",    0, 0.66f, 1.22f, { P (-0.15f, 0.42f), P (0.15f, 0.42f), P (-0.15f, 0.14f), P (0.15f, 0.14f),
                                                P (-0.15f, -0.14f), P (0.15f, -0.14f), P (-0.15f, -0.42f), P (0.15f, -0.42f) },
              0.105f, false, {}, false, {}, "re20" },
            { "VB-112",     1, 0.52f, 0.40f, { P (-0.09f, 0) }, 0.13f, true, P (0.17f, 0.08f), true, P (0.17f, -0.12f) },
            { "VB-115",     1, 0.56f, 0.58f, { P (0, -0.04f) }, 0.165f, true, P (0.18f, 0.19f), true, P (0, -0.25f), "d112" },
            { "VB-210",     1, 0.56f, 0.42f, { P (-0.135f, -0.02f), P (0.135f, -0.02f) }, 0.105f, true, P (0, 0.13f), true, P (0, -0.17f) },
            { "VB-410",     1, 0.58f, 0.66f, { P (-0.14f, 0.145f), P (0.14f, 0.145f), P (-0.14f, -0.145f), P (0.14f, -0.145f) },
              0.105f, true, P (0, 0), true, P (0, -0.30f) },
        };
        return all;
    }

    static int indexOf (const juce::String& name)
    {
        for (int i = 0; i < (int) cabs().size(); ++i)
            if (cabs()[(size_t) i].name == name)
                return i;
        return -1;
    }

    /** What plays: a cabinet of cabs() and its mics — or none (an IR of the player's own, or the factory's). */
    void show (int cab, Setup setup)
    {
        if (dragging)
            return;   // the hand's, until it lets go
        chosen = cab;
        setup_ = setup;
        if (chosen >= 0)
            family = cabs()[(size_t) chosen].family;
        else
            zoomed = false;
        repaint();
    }

    std::function<void (int cab, Setup setup)> onSetup;

    /** The door to the player's own IRs, top right of the row (where it is set). */
    std::function<void()> onOwnIrs;

    /** The chosen box's speaker opened, or the row back. */
    void openSpeaker (bool open)
    {
        zoomed = open && chosen >= 0;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        if (zoomed && chosen >= 0)
            paintSpeaker (g);
        else
            paintRow (g);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        if (zoomed)
            return;
        const int was = hover;
        hover = cabAt (e.position);
        if (hover != was)
        {
            setTooltip (hover >= 0 ? cabs()[(size_t) hover].name : juce::String());
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        hover = -1;
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (zoomed && chosen >= 0)
        {
            speakerDown (e);
            return;
        }

        for (int f = 0; f < 2; ++f)
            if (tabArea (f).contains (e.getPosition()))
            {
                family = f;
                hover = -1;
                repaint();
                return;
            }

        if (onOwnIrs != nullptr && ownArea().contains (e.getPosition()))
        {
            onOwnIrs();
            return;
        }

        const int at = cabAt (e.position);
        if (at < 0)
            return;

        if (at != chosen)
        {
            chosen = at;
            setup_ = {};
            setup_.a.mic = cabs()[(size_t) at].mic;
            send();
            return;
        }

        // The chosen box again: its horn turned over, or its speaker opened.
        const auto& cab = cabs()[(size_t) at];
        const auto box = boxOf (at);
        const float s = box.getWidth() / cab.w;
        if (cab.horn && e.position.getDistanceFrom (onBox (box, s, cab.hornAt)) < juce::jmax (7.0f, 0.06f * s))
        {
            setup_.a.horn = ! setup_.a.horn;
            send();
            return;
        }
        zoomed = true;
        setTooltip ({});
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! zoomed || chosen < 0 || drag == Drag::none)
            return;
        auto& m = mic (dragMic);
        if (drag == Drag::spot)
        {
            const auto o = scene().getCentre();
            const float R = radius();
            auto p = (e.position - o) / R;
            const float len = p.getDistanceFromOrigin();
            if (len > maxReach)
                p *= maxReach / len;
            m.x = p.x;
            m.y = -p.y;
        }
        else if (drag == Drag::distance)
            m.distance = minCm * std::pow (maxCm / minCm, along (track (dragMic, 0), e.position.x)) / 100.0f;
        else if (drag == Drag::angle)
            m.angle = maxAngle * along (track (dragMic, 1), e.position.x);
        else if (drag == Drag::blend)
            setup_.blend = along (blendTrack(), e.position.x);
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (drag != Drag::none)
        {
            drag = Drag::none;
            dragging = false;
            send();
        }
    }

private:
    static constexpr float maxReach = 1.35f;   // how far past its rim the mic may go, in radii
    static constexpr float minCm = 1.0f, maxCm = 30.0f, maxAngle = 60.0f;

    enum class Drag { none, spot, distance, angle, blend };

    int chosen = -1, hover = -1, family = 0;
    Setup setup_;
    bool zoomed = false, dragging = false;
    Drag drag = Drag::none;
    int dragMic = 0;

    Spot& mic (int m) { return m == 0 ? setup_.a : setup_.b; }
    const Spot& mic (int m) const { return m == 0 ? setup_.a : setup_.b; }

    void send()
    {
        repaint();
        if (onSetup != nullptr && chosen >= 0)
            onSetup (chosen, setup_);
    }

    // ---------------------------------------------------------------- the row
    juce::Rectangle<int> tabArea (int f) const
    {
        const auto font = theme::displayFont (13.0f);
        const int svtW = juce::roundToInt (theme::trackedWidth ("SVT", font, 0.08f)) + 4;
        const int ventW = juce::roundToInt (theme::trackedWidth ("VENTURE", font, 0.08f)) + 4;
        return f == 0 ? juce::Rectangle<int> (0, 0, svtW, 18) : juce::Rectangle<int> (svtW + 14, 0, ventW, 18);
    }

    juce::Rectangle<int> ownArea() const
    {
        const auto font = theme::displayFont (13.0f);
        const int w = juce::roundToInt (theme::trackedWidth ("MY IRS", font, 0.08f)) + 4;
        return { getWidth() - w, 0, w, 18 };
    }

    juce::Rectangle<int> rowArea() const { return getLocalBounds().withTrimmedTop (24).reduced (2, 0); }

    /** Each box of the shown family, to scale, its feet on the floor, the row fitted to the area. */
    std::vector<std::pair<int, juce::Rectangle<float>>> layOut() const
    {
        std::vector<int> shown;
        float sumW = 0, maxH = 0;
        for (int i = 0; i < (int) cabs().size(); ++i)
            if (cabs()[(size_t) i].family == family)
            {
                shown.push_back (i);
                sumW += cabs()[(size_t) i].w;
                maxH = juce::jmax (maxH, cabs()[(size_t) i].h);
            }

        const auto row = rowArea().toFloat();
        const float gapPx = 8.0f;
        const float s = juce::jmin ((row.getWidth() - gapPx * (float) (shown.size() - 1)) / sumW, (row.getHeight() - 2.0f) / maxH);
        float x = row.getX() + 0.5f * (row.getWidth() - (sumW * s + gapPx * (float) (shown.size() - 1)));

        std::vector<std::pair<int, juce::Rectangle<float>>> out;
        for (int i : shown)
        {
            const auto& c = cabs()[(size_t) i];
            out.push_back ({ i, juce::Rectangle<float> (x, row.getBottom() - 1.0f - c.h * s, c.w * s, c.h * s) });
            x += c.w * s + gapPx;
        }
        return out;
    }

    juce::Rectangle<float> boxOf (int index) const
    {
        for (const auto& [i, box] : layOut())
            if (i == index)
                return box;
        return {};
    }

    int cabAt (juce::Point<float> p) const
    {
        for (const auto& [i, box] : layOut())
            if (box.expanded (3.0f).contains (p))
                return i;
        return -1;
    }

    static juce::Point<float> onBox (juce::Rectangle<float> box, float s, juce::Point<float> p)
    {
        return { box.getCentreX() + p.x * s, box.getCentreY() - p.y * s };
    }

    void paintRow (juce::Graphics& g) const
    {
        // The two families, as words to click: the one shown in the block's orange, the other faint.
        const auto font = theme::displayFont (13.0f);
        for (int f = 0; f < 2; ++f)
        {
            g.setColour (f == family ? theme::orange : theme::txFaint);
            theme::drawTracked (g, f == 0 ? "SVT" : "VENTURE", tabArea (f).toFloat(), font, 0.08f, juce::Justification::centredLeft);
        }

        if (onOwnIrs != nullptr)
        {
            g.setColour (theme::txDim);
            theme::drawTracked (g, "MY IRS", ownArea().toFloat(), font, 0.08f, juce::Justification::centredRight);
        }

        const auto row = rowArea();
        g.setColour (theme::hair);
        g.fillRect (juce::Rectangle<float> ((float) row.getX(), (float) row.getBottom() - 1.0f, (float) row.getWidth(), 1.0f));

        for (const auto& [index, box] : layOut())
            paintCab (g, cabs()[(size_t) index], box, index == chosen, index == hover);
    }

    void paintCab (juce::Graphics& g, const Cab& c, juce::Rectangle<float> box, bool isChosen, bool isHover) const
    {
        const float s = box.getWidth() / c.w;

        // The box: the tolex, a grille's hint, its outline — the chosen one in the block's orange line.
        g.setColour (theme::panel2);
        g.fillRoundedRectangle (box, c.family == 1 ? 4.0f : 2.0f);
        g.setColour (theme::bezel);
        g.fillRoundedRectangle (box.reduced (juce::jmax (1.5f, 0.025f * s)), 1.5f);
        g.setColour (isChosen ? theme::orange : isHover ? theme::violet.withAlpha (0.7f) : theme::hair2);
        g.drawRoundedRectangle (box, c.family == 1 ? 4.0f : 2.0f, isChosen ? 1.5f : 1.0f);

        if (c.port)
        {
            g.setColour (juce::Colours::black);
            g.fillRoundedRectangle (juce::Rectangle<float> (0.20f * s, juce::jmax (2.0f, 0.035f * s)).withCentre (onBox (box, s, c.portAt)), 1.0f);
        }

        const auto line = theme::violet.withAlpha (isChosen ? 0.85f : 0.5f);
        for (const auto& d : c.drivers)
        {
            const auto o = onBox (box, s, d);
            const float r = c.radius * s;
            g.setColour (line);
            g.drawEllipse (juce::Rectangle<float> (2 * r, 2 * r).withCentre (o), 1.0f);
            g.drawEllipse (juce::Rectangle<float> (0.5f * r, 0.5f * r).withCentre (o), 0.8f);
        }

        if (c.horn)
        {
            const auto r = juce::Rectangle<float> (0.10f * s, 0.05f * s).withCentre (onBox (box, s, c.hornAt));
            g.setColour (isChosen && ! setup_.a.horn ? theme::txFaint.withAlpha (0.4f) : theme::lilac.withAlpha (isChosen ? 0.9f : 0.55f));
            g.drawRoundedRectangle (r, 1.5f, 1.0f);
        }

        // The mics where they stand on the first speaker, A orange and B lilac; farther off, a ring as wide as the distance.
        if (isChosen)
        {
            const auto o = onBox (box, s, c.drivers.front());
            const float r = c.radius * s;
            for (int m = setup_.blend > 0.0f ? 1 : 0; m >= 0; --m)
            {
                const auto& sp = m == 0 ? setup_.a : setup_.b;
                const auto at = o + juce::Point<float> (sp.x * r, -sp.y * r);
                g.setColour (m == 0 ? theme::orange : theme::lilac);
                g.fillEllipse (juce::Rectangle<float> (6, 6).withCentre (at));
                if (sp.distance > 0.08f)
                    g.drawEllipse (juce::Rectangle<float> (2, 2).withCentre (at).expanded (sp.distance * s), 1.0f);
            }
        }
    }

    // ---------------------------------------------------------------- the speaker
    // Its mics in two columns, A at the left in the block's orange and B at the right in lilac — each its list of mics,
    // its distance and its angle; between them the speaker, flat, as the player faces it, and on it the mics in
    // perspective: the nearer the cone, the smaller (the farther from the eye), a shadow on the cone that grows and
    // fades with the distance, the glyph leaning back toward the cap by the angle. BLEND, ALIGN and Ø under it.
    static constexpr int colW = 150, rowH = 18, headH = 22, footH = 26;

    static juce::Colour colourOf (int m) { return m == 0 ? theme::orange : theme::lilac; }

    juce::Rectangle<int> backArea() const
    {
        const auto font = theme::displayFont (13.0f);
        return { 0, 0, juce::roundToInt (theme::trackedWidth ("< CABS", font, 0.08f)) + 4, 18 };
    }

    juce::Rectangle<int> hornArea() const
    {
        const auto font = theme::displayFont (13.0f);
        const int w = juce::roundToInt (theme::trackedWidth ("HORN", font, 0.08f)) + 4;
        return { getWidth() - w, 0, w, 18 };
    }

    juce::Rectangle<int> column (int m) const
    {
        const int h = getHeight() - headH - footH;
        return m == 0 ? juce::Rectangle<int> (0, headH, colW, h) : juce::Rectangle<int> (getWidth() - colW, headH, colW, h);
    }

    juce::Rectangle<int> micRow (int m, int i) const
    {
        const auto c = column (m);
        return { c.getX(), c.getY() + rowH + 4 + i * rowH, c.getWidth(), rowH };
    }

    /** A column's two sliders: 0 the distance, 1 the angle — a word and its value over a track. */
    juce::Rectangle<float> track (int m, int which) const
    {
        const auto c = column (m);
        const int top = micRow (m, mics().size() - 1).getBottom() + 8 + which * (rowH + 12);
        return juce::Rectangle<float> ((float) c.getX() + 4.0f, (float) (top + rowH + 2), (float) c.getWidth() - 12.0f, 6.0f);
    }

    juce::Rectangle<float> scene() const
    {
        return getLocalBounds().withTrimmedTop (headH).withTrimmedBottom (footH).reduced (colW + 10, 0).toFloat();
    }

    float radius() const { const auto s = scene(); return juce::jmax (10.0f, juce::jmin (s.getWidth(), s.getHeight()) * 0.5f / 1.2f); }

    juce::Rectangle<int> foot() const { return { colW + 10, getHeight() - footH + 4, getWidth() - 2 * (colW + 10), footH - 4 }; }

    juce::Rectangle<float> blendTrack() const
    {
        const auto f = foot().toFloat();
        const float word = theme::trackedWidth ("BLEND", theme::displayFont (13.0f), 0.06f);
        return { f.getX() + word + 30.0f, f.getCentreY() - 3.0f, f.getWidth() * 0.38f, 6.0f };
    }

    juce::Rectangle<int> alignArea() const
    {
        const auto font = theme::displayFont (13.0f);
        const int x = juce::roundToInt (blendTrack().getRight()) + 26;
        return { x, foot().getY(), juce::roundToInt (theme::trackedWidth ("ALIGN", font, 0.06f)) + 4, foot().getHeight() };
    }

    juce::Rectangle<int> invertArea() const { return { alignArea().getRight() + 14, foot().getY(), 20, foot().getHeight() }; }

    static float along (juce::Rectangle<float> t, float x) { return juce::jlimit (0.0f, 1.0f, (x - t.getX()) / t.getWidth()); }

    /** Where a mic stands on the cone, on the screen, and how big its glyph is there. */
    juce::Point<float> spotOf (int m) const
    {
        const auto o = scene().getCentre();
        const float R = radius();
        return o + juce::Point<float> (mic (m).x * R, -mic (m).y * R);
    }

    static float nearness (float distance)   // 0 at the cone, 1 at 30 cm
    {
        return std::log (juce::jlimit (minCm, maxCm, distance * 100.0f) / minCm) / std::log (maxCm / minCm);
    }

    bool heardB() const { return setup_.blend > 0.0f; }

    /** B's column taken up: B is heard, half and half, from the first touch. */
    void takeUpB()
    {
        if (! heardB())
            setup_.blend = 0.5f;
    }

    void speakerDown (const juce::MouseEvent& e)
    {
        const auto pos = e.getPosition();
        if (backArea().contains (pos))
        {
            zoomed = false;
            repaint();
            return;
        }
        const auto& cab = cabs()[(size_t) chosen];
        if (cab.horn && hornArea().contains (pos))
        {
            setup_.a.horn = ! setup_.a.horn;
            send();
            return;
        }

        for (int m = 0; m < 2; ++m)
        {
            if (! column (m).contains (pos))
                continue;
            for (int i = 0; i < mics().size(); ++i)
                if (micRow (m, i).contains (pos))
                {
                    if (m == 1) takeUpB();
                    mic (m).mic = mics()[i];
                    send();
                    return;
                }
            for (int which = 0; which < 2; ++which)
                if (track (m, which).expanded (2.0f, 10.0f).contains (e.position))
                {
                    if (m == 1) takeUpB();
                    drag = which == 0 ? Drag::distance : Drag::angle;
                    dragMic = m;
                    dragging = true;
                    mouseDrag (e);
                    return;
                }
            return;
        }

        if (alignArea().expanded (4, 2).contains (pos)) { setup_.align = ! setup_.align; send(); return; }
        if (invertArea().expanded (4, 2).contains (pos)) { setup_.invert = ! setup_.invert; send(); return; }
        if (blendTrack().expanded (8.0f, 8.0f).contains (e.position))
        {
            drag = Drag::blend;
            dragging = true;
            mouseDrag (e);
            return;
        }

        if (scene().contains (e.position))
        {
            // The nearer mic of the two (B only once it is heard) is the one taken; elsewhere, A moves.
            int m = 0;
            if (heardB() && e.position.getDistanceFrom (spotOf (1)) < e.position.getDistanceFrom (spotOf (0)))
                m = 1;
            drag = Drag::spot;
            dragMic = m;
            dragging = true;
            mouseDrag (e);
        }
    }

    void paintMicGlyph (juce::Graphics& g, const juce::String& name, juce::Rectangle<float> box, juce::Colour colour, float lean,
                        juce::Point<float> pivot) const
    {
        if (const auto* d = micGlyph (name))
        {
            auto copy = d->createCopy();
            copy->replaceColour (juce::Colours::black, colour);
            juce::Graphics::ScopedSaveState keep (g);
            g.addTransform (juce::AffineTransform::rotation (lean, pivot.x, pivot.y));
            copy->drawWithin (g, box, juce::RectanglePlacement::centred, 1.0f);
        }
    }

    void paintColumn (juce::Graphics& g, int m) const
    {
        const auto font = theme::displayFont (13.0f);
        const auto c = column (m);
        const auto colour = colourOf (m);
        const bool live = m == 0 || heardB();
        const float dim = live ? 1.0f : 0.45f;
        const auto& sp = mic (m);
        const auto& cab = cabs()[(size_t) chosen];
        const auto heardBy = sp.mic.isNotEmpty() ? sp.mic : cab.mic;

        g.setColour (colour.withMultipliedAlpha (dim));
        theme::drawTracked (g, m == 0 ? "MIC A" : "MIC B", c.toFloat().withHeight ((float) rowH), font, 0.08f, juce::Justification::centredLeft);

        for (int i = 0; i < mics().size(); ++i)
        {
            const auto r = micRow (m, i).toFloat();
            const bool lit = mics()[i] == heardBy;
            const auto tint = (lit ? colour : theme::txFaint).withMultipliedAlpha (dim);
            paintMicGlyph (g, mics()[i], r.withWidth (12.0f).reduced (0, 1), tint, 0.0f, {});
            g.setColour (tint);
            theme::drawTracked (g, micWords()[i], r.withTrimmedLeft (20.0f), font, 0.06f, juce::Justification::centredLeft);
        }

        const juce::String words[2] { "DIST", "ANGLE" };
        const juce::String values[2] { juce::String (juce::roundToInt (sp.distance * 100.0f)) + " cm",
                                       juce::String (juce::roundToInt (sp.angle)) + juce::String::fromUTF8 ("\xc2\xb0") };
        const float at[2] { nearness (sp.distance), sp.angle / maxAngle };
        for (int which = 0; which < 2; ++which)
        {
            const auto t = track (m, which);
            const auto label = juce::Rectangle<float> (t.getX(), t.getY() - (float) rowH - 2.0f, t.getWidth(), (float) rowH);
            g.setColour (theme::txFaint.withMultipliedAlpha (dim));
            theme::drawTracked (g, words[which], label, font, 0.06f, juce::Justification::centredLeft);
            g.setColour (theme::txDim.withMultipliedAlpha (dim));
            theme::drawTracked (g, values[which], label, font, 0.0f, juce::Justification::centredRight);
            g.setColour (theme::hair2);
            g.fillRoundedRectangle (t.withSizeKeepingCentre (t.getWidth(), 2.0f), 1.0f);
            g.setColour (colour.withMultipliedAlpha (dim));
            g.fillRoundedRectangle (juce::Rectangle<float> (6.0f, 12.0f).withCentre ({ t.getX() + at[which] * t.getWidth(), t.getCentreY() }), 2.0f);
        }
    }

    void paintSpeaker (juce::Graphics& g) const
    {
        const auto& cab = cabs()[(size_t) chosen];
        const auto font = theme::displayFont (13.0f);

        g.setColour (theme::txDim);
        theme::drawTracked (g, "< CABS", backArea().toFloat(), font, 0.08f, juce::Justification::centredLeft);
        if (cab.horn)
        {
            g.setColour (setup_.a.horn ? theme::orange : theme::txFaint);
            theme::drawTracked (g, "HORN", hornArea().toFloat(), font, 0.08f, juce::Justification::centredRight);
        }

        paintColumn (g, 0);
        paintColumn (g, 1);

        const auto sc = scene();
        const float R = radius();
        const auto o = sc.getCentre();
        const float s = R / cab.radius;   // px a metre

        // The neighbours and the horn, at the same scale, cut off at the scene's edge.
        {
            juce::Graphics::ScopedSaveState keep (g);
            g.reduceClipRegion (sc.toNearestInt());
            g.setColour (theme::bezel);
            g.fillRoundedRectangle (sc, 4.0f);
            for (size_t i = 1; i < cab.drivers.size(); ++i)
            {
                const auto d = cab.drivers[i] - cab.drivers.front();
                g.setColour (theme::violet.withAlpha (0.25f));
                g.drawEllipse (juce::Rectangle<float> (2 * R, 2 * R).withCentre (o + juce::Point<float> (d.x * s, -d.y * s)), 1.0f);
            }
            if (cab.horn)
            {
                const auto d = cab.hornAt - cab.drivers.front();
                g.setColour (setup_.a.horn ? theme::lilac.withAlpha (0.6f) : theme::txFaint.withAlpha (0.3f));
                g.drawRoundedRectangle (juce::Rectangle<float> (0.10f * s, 0.05f * s).withCentre (o + juce::Point<float> (d.x * s, -d.y * s)), 3.0f, 1.0f);
            }

            // The speaker, flat: its surround, its cone in rings, its cap.
            g.setColour (theme::panel2);
            g.fillEllipse (juce::Rectangle<float> (2.12f * R, 2.12f * R).withCentre (o));
            g.setGradientFill (juce::ColourGradient (theme::panel, o.x, o.y, theme::bezel, o.x + R, o.y, true));
            g.fillEllipse (juce::Rectangle<float> (2 * R, 2 * R).withCentre (o));
            g.setColour (theme::violet.withAlpha (0.8f));
            g.drawEllipse (juce::Rectangle<float> (2 * R, 2 * R).withCentre (o), 1.2f);
            g.setColour (theme::violet.withAlpha (0.18f));
            for (float k : { 0.45f, 0.7f, 0.88f })
                g.drawEllipse (juce::Rectangle<float> (2 * k * R, 2 * k * R).withCentre (o), 0.8f);
            g.setColour (theme::panel2.brighter (0.15f));
            g.fillEllipse (juce::Rectangle<float> (0.5f * R, 0.5f * R).withCentre (o));
            g.setColour (theme::violet.withAlpha (0.6f));
            g.drawEllipse (juce::Rectangle<float> (0.5f * R, 0.5f * R).withCentre (o), 1.0f);

            // The mics in perspective, the farther from the cone drawn last (nearest the eye): its shadow on the cone,
            // then the glyph, its capsule over the spot, as tall as it is near the eye, leaning back toward the cap.
            int order[2] { 0, 1 };
            if (mic (1).distance < mic (0).distance) std::swap (order[0], order[1]);
            for (int m : order)
            {
                if (m == 1 && ! heardB())
                    continue;
                const auto& sp = mic (m);
                const float near = nearness (sp.distance);
                const auto at = spotOf (m);
                const auto colour = colourOf (m);

                const float sh = 6.0f + 26.0f * near;
                g.setColour (juce::Colours::black.withAlpha (0.55f - 0.35f * near));
                g.fillEllipse (juce::Rectangle<float> (sh, sh * 0.6f).withCentre (at + juce::Point<float> (10.0f * near, 8.0f * near)));
                g.setColour (colour.withAlpha (0.9f));
                g.fillEllipse (juce::Rectangle<float> (5, 5).withCentre (at));

                const float h = 30.0f + 70.0f * near, w = h * 24.0f / 64.0f;
                const float side = at.x >= o.x ? 1.0f : -1.0f;   // leaning back from the cap: its top away from the centre
                const float lean = side * sp.angle * juce::MathConstants<float>::pi / 180.0f;
                const auto box = juce::Rectangle<float> (w, h).withPosition (at.x - w * 0.5f, at.y - h * 0.94f);
                paintMicGlyph (g, sp.mic.isNotEmpty() ? sp.mic : cab.mic, box, colour, lean, at);
            }
        }

        // The blend: A..B on a track, ALIGN, and Ø for B.
        {
            const auto row = foot().toFloat();
            g.setColour (theme::txFaint);
            theme::drawTracked (g, "BLEND", row, font, 0.06f, juce::Justification::centredLeft);
            const auto t = blendTrack();
            g.setColour (theme::orange);
            theme::drawTracked (g, "A", juce::Rectangle<float> (t.getX() - 18.0f, row.getY(), 14.0f, row.getHeight()), font, 0.0f, juce::Justification::centred);
            g.setColour (theme::lilac);
            theme::drawTracked (g, "B", juce::Rectangle<float> (t.getRight() + 4.0f, row.getY(), 14.0f, row.getHeight()), font, 0.0f, juce::Justification::centred);
            g.setColour (theme::hair2);
            g.fillRoundedRectangle (t.withSizeKeepingCentre (t.getWidth(), 2.0f), 1.0f);
            g.setColour (theme::orange.interpolatedWith (theme::lilac, setup_.blend));
            g.fillRoundedRectangle (juce::Rectangle<float> (6.0f, 14.0f).withCentre ({ t.getX() + setup_.blend * t.getWidth(), t.getCentreY() }), 2.0f);
            g.setColour (heardB() && setup_.align ? theme::orange : theme::txFaint);
            theme::drawTracked (g, "ALIGN", alignArea().toFloat(), font, 0.06f, juce::Justification::centredLeft);
            g.setColour (heardB() && setup_.invert ? theme::orange : theme::txFaint);
            theme::drawTracked (g, juce::String::fromUTF8 ("\xc3\x98"), invertArea().toFloat(), font, 0.0f, juce::Justification::centred);
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CabPicker)
};

} // namespace orbitamp
