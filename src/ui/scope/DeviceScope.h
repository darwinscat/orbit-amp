// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../../core/ScopeTap.h"
#include "../../core/WaveRibbon.h"
#include "../Theme.h"
#include "EnvelopeView.h"
#include "PhotoView.h"
#include "ShapeView.h"
#include <felitronics/analysis/MultiResSpectrumPaneFast.h>
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
    enum class Mode { shape, envelope, transfer, tone, wave, device, photo };

    /** WAVE only: the half-wave silhouette instead of the mirrored band. */
    bool waveHalf = false;

    /** TONE only: the family spectrum pane, fed from a post-block tap — the EQ's grammar. The
        pull is gated on being on screen, so a hidden twin never steals the mailbox's frames. */
    void setSpectrumTap (const felitronics::analysis::RollingSpectrumTap* tap, int order)
    {
        specTap   = tap;

        if (order == specOrder)
            return;

        specOrder = order;

        // THE ANALYSER FOLLOWS THE ROOM. A tile shows one fixed window because six panes at a
        // constant-Q reading would cost the face fifteen times what the classic ones do — and the
        // eye cannot spend it there anyway. A picture that owns the face is ONE pane, looked at,
        // and it gets the analyser a log axis actually wants: several window lengths at once, read
        // in bands of a twenty-fourth of an octave, so the bottom two decades stop being three bins
        // in a row.
        if (order >= felitronics::analysis::MultiResSpectrumPaneFast::kMaxOrder)
        {
            const int tiers[] = { 14, 12, 10 };
            multi.setTiers (tiers, 3);
        }
    }

    /** Whether the reading is the constant-Q one. The tile's fixed window is the other. */
    bool readsInBands() const noexcept
    {
        return specOrder >= felitronics::analysis::MultiResSpectrumPaneFast::kMaxOrder;
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

    const felitronics::appkit::DeviceSpec& deviceSpec() const noexcept { return spec; }

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

    const juce::StringArray& paper() const noexcept { return info; }

    /** Provenance — who made this capture, and one day off what box, in what year. The CARD says
        it; the tile does not, because the tile has four lines and they are spoken for. This is the
        difference between the two pages made concrete: one is what the device IS, the other adds
        where it came from. */
    void setCredit (juce::StringArray lines)
    {
        if (lines != credit)
        {
            credit = std::move (lines);
            repaint();
        }
    }

    const juce::StringArray& creditLines() const noexcept { return credit; }

    /** The photograph the pack ships of the box, decoded — invalid when it ships none, which is
        what makes the PHOTO page step out of the loop rather than stand empty. */
    void setPicture (juce::Image img)
    {
        photoView.setPicture (std::move (img));
        repaint();
    }

    const juce::Image& picture() const noexcept { return photoView.picture(); }
    bool hasPicture() const noexcept { return photoView.has(); }

    /** Whether a scope THIS SIZE draws DEVICE and PHOTO as one card rather than two pages. The
        block asks, because where they are one page the loop must stop on them once, not twice. */
    bool pairsAsCard() const { return canCard (getLocalBounds().toFloat().reduced (6.0f)); }

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
                case Mode::tone:
                    if (specTap == nullptr)
                        toneView.paint (g, area, toneDb, frame, (felitronics::analysis::SpectrumPane*) nullptr,
                                        sampleRateNow());
                    else if (readsInBands())
                        toneView.paint (g, area, toneDb, frame, &multi, sampleRateNow());
                    else
                        toneView.paint (g, area, toneDb, frame, &pane, sampleRateNow());
                    break;
                case Mode::wave:     waveView.paint (g, area, ribbon, waveHalf); break;
                // ONE PAIR, two doors. Small, they are two pages because the tile cannot hold
                // both; big, they are the card they always were.
                case Mode::device:
                case Mode::photo:    paintBox (g, area); break;
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

    /** The constant-Q reading, for the picture that owns the face. Its FFT plans are built once,
        when the room first asks for it — never per tick.

        The FAST sibling: the same pane, computed differently. A profile of the original found 44%
        of a tick inside libm and almost none of it in the arithmetic anyone would have guessed —
        the peak trace was kept in decibels, which cost a log10 per bin to compare and an exp per
        bin to undo, and the fill and the peak each rebuilt the same column geometry. Holding the
        peak in power, deriving that geometry once, and peeling DC and Nyquist out of the loop take
        it to roughly half the cost, with the fill bit-identical to the pane it replaces. */
    felitronics::analysis::MultiResSpectrumPaneFast multi;

    /** DEVICE and PHOTO, resolved against the room they were given.

        In the tile they are two pages, because 106 points cannot hold a photograph and four lines
        of paper at any size worth reading. Thrown open or on the monitor they are ONE page — the
        card — because there the split has nothing left to justify it and both halves would be
        floating in black. A pack with no photograph lands on the paper either way. */
    void paintBox (juce::Graphics& g, juce::Rectangle<float> r)
    {
        if (canCard (r))
        {
            paintCard (g, r);
            return;
        }

        if (mode == Mode::photo)
            photoView.paint (g, r);
        else
            paintPaper (g, r, true);
    }

    /** The box and its paper together. The photograph keeps the room and the paper takes what is
        left over — the column beside it when the room is wider than the picture wants, the strip
        under it when it is not. The picture leads because it is the thing you recognise; the paper
        is what you read once you have. */
    void paintCard (juce::Graphics& g, juce::Rectangle<float> r)
    {
        if (! photoView.has())
        {
            paintPaper (g, r, false);
            return;
        }

        const float wants = photoWidthAt (r.getHeight());

        if (r.getWidth() - wants - cardGap - dividerWidth (r) >= paperMinW)
        {
            // THE CIRCUIT IS THE DIVIDER. Standing at the top of the text column the symbols were a
            // stray badge in the gap between the box and its words; stacked between them they are
            // structure — and a column of four diodes is what a Big Muff's clipping section
            // actually looks like on paper. No rule needed beside them: the parts are the rule.
            //
            // The PAIR is centred as one thing, not each piece in its own share of the width.
            // Fitting the picture inside everything-but-the-column and then centring the text
            // inside the column pushes the two apart and opens a hole down the middle.
            const bool  tight = r.getHeight() < paperHeight (false);
            const auto  lines = tight ? info : cardLines();
            const int   n     = partCount (spec);
            const float divW  = dividerWidth (r);
            const float colW  = juce::jmin (r.getWidth() - wants - cardGap - divW, paperMaxW);

            auto pair = r.withSizeKeepingCentre (wants + cardGap + divW + colW, r.getHeight());

            photoView.paint (g, pair.removeFromLeft (wants));
            pair.removeFromLeft (cardGap);

            if (n > 0)
            {
                const float side = partSide (n, pair.getHeight() * 0.8f, tight ? glyphBig * 0.5f : glyphBig);
                auto stack = juce::Rectangle<float> (side, side * (float) n)
                               .withCentre (pair.removeFromLeft (side).getCentre());
                drawParts (g, stack, spec, true, side);
                pair.removeFromLeft (cardGap);
            }

            float text = 0.0f;
            for (int i = 0; i < lines.size(); ++i)
                text += i == 0 ? 22.0f : 17.0f;

            paintLines (g, pair.withSizeKeepingCentre (pair.getWidth(),
                                                       juce::jmin (pair.getHeight(), text)), lines);
            return;
        }

        paintPaper (g, r.removeFromBottom (paperHeight (false)), false);
        photoView.paint (g, r.withTrimmedBottom (cardGap));
    }

    /** The spec counted as PARTS. appkit's `deviceSpecCount` collapses every entry to one symbol —
        «bjt,diode:4» comes out as two — while the header above it promises «4 tubes for a V4, one
        for a Volt». Implementation and documentation disagree there; this file believes the
        documentation, and does its own drawing rather than change a function OrbitCab also calls.

        Four diodes are four diodes: that is what the line under it says in words, and a picture
        that says two beside a sentence that says four is a picture nobody trusts again. */
    static int partCount (const felitronics::appkit::DeviceSpec& spec)
    {
        int n = 0;

        for (const auto& [type, count] : spec)
            if (type != felitronics::appkit::DeviceType::none && count > 0)
                n = juce::jmin (felitronics::appkit::kMaxDeviceGlyphs, n + count);

        return n;
    }

    /** The parts in a row or a column, each stroked in its family's colour. */
    static void drawParts (juce::Graphics& g, juce::Rectangle<float> area,
                           const felitronics::appkit::DeviceSpec& spec, bool vertical, float side)
    {
        int drawn = 0;

        for (const auto& [type, count] : spec)
        {
            if (type == felitronics::appkit::DeviceType::none || count <= 0)
                continue;

            for (int i = 0; i < count && drawn < felitronics::appkit::kMaxDeviceGlyphs; ++i, ++drawn)
            {
                const auto cell = vertical ? area.removeFromTop (side) : area.removeFromLeft (side);
                felitronics::appkit::drawDeviceGlyph (g, cell.reduced (side * 0.12f), type,
                                                      felitronics::appkit::deviceStroke (type));
            }
        }
    }

    /** How big one part may be drawn so the whole run fits the space it was given. */
    static float partSide (int n, float room, float cap)
    {
        return n > 0 ? juce::jlimit (9.0f, cap, juce::jmin (cap, room / (float) n)) : cap;
    }

    /** Whether the pair FITS here — which is the honest question, and it is not «is this tall».

        A block alone on its row gets a tile 740 points wide and 106 tall: no height at all, and
        more width than anything on the face. Judged by height it stayed two pages, and the DEVICE
        one was four short lines floating in the middle of a black field the size of a postcard.
        Judged by whether the box and its paper can stand side by side, it is a card — a 106-point
        photograph costs nothing there and turns the emptiest thing on the face into the one that
        says at a glance what is playing.

        Three rooms, one sentence: the narrow tile cannot seat the pair either way and stays split;
        the wide tile seats them side by side; a thrown-open block seats them stacked. */
    bool canCard (juce::Rectangle<float> r) const
    {
        // Nothing to stand beside the paper: it is a page of its own only where it reads as one.
        if (! photoView.has())
            return r.getHeight() >= paperPageMin;


        // Side by side. The paper may have to be the SHORT version of itself here — a lone block
        // gets a tile that is all width and no height — so what must fit is the compact one.
        if (r.getWidth() - photoWidthAt (r.getHeight()) - cardGap - dividerWidth (r) >= paperMinW
            && r.getHeight() >= paperHeight (true))
            return true;

        return r.getHeight() >= paperHeight (false) + cardGap + photoMinH;
    }

    /** What the standing circuit takes across — its own width plus the gap after it, or nothing
        when the pack states no parts. */
    float dividerWidth (juce::Rectangle<float> r) const
    {
        const int n = partCount (spec);

        if (n <= 0)
            return 0.0f;

        const bool tight = r.getHeight() < paperHeight (false);
        return partSide (n, r.getHeight() * 0.8f, tight ? glyphBig * 0.5f : glyphBig) + cardGap;
    }

    /** What the picture wants across, given all the height there is. */
    float photoWidthAt (float height) const
    {
        const auto& src = photoView.picture();

        return src.getHeight() > 0 ? height * (float) src.getWidth() / (float) src.getHeight()
                                   : height;
    }

    /** What the paper needs on the card, so a layout can give it exactly that and no more. */
    /** What the paper needs. COMPACT is the short version of itself — symbols at half size on the
        line the corner glyphs already own, and the specification alone; FULL adds the provenance
        and reads the circuit at 46. A card in a room with no height wears the short one: 99 points
        against 163, which is the difference between fitting in a 106-point tile and printing the
        last two lines on top of each other. */
    float paperHeight (bool compact) const
    {
        const auto lines = compact ? info : cardLines();

        float h = partCount (spec) > 0
                    ? (compact ? glyphBig * 0.5f + 3.0f : glyphBig + 10.0f) : 0.0f;

        for (int i = 0; i < lines.size(); ++i)
            h += i == 0 ? 22.0f : 17.0f;

        return h;
    }

    /** The facts, one per line, name first because that is what you came to check.

        Each line is FITTED to the width it was given. «11 CAPTURES ON THE DIAL» already reaches
        both walls of the tile at twelve points, so the next device with a longer name would have
        run straight off the sides — and a centred line that overflows loses both ends at once.
        Tracking is a fraction of the height, so the width a line wants is linear in it and the fit
        is one division. Only the line that needs it shrinks; the rest keep their size. */
    void paintLines (juce::Graphics& g, juce::Rectangle<float> r, const juce::StringArray& lines)
    {
        for (int i = 0; i < lines.size(); ++i)
        {
            const bool  head  = i == 0;
            const float size  = head ? 15.0f : 12.0f;
            const float track = head ? 0.06f : 0.04f;
            const float floorSize = head ? 11.0f : 9.0f;

            const float wants = theme::trackedWidth (lines[i], theme::displayFont (size), track);
            const float use   = wants > r.getWidth() && wants > 0.0f
                                  ? juce::jmax (floorSize, size * r.getWidth() / wants)
                                  : size;

            g.setColour (head ? theme::tx : theme::txDim);
            theme::drawTracked (g, lines[i], r.removeFromTop (head ? 22.0f : 17.0f),
                                theme::displayFont (use), track, juce::Justification::centred);
        }
    }

    /** The card says everything; the tile says the first four things. */
    juce::StringArray cardLines() const
    {
        juce::StringArray all (info);

        if (! credit.isEmpty())
        {
            all.add ({});          // a breath: provenance is a different kind of fact
            all.addArray (credit);
        }

        return all;
    }

    /** The paper. The circuit's symbols at a size you can actually read them at, and under them
        the plain facts — name first, because that is what you came to check.

        CENTRED in whatever it is handed, rather than started from the top: on the card it is a
        column beside a picture, and a block of text hanging from the ceiling of its column reads
        as a mistake. */
    void paintPaper (juce::Graphics& g, juce::Rectangle<float> r, bool compact)
    {
        const int n = partCount (spec);

        // THE TILE. The top line is already spent: the fold arrows and the whole-screen brackets
        // stand in it, at the right. So the symbols move INTO that line at its left, and the row
        // they used to own — 46 points plus its gap, more than all four lines of text together —
        // comes back to the text. That row was what pushed the last two lines off the bottom to
        // print on top of each other.
        //
        // Half size rather than a quarter: small enough to be a badge beside the corner glyphs,
        // big enough that a transistor still reads as a transistor and two of them read as two.
        if (compact)
        {
            if (n > 0)
            {
                // The corner glyphs own the right end of this line; the parts take the left, and
                // shrink rather than run into them when a device has many.
                const float side = partSide (n, r.getWidth() * 0.5f, glyphBig * 0.5f);
                const juce::Rectangle<float> row (r.getX(), r.getY() + cornerMid - side * 0.5f,
                                                  side * (float) n, side);
                drawParts (g, row, spec, false, side);
                r = r.withTop (row.getBottom() + 3.0f);
            }

            // Up against the symbols, not floating in the middle: the block reads as one thing
            // hanging off the top line, and the slack it leaves goes to the bottom where the last
            // line was pressed against the wall.
            paintLines (g, r, info);
            return;
        }

        // THE CARD. Room enough for the circuit at reading size, the whole block centred in the
        // column beside the picture — a block of text hanging from the ceiling of its column reads
        // as a mistake.
        r = r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), paperHeight (false)));

        if (n > 0)
        {
            const float side = partSide (n, r.getWidth() * 0.8f, glyphBig);
            auto row = r.removeFromTop (side).withSizeKeepingCentre (side * (float) n, side);
            drawParts (g, row, spec, false, side);
            r.removeFromTop (10.0f);
        }

        paintLines (g, r, cardLines());
    }

    void timerCallback() override
    {
        if (specTap != nullptr && mode == Mode::tone && isShowing())
        {
            auto* tap = const_cast<felitronics::analysis::RollingSpectrumTap*> (specTap);
            int order = 0;

            if (readsInBands())
            {
                if (tap->tryPull (multi.frameInput(), order) && order == specOrder)
                    multi.ingest (order);
                else
                    multi.starve();
            }
            else if (tap->tryPull (pane.frameInput(), order) && order == specOrder)
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

    static constexpr float glyphBig  = 46.0f;   // the paper's own symbols, at reading size
    static constexpr float paperMinW = 150.0f;  // narrower than this and the facts wrap into rubble
    static constexpr float paperMaxW = 320.0f;  // ...and wider than this the picture is being robbed
    static constexpr float cardGap   = 14.0f;
    static constexpr float cornerMid = 6.0f;    // where the corner glyphs' line runs, in this area
    static constexpr float photoMinH = 120.0f;  // under this a stacked picture is a stamp, not a portrait
    static constexpr float paperPageMin = 200.0f;   // paper alone, big enough to read as a page

    const core::ScopeTap& tap;
    core::WaveRibbon& ribbon;
    std::function<double (double)> toneDb;
    felitronics::appkit::DeviceSpec spec;
    juce::StringArray info, credit;

    scope::ToneView  toneView;
    scope::WaveView  waveView;
    scope::PhotoView photoView;

    std::array<float, core::ScopeTap::size> dry {}, wet {};
    Mode mode = Mode::shape;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeviceScope)
};

} // namespace orbitamp
