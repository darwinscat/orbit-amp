// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "IoArt.h"

#include "../../face/Renders.h"
#include "../../underwater/view/UnderwaterArt.h"     // decodeWebp

#include <juce_core/juce_core.h>

namespace orbitamp::io
{

namespace
{
    constexpr const char* product = "IoBass";
    constexpr const char* folderVariable = "IO_ART_DIR";

    juce::File fileOf (const std::string& name) { return face::renders::fileOf (product, name.c_str(), folderVariable); }

    /** A plate, asked for later: read and decoded then; none when the file is not there. */
    std::function<IoGl::Picture()> picture (const char* name)
    {
        if (! fileOf (name).existsAsFile())
            return {};
        return [n = std::string (name)]
        {
            juce::MemoryBlock bytes;
            return fileOf (n).loadFileAsData (bytes) ? underwater::decodeWebp (bytes.getData(), bytes.getSize()) : IoGl::Picture {};
        };
    }

    /** The finds' folder: the one IO_ART_DIR names where it has one, the product's own otherwise. */
    juce::File findsFolder()
    {
        if (const auto dir = juce::SystemStats::getEnvironmentVariable (folderVariable, {}); dir.isNotEmpty())
            if (const auto f = juce::File (dir).getChildFile ("finds"); f.isDirectory())
                return f;
        return face::renders::folder (product).getChildFile ("finds");
    }
}

IoGl::Art shippedArt()
{
    IoGl::Art art;
    art.day  = picture ("dashboard.webp");
    art.dark = picture ("dashboard_dark.webp");
    // the finds as the prototype's view lists its folder: each <variant>3d folder, its first mesh, by their names' order
    art.finds = []
    {
        std::vector<std::string> out;
        auto dirs = findsFolder().findChildFiles (juce::File::findDirectories, false, "*3d");
        dirs.sort();
        for (const auto& d : dirs)
        {
            auto files = d.findChildFiles (juce::File::findFiles, false, "*.mesh");
            files.sort();
            if (! files.isEmpty())
                out.push_back ((d.getFileName() + "/" + files[0].getFileNameWithoutExtension()).toStdString());
        }
        return out;
    };
    art.text = [] (const std::string& path)
    {
        const auto file = findsFolder().getChildFile (path);
        return file.existsAsFile() ? file.loadFileAsString().toStdString() : std::string();
    };
    art.bytes = [] (const std::string& path)
    {
        juce::MemoryBlock bytes;
        if (! findsFolder().getChildFile (path).loadFileAsData (bytes))
            return std::vector<std::uint8_t>();
        const auto* p = static_cast<const std::uint8_t*> (bytes.getData());
        return std::vector<std::uint8_t> (p, p + bytes.getSize());
    };
    return art;
}

} // namespace orbitamp::io
