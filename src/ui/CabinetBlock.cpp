#include "CabinetBlock.h"

#include "../PluginProcessor.h"

namespace orbitamp
{

CabinetBlock::CabinetBlock (AmpProcessor& processor)
    : BlockFrame ("Cab IR", BlockFrame::Kind::captured), amp (processor)
{
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

} // namespace orbitamp
