// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "Controls.h"
#include "../channels/Channels.h"

#include <functional>

namespace orbitamp::face
{

using channels::Channels;

/** THE CABINETS' WINDOW (a face's Sheet shows it): every cabinet CAB steps through, by name, where
    the slider gives each a few pixels of its travel.

    TWO LISTS THAT NEVER MIX. FACTORY is what the build carries; YOURS is what the player has added —
    dropped on the face, or taken in with ADD. A click on a row plays that cabinet. The rows stand in
    columns, since a face's window is wide and not tall, and when there are more columns than the
    window has room for, the lists scroll sideways.

    THE KEYS under them: ADD takes files in; SAVE hands the cabinet that is playing out as a file; and
    RENAME and REMOVE are for a cabinet of the player's own — the build's are not the player's to
    change, and with one of those playing the two stand faint. BYPASS takes the cabinet out of the
    sound altogether, for every channel, and stands lit while it is out — the lists pale behind it.
    OK, apart from them, shuts the window: a click beside it does too, but nothing on the window says so.

    A dumb view. It reads the channels' list and says what the hand asked for; what a key does is the
    face's to carry out. */
class CabPanel final : public juce::Component,
                       private juce::Timer
{
public:
    /** `room` is the most of the window it may take. */
    CabPanel (const Channels&, juce::Point<int> room);

    std::function<void (int)> onChoose;                         // a row: an index into the channels' cabinets
    std::function<void()> onAdd, onSave, onRename, onRemove;    // the last three act on the one playing
    std::function<void()> onClose;                              // OK
    std::function<void()> onBypass;                             // the cabinet out of the sound, or back in

    /** Reads the list again now, rather than at the next look of its own. */
    void refresh();

private:
    /** The two lists, laid out in columns — as wide as they need, behind a viewport when that is too wide. */
    class Lists final : public juce::Component
    {
    public:
        explicit Lists (CabPanel&);
        void layOut (int rowsToAColumn);

        juce::Rectangle<int> row (int index) const;
        int rowAt (juce::Point<int>) const;

        void paint (juce::Graphics&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;

    private:
        CabPanel& owner;
        int deep = 1;                   // rows to a column
        int wide[2] { 0, 0 };           // a column's width in each list: its longest name's, within reason
        int hovered = -1;
        int columnsOf (int group) const;
        int leftOf (int group) const;
    };

    const Channels& channels;
    const juce::Point<int> room;

    // What the lists were last laid out from.
    juce::Array<juce::File> files;
    int factory = 0, playing = -1;

    Lists lists { *this };
    juce::Viewport view;
    panel::WordKey add { "Add" }, bypass { "Bypass" }, save { "Save" }, rename { "Rename" }, remove { "Remove" }, ok { "OK" };
    bool out = false;               // the cabinet is bypassed

    void timerCallback() override { refresh(); }
};

} // namespace orbitamp::face
