// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "Theme.h"
#include "../device/DeviceLibrary.h"
#include "library/MiniClose.h"

#include <BinaryData.h>

namespace orbitamp
{

/** DEVICES & TRADEMARKS — the long notice, and the list it is about.

    The version window carries the short notice: three sentences saying the names on the faces
    describe equipment rather than claim it. THIS page is the long form, and it exists because the
    long form has a LIST: every device the installed packs name, by maker and model, with what the
    pack says about the particular box — a capture is of one unit on one day, and the year and the
    serial are what tell that unit from every other one wearing the same name.

    The page is cut in half: the text above, the table below, each scrolling on its own, because a
    reader checking a name against what they hear should not lose the paragraph to do it.

    Shipped packs and a player's own are two sections, never one list. What we put in the installer
    is what WE captured and stand behind; what somebody dropped into the Devices folder is theirs,
    and a page about who made what must not blur the two.

    Both texts are files (`resources/notice.txt`, `resources/disclaimer.txt`), embedded like the
    typeface and the cat: a legal sentence in a string literal is one nobody diffs against the README.

    An overlay rather than a desktop window, for the reason every other page here is one: a plugin
    editor is a guest, and hosts reparent, hide and destroy it freely. */
class DisclaimerPanel final : public juce::Component
{
public:
    DisclaimerPanel()
    {
        setWantsKeyboardFocus (true);

        for (auto* v : { &textView, &listView })
        {
            v->setScrollBarsShown (true, false);
            v->setScrollBarThickness (8);
            addAndMakeVisible (*v);
        }

        textView.setViewedComponent (&prose, false);
        listView.setViewedComponent (&table, false);

        closeButton.onClick = [this] { setVisible (false); };
        addAndMakeVisible (closeButton);
    }

    /** Read afresh on every open: a player who drops a pack in and comes straight here should see it
        named, not be told to restart. */
    void open()
    {
        table.rebuild();
        setVisible (true);
        toFront (true);
        grabKeyboardFocus();
        resized();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::ground.withAlpha (0.78f));

        const auto p = panel.toFloat();
        g.setColour (theme::panel);
        g.fillRoundedRectangle (p, theme::radiusLg);
        g.setColour (theme::hair2);
        g.drawRoundedRectangle (p.reduced (0.75f), theme::radiusLg, 1.5f);

        g.setColour (theme::tx);
        theme::drawTracked (g, "DEVICES & TRADEMARKS",
                            panel.reduced (26, 20).removeFromTop (20).toFloat(),
                            theme::displayFont (13.0f), 0.10f, juce::Justification::centredLeft);

        // The seam between the halves, drawn where resized() put it.
        g.setColour (theme::hair2);
        g.fillRect (seam);
    }

    void resized() override
    {
        panel = getLocalBounds().withSizeKeepingCentre (juce::jmin (getWidth()  - 40, panelW),
                                                        juce::jmin (getHeight() - 40, panelH));

        auto r = panel.reduced (26, 20);
        closeButton.setBounds (r.removeFromTop (20).removeFromRight (20));
        r.removeFromTop (14);

        // Half and half: the paragraphs above, the devices below, a rule between them.
        auto top = r.removeFromTop (r.getHeight() / 2);
        prose.setWidth (top.getWidth() - textView.getScrollBarThickness() - 6);
        textView.setBounds (top);

        r.removeFromTop (9);
        seam = r.removeFromTop (1);
        r.removeFromTop (9);

        table.setWidth (r.getWidth() - listView.getScrollBarThickness() - 6);
        listView.setBounds (r);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! panel.contains (e.getPosition()))
            setVisible (false);
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::escapeKey)
        {
            setVisible (false);
            return true;
        }

        return false;
    }

private:
    static constexpr int   panelW = 760;
    static constexpr int   panelH = 620;
    static constexpr float textH  = 13.0f;   // the floor for anything read at 1x — see CLAUDE.md

    /** One device, as the pack states it. `make` / `model` are namz's own fields — the same pair the
        block's paper prints — and the rest is what tells one physical box from another wearing the
        same name. A pack that names no model falls back to the file's own name, so a model somebody
        dropped in is listed rather than silently missing. */
    struct Named
    {
        juce::String maker, model, slot, serial, origin;
        int          year = 0;
        int          captures = 0;   // model files in the pack — one per captured setting

        bool operator< (const Named& o) const
        {
            return maker == o.maker ? model.compareIgnoreCase (o.model) < 0
                                    : maker.compareIgnoreCase (o.maker) < 0;
        }

        bool operator== (const Named& o) const
        {
            return maker == o.maker && model == o.model && year == o.year && serial == o.serial;
        }
    };

    /** The paragraphs, measured so the last line is never the one that gets cut. */
    struct Prose final : public juce::Component
    {
        void setWidth (int w)
        {
            width = juce::jmax (160, w);

            const juce::Font f { juce::FontOptions (textH) };
            juce::GlyphArrangement ga;
            ga.addJustifiedText (f, text(), 0.0f, 0.0f, (float) width, juce::Justification::topLeft);
            setSize (width, (int) std::ceil (ga.getBoundingBox (0, -1, true).getHeight() + f.getHeight()));
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (theme::txDim);
            g.setFont (juce::FontOptions (textH));
            g.drawFittedText (text(), getLocalBounds(), juce::Justification::topLeft, 200);
        }

        static juce::String text()
        {
            return juce::String::fromUTF8 (BinaryData::disclaimer_txt, BinaryData::disclaimer_txtSize).trim();
        }

        int width = 600;
    };

    /** The table: two sections, five columns, and a header that says which is which. */
    struct Table final : public juce::Component
    {
        void setWidth (int w) { width = juce::jmax (320, w); layout(); }

        void rebuild()
        {
            shipped.clear();
            added.clear();

            for (const auto& pack : device::DeviceLibrary::scan (device::DeviceLibrary::Slot::any))
            {
                Named n;
                n.slot = slotName (pack.slot);

                for (const auto& stage : pack.rig.chain)
                    if (stage.kind == namz::rig::StageKind::Nam)
                    {
                        n.maker  = juce::String (stage.make).trim();
                        n.model  = juce::String (stage.model).trim();
                        n.serial = juce::String (stage.serialNumber).trim();
                        n.year   = stage.year;

                        // How many times the box was actually recorded: one file per captured
                        // setting. NOT the gain dial's declared points — a dial can state eleven
                        // positions and be captured at nine, and this column is about the work done,
                        // not about what the knob says.
                        n.captures = (int) stage.device.files.size();

                        const auto designed = juce::String (stage.designedIn).trim();
                        const auto made     = juce::String (stage.madeIn).trim();
                        n.origin = made.isNotEmpty() && designed.isNotEmpty() && made != designed
                                     ? designed + juce::String::fromUTF8 (" \xe2\x86\x92 ") + made
                                     : (made.isNotEmpty() ? made : designed);
                        break;
                    }

                if (n.model.isEmpty())
                    n.model = pack.displayName().trim();

                if (n.model.isEmpty())
                    continue;

                auto& into = pack.bundled ? shipped : added;
                if (! into.contains (n))
                    into.add (n);
            }

            shipped.sort();
            added.sort();
            layout();
        }

        void layout()
        {
            const int rows = (int) shipped.size() + (int) added.size();
            const int heads = (shipped.isEmpty() ? 0 : 1) + (added.isEmpty() ? 0 : 1);
            setSize (width, headerH + rows * rowH + heads * (sectionH + 6) + 8);
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds();

            // The column heads, once, above both sections.
            auto head = r.removeFromTop (headerH);
            g.setColour (theme::txFaint);
            const juce::Font hf = theme::displayFont (11.0f);
            drawRow (g, head, hf, 0.08f, "MAKER", "MODEL", "SLOT", "CAPTURES", "YEAR", "SERIAL", "ORIGIN");
            g.setColour (theme::hair2);
            g.fillRect (r.removeFromTop (1));

            section (g, r, "SHIPPED WITH ORBITAMP", shipped);
            section (g, r, "ADDED BY YOU", added);
        }

        void section (juce::Graphics& g, juce::Rectangle<int>& r,
                      const juce::String& title, const juce::Array<Named>& list)
        {
            if (list.isEmpty())
                return;

            r.removeFromTop (6);
            g.setColour (theme::violet);
            theme::drawTracked (g, title, r.removeFromTop (sectionH).toFloat(),
                                theme::displayFont (11.0f), 0.10f, juce::Justification::centredLeft);

            const juce::Font f { juce::FontOptions (textH) };

            for (const auto& d : list)
            {
                auto row = r.removeFromTop (rowH);
                g.setColour (theme::tx);
                drawCell (g, row.removeFromLeft (makerW), f, d.maker.isNotEmpty() ? d.maker : juce::String ("-"));
                drawCell (g, row.removeFromLeft (modelW), f, d.model);
                g.setColour (theme::txFaint);
                drawCell (g, row.removeFromLeft (slotW),     f, d.slot);
                drawCell (g, row.removeFromLeft (capturesW), f, d.captures > 0 ? juce::String (d.captures) : juce::String());
                drawCell (g, row.removeFromLeft (yearW),     f, d.year > 0 ? juce::String (d.year) : juce::String());
                drawCell (g, row.removeFromLeft (serialW), f, d.serial);
                drawCell (g, row,                          f, d.origin);
            }
        }

        static void drawCell (juce::Graphics& g, juce::Rectangle<int> cell,
                              const juce::Font& f, const juce::String& text)
        {
            if (text.isEmpty())
                return;

            g.setFont (f);
            g.drawText (text, cell.withTrimmedRight (10), juce::Justification::centredLeft, true);
        }

        static void drawRow (juce::Graphics& g, juce::Rectangle<int> row, const juce::Font& f, float tracking,
                             const juce::String& a, const juce::String& b, const juce::String& c,
                             const juce::String& d, const juce::String& e, const juce::String& h,
                             const juce::String& i)
        {
            const auto cell = [&] (int w, const juce::String& t)
            {
                theme::drawTracked (g, t, row.removeFromLeft (w).withTrimmedRight (10).toFloat(), f, tracking,
                                    juce::Justification::centredLeft);
            };

            cell (makerW, a); cell (modelW, b); cell (slotW, c); cell (capturesW, d);
            cell (yearW, e); cell (serialW, h);
            theme::drawTracked (g, i, row.withTrimmedRight (10).toFloat(), f, tracking,
                                juce::Justification::centredLeft);
        }

        static juce::String slotName (device::DeviceLibrary::Slot s)
        {
            using Slot = device::DeviceLibrary::Slot;
            switch (s)
            {
                case Slot::pedal:    return "PEDAL";
                case Slot::preamp:   return "PREAMP";
                case Slot::poweramp: return "POWER";
                case Slot::any:      break;
            }
            return {};
        }

        static constexpr int rowH     = 20;
        static constexpr int headerH  = 18;
        static constexpr int sectionH = 18;
        static constexpr int makerW    = 150;
        static constexpr int modelW    = 190;
        static constexpr int slotW     = 62;
        static constexpr int capturesW = 70;
        static constexpr int yearW     = 48;
        static constexpr int serialW   = 100;

        juce::Array<Named> shipped, added;
        int width = 640;
    };

    juce::Rectangle<int> panel, seam;
    juce::Viewport       textView, listView;
    Prose                prose;
    Table                table;
    MiniClose            closeButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DisclaimerPanel)
};

} // namespace orbitamp
