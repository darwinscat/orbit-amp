// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

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

    /** What plays: a cabinet of cabs() and its spot — or none (an IR of the player's own, or the factory's). */
    void show (int cab, Spot spot)
    {
        if (dragging)
            return;   // the hand's, until it lets go
        chosen = cab;
        spot_ = spot;
        if (chosen >= 0)
            family = cabs()[(size_t) chosen].family;
        else
            zoomed = false;
        repaint();
    }

    std::function<void (int cab, Spot spot)> onSpot;

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
            spot_ = {};
            spot_.mic = cabs()[(size_t) at].mic;
            send();
            return;
        }

        // The chosen box again: its horn turned over, or its speaker opened.
        const auto& cab = cabs()[(size_t) at];
        const auto box = boxOf (at);
        const float s = box.getWidth() / cab.w;
        if (cab.horn && e.position.getDistanceFrom (onBox (box, s, cab.hornAt)) < juce::jmax (7.0f, 0.06f * s))
        {
            spot_.horn = ! spot_.horn;
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
        if (drag == Drag::mic)
        {
            const auto sp = speakerArea();
            const float R = speakerRadius();
            auto p = (e.position - sp.getCentre()) / R;
            const float len = p.getDistanceFromOrigin();
            if (len > maxReach)
                p *= maxReach / len;
            spot_.x = p.x;
            spot_.y = -p.y;
        }
        else
            spot_.distance = distanceAt (e.position.y);
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
    static constexpr float minCm = 1.0f, maxCm = 30.0f;

    enum class Drag { none, mic, distance };

    int chosen = -1, hover = -1, family = 0;
    Spot spot_;
    bool zoomed = false, dragging = false;
    Drag drag = Drag::none;

    void send()
    {
        repaint();
        if (onSpot != nullptr && chosen >= 0)
            onSpot (chosen, spot_);
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
            g.setColour (isChosen && ! spot_.horn ? theme::txFaint.withAlpha (0.4f) : theme::lilac.withAlpha (isChosen ? 0.9f : 0.55f));
            g.drawRoundedRectangle (r, 1.5f, 1.0f);
        }

        // The mic where it stands on the first speaker; farther off, a ring as wide as the distance.
        if (isChosen)
        {
            const auto o = onBox (box, s, c.drivers.front());
            const float r = c.radius * s;
            const auto at = o + juce::Point<float> (spot_.x * r, -spot_.y * r);
            g.setColour (theme::orange);
            g.fillEllipse (juce::Rectangle<float> (6, 6).withCentre (at));
            if (spot_.distance > 0.08f)
                g.drawEllipse (juce::Rectangle<float> (2, 2).withCentre (at).expanded (spot_.distance * s), 1.0f);
        }
    }

    // ---------------------------------------------------------------- the speaker
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

    juce::Rectangle<float> railArea() const
    {
        auto b = getLocalBounds().withTrimmedTop (24).toFloat();
        return b.removeFromRight (34.0f).reduced (0, 4);
    }

    juce::Rectangle<float> speakerArea() const
    {
        auto b = getLocalBounds().withTrimmedTop (22).withTrimmedBottom (micRowH + 4).toFloat();
        b.removeFromRight (44.0f);
        return b;
    }

    static constexpr int micRowH = 18;

    /** The mics' words along the bottom, each its own cell, the row centred under the speaker. */
    juce::Rectangle<int> micCell (int i) const
    {
        const auto font = theme::displayFont (13.0f);
        const int gapPx = 14;
        int total = 0;
        for (const auto& w : micWords()) total += juce::roundToInt (theme::trackedWidth (w, font, 0.06f)) + gapPx;
        total -= gapPx;
        int x = juce::jmax (0, (getWidth() - 44 - total) / 2);
        for (int k = 0; k < micWords().size(); ++k)
        {
            const int w = juce::roundToInt (theme::trackedWidth (micWords()[k], font, 0.06f));
            if (k == i) return { x, getHeight() - micRowH, w, micRowH };
            x += w + gapPx;
        }
        return {};
    }

    float speakerRadius() const
    {
        const auto a = speakerArea();
        return juce::jmax (10.0f, juce::jmin (a.getHeight(), a.getWidth()) * 0.5f / 1.25f);
    }

    float railY (float d) const
    {
        const auto r = railArea();
        const float t = std::log (juce::jlimit (minCm, maxCm, d * 100.0f) / minCm) / std::log (maxCm / minCm);
        return r.getBottom() - 16.0f - t * (r.getHeight() - 32.0f);
    }

    float distanceAt (float y) const
    {
        const auto r = railArea();
        const float t = juce::jlimit (0.0f, 1.0f, (r.getBottom() - 16.0f - y) / (r.getHeight() - 32.0f));
        return minCm * std::pow (maxCm / minCm, t) / 100.0f;
    }

    void speakerDown (const juce::MouseEvent& e)
    {
        if (backArea().contains (e.getPosition()))
        {
            zoomed = false;
            repaint();
            return;
        }
        const auto& cab = cabs()[(size_t) chosen];
        if (cab.horn && hornArea().contains (e.getPosition()))
        {
            spot_.horn = ! spot_.horn;
            send();
            return;
        }
        for (int i = 0; i < mics().size(); ++i)
            if (micCell (i).expanded (4, 2).contains (e.getPosition()))
            {
                spot_.mic = mics()[i];
                send();
                return;
            }
        if (railArea().expanded (6, 0).contains (e.position))
        {
            drag = Drag::distance;
            dragging = true;
            mouseDrag (e);
            return;
        }
        const auto sp = speakerArea();
        if (e.position.getDistanceFrom (sp.getCentre()) <= speakerRadius() * (maxReach + 0.1f))
        {
            drag = Drag::mic;
            dragging = true;
            mouseDrag (e);
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
            g.setColour (spot_.horn ? theme::orange : theme::txFaint);
            theme::drawTracked (g, "HORN", hornArea().toFloat(), font, 0.08f, juce::Justification::centredRight);
        }

        const auto sp = speakerArea();
        const float R = speakerRadius();
        const auto o = sp.getCentre();
        const float s = R / cab.radius;   // px a metre

        // The neighbours, at the same scale, where they stand from the first — cut off at the area's edge.
        {
            juce::Graphics::ScopedSaveState keep (g);
            g.reduceClipRegion (sp.toNearestInt());
            g.setColour (theme::bezel);
            g.fillRect (sp);
            for (size_t i = 1; i < cab.drivers.size(); ++i)
            {
                const auto d = cab.drivers[i] - cab.drivers.front();
                const auto c = o + juce::Point<float> (d.x * s, -d.y * s);
                g.setColour (theme::violet.withAlpha (0.25f));
                g.drawEllipse (juce::Rectangle<float> (2 * R, 2 * R).withCentre (c), 1.0f);
            }
            if (cab.horn)
            {
                const auto d = cab.hornAt - cab.drivers.front();
                const auto c = o + juce::Point<float> (d.x * s, -d.y * s);
                g.setColour (spot_.horn ? theme::lilac.withAlpha (0.6f) : theme::txFaint.withAlpha (0.3f));
                g.drawRoundedRectangle (juce::Rectangle<float> (0.10f * s, 0.05f * s).withCentre (c), 3.0f, 1.0f);
            }
        }

        // The speaker: its surround, its cone in rings, its cap.
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

        // The mic: a dot where it stands, a faint ring as wide as its distance — the farther, the more of the box it hears.
        const auto at = o + juce::Point<float> (spot_.x * R, -spot_.y * R);
        g.setColour (theme::orange.withAlpha (0.35f));
        g.drawEllipse (juce::Rectangle<float> (2, 2).withCentre (at).expanded (juce::jmin (2.0f * R, spot_.distance * s)), 1.0f);
        g.setColour (theme::orange);
        g.fillEllipse (juce::Rectangle<float> (10, 10).withCentre (at));
        g.setColour (theme::ground);
        g.fillEllipse (juce::Rectangle<float> (4, 4).withCentre (at));

        // The mics, the one that hears in the block's orange.
        const auto heardBy = spot_.mic.isNotEmpty() ? spot_.mic : cab.mic;
        for (int i = 0; i < mics().size(); ++i)
        {
            g.setColour (mics()[i] == heardBy ? theme::orange : theme::txFaint);
            theme::drawTracked (g, micWords()[i], micCell (i).toFloat(), font, 0.06f, juce::Justification::centred);
        }

        // The distance: a rail, the near end at the bottom, its number under it.
        const auto rail = railArea();
        const float x = rail.getCentreX();
        g.setColour (theme::hair2);
        g.fillRect (juce::Rectangle<float> (x - 1.0f, rail.getY() + 16.0f, 2.0f, rail.getHeight() - 32.0f));
        const float y = railY (spot_.distance);
        g.setColour (theme::orange);
        g.fillRoundedRectangle (juce::Rectangle<float> (18.0f, 6.0f).withCentre ({ x, y }), 2.0f);
        g.setColour (theme::txDim);
        theme::drawTracked (g, juce::String (juce::roundToInt (spot_.distance * 100.0f)) + " cm",
                            juce::Rectangle<float> (rail.getX() - 14.0f, rail.getBottom() - 14.0f, rail.getWidth() + 14.0f, 14.0f),
                            theme::displayFont (13.0f), 0.0f, juce::Justification::centredRight);
        g.setColour (theme::txFaint);
        theme::drawTracked (g, "DIST", juce::Rectangle<float> (rail.getX() - 14.0f, rail.getY() - 2.0f, rail.getWidth() + 14.0f, 14.0f),
                            theme::displayFont (13.0f), 0.0f, juce::Justification::centredRight);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CabPicker)
};

} // namespace orbitamp
