// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "metal/view/MetalFound.h"
#include "metal/view/MetalWorld.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace orbitamp::metal::shipped
{

/** THE FILES THE METAL WORLD READS AT RUN TIME (MetalDrive's glass, metal::find and MetalGl::makeArt), by the paths it
    asks for them under the art's folder (MetalArt.h): the cabin's two plates; what the finds bring the world — the
    dragon's head, the groves' guitars and their names, the landmarks' tops, the letters' table with a WebP for every text
    it names and the CENSORED cell, the banners' table, eight credits (the lava crawler's file has none) — and what the
    renderer draws them with: the banners' atlas, the eight meshes and the groves' eight guitars, each a CBMESH1, and the
    dragon's mesh with its wing-beat. The art needs `bytes`, `list` and `pictureSize`; `picture` is not asked. What is
    missing or will not read, in words; none when the world finds all it reads. Shared by metal_world_test (the source
    tree's resources/metal) and the E530's face gate (the factory folder's Faces/MetalAmp, as the build lays it out). */
inline std::vector<std::string> missing (const Art& art)
{
    using W = MetalWorld;
    std::vector<std::string> out;
    const auto need = [&out] (bool ok, const std::string& what) { if (! ok) out.push_back (what); };
    const auto there = [&art] (const std::string& path) { const auto s = art.pictureSize (path); return s[0] > 0 && s[1] > 0; };
    const auto mesh = [&art] (const std::string& path) { const auto b = art.bytes (path); return W::readMesh (b.data(), b.size()).ok; };

    for (const char* plate : { "dashboard.webp", "dashboard_flash.webp" })
        need (there (plate), plate);

    const auto found = find (art);
    const auto& f = found.finds;
    need (f.dragonHead.has_value(), "the dragon's head (world/dragon3d)");
    need (! f.guitars.empty() && f.guitarNames.size() == f.guitars.size(), "the groves' guitars and their names (world/guitars3d/guitars.txt)");
    need (! f.landmarkTops.empty(), "the landmarks' tops (world/*3d)");
    need (! f.letters.empty() && ! found.letters.pieces.empty() && found.letters.censored.has_value(),
          "the letters' table and the CENSORED cell (world/letters/letters.txt)");
    for (const auto& piece : found.letters.pieces)
        if (! piece.file.empty())
        {
            const auto webp = "world/letters/" + piece.file.substr (0, piece.file.size() - 4) + ".webp";
            need (there (webp), webp);
        }
    need (! found.bannersTxt.empty(), "the banners' table (world/banners/banners.txt)");
    need (there ("world/banners/atlas.webp"), "world/banners/atlas.webp");
    need (found.credits.size() == 8, "eight credits, the meshes' .txt (" + std::to_string (found.credits.size()) + ")");
    need (found.meshOf.size() == 16, "the eight meshes and the groves' eight guitars (" + std::to_string (found.meshOf.size()) + " found)");
    for (const auto& [role, path] : found.meshOf)
        need (mesh (path), "the mesh " + path);
    need (mesh ("world/dragon3d/dragon.mesh"), "world/dragon3d/dragon.mesh");
    const auto anim = art.bytes ("world/dragon3d/dragon.anim");
    need (anim.size() >= 24 && std::memcmp (anim.data(), "CBANIM1", 7) == 0, "world/dragon3d/dragon.anim");
    return out;
}

} // namespace orbitamp::metal::shipped
