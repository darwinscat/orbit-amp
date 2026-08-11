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
        : title (std::move (blockTitle)), kind (blockKind), hasSwitch (withSwitch)
    {
        addChildComponent (shield);
    }

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
        dimChildren();   // NOT setAlpha on the whole block: the power switch stays full strength

        // An off block is READ-ONLY: everything stays visible — a control you cannot see is a
        // control you forget you set — but nothing answers, so a dark block can never quietly
        // eat an edit meant for a live one. The SHIELD does this, not interception flags:
        // setInterceptsMouseClicks(true, false) turned out not to block children at all —
        // getComponentAt descends into them regardless (found in review). The shield forwards
        // to the frame, so the power switch and the right-click menu work through it.
        shield.setVisible (! on);
        raiseShield();
        repaint();

        if (notify == juce::dontSendNotification)
            return;

        if (power != nullptr)
            power->setValueAsCompleteGesture (on ? 1.0f : 0.0f);

        if (onToggled != nullptr)
            onToggled (on);
    }

    /** Binds the frame's switch to the block's power parameter. The PARAMETER is the only truth:
        clicks write the negation of ITS value and the visual follows the attachment's echo — a
        widget that flips its own memory first can drift from the parameter and stay drifted,
        which is exactly how two switches on one parameter once disagreed. */
    void attachPower (juce::RangedAudioParameter& param)
    {
        powerParam = &param;
        power = std::make_unique<juce::ParameterAttachment> (param,
            [this] (float v) { setBlockOn (v > 0.5f, juce::dontSendNotification); });
        power->sendInitialUpdate();
    }

    /** Re-reads the power parameter into the visual state — insurance against any stale widget,
        called whenever a face comes (back) into view. */
    void resyncPower()
    {
        if (power != nullptr)
            power->sendInitialUpdate();
    }

    bool powerParamOn() const noexcept
    {
        return powerParam != nullptr && powerParam->getValue() > 0.5f;
    }

    void togglePowerParam()
    {
        if (power != nullptr)
            power->setValueAsCompleteGesture (powerParamOn() ? 0.0f : 1.0f);
    }

    std::function<void (bool)> onToggled;

    //==============================================================================
    void paint (juce::Graphics& g) override
    {
        // The off state dims the FACE, not the component: everything the frame paints drops into
        // a transparency layer, and the power switch is painted after it at full strength — on a
        // dark block the way back must be the one thing that still reads. The block itself STAYS
        // dark under the mouse; hover lights the switch alone.
        const bool dim = ! on;
        if (dim)
            g.beginTransparencyLayer (theme::offAlpha);

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
        // A block may keep its name off the border (the badge writes it inside instead).
        if (showTitle)
        {
            if (titleInBorder)
            {
                g.setColour (topFill);
                g.fillRect (titleBox.expanded (legendGap, 0.0f).withHeight (theme::blockBorder + 2.0f)
                                    .withCentre ({ titleBox.getCentreX(), box.getY() }));
            }

            g.setColour (kind == Kind::captured ? theme::orange : theme::lilac);
            theme::drawTracked (g, title.toUpperCase(), titleBox, theme::displayFont (titleHeight),
                                0.15f, juce::Justification::centredLeft);
        }

        paintContent (g);

        if (dim)
            g.endTransparencyLayer();

        // No mask for the switch: the border line runs straight into it and its own fill covers the
        // segment underneath, so the two read as one piece of hardware rather than a button parked
        // on a broken line. Painted OUTSIDE the dim layer, with a hover halo — the request that
        // built this: on a dark block the switch was the thing you could not find.
        if (hasSwitch)
            paintSwitch (g, sw);
    }

    void resized() override final
    {
        shield.setBounds (getLocalBounds());
        layOutHeader (headerWidgetArea());
        layOutContent (contentArea());
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        // Right-click anywhere on the block, at any scale: the power menu. The one edit that must
        // never need hunting for a target.
        if (e.mods.isPopupMenu())
        {
            showPowerMenu (e.getScreenPosition());
            return;
        }

        if (hasSwitch && switchArea().contains (e.getPosition()))
        {
            // Through the parameter, never the local flag — see attachPower.
            if (power != nullptr)
                togglePowerParam();
            else
                setBlockOn (! on);
        }
    }

    void showPowerMenu (juce::Point<int> screenPos)
    {
        if (! hasSwitch)
            return;

        juce::PopupMenu m;
        m.addSectionHeader (title.toUpperCase());
        m.addItem (1, "ON", true, power != nullptr ? powerParamOn() : on);

        // At the MOUSE — a whole block is a poor aiming point.
        m.showMenuAsync (juce::PopupMenu::Options()
                             .withTargetScreenArea ({ screenPos.x, screenPos.y, 1, 1 }),
                         [safe = juce::Component::SafePointer<BlockFrame> (this)] (int r)
                         {
                             if (r != 1 || safe == nullptr)
                                 return;

                             if (safe->power != nullptr)
                                 safe->togglePowerParam();
                             else
                                 safe->setBlockOn (! safe->isBlockOn());
                         });
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
        if (! switchInBorder) h.removeFromRight (switchW + headerGap);
        return h;
    }

    /** Zero when nothing is left in the first row: an empty row is dead space, so the content of a
        block without a header widget moves up instead. */
    virtual int headerHeight() const { return (titleInBorder && switchInBorder) ? 0 : 14; }

    virtual void layOutHeader (juce::Rectangle<int>) {}
    virtual void layOutContent (juce::Rectangle<int>) {}
    virtual void paintContent (juce::Graphics&) {}

    /** Blocks rebuild their faces (a device change makes new knobs mid-life) — every child that
        arrives inherits the block's dimming, and the shield stays on top of all of them. */
    void childrenChanged() override
    {
        dimChildren();
        raiseShield();
    }

private:
    /** The read-only curtain: invisible, above every child while the block is off, handing its
        clicks to the frame — which still answers for the power switch and the menu. */
    struct OffShield final : public juce::Component
    {
        explicit OffShield (BlockFrame& owner) : frame (owner) {}

        void mouseDown (const juce::MouseEvent& e) override
        {
            frame.mouseDown (e.getEventRelativeTo (&frame));
        }

        BlockFrame& frame;
    };

    void dimChildren()
    {
        for (auto* c : getChildren())
            if (c != &shield)
                c->setAlpha (on ? 1.0f : theme::offAlpha);
    }

    /** Keeps the shield the frontmost child. Guarded: toFront itself fires childrenChanged. */
    void raiseShield()
    {
        if (shieldRaising || getNumChildComponents() == 0)
            return;

        if (getChildComponent (getNumChildComponents() - 1) == &shield)
            return;

        shieldRaising = true;
        shield.toFront (false);
        shieldRaising = false;
    }

    /** Anything riding the top border needs room above the line for its own height, so the drawn
        frame starts lower than the component. Zero when nothing rides it. */
    int topInset() const
    {
        return (titleInBorder || switchInBorder) ? switchH / 2 + 1 : 0;
    }

protected:
    juce::Rectangle<int> boxArea() const { return getLocalBounds().withTrimmedTop (topInset()); }

private:

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
                                                             theme::displayFont (titleHeight), 0.15f)) + 1;

        return juce::Rectangle<int> (0, 0, w, juce::jmax (switchH, (int) titleHeight + 6))
                 .withCentre ({ boxArea().getX() + borderInset + w / 2, boxArea().getY() });
    }

protected:
    juce::Rectangle<int> switchArea() const
    {
        if (! switchInBorder)
        {
            auto h = headerArea();
            return h.removeFromRight (switchW).withSizeKeepingCentre (switchW, switchH);
        }

        return juce::Rectangle<int> (0, 0, switchW, switchH)
                 .withCentre ({ boxArea().getRight() - borderInset - switchW / 2, boxArea().getY() });
    }

private:

    juce::Colour borderColour() const
    {
        // A live block wears its accent nearly full strength — active and inactive must be
        // tellable at a glance, and the frame is the first thing a glance meets. (The off state
        // is dimmed wholesale by setAlpha, so one bright constant serves both.)
        return accent().withAlpha (kind == Kind::captured ? 0.85f : 0.80f);
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

protected:
    /** The title's font height. Panel-size blocks keep the default; zoom faces raise it to
        reading size — a magnified block must not whisper its own name. */
    float titleHeight = labelHeight;

    /** The frame switch's size — the zoom scales it up with the title: on a full-panel face the
        one control the frame owns must be sized like a control. */
    int switchW = switchWidth;
    int switchH = switchHeight;

    /** Off: the name stays out of the frame entirely — the block writes it where it wants. */
    bool showTitle = true;

public:
    // The reading-size metrics every zoomed face shares — ONE constant, not a per-block opinion.
    static constexpr float zoomTitleHeight = 16.0f;
    static constexpr int   zoomSwitchW     = 42;
    static constexpr int   zoomSwitchH     = 22;

    /** Zoomed faces grow their frame furniture; back on the overview it shrinks home. */
    void setZoomed (bool z)
    {
        titleHeight = z ? zoomTitleHeight : labelHeight;
        switchW     = z ? zoomSwitchW : switchWidth;
        switchH     = z ? zoomSwitchH : switchHeight;
        resized();
        repaint();
    }

private:

    juce::String title;
    Kind kind;
    bool hasSwitch = true;
    bool on = true;
    bool shieldRaising = false;
    OffShield shield { *this };
    juce::RangedAudioParameter* powerParam = nullptr;
    std::unique_ptr<juce::ParameterAttachment> power;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BlockFrame)
};

} // namespace orbitamp
