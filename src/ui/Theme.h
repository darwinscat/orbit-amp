#pragma once

#include <felitronics/appkit/Brand.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace orbitamp::theme
{

// The brand accents come from felitronics-appkit — violet (DSP) and orange (captured) are the
// family's, not this product's. Everything below is the DEVICE surface: the dark instrument face
// the blocks sit on. Values are the faceplate spec's, transcribed from the design mockup.
using felitronics::appkit::brand::violet;   // DSP blocks, hairlines, primary accent
using felitronics::appkit::brand::lilac;    // DSP label tint
using felitronics::appkit::brand::orange;   // "spark" — the captured neural core

inline const juce::Colour ground  { 0xff08080d };   // behind the device
inline const juce::Colour panel   { 0xff111119 };
inline const juce::Colour panel2  { 0xff15151f };
inline const juce::Colour bezel   { 0xff050509 };   // recessed wells (meters, scope)

// Hairlines are the accent at low alpha — the whole face is tinted violet, never neutral grey.
inline const juce::Colour hair    = violet.withAlpha (0.12f);
inline const juce::Colour hair2   = violet.withAlpha (0.24f);

inline const juce::Colour tx      { 0xffece9f6 };
inline const juce::Colour txDim   { 0xffa6a2bd };
inline const juce::Colour txFaint { 0xff6f6b86 };

// Block fills, per kind. The spec's borders are a mix of the accent over the hairline; JUCE has no
// color-mix, so these are the mixed result at the alpha that reads the same on the device gradient.
inline const juce::Colour capTop  { 0xff1b1620 };
inline const juce::Colour capBot  { 0xff120e17 };
inline const juce::Colour dspTop  { 0xff181420 };
inline const juce::Colour dspBot  { 0xff111019 };

/** The character ramp: green through yellow and orange to red, following gain. The colour IS the
    type, so a voicing list can drop the word and still say what it is at a glance. */
inline juce::Colour characterColour (int typeIndex)
{
    static const juce::Colour ramp[] = {
        juce::Colour (0xff5fc97a),   // clean
        juce::Colour (0xffb8cc5a),   // edge
        juce::Colour (0xffe8b04a),   // crunch
        juce::Colour (0xffe8783c),   // high-gain
        juce::Colour (0xffe0503c),   // modern
    };
    constexpr int n = (int) (sizeof (ramp) / sizeof (ramp[0]));
    return ramp[juce::jlimit (0, n - 1, typeIndex)];
}

constexpr float radiusSm = 5.0f;
constexpr float radiusMd = 9.0f;    // blocks
constexpr float radiusLg = 15.0f;
constexpr float radiusXl = 22.0f;   // the device shell

constexpr float blockBorder = 1.5f;
constexpr float offAlpha    = 0.46f;   // a switched-off block dims but stays interactive

//==============================================================================
// The display face — Michroma (OFL), embedded from felitronics-appkit's assets. Every label on the
// faceplate is uppercase, small and widely tracked; that tracking is what makes it read as an
// instrument rather than a form, so labels go through drawTracked below, not g.drawText.

juce::Typeface::Ptr displayTypeface();

inline juce::Font displayFont (float height)
{
    return felitronics::appkit::brand::wordmarkFont (displayTypeface(), height);
}

/** Draws `text` with per-character tracking (the spec quotes it in em, e.g. .15em). JUCE lays glyphs
    out at their natural advance, so the letters are placed one at a time. */
void drawTracked (juce::Graphics&, const juce::String& text, juce::Rectangle<float> area,
                  const juce::Font&, float trackingEm, juce::Justification);

/** Width `text` would occupy at that tracking — for hand-laying a label row. */
float trackedWidth (const juce::String& text, const juce::Font&, float trackingEm);

} // namespace orbitamp::theme
