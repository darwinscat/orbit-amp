// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// What the metal world's art brings it, found as metal-gl.js finds its finds/ (the order the page applies them in is
// fixed by MetalWorld::furnish). JUCE-free and GL-free: the app, the site and metal_world_test read it alike.

#include "MetalFound.h"

#include <algorithm>
#include <cstring>

namespace orbitamp::metal
{

namespace
{
    using W = MetalWorld;

    bool endsWith (const std::string& s, const char* tail) { const size_t n = std::strlen (tail); return s.size() >= n && s.compare (s.size() - n, n, tail) == 0; }

    std::string textOf (const Art& art, const std::string& path)
    {
        if (! art.bytes) return {};
        const auto b = art.bytes (path);
        return std::string (b.begin(), b.end());
    }

    inline unsigned le16 (const std::uint8_t* p) noexcept { return (unsigned) p[0] | ((unsigned) p[1] << 8); }
    inline unsigned le24 (const std::uint8_t* p) noexcept { return le16 (p) | ((unsigned) p[2] << 16); }
}

std::array<int, 2> webpSize (const std::uint8_t* b, size_t size) noexcept
{
    if (b == nullptr || size < 30 || std::memcmp (b, "RIFF", 4) != 0 || std::memcmp (b + 8, "WEBP", 4) != 0)
        return { 0, 0 };
    if (std::memcmp (b + 12, "VP8 ", 4) == 0 && b[23] == 0x9d && b[24] == 0x01 && b[25] == 0x2a)
        return { (int) (le16 (b + 26) & 0x3fff), (int) (le16 (b + 28) & 0x3fff) };
    if (std::memcmp (b + 12, "VP8L", 4) == 0 && b[20] == 0x2f)
    {
        const std::uint32_t bits = (std::uint32_t) b[21] | ((std::uint32_t) b[22] << 8) | ((std::uint32_t) b[23] << 16) | ((std::uint32_t) b[24] << 24);
        return { (int) (bits & 0x3fff) + 1, (int) ((bits >> 14) & 0x3fff) + 1 };
    }
    if (std::memcmp (b + 12, "VP8X", 4) == 0)
        return { (int) le24 (b + 24) + 1, (int) le24 (b + 27) + 1 };
    return { 0, 0 };
}

Found find (const Art& art)
{
    Found f;
    if (! art.bytes || ! art.list)
        return f;
    // The owner's meshes, folder by folder by name (the page's finds/ listing), the guitars' index among them.
    std::vector<std::string> dirs;
    for (const auto& n : art.list ("world"))
        if (endsWith (n, "3d") && W::roleOf (n) != "dragonFile")
            dirs.push_back (n);
    std::sort (dirs.begin(), dirs.end());
    std::string gd;
    for (const auto& d : dirs) if (W::roleOf (d) == "guitars") { gd = d; break; }
    // THE DRAGON (the rogue planet's): its poses' heads for its fire, its credit first.
    {
        const auto anim = art.bytes ("world/dragon3d/dragon.anim");
        const auto meshBytes = art.bytes ("world/dragon3d/dragon.mesh");
        const auto m = W::readMesh (meshBytes.data(), meshBytes.size());
        if (m.ok && ! anim.empty())
            f.finds.dragonHead = W::readDragonHead (anim.data(), anim.size(), m.nv);
        if (const auto c = W::creditOf (textOf (art, "world/dragon3d/dragon.txt")))
            f.credits.push_back (*c);
    }
    // THE LETTERS: letters.txt and a picture per text, packed into one atlas as the page packs them.
    {
        std::vector<W::Png> pngs;
        if (art.pictureSize)
            for (const auto& n : art.list ("world/letters"))
                if (endsWith (n, ".webp"))
                {
                    const auto size = art.pictureSize ("world/letters/" + n);
                    if (size[0] > 0 && size[1] > 0)
                        pngs.push_back ({ n.substr (0, n.size() - 5) + ".png", size[0], size[1] });   // the table names its PNG
                }
        f.letters = W::packLetters (textOf (art, "world/letters/letters.txt"), pngs);
        f.finds.letters = f.letters.table;
    }
    // THE GUITARS: the index and the groves' deal.
    if (! gd.empty())
    {
        f.guitarSet = W::readGuitars (textOf (art, "world/" + gd + "/guitars.txt"));
        f.deal = W::dealGroves (f.guitarSet);
        f.finds.guitars = f.deal.items;
        f.finds.guitarNames = f.deal.names;
    }
    // THE LANDMARKS AND THE REST: each folder's first mesh, its credit, its top a target for the lightning.
    for (const auto& d : dirs)
    {
        if (d == gd) continue;
        std::string meshName;
        for (const auto& n : art.list ("world/" + d)) if (endsWith (n, ".mesh")) { meshName = n; break; }
        if (meshName.empty()) continue;
        const std::string path = "world/" + d + "/" + meshName;
        if (const auto c = W::creditOf (textOf (art, path.substr (0, path.size() - 5) + ".txt")))
            if (std::find (f.credits.begin(), f.credits.end(), *c) == f.credits.end()) f.credits.push_back (*c);
        const std::string role = W::roleOf (d);
        const auto b = art.bytes (path);
        const auto m = W::readMesh (b.data(), b.size());
        if (! m.ok) continue;
        f.meshOf[role] = path;
        if (const auto top = W::landmarkTop (role, m)) f.finds.landmarkTops.push_back ({ role, *top });
    }
    for (const int c : f.deal.chosen)
        f.meshOf["guitar:" + f.guitarSet[(size_t) c].name] = "world/" + gd + "/" + f.guitarSet[(size_t) c].file + ".mesh";
    f.bannersTxt = textOf (art, "world/banners/banners.txt");
    return f;
}

} // namespace orbitamp::metal
