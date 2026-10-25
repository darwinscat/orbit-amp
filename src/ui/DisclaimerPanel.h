// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "Theme.h"
#include "../device/DeviceLibrary.h"
#include "library/MiniClose.h"

#include <BinaryData.h>
#include <felitronics/appkit/WebpImage.h>

#include <algorithm>
#include <memory>
#include <set>

namespace orbitamp
{

/** DEVICES & TRADEMARKS — the long notice, and the list it is about.

    The version window carries the short notice: three sentences saying the names on the faces
    describe equipment rather than claim it. THIS page is the long form, and it exists because the
    long form has a LIST: every box the installed packs were recorded from, by maker and model, with
    what the pack says about the particular box — a capture is of one unit on one day, and the year
    and the serial are what tell that unit from every other one wearing the same name. A click on a
    row opens the box's card: its photograph and the rest of what the packs say.

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

    /** One BOX — one physical device the packs were recorded from — as the packs state it. `make` /
        `model` are namz's own fields, the pair the block's paper prints; the serial is what tells one
        box from every other wearing the same name, and it makes the row: three V4s recorded from three
        boxes are three rows, the X88-IR's four channels taken from one box are one (Oleh, 24.10). A
        pack that names no serial is told apart by its family, then by its file; one that names no
        model falls back to the file's own name, so a model somebody dropped in is listed rather than
        silently missing. */
    struct Box
    {
        juce::String      maker, model, serial, designed, made, by;
        juce::String      key;            // what tells this box from another of its model: see above
        juce::String      label;          // the model as the row says it — Table::nameBoxes()
        juce::String      packName;       // the first pack's own name, where `label` is cut from
        juce::Array<int>  years;
        int               captures = 0;   // model files in the box's packs — one per captured setting
        std::vector<std::pair<int, juce::String>> channels;   // a pack a channel: its place on the ramp, its name
        juce::StringArray controls;       // what was turned while it was recorded

        std::shared_ptr<const device::DeviceLibrary::Pack> pictured;   // the first of its packs with a photograph
        juce::Image photo;                // read when its card is first opened, not before
        bool        photoRead = false;

        bool sameBox (const Box& o) const
        {
            return maker == o.maker && model == o.model && key == o.key;
        }

        bool operator< (const Box& o) const
        {
            if (maker != o.maker)    return maker.compareIgnoreCase (o.maker) < 0;
            if (label != o.label)    return label.compareIgnoreCase (o.label) < 0;
            return serial.compareIgnoreCase (o.serial) < 0;
        }

        void merge (const Box& o)
        {
            captures += o.captures;
            for (const int y : o.years)
                years.addIfNotAlreadyThere (y);
            years.sort();
            channels.insert (channels.end(), o.channels.begin(), o.channels.end());
            for (const auto& c : o.controls)
                controls.addIfNotAlreadyThere (c);
            if (pictured == nullptr)
                pictured = o.pictured;
        }

        juce::String yearsText() const
        {
            juce::StringArray t;
            for (const int y : years)
                t.add (juce::String (y));
            return t.joinIntoString (", ");
        }

        /** The column's short form: one place, or where it was drawn up and where it was built. */
        juce::String originText() const
        {
            return made.isNotEmpty() && designed.isNotEmpty() && made != designed
                     ? designed + juce::String::fromUTF8 (" \xe2\x86\x92 ") + made
                     : (made.isNotEmpty() ? made : designed);
        }

        /** The card's long form, saying only what it has: «Made in Britain (UK)», not the same country twice. */
        juce::String originLong() const
        {
            if (made.isNotEmpty() && (designed.isEmpty() || designed == made))
                return "Made in " + made;
            if (made.isNotEmpty())
                return "Designed in " + designed + juce::String::fromUTF8 (" \xc2\xb7 ") + "made in " + made;
            return designed.isNotEmpty() ? "Designed in " + designed : juce::String();
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

    /** The table: two sections, six columns, and a header that says which is which. A click on a row
        opens that box's card under it — its photograph, the pack's own, and what the packs say about
        it; a click on it again, or on another row, folds it. One open at a time, and the list scrolls
        to show it: nothing else on the page moves. */
    struct Table final : public juce::Component
    {
        void setWidth (int w) { width = juce::jmax (320, w); layout(); }

        void rebuild()
        {
            shipped.clear();
            added.clear();
            openRow  = -1;
            hoverRow = -1;

            for (const auto& pack : device::DeviceLibrary::scan (product, device::DeviceLibrary::Slot::any))
            {
                Box n;

                for (const auto& stage : pack.rig.chain)
                    if (stage.kind == namz::rig::StageKind::Nam)
                    {
                        n.maker    = juce::String (stage.make).trim();
                        n.model    = juce::String (stage.model).trim();
                        n.serial   = juce::String (stage.serialNumber).trim();
                        n.designed = juce::String (stage.designedIn).trim();
                        n.made     = juce::String (stage.madeIn).trim();
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

                        for (const auto& c : stage.device.controls)
                            if (const auto name = juce::String (c.name).trim(); name.isNotEmpty())
                                n.controls.addIfNotAlreadyThere (name);
                        break;
                    }

                if (n.model.isEmpty())
                    n.model = pack.displayName().trim();

                if (n.model.isEmpty())
                    continue;

                n.packName = pack.displayName().trim();
                n.key      = n.serial.isNotEmpty()        ? n.serial
                           : pack.familyId.isNotEmpty()   ? pack.familyId
                                                          : n.packName;
                n.by       = juce::String (pack.rig.modeledBy).trim();
                n.channels.push_back ({ pack.character, pack.alias.isNotEmpty() ? pack.alias : n.packName });

                if (juce::String (pack.rig.picture).trim().isNotEmpty())
                    n.pictured = std::make_shared<const device::DeviceLibrary::Pack> (pack);

                // One row a box: its channels are packs of their own (the X88-IR's four), and every
                // one of them was recorded — their captures add up, their names go on its card.
                auto& into = pack.bundled ? shipped : added;
                const auto at = std::find_if (into.begin(), into.end(), [&n] (const Box& b) { return b.sameBox (n); });
                if (at != into.end())
                    at->merge (n);
                else
                    into.push_back (std::move (n));
            }

            for (auto* list : { &shipped, &added })
            {
                nameBoxes (*list);
                std::sort (list->begin(), list->end());
                for (auto& b : *list)   // clean to hot, the way the lever reads them
                    std::stable_sort (b.channels.begin(), b.channels.end(),
                                      [] (const auto& x, const auto& y) { return x.first < y.first; });
            }

            layout();
        }

        void layout()
        {
            placed.clear();
            heads.clear();
            card = {};

            int y = headerH + 1;

            const auto lay = [&] (const juce::String& title, std::vector<Box>& list)
            {
                if (list.empty())
                    return;

                y += 6;
                heads.push_back ({ headText (title, list), { 0, y, width, sectionH } });
                y += sectionH;

                for (auto& b : list)
                {
                    placed.push_back ({ &b, { 0, y, width, rowH } });
                    y += rowH;

                    if ((int) placed.size() - 1 == openRow)
                    {
                        card = { 0, y, width, cardH };
                        y += cardH;
                    }
                }
            };

            lay ("SHIPPED WITH ORBITAMP", shipped);
            lay ("ADDED BY YOU", added);

            if (placed.empty())
                y += 10 + rowH * 2;   // the "no packs" line

            setSize (width, y + 8);
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            // The column heads, once, above both sections.
            g.setColour (theme::txFaint);
            const juce::Font hf = theme::displayFont (11.0f);
            drawRow (g, getLocalBounds().removeFromTop (headerH), hf, 0.08f,
                     "MAKER", "MODEL", "CAPTURES", "YEAR", "SERIAL", "ORIGIN");
            g.setColour (theme::hair2);
            g.fillRect (0, headerH, width, 1);

            // Each section's head carries its sum: how many NAM captures it holds, every row's together.
            for (const auto& [text, area] : heads)
            {
                g.setColour (theme::violet);
                theme::drawTracked (g, text, area.toFloat(), theme::displayFont (11.0f), 0.10f,
                                    juce::Justification::centredLeft);
            }

            const juce::Font f { juce::FontOptions (textH) };

            for (int i = 0; i < (int) placed.size(); ++i)
            {
                const auto& d    = *placed[(size_t) i].box;
                auto        row  = placed[(size_t) i].row;
                const bool  open = i == openRow;

                if (open || i == hoverRow)
                {
                    g.setColour (theme::violet.withAlpha (open ? 0.16f : 0.08f));
                    g.fillRoundedRectangle (row.toFloat(), 3.0f);
                }

                paintChevron (g, row.removeFromRight (chevronW), open);

                g.setColour (theme::tx);
                drawCell (g, row.removeFromLeft (makerW), f, d.maker.isNotEmpty() ? d.maker : juce::String ("-"));
                drawCell (g, row.removeFromLeft (modelW), f, d.label);
                g.setColour (theme::txFaint);
                drawCell (g, row.removeFromLeft (capturesW), f, d.captures > 0 ? juce::String (d.captures) : juce::String());
                drawCell (g, row.removeFromLeft (yearW),     f, d.yearsText());
                drawCell (g, row.removeFromLeft (serialW),   f, d.serial);
                drawCell (g, row,                            f, d.originText());
            }

            if (juce::isPositiveAndBelow (openRow, (int) placed.size()))
                paintCard (g, *placed[(size_t) openRow].box, card);

            // A page about named devices, with no devices named: say so, rather than leave a reader
            // staring at column heads over nothing. This build ships an empty factory on purpose.
            if (placed.empty())
            {
                g.setColour (theme::txFaint);
                g.setFont (juce::FontOptions (textH));
                g.drawFittedText ("No packs installed. Drop an .orbitrig pack into the Devices folder "
                                  "and every device it names appears here.",
                                  { 0, headerH + 11, width, rowH * 2 }, juce::Justification::topLeft, 2);
            }
        }

        void mouseMove (const juce::MouseEvent& e) override { setHover (rowAt (e.getPosition())); }
        void mouseExit (const juce::MouseEvent&) override   { setHover (-1); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const int i = rowAt (e.getPosition());
            if (i < 0)
                return;

            openRow = i == openRow ? -1 : i;

            if (openRow >= 0)
                readPhoto (*placed[(size_t) openRow].box);

            layout();

            if (openRow >= 0)
                reveal (placed[(size_t) openRow].row.getUnion (card));
        }

    private:
        struct Placed { Box* box; juce::Rectangle<int> row; };

        int rowAt (juce::Point<int> p) const
        {
            for (int i = 0; i < (int) placed.size(); ++i)
                if (placed[(size_t) i].row.contains (p))
                    return i;
            return -1;
        }

        void setHover (int i)
        {
            if (i == hoverRow)
                return;

            hoverRow = i;
            setMouseCursor (i >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
            repaint();
        }

        /** The list scrolls the open card into sight — all of it, the row on top. */
        void reveal (juce::Rectangle<int> area)
        {
            if (auto* vp = findParentComponentOfClass<juce::Viewport>())
            {
                int y = vp->getViewPositionY();
                if (area.getBottom() > y + vp->getViewHeight())
                    y = area.getBottom() - vp->getViewHeight();
                if (area.getY() < y)
                    y = area.getY();
                vp->setViewPosition (0, y);
            }
        }

        /** The model as the row says it. Where a section holds ONE box of a model, the model alone;
            where it holds several — three V4s — each says which, by the name its pack file carries,
            less the maker in front and the voice in brackets: «Victory V4 Copper (clean)» is «V4
            Copper». A file that says no more than the model says its serial instead. */
        static void nameBoxes (std::vector<Box>& list)
        {
            for (auto& b : list)
            {
                b.label = b.model;

                const auto twins = std::count_if (list.begin(), list.end(), [&b] (const Box& o)
                                                  { return o.maker == b.maker && o.model == b.model; });
                if (twins < 2)
                    continue;

                auto n = b.packName;
                if (b.maker.isNotEmpty() && n.startsWithIgnoreCase (b.maker + " "))
                    n = n.substring (b.maker.length() + 1).trim();
                if (n.endsWithChar (')') && n.containsChar ('('))
                    n = n.upToLastOccurrenceOf ("(", false, false).trim();

                if (n.isNotEmpty() && ! n.equalsIgnoreCase (b.model))
                    b.label = n;
                else if (b.serial.isNotEmpty())
                    b.label = b.model + " " + b.serial;
            }
        }

        /** The photograph, from the pack, the way the block reads it: WebP as the format states, a PNG
            a hand-assembled pack names taken too. Once, on the card's first opening. */
        static void readPhoto (Box& b)
        {
            if (b.photoRead || b.pictured == nullptr)
                return;

            b.photoRead = true;

            const auto bytes = device::DeviceLibrary::readBinaryEntry (*b.pictured, juce::String (b.pictured->rig.picture).trim());
            if (bytes.getSize() == 0)
                return;

            b.photo = felitronics::appkit::decodeWebp (bytes.getData(), bytes.getSize());
            if (! b.photo.isValid())
                b.photo = juce::ImageFileFormat::loadFrom (bytes.getData(), bytes.getSize());
        }

        static juce::String headText (const juce::String& title, const std::vector<Box>& list)
        {
            int total = 0;
            for (const auto& d : list)
                total += d.captures;

            return total > 0 ? title + juce::String::fromUTF8 (" \xe2\x80\x94 ") + juce::String (total)
                                     + (total == 1 ? " NAM CAPTURE" : " NAM CAPTURES")
                             : title;
        }

        /** The card: the photograph in a well on the left, what the packs say on the right. */
        void paintCard (juce::Graphics& g, const Box& b, juce::Rectangle<int> area) const
        {
            auto r = area.reduced (0, 6).toFloat();

            g.setColour (theme::bezel.withAlpha (0.55f));
            g.fillRoundedRectangle (r, theme::radiusSm);
            g.setColour (theme::hair2);
            g.drawRoundedRectangle (r.reduced (0.5f), theme::radiusSm, 1.0f);

            auto in   = r.reduced (12.0f, 10.0f);
            auto well = in.removeFromLeft (in.getHeight());

            g.setColour (theme::bezel);
            g.fillRoundedRectangle (well, theme::radiusSm);

            if (b.photo.isValid())
            {
                g.setOpacity (1.0f);
                g.drawImage (b.photo, well.reduced (6.0f), juce::RectanglePlacement::centred);
            }
            else
            {
                g.setColour (theme::txFaint);
                g.setFont (juce::FontOptions (textH));
                g.drawFittedText ("No photograph\nin this pack", well.toNearestInt(), juce::Justification::centred, 2);
            }

            in.removeFromLeft (18.0f);

            g.setColour (theme::tx);
            theme::drawTracked (g, (b.maker.isNotEmpty() ? b.maker + " " + b.label : b.label).toUpperCase(),
                                in.removeFromTop (22.0f), theme::displayFont (14.0f), 0.08f,
                                juce::Justification::centredLeft);
            in.removeFromTop (4.0f);

            juce::StringArray channelNames;
            for (const auto& c : b.channels)
                channelNames.addIfNotAlreadyThere (c.second);

            const auto dot = juce::String::fromUTF8 (" \xc2\xb7 ");
            const std::pair<juce::String, juce::String> lines[] =
            {
                { "Serial",      b.serial },
                { "Recorded",    b.yearsText() },
                { "Origin",      b.originLong() },
                { "Captures",    b.captures > 0 ? juce::String (b.captures) + (b.captures == 1 ? " NAM capture" : " NAM captures")
                                                : juce::String() },
                { channelNames.size() == 1 ? "Channel" : "Channels", channelNames.joinIntoString (dot) },
                { "Controls",    b.controls.joinIntoString (dot) },
                { "Captured by", b.by },
            };

            const juce::Font f { juce::FontOptions (textH) };
            g.setFont (f);

            for (const auto& [name, value] : lines)
            {
                if (value.isEmpty())
                    continue;

                auto line = in.removeFromTop (cardLineH).toNearestInt();
                g.setColour (theme::txFaint);
                g.drawText (name, line.removeFromLeft (cardLabelW), juce::Justification::centredLeft, true);
                g.setColour (theme::tx);
                g.drawText (value, line, juce::Justification::centredLeft, true);
            }
        }

        static void paintChevron (juce::Graphics& g, juce::Rectangle<int> cell, bool open)
        {
            const auto c = cell.toFloat().getCentre();
            juce::Path p;

            if (open)
                p.addTriangle (c.x - 4.0f, c.y - 2.0f, c.x + 4.0f, c.y - 2.0f, c.x, c.y + 3.0f);
            else
                p.addTriangle (c.x - 2.0f, c.y - 4.0f, c.x - 2.0f, c.y + 4.0f, c.x + 3.0f, c.y);

            g.setColour (open ? theme::violet : theme::txFaint);
            g.fillPath (p);
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

            row.removeFromRight (chevronW);
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
        static constexpr int chevronW  = 16;

        // The card: as tall as the list's half shows with its row above it, the photograph square.
        static constexpr int   cardH      = 180;
        static constexpr float cardLineH  = 17.0f;
        static constexpr int   cardLabelW = 96;

        std::vector<Box>    shipped, added;
        std::vector<Placed> placed;
        std::vector<std::pair<juce::String, juce::Rectangle<int>>> heads;
        juce::Rectangle<int> card;
        int openRow  = -1;
        int hoverRow = -1;
        int width    = 640;

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
