#include "CabinetBlock.h"

#include "../PluginProcessor.h"

#include <cmath>

namespace orbitamp
{

namespace
{
    constexpr int kSpecBins = 72;
    constexpr float kSpecFloorDb = -54.0f;
}

CabinetBlock::CabinetBlock (AmpProcessor& processor)
    : BlockFrame ("Cab IR", BlockFrame::Kind::captured), amp (processor),
      fft (AmpProcessor::eqSpectrumOrder)
{
    (void) 0;
    frame.resize ((size_t) felitronics::analysis::RollingSpectrumTap::kMaxSize);
    work.resize ((size_t) (2 << AmpProcessor::eqSpectrumOrder));
    startTimerHz (30);
    attachPower (*amp.apvts.getParameter (params::cabOn));

    // The IR's name on the border beside the block's, set like a name and in the block's colour.
    {
        juce::Array<VoicingSelector::Entry> entries;
        for (const auto& name : params::cabIrNames)
            entries.add ({ name, 0, false });
        ir.setEntries (std::move (entries));
    }
    ir.fontHeight = 16.0f;
    ir.tracking   = 0.15f;
    ir.boxed      = false;
    ir.tint       = theme::orange;
    addAndMakeVisible (ir);

    irAtt = std::make_unique<juce::ParameterAttachment> (
        *amp.apvts.getParameter (params::cabIr),
        [this] (float v)
        {
            const int i = juce::jlimit (0, params::cabIrNames.size() - 1, juce::roundToInt (v));
            ir.setSelection (i);
            loadWave (i);
            resized();   // the name on the border is as wide as the name
        });

    ir.onPick = [this] (int i) { irAtt->setValueAsCompleteGesture ((float) i); };

    // The picture: the IR, the cuts' curve and the trim's handle drawn on it, in the block's colour.
    wave.setAccent (theme::orange);
    wave.setTrimInteractive (true);
    wave.setEqVisible (true);
    addAndMakeVisible (wave);

    // A handle dragged on the picture writes its parameter; the parameter's echo redraws it.
    wave.onHpfChanged  = [this] (bool, float hz) { hpfHzAtt->setValueAsCompleteGesture (hz); };
    wave.onLpfChanged  = [this] (bool, float hz) { lpfHzAtt->setValueAsCompleteGesture (hz); };
    wave.onTrimChanged = [this] (float f)        { trimAtt->setValueAsCompleteGesture (f); };

    const auto follow = [this] (const char* id) -> std::unique_ptr<juce::ParameterAttachment>
    {
        return std::make_unique<juce::ParameterAttachment> (*amp.apvts.getParameter (id),
                                                            [this] (float) { pushToWave(); });
    };

    hpfHzAtt = follow (params::cabHpfHz);
    lpfHzAtt = follow (params::cabLpfHz);
    trimAtt  = follow (params::cabTrim);

    // The four switches, each the truth of its parameter and nothing of its own.
    const char* ids[]    = { params::cabHpfOn, params::cabLpfOn, params::cabTrimOn, params::cabPhase };
    const char* names[]  = { "HPF", "LPF", "TRIM", "\xc3\x98" };

    for (size_t i = 0; i < switches.size(); ++i)
    {
        auto& s = switches[i];
        s.sw.accent = theme::orange;
        addAndMakeVisible (s.sw);

        s.label.setText (juce::String::fromUTF8 (names[i]), juce::dontSendNotification);
        s.label.setFont (theme::displayFont (12.0f));
        s.label.setColour (juce::Label::textColourId, theme::txDim);
        s.label.setJustificationType (juce::Justification::centredLeft);
        s.label.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (s.label);

        s.att = std::make_unique<juce::ParameterAttachment> (
            *amp.apvts.getParameter (ids[i]),
            [this, i] (float v)
            {
                switches[i].sw.setOn (v > 0.5f, false);
                pushToWave();
            });

        s.sw.onChange = [this, i] (bool on) { switches[i].att->setValueAsCompleteGesture (on ? 1.0f : 0.0f); };
        s.att->sendInitialUpdate();
    }

    irAtt->sendInitialUpdate();
    pushToWave();
}

CabinetBlock::~CabinetBlock() = default;

void CabinetBlock::loadWave (int index)
{
    const auto& bytes = AmpProcessor::cabIrBytes (index);
    wave.setFromMemory (bytes.data, (size_t) bytes.size);
    pushToWave();
}

void CabinetBlock::pushToWave()
{
    // The PARAMETERS, not the atomics: an attachment callback runs before the atomic is written,
    // and reading it there leaves the picture exactly one change behind for good.
    const auto plain = [this] (const char* id)
    {
        auto* p = amp.apvts.getParameter (id);
        return p->convertFrom0to1 (p->getValue());
    };

    wave.setFilters (plain (params::cabHpfOn) > 0.5f, plain (params::cabHpfHz), params::cabHpfMinHz, params::cabHpfMaxHz,
                     plain (params::cabLpfOn) > 0.5f, plain (params::cabLpfHz), params::cabLpfMinHz, params::cabLpfMaxHz);
    wave.setTrimEnabled (plain (params::cabTrimOn) > 0.5f);
    wave.setTrimFraction (plain (params::cabTrim));
}

void CabinetBlock::layOutContent (juce::Rectangle<int> area)
{
    // The IR's name on the border, sized to itself and centred in the run.
    {
        const auto slot = borderSlotArea();
        ir.setBounds (slot.withSizeKeepingCentre (juce::jmin (slot.getWidth(), ir.idealWidth()), slot.getHeight()));
        borderSlotUsed = ir.getBounds();
    }

    // The switches along the bottom, four equal cells: a switch and its word.
    auto row = area.removeFromBottom (switchRow);
    area.removeFromBottom (gap);

    const int cellW = row.getWidth() / (int) switches.size();
    for (auto& s : switches)
    {
        auto cell = row.removeFromLeft (cellW);
        s.sw.setBounds (cell.removeFromLeft (30).withSizeKeepingCentre (30, 16));
        cell.removeFromLeft (6);
        s.label.setBounds (cell);
    }

    wave.setBounds (area);
}

void CabinetBlock::paintContent (juce::Graphics&) {}

/** The faint spectra behind the impulse: what walks into the IR and what walks out. Both bins are
    held against ONE reference — the larger frame's peak, decayed slowly — so the post reads as the
    pre with the cabinet's opinion, not as two pictures each full of itself. */
void CabinetBlock::timerCallback()
{
    if (! isBlockOn() || ! wave.isShowing())
        return;

    float ref = specRef * 0.94f;   // the shared peak decays, so a quiet passage regrows the picture

    const bool a = binsOf (0, preBins, ref);
    const bool b = binsOf (1, postBins, ref);

    if (! a && ! b)
        return;

    specRef = ref;

    const auto scale = [&] (std::vector<float>& bins)
    {
        for (auto& v : bins)
        {
            const float db = juce::Decibels::gainToDecibels (v / juce::jmax (1.0e-9f, ref), kSpecFloorDb);
            v = juce::jlimit (0.0f, 1.0f, (db - kSpecFloorDb) / -kSpecFloorDb);
        }
    };

    scale (preBins);
    scale (postBins);
    wave.setSpectrum (preBins, postBins);
}

bool CabinetBlock::binsOf (int tap, std::vector<float>& out, float& ref)
{
    auto& t = tap == 0 ? amp.cabSpectrumTap[0] : amp.cabSpectrumTap[1];

    int order = 0;
    if (! t.tryPull (frame.data(), order) || order != AmpProcessor::eqSpectrumOrder)
        return false;

    const int n = 1 << order;
    std::fill (work.begin(), work.end(), 0.0f);

    for (int i = 0; i < n; ++i)
    {
        const float w = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) (n - 1));
        work[(size_t) i] = frame[(size_t) i] * w;
    }

    fft.performRealOnlyForwardTransform (work.data(), true);

    // ~72 log-frequency bins, 20 Hz .. 20 kHz, each the loudest FFT bin in its span.
    out.assign ((size_t) kSpecBins, 0.0f);
    const double sr = juce::jmax (8000.0, amp.currentSampleRate());

    for (int bIdx = 0; bIdx < kSpecBins; ++bIdx)
    {
        const double f0 = 20.0 * std::pow (1000.0, (double) bIdx / (double) kSpecBins);
        const double f1 = 20.0 * std::pow (1000.0, (double) (bIdx + 1) / (double) kSpecBins);
        int k0 = (int) std::floor (f0 * n / sr);
        int k1 = (int) std::ceil  (f1 * n / sr);
        k0 = juce::jlimit (1, n / 2 - 1, k0);
        k1 = juce::jlimit (k0 + 1, n / 2, k1);

        float mx = 0.0f;
        for (int k = k0; k < k1; ++k)
        {
            const float re = work[(size_t) (2 * k)], im = work[(size_t) (2 * k + 1)];
            mx = juce::jmax (mx, std::sqrt (re * re + im * im));
        }
        out[(size_t) bIdx] = mx;
        ref = juce::jmax (ref, mx);
    }

    return true;
}

} // namespace orbitamp
