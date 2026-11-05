// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "CabWindow.h"

#include "Theme.h"

#include <panel/Response.h>
#include <synth/CabSynth.h>

#include <BinaryData.h>

#include <juce_audio_formats/juce_audio_formats.h>

namespace orbitamp
{

std::pair<int, CabPicker::Setup> CabWindow::drawnOf (const AmpProcessor::CabSynthChoice& synth)
{
    if (! synth.valid)
        return { -1, {} };
    return CabPicker::setupOf (synth.state);
}

cabir::State CabWindow::stateOf (int cab, const CabPicker::Setup& setup)
{
    return CabPicker::stateOf (cab, setup);
}

void CabWindow::request (AmpProcessor& processor, int cab, const CabPicker::Setup& setup, bool live)
{
    processor.requestCabSynth (stateOf (cab, setup), live);
}

juce::String CabWindow::labelOf (const AmpProcessor::CabSynthChoice& synth)
{
    return synth.valid ? CabPicker::labelOf (synth.state) : juce::String();
}

CabWindow::CabWindow (AmpProcessor& a, Kept k)
    : amp (a), kept (std::move (k))
{
    useOurLook();
    // The bass's own cabinets, rendered with the mics where the hand put them.
    picker.onSetup = [this] (int cab, CabPicker::Setup setup)
    {
        const bool bTakenUp = ! bHeard && setup.bOn;
        bHeard = setup.bOn;
        request (amp, cab, setup, false);
        if (kept != nullptr)
            kept (AmpProcessor::cabSynthFrom (stateOf (cab, setup)));
        if (bTakenUp)
            placeB (cab, setup);
    };
    picker.onMove  = [this] (int cab, CabPicker::Setup setup) { request (amp, cab, setup, true); };
    picker.onClose = [this] { if (auto* box = findParentComponentOfClass<juce::CallOutBox>()) box->dismiss(); };
    picker.onDownload = [this] { download (amp); };
    picker.onFind     = [this] (int cab, CabPicker::Setup setup) { placeB (cab, setup, true); };   // FIND looks harder

    addAndMakeVisible (picker);
    setSize (CabPicker::panelW, CabPicker::panelH);
    timerCallback();        // what plays, from the first frame
    startTimerHz (30);      // ...and the response as fast as a moving hand's renders land
}

void CabWindow::resized()
{
    picker.setBounds (getLocalBounds());
}

void CabWindow::useOurLook()
{
    auto& look = cabir::look();
    look.orange  = theme::orange;
    look.violet  = theme::violet;
    look.lilac   = theme::lilac;
    look.panel   = theme::panel;
    look.panel2  = theme::panel2;
    look.bezel   = theme::bezel;
    look.hair    = theme::hair;
    look.hair2   = theme::hair2;
    look.tx      = theme::tx;
    look.txDim   = theme::txDim;
    look.txFaint = theme::txFaint;
    look.font         = [] (float height) { return theme::displayFont (height); };
    look.drawTracked  = theme::drawTracked;
    look.trackedWidth = theme::trackedWidth;
}

void CabWindow::timerCallback()
{
    // The response: the IR the cabinet last loaded — live under a moving hand, over the kept one's ghost; a kept one over
    // the one kept before it.
    if (const auto heard = amp.cabHeard(); heard.serial != heardSerial)
    {
        heardSerial = heard.serial;
        auto now = heard.bytes != nullptr ? cabir::responseOf (*heard.bytes) : std::vector<float>();
        auto before = keptCurve;
        if (heard.kept)
            keptCurve = now;
        picker.showResponse (std::move (now), std::move (before),
                             heard.parts != nullptr ? cabir::combOf (heard.parts->a, heard.parts->b) : std::vector<float>());
    }

    const auto choice = amp.cabChoice();
    const auto now = choice.identity() + "|" + choice.from;
    if (now == showing)
        return;
    showing = now;
    const auto [cab, setup] = drawnOf (AmpProcessor::cabSynthOf (choice));
    bHeard = setup.bOn;
    picker.show (cab, setup);
}

void CabWindow::placeB (int cab, const CabPicker::Setup& setup, bool thorough)
{
    // B just taken up, or FIND: where it combs least with A, found off the message thread (a second or so), each place it
    // judges drawn on the speaker as it goes; B moves there unless the hand moved it first — what plays still has B where
    // it was put — or the window went.
    static juce::ThreadPool searcher { 1 };
    const auto state = stateOf (cab, setup);
    juce::Component::SafePointer<CabWindow> safe (this);
    searcher.addJob ([safe, state, cab, setup, thorough]
    {
        static const auto table = cabir::synth::parseTable (std::string (BinaryData::cabinets_txt, (size_t) BinaryData::cabinets_txtSize));
        const auto place = cabir::synth::quietPlaceForB (table, state, (size_t) 1 << 13, [safe] (const cabir::synth::Place& p, double score)
        {
            juce::MessageManager::callAsync ([safe, p, score]
            {
                if (safe != nullptr)
                    safe->picker.showSearch ((float) p.x, (float) p.y, (float) score);
            });
        }, thorough);
        juce::MessageManager::callAsync ([safe, state, cab, setup, place]
        {
            if (safe == nullptr)
                return;
            safe->picker.endSearch();
            const auto now = AmpProcessor::cabSynthOf (safe->amp.cabChoice());
            if (! now.valid || ! (now.state.b == state.b) || now.state.box != state.box)
                return;
            auto moved = setup;
            moved.b.x = (float) place.x;
            moved.b.y = (float) place.y;
            moved.b.distance = (float) place.distance;
            moved.shiftMs = (float) place.shiftMs;
            safe->picker.show (cab, moved);
            safe->picker.onSetup (cab, moved);
        });
    });
}

namespace
{
    // The save dialog outlives the window: a native dialog takes the focus, and the call-out goes when it loses it.
    std::unique_ptr<juce::FileChooser> saving;
}

void CabWindow::download (AmpProcessor& amp)
{
    const auto choice = amp.cabChoice();
    if (! choice.isUser())
        return;

    // The bytes the cabinet plays — a rendered one is a WAV already — under the name its line reads, in Downloads.
    auto bytes = std::make_shared<juce::MemoryBlock> (*choice.bytes);
    auto name = labelOf (AmpProcessor::cabSynthOf (choice)).replace (juce::String::fromUTF8 (" \xc2\xb7 "), " ");
    if (name.isEmpty())
        name = choice.name;
    auto folder = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Downloads");
    if (! folder.isDirectory())
        folder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

    saving = std::make_unique<juce::FileChooser> ("Download IR", folder.getChildFile (juce::File::createLegalFileName (name) + ".wav"), "*.wav");
    saving->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                             | juce::FileBrowserComponent::warnAboutOverwriting,
                         [bytes] (const juce::FileChooser& chooser)
                         {
                             if (const auto file = chooser.getResult(); file != juce::File())
                                 file.withFileExtension ("wav").replaceWithData (bytes->getData(), bytes->getSize());
                             juce::MessageManager::callAsync ([] { saving.reset(); });   // not from inside its own call
                         });
}

void CabWindow::open (AmpProcessor& amp, juce::Component& anchor, Kept kept)
{
    // Over the face above what opened it, its arrow at that one's top, inside the window where it fits: what opened it
    // stays in sight, answering the hand.
    const auto at = anchor.getScreenBounds();
    auto& box = juce::CallOutBox::launchAsynchronously (std::unique_ptr<CabWindow> (new CabWindow (amp, std::move (kept))), at, nullptr);

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
