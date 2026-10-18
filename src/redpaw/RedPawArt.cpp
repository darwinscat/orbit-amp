// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "RedPawArt.h"
#include "RedPawLineup.h"
#include "RedPawSkin.h"

#include "../face/Renders.h"

namespace orbitamp::redpaw
{

namespace
{
    /** A render of the product's own, by the rover's file name (RedPawSkin.h). */
    juce::Image load (const char* name) { return face::renders::load (name, skin().renders, skin().rendersFolderVariable); }

    /** A render only some rovers carry: none is no image, not a typo. */
    juce::Image loadOwn (const char* name)
    {
        const auto& binary = skin().renders;
        for (int i = 0; i < binary.count; ++i)
            if (juce::String (binary.originalName (binary.names[i])) == name)
                return load (name);
        return {};
    }

    /** THE UPPER RIMS OF REVERB'S AND DELAY'S FRAMES GROW, so that the words over each well stand ON its
        frame (layout::rims). The frame's top — its shadow and the rim's lit edge — goes `rimLift` higher,
        and the room it leaves is filled with the frame's own face: along the rim, the rim's face band
        mirrored back and forth; on the end caps, the plain face under their top screws, so the screws
        keep their shape. Numbers are in the dashboard's 1672-wide space, measured off it. A picture with
        room for the words as it is painted lifts them nothing. */
    juce::Image widenRims (juce::Image img)
    {
        if (! img.isValid() || layout::rimLift <= 0.0f)
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

    /** THE NOTCHES PAST THE LAST GEAR ARE FILLED IN (layout::NotchFill): a rover of fewer gears than its
        picture has notches gets its slot straight there. Over each such notch the slot's edge is the
        straight run above it, mirrored down and back, and the plate beside it is the plate further right
        on the same rows; faded into the picture at the fill's top, bottom and right. Numbers are in the
        dashboard's 1672-wide space, measured off it. */
    juce::Image fillSpareNotches (juce::Image img)
    {
        if (! img.isValid() || numGears() >= (int) layout::notches.size())
            return img;

        const auto& f = layout::notchFill;
        const float k = (float) img.getWidth() / layout::width;
        const auto at = [k] (float v) { return juce::roundToInt (v * k); };
        const auto original = img.createCopy();
        const juce::Image::BitmapData src (original, juce::Image::BitmapData::readOnly);
        juce::Image::BitmapData dst (img, juce::Image::BitmapData::readWrite);

        const int left = at (f.left), seam = at (f.seam), right = at (f.right), shift = at (f.plateShift);
        const int run = juce::jmax (1, at (f.run));
        const float feather = (float) juce::jmax (1, at (f.feather));

        for (size_t n = (size_t) numGears(); n < layout::notches.size(); ++n)
        {
            const int top = at (layout::notches[n] - f.above), bottom = at (layout::notches[n] + f.below);
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
        return img;
    }

    /** THE CORNER LAMPS' PLATES GROW ON THEIR OUTER LEGS (layout::LampPlate): the plate inward of the cut
        moves inward, cut to its own outline; the room it leaves gets the plate's top edge, plain metal, the plate's bottom edge,
        each from where the picture has it plain; and down the room, INPUT's slot at a smaller size — its
        round ends whole, its straight run cut short. A picture that paints the corner slots itself grows
        its plates nothing. */
    juce::Image widenLampPlates (juce::Image img)
    {
        if (! img.isValid() || layout::lampPlateGrow <= 0.0f)
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
}

const Art& Art::get()
{
    static const Art art = []
    {
        Art a;
        a.bg         = widenLampPlates (widenRims (fillSpareNotches (load ("dashboard.png"))));
        if (skin().lampness != nullptr)
            a.bg = face::renders::putOutLamps (a.bg, layout::lamps, layout::width, skin().lampness, a.lampLit);
        a.knobDrive  = load ("knob-drive.png");
        a.knobTone   = load ("knob-tone.png");
        a.lever      = load ("lever.png");
        a.leverGlow  = glowOf (a.lever, Art::leverGlowPad);
        a.sliderCap  = load ("slider-cap.png");
        a.faderCap   = load ("fader-cap.png");
        a.faderCapLight = lifted (a.faderCap, 0.55f);
        a.toggleUp   = load ("toggle-up.png");
        a.toggleDown = load ("toggle-down.png");
        a.keyUp      = load ("key-up.png");
        a.keyDown    = load ("key-down.png");
        a.turboUp    = load ("turbo-up.png");
        a.turboDown  = load ("turbo-down.png");
        a.turboLid   = loadOwn ("turbo-lid.png");
        a.emblem     = loadOwn ("emblem.png");
        return a;
    }();
    return art;
}

juce::Image Art::still()
{
    return loadOwn ("still.jpg");
}

juce::String Art::mark()
{
    const auto& binary = skin().renders;
    for (int i = 0; i < binary.count; ++i)
        if (juce::String (binary.originalName (binary.names[i])) == "mark.svg")
        {
            int size = 0;
            if (const char* svg = binary.data (binary.names[i], size); svg != nullptr)
                return juce::String::fromUTF8 (svg, size);
        }
    return {};
}

} // namespace orbitamp::redpaw
