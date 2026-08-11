#include "CapturedBlockPanel.h"

#include "../PluginProcessor.h"

namespace orbitamp
{

CapturedBlockPanel::CapturedBlockPanel (AmpProcessor& processor, Block& b,
                                        const juce::String& title, const char* blockId)
    : BlockFrame (title, BlockFrame::Kind::captured), amp (processor), block (b), blk (blockId)
{
    addAndMakeVisible (device);
    addAndMakeVisible (gain);

    device.fontHeight = 16.0f;   // the device's NAME is the face's headline, sized like one

    // Five ways of showing the same device, TOGETHER: one scope per way, a checkbox per scope,
    // whichever are down tile the picture zone. The tone curve comes from the block itself —
    // the same data its filters were designed from, resolved on the processor's pump.
    static const char* const vizNames[numViz] = { "SHAPE", "ENVELOPE", "TRANSFER", "TONE", "WAVE" };

    for (int i = 0; i < numViz; ++i)
    {
        scopes[(size_t) i] = std::make_unique<DeviceScope> (
            b.scope, b.ribbon, [this] (double hz) { return block.toneDb (hz); });
        scopes[(size_t) i]->setMode ((DeviceScope::Mode) i);
        scopes[(size_t) i]->setSampleRate (amp.currentSampleRate());
        addChildComponent (*scopes[(size_t) i]);

        auto& c = vizChecks[(size_t) i];
        c.label = vizNames[i];
        c.on = i == 0;   // SHAPE starts down, like the combo used to
        c.onToggle = [this] { resized(); repaint(); };
        addAndMakeVisible (c);
    }

    attachPower (*amp.apvts.getParameter (params::blockOn (blk)));

    // The attachment hears everyone BUT this panel — a restored session, a host automating the
    // parameter. Load the device it names before rebuilding the face, or the face is rebuilt from
    // the pack that is leaving; loading twice costs nothing because selectIfMoved is a no-op when
    // the pump already did it.
    deviceAttachment = std::make_unique<juce::ParameterAttachment> (
        *amp.apvts.getParameter (params::blockDevice (blk)),
        [this] (float v)
        {
            const int i = juce::roundToInt (v);
            device.setSelection (i);
            block.selectIfMoved (i);
            deviceChanged();
        });

    device.onPick = [this] (int i)
    {
        deviceAttachment->setValueAsCompleteGesture ((float) i);
        block.select (i);   // message thread — it reads files
        deviceChanged();
    };

    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        amp.apvts, params::blockGain (blk), gain);

    deviceAttachment->sendInitialUpdate();

    deviceChanged();
}

CapturedBlockPanel::~CapturedBlockPanel() = default;

void CapturedBlockPanel::deviceChanged()
{
    for (auto& sc : scopes)
        sc->setSampleRate (amp.currentSampleRate());

    const auto* measured = block.measured();
    const auto  positions = block.gainPositions();

    // The list IS the combo: what a player has, greenest first. Nothing invented, nothing curated
    // into groups — the character ramp does the ordering a "type" heading used to.
    juce::Array<VoicingSelector::Entry> entries;
    bool sawUser = false;

    for (const auto& pack : block.packs)
    {
        VoicingSelector::Entry e;
        e.name = pack.displayName();
        e.character = pack.character;
        e.startsSection = ! pack.bundled && ! sawUser;   // the rule between shipped and added
        sawUser = sawUser || ! pack.bundled;
        entries.add (std::move (e));
    }

    device.setEntries (std::move (entries));

    caption = block.deviceName();

    // The circuit glyphs ride the FIRST picture only — five copies of the same badge is noise.
    scopes[0]->setSpec (felitronics::appkit::parseDeviceSpec (block.circuit()));

    // The gain knob's detents ARE the captured positions. Twenty-one for SM7, whatever the next
    // pack says for the next one. Detented where the pack has positions, continuous where it does
    // not — but always THERE: a knob with nothing to select drives instead, and a device without a
    // gain knob is not a device.
    gain.setNotches (juce::jmax (0, positions.size()));

    for (int i = 0; i < params::boostNumMeasured; ++i)
    {
        auto& slot = slots[(size_t) i];

        // ORDER MATTERS, and `slot = Slot {}` had it backwards. Move-assignment walks the members in
        // declaration order, so the knob was destroyed first and its attachment — which reaches for
        // the slider in its own destructor — went next, into freed memory. Changing a device crashed
        // the plugin, and it only showed once a second pack made the combo worth using.
        slot.knobAtt.reset();
        slot.stepAtt.reset();
        slot.knob.reset();
        slot.steps.reset();
        slot.measuredIndex = -1;

        if (measured == nullptr || i >= (int) measured->size())
            continue;

        const auto& m = (*measured)[(size_t) i];
        slot.measuredIndex = i;

        const auto name = juce::String (m.name).toUpperCase();

        // A switch is a control whose positions have NAMES. Not one with two of them: a measured
        // control's positions are the points it was measured AT, and the player interpolates between
        // them — two is a perfectly ordinary number for a knob that was swept at each end. Fur Coat's
        // EQ says "0" and "300", which are degrees, and counting them turned a tone knob into a
        // two-position switch stuck at one end.
        if (m.positions.size() == 2 && hasNamedPositions (m))
        {
            juce::StringArray labels;
            for (const auto& p : m.positions)
                labels.add (juce::String (p.label.empty() ? p.value : p.label).toUpperCase());

            slot.steps = std::make_unique<VSwitch>();
            slot.steps->accent = theme::orange;
            slot.steps->setItems (labels, 0);
            addAndMakeVisible (*slot.steps);

            slot.stepAtt = std::make_unique<juce::ParameterAttachment> (
                *amp.apvts.getParameter (params::blockMeasured (blk, i)),
                [this, i] (float v)
                {
                    if (auto* s = slots[(size_t) i].steps.get())
                        s->setSelectedIndex (v > 0.5f ? 1 : 0, juce::dontSendNotification);
                });

            slot.steps->onChange = [this, i] (int v)
            {
                slots[(size_t) i].stepAtt->setValueAsCompleteGesture (v > 0 ? 1.0f : 0.0f);
            };

            slot.stepAtt->sendInitialUpdate();

            // A switch has two positions and the parameter defaults to the middle of its range, which
            // for a knob is sensible and for a switch is a place the hardware cannot be. It played as
            // half of Smooth while the face lit Sharp, and clicking the lit half writes nothing — so
            // it could sit there forever. Land it on a real position as the device loads.
            // The SAME rule the switch lights by, below — off by half a step and the parameter says
            // Smooth while the face says Sharp, which is exactly the inversion this introduced.
            if (const float v = amp.apvts.getRawParameterValue (params::blockMeasured (blk, i))->load();
                v > 0.0f && v < 1.0f)
                slot.stepAtt->setValueAsCompleteGesture (v > 0.5f ? 1.0f : 0.0f);
        }
        else
        {
            slot.knob = std::make_unique<Knob> (name, theme::orange, (int) m.positions.size());
            slot.knob->textForValue = [] (double) { return juce::String(); };   // no numbers here
            addAndMakeVisible (*slot.knob);

            slot.knobAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                amp.apvts, params::blockMeasured (blk, i), *slot.knob);
        }
    }

    buildSelectors();

    resized();
    repaint();
}

void CapturedBlockPanel::buildSelectors()
{
    const auto list = block.stage.selectors();

    for (int i = 0; i < params::numSelectors; ++i)
    {
        auto& sel = selectors[(size_t) i];

        sel.attachment.reset();
        sel.steps.reset();

        if (i >= list.size())
            continue;

        const auto& def = list.getReference (i);

        juce::StringArray labels;
        for (const auto& v : def.values)
            labels.add (v.toUpperCase());

        sel.steps = std::make_unique<VSwitch>();
        sel.steps->accent = theme::orange;
        sel.steps->setItems (labels, 0);
        addAndMakeVisible (*sel.steps);

        sel.attachment = std::make_unique<juce::ParameterAttachment> (
            *amp.apvts.getParameter (params::selectorId (blk, i)),
            [this, i, n = labels.size()] (float v)
            {
                if (auto* s = selectors[(size_t) i].steps.get())
                    s->setSelectedIndex (juce::jlimit (0, n - 1, juce::roundToInt (v)),
                                         juce::dontSendNotification);
            });

        sel.steps->onChange = [this, i] (int v)
        {
            selectors[(size_t) i].attachment->setValueAsCompleteGesture ((float) v);
        };

        sel.attachment->sendInitialUpdate();
    }
}

bool CapturedBlockPanel::hasNamedPositions (const namz::rig::Measured& m)
{
    for (const auto& p : m.positions)
    {
        const auto s = juce::String (p.label.empty() ? p.value : p.label).trim();

        if (s.isNotEmpty() && ! s.containsOnly ("0123456789.+-"))
            return true;
    }

    return false;
}

void CapturedBlockPanel::layOutHeader (juce::Rectangle<int> area)
{
    device.setBounds (area.withTrimmedTop (6));   // the headline sits a touch lower
}

void CapturedBlockPanel::layOutContent (juce::Rectangle<int> area)
{
    // Two geometries, one face: the grand grid below is the ZOOM's — at overview size it made
    // doll's-house knobs. The compact overview keeps the old row.
    if (! zoomedFace)
    {
        layOutCompact (area);
        return;
    }

    // ---- the knob zone: the top third. GAIN is a full-height square on the left, always;
    //      the measured knobs grid to its right in cells sized AS IF there were six, so a
    //      two-knob device wears the same knobs a six-knob one does. ----
    auto zone = area.removeFromTop (area.getHeight() / 3);
    area.removeFromTop (gap);

    const int gainSide = juce::jmin (maxGainSide, zone.getHeight());
    gain.setBounds (zone.removeFromLeft (gainSide).withSizeKeepingCentre (gainSide, gainSide));
    zone.removeFromLeft (knobGap);

    std::vector<Knob*> knobs;
    for (auto& slot : slots)
        if (slot.knob != nullptr)
            knobs.push_back (slot.knob.get());

    std::vector<VSwitch*> switches;
    for (auto& sel : selectors)
        if (sel.steps != nullptr)
            switches.push_back (sel.steps.get());
    for (auto& slot : slots)
        if (slot.steps != nullptr)
            switches.push_back (slot.steps.get());

    const int k = (int) knobs.size();

    // The cell: a six-knob grid is three columns by two rows — that fit IS the size, whatever
    // the actual count.
    const int cell = juce::jmin (maxKnobSide,
                                 juce::jmin ((zone.getWidth() - 2 * knobGap) / 3,
                                             (zone.getHeight() - knobGap) / 2));

    // Row shapes per count: staggered rows carry fewer and centre over the fuller one.
    const auto rowsFor = [k] () -> std::vector<int>
    {
        switch (k)
        {
            case 6:  return { 3, 3 };
            case 5:  return { 2, 3 };
            case 4:  return { 2, 2 };
            case 3:  return { 1, 2 };
            case 2:  return { 1, 1 };
            case 1:  return { 1 };
            default: return {};
        }
    };

    const auto rows = rowsFor();
    int gridCols = 0;
    for (int n : rows)
        gridCols = juce::jmax (gridCols, n);

    const int gridW = gridCols > 0 ? gridCols * cell + (gridCols - 1) * knobGap : 0;

    // Switches go RIGHT whenever the room right of the grid can take them — the fallback row
    // under the knobs exists for the day a wide grid leaves no room.
    const bool swRight = ! switches.empty()
                      && zone.getWidth() - gridW - (gridW > 0 ? knobGap : 0) >= switchW;

    auto grid = zone.removeFromLeft (juce::jmax (gridW, 1));

    // The knobs, top row first; a staggered row centres over the widest.
    {
        size_t next = 0;
        int y = grid.getY();

        for (int rowN : rows)
        {
            const int rowW = rowN * cell + (rowN - 1) * knobGap;
            int x = grid.getX() + (gridW - rowW) / 2;

            for (int i = 0; i < rowN && next < knobs.size(); ++i)
            {
                knobs[next++]->setBounds (x, y, cell, cell);
                x += cell + knobGap;
            }

            y += cell + knobGap;
        }
    }

    if (! switches.empty())
    {
        if (swRight)
        {
            zone.removeFromLeft (knobGap);
            auto col = zone.removeFromLeft (switchW);

            for (auto* sw : switches)
            {
                sw->setBounds (col.removeFromTop (juce::jmin (col.getHeight(), sw->idealHeight())));
                col.removeFromTop (gap);
            }
        }
        else
        {
            // The fallback: a horizontal row of vertical switches under the grid.
            auto row = juce::Rectangle<int> (grid.getX(), grid.getY() + 2 * cell + 2 * knobGap,
                                             gridW, zone.getBottom() - (grid.getY() + 2 * cell + 2 * knobGap));
            const int each = juce::jmax (1, (row.getWidth() - ((int) switches.size() - 1) * gap)
                                                / (int) switches.size());

            for (auto* sw : switches)
            {
                sw->setBounds (row.removeFromLeft (each).withHeight (
                    juce::jmin (row.getHeight(), sw->idealHeight())));
                row.removeFromLeft (gap);
            }
        }
    }

    layOutViz (area);
}

void CapturedBlockPanel::layOutViz (juce::Rectangle<int> area)
{
    // The picture zone: the checkbox column picks which ways of looking are down; whichever are
    // tile the rest — one row up to three, two rows past.
    auto checks = area.removeFromLeft (110);
    for (auto& c : vizChecks)
    {
        c.setBounds (checks.removeFromTop (24));
        checks.removeFromTop (4);
    }
    area.removeFromLeft (gap);

    std::vector<DeviceScope*> shown;
    for (int i = 0; i < numViz; ++i)
    {
        if (vizChecks[(size_t) i].on)
            shown.push_back (scopes[(size_t) i].get());
        else
            scopes[(size_t) i]->setVisible (false);
    }

    const int n = (int) shown.size();
    if (n == 0)
        return;

    const int tileRows = n <= 3 ? 1 : 2;
    const int cols     = (n + tileRows - 1) / tileRows;
    const int tileW    = (area.getWidth() - (cols - 1) * gap) / juce::jmax (1, cols);
    const int tileH    = (area.getHeight() - (tileRows - 1) * gap) / tileRows;

    for (int i = 0; i < n; ++i)
    {
        const int rr = i / cols, cc = i % cols;
        shown[(size_t) i]->setBounds (area.getX() + cc * (tileW + gap),
                                      area.getY() + rr * (tileH + gap), tileW, tileH);
        shown[(size_t) i]->setVisible (true);
    }
}

void CapturedBlockPanel::layOutCompact (juce::Rectangle<int> area)
{
    // The overview's face: pictures along the bottom, the gain square and a single knob row
    // above, the switches wherever they fit — right of the knobs first, under them second.
    layOutViz (area.removeFromBottom (juce::jmax (150, area.getHeight() * 2 / 5)));
    area.removeFromBottom (gap);

    std::vector<Knob*> knobs;
    for (auto& slot : slots)
        if (slot.knob != nullptr)
            knobs.push_back (slot.knob.get());

    std::vector<VSwitch*> switches;
    for (auto& sel : selectors)
        if (sel.steps != nullptr)
            switches.push_back (sel.steps.get());
    for (auto& slot : slots)
        if (slot.steps != nullptr)
            switches.push_back (slot.steps.get());

    const int gainSide = juce::jmin (maxGainSide, juce::jmin (area.getHeight(), area.getWidth() / 2));
    gain.setBounds (area.removeFromLeft (gainSide).withSizeKeepingCentre (gainSide, gainSide));
    area.removeFromLeft (knobGap);

    const int k = (int) knobs.size();

    if (k > 0)
    {
        const int small = juce::jmin (maxKnobSide,
                                      juce::jmin (area.getHeight() / 2,
                                                  (area.getWidth() - (k - 1) * knobGap) / k));

        auto row = area.removeFromTop (small);
        for (auto* kn : knobs)
        {
            kn->setBounds (row.removeFromLeft (small));
            row.removeFromLeft (knobGap);
        }
        area.removeFromTop (gap);
    }

    for (auto* sw : switches)
    {
        sw->setBounds (area.removeFromTop (juce::jmin (area.getHeight(), sw->idealHeight()))
                           .withWidth (juce::jmin (area.getWidth(), switchW)));
        area.removeFromTop (gap / 2);
    }
}

void CapturedBlockPanel::paintContent (juce::Graphics& g)
{
    if (caption.isEmpty())
    {
        g.setColour (theme::txFaint.withAlpha (0.5f));
        theme::drawTracked (g, "No device loaded", contentArea().toFloat(), theme::displayFont (8.0f),
                            0.1f, juce::Justification::centred);
    }
}

} // namespace orbitamp
