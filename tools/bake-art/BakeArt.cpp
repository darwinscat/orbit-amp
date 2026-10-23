// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// BAKE-ART [--check] [--out <dir>] [--resources <dir>] [face ...] — makes the faces' pictures as they ship.
//
// Each face's renders are painted once and kept as they were painted, in resources/<face>/source. What a
// face draws is not quite that: a rover's dashboard has its rims made taller for the words, its notches
// fitted to the product's gears, its corner plates grown for a fader, the lamps it paints lit cut out
// of it, to be lit again from the engine, and the rings it and the sockets paint round the knobs taken
// out, to be drawn whole by the face; the cockpit's render has its windshield cut out and its TURBO
// frame raised; a lever has a glow the shape of it, a throttle grip is rebuilt. Each
// face used to do that itself on every start. This does it once, and writes what comes out into
// resources/<face> as lossless WebP — the panel, each render, each sprite made from one — and that is
// what a product carries and draws as it is (src/face/Renders.h). The numbers each correction was
// measured to are here, in Rover.cpp and Cockpit.cpp; a face's skin keeps only where things are.
//
// A picture is written so that the faces' decoding gives back every byte it had here: JUCE keeps a
// picture premultiplied and WebP straight, and for each premultiplied value the straight one is the
// nearest to its true colour that face::renders::premultiplied takes back to it.
//
// BAKE ON A MAC. The faces made these pictures with what JUCE decodes and draws with there — CoreImage
// and CoreGraphics — and so does this; elsewhere they come out a shade different, which --check says.
//
//   (no face)        every face: redpaw, frostpaw, amberpaw, catpit
//   --check          writes nothing: fails when a picture a face ships is not what its sources make
//   --out <dir>      writes into <dir>/<face> instead — a folder for the face's *_ART_DIR to try
//   --resources <d>  the resources folder to read and write (the tree's, by default)
//
// A file whose pixels are already what the bake makes is left as it is.

#include "Bake.h"

#include "catpit/CatpitLayout.h"
#include "face/Renders.h"

#include <webp/encode.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <optional>

namespace orbitamp::bake
{

bool Sources::has (const juce::String& name) const
{
    return own.getChildFile (name).existsAsFile();
}

juce::Image Sources::load (const juce::String& name) const
{
    // As the faces loaded their renders: decoded from the bytes, then as ARGB.
    for (const auto& dir : { own, fallback })
        if (dir != juce::File() && dir.getChildFile (name).existsAsFile())
        {
            juce::MemoryBlock bytes;
            if (! dir.getChildFile (name).loadFileAsData (bytes))
                return {};
            const auto img = juce::ImageFileFormat::loadFrom (bytes.getData(), bytes.getSize());
            return img.isValid() ? img.convertedToFormat (juce::Image::ARGB) : img;
        }
    return {};
}

juce::Image putOutLamp (juce::Image& img, juce::Rectangle<float> stageBox, float stageWidth,
                        const std::function<float (juce::Colour)>& lampness)
{
    const float k = (float) img.getWidth() / stageWidth;
    const auto box = (stageBox * k).getSmallestIntegerContainer().getIntersection (img.getBounds());
    juce::Image lit (juce::Image::ARGB, juce::jmax (1, box.getWidth()), juce::jmax (1, box.getHeight()), true);
    if (box.isEmpty())
        return lit;

    juce::Image::BitmapData bd (img, box.getX(), box.getY(), box.getWidth(), box.getHeight(), juce::Image::BitmapData::readWrite);
    juce::Image::BitmapData out (lit, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < box.getHeight(); ++y)
        for (int x = 0; x < box.getWidth(); ++x)
        {
            const auto c = bd.getPixelColour (x, y);
            const float s = juce::jlimit (0.0f, 1.0f, lampness (c));
            if (s <= 0.0f)
                continue;

            const float r = c.getFloatRed(), g = c.getFloatGreen(), b = c.getFloatBlue();
            const float lum = 0.3f * r + 0.59f * g + 0.11f * b;
            const auto off = juce::Colour::fromFloatRGBA (lum * 0.30f + 0.03f, lum * 0.25f + 0.028f, lum * 0.24f + 0.03f, c.getFloatAlpha());
            out.setPixelColour (x, y, c);
            bd.setPixelColour (x, y, c.interpolatedWith (off, s));
        }
    return lit;
}

} // namespace orbitamp::bake

namespace
{
    using namespace orbitamp;

    /** For each alpha, each premultiplied value's straight one: of those face::renders::premultiplied
        takes back to it, the nearest to value * 255 / alpha. -1 where none does — a value over its alpha,
        which no premultiplied picture holds. */
    struct Straight
    {
        std::array<std::array<int, 256>, 256> of;

        Straight()
        {
            for (int a = 0; a < 256; ++a)
                for (int c = 0; c < 256; ++c)
                {
                    int best = -1;
                    float err = 0.0f;
                    for (int u = 0; u < 256; ++u)
                        if (face::renders::premultiplied ((juce::uint8) u, (juce::uint8) a) == c)
                        {
                            const float e = std::abs ((float) u - (a == 0 ? 0.0f : (float) c * 255.0f / (float) a));
                            if (best < 0 || e < err)
                                best = u, err = e;
                        }
                    of[(size_t) a][(size_t) c] = best;
                }
        }
    };

    /** A picture as lossless WebP, in straight colour the faces' decoding takes back to every byte of it;
        none when a pixel cannot be, or the encoder refuses. */
    std::optional<juce::MemoryBlock> encode (const juce::Image& image)
    {
        const auto img = image.convertedToFormat (juce::Image::ARGB);
        static const Straight straight;
        const int w = img.getWidth(), h = img.getHeight();

        WebPConfig config;
        WebPPicture picture;
        if (! WebPConfigInit (&config) || ! WebPConfigLosslessPreset (&config, 9) || ! WebPPictureInit (&picture))
            return std::nullopt;
        picture.use_argb = 1;
        picture.width = w;
        picture.height = h;
        if (! WebPPictureAlloc (&picture))
            return std::nullopt;

        bool exact = true;
        {
            const juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                {
                    const auto px = *reinterpret_cast<const juce::PixelARGB*> (bd.getPixelPointer (x, y));
                    const auto a = (size_t) px.getAlpha();
                    const int r = straight.of[a][px.getRed()], g = straight.of[a][px.getGreen()], b = straight.of[a][px.getBlue()];
                    exact = exact && r >= 0 && g >= 0 && b >= 0;
                    picture.argb[(size_t) y * (size_t) picture.argb_stride + (size_t) x]
                        = ((uint32_t) a << 24) | ((uint32_t) juce::jmax (0, r) << 16) | ((uint32_t) juce::jmax (0, g) << 8) | (uint32_t) juce::jmax (0, b);
                }
        }

        WebPMemoryWriter writer;
        WebPMemoryWriterInit (&writer);
        picture.writer = WebPMemoryWrite;
        picture.custom_ptr = &writer;
        const bool ok = exact && WebPEncode (&config, &picture) != 0;
        WebPPictureFree (&picture);

        std::optional<juce::MemoryBlock> bytes;
        if (ok)
            bytes = juce::MemoryBlock (writer.mem, writer.size);
        WebPMemoryWriterClear (&writer);
        return bytes;
    }

    /** Whether two pictures are the same, every byte of every pixel, premultiplied as the faces hold them. */
    bool samePixels (const juce::Image& first, const juce::Image& second)
    {
        if (! first.isValid() || ! second.isValid() || first.getWidth() != second.getWidth() || first.getHeight() != second.getHeight())
            return false;
        const auto a = first.convertedToFormat (juce::Image::ARGB), b = second.convertedToFormat (juce::Image::ARGB);
        const juce::Image::BitmapData da (a, juce::Image::BitmapData::readOnly), db (b, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
                if (reinterpret_cast<const juce::PixelARGB*> (da.getPixelPointer (x, y))->getNativeARGB()
                    != reinterpret_cast<const juce::PixelARGB*> (db.getPixelPointer (x, y))->getNativeARGB())
                    return false;
        return true;
    }

    juce::Image decodeFile (const juce::File& file)
    {
        juce::MemoryBlock bytes;
        return file.existsAsFile() && file.loadFileAsData (bytes) ? face::renders::decodeWebp (bytes.getData(), bytes.getSize())
                                                                   : juce::Image();
    }
}

int main (int argc, char* argv[])
{
    const juce::ScopedJuceInitialiser_GUI gui;

    const juce::StringArray everyFace { "redpaw", "frostpaw", "amberpaw", "catpit" };
    auto resources = juce::File (ORBITAMP_RESOURCES_DIR);
    juce::File outDir;
    bool check = false;
    juce::StringArray faces;

    for (int i = 1; i < argc; ++i)
    {
        const auto arg = juce::String::fromUTF8 (argv[i]);
        if (arg == "--check")
            check = true;
        else if ((arg == "--out" || arg == "--resources") && i + 1 < argc)
            (arg == "--out" ? outDir : resources) = juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (argv[++i]));
        else if (everyFace.contains (arg))
            faces.addIfNotAlreadyThere (arg);
        else
        {
            std::fprintf (stderr, "usage: bake-art [--check] [--out <dir>] [--resources <dir>] [%s ...]\n",
                          everyFace.joinIntoString (" | ").toRawUTF8());
            return 2;
        }
    }
    if (faces.isEmpty())
        faces = everyFace;

    int failed = 0;
    for (const auto& face : faces)
    {
        const bake::Sources sources { resources.getChildFile (face).getChildFile ("source"),
                                      bake::isRover (face) && face != "redpaw" ? resources.getChildFile ("redpaw").getChildFile ("source")
                                                                               : juce::File() };
        const auto shipped = outDir == juce::File() ? resources.getChildFile (face) : outDir.getChildFile (face);

        float gripAxis = 0.0f;
        const auto pictures = bake::isRover (face) ? bake::bakeRover (face, sources) : bake::bakeCockpit (sources, gripAxis);
        for (const auto& [name, img] : pictures)
        {
            const auto file = shipped.getChildFile (name);
            const auto line = [&] (const char* what)
            {
                std::printf ("%-9s %-22s %-10s %dx%d\n", face.toRawUTF8(), name.toRawUTF8(), what, img.getWidth(), img.getHeight());
            };
            if (! img.isValid())
            {
                line ("NO SOURCE");
                ++failed;
                continue;
            }
            if (samePixels (decodeFile (file), img))
            {
                line ("as baked");
                continue;
            }
            if (check)
            {
                line (file.existsAsFile() ? "STALE" : "MISSING");
                ++failed;
                continue;
            }

            const auto bytes = encode (img);
            if (! bytes.has_value() || ! shipped.createDirectory() || ! file.replaceWithData (bytes->getData(), bytes->getSize())
                || ! samePixels (decodeFile (file), img))
            {
                line ("FAILED");
                ++failed;
                continue;
            }
            line ("written");
        }
        // ...and where the throttle grip's stem stands across the grip, which the cockpit's layout carries.
        if (face == "catpit" && ! juce::exactlyEqual (gripAxis, catpit::layout::gripAxis))
        {
            std::printf ("%-9s the throttle grip's stem stands %.9g across it; catpit::layout::gripAxis says %.9g\n",
                         face.toRawUTF8(), (double) gripAxis, (double) catpit::layout::gripAxis);
            ++failed;
        }
    }

    if (failed > 0)
        std::fprintf (stderr, "bake-art: %d %s\n", failed, check ? "not as baked — run bake-art" : "not made");
    return failed > 0 ? 1 : 0;
}
