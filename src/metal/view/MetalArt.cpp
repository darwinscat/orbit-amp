// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "MetalArt.h"

#include "../../face/Renders.h"

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <webp/decode.h>

#include <algorithm>

namespace orbitamp::metal
{

namespace
{
    constexpr const char* product = "FerroClawAmp";
    constexpr const char* folderVariable = "METAL_ART_DIR";

    /** The folder a path's file is read from: METAL_ART_DIR's where it has it, the product's own otherwise. */
    juce::File fileAt (const std::string& path)
    {
        return face::renders::fileOf (product, path.c_str(), folderVariable);
    }
    juce::File folderAt (const std::string& path)
    {
        if (const auto dir = juce::SystemStats::getEnvironmentVariable (folderVariable, {}); dir.isNotEmpty())
            if (const auto d = juce::File (dir).getChildFile (path); d.isDirectory())
                return d;
        return face::renders::folder (product).getChildFile (path);
    }

    MetalGl::Picture decodeWebp (const void* data, size_t size)
    {
        MetalGl::Picture p;
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

    /** The word CENSORED as the page draws it on its canvas: white bold Helvetica, 190 px, centred on (512, 118) of a
        1024 x 220 clear picture, squeezed to 990 px wide at the most. */
    MetalGl::Picture censoredWord()
    {
        juce::Image image (juce::Image::ARGB, 1024, 220, true);
        {
            juce::Graphics g (image);
            const juce::Font font (juce::FontOptions ("Helvetica Neue", 190.0f, juce::Font::bold).withPointHeight (190.0f));
            juce::GlyphArrangement glyphs;
            glyphs.addLineOfText (font, "CENSORED", 0.0f, 0.0f);
            const auto box = glyphs.getBoundingBox (0, -1, true);
            const float squeeze = std::min (1.0f, 990.0f / std::max (1.0f, box.getWidth()));
            const float baseline = 118.0f + (font.getAscent() - font.getDescent()) * 0.5f;     // the em box's middle on 118
            glyphs.moveRangeOfGlyphs (0, -1, -box.getCentreX(), 0.0f);
            g.setColour (juce::Colours::white);
            glyphs.draw (g, juce::AffineTransform::scale (squeeze, 1.0f).translated (512.0f, baseline));
        }
        MetalGl::Picture p;
        p.width = image.getWidth(); p.height = image.getHeight();
        p.rgba.resize ((size_t) p.width * (size_t) p.height * 4);
        const juce::Image::BitmapData px (image, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < p.height; ++y)
            for (int x = 0; x < p.width; ++x)
            {
                const auto c = px.getPixelColour (x, y);        // straight
                auto* q = &p.rgba[((size_t) y * (size_t) p.width + (size_t) x) * 4];
                q[0] = c.getRed(); q[1] = c.getGreen(); q[2] = c.getBlue(); q[3] = c.getAlpha();
            }
        return p;
    }
}

MetalGl::Art shippedArt()
{
    MetalGl::Art art;
    art.bytes = [] (const std::string& path)
    {
        juce::MemoryBlock block;
        std::vector<std::uint8_t> out;
        if (const auto f = fileAt (path); f.existsAsFile() && f.loadFileAsData (block))
            out.assign (static_cast<const std::uint8_t*> (block.getData()), static_cast<const std::uint8_t*> (block.getData()) + block.getSize());
        return out;
    };
    art.list = [] (const std::string& folder)
    {
        std::vector<std::string> names;
        for (const auto& f : folderAt (folder).findChildFiles (juce::File::findFilesAndDirectories, false))
            names.push_back (f.getFileName().toStdString());
        std::sort (names.begin(), names.end());
        return names;
    };
    art.picture = [] (const std::string& path)
    {
        juce::MemoryBlock block;
        const auto f = fileAt (path);
        return f.existsAsFile() && f.loadFileAsData (block) ? decodeWebp (block.getData(), block.getSize()) : MetalGl::Picture {};
    };
    art.pictureSize = [] (const std::string& path)
    {
        std::array<int, 2> size { 0, 0 };
        juce::FileInputStream in (fileAt (path));
        if (! in.openedOk()) return size;
        juce::MemoryBlock head;
        in.readIntoMemoryBlock (head, 64);
        int w = 0, h = 0;
        if (WebPGetInfo (static_cast<const uint8_t*> (head.getData()), head.getSize(), &w, &h) != 0) size = { w, h };
        return size;
    };
    art.censored = [] { return censoredWord(); };
    return art;
}

} // namespace orbitamp::metal
