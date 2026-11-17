// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "UnderwaterEditor.h"

#include "UnderwaterFaceArt.h"
#include "UnderwaterLayout.h"
#include "UnderwaterLineup.h"

#include "view/UnderwaterArt.h"
#include "view/UnderwaterDrive.h"

#include "../ui/BassControls.h"
#include "../ui/CabWindow.h"
#include "../ui/FactoryMissing.h"
#include "../ui/Prefs.h"

#include <BinaryData.h>

namespace orbitamp::underwater
{

juce::AudioProcessorEditor* makeEditor (AmpProcessor& p)
{
    // No knobs to draw without their renders, which lie in the factory folder: where they are missing, the window
    // says so, and where they belong (ui/FactoryMissing.h).
    if (! FaceArt::present())
        return new ui::FactoryMissingEditor (p, p.product());
    return new UnderwaterEditor (p);
}

using namespace face;
using redgroove::Readout;
using redgroove::writeAsIs;

namespace
{
    /** What the window shows, and how often — the machine's, kept with its settings: the landscape as a word, in the
        order of Windscreen::Mode, and what the menu calls each. */
    const juce::Identifier landscapeKey { "landscape" };
    const juce::Identifier frameRateKey { "landscape_fps" };
    const juce::StringArray landscapeWords { "sea", "1993", "still" };
    const juce::StringArray landscapeNames { "The sea", "1993", "Still" };

    // Where the pointer is drawn on the knobs' renders — the walker's, whose renders these are.
    constexpr ArtKnob::Pointer drivePointer { 0.30f, 0.66f, 0.06f };
    constexpr ArtKnob::Pointer tonePointer  { 0.30f, 0.64f, 0.07f };

    /** THE HAND'S LIGHT: a soft orange round whatever the hand is over or on — a line, never a wash. */
    const juce::Colour handLight = orange.withAlpha (0.8f);

    /** WHAT THE WORDS ARE WRITTEN IN, as the walker's: the controls' names lilac, their values near white, a state the
        family's orange — each with a dark edge under it, off the gunmetal. */
    Ink cabinInk()
    {
        Ink ink;
        ink.words  = juce::Colour (0xffb9a6ff);
        ink.values = juce::Colour (0xfff2eff8);
        ink.edge   = juce::Colour (0xaa000000);
        return ink;
    }

    const juce::Colour titleInk { 0xffded9e8 };     // the panels' titles, a little lighter than the names
    const juce::Colour makerInk { 0xffd3cedd };     // the maker's words on the plates, quieter than the titles
    const juce::Colour keyCap { 0xff22222e }, keyRim { 0xff8b6cff };
    const juce::Colour ringLight { 0xff8b6cff };    // the rings round the knobs: the family's violet

    juce::Rectangle<float> around (juce::Point<float> c, float w, float h)
    {
        return juce::Rectangle<float> (w, h).withCentre (c);
    }

    /** A WORD PRINTED ON THE PANEL, centred on `at`, a dark edge under it. */
    void print (juce::Graphics& g, const juce::String& text, juce::Point<float> at, float em, juce::Colour colour,
                float tracking = 0.08f)
    {
        cabinInk().label (g, text, juce::Rectangle<float> (360.0f, 32.0f).withCentre (at), em, colour, tracking);
    }

    /** ...set condensed into `room` about its centre, as tall, where it is wider. */
    void printInto (juce::Graphics& g, const juce::String& text, juce::Point<float> at, float room, float em, juce::Colour colour)
    {
        const float wide = juce::GlyphArrangement::getStringWidth (labelFont (em).withExtraKerningFactor (0.08f), text.toUpperCase());
        const juce::Graphics::ScopedSaveState keep (g);
        if (wide > room)
            g.addTransform (juce::AffineTransform::scale (room / wide, 1.0f, at.x, at.y));
        print (g, text, at, em, colour);
    }

    /** ...a title, from its left edge at `at.x`. */
    void printFromLeft (juce::Graphics& g, const juce::String& text, juce::Point<float> at, float em, juce::Colour colour)
    {
        const float wide = juce::GlyphArrangement::getStringWidth (labelFont (em).withExtraKerningFactor (0.08f), text.toUpperCase());
        print (g, text, { at.x + 0.5f * wide, at.y }, em, colour);
    }

    juce::String dbTenth (float v) { return (v > 0.04f ? "+" : "") + juce::String (v, 1) + " dB"; }
    juce::String percent (float share) { return juce::String (juce::roundToInt (share * 100.0f)) + "%"; }

    /** A PACK'S DIAL in decibels where its tone slot stands (0..1), as the rig player plays it — the walker's reading. */
    float dialDb (const namz::rig::Tone& tone, float v)
    {
        if (tone.sections.empty())
            return 0.0f;
        const double t = felitronics::rigplayer::travelOf (tone, (double) v);
        const auto& band = tone.sections.front();
        return (float) (t < 0.0 ? -t * band.dbAtMin : t * band.dbAtMax);
    }

    /** THE LEVER'S NAMES beside its stops, each from the same left edge, set in the room short of DRIVE's socket. */
    const juce::StringArray leverWords { "PARK", "CLEAN", "DIRT", "DRIVE" };

    std::vector<juce::Point<float>> leverNameCentres()
    {
        using namespace layout;
        std::vector<juce::Point<float>> at;
        const auto font = labelFont (stage::px13).withExtraKerningFactor (0.04f);
        for (int i = 0; i < leverWords.size(); ++i)
        {
            const float wide = juce::jmin (leverNameRoom, juce::GlyphArrangement::getStringWidth (font, leverWords[i]) + 2.0f);
            at.push_back ({ leverNameLeft + 0.5f * wide, leverStops[(size_t) i] });
        }
        return at;
    }

    /** WHERE THE CONSOLE'S CONTROLS STAND on the dashboard (UnderwaterLayout.h). */
    Console::Places cabinPlaces()
    {
        using namespace layout;
        using namespace stage;

        Console::Places p;
        p.drive = around (drive, driveKnob, driveKnob);

        // The tones: as many knobs as the playing channel has, on the three sockets the picture has.
        const float pitch = toneX[1] - toneX[0];
        p.toneStrip.area = juce::Rectangle<float> (2.0f * pitch + toneSocket + 1.0f, toneSocket).withCentre ({ toneX[1], toneY });
        p.toneStrip.pitch = pitch;
        p.toneStrip.size = toneSocket;
        p.toneStrip.minScale = 1.0f;
        p.toneStrip.spread = true;          // CLEAN's two in the outer sockets, the middle one bare
        p.toneKnob = toneKnob;
        p.toneNameY = nameY;
        p.toneNameEm = px13;
        p.toneSignDrop = 0.0f;

        // No filter keys; the six keys of the history and the registers in their holes, the history's wider.
        p.filterKeys = false;
        for (size_t i = 0; i < p.holes.size(); ++i)
        {
            p.holes[i] = { keyX[i], keyRowY };
            const float hole = i < (size_t) ovFirstRegister ? historyHole : registerHole;
            p.holeKeys[i] = around (p.holes[i], hole - 2.0f * keyInset, keyHole - 2.0f * keyInset);
        }
        p.holeKeySize = keyHole - 2.0f * keyInset;

        // No REVERB, DELAY or CAB plates: REVERB is a fader of the effects', and CAB's box is the cabinet's screen.
        p.plates = false;
        p.sliderNames[2] = cabinetScreen;

        p.faderCapLength = faderCapLength;
        p.faderCapSquash = faderCapSquash;
        p.inFader  = inFaderSlot.withSizeKeepingCentre (faderWidth, inFaderSlot.getHeight());
        p.outFader = outFaderSlot.withSizeKeepingCentre (faderWidth, outFaderSlot.getHeight());
        p.inMeter  = inMeterSlot.reduced (2.0f, 4.0f);
        p.outMeter = outMeterSlot.reduced (2.0f, 4.0f);

        p.tuner = tunerScreen.reduced (6.0f, 3.0f);
        p.tunerStrip = true;
        p.tunerMenuOnClick = true;

        // The lamps, drawn by code: each overload over its meter, GATE's and LIMITER's at the left of their slots.
        p.lampDot = 8.0f;
        p.lamps[inClipLamp]  = around ({ inMeterSlot.getCentreX(), inMeterSlot.getY() - 9.0f }, 14.0f, 14.0f);
        p.lamps[outClipLamp] = around ({ outMeterSlot.getCentreX(), outMeterSlot.getY() - 9.0f }, 14.0f, 14.0f);
        p.lamps[gateLamp]    = around ({ gateSlot.getX() + 13.0f, gateSlot.getCentreY() }, 14.0f, 14.0f);
        p.lamps[limiterLamp] = around ({ limiterSlot.getX() + 13.0f, limiterSlot.getCentreY() }, 14.0f, 14.0f);
        p.gateDoor    = gateSlot.withTrimmedLeft (24.0f).withTrimmedRight (2.0f);
        p.limiterDoor = limiterSlot.withTrimmedLeft (24.0f).withTrimmedRight (2.0f);
        p.doorsFit = true;
        p.limiterWord = "LIMIT";

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
        const auto& art = FaceArt::of();
        const auto mark = FaceArt::mark();
        return { art.knobDrive, drivePointer, art.knobTone, tonePointer,
                 art.faderCap,
                 art.faderCap,
                 { &art.none, &art.none, &art.none, &art.none },
                 mark.isNotEmpty() ? mark : juce::String::fromUTF8 (BinaryData::catlogo_svg, BinaryData::catlogo_svgSize),
                 FaceArt::midiCommands(),
                 handLight,
                 cabinInk(),
                 juce::Colour (0xff9a7aff) };
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

    // The panel, unless the world is drawing it with the sea's light on it — and always in a picture taken.
    const bool worldHasIt = onScreen && worldDrawsPanel != nullptr && worldDrawsPanel();
    if (const auto& bg = FaceArt::of().bg; bg.isValid() && ! worldHasIt)
        g.drawImage (bg, juce::Rectangle<float> (0.0f, 0.0f, width, height));

    // The family's cat, cut into the maker's plate; its words on the plates either side of it.
    engraveMark (g, emblemMark, scale);
    print (g, "POWERED BY", poweredByPlate.getCentre(), px13, makerInk);
    print (g, "DARWIN'S CAT", makerNamePlate.getCentre(), px13, makerInk);

    // The panels' titles.
    printFromLeft (g, "TUNER", tunerTitle, px13, titleInk);
    printFromLeft (g, "KEYS", keysTitle, px13, titleInk);
    print (g, "IN", inTitle, px13, titleInk);
    printFromLeft (g, "AMP", ampTitle, px13, titleInk);
    printFromLeft (g, "TONE", toneTitle, px13, titleInk);
    printFromLeft (g, "EFFECTS", effectsTitle, px13, titleInk);
    printFromLeft (g, "CABINET", cabinetTitle, px13, titleInk);
    print (g, "OUT", outTitle, px13, titleInk);

    // DRIVE, the hero, a size up and in the values' white, in the titles' row its socket leaves; BLEND and the effects in
    // the names' row; the tones' names are the playing channel's (paintStrips).
    const auto ink = cabinInk();
    print (g, "DRIVE", { drive.x, titleY }, px15, ink.values);
    print (g, "BLEND", { blendSlot.getCentreX(), nameY }, px13, ink.words);
    const char* effects[] { "OCTAVE", "CHORUS", "REVERB" };
    for (size_t i = 0; i < effectX.size(); ++i)
        printInto (g, effects[i], { effectX[i], nameY }, 80.0f, px13, ink.words);
}

/** THE PLAYING CHANNEL'S TONES' NAMES over their knobs. */
void Cabin::paintStrips (juce::Graphics& g, float) const
{
    const auto ink = cabinInk();
    for (const auto& t : strips.tones)
        ink.label (g, t.name, t.label.withSizeKeepingCentre (t.label.getWidth() + 40.0f, t.label.getHeight()), stage::px13, ink.words);
}

//==============================================================================
UnderwaterEditor::UnderwaterEditor (AmpProcessor& p)
    : face::Console (p, std::make_unique<Cabin>(), cabinPlaces(), cabinLook()),
      view (std::make_unique<redpaw::Windscreen> (makeDrive())),
      blend (FaceArt::of().faderCap), octave (FaceArt::of().faderCap), chorus (FaceArt::of().faderCap), reverb (FaceArt::of().faderCap),
      lever (cabinInk(), leverWords, layout::leverStops, layout::leverX, leverNameCentres(), layout::leverBox.getPosition()),
      lowCut (cabinInk(), "STR", "4", "5", 2, true),
      highCut (cabinInk(), "STYL", "F", "S"),
      cabinet (cabinInk(), FaceArt::of().faderCap, layout::faderCapLength, layout::faderCapSquash),
      park (p.bypassParameter(), [this] (float v) { showPark (v > 0.5f); }, nullptr)
{
    // Everything this constructor sets is the face settling onto the state, not a player's edit.
    const felitronics::appkit::CompareHistory::ScopedSuppress settling (amp.history);

    // THE SIX KEYS: push keys, each its word on its cap, as the walker's.
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

    // THE RINGS, one round every knob in its socket's groove, the tones' where the rows put them (stripsLaid).
    const auto ringFor = [this] (const face::ArtKnob& knob, juce::Point<float> centre, float socket, float width)
    {
        rings.push_back (std::make_unique<face::KnobRing> (knob, ringLight, centre, socket * 0.5f - layout::ringInset, width));
        addToStage (*rings.back());
    };
    ringFor (driveKnob(), layout::drive, layout::driveSocket, layout::driveRingWidth);
    for (int t = 0; t < Channels::maxTones; ++t)
        ringFor (toneKnob (t), { layout::toneX[0], layout::toneY }, layout::toneSocket, layout::ringWidth);
    stripsLaid (strips);

    // THE FADERS: BLEND in the AMP, the three effects upright in their slots. Each says where it stands in the tag over
    // it while it is held.
    const auto faderIn = [this] (face::ColumnFader& fader, juce::Rectangle<float> box)
    {
        fader.capLength = layout::faderCapLength;
        fader.capSquash = layout::faderCapSquash;
        fader.handLight = handLight;
        fader.marks = 6;
        fader.longMark = -1;
        addToStage (fader);
        fader.setBounds (box.toNearestInt());
    };
    faderIn (blend, layout::blendSlot.withSizeKeepingCentre (layout::faderWidth, layout::blendSlot.getHeight()));
    blend.marks = 4;
    face::ColumnFader* effectFaders[] { &octave, &chorus, &reverb };
    for (size_t i = 0; i < 3; ++i)
        faderIn (*effectFaders[i], around ({ layout::effectX[i], layout::effectY }, layout::effectFaderWidth, layout::effectSlotHeight));

    blend.setComponentID ("blend");
    octave.setComponentID ("octave");
    chorus.setComponentID ("chorus");
    reverb.setComponentID ("reverb");

    // THE READINGS, one under every control.
    const auto under = [] (float x, float y, float w = 120.0f) { return around ({ x, y }, w, 26.0f); };
    auto& driveValue = readout (under (layout::drive.x, layout::driveValueY));
    blendValue = &readout (under (layout::blendSlot.getCentreX(), layout::valueY, 84.0f));
    auto& inValue  = readout (under (layout::inTitle.x, layout::levelValueY, 104.0f));
    auto& outValue = readout (under (layout::outTitle.x, layout::levelValueY, 104.0f));
    Readout* effectValues[] { &readout (under (layout::effectX[0], layout::valueY, 90.0f)),
                              &readout (under (layout::effectX[1], layout::valueY, 90.0f)),
                              &readout (under (layout::effectX[2], layout::valueY, 90.0f)) };

    // ...DRIVE's as its dial reads, 0 to 10; the tones' in decibels — the pack's dials, each its band's gain where it
    // stands, or on a pack with no tone the bass's standard's three; the levers' in decibels.
    auto* gain = amp.apvts.getParameter (params::preampGain);
    follow (params::preampGain, [gain, &driveValue] (float v) { driveValue.set (juce::String (gain->convertTo0to1 (v) * 10.0f, 1)); })->sendInitialUpdate();
    const auto dials = channels.tonesOf (channels.active());
    for (int t = 0; t < 3; ++t)
    {
        auto& value = readout (under (layout::toneX[(size_t) t], layout::valueY, 100.0f));
        const auto id = channels.toneOf (channels.active(), t);
        if (id.isEmpty())
            continue;
        if (t < (int) dials.size())
            follow (id.toRawUTF8(), [&value, tone = dials[(size_t) t]] (float v) { value.set (dbTenth (dialDb (tone, v))); })->sendInitialUpdate();
        else
            follow (id.toRawUTF8(), [&value] (float v) { value.set (dbTenth (v)); })->sendInitialUpdate();
    }
    follow (params::inTrim,  [&inValue]  (float v) { inValue.set (dbTenth (v)); })->sendInitialUpdate();
    follow (params::outTrim, [&outValue] (float v) { outValue.set (dbTenth (v)); })->sendInitialUpdate();

    // ...and the bass's own, each bound through its own travel: BLEND and OCTAVE and CHORUS their 0..1, REVERB its
    // share in percent.
    bindTravel (blend, params::ampBlend, [] (float v) { return v; }, [] (float t) { return t; },
                [this] (float v) { blendValue->set (percent (v)); });
    sayLeverAs (blend, "BLEND", [this] { return percent (getParam (params::ampBlend)); });
    const char* effectIds[] { params::octaveAmount, params::chorusAmount, params::reverbMix };
    const char* effectNames[] { "OCTAVE", "CHORUS", "REVERB" };
    for (size_t i = 0; i < 3; ++i)
    {
        auto& value = *effectValues[i];
        const float full = i == 2 ? 100.0f : 1.0f;
        bindTravel (*effectFaders[i], effectIds[i], [full] (float v) { return v / full; }, [full] (float t) { return t * full; },
                    [&value, full] (float v) { value.set (percent (v / full)); });
        sayLeverAs (*effectFaders[i], effectNames[i], [this, id = effectIds[i], full] { return percent (getParam (id) / full); });
    }

    // THE LEVER: PARK over the three gears. PARK takes the whole amp out (the amp's bypass) and leaves the channel as it
    // was; a gear brings the amp back in, on that channel.
    addToStage (lever);
    lever.setBounds (layout::leverBox.toNearestInt());
    lever.nameRoom = layout::leverNameRoom;
    lever.handLight = handLight;
    lever.onPlace = [this] (int at)
    {
        if (at == 0)
        {
            park.setValueAsCompleteGesture (1.0f);
            return;
        }
        if (parked)
            park.setValueAsCompleteGesture (0.0f);
        go (at - 1);
    };
    lever.onRelease = [this] { showChannel(); };
    lever.setComponentID ("lever");

    // THE CUTS' SWITCHES under the cabinet's screen, each in the picture's own slot: the HPF by the strings, the LPF by
    // the hand — both always in (channels::Lineup::pinned), the switch choosing between their positions; a right click
    // on either its positions' frequencies.
    const auto lineAt = [this] (redgroove::SlideSwitch& sw, juce::Rectangle<float> slotOnStage)
    {
        const float left = sw.nameRoom + sw.wordRoom + 6.0f, right = sw.wordRoom + 6.0f;
        const auto box = juce::Rectangle<float> (slotOnStage.getX() - left, slotOnStage.getY() - 3.0f,
                                                 left + slotOnStage.getWidth() + right, slotOnStage.getHeight() + 6.0f);
        sw.slot = slotOnStage - box.getPosition();
        const auto& bg = FaceArt::of().bg;
        sw.slotPicture = bg.getClippedImage ((slotOnStage * ((float) bg.getWidth() / stage::width)).toNearestInt());
        sw.handLight = handLight;
        addToStage (sw);
        sw.setBounds (box.toNearestInt());
    };
    lineAt (lowCut, layout::stringsSlot);
    lineAt (highCut, layout::fingersSlot);
    const auto sayOver = [this] (redgroove::SlideSwitch& sw, juce::String words)
    {
        sw.onHover = [this, &sw, words] (bool over)
        {
            const auto at = sw.getBoundsInParent().toFloat();
            nameTag.show ({ at.getCentreX(), at.getY() - 16.0f }, over ? words : juce::String());
        };
    };
    sayOver (lowCut, bass::strHint);
    sayOver (highCut, bass::stylHint);
    bindSwitch (lowCut, params::bassHpf);
    bindSwitch (highCut, params::bassLpf);
    lowCut.onMenu  = [this] { bass::Menu m; bass::addHpf (m, m.items, amp.apvts); bass::show (std::move (m), &lowCut); };
    highCut.onMenu = [this] { bass::Menu m; bass::addLpf (m, m.items, amp.apvts); bass::show (std::move (m), &highCut); };
    lowCut.setComponentID ("lowCut");
    highCut.setComponentID ("highCut");

    // THE CABINET'S SLIDER in its screen: a notch a cabinet of the bass's dozen, set and fixed, as the walker's; over it,
    // the cabinet that plays by its name — the slot's under the hand while it moves the slider, OFF while the cabinet is
    // out of the sound — and its number after CABINET in the title's row.
    cabinet.name = [this]
    {
        const auto named = [this] (int slot)
        {
            const auto name = channels.slotName (slot);
            return name.isNotEmpty() ? name : CabWindow::labelOf (AmpProcessor::cabSynthOf (channels.slotFrom (slot)));
        };
        if (dragAt >= 0)
            return named (dragAt);
        if (! channels.cabinetOn())
            return juce::String ("OFF");
        const auto playing = AmpProcessor::cabSynthOf (amp.cabChoice());
        if (const auto slot = AmpProcessor::cabSynthOf (channels.slotFrom (channels.cabinetSlot())); playing.valid && slot.valid && playing.state == slot.state)
            return named (channels.cabinetSlot());
        return CabWindow::labelOf (playing);
    };
    cabinet.notches = channels.cabinetSlots();
    cabinet.notch   = [this] { return channels.cabinetSlot(); };
    cabinet.onMove  = [this] (int at) { hearSlot (at); };
    cabinet.onLetGo = [this] (int at) { chooseSlot (at); };
    cabinet.handLight = handLight;
    addToStage (cabinet);
    cabinet.setBounds (layout::cabinetControl.toNearestInt());
    cabinet.setComponentID ("cabinet");
    cabinet.label = &readout (layout::cabinetName);
    {
        const float after = layout::cabinetTitle.x + juce::GlyphArrangement::getStringWidth (
                                labelFont (stage::px13).withExtraKerningFactor (0.08f), "CABINET ");
        auto& n = readout ({ after, layout::titleY - 16.0f, layout::cabinetScreen.getRight() - after, 32.0f });
        n.colour = titleInk;
        n.fromLeft = true;
        n.setComponentID ("cabinetNumber");
        cabinet.number = &n;
    }
    cabinet.refresh();

    // LEARN — from GATE's menu, or a double click on its lamp — takes the tuner's glass while it runs.
    addToStage (learnGlass);
    learnGlass.setBounds (layout::tunerScreen.toNearestInt());
    learnGlass.inset = { 10.0f, 4.0f };
    learnGlass.onShow = [this] (bool up) { tuner.setAlpha (up ? 0.0f : 1.0f); };

    // THE SEA, under the whole stage: the panel's alpha is its window; the world says where its glass is (glassAt).
    stage.addAndMakeVisible (*view, 0);
    view->setBounds (layout::view.toNearestInt());
    view->addMouseListener (&glassClicks, false);
    glassClicks.menu = [this] { showWindowMenu(); };
    // ...the panel the world's while it is drawing it — and not while the view is hidden for a picture taken of the face
    // (Console::writePoster): the face's own panel goes into that.
    cabin().worldDrawsPanel = [this] { return view->isVisible() && view->drawsCabin(); };

    park.sendInitialUpdate();
    showChannel();

    view->setLogName (amp.loadedAs());
    view->setHost (&gl);
    view->setHearing ([&a = amp] { return a.takePlay(); });
    view->setLit ([&a = amp] { return a.viewLit(); });
    view->setTempo ([&a = amp] { return (double) a.hostTempoBpm(); });
    gl.setPreferredVersion ({ 3, 2 });
    gl.setPreferredProfile (juce::OpenGLProfile::core);
    gl.setRenderer (this);
    gl.setComponentPaintingEnabled (true);
    gl.setContinuousRepainting (false);     // the view asks for each frame, at its own pace
    gl.attachTo (*this);
    view->setFrameRateCap ((int) prefs::store (amp.product()).get (frameRateKey, 60));
    view->setMode ((redpaw::Windscreen::Mode) juce::jmax (0, landscapeWords.indexOf (prefs::store (amp.product()).get (landscapeKey, landscapeWords[0]).toString())));
    placeView();
}

UnderwaterEditor::~UnderwaterEditor()
{
    view->removeMouseListener (&glassClicks);
    gl.detach();
}

std::unique_ptr<redpaw::Drive> UnderwaterEditor::makeDrive()
{
    return std::make_unique<UnderwaterDrive> (shippedArt());
}

Readout& UnderwaterEditor::readout (juce::Rectangle<float> box)
{
    auto& r = *readouts.emplace_back (std::make_unique<Readout> (cabinInk(), stage::px13));
    addToStage (r);
    r.setBounds (box.toNearestInt());
    return r;
}

template <typename Control>
void UnderwaterEditor::bindTravel (Control& control, const juce::String& paramId, std::function<float (float)> toTravel,
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
    control.defaultValue   = toTravel (p->convertFrom0to1 (p->getDefaultValue()));
    a->sendInitialUpdate();
}

void UnderwaterEditor::bindSwitch (redgroove::SlideSwitch& sw, const char* paramId)
{
    auto* a = follow (paramId, [&sw] (float v) { sw.setPosition (juce::roundToInt (v), false); });
    sw.onFlip = [a] (int to) { a->setValueAsCompleteGesture ((float) to); };
    a->sendInitialUpdate();
}

void UnderwaterEditor::hearSlot (int at)
{
    dragAt = at;
    channels.hearSlot (at);
}

void UnderwaterEditor::chooseSlot (int at)
{
    if (dragAt >= 0)
    {
        channels.chooseSlot (at);
        channels.setCabinetOn (true);
    }
    dragAt = -1;
}

/** The lever goes where the state is — PARK while the amp is out, else the channel — unless the hand holds it; the
    sea in the channel's gear; BLEND the channel's, where it has one. */
void UnderwaterEditor::showSelector (int channel)
{
    const int place = parked ? 0 : channel + 1;
    if (! lever.isHeld() && lever.getPlace() != place)
        lever.setPlace (place, true, false);
    lever.repaint();
    cabinet.refresh();
    view->setGear (channel + 1);        // CLEAN the first gear, DIRT the second, DRIVE the third

    // CLEAN HAS NO BLEND (the owner, 16.11): the capture alone, the fader dark and out of the hand's reach.
    const bool blends = channel != 0;
    blend.setEnabled (blends);
    blend.setAlpha (blends ? 1.0f : 0.35f);
    if (blendValue != nullptr)
        blendValue->setAlpha (blends ? 1.0f : 0.35f);
}

/** PARK: the amp's bypass, from the lever, a host's bypass or anywhere — the lever to it, the sea parked. */
void UnderwaterEditor::showPark (bool on)
{
    parked = on;
    view->setPark (on);
    const int place = on ? 0 : channels.active() + 1;
    if (! lever.isHeld() && lever.getPlace() != place)
        lever.setPlace (place, true, false);
    lever.repaint();
}

/** The number keys pull the lever: 1 is CLEAN, 2 DIRT, 3 DRIVE. */
bool UnderwaterEditor::channelKey (const juce::KeyPress& key)
{
    const auto c = (juce::juce_wchar) key.getKeyCode();
    const int at = c >= '1' && c < '1' + channels.count() ? (int) (c - '1') : -1;
    if (at < 0)
        return false;
    if (! lever.isHeld())
        lever.setPlace (at + 1, true);
    return true;
}

void UnderwaterEditor::stripsLaid (const face::Strips& laid)
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

/** THE BATHYSCAPHE'S OWN PART OF THE SITE'S DEMO (Console::webExtras), in the walker's words and no others: under
    places.json's "world", the box the sea is drawn in — the whole stage — and every control the console has not already
    said: the lever, its places the three gears (the site has no PARK: the app's top stop is not written), the rings, the
    four columns — BLEND, OCTAVE, CHORUS, REVERB — the cuts' two switches, the cabinet's screen and the readings, each
    with the parameter it moves and how. The cabin is the console's own, the dashboard in it: on the site the world draws
    behind the glass alone. THE STILL, the one the window shows where there is no live sea (no WebGL 2, before the first
    frame, a view that fails): the sea's first frame, the world alone, the whole stage at twice its size — the first a
    poster's frame brings, the same sea in every gear at the start. */
void UnderwaterEditor::webExtras (const juce::File& dir, juce::DynamicObject& kit, juce::DynamicObject& world)
{
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

    world.setProperty ("dome", rect (layout::view));
    world.setProperty ("gears", channels.count());
    world.setProperty ("channelGears", list ({ 1, 2, 3 }));     // CLEAN the first, DIRT the second, DRIVE the third; no TURBO

    // THE LEVER: its places top to bottom — the gears, PARK left out — the slot it runs in, each name where it stands.
    {
        auto* l = new Obj();
        l->setProperty ("box", rect (boxOf (lever)));
        l->setProperty ("slotX", layout::leverX);
        juce::Array<juce::var> stops, names;
        const auto centres = leverNameCentres();
        for (size_t i = 1; i < layout::leverStops.size(); ++i)
        {
            stops.add (layout::leverStops[i]);
            auto* n = new Obj();
            n->setProperty ("name", leverWords[(int) i]); n->setProperty ("at", point (centres[i]));
            names.add (juce::var (n));
        }
        l->setProperty ("stops", stops);
        l->setProperty ("names", names);
        l->setProperty ("capWidth", redgroove::ChannelLever::capWidth);
        l->setProperty ("capHeight", redgroove::ChannelLever::capHeight);
        l->setProperty ("does", "the channel: place i is channel i (the lineup's), its gear i + 1");
        world.setProperty ("lever", l);
    }

    // THE RINGS: DRIVE's and the tones' (the console's knobs) — the cabin has no knob of its own.
    {
        world.setProperty ("knobs", juce::Array<juce::var>());
        auto* r = new Obj();
        r->setProperty ("inset", layout::ringInset); r->setProperty ("width", layout::ringWidth); r->setProperty ("driveWidth", layout::driveRingWidth);
        r->setProperty ("sockets", list ({ juce::var ("drive"), juce::var ("tones") }));
        world.setProperty ("rings", r);
        world.setProperty ("knobNameY", layout::nameY);
    }

    // THE COLUMNS: BLEND, then the three effects' amounts, upright, the fader cap's render.
    {
        struct F { const char* id; const char* name; const juce::Component* c; const char* param; const char* travel; };
        const F faders[] {
            { "blend",  "BLEND",  &blend,  params::ampBlend,     "the value, 0..1, bottom to top" },
            { "octave", "OCTAVE", &octave, params::octaveAmount, "the value, 0..1, bottom to top" },
            { "chorus", "CHORUS", &chorus, params::chorusAmount, "the value, 0..1, bottom to top" },
            { "reverb", "REVERB", &reverb, params::reverbMix,    "the value / 100, bottom to top" } };
        juce::Array<juce::var> a;
        for (const auto& f : faders)
        {
            auto* v = new Obj();
            v->setProperty ("id", f.id); v->setProperty ("name", f.name); v->setProperty ("box", rect (boxOf (*f.c)));
            v->setProperty ("sprite", "fader-cap"); v->setProperty ("capLength", layout::faderCapLength);
            v->setProperty ("param", f.param); v->setProperty ("travel", f.travel); v->setProperty ("reading", "percent");
            a.add (juce::var (v));
        }
        world.setProperty ("columns", a);
    }

    // THE CUTS' SWITCHES: two positions each, a word over each, the one it stands at lit; the parameter's value is the position.
    {
        struct S { const char* id; const redgroove::SlideSwitch* sw; const char* left; const char* right; const char* param; };
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
        world.setProperty ("switches", a);
    }

    // THE CABINET'S SCREEN.
    {
        auto* c = new Obj();
        c->setProperty ("box", rect (boxOf (cabinet))); c->setProperty ("shows", "the playing cabinet's name, a list on a click");
        world.setProperty ("cabinetScreen", c);
    }

    // THE READINGS, each under its control, in the order the face makes them: the box and how it writes the value.
    {
        struct R { const char* id; const char* format; };
        const R names[] { { "drive", "the dial, 0.0..10.0" }, { "blend", "percent: 30%" }, { "in", "dB, a tenth: +1.5 dB, 0.0 dB" },
                          { "out", "dB, a tenth" }, { "octave", "percent" }, { "chorus", "percent" }, { "reverb", "percent" },
                          { "tone0", "dB, a tenth" }, { "tone1", "dB, a tenth" }, { "tone2", "dB, a tenth" } };
        juce::Array<juce::var> a;
        for (size_t i = 0; i < readouts.size() && i < std::size (names); ++i)
        {
            auto* v = new Obj();
            v->setProperty ("id", names[i].id); v->setProperty ("box", rect (boxOf (*readouts[i]))); v->setProperty ("format", names[i].format);
            a.add (juce::var (v));
        }
        world.setProperty ("readings", a);
        world.setProperty ("readingPx", stage::px13);
    }

    kit.setProperty ("window", rect (layout::view));
    kit.setProperty ("stills", list ({ juce::var ("still.webp") }));

    webPoses.count = channels.count();
    webPoses.opening = channels.active();
    webPoses.standAt = [this] (int at) { go (at); };
    webStill = {};
    webPoses.firstFrame = [this, dir] (int, std::function<void (juce::Image)> done)
    {
        const auto box = layout::view * 2.0f;
        view->firstFrame (makeDrive(), juce::roundToInt (box.getWidth()), juce::roundToInt (box.getHeight()),
                          [this, dir, done = std::move (done)] (juce::Image frame)
                          {
                              if (frame.isValid() && ! webStill.isValid())
                              {
                                  webStill = frame;
                                  writeForWeb (dir.getChildFile ("still.webp"), webStill);
                              }
                              done (frame);
                          });
    };
    webPoses.live = "the sea's first frame for the channel, behind the glass alone: the bathyscaphe on the GPU from its world's "
                    "start, in the channel's gear, settled, nothing stepped (t = 0); the top poster is the channel the app opens at";
    webPoses.still = [this] (int) { return webStill; };
    webPoses.window = layout::view;
    webPoses.hidden = { view.get() };
}

//==============================================================================
/** What the window shows — the sea on the GPU, the 1993 landscape on the CPU, or the still — and how often; whether the
    sea answers the bass; and full screen. The machine's, kept with its settings. */
void UnderwaterEditor::addWindowSections (juce::PopupMenu& menu)
{
    const auto self = [] (Console& e) -> UnderwaterEditor& { return static_cast<UnderwaterEditor&> (e); };

    if (! amp.runsInHost())
    {
        menu.addSectionHeader ("WINDOW");
        addChoice (menu, "Full screen", fullScreen, [self] (Console& e) { self (e).setFullScreen (! self (e).fullScreen); });
    }

    menu.addSectionHeader ("LANDSCAPE");
    const auto running = view->mode();
    for (int i = 0; i < landscapeNames.size(); ++i)
        addChoice (menu, landscapeNames[i], (int) running == i,
                   [self, i] (Console& e) { self (e).chooseLandscape ((redpaw::Windscreen::Mode) i); },
                   i != (int) redpaw::Windscreen::Mode::gpu || ! view->status().gpuBroken);
    for (const int fps : { 60, 30 })
        addChoice (menu, juce::String (fps) + " frames a second", view->frameRateCap() == fps,
                   [self, fps] (Console& e) { self (e).chooseFrameRate (fps); }, running != redpaw::Windscreen::Mode::still);
    const bool lit = prefs::viewLitByGuitar (amp.product());
    addChoice (menu, "Lit by the bass", lit, [self, lit] (Console& e) { prefs::setViewLitByGuitar (self (e).amp.product(), ! lit); });
}

void UnderwaterEditor::showWindowMenu()
{
    juce::PopupMenu menu;
    addWindowSections (menu);
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

void UnderwaterEditor::chooseLandscape (redpaw::Windscreen::Mode mode)
{
    prefs::store (amp.product()).set (landscapeKey, landscapeWords[(int) mode]);
    view->setMode (mode);
}

void UnderwaterEditor::chooseFrameRate (int fps)
{
    prefs::store (amp.product()).set (frameRateKey, fps);
    view->setFrameRateCap (fps);
}

/** Where the stage is in the editor's own pixels, for the GL thread. */
void UnderwaterEditor::placeView()
{
    const auto at = (layout::view * stageScale() + stageOrigin()).toNearestInt();
    glassX.store (at.getX());
    glassY.store (at.getY());
    glassW.store (at.getWidth());
    glassH.store (at.getHeight());
    gl.triggerRepaint();
}

void UnderwaterEditor::newOpenGLContextCreated()
{
    view->hostContextCreated();
}

void UnderwaterEditor::renderOpenGL()
{
    const float k = (float) gl.getRenderingScale();
    const int fw = juce::roundToInt ((float) getWidth() * k), fh = juce::roundToInt ((float) getHeight() * k);
    juce::OpenGLHelpers::clear (juce::Colours::black);

    const auto glass = juce::Rectangle<float> ((float) glassX.load(), (float) glassY.load(), (float) glassW.load(), (float) glassH.load()) * k;
    view->hostRender (glass.toNearestInt(), fw, fh);
}

void UnderwaterEditor::openGLContextClosing()
{
    view->hostContextClosing();
}

} // namespace orbitamp::underwater
