// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Gate for THE PICTURES OF A FACE'S KIT FOR THE SITE (src/face/WebImages.h), the one writer every face's
// export writes its pictures through:
//
//   - a picture is written as WebP, whatever JUCE format it is held in;
//   - one with a hole keeps its alpha, and the alpha comes back exactly as it went in (lossless); one with
//     none — even held as ARGB, as every poster is — is written with no alpha plane at all;
//   - the colour comes back near what went in, straight, not premultiplied;
//   - the three sizes are 3344 x 1882, 1672 x 941 and 836 x 470 for a poster drawn at twice the stage, under
//     the names and the keys places.json carries them by; the downscale averages what it shrinks — a
//     one-pixel checkerboard comes out grey at a quarter, where a renderer that picks pixels gives black or
//     white;
//   - a sprite lies in sprites/, a WebP named as its key, the folder made for it; a render a skin has not is
//     no file and null where places.json would name it (the site's reader fails on a named file not there).
//
// It writes into a folder of its own in the temporary folder and leaves nothing behind. main() returns 0
// when every check held.

#include "face/WebImages.h"

#include <webp/decode.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace orbitamp::face;

namespace
{
    int failed = 0;

    void check (bool ok, const juce::String& what)
    {
        std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
        if (! ok)
            ++failed;
    }

    struct Decoded
    {
        bool webp = false, alpha = false;
        int width = 0, height = 0;
        std::vector<uint8_t> rgba;      // straight
        int at (int x, int y, int channel) const { return rgba[(size_t) ((y * width + x) * 4 + channel)]; }
    };

    Decoded decode (const juce::File& f)
    {
        Decoded d;
        juce::MemoryBlock bytes;
        if (! f.loadFileAsData (bytes) || bytes.getSize() < 12)
            return d;
        const auto* b = static_cast<const uint8_t*> (bytes.getData());
        d.webp = std::memcmp (b, "RIFF", 4) == 0 && std::memcmp (b + 8, "WEBP", 4) == 0;
        WebPBitstreamFeatures features;
        if (! d.webp || WebPGetFeatures (b, bytes.getSize(), &features) != VP8_STATUS_OK)
            return d;
        d.alpha = features.has_alpha != 0;
        d.width = features.width;
        d.height = features.height;
        d.rgba.resize ((size_t) (d.width * d.height * 4));
        if (WebPDecodeRGBAInto (b, bytes.getSize(), d.rgba.data(), d.rgba.size(), d.width * 4) == nullptr)
            d.webp = false;
        return d;
    }

    juce::Image filled (int w, int h, juce::Colour c)
    {
        juce::Image img (juce::Image::ARGB, w, h, true);
        juce::Graphics (img).fillAll (c);
        return img;
    }
}

int main()
{
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("orbitamp-web-images-" + juce::String::toHexString (juce::Random().nextInt64()));
    dir.createDirectory();

    // AN OPAQUE PICTURE, HELD AS ARGB — a poster is: no alpha plane, the colour near what went in.
    {
        const auto img = filled (64, 48, juce::Colour (200, 120, 40));
        check (! web::hasHoles (img), "an opaque ARGB picture has no hole");
        const auto f = dir.getChildFile ("opaque.webp");
        check (web::write (f, img), "an opaque picture is written");
        const auto d = decode (f);
        check (d.webp, "...as WebP");
        check (d.webp && ! d.alpha, "...with no alpha plane (cwebp -noalpha)");
        check (d.width == 64 && d.height == 48, "...at its own size");
        check (d.webp && std::abs (d.at (32, 24, 0) - 200) <= 6 && std::abs (d.at (32, 24, 1) - 120) <= 6
                   && std::abs (d.at (32, 24, 2) - 40) <= 6, "...its colour near what went in");
    }

    // A PICTURE WITH A HOLE — a sprite, the cabin with its window: the alpha kept, and kept exactly, and the
    // colour under a half-covered pixel straight, not darkened by the premultiplied form JUCE holds it in.
    {
        auto img = filled (64, 48, juce::Colour (40, 200, 90));
        {
            const juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 64; ++x)
                {
                    juce::PixelARGB px;
                    px.setARGB ((juce::uint8) (x < 32 ? 0 : 128), 40, 200, 90);
                    px.premultiply();
                    *reinterpret_cast<juce::PixelARGB*> (bd.getPixelPointer (x, y)) = px;
                }
        }
        check (web::hasHoles (img), "a picture with transparent pixels has a hole");
        const auto f = dir.getChildFile ("hole.webp");
        check (web::write (f, img), "a picture with a hole is written");
        const auto d = decode (f);
        check (d.webp && d.alpha, "...as WebP with its alpha");
        bool exact = d.webp;
        for (int y = 0; exact && y < 48; ++y)
            for (int x = 0; exact && x < 64; ++x)
                exact = d.at (x, y, 3) == (y >= 16 ? 255 : (x < 32 ? 0 : 128));
        check (exact, "...the alpha exactly as it went in (lossless, -alpha_q 100)");
        check (d.webp && std::abs (d.at (48, 8, 1) - 200) <= 8, "...a half-covered pixel's colour straight (unpremultiplied)");
    }

    // THE THREE SIZES, under the names places.json carries them by.
    {
        const auto full = filled (3344, 1882, juce::Colours::black);
        {
            const juce::Image::BitmapData bd (full, juce::Image::BitmapData::readWrite);
            for (int y = 0; y < 1882; ++y)
                for (int x = 0; x < 3344; ++x)
                    if (((x + y) & 1) != 0)
                        reinterpret_cast<juce::PixelARGB*> (bd.getPixelPointer (x, y))->setARGB (255, 255, 255, 255);
        }
        const auto names = web::writeThreeSizes (dir, "poster", full, "poster");
        check (names["poster"] == juce::var ("poster.webp") && names["poster1x"] == juce::var ("poster@1x.webp")
                   && names["posterHalf"] == juce::var ("poster@half.webp"), "a poster's three names, under poster / poster1x / posterHalf");
        const auto cabin = web::threeNames ("cabin", "cabin");
        check (cabin["cabin"] == juce::var ("cabin.webp") && cabin["cabin1x"] == juce::var ("cabin@1x.webp")
                   && cabin["cabinHalf"] == juce::var ("cabin@half.webp"), "a cabin's, under cabin / cabin1x / cabinHalf");

        const auto big = decode (dir.getChildFile ("poster.webp"));
        const auto one = decode (dir.getChildFile ("poster@1x.webp"));
        const auto half = decode (dir.getChildFile ("poster@half.webp"));
        check (big.webp && big.width == 3344 && big.height == 1882, "poster.webp is 3344 x 1882");
        check (one.webp && one.width == 1672 && one.height == 941, "poster@1x.webp is 1672 x 941");
        check (half.webp && half.width == 836 && half.height == 470, "poster@half.webp is 836 x 470");

        // A one-pixel checkerboard shrunk well is grey; shrunk by picking pixels, black or white.
        const auto grey = [] (const Decoded& d)
        {
            if (! d.webp)
                return false;
            double sum = 0.0, worst = 0.0;
            int n = 0;
            for (int y = d.height / 4; y < d.height * 3 / 4; y += 7)
                for (int x = d.width / 4; x < d.width * 3 / 4; x += 7)
                {
                    const double v = d.at (x, y, 1);
                    sum += v;
                    worst = std::max (worst, std::abs (v - 127.5));
                    ++n;
                }
            std::printf ("      mean %.1f, farthest from grey %.1f\n", sum / n, worst);
            return std::abs (sum / n - 127.5) < 16.0 && worst < 48.0;
        };
        check (grey (one), "@1x averages what it shrinks: a one-pixel checkerboard comes out grey");
        check (grey (half), "@half too, shrunk by four");
    }

    // A SPRITE: sprites/<its key>.webp, the folder made for it.
    {
        check (web::spriteFile ("knob-drive") == "sprites/knob-drive.webp", "a sprite lies in sprites/, named as its key");
        const auto f = dir.getChildFile (web::spriteFile ("knob-drive"));
        check (web::writeSprite (dir, "knob-drive", filled (32, 32, juce::Colours::red.withAlpha (0.5f))) == juce::var ("sprites/knob-drive.webp")
                   && decode (f).webp, "...written there, its folder made for it, its name given back for places.json");
        check (web::writeSprite (dir, "socket", juce::Image()).isVoid() && ! dir.getChildFile (web::spriteFile ("socket")).exists(),
               "a render the skin has not: no file, and null for places.json instead of a name");
    }

    check (! web::write (dir.getChildFile ("nothing.webp"), juce::Image()) && ! dir.getChildFile ("nothing.webp").exists(),
           "an invalid picture writes nothing");

    dir.deleteRecursively();
    std::printf ("\n%s\n", failed == 0 ? "ALL OK" : (juce::String (failed) + " FAILED").toRawUTF8());
    return failed == 0 ? 0 : 1;
}
