#include "Footer.h"

#include "../Parameters.h"
#include "../PluginProcessor.h"

namespace orbitamp
{

namespace chrome = felitronics::appkit::chrome;

Footer::Footer (AmpProcessor& processor)
    : amp (processor)
{
    oversample.theme = chrome::ChromeTheme { .fill       = theme::panel,
                                             .underline  = theme::hair,
                                             .accent     = theme::violet,
                                             .attention  = theme::orange,
                                             .text       = theme::tx,
                                             .textDim    = theme::txDim,
                                             .activeText = juce::Colours::white };
    oversample.onClick = [this] { showOversampleMenu(); };
    addAndMakeVisible (oversample);

    oversampleAttachment = std::make_unique<juce::ParameterAttachment> (
        *amp.apvts.getParameter (params::oversample),
        [this] (float v)
        {
            const int i = juce::jlimit (0, params::oversampleFactors.size() - 1, juce::roundToInt (v));
            oversample.setButtonText ("OVERSAMPLE  " + params::oversampleFactors[i]);
        });

    oversampleAttachment->sendInitialUpdate();

    timerCallback();
    startTimerHz (4);   // nobody needs the sample rate or the load sooner than that
}

Footer::~Footer() = default;

void Footer::timerCallback()
{
    const double rate = amp.currentSampleRate();
    const auto text = rate > 0.0 ? juce::String (rate / 1000.0, rate < 100000.0 ? 1 : 0) + " KHZ"
                                 : juce::String ("— KHZ");

    loadPercent = amp.dspLoadPercent();
    const auto load = juce::String (juce::roundToInt (loadPercent)) + "%";

    if (text != rateText || load != loadText)
    {
        rateText = text;
        loadText = load;
        repaint();
    }
}

void Footer::showOversampleMenu()
{
    const int current = juce::jlimit (0, params::oversampleFactors.size() - 1,
                                      juce::roundToInt (amp.apvts.getRawParameterValue (params::oversample)->load()));

    juce::PopupMenu menu;
    for (int i = 0; i < params::oversampleFactors.size(); ++i)
        menu.addItem (i + 1, params::oversampleFactors[i], true, i == current);

    // Upward, because the strip is at the bottom of the window.
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&oversample)
                                                  .withPreferredPopupDirection (juce::PopupMenu::Options::PopupDirection::upwards),
                        [this] (int choice)
                        {
                            if (choice > 0)
                                oversampleAttachment->setValueAsCompleteGesture ((float) (choice - 1));
                        });
}

void Footer::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();

    g.setColour (theme::hair);
    g.fillRect (r.removeFromTop (1.0f));

    auto right = r.withTrimmedLeft ((float) (itemWidth + gap));

    g.setColour (theme::txFaint);
    theme::drawTracked (g, rateText, right.removeFromLeft (70.0f), theme::displayFont (8.0f), 0.1f,
                        juce::Justification::centredLeft);

    // The load turns warm as it climbs — a number you only notice when it starts to matter.
    g.setColour (loadPercent > 80.0f ? theme::orange
               : loadPercent > 50.0f ? theme::lilac
                                     : theme::txFaint);
    theme::drawTracked (g, "DSP " + loadText, right.removeFromLeft (80.0f), theme::displayFont (8.0f), 0.1f,
                        juce::Justification::centredLeft);
}

void Footer::resized()
{
    oversample.setBounds (getLocalBounds().withTrimmedTop (1).removeFromLeft (itemWidth));
}

} // namespace orbitamp
