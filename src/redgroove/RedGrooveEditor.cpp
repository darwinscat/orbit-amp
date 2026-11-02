// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "RedGrooveEditor.h"

#include "RedGrooveArt.h"
#include "RedGrooveLayout.h"
#include "RedGrooveLineup.h"

#include "../ui/FactoryMissing.h"

#include <BinaryData.h>

namespace orbitamp::redgroove
{

juce::AudioProcessorEditor* makeEditor (AmpProcessor& p)
{
    // No dashboard to draw without its pictures, which lie in the factory folder: where they are missing, the
    // window says so, and where they belong (ui/FactoryMissing.h).
    if (! Art::present())
        return new ui::FactoryMissingEditor (p, p.product());
    return new RedGrooveEditor (p);
}

using namespace face;

namespace
{
    // Where the pointer is drawn on the knobs' renders: a lit slot from `inner` to `outer` of the cap's
    // radius, `width` wide — on the machined cap, clear of the knurled ring.
    constexpr ArtKnob::Pointer drivePointer { 0.30f, 0.66f, 0.06f };
    constexpr ArtKnob::Pointer tonePointer  { 0.30f, 0.64f, 0.07f };

    /** THE HAND'S LIGHT: a soft orange round whatever the hand is over or on — the family's colour for the
        hand, a line round the control, never a wash. */
    const juce::Colour handLight = orange.withAlpha (0.8f);

    /** WHAT THE WORDS ARE WRITTEN IN, as the layout's drawing has them: the controls' names lilac, their
        values near white, a state the family's orange — each with a dark edge under it, off the brushed metal. */
    Ink cabinInk()
    {
        Ink ink;
        ink.words  = juce::Colour (0xffb9a6ff);
        ink.values = juce::Colour (0xfff2eff8);
        ink.edge   = juce::Colour (0xaa000000);
        return ink;
    }

    /** The panels' titles, a little lighter than the controls' names. */
    const juce::Colour titleInk { 0xffded9e8 };
    /** The maker's words beside the plate, quieter than the titles. */
    const juce::Colour makerInk { 0xffd3cedd };

    /** The keys: a dark cap with a violet rim, its word on it. */
    const juce::Colour keyCap { 0xff22222e }, keyRim { 0xff8b6cff };

    juce::Rectangle<float> around (juce::Point<float> c, float w, float h)
    {
        return juce::Rectangle<float> (w, h).withCentre (c);
    }

    /** A WORD PRINTED ON THE PANEL, centred on `at`, a dark edge under it. */
    void print (juce::Graphics& g, const juce::String& text, juce::Point<float> at, float em, juce::Colour colour)
    {
        const auto ink = cabinInk();
        ink.label (g, text, juce::Rectangle<float> (360.0f, 32.0f).withCentre (at), em, colour);
    }

    /** ...a title, from its left edge at `at.x`, as the layout's drawing sets its panels' names. */
    void printFromLeft (juce::Graphics& g, const juce::String& text, juce::Point<float> at, float em, juce::Colour colour)
    {
        constexpr float tracking = 0.08f;
        const float wide = juce::GlyphArrangement::getStringWidth (labelFont (em).withExtraKerningFactor (tracking), text.toUpperCase());
        print (g, text, { at.x + 0.5f * wide, at.y }, em, colour);
    }

    /** WHERE THE CONSOLE'S CONTROLS STAND on the dashboard (RedGrooveLayout.h). */
    Console::Places cabinPlaces()
    {
        using namespace layout;
        using namespace stage;

        Console::Places p;
        p.drive = around (drive, driveKnob, driveKnob);

        // The tones: as many knobs as the standard has, on the three sockets the picture paints — the row
        // measured to hold three at their size and not one more.
        const float pitch = toneX[1] - toneX[0];
        p.toneStrip.area = juce::Rectangle<float> (2.0f * pitch + toneSocket + 1.0f, toneSocket).withCentre ({ toneX[1], toneY });
        p.toneStrip.pitch = pitch;
        p.toneStrip.size = toneSocket;
        p.toneStrip.minScale = 1.0f;
        p.toneKnob = toneKnob;
        p.toneNameY = knobNameY;
        p.toneNameEm = px13;
        p.toneSignDrop = 0.0f;              // the names stand in the row itself, as DRIVE's does

        // No filter keys and no keys of a pack's under the tones; the six keys of the history and the
        // registers in their holes, the history's wider.
        p.filterKeys = false;
        for (size_t i = 0; i < p.holes.size(); ++i)
        {
            p.holes[i] = { keyX[i], keyRowY };
            p.holeKeys[i] = around (p.holes[i], i < (size_t) ovFirstRegister ? historyKeyWidth : registerKeyWidth, keyHeight);
        }
        p.holeKeySize = keyHeight;

        // No REVERB, DELAY or CAB plates. CAB's box is the cabinet's screen: a dropped IR's tag stands over it,
        // and a right click on it opens the cabinets' window.
        p.plates = false;
        p.sliderNames[2] = { 1246.0f, 707.5f, 251.5f, 49.0f };

        p.faderCapLength = faderCapLength;
        p.inFader  = inFaderSlot.withSizeKeepingCentre (faderWidth, inFaderSlot.getHeight());
        p.outFader = outFaderSlot.withSizeKeepingCentre (faderWidth, outFaderSlot.getHeight());
        p.inMeter  = inMeterSlot.reduced (4.0f, 5.0f);
        p.outMeter = outMeterSlot.reduced (4.0f, 5.0f);

        p.tuner = tunerScreen.reduced (6.0f, 3.0f);
        p.tunerStrip = true;            // one low line along the screen
        p.tunerMenuOnClick = true;      // ...whose settings a click on it opens

        // The lamps, drawn by code: each overload over its meter, GATE's and LIMITER's at the left of their slots.
        p.lampDot = 8.0f;
        p.lamps[inClipLamp]  = around ({ inMeterSlot.getCentreX(), inMeterSlot.getY() - 9.0f }, 14.0f, 14.0f);
        p.lamps[outClipLamp] = around ({ outMeterSlot.getCentreX(), outMeterSlot.getY() - 9.0f }, 14.0f, 14.0f);
        p.lamps[gateLamp]    = around ({ gateSlot.getX() + 14.0f, gateSlot.getCentreY() }, 14.0f, 14.0f);
        p.lamps[limiterLamp] = around ({ limiterSlot.getX() + 14.0f, limiterSlot.getCentreY() }, 14.0f, 14.0f);
        p.gateDoor    = gateSlot.withTrimmedLeft (24.0f).withTrimmedRight (2.0f);
        p.limiterDoor = limiterSlot.withTrimmedLeft (24.0f).withTrimmedRight (2.0f);
        p.doorsFit = true;              // LIMITER is wider than its slot leaves it: set condensed

        p.emblemFace = { emblemFace.begin(), emblemFace.end() };
        p.emblemMark = emblemMark;

        p.preset  = presetPlace;
        p.version = versionPlace;
        p.memory  = memoryPlace;
        p.load    = loadPlace;
        p.readingsFit = true;
        p.readingPx = px13;
        return p;
    }

    Console::Look cabinLook()
    {
        const auto& art = Art::of();
        const auto mark = Art::mark();
        return { art.knobDrive, drivePointer, art.knobTone, tonePointer,
                 art.faderCap,              // no plate sliders: their cap is the columns'
                 art.faderCap,
                 { &art.none, &art.none, &art.none, &art.none },
                 mark.isNotEmpty() ? mark : juce::String::fromUTF8 (BinaryData::catlogo_svg, BinaryData::catlogo_svgSize),
                 Art::midiCommands(),
                 handLight,
                 cabinInk(),
                 juce::Colour (0xff9a7aff) };       // the lamp under the cat's glass: the family's violet
    }
}

//==============================================================================
juce::Rectangle<int> Cabin::historyArea() const
{
    using namespace layout;
    return juce::Rectangle<float> (keyX.front() - 50.0f, keyRowY - 26.0f, keyX.back() - keyX.front() + 100.0f, 52.0f).toNearestInt();
}

juce::Colour Cabin::signInk() const { return cabinInk().words; }

void Cabin::paintStatic (juce::Graphics& g, float scale) const
{
    using namespace layout;
    using namespace stage;

    if (const auto& bg = Art::of().bg; bg.isValid())
        g.drawImage (bg, juce::Rectangle<float> (0.0f, 0.0f, width, height));

    // The family's cat, cut into the maker's plate; its words either side of it.
    engraveMark (g, emblemMark, scale);
    print (g, "POWERED BY", poweredByPlate.getCentre(), px13, makerInk);
    print (g, "DARWIN'S CAT", makerNamePlate.getCentre(), px13, makerInk);

    // The panels' titles.
    printFromLeft (g, "KEYS", keysTitle, px13, titleInk);
    printFromLeft (g, "TUNER", tunerTitle, px13, titleInk);
    print (g, "IN", inTitle, px13, titleInk);
    printFromLeft (g, "AMP", ampTitle, px13, titleInk);
    printFromLeft (g, "TONE", toneTitle, px13, titleInk);
    printFromLeft (g, "EFFECTS", effectsTitle, px13, titleInk);
    printFromLeft (g, "CABINET", cabinetTitle, px13, titleInk);
    print (g, "OUT", outTitle, px13, titleInk);

    // DRIVE, the hero, a size up and in the values' white; the tones' names are the playing channel's (paintStrips).
    print (g, "DRIVE", { drive.x, knobNameY }, px15, cabinInk().values);
}

/** THE PLAYING CHANNEL'S TONES' NAMES over their knobs, each in its row — its sockets are the picture's own. */
void Cabin::paintStrips (juce::Graphics& g, float) const
{
    const auto ink = cabinInk();
    for (const auto& t : strips.tones)
        ink.label (g, t.name, t.label.withSizeKeepingCentre (t.label.getWidth() + 40.0f, t.label.getHeight()), stage::px13, ink.words);
}

//==============================================================================
Dome::Dome()
{
    setInterceptsMouseClicks (false, false);
    setOpaque (true);
}

void Dome::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0b0b11));
    if (still.isValid())
        g.drawImage (still, getLocalBounds().toFloat(), juce::RectanglePlacement::fillDestination);
}

//==============================================================================
RedGrooveEditor::RedGrooveEditor (AmpProcessor& p)
    : face::Console (p, std::make_unique<Cabin>(), cabinPlaces(), cabinLook())
{
    // Everything this constructor sets is the face settling onto the state, not a player's edit.
    const felitronics::appkit::CompareHistory::ScopedSuppress settling (amp.history);

    // THE SIX KEYS: push keys, each its word on its cap — UNDO and REDO dim while there is nothing to step
    // to, the register that is playing its letter and its rim the family's orange; a key held down sinks a
    // shade, its rim orange; the hand's orange rim round it while the hand is over it.
    for (int i = 0; i < 6; ++i)
        roofKey (i).drawKey = [this, &k = roofKey (i), i] (juce::Graphics& g, juce::Rectangle<float> box, bool down, float)
        {
            const auto ink = cabinInk();
            const bool history = i < ovFirstRegister;
            const bool lit = ! history && k.isOn();
            const bool live = ! history || (i == ovUndo ? amp.history.canUndo() : amp.history.canRedo());
            const bool held = history && down;

            const auto cap = box.reduced (1.5f);
            g.setColour (held ? keyCap.darker (0.35f) : keyCap);
            g.fillRoundedRectangle (cap, 6.0f);
            g.setColour (lit || held ? orange : keyRim.withAlpha (live ? 1.0f : 0.45f));
            g.drawRoundedRectangle (cap, 6.0f, 1.4f);

            const auto word = lit ? ink.state : ink.at (ink.values, live ? 0.92f : 0.35f);
            ink.label (g, k.name, cap.translated (0.0f, held ? 1.0f : 0.0f), stage::px13, word, 0.04f);

            if (k.isMouseOverOrDragging() && live)
            {
                juce::Path rim;
                rim.addRoundedRectangle (cap.expanded (1.0f), 7.0f);
                drawHandRim (g, rim, handLight);
            }
        };

    // THE DOME, under the dashboard: the picture's alpha is its frame.
    dome.still = Art::still();
    stage.addAndMakeVisible (dome, 0);
    dome.setBounds (layout::dome.toNearestInt());

    // A prototype's hook for looking at the face on another channel without a hand on anything:
    // REDGROOVE_CHANNEL names one, from 0 to the last.
    if (const auto c = juce::SystemStats::getEnvironmentVariable ("REDGROOVE_CHANNEL", {}); c.isNotEmpty())
        engine.go (c.getIntValue());

    showChannel();
}

/** The walker's own part of the site's demo (Console::webExtras): the dome — the box the world is drawn in — the
    still it ships, and a poster for each channel, that still through the glass. */
void RedGrooveEditor::webExtras (const juce::File& dir, juce::DynamicObject& places, juce::DynamicObject&)
{
    writeForWeb (dir.getChildFile ("still.webp"), Art::still());

    auto* window = new juce::DynamicObject();
    window->setProperty ("x", layout::dome.getX());
    window->setProperty ("y", layout::dome.getY());
    window->setProperty ("w", layout::dome.getWidth());
    window->setProperty ("h", layout::dome.getHeight());
    places.setProperty ("window", juce::var (window));
    juce::Array<juce::var> stills;
    stills.add ("still.webp");
    places.setProperty ("stills", stills);

    webPoses.count = channels.count();
    webPoses.opening = channels.active();
    webPoses.standAt = [this] (int at) { go (at); };
    webPoses.still = [] (int) { return Art::still(); };
    webPoses.window = layout::dome;
    webPoses.hidden = { &dome };
}

} // namespace orbitamp::redgroove
