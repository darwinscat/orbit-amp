// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "SetupPanel.h"

#include "../PluginProcessor.h"
#include "Prefs.h"
#include "Theme.h"

namespace orbitamp
{

SetupPanel::SetupPanel (AmpProcessor& processor) : amp (processor)
{
    setWantsKeyboardFocus (true);

    for (auto* devices : { &preampDevices, &boostDevices })
        devices->onChanged = [this]
        {
            if (onDevicesChanged)
                onDevicesChanged();
        };

    closeButton.onClick = [this] { setVisible (false); };

    // ---- LIBRARY: the three lists behind their own sub-tabs, and the one switch that is about
    //      the packs rather than about the window. ----
    // It is a HOLDER, not a surface: its own sub-tabs are painted by the panel behind it, so a
    // click in that strip has to fall through to the panel or the tabs cannot be picked at all.
    libraryPage.setInterceptsMouseClicks (false, true);
    addAndMakeVisible (libraryPage);
    for (auto* v : { (juce::Component*) &preampDevices, (juce::Component*) &boostDevices,
                     (juce::Component*) &irs })
        libraryPage.addAndMakeVisible (*v);
    libraryPage.addAndMakeVisible (packSwitches);

    libraries.push_back ({ "PREAMP", &preampDevices, [this] { preampDevices.rebuild(); }, {} });
    libraries.push_back ({ "BOOST",  &boostDevices,  [this] { boostDevices.rebuild(); },  {} });
    libraries.push_back ({ "IR",     &irs,           [this] { irs.rebuild(); },           {} });

    {
        auto* p = amp.apvts.getParameter (params::packLevelComp);

        packSwitches.setRows ({ { "PACK LEVEL COMP",
                                  "honour each capture's own level trim - travels with the preset",
                                  [p] { return p->getValue() > 0.5f; },
                                  [p] (bool on)
                                  {
                                      p->beginChangeGesture();
                                      p->setValueNotifyingHost (on ? 1.0f : 0.0f);
                                      p->endChangeGesture();
                                  },
                                  {}, false } });
    }

    buildEditorPage();
    buildViewPage();

    for (auto* pair : { &editorView, &viewView })
    {
        pair->setScrollBarsShown (true, false);
        pair->setScrollBarThickness (8);
        addAndMakeVisible (*pair);
    }

    editorView.setViewedComponent (&editorPage, false);
    viewView  .setViewedComponent (&viewPage,   false);

    pages.push_back ({ "LIBRARY", &libraryPage, [this] { libraries[(size_t) currentLibrary].refresh(); }, {} });
    pages.push_back ({ "EDITOR",  &editorView,  [this] { editorPage.repaint(); }, {} });
    pages.push_back ({ "VIEW",    &viewView,    [this] { buildViewPage(); },      {} });

    addAndMakeVisible (closeButton);
    selectLibrary (0);
    selectPage (0);
}

void SetupPanel::buildEditorPage()
{
    // Straight off the one list, in chain order, each row wearing its link's own accent — so the
    // page reads as the strip it edits rather than as a form about it.
    std::vector<SettingsList::Row> rows;

    for (int i = 0; i < params::numChainRows; ++i)
    {
        const auto& link = params::chainLinks[(size_t) i];
        auto* p = amp.apvts.getParameter (link.presentParam);

        if (p == nullptr)
            continue;

        rows.push_back ({ link.name,
                          link.captured ? "a captured device" : "",
                          [p] { return p->getValue() > 0.5f; },
                          [p] (bool on)
                          {
                              p->beginChangeGesture();
                              p->setValueNotifyingHost (on ? 1.0f : 0.0f);
                              p->endChangeGesture();
                          },
                          link.captured ? theme::orange : theme::violet, true });
    }

    editorPage.setRows (std::move (rows));
}

void SetupPanel::buildViewPage()
{
    std::vector<SettingsList::Row> rows;

    const auto add = [&] (juce::String name, juce::String note, juce::Identifier key, bool fallback)
    {
        rows.push_back ({ std::move (name), std::move (note),
                          [key, fallback] { return prefs::getBool (key, fallback); },
                          [this, key, fallback] (bool on)
                          {
                              prefs::setBool (key, on);
                              if (onViewChanged)
                                  onViewChanged();
                          },
                          {}, false });
    };

    rows.push_back ({ "SHOW SPECTRA", "the analyser behind every curve and picture",
                      [] { return prefs::spectraShown(); },
                      [this] (bool on) { prefs::setSpectraShown (on); if (onViewChanged) onViewChanged(); },
                      {}, false });

    add ("KEEP WINDOW HEIGHT", "an emptied row hands its height to whoever is left",
         prefs::growBlocks, true);
    add ("STANDBY KEEPS ITS PLACE", "a link standing by stays on the panel, dark, instead of leaving",
         prefs::dimStandby, false);

    if (params::demoLoopsPresent())   // no loops on disk: no player, and no offer of one
        add ("SHOW DEMO PLAYER", "the audition strip under the footer", prefs::showDemo, false);

    add ("EQ HANDS OVER THE CURVE", "the console's row of knobs sits above its picture, not below",
         prefs::eqRowOnTop, false);

    add ("SHOW DEVICE GLYPHS", "the device-glyph review strip", prefs::showGlyphs, false);

    viewPage.setRows (std::move (rows));
}

void SetupPanel::open()
{
    pages[(size_t) currentPage].refresh();

    setVisible (true);
    toFront (false);
    grabKeyboardFocus();
}

void SetupPanel::selectPage (int index)
{
    currentPage = index;

    for (int i = 0; i < (int) pages.size(); ++i)
        pages[(size_t) i].view->setVisible (i == currentPage);

    pages[(size_t) currentPage].refresh();
    resized();
    repaint();
}

void SetupPanel::selectLibrary (int index)
{
    currentLibrary = index;

    for (int i = 0; i < (int) libraries.size(); ++i)
        libraries[(size_t) i].view->setVisible (i == currentLibrary);

    libraries[(size_t) currentLibrary].refresh();
    resized();
    repaint();
}

void SetupPanel::layOutTabs (juce::Rectangle<int>& header, std::vector<Tab>& tabs, float height)
{
    const auto font = theme::displayFont (height);

    for (auto& tab : tabs)
    {
        const int w = juce::roundToInt (theme::trackedWidth (tab.title, font, 0.15f)) + 22;
        tab.area = header.removeFromLeft (w);
    }
}

void SetupPanel::resized()
{
    // FITS, always. The editor shrinks with its layout, and the one window a player uses to put
    // the missing rows back is the last thing that may go off the top edge.
    panel = getLocalBounds().withSizeKeepingCentre (juce::jmin (getWidth()  - 24, panelW),
                                                    juce::jmin (getHeight() - 24, panelH));

    auto r = panel.reduced (16, 12);

    auto header = r.removeFromTop (headerH);
    closeButton.setBounds (header.removeFromRight (24).withSizeKeepingCentre (22, 22));

    header.removeFromLeft (juce::roundToInt (
        theme::trackedWidth ("SETUP", theme::displayFont (13.0f), 0.15f)) + 28);

    layOutTabs (header, pages, 13.0f);

    r.removeFromTop (6);

    for (auto& page : pages)
        page.view->setBounds (r);

    // The scrolled pages are as tall as their rows; the viewport is the window onto them.
    editorPage.setSize (r.getWidth() - 10, editorPage.getHeight());
    viewPage  .setSize (r.getWidth() - 10, viewPage.getHeight());

    // LIBRARY's own body: its sub-tabs, then whichever list they picked, then the pack switch.
    {
        auto lib = libraryPage.getLocalBounds();
        auto sub = lib.removeFromTop (subHeaderH);
        layOutTabs (sub, libraries, 13.0f);

        auto packRow = lib.removeFromBottom (SettingsList::rowH);
        packSwitches.setBounds (packRow);

        for (auto& tab : libraries)
            tab.view->setBounds (lib);
    }
}

void SetupPanel::paintTabs (juce::Graphics& g, const std::vector<Tab>& tabs, int current,
                            float height, juce::Point<int> offset) const
{
    const auto font = theme::displayFont (height);

    for (int i = 0; i < (int) tabs.size(); ++i)
    {
        const auto& tab = tabs[(size_t) i];
        const bool active = i == current;
        const auto area = tab.area.translated (offset.x, offset.y);

        g.setColour (active ? theme::tx : theme::txDim);
        theme::drawTracked (g, tab.title, area.toFloat(), font, 0.15f, juce::Justification::centred);

        if (active)
        {
            g.setColour (theme::violet);
            g.fillRect (area.withTrimmedTop (area.getHeight() - 2).reduced (4, 0));
        }
    }
}

void SetupPanel::paint (juce::Graphics& g)
{
    // The scrim IS the modality: the device stays visible but unmistakably behind.
    g.fillAll (theme::ground.withAlpha (0.78f));

    const auto p = panel.toFloat();
    g.setColour (theme::panel);
    g.fillRoundedRectangle (p, theme::radiusLg);
    g.setColour (theme::hair2);
    g.drawRoundedRectangle (p.reduced (0.75f), theme::radiusLg, 1.5f);

    const auto header = panel.reduced (16, 12).removeFromTop (headerH);
    g.setColour (theme::tx);
    theme::drawTracked (g, "SETUP", header.toFloat(), theme::displayFont (13.0f), 0.15f,
                        juce::Justification::centredLeft);

    paintTabs (g, pages, currentPage, 13.0f);

    // Which lifetime this page has, said quietly under its own tabs rather than left to be
    // discovered when somebody's preset rearranges somebody else's window.
    // LIBRARY is the one page with two answers — the folders are this machine's, the one switch
    // on it rides in the preset — and saying NOTHING was the worst of the three: a page with no
    // note reads as a page nobody thought about, not as a page whose answer is "both".
    const juce::String lifetime = currentPage == 1 ? "TRAVELS WITH THE PRESET"
                                : currentPage == 2 ? "THIS MACHINE ONLY"
                                                   : juce::String ("FOLDERS: THIS MACHINE");

    {
        auto note = panel.reduced (16, 12).removeFromTop (headerH).removeFromRight (330)
                         .withTrimmedRight (30);
        g.setColour (theme::txFaint);
        theme::drawTracked (g, lifetime, note.toFloat(), theme::displayFont (13.0f), 0.15f,
                            juce::Justification::centredRight);
    }

    if (currentPage == 0)
        paintTabs (g, libraries, currentLibrary, 13.0f, libraryPage.getPosition());
}

void SetupPanel::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < (int) pages.size(); ++i)
        if (pages[(size_t) i].area.contains (e.getPosition()))
        {
            selectPage (i);
            return;
        }

    if (currentPage == 0)
        for (int i = 0; i < (int) libraries.size(); ++i)
            if (libraries[(size_t) i].area.translated (libraryPage.getX(), libraryPage.getY())
                    .contains (e.getPosition()))
            {
                selectLibrary (i);
                return;
            }

    if (! panel.contains (e.getPosition()))
        setVisible (false);
}

bool SetupPanel::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey)
    {
        setVisible (false);
        return true;
    }

    return false;
}

} // namespace orbitamp
