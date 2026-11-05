// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "CabWindow.h"

#include "Theme.h"

namespace orbitamp
{

namespace
{
    AmpProcessor::CabSpot toEngine (const CabPicker::Spot& s) { return { s.x, s.y, s.distance, s.horn, s.mic, s.angle, s.hornDb }; }
    CabPicker::Spot toPicture (const AmpProcessor::CabSpot& s) { return { s.x, s.y, s.distance, s.horn, s.mic, s.angle, s.hornDb }; }
}

std::pair<int, CabPicker::Setup> CabWindow::drawnOf (const AmpProcessor::CabSynthChoice& synth)
{
    const auto& su = synth.setup;
    const int cab = synth.valid ? CabPicker::indexOf (synth.cab) : -1;
    // B where it stood, on or off, once it has stood anywhere — the mic it was given says so; else where B waits.
    return { cab, { toPicture (su.a), su.b.mic.isNotEmpty() ? toPicture (su.b) : CabPicker::Setup{}.b, su.blend, su.align, su.invert,
                    su.aOn, su.bOn } };
}

namespace
{
    AmpProcessor::CabSetup engineOf (const CabPicker::Setup& s)
    {
        return { toEngine (s.a), toEngine (s.b), s.blend, s.align, s.invert, s.aOn, s.bOn };
    }
}

void CabWindow::request (AmpProcessor& processor, int cab, const CabPicker::Setup& setup, bool live)
{
    processor.requestCabSynth (CabPicker::cabs()[(size_t) cab].name, engineOf (setup), live);
}

juce::String CabWindow::labelOf (const AmpProcessor::CabSynthChoice& synth)
{
    if (! synth.valid)
        return {};
    const int at = CabPicker::indexOf (synth.cab);
    const auto micOf = [at] (const AmpProcessor::CabSpot& s)
    { return (s.mic.isNotEmpty() ? s.mic : at >= 0 ? CabPicker::cabs()[(size_t) at].mic : juce::String()).toUpperCase(); };
    const auto& su = synth.setup;
    return (at >= 0 ? CabPicker::wordOf (at) : synth.cab) + juce::String::fromUTF8 (" \xc2\xb7 ")
         + (su.aOn ? micOf (su.a) : juce::String()) + (su.aOn && su.bOn ? "+" : "") + (su.bOn ? micOf (su.b) : juce::String());
}

CabWindow::CabWindow (AmpProcessor& a, CabPicker::Range range, Kept k)
    : amp (a), picker (range), kept (std::move (k))
{
    // The bass's own cabinets, rendered with the mics where the hand put them.
    picker.onSetup = [this] (int cab, CabPicker::Setup setup)
    {
        request (amp, cab, setup, false);
        if (kept != nullptr)
            kept (AmpProcessor::cabSynthFrom (CabPicker::cabs()[(size_t) cab].name, engineOf (setup)));
    };
    picker.onMove  = [this] (int cab, CabPicker::Setup setup) { request (amp, cab, setup, true); };
    picker.onClose = [this] { if (auto* box = findParentComponentOfClass<juce::CallOutBox>()) box->dismiss(); };

    addAndMakeVisible (picker);
    setSize (CabPicker::panelW, CabPicker::panelH);
    timerCallback();        // what plays, from the first frame
    startTimerHz (10);
}

void CabWindow::resized()
{
    picker.setBounds (getLocalBounds());
}

void CabWindow::timerCallback()
{
    const auto choice = amp.cabChoice();
    const auto now = choice.identity() + "|" + choice.from;
    if (now == showing)
        return;
    showing = now;
    const auto [cab, setup] = drawnOf (AmpProcessor::cabSynthOf (choice));
    picker.show (cab, setup);
}

void CabWindow::open (AmpProcessor& amp, juce::Component& anchor, CabPicker::Range range, Kept kept)
{
    // Over the face above what opened it, its arrow at that one's top, inside the window where it fits: what opened it
    // stays in sight, answering the hand.
    const auto at = anchor.getScreenBounds();
    auto& box = juce::CallOutBox::launchAsynchronously (std::unique_ptr<CabWindow> (new CabWindow (amp, range, std::move (kept))), at, nullptr);

    // Ours, not JUCE's grey-green: the face's panel under it, the CAB's orange line round it and its arrow.
    struct Look final : juce::LookAndFeel_V4
    {
        void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics& g, const juce::Path& path, juce::Image&) override
        {
            g.setColour (theme::panel);
            g.fillPath (path);
            g.setColour (theme::orange);
            g.strokePath (path, juce::PathStrokeType (1.2f));
        }
        float getCallOutBoxCornerSize (const juce::CallOutBox&) override { return 6.0f; }
    };
    static Look look;
    box.setLookAndFeel (&look);
    if (auto* top = anchor.getTopLevelComponent(); top != nullptr && box.getWidth() < top->getWidth() && box.getHeight() < top->getHeight())
        box.updatePosition (at, top->getScreenBounds());
}

} // namespace orbitamp
