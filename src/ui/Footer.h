// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "Theme.h"

#include <felitronics/appkit/VersionBadge.h>
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

    /** Four taller than it was. The strip carries FACTS a player reads — the build, the rate, the
        cost — and it was setting them at 12 px, under the house floor of 13. The floor won; the
        strip grew to hold it. */
    static constexpr int designHeight = 26;

    /** The version window — the build stamp, the licence, the short trademark notice and the tip
        jar — opened by a click on the stamp itself. It used to have a second door in the gear's
        menu, and the menu is a window now; nothing was lost, because the door that remains is the
        one that was always right: the words the page is ABOUT. */
    void showAbout() { versionBadge.showAbout(); }

    /** DEVICES & TRADEMARKS was reached from the gear's menu, and the gear is a window now. Its
        door belongs down here anyway: the page is a LIST of what is installed on this machine,
        which is a fact about the run, like the sample rate and the load beside it — and it ends
        up next to the build stamp, the other page that is only ever read. */
    std::function<void()> onDevices;

private:
    void timerCallback() override;
    void showLoadBreakdown();

    // Every width here is the 12 px one scaled by the 13/12 the type grew by: the same words,
    // the same air around them. `rateWidth` and `loadWidth` are constants rather than literals
    // because paint() draws in them and resized() puts the click target over them — two places
    // that must never disagree, and did, as two bare numbers.
    static constexpr int itemWidth   = 100;
    static constexpr int stereoWidth = 134;   // room for the longest of the three modes, STEREO SPACE
    static constexpr int gap       = 10;
    static constexpr int stampWidth = 184;    // the version + format line at the far right
    static constexpr int devicesWidth = 92;   // the DEVICES door, just left of the stamp
    static constexpr int rateWidth = 104;     // the host's sample rate
    static constexpr int loadWidth = 108;     // "DSP 12%", and the door to the breakdown

    AmpProcessor& amp;

    felitronics::appkit::chrome::FlatItem stereo;

    /** Numbers on this strip are doors: an invisible target over the painted text, and the click
        opens what the number is about. The DSP figure opens the per-stage breakdown; the stamp
        opens appkit's version popover. */
    struct ClickTarget final : public juce::Component,
                              public juce::SettableTooltipClient
    {
        std::function<void()> onClick;
        void mouseDown  (const juce::MouseEvent&) override { if (onClick) onClick(); }

        // The painted text lives in the parent, so the parent is what repaints: the strip lifts the
        // words under the cursor, which is the only thing here that says they can be pressed.
        void mouseEnter (const juce::MouseEvent&) override { if (auto* p = getParentComponent()) p->repaint(); }
        void mouseExit  (const juce::MouseEvent&) override { if (auto* p = getParentComponent()) p->repaint(); }
    };

    ClickTarget loadBadge;
    ClickTarget devicesBadge;
    ClickTarget stampBadge;

    /** appkit's version badge, kept INVISIBLE and only for its window — the whole build stamp, the
        opt-in update check and the family's tip jar, one `showPopup()` away (public since appkit
        v0.11.3). Its own face paints two lines, which is right for a toolbar corner and wrong for a
        22 px strip: the second line would sit on the window's bottom edge. So this strip paints the
        stamp itself, in its own voice and one line — and the badge stays as the popover's anchor,
        sized to that text so the callout points at it.

        The strip, not the toolbar, for the reason everything else on this line is here: it is a fact
        about the RUN, not about the sound — and the toolbar has no room to spare either. */
    felitronics::appkit::VersionBadge versionBadge;

    std::unique_ptr<juce::ParameterAttachment> stereoAttachment;

    juce::String rateText, loadText;
    juce::String stampText;            // "V0.2.0 · STANDALONE" — fixed for the life of the window
    bool  updateDot  = false;          // a stored release is newer than this build
    float loadPercent = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Footer)
};

} // namespace orbitamp
