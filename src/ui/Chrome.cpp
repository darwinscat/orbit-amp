#include "Chrome.h"

#include "../PluginProcessor.h"
#include "../PresetManager.h"

#include <felitronics/appkit/TextPrompt.h>

#include <BinaryData.h>

namespace orbitamp
{

namespace appkit = felitronics::appkit;
namespace chrome = felitronics::appkit::chrome;

Chrome::Chrome (AmpProcessor& processor)
    : amp (processor),
      // Seeded from the DEVICE palette, not the family brand constants: appkit's own guardrail says
      // a product must supply its tuned colours or every active-register frame drifts off its face.
      theme { .fill       = theme::panel,
              .underline  = theme::hair,
              .accent     = theme::violet,
              .attention  = theme::orange,
              .text       = theme::tx,
              .textDim    = theme::txDim,
              .activeText = juce::Colours::white },
      brand (BinaryData::catlogo_svg, (size_t) BinaryData::catlogo_svgSize,
             BinaryData::MichromaRegular_ttf, (size_t) BinaryData::MichromaRegular_ttfSize,
             "OrbitAmp", "https://darwinscat.com/orbitamp?utm_source=orbitamp&utm_medium=plugin"),
      preset (theme)
{
    auto& history = amp.history;

    addAndMakeVisible (brand);

    for (auto* b : { &undo, &redo, &save, &saveAs, &trash })
    {
        b->colour      = theme::txDim;
        b->panelColour = theme::panel;
        addAndMakeVisible (*b);
    }

    undo.onClick = [&history] { history.undo(); };
    redo.onClick = [&history] { history.redo(); };

    // Save writes back to the loaded preset; Save As always asks for a name — the same split as the
    // sibling, so a working preset can be updated without a dialog every time.
    save.onClick   = [this] { savePreset (false); };
    saveAs.onClick = [this] { savePreset (true); };
    trash.onClick  = [this]
    {
        if (! PresetManager::names().contains (presetName))
            return;

        PresetManager::remove (presetName);
        presetName = "Default";
        preset.setCurrentName (presetName);
    };

    for (int i = 0; i < history.numRegisters(); ++i)
    {
        auto b = std::make_unique<chrome::RegisterButton>();
        b->setRegisterIndex (i);
        b->setButtonText (juce::String::charToString ((juce::juce_wchar) ('A' + i)));
        b->theme      = theme;
        b->onClick    = [&history, i] { history.switchTo (i); };
        b->onPopup    = [this, i] { showRegisterMenu (i); };
        b->onCopyDrop = [&history] (int from, int to) { history.copyRegister (from, to); };
        addAndMakeVisible (*b);
        registers.push_back (std::move (b));
    }

    preset.setActions ({ .showList = [this] { showPresetMenu(); } });
    preset.setCurrentName (presetName);
    addAndMakeVisible (preset);

    // The engine tells the header when anything moved. Cleared in the destructor — the history
    // outlives this window, and a dangling callback would fire into a destroyed component.
    history.onHistoryChanged = [this] { refreshModel(); };
    refreshModel();
}

Chrome::~Chrome()
{
    amp.history.onHistoryChanged = nullptr;
}

void Chrome::refreshModel()
{
    const auto& history = amp.history;

    for (int i = 0; i < (int) registers.size(); ++i)
    {
        registers[(size_t) i]->setToggleState (i == history.active(), juce::dontSendNotification);
        registers[(size_t) i]->setEdited (history.registerEdited (i));
    }

    undo.setEnabled (history.canUndo());
    redo.setEnabled (history.canRedo());
}

void Chrome::resized()
{
    auto header = getLocalBounds();

    // ---- right cluster: file actions pinned right, then registers, then the preset name ----
    auto right    = header.removeFromRight (440);
    auto rightBar = right.withSizeKeepingCentre (right.getWidth(), controlBand);

    trash .setBounds (rightBar.removeFromRight (40).reduced (4, 7));
    saveAs.setBounds (rightBar.removeFromRight (40).reduced (4, 7));
    save  .setBounds (rightBar.removeFromRight (40).reduced (4, 7));

    auto snapArea = rightBar.removeFromLeft (124).reduced (6, 8);
    const int count = (int) registers.size();
    const int w     = count > 0 ? snapArea.getWidth() / count : 0;

    for (int i = 0; i < count; ++i)
        registers[(size_t) i]->setBounds ((i < count - 1 ? snapArea.removeFromLeft (w) : snapArea).reduced (2, 0));

    preset.setBounds (rightBar.reduced (7));

    // ---- brand on the left, sized to its content so the click area hugs the text ----
    header.removeFromLeft (4);
    brand.setBounds (header.withWidth (header.getWidth()));   // height first: contentRight() reads it
    const int brandWidth = juce::jmin (header.getWidth(), juce::roundToInt (brand.contentRight()));
    brand.setBounds (header.removeFromLeft (brandWidth));
    brand.clickRight = brandWidth;

    // ---- undo / redo in the gap between the brand and the register cluster ----
    header.removeFromLeft (14);
    auto leftBar = header.withSizeKeepingCentre (header.getWidth(), controlBand);
    undo.setBounds (leftBar.removeFromLeft (34).reduced (3, 8));
    redo.setBounds (leftBar.removeFromLeft (34).reduced (3, 8));
}

void Chrome::showRegisterMenu (int index)
{
    auto& history = amp.history;

    juce::PopupMenu menu;
    for (int i = 0; i < history.numRegisters(); ++i)
        if (i != index)
        {
            const auto from = juce::String::charToString ((juce::juce_wchar) ('A' + index));
            const auto to   = juce::String::charToString ((juce::juce_wchar) ('A' + i));
            menu.addItem ("Copy " + from + " to " + to, [&history, index, i] { history.copyRegister (index, i); });
        }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (registers[(size_t) index].get()));
}

void Chrome::showPresetMenu()
{
    juce::PopupMenu menu;

    const auto names = PresetManager::names();

    if (names.isEmpty())
        menu.addItem ("No presets yet", false, false, [] {});

    for (const auto& n : names)
        menu.addItem (n, true, n == presetName, [this, n]
        {
            const auto tree = PresetManager::read (n);
            if (! tree.isValid())
                return;

            // Through the history, so loading a preset is one undoable step rather than a silent
            // replacement of everything behind undo's back.
            amp.history.applyEdit (amp.history.active(), tree, "Load " + n);
            presetName = n;
            preset.setCurrentName (presetName);
        });

    menu.addSeparator();
    menu.addItem ("Reveal preset folder", [] { PresetManager::directory().revealToUser(); });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (preset.nameAnchor()));
}

void Chrome::savePreset (bool forceNewName)
{
    auto store = [this] (const juce::String& name)
    {
        if (! PresetManager::write (name, amp.apvts.copyState()))
            return;

        presetName = name;
        preset.setCurrentName (presetName);
        amp.history.markSaved();
    };

    if (! forceNewName && PresetManager::names().contains (presetName))
    {
        store (presetName);
        return;
    }

    appkit::textPrompt ("Save preset", presetName, [store] (juce::String name)
    {
        name = name.trim();
        if (name.isNotEmpty())
            store (name);
    });
}

} // namespace orbitamp
