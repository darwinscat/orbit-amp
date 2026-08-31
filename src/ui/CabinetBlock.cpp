#include "CabinetBlock.h"

#include "../PluginProcessor.h"
#include "Prefs.h"

namespace orbitamp
{

CabinetBlock::CabinetBlock (AmpProcessor& processor)
    : BlockFrame ("Cab IR", BlockFrame::Kind::captured), amp (processor)
{
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

    // The spectra behind the impulse, in the consoles' own hand: what walks into the IR as the
    // ground, what leaves it as the line — the same liquid columns, the same tilt, one renderer.
    wave.paintSpectrumUnder = [this] (juce::Graphics& g, juce::Rectangle<float> r)
    {
        if (! prefs::spectraShown())
            return;

        felitronics::analysis::PlotMap pm;
        pm.width      = r.getWidth();
        pm.height     = r.getHeight();
        pm.plotBottom = r.getHeight();
        pm.freqMin    = 20.0;
        pm.freqMax    = 20000.0;
        pm.specTop    = 0.0;
        pm.specBottom = -90.0;

        const double fs = juce::jmax (8000.0, amp.currentSampleRate());

        const auto draw = [&] (felitronics::analysis::SpectrumPane& pane, juce::Colour tint,
                               float fillTop, float fillBottom, float line)
        {
            juce::Path fill, peak;
            fill.startNewSubPath (r.getX(), r.getBottom());
            bool first = true;

            pane.buildColumns (pm, fs, 4.5, 1000.0,
                               [&] (int, float x, float yFill, float yPeak)
                               {
                                   fill.lineTo (r.getX() + x, r.getY() + yFill);

                                   if (first) { peak.startNewSubPath (r.getX() + x, r.getY() + yPeak); first = false; }
                                   else       peak.lineTo (r.getX() + x, r.getY() + yPeak);
                               });

            fill.lineTo (r.getRight(), r.getBottom());
            fill.closeSubPath();

            g.setGradientFill (juce::ColourGradient (tint.withAlpha (fillTop),
                                                     0.0f, r.getY() + r.getHeight() * 0.30f,
                                                     tint.withAlpha (fillBottom),
                                                     0.0f, r.getBottom(), false));
            g.fillPath (fill);
            g.setColour (tint.withAlpha (line));
            g.strokePath (peak, juce::PathStrokeType (1.0f));
        };

        draw (panes[0], theme::spectrum, 0.14f, 0.02f, 0.30f);   // the door: the consoles' quiet ground
        draw (panes[1], theme::orange,   0.18f, 0.02f, 0.55f);   // the exit: the cabinet's own voice
    };

    addAndMakeVisible (wave);

    // A handle dragged on the picture writes its parameter; the parameter's echo redraws it.
    wave.onHpfChanged  = [this] (bool, float hz) { hpfHzAtt->setValueAsCompleteGesture (hz); };
    wave.onLpfChanged  = [this] (bool, float hz) { lpfHzAtt->setValueAsCompleteGesture (hz); };
    wave.onTrimChanged = [this] (float f)        { trimAtt->setValueAsCompleteGesture (f); };

    // The vertical half of a cut drag: the consoles' slope ladder, one notch per step.
    const auto stepSlope = [this] (const char* id, juce::ParameterAttachment& att, int steps)
    {
        auto* p = amp.apvts.getParameter (id);
        const int next = juce::jlimit (0, params::eqSlopes.size() - 1,
                                       juce::roundToInt (p->convertFrom0to1 (p->getValue())) + steps);
        att.setValueAsCompleteGesture ((float) next);
    };
    wave.onHpfSlopeStep = [this, stepSlope] (int n) { stepSlope (params::cabHpfSlope, *hpfSlopeAtt, n); };
    wave.onLpfSlopeStep = [this, stepSlope] (int n) { stepSlope (params::cabLpfSlope, *lpfSlopeAtt, n); };

    const auto follow = [this] (const char* id) -> std::unique_ptr<juce::ParameterAttachment>
    {
        return std::make_unique<juce::ParameterAttachment> (*amp.apvts.getParameter (id),
                                                            [this] (float) { pushToWave(); });
    };

    hpfHzAtt    = follow (params::cabHpfHz);
    lpfHzAtt    = follow (params::cabLpfHz);
    trimAtt     = follow (params::cabTrim);
    hpfSlopeAtt = follow (params::cabHpfSlope);
    lpfSlopeAtt = follow (params::cabLpfSlope);

    // The four switches, each the truth of its parameter and nothing of its own.
    const char* ids[]    = { params::cabHpfOn, params::cabLpfOn, params::cabTrimOn, params::cabPhase };
    const char* names[]  = { "HPF", "LPF", "TRIM", "\xc3\x98" };

    for (size_t i = 0; i < switches.size(); ++i)
    {
        auto& s = switches[i];
        // Each cut wears its line's colour, the consoles' grammar — orange HPF, violet LPF;
        // the trim and the phase stay the block's own orange.
        s.sw.accent = ids[i] == params::cabLpfOn ? theme::violet : theme::orange;
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

    const auto slopeDb = [&] (const char* id)
    { return params::eqSlopeValues[juce::jlimit (0, (int) std::size (params::eqSlopeValues) - 1,
                                                 juce::roundToInt (plain (id)))]; };

    wave.setFilters (plain (params::cabHpfOn) > 0.5f, plain (params::cabHpfHz), params::cabHpfMinHz, params::cabHpfMaxHz,
                     plain (params::cabLpfOn) > 0.5f, plain (params::cabLpfHz), params::cabLpfMinHz, params::cabLpfMaxHz);
    wave.setSlopes (slopeDb (params::cabHpfSlope), slopeDb (params::cabLpfSlope));
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

/** The consoles' feeding rule, twice: a fresh window into its pane, a starve when the audio
    stopped — then one repaint, and the hook above draws both. */
void CabinetBlock::timerCallback()
{
    if (! isBlockOn() || ! wave.isShowing())
        return;

    bool moved = false;

    for (int i = 0; i < 2; ++i)
    {
        auto& pane = panes[(size_t) i];
        int order = 0;

        if (amp.cabSpectrumTap[(size_t) i].tryPull (pane.frameInput(), order)
            && order == AmpProcessor::eqSpectrumOrder)
            pane.ingest (order);
        else
            pane.starve();

        moved = true;
    }

    if (moved)
        wave.repaint();
}

} // namespace orbitamp
