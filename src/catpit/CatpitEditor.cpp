// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "CatpitEditor.h"
#include "CatpitLayout.h"

#include "../Parameters.h"
#include "../ui/Prefs.h"

#include <CatpitData.h>

#include <utility>

namespace orbitamp
{

juce::AudioProcessorEditor* makeEditor (AmpProcessor& p)
{
    return new catpit::CatpitEditor (p);
}

namespace catpit
{

namespace
{
    /** Where the keys stand along their row. ONE pitch for all six — the widest pair of neighbouring
        names plus a little air, so no name touches the next — the filters centred under BASS, the four
        layers set against the right end of the row, clear of the divider that parts the two groups. */
    std::array<float, 6> keyCentres()
    {
        const auto font = labelFont (layout::px11).withExtraKerningFactor (keyTracking);
        std::array<float, 6> w {}, x {};
        for (size_t i = 0; i < w.size(); ++i)
            w[i] = juce::GlyphArrangement::getStringWidth (font, switches[i].shortName);

        float pitch = 0.0f;
        for (size_t i = 0; i + 1 < w.size(); ++i)
            if (i != btLpf)     // the divider stands between LPF and FAT, not air
                pitch = juce::jmax (pitch, (w[i] + w[i + 1]) * 0.5f + 7.5f);

        x[btHpf] = layout::keyGroupX - pitch * 0.5f;
        x[btLpf] = layout::keyGroupX + pitch * 0.5f;
        for (size_t i = btFat; i <= btPresence; ++i)
            x[i] = layout::keyRowRight - w[btPresence] * 0.5f - pitch * (float) (btPresence - i);
        return x;
    }

    // Measured off the original renders (the ones that still had their pointers): where the lit slot
    // starts and ends along the radius, and how wide it is, as fractions of the knob's opaque radius.
    constexpr ArtKnob::Pointer drivePointer { 0.32f, 0.76f, 0.068f };
    constexpr ArtKnob::Pointer tonePointer  { 0.335f, 0.70f, 0.076f };

    /** How the stars fly, and what the windshield shows — settings of the machine's, in the cockpit's
        settings file. The stars are kept as a word, in the order of CatpitEditor::Stars. */
    const juce::Identifier starsKey  { "stars" };
    const juce::Identifier windowKey { "window" };
    const juce::StringArray starsWords { "opengl", "cpu", "off" };

    // How many pixels of images the GL context may hold as textures before it starts throwing the
    // oldest out — room for the layer at the largest window on a 3x screen, and the art beside it.
    constexpr size_t glTexturePixels = 32u * 1024u * 1024u;

    juce::Rectangle<float> around (juce::Point<float> c, float w, float h)
    {
        return juce::Rectangle<float> (w, h).withCentre (c);
    }

    /** Where a lower plate's name stands: over its inset. */
    juce::Rectangle<float> plateName (size_t plate)
    {
        const auto& inset = layout::sliderInsets[plate];
        return juce::Rectangle<float> (inset.getWidth(), 30.0f).withCentre ({ inset.getCentreX(), 640.0f });
    }

    /** WHERE THE CONSOLE'S CONTROLS STAND on the cockpit's render (CatpitLayout.h). Knobs fill their
        sockets: the drive's knurl sits inside the violet ring, a tone knob's own bezel lies over its
        ring. */
    face::Console::Places cockpitPlaces()
    {
        using namespace layout;

        face::Console::Places p;
        p.drive = around (drive.centre, drive.radius * 1.86f, drive.radius * 1.86f);
        for (size_t i = 0; i < tones.size(); ++i)
            p.tones[i] = around (tones[i].centre, tones[i].radius * 2.05f, tones[i].radius * 2.05f);

        const auto keyX = keyCentres();
        for (size_t i = 0; i < p.keys.size(); ++i)
            p.keys[i] = { keyX[i], keyRowY };
        p.keySize = keySize;
        p.holes = holes;
        p.holeKeySize = 40.0f;

        for (size_t i = 0; i < p.sliders.size(); ++i)
        {
            p.sliders[i] = sliderInsets[i];
            p.sliderNames[i] = plateName (i);
        }

        p.inMeter = inMeter;   p.inFader = inFader;
        p.outMeter = outMeter; p.outFader = outFader;
        p.tuner = tunerScreen;
        for (size_t i = 0; i < p.lamps.size(); ++i)
            p.lamps[i] = lamps[i];

        p.emblemFace = { emblemFace.begin(), emblemFace.end() };
        p.emblemMark = emblemMark;

        p.version     = around (versionPlate, 124.0f, 30.0f);   // the version, on the flat plate under CAB's right end
        p.load        = loadPlate;                              // CPU, on the plate under the grille
        p.memory      = memoryPlate;                            // MEM, on the mirror of CPU's plate
        p.preset      = around (presetPlate, 140.0f, 30.0f);    // PRESET, on the version's mirror
        p.gateDoor    = around (gatePlate, 100.0f, 28.0f);      // GATE and LIMITER, on the plates over their lamps,
        p.limiterDoor = around (limiterPlate, 100.0f, 28.0f);   // outside PRESET and the version
        p.readingPx   = px11;
        return p;
    }

    face::Console::Look cockpitLook()
    {
        const auto& art = Art::get();
        return { art.knobDrive, drivePointer, art.knobTone, tonePointer, art.sliderCap, art.sliderCap,
                 { &art.lampLit[0], &art.lampLit[1], &art.lampLit[2], &art.lampLit[3] },
                 juce::String::fromUTF8 (CatpitData::mark_svg, CatpitData::mark_svgSize),
                 juce::String::fromUTF8 (CatpitData::midi_txt, CatpitData::midi_txtSize) };
    }

    /** Whether a point on the stage is glass: where the render is cut through to the sky (Art), which
        the windshield's box also holds the overhead panel and the frame's corners round. */
    bool onGlass (juce::Point<int> at)
    {
        const auto& bg = Art::get().bg;
        if (! bg.isValid() || ! layout::glass.contains (at.toFloat()))
            return false;

        const float k = (float) bg.getWidth() / layout::width;
        const int x = (int) ((float) at.x * k), y = (int) ((float) at.y * k);
        return juce::isPositiveAndBelow (x, bg.getWidth()) && juce::isPositiveAndBelow (y, bg.getHeight())
            && bg.getPixelAt (x, y).getAlpha() < 16;
    }

    /** A CORNER'S SLOT, CUT INTO THE PICTURE (layout::CornerSlot): INPUT's well as painted — its rim, the slot
        down it, the T at its head — at a third of its size, its two ends whole and of its straight run only
        as much as the slot is long, turned to lie as the slot does; its cut-off corners are the cut's edge.
        Drawn into the picture at the screen's own pixel size, not into the render: the render is the
        stage's own size, and a rim one pixel wide, made a third of that, would be gone. */
    void cutSlot (juce::Graphics& g, const layout::CornerSlot& slot)
    {
        using namespace layout;

        const auto& bg = Art::get().bg;
        if (! bg.isValid())
            return;

        const float k = (float) bg.getWidth() / width;      // the render's pixels to the unit
        const float s = cornerSlotScale;
        const auto well = slot.upright (inWell.getWidth() * s, cornerSlotLength);
        const float c = inWellChamfer * s;

        const juce::Graphics::ScopedSaveState keep (g);
        g.addTransform (slot.turn());

        juce::Path cut;
        cut.startNewSubPath (well.getX() + c, well.getY());
        cut.lineTo (well.getRight() - c, well.getY());
        cut.lineTo (well.getRight(), well.getY() + c);
        cut.lineTo (well.getRight(), well.getBottom() - c);
        cut.lineTo (well.getRight() - c, well.getBottom());
        cut.lineTo (well.getX() + c, well.getBottom());
        cut.lineTo (well.getX(), well.getBottom() - c);
        cut.lineTo (well.getX(), well.getY() + c);
        cut.closeSubPath();
        g.reduceClipRegion (cut);

        // INPUT's well from `from` down, drawn at the slot's size from `to` down.
        const auto draw = [&] (float from, float to)
        {
            g.drawImageTransformed (bg, juce::AffineTransform::translation (-inWell.getX() * k, -from * k)
                                            .scaled (s / k)
                                            .translated (well.getX(), to));
        };

        // The straight run the whole length, then the two ends over it — so no seam shows where they meet.
        draw (inWell.getY() + inWellTop, well.getY());
        for (const bool head : { true, false })
        {
            const float length = (head ? inWellTop : inWellBottom) * s;
            const float to = head ? well.getY() : well.getBottom() - length;
            const juce::Graphics::ScopedSaveState end (g);
            juce::Path area;
            area.addRectangle (well.getX(), to, well.getWidth(), length);
            g.reduceClipRegion (area);
            draw (head ? inWell.getY() : inWell.getBottom() - inWellBottom, to);
        }
    }
}

//==============================================================================
juce::Rectangle<int> Cockpit::historyArea() const
{
    return juce::Rectangle<float> (layout::holes.front().x - 40.0f, 12.0f,
                                   layout::holes.back().x - layout::holes.front().x + 80.0f, 72.0f).toNearestInt();
}

void Cockpit::paintLive (juce::Graphics& g) const
{
    paintOverhead (g);
    paintKeyNames (g);
}

/** The keys' names, under them — a dimmed key's as dim as the key, and the playing pack's own where its
    switches are on them. They stand where the six names of their own set them (keyCentres). */
void Cockpit::paintKeyNames (juce::Graphics& g) const
{
    using namespace layout;

    const auto keyX = keyCentres();
    for (size_t i = 0; i < keyNames.size(); ++i)
        drawLabel (g, keyNames[i], juce::Rectangle<float> (120.0f, 20.0f).withCentre ({ keyX[i], keyNameY }),
                   px11, labelLilac.withAlpha (keysLive[i] ? 1.0f : PushButton::dimmed), keyTracking);
}

void Cockpit::paintOverhead (juce::Graphics& g) const
{
    using namespace layout;

    const auto signAt = [] (juce::Point<float> hole) { return juce::Point<float> (hole.x - 29.0f, hole.y); };
    const auto lamp = [&g] (juce::Point<float> hole, bool lit)
    {
        if (! lit)
            return;
        juce::Path glass;
        glass.addRoundedRectangle (juce::Rectangle<float> (19.0f, 4.0f).withCentre ({ hole.x, 29.2f }), 2.0f);
        juce::DropShadow (orange, 6, {}).drawForPath (g, glass);
        g.setColour (orange);
        g.fillPath (glass);
    };
    const auto socket = [&g] (juce::Point<float> hole)
    {
        const auto box = juce::Rectangle<float> (23.0f, 7.0f).withCentre ({ hole.x, 29.2f });
        g.setColour (juce::Colour (0xff3a3843));
        g.fillRoundedRectangle (box, 2.5f);
        g.setColour (juce::Colour (0xff6b6878));
        g.drawRoundedRectangle (box, 2.5f, 0.9f);
    };

    // The history: a turning arrow each, its lamp lit while there is a step to take.
    const bool can[] { canUndo, canRedo };
    for (int i = 0; i < 2; ++i)
    {
        const auto hole = holes[(size_t) (ovUndo + i)];
        drawTurnArrow (g, signAt (hole), 8.0f, i == 0, labelLilac.withAlpha (can[i] ? 1.0f : 0.35f));
        socket (hole);
        lamp (hole, can[i]);
    }

    // The registers: a letter each, the lamp lit over the one that is playing.
    for (int i = 0; i < numRegisterKeys; ++i)
    {
        const auto hole = holes[(size_t) (ovFirstRegister + i)];
        const bool here = i == activeRegister;
        drawLabel (g, juce::String::charToString ((juce::juce_wchar) ('A' + i)),
                   juce::Rectangle<float> (24.0f, 26.0f).withCentre (signAt (hole)),
                   px13, here ? orange : labelLilac.withAlpha (0.8f), 0.0f);
        lamp (hole, here);
    }
}

void Cockpit::paintEmblem (juce::Graphics& g, float scale) const
{
    using namespace layout;

    // A fresh face over the render's: dark metal, lit from above, with a fine bevel — so the render's
    // own cat is gone and the plate still reads as the same piece of hull.
    juce::Path face;
    face.startNewSubPath (emblemFace[0]);
    for (size_t i = 1; i < emblemFace.size(); ++i)
        face.lineTo (emblemFace[i]);
    face.closeSubPath();

    const auto box = face.getBounds();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff24222b), box.getCentreX(), box.getY(),
                                             juce::Colour (0xff111016), box.getCentreX(), box.getBottom(), false));
    g.fillPath (face);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.05f), box.getX(), box.getY(),
                                             juce::Colours::transparentWhite, box.getCentreX(), box.getCentreY(), false));
    g.fillPath (face);
    g.setColour (juce::Colour (0xff4a4757).withAlpha (0.8f));
    g.strokePath (face, juce::PathStrokeType (1.2f));

    for (const auto& c : emblemScrews)
    {
        const auto head = juce::Rectangle<float> (8.0f, 8.0f).withCentre (c);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6d6a78), c.translated (-1.5f, -2.0f),
                                                 juce::Colour (0xff141318), c.translated (2.5f, 3.0f), true));
        g.fillEllipse (head);
        g.setColour (juce::Colour (0xff09080c));
        g.drawLine (c.x - 2.2f, c.y - 2.2f, c.x + 2.2f, c.y + 2.2f, 1.0f);
    }

    engraveMark (g, emblemMark, scale);
}

void Cockpit::paintStatic (juce::Graphics& g, float scale) const
{
    using namespace layout;

    const auto& bg = Art::get().bg;
    if (bg.isValid())
        g.drawImage (bg, juce::Rectangle<float> (0.0f, 0.0f, width, height));

    cutSlot (g, gateSlot);
    cutSlot (g, limiterSlot);

    paintEmblem (g, scale);

    drawLabel (g, "DRIVE", juce::Rectangle<float> (220.0f, 30.0f).withCentre ({ drive.centre.x, 394.0f }), px15, labelLilac);
    const char* toneNames[] { "BASS", "MIDDLE", "TREBLE" };
    for (size_t i = 0; i < tones.size(); ++i)
        drawLabel (g, toneNames[i], juce::Rectangle<float> (150.0f, 28.0f).withCentre ({ tones[i].centre.x, 394.0f }), px13, labelLilac);

    // TURBO stands IN its frame's top line, in the gap the two brackets leave (96 wide; the word is 80).
    // The capitals sit a hair under the middle of the row they are drawn in — hence the 1.
    drawLabel (g, "TURBO", juce::Rectangle<float> (160.0f, 26.0f).withCentre ({ turboFrame.getCentreX(), turboFrame.getY() - 1.0f }), px13, orange);
    // ...and the throttle's frame is named the same way (its gap is 148 wide).
    drawLabel (g, "CHANNEL", juce::Rectangle<float> (160.0f, 26.0f).withCentre ({ leverFrame.getCentreX(), leverFrame.getY() - 1.0f }), px13, orange);

    drawLabel (g, "INPUT",  juce::Rectangle<float> (170.0f, 26.0f).withCentre ({ 117.0f, 314.0f }),  px13, labelLilac);
    drawLabel (g, "OUTPUT", juce::Rectangle<float> (170.0f, 26.0f).withCentre ({ 1557.0f, 314.0f }), px13, labelLilac);

    // The tuner's name; its screen is the live TunerFace.
    drawLabel (g, "TUNER", juce::Rectangle<float> (200.0f, 26.0f).withCentre ({ tunerScreen.getCentreX(), 781.0f }), px13, labelLilac);
}

//==============================================================================
CatpitEditor::CatpitEditor (AmpProcessor& p)
    : face::Console (p, std::make_unique<Cockpit>(), cockpitPlaces(), cockpitLook()),
      monitor (p)
{
    using namespace layout;

    // Everything this constructor sets is the face settling onto the state, not a player's edit.
    const felitronics::appkit::CompareHistory::ScopedSuppress settling (amp.history);

    // The sky, under the cockpit's picture: what the windshield is cut through to.
    stage.addAndMakeVisible (space, 0);
    space.setBounds (glass.toNearestInt());
    space.setWindow (windshield - glass.getPosition());
    stage.addChildComponent (still, 1);
    still.setBounds (glass.toNearestInt());
    stage.addChildComponent (monitor, 2);
    monitor.setBounds (windshield.toNearestInt());
    // In the render's space the glass is clear of the overhead panel above and the emblem below between
    // these two heights; across, the picture takes the windshield's whole width and runs out under the
    // frame on both sides rather than stopping short of it.
    monitor.setBand ({ 0.0f, 100.0f - windshield.getY(), windshield.getWidth(), 134.0f });
    // Over the stars a flat line in a pause is a scratch on the glass: SHAPE goes out with the sound.
    monitor.shapeFadesInSilence = true;

    // The throttle picks the channel — its pack, and everything that channel was left with.
    addToStage (lever);
    lever.setBounds (leverFrame.toNearestInt().withLeft (juce::roundToInt (leverFrame.getX()) - Lever::overhang));
    lever.onTravel = [this] (float pos)
    {
        const float t = pos / (float) (Lever::numDetents - 1);
        space.setThrottle (t);
        if (glSky != nullptr)
            glSky->setThrottle (t);
    };
    lever.onDetent  = [this] (int d) { go (d); };
    lever.onRelease = [this] { showChannel(); };   // held, it follows the hand; let go, it follows the state
    // A prototype's hook for looking at the window at speed without a hand on the lever.
    if (const auto d = juce::SystemStats::getEnvironmentVariable ("CATPIT_DETENT", {}); d.isNotEmpty())
        engine.go (d.getIntValue());
    lever.setDetent (channels.active(), false, false);

    // The helm turns whichever sky is flying.
    steering.onChange = [this] (float s)
    {
        space.setSteer (s);
        if (glSky != nullptr)
            glSky->setSteer (s);
    };

    // TURBO pushes the capture: the preamp's input trim, at the push the settings give or at nothing.
    // The trim is a parameter and a host may leave it anywhere, so anything well above nothing reads as on.
    addToStage (turbo);
    turbo.setBounds (turboFrame.toNearestInt());
    turbo.onMenu = [this] { showTurboMenu(); };
    {
        auto* push = follow (params::blockIn (params::preampId).toRawUTF8(),
                             [this] (float db) { turbo.setOn (db > Channels::turboOnAbove, false); });
        turbo.onChange = [this, push] (bool on) { push->setValueAsCompleteGesture (on ? channels.turboDb() : 0.0f); };
        push->sendInitialUpdate();
    }

    // GATE'S THRESHOLD AND LIMITER'S CEILING, each on a short fader in the slot cut into its corner, the
    // fader turned with its slot (layout::CornerSlot) — INPUT's fader drawn at the slot's size, its cap the
    // columns'. A double click puts it back where it opens, a right click is its block's menu, and while it
    // is held the tag over it says where it stands. While its block is off it stands dim, and still moves.
    const auto cornerFader = [this] (ColumnFader& fader, const CornerSlot& slot, const char* paramId, const char* onId,
                                     float low, float high, std::function<void()> menu, const juce::String& name)
    {
        const auto travelOf = [low, high] (float v) { return (v - low) / (high - low); };
        const auto* param = amp.apvts.getParameter (paramId);
        fader.scale = cornerSlotScale;
        fader.capLength = (layout::inFader.getWidth() - 8.0f) * cornerSlotScale;    // as long, to the slot, as INPUT's
        fader.defaultValue = travelOf (param->convertFrom0to1 (param->getDefaultValue()));
        auto* attachment = follow (paramId, [&fader, travelOf] (float v) { fader.setValue (travelOf (v), false); });
        fader.onGestureStart = [attachment] { attachment->beginGesture(); };
        fader.onChange       = [attachment, low, high] (float travel) { attachment->setValueAsPartOfGesture (juce::jmap (travel, low, high)); };
        fader.onGestureEnd   = [attachment] { attachment->endGesture(); };
        fader.onMenu         = std::move (menu);
        attachment->sendInitialUpdate();
        sayLever (fader, name, [this, paramId] { return getParam (paramId); });
        follow (onId, [&fader] (float on) { fader.setAlpha (on > 0.5f ? 1.0f : 0.6f); })->sendInitialUpdate();
        if (std::abs (slot.angle) > juce::MathConstants<float>::pi * 0.25f)
            fader.setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);     // it lies more across than up

        // INPUT's fader stands in its well as its box does in INPUT's; the box is the stage's whole units,
        // so the turn starts by putting its middle back on the slot's.
        addToStage (fader);
        const auto box = slot.upright (layout::inFader.getWidth() * cornerSlotScale,
                                       cornerSlotLength - (inWell.getHeight() - layout::inFader.getHeight()) * cornerSlotScale).toNearestInt();
        fader.setBounds (box);
        fader.setTransform (juce::AffineTransform::translation (slot.centre - box.toFloat().getCentre()).followedBy (slot.turn()));
    };
    threshold.marks = 6;            // every 5 dB, the longer at -50
    threshold.longMark = 4;
    cornerFader (threshold, gateSlot, params::gateThreshold, params::gateOn, thresholdLowDb, thresholdHighDb,
                 [this] { showGateMenu(); }, "GATE");
    ceiling.marks = 6;              // every half a decibel or so, none longer
    ceiling.longMark = -1;
    cornerFader (ceiling, limiterSlot, params::limiterCeiling, params::limiterOn, params::limiterCeilingMin, params::limiterCeilingMax,
                 [this] { showOutputMenu(); }, "LIMITER");

    showChannel();

    // The stars fly on the GPU unless the player has said otherwise (or CATPIT_SKY=cpu or =off says so
    // for this run): the choice is the machine's, not the sound's, so it lives with the settings, not
    // the state.
    {
        auto word = juce::SystemStats::getEnvironmentVariable ("CATPIT_SKY", {}).toLowerCase();
        if (! starsWords.contains (word))
            word = prefs::store().get (starsKey, starsWords[0]).toString();
        stars = (Stars) juce::jmax (0, starsWords.indexOf (word));
    }

    // ...and so does what the windshield shows over it: nothing, or a picture of the amp at work.
    monitor.show ((Monitor::View) juce::jlimit (0, (int) Monitor::View::numViews - 1,
                                                (int) prefs::store().get (windowKey, 0)));
    applySky();

    // LEARN from GATE's menu takes the tuner's glass while it runs.
    addToStage (learnGlass);
    learnGlass.setBounds (tunerScreen.toNearestInt());
    learnGlass.inset = { 18.0f, 10.0f };    // where the tuner's own note and ruler start
    learnGlass.onShow = [this] (bool up) { tuner.setAlpha (up ? 0.0f : 1.0f); };
}

CatpitEditor::~CatpitEditor()
{
    gl.detach();
}

void CatpitEditor::showSelector (int channel)
{
    if (! lever.isHeld() && lever.getDetent() != channel)
        lever.setDetent (channel, true, false);
    still.setChannel (channel);
}

/** 1 2 3 4: the throttle goes to that channel, as if a hand had put it there. */
bool CatpitEditor::channelKey (const juce::KeyPress& key)
{
    const int channel = key.getKeyCode() - '1';
    if (channel < 0 || channel >= Lever::numDetents)
        return false;

    if (! lever.isHeld())
        lever.setDetent (channel, true);
    return true;
}

/** A Program Change: the throttle goes to that channel, as it does for 1 2 3 4 on the keyboard. Programs
    past the fourth are not the cockpit's. */
void CatpitEditor::programChange (int program)
{
    if (program >= 0 && program < Lever::numDetents && ! lever.isHeld())
        lever.setDetent (program, true);
}

/** TURBO's controller: the push, or none. */
void CatpitEditor::turboChange (bool thrown)
{
    setParam (params::blockIn (params::preampId).toRawUTF8(), thrown ? channels.turboDb() : 0.0f);
}

/** What the windshield shows over the sky — nothing, or one of the pictures of the amp at work — and
    full screen; then the stars outside it: flying on the GPU, flying on the CPU, or a still picture and
    nothing running. Where the GPU's sky cannot be had the CPU's flies instead, and the tick says so. */
void CatpitEditor::addWindowSections (juce::PopupMenu& menu)
{
    const auto self = [] (face::Console& e) -> CatpitEditor& { return static_cast<CatpitEditor&> (e); };

    menu.addSectionHeader ("WINDOW");
    for (int i = 0; i < Monitor::names().size(); ++i)
        addChoice (menu, i == 0 ? juce::String ("Stars only") : Monitor::names()[i], (int) monitor.shown() == i,
                   [i, self] (face::Console& e) { self (e).showInWindow ((Monitor::View) i); });
    if (! amp.runsInHost())
        addChoice (menu, "Full screen", fullScreen, [self] (face::Console& e) { self (e).setFullScreen (! self (e).fullScreen); });

    menu.addSectionHeader ("STARS");
    const auto flying = stars == Stars::gpu && gpuBroken ? Stars::cpu : stars;
    addChoice (menu, gpuBroken ? juce::String::fromUTF8 ("OpenGL \xe2\x80\x94 not available here") : juce::String ("OpenGL"),
               flying == Stars::gpu, [self] (face::Console& e) { self (e).chooseStars (Stars::gpu); }, ! gpuBroken);
    addChoice (menu, "CPU", flying == Stars::cpu, [self] (face::Console& e) { self (e).chooseStars (Stars::cpu); });
    addChoice (menu, "Off", flying == Stars::off, [self] (face::Console& e) { self (e).chooseStars (Stars::off); });
}

//==============================================================================
/** THE HAND ON THE GLASS — where the picture is cut through to the sky, so not the overhead panel that
    hangs into it. Dragged left or right it steers, a quarter of the windshield's width either way full
    lock, and the ship straightens when the button comes up. Pressed and let go without moving, it was a
    click, and asks what the windshield shows: the sections the settings menu opens with. Which of the
    two it was is known only when the button comes up — the platform's few pixels of travel tell them
    apart — so the menu waits for that. Anywhere else, the hand is the console's. */
void CatpitEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto onStage = e.getEventRelativeTo (&stage);
    if (e.originalComponent == &stage && ! e.mods.isPopupMenu() && onGlass (onStage.getPosition()))
    {
        handOnGlass = true;
        grabKeyboardFocus();        // ...and from here on the arrows steer as well
        steering.press (onStage.position.x);
        return;
    }
    Console::mouseDown (e);
}

void CatpitEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (handOnGlass)
        steering.drag (e.getEventRelativeTo (&stage).position.x, layout::windshield.getWidth());
}

void CatpitEditor::mouseUp (const juce::MouseEvent& e)
{
    if (! std::exchange (handOnGlass, false))
        return;
    steering.release();
    if (! e.mouseWasDraggedSinceMouseDown())
        showWindowMenu();
}

/** ← AND → steer while they are held, as the drag does — while the face itself has the keyboard, which a
    click anywhere on it gives it back. A window open over the face (Sheet) keeps its arrows. */
bool CatpitEditor::keyPressed (const juce::KeyPress& key)
{
    return (hasKeyboardFocus (false) && face::Steering::steersBy (key)) || Console::keyPressed (key);
}

bool CatpitEditor::keyStateChanged (bool)
{
    return hasKeyboardFocus (false) && steering.keysChanged();
}

void CatpitEditor::focusLost (FocusChangeType)
{
    steering.keysLost();
}

void CatpitEditor::visibilityChanged()
{
    if (! isVisible())
    {
        handOnGlass = false;
        steering.clear();
    }
}

void CatpitEditor::showWindowMenu()
{
    endTapping();

    juce::PopupMenu menu;
    addWindowSections (menu);
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

void CatpitEditor::placeSky()
{
    if (glSky != nullptr)
    {
        const float s = stageScale();
        const auto at = stageOrigin();
        glSky->setArea (layout::glass * s + at, layout::windshield * s + at, { (float) getWidth(), (float) getHeight() });
    }
}

void CatpitEditor::chooseStars (Stars chosen)
{
    stars = chosen;
    prefs::store().set (starsKey, starsWords[(int) chosen]);
    applySky();
}

/** Puts up the sky the choice and the machine allow: the shader on the GPU, the CPU's stars, or the
    still picture. */
void CatpitEditor::applySky()
{
    const bool wantGpu = stars == Stars::gpu && ! gpuBroken;

    if (wantGpu && glSky == nullptr)
    {
        glSky = std::make_unique<GlSky> (gl);
        glSky->setThrottle ((float) lever.getDetent() / (float) (Lever::numDetents - 1));
        glSky->setSteer (steering.value());
        gl.setRenderer (glSky.get());
        gl.setComponentPaintingEnabled (true);
       #if JUCE_LINUX || JUCE_BSD
        gl.setContinuousRepainting (false);     // glPulse asks for each frame
       #else
        gl.setContinuousRepainting (true);
       #endif

        // JUCE keeps the textures it makes of images in a cache whose limit — 8 * 1024 * 1024, and
        // called bytes — it compares with PIXEL counts. The cockpit's own layer is the whole window at
        // the screen's pixel size: 2.4 million pixels at the smallest window and 9.5 million at the
        // largest on a 2x screen, so from about 1740 wide the layer and the art no longer fitted, and
        // every paint threw textures out and loaded them again. The limit is a ceiling, not an
        // allocation: only what is drawn is held, and a replaced layer leaves the cache with its image.
        gl.setImageCacheSize (glTexturePixels);
        gl.attachTo (*this);

        // A shader that will not build on this machine leaves the window dark; look once, and fall back.
        ::juce::Timer::callAfterDelay (800, [safe = juce::Component::SafePointer<CatpitEditor> (this)]   // from the top: Console inherits Timer privately, and MSVC refuses that base's name here
        {
            if (safe != nullptr && safe->glSky != nullptr && safe->glSky->failed())
            {
                safe->gpuBroken = true;
                safe->applySky();
            }
        });
    }
    else if (! wantGpu && glSky != nullptr)
    {
        gl.detach();
        gl.setRenderer (nullptr);
        glSky.reset();
    }

    space.setVisible (glSky == nullptr && stars != Stars::off);
    still.setVisible (glSky == nullptr && stars == Stars::off);
    placeSky();
}

void CatpitEditor::showInWindow (Monitor::View view)
{
    prefs::store().set (windowKey, (int) view);
    monitor.show (view);
}

} // namespace catpit
} // namespace orbitamp
