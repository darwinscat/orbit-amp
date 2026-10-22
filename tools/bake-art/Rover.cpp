// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "Bake.h"

#include "face/Stage.h"

#include <algorithm>
#include <array>
#include <cmath>

/** THE ROVERS' PICTURES (src/redpaw/RedPawSkin.h): one dashboard plan, each rover over a picture of its own,
    each made to stand the way its face stands now. Numbers are in the dashboard's 1672-wide space,
    measured off each picture — the same space its skin lays its controls out in. */
namespace orbitamp::bake
{

namespace
{
    using face::stage::width;

    /** A NOTCH PAINTED WHERE NO GEAR STANDS IS FILLED IN (`fitNotches`), so the slot runs straight there
        and the lever has nowhere to stand that is not a gear; where a gear stands with no notch painted,
        the painted one nearest it is copied there, over the same rows and faded in the same way. From
        `above` the notch's middle to `below` it: the slot's edge, from `left` in the slot's dark to `seam`,
        is the straight run just over the fill, `run` tall, mirrored down and back, so the edge's lit line
        goes on unbroken; the plate beside it, from `seam` to `right`, is the plate `plateShift` further
        right on the same rows, so its grain and its light are the rows' own. The fill fades into the
        picture over `feather` at its top, its bottom and its right, and the edge into the plate across
        the seam. */
    struct NotchFill { float above, below, left, seam, right, run, plateShift, feather; };

    /** A SLOT PAINTED WITH ROOM OVER ITS FIRST NOTCH IS CUT SHORT (`shortenSlot`), so it ends as far over
        the first gear as it runs under the last and nothing over the gears reads as a place. Its top end
        — the plate just over it, the rim and the round corners, from `top` down `end` rows, `left` to
        `right` — moves `drop` down; the rows it leaves are the plate `plateShift` to the left, from
        `plateTop` down, where it is plain; the whole faded into the picture over `feather` at its top and
        its sides, and the plate into the moved end across their seam. Under the moved end the slot runs
        on as painted. A `drop` of nought: the slot as painted. */
    struct SlotEnd { float left, right, top, end, drop, plateShift, plateTop, feather; };

    /** REVERB's or DELAY's frame, as the picture painted it: the plate's top (its shadow), where the rim's
        lit edge ends and its face begins, where the well opens under it, and how far down the end caps'
        top screws reach. */
    struct Rim { float left, right, top, face, well, capScrews, capLeft, capRight; };

    /** A corner lamp's plate, grown on its outer leg (`widenLampPlates`). */
    struct LampPlate
    {
        juce::Rectangle<float> moved;       // what moves inward: the plate inward of the cut, with its shadow...
        std::array<juce::Point<float>, 5> outline;  // ...cut to the plate's own outline, so the panel round it stays put
        float shift;                        // ...this far, to the right for plus
        float room;                         // the new room's left edge; it is lampPlateGrow wide
        std::array<float, 3> from;          // where its pieces are taken from, across: the top edge, the plain metal, the bottom edge
        std::array<float, 4> rows;          // the pieces' rows, the same where they are taken and where they go
        juce::Rectangle<float> slot;        // the slot cut down the room, its rim included — where the skin lays its fader
    };

    /** WHAT ONE ROVER'S PICTURE NEEDS. */
    struct Rover
    {
        const char* face;

        /** The lever: its gears (the product's pairs of channels, RedPawLineup.h), the notches its picture
            paints, the stops the gears stand at (the skin's layout::stops), how a notch is filled, and how
            the slot is cut short over the first gear. */
        int gears;
        std::vector<float> notches, stops;
        NotchFill notchFill;
        SlotEnd slotEnd;

        /** REVERB's and DELAY's upper rims, each made `rimLift` taller, upward, so that the words over each
            well stand ON its frame; none where the picture has room for the words as painted. */
        std::array<Rim, 2> rims;
        float rimLift;

        /** THE CORNER LAMPS' PLATES GROW ON THEIR OUTER LEGS, each for a fader's slot: the plate as painted
            is cut along its outer leg, between its edge and its screws; all of it inward of the cut — the
            screws, the lamp, the slanted edge — moves `lampPlateGrow` inward, and the room left is filled
            with the plate's own top and bottom edges and plain metal between, a slot of INPUT's kind cut
            down it. Nothing where the picture paints the slots itself. */
        float lampPlateGrow;
        LampPlate gatePlate, limiterPlate;
        juce::Rectangle<float> inSlotRim;   // INPUT's slot with its rim, as painted
        float inSlotEnd;                    // each round end, rim and all
        float slotScale;                    // the corner slots' size to INPUT's

        /** THE LAMPS THE DASHBOARD PAINTS LIT, each in its box (the skin's layout::lamps): how much of a
            pixel is a lit lamp, 0..1 — none where the picture paints no lamp lit. */
        float (*lampness) (juce::Colour);
        std::array<juce::Rectangle<float>, 4> lamps;
    };

    /** On a rusted dashboard warm is everywhere: a lamp is what is both warm AND bright — its red well
        over its blue, and the whole of it lit — so the rust and the dust round it stay as they were
        painted. */
    float marsLampness (juce::Colour c)
    {
        const float r = c.getFloatRed(), g = c.getFloatGreen(), b = c.getFloatBlue();
        const float warm   = (r - b - 0.25f) / 0.25f;
        const float bright = (0.3f * r + 0.59f * g + 0.11f * b - 0.38f) / 0.18f;
        return juce::jmin (juce::jlimit (0.0f, 1.0f, warm), juce::jlimit (0.0f, 1.0f, bright));
    }

    /** On frosted steel and ice nothing is warm but the amber lamps and the light they throw: a lamp is
        what is warm — its red well over its blue — and lit, so the steel, the frost and the ice-blue
        strips stay as they were painted. */
    float iceLampness (juce::Colour c)
    {
        const float r = c.getFloatRed(), g = c.getFloatGreen(), b = c.getFloatBlue();
        const float warm   = (r - b - 0.25f) / 0.25f;
        const float bright = (0.3f * r + 0.59f * g + 0.11f * b - 0.38f) / 0.18f;
        return juce::jmin (juce::jlimit (0.0f, 1.0f, warm), juce::jlimit (0.0f, 1.0f, bright));
    }

    const std::array<Rover, 3> rovers {{
        // REDPAWAMP: two gears down a slot painted for three, spread evenly from 694 under the slot's top
        // end — a step of 61.75 to the middle and another to the third notch. The first two notches are filled in, and the middle
        // one is the second, copied up. A notch is cut 805..829 down and out to 824, its bevels' corners
        // reaching 803.5 and 831.5 on the slot's lit edge, 809.5..811.5; the straight run over the third is
        // 789..803.5. The plate right of it is plain out to 850.
        // The slot's dark is 786..810 across, 681.25..855.25 down, its rim 783.5..812.25 and its top 679.75;
        // the last gear stands 37.75 over its bottom end, so its top end goes to 37.75 over the first gear,
        // 718 — 36.75 down. The plate left of it, 747..781, is plain from 697 down, under the plate's screw.
        { "redpaw", 2, { 733.8f, 776.0f, 817.5f }, { 755.75f, 817.5f },
          { 17.0f, 17.0f, 803.0f, 813.0f, 831.0f, 11.0f, 16.0f, 2.0f },
          { 781.0f, 815.0f, 676.0f, 15.0f, 36.75f, 34.0f, 697.0f, 2.0f },
          {{ { 276.0f, 681.0f, 683.0f, 691.0f, 699.5f, 703.0f, 316.0f, 637.0f },      // REVERB
             { 276.0f, 681.0f, 755.5f, 763.0f, 772.5f, 777.0f, 316.0f, 637.0f } }},   // DELAY
          10.0f,
          24.0f,
          { { 37.5f, 794.0f, 107.5f, 106.0f },
            {{ { 37.5f, 795.0f }, { 64.5f, 795.0f }, { 140.0f, 870.5f }, { 140.0f, 900.0f }, { 37.5f, 900.0f } }},
            24.0f, 37.5f,
            {{ 38.0f, 146.0f, 60.0f }}, {{ 794.0f, 806.0f, 889.0f, 900.0f }},
            { 40.5f, 808.0f, 18.0f, 80.0f } },
          { { 1525.0f, 791.0f, 110.5f, 106.0f },
            {{ { 1635.5f, 792.5f }, { 1607.5f, 792.5f }, { 1529.5f, 866.5f }, { 1529.5f, 897.0f }, { 1635.5f, 897.0f } }},
            -24.0f, 1611.5f,
            {{ 1612.0f, 1502.0f, 1590.0f }}, {{ 791.0f, 803.0f, 886.0f, 897.0f }},
            { 1614.5f, 805.0f, 18.0f, 80.0f } },
          { 122.0f, 444.0f, 31.0f, 243.0f }, 16.0f, 18.0f / 31.0f,
          marsLampness,
          {{ { 27.0f,   363.0f, 135.0f, 32.0f },      // the segments 37..152 x 373..385
             { 1510.0f, 363.0f, 132.0f, 32.0f },      // the segments 1520..1632 x 372..385
             { 69.0f,   824.0f, 62.0f, 63.0f },       // the slant 55..97 x 834..877 as painted, moved in with its plate
             { 1541.0f, 824.0f, 62.0f, 63.0f } }} },  // the slant 1575..1617 x 834..877 as painted, moved in with its plate

        // FROSTPAWAMP: three gears in its three notches, so nothing is filled; its third notch cut 804..830
        // down, its slot's lit edge 810..812, the straight run over it 789..803, the plate plain out to 848:
        // filled as RedPawAmp's would be, were it to have fewer gears. Rims and corner plates as RedPawAmp's.
        // The slot's dark is 681.5..855.25 down; the last gear stands 38.45 over its bottom end, so its top
        // end goes to 38.45 over the first gear, 694.35 — 12.85 down — cut short as RedPawAmp's is.
        { "frostpaw", 3, { 732.8f, 775.5f, 816.8f }, { 732.8f, 775.5f, 816.8f },
          { 17.0f, 17.0f, 803.0f, 813.0f, 831.0f, 11.0f, 16.0f, 2.0f },
          { 781.0f, 815.0f, 676.0f, 15.0f, 12.85f, 34.0f, 697.0f, 2.0f },
          {{ { 276.0f, 681.0f, 683.0f, 691.0f, 699.5f, 703.0f, 316.0f, 637.0f },      // REVERB
             { 276.0f, 681.0f, 755.5f, 763.0f, 772.5f, 777.0f, 316.0f, 637.0f } }},   // DELAY
          10.0f,
          24.0f,
          { { 37.5f, 794.0f, 107.5f, 106.0f },
            {{ { 37.5f, 795.0f }, { 64.5f, 795.0f }, { 140.0f, 870.5f }, { 140.0f, 900.0f }, { 37.5f, 900.0f } }},
            24.0f, 37.5f,
            {{ 38.0f, 146.0f, 60.0f }}, {{ 794.0f, 806.0f, 889.0f, 900.0f }},
            { 40.5f, 808.0f, 18.0f, 80.0f } },
          { { 1525.0f, 791.0f, 110.5f, 106.0f },
            {{ { 1635.5f, 792.5f }, { 1607.5f, 792.5f }, { 1529.5f, 866.5f }, { 1529.5f, 897.0f }, { 1635.5f, 897.0f } }},
            -24.0f, 1611.5f,
            {{ 1612.0f, 1502.0f, 1590.0f }}, {{ 791.0f, 803.0f, 886.0f, 897.0f }},
            { 1614.5f, 805.0f, 18.0f, 80.0f } },
          { 122.0f, 444.0f, 31.0f, 243.0f }, 16.0f, 18.0f / 31.0f,
          iceLampness,
          {{ { 27.0f,   363.0f, 135.0f, 32.0f },      // the segments 40..152 x 372..386
             { 1510.0f, 363.0f, 132.0f, 32.0f },      // the segments 1520..1631 x 372..386
             { 69.0f,   824.0f, 62.0f, 63.0f },       // the slant 55..97 x 834..877 as painted, moved in with its plate
             { 1541.0f, 824.0f, 62.0f, 63.0f } }} },  // the slant 1575..1617 x 834..877 as painted, moved in with its plate

        // AMBERPAWAMP: five ticks right of a straight slot, four places and one over them that is none; the
        // top tick, and a fourth gear's tick past its three, are painted over — the spare tick's white bar is
        // 926..946.5 x 702..708: over it, the plain plate just above, mirrored down and back, faded in at the
        // fill's top, bottom and right. Its slot stands as painted: with both end ticks gone it runs about as
        // far over the first gear as under the last. The wells' words have room as painted, and its corner
        // slots are painted: nothing grows. No lamp is painted lit.
        { "amberpaw", 3, { 548.9f, 587.5f, 627.0f, 666.3f, 704.7f }, { 587.5f, 627.0f, 666.3f, 704.7f },
          { 9.0f, 9.0f, 921.0f, 960.0f, 953.0f, 9.0f, 0.0f, 2.0f },
          {},
          {}, 0.0f,
          0.0f, {}, {},
          { 20.0f, 492.0f, 27.0f, 221.0f }, 13.0f, 21.0f / 27.0f,
          nullptr, {} },
    }};

    /** THE UPPER RIMS OF REVERB'S AND DELAY'S FRAMES GROW, so that the words over each well stand ON its
        frame. The frame's top — its shadow and the rim's lit edge — goes `rimLift` higher, and the room it
        leaves is filled with the frame's own face: along the rim, the rim's face band mirrored back and
        forth; on the end caps, the plain face under their top screws, so the screws keep their shape. A
        picture with room for the words as it is painted lifts them nothing. */
    juce::Image widenRims (juce::Image img, const Rover& rover)
    {
        if (! img.isValid() || rover.rimLift <= 0.0f)
            return img;

        const float k = (float) img.getWidth() / width;
        const auto original = img.createCopy();
        const juce::Image::BitmapData src (original, juce::Image::BitmapData::readOnly);
        juce::Image::BitmapData dst (img, juce::Image::BitmapData::readWrite);
        const int lift = juce::roundToInt (rover.rimLift * k);
        const auto at = [k] (float v) { return juce::roundToInt (v * k); };

        for (const auto& rim : rover.rims)
            for (int x = at (rim.left); x <= at (rim.right); ++x)
            {
                const bool cap = x < at (rim.capLeft) || x > at (rim.capRight);
                const int seam = cap ? at (rim.capScrews) : at (rim.face);     // rows above this go up; rows below stay
                const int band = cap ? lift : juce::jmax (1, at (rim.well) - seam);

                for (int y = at (rim.top) - lift; y < seam - lift; ++y)
                    dst.setPixelColour (x, y, src.getPixelColour (x, y + lift));
                for (int i = 0; i < lift; ++i)
                {
                    const int m = i % (2 * band);
                    dst.setPixelColour (x, seam - lift + i, src.getPixelColour (x, seam + (m < band ? m : 2 * band - 1 - m)));
                }
            }
        return img;
    }

    /** THE PAINTED NOTCHES FIT TO THE GEARS (NotchFill): a notch painted where no gear stands is filled in,
        so the slot runs straight there — over it the slot's edge is the straight run above it, mirrored
        down and back, and the plate beside it is the plate further right on the same rows — and where a
        gear stands with no notch painted, the painted one nearest it is copied there, the same rows of the
        picture moved. Each is faded into the picture at its top, bottom and right. */
    juce::Image fitNotches (juce::Image img, const Rover& rover)
    {
        const auto gearsAt = std::vector<float> (rover.stops.begin(), rover.stops.begin() + juce::jmin ((std::ptrdiff_t) rover.gears, (std::ptrdiff_t) rover.stops.size()));
        const auto among = [] (float y, const std::vector<float>& ys)
        {
            return std::any_of (ys.begin(), ys.end(), [y] (float v) { return std::abs (v - y) < 1.0f; });
        };
        std::vector<float> filled, cut;
        for (const float y : rover.notches)
            if (! among (y, gearsAt)) filled.push_back (y);
        for (const float y : gearsAt)
            if (! among (y, rover.notches)) cut.push_back (y);
        if (! img.isValid() || (filled.empty() && cut.empty()))
            return img;

        const auto& f = rover.notchFill;
        const float k = (float) img.getWidth() / width;
        const auto at = [k] (float v) { return juce::roundToInt (v * k); };
        const auto original = img.createCopy();
        const juce::Image::BitmapData src (original, juce::Image::BitmapData::readOnly);
        juce::Image::BitmapData dst (img, juce::Image::BitmapData::readWrite);

        const int left = at (f.left), seam = at (f.seam), right = at (f.right), shift = at (f.plateShift);
        const int run = juce::jmax (1, at (f.run));
        const float feather = (float) juce::jmax (1, at (f.feather));

        for (const float notch : filled)
        {
            const int top = at (notch - f.above), bottom = at (notch + f.below);
            for (int y = top; y < bottom; ++y)
            {
                const int m = (y - top) % (2 * run);
                const int edgeRow = top - 1 - (m < run ? m : 2 * run - 1 - m);     // the run over the fill, mirrored down and back
                const float down = juce::jmin ((float) (y - top + 1) / feather, (float) (bottom - y) / feather);

                for (int x = left; x < right; ++x)
                {
                    const float across = juce::jlimit (0.0f, 1.0f, (float) (x - seam) / feather + 0.5f);     // edge to plate
                    const auto fill = src.getPixelColour (x, edgeRow).interpolatedWith (src.getPixelColour (x + shift, y), across);
                    const float amount = juce::jlimit (0.0f, 1.0f, juce::jmin (down, (float) (right - x) / feather));
                    dst.setPixelColour (x, y, src.getPixelColour (x, y).interpolatedWith (fill, amount));
                }
            }
        }

        // Over the fills: the notches of the gears the picture has none for, each its nearest painted one.
        for (const float notch : cut)
        {
            const float from = *std::min_element (rover.notches.begin(), rover.notches.end(),
                                                  [notch] (float a, float b) { return std::abs (a - notch) < std::abs (b - notch); });
            const int top = at (notch - f.above), bottom = at (notch + f.below), rows = at (from) - at (notch);
            for (int y = top; y < bottom; ++y)
            {
                const float down = juce::jmin ((float) (y - top + 1) / feather, (float) (bottom - y) / feather);
                for (int x = left; x < right; ++x)
                {
                    const float amount = juce::jlimit (0.0f, 1.0f, juce::jmin (down, (float) (right - x) / feather));
                    dst.setPixelColour (x, y, dst.getPixelColour (x, y).interpolatedWith (src.getPixelColour (x, y + rows), amount));
                }
            }
        }
        return img;
    }

    /** THE SLOT CUT SHORT OVER THE FIRST GEAR (SlotEnd): its top end moves down, the rows it leaves are
        plain plate from beside the slot, and the two meet across a seam as soft as the fill's edges. */
    juce::Image shortenSlot (juce::Image img, const Rover& rover)
    {
        const auto& s = rover.slotEnd;
        if (! img.isValid() || s.drop <= 0.0f)
            return img;

        const float k = (float) img.getWidth() / width;
        const auto at = [k] (float v) { return juce::roundToInt (v * k); };
        const auto original = img.createCopy();
        const juce::Image::BitmapData src (original, juce::Image::BitmapData::readOnly);
        juce::Image::BitmapData dst (img, juce::Image::BitmapData::readWrite);

        const int left = at (s.left), right = at (s.right), top = at (s.top), drop = at (s.drop);
        const int bottom = top + drop + at (s.end), shift = at (s.plateShift), plateTop = at (s.plateTop);
        const float feather = (float) juce::jmax (1, at (s.feather));

        for (int y = top; y < bottom; ++y)
        {
            const float down = juce::jmin (1.0f, (float) (y - top + 1) / feather);     // under the moved end the slot is as painted
            const float seam = juce::jlimit (0.0f, 1.0f, (float) (y - top - drop) / feather + 0.5f);     // plate to end

            for (int x = left; x < right; ++x)
            {
                const auto fill = src.getPixelColour (x - shift, plateTop + y - top).interpolatedWith (src.getPixelColour (x, y - drop), seam);
                const float amount = juce::jlimit (0.0f, 1.0f, juce::jmin (down, (float) (x - left + 1) / feather, (float) (right - x) / feather));
                dst.setPixelColour (x, y, src.getPixelColour (x, y).interpolatedWith (fill, amount));
            }
        }
        return img;
    }

    /** THE CORNER LAMPS' PLATES GROW ON THEIR OUTER LEGS (LampPlate): the plate inward of the cut moves
        inward, cut to its own outline; the room it leaves gets the plate's top edge, plain metal, the
        plate's bottom edge, each from where the picture has it plain; and down the room, INPUT's slot at a
        smaller size — its round ends whole, its straight run cut short. A picture that paints the corner
        slots itself grows its plates nothing. */
    juce::Image widenLampPlates (juce::Image img, const Rover& rover)
    {
        if (! img.isValid() || rover.lampPlateGrow <= 0.0f)
            return img;

        const float k = (float) img.getWidth() / width;
        const auto px = [k] (float v) { return juce::roundToInt (v * k); };
        const auto original = img.createCopy();
        juce::Graphics g (img);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);

        // `from` in the picture as painted, drawn over `to` here — both in the dashboard's space.
        const auto copy = [&] (juce::Rectangle<float> from, juce::Rectangle<float> to)
        {
            g.drawImage (original, px (to.getX()), px (to.getY()), px (to.getRight()) - px (to.getX()), px (to.getBottom()) - px (to.getY()),
                         px (from.getX()), px (from.getY()), px (from.getRight()) - px (from.getX()), px (from.getBottom()) - px (from.getY()));
        };

        const auto grow = rover.lampPlateGrow;
        const auto& inSlotRim = rover.inSlotRim;
        const auto inSlotEnd = rover.inSlotEnd, slotScale = rover.slotScale;

        for (const auto* plate : { &rover.gatePlate, &rover.limiterPlate })
        {
            {
                juce::Path outline;
                for (size_t i = 0; i < plate->outline.size(); ++i)
                {
                    const auto at = plate->outline[i].translated (plate->shift, 0.0f) * k;
                    if (i == 0) outline.startNewSubPath (at);
                    else        outline.lineTo (at);
                }
                outline.closeSubPath();
                const juce::Graphics::ScopedSaveState keep (g);
                g.reduceClipRegion (outline);
                copy (plate->moved, plate->moved.translated (plate->shift, 0.0f));
            }

            for (size_t i = 0; i < 3; ++i)
            {
                const float top = plate->rows[i], bottom = plate->rows[i + 1];
                copy ({ plate->from[i], top, grow, bottom - top }, { plate->room, top, grow, bottom - top });
            }

            const auto& slot = plate->slot;
            const float end = inSlotEnd * slotScale;
            const float run = slot.getHeight() / slotScale - 2.0f * inSlotEnd;
            copy (inSlotRim.withHeight (inSlotEnd), slot.withHeight (end));
            copy ({ inSlotRim.getX(), inSlotRim.getY() + inSlotEnd, inSlotRim.getWidth(), run },
                  { slot.getX(), slot.getY() + end, slot.getWidth(), slot.getHeight() - 2.0f * end });
            copy (inSlotRim.withTrimmedTop (inSlotRim.getHeight() - inSlotEnd), slot.withTrimmedTop (slot.getHeight() - end));
        }
        return img;
    }

    /** A RENDER'S METAL LIFTED: each colour raised to `gamma` (under 1 lightens the darks most), its alpha
        as it was. */
    juce::Image lifted (const juce::Image& img, float gamma)
    {
        if (! img.isValid())
            return img;
        auto out = img.convertedToFormat (juce::Image::ARGB).createCopy();
        juce::Image::BitmapData d (out, juce::Image::BitmapData::readWrite);
        for (int y = 0; y < d.height; ++y)
            for (int x = 0; x < d.width; ++x)
                if (const auto c = d.getPixelColour (x, y); c.getAlpha() > 0)
                    d.setPixelColour (x, y, juce::Colour::fromFloatRGBA (std::pow (c.getFloatRed(), gamma), std::pow (c.getFloatGreen(), gamma),
                                                                         std::pow (c.getFloatBlue(), gamma), c.getFloatAlpha()));
        return out;
    }

    /** A SOFT LIGHT THE SHAPE OF A RENDER: its alpha blurred, white, on a canvas `pad` wider all round —
        drawn tinted under the render, it keeps a dark control in sight on a dark panel. */
    juce::Image glowOf (const juce::Image& img, int pad)
    {
        if (! img.isValid())
            return {};
        juce::Image glow (juce::Image::ARGB, img.getWidth() + 2 * pad, img.getHeight() + 2 * pad, true);
        juce::Graphics g (glow);
        juce::DropShadow (juce::Colours::white, pad / 2, { pad, pad }).drawForImage (g, img);
        return glow;
    }

    /** The lever's glow is this much wider all round than the lever's render, in its own pixels — the
        number redpaw::Art::leverGlowPad lays it back with. */
    constexpr int leverGlowPad = 26;

    /** The corner faders' caps: the fader cap's metal lifted by this gamma. */
    constexpr float faderCapLift = 0.55f;
}

bool isRover (const juce::String& face)
{
    return std::any_of (rovers.begin(), rovers.end(), [&face] (const Rover& r) { return face == r.face; });
}

Pictures bakeRover (const juce::String& face, const Sources& sources)
{
    const auto& rover = *std::find_if (rovers.begin(), rovers.end(), [&face] (const Rover& r) { return face == r.face; });
    Pictures out;

    auto dashboard = widenLampPlates (widenRims (shortenSlot (fitNotches (sources.load ("dashboard.png"), rover), rover), rover), rover);
    std::array<juce::Image, 4> lit;
    if (rover.lampness != nullptr && dashboard.isValid())
        for (size_t i = 0; i < lit.size(); ++i)
            lit[i] = putOutLamp (dashboard, rover.lamps[i], width, rover.lampness);
    out.push_back ({ "dashboard.webp", dashboard });
    for (size_t i = 0; i < lit.size(); ++i)
        if (lit[i].isValid())
            out.push_back ({ "lamp-" + juce::String ((int) i) + ".webp", lit[i] });

    for (const char* name : { "knob-drive", "knob-tone", "socket", "key-frame-left", "key-frame-cell", "key-frame-right",
                              "lever", "slider-cap", "fader-cap",
                              "toggle-up", "toggle-down", "key-up", "key-down", "turbo-up", "turbo-down" })
        out.push_back ({ juce::String (name) + ".webp", sources.load (juce::String (name) + ".png") });

    const auto lever = sources.load ("lever.png");
    out.push_back ({ "lever-glow.webp", glowOf (lever, leverGlowPad) });
    out.push_back ({ "fader-cap-light.webp", lifted (sources.load ("fader-cap.png"), faderCapLift) });

    // ...and the render only some rovers carry: never another rover's.
    if (sources.has ("emblem.png"))
        out.push_back ({ "emblem.webp", juce::Image (sources.load ("emblem.png")) });

    return out;
}

} // namespace orbitamp::bake
