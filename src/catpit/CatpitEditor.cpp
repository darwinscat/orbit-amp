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
    // The overhead panel has room for a short name over each hole; the full one shows on hover.
    struct Switch { const char* name; const char* shortName; int positions; };
    constexpr std::array<Switch, 6> switches {{
        { "FAT",      "FAT",    2 },
        { "BRIGHT",   "BRIGHT", 2 },
        { "DEPTH",    "DEPTH",  3 },
        { "PRESENCE", "PRES",   3 },
        { "HIGHPASS", "HPF",    3 },
        { "LOWPASS",  "LPF",    3 },
    }};

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

void Cockpit::showHover (int hole, juce::String text)
{
    hoverHole = hole;
    hoverText = std::move (text);
    repaint (juce::Rectangle<int> (0, 80, (int) layout::width, 70));
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
        paintStatic (lg);
    }
    g.drawImage (layer, juce::Rectangle<float> (0.0f, 0.0f, width, height));

    // The full name of the overhead toggle under the hand, on a dark tag below the panel.
    if (hoverHole >= 0 && hoverText.isNotEmpty())
    {
        const auto font = labelFont (px13).withExtraKerningFactor (0.06f);
        const float w = juce::GlyphArrangement::getStringWidth (font, hoverText) + 28.0f;
        const auto tag = juce::Rectangle<float> (w, 30.0f).withCentre ({ holes[(size_t) hoverHole].x, 112.0f });
        g.setColour (juce::Colour (0xe0050509));
        g.fillRoundedRectangle (tag, 6.0f);
        g.setColour (violet.withAlpha (0.7f));
        g.drawRoundedRectangle (tag, 6.0f, 1.2f);
        drawLabel (g, hoverText, tag, px13, juce::Colour (0xffece9f6), 0.06f);
    }
}

void Cockpit::paintEmblem (juce::Graphics& g) const
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

    // The family's mark, raised a hair off the plate: its own outline cast as a soft shadow, then the mark.
    if (auto* mark = felitronics::appkit::brand::catMark())
    {
        auto outline = mark->getOutlineAsPath();
        outline.applyTransform (juce::RectanglePlacement (juce::RectanglePlacement::centred)
                                    .getTransformToFit (mark->getDrawableBounds(), emblemMark));
        juce::DropShadow (juce::Colours::black.withAlpha (0.85f), 5, { 1, 3 }).drawForPath (g, outline);
        mark->drawWithin (g, emblemMark, juce::RectanglePlacement::centred, 1.0f);
    }
}

void Cockpit::paintStatic (juce::Graphics& g) const
{
    using namespace layout;

    const auto& bg = Art::get().bg;
    if (bg.isValid())
        g.drawImage (bg, juce::Rectangle<float> (0.0f, 0.0f, width, height));

    paintEmblem (g);

    // The overhead names, over their holes. The right four holes sit closer than a 13-px name is wide,
    // so their names alternate between two rows.
    for (size_t i = 0; i < switches.size(); ++i)
    {
        const float y = (i == 2 || i == 4) ? 17.0f : 35.0f;
        drawLabel (g, switches[i].shortName, juce::Rectangle<float> (110.0f, 20.0f).withCentre ({ holes[i].x, y }),
                   px13, labelLilac, 0.02f);
    }

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

    stage.addAndMakeVisible (lever);
    lever.setBounds (leverFrame.toNearestInt());
    lever.onTravel = [this] (float pos) { space.setThrottle (pos / (float) (Lever::numDetents - 1)); };
    lever.onDetent = [this] (int d) { toggles[0]->setEnabled (d >= 2); };   // FAT lives only in CRUNCH and OVERDRIVE

    for (size_t i = 0; i < toggles.size(); ++i)
    {
        toggles[i] = std::make_unique<BatToggle> (switches[i].name, switches[i].positions);
        auto& t = *toggles[i];
        stage.addAndMakeVisible (t);
        t.setBounds (around (holes[i], 46.0f, 66.0f));
        t.onHover = [this, i, &t] (bool over)
        {
            cockpit.showHover (over ? (int) i : -1, over ? t.name + "  " + t.stateText() : juce::String());
        };
    }
    toggles[0]->setEnabled (false);

    // A prototype's hook for looking at the window at speed without a hand on the lever.
    if (const auto d = juce::SystemStats::getEnvironmentVariable ("CATPIT_DETENT", {}); d.isNotEmpty())
        lever.setDetent (d.getIntValue(), false);

    stage.addAndMakeVisible (turbo);
    turbo.setBounds (turboFrame.toNearestInt());

    const juce::Colour lineFrom[] { orange.darker (0.3f), violet, orange.darker (0.3f) };
    const juce::Colour lineTo[]   { orange,               orange, orange };
    for (size_t i = 0; i < sliders.size(); ++i)
    {
        sliders[i] = std::make_unique<PlateSlider> (lineFrom[i], lineTo[i]);
        stage.addAndMakeVisible (*sliders[i]);
        sliders[i]->setBounds (sliderInsets[i].toNearestInt());
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
