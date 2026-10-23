// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "Console.h"

#include <algorithm>
#include <utility>

#include "Cabs.h"
#include "Midi.h"
#include "Standalone.h"

#include "../Parameters.h"
#include "../Product.h"
#include "../ProductPage.h"
#include "../core/ProcessMemory.h"
#include "../device/IrLibrary.h"
#include "../device/SwitchWords.h"
#include "../ui/AboutConfig.h"
#include "../ui/HumMenu.h"
#include "../ui/HumTuning.h"
#include "../ui/LoadBreakdown.h"
#include "../ui/Prefs.h"

#include <felitronics/appkit/TextPrompt.h>

namespace orbitamp::face
{

namespace
{
    juce::String decibels (int db) { return (db > 0 ? "+" : "") + juce::String (db) + " dB"; }

    // The ceilings the OUTPUT column's menu offers the limiter, within what the engine takes
    // (params::limiterCeilingMin .. Max).
    constexpr float limiterCeilings[] { -0.1f, -0.3f, -0.5f, -1.0f, -2.0f, -3.0f };

    // How much heavier than the ink's weight GATE and LIMITER are cut, on their busy corner plates.
    constexpr float cornerWeight = 1.6f;

    // TURBO's push — see Console.h. Kept with the machine's settings, where the engine reads it too
    // (channels/Engine.h).
    using channels::settings::turboKey, channels::settings::turboPushes, channels::settings::snapTurbo;

    // How DRIVE goes between the pack's captures — see Console.h. Kept with the machine's settings.
    const juce::Identifier driveStepsKey { "drive_steps" };
    const juce::StringArray driveWays { "Smoothly", "In steps" };

    // WHAT A PRODUCT HANDS OUT SAYS WHERE IT CAME FROM: a capture and a cabinet saved from it are
    // offered a name that starts with the product's own and with what kind of file it is —
    // "CatpitAmp-NamA2-", "CatpitAmp-IR-".
    juce::String captureNamePrefix (const Product& p) { return juce::String (p.name) + "-NamA2-"; }
    juce::String cabNamePrefix (const Product& p)     { return juce::String (p.name) + "-IR-"; }

    /** ...and a capture's name ends with where the gear's gain knob stood for it, as the hour it
        pointed at: "-1h", "-9h30". Nothing when the pack does not say. */
    juce::String hourTag (double hour)
    {
        if (hour <= 0.0)
            return {};

        const int minutes = juce::roundToInt (hour * 60.0);
        const int h = (minutes / 60 + 11) % 12 + 1, m = minutes % 60;
        return "-" + juce::String (h) + "h" + (m > 0 ? juce::String (m).paddedLeft ('0', 2) : juce::String());
    }

    // Where a capture was last saved to (saveCapture): the next one is offered the same folder.
    const juce::Identifier captureFolderKey { "capture_folder" };

    // ...and a cabinet (saveCab), which goes where cabinets are kept, not where captures are.
    const juce::Identifier cabFolderKey { "cab_folder" };

    void sayNotSaved (const juce::File& file)
    {
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::WarningIcon)
                                          .withTitle ("Not saved")
                                          .withMessage (file.getFullPathName())
                                          .withButton ("OK"),
                                      nullptr);
    }


    /** THE BUILD WINDOW, AS THE PRODUCT'S. What the build is and what it was built against is the same
        for every face of the processor (aboutConfig); what is the product's own is its name, its mark —
        the cockpit's a small starship head-on, its fins a cat's ears — and its name where the notice
        says whose product this is. The window outlives the badge that opens it, so the mark is
        shared, not lent. */
    felitronics::appkit::VersionBadge::Config aboutTheProduct (const Product& p, const juce::String& svg, bool standalone)
    {
        auto config = aboutConfig (p.name, productPageUrl (p.name, standalone), familyLinkTag (p.name));

        std::shared_ptr<juce::Drawable> mark { juce::Drawable::createFromImageData (
            svg.toRawUTF8(), svg.getNumBytesAsUTF8()).release() };

        config.drawMark = [mark] (juce::Graphics& g, float cx, float cy, float d)
        {
            if (mark != nullptr)
                mark->drawWithin (g, { cx - d * 0.5f, cy - d * 0.5f, d, d }, juce::RectanglePlacement::centred, 1.0f);
        };
        config.notice = config.notice.replace ("OrbitAmp", p.name);
        return config;
    }

    juce::Rectangle<int> around (juce::Point<float> c, float w, float h)
    {
        return juce::Rectangle<float> (w, h).withCentre (c).toNearestInt();
    }

    juce::Rectangle<int> box (juce::Rectangle<float> r) { return r.toNearestInt(); }
}

void drawTurnArrow (juce::Graphics& g, juce::Point<float> centre, float radius, bool back, juce::Colour colour)
{
    const float from = juce::degreesToRadians (-140.0f), to = juce::degreesToRadians (70.0f);
    juce::Path arc, head;
    arc.addCentredArc (0.0f, 0.0f, radius, radius, 0.0f, from, to, true);

    const juce::Point<float> end { radius * std::sin (to), -radius * std::cos (to) };
    const juce::Point<float> along { std::cos (to), std::sin (to) }, across { std::sin (to), -std::cos (to) };
    const float h = radius * 0.85f;
    head.addTriangle (end + along * h, end + across * (h * 0.62f), end - across * (h * 0.62f));

    const auto place = juce::AffineTransform::scale (back ? -1.0f : 1.0f, 1.0f).translated (centre);
    g.setColour (colour);
    g.strokePath (arc, juce::PathStrokeType (radius * 0.26f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), place);
    g.fillPath (head, place);
}

//==============================================================================
Console::Console (AmpProcessor& p, std::unique_ptr<Picture> pic, const Places& where, const Look& look)
    : juce::AudioProcessorEditor (&p), amp (p), engine (Engine::of (p)), channels (engine.channels), presets (p, channels),
      places (where),
      picture (std::move (pic)),
      midiCommands (look.midiCommands),
      ink (look.ink),
      drive (look.driveKnob, look.drivePointer),
      reverbName (stage::px13),
      cabName (stage::px13),
      delayWord (stage::px13),
      delayNote (stage::px13),
      delayTempo (stage::px13),
      inFader (look.faderCap),
      outFader (look.faderCap),
      inMeter ([this] { return amp.gateKeyDb.load (std::memory_order_relaxed); }),
      outMeter ([this] { return amp.outDb.load (std::memory_order_relaxed); }),
      inClip (*look.lampsLit[inClipLamp],   [this] { return amp.inClip.exchange (false) ? 1.0f : 0.0f; }, clipHoldMs, clipFadeMs),
      outClip (*look.lampsLit[outClipLamp], [this] { return amp.outClip.exchange (false) ? 1.0f : 0.0f; }, clipHoldMs, clipFadeMs),
      gateLight (*look.lampsLit[gateLamp],
                 [this] { return -amp.gateMeterDb.load (std::memory_order_relaxed) / 20.0f; }, 150.0, 350.0),       // full at 20 dB pressed
      limiterLight (*look.lampsLit[limiterLamp],
                    [this] { return -amp.limiterGrDb.load (std::memory_order_relaxed) / 3.0f; }, 300.0, 400.0),   // full at 3 dB squeezed
      gateDoor (stage::px13),
      limiterDoor (stage::px13),
      tuner (p.tunerEar),
      emblem (where.emblemFace, where.emblemMark),
      version (stage::px13),
      about (p.updateChecker(), aboutTheProduct (p.product(), look.mark, p.wrapperType == juce::AudioProcessor::wrapperType_Standalone), p.pluginFormat()),
      load (where.readingPx),
      memory (where.readingPx),
      presetDoor (stage::px13)
{
    using namespace stage;

    // Everything this constructor sets is the face settling onto the state, not a player's edit: with
    // undo on the panel it must not leave a step to press back through.
    const felitronics::appkit::CompareHistory::ScopedSuppress settling (amp.history);

    channels.setTurboDb ((float) channels::settings::turbo (amp.product()));
    driveSteps = (bool) prefs::store (amp.product()).get (driveStepsKey, false);

    channels.readCabinets();    // the player's folder may have gained or lost a file since the engine was built
    engine.setUpChain (channels.adopt());
    channels.align();   // the packs and cabinets are files, and the folders may have changed since the state was saved
    settleOnSettings(); // ...and TURBO's push and DRIVE's way may not be the settings' now

    // THE STANDALONE PLAYS IN STEREO SPACE — one guitar in, a wide room out. The processor's own default
    // waits for a device to open, so a first run with none saved MONO for good; and the first players'
    // states were saved before this. So once on each machine, over whatever the state says; after that
    // the choice is the player's.
    if (standalone::firstTimeHere ("stereoSpaceSet"))
        setParam (params::stereoMode, (float) params::StereoMode::stereoSpace);

    addAndMakeVisible (stage);
    stage.setBounds (0, 0, (int) width, (int) height);

    // The picture lies under every control; what shows through its window, the product puts under it.
    stage.addAndMakeVisible (*picture);
    picture->setBounds (stage.getLocalBounds());

    stage.addAndMakeVisible (drive);
    drive.setBounds (box (places.drive));
    // The tone knobs and the right group's keys: as many as the console can show, each put where the
    // playing channel's rows have it, and shown only while they have it (layStrips).
    for (size_t i = 0; i < tones.size(); ++i)
    {
        tones[i] = std::make_unique<ArtKnob> (look.toneKnob, look.tonePointer);
        stage.addChildComponent (*tones[i]);

        // ITS NAME, which its sign stands for: in the tag over it while the hand is on the knob or on the sign.
        toneSpots[i] = std::make_unique<HoverSpot>();
        stage.addChildComponent (*toneSpots[i]);
        const auto name = [this, i] (bool over)
        {
            const bool has = i < strips.tones.size();
            nameTag.show (has ? strips.tones[i].label.getCentre().translated (0.0f, -26.0f) : juce::Point<float>(),
                          over && has ? strips.tones[i].name : juce::String());
        };
        tones[i]->onHover = name;
        toneSpots[i]->onHover = name;
    }

    for (size_t i = 0; i < buttons.size(); ++i)
    {
        const bool filter = i < (size_t) btFirstRight;
        buttons[i] = std::make_unique<PushButton> (filter ? switches[i].name : "");
        auto& b = *buttons[i];
        stage.addChildComponent (b);
        if (filter)
        {
            b.setVisible (true);
            b.setBounds (around (places.keys[i], places.filterKeyWidth > 0.0f ? places.filterKeyWidth : places.keySize,
                                 places.filterKeyHeight > 0.0f ? places.filterKeyHeight : places.keySize));
            keyNames[i] = switches[i].name;
        }
        b.onHover = [this, &b, i] (bool over)     // a key that does not answer the hand says nothing either
        {
            const auto at = b.getBounds().toFloat().getCentre();
            nameTag.show (at.translated (0.0f, -40.0f), over && b.isEnabled() ? keyNames[i] + "  " + b.stateText() : juce::String());
        };
    }

    // UNDO and REDO, in the two holes left of the roof's divider: plain keys over the processor's own
    // history — the same one OrbitAmp's header drives.
    for (const size_t i : { (size_t) ovUndo, (size_t) ovRedo })
    {
        overhead[i] = std::make_unique<PushButton> (i == ovUndo ? "UNDO" : "REDO");
        auto& key = *overhead[i];
        key.latching = false;
        stage.addAndMakeVisible (key);
        key.setBounds (around (places.holes[i], places.holeKeySize, places.holeKeySize));
    }
    overhead[ovUndo]->onPress = [this] { endTapping(); amp.history.undo(); };
    overhead[ovRedo]->onPress = [this] { endTapping(); amp.history.redo(); };

    // A B C D, in the four holes right of it: a press plays that register, a right click copies it.
    for (int r = 0; r < numRegisterKeys; ++r)
    {
        const auto i = (size_t) (ovFirstRegister + r);
        overhead[i] = std::make_unique<PushButton> (juce::String::charToString ((juce::juce_wchar) ('A' + r)));
        auto& key = *overhead[i];
        key.latching = false;
        key.onPress  = [this, r] { endTapping(); amp.history.switchTo (r); };
        key.onMenu   = [this, r] { showRegisterMenu (r); };
        stage.addAndMakeVisible (key);
        key.setBounds (around (places.holes[i], places.holeKeySize, places.holeKeySize));
    }
    amp.history.onHistoryChanged = [this] { refreshHistory(); };
    refreshHistory();

    // A host moves the channel through CHANNEL, with no hand on anything: the engine makes the switch
    // (channels/Engine.h) and the face goes after it.
    engine.onChange = [this] { followEngine(); };

    const juce::Colour lineFrom[] { orange.darker (0.3f), violet, orange.darker (0.3f) };
    const juce::Colour lineTo[]   { orange,               orange, orange };
    for (size_t i = 0; i < sliders.size(); ++i)
    {
        // CAB has a position for every cabinet the product has; a selector needs two to be one.
        sliders[i] = std::make_unique<PlateSlider> (look.cap, lineFrom[i], lineTo[i],
                                                    i == 2 ? juce::jmax (2, channels.cabinets().size()) : 0);
        sliders[i]->capAngle  = places.sliderCapAngle;
        sliders[i]->capLength = places.sliderCapLength;
        sliders[i]->handLight = look.handLight;
        stage.addAndMakeVisible (*sliders[i]);
        sliders[i]->setBounds (box (places.sliders[i]));
    }

    // Every word on the panel that answers the hand is written in the face's ink.
    for (auto* legend : { &reverbName, &cabName, &delayWord, &delayNote, &delayTempo, &gateDoor, &limiterDoor,
                          &version, &load, &memory, &presetDoor })
    {
        legend->nameColour  = ink.words;
        legend->valueColour = ink.values;
        legend->edgeColour  = ink.edge;
        legend->shadeColour = ink.shade;
        legend->weight      = ink.weight;
    }
    // GATE and LIMITER stand on the corner plates, where the panel is busiest: cut deeper than the rest.
    gateDoor.weight = limiterDoor.weight = ink.weight * cornerWeight;

    // REVERB's name carries the reverb's character, and a click on it opens the list — the same six
    // OrbitAmp's reverb block offers.
    stage.addAndMakeVisible (reverbName);
    reverbName.setBounds (box (places.sliderNames[0]));
    reverbName.onClick = [this] { showReverbMenu(); };
    reverbType = follow (params::reverbType, [this] (float v)
    {
        reverbName.set ("REVERB", params::reverbCharacters[juce::jlimit (0, params::reverbCharacters.size() - 1, juce::roundToInt (v))]);
    });
    reverbType->sendInitialUpdate();

    // The delay always keeps time, so its plate reads a beat: DELAY, a note value, a tempo. The word is
    // tapped to give the tempo by hand; the note value opens the list of them; and the tempo — the
    // delay's own when it stands alone, since with a host the host conducts — is dragged up or down,
    // or rolled under the wheel, like a knob.
    for (auto* legend : { &delayWord, &delayNote, &delayTempo })
        stage.addAndMakeVisible (legend);
    delayWord.nameColour = ink.state;
    delayWord.onClick = [this] { tapTempo(); };
    // Nothing on the plate says the word is a key, so the pointer is told: what a tap on it does —
    // or, with a host conducting, why it does nothing.
    delayWord.onHover = [this] (bool over)
    {
        const auto word = delayWord.getBounds().toFloat();
        nameTag.show ({ word.getCentreX(), word.getY() - 22.0f },
                      ! over ? juce::String() : tempoIsOwn ? "TAP TEMPO" : "HOST TEMPO");
    };
    delayNote.onClick = [this] { showDelayMenu(); };
    tapPause.then = [this] { endTapping(); };
    delayTempo.setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    delayTempo.onDragStart = [this] { delayBpm->beginGesture(); };
    delayTempo.onDragEnd   = [this] { delayBpm->endGesture(); };
    delayTempo.onDrag = [this] (float pixels)
    {
        tempoTravel += pixels / 3.0f;                       // three pixels to the beat-per-minute
        const int whole = (int) tempoTravel;
        if (whole == 0)
            return;
        tempoTravel -= (float) whole;
        delayBpm->setValueAsPartOfGesture (juce::jlimit (params::delayBpmMin, params::delayBpmMax,
                                                         std::round (getParam (params::delayBpm)) + (float) whole));
    };
    // ...and a double click on it is the tempo it starts at, as a double click on a knob is its middle.
    delayTempo.onDoubleClick = [this]
    {
        endTapping();
        delayBpm->setValueAsCompleteGesture (params::delayBpmDefault);
    };
    delayDiv = follow (params::delayDiv, [this] (float) { showDelayTime(); });
    delayBpm = follow (params::delayBpm, [this] (float) { showDelayTime(); });
    showDelayTime();

    // CAB steps through the product's own cabinets, and its plate reads the one it sits on, the way
    // REVERB's reads its character. With fewer than two there is nothing to choose between, and it
    // stands dim.
    cabName.setInterceptsMouseClicks (false, false);      // a reading: the slider under it is the control
    stage.addAndMakeVisible (cabName);
    cabName.setBounds (box (places.sliderNames[2]));

    sliders[2]->onMenu = [this] { showCabs(); };
    cabinetsChanged();

    for (auto* c : std::initializer_list<juce::Component*> { &inFader, &outFader, &inMeter, &outMeter, &tuner })
        stage.addAndMakeVisible (c);
    tuner.setBounds (box (places.tuner));
    tuner.setStrip (places.tunerStrip);
    tuner.setDial (places.tunerDial);
    inFader.capLength = outFader.capLength = places.faderCapLength;
    inFader.handLight = outFader.handLight = look.handLight;
    inFader.setBounds (box (places.inFader));
    outFader.setBounds (box (places.outFader));
    inMeter.setBounds (box (places.inMeter));
    outMeter.setBounds (box (places.outMeter));
    inMeter.setDots (places.meterDots);
    outMeter.setDots (places.meterDots);

    for (auto* lamp : { &inClip, &outClip, &gateLight, &limiterLight })
        stage.addAndMakeVisible (lamp);
    inClip.setBounds (box (places.lamps[inClipLamp]));
    outClip.setBounds (box (places.lamps[outClipLamp]));
    gateLight.setBounds (box (places.lamps[gateLamp]));
    limiterLight.setBounds (box (places.lamps[limiterLamp]));

    // Where the picture paints no lamp lit, each is drawn: the overloads in the meters' red, GATE and
    // LIMITER in the family's orange.
    for (auto* lamp : { &inClip, &outClip, &gateLight, &limiterLight })
        lamp->dot = places.lampDot;
    inClip.light = outClip.light = juce::Colour (0xffff4646);
    gateLight.light = limiterLight.light = orange;

    // What has an obvious home in the engine is wired to it; the rest only moves for now.
    bind (drive, params::preampGain);
    drive.onMenu = [this] { showDriveMenu(); };
    drive.detent = [this] (float travel) { return driveSteps ? (float) amp.preamp.stepAt ((double) travel) : travel; };
    bind (*sliders[0], params::reverbMix);
    bind (*sliders[1], params::delayMix);
    bind (outFader, params::outTrim);

    // INPUT's lever IS the trim, as OUTPUT's is: unity in its middle (Console.h).
    bind (inFader, params::inTrim);

    inFader.onMenu = [this] { showInputMenu(); };   // ...the face's own menu while HUM is off (showInputMenu)

    outFader.onMenu = [this] { showOutputMenu(); };

    // Whether the cabinet is in the sound at all is on CAB's plate.
    follow (params::cabOn, [this] (float) { showCab(); });

    // The tuner's two switches are read here and thrown from its menu (showTunerMenu); on and off, also
    // by a click on its screen (mouseDown).
    follow (params::tunerOn,   [this] (float) { showTunerState(); });
    follow (params::tunerMute, [this] (float) { showTunerState(); });
    showTunerState();

    sayLever (inFader,  "INPUT",  [this] { return getParam (params::inTrim); });
    sayLever (outFader, "OUTPUT", [this] { return getParam (params::outTrim); });

    // The tone stack is the playing pack's own — its Bass, Middle and Treble as they were measured on
    // the amp, centre detent where every capture was taken.
    bindTones();

    // HPF and LPF switch the cabinet's own cut filters in and out. The keys right of them are the playing
    // pack's switches a hand may throw, as many as it has (rightKeysOf), and the face's own after them;
    // a pack with none has none there. Which they are is showKeys' to say, on every channel.
    bindButton (*buttons[btHpf], params::cabHpfOn);
    bindButton (*buttons[btLpf], params::cabLpfOn);
    for (int key = btFirstRight; key < numKeys; ++key)
        buttons[(size_t) key]->onChange = [this, key] (bool on) { keyThrown (key, on); };

    // A switch moved by anything — a key, an undo, a register, a host — lights the key it stands on.
    for (int s = 0; s < params::numSelectors; ++s)
        follow (params::selectorId (params::preampId, s).toRawUTF8(), [this] (float) { showKeys(); });

    showChannel();

    // The mark lights under the pointer and leads to the product's own page on the maker's site.
    stage.addAndMakeVisible (emblem);
    {
        juce::Path face;
        for (size_t i = 0; i < places.emblemFace.size(); ++i)
            i == 0 ? face.startNewSubPath (places.emblemFace[i]) : face.lineTo (places.emblemFace[i]);
        emblem.setBounds (face.getBounds().expanded (Emblem::bloom + 1.0f).getSmallestIntegerContainer());     // room for the lamp's glow; the target is the plate alone
    }
    emblem.setLamp (look.emblemLamp);
    emblem.onClick = [page = productPageUrl (amp.product().name, amp.wrapperType == juce::AudioProcessor::wrapperType_Standalone)]
    {
        juce::URL (page).launchInDefaultBrowser();
    };

    // The version, and behind it the window with everything else about the build. That window is
    // hung on THIS editor rather than on the top-level window: a GL surface lies over everything in
    // the window that is not this editor's own child, and a dialog on the window would open under it.
    about.setBrandTypeface (theme::displayTypeface());
    addChildComponent (about);
    version.upper = false;
    version.valueColour = ink.at (ink.words, 0.85f);
    version.set ({}, "v" + amp.updateChecker().currentVersion());
    version.onClick = [this] { about.showAbout (this); };
    stage.addAndMakeVisible (version);
    version.setBounds (box (places.version));

    // CPU. A click opens what the number is made of — the window OrbitAmp's footer opens from its DSP
    // figure.
    load.nameColour = ink.at (ink.words, 0.85f);
    load.fromLeft = true;
    load.fit = places.readingsFit;
    load.onClick = [this] { showLoad(); };
    stage.addAndMakeVisible (load);
    load.setBounds (box (places.load));

    // MEM. A reading and nothing else: a click there is a click on the panel.
    memory.nameColour = memory.valueColour = ink.at (ink.words, 0.85f);
    memory.fromLeft = true;
    memory.fit = places.readingsFit;
    memory.setInterceptsMouseClicks (false, false);
    stage.addChildComponent (memory);
    memory.setBounds (box (places.memory));

    // PRESET.
    presetDoor.valueColour = ink.at (ink.words, 0.85f);
    presetDoor.set ({}, "PRESET");
    presetDoor.onClick = [this] { showPresetMenu(); };
    stage.addAndMakeVisible (presetDoor);
    presetDoor.setBounds (box (places.preset));

    // GATE and LIMITER, over their lamps.
    for (auto* door : { &gateDoor, &limiterDoor })
    {
        door->valueColour = ink.at (ink.words, 0.85f);
        stage.addAndMakeVisible (door);
    }
    gateDoor.set ({}, "GATE");
    gateDoor.onClick = [this] { showGateMenu(); };
    gateDoor.setBounds (box (places.gateDoor));
    if (places.gateDoorAngle != 0.0f)
        gateDoor.setTransform (juce::AffineTransform::rotation (places.gateDoorAngle, places.gateDoor.getCentreX(), places.gateDoor.getCentreY()));
    limiterDoor.set ({}, "LIMITER");
    limiterDoor.onClick = [this] { showOutputMenu(); };
    limiterDoor.setBounds (box (places.limiterDoor));
    if (places.limiterDoorAngle != 0.0f)
        limiterDoor.setTransform (juce::AffineTransform::rotation (places.limiterDoorAngle, places.limiterDoor.getCentreX(), places.limiterDoor.getCentreY()));

    timerCallback();
    startTimerHz (4);   // the dots are a lookup and the load is a smoothed figure: nobody needs either sooner

    midiPulse.beat = [this] { playMidi(); };
    midiPulse.startTimerHz (100);

    // A right click on the bare panel is the door to the settings; the panel is the stage itself,
    // since the picture over it lets every click through.
    stage.addMouseListener (this, false);

    // The keys. A click anywhere on the face brings the focus here, since nothing on it wants the
    // keyboard for itself; and the face takes it once when it first comes up, so the keys answer
    // before the first click too.
    setWantsKeyboardFocus (true);
    juce::Timer::callAfterDelay (300, [safe = juce::Component::SafePointer<Console> (this)]
    {
        if (safe != nullptr && safe->isShowing())
            safe->grabKeyboardFocus();
    });

    // The name tag goes on last, so that it lies over every control it may be shown beside.
    stage.addChildComponent (nameTag);

    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) width / (double) height);
    setResizeLimits (minWindowWidth, juce::roundToInt ((float) minWindowWidth * height / width),
                     readingWidth * 2, juce::roundToInt ((float) readingWidth * 2.0f * height / width));
    setSize (readingWidth, juce::roundToInt ((float) readingWidth * height / width));

    renders = { { "knob-drive", look.driveKnob }, { "knob-tone", look.toneKnob },
                { "slider-cap", look.cap }, { "fader-cap", look.faderCap } };
    for (size_t i = 0; i < look.lampsLit.size(); ++i)
        if (look.lampsLit[i] != nullptr)
            renders.push_back ({ "lamp-" + juce::String ((int) i), *look.lampsLit[i] });
    lookForWeb = { look.drivePointer, look.tonePointer, look.handLight, look.ink };

    if (const auto folder = juce::SystemStats::getEnvironmentVariable ("ORBITAMP_WEB_EXPORT", {}); folder.isNotEmpty())
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<Console> (this), folder]
        {
            if (safe != nullptr)
                safe->exportForWeb (juce::File (folder));
        });
}

namespace
{
    juce::var rectVar (juce::Rectangle<float> r)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("x", r.getX()); o->setProperty ("y", r.getY());
        o->setProperty ("w", r.getWidth()); o->setProperty ("h", r.getHeight());
        return o;
    }

    juce::var pointVar (juce::Point<float> p)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("x", p.x); o->setProperty ("y", p.y);
        return o;
    }

    juce::var colourVar (juce::Colour c)
    {
        return "#" + juce::String::toHexString ((int) c.getRed()).paddedLeft ('0', 2)
                   + juce::String::toHexString ((int) c.getGreen()).paddedLeft ('0', 2)
                   + juce::String::toHexString ((int) c.getBlue()).paddedLeft ('0', 2)
                   + juce::String::toHexString ((int) c.getAlpha()).paddedLeft ('0', 2);
    }

    juce::var pointerVar (ArtKnob::Pointer p)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("inner", p.inner); o->setProperty ("outer", p.outer); o->setProperty ("width", p.width);
        return o;
    }

    template <typename Each, typename Range>
    juce::var listVar (const Range& range, Each each)
    {
        juce::Array<juce::var> list;
        for (const auto& v : range)
            list.add (each (v));
        return list;
    }
}

void writeForWeb (const juce::File& file, const juce::Image& img)
{
    web::write (file, img);
}

void Console::writePoster (const juce::File& dir, juce::Rectangle<float> window, const juce::Image& still,
                           const juce::Image& live, const std::vector<juce::Component*>& hidden, const juce::String& stem)
{
    std::vector<std::pair<juce::Component*, bool>> was;
    for (auto* c : hidden)
        if (c != nullptr)
        {
            was.push_back ({ c, c->isVisible() });
            c->setVisible (false);
        }

    const float k = 2.0f;
    const auto shot = stage.createComponentSnapshot (stage.getLocalBounds(), true, k);
    for (const auto& [c, visible] : was)
        c->setVisible (visible);
    const auto under = [&] (const juce::Image& view)
    {
        juce::Image poster (juce::Image::ARGB, juce::roundToInt (stage::width * k), juce::roundToInt (stage::height * k), true);
        {
            juce::Graphics g (poster);
            g.fillAll (juce::Colours::black);
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            if (view.isValid())
                g.drawImage (view, window * k, juce::RectanglePlacement::stretchToFit);
            g.drawImageAt (shot, 0, 0);
        }
        return poster;
    };
    if (live.isValid())
        web::writeThreeSizes (dir, stem, under (live), "poster");
    web::writeThreeSizes (dir, stem + "-still", under (still), "poster");

    // ...and the same face with nothing under it: the window empty, for a picture of the site's own to go
    // under it there.
    juce::Image hole (juce::Image::ARGB, shot.getWidth(), shot.getHeight(), true);
    juce::Graphics (hole).drawImageAt (shot, 0, 0);
    web::writeThreeSizes (dir, stem + "-hole", hole, "poster");
    postersWritten[stem] = live.isValid();
}

juce::var Console::posterFiles (const juce::String& stem, bool live)
{
    auto files = web::threeNames (live ? stem : stem + "-still", "poster");
    files.getDynamicObject()->setProperty ("still", web::threeNames (stem + "-still", "poster"));
    files.getDynamicObject()->setProperty ("hole", web::threeNames (stem + "-hole", "poster"));
    return files;
}

void Console::shootPoster (const juce::File& dir, int place, const juce::String& stem, std::function<void()> then)
{
    const auto shoot = [safe = juce::Component::SafePointer<Console> (this), dir, place, stem, then] (const juce::Image& live)
    {
        if (safe == nullptr)
            return;
        safe->writePoster (dir, safe->webPoses.window, safe->webPoses.still (place), live, safe->webPoses.hidden, stem);
        then();
    };
    if (webPoses.firstFrame == nullptr)
    {
        shoot ({});
        return;
    }
    // The live view's first frame, from the GPU — or, where none has come in ten seconds, the still: taken
    // once, whichever comes first.
    const auto taken = std::make_shared<bool> (false);
    const auto take = [taken, shoot] (const juce::Image& live)
    {
        if (! std::exchange (*taken, true))
            shoot (live);
    };
    webPoses.firstFrame (place, take);
    juce::Timer::callAfterDelay (10000, [take] { take ({}); });
}

void Console::webPatch (const juce::String& name, std::initializer_list<const juce::Component*> parts)
{
    juce::Rectangle<float> box;
    for (const auto* c : parts)
        if (c != nullptr && c->isVisible())
            box = box.isEmpty() ? c->getBoundsInParent().toFloat() : box.getUnion (c->getBoundsInParent().toFloat());
    if (! box.isEmpty())
        webPatches.push_back ({ name, box });
}

void Console::poseNext (const juce::File& dir, int pose)
{
    if (pose >= webPoses.count)
    {
        finishWeb (dir);
        return;
    }
    webPoses.standAt (pose);
    // Long enough for what follows a switch to have settled before the picture is taken — a repaint, a
    // lever or a hatch let run where a face animates it anyway, and a rover's speed eased to its gear's
    // (or to a stand), as its nav screen reads it.
    juce::Timer::callAfterDelay (2500, [safe = juce::Component::SafePointer<Console> (this), dir, pose]
    {
        if (safe != nullptr)
            safe->shootPoster (dir, pose, "poster-" + juce::String (pose), [safe, dir, pose]
            {
                if (safe != nullptr)
                    safe->poseNext (dir, pose + 1);
            });
    });
}

/** Every poster written: places.json names each one's files — a live frame's where one went under its glass,
    the still's where none did — and says what its window shows; done.txt last. */
void Console::finishWeb (const juce::File& dir)
{
    if (auto* o = webPlaces.getDynamicObject())
    {
        int live = 0;
        for (const auto& [stem, withFrame] : postersWritten)
            live += withFrame ? 1 : 0;
        if (const auto top = postersWritten.find ("poster"); top != postersWritten.end())
        {
            const auto files = posterFiles ("poster", top->second);
            for (const auto* size : { "poster", "poster1x", "posterHalf" })
                o->setProperty (size, files[size]);
            o->setProperty ("posterStill", files["still"]);
            o->setProperty ("posterHole", files["hole"]);
            const juce::String still = "still: the still the app ships, as its STILL mode shows it";
            o->setProperty ("posterWindow", live == 0 ? still
                                          : live == (int) postersWritten.size() ? "live: " + webPoses.live
                                          : "live: " + webPoses.live + "; where a place's frame could not be had, the still (" + still.fromFirstOccurrenceOf (": ", false, false) + ")");
        }
        juce::Array<juce::var> posters;
        for (int i = 0; i < webPoses.count; ++i)
        {
            const auto at = postersWritten.find ("poster-" + juce::String (i));
            posters.add (posterFiles ("poster-" + juce::String (i), at != postersWritten.end() && at->second));
        }
        o->setProperty ("posters", posters);
        dir.getChildFile ("places.json").replaceWithText (juce::JSON::toString (webPlaces));
    }
    dir.getChildFile ("done.txt").replaceWithText ("done");
}

juce::var Console::writeSprite (const juce::File& dir, const juce::String& stem, const juce::Image& img)
{
    auto file = web::writeSprite (dir, stem, img);
    if (! file.isVoid())
        spritesWritten.push_back ({ stem, opaqueBounds (img) });
    return file;
}

juce::var Console::spriteRef (const juce::String& stem) const
{
    for (const auto& written : spritesWritten)
        if (written.first == stem)
            return web::spriteFile (stem);
    return {};
}

void Console::exportForWeb (const juce::File& dir)
{
    dir.createDirectory();
    spritesWritten.clear();
    postersWritten.clear();
    const auto cabin = web::writeThreeSizes (dir, "cabin", picture->staticImage (2.0f), "cabin", web::halfCabinQuality);
    for (const auto& [name, img] : renders)
        writeSprite (dir, name, img);

    const auto& p = places;
    auto* o = new juce::DynamicObject();
    o->setProperty ("format", "orbitamp-web-face");
    o->setProperty ("schema", 4);       // 2: what is one world's own is under "world"; spriteBounds. 3: the world's whole map there. 4: each channel's "face"
    o->setProperty ("stage", rectVar ({ 0.0f, 0.0f, stage::width, stage::height }));
    for (const auto& size : cabin.getDynamicObject()->getProperties())       // cabin, cabin1x, cabinHalf
        o->setProperty (size.name, size.value);
    o->setProperty ("cabinScale", 2.0f);
    o->setProperty ("drive", rectVar (p.drive));
    o->setProperty ("keys", listVar (p.keys, pointVar));       // the two filters; the tone knobs and the right group are each channel's "face"
    o->setProperty ("keySize", p.keySize);
    o->setProperty ("holes", listVar (p.holes, pointVar));
    o->setProperty ("holeKeySize", p.holeKeySize);
    o->setProperty ("sliders", listVar (p.sliders, rectVar));
    o->setProperty ("sliderNames", listVar (p.sliderNames, rectVar));
    o->setProperty ("sliderCapAngle", p.sliderCapAngle);
    o->setProperty ("sliderCapLength", p.sliderCapLength);
    o->setProperty ("faderCapLength", p.faderCapLength);
    o->setProperty ("inMeter", rectVar (p.inMeter));
    o->setProperty ("inFader", rectVar (p.inFader));
    o->setProperty ("outMeter", rectVar (p.outMeter));
    o->setProperty ("outFader", rectVar (p.outFader));
    o->setProperty ("tuner", rectVar (p.tuner));
    o->setProperty ("lamps", listVar (p.lamps, rectVar));
    o->setProperty ("emblemFace", listVar (p.emblemFace, pointVar));
    o->setProperty ("emblemMark", rectVar (p.emblemMark));
    // ...and the mark lit under the hand, where the face has a lamp for it (Emblem): the lit glass whole, at
    // twice the stage, its box, the lamp, and the spring the light comes up and goes on — for the site to
    // fade the picture in over the engraving at the light's share, as the face does.
    if (const auto glass = emblem.litPicture (2.0f); glass.isValid())
    {
        auto* l = new juce::DynamicObject();
        l->setProperty ("sprite", writeSprite (dir, "emblem-lit", glass));
        l->setProperty ("box", rectVar (emblem.litBox()));
        l->setProperty ("lamp", colourVar (emblem.lampColour()));
        l->setProperty ("lights", "mark");      // the cat is the glass: its floor lit through
        l->setProperty ("stiffness", emblem.lightStiffness());
        l->setProperty ("damping", emblem.lightDamping());
        l->setProperty ("riseSeconds", emblem.lightSeconds());
        l->setProperty ("fallSeconds", emblem.lightSeconds());
        l->setProperty ("how", "laid over the badge at the light's share, a spring with no overshoot toward 1 under the hand and 0 off it; "
                               "the cat's floor lit through, its walls kept, a bloom past its edges");
        o->setProperty ("emblemLit", juce::var (l));
    }
    else
        o->setProperty ("emblemLit", juce::var());
    o->setProperty ("version", rectVar (p.version));
    o->setProperty ("load", rectVar (p.load));
    o->setProperty ("memory", rectVar (p.memory));
    o->setProperty ("preset", rectVar (p.preset));
    o->setProperty ("gateDoor", rectVar (p.gateDoor));
    o->setProperty ("limiterDoor", rectVar (p.limiterDoor));
    o->setProperty ("gateDoorAngle", p.gateDoorAngle);
    o->setProperty ("limiterDoorAngle", p.limiterDoorAngle);
    o->setProperty ("emblemFace", listVar (p.emblemFace, pointVar));
    {
        auto* m = new juce::DynamicObject();
        m->setProperty ("count", p.meterDots.count); m->setProperty ("size", p.meterDots.size);
        o->setProperty ("meterDots", p.meterDots.count > 0 ? juce::var (m) : juce::var());
    }
    o->setProperty ("lampDot", p.lampDot);
    o->setProperty ("readingPx", p.readingPx);
    o->setProperty ("readingsFit", p.readingsFit);
    o->setProperty ("tunerStrip", p.tunerStrip);
    if (p.tunerDial.reach > 0.0f)
    {
        auto* d = new juce::DynamicObject();
        d->setProperty ("pivot", pointVar (p.tunerDial.pivot)); d->setProperty ("reach", p.tunerDial.reach);
        d->setProperty ("sweep", p.tunerDial.sweep); d->setProperty ("window", rectVar (p.tunerDial.window));
        o->setProperty ("tunerDial", juce::var (d));
    }

    // WHAT EACH CHANNEL OPENS ON, in the lever's order: its pack, the switch it holds, the channel it
    // shares its memory with, its room and its shares of reverb and delay, DRIVE and the tones as they
    // stand on it, and its pack's switches.
    {
        const auto& line = channels.lineup();
        const auto ranged = [this] (const juce::String& id, float value)
        {
            auto* v = new juce::DynamicObject();
            v->setProperty ("param", id);
            v->setProperty ("value", value);
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (amp.apvts.getParameter (id)))
            {
                v->setProperty ("min", r->getNormalisableRange().start);
                v->setProperty ("max", r->getNormalisableRange().end);
            }
            return juce::var (v);
        };
        juce::Array<juce::var> list;
        for (int c = 0; c < channels.count(); ++c)
        {
            const auto& ch = line.channels[(size_t) c];
            auto* x = new juce::DynamicObject();
            x->setProperty ("name", ch.name);
            x->setProperty ("pack", ch.capture);
            auto* held = new juce::DynamicObject();
            if (ch.held.control != nullptr)
                held->setProperty (ch.held.control, ch.held.value);
            x->setProperty ("held", juce::var (held));
            x->setProperty ("pair", line.pairs ? juce::var (c ^ 1) : juce::var());
            x->setProperty ("hot", ch.hot);
            x->setProperty ("reverb", params::reverbCharacters[juce::roundToInt (channels.defaultOf (c, params::reverbType))]);
            x->setProperty ("reverbMix", channels.defaultOf (c, params::reverbMix));
            x->setProperty ("delayMix", channels.defaultOf (c, params::delayMix));
            x->setProperty ("drive", ranged (params::preampGain, channels.defaultOf (c, params::preampGain)));
            juce::Array<juce::var> tones;
            const auto toneNames = channels.toneNamesOf (c);     // the pack's tones, knob i its tone i
            for (int t = 0; t < toneNames.size(); ++t)
            {
                const auto id = channels.toneOf (c, t);
                auto v = ranged (id, channels.defaultOf (c, id));
                v.getDynamicObject()->setProperty ("name", toneNames[t]);
                tones.add (v);
            }
            x->setProperty ("tones", tones);

            // ITS FACE: the rows as the face lays them on this channel (stripsFor) — each tone knob's box,
            // its name's box and word, the pack's tone it plays and the parameter that is; each key right
            // of the filters, its box, its name's box and word, the switch it throws and how; and the
            // frame round them, where the face draws one.
            {
                const auto laid = stripsFor (c);
                const auto right = rightKeysOf (c);
                auto* f = new juce::DynamicObject();
                juce::Array<juce::var> knobs, keys;
                for (const auto& t : laid.tones)
                {
                    // ...its sign, a sprite the size of its label's box (at twice the stage), drawn as the face draws it.
                    juce::var file;
                    {
                        juce::Image img (juce::Image::ARGB, juce::roundToInt (t.label.getWidth() * 2.0f), juce::roundToInt (t.label.getHeight() * 2.0f), true);
                        juce::Graphics sg (img);
                        sg.addTransform (juce::AffineTransform::scale (2.0f));
                        drawToneSign (sg, { 0.0f, 0.0f, t.label.getWidth(), t.label.getHeight() }, t.curve, picture->signInk(), laid.toneSignHeight);
                        file = writeSprite (dir, "tone-" + juce::String (c) + "-" + juce::String (t.tone), img);
                    }
                    auto* k = new juce::DynamicObject();
                    k->setProperty ("box", rectVar (t.knob));
                    k->setProperty ("label", rectVar (t.label));
                    k->setProperty ("icon", file);
                    k->setProperty ("text", t.name);
                    k->setProperty ("tone", t.tone);
                    k->setProperty ("param", t.param);
                    knobs.add (juce::var (k));
                }
                for (size_t i = (size_t) laid.firstRightKey; i < laid.keys.size(); ++i)
                {
                    const auto& key = laid.keys[i];
                    const auto& r = right[i - (size_t) laid.firstRightKey];
                    auto* k = new juce::DynamicObject();
                    k->setProperty ("box", rectVar (key.box));
                    k->setProperty ("label", rectVar (key.label));
                    k->setProperty ("text", key.name);
                    if (r.slot >= 0)
                    {
                        k->setProperty ("control", r.control);
                        k->setProperty ("param", params::selectorId (params::preampId, r.slot));
                        k->setProperty ("lit", r.position);         // the switch's value it is lit at, and puts it to
                        if (r.off >= 0)
                            k->setProperty ("off", r.off);          // ...an off/on key: where letting it up puts it
                    }
                    else
                        k->setProperty ("own", true);               // the face's own key, not the pack's
                    keys.add (juce::var (k));
                }
                f->setProperty ("tones", knobs);
                f->setProperty ("toneScale", laid.toneScale);
                f->setProperty ("keys", keys);
                f->setProperty ("keyScale", laid.keyScale);
                f->setProperty ("frame", laid.keyFrame.isEmpty() ? juce::var() : rectVar (laid.keyFrame));
                x->setProperty ("face", juce::var (f));
            }
            juce::Array<juce::var> switches;
            for (const auto& sw : channels.switchesOf (c))
            {
                auto* w = new juce::DynamicObject();
                w->setProperty ("name", sw.name);
                juce::Array<juce::var> values;
                for (const auto& v : sw.values)
                    values.add (v);
                w->setProperty ("values", values);
                w->setProperty ("opening", sw.opening);
                w->setProperty ("held", sw.held);
                switches.add (juce::var (w));
            }
            x->setProperty ("switches", switches);
            list.add (juce::var (x));
        }
        auto* l = new juce::DynamicObject();
        l->setProperty ("channels", list);
        l->setProperty ("cabinet", line.openingCabinet);
        // ...and the cabinets CAB steps through, in its order — the build's own, as an export has no
        // player's beside them — with the positions its slider has, a tick each.
        juce::Array<juce::var> cabinets;
        for (const auto& file : channels.cabinets())
        {
            auto* cab = new juce::DynamicObject();
            cab->setProperty ("file", Channels::cabinetStem (file));
            cab->setProperty ("name", Channels::cabinetName (file));
            cab->setProperty ("folder", channels.cabinetFolder (cabinets.size()));
            cabinets.add (juce::var (cab));
        }
        l->setProperty ("cabinets", cabinets);
        l->setProperty ("cabSteps", juce::jmax (2, channels.cabinets().size()));
        auto* delay = new juce::DynamicObject();
        delay->setProperty ("division", params::delayDivisions[juce::roundToInt (channels.defaultOf (0, params::delayDiv))]);
        delay->setProperty ("bpm", channels.defaultOf (0, params::delayBpm));
        l->setProperty ("delay", juce::var (delay));
        o->setProperty ("lineup", juce::var (l));
    }

    // HOW THIS FACE DRAWS THE FAMILY'S CONTROLS — what is its own; what every face shares is in
    // face/Controls.cpp's constants.
    {
        auto* k = new juce::DynamicObject();
        k->setProperty ("drivePointer", pointerVar (lookForWeb.drive));
        k->setProperty ("tonePointer", pointerVar (lookForWeb.tone));
        k->setProperty ("pointerColour", colourVar (juce::Colour (0xffd9c8ff)));
        k->setProperty ("pointerGlow", colourVar (violet));
        k->setProperty ("handLight", colourVar (lookForWeb.handLight));
        auto* ink = new juce::DynamicObject();
        ink->setProperty ("words", colourVar (lookForWeb.ink.words));
        ink->setProperty ("values", colourVar (lookForWeb.ink.values));
        ink->setProperty ("state", colourVar (lookForWeb.ink.state));
        ink->setProperty ("edge", colourVar (lookForWeb.ink.edge));
        ink->setProperty ("shade", colourVar (lookForWeb.ink.shade));
        ink->setProperty ("weight", lookForWeb.ink.weight);
        ink->setProperty ("leastAlpha", lookForWeb.ink.leastAlpha);
        ink->setProperty ("edgeDrop", Ink::edgeDrop);
        k->setProperty ("ink", juce::var (ink));
        k->setProperty ("labelLilac", colourVar (labelLilac));
        auto* px = new juce::DynamicObject();
        px->setProperty ("px11", stage::px11); px->setProperty ("px13", stage::px13);
        px->setProperty ("px15", stage::px15); px->setProperty ("px16", stage::px16);
        k->setProperty ("labelPx", juce::var (px));
        k->setProperty ("keyTracking", keyTracking);

        // ...and the tuner's ink, the same on every face — what its screen is written with, whatever the
        // hull's ink is (TunerFace).
        auto* t = new juce::DynamicObject();
        t->setProperty ("off", colourVar (violet));
        t->setProperty ("on", colourVar (orange));          // ON, and MUTE
        t->setProperty ("note", colourVar (TunerFace::heard));
        t->setProperty ("inTune", colourVar (TunerFace::home));
        t->setProperty ("idle", colourVar (lilac.withAlpha (TunerFace::idleAlpha)));       // the dash while nothing is heard
        t->setProperty ("quiet", colourVar (lilac.withAlpha (TunerFace::quietAlpha)));     // the octave, "A 442"
        t->setProperty ("numbers", colourVar (lilac.withAlpha (TunerFace::numberAlpha)));  // under the ruler, on a screen of its own
        t->setProperty ("tick", colourVar (violet));
        t->setProperty ("centreTick", colourVar (orange));
        t->setProperty ("majorTickAlpha", TunerFace::majorAlpha);
        t->setProperty ("minorTickAlpha", TunerFace::minorAlpha);
        t->setProperty ("needleWarmsToOrangeByCents", TunerFace::warmCents);
        k->setProperty ("tuner", juce::var (t));
        o->setProperty ("look", juce::var (k));
    }

    auto* world = new juce::DynamicObject();
    webPoses = {};
    webPatches.clear();
    {
        std::vector<const juce::Component*> roof;
        for (const auto& k : overhead)
            roof.push_back (k.get());
        juce::Rectangle<float> history = picture->historyBox().toFloat();     // the signs and the lamps by the keys
        for (const auto* k : roof)
            if (k != nullptr && k->isVisible())
                history = history.getUnion (k->getBoundsInParent().toFloat());
        webPatches.push_back ({ "history", history });
    }
    webPatch ("tuner", { &tuner });
    webPatch ("gate", { &gateDoor });
    webPatch ("limiter", { &limiterDoor });
    webPatch ("version", { &version });
    webPatch ("preset", { &presetDoor });
    webPatch ("load", { &load });
    webPatch ("memory", { &memory });
    webExtras (dir, *o, *world);
    o->setProperty ("world", juce::var (world));

    // THE POSTER THE PAGE OPENS ON, where the face has a window — its three sizes, and its still and its
    // hole as kinds of their own — and one for each place of the lever: their keys here, in their order,
    // their files named once they are written (finishWeb).
    if (! webPoses.window.isEmpty())
        for (const auto* key : { "poster", "poster1x", "posterHalf", "posterStill", "posterHole", "posterWindow" })
            o->setProperty (key, juce::var());
    o->setProperty ("posters", juce::Array<juce::var>());
    juce::Array<juce::var> patches;
    for (const auto& [name, r] : webPatches)
    {
        auto v = rectVar (r);
        v.getDynamicObject()->setProperty ("name", name);
        patches.add (v);
    }
    o->setProperty ("posterPatches", patches);

    auto* bounds = new juce::DynamicObject();
    for (const auto& [name, r] : spritesWritten)
        bounds->setProperty (name, rectVar (r));
    o->setProperty ("spriteBounds", juce::var (bounds));
    webPlaces = juce::var (o);

    // The face as it opened first, at the place it opened at; then each place in turn.
    if (webPoses.window.isEmpty())
        poseNext (dir, 0);
    else
        shootPoster (dir, webPoses.opening, "poster", [safe = juce::Component::SafePointer<Console> (this), dir]
        {
            if (safe != nullptr)
                safe->poseNext (dir, 0);
        });
}

Console::~Console()
{
    amp.history.onHistoryChanged = nullptr;   // the history outlives this window
    engine.onChange = nullptr;                // ...and so does the engine
    endTapping();
    setFullScreen (false);                    // a window closed in full screen is remembered where it stood before
}

void Console::addToStage (juce::Component& c)
{
    stage.addAndMakeVisible (c);
    nameTag.toFront (false);
}

bool Console::go (int to)
{
    endTapping();
    const bool went = engine.go (to);       // refused when that channel's pack is not installed...
    showChannel();                          // ...and then this puts the selector back where the sound is
    return went;
}

/** A STATE THE PRODUCT HAS NOT SEEN — a session saved before it existed, loaded while the window is
    open — is made its own the way the constructor makes the first one: the fixed chain, the channel
    whose pack is playing, a cabinet CAB can reach. And one it HAS seen, whose cabinet is no longer
    the file of that name — a register kept from before the file was replaced — has it read again;
    and one kept under another TURBO or another way of DRIVE's is given the settings' own. */
void Console::adoptState()
{
    adoptionPending = false;
    const bool unseen = ! channels.adopted();
    if (! unseen && ! channels.cabinetIsStale() && ! settingsDiffer())
        return;

    const felitronics::appkit::CompareHistory::ScopedSuppress settling (amp.history);
    engine.makeOwn();
    channels.align();
    settleOnSettings();
    showChannel();
}

/** The engine moved a channel, or took in a state the product had not seen, with no hand of this
    window's on it. The face shows where the sound is now — and a state just taken in is settled on
    the machine's settings, which are this window's to keep. */
void Console::followEngine()
{
    if (settingsDiffer())
    {
        const felitronics::appkit::CompareHistory::ScopedSuppress settling (amp.history);
        settleOnSettings();
    }
    showChannel();
}

bool Console::settingsDiffer() const
{
    const float push = getParam (params::blockIn (params::preampId).toRawUTF8());

    return (push > Channels::turboOnAbove && std::abs (push - channels.turboDb()) > 0.05f)
        || (getParam (params::blockSmooth (params::preampId).toRawUTF8()) > 0.5f) == driveSteps;
}

void Console::settleOnSettings()
{
    // TURBO: that it is on is the state's, how hard is the settings'.
    const auto turboTrim = params::blockIn (params::preampId);
    if (const float push = getParam (turboTrim.toRawUTF8());
        push > Channels::turboOnAbove && std::abs (push - channels.turboDb()) > 0.05f)
        setParam (turboTrim.toRawUTF8(), channels.turboDb());

    // ...and so is how DRIVE goes between the captures: mixing the two it stands between, or in steps.
    const auto smooth = params::blockSmooth (params::preampId);
    if ((getParam (smooth.toRawUTF8()) > 0.5f) == driveSteps)
        setParam (smooth.toRawUTF8(), driveSteps ? 0.0f : 1.0f);
}

void Console::flipHum()
{
    endTapping();
    prefs::setHum (amp.product(), prefs::humFlip (amp.product()));

    const auto at = inFader.getBoundsInParent().toFloat();
    sayForAMoment ({ at.getCentreX(), at.getY() - 24.0f }, prefs::humTag (prefs::hum (amp.product()), amp.humHeard()));
}

/** G: the gate's On, through the same hands as GATE's menu — and the tag over GATE says where it is. */
void Console::flipGate()
{
    endTapping();
    setParam (params::gateOn, getParam (params::gateOn) > 0.5f ? 0.0f : 1.0f);

    const auto at = gateDoor.getBoundsInParent().toFloat();
    sayForAMoment ({ at.getCentreX(), at.getY() - 22.0f }, getParam (params::gateOn) > 0.5f ? "GATE ON" : "GATE OFF");
}

/** The name tag, for a moment and a half — unless something says more meanwhile. */
void Console::sayForAMoment (juce::Point<float> at, const juce::String& words)
{
    nameTag.show (at, words);
    juce::Timer::callAfterDelay (1500, [safe = juce::Component::SafePointer<Console> (this), shown = ++humTagShown]
    {
        if (safe != nullptr && safe->humTagShown == shown)
            safe->nameTag.show ({}, {});
    });
}

void Console::sayLever (ColumnFader& fader, const juce::String& name, std::function<float()> reading)
{
    const auto say = [this, &fader, name, reading]
    {
        float db = reading();
        if (std::abs (db) < 0.05f)
            db = 0.0f;      // never "-0.0"

        const auto at = fader.getBoundsInParent().toFloat();    // as it stands on the stage, turned or not
        nameTag.show ({ at.getCentreX(), at.getY() - 24.0f },
                      name + "  " + (db > 0.0f ? "+" : "") + juce::String (db, 1) + " dB");
    };

    // Round what the lever already does: the value is the parameter's, and it has moved by the time this reads it.
    fader.onGestureStart = [say, before = fader.onGestureStart] { if (before) before(); say(); };
    fader.onChange       = [say, before = fader.onChange] (float travel) { if (before) before (travel); say(); };
    fader.onGestureEnd   = [this, before = fader.onGestureEnd] { if (before) before(); nameTag.show ({}, {}); };
}

void Console::chooseTurbo (int db)
{
    db = snapTurbo (db);
    prefs::store (amp.product()).set (turboKey, db);
    channels.setTurboDb ((float) db);

    const felitronics::appkit::CompareHistory::ScopedSuppress settling (amp.history);
    settleOnSettings();
}

void Console::chooseDriveSteps (bool steps)
{
    driveSteps = steps;
    prefs::store (amp.product()).set (driveStepsKey, steps);

    const felitronics::appkit::CompareHistory::ScopedSuppress settling (amp.history);
    settleOnSettings();
    drive.settle();
}

/** THE TWO LEARNS, one at a time: starting either cancels the other — the gate's measurement and the
    hum's both need the same quiet, and each would hear the other's stage at work. */
void Console::startGateLearn()
{
    humLearner.cancel();
    learner.start();
}

void Console::startHumLearn()
{
    learner.cancel();
    humLearner.start();
}

/** THE HUM'S LINES (ui/HumMenu.h) — its LEARN, the switch, which flips as the H key does and says so
    on the tag, and the three modes. */
void Console::addHumLines (juce::PopupMenu& menu)
{
    for (const auto& line : prefs::humMenu (amp.product(), amp.humHeard(), humLearner.isLearning()))
        addChoice (menu, line.text, line.ticked, [line] (Console& e)
        {
            if (line.isLearn)
                e.startHumLearn();
            else if (line.isSwitch)
                e.flipHum();
            else
                prefs::humPick (e.amp.product(), line);
        });

    // How hard it works — TEMPORARY, for tuning by ear (ui/HumTuning.h): each a submenu, its value in
    // its title.
    for (const auto& knob : prefs::humtune::knobs (amp.product()))
    {
        juce::PopupMenu values;
        for (int i = 0; i < knob.count; ++i)
            addChoice (values, knob.say (knob.values[i]), juce::exactlyEqual (knob.values[i], knob.now),
                       [choose = knob.choose, v = knob.values[i]] (Console&) { choose (v); });
        menu.addSubMenu (juce::String (knob.name) + "  " + knob.say (knob.now), values);
    }
}

void Console::addChoice (juce::PopupMenu& menu, const juce::String& text, bool ticked,
                              std::function<void (Console&)> picked, bool enabled)
{
    juce::PopupMenu::Item item (text);
    item.itemID    = menu.getNumItems() + 1;
    item.colour    = lilac;
    item.isTicked  = ticked;
    item.isEnabled = enabled;
    item.action    = [safe = juce::Component::SafePointer<Console> (this), run = std::move (picked)]
    {
        if (safe != nullptr)     // the action is posted: the window may be gone by the time it runs
            run (*safe);
    };
    menu.addItem (item);
}

/** A RIGHT CLICK ON THE INPUT COLUMN — the lever, or the meter beside it: the hum the input carries — the
    machine's, the four modes here as flat items, AUTO's line naming what it decided once it has. With HUM
    off in the build (Features.h) the column has no menu of its own, and a right click on it is one on the
    face, as anywhere else that has none: the face's menu — whose INPUT is DRIVE's — rather than a lever
    that jumps. */
void Console::showInputMenu()
{
    endTapping();

    if constexpr (! features::hum)
    {
        showSettingsMenu();
        return;
    }

    juce::PopupMenu menu;
    menu.addSectionHeader ("HUM");
    addHumLines (menu);

    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

/** A RIGHT CLICK ON THE OUTPUT COLUMN — the lever, or the meter beside it: the limiter that stands last
    in the chain, whether it works and what it lets through at the most. It is the sound's, not the
    machine's: a session and a preset keep it, and undo steps over it. */
void Console::showOutputMenu()
{
    endTapping();

    const bool on  = getParam (params::limiterOn) > 0.5f;
    const float at = getParam (params::limiterCeiling);

    juce::PopupMenu menu;
    menu.addSectionHeader ("LIMITER");
    addChoice (menu, "On", on, [on] (Console& e) { e.setParam (params::limiterOn, on ? 0.0f : 1.0f); });

    menu.addSectionHeader ("CEILING");
    for (const float db : limiterCeilings)
        addChoice (menu, juce::String (db, 1) + " dB", std::abs (at - db) < 0.05f,
                   [db] (Console& e) { e.setParam (params::limiterCeiling, db); }, on);

    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

/** A CLICK ON GATE, on its plate left of PRESET: the gate, as the settings menu holds it too — whether it
    works, where it closes, how fast, and where in the chain it mutes. The sound's, like the limiter. */
void Console::showGateMenu()
{
    endTapping();

    const bool on = getParam (params::gateOn) > 0.5f;
    const auto choice = [this] (const char* id) { return juce::roundToInt (getParam (id)); };

    juce::PopupMenu menu;
    menu.addSectionHeader ("GATE");
    // LEARN first: three seconds of quiet set the threshold and switch the gate on — so the gate off, it is offered too.
    addChoice (menu, "Learn", learner.isLearning(), [] (Console& e) { e.startGateLearn(); });
    addChoice (menu, "On", on, [on] (Console& e) { e.setParam (params::gateOn, on ? 0.0f : 1.0f); });

    juce::PopupMenu threshold;
    const int now = juce::roundToInt (getParam (params::gateThreshold));
    for (int db = (int) params::gateThresholdMin; db <= (int) params::gateThresholdMax; db += 5)
        addChoice (threshold, juce::String (db) + " dB", std::abs (now - db) <= 2,
                   [db] (Console& e) { e.setParam (params::gateThreshold, (float) db); });
    menu.addSubMenu ("Threshold  " + juce::String (now) + " dB", threshold, on);

    juce::PopupMenu decay;
    for (int i = 0; i < params::gateDecayModes.size(); ++i)
        addChoice (decay, params::gateDecayModes[i], choice (params::gateDecay) == i,
                   [i] (Console& e) { e.setParam (params::gateDecay, (float) i); });
    menu.addSubMenu ("Closes  " + params::gateDecayModes[choice (params::gateDecay)], decay, on);

    juce::PopupMenu position;
    for (int i = 0; i < params::gatePositions.size(); ++i)
        addChoice (position, params::gatePositions[i], choice (params::gatePos) == i,
                   [i] (Console& e) { e.setParam (params::gatePos, (float) i); });
    menu.addSubMenu ("Mutes at  " + params::gatePositions[choice (params::gatePos)], position, on);

    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

/** A RIGHT CLICK ON TURBO: how hard it pushes into the capture — here only, the settings menu leaves it to
    TURBO. The machine's to say: every channel that has TURBO on comes back at the push in force. */
void Console::showTurboMenu()
{
    endTapping();

    const int now = juce::roundToInt (channels.turboDb());

    juce::PopupMenu menu;
    menu.addSectionHeader ("TURBO ADDS");
    for (const int db : turboPushes)
        addChoice (menu, decibels (db), db == now, [db] (Console& e) { e.chooseTurbo (db); });

    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

/** A RIGHT CLICK ON THE TUNER: whether it listens at all, and whether the amp is silent while it does —
    the way a tuner pedal mutes the rig. Off, it does neither. Both are the sound's own switches, the
    same two the engine has always had; the glass says which is thrown. And the pitch it measures
    from, which is the machine's. */
void Console::showTunerMenu()
{
    endTapping();

    const bool on    = getParam (params::tunerOn) > 0.5f;
    const bool muted = getParam (params::tunerMute) > 0.5f;

    juce::PopupMenu menu;
    menu.addSectionHeader ("TUNER");
    addChoice (menu, "On", on, [on] (Console& e) { e.setParam (params::tunerOn, on ? 0.0f : 1.0f); });
    addChoice (menu, "Mute the amp", muted, [muted] (Console& e) { e.setParam (params::tunerMute, muted ? 0.0f : 1.0f); }, on);

    // The pitch its A is tuned to: the machine's, not the sound's — it is about who the guitar plays with.
    juce::PopupMenu reference;
    const int a4 = prefs::tunerReference (amp.product());
    for (const int hz : prefs::tunerReferences)
        addChoice (reference, juce::String (hz) + " Hz", hz == a4, [hz] (Console& c) { prefs::setTunerReference (c.amp.product(), hz); });
    menu.addSubMenu ("Reference  A = " + juce::String (a4) + " Hz", reference);

    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

void Console::showTunerState()
{
    const bool on = getParam (params::tunerOn) > 0.5f;
    tuner.setState (on, on && getParam (params::tunerMute) > 0.5f);
}

/** A RIGHT CLICK ON DRIVE: how it goes between the pack's captures — the setting the settings menu
    keeps under INPUT, here on the knob it is about — and a capture of the pack as a `.nam`, the model
    alone, as it was shot, for whatever else plays one. IN STEPS, DRIVE STANDS ON ONE CAPTURE, and that
    is the one: the sound that is playing. Turning smoothly it stands between two and plays a mix of
    them, which is no file — so the menu offers the nearer of the two, and says so. */
void Console::showDriveMenu()
{
    endTapping();

    const auto itemFor = [this] (const juce::String& text, int id, bool ticked, std::function<void (Console&)> then)
    {
        juce::PopupMenu::Item item (text);
        item.itemID   = id;
        item.colour   = lilac;
        item.isTicked = ticked;
        item.action   = [safe = juce::Component::SafePointer<Console> (this), run = std::move (then)]
        {
            if (safe != nullptr)     // the action is posted: the window may be gone by the time it runs
                run (*safe);
        };
        return item;
    };

    juce::PopupMenu menu;
    menu.addSectionHeader ("DRIVE TURNS");
    for (int i = 0; i < driveWays.size(); ++i)
        menu.addItem (itemFor (driveWays[i], 1 + i, (i == 1) == driveSteps,
                               [steps = i == 1] (Console& e) { e.chooseDriveSteps (steps); }));

    menu.addSeparator();
    menu.addItem (itemFor (driveSteps ? "Save the current sound as .nam..." : "Save the nearest capture as .nam...",
                           1 + driveWays.size(), false, [] (Console& e) { e.saveCapture(); }));

    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

void Console::saveCapture()
{
    auto capture = amp.preamp.captureOnDial();
    if (capture.nam.isEmpty())
        return;

    auto folder = juce::File (prefs::store (amp.product()).get (captureFolderKey, juce::String()).toString());
    if (! folder.isDirectory())
        folder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

    captureChooser = std::make_unique<juce::FileChooser> ("Save the capture", folder.getChildFile (captureNamePrefix (amp.product()) + capture.name + hourTag (capture.hour) + ".nam"), "*.nam");
    captureChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                     | juce::FileBrowserComponent::warnAboutOverwriting,
                                 [nam = std::move (capture.nam), &product = amp.product()] (const juce::FileChooser& picked)
                                 {
                                     const auto file = picked.getResult();
                                     if (file == juce::File())
                                         return;

                                     prefs::store (product).set (captureFolderKey, file.getParentDirectory().getFullPathName());
                                     if (! file.replaceWithData (nam.getData(), nam.getSize()))
                                         sayNotSaved (file);
                                 });
}

/** A RIGHT CLICK ON CAB — the slider, or the name over it — opens the cabinets' window: all of them by
    name, the build's own and the player's apart, and the keys that add one, hand one out, and rename
    or remove one of the player's. */
void Console::showCabs()
{
    endTapping();

    auto cabs = std::make_unique<CabPanel> (channels, juce::Point<int> (getWidth() - 72, getHeight() - 72));
    cabs->onChoose = [this] (int cabinet) { chooseCab (cabinet); showChannel(); };
    cabs->onAdd    = [this] { chooseCabsToAdd(); };
    cabs->onSave   = [this] { saveCab(); };
    cabs->onRename = [this] { renameCab(); };
    cabs->onRemove = [this] { removeCab(); };
    cabs->onClose  = [this] { if (sheet != nullptr && sheet->onDismiss) sheet->onDismiss(); };
    cabs->onBypass = [this] { channels.setCabinetOn (! channels.cabinetOn()); };
    showSheet (std::move (cabs));
}

/** The cabinet that is playing, as a file: the IR as it is, under the name it has here without the
    number that orders it, for whatever else loads one. */
void Console::saveCab()
{
    const int playing = channels.cabinet();
    if (playing < 0)
        return;
    const auto cabinet = channels.cabinets()[playing];

    auto folder = juce::File (prefs::store (amp.product()).get (cabFolderKey, juce::String()).toString());
    if (! folder.isDirectory())
        folder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

    cabChooser = std::make_unique<juce::FileChooser> ("Save the cabinet",
                                                      folder.getChildFile (cabNamePrefix (amp.product()) + Channels::cabinetStem (cabinet) + cabinet.getFileExtension()),
                                                      "*" + cabinet.getFileExtension());
    cabChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                 | juce::FileBrowserComponent::warnAboutOverwriting,
                             [cabinet, &product = amp.product()] (const juce::FileChooser& picked)
                             {
                                 const auto file = picked.getResult();
                                 if (file == juce::File())
                                     return;

                                 prefs::store (product).set (cabFolderKey, file.getParentDirectory().getFullPathName());
                                 if (! cabinet.copyFileTo (file))
                                     sayNotSaved (file);
                             });
}

void Console::timerCallback()
{
    version.setDot (amp.updateChecker().updateAvailable());
    presetDoor.setDot (presets.changed());
    drive.settle();     // in steps it stands on a capture, and the pack under it may be another now

    // The load turns warm as it climbs — OrbitAmp's thresholds: a number noticed when it starts to matter.
    const float percent = amp.dspLoadPercent();
    const auto warmth = percent > 80.0f ? ink.state
                      : percent > 50.0f ? ink.values
                                        : ink.at (ink.words, 0.85f);
    if (warmth != load.valueColour)
    {
        load.valueColour = warmth;
        load.repaint();
    }
    load.set ("CPU", juce::String (juce::roundToInt (percent)) + "%");

    // Memory moves slowly, and a figure that flickers in its last digit four times a second is a
    // distraction: once a second is plenty.
    if (memoryTick++ % 4 == 0)
    {
        const double mb = amp.runsInHost() ? 0.0 : (double) core::processMemoryBytes() / (1024.0 * 1024.0);
        memory.setVisible (mb > 0.0);
        memory.set ("MEM", mb < 1000.0 ? juce::String (juce::roundToInt (mb)) + " MB"
                                       : juce::String (mb / 1024.0, 1) + " GB");
    }

    // A host starts or stops saying its tempo whenever it likes.
    if (tempoIsOwn == (amp.runsInHost() && amp.hostTempoBpm() > 0.0f))
        showDelayTime();
}

/** A Program Change from a MIDI input picks a channel the product's way (programChange); then the
    controllers. */
void Console::playMidi()
{
    if (const int program = amp.takeProgramChange(); program >= 0)
        programChange (program);

    // The controllers, after the program: a controller's preset sends its program first, and what
    // follows it is meant for the channel that program chose.
    const int turboTo = amp.takeControlChange (midi::turbo);
    const int hpfTo   = amp.takeControlChange (midi::hpf);
    const int lpfTo   = amp.takeControlChange (midi::lpf);
    const int driveTo = amp.takeControlChange (midi::drive);
    const int toneTo[] { amp.takeControlChange (midi::bass), amp.takeControlChange (midi::middle), amp.takeControlChange (midi::treble) };
    if (turboTo < 0 && hpfTo < 0 && lpfTo < 0 && driveTo < 0 && toneTo[0] < 0 && toneTo[1] < 0 && toneTo[2] < 0)
        return;

    // A foot on a pedal is playing, not editing: none of it is a step to undo.
    const felitronics::appkit::CompareHistory::ScopedSuppress playing (amp.history);

    const auto along = [this] (const juce::String& paramId, float travel)       // 0..1 of the control's own travel
    {
        if (auto* p = amp.apvts.getParameter (paramId))
            p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, travel));
    };
    const auto thrown = [] (int value) { return value >= 64; };

    if (turboTo >= 0)   turboChange (thrown (turboTo));
    if (hpfTo >= 0)     along (params::cabHpfOn, thrown (hpfTo) ? 1.0f : 0.0f);
    if (lpfTo >= 0)     along (params::cabLpfOn, thrown (lpfTo) ? 1.0f : 0.0f);
    if (driveTo >= 0)   along (params::preampGain, (float) driveTo / 127.0f);

    // A tone knob's middle is where the pack was captured, and 0..127 has no middle: 64 is given it, and
    // each half of the controller's travel is spread over its own half of the knob's. The three tone
    // controllers are the first three knobs, whatever the pack calls its tones; a knob the pack has not
    // names no parameter (Channels::toneOf), and its controller turns nothing.
    for (int t = 0; t < (int) std::size (toneTo); ++t)
        if (const int v = toneTo[(size_t) t]; v >= 0)
            along (channels.toneId (t), v <= 64 ? 0.5f * (float) v / 64.0f : 0.5f + 0.5f * (float) (v - 64) / 63.0f);
}

/** THE LOAD, ITEMISED — OrbitAmp's window, in the face's colours. Its words are already the face's:
    a product's packs are whole amps, with the boost and the power stage inside them, and OrbitAmp
    calls that row the AMP too. */
void Console::showLoad()
{
    auto panel = std::make_unique<LoadBreakdown> (amp);
    panel->dim   = labelLilac.withAlpha (0.85f);
    panel->faint = labelLilac.withAlpha (0.45f);
    showSheet (std::move (panel));
}

/** THE MIDI WINDOW — which inputs the product listens to, and the list of what it does with what it hears. */
void Console::showMidi()
{
    showSheet (std::make_unique<MidiPanel> (amp.product().name, midiCommands));
}

void Console::showSheet (std::unique_ptr<juce::Component> content)
{
    sheet = std::make_unique<Sheet> (std::move (content));
    // Closing is deferred: it is asked for from inside the very component it destroys.
    sheet->onDismiss = [safe = juce::Component::SafePointer<Console> (this)]
    {
        juce::MessageManager::callAsync ([safe]
        {
            if (safe == nullptr)
                return;
            safe->sheet.reset();
            safe->grabKeyboardFocus();       // the keys — undo, the channels — go back to the face
        });
    };

    addAndMakeVisible (*sheet);
    sheet->setBounds (getLocalBounds());
    sheet->toFront (true);
    sheet->grabKeyboardFocus();
}

void Console::refreshHistory()
{
    const auto& history = amp.history;
    picture->setHistory (history.canUndo(), history.canRedo(), history.active());
    for (int r = 0; r < numRegisterKeys; ++r)
        overhead[(size_t) (ovFirstRegister + r)]->setOn (r == history.active(), false);

    showChannel();   // an undo, a redo or another register may have brought a different channel with it

    // ...or a state with no channels in it at all, or one whose cabinet is not the file of that name
    // any more, or one kept under another TURBO or DRIVE's other way. Settling any of them writes, and this
    // is the history's own callback, so it is done on the way out.
    if ((! channels.adopted() || channels.cabinetIsStale() || settingsDiffer()) && ! adoptionPending)
    {
        adoptionPending = true;
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<Console> (this)]
        {
            if (safe != nullptr)
                safe->adoptState();
        });
    }
}

/** Puts the face where the state is: the product's selector on the playing channel, and the controls
    that live in the channels' node rather than in a parameter. Reads only — it is called from inside
    the history's own callbacks. */
void Console::showChannel()
{
    const int channel = channels.active();
    showSelector (channel);
    bindTones();        // another channel's pack may have other tones, and another count of them
    showKeys();

    // CAB's cap, on the cabinet that is playing. (This is first called while the face is still being
    // built, before the plates exist.)
    if (const int playing = channels.cabinet(); playing >= 0 && sliders[2] != nullptr)
        sliders[2]->setStep (playing, false);
    showCab();
}

std::vector<Console::RightKey> Console::rightKeysOf (int channel) const
{
    const auto isOff = [] (const juce::String& v)
    {
        const auto w = v.trim().toLowerCase();
        return w == "off" || w == "0" || w == "false" || w == "no";
    };

    // The keys the pack's switches may have: the right group's, but the ones the face has taken.
    std::vector<int> free, own;
    for (int b = btFirstRight; b < numKeys; ++b)
        (claimed[(size_t) b] ? own : free).push_back (b);

    std::vector<RightKey> out;
    size_t next = 0;
    for (const auto& s : channels.switchesOf (channel))
    {
        if (s.held)
            continue;
        const bool offOn = s.values.size() == 2 && isOff (s.values[0]) != isOff (s.values[1]);
        if (next + (offOn ? 1 : (size_t) s.values.size()) > free.size())
            continue;       // a switch that would not fit whole is left off
        if (offOn)
        {
            const int off = isOff (s.values[0]) ? 0 : 1;
            out.push_back ({ free[next++], s.slot, 1 - off, off, s.name, s.name.toUpperCase(), {} });
        }
        else
        {
            // A switch whose every position is bare (the IR-X Ch1's Bright: off, 1, 2) has its name written
            // once over its keys, and each key its position alone: BRIGHT over OFF · 1 · 2.
            const bool titled = std::all_of (s.values.begin(), s.values.end(), [] (const juce::String& v) { return device::isBarePosition (v); });
            for (int p = 0; p < s.values.size(); ++p)
                out.push_back ({ free[next++], s.slot, p, -1, s.name,
                                 titled ? s.values[p].trim().toUpperCase() : device::positionWords (s.name, s.values[p]),
                                 titled ? s.name.trim().toUpperCase() : juce::String() });
        }
    }
    for (const int b : own)
        out.push_back ({ b, -1, -1, -1, {}, keyNames[(size_t) b], {} });
    return out;
}

/** The keys under the tones as the playing channel has them (rightKeysOf): the pack's switches it may
    throw, each lit where it stands, and the face's own; the rest of the right group's keys hidden. Then
    the rows are laid again. Reads only. */
void Console::showKeys()
{
    const auto standsAt = [this] (int slot) { return juce::roundToInt (getParam (params::selectorId (params::preampId, slot).toRawUTF8())); };

    rightKeys = rightKeysOf (channels.active());
    for (const auto& k : rightKeys)
    {
        if (k.slot < 0)
            continue;       // the face's own: lit as the face says
        auto& b = *buttons[(size_t) k.button];
        b.setEnabled (true);
        b.setOn (standsAt (k.slot) == k.position, false);
        keyNames[(size_t) k.button] = k.title.isEmpty() ? k.name : k.title + " " + k.name;     // the tag says it whole
    }
    layStrips();
}

/** One of the keys right of the filters, thrown by the hand: the pack's switch moved to the position
    the key is — its other position, for an off/on key let up. A position's key pressed while lit stays
    lit: the switch leaves it only for another of its keys. */
void Console::claimKey (int key, const juce::String& name)
{
    const auto k = (size_t) key;
    claimed[k] = true;
    keyNames[k] = name;
    buttons[k]->onChange = nullptr;
    buttons[k]->setEnabled (true);
    showKeys();
}

void Console::keyThrown (int key, bool on)
{
    const auto it = std::find_if (rightKeys.begin(), rightKeys.end(), [key] (const RightKey& k) { return k.button == key; });
    if (it == rightKeys.end() || it->slot < 0)
        return;

    const auto pk = *it;
    const int to = on ? pk.position : pk.off;
    if (to < 0)
    {
        buttons[(size_t) key]->setOn (true, false);
        return;
    }

    if (auto* p = amp.apvts.getParameter (params::selectorId (params::preampId, pk.slot)))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) to));
        p->endChangeGesture();      // ...and the switch's follower lights the key it stands on now
    }
}

/** THE ROWS (Strip.h), for any channel: its pack's tones, as many as the face's row shows, each knob's
    box and its name's; its right keys likewise, and the frame round them; and the filters, where they
    always are. Nothing on the face moves here. */
std::vector<float> Console::toneCurve (const namz::rig::Tone& tone)
{
    using namz::rig::SectionKind;
    struct Band { SectionKind kind; double hz, q, db, pivot; };
    std::vector<Band> bands;
    std::vector<double> measured;       // ...or a position's own curve, on the pack's grid
    if (! tone.sections.empty())
        for (const auto& s : tone.sections)     // a knob: at the top of its travel
            bands.push_back ({ s.kind, s.hz, s.q, std::abs (s.dbAtMax) > 1.0e-6 ? s.dbAtMax : s.dbAtMin, s.pivot });
    else if (! tone.positions.empty())          // a switch: at its last position
    {
        const auto& last = tone.positions.back();
        for (const auto& s : last.sections)
            bands.push_back ({ s.kind, s.hz, s.q, s.gainDb, s.pivot });
        measured = last.db;
    }

    // The cookbook's analogue shapes, |H| in dB at w = f / f0.
    const auto bandDb = [] (const Band& b, double f)
    {
        if (b.hz <= 0.0 || b.q <= 0.0)
            return 0.0;
        const double w = f / b.hz, w2 = w * w, A = std::pow (10.0, b.db / 40.0), sA = std::sqrt (A);
        const auto db = [] (double num, double den) { return den > 0.0 && num > 0.0 ? 10.0 * std::log10 (num / den) : 0.0; };
        const double sh = juce::square (sA * w / b.q);
        switch (b.kind)
        {
            case SectionKind::Bell:      return db (juce::square (1.0 - w2) + juce::square (w * A / b.q), juce::square (1.0 - w2) + juce::square (w / (A * b.q)));
            case SectionKind::LowShelf:  return db (A * A * (juce::square (A - w2) + sh), juce::square (1.0 - A * w2) + sh);
            case SectionKind::HighShelf: return db (A * A * (juce::square (1.0 - A * w2) + sh), juce::square (A - w2) + sh);
            case SectionKind::Tilt:      return db (A * A * (juce::square (1.0 - A * w2) + sh), juce::square (A - w2) + sh) - b.pivot * b.db;
        }
        return 0.0;
    };

    constexpr int n = 40;
    constexpr double fLo = 30.0, fHi = 16000.0;
    std::vector<double> dB;
    for (int i = 0; i < n; ++i)
    {
        const double f = fLo * std::pow (fHi / fLo, (double) i / (double) (n - 1));
        double v = 0.0;
        for (const auto& b : bands)
            v += bandDb (b, f);
        if (bands.empty() && measured.size() >= 2)      // the measured curve, read on its own log grid
        {
            const double g0 = tone.grid.fLo > 0.0 ? tone.grid.fLo : 20.0, g1 = tone.grid.fHi > g0 ? tone.grid.fHi : 20000.0;
            const double at = juce::jlimit (0.0, 1.0, std::log (f / g0) / std::log (g1 / g0)) * (double) (measured.size() - 1);
            const auto k = (size_t) juce::jmin ((int) at, (int) measured.size() - 2);
            v = measured[k] + (measured[k + 1] - measured[k]) * (at - (double) k);
        }
        dB.push_back (v);
    }

    const auto [lo, hi] = std::minmax_element (dB.begin(), dB.end());
    std::vector<float> curve;
    for (const double v : dB)
        curve.push_back (*hi - *lo < 0.1 ? 0.5f : (float) ((v - *lo) / (*hi - *lo)));
    return curve;
}

Strips Console::stripsFor (int channel) const
{
    Strips out;
    const auto& p = places;

    const auto names = channels.toneNamesOf (channel);
    const auto packTones = channels.tonesOf (channel);
    const auto tonesLaid = lay (p.toneStrip, names.size());
    out.toneScale = tonesLaid.scale;
    out.toneSignHeight = p.toneSignHeight;
    const float toneRoom = p.toneStrip.pitch * tonesLaid.scale - 8.0f;
    for (size_t i = 0; i < tonesLaid.centres.size(); ++i)
    {
        const auto c = tonesLaid.centres[i];
        const float k = p.toneKnob * tonesLaid.scale;
        out.tones.push_back ({ juce::Rectangle<float> (k, k).withCentre (c),
                               juce::Rectangle<float> (toneRoom, 28.0f).withCentre ({ c.x, p.toneNameY + p.toneSignDrop }),
                               names[(int) i].toUpperCase(), (int) i, channels.toneOf (channel, (int) i),
                               i < packTones.size() ? toneCurve (packTones[i]) : std::vector<float>() });
    }

    const auto keyAt = [&p] (juce::Point<float> c, float size, int index, const juce::String& name, bool live, bool under)
    {
        const bool over = p.keyNamesAlternate && index % 2 == 1 && ! under;
        const auto label = p.keyNamesOnKeys ? juce::Rectangle<float> (size, size).withCentre (c)
                                            : juce::Rectangle<float> (p.keyNameRoom, 20.0f).withCentre ({ c.x, over ? p.keyNameOverY : p.keyNameUnderY });
        return Strips::Key { juce::Rectangle<float> (size, size).withCentre (c), label, name, live, p.keyNamesOnKeys };
    };
    for (int f = btHpf; f < btFirstRight; ++f)      // the filters' names are on their keys, on every face
    {
        const auto box = juce::Rectangle<float> (p.filterKeyWidth > 0.0f ? p.filterKeyWidth : p.keySize,
                                                 p.filterKeyHeight > 0.0f ? p.filterKeyHeight : p.keySize).withCentre (p.keys[(size_t) f]);
        out.keys.push_back ({ box, box, switches[(size_t) f].shortName, true, true });
    }
    out.firstRightKey = (int) out.keys.size();

    const auto right = channel == channels.active() ? rightKeys : rightKeysOf (channel);
    const auto keysLaid = lay (p.keyStrip, (int) right.size());
    out.keyScale = keysLaid.scale;
    for (size_t i = 0; i < keysLaid.centres.size(); ++i)
        out.keys.push_back (keyAt (keysLaid.centres[i], p.keySize * keysLaid.scale, (int) i, right[i].name, true,
                                   right[i].title.isNotEmpty()));

    // A titled switch's name, once over its keys: its positions' names are all under them (keyAt), so the
    // row over them is the title's alone. Where the names are on the keys' faces, or the face has no row
    // over its keys, it stands just over the keys.
    for (size_t i = 0; i < keysLaid.centres.size();)
    {
        size_t end = i + 1;
        while (end < keysLaid.centres.size() && right[end].slot == right[i].slot && right[end].title == right[i].title)
            ++end;
        if (right[i].title.isNotEmpty())
        {
            const float size = p.keySize * keysLaid.scale;
            const float x0 = keysLaid.centres[i].x, x1 = keysLaid.centres[end - 1].x;
            const float y = p.keyNamesOnKeys || p.keyNameOverY <= 0.0f ? keysLaid.centres[i].y - 0.5f * size - 12.0f
                                                                       : p.keyNameOverY;
            out.keyTitles.push_back ({ juce::Rectangle<float> (x1 - x0 + p.keyStrip.pitch * keysLaid.scale, 20.0f)
                                           .withCentre ({ 0.5f * (x0 + x1), y }),
                                       right[i].title });
        }
        i = end;
    }
    if (! keysLaid.centres.empty() && p.keyFrameHeight > 0.0f)
    {
        const float s = keysLaid.scale, half = 0.5f * p.keyStrip.pitch * s;
        const float left = keysLaid.centres.front().x - half - p.keyCapLeft * s;
        const float frameRight = keysLaid.centres.back().x + half + p.keyCapRight * s;
        out.keyFrame = { left, p.keyStrip.area.getCentreY() - 0.5f * p.keyFrameHeight * s, frameRight - left, p.keyFrameHeight * s };
    }
    return out;
}

/** The playing channel's rows laid, and everything that stands on them put there: the tone knobs, the
    right group's keys — the ones past what the row shows, and the ones no switch has, hidden — the
    picture's sockets, frame and names, and whatever the face draws round them. */
void Console::layStrips()
{
    strips = stripsFor (channels.active());

    for (size_t t = 0; t < tones.size(); ++t)
    {
        const bool shown = t < strips.tones.size();
        if (shown)
        {
            tones[t]->setBounds (box (strips.tones[t].knob));
            toneSpots[t]->setBounds (box (strips.tones[t].label));
        }
        tones[t]->setVisible (shown);
        toneSpots[t]->setVisible (shown);
    }

    std::array<bool, numKeys> shown {};
    for (int f = btHpf; f < btFirstRight; ++f)
        shown[(size_t) f] = true;
    for (size_t i = 0; i + (size_t) strips.firstRightKey < strips.keys.size() && i < rightKeys.size(); ++i)
    {
        const auto b = (size_t) rightKeys[i].button;
        shown[b] = true;
        buttons[b]->setBounds (box (strips.keys[(size_t) strips.firstRightKey + i].box));
    }
    for (size_t b = (size_t) btFirstRight; b < buttons.size(); ++b)
        buttons[b]->setVisible (shown[b]);

    picture->setStrips (strips);
    stripsLaid (strips);
}

void Console::showRegisterMenu (int index)
{
    auto& history = amp.history;
    const auto letter = [] (int i) { return juce::String::charToString ((juce::juce_wchar) ('A' + i)); };

    juce::PopupMenu menu;
    for (int i = 0; i < history.numRegisters(); ++i)
        if (i != index)
        {
            juce::PopupMenu::Item item ("Copy " + letter (index) + " to " + letter (i));
            item.itemID = i + 1;
            item.colour = lilac;
            item.action = [safe = juce::Component::SafePointer<Console> (this), index, i]
            {
                if (safe != nullptr)     // the action is posted: the window may be gone by the time it runs
                    safe->amp.history.copyRegister (index, i);
            };
            menu.addItem (std::move (item));
        }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (overhead[(size_t) (ovFirstRegister + index)].get()));
}

bool Console::keyPressed (const juce::KeyPress& key)
{
    using Mods = juce::ModifierKeys;

    // The history — the same two steps the overhead keys take. (Ctrl on Windows and Linux; and there
    // Ctrl+Y is redo as well, as hands expect.)
    if (key == juce::KeyPress ('z', Mods::commandModifier | Mods::shiftModifier, 0)
        || key == juce::KeyPress ('y', Mods::commandModifier, 0))
    {
        endTapping();
        amp.history.redo();
        return true;
    }
    if (key == juce::KeyPress ('z', Mods::commandModifier, 0))
    {
        endTapping();
        amp.history.undo();
        return true;
    }

    // Full screen, where the product stands alone: Cmd+Ctrl+F as a Mac has it, F11 as the others do.
    // Esc leaves it.
    if (key == juce::KeyPress ('f', Mods::commandModifier | Mods::ctrlModifier, 0) || key == juce::KeyPress (juce::KeyPress::F11Key))
    {
        setFullScreen (! fullScreen);
        return true;
    }
    if (fullScreen && key == juce::KeyPress::escapeKey)
    {
        setFullScreen (false);
        return true;
    }

    // The number keys, and whatever else picks a channel on this face: the product's.
    if (! key.getModifiers().isAnyModifierKeyDown())
    {
        if (channelKey (key))
            return true;

        // A B C D: that register plays, as its key under the lamp does. The four keys are the same four
        // whatever the layout prints on them — under a Russian one they arrive as Ф И С В.
        static constexpr juce::juce_wchar latin[numRegisterKeys]   { 'A', 'B', 'C', 'D' };
        static constexpr juce::juce_wchar russian[numRegisterKeys] { 0x0424, 0x0418, 0x0421, 0x0412 };
        const auto letter = juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) key.getKeyCode());
        for (int r = 0; r < numRegisterKeys; ++r)
            if (letter == latin[r] || letter == russian[r])
            {
                endTapping();
                amp.history.switchTo (r);
                return true;
            }

        // H: the hum, OFF and back — Р under a Russian layout, the same key; only while the build has HUM
        // (Features.h), and without it the key goes on to the host. G: the gate — П.
        if (features::hum && (letter == 'H' || letter == 0x0420))
        {
            flipHum();
            return true;
        }
        if (letter == 'G' || letter == 0x041f)
        {
            flipGate();
            return true;
        }
    }

    return false;
}

float Console::stageScale() const
{
    // In a window of the face's shape the width decides, as it always has: the height is that shape
    // rounded to a pixel, and fitting by it would leave a hair of window at the sides.
    const float byWidth = (float) getWidth() / stage::width;
    return stage::height * byWidth <= (float) getHeight() + 1.0f ? byWidth
                                                                  : (float) getHeight() / stage::height;
}

juce::Point<float> Console::stageOrigin() const
{
    // On whole pixels: the face's cached picture is copied to the screen, not resampled.
    const float s = stageScale();
    return { std::round (juce::jmax (0.0f, ((float) getWidth()  - stage::width  * s) * 0.5f)),
             std::round (juce::jmax (0.0f, ((float) getHeight() - stage::height * s) * 0.5f)) };
}

void Console::paint (juce::Graphics& g)
{
    const auto faceArea = juce::Rectangle<float> (stage::width, stage::height) * stageScale() + stageOrigin();
    if (faceArea.getWidth() + 1.0f >= (float) getWidth() && faceArea.getHeight() + 1.0f >= (float) getHeight())
        return;

    g.excludeClipRegion (faceArea.reduced (1.0f).toNearestInt());      // the product's window is under there
    g.fillAll (juce::Colours::black);
}

void Console::resized()
{
    stage.setTransform (juce::AffineTransform::scale (stageScale()).translated (stageOrigin()));
    stagePlaced();
    if (sheet != nullptr)
        sheet->setBounds (getLocalBounds());
}

/** FULL SCREEN, and back. The standalone's window follows the face's size and the face keeps its
    shape; in full screen it is the other way round — the window is the screen's, whatever its shape —
    so for as long as it lasts the face lets go of its shape and its limits and takes what the
    window gives it, centring its stage (stageScale). JUCE's kiosk mode does the rest: it hides the
    title bar, the menu bar and the Dock, and puts the window back where it was on the way out. */
void Console::setFullScreen (bool on)
{
    auto* window = dynamic_cast<juce::DocumentWindow*> (getTopLevelComponent());
    if (on == fullScreen || window == nullptr || amp.runsInHost())
        return;

    fullScreen = on;

    auto& desktop = juce::Desktop::getInstance();
    if (on)
    {
        windowedAt = window->getPosition();
        getConstrainer()->setFixedAspectRatio (0.0);
        getConstrainer()->setSizeLimits (stage::minWindowWidth, juce::roundToInt ((float) stage::minWindowWidth * stage::height / stage::width),
                                         0x3fffffff, 0x3fffffff);
        desktop.setKioskModeComponent (window, false);
    }
    else
    {
        getConstrainer()->setFixedAspectRatio ((double) stage::width / (double) stage::height);
        getConstrainer()->setSizeLimits (stage::minWindowWidth, juce::roundToInt ((float) stage::minWindowWidth * stage::height / stage::width),
                                         stage::readingWidth * 2, juce::roundToInt ((float) stage::readingWidth * 2.0f * stage::height / stage::width));
        desktop.setKioskModeComponent (nullptr);
        setBoundsConstrained (getBounds());
        standalone::rememberWindowPosition (windowedAt);
    }
}

void Console::mouseDown (const juce::MouseEvent& e)
{
    const auto at = e.getEventRelativeTo (&stage).getPosition();

    // The tuner's screen is its switch: a click turns it on or off; the right click has the rest.
    // Anywhere else on the bare picture, the product may have a use for a click (pictureClicked).
    if (! e.mods.isPopupMenu())
    {
        if (e.originalComponent == &stage && tuner.getBounds().contains (at))
        {
            endTapping();
            if (places.tunerMenuOnClick)
                showTunerMenu();
            else
                setParam (params::tunerOn, getParam (params::tunerOn) > 0.5f ? 0.0f : 1.0f);
        }
        else if (e.originalComponent == &stage)
        {
            pictureClicked (at);
        }
        return;
    }

    // The product's own first; then CAB's name, a reading that takes no clicks of its own — a right
    // click on it is one on CAB. So with a column's meter: it is the same column as the lever beside it.
    if (e.originalComponent == &stage && pictureRightClicked (at))
        return;
    if (cabName.getBounds().contains (at))
        showCabs();
    else if (inMeter.getBounds().contains (at))
        showInputMenu();
    else if (outMeter.getBounds().contains (at))
        showOutputMenu();
    else if (tuner.getBounds().contains (at))
        showTunerMenu();
    else
        showSettingsMenu();
}

/** A DOUBLE CLICK ON GATE'S LAMP: LEARN, as from GATE's menu. The lamp takes no clicks of its own, so it
    is the panel under it that hears them. */
void Console::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || e.originalComponent != &stage)
        return;

    if (gateLight.getBounds().contains (e.getEventRelativeTo (&stage).getPosition()))
    {
        endTapping();
        startGateLearn();
    }
}

void Console::playDemo (int loop)
{
    const bool start = loop >= 0 || ! amp.demo.isPlaying();
    if (loop >= 0 || (start && amp.demoLoop() < 0))
        amp.selectDemoLoop (juce::jmax (0, loop));      // none picked yet: the first
    amp.demo.setPlaying (start);
}

/** THE WINDOW SECTION, as a face with nothing of its own to show there has it: full screen. */
void Console::addWindowSections (juce::PopupMenu& menu)
{
    if (amp.runsInHost())
        return;

    menu.addSectionHeader ("WINDOW");
    addChoice (menu, "Full screen", fullScreen, [] (Console& e) { e.setFullScreen (! e.fullScreen); });
}

/** THE SETTINGS — what is set once for the machine and the room. The gate and the stereo mode are the
    engine's own and are part of the sound; what the product's window shows is this machine's and is
    not. */
void Console::showSettingsMenu()
{
    endTapping();

    const auto choice = [this] (const char* id) { return juce::roundToInt (getParam (id)); };

    juce::PopupMenu menu;

    // What the product's window shows, and full screen.
    addWindowSections (menu);

    // How DRIVE goes between the pack's captures, and the hum the input carries. How hard TURBO pushes,
    // where it pushes, is TURBO's own menu's (showTurboMenu).
    menu.addSectionHeader ("INPUT");

    juce::PopupMenu way;
    for (int i = 0; i < driveWays.size(); ++i)
        addChoice (way, driveWays[i], (i == 1) == driveSteps, [steps = i == 1] (Console& e) { e.chooseDriveSteps (steps); });
    menu.addSubMenu ("DRIVE turns  " + driveWays[driveSteps ? 1 : 0], way);

    // The hum is the machine's (prefs::hum): every window of this product and
    // every engine behind one follow the choice from their next look and their next block. The title
    // says where it stands — AUTO's state with it. Only while the build has HUM (Features.h).
    if constexpr (features::hum)
    {
        juce::PopupMenu hum;
        addHumLines (hum);
        menu.addSubMenu ("HUM  " + prefs::humState (amp.product(), amp.humHeard()), hum);
    }

    menu.addSectionHeader ("OUTPUT");
    for (int i = 0; i < params::stereoModes.size(); ++i)
        addChoice (menu, params::stereoModes[i], choice (params::stereoMode) == i,
                   [i] (Console& e) { e.setParam (params::stereoMode, (float) i); });

    // The gate: LEARN, and whether it stands in the chain. Where it closes, how fast and where it mutes
    // are GATE's own menu's (showGateMenu).
    menu.addSectionHeader ("GATE");
    const bool gateIsOn = getParam (params::gateOn) > 0.5f;
    addChoice (menu, "Learn", learner.isLearning(), [] (Console& e) { e.startGateLearn(); });
    addChoice (menu, "On", gateIsOn, [gateIsOn] (Console& e) { e.setParam (params::gateOn, gateIsOn ? 0.0f : 1.0f); });

    // The MIDI inputs are the standalone's; a host routes MIDI itself.
    if (MidiPanel::available())
    {
        menu.addSeparator();
        addChoice (menu, "MIDI...", false, [] (Console& e) { e.showMidi(); });
    }

    // A loop through the amp in place of the guitar, where the loops are — see playDemo.
    if (params::demoLoopsPresent())
    {
        menu.addSectionHeader ("DEMO");
        addChoice (menu, "Play", amp.demo.isPlaying(), [] (Console& e) { e.playDemo (-1); });
        for (int i = 0; i < params::demoLoops.size(); ++i)
            addChoice (menu, params::demoLoops[i], i == juce::jmax (0, amp.demoLoop()), [i] (Console& e) { e.playDemo (i); });
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

void Console::setParam (const char* paramId, float value)
{
    if (auto* p = amp.apvts.getParameter (paramId))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

float Console::getParam (const char* paramId) const
{
    if (auto* p = amp.apvts.getParameter (paramId))
        return p->convertFrom0to1 (p->getValue());
    return 0.0f;
}

/** THE COCKPIT'S CHAIN IS FIXED: one captured amp (the boost now lives inside the packs), the delay,
    a hall, the cabinet. There is no strip to switch links in or out, so the face puts them there. */
void Console::chooseCab (int step)
{
    channels.chooseCabinet (step);
    channels.setCabinetOn (true);       // a cabinet picked is a cabinet wanted: it comes out of bypass
}

/** CAB has a position for every cabinet the product has, and the list has just been read again — a
    file added, renamed or taken away. A selector needs two to be one: with fewer it stands dim. */
void Console::cabinetsChanged()
{
    auto& cab = *sliders[2];
    const int count = channels.cabinets().size();
    const bool choice = count >= 2;

    cab.setSteps (juce::jmax (2, count));
    cab.onStep = choice ? std::function<void (int)> ([this] (int s) { chooseCab (s); showCab(); }) : nullptr;
    cab.setEnabled (choice);
    cab.setAlpha (choice ? 1.0f : 0.35f);
    showChannel();
}

/** ADD: the files a player points at, taken in the way dropped ones are. */
void Console::chooseCabsToAdd()
{
    auto folder = juce::File (prefs::store (amp.product()).get (cabFolderKey, juce::String()).toString());
    if (! folder.isDirectory())
        folder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

    cabChooser = std::make_unique<juce::FileChooser> ("Add cabinets", folder, "*.wav;*.aif;*.aiff;*.zip");
    cabChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                 | juce::FileBrowserComponent::canSelectDirectories
                                 | juce::FileBrowserComponent::canSelectMultipleItems,
                             [safe = juce::Component::SafePointer<Console> (this)] (const juce::FileChooser& picked)
                             {
                                 juce::StringArray paths;
                                 for (const auto& file : picked.getResults())
                                     paths.add (file.getFullPathName());
                                 if (safe != nullptr && ! paths.isEmpty())
                                     safe->addCabinets (paths);
                             });
}

/** RENAME: the file of a cabinet of the player's own, under another name. The sound does not move, and
    every channel that was left on that cabinet goes with it (Channels::renameCabinet). */
void Console::renameCab()
{
    const int playing = channels.cabinet();
    if (playing < channels.factoryCabinets())
        return;

    const auto file = channels.cabinets()[playing];
    felitronics::appkit::textPrompt ("Rename the cabinet", file.getFileNameWithoutExtension(),
                                     [safe = juce::Component::SafePointer<Console> (this), file] (juce::String name)
    {
        // The prompt is a window of its own and answers later: the face may be gone by then, and the
        // list may have been read again — the cabinet is found by its file, not by where it stood.
        if (safe == nullptr)
            return;
        auto& e = *safe;
        const int index = e.channels.cabinets().indexOf (file);

        bool renamed = false;
        {
            const felitronics::appkit::CompareHistory::ScopedSuppress quiet (e.amp.history);
            renamed = e.channels.renameCabinet (index, name);
        }

        if (renamed)
            e.cabinetsChanged();
        else
            juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                              .withIconType (juce::MessageBoxIconType::WarningIcon)
                                              .withTitle ("Not renamed")
                                              .withMessage (name + "\n\nThere is a cabinet of that name already, or the file would not move.")
                                              .withButton ("OK"),
                                          nullptr);
    });
}

/** REMOVE: the file of a cabinet of the player's own, to the Trash — asked first. The sound goes on to
    the cabinet that takes its place in the list: a state keeps its cabinet's bytes, and without that
    the plate would go on naming a file that is no longer there. */
void Console::removeCab()
{
    const int playing = channels.cabinet();
    if (playing < channels.factoryCabinets())
        return;

    const auto file = channels.cabinets()[playing];
    juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::QuestionIcon,
                                                                                "Remove the cabinet?",
                                                                                Channels::cabinetName (file) + "\n\nIts file goes to the Trash.",
                                                                                "Remove", "Cancel"),
                                  [safe = juce::Component::SafePointer<Console> (this), file] (int answer)
    {
        if (safe == nullptr || answer != 1)
            return;
        auto& e = *safe;
        const int index = e.channels.cabinets().indexOf (file);
        if (! e.channels.removeCabinet (index))
            return;

        e.cabinetsChanged();
        if (const int left = e.channels.cabinets().size(); left > 0)
        {
            e.channels.chooseCabinet (juce::jmin (index, left - 1));    // not chooseCab: a file removed is no reason to leave bypass
            e.showChannel();
        }
    });
}

void Console::addCabinets (const juce::StringArray& paths)
{
    const auto added = channels.importCabinets (paths);
    cabinetsChanged();

    if (added.first >= 0)       // the first of the new ones plays: that is what the hand was asking to hear
    {
        chooseCab (added.first);
        showChannel();
    }

    if (! added.refused.isEmpty())
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::WarningIcon)
                                          .withTitle ("Not added")
                                          .withMessage (added.refused.joinIntoString ("\n")
                                                        + "\n\nA cabinet is kept inside the session it plays in, and an IR over "
                                                        + juce::String (device::EmbeddedIrs::maxBytes / (1024 * 1024)) + " MB does not fit.")
                                          .withButton ("OK"),
                                      nullptr);
}

bool Console::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& path : files)
        if (device::IrLibrary::looksLikeImport (juce::File (path)))
            return true;
    return false;
}

void Console::fileDragEnter (const juce::StringArray&, int, int)
{
    const auto plate = cabName.getBounds().toFloat();
    nameTag.show ({ plate.getCentreX(), plate.getY() - 22.0f }, "DROP TO ADD A CABINET");
}

void Console::fileDragExit (const juce::StringArray&)
{
    nameTag.show ({}, {});
}

void Console::filesDropped (const juce::StringArray& files, int, int)
{
    nameTag.show ({}, {});
    addCabinets (files);
}

/** CAB's plate says which cabinet is playing: the name its file carries, without the number that
    orders it. */
void Console::showCab()
{
    const auto& cabinets = channels.cabinets();
    const int step = sliders[2] != nullptr ? sliders[2]->getStep() : -1;
    cabName.set ("CAB", ! channels.cabinetOn() ? juce::String ("OFF")      // bypassed: see the cabinets' window
                        : step >= 0 && step < cabinets.size() ? Channels::cabinetName (cabinets.getReference (step))
                                                              : juce::String());
}

template <typename Control>
void Console::bind (Control& control, const juce::String& paramId)
{
    if (auto att = attach (control, paramId))
        attachments.push_back (std::move (att));
}

template <typename Control>
std::unique_ptr<juce::ParameterAttachment> Console::attach (Control& control, const juce::String& paramId)
{
    auto* p = amp.apvts.getParameter (paramId);
    jassert (p != nullptr);   // a binding to an id the processor does not have is a typo, not an option
    if (p == nullptr)
        return {};

    auto att = std::make_unique<juce::ParameterAttachment> (*p, [&control, p] (float v)
    {
        control.setValue (p->convertTo0to1 (v), false);
    });

    auto* a = att.get();
    control.onGestureStart = [a] { a->beginGesture(); };
    control.onChange       = [a, p] (float v) { a->setValueAsPartOfGesture (p->convertFrom0to1 (v)); };
    control.onGestureEnd   = [a] { a->endGesture(); };
    control.defaultValue   = amp.engineDefault (*p);

    a->sendInitialUpdate();
    return att;
}

/** The tone knobs on the playing pack's tones, knob i on its tone i (Channels::toneOf), as many as it
    has: only a knob whose parameter is another now is bound again, and a knob past the pack's list is
    bound to nothing and hidden (layStrips). */
void Console::bindTones()
{
    for (size_t t = 0; t < tones.size(); ++t)
    {
        auto& knob = *tones[t];
        if (const auto id = channels.toneId ((int) t); id.isEmpty())
        {
            knob.onGestureStart = nullptr;
            knob.onChange       = nullptr;
            knob.onGestureEnd   = nullptr;
            toneBindings[t].reset();
            toneBound[t] = {};
        }
        else if (id != toneBound[t])
        {
            toneBindings[t] = attach (knob, id);
            toneBound[t] = id;
        }
    }
}

juce::ParameterAttachment* Console::follow (const char* paramId, std::function<void (float)> moved)
{
    auto* p = amp.apvts.getParameter (paramId);
    jassert (p != nullptr);
    attachments.push_back (std::make_unique<juce::ParameterAttachment> (*p, std::move (moved)));
    return attachments.back().get();
}

/** What the delay's plate says, and where the three stand: the word and the note value always; the
    tempo only while it is the delay's own to set — a host that is conducting has the say, and there is
    nothing to drag or tap. */
void Console::showDelayTime()
{
    delayWord.set ("DELAY");
    delayNote.set ({}, params::delayDivisions[juce::jlimit (0, params::delayDivisions.size() - 1,
                                                            juce::roundToInt (getParam (params::delayDiv)))]);

    tempoIsOwn = ! (amp.runsInHost() && amp.hostTempoBpm() > 0.0f);
    delayTempo.setVisible (tempoIsOwn);
    delayTempo.set ({}, juce::String (juce::roundToInt (getParam (params::delayBpm))) + " BPM");

    // Side by side, the row centred over the plate.
    const auto row = places.sliderNames[1];
    const float pad = 8.0f, gapNote = 6.0f, gapTempo = 22.0f;
    const float wordWidth = delayWord.idealWidth() + pad, noteWidth = delayNote.idealWidth() + pad;
    const float tempoWidth = tempoIsOwn ? delayTempo.idealWidth() + pad : 0.0f;

    float x = row.getCentreX() - (wordWidth + gapNote + noteWidth + (tempoIsOwn ? gapTempo + tempoWidth : 0.0f)) * 0.5f;
    const auto place = [&] (Legend& legend, float width, float gapAfter)
    {
        legend.setBounds (juce::Rectangle<float> (x, row.getY(), width, row.getHeight()).toNearestInt());
        x += width + gapAfter;
    };
    place (delayWord, wordWidth, gapNote);
    place (delayNote, noteWidth, gapTempo);
    place (delayTempo, tempoWidth, 0.0f);
}

/** A TAP ON THE WORD. The tempo is the mean of the last few gaps between taps — the first tap only
    starts the count, and a pause longer than the slowest beat worth counting starts it again. */
void Console::tapTempo()
{
    if (! tempoIsOwn)
        return;     // a host is conducting: the beat is not ours to give

    constexpr double longestGapMs = 2000.0;
    constexpr size_t gapsKept = 4;

    const double now = juce::Time::getMillisecondCounterHiRes();
    if (! tapping)
    {
        amp.history.beginGesture ("Tap tempo");
        tapping = true;
        tapGaps.clear();
    }
    else if (now - lastTap <= longestGapMs)
    {
        tapGaps.push_back (now - lastTap);
        if (tapGaps.size() > gapsKept)
            tapGaps.erase (tapGaps.begin());

        double mean = 0.0;
        for (const double gap : tapGaps)
            mean += gap / (double) tapGaps.size();
        delayBpm->setValueAsCompleteGesture (juce::jlimit (params::delayBpmMin, params::delayBpmMax,
                                                           (float) std::round (60000.0 / mean)));
    }

    lastTap = now;
    delayWord.flash();
    tapPause.startTimer ((int) longestGapMs);
}

/** The taps have stopped — or something else is about to move the state, and the count must not be
    left open across it. Closes the gesture: everything the taps did is one undo step. */
void Console::endTapping()
{
    if (! tapping)
        return;
    tapping = false;
    tapPause.stopTimer();
    tapGaps.clear();
    amp.history.endGesture();
}

void Console::showDelayMenu()
{
    const int div = juce::roundToInt (getParam (params::delayDiv));

    juce::PopupMenu menu;
    for (int i = 0; i < params::delayDivisions.size(); ++i)
    {
        juce::PopupMenu::Item item (params::delayDivisions[i]);
        item.itemID   = i + 1;
        item.colour   = lilac;
        item.isTicked = i == div;
        menu.addItem (std::move (item));
    }

    // In a host, whose beat those are counted in — said, not chosen: the host conducts.
    if (amp.runsInHost())
    {
        const float hostBpm = amp.hostTempoBpm();
        menu.addSeparator();
        menu.addItem (-1, hostBpm > 0.0f ? "HOST  " + juce::String (hostBpm, 1) + " BPM"
                                         : juce::String ("HOST  (no tempo)"), false, false);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&delayNote),
                        [safe = juce::Component::SafePointer<Console> (this)] (int picked)
                        {
                            if (safe != nullptr && picked > 0)
                                safe->delayDiv->setValueAsCompleteGesture ((float) (picked - 1));
                        });
}

/** EVERYTHING ABOUT PRESETS IS IN HERE: the plate only says the word. Whose sound this is stands at the
    top; then the list, the one that is loaded ticked; then what can be done with it. */
void Console::showPresetMenu()
{
    enum : int { itemSave = 1, itemSaveAs, itemRename, itemDelete, itemInit, firstPreset = 100 };

    const auto names   = presets.names();
    const auto current = presets.current();
    const bool onDisk  = presets.exists (current);

    const auto itemFor = [] (const juce::String& text, int id, bool ticked = false, bool enabled = true)
    {
        juce::PopupMenu::Item item (text);
        item.itemID    = id;
        item.colour    = lilac;
        item.isTicked  = ticked;
        item.isEnabled = enabled;
        return item;
    };

    juce::PopupMenu menu;
    menu.addSectionHeader (current.isEmpty() ? juce::String ("No preset")
                                             : current + (presets.changed() ? "  (changed)" : ""));
    for (int i = 0; i < names.size(); ++i)
        menu.addItem (itemFor (names[i], firstPreset + i, names[i] == current));

    // Save writes back to the loaded preset; Save as always asks for a name. Rename and Delete are
    // GREY, not absent, when the sound is not a saved preset — a row that vanishes says nothing about why.
    menu.addSeparator();
    menu.addItem (itemFor ("Save", itemSave));
    menu.addItem (itemFor (juce::String::fromUTF8 ("Save as\xe2\x80\xa6"), itemSaveAs));
    menu.addItem (itemFor (juce::String::fromUTF8 ("Rename\xe2\x80\xa6"), itemRename, false, onDisk));
    menu.addItem (itemFor ("Delete", itemDelete, false, onDisk));
    menu.addSeparator();
    menu.addItem (itemFor ("Init", itemInit));

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetDoor),
                        [safe = juce::Component::SafePointer<Console> (this), names, current, onDisk] (int picked)
                        {
                            if (safe == nullptr || picked <= 0)
                                return;

                            safe->endTapping();

                            // The prompt is a window of its own and answers later: the face may be gone by then.
                            const auto ask = [safe] (const juce::String& title, const juce::String& initial,
                                                     bool (Presets::* then) (const juce::String&))
                            {
                                felitronics::appkit::textPrompt (title, initial, [safe, then] (juce::String name)
                                {
                                    if (safe != nullptr)
                                    {
                                        (safe->presets.*then) (name.trim());
                                        safe->timerCallback();
                                    }
                                });
                            };

                            if (picked >= firstPreset)                  safe->presets.load (names[picked - firstPreset]);
                            else if (picked == itemSave && onDisk)      safe->presets.save (current);
                            else if (picked == itemSave
                                     || picked == itemSaveAs)           ask ("Save preset", current, &Presets::save);
                            else if (picked == itemRename)              ask ("Rename preset", current, &Presets::rename);
                            else if (picked == itemDelete)
                            {
                                // A file gone is gone — undo does not bring it back — so this one asks.
                                juce::AlertWindow::showAsync (
                                    juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::QuestionIcon,
                                                                                  "Delete preset",
                                                                                  "Delete \"" + current + "\"? Its file is removed for good.",
                                                                                  "Delete", "Cancel", safe.getComponent()),
                                    [safe] (int answer)
                                    {
                                        if (safe != nullptr && answer == 1)
                                        {
                                            safe->presets.removeCurrent();
                                            safe->timerCallback();
                                        }
                                    });
                            }
                            else if (picked == itemInit)                safe->presets.init();

                            safe->timerCallback();
                        });
}

void Console::showReverbMenu()
{
    const int now = juce::roundToInt (getParam (params::reverbType));

    juce::PopupMenu menu;
    for (int i = 0; i < params::reverbCharacters.size(); ++i)
    {
        juce::PopupMenu::Item item (params::reverbCharacters[i]);
        item.itemID   = i + 1;
        item.colour   = lilac;
        item.isTicked = i == now;
        menu.addItem (std::move (item));
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&reverbName),
                        [safe = juce::Component::SafePointer<Console> (this)] (int picked)
                        {
                            if (safe != nullptr && picked > 0)
                                safe->reverbType->setValueAsCompleteGesture ((float) (picked - 1));
                        });
}

void Console::bindButton (PushButton& button, const char* paramId)
{
    auto* p = amp.apvts.getParameter (paramId);
    jassert (p != nullptr);
    if (p == nullptr)
        return;

    auto att = std::make_unique<juce::ParameterAttachment> (*p, [&button] (float v) { button.setOn (v > 0.5f, false); });
    auto* a = att.get();
    button.onChange = [a] (bool on) { a->setValueAsCompleteGesture (on ? 1.0f : 0.0f); };
    a->sendInitialUpdate();
    attachments.push_back (std::move (att));
}

} // namespace orbitamp::face
