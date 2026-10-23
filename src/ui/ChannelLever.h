// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../face/Controls.h"
#include "Theme.h"

#include <vector>

namespace orbitamp
{

/** THE AMP'S CHANNEL LEVER: one place down a slot for each channel of the device that is playing —
    NORMAL, ALTERED, CRUNCH, OVERDRIVE on an X88-IR, CLEAN, CRUNCH, LEAD on a ReVolt — the way the
    whole family picks a channel. The cockpit has its throttle and the rovers their gear levers;
    OrbitAmp had a row of keys, and a row of keys is a different instrument from the one its siblings
    play.

    HOW IT MOVES is every lever's, said once in the family's kit (face::DetentLever): held, it follows
    the hand and sits IN a place within reach of one, so the hand has to pull it out; let go, it springs
    into the nearest place; the wheel moves it a place at a time. What is OrbitAmp's own is only how it
    is drawn — no render, the face's own ink: a recessed slot in the bezel's black with the panel's
    hairline round it, a tick at every place, a lamp and a name beside each, and a dark grip across the
    slot with the hand's orange as a LINE through it, never a wash (theme.h's rule for orange on the
    violet panel).

    THE CHANNEL CHANGES WHEN THE HAND LETS GO, not as the grip passes a place. A channel is a pack to
    load, and dragging from the first to the last would otherwise load every one in between on the
    way. A click on a name, or the wheel, is one place and is played at once.

    The names read at 13 px, the face's floor for anything read; a name too long for its room keeps
    that size and gives up its end to an ellipsis instead.

    Dumb view: names in, which one plays, a pick out through `onPick`. It knows nothing of packs. Its
    count of places is fixed for its life (DetentLever's), so the block builds a new one when a device
    with a different number of channels comes in. */
class ChannelLever final : public face::DetentLever,
                           public juce::SettableTooltipClient
{
public:
    explicit ChannelLever (int places)
        : DetentLever (juce::jmax (1, places), 0, 260.0f, 22.0f)   // the family's heavy lever: a hair of overshoot
    {
        setRepaintsOnMouseActivity (true);

        // A click on a name, or the wheel: a place, played now. Passing a place while held is not a
        // pick — the release is (below).
        onPlace = [this] (int p)
        {
            if (! isHeld())
                commit (p);
        };

        onRelease = [this] { commit (getPlace()); };
    }

    /** The playing device's channels, by name, and which of them plays. Follows a state that moved
        by itself — a session, a preset, the device menu — so it glides there and reports nothing. */
    void setChannels (const juce::StringArray& newNames, int newLit)
    {
        names = newNames;
        lit   = newLit;
        hover = -1;

        if (juce::isPositiveAndBelow (lit, numPlaces()) && ! isHeld())
            setPlace (lit, isShowing(), false);

        resized();
        repaint();
    }

    int count() const noexcept { return names.size(); }
    const juce::StringArray& channelNames() const noexcept { return names; }

    /** A channel picked, by its place on the lever. */
    std::function<void (int)> onPick;

    /** How wide the lever wants to be for these names at reading size — the slot, the grip's reach,
        the lamp, and the longest name. */
    static float idealWidth (const juce::StringArray& channelNames)
    {
        float w = 0.0f;
        for (const auto& n : channelNames)
            w = juce::jmax (w, theme::trackedWidth (n.toUpperCase(), theme::displayFont (fontH), tracking));
        return namesX + w + 4.0f;
    }

    void resized() override
    {
        // Each name's row is its key: the press that sends the lever there. As tall as the step between
        // two places, never taller than a key of the old row was.
        keys.clear();

        const float h = juce::jmin (keyH, numPlaces() > 1 ? step() : keyH);

        for (int i = 0; i < numPlaces(); ++i)
            keys.push_back ({ lampX - 8.0f, yOf ((float) i) - h * 0.5f,
                              juce::jmax (0.0f, (float) getWidth() - (lampX - 8.0f)), h });
    }

    void paint (juce::Graphics& g) override
    {
        const int n = numPlaces();

        // THE SLOT: a cut in the panel, in the bezel's black, the hairline round it — the wells' own
        // grammar, so it reads as part of the same instrument as the picture beside it.
        const auto slot = juce::Rectangle<float> (slotX - slotW * 0.5f, yOf (0.0f) - slotOver,
                                                  slotW, yOf ((float) (n - 1)) - yOf (0.0f) + 2.0f * slotOver);
        g.setColour (theme::bezel);
        g.fillRoundedRectangle (slot, slotW * 0.5f);
        g.setColour (theme::violet.withAlpha (0.45f));   // a lip a shade brighter than a hairline: a cut, not a mark
        g.drawRoundedRectangle (slot.reduced (0.5f), slotW * 0.5f, 1.2f);

        // A tick across the slot at every place: where the lever can stand.
        for (int i = 0; i < n; ++i)
        {
            g.setColour (theme::hair2);
            g.fillRect (juce::Rectangle<float> (slotX - tickW * 0.5f, yOf ((float) i) - 0.5f, tickW, 1.0f));
        }

        // The names, a lamp beside each: the place the lever stands in lit — a solid of the hand's
        // colour and the name in it — the others a ring of the faint ink and the dimmed name.
        const int here = getPlace();

        for (int i = 0; i < n && i < names.size(); ++i)
        {
            const bool on = i == here;
            const float y = yOf ((float) i);
            const auto lamp = juce::Rectangle<float> (lampD, lampD).withCentre ({ lampX, y });

            if (on)
            {
                g.setColour (theme::orange);
                g.fillEllipse (lamp);
            }
            else
            {
                g.setColour (theme::txFaint);
                g.drawEllipse (lamp.reduced (0.5f), 1.0f);
            }

            const auto row = juce::Rectangle<float> (namesX, y - 10.0f, juce::jmax (0.0f, (float) getWidth() - namesX), 20.0f);
            g.setColour (on ? theme::orange : (i == hover ? theme::tx : theme::txDim));
            theme::drawTracked (g, fitted (names[i].toUpperCase(), row.getWidth()), row,
                                theme::displayFont (fontH), tracking, juce::Justification::centredLeft);
        }

        paintGrip (g);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        // A right click is the block's, not a pull on the lever.
        if (e.mods.isPopupMenu())
            return;

        // On a name: the lever goes there. On the slot: it comes to the hand — near the grip, the grip
        // goes with the hand from where it was taken.
        if (const int k = keyAt (e.position); k >= 0)
        {
            setPlace (k, true);
            return;
        }

        take (std::abs (e.position.y - yOf (travel())) < gripH, e.position.y);
        follow (e.position.y);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (isHeld())
            follow (e.position.y);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (isHeld())
            letGo (travel());
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int k = keyAt (e.position);
        setMouseCursor (k >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::UpDownResizeCursor);
        setHover (k);
    }

    void mouseExit (const juce::MouseEvent&) override { setHover (-1); }

private:
    friend class WebFaceplate;   // the site's kit reads where the parts stand (WebFaceplate.h)

    juce::StringArray names;
    int lit   = -1;
    int hover = -1;
    std::vector<juce::Rectangle<float>> keys;

    // The lever's own space: the slot near the left, the grip reaching either side of it, then a lamp
    // and a name for each place.
    static constexpr float gripW    = 34.0f;
    static constexpr float gripH    = 12.0f;
    static constexpr float slotW    = 12.0f;
    static constexpr float slotOver = 8.0f;     // how far the slot runs past its first and last place
    static constexpr float tickW    = 22.0f;
    static constexpr float slotX    = gripW * 0.5f + 1.0f;
    static constexpr float lampD    = 6.0f;
    static constexpr float lampX    = gripW + 12.0f;
    static constexpr float namesX   = lampX + 10.0f;
    static constexpr float fontH    = 13.0f;
    static constexpr float tracking = 0.1f;
    static constexpr float keyH     = 24.0f;
    static constexpr float edge     = slotOver + 2.0f;   // the first and last place, in from the ends

    float step() const noexcept
    {
        return numPlaces() > 1 ? ((float) getHeight() - 2.0f * edge) / (float) (numPlaces() - 1) : 0.0f;
    }

    // The places run evenly from the top to the bottom of the room it was given — as tall as the
    // picture beside it, however many channels the device has. One place stands in the middle.
    float yOf (float t) const noexcept override
    {
        return numPlaces() > 1 ? edge + step() * t : (float) getHeight() * 0.5f;
    }

    float travelAt (float y) const noexcept override
    {
        return numPlaces() > 1 ? juce::jlimit (0.0f, (float) (numPlaces() - 1), (y - edge) / step()) : 0.0f;
    }

    /** The grip: a dark bar across the slot, lit from above like a knob's cap, with the hand's orange
        drawn through it as one line — brighter under the hand. */
    void paintGrip (juce::Graphics& g) const
    {
        const float y = yOf (travel());
        const auto bar = juce::Rectangle<float> (gripW, gripH).withCentre ({ slotX, y });

        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (bar.translated (0.0f, 2.0f).expanded (1.0f), gripH * 0.5f + 1.0f);

        juce::ColourGradient body (juce::Colour (0xff2c2a38), 0.0f, bar.getY(),
                                   juce::Colour (0xff111018), 0.0f, bar.getBottom(), false);
        g.setGradientFill (body);
        g.fillRoundedRectangle (bar, gripH * 0.5f);

        const bool handled = isHeld() || (isMouseOver() && hover < 0);
        g.setColour (handled ? theme::orange.withAlpha (0.8f) : theme::hair2);
        g.drawRoundedRectangle (bar.reduced (0.5f), gripH * 0.5f, 1.0f);

        g.setColour (handled ? theme::orange.brighter (0.3f) : theme::orange);
        g.fillRect (juce::Rectangle<float> (bar.getWidth() - 10.0f, 1.5f).withCentre (bar.getCentre()));
    }

    /** What a release or a press settles on: played if it is not what plays already. A pick the
        block refused leaves the lever where it was not — so it glides back. */
    void commit (int p)
    {
        if (p != lit && onPick != nullptr)
            onPick (p);

        if (p != lit && juce::isPositiveAndBelow (lit, numPlaces()))
            setPlace (lit, true, false);
    }

    int keyAt (juce::Point<float> p) const
    {
        for (int i = 0; i < (int) keys.size(); ++i)
            if (keys[(size_t) i].contains (p))
                return i;
        return -1;
    }

    void setHover (int i)
    {
        if (i != hover)
        {
            hover = i;
            repaint();
        }
    }

    /** The name, its end traded for an ellipsis until it fits — never a smaller type. */
    static juce::String fitted (juce::String text, float width)
    {
        const auto font = theme::displayFont (fontH);
        const auto dots = juce::String::charToString ((juce::juce_wchar) 0x2026);

        while (text.length() > 2 && theme::trackedWidth (text, font, tracking) > width)
            text = text.dropLastCharacters (text.endsWith (dots) ? 2 : 1).trimEnd() + dots;

        return text;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelLever)
};

} // namespace orbitamp
