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
    /** What is the amp's own in the version window (OrbitAmp's, or OrbitBass's) — its page (ProductPage.h), the family's tag on the
        maker's page and the tip jar; the rest of it is aboutConfig's. */
    appkit::VersionBadge::Config badgeConfig (const juce::String& name, bool standalone)
    {
        return aboutConfig (name, productPageUrl (name, standalone), familyLinkTag (name));
    }
}

Footer::Footer (AmpProcessor& processor)
    : amp (processor),
      versionBadge (processor.updateChecker(), badgeConfig (processor.product().name, processor.wrapperType == juce::AudioProcessor::wrapperType_Standalone), processor.pluginFormat())
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

    // Its place is as wide as its longest word, whichever is showing.
    for (const auto& mode : params::stereoModes)
    {
        stereo.setButtonText (mode.toUpperCase());
        stereoSlotW = juce::jmax (stereoSlotW, stereo.preferredWidth (0));
    }

    stereoAttachment = std::make_unique<juce::ParameterAttachment> (
        *amp.apvts.getParameter (params::stereoMode),
        [this] (float v)
        {
            stereo.setButtonText (params::stereoModes[juce::jlimit (0, params::stereoModes.size() - 1,
                                                                    juce::roundToInt (v))].toUpperCase());
        });

    stereoAttachment->sendInitialUpdate();

    // The bass's chain is mono, whatever the mode says (Product::chain): nothing to switch — its place holds the three
    // effects' boxes instead (the owner, 01.11).
    if (amp.product().chain == Chain::bass)
    {
        stereo.setVisible (false);
        for (const auto& [id, word, hint] : { std::tuple { params::octavePresent, "OCT", "OCTAVE" },
                                              std::tuple { params::filterPresent, "ENV", "ENVELOPE" },
                                              std::tuple { params::chorusPresent, "CHR", "CHORUS" },
                                              std::tuple { params::delayPresent,  "DEL", "DELAY" },
                                              std::tuple { params::reverbPresent, "REV", "REVERB" } })
        {
            presence.push_back (std::make_unique<PresenceBox> (amp, id, word, hint));
            addAndMakeVisible (*presence.back());
        }
        stereoSlotW = (int) presence.size() * boxW + ((int) presence.size() - 1) * boxGap;
    }

    loadBadge.onClick = [this] { showLoadBreakdown(); };
    loadBadge.setMouseCursor (juce::MouseCursor::PointingHandCursor);
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
    // would say it, and nothing after it: the running wrapper is said in the window the stamp
    // opens, on its environment line, and the strip has no room for a word only that window needs.
    stampText = "V" + amp.updateChecker().currentVersion();

    // The SAME window the gear opens: centred, opaque, on its own dimmed ground. A call-out here
    // hung a half-transparent card over the faceplate and read as a tooltip that would not go away.
    stampBadge.onClick = [this] { versionBadge.showAbout(); };
    stampBadge.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    stampBadge.setTooltip ("Version, build stamp and updates");
    addAndMakeVisible (stampBadge);

    timerCallback();
    startTimerHz (4);   // nobody needs the sample rate or the load sooner than that
}

Footer::~Footer()
{
    // The load window holds the processor: it never outlives the footer. It stops reading and hides at once; the callout
    // goes by its own dismissal (the modal manager owns it).
    if (auto* box = loadBox.getComponent())
    {
        for (auto* c : box->getChildren())
            if (auto* lb = dynamic_cast<LoadBreakdown*> (c))
                lb->detach();
        box->setVisible (false);
        box->dismiss();
    }
}

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

Footer::Places Footer::places() const
{
    const auto font  = theme::displayFont (13.0f);
    const auto width = [&font] (const juce::String& s)
    {
        return juce::roundToInt (std::ceil (theme::trackedWidth (s, font, 0.1f)));
    };

    constexpr int dotRoom = 14;   // the update dot rides after the stamp

    // The longest each can read: a rate at 88.2 and a load at three figures.
    int w[] = { stereoSlotW, width ("88.2 KHZ"), width ("DSP 100%"), width ("DEVICES"),
                width (stampText) + dotRoom };

    // The bass's five boxes stand only where all five fit beside the facts at the strip's least gaps — else none of them
    // (the owner, 01.11): the strip in the plug-in's menu still has them.
    if (! presence.empty())
    {
        int facts = 0;
        for (size_t i = 1; i < std::size (w); ++i)
            facts += w[i];
        if (facts + stereoSlotW + 4 * 8 > getWidth())
            w[0] = 0;
    }

    int total = 0;
    for (const int x : w)
        total += x;

    auto r = getLocalBounds();
    const int gap = juce::jmax (8, (r.getWidth() - total) / 4);

    Places p;
    // The stereo item centres its word in its own place, and a place exactly the word's width
    // drops the last letter — so the item is the place with a little air either side.
    if (presence.empty())
        p.stereo = r.removeFromLeft (w[0]).expanded (6, 0);
    else
        p.stereo = r.removeFromLeft (w[0]);
    if (w[0] > 0)
        r.removeFromLeft (gap);
    p.rate    = r.removeFromLeft (w[1]);  r.removeFromLeft (gap);
    p.load    = r.removeFromLeft (w[2]);  r.removeFromLeft (gap);
    p.devices = r.removeFromLeft (w[3]);  r.removeFromLeft (gap);
    p.stamp   = r.removeFromLeft (w[4]);
    return p;
}

void Footer::paint (juce::Graphics& g)
{
    const auto p = places();
    const auto font = theme::displayFont (13.0f);

    g.setColour (theme::txFaint);
    theme::drawTracked (g, rateText, p.rate.toFloat(), font, 0.1f, juce::Justification::centredLeft);

    // The load turns warm as it climbs — a number you only notice when it starts to matter. Cool,
    // it lifts under the mouse like the other doors on this strip.
    g.setColour (loadPercent > 80.0f ? theme::orange
               : loadPercent > 50.0f ? theme::lilac
               : loadBadge.isMouseOver() ? theme::tx
                                     : theme::txFaint);
    theme::drawTracked (g, "DSP " + loadText, p.load.toFloat(), font, 0.1f, juce::Justification::centredLeft);

    // The two pages that are only read: what is installed, and what this build is.
    g.setColour (devicesBadge.isMouseOver() ? theme::tx : theme::txFaint);
    theme::drawTracked (g, "DEVICES", p.devices.toFloat(), font, 0.1f, juce::Justification::centredLeft);

    g.setColour (stampBadge.isMouseOver() ? theme::tx : theme::txFaint);
    theme::drawTracked (g, stampText, p.stamp.toFloat(), font, 0.1f, juce::Justification::centredLeft);

    // Its dot is the family's "needs a look": a release seen by an earlier, deliberate check is
    // newer than what is running. Right after the words it is about.
    if (updateDot)
    {
        const float x  = (float) p.stamp.getX() + theme::trackedWidth (stampText, font, 0.1f) + 9.0f;
        const float cy = (float) p.stamp.getCentreY();
        g.setColour (theme::orange);
        g.fillEllipse (x - 3.0f, cy - 3.0f, 6.0f, 6.0f);
    }
}

void Footer::resized()
{
    const auto p = places();
    stereo.setBounds (p.stereo);
    {
        auto row = p.stereo;
        for (auto& b : presence)
        {
            b->setVisible (! p.stereo.isEmpty());
            b->setBounds (row.removeFromLeft (boxW));   // the row's whole height: its word centred where the facts' are
            row.removeFromLeft (boxGap);
        }
    }

    // The stamp's click target, and under it the badge that owns the popover — same rectangle, so
    // the callout's arrow points at the words that opened it.
    stampBadge  .setBounds (p.stamp);
    versionBadge.setBounds (p.stamp);

    devicesBadge.setBounds (p.devices.withWidth (juce::roundToInt (
        theme::trackedWidth ("DEVICES", theme::displayFont (13.0f), 0.1f)) + 4));

    // The invisible click target over the painted DSP number.
    loadBadge.setBounds (p.load);
}

Footer::PresenceBox::PresenceBox (AmpProcessor& processor, const char* paramId, const juce::String& w, const juce::String& hint)
    : amp (processor), param (*processor.apvts.getParameter (paramId)), word (w)
{
    att = std::make_unique<juce::ParameterAttachment> (param, [this] (float) { repaint(); });
    setTooltip (hint);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

bool Footer::PresenceBox::on() const { return param.getValue() > 0.5f; }

void Footer::PresenceBox::paint (juce::Graphics& g)
{
    // A word and its underline, as the strip's flat items (the owner, 01.11): no box, no fill — in, the word and its line
    // in the violet of what works; out, both faint.
    const auto r    = getLocalBounds().toFloat();
    const bool over = isMouseOver();
    const auto c    = on() ? (over ? theme::violet.brighter (0.35f) : theme::violet.brighter (0.15f))
                           : (over ? theme::tx : theme::txFaint);
    g.setColour (c);
    // On the strip's own line: centred on the row as every fact beside it, the strip's tracking — the underline under it.
    theme::drawTracked (g, word, r, theme::displayFont (13.0f), 0.1f, juce::Justification::centred);
    const float lineH = on() ? 2.0f : 1.0f, lineY = r.getCentreY() + 9.0f;
    g.setColour (on() ? c : theme::hair);
    g.fillRect (r.getX() + 2.0f, lineY, r.getWidth() - 4.0f, lineH);
}

void Footer::PresenceBox::mouseDown (const juce::MouseEvent&)
{
    amp.history.beginGesture();
    att->setValueAsCompleteGesture (on() ? 0.0f : 1.0f);
    amp.history.endGesture();
}

void Footer::showLoadBreakdown()
{
    if (auto* box = loadBox.getComponent())
        box->dismiss();
    loadBox = &openLoadBreakdown (amp, loadBadge.getScreenBounds(), getTopLevelComponent()->getScreenBounds());
}

} // namespace orbitamp
