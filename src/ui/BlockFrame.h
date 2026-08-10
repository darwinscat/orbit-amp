#pragma once

#include "Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace orbitamp
{

/** The chassis every faceplate module shares: a framed, colour-coded, toggleable box with a title
    row. Orange = captured (a neural profile of real hardware), violet = DSP (rebuilt by us) — that
    distinction is the product's central honesty claim, so it is carried by the frame itself rather
    than left to each block.

    Blocks subclass this and fill `layOutContent` / `paintContent`; the frame owns nothing about
    what is inside. It holds no parameter and reaches nothing — the owner wires `onToggled`. */
class BlockFrame : public juce::Component
{
public:
    enum class Kind { captured, dsp };

    /** `withSwitch = false` is for the one kind of block that has no on/off at all — a listener
        like the tuner, which does nothing to the signal there could be a switch about. */
    BlockFrame (juce::String blockTitle, Kind blockKind, bool withSwitch = true)
        : title (std::move (blockTitle)), kind (blockKind), hasSwitch (withSwitch) {}

    /** The accent this block is coded with — orange when captured, violet when DSP. */
    juce::Colour accent() const
    {
        return kind == Kind::captured ? theme::orange : theme::violet;
    }

    /** A block whose nature depends on what is loaded into it — a power amp is a capture or our own
        DSP depending on the choice — recolours itself, so the frame keeps telling the truth. */
    void setKind (Kind newKind)
    {
        if (kind == newKind)
            return;

        kind = newKind;
        repaint();
    }

    bool isBlockOn() const noexcept { return on; }

    void setBlockOn (bool shouldBeOn, juce::NotificationType notify = juce::sendNotification)
    {
        if (on == shouldBeOn)
            return;

        on = shouldBeOn;
        setAlpha (on ? 1.0f : theme::offAlpha);   // dims children too — an off block stays interactive
        repaint();

        if (notify == juce::dontSendNotification)
            return;

        if (power != nullptr)
            power->setValueAsCompleteGesture (on ? 1.0f : 0.0f);

        if (onToggled != nullptr)
            onToggled (on);
    }

    /** Binds the frame's switch to the block's power parameter. The frame stays dumb — it holds the
        attachment, not the parameter, and never reads the tree. */
    void attachPower (juce::RangedAudioParameter& param)
    {
        power = std::make_unique<juce::ParameterAttachment> (param,
            [this] (float v) { setBlockOn (v > 0.5f, juce::dontSendNotification); });
        power->sendInitialUpdate();
    }

    std::function<void (bool)> onToggled;

    //==============================================================================
    void paint (juce::Graphics& g) override
    {
        const auto box     = boxArea().toFloat().reduced (theme::blockBorder * 0.5f);
        const auto topFill = kind == Kind::captured ? theme::capTop : theme::dspTop;

        juce::ColourGradient fill (topFill, 0.0f, box.getY(),
                                   kind == Kind::captured ? theme::capBot : theme::dspBot, 0.0f, box.getBottom(), false);
        g.setGradientFill (fill);
        g.fillRoundedRectangle (box, theme::radiusMd);

        // The frame is the block's colour code — orange for captured, violet for DSP — and it is
        // load-bearing: it is how the face states which stages are profiled hardware and which are
        // our own DSP. The spec mixes the accent into the hairline; the mixed result is the accent
        // at roughly half alpha, which keeps a switched-off block from reading as a hard outline.
        g.setColour (borderColour());
        g.drawRoundedRectangle (box, theme::radiusMd, theme::blockBorder);

        const auto titleBox = titleArea().toFloat();
        const auto sw       = switchArea().toFloat();

        // Sitting ON the border means masking the line behind it, fieldset-style. The mask is the
        // gradient's top colour, which is exactly what the border is drawn over up there.
        if (titleInBorder)
        {
            g.setColour (topFill);
            g.fillRect (titleBox.expanded (legendGap, 0.0f).withHeight (theme::blockBorder + 2.0f)
                                .withCentre ({ titleBox.getCentreX(), box.getY() }));
        }

        g.setColour (kind == Kind::captured ? theme::orange : theme::lilac);
        theme::drawTracked (g, title.toUpperCase(), titleBox, theme::displayFont (labelHeight),
                            0.15f, juce::Justification::centredLeft);

        // No mask for the switch: the border line runs straight into it and its own fill covers the
        // segment underneath, so the two read as one piece of hardware rather than a button parked
        // on a broken line.
        if (hasSwitch)
            paintSwitch (g, sw);

        paintContent (g);
    }

    void resized() override final
    {
        layOutHeader (headerWidgetArea());
        layOutContent (contentArea());
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (hasSwitch && switchArea().contains (e.getPosition()))
            setBlockOn (! on);
    }

protected:
    // ---- two independent placement switches; flip either back to false on its own --------------
    // titleInBorder : the block name interrupts the top border, fieldset-style, instead of taking
    //                 the first row.
    // switchInBorder: the on/off toggle rides the top border at the right end.
    // Both false restores the original title row exactly.
    static constexpr bool titleInBorder  = true;
    static constexpr bool switchInBorder = true;

    /** Everything below the title row — where a block puts its knobs and selectors. */
    juce::Rectangle<int> contentArea() const
    {
        auto r = boxArea().reduced (padX, padY);
        const int h = headerHeight();
        return h > 0 ? r.withTrimmedTop (h + headerGap) : r;
    }

    /** The first row, minus whatever is still living in it. With both the title and the switch moved
        onto the border, a block's header widget gets the full width. */
    juce::Rectangle<int> headerWidgetArea() const
    {
        auto h = headerArea();
        if (! titleInBorder)  h.removeFromLeft (titleWidth + headerGap);
        if (! switchInBorder) h.removeFromRight (switchWidth + headerGap);
        return h;
    }

    /** Zero when nothing is left in the first row: an empty row is dead space, so the content of a
        block without a header widget moves up instead. */
    virtual int headerHeight() const { return (titleInBorder && switchInBorder) ? 0 : 14; }

    virtual void layOutHeader (juce::Rectangle<int>) {}
    virtual void layOutContent (juce::Rectangle<int>) {}
    virtual void paintContent (juce::Graphics&) {}

private:
    /** Anything riding the top border needs room above the line for its own height, so the drawn
        frame starts lower than the component. Zero when nothing rides it. */
    static constexpr int topInset()
    {
        return (titleInBorder || switchInBorder) ? switchHeight / 2 + 1 : 0;
    }

    juce::Rectangle<int> boxArea() const { return getLocalBounds().withTrimmedTop (topInset()); }

    juce::Rectangle<int> headerArea() const
    {
        return boxArea().reduced (padX, padY).removeFromTop (headerHeight());
    }

    /** Sized to the WORD, not to a fixed column: the mask that opens the border has to be as wide as
        the text and no wider, or a short name like "EQ" leaves a hole in the line and a long one
        loses its right-hand gap. */
    juce::Rectangle<int> titleArea() const
    {
        if (! titleInBorder)
            return headerArea().withWidth (titleWidth);

        const int w = juce::roundToInt (theme::trackedWidth (title.toUpperCase(),
                                                             theme::displayFont (labelHeight), 0.15f)) + 1;

        return juce::Rectangle<int> (0, 0, w, switchHeight)
                 .withCentre ({ boxArea().getX() + borderInset + w / 2, boxArea().getY() });
    }

    juce::Rectangle<int> switchArea() const
    {
        if (! switchInBorder)
        {
            auto h = headerArea();
            return h.removeFromRight (switchWidth).withSizeKeepingCentre (switchWidth, switchHeight);
        }

        return juce::Rectangle<int> (0, 0, switchWidth, switchHeight)
                 .withCentre ({ boxArea().getRight() - borderInset - switchWidth / 2, boxArea().getY() });
    }

    juce::Colour borderColour() const
    {
        return accent().withAlpha (kind == Kind::captured ? 0.55f : 0.50f);
    }

    void paintSwitch (juce::Graphics& g, juce::Rectangle<float> sw) const
    {
        const float r = sw.getHeight() * 0.5f;

        g.setColour (on ? accent().withAlpha (0.5f).overlaidWith (juce::Colour (0xff26262f)) : juce::Colour (0xff26262f));
        g.fillRoundedRectangle (sw, r);

        // Outlined in the BORDER's colour, not the accent's — it is a piece of the frame, and a
        // brighter ring made it read as a separate control dropped on top. State is carried by the
        // fill and the knob, which is enough.
        g.setColour (borderColour());
        g.drawRoundedRectangle (sw.reduced (0.5f), r, theme::blockBorder);

        const float knob = sw.getHeight() - 5.0f;
        const float kx   = on ? sw.getRight() - knob - 2.0f : sw.getX() + 2.0f;
        g.setColour (on ? juce::Colours::white : juce::Colour (0xff8a8a96));
        g.fillEllipse (kx, sw.getCentreY() - knob * 0.5f, knob, knob);
    }

    // Design-unit metrics, straight from the visual spec. One scale factor on the editor turns these
    // into pixels, so nothing here is ever recomputed per zoom level.
    static constexpr int   padX         = 12;
    static constexpr int   padY         = 11;
    static constexpr int   titleWidth   = 62;   // enough for the longest block name at the label size
    static constexpr int   headerGap    = 12;
    static constexpr int   borderInset  = 18;   // clear of the corner radius, for anything on the line
    static constexpr float legendGap    = 6.0f; // breathing room each side of anything on the line
    static constexpr int   switchWidth  = 26;
    static constexpr int   switchHeight = 14;
    static constexpr float labelHeight  = 9.0f;

    juce::String title;
    Kind kind;
    bool hasSwitch = true;
    bool on = true;
    std::unique_ptr<juce::ParameterAttachment> power;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BlockFrame)
};

} // namespace orbitamp
