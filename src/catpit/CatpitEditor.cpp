// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "CatpitEditor.h"
#include "CatpitLayout.h"
#include "CatpitMidi.h"
#include "CatpitStandalone.h"

#include "../Parameters.h"
#include "../core/ProcessMemory.h"
#include "../ui/AboutConfig.h"
#include "../ui/LoadBreakdown.h"

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

    constexpr int preampConsole = 1;        // the preamp's universal console: kept flat, the tones are the pack's

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

    /** How the stars fly, and what the windshield shows — settings of the machine's, in the cockpit's
        settings file. The stars are kept as a word, in the order of CatpitEditor::Stars. */
    const juce::Identifier starsKey  { "stars" };

    // The INPUT lever's zero — see CatpitEditor.h. Kept with the machine's settings.
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

    // TURBO's push — see CatpitEditor.h. Kept with the machine's settings.
    const juce::Identifier turboKey { "turbo_db" };
    constexpr int turboDefault = 9;
    constexpr std::array<int, 4> turboPushes { 3, 6, 9, 12 };

    int snapTurbo (int db)
    {
        int best = turboPushes.front();
        for (const int push : turboPushes)
            if (std::abs (push - db) < std::abs (best - db))
                best = push;
        return best;
    }

    // How DRIVE goes between the pack's captures — see CatpitEditor.h. Kept with the machine's settings.
    const juce::Identifier driveStepsKey { "drive_steps" };

    // Where a capture was last saved to (saveCapture): the next one is offered the same folder.
    const juce::Identifier captureFolderKey { "capture_folder" };

    const juce::Identifier windowKey { "window" };
    const juce::StringArray starsWords { "opengl", "cpu", "off" };

    // How many pixels of images the GL context may hold as textures before it starts throwing the
    // oldest out — room for the layer at the largest window on a 3x screen, and the art beside it.
    constexpr size_t glTexturePixels = 32u * 1024u * 1024u;

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
void Cockpit::setKeysLive (const std::array<bool, 6>& live)
{
    if (live == keysLive)
        return;
    keysLive = live;
    repaint();
}

/** The keys' names, under them — a dimmed key's as dim as the key. */
void Cockpit::paintKeyNames (juce::Graphics& g) const
{
    using namespace layout;

    const auto keyX = keyCentres();
    for (size_t i = 0; i < switches.size(); ++i)
        drawLabel (g, switches[i].shortName, juce::Rectangle<float> (120.0f, 20.0f).withCentre ({ keyX[i], keyNameY }),
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
    paintKeyNames (g);
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
NameTag::NameTag()
{
    setInterceptsMouseClicks (false, false);
}

void NameTag::show (juce::Point<float> anchor, const juce::String& words)
{
    text = words;
    setVisible (text.isNotEmpty());
    if (text.isEmpty())
        return;

    const auto font = labelFont (layout::px13).withExtraKerningFactor (0.06f);
    const float w = juce::GlyphArrangement::getStringWidth (font, text) + 30.0f;
    // A pixel of room all round for the outline, which is stroked on the edge of the plate.
    auto box = juce::Rectangle<float> (w, 32.0f).withCentre (anchor).expanded (2.0f).getSmallestIntegerContainer();
    if (const auto* parent = getParentComponent())
        box = box.constrainedWithin (parent->getLocalBounds());      // a tag by the stage's edge stays on the stage
    setBounds (box);
    repaint();
}

void NameTag::paint (juce::Graphics& g)
{
    const auto tag = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (juce::Colour (0xe8050509));
    g.fillRoundedRectangle (tag, 6.0f);
    g.setColour (violet.withAlpha (0.7f));
    g.drawRoundedRectangle (tag, 6.0f, 1.2f);
    drawLabel (g, text, tag, layout::px13, juce::Colour (0xffece9f6), 0.06f);
}

//==============================================================================
Sheet::Sheet (std::unique_ptr<juce::Component> c) : content (std::move (c))
{
    addAndMakeVisible (*content);
    setWantsKeyboardFocus (true);
}

juce::Rectangle<int> Sheet::plate() const
{
    constexpr int margin = 12;
    return getLocalBounds().withSizeKeepingCentre (content->getWidth() + margin * 2, content->getHeight() + margin * 2);
}

void Sheet::resized()
{
    content->setBounds (getLocalBounds().withSizeKeepingCentre (content->getWidth(), content->getHeight()));
}

void Sheet::paint (juce::Graphics& g)
{
    // A light scrim, as the build window has: the cockpit steps back, it does not go dark.
    g.fillAll (juce::Colours::black.withAlpha (0.22f));

    // The plate is the name tag's, grown to a window: near-black, the family's violet round its edge.
    const auto face = plate().toFloat();
    g.setColour (juce::Colour (0xff0b0a12));
    g.fillRoundedRectangle (face, 8.0f);
    g.setColour (violet.withAlpha (0.7f));
    g.drawRoundedRectangle (face.reduced (0.6f), 8.0f, 1.2f);
}

void Sheet::mouseDown (const juce::MouseEvent& e)
{
    if (! plate().contains (e.getPosition()) && onDismiss)
        onDismiss();
}

void Sheet::childBoundsChanged (juce::Component*)
{
    resized();
    repaint();
}

bool Sheet::keyPressed (const juce::KeyPress& key)
{
    if (key != juce::KeyPress::escapeKey)
        return false;
    if (onDismiss)
        onDismiss();
    return true;
}

//==============================================================================
CatpitEditor::CatpitEditor (AmpProcessor& p)
    : juce::AudioProcessorEditor (&p), amp (p), channels (p), presets (p, channels),
      monitor (p),
      drive (Art::get().knobDrive, drivePointer),
      reverbName (layout::px13),
      cabName (layout::px13),
      delayWord (layout::px13),
      delayNote (layout::px13),
      delayTempo (layout::px13),
      inMeter ([this] { return amp.gateKeyDb.load (std::memory_order_relaxed); }),
      outMeter ([this] { return amp.outDb.load (std::memory_order_relaxed); }),
      tuner (p.tunerEar),
      version (layout::px13),
      about (p.updateChecker(), aboutTheCockpit(), p.pluginFormat()),
      load (layout::px11),
      memory (layout::px11),
      presetDoor (layout::px13)
{
    using namespace layout;

    // Everything this constructor sets is the face settling onto the state, not a player's edit: with
    // undo on the panel it must not leave a step to press back through.
    const felitronics::appkit::CompareHistory::ScopedSuppress settling (amp.history);

    leverZero = snapLeverZero ((int) prefs::store().get (leverZeroKey, leverZeroDefault));
    channels.setTurboDb ((float) snapTurbo ((int) prefs::store().get (turboKey, turboDefault)));
    driveSteps = (bool) prefs::store().get (driveStepsKey, false);

    setUpChain (channels.adopt());
    channels.align();   // the packs and cabinets are files, and the folders may have changed since the state was saved
    settleOnSettings(); // ...and INPUT's lever may have another zero than the one the state was dialled against

    addAndMakeVisible (stage);
    stage.setBounds (0, 0, (int) width, (int) height);

    stage.addAndMakeVisible (space);
    space.setBounds (glass.toNearestInt());
    space.setWindow (windshield - glass.getPosition());
    stage.addChildComponent (still);
    still.setBounds (glass.toNearestInt());
    stage.addChildComponent (monitor);
    monitor.setBounds (windshield.toNearestInt());
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
        b.onHover = [this, at, &b] (bool over)     // a key that does not answer the hand says nothing either
        {
            nameTag.show (at.translated (0.0f, -40.0f), over && b.isEnabled() ? b.name + "  " + b.stateText() : juce::String());
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

    // TURBO pushes the capture: the preamp's input trim, at the push the settings give or at nothing.
    // The trim is a parameter and a host may leave it anywhere, so anything well above nothing reads as on.
    stage.addAndMakeVisible (turbo);
    turbo.setBounds (turboFrame.toNearestInt());
    {
        auto* push = follow (params::blockIn (params::preampId).toRawUTF8(),
                             [this] (float db) { turbo.setOn (db > Channels::turboOnAbove, false); });
        turbo.onChange = [this, push] (bool on) { push->setValueAsCompleteGesture (on ? channels.turboDb() : 0.0f); };
        push->sendInitialUpdate();
    }

    const juce::Colour lineFrom[] { orange.darker (0.3f), violet, orange.darker (0.3f) };
    const juce::Colour lineTo[]   { orange,               orange, orange };
    for (size_t i = 0; i < sliders.size(); ++i)
    {
        // CAB has a position for every cabinet the cockpit has; a selector needs two to be one.
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
    delayDiv = follow (params::delayDiv, [this] (float) { showDelayTime(); });
    delayBpm = follow (params::delayBpm, [this] (float) { showDelayTime(); });
    showDelayTime();

    // CAB steps through the cockpit's own cabinets, and its plate reads the one it sits on, the way
    // REVERB's reads its character. With fewer than two there is nothing to choose between, and it
    // stands dim.
    cabName.setInterceptsMouseClicks (false, false);      // a reading: the slider under it is the control
    stage.addAndMakeVisible (cabName);
    cabName.setBounds (plateName (2).toNearestInt());

    auto& cab = *sliders[2];
    if (channels.cabinets().size() >= 2)
        cab.onStep = [this] (int s) { chooseCab (s); showCab(); };
    else
    {
        cab.setEnabled (false);
        cab.setAlpha (0.35f);
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
    drive.onMenu = [this] { showDriveMenu(); };
    drive.detent = [this] (float travel) { return driveSteps ? (float) amp.preamp.stepAt ((double) travel) : travel; };
    bind (*sliders[0], params::reverbMix);
    bind (*sliders[1], params::delayMix);
    bind (outFader, params::outTrim);

    // INPUT's lever does not read the trim's own scale: its middle is the lever's zero (CatpitEditor.h).
    inputTrim = follow (params::inTrim, [this] (float db) { inFader.setValue (dbToLever (db, leverZero), false); });
    inFader.onGestureStart = [this] { inputTrim->beginGesture(); };
    inFader.onChange       = [this] (float travel) { inputTrim->setValueAsPartOfGesture (leverToDb (travel, leverZero)); };
    inFader.onGestureEnd   = [this] { inputTrim->endGesture(); };
    inFader.defaultValue   = 0.5f;
    inputTrim->sendInitialUpdate();

    sayLever (inFader,  "INPUT",  [this] { return getParam (params::inTrim) - (float) leverZero; });
    sayLever (outFader, "OUTPUT", [this] { return getParam (params::outTrim); });

    // The tone stack is the playing pack's own — its Bass, Middle and Treble as they were measured on
    // the amp, centre detent where every capture was taken.
    for (int t = 0; t < Channels::numTones; ++t)
        bind (*tones[(size_t) t], Channels::toneId (t));

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

    // CPU, on the plate under the grille. A click opens what the number is made of — the window
    // OrbitAmp's footer opens from its DSP figure.
    load.nameColour = labelLilac.withAlpha (0.85f);
    load.fromLeft = true;
    load.onClick = [this] { showLoad(); };
    stage.addAndMakeVisible (load);
    load.setBounds (loadPlate.toNearestInt());

    // MEM, on the mirror of CPU's plate. A reading and nothing else: a click there is a click on the
    // panel.
    memory.nameColour = memory.valueColour = labelLilac.withAlpha (0.85f);
    memory.fromLeft = true;
    memory.setInterceptsMouseClicks (false, false);
    stage.addChildComponent (memory);
    memory.setBounds (memoryPlate.toNearestInt());

    // PRESET, on the mirrored plate.
    presetDoor.valueColour = labelLilac.withAlpha (0.85f);
    presetDoor.set ({}, "PRESET");
    presetDoor.onClick = [this] { showPresetMenu(); };
    stage.addAndMakeVisible (presetDoor);
    presetDoor.setBounds (around (presetPlate, 140.0f, 30.0f));

    timerCallback();
    startTimerHz (4);   // the dots are a lookup and the load is a smoothed figure: nobody needs either sooner

    midiPulse.beat = [this] { playMidi(); };
    midiPulse.startTimerHz (100);

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

    // A right click on the bare panel is the door to the settings; the panel is the stage itself,
    // since the cockpit's picture over it lets every click through.
    stage.addMouseListener (this, false);

    // The keys. A click anywhere on the face brings the focus here, since nothing on it wants the
    // keyboard for itself; and the face takes it once when it first comes up, so the keys answer
    // before the first click too.
    setWantsKeyboardFocus (true);
    juce::Timer::callAfterDelay (300, [safe = juce::Component::SafePointer<CatpitEditor> (this)]
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

CatpitEditor::~CatpitEditor()
{
    amp.history.onHistoryChanged = nullptr;   // the history outlives this window
    endTapping();
    setFullScreen (false);                    // a window closed in full screen is remembered where it stood before
    gl.detach();
}

/** A STATE THE COCKPIT HAS NOT SEEN — a session saved before it existed, loaded while the window is
    open — is made its own the way the constructor makes the first one: the fixed chain, the channel
    whose pack is playing, a cabinet CAB can reach. And one it HAS seen, whose cabinet is no longer
    the file of that name — a register kept from before the file was replaced — has it read again;
    as one whose INPUT was dialled against another zero of the lever has it settled on this one, and
    one kept under another TURBO or another way of DRIVE's is given the settings' own. */
void CatpitEditor::adoptState()
{
    adoptionPending = false;
    const bool unseen = ! channels.adopted();
    if (! unseen && ! channels.cabinetIsStale() && ! settingsDiffer())
        return;

    const felitronics::appkit::CompareHistory::ScopedSuppress settling (amp.history);
    if (unseen)
        setUpChain (channels.adopt());
    channels.align();
    settleOnSettings();
    showChannel();
}

bool CatpitEditor::settingsDiffer() const
{
    const auto dialled = amp.apvts.state.getProperty (Channels::leverZeroId);
    const float push = getParam (params::blockIn (params::preampId).toRawUTF8());

    return dialled.isVoid() || (int) dialled != leverZero
        || (push > Channels::turboOnAbove && std::abs (push - channels.turboDb()) > 0.05f)
        || (getParam (params::blockSmooth (params::preampId).toRawUTF8()) > 0.5f) == driveSteps;
}

void CatpitEditor::settleOnSettings()
{
    // No note: the trim was dialled on the engine's own scale, whose middle is 0 dB.
    const auto dialled = amp.apvts.state.getProperty (Channels::leverZeroId);
    const int was = dialled.isVoid() ? 0 : (int) dialled;

    if (was != leverZero)
        setParam (params::inTrim, leverToDb (dbToLever (getParam (params::inTrim), was), leverZero));
    if (dialled.isVoid() || was != leverZero)
        amp.apvts.state.setProperty (Channels::leverZeroId, leverZero, nullptr);

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
void CatpitEditor::chooseLeverZero (int db)
{
    leverZero = snapLeverZero (db);
    prefs::store().set (leverZeroKey, leverZero);

    const felitronics::appkit::CompareHistory::ScopedSuppress settling (amp.history);
    settleOnSettings();
}

void CatpitEditor::sayLever (ColumnFader& fader, const juce::String& name, std::function<float()> reading)
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

void CatpitEditor::chooseTurbo (int db)
{
    db = snapTurbo (db);
    prefs::store().set (turboKey, db);
    channels.setTurboDb ((float) db);

    const felitronics::appkit::CompareHistory::ScopedSuppress settling (amp.history);
    settleOnSettings();
}

void CatpitEditor::chooseDriveSteps (bool steps)
{
    driveSteps = steps;
    prefs::store().set (driveStepsKey, steps);

    const felitronics::appkit::CompareHistory::ScopedSuppress settling (amp.history);
    settleOnSettings();
    drive.settle();
}

/** IN STEPS, DRIVE STANDS ON ONE CAPTURE OF THE PACK, and a right click on it offers that capture as a
    `.nam` — the model alone, as it was shot, for whatever else plays one. Turning smoothly it stands
    between two, and there is no one file to give: no menu either. */
void CatpitEditor::showDriveMenu()
{
    if (! driveSteps)
        return;

    endTapping();

    juce::PopupMenu::Item item ("Save the current sound as .nam...");
    item.itemID = 1;
    item.colour = lilac;
    item.action = [safe = juce::Component::SafePointer<CatpitEditor> (this)]
    {
        if (safe != nullptr)     // the action is posted: the window may be gone by the time it runs
            safe->saveCapture();
    };

    juce::PopupMenu menu;
    menu.addItem (std::move (item));
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

void CatpitEditor::saveCapture()
{
    auto capture = amp.preamp.captureOnDial();
    if (capture.nam.isEmpty())
        return;

    auto folder = juce::File (prefs::store().get (captureFolderKey, juce::String()).toString());
    if (! folder.isDirectory())
        folder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

    captureChooser = std::make_unique<juce::FileChooser> ("Save the capture", folder.getChildFile (capture.name + ".nam"), "*.nam");
    captureChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                     | juce::FileBrowserComponent::warnAboutOverwriting,
                                 [nam = std::move (capture.nam)] (const juce::FileChooser& picked)
                                 {
                                     const auto file = picked.getResult();
                                     if (file == juce::File())
                                         return;

                                     prefs::store().set (captureFolderKey, file.getParentDirectory().getFullPathName());
                                     if (! file.replaceWithData (nam.getData(), nam.getSize()))
                                         juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                                                           .withIconType (juce::MessageBoxIconType::WarningIcon)
                                                                           .withTitle ("Not saved")
                                                                           .withMessage (file.getFullPathName())
                                                                           .withButton ("OK"),
                                                                       nullptr);
                                 });
}

void CatpitEditor::timerCallback()
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

/** A Program Change from a MIDI input: the throttle goes to that channel, as it does for 1 2 3 4 on the
    keyboard. Programs past the fourth are not the cockpit's. */
void CatpitEditor::playMidi()
{
    const int program = amp.takeProgramChange();
    if (program >= 0 && program < Lever::numDetents && ! lever.isHeld())
        lever.setDetent (program, true);

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

    if (turboTo >= 0)   setParam (params::blockIn (params::preampId).toRawUTF8(), thrown (turboTo) ? channels.turboDb() : 0.0f);
    if (hpfTo >= 0)     along (params::cabHpfOn, thrown (hpfTo) ? 1.0f : 0.0f);
    if (lpfTo >= 0)     along (params::cabLpfOn, thrown (lpfTo) ? 1.0f : 0.0f);
    if (driveTo >= 0)   along (params::preampGain, (float) driveTo / 127.0f);

    // A tone knob's middle is where the pack was captured, and 0..127 has no middle: 64 is given it, and
    // each half of the controller's travel is spread over its own half of the knob's.
    for (int t = 0; t < Channels::numTones; ++t)
        if (const int v = toneTo[(size_t) t]; v >= 0)
            along (Channels::toneId (t), v <= 64 ? 0.5f * (float) v / 64.0f : 0.5f + 0.5f * (float) (v - 64) / 63.0f);
}

/** THE LOAD, ITEMISED — OrbitAmp's window, in the cockpit's words and colours. The cockpit's packs are
    whole amps, with the boost and the power stage inside them, so the row OrbitAmp calls PREAMP is
    the AMP here, and its console the amp's. */
void CatpitEditor::showLoad()
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

/** THE MIDI WINDOW — which inputs the cockpit listens to, and the list of what it does with what it hears. */
void CatpitEditor::showMidi()
{
    showSheet (std::make_unique<MidiPanel>());
}

void CatpitEditor::showSheet (std::unique_ptr<juce::Component> content)
{
    sheet = std::make_unique<Sheet> (std::move (content));
    // Closing is deferred: it is asked for from inside the very component it destroys.
    sheet->onDismiss = [safe = juce::Component::SafePointer<CatpitEditor> (this)]
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

void CatpitEditor::refreshHistory()
{
    const auto& history = amp.history;
    cockpit.setHistory (history.canUndo(), history.canRedo(), history.active());
    for (int r = 0; r < numRegisterKeys; ++r)
        overhead[(size_t) (ovFirstRegister + r)]->setOn (r == history.active(), false);

    showChannel();   // an undo, a redo or another register may have brought a different channel with it

    // ...or a state with no channels in it at all, or one whose cabinet is not the file of that name
    // any more, or whose INPUT was dialled against another zero. Settling any of them writes, and this
    // is the history's own callback, so it is done on the way out.
    if ((! channels.adopted() || channels.cabinetIsStale() || settingsDiffer()) && ! adoptionPending)
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

    // A layer the packs do not carry yet never shows as thrown — whatever the state says.
    const auto playable = [] (Channels::Flag f) { return Channels::layerPlayable[(size_t) f]; };
    const auto thrown = [this, playable] (Channels::Flag f) { return playable (f) && channels.flag (f); };
    buttons[btFat]->setEnabled (playable (Channels::fat) && channel >= 2);
    buttons[btFat]->setOn      (thrown (Channels::fat),      false);
    buttons[btBright]->setOn   (thrown (Channels::bright),   false);
    buttons[btDepth]->setOn    (thrown (Channels::depth),    false);
    buttons[btPresence]->setOn (thrown (Channels::presence), false);

    still.setChannel (channel);

    std::array<bool, 6> live {};
    for (size_t i = 0; i < live.size(); ++i)
        live[i] = buttons[i]->isEnabled();
    cockpit.setKeysLive (live);

    // CAB's cap, on the cabinet that is playing. (This is first called while the face is still being
    // built, before the plates exist.)
    if (const int playing = channels.cabinet(); playing >= 0 && sliders[2] != nullptr)
        sliders[2]->setStep (playing, false);
    showCab();
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

    // Full screen, where the cockpit stands alone: Cmd+Ctrl+F as a Mac has it, F11 as the others do.
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

float CatpitEditor::stageScale() const
{
    // In a window of the cockpit's shape the width decides, as it always has: the height is that shape
    // rounded to a pixel, and fitting by it would leave a hair of window at the sides.
    const float byWidth = (float) getWidth() / layout::width;
    return layout::height * byWidth <= (float) getHeight() + 1.0f ? byWidth
                                                                  : (float) getHeight() / layout::height;
}

juce::Point<float> CatpitEditor::stageOrigin() const
{
    // On whole pixels: the cockpit's cached picture is copied to the screen, not resampled.
    const float s = stageScale();
    return { std::round (juce::jmax (0.0f, ((float) getWidth()  - layout::width  * s) * 0.5f)),
             std::round (juce::jmax (0.0f, ((float) getHeight() - layout::height * s) * 0.5f)) };
}

void CatpitEditor::paint (juce::Graphics& g)
{
    const auto cockpitArea = juce::Rectangle<float> (layout::width, layout::height) * stageScale() + stageOrigin();
    if (cockpitArea.getWidth() + 1.0f >= (float) getWidth() && cockpitArea.getHeight() + 1.0f >= (float) getHeight())
        return;

    g.excludeClipRegion (cockpitArea.reduced (1.0f).toNearestInt());      // the sky is under there
    g.fillAll (juce::Colours::black);
}

void CatpitEditor::resized()
{
    stage.setTransform (juce::AffineTransform::scale (stageScale()).translated (stageOrigin()));
    placeSky();
    if (sheet != nullptr)
        sheet->setBounds (getLocalBounds());
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

/** FULL SCREEN, and back. The standalone's window follows the cockpit's size and the cockpit keeps its
    shape; in full screen it is the other way round — the window is the screen's, whatever its shape —
    so for as long as it lasts the cockpit lets go of its shape and its limits and takes what the
    window gives it, centring its stage (stageScale). JUCE's kiosk mode does the rest: it hides the
    title bar, the menu bar and the Dock, and puts the window back where it was on the way out. */
void CatpitEditor::setFullScreen (bool on)
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
        getConstrainer()->setSizeLimits (layout::minWindowWidth, juce::roundToInt ((float) layout::minWindowWidth * layout::height / layout::width),
                                         0x3fffffff, 0x3fffffff);
        desktop.setKioskModeComponent (window, false);
    }
    else
    {
        getConstrainer()->setFixedAspectRatio ((double) layout::width / (double) layout::height);
        getConstrainer()->setSizeLimits (layout::minWindowWidth, juce::roundToInt ((float) layout::minWindowWidth * layout::height / layout::width),
                                         layout::readingWidth * 2, juce::roundToInt ((float) layout::readingWidth * 2.0f * layout::height / layout::width));
        desktop.setKioskModeComponent (nullptr);
        setBoundsConstrained (getBounds());
        standalone::rememberWindowPosition (windowedAt);
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
        gl.setRenderer (glSky.get());
        gl.setComponentPaintingEnabled (true);
        gl.setContinuousRepainting (true);

        // JUCE keeps the textures it makes of images in a cache whose limit — 8 * 1024 * 1024, and
        // called bytes — it compares with PIXEL counts. The cockpit's own layer is the whole window at
        // the screen's pixel size: 2.4 million pixels at the smallest window and 9.5 million at the
        // largest on a 2x screen, so from about 1740 wide the layer and the art no longer fitted, and
        // every paint threw textures out and loaded them again. The limit is a ceiling, not an
        // allocation: only what is drawn is held, and a replaced layer leaves the cache with its image.
        gl.setImageCacheSize (glTexturePixels);
        gl.attachTo (*this);

        // A shader that will not build on this machine leaves the window dark; look once, and fall back.
        juce::Timer::callAfterDelay (800, [safe = juce::Component::SafePointer<CatpitEditor> (this)]
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

void CatpitEditor::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        showSettingsMenu();
}

void CatpitEditor::playDemo (int loop)
{
    const bool start = loop >= 0 || ! amp.demo.isPlaying();
    if (loop >= 0 || (start && amp.demoLoop() < 0))
        amp.selectDemoLoop (juce::jmax (0, loop));      // none picked yet: the first
    amp.demo.setPlaying (start);
}

/** THE SETTINGS — what is set once for the machine and the room. The gate and the stereo mode are the
    engine's own and are part of the sound; how the stars fly is this machine's and is not. */
void CatpitEditor::showSettingsMenu()
{
    enum : int { itemGateOn = 1, itemMidi, itemFullScreen, itemDemo, firstStars = 5, firstStereo = 10, firstDecay = 20, firstPos = 30, firstWindow = 40, firstThreshold = 100, firstDemo = 200, firstLeverZero = 300, firstTurbo = 320, firstDrive = 340 };

    const auto itemFor = [] (const juce::String& text, int id, bool ticked, bool enabled = true)
    {
        juce::PopupMenu::Item item (text);
        item.itemID    = id;
        item.colour    = lilac;
        item.isTicked  = ticked;
        item.isEnabled = enabled;
        return item;
    };
    const auto choice = [this] (const char* id) { return juce::roundToInt (getParam (id)); };

    juce::PopupMenu menu;

    // What the windshield shows over the sky: nothing, or one of the pictures of the amp at work.
    menu.addSectionHeader ("WINDOW");
    for (int i = 0; i < Monitor::names().size(); ++i)
        menu.addItem (itemFor (Monitor::names()[i], firstWindow + i, (int) monitor.shown() == i));
    if (! amp.runsInHost())
        menu.addItem (itemFor ("Full screen", itemFullScreen, fullScreen));

    // The stars outside it: flying on the GPU, flying on the CPU, or a still picture and nothing running.
    // Where the GPU's sky cannot be had the CPU's flies instead, and the tick says so.
    menu.addSectionHeader ("STARS");
    const auto flying = stars == Stars::gpu && gpuBroken ? Stars::cpu : stars;
    menu.addItem (itemFor (gpuBroken ? juce::String::fromUTF8 ("OpenGL \xe2\x80\x94 not available here") : juce::String ("OpenGL"),
                           firstStars + (int) Stars::gpu, flying == Stars::gpu, ! gpuBroken));
    menu.addItem (itemFor ("CPU", firstStars + (int) Stars::cpu, flying == Stars::cpu));
    menu.addItem (itemFor ("Off", firstStars + (int) Stars::off, flying == Stars::off));

    // What the INPUT lever gives in the middle of its travel, how hard TURBO pushes, and how DRIVE
    // goes between the pack's captures.
    menu.addSectionHeader ("INPUT");
    juce::PopupMenu zero;
    for (int db = leverZeroHighest; db >= leverZeroLowest; db -= leverZeroStep)
        zero.addItem (itemFor (decibels (db), firstLeverZero + (db - leverZeroLowest) / leverZeroStep, db == leverZero));
    menu.addSubMenu ("Lever's zero  " + decibels (leverZero), zero);

    juce::PopupMenu push;
    const int turboNow = juce::roundToInt (channels.turboDb());
    for (size_t i = 0; i < turboPushes.size(); ++i)
        push.addItem (itemFor (decibels (turboPushes[i]), firstTurbo + (int) i, turboPushes[i] == turboNow));
    menu.addSubMenu ("TURBO adds  " + decibels (turboNow), push);

    const juce::StringArray ways { "Smoothly", "In steps" };
    juce::PopupMenu way;
    for (int i = 0; i < ways.size(); ++i)
        way.addItem (itemFor (ways[i], firstDrive + i, (i == 1) == driveSteps));
    menu.addSubMenu ("DRIVE turns  " + ways[driveSteps ? 1 : 0], way);

    menu.addSectionHeader ("OUTPUT");
    for (int i = 0; i < params::stereoModes.size(); ++i)
        menu.addItem (itemFor (params::stereoModes[i], firstStereo + i, choice (params::stereoMode) == i));

    // The gate: whether it stands in the chain, where it closes, how fast, and where it mutes.
    menu.addSectionHeader ("GATE");
    const bool gateIsOn = getParam (params::gateOn) > 0.5f;
    menu.addItem (itemFor ("On", itemGateOn, gateIsOn));

    juce::PopupMenu threshold;
    const int now = juce::roundToInt (getParam (params::gateThreshold));
    for (int db = -80; db <= -10; db += 5)
        threshold.addItem (itemFor (juce::String (db) + " dB", firstThreshold + (db + 80), std::abs (now - db) <= 2));
    menu.addSubMenu ("Threshold  " + juce::String (now) + " dB", threshold, gateIsOn);

    juce::PopupMenu decay;
    for (int i = 0; i < params::gateDecayModes.size(); ++i)
        decay.addItem (itemFor (params::gateDecayModes[i], firstDecay + i, choice (params::gateDecay) == i));
    menu.addSubMenu ("Closes  " + params::gateDecayModes[choice (params::gateDecay)], decay, gateIsOn);

    juce::PopupMenu position;
    for (int i = 0; i < params::gatePositions.size(); ++i)
        position.addItem (itemFor (params::gatePositions[i], firstPos + i, choice (params::gatePos) == i));
    menu.addSubMenu ("Mutes at  " + params::gatePositions[choice (params::gatePos)], position, gateIsOn);

    // The MIDI inputs are the standalone's; a host routes MIDI itself.
    if (MidiPanel::available())
    {
        menu.addSeparator();
        menu.addItem (itemFor ("MIDI...", itemMidi, false));
    }

    // A loop through the amp in place of the guitar, where the loops are — see playDemo.
    if (params::demoLoopsPresent())
    {
        menu.addSectionHeader ("DEMO");
        menu.addItem (itemFor ("Play", itemDemo, amp.demo.isPlaying()));
        for (int i = 0; i < params::demoLoops.size(); ++i)
            menu.addItem (itemFor (params::demoLoops[i], firstDemo + i, i == juce::jmax (0, amp.demoLoop())));
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
                        [safe = juce::Component::SafePointer<CatpitEditor> (this), gateIsOn] (int picked)
                        {
                            if (safe == nullptr || picked <= 0)
                                return;
                            auto& e = *safe;
                            e.endTapping();

                            if (picked >= firstDrive)                  e.chooseDriveSteps (picked > firstDrive);
                            else if (picked >= firstTurbo)             e.chooseTurbo (turboPushes[(size_t) juce::jlimit (0, (int) turboPushes.size() - 1, picked - firstTurbo)]);
                            else if (picked >= firstLeverZero)         e.chooseLeverZero (leverZeroLowest + (picked - firstLeverZero) * leverZeroStep);
                            else if (picked >= firstDemo)              e.playDemo (picked - firstDemo);
                            else if (picked == itemDemo)               e.playDemo (-1);
                            else if (picked == itemGateOn)             e.setParam (params::gateOn, gateIsOn ? 0.0f : 1.0f);
                            else if (picked == itemMidi)               e.showMidi();
                            else if (picked == itemFullScreen)         e.setFullScreen (! e.fullScreen);
                            else if (picked < firstStereo)             e.chooseStars ((Stars) (picked - firstStars));
                            else if (picked >= firstThreshold)         e.setParam (params::gateThreshold, (float) (picked - firstThreshold - 80));
                            else if (picked >= firstWindow)            e.showInWindow ((Monitor::View) (picked - firstWindow));
                            else if (picked >= firstPos)               e.setParam (params::gatePos, (float) (picked - firstPos));
                            else if (picked >= firstDecay)             e.setParam (params::gateDecay, (float) (picked - firstDecay));
                            else if (picked >= firstStereo)            e.setParam (params::stereoMode, (float) (picked - firstStereo));
                        });
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
    setParam (params::gatePresent,   1.0f);   // the gate stands in the chain; whether it works is the settings' ON
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

    // TURBO is the preamp's input trim, at its push or at nothing: a state the cockpit has never seen
    // starts with it off, wherever that trim was left.
    if (firstTime)
        setParam (params::blockIn (params::preampId).toRawUTF8(), 0.0f);

    // BASS / MIDDLE / TREBLE are the pack's own tone stack. The universal console's three bands they
    // used to ride stand flat behind it: a pack with no tone of its own falls to that console, and
    // must not find a setting there that no knob on the face shows.
    setParam (params::blockEqMode (params::preampId).toRawUTF8(), (float) params::eqModes.indexOf ("Device Tone"));
    setParam (params::eqLoDb (preampConsole).toRawUTF8(), 0.0f);
    setParam (params::eqBellDb (preampConsole, 0).toRawUTF8(), 0.0f);
    setParam (params::eqHiDb (preampConsole).toRawUTF8(), 0.0f);

    // HPF / LPF are on-off: the corners and slopes stay where OrbitCab puts them until set by ear.
    setParam (params::cabHpfHz, params::cabHpfDefaultHz);
    setParam (params::cabLpfHz, params::cabLpfDefaultHz);
}

void CatpitEditor::chooseCab (int step)
{
    channels.chooseCabinet (step);
}

/** CAB's plate says which cabinet is playing: the name its file carries, without the number that
    orders it. */
void CatpitEditor::showCab()
{
    const auto& cabinets = channels.cabinets();
    const int step = sliders[2] != nullptr ? sliders[2]->getStep() : -1;
    cabName.set ("CAB", step >= 0 && step < cabinets.size() ? Channels::cabinetName (cabinets.getReference (step))
                                                               : juce::String());
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
