#include "PluginEditor.h"

namespace orbitamp
{

AmpEditor::AmpEditor (AmpProcessor& p)
    : juce::AudioProcessorEditor (&p)
{
    addAndMakeVisible (faceplate);
    setSize (FaceplateView::designWidth  + margin * 2,
             FaceplateView::designHeight + margin * 2);
}

void AmpEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::ground);
}

void AmpEditor::resized()
{
    faceplate.setBounds (getLocalBounds().reduced (margin));
}

} // namespace orbitamp
