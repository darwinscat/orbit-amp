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

// The EQ node palette, tabby's band slot colours: cool/non-orange so nodes never clash with the
// orange composite line. LO, LO MID, HI MID, HI take the first four; the surgical B3 the fifth.
inline const juce::Colour eqNode[5] = {
    juce::Colour (0xff5ec8ff),   // blue
    juce::Colour (0xff62d2a2),   // green
    juce::Colour (0xffb388ff),   // light violet
    juce::Colour (0xfff06292),   // pink
    juce::Colour (0xff4dd0e1),   // cyan — the scalpel
};

// The analyzer's smoke — tabby's spectrum token, a cool grey-blue deliberately clear of the
// violet band fills, so the signal reads as weather behind the response, not as another band.
inline const juce::Colour spectrum { 0xff7f93b5 };

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

/** The thermometer's fixed colours — the heat scale's own stops, shared by every column and arc
    that tells heat, so the whole face warms up through the same weather. */
inline const juce::Colour heatFloor  { 0xff443a7d };   // the dark corporate ground
inline const juce::Colour heatGreen  { 0xff5fc97a };   // the zone worth living in
inline const juce::Colour heatYellow { 0xffe9c94c };
inline const juce::Colour heatRed    { 0xffff4646 };   // past orange — the truly hot

/** HEAT, 0..1: the one scale for "how hot" wherever the face asks it — cold blue, through the
    family's violet, to orange and red. The gain dial's arc runs along it, and the character ramp
    below samples it, so a device's place in the list and the dial that drives it speak one colour.
    The METER columns tell a different half of the story and wear the thermometer stops above —
    the two scales share their green, yellow and red, not their floor: a dial's bottom is a
    character (cold), a column's bottom is silence. */
inline juce::Colour heatColour (float t)
{
    static const juce::ColourGradient scale = []
    {
        juce::ColourGradient g (juce::Colour (0xff4d7cf0), 0.0f, 0.0f, juce::Colour (0xffff3b30), 1.0f, 0.0f, false);
        g.addColour (0.45, violet);
        g.addColour (0.80, orange);
        return g;
    }();
    return scale.getColourAtPosition ((double) juce::jlimit (0.0f, 1.0f, t));
}

/** The character ramp: five stops on the heat scale, following gain. The colour IS the type, so a
    voicing list can drop the word and still say what it is at a glance. */
inline juce::Colour characterColour (int typeIndex)
{
    static const juce::Colour ramp[] = {
        heatColour (0.00f),   // clean
        heatColour (0.25f),   // edge
        heatColour (0.50f),   // crunch
        heatColour (0.75f),   // high-gain
        heatColour (1.00f),   // modern
    };
    constexpr int n = (int) (sizeof (ramp) / sizeof (ramp[0]));
    return ramp[juce::jlimit (0, n - 1, typeIndex)];
}

constexpr float radiusSm = 5.0f;
constexpr float radiusMd = 9.0f;    // blocks
constexpr float radiusLg = 15.0f;
constexpr float radiusXl = 22.0f;   // the device shell

constexpr float blockBorder = 1.5f;

/** A switched-off block goes properly DARK and read-only. 0.46 looked merely moody — dark enough
    that nobody adjusted it, bright enough that a whole session was spent fighting a gate nobody
    noticed was off. Off now reads as off across the face and the strip alike. */
constexpr float offAlpha    = 0.33f;

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
