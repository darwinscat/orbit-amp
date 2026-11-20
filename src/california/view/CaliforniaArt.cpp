// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "CaliforniaArt.h"

#include "../../face/Renders.h"
#include "../../underwater/view/UnderwaterArt.h"     // decodeWebp

#include <juce_core/juce_core.h>

namespace orbitamp::california
{

namespace
{
    constexpr const char* product = "CaliforniaAmp";
    constexpr const char* folderVariable = "CALIFORNIA_ART_DIR";

    juce::File fileOf (const std::string& name) { return face::renders::fileOf (product, name.c_str(), folderVariable); }

    /** A picture's file, read and decoded now; invalid when it is not there. The prototype's .png is our .webp. */
    CaliforniaGl::Picture readPicture (std::string name)
    {
        if (name.size() > 4 && name.compare (name.size() - 4, 4, ".png") == 0)
            name = name.substr (0, name.size() - 4) + ".webp";
        juce::MemoryBlock bytes;
        return fileOf (name).loadFileAsData (bytes) ? underwater::decodeWebp (bytes.getData(), bytes.getSize()) : CaliforniaGl::Picture {};
    }

    /** The same, asked for later: none when the file is not there. */
    std::function<CaliforniaGl::Picture()> picture (const char* name)
    {
        if (! fileOf (name).existsAsFile())
            return {};
        return [n = std::string (name)] { return readPicture (n); };
    }
}

CaliforniaGl::Art shippedArt (bool wink)
{
    CaliforniaGl::Art art;
    art.dashboard  = picture ("dashboard.webp");
    art.pendant    = picture ("dashboard_pendant.webp");
    art.pendantBox = face::renders::text (product, "dashboard_pendant.txt", folderVariable).toStdString();
    art.label      = picture ("label.webp");
    art.picture    = [] (const std::string& path) { return readPicture ("finds/" + path); };
    art.text       = [] (const std::string& path)
    {
        const auto file = fileOf ("finds/" + path);
        return file.existsAsFile() ? file.loadFileAsString().toStdString() : std::string();
    };
    art.bytes      = [] (const std::string& path)
    {
        juce::MemoryBlock bytes;
        if (! fileOf ("finds/" + path).loadFileAsData (bytes))
            return std::vector<std::uint8_t>();
        const auto* p = static_cast<const std::uint8_t*> (bytes.getData());
        return std::vector<std::uint8_t> (p, p + bytes.getSize());
    };
    art.wink = wink;
    return art;
}

} // namespace orbitamp::california
