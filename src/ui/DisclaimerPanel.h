// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "Theme.h"
#include "../device/DeviceLibrary.h"
#include "library/MiniClose.h"

#include <BinaryData.h>

#include <set>

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
    /** The notice of the product of that name, over the devices it plays. */
    explicit DisclaimerPanel (const juce::String& product)
    {
        table.product = product;
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

    /** One device, as the packs state it. `make` / `model` are namz's own fields — the same pair the
        block's paper prints — and they make the row: ONE row a name. The physical boxes behind it are
        its serials, listed in the order the packs met them — more than one when that model was
        recorded from more than one box — and the years they were recorded in. A pack that names no
        model falls back to the file's own name, so a model somebody dropped in is listed rather than
        silently missing. */
    struct Named
    {
        juce::String      maker, model, origin;
        juce::StringArray serials;
        juce::Array<int>  years;
        int               captures = 0;   // model files in the box's packs — one per captured setting

        bool operator< (const Named& o) const
        {
            return maker == o.maker ? model.compareIgnoreCase (o.model) < 0
                                    : maker.compareIgnoreCase (o.maker) < 0;
        }

        bool operator== (const Named& o) const
        {
            return maker == o.maker && model == o.model;
        }

        void merge (const Named& o)
        {
            captures += o.captures;
            for (const auto& s : o.serials)
                serials.addIfNotAlreadyThere (s);
            for (const int y : o.years)
                years.addIfNotAlreadyThere (y);
            years.sort();
        }

        juce::String yearsText() const
        {
            juce::StringArray t;
            for (const int y : years)
                t.add (juce::String (y));
            return t.joinIntoString (", ");
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

            for (const auto& pack : device::DeviceLibrary::scan (product, device::DeviceLibrary::Slot::any))
            {
                Named n;

                for (const auto& stage : pack.rig.chain)
                    if (stage.kind == namz::rig::StageKind::Nam)
                    {
                        n.maker  = juce::String (stage.make).trim();
                        n.model  = juce::String (stage.model).trim();
                        if (const auto serial = juce::String (stage.serialNumber).trim(); serial.isNotEmpty())
                            n.serials.add (serial);
                        if (stage.year > 0)
                            n.years.add (stage.year);

                        // How many times the box was actually recorded: one FILE per captured
                        // setting. Not the entries of `files` — a dial can state eleven positions and
                        // be captured at nine, its bottom notches pointing at the capture above them
                        // (namz FileEntry: the X88-IR's 0 and 30 play its 60) — and this column is
                        // about the work done, not about what the knob says.
                        std::set<std::string> recorded;
                        for (const auto& f : stage.device.files)
                            recorded.insert (f.id);
                        n.captures = (int) recorded.size();

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

                // One row a name: its channels are packs of their own (the X88-IR's four), and every
                // one of them was recorded — their captures add up, their boxes are listed.
                auto& into = pack.bundled ? shipped : added;
                if (const int at = into.indexOf (n); at >= 0)
                    into.getReference (at).merge (n);
                else
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
            const int empty = rows == 0 ? 10 + rowH * 2 : 0;   // the "no packs" line
            setSize (width, headerH + rows * rowH + heads * (sectionH + 6) + empty + 8);
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds();

            // The column heads, once, above both sections.
            auto head = r.removeFromTop (headerH);
            g.setColour (theme::txFaint);
            const juce::Font hf = theme::displayFont (11.0f);
            drawRow (g, head, hf, 0.08f, "MAKER", "MODEL", "CAPTURES", "YEAR", "SERIAL", "ORIGIN");
            g.setColour (theme::hair2);
            g.fillRect (r.removeFromTop (1));

            section (g, r, "SHIPPED WITH ORBITAMP", shipped);
            section (g, r, "ADDED BY YOU", added);

            // A page about named devices, with no devices named: say so, rather than leave a reader
            // staring at column heads over nothing. This build ships an empty factory on purpose.
            if (shipped.isEmpty() && added.isEmpty())
            {
                r.removeFromTop (10);
                g.setColour (theme::txFaint);
                g.setFont (juce::FontOptions (textH));
                g.drawFittedText ("No packs installed. Drop an .orbitrig pack into the Devices folder "
                                  "and every device it names appears here.",
                                  r.removeFromTop (rowH * 2), juce::Justification::topLeft, 2);
            }
        }

        void section (juce::Graphics& g, juce::Rectangle<int>& r,
                      const juce::String& title, const juce::Array<Named>& list)
        {
            if (list.isEmpty())
                return;

            // The section's sum on its own head: how many NAM captures it holds, every row's together.
            int total = 0;
            for (const auto& d : list)
                total += d.captures;
            const auto head = total > 0 ? title + juce::String::fromUTF8 (" \xe2\x80\x94 ") + juce::String (total)
                                                + (total == 1 ? " NAM CAPTURE" : " NAM CAPTURES")
                                        : title;

            r.removeFromTop (6);
            g.setColour (theme::violet);
            theme::drawTracked (g, head, r.removeFromTop (sectionH).toFloat(),
                                theme::displayFont (11.0f), 0.10f, juce::Justification::centredLeft);

            const juce::Font f { juce::FontOptions (textH) };

            for (const auto& d : list)
            {
                auto row = r.removeFromTop (rowH);
                g.setColour (theme::tx);
                drawCell (g, row.removeFromLeft (makerW), f, d.maker.isNotEmpty() ? d.maker : juce::String ("-"));
                drawCell (g, row.removeFromLeft (modelW), f, d.model);
                g.setColour (theme::txFaint);
                drawCell (g, row.removeFromLeft (capturesW), f, d.captures > 0 ? juce::String (d.captures) : juce::String());
                drawCell (g, row.removeFromLeft (yearW),     f, d.yearsText());
                drawCell (g, row.removeFromLeft (serialW), f, d.serials.joinIntoString (", "));
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
                             const juce::String& a, const juce::String& b, const juce::String& d,
                             const juce::String& e, const juce::String& h, const juce::String& i)
        {
            const auto cell = [&] (int w, const juce::String& t)
            {
                theme::drawTracked (g, t, row.removeFromLeft (w).withTrimmedRight (10).toFloat(), f, tracking,
                                    juce::Justification::centredLeft);
            };

            cell (makerW, a); cell (modelW, b); cell (capturesW, d);
            cell (yearW, e); cell (serialW, h);
            theme::drawTracked (g, i, row.withTrimmedRight (10).toFloat(), f, tracking,
                                juce::Justification::centredLeft);
        }

        static constexpr int rowH     = 20;
        static constexpr int headerH  = 18;
        static constexpr int sectionH = 18;
        // No SLOT: every device the set ships is an amp, and a column that says so on every row says
        // nothing (Oleh, 21.10). Its width went where the words were cut — CAPTURES' own head, the
        // serials, ORIGIN.
        static constexpr int makerW    = 120;
        static constexpr int modelW    = 150;
        static constexpr int capturesW = 96;
        static constexpr int yearW     = 56;
        static constexpr int serialW   = 170;

        juce::Array<Named> shipped, added;
        int width = 640;

    public:
        juce::String product;       // whose devices: the product's name
    };

    juce::Rectangle<int> panel, seam;
    juce::Viewport       textView, listView;
    Prose                prose;
    Table                table;
    MiniClose            closeButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DisclaimerPanel)
};

} // namespace orbitamp
