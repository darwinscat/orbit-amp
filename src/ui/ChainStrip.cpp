#include "ChainStrip.h"

#include "../PluginProcessor.h"
#include "Theme.h"
#include "ZoneSwitch.h"

#include <felitronics/appkit/Brand.h>

namespace orbitamp
{

//==============================================================================
/** The horizontal gain runner inside a captured thumb: a track, its filled share in orange, the
    grip notch at the value. It eats its own clicks whole — the empty thumb around it stays the
    door to the zoom. */
class MiniGain final : public juce::Component
{
public:
    explicit MiniGain (juce::RangedAudioParameter& p) : param (p)
    {
        att = std::make_unique<juce::ParameterAttachment> (p, [this] (float) { repaint(); });
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        const auto track = r.withSizeKeepingCentre (r.getWidth(), 6.0f);

        g.setColour (theme::hair);
        g.fillRoundedRectangle (track, 3.0f);

        const float prop = param.getValue();
        const float x    = track.getX() + track.getWidth() * prop;

        g.setColour (theme::orange.withAlpha (0.85f));
        g.fillRoundedRectangle (track.withWidth (track.getWidth() * prop), 3.0f);

        g.setColour (juce::Colours::white);
        g.fillRoundedRectangle (x - 1.5f, r.getY(), 3.0f, r.getHeight(), 1.5f);
    }

    std::function<void (bool)> onDragActive;

    void mouseDown (const juce::MouseEvent& e) override
    {
        att->beginGesture();
        att->setValueAsPartOfGesture (fromX (e.position.x));
        if (onDragActive != nullptr)
            onDragActive (true);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        att->setValueAsPartOfGesture (fromX (e.position.x));
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        att->endGesture();
        if (onDragActive != nullptr)
            onDragActive (false);
    }

private:
    float fromX (float x) const
    {
        const float w = juce::jmax (1.0f, (float) getWidth() - 2.0f);
        return param.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, (x - 1.0f) / w));
    }

    juce::RangedAudioParameter& param;
    std::unique_ptr<juce::ParameterAttachment> att;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MiniGain)
};

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

        dimmed = true;   // attach() below corrects this for a link that is on

        sw = std::make_unique<ZoneSwitch>();
        addAndMakeVisible (*sw);
        sw->accent = accent;
        sw->onChange = [this] (bool b) { dimmed = ! b; repaint(); };
        sw->attach (*onParam);
    }

    std::function<juce::String()> caption;
    std::function<void (juce::Graphics&, juce::Rectangle<int>)> preview;
    std::function<void()> onClick;

    /** Captured thumbs stack downward: name under the title, the gain runner under the name. */
    bool captionUnderTitle = false;

    /** The runner entered/left the hand — the editor summons the magnified ladder. */
    std::function<void (bool)> onGainDrag;

    void attachGain (juce::RangedAudioParameter& p)
    {
        gain = std::make_unique<MiniGain> (p);
        gain->onDragActive = [this] (bool a) { if (onGainDrag != nullptr) onGainDrag (a); };
        addAndMakeVisible (*gain);
        resized();
    }

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
        if (sw != nullptr)
        {
            const int w = narrow() ? 20 : 26;
            const int h = narrow() ? 11 : 14;

            sw->setBounds (getLocalBounds().reduced (narrow() ? 4 : pad, pad)
                               .removeFromTop (h).removeFromRight (w).withHeight (h));
        }

        if (gain != nullptr)
        {
            auto area = getLocalBounds().reduced (narrow() ? 4 : pad, pad);
            area.removeFromTop (14 + 3);                // the title row, plus breathing room
            if (captionUnderTitle) area.removeFromTop (11 + 3);
            gain->setBounds (area.removeFromTop (12));
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        // Right-click: the power menu, same as on the block itself. Left: open the zoom.
        if (e.mods.isPopupMenu())
        {
            if (sw == nullptr)
                return;

            juce::PopupMenu m;
            m.addSectionHeader (name.toUpperCase());
            m.addItem (1, "ON", true, sw->paramOn());

            m.showMenuAsync (juce::PopupMenu::Options()
                                 .withTargetScreenArea ({ e.getScreenPosition().x,
                                                          e.getScreenPosition().y, 1, 1 }),
                             [safe = juce::Component::SafePointer<Thumb> (this)] (int r)
                             {
                                 if (r == 1 && safe != nullptr && safe->sw != nullptr)
                                     safe->sw->toggle();
                             });
            return;
        }

        if (onClick != nullptr)
            onClick();
    }

    void paint (juce::Graphics& g) override
    {
        // Dim the FACE, not the component — the switch is a child painted after this, at full
        // strength: on a dark thumb it is the one thing that must still read. The thumb stays
        // dark under the mouse; hover lights the switch alone.
        const bool dim = dimmed;
        if (dim)
            g.beginTransparencyLayer (theme::offAlpha);

        auto r = getLocalBounds().toFloat().reduced (0.5f);

        juce::ColourGradient fill (theme::capTop.interpolatedWith (theme::dspTop, 0.5f), 0.0f, r.getY(),
                                   theme::capBot.interpolatedWith (theme::dspBot, 0.5f), 0.0f, r.getBottom(), false);
        g.setGradientFill (fill);
        g.fillRoundedRectangle (r, theme::radiusMd);

        // The border carries the same colour code the blocks do — a live link wears it near full
        // strength, the open one brightest of all. (Off thumbs dim wholesale via their alpha.)
        g.setColour (accent.withAlpha (lit ? 0.95f : 0.65f));
        g.drawRoundedRectangle (r, theme::radiusMd, lit ? 1.8f : theme::blockBorder);

        auto area = getLocalBounds().reduced (narrow() ? 4 : pad, pad);

        auto top = area.removeFromTop (14);
        g.setColour (accent);
        theme::drawTracked (g, name.toUpperCase(), top.toFloat(),
                            theme::displayFont (narrow() ? 8.0f : 10.0f), narrow() ? 0.06f : 0.12f,
                            juce::Justification::centredLeft);

        if (caption != nullptr && captionUnderTitle)
        {
            area.removeFromTop (3);
            auto label = area.removeFromTop (11);
            g.setColour (theme::txDim);
            theme::drawTracked (g, caption().toUpperCase(), label.toFloat(), theme::displayFont (8.0f),
                                0.08f, juce::Justification::centredLeft);
        }
        else if (caption != nullptr)
        {
            auto label = area.removeFromBottom (10);
            g.setColour (theme::txDim);
            theme::drawTracked (g, caption().toUpperCase(), label.toFloat(), theme::displayFont (6.5f),
                                0.08f, juce::Justification::centred);
        }

        if (gain != nullptr)
            area.removeFromTop (3 + 12 + 2);            // the runner's row (a child, painted by itself)

        if (preview != nullptr)
            preview (g, area.reduced (1, 1));

        if (dim)
            g.endTransparencyLayer();
    }

private:
    static constexpr int pad = 7;

    std::unique_ptr<ZoneSwitch> sw;
    std::unique_ptr<MiniGain> gain;
    bool lit = false;
    bool dimmed = false;
};

//==============================================================================
namespace
{
    /** A response curve in a preview box, ±15 dB against the zero hairline — the zoom's grammar
        in miniature. `deviationFill` paints the area between curve and zero in brand violet. */
    void drawMiniCurve (juce::Graphics& g, juce::Rectangle<int> box,
                        const std::function<double (double)>& magnitudeDb, juce::Colour colour,
                        bool deviationFill = false)
    {
        const auto r = box.toFloat();
        constexpr float rangeDb = 15.0f;

        g.setColour (juce::Colour (0x40ffffff));
        g.fillRect (r.getX(), r.getCentreY(), r.getWidth(), 1.0f);

        // NOT clamped: a cut keeps diving and leaves through the floor — pinning it to the edge
        // drew a wall crawling along the bottom that the filter does not have. The clip is what
        // ends the line.
        const juce::Graphics::ScopedSaveState clipped (g);
        g.reduceClipRegion (box);

        const int w = juce::jmax (2, box.getWidth());

        juce::Path p, fill;
        fill.startNewSubPath (r.getX(), r.getCentreY());

        for (int x = 0; x < w; ++x)
        {
            const double hz = 20.0 * std::pow (1000.0, (double) x / (double) (w - 1));   // 20 Hz .. 20 kHz
            const float  db = (float) magnitudeDb (hz);
            const float  y  = r.getCentreY() - db / rangeDb * (r.getHeight() * 0.5f - 1.0f);

            if (x == 0) p.startNewSubPath (r.getX(), y);
            else        p.lineTo (r.getX() + (float) x, y);
            fill.lineTo (r.getX() + (float) x, y);
        }

        if (deviationFill)
        {
            fill.lineTo (r.getRight(), r.getCentreY());
            fill.closeSubPath();

            // Both halves in one pass at thumb size: violet, densest at the zero line.
            g.setGradientFill (juce::ColourGradient (theme::violet.withAlpha (0.40f), 0.0f, r.getCentreY(),
                                                     theme::violet.withAlpha (0.06f), 0.0f, r.getBottom(),
                                                     false));
            g.fillPath (fill);
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

    // No tuner thumb: the full-width TunerStrip by the footer is the tuner's one home — the
    // needle lane there is better at the job than a half-lane tile ever was. The zoom face
    // still exists; the strip below opens it.

    // No gate thumb either: the IN sliver left of the faceplate IS the gate's face out here —
    // meter, threshold, pressure and presets in one column; its click opens the zoom.

    // The EQ links: the curve, and — while the link is switched on — the live spectrum under it,
    // read from the same tap the zoom reads.
    for (int l = 0; l < params::numEqLinks; ++l)
    {
        auto* t = make ((ChainLink) (l == 0 ? ChainLink::eq1 : ChainLink::eq2),
                        "EQ " + juce::String (l + 1), theme::violet, params::eqOn (l));

        auto* onParam = s.getRawParameterValue (params::eqOn (l));

        t->preview = [this, l, onParam] (juce::Graphics& g, juce::Rectangle<int> box)
        {
            if (onParam->load() > 0.5f)
            {
                const auto r = box.toFloat();

                felitronics::analysis::PlotMap pm;
                pm.width      = r.getWidth();
                pm.height     = r.getHeight();
                pm.plotBottom = r.getHeight();
                pm.freqMin    = 20.0;
                pm.freqMax    = 20000.0;
                pm.specTop    = 0.0;
                pm.specBottom = -90.0;

                juce::Path fill;
                fill.startNewSubPath (r.getX(), r.getBottom());

                eqSpecPane[(size_t) l].buildColumns (pm, amp.currentSampleRate(), 4.5, 1000.0,
                                                     [&] (int, float x, float yFill, float)
                                                     {
                                                         fill.lineTo (r.getX() + x, r.getY() + yFill);
                                                     });

                fill.lineTo (r.getRight(), r.getBottom());
                fill.closeSubPath();

                g.setGradientFill (juce::ColourGradient (theme::spectrum.withAlpha (0.24f),
                                                         0.0f, r.getY() + r.getHeight() * 0.30f,
                                                         theme::spectrum.withAlpha (0.04f),
                                                         0.0f, r.getBottom(), false));
                g.fillPath (fill);
            }

            drawMiniCurve (g, box, [this, l] (double hz) { return eqDisplay[(size_t) l].magnitudeDb (hz); },
                           theme::orange, true);
        };
    }

    // The captured blocks, stacked: the device's NAME under the title, the gain as a horizontal
    // runner under the name, and the WAVE below — the ribbon's live silhouette in violet with
    // the tone curve in orange over it: what the pedal is, and what it does to the spectrum.
    auto captured = [&] (ChainLink link, const juce::String& title, const char* blk, auto& block)
    {
        auto* t = make (link, title, theme::orange, params::blockOn (blk));

        t->captionUnderTitle = true;
        t->caption = [&block] { return block.deviceName(); };
        t->attachGain (*s.getParameter (params::blockGain (blk)));
        t->onGainDrag = [this, link] (bool a) { if (onGainDrag != nullptr) onGainDrag (link, a); };

        t->preview = [this, &block] (juce::Graphics& g, juce::Rectangle<int> box)
        {
            const auto r = box.toFloat().reduced (0.0f, 1.0f);

            // The wave: the ribbon read at ITS resolution and bucketed here per pixel — calling
            // setResolution from a second consumer would fight the zoom's scope over it.
            int count = 0;
            float phase = 0.0f;

            if (block.ribbon.read (ribbonBuf, count, phase) && count > 1)
            {
                const int px = juce::jmax (2, box.getWidth());

                // HALF-wave on the dB ruler, like the big WAVE — magnitude off the floor,
                // magnified the way only a log can. (The 0.55 power curve is the one-line
                // alternative if this reads too swollen.)
                const auto mag = [] (float a)
                {
                    const float db = juce::Decibels::gainToDecibels (std::abs (a), -80.0f);
                    return juce::jlimit (0.0f, 1.0f, (db + 80.0f) / 80.0f);
                };

                juce::Path wave;
                wave.startNewSubPath (r.getX(), r.getBottom());

                for (int x = 0; x < px; ++x)
                {
                    const int a = x * count / px, b = juce::jmax (a + 1, (x + 1) * count / px);
                    float m = 0.0f;
                    for (int i = a; i < b && i < count; ++i)
                        m = juce::jmax (m, juce::jmax (ribbonBuf[(size_t) i].wetHi,
                                                       -ribbonBuf[(size_t) i].wetLo));
                    wave.lineTo (r.getX() + (float) x, r.getBottom() - mag (m) * r.getHeight());
                }

                wave.lineTo (r.getX() + (float) (px - 1), r.getBottom());
                wave.closeSubPath();
                g.setColour (theme::violet.withAlpha (0.35f));
                g.fillPath (wave);
            }

            // The tone curve over it, plain: no axis — two signatures, not a graph.
            juce::Path curve;
            constexpr float rangeDb = 15.0f;
            const int w = juce::jmax (2, box.getWidth());

            for (int x = 0; x < w; ++x)
            {
                const double hz = 20.0 * std::pow (1000.0, (double) x / (double) (w - 1));
                const float  y  = r.getCentreY()
                                - (float) block.toneDb (hz) / rangeDb * (r.getHeight() * 0.5f - 1.0f);
                if (x == 0) curve.startNewSubPath (r.getX(), y);
                else        curve.lineTo (r.getX() + (float) x, y);
            }

            g.setColour (theme::orange);
            g.strokePath (curve, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
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

juce::Rectangle<int> ChainStrip::thumbBounds (ChainLink l) const
{
    return thumbs[(size_t) l] != nullptr ? thumbs[(size_t) l]->getBounds()
                                         : juce::Rectangle<int>();
}

void ChainStrip::setActive (std::optional<ChainLink> link)
{
    for (int i = 0; i < numChainLinks; ++i)
        if (thumbs[(size_t) i] != nullptr)
            thumbs[(size_t) i]->setLit (link.has_value() && (int) *link == i);
}

void ChainStrip::timerCallback()
{
    // Redesign the EQ previews from wherever the parameters are now; setSettings itself skips the
    // work when nothing moved.
    for (int l = 0; l < params::numEqLinks; ++l)
    {
        auto& s = amp.apvts;
        const auto raw = [&s] (const juce::String& id) { return s.getRawParameterValue (id)->load(); };
        const auto slope = [] (float v)
        {
            return params::eqSlopeValues[juce::jlimit (0, params::eqSlopes.size() - 1, juce::roundToInt (v))];
        };

        core::EqLink::Settings set;
        set.hpfOn    = raw (params::eqHpfOn (l)) > 0.5f;
        set.hpfHz    = (double) raw (params::eqHpfHz (l));
        set.hpfSlope = slope (raw (params::eqHpfSlope (l)));
        set.loDb     = (double) raw (params::eqLoDb (l));
        set.loHz     = (double) raw (params::eqLoHz (l));
        set.b1Db     = (double) raw (params::eqBellDb (l, 0));
        set.b1Hz     = (double) raw (params::eqBellHz (l, 0));
        set.b1Q      = (double) raw (params::eqBellQ (l, 0));
        set.b2Db     = (double) raw (params::eqBellDb (l, 1));
        set.b2Hz     = (double) raw (params::eqBellHz (l, 1));
        set.b2Q      = (double) raw (params::eqBellQ (l, 1));
        set.b3On     = raw (params::eqB3On (l)) > 0.5f;
        set.b3Db     = (double) raw (params::eqBellDb (l, 2));
        set.b3Hz     = (double) raw (params::eqBellHz (l, 2));
        set.b3Q      = (double) raw (params::eqBellQ (l, 2));
        set.hiDb     = (double) raw (params::eqHiDb (l));
        set.hiHz     = (double) raw (params::eqHiHz (l));
        set.lpfOn    = raw (params::eqLpfOn (l)) > 0.5f;
        set.lpfHz    = (double) raw (params::eqLpfHz (l));
        set.lpfSlope = slope (raw (params::eqLpfSlope (l)));

        eqDisplay[(size_t) l].setSettings (set);
    }

    // The thumbs' spectrum readers sip their taps — only for links that are ON, since only their
    // thumbs draw it.
    for (int l = 0; l < params::numEqLinks; ++l)
    {
        if (amp.apvts.getRawParameterValue (params::eqOn (l))->load() > 0.5f)
        {
            int order = 0;
            auto& pane = eqSpecPane[(size_t) l];

            if (amp.eqSpectrumTap[(size_t) l].tryPull (pane.frameInput(), order)
                 && order == AmpProcessor::eqSpectrumOrder)
                pane.ingest (order);
            else
                pane.starve();
        }
    }

    // The power amp recolours with what is loaded into it, the same truth-telling the block does.
    {
        const bool captured = juce::roundToInt (
            amp.apvts.getRawParameterValue (params::powerType)->load()) > 0;
        thumbs[(size_t) ChainLink::power]->accent = captured ? theme::orange : theme::violet;
    }

    for (auto& t : thumbs)
        if (t != nullptr)
            t->repaint();
}

void ChainStrip::resized()
{
    // Weighted lanes: a service link takes half a tone link's width, and the unit falls out of
    // whatever the weights add up to.
    float units = 0.0f;
    int   present = 0;
    for (int i = 0; i < numChainLinks; ++i)
        if (thumbs[(size_t) i] != nullptr)
        {
            units += chainLinkWeight ((ChainLink) i);
            ++present;
        }

    const float unit = ((float) getWidth() - (float) thumbGap * (present - 1)) / units;

    float x = 0.0f;
    for (int i = 0; i < numChainLinks; ++i)
    {
        if (thumbs[(size_t) i] == nullptr)
            continue;

        const float w = unit * chainLinkWeight ((ChainLink) i);
        thumbs[(size_t) i]->setBounds (juce::Rectangle<float> (x, 0.0f, w, (float) getHeight()).toNearestInt());
        x += w + (float) thumbGap;
    }
}

void ChainStrip::paint (juce::Graphics& g)
{
    // The flow marks in the gaps — the strip is a signal path, not a row of tiles.
    g.setColour (theme::txFaint);

    const Thumb* prev = nullptr;
    for (int i = 0; i < numChainLinks; ++i)
    {
        if (thumbs[(size_t) i] == nullptr)
            continue;

        const auto* curr = thumbs[(size_t) i].get();
        if (prev == nullptr)
        {
            prev = curr;
            continue;
        }

        const auto left  = prev->getBounds();
        const auto right = curr->getBounds();
        prev = curr;

        const float cx = (float) (left.getRight() + right.getX()) * 0.5f;
        const float cy = (float) getHeight() * 0.5f;

        juce::Path arrow;
        arrow.addTriangle (cx - 2.0f, cy - 3.5f, cx - 2.0f, cy + 3.5f, cx + 2.5f, cy);
        g.fillPath (arrow);
    }
}

} // namespace orbitamp
