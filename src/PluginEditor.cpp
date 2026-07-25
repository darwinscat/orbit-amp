#include "PluginEditor.h"

namespace orbitamp
{

AmpEditor::AmpEditor (AmpProcessor& p)
    : juce::AudioProcessorEditor (&p), amp (p)
{
    addAndMakeVisible (faceplate);

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

    // Bounds stay in design units; the transform does all the scaling, including the margin offset.
    faceplate.setBounds (margin, margin, FaceplateView::designWidth, FaceplateView::designHeight);
    faceplate.setTransform (juce::AffineTransform::scale (s));
}

} // namespace orbitamp
