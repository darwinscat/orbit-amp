// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <map>
#include <vector>

namespace orbitamp
{

class AmpEditor;
class AmpProcessor;
class Knob;
class VolumeColumn;

/** ORBITAMP'S OWN FACE, WRITTEN OUT FOR THE SITE. The site plays the family's faces in the browser,
    each from a kit its app writes; the consoles write theirs in Console::exportForWeb, and this is
    the player's. Switched on by the environment variable ORBITAMP_WEB_EXPORT naming a folder — a
    player never sees it — and run once, a moment after the window opened.

    The kit is the site's contract, format "orbitamp-web-faceplate", schema 1 — a SEPARATE format from
    the consoles' on purpose: none of a console's top-level fields appears here, so a console parser
    cannot half-read it. In stage units (the editor's design units, the window at 100%):
      - cabin.png / cabin@1x.png — the window with every part the site draws itself taken out;
      - poster-<rig_id>.png / @1x — the whole window as it opens on each channel of the device;
      - sprites/ — every live part in each state it can be in, cut to its place, and reference renders
        of what the site paints itself, for its tests;
      - places.json — where everything stands, by the app's own names, the lineup, the look, the regions
        the site keeps inert, each sprite's opaque bounds;
      - done.txt, last, so whoever waits knows the folder is whole.

    THE FACE IS FROZEN FOR IT AND PUT BACK: the shipping layout, the device's opening and the defaults
    the site plays are stood for the pictures — parameters moved, views told what to show — and every
    one of them is put back afterwards, the channels' memory with them. Nothing a player sees or hears
    is touched outside the export, which only an environment variable starts. */
class WebFaceplate
{
public:
    /** Writes the kit into `folder` once the window has settled. */
    static void exportAfterOpening (AmpEditor&, const juce::File& folder);

    /** The same, now — for a gate, which has no message loop to wait on. */
    static void writeKit (AmpEditor&, const juce::File& folder);

    /** A LIVE STILL for a product page, once the window has settled: the face as it ships, a DI riff
        played through the real processor, pictured mid-riff — live.png, and live-16x9.png on the
        panel's ground — and then the app quits. A dev tool: ORBITAMP_LIVE_SHOT names the folder,
        ORBITAMP_LIVE_SHOT_DI the riff (a WAV). Run it in a copy with no audio device of its own. */
    static void liveShotAfterOpening (AmpEditor&, const juce::File& folder, const juce::File& riff);

private:
    WebFaceplate (AmpEditor&, const juce::File& folder);

    void write();
    void playAndShoot (const juce::File& riff);
    void freeze();
    void thaw();

    // Standing the face in a state, and putting it back.
    void standParam (const juce::String& id, float normalised);
    void standPlain (const juce::String& id, float plain);
    void standChoice (const juce::String& id, const juce::String& text);
    void putBackParams();

    // Where things stand.
    bool stands (const juce::Component&) const;
    juce::Rectangle<float> onStage (const juce::Component&, juce::Rectangle<float> area) const;
    juce::var rect (const juce::Component&, juce::Rectangle<float> area = {}) const;
    juce::var knobVar (Knob&, const juce::String& param, const juce::String& file);
    juce::var columnVar (VolumeColumn&, const juce::String& file);

    // The pictures.
    juce::Image shootEditor (float k) const;
    juce::Image shoot (juce::Component&, juce::Rectangle<float> area = {}) const;
    juce::Image shootStage (juce::Rectangle<float> stageArea) const;
    void writePng (const juce::String& file, const juce::Image&) const;
    juce::String sprite (const juce::String& file, const juce::Image&);
    juce::String spriteOf (const juce::String& file, juce::Component&, juce::Rectangle<float> area = {});

    /** A pill on a block's top border, one sprite per state, all cut to one box: the union of where the
        pill stands in every state and the line it opens, so a narrower name lays the line back. Cut out
        of the whole window, so opaque: the block's own line stays on the cabin, and a sprite of the block
        alone would draw its translucent edges a second time over it. */
    juce::var pills (juce::Component& block, juce::Component& pill, const juce::String& param,
                     const juce::String& stem, const std::vector<int>& indices, juce::StringArray& names);

    void patch (const juce::String& name, const juce::Component&, juce::Rectangle<float> area = {});

    juce::var places();
    juce::var references();
    juce::var posters();
    juce::var deviceVar() const;
    juce::var lineupVar() const;
    juce::var lookVar() const;

    static juce::String slug (const juce::String&);

    static constexpr float scale = 2.0f;   // the 2x files, against the stage

    AmpEditor&    editor;
    AmpProcessor& amp;
    const juce::File dir;
    std::map<juce::String, float> before;              // every parameter a picture moved, as it stood
    std::vector<std::function<void()>> thawing;        // what puts each view-only state back, in reverse
    std::vector<juce::Component*> live;                // what the site draws: off the cabin
    juce::DynamicObject::Ptr spriteBounds { new juce::DynamicObject() };
    juce::Array<juce::var> patches;
    juce::ValueTree channelsMemory;                    // the channels' records as they stood, put back

    JUCE_DECLARE_NON_COPYABLE (WebFaceplate)
};

} // namespace orbitamp
