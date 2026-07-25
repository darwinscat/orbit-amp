#include "Theme.h"

#include <BinaryData.h>

namespace orbitamp::theme
{

juce::Typeface::Ptr displayTypeface()
{
    // Parsed once for the process — every label on every open editor shares it.
    static juce::Typeface::Ptr tf = juce::Typeface::createSystemTypefaceFor (
        BinaryData::MichromaRegular_ttf, (size_t) BinaryData::MichromaRegular_ttfSize);
    return tf;
}

namespace
{
    // Lays `text` out on a baseline at y = 0 and reports the advance width, so both the measure and
    // the draw below agree on where each glyph lands.
    float layOut (juce::GlyphArrangement& ga, const juce::String& text, const juce::Font& font, float trackingEm)
    {
        ga.addLineOfText (font, text, 0.0f, 0.0f);

        const int n = ga.getNumGlyphs();
        if (n == 0)
            return 0.0f;

        const float advance = ga.getGlyph (n - 1).getRight() - ga.getGlyph (0).getLeft();
        return advance + trackingEm * font.getHeight() * (float) (n - 1);
    }
}

float trackedWidth (const juce::String& text, const juce::Font& font, float trackingEm)
{
    juce::GlyphArrangement ga;
    return layOut (ga, text, font, trackingEm);
}

void drawTracked (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                  const juce::Font& font, float trackingEm, juce::Justification just)
{
    juce::GlyphArrangement ga;
    const float total = layOut (ga, text, font, trackingEm);

    const int n = ga.getNumGlyphs();
    if (n == 0)
        return;

    float x = area.getX();
    if (just.testFlags (juce::Justification::horizontallyCentred))  x += (area.getWidth() - total) * 0.5f;
    else if (just.testFlags (juce::Justification::right))           x += area.getWidth() - total;

    // Centre the cap height in the box: JUCE's ascent includes room for accents this face never uses.
    float y = area.getCentreY() + (font.getAscent() - font.getDescent()) * 0.5f;
    if (just.testFlags (juce::Justification::top))         y = area.getY() + font.getAscent();
    else if (just.testFlags (juce::Justification::bottom)) y = area.getBottom() - font.getDescent();

    const float step = trackingEm * font.getHeight();
    const float x0   = ga.getGlyph (0).getLeft();

    for (int i = 0; i < n; ++i)
        ga.getGlyph (i).draw (g, juce::AffineTransform::translation (x - x0 + step * (float) i, y));
}

} // namespace orbitamp::theme
