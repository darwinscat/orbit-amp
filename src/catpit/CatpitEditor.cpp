// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "CatpitEditor.h"
#include "CatpitLayout.h"

#include "../Parameters.h"

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
    // The overhead buttons. The panel has room for a short name over each hole; the full one shows on
    // hover. FAT is a layer only the two hottest packs carry.
    struct Switch { const char* name; const char* shortName; };
    constexpr std::array<Switch, 6> switches {{
        { "FAT",      "FAT"    },
        { "BRIGHT",   "BRIGHT" },
        { "DEPTH",    "DEPTH"  },
        { "PRESENCE", "PRES"   },
        { "HIGHPASS", "HPF"    },
        { "LOWPASS",  "LPF"    },
    }};

    /** STAND-INS until the cockpit's own captures exist: four preamp packs from the library, in order
        of gain, matched by their file name without the extension (exactly — "V4 Copper" is not
        "V4 Copper TB"). */
    constexpr std::array<const char*, 4> leverPacks { "IR-X Ch1", "V4 Copper", "V4 Sheriff Ch1", "V4 Kraken Ch2" };

    /** ...and six factory cabinets for COMBO to step through, the shipping default first. */
    constexpr std::array<int, 6> comboIrs { params::cabIrDefault, 0, 2, 3, 10, 11 };

    // Measured off the original renders (the ones that still had their pointers): where the lit slot
    // starts and ends along the radius, and how wide it is, as fractions of the knob's opaque radius.
    constexpr ArtKnob::Pointer drivePointer { 0.32f, 0.76f, 0.068f };
    constexpr ArtKnob::Pointer tonePointer  { 0.335f, 0.70f, 0.076f };

    const juce::Colour labelLilac { 0xffc9b8f5 };

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
    auto* mark = felitronics::appkit::brand::catMark();
    if (mark == nullptr)
        return;

    const int mw = juce::roundToInt (emblemMark.getWidth() * scale), mh = juce::roundToInt (emblemMark.getHeight() * scale);
    juce::Image lit (juce::Image::ARGB, mw, mh, true);
    {
        juce::Graphics mg (lit);
        mg.fillAll (juce::Colours::black);
        mg.addTransform (juce::AffineTransform::scale (scale));
        mark->drawWithin (mg, emblemMark.withZeroOrigin(), juce::RectanglePlacement::centred, 1.0f);
    }
    juce::Image cut (juce::Image::SingleChannel, mw, mh, true);
    {
        const juce::Image::BitmapData from (lit, juce::Image::BitmapData::readOnly);
        juce::Image::BitmapData to (cut, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < mh; ++y)
            for (int x = 0; x < mw; ++x)
                to.setPixelColour (x, y, juce::Colours::white.withAlpha (from.getPixelColour (x, y).getBrightness()));
    }

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

    // The overhead names, one row over their holes, set tight: the right four holes sit barely a name
    // apart, so DEPTH and PRES lean away from each other into the room either side of them.
    constexpr std::array<float, 6> lean { 0.0f, 0.0f, -8.0f, 8.0f, 0.0f, 0.0f };
    for (size_t i = 0; i < switches.size(); ++i)
        drawLabel (g, switches[i].shortName, juce::Rectangle<float> (110.0f, 22.0f).withCentre ({ holes[i].x + lean[i], 29.0f }),
                   px13, labelLilac, 0.0f);

    drawLabel (g, "DRIVE", juce::Rectangle<float> (220.0f, 30.0f).withCentre ({ drive.centre.x, 394.0f }), px15, labelLilac);
    const char* toneNames[] { "BASS", "MIDDLE", "TREBLE" };
    for (size_t i = 0; i < tones.size(); ++i)
        drawLabel (g, toneNames[i], juce::Rectangle<float> (150.0f, 28.0f).withCentre ({ tones[i].centre.x, 420.0f }), px13, labelLilac);

    drawLabel (g, "TURBO", juce::Rectangle<float> (160.0f, 26.0f).withCentre ({ turboFrame.getCentreX(), 400.0f }), px13, orange);

    const char* plateNames[] { "REVERB", "DELAY AMOUNT", "COMBO" };
    const juce::Colour plateColours[] { labelLilac, orange, labelLilac };
    for (size_t i = 0; i < sliderInsets.size(); ++i)
        drawLabel (g, plateNames[i], juce::Rectangle<float> (sliderInsets[i].getWidth(), 30.0f)
                                         .withCentre ({ sliderInsets[i].getCentreX(), 640.0f }), px13, plateColours[i]);

    drawLabel (g, "INPUT",  juce::Rectangle<float> (170.0f, 26.0f).withCentre ({ 117.0f, 314.0f }),  px13, labelLilac);
    drawLabel (g, "OUTPUT", juce::Rectangle<float> (170.0f, 26.0f).withCentre ({ 1557.0f, 314.0f }), px13, labelLilac);

    // The tuner: its name, and a dial that waits for a note (the ear is wired in a later step).
    drawLabel (g, "TUNER", juce::Rectangle<float> (200.0f, 26.0f).withCentre ({ tunerScreen.getCentreX(), 781.0f }), px13, labelLilac);
    {
        const auto s = tunerScreen.reduced (24.0f, 18.0f);
        for (int c = -50; c <= 50; c += 5)
        {
            const float x = s.getCentreX() + s.getWidth() * 0.5f * (float) c / 50.0f;
            const bool major = c % 25 == 0;
            g.setColour ((c == 0 ? orange : violet).withAlpha (major ? 0.95f : 0.45f));
            g.fillRect (juce::Rectangle<float> (x - 1.0f, s.getY() + (major ? 4.0f : 14.0f), 2.0f, major ? 34.0f : 22.0f));
        }
        const char* marks[] { "-50", "-25", "0", "+25", "+50" };
        for (int k = 0; k < 5; ++k)
        {
            const float x = s.getCentreX() + s.getWidth() * 0.5f * (float) (k * 25 - 50) / 50.0f;
            drawLabel (g, marks[k], juce::Rectangle<float> (60.0f, 24.0f).withCentre ({ x, s.getBottom() - 12.0f }),
                       px13, lilac.withAlpha (0.7f), 0.0f);
        }
    }
}

//==============================================================================
CatpitEditor::CatpitEditor (AmpProcessor& p)
    : juce::AudioProcessorEditor (&p), amp (p),
      drive (Art::get().knobDrive, drivePointer),
      inMeter ([this] { return amp.gateKeyDb.load (std::memory_order_relaxed); }),
      outMeter ([this] { return amp.outDb.load (std::memory_order_relaxed); })
{
    using namespace layout;

    setUpChain();

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

    for (size_t i = 0; i < buttons.size(); ++i)
    {
        buttons[i] = std::make_unique<PushButton> (switches[i].name);
        auto& b = *buttons[i];
        stage.addAndMakeVisible (b);
        b.setBounds (around (holes[i], 40.0f, 40.0f));
        b.onHover = [this, i, &b] (bool over)
        {
            cockpit.showTag ({ holes[i].x, 116.0f }, over ? b.name + "  " + b.stateText() : juce::String());
        };
    }

    // The throttle picks the pack; where it starts is wherever the pack already playing sits, if it is one of
    // the four.
    stage.addAndMakeVisible (lever);
    lever.setBounds (leverFrame.toNearestInt());
    lever.onTravel = [this] (float pos) { space.setThrottle (pos / (float) (Lever::numDetents - 1)); };
    lever.onDetent = [this] (int d)
    {
        buttons[0]->setEnabled (d >= 2);
        choosePack (d);
    };
    int startDetent = -1;
    for (int d = 0; d < Lever::numDetents; ++d)
        if (packIndexFor (d) >= 0 && packIndexFor (d) == juce::roundToInt (getParam (params::preampDevice)))
            startDetent = d;
    if (startDetent < 0)
    {
        startDetent = 0;
        choosePack (0);
    }
    // A prototype's hook for looking at the window at speed without a hand on the lever.
    if (const auto d = juce::SystemStats::getEnvironmentVariable ("CATPIT_DETENT", {}); d.isNotEmpty())
        startDetent = d.getIntValue();
    lever.setDetent (startDetent, false);
    buttons[0]->setEnabled (lever.getDetent() >= 2);

    // TURBO moves but drives nothing: it will be a layer inside the pack.
    stage.addAndMakeVisible (turbo);
    turbo.setBounds (turboFrame.toNearestInt());

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

    for (auto* c : std::initializer_list<juce::Component*> { &inFader, &outFader, &inMeter, &outMeter })
        stage.addAndMakeVisible (c);
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

    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) width / (double) height);
    setResizeLimits (minWindowWidth, juce::roundToInt ((float) minWindowWidth * height / width),
                     minWindowWidth * 2, juce::roundToInt ((float) minWindowWidth * 2.0f * height / width));
    setSize (minWindowWidth, juce::roundToInt ((float) minWindowWidth * height / width));
}

CatpitEditor::~CatpitEditor() = default;

void CatpitEditor::resized()
{
    stage.setTransform (juce::AffineTransform::scale ((float) getWidth() / layout::width));
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
}

int CatpitEditor::packIndexFor (int detent) const
{
    const auto& packs = amp.preamp.packs;
    for (int i = 0; i < packs.size(); ++i)
        if (packs.getReference (i).location.getFileName().upToFirstOccurrenceOf (".", false, false)
                .equalsIgnoreCase (leverPacks[(size_t) detent]))
            return i;
    return -1;
}

void CatpitEditor::choosePack (int detent)
{
    if (const int i = packIndexFor (detent); i >= 0)
        setParam (params::preampDevice, (float) i);
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
void CatpitEditor::bind (Control& control, const char* paramId)
{
    auto* p = amp.apvts.getParameter (paramId);
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

} // namespace catpit
} // namespace orbitamp
