// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "CaliforniaEditor.h"

#include "CaliforniaFaceArt.h"
#include "CaliforniaLayout.h"
#include "CaliforniaLineup.h"

#include "../redpaw/view/PlainsDrive.h"
#include "../ui/CabWindow.h"
#include "../ui/FactoryMissing.h"
#include "../ui/Prefs.h"

#include <BinaryData.h>

namespace orbitamp::california
{

juce::AudioProcessorEditor* makeEditor (AmpProcessor& p)
{
    // No knobs to draw without their renders, which lie in the factory folder: where they are missing, the window
    // says so, and where they belong (ui/FactoryMissing.h).
    if (! FaceArt::present())
        return new ui::FactoryMissingEditor (p, p.product());
    return new CaliforniaEditor (p);
}

using namespace face;
using glyphs::Kind;

namespace
{
    /** What the window shows, and how often — the machine's, kept with its settings: the landscape as a word, in the
        order of Windscreen::Mode, and what the menu calls each. */
    const juce::Identifier landscapeKey { "landscape" };
    const juce::Identifier frameRateKey { "landscape_fps" };
    const juce::StringArray landscapeWords { "world", "1993", "still" };
    const juce::StringArray landscapeNames { "The view", "1993", "Still" };

    // Where the pointer is drawn on the knobs' renders — the walker's, whose renders these are.
    constexpr ArtKnob::Pointer gainPointer { 0.30f, 0.66f, 0.06f };
    constexpr ArtKnob::Pointer tonePointer { 0.30f, 0.64f, 0.07f };

    /** THE HAND'S LIGHT: a soft orange round whatever the hand is over or on — a line, never a wash. */
    const juce::Colour handLight = orange.withAlpha (0.8f);

    /** THE RINGS round the knobs, in their sockets' grooves: the van's orange. */
    const juce::Colour ringLight { 0xffe0803c };

    /** WHAT THE FEW WORDS LEFT ARE WRITTEN IN — the tuner's, the cabinet's name in its screen, PRESET, the version, MEM and
        CPU on the van's body over the plates: cream, a dark edge under them. */
    Ink cabinInk()
    {
        Ink ink;
        ink.words  = glyphs::capFill;
        ink.values = glyphs::capFill;
        ink.state  = juce::Colour (0xffe0803c);
        ink.edge   = juce::Colour (0x99000000);
        return ink;
    }

    /** ...and on the light plates either side of the maker's: the panels' brown, no edge. */
    Ink plateInk()
    {
        Ink ink;
        ink.words = ink.values = glyphs::ink;
        return ink;
    }

    juce::Rectangle<float> around (juce::Point<float> c, float w, float h)
    {
        return juce::Rectangle<float> (w, h).withCentre (c);
    }

    /** THE PICTOGRAMS OVER THE SOCKETS, where the approved mockup has them (van.html): each with its full name. */
    struct Sign
    {
        Kind kind;
        juce::Point<float> at;
        float scale;
        const char* name;
    };
    const std::vector<Sign>& signs()
    {
        using namespace layout;
        static const std::vector<Sign> all {
            { Kind::bass,    { toneX[0], glyphY },           glyphScale,      "BASS" },
            { Kind::middle,  { toneX[1], glyphY },           glyphScale,      "MIDDLE" },
            { Kind::treble,  { toneX[2], glyphY },           glyphScale,      "TREBLE" },
            { Kind::gain,    { gain.x, gainGlyphY },         glyphScale,      "GAIN" },
            { Kind::volume,  { volumeGlyphX, glyphY },       smallGlyphScale, "VOLUME" },
            { Kind::out,     { outGlyphX, levelGlyphY },     outGlyphScale,   "OUTPUT" },
            { Kind::reverb,  { effectGlyphX[2], glyphY },    glyphScale,      "REVERB" },
            { Kind::delay,   { effectGlyphX[0], glyphY },    smallGlyphScale, "DELAY" },
            { Kind::phaser,  { effectGlyphX[1], glyphY },    smallGlyphScale, "PHASER" },
            { Kind::comp,    { compGlyphX, glyphY },         smallGlyphScale, "COMP" },
            { Kind::input,   { inGlyphX, levelGlyphY },      inGlyphScale,    "INPUT" },
            { Kind::clean,   { leverGlyphX, leverStops[1] }, leverGlyphScale, "CLEAN" },
            { Kind::dirty,   { leverGlyphX, leverStops[2] }, leverGlyphScale, "DIRTY" },
            { Kind::gate,    gateSlot.getCentre(),           capGlyphScale,   "GATE" },
            { Kind::limiter, limiterSlot.getCentre(),        capGlyphScale,   "LIMITER" },
        };
        return all;
    }

    /** WHERE THE CONSOLE'S CONTROLS STAND on the dashboard (CaliforniaLayout.h). */
    Console::Places cabinPlaces()
    {
        using namespace layout;
        using namespace stage;

        Console::Places p;
        p.drive = around (gain, gainKnob, gainKnob);

        // No row of tone knobs (the face has its own, by name) and no row of a pack's switches: neither strip has room.
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

        // No REVERB, DELAY or CAB plates: REVERB and DELAY are faders, and CAB's box is the cabinet's screen.
        p.plates = false;
        p.sliderNames[2] = cabinetScreen;

        p.faderCapLength = faderCapLength;
        p.faderCapSquash = faderCapSquash;
        p.inFader  = inFaderSlot.withSizeKeepingCentre (faderWidth, inFaderSlot.getHeight());
        p.outFader = outFaderSlot.withSizeKeepingCentre (faderWidth, outFaderSlot.getHeight());
        p.inMeter  = inMeterSlot.reduced (2.0f, 4.0f);
        p.outMeter = outMeterSlot.reduced (2.0f, 4.0f);

        p.tuner = tunerScreen.withTrimmedLeft (tunerLeftInset).reduced (6.0f, 3.0f);     // right of the fork
        p.tunerStrip = true;
        p.tunerMenuOnClick = true;

        // The lamps, drawn by code: each overload under its meter — over it stand the jack and the speaker — GATE's and
        // LIMITER's at the left of their caps.
        p.lampDot = 8.0f;
        p.lamps[inClipLamp]  = around ({ inMeterSlot.getCentreX(), inMeterSlot.getBottom() + 11.0f }, 14.0f, 14.0f);
        p.lamps[outClipLamp] = around ({ outMeterSlot.getCentreX(), outMeterSlot.getBottom() + 11.0f }, 14.0f, 14.0f);
        p.lamps[gateLamp]    = around ({ gateSlot.getX() + 15.0f, gateSlot.getCentreY() }, 14.0f, 14.0f);
        p.lamps[limiterLamp] = around ({ limiterSlot.getX() + 15.0f, limiterSlot.getCentreY() }, 14.0f, 14.0f);

        // GATE's and LIMITER's doors are no words (the owner, 20.11): none of the console's — the caps are the face's own,
        // their pictograms on them, a click on either the menu the door opens (CaliforniaEditor's spots).
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
        return { art.knobDrive, gainPointer, art.knobTone, tonePointer,
                 art.faderCap,
                 art.faderCap,
                 { &art.none, &art.none, &art.none, &art.none },
                 mark.isNotEmpty() ? mark : juce::String::fromUTF8 (BinaryData::catlogo_svg, BinaryData::catlogo_svgSize),
                 FaceArt::midiCommands(),
                 handLight,
                 cabinInk(),
                 juce::Colour (0xffe0803c) };
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

    // The panel, unless the world is drawing it — and always in a picture taken. The pendant hangs still under the mirror
    // until the world swings it.
    const bool worldHasIt = onScreen && worldDrawsPanel != nullptr && worldDrawsPanel();
    const auto& art = FaceArt::of();
    if (art.bg.isValid() && ! worldHasIt)
        g.drawImage (art.bg, juce::Rectangle<float> (0.0f, 0.0f, width, height));
    if (art.pendant.isValid() && ! worldHasIt)
        g.drawImage (art.pendant, pendant);

    // The family's cat, cut into the maker's plate; DARWIN'S CAT and POWERED BY on the blank plates either side of it.
    engraveMark (g, emblemMark, scale);
    const auto plates = plateInk();
    const auto onPlate = [&g, &plates] (const juce::String& words, juce::Rectangle<float> plate)
    {
        // ...set condensed, as tall, into the room between the plate's screws where it is wider.
        const float room = plate.getWidth() - 34.0f;
        const float wide = juce::GlyphArrangement::getStringWidth (labelFont (px13).withExtraKerningFactor (0.08f), words);
        const juce::Graphics::ScopedSaveState keep (g);
        if (wide > room)
            g.addTransform (juce::AffineTransform::scale (room / wide, 1.0f, plate.getCentreX(), plate.getCentreY()));
        plates.label (g, words, plate.withSizeKeepingCentre (wide + 24.0f, plate.getHeight()), px13, plates.words);
    };
    onPlate ("POWERED BY", poweredByPlate);
    onPlate ("DARWIN'S CAT", makerNamePlate);

    // GATE's and LIMITER's caps, the van's own; and every pictogram over its socket, the fork on the tuner's dark glass.
    glyphs::drawCap (g, gateSlot);
    glyphs::drawCap (g, limiterSlot);
    for (const auto& s : signs())
        glyphs::draw (g, s.kind, s.at, s.scale);
    glyphs::drawLetter (g, "P", parkLetterX, parkLetterBaseline, parkLetterSize);
    glyphs::draw (g, Kind::fork, { forkX, forkY }, forkScale, glyphs::cream);
}

//==============================================================================
void CaliforniaEditor::CabinetPicture::paint (juce::Graphics& g)
{
    // The cabinet that plays, drawn as the cabinets' window draws it, on the screen's dark glass — or, a box that window
    // does not draw (the guitar's), a cabinet of as many cones as its name's size says, "4x12" four; none for a file.
    const auto area = getLocalBounds().toFloat().reduced (10.0f, 4.0f).withTrimmedBottom (20.0f);
    if (cab >= 0)
    {
        CabPicker::paintSketch (g, cab, setup, area);
        return;
    }
    int cones = 0;
    for (int i = 0; i + 2 < words.length(); ++i)
        if (juce::CharacterFunctions::isDigit (words[i]) && (words[i + 1] == 'x' || words[i + 1] == 'X')
            && juce::CharacterFunctions::isDigit (words[i + 2]) && (i == 0 || ! juce::CharacterFunctions::isDigit (words[i - 1])))
        {
            cones = words[i] - '0';
            break;
        }
    if (cones <= 0)
        return;
    const int rows = cones >= 4 ? 2 : 1, cols = (cones + rows - 1) / rows;
    const float cell = area.getHeight() / (float) rows;
    const auto box = juce::Rectangle<float> (cell * (float) cols + 6.0f, area.getHeight()).withCentre (area.getCentre());
    const auto line = glyphs::cream.withAlpha (0.75f);
    g.setColour (line);
    g.drawRoundedRectangle (box, 4.0f, 1.8f);
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols && r * cols + c < cones; ++c)
        {
            const juce::Point<float> at { box.getX() + 3.0f + cell * ((float) c + 0.5f), box.getY() + cell * ((float) r + 0.5f) };
            const float radius = cell * 0.4f;
            g.drawEllipse (juce::Rectangle<float> (2.0f * radius, 2.0f * radius).withCentre (at), 1.5f);
            g.fillEllipse (juce::Rectangle<float> (0.6f * radius, 0.6f * radius).withCentre (at));
        }
}

//==============================================================================
CaliforniaEditor::CaliforniaEditor (AmpProcessor& p)
    : face::Console (p, std::make_unique<Cabin>(), cabinPlaces(), cabinLook()),
      view (std::make_unique<redpaw::Windscreen> (makeDrive())),
      comp (FaceArt::of().faderCap), delay (FaceArt::of().faderCap), phaser (FaceArt::of().faderCap),
      reverb (FaceArt::of().faderCap), volume (FaceArt::of().faderCap),
      squeeze ([&p] { return p.compGrDb.load (std::memory_order_relaxed); }),
      // ...its places named, and no name written by them: the pictograms beside the stops are the cabin's (no centres given)
      lever (cabinInk(), { "PARK", "CLEAN", "DIRTY" }, layout::leverStops, layout::leverX, {}, layout::leverBox.getPosition()),
      cabinet (cabinInk(), FaceArt::of().faderCap, layout::faderCapLength, layout::faderCapSquash),
      park (p.bypassParameter(), [this] (float v) { showPark (v > 0.5f); }, nullptr)
{
    // Everything this constructor sets is the face settling onto the state, not a player's edit.
    const felitronics::appkit::CompareHistory::ScopedSuppress settling (amp.history);

    // THE SIX KEYS: cream caps with an orange edge (the mockup's), UNDO's and REDO's arrows and the letters A to D in
    // Shrikhand on them; the register that plays its edge heavier and its letter orange.
    for (int i = 0; i < 6; ++i)
    {
        roofKey (i).drawKey = [this, &k = roofKey (i), i] (juce::Graphics& g, juce::Rectangle<float> box, bool down, float)
        {
            const bool history = i < ovFirstRegister;
            const bool lit = ! history && k.isOn();
            const bool live = ! history || (i == ovUndo ? amp.history.canUndo() : amp.history.canRedo());
            const bool held = down && live;

            glyphs::drawCap (g, box, held, lit);
            const float drop = held ? 1.0f : 0.0f;
            const auto ink = glyphs::ink.withAlpha (live ? 1.0f : 0.35f);
            if (history)
                glyphs::draw (g, i == ovUndo ? Kind::undo : Kind::redo, { box.getCentreX(), box.getCentreY() - 1.0f + drop }, 1.0f, ink);
            else
                glyphs::drawLetter (g, k.name, box.getCentreX(), box.getCentreY() + 7.0f + drop, layout::letterSize,
                                    lit ? glyphs::capEdge : ink);

            if (k.isMouseOverOrDragging() && live)
            {
                juce::Path rim;
                rim.addRoundedRectangle (box.reduced (2.0f), 8.0f);
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
    ringFor (driveKnob(), layout::gain, layout::gainSocket, layout::gainRingWidth);
    nameOnHover (driveKnob(), "GAIN");

    // THE TONES, BASS, MIDDLE AND TREBLE, a knob in each socket (bindTones says which tone each turns).
    const char* toneNames[] { "BASS", "MIDDLE", "TREBLE" };
    for (size_t s = 0; s < tones.size(); ++s)
    {
        tones[s] = std::make_unique<face::ArtKnob> (FaceArt::of().knobTone, tonePointer);
        auto& knob = *tones[s];
        addToStage (knob);
        knob.setBounds (around ({ layout::toneX[s], layout::toneY }, layout::toneKnob, layout::toneKnob).toNearestInt());
        knob.setComponentID (juce::String ("tone-") + juce::String (toneNames[s]).toLowerCase());
        knob.onHover = [this, s, name = juce::String (toneNames[s])] (bool over)
        {
            sayName (tones[s]->getBounds().toFloat(), name, over);
        };
        ringFor (knob, { layout::toneX[s], layout::toneY }, layout::toneSocket, layout::ringWidth);
    }

    // THE FADERS: COMP and its squeeze, DELAY and REVERB live; VOLUME and PHASER dark — the guitar's chain keeps no level of
    // a channel's own and has no phaser yet. Each live one says where it stands in the tag over it while it is held.
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
    const auto upright = [] (juce::Rectangle<float> slot) { return slot.withSizeKeepingCentre (layout::effectFaderWidth, slot.getHeight()); };
    faderIn (comp,   layout::compSlot.withSizeKeepingCentre (layout::faderWidth, layout::compSlot.getHeight()), "comp");
    faderIn (volume, layout::volumeSlot.withSizeKeepingCentre (layout::faderWidth, layout::volumeSlot.getHeight()), "volume");
    faderIn (delay,  upright (around ({ layout::effectX[0], layout::effectY }, 16.0f, layout::effectSlotHeight)), "delay");
    faderIn (phaser, upright (around ({ layout::effectX[1], layout::effectY }, 16.0f, layout::effectSlotHeight)), "phaser");
    faderIn (reverb, upright (around ({ layout::effectX[2], layout::effectY }, 16.0f, layout::effectSlotHeight)), "reverb");
    addToStage (squeeze);
    squeeze.setBounds (layout::compGrSlot.reduced (2.0f, 4.0f).toNearestInt());   // inside the render's slot, as the meters are
    squeeze.setComponentID ("squeeze");
    for (auto* dark : { &volume, &phaser })
    {
        dark->setValue (0.0f, false);
        dark->setEnabled (false);
        dark->setInterceptsMouseClicks (false, false);
        dark->setAlpha (0.35f);
    }

    bindTravel (comp, params::compThreshold, [] (float db) { return db / params::compThresholdMin; },
                [] (float t) { return t * params::compThresholdMin; });
    sayLeverAs (comp, "COMP", [this] { return juce::String (juce::roundToInt (getParam (params::compThreshold))) + " dB"; });
    const char* effectIds[] { params::delayMix, params::reverbMix };
    face::ColumnFader* effectFaders[] { &delay, &reverb };
    const char* effectNames[] { "DELAY", "REVERB" };
    for (size_t i = 0; i < 2; ++i)
    {
        bindTravel (*effectFaders[i], effectIds[i], [] (float v) { return v / 100.0f; }, [] (float t) { return t * 100.0f; });
        sayLeverAs (*effectFaders[i], effectNames[i],
                    [this, id = effectIds[i]] { return juce::String (juce::roundToInt (getParam (id))) + "%"; });
    }
    nameOnHover (comp, "COMP");
    nameOnHover (delay, "DELAY");
    nameOnHover (reverb, "REVERB");
    nameOnHover (squeeze, "COMP: ITS SQUEEZE");

    // THE LEVER: PARK over the two channels. PARK takes the whole amp out (the amp's bypass) and leaves the channel as it
    // was; a channel brings the amp back in, on that channel.
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
    nameOnHover (lever, "CHANNEL");

    // THE CABINET'S SLIDER in its narrow screen: a notch a cabinet of the TOP 12, set and fixed, as the bathyscaphe's; and
    // the cabinet that plays drawn in the wide screen under it, its name along the screen's foot — the slot's under the
    // hand while it moves the slider, OFF while the cabinet is out of the sound.
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
        // ...its name alone, the TOP 12's number left out, as the bathyscaphe's: "4x12 SELFISH GENE".
        auto words = plays();
        if (const auto head = words.upToFirstOccurrenceOf (": ", false, false); words.contains (": ") && head.isNotEmpty() && head.containsOnly ("0123456789"))
            words = words.fromFirstOccurrenceOf (": ", false, false);
        if (words != cabinetPicture.words)
        {
            cabinetPicture.words = words;
            cabinetPicture.repaint();
        }
        showCabinet();
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
    addToStage (cabinetPicture);
    cabinetPicture.setBounds (layout::cabinetPicture.reduced (6.0f, 4.0f).toNearestInt());
    cabinetPicture.setComponentID ("cabinetPicture");
    cabinetLabel = std::make_unique<redgroove::Readout> (cabinInk(), stage::px13);     // 13 px: the family's floor for what is read
    cabinetLabel->colour = glyphs::capFill.withAlpha (0.85f);
    addToStage (*cabinetLabel);
    cabinetLabel->setBounds (juce::Rectangle<float> (layout::cabinetPicture.getX() + 10.0f, layout::cabinetPicture.getBottom() - 26.0f,
                                                     layout::cabinetPicture.getWidth() - 20.0f, 20.0f).toNearestInt());
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
        auto box = glyphs::boxOf (s.kind, s.at, s.scale).expanded (3.0f);
        std::function<void()> click;
        if (s.kind == Kind::clean || s.kind == Kind::dirty)
            click = [this, at = s.kind == Kind::clean ? 1 : 2] { lever.setPlace (at, true, true); };
        if (s.kind == Kind::gate || s.kind == Kind::limiter)
        {
            const auto cap = (s.kind == Kind::gate ? layout::gateSlot : layout::limiterSlot).reduced (3.0f);
            box = cap.withTrimmedLeft (24.0f);      // the whole cap but its lamp
            click = s.kind == Kind::gate ? std::function<void()> ([this] { showGateMenu(); })
                                         : std::function<void()> ([this] { showOutputMenu(); });
        }
        spot (box, s.name, std::move (click)).setComponentID (juce::String ("sign-") + juce::String (s.name).toLowerCase());
    }
    spot (around ({ layout::parkLetterX, layout::parkLetterBaseline - 7.0f }, 24.0f, 24.0f), "PARK",
          [this] { lever.setPlace (0, true, true); }).setComponentID ("sign-park");
    spot (glyphs::boxOf (Kind::fork, { layout::forkX, layout::forkY }, layout::forkScale).expanded (3.0f), "TUNER");
    // ...and the dark faders, which take no hand: their names over their slots too.
    spot (volume.getBounds().toFloat(), "VOLUME");
    spot (phaser.getBounds().toFloat(), "PHASER");

    // THE VIEW, under the whole stage: the panel's alpha is its windscreen.
    stage.addAndMakeVisible (*view, 0);
    view->setBounds (layout::view.toNearestInt());
    view->addMouseListener (&glassClicks, false);
    glassClicks.menu = [this] { showWindowMenu(); };
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

CaliforniaEditor::~CaliforniaEditor()
{
    view->removeMouseListener (&glassClicks);
    gl.detach();
}

/** THE WORLD BEHIND THE GLASS: until the van's own exists, the Mars plains (redpaw::PlainsDrive), a gear a channel. */
std::unique_ptr<redpaw::Drive> CaliforniaEditor::makeDrive()
{
    // SEAM: the California world's CaliforniaDrive goes here (feat/california-world).
    return std::make_unique<redpaw::PlainsDrive> (lineup().count());
}

CaliforniaEditor::Spot& CaliforniaEditor::spot (juce::Rectangle<float> onStage, const juce::String& name, std::function<void()> onClick)
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

void CaliforniaEditor::nameOnHover (juce::Component& c, const juce::String& name)
{
    auto& h = *hovers.emplace_back (std::make_unique<Hover>());
    h.said = [this, &c, name] (bool over) { sayName (c.getBoundsInParent().toFloat(), name, over); };
    c.addMouseListener (&h, true);
}

void CaliforniaEditor::sayName (juce::Rectangle<float> over, const juce::String& name, bool shown)
{
    nameTag.show ({ over.getCentreX(), over.getY() - 14.0f }, shown ? name : juce::String());
}

template <typename Control>
void CaliforniaEditor::bindTravel (Control& control, const juce::String& paramId, std::function<float (float)> toTravel,
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

/** EACH SOCKET ITS TONE BY NAME (Channels::toneNamed): BASS, MIDDLE (or MID) and TREBLE, wherever the channel's pack lists
    them — a knob bound again only where its parameter is another now; a socket the pack has no tone for, a frozen knob
    at its noon, dim, taking no hand, its ring dim with it. */
void CaliforniaEditor::bindTones (int channel)
{
    using Band = Channels::Band;
    constexpr Band bands[] { Band::bass, Band::middle, Band::treble };
    for (size_t s = 0; s < tones.size(); ++s)
    {
        auto& knob = *tones[s];
        const int t = channels.toneNamed (channel, bands[s]);
        const auto id = t >= 0 ? channels.toneOf (channel, t) : juce::String();
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
            toneBindings[s] = std::make_unique<juce::ParameterAttachment> (*p, [&knob, p] (float v) { knob.setValue (p->convertTo0to1 (v), false); });
            auto* a = toneBindings[s].get();
            knob.onGestureStart = [a] { a->beginGesture(); };
            knob.onChange       = [a, p] (float v) { a->setValueAsPartOfGesture (p->convertFrom0to1 (v)); };
            knob.onGestureEnd   = [a] { a->endGesture(); };
            knob.defaultValue   = amp.engineDefault (*p);
            toneBound[s] = id;
            a->sendInitialUpdate();
        }
        const bool live = p != nullptr;
        knob.setEnabled (live);
        knob.setInterceptsMouseClicks (live, false);
        knob.setAlpha (live ? 1.0f : 0.4f);
        if (s + 1 < rings.size())
            rings[s + 1]->setAlpha (live ? 1.0f : 0.4f);
    }
}

void CaliforniaEditor::hearSlot (int at)
{
    dragAt = at;
    channels.hearSlot (at);
}

void CaliforniaEditor::chooseSlot (int at)
{
    if (dragAt >= 0)
    {
        channels.chooseSlot (at);
        channels.setCabinetOn (true);
    }
    dragAt = -1;
}

/** The cabinet that plays — or the slot under the hand — in the picture's screen. */
void CaliforniaEditor::showCabinet()
{
    auto choice = dragAt >= 0 ? AmpProcessor::cabSynthOf (channels.slotFrom (dragAt)) : AmpProcessor::cabSynthOf (amp.cabChoice());
    if (dragAt < 0 && ! channels.cabinetOn())
        choice = {};
    auto& shown = cabinetPicture.shown;
    if (choice.valid == shown.valid && (! choice.valid || choice.state == shown.state))
        return;
    shown = choice;
    const auto drawn = choice.valid ? CabWindow::drawnOf (choice) : std::pair<int, CabPicker::Setup> { -1, {} };
    cabinetPicture.cab = drawn.first;
    cabinetPicture.setup = drawn.second;
    cabinetPicture.repaint();
}

/** The lever goes where the state is — PARK while the amp is out, else the channel — unless the hand holds it; the view
    in the channel's gear; the tones the channel's, by name. */
void CaliforniaEditor::showSelector (int channel)
{
    const int place = parked ? 0 : channel + 1;
    if (! lever.isHeld() && lever.getPlace() != place)
        lever.setPlace (place, true, false);
    lever.repaint();
    cabinet.refresh();
    view->setGear (channel + 1);        // CLEAN the first gear, DIRTY the second
    bindTones (channel);
}

/** PARK: the amp's bypass, from the lever, a host's bypass or anywhere — the lever to it, the view parked. */
void CaliforniaEditor::showPark (bool on)
{
    parked = on;
    view->setPark (on);
    const int place = on ? 0 : channels.active() + 1;
    if (! lever.isHeld() && lever.getPlace() != place)
        lever.setPlace (place, true, false);
    lever.repaint();
}

/** The number keys pull the lever: 1 is CLEAN, 2 DIRTY. */
bool CaliforniaEditor::channelKey (const juce::KeyPress& key)
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
/** THE VAN'S OWN PART OF THE SITE'S DEMO (Console::webExtras), in the keys the family's faces have written already and no
    others — what they cannot carry, the kit's README says: under places.json's "world", the box the view is drawn in (the
    whole stage: the picture's alpha is its windscreen), the lever and its three places — PARK, CLEAN, DIRTY — the gears
    the channels drive in, the three tone sockets and their rings, the five columns — COMP, VOLUME, DELAY, PHASER and
    REVERB, the two dark ones moving nothing — COMP's squeeze, the cabinet's screen and the one reading, the cabinet's
    name. Each channel's own sockets go into the lineup's "face" (the console's key). THE STILL: the view's first frame,
    the whole stage at twice its size, the first a poster's frame brings. Until the California world is ported the view is
    the Mars plains (makeDrive), and so are the still and the posters. */
void CaliforniaEditor::webExtras (const juce::File& dir, juce::DynamicObject& kit, juce::DynamicObject& world)
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
        juce::Array<juce::var> gears;       // CLEAN the first gear, DIRTY the second (showSelector); PARK parks; no TURBO
        for (int c = 0; c < channels.count(); ++c)
            gears.add (c + 1);
        world.setProperty ("channelGears", gears);
    }

    // THE LEVER: its places top to bottom — PARK, then the channels — the slot it runs in, and where each place's sign
    // stands beside it (P's centre, the waves' centres): signs, not words, all three in the cabin.
    {
        auto* l = new Obj();
        l->setProperty ("box", rect (boxOf (lever)));
        l->setProperty ("slotX", layout::leverX);
        const juce::Point<float> signsAt[] { { layout::parkLetterX, layout::parkLetterBaseline - 7.0f },
                                             { layout::leverGlyphX, layout::leverStops[1] },
                                             { layout::leverGlyphX, layout::leverStops[2] } };
        const char* words[] { "PARK", "CLEAN", "DIRTY" };
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
        l->setProperty ("does", "place 0 PARK: the amp's bypass (the plug-in's own bypass parameter), the channel kept; "
                                "place i (1, 2): channel i - 1 (the lineup's), its gear i, the amp back in");
        world.setProperty ("lever", l);
    }

    // THE TONE SOCKETS, BASS, MIDDLE AND TREBLE: each the playing channel's tone of that name; and the rings, GAIN's (the
    // console's drive) and theirs, in the van's orange.
    const char* toneNames[] { "BASS", "MIDDLE", "TREBLE" };
    constexpr Kind toneSigns[] { Kind::bass, Kind::middle, Kind::treble };
    constexpr Band toneBands[] { Band::bass, Band::middle, Band::treble };
    {
        juce::Array<juce::var> a;
        for (size_t s = 0; s < tones.size(); ++s)
        {
            auto* v = new Obj();
            v->setProperty ("id", juce::String (toneNames[s]).toLowerCase()); v->setProperty ("name", toneNames[s]);
            v->setProperty ("box", rect (boxOf (*tones[s]))); v->setProperty ("sprite", "knob-tone");
            v->setProperty ("travel", "the playing channel's tone of this name (lineup.channels[c].face.tones, by text): 0..1 over its "
                                      "min..max; a channel with none: the knob frozen at noon (0.5), alpha 0.4, its ring too, no hand");
            a.add (juce::var (v));
        }
        world.setProperty ("knobs", a);

        auto* r = new Obj();
        r->setProperty ("inset", layout::ringInset); r->setProperty ("width", layout::ringWidth); r->setProperty ("driveWidth", layout::gainRingWidth);
        r->setProperty ("sockets", list ({ juce::var ("drive"), juce::var ("tones") }));
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

    // EACH CHANNEL'S SOCKETS, in the lineup's "face": the van lays no row of the console's tone knobs, so the console wrote
    // none. Here each socket the channel has a tone for by name (Channels::toneNamed, as bindTones binds it): the knob's
    // box, its pictogram's box — the pictogram in the cabin, no icon of its own — its name, the pack's tone and its
    // parameter. A socket missing from a channel's list stands frozen on it.
    if (auto* line = kit.getProperty ("lineup").getDynamicObject())
        if (auto* all = line->getProperty ("channels").getArray())
            for (int c = 0; c < all->size() && c < channels.count(); ++c)
                if (auto* f = all->getReference (c).getProperty ("face", {}).getDynamicObject())
                {
                    juce::Array<juce::var> knobs;
                    for (size_t s = 0; s < tones.size(); ++s)
                    {
                        const int t = channels.toneNamed (c, toneBands[s]);
                        if (t < 0)
                            continue;
                        auto* k = new Obj();
                        k->setProperty ("box", rect (boxOf (*tones[s])));
                        k->setProperty ("label", rect (glyphs::boxOf (toneSigns[s], { layout::toneX[s], layout::glyphY }, layout::glyphScale)));
                        k->setProperty ("icon", juce::var());
                        k->setProperty ("text", toneNames[s]);
                        k->setProperty ("tone", t);
                        k->setProperty ("param", channels.toneOf (c, t));
                        knobs.add (juce::var (k));
                    }
                    f->setProperty ("tones", knobs);
                }

    // THE COLUMNS, left to right: COMP's threshold, VOLUME, DELAY's and REVERB's shares, PHASER — VOLUME and PHASER dark.
    {
        struct F { const char* id; const char* name; const face::ColumnFader* c; const char* param; const char* travel; const char* reading; };
        const F faders[] {
            { "comp",   "COMP",   &comp,   params::compThreshold, "travel = threshold / -48 dB: 0 dB at the foot, -48 dB at the head", "whole dB" },
            { "volume", "VOLUME", &volume, nullptr, "dark: the cap at the foot, alpha 0.35, no hand (the chain keeps no level of a channel's own)", nullptr },
            { "delay",  "DELAY",  &delay,  params::delayMix, "the value / 100, bottom to top", "percent" },
            { "phaser", "PHASER", &phaser, nullptr, "dark: the cap at the foot, alpha 0.35, no hand (the chain has no phaser yet)", nullptr },
            { "reverb", "REVERB", &reverb, params::reverbMix, "the value / 100, bottom to top", "percent" } };
        juce::Array<juce::var> a;
        for (const auto& f : faders)
        {
            auto* v = new Obj();
            v->setProperty ("id", f.id); v->setProperty ("name", f.name); v->setProperty ("box", rect (boxOf (*f.c)));
            v->setProperty ("sprite", "fader-cap"); v->setProperty ("capLength", layout::faderCapLength);
            v->setProperty ("param", f.param != nullptr ? juce::var (f.param) : juce::var());
            v->setProperty ("travel", f.travel);
            v->setProperty ("reading", f.reading != nullptr ? juce::var (f.reading) : juce::var());
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

    // THE CABINET'S SCREEN: its slider; and the one reading, the playing cabinet's name along the picture's foot.
    {
        auto* c = new Obj();
        c->setProperty ("box", rect (boxOf (cabinet)));
        c->setProperty ("shows", "the cabinet's slider lying across: a notch each of the TOP 12 (lineup.cabinets, cabSteps), heard at the "
                                 "notch nearest the cap while it moves, kept where it settles; the playing cabinet drawn in the screen under it");
        world.setProperty ("cabinetScreen", c);

        auto* v = new Obj();
        v->setProperty ("id", "cabinet"); v->setProperty ("box", rect (boxOf (*cabinetLabel)));
        v->setProperty ("format", "the playing cabinet's name, the TOP 12's number left out: 4x12 SELFISH GENE; OFF while the cabinet is out");
        world.setProperty ("readings", list ({ juce::var (v) }));
        world.setProperty ("readingPx", stage::px13);
    }

    kit.setProperty ("window", rect (layout::view));
    kit.setProperty ("stills", list ({ juce::var ("still.webp") }));

    // THE POSTERS: the van as it opens, and one for each place of the lever — PARK, CLEAN, DIRTY — through the glass the
    // view's first frame there.
    // The lever stood at once where the van opens: when the window first comes up it is still easing there from PARK, and
    // the top poster would catch it half way.
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
        view->firstFrame (makeDrive(), juce::roundToInt (box.getWidth()), juce::roundToInt (box.getHeight()),
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
    webPoses.live = "the view's first frame for the place: the Mars plains (redpaw::PlainsDrive), the stand-in until the California "
                    "world is ported, on the GPU from its start, at the place's gear (PARK parked), settled, nothing stepped (t = 0); "
                    "the top poster is the place the app opens at";
    webPoses.still = [this] (int) { return webStill; };
    webPoses.window = layout::view;
    webPoses.hidden = { view.get() };
}

//==============================================================================
/** What the window shows — the view on the GPU, the 1993 landscape on the CPU, or the still — and how often; and full
    screen. The machine's, kept with its settings. */
void CaliforniaEditor::addWindowSections (juce::PopupMenu& menu)
{
    const auto self = [] (Console& e) -> CaliforniaEditor& { return static_cast<CaliforniaEditor&> (e); };

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
    addChoice (menu, "Lit by the guitar", lit, [self, lit] (Console& e) { prefs::setViewLitByGuitar (self (e).amp.product(), ! lit); });
}

void CaliforniaEditor::showWindowMenu()
{
    juce::PopupMenu menu;
    addWindowSections (menu);
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

void CaliforniaEditor::chooseLandscape (redpaw::Windscreen::Mode mode)
{
    prefs::store (amp.product()).set (landscapeKey, landscapeWords[(int) mode]);
    view->setMode (mode);
}

void CaliforniaEditor::chooseFrameRate (int fps)
{
    prefs::store (amp.product()).set (frameRateKey, fps);
    view->setFrameRateCap (fps);
}

/** Where the stage is in the editor's own pixels, for the GL thread. */
void CaliforniaEditor::placeView()
{
    const auto at = (layout::view * stageScale() + stageOrigin()).toNearestInt();
    glassX.store (at.getX());
    glassY.store (at.getY());
    glassW.store (at.getWidth());
    glassH.store (at.getHeight());
    gl.triggerRepaint();
}

void CaliforniaEditor::newOpenGLContextCreated()
{
    view->hostContextCreated();
}

void CaliforniaEditor::renderOpenGL()
{
    const float k = (float) gl.getRenderingScale();
    const int fw = juce::roundToInt ((float) getWidth() * k), fh = juce::roundToInt ((float) getHeight() * k);
    juce::OpenGLHelpers::clear (juce::Colours::black);

    const auto glass = juce::Rectangle<float> ((float) glassX.load(), (float) glassY.load(), (float) glassW.load(), (float) glassH.load()) * k;
    view->hostRender (glass.toNearestInt(), fw, fh);
}

void CaliforniaEditor::openGLContextClosing()
{
    view->hostContextClosing();
}

} // namespace orbitamp::california
