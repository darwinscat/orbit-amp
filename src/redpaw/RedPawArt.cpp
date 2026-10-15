// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "RedPawArt.h"

#include "../face/Renders.h"

#include <RedPawData.h>

namespace orbitamp::redpaw
{

namespace
{
    const face::renders::Binary binary { RedPawData::namedResourceList, RedPawData::namedResourceListSize,
                                         RedPawData::getNamedResourceOriginalFilename, RedPawData::getNamedResource };

    juce::Image load (const char* name) { return face::renders::load (name, binary, "REDPAW_ART_DIR"); }

    /** THE UPPER RIMS OF REVERB'S AND DELAY'S FRAMES GROW, so that the words over each well stand ON its
        frame (layout::rims). The frame's top — its shadow and the rim's lit edge — goes `rimLift` higher,
        and the room it leaves is filled with the frame's own face: along the rim, the rim's face band
        mirrored back and forth; on the end caps, the plain face under their top screws, so the screws
        keep their shape. Numbers are in the dashboard's 1672-wide space, measured off it. */
    juce::Image widenRims (juce::Image img)
    {
        if (! img.isValid())
            return img;

        const float k = (float) img.getWidth() / layout::width;
        const auto original = img.createCopy();
        const juce::Image::BitmapData src (original, juce::Image::BitmapData::readOnly);
        juce::Image::BitmapData dst (img, juce::Image::BitmapData::readWrite);
        const int lift = juce::roundToInt (layout::rimLift * k);
        const auto at = [k] (float v) { return juce::roundToInt (v * k); };

        for (const auto& rim : layout::rims)
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

    /** THE CORNER LAMPS' PLATES GROW ON THEIR OUTER LEGS (layout::LampPlate): the plate inward of the cut
        moves inward, cut to its own outline; the room it leaves gets the plate's top edge, plain metal, the plate's bottom edge,
        each from where the picture has it plain; and down the room, INPUT's slot at a smaller size — its
        round ends whole, its straight run cut short. */
    juce::Image widenLampPlates (juce::Image img)
    {
        if (! img.isValid())
            return img;

        using namespace layout;
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

        for (const auto* plate : { &gatePlate, &limiterPlate })
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
                copy ({ plate->from[i], top, lampPlateGrow, bottom - top }, { plate->room, top, lampPlateGrow, bottom - top });
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

    /** THE LAMPS GO OUT (face/Renders.h). On a rusted dashboard warm is everywhere: a lamp is what is
        both warm AND bright — its red well over its blue, and the whole of it lit — so the rust and the
        dust round it stay as they were painted. */
    float lampness (juce::Colour c)
    {
        const float r = c.getFloatRed(), g = c.getFloatGreen(), b = c.getFloatBlue();
        const float warm   = (r - b - 0.25f) / 0.25f;
        const float bright = (0.3f * r + 0.59f * g + 0.11f * b - 0.38f) / 0.18f;
        return juce::jmin (juce::jlimit (0.0f, 1.0f, warm), juce::jlimit (0.0f, 1.0f, bright));
    }
}

const Art& Art::get()
{
    static const Art art = []
    {
        Art a;
        a.bg         = face::renders::putOutLamps (widenLampPlates (widenRims (load ("dashboard.png"))), layout::lamps, layout::width, lampness, a.lampLit);
        a.knobDrive  = load ("knob-drive.png");
        a.knobTone   = load ("knob-tone.png");
        a.lever      = load ("lever.png");
        a.sliderCap  = load ("slider-cap.png");
        a.faderCap   = load ("fader-cap.png");
        a.toggleUp   = load ("toggle-up.png");
        a.toggleDown = load ("toggle-down.png");
        a.keyUp      = load ("key-up.png");
        a.keyDown    = load ("key-down.png");
        a.turboUp    = load ("turbo-up.png");
        a.turboDown  = load ("turbo-down.png");
        return a;
    }();
    return art;
}

} // namespace orbitamp::redpaw
