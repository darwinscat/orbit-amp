// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Gate for IOBASS'S FACE (src/io/IoEditor.h), its window made and never shown, run as the Io rover out of the build's
// factory folder:
//
//   - the lever: PARK over the two speeds, SLOW and FAST. PARK is the amp's bypass (AmpProcessor::bypassParameter) and
//     leaves the speed as it was; a speed brings the amp back in; a host's bypass moves the lever to PARK, and a number
//     key, the amp out, brings it back in;
//   - ONE SOUND: SLOW and FAST play the same pack, nothing held, and every parameter but CHANNEL stands where it stood
//     when the lever goes from one to the other — whatever the hand set on either (Lineup::pairs, one memory);
//   - the tones by name: BASS, MIDDLE and TREBLE each turn the channel's tone of that name; PRESENCE, which the stand-in's
//     pack has none of, a frozen knob at its noon, dim, taking no hand, its name said on hover — and no parameter made up
//     for it;
//   - every pictogram named (a spot over each, its title the name);
//   - a still of the face renders: the day's dark panel painted, DRIVE's wedge on it in Io's light paint;
//   - THE NIGHT: the cabin's dark share at 1 lights the labels — a warm halo round DRIVE's wedge where the day has none,
//     the wedge itself warmer — and back at 0 the still is the day's again;
//   - THE WORLD BEHIND THE GLASS is Io (io::IoDrive), in the speed's gear — SLOW 1, FAST 2 — and parked at PARK; DRIVE's
//     and BLEND's places over their ranges reach the drive as the time of day and Jupiter's phase, PRESENCE (frozen) the
//     prototype's 0.2; the face's night glow follows how dark the world says the cabin is (IoDrive::cabinDark) — dark at
//     DRIVE's end, the day at its start; and the world finds every file it reads where the build lays them out, the
//     factory folder's Faces/IoBass (io::shippedArt): both plates, a mesh and its table for every variant of the finds,
//     the skylight's pit radius.
//
// Each control is found by the name the face gives it; the tone knobs by the sockets they stand in (io/IoLayout.h).
// main() returns 0 when every check held.

#include "GateMain.h"
#include "PluginProcessor.h"
#include "Parameters.h"
#include "channels/Engine.h"
#include "face/Controls.h"
#include "io/IoEditor.h"
#include "io/IoLayout.h"
#include "io/view/IoArt.h"
#include "io/view/IoDrive.h"
#include "redpaw/view/Windscreen.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

namespace
{
    juce::Component* named (juce::Component& in, const juce::String& id)
    {
        if (in.getComponentID() == id)
            return &in;
        for (auto* child : in.getChildren())
            if (auto* found = named (*child, id))
                return found;
        return nullptr;
    }

    /** Every component of a kind under `in`, wherever it stands. */
    template <typename Kind>
    void allOf (juce::Component& in, std::vector<Kind*>& out)
    {
        if (auto* k = dynamic_cast<Kind*> (&in))
            out.push_back (k);
        for (auto* child : in.getChildren())
            allOf (*child, out);
    }

    /** What a component of a kind stands at `centre` of the stage, within a few px, and is showing. */
    template <typename Kind>
    Kind* standingAt (juce::Component& in, juce::Point<float> centre)
    {
        std::vector<Kind*> all;
        allOf (in, all);
        for (auto* k : all)
            if (k->isVisible() && k->getBounds().toFloat().getCentre().getDistanceFrom (centre) < 4.0f)
                return k;
        return nullptr;
    }

    /** Two pictures alike to the last pixel in `area` — the bottom row's labels and controls: MEM and CPU over the plates
        read the machine, and change as they please. */
    bool alikeIn (const juce::Image& a, const juce::Image& b, juce::Rectangle<int> area)
    {
        if (! a.isValid() || ! b.isValid() || a.getBounds() != b.getBounds())
            return false;
        area = area.getIntersection (a.getBounds());
        for (int y = area.getY(); y < area.getBottom(); ++y)
            for (int x = area.getX(); x < area.getRight(); ++x)
                if (a.getPixelAt (x, y) != b.getPixelAt (x, y))
                    return false;
        return true;
    }

    float brightness (juce::Colour c) { return (c.getFloatRed() + c.getFloatGreen() + c.getFloatBlue()) / 3.0f; }
}

int ORBITAMP_GATE_MAIN()
{
    using namespace orbitamp;
    namespace layout = io::layout;
    const juce::ScopedJuceInitialiser_GUI juceInit;

    int failed = 0;
    const auto check = [&failed] (bool ok, const juce::String& what)
    {
        std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
        if (! ok)
            ++failed;
    };

    auto amp = std::make_unique<AmpProcessor> (ORBITAMP_GATE_PRODUCT());
    amp->inlineLoads = true;
    const auto setPlain = [&amp] (const juce::String& id, float v) { if (auto* p = amp->apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v)); };
    const auto settle = [] { juce::MessageManager::getInstance()->runDispatchLoopUntil (100); };

    auto& engine   = channels::Engine::of (*amp);
    auto& channels = engine.channels;
    auto* device   = amp->apvts.getParameter (params::preampDevice);
    auto& bypass   = amp->bypassParameter();
    const auto playing = [device] { return juce::roundToInt (device->convertFrom0to1 (device->getValue())); };
    const auto nameOf  = [&channels] (int c) { return juce::String (channels.lineup().channels[(size_t) c].name); };
    const auto out     = [&bypass] { return bypass.get(); };

    std::unique_ptr<juce::AudioProcessorEditor> face (amp->createEditor());
    auto* io = dynamic_cast<io::IoEditor*> (face.get());
    check (io != nullptr && named (*face, "lever") != nullptr && named (*face, "comp") != nullptr,
           "the Io rover's face comes up, its own controls on it");
    if (io == nullptr || named (*face, "lever") == nullptr || named (*face, "comp") == nullptr)
    {
        std::printf ("FAILED\n");
        return 1;
    }
    settle();

    // THE LEVER: PARK, then the two speeds. A fresh rover opens on SLOW, the amp in.
    auto* lever = dynamic_cast<face::DetentLever*> (named (*face, "lever"));
    check (lever != nullptr && lever->numPlaces() == 3 && channels.count() == 2 && nameOf (0) == "SLOW" && nameOf (1) == "FAST",
           "the lever has three places: PARK, SLOW and FAST");
    check (lever != nullptr && ! out() && channels.active() == 0 && lever->getPlace() == 1,
           "...and a fresh one stands on SLOW, the amp in");
    const auto pull = [&] (int place) { if (lever != nullptr) lever->setPlace (place, false, true); settle(); };
    if (lever != nullptr)
    {
        pull (0);
        check (out() && channels.active() == 0 && lever->getPlace() == 0,
               "the lever pulled to PARK takes the amp out — its bypass on — and leaves the speed on SLOW");
        pull (2);
        check (! out() && channels.active() == 1 && playing() == channels.packFor (1) && lever->getPlace() == 2,
               "...pulled to FAST brings the amp back in, on FAST, its pack playing");
        bypass.setValueNotifyingHost (1.0f);
        settle();
        check (lever->getPlace() == 0 && channels.active() == 1, "a host's bypass on moves the lever to PARK, the speed left on FAST");
        face->keyPressed (juce::KeyPress ('1'));
        settle();
        check (! out() && channels.active() == 0 && lever->getPlace() == 1, "the 1 key, the amp out, pulls SLOW: the amp back in");
    }

    // ONE SOUND UNDER BOTH SPEEDS: the same pack, nothing held, and every parameter but CHANNEL where it stood.
    {
        const auto engineChannel = juce::String (engine.channelId());
        const auto everything = [&amp, &engineChannel]
        {
            juce::StringPairArray all;
            for (auto* p : amp->getParameters())
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p); ranged != nullptr && ranged->getParameterID() != engineChannel)
                    all.set (ranged->getParameterID(), juce::String (ranged->getValue(), 6));
            return all;
        };
        const auto differing = [] (const juce::StringPairArray& a, const juce::StringPairArray& b)
        {
            juce::StringArray off;
            for (const auto& key : a.getAllKeys())
                if (a[key] != b[key])
                    off.add (key + " " + a[key] + " -> " + b[key]);
            return off;
        };
        check (channels.packFor (0) >= 0 && channels.packFor (0) == channels.packFor (1)
                   && channels.lineup().channels[0].held.control == nullptr && channels.lineup().channels[1].held.control == nullptr,
               "SLOW and FAST play the same pack, the ReVolt Bass's DIRT, and hold none of its switches");

        pull (1);
        setPlain (params::preampGain, 7.5f);
        for (int t = 0; t < 3; ++t)
            if (auto* p = amp->apvts.getParameter (channels.toneOf (0, t)))
                p->setValueNotifyingHost (0.2f + 0.25f * (float) t);
        setPlain (params::ampBlend, 0.55f);
        setPlain (params::compThreshold, -30.0f);
        setPlain (params::octaveAmount, 0.3f);
        setPlain (params::chorusAmount, 0.6f);
        setPlain (params::reverbMix, 40.0f);
        setPlain (params::bassHpf, 1.0f);
        settle();
        const auto onSlow = everything();
        pull (2);
        const auto offFast = differing (onSlow, everything());
        check (channels.active() == 1 && playing() == channels.packFor (0) && offFast.isEmpty(),
               "the hand's DRIVE, tones, BLEND, COMP, effects and cut set on SLOW all stand on FAST, the pack the same"
                   + (offFast.isEmpty() ? juce::String() : ": " + offFast.joinIntoString ("; ")));
        setPlain (params::preampGain, 3.0f);
        setPlain (params::reverbMix, 12.0f);
        setPlain (params::bassLpf, 1.0f);
        settle();
        const auto onFast = everything();
        pull (1);
        const auto offSlow = differing (onFast, everything());
        check (channels.active() == 0 && offSlow.isEmpty(),
               "...and what the hand sets on FAST stands on SLOW"
                   + (offSlow.isEmpty() ? juce::String() : ": " + offSlow.joinIntoString ("; ")));
    }

    // THE TONES BY NAME, AND PRESENCE FROZEN.
    {
        const std::array<juce::Point<float>, 3> sockets {{ { layout::toneX[0], layout::toneY }, { layout::toneX[1], layout::toneY }, { layout::toneX[2], layout::toneY } }};
        const char* names[] { "Bass", "Mid", "Treble" };
        bool byName = channels.toneNamesOf (0) == juce::StringArray { "Bass", "Mid", "Treble" };
        for (size_t s = 0; s < sockets.size(); ++s)
        {
            auto* knob = standingAt<face::ArtKnob> (*face, sockets[s]);
            const int t = channels.toneNamesOf (0).indexOf (names[s]);
            auto* p = t >= 0 ? amp->apvts.getParameter (channels.toneOf (0, t)) : nullptr;
            if (knob == nullptr || p == nullptr || ! knob->isEnabled())
            {
                byName = false;
                continue;
            }
            knob->setValue (0.15f + 0.3f * (float) s, true);
            byName = byName && std::abs (p->getValue() - (0.15f + 0.3f * (float) s)) < 1.0e-3f;
        }
        check (byName, "BASS, MIDDLE and TREBLE each turn the DIRT pack's tone of that name: Bass, Mid and Treble");

        auto* presence = standingAt<face::ArtKnob> (*face, layout::presence);
        bool takes = true, children = true;
        if (presence != nullptr)
            presence->getInterceptsMouseClicks (takes, children);
        auto* said = named (*face, "frozen-presence");
        bool invented = false;
        for (auto* p : amp->getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
                invented = invented || ranged->getParameterID().containsIgnoreCase ("presence");
        check (presence != nullptr && ! presence->isEnabled() && ! takes && presence->getAlpha() < 0.5f
                   && std::abs (presence->getValue() - 0.5f) < 1.0e-6f,
               "PRESENCE, which the stand-in's pack has none of, is a frozen knob at its noon, dim, taking no hand");
        check (said != nullptr && said->isVisible() && said->getTitle() == "PRESENCE"
                   && said->getBounds().toFloat().getCentre().getDistanceFrom (layout::presence) < 4.0f,
               "...its name said on hover over it");
        check (! invented, "...and no parameter is made up for it");
    }

    // EVERY PICTOGRAM NAMED.
    {
        const char* signs[] { "drive", "presence", "blend", "bass", "middle", "treble", "octave", "chorus", "reverb", "output",
                              "compressor", "input", "park", "slow", "fast", "gate", "limiter" };
        juce::StringArray missing;
        for (const auto* s : signs)
            if (auto* c = named (*face, juce::String ("sign-") + s); c == nullptr || ! c->isVisible() || c->getTitle() != juce::String (s).toUpperCase())
                missing.add (s);
        check (missing.isEmpty(), "every pictogram has a spot that names it" + (missing.isEmpty() ? juce::String() : ": not " + missing.joinIntoString (", ")));
    }

    // A STILL, BY DAY AND BY NIGHT.
    {
        face->setSize (1672, 941);
        settle();
        const auto shot = [&face, &settle] { settle(); return face->createComponentSnapshot (face->getLocalBounds()); };
        const auto day = shot();
        const float k = (float) day.getWidth() / layout::width;
        const auto at = [k] (const juce::Image& img, float x, float y) { return img.getPixelAt (juce::roundToInt (x * k), juce::roundToInt (y * k)); };

        // DRIVE's wedge, a point inside it, and one just under its base, where the night's halo lies.
        const auto& g = layout::driveGlyph;
        const juce::Point<float> inWedge { g.x + 8.0f * g.scale, g.y + 4.0f * g.scale };
        const juce::Point<float> underWedge { g.x + 6.0f * g.scale, g.y + 6.5f * g.scale + 1.2f };     // the halo, a px under the base
        const auto panel = at (day, 380.0f, 905.0f);
        const auto wedge = at (day, inWedge.x, inWedge.y);
        check (day.isValid() && brightness (panel) < 0.35f && brightness (wedge) > 0.7f,
               "a still of the face renders: the dark panel (" + panel.toDisplayString (false) + ") and DRIVE's wedge on it in Io's paint ("
                   + wedge.toDisplayString (false) + ")");

        io->setCabinDark (1.0f);
        const auto night = shot();
        const auto haloDay = at (day, underWedge.x, underWedge.y), haloNight = at (night, underWedge.x, underWedge.y);
        const auto wedgeNight = at (night, inWedge.x, inWedge.y);
        check (juce::approximatelyEqual (io->cabinDark(), 1.0f) && brightness (haloNight) > brightness (haloDay) + 0.06f,
               "the cabin's dark share at 1 draws the night's glow: a warm halo under DRIVE's wedge ("
                   + haloDay.toDisplayString (false) + " by day, " + haloNight.toDisplayString (false) + " by night)");
        check (wedgeNight.getRed() > wedge.getRed() + 15 && wedgeNight.getRed() > wedgeNight.getBlue(),
               "...the wedge itself lit warm white (" + wedgeNight.toDisplayString (false) + ")");
        io->setCabinDark (0.0f);
        const auto bottomRow = (juce::Rectangle<float> (0.0f, 750.0f, layout::width, 180.0f) * k).toNearestInt();
        check (alikeIn (day, shot(), bottomRow), "...and back at 0 the bottom row is the day's again, to the pixel");
    }

    // THE WORLD BEHIND THE GLASS: Io, in the speed's gear, parked at PARK.
    std::vector<redpaw::Windscreen*> views;
    allOf (*face, views);
    auto* view = views.empty() ? nullptr : views.front();
    const auto line = [view] { return view != nullptr ? view->statusLine() : juce::String ("no view"); };
    check (view != nullptr && line().startsWith ("GPU  Io"), "the world behind the glass is Io (" + line() + ")");
    if (lever != nullptr)
    {
        pull (1);
        check (line().contains ("gear 1") && ! line().contains ("PARK"), "...on SLOW in its first gear (" + line() + ")");
        pull (2);
        check (line().contains ("gear 2") && ! line().contains ("PARK"), "...on FAST in its second (" + line() + ")");
        pull (0);
        check (line().contains ("gear 2 PARK"), "...and parked at PARK, the gear kept (" + line() + ")");
        pull (1);
    }

    // THE AMP'S KNOBS ARE THE WORLD'S INPUTS, and THE NIGHT GLOW FOLLOWS IT. The drive is stepped here, by no time: the
    // window is never shown, so no frame takes the knobs to it.
    {
        auto* d = view != nullptr ? dynamic_cast<io::IoDrive*> (&view->drive()) : nullptr;
        const auto knobs = [d] { if (d == nullptr) return std::array<double, 3> { -1.0, -1.0, -1.0 }; d->step (0.0); return d->io().knobs(); };
        const auto words = [] (const std::array<double, 3>& k) { return juce::String (k[0], 3) + ", " + juce::String (k[1], 3) + ", " + juce::String (k[2], 3); };
        setPlain (params::preampGain, 10.0f);
        setPlain (params::ampBlend, 0.25f);
        settle();
        const auto first = knobs();
        setPlain (params::preampGain, 2.5f);
        setPlain (params::ampBlend, 0.8f);
        settle();
        const auto then = knobs();
        check (std::abs (first[0] - 1.0) < 1.0e-6 && std::abs (first[1] - 0.25) < 1.0e-6 && std::abs (then[0] - 0.25) < 1.0e-6
                   && std::abs (then[1] - 0.8) < 1.0e-6,
               "DRIVE's and BLEND's places over their ranges reach the drive: 10 and 0.25 as " + words (first) + ", 2.5 and 0.8 as "
                   + words (then));
        check (std::abs (first[2] - io::IoEditor::presenceAtRest) < 1.0e-9 && std::abs (then[2] - io::IoEditor::presenceAtRest) < 1.0e-9,
               "...and PRESENCE, frozen, the prototype's " + juce::String (io::IoEditor::presenceAtRest) + " (" + juce::String (then[2]) + ")");

        const auto darkAt = [&] (float drive)
        {
            setPlain (params::preampGain, drive);
            setPlain (params::ampBlend, 1.0f);
            settle();
            if (d != nullptr)
            {
                d->step (0.0);
                d->io().settle();       // the knobs reached at once, not eased
                d->step (0.0);
            }
            io->followWorld();
            return std::pair<float, float> { d != nullptr ? d->cabinDark() : -1.0f, io->cabinDark() };
        };
        const auto step = [] (float share) { return (float) juce::roundToInt (share * (float) io::Cabin::darkSteps) / (float) io::Cabin::darkSteps; };
        const auto night = darkAt (10.0f), noon = darkAt (0.0f);
        check (night.first > 0.5f && juce::approximatelyEqual (night.second, step (night.first))
                   && noon.first < 0.2f && juce::approximatelyEqual (noon.second, step (noon.first)),
               "the night glow follows the world: the cabin " + juce::String (night.first, 3) + " dark at DRIVE 10, the face "
                   + juce::String (night.second, 3) + "; " + juce::String (noon.first, 3) + " at DRIVE 0, the face " + juce::String (noon.second, 3));
    }

    // THE WORLD'S FILES, as the build lays them out: what io::shippedArt reads from the factory folder's Faces/IoBass.
    {
        const auto art = io::shippedArt();
        juce::StringArray lost;
        for (const auto& [plate, name] : { std::pair { art.day, "dashboard.webp" }, std::pair { art.dark, "dashboard_dark.webp" } })
            if (const auto pic = plate ? plate() : io::IoGl::Picture {}; ! pic.valid() || pic.width != 3344 || pic.height != 1882)
                lost.add (name);
        std::array<bool, io::IoWorld::numVariants> filled {};
        for (const auto& base : art.finds ? art.finds() : std::vector<std::string>())
        {
            const int role = io::IoWorld::roleOf (base.substr (0, base.find ('/')));
            const auto bytes = art.bytes ? art.bytes (base + ".mesh") : std::vector<std::uint8_t>();
            const auto mesh = io::IoWorld::readMesh (bytes.data(), bytes.size());
            if (role >= 0 && mesh.ok && mesh.nv > 0 && mesh.ni > 0 && art.text && ! art.text (base + ".txt").empty())
                filled[(size_t) role] = true;
        }
        for (size_t v = 0; v < filled.size(); ++v)
            if (! filled[v])
                lost.add (juce::String ("finds/") + io::IoWorld::variants[v] + "3d");
        const auto pit = art.text ? io::IoWorld::pitRadiusOf (art.text ("tubebase3d/tubebase.txt")) : std::nullopt;
        if (! pit || std::abs (*pit - 13.0) > 1.0e-9)
            lost.add ("the skylight's pit radius (finds/tubebase3d/tubebase.txt)");
        check (lost.isEmpty(), "the world finds every file it reads in the factory folder's Faces/" + juce::String (ORBITAMP_PRODUCT_NAME)
                                   + (lost.isEmpty() ? juce::String() : ": not " + lost.joinIntoString (", ")));
    }

    face.reset();
    amp.reset();
    std::printf ("%s\n", failed == 0 ? "PASSED" : "FAILED");
    return failed == 0 ? 0 : 1;
}
