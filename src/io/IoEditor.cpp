// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "IoEditor.h"

#include "IoFaceArt.h"
#include "IoLayout.h"
#include "IoLineup.h"

#include "view/IoArt.h"
#include "../ui/BassControls.h"
#include "../ui/CabWindow.h"
#include "../ui/FactoryMissing.h"
#include "../ui/Prefs.h"

#include <BinaryData.h>

namespace orbitamp::io
{

juce::AudioProcessorEditor* makeEditor (AmpProcessor& p)
{
    // No knobs to draw without their renders, which lie in the factory folder: where they are missing, the window
    // says so, and where they belong (ui/FactoryMissing.h).
    if (! FaceArt::present())
        return new ui::FactoryMissingEditor (p, p.product());
    return new IoEditor (p);
}

using namespace face;
using glyphs::Kind;
using glyphs::Anchor;

namespace
{
    /** What the window shows, and how often — the machine's, kept with its settings: the landscape as a word, in the
        order of Windscreen::Mode, and what the menu calls each. */
    const juce::Identifier landscapeKey { "landscape" };
    const juce::Identifier frameRateKey { "landscape_fps" };
    const juce::StringArray landscapeWords { "world", "1993", "still" };
    const juce::StringArray landscapeNames { "The view", "1993", "Still" };

    // Where the pointer is drawn on the knobs' renders — the walker's, whose renders these are.
    constexpr ArtKnob::Pointer drivePointer { 0.30f, 0.66f, 0.06f };
    constexpr ArtKnob::Pointer tonePointer  { 0.30f, 0.64f, 0.07f };

    /** THE HAND'S LIGHT: a soft orange round whatever the hand is over or on — a line, never a wash. */
    const juce::Colour handLight = orange.withAlpha (0.8f);

    /** THE RINGS round the knobs, in their sockets' grooves: Io's paint, quieter. */
    const juce::Colour ringLight = glyphs::dayInk.withAlpha (0.55f);

    /** WHAT THE APP'S OWN WORDS ARE WRITTEN IN — the tuner's, the cabinet's name over its screen, PRESET, the version, MEM
        and CPU: Io's paint, a dark edge under them; a state the family's orange. */
    Ink cabinInk()
    {
        Ink ink;
        ink.words  = glyphs::dayInk;
        ink.values = glyphs::dayInk;
        ink.edge   = juce::Colour (0x99000000);
        return ink;
    }

    juce::Rectangle<float> around (juce::Point<float> c, float w, float h)
    {
        return juce::Rectangle<float> (w, h).withCentre (c);
    }

    /** THE PICTOGRAMS OVER THE SOCKETS, where the approved mockup has them (io.html): each with its full name. */
    struct Sign
    {
        Kind kind;
        layout::GlyphAt at;
        const char* name;
    };
    const std::vector<Sign>& signs()
    {
        using namespace layout;
        static const std::vector<Sign> all {
            { Kind::drive,    driveGlyph,                         "DRIVE" },
            { Kind::presence, presenceGlyph,                      "PRESENCE" },
            { Kind::blend,    blendGlyph,                         "BLEND" },
            { Kind::bass,     toneGlyphs[0],                      "BASS" },
            { Kind::middle,   toneGlyphs[1],                      "MIDDLE" },
            { Kind::treble,   toneGlyphs[2],                      "TREBLE" },
            { Kind::octave,   effectGlyphs[0],                    "OCTAVE" },
            { Kind::chorus,   effectGlyphs[1],                    "CHORUS" },
            { Kind::reverb,   effectGlyphs[2],                    "REVERB" },
            { Kind::out,      outGlyph,                           "OUTPUT" },
            { Kind::comp,     compGlyph,                          "COMPRESSOR" },
            { Kind::input,    inGlyph,                            "INPUT" },
            { Kind::slow,     { leverSignX, leverStops[1], 1.0f }, "SLOW" },
            { Kind::fast,     { leverSignX, leverStops[2], 1.0f }, "FAST" },
        };
        return all;
    }

    juce::Point<float> centreOf (const layout::GlyphAt& at) { return { at.x, at.y }; }

    /** WHERE THE CONSOLE'S CONTROLS STAND on the dashboard (IoLayout.h). */
    Console::Places cabinPlaces()
    {
        using namespace layout;
        using namespace stage;

        Console::Places p;
        p.drive = around (drive, driveKnob, driveKnob);

        // No row of tone knobs (the face has its own, by name) and no row of a pack's switches.
        p.toneStrip = {};
        p.keyStrip = {};
        p.filterKeys = false;

        // The six keys of the history and the registers, each its whole hole: its cap drawn inside it (glyphs::drawCap).
        for (size_t i = 0; i < p.holes.size(); ++i)
        {
            p.holes[i] = { keyX[i], keyRowY };
            p.holeKeys[i] = around (p.holes[i], i < (size_t) ovFirstRegister ? historyHole : registerHole, keyHole);
        }
        p.holeKeySize = keyHole;

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

        // The lamps, drawn by code: each overload on its panel's own lamp, top right of IN and top left of OUT; GATE's and
        // LIMITER's at the left of their caps.
        p.lampDot = 8.0f;
        p.lamps[inClipLamp]  = around (inLampAt, 14.0f, 14.0f);
        p.lamps[outClipLamp] = around (outLampAt, 14.0f, 14.0f);
        p.lamps[gateLamp]    = around ({ gateSlot.getX() + 13.0f, gateSlot.getCentreY() }, 14.0f, 14.0f);
        p.lamps[limiterLamp] = around ({ limiterSlot.getX() + 13.0f, limiterSlot.getCentreY() }, 14.0f, 14.0f);

        // GATE's and LIMITER's doors are none of the console's: the caps and their words are the cabin's (B612), a click on
        // either the menu the door opens (IoEditor's spots).
        p.gateDoor = {};
        p.limiterDoor = {};
        p.limiterWord = "";

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
                 glyphs::dayInk };
    }
}

//==============================================================================
juce::Rectangle<int> Cabin::historyArea() const
{
    using namespace layout;
    return juce::Rectangle<float> (keyX.front() - 50.0f, keyRowY - 26.0f, keyX.back() - keyX.front() + 100.0f, 52.0f).toNearestInt();
}

void Cabin::paintStatic (juce::Graphics& g, float scale) const
{
    using namespace layout;
    using namespace stage;

    // The day's panel, unless the world is drawing the cabin — and always in a picture taken.
    const bool worldHasIt = onScreen && worldDrawsPanel != nullptr && worldDrawsPanel();
    if (const auto& bg = FaceArt::of().bg; bg.isValid() && ! worldHasIt)
        g.drawImage (bg, juce::Rectangle<float> (0.0f, 0.0f, width, height));

    // The family's cat, cut into the maker's plate; GATE's and LIMITER's caps.
    engraveMark (g, emblemMark, scale);
    glyphs::drawCap (g, gateSlot);
    glyphs::drawCap (g, limiterSlot);

    // IO'S LABELS, in its paint, lit as dark as the cabin is: every pictogram over its socket, the lever's P and its
    // triangles, GATE and LIMITER on their caps, the switches' words either side of them, and DARWIN'S CAT and POWERED BY
    // on the plates either side of the maker's.
    glyphs::paintLabels (g, labelsArea, dark, [] (juce::Graphics& l, juce::Colour c, bool shadowPass)
    {
        if (! shadowPass)       // the shadow falls under the words alone (the mockup's)
            for (const auto& s : signs())
                glyphs::draw (l, s.kind, centreOf (s.at), s.at.scale, c);
        glyphs::word (l, "P", leverSignX, parkBaseline, parkEm, c, Anchor::middle, true);
        glyphs::word (l, "GATE", gateSlot.getCentreX(), buttonBaseline, wordEm, c);
        glyphs::word (l, "LIMITER", limiterSlot.getCentreX(), buttonBaseline, wordEm, c);
        glyphs::word (l, "5STR", switchLeftEnd, stringsBaseline, wordEm, c, Anchor::end);
        glyphs::word (l, "4STR", switchRightStart, stringsBaseline, wordEm, c, Anchor::start);
        glyphs::word (l, "FING", switchLeftEnd, fingersBaseline, wordEm, c, Anchor::end);
        glyphs::word (l, "SLAP", switchRightStart, fingersBaseline, wordEm, c, Anchor::start);

        // The plates' words in the family's letters, as every cabin writes them.
        const Ink plain;
        const auto onPlate = [&] (const juce::String& words, juce::Rectangle<float> plate)
        {
            plain.label (l, words, plate.withSizeKeepingCentre (plate.getWidth() + 120.0f, plate.getHeight()), px13, c.withMultipliedAlpha (0.85f));
        };
        onPlate ("POWERED BY", poweredByPlate);
        onPlate ("DARWIN'S CAT", makerNamePlate);
    });
}

//==============================================================================
IoEditor::IoEditor (AmpProcessor& p)
    : face::Console (p, std::make_unique<Cabin>(), cabinPlaces(), cabinLook()),
      view (std::make_unique<redpaw::Windscreen> (makeDrive())),
      blend (FaceArt::of().faderCap), comp (FaceArt::of().faderCap),
      octave (FaceArt::of().faderCap), chorus (FaceArt::of().faderCap), reverb (FaceArt::of().faderCap),
      squeeze ([&p] { return p.compGrDb.load (std::memory_order_relaxed); }),
      // ...its places named, and no name written by them: the signs beside the stops are the cabin's (no centres given)
      lever (cabinInk(), { "PARK", "SLOW", "FAST" }, layout::leverStops, layout::leverX, {}, layout::leverBox.getPosition()),
      lowCut (cabinInk(), {}, {}, {}),      // ...the words either side the cabin's, in B612 (Cabin::paintStatic)
      highCut (cabinInk(), {}, {}, {}),
      cabinet (cabinInk(), FaceArt::of().faderCap, layout::faderCapLength, layout::faderCapSquash),
      park (p.bypassParameter(), [this] (float v) { showPark (v > 0.5f); }, nullptr)
{
    // Everything this constructor sets is the face settling onto the state, not a player's edit.
    const felitronics::appkit::CompareHistory::ScopedSuppress settling (amp.history);

    // THE SIX KEYS: dark caps (the mockup's), UNDO's and REDO's arrows and the letters A to D in B612 Bold on them, lit as
    // the labels are; the register that plays its edge and its letter the family's orange.
    for (int i = 0; i < 6; ++i)
    {
        roofKey (i).drawKey = [this, &k = roofKey (i), i] (juce::Graphics& g, juce::Rectangle<float> box, bool down, float)
        {
            const bool history = i < ovFirstRegister;
            const bool lit = ! history && k.isOn();
            const bool live = ! history || (i == ovUndo ? amp.history.canUndo() : amp.history.canRedo());
            const bool held = down && live;

            glyphs::drawCap (g, box, held, lit ? orange : juce::Colour());
            const float drop = held ? 1.0f : 0.0f;
            const float alpha = live ? 1.0f : 0.35f;
            glyphs::paintLabels (g, box, dark, [&] (juce::Graphics& l, juce::Colour c, bool shadowPass)
            {
                if (history)
                {
                    if (! shadowPass)
                        glyphs::draw (l, i == ovUndo ? Kind::undo : Kind::redo,
                                      { box.getCentreX(), box.getCentreY() + layout::arrowY - layout::keyRowY + drop }, 1.0f,
                                      c.withMultipliedAlpha (alpha));
                    return;
                }
                glyphs::word (l, k.name, box.getCentreX(), box.getCentreY() + layout::letterBaseline - layout::keyRowY + drop,
                              layout::letterEm, lit && ! shadowPass ? orange : c.withMultipliedAlpha (alpha), Anchor::middle, true);
            });

            if (k.isMouseOverOrDragging() && live)
            {
                juce::Path rim;
                rim.addRoundedRectangle (box.reduced (2.0f), 5.0f);
                drawHandRim (g, rim, handLight);
            }
        };
        nameOnHover (roofKey (i), i == ovUndo ? juce::String ("UNDO") : i == ovRedo ? juce::String ("REDO")
                                                                     : "REGISTER " + juce::String::charToString ((juce::juce_wchar) ('A' + i - ovFirstRegister)));
    }

    // THE RINGS, one round every knob in its socket's groove.
    const auto ringFor = [this] (const face::ArtKnob& knob, juce::Point<float> centre, float socket, float width)
    {
        rings.push_back (std::make_unique<face::KnobRing> (knob, ringLight, centre, socket * 0.5f - layout::ringInset, width));
        addToStage (*rings.back());
    };
    ringFor (driveKnob(), layout::drive, layout::driveSocket, layout::driveRingWidth);
    nameOnHover (driveKnob(), "DRIVE");

    // THE TONES, PRESENCE, BASS, MIDDLE AND TREBLE, a knob in each socket (bindTones says which tone each turns).
    const char* toneNames[] { "PRESENCE", "BASS", "MIDDLE", "TREBLE" };
    const juce::Point<float> toneAt[] { layout::presence, { layout::toneX[0], layout::toneY }, { layout::toneX[1], layout::toneY },
                                        { layout::toneX[2], layout::toneY } };
    for (size_t s = 0; s < tones.size(); ++s)
    {
        tones[s] = std::make_unique<face::ArtKnob> (FaceArt::of().knobTone, tonePointer);
        auto& knob = *tones[s];
        addToStage (knob);
        knob.setBounds (around (toneAt[s], layout::toneKnob, layout::toneKnob).toNearestInt());
        knob.setComponentID (juce::String ("tone-") + juce::String (toneNames[s]).toLowerCase());
        knob.onHover = [this, s, name = juce::String (toneNames[s])] (bool over)
        {
            // PRESENCE on the rover's own parameter says, in one line, what it does and what it does not (the owner, 20.11).
            sayName (tones[s]->getBounds().toFloat(),
                     s == presenceSocket && presenceSilent ? juce::String ("PRESENCE:   the snow, no sound in this pack yet") : name, over);
        };
        ringFor (knob, toneAt[s], layout::toneSocket, layout::ringWidth);
    }

    // THE FADERS: BLEND, COMP and its squeeze, and the three effects upright in their slots. Each says where it stands in
    // the tag over it while it is held.
    const auto faderIn = [this] (face::ColumnFader& fader, juce::Rectangle<float> box, const char* id)
    {
        fader.capLength = layout::faderCapLength;
        fader.capSquash = layout::faderCapSquash;
        fader.handLight = handLight;
        fader.marks = 6;
        fader.longMark = -1;
        addToStage (fader);
        fader.setBounds (box.toNearestInt());
        fader.setComponentID (id);
    };
    faderIn (blend, layout::blendSlot.withSizeKeepingCentre (layout::faderWidth, layout::blendSlot.getHeight()), "blend");
    blend.marks = 4;
    faderIn (comp, layout::compSlot.withSizeKeepingCentre (layout::faderWidth, layout::compSlot.getHeight()), "comp");
    face::ColumnFader* effectFaders[] { &octave, &chorus, &reverb };
    const char* effectIds[] { params::octaveAmount, params::chorusAmount, params::reverbMix };
    const char* effectNames[] { "OCTAVE", "CHORUS", "REVERB" };
    for (size_t i = 0; i < 3; ++i)
        faderIn (*effectFaders[i], around ({ layout::effectX[i], layout::effectY }, layout::effectFaderWidth, layout::effectSlotHeight),
                 juce::String (effectNames[i]).toLowerCase().toRawUTF8());
    addToStage (squeeze);
    squeeze.setBounds (layout::compGrSlot.reduced (2.0f, 4.0f).toNearestInt());   // inside the render's slot, as the meters are
    squeeze.setComponentID ("squeeze");

    const auto percent = [] (float share) { return juce::String (juce::roundToInt (share * 100.0f)) + "%"; };
    bindTravel (comp, params::compThreshold, [] (float db) { return db / params::compThresholdMin; },
                [] (float t) { return t * params::compThresholdMin; });
    sayLeverAs (comp, "COMPRESSOR", [this] { return juce::String (juce::roundToInt (getParam (params::compThreshold))) + " dB"; });
    bindTravel (blend, params::ampBlend, [] (float v) { return v; }, [] (float t) { return t; });
    sayLeverAs (blend, "BLEND", [this, percent] { return percent (getParam (params::ampBlend)); });
    // ...and a right click on BLEND, its crossover alone, 80 to 250 Hz, as DeepGrooveBass's.
    blend.onMenu = [this]
    {
        bass::Menu m;
        m.items.addSectionHeader ("BLEND CROSSOVER");
        bass::addSteps (m, m.items, amp.apvts, params::ampBlendHz,
                        { 80.0f, 100.0f, 120.0f, 150.0f, 180.0f, 200.0f, 250.0f }, bass::hzWord);
        bass::show (std::move (m), &blend);
    };
    for (size_t i = 0; i < 3; ++i)
    {
        const float full = i == 2 ? 100.0f : 1.0f;
        bindTravel (*effectFaders[i], effectIds[i], [full] (float v) { return v / full; }, [full] (float t) { return t * full; });
        sayLeverAs (*effectFaders[i], effectNames[i], [this, id = effectIds[i], full, percent] { return percent (getParam (id) / full); });
        nameOnHover (*effectFaders[i], effectNames[i]);
    }
    nameOnHover (blend, "BLEND");
    nameOnHover (comp, "COMPRESSOR");
    nameOnHover (squeeze, "GAIN REDUCTION");

    // THE LEVER: PARK over the two speeds. PARK takes the whole amp out (the amp's bypass) and leaves the speed as it was;
    // a speed brings the amp back in — the same sound either way.
    addToStage (lever);
    lever.setBounds (layout::leverBox.toNearestInt());
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
    nameOnHover (lever, "SPEED");

    // THE CUTS' SWITCHES under the cabinet's screen, each in the picture's own slot: the HPF by the strings, the LPF by
    // the hand — both always in (channels::Lineup::pinned), the switch choosing between their positions, 5STR and FING on
    // the left; a right click on either its positions' frequencies.
    const auto lineAt = [this] (redgroove::SlideSwitch& sw, juce::Rectangle<float> slotOnStage)
    {
        sw.nameRoom = 0.0f;
        sw.wordRoom = 0.0f;
        const auto box = slotOnStage.expanded (6.0f, 3.0f);
        sw.slot = slotOnStage - box.getPosition();
        const auto& bg = FaceArt::of().bg;
        sw.slotPicture = bg.getClippedImage ((slotOnStage * ((float) bg.getWidth() / stage::width)).toNearestInt());
        sw.handLight = handLight;
        addToStage (sw);
        sw.setBounds (box.toNearestInt());
    };
    lineAt (lowCut, layout::stringsSlot);
    lineAt (highCut, layout::fingersSlot);
    lowCut.onHover  = [this] (bool over) { sayName (lowCut.getBoundsInParent().toFloat(), bass::strHint, over); };
    highCut.onHover = [this] (bool over) { sayName (highCut.getBoundsInParent().toFloat(), bass::stylHint, over); };
    bindSwitch (lowCut, params::bassHpf);
    bindSwitch (highCut, params::bassLpf);
    lowCut.onMenu  = [this] { bass::Menu m; bass::addHpf (m, m.items, amp.apvts); bass::show (std::move (m), &lowCut); };
    highCut.onMenu = [this] { bass::Menu m; bass::addLpf (m, m.items, amp.apvts); bass::show (std::move (m), &highCut); };
    lowCut.setComponentID ("lowCut");
    highCut.setComponentID ("highCut");

    // THE CABINET'S SLIDER in its screen: a notch a cabinet of the bass's dozen, set and fixed, as the bathyscaphe's; over
    // it the cabinet that plays by its name — the slot's under the hand while it moves the slider, OFF while the cabinet
    // is out of the sound.
    cabinet.name = [this]
    {
        const auto named = [this] (int slot)
        {
            const auto name = channels.slotName (slot);
            return name.isNotEmpty() ? name : CabWindow::labelOf (AmpProcessor::cabSynthOf (channels.slotFrom (slot)));
        };
        const auto plays = [this, &named]
        {
            if (dragAt >= 0)
                return named (dragAt);
            if (! channels.cabinetOn())
                return juce::String ("OFF");
            const auto playing = AmpProcessor::cabSynthOf (amp.cabChoice());
            if (const auto slot = AmpProcessor::cabSynthOf (channels.slotFrom (channels.cabinetSlot())); playing.valid && slot.valid && playing.state == slot.state)
                return named (channels.cabinetSlot());
            return CabWindow::labelOf (playing);
        };
        // ...its name alone, the dozen's number left out: "8x10 STACK".
        auto words = plays();
        if (const auto head = words.upToFirstOccurrenceOf (": ", false, false); words.contains (": ") && head.isNotEmpty() && head.containsOnly ("0123456789"))
            words = words.fromFirstOccurrenceOf (": ", false, false);
        return words;
    };
    cabinet.notches = channels.cabinetSlots();
    cabinet.notch   = [this] { return channels.cabinetSlot(); };
    cabinet.onMove  = [this] (int at) { hearSlot (at); };
    cabinet.onLetGo = [this] (int at) { chooseSlot (at); };
    cabinet.handLight = handLight;
    addToStage (cabinet);
    cabinet.setBounds (layout::cabinetControl.toNearestInt());
    cabinet.setComponentID ("cabinet");
    nameOnHover (cabinet, "CABINET");
    cabinetLabel = std::make_unique<redgroove::Readout> (cabinInk(), stage::px13);     // 13 px: the family's floor for what is read
    cabinetLabel->colour = glyphs::dayInk.withAlpha (0.85f);
    addToStage (*cabinetLabel);
    cabinetLabel->setBounds (layout::cabinetName.toNearestInt());
    cabinet.label = cabinetLabel.get();
    cabinet.refresh();

    // LEARN — from GATE's menu, or a double click on its lamp — takes the tuner's glass while it runs.
    addToStage (learnGlass);
    learnGlass.setBounds (layout::tunerScreen.toNearestInt());
    learnGlass.inset = { 10.0f, 4.0f };
    learnGlass.onShow = [this] (bool up) { tuner.setAlpha (up ? 0.0f : 1.0f); };
    nameOnHover (tuner, "TUNER");

    // EVERY PICTOGRAM'S NAME, a spot over each; the lever's stops pull the lever there, GATE's and LIMITER's caps open the
    // menus their doors open.
    for (const auto& s : signs())
    {
        std::function<void()> click;
        if (s.kind == Kind::slow || s.kind == Kind::fast)
            click = [this, at = s.kind == Kind::slow ? 1 : 2] { lever.setPlace (at, true, true); };
        spot (glyphs::boxOf (s.kind, centreOf (s.at), s.at.scale).expanded (3.0f), s.name, std::move (click))
            .setComponentID (juce::String ("sign-") + juce::String (s.name).toLowerCase());
    }
    spot (around ({ layout::leverSignX, layout::parkBaseline - 7.5f }, 24.0f, 24.0f), "PARK",
          [this] { lever.setPlace (0, true, true); }).setComponentID ("sign-park");
    spot (layout::gateSlot.reduced (3.0f).withTrimmedLeft (24.0f), "GATE", [this] { showGateMenu(); }).setComponentID ("sign-gate");
    spot (layout::limiterSlot.reduced (3.0f).withTrimmedLeft (24.0f), "LIMITER", [this] { showOutputMenu(); }).setComponentID ("sign-limiter");

    // THE VIEW, under the whole stage: the panel's alpha is its windscreen.
    stage.addAndMakeVisible (*view, 0);
    view->setBounds (layout::view.toNearestInt());
    view->addMouseListener (&glassClicks, false);
    glassClicks.menu = [this] { showWindowMenu(); };
    cabin().worldDrawsPanel = [this] { return view->isVisible() && view->drawsCabin(); };

    // THE AMP'S KNOBS DRIVE THE WORLD: DRIVE and BLEND told it whenever they move, and once now (PRESENCE with the tones,
    // bindTones).
    ioWorld = dynamic_cast<IoDrive*> (&view->drive());
    for (const char* id : { params::preampGain, params::ampBlend })
        follow (id, [this] (float) { if (ioWorld != nullptr) tellWorld (*ioWorld); })->sendInitialUpdate();

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

IoEditor::~IoEditor()
{
    view->removeMouseListener (&glassClicks);
    gl.detach();
}

/** THE WORLD BEHIND THE GLASS: Io (view/IoDrive.h), its art read from the factory folder's Faces/IoBass (view/IoArt.h)
    — SLOW its first gear, FAST its second, PARK the rover on the shoulder. */
std::unique_ptr<redpaw::Drive> IoEditor::makeDrive()
{
    return std::make_unique<IoDrive> (shippedArt());
}

void IoEditor::tellWorld (IoDrive& d) const
{
    // Each parameter's own value, 0..1 over its range (both are linear: DRIVE 0..10, BLEND 0..1).
    if (auto* p = amp.apvts.getParameter (params::preampGain))
        d.setDrive (p->getValue());
    if (auto* p = amp.apvts.getParameter (params::ampBlend))
        d.setBlend (p->getValue());
    d.setPresence (presence != nullptr ? (double) presence->getValue() : (double) presenceDefault);
}

void IoEditor::followWorld()
{
    const float share = ioWorld != nullptr && view->mode() == redpaw::Windscreen::Mode::gpu ? ioWorld->cabinDark() : 0.0f;
    setCabinDark ((float) juce::roundToInt (share * (float) Cabin::darkSteps) / (float) Cabin::darkSteps);
}

void IoEditor::setCabinDark (float share)
{
    share = juce::jlimit (0.0f, 1.0f, share);
    if (juce::approximatelyEqual (share, dark))
        return;
    dark = share;
    cabin().dark = share;
    picture->repaint();
    for (int i = 0; i < 6; ++i)
        roofKey (i).repaint();
}

IoEditor::Spot& IoEditor::spot (juce::Rectangle<float> onStage, const juce::String& name, std::function<void()> onClick)
{
    auto& s = *spots.emplace_back (std::make_unique<Spot>());
    s.name = name;
    s.setTitle (name);          // ...and what it is called to whoever asks — a screen reader, the face's gate
    s.onClick = std::move (onClick);
    s.onHover = [this, &s] (bool over) { sayName (s.getBounds().toFloat(), s.name, over); };
    if (s.onClick != nullptr)
        s.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    addToStage (s);
    s.setBounds (onStage.toNearestInt());
    return s;
}

void IoEditor::nameOnHover (juce::Component& c, const juce::String& name)
{
    auto& h = *hovers.emplace_back (std::make_unique<Hover>());
    h.said = [this, &c, name] (bool over) { sayName (c.getBoundsInParent().toFloat(), name, over); };
    c.addMouseListener (&h, true);
}

void IoEditor::sayName (juce::Rectangle<float> over, const juce::String& name, bool shown)
{
    nameTag.show ({ over.getCentreX(), over.getY() - 14.0f }, shown ? name : juce::String());
}

template <typename Control>
void IoEditor::bindTravel (Control& control, const juce::String& paramId, std::function<float (float)> toTravel,
                           std::function<float (float)> toPlain)
{
    auto* p = amp.apvts.getParameter (paramId);
    jassert (p != nullptr);
    if (p == nullptr)
        return;
    auto* a = follow (paramId.toRawUTF8(), [&control, toTravel] (float v) { control.setValue (toTravel (v), false); });
    control.onGestureStart = [a] { a->beginGesture(); };
    control.onChange       = [a, toPlain] (float t) { a->setValueAsPartOfGesture (toPlain (t)); };
    control.onGestureEnd   = [a] { a->endGesture(); };
    control.defaultValue   = toTravel (p->convertFrom0to1 (p->getDefaultValue()));
    a->sendInitialUpdate();
}

void IoEditor::bindSwitch (redgroove::SlideSwitch& sw, const char* paramId)
{
    auto* a = follow (paramId, [&sw] (float v) { sw.setPosition (juce::roundToInt (v), false); });
    sw.onFlip = [a] (int to) { a->setValueAsCompleteGesture ((float) to); };
    a->sendInitialUpdate();
}

/** EACH SOCKET ITS TONE BY NAME: PRESENCE the pack's tone named so, BASS, MIDDLE (or MID) and TREBLE wherever the
    channel's pack lists them (Channels::toneNamed) — a knob bound again only where its parameter is another now; BASS,
    MIDDLE or TREBLE the pack has no tone for, a frozen knob at its noon, dim, taking no hand, its ring dim with it;
    PRESENCE the pack has none for, the rover's own parameter (presenceParam): live, the world's snow, no sound. */
void IoEditor::bindTones (int channel)
{
    using Band = Channels::Band;
    constexpr Band bands[] { Band::bass, Band::middle, Band::treble };
    const auto names = channels.toneNamesOf (channel);
    for (size_t s = 0; s < tones.size(); ++s)
    {
        auto& knob = *tones[s];
        int t = -1;
        if (s == presenceSocket)
            t = names.indexOf ("Presence", true);
        else
            t = channels.toneNamed (channel, bands[s - 1]);
        auto id = t >= 0 ? channels.toneOf (channel, t) : juce::String();
        if (s == presenceSocket)
        {
            presenceSilent = id.isEmpty();
            if (presenceSilent)
                id = presenceParam;
        }
        auto* p = id.isNotEmpty() ? amp.apvts.getParameter (id) : nullptr;
        if (p == nullptr)
        {
            knob.onGestureStart = nullptr;
            knob.onChange       = nullptr;
            knob.onGestureEnd   = nullptr;
            toneBindings[s].reset();
            toneBound[s] = {};
            knob.setValue (0.5f, false);
        }
        else if (id != toneBound[s])
        {
            toneBindings[s] = std::make_unique<juce::ParameterAttachment> (*p, [this, s, &knob, p] (float v)
            {
                knob.setValue (p->convertTo0to1 (v), false);
                if (s == presenceSocket && ioWorld != nullptr)
                    tellWorld (*ioWorld);       // PRESENCE, the sulphur snow
            });
            auto* a = toneBindings[s].get();
            knob.onGestureStart = [a] { a->beginGesture(); };
            knob.onChange       = [a, p] (float v) { a->setValueAsPartOfGesture (p->convertFrom0to1 (v)); };
            knob.onGestureEnd   = [a] { a->endGesture(); };
            knob.defaultValue   = amp.engineDefault (*p);
            toneBound[s] = id;
            a->sendInitialUpdate();
        }
        if (s == presenceSocket)
        {
            presence = p;
            if (ioWorld != nullptr)
                tellWorld (*ioWorld);
        }
        const bool live = p != nullptr;
        knob.setEnabled (live);
        knob.setInterceptsMouseClicks (live, false);
        knob.setAlpha (live ? 1.0f : 0.4f);
        if (s + 1 < rings.size())
            rings[s + 1]->setAlpha (live ? 1.0f : 0.4f);
    }
}

void IoEditor::hearSlot (int at)
{
    dragAt = at;
    channels.hearSlot (at);
}

void IoEditor::chooseSlot (int at)
{
    if (dragAt >= 0)
    {
        channels.chooseSlot (at);
        channels.setCabinetOn (true);
    }
    dragAt = -1;
}

/** The lever goes where the state is — PARK while the amp is out, else the speed — unless the hand holds it; the view in
    the speed's gear; the tones the channel's, by name. */
void IoEditor::showSelector (int channel)
{
    const int place = parked ? 0 : channel + 1;
    if (! lever.isHeld() && lever.getPlace() != place)
        lever.setPlace (place, true, false);
    lever.repaint();
    cabinet.refresh();
    view->setGear (channel + 1);        // SLOW the first gear, FAST the second
    bindTones (channel);
}

/** PARK: the amp's bypass, from the lever, a host's bypass or anywhere — the lever to it, the view parked. */
void IoEditor::showPark (bool on)
{
    parked = on;
    view->setPark (on);
    const int place = on ? 0 : channels.active() + 1;
    if (! lever.isHeld() && lever.getPlace() != place)
        lever.setPlace (place, true, false);
    lever.repaint();
}

/** The number keys pull the lever: 1 is SLOW, 2 FAST. */
bool IoEditor::channelKey (const juce::KeyPress& key)
{
    const auto c = (juce::juce_wchar) key.getKeyCode();
    const int at = c >= '1' && (int) c < '1' + channels.count() ? (int) (c - '1') : -1;
    if (at < 0)
        return false;
    if (! lever.isHeld())
        lever.setPlace (at + 1, true);
    return true;
}

//==============================================================================
/** THE ROVER'S OWN PART OF THE SITE'S DEMO (Console::webExtras), in the keys the family's faces have written already and no
    others — what they cannot carry (the pictograms, the B612 words, the night's glow, the dosimeter) the kit's README
    says: under places.json's "world", the box the view is drawn in (the whole stage: the picture's alpha is its
    windscreen), the lever and its three places — PARK, SLOW, FAST — the gears the two speeds drive in, the four knobs —
    PRESENCE and the three tones — and their rings, the five columns — COMP, BLEND, OCTAVE, CHORUS and REVERB — COMP's
    squeeze, the cuts' two switches, the cabinet's screen and the one reading, the cabinet's name. Each channel's own
    sockets go into the lineup's "face" (the console's key). THE STILL: the view's first frame, the whole stage at twice
    its size, the first a poster's frame brings: the Io world (makeDrive), as the posters, under the knobs as they stand
    (tellWorld). */
void IoEditor::webExtras (const juce::File& dir, juce::DynamicObject& kit, juce::DynamicObject& world)
{
    using Obj = juce::DynamicObject;
    using Band = Channels::Band;
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
    const auto colour = [] (juce::Colour c) { return juce::String::formatted ("#%02x%02x%02x%02x", c.getRed(), c.getGreen(), c.getBlue(), c.getAlpha()); };

    world.setProperty ("windscreen", rect (layout::view));
    world.setProperty ("gears", channels.count());
    {
        juce::Array<juce::var> gears;       // SLOW the first gear, FAST the second (showSelector); PARK parks; no TURBO
        for (int c = 0; c < channels.count(); ++c)
            gears.add (c + 1);
        world.setProperty ("channelGears", gears);
    }

    // THE LEVER: its places top to bottom — PARK, then the two speeds — the slot it runs in, and where each place's sign
    // stands beside it (P's centre, the triangles' centres): signs, not words, all three in the cabin.
    {
        auto* l = new Obj();
        l->setProperty ("box", rect (boxOf (lever)));
        l->setProperty ("slotX", layout::leverX);
        const juce::Point<float> signsAt[] { { layout::leverSignX, layout::parkBaseline - 7.5f },
                                             { layout::leverSignX, layout::leverStops[1] },
                                             { layout::leverSignX, layout::leverStops[2] } };
        const char* words[] { "PARK", "SLOW", "FAST" };
        juce::Array<juce::var> stops, names;
        for (size_t i = 0; i < layout::leverStops.size() && i < std::size (signsAt); ++i)
        {
            stops.add (layout::leverStops[i]);
            auto* n = new Obj();
            n->setProperty ("name", words[i]); n->setProperty ("at", point (signsAt[i]));
            names.add (juce::var (n));
        }
        l->setProperty ("stops", stops);
        l->setProperty ("names", names);
        l->setProperty ("capWidth", redgroove::ChannelLever::capWidth);
        l->setProperty ("capHeight", redgroove::ChannelLever::capHeight);
        l->setProperty ("does", "place 0 PARK: the amp's bypass (the plug-in's own bypass parameter), the speed kept; "
                                "place i (1, 2): channel i - 1 (the lineup's SLOW and FAST, a pair over one memory: one sound), "
                                "its gear i, the amp back in");
        world.setProperty ("lever", l);
    }

    // THE KNOBS, PRESENCE AND THE TONES, BASS, MIDDLE AND TREBLE: each the playing channel's tone of that name; and the
    // rings, DRIVE's (the console's drive) and theirs, in Io's paint.
    const char* knobNames[] { "PRESENCE", "BASS", "MIDDLE", "TREBLE" };
    const std::pair<Kind, layout::GlyphAt> knobSigns[] { { Kind::presence, layout::presenceGlyph }, { Kind::bass, layout::toneGlyphs[0] },
                                                         { Kind::middle, layout::toneGlyphs[1] }, { Kind::treble, layout::toneGlyphs[2] } };
    constexpr Band toneBands[] { Band::bass, Band::middle, Band::treble };
    {
        juce::Array<juce::var> a;
        for (size_t s = 0; s < tones.size(); ++s)
        {
            auto* v = new Obj();
            v->setProperty ("id", juce::String (knobNames[s]).toLowerCase()); v->setProperty ("name", knobNames[s]);
            v->setProperty ("box", rect (boxOf (*tones[s]))); v->setProperty ("sprite", "knob-tone");
            v->setProperty ("travel", s == presenceSocket
                ? "the playing channel's tone named Presence (lineup.channels[c].face.tones, by text): its parameter's 0..1 over "
                  "its min..max; a channel with none (the stand-in's): the rover's own io_presence, 0..1, opening at 0.2, live, "
                  "its tone null: the world's snow, no sound"
                : "the playing channel's tone of this name (lineup.channels[c].face.tones, by text): its parameter's 0..1 over its "
                  "min..max; a channel with none: the knob frozen at noon (0.5), alpha 0.4, its ring too, no hand");
            a.add (juce::var (v));
        }
        world.setProperty ("knobs", a);

        auto* r = new Obj();
        r->setProperty ("inset", layout::ringInset); r->setProperty ("width", layout::ringWidth); r->setProperty ("driveWidth", layout::driveRingWidth);
        r->setProperty ("sockets", list ({ juce::var ("drive"), juce::var ("presence"), juce::var ("tones") }));
        world.setProperty ("rings", r);

        auto* ring = new Obj();
        ring->setProperty ("from", KnobRing::from);
        ring->setProperty ("sweep", KnobRing::sweep);
        ring->setProperty ("light", colour (ringLight));
        ring->setProperty ("rest", colour (KnobRing::restOf (ringLight)));
        ring->setProperty ("litAlpha", KnobRing::litAlpha);
        ring->setProperty ("litAlphaHand", KnobRing::litAlphaHand);
        ring->setProperty ("glowAlpha", KnobRing::glowAlpha);
        ring->setProperty ("glowAlphaHand", KnobRing::glowAlphaHand);
        ring->setProperty ("glowRadius", KnobRing::glowRadius);
        ring->setProperty ("glowRadiusHand", KnobRing::glowRadiusHand);
        ring->setProperty ("rimOfKnob", KnobRing::rimOfKnob);
        ring->setProperty ("rimAlpha", KnobRing::rimAlpha);
        world.setProperty ("knobRing", juce::var (ring));
    }

    // EACH CHANNEL'S SOCKETS, in the lineup's "face": the rover lays no row of the console's tone knobs, so the console
    // wrote none. Here each socket the channel has a tone for by name (as bindTones binds it): the knob's box, its
    // pictogram's box — the pictogram in the cabin, no icon of its own — its name, the pack's tone and its parameter. A
    // socket missing from a channel's list stands frozen on it. PRESENCE is never missing: where the pack has no tone
    // named so, its tone is null and its parameter the rover's own (presenceParam).
    if (auto* line = kit.getProperty ("lineup").getDynamicObject())
        if (auto* all = line->getProperty ("channels").getArray())
            for (int c = 0; c < all->size() && c < channels.count(); ++c)
                if (auto* f = all->getReference (c).getProperty ("face", {}).getDynamicObject())
                {
                    const auto names = channels.toneNamesOf (c);
                    juce::Array<juce::var> knobs;
                    for (size_t s = 0; s < tones.size(); ++s)
                    {
                        const int t = s == presenceSocket ? names.indexOf ("Presence", true) : channels.toneNamed (c, toneBands[s - 1]);
                        const bool own = t < 0 && s == presenceSocket;
                        if (t < 0 && ! own)
                            continue;
                        const auto& [sign, at] = knobSigns[s];
                        auto* k = new Obj();
                        k->setProperty ("box", rect (boxOf (*tones[s])));
                        k->setProperty ("label", rect (glyphs::boxOf (sign, centreOf (at), at.scale)));
                        k->setProperty ("icon", juce::var());
                        k->setProperty ("text", knobNames[s]);
                        k->setProperty ("tone", own ? juce::var() : juce::var (t));
                        k->setProperty ("param", own ? juce::String (presenceParam) : channels.toneOf (c, t));
                        knobs.add (juce::var (k));
                    }
                    f->setProperty ("tones", knobs);
                }

    // THE COLUMNS, left to right: COMP's threshold, BLEND, and the three effects' amounts.
    {
        struct F { const char* id; const char* name; const face::ColumnFader* c; const char* param; const char* travel; const char* reading; };
        const F faders[] {
            { "comp",   "COMP",   &comp,   params::compThreshold, "travel = threshold / -48 dB: 0 dB at the foot, -48 dB at the head", "whole dB" },
            { "blend",  "BLEND",  &blend,  params::ampBlend,      "the value, 0..1, bottom to top", "percent" },
            { "octave", "OCTAVE", &octave, params::octaveAmount,  "the value, 0..1, bottom to top", "percent" },
            { "chorus", "CHORUS", &chorus, params::chorusAmount,  "the value, 0..1, bottom to top", "percent" },
            { "reverb", "REVERB", &reverb, params::reverbMix,     "the value / 100, bottom to top", "percent" } };
        juce::Array<juce::var> a;
        for (const auto& f : faders)
        {
            auto* v = new Obj();
            v->setProperty ("id", f.id); v->setProperty ("name", f.name); v->setProperty ("box", rect (boxOf (*f.c)));
            v->setProperty ("sprite", "fader-cap"); v->setProperty ("capLength", layout::faderCapLength);
            v->setProperty ("param", f.param); v->setProperty ("travel", f.travel); v->setProperty ("reading", f.reading);
            a.add (juce::var (v));
        }
        world.setProperty ("columns", a);
    }

    // COMP'S SQUEEZE: its gain reduction as a column filling down, its full span.
    {
        auto* g = new Obj();
        g->setProperty ("box", rect (boxOf (squeeze))); g->setProperty ("slot", rect (layout::compGrSlot)); g->setProperty ("across", false);
        g->setProperty ("rangeDb", redgroove::GrColumn::rangeDb); g->setProperty ("reads", "COMP's gain reduction in dB (AmpProcessor::compGrDb)");
        world.setProperty ("squeeze", g);
    }

    // THE CUTS' SWITCHES: two positions each, the one it stands at lit; the parameter's value is the position. Their words
    // either side are the cabin's (B612, in cabin.webp): the switches write none.
    {
        struct S { const char* id; const redgroove::SlideSwitch* sw; const char* left; const char* right; const char* param; };
        const S cuts[] { { "lowCut", &lowCut, "5STR", "4STR", params::bassHpf }, { "highCut", &highCut, "FING", "SLAP", params::bassLpf } };
        juce::Array<juce::var> a;
        for (const auto& w : cuts)
        {
            auto* v = new Obj();
            const auto box = boxOf (*w.sw);
            v->setProperty ("id", w.id); v->setProperty ("box", rect (box)); v->setProperty ("slot", rect (w.sw->slot + box.getPosition()));
            v->setProperty ("words", list ({ juce::var (w.left), juce::var (w.right) }));
            v->setProperty ("param", w.param); v->setProperty ("values", list ({ 0, 1 }));
            a.add (juce::var (v));
        }
        world.setProperty ("switches", a);
    }

    // THE CABINET'S SCREEN: its slider; and the one reading, the playing cabinet's name over it.
    {
        auto* c = new Obj();
        c->setProperty ("box", rect (boxOf (cabinet)));
        c->setProperty ("shows", "the cabinet's slider lying across the screen: a notch each of the bass's dozen (lineup.cabinets, cabSteps), "
                                 "heard at the notch nearest the cap while it moves, kept where it settles");
        world.setProperty ("cabinetScreen", c);

        auto* v = new Obj();
        v->setProperty ("id", "cabinet"); v->setProperty ("box", rect (boxOf (*cabinetLabel)));
        v->setProperty ("format", "the playing cabinet's name, the dozen's number left out: 8x10 STACK; OFF while the cabinet is out");
        world.setProperty ("readings", list ({ juce::var (v) }));
        world.setProperty ("readingPx", stage::px13);
    }

    kit.setProperty ("window", rect (layout::view));
    kit.setProperty ("stills", list ({ juce::var ("still.webp") }));

    // THE POSTERS: the rover as it opens, and one for each place of the lever — PARK, SLOW, FAST — through the glass the
    // view's first frame there. The lever stood at once where the rover opens: when the window first comes up it is still
    // easing there from PARK, and the top poster would catch it half way.
    webPoses.count = (int) layout::leverStops.size();
    webPoses.opening = parked ? 0 : channels.active() + 1;
    lever.setPlace (webPoses.opening, false, false);
    webPoses.standAt = [this] (int at)
    {
        lever.onPlace (at);
        lever.setPlace (at, false, false);
    };
    webStill = {};
    webPoses.firstFrame = [this, dir] (int, std::function<void (juce::Image)> done)
    {
        const auto box = layout::view * 2.0f;
        auto fresh = std::make_unique<IoDrive> (shippedArt());
        tellWorld (*fresh);             // the Sun and Jupiter where the knobs put them
        view->firstFrame (std::move (fresh), juce::roundToInt (box.getWidth()), juce::roundToInt (box.getHeight()),
                          [this, dir, then = std::move (done)] (juce::Image frame)
                          {
                              if (frame.isValid() && ! webStill.isValid())
                              {
                                  webStill = frame;
                                  writeForWeb (dir.getChildFile ("still.webp"), webStill);
                              }
                              then (frame);
                          });
    };
    webPoses.live = "the view's first frame for the place: the Io world (io::IoDrive), on the GPU from its start, at the place's "
                    "gear (PARK parked), under DRIVE, BLEND and PRESENCE as they stand, settled, nothing stepped (t = 0); the top "
                    "poster is the place the app opens at";
    webPoses.still = [this] (int) { return webStill; };
    webPoses.window = layout::view;
    webPoses.hidden = { view.get() };
}

//==============================================================================
/** What the window shows — the view on the GPU, the 1993 landscape on the CPU, or the still — and how often; whether the
    view answers the bass; and full screen. The machine's, kept with its settings. */
void IoEditor::addWindowSections (juce::PopupMenu& menu)
{
    const auto self = [] (Console& e) -> IoEditor& { return static_cast<IoEditor&> (e); };

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

void IoEditor::showWindowMenu()
{
    juce::PopupMenu menu;
    addWindowSections (menu);
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

void IoEditor::chooseLandscape (redpaw::Windscreen::Mode mode)
{
    prefs::store (amp.product()).set (landscapeKey, landscapeWords[(int) mode]);
    view->setMode (mode);
}

void IoEditor::chooseFrameRate (int fps)
{
    prefs::store (amp.product()).set (frameRateKey, fps);
    view->setFrameRateCap (fps);
}

/** Where the stage is in the editor's own pixels, for the GL thread. */
void IoEditor::placeView()
{
    const auto at = (layout::view * stageScale() + stageOrigin()).toNearestInt();
    glassX.store (at.getX());
    glassY.store (at.getY());
    glassW.store (at.getWidth());
    glassH.store (at.getHeight());
    gl.triggerRepaint();
}

void IoEditor::newOpenGLContextCreated()
{
    view->hostContextCreated();
}

void IoEditor::renderOpenGL()
{
    const float k = (float) gl.getRenderingScale();
    const int fw = juce::roundToInt ((float) getWidth() * k), fh = juce::roundToInt ((float) getHeight() * k);
    juce::OpenGLHelpers::clear (juce::Colours::black);

    const auto glass = juce::Rectangle<float> ((float) glassX.load(), (float) glassY.load(), (float) glassW.load(), (float) glassH.load()) * k;
    view->hostRender (glass.toNearestInt(), fw, fh);
}

void IoEditor::openGLContextClosing()
{
    view->hostContextClosing();
}

} // namespace orbitamp::io
