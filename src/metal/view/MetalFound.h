// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "MetalWorld.h"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace orbitamp::metal
{

/** A picture for a texture: straight RGBA bytes, the top row first, as a decoder hands them over. */
struct Picture
{
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
    bool valid() const noexcept { return width > 0 && height > 0 && rgba.size() == (size_t) width * (size_t) height * 4; }
};

/** THE ART THE METAL WORLD READS AT RUN TIME, by its path in the art's folder (resources/metal as the factory folder
    lays it out): the plates dashboard.webp and dashboard_flash.webp, and world/ — the owner's meshes (<name>3d/, a
    CBMESH1 and its credit's .txt), guitars3d/guitars.txt and the groves' meshes, letters/ (letters.txt and a WebP
    per text), banners/ (atlas.webp and banners.txt), dragon3d/ (dragon.mesh, dragon.anim, dragon.txt). Each read
    when asked, so nothing is held between. A function left empty, or a file that is not there, leaves that part out:
    no plates, no cabin; no mesh, the procedural stand-in or nothing. No JUCE: the app's reads the factory folder
    (MetalArt.h), a test's or the site's its own. */
struct Art
{
    std::function<std::vector<std::uint8_t> (const std::string& path)> bytes;      // empty: not there
    std::function<std::vector<std::string> (const std::string& folder)> list;      // the names in a folder
    std::function<Picture (const std::string& path)> picture;                      // a WebP decoded; invalid: not there
    std::function<std::array<int, 2> (const std::string& path)> pictureSize;       // its size from its header
    std::function<Picture()> censored;      // the word CENSORED, white bold on clear, 1024 x 220 (the page's canvas)
};

/** WHAT THE FINDS BRING THE WORLD, read from the art as the page reads its finds/ (MetalWorld::furnish takes it),
    and what the renderer draws them with: per role its mesh's path (the groves' guitars as "guitar:<name>"), the
    groves' guitars (their set, the deal), the letters' atlas, the banners' table, the credits (the dragon's first). */
struct Found
{
    MetalWorld::Finds finds;
    std::map<std::string, std::string> meshOf;
    std::vector<MetalWorld::GuitarEntry> guitarSet;
    MetalWorld::GroveDeal deal;
    MetalWorld::LetterAtlas letters;
    std::string bannersTxt;
    std::vector<std::string> credits;
};

/** The finds in the art, as the page finds them: the folders ending in 3d by name (the dragon's apart), each one's first
    mesh, its credit and its top for the lightning; the guitars' index and its groves; the letters' table with the
    WebP of each PNG it names (by the same name), packed as the page packs them; the banners' table. */
Found find (const Art& art);

/** A WebP's width and height from its header (VP8, VP8L or VP8X), 0 0 when the bytes are not one. */
std::array<int, 2> webpSize (const std::uint8_t* bytes, size_t size) noexcept;

} // namespace orbitamp::metal
