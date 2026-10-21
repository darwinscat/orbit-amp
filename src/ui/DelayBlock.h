// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "BlockFrame.h"
#include "Knob.h"
#include "VoicingSelector.h"

namespace orbitamp
{

class AmpProcessor;

/** The delay — the echo between the preamp's console and the reverb.

    The TIME stands on the border where the captured blocks stand their device combo: the sync
    division as a pill (1/4, 1/8T…), with FREE at the bottom of its menu for the millisecond
    specialist, and — in a host — whose tempo the division counts in: the HOST's, the default, or
    the block's OWN.

    LAID OUT AS THE REVERB IS, to the unit: the picture takes the whole box and the hands ride its
    top. MIX is the hero, the reverb's MIX in the reverb's place at the reverb's size; REPEATS, DARK
    and OFFSET stand small at its left hand, each the size of the reverb's DECAY and PRE — how many,
    how dark, how wide, in the loop's own order. Under those three, one line: the one typed control
    on the face — the BPM field (which reads HOST and the host's tempo, and does not move, while the
    host conducts), turning into the MS field when the time goes free.

    The picture is the COMB: one tooth per repeat, standing where the repeats will land — the
    spacing is the glided time (turn the knob and the comb slides with the motor), the heights
    fall by REPEATS, the ink dulls along the tail by DARK, and OFFSET stands a lilac twin
    beside every tooth. The pulse is live: what walks into the line now lights tooth n when
    its n-th repeat is due — a note travels down the comb as it echoes. */
class DelayBlock final : public BlockFrame,
                         private juce::Timer
{
public:
    explicit DelayBlock (AmpProcessor&);
    ~DelayBlock() override;

private:
    void layOutContent (juce::Rectangle<int>) override;
    void timerCallback() override;
    void paintComb (juce::Graphics&, juce::Rectangle<float>);

    /** Re-reads sync + division into the border pill and the corner field — called from every
        attachment echo, so a restored session and a host automation both redress the face. */
    void applyTimeMode();

    bool syncOn() const;
    float plain (const char* id) const;

    /** The sync is on, the block follows the host, and the host is reporting a tempo — the one case
        the corner field shows a number that is not the block's to move. */
    bool hostConducts() const;

    /** The time pill's menu: the division ladder, FREE, and in a host whose tempo the ladder
        counts in — the host's or the block's own BPM. */
    void showTimeMenu();

    float shownHostTempo = -1.0f;   // what the field last showed of the host's tempo, 0 for none


    /** The comb's surface: dumb, the block paints through it — the reverb picture's pattern. */
    struct CombView final : public juce::Component
    {
        std::function<void (juce::Graphics&, juce::Rectangle<float>)> paintBody;

        void paint (juce::Graphics& g) override
        {
            if (paintBody) paintBody (g, getLocalBounds().toFloat());
        }
    };

    /** The typed number: a pill that reads "120 BPM" or "350 MS", drags vertically, and opens
        an editor on double-click. Dumb — text and gestures come from the block. */
    struct NumberField final : public juce::Component
    {
        std::function<juce::String()> text;
        std::function<void (int phase, float dy)> onDrag;   // 0 = down, 1 = drag, 2 = up
        std::function<void (const juce::String&)> onTyped;

        NumberField() { setMouseCursor (juce::MouseCursor::UpDownResizeCursor); }

        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat();
            g.setColour (juce::Colour (0xff101016));
            g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
            g.setColour (theme::hair2);
            g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.5f, 1.0f);

            if (editor == nullptr && text != nullptr)
            {
                g.setColour (hover ? theme::tx : theme::txDim);
                theme::drawTracked (g, text(), r, theme::displayFont (fontH), 0.04f,
                                    juce::Justification::centred);
            }
        }

        void mouseEnter (const juce::MouseEvent&) override { hover = true;  repaint(); }
        void mouseExit  (const juce::MouseEvent&) override { hover = false; repaint(); }

        void mouseDown (const juce::MouseEvent&) override        { if (onDrag) onDrag (0, 0.0f); }
        void mouseDrag (const juce::MouseEvent& e) override      { if (onDrag) onDrag (1, (float) -e.getDistanceFromDragStartY()); }
        void mouseUp   (const juce::MouseEvent& e) override
        {
            if (onDrag) onDrag (2, 0.0f);
            if (e.getNumberOfClicks() >= 2) beginEdit();
        }

        void beginEdit()
        {
            editor = std::make_unique<juce::TextEditor>();
            editor->setFont (theme::displayFont (fontH));
            editor->setJustification (juce::Justification::centred);
            editor->setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff101016));
            editor->setColour (juce::TextEditor::textColourId, theme::tx);
            editor->setColour (juce::TextEditor::outlineColourId, theme::hair2);
            editor->setColour (juce::TextEditor::focusedOutlineColourId, theme::violet);
            editor->setInputRestrictions (7, "0123456789.");
            editor->setText (text != nullptr ? text().upToFirstOccurrenceOf (" ", false, false)
                                             : juce::String(), false);

            editor->onReturnKey = [this]
            {
                const auto typed = editor->getText();
                endEdit();
                if (onTyped) onTyped (typed);
            };
            editor->onEscapeKey = [this] { endEdit(); };
            editor->onFocusLost = [this] { endEdit(); };

            addAndMakeVisible (*editor);
            editor->setBounds (getLocalBounds());
            editor->selectAll();
            editor->grabKeyboardFocus();
            repaint();
        }

        void endEdit()
        {
            if (editor == nullptr)
                return;

            // The editor calls this from its own callbacks; it cannot be deleted mid-call.
            auto* e = editor.release();
            removeChildComponent (e);
            juce::MessageManager::callAsync ([e] { delete e; });
            repaint();
        }

        std::unique_ptr<juce::TextEditor> editor;
        bool hover = false;

        static constexpr float fontH = 13.0f;   // the face's floor for anything read
    };

    static constexpr int maxKnobSide = 96;   // the reverb's hero, to the unit (ReverbBlock::maxKnobSide)
    static constexpr int miniSide    = 42;   // the reverb's DECAY and PRE

    AmpProcessor& amp;

    VoicingSelector timeSel;
    Knob mix     { "Mix",     theme::violet, 0 };
    Knob repeats { "Repeats", theme::violet, 0 };
    Knob dark    { "Dark",    theme::violet, 0 };
    Knob offset  { "Offset",  theme::violet, 0 };

    CombView    view;
    NumberField field;

    float dragStart = 0.0f;

    /** The pulse's memory: the input envelope, one bucket per timer tick. Tooth n reads the
        bucket n repeats of the current time ago — the note walking down the comb. */
    static constexpr int envTicksPerSecond = 30;
    static constexpr int envHistSize = 256;   // ~8.5 s — past that a tooth simply stays quiet
    std::array<float, (size_t) envHistSize> envHist {};
    int   envPos = 0;
    float envSmooth = 0.0f;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment,
                                                                          repeatsAttachment,
                                                                          darkAttachment,
                                                                          offsetAttachment;
    std::unique_ptr<juce::ParameterAttachment> syncAtt, divAtt, bpmAtt, timeAtt, hostTempoAtt;

    friend class WebFaceplate;   // the site's kit reads where the parts stand (WebFaceplate.h)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DelayBlock)
};

} // namespace orbitamp
