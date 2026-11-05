// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "CabWindow.h"

#include "Theme.h"

#include "../core/CabSynth.h"

#include <juce_audio_formats/juce_audio_formats.h>

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
                    su.aOn, su.bOn, su.back } };
}

namespace
{
    AmpProcessor::CabSetup engineOf (const CabPicker::Setup& s)
    {
        return { toEngine (s.a), toEngine (s.b), s.blend, s.align, s.invert, s.aOn, s.bOn, s.back };
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
    // The back said only where the player turned it from the box's own: "4x10 PORTED".
    const auto box = at < 0 ? synth.cab
                   : CabPicker::wordOf (at) + (su.back >= 0 && su.back != CabPicker::ownBack (at)
                                                   ? " " + juce::String (CabPicker::backWords[su.back]) : juce::String());
    return box + juce::String::fromUTF8 (" \xc2\xb7 ")
         + (su.aOn ? micOf (su.a) : juce::String()) + (su.aOn && su.bOn ? "+" : "") + (su.bOn ? micOf (su.b) : juce::String());
}

CabWindow::CabWindow (AmpProcessor& a, Kept k)
    : amp (a), kept (std::move (k))
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
    picker.onDownload = [this] { download (amp); };

    addAndMakeVisible (picker);
    setSize (CabPicker::panelW, CabPicker::panelH);
    timerCallback();        // what plays, from the first frame
    startTimerHz (10);
}

void CabWindow::resized()
{
    picker.setBounds (getLocalBounds());
}

std::vector<float> CabWindow::responseOf (const juce::MemoryBlock& bytes)
{
    juce::AudioFormatManager formats;   // a few objects, at most ten times a second
    formats.registerBasicFormats();
    const std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (std::make_unique<juce::MemoryInputStream> (bytes, false)));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0)
        return {};

    const int length = (int) juce::jmin<juce::int64> (reader->lengthInSamples, 1 << 16);
    juce::AudioBuffer<float> ir (1, length);
    reader->read (&ir, 0, length, 0, true, false);

    size_t n = 1 << 16;   // padded far past the IR, so a sixth of an octave at 30 Hz still spans bins enough
    while (n < (size_t) length)
        n <<= 1;
    std::vector<core::cabsynth::cd> x (n, 0.0);
    for (int i = 0; i < length; ++i)
        x[(size_t) i] = ir.getSample (0, i);
    core::cabsynth::fft (x, false);

    const double binHz = reader->sampleRate / (double) n, half = std::pow (2.0, 1.0 / 12.0);
    std::vector<float> db ((size_t) CabPicker::responsePoints);
    double middle = 0;
    int counted = 0;
    for (int i = 0; i < CabPicker::responsePoints; ++i)
    {
        const double hz = CabPicker::responseHz (i);
        const auto lo = (size_t) juce::jlimit (1.0, (double) (n / 2 - 1), std::floor (hz / half / binHz));
        const auto hi = (size_t) juce::jlimit ((double) lo, (double) (n / 2 - 1), std::ceil (hz * half / binHz));
        double power = 0;
        for (size_t k = lo; k <= hi; ++k)
            power += std::norm (x[k]);
        db[(size_t) i] = (float) (10.0 * std::log10 (power / (double) (hi - lo + 1) + 1.0e-20));
        if (hz >= 100.0 && hz <= 5000.0)
        {
            middle += db[(size_t) i];
            ++counted;
        }
    }
    for (auto& v : db)
        v -= (float) (middle / juce::jmax (1, counted));
    return db;
}

void CabWindow::timerCallback()
{
    // The response: the IR the cabinet last loaded — live under a moving hand, over the kept one's ghost; a kept one over
    // the one kept before it.
    if (const auto heard = amp.cabHeard(); heard.serial != heardSerial)
    {
        heardSerial = heard.serial;
        auto now = heard.bytes != nullptr ? responseOf (*heard.bytes) : std::vector<float>();
        auto before = keptCurve;
        if (heard.kept)
            keptCurve = now;
        picker.showResponse (std::move (now), std::move (before));
    }

    const auto choice = amp.cabChoice();
    const auto now = choice.identity() + "|" + choice.from;
    if (now == showing)
        return;
    showing = now;
    const auto [cab, setup] = drawnOf (AmpProcessor::cabSynthOf (choice));
    picker.show (cab, setup);
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
