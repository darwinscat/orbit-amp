// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "WebImages.h"

#include <webp/encode.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace orbitamp::face::web
{

bool hasHoles (const juce::Image& img)
{
    if (! img.isValid() || ! img.hasAlphaChannel())
        return false;
    const juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly);
    for (int y = 0; y < bd.height; ++y)
        for (int x = 0; x < bd.width; ++x)
            if (reinterpret_cast<const juce::PixelARGB*> (bd.getPixelPointer (x, y))->getAlpha() != 255)
                return true;
    return false;
}

juce::MemoryBlock encode (const juce::Image& source, float colourQuality)
{
    if (! source.isValid())
        return {};
    const auto img = source.convertedToFormat (juce::Image::ARGB);
    const int w = img.getWidth(), h = img.getHeight();
    if (w > WEBP_MAX_DIMENSION || h > WEBP_MAX_DIMENSION)
        return {};

    // The pixels as straight colour, B G R (A) in a row — with no alpha at all where there is no hole.
    const bool alpha = hasHoles (img);
    const int channels = alpha ? 4 : 3;
    std::vector<std::uint8_t> straight ((size_t) w * (size_t) h * (size_t) channels);
    {
        const juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly);
        auto* o = straight.data();
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                auto px = *reinterpret_cast<const juce::PixelARGB*> (bd.getPixelPointer (x, y));
                px.unpremultiply();
                *o++ = px.getBlue(); *o++ = px.getGreen(); *o++ = px.getRed();
                if (alpha)
                    *o++ = px.getAlpha();
            }
    }

    // cwebp -q <quality> -m 6 [-alpha_q 100 | -noalpha]: everything else as the encoder's defaults leave it,
    // and the picture imported straight into YUV, as cwebp leaves a lossy one (use_argb unset).
    WebPConfig config;
    if (! WebPConfigInit (&config))
        return {};
    config.quality = juce::jlimit (0.0f, 100.0f, colourQuality);
    config.method = 6;
    config.alpha_quality = 100;
    if (! WebPValidateConfig (&config))
        return {};

    WebPPicture picture;
    if (! WebPPictureInit (&picture))
        return {};
    picture.width = w;
    picture.height = h;
    const int imported = alpha ? WebPPictureImportBGRA (&picture, straight.data(), w * channels)
                               : WebPPictureImportBGR (&picture, straight.data(), w * channels);
    juce::MemoryBlock bytes;
    if (imported != 0)
    {
        WebPMemoryWriter writer;
        WebPMemoryWriterInit (&writer);
        picture.writer = WebPMemoryWrite;
        picture.custom_ptr = &writer;
        if (WebPEncode (&config, &picture) != 0 && writer.size > 0)
            bytes.append (writer.mem, writer.size);
        WebPMemoryWriterClear (&writer);
    }
    WebPPictureFree (&picture);
    return bytes;
}

bool write (const juce::File& file, const juce::Image& img, float colourQuality)
{
    const auto bytes = encode (img, colourQuality);
    if (bytes.isEmpty())
        return false;
    file.getParentDirectory().createDirectory();
    return file.replaceWithData (bytes.getData(), bytes.getSize());
}

juce::Image scaled (const juce::Image& img, int width, int height)
{
    width = std::max (1, width);
    height = std::max (1, height);
    auto step = img;
    while (step.getWidth() > 2 * width || step.getHeight() > 2 * height)
        step = step.rescaled (std::max (width, step.getWidth() / 2), std::max (height, step.getHeight() / 2),
                              juce::Graphics::highResamplingQuality);
    if (step.getWidth() != width || step.getHeight() != height)
        step = step.rescaled (width, height, juce::Graphics::highResamplingQuality);
    return step;
}

juce::var threeNames (const juce::String& stem, const juce::String& key)
{
    auto* o = new juce::DynamicObject();
    o->setProperty (key, stem + ".webp");
    o->setProperty (key + "1x", stem + "@1x.webp");
    o->setProperty (key + "Half", stem + "@half.webp");
    return juce::var (o);
}

juce::var writeThreeSizes (const juce::File& dir, const juce::String& stem, const juce::Image& full,
                           const juce::String& key, float halfQuality)
{
    if (! full.isValid())
        return {};
    const int w = full.getWidth(), h = full.getHeight();
    write (dir.getChildFile (stem + ".webp"), full);
    write (dir.getChildFile (stem + "@1x.webp"), scaled (full, w / 2, h / 2));
    write (dir.getChildFile (stem + "@half.webp"), scaled (full, w / 4, h / 4), halfQuality);
    return threeNames (stem, key);
}

juce::String spriteFile (const juce::String& stem)
{
    return "sprites/" + stem + ".webp";
}

juce::var writeSprite (const juce::File& dir, const juce::String& stem, const juce::Image& img)
{
    const auto file = spriteFile (stem);
    return write (dir.getChildFile (file), img) ? juce::var (file) : juce::var();
}

} // namespace orbitamp::face::web
