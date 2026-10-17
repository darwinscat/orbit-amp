// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "Renders.h"

#include <juce_core/juce_core.h>

namespace orbitamp::face::renders
{

juce::Image decode (const char* name, const Binary& binary, const char* folderVariable)
{
    juce::Image img;

    if (const auto dir = juce::SystemStats::getEnvironmentVariable (folderVariable, {}); dir.isNotEmpty())
        img = juce::ImageFileFormat::loadFrom (juce::File (dir).getChildFile (name));

    for (int i = 0; ! img.isValid() && i < binary.count; ++i)
        if (juce::String (binary.originalName (binary.names[i])) == name)
        {
            int size = 0;
            if (const auto* data = binary.data (binary.names[i], size))
                img = juce::ImageFileFormat::loadFrom (data, (size_t) size);
        }

    jassert (img.isValid());   // a name that is neither on disk nor in the binary is a typo
    return img;
}

juce::Image load (const char* name, const Binary& binary, const char* folderVariable)
{
    const auto img = decode (name, binary, folderVariable);
    return img.isValid() ? img.convertedToFormat (juce::Image::ARGB) : img;
}

juce::Image putOutLamp (juce::Image& img, juce::Rectangle<float> stageBox, float stageWidth,
                        const std::function<float (juce::Colour)>& lampness)
{
    const float k = (float) img.getWidth() / stageWidth;
    const auto box = (stageBox * k).getSmallestIntegerContainer().getIntersection (img.getBounds());
    juce::Image lit (juce::Image::ARGB, juce::jmax (1, box.getWidth()), juce::jmax (1, box.getHeight()), true);
    if (box.isEmpty())
        return lit;

    juce::Image::BitmapData bd (img, box.getX(), box.getY(), box.getWidth(), box.getHeight(), juce::Image::BitmapData::readWrite);
    juce::Image::BitmapData out (lit, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < box.getHeight(); ++y)
        for (int x = 0; x < box.getWidth(); ++x)
        {
            const auto c = bd.getPixelColour (x, y);
            const float s = juce::jlimit (0.0f, 1.0f, lampness (c));
            if (s <= 0.0f)
                continue;

            const float r = c.getFloatRed(), g = c.getFloatGreen(), b = c.getFloatBlue();
            const float lum = 0.3f * r + 0.59f * g + 0.11f * b;
            const auto off = juce::Colour::fromFloatRGBA (lum * 0.30f + 0.03f, lum * 0.25f + 0.028f, lum * 0.24f + 0.03f, c.getFloatAlpha());
            out.setPixelColour (x, y, c);
            bd.setPixelColour (x, y, c.interpolatedWith (off, s));
        }
    return lit;
}

} // namespace orbitamp::face::renders
