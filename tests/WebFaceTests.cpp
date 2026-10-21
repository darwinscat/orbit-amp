// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Gate for ORBITAMP'S FACE AS THE SITE GETS IT (src/ui/WebFaceplate.h). A fresh OrbitAmp opens its window
// and writes the kit, and the kit must be what the site can build the face back from: its own format,
// never a console's; the cabin and a poster per channel the stage's size, twice and once; every place on
// the stage, by the app's own names; the cabin the poster with the live parts taken out and NOTHING else;
// each live part's opening sprite laid on the cabin giving the poster back; and the window left as it
// opened — every parameter where it stood, the channels' memory as it was.

#include "AmpChannels.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Parameters.h"
#include "ui/WebFaceplate.h"

#include <cstdio>

namespace
{
    juce::Image load (const juce::File& f) { return juce::ImageFileFormat::loadFrom (f); }

    /** The largest difference of any channel of any pixel between two images of one size. */
    int worstDifference (const juce::Image& a, const juce::Image& b)
    {
        if (a.getBounds() != b.getBounds())
            return 256;
        const juce::Image::BitmapData pa (a, juce::Image::BitmapData::readOnly);
        const juce::Image::BitmapData pb (b, juce::Image::BitmapData::readOnly);
        int worst = 0;
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
            {
                const auto ca = pa.getPixelColour (x, y), cb = pb.getPixelColour (x, y);
                worst = juce::jmax (worst, std::abs ((int) ca.getRed()   - (int) cb.getRed()),
                                           std::abs ((int) ca.getGreen() - (int) cb.getGreen()),
                                           std::abs ((int) ca.getBlue()  - (int) cb.getBlue()));
            }
        return worst;
    }

    bool isRect (const juce::var& v)
    {
        return v.isObject() && v.hasProperty ("x") && v.hasProperty ("y") && v.hasProperty ("w") && v.hasProperty ("h");
    }

    juce::Rectangle<float> rectOf (const juce::var& v)
    {
        return { (float) v["x"], (float) v["y"], (float) v["w"], (float) v["h"] };
    }

    /** Every object under `v`, depth first. */
    void walk (const juce::var& v, const std::function<void (const juce::var&)>& each)
    {
        if (auto* a = v.getArray())
            for (const auto& x : *a)
                walk (x, each);
        else if (auto* o = v.getDynamicObject())
        {
            each (v);
            for (const auto& p : o->getProperties())
                walk (p.value, each);
        }
    }
}

int main()
{
    using namespace orbitamp;
    const juce::ScopedJuceInitialiser_GUI juceInit;

    int failed = 0;
    const auto check = [&failed] (bool ok, const juce::String& what)
    {
        std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
        if (! ok)
            ++failed;
    };

   #if JUCE_LINUX
    std::printf ("skip  OrbitAmp's window, on a system that may have no display\n");
   #else
    auto processor = std::make_unique<AmpProcessor> (ORBITAMP_GATE_PRODUCT());
    processor->inlineLoads = true;            // no message loop here: a model lands where it is asked for
    processor->prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> window (processor->createEditor());
    auto* editor = dynamic_cast<AmpEditor*> (window.get());
    check (editor != nullptr, "OrbitAmp opens its own window");
    if (editor == nullptr)
        return 1;

    std::vector<float> opened;
    for (auto* p : processor->getParameters())
        opened.push_back (p->getValue());
    const auto size = editor->getBounds();
    const auto memory = processor->apvts.state.getChildWithName (AmpChannels::nodeType()).createCopy();

    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("OrbitAmpWebFaceTest-" + juce::String (juce::Time::currentTimeMillis()));
    WebFaceplate::writeKit (*editor, dir);

    // THE WINDOW AS IT OPENED: the face is stood in states for the pictures, never left in one.
    {
        bool same = true;
        auto params = processor->getParameters();
        for (int i = 0; i < params.size(); ++i)
            if (! juce::exactlyEqual (params[i]->getValue(), opened[(size_t) i]))
            {
                same = false;
                std::printf ("      %s moved: %f -> %f\n", params[i]->getName (64).toRawUTF8(),
                             (double) opened[(size_t) i], (double) params[i]->getValue());
            }
        check (same, "every parameter is where it opened after the export");
        check (processor->apvts.state.getChildWithName (AmpChannels::nodeType()).isEquivalentTo (memory),
               "...the channels' memory is as it was, though every channel was played for its poster");
        check (editor->getBounds() == size, "...and the window is the size it opened at");
    }

    check (dir.getChildFile ("done.txt").existsAsFile(), "the kit says it is whole (done.txt)");
    const auto json = juce::JSON::parse (dir.getChildFile ("places.json"));
    check (json.isObject(), "places.json reads");
    if (! json.isObject())
        return 1;

    // ITS OWN FORMAT: none of a console's fields, so a console parser cannot half-read it.
    check (json["format"].toString() == "orbitamp-web-faceplate" && (int) json["schema"] == 1,
           "the format is the faceplate's own, schema 1");
    bool noConsole = true;
    for (const auto* key : { "drive", "tones", "keys", "holes", "sliders", "lever", "world" })
        noConsole = noConsole && ! json.hasProperty (key);
    check (noConsole, "...and carries none of a console's top-level fields");

    const float k = (float) json["scale"];
    const auto stage = rectOf (json["stage"]);
    const auto twice = (stage * k).toNearestInt();
    const auto cabin = load (dir.getChildFile (json["cabin"].toString()));
    check (k == 2.0f && cabin.getBounds() == twice && load (dir.getChildFile (json["cabin1x"].toString())).getBounds() == stage.toNearestInt(),
           "the cabin is the stage at twice its size, and once");

    // A POSTER PER CHANNEL, each its own: the keys and the pill differ.
    const auto& posters = *json["posters"].getArray();
    const auto& keys = *json["places"]["amp"]["channelRow"]["keys"].getArray();
    juce::Image opening;
    {
        bool sized = posters.size() == keys.size() && posters.size() > 1;
        std::vector<juce::Image> shots;
        for (const auto& p : posters)
        {
            shots.push_back (load (dir.getChildFile (p["poster"].toString())));
            sized = sized && shots.back().getBounds() == twice
                          && load (dir.getChildFile (p["poster1x"].toString())).getBounds() == stage.toNearestInt();
        }
        bool distinct = true;
        for (size_t i = 1; i < shots.size(); ++i)
            distinct = distinct && worstDifference (shots[i - 1], shots[i]) > 0;
        check (sized, "a poster for each of the device's " + juce::String (keys.size()) + " channels, the stage twice and once");
        check (distinct, "...each its own");
        for (int i = 0; i < keys.size(); ++i)
            if (keys.getReference (i)["opens"].toString() == "lit")
                for (size_t j = 0; j < shots.size(); ++j)
                    if (posters.getReference ((int) j)["pack"] == keys.getReference (i)["pack"])
                        opening = shots[j];
        check (opening.isValid(), "...and the channel the window opened on is one of them");
    }
    if (! opening.isValid())
        return 1;

    // THE PLACES: by the app's own names, and on the stage.
    {
        juce::StringArray missing;
        const auto need = [&] (const char* path)
        {
            auto v = json["places"];
            for (const auto& part : juce::StringArray::fromTokens (path, ".", ""))
                v = v[juce::Identifier (part)];
            if (v.isVoid())
                missing.add (path);
        };
        for (const auto* p : { "amp.block", "amp.device", "amp.channelRow", "amp.gain", "amp.gainKnob", "amp.smooth",
                               "amp.picture", "amp.shapeBand", "amp.tones", "amp.eqCurve", "amp.eqMode", "amp.inWall",
                               "amp.outWall", "amp.loudnessTag", "delay.block", "delay.time", "delay.field", "delay.mix",
                               "delay.repeats", "delay.dark", "delay.offset", "delay.comb", "reverb.block",
                               "reverb.character", "reverb.mix", "reverb.decay", "reverb.pre", "reverb.picture",
                               "cab.block", "cab.ir", "cab.hpf", "cab.lpf", "cab.trim", "cab.picture", "inColumn",
                               "outColumn", "tuner", "emblem", "chrome", "layoutStrip", "footer" })
            need (p);
        check (missing.isEmpty(), "every place the site reads is there" + (missing.isEmpty() ? juce::String() : ": missing " + missing.joinIntoString (", ")));
        check (json["places"]["amp"]["tones"].size() == 3, "...the device's three tones among them");

        bool onStage = true, named = true;
        walk (json["places"], [&] (const juce::var& v)
        {
            if (isRect (v))
                onStage = onStage && stage.contains (rectOf (v)) && ! rectOf (v).isEmpty();
            if (v.hasProperty ("param"))
                named = named && processor->apvts.getParameter (v["param"].toString()) != nullptr;
        });
        check (onStage, "every place stands on the stage");
        check (named, "...and every parameter a place names is one of the amp's");
    }

    // EVERY SPRITE IS THERE, and its opaque bounds inside it.
    {
        bool all = true;
        for (const auto& s : json["sprites"].getDynamicObject()->getProperties())
        {
            const auto img = load (dir.getChildFile (s.name.toString()));
            all = all && img.isValid() && img.getBounds().toFloat().contains (rectOf (s.value));
        }
        check (all && json["sprites"].getDynamicObject()->getProperties().size() > 50,
               "every sprite is written, its opaque bounds inside it (" + juce::String (json["sprites"].getDynamicObject()->getProperties().size()) + ")");
    }

    // THE CABIN IS THE POSTER LESS ITS LIVE PARTS, and each live part's opening sprite gives it back.
    {
        auto cleared = opening.createCopy(), cabinOut = cabin.createCopy(), rebuilt = cabin.createCopy();
        int worst = 0;
        juce::String worstAt;
        std::vector<std::pair<juce::Rectangle<int>, juce::String>> laid;
        const auto lay = [&] (const juce::String& file, juce::Rectangle<float> at, const juce::String& what)
        {
            const auto box = (at * k).toNearestInt();
            for (auto* img : { &cleared, &cabinOut })
            {
                juce::Graphics g (*img);
                g.setColour (juce::Colours::black);
                g.fillRect (box);
            }
            const auto img = load (dir.getChildFile (file));
            if (! img.isValid() || img.getBounds() != box.withZeroOrigin())
            {
                worst = 256;
                worstAt = what + " (no sprite of its size)";
                return;
            }
            juce::Graphics (rebuilt).drawImageAt (img, box.getX(), box.getY());
            laid.push_back ({ box, what });
        };
        walk (json["places"], [&] (const juce::var& v)
        {
            if (v["live"].isObject() && v["live"].hasProperty ("opens"))
                lay (v["live"]["opens"].toString(), rectOf (v["live"]["at"]), v["live"]["opens"].toString());
        });
        for (const auto& key : keys)
            lay (key[juce::Identifier (key["opens"].toString())].toString(), rectOf (key), key["key"].toString());
        // The pill's sprite is the playing channel's.
        for (const auto& c : *json["device"]["channels"].getArray())
            for (const auto& key : keys)
                if (key["opens"].toString() == "lit" && key["pack"] == c["pack"])
                    lay (c["device"].toString(), rectOf (json["places"]["amp"]["device"]["live"]["at"]), "device");

        // ...and what stands over a live part, laid again after it (the console's mode over its curve).
        walk (json["places"], [&] (const juce::var& v)
        {
            if (v["over"].isObject())
            {
                const auto box = (rectOf (v["over"]["at"]) * k).toNearestInt();
                juce::Graphics (rebuilt).drawImageAt (load (dir.getChildFile (v["over"]["sprite"].toString())), box.getX(), box.getY());
            }
        });
        for (const auto& [box, what] : laid)
            if (const int d = worstDifference (rebuilt.getClippedImage (box).createCopy(), opening.getClippedImage (box).createCopy()); d > worst)
            {
                worst = d;
                worstAt = what;
            }
        const auto inside = [&] (const juce::Image& a, const juce::Image& b) { return worstDifference (a, b); };
        check (inside (cleared, cabinOut) == 0, "outside its live parts, the cabin is the poster pixel for pixel");
        if (const int d = inside (rebuilt, opening); d > worst)
        {
            worst = d;
            worstAt = "the whole window";
        }
        check (worst <= 6, "every live part's opening sprite on the cabin gives the poster back (worst "
                               + juce::String (worst) + "/255" + (worstAt.isEmpty() ? juce::String() : ", " + worstAt) + ")");
    }

    std::printf ("      written to %s\n", dir.getFullPathName().toRawUTF8());
    window.reset();
    processor->releaseResources();
    if (failed == 0)
        dir.deleteRecursively();                 // kept when red, to be looked at
   #endif

    std::printf ("%s\n", failed == 0 ? "PASSED" : "FAILED");
    return failed == 0 ? 0 : 1;
}
