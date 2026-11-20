// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "california/view/CaliforniaWorld.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace orbitamp::california::shipped
{

/** THE FILES THE GPU'S HALF READS AT RUN TIME (CaliforniaGl::makeArt, as the prototype's view loads them), by the paths
    it asks for them under the art's folder (CaliforniaArt.h): the cabin's, every sprite's atlas and table, the signs',
    the flags', the chalk's, and every mesh with its table and each texture its table names — the prototype's .png read
    as .webp — each table one its parser takes. `has (rel)`: the file is there with something in it; `read (rel)`: its
    bytes. What is missing or will not parse, in words; none when the world finds all it reads. Shared by
    california_world_test (the source tree's resources/california) and the van's face gate (the factory folder's
    Faces/CaliforniaAmp, as the build lays it out). */
template <typename Has, typename Read>
std::vector<std::string> missing (Has has, Read read)
{
    using W = CaliforniaWorld;
    std::vector<std::string> out;
    const auto webp = [] (std::string name) { return name.size() > 4 && name.compare (name.size() - 4, 4, ".png") == 0 ? name.substr (0, name.size() - 4) + ".webp" : name; };
    const auto need = [&out] (bool ok, const std::string& what) { if (! ok) out.push_back (what); };
    for (const char* f : { "dashboard.webp", "dashboard_pendant.webp", "label.webp" })
        need (has (f), f);
    need (W::parsePendantBox (read ("dashboard_pendant.txt")).has_value(), "dashboard_pendant.txt: a box and a pivot");
    W w;
    w.build();
    std::vector<std::string> sprites { "dish" }, meshes { "van3d/van", "saucer3d/saucer", "yellowsub3d/yellowsub" };
    for (const auto& sp : w.spritePlaces())
        if (sp.mesh.empty()) sprites.push_back (sp.name);
        else if (std::find (meshes.begin(), meshes.end(), sp.mesh) == meshes.end()) meshes.push_back (sp.mesh);
    for (const auto& s : sprites)
        need (has ("finds/" + s + "/atlas.webp") && W::parseSpriteTable (read ("finds/" + s + "/sprites.txt")).scale > 0, "the sprite " + s);
    need (has ("finds/signs/atlas.webp") && ! W::parseSigns (read ("finds/signs/signs.txt")).empty(), "the signs");
    need (has ("finds/flags/atlas.webp") && ! W::parseFlagCells (read ("finds/flags/flags.txt"), 576, 256).empty(), "the flags");
    need (has ("finds/chalk/atlas.webp") && ! W::parseChalk (read ("finds/chalk/chalk.txt"), 2048, 1024).empty(), "the chalk");
    for (const auto& m : meshes)
    {
        const auto bytes = read ("finds/" + m + ".mesh");
        need (W::parseCbMesh (reinterpret_cast<const uint8_t*> (bytes.data()), bytes.size()).has_value(), "the mesh " + m);
        const auto dir = m.substr (0, m.find_last_of ('/') + 1);
        for (const auto& tex : W::parseMeshInfo (read ("finds/" + m + ".txt")).textures)
            if (! tex.empty())
                need (has ("finds/" + dir + webp (tex)), "the texture " + dir + tex);
    }
    return out;
}

} // namespace orbitamp::california::shipped
