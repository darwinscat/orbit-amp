// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "Footer.h"

#include "../Parameters.h"
#include "../PluginProcessor.h"

namespace orbitamp
{

namespace chrome = felitronics::appkit::chrome;

Footer::Footer (AmpProcessor& processor)
    : amp (processor)
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

    if (text != rateText || load != loadText)
    {
        rateText = text;
        loadText = load;
        repaint();
    }
}

void Footer::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();

    g.setColour (theme::hair);
    g.fillRect (r.removeFromTop (1.0f));

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
                "TOTAL", "TUNER", "GATE", "BOOST", "B-EQ", "PREAMP", "P-EQ", "REVERB", "POWER",
                "CAB", "LIMIT", "OUT",
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
                "TOTAL", "TUNER", "GATE", "BOOST", "BOOST EQ", "PREAMP", "PREAMP EQ", "REVERB", "POWER",
                "CAB", "LIMIT", "OUT",
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
