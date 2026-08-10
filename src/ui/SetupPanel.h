#pragma once

#include "library/DeviceListView.h"
#include "library/IrTreeView.h"
#include "library/MiniClose.h"

#include <vector>

namespace orbitamp
{

/** The SETUP window, opened from the toolbar gear: an overlay over the whole editor, one panel on
    a scrim, its pages behind tabs across the top. One tab per library a block draws from —
    PREAMP, BOOST, POWER AMP, and the cabinet's IR tree — so each list is curated where it plays,
    and the strip is built for the pages that follow. Closes on ✕, Esc, or a click on the scrim.

    An overlay rather than a desktop window because a plugin editor is a guest: hosts reparent,
    hide, and destroy it freely, and a floating window can outlive or lose the editor it belongs
    to. Inside the component tree, the window scales with the zoom and dies with the editor. */
class SetupPanel final : public juce::Component
{
public:
    SetupPanel();

    /** Shows, refreshing the visible page — folders change behind a closed window. */
    void open();

    /** Forwarded from the device tabs: devices came or went under the running blocks. */
    std::function<void()> onDevicesChanged;

    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    /** One page: its name in the strip, the component that is the page, and how it re-reads. */
    struct Tab
    {
        juce::String          title;
        juce::Component*      view;
        std::function<void()> refresh;
        juce::Rectangle<int>  area;   // the strip cell, for hit-testing and the underline
    };

    void selectTab (int index);

    std::vector<Tab> tabs;
    int current = 0;

    juce::Rectangle<int> panel;   // centred; the rest of the bounds is scrim

    DeviceListView preampDevices { device::DeviceLibrary::Slot::preamp };
    DeviceListView boostDevices  { device::DeviceLibrary::Slot::pedal };
    DeviceListView powerDevices  { device::DeviceLibrary::Slot::poweramp };
    IrTreeView     irs;
    MiniClose      closeButton;

    static constexpr int panelW = 720;
    static constexpr int panelH = 440;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SetupPanel)
};

} // namespace orbitamp
