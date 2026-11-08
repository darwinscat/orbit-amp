// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "RedGrooveEditor.h"

#include "RedGrooveArt.h"
#include "RedGrooveLayout.h"
#include "RedGrooveLineup.h"

#include "view/WalkerDrive.h"

#include "../ui/BassControls.h"
#include "../ui/CabWindow.h"
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
    juce::String hertz (float hz) { return hz >= 1000.0f ? juce::String (hz / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz"; }

    /** MIDDLE's frequency on its fader: the span params::bassMidMinHz .. bassMidMaxHz on a logarithmic travel. */
    float midTravel (float hz)
    {
        const float lo = params::bassMidMinHz, hi = params::bassMidMaxHz;
        return juce::jlimit (0.0f, 1.0f, std::log (juce::jlimit (lo, hi, hz) / lo) / std::log (hi / lo));
    }
    float midHz (float travel) { return params::bassMidMinHz * std::pow (params::bassMidMaxHz / params::bassMidMinHz, travel); }

    /** A PACK'S DIAL in decibels where its tone slot stands (0..1): its first band's gain at that travel, as the rig
        player plays it (felitronics::rigplayer::sectionAt). */
    float dialDb (const namz::rig::Tone& tone, float v)
    {
        if (tone.sections.empty())
            return 0.0f;
        const double t = felitronics::rigplayer::travelOf (tone, (double) v);
        const auto& band = tone.sections.front();
        return (float) (t < 0.0 ? -t * band.dbAtMin : t * band.dbAtMax);
    }

    /** ...and a pack's FREQUENCY knob where its slot stands: where it puts the band it moves (frequencyAt). */
    float knobHz (const namz::rig::Tone& tone, float v)
    {
        double hz = 0.0, q = 0.0;
        const auto at = std::to_string (juce::roundToInt (juce::jlimit (0.0f, 1.0f, v) * (float) tone.sweep));
        return felitronics::rigplayer::frequencyAt (tone, at, hz, q) ? (float) hz : 0.0f;
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

        // No REVERB, DELAY or CAB plates. CAB's box is the cabinet's screen: a click on it opens the cabinets' window.
        p.plates = false;
        p.sliderNames[2] = cabinetScreen;

        p.faderCapLength = faderCapLength;
        p.faderCapSquash = faderCapSquash;
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
    {
        g.drawImage (bg, juce::Rectangle<float> (0.0f, 0.0f, width, height));

        // KEYS and TUNER changed places: each top panel's picture drawn where its twin's is.
        const float k = (float) bg.getWidth() / width;
        const auto left = leftPanelArt, right = leftPanelArt.translated (panelShift, 0.0f);
        const auto keysPicture  = bg.getClippedImage ((left * k).toNearestInt());
        const auto tunerPicture = bg.getClippedImage ((right * k).toNearestInt());
        g.drawImage (keysPicture, right);
        g.drawImage (tunerPicture, left);

        // BLEND's socket under plain panel — its top half the panel over it, its bottom half the panel under it — and
        // DRIVE's socket drawn a size up where DRIVE stands, round, over its old one.
        const auto plainOf = [&bg, k] (juce::Rectangle<float> r) { return bg.getClippedImage ((r * k).toNearestInt()); };
        const auto socket = blendSocketArt;
        const float half = 0.5f * socket.getHeight();
        g.drawImage (plainOf (socket.withY (socket.getY() - half - 4.0f).withHeight (half)), socket.withHeight (half));
        g.drawImage (plainOf (socket.withY (socket.getBottom() + 1.0f).withHeight (half)), socket.withTrimmedTop (half));

        // ...and the squeeze's old slot between COMP and DRIVE, the squeeze lying over COMP now: the panel at the same
        // height between the lever and COMP — the panel brightens toward its foot, and a patch from over or under showed.
        const auto gr = grSlot.expanded (4.0f);
        g.drawImage (plainOf (gr.withX (210.0f)), gr);

        const float grow = driveSocket / driveArtSocket, span = 1.1f * driveArtSocket;
        const auto from = juce::Rectangle<float> (span, span).withCentre (driveArt);
        const auto to   = juce::Rectangle<float> (span * grow, span * grow).withCentre (drive);
        const juce::Graphics::ScopedSaveState keep (g);
        juce::Path round;
        round.addEllipse (to);
        g.reduceClipRegion (round);
        g.drawImage (plainOf (from), to);
    }

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
    print (g, "BLEND", { blendSlot.getCentreX(), knobNameY }, px13, words);
    const char* effects[] { "OCTAVE", "ENVELOPE", "CHORUS" };
    for (size_t i = 0; i < effectX.size(); ++i)
        printInto (g, effects[i], { effectX[i], knobNameY }, effectNameRoom, px13, words, effectNameTracking);
    print (g, "CAB", { diCab.x, cabWordY }, px13, words);
    print (g, "DI", { diCab.x, diWordY }, px13, words);
    const auto ends = cabinInk().at (words, 0.8f);
    writeAsIs (g, cabinInk(), "200 Hz", around ({ midFreqSlot.getX() - 48.0f, midFreqSlot.getCentreY() }, 90.0f, 28.0f), px13, ends);
    writeAsIs (g, cabinInk(), "3 kHz", around ({ midFreqSlot.getRight() + 40.0f, midFreqSlot.getCentreY() }, 80.0f, 28.0f), px13, ends);

    // The picture's own two slots for the cuts' switches go under plain panel — the switches stand one a line now
    // (switchLineY), each its slot cut out of the picture where it stands. The plain is the panel between the two, clear
    // of both their rims.
    if (const auto& bg = Art::of().bg; bg.isValid())
    {
        const float k = (float) bg.getWidth() / width;
        const auto plainOf = [&bg, k] (juce::Rectangle<float> r) { return bg.getClippedImage ((r * k).toNearestInt()); };
        g.setOpacity (1.0f);        // an image is drawn at the last colour's alpha — the words' above were translucent
        for (const auto slot : { lowCutSlot, highCutSlot })
        {
            const auto over = slot.expanded (6.0f, 5.0f);
            g.drawImage (plainOf (over.withY (lowCutSlot.getBottom() + 3.0f)), over);
        }

        // IN's slot where BLEND's knob was.
        g.drawImage (plainOf (inFaderSlot), blendSlot);

        // The picture's screen under plain panel — the panel just under it, in three strips, clear of DI/CAB's socket, the
        // middle one turned over so that each meets the next on the same row and no seam shows — and in its place the
        // cabinet's slot: OCTAVE's socket turned a quarter, its ends as they are, its middle longer.
        const auto screen = cabinetScreen.expanded (3.0f);
        const float strip = screen.getHeight() / 3.0f;
        const auto plain = plainOf (screen.withY (screen.getBottom() + 3.0f).withHeight (strip));
        for (int i = 0; i < 3; ++i)
        {
            // ...each a little over the next, so that no edge falls between two pixels with the screen showing through.
            const auto to = screen.withY (screen.getY() + strip * (float) i).withHeight (strip).expanded (0.0f, i == 1 ? 1.0f : 0.0f);
            const float sx = to.getWidth() / (float) plain.getWidth(), sy = to.getHeight() / (float) plain.getHeight();
            g.drawImageTransformed (plain, i == 1 ? juce::AffineTransform::scale (sx, -sy).translated (to.getX(), to.getBottom())
                                                  : juce::AffineTransform::scale (sx, sy).translated (to.getX(), to.getY()));
        }
        const auto across = [&g, &plainOf] (juce::Rectangle<float> from, juce::Rectangle<float> to)
        {
            // A piece of the upright socket, turned a quarter clockwise into `to`: its top at the right.
            const auto piece = plainOf (from);
            const float w = (float) piece.getWidth(), h = (float) piece.getHeight();
            g.drawImageTransformed (piece, juce::AffineTransform::rotation (juce::MathConstants<float>::halfPi)
                                               .translated (h, 0.0f)
                                               .scaled (to.getWidth() / h, to.getHeight() / w)
                                               .translated (to.getX(), to.getY()));
        };
        const auto socket = octaveSocketArt;
        const float end = 24.0f;        // a rounded end, with the panel round it
        across (socket.withTrimmedTop (socket.getHeight() - end), cabinetSlot.withWidth (end));                  // its foot at the left
        across (socket.withHeight (end), cabinetSlot.withTrimmedLeft (cabinetSlot.getWidth() - end));             // its head at the right
        across (socket.withTrimmedTop (0.5f * socket.getHeight() - 5.0f).withHeight (10.0f), cabinetSlot.reduced (end, 0.0f));

        // DI/CAB's socket under the plain panel beside it; IN's slot where the knob was, its middle left out.
        g.drawImage (plainOf (diCabPlainArt), diCabSocketArt);
        const float half = 0.5f * diCabSlot.getHeight();
        const auto inTop = inFaderSlot.withHeight (half), inBottom = inFaderSlot.withTrimmedTop (inFaderSlot.getHeight() - half);
        g.drawImage (plainOf (inTop), diCabSlot.withHeight (half));
        g.drawImage (plainOf (inBottom), diCabSlot.withTrimmedTop (half));
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
      octave (Art::of().faderCap), envelope (Art::of().faderCap), chorus (Art::of().faderCap), midFreq (Art::of().faderCap),
      diCab (Art::of().faderCap), blend (Art::of().faderCap),
      squeeze ([&p] { return p.compGrDb.load (std::memory_order_relaxed); }),
      lever (cabinInk(), { "B15", "SVT" }, layout::leverPlaces, layout::leverX, layout::leverNames, layout::leverBox.getPosition()),
      lowCut (cabinInk(), "STR", "4", "5", 2, true),
      highCut (cabinInk(), "STYL", "F", "S"),
      ultraLo (cabinInk(), "U-LO", "-", "+", 3),
      ultraHi (cabinInk(), "U-HI", "0", "+", 2),
      cabinet (cabinInk(), Art::of().faderCap, layout::faderCapLength, layout::faderCapSquash),
      cabinetOn (cabinInk(), {}, {}, {})
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

    // THE RINGS, one round every knob in its socket's groove, the tones' where the rows put them (stripsLaid).
    const auto ringFor = [this] (const face::ArtKnob& knob, juce::Point<float> centre, float socket, float width)
    {
        rings.push_back (std::make_unique<face::KnobRing> (knob, ringLight, centre, socket * 0.5f - layout::ringInset, width));
        addToStage (*rings.back());
    };
    ringFor (driveKnob(), layout::drive, layout::driveSocket, layout::driveRingWidth);
    ringFor (comp, layout::comp, layout::smallSocket, layout::ringWidth);
    for (int t = 0; t < Channels::maxTones; ++t)
        ringFor (toneKnob (t), { layout::toneX[0], layout::toneY }, layout::toneSocket, layout::ringWidth);
    stripsLaid (strips);        // the console laid the rows before there were rings to put round them

    // THE FADERS: the three effects upright in their slots, MIDDLE's frequency across under the tones. Each says
    // where it stands in the tag over it while it is held.
    const auto faderIn = [this] (face::ColumnFader& fader, juce::Rectangle<float> box, float cap, bool across)
    {
        fader.capLength = cap;
        fader.capSquash = layout::faderCapSquash;
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
    faderIn (diCab, layout::diCabSlot.withSizeKeepingCentre (layout::faderWidth, layout::diCabSlot.getHeight()), layout::faderCapLength, false);
    diCab.marks = 4;
    faderIn (blend, layout::blendSlot.withSizeKeepingCentre (layout::faderWidth, layout::blendSlot.getHeight()), layout::faderCapLength, false);
    blend.marks = 4;

    // Each known by a name of its own, as a gate finds it (tests/RedGrooveFaceTests.cpp).
    comp.setComponentID ("comp");
    blend.setComponentID ("blend");
    diCab.setComponentID ("diCab");
    octave.setComponentID ("octave");
    envelope.setComponentID ("envelope");
    chorus.setComponentID ("chorus");
    midFreq.setComponentID ("midFreq");

    // COMP's squeeze, across over its knob.
    addToStage (squeeze);
    squeeze.across = true;
    squeeze.setBounds (layout::grBar.toNearestInt());

    // THE READINGS, one under every control.
    const auto under = [] (float x, float y, float w = 120.0f) { return around ({ x, y }, w, 26.0f); };
    auto& driveValue  = readout (under (layout::drive.x, layout::valueY));
    auto& compValue   = readout (under (layout::comp.x, layout::valueY, 84.0f));
    auto& blendValue  = readout (under (layout::blendSlot.getCentreX(), layout::valueY, 84.0f));
    auto& midValue    = readout (under (layout::toneX[1], layout::midFreqValueY));
    auto& inValue     = readout (under (layout::inTitle.x, layout::levelValueY, 104.0f));
    auto& outValue    = readout (under (layout::outTitle.x, layout::levelValueY, 104.0f));
    Readout* effectValues[] { &readout (under (layout::effectX[0], layout::valueY, 90.0f)),
                              &readout (under (layout::effectX[1], layout::valueY, 90.0f)),
                              &readout (under (layout::effectX[2], layout::valueY, 90.0f)) };

    // ...DRIVE's as its dial reads, 0 to 10; the tones' in decibels — the pack's dials, its BASS, MID and TREBLE, each
    // its band's gain where it stands, or on a pack with no tone the bass's standard's three (Channels::toneOf); the
    // levers' in decibels.
    auto* gain = amp.apvts.getParameter (params::preampGain);
    follow (params::preampGain, [gain, &driveValue] (float v) { driveValue.set (juce::String (gain->convertTo0to1 (v) * 10.0f, 1)); })->sendInitialUpdate();
    const auto dials = channels.tonesOf (channels.active());
    for (int t = 0; t < 3; ++t)
    {
        auto& value = readout (under (layout::toneX[(size_t) t], layout::toneValueY, 100.0f));
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

    // ...and the bass's own, each bound through its own travel.
    bindTravel (comp, params::compThreshold, [] (float db) { return db / params::compThresholdMin; },
                [] (float t) { return t * params::compThresholdMin; }, [&compValue] (float db) { compValue.set (dbWhole (db)); });
    bindTravel (blend, params::ampBlend, [] (float v) { return v; }, [] (float t) { return t; },
                [&blendValue] (float v) { blendValue.set (percent (v)); });
    // CAB / DI: the cabinet at the head, the DI at the foot — the cabinet's share up its travel.
    bindTravel (diCab, params::cabMix, [] (float v) { return v / 100.0f; }, [] (float t) { return t * 100.0f; }, {});
    const char* effectIds[] { params::octaveAmount, params::filterAmount, params::chorusAmount };
    const char* effectNames[] { "OCTAVE", "ENVELOPE", "CHORUS" };
    for (size_t i = 0; i < 3; ++i)
    {
        auto& value = *effectValues[i];
        bindTravel (*effectFaders[i], effectIds[i], [] (float v) { return v; }, [] (float t) { return t; },
                    [&value] (float v) { value.set (percent (v)); });
        sayLeverAs (*effectFaders[i], effectNames[i], [this, id = effectIds[i]] { return percent (getParam (id)); });
    }
    // MID FREQ: the pack's frequency knob, its travel the knob's (Channels::frequencyOf) — or, on a pack with none, the
    // standard MIDDLE's frequency on a logarithmic travel.
    if (namz::rig::Tone freq; channels.frequencyToneOf (channels.active(), freq))
    {
        const auto freqId = channels.frequencyOf (channels.active());
        bindTravel (midFreq, freqId, [] (float v) { return v; }, [] (float t) { return t; },
                    [&midValue, freq] (float v) { midValue.set (hertz (knobHz (freq, v))); });
        sayLeverAs (midFreq, "FREQ", [this, freqId, freq] { return hertz (knobHz (freq, getParam (freqId.toRawUTF8()))); });
    }
    else
    {
        const auto midId = params::eqBellHz (0, 0);
        bindTravel (midFreq, midId, midTravel, midHz, [&midValue] (float hz) { midValue.set (hertz (juce::jlimit (params::bassMidMinHz, params::bassMidMaxHz, hz))); });
        sayLeverAs (midFreq, "FREQ", [this, midId] { return hertz (getParam (midId.toRawUTF8())); });
    }

    // THE CHANNEL LEVER: B15 or SVT — the gear and everything it was left with.
    addToStage (lever);
    lever.setBounds (layout::leverBox.toNearestInt());
    lever.handLight = handLight;
    lever.onPlace   = [this] (int at) { go (at); };
    lever.onRelease = [this] { showChannel(); };       // held, it follows the hand; let go, it follows the state
    lever.setComponentID ("lever");
    lever.setPlace (channels.active(), false, false);  // a window opens with it where the state is, not on its way there

    // THE CUTS' SWITCHES: the HPF by the strings, the LPF by the hand — both always in (channels::Lineup::pinned), the
    // switch choosing between their positions; a right click on either its positions' frequencies.
    // One line a switch, in a column right of DI/CAB: the name, the end positions' words either side of the slot.
    const auto lineAt = [this] (SlideSwitch& sw, float y)
    {
        const auto slot = juce::Rectangle<float> (layout::switchSlotW, layout::switchSlotH)
                              .withCentre ({ layout::switchSlotX + 0.5f * layout::switchSlotW, y });
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
    lineAt (lowCut, layout::switchLineY[0]);
    lineAt (highCut, layout::switchLineY[1]);
    lineAt (ultraLo, layout::switchLineY[2]);
    lineAt (ultraHi, layout::switchLineY[3]);

    // CABINET'S OWN SWITCH, over the column in the title's row (the owner, 07.11): its cap at the left takes the cabinet out
    // of the sound, the DI alone, and the name over the slider says OFF; at the right it is back. No words either side —
    // the row has no room for them beside CABINET and its corner's screw. The slider moved brings it back too (chooseSlot).
    cabinetOn.nameRoom = 0.0f;
    cabinetOn.wordRoom = 0.0f;
    lineAt (cabinetOn, layout::cabinetSwitchY);
    cabinetOn.setComponentID ("cabinetOn");

    // ...each saying what it is in the tag over it while the hand is on it (the owner, 03.11).
    const auto sayOver = [this] (SlideSwitch& sw, juce::String words)
    {
        sw.onHover = [this, &sw, words] (bool over)
        {
            const auto at = sw.getBoundsInParent().toFloat();
            nameTag.show ({ at.getCentreX(), at.getY() - 16.0f }, over ? words : juce::String());
        };
    };
    sayOver (lowCut, bass::strHint);
    sayOver (highCut, bass::stylHint);
    sayOver (cabinetOn, "The cabinet in the sound, or the DI alone");
    bindSwitch (lowCut, params::bassHpf);
    bindSwitch (highCut, params::bassLpf);
    bindSwitch (cabinetOn, params::cabOn);
    // ...and CAB / DI with the cabinet out: nothing to share between (the processor's share does nothing then), so it is
    // held where it stands, dimmed, out of the hand's reach (the owner, 07.11).
    follow (params::cabOn, [this] (float v)
    {
        const bool on = v > 0.5f;
        diCab.setEnabled (on);
        diCab.setAlpha (on ? 1.0f : 0.5f);
    })->sendInitialUpdate();

    // U-LO and U-HI: the pack's tone switches (Channels::toneSwitchOf), Ultra Lo and Ultra Hi — each on its tone slot,
    // a position its place in the pack's list. A pack with none has nothing for them to throw, and they are not there.
    const auto bindUltra = [this] (SlideSwitch& sw, int which)
    {
        namz::rig::Tone tone;
        const auto id = channels.toneSwitchOf (channels.active(), which);
        const bool plays = id.isNotEmpty() && channels.toneSwitchToneOf (channels.active(), which, tone);
        sw.setVisible (plays);
        if (! plays)
            return;
        sw.onHover = [this, &sw, words = bass::toneSwitchHint (tone.name, tone)] (bool over)
        {
            const auto at = sw.getBoundsInParent().toFloat();
            nameTag.show ({ at.getCentreX(), at.getY() - 16.0f }, over ? words : juce::String());
        };
        const int n = juce::jmax (1, (int) tone.positions.size() - 1);
        auto* a = follow (id.toRawUTF8(), [&sw, n] (float v) { sw.setPosition (juce::roundToInt (v * (float) n), false); });
        sw.onFlip = [a, n] (int to) { a->setValueAsCompleteGesture ((float) to / (float) n); };
        a->sendInitialUpdate();
    };
    bindUltra (ultraLo, 0);
    bindUltra (ultraHi, 1);
    ultraLo.setComponentID ("ultraLo");
    ultraHi.setComponentID ("ultraHi");
    lowCut.onMenu  = [this] { bass::Menu m; bass::addHpf (m, m.items, amp.apvts); bass::show (std::move (m), &lowCut); };
    highCut.onMenu = [this] { bass::Menu m; bass::addLpf (m, m.items, amp.apvts); bass::show (std::move (m), &highCut); };
    lowCut.setComponentID ("lowCut");
    highCut.setComponentID ("highCut");


    // THE CABINET'S SLIDER: a notch a cabinet of the bass's dozen (Channels' cabinet slots), set and fixed — no gear, no
    // window to change them (the owner, 06.11); over it on the panel, the cabinet that plays by its name — the slot's
    // under the hand while it moves the slider, OFF while the cabinet is out of the sound.
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
    cabinet.label = &readout (layout::cabinetName);     // the name over the slot, on the panel
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

/** THE SCREEN'S SLIDER MOVED: the slot at notch `at`, heard as the cap comes to it — not kept. */
void RedGrooveEditor::hearSlot (int at)
{
    dragAt = at;
    channels.hearSlot (at);
}

/** ...and let go in notch `at`: the playing gear moved to that slot, its cabinet kept — even the slot it began at, the
    sound having passed through others — and the cabinet back in the sound. A click that moved nothing keeps nothing. */
void RedGrooveEditor::chooseSlot (int at)
{
    if (dragAt >= 0)
    {
        channels.chooseSlot (at);
        channels.setCabinetOn (true);
    }
    dragAt = -1;
}

/** THE CABINETS, ON THE SCREEN'S CHEVRON: Ampeg's, drawn, in the window OrbitBass's are in (ui/CabWindow.h) — what it
    keeps put in the slot the slider stands at, and the cabinet back in the sound. */
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
            { "comp",  "COMP",   &comp,  params::compThreshold, "travel = threshold / -48 dB: 0 dB fully left, -48 dB fully right" } };
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
        r->setProperty ("sockets", list ({ juce::var ("drive"), juce::var ("comp"), juce::var ("tones") }));
        o->setProperty ("rings", r);
        o->setProperty ("knobNameY", layout::knobNameY);
        o->setProperty ("cabWordY", layout::cabWordY);
        o->setProperty ("diWordY", layout::diWordY);
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
        {
            auto* v = new Obj();        // BLEND, upright where its knob was
            v->setProperty ("id", "blend"); v->setProperty ("name", "BLEND"); v->setProperty ("box", rect (boxOf (blend)));
            v->setProperty ("slot", rect (layout::blendSlot));
            v->setProperty ("sprite", "fader-cap"); v->setProperty ("capLength", layout::faderCapLength);
            v->setProperty ("param", params::ampBlend); v->setProperty ("travel", "the value, 0..1, bottom to top"); v->setProperty ("reading", "percent");
            a.add (juce::var (v));
        }
        {
            auto* v = new Obj();        // CAB / DI, upright where DI/CAB's knob was: the cabinet at the head, the DI at the foot
            v->setProperty ("id", "diCab"); v->setProperty ("name", "CAB / DI"); v->setProperty ("box", rect (boxOf (diCab)));
            v->setProperty ("slot", rect (layout::diCabSlot));
            v->setProperty ("sprite", "fader-cap"); v->setProperty ("capLength", layout::faderCapLength);
            v->setProperty ("param", params::cabMix); v->setProperty ("travel", "the value / 100, bottom (DI) to top (CAB)");
            a.add (juce::var (v));
        }
        o->setProperty ("columns", a);
        auto* m = new Obj();
        m->setProperty ("box", rect (boxOf (midFreq))); m->setProperty ("slot", rect (layout::midFreqSlot));
        m->setProperty ("sprite", "fader-cap"); m->setProperty ("capLength", layout::midFreqCap); m->setProperty ("across", true);
        // MID FREQ as the face binds it (above): on a pack with a frequency knob of its own — the SGT-DI's Freq — that
        // knob's tone slot, its travel the slot's value 0..1 and its frequency where the pack's positions put it; no
        // map, there being none of the kinds said here for that. On a pack with none, the standard MIDDLE's frequency
        // on its logarithmic travel.
        if (namz::rig::Tone freq; channels.frequencyToneOf (channels.active(), freq))
        {
            m->setProperty ("param", channels.frequencyOf (channels.active()));
            m->setProperty ("map", juce::var());
        }
        else
        {
            m->setProperty ("param", params::eqBellHz (0, 0));
            auto* map = new Obj();
            map->setProperty ("kind", "log"); map->setProperty ("minHz", params::bassMidMinHz); map->setProperty ("maxHz", params::bassMidMaxHz);
            map->setProperty ("travel", "log (hz / minHz) / log (maxHz / minHz), left to right");
            m->setProperty ("map", map);
        }
        m->setProperty ("reading", "hertz");
        o->setProperty ("midFreq", m);
    }

    // THE CUTS' SWITCHES: two positions each, a word over each, the one it stands at lit; the parameter's value is the position.
    {
        struct S { const char* id; const SlideSwitch* sw; const char* left; const char* right; const char* param; };
        // CABINET's own switch is the app's alone: the page's face has none (the owner, 08.11).
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
        // U-LO and U-HI under them (the owner, 08.11: the page shows them as the app does): the pack's tone switches, each
        // on its tone slot — the parameter per channel, a position its normalised value, as many as the pack's tone has.
        struct U { const char* id; const SlideSwitch* sw; const char* name; const char* left; const char* right; int which; };
        const U ultras[] { { "ultraLo", &ultraLo, "U-LO", "-", "+", 0 }, { "ultraHi", &ultraHi, "U-HI", "0", "+", 1 } };
        for (const auto& u : ultras)
        {
            juce::Array<juce::var> perChannel;
            for (int c = 0; c < channels.lineup().count(); ++c)
                perChannel.add (juce::var (channels.toneSwitchOf (c, u.which)));
            const auto id = channels.toneSwitchOf (channels.active(), u.which);
            namz::rig::Tone tone;
            if (id.isEmpty() || ! channels.toneSwitchToneOf (channels.active(), u.which, tone))
                continue;
            const int positions = juce::jmax (2, (int) tone.positions.size());
            auto* v = new Obj();
            const auto box = boxOf (*u.sw);
            v->setProperty ("id", u.id); v->setProperty ("name", u.name);
            v->setProperty ("box", rect (box)); v->setProperty ("slot", rect (u.sw->slot + box.getPosition()));
            v->setProperty ("wordsY", u.sw->wordsY + box.getY()); v->setProperty ("wordRoom", u.sw->wordRoom);
            v->setProperty ("words", list ({ juce::var (u.left), juce::var (u.right) }));
            v->setProperty ("param", id); v->setProperty ("paramPerChannel", perChannel);
            juce::Array<juce::var> values;
            for (int i = 0; i < positions; ++i)
                values.add ((double) i / (double) (positions - 1));
            v->setProperty ("values", values);
            v->setProperty ("valuesAre", "the parameter's normalised value, a position each, left to right");
            a.add (juce::var (v));
        }
        o->setProperty ("switches", a);
    }

    // THE SQUEEZE: COMP's gain reduction as a column filling down, its full span; the cabinet's screen.
    {
        auto* g = new Obj();
        g->setProperty ("box", rect (boxOf (squeeze))); g->setProperty ("slot", rect (layout::grBar)); g->setProperty ("across", true);
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
                          { "midFreq", "hertz: 400 Hz, 1.2 kHz" }, { "in", "dB, a tenth: +1.5 dB, 0.0 dB" },
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
