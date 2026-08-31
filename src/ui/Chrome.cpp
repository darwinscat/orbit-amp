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
              // No rule under the wordmark. It was drawing a line across a third of the toolbar
              // that separated nothing from nothing — the blocks below have their own frames, and
              // the eye reads a hairline as "these two things are apart" when they are not.
              .underline  = juce::Colours::transparentBlack,
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
    mark = juce::Drawable::createFromImageData (BinaryData::OrbitAmpmark_svg,
                                                (size_t) BinaryData::OrbitAmpmark_svgSize);
    // The wordmark speaks up without growing the cat or the mark.
    brand.wordmarkScale = 0.50f;
    brand.bylineScale   = 0.30f;

    for (auto* b : { &undo, &redo, &save, &saveAs, &trash, &gear })
    {
        b->colour      = theme::txDim;
        b->panelColour = theme::panel;
        addAndMakeVisible (*b);
    }

    undo.onClick = [&history] { history.undo(); };
    redo.onClick = [&history] { history.redo(); };

    gear.onClick = [this]
    {
        if (onGear)
            onGear (gear.getScreenBounds().getBottomLeft());
    };

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
        b->textHeight = 14.5f;   // louder letters, same cells
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

void Chrome::paintOverChildren (juce::Graphics& g)
{
    // BrandHeader still paints the family's FIRST product mark — orbit-capture's chevrons, hardcoded
    // (see appkit Brand.h drawOrbit). Until the header takes a per-product painter, OrbitAmp lays
    // its own mark over that slot: the same disc, the same geometry as BrandHeader::paint, so the
    // cover is exact and nothing of the borrowed mark survives.
    if (mark == nullptr)
        return;

    const auto b  = brand.getBounds().toFloat();
    const float h = b.getHeight();
    const float d = h * 0.86f;
    mark->drawWithin (g, { b.getX() + h + 6.0f, b.getCentreY() - d * 0.5f, d, d },
                      juce::RectanglePlacement::centred, 1.0f);
}

void Chrome::resized()
{
    auto header = getLocalBounds();

    // ---- brand on the left, sized to its content so the click area hugs the text.
    //      ONE unit taller than the strip: the kit's header draws a hairline along its own bottom
    //      edge, with no way to ask it not to, and that line was cutting under the logo for no
    //      reason a block below could give — a unit past the strip's edge, the parent clips it. ----
    brand.setBounds (header.withHeight (header.getHeight() + 1));   // height first: contentRight() reads it
    const int brandWidth = juce::jmin (header.getWidth() - 360, juce::roundToInt (brand.contentRight()));
    brand.setBounds (header.removeFromLeft (brandWidth).withHeight (header.getHeight() + 1));
    brand.clickRight = brandWidth;

    // ---- undo / redo right after the brand ----
    header.removeFromLeft (10);
    auto leftBar = header.withSizeKeepingCentre (header.getWidth(), controlBand);
    undo.setBounds (leftBar.removeFromLeft (34).reduced (3, 6));
    redo.setBounds (leftBar.removeFromLeft (34).reduced (3, 6));
    header.removeFromLeft (68 + 8);

    // ---- right cluster takes the REST: the gear at the very edge, file actions, registers, and
    //      the preset name in whatever is left — the one cell here that can give. ----
    auto rightBar = header.withSizeKeepingCentre (header.getWidth(), controlBand);

    gear.setBounds (rightBar.removeFromRight (40).reduced (4, 7));
    rightBar.removeFromRight (6);   // the gear opens a window, the rest edit the preset — a seam
    trash .setBounds (rightBar.removeFromRight (40).reduced (4, 7));
    saveAs.setBounds (rightBar.removeFromRight (40).reduced (4, 7));
    save  .setBounds (rightBar.removeFromRight (40).reduced (4, 7));

    auto snapArea = rightBar.removeFromLeft (124).reduced (6, 8);
    const int count = (int) registers.size();
    const int w     = count > 0 ? snapArea.getWidth() / count : 0;

    for (int i = 0; i < count; ++i)
        registers[(size_t) i]->setBounds ((i < count - 1 ? snapArea.removeFromLeft (w) : snapArea).reduced (2, 0));

    preset.setBounds (rightBar.reduced (7));
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

    // Back to what a fresh instance is — every parameter to its default, through the history, so
    // the reset is one undoable step like any preset load.
    menu.addItem ("Reset to default", [this]
    {
        auto tree = amp.apvts.copyState();

        for (auto* p : amp.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                auto child = tree.getChildWithProperty ("id", rp->paramID);
                if (child.isValid())
                    child.setProperty ("value", rp->convertFrom0to1 (rp->getDefaultValue()), nullptr);
            }

        amp.history.applyEdit (amp.history.active(), tree, "Reset to default");
        presetName = "Default";
        preset.setCurrentName (presetName);
    });

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
