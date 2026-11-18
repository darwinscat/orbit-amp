// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "UnderwaterArt.h"

#include "../../face/Renders.h"

#include <juce_core/juce_core.h>
#include <webp/decode.h>

namespace orbitamp::underwater
{

UnderwaterGl::Picture decodeWebp (const void* data, size_t size)
{
    UnderwaterGl::Picture p;
    int w = 0, h = 0;
    const auto* bytes = static_cast<const uint8_t*> (data);
    if (bytes == nullptr || size == 0 || WebPGetInfo (bytes, size, &w, &h) == 0 || w <= 0 || h <= 0)
        return p;
    p.rgba.resize ((size_t) w * (size_t) h * 4);
    if (WebPDecodeRGBAInto (bytes, size, p.rgba.data(), p.rgba.size(), w * 4) == nullptr)
        return {};
    p.width = w;
    p.height = h;
    return p;
}

namespace
{
    constexpr const char* product = "DeepGrooveBass";
    constexpr const char* folderVariable = "UNDERWATER_ART_DIR";

    /** A picture's file, read and decoded when it is asked for; none when it is not there. */
    std::function<UnderwaterGl::Picture()> picture (const char* name)
    {
        if (! face::renders::has (product, name, folderVariable))
            return {};
        return [file = face::renders::fileOf (product, name, folderVariable)]
        {
            juce::MemoryBlock bytes;
            return file.loadFileAsData (bytes) ? decodeWebp (bytes.getData(), bytes.getSize()) : UnderwaterGl::Picture {};
        };
    }
}

UnderwaterGl::Art shippedArt()
{
    UnderwaterGl::Art art;
    art.dashboard = picture ("dashboard.webp");
    art.sea       = picture ("sea.webp");
    art.atlas     = picture ("atlas.webp");
    art.sprites   = face::renders::text (product, "sprites.txt", folderVariable).toStdString();
    return art;
}

} // namespace orbitamp::underwater
