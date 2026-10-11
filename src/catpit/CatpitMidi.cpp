// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "CatpitMidi.h"

#include "../Product.h"

#include <CatpitData.h>

#if defined (JucePlugin_Build_Standalone) && JucePlugin_Build_Standalone
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
 #define CATPIT_STANDS_ALONE 1
#else
 #define CATPIT_STANDS_ALONE 0
#endif

namespace orbitamp::catpit
{

namespace
{
    // In the editor's own pixels: a Sheet's content is not on the scaled stage.
    constexpr int width = 400, pad = 16, titleH = 34, rowH = 30, gap = 14, lineH = 20;
    constexpr int keyW = 86, keyH = 28, tabStop = 44;
    constexpr float textPx = 13.0f, tracking = 0.08f;

    const juce::Colour ink   { 0xffece9f6 };
    const juce::Colour dim   = juce::Colour (0xffc9b8f5).withAlpha (0.85f);
    const juce::Colour faint = juce::Colour (0xffc9b8f5).withAlpha (0.45f);

    /** The standalone's device manager: the one thing that opens MIDI inputs and hands what they send
        to the engine. Null inside a host. */
    juce::AudioDeviceManager* devices()
    {
       #if CATPIT_STANDS_ALONE
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
            return &holder->deviceManager;
       #endif
        return nullptr;
    }

    /** The standalone writes its devices down when it quits; a choice made here is written at once. */
    void remember()
    {
       #if CATPIT_STANDS_ALONE
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
            holder->saveAudioDeviceState();
       #endif
    }

    juce::Font textFont (float px) { return labelFont (px).withExtraKerningFactor (tracking); }

    void write (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area, float px, juce::Colour colour,
                juce::Justification where = juce::Justification::centredLeft)
    {
        g.setColour (colour);
        g.setFont (textFont (px));
        g.drawText (text.toUpperCase(), area, where, true);
    }
}

//==============================================================================
MidiPanel::WordKey::WordKey (juce::String w) : word (std::move (w))
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void MidiPanel::WordKey::flash (const juce::String& text)
{
    said = text;
    startTimer (1400);
    repaint();
}

void MidiPanel::WordKey::timerCallback()
{
    stopTimer();
    said.clear();
    repaint();
}

void MidiPanel::WordKey::paint (juce::Graphics& g)
{
    const auto face = getLocalBounds().toFloat().reduced (0.6f);
    const bool over = isMouseOver();
    if (over)
    {
        g.setColour (violet.withAlpha (0.18f));
        g.fillRoundedRectangle (face, 5.0f);
    }
    g.setColour (violet.withAlpha (over ? 0.9f : 0.55f));
    g.drawRoundedRectangle (face, 5.0f, 1.2f);
    write (g, said.isNotEmpty() ? said : word, getLocalBounds(), textPx, said.isNotEmpty() ? orange : (over ? ink : dim),
           juce::Justification::centred);
}

void MidiPanel::WordKey::mouseUp (const juce::MouseEvent& e)
{
    if (getLocalBounds().contains (e.getPosition()) && onClick)
        onClick();
}

//==============================================================================
bool MidiPanel::available() { return devices() != nullptr; }

MidiPanel::MidiPanel()
    : commands (juce::String::fromUTF8 (CatpitData::midi_txt, CatpitData::midi_txtSize))
{
    layOutCommands();

    addAndMakeVisible (copy);
    addAndMakeVisible (save);
    copy.onClick = [this]
    {
        juce::SystemClipboard::copyTextToClipboard (commands);
        copy.flash ("Copied");
    };
    save.onClick = [this] { saveCommands(); };

    refresh();
    startTimerHz (2);
}

/** The file as rows to draw. The three kinds are told apart by how the line is written: a tab makes a
    row of a table, a full stop at the end makes prose, and what is left is a heading. */
void MidiPanel::layOutCommands()
{
    lines.clear();
    commandsHeight = 0;

    for (const auto& raw : juce::StringArray::fromLines (commands.trimEnd()))
    {
        Line line;
        const auto text = raw.trimEnd();

        if (text.isEmpty())
        {
            line.height = lineH / 2;
        }
        else if (text.containsChar ('\t'))
        {
            line.kind   = Line::Kind::row;
            line.text   = text.upToFirstOccurrenceOf ("\t", false, false).trim();
            line.second = text.fromFirstOccurrenceOf ("\t", false, false).trim();
            line.height = lineH;
        }
        else if (text.endsWithChar ('.'))
        {
            line.kind = Line::Kind::prose;
            juce::AttributedString words;
            words.append (text.toUpperCase(), textFont (textPx), dim);
            words.setWordWrap (juce::AttributedString::byWord);
            words.setLineSpacing (4.0f);
            line.wrapped.createLayout (words, (float) (width - pad * 2));
            line.height = juce::roundToInt (std::ceil (line.wrapped.getHeight())) + 2;
        }
        else
        {
            line.kind   = Line::Kind::heading;
            line.text   = text;
            line.height = lineH + 2;
        }

        commandsHeight += line.height;
        lines.push_back (std::move (line));
    }
}

int MidiPanel::inputsHeight() const
{
    return available() ? titleH + rowH * juce::jmax (1, inputs.size()) + gap : pad - 6;
}

juce::Rectangle<int> MidiPanel::row (int index) const
{
    return { pad - 8, titleH + rowH * index, width - (pad - 8) * 2, rowH };
}

int MidiPanel::rowAt (juce::Point<int> p) const
{
    for (int i = 0; i < inputs.size(); ++i)
        if (row (i).contains (p))
            return i;
    return -1;
}

void MidiPanel::refresh()
{
    juce::Array<juce::MidiDeviceInfo> found;
    juce::Array<bool> on;
    if (auto* manager = devices())
    {
        found = juce::MidiInput::getAvailableDevices();
        for (const auto& input : found)
            on.add (manager->isMidiInputDeviceEnabled (input.identifier));
    }

    if (found == inputs && on == listening && getHeight() > 0)
        return;

    inputs    = std::move (found);
    listening = std::move (on);
    hovered   = -1;

    const int keysY = inputsHeight() + commandsHeight + gap;
    save.setBounds (width - pad - keyW, keysY, keyW, keyH);
    copy.setBounds (save.getX() - 10 - keyW, keysY, keyW, keyH);
    setSize (width, keysY + keyH + pad - 6);
    repaint();
}

void MidiPanel::paint (juce::Graphics& g)
{
    if (available())
    {
        write (g, "MIDI inputs", { pad, 4, width - pad * 2, titleH - 8 }, textPx, faint);

        if (inputs.isEmpty())
            write (g, "None found", row (0).withTrimmedLeft (8), 15.0f, dim);

        for (int i = 0; i < inputs.size(); ++i)
        {
            const auto r = row (i);
            const bool on = listening[i];

            if (i == hovered)
            {
                g.setColour (violet.withAlpha (0.14f));
                g.fillRoundedRectangle (r.toFloat(), 5.0f);
            }

            // The lamp: the channel names' own, lit orange while the input is listened to.
            g.setColour (on ? orange : juce::Colour (0xff3a2a22));
            g.fillRoundedRectangle (juce::Rectangle<float> (12.0f, 6.0f).withCentre ({ (float) r.getX() + 16.0f, (float) r.getCentreY() }), 2.0f);

            write (g, inputs[i].name, r.withTrimmedLeft (32).withTrimmedRight (8), 15.0f, on ? ink : dim);
        }

        g.setColour (violet.withAlpha (0.28f));
        g.fillRect (pad, inputsHeight() - gap / 2, width - pad * 2, 1);
    }

    int y = inputsHeight();
    for (const auto& line : lines)
    {
        const juce::Rectangle<int> area { pad, y, width - pad * 2, line.height };
        switch (line.kind)
        {
            case Line::Kind::gap:      break;
            case Line::Kind::heading:  write (g, line.text, area, textPx, faint); break;
            case Line::Kind::prose:    line.wrapped.draw (g, area.toFloat()); break;
            case Line::Kind::row:
                write (g, line.text,   area.withWidth (tabStop), textPx, dim);
                write (g, line.second, area.withTrimmedLeft (tabStop), textPx, ink);
                break;
        }
        y += line.height;
    }
}

void MidiPanel::mouseMove (const juce::MouseEvent& e)
{
    if (const int now = rowAt (e.getPosition()); now != hovered)
    {
        hovered = now;
        repaint();
    }
}

void MidiPanel::mouseExit (const juce::MouseEvent&)
{
    if (hovered >= 0)
    {
        hovered = -1;
        repaint();
    }
}

void MidiPanel::mouseDown (const juce::MouseEvent& e)
{
    const int i = rowAt (e.getPosition());
    auto* manager = devices();
    if (i < 0 || manager == nullptr)
        return;

    const auto id = inputs[i].identifier;
    manager->setMidiInputDeviceEnabled (id, ! manager->isMidiInputDeviceEnabled (id));
    remember();
    refresh();
    hovered = i;
    repaint();
}

/** The file, written out as it is — the same text COPY puts on the clipboard. */
void MidiPanel::saveCommands()
{
    const auto suggested = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                               .getChildFile (juce::String (product::name) + " MIDI commands.txt");
    chooser = std::make_unique<juce::FileChooser> ("Save the MIDI commands", suggested, "*.txt");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe = juce::Component::SafePointer<MidiPanel> (this)] (const juce::FileChooser& picked)
                          {
                              const auto file = picked.getResult();
                              if (safe == nullptr || file == juce::File())
                                  return;
                              safe->save.flash (file.replaceWithText (safe->commands, false, false, "\n") ? "Saved" : "Not saved");
                          });
}

} // namespace orbitamp::catpit
