// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "RedGrooveEditor.h"

#include "RedGrooveArt.h"
#include "RedGrooveLayout.h"
#include "RedGrooveLineup.h"

#include "view/WalkerDrive.h"

#include "../ui/BassControls.h"
#include "../ui/FactoryMissing.h"
#include "../ui/Prefs.h"

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
    /** What the dome shows, and how often — the machine's, kept with its settings: the landscape as a word, in the
        order of Windscreen::Mode, and what the menu calls each. */
    const juce::Identifier landscapeKey { "landscape" };
    const juce::Identifier frameRateKey { "landscape_fps" };
    const juce::StringArray landscapeWords { "forest", "1993", "still" };
    const juce::StringArray landscapeNames { "The forest", "1993", "Still" };

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
    void print (juce::Graphics& g, const juce::String& text, juce::Point<float> at, float em, juce::Colour colour,
                float tracking = 0.08f)
    {
        const auto ink = cabinInk();
        ink.label (g, text, juce::Rectangle<float> (360.0f, 32.0f).withCentre (at), em, colour, tracking);
    }

    /** ...set condensed into `room` about its centre, as tall, where it is wider — tracked `tracking` of the size. */
    void printInto (juce::Graphics& g, const juce::String& text, juce::Point<float> at, float room, float em, juce::Colour colour,
                    float tracking = 0.08f)
    {
        const float wide = juce::GlyphArrangement::getStringWidth (labelFont (em).withExtraKerningFactor (tracking), text.toUpperCase());
        const juce::Graphics::ScopedSaveState keep (g);
        if (wide > room)
            g.addTransform (juce::AffineTransform::scale (room / wide, 1.0f, at.x, at.y));
        print (g, text, at, em, colour, tracking);
    }

    /** THE RINGS' LIGHT: the family's violet, round every knob. */
    const juce::Colour ringLight { 0xff8b6cff };

    /** What a reading says: decibels signed to a tenth, whole decibels, a share in percent, a frequency. */
    juce::String dbTenth (float v) { return (v > 0.04f ? "+" : "") + juce::String (v, 1) + " dB"; }
    juce::String dbWhole (float v) { return juce::String (juce::roundToInt (v)) + " dB"; }
    juce::String percent (float share) { return juce::String (juce::roundToInt (share * 100.0f)) + "%"; }
    juce::String hertz (float hz) { return hz >= 1000.0f ? juce::String (hz / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz"; }

    /** MIDDLE's frequency on its fader: the span params::bassMidMinHz .. bassMidMaxHz on a logarithmic travel. */
    float midTravel (float hz)
    {
        const float lo = params::bassMidMinHz, hi = params::bassMidMaxHz;
        return juce::jlimit (0.0f, 1.0f, std::log (juce::jlimit (lo, hi, hz) / lo) / std::log (hi / lo));
    }
    float midHz (float travel) { return params::bassMidMinHz * std::pow (params::bassMidMaxHz / params::bassMidMinHz, travel); }

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
        p.sliderNames[2] = cabinetScreen;

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
        p.doorsFit = true;              // set condensed into their slots, where wider
        p.limiterWord = "LIMIT";        // ...and LIMITER's door says LIMIT, as the plate has room for

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

    // The other controls' names over them: COMP and BLEND beside DRIVE — COMP's squeeze between COMP and DRIVE says
    // itself; the three effects tracked tighter than the rest, each condensed into a room well short of its pitch, so
    // that air stands between them; DI/CAB over its knob; and the words at the ends of MIDDLE's frequency.
    const auto words = cabinInk().words;
    print (g, "COMP", { comp.x, knobNameY }, px13, words);
    print (g, "BLEND", { blend.x, knobNameY }, px13, words);
    const char* effects[] { "OCTAVE", "ENVELOPE", "CHORUS" };
    for (size_t i = 0; i < effectX.size(); ++i)
        printInto (g, effects[i], { effectX[i], knobNameY }, effectNameRoom, px13, words, effectNameTracking);
    print (g, "DI/CAB", { diCab.x, diCabNameY }, px13, words);
    const auto ends = cabinInk().at (words, 0.8f);
    writeAsIs (g, cabinInk(), "200 Hz", around ({ midFreqSlot.getX() - 48.0f, midFreqSlot.getCentreY() }, 90.0f, 28.0f), px13, ends);
    writeAsIs (g, cabinInk(), "3 kHz", around ({ midFreqSlot.getRight() + 40.0f, midFreqSlot.getCentreY() }, 80.0f, 28.0f), px13, ends);

    // The picture's own two slots for the cuts' switches go under plain panel — the switches stand one a line now
    // (switchLineY), each its slot cut out of the picture where it stands. The plain is the panel between the two.
    if (const auto& bg = Art::of().bg; bg.isValid())
    {
        const float k = (float) bg.getWidth() / width;
        for (const auto slot : { lowCutSlot, highCutSlot })
        {
            const auto over  = slot.expanded (7.0f);
            const auto plain = over.withY (lowCutSlot.getBottom() + 4.0f);
            g.drawImage (bg.getClippedImage ((plain * k).toNearestInt()), over);
        }
    }

    // ...and a line cut into the panel between DI/CAB and them: a shadow, and its lit edge beside it.
    const float top = cabinetScreen.getBottom() + 10.0f, bottom = switchLineY.back() + 0.5f * switchSlotH + 6.0f;
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillRect (juce::Rectangle<float> (switchRuleX - 0.75f, top, 1.5f, bottom - top));
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.fillRect (juce::Rectangle<float> (switchRuleX + 0.75f, top, 1.0f, bottom - top));
}

/** THE PLAYING CHANNEL'S TONES' NAMES over their knobs, each in its row — its sockets are the picture's own. */
void Cabin::paintStrips (juce::Graphics& g, float) const
{
    const auto ink = cabinInk();
    for (const auto& t : strips.tones)
        ink.label (g, t.name, t.label.withSizeKeepingCentre (t.label.getWidth() + 40.0f, t.label.getHeight()), stage::px13, ink.words);
}

//==============================================================================
RedGrooveEditor::RedGrooveEditor (AmpProcessor& p)
    : face::Console (p, std::make_unique<Cabin>(), cabinPlaces(), cabinLook()),
      view (std::make_unique<redpaw::Windscreen> (makeDrive())),
      comp (Art::of().knobTone, tonePointer),
      blend (Art::of().knobTone, tonePointer),
      diCab (Art::of().knobTone, tonePointer),
      octave (Art::of().faderCap), envelope (Art::of().faderCap), chorus (Art::of().faderCap), midFreq (Art::of().faderCap),
      squeeze ([&p] { return p.compGrDb.load (std::memory_order_relaxed); }),
      lever (cabinInk(), { "B15", "SVT" }, layout::leverPlaces, layout::leverX, layout::leverNames, layout::leverBox.getPosition()),
      lowCut (cabinInk(), "STR", "4", "5", 2, true),
      highCut (cabinInk(), {}, "FING", "SLAP"),
      ultraLo (cabinInk(), "U-LO", "-", "+", 3),
      ultraHi (cabinInk(), "U-HI", "0", "+", 2),
      cabinet (cabinInk())
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

    // THE BASS'S OWN KNOBS, each in its socket: COMP, BLEND, DI/CAB.
    const auto knobAt = [this] (face::ArtKnob& knob, juce::Point<float> centre, float size)
    {
        addToStage (knob);
        knob.setBounds (around (centre, size, size).toNearestInt());
    };
    knobAt (comp, layout::comp, layout::smallKnob);
    knobAt (blend, layout::blend, layout::smallKnob);
    knobAt (diCab, layout::diCab, layout::diCabKnob);

    // THE RINGS, one round every knob in its socket's groove, the tones' where the rows put them (stripsLaid).
    const auto ringFor = [this] (const face::ArtKnob& knob, juce::Point<float> centre, float socket, float width)
    {
        rings.push_back (std::make_unique<face::KnobRing> (knob, ringLight, centre, socket * 0.5f - layout::ringInset, width));
        addToStage (*rings.back());
    };
    ringFor (driveKnob(), layout::drive, layout::driveSocket, layout::driveRingWidth);
    ringFor (comp, layout::comp, layout::smallSocket, layout::ringWidth);
    ringFor (blend, layout::blend, layout::smallSocket, layout::ringWidth);
    ringFor (diCab, layout::diCab, layout::diCabSocket, layout::ringWidth);
    for (int t = 0; t < Channels::maxTones; ++t)
        ringFor (toneKnob (t), { layout::toneX[0], layout::toneY }, layout::toneSocket, layout::ringWidth);
    stripsLaid (strips);        // the console laid the rows before there were rings to put round them

    // THE FADERS: the three effects upright in their slots, MIDDLE's frequency across under the tones. Each says
    // where it stands in the tag over it while it is held.
    const auto faderIn = [this] (face::ColumnFader& fader, juce::Rectangle<float> box, float cap, bool across)
    {
        fader.capLength = cap;
        fader.handLight = handLight;
        fader.marks = across ? 4 : 10;
        fader.longMark = -1;
        fader.setAcross (across);
        addToStage (fader);
        fader.setBounds (box.toNearestInt());
    };
    face::ColumnFader* effectFaders[] { &octave, &envelope, &chorus };
    const float effectHeight = layout::effectBottom - layout::effectTop;
    for (size_t i = 0; i < 3; ++i)
        faderIn (*effectFaders[i], juce::Rectangle<float> (layout::effectFaderWidth, effectHeight)
                                       .withCentre ({ layout::effectX[i], layout::effectTop + 0.5f * effectHeight }),
                 layout::faderCapLength, false);
    faderIn (midFreq, layout::midFreqSlot.withSizeKeepingCentre (layout::midFreqSlot.getWidth(), 40.0f), layout::midFreqCap, true);

    // Each known by a name of its own, as a gate finds it (tests/RedGrooveFaceTests.cpp).
    comp.setComponentID ("comp");
    blend.setComponentID ("blend");
    diCab.setComponentID ("diCab");
    octave.setComponentID ("octave");
    envelope.setComponentID ("envelope");
    chorus.setComponentID ("chorus");
    midFreq.setComponentID ("midFreq");

    // COMP's squeeze, in its slot.
    addToStage (squeeze);
    squeeze.setBounds (layout::grSlot.reduced (2.0f).toNearestInt());

    // THE READINGS, one under every control.
    const auto under = [] (float x, float y, float w = 120.0f) { return around ({ x, y }, w, 26.0f); };
    auto& driveValue  = readout (under (layout::drive.x, layout::valueY));
    auto& compValue   = readout (under (layout::comp.x, layout::valueY, 84.0f));
    auto& blendValue  = readout (under (layout::blend.x, layout::valueY));
    auto& midValue    = readout (under (layout::toneX[1], layout::midFreqValueY));
    auto& diCabValue  = readout (under (layout::diCab.x, layout::diCabValueY));
    auto& inValue     = readout (under (layout::inTitle.x, layout::levelValueY, 104.0f));
    auto& outValue    = readout (under (layout::outTitle.x, layout::levelValueY, 104.0f));
    Readout* effectValues[] { &readout (under (layout::effectX[0], layout::valueY, 90.0f)),
                              &readout (under (layout::effectX[1], layout::valueY, 90.0f)),
                              &readout (under (layout::effectX[2], layout::valueY, 90.0f)) };

    // ...DRIVE's as its dial reads, 0 to 10; the tones' in decibels — the bass's standard's three, the same on every
    // gear (Channels::toneOf); the levers' in decibels.
    auto* gain = amp.apvts.getParameter (params::preampGain);
    follow (params::preampGain, [gain, &driveValue] (float v) { driveValue.set (juce::String (gain->convertTo0to1 (v) * 10.0f, 1)); })->sendInitialUpdate();
    for (int t = 0; t < 3; ++t)
    {
        auto& value = readout (under (layout::toneX[(size_t) t], layout::toneValueY, 100.0f));
        follow (channels.toneOf (channels.active(), t).toRawUTF8(), [&value] (float v) { value.set (dbTenth (v)); })->sendInitialUpdate();
    }
    follow (params::inTrim,  [&inValue]  (float v) { inValue.set (dbTenth (v)); })->sendInitialUpdate();
    follow (params::outTrim, [&outValue] (float v) { outValue.set (dbTenth (v)); })->sendInitialUpdate();

    // ...and the bass's own, each bound through its own travel.
    bindTravel (comp, params::compThreshold, [] (float db) { return db / params::compThresholdMin; },
                [] (float t) { return t * params::compThresholdMin; }, [&compValue] (float db) { compValue.set (dbWhole (db)); });
    bindTravel (blend, params::ampBlend, [] (float v) { return v; }, [] (float t) { return t; },
                [&blendValue] (float v) { blendValue.set (percent (v)); });
    bindTravel (diCab, params::diMix, [] (float v) { return v / 100.0f; }, [] (float t) { return t * 100.0f; },
                [&diCabValue] (float v) { diCabValue.set (percent (v / 100.0f)); });
    const char* effectIds[] { params::octaveAmount, params::filterAmount, params::chorusAmount };
    const char* effectNames[] { "OCTAVE", "ENVELOPE", "CHORUS" };
    for (size_t i = 0; i < 3; ++i)
    {
        auto& value = *effectValues[i];
        bindTravel (*effectFaders[i], effectIds[i], [] (float v) { return v; }, [] (float t) { return t; },
                    [&value] (float v) { value.set (percent (v)); });
        sayLever (*effectFaders[i], effectNames[i], [this, id = effectIds[i]] { return 100.0f * getParam (id); });
    }
    const auto midId = params::eqBellHz (0, 0);
    bindTravel (midFreq, midId, midTravel, midHz, [&midValue] (float hz) { midValue.set (hertz (juce::jlimit (params::bassMidMinHz, params::bassMidMaxHz, hz))); });
    sayLever (midFreq, "MIDDLE", [this, midId] { return getParam (midId.toRawUTF8()); });

    // THE CHANNEL LEVER: B15 or SVT — the gear and everything it was left with.
    addToStage (lever);
    lever.setBounds (layout::leverBox.toNearestInt());
    lever.handLight = handLight;
    lever.onPlace   = [this] (int at) { go (at); };
    lever.onRelease = [this] { showChannel(); };       // held, it follows the hand; let go, it follows the state
    lever.setComponentID ("lever");

    // THE CUTS' SWITCHES: the HPF by the strings, the LPF by the hand — both always in (channels::Lineup::pinned), the
    // switch choosing between their positions; a right click on either its positions' frequencies.
    // One line a switch, in a column right of DI/CAB: the name, the end positions' words either side of the slot.
    const auto lineAt = [this] (SlideSwitch& sw, int line)
    {
        const auto slot = juce::Rectangle<float> (layout::switchSlotW, layout::switchSlotH)
                              .withCentre ({ layout::switchSlotX + 0.5f * layout::switchSlotW, layout::switchLineY[(size_t) line] });
        const float left = sw.nameRoom + sw.wordRoom + 6.0f, right = sw.wordRoom + 6.0f;
        const auto box = juce::Rectangle<float> (slot.getX() - left, slot.getY() - 3.0f, left + slot.getWidth() + right, slot.getHeight() + 6.0f);
        sw.slot = slot - box.getPosition();
        const auto& bg = Art::of().bg;
        sw.slotPicture = bg.getClippedImage ((layout::lowCutSlot * ((float) bg.getWidth() / face::stage::width)).toNearestInt());
        sw.em = face::stage::px13 * 12.0f / 13.0f;
        sw.handLight = handLight;
        addToStage (sw);
        sw.setBounds (box.toNearestInt());
    };
    lineAt (lowCut, 0);
    lineAt (highCut, 1);
    lineAt (ultraLo, 2);
    lineAt (ultraHi, 3);
    bindSwitch (lowCut, params::bassHpf);
    bindSwitch (highCut, params::bassLpf);
    ultraLo.setPosition (1, false);
    lowCut.onMenu  = [this] { bass::Menu m; bass::addHpf (m, m.items, amp.apvts); bass::show (std::move (m), &lowCut); };
    highCut.onMenu = [this] { bass::Menu m; bass::addLpf (m, m.items, amp.apvts); bass::show (std::move (m), &highCut); };
    lowCut.setComponentID ("lowCut");
    highCut.setComponentID ("highCut");


    // THE CABINET'S SCREEN: the cabinet that plays, by its name — OFF while it is out of the sound.
    cabinet.name = [this]
    {
        if (! channels.cabinetOn())
            return juce::String ("OFF");
        const int playing = channels.cabinet();
        return playing >= 0 ? Channels::cabinetName (channels.cabinets().getReference (playing)).toUpperCase() : juce::String();
    };
    cabinet.onClick = [this] { showCabinetList(); };
    cabinet.onMenu  = [this] { showCabs(); };
    cabinet.handLight = handLight;
    addToStage (cabinet);
    cabinet.setBounds (layout::cabinetScreen.reduced (3.0f).toNearestInt());
    cabinet.setComponentID ("cabinet");
    cabinet.refresh();

    // LEARN — from GATE's menu, or a double click on its lamp — takes the tuner's glass while it runs.
    addToStage (learnGlass);
    learnGlass.setBounds (layout::tunerScreen.toNearestInt());
    learnGlass.inset = { 10.0f, 4.0f };
    learnGlass.onShow = [this] (bool up) { tuner.setAlpha (up ? 0.0f : 1.0f); };

    // THE DOME, under the dashboard: the picture's alpha is its frame.
    stage.addAndMakeVisible (*view, 0);
    view->setBounds (layout::dome.toNearestInt());
    view->addMouseListener (&glassClicks, false);
    glassClicks.menu = [this] { showWindowMenu(); };

    // A prototype's hook for looking at the face on another channel without a hand on anything:
    // REDGROOVE_CHANNEL names one, from 0 to the last.
    if (const auto c = juce::SystemStats::getEnvironmentVariable ("REDGROOVE_CHANNEL", {}); c.isNotEmpty())
        engine.go (c.getIntValue());

    showChannel();

    // What the dome shows: the machine's choice, the forest unless it has said otherwise — drawn by this window's
    // context, which paints the face over it. A GPU that will not draw it says so in the family's log.
    view->setLogName (amp.loadedAs());
    view->setHost (&gl);
    view->setHearing ([&a = amp] { return a.takePlay(); });            // the forest answers the bass (WalkerDrive::hear)...
    view->setLit ([&a = amp] { return a.viewLit(); });                   // ...unless the player said not to (prefs::viewLit)
    view->setTempo ([&a = amp] { return (double) a.hostTempoBpm(); });   // the footfalls on the host's beat; standalone its own
    gl.setOpenGLVersionRequired (juce::OpenGLContext::openGL3_2);
    gl.setRenderer (this);
    gl.setComponentPaintingEnabled (true);
    gl.setContinuousRepainting (false);     // the view asks for each frame, at its own pace
    gl.attachTo (*this);
    view->setFrameRateCap ((int) prefs::store (amp.product()).get (frameRateKey, 60));
    view->setPicture ([] { return Art::still(); });                      // STILL is the walker's own still
    view->setMode ((redpaw::Windscreen::Mode) juce::jmax (0, landscapeWords.indexOf (prefs::store (amp.product()).get (landscapeKey, landscapeWords[0]).toString())));
    placeView();
}

RedGrooveEditor::~RedGrooveEditor()
{
    view->removeMouseListener (&glassClicks);
    gl.detach();
}

std::unique_ptr<redpaw::Drive> RedGrooveEditor::makeDrive()
{
    return std::make_unique<WalkerDrive> (2, 0.57);
}

Readout& RedGrooveEditor::readout (juce::Rectangle<float> box)
{
    auto& r = *readouts.emplace_back (std::make_unique<Readout> (cabinInk(), stage::px13));
    addToStage (r);
    r.setBounds (box.toNearestInt());
    return r;
}

template <typename Control>
void RedGrooveEditor::bindTravel (Control& control, const juce::String& paramId, std::function<float (float)> toTravel,
                                  std::function<float (float)> toPlain, std::function<void (float)> shown)
{
    auto* p = amp.apvts.getParameter (paramId);
    jassert (p != nullptr);
    if (p == nullptr)
        return;
    auto* a = follow (paramId.toRawUTF8(), [&control, toTravel, shown] (float v)
    {
        control.setValue (toTravel (v), false);
        if (shown)
            shown (v);
    });
    control.onGestureStart = [a] { a->beginGesture(); };
    control.onChange       = [a, toPlain] (float t) { a->setValueAsPartOfGesture (toPlain (t)); };
    control.onGestureEnd   = [a] { a->endGesture(); };
    control.defaultValue   = toTravel (p->convertFrom0to1 (p->getDefaultValue()));     // where a double click puts it
    a->sendInitialUpdate();
}

/** A SWITCH ON ITS PARAMETER: where it stands, the switch shows; a hand's throw is one step of the history. */
void RedGrooveEditor::bindSwitch (SlideSwitch& sw, const char* paramId)
{
    auto* a = follow (paramId, [&sw] (float v) { sw.setPosition (juce::roundToInt (v), false); });
    sw.onFlip = [a] (int to) { a->setValueAsCompleteGesture ((float) to); };
    a->sendInitialUpdate();
}

/** THE CABINETS, ON A CLICK ON THE SCREEN: the build's own under their shelf's folders, then the player's own, the one
    that plays ticked. A pick plays it, and brings the cabinet back into the sound. */
void RedGrooveEditor::showCabinetList()
{
    juce::PopupMenu menu;
    const auto& all = channels.cabinets();
    const int playing = channels.cabinetOn() ? channels.cabinet() : -1;
    juce::String heading = "-";
    for (int i = 0; i < all.size(); ++i)
    {
        const auto folder = i < channels.factoryCabinets() ? channels.cabinetFolder (i) : juce::String ("YOUR OWN");
        if (folder != heading)
        {
            heading = folder;
            menu.addSectionHeader (folder.isEmpty() ? juce::String ("DARWIN'S CAT") : folder.toUpperCase());
        }
        menu.addItem (i + 1, Channels::cabinetName (all.getReference (i)), true, i == playing);
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&cabinet),
                        [safe = juce::Component::SafePointer<RedGrooveEditor> (this)] (int picked)
                        {
                            if (safe == nullptr || picked <= 0)
                                return;
                            safe->channels.chooseCabinet (picked - 1);
                            safe->channels.setCabinetOn (true);
                            safe->cabinet.refresh();
                        });
}

/** The lever goes where the state's channel is — unless the hand holds it. */
void RedGrooveEditor::showSelector (int channel)
{
    if (! lever.isHeld() && lever.getPlace() != channel)
        lever.setPlace (channel, true, false);
    lever.repaint();
    cabinet.refresh();
    view->setGear (channel + 1);        // B15 walks in the first gear, SVT in the second
}

/** The number keys pull the lever: 1 is B15, 2 SVT. */
bool RedGrooveEditor::channelKey (const juce::KeyPress& key)
{
    const auto c = (juce::juce_wchar) key.getKeyCode();
    const int at = c >= '1' && c < '1' + lever.numPlaces() ? (int) (c - '1') : -1;
    if (at < 0)
        return false;
    if (! lever.isHeld())
        lever.setPlace (at, true);
    return true;
}

/** THE TONES' RINGS GO WITH THEIR KNOBS: each round its knob where the rows put it, the size the row gives it; a ring
    whose knob the channel has not, hidden with it. */
void RedGrooveEditor::stripsLaid (const face::Strips& laid)
{
    for (size_t t = 0; t + firstToneRing < rings.size(); ++t)
    {
        auto& ring = *rings[t + firstToneRing];
        const bool shown = t < laid.tones.size();
        if (shown)
            ring.place (laid.tones[t].knob.getCentre(), (layout::toneSocket * 0.5f - layout::ringInset) * laid.toneScale,
                        layout::ringWidth * laid.toneScale);
        ring.setVisible (shown);
    }
}

/** The walker's own part of the site's demo (Console::webExtras): the dome — the box the world is drawn in — the
    still it ships, a poster for each channel, the dome's first frame through the glass; and under places.json's
    "world" (and on its own, walker.json) every control the console has not already said: the channel lever, the
    bass's knobs and their rings, the three effects' columns, MIDDLE's frequency across, the cuts' two switches, the
    squeeze's column, the cabinet's screen and every reading, each with the parameter it moves and how. Every box is
    the control's own where it stands on the stage, so a new layout exports itself. */
void RedGrooveEditor::webExtras (const juce::File& dir, juce::DynamicObject& kit, juce::DynamicObject& world)
{
    writeForWeb (dir.getChildFile ("still.webp"), Art::still());

    using Obj = juce::DynamicObject;
    const auto rect = [] (juce::Rectangle<float> r)
    {
        auto* o = new Obj();
        o->setProperty ("x", r.getX()); o->setProperty ("y", r.getY());
        o->setProperty ("w", r.getWidth()); o->setProperty ("h", r.getHeight());
        return juce::var (o);
    };
    const auto point = [] (juce::Point<float> p) { auto* o = new Obj(); o->setProperty ("x", p.x); o->setProperty ("y", p.y); return juce::var (o); };
    const auto boxOf = [] (const juce::Component& c) { return c.getBoundsInParent().toFloat(); };
    const auto list = [] (std::initializer_list<juce::var> items) { juce::Array<juce::var> a; for (const auto& v : items) a.add (v); return juce::var (a); };

    auto* o = new Obj();
    o->setProperty ("dome", rect (layout::dome));
    o->setProperty ("gears", 2);
    o->setProperty ("channelGears", list ({ 1, 2 }));       // B15 walks in gear 1, SVT in gear 2; no TURBO
    o->setProperty ("horizon", 0.57);                      // a fraction of the dome from its top (WalkerDrive)

    // THE CHANNEL LEVER: its places top to bottom, the slot it runs in, each name where it stands.
    {
        auto* l = new Obj();
        l->setProperty ("box", rect (boxOf (lever)));
        l->setProperty ("slotX", layout::leverX);
        juce::Array<juce::var> stops, names;
        for (const float y : layout::leverPlaces) stops.add (y);
        const char* const words[] { "B15", "SVT" };
        for (size_t i = 0; i < layout::leverNames.size(); ++i)
        {
            auto* n = new Obj();
            n->setProperty ("name", words[i]); n->setProperty ("at", point (layout::leverNames[i]));
            names.add (juce::var (n));
        }
        l->setProperty ("stops", stops);
        l->setProperty ("names", names);
        l->setProperty ("capWidth", ChannelLever::capWidth);
        l->setProperty ("capHeight", ChannelLever::capHeight);
        l->setProperty ("does", "the channel: place i is channel i (the lineup's), its gear i + 1");
        o->setProperty ("lever", l);
    }

    // THE BASS'S KNOBS (DRIVE and the tones are the console's): the tone knob's render, a pointer drawn on it.
    {
        struct K { const char* id; const char* name; const juce::Component* c; const char* param; const char* travel; };
        const K knobs[] {
            { "comp",  "COMP",   &comp,  params::compThreshold, "travel = threshold / -48 dB: 0 dB fully left, -48 dB fully right" },
            { "blend", "BLEND",  &blend, params::ampBlend,      "travel = the value, 0..1" },
            { "diCab", "DI/CAB", &diCab, params::diMix,         "travel = the value / 100, 0..100" } };
        juce::Array<juce::var> a;
        for (const auto& k : knobs)
        {
            auto* v = new Obj();
            v->setProperty ("id", k.id); v->setProperty ("name", k.name); v->setProperty ("box", rect (boxOf (*k.c)));
            v->setProperty ("sprite", "knob-tone"); v->setProperty ("param", k.param); v->setProperty ("travel", k.travel);
            a.add (juce::var (v));
        }
        o->setProperty ("knobs", a);
        auto* r = new Obj();           // a ring round every knob, lit along its travel: inset from its socket's edge
        r->setProperty ("inset", layout::ringInset); r->setProperty ("width", layout::ringWidth); r->setProperty ("driveWidth", layout::driveRingWidth);
        r->setProperty ("sockets", list ({ juce::var ("drive"), juce::var ("comp"), juce::var ("blend"), juce::var ("diCab"), juce::var ("tones") }));
        o->setProperty ("rings", r);
        o->setProperty ("knobNameY", layout::knobNameY);
        o->setProperty ("diCabNameY", layout::diCabNameY);
    }

    // THE COLUMNS: the three effects' amounts, upright, the fader cap's render; MIDDLE's frequency across.
    {
        struct F { const char* id; const char* name; const juce::Component* c; const char* param; };
        const F faders[] { { "octave", "OCTAVE", &octave, params::octaveAmount }, { "envelope", "ENVELOPE", &envelope, params::filterAmount },
                           { "chorus", "CHORUS", &chorus, params::chorusAmount } };
        juce::Array<juce::var> a;
        for (const auto& f : faders)
        {
            auto* v = new Obj();
            v->setProperty ("id", f.id); v->setProperty ("name", f.name); v->setProperty ("box", rect (boxOf (*f.c)));
            v->setProperty ("sprite", "fader-cap"); v->setProperty ("capLength", layout::faderCapLength);
            v->setProperty ("param", f.param); v->setProperty ("travel", "the value, 0..1, bottom to top"); v->setProperty ("reading", "percent");
            a.add (juce::var (v));
        }
        o->setProperty ("columns", a);
        auto* m = new Obj();
        m->setProperty ("box", rect (boxOf (midFreq))); m->setProperty ("slot", rect (layout::midFreqSlot));
        m->setProperty ("sprite", "fader-cap"); m->setProperty ("capLength", layout::midFreqCap); m->setProperty ("across", true);
        m->setProperty ("param", params::eqBellHz (0, 0));
        auto* map = new Obj();
        map->setProperty ("kind", "log"); map->setProperty ("minHz", params::bassMidMinHz); map->setProperty ("maxHz", params::bassMidMaxHz);
        map->setProperty ("travel", "log (hz / minHz) / log (maxHz / minHz), left to right");
        m->setProperty ("map", map);
        m->setProperty ("reading", "hertz");
        o->setProperty ("midFreq", m);
    }

    // THE CUTS' SWITCHES: two positions each, a word over each, the one it stands at lit; the parameter's value is the position.
    {
        struct S { const char* id; const SlideSwitch* sw; const char* left; const char* right; const char* param; };
        const S switches[] { { "lowCut", &lowCut, "5 STR", "4 STR", params::bassHpf }, { "highCut", &highCut, "FINGERS", "SLAP", params::bassLpf } };
        juce::Array<juce::var> a;
        for (const auto& w : switches)
        {
            auto* v = new Obj();
            const auto box = boxOf (*w.sw);
            v->setProperty ("id", w.id); v->setProperty ("box", rect (box)); v->setProperty ("slot", rect (w.sw->slot + box.getPosition()));
            v->setProperty ("wordsY", w.sw->wordsY + box.getY()); v->setProperty ("wordRoom", w.sw->wordRoom);
            v->setProperty ("words", list ({ juce::var (w.left), juce::var (w.right) }));
            v->setProperty ("param", w.param); v->setProperty ("values", list ({ 0, 1 }));
            a.add (juce::var (v));
        }
        o->setProperty ("switches", a);
    }

    // THE SQUEEZE: COMP's gain reduction as a column filling down, its full span; the cabinet's screen.
    {
        auto* g = new Obj();
        g->setProperty ("box", rect (boxOf (squeeze))); g->setProperty ("slot", rect (layout::grSlot));
        g->setProperty ("rangeDb", GrColumn::rangeDb); g->setProperty ("reads", "COMP's gain reduction in dB (AmpProcessor::compGrDb)");
        o->setProperty ("squeeze", g);
        auto* c = new Obj();
        c->setProperty ("box", rect (boxOf (cabinet))); c->setProperty ("shows", "the playing cabinet's name, a list on a click");
        o->setProperty ("cabinetScreen", c);
    }

    // THE READINGS, each under its control, in the order the face makes them: the box and how it writes the value.
    {
        struct R { const char* id; const char* format; };
        const R names[] { { "drive", "the dial, 0.0..10.0" }, { "comp", "whole dB: -24 dB" }, { "blend", "percent: 30%" },
                          { "midFreq", "hertz: 400 Hz, 1.25 kHz" }, { "diCab", "percent of 100: 30%" }, { "in", "dB, a tenth: +1.5 dB, 0.0 dB" },
                          { "out", "dB, a tenth" }, { "octave", "percent" }, { "envelope", "percent" }, { "chorus", "percent" },
                          { "tone0", "dB, a tenth" }, { "tone1", "dB, a tenth" }, { "tone2", "dB, a tenth" } };
        jassert (readouts.size() == std::size (names));
        juce::Array<juce::var> a;
        for (size_t i = 0; i < readouts.size() && i < std::size (names); ++i)
        {
            auto* v = new Obj();
            v->setProperty ("id", names[i].id); v->setProperty ("box", rect (boxOf (*readouts[i]))); v->setProperty ("format", names[i].format);
            a.add (juce::var (v));
        }
        o->setProperty ("readings", a);
        o->setProperty ("readingPx", stage::px13);
    }

    for (const auto& p : o->getProperties())       // all of it in places.json's "world", which is all the site reads...
        world.setProperty (p.name, p.value);
    dir.getChildFile ("walker.json").replaceWithText (juce::JSON::toString (juce::var (o)));     // ...and on its own

    auto* window = new juce::DynamicObject();
    window->setProperty ("x", layout::dome.getX());
    window->setProperty ("y", layout::dome.getY());
    window->setProperty ("w", layout::dome.getWidth());
    window->setProperty ("h", layout::dome.getHeight());
    kit.setProperty ("window", juce::var (window));
    juce::Array<juce::var> stills;
    stills.add ("still.webp");
    kit.setProperty ("stills", stills);

    webPoses.count = channels.count();
    webPoses.opening = channels.active();
    webPoses.standAt = [this] (int at) { go (at); };
    webPoses.firstFrame = [this] (int, std::function<void (juce::Image)> done)
    {
        const auto box = layout::dome * 2.0f;
        view->firstFrame (makeDrive(), juce::roundToInt (box.getWidth()), juce::roundToInt (box.getHeight()), std::move (done));
    };
    webPoses.live = "the dome's first frame for the channel: the walker on the GPU from its world's start, in the channel's "
                    "gear, settled, nothing stepped (t = 0); the top poster is the channel the app opens at";
    webPoses.still = [] (int) { return Art::still(); };
    webPoses.window = layout::dome;
    webPoses.hidden = { view.get() };
}

//==============================================================================
/** What the dome shows — the forest on the GPU, the 1993 landscape on the CPU, or the still — and how often; whether
    the forest answers the bass; and full screen. The machine's, kept with its settings. */
void RedGrooveEditor::addWindowSections (juce::PopupMenu& menu)
{
    const auto self = [] (Console& e) -> RedGrooveEditor& { return static_cast<RedGrooveEditor&> (e); };

    if (! amp.runsInHost())
    {
        menu.addSectionHeader ("WINDOW");
        addChoice (menu, "Full screen", fullScreen, [self] (Console& e) { self (e).setFullScreen (! self (e).fullScreen); });
    }

    // Where the GPU's forest cannot be had the CPU's drives instead, and the tick says what runs.
    menu.addSectionHeader ("LANDSCAPE");
    const auto running = view->mode();
    for (int i = 0; i < landscapeNames.size(); ++i)
        addChoice (menu, landscapeNames[i], (int) running == i,
                   [self, i] (Console& e) { self (e).chooseLandscape ((redpaw::Windscreen::Mode) i); },
                   i != (int) redpaw::Windscreen::Mode::gpu || ! view->status().gpuBroken);
    for (const int fps : { 60, 30 })
        addChoice (menu, juce::String (fps) + " frames a second", view->frameRateCap() == fps,
                   [self, fps] (Console& e) { self (e).chooseFrameRate (fps); }, running != redpaw::Windscreen::Mode::still);
    // The forest answering the bass (prefs::viewLit) — the machine's, on unless asked otherwise on this face.
    const bool lit = prefs::viewLitByGuitar (amp.product());
    addChoice (menu, "Lit by the bass", lit, [self, lit] (Console& e) { prefs::setViewLitByGuitar (self (e).amp.product(), ! lit); });
}

void RedGrooveEditor::showWindowMenu()
{
    juce::PopupMenu menu;
    addWindowSections (menu);
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

void RedGrooveEditor::chooseLandscape (redpaw::Windscreen::Mode mode)
{
    prefs::store (amp.product()).set (landscapeKey, landscapeWords[(int) mode]);
    view->setMode (mode);
}

void RedGrooveEditor::chooseFrameRate (int fps)
{
    prefs::store (amp.product()).set (frameRateKey, fps);
    view->setFrameRateCap (fps);
}

/** Where the dome is in the editor's own pixels, for the GL thread: the stage's scale and place. */
void RedGrooveEditor::placeView()
{
    const auto at = (layout::dome * stageScale() + stageOrigin()).toNearestInt();
    glassX.store (at.getX());
    glassY.store (at.getY());
    glassW.store (at.getWidth());
    glassH.store (at.getHeight());
    gl.triggerRepaint();
}

void RedGrooveEditor::newOpenGLContextCreated()
{
    view->hostContextCreated();
}

void RedGrooveEditor::renderOpenGL()
{
    const float k = (float) gl.getRenderingScale();
    const int fw = juce::roundToInt ((float) getWidth() * k), fh = juce::roundToInt ((float) getHeight() * k);
    juce::OpenGLHelpers::clear (juce::Colours::black);

    const auto glass = juce::Rectangle<float> ((float) glassX.load(), (float) glassY.load(), (float) glassW.load(), (float) glassH.load()) * k;
    view->hostRender (glass.toNearestInt(), fw, fh);
}

void RedGrooveEditor::openGLContextClosing()
{
    view->hostContextClosing();
}

} // namespace orbitamp::redgroove
