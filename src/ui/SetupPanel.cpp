// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "SetupPanel.h"

#include "Theme.h"

namespace orbitamp
{

SetupPanel::SetupPanel()
{
    setWantsKeyboardFocus (true);

    for (auto* devices : { &preampDevices, &boostDevices, &powerDevices })
        devices->onChanged = [this]
        {
            if (onDevicesChanged)
                onDevicesChanged();
        };

    closeButton.onClick = [this] { setVisible (false); };

    tabs.push_back ({ "PREAMP",    &preampDevices, [this] { preampDevices.rebuild(); }, {} });
    tabs.push_back ({ "BOOST",     &boostDevices,  [this] { boostDevices.rebuild(); },  {} });
    // No POWER AMP tab: the block is benched until its packs are real — see the editor's
    // layout table, which benches it from the panel the same way.
    tabs.push_back ({ "IR",        &irs,           [this] { irs.rebuild(); },           {} });

    for (auto& tab : tabs)
        addAndMakeVisible (*tab.view);

    addAndMakeVisible (closeButton);
}

void SetupPanel::open()
{
    tabs[(size_t) current].refresh();

    setVisible (true);
    toFront (false);
    grabKeyboardFocus();
}

void SetupPanel::selectTab (int index)
{
    current = index;

    for (int i = 0; i < (int) tabs.size(); ++i)
        tabs[(size_t) i].view->setVisible (i == current);

    tabs[(size_t) current].refresh();   // a page shows the disk as it is NOW, not as it was on open
    repaint();
}

void SetupPanel::resized()
{
    panel = getLocalBounds().withSizeKeepingCentre (panelW, panelH);

    auto r = panel.reduced (16, 12);

    auto header = r.removeFromTop (26);
    closeButton.setBounds (header.removeFromRight (24).reduced (1));

    // The window's name, then its pages. Each tab cell is sized to its own title.
    header.removeFromLeft (juce::roundToInt (
        theme::trackedWidth ("SETUP", theme::displayFont (13.0f), 0.15f)) + 28);

    const auto tabFont = theme::displayFont (10.0f);
    for (auto& tab : tabs)
    {
        const int w = juce::roundToInt (theme::trackedWidth (tab.title, tabFont, 0.15f)) + 20;
        tab.area = header.removeFromLeft (w);
    }

    r.removeFromTop (6);

    for (auto& tab : tabs)
        tab.view->setBounds (r);

    for (int i = 0; i < (int) tabs.size(); ++i)
        tabs[(size_t) i].view->setVisible (i == current);
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

    const auto header = panel.reduced (16, 12).removeFromTop (26);
    g.setColour (theme::tx);
    theme::drawTracked (g, "SETUP", header.toFloat(), theme::displayFont (13.0f), 0.15f,
                        juce::Justification::centredLeft);

    const auto tabFont = theme::displayFont (10.0f);
    for (int i = 0; i < (int) tabs.size(); ++i)
    {
        const auto& tab = tabs[(size_t) i];
        const bool active = i == current;

        g.setColour (active ? theme::tx : theme::txDim);
        theme::drawTracked (g, tab.title, tab.area.toFloat(), tabFont, 0.15f,
                            juce::Justification::centred);

        if (active)
        {
            g.setColour (theme::violet);
            g.fillRect (tab.area.withTrimmedTop (tab.area.getHeight() - 2).reduced (4, 0));
        }
    }
}

void SetupPanel::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < (int) tabs.size(); ++i)
        if (tabs[(size_t) i].area.contains (e.getPosition()))
        {
            selectTab (i);
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
