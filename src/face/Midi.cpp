// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "Midi.h"

#include "Standalone.h"
#include "../Product.h"

namespace orbitamp::face
{

namespace
{
    // In the editor's own pixels: a Sheet's content is not on the scaled stage. The inputs' column, then
    // the commands'.
    constexpr int inputsW = 320, commandsW = 400, pad = 16, titleH = 34, rowH = 30, gap = 14, lineH = 20;
    constexpr int commandsTop = 6;      // so the commands' first line reads level with the inputs' title
    constexpr int keyW = 86, keyH = 28, tabStop = 44;

    using standalone::devices;
    using panel::textPx, panel::ink, panel::dim, panel::faint, panel::textFont, panel::write;
}

//==============================================================================
bool MidiPanel::available() { return devices() != nullptr; }

MidiPanel::MidiPanel (juce::String text)
    : commands (std::move (text))
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

    // A TABLE'S ROWS STAND TWO ABREAST: the first half down the left, the rest down the right. The rows
    // that follow one another are one table; it is set when the line after its last has been read.
    size_t tableFrom = 0;
    const auto setTable = [this, &tableFrom]
    {
        const int count = (int) (lines.size() - tableFrom);
        const int deep = (count + 1) / 2;
        for (int i = 0; i < count; ++i)
        {
            auto& cell = lines[tableFrom + (size_t) i];
            cell.x = (i / deep) * (commandsW - pad * 2) / 2;
            cell.y = commandsHeight + (i % deep) * lineH;
        }
        commandsHeight += deep * lineH;
    };
    const auto inTable = [this] { return ! lines.empty() && lines.back().kind == Line::Kind::row; };

    for (const auto& raw : juce::StringArray::fromLines (commands.trimEnd()))
    {
        Line line;
        const auto text = raw.trimEnd();

        if (text.containsChar ('\t'))
        {
            if (! inTable())
                tableFrom = lines.size();
            line.kind   = Line::Kind::row;
            line.text   = text.upToFirstOccurrenceOf ("\t", false, false).trim();
            line.second = text.fromFirstOccurrenceOf ("\t", false, false).trim();
            line.height = lineH;
            lines.push_back (std::move (line));
            continue;
        }

        if (inTable())
            setTable();

        if (text.isEmpty())
        {
            line.height = lineH / 2;
        }
        else if (text.endsWithChar ('.'))
        {
            line.kind = Line::Kind::prose;
            juce::AttributedString words;
            words.append (text.toUpperCase(), textFont (textPx), dim);
            words.setWordWrap (juce::AttributedString::byWord);
            words.setLineSpacing (4.0f);
            line.wrapped.createLayout (words, (float) (commandsW - pad * 2));
            line.height = juce::roundToInt (std::ceil (line.wrapped.getHeight())) + 2;
        }
        else
        {
            line.kind   = Line::Kind::heading;
            line.text   = text;
            line.height = lineH + 2;
        }

        line.y = commandsHeight;
        commandsHeight += line.height;
        lines.push_back (std::move (line));
    }

    if (inTable())
        setTable();
}

int MidiPanel::commandsLeft() const
{
    return available() ? inputsW : 0;
}

int MidiPanel::inputsHeight() const
{
    return available() ? titleH + rowH * juce::jmax (1, inputs.size()) + pad - 6 : 0;
}

juce::Rectangle<int> MidiPanel::row (int index) const
{
    return { pad - 8, titleH + rowH * index, inputsW - (pad - 8) * 2, rowH };
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

    // The keys stand under the commands they copy and save, at the foot of the window whichever column
    // is the taller.
    const int height = juce::jmax (inputsHeight(), commandsTop + commandsHeight + gap + keyH + pad - 6);
    const int keysY = height - (pad - 6) - keyH;
    save.setBounds (commandsLeft() + commandsW - pad - keyW, keysY, keyW, keyH);
    copy.setBounds (save.getX() - 10 - keyW, keysY, keyW, keyH);
    setSize (commandsLeft() + commandsW, height);
    repaint();
}

void MidiPanel::paint (juce::Graphics& g)
{
    if (available())
    {
        write (g, "MIDI inputs", { pad, 4, inputsW - pad * 2, titleH - 8 }, textPx, faint);

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
        g.fillRect (inputsW, pad - 6, 1, getHeight() - (pad - 6) * 2);
    }

    const int half = (commandsW - pad * 2) / 2;
    for (const auto& line : lines)
    {
        const juce::Rectangle<int> area { commandsLeft() + pad + line.x, commandsTop + line.y,
                                          line.kind == Line::Kind::row ? half : commandsW - pad * 2, line.height };
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
    standalone::rememberDevices();
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

} // namespace orbitamp::face
