#include "PluginEditor.h"

namespace orbitamp
{

AmpEditor::AmpEditor (AmpProcessor& p)
    : juce::AudioProcessorEditor (&p)
{
    // Free resizing within the design's 50-200% range, aspect locked — the faceplate is one
    // scaled drawing, not a reflowing layout.
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) baseWidth / (double) baseHeight);
    setResizeLimits (baseWidth / 2, baseHeight / 2, baseWidth * 2, baseHeight * 2);
    setSize (baseWidth, baseHeight);
}

void AmpEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff141317));   // placeholder: the dark instrument face
}

void AmpEditor::resized()
{
}

} // namespace orbitamp
