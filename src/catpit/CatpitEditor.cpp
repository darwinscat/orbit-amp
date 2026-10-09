// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "CatpitEditor.h"
#include "CatpitLayout.h"

#include "../Parameters.h"
#include "../ui/AboutConfig.h"

#include <felitronics/appkit/BrandAssets.h>

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

    /** STAND-INS until the cockpit's own cabinets exist: six factory IRs for COMBO to step through, the
        shipping default first. */
    constexpr std::array<int, 6> comboIrs { params::cabIrDefault, 0, 2, 3, 10, 11 };

    // Measured off the original renders (the ones that still had their pointers): where the lit slot
    // starts and ends along the radius, and how wide it is, as fractions of the knob's opaque radius.
    constexpr ArtKnob::Pointer drivePointer { 0.32f, 0.76f, 0.068f };
    constexpr ArtKnob::Pointer tonePointer  { 0.335f, 0.70f, 0.076f };

    const juce::Colour labelLilac { 0xffc9b8f5 };

    /** The cockpit's own tag on a family link, for the site's analytics. */
    constexpr const char* campaign = "utm_source=catpitamp&utm_medium=plugin";

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
    with the register that is playing. */
void Cockpit::paintOverhead (juce::Graphics& g) const
{
    using namespace layout;
    drawTurnArrow (g, { holes[ovUndo].x, 29.0f }, 8.0f, true,  labelLilac.withAlpha (canUndo ? 1.0f : 0.3f));
    drawTurnArrow (g, { holes[ovRedo].x, 29.0f }, 8.0f, false, labelLilac.withAlpha (canRedo ? 1.0f : 0.3f));

    // Each register's letter stands to the left of its key; the render's own little lamp over the hole
    // is lit for the one that is playing.
    for (int i = 0; i < numRegisterKeys; ++i)
    {
        const auto hole = holes[(size_t) (ovFirstRegister + i)];
        const bool here = i == activeRegister;
        drawLabel (g, juce::String::charToString ((juce::juce_wchar) ('A' + i)),
                   juce::Rectangle<float> (24.0f, 26.0f).withCentre ({ hole.x - 29.0f, hole.y }),
                   px13, here ? orange : labelLilac.withAlpha (0.8f), 0.0f);
        if (here)
        {
            juce::Path lamp;
            lamp.addRoundedRectangle (juce::Rectangle<float> (19.0f, 4.0f).withCentre ({ hole.x, 28.0f }), 2.0f);
            juce::DropShadow (orange, 6, {}).drawForPath (g, lamp);
            g.setColour (orange);
            g.fillPath (lamp);
        }
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

    const char* plateNames[] { "REVERB", "DELAY AMOUNT", "COMBO" };
    const juce::Colour plateColours[] { labelLilac, orange, labelLilac };
    for (size_t i = 0; i < sliderInsets.size(); ++i)
        drawLabel (g, plateNames[i], juce::Rectangle<float> (sliderInsets[i].getWidth(), 30.0f)
                                         .withCentre ({ sliderInsets[i].getCentreX(), 640.0f }), px13, plateColours[i]);

    drawLabel (g, "INPUT",  juce::Rectangle<float> (170.0f, 26.0f).withCentre ({ 117.0f, 314.0f }),  px13, labelLilac);
    drawLabel (g, "OUTPUT", juce::Rectangle<float> (170.0f, 26.0f).withCentre ({ 1557.0f, 314.0f }), px13, labelLilac);

    // The tuner's name; its screen is the live TunerFace.
    drawLabel (g, "TUNER", juce::Rectangle<float> (200.0f, 26.0f).withCentre ({ tunerScreen.getCentreX(), 781.0f }), px13, labelLilac);
}

//==============================================================================
CatpitEditor::CatpitEditor (AmpProcessor& p)
    : juce::AudioProcessorEditor (&p), amp (p), channels (p),
      drive (Art::get().knobDrive, drivePointer),
      inMeter ([this] { return amp.gateKeyDb.load (std::memory_order_relaxed); }),
      outMeter ([this] { return amp.outDb.load (std::memory_order_relaxed); }),
      tuner (p.tunerEar),
      version (layout::px13),
      about (p.updateChecker(), aboutConfig ("CatpitAmp", "https://darwinscat.com", campaign), p.pluginFormat())
{
    using namespace layout;

    // Everything this constructor sets is the face settling onto the state, not a player's edit: with
    // undo on the panel it must not leave a step to press back through.
    const felitronics::appkit::CompareHistory::ScopedSuppress settling (amp.history);

    setUpChain();
    channels.adopt();

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
    overhead[ovUndo]->onPress = [this] { amp.history.undo(); };
    overhead[ovRedo]->onPress = [this] { amp.history.redo(); };

    // A B C D, in the four holes right of it: a press plays that register, a right click copies it.
    for (int r = 0; r < numRegisterKeys; ++r)
    {
        const auto i = (size_t) (ovFirstRegister + r);
        overhead[i] = std::make_unique<PushButton> (juce::String::charToString ((juce::juce_wchar) ('A' + r)));
        auto& key = *overhead[i];
        key.latching = false;
        key.onPress  = [this, r] { amp.history.switchTo (r); };
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
        channels.select (d);
        showChannel();
    };
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
        sliders[i] = std::make_unique<PlateSlider> (lineFrom[i], lineTo[i], i == 2 ? (int) comboIrs.size() : 0);
        stage.addAndMakeVisible (*sliders[i]);
        sliders[i]->setBounds (sliderInsets[i].toNearestInt());
    }

    // COMBO steps through six cabinets; the one it sits on shows by name while the hand is on it.
    auto& combo = *sliders[2];
    combo.onStep  = [this] (int s) { chooseCombo (s); showComboTag (true); };
    combo.onHover = [this] (bool over) { showComboTag (over); };
    {
        const int playing = juce::roundToInt (getParam (params::cabIr));
        const auto it = std::find (comboIrs.begin(), comboIrs.end(), playing);
        if (it != comboIrs.end())
            combo.setStep ((int) std::distance (comboIrs.begin(), it), false);
        else
            chooseCombo (0);
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

    // COMBO is set by hand, but a channel, an undo or a register moves the cabinet from under it — so
    // the cap follows the one that is playing.
    follow (params::cabIr, [this] (float v)
    {
        const auto it = std::find (comboIrs.begin(), comboIrs.end(), juce::roundToInt (v));
        if (it != comboIrs.end())
            sliders[2]->setStep ((int) std::distance (comboIrs.begin(), it), false);
    });

    showChannel();

    // The mark lights under the pointer and leads to the maker's site.
    stage.addAndMakeVisible (emblem);
    {
        juce::Path face;
        face.startNewSubPath (emblemFace[0]);
        for (size_t i = 1; i < emblemFace.size(); ++i)
            face.lineTo (emblemFace[i]);
        emblem.setBounds (face.getBounds().expanded (MarkCut::haloPad).toNearestInt());
    }
    emblem.onClick = [] { juce::URL (juce::String ("https://darwinscat.com?") + campaign).launchInDefaultBrowser(); };

    // The version, and behind it the window with everything else about the build.
    about.setBrandTypeface (theme::displayTypeface());
    addChildComponent (about);
    version.upper = false;
    version.valueColour = labelLilac.withAlpha (0.85f);
    version.set ({}, "v" + amp.updateChecker().currentVersion());
    version.onClick = [this] { showAbout(); };
    stage.addAndMakeVisible (version);
    version.setBounds (around (versionPlate, 124.0f, 30.0f));
    timerCallback();
    startTimerHz (4);   // the dot only ever moves after a deliberate check: a lookup, a few times a second

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

    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) width / (double) height);
    setResizeLimits (minWindowWidth, juce::roundToInt ((float) minWindowWidth * height / width),
                     minWindowWidth * 2, juce::roundToInt ((float) minWindowWidth * 2.0f * height / width));
    setSize (minWindowWidth, juce::roundToInt ((float) minWindowWidth * height / width));
}

CatpitEditor::~CatpitEditor()
{
    amp.history.onHistoryChanged = nullptr;   // the history outlives this window
    gl.detach();
}

void CatpitEditor::timerCallback()
{
    version.setDot (amp.updateChecker().updateAvailable());

    // The build window has closed: the sky goes back to the GPU.
    if (skyParked && aboutWindow == nullptr)
    {
        skyParked = false;
        if (glSky != nullptr)
        {
            space.setVisible (false);
            gl.attachTo (*this);
            placeSky();
        }
    }
}

/** THE BUILD WINDOW HANGS ON THE WINDOW, NOT ON THIS FACE — it is appkit's, the same one OrbitAmp opens —
    and the GL view lies over everything in the window that is not its own, so it would open underneath
    the cockpit. While it is up the sky flies on the CPU instead, and returns to the GPU when it closes. */
void CatpitEditor::showAbout()
{
    auto* top = getTopLevelComponent();
    const int before = top != nullptr ? top->getNumChildComponents() : 0;

    if (glSky != nullptr && ! skyParked)
    {
        gl.detach();
        space.setVisible (true);
        skyParked = true;
    }

    about.showAbout();

    // The window is the child it has just added; when that is gone, the timer brings the GPU sky back.
    aboutWindow = top != nullptr && top->getNumChildComponents() > before
                      ? top->getChildComponent (top->getNumChildComponents() - 1) : nullptr;
}

void CatpitEditor::refreshHistory()
{
    const auto& history = amp.history;
    cockpit.setHistory (history.canUndo(), history.canRedo(), history.active());
    for (int r = 0; r < numRegisterKeys; ++r)
        overhead[(size_t) (ovFirstRegister + r)]->setOn (r == history.active(), false);

    showChannel();   // an undo, a redo or another register may have brought a different channel with it
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
            item.action = [&history, index, i] { history.copyRegister (index, i); };
            menu.addItem (std::move (item));
        }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (overhead[(size_t) (ovFirstRegister + index)].get()));
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
void CatpitEditor::setUpChain()
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
    setParam (params::reverbPresent, 1.0f);
    setParam (params::reverbOn,      1.0f);
    setParam (params::reverbType,    (float) params::reverbCharacters.indexOf ("Hall"));
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
    amp.chooseCabFactory (comboIrs[(size_t) juce::jlimit (0, (int) comboIrs.size() - 1, step)]);
}

void CatpitEditor::showComboTag (bool show)
{
    const auto& inset = layout::sliderInsets[2];
    const auto name = params::cabIrNames[comboIrs[(size_t) sliders[2]->getStep()]];
    cockpit.showTag ({ inset.getCentreX(), inset.getY() - 52.0f }, show ? name : juce::String());
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

void CatpitEditor::follow (const char* paramId, std::function<void (float)> moved)
{
    if (auto* p = amp.apvts.getParameter (paramId))
        attachments.push_back (std::make_unique<juce::ParameterAttachment> (*p, std::move (moved)));
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
