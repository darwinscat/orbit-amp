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

/** THE BASS'S CABINETS, DRAWN — one panel, the boxes at the left and the chosen one's speaker at the right.

    THE PILE: a box of each size, no maker's name over them (the owner, 05.11), to scale and stacked as a backline stands
    them, the small on the big; each with its speakers, its horn and its port where it has them; under them the chosen
    box's size, and DOWNLOAD IR beside it, the cabinet that plays saved as a file; over them its BACK, CLOSED, PORTED or
    OPEN, each the box drawn cut through from the side, a click each — a box comes with its own until the player turns
    one, and the one turned goes on with every box after, as the mics do (the owner, 05.11); a sealed one turned
    ported gets a port at its foot. Every bass of the family offers the same ten (the owner, 05.11): none knows a
    cabinet of its own.
    A click on a box picks it, the mics staying where they stand, and the horn too — but a box whose horn comes off
    (Cab::hornOn, the SVT-15E's and the 8x10's) takes it off, and the next box puts it back; a click on the chosen box's
    horn turns the horn off and on, its step kept.

    THE SPEAKER: the chosen box's first speaker, flat, its neighbours at its edge, its horn big in the corner on the horn's
    side — the speaker moved away from it — and beside it HORN's attenuator, OFF -12 -6 0; the mics in perspective on
    the speaker, dragged across it and just past its rim; their two columns either side, each its mic, its places (CAP,
    EDGE, OFF and ROOM, one click each), distance and angle; BLEND, ALIGN and Ø under it, and two buttons: DEFAULT, the
    box's own mic back at its cap — SURE? at the first click, done at the second — and CLOSE. A mic's row clicked again
    while both are heard turns that mic off where it stands, its column dark, BLEND left where it is and not heard while
    one mic is (the owner, 04.11); a click in the dark column brings it back; the last one stays. While the hand moves,
    the cabinet is heard as it goes (onMove) — each position the worker reaches next, a tenth of a second for one mic,
    near a second for two on the 8x10 — and what is set is kept when the hand lets go (onSetup).

    THE RESPONSE, across the panel under it all: what plays, 30 Hz to 12 kHz — the cabinet heard in orange, following
    a moving hand, and the one kept before it a faint grey ghost, so every click and every drag reads as before and
    after; with two mics heard, green under it where they add in phase and red where they cancel.

    A dumb view: the cabinets in (cabs(), geometry from tools/cabgen/cabinets.txt — metres, the baffle's centre the
    origin, up positive), what plays in (show, showResponse), a pick out (onSetup). */
class CabPicker final : public juce::Component
{
public:
    CabPicker() = default;

    /** The size it is drawn for — small enough to open over a cabin's face too. */
    static constexpr int panelW = 906, panelH = 384;

    struct Spot
    {
        float x = 0.0f, y = 0.0f, distance = 0.05f;   // across the first speaker in radii from the cap; in front of it, m
        bool horn = true;
        juce::String mic;                             // one of mics(), or empty for the cabinet's own
        float angle = 0.0f;                           // degrees it leans back toward the cap
        float hornDb = 0.0f;                          // the horn's attenuator while it is on: 0, -6, -12 (A's counts)
    };

    /** Two mics on the box (AmpProcessor::CabSetup), each on or off: both on, B mixed into A by `blend` on one clock
        unless `align`, B turned over by `invert`; one on, that one alone, and `blend` not heard. The horn is A's. ALIGN
        from the start: two mics at two distances, unaligned, comb — rarely what a second mic is put up for. */
    struct Setup
    {
        Spot a, b { 0.0f, 0.0f, 0.25f, true, "u87" };
        float blend = 0.5f;
        bool align = true, invert = false;
        bool aOn = true, bOn = false;
        int back = -1;   // the box's back as the player turned it — an index of backWords — or -1, the box's own (ownBack)
    };

    /** The mics to choose from, as the table names them (its "mic-<name>" sections), and as the columns write them. */
    static const juce::StringArray& mics()      { static const juce::StringArray m { "md421", "sm57", "re20", "d112", "u87", "u47", "ribbon" }; return m; }
    static const juce::StringArray& micWords()  { static const juce::StringArray m { "MD421", "SM57", "RE20", "D112", "U87", "U47", "RIBB" }; return m; }

    struct Cab
    {
        juce::String name;                       // as the table names it: "SVT-410HE"
        int family = 0;                          // whose box it is, for its corners: 1 the rounded ones
        float w = 0.6f, h = 0.6f;                // the baffle
        std::vector<juce::Point<float>> drivers; // the first is the one the mic stands at
        float radius = 0.105f;                   // a cone's radiating radius
        bool horn = false;
        juce::Point<float> hornAt;
        bool port = false;
        juce::Point<float> portAt;
        juce::String mic = "md421";              // the one its table's cap section stands
        bool hornOn = true;                      // how its horn comes when the box is picked: the SVT-15E's and the 8x10's off
    };

    static const std::vector<Cab>& cabs()
    {
        using P = juce::Point<float>;
        static const std::vector<Cab> all {
            { "SVT-15E",    0, 0.61f, 0.56f, { P (0, 0.02f) }, 0.165f, true, P (0.22f, 0.20f), true, P (0, -0.22f), "re20", false },
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
              0.105f, true, P (0, 0.565f), false, {}, "re20", false },
            { "VB-112",     1, 0.52f, 0.40f, { P (-0.09f, 0) }, 0.13f, true, P (0.17f, 0.08f), true, P (0.17f, -0.12f) },
            { "VB-115",     1, 0.56f, 0.58f, { P (0, -0.04f) }, 0.165f, true, P (0.18f, 0.19f), true, P (0, -0.25f), "d112" },
            { "VB-210",     1, 0.56f, 0.42f, { P (-0.135f, -0.02f), P (0.135f, -0.02f) }, 0.105f, true, P (0, 0.13f), true, P (0, -0.17f) },
            { "VB-212",     1, 0.63f, 0.48f, { P (-0.155f, -0.02f), P (0.155f, -0.02f) }, 0.13f, true, P (0, 0.16f), true, P (0, -0.19f) },
            { "VB-410",     1, 0.58f, 0.66f, { P (-0.14f, 0.145f), P (0.14f, 0.145f), P (-0.14f, -0.145f), P (0.14f, -0.145f) },
              0.105f, true, P (0, 0), true, P (0, -0.30f) },
            { "VB-88",      1, 0.56f, 0.98f, { P (-0.13f, 0.33f), P (0.13f, 0.33f), P (-0.13f, 0.11f), P (0.13f, 0.11f),
                                                P (-0.13f, -0.11f), P (0.13f, -0.11f), P (-0.13f, -0.33f), P (0.13f, -0.33f) },
              0.085f, true, P (0, 0), true, P (0, -0.45f) },
            { "OBC115",     2, 0.60f, 0.66f, { P (0, 0.03f) }, 0.165f, false, {}, false, {}, "d112" },
            { "OBC210",     2, 0.60f, 0.40f, { P (-0.14f, 0), P (0.14f, 0) }, 0.105f, false, {}, false, {} },
            { "OBC410",     2, 0.64f, 0.64f, { P (-0.15f, 0.15f), P (0.15f, 0.15f), P (-0.15f, -0.15f), P (0.15f, -0.15f) },
              0.105f, false, {}, false, {} },
            { "OBC810",     2, 0.64f, 1.24f, { P (-0.15f, 0.43f), P (0.15f, 0.43f), P (-0.15f, 0.145f), P (0.15f, 0.145f),
                                                P (-0.15f, -0.145f), P (0.15f, -0.145f), P (-0.15f, -0.43f), P (0.15f, -0.43f) },
              0.105f, false, {}, false, {}, "re20" },
            { "STD-104HF",  3, 0.59f, 0.60f, { P (-0.14f, 0.16f), P (0.14f, 0.16f), P (-0.14f, -0.10f), P (0.14f, -0.10f) },
              0.105f, true, P (0, 0.03f), true, P (0, -0.265f) },
            { "STD-102HF",  3, 0.59f, 0.475f, { P (-0.14f, -0.03f), P (0.14f, -0.03f) }, 0.105f, true, P (0, 0.16f), true, P (0, -0.20f) },
            { "STD-151HR",  3, 0.59f, 0.65f, { P (0, -0.04f) }, 0.165f, true, P (0, 0.23f), false, {}, "d112" },
            { "NY-604",     3, 0.40f, 0.40f, { P (-0.09f, 0.07f), P (0.09f, 0.07f), P (-0.09f, -0.11f), P (0.09f, -0.11f) },
              0.06f, true, P (0, 0.165f), false, {} },
            { "NY-122",     3, 0.72f, 0.39f, { P (-0.17f, -0.01f), P (0.17f, -0.01f) }, 0.13f, true, P (0, 0.14f), false, {} },
            { "SUBWAY-112", 4, 0.49f, 0.39f, { P (-0.06f, -0.01f) }, 0.13f, true, P (0.17f, 0.10f), true, P (0.17f, -0.13f) },
            { "SUBWAY-115", 4, 0.49f, 0.49f, { P (0, -0.03f) }, 0.165f, true, P (0.17f, 0.19f), true, P (-0.17f, -0.21f), "d112" },
            { "SUBWAY-210", 4, 0.49f, 0.53f, { P (-0.11f, 0.12f), P (0.11f, -0.11f) }, 0.105f, true, P (0.13f, 0.17f), true, P (-0.13f, -0.20f) },
            { "SUBWAY-212", 4, 0.50f, 0.72f, { P (0, 0.13f), P (0, -0.19f) }, 0.13f, true, P (0, 0.31f), true, P (0.17f, -0.30f) },
            { "SUBWAY-410", 4, 0.56f, 0.58f, { P (-0.13f, 0.15f), P (0.13f, 0.15f), P (-0.13f, -0.11f), P (0.13f, -0.11f) },
              0.105f, true, P (0, 0.02f), true, P (0, -0.26f) },
            { "SUBWAY-215", 4, 0.49f, 0.87f, { P (0, 0.20f), P (0, -0.16f) }, 0.165f, true, P (0, 0.40f), true, P (0, -0.38f), "d112" },
        };
        return all;
    }

    /** The boxes it offers, as they stand: a box of each size, no maker's name over them (the owner, 05.11), its columns
        left to right, each from the floor up — the small on the big, the tall ones alone, the way a backline is stacked.
        Every size the table holds — 1x15, 1x12, 8x10, 4x10, 2x10, 6x10, 2x12, 2x15, 4x6, 8x8 — the same for every bass
        of the family (the owner, 05.11). The table's other boxes stay in cabs(): a state that kept one still reads as
        its size. */
    static const std::vector<juce::StringArray>& pile()
    {
        static const std::vector<juce::StringArray> every {
            { "SVT-15E", "VB-112" }, { "SVT-810" }, { "SVT-410HE", "VB-210" }, { "SVT-610HLF", "VB-212" },
            { "SUBWAY-215", "NY-604" }, { "VB-88" } };
        return every;
    }

    /** A BOX AS THE PLAYER READS IT: its speakers and their size — "4x10" — and no maker's model (the owner, 05.11). The
        table's name stays the key a state keeps. */
    static juce::String wordOf (int cab)
    {
        const auto& c = cabs()[(size_t) cab];
        const int inches = c.radius > 0.15f ? 15 : c.radius > 0.12f ? 12 : c.radius > 0.095f ? 10 : c.radius > 0.07f ? 8 : 6;
        return juce::String ((int) c.drivers.size()) + "x" + juce::String (inches);
    }

    /** The boxes the table calls sealed (tools/cabgen/cabinets.txt, `alignment`); every other one is ported. */
    static bool sealed (const juce::String& name)
    {
        static const juce::StringArray closed { "SVT-210AV", "SVT-410HE", "SVT-810", "OBC115", "OBC210", "OBC410", "OBC810" };
        return closed.contains (name);
    }

    /** THE BOX'S BACK, as its switch is marked — a click each, the one it stands at in the block's orange; the index the
        engine's (core::cabsynth::Back). A box comes with its own (ownBack) until one is turned, and that one stays from
        box to box; turned, a sealed one gets a port at its foot. */
    static constexpr const char* backWords[] { "CLOSED", "PORTED", "OPEN" };
    static constexpr int backs = 3;
    static int ownBack (int cab) { return sealed (cabs()[(size_t) cab].name) ? 0 : 1; }
    static int backOf (int cab, const Setup& s) { return s.back >= 0 ? s.back : ownBack (cab); }

    /** Where a box's port is drawn: the table's, or the one a sealed box is given when turned ported — in the middle of
        its foot (core::cabsynth::portAtFoot). */
    static juce::Point<float> portOf (const Cab& c) { return c.port ? c.portAt : juce::Point<float> (0.0f, -0.5f * c.h + 0.045f); }

    /** THE MICS A BOX IS PICKED WITH, `from` the one that was playing: where they stood — the same cap, the same
        centimetres, the way a cabinet is compared — only a mic never chosen following the box's own; the horn as it
        stood, unless the two boxes come with theirs otherwise (Cab::hornOn): the SVT-15E's and the 8x10's go off, the
        next one's on — and the back as it was turned, or the box's own where none was. A click in the pile picks by it. */
    static Setup pickedWith (int from, int to, Setup s)
    {
        if (from < 0 || cabs()[(size_t) from].hornOn != cabs()[(size_t) to].hornOn)
            s.a.horn = cabs()[(size_t) to].hornOn;
        if (s.a.mic.isEmpty())
            s.a.mic = cabs()[(size_t) to].mic;
        return s;
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
        repaint();
    }

    std::function<void (int cab, Setup setup)> onSetup;

    /** The hand still moving: what it would sound like there, heard and not kept — onSetup follows when it lets go. */
    std::function<void (int cab, Setup setup)> onMove;

    /** CLOSE: whoever opened the panel takes it away. No button without it. */
    std::function<void()> onClose;

    /** DOWNLOAD IR: whoever opened the panel saves the cabinet that plays as a file. No button without it. */
    std::function<void()> onDownload;

    /** THE RESPONSE of what plays, in dB at responseHz — `now` the cabinet heard, `before` what was kept before it
        (empty, none): each level to its own middle, so the two read as shapes; and with two mics heard `comb`, where
        they meet: dB above nought where they add in phase, below where they cancel (empty, one mic). */
    static constexpr int responsePoints = 240;
    static float responseHz (int i) { return 30.0f * std::pow (12000.0f / 30.0f, (float) i / (float) (responsePoints - 1)); }

    void showResponse (std::vector<float> now, std::vector<float> before, std::vector<float> comb = {})
    {
        responseNow = std::move (now);
        responseBefore = std::move (before);
        responseComb = std::move (comb);
        repaint (responseArea());
    }


    void paint (juce::Graphics& g) override
    {
        paintPile (g);
        g.setColour (theme::hair);
        const float x = (float) leftW + 0.5f * gutter;
        g.fillRect (juce::Rectangle<float> (x, 0.0f, 1.0f, (float) bodyH()));
        g.fillRect (juce::Rectangle<float> (x, (float) (getHeight() - footH), 1.0f, (float) footH));
        paintSpeaker (g);
        paintResponse (g);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int was = hover, wasDist = distHover;
        hover = cabAt (e.position);
        distHover = distHoverAt (e.position);
        if (hover != was || distHover != wasDist)
            repaint();
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        hover = -1;
        if (! dragging)
            distHover = -1;
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (defaultArmed && ! defaultArea().contains (e.getPosition()))
            disarmDefault();   // a click anywhere else: DEFAULT's question withdrawn

        if (e.position.x < (float) leftW)
            pileDown (e);
        else if (chosen >= 0)
            speakerDown (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (chosen < 0 || drag == Drag::none)
            return;
        auto& m = mic (dragMic);
        if (drag == Drag::spot)
        {
            const auto o = coneCentre();
            const float R = radius();
            auto p = (e.position - o) / R;
            const float len = p.getDistanceFromOrigin();
            if (len > maxReach)
                p *= maxReach / len;
            m.x = p.x;
            m.y = -p.y;
        }
        else if (drag == Drag::distance)
        {
            const auto t = dragTrack;
            m.distance = minCm * std::pow (maxCm / minCm, juce::jlimit (0.0f, 1.0f, (t.getBottom() - e.position.y) / t.getHeight())) / 100.0f;
        }
        else if (drag == Drag::angle)
            m.angle = maxAngle * along (angleTrack (dragMic), e.position.x);
        else if (drag == Drag::blend)
            setup_.blend = along (blendTrack(), e.position.x);
        repaint();
        if (onMove != nullptr)
            onMove (chosen, setup_);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (drag != Drag::none)
        {
            drag = Drag::none;
            dragging = false;
            distHover = distHoverAt (e.position);   // a slider out stays only while the hand is still on it
            send();
        }
    }

private:
    static constexpr float maxReach = 1.1f;    // how far from the cap the mic may go, in radii: just past the rim, in sight
    static constexpr float minCm = 1.0f, maxCm = 30.0f, maxAngle = 60.0f;
    static constexpr int leftW = 460, gutter = 14;   // the pile's half; the hair between the halves
    static constexpr int footH = 26, responseH = 104;  // the foot's row under it all; the response's band over it

    /** Down to the response's band: the pile, the columns and the speaker over it, the feet under it. */
    int bodyH() const { return getHeight() - footH - responseH; }

    enum class Drag { none, spot, distance, angle, blend };

    int chosen = -1, hover = -1;
    int distHover = -1;                 // the mic whose distance the hand is over, on the speaker: its slider out
    juce::Rectangle<float> dragTrack;   // the distance track a drag began on: the column's, or the one out on the speaker
    Setup setup_;
    bool dragging = false;
    Drag drag = Drag::none;
    int dragMic = 0;
    bool defaultArmed = false;   // DEFAULT asked SURE? and waits for its second click
    int armings = 0;             // which asking a timer's disarm belongs to

    void disarmDefault()
    {
        if (! defaultArmed)
            return;
        defaultArmed = false;
        repaint();
    }

    Spot& mic (int m) { return m == 0 ? setup_.a : setup_.b; }
    const Spot& mic (int m) const { return m == 0 ? setup_.a : setup_.b; }

    void send()
    {
        repaint();
        if (onSetup != nullptr && chosen >= 0)
            onSetup (chosen, setup_);
    }

    void toggleHorn()
    {
        setup_.a.horn = ! setup_.a.horn;
        send();
    }

    // ---------------------------------------------------------------- the pile
    // Over it the chosen box's back, in the room the pile leaves there (the owner, 05.11); under it the box's size.
    static constexpr int backTop = 6, backIconH = 40, backWordH = 16, nameH = footH;
    static constexpr int pileTop = backTop + backIconH + 2 + backWordH + 6;

    juce::Rectangle<int> pileArea() const { return { 0, pileTop, leftW, bodyH() - pileTop - 2 }; }

    /** The line under the pile: the chosen box's name, or the one under the hand; at its right DOWNLOAD IR. */
    juce::Rectangle<int> nameRow() const { return { 0, getHeight() - nameH + 4, leftW, nameH - 4 }; }

    juce::Rectangle<int> downloadArea() const
    {
        if (onDownload == nullptr || chosen < 0)
            return {};
        const int w = wordW ("DOWNLOAD IR") + 14;
        return nameRow().removeFromRight (w);
    }

    /** THE BACK over the pile, centred: BACK, then a cell each — the box cut through from the side (paintBack), its word
        under it. */
    static constexpr float backCellW = 84.0f, backCellGap = 18.0f, backLabelGap = 16.0f;

    juce::Rectangle<float> backWordArea() const
    {
        const float label = theme::trackedWidth ("BACK", theme::displayFont (13.0f), 0.08f) + 4.0f;
        const float total = label + backLabelGap + (float) backs * backCellW + (float) (backs - 1) * backCellGap;
        return { 0.5f * ((float) leftW - total), (float) backTop, label, (float) backIconH };
    }

    juce::Rectangle<float> backCell (int i) const
    {
        const float x = backWordArea().getRight() + backLabelGap + (float) i * (backCellW + backCellGap);
        return { x, (float) backTop, backCellW, (float) (backIconH + 2 + backWordH) };
    }

    /** A BOX CUT THROUGH FROM THE SIDE, as its back is: the baffle at the left with the speaker on it in profile, its cone
        narrowing in to the magnet, the top and the bottom, and at the right the back — whole (CLOSED); whole, the port's
        tube in from the baffle under the speaker (PORTED); gone but for a lip at the top, the sound out of it (OPEN). */
    static void paintBack (juce::Graphics& g, int kind, juce::Rectangle<float> b, juce::Colour colour)
    {
        const float x0 = b.getX(), x1 = b.getRight(), y0 = b.getY(), y1 = b.getBottom(), w = b.getWidth(), h = b.getHeight();
        const float line = 1.2f, baffle = 1.8f;
        g.setColour (colour);
        g.drawLine (x0, y0, x1, y0, line);
        g.drawLine (x0, y1, x1, y1, line);

        const float portTop = y1 - 0.30f * h, portBottom = y1 - 0.12f * h;
        if (kind == 1)
        {
            g.drawLine (x0, y0, x0, portTop, baffle);
            g.drawLine (x0, portBottom, x0, y1, baffle);
            g.drawLine (x0, portTop, x0 + 0.45f * w, portTop, line);
            g.drawLine (x0, portBottom, x0 + 0.45f * w, portBottom, line);
        }
        else
            g.drawLine (x0, y0, x0, y1, baffle);

        if (kind != 2)
            g.drawLine (x1, y0, x1, y1, line);
        else
        {
            g.drawLine (x1, y0, x1, y0 + 0.22f * h, line);
            juce::Path out;
            for (const float r : { 5.0f, 10.0f })
                out.addCentredArc (x1 - 1.0f, y0 + 0.62f * h, r, r, 0.0f, 0.5f * juce::MathConstants<float>::pi - 0.7f,
                                   0.5f * juce::MathConstants<float>::pi + 0.7f, true);
            g.strokePath (out, juce::PathStrokeType (line));
        }

        const float cy = kind == 1 ? y0 + 0.40f * h : b.getCentreY(), half = 0.27f * h;
        juce::Path cone;
        cone.startNewSubPath (x0, cy - half);
        cone.lineTo (x0 + 0.22f * w, cy - 0.09f * h);
        cone.lineTo (x0 + 0.22f * w, cy + 0.09f * h);
        cone.lineTo (x0, cy + half);
        g.strokePath (cone, juce::PathStrokeType (line));
        g.fillRect (juce::Rectangle<float> (0.10f * w, 0.18f * h).withPosition (x0 + 0.22f * w, cy - 0.09f * h));
    }

    /** The chosen box's horn over its speaker, big, always in the scene's top right corner, wherever it stands on the
        box — a horn that moves with the box jumps under the eye (the owner, 05.11); none where the box has none. */
    juce::Rectangle<float> hornCorner() const
    {
        if (chosen < 0 || ! cabs()[(size_t) chosen].horn)
            return {};
        const auto sc = scene();
        const float w = 0.30f * sc.getWidth(), h = 0.54f * w;
        return { sc.getRight() - 6.0f - w, sc.getY() + 6.0f, w, h };
    }

    /** HORN's attenuator, as a cabinet's switch is marked: OFF, -12, -6, 0 — a click each. */
    static constexpr const char* hornWords[] { "OFF", "-12", "-6", "0" };
    static constexpr float hornSteps[] { 0.0f, -12.0f, -6.0f, 0.0f };   // dB; OFF takes the horn out instead
    static constexpr float hornGap = 8.0f;

    int hornStep() const
    {
        if (! setup_.a.horn)
            return 0;
        return setup_.a.hornDb < -9.0f ? 1 : setup_.a.hornDb < -3.0f ? 2 : 3;
    }

    /** Left of the horn, two lines: HORN, then its steps — flush to the horn's side. */
    juce::Rectangle<float> hornBlock() const
    {
        const auto hc = hornCorner();
        const auto font = theme::displayFont (13.0f);
        float w = 3.0f * hornGap;
        for (const auto* word : hornWords)
            w += theme::trackedWidth (word, font, 0.06f) + 4.0f;
        return { hc.getX() - 8.0f - w, hc.getCentreY() - 15.0f, w, 30.0f };
    }

    juce::Rectangle<float> hornWordArea() const { return hornBlock().withHeight (14.0f); }

    juce::Rectangle<float> hornStepArea (int i) const
    {
        const auto font = theme::displayFont (13.0f);
        const auto block = hornBlock();
        float x = block.getX();
        for (int k = 0; k < i; ++k)
            x += theme::trackedWidth (hornWords[k], font, 0.06f) + 4.0f + hornGap;
        return { x, block.getBottom() - 14.0f, theme::trackedWidth (hornWords[i], font, 0.06f) + 4.0f, 14.0f };
    }

    /** The speaker's centre: the scene's, moved down under a horn, so the horn has its room. */
    juce::Point<float> coneCentre() const
    {
        const auto sc = scene();
        if (chosen < 0 || ! cabs()[(size_t) chosen].horn)
            return sc.getCentre();
        const float shift = juce::jlimit (0.0f, 28.0f, 0.5f * (sc.getHeight() - 2.12f * radius()) - 4.0f);
        return sc.getCentre().translated (0.0f, shift);
    }

    static constexpr float columnGapM = 0.04f;   // between a pile's columns, m
    static constexpr float stackGapPx = 2.0f;    // between a box and the one on it

    /** One scale for the whole pile, so a box is as big as itself against the others: its width and its tallest column
        fitted to the area. */
    float pileScale() const
    {
        const auto area = pileArea().toFloat();
        float w = 0, h = 0;
        for (const auto& column : pile())
        {
            float cw = 0, ch = 0;
            for (const auto& name : column)
            {
                const auto& c = cabs()[(size_t) indexOf (name)];
                cw = juce::jmax (cw, c.w);
                ch += c.h;
            }
            w += cw;
            h = juce::jmax (h, ch);
        }
        w += columnGapM * (float) (pile().size() - 1);
        return juce::jmin ((area.getWidth() - 8.0f) / w, (area.getHeight() - 8.0f) / h);
    }

    /** Each box, to scale, standing as pile() stacks them, the pile centred on its floor. */
    std::vector<std::pair<int, juce::Rectangle<float>>> layOut() const
    {
        const auto area = pileArea().toFloat();
        const float s = pileScale();
        const auto& columns = pile();

        float width = columnGapM * (float) (columns.size() - 1);
        for (const auto& column : columns)
        {
            float cw = 0;
            for (const auto& name : column)
                cw = juce::jmax (cw, cabs()[(size_t) indexOf (name)].w);
            width += cw;
        }

        std::vector<std::pair<int, juce::Rectangle<float>>> out;
        float x = area.getCentreX() - 0.5f * width * s;
        const float floor = area.getBottom() - 2.0f;
        for (const auto& column : columns)
        {
            float cw = 0;
            for (const auto& name : column)
                cw = juce::jmax (cw, cabs()[(size_t) indexOf (name)].w);
            float bottom = floor;
            for (const auto& name : column)
            {
                const int i = indexOf (name);
                const auto& c = cabs()[(size_t) i];
                out.push_back ({ i, { x + 0.5f * (cw - c.w) * s, bottom - c.h * s, c.w * s, c.h * s } });
                bottom -= c.h * s + stackGapPx;
            }
            x += (cw + columnGapM) * s;
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
            if (box.expanded (1.0f).contains (p))
                return i;
        return -1;
    }

    static juce::Point<float> onBox (juce::Rectangle<float> box, float s, juce::Point<float> p)
    {
        return { box.getCentreX() + p.x * s, box.getCentreY() - p.y * s };
    }

    void pileDown (const juce::MouseEvent& e)
    {
        if (downloadArea().contains (e.getPosition()))
        {
            onDownload();
            return;
        }

        // The chosen box's back, a cell each: kept as turned, its own one too, so every box after comes with it.
        if (chosen >= 0)
            for (int i = 0; i < backs; ++i)
                if (backCell (i).expanded (backCellGap / 2, 2.0f).contains (e.position))
                {
                    setup_.back = i;
                    send();
                    return;
                }

        const int at = cabAt (e.position);
        if (at < 0)
            return;

        if (at != chosen)
        {
            setup_ = pickedWith (chosen, at, setup_);
            chosen = at;
            send();
            return;
        }

        // The chosen box again: its horn turned over where the hand found it.
        const auto& cab = cabs()[(size_t) at];
        const auto box = boxOf (at);
        const float s = box.getWidth() / cab.w;
        if (cab.horn && e.position.getDistanceFrom (onBox (box, s, cab.hornAt)) < juce::jmax (7.0f, 0.06f * s))
            toggleHorn();
    }

    /** A HF horn as it faces the player: its mouth a rounded rectangle, its walls narrowing to the throat, the driver a
        dark slot at the bottom of it — `s` px a metre; dim when it is turned off. */
    static void paintHorn (juce::Graphics& g, juce::Point<float> c, float s, juce::Colour colour)
    {
        // A tunnel: the mouth, the flare narrowing inside it a little above the centre, the throat dark at its end.
        const auto mouth  = juce::Rectangle<float> (0.12f * s, 0.065f * s).withCentre (c);
        const auto flare  = juce::Rectangle<float> (0.075f * s, 0.038f * s).withCentre (c.translated (0, -0.004f * s));
        const auto throat = juce::Rectangle<float> (0.03f * s, 0.016f * s).withCentre (c.translated (0, -0.007f * s));
        const float line = juce::jlimit (0.7f, 1.2f, 0.01f * s);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRoundedRectangle (mouth, 0.014f * s);
        g.setColour (colour.withMultipliedAlpha (0.10f));
        g.fillRoundedRectangle (mouth.reduced (line), 0.012f * s);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (flare, 0.009f * s);
        g.setColour (juce::Colours::black);
        g.fillRoundedRectangle (throat, 0.005f * s);
        g.setColour (colour.withMultipliedAlpha (0.45f));
        g.drawRoundedRectangle (flare, 0.009f * s, line * 0.7f);
        g.setColour (colour.withMultipliedAlpha (0.8f));
        g.drawRoundedRectangle (throat, 0.005f * s, line * 0.7f);
        g.setColour (colour);
        g.drawRoundedRectangle (mouth, 0.014f * s, line);
    }

    void paintPile (juce::Graphics& g) const
    {
        const auto font = theme::displayFont (13.0f);
        const auto area = pileArea();
        g.setColour (theme::hair);
        g.fillRect (juce::Rectangle<float> ((float) area.getX(), (float) area.getBottom() - 1.0f, (float) area.getWidth(), 1.0f));
        // The back over the pile kept apart from it, as the halves are, by a hair across up to theirs (the owner, 05.11).
        g.fillRect (juce::Rectangle<float> (0.0f, (float) pileTop - 3.0f, (float) leftW + 0.5f * gutter, 1.0f));

        for (const auto& [index, box] : layOut())
            paintCab (g, cabs()[(size_t) index], box, setup_, index == chosen, index == hover);

        // Under the pile: the box under the hand, else the chosen one.
        const auto row = nameRow().toFloat();
        const int named = hover >= 0 ? hover : chosen;
        if (named >= 0)
        {
            g.setColour (named == chosen ? theme::tx : theme::txDim);
            theme::drawTracked (g, wordOf (named), row, font, 0.06f, juce::Justification::centredLeft);
        }

        paintButton (g, "DOWNLOAD IR", downloadArea(), false);

        // Over the pile the chosen box's back: BACK, then each drawn with its word, the one it stands at in the block's
        // orange line.
        if (chosen >= 0)
        {
            g.setColour (theme::txFaint);
            theme::drawTracked (g, "BACK", backWordArea(), font, 0.08f, juce::Justification::centredLeft);
            for (int i = 0; i < backs; ++i)
            {
                const bool on = i == backOf (chosen, setup_);
                auto cell = backCell (i);
                const auto word = cell.removeFromBottom ((float) backWordH);
                paintBack (g, i, cell.withHeight ((float) backIconH).withSizeKeepingCentre (56.0f, 34.0f),
                           on ? theme::orange : theme::violet.withAlpha (0.6f));
                g.setColour (on ? theme::orange : theme::txDim);
                theme::drawTracked (g, backWords[i], word, font, 0.06f, juce::Justification::centred);
            }
        }
    }

public:
    /** One box as the pile draws it, `setup` its mics and horn when it is the chosen one, framed in the block's orange
        unless `plainFrame`; its tolex `body` opaque — the block's picture draws it over its IR (paintSketch) with the IR
        showing through, in a plain frame: alone there, it has nothing to be told from. */
    static void paintCab (juce::Graphics& g, const Cab& c, juce::Rectangle<float> box, const Setup& setup, bool isChosen, bool isHover,
                          float body = 1.0f, bool plainFrame = false)
    {
        const float s = box.getWidth() / c.w;

        // The box: the tolex, a grille's hint, its outline — the chosen one in the block's orange line.
        g.setColour (theme::panel2.withMultipliedAlpha (body));
        g.fillRoundedRectangle (box, c.family == 1 ? 4.0f : 2.0f);
        g.setColour (theme::bezel.withMultipliedAlpha (body));
        g.fillRoundedRectangle (box.reduced (juce::jmax (1.5f, 0.025f * s)), 1.5f);
        const bool orange = isChosen && ! plainFrame;
        g.setColour (orange ? theme::orange : isHover ? theme::violet.withAlpha (0.7f) : theme::hair2);
        g.drawRoundedRectangle (box, c.family == 1 ? 4.0f : 2.0f, orange ? 1.5f : 1.0f);

        // The port where the back the chosen one is turned to has one: none closed, at its foot on a sealed box ported.
        if (const int back = isChosen ? setup.back : -1; back < 0 || back == (sealed (c.name) ? 0 : 1) ? c.port : back == 1)
        {
            const auto at = portOf (c);
            g.setColour (juce::Colours::black);
            const float w = juce::jmin (0.20f, 2.0f * (0.5f * c.w - std::abs (at.x)) - 0.04f);
            g.fillRoundedRectangle (juce::Rectangle<float> (w * s, juce::jmax (2.0f, 0.035f * s)).withCentre (onBox (box, s, at)), 1.0f);
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
            paintHorn (g, onBox (box, s, c.hornAt), s,
                       isChosen && ! setup.a.horn ? theme::txFaint.withAlpha (0.4f) : theme::lilac.withAlpha (isChosen ? 0.9f : 0.55f));
        }

        // The mics where they stand on the first speaker, A orange and B lilac.
        if (isChosen)
        {
            const auto o = onBox (box, s, c.drivers.front());
            const float r = c.radius * s;
            for (int m = 1; m >= 0; --m)
            {
                if (! (m == 0 ? setup.aOn : setup.bOn))
                    continue;
                const auto& sp = m == 0 ? setup.a : setup.b;
                g.setColour (m == 0 ? theme::orange : theme::lilac);
                g.fillEllipse (juce::Rectangle<float> (5, 5).withCentre (o + juce::Point<float> (sp.x * r, -sp.y * r)));
            }
        }
    }

    /** A speaker, flat as the player faces it — its surround, its cone in rings, its cap — `R` its radius; its paper
        `body` opaque (the sketch over the IR lets the IR through). */
    static void paintCone (juce::Graphics& g, juce::Point<float> o, float R, float body = 1.0f)
    {
        g.setColour (theme::panel2.withMultipliedAlpha (body));
        g.fillEllipse (juce::Rectangle<float> (2.12f * R, 2.12f * R).withCentre (o));
        g.setGradientFill (juce::ColourGradient (theme::panel.withMultipliedAlpha (body), o.x, o.y, theme::bezel.withMultipliedAlpha (body), o.x + R, o.y, true));
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
    }

    /** The mics on a speaker drawn at `o`, radius `R`, in perspective, the farther from the cone drawn last (nearest the
        eye): its shadow on the cone, then the glyph, its capsule over the spot, as tall as it is near the eye, leaning back
        toward the cap; each only while it is on. `k` the scene's size, the panel's 1. */
    static void paintMicsOn (juce::Graphics& g, const Cab& cab, const Setup& setup, juce::Point<float> o, float R, float k)
    {
        int order[2] { 0, 1 };
        if (setup.b.distance < setup.a.distance) std::swap (order[0], order[1]);
        for (int m : order)
        {
            if (! (m == 0 ? setup.aOn : setup.bOn))
                continue;
            const auto& sp = m == 0 ? setup.a : setup.b;
            const float near = nearness (sp.distance);
            const auto at = o + juce::Point<float> (sp.x * R, -sp.y * R);
            const auto colour = colourOf (m);

            const float sh = (6.0f + 26.0f * near) * k;
            g.setColour (juce::Colours::black.withAlpha (0.55f - 0.35f * near));
            g.fillEllipse (juce::Rectangle<float> (sh, sh * 0.6f).withCentre (at + juce::Point<float> (10.0f * near, 8.0f * near) * k));
            g.setColour (colour.withAlpha (0.9f));
            g.fillEllipse (juce::Rectangle<float> (5, 5).withCentre (at));

            const float h = (30.0f + 70.0f * near) * k, w = h * 36.0f / 96.0f;
            const float side = at.x >= o.x ? 1.0f : -1.0f;   // leaning back from the cap: its top away from the centre
            const float lean = side * sp.angle * juce::MathConstants<float>::pi / 180.0f;
            const auto box = juce::Rectangle<float> (w, h).withPosition (at.x - w * 0.5f, at.y - h * 0.94f);
            paintMicGlyph (g, sp.mic.isNotEmpty() ? sp.mic : cab.mic, box, colour, lean, at);
        }
    }

    /** THE CABINET THAT PLAYS, SMALL, in `area` (the band of the IR's picture between its words): its speaker at the
        right edge, the mics on it as the panel's scene stands them (a tall one over the band's top); the box beside it on
        the band's floor, its mics' dots on its first speaker, every box at one scale — the tallest as tall as the band — so an 8x10 stands
        taller than a 2x10. The bass's CAB block draws it over its IR (CabinetBlock), the IR showing through the tolex. */
    static void paintSketch (juce::Graphics& g, int cab, const Setup& setup, juce::Rectangle<float> area)
    {
        if (cab < 0 || cab >= (int) cabs().size())
            return;

        const auto& c = cabs()[(size_t) cab];
        const float R = juce::jmax (8.0f, juce::jmin (0.45f * area.getHeight(), 0.09f * area.getWidth() + 18.0f));
        const juce::Point<float> o (area.getRight() - R - 28.0f, area.getCentreY());   // clear of the picture's dB marks

        static const auto tallest = [] { float h = 0.0f, w = 0.0f; for (const auto& b : cabs()) { h = juce::jmax (h, b.h); w = juce::jmax (w, b.w); } return juce::Point<float> (w, h); }();
        const float right = o.x - R - 10.0f;
        const float s = juce::jmin (area.getHeight() / tallest.y, 0.3f * area.getWidth() / tallest.x);
        paintCab (g, c, juce::Rectangle<float> (c.w * s, c.h * s).withPosition (right - c.w * s, area.getBottom() - c.h * s),   // on the floor
                  setup, true, false, 0.55f, true);

        paintCone (g, o, R, 0.55f);
        paintMicsOn (g, c, setup, o, R, R / 80.0f);   // the panel's speaker is about 80 px across its radius
    }

private:

    // ---------------------------------------------------------------- the speaker
    // Its mics in two columns, A at the left in the block's orange and B at the right in lilac, B's a mirror of A's —
    // each its list of mics, its distance standing beside the list, its places and its angle; between them the speaker,
    // flat, as the player faces it, and on it the mics in perspective: the nearer the cone, the smaller (the farther
    // from the eye), a shadow on the cone that grows and fades with the distance, the glyph leaning back toward the cap
    // by the angle, its centimetres under it — a slider out at the hand. BLEND right under the speaker, ALIGN and Ø
    // after it (the owner, 05.11).
    static constexpr int colW = 108, rowH = 18, edge = 6;   // edge: B's column off the panel's

    static juce::Colour colourOf (int m) { return m == 0 ? theme::orange : theme::lilac; }

    int rightX() const { return leftW + gutter; }

    juce::Rectangle<int> column (int m) const
    {
        const int h = bodyH() - blendH;   // down to BLEND's row
        return m == 0 ? juce::Rectangle<int> (rightX(), 0, colW, h) : juce::Rectangle<int> (getWidth() - edge - colW, 0, colW, h);
    }

    juce::Rectangle<int> micRow (int m, int i) const
    {
        const auto c = column (m);
        return { c.getX(), c.getY() + rowH + 4 + i * rowH, c.getWidth(), rowH };
    }

    /** A column's places under its mics, two to a line: CAP EDGE, OFF ROOM. */
    juce::Rectangle<int> placeArea (int m, int i) const
    {
        const auto c = column (m);
        const int top = micRow (m, mics().size() - 1).getBottom() + 4;
        return { c.getX() + 4 + (i % 2) * (c.getWidth() / 2), top + (i / 2) * rowH, c.getWidth() / 2 - 4, rowH };
    }

    /** A column's angle: a word and its value over a track. */
    juce::Rectangle<float> angleTrack (int m) const
    {
        const auto c = column (m);
        const int top = placeArea (m, 3).getBottom() + 6;
        return juce::Rectangle<float> ((float) c.getX() + (m == 0 ? 4.0f : 8.0f), (float) (top + rowH + 2), (float) c.getWidth() - 12.0f, 6.0f);
    }

    /** A mic's distance in its column, along its list of mics and no lower (the owner, 05.11) — over EDGE's GE in A's,
        over CAP's CA in B's, where the names leave room — its centimetres level with the column's name, far at the top. */
    float distX (int m) const
    {
        const auto font = theme::displayFont (13.0f);
        const auto width = [&font] (const juce::String& word) { return theme::trackedWidth (word, font, 0.06f); };
        float longest = 0.0f;   // ...and clear of the longest name by a little
        for (const auto& word : micWords())
            longest = juce::jmax (longest, width (word));
        const auto c = column (m).toFloat();
        if (m == 0)
            return juce::jmax ((float) placeArea (0, 1).getX() + width ("EDGE") - 0.5f * width ("GE"), c.getX() + 20.0f + longest + 14.0f);
        return juce::jmin ((float) placeArea (1, 0).getRight() - 4.0f - width ("CAP") + 0.5f * width ("CA"), c.getRight() - 20.0f - longest - 14.0f);
    }

    juce::Rectangle<int> distArea (int m) const   // the hand's: the value over the track and the track
    {
        const int top = column (m).getY(), bottom = micRow (m, mics().size() - 1).getBottom();
        return { juce::roundToInt (distX (m)) - 12, top, 24, bottom - top };
    }

    juce::Rectangle<float> distTrack (int m) const
    {
        const float top = (float) micRow (m, 0).getY() + 4.0f, bottom = (float) micRow (m, mics().size() - 1).getBottom() - 4.0f;
        return { distX (m) - 3.0f, top, 6.0f, bottom - top };
    }

    static juce::String cmOf (float distance) { return juce::String (juce::roundToInt (distance * 100.0f)) + " cm"; }

    /** ...and by the mic on the speaker, under its spot; the hand over it brings a slider out beside it (the owner, 05.11). */
    juce::Rectangle<float> distLabel (int m) const
    {
        const auto at = spotOf (m);
        const float w = theme::trackedWidth (cmOf (mic (m).distance), theme::displayFont (13.0f), 0.0f) + 8.0f;
        return { at.x - 0.5f * w, at.y + 8.0f, w, 16.0f };
    }

    juce::Rectangle<float> distPopup (int m) const
    {
        const auto at = spotOf (m);
        const auto sc = scene();
        const float h = 110.0f;
        const float y = juce::jlimit (sc.getY() + 8.0f, sc.getBottom() - 8.0f - h, at.y + 16.0f - 0.5f * h);
        float x = at.x + 34.0f;   // right of the value, or left where the scene ends
        if (x + 15.0f > sc.getRight())
            x = at.x - 40.0f;
        return { x, y, 6.0f, h };
    }

    /** The mic whose value or slider the hand is over — a slider once out stays while the hand is on its way to it. */
    int distHoverAt (juce::Point<float> p) const
    {
        if (chosen < 0)
            return -1;
        for (int m = 0; m < 2; ++m)
            if (heard (m) && (distLabel (m).contains (p)
                              || (distHover == m && distPopup (m).expanded (12.0f, 8.0f).getUnion (distLabel (m)).contains (p))))
                return m;
        return -1;
    }

    static constexpr int blendH = 26;   // BLEND's row, right under the speaker

    juce::Rectangle<float> scene() const
    {
        const int x0 = column (0).getRight() + 8, x1 = column (1).getX() - 8;
        return juce::Rectangle<int> (x0, 4, x1 - x0, bodyH() - 4 - blendH).toFloat();
    }

    float radius() const { const auto s = scene(); return juce::jmax (10.0f, juce::jmin (s.getWidth(), s.getHeight()) * 0.5f / 1.2f); }

    /** Under the speaker (the owner, 05.11): BLEND, A, its track as wide as the speaker's scene, B — ALIGN and Ø after. */
    juce::Rectangle<int> blendRow() const
    {
        const int y = scene().toNearestInt().getBottom() + 2;
        return { rightX(), y, getWidth() - rightX(), bodyH() - y };
    }

    juce::Rectangle<int> foot() const { return { rightX(), getHeight() - footH + 4, getWidth() - rightX(), footH - 4 }; }

    /** The foot: CLOSE and DEFAULT, buttons, at the right. */
    static int wordW (const char* word) { return juce::roundToInt (theme::trackedWidth (word, theme::displayFont (13.0f), 0.06f)) + 4; }

    juce::Rectangle<int> closeArea() const
    {
        const int w = onClose != nullptr ? wordW ("CLOSE") + 14 : 0;
        return { getWidth() - w, foot().getY(), w, foot().getHeight() };
    }

    juce::Rectangle<int> defaultArea() const
    {
        const int w = wordW ("DEFAULT") + 14;
        return { closeArea().getX() - (onClose != nullptr ? 6 : 0) - w, foot().getY(), w, foot().getHeight() };
    }

    juce::Rectangle<int> alignArea() const { return { column (1).getX(), blendRow().getY(), wordW ("ALIGN"), blendRow().getHeight() }; }

    juce::Rectangle<int> invertArea() const { return { alignArea().getRight() + 8, blendRow().getY(), 20, blendRow().getHeight() }; }

    juce::Rectangle<float> blendTrack() const
    {
        const auto sc = scene();
        return { sc.getX() + 18.0f, blendRow().toFloat().getCentreY() - 3.0f, sc.getWidth() - 36.0f, 6.0f };
    }

    juce::Rectangle<float> blendWordArea() const
    {
        const float w = theme::trackedWidth ("BLEND", theme::displayFont (13.0f), 0.06f) + 4.0f;
        const auto row = blendRow().toFloat();
        return { blendTrack().getX() - 22.0f - w, row.getY(), w, row.getHeight() };
    }

    /** The places a mic is put at with one click, as the table's positions stand them: at the cap, at the cone's edge,
        half way out and turned 35° to the cap, and as far off as it goes. Across the cone on the mic's own side. */
    struct Place { const char* word; float r, distance, angle; };
    static constexpr Place places[] { { "CAP", 0.0f, 0.05f, 0.0f }, { "EDGE", 0.8f, 0.04f, 0.0f },
                                      { "OFF", 0.5f, 0.05f, 35.0f }, { "ROOM", 0.0f, 0.30f, 0.0f } };
    void putAt (int m, const Place& p)
    {
        takeUp (m);
        auto& sp = mic (m);
        float side = sp.x > 0.05f ? 1.0f : sp.x < -0.05f ? -1.0f : 0.0f;
        if (side == 0.0f)   // on the axis: the side away from the other mic, while that one is on
            side = mic (1 - m).x > 0.0f && heard (1 - m) ? -1.0f : 1.0f;
        sp.x = side * p.r;
        sp.y = 0.0f;
        sp.distance = p.distance;
        sp.angle = p.angle;
        send();
    }

    bool standsAt (const Spot& sp, const Place& p) const
    {
        return std::abs (std::abs (sp.x) - p.r) < 0.02f && std::abs (sp.y) < 0.02f
            && std::abs (sp.distance - p.distance) < 0.004f && std::abs (sp.angle - p.angle) < 1.0f;
    }

    static float along (juce::Rectangle<float> t, float x) { return juce::jlimit (0.0f, 1.0f, (x - t.getX()) / t.getWidth()); }

    /** Where a mic stands on the cone, on the screen. */
    juce::Point<float> spotOf (int m) const
    {
        const auto o = coneCentre();
        const float R = radius();
        return o + juce::Point<float> (mic (m).x * R, -mic (m).y * R);
    }

    static float nearness (float distance)   // 0 at the cone, 1 at 30 cm
    {
        return std::log (juce::jlimit (minCm, maxCm, distance * 100.0f) / minCm) / std::log (maxCm / minCm);
    }

    bool heard (int m) const { return m == 0 ? setup_.aOn : setup_.bOn; }
    bool bothHeard() const { return setup_.aOn && setup_.bOn; }

    /** A dark column taken up: its mic heard again where it stood, BLEND where it was left. B taken up the first time
        stands on the other side of the cone from A, not on top of it, where it was only waiting. */
    void takeUp (int m)
    {
        if (heard (m))
            return;
        if (m == 0)
        {
            setup_.aOn = true;
            return;
        }
        setup_.bOn = true;
        if (std::hypot (setup_.b.x - setup_.a.x, setup_.b.y - setup_.a.y) < 0.4f)
        {
            setup_.b.x = setup_.a.x >= 0.0f ? -0.6f : 0.6f;
            setup_.b.y = 0.0f;
        }
    }

    void speakerDown (const juce::MouseEvent& e)
    {
        const auto pos = e.getPosition();
        if (! hornCorner().isEmpty())
            for (int i = 0; i < 4; ++i)
                if (hornStepArea (i).expanded (hornGap / 2, 3.0f).contains (e.position))
                {
                    setup_.a.horn = i > 0;
                    if (i > 0)
                        setup_.a.hornDb = hornSteps[i];
                    send();
                    return;
                }

        // A distance: the slider out on the speaker, its value there (the slider's to come), or the column's.
        for (int m = 0; m < 2; ++m)
        {
            if (distHover == m && distPopup (m).expanded (9.0f, 8.0f).contains (e.position))
            {
                drag = Drag::distance;
                dragTrack = distPopup (m);
                dragMic = m;
                dragging = true;
                mouseDrag (e);
                return;
            }
            if (heard (m) && distLabel (m).contains (e.position))
                return;
        }
        for (int m = 0; m < 2; ++m)
            if (distArea (m).contains (pos))
            {
                takeUp (m);
                drag = Drag::distance;
                dragTrack = distTrack (m);
                dragMic = m;
                dragging = true;
                mouseDrag (e);
                return;
            }

        for (int m = 0; m < 2; ++m)
        {
            if (! column (m).contains (pos))
                continue;
            for (int i = 0; i < 4; ++i)
                if (placeArea (m, i).contains (pos))
                {
                    putAt (m, places[i]);
                    return;
                }
            for (int i = 0; i < mics().size(); ++i)
                if (micRow (m, i).contains (pos))
                {
                    // The lit one clicked again, with both mics heard: that mic goes off where it stands, the other
                    // stays where it is, BLEND too. The last mic never goes.
                    const auto& own = mic (m).mic.isNotEmpty() ? mic (m).mic : cabs()[(size_t) chosen].mic;
                    if (bothHeard() && mics()[i] == own)
                    {
                        (m == 0 ? setup_.aOn : setup_.bOn) = false;
                        send();
                        return;
                    }
                    takeUp (m);
                    mic (m).mic = mics()[i];
                    send();
                    return;
                }
            if (angleTrack (m).expanded (2.0f, 10.0f).contains (e.position))
            {
                takeUp (m);
                drag = Drag::angle;
                dragMic = m;
                dragging = true;
                mouseDrag (e);
                return;
            }
            return;
        }

        if (closeArea().contains (pos) && onClose != nullptr)
        {
            onClose();
            return;
        }
        // DEFAULT asks first: the first click turns it into SURE? for three seconds, the second resets.
        if (defaultArea().contains (pos))
        {
            if (! defaultArmed)
            {
                defaultArmed = true;
                repaint();
                juce::Timer::callAfterDelay (3000, [safe = juce::Component::SafePointer<CabPicker> (this), armedAt = ++armings]
                                                   { if (safe != nullptr && safe->armings == armedAt) safe->disarmDefault(); });
                return;
            }
            disarmDefault();
            setup_ = {};
            setup_.a.mic = cabs()[(size_t) chosen].mic;
            setup_.a.horn = cabs()[(size_t) chosen].hornOn;
            send();
            return;
        }
        if (alignArea().expanded (4, 2).contains (pos)) { setup_.align = ! setup_.align; send(); return; }
        if (invertArea().expanded (4, 2).contains (pos)) { setup_.invert = ! setup_.invert; send(); return; }
        if (bothHeard() && blendTrack().expanded (8.0f, 8.0f).contains (e.position))
        {
            drag = Drag::blend;
            dragging = true;
            mouseDrag (e);
            return;
        }

        if (scene().contains (e.position))
        {
            // The nearer mic of the two heard is the one taken; with one heard, that one.
            int m = heard (0) ? 0 : 1;
            if (bothHeard() && e.position.getDistanceFrom (spotOf (1)) < e.position.getDistanceFrom (spotOf (0)))
                m = 1;
            drag = Drag::spot;
            dragMic = m;
            dragging = true;
            mouseDrag (e);
        }
    }

    static void paintMicGlyph (juce::Graphics& g, const juce::String& name, juce::Rectangle<float> box, juce::Colour colour, float lean,
                               juce::Point<float> pivot)
    {
        if (const auto* d = micGlyph (name))
        {
            auto copy = d->createCopy();
            tintGlyph (*copy, colour);
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
        const bool live = heard (m);
        const float dim = live ? 1.0f : 0.45f;
        const auto& sp = mic (m);
        const auto& cab = cabs()[(size_t) chosen];
        const auto heardBy = sp.mic.isNotEmpty() ? sp.mic : cab.mic;

        // B's a mirror of A's: its words at the right, each mic's glyph after its name (the owner, 05.11).
        const auto side = m == 0 ? juce::Justification::centredLeft : juce::Justification::centredRight;
        g.setColour (colour.withMultipliedAlpha (dim));
        theme::drawTracked (g, m == 0 ? "MIC A" : "MIC B", c.toFloat().withHeight ((float) rowH), font, 0.08f, side);

        for (int i = 0; i < mics().size(); ++i)
        {
            auto r = micRow (m, i).toFloat();
            const bool lit = mics()[i] == heardBy;
            const auto tint = (lit ? colour : theme::txFaint).withMultipliedAlpha (dim);
            const auto glyph = m == 0 ? r.removeFromLeft (12.0f) : r.removeFromRight (12.0f);
            paintMicGlyph (g, mics()[i], glyph.reduced (0, 1), tint, 0.0f, {});
            g.setColour (tint);
            theme::drawTracked (g, micWords()[i], m == 0 ? r.withTrimmedLeft (8.0f) : r.withTrimmedRight (8.0f), font, 0.06f, side);
        }

        // Its places, in its colour where it stands.
        for (int i = 0; i < 4; ++i)
        {
            g.setColour ((standsAt (sp, places[i]) ? colour : theme::txDim).withMultipliedAlpha (dim));
            auto cell = placeArea (m, i).toFloat();
            theme::drawTracked (g, places[i].word, m == 0 ? cell : cell.withTrimmedRight (4.0f), font, 0.06f, side);
        }

        // Its angle, a word and its value over a track — the value on the side away from the word.
        {
            const auto t = angleTrack (m);
            const auto label = juce::Rectangle<float> (t.getX(), t.getY() - (float) rowH - 2.0f, t.getWidth(), (float) rowH);
            g.setColour (theme::txFaint.withMultipliedAlpha (dim));
            theme::drawTracked (g, "ANGLE", label, font, 0.06f, side);
            g.setColour (theme::txDim.withMultipliedAlpha (dim));
            theme::drawTracked (g, juce::String (juce::roundToInt (sp.angle)) + juce::String::fromUTF8 ("\xc2\xb0"), label, font, 0.0f,
                                m == 0 ? juce::Justification::centredRight : juce::Justification::centredLeft);
            g.setColour (theme::hair2);
            g.fillRoundedRectangle (t.withSizeKeepingCentre (t.getWidth(), 2.0f), 1.0f);
            g.setColour (colour.withMultipliedAlpha (dim));
            g.fillRoundedRectangle (juce::Rectangle<float> (6.0f, 12.0f).withCentre ({ t.getX() + sp.angle / maxAngle * t.getWidth(), t.getCentreY() }), 2.0f);
        }

        // Its distance, beside its list: its centimetres level with the column's name, far at the top.
        {
            const auto t = distTrack (m);
            g.setColour (theme::txDim.withMultipliedAlpha (dim));
            theme::drawTracked (g, cmOf (sp.distance), { t.getCentreX() - 24.0f, (float) c.getY(), 48.0f, (float) rowH }, font, 0.0f,
                                juce::Justification::centred);
            g.setColour (theme::hair2);
            g.fillRoundedRectangle (t.withSizeKeepingCentre (2.0f, t.getHeight()), 1.0f);
            g.setColour (colour.withMultipliedAlpha (dim));
            g.fillRoundedRectangle (juce::Rectangle<float> (12.0f, 6.0f).withCentre ({ t.getCentreX(), t.getBottom() - nearness (sp.distance) * t.getHeight() }), 2.0f);
        }
    }

    void paintSpeaker (juce::Graphics& g) const
    {
        const auto font = theme::displayFont (13.0f);
        if (chosen < 0)
        {
            // An IR of the player's own plays (or the factory's): the speaker waits for a box.
            g.setColour (theme::txFaint);
            theme::drawTracked (g, "CHOOSE A CABINET", juce::Rectangle<int> (rightX(), 0, getWidth() - rightX(), bodyH()).toFloat(),
                                font, 0.08f, juce::Justification::centred);
            return;
        }

        const auto& cab = cabs()[(size_t) chosen];
        paintColumn (g, 0);
        paintColumn (g, 1);

        const auto sc = scene();
        const float R = radius();
        const auto o = coneCentre();
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
            // The horn, big, in its corner; beside it HORN and its steps, the one it stands at in the block's orange.
            if (const auto hc = hornCorner(); ! hc.isEmpty())
            {
                paintHorn (g, hc.getCentre(), hc.getWidth() / 0.12f, setup_.a.horn ? theme::lilac.withAlpha (0.8f) : theme::txFaint.withAlpha (0.35f));
                const auto side = juce::Justification::centredRight;
                g.setColour (theme::txFaint);
                theme::drawTracked (g, "HORN", hornWordArea(), font, 0.08f, side);
                for (int i = 0; i < 4; ++i)
                {
                    g.setColour (i == hornStep() ? theme::orange : theme::txDim);
                    theme::drawTracked (g, hornWords[i], hornStepArea (i), font, 0.06f, juce::Justification::centred);
                }
            }

            paintCone (g, o, R);
            paintMicsOn (g, cab, setup_, o, R, 1.0f);

            // Each heard mic's centimetres under it; the one the hand is over with its slider out beside it.
            for (int m = 0; m < 2; ++m)
            {
                if (! heard (m))
                    continue;
                g.setColour (colourOf (m));
                theme::drawTracked (g, cmOf (mic (m).distance), distLabel (m), font, 0.0f, juce::Justification::centred);
                if (distHover != m && ! (drag == Drag::distance && dragMic == m && dragTrack == distPopup (m)))
                    continue;
                const auto t = distPopup (m);
                const auto back = t.expanded (9.0f, 8.0f);
                g.setColour (theme::panel2.withAlpha (0.95f));
                g.fillRoundedRectangle (back, 4.0f);
                g.setColour (colourOf (m).withAlpha (0.6f));
                g.drawRoundedRectangle (back, 4.0f, 1.0f);
                g.setColour (theme::hair2);
                g.fillRoundedRectangle (t.withSizeKeepingCentre (2.0f, t.getHeight()), 1.0f);
                g.setColour (colourOf (m));
                g.fillRoundedRectangle (juce::Rectangle<float> (12.0f, 6.0f).withCentre ({ t.getCentreX(), t.getBottom() - nearness (mic (m).distance) * t.getHeight() }), 2.0f);
            }
        }

        // The blend: A..B on a track, ALIGN, and Ø for B — faint while one mic is heard, the blend not heard then.
        {
            const float dim = bothHeard() ? 1.0f : 0.45f;
            const auto row = blendRow().toFloat();
            g.setColour (theme::txFaint);
            theme::drawTracked (g, "BLEND", blendWordArea(), font, 0.06f, juce::Justification::centredRight);
            const auto t = blendTrack();
            g.setColour (theme::orange.withMultipliedAlpha (dim));
            theme::drawTracked (g, "A", juce::Rectangle<float> (t.getX() - 18.0f, row.getY(), 14.0f, row.getHeight()), font, 0.0f, juce::Justification::centred);
            g.setColour (theme::lilac.withMultipliedAlpha (dim));
            theme::drawTracked (g, "B", juce::Rectangle<float> (t.getRight() + 4.0f, row.getY(), 14.0f, row.getHeight()), font, 0.0f, juce::Justification::centred);
            g.setColour (theme::hair2);
            g.fillRoundedRectangle (t.withSizeKeepingCentre (t.getWidth(), 2.0f), 1.0f);
            g.setColour (theme::orange.interpolatedWith (theme::lilac, setup_.blend).withMultipliedAlpha (dim));
            g.fillRoundedRectangle (juce::Rectangle<float> (6.0f, 14.0f).withCentre ({ t.getX() + setup_.blend * t.getWidth(), t.getCentreY() }), 2.0f);
            g.setColour (bothHeard() && setup_.align ? theme::orange : theme::txFaint);
            theme::drawTracked (g, "ALIGN", alignArea().toFloat(), font, 0.06f, juce::Justification::centredLeft);
            g.setColour (bothHeard() && setup_.invert ? theme::orange : theme::txFaint);
            theme::drawTracked (g, juce::String::fromUTF8 ("\xc3\x98"), invertArea().toFloat(), font, 0.0f, juce::Justification::centred);
            paintButton (g, defaultArmed ? "SURE?" : "DEFAULT", defaultArea(), defaultArmed);
            paintButton (g, "CLOSE", closeArea(), false);
        }
    }

    /** A button: a box round its word — DEFAULT, CLOSE, DOWNLOAD IR — orange while it asks. */
    static void paintButton (juce::Graphics& g, const char* word, juce::Rectangle<int> r, bool asking)
    {
        if (r.isEmpty())
            return;
        const auto b = r.toFloat().reduced (0.5f, 1.5f);
        g.setColour (theme::panel2);
        g.fillRoundedRectangle (b, 4.0f);
        g.setColour (asking ? theme::orange : theme::txFaint);
        g.drawRoundedRectangle (b, 4.0f, asking ? 1.2f : 1.0f);
        g.setColour (asking ? theme::orange : theme::txDim);
        theme::drawTracked (g, word, b, theme::displayFont (13.0f), 0.06f, juce::Justification::centred);
    }

    // ---------------------------------------------------------------- the response
    // Across the panel under it all: the cabinet's response, 30 Hz to 12 kHz on a log scale, +-18 dB — the one heard in
    // the block's orange line, the one kept before it a faint grey ghost; dB at the left, the frequencies under it.
    std::vector<float> responseNow, responseBefore, responseComb;
    static constexpr float responseDb = 18.0f;

    juce::Rectangle<int> responseArea() const { return { 0, bodyH(), getWidth(), responseH }; }

    juce::Rectangle<float> responsePlot() const
    {
        return responseArea().toFloat().withTrimmedLeft (36.0f).withTrimmedRight (12.0f).withTrimmedTop (9.0f).withTrimmedBottom (20.0f);
    }

    void paintResponse (juce::Graphics& g) const
    {
        const auto font = theme::displayFont (13.0f);
        const auto area = responseArea().toFloat();
        const auto p = responsePlot();
        g.setColour (theme::hair);
        g.fillRect (area.withHeight (1.0f));

        const auto xOf = [p] (float hz) { return p.getX() + p.getWidth() * std::log (hz / 30.0f) / std::log (12000.0f / 30.0f); };
        const auto yOf = [p] (float db) { return p.getCentreY() - db / responseDb * 0.5f * p.getHeight(); };

        // Under it all, with two mics heard, where they meet (the owner, 05.11): green where they add in phase, red
        // where they cancel — the stronger, the deeper, full at 6 dB.
        if ((int) responseComb.size() == responsePoints)
            for (int i = 0; i < responsePoints; ++i)
            {
                const float x0 = i == 0 ? p.getX() : 0.5f * (xOf (responseHz (i - 1)) + xOf (responseHz (i)));
                const float x1 = i == responsePoints - 1 ? p.getRight() : 0.5f * (xOf (responseHz (i)) + xOf (responseHz (i + 1)));
                const float v = responseComb[(size_t) i];
                g.setColour ((v > 0.0f ? juce::Colour (0xff3fae6a) : juce::Colour (0xffd5455a)).withAlpha (0.45f * juce::jmin (1.0f, std::abs (v) / 6.0f)));
                g.fillRect (juce::Rectangle<float> (x0, p.getY(), x1 - x0 + 0.5f, p.getHeight()));
            }

        struct Mark { float hz; const char* word; };
        static constexpr Mark marks[] { { 50.0f, "50" }, { 100.0f, "100" }, { 200.0f, "200" }, { 500.0f, "500" },
                                        { 1000.0f, "1k" }, { 2000.0f, "2k" }, { 5000.0f, "5k" }, { 10000.0f, "10k" } };
        for (const auto& m : marks)
        {
            const float x = xOf (m.hz);
            g.setColour (theme::hair);
            g.fillRect (juce::Rectangle<float> (x, p.getY(), 1.0f, p.getHeight()));
            g.setColour (theme::txFaint);
            theme::drawTracked (g, m.word, { x - 20.0f, p.getBottom() + 3.0f, 40.0f, 16.0f }, font, 0.0f, juce::Justification::centred);
        }
        struct Level { float db; const char* word; };
        static constexpr Level levels[] { { 10.0f, "+10" }, { 0.0f, "0" }, { -10.0f, "-10" } };
        for (const auto& l : levels)
        {
            const float y = yOf (l.db);
            g.setColour (std::abs (l.db) < 1.0f ? theme::hair2 : theme::hair);
            g.fillRect (juce::Rectangle<float> (p.getX(), y, p.getWidth(), 1.0f));
            g.setColour (theme::txFaint);
            theme::drawTracked (g, l.word, { area.getX() + 2.0f, y - 8.0f, 28.0f, 16.0f }, font, 0.0f, juce::Justification::centredRight);
        }

        const auto curve = [&] (const std::vector<float>& db, juce::Colour colour, float width)
        {
            if ((int) db.size() != responsePoints)
                return;
            juce::Path path;
            for (int i = 0; i < responsePoints; ++i)
            {
                // Past the scale it goes out of the frame, as a dip does, not along its floor (the owner, 05.11).
                const juce::Point<float> at (xOf (responseHz (i)), yOf (juce::jlimit (-4.0f * responseDb, 4.0f * responseDb, db[(size_t) i])));
                if (i == 0)
                    path.startNewSubPath (at);
                else
                    path.lineTo (at);
            }
            g.setColour (colour);
            g.strokePath (path, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };
        juce::Graphics::ScopedSaveState keep (g);
        g.reduceClipRegion (p.toNearestInt().expanded (0, 2));
        curve (responseBefore, theme::txDim.withAlpha (0.3f), 1.2f);   // barely there: a second line reads as a second mic
        curve (responseNow, theme::orange, 1.6f);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CabPicker)
};

} // namespace orbitamp
