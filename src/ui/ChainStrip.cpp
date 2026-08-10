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
    /** `onParam` may be null — a link with nothing to switch (the tuner) gets no toggle and is
        always at full strength. */
    Thumb (const juce::String& title, juce::Colour accentColour, juce::RangedAudioParameter* onParam)
        : name (title), accent (accentColour)
    {
        if (onParam == nullptr)
            return;

        setAlpha (theme::offAlpha);   // attach() below corrects this for a link that is on

        sw = std::make_unique<ZoneSwitch>();
        addAndMakeVisible (*sw);
        sw->accent = accent;
        sw->onChange = [this] (bool b) { setAlpha (b ? 1.0f : theme::offAlpha); };
        sw->attach (*onParam);
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

    /** Half-lane thumbs shrink their chrome instead of clipping it: a smaller switch and a
        tighter title, so "GATE" and its toggle share a row a tone link would give the title
        alone. */
    bool narrow() const { return getWidth() < 70; }

    void resized() override
    {
        if (sw == nullptr)
            return;

        const int w = narrow() ? 16 : ZoneSwitch::designWidth;
        const int h = narrow() ? 9  : ZoneSwitch::designHeight;

        sw->setBounds (getLocalBounds().reduced (narrow() ? 4 : pad, pad)
                           .removeFromTop (ZoneSwitch::designHeight).removeFromRight (w).withHeight (h));
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

        auto area = getLocalBounds().reduced (narrow() ? 4 : pad, pad);

        auto top = area.removeFromTop (11);
        g.setColour (accent);
        theme::drawTracked (g, name.toUpperCase(), top.toFloat(),
                            theme::displayFont (narrow() ? 6.0f : 7.0f), narrow() ? 0.06f : 0.12f,
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

    std::unique_ptr<ZoneSwitch> sw;
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
        auto t = std::make_unique<Thumb> (title, accent,
                                          onId.isEmpty() ? nullptr : s.getParameter (onId));
        t->onClick = [this, link] { if (onOpen != nullptr) onOpen (link); };
        addAndMakeVisible (*t);
        thumbs[(size_t) link] = std::move (t);
        return thumbs[(size_t) link].get();
    };

    // The tuner: no switch — it does nothing to the signal there could be a switch about. The
    // needle is the whole preview, reading the same ear the zoomed tuner reads.
    {
        auto* t = make (ChainLink::tuner, "Tuner", theme::violet, {});

        const auto& ear = amp.tunerEar;

        t->caption = [&ear]
        {
            // Silence earns an empty line, not a dash — the display face owes us no dash, and a
            // multi-byte one through a plain char* reads as garbage anyway.
            if (! ear.live())
                return juce::String();

            const auto note = ear.nearestNote();
            return juce::String (core::PitchTracker::noteName (note.midi))
                     + juce::String (core::PitchTracker::noteOctave (note.midi));
        };

        t->preview = [&ear] (juce::Graphics& g, juce::Rectangle<int> box)
        {
            const auto r = box.toFloat().reduced (3.0f, 4.0f);
            const float mid = r.getBottom() - 4.0f;

            for (int c = -50; c <= 50; c += 10)
            {
                const float x = r.getX() + r.getWidth() * (float) (c + 50) / 100.0f;
                const float h = c == 0 ? 14.0f : 6.0f;
                g.setColour (c == 0 ? theme::hair2 : theme::hair);
                g.fillRect (juce::Rectangle<float> (x - 0.5f, mid - h, 1.0f, h));
            }

            if (! ear.live())
                return;

            const float x = r.getX()
                          + r.getWidth() * (juce::jlimit (-50.0f, 50.0f, ear.needle()) + 50.0f) / 100.0f;
            g.setColour (ear.green() ? juce::Colour (0xff5fc97a) : theme::tx);
            g.fillRoundedRectangle (x - 1.0f, mid - 18.0f, 2.0f, 21.0f, 1.0f);
        };
    }

    // The gate, indicated the way gates are: the key level running along a scale with both
    // decision marks on it — where it opens, where it closes — and the fill dimming while the
    // gate holds. The zoomed face draws the same picture at reading size.
    {
        auto* t = make (ChainLink::gate, "Gate", theme::violet, params::gateOn);

        auto* thresholdParam = s.getRawParameterValue (params::gateThreshold);
        const auto& pressureDb = amp.gateMeterDb;
        const auto& keyDb      = amp.gateKeyDb;

        t->caption = [thresholdParam]
        {
            return juce::String (juce::roundToInt (thresholdParam->load())) + " DB";
        };

        t->preview = [&pressureDb, &keyDb, thresholdParam] (juce::Graphics& g, juce::Rectangle<int> box)
        {
            const auto r = box.toFloat().reduced (2.0f, (float) box.getHeight() / 3.0f);
            constexpr float floorDb = -80.0f;
            const auto dbToX = [&r] (float db)
            {
                return r.getX() + r.getWidth() * (juce::jlimit (floorDb, 0.0f, db) - floorDb) / -floorDb;
            };

            g.setColour (theme::bezel);
            g.fillRoundedRectangle (r, theme::radiusSm);

            const bool  closed = pressureDb.load() < -1.0f;
            const float lx     = dbToX (keyDb.load());

            if (lx > r.getX() + 1.0f)
            {
                g.setColour (theme::violet.withAlpha (closed ? 0.30f : 0.75f));
                g.fillRoundedRectangle (r.withRight (lx).reduced (1.0f), theme::radiusSm);
            }

            const float openDb = thresholdParam->load();
            g.setColour (theme::lilac.withAlpha (0.45f));
            g.fillRect (dbToX (openDb - params::gateHysteresisDb) - 0.5f, r.getY(), 1.0f, r.getHeight());
            g.setColour (theme::lilac);
            g.fillRect (dbToX (openDb) - 0.75f, r.getY(), 1.5f, r.getHeight());

            g.setColour (theme::hair2);
            g.drawRoundedRectangle (r.reduced (0.5f), theme::radiusSm, 1.0f);
        };
    }

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

        t->caption = [&block] { return block.deviceName(); };

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

            return names.joinIntoString (" + ");
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

    // The previews follow parameters and loaded devices, not events — a steady repaint is simpler
    // than attaching to two dozen parameters, and cheaper than being wrong about one. 30 Hz
    // because the tuner's needle lives here now, and a needle at 15 reads as a slideshow.
    startTimerHz (30);
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
    // Weighted lanes: a service link takes half a tone link's width, and the unit falls out of
    // whatever the weights add up to.
    float units = 0.0f;
    for (int i = 0; i < numChainLinks; ++i)
        units += chainLinkWeight ((ChainLink) i);

    const float unit = ((float) getWidth() - (float) thumbGap * (numChainLinks - 1)) / units;

    float x = 0.0f;
    for (int i = 0; i < numChainLinks; ++i)
    {
        const float w = unit * chainLinkWeight ((ChainLink) i);
        thumbs[(size_t) i]->setBounds (juce::Rectangle<float> (x, 0.0f, w, (float) getHeight()).toNearestInt());
        x += w + (float) thumbGap;
    }
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
