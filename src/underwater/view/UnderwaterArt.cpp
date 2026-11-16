// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "UnderwaterArt.h"

#include <UnderwaterData.h>     // resources/underwater, as binary data
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

UnderwaterGl::Art shippedArt()
{
    UnderwaterGl::Art art;
    art.dashboard = [] { return decodeWebp (UnderwaterData::dashboard_webp, (size_t) UnderwaterData::dashboard_webpSize); };
    art.sea       = [] { return decodeWebp (UnderwaterData::sea_webp, (size_t) UnderwaterData::sea_webpSize); };
    art.atlas     = [] { return decodeWebp (UnderwaterData::atlas_webp, (size_t) UnderwaterData::atlas_webpSize); };
    art.sprites.assign (UnderwaterData::sprites_txt, (size_t) UnderwaterData::sprites_txtSize);
    return art;
}

} // namespace orbitamp::underwater
