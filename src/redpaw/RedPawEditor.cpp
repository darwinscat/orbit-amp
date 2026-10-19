// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "RedPawEditor.h"

#include "RedPawLayout.h"
#include "RedPawLineup.h"
#include "RedPawSkin.h"

#include "../ui/Prefs.h"

#include "../ui/FactoryMissing.h"

#include <BinaryData.h>

namespace orbitamp::redpaw
{

juce::AudioProcessorEditor* makeEditor (AmpProcessor& p)
{
    // No dashboard to draw without its pictures, which lie in the factory folder: where that is missing,
    // the window says so, and where it belongs (ui/FactoryMissing.h).
    const auto& skin = skinOf (p.product());
    if (! face::renders::has (skin.product, "dashboard.webp", skin.rendersFolderVariable))
        return new ui::FactoryMissingEditor (p, p.product());

    return new RedPawEditor (p);
}

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
    juce::Rectangle<float> wellRow (const Skin& skin, size_t well)
    {
        const auto& w = skin.plan.wells[well];
        return { w.getX(), skin.plan.wellNameY[well] - 15.0f, w.getWidth(), 30.0f };
    }

    /** WHERE THE CONSOLE'S CONTROLS STAND on the dashboard (RedPawLayout.h). Each knob is drawn smaller
        than its cyan ring, which stays in sight round it. */
    Console::Places dashboardPlaces (const Skin& skin)
    {
        using namespace layout;

        Console::Places p;
        p.drive = around (skin.plan.drive.centre, skin.plan.drive.knob, skin.plan.drive.knob);
        p.toneStrip  = skin.plan.toneStrip();       // as many knobs as the pack has tones, where the three stood
        p.toneKnob   = skin.plan.tones[0].knob;
        p.toneNameY  = skin.plan.knobNameY;
        p.toneNameEm = skin.plan.knobNameEm;
        p.toneSignDrop = skin.plan.toneSignDrop;
        p.toneSignHeight = skin.plan.toneSignHeight;

        p.keys = { skin.plan.keys[0], skin.plan.keys[1] };
        p.keySize = skin.plan.keySize;
        p.keyStrip = skin.plan.keyStrip();          // ...and as many keys as it has switches, where the four stood
        p.keyCapLeft = skin.plan.keyCapLeft;
        p.keyCapRight = skin.plan.keyCapRight;
        p.keyFrameHeight = skin.plan.keyFrameHeight;
        p.keyNameOverY = skin.plan.keyNameOverY;
        p.keyNameUnderY = skin.plan.keyNameUnderY;
        p.keyNameRoom = skin.plan.keyNameRoom;
        p.keyNamesAlternate = true;                 // the right group's under, over, under...
        p.keyNamesOnKeys = skin.plan.keyNamesOnKeys;
        p.holes = skin.plan.holes;
        p.holeKeySize = 44.0f;      // the hole's pitch: a toggle up or down stands out of it

        for (size_t i = 0; i < p.sliders.size(); ++i)
        {
            const auto& w = skin.plan.wells[i];
            p.sliders[i] = juce::Rectangle<float> (w.getWidth(), skin.plan.sliderHeight).withCentre (w.getCentre());
            p.sliderNames[i] = wellRow (skin, i);
        }
        p.sliderCapAngle  = 0.0f;   // the caps' renders lie along their wells...
        p.sliderCapLength = skin.plan.sliderCapLength;
        p.faderCapLength  = skin.plan.faderCapLength;     // ...and across their slots

        p.inMeter  = skin.plan.inMeter;
        p.outMeter = skin.plan.outMeter;
        p.meterDots = skin.plan.meterDots;
        p.inFader  = skin.plan.inFader.withSizeKeepingCentre (skin.plan.faderWidth, skin.plan.inFader.getHeight());
        p.outFader = skin.plan.outFader.withSizeKeepingCentre (skin.plan.faderWidth, skin.plan.outFader.getHeight());
        p.tuner = skin.plan.tunerScreen;
        p.tunerStrip = true;            // one low line along the screen's bottom...
        p.tunerDial = skin.plan.tunerDial;        // ...unless the picture paints the tuner a dial of its own...
        p.tunerMenuOnClick = true;      // ...whose settings a click on it opens
        for (size_t i = 0; i < p.lamps.size(); ++i)
            p.lamps[i] = skin.plan.lamps[i];
        p.lampDot = skin.plan.lampDot;

        if (skin.mark && skin.badge.has_value())        // the badge's plate is the emblem then
        {
            p.emblemFace = { skin.plan.badgeFace.begin(), skin.plan.badgeFace.end() };
            p.emblemMark = skin.plan.badgeMark;
        }
        else if (skin.mark)
        {
            p.emblemFace = { skin.plan.emblemFace.begin(), skin.plan.emblemFace.end() };
            p.emblemMark = skin.plan.emblemMark;
        }

        // The bottom plates: PRESET alone on the left, the version in the middle, MEM over CPU on the
        // right.
        p.preset  = skin.plan.leftPlate;
        p.version = skin.plan.centrePlate;
        p.memory  = skin.plan.readings[0];
        p.load    = skin.plan.readings[1];
        p.readingsFit = true;           // on a small screen, condensed into it

        p.gateDoor    = around (skin.plan.gateWord, 110.0f, 28.0f);
        p.limiterDoor = around (skin.plan.limiterWord, 124.0f, 28.0f);
        p.gateDoorAngle    = skin.plan.lampSlant;
        p.limiterDoorAngle = -skin.plan.lampSlant;
        p.readingPx   = px13;
        return p;
    }

    Console::Look dashboardLook (const Skin& skin)
    {
        const auto& art = Art::of (skin);
        return { art.knobDrive, drivePointer, art.knobTone, tonePointer, art.sliderCap, art.faderCap,
                 { &art.lampLit[0], &art.lampLit[1], &art.lampLit[2], &art.lampLit[3] },
                 Art::mark (skin).isNotEmpty() ? Art::mark (skin) : juce::String::fromUTF8 (BinaryData::catlogo_svg, BinaryData::catlogo_svgSize),   // the rover's own mark; the family's, where it has none
                 skin.midiCommands,
                 handLight,         // under the hand, the dashboard's orange
                 skin.ink,
                 skin.badge.has_value() ? skin.badge->lamp : skin.markLamp };      // the lamp under the cat's glass: the badge's, or the bare mark's
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
    void capWord (const Skin& skin, juce::Graphics& g, const juce::String& text, juce::Rectangle<float> key, bool lit)
    {
        const auto& ink = skin.ink;
        const juce::Graphics::ScopedSaveState keep (g);
        condenseInto (g, text, capEm, keyTracking, key.getWidth() * 0.62f, key.getCentre());
        ink.label (g, text, juce::Rectangle<float> (80.0f, 20.0f).withCentre (key.getCentre()), capEm,
                   lit ? ink.state : ink.at (ink.words, 0.75f), keyTracking);
    }

    /** HPF AND LPF ON THEIR KEYS, on every rover: the filter's sign (face::drawFilterSign) on the key's
        face — which goes down with the key — in the state's ink while the filter is in, else the words' at
        0.75, as every word on a key is. */
    void filterSign (const Skin& skin, juce::Graphics& g, juce::Rectangle<float> box, bool down, bool highpass)
    {
        const auto& img = down ? Art::of (skin).keyDown : Art::of (skin).keyUp;
        face::drawFilterSign (g, fittedArea (img, box), highpass, down ? skin.ink.state : skin.ink.at (skin.ink.words, 0.75f));
    }

    /** THE ROOF'S TOGGLE. The two renders are not drawn alike — up, the bat stands over its nut; down,
        it hangs under a nut drawn larger — so each is fitted by its own opaque part: both nuts as wide
        as `nut`, centred on the hole, the bat up or down from there. */
    void drawToggle (const Skin& skin, juce::Graphics& g, juce::Rectangle<float> keyBox, bool up)
    {
        const auto& art = Art::of (skin);
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
    return juce::Rectangle<float> (skin.plan.holes.front().x - 40.0f, 0.0f,
                                   skin.plan.holes.back().x - skin.plan.holes.front().x + 60.0f, 47.0f).toNearestInt();
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

    const auto& ink = skin.ink;
    const auto signAt = [this] (juce::Point<float> hole) { return juce::Point<float> (hole.x - skin.plan.signOffset, hole.y); };

    const bool can[] { canUndo, canRedo };
    for (int i = 0; i < 2; ++i)
    {
        const auto at = signAt (skin.plan.holes[(size_t) (ovUndo + i)]);
        const auto colour = ink.at (ink.words, can[i] ? 1.0f : 0.35f);
        if (! ink.edge.isTransparent())
            drawTurnArrow (g, at.translated (0.0f, Ink::edgeDrop), 6.0f, i == 0, ink.under (colour));
        drawTurnArrow (g, at, 6.0f, i == 0, colour);
    }

    // The gaps between the holes' bezels are narrower than a 13-px capital is wide: the letters are set
    // condensed — as tall as ever, three quarters as wide.
    for (int i = 0; i < numRegisterKeys; ++i)
    {
        const auto at = signAt (skin.plan.holes[(size_t) (ovFirstRegister + i)]);
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

    const auto& ink = skin.ink;
    for (const auto& k : strips.keys)
    {
        if (k.onKey)
            continue;       // on the key's own face (RedPawEditor's keys)
        const auto at = k.label.getCentre();
        const juce::Graphics::ScopedSaveState keep (g);
        condenseInto (g, k.name, px11, keyTracking, skin.plan.keyNameRoom, at);
        ink.label (g, k.name, juce::Rectangle<float> (120.0f, 20.0f).withCentre (at),
                   px11, ink.at (ink.words, k.live ? 1.0f : PushButton::dimmed), keyTracking);
    }
    for (const auto& t : strips.keyTitles)      // a switch's name once over its keys (BRIGHT)
        ink.label (g, t.name, t.label, px11, ink.at (ink.words, 1.0f), keyTracking);
}

/** The tone signs in the words' ink as the keys' quiet words are, at 0.75: a line no brighter than the
    panel's printed names beside it. */
juce::Colour Dashboard::signInk() const { return skin.ink.at (skin.ink.words, 0.75f); }

/** THE PLAYING CHANNEL'S ROWS on the panel (face::Strips): a socket under each tone knob, the size the
    row gives it; the frame round the keys right of the filters, a cell for each; and each knob's name
    over it, the pack's own word, set condensed into the room its pitch leaves. */
void Dashboard::paintStrips (juce::Graphics& g, float) const
{
    using namespace layout;
    const auto& art = Art::of (skin);

    for (const auto& t : strips.tones)
        if (art.socket.isValid())
        {
            const float d = skin.plan.socketSize * strips.toneScale;
            g.drawImage (art.socket, juce::Rectangle<float> (d, d).withCentre (t.knob.getCentre()));
        }

    if (! strips.keyFrame.isEmpty() && art.keyFrameCell.isValid())
    {
        const float s = strips.keyScale, y = strips.keyFrame.getY(), h = strips.keyFrame.getHeight();
        float x = strips.keyFrame.getX();
        const auto piece = [&g, &x, y, h] (const juce::Image& img, float w)
        {
            g.drawImage (img, juce::Rectangle<float> (x, y, w + 0.5f, h));     // a hair over: no seam between pieces
            x += w;
        };
        piece (art.keyFrameLeft, skin.plan.keyCapLeft * s);
        for (size_t i = (size_t) strips.firstRightKey; i < strips.keys.size(); ++i)
            piece (art.keyFrameCell, skin.plan.keyStrip().pitch * s);
        piece (art.keyFrameRight, skin.plan.keyCapRight * s);
    }

    // Each knob's sign over it, in its name's place: the tone's own curve (face::drawToneSign), a shadow
    // under it where the skin prints one. The name is the tag's, under the hand (Console).
    for (const auto& t : strips.tones)
    {
        if (! skin.printShadow.isTransparent())
            drawToneSign (g, t.label.translated (0.0f, 1.5f), t.curve, skin.printShadow, strips.toneSignHeight);
        drawToneSign (g, t.label, t.curve, signInk(), strips.toneSignHeight);
    }
}

void Dashboard::paintStatic (juce::Graphics& g, float scale) const
{
    using namespace layout;

    if (const auto& bg = Art::of (skin).bg; bg.isValid())
        g.drawImage (bg, juce::Rectangle<float> (0.0f, 0.0f, width, height));

    // The maker's mark: on the family's badge where the skin wears it (face::paintBadge) — the cockpit's plate
    // over the picture's, where the plan puts it, its screws, the cat engraved light; else raised in the
    // rover's metal where the skin brings a render of it — cut into bright steel it read as a stamp — and
    // engraved where it does not; none where the skin has no place for one.
    if (skin.mark && skin.badge.has_value())
    {
        juce::Path plate;
        for (size_t i = 0; i < skin.plan.badgeFace.size(); ++i)
            i == 0 ? plate.startNewSubPath (skin.plan.badgeFace[i]) : plate.lineTo (skin.plan.badgeFace[i]);
        plate.closeSubPath();
        paintBadge (g, plate, { skin.plan.badgeScrews.begin(), skin.plan.badgeScrews.end() }, skin.plan.badgeScrew,
                    skin.plan.badgeMark, scale, *skin.badge);
    }
    else if (const auto& mark = Art::of (skin).emblem; skin.mark && skin.markRender && mark.isValid())
    {
        g.setOpacity (1.0f);
        g.drawImage (mark, skin.plan.emblemMark, juce::RectanglePlacement::centred);
    }
    else if (skin.mark)
    {
        engraveMark (g, skin.plan.emblemMark, scale, skin.markInk);
    }

    // A word on the dust: a shadow under it, so it reads off the rust as well as off the bare metal —
    // where the skin has one (RedPawSkin.h).
    const auto& ink = skin.ink;
    const auto shadow = skin.printShadow;
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

    print (skin.driveName, { skin.plan.drive.centre.x, skin.plan.driveNameY }, 200.0f, ink.words);
    // ...the tone knobs' names are the playing pack's, with their sockets (paintStrips).

    print ("TURBO", skin.plan.turboName, 140.0f, ink.state);
    if (skin.plan.learnKeySize > 0.0f)
        for (size_t k = 0; k < (size_t) learnKeysShown; ++k)        // each key's LEARN over its name
        {
            constexpr float em = 8.5f / readingScale;     // under the keys' own words
            print ("LEARN", skin.plan.learnNames[k].translated (0.0f, -0.5f * skin.plan.learnLine), 90.0f, ink.words, 0.0f, em);
            print (k == 0 ? "GATE" : "HUM", skin.plan.learnNames[k].translated (0.0f, 0.5f * skin.plan.learnLine), 90.0f, ink.words, 0.0f, em);
        }
    print (skin.inputName, skin.plan.inputName, 170.0f, ink.words, skin.plan.columnNameRoom);
    print (skin.outputName, skin.plan.outputName, 170.0f, ink.words, skin.plan.columnNameRoom);

    // The maker's words beside the mark, a size under the controls' names and a little quieter.
    if (skin.plan.makerRoom > 0.0f)
    {
        print ("POWERED BY", skin.plan.poweredBy, 220.0f, ink.at (ink.words, 0.8f), skin.plan.makerRoom, px11);
        print ("DARWIN'S CAT", skin.plan.makerName, 240.0f, ink.at (ink.words, 0.8f), skin.plan.makerRoom, px11);
    }
}

//==============================================================================
/** The rover's own part of the site's demo (Console::webExtras): the windscreen — the box its view is
    drawn in and the corners of the glass in it — the lever's slot, its notches, the gears' names, TURBO,
    the wipers' keys; the renders of all of them, the still it ships, and the poster, that still through
    the glass. */
/** THE RINGS GO WITH THE KNOBS: each tone knob's round it where the rows put it, the size the row gives
    it; a ring whose knob the pack has not, hidden with it. DRIVE's is the first and never moves. */
void RedPawEditor::stripsLaid (const face::Strips& laid)
{
    for (size_t t = 0; t + 1 < rings.size(); ++t)
    {
        auto& ring = *rings[t + 1];
        const bool shown = t < laid.tones.size();
        if (shown)
        {
            const auto& nominal = skin.plan.tones[0];
            ring.place (laid.tones[t].knob.getCentre(), nominal.ring * laid.toneScale, nominal.ringWidth * laid.toneScale);
        }
        ring.setVisible (shown);
    }
}

void RedPawEditor::webExtras (const juce::File& dir, juce::DynamicObject& places, juce::DynamicObject& world)
{
    using namespace layout;
    const auto& art = Art::of (skin);
    const std::pair<const char*, const juce::Image*> own[] {
        { "lever", &art.lever }, { "lever-glow", &art.leverGlow }, { "fader-cap-light", &art.faderCapLight },
        { "toggle-up", &art.toggleUp }, { "toggle-down", &art.toggleDown }, { "key-up", &art.keyUp },
        { "key-down", &art.keyDown }, { "turbo-up", &art.turboUp }, { "turbo-down", &art.turboDown },
        { "emblem", &art.emblem }, { "socket", &art.socket },
        { "key-frame-left", &art.keyFrameLeft }, { "key-frame-cell", &art.keyFrameCell }, { "key-frame-right", &art.keyFrameRight } };
    for (const auto& [name, img] : own)
        if (! ((skin.badge.has_value() || ! skin.markRender) && juce::String (name) == "emblem"))     // a skin on the badge, or drawing its mark, shows no render of it
            writeSprite (dir, name, *img);
    writeForWeb (dir.getChildFile ("still.webp"), Art::still (skin));

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
    o->setProperty ("windscreen", rect (skin.plan.windscreen));
    juce::Array<juce::var> corners;
    for (const auto& c : skin.plan.windscreenCorners)
        corners.add (point (c));
    o->setProperty ("windscreenCorners", corners);

    auto* l = new juce::DynamicObject();
    l->setProperty ("slot", rect (skin.plan.gateSlot));
    l->setProperty ("slotX", skin.plan.slotX);
    l->setProperty ("notchX", skin.plan.notchX);
    juce::Array<juce::var> gearsAt, names;
    for (int g = 0; g < lever.numGears(); ++g)
        gearsAt.add (skin.plan.stops[(size_t) g]);
    for (int at = 0; at < lever.numGears(); ++at)
        names.add (GearLever::nameOf (at, lever.numGears()));
    l->setProperty ("notches", gearsAt);     // where the gears stand: the notches the face shows
    l->setProperty ("names", names);      // the gears from the top, a name to a notch
    l->setProperty ("leverWidth", skin.plan.leverWidth);
    l->setProperty ("collarSink", skin.plan.collarSink);
    l->setProperty ("gripBar", GearLever::barBottom);       // the render's top part the hand takes it by
    l->setProperty ("leverBox", point ({ skin.plan.leverLeft, skin.plan.leverTop }));
    l->setProperty ("plate", rect (skin.plan.gearPlate));
    l->setProperty ("glowPad", Art::leverGlowPad);
    o->setProperty ("lever", juce::var (l));

    auto* t = new juce::DynamicObject();
    t->setProperty ("centre", point (skin.plan.turbo));
    t->setProperty ("size", skin.plan.turboSize);
    t->setProperty ("name", point (skin.plan.turboName));
    t->setProperty ("kind", "pair");      // TURBO walks to the gear's paired channel (channels::Lineup::pairs)
    o->setProperty ("turbo", juce::var (t));

    juce::Array<juce::var> wipers;
    for (const auto& k : skin.plan.wiperKeys)
        wipers.add (point (k));
    o->setProperty ("wiperKeys", skin.plan.wiperKeySize > 0.0f ? juce::var (wipers) : juce::var());
    o->setProperty ("wiperKeySize", skin.plan.wiperKeySize);
    o->setProperty ("gears", lever.numGears());
    for (const auto& p : o->getProperties())       // all of it in places.json's "world", which is all the site reads...
        world.setProperty (p.name, p.value);
    dir.getChildFile ("rover.json").replaceWithText (juce::JSON::toString (juce::var (o)));     // ...and on its own (the var takes `o`)

    places.setProperty ("window", rect (skin.plan.windscreen));

    auto* keyRenders = new juce::DynamicObject();      // the keys and the roof's toggles are renders, up and down
    keyRenders->setProperty ("up", spriteRef ("key-up"));
    keyRenders->setProperty ("down", spriteRef ("key-down"));
    keyRenders->setProperty ("toggleUp", spriteRef ("toggle-up"));
    keyRenders->setProperty ("toggleDown", spriteRef ("toggle-down"));
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
    ring->setProperty ("drive", socket (skin.plan.drive));
    ring->setProperty ("tone", socket (skin.plan.tones[0]));        // a tone knob's at the nominal size: each channel's "face" scales it (toneScale)

    // THE ROWS' PIECES (each channel's "face" says where): the socket under a tone knob, `socketSize`
    // across at the nominal size; the frame round the right group, its left cap, a cell for each key
    // and its right cap, `frameHeight` tall — each as wide as said here, times the row's keyScale.
    {
        auto* pieces = new juce::DynamicObject();
        pieces->setProperty ("socket", spriteRef ("socket"));                 // each null where the skin has no render of it
        pieces->setProperty ("socketSize", skin.plan.socketSize);
        pieces->setProperty ("frameLeft", spriteRef ("key-frame-left"));
        pieces->setProperty ("frameCell", spriteRef ("key-frame-cell"));
        pieces->setProperty ("frameRight", spriteRef ("key-frame-right"));
        pieces->setProperty ("capLeft", skin.plan.keyCapLeft);
        pieces->setProperty ("cell", skin.plan.keyStrip().pitch);
        pieces->setProperty ("capRight", skin.plan.keyCapRight);
        pieces->setProperty ("frameHeight", skin.plan.keyFrameHeight);
        world.setProperty ("rows", juce::var (pieces));
    }
    ring->setProperty ("from", KnobRing::from);
    ring->setProperty ("sweep", KnobRing::sweep);
    ring->setProperty ("light", colour (skin.ringLight));
    ring->setProperty ("rest", colour (KnobRing::restOf (skin.ringLight)));     // the whole ring, under the lit arc: the pictures paint none
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
    knobNames->setProperty ("drive", skin.driveName);
    knobNames->setProperty ("driveY", skin.plan.driveNameY);
    knobNames->setProperty ("tonesY", skin.plan.knobNameY);
    knobNames->setProperty ("px", skin.plan.knobNameEm);
    knobNames->setProperty ("shadow", colour (skin.printShadow));
    world.setProperty ("knobNames", juce::var (knobNames));

    // THE KEYS' NAMES: over or under each key by turns, condensed into `room` where wider — or, where the
    // picture leaves no room round them, the filters' on their caps (condensed into 0.62 of the cap, lit in
    // the state's ink, else the words' at 0.75) and the layers' none until the packs carry them.
    auto* keyNames = new juce::DynamicObject();
    keyNames->setProperty ("onKeys", skin.plan.keyNamesOnKeys);
    keyNames->setProperty ("overY", skin.plan.keyNameOverY);
    keyNames->setProperty ("underY", skin.plan.keyNameUnderY);
    juce::Array<juce::var> over;
    for (const bool b : skin.plan.keyNameOver)
        over.add (b);
    keyNames->setProperty ("over", over);
    keyNames->setProperty ("room", skin.plan.keyNameRoom);
    keyNames->setProperty ("px", px11);
    keyNames->setProperty ("capPx", capEm);
    keyNames->setProperty ("filters", "signs");         // HPF and LPF wear no word: their signs (world.filterSigns)

    // THE FILTER KEYS' SIGNS, as the keys are drawn with them: each the size of the key's box (keySize, at
    // twice the stage), laid over the key's render at the same box — off, the key up and the sign quiet;
    // on, the key down and the sign lit.
    {
        auto* signs = new juce::DynamicObject();
        for (const bool highpass : { true, false })
        {
            auto* states = new juce::DynamicObject();
            for (const bool on : { false, true })
            {
                const int px = juce::roundToInt (skin.plan.keySize * 2.0f);
                juce::Image img (juce::Image::ARGB, px, px, true);
                {
                    juce::Graphics sg (img);
                    sg.addTransform (juce::AffineTransform::scale (2.0f));
                    filterSign (skin, sg, { 0.0f, 0.0f, skin.plan.keySize, skin.plan.keySize }, on, highpass);
                }
                const auto file = writeSprite (dir, juce::String (highpass ? "hpf" : "lpf") + (on ? "-on" : "-off"), img);
                states->setProperty (on ? "on" : "off", file);
            }
            signs->setProperty (highpass ? "hpf" : "lpf", juce::var (states));
        }
        world.setProperty ("filterSigns", juce::var (signs));
    }
    keyNames->setProperty ("capRoom", 0.62f);
    keyNames->setProperty ("tracking", face::keyTracking);
    keyNames->setProperty ("dimmed", face::PushButton::dimmed);
    world.setProperty ("keyNames", juce::var (keyNames));

    // THE KEYS THE WORLD TAKES FOR ITSELF: the searchlights, where it has lights to throw — each a latching
    // key with a lamp's sign on its cap (drawLampSign: lit in the state's ink, else the words' at 0.75),
    // kept as the machine's choice and thrown to the view (Drive::setLights); and the wipers' keys.
    juce::Array<juce::var> claimed;
    if (skin.plan.lightKeys[0] >= 0)
        for (int lamp = 0; lamp < 2; ++lamp)
        {
            auto* k = new juce::DynamicObject();
            k->setProperty ("key", skin.plan.lightKeys[(size_t) lamp]);
            k->setProperty ("name", lamp == 0 ? "SEARCHLIGHT LEFT" : "SEARCHLIGHT RIGHT");
            k->setProperty ("does", lamp == 0 ? "lights.left" : "lights.right");
            k->setProperty ("latching", true);
            k->setProperty ("sign", lamp == 0 ? "lamp-left" : "lamp-right");
            claimed.add (juce::var (k));
        }
    world.setProperty ("claimedKeys", claimed);
    if (skin.plan.wiperKeySize > 0.0f)
    {
        auto* w = new juce::DynamicObject();
        w->setProperty ("names", juce::StringArray { "OFF", "INT", "LO", "HI" });
        w->setProperty ("opening", 1);
        w->setProperty ("does", "wipers (Drive::setWipers 0..3)");
        world.setProperty ("wipers", juce::var (w));
    }

    // THE SPEEDS the gears drive at, km/h as the nav screen reads them once eased: each gear, then each
    // with TURBO.
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
        ease->setProperty ("gpu", juce::var (gpu));
        auto* cpu = new juce::DynamicObject();
        cpu->setProperty ("tau", Windscreen::speedSeconds1993);
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
        ease->setProperty ("still", juce::var (still));
    }
    speeds->setProperty ("ease", juce::var (ease));
    world.setProperty ("speeds", juce::var (speeds));

    // THE NAV SCREEN: its boxes, how its top line is set (NavHeader::paint — sizes in the stage's units,
    // a font's size its height as JUCE counts it, ascent and descent), what the skin puts on it, and what
    // the line shows at each place of the lever with each channel there — the channel cut with an
    // ellipsis where it is wider than its room.
    auto* nav = new juce::DynamicObject();
    nav->setProperty ("screen", rect (skin.plan.navScreen));
    nav->setProperty ("header", rect (skin.plan.navHeader));
    nav->setProperty ("picture", rect (skin.plan.navPicture));
    nav->setProperty ("speedHud", skin.plan.speedHud.isEmpty() ? juce::var() : rect (skin.plan.speedHud));
    if (! skin.plan.speedHud.isEmpty())
    {
        // ...and how the speed on the glass is drawn there (SpeedHud's constants): a share of the box's height
        // each size and place, the colours as #rrggbbaa.
        const auto colour = [] (juce::Colour c, float alpha) { c = c.withAlpha (alpha); return juce::String::formatted ("#%02x%02x%02x%02x", c.getRed(), c.getGreen(), c.getBlue(), c.getAlpha()); };
        const auto line = [] (const char* text, float size, float kerning, float centre)
        {
            auto* l = new juce::DynamicObject();
            l->setProperty ("text", text); l->setProperty ("sizeOfHeight", size);
            l->setProperty ("kerning", kerning); l->setProperty ("centreYOfHeight", centre);
            return juce::var (l);
        };
        auto* look = new juce::DynamicObject();
        look->setProperty ("reads", "the shown view's speed (Windscreen::speedKmh), rounded, at least 0, refreshHz times a second");
        look->setProperty ("refreshHz", SpeedHud::refreshHz);
        look->setProperty ("font", "labelFont: the family's label face (Michroma), its size its height as JUCE counts it");
        look->setProperty ("number", line ("<km/h>", SpeedHud::numberOfHeight, SpeedHud::numberKerning, SpeedHud::numberCentreOfHeight));
        look->setProperty ("unit", line (SpeedHud::unit, SpeedHud::unitOfHeight, SpeedHud::unitKerning, SpeedHud::unitCentreOfHeight));
        look->setProperty ("centredX", true);
        look->setProperty ("fill", colour (juce::Colour (SpeedHud::light), SpeedHud::lightAlpha));
        auto* glow = new juce::DynamicObject();
        glow->setProperty ("colour", colour (juce::Colour (SpeedHud::light), SpeedHud::glowAlpha));
        glow->setProperty ("radiusOfHeight", SpeedHud::glowOfHeight);
        glow->setProperty ("how", "juce::DropShadow round the glyphs' outlines, no offset, drawn under the fill");
        look->setProperty ("glow", juce::var (glow));
        nav->setProperty ("speedHudLook", juce::var (look));
    }
    // THE SCREEN'S GROUND is no colour: what shows under its picture is the cabin's own glass, cabin.webp's
    // pixels in the box, and the screen's glass (GlassFilm: murk, dust, scratches) is laid over everything it
    // shows — nav-glass.webp, at twice the stage's size, over its box.
    writeForWeb (dir.getChildFile ("nav-glass.webp"), glassFilm.picture (2.0f));
    auto* glass = new juce::DynamicObject();
    glass->setProperty ("sprite", "nav-glass.webp");
    glass->setProperty ("box", rect (glassFilm.getBounds().toFloat()));
    glass->setProperty ("ground", "nothing filled: the cabin picture's pixels in the screen, the glass over all it shows");
    nav->setProperty ("glass", juce::var (glass));
    nav->setProperty ("learn", rect (skin.plan.learnArea));
    nav->setProperty ("packOnScreen", skin.packOnScreen);
    nav->setProperty ("firstScreen", Monitor::names()[(int) skin.firstScreen]);

    // SHAPE, the screen's first picture, as the screen draws it — edge to edge across `picture`, and not
    // fading in a pause.
    nav->setProperty ("shape", screen.shapeForWeb());

    auto* type = new juce::DynamicObject();
    type->setProperty ("gearPx", px15);
    type->setProperty ("channelPx", px13);
    type->setProperty ("speedPx", px15);
    type->setProperty ("unitPx", px13);
    type->setProperty ("unit", "KM/H");
    type->setProperty ("kerning", 0.04f);
    type->setProperty ("insetX", 8.0f);
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
        const auto gearName = GearLever::nameOf (at, lever.numGears());
        const auto name = skin.packOnScreen ? juce::String (skin.lineup.channels[(size_t) channel].name).toUpperCase() : juce::String();
        const int speed = juce::roundToInt (view->drive().speedFor (gearOf (channel) + 1, turboOf (channel)) * 3.6f);
        const auto f = header.fit (gearName, name, speed);
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
            if (const auto s = header.fit (gearName, name, k).shown; k == 0 || s != was)
            {
                byKmh.add (juce::var (juce::Array<juce::var> { k, s }));
                was = s;
            }
        x->setProperty ("shownByKmh", byKmh);
        lines.add (juce::var (x));
    };
    for (int c = 0; c < (int) skin.lineup.channels.size(); ++c)
        line (gearOf (c), c);
    nav->setProperty ("lines", lines);
    world.setProperty ("nav", juce::var (nav));

    juce::Array<juce::var> stills;
    stills.add ("still.webp");
    places.setProperty ("stills", stills);

    // THE POSTERS: the rover as it opens, and one for each place of the lever, its first gear first, TURBO
    // off — through the glass the windscreen's first frame there, a drive of the rover's made at its start
    // and settled at the place (Windscreen::firstFrame), and the still it ships in the poster's -still kind.
    webPoses.count = lever.numPlaces();
    webPoses.opening = lever.getPlace();
    webPoses.firstFrame = [this] (int, std::function<void (juce::Image)> done)
    {
        const auto box = skin.plan.windscreen * 2.0f;
        view->firstFrame (skin.makeDrive(), juce::roundToInt (box.getWidth()), juce::roundToInt (box.getHeight()), std::move (done));
    };
    webPoses.live = juce::String ("the windscreen's first frame for the place: the ") + view->drive().name()
                  + " drive on the GPU from its start, at the place's gear, settled, nothing stepped (t = 0), TURBO off, "
                    "the wipers and the lights as the face opens; the top poster is the place the app opens at";
    webPoses.standAt = [this] (int at)
    {
        pullGear (at);
        lever.setPlace (at, false, false);
    };
    webPoses.still = [this] (int) { return Art::still (skin); };
    webPoses.window = skin.plan.windscreen;
    webPoses.hidden = { view.get() };
    webPatch ("gateFader", { &threshold });
    webPatch ("limiterFader", { &ceiling });
    webPatch ("nav", { &header, &screen, &glassFilm });
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
    : face::Console (p, std::make_unique<Dashboard> (skinOf (p.product())), dashboardPlaces (skinOf (p.product())),
                     dashboardLook (skinOf (p.product()))),
      skin (skinOf (p.product())),
      view (std::make_unique<Windscreen> (skin.makeDrive())),
      lever (skin, numGears (skin.lineup)),
      screen (p)
{
    using namespace layout;

    // Everything this constructor sets is the face settling onto the state, not a player's edit.
    const felitronics::appkit::CompareHistory::ScopedSuppress settling (amp.history);

    // THE ROOF'S KEYS ARE TOGGLES: thrown up while a history key is pressed, and up on the register
    // that is playing.
    for (int i = 0; i < 6; ++i)
        roofKey (i).drawKey = [this, &k = roofKey (i)] (juce::Graphics& g, juce::Rectangle<float> box, bool down, float)
        {
            if (k.isMouseOverOrDragging())
            {
                juce::Path hole;
                hole.addEllipse (juce::Rectangle<float> (skin.plan.holeRadius * 2.0f, skin.plan.holeRadius * 2.0f).withCentre (box.getCentre()));
                drawHandRim (g, hole, handLight);
            }
            drawToggle (skin, g, box, down);
        };

    // ...the six square keys, and TURBO, are the dashboard's own buttons, up or pressed — each with the
    // hand's orange rim round its face while the hand is over it, like everything else that moves; the
    // filters' names on their faces where the layout puts the names there.
    for (int i = 0; i < face::numKeys; ++i)
        key (i).drawKey = [this, &k = key (i), i] (juce::Graphics& g, juce::Rectangle<float> box, bool down, float)
        {
            const auto& img = down ? Art::of (skin).keyDown : Art::of (skin).keyUp;
            drawRender (g, img, box);
            const bool lamp = skin.plan.lightKeys[0] == i || skin.plan.lightKeys[1] == i;
            if (i <= btLpf)
                filterSign (skin, g, box, down, i == btHpf);
            else if (skin.plan.keyNamesOnKeys && ! lamp)
                capWord (skin, g, keyName (i), fittedArea (img, box), down);
            for (int lamp = 0; lamp < 2; ++lamp)
                if (skin.plan.lightKeys[(size_t) lamp] == i)
                    drawLampSign (g, fittedArea (img, box), lamp == 0,
                                  down ? skin.ink.state : skin.ink.at (skin.ink.words, 0.75f));
            if (k.isMouseOverOrDragging() && k.isEnabled())
            {
                juce::Path face;
                face.addRoundedRectangle (fittedArea (img, box).reduced (2.0f), 4.0f);
                drawHandRim (g, face, handLight);
            }
        };

    // THE RINGS ROUND THE KNOBS say where each stands.
    const auto ringFor = [this] (const ArtKnob& knob, const layout::Socket& s) { return std::make_unique<KnobRing> (skin, knob, s.centre, s.ring, s.ringWidth); };
    rings.push_back (ringFor (driveKnob(), skin.plan.drive));
    for (int t = 0; t < Channels::maxTones; ++t)        // as many as there are knobs: where they stand, the rows say (stripsLaid)
        rings.push_back (ringFor (toneKnob (t), skin.plan.tones[0]));
    for (auto& ring : rings)
        addToStage (*ring);
    stripsLaid (strips);        // the console laid the rows before there were rings to put round them

    // THE WINDSCREEN, under the dashboard: the picture's alpha is its frame.
    stage.addAndMakeVisible (*view, 0);
    view->setBounds (skin.plan.windscreen.toNearestInt());
    view->addMouseListener (&glassClicks, false);
    glassClicks.menu = [this] { showWindowMenu(); };

    // THE GEAR LEVER: the pedal, and everything its channel was left with. Its box is the gate's plate
    // and round it as far as the grip, its light and its shadow reach.
    addToStage (lever);
    const float leverRight = juce::jmax (skin.plan.gearPlate.getRight(), skin.plan.notchX + skin.plan.leverWidth * 0.5f + 16.0f);
    lever.setBounds (juce::Rectangle<float> (skin.plan.leverLeft, skin.plan.leverTop, leverRight - skin.plan.leverLeft, skin.plan.gearPlate.getBottom() - skin.plan.leverTop).toNearestInt());
    lever.onPlace    = [this] (int at) { pullGear (at); };
    lever.onRelease  = [this] { showChannel(); };  // held, it follows the hand; let go, it follows the state

    // TURBO: the pedal's second channel, on or off — one switch for every gear.
    turbo.latching = false;
    turbo.drawKey = [this] (juce::Graphics& g, juce::Rectangle<float> box, bool down, float)
    {
        const auto& img = down ? Art::of (skin).turboDown : Art::of (skin).turboUp;
        drawRender (g, img, box);
        if (turbo.isMouseOverOrDragging() && turbo.isEnabled())
        {
            juce::Path face;
            face.addEllipse (fittedArea (img, box).reduced (2.0f));
            drawHandRim (g, face, handLight);
        }
    };
    turbo.onPress = [this] { throwTurbo (! turboOf (channels.active())); };
    turbo.onMenu  = [this] { showBoostMenu(); };   // a right click: how much louder the amp leaves while it is on
    addToStage (turbo);
    turbo.setBounds (juce::Rectangle<float> (skin.plan.turboSize, skin.plan.turboSize).withCentre (skin.plan.turbo).toNearestInt());

    // GATE'S THRESHOLD AND LIMITER'S CEILING, each on a short fader down its lamp plate's outer leg, its
    // cap lighter than the columns' so a small one reads in its corner. A double click puts it back where
    // it opens, a right click is its block's menu, and while it is held the tag over it says where it
    // stands. While its block is off it stands dim, and still moves.
    const auto plateFader = [this] (face::ColumnFader& fader, juce::Rectangle<float> slot, const char* paramId, const char* onId,
                                    float low, float high, std::function<void()> menu, const juce::String& name)
    {
        const auto travelOf = [low, high] (float v) { return (v - low) / (high - low); };
        fader.scale = skin.plan.slotScale;
        fader.capLength = skin.plan.faderCapLength * skin.plan.slotScale;
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
        const bool across = slot.getWidth() > slot.getHeight();     // a slot lying across: the fader runs left to right
        const auto run = slot.reduced (across ? skin.plan.slotRim : 0.0f, across ? 0.0f : skin.plan.slotRim);
        const float breadth = skin.plan.faderWidth * skin.plan.slotScale;
        fader.setAcross (across);
        fader.setBounds ((across ? run.withSizeKeepingCentre (run.getWidth(), breadth)
                                 : run.withSizeKeepingCentre (breadth, run.getHeight())).toNearestInt());
    };
    threshold.marks = 6;            // every 5 dB, the longer at -50
    threshold.longMark = 4;
    plateFader (threshold, skin.plan.thresholdSlot, params::gateThreshold, params::gateOn, thresholdLowDb, thresholdHighDb,
                [this] { showGateMenu(); }, "GATE");
    ceiling.marks = 6;              // every half a decibel or so, none longer
    ceiling.longMark = -1;
    plateFader (ceiling, skin.plan.ceilingSlot, params::limiterCeiling, params::limiterOn, params::limiterCeilingMin, params::limiterCeilingMax,
                [this] { showOutputMenu(); }, "LIMITER");

    // THE NAV SCREEN: along its top the gear, the channel where the skin names it, and the speed the
    // windscreen is driving at; under them the picture of the amp at work the machine has chosen, the
    // skin's until then.
    header.speed = [this] { return view->speedKmh(); };
    addToStage (header);
    header.setBounds (skin.plan.navHeader.toNearestInt());
    if (! skin.plan.speedHud.isEmpty())       // ...or the speed on the glass, the screen's top line the gear alone
    {
        hud.speed = header.speed;
        header.showsSpeed = false;
        addToStage (hud);
        hud.setBounds (skin.plan.speedHud.toNearestInt());
    }
    addToStage (screen);
    screen.setBounds (skin.plan.navPicture.toNearestInt());
    screen.show ((Monitor::View) juce::jlimit (1, (int) Monitor::View::numViews - 1,
                                               (int) prefs::store (amp.product()).get (screenKey, (int) skin.firstScreen)));

    // LEARN, from the menus or a key of its own, takes the picture's place while it runs — and the top
    // line's where its place takes that in (RedPawLayout.h) — and lights its key.
    addToStage (learnGlass);
    learnGlass.setBounds (skin.plan.learnArea.toNearestInt());
    learnGlass.inset = { 8.0f, 4.0f };      // the top line's words start where the header's do
    learnGlass.onShow = [this] (bool up)
    {
        screen.setAlpha (up ? 0.0f : 1.0f);
        if (skin.plan.learnArea.intersects (skin.plan.navHeader))
            header.setAlpha (up ? 0.0f : 1.0f);
        learnShowing = up ? (learnGlass.showsHum() ? 1 : 0) : -1;
        learnKey.repaint();
        humKey.repaint();
    };

    if (skin.plan.learnKeySize > 0.0f)
        for (int k = 0; k < learnKeysShown; ++k)
        {
            auto& key = k == 0 ? learnKey : humKey;
            key.latching = false;
            key.drawKey = [this, &key, k] (juce::Graphics& g, juce::Rectangle<float> box, bool down, float)
            {
                const auto& img = down || learnShowing == k ? Art::of (skin).turboDown : Art::of (skin).turboUp;
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
            key.setBounds (juce::Rectangle<float> (skin.plan.learnKeySize, skin.plan.learnKeySize).withCentre (skin.plan.learnKeys[(size_t) k]).toNearestInt());
        }

    // THE SEARCHLIGHTS on two of the six keys, where the layout gives them some: each key a lamp, lit
    // while it burns, kept as the machine's choice.
    if (skin.plan.lightKeys[0] >= 0)
    {
        const int kept = (int) prefs::store (amp.product()).get (lightsKey, 0);
        const auto throwLights = [this]
        {
            const bool l = key (skin.plan.lightKeys[0]).isOn(), r = key (skin.plan.lightKeys[1]).isOn();
            prefs::store (amp.product()).set (lightsKey, (l ? 1 : 0) | (r ? 2 : 0));
            view->setLights (l, r);
        };
        for (int lamp = 0; lamp < 2; ++lamp)
        {
            auto& k = key (skin.plan.lightKeys[(size_t) lamp]);
            claimKey (skin.plan.lightKeys[(size_t) lamp], lamp == 0 ? "SEARCHLIGHT LEFT" : "SEARCHLIGHT RIGHT");
            k.latching = true;
            k.setOn ((kept & (1 << lamp)) != 0, false);
            k.onChange = [throwLights] (bool) { throwLights(); };
        }
        view->setLights ((kept & 1) != 0, (kept & 2) != 0);
    }

    // THE WIPERS' SPEED, where the picture paints keys for it: OFF, INT, LO, HI, the speed set lit.
    if (skin.plan.wiperKeySize > 0.0f)
    {
        wipers = juce::jlimit (0, 3, (int) prefs::store (amp.product()).get (wipersKey, 1));
        const char* words[] { "OFF", "INT", "LO", "HI" };
        for (int i = 0; i < 4; ++i)
        {
            auto& k = *wiperButtons.emplace_back (std::make_unique<PushButton> (words[i]));
            k.latching = false;
            k.drawKey = [this, &k, i] (juce::Graphics& g, juce::Rectangle<float> box, bool down, float)
            {
                const bool lit = down || wipers == i;
                const auto& img = lit ? Art::of (skin).keyDown : Art::of (skin).keyUp;
                drawRender (g, img, box);
                capWord (skin, g, k.name, fittedArea (img, box), lit);
                if (k.isMouseOverOrDragging())
                {
                    juce::Path face;
                    face.addRoundedRectangle (fittedArea (img, box).reduced (2.0f), 4.0f);
                    drawHandRim (g, face, handLight);
                }
            };
            k.onPress = [this, i] { chooseWipers (i); };
            addToStage (k);
            k.setBounds (juce::Rectangle<float> (skin.plan.wiperKeySize, skin.plan.wiperKeySize).withCentre (skin.plan.wiperKeys[(size_t) i]).toNearestInt());
        }
        view->setWipers (wipers);
    }

    // The screen's glass, over the lot: a little murky, dusty, lightly scratched — and the tuner's strip,
    // where the tuner is not a dial of its own.
    addToStage (glassFilm);
    glassFilm.setBounds ((skin.plan.tunerDial.reach > 0.0f ? skin.plan.navHeader.getUnion (skin.plan.navPicture) : skin.plan.navHeader.getUnion (skin.plan.tunerScreen)).toNearestInt());

    // A prototype's hook for looking at the windscreen in a gear, or with TURBO, without a hand on
    // anything: ROVER_CHANNEL names a channel, from 0 to the product's last.
    if (const auto c = juce::SystemStats::getEnvironmentVariable ("ROVER_CHANNEL", {}); c.isNotEmpty())
        engine.go (c.getIntValue());

    showChannel();

    // What the windscreen shows: the machine's choice, the Plains unless it has said otherwise — drawn
    // by this window's context, which paints the face over it. A GPU that will not draw it says so in the
    // family's log, under this amp's name, format and host.
    view->setLogName (amp.loadedAs());
    view->setHost (&gl);
    view->setHearing ([&a = amp] { return a.takePlayPeak(); });   // the world lights by the guitar (worlds/Play.h)
    gl.setOpenGLVersionRequired (juce::OpenGLContext::openGL3_2);
    gl.setRenderer (this);
    gl.setComponentPaintingEnabled (true);
    gl.setContinuousRepainting (false);     // the view asks for each frame, at its own pace
    gl.attachTo (*this);
    view->setFrameRateCap ((int) prefs::store (amp.product()).get (frameRateKey, 60));
    view->setPicture ([this] { return Art::still (skin); });     // STILL is the rover's own picture, where it ships one
    view->setMode ((Windscreen::Mode) juce::jmax (0, landscapeWords.indexOf (prefs::store (amp.product()).get (landscapeKey, landscapeWords[0]).toString())));
    placeView();
}

RedPawEditor::~RedPawEditor()
{
    view->removeMouseListener (&glassClicks);
    gl.detach();
}

/** A gear pulled: that pedal, TURBO as it stands. */
void RedPawEditor::pullGear (int gear)
{
    go (channels.leftOn (channelOf (gear, false)));     // TURBO as the gear was left with it
}

void RedPawEditor::throwTurbo (bool on)
{
    go (channelOf (gearOf (channels.active()), on));
}

void RedPawEditor::showSelector (int channel)
{
    const int gear = gearOf (channel);
    const bool turboOn = turboOf (channel);

    if (! lever.isHeld() && lever.getPlace() != gear)
        lever.setPlace (gear, true, false);
    turbo.setOn (turboOn, false);

    header.set (GearLever::nameOf (gear, lever.numGears()),
                skin.packOnScreen ? juce::String (skin.lineup.channels[(size_t) channel].name).toUpperCase() : juce::String());

    view->setGear (gear + 1);
    view->setTurbo (turboOn);
}

/** 1, 2, ... as many as the gears: the lever goes to that gear, as if a hand had put it there; T throws
    TURBO.
    The keys are the same whatever the layout prints on them — under a Russian one T arrives as Е. */
bool RedPawEditor::channelKey (const juce::KeyPress& key)
{
    const auto letter = juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) key.getKeyCode());
    const int at = letter >= '1' && letter < '1' + lever.numGears() ? (int) (letter - '1') : -1;
    if (at >= 0)
    {
        if (! lever.isHeld())
            lever.setPlace (at, true);
        return true;
    }

    if (letter == 'T' || letter == 0x0415)
    {
        throwTurbo (! turboOf (channels.active()));
        return true;
    }
    return false;
}

/** Programs 1 to the number of channels are the channels. */
void RedPawEditor::programChange (int program)
{
    if (program >= 0 && program < channels.count())
        go (program);
}

/** TURBO's controller: the pedal's second channel, or its first. */
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
    const auto& landscapeNames = skin.landscapeNames;
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
    if (! skin.plan.navHeader.getUnion (skin.plan.navPicture).contains (at.toFloat()))
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
    prefs::store (amp.product()).set (screenKey, (int) v);
    screen.show (v);
}

void RedPawEditor::chooseWipers (int speed)
{
    wipers = speed;
    prefs::store (amp.product()).set (wipersKey, speed);
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
    prefs::store (amp.product()).set (landscapeKey, landscapeWords[(int) mode]);
    view->setMode (mode);
}

void RedPawEditor::chooseFrameRate (int fps)
{
    prefs::store (amp.product()).set (frameRateKey, fps);
    view->setFrameRateCap (fps);
}

/** Where the windscreen is in the editor's own pixels, for the GL thread: the stage's scale and place. */
void RedPawEditor::placeView()
{
    const auto at = (skin.plan.windscreen * stageScale() + stageOrigin()).toNearestInt();
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

} // namespace orbitamp::redpaw
