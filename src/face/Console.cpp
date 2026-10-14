// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "Console.h"

#include "Cabs.h"
#include "Midi.h"
#include "Standalone.h"

#include "../Parameters.h"
#include "../Product.h"
#include "../core/ProcessMemory.h"
#include "../device/IrLibrary.h"
#include "../ui/AboutConfig.h"
#include "../ui/LoadBreakdown.h"
#include "../ui/Prefs.h"

#include <felitronics/appkit/TextPrompt.h>

namespace orbitamp::face
{

namespace
{
    // The INPUT lever's zero — see Console.h. Kept with the machine's settings.
    const juce::Identifier leverZeroKey { "input_zero_db" };
    constexpr int leverZeroDefault = -9, leverZeroLowest = -24, leverZeroHighest = 12, leverZeroStep = 3;

    int snapLeverZero (int db)
    {
        db = juce::jlimit (leverZeroLowest, leverZeroHighest, db);
        return leverZeroLowest + leverZeroStep * juce::roundToInt ((float) (db - leverZeroLowest) / (float) leverZeroStep);
    }

    /** The lever's travel against the engine's trim: the bottom of the travel is the trim's bottom, the
        top its top, and the middle whatever `zero` says the lever's zero gives. */
    float leverToDb (float travel, int zero)
    {
        const float z = (float) zero, range = params::inTrimRangeDb;
        return travel <= 0.5f ? juce::jmap (travel * 2.0f, -range, z)
                              : juce::jmap ((travel - 0.5f) * 2.0f, z, range);
    }

    float dbToLever (float db, int zero)
    {
        const float z = (float) zero, range = params::inTrimRangeDb;
        if (db <= z)
            return z > -range ? 0.5f * (db + range) / (z + range) : 0.5f;   // a zero at the very bottom has nothing under it
        return 0.5f + 0.5f * (db - z) / (range - z);
    }

    juce::String decibels (int db) { return (db > 0 ? "+" : "") + juce::String (db) + " dB"; }

    // The ceilings the OUTPUT column's menu offers the limiter, within what the engine takes
    // (params::limiterCeilingMin .. Max).
    constexpr float limiterCeilings[] { -0.1f, -0.3f, -0.5f, -1.0f, -2.0f, -3.0f };

    // TURBO's push — see Console.h. Kept with the machine's settings, where the engine reads it too
    // (channels/Engine.h).
    using channels::settings::turboKey, channels::settings::turboPushes, channels::settings::snapTurbo;

    // How DRIVE goes between the pack's captures — see Console.h. Kept with the machine's settings.
    const juce::Identifier driveStepsKey { "drive_steps" };
    const juce::StringArray driveWays { "Smoothly", "In steps" };

    // WHAT A PRODUCT HANDS OUT SAYS WHERE IT CAME FROM: a capture and a cabinet saved from it are
    // offered a name that starts with the product's own and with what kind of file it is —
    // "CatpitAmp-NamA2-", "CatpitAmp-IR-".
    const juce::String captureNamePrefix = juce::String (product::name) + "-NamA2-";
    const juce::String cabNamePrefix     = juce::String (product::name) + "-IR-";

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

    /** The product's own tag on a family link, for the site's analytics: "utm_source=catpitamp&...". */
    juce::String campaign()
    {
        return "utm_source=" + juce::String (product::name).toLowerCase() + "&utm_medium=plugin";
    }

    /** THE BUILD WINDOW, AS THE PRODUCT'S. What the build is and what it was built against is the same
        for every face of the processor (aboutConfig); what is the product's own is its name, its mark —
        the cockpit's a small starship head-on, its fins a cat's ears — and its name where the notice
        says whose product this is. The window outlives the badge that opens it, so the mark is
        shared, not lent. */
    felitronics::appkit::VersionBadge::Config aboutTheProduct (const juce::String& svg)
    {
        auto config = aboutConfig (product::name, "https://darwinscat.com", campaign().toRawUTF8());

        std::shared_ptr<juce::Drawable> mark { juce::Drawable::createFromImageData (
            svg.toRawUTF8(), svg.getNumBytesAsUTF8()).release() };

        config.drawMark = [mark] (juce::Graphics& g, float cx, float cy, float d)
        {
            if (mark != nullptr)
                mark->drawWithin (g, { cx - d * 0.5f, cy - d * 0.5f, d, d }, juce::RectanglePlacement::centred, 1.0f);
        };
        config.notice = config.notice.replace ("OrbitAmp", product::name);
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
      drive (look.driveKnob, look.drivePointer),
      reverbName (stage::px13),
      cabName (stage::px13),
      delayWord (stage::px13),
      delayNote (stage::px13),
      delayTempo (stage::px13),
      inFader (look.cap),
      outFader (look.cap),
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
      about (p.updateChecker(), aboutTheProduct (look.mark), p.pluginFormat()),
      load (where.readingPx),
      memory (where.readingPx),
      presetDoor (stage::px13)
{
    using namespace stage;

    // Everything this constructor sets is the face settling onto the state, not a player's edit: with
    // undo on the panel it must not leave a step to press back through.
    const felitronics::appkit::CompareHistory::ScopedSuppress settling (amp.history);

    leverZero = snapLeverZero ((int) prefs::store().get (leverZeroKey, leverZeroDefault));
    channels.setTurboDb ((float) channels::settings::turbo());
    driveSteps = (bool) prefs::store().get (driveStepsKey, false);

    channels.readCabinets();    // the player's folder may have gained or lost a file since the engine was built
    engine.setUpChain (channels.adopt());
    channels.align();   // the packs and cabinets are files, and the folders may have changed since the state was saved
    settleOnSettings(); // ...and INPUT's lever may have another zero than the one the state was dialled against

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
    for (size_t i = 0; i < tones.size(); ++i)
    {
        tones[i] = std::make_unique<ArtKnob> (look.toneKnob, look.tonePointer);
        stage.addAndMakeVisible (*tones[i]);
        tones[i]->setBounds (box (places.tones[i]));
    }

    for (size_t i = 0; i < buttons.size(); ++i)
    {
        buttons[i] = std::make_unique<PushButton> (switches[i].name);
        auto& b = *buttons[i];
        stage.addAndMakeVisible (b);
        const auto at = places.keys[i];
        b.setBounds (around (at, places.keySize, places.keySize));
        b.onHover = [this, at, &b] (bool over)     // a key that does not answer the hand says nothing either
        {
            nameTag.show (at.translated (0.0f, -40.0f), over && b.isEnabled() ? b.name + "  " + b.stateText() : juce::String());
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
        stage.addAndMakeVisible (*sliders[i]);
        sliders[i]->setBounds (box (places.sliders[i]));
    }

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
    delayWord.nameColour = orange;
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
    inFader.setBounds (box (places.inFader));
    outFader.setBounds (box (places.outFader));
    inMeter.setBounds (box (places.inMeter));
    outMeter.setBounds (box (places.outMeter));

    for (auto* lamp : { &inClip, &outClip, &gateLight, &limiterLight })
        stage.addAndMakeVisible (lamp);
    inClip.setBounds (box (places.lamps[inClipLamp]));
    outClip.setBounds (box (places.lamps[outClipLamp]));
    gateLight.setBounds (box (places.lamps[gateLamp]));
    limiterLight.setBounds (box (places.lamps[limiterLamp]));

    // What has an obvious home in the engine is wired to it; the rest only moves for now.
    bind (drive, params::preampGain);
    drive.onMenu = [this] { showDriveMenu(); };
    drive.detent = [this] (float travel) { return driveSteps ? (float) amp.preamp.stepAt ((double) travel) : travel; };
    bind (*sliders[0], params::reverbMix);
    bind (*sliders[1], params::delayMix);
    bind (outFader, params::outTrim);

    // INPUT's lever does not read the trim's own scale: its middle is the lever's zero (Console.h).
    inputTrim = follow (params::inTrim, [this] (float db) { inFader.setValue (dbToLever (db, leverZero), false); });
    inFader.onGestureStart = [this] { inputTrim->beginGesture(); };
    inFader.onChange       = [this] (float travel) { inputTrim->setValueAsPartOfGesture (leverToDb (travel, leverZero)); };
    inFader.onGestureEnd   = [this] { inputTrim->endGesture(); };
    inFader.defaultValue   = 0.5f;
    inputTrim->sendInitialUpdate();

    inFader.onMenu = [this] { showInputMenu(); };

    outFader.onMenu = [this] { showOutputMenu(); };

    // Whether the cabinet is in the sound at all is on CAB's plate.
    follow (params::cabOn, [this] (float) { showCab(); });

    // The tuner's two switches are read here and thrown from its menu (showTunerMenu); on and off, also
    // by a click on its screen (mouseDown).
    follow (params::tunerOn,   [this] (float) { showTunerState(); });
    follow (params::tunerMute, [this] (float) { showTunerState(); });
    showTunerState();

    sayLever (inFader,  "INPUT",  [this] { return getParam (params::inTrim) - (float) leverZero; });
    sayLever (outFader, "OUTPUT", [this] { return getParam (params::outTrim); });

    // The tone stack is the playing pack's own — its Bass, Middle and Treble as they were measured on
    // the amp, centre detent where every capture was taken.
    bindTones();

    // HPF and LPF switch the cabinet's own cut filters in and out; FAT, BRIGHT, DEPTH and PRESENCE are
    // kept with the channel and wait for the layers the packs will carry — each dimmed and deaf to the
    // hand until its own arrives (Channels::layerPlayable).
    bindButton (*buttons[btHpf], params::cabHpfOn);
    bindButton (*buttons[btLpf], params::cabLpfOn);
    const std::pair<int, Channels::Flag> layers[] { { btFat, Channels::fat }, { btBright, Channels::bright },
                                                    { btDepth, Channels::depth }, { btPresence, Channels::presence } };
    for (const auto& [key, flag] : layers)
    {
        buttons[(size_t) key]->onChange = [this, f = flag] (bool on) { channels.setFlag (f, on); };
        buttons[(size_t) key]->setEnabled (Channels::layerPlayable[(size_t) flag]);
    }

    showChannel();

    // The mark lights under the pointer and leads to the product's own page on the maker's site.
    stage.addAndMakeVisible (emblem);
    {
        juce::Path face;
        for (size_t i = 0; i < places.emblemFace.size(); ++i)
            i == 0 ? face.startNewSubPath (places.emblemFace[i]) : face.lineTo (places.emblemFace[i]);
        emblem.setBounds (face.getBounds().getSmallestIntegerContainer());
    }
    emblem.onClick = []
    {
        juce::URL ("https://darwinscat.com/felitronics/" + juce::String (product::name).toLowerCase() + "?" + campaign()).launchInDefaultBrowser();
    };

    // The version, and behind it the window with everything else about the build. That window is
    // hung on THIS editor rather than on the top-level window: a GL surface lies over everything in
    // the window that is not this editor's own child, and a dialog on the window would open under it.
    about.setBrandTypeface (theme::displayTypeface());
    addChildComponent (about);
    version.upper = false;
    version.valueColour = labelLilac.withAlpha (0.85f);
    version.set ({}, "v" + amp.updateChecker().currentVersion());
    version.onClick = [this] { about.showAbout (this); };
    stage.addAndMakeVisible (version);
    version.setBounds (box (places.version));

    // CPU. A click opens what the number is made of — the window OrbitAmp's footer opens from its DSP
    // figure.
    load.nameColour = labelLilac.withAlpha (0.85f);
    load.fromLeft = true;
    load.onClick = [this] { showLoad(); };
    stage.addAndMakeVisible (load);
    load.setBounds (box (places.load));

    // MEM. A reading and nothing else: a click there is a click on the panel.
    memory.nameColour = memory.valueColour = labelLilac.withAlpha (0.85f);
    memory.fromLeft = true;
    memory.setInterceptsMouseClicks (false, false);
    stage.addChildComponent (memory);
    memory.setBounds (box (places.memory));

    // PRESET.
    presetDoor.valueColour = labelLilac.withAlpha (0.85f);
    presetDoor.set ({}, "PRESET");
    presetDoor.onClick = [this] { showPresetMenu(); };
    stage.addAndMakeVisible (presetDoor);
    presetDoor.setBounds (box (places.preset));

    // GATE and LIMITER, over their lamps.
    for (auto* door : { &gateDoor, &limiterDoor })
    {
        door->valueColour = labelLilac.withAlpha (0.85f);
        stage.addAndMakeVisible (door);
    }
    gateDoor.set ({}, "GATE");
    gateDoor.onClick = [this] { showGateMenu(); };
    gateDoor.setBounds (box (places.gateDoor));
    limiterDoor.set ({}, "LIMITER");
    limiterDoor.onClick = [this] { showOutputMenu(); };
    limiterDoor.setBounds (box (places.limiterDoor));

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
    as one whose INPUT was dialled against another zero of the lever has it settled on this one, and
    one kept under another TURBO or another way of DRIVE's is given the settings' own. */
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
    const auto dialled = amp.apvts.state.getProperty (channels.leverZeroId());
    const float push = getParam (params::blockIn (params::preampId).toRawUTF8());

    return dialled.isVoid() || (int) dialled != leverZero
        || (push > Channels::turboOnAbove && std::abs (push - channels.turboDb()) > 0.05f)
        || (getParam (params::blockSmooth (params::preampId).toRawUTF8()) > 0.5f) == driveSteps;
}

void Console::settleOnSettings()
{
    // No note: the trim was dialled on the engine's own scale, whose middle is 0 dB.
    const auto dialled = amp.apvts.state.getProperty (channels.leverZeroId());
    const int was = dialled.isVoid() ? 0 : (int) dialled;

    if (was != leverZero)
        setParam (params::inTrim, leverToDb (dbToLever (getParam (params::inTrim), was), leverZero));
    if (dialled.isVoid() || was != leverZero)
        amp.apvts.state.setProperty (channels.leverZeroId(), leverZero, nullptr);

    inFader.setValue (dbToLever (getParam (params::inTrim), leverZero), false);

    // TURBO the same way: that it is on is the state's, how hard is the settings'.
    const auto turboTrim = params::blockIn (params::preampId);
    if (const float push = getParam (turboTrim.toRawUTF8());
        push > Channels::turboOnAbove && std::abs (push - channels.turboDb()) > 0.05f)
        setParam (turboTrim.toRawUTF8(), channels.turboDb());

    // ...and so is how DRIVE goes between the captures: mixing the two it stands between, or in steps.
    const auto smooth = params::blockSmooth (params::preampId);
    if ((getParam (smooth.toRawUTF8()) > 0.5f) == driveSteps)
        setParam (smooth.toRawUTF8(), driveSteps ? 0.0f : 1.0f);
}

/** The machine's setting, not an edit of the sound: the lever stays, the trim follows, and there is
    no step to undo. */
void Console::chooseLeverZero (int db)
{
    leverZero = snapLeverZero (db);
    prefs::store().set (leverZeroKey, leverZero);

    const felitronics::appkit::CompareHistory::ScopedSuppress settling (amp.history);
    settleOnSettings();
}

void Console::sayLever (ColumnFader& fader, const juce::String& name, std::function<float()> reading)
{
    const auto say = [this, &fader, name, reading]
    {
        float db = reading();
        if (std::abs (db) < 0.05f)
            db = 0.0f;      // never "-0.0"

        const auto at = fader.getBounds().toFloat();
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
    prefs::store().set (turboKey, db);
    channels.setTurboDb ((float) db);

    const felitronics::appkit::CompareHistory::ScopedSuppress settling (amp.history);
    settleOnSettings();
}

void Console::chooseDriveSteps (bool steps)
{
    driveSteps = steps;
    prefs::store().set (driveStepsKey, steps);

    const felitronics::appkit::CompareHistory::ScopedSuppress settling (amp.history);
    settleOnSettings();
    drive.settle();
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

/** A RIGHT CLICK ON THE INPUT COLUMN — the lever, or the meter beside it: what the lever gives in the
    middle of its travel. */
void Console::showInputMenu()
{
    endTapping();

    juce::PopupMenu menu;
    menu.addSectionHeader ("LEVER'S ZERO");
    for (int db = leverZeroHighest; db >= leverZeroLowest; db -= leverZeroStep)
        addChoice (menu, decibels (db), db == leverZero, [db] (Console& e) { e.chooseLeverZero (db); });

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
    addChoice (menu, "On", on, [on] (Console& e) { e.setParam (params::gateOn, on ? 0.0f : 1.0f); });

    juce::PopupMenu threshold;
    const int now = juce::roundToInt (getParam (params::gateThreshold));
    for (int db = -80; db <= -10; db += 5)
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

/** A RIGHT CLICK ON TURBO: how hard it pushes into the capture. The machine's to say, as in the settings
    menu — every channel that has TURBO on comes back at the push in force. */
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
    const int a4 = prefs::tunerReference();
    for (const int hz : prefs::tunerReferences)
        addChoice (reference, juce::String (hz) + " Hz", hz == a4, [hz] (Console&) { prefs::setTunerReference (hz); });
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

    auto folder = juce::File (prefs::store().get (captureFolderKey, juce::String()).toString());
    if (! folder.isDirectory())
        folder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

    captureChooser = std::make_unique<juce::FileChooser> ("Save the capture", folder.getChildFile (captureNamePrefix + capture.name + hourTag (capture.hour) + ".nam"), "*.nam");
    captureChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                     | juce::FileBrowserComponent::warnAboutOverwriting,
                                 [nam = std::move (capture.nam)] (const juce::FileChooser& picked)
                                 {
                                     const auto file = picked.getResult();
                                     if (file == juce::File())
                                         return;

                                     prefs::store().set (captureFolderKey, file.getParentDirectory().getFullPathName());
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

    auto folder = juce::File (prefs::store().get (cabFolderKey, juce::String()).toString());
    if (! folder.isDirectory())
        folder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

    cabChooser = std::make_unique<juce::FileChooser> ("Save the cabinet",
                                                      folder.getChildFile (cabNamePrefix + Channels::cabinetStem (cabinet) + cabinet.getFileExtension()),
                                                      "*" + cabinet.getFileExtension());
    cabChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                 | juce::FileBrowserComponent::warnAboutOverwriting,
                             [cabinet] (const juce::FileChooser& picked)
                             {
                                 const auto file = picked.getResult();
                                 if (file == juce::File())
                                     return;

                                 prefs::store().set (cabFolderKey, file.getParentDirectory().getFullPathName());
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
    const auto warmth = percent > 80.0f ? orange
                      : percent > 50.0f ? juce::Colour (0xffece9f6)
                                        : labelLilac.withAlpha (0.85f);
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
    // each half of the controller's travel is spread over its own half of the knob's.
    for (int t = 0; t < Channels::numTones; ++t)
        if (const int v = toneTo[(size_t) t]; v >= 0)
            along (channels.toneId (t), v <= 64 ? 0.5f * (float) v / 64.0f : 0.5f + 0.5f * (float) (v - 64) / 63.0f);
}

/** THE LOAD, ITEMISED — OrbitAmp's window, in the face's words and colours. A product's packs are
    whole amps, with the boost and the power stage inside them, so the row OrbitAmp calls PREAMP is
    the AMP here, and its console the amp's. */
void Console::showLoad()
{
    auto panel = std::make_unique<LoadBreakdown> (amp);
    panel->nameOf = [] (int which, bool brief) -> juce::String
    {
        if (which == params::stPreamp)  return "AMP";
        if (which == params::stEq2)     return brief ? "A-EQ" : "AMP EQ";
        return {};
    };
    panel->dim   = labelLilac.withAlpha (0.85f);
    panel->faint = labelLilac.withAlpha (0.45f);
    showSheet (std::move (panel));
}

/** THE MIDI WINDOW — which inputs the product listens to, and the list of what it does with what it hears. */
void Console::showMidi()
{
    showSheet (std::make_unique<MidiPanel> (midiCommands));
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
    // any more, or whose INPUT was dialled against another zero. Settling any of them writes, and this
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
    bindTones();        // another channel's pack may keep its Bass, Middle and Treble in other places

    // A layer the packs do not carry yet never shows as thrown — whatever the state says.
    const auto playable = [] (Channels::Flag f) { return Channels::layerPlayable[(size_t) f]; };
    const auto thrown = [this, playable] (Channels::Flag f) { return playable (f) && channels.flag (f); };
    const bool hot = channels.lineup().channels[(size_t) channel].hot;     // FAT is a layer only the hottest packs carry
    buttons[btFat]->setEnabled (hot && playable (Channels::fat));
    buttons[btFat]->setOn      (thrown (Channels::fat),      false);
    buttons[btBright]->setOn   (thrown (Channels::bright),   false);
    buttons[btDepth]->setOn    (thrown (Channels::depth),    false);
    buttons[btPresence]->setOn (thrown (Channels::presence), false);

    std::array<bool, 6> live {};
    for (size_t i = 0; i < live.size(); ++i)
        live[i] = buttons[i]->isEnabled();
    picture->setKeysLive (live);

    // CAB's cap, on the cabinet that is playing. (This is first called while the face is still being
    // built, before the plates exist.)
    if (const int playing = channels.cabinet(); playing >= 0 && sliders[2] != nullptr)
        sliders[2]->setStep (playing, false);
    showCab();
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
            setParam (params::tunerOn, getParam (params::tunerOn) > 0.5f ? 0.0f : 1.0f);
        }
        else if (e.originalComponent == &stage)
        {
            pictureClicked (at);
        }
        return;
    }

    // CAB's name is a reading and takes no clicks of its own: a right click on it is one on CAB. So
    // with a column's meter: it is the same column as the lever beside it.
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

    // What the INPUT lever gives in the middle of its travel, how hard TURBO pushes where it pushes, and
    // how DRIVE goes between the pack's captures.
    menu.addSectionHeader ("INPUT");
    juce::PopupMenu zero;
    for (int db = leverZeroHighest; db >= leverZeroLowest; db -= leverZeroStep)
        addChoice (zero, decibels (db), db == leverZero, [db] (Console& e) { e.chooseLeverZero (db); });
    menu.addSubMenu ("Lever's zero  " + decibels (leverZero), zero);

    if (turboPushesCapture())
    {
        juce::PopupMenu push;
        const int turboNow = juce::roundToInt (channels.turboDb());
        for (const int db : turboPushes)
            addChoice (push, decibels (db), db == turboNow, [db] (Console& e) { e.chooseTurbo (db); });
        menu.addSubMenu ("TURBO adds  " + decibels (turboNow), push);
    }

    juce::PopupMenu way;
    for (int i = 0; i < driveWays.size(); ++i)
        addChoice (way, driveWays[i], (i == 1) == driveSteps, [steps = i == 1] (Console& e) { e.chooseDriveSteps (steps); });
    menu.addSubMenu ("DRIVE turns  " + driveWays[driveSteps ? 1 : 0], way);

    menu.addSectionHeader ("OUTPUT");
    for (int i = 0; i < params::stereoModes.size(); ++i)
        addChoice (menu, params::stereoModes[i], choice (params::stereoMode) == i,
                   [i] (Console& e) { e.setParam (params::stereoMode, (float) i); });

    // The gate: whether it stands in the chain, where it closes, how fast, and where it mutes.
    menu.addSectionHeader ("GATE");
    const bool gateIsOn = getParam (params::gateOn) > 0.5f;
    addChoice (menu, "On", gateIsOn, [gateIsOn] (Console& e) { e.setParam (params::gateOn, gateIsOn ? 0.0f : 1.0f); });

    juce::PopupMenu threshold;
    const int now = juce::roundToInt (getParam (params::gateThreshold));
    for (int db = -80; db <= -10; db += 5)
        addChoice (threshold, juce::String (db) + " dB", std::abs (now - db) <= 2,
                   [db] (Console& e) { e.setParam (params::gateThreshold, (float) db); });
    menu.addSubMenu ("Threshold  " + juce::String (now) + " dB", threshold, gateIsOn);

    juce::PopupMenu decay;
    for (int i = 0; i < params::gateDecayModes.size(); ++i)
        addChoice (decay, params::gateDecayModes[i], choice (params::gateDecay) == i,
                   [i] (Console& e) { e.setParam (params::gateDecay, (float) i); });
    menu.addSubMenu ("Closes  " + params::gateDecayModes[choice (params::gateDecay)], decay, gateIsOn);

    juce::PopupMenu position;
    for (int i = 0; i < params::gatePositions.size(); ++i)
        addChoice (position, params::gatePositions[i], choice (params::gatePos) == i,
                   [i] (Console& e) { e.setParam (params::gatePos, (float) i); });
    menu.addSubMenu ("Mutes at  " + params::gatePositions[choice (params::gatePos)], position, gateIsOn);

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
    auto folder = juce::File (prefs::store().get (cabFolderKey, juce::String()).toString());
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
    control.defaultValue   = p->getDefaultValue();

    a->sendInitialUpdate();
    return att;
}

/** The tone knobs on the playing pack's own Bass, Middle and Treble — wherever its list has them, so
    the knobs of a channel whose pack lists them otherwise turn other parameters (Channels::toneId). Only
    a knob whose parameter is another now is bound again. */
void Console::bindTones()
{
    for (size_t t = 0; t < tones.size(); ++t)
        if (const auto id = channels.toneId ((int) t); tones[t] != nullptr && id != toneBound[t])
        {
            toneBindings[t] = attach (*tones[t], id);
            toneBound[t] = id;
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

    const auto names   = Presets::names();
    const auto current = presets.current();
    const bool onDisk  = Presets::exists (current);

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
