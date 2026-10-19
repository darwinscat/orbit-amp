// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "RedPawEditor.h"

#include "RedPawLayout.h"
#include "RedPawLineup.h"
#include "RedPawSkin.h"

#include "../ui/Prefs.h"

#include <BinaryData.h>

namespace orbitamp
{

juce::AudioProcessorEditor* makeEditor (AmpProcessor& p)
{
    return new redpaw::RedPawEditor (p);
}

namespace redpaw
{

using namespace face;

namespace
{
    // Where the pointer is drawn on the knobs' renders: a lit slot from `inner` to `outer` of the cap's
    // radius, `width` wide — on the machined cap, clear of the knurled ring.
    constexpr ArtKnob::Pointer drivePointer { 0.30f, 0.66f, 0.06f };
    constexpr ArtKnob::Pointer tonePointer  { 0.30f, 0.64f, 0.07f };

    /** THE HAND'S LIGHT: a soft orange round whatever the hand is over or on — the family's colour for
        the hand, the dashboard's desert accent. */
    const juce::Colour handLight = orange.withAlpha (0.8f);

    /** What the windscreen shows, and how often — the machine's, kept with its settings. The landscape
        as a word, in the order of Windscreen::Mode; what each is called in the menu is the world's
        (RedPawSkin.h). */
    const juce::Identifier landscapeKey { "landscape" };
    const juce::Identifier frameRateKey { "landscape_fps" };
    const juce::StringArray landscapeWords { "plains", "1993", "still" };

    /** The nav screen's picture — the machine's too, as a view's number (face::Monitor::View). */
    const juce::Identifier screenKey { "screen" };

    /** The wipers' speed, where the face has keys for it — the machine's too (Drive::setWipers). */
    const juce::Identifier wipersKey { "wipers" };
    const juce::Identifier lightsKey { "searchlights" };     // ...and the searchlights, a bit each

    /** A SEARCHLIGHT'S SIGN, as a dashboard writes it: the lamp's round face, flat on the side its beam
        leaves by, and the beam's four rays — to the left for the left lamp. */
    void drawLampSign (juce::Graphics& g, juce::Rectangle<float> key, bool left, juce::Colour colour)
    {
        const auto box = key.withSizeKeepingCentre (key.getWidth() * 0.5f, key.getHeight() * 0.34f);
        const float w = box.getWidth(), h = box.getHeight();
        const float lampW = w * 0.42f, gap = w * 0.1f, rays = w - lampW - gap;
        juce::Path lamp, beam;
        const float lx = left ? box.getRight() - lampW : box.getX();
        // The D: round on the lamp's back, flat where the light leaves.
        lamp.startNewSubPath (left ? lx : lx + lampW, box.getY());
        lamp.cubicTo (left ? lx + lampW * 1.3f : lx - lampW * 0.3f, box.getY(),
                      left ? lx + lampW * 1.3f : lx - lampW * 0.3f, box.getBottom(),
                      left ? lx : lx + lampW, box.getBottom());
        lamp.closeSubPath();
        const float rx0 = left ? box.getX() : box.getRight() - rays, rx1 = rx0 + rays;
        for (int i = 0; i < 4; ++i)
        {
            const float y = box.getY() + h * (0.12f + 0.76f * (float) i / 3.0f);
            beam.startNewSubPath (rx0, y);
            beam.lineTo (rx1, y);
        }
        g.setColour (colour);
        g.strokePath (lamp, juce::PathStrokeType (1.6f));
        g.strokePath (beam, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    juce::Rectangle<float> around (juce::Point<float> c, float w, float h)
    {
        return juce::Rectangle<float> (w, h).withCentre (c);
    }

    /** A row of words over or under a well, as wide as the well. */
    juce::Rectangle<float> wellRow (size_t well)
    {
        const auto& w = layout::wells[well];
        return { w.getX(), layout::wellNameY[well] - 15.0f, w.getWidth(), 30.0f };
    }

    /** WHERE THE CONSOLE'S CONTROLS STAND on the dashboard (RedPawLayout.h). Each knob is drawn smaller
        than its cyan ring, which stays in sight round it. */
    Console::Places dashboardPlaces()
    {
        using namespace layout;

        Console::Places p;
        p.drive = around (drive.centre, drive.knob, drive.knob);
        for (size_t i = 0; i < tones.size(); ++i)
            p.tones[i] = around (tones[i].centre, tones[i].knob, tones[i].knob);

        p.keys = keys;
        p.keySize = keySize;
        p.holes = holes;
        p.holeKeySize = 44.0f;      // the hole's pitch: a toggle up or down stands out of it

        for (size_t i = 0; i < p.sliders.size(); ++i)
        {
            const auto& w = wells[i];
            p.sliders[i] = juce::Rectangle<float> (w.getWidth(), sliderHeight).withCentre (w.getCentre());
            p.sliderNames[i] = wellRow (i);
        }
        p.sliderCapAngle  = 0.0f;   // the caps' renders lie along their wells...
        p.sliderCapLength = sliderCapLength;
        p.faderCapLength  = faderCapLength;     // ...and across their slots

        p.inMeter  = inMeter;
        p.outMeter = outMeter;
        p.meterDots = meterDots;
        p.inFader  = inFader.withSizeKeepingCentre (faderWidth, inFader.getHeight());
        p.outFader = outFader.withSizeKeepingCentre (faderWidth, outFader.getHeight());
        p.tuner = tunerScreen;
        p.tunerStrip = true;            // one low line along the screen's bottom...
        p.tunerDial = tunerDial;        // ...unless the picture paints the tuner a dial of its own...
        p.tunerMenuOnClick = true;      // ...whose settings a click on it opens
        for (size_t i = 0; i < p.lamps.size(); ++i)
            p.lamps[i] = lamps[i];
        p.lampDot = lampDot;

        if (skin().mark)
        {
            p.emblemFace = { emblemFace.begin(), emblemFace.end() };
            p.emblemMark = emblemMark;
        }

        // The bottom plates: PRESET alone on the left, the version in the middle, MEM over CPU on the
        // right.
        p.preset  = leftPlate;
        p.version = centrePlate;
        p.memory  = readings[0];
        p.load    = readings[1];
        p.readingsFit = true;           // on a small screen, condensed into it

        p.gateDoor    = around (gateWord, 110.0f, 28.0f);
        p.limiterDoor = around (limiterWord, 124.0f, 28.0f);
        p.gateDoorAngle    = lampSlant;
        p.limiterDoorAngle = -lampSlant;
        p.readingPx   = px13;
        return p;
    }

    Console::Look dashboardLook()
    {
        const auto& art = Art::get();
        return { art.knobDrive, drivePointer, art.knobTone, tonePointer, art.sliderCap, art.faderCap,
                 { &art.lampLit[0], &art.lampLit[1], &art.lampLit[2], &art.lampLit[3] },
                 Art::mark().isNotEmpty() ? Art::mark() : juce::String::fromUTF8 (BinaryData::catlogo_svg, BinaryData::catlogo_svgSize),   // the rover's own mark; the family's, where it has none
                 skin().midiCommands,
                 handLight,         // under the hand, the dashboard's orange
                 skin().ink };
    }

    /** A WORD WIDER THAN ITS ROOM is set condensed into it — as tall as ever, narrower — about `centre`:
        what follows is drawn so. */
    void condenseInto (juce::Graphics& g, const juce::String& text, float em, float trackingEm, float room, juce::Point<float> centre)
    {
        const float wide = juce::GlyphArrangement::getStringWidth (labelFont (em).withExtraKerningFactor (trackingEm), text.toUpperCase());
        if (wide > room)
            g.addTransform (juce::AffineTransform::scale (room / wide, 1.0f, centre.x, centre.y));
    }

    /** A render drawn fitted into `box` by its opaque part — the square keys and TURBO, up or down. */
    void drawRender (juce::Graphics& g, const juce::Image& img, juce::Rectangle<float> box)
    {
        drawFitted (g, img, box);
    }

    /** A KEY'S OWN WORD, on its face (`key`, the render's opaque part): a size under the names beside the
        keys, condensed into the face where it is wider, the state's colour while the key is lit. */
    constexpr float capEm = 10.0f / layout::readingScale;
    void capWord (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> key, bool lit)
    {
        const auto& ink = skin().ink;
        const juce::Graphics::ScopedSaveState keep (g);
        condenseInto (g, text, capEm, keyTracking, key.getWidth() * 0.62f, key.getCentre());
        ink.label (g, text, juce::Rectangle<float> (80.0f, 20.0f).withCentre (key.getCentre()), capEm,
                   lit ? ink.state : ink.at (ink.words, 0.75f), keyTracking);
    }

    /** THE ROOF'S TOGGLE. The two renders are not drawn alike — up, the bat stands over its nut; down,
        it hangs under a nut drawn larger — so each is fitted by its own opaque part: both nuts as wide
        as `nut`, centred on the hole, the bat up or down from there. */
    void drawToggle (juce::Graphics& g, juce::Rectangle<float> keyBox, bool up)
    {
        const auto& art = Art::get();
        const auto& img = up ? art.toggleUp : art.toggleDown;
        if (! img.isValid())
            return;

        const auto src = opaqueBounds (img);
        constexpr float nut = 20.0f;
        const float k = nut / src.getWidth();
        const auto c = keyBox.getCentre();
        const float h = src.getHeight() * k;
        const auto dst = up ? juce::Rectangle<float> (nut, h).withPosition (c.x - nut * 0.5f, c.y + nut * 0.5f - h)
                            : juce::Rectangle<float> (nut, h).withPosition (c.x - nut * 0.5f, c.y - nut * 0.5f);
        g.drawImageTransformed (img, juce::AffineTransform::translation (-src.getX(), -src.getY()).scaled (k).translated (dst.getX(), dst.getY()));
    }
}

//==============================================================================
juce::Rectangle<int> Dashboard::historyArea() const
{
    return juce::Rectangle<float> (layout::holes.front().x - 40.0f, 0.0f,
                                   layout::holes.back().x - layout::holes.front().x + 60.0f, 47.0f).toNearestInt();
}

void Dashboard::paintLive (juce::Graphics& g) const
{
    paintRoof (g);
    paintKeyNames (g);
}

/** The signs by the roof's keys, each in the gap left of its hole: the history's turning arrows, lit
    while there is a step to take, and the registers' letters, the one that is playing lit orange. */
void Dashboard::paintRoof (juce::Graphics& g) const
{
    using namespace layout;

    const auto& ink = skin().ink;
    const auto signAt = [] (juce::Point<float> hole) { return juce::Point<float> (hole.x - signOffset, hole.y); };

    const bool can[] { canUndo, canRedo };
    for (int i = 0; i < 2; ++i)
    {
        const auto at = signAt (holes[(size_t) (ovUndo + i)]);
        const auto colour = ink.at (ink.words, can[i] ? 1.0f : 0.35f);
        if (! ink.edge.isTransparent())
            drawTurnArrow (g, at.translated (0.0f, Ink::edgeDrop), 6.0f, i == 0, ink.under (colour));
        drawTurnArrow (g, at, 6.0f, i == 0, colour);
    }

    // The gaps between the holes' bezels are narrower than a 13-px capital is wide: the letters are set
    // condensed — as tall as ever, three quarters as wide.
    for (int i = 0; i < numRegisterKeys; ++i)
    {
        const auto at = signAt (holes[(size_t) (ovFirstRegister + i)]);
        const juce::Graphics::ScopedSaveState keep (g);
        g.addTransform (juce::AffineTransform::scale (0.75f, 1.0f, at.x, at.y));
        ink.label (g, juce::String::charToString ((juce::juce_wchar) ('A' + i)), juce::Rectangle<float> (30.0f, 26.0f).withCentre (at),
                   px13, i == activeRegister ? ink.state : ink.at (ink.words, 0.8f), 0.0f);
    }
}

/** The keys' names, over or under them by turns — a dimmed key's as dim as the key, and the playing
    pack's own where its switches are on them. None round the keys where they are on them. */
void Dashboard::paintKeyNames (juce::Graphics& g) const
{
    using namespace layout;
    if (keyNamesOnKeys)
        return;

    const auto& ink = skin().ink;
    for (size_t i = 0; i < keyNames.size(); ++i)
    {
        const juce::Point<float> at { keys[i].x, keyNameOver[i] ? keyNameOverY : keyNameUnderY };
        const juce::Graphics::ScopedSaveState keep (g);
        condenseInto (g, keyNames[i], px11, keyTracking, keyNameRoom, at);
        ink.label (g, keyNames[i], juce::Rectangle<float> (120.0f, 20.0f).withCentre (at),
                   px11, ink.at (ink.words, keysLive[i] ? 1.0f : PushButton::dimmed), keyTracking);
    }
}

void Dashboard::paintStatic (juce::Graphics& g, float scale) const
{
    using namespace layout;

    if (const auto& bg = Art::get().bg; bg.isValid())
        g.drawImage (bg, juce::Rectangle<float> (0.0f, 0.0f, width, height));

    // The maker's mark: raised in the rover's metal where the skin brings a render of it — cut into bright
    // steel it read as a stamp — and engraved where it does not; none where the skin has no place for one.
    if (const auto& mark = Art::get().emblem; skin().mark && mark.isValid())
    {
        g.setOpacity (1.0f);
        g.drawImage (mark, emblemMark, juce::RectanglePlacement::centred);
    }
    else if (skin().mark)
    {
        engraveMark (g, emblemMark, scale);
    }

    // A word on the dust: a shadow under it, so it reads off the rust as well as off the bare metal —
    // where the skin has one (RedPawSkin.h).
    const auto& ink = skin().ink;
    const auto shadow = skin().printShadow;
    const auto print = [&g, &ink, shadow] (const juce::String& text, juce::Point<float> at, float w, juce::Colour colour,
                                           float room = 0.0f, float em = px13)
    {
        const auto area = juce::Rectangle<float> (w, 28.0f).withCentre (at);
        const juce::Graphics::ScopedSaveState keep (g);
        if (room > 0.0f)
            condenseInto (g, text, em, 0.08f, room, at);
        if (! shadow.isTransparent())
            drawLabel (g, text, area.translated (0.0f, 1.5f), em, shadow);
        ink.label (g, text, area, em, colour);
    };

    print (skin().driveName, { drive.centre.x, driveNameY }, 200.0f, ink.words);
    const char* toneNames[] { "BASS", "MIDDLE", "TREBLE" };
    for (size_t i = 0; i < tones.size(); ++i)
        print (toneNames[i], { tones[i].centre.x, knobNameY }, 150.0f, ink.words, 0.0f, knobNameEm);

    print ("TURBO", turboName, 140.0f, ink.state);
    if (learnKeySize > 0.0f)
        for (size_t k = 0; k < learnNames.size(); ++k)        // each key's LEARN over its name
        {
            constexpr float em = 8.5f / readingScale;     // under the keys' own words
            print ("LEARN", learnNames[k].translated (0.0f, -0.5f * learnLine), 90.0f, ink.words, 0.0f, em);
            print (k == 0 ? "GATE" : "HUM", learnNames[k].translated (0.0f, 0.5f * learnLine), 90.0f, ink.words, 0.0f, em);
        }
    print (skin().inputName, inputName, 170.0f, ink.words, columnNameRoom);
    print (skin().outputName, outputName, 170.0f, ink.words, columnNameRoom);

    // The maker's words beside the mark, a size under the controls' names and a little quieter.
    if (makerRoom > 0.0f)
    {
        print ("POWERED BY", poweredBy, 220.0f, ink.at (ink.words, 0.8f), makerRoom, px11);
        print ("DARWIN'S CAT", makerName, 240.0f, ink.at (ink.words, 0.8f), makerRoom, px11);
    }
}

//==============================================================================
/** The rover's own part of the site's demo (Console::webExtras): the windscreen — the box its view is
    drawn in and the corners of the glass in it — the lever's slot, its notches and its PARK end, the gears'
    names, TURBO and its hatch, the wipers' keys; the renders of all of them, the still it ships, and the
    poster, that still through the glass. */
void RedPawEditor::webExtras (const juce::File& dir, juce::DynamicObject& places, juce::DynamicObject& world)
{
    using namespace layout;
    const auto& art = Art::get();
    const std::pair<const char*, const juce::Image*> own[] {
        { "lever.png", &art.lever }, { "lever-glow.png", &art.leverGlow }, { "fader-cap-light.png", &art.faderCapLight },
        { "toggle-up.png", &art.toggleUp }, { "toggle-down.png", &art.toggleDown }, { "key-up.png", &art.keyUp },
        { "key-down.png", &art.keyDown }, { "turbo-up.png", &art.turboUp }, { "turbo-down.png", &art.turboDown },
        { "turbo-lid.png", &art.turboLid }, { "emblem.png", &art.emblem } };
    for (const auto& [name, img] : own)
        writeSprite (dir, name, *img);
    if (const auto still = Art::still(); still.isValid())
    {
        dir.getChildFile ("still.jpg").deleteFile();
        juce::FileOutputStream out (dir.getChildFile ("still.jpg"));
        juce::JPEGImageFormat jpeg;
        jpeg.setQuality (0.9f);
        if (out.openedOk())
            jpeg.writeImageToStream (still, out);
    }

    const auto rect = [] (juce::Rectangle<float> r)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("x", r.getX()); o->setProperty ("y", r.getY());
        o->setProperty ("w", r.getWidth()); o->setProperty ("h", r.getHeight());
        return juce::var (o);
    };
    const auto point = [] (juce::Point<float> p)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("x", p.x); o->setProperty ("y", p.y);
        return juce::var (o);
    };
    auto* o = new juce::DynamicObject();
    o->setProperty ("windscreen", rect (windscreen));
    juce::Array<juce::var> corners;
    for (const auto& c : windscreenCorners)
        corners.add (point (c));
    o->setProperty ("windscreenCorners", corners);

    auto* l = new juce::DynamicObject();
    l->setProperty ("slot", rect (gateSlot));
    l->setProperty ("slotX", slotX);
    l->setProperty ("notchX", notchX);
    l->setProperty ("parkY", slotTopY);
    juce::Array<juce::var> gearsAt, names;
    for (int g = 0; g < lever.numGears(); ++g)
        gearsAt.add (stops[(size_t) g]);
    for (int at = 0; at <= lever.numGears(); ++at)
        names.add (GearLever::nameOf (at, lever.numGears()));
    l->setProperty ("notches", gearsAt);     // where the gears stand: the notches the face shows
    l->setProperty ("names", names);      // PARK first, then the gears from the top
    l->setProperty ("leverWidth", leverWidth);
    l->setProperty ("collarSink", collarSink);
    l->setProperty ("gripBar", GearLever::barBottom);       // the render's top part the hand takes it by
    l->setProperty ("leverBox", point ({ leverLeft, leverTop }));
    l->setProperty ("plate", rect (gearPlate));
    l->setProperty ("glowPad", Art::leverGlowPad);
    o->setProperty ("lever", juce::var (l));

    auto* t = new juce::DynamicObject();
    t->setProperty ("centre", point (layout::turbo));
    t->setProperty ("size", turboSize);
    t->setProperty ("name", point (turboName));
    t->setProperty ("lid", rect (turboLid));
    t->setProperty ("kind", "pair");      // TURBO walks to the gear's paired channel (channels::Lineup::pairs)
    // ...and parked: the button deaf, drawn at `alpha` at once; and where the rover has a hatch's render,
    // the hatch shut over it on its spring (TurboLid), open again when a gear is pulled.
    auto* atPark = new juce::DynamicObject();
    atPark->setProperty ("alpha", face::PushButton::dimmed);
    atPark->setProperty ("lid", Art::get().turboLid.isValid() ? juce::var ("turbo-lid.png") : juce::var());
    auto* spring = new juce::DynamicObject();
    spring->setProperty ("stiffness", TurboLid::stiffness);
    spring->setProperty ("damping", TurboLid::damping);
    atPark->setProperty ("lidSpring", juce::var (spring));
    t->setProperty ("parked", juce::var (atPark));
    o->setProperty ("turbo", juce::var (t));

    juce::Array<juce::var> wipers;
    for (const auto& k : wiperKeys)
        wipers.add (point (k));
    o->setProperty ("wiperKeys", wiperKeySize > 0.0f ? juce::var (wipers) : juce::var());
    o->setProperty ("wiperKeySize", wiperKeySize);
    o->setProperty ("gears", lever.numGears());
    for (const auto& p : o->getProperties())       // all of it in places.json's "world", which is all the site reads...
        world.setProperty (p.name, p.value);
    dir.getChildFile ("rover.json").replaceWithText (juce::JSON::toString (juce::var (o)));     // ...and on its own (the var takes `o`)

    places.setProperty ("window", rect (windscreen));

    auto* keyRenders = new juce::DynamicObject();      // the keys and the roof's toggles are renders, up and down
    keyRenders->setProperty ("up", "key-up.png");
    keyRenders->setProperty ("down", "key-down.png");
    keyRenders->setProperty ("toggleUp", "toggle-up.png");
    keyRenders->setProperty ("toggleDown", "toggle-down.png");
    world.setProperty ("keys", juce::var (keyRenders));
    const auto colour = [] (juce::Colour c) { return juce::String::formatted ("#%02x%02x%02x%02x", c.getRed(), c.getGreen(), c.getBlue(), c.getAlpha()); };

    // THE RINGS ROUND THE KNOBS (KnobRing): each socket's ring, and how it is drawn.
    const auto socket = [&rect, &point] (const Socket& s)
    {
        auto* x = new juce::DynamicObject();
        x->setProperty ("centre", point (s.centre)); x->setProperty ("radius", s.ring);
        x->setProperty ("width", s.ringWidth); x->setProperty ("knob", s.knob);
        juce::ignoreUnused (rect);
        return juce::var (x);
    };
    auto* ring = new juce::DynamicObject();
    ring->setProperty ("drive", socket (layout::drive));
    juce::Array<juce::var> toneRings;
    for (const auto& t : layout::tones)
        toneRings.add (socket (t));
    ring->setProperty ("tones", toneRings);
    ring->setProperty ("from", KnobRing::from);
    ring->setProperty ("sweep", KnobRing::sweep);
    ring->setProperty ("light", colour (skin().ringLight));
    ring->setProperty ("veil", colour (juce::Colour (KnobRing::veil)));
    ring->setProperty ("veilAlpha", KnobRing::veilAlpha);
    ring->setProperty ("veilWider", KnobRing::veilWider);
    ring->setProperty ("litAlpha", KnobRing::litAlpha);
    ring->setProperty ("litAlphaHand", KnobRing::litAlphaHand);
    ring->setProperty ("glowAlpha", KnobRing::glowAlpha);
    ring->setProperty ("glowAlphaHand", KnobRing::glowAlphaHand);
    ring->setProperty ("glowRadius", KnobRing::glowRadius);
    ring->setProperty ("glowRadiusHand", KnobRing::glowRadiusHand);
    ring->setProperty ("rimOfKnob", KnobRing::rimOfKnob);
    ring->setProperty ("rimAlpha", KnobRing::rimAlpha);
    world.setProperty ("knobRing", juce::var (ring));

    // THE KNOBS' NAMES over their sockets, and what DRIVE is called on this face.
    auto* knobNames = new juce::DynamicObject();
    knobNames->setProperty ("drive", skin().driveName);
    knobNames->setProperty ("driveY", driveNameY);
    knobNames->setProperty ("tonesY", knobNameY);
    knobNames->setProperty ("px", knobNameEm);
    knobNames->setProperty ("shadow", colour (skin().printShadow));
    world.setProperty ("knobNames", juce::var (knobNames));

    // THE KEYS' NAMES: over or under each key by turns, condensed into `room` where wider — or, where the
    // picture leaves no room round them, the filters' on their caps (condensed into 0.62 of the cap, lit in
    // the state's ink, else the words' at 0.75) and the layers' none until the packs carry them.
    auto* keyNames = new juce::DynamicObject();
    keyNames->setProperty ("onKeys", keyNamesOnKeys);
    keyNames->setProperty ("overY", keyNameOverY);
    keyNames->setProperty ("underY", keyNameUnderY);
    juce::Array<juce::var> over;
    for (const bool b : keyNameOver)
        over.add (b);
    keyNames->setProperty ("over", over);
    keyNames->setProperty ("room", keyNameRoom);
    keyNames->setProperty ("px", px11);
    keyNames->setProperty ("capPx", capEm);
    keyNames->setProperty ("capRoom", 0.62f);
    keyNames->setProperty ("tracking", face::keyTracking);
    keyNames->setProperty ("dimmed", face::PushButton::dimmed);
    world.setProperty ("keyNames", juce::var (keyNames));

    // THE KEYS THE WORLD TAKES FOR ITSELF: the searchlights, where it has lights to throw — each a latching
    // key with a lamp's sign on its cap (drawLampSign: lit in the state's ink, else the words' at 0.75),
    // kept as the machine's choice and thrown to the view (Drive::setLights); and the wipers' keys.
    juce::Array<juce::var> claimed;
    if (lightKeys[0] >= 0)
        for (int lamp = 0; lamp < 2; ++lamp)
        {
            auto* k = new juce::DynamicObject();
            k->setProperty ("key", lightKeys[(size_t) lamp]);
            k->setProperty ("name", lamp == 0 ? "SEARCHLIGHT LEFT" : "SEARCHLIGHT RIGHT");
            k->setProperty ("does", lamp == 0 ? "lights.left" : "lights.right");
            k->setProperty ("latching", true);
            k->setProperty ("sign", lamp == 0 ? "lamp-left" : "lamp-right");
            claimed.add (juce::var (k));
        }
    world.setProperty ("claimedKeys", claimed);
    if (wiperKeySize > 0.0f)
    {
        auto* w = new juce::DynamicObject();
        w->setProperty ("names", juce::StringArray { "OFF", "INT", "LO", "HI" });
        w->setProperty ("opening", 1);
        w->setProperty ("does", "wipers (Drive::setWipers 0..3)");
        world.setProperty ("wipers", juce::var (w));
    }

    // THE SPEEDS the gears drive at, km/h as the nav screen reads them once eased: each gear, then each
    // with TURBO; parked, none.
    juce::Array<juce::var> kmh, turboKmh;
    for (int g = 1; g <= lever.numGears(); ++g)
    {
        kmh.add (view->drive().speedFor (g, false) * 3.6f);
        turboKmh.add (view->drive().speedFor (g, true) * 3.6f);
    }
    auto* speeds = new juce::DynamicObject();
    speeds->setProperty ("kmh", kmh);
    speeds->setProperty ("turboKmh", turboKmh);

    // ...and how the speed is eased to them, time constants in seconds: the nav reads the view it shows
    // (Windscreen::speedKmh) — the GPU's world eases its own; the 1993 landscape its pace, read against the
    // gear asked now; the still none, what is asked at once.
    auto* ease = new juce::DynamicObject();
    {
        const auto e = view->drive().ease();
        auto* gpu = new juce::DynamicObject();
        gpu->setProperty ("tau", e.drive);
        gpu->setProperty ("parkTau", e.park);
        ease->setProperty ("gpu", juce::var (gpu));
        auto* cpu = new juce::DynamicObject();
        cpu->setProperty ("tau", Windscreen::speedSeconds1993);
        cpu->setProperty ("parkTau", PlainsWorld::parkSeconds);
        juce::Array<juce::var> pace;
        const auto& table = Windscreen::texelsPerSecond1993;
        for (int g = 1; g <= lever.numGears(); ++g)
            pace.add (table[(size_t) juce::jmin (g, (int) table.size()) - 1]);
        cpu->setProperty ("texelsPerSecond", pace);
        cpu->setProperty ("turboFactor", PlainsWorld::turboFactor);
        cpu->setProperty ("reading", "km/h = pace / texelsPerSecond[the gear asked now] * kmh[the gear asked now]");
        ease->setProperty ("view1993", juce::var (cpu));
        auto* still = new juce::DynamicObject();
        still->setProperty ("tau", 0.0);
        still->setProperty ("parkTau", 0.0);
        ease->setProperty ("still", juce::var (still));
    }
    speeds->setProperty ("ease", juce::var (ease));
    world.setProperty ("speeds", juce::var (speeds));

    // THE NAV SCREEN: its boxes, how its top line is set (NavHeader::paint — sizes in the stage's units,
    // a font's size its height as JUCE counts it, ascent and descent), what the skin puts on it, and what
    // the line shows at each place of the lever with each channel there — the channel cut with an
    // ellipsis where it is wider than its room.
    auto* nav = new juce::DynamicObject();
    nav->setProperty ("screen", rect (navScreen));
    nav->setProperty ("header", rect (navHeader));
    nav->setProperty ("picture", rect (navPicture));
    nav->setProperty ("speedHud", speedHud.isEmpty() ? juce::var() : rect (speedHud));
    nav->setProperty ("learn", rect (learnArea));
    nav->setProperty ("packOnScreen", skin().packOnScreen);
    nav->setProperty ("firstScreen", Monitor::names()[(int) skin().firstScreen]);
    nav->setProperty ("parkSignOnTopLine", skin().parkSignOnTopLine);

    // SHAPE, the screen's first picture, as the screen draws it — edge to edge across `picture`, and not
    // fading in a pause.
    nav->setProperty ("shape", screen.shapeForWeb());

    // PARKED, the picture goes at once and the screen is the large warning's (ParkSign::paint): darkened,
    // the red triangle in its middle.
    auto* parkedScreen = new juce::DynamicObject();
    parkedScreen->setProperty ("picture", "hidden at once, no fade");
    parkedScreen->setProperty ("sign", "ParkSign::paint (src/redpaw/RedPawControls.cpp): the picture box darkened, the warning centred in it, glowing");
    {
        // ...and in numbers: the box H the picture's height; the triangle's height min (max (H - margin,
        // atLeast x H), atMost), its width widthOfHeight x that; the mark in twentieths of the triangle's
        // height, k = height / unit.
        auto* data = new juce::DynamicObject();
        auto* dim = new juce::DynamicObject();
        dim->setProperty ("colour", colour (juce::Colour (ParkSign::dim)));
        dim->setProperty ("alpha", ParkSign::dimAlpha);
        data->setProperty ("dim", juce::var (dim));
        auto* tri = new juce::DynamicObject();
        tri->setProperty ("margin", ParkSign::margin);
        tri->setProperty ("atLeast", ParkSign::atLeast);
        tri->setProperty ("atMost", ParkSign::atMost);
        tri->setProperty ("widthOfHeight", ParkSign::widthOfHeight);
        tri->setProperty ("colour", colour (juce::Colour (Warning::red)));
        tri->setProperty ("apex", "top centre; base along the bottom, corner to corner");
        tri->setProperty ("cornerRadius", Warning::corner);           // in k
        data->setProperty ("triangle", juce::var (tri));
        auto* mark = new juce::DynamicObject();
        mark->setProperty ("unit", Warning::unit);
        mark->setProperty ("colour", colour (juce::Colour (Warning::cut)));
        mark->setProperty ("barWidth", Warning::barWidth);
        mark->setProperty ("barHeight", Warning::barHeight);
        mark->setProperty ("barRadius", Warning::barRadius);
        mark->setProperty ("barCentreOfHeight", Warning::barCentre);   // from the triangle's top
        mark->setProperty ("dot", Warning::dot);
        mark->setProperty ("dotUp", Warning::dotUp);                  // its middle over the base, in k
        data->setProperty ("mark", juce::var (mark));
        auto* glow = new juce::DynamicObject();
        glow->setProperty ("colour", colour (juce::Colour (Warning::red)));
        glow->setProperty ("alpha", ParkSign::glowAlpha);
        glow->setProperty ("radius", ParkSign::glowRadius);
        data->setProperty ("glow", juce::var (glow));
        parkedScreen->setProperty ("signData", juce::var (data));
    }
    nav->setProperty ("parked", juce::var (parkedScreen));
    auto* type = new juce::DynamicObject();
    type->setProperty ("gearPx", px15);
    type->setProperty ("channelPx", px13);
    type->setProperty ("speedPx", px15);
    type->setProperty ("unitPx", px13);
    type->setProperty ("unit", "KM/H");
    type->setProperty ("kerning", 0.04f);
    type->setProperty ("insetX", 8.0f);
    type->setProperty ("warning", rect ({ 0.0f, 0.0f, 23.0f, 20.0f }));
    type->setProperty ("afterWarning", 8.0f);
    type->setProperty ("afterGear", 10.0f);
    type->setProperty ("beforeSpeed", 8.0f);
    type->setProperty ("beforeUnit", 6.0f);
    type->setProperty ("widthPad", 2.0f);       // every word's measured width + 2
    type->setProperty ("gearColour", colour (orange));
    type->setProperty ("wordsColour", colour (juce::Colour (0xffece9f6)));
    type->setProperty ("unitColour", colour (labelLilac.withAlpha (0.8f)));
    nav->setProperty ("type", juce::var (type));
    juce::Array<juce::var> lines;
    int topKmh = 0;
    for (int g = 1; g <= lever.numGears(); ++g)
        topKmh = juce::jmax (topKmh, juce::roundToInt (view->drive().speedFor (g, true) * 3.6f));
    const auto line = [&] (int at, int channel)
    {
        const bool parked = at == GearLever::park;
        const auto gearName = GearLever::nameOf (at, lever.numGears());
        const auto name = skin().packOnScreen ? juce::String (lineup().channels[(size_t) channel].name).toUpperCase() : juce::String();
        const int speed = parked ? 0 : juce::roundToInt (view->drive().speedFor (gearOf (channel) + 1, turboOf (channel)) * 3.6f);
        const auto f = header.fit (gearName, name, speed, parked);
        auto* x = new juce::DynamicObject();
        x->setProperty ("place", at);
        x->setProperty ("channel", channel);
        x->setProperty ("turbo", turboOf (channel));
        x->setProperty ("gear", gearName);
        x->setProperty ("text", name);
        x->setProperty ("kmh", speed);
        x->setProperty ("wanted", f.wanted);
        x->setProperty ("room", f.room);
        x->setProperty ("lines", f.lines);
        x->setProperty ("shown", f.shown);

        // ...and what it shows while the speed is eased there, since the number takes room from the
        // channel: [from km/h, shown] wherever that changes, from a stand to the world's top speed.
        juce::Array<juce::var> byKmh;
        juce::String was;
        for (int k = 0; k <= topKmh; ++k)
            if (const auto s = header.fit (gearName, name, k, parked).shown; k == 0 || s != was)
            {
                byKmh.add (juce::var (juce::Array<juce::var> { k, s }));
                was = s;
            }
        x->setProperty ("shownByKmh", byKmh);
        lines.add (juce::var (x));
    };
    for (int c = 0; c < (int) lineup().channels.size(); ++c)
        line (GearLever::positionOf (gearOf (c)), c);
    for (int c = 0; c < (int) lineup().channels.size(); ++c)
        line (GearLever::park, c);
    nav->setProperty ("lines", lines);
    world.setProperty ("nav", juce::var (nav));

    juce::Array<juce::var> stills;
    stills.add ("still.jpg");
    places.setProperty ("stills", stills);
    writePoster (dir, windscreen, Art::still(), { view.get() });

    // ...and one for each place of the lever, PARK first: the hatch shut there only, TURBO off.
    webPoses.count = lever.numPositions();
    webPoses.standAt = [this] (int at)
    {
        if (at == GearLever::park)
            parkIt();
        else
            pullGear (at - GearLever::positionOf (0));
        lever.setPosition (at, false, false);
        hatch.setShut (at == GearLever::park, false);
    };
    webPoses.still = [] (int) { return Art::still(); };
    webPoses.window = windscreen;
    webPoses.hidden = { view.get() };
    webPatch ("gateFader", { &threshold });
    webPatch ("limiterFader", { &ceiling });
    webPatch ("nav", { &header, &screen, &parkSign, &glassFilm });
    webPatch ("learn", { &learnKey, &humKey });
    std::vector<const juce::Component*> wiperKeys;
    juce::Rectangle<float> wipersBox;
    for (const auto& k : wiperButtons)
        if (k != nullptr && k->isVisible())
            wipersBox = wipersBox.isEmpty() ? k->getBoundsInParent().toFloat() : wipersBox.getUnion (k->getBoundsInParent().toFloat());
    if (! wipersBox.isEmpty())
        webPatches.push_back ({ "wipers", wipersBox });
}

RedPawEditor::RedPawEditor (AmpProcessor& p)
    : face::Console (p, std::make_unique<Dashboard>(), dashboardPlaces(), dashboardLook()),
      view (skin().makeView()),
      lever (numGears()),
      screen (p)
{
    using namespace layout;

    // Everything this constructor sets is the face settling onto the state, not a player's edit.
    const felitronics::appkit::CompareHistory::ScopedSuppress settling (amp.history);

    // THE ROOF'S KEYS ARE TOGGLES: thrown up while a history key is pressed, and up on the register
    // that is playing.
    for (int i = 0; i < 6; ++i)
        roofKey (i).drawKey = [&k = roofKey (i)] (juce::Graphics& g, juce::Rectangle<float> box, bool down, float)
        {
            if (k.isMouseOverOrDragging())
            {
                juce::Path hole;
                hole.addEllipse (juce::Rectangle<float> (layout::holeRadius * 2.0f, layout::holeRadius * 2.0f).withCentre (box.getCentre()));
                drawHandRim (g, hole, handLight);
            }
            drawToggle (g, box, down);
        };

    // ...the six square keys, and TURBO, are the dashboard's own buttons, up or pressed — each with the
    // hand's orange rim round its face while the hand is over it, like everything else that moves; the
    // filters' names on their faces where the layout puts the names there.
    for (int i = 0; i < 6; ++i)
        key (i).drawKey = [&k = key (i), i] (juce::Graphics& g, juce::Rectangle<float> box, bool down, float)
        {
            const auto& img = down ? Art::get().keyDown : Art::get().keyUp;
            drawRender (g, img, box);
            if (layout::keyNamesOnKeys && i <= btLpf)
                capWord (g, switches[(size_t) i].shortName, fittedArea (img, box), down);
            for (int lamp = 0; lamp < 2; ++lamp)
                if (layout::lightKeys[(size_t) lamp] == i)
                    drawLampSign (g, fittedArea (img, box), lamp == 0,
                                  down ? skin().ink.state : skin().ink.at (skin().ink.words, 0.75f));
            if (k.isMouseOverOrDragging() && k.isEnabled())
            {
                juce::Path face;
                face.addRoundedRectangle (fittedArea (img, box).reduced (2.0f), 4.0f);
                drawHandRim (g, face, handLight);
            }
        };

    // THE RINGS ROUND THE KNOBS say where each stands.
    const auto ringFor = [] (const ArtKnob& knob, const layout::Socket& s) { return std::make_unique<KnobRing> (knob, s.centre, s.ring, s.ringWidth); };
    rings.push_back (ringFor (driveKnob(), layout::drive));
    for (int t = 0; t < 3; ++t)
        rings.push_back (ringFor (toneKnob (t), layout::tones[(size_t) t]));
    for (auto& ring : rings)
        addToStage (*ring);

    // THE WINDSCREEN, under the dashboard: the picture's alpha is its frame.
    stage.addAndMakeVisible (*view, 0);
    view->setBounds (windscreen.toNearestInt());
    view->addMouseListener (&glassClicks, false);
    glassClicks.menu = [this] { showWindowMenu(); };

    // THE GEAR LEVER: the pedal, and everything its channel was left with. Its box is the gate's plate
    // and round it as far as the grip, its light and its shadow reach.
    addToStage (lever);
    const float leverRight = juce::jmax (gearPlate.getRight(), notchX + leverWidth * 0.5f + 16.0f);
    lever.setBounds (juce::Rectangle<float> (leverLeft, leverTop, leverRight - leverLeft, gearPlate.getBottom() - leverTop).toNearestInt());
    lever.onPosition = [this] (int at) { if (at == GearLever::park) parkIt(); else pullGear (at - GearLever::positionOf (0)); };
    lever.onRelease  = [this] { showChannel(); };  // held, it follows the hand; let go, it follows the state

    // PARK may move with no hand on the lever: a host's automation, an undo.
    follow (engine.parkId(), [this] (float) { showChannel(); });

    // TURBO: the pedal's second channel, on or off — one switch for every gear.
    turbo.latching = false;
    turbo.drawKey = [this] (juce::Graphics& g, juce::Rectangle<float> box, bool down, float)
    {
        const auto& img = down ? Art::get().turboDown : Art::get().turboUp;
        drawRender (g, img, box);
        if (turbo.isMouseOverOrDragging() && turbo.isEnabled())
        {
            juce::Path face;
            face.addEllipse (fittedArea (img, box).reduced (2.0f));
            drawHandRim (g, face, handLight);
        }
    };
    turbo.onPress = [this] { throwTurbo (! turboOf (channels.active())); };
    addToStage (turbo);
    turbo.setBounds (juce::Rectangle<float> (turboSize, turboSize).withCentre (layout::turbo).toNearestInt());
    addToStage (hatch);         // after the button, so over it
    hatch.setBounds (layout::turboLid.getSmallestIntegerContainer());

    // GATE'S THRESHOLD AND LIMITER'S CEILING, each on a short fader down its lamp plate's outer leg, its
    // cap lighter than the columns' so a small one reads in its corner. A double click puts it back where
    // it opens, a right click is its block's menu, and while it is held the tag over it says where it
    // stands. While its block is off it stands dim, and still moves.
    const auto plateFader = [this] (face::ColumnFader& fader, const LampPlate& plate, const char* paramId, const char* onId,
                                    float low, float high, std::function<void()> menu, const juce::String& name)
    {
        const auto travelOf = [low, high] (float v) { return (v - low) / (high - low); };
        fader.scale = slotScale;
        fader.capLength = faderCapLength * slotScale;
        fader.handLight = handLight;
        fader.defaultValue = travelOf (amp.apvts.getParameter (paramId)->convertFrom0to1 (amp.apvts.getParameter (paramId)->getDefaultValue()));
        auto* attachment = follow (paramId, [&fader, travelOf] (float v) { fader.setValue (travelOf (v), false); });
        fader.onGestureStart = [attachment] { attachment->beginGesture(); };
        fader.onChange       = [attachment, low, high] (float travel) { attachment->setValueAsPartOfGesture (juce::jmap (travel, low, high)); };
        fader.onGestureEnd   = [attachment] { attachment->endGesture(); };
        fader.onMenu         = std::move (menu);
        attachment->sendInitialUpdate();
        sayLever (fader, name, [this, paramId] { return getParam (paramId); });
        follow (onId, [&fader] (float on) { fader.setAlpha (on > 0.5f ? 1.0f : 0.6f); })->sendInitialUpdate();
        addToStage (fader);
        const bool across = plate.slot.getWidth() > plate.slot.getHeight();     // a slot lying across: the fader runs left to right
        const auto run = plate.slot.reduced (across ? slotRim : 0.0f, across ? 0.0f : slotRim);
        const float breadth = faderWidth * slotScale;
        fader.setAcross (across);
        fader.setBounds ((across ? run.withSizeKeepingCentre (run.getWidth(), breadth)
                                 : run.withSizeKeepingCentre (breadth, run.getHeight())).toNearestInt());
    };
    threshold.marks = 6;            // every 5 dB, the longer at -50
    threshold.longMark = 4;
    plateFader (threshold, gatePlate, params::gateThreshold, params::gateOn, thresholdLowDb, thresholdHighDb,
                [this] { showGateMenu(); }, "GATE");
    ceiling.marks = 6;              // every half a decibel or so, none longer
    ceiling.longMark = -1;
    plateFader (ceiling, limiterPlate, params::limiterCeiling, params::limiterOn, params::limiterCeilingMin, params::limiterCeilingMax,
                [this] { showOutputMenu(); }, "LIMITER");

    // THE NAV SCREEN: along its top the gear, the channel where the skin names it, and the speed the
    // windscreen is driving at; under them the picture of the amp at work the machine has chosen, the
    // skin's until then.
    header.speed = [this] { return view->speedKmh(); };
    header.showsWarning = skin().parkSignOnTopLine;
    addToStage (header);
    header.setBounds (navHeader.toNearestInt());
    if (! speedHud.isEmpty())       // ...or the speed on the glass, the screen's top line the gear alone
    {
        hud.speed = header.speed;
        header.showsSpeed = false;
        addToStage (hud);
        hud.setBounds (speedHud.toNearestInt());
    }
    addToStage (screen);
    screen.setBounds (navPicture.toNearestInt());
    screen.show ((Monitor::View) juce::jlimit (1, (int) Monitor::View::numViews - 1,
                                               (int) prefs::store().get (screenKey, (int) skin().firstScreen)));

    // LEARN, from the menus or a key of its own, takes the picture's place while it runs — the parked
    // warning's too, and the top line's where its place takes that in (RedPawLayout.h) — and lights its key.
    addToStage (learnGlass);
    learnGlass.setBounds (learnArea.toNearestInt());
    learnGlass.inset = { 8.0f, 4.0f };      // the top line's words start where the header's do
    learnGlass.onShow = [this] (bool up)
    {
        screen.setAlpha (up ? 0.0f : 1.0f);
        parkSign.setAlpha (up ? 0.0f : 1.0f);
        if (learnArea.intersects (navHeader))
            header.setAlpha (up ? 0.0f : 1.0f);
        learnShowing = up ? (learnGlass.showsHum() ? 1 : 0) : -1;
        learnKey.repaint();
        humKey.repaint();
    };

    if (learnKeySize > 0.0f)
        for (int k = 0; k < 2; ++k)
        {
            auto& key = k == 0 ? learnKey : humKey;
            key.latching = false;
            key.drawKey = [this, &key, k] (juce::Graphics& g, juce::Rectangle<float> box, bool down, float)
            {
                const auto& img = down || learnShowing == k ? Art::get().turboDown : Art::get().turboUp;
                drawRender (g, img, box);
                if (key.isMouseOverOrDragging() && key.isEnabled())
                {
                    juce::Path face;
                    face.addEllipse (fittedArea (img, box).reduced (2.0f));
                    drawHandRim (g, face, handLight);
                }
            };
            key.onPress = [this, k] { k == 0 ? startGateLearn() : startHumLearn(); };
            addToStage (key);
            key.setBounds (juce::Rectangle<float> (learnKeySize, learnKeySize).withCentre (learnKeys[(size_t) k]).toNearestInt());
        }

    // THE SEARCHLIGHTS on two of the six keys, where the layout gives them some: each key a lamp, lit
    // while it burns, kept as the machine's choice.
    if (lightKeys[0] >= 0)
    {
        const int kept = (int) prefs::store().get (lightsKey, 0);
        const auto throwLights = [this]
        {
            const bool l = key (lightKeys[0]).isOn(), r = key (lightKeys[1]).isOn();
            prefs::store().set (lightsKey, (l ? 1 : 0) | (r ? 2 : 0));
            view->setLights (l, r);
        };
        for (int lamp = 0; lamp < 2; ++lamp)
        {
            auto& k = key (lightKeys[(size_t) lamp]);
            claimKey (lightKeys[(size_t) lamp], lamp == 0 ? "SEARCHLIGHT LEFT" : "SEARCHLIGHT RIGHT");
            k.latching = true;
            k.setOn ((kept & (1 << lamp)) != 0, false);
            k.onChange = [throwLights] (bool) { throwLights(); };
        }
        view->setLights ((kept & 1) != 0, (kept & 2) != 0);
    }

    // THE WIPERS' SPEED, where the picture paints keys for it: OFF, INT, LO, HI, the speed set lit.
    if (wiperKeySize > 0.0f)
    {
        wipers = juce::jlimit (0, 3, (int) prefs::store().get (wipersKey, 1));
        const char* words[] { "OFF", "INT", "LO", "HI" };
        for (int i = 0; i < 4; ++i)
        {
            auto& k = *wiperButtons.emplace_back (std::make_unique<PushButton> (words[i]));
            k.latching = false;
            k.drawKey = [this, &k, i] (juce::Graphics& g, juce::Rectangle<float> box, bool down, float)
            {
                const bool lit = down || wipers == i;
                const auto& img = lit ? Art::get().keyDown : Art::get().keyUp;
                drawRender (g, img, box);
                capWord (g, k.name, fittedArea (img, box), lit);
                if (k.isMouseOverOrDragging())
                {
                    juce::Path face;
                    face.addRoundedRectangle (fittedArea (img, box).reduced (2.0f), 4.0f);
                    drawHandRim (g, face, handLight);
                }
            };
            k.onPress = [this, i] { chooseWipers (i); };
            addToStage (k);
            k.setBounds (juce::Rectangle<float> (wiperKeySize, wiperKeySize).withCentre (layout::wiperKeys[(size_t) i]).toNearestInt());
        }
        view->setWipers (wipers);
    }

    // Parked, the warning stands large in the middle of the screen, over the picture dimmed.
    addToStage (parkSign);
    parkSign.setVisible (false);      // showSelector says when
    parkSign.setBounds (navPicture.toNearestInt());

    // ...and its glass, over the lot: a little murky, dusty, lightly scratched — and the tuner's strip,
    // where the tuner is not a dial of its own.
    addToStage (glassFilm);
    glassFilm.setBounds ((tunerDial.reach > 0.0f ? navHeader.getUnion (navPicture) : navHeader.getUnion (tunerScreen)).toNearestInt());

    // A prototype's hook for looking at the windscreen in a gear, with TURBO, or parked, without a hand
    // on anything: ROVER_CHANNEL names a channel, from 0 to the product's last, or "park".
    if (const auto c = juce::SystemStats::getEnvironmentVariable ("ROVER_CHANNEL", {}); c.equalsIgnoreCase ("park"))
        setParam (engine.parkId(), 1.0f);
    else if (c.isNotEmpty())
        engine.go (c.getIntValue());

    showChannel();

    // What the windscreen shows: the machine's choice, the Plains unless it has said otherwise — drawn
    // by this window's context, which paints the face over it.
    view->setHost (&gl);
    gl.setOpenGLVersionRequired (juce::OpenGLContext::openGL3_2);
    gl.setRenderer (this);
    gl.setComponentPaintingEnabled (true);
    gl.setContinuousRepainting (false);     // the view asks for each frame, at its own pace
    gl.attachTo (*this);
    view->setFrameRateCap ((int) prefs::store().get (frameRateKey, 60));
    view->setPicture ([] { return Art::still(); });     // STILL is the rover's own picture, where it ships one
    view->setMode ((Windscreen::Mode) juce::jmax (0, landscapeWords.indexOf (prefs::store().get (landscapeKey, landscapeWords[0]).toString())));
    placeView();
}

RedPawEditor::~RedPawEditor()
{
    view->removeMouseListener (&glassClicks);
    gl.detach();
}

bool RedPawEditor::isParked() const
{
    return getParam (engine.parkId()) > 0.5f;
}

/** A gear pulled: that pedal, TURBO as it stands — and out of PARK, in the same step of the history. */
void RedPawEditor::pullGear (int gear)
{
    if (isParked())
        setParam (engine.parkId(), 0.0f);
    go (channels.leftOn (channelOf (gear, false)));     // TURBO as the gear was left with it
}

void RedPawEditor::throwTurbo (bool on)
{
    if (! isParked())
        go (channelOf (gearOf (channels.active()), on));
}

void RedPawEditor::parkIt()
{
    endTapping();
    if (! isParked())
        setParam (engine.parkId(), 1.0f);
    showChannel();
}

void RedPawEditor::showSelector (int channel)
{
    const int gear = gearOf (channel);
    const bool turboOn = turboOf (channel);
    const bool parked = isParked();
    const int at = parked ? GearLever::park : GearLever::positionOf (gear);

    if (! lever.isHeld() && lever.getPosition() != at)
        lever.setPosition (at, true, false);
    turbo.setOn (turboOn, false);
    turbo.setEnabled (! parked);
    hatch.setShut (parked, isShowing());

    header.set (GearLever::nameOf (at, lever.numGears()),
                skin().packOnScreen ? juce::String (lineup().channels[(size_t) channel].name).toUpperCase() : juce::String(), parked);

    view->setGear (gear + 1);
    view->setTurbo (turboOn);
    view->setParked (parked);
    // Parked, the amp is out of the sound and its picture with it: the screen dark but for the large
    // warning.
    screen.setVisible (! parked);
    parkSign.setVisible (parked);
}

/** 1, 2, ... as many as the gears: the lever goes to that gear, as if a hand had put it there; 0 or P to
    PARK; T throws TURBO.
    The keys are the same whatever the layout prints on them — under a Russian one T arrives as Е, and
    P as З. */
bool RedPawEditor::channelKey (const juce::KeyPress& key)
{
    const auto letter = juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) key.getKeyCode());
    const int at = letter == '0' || letter == 'P' || letter == 0x0417 ? GearLever::park
                 : letter >= '1' && letter < '1' + lever.numGears() ? GearLever::positionOf ((int) (letter - '1'))
                 : -1;
    if (at >= 0)
    {
        if (! lever.isHeld())
            lever.setPosition (at, true);
        return true;
    }

    if (letter == 'T' || letter == 0x0415)
    {
        throwTurbo (! turboOf (channels.active()));
        return true;
    }
    return false;
}

/** Programs 1 to the number of channels are the channels, out of PARK; the one after them is PARK. */
void RedPawEditor::programChange (int program)
{
    if (program == channels.count())
    {
        parkIt();
    }
    else if (program >= 0 && program < channels.count())
    {
        if (isParked())
            setParam (engine.parkId(), 0.0f);
        go (program);
    }
}

/** TURBO's controller: the pedal's second channel, or its first — nothing, parked. */
void RedPawEditor::turboChange (bool thrown)
{
    if (thrown != turboOf (channels.active()))
        throwTurbo (thrown);
}

//==============================================================================
/** What the windscreen shows — the Plains on the GPU, the 1993 landscape on the CPU, or a still — and
    how often; and full screen. The machine's, kept with its settings. */
void RedPawEditor::addWindowSections (juce::PopupMenu& menu)
{
    const auto self = [] (Console& e) -> RedPawEditor& { return static_cast<RedPawEditor&> (e); };

    if (! amp.runsInHost())
    {
        menu.addSectionHeader ("WINDOW");
        addChoice (menu, "Full screen", fullScreen, [self] (Console& e) { self (e).setFullScreen (! self (e).fullScreen); });
    }

    // Where the GPU's landscape cannot be had the CPU's drives instead, and the tick says what runs.
    menu.addSectionHeader ("LANDSCAPE");
    const auto running = view->mode();
    const auto& landscapeNames = skin().landscapeNames;
    for (int i = 0; i < landscapeNames.size(); ++i)
        addChoice (menu, landscapeNames[i], (int) running == i,
                   [self, i] (Console& e) { self (e).chooseLandscape ((Windscreen::Mode) i); },
                   i != (int) Windscreen::Mode::gpu || ! view->status().gpuBroken);
    for (const int fps : { 60, 30 })
        addChoice (menu, juce::String (fps) + " frames a second", view->frameRateCap() == fps,
                   [self, fps] (Console& e) { self (e).chooseFrameRate (fps); }, running != Windscreen::Mode::still);
}

/** A RIGHT CLICK ON THE NAV SCREEN — anywhere over its picture or its top line — offers its five
    pictures; the tuner's strip under them has a menu of its own. */
bool RedPawEditor::pictureRightClicked (juce::Point<int> at)
{
    if (! layout::navHeader.getUnion (layout::navPicture).contains (at.toFloat()))
        return false;
    showScreenMenu();
    return true;
}

void RedPawEditor::showScreenMenu()
{
    endTapping();

    const auto self = [] (Console& e) -> RedPawEditor& { return static_cast<RedPawEditor&> (e); };
    juce::PopupMenu menu;
    menu.addSectionHeader ("SCREEN");
    for (int i = (int) Monitor::View::spectrum; i < (int) Monitor::View::numViews; ++i)
        addChoice (menu, Monitor::names()[i], (int) screen.shown() == i,
                   [self, i] (Console& e) { self (e).chooseScreen ((Monitor::View) i); });
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

void RedPawEditor::chooseScreen (Monitor::View v)
{
    prefs::store().set (screenKey, (int) v);
    screen.show (v);
}

void RedPawEditor::chooseWipers (int speed)
{
    wipers = speed;
    prefs::store().set (wipersKey, speed);
    view->setWipers (speed);
    for (auto& k : wiperButtons)
        k->repaint();
}

void RedPawEditor::showWindowMenu()
{
    endTapping();

    juce::PopupMenu menu;
    addWindowSections (menu);
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

void RedPawEditor::chooseLandscape (Windscreen::Mode mode)
{
    prefs::store().set (landscapeKey, landscapeWords[(int) mode]);
    view->setMode (mode);
}

void RedPawEditor::chooseFrameRate (int fps)
{
    prefs::store().set (frameRateKey, fps);
    view->setFrameRateCap (fps);
}

/** Where the windscreen is in the editor's own pixels, for the GL thread: the stage's scale and place. */
void RedPawEditor::placeView()
{
    const auto at = (layout::windscreen * stageScale() + stageOrigin()).toNearestInt();
    glassX.store (at.getX());
    glassY.store (at.getY());
    glassW.store (at.getWidth());
    glassH.store (at.getHeight());
    gl.triggerRepaint();
}

void RedPawEditor::newOpenGLContextCreated()
{
    view->hostContextCreated();
}

void RedPawEditor::renderOpenGL()
{
    const float k = (float) gl.getRenderingScale();
    const int fw = juce::roundToInt ((float) getWidth() * k), fh = juce::roundToInt ((float) getHeight() * k);
    juce::OpenGLHelpers::clear (juce::Colours::black);

    const auto glass = juce::Rectangle<float> ((float) glassX.load(), (float) glassY.load(), (float) glassW.load(), (float) glassH.load()) * k;
    view->hostRender (glass.toNearestInt(), fw, fh);
}

void RedPawEditor::openGLContextClosing()
{
    view->hostContextClosing();
}

} // namespace redpaw
} // namespace orbitamp
