// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../Parameters.h"
#include "../core/CapturedBlock.h"
#include "BlockFrame.h"
#include "BlockMeter.h"
#include "EqSection.h"
#include "scope/DeviceScope.h"
#include "Knob.h"
#include "VSwitch.h"
#include "VoicingSelector.h"

#include <felitronics/appkit/DeviceGlyph.h>

#include <array>
#include <memory>

namespace orbitamp
{

class AmpProcessor;

/** The face every captured block wears — the boost in front, the preamp behind it, and whatever
    captured block comes next. One class, because the question is always the same: which device,
    which capture of it, and what its own controls do. Which BLOCK it is arrives as a reference and
    an id prefix, and nothing else in the file knows.

    The pack says what this device HAS: how many positions its gain was captured at, which of its
    knobs were measured instead, whether one of them is a two-position switch, and what the circuit
    is. SM7 comes out as one big Gain over twenty-one detents, two EQ knobs and a Sharp/Smooth
    switch — nothing in this file names any of that.

    A control slot with nothing behind it is hidden. A knob doing nothing is worse than a gap. */
class CapturedBlockPanel final : public BlockFrame,
                                 private juce::Timer
{
public:
    using Block = core::CapturedBlock;

    /** `eqLink` is which of the two EQ consoles is this block's — they belong to the blocks now,
        and the index is the only thing that still says which is which. */
    CapturedBlockPanel (AmpProcessor&, Block&, const juce::String& title, const char* blockId,
                        int eqLink, felitronics::analysis::RollingSpectrumTap& toneSpectrumTap,
                        felitronics::analysis::RollingSpectrumTap& toneInSpectrumTap);
    ~CapturedBlockPanel() override;

    /** Rebuilds the face from whatever pack is loaded. Called when the device changes. */
    void deviceChanged();

    /** Puts a thrown-open picture back in its corner. Returns whether there was one — the editor
        asks both blocks and stops at the first that answers, so one Escape closes one thing. */
    bool foldPicture();

private:
    void layOutContent (juce::Rectangle<int>) override;
    void paintContent (juce::Graphics&) override;
    void blockOnChanged (bool on) override;

    /** Builds a switch for each selecting control the device has beyond its gain dial. */
    void buildSelectors();

    /** The tone slots alone back to the pack's defaults — the console's RESET, when the block
        wears the device's own knobs. */
    void resetToneSlotsToPackDefaults();

    /** Puts every slot back where the loaded pack says a player starts.

        The measured slots and the selector slots are SLOTS, not controls: `boost_meas1` exists
        always and what it turns is whatever the pack decided. So its value cannot survive a device
        change — 0.7 of a Tube Screamer's Tone and 0.7 of an SM7's EQ-Lo are different physical
        things wearing one number. GAIN is the exception and keeps its place: every device has one
        and it means the same thing on all of them, which is what makes two devices comparable at
        the same dial position. */
    void resetToPackDefaults();

    /** The device's OWN tone controls as console bands — one to three of whatever it happened to
        have, in the order the pack lists them. Empty when it measured nothing, which is what makes
        the block fall back to ours without being told to. */
    std::vector<EqSection::Band> nativeBands() const;

    /** Puts the console into whichever set the mode parameter names, falling to ours when the
        device has no native one to give. */
    void applyEqMode();

    std::unique_ptr<juce::ParameterAttachment> eqModeAttachment;

    static constexpr int gap        = 10;
    static constexpr int knobGap    = 10;

    // Zoom caps: a block across the whole panel gets a bigger picture, not balloon knobs.
    static constexpr int maxGainSide  = 200;
    static constexpr int maxKnobSide  = 150;

    /** The left column's width, FIXED. Whether the device brought selectors or not, and however
        many, only the GAIN dial's diameter answers for it — so the picture beside the column never
        moves when the pack changes. A face that reshuffles itself per device is a face you have to
        re-learn per device. */
    static constexpr int columnW = 140;

    /** The ceiling on the dial-and-picture zone. Past this the dial is only bigger, not easier to
        land on, and every unit above it is one the EQ curve could have used to separate two
        decibels you would otherwise have to guess at. */
    static constexpr int topZoneH = 118;

    static constexpr int numViz = 7;

    AmpProcessor& amp;
    Block&        block;
    const char*   blk;
    const int     link;   // which of the two consoles/taps is this block's
    felitronics::analysis::RollingSpectrumTap& toneTap;

    VoicingSelector device;

    /** The block's own EQ, after the capture. Not a link that happens to be drawn here — the
        block owns it, its switch darkens it, and the curve's spectrum is this block's output. */
    EqSection eq;

    /** No label: the dial is the block's one big control and needs no caption to be found; its
        name rides the hint. The row a label took is diameter now. */
    Knob gain { "", theme::orange, 0 };

    /** SMOOTH or STEP — how the dial moves between the captured positions. A pill in the gap at
        the bottom of the dial's arc, the one place inside the dial's square nothing else reaches:
        a little lever and the word. Lit, the dial reaches every angle and the two neighbouring
        captures play mixed; dark, it lands on the captures alone. What that costs is the hint's
        to say. */
    struct SmoothTag final : public juce::Component,
                             public juce::SettableTooltipClient
    {
        bool on = true;
        std::function<void()> onChange;

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            g.setColour (theme::bezel.withAlpha (0.8f));
            g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
            r.reduce (3.0f, 0.0f);

            // The lever: a pill most of the row's height, the knob at the lit end.
            const auto sw = r.removeFromLeft (r.getHeight() * 1.3f).reduced (0.0f, 2.5f);
            g.setColour (on ? theme::orange.withAlpha (0.55f).overlaidWith (juce::Colour (0xff26262f))
                            : juce::Colour (0xff26262f));
            g.fillRoundedRectangle (sw, sw.getHeight() * 0.5f);
            const float k = sw.getHeight() - 3.0f;
            g.setColour (on ? juce::Colours::white : juce::Colour (0xff8a8a96));
            g.fillEllipse (on ? sw.getRight() - k - 1.5f : sw.getX() + 1.5f, sw.getCentreY() - k * 0.5f, k, k);

            g.setColour (on ? theme::orange : theme::txDim);
            theme::drawTracked (g, "SMOOTH", r.withTrimmedLeft (3.0f), theme::displayFont (8.0f), 0.06f,
                                juce::Justification::centredLeft);
        }

        void mouseDown (const juce::MouseEvent&) override
        {
            on = ! on;
            repaint();
            if (onChange != nullptr)
                onChange();
        }
    };

    SmoothTag smoothTag;
    std::unique_ptr<juce::ParameterAttachment> smoothAttachment;

    /** The block's two walls: IN at the left — what the model is being fed, with the trim's
        hand on it (see `params::blockIn`) — and OUT at the right, a meter alone: what leaves,
        EQ and level included, with no hand because a block deliberately has no output volume. */
    BlockMeter inMeter;
    BlockMeter outMeter;

    struct Slot
    {
        std::unique_ptr<Knob>    knob;        // a sweeping measured control
        std::unique_ptr<VSwitch> steps;       // ...or a switch, when the pack lists two positions
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> knobAtt;
        std::unique_ptr<juce::ParameterAttachment> stepAtt;
        int measuredIndex = -1;
    };

    std::array<Slot, (size_t) params::boostNumMeasured> slots;

    /** A device's other selecting controls — the ones that pick a FILE rather than shape one. Fur
        Coat's octave is the first of them: two captures at every dial position, and without this
        half the pack was unreachable. Drawn as a switch because that is what it is on the pedal. */
    struct PickSlot
    {
        std::unique_ptr<VSwitch> steps;
        std::unique_ptr<juce::ParameterAttachment> attachment;
        juce::String         name;     // the control, written left of the switch
        juce::StringArray    values;   // its positions — the chosen one written right of it, all of them in the hint
        juce::Rectangle<int> row;      // the one line the three share
    };

    /** A selecting control is ONE LINE: its name, the slot with the lever, the chosen position's
        name. The names under each detent took a second line out of the dial's diameter; the list
        they made is the switch's hint now. */
    static constexpr int pickRowH    = 20;
    static constexpr int pickNameW   = 50;
    static constexpr int pickSwitchW = 36;

    std::array<PickSlot, (size_t) params::numSelectors> selectors;

    /** The visualizations: one scope per way of looking, and exactly ONE of them on screen. The
        five used to tile the picture zone behind a column of checkboxes; a block that also carries
        a whole EQ console has room for one picture, and five ways to look at a device is a thing
        you CHOOSE between rather than a thing you watch at once. Right-click the picture to
        change it — the choice rides the session, so a reopened window shows what was left up. */
    std::array<std::unique_ptr<DeviceScope>, (size_t) numViz> scopes;

    int  vizPick = 0;
    juce::Rectangle<int> widgetArea;   // where the picture stands, so a right-click can find it
    void mouseDown (const juce::MouseEvent&) override;
    void showVizMenu (juce::Point<int> screenPos);
    void setViz (int which);

    /** Whether the loaded pack ships a photograph of its box. Most do not — the field is newer than
        the packs — and a page with nothing behind it is the gap this face already refuses to have
        anywhere else. */
    bool hasPicture() const;

    /** The next page the loop should land on, skipping any that has nothing of its own to show. */
    int nextViz (int from) const;

    /** Whether the picture standing here is big enough that DEVICE and PHOTO have become one card.
        Asked of the scope rather than deduced from the expanded flag: the wide tile a lone block
        gets is a card too, and one question is better than two rules. */
    bool pairsAsCard() const;
    juce::Identifier vizProperty() const { return juce::Identifier (juce::String (blk) + "_viz"); }

    /** The little overlay on the WAVE tile: half-wave against the mirrored band. */
    struct HalfTag final : public juce::Component
    {
        bool half = true;
        std::function<void()> onChange;

        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat();
            g.setColour (theme::bezel.withAlpha (0.8f));
            g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
            g.setColour (half ? theme::orange : theme::txDim);
            theme::drawTracked (g, "1/2", r, theme::displayFont (10.0f), 0.06f,
                                juce::Justification::centred);
        }

        void mouseDown (const juce::MouseEvent&) override
        {
            half = ! half;
            repaint();
            if (onChange != nullptr)
                onChange();
        }
    };

    HalfTag halfTag;

    /** The corner glyph that throws a tile across the whole face — and brings it back. */
    struct ExpandTag final : public juce::Component
    {
        bool expanded = false;
        std::function<void()> onClick;

        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat();
            g.setColour (theme::bezel.withAlpha (0.8f));
            g.fillRoundedRectangle (r, r.getHeight() * 0.5f);

            // Two diagonal arrows: outward to expand, inward to fold.
            const auto c = r.getCentre();
            g.setColour (theme::orange);

            for (const float s : { -1.0f, 1.0f })
            {
                const juce::Point<float> tip  = expanded ? c + juce::Point<float> (2.0f * s, 2.0f * s)
                                                         : c + juce::Point<float> (5.5f * s, 4.5f * s);
                const juce::Point<float> tail = expanded ? c + juce::Point<float> (5.5f * s, 4.5f * s)
                                                         : c + juce::Point<float> (2.0f * s, 2.0f * s);
                g.drawLine ({ tail, tip }, 1.4f);
                g.fillEllipse (tip.x - 1.6f, tip.y - 1.6f, 3.2f, 3.2f);
            }
        }

        void mouseDown (const juce::MouseEvent&) override
        {
            if (onClick != nullptr)
                onClick();
        }
    };

    std::array<ExpandTag, (size_t) numViz> expandTags;
    int expandedViz = -1;

    /** The theatre: one scope on the WHOLE MONITOR — a kiosk window of its own, black to the
        edges, dismissed by a click anywhere or Escape. Its scope is a fresh instance reading
        the same taps; the face's own copy hides while it runs so the wave ribbon has ONE
        resolution-setting reader at a time. */
    class ScopeTheater;
    std::unique_ptr<ScopeTheater> theater;

    struct ScreenTag final : public juce::Component
    {
        std::function<void()> onClick;

        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat();
            g.setColour (theme::bezel.withAlpha (0.8f));
            g.fillRoundedRectangle (r, r.getHeight() * 0.5f);

            // Four corner brackets: the whole screen.
            const auto b = r.withSizeKeepingCentre (11.0f, 9.0f);
            g.setColour (theme::orange);
            for (int cx = 0; cx < 2; ++cx)
                for (int cy = 0; cy < 2; ++cy)
                {
                    const float x = cx == 0 ? b.getX() : b.getRight();
                    const float y = cy == 0 ? b.getY() : b.getBottom();
                    const float dx = cx == 0 ? 3.5f : -3.5f;
                    const float dy = cy == 0 ? 3.0f : -3.0f;
                    g.drawLine (x, y, x + dx, y, 1.3f);
                    g.drawLine (x, y, x, y + dy, 1.3f);
                }
        }

        void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }
    };

    ScreenTag screenTag;

    /** THE GLYPHS COME WHEN THE HAND DOES. A photograph is the one thing on this face that is
        better with nothing on it, and three orange marks sitting permanently on a picture of a
        pedal are three marks that are always in the way. Every player and every image viewer works
        this way, so the hand looks for them there by reflex.

        Polled rather than driven by enter and exit: the glyphs are children that take their own
        mouse, so entering one is LEAVING the picture as far as the panel is concerned, and the
        mouse can also leave the window without a single event arriving. One question asked thirty
        times a second answers all of it and costs nothing.

        The fade is short — a quarter of a second in, half out. Instant is a flicker when the hand
        crosses the tile on its way somewhere else; slow reads as lag. */
    float cornerAlpha = 0.0f;
    void timerCallback() override;
    void setCornerAlpha (float);
    bool handIsOnThePicture() const;

    /** How finely this block's tap is asked to analyse. The tile gets the standing resolution;
        a TONE picture thrown open — or on the whole monitor — gets the long window, because there
        the console that shares the tap has stopped pulling and the picture is being READ. */
    void applySpectrumResolution();

    void openTheater();
    void closeTheater();

    void setControlsVisible (bool);
    void layWidget (juce::Rectangle<int>);

    juce::String caption;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment;
    std::unique_ptr<juce::ParameterAttachment> deviceAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CapturedBlockPanel)
};

} // namespace orbitamp
