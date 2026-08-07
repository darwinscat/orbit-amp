#include "DemoStrip.h"

#include "../Parameters.h"
#include "../PluginProcessor.h"

namespace orbitamp
{

DemoStrip::DemoStrip (AmpProcessor& processor)
    : amp (processor)
{
    addAndMakeVisible (loop);
    loop.setItems (params::demoLoops, 0);

    loop.onChange = [this] (int i)
    {
        amp.selectDemoLoop (i);   // switching loops while playing keeps playing, from the new top
    };

    amp.selectDemoLoop (0);
}

DemoStrip::~DemoStrip() = default;

juce::Rectangle<int> DemoStrip::buttonArea() const
{
    return getLocalBounds().removeFromLeft (buttonWidth).reduced (0, 2);
}

void DemoStrip::paint (juce::Graphics& g)
{
    const auto r = buttonArea().toFloat();
    const bool playing = amp.demo.isPlaying();

    g.setColour (juce::Colour (0xff0d0d14));
    g.fillRoundedRectangle (r, theme::radiusSm);
    g.setColour (playing ? theme::lilac : theme::hair2);
    g.drawRoundedRectangle (r.reduced (0.5f), theme::radiusSm, 1.0f);

    const auto c = r.getCentre();
    const float s = r.getHeight() * 0.34f;

    g.setColour (playing ? theme::lilac : theme::txDim);

    if (playing)
    {
        // Stop: a square, because the button says what pressing it does.
        g.fillRect (juce::Rectangle<float> (s * 1.6f, s * 1.6f).withCentre (c));
    }
    else
    {
        juce::Path tri;
        tri.startNewSubPath (c.x - s * 0.7f, c.y - s);
        tri.lineTo          (c.x + s,        c.y);
        tri.lineTo          (c.x - s * 0.7f, c.y + s);
        tri.closeSubPath();
        g.fillPath (tri);
    }

    g.setColour (theme::txFaint);
    theme::drawTracked (g, "Demo", getLocalBounds().toFloat()
                                       .withTrimmedLeft ((float) (buttonWidth + gap + loopWidth + gap))
                                       .withTrimmedRight (4.0f),
                        theme::displayFont (7.5f), 0.12f, juce::Justification::centredLeft);
}

void DemoStrip::resized()
{
    loop.setBounds (getLocalBounds().withTrimmedLeft (buttonWidth + gap).removeFromLeft (loopWidth));
}

void DemoStrip::mouseDown (const juce::MouseEvent& e)
{
    if (buttonArea().contains (e.getPosition()))
    {
        amp.demo.setPlaying (! amp.demo.isPlaying());
        repaint();
    }
}

} // namespace orbitamp
