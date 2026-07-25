#include "Chrome.h"

#include "../PluginProcessor.h"
#include "../PresetManager.h"

#include <felitronics/appkit/TextPrompt.h>

namespace orbitamp
{

namespace chrome = felitronics::appkit::chrome;

void Chrome::OrbitMark::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    felitronics::appkit::brand::drawOrbitRings (g, r.getCentreX(), r.getCentreY(),
                                                juce::jmin (r.getWidth(), r.getHeight()) * 0.62f);
}

Chrome::Chrome (AmpProcessor& processor)
    : amp (processor),
      // Seeded from the DEVICE palette, not from the family brand constants: appkit's own guardrail
      // says a product must supply its tuned colours here or every active-register frame drifts.
      theme { .fill       = theme::panel,
              .underline  = theme::hair,
              .accent     = theme::violet,
              .attention  = theme::orange,
              .text       = theme::tx,
              .textDim    = theme::txDim,
              .activeText = juce::Colours::white },
      blister (metrics, theme),
      compare (amp.history.numRegisters(), theme),
      preset (theme),
      underline (blister, metrics, theme)
{
    addAndMakeVisible (blister);
    blister.setMark (&mark);
    addAndMakeVisible (compare);
    addAndMakeVisible (preset);
    addAndMakeVisible (underline);   // the hairline sits over everything

    auto& history = amp.history;

    compare.setActions ({
        .recall   = [&history] (int i)         { history.switchTo (i); },
        .copy     = [&history] (int from, int to) { history.copyRegister (from, to); },
        .undo     = [&history]                 { history.undo(); },
        .redo     = [&history]                 { history.redo(); },
        .showMenu = [this] (int i)             { showRegisterMenu (i); },
    });

    preset.setActions ({ .showList = [this] { showPresetMenu(); } });
    preset.setCurrentName (presetName);

    // The engine tells the bar when anything moved. Cleared in the destructor — the history outlives
    // this window, and a dangling callback would fire into a destroyed component.
    history.onHistoryChanged = [this] { refreshModel(); };
    refreshModel();

    chrome::Cell badge;
    badge.component  = &blister;
    badge.fixedWidth = blister.preferredWidth ((int) metrics.blisterHeight);
    badge.height     = (int) metrics.blisterHeight;
    bar.add (badge);

    chrome::Cell abcd;
    abcd.component  = &compare;
    abcd.fixedWidth = compare.fixedWidth();
    bar.add (abcd);

    chrome::Cell name;
    name.component  = &preset;
    name.fixedWidth = preset.fixedWidth();
    bar.add (name);
}

Chrome::~Chrome()
{
    amp.history.onHistoryChanged = nullptr;
}

void Chrome::refreshModel()
{
    const auto& history = amp.history;

    chrome::CompareModel m;
    m.active = history.active();

    for (int i = 0; i < juce::jmin (history.numRegisters(), chrome::CompareModel::kMaxRegisters); ++i)
        m.registerEdited[(size_t) i] = history.registerEdited (i);

    m.canUndo   = history.canUndo();
    m.canRedo   = history.canRedo();
    m.undoLabel = history.peekUndoLabel();
    m.redoLabel = history.peekRedoLabel();

    compare.setModel (m);
}

void Chrome::resized()
{
    underline.setBounds (getLocalBounds());
    bar.layout (getLocalBounds().withHeight ((int) metrics.blisterHeight), (int) metrics.barHeight, 0);
}

void Chrome::showRegisterMenu (int index)
{
    auto& history = amp.history;

    juce::PopupMenu menu;
    for (int i = 0; i < history.numRegisters(); ++i)
        if (i != index)
            menu.addItem ("Copy " + compare.registerName (index) + " to " + compare.registerName (i),
                          [&history, index, i] { history.copyRegister (index, i); });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (compare.registerAnchor (index)));
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
    menu.addItem ("Save as...", [this] { savePresetAs(); });

    if (names.contains (presetName))
        menu.addItem ("Delete " + presetName, [this]
        {
            PresetManager::remove (presetName);
            presetName = "Default";
            preset.setCurrentName (presetName);
        });

    menu.addSeparator();
    menu.addItem ("Reveal preset folder", [] { PresetManager::directory().revealToUser(); });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (preset.nameAnchor()));
}

void Chrome::savePresetAs()
{
    felitronics::appkit::textPrompt ("Save preset", presetName, [this] (juce::String name)
    {
        name = name.trim();
        if (name.isEmpty())
            return;

        if (PresetManager::write (name, amp.apvts.copyState()))
        {
            presetName = name;
            preset.setCurrentName (presetName);
            amp.history.markSaved();
        }
    });
}

} // namespace orbitamp
