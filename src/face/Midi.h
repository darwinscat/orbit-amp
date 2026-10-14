// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "Controls.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <memory>
#include <vector>

namespace orbitamp::face
{

/** THE CONTROLLERS A FACE RIDES. Three switches — 64 and up is on, anything under it off — and four
    knobs, each over its whole travel. These are the numbers each product's midi.txt gives the player
    (resources/catpit/midi.txt for the cockpit): change one, change the others. */
namespace midi
{
    enum Controller { turbo = 20, hpf, lpf, drive, bass, middle, treble };
}

/** THE MIDI WINDOW'S CONTENT (a face's Sheet shows it): which inputs the product listens to, and what
    it does with what it hears.

    THE INPUTS — every one the machine has, a lamp by each; a click switches one on or off. They are
    the standalone's own: the same inputs, the same choice and the same saved settings as its
    Audio/MIDI window. Inside a host there are none to show — the host routes MIDI itself — so
    available() is false there and the face does not offer the window.

    THE COMMANDS — a text file in the product's resources (resources/catpit/midi.txt), handed in and
    shown as it is written:
    a line with a tab in it is a row of a table, a line that is not a sentence is a heading, the rest
    is prose. COPY puts the file on the clipboard and SAVE writes it out, so the list can go where the
    player keeps the controller's notes. To change what the window says, change the file.

    The two stand side by side, the inputs on the left: a face's window is wide and not tall, and
    a table's rows are set two abreast for the same reason. */
class MidiPanel final : public juce::Component,
                        private juce::Timer
{
public:
    explicit MidiPanel (juce::String commands);

    /** Whether there are inputs to choose at all: only where the product stands alone. */
    static bool available();

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    // The inputs.
    juce::Array<juce::MidiDeviceInfo> inputs;
    juce::Array<bool> listening;
    int hovered = -1;
    int inputsHeight() const;
    juce::Rectangle<int> row (int index) const;
    int rowAt (juce::Point<int>) const;

    // An input plugged in while the window is open joins the list, and one pulled out leaves it.
    void timerCallback() override { refresh(); }
    void refresh();

    // The commands: the file, and the file laid out.
    const juce::String commands;
    struct Line
    {
        enum class Kind { gap, heading, prose, row };
        Kind kind = Kind::gap;
        juce::String text, second;      // a row's two cells
        juce::TextLayout wrapped;       // prose
        int x = 0, y = 0, height = 0;   // where it stands in the commands' column
    };
    std::vector<Line> lines;
    int commandsHeight = 0;
    void layOutCommands();
    int commandsLeft() const;           // where the commands' column begins

    panel::WordKey copy { "Copy" }, save { "Save" };
    std::unique_ptr<juce::FileChooser> chooser;
    void saveCommands();
};

} // namespace orbitamp::face
