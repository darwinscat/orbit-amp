// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "Renders.h"

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

juce::Image decode (const char* name, const Binary& binary, const char* folderVariable)
{
    juce::Image img;

    if (const auto dir = juce::SystemStats::getEnvironmentVariable (folderVariable, {}); dir.isNotEmpty())
    {
        const auto file = juce::File (dir).getChildFile (name);
        if (juce::MemoryBlock bytes; file.existsAsFile() && file.loadFileAsData (bytes))
            img = decodeBytes (name, bytes.getData(), bytes.getSize());
    }

    for (int i = 0; ! img.isValid() && i < binary.count; ++i)
        if (juce::String (binary.originalName (binary.names[i])) == name)
        {
            int size = 0;
            if (const auto* data = binary.data (binary.names[i], size))
                img = decodeBytes (name, data, (size_t) size);
        }

    jassert (img.isValid());   // a name that is neither on disk nor in the binary is a typo
    return img;
}

juce::Image load (const char* name, const Binary& binary, const char* folderVariable)
{
    const auto img = decode (name, binary, folderVariable);
    return img.isValid() ? img.convertedToFormat (juce::Image::ARGB) : img;
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
