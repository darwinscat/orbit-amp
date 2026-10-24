// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../Parameters.h"
#include "../core/CapturedBlock.h"
#include "BlockFrame.h"
#include "EqSection.h"
#include "scope/DeviceScope.h"
#include "Knob.h"
#include "VSwitch.h"
#include "ChannelLever.h"
#include "LatchKey.h"
#include "VoicingSelector.h"

#include <felitronics/appkit/DeviceGlyph.h>

#include <array>
#include <memory>

namespace orbitamp
{

class AmpProcessor;

/** The face every captured block wears — the preamp, and whatever captured block comes next. One
    class, because the question is always the same: which device, which capture of it, and what its
    own controls do. Which BLOCK it is arrives as a reference and an id prefix, and nothing else in
    the file knows.

    THREE SECTIONS, side by side, each a TIER on top and a STRIP under it, the strips one line across
    the block:

        [ GAIN dial      ] [ channel lever ] [ IN | the picture well | OUT ]
        [ SMOOTH, picks  ] [ BOOST         ] [ HPF  BASS  MIDDLE  TREBLE  LPF ]

    The dial is what the amp is set to, the lever which of its channels plays, the well what it does —
    the EQ curve among the pictures it can show, so the console's row of hands is the third strip and
    no longer a second storey of the block. The two narrow meters stand at the well's edges: what goes
    in at its left, what comes out at its right.

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

    /** `eqLink` is which EQ console is this block's — they belong to the blocks now, and the index
        is the only thing that says which is which. */
    CapturedBlockPanel (AmpProcessor&, Block&, const juce::String& title, const char* blockId,
                        int eqLink, felitronics::analysis::RollingSpectrumTap& toneSpectrumTap,
                        felitronics::analysis::RollingSpectrumTap& toneInSpectrumTap);
    ~CapturedBlockPanel() override;

    /** Rebuilds the face from whatever pack is loaded. Called when the device changes. */
    void deviceChanged();

    /** The EQ console's row of hands moved to the other side of its curve — a preference of the old
        layout, kept for the editor to land on (EqSection::setRowOnTop). */
    void setEqRowOnTop (bool onTop) { eq.setRowOnTop (onTop); resized(); }

    /** Puts a thrown-open picture back in its corner. Returns whether there was one — the editor
        asks every block and stops at the first that answers, so one Escape closes one thing. */
    bool foldPicture();

private:
    void layOutContent (juce::Rectangle<int>) override;
    void paintContent (juce::Graphics&) override;
    void blockOnChanged (bool on) override;
    void childrenChanged() override;

    /** Builds a switch for each selecting control the device has beyond its gain dial. */
    void buildSelectors();

    /** A HAND'S PICK — a device out of the menu, a place of the channel lever: the channel the amp
        leaves keeps what it was set to, and the one it plays comes back as it was left
        (AmpChannels.h). */
    void playPack (int index);

    /** The AMP's menu: the devices, then the folders of the player's own, mirrored. */
    void showDeviceMenu();

    /** The tone slots alone back to the pack's defaults — the console's RESET, when the block
        wears the device's own knobs. */
    void resetToneSlotsToPackDefaults();

    /** The device's OWN tone controls as console bands — one to three of whatever it happened to
        have, in the order the pack lists them. Empty when it measured nothing, which is what makes
        the block fall back to ours without being told to. */
    std::vector<EqSection::Band> nativeBands() const;

    /** Puts the console into whichever set the mode parameter names, falling to ours when the
        device has no native one to give. */
    void applyEqMode();

    std::unique_ptr<juce::ParameterAttachment> eqModeAttachment;

    static constexpr int gap        = 10;

    // Zoom caps: a block across the whole panel gets a bigger picture, not balloon knobs.
    static constexpr int maxGainSide  = 200;

    /** The two narrow sections' widths, FIXED. Whatever the device brings — a lever of two places or
        four, a BOOST or none, a selector or two — the sections keep their width, so the picture well
        beside them never moves when the pack changes. A face that reshuffles itself per device is a
        face you have to re-learn per device. Wide enough for what they hold at 13 px: OVERDRIVE on
        the lever, BRIGHT 2 beside its switch. */
    static constexpr int section1W  = 160;   // GAIN, and SMOOTH with the picks under it — the dial's notches clear of the edges
    static constexpr int section2W  = 150;   // the channel lever, and BOOST under it
    static constexpr int sectionGap = 16;    // a hairline down its middle
    static constexpr int tierGap    = 2;     // between a section's tier and its strip — 6 -> 2, the EQ's names closer under the curve
    static constexpr int leverGap   = 12;    // between the channel lever's foot and BOOST

    /** THE STRIPS' HEIGHT is the console's row, the tallest thing any of them holds: a dial of reading
        size with its name over it. Every tier is what is left above — so the lever stands exactly as
        tall as the picture beside it. All but GAIN's: its strip is only what SMOOTH and the picks
        need, and the dial takes the rest (layOutContent). */
    static constexpr int stripH = EqSection::rowH;

    static constexpr int numViz = 7;

    /** The picture well's pages: the seven scopes by their Mode, and the EQ curve after them — one
        loop, PHOTO to EQ to SHAPE. */
    static constexpr int eqPage   = numViz;
    static constexpr int numPages = numViz + 1;

    AmpProcessor& amp;
    Block&        block;
    const char*   blk;
    const int     link;   // which console/tap is this block's
    felitronics::analysis::RollingSpectrumTap& toneTap;

    /** The DEVICE — its name on the block's top border, and the menu of every device under it. */
    VoicingSelector device;

    /** The devices as the menu has them, read from the block's list each time the device changes. */
    device::DeviceLibrary::Shelf shelf;

    /** The playing device's channels, one place each down a lever. None for a device of one channel:
        a lever with nowhere to go is furniture, and the section keeps the room. Rebuilt when a device
        with another count of channels comes in — a lever's places are fixed for its life. */
    std::unique_ptr<ChannelLever> lever;
    void showChannels (const juce::StringArray& names, int lit);

    /** BOOST — a pack's selector of that name — as its own key under the lever, apart from the
        other picks: the switch a player reaches for mid-song. A pack without one leaves its place
        empty, rather than closing the gap. */
    LatchKey boostKey { "BOOST" };
    int boostIndex = -1;   // which of the selectors it is, or -1

    /** ...and on a pack with no Boost switch — the X88-IR's — the key is still BOOST, thrown on
        params::preampBoost: the capture played two positions up the dial, the knob where it stands.
        Any pack that plays has the key; only an empty block has none. */
    std::unique_ptr<juce::ParameterAttachment> boostAttachment;
    bool boostShown = false;

    /** BOOST'S LEVEL, the key's right part — "BOOST | +3 ▾": params::boostLevel, lit with the key and
        dim while BOOST is out, so the level can be set before the boost is thrown. A press on it, or a
        right click anywhere on the key, is the menu below. */
    std::unique_ptr<juce::ParameterAttachment> boostLevelAttachment;

    /** How much louder the amp leaves while BOOST is on (params::boostLevel), the current one ticked.
        One level for the whole amp, so every device's BOOST plays at it. */
    void showBoostMenu();

    /** The block's own EQ, after the capture. Not a link that happens to be drawn here — the
        block owns it, its switch darkens it, and the curve's spectrum is this block's output. */
    EqSection eq;

    /** No label: the dial is the block's one big control and needs no caption to be found; its
        name rides the hint. The row a label took is diameter now. */
    Knob gain { "", theme::orange, 0 };

    /** SMOOTH or STEP — how the dial moves between the captured positions. The first line of the
        dial's strip: a little lever and the word, at reading size now that it has a line of its own
        rather than the gap at the foot of the dial's arc. Lit, the dial reaches every angle and the
        two neighbouring captures play mixed; dark, it lands on the captures alone. What that costs is
        the hint's to say. */
    struct SmoothTag final : public juce::Component,
                             public juce::SettableTooltipClient
    {
        bool on = true;
        std::function<void()> onChange;

        static constexpr float fontH = 13.0f;
        static constexpr float leverW = 26.0f;

        /** The lever, the word, and the pill's ends. */
        static int idealWidth()
        {
            return juce::roundToInt (leverW + 6.0f + theme::trackedWidth ("SMOOTH", theme::displayFont (fontH), 0.06f) + 10.0f);
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            g.setColour (theme::bezel.withAlpha (0.8f));
            g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
            r.reduce (3.0f, 0.0f);

            // The lever: a pill most of the row's height, the knob at the lit end.
            const auto sw = r.removeFromLeft (leverW).reduced (0.0f, 3.5f);
            g.setColour (on ? theme::orange.withAlpha (0.55f).overlaidWith (juce::Colour (0xff26262f))
                            : juce::Colour (0xff26262f));
            g.fillRoundedRectangle (sw, sw.getHeight() * 0.5f);
            const float k = sw.getHeight() - 3.0f;
            g.setColour (on ? juce::Colours::white : juce::Colour (0xff8a8a96));
            g.fillEllipse (on ? sw.getRight() - k - 1.5f : sw.getX() + 1.5f, sw.getCentreY() - k * 0.5f, k, k);

            g.setColour (on ? theme::orange : theme::txDim);
            theme::drawTracked (g, "SMOOTH", r.withTrimmedLeft (6.0f), theme::displayFont (fontH), 0.06f,
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

    struct Slot
    {
        std::unique_ptr<Knob>    knob;        // a sweeping measured control
        std::unique_ptr<VSwitch> steps;       // ...or a switch, when the pack lists two positions
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> knobAtt;
        std::unique_ptr<juce::ParameterAttachment> stepAtt;
        int measuredIndex = -1;
    };

    std::array<Slot, (size_t) Block::numMeasured> slots;

    /** A device's other selecting controls — the ones that pick a FILE rather than shape one. Fur
        Coat's octave is the first of them: two captures at every dial position, and without this
        half the pack was unreachable. Drawn as a switch because that is what it is on the pedal —
        all but BOOST, which is a key of its own (boostKey). */
    struct PickSlot
    {
        std::unique_ptr<VSwitch> steps;
        std::unique_ptr<juce::ParameterAttachment> attachment;
        juce::String         name;     // the control — written beside the switch when it is an off/on
        juce::StringArray    values;   // its positions — the chosen one written beside the switch, all of them in the hint
        juce::Rectangle<int> row;      // the one line the switch and its word share
        bool                 onOff = false;   // two positions, OFF and ON: named by what it switches
    };

    /** A selecting control is ONE LINE of the dial's strip: the slot with the lever, then ONE word in
        the hand's colour. Which word is the switch's own nature: a two-way OFF/ON is named by what it
        switches — its NAME, lit when on — and anything else by where it stands, the chosen position's
        name (BRIGHT 1, CENTER, FAT). The name and every position are the hint. One word, not the
        three the old column wrote, because the strip is a section wide and 13 px is not negotiable. */
    static constexpr int   pickRowH     = 18;
    static constexpr int   pickRowGap   = 3;
    static constexpr int   dialGap      = 4;   // between the dial's square and its strip: the notches overhang it
    static constexpr int   pickSwitchW  = 30;
    static constexpr float pickFontH    = 13.0f;
    static constexpr float pickTracking = 0.08f;

    /** The widest the word beside a pick's switch can be — its name, or its longest position. */
    static float pickWordWidth (const PickSlot&);

    std::array<PickSlot, (size_t) params::numSelectors> selectors;

    /** The visualizations: one scope per way of looking, and exactly ONE page of the well on screen —
        a scope, or the EQ curve, which is a page like the others now rather than a storey of its own.
        Several ways to look at a device is a thing you CHOOSE between rather than a thing you watch
        at once. The page's name in the well's corner opens the list; a click on a scope walks the
        loop; the choice rides the session, so a reopened window shows what was left up. */
    std::array<std::unique_ptr<DeviceScope>, (size_t) numViz> scopes;

    int  vizPick = eqPage;
    juce::Rectangle<int> widgetArea;   // where the picture stands, so a right-click can find it
    void mouseDown (const juce::MouseEvent&) override;
    void showVizMenu (juce::Point<int> screenPos);
    void setViz (int which);

    /** The scope that stands for the page on the whole monitor: the EQ curve's is TONE, the same
        curve over the same spectrum, read rather than handled. */
    DeviceScope::Mode modeOfPage (int page) const
    {
        return page == eqPage ? DeviceScope::Mode::tone : (DeviceScope::Mode) page;
    }

    /** Where the hairlines between the sections stand, for paintContent. */
    std::array<int, 2> dividerX { -1, -1 };

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
            theme::drawTracked (g, "1/2", r, theme::displayFont (13.0f), 0.04f,
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

    /** THE ONE GLYPH in the well's corner: it throws the picture across the whole block — and brings
        it back. There used to be two, this and the whole monitor's brackets beside it, two doors to
        two sizes of the same picture in one corner of a well two hundred units tall; the monitor is
        the picture menu's first line now (WHOLE SCREEN), and the corner keeps one door. Drawn as the
        four corners of a frame, outward to throw open, inward to fold. */
    struct ExpandTag final : public juce::Component
    {
        bool expanded = false;
        std::function<void()> onClick;

        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat();
            g.setColour (theme::bezel.withAlpha (0.8f));
            g.fillRoundedRectangle (r, r.getHeight() * 0.5f);

            const auto b = r.withSizeKeepingCentre (11.0f, 9.0f);
            g.setColour (theme::orange);

            for (int cx = 0; cx < 2; ++cx)
                for (int cy = 0; cy < 2; ++cy)
                {
                    // Folded, the brackets open outward from the frame's corners; thrown open, they
                    // turn round and point in.
                    const float x  = cx == 0 ? b.getX() : b.getRight();
                    const float y  = cy == 0 ? b.getY() : b.getBottom();
                    const float dx = (cx == 0 ? 3.5f : -3.5f) * (expanded ? -1.0f : 1.0f);
                    const float dy = (cy == 0 ? 3.0f : -3.0f) * (expanded ? -1.0f : 1.0f);
                    const float ox = expanded ? (cx == 0 ? 3.5f : -3.5f) : 0.0f;
                    const float oy = expanded ? (cy == 0 ? 3.0f : -3.0f) : 0.0f;
                    g.drawLine (x + ox, y + oy, x + ox + dx, y + oy, 1.3f);
                    g.drawLine (x + ox, y + oy, x + ox, y + oy + dy, 1.3f);
                }
        }

        void mouseDown (const juce::MouseEvent&) override
        {
            if (onClick != nullptr)
                onClick();
        }
    };

    ExpandTag expandTag;
    bool thrownOpen = false;   // the picture across the whole block

    /** THE PAGE'S NAME, in the well's other corner — EQ, SHAPE, TONE… — and the list of pages under
        it. On a scope a click on the picture walks the loop, but on the EQ curve a click is the
        curve's own (its nodes are dragged), so the way to the other pages has to be a thing of its
        own. It comes and goes with the hand, as the corner glyph does. */
    struct PageTag final : public juce::Component
    {
        juce::String text;
        std::function<void (juce::Point<int>)> onClick;

        static constexpr float fontH = 13.0f;

        int idealWidth() const
        {
            return juce::roundToInt (theme::trackedWidth (text, theme::displayFont (fontH), 0.12f) + 34.0f);
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (theme::panel.withAlpha (0.85f));
            g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
            g.setColour (theme::hair2);
            g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
            g.setColour (theme::txDim);
            theme::drawTracked (g, text, r.withTrimmedRight (14.0f), theme::displayFont (fontH), 0.12f,
                                juce::Justification::centred);

            juce::Path v;
            const float cx = r.getRight() - 13.0f, cy = r.getCentreY() - 1.0f;
            v.startNewSubPath (cx - 3.0f, cy);
            v.lineTo (cx, cy + 3.0f);
            v.lineTo (cx + 3.0f, cy);
            g.strokePath (v, juce::PathStrokeType (1.2f));
        }

        void mouseDown (const juce::MouseEvent&) override
        {
            if (onClick != nullptr)
                onClick (localPointToGlobal (juce::Point<int> (0, getHeight())));
        }
    };

    PageTag pageTag;

    /** The theatre: one scope on the WHOLE MONITOR — a kiosk window of its own, black to the
        edges, dismissed by a click anywhere or Escape. Its scope is a fresh instance reading
        the same taps; the face's own copy hides while it runs so the wave ribbon has ONE
        resolution-setting reader at a time. */
    class ScopeTheater;
    std::unique_ptr<ScopeTheater> theater;

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
    bool  glyphsShownForStill = false;   // the live still's (WebFaceplate.h): as if the hand were on the picture
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

    /** The page that is up, filling the well it is handed, its corner glyphs riding it. */
    void layPicture (juce::Rectangle<int>);

    /** The page that is up, shown or put away — the theatre puts it away while it runs. */
    void setPictureShown (bool);

    juce::String caption;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment;
    std::unique_ptr<juce::ParameterAttachment> deviceAttachment;

    /** Whether the border carries the model's loudness tag. Always, but for the site's kit, which is
        taken without it (WebFaceplate.h): the tag moves with the dial and the site has nothing to
        write it from. */
    bool showsLoudnessTag = true;

    friend class WebFaceplate;   // the site's kit reads where the parts stand (WebFaceplate.h)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CapturedBlockPanel)
};

} // namespace orbitamp
