// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "SettingsList.h"
#include "library/DeviceListView.h"
#include "library/IrTreeView.h"
#include "library/MiniClose.h"

#include <vector>

namespace orbitamp
{

class AmpProcessor;

/** SETUP — the one window, opened by the toolbar's gear. Two pages across the top:

        LIBRARY   the packs and the IRs, one sub-tab per list
        VIEW      what this window shows — the eye's business, and only the eye's

    The libraries used to BE this window, one tab each, and there was nowhere for a setting to
    live except a popup menu hanging off the gear. They are one page now with their own sub-tabs,
    which is what freed the top row.

    There was a third page, EDITOR — which links the rig has at all. It has gone to the strip's
    own checklist, next to the chain it edits, which is where a rig is actually built: the two
    switches for what an emptied row does to the window went with it, since they only mean
    anything while a link is being taken out. Nothing here is a second copy of a switch that
    lives somewhere else; a setting with two homes is a setting that will read wrong in one of
    them.

    An overlay rather than a desktop window because a plugin editor is a guest: hosts reparent,
    hide, and destroy it freely, and a floating window can outlive or lose the editor it belongs
    to. Inside the component tree, the window scales with the zoom and dies with the editor. */
class SetupPanel final : public juce::Component
{
public:
    explicit SetupPanel (AmpProcessor&);

    /** Shows, refreshing the visible page — folders change behind a closed window. */
    void open();

    /** Forwarded from the device lists: devices came or went under the running blocks. */
    std::function<void()> onDevicesChanged;

    /** A switch on the VIEW page moved. What it means to the window — a resize, a relayout — is
        the editor's business; this page only writes the preference and says so. */
    std::function<void()> onViewChanged;

    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    /** One tab, at either level: its name, the component it shows, how it re-reads, and the cell
        it was given so a click can find it. */
    struct Tab
    {
        juce::String          title;
        juce::Component*      view;
        std::function<void()> refresh;
        juce::Rectangle<int>  area;
    };

    void selectPage (int index);
    void selectLibrary (int index);
    void buildViewPage();
    void layOutTabs (juce::Rectangle<int>& header, std::vector<Tab>& tabs, float height);
    void paintTabs (juce::Graphics&, const std::vector<Tab>&, int current, float height,
                    juce::Point<int> offset = {}) const;

    AmpProcessor& amp;

    std::vector<Tab> pages, libraries;
    int currentPage = 0, currentLibrary = 0;

    juce::Rectangle<int> panel;   // centred and CLAMPED; the rest of the bounds is scrim

    /** LIBRARY's own body: the sub-tabs pick which of these shows, and the one switch that is
        about the packs rather than about the window sits under them. */
    juce::Component libraryPage;
    DeviceListView  preampDevices { device::DeviceLibrary::Slot::preamp };
    DeviceListView  boostDevices  { device::DeviceLibrary::Slot::pedal };
    IrTreeView      irs;
    SettingsList    packSwitches;

    /** VIEW scrolls: whatever it grows to is more than a 440-tall window holds, and a page that
        cannot reach its own last row is a page with a hidden switch. */
    SettingsList   viewPage;
    juce::Viewport viewView;
    MiniClose    closeButton;

    static constexpr int panelW = 720;
    static constexpr int panelH = 440;
    static constexpr int headerH = 30;
    static constexpr int subHeaderH = 24;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SetupPanel)
};

} // namespace orbitamp
