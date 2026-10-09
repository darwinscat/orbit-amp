// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "CatpitEditor.h"
#include "CatpitLayout.h"

#include "../Parameters.h"
#include "../ui/AboutConfig.h"

#include <CatpitData.h>
#include <felitronics/appkit/BrandAssets.h>
#include <felitronics/appkit/TextPrompt.h>

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
    // The six keys under the tone knobs: the two filters, then the pack's own layers. The row has room
    // for a short name under each; the full one shows on hover. FAT is a layer only the two hottest
    // packs carry.
    struct Switch { const char* name; const char* shortName; };
    enum { btHpf, btLpf, btFat, btBright, btDepth, btPresence };
    constexpr std::array<Switch, 6> switches {{
        { "HIGHPASS", "HPF"    },
        { "LOWPASS",  "LPF"    },
        { "FAT",      "FAT"    },
        { "BRIGHT",   "BRIGHT" },
        { "DEPTH",    "DEPTH"  },
        { "PRESENCE", "PRES"   },
    }};
    constexpr float keyTracking = 0.04f;

    // The overhead keys, in hole order: the history on the left of the divider, the four registers —
    // four whole amps to compare, each with a history of its own — on the right.
    enum { ovUndo, ovRedo, ovFirstRegister };
    constexpr int numRegisterKeys = 4;

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

    const juce::Colour labelLilac { 0xffc9b8f5 };

    /** The cockpit's own tag on a family link, for the site's analytics. */
    constexpr const char* campaign = "utm_source=catpitamp&utm_medium=plugin";

    /** THE BUILD WINDOW, AS THE COCKPIT'S. What the build is and what it was built against is the same
        for both faces of the processor (aboutConfig); what is the cockpit's own is its name, its mark —
        a small starship head-on, its fins a cat's ears — and its name where the notice says whose
        product this is. The window outlives the badge that opens it, so the mark is shared, not lent. */
    felitronics::appkit::VersionBadge::Config aboutTheCockpit()
    {
        auto config = aboutConfig ("CatpitAmp", "https://darwinscat.com", campaign);

        std::shared_ptr<juce::Drawable> mark { juce::Drawable::createFromImageData (
            CatpitData::mark_svg, (size_t) CatpitData::mark_svgSize).release() };

        config.drawMark = [mark] (juce::Graphics& g, float cx, float cy, float d)
        {
            if (mark != nullptr)
                mark->drawWithin (g, { cx - d * 0.5f, cy - d * 0.5f, d, d }, juce::RectanglePlacement::centred, 1.0f);
        };
        config.notice = config.notice.replace ("OrbitAmp", "CatpitAmp");
        return config;
    }

    /** A turning arrow — three quarters of a circle with a head on its open end. Forward turns
        clockwise (redo); back is its mirror (undo). */
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

    juce::Rectangle<int> around (juce::Point<float> c, float w, float h)
    {
        return juce::Rectangle<float> (w, h).withCentre (c).toNearestInt();
    }

    /** Where a lower plate's name stands: over its inset. */
    juce::Rectangle<float> plateName (size_t plate)
    {
        const auto& inset = layout::sliderInsets[plate];
        return juce::Rectangle<float> (inset.getWidth(), 30.0f).withCentre ({ inset.getCentreX(), 640.0f });
    }
}

//==============================================================================
Cockpit::Cockpit()
{
    setInterceptsMouseClicks (false, false);
}

juce::Rectangle<float> Cockpit::tagBounds() const
{
    if (tagText.isEmpty())
        return {};
    const auto font = labelFont (layout::px13).withExtraKerningFactor (0.06f);
    const float w = juce::GlyphArrangement::getStringWidth (font, tagText) + 30.0f;
    return juce::Rectangle<float> (w, 32.0f).withCentre (tagAnchor);
}

void Cockpit::showTag (juce::Point<float> anchor, juce::String text)
{
    repaint (tagBounds().expanded (2.0f).getSmallestIntegerContainer());
    tagAnchor = anchor;
    tagText = std::move (text);
    repaint (tagBounds().expanded (2.0f).getSmallestIntegerContainer());
}

void Cockpit::setHistory (bool undo, bool redo, int reg)
{
    if (undo == canUndo && redo == canRedo && reg == activeRegister)
        return;
    canUndo = undo;
    canRedo = redo;
    activeRegister = reg;
    repaint (juce::Rectangle<float> (layout::holes.front().x - 40.0f, 12.0f,
                                     layout::holes.back().x - layout::holes.front().x + 80.0f, 72.0f).toNearestInt());
}

/** The signs by the overhead keys. Not in the cached layer: they change with what there is to undo and
    with the register that is playing. Every key has its sign to its left and a small lamp over it —
    the render painted lamps over the four register holes only, so the two over the history keys are
    drawn here to match. */
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

void Cockpit::paint (juce::Graphics& g)
{
    using namespace layout;

    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (! layer.isValid() || std::abs (scale - layerScale) > 1.0e-3f)
    {
        layerScale = scale;
        layer = juce::Image (juce::Image::ARGB, juce::roundToInt (width * scale), juce::roundToInt (height * scale), true);
        juce::Graphics lg (layer);
        lg.addTransform (juce::AffineTransform::scale (scale));
        lg.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        paintStatic (lg, scale);
    }
    g.drawImage (layer, juce::Rectangle<float> (0.0f, 0.0f, width, height));
    paintOverhead (g);

    if (const auto tag = tagBounds(); ! tag.isEmpty())
    {
        g.setColour (juce::Colour (0xe8050509));
        g.fillRoundedRectangle (tag, 6.0f);
        g.setColour (violet.withAlpha (0.7f));
        g.drawRoundedRectangle (tag, 6.0f, 1.2f);
        drawLabel (g, tagText, tag, px13, juce::Colour (0xffece9f6), 0.06f);
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

    // THE MARK, ENGRAVED. Its light parts are cut into the plate: the cut's upper wall falls into
    // shadow, its lower lip catches the light, and its floor is bare metal a shade brighter than the
    // plate — so the mark reads as worked into the hull, not printed on it.
    const auto& cut = MarkCut::at (scale).cut;
    if (! cut.isValid())
        return;

    const float px = 1.0f / juce::jmax (1.0f, scale) * 1.6f;   // the wall's depth, about a pixel and a half
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.drawImage (cut, emblemMark.translated (-0.5f * px, -px), juce::RectanglePlacement::stretchToFit, true);
    g.setColour (juce::Colour (0xffd8d2ee).withAlpha (0.28f));
    g.drawImage (cut, emblemMark.translated (0.4f * px, px), juce::RectanglePlacement::stretchToFit, true);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff8a8698), emblemMark.getCentreX(), emblemMark.getY(),
                                             juce::Colour (0xff4b4858), emblemMark.getCentreX(), emblemMark.getBottom(), false));
    g.drawImage (cut, emblemMark, juce::RectanglePlacement::stretchToFit, true);
}

void Cockpit::paintStatic (juce::Graphics& g, float scale) const
{
    using namespace layout;

    const auto& bg = Art::get().bg;
    if (bg.isValid())
        g.drawImage (bg, juce::Rectangle<float> (0.0f, 0.0f, width, height));

    paintEmblem (g, scale);

    // The keys' names, under them.
    const auto keyX = keyCentres();
    for (size_t i = 0; i < switches.size(); ++i)
        drawLabel (g, switches[i].shortName, juce::Rectangle<float> (120.0f, 20.0f).withCentre ({ keyX[i], keyNameY }),
                   px11, labelLilac, keyTracking);

    drawLabel (g, "DRIVE", juce::Rectangle<float> (220.0f, 30.0f).withCentre ({ drive.centre.x, 394.0f }), px15, labelLilac);
    const char* toneNames[] { "BASS", "MIDDLE", "TREBLE" };
    for (size_t i = 0; i < tones.size(); ++i)
        drawLabel (g, toneNames[i], juce::Rectangle<float> (150.0f, 28.0f).withCentre ({ tones[i].centre.x, 394.0f }), px13, labelLilac);

    drawLabel (g, "TURBO", juce::Rectangle<float> (160.0f, 26.0f).withCentre ({ turboFrame.getCentreX(), 400.0f }), px13, orange);

    // COMBO's name. REVERB's and DELAY AMOUNT's are live legends — they also say what is chosen.
    drawLabel (g, "COMBO", plateName (2), px13, labelLilac);

    drawLabel (g, "INPUT",  juce::Rectangle<float> (170.0f, 26.0f).withCentre ({ 117.0f, 314.0f }),  px13, labelLilac);
    drawLabel (g, "OUTPUT", juce::Rectangle<float> (170.0f, 26.0f).withCentre ({ 1557.0f, 314.0f }), px13, labelLilac);

    // The tuner's name; its screen is the live TunerFace.
    drawLabel (g, "TUNER", juce::Rectangle<float> (200.0f, 26.0f).withCentre ({ tunerScreen.getCentreX(), 781.0f }), px13, labelLilac);
}

//==============================================================================
CatpitEditor::CatpitEditor (AmpProcessor& p)
    : juce::AudioProcessorEditor (&p), amp (p), channels (p), presets (p, channels),
      drive (Art::get().knobDrive, drivePointer),
      reverbName (layout::px13),
      delayWord (layout::px13),
      delayNote (layout::px13),
      delayTempo (layout::px13),
      inMeter ([this] { return amp.gateKeyDb.load (std::memory_order_relaxed); }),
      outMeter ([this] { return amp.outDb.load (std::memory_order_relaxed); }),
      tuner (p.tunerEar),
      version (layout::px13),
      about (p.updateChecker(), aboutTheCockpit(), p.pluginFormat()),
      presetDoor (layout::px13)
{
    using namespace layout;

    // Everything this constructor sets is the face settling onto the state, not a player's edit: with
    // undo on the panel it must not leave a step to press back through.
    const felitronics::appkit::CompareHistory::ScopedSuppress settling (amp.history);

    setUpChain (channels.adopt());
    channels.align();   // the packs and cabinets are files, and the folders may have changed since the state was saved

    addAndMakeVisible (stage);
    stage.setBounds (0, 0, (int) width, (int) height);

    stage.addAndMakeVisible (space);
    space.setBounds (windshield.toNearestInt());
    stage.addAndMakeVisible (cockpit);
    cockpit.setBounds (stage.getLocalBounds());

    // Knobs fill their sockets: the drive's knurl sits inside the violet ring, a tone knob's own bezel
    // lies over its ring.
    stage.addAndMakeVisible (drive);
    drive.setBounds (around (layout::drive.centre, layout::drive.radius * 1.86f, layout::drive.radius * 1.86f));
    for (size_t i = 0; i < tones.size(); ++i)
    {
        tones[i] = std::make_unique<ArtKnob> (Art::get().knobTone, tonePointer);
        stage.addAndMakeVisible (*tones[i]);
        const float d = layout::tones[i].radius * 2.05f;
        tones[i]->setBounds (around (layout::tones[i].centre, d, d));
    }

    const auto keyX = keyCentres();
    for (size_t i = 0; i < buttons.size(); ++i)
    {
        buttons[i] = std::make_unique<PushButton> (switches[i].name);
        auto& b = *buttons[i];
        stage.addAndMakeVisible (b);
        const juce::Point<float> at { keyX[i], keyRowY };
        b.setBounds (around (at, keySize, keySize));
        b.onHover = [this, at, &b] (bool over)
        {
            cockpit.showTag (at.translated (0.0f, -40.0f), over ? b.name + "  " + b.stateText() : juce::String());
        };
    }

    // UNDO and REDO, in the two holes left of the overhead divider: plain keys over the processor's own
    // history — the same one OrbitAmp's header drives.
    for (const size_t i : { (size_t) ovUndo, (size_t) ovRedo })
    {
        overhead[i] = std::make_unique<PushButton> (i == ovUndo ? "UNDO" : "REDO");
        auto& key = *overhead[i];
        key.latching = false;
        stage.addAndMakeVisible (key);
        key.setBounds (around (holes[i], 40.0f, 40.0f));
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
        key.setBounds (around (holes[i], 40.0f, 40.0f));
    }
    amp.history.onHistoryChanged = [this] { refreshHistory(); };
    refreshHistory();

    // The throttle picks the channel — its pack, and everything that channel was left with.
    stage.addAndMakeVisible (lever);
    lever.setBounds (leverFrame.toNearestInt().withLeft (juce::roundToInt (leverFrame.getX()) - Lever::overhang));
    lever.onTravel = [this] (float pos)
    {
        const float t = pos / (float) (Lever::numDetents - 1);
        space.setThrottle (t);
        if (glSky != nullptr)
            glSky->setThrottle (t);
    };
    lever.onDetent = [this] (int d)
    {
        endTapping();
        channels.select (d);    // refused when that channel's pack is not installed...
        showChannel();          // ...and then this puts the lever back where the sound is
    };
    lever.onRelease = [this] { showChannel(); };   // held, it follows the hand; let go, it follows the state
    // A prototype's hook for looking at the window at speed without a hand on the lever.
    if (const auto d = juce::SystemStats::getEnvironmentVariable ("CATPIT_DETENT", {}); d.isNotEmpty())
        channels.select (d.getIntValue());
    lever.setDetent (channels.active(), false, false);

    // TURBO moves and is remembered, but drives nothing yet: it will be a layer inside the pack.
    stage.addAndMakeVisible (turbo);
    turbo.setBounds (turboFrame.toNearestInt());
    turbo.onChange = [this] (bool on) { channels.setFlag (Channels::turbo, on); };

    const juce::Colour lineFrom[] { orange.darker (0.3f), violet, orange.darker (0.3f) };
    const juce::Colour lineTo[]   { orange,               orange, orange };
    for (size_t i = 0; i < sliders.size(); ++i)
    {
        // COMBO has a position for every cabinet the cockpit has; a selector needs two to be one.
        sliders[i] = std::make_unique<PlateSlider> (lineFrom[i], lineTo[i],
                                                    i == 2 ? juce::jmax (2, channels.cabinets().size()) : 0);
        stage.addAndMakeVisible (*sliders[i]);
        sliders[i]->setBounds (sliderInsets[i].toNearestInt());
    }

    // REVERB's name carries the reverb's character, and a click on it opens the list — the same six
    // OrbitAmp's reverb block offers.
    stage.addAndMakeVisible (reverbName);
    reverbName.setBounds (plateName (0).toNearestInt());
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
    delayDiv = follow (params::delayDiv, [this] (float) { showDelayTime(); });
    delayBpm = follow (params::delayBpm, [this] (float) { showDelayTime(); });
    showDelayTime();

    // COMBO steps through the cockpit's own cabinets; the one it sits on shows by name while the hand
    // is on it. With fewer than two there is nothing to choose between, and it stands dim.
    auto& combo = *sliders[2];
    if (channels.cabinets().size() >= 2)
    {
        combo.onStep  = [this] (int s) { chooseCombo (s); showComboTag (true); };
        combo.onHover = [this] (bool over) { showComboTag (over); };
    }
    else
    {
        combo.setEnabled (false);
        combo.setAlpha (0.35f);
    }

    for (auto* c : std::initializer_list<juce::Component*> { &inFader, &outFader, &inMeter, &outMeter, &tuner })
        stage.addAndMakeVisible (c);
    tuner.setBounds (tunerScreen.toNearestInt());
    inFader.setBounds (layout::inFader.toNearestInt());
    outFader.setBounds (layout::outFader.toNearestInt());
    inMeter.setBounds (layout::inMeter.toNearestInt());
    outMeter.setBounds (layout::outMeter.toNearestInt());

    // What has an obvious home in the engine is wired to it; the rest only moves for now.
    bind (drive, params::preampGain);
    bind (*sliders[0], params::reverbMix);
    bind (*sliders[1], params::delayMix);
    bind (inFader, params::inTrim);
    bind (outFader, params::outTrim);

    // The tone stack is the preamp's own console in its universal mode — the same three bands on every
    // pack, centre detent at 0 dB — until the cockpit's packs carry a captured tone of their own.
    constexpr int preampConsole = 1;
    bind (*tones[0], params::eqLoDb (preampConsole));
    bind (*tones[1], params::eqBellDb (preampConsole, 0));
    bind (*tones[2], params::eqHiDb (preampConsole));

    // HPF and LPF switch the cabinet's own cut filters in and out; FAT, BRIGHT, DEPTH and PRESENCE are
    // kept with the channel and wait for the layers the packs will carry.
    bindButton (*buttons[btHpf], params::cabHpfOn);
    bindButton (*buttons[btLpf], params::cabLpfOn);
    const std::pair<int, Channels::Flag> layers[] { { btFat, Channels::fat }, { btBright, Channels::bright },
                                                    { btDepth, Channels::depth }, { btPresence, Channels::presence } };
    for (const auto& [key, flag] : layers)
        buttons[(size_t) key]->onChange = [this, f = flag] (bool on) { channels.setFlag (f, on); };

    showChannel();

    // The mark lights under the pointer and leads to the maker's site.
    stage.addAndMakeVisible (emblem);
    {
        juce::Path face;
        face.startNewSubPath (emblemFace[0]);
        for (size_t i = 1; i < emblemFace.size(); ++i)
            face.lineTo (emblemFace[i]);
        emblem.setBounds (face.getBounds().getSmallestIntegerContainer());
    }
    emblem.onClick = [] { juce::URL (juce::String ("https://darwinscat.com?") + campaign).launchInDefaultBrowser(); };

    // The version, and behind it the window with everything else about the build. That window is
    // hung on THIS editor rather than on the top-level window: the sky's GL surface lies over everything
    // in the window that is not this editor's own child, and a dialog on the window would open under it.
    about.setBrandTypeface (theme::displayTypeface());
    addChildComponent (about);
    version.upper = false;
    version.valueColour = labelLilac.withAlpha (0.85f);
    version.set ({}, "v" + amp.updateChecker().currentVersion());
    version.onClick = [this] { about.showAbout (this); };
    stage.addAndMakeVisible (version);
    version.setBounds (around (versionPlate, 124.0f, 30.0f));

    // PRESET, on the mirrored plate.
    presetDoor.valueColour = labelLilac.withAlpha (0.85f);
    presetDoor.set ({}, "PRESET");
    presetDoor.onClick = [this] { showPresetMenu(); };
    stage.addAndMakeVisible (presetDoor);
    presetDoor.setBounds (around (presetPlate, 140.0f, 30.0f));

    timerCallback();
    startTimerHz (4);   // both dots are a lookup, a few times a second: nobody needs either sooner

    if (juce::SystemStats::getEnvironmentVariable ("CATPIT_SKY", {}).equalsIgnoreCase ("cpu"))
        useCpuSky();
    else
    {
        space.setVisible (false);
        glSky = std::make_unique<GlSky> (gl);
        glSky->setThrottle ((float) lever.getDetent() / (float) (Lever::numDetents - 1));
        gl.setRenderer (glSky.get());
        gl.setComponentPaintingEnabled (true);
        gl.setContinuousRepainting (true);
        gl.attachTo (*this);

        // A shader that will not build on this machine leaves the window dark; look once, and fall back.
        juce::Timer::callAfterDelay (800, [safe = juce::Component::SafePointer<CatpitEditor> (this)]
        {
            if (safe != nullptr && safe->glSky != nullptr && safe->glSky->failed())
                safe->useCpuSky();
        });
    }

    // The keys. A click anywhere on the face brings the focus here, since nothing on it wants the
    // keyboard for itself; and the face takes it once when it first comes up, so the keys answer
    // before the first click too.
    setWantsKeyboardFocus (true);
    juce::Timer::callAfterDelay (300, [safe = juce::Component::SafePointer<CatpitEditor> (this)]
    {
        if (safe != nullptr && safe->isShowing())
            safe->grabKeyboardFocus();
    });

    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) width / (double) height);
    setResizeLimits (minWindowWidth, juce::roundToInt ((float) minWindowWidth * height / width),
                     minWindowWidth * 2, juce::roundToInt ((float) minWindowWidth * 2.0f * height / width));
    setSize (minWindowWidth, juce::roundToInt ((float) minWindowWidth * height / width));
}

CatpitEditor::~CatpitEditor()
{
    amp.history.onHistoryChanged = nullptr;   // the history outlives this window
    endTapping();
    gl.detach();
}

/** A STATE THE COCKPIT HAS NOT SEEN — a session saved before it existed, loaded while the window is
    open — is made its own the way the constructor makes the first one: the fixed chain, the channel
    whose pack is playing, a cabinet COMBO can reach. */
void CatpitEditor::adoptState()
{
    adoptionPending = false;
    if (channels.adopted())
        return;

    const felitronics::appkit::CompareHistory::ScopedSuppress settling (amp.history);
    setUpChain (channels.adopt());
    channels.align();
    showChannel();
}

void CatpitEditor::timerCallback()
{
    version.setDot (amp.updateChecker().updateAvailable());
    presetDoor.setDot (presets.changed());

    // A host starts or stops saying its tempo whenever it likes.
    if (tempoIsOwn == (amp.runsInHost() && amp.hostTempoBpm() > 0.0f))
        showDelayTime();
}

void CatpitEditor::refreshHistory()
{
    const auto& history = amp.history;
    cockpit.setHistory (history.canUndo(), history.canRedo(), history.active());
    for (int r = 0; r < numRegisterKeys; ++r)
        overhead[(size_t) (ovFirstRegister + r)]->setOn (r == history.active(), false);

    showChannel();   // an undo, a redo or another register may have brought a different channel with it

    // ...or a state with no channels in it at all. Adopting it writes, and this is the history's own
    // callback, so it is done on the way out.
    if (! channels.adopted() && ! adoptionPending)
    {
        adoptionPending = true;
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<CatpitEditor> (this)]
        {
            if (safe != nullptr)
                safe->adoptState();
        });
    }
}

/** Puts the face where the state is: the lever in the playing channel's detent, and the controls that
    live in the channels' node rather than in a parameter. Reads only — it is called from inside the
    history's own callbacks. */
void CatpitEditor::showChannel()
{
    const int channel = channels.active();
    if (! lever.isHeld() && lever.getDetent() != channel)
        lever.setDetent (channel, true, false);

    buttons[btFat]->setEnabled (channel >= 2);
    buttons[btFat]->setOn      (channels.flag (Channels::fat),      false);
    buttons[btBright]->setOn   (channels.flag (Channels::bright),   false);
    buttons[btDepth]->setOn    (channels.flag (Channels::depth),    false);
    buttons[btPresence]->setOn (channels.flag (Channels::presence), false);
    turbo.setOn (channels.flag (Channels::turbo), false);

    // COMBO's cap, on the cabinet that is playing. (This is first called while the face is still being
    // built, before the plates exist.)
    if (const int playing = channels.cabinet(); playing >= 0 && sliders[2] != nullptr)
        sliders[2]->setStep (playing, false);
}

void CatpitEditor::showRegisterMenu (int index)
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
            item.action = [safe = juce::Component::SafePointer<CatpitEditor> (this), index, i]
            {
                if (safe != nullptr)     // the action is posted: the window may be gone by the time it runs
                    safe->amp.history.copyRegister (index, i);
            };
            menu.addItem (std::move (item));
        }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (overhead[(size_t) (ovFirstRegister + index)].get()));
}

bool CatpitEditor::keyPressed (const juce::KeyPress& key)
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

    // 1 2 3 4: the throttle goes to that channel, as if a hand had put it there.
    if (! key.getModifiers().isAnyModifierKeyDown())
    {
        const int channel = key.getKeyCode() - '1';
        if (channel >= 0 && channel < Lever::numDetents)
        {
            if (! lever.isHeld())
                lever.setDetent (channel, true);
            return true;
        }
    }

    return false;
}

void CatpitEditor::resized()
{
    stage.setTransform (juce::AffineTransform::scale ((float) getWidth() / layout::width));
    placeSky();
}

void CatpitEditor::placeSky()
{
    if (glSky != nullptr)
    {
        const float s = (float) getWidth() / layout::width;
        glSky->setArea (layout::windshield * s, { (float) getWidth(), (float) getHeight() });
    }
}

void CatpitEditor::useCpuSky()
{
    gl.detach();
    glSky.reset();
    space.setVisible (true);
}

void CatpitEditor::setParam (const char* paramId, float value)
{
    if (auto* p = amp.apvts.getParameter (paramId))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

float CatpitEditor::getParam (const char* paramId) const
{
    if (auto* p = amp.apvts.getParameter (paramId))
        return p->convertFrom0to1 (p->getValue());
    return 0.0f;
}

/** THE COCKPIT'S CHAIN IS FIXED: one captured amp (the boost now lives inside the packs), the delay,
    a hall, the cabinet. There is no strip to switch links in or out, so the face puts them there. */
void CatpitEditor::setUpChain (bool firstTime)
{
    setParam (params::tunerPresent,  1.0f);   // the tuner bay is always there, and always listening
    setParam (params::tunerOn,       1.0f);
    setParam (params::inPresent,     1.0f);   // the INPUT and OUTPUT faders are these links' trims
    setParam (params::inOn,          1.0f);
    setParam (params::outPresent,    1.0f);
    setParam (params::outOn,         1.0f);
    setParam (params::boostPresent,  0.0f);
    setParam (params::preampPresent, 1.0f);
    setParam (params::preampOn,      1.0f);
    setParam (params::delayPresent,  1.0f);
    setParam (params::delayOn,       1.0f);
    setParam (params::delaySync,      1.0f);  // the delay keeps time, always: the host's when there is one...
    setParam (params::delayHostTempo, 1.0f);  // ...and its own BPM when there is not
    setParam (params::reverbPresent, 1.0f);
    setParam (params::reverbOn,      1.0f);
    if (firstTime)                            // after that the reverb is the player's to choose
        setParam (params::reverbType, (float) params::reverbCharacters.indexOf (Channels::defaultReverb));
    setParam (params::cabPresent,    1.0f);
    setParam (params::cabOn,         1.0f);

    // BASS / MIDDLE / TREBLE ride the preamp's universal console, not the device's own tone.
    setParam (params::blockEqMode (params::preampId).toRawUTF8(), (float) params::eqModes.indexOf ("Universal EQ"));

    // HPF / LPF are on-off: the corners and slopes stay where OrbitCab puts them until set by ear.
    setParam (params::cabHpfHz, params::cabHpfDefaultHz);
    setParam (params::cabLpfHz, params::cabLpfDefaultHz);
}

void CatpitEditor::chooseCombo (int step)
{
    channels.chooseCabinet (step);
}

void CatpitEditor::showComboTag (bool show)
{
    const auto& inset = layout::sliderInsets[2];
    const auto& cabinets = channels.cabinets();
    const int step = sliders[2]->getStep();
    cockpit.showTag ({ inset.getCentreX(), inset.getY() - 52.0f },
                     show && step < cabinets.size() ? Channels::cabinetName (cabinets.getReference (step)) : juce::String());
}

template <typename Control>
void CatpitEditor::bind (Control& control, const juce::String& paramId)
{
    auto* p = amp.apvts.getParameter (paramId);
    jassert (p != nullptr);   // a binding to an id the processor does not have is a typo, not an option
    if (p == nullptr)
        return;

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
    attachments.push_back (std::move (att));
}

juce::ParameterAttachment* CatpitEditor::follow (const char* paramId, std::function<void (float)> moved)
{
    auto* p = amp.apvts.getParameter (paramId);
    jassert (p != nullptr);
    attachments.push_back (std::make_unique<juce::ParameterAttachment> (*p, std::move (moved)));
    return attachments.back().get();
}

/** What the delay's plate says, and where the three stand: the word and the note value always; the
    tempo only while it is the delay's own to set — a host that is conducting has the say, and there is
    nothing to drag or tap. */
void CatpitEditor::showDelayTime()
{
    delayWord.set ("DELAY");
    delayNote.set ({}, params::delayDivisions[juce::jlimit (0, params::delayDivisions.size() - 1,
                                                            juce::roundToInt (getParam (params::delayDiv)))]);

    tempoIsOwn = ! (amp.runsInHost() && amp.hostTempoBpm() > 0.0f);
    delayTempo.setVisible (tempoIsOwn);
    delayTempo.set ({}, juce::String (juce::roundToInt (getParam (params::delayBpm))) + " BPM");

    // Side by side, the row centred over the plate.
    const auto row = plateName (1);
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
void CatpitEditor::tapTempo()
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
void CatpitEditor::endTapping()
{
    if (! tapping)
        return;
    tapping = false;
    tapPause.stopTimer();
    tapGaps.clear();
    amp.history.endGesture();
}

void CatpitEditor::showDelayMenu()
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
                        [safe = juce::Component::SafePointer<CatpitEditor> (this)] (int picked)
                        {
                            if (safe != nullptr && picked > 0)
                                safe->delayDiv->setValueAsCompleteGesture ((float) (picked - 1));
                        });
}

/** EVERYTHING ABOUT PRESETS IS IN HERE: the plate only says the word. Whose sound this is stands at the
    top; then the list, the one that is loaded ticked; then what can be done with it. */
void CatpitEditor::showPresetMenu()
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
                        [safe = juce::Component::SafePointer<CatpitEditor> (this), names, current, onDisk] (int picked)
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

void CatpitEditor::showReverbMenu()
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
                        [safe = juce::Component::SafePointer<CatpitEditor> (this)] (int picked)
                        {
                            if (safe != nullptr && picked > 0)
                                safe->reverbType->setValueAsCompleteGesture ((float) (picked - 1));
                        });
}

void CatpitEditor::bindButton (PushButton& button, const char* paramId)
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

} // namespace catpit
} // namespace orbitamp
