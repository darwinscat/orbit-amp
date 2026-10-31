// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "BassControls.h"
#include "BlockFrame.h"
#include "GuardRail.h"
#include "MeterRail.h"
#include "scope/TransferView.h"
#include "../PluginProcessor.h"
#include "../core/BassComp.h"

#include <vector>

namespace orbitamp
{

/** COMP'S PICTURE: the level running right to left over the last 2.5 s — what goes in dim grey, what comes out bright
    over it, so the squeeze is the grey left showing — the squeeze itself falling from the top as a violet flood, and
    the THRESHOLD across it, a lilac line with an orange handle carrying its number.

    THE LINE AND THE SLIDER ARE ONE CONTROL: dragging the line moves COMP's parameter to the knob whose threshold the
    line is (core::BassComp::amountForThreshold), through the parameter's attachment, one gesture a drag; the slider
    moved, the line follows. A right click is COMP's menu.

    It reads the processor's taps (core::CompTap) on its own timer, and only while it is on screen — the audio thread
    writes them only then — and it freezes in silence: the tap writes no column of it. Every number on it is 13 px. */
class CompGraph final : public juce::Component,
                        private juce::Timer
{
public:
    explicit CompGraph (AmpProcessor& processor)
        : amp (processor),
          amountP (*processor.apvts.getParameter (params::compAmount)),
          characterP (*processor.apvts.getParameter (params::compCharacter)),
          cols ((size_t) columnsShown)
    {
        amountAtt    = std::make_unique<juce::ParameterAttachment> (amountP,    [this] (float) { repaint(); });
        characterAtt = std::make_unique<juce::ParameterAttachment> (characterP, [this] (float) { repaint(); });
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    }

    ~CompGraph() override { watch (false); }

    static constexpr double topDb = 0.0, bottomDb = -48.0;   // the level's scale
    static constexpr double grRangeDb = 48.0;                // the squeeze's, from the top: the level's own scale
    static constexpr double spanSeconds = 2.5;
    static constexpr int    columnsShown = (int) (spanSeconds / core::CompTap::hopSeconds + 0.5);
    static constexpr float  textHeight = 13.0f, handleW = 64.0f;

    std::function<void (juce::Point<int> screenPos)> onMenu;

    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (1.0f); }

    float yOfDb (double db) const
    {
        const auto r = plot();
        return r.getY() + (float) ((topDb - db) / (topDb - bottomDb)) * r.getHeight();
    }

    double dbAtY (float y) const
    {
        const auto r = plot();
        return topDb - (double) ((y - r.getY()) / r.getHeight()) * (topDb - bottomDb);
    }

    core::BassComp::Character character() const
    {
        return characterP.getValue() > 0.5f ? core::BassComp::Character::punchy : core::BassComp::Character::smooth;
    }

    /** Where the line stands: the detector's threshold at the knob as it is, dB. */
    double thresholdDb() const
    {
        return core::BassComp::thresholdAt ((double) amountP.convertFrom0to1 (amountP.getValue()), character());
    }

    /** The line taken, dragged to `y`, let go: ONE gesture on COMP's parameter. */
    void beginLineDrag()
    {
        amountAtt->beginGesture();
        dragging = true;
        repaint();
    }

    void dragLineTo (float y)
    {
        amountAtt->setValueAsPartOfGesture ((float) core::BassComp::amountForThreshold (dbAtY (y), character()));
    }

    void endLineDrag()
    {
        if (dragging)
            amountAtt->endGesture();
        dragging = false;
        repaint();
    }

    /** What the taps hold, newest last — the timer's work, and a picture's made off screen. */
    void pull()
    {
        shown = amp.compTap.read (cols.data(), (int) cols.size());
        if (const auto w = amp.compTap.written(); w != lastWritten)
        {
            lastWritten = w;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = plot();
        g.setColour (well);
        g.fillRoundedRectangle (r, 3.0f);

        g.setColour (theme::hair);
        for (double db = -12.0; db > bottomDb; db -= 12.0)
            g.fillRect (r.getX(), yOfDb (db), r.getWidth(), 1.0f);

        // The columns, the newest at the right edge; a pixel shows the loudest of the columns it covers.
        const int w = juce::jmax (1, (int) r.getWidth());
        float lastGr = 0.0f;
        if (shown > 0)
        {
            lastGr = cols[(size_t) shown - 1].gr;
            for (int x = 0; x < w; ++x)
            {
                const int a = x * columnsShown / w, b = juce::jmax (a + 1, (x + 1) * columnsShown / w);
                float in = 0.0f, out = 0.0f, gr = 0.0f;
                bool  any = false;
                for (int j = a; j < b; ++j)
                    if (const int k = j - (columnsShown - shown); k >= 0)
                    {
                        const auto& c = cols[(size_t) k];
                        in  = juce::jmax (in, c.in);
                        out = juce::jmax (out, c.out);
                        gr  = juce::jmin (gr, c.gr);
                        any = true;
                    }
                if (! any)
                    continue;

                // The squeeze first, a violet flood from the top; the levels over it, solid, so the flood reads where
                // the playing does not reach and the grey band between in and out is the squeeze heard.
                const float px = r.getX() + (float) x;
                const float yIn = juce::jmax (r.getY(), yOfDb (dbOf (in))), yOut = juce::jmax (r.getY(), yOfDb (dbOf (out)));
                if (gr < -0.05f)
                {
                    g.setColour (theme::violet.withAlpha (0.55f));
                    g.fillRect (px, r.getY(), 1.0f, juce::jmin (1.0f, (float) (-gr / grRangeDb)) * r.getHeight());
                }
                g.setColour (levelIn);                                                  // in, dim grey
                g.fillRect (px, yIn, 1.0f, r.getBottom() - yIn);
                g.setColour (levelOut);                                                 // out, bright over it
                g.fillRect (px, yOut, 1.0f, r.getBottom() - yOut);
            }
        }

        // The squeeze now, in the top right corner.
        g.setColour (theme::tx);
        theme::drawTracked (g, dbText (lastGr < -0.05f ? (double) lastGr : 0.0), r.reduced (6.0f, 3.0f).removeFromTop (textHeight + 4.0f),
                            theme::displayFont (textHeight), 0.02f, juce::Justification::centredRight);

        // THE THRESHOLD: the lilac line, the orange handle at its left end with its number.
        const float yT = juce::jlimit (r.getY() + metersHalfGrip(), r.getBottom() - metersHalfGrip(), yOfDb (thresholdDb()));
        g.setColour (theme::lilac);
        g.fillRect (r.getX(), yT - 0.75f, r.getWidth(), 1.5f);
        const juce::Rectangle<float> track (r.getX() + 3.0f, r.getY(), handleW, r.getHeight());
        g.setColour (well);
        g.fillRoundedRectangle (juce::Rectangle<float> (track.getX(), yT - meterrail::gripH * 0.5f, handleW, meterrail::gripH), 2.5f);
        meterrail::paintGrip (g, track, yT, dbText (thresholdDb()), theme::orange, dragging, textHeight);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            if (onMenu != nullptr)
                onMenu (e.getScreenPosition());
            return;
        }
        grab = yOfDb (thresholdDb()) - (float) e.y;   // the line moves with the hand, from where it was taken
        beginLineDrag();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging)
            dragLineTo ((float) e.y + grab);
    }

    void mouseUp (const juce::MouseEvent&) override { endLineDrag(); }

    void visibilityChanged() override      { watch (isShowing()); }
    void parentHierarchyChanged() override { watch (isShowing()); }

private:
    static float metersHalfGrip() { return meterrail::gripH * 0.5f; }
    static double dbOf (float peak) { return peak > 1.0e-6f ? 20.0 * std::log10 ((double) peak) : -120.0; }
    static juce::String dbText (double db) { return juce::String (juce::roundToInt (db)) + " dB"; }

    void watch (bool on)
    {
        amp.compWatched.store (on, std::memory_order_relaxed);
        if (on)
            startTimerHz (30);
        else
            stopTimer();
    }

    void timerCallback() override { pull(); }

    inline static const juce::Colour well { 0xff121218 }, levelIn { 0xff3b3b46 }, levelOut { 0xffc9c9d6 };

    AmpProcessor& amp;
    juce::RangedAudioParameter& amountP;
    juce::RangedAudioParameter& characterP;
    std::unique_ptr<juce::ParameterAttachment> amountAtt, characterAtt;
    std::vector<core::CompTap::Column> cols;
    int shown = 0;
    std::uint32_t lastWritten = 0;
    bool  dragging = false;
    float grab = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompGraph)
};

/** COMP'S RATIO, an IN/OUT square beside the picture (the TRANSFER painter's square, unity and stroke): the
    compressor's own curve in dB, -48..0 each way — the knee at the threshold, a lilac dot there, the upper segment's
    slope the ratio — and the ratio written in its corner. A drag up or down sets the ratio the hand HOLDS
    (params::compRatio, compRatioHeld on); until then it follows the slider's law. A right click is COMP's menu. */
class CompTransfer final : public juce::Component
{
public:
    explicit CompTransfer (AmpProcessor& processor)
        : amountP (*processor.apvts.getParameter (params::compAmount)),
          characterP (*processor.apvts.getParameter (params::compCharacter)),
          ratioP (*processor.apvts.getParameter (params::compRatio)),
          heldP (*processor.apvts.getParameter (params::compRatioHeld))
    {
        for (auto* p : { &amountP, &characterP, &ratioP, &heldP })
            atts.push_back (std::make_unique<juce::ParameterAttachment> (*p, [this] (float) { repaint(); }));
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    }

    std::function<void (juce::Point<int> screenPos)> onMenu;

    static constexpr double lowDb = -48.0;

    bool held() const { return heldP.getValue() > 0.5f; }

    double thresholdDb() const
    {
        const auto c = characterP.getValue() > 0.5f ? core::BassComp::Character::punchy : core::BassComp::Character::smooth;
        return core::BassComp::thresholdAt ((double) amountP.convertFrom0to1 (amountP.getValue()), c);
    }

    /** The ratio the curve is drawn at: the hand's when it holds one, the law's at the knob otherwise. */
    double ratio() const
    {
        return held() ? (double) ratioP.convertFrom0to1 (ratioP.getValue())
                      : core::BassComp::lawAt ((double) amountP.convertFrom0to1 (amountP.getValue())).ratio;
    }

    juce::Rectangle<float> box() const { return scope::TransferView::square (getLocalBounds().toFloat().reduced (1.0f)); }

    void paint (juce::Graphics& g) override
    {
        const auto b = box();
        g.setColour (juce::Colour (0xff121218));
        g.fillRoundedRectangle (b, 3.0f);
        scope::TransferView::paintUnity (g, b);

        const double t = thresholdDb(), r = ratio();
        juce::Path p;
        for (int i = 0; i <= 96; ++i)
        {
            const double in = lowDb + (double) i * (-lowDb / 96.0);
            const juce::Point<float> at { xOf (in, b), yOf (curve (in, t, r), b) };
            if (i == 0)
                p.startNewSubPath (at);
            else
                p.lineTo (at);
        }
        scope::TransferView::strokeCurve (g, p);

        g.setColour (theme::lilac);
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ xOf (t, b), yOf (t, b) }));

        g.setColour (held() ? theme::orange : theme::tx);
        theme::drawTracked (g, juce::String (r, 1) + ":1", b.reduced (6.0f, 3.0f).removeFromTop (17.0f),
                            theme::displayFont (13.0f), 0.02f, juce::Justification::centredLeft);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            if (onMenu != nullptr)
                onMenu (e.getScreenPosition());
            return;
        }
        heldP.beginChangeGesture();
        heldP.setValueNotifyingHost (1.0f);
        heldP.endChangeGesture();
        ratioP.beginChangeGesture();
        dragging = true;
        mouseDrag (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging)
            return;
        // The upper segment's end, where the input reaches 0 dB, follows the hand: its height says the slope.
        const auto   b   = box();
        const double t   = thresholdDb();
        const double out = juce::jlimit (t + 0.05, 0.0, (double) (lowDb * (e.position.y - b.getY()) / b.getHeight()));
        const double r   = juce::jlimit ((double) params::compRatioMin, (double) params::compRatioMax, -t / (out - t));
        ratioP.setValueNotifyingHost (ratioP.convertTo0to1 ((float) r));
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging)
            ratioP.endChangeGesture();
        dragging = false;
    }

private:
    static float xOf (double db, juce::Rectangle<float> b) { return b.getX() + (float) ((db - lowDb) / -lowDb) * b.getWidth(); }
    static float yOf (double db, juce::Rectangle<float> b) { return b.getBottom() - (float) ((db - lowDb) / -lowDb) * b.getHeight(); }

    /** The compressor's static curve with core's soft knee (core::BassComp::kneeDb wide). */
    static double curve (double in, double t, double r)
    {
        const double w = core::BassComp::kneeDb, over = in - t;
        if (2.0 * over < -w)
            return in;
        if (2.0 * std::abs (over) <= w)
            return in + (1.0 / r - 1.0) * (over + w * 0.5) * (over + w * 0.5) / (2.0 * w);
        return t + over / r;
    }

    juce::RangedAudioParameter& amountP;
    juce::RangedAudioParameter& characterP;
    juce::RangedAudioParameter& ratioP;
    juce::RangedAudioParameter& heldP;
    std::vector<std::unique_ptr<juce::ParameterAttachment>> atts;
    bool dragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompTransfer)
};

/** THE BASS'S COMP, A BLOCK OF ITS OWN (the owner, 31.10): after the GATE and before OCTAVE on the panel, as in the
    strip. Inside, left to right: its SLIDER (a rail over COMP's one knob, 0..1, the law's threshold -12..-36 and ratio
    2:1..6:1), its PICTURE with the threshold line — the slider seen another way — and its RATIO square. ATTACK,
    RELEASE, CHARACTER and the ratio given back to the slider are its menu, on a right click anywhere on it. Our own
    DSP, so violet; its arrow in the strip switches it, and standing by it dims in place, still in reach. */
class CompBlock final : public BlockFrame
{
public:
    explicit CompBlock (AmpProcessor& processor)
        : BlockFrame ("COMP", Kind::dsp),
          state (processor.apvts),
          rail (*processor.apvts.getParameter (params::compAmount), *processor.apvts.getParameter (params::compOn), 0.0f, 1.0f, false),
          graph (processor),
          transfer (processor)
    {
        shieldsWhenOff = false;
        for (auto* c : { (juce::Component*) &rail, (juce::Component*) &graph, (juce::Component*) &transfer })
            addAndMakeVisible (*c);
        const auto menu = [this] (juce::Point<int>) { bass::showCompMenu (state, this); };
        rail.onMenu = graph.onMenu = transfer.onMenu = menu;
        attachPower (*processor.apvts.getParameter (params::compOn));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            bass::showCompMenu (state, this);
    }

    CompGraph&    picture() noexcept { return graph; }
    CompTransfer& square() noexcept  { return transfer; }

protected:
    void layOutContent (juce::Rectangle<int> r) override
    {
        rail.setBounds (r.removeFromLeft (GuardRail::designWidth));
        r.removeFromLeft (gap);
        const int side = juce::jmin (r.getHeight(), squareMax);
        transfer.setBounds (r.removeFromRight (side).withSizeKeepingCentre (side, side));
        r.removeFromRight (gap);
        graph.setBounds (r);
    }

private:
    static constexpr int gap = 10, squareMax = 118;

    juce::AudioProcessorValueTreeState& state;
    GuardRail    rail;
    CompGraph    graph;
    CompTransfer transfer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompBlock)
};

} // namespace orbitamp
