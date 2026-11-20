// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "MetalEditor.h"

#include "MetalFaceArt.h"
#include "MetalLayout.h"
#include "MetalLineup.h"

#include "../redpaw/view/PlainsDrive.h"
#include "../ui/FactoryMissing.h"
#include "../ui/Prefs.h"

#include <BinaryData.h>

namespace orbitamp::metal
{

juce::AudioProcessorEditor* makeEditor (AmpProcessor& p)
{
    // No knobs to draw without their renders, which lie in the factory folder: where they are missing, the window says
    // so, and where they belong (ui/FactoryMissing.h).
    if (! FaceArt::present())
        return new ui::FactoryMissingEditor (p, p.product());
    return new MetalEditor (p);
}

using namespace face;

namespace
{
    /** What the window shows, and how often — the machine's, kept with its settings: the landscape as a word, in the
        order of Windscreen::Mode, and what the menu calls each. The plains are a stand-in until the metal world comes. */
    const juce::Identifier landscapeKey { "landscape" };
    const juce::Identifier frameRateKey { "landscape_fps" };
    const juce::Identifier screenKey    { "screen" };
    const juce::StringArray landscapeWords { "plains", "1993", "still" };
    const juce::StringArray landscapeNames { "The plains (until the metal world)", "1993", "Still" };

    // Where the pointer is drawn on the knobs' renders — the rovers', whose renders these are.
    constexpr ArtKnob::Pointer drivePointer { 0.30f, 0.66f, 0.06f };
    constexpr ArtKnob::Pointer tonePointer  { 0.30f, 0.64f, 0.07f };

    /** THE HAND'S LIGHT: a soft orange round whatever the hand is over or on — a line, never a wash. */
    const juce::Colour handLight = orange.withAlpha (0.8f);

    /** WHAT THE FAMILY'S OWN WORDS ARE WRITTEN IN on this panel — PRESET, the version, MEM and CPU, the cabinet's name:
        the cabin's silver, a dark edge under it. Every name of a control is DC Rocker's (silverWords). */
    Ink cabinInk()
    {
        Ink ink;
        ink.words  = juce::Colour (0xffc4cad0);
        ink.values = juce::Colour (0xffd8dde1);
        ink.edge   = juce::Colour (0xaa000000);
        return ink;
    }

    juce::Rectangle<float> around (juce::Point<float> c, float w, float h)
    {
        return juce::Rectangle<float> (w, h).withCentre (c);
    }

    juce::String percent (float share) { return juce::String (juce::roundToInt (share * 100.0f)) + "%"; }
    juce::String dbWhole (float v) { return juce::String (juce::roundToInt (v)) + " dB"; }

    /** A pack's word as the panel writes it: its first letter a capital, the rest small — the mockup's "Bass", "Bass cut". */
    juce::String asWritten (const juce::String& word)
    {
        const auto w = word.trim().toLowerCase();
        return w.substring (0, 1).toUpperCase() + w.substring (1);
    }

    /** The world's gear for a channel: CLEAN GAIN LO the first, HI the second, both LEADs the third — LEAD GAIN HI with its
        TURBO's dust. */
    int gearOf (int channel) { return juce::jlimit (1, 3, channel + 1); }

    /** WHERE THE CONSOLE'S CONTROLS STAND on the dashboard (MetalLayout.h). */
    Console::Places cabinPlaces()
    {
        using namespace layout;
        using namespace stage;

        Console::Places p;
        p.drive = around (gain, gainKnob, gainKnob);

        // The tone strip: a row of five 72 apart in the strip, fewer spread to its ends, six shrunk to fit; each name in
        // the band along the strip's top.
        p.toneStrip.area = juce::Rectangle<float> (4.0f * tonePitch + toneSocket, toneSocket).withCentre ({ toneStripBox.getCentreX(), toneY });
        p.toneStrip.pitch = tonePitch;
        p.toneStrip.size = toneSocket;
        p.toneStrip.minScale = 0.8f;
        p.toneStrip.spread = true;
        p.toneKnob = toneKnob;
        p.toneNameY = bandY;
        p.toneNameEm = px13;
        p.toneSignDrop = 0.0f;

        // No filter keys; the key strip: the pack's switches, two 106 apart, each its name in the band over it.
        p.filterKeys = false;
        p.keyStrip.area = juce::Rectangle<float> (keyStripBox.getWidth() - 8.0f, keySize).withCentre ({ keyStripBox.getCentreX(), keyY });
        p.keyStrip.pitch = keyPitch;
        p.keyStrip.size = 60.0f;
        p.keyStrip.minScale = 0.6f;
        p.keySize = keySize;
        p.keyNameOverY = keyBandY;
        p.keyNameUnderY = keyBandY;
        p.keyNameRoom = 100.0f;

        // The six keys of the history and the registers, each cap in its well.
        for (size_t i = 0; i < p.holes.size(); ++i)
        {
            p.holes[i] = { keyX[i], keyRowY };
            const float w = i < (size_t) ovFirstRegister ? historyKeyWidth : registerKeyWidth;
            p.holeKeys[i] = around (p.holes[i], w - 2.0f * keyInset, keyHeight - 2.0f * keyInset);
        }
        p.holeKeySize = keyHeight - 2.0f * keyInset;

        // No REVERB, DELAY or CAB plates: REVERB is a fader, and CAB's box is the cabinet's screen.
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

        // The lamps, drawn by code: each overload under its meter, clear of the names over them; GATE's and LIMITER's at the
        // left end of their caps.
        p.lampDot = 7.0f;
        p.lamps[inClipLamp]  = around ({ inMeterSlot.getCentreX(), inMeterSlot.getBottom() + 8.0f }, 12.0f, 12.0f);
        p.lamps[outClipLamp] = around ({ outMeterSlot.getCentreX(), outMeterSlot.getBottom() + 8.0f }, 12.0f, 12.0f);
        p.lamps[gateLamp]    = around ({ gateButton.getX() + 11.0f, gateButton.getCentreY() }, 12.0f, 12.0f);
        p.lamps[limiterLamp] = around ({ limiterButton.getX() + 11.0f, limiterButton.getCentreY() }, 12.0f, 12.0f);
        p.gateDoor = {};            // GATE and LIMITER are the face's own buttons (WordButton): the doors take no room
        p.limiterDoor = {};

        p.emblemFace = { seatRim.begin(), seatRim.end() };
        p.emblemMark = logoBox;

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
                 juce::Colours::transparentBlack };     // no lamp under the cat: it lights itself (CatLogo)
    }
}

//==============================================================================
juce::Rectangle<int> Cabin::historyArea() const
{
    using namespace layout;
    return juce::Rectangle<float> (keyX.front() - 46.0f, keyRowY - 22.0f, keyX.back() - keyX.front() + 92.0f, 44.0f).toNearestInt();
}

void Cabin::paintStatic (juce::Graphics& g, float) const
{
    using namespace layout;

    // The night plate, unless the world is drawing it — and always in a picture taken.
    const bool worldHasIt = onScreen && worldDrawsPanel != nullptr && worldDrawsPanel();
    if (const auto& bg = FaceArt::of().bg; bg.isValid() && ! worldHasIt)
        g.drawImage (bg, juce::Rectangle<float> (0.0f, 0.0f, width, height));

    // THE CONTROLS' NAMES, each in its band (labels.txt), as the mockup writes them; the tones' and the keys' are the playing
    // channel's (paintStrips), GATE's and LIMITER's on their caps.
    const float h = stage::px13;
    silverWords (g, "In",     { inBandX, bandY },     h, 72.0f);
    silverWords (g, "Comp",   { compBandX, bandY },   h, 64.0f);
    silverWords (g, "Gain",   { gainBandX, bandY },   h, 112.0f);
    silverWords (g, "Fx 1",   { fx1BandX, bandY },    h, 56.0f);
    silverWords (g, "Fx 2",   { fx2BandX, bandY },    h, 56.0f);
    silverWords (g, "Reverb", { reverbBandX, bandY }, h, 58.0f);
    silverWords (g, "Out",    { outBandX, bandY },    h, 72.0f);
}

/** THE PLAYING CHANNEL'S NAMES: its tones' over their knobs, its keys' over its keys, and a titled switch's name once
    over its keys. */
void Cabin::paintStrips (juce::Graphics& g, float) const
{
    const float h = stage::px13;
    for (const auto& t : strips.tones)
        silverWords (g, asWritten (t.name), t.label.getCentre(), h, t.label.getWidth());
    for (size_t i = (size_t) strips.firstRightKey; i < strips.keys.size(); ++i)
        silverWords (g, asWritten (strips.keys[i].name), strips.keys[i].label.getCentre(), h, strips.keys[i].label.getWidth());
    for (const auto& title : strips.keyTitles)
        silverWords (g, asWritten (title.name), title.label.getCentre(), h, title.label.getWidth());
}

//==============================================================================
MetalEditor::MetalEditor (AmpProcessor& p)
    : face::Console (p, std::make_unique<Cabin>(), cabinPlaces(), cabinLook()),
      view (std::make_unique<redpaw::Windscreen> (makeDrive())),
      comp (FaceArt::of().faderCap), fx1 (FaceArt::of().faderCap), fx2 (FaceArt::of().faderCap), reverb (FaceArt::of().faderCap),
      squeeze ([&p] { return p.compGrDb.load (std::memory_order_relaxed); }),
      lever ({ layout::leverStops.begin(), layout::leverStops.end() }, layout::leverSlot.getCentreX(), layout::glyphLeft,
             layout::leverBox.getPosition()),
      park (p.bypassParameter(), [this] (float v) { showPark (v > 0.5f); }, nullptr),
      cabinet (cabinInk(), FaceArt::of().faderCap, layout::faderCapLength, layout::faderCapSquash),
      screen (p),
      logo (layout::flameBox, layout::logoBox, { layout::seatRim.begin(), layout::seatRim.end() },
            { layout::seatInside.begin(), layout::seatInside.end() })
{
    // Everything this constructor sets is the face settling onto the state, not a player's edit.
    const felitronics::appkit::CompareHistory::ScopedSuppress settling (amp.history);

    // THE SIX KEYS: dark caps with a chrome rim (the mockup) — UNDO and REDO the arrows, their names under the hand; A B C D
    // DC Rocker's letters, the register that plays its rim orange.
    for (int i = 0; i < 6; ++i)
        roofKey (i).drawKey = [this, &k = roofKey (i), i] (juce::Graphics& g, juce::Rectangle<float> box, bool down, float)
        {
            const bool history = i < ovFirstRegister;
            const bool lit = ! history && k.isOn();
            const bool live = ! history || (i == ovUndo ? amp.history.canUndo() : amp.history.canRedo());
            const bool held = history && down;
            const auto cap = box.reduced (0.7f);
            drawCap (g, cap, held, lit);
            const auto middle = cap.getCentre().translated (0.0f, held ? 1.0f : 0.0f);
            if (history)
                drawHistoryArrow (g, middle.translated (0.0f, -1.0f), i == ovRedo, live ? 1.0f : 0.4f);
            else
                silverWords (g, k.name, middle, rockerHeightOfEm (19.0f));
            if (k.isMouseOverOrDragging() && live)
            {
                juce::Path rim;
                rim.addRoundedRectangle (cap.expanded (1.0f), 6.0f);
                drawHandRim (g, rim, handLight);
            }
        };
    for (const int i : { (int) ovUndo, (int) ovRedo })
        roofKey (i).onHover = [this, i] (bool over)
        {
            const auto at = roofKey (i).getBounds().toFloat();
            nameTag.show ({ at.getCentreX(), at.getY() - 16.0f }, over ? juce::String (i == ovUndo ? "UNDO" : "REDO") : juce::String());
        };

    // THE KEY STRIP'S KEYS, code-drawn: a cap, a lamp in it crimson as lit as the key is.
    for (int b = btFirstRight; b < numKeys; ++b)
        key (b).drawKey = [&k = key (b)] (juce::Graphics& g, juce::Rectangle<float> box, bool down, float lit)
        {
            const auto cap = box.reduced (1.0f);
            drawCap (g, cap, down, false, k.isEnabled());
            const auto lamp = juce::Rectangle<float> (7.0f, 7.0f).withCentre (cap.getCentre().translated (0.0f, down ? 1.0f : 0.0f));
            g.setColour (juce::Colour (0xff3a0a10));
            g.fillEllipse (lamp);
            if (lit > 0.01f)
            {
                juce::Path dot;
                dot.addEllipse (lamp);
                glow (g, dot, juce::Colour (0xffdc143c).withAlpha (0.7f * lit), 5.0f);
                g.setColour (juce::Colour (0xffff3b3b).withAlpha (lit));
                g.fillEllipse (lamp);
            }
            if (k.isMouseOverOrDragging() && k.isEnabled())
            {
                juce::Path rim;
                rim.addRoundedRectangle (cap.expanded (1.0f), 6.0f);
                drawHandRim (g, rim, handLight);
            }
        };

    // THE FADERS: COMP's threshold, FX 1 and REVERB upright in their slots, FX 2 dark; each says where it stands in the tag
    // over it while it is held.
    const auto faderIn = [this] (face::ColumnFader& fader, juce::Rectangle<float> slot, const char* id)
    {
        fader.capLength = layout::faderCapLength;
        fader.capSquash = layout::faderCapSquash;
        fader.handLight = handLight;
        fader.marks = 6;
        fader.longMark = -1;
        addToStage (fader);
        fader.setBounds (slot.withSizeKeepingCentre (layout::faderWidth, slot.getHeight()).toNearestInt());
        fader.setComponentID (id);
    };
    faderIn (comp, layout::compSlot, "comp");
    faderIn (fx1, layout::fx1Slot, "fx1");
    faderIn (fx2, layout::fx2Slot, "fx2");
    faderIn (reverb, layout::reverbSlot, "reverb");
    addToStage (squeeze);
    squeeze.setBounds (layout::compGrSlot.reduced (2.0f, 4.0f).toNearestInt());   // inside the plate's slot, as the meters are
    squeeze.setComponentID ("squeeze");

    bindTravel (comp, params::compThreshold, [] (float db) { return db / params::compThresholdMin; },
                [] (float t) { return t * params::compThresholdMin; });
    sayLeverAs (comp, "COMP", [this] { return dbWhole (getParam (params::compThreshold)); });
    bindTravel (fx1, params::delayMix, [] (float v) { return v / 100.0f; }, [] (float t) { return t * 100.0f; });
    sayLeverAs (fx1, "FX 1  DELAY", [this] { return percent (getParam (params::delayMix) / 100.0f); });
    bindTravel (reverb, params::reverbMix, [] (float v) { return v / 100.0f; }, [] (float t) { return t * 100.0f; });
    sayLeverAs (reverb, "REVERB", [this] { return percent (getParam (params::reverbMix) / 100.0f); });

    // FX 2 HAS NOTHING TO PLAY YET: its fader dark at its foot, out of the hand's reach, and its name under the hand.
    fx2.setValue (0.0f, false);
    fx2.setEnabled (false);
    fx2.setInterceptsMouseClicks (false, false);
    fx2.setAlpha (0.35f);
    addToStage (fx2Spot);
    fx2Spot.setBounds (fx2.getBounds());
    fx2Spot.onHover = [this] (bool over)
    {
        nameTag.show ({ layout::fx2Slot.getCentreX(), layout::fx2Slot.getY() - 14.0f }, over ? juce::String ("FX 2") : juce::String());
    };

    // THE LEVER: PARK over the four channels. PARK takes the whole amp out (the amp's bypass) and leaves the channel as it
    // was; a channel brings the amp back in, on that channel. The hand over a stop names it.
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
    lever.onHover = [this] (int stop)
    {
        const auto at = lever.getBoundsInParent().toFloat().getPosition() + lever.glyphAt (juce::jmax (0, stop));
        nameTag.show ({ at.x + 24.0f, at.y - 20.0f }, stop >= 0 ? placeName (stop) : juce::String());
    };
    lever.setComponentID ("lever");

    // ...and the stop it lands on, on the tuner's screen for a moment, the tuner stepping aside.
    addToStage (note);
    note.setVisible (false);
    note.setBounds (layout::tunerScreen.toNearestInt());
    note.onShow = [this] (bool up) { tuner.setAlpha (up ? 0.0f : 1.0f); };
    note.setComponentID ("note");

    // GATE AND LIMITER: just over the plate, under the console's lamps, which light at their left ends.
    for (auto* button : { &gateButton, &limiterButton })
    {
        button->handLight = handLight;
        stage.addAndMakeVisible (*button, stage.getIndexOfChildComponent (picture.get()) + 1);
    }
    gateButton.setBounds (layout::gateButton.toNearestInt());
    gateButton.onClick = [this] { showGateMenu(); };
    gateButton.setComponentID ("gate");
    limiterButton.setBounds (layout::limiterButton.toNearestInt());
    limiterButton.onClick = [this] { showOutputMenu(); };
    limiterButton.setComponentID ("limiter");

    // THE CABINET'S SLIDER in its screen: a notch a cabinet of the TOP 12, as the bathyscaphe's of the bass's dozen; over the
    // picture under it the cabinet that plays by its name — the slot's under the hand while it moves the slider, OFF while
    // the cabinet is out of the sound. A right click on it lists them (Console::showDrawnCabs).
    cabinet.name = [this]
    {
        auto words = dragAt >= 0 ? channels.slotName (dragAt)
                                 : ! channels.cabinetOn() ? juce::String ("OFF") : channels.slotName (channels.cabinetSlot());
        if (const auto head = words.upToFirstOccurrenceOf (": ", false, false); words.contains (": ") && head.isNotEmpty() && head.containsOnly ("0123456789"))
            words = words.fromFirstOccurrenceOf (": ", false, false);
        return "CAB: " + words;
    };
    cabinet.notches = channels.cabinetSlots();
    cabinet.notch   = [this] { return channels.cabinetSlot(); };
    cabinet.onMove  = [this] (int at) { hearSlot (at); };
    cabinet.onLetGo = [this] (int at) { chooseSlot (at); };
    cabinet.onMenu  = [this] { showDrawnCabs(); };
    cabinet.handLight = handLight;
    addToStage (cabinet);
    cabinet.setBounds (layout::cabinetControl.toNearestInt());
    cabinet.setComponentID ("cabinet");
    cabinet.label = &readout (layout::cabinetName);
    cabinet.refresh();

    // ...and the amp at work under its name: the view the machine chose, SPECTRUM until then (a right click picks another).
    addToStage (screen);
    screen.setBounds (layout::monitorBand.toNearestInt());
    screen.setBand (screen.getLocalBounds().toFloat());
    screen.show ((Monitor::View) juce::jlimit (1, (int) Monitor::View::numViews - 1,
                                               (int) prefs::store (amp.product()).get (screenKey, (int) Monitor::View::spectrum)));
    screen.setComponentID ("screen");

    // LEARN — from GATE's menu, or a double click on its lamp — takes the tuner's glass while it runs.
    addToStage (learnGlass);
    learnGlass.setBounds (layout::tunerScreen.toNearestInt());
    learnGlass.inset = { 10.0f, 4.0f };
    learnGlass.onShow = [this] (bool up) { tuner.setAlpha (up ? 0.0f : 1.0f); };

    // THE CAT, live over the maker's plate: silver, and crimson in flames while the hand is over the seat.
    addToStage (logo);
    logo.setBounds (layout::flameBox.toNearestInt());
    logo.watch (stage);
    logo.setComponentID ("logo");

    // THE WORLD, under the whole stage: the plate's alpha is its window.
    stage.addAndMakeVisible (*view, 0);
    view->setBounds (layout::view.toNearestInt());
    view->addMouseListener (&glassClicks, false);
    glassClicks.menu = [this] { showWindowMenu(); };
    // ...the plate the world's while it is drawing it — and not while the view is hidden for a picture taken of the face.
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

MetalEditor::~MetalEditor()
{
    view->removeMouseListener (&glassClicks);
    gl.detach();
}

/** THE WORLD BEHIND THE GLASS. Until the metal world's port comes (feat/metal-world), the Mars plains stand in: three
    gears, no cabin of their own, so the face paints the night plate flat. */
std::unique_ptr<redpaw::Drive> MetalEditor::makeDrive()
{
    // MetalDrive goes here, its world drawing the cabin with the lightning's flash plate (Drive::drawsCabin).
    return std::make_unique<redpaw::PlainsDrive> (3);
}

redgroove::Readout& MetalEditor::readout (juce::Rectangle<float> box)
{
    auto& r = *readouts.emplace_back (std::make_unique<redgroove::Readout> (cabinInk(), stage::px13));
    r.colour = cabinInk().values;
    addToStage (r);
    r.setBounds (box.toNearestInt());
    return r;
}

template <typename Control>
void MetalEditor::bindTravel (Control& control, const juce::String& paramId, std::function<float (float)> toTravel,
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

void MetalEditor::hearSlot (int at)
{
    dragAt = at;
    channels.hearSlot (at);
}

void MetalEditor::chooseSlot (int at)
{
    if (dragAt >= 0)
    {
        channels.chooseSlot (at);
        channels.setCabinetOn (true);
    }
    dragAt = -1;
}

juce::String MetalEditor::placeName (int place) const
{
    if (place <= 0)
        return "PARK";
    return juce::String (channels.lineup().channels[(size_t) juce::jlimit (0, channels.count() - 1, place - 1)].name).toUpperCase();
}

/** The lever goes where the state is — PARK while the amp is out, else the channel — unless the hand holds it; the world
    in the channel's gear. */
void MetalEditor::showSelector (int channel)
{
    const int place = parked ? 0 : channel + 1;
    if (! lever.isHeld() && lever.getPlace() != place)
        lever.setPlace (place, true, false);
    lever.repaint();
    cabinet.refresh();
    view->setGear (gearOf (channel));
    view->setTurbo (channel == 3);
    showPlace();
}

/** PARK: the amp's bypass, from the lever, a host's bypass or anywhere — the lever to it, the world parked. */
void MetalEditor::showPark (bool on)
{
    parked = on;
    view->setPark (on);
    const int place = on ? 0 : channels.active() + 1;
    if (! lever.isHeld() && lever.getPlace() != place)
        lever.setPlace (place, true, false);
    lever.repaint();
    showPlace();
}

/** The stop the amp has come to, named on the tuner's screen for a moment — whoever moved it. */
void MetalEditor::showPlace()
{
    const int place = parked ? 0 : channels.active() + 1;
    if (placeShown >= 0 && place != placeShown)
        note.show (placeName (place));
    placeShown = place;
}

/** The number keys pull the lever: 1 is CLEAN GAIN LO, 4 LEAD GAIN HI. */
bool MetalEditor::channelKey (const juce::KeyPress& key)
{
    const auto c = (juce::juce_wchar) key.getKeyCode();
    const int at = c >= '1' && (int) c < '1' + channels.count() ? (int) (c - '1') : -1;
    if (at < 0)
        return false;
    if (! lever.isHeld())
        lever.setPlace (at + 1, true);
    return true;
}

/** A right click on the cabinet's picture: the views of the amp at work, the one up ticked. */
bool MetalEditor::pictureRightClicked (juce::Point<int> onStage)
{
    if (! layout::cabinetPicture.contains (onStage.toFloat()))
        return false;
    juce::PopupMenu menu;
    menu.addSectionHeader ("PICTURE");
    for (int i = (int) Monitor::View::spectrum; i < (int) Monitor::View::numViews; ++i)
        addChoice (menu, Monitor::names()[i], (int) screen.shown() == i, [i] (Console& e)
        {
            auto& self = static_cast<MetalEditor&> (e);
            self.screen.show ((Monitor::View) i);
            prefs::store (self.amp.product()).set (screenKey, i);
        });
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
    return true;
}

//==============================================================================
/** What the window shows — the world on the GPU, the 1993 landscape on the CPU, or the still — and how often; whether it
    answers the guitar; and full screen. The machine's, kept with its settings. */
void MetalEditor::addWindowSections (juce::PopupMenu& menu)
{
    const auto self = [] (Console& e) -> MetalEditor& { return static_cast<MetalEditor&> (e); };

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

void MetalEditor::showWindowMenu()
{
    juce::PopupMenu menu;
    addWindowSections (menu);
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

void MetalEditor::chooseLandscape (redpaw::Windscreen::Mode mode)
{
    prefs::store (amp.product()).set (landscapeKey, landscapeWords[(int) mode]);
    view->setMode (mode);
}

void MetalEditor::chooseFrameRate (int fps)
{
    prefs::store (amp.product()).set (frameRateKey, fps);
    view->setFrameRateCap (fps);
}

/** Where the stage is in the editor's own pixels, for the GL thread. */
void MetalEditor::placeView()
{
    const auto at = (layout::view * stageScale() + stageOrigin()).toNearestInt();
    glassX.store (at.getX());
    glassY.store (at.getY());
    glassW.store (at.getWidth());
    glassH.store (at.getHeight());
    gl.triggerRepaint();
}

void MetalEditor::newOpenGLContextCreated()
{
    view->hostContextCreated();
}

void MetalEditor::renderOpenGL()
{
    const float k = (float) gl.getRenderingScale();
    const int fw = juce::roundToInt ((float) getWidth() * k), fh = juce::roundToInt ((float) getHeight() * k);
    juce::OpenGLHelpers::clear (juce::Colours::black);

    const auto glass = juce::Rectangle<float> ((float) glassX.load(), (float) glassY.load(), (float) glassW.load(), (float) glassH.load()) * k;
    view->hostRender (glass.toNearestInt(), fw, fh);
}

void MetalEditor::openGLContextClosing()
{
    view->hostContextClosing();
}

} // namespace orbitamp::metal
