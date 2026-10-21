// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "RedPawControls.h"
#include "RedPawSkin.h"

#include "../face/Console.h"

namespace orbitamp::redpaw
{

using face::orange;
using face::labelLilac;

juce::Rectangle<float> GearLever::leverBox (float y, float x) const
{
    const auto& art = Art::of (skin).lever;
    const float w = skin.plan.leverWidth;
    const float h = art.isValid() ? w * (float) art.getHeight() / (float) art.getWidth() : w / 3.0f;
    return { x - w * 0.5f, y + skin.plan.collarSink - h, w, h };
}

juce::Rectangle<float> GearLever::standingBox() const
{
    // In a notch the collar stands in the notch; between two, in the slot. The slot and its notches in
    // the lever's own space, read here and not as the program starts (RedPawLayout.h).
    const float slotX  = skin.plan.slotX - skin.plan.leverLeft;
    const float notchX = skin.plan.notchX - skin.plan.leverLeft;
    const float p = travel();
    const float inNotch = juce::jlimit (0.0f, 1.0f, 1.0f - std::abs (p - std::round (p)) / 0.2f);
    return leverBox (yOf (p), slotX + (notchX - slotX) * inNotch);
}

GearLever::GearLever (const Skin& s, int g)
    : DetentLever (juce::jlimit (1, (int) s.plan.stops.size(), g), 0,
                   260.0f, 22.0f),     // a heavy lever: it overshoots the notch a hair and comes back
      skin (s)
{
    // Each position's height in the dashboard's space: the gears at their stops from the top. A stop
    // past the last gear is no position.
    jassert (numGears() == g);
    for (int i = 0; i < numGears(); ++i)
        heights.push_back (skin.plan.stops[(size_t) i]);

    lit.stiffness = 40.0f;      // soft, like the knobs' rings
    lit.damping   = 13.0f;
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

float GearLever::yOf (float at) const noexcept
{
    const int last = numPlaces() - 1;
    if (last < 1)       // a lever of one gear stands in it
        return heights.front() - skin.plan.leverTop;
    const float p = juce::jlimit (0.0f, (float) last, at);
    const int i = juce::jmin (last - 1, (int) p);
    return juce::jmap (p - (float) i, heights[(size_t) i], heights[(size_t) i + 1]) - skin.plan.leverTop;
}

float GearLever::travelAt (float y) const noexcept
{
    const int last = numPlaces() - 1;
    y += skin.plan.leverTop;
    if (y <= heights.front()) return 0.0f;
    for (int i = 0; i < last; ++i)
        if (y <= heights[(size_t) i + 1])
            return (float) i + (y - heights[(size_t) i]) / (heights[(size_t) i + 1] - heights[(size_t) i]);
    return (float) last;
}

void GearLever::timerCallback()
{
    // Its light glides on the same frames as the lever, and the frames go on until both stand still.
    const bool moving = glide();
    if (! lit.step (face::frameDt) && ! moving)
        stopTimer();
    travelled();
}

void GearLever::paint (juce::Graphics& g)
{
    // The lever from above: its grip across the slot, the collar under it standing on the position.
    const auto& art = Art::of (skin).lever;
    const auto box = standingBox();
    const auto bar = box.withHeight (box.getHeight() * barBottom);

    // Its shadow on the plate, the light being from the upper left: the bar's, soft-edged.
    for (const auto& [grow, alpha] : { std::pair { 3.0f, 0.12f }, std::pair { 1.5f, 0.18f }, std::pair { 0.0f, 0.25f } })
    {
        g.setColour (juce::Colours::black.withAlpha (alpha));
        g.fillRoundedRectangle (bar.translated (3.0f, 5.0f).expanded (grow), bar.getHeight() * 0.5f + grow);
    }

    // A soft cyan light round it, the knobs' rings' own, keeps it in sight on the dark plate; under the
    // hand it turns the orange everything that moves wears there.
    if (const auto& glow = Art::of (skin).leverGlow; glow.isValid() && art.isValid())
    {
        const float k = box.getWidth() / (float) art.getWidth();
        const float hand = juce::jlimit (0.0f, 1.0f, lit.value);
        g.setColour (skin.ringLight.interpolatedWith (orange, hand).withAlpha (0.6f + 0.4f * hand));
        g.drawImage (glow, box.expanded ((float) Art::leverGlowPad * k), juce::RectanglePlacement::stretchToFit, true);
    }

    g.setOpacity (1.0f);        // an image is drawn at the colour's opacity: the light's would let the slot through
    if (art.isValid())
        g.drawImage (art, box);
    else
    {
        g.setColour (juce::Colour (0xff2a2a30));
        g.fillRoundedRectangle (bar, bar.getHeight() * 0.5f);
    }
}

void GearLever::mouseDown (const juce::MouseEvent& e)
{
    // Taken by the grip — the bar the hand sees, not the room round it, where the next position up
    // already is — it goes with the hand from where it was taken; anywhere else on the gate it waits:
    // dragged, it goes where the hand is, and let go where it was pressed, into the position nearest the
    // press (mouseUp).
    const auto box = standingBox();
    take (box.withHeight (box.getHeight() * barBottom).contains (e.position), e.position.y);
}

void GearLever::mouseDrag (const juce::MouseEvent& e)
{
    if (isHeld() && e.mouseWasDraggedSinceMouseDown())
        follow (e.position.y);
}

void GearLever::mouseUp (const juce::MouseEvent& e)
{
    if (! isHeld())
        return;
    if (! isMouseOver (true) && ! getLocalBounds().contains (e.getPosition()))
    {
        lit.target = 0.0f;
        startTimerHz (60);
    }
    // A press that did not travel: the position nearest it — on the grip, the one the lever stands in.
    letGo (e.mouseWasDraggedSinceMouseDown() ? travel() : underHand (e.position.y));
}

//==============================================================================
NavHeader::NavHeader()
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (10);
}

void NavHeader::set (const juce::String& g, const juce::String& c)
{
    if (g == gear && c == channel)
        return;
    gear = g;
    channel = c;
    repaint();
}

void NavHeader::timerCallback()
{
    if (const int kmh = speed ? juce::roundToInt (speed()) : 0; kmh != shownKmh)
    {
        shownKmh = kmh;
        repaint();
    }
}

NavHeader::Fit NavHeader::fit (const juce::String& g, const juce::String& c, int kmh) const
{
    const auto area = getLocalBounds().toFloat().reduced (8.0f, 0.0f);
    const auto words = face::labelFont (layout::px15).withExtraKerningFactor (0.04f);
    const auto small = face::labelFont (layout::px13).withExtraKerningFactor (0.04f);
    const auto unit = juce::String ("KM/H"), number = juce::String (juce::jmax (0, kmh));
    const auto widthOf = [] (const juce::Font& font, const juce::String& text) { return juce::GlyphArrangement::getStringWidth (font, text) + 2.0f; };

    Fit f;
    const float beside = widthOf (words, g) + 10.0f
                       + (showsSpeed ? 8.0f + widthOf (words, number) + 6.0f + widthOf (small, unit) : 0.0f);
    f.wanted = beside + widthOf (small, c);
    f.room = juce::jmax (0.0f, area.getWidth() - beside);
    if (const int lines = c.isEmpty() ? 2 : 3; f.wanted > area.getWidth() && area.getHeight() >= (float) lines * layout::px13)
        f.lines = lines;

    // What the cut line shows, read back off it. The ellipsis JUCE ends a cut with is a glyph of its own
    // that reports, as its character, the first letter cut off — and, from a font of its own, it can share
    // its number with a letter of this one: the line is read off the cut as far as its glyphs are the
    // whole line's, the glyph and the font it stands in (its height over and under the line).
    juce::GlyphArrangement whole, cut;
    whole.addLineOfText (small, c, 0.0f, 0.0f);
    cut.addCurtailedLineOfText (small, c, 0.0f, 0.0f, f.room, true);
    const auto same = [] (const juce::PositionedGlyph& a, const juce::PositionedGlyph& b)
    {
        return a.getGlyphIndex() == b.getGlyphIndex()
            && juce::approximatelyEqual (a.getBaselineY() - a.getTop(), b.getBaselineY() - b.getTop())
            && juce::approximatelyEqual (a.getBottom() - a.getBaselineY(), b.getBottom() - b.getBaselineY());
    };
    int kept = 0;
    while (kept < cut.getNumGlyphs() && kept < whole.getNumGlyphs() && same (cut.getGlyph (kept), whole.getGlyph (kept)))
        ++kept;
    for (int i = 0; i < kept; ++i)
        if (const auto ch = cut.getGlyph (i).getCharacter(); ch != 0)
            f.shown << juce::String::charToString (ch);
    if (kept < cut.getNumGlyphs() || f.shown != c)
        f.shown << juce::String::charToString ((juce::juce_wchar) 0x2026);
    return f;
}

void NavHeader::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat().reduced (8.0f, 0.0f);
    const auto words = face::labelFont (layout::px15).withExtraKerningFactor (0.04f);
    const auto small = face::labelFont (layout::px13).withExtraKerningFactor (0.04f);
    const auto unit = juce::String ("KM/H"), number = juce::String (juce::jmax (0, shownKmh));
    const auto widthOf = [] (const juce::Font& font, const juce::String& text) { return juce::GlyphArrangement::getStringWidth (font, text) + 2.0f; };

    // A SCREEN TOO NARROW FOR THEM ON ONE LINE, and tall enough for a line each: the gear, the channel
    // under it — set condensed, as tall, where it is wider than the screen — and the speed under that;
    // the gear and the speed alone where the channel is not named.
    if (const int lines = fit (gear, channel, shownKmh).lines; lines > 1)
    {
        const float row = area.getHeight() / (float) lines;
        auto first = area.removeFromTop (row), second = lines == 3 ? area.removeFromTop (row) : juce::Rectangle<float>(), third = area;
        g.setColour (orange);
        g.setFont (words);
        g.drawText (gear, first, juce::Justification::centredLeft, false);
        if (lines == 3)
        {
            const juce::Graphics::ScopedSaveState keep (g);
            const float wide = widthOf (small, channel);
            if (wide > second.getWidth())
                g.addTransform (juce::AffineTransform::scale (second.getWidth() / wide, 1.0f, second.getX(), second.getY()));
            g.setColour (juce::Colour (0xffece9f6));
            g.setFont (small);
            g.drawText (channel, second.withWidth (juce::jmax (wide, second.getWidth())), juce::Justification::centredLeft, false);
        }
        if (showsSpeed)
        {
            g.setColour (juce::Colour (0xffece9f6));
            g.setFont (words);
            g.drawText (number, third.removeFromLeft (widthOf (words, number)), juce::Justification::centredLeft, false);
            third.removeFromLeft (6.0f);
            g.setColour (labelLilac.withAlpha (0.8f));
            g.setFont (small);
            g.drawText (unit, third, juce::Justification::centredLeft, false);
        }
        return;
    }

    // The speed on the right: the number, then its unit a size down.
    if (showsSpeed)
    {
        g.setColour (labelLilac.withAlpha (0.8f));
        g.setFont (small);
        g.drawText (unit, area.removeFromRight (juce::GlyphArrangement::getStringWidth (small, unit) + 2.0f),
                    juce::Justification::centredRight, false);
        area.removeFromRight (6.0f);
        g.setColour (juce::Colour (0xffece9f6));
        g.setFont (words);
        g.drawText (number, area.removeFromRight (juce::GlyphArrangement::getStringWidth (words, number) + 2.0f),
                    juce::Justification::centredRight, false);
        area.removeFromRight (8.0f);
    }
    g.setFont (words);

    // The gear, then the channel.
    g.setColour (orange);
    g.drawText (gear, area.removeFromLeft (juce::GlyphArrangement::getStringWidth (words, gear) + 2.0f),
                juce::Justification::centredLeft, false);
    area.removeFromLeft (10.0f);
    g.setColour (juce::Colour (0xffece9f6));
    g.setFont (small);      // the channel a size down: at the gear's size the longest would not fit beside the speed
    g.drawText (channel, area, juce::Justification::centredLeft, true);
}

//==============================================================================
SpeedHud::SpeedHud()
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (refreshHz);
}

void SpeedHud::timerCallback()
{
    if (const int kmh = speed ? juce::roundToInt (speed()) : 0; kmh != shownKmh)
    {
        shownKmh = kmh;
        repaint();
    }
}

void SpeedHud::paint (juce::Graphics& g)
{
    const auto box = getLocalBounds().toFloat();
    const juce::Colour ink { light };
    const float h = box.getHeight();

    // The number and its unit as outlines, so the light can glow round them as well as fill them.
    const auto glyphs = [] (const juce::String& text, const juce::Font& font, juce::Point<float> centre)
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText (font, text, 0.0f, 0.0f);
        juce::Path p;
        ga.createPath (p);
        const auto b = p.getBounds();
        p.applyTransform (juce::AffineTransform::translation (centre.x - b.getCentreX(), centre.y - b.getCentreY()));
        return p;
    };
    juce::Path lit;
    lit.addPath (glyphs (juce::String (juce::jmax (0, shownKmh)), face::labelFont (h * numberOfHeight).withExtraKerningFactor (numberKerning),
                         { box.getCentreX(), box.getY() + h * numberCentreOfHeight }));
    lit.addPath (glyphs (unit, face::labelFont (h * unitOfHeight).withExtraKerningFactor (unitKerning),
                         { box.getCentreX(), box.getY() + h * unitCentreOfHeight }));


    juce::DropShadow (ink.withAlpha (glowAlpha), juce::roundToInt (h * glowOfHeight), {}).drawForPath (g, lit);
    g.setColour (ink.withAlpha (lightAlpha));
    g.fillPath (lit);
}

//==============================================================================
GlassFilm::GlassFilm()
{
    setInterceptsMouseClicks (false, false);
}

void GlassFilm::paint (juce::Graphics& g)
{
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (! film.isValid() || std::abs (scale - filmScale) > 1.0e-3f)
    {
        filmScale = scale;
        film = make (scale);
    }
    g.drawImage (film, getLocalBounds().toFloat());
}

juce::Image GlassFilm::make (float scale) const
{
    const auto size = getLocalBounds().toFloat();
    juce::Image img (juce::Image::ARGB, juce::jmax (1, juce::roundToInt (size.getWidth() * scale)),
                     juce::jmax (1, juce::roundToInt (size.getHeight() * scale)), true);
    juce::Graphics g (img);
    g.addTransform (juce::AffineTransform::scale (scale));
    juce::Random random (0x52504157);       // the same glass on every machine and every start

    const float w = size.getWidth(), h = size.getHeight();

    // MURK: the glass a shade milky toward one corner, a shade darker at its edges.
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.045f), w * 0.18f, h * 0.15f,
                                             juce::Colours::transparentWhite, w * 0.75f, h * 0.9f, true));
    g.fillAll();
    g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, w * 0.5f, h * 0.5f,
                                             juce::Colours::black.withAlpha (0.22f), 0.0f, 0.0f, true));
    g.fillAll();

    // A SHEEN: a soft band of light across it, the way a screen catches the room.
    {
        auto sheen = juce::ColourGradient (juce::Colours::transparentWhite, w * 0.30f, 0.0f, juce::Colours::transparentWhite, w * 0.58f, h, false);
        sheen.addColour (0.5, juce::Colours::white.withAlpha (0.03f));
        g.setGradientFill (sheen);
        g.fillAll();
    }

    // DUST: fine specks, most of them a fraction of a pixel's light, a few soft larger motes.
    for (int i = 0; i < 420; ++i)
    {
        const float d = 0.5f + random.nextFloat() * random.nextFloat() * 1.6f;
        g.setColour (juce::Colour (0xffe8dccf).withAlpha (0.05f + random.nextFloat() * 0.11f));
        g.fillEllipse (random.nextFloat() * w, random.nextFloat() * h, d, d);
    }
    for (int i = 0; i < 18; ++i)
    {
        const float d = 3.0f + random.nextFloat() * 5.0f;
        const juce::Point<float> c { random.nextFloat() * w, random.nextFloat() * h };
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd8c8b8).withAlpha (0.05f), c,
                                                 juce::Colours::transparentWhite, c.translated (d, 0.0f), true));
        g.fillEllipse (juce::Rectangle<float> (d * 2.0f, d * 2.0f).withCentre (c));
    }

    // SCRATCHES: a few long light ones, mostly the one way a cloth went, gently bent, and a few short.
    for (int i = 0; i < 14; ++i)
    {
        const bool lengthy = i < 6;
        const float len = lengthy ? w * (0.25f + random.nextFloat() * 0.35f) : 12.0f + random.nextFloat() * 30.0f;
        const float angle = juce::degreesToRadians (-18.0f + random.nextFloat() * 14.0f + (i % 5 == 4 ? 70.0f : 0.0f));
        const juce::Point<float> a { random.nextFloat() * w, random.nextFloat() * h };
        const juce::Point<float> b = a + juce::Point<float> (std::cos (angle), std::sin (angle)) * len;
        const auto mid = (a + b) * 0.5f + juce::Point<float> (-std::sin (angle), std::cos (angle)) * (random.nextFloat() - 0.5f) * len * 0.08f;
        juce::Path line;
        line.startNewSubPath (a);
        line.quadraticTo (mid, b);
        g.setColour (juce::Colour (0xffece6f0).withAlpha (lengthy ? 0.06f + random.nextFloat() * 0.05f : 0.08f + random.nextFloat() * 0.06f));
        g.strokePath (line, juce::PathStrokeType (0.45f + random.nextFloat() * 0.3f));
    }
    return img;
}

//==============================================================================
namespace
{
    constexpr double breathHoldMs = 1000.0;
}

KnobRing::KnobRing (const Skin& s, const face::ArtKnob& k, juce::Point<float> centre, float r, float w)
    : skin (s), knob (k), radius (r), width (w)
{
    setInterceptsMouseClicks (false, false);
    breath.stiffness = 40.0f;       // soft: a lamp warming, not a spring
    breath.damping   = 13.0f;
    place (centre, r, w);
    startTimerHz (30);
}

void KnobRing::place (juce::Point<float> centre, float r, float w)
{
    radius = r;
    width = w;
    setBounds (juce::Rectangle<float> ((radius + width + 8.0f) * 2.0f, (radius + width + 8.0f) * 2.0f)
                   .withCentre (centre).getSmallestIntegerContainer());
    repaint();
}

void KnobRing::timerCallback()
{
    // The hand on the knob, or a moment ago: the ring warms; a second after it has gone, it cools.
    const bool now = knob.isMouseOverOrDragging();
    const double ms = juce::Time::getMillisecondCounterHiRes();
    if (touched && ! now)
        leftAt = ms;
    touched = now;
    breath.target = now || ms - leftAt < breathHoldMs ? 1.0f : 0.0f;

    const bool easing = breath.step (1.0f / 30.0f);
    const bool moved = std::abs (knob.getValue() - shown) > 1.0e-4f;
    if (easing || moved)
        repaint();
}

void KnobRing::paint (juce::Graphics& g)
{
    shown = knob.getValue();
    const auto c = getLocalBounds().toFloat().getCentre();
    const float b = juce::jlimit (0.0f, 1.0f, breath.value);

    // Under the hand, an orange light round the knob's cap, inside its ring — the hand's colour, as on
    // everything else that moves — warming and cooling with the ring.
    if (b > 0.01f)
    {
        juce::Path rim;
        const float r = (float) knob.getWidth() * rimOfKnob;
        rim.addEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
        face::drawHandRim (g, rim, orange.withAlpha (rimAlpha * b));
    }

    // The rest of the ring dimmed: the painted cyan under a dark veil.
    juce::Path whole;
    whole.addCentredArc (c.x, c.y, radius, radius, 0.0f, 0.0f, juce::MathConstants<float>::twoPi, true);
    g.setColour (juce::Colour (veil).withAlpha (veilAlpha));
    g.strokePath (whole, juce::PathStrokeType (width + veilWider));

    // From the start of the travel to the pointer, lit: the arc made a band of the ring's own width
    // first, so that the glow and the light are that band and nothing else — a shadow drawn for the
    // bare arc would fill the chord it spans, over the knob.
    const float to = juce::degreesToRadians (from + sweep * juce::jlimit (0.0f, 1.0f, shown));
    juce::Path arc, band;
    arc.addCentredArc (c.x, c.y, radius, radius, 0.0f, juce::degreesToRadians (from), to, true);
    juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath (band, arc);
    const auto light = skin.ringLight;
    juce::DropShadow (light.withAlpha (glowAlpha + glowAlphaHand * b), juce::roundToInt (glowRadius + glowRadiusHand * b), {}).drawForPath (g, band);
    g.setColour (light.withAlpha (litAlpha + litAlphaHand * b));
    g.fillPath (band);
}

} // namespace orbitamp::redpaw
