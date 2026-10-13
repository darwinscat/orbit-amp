// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "CatpitCabs.h"

namespace orbitamp::catpit
{

namespace
{
    // In the editor's own pixels: a Sheet's content is not on the scaled stage.
    constexpr int narrowest = 176, widest = 320, rowH = 28, titleH = 34, pad = 16, gap = 14, groupGap = 24;
    constexpr int nameLeft = 32, nameRight = 6, rowGap = 8;     // a row: the lamp's strip, the name, and the gap to the next column
    constexpr int keyW = 92, keyH = 28, keyGap = 10, fewestRows = 6;
    constexpr int okGap = 28;       // OK stands apart from the keys that do something to a cabinet
    constexpr float namePx = 15.0f;

    using panel::textPx, panel::ink, panel::dim, panel::faint, panel::write;

    constexpr const char* headings[] { "Factory", "Yours" };
}

//==============================================================================
CabPanel::Lists::Lists (CabPanel& p) : owner (p) {}

int CabPanel::Lists::columnsOf (int group) const
{
    const int rows = group == 0 ? owner.factory : owner.files.size() - owner.factory;
    return juce::jmax (1, (rows + deep - 1) / deep);        // a list with nothing in it still has its heading
}

int CabPanel::Lists::leftOf (int group) const
{
    return group == 0 ? 0 : columnsOf (0) * wide[0] + groupGap;
}

void CabPanel::Lists::layOut (int rowsToAColumn)
{
    deep = juce::jmax (1, rowsToAColumn);
    hovered = -1;

    // A column is as wide as its list's longest name wants — a name cut short is a cabinet half told —
    // but no wider than is reasonable: past that the name is cut, and the file can be renamed.
    const auto font = panel::textFont (namePx);
    for (int group = 0; group < 2; ++group)
    {
        float longest = 0.0f;
        for (int i = group == 0 ? 0 : owner.factory; i < (group == 0 ? owner.factory : owner.files.size()); ++i)
            longest = juce::jmax (longest, juce::GlyphArrangement::getStringWidth (font, Channels::cabinetName (owner.files.getReference (i)).toUpperCase()));
        wide[group] = juce::jlimit (narrowest, widest, nameLeft + juce::roundToInt (std::ceil (longest)) + 2 + nameRight + rowGap);
    }

    const int own = owner.files.size() - owner.factory;
    const int tallest = juce::jlimit (2, deep, juce::jmax (owner.factory, own));    // two: the empty list says two lines
    setSize (leftOf (1) + columnsOf (1) * wide[1], titleH + tallest * rowH);
    repaint();
}

juce::Rectangle<int> CabPanel::Lists::row (int index) const
{
    const int group = index < owner.factory ? 0 : 1;
    const int k = index - (group == 0 ? 0 : owner.factory);
    return { leftOf (group) + (k / deep) * wide[group], titleH + (k % deep) * rowH, wide[group] - rowGap, rowH };
}

int CabPanel::Lists::rowAt (juce::Point<int> p) const
{
    for (int i = 0; i < owner.files.size(); ++i)
        if (row (i).contains (p))
            return i;
    return -1;
}

void CabPanel::Lists::paint (juce::Graphics& g)
{
    for (int group = 0; group < 2; ++group)
        write (g, headings[group], { leftOf (group) + 8, 4, wide[group] - 16, titleH - 8 }, textPx, faint);

    // The line between what is the build's and what is the player's.
    g.setColour (violet.withAlpha (0.28f));
    g.fillRect (leftOf (1) - groupGap / 2, 2, 1, getHeight() - 4);

    if (owner.files.size() == owner.factory)
    {
        const auto first = juce::Rectangle<int> (leftOf (1) + 8, titleH, wide[1] - 16, rowH);
        write (g, "Drop IR files here", first, textPx, dim);
        write (g, "or press Add", first.translated (0, rowH), textPx, dim);
    }

    for (int i = 0; i < owner.files.size(); ++i)
    {
        const auto r = row (i);
        const bool on = i == owner.playing;

        if (i == hovered)
        {
            g.setColour (violet.withAlpha (0.14f));
            g.fillRoundedRectangle (r.toFloat(), 5.0f);
        }

        // The lamp: the channel names' own, lit orange on the cabinet that is playing.
        g.setColour (on ? orange : juce::Colour (0xff3a2a22));
        g.fillRoundedRectangle (juce::Rectangle<float> (12.0f, 6.0f).withCentre ({ (float) r.getX() + 16.0f, (float) r.getCentreY() }), 2.0f);

        write (g, Channels::cabinetName (owner.files.getReference (i)), r.withTrimmedLeft (nameLeft).withTrimmedRight (nameRight), namePx, on ? ink : dim);
    }
}

void CabPanel::Lists::mouseMove (const juce::MouseEvent& e)
{
    if (const int now = rowAt (e.getPosition()); now != hovered)
    {
        hovered = now;
        setMouseCursor (now >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void CabPanel::Lists::mouseExit (const juce::MouseEvent&)
{
    if (hovered >= 0)
    {
        hovered = -1;
        repaint();
    }
}

void CabPanel::Lists::mouseDown (const juce::MouseEvent& e)
{
    if (const int i = rowAt (e.getPosition()); i >= 0 && owner.onChoose)
    {
        owner.onChoose (i);
        owner.refresh();
    }
}

//==============================================================================
CabPanel::CabPanel (const Channels& c, juce::Point<int> space) : channels (c), room (space)
{
    view.setViewedComponent (&lists, false);
    view.setScrollBarsShown (false, true);
    view.setScrollBarThickness (8);
    view.getHorizontalScrollBar().setColour (juce::ScrollBar::thumbColourId, violet.withAlpha (0.6f));
    addAndMakeVisible (view);

    for (auto* key : { &add, &bypass, &save, &rename, &remove, &ok })
        addAndMakeVisible (key);
    bypass.onClick = [this] { if (onBypass) onBypass(); refresh(); };
    add.onClick    = [this] { if (onAdd)    onAdd(); };
    save.onClick   = [this] { if (onSave)   onSave(); };
    rename.onClick = [this] { if (onRename) onRename(); };
    remove.onClick = [this] { if (onRemove) onRemove(); };
    ok.onClick     = [this] { if (onClose)  onClose(); };

    refresh();

    // Opened on a cabinet far along the lists, the window shows it.
    if (playing >= 0)
        view.setViewPosition (juce::jmax (0, lists.row (playing).getRight() + 8 - view.getWidth()), 0);

    startTimerHz (4);       // a file dropped on the cockpit, an undo: the lists follow without being told
}

void CabPanel::refresh()
{
    const auto& now = channels.cabinets();
    const int   nowPlaying = channels.cabinet();
    const bool  relaid = now != files || channels.factoryCabinets() != factory || getHeight() == 0;

    // Bypassed, the key stands lit and the lists pale: they are what is not playing.
    if (const bool nowOut = ! channels.cabinetOn(); nowOut != out || relaid)
    {
        out = nowOut;
        bypass.setLit (out);
        lists.setAlpha (out ? 0.4f : 1.0f);
    }

    if (! relaid && nowPlaying == playing)
        return;

    files   = now;
    factory = channels.factoryCabinets();
    playing = nowPlaying;

    // RENAME and REMOVE are for a cabinet of the player's own; SAVE for any that is one of the cockpit's.
    save.setEnabled (playing >= 0);
    rename.setEnabled (playing >= factory);
    remove.setEnabled (playing >= factory);

    if (relaid)
    {
        const int keysW = keyW * 6 + keyGap * 4 + okGap;
        const int deep  = juce::jmax (fewestRows, (room.y - pad * 2 - gap - keyH - titleH) / rowH);
        lists.layOut (deep);

        // As wide as the lists, as far as the window lets it; past that they scroll, and the bar takes its strip.
        const int widest = juce::jmax (keysW, room.x - pad * 2);
        const bool scrolls = lists.getWidth() > widest;
        const int viewW = juce::jmax (keysW, juce::jmin (lists.getWidth(), widest));
        const int viewH = lists.getHeight() + (scrolls ? view.getScrollBarThickness() + 4 : 0);

        view.setBounds (pad, pad - 6, viewW, viewH);

        const int keysY = view.getBottom() + gap;
        add.setBounds (pad, keysY, keyW, keyH);
        bypass.setBounds (add.getRight() + keyGap, keysY, keyW, keyH);
        ok.setBounds (pad + viewW - keyW, keysY, keyW, keyH);
        remove.setBounds (ok.getX() - okGap - keyW, keysY, keyW, keyH);
        rename.setBounds (remove.getX() - keyGap - keyW, keysY, keyW, keyH);
        save.setBounds (rename.getX() - keyGap - keyW, keysY, keyW, keyH);

        setSize (viewW + pad * 2, keysY + keyH + pad - 6);
    }

    lists.repaint();
}

} // namespace orbitamp::catpit
