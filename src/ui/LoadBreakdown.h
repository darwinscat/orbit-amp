// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "Theme.h"

#include "../Parameters.h"
#include "../PluginProcessor.h"
#include "../Product.h"

namespace orbitamp
{

/** THE LOAD, ITEMISED — the window behind the load figure, for whichever face shows one: OrbitAmp's
    footer reads it DSP, the cockpit reads it CPU, and a click on either opens this. orbitcab's
    grammar: rows of stage, bar and number — the whole chain's cost, with a strip chart of the last
    seconds over them, the count of blown blocks under them, and two buttons: RESET starts a fresh
    measurement, COPY hands the numbers over as text. */
struct LoadBreakdown final : public juce::Component,
                             private juce::Timer
{
    /** WHAT A FACE MAY MAKE ITS OWN. Its words for the stages — an empty answer keeps the chain's
        own name — and the three weights the text is set in. Left alone, it is OrbitAmp's. */
    std::function<juce::String (int stage, bool brief)> nameOf;
    juce::Colour text { theme::tx }, dim { theme::txDim }, faint { theme::txFaint };

    juce::String stageName (int stage, bool brief) const
    {
        if (nameOf)
            if (const auto own = nameOf (stage, brief); own.isNotEmpty())
                return own;

        return brief ? params::stageNames[stage].brief : params::stageNames[stage].full;
    }

    explicit LoadBreakdown (AmpProcessor& p) : amp (p)
    {
        // Sized for EVERY stage, not for the ones standing when it opened: presence can be
        // switched while this callout is up, and a panel that grew a row would spill into its
        // own buttons. The unused rows cost a little air and nothing else.
        setSize (360, 26 + graphH + 6 + AmpProcessor::numStages * rowH + 34 + 34);
        startTimerHz (15);
    }

    /** THE LIST IS THE RIG. A link out of the rig has no cost entry, because it has no cost:
        it is not in the chain at all. A link STANDING BY keeps its row and keeps its real
        number — a bypassed insert costs what it costs in any DAW — but reads faint, which is
        the useful truth: the bypass bought you nothing, and taking it out of the rig will. */
    bool stageInRig (int stage) const
    {
        const int row = params::rowForStage ((params::Stage) stage);

        if (row >= params::numChainRows)
            return true;                     // TOTAL belongs to nobody and is always there

        const auto* p = amp.apvts.getParameter (params::chainLinks[(size_t) row].presentParam);
        return p == nullptr || p->getValue() > 0.5f;
    }

    bool stageWorking (int stage) const
    {
        const int row = params::rowForStage ((params::Stage) stage);

        if (row >= params::numChainRows)
            return true;

        const auto* p = amp.apvts.getParameter (params::chainLinks[(size_t) row].onParam);
        return p == nullptr || p->getValue() > 0.5f;
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
        juce::String t;
        t << amp.product().name << " DSP report  |  " << juce::String (amp.currentSampleRate() / 1000.0, 1)
          << " kHz  |  block " << amp.getBlockSize()
          << "  |  " << params::stereoModes[juce::jlimit (0, params::stereoModes.size() - 1,
                                                          juce::roundToInt (amp.apvts.getRawParameterValue (params::stereoMode)->load()))].toUpperCase()
          << "\n";
        t << juce::String::formatted ("%-8s %8s %8s\n", "STAGE", "MEAN", "WORST");

        for (int i = 0; i < AmpProcessor::numStages; ++i)
        {
            if (! stageInRig (i))
                continue;

            t << juce::String::formatted ("%-8s %7.1f%% %7.0f%%\n", stageName (i, true).toRawUTF8(),
                                          amp.stageLoad[i].load(), amp.stageWorst[i].load());
        }

        t << "OVERRUNS " << (int) amp.overruns.load() << "\n";
        return t;
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().reduced (12, 4);

        {
            auto head = r.removeFromTop (24);
            g.setColour (text);
            theme::drawTracked (g, "DSP LOAD", head.toFloat(),
                                theme::displayFont (13.0f), 0.1f, juce::Justification::centredLeft);
            g.setColour (dim);
            theme::drawTracked (g, "MEAN / WORST", head.toFloat(),
                                theme::displayFont (13.0f), 0.08f, juce::Justification::centredRight);
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
            if (! stageInRig (i))
                continue;

            auto row = r.removeFromTop (rowH);
            const float v = amp.stageLoad[i].load();
            const bool total   = i == AmpProcessor::stTotal;
            const bool working = stageWorking (i);

            g.setColour (total ? text : working ? dim : faint);
            // Wide enough for a name of nine letters, which at 13 px with this tracking is a shade
            // under ninety — the chain's own are shorter, and a face may bring its own. `drawTracked`
            // does not clip, and the bar is painted after the label, so a column measured for the
            // SECOND longest name reads as a bar that has eaten the end of a word.
            theme::drawTracked (g, stageName (i, false), row.removeFromLeft (96).toFloat(),
                                theme::displayFont (13.0f), 0.08f, juce::Justification::centredLeft);

            const float w = amp.stageWorst[i].load();

            auto worst = row.removeFromRight (58);
            g.setColour (w > 100.0f ? theme::orange : faint);
            theme::drawTracked (g, juce::String (juce::roundToInt (w)) + "%", worst.toFloat(),
                                theme::displayFont (13.0f), 0.06f, juce::Justification::centredRight);

            auto num = row.removeFromRight (52);
            g.setColour (v > 50.0f ? theme::orange
                       : total ? text : working ? dim : faint);
            theme::drawTracked (g, juce::String (v, 1) + "%", num.toFloat(),
                                theme::displayFont (13.0f), 0.06f, juce::Justification::centredRight);

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
            auto foot = r.removeFromTop (26);
            const auto n = amp.overruns.load();
            g.setColour (n > 0 ? theme::orange : dim);
            theme::drawTracked (g, "OVERRUNS  " + juce::String ((int) n)
                                    + juce::String (n > 0 ? "  = AUDIBLE DROPS" : ""),
                                foot.toFloat(), theme::displayFont (13.0f), 0.08f,
                                juce::Justification::centredLeft);
        }

        // The buttons: RESET starts a fresh measurement, COPY hands me the numbers as text.
        const auto pill = [&g, this] (juce::Rectangle<int> b, const juce::String& word, bool lit)
        {
            g.setColour (theme::panel);
            g.fillRoundedRectangle (b.toFloat(), b.getHeight() * 0.5f);
            g.setColour (lit ? theme::orange : theme::hair2);
            g.drawRoundedRectangle (b.toFloat().reduced (0.5f), b.getHeight() * 0.5f, 1.0f);
            g.setColour (lit ? theme::orange : dim);
            theme::drawTracked (g, word, b.toFloat(), theme::displayFont (13.0f), 0.1f,
                                juce::Justification::centred);
        };

        pill (resetArea(), "RESET", false);
        pill (copyArea(), copied > 0 ? "COPIED" : "COPY", copied > 0);
    }

    juce::Rectangle<int> resetArea() const { return { 12, getHeight() - 32, 78, 24 }; }
    juce::Rectangle<int> copyArea()  const { return { 98, getHeight() - 32, 78, 24 }; }

    void timerCallback() override
    {
        if (copied > 0)
            --copied;
        repaint();
    }

    enum { rowH = 22, graphH = 64 };   // rowH is 13 px of type and the air around it
    AmpProcessor& amp;
    int copied = 0;
};

/** Opens it as a callout pointing at `anchor`, which is in screen coordinates — the words that were
    clicked. */
inline void openLoadBreakdown (AmpProcessor& amp, juce::Rectangle<int> anchor)
{
    juce::CallOutBox::launchAsynchronously (std::make_unique<LoadBreakdown> (amp), anchor, nullptr);
}

} // namespace orbitamp
