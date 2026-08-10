#pragma once

#include "BlockFrame.h"
#include "CabinetBlock.h"
#include "CapturedBlockPanel.h"
#include "ChainLink.h"
#include "EqBlock.h"
#include "GateBlock.h"
#include "PowerAmpBlock.h"
#include "ReverbBlock.h"
#include "TunerBlock.h"

#include <array>
#include <optional>

namespace orbitamp
{

class AmpProcessor;

/** The faceplate: the device shell, the in/out gutters and the block rows — or, zoomed, ONE link
    of the chain across the whole panel.

    The overview's proportions are the design's golden-ratio lane — boost and reverb take one unit
    each and the preamp takes phi, because the captured voicing is the instrument and the rest are
    layers on it. The zoom is a magnifying glass, not a mode: the overview comes back the moment
    the open link's thumb is clicked again. */
class FaceplateView : public juce::Component
{
public:
    explicit FaceplateView (AmpProcessor&);

    /** The loaded device changed — the captured blocks rebuild their faces from their packs. */
    void deviceChanged() { boost.deviceChanged(); preamp.deviceChanged(); }

    /** Opens one link across the whole panel, or none for the overview. */
    void setZoom (std::optional<ChainLink>);
    std::optional<ChainLink> zoom() const noexcept { return zoomed; }

    void resized() override;

    // The faceplate's design size, at 100%. The editor scales from these and never re-lays out.
    static constexpr int designWidth  = 880;

    // Row 1 is sized by the preamp: voicing row, gain knob, and the tone stack under it. Row 2 —
    // power amp and cabinet — is half of it.
    static constexpr int row1H  = 318;
    static constexpr int row2H  = row1H / 2;
    static constexpr int rowGap = 12;
    static constexpr int designHeight = row1H + rowGap + row2H + 32;   // + lanePadY top and bottom

private:
    // Lane metrics. The in/out gutter columns are gone — they were empty wells reserved for
    // metering that never landed, and the lane wears their width better than they did.
    static constexpr int   lanePadX   = 10;
    static constexpr int   lanePadY   = 16;
    static constexpr int   colGap     = 14;
    static constexpr float phi        = 1.62f; // the preamp column's weight — the wider anchor

    /** Every link's face, indexable in chain order — what setZoom opens by. */
    std::array<juce::Component*, (size_t) numChainLinks> linkFaces();

    TunerBlock         tuner;
    GateBlock          gate;
    EqBlock            eq1;
    EqBlock            eq2;
    CapturedBlockPanel boost;
    CapturedBlockPanel preamp;
    ReverbBlock        reverb;
    PowerAmpBlock      power;
    CabinetBlock       cabinet;

    std::optional<ChainLink> zoomed;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FaceplateView)
};

} // namespace orbitamp
