// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "../ProductPage.h"
#include "Footer.h"

#include "AboutConfig.h"
#include "LoadBreakdown.h"

#include "../Parameters.h"
#include "../PluginProcessor.h"

#include <BinaryData.h>
#include <OrbitAmpVersion.h>

namespace orbitamp
{

namespace appkit = felitronics::appkit;
namespace chrome = felitronics::appkit::chrome;

namespace
{
    /** The product's own tag on a family link, for the SITE's analytics — the same pair OrbitAmp's
        plugin links have always carried. Everything that describes the BUILD is added by appkit:
        `from` (the product slug), `platform` (the machine) and `format` (the wrapper). None of it
        reaches the payment page — the hop logs the query and redirects clean. */
    /** What is OrbitAmp's own in the version window — its page (ProductPage.h), the family's tag on the
        maker's page and the tip jar; the rest of it is aboutConfig's. */
    appkit::VersionBadge::Config badgeConfig (bool standalone)
    {
        return aboutConfig ("OrbitAmp", productPageUrl ("OrbitAmp", standalone), familyLinkTag ("OrbitAmp"));
    }
}

Footer::Footer (AmpProcessor& processor)
    : amp (processor),
      versionBadge (processor.updateChecker(), badgeConfig (processor.wrapperType == juce::AudioProcessor::wrapperType_Standalone), processor.pluginFormat())
{
    // MONO | STEREO | STEREO SPACE: the click walks the loop — parameter truth, no local state.
    stereo.theme = chrome::ChromeTheme { .fill       = theme::panel,
                                         .underline  = theme::hair,
                                         .accent     = theme::violet,
                                         .attention  = theme::orange,
                                         .text       = theme::tx,
                                         .textDim    = theme::txDim,
                                         .activeText = juce::Colours::white };

    // appkit's flat item opens at 12 px. The word it wears — MONO, STEREO, STEREO SPACE — is read
    // like every other fact on this strip, so it stands at the floor with them.
    stereo.textHeight = 13.0f;

    stereo.onClick = [this]
    {
        auto* p = amp.apvts.getParameter (params::stereoMode);
        const int now = juce::roundToInt (p->convertFrom0to1 (p->getValue()));
        stereoAttachment->setValueAsCompleteGesture ((float) ((now + 1) % params::stereoModes.size()));
    };
    addAndMakeVisible (stereo);

    stereoAttachment = std::make_unique<juce::ParameterAttachment> (
        *amp.apvts.getParameter (params::stereoMode),
        [this] (float v)
        {
            stereo.setButtonText (params::stereoModes[juce::jlimit (0, params::stereoModes.size() - 1,
                                                                    juce::roundToInt (v))].toUpperCase());
        });

    stereoAttachment->sendInitialUpdate();

    loadBadge.onClick = [this] { showLoadBreakdown(); };
    addAndMakeVisible (loadBadge);

    devicesBadge.onClick = [this] { if (onDevices) onDevices(); };
    devicesBadge.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    devicesBadge.setTooltip ("Devices and trademarks: every device the installed packs name");
    addAndMakeVisible (devicesBadge);

    // The popover wears the header's wordmark, so the two read as one product — and it is now the
    // SAME object, not a second parse of the same bytes: appkit hands out one Typeface for the
    // family's face and everything that wants it asks for that one.
    versionBadge.setBrandTypeface (theme::displayTypeface());
    addChildComponent (versionBadge);   // never shown: the strip paints the line, the badge holds the popover

    // The line this strip draws for itself, in the strip's own voice — the version as the toolbar
    // would say it, the running wrapper after it.
    stampText = "V" + amp.updateChecker().currentVersion() + juce::String::fromUTF8 (" \xc2\xb7 ")
                    + amp.pluginFormat().toUpperCase();

    // The SAME window the gear opens: centred, opaque, on its own dimmed ground. A call-out here
    // hung a half-transparent card over the faceplate and read as a tooltip that would not go away.
    stampBadge.onClick = [this] { versionBadge.showAbout(); };
    stampBadge.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    stampBadge.setTooltip ("Version, build stamp and updates");
    addAndMakeVisible (stampBadge);

    timerCallback();
    startTimerHz (4);   // nobody needs the sample rate or the load sooner than that
}

Footer::~Footer() = default;

void Footer::timerCallback()
{
    const double rate = amp.currentSampleRate();
    const auto text = rate > 0.0 ? juce::String (rate / 1000.0, rate < 100000.0 ? 1 : 0) + " KHZ"
                                 : juce::String ("- KHZ");

    loadPercent = amp.dspLoadPercent();
    const auto load = juce::String (juce::roundToInt (loadPercent)) + "%";

    // The dot only ever moves after a deliberate check — reading it is a lookup in the badge's own
    // settings file, on this thread, four times a second.
    const bool upd = amp.updateChecker().updateAvailable();

    if (text != rateText || load != loadText || upd != updateDot)
    {
        rateText  = text;
        loadText  = load;
        updateDot = upd;
        repaint();
    }
}

void Footer::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();

    g.setColour (theme::hair);
    g.fillRect (r.removeFromTop (1.0f));

    // The build stamp, right-aligned at the far end. Its dot is the family's "needs a look": a
    // release seen by an earlier, deliberate check is newer than what is running.
    auto stamp = r.removeFromRight ((float) stampWidth).withTrimmedRight (8.0f);
    const auto dot = stamp.removeFromRight (12.0f);

    if (updateDot)
    {
        g.setColour (theme::orange);
        g.fillEllipse (dot.getCentreX() - 3.0f, dot.getCentreY() - 3.0f, 6.0f, 6.0f);
    }

    g.setColour (stampBadge.isMouseOver() ? theme::tx : theme::txFaint);
    theme::drawTracked (g, stampText, stamp, theme::displayFont (13.0f), 0.1f,
                        juce::Justification::centredRight);

    // The other page that is only read, beside the one that is: what is installed, and what
    // this build is.
    {
        auto d = r.removeFromRight ((float) devicesWidth).withTrimmedRight (14.0f);
        g.setColour (devicesBadge.isMouseOver() ? theme::tx : theme::txFaint);
        theme::drawTracked (g, "DEVICES", d, theme::displayFont (13.0f), 0.1f,
                            juce::Justification::centredRight);
    }

    auto right = r.withTrimmedLeft ((float) (stereoWidth + gap));

    g.setColour (theme::txFaint);
    theme::drawTracked (g, rateText, right.removeFromLeft ((float) rateWidth), theme::displayFont (13.0f), 0.1f,
                        juce::Justification::centredLeft);

    // The load turns warm as it climbs — a number you only notice when it starts to matter.
    g.setColour (loadPercent > 80.0f ? theme::orange
               : loadPercent > 50.0f ? theme::lilac
                                     : theme::txFaint);
    theme::drawTracked (g, "DSP " + loadText, right.removeFromLeft ((float) loadWidth), theme::displayFont (13.0f), 0.1f,
                        juce::Justification::centredLeft);
}

void Footer::resized()
{
    auto row = getLocalBounds().withTrimmedTop (1);
    stereo.setBounds (row.removeFromLeft (stereoWidth));

    // The stamp's click target, and under it the badge that owns the popover — same rectangle, so
    // the callout's arrow points at the words that opened it.
    const auto stamp = row.removeFromRight (stampWidth).withTrimmedRight (8);
    stampBadge  .setBounds (stamp);
    versionBadge.setBounds (stamp);

    devicesBadge.setBounds (row.removeFromRight (devicesWidth).withTrimmedRight (14));

    // The invisible click target over the painted DSP number.
    row.removeFromLeft (gap + rateWidth);
    loadBadge.setBounds (row.removeFromLeft (loadWidth));
}

void Footer::showLoadBreakdown()
{
    openLoadBreakdown (amp, loadBadge.getScreenBounds());
}

} // namespace orbitamp
