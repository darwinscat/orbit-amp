// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../../core/ScopeTap.h"
#include "../../core/WaveRibbon.h"
#include "../Theme.h"
#include "EnvelopeView.h"
#include "ShapeView.h"
#include <felitronics/analysis/RollingSpectrumTap.h>
#include <felitronics/analysis/SpectrumPane.h>
#include "ToneView.h"
#include "TransferView.h"
#include "WaveView.h"

#include <felitronics/appkit/DeviceGlyph.h>

#include <array>

namespace orbitamp
{

/** What a captured device does, drawn several ways.

    The WELL, not the drawing: it owns the recess, the mode, the frame rate, the glyph row, and
    reading the tap. Each way of looking lives in its own file beside this one and is handed a
    rectangle and a window of audio — none of them knows it is in a plugin, and none of them can
    decide when to read.

    Nothing here is about the boost. Every captured block gets the same well and the same ways of
    looking, because the question is always the same one: what did this thing do to what went in.
    Which of them a block offers is the block's business — it owns the selector.

    No axes and no numbers on any of them. This is the picture on a knob that reads 1 to 10: it
    exists so you can see what bends, not so you can measure it. The one exception is the tone curve,
    which is a measurement and says so by being drawn against the band it is trusted in. */
class DeviceScope final : public juce::Component,
                         private juce::Timer
{
public:
    enum class Mode { shape, envelope, transfer, tone, wave, device };

    /** WAVE only: the half-wave silhouette instead of the mirrored band. */
    bool waveHalf = false;

    /** TONE only: the family spectrum pane, fed from a post-block tap — the EQ's grammar. The
        pull is gated on being on screen, so a hidden twin never steals the mailbox's frames. */
    void setSpectrumTap (const felitronics::analysis::RollingSpectrumTap* tap, int order)
    {
        specTap   = tap;
        specOrder = order;
    }

    explicit DeviceScope (const core::ScopeTap& source, core::WaveRibbon& ribbonSource,
                         std::function<double (double)> toneDbAt)
        : tap (source), ribbon (ribbonSource), toneDb (std::move (toneDbAt))
    {
        setInterceptsMouseClicks (false, false);
        startTimerHz (60);   // the ribbon slides; 24 reads as a slideshow
    }

    void setMode (Mode m)
    {
        if (m != mode)
        {
            mode = m;
            repaint();
        }
    }

    Mode getMode() const noexcept { return mode; }

    /** The rate the tap is being filled at, so a partial lands on the frequency it actually has. */
    void setSampleRate (double newRate)
    {
        rate = newRate > 0.0 ? newRate : 48000.0;
        toneView.setSampleRate (rate);
    }

    double sampleRateNow() const noexcept { return rate; }

    /** What is inside the device — and it belongs to the DEVICE view alone.

        It has been three things now, each honest about a different mistake. A row of its own cost a
        line of height and drew the glyphs too small in it. A badge in the picture's corner sat on a
        scrim that went opaque across a spectrum and hid the thing it was captioning. Turned into a
        watermark it stopped hiding anything and started being visual noise on every view instead —
        a caption repeated five times is not five captions, it is clutter with one meaning.

        The paper page says it once, at a size worth reading, next to the rest of the facts. */
    void setSpec (felitronics::appkit::DeviceSpec s)
    {
        if (s != spec)
        {
            spec = std::move (s);
            repaint();
        }
    }

    /** The device's paper: what it is, what it is made of, how it was captured. Shown by the DEVICE
        view, which is the glyph badge grown into a picture of its own — the badge says the circuit
        in two symbols and there was nowhere to say the rest. */
    void setInfo (juce::StringArray lines)
    {
        if (lines != info)
        {
            info = std::move (lines);
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();

        g.setColour (theme::bezel);
        g.fillRoundedRectangle (r, theme::radiusMd);

        {
            const juce::Graphics::ScopedSaveState clipped (g);
            juce::Path well;
            well.addRoundedRectangle (r, theme::radiusMd);
            g.reduceClipRegion (well);

            // tabby's vignette, same as the EQ's well — TONE only: the measuring view earns
            // the measuring ground; the moving pictures keep their flat black.
            if (mode == Mode::tone)
            {
                juce::ColourGradient vg (theme::bezel.brighter (0.18f),
                                         r.getCentreX(), r.getCentreY() - r.getHeight() * 0.06f,
                                         theme::bezel.darker (0.55f),
                                         r.getX(), r.getBottom(), true);
                g.setGradientFill (vg);
                g.fillRect (r);
            }

            const auto area = r.reduced (6.0f);
            const auto frame = fetch();

            switch (mode)
            {
                case Mode::shape:    scope::ShapeView::paint (g, area, frame); break;
                case Mode::envelope: scope::EnvelopeView::paint (g, area, frame); break;
                case Mode::transfer: scope::TransferView::paint (g, area, frame); break;
                case Mode::tone:     toneView.paint (g, area, toneDb, frame,
                                                     specTap != nullptr ? &pane : nullptr,
                                                     sampleRateNow()); break;
                case Mode::wave:     waveView.paint (g, area, ribbon, waveHalf); break;
                case Mode::device:   paintDevice (g, area); break;
            }

        }

        g.setColour (theme::hair2);
        g.drawRoundedRectangle (r.reduced (0.5f), theme::radiusMd, 1.0f);
    }

private:
    const felitronics::analysis::RollingSpectrumTap* specTap = nullptr;
    int    specOrder = 11;
    double rate      = 48000.0;
    felitronics::analysis::SpectrumPane pane;

    /** The paper. The circuit's symbols at a size you can actually read them at, and under them
        the plain facts — name first, because that is what you came to check. */
    void paintDevice (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const int n = felitronics::appkit::deviceSpecCount (spec);

        if (n > 0)
        {
            constexpr float big = 46.0f;
            auto row = r.removeFromTop (big).withSizeKeepingCentre (big * (float) n, big);
            felitronics::appkit::drawDeviceSpecStatic (g, row, spec);
            r.removeFromTop (10.0f);
        }

        for (int i = 0; i < info.size(); ++i)
        {
            const bool head = i == 0;
            g.setColour (head ? theme::tx : theme::txDim);
            theme::drawTracked (g, info[i], r.removeFromTop (head ? 22.0f : 17.0f),
                                theme::displayFont (head ? 15.0f : 12.0f), head ? 0.06f : 0.04f,
                                juce::Justification::centred);
        }
    }

    void timerCallback() override
    {
        if (specTap != nullptr && mode == Mode::tone && isShowing())
        {
            int order = 0;
            if (const_cast<felitronics::analysis::RollingSpectrumTap*> (specTap)
                    ->tryPull (pane.frameInput(), order) && order == specOrder)
                pane.ingest (order);
            else
                pane.starve();
        }

        repaint();
    }

    /** The tap, copied out once per frame and handed to whichever view is showing. Empty when the tap
        has never been written. */
    scope::Frame fetch()
    {
        if (! tap.read (dry, wet))
            return {};

        return { dry.data(), wet.data(), core::ScopeTap::size };
    }

    static constexpr float glyphSize = 26.0f;   // was 18 in a row of its own, and small there

    const core::ScopeTap& tap;
    core::WaveRibbon& ribbon;
    std::function<double (double)> toneDb;
    felitronics::appkit::DeviceSpec spec;
    juce::StringArray info;

    scope::ToneView toneView;
    scope::WaveView waveView;

    std::array<float, core::ScopeTap::size> dry {}, wet {};
    Mode mode = Mode::shape;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeviceScope)
};

} // namespace orbitamp
