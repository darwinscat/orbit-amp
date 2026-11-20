// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "CaliforniaGlyphs.h"

#include "../face/Console.h"
#include "../redgroove/RedGrooveControls.h"
#include "../redpaw/view/Windscreen.h"
#include "../ui/CabWindow.h"

#include <juce_opengl/juce_opengl.h>

#include <array>
#include <atomic>

namespace orbitamp::california
{

/** THE VAN'S DASHBOARD ITSELF: the picture with its windscreen transparent, the peace pendant under the mirror, the
    family's cat cut into the maker's plate, POWERED BY and DARWIN'S CAT on the plates either side of it, GATE's and
    LIMITER's caps — and the pictograms (glyphs::Kind) over every socket: no word on the panels (the owner, 20.11).
    Drawn over the view, under the controls, and never takes a click.

    THE PANEL IS THE WORLD'S TO DRAW while it draws it (Windscreen::drawsCabin), as the bathyscaphe's is: the California
    world draws the dashboard and swings the pendant. Whenever it does not, and in every picture taken of it, they are
    painted here, the pendant hanging still. */
class Cabin final : public face::Picture
{
public:
    std::function<bool()> worldDrawsPanel;

    /** THE PLATE FOR A PENDANT SWUNG UNDER IT (the site's cabin-swing.webp): the whole picture as staticImage paints it,
        the pendant left out — where it hung, the dashboard's own glass, transparent but for its tint and the mirror's
        foot — so that a view swinging its own pendant under the plate shows it through. */
    juce::Image withoutPendant (float scale) const;

private:
    mutable bool pendantLeftOut = false;

    void paintStatic (juce::Graphics&, float scale) const override;
    juce::Colour signInk() const override { return glyphs::ink; }
    juce::Rectangle<int> historyArea() const override;
    int layerState() const override { return worldDrawsPanel != nullptr && worldDrawsPanel() ? 1 : 0; }
};

//==============================================================================
/** CALIFORNIAAMP'S FACE over OrbitAmp's processor: the family's console (face/Console.h) laid out on the van's
    dashboard, a world under the whole of it, its channels CLEAN and DIRTY. What it has of the console's own: GAIN (the
    console's DRIVE), the six keys of the history and the registers, IN and OUT, the lamps, the tuner, the maker's plate;
    what it has not: the REVERB, DELAY and CAB plates, the HPF and LPF keys, the console's row of tone knobs and of a
    pack's switches. What is its own over it: the lever, PARK over the two channels; BASS, MIDDLE and TREBLE, each the
    playing channel's tone OF THAT NAME, a socket it has none for keeping a frozen, dim knob; COMP and its squeeze; DELAY
    and REVERB; VOLUME and PHASER, dark — the chain has neither yet; GATE and LIMITER on caps of their own; the cabinet's
    slider in its screen and its picture under it; and the name of every pictogram, and of every control, on hover. */
class CaliforniaEditor final : public face::Console,
                               private juce::OpenGLRenderer
{
public:
    explicit CaliforniaEditor (AmpProcessor&);
    ~CaliforniaEditor() override;

private:
    // THE VIEW, under the whole stage, in the windscreen the rovers look through (redpaw/view/Windscreen.h), its gear the
    // channel's, parked while the amp is. Its GPU frame is drawn by this window's own context, which paints the face over it.
    std::unique_ptr<redpaw::Windscreen> view;
    juce::OpenGLContext gl;
    std::atomic<int> glassX { 0 }, glassY { 0 }, glassW { 0 }, glassH { 0 };     // the stage in the editor's pixels
    static std::unique_ptr<redpaw::Drive> makeDrive();
    Cabin& cabin() { return static_cast<Cabin&> (*picture); }
    void placeView();
    void chooseLandscape (redpaw::Windscreen::Mode);
    void chooseFrameRate (int fps);
    void showWindowMenu();
    void addWindowSections (juce::PopupMenu&) override;
    void stagePlaced() override { placeView(); }
    struct GlassClicks final : juce::MouseListener
    {
        std::function<void()> menu;
        void mouseDown (const juce::MouseEvent& e) override { if (e.mods.isPopupMenu() && menu) menu(); }
    };
    GlassClicks glassClicks;
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;

    // EVERY PICTOGRAM'S NAME ON HOVER (the owner, 20.11): a spot over each the hand finds it by — a click there does what
    // its control does, where it has one (the lever's stops, GATE, LIMITER) — and the name of a control the hand is over.
    struct Spot final : juce::Component
    {
        juce::String name;
        std::function<void()> onClick;
        std::function<void (bool)> onHover;
        void mouseEnter (const juce::MouseEvent&) override { if (onHover) onHover (true); }
        void mouseExit (const juce::MouseEvent&) override  { if (onHover) onHover (false); }
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (onClick && ! e.mods.isPopupMenu() && getLocalBounds().contains (e.getPosition()))
                onClick();
        }
    };
    std::vector<std::unique_ptr<Spot>> spots;
    Spot& spot (juce::Rectangle<float> onStage, const juce::String& name, std::function<void()> onClick = {});
    struct Hover final : juce::MouseListener
    {
        std::function<void (bool)> said;
        void mouseEnter (const juce::MouseEvent&) override { if (said) said (true); }
        void mouseExit (const juce::MouseEvent&) override  { if (said) said (false); }
    };
    std::vector<std::unique_ptr<Hover>> hovers;
    void nameOnHover (juce::Component&, const juce::String& name);
    void sayName (juce::Rectangle<float> over, const juce::String& name, bool shown);

    // THE FADERS: COMP's threshold, DELAY's and REVERB's shares; VOLUME and PHASER dark. A double click puts a live one back
    // where it opens.
    face::ColumnFader comp, delay, phaser, reverb, volume;
    redgroove::GrColumn squeeze;
    std::vector<std::unique_ptr<face::KnobRing>> rings;     // GAIN's, then the tones'

    template <typename Control>
    void bindTravel (Control&, const juce::String& paramId, std::function<float (float)> toTravel,
                     std::function<float (float)> toPlain);

    // THE TONES' SOCKETS, BASS, MIDDLE AND TREBLE: each the playing channel's tone of that name (Channels::toneNamed) — its
    // knob bound to that tone's parameter — or, where the pack has none, a frozen knob at its noon, dim, out of the hand's
    // reach, its ring with it.
    std::array<std::unique_ptr<face::ArtKnob>, 3> tones;
    std::array<std::unique_ptr<juce::ParameterAttachment>, 3> toneBindings;
    std::array<juce::String, 3> toneBound;
    void bindTones (int channel);

    // THE HAND ON THE CHANNELS AND THE CABINET. The lever: PARK, then CLEAN and DIRTY — PARK is the amp's bypass, the
    // others the channels (and the number keys, a Program Change, a host's CHANNEL with them). The cabinet's slider in its
    // screen, a notch each of the TOP 12; its picture and its name in the screen under it.
    redgroove::ChannelLever lever;
    redgroove::CabinetScreen cabinet;
    struct CabinetPicture final : juce::Component
    {
        CabinetPicture() { setInterceptsMouseClicks (false, false); }
        int cab = -1;
        CabPicker::Setup setup;
        AmpProcessor::CabSynthChoice shown;     // what it was drawn for
        juce::String words;                     // ...and its name, whose size ("4x12") draws a cabinet the sketch has not
        void paint (juce::Graphics&) override;
    };
    CabinetPicture cabinetPicture;
    std::unique_ptr<redgroove::Readout> cabinetLabel;   // the cabinet's name along the picture's foot (CabinetScreen::label)
    juce::ParameterAttachment park;
    bool parked = false;
    void hearSlot (int notch);
    void chooseSlot (int notch);
    void showCabinet();
    int dragAt = -1;

    void showSelector (int channel) override;
    void showPark (bool on);
    bool channelKey (const juce::KeyPress&) override;
    void stripsLaid (const face::Strips&) override { bindTones (channels.active()); }

    // FOR THE SITE (Console::webExtras): the van's map in places.json's existing keys, and its posters.
    void webExtras (const juce::File& dir, juce::DynamicObject& kit, juce::DynamicObject& world) override;
    juce::Image webStill;           // the site's still: the view's first frame, the first a poster brings

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CaliforniaEditor)
};

} // namespace orbitamp::california
