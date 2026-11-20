// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "IoGlyphs.h"
#include "view/IoDrive.h"

#include "../face/Console.h"
#include "../redgroove/RedGrooveControls.h"
#include "../redpaw/view/Windscreen.h"

#include <juce_opengl/juce_opengl.h>

#include <array>
#include <atomic>

namespace orbitamp::io
{

/** THE IO ROVER'S DASHBOARD ITSELF: the day's picture with its windscreen transparent, the family's cat cut into the
    maker's plate, POWERED BY and DARWIN'S CAT on the plates either side of it, GATE's and LIMITER's caps — and Io's
    labels: the pictograms over the sockets (glyphs::Kind), the lever's P and its triangles, and the few words in B612
    (the owner, 20.11). Drawn over the view, under the controls, and never takes a click.

    THE PANEL IS THE WORLD'S TO DRAW while it draws it (Windscreen::drawsCabin), as the bathyscaphe's is: the Io world
    crossfades its day and dark plates by the Sun and moves the dosimeter's needle. Whenever it does not, and in every
    picture taken of it, the day's plate is painted here, flat. HOW DARK THE CABIN IS (`dark`, 0 the day, 1 the dark
    plate) is the world's to say: the labels follow it, from the day's paint to the night's lit glow (glyphs::paintLabels). */
class Cabin final : public face::Picture
{
public:
    std::function<bool()> worldDrawsPanel;
    float dark = 0.0f;

    /** The labels' steps of darkness the layer is painted again at. */
    static constexpr int darkSteps = 64;

private:
    void paintStatic (juce::Graphics&, float scale) const override;
    juce::Colour signInk() const override { return glyphs::inkAt (dark); }
    juce::Rectangle<int> historyArea() const override;
    int layerState() const override
    {
        return (worldDrawsPanel != nullptr && worldDrawsPanel() ? 1 : 0) | (juce::roundToInt (dark * (float) darkSteps) << 1);
    }
};

//==============================================================================
/** IOBASS'S FACE over OrbitAmp's processor: the family's console (face/Console.h) laid out on the Io rover's dashboard
    — the SansAmp RBI's controls — a world under the whole of it, one sound under the rover's two speeds. What it has of
    the console's own: DRIVE, the six keys of the history and the registers, IN and OUT, the lamps, the tuner, the
    maker's plate; what it has not: the REVERB, DELAY and CAB plates, the HPF and LPF keys, the console's row of tone
    knobs and of a pack's switches. What is its own over it: the lever, PARK over SLOW and FAST; BASS, MIDDLE and
    TREBLE, each the channel's tone OF THAT NAME, and PRESENCE, the tone named so — a socket the pack has none for keeping
    a frozen, dim knob at its noon; BLEND; COMP and its squeeze; OCTAVE, CHORUS and REVERB; GATE and LIMITER on caps of
    their own; the cabinet's slider in its screen and the cuts' two switches; and the name of every pictogram, and of
    every control, on hover. */
class IoEditor final : public face::Console,
                       private juce::OpenGLRenderer
{
public:
    explicit IoEditor (AmpProcessor&);
    ~IoEditor() override;

    /** HOW DARK THE CABIN IS, 0 the day to 1 the dark plate: the labels' paint and their glow follow it. */
    void setCabinDark (float share);
    float cabinDark() const noexcept { return dark; }

    /** THE NIGHT GLOW FOLLOWS THE WORLD, every frame of the screen (a VBlankAttachment, `glow`): the cabin as dark as the
        Io world says it is (IoDrive::cabinDark), in the labels' steps (Cabin::darkSteps), while the view is the world on
        the GPU; the day (0) while it is the 1993 landscape or the still, where the face paints the day's plate. */
    void followWorld();

    /** PRESENCE as the world takes it while the pack has no Presence tone (the knob frozen at its noon): the prototype's
        own default. */
    static constexpr double presenceAtRest = 0.2;

private:
    // THE VIEW, under the whole stage, in the windscreen the rovers look through (redpaw/view/Windscreen.h), its gear the
    // speed the lever stands at, parked while the amp is. Its GPU frame is drawn by this window's own context, which
    // paints the face over it.
    std::unique_ptr<redpaw::Windscreen> view;
    IoDrive* ioWorld = nullptr;     // the view's drive, which the view owns: the Io world the knobs go to
    juce::OpenGLContext gl;
    std::atomic<int> glassX { 0 }, glassY { 0 }, glassW { 0 }, glassH { 0 };     // the stage in the editor's pixels
    static std::unique_ptr<redpaw::Drive> makeDrive();
    /** THE AMP'S KNOBS ARE THE WORLD'S INPUTS (io/README.md, "The knobs"), each its place over its range, 0..1: DRIVE the
        time of day (IoDrive::setDrive), BLEND Jupiter's phase (setBlend), PRESENCE the sulphur snow (setPresence) — the
        Presence tone's where the pack has one, presenceAtRest where it has none. Read from the parameters themselves. */
    void tellWorld (IoDrive&) const;
    juce::RangedAudioParameter* presenceTone = nullptr;     // the playing channel's Presence tone, none where its pack has none
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

    float dark = 0.0f;
    juce::VBlankAttachment glow { this, [this] { followWorld(); } };

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

    // THE FADERS: BLEND, COMP's threshold, and the three effects' amounts, 0 the dry. A double click puts any of them back
    // where it opens.
    face::ColumnFader blend, comp, octave, chorus, reverb;
    redgroove::GrColumn squeeze;
    std::vector<std::unique_ptr<face::KnobRing>> rings;     // DRIVE's, PRESENCE's, then the tones'

    template <typename Control>
    void bindTravel (Control&, const juce::String& paramId, std::function<float (float)> toTravel,
                     std::function<float (float)> toPlain);

    // THE TONES' SOCKETS, PRESENCE, BASS, MIDDLE AND TREBLE: each the channel's tone of that name — its knob bound to that
    // tone's parameter — or, where the pack has none (the stand-in's PRESENCE), a frozen knob at its noon, dim, out of the
    // hand's reach, its ring with it, its name on hover still.
    static constexpr size_t presenceSocket = 0;
    std::array<std::unique_ptr<face::ArtKnob>, 4> tones;
    std::array<std::unique_ptr<juce::ParameterAttachment>, 4> toneBindings;
    std::array<juce::String, 4> toneBound;
    Spot* frozenPresence = nullptr;
    void bindTones (int channel);

    // THE HAND ON THE SPEEDS, THE CUTS AND THE CABINET. The lever: PARK, then SLOW and FAST — PARK is the amp's bypass,
    // the speeds the pair's two channels (and the number keys, a Program Change, a host's CHANNEL with them), which play
    // the same; the two switches throw 5STR | 4STR and FING | SLAP, each its frequency on its right click; the cabinet's
    // slider in its screen, a notch each of the bass's dozen.
    redgroove::ChannelLever lever;
    redgroove::SlideSwitch lowCut, highCut;
    redgroove::CabinetScreen cabinet;
    std::unique_ptr<redgroove::Readout> cabinetLabel;
    juce::ParameterAttachment park;
    bool parked = false;
    void bindSwitch (redgroove::SlideSwitch&, const char* paramId);
    void hearSlot (int notch);
    void chooseSlot (int notch);
    int dragAt = -1;

    void showSelector (int channel) override;
    void showPark (bool on);
    bool channelKey (const juce::KeyPress&) override;
    void stripsLaid (const face::Strips&) override { bindTones (channels.active()); }

    // FOR THE SITE (Console::webExtras): the rover's map in places.json's existing keys, and its posters.
    void webExtras (const juce::File& dir, juce::DynamicObject& kit, juce::DynamicObject& world) override;
    juce::Image webStill;           // the site's still: the view's first frame, the first a poster brings

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IoEditor)
};

} // namespace orbitamp::io
