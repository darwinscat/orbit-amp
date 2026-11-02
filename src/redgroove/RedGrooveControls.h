// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../face/Controls.h"

#include <functional>
#include <vector>

/** THE WALKER'S OWN CONTROLS: what its dashboard has that no other face's console does. Dumb views — each is told
    what it shows and says what the hand did; none of them reaches the processor. */
namespace orbitamp::redgroove
{

/** WORDS IN THEIR OWN CASE — a reading's units, "dB", "Hz", "kHz", which the panel's names in capitals would turn into
    other words — centred in `area`, in the face's ink, its edge under them. */
void writeAsIs (juce::Graphics&, const face::Ink&, const juce::String& words, juce::Rectangle<float> area, float em,
                juce::Colour);

/** A READING UNDER A CONTROL: what the control is set to, in words in their own case (writeAsIs), centred in its box.
    Its owner says the words; it takes no click. */
class Readout final : public juce::Component
{
public:
    Readout (const face::Ink&, float em);

    void set (const juce::String& words);
    juce::Colour colour;

    void paint (juce::Graphics&) override;

private:
    const face::Ink ink;
    const float em;
    juce::String words;
};

/** COMP'S SQUEEZE: a column falling from its top, violet, as deep as COMP is pressing — its whole height `rangeDb` —
    the depth's edge an orange line. Read 30 times a second from `grDb` (0 or below); takes no click. */
class GrColumn final : public juce::Component, private juce::Timer
{
public:
    explicit GrColumn (std::function<float()> grDb);

    static constexpr float rangeDb = 24.0f;

    void paint (juce::Graphics&) override;

private:
    std::function<float()> read;
    float shown = 0.0f;
    void timerCallback() override;
};

/** THE CHANNEL LEVER: the walker's two gears, B15 and SVT, in its slot from the top — a cap across the slot, its
    orange line where it stands, and each place's name where the face puts it, the one it stands in lit. The family's lever
    (face::DetentLever): held, it follows the hand and sits in a place within reach; let go, it springs into the
    nearest; a click on a name sends it there. */
class ChannelLever final : public face::DetentLever
{
public:
    /** Its places' names, top to bottom, at their heights; the slot's middle; and where each name is centred — all in
        the stage's space, which the top left of its box there, `stageOrigin`, makes its own. */
    ChannelLever (const face::Ink&, juce::StringArray names, std::vector<float> heights, float slotX,
                  std::vector<juce::Point<float>> nameCentres, juce::Point<float> stageOrigin);

    juce::Colour handLight;
    static constexpr float capWidth = 44.0f, capHeight = 28.0f;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }

private:
    const face::Ink ink;
    const juce::StringArray names;
    std::vector<float> heights;     // in its own space
    float slotX;                    // ...and this
    std::vector<juce::Point<float>> nameCentres;    // ...and these
    float yOf (float travel) const noexcept override;
    float travelAt (float y) const noexcept override;
    juce::Rectangle<float> capAt (float y) const noexcept;
};

/** A SWITCH ON ONE LINE: its name, then its end positions' words either side of a slot — the one it stands at lit —
    and a cap in the slot at one end, or in its middle for a three-way's middle (`STR 4 ( o) 5`, `U-LO - ( o ) +`).
    The slot is the cuts' slot as the picture paints it, cut out and set where the switch stands; `reversed` shows the
    positions right to left. A click moves it one position toward the side it lands on — a two-way's to the other —
    and a right click asks for its menu. */
class SlideSwitch final : public juce::Component
{
public:
    SlideSwitch (const face::Ink&, juce::String name, juce::String left, juce::String right, int positions = 2,
                 bool reversed = false);

    /** Where the slot is, in its own space, and the slot as the picture paints it. */
    juce::Rectangle<float> slot;
    juce::Image slotPicture;
    float wordsY = 0.0f, wordRoom = 38.0f, nameRoom = 40.0f;
    float em = face::stage::px13;   // its words' size: the column's a size down (the owner, 02.11)
    juce::Colour handLight;

    int  position() const noexcept { return at; }
    void setPosition (int, bool notify);

    std::function<void (int)> onFlip;   // a hand threw it here
    std::function<void()> onMenu;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }

private:
    const face::Ink ink;
    const juce::String name, words[2];
    const int count;
    const bool reversed;
    int at = 0;

    int shown() const noexcept { return reversed ? count - 1 - at : at; }   // where the cap stands, left to right
};

/** THE CABINET'S SCREEN: the name of the cabinet that plays — OFF while it is out of the sound — and a chevron at its
    right end. A click opens the list of the cabinets; a right click their window. Asks for the name four times a
    second, and draws it again when it is another. */
class CabinetScreen final : public juce::Component, private juce::Timer
{
public:
    explicit CabinetScreen (const face::Ink&);

    std::function<juce::String()> name;
    std::function<void()> onClick, onMenu;
    juce::Colour handLight;

    /** What it says now. */
    const juce::String& says() const noexcept { return shown; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { repaint(); }

    /** Asks for the name now, rather than at the next tick. */
    void refresh();

private:
    const face::Ink ink;
    juce::String shown;
    void timerCallback() override { refresh(); }
};

} // namespace orbitamp::redgroove
