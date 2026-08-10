#include "ChainStrip.h"

#include "../PluginProcessor.h"
#include "Theme.h"
#include "ZoneSwitch.h"

#include <felitronics/appkit/Brand.h>

namespace orbitamp
{

//==============================================================================
/** One link's thumbnail: title and switch on top, the preview in the middle, what is loaded into
    the link along the bottom. The thumb owns nothing it shows — the caption and the preview are
    callbacks into whoever knows, which keeps every kind of link the same few pixels of code. */
class ChainStrip::Thumb final : public juce::Component
{
public:
    Thumb (const juce::String& title, juce::Colour accentColour, juce::RangedAudioParameter& onParam)
        : name (title), accent (accentColour)
    {
        setAlpha (theme::offAlpha);   // attach() below corrects this for a link that is on

        addAndMakeVisible (sw);
        sw.accent = accent;
        sw.onChange = [this] (bool b) { setAlpha (b ? 1.0f : theme::offAlpha); };
        sw.attach (onParam);
    }

    std::function<juce::String()> caption;
    std::function<void (juce::Graphics&, juce::Rectangle<int>)> preview;
    std::function<void()> onClick;

    juce::String name;
    juce::Colour accent;

    void setLit (bool shouldBeLit)
    {
        if (lit == shouldBeLit)
            return;

        lit = shouldBeLit;
        repaint();
    }

    void resized() override
    {
        sw.setBounds (getLocalBounds().reduced (pad).removeFromTop (ZoneSwitch::designHeight)
                          .removeFromRight (ZoneSwitch::designWidth));
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        if (onClick != nullptr)
            onClick();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);

        juce::ColourGradient fill (theme::capTop.interpolatedWith (theme::dspTop, 0.5f), 0.0f, r.getY(),
                                   theme::capBot.interpolatedWith (theme::dspBot, 0.5f), 0.0f, r.getBottom(), false);
        g.setGradientFill (fill);
        g.fillRoundedRectangle (r, theme::radiusMd);

        // The border carries the same colour code the blocks do — and brightens when this link is
        // the one the faceplate has open.
        g.setColour (accent.withAlpha (lit ? 0.95f : 0.45f));
        g.drawRoundedRectangle (r, theme::radiusMd, lit ? 1.8f : theme::blockBorder);

        auto area = getLocalBounds().reduced (pad);

        auto top = area.removeFromTop (11);
        g.setColour (accent);
        theme::drawTracked (g, name.toUpperCase(), top.toFloat(), theme::displayFont (7.0f), 0.12f,
                            juce::Justification::centredLeft);

        auto label = area.removeFromBottom (10);
        if (caption != nullptr)
        {
            g.setColour (theme::txDim);
            theme::drawTracked (g, caption().toUpperCase(), label.toFloat(), theme::displayFont (6.5f),
                                0.08f, juce::Justification::centred);
        }

        if (preview != nullptr)
            preview (g, area.reduced (1, 3));
    }

private:
    static constexpr int pad = 7;

    ZoneSwitch sw;
    bool lit = false;
};

//==============================================================================
namespace
{
    /** A response curve in a preview box, ±15 dB against a faint zero line. */
    void drawMiniCurve (juce::Graphics& g, juce::Rectangle<int> box,
                        const std::function<double (double)>& magnitudeDb, juce::Colour colour)
    {
        const auto r = box.toFloat();
        constexpr float rangeDb = 15.0f;

        g.setColour (theme::hair2);
        g.fillRect (r.getX(), r.getCentreY(), r.getWidth(), 1.0f);

        juce::Path p;
        const int w = juce::jmax (2, box.getWidth());

        for (int x = 0; x < w; ++x)
        {
            const double hz = 20.0 * std::pow (1000.0, (double) x / (double) (w - 1));   // 20 Hz .. 20 kHz
            const float  db = juce::jlimit (-rangeDb, rangeDb, (float) magnitudeDb (hz));
            const float  y  = r.getCentreY() - db / rangeDb * (r.getHeight() * 0.5f - 1.0f);

            if (x == 0) p.startNewSubPath (r.getX(), y);
            else        p.lineTo (r.getX() + (float) x, y);
        }

        g.setColour (colour);
        g.strokePath (p, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    }

    /** A knob at a glance: the ring and where it points. `t` is 0..1 across the travel. */
    void drawKnobGlyph (juce::Graphics& g, juce::Rectangle<float> r, float t, juce::Colour colour)
    {
        const float side = juce::jmin (r.getWidth(), r.getHeight());
        const auto  k    = r.withSizeKeepingCentre (side, side).reduced (1.0f);

        g.setColour (theme::bezel);
        g.fillEllipse (k);
        g.setColour (colour.withAlpha (0.7f));
        g.drawEllipse (k, 1.2f);

        // The same seven-o'clock-to-five-o'clock sweep every real knob on the face makes.
        const float angle = juce::MathConstants<float>::pi * (1.25f - 2.5f * juce::jlimit (0.0f, 1.0f, t));
        const auto  c     = k.getCentre();
        const float rad   = side * 0.5f - 2.0f;

        g.setColour (colour);
        g.drawLine (c.x, c.y,
                    c.x + rad * std::cos (angle + juce::MathConstants<float>::halfPi),
                    c.y - rad * std::sin (angle + juce::MathConstants<float>::halfPi), 1.6f);
    }
}

//==============================================================================
ChainStrip::ChainStrip (AmpProcessor& processor) : amp (processor)
{
    auto& s = amp.apvts;

    for (auto& eq : eqDisplay)
        eq.prepare (48000.0, 1);

    auto make = [&] (ChainLink link, const juce::String& title, juce::Colour accent,
                     const juce::String& onId)
    {
        auto t = std::make_unique<Thumb> (title, accent, *s.getParameter (onId));
        t->onClick = [this, link] { if (onOpen != nullptr) onOpen (link); };
        addAndMakeVisible (*t);
        thumbs[(size_t) link] = std::move (t);
        return thumbs[(size_t) link].get();
    };

    // The EQ links: the curve is the whole story, told by a display stack fed from the parameters
    // on the strip's clock.
    for (int l = 0; l < params::numEqLinks; ++l)
    {
        auto* t = make ((ChainLink) (l == 0 ? ChainLink::eq1 : ChainLink::eq2),
                        "EQ " + juce::String (l + 1), theme::violet, params::eqOn (l));

        t->preview = [this, l] (juce::Graphics& g, juce::Rectangle<int> box)
        {
            drawMiniCurve (g, box, [this, l] (double hz) { return eqDisplay[(size_t) l].magnitudeDb (hz); },
                           theme::violet);
        };
    }

    // The captured blocks: which device, the gain dial, and what its measured controls are doing —
    // the tone curve straight from the block, the same data its filters were designed from.
    auto captured = [&] (ChainLink link, const juce::String& title, const char* blk, auto& block)
    {
        auto* t = make (link, title, theme::orange, params::blockOn (blk));

        t->caption = [&block] { const auto n = block.deviceName(); return n.isEmpty() ? juce::String ("—") : n; };

        auto* gainParam = s.getRawParameterValue (params::blockGain (blk));

        t->preview = [&block, gainParam] (juce::Graphics& g, juce::Rectangle<int> box)
        {
            auto knob = box.removeFromLeft (box.getHeight()).toFloat();
            drawKnobGlyph (g, knob, gainParam->load() * 0.1f, theme::orange);
            box.removeFromLeft (4);
            drawMiniCurve (g, box, [&block] (double hz) { return block.toneDb (hz); }, theme::orange);
        };
    };

    captured (ChainLink::boost,  "Boost",  params::boostId,  amp.boost);
    captured (ChainLink::preamp, "Preamp", params::preampId, amp.preamp);

    // The reverb: its character is what is "loaded", the mix is the one knob it has.
    {
        auto* t = make (ChainLink::reverb, "Reverb", theme::violet, params::reverbOn);

        auto* type = s.getRawParameterValue (params::reverbType);
        auto* mix  = s.getRawParameterValue (params::reverbMix);

        t->caption = [type]
        {
            return params::reverbCharacters[juce::jlimit (0, params::reverbCharacters.size() - 1,
                                                          juce::roundToInt (type->load()))];
        };

        t->preview = [mix] (juce::Graphics& g, juce::Rectangle<int> box)
        {
            drawKnobGlyph (g, box.toFloat(), mix->load() * 0.01f, theme::violet);
        };
    }

    // The power amp: simulation or a capture — the accent follows, like the block's frame does.
    {
        auto* t = make (ChainLink::power, "Power", theme::violet, params::powerOn);

        auto* type  = s.getRawParameterValue (params::powerType);
        auto* drive = s.getRawParameterValue (params::powerDrive);
        auto* sag   = s.getRawParameterValue (params::powerSag);

        t->caption = [type]
        {
            return params::powerTypes[juce::jlimit (0, params::powerTypes.size() - 1,
                                                    juce::roundToInt (type->load()))];
        };

        // `t` outlives its own preview, so the glyphs can follow the accent the border wears.
        t->preview = [t, drive, sag] (juce::Graphics& g, juce::Rectangle<int> box)
        {
            auto left = box.removeFromLeft (box.getWidth() / 2);
            drawKnobGlyph (g, left.toFloat(), drive->load() * 0.1f, t->accent);
            drawKnobGlyph (g, box.toFloat(),  sag->load()   * 0.1f, t->accent);
        };
    }

    // The cabinet: the mics on the grille — position along the rows, distance along the width,
    // slot-coloured like the grid itself.
    {
        auto* t = make (ChainLink::cab, "Cabinet", theme::orange, params::cabOn);

        struct Mic { std::atomic<float> *on, *type, *pos, *dist; };
        auto mics = std::make_shared<std::array<Mic, (size_t) params::cabNumMics>>();

        for (int i = 0; i < params::cabNumMics; ++i)
            (*mics)[(size_t) i] = { s.getRawParameterValue (params::cabMicOn (i)),
                                    s.getRawParameterValue (params::cabMicType (i)),
                                    s.getRawParameterValue (params::cabMicPos (i)),
                                    s.getRawParameterValue (params::cabMicDist (i)) };

        t->caption = [mics]
        {
            juce::StringArray names;
            for (const auto& m : *mics)
                if (m.on->load() > 0.5f)
                    names.add (params::cabMics[juce::jlimit (0, params::cabMics.size() - 1,
                                                             juce::roundToInt (m.type->load()))]);

            return names.isEmpty() ? juce::String ("—") : names.joinIntoString (" + ");
        };

        t->preview = [mics] (juce::Graphics& g, juce::Rectangle<int> box)
        {
            const auto r = box.toFloat().reduced (2.0f);

            // The rows are the speaker's own zones, rim inwards — the same ladder the grid draws.
            const int rows = params::cabPositions.size();
            for (int i = 0; i < rows; ++i)
            {
                const float y = r.getY() + r.getHeight() * ((float) i + 0.5f) / (float) rows;
                g.setColour (theme::hair);
                g.fillRect (r.getX(), y, r.getWidth(), 1.0f);
            }

            for (int i = 0; i < params::cabNumMics; ++i)
            {
                const auto& m = (*mics)[(size_t) i];
                if (m.on->load() < 0.5f)
                    continue;

                const int   row = juce::jlimit (0, rows - 1, juce::roundToInt (m.pos->load()));
                const float x   = r.getX() + r.getWidth() * juce::jlimit (0.0f, 1.0f, m.dist->load() / 15.0f);
                const float y   = r.getY() + r.getHeight() * ((float) row + 0.5f) / (float) rows;

                g.setColour (felitronics::appkit::brand::slotColours[(size_t) i]);
                g.fillEllipse (x - 2.5f, y - 2.5f, 5.0f, 5.0f);
            }
        };
    }

    // The previews follow parameters and loaded devices, not events — a steady low-rate repaint is
    // simpler than attaching to two dozen parameters, and cheaper than being wrong about one.
    startTimerHz (15);
}

ChainStrip::~ChainStrip() = default;

void ChainStrip::setActive (std::optional<ChainLink> link)
{
    for (int i = 0; i < numChainLinks; ++i)
        thumbs[(size_t) i]->setLit (link.has_value() && (int) *link == i);
}

void ChainStrip::timerCallback()
{
    // Redesign the EQ previews from wherever the parameters are now; setSettings itself skips the
    // work when nothing moved.
    for (int l = 0; l < params::numEqLinks; ++l)
    {
        auto& s = amp.apvts;

        core::ToneStack::Settings set;
        set.lowDb      = (double) s.getRawParameterValue (params::eqLow (l))->load();
        set.midDb      = (double) s.getRawParameterValue (params::eqMid (l))->load();
        set.midHz      = (double) s.getRawParameterValue (params::eqMidHz (l))->load();
        set.highDb     = (double) s.getRawParameterValue (params::eqHigh (l))->load();
        set.presenceDb = (double) s.getRawParameterValue (params::eqPresence (l))->load();
        set.hpfOn      = s.getRawParameterValue (params::eqHpfOn (l))->load() > 0.5f;
        set.hpfHz      = (double) s.getRawParameterValue (params::eqHpfHz (l))->load();
        set.lpfOn      = s.getRawParameterValue (params::eqLpfOn (l))->load() > 0.5f;
        set.lpfHz      = (double) s.getRawParameterValue (params::eqLpfHz (l))->load();

        eqDisplay[(size_t) l].setSettings (set);
    }

    // The power amp recolours with what is loaded into it, the same truth-telling the block does.
    {
        const bool captured = juce::roundToInt (
            amp.apvts.getRawParameterValue (params::powerType)->load()) > 0;
        thumbs[(size_t) ChainLink::power]->accent = captured ? theme::orange : theme::violet;
    }

    for (auto& t : thumbs)
        t->repaint();
}

void ChainStrip::resized()
{
    const float w = ((float) getWidth() - (float) thumbGap * (numChainLinks - 1)) / (float) numChainLinks;

    for (int i = 0; i < numChainLinks; ++i)
        thumbs[(size_t) i]->setBounds (juce::Rectangle<float> (
            (w + (float) thumbGap) * (float) i, 0.0f, w, (float) getHeight()).toNearestInt());
}

void ChainStrip::paint (juce::Graphics& g)
{
    // The flow marks in the gaps — the strip is a signal path, not a row of tiles.
    g.setColour (theme::txFaint);

    for (int i = 1; i < numChainLinks; ++i)
    {
        const auto left  = thumbs[(size_t) (i - 1)]->getBounds();
        const auto right = thumbs[(size_t) i]->getBounds();

        const float cx = (float) (left.getRight() + right.getX()) * 0.5f;
        const float cy = (float) getHeight() * 0.5f;

        juce::Path arrow;
        arrow.addTriangle (cx - 2.0f, cy - 3.5f, cx - 2.0f, cy + 3.5f, cx + 2.5f, cy);
        g.fillPath (arrow);
    }
}

} // namespace orbitamp
