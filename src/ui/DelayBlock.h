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
    specialist. Under it, in the corner, the one typed control on the face — the BPM field the
    standalone conducts by (a host's tempo outranks it), which turns into the MS field when the
    time goes free. Mix is the hero; REPEATS and DARK stand small at its left hand, OFFSET —
    the block's stereo — small in the opposite corner. The picture is the comb of repeats
    (its own step; the well waits dark for now). */
class DelayBlock final : public BlockFrame
{
public:
    explicit DelayBlock (AmpProcessor&);
    ~DelayBlock() override;

private:
    void layOutContent (juce::Rectangle<int>) override;

    /** Re-reads sync + division into the border pill and the corner field — called from every
        attachment echo, so a restored session and a host automation both redress the face. */
    void applyTimeMode();

    bool syncOn() const;
    float plain (const char* id) const;

    /** The comb's future home: the dark well, framed like every picture. */
    struct WellView final : public juce::Component
    {
        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat();
            g.setColour (juce::Colour (0xff101016));
            g.fillRoundedRectangle (r, theme::radiusSm);
            g.setColour (theme::hair2);
            g.drawRoundedRectangle (r.reduced (0.5f), theme::radiusSm, 1.0f);
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
                theme::drawTracked (g, text(), r, theme::displayFont (11.0f), 0.08f,
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
            editor->setFont (theme::displayFont (11.0f));
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
    };

    static constexpr int maxKnobSide = 84;

    AmpProcessor& amp;

    VoicingSelector timeSel;
    Knob mix     { "Mix",     theme::violet, 0 };
    Knob repeats { "Repeats", theme::violet, 0 };
    Knob dark    { "Dark",    theme::violet, 0 };
    Knob offset  { "Offset",  theme::violet, 0 };

    WellView    view;
    NumberField field;

    float dragStart = 0.0f;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment,
                                                                          repeatsAttachment,
                                                                          darkAttachment,
                                                                          offsetAttachment;
    std::unique_ptr<juce::ParameterAttachment> syncAtt, divAtt, bpmAtt, timeAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DelayBlock)
};

} // namespace orbitamp
