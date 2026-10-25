// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "Renders.h"

#include "../device/FactorySet.h"

#include <juce_core/juce_core.h>

#include <webp/decode.h>

#include <vector>

namespace orbitamp::face::renders
{

namespace
{
    juce::Image decodeBytes (const juce::String& name, const void* data, size_t size)
    {
        return name.endsWithIgnoreCase (".webp") ? decodeWebp (data, size) : juce::ImageFileFormat::loadFrom (data, size);
    }
}

juce::File folder (const juce::String& product)
{
    return device::FactorySet::location().getChildFile ("Faces").getChildFile (product);
}

juce::File fileOf (const juce::String& product, const char* name, const char* folderVariable)
{
    if (const auto dir = juce::SystemStats::getEnvironmentVariable (folderVariable, {}); dir.isNotEmpty())
        if (const auto file = juce::File (dir).getChildFile (name); file.existsAsFile())
            return file;

    return folder (product).getChildFile (name);
}

bool has (const juce::String& product, const char* name, const char* folderVariable)
{
    return fileOf (product, name, folderVariable).existsAsFile();
}

juce::Image decode (const juce::String& product, const char* name, const char* folderVariable)
{
    juce::Image img;

    if (juce::MemoryBlock bytes; fileOf (product, name, folderVariable).loadFileAsData (bytes))
        img = decodeBytes (name, bytes.getData(), bytes.getSize());

    jassert (img.isValid() || ! folder (product).isDirectory());   // a name the face's own folder lacks is a typo
    return img;
}

juce::Image load (const juce::String& product, const char* name, const char* folderVariable)
{
    const auto img = decode (product, name, folderVariable);
    return img.isValid() ? img.convertedToFormat (juce::Image::ARGB) : img;
}

juce::String text (const juce::String& product, const char* name, const char* folderVariable)
{
    const auto file = fileOf (product, name, folderVariable);
    return file.existsAsFile() ? file.loadFileAsString() : juce::String();
}

juce::Image decodeWebp (const void* bytes, size_t size)
{
    if (bytes == nullptr || size == 0)
        return {};
    const auto* data = static_cast<const uint8_t*> (bytes);

    // What the picture is before a byte of it is held: one picture, no animation, of a size WebP can hold.
    constexpr int maxSide = 16383;      // WebP's own ceiling
    WebPBitstreamFeatures f;
    if (WebPGetFeatures (data, size, &f) != VP8_STATUS_OK || f.has_animation != 0
        || f.width <= 0 || f.height <= 0 || f.width > maxSide || f.height > maxSide)
        return {};

    const int w = f.width, h = f.height;
    const size_t rowBytes = (size_t) w * 4;
    std::vector<uint8_t> straight (rowBytes * (size_t) h);
    if (WebPDecodeBGRAInto (data, size, straight.data(), straight.size(), (int) rowBytes) == nullptr)
        return {};

    juce::Image img (juce::Image::ARGB, w, h, false);
    const juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < h; ++y)
    {
        const uint8_t* in = straight.data() + (size_t) y * rowBytes;
        for (int x = 0; x < w; ++x, in += 4)
        {
            const auto a = in[3];
            juce::PixelARGB px;
            px.setARGB (a, premultiplied (in[2], a), premultiplied (in[1], a), premultiplied (in[0], a));
            *reinterpret_cast<juce::PixelARGB*> (bd.getPixelPointer (x, y)) = px;
        }
    }
    return img;
}

} // namespace orbitamp::face::renders
