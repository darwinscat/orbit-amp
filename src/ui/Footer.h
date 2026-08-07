#pragma once

#include "Theme.h"

#include <felitronics/appkit/chrome/FlatButtons.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace orbitamp
{

class AmpProcessor;

/** The bottom strip: facts about the RUN rather than about the sound.

    Oversampling, the host's sample rate, and what the plugin is costing. None of these belong on a
    block — a block says what the amp is, and this says what the machine is doing. It is the same
    shape as the sibling EQ's footer: flat items whose menus open upward, and readouts beside them.

    Sample rate and load are polled rather than pushed: the audio thread should not be reaching into
    a view, and nobody needs either number sooner than a few times a second. */
class Footer final : public juce::Component,
                     private juce::Timer
{
public:
    explicit Footer (AmpProcessor&);
    ~Footer() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int designHeight = 22;

private:
    void timerCallback() override;
    void showOversampleMenu();

    static constexpr int itemWidth = 92;
    static constexpr int gap       = 10;

    AmpProcessor& amp;

    felitronics::appkit::chrome::FlatItem oversample;

    std::unique_ptr<juce::ParameterAttachment> oversampleAttachment;

    juce::String rateText, loadText;
    float loadPercent = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Footer)
};

} // namespace orbitamp
