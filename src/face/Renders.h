// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <juce_graphics/juce_graphics.h>

#include <array>
#include <functional>

/** HOW A FACE'S RENDERS ARE MADE READY: found by their file names, and the lamps a background render
    paints lit taken out of it, to be lit again from the engine. Each face keeps its renders in a
    binary-data namespace of its own; this is what it does with them, said once. */
namespace orbitamp::face::renders
{

/** Where a product's renders are built into its binary — the three things juce_add_binary_data makes. */
struct Binary
{
    const char* const* names;               // namedResourceList
    int count;                              // namedResourceListSize
    const char* (*originalName) (const char*);              // getNamedResourceOriginalFilename
    const char* (*data) (const char*, int& size);           // getNamedResource
};

/** A render by its file name, as it was made — whatever format it decodes to: the copy built into
    the binary — or, when the environment variable `folderVariable` names a folder that has one, the
    file from there, so a new render can be tried by restarting rather than rebuilding. */
juce::Image decode (const char* name, const Binary&, const char* folderVariable);

/** ...and the same, as ARGB, ready to be drawn from. */
juce::Image load (const char* name, const Binary&, const char* folderVariable);

/** THE LAMPS GO OUT. A render paints lit the lamps a face lights itself. Each, in its box (`boxes`, in
    the face's 1672-wide stage), is taken out as it was painted, glow and all, into `lit` — to be laid
    back over at the strength it burns; in the picture it is put out: how much of a pixel is the lamp's
    (`lampness`, 0..1) is how far it is taken down to the dark warm grey of a lamp that is off, so the
    glow on the plate round it goes too. What the lamp did not touch stays as it was, in the picture
    and out of the copy. */
template <size_t N>
juce::Image putOutLamps (juce::Image img, const std::array<juce::Rectangle<float>, N>& boxes, float stageWidth,
                         const std::function<float (juce::Colour)>& lampness, std::array<juce::Image, N>& lit);

juce::Image putOutLamp (juce::Image& img, juce::Rectangle<float> box, float stageWidth,
                        const std::function<float (juce::Colour)>& lampness);

template <size_t N>
juce::Image putOutLamps (juce::Image img, const std::array<juce::Rectangle<float>, N>& boxes, float stageWidth,
                         const std::function<float (juce::Colour)>& lampness, std::array<juce::Image, N>& lit)
{
    if (img.isValid())
        for (size_t i = 0; i < N; ++i)
            lit[i] = putOutLamp (img, boxes[i], stageWidth, lampness);
    return img;
}

} // namespace orbitamp::face::renders
