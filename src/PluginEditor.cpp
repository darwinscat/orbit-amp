#include "PluginEditor.h"

namespace orbitamp
{

AmpEditor::AmpEditor (AmpProcessor& p)
    : juce::AudioProcessorEditor (&p), amp (p), chrome (p), faceplate (p.apvts)
{
    addAndMakeVisible (chrome);
    addAndMakeVisible (faceplate);
    addAndMakeVisible (glyphs);   // TEMPORARY

    // Dragging the corner IS the zoom: the aspect is locked, so width alone determines the factor.
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) baseWidth / (double) baseHeight);
    setResizeLimits (juce::roundToInt (baseWidth  * AmpProcessor::minScale),
                     juce::roundToInt (baseHeight * AmpProcessor::minScale),
                     juce::roundToInt (baseWidth  * AmpProcessor::maxScale),
                     juce::roundToInt (baseHeight * AmpProcessor::maxScale));

    const float s = amp.getEditorScale();
    setSize (juce::roundToInt (baseWidth * s), juce::roundToInt (baseHeight * s));
}

void AmpEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::ground);
}

void AmpEditor::resized()
{
    const float s = (float) getWidth() / (float) baseWidth;
    amp.setEditorScale (s);

    // Bounds stay in design units; the transform does all the scaling, margins included.
    const auto zoom = juce::AffineTransform::scale (s);

    chrome.setBounds (margin, margin, FaceplateView::designWidth, Chrome::designHeight);
    chrome.setTransform (zoom);

    const int faceplateY = margin + Chrome::designHeight + chromeGap;
    faceplate.setBounds (margin, faceplateY, FaceplateView::designWidth, FaceplateView::designHeight);
    faceplate.setTransform (zoom);

    // TEMPORARY — the glyph review strip, directly under the faceplate.
    glyphs.setBounds (margin, faceplateY + FaceplateView::designHeight,
                      FaceplateView::designWidth, GlyphPreview::designHeight);
    glyphs.setTransform (zoom);
}

} // namespace orbitamp
