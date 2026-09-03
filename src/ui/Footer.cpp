// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "Footer.h"

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
    /** The tip jar the badge's foot offers — ONE hop for the whole suite, darwinscat.com's
        deliberate 302, whose target the site steers server-side: a URL baked into a build that
        ships today still lands on the right jar years from now.

        `platform` says which machine fed the cat; the badge adds `from=orbitamp` itself. Neither
        parameter reaches the payment page — the hop logs them and redirects clean — so they are
        access-log statistics and nothing more. Written here first (house rule): the OS split
        belongs in appkit's brand::feedTheCatLink once a second product wants it. */
    juce::String feedTheCatBase()
    {
       #if JUCE_MAC
        const char* const platform = "macos";
       #elif JUCE_WINDOWS
        const char* const platform = "windows";
       #elif JUCE_LINUX
        const char* const platform = "linux";
       #else
        const char* const platform = "other";
       #endif

        return juce::String (appkit::brand::feedTheCatUrl) + "?platform=" + platform;
    }

    /** Everything the badge cannot know: who we are, what this build is, and the palette it draws
        in. The version and the GitHub slug come from the checker, so they can never drift. */
    appkit::VersionBadge::Config badgeConfig()
    {
        // JUCE names its own version at compile time — "JUCE v8.0.14"; the row wants the tag alone.
        const juce::String juceVersion = juce::SystemStats::getJUCEVersion().fromFirstOccurrenceOf (" ", false, false);

        // The maker's mark beside the product's, the way the window header carries both: the same
        // embedded cat, drawn from the same bytes. The popover outlives the badge that launched it,
        // so the drawable is shared rather than borrowed.
        std::shared_ptr<juce::Drawable> cat { juce::Drawable::createFromImageData (
            BinaryData::catlogo_svg, (size_t) BinaryData::catlogo_svgSize).release() };

        return { .productName  = "OrbitAmp",
                 .productUrl   = "https://darwinscat.com/orbitamp?utm_source=orbitamp&utm_medium=plugin",
                 .gitHash      = version::kGitHash,
                 .buildNumber  = version::kBuildNumber,
                 .buildCount   = version::kBuildCount,
                 .gitDirty     = version::kGitDirty,
                 .os           = version::kOS,
                 .arch         = version::kArch,
                 .builder      = version::kBuilder,
                 .licence      = "AGPL-3.0+",   // the row is one line: the long spelling truncates
                 // What this binary was actually built against — sibling checkout or pin, and the
                 // commit when there was a checkout to ask. The stamp header bakes it at build time.
                 .dependencies = { { .label     = "felitronics-core",
                                     .version   = version::kCoreVersion,
                                     .ownerRepo = "darwinscat/felitronics-core",
                                     .commit    = version::kCoreCommit,
                                     .state     = version::kCoreState },
                                   { .label     = "felitronics-appkit",
                                     .version   = version::kAppkitVersion,
                                     .ownerRepo = "darwinscat/felitronics-appkit",
                                     .commit    = version::kAppkitCommit,
                                     .state     = version::kAppkitState },
                                   { .label     = "namz",
                                     .version   = version::kNamzVersion,
                                     .ownerRepo = "darwinscat/namz",
                                     .commit    = version::kNamzCommit,
                                     .state     = version::kNamzState },
                                   { .label     = "NeuralAmpModelerCore",
                                     .version   = version::kNamCoreVersion,
                                     .ownerRepo = "sdatkinson/NeuralAmpModelerCore",
                                     .commit    = version::kNamCoreCommit,
                                     .state     = "pin" },
                                   { .label     = "JUCE",
                                     .version   = juceVersion,
                                     .ownerRepo = "juce-framework/JUCE" } },
                 .drawByline   = [cat] (juce::Graphics& g, float cx, float cy, float d)
                                 {
                                     if (cat != nullptr)
                                         cat->drawWithin (g, { cx - d * 0.5f, cy - d * 0.5f, d, d },
                                                          juce::RectanglePlacement::centred, 1.0f);
                                 },
                 .accent       = theme::violet,
                 .accentHover  = theme::lilac,
                 .accentB      = theme::orange,
                 .text         = theme::tx,
                 .feedUrl      = feedTheCatBase() };
    }
}

Footer::Footer (AmpProcessor& processor)
    : amp (processor),
      versionBadge (processor.updateChecker(), badgeConfig(), processor.pluginFormat())
{
    // MONO | STEREO | STEREO SPACE: the click walks the loop — parameter truth, no local state.
    stereo.theme = chrome::ChromeTheme { .fill       = theme::panel,
                                         .underline  = theme::hair,
                                         .accent     = theme::violet,
                                         .attention  = theme::orange,
                                         .text       = theme::tx,
                                         .textDim    = theme::txDim,
                                         .activeText = juce::Colours::white };
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

    // The popover wears the header's wordmark, so the two read as one product. appkit keeps
    // BrandHeader's own typeface private, so this is the same embedded bytes, loaded once per window.
    versionBadge.setBrandTypeface (juce::Typeface::createSystemTypefaceFor (
        BinaryData::MichromaRegular_ttf, (size_t) BinaryData::MichromaRegular_ttfSize));
    addChildComponent (versionBadge);   // never shown: the strip paints the line, the badge holds the popover

    // The line this strip draws for itself, in the strip's own voice — the version as the toolbar
    // would say it, the running wrapper after it.
    stampText = "V" + amp.updateChecker().currentVersion() + juce::String::fromUTF8 (" \xc2\xb7 ")
                    + amp.pluginFormat().toUpperCase();

    stampBadge.onClick = [this] { versionBadge.showPopup(); };
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
    theme::drawTracked (g, stampText, stamp, theme::displayFont (12.0f), 0.1f,
                        juce::Justification::centredRight);

    auto right = r.withTrimmedLeft ((float) (stereoWidth + gap));

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
    stereo.setBounds (row.removeFromLeft (stereoWidth));

    // The stamp's click target, and under it the badge that owns the popover — same rectangle, so
    // the callout's arrow points at the words that opened it.
    const auto stamp = row.removeFromRight (stampWidth).withTrimmedRight (8);
    stampBadge  .setBounds (stamp);
    versionBadge.setBounds (stamp);

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
            setSize (320, 24 + graphH + 6 + AmpProcessor::numStages * rowH + 30 + 26);
            startTimerHz (15);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (resetArea().contains (e.getPosition()))
            {
                for (auto& w : amp.stageWorst)
                    w.store (0.0f);
                amp.overruns.store (0);
                for (auto& hcol : amp.loadHist)
                    hcol.store (0.0f);
                repaint();
                return;
            }

            if (copyArea().contains (e.getPosition()))
            {
                juce::SystemClipboard::copyTextToClipboard (report());
                copied = 24;   // a moment of confirmation on the button
                repaint();
            }
        }

        juce::String report() const
        {
            static const char* const names[AmpProcessor::numStages] = {
                // ASCII on purpose: these run through formatted ("%-8s"), where a multi-byte
                // middle dot breaks both the encoding and the column width.
                "TOTAL", "TUNER", "GATE", "BOOST", "B-EQ", "PREAMP", "P-EQ", "DELAY", "REVERB",
                "POWER", "CAB", "LIMIT", "OUT",
            };

            juce::String t;
            t << "OrbitAmp DSP report  |  " << juce::String (amp.currentSampleRate() / 1000.0, 1)
              << " kHz  |  block " << amp.getBlockSize()
              << "  |  " << params::stereoModes[juce::jlimit (0, params::stereoModes.size() - 1,
                                                              juce::roundToInt (amp.apvts.getRawParameterValue (params::stereoMode)->load()))].toUpperCase()
              << "\n";
            t << juce::String::formatted ("%-8s %8s %8s\n", "STAGE", "MEAN", "WORST");

            for (int i = 0; i < AmpProcessor::numStages; ++i)
                t << juce::String::formatted ("%-8s %7.1f%% %7.0f%%\n", names[i],
                                              amp.stageLoad[i].load(), amp.stageWorst[i].load());

            t << "OVERRUNS " << (int) amp.overruns.load() << "\n";
            return t;
        }

        void paint (juce::Graphics& g) override
        {
            static const char* const names[AmpProcessor::numStages] = {
                "TOTAL", "TUNER", "GATE", "BOOST", "BOOST EQ", "PREAMP", "PREAMP EQ", "DELAY",
                "REVERB", "POWER", "CAB", "LIMIT", "OUT",
            };

            auto r = getLocalBounds().reduced (12, 4);

            {
                auto head = r.removeFromTop (22);
                g.setColour (theme::tx);
                theme::drawTracked (g, "DSP LOAD", head.toFloat(),
                                    theme::displayFont (12.0f), 0.1f, juce::Justification::centredLeft);
                g.setColour (theme::txDim);
                theme::drawTracked (g, "MEAN / WORST", head.toFloat(),
                                    theme::displayFont (10.0f), 0.08f, juce::Justification::centredRight);
            }

            // The strip chart: ~12 s of worst-in-column shares, the budget line at 100% —
            // anything over it is a block that missed its deadline, i.e. an audible drop.
            {
                auto plot = r.removeFromTop (graphH).toFloat();
                r.removeFromTop (6);

                g.setColour (theme::bezel);
                g.fillRoundedRectangle (plot, 3.0f);

                constexpr float ceilPct = 250.0f;
                const float y100 = plot.getBottom() - plot.getHeight() * (100.0f / ceilPct);

                g.setColour (theme::hair2);
                g.fillRect (plot.getX(), y100, plot.getWidth(), 1.0f);

                const int pos = amp.loadHistPos.load (std::memory_order_acquire);
                const float colW = plot.getWidth() / (float) AmpProcessor::loadHistSize;

                for (int i = 0; i < AmpProcessor::loadHistSize; ++i)
                {
                    const float v = amp.loadHist[(size_t) ((pos + 1 + i)
                                                           % AmpProcessor::loadHistSize)].load();
                    if (v <= 0.01f)
                        continue;

                    const float hgt = plot.getHeight()
                                    * juce::jlimit (0.0f, 1.0f, v / ceilPct);
                    g.setColour (v > 100.0f ? theme::orange : theme::violet.withAlpha (0.75f));
                    g.fillRect (plot.getX() + (float) i * colW, plot.getBottom() - hgt,
                                juce::jmax (1.0f, colW), hgt);
                }
            }

            for (int i = 0; i < AmpProcessor::numStages; ++i)
            {
                auto row = r.removeFromTop (rowH);
                const float v = amp.stageLoad[i].load();
                const bool total = i == AmpProcessor::stTotal;

                g.setColour (total ? theme::tx : theme::txDim);
                theme::drawTracked (g, names[i], row.removeFromLeft (64).toFloat(),
                                    theme::displayFont (11.0f), 0.08f, juce::Justification::centredLeft);

                const float w = amp.stageWorst[i].load();

                auto worst = row.removeFromRight (52);
                g.setColour (w > 100.0f ? theme::orange : theme::txFaint);
                theme::drawTracked (g, juce::String (juce::roundToInt (w)) + "%", worst.toFloat(),
                                    theme::displayFont (11.0f), 0.06f, juce::Justification::centredRight);

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

            // The verdict line: blown blocks ARE the audible drops.
            {
                auto foot = r.removeFromTop (24);
                const auto n = amp.overruns.load();
                g.setColour (n > 0 ? theme::orange : theme::txDim);
                theme::drawTracked (g, "OVERRUNS  " + juce::String ((int) n)
                                        + juce::String (n > 0 ? "  = AUDIBLE DROPS" : ""),
                                    foot.toFloat(), theme::displayFont (11.0f), 0.08f,
                                    juce::Justification::centredLeft);
            }

            // The buttons: RESET starts a fresh measurement, COPY hands me the numbers as text.
            const auto pill = [&g] (juce::Rectangle<int> b, const juce::String& text, bool lit)
            {
                g.setColour (theme::panel);
                g.fillRoundedRectangle (b.toFloat(), b.getHeight() * 0.5f);
                g.setColour (lit ? theme::orange : theme::hair2);
                g.drawRoundedRectangle (b.toFloat().reduced (0.5f), b.getHeight() * 0.5f, 1.0f);
                g.setColour (lit ? theme::orange : theme::txDim);
                theme::drawTracked (g, text, b.toFloat(), theme::displayFont (11.0f), 0.1f,
                                    juce::Justification::centred);
            };

            pill (resetArea(), "RESET", false);
            pill (copyArea(), copied > 0 ? "COPIED" : "COPY", copied > 0);
        }

        juce::Rectangle<int> resetArea() const { return { 12, getHeight() - 28, 70, 20 }; }
        juce::Rectangle<int> copyArea()  const { return { 90, getHeight() - 28, 70, 20 }; }

        void timerCallback() override
        {
            if (copied > 0)
                --copied;
            repaint();
        }

        enum { rowH = 19, graphH = 64 };
        AmpProcessor& amp;
        int copied = 0;
    };

    auto panel = std::make_unique<Panel> (amp);
    juce::CallOutBox::launchAsynchronously (std::move (panel),
                                            loadBadge.getScreenBounds(), nullptr);
}

} // namespace orbitamp
