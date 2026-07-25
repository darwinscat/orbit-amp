#pragma once

#include "Theme.h"

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

    BlockFrame (juce::String blockTitle, Kind blockKind)
        : title (std::move (blockTitle)), kind (blockKind) {}

    /** The accent this block is coded with — orange when captured, violet when DSP. */
    juce::Colour accent() const
    {
        return kind == Kind::captured ? theme::orange : theme::violet;
    }

    bool isBlockOn() const noexcept { return on; }

    void setBlockOn (bool shouldBeOn, juce::NotificationType notify = juce::sendNotification)
    {
        if (on == shouldBeOn)
            return;

        on = shouldBeOn;
        setAlpha (on ? 1.0f : theme::offAlpha);   // dims children too — an off block stays interactive
        repaint();

        if (notify != juce::dontSendNotification && onToggled != nullptr)
            onToggled (on);
    }

    std::function<void (bool)> onToggled;

    //==============================================================================
    void paint (juce::Graphics& g) override
    {
        const auto box = getLocalBounds().toFloat().reduced (theme::blockBorder * 0.5f);

        juce::ColourGradient fill (kind == Kind::captured ? theme::capTop : theme::dspTop, 0.0f, box.getY(),
                                   kind == Kind::captured ? theme::capBot : theme::dspBot, 0.0f, box.getBottom(), false);
        g.setGradientFill (fill);
        g.fillRoundedRectangle (box, theme::radiusMd);

        // The spec mixes the accent into the hairline for the border; the mixed result is the accent
        // at roughly half alpha, which is what keeps an off block from reading as a hard outline.
        g.setColour (accent().withAlpha (kind == Kind::captured ? 0.55f : 0.50f));
        g.drawRoundedRectangle (box, theme::radiusMd, theme::blockBorder);

        auto header = headerArea().toFloat();
        const auto sw = switchArea().toFloat();

        g.setColour (kind == Kind::captured ? theme::orange : theme::lilac);
        theme::drawTracked (g, title.toUpperCase(),
                            header.withTrimmedRight (header.getRight() - sw.getX()),
                            theme::displayFont (labelHeight), 0.15f, juce::Justification::centredLeft);

        paintSwitch (g, sw);
        paintContent (g);
    }

    void resized() override final
    {
        layOutContent (contentArea());
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (switchArea().contains (e.getPosition()))
            setBlockOn (! on);
    }

protected:
    /** Everything below the title row — where a block puts its knobs and selectors. */
    juce::Rectangle<int> contentArea() const
    {
        return getLocalBounds().reduced (padX, padY).withTrimmedTop (headerHeight + headerGap);
    }

    virtual void layOutContent (juce::Rectangle<int>) {}
    virtual void paintContent (juce::Graphics&) {}

private:
    juce::Rectangle<int> headerArea() const
    {
        return getLocalBounds().reduced (padX, padY).removeFromTop (headerHeight);
    }

    juce::Rectangle<int> switchArea() const
    {
        auto h = headerArea();
        return h.removeFromRight (switchWidth).withSizeKeepingCentre (switchWidth, switchHeight);
    }

    void paintSwitch (juce::Graphics& g, juce::Rectangle<float> sw) const
    {
        const float r = sw.getHeight() * 0.5f;

        g.setColour (on ? accent().withAlpha (0.5f).overlaidWith (juce::Colour (0xff26262f)) : juce::Colour (0xff26262f));
        g.fillRoundedRectangle (sw, r);
        g.setColour (on ? accent() : theme::hair2);
        g.drawRoundedRectangle (sw.reduced (0.5f), r, 1.0f);

        const float knob = sw.getHeight() - 5.0f;
        const float kx   = on ? sw.getRight() - knob - 2.0f : sw.getX() + 2.0f;
        g.setColour (on ? juce::Colours::white : juce::Colour (0xff8a8a96));
        g.fillEllipse (kx, sw.getCentreY() - knob * 0.5f, knob, knob);
    }

    // Design-unit metrics, straight from the visual spec. One scale factor on the editor turns these
    // into pixels, so nothing here is ever recomputed per zoom level.
    static constexpr int   padX         = 12;
    static constexpr int   padY         = 11;
    static constexpr int   headerHeight = 14;
    static constexpr int   headerGap    = 12;
    static constexpr int   switchWidth  = 26;
    static constexpr int   switchHeight = 14;
    static constexpr float labelHeight  = 9.0f;

    juce::String title;
    Kind kind;
    bool on = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BlockFrame)
};

} // namespace orbitamp
