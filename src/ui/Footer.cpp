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

    // MONO | STEREO: binary, so the click IS the toggle — parameter truth, no local state.
    stereo.theme = oversample.theme;
    stereo.onClick = [this]
    {
        auto* p = amp.apvts.getParameter (params::stereoMode);
        stereoAttachment->setValueAsCompleteGesture (p->getValue() > 0.5f ? 0.0f : 1.0f);
    };
    addAndMakeVisible (stereo);

    stereoAttachment = std::make_unique<juce::ParameterAttachment> (
        *amp.apvts.getParameter (params::stereoMode),
        [this] (float v) { stereo.setButtonText (v > 0.5f ? "STEREO" : "MONO"); });

    stereoAttachment->sendInitialUpdate();

    loadBadge.onClick = [this] { showLoadBreakdown(); };
    addAndMakeVisible (loadBadge);

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

    auto right = r.withTrimmedLeft ((float) (itemWidth + 66 + gap * 2));

    g.setColour (theme::txFaint);
    theme::drawTracked (g, rateText, right.removeFromLeft (96.0f), theme::displayFont (12.0f), 0.1f,
                        juce::Justification::centredLeft);

    // The load turns warm as it climbs — a number you only notice when it starts to matter.
    g.setColour (loadPercent > 80.0f ? theme::orange
               : loadPercent > 50.0f ? theme::lilac
                                     : theme::txFaint);
    theme::drawTracked (g, "DSP " + loadText, right.removeFromLeft (100.0f), theme::displayFont (12.0f), 0.1f,
                        juce::Justification::centredLeft);
}

void Footer::resized()
{
    auto row = getLocalBounds().withTrimmedTop (1);
    oversample.setBounds (row.removeFromLeft (itemWidth));
    stereo.setBounds (row.removeFromLeft (66));

    // The invisible click target over the painted DSP number.
    row.removeFromLeft (gap + 96);
    loadBadge.setBounds (row.removeFromLeft (100));
}

void Footer::showLoadBreakdown()
{
    // orbitcab's grammar: rows of stage, bar and number — the whole chain's cost, itemised.
    struct Panel final : public juce::Component,
                         private juce::Timer
    {
        explicit Panel (AmpProcessor& p) : amp (p)
        {
            setSize (240, 24 + AmpProcessor::numStages * rowH + 8);
            startTimerHz (10);
        }

        void paint (juce::Graphics& g) override
        {
            static const char* const names[AmpProcessor::numStages] = {
                "TOTAL", "GATE", "EQ 1", "BOOST", "EQ 2", "PREAMP", "REVERB", "POWER", "CAB", "OUT",
            };

            auto r = getLocalBounds().reduced (12, 4);

            g.setColour (theme::tx);
            theme::drawTracked (g, "DSP LOAD", r.removeFromTop (22).toFloat(),
                                theme::displayFont (12.0f), 0.1f, juce::Justification::centredLeft);

            for (int i = 0; i < AmpProcessor::numStages; ++i)
            {
                auto row = r.removeFromTop (rowH);
                const float v = amp.stageLoad[i].load();
                const bool total = i == AmpProcessor::stTotal;

                g.setColour (total ? theme::tx : theme::txDim);
                theme::drawTracked (g, names[i], row.removeFromLeft (64).toFloat(),
                                    theme::displayFont (11.0f), 0.08f, juce::Justification::centredLeft);

                auto num = row.removeFromRight (46);
                g.setColour (v > 50.0f ? theme::orange : total ? theme::tx : theme::txDim);
                theme::drawTracked (g, juce::String (v, 1) + "%", num.toFloat(),
                                    theme::displayFont (11.0f), 0.06f, juce::Justification::centredRight);

                auto bar = row.reduced (6, 7).toFloat();
                g.setColour (theme::hair);
                g.fillRoundedRectangle (bar, 2.0f);
                g.setColour (total ? theme::violet : theme::orange.withAlpha (0.8f));
                g.fillRoundedRectangle (bar.withWidth (bar.getWidth()
                                                        * juce::jlimit (0.0f, 1.0f, v / 100.0f)),
                                        2.0f);
            }
        }

        void timerCallback() override { repaint(); }

        enum { rowH = 19 };
        AmpProcessor& amp;
    };

    auto panel = std::make_unique<Panel> (amp);
    juce::CallOutBox::launchAsynchronously (std::move (panel),
                                            loadBadge.getScreenBounds(), nullptr);
}

} // namespace orbitamp
