// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "Shaders.h"

#include <ShaderData.h>     // the files, as binary data — which knows a file by its name alone
#include <ShaderPaths.h>    // ...so the build writes their paths too, in the same order

#include <algorithm>
#include <iterator>

namespace orbitamp::shaders
{

namespace
{
    // An included file includes in its turn; deeper than this is a file that includes itself.
    constexpr int maxDepth = 8;

    bool put (std::string& out, std::string_view text, Find find, const std::vector<Written>& written, int depth)
    {
        constexpr std::string_view include = "#include ";
        while (! text.empty())
        {
            const auto end = text.find ('\n');
            const auto line = text.substr (0, end == std::string_view::npos ? text.size() : end + 1);
            text.remove_prefix (line.size());

            auto what = line;
            if (what.ends_with ('\n'))
                what.remove_suffix (1);
            if (! what.starts_with (include))
            {
                out += line;
                continue;
            }
            what.remove_prefix (include.size());
            if (what.size() < 3)
                return false;
            const auto name = what.substr (1, what.size() - 2);

            if (what.front() == '"' && what.back() == '"')
            {
                const auto included = find == nullptr ? std::string_view() : find (name);
                if (included.empty() || depth >= maxDepth || ! put (out, included, find, written, depth + 1))
                    return false;
            }
            else if (what.front() == '<' && what.back() == '>')
            {
                const auto w = std::find_if (written.begin(), written.end(), [name] (const Written& x) { return x.name == name; });
                if (w == written.end())
                    return false;
                out += w->text;
            }
            else
            {
                return false;
            }
        }
        return true;
    }

    /** Every `from` in `text` made `to`, left to right, a replaced word never read again: juce::String::replace. */
    std::string replaced (std::string_view text, std::string_view from, std::string_view to)
    {
        std::string out;
        out.reserve (text.size());
        for (size_t at = 0;;)
        {
            const auto next = text.find (from, at);
            if (next == std::string_view::npos)
            {
                out += text.substr (at);
                return out;
            }
            out += text.substr (at, next - at);
            out += to;
            at = next + from.size();
        }
    }
}

std::string assemble (std::string_view text, Find find, const std::vector<Written>& written)
{
    std::string out;
    out.reserve (text.size() * 2);
    if (text.empty() || ! put (out, text, find, written, 0))
        return {};
    return out;
}

std::string_view file (std::string_view path) noexcept
{
    static_assert (std::size (embeddedPaths) == ShaderData::namedResourceListSize);

    for (int i = 0; i < ShaderData::namedResourceListSize; ++i)
    {
        if (path != embeddedPaths[i])
            continue;

        // The two lists are one order; the file's own name, the last part of its path, says they still are.
        const auto* resource = ShaderData::namedResourceList[i];
        const auto* original = ShaderData::getNamedResourceOriginalFilename (resource);
        if (original == nullptr || path.substr (path.rfind ('/') + 1) != original)
            return {};

        int size = 0;
        const char* data = ShaderData::getNamedResource (resource, size);
        return data == nullptr ? std::string_view() : std::string_view (data, (size_t) size);
    }
    return {};
}

std::string source (std::string_view path, const std::vector<Written>& written)
{
    return assemble (file (path), file, written);
}

std::string vertexToV3 (std::string_view code, std::string_view head)
{
    if (head.empty())
        return std::string (code);
    return std::string (head) + replaced (replaced (code, "attribute", "in"), "varying", "out");
}

std::string fragmentToV3 (std::string_view code, std::string_view head)
{
    if (head.empty())
        return std::string (code);
    // JUCE writes "out " JUCE_MEDIUMP " vec4 fragColor;", JUCE_MEDIUMP being nothing on the desktop: two spaces.
    return std::string (head) + "out  vec4 fragColor;\n"
         + replaced (replaced (replaced (code, "varying", "in"), "texture2D", "texture"), "gl_FragColor", "fragColor");
}

} // namespace orbitamp::shaders
