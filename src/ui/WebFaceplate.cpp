// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "WebFaceplate.h"

#include "../AmpChannels.h"
#include "../PluginEditor.h"
#include "../device/FactoryShelf.h"
#include "../device/PackOpening.h"
#include "MeterRail.h"
#include "Prefs.h"
#include "scope/ShapeView.h"

namespace orbitamp
{

namespace
{
    using Obj = juce::DynamicObject::Ptr;

    Obj object() { return new juce::DynamicObject(); }

    juce::var rectVar (juce::Rectangle<float> r)
    {
        auto o = object();
        o->setProperty ("x", r.getX()); o->setProperty ("y", r.getY());
        o->setProperty ("w", r.getWidth()); o->setProperty ("h", r.getHeight());
        return juce::var (o.get());
    }

    juce::var colourVar (juce::Colour c)
    {
        return juce::String::formatted ("#%02x%02x%02x%02x", c.getRed(), c.getGreen(), c.getBlue(), c.getAlpha());
    }

    /** Where a picture's opaque part sits, in its own pixels — what the consoles' kits give
        (face::opaqueBounds): every pixel more than half covered. */
    juce::Rectangle<float> opaqueBounds (const juce::Image& img)
    {
        const juce::Image::BitmapData px (img, juce::Image::BitmapData::readOnly);
        int x0 = img.getWidth(), y0 = img.getHeight(), x1 = -1, y1 = -1;
        for (int y = 0; y < img.getHeight(); ++y)
            for (int x = 0; x < img.getWidth(); ++x)
                if (px.getPixelColour (x, y).getAlpha() > 128)
                {
                    x0 = juce::jmin (x0, x); y0 = juce::jmin (y0, y);
                    x1 = juce::jmax (x1, x); y1 = juce::jmax (y1, y);
                }
        return x1 < 0 ? juce::Rectangle<float>()
                      : juce::Rectangle<float> ((float) x0, (float) y0, (float) (x1 - x0 + 1), (float) (y1 - y0 + 1));
    }

    /** One parameter moved for a picture and put back where it stood, whatever stood it there. */
    template <typename Fn>
    void withParam (AmpProcessor& amp, const juce::String& id, float normalised, Fn&& fn)
    {
        auto* p = amp.apvts.getParameter (id);
        if (p == nullptr)
            return;
        const float was = p->getValue();
        p->setValueNotifyingHost (normalised);
        fn();
        p->setValueNotifyingHost (was);
    }

    float normalisedOf (AmpProcessor& amp, const juce::String& id, float plain)
    {
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (amp.apvts.getParameter (id)))
            return p->convertTo0to1 (plain);
        return 0.0f;
    }
}

void WebFaceplate::exportAfterOpening (AmpEditor& editor, const juce::File& folder)
{
    // Long enough for the window to have settled: the packs read, the device's channels on their row,
    // the first repaints of everything that polls.
    juce::Timer::callAfterDelay (3000, [safe = juce::Component::SafePointer<AmpEditor> (&editor), folder]
    {
        if (safe != nullptr)
            writeKit (*safe, folder);
    });
}

void WebFaceplate::writeKit (AmpEditor& editor, const juce::File& folder)
{
    WebFaceplate (editor, folder).write();
}

WebFaceplate::WebFaceplate (AmpEditor& e, const juce::File& folder) : editor (e), amp (e.amp), dir (folder) {}

//==============================================================================
// THE LIVE STILL

namespace
{
    /** An audio device's part, played by a thread: the riff, looped, through the processor in blocks at
        the clock's pace — so the scope, the meters, the echo and the room all hear someone playing. */
    class RiffPlayer final : public juce::Thread
    {
    public:
        RiffPlayer (juce::AudioProcessor& p, juce::AudioBuffer<float> r, double rate)
            : juce::Thread ("live still riff"), amp (p), riff (std::move (r)), fs (rate) {}

        void run() override
        {
            constexpr int block = 256;
            const int ins = amp.getTotalNumInputChannels();
            juce::AudioBuffer<float> buf (juce::jmax (1, ins, amp.getTotalNumOutputChannels()), block);
            juce::MidiBuffer midi;
            int pos = 0;
            auto due = juce::Time::getMillisecondCounterHiRes();
            while (! threadShouldExit())
            {
                buf.clear();
                for (int i = 0; i < block; ++i, pos = (pos + 1) % riff.getNumSamples())
                    for (int c = 0; c < ins; ++c)
                        buf.setSample (c, i, riff.getSample (0, pos));
                {
                    const juce::ScopedLock sl (amp.getCallbackLock());
                    amp.processBlock (buf, midi);
                }
                due += 1000.0 * block / fs;
                if (const auto ms = due - juce::Time::getMillisecondCounterHiRes(); ms > 1.0)
                    wait ((int) ms);
            }
        }

    private:
        juce::AudioProcessor& amp;
        juce::AudioBuffer<float> riff;
        double fs;
    };
}

void WebFaceplate::liveShotAfterOpening (AmpEditor& ed, const juce::File& folder, const juce::File& riff)
{
    juce::Timer::callAfterDelay (3000, [safe = juce::Component::SafePointer<AmpEditor> (&ed), folder, riff]
    {
        if (safe != nullptr)
            (new WebFaceplate (*safe, folder))->playAndShoot (riff);   // lives until the app quits, below
    });
}

void WebFaceplate::playAndShoot (const juce::File& riffFile)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (riffFile));
    if (reader == nullptr || reader->lengthInSamples < 1)
    {
        dir.createDirectory();
        dir.getChildFile ("done.txt").replaceWithText ("no riff: " + riffFile.getFullPathName());
        juce::JUCEApplicationBase::quit();
        return;
    }
    const double fs = reader->sampleRate;
    juce::AudioBuffer<float> riff (1, (int) reader->lengthInSamples);
    reader->read (&riff, 0, riff.getNumSamples(), 0, true, false);

    dir.createDirectory();
    dir.getChildFile ("done.txt").deleteFile();

    // The face as it ships (freeze), then played: the X88-IR's CRUNCH, DRIVE past its middle, the device's
    // tone moved off flat so its curve shows, the echo in, the loudness tag and the spectra on.
    freeze();
    auto& pre = editor.faceplate.preamp;
    if (const auto* dev = pre.shelf.deviceOf (AmpChannels (amp).playing()))
        for (int k = 0; k < dev->channels.size(); ++k)
            if (dev->channelNames[k].equalsIgnoreCase ("Crunch"))
            {
                pre.playPack (dev->channels[k]);
                break;
            }
    standPlain (params::preampGain, 6.0f);
    const float tones[] = { 0.78f, 0.28f, 0.74f };
    for (size_t i = 0; i < pre.eq.bands.size() && i < std::size (tones); ++i)
        standParam (pre.eq.bands[i].param, tones[i]);
    standPlain (params::delayMix, 22.0f);
    pre.showsLoudnessTag = true;
    prefs::spectraCache (amp.product()).store (true);
    editor.applyRowStates();

    juce::AudioProcessor& processor = amp;
    processor.setRateAndBufferSizeDetails (fs, 256);
    processor.prepareToPlay (fs, 256);
    auto* player = new RiffPlayer (processor, std::move (riff), fs);
    player->startThread (juce::Thread::Priority::highest);

    // Nine seconds into the riff: the captures warm, the echo and the room full, the meters moving.
    juce::Timer::callAfterDelay (9000, [this, player]
    {
        const auto face = shootEditor (2.0f);
        writePng ("live.png", face);

        // 16:9 around it, on the ground the device stands on: the face whole, never stretched.
        const int h = face.getHeight() + 2 * 98;
        const int w = juce::roundToInt (h * 16.0 / 9.0);
        juce::Image wide (juce::Image::RGB, w, h, false);
        {
            juce::Graphics g (wide);
            g.fillAll (theme::ground);
            g.drawImageAt (face, (w - face.getWidth()) / 2, (h - face.getHeight()) / 2);
        }
        writePng ("live-16x9.png", wide);

        player->stopThread (2000);
        dir.getChildFile ("done.txt").replaceWithText ("done");
        juce::JUCEApplicationBase::quit();
    });
}

//==============================================================================

void WebFaceplate::write()
{
    dir.createDirectory();
    dir.getChildFile ("sprites").createDirectory();
    dir.getChildFile ("done.txt").deleteFile();

    freeze();

    // Everything is measured, and every part's sprites taken, on the face as it opens — the channel it
    // opened on, the defaults the site plays.
    auto o = object();
    o->setProperty ("format", "orbitamp-web-faceplate");
    o->setProperty ("schema", 1);
    o->setProperty ("stage", rectVar ({ 0.0f, 0.0f, (float) AmpEditor::baseWidth, (float) editor.baseHeight() }));
    o->setProperty ("scale", scale);
    o->setProperty ("places", places());
    const auto refs = references();

    // THE CABIN: the window with every part the site draws taken out.
    {
        std::vector<juce::Component*> hidden;
        for (auto* c : live)
            if (c->isVisible())
            {
                c->setVisible (false);
                hidden.push_back (c);
            }
        writePng ("cabin.png", shootEditor (scale));
        writePng ("cabin@1x.png", shootEditor (1.0f));
        for (auto* c : hidden)
            c->setVisible (true);
        o->setProperty ("cabin", "cabin.png");
        o->setProperty ("cabin1x", "cabin@1x.png");
    }

    o->setProperty ("posters", posters());
    o->setProperty ("device", deviceVar());
    o->setProperty ("lineup", lineupVar());
    {
        auto look = lookVar();
        look.getDynamicObject()->setProperty ("references", refs);   // for the site's tests, never shown
        o->setProperty ("look", look);
    }
    o->setProperty ("posterPatches", patches);
    o->setProperty ("sprites", juce::var (spriteBounds.get()));

    thaw();

    dir.getChildFile ("places.json").replaceWithText (juce::JSON::toString (juce::var (o.get())));
    dir.getChildFile ("done.txt").replaceWithText ("done");
}

//==============================================================================
// THE FROZEN STATE: the shipping face, the device's opening, the defaults the site plays. Stood for the
// pictures, put back after them.

void WebFaceplate::freeze()
{
    auto& face = editor.faceplate;
    auto& pre  = face.preamp;

    // The posters switch channels, and a switch writes the channels' records: kept as they stood.
    channelsMemory = amp.apvts.state.getChildWithName (AmpChannels::nodeType()).createCopy();

    for (const auto& link : params::chainLinks)
    {
        standParam (link.presentParam, 1.0f);
        standParam (link.onParam, juce::String (link.onParam) == params::gateOn ? 0.0f : 1.0f);
    }
    standParam (params::tunerMute, 0.0f);

    // The captured block: SMOOTH lit, the device's own tone set, the console flat with its cuts off.
    standParam (params::blockSmooth (pre.blk), 1.0f);
    standChoice (params::blockEqMode (pre.blk), "Device Tone");
    for (auto* p : amp.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (r->getParameterID().startsWith (params::eqId (0, "")) && r->getParameterID() != params::blockEqMode (pre.blk))
                standParam (r->getParameterID(), r->getDefaultValue());
    standParam (params::eqHpfOn (0), 0.0f);
    standParam (params::eqLpfOn (0), 0.0f);
    standPlain (params::preampGain, 5.0f);

    // The chain's tail as the site opens it.
    standChoice (params::reverbType, "Plate");
    standPlain (params::reverbMix, 30.0f);
    standParam (params::delaySync, 1.0f);
    standChoice (params::delayDiv, "1/4");
    standPlain (params::delayBpm, 120.0f);
    standPlain (params::delayMix, 0.0f);
    standPlain (params::delayRepeats, 35.0f);
    standPlain (params::cabIr, (float) params::cabIrDefault);      // the cabinet every amp opens on (resources/cabinets.txt)
    standParam (params::cabHpfOn, 0.0f);
    standParam (params::cabLpfOn, 0.0f);
    standPlain (params::cabHpfHz, 80.0f);
    standPlain (params::cabLpfHz, 7000.0f);
    standParam (params::cabTrimOn, 0.0f);
    standPlain (params::inTrim, 0.0f);
    standPlain (params::outTrim, 0.0f);

    // The window's own preferences, as the face ships — told to the views, never written to the prefs.
    const auto keep = [this] (std::function<void()> back) { thawing.push_back (std::move (back)); };
    {
        const bool was = face.fillsHeight;
        face.setFillsHeight (true);
        keep ([&face, was] { face.setFillsHeight (was); });
    }
    {
        const bool was = pre.eq.rowOnTop;
        face.setEqRowOnTop (false);
        keep ([&face, was] { face.setEqRowOnTop (was); });
    }
    {
        const bool dim = editor.dimRatherThanRemove, demo = editor.showDemo, glyphs = editor.showGlyphs;
        editor.dimRatherThanRemove = editor.showDemo = editor.showGlyphs = false;
        keep ([this, dim, demo, glyphs] { editor.dimRatherThanRemove = dim; editor.showDemo = demo; editor.showGlyphs = glyphs; });
    }
    {
        auto& spectra = prefs::spectraCache (amp.product());
        const bool was = spectra.load();
        spectra.store (false);
        keep ([&spectra, was] { spectra.store (was); });
    }
    {
        // The picture: SHAPE. Picking it writes the session's property — put back as it was, or taken
        // away again where there was none.
        const int was = pre.vizPick;
        const bool had = amp.apvts.state.hasProperty (pre.vizProperty());
        const auto value = amp.apvts.state.getProperty (pre.vizProperty());
        pre.setViz (0);
        keep ([this, &pre, was, had, value]
        {
            pre.setViz (was);
            if (had)
                amp.apvts.state.setProperty (pre.vizProperty(), value, nullptr);
            else
                amp.apvts.state.removeProperty (pre.vizProperty(), nullptr);
        });
    }
    {
        // The loudness tag moves with the dial, and the site has nothing to write it from.
        pre.showsLoudnessTag = false;
        keep ([&pre] { pre.showsLoudnessTag = true; pre.repaint(); });
    }
    editor.applyRowStates();
}

void WebFaceplate::thaw()
{
    putBackParams();
    for (auto it = thawing.rbegin(); it != thawing.rend(); ++it)
        (*it)();
    thawing.clear();

    auto& state = amp.apvts.state;
    if (const auto now = state.getChildWithName (AmpChannels::nodeType()); now.isValid())
        state.removeChild (now, nullptr);
    if (channelsMemory.isValid())
        state.appendChild (channelsMemory, nullptr);

    editor.applyRowStates();
}

void WebFaceplate::standParam (const juce::String& id, float normalised)
{
    if (auto* p = amp.apvts.getParameter (id))
    {
        before.emplace (id, p->getValue());
        p->setValueNotifyingHost (normalised);
    }
}

void WebFaceplate::standPlain (const juce::String& id, float plain)
{
    standParam (id, normalisedOf (amp, id, plain));
}

void WebFaceplate::standChoice (const juce::String& id, const juce::String& text)
{
    if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (amp.apvts.getParameter (id)))
    {
        const auto range = p->getNormalisableRange();
        for (float v = range.start; v <= range.end + 0.5f; v += 1.0f)
            if (p->getText (p->convertTo0to1 (v), 64).equalsIgnoreCase (text))
                return standParam (id, p->convertTo0to1 (v));
    }
}

void WebFaceplate::putBackParams()
{
    for (const auto& [id, v] : before)
        if (auto* p = amp.apvts.getParameter (id))
            p->setValueNotifyingHost (v);
    before.clear();
}

//==============================================================================
// WHERE THINGS STAND.

bool WebFaceplate::stands (const juce::Component& c) const
{
    for (auto* p = &c; p != nullptr && p != &editor; p = p->getParentComponent())
        if (! p->isVisible() || p->getBounds().isEmpty())
            return false;
    return true;
}

/** A part's area in stage units: its bounds up the tree to the editor. Only the editor's own children
    carry the zoom, as a transform over bounds kept in design units, so the sum is the stage's. */
juce::Rectangle<float> WebFaceplate::onStage (const juce::Component& c, juce::Rectangle<float> r) const
{
    for (auto* p = &c; p != nullptr && p != &editor; p = p->getParentComponent())
        r += p->getPosition().toFloat();
    return r;
}

juce::var WebFaceplate::rect (const juce::Component& c, juce::Rectangle<float> area) const
{
    return rectVar (onStage (c, area.isEmpty() ? c.getLocalBounds().toFloat() : area));
}

void WebFaceplate::patch (const juce::String& name, const juce::Component& c, juce::Rectangle<float> area)
{
    if (! stands (c))
        return;
    auto v = rect (c, area);
    v.getDynamicObject()->setProperty ("name", name);
    patches.add (v);
}

//==============================================================================
// THE PICTURES.

juce::Image WebFaceplate::shootEditor (float k) const
{
    const float zoom = (float) editor.getWidth() / (float) AmpEditor::baseWidth;
    const auto shot = editor.createComponentSnapshot (editor.getLocalBounds(), true, k / zoom);
    juce::Image out (juce::Image::ARGB, juce::roundToInt ((float) AmpEditor::baseWidth * k),
                     juce::roundToInt ((float) editor.baseHeight() * k), true);
    juce::Graphics (out).drawImageAt (shot, 0, 0);
    return out;
}

/** A part drawn alone at the files' scale, cut to `area` (its own units; whole pixels at that scale),
    transparent wherever the part paints nothing. */
juce::Image WebFaceplate::shoot (juce::Component& c, juce::Rectangle<float> area) const
{
    const auto a = area.isEmpty() ? c.getLocalBounds().toFloat() : area;
    const auto box = (a * scale).getSmallestIntegerContainer();
    const auto shot = c.createComponentSnapshot (c.getLocalBounds(), true, scale);
    juce::Image out (juce::Image::ARGB, box.getWidth(), box.getHeight(), true);
    juce::Graphics (out).drawImageAt (shot, -box.getX(), -box.getY());
    return out;
}

/** A box of the whole window as it stands, at the files' scale. */
juce::Image WebFaceplate::shootStage (juce::Rectangle<float> stageArea) const
{
    return shootEditor (scale).getClippedImage ((stageArea * scale).toNearestInt()).createCopy();
}

void WebFaceplate::writePng (const juce::String& file, const juce::Image& img) const
{
    const auto f = dir.getChildFile (file);
    f.deleteFile();
    juce::FileOutputStream out (f);
    if (out.openedOk())
        juce::PNGImageFormat().writeImageToStream (img, out);
}

juce::String WebFaceplate::sprite (const juce::String& file, const juce::Image& img)
{
    const auto path = "sprites/" + file;
    writePng (path, img);
    spriteBounds->setProperty (path, rectVar (opaqueBounds (img)));
    return path;
}

juce::String WebFaceplate::spriteOf (const juce::String& file, juce::Component& c, juce::Rectangle<float> area)
{
    return sprite (file, shoot (c, area));
}

juce::String WebFaceplate::slug (const juce::String& name)
{
    juce::String s;
    for (auto ch : name.toLowerCase())
        s << (juce::CharacterFunctions::isLetterOrDigit (ch) ? juce::String::charToString (ch) : juce::String ("-"));
    while (s.contains ("--"))
        s = s.replace ("--", "-");
    return s.trimCharactersAtStart ("-").trimCharactersAtEnd ("-");
}

juce::var WebFaceplate::pills (juce::Component& block, juce::Component& pill, const juce::String& param,
                               const juce::String& stem, const std::vector<int>& indices, juce::StringArray& names)
{
    // Where the pill stands in every state, and the line it opens either side (BlockFrame::legendGap).
    juce::Rectangle<float> box;
    const auto pillIn = [&] { return pill.getBoundsInParent().toFloat().expanded (BlockFrame::legendGap, 0.0f); };
    auto* q = dynamic_cast<juce::RangedAudioParameter*> (amp.apvts.getParameter (param));
    if (q == nullptr)
        return {};
    for (const int i : indices)
        withParam (amp, param, q->convertTo0to1 ((float) i), [&] { box = box.isEmpty() ? pillIn() : box.getUnion (pillIn()); });
    box = (box * scale).getSmallestIntegerContainer().toFloat() / scale;

    auto states = object();
    for (const int i : indices)
    {
        const auto name = q->getText (q->convertTo0to1 ((float) i), 64);
        names.add (name);
        withParam (amp, param, q->convertTo0to1 ((float) i), [&]
        {
            states->setProperty (name, sprite (stem + "-" + slug (name) + ".png", shootStage (onStage (block, box))));
        });
    }
    auto v = rect (pill);
    auto* o = v.getDynamicObject();
    o->setProperty ("param", param);
    auto l = object();
    l->setProperty ("at", rect (block, box));
    l->setProperty ("opens", states->getProperty (q->getText (q->getValue(), 64)));
    l->setProperty ("states", juce::var (states.get()));
    o->setProperty ("live", juce::var (l.get()));
    live.push_back (&pill);
    return v;
}

//==============================================================================
// THE PLACES, by the app's own names.

juce::var WebFaceplate::knobVar (Knob& k, const juce::String& param, const juce::String& file)
{
    auto v = rect (k);
    auto* o = v.getDynamicObject();
    o->setProperty ("param", param);

    // The dial as Knob::paint lays it out: the label's row on top, the largest square under it.
    auto r = k.getLocalBounds();
    r.removeFromTop (k.labelRowHeight);
    const float d = (float) juce::jmin (r.getWidth(), r.getHeight());
    const auto c = onStage (k, r.toFloat()).getCentre();
    auto circle = object();
    circle->setProperty ("cx", c.x); circle->setProperty ("cy", c.y); circle->setProperty ("d", d);
    circle->setProperty ("labelRow", k.labelRowHeight);
    o->setProperty ("knob", juce::var (circle.get()));
    o->setProperty ("label", k.label.toUpperCase());
    o->setProperty ("labelPx", k.labelFontHeight);
    o->setProperty ("accent", colourVar (k.accent));
    o->setProperty ("notches", k.notches);
    o->setProperty ("startAngle", k.startAngle);     // its travel, radians clockwise from twelve: DRIVE's 7:00 to 5:00, the rest the look's
    o->setProperty ("endAngle", k.endAngle);
    o->setProperty ("heat", k.heat);
    o->setProperty ("min", k.getMinimum());
    o->setProperty ("max", k.getMaximum());
    o->setProperty ("units", k.getValue());
    if (auto* p = amp.apvts.getParameter (param))
    {
        o->setProperty ("value", p->getValue());
        o->setProperty ("default", p->getDefaultValue());
    }
    // The number in the cap, as the knob writes it, at eleven places on its travel.
    juce::Array<juce::var> texts;
    for (int i = 0; i <= 10; ++i)
        texts.add (k.textForValue (k.proportionOfLengthToValue ((double) i / 10.0)));
    o->setProperty ("texts", texts);
    o->setProperty ("text", k.textForValue (k.getValue()));

    auto l = object();
    l->setProperty ("at", rect (k));
    l->setProperty ("opens", spriteOf ("open-" + file + ".png", k));
    o->setProperty ("live", juce::var (l.get()));
    live.push_back (&k);
    return v;
}

/** A side column: the site draws it whole (look.column), so it is off the cabin; the lanes are where. */
juce::var WebFaceplate::columnVar (VolumeColumn& c, const juce::String& file)
{
    auto v = rect (c);
    auto* o = v.getDynamicObject();
    o->setProperty ("param", c.side == VolumeColumn::Side::in ? params::inTrim : params::outTrim);
    o->setProperty ("name", c.side == VolumeColumn::Side::in ? "IN" : "OUT");

    // The hand's lane: the track, its centre 0 dB, the ends of the trim six units in from its edges.
    const auto track = onStage (c, c.scaleArea());
    const float half = track.getHeight() * 0.5f - 6.0f;
    auto fader = object();
    fader->setProperty ("x", track.getX()); fader->setProperty ("w", track.getWidth());
    fader->setProperty ("top", track.getY()); fader->setProperty ("bottom", track.getBottom());
    fader->setProperty ("zeroY", track.getCentreY());
    fader->setProperty ("plusEndY", track.getCentreY() - half);
    fader->setProperty ("minusEndY", track.getCentreY() + half);
    fader->setProperty ("rangeDb", params::inTrimRangeDb);
    o->setProperty ("fader", juce::var (fader.get()));
    auto grip = object();
    grip->setProperty ("w", track.getWidth() - 1.5f);
    grip->setProperty ("h", meterrail::gripH);
    o->setProperty ("grip", juce::var (grip.get()));

    // The meter's lane: 0 dBFS at its top, the floor at its bottom, linear in dB between.
    const auto lane = onStage (c, c.columnArea());
    auto meter = object();
    meter->setProperty ("x", lane.getX()); meter->setProperty ("w", lane.getWidth());
    meter->setProperty ("top", lane.getY()); meter->setProperty ("bottom", lane.getBottom());
    meter->setProperty ("floorDb", VolumeColumn::floorDb);
    meter->setProperty ("topDb", 0.0f);
    o->setProperty ("meter", juce::var (meter.get()));

    auto l = object();
    l->setProperty ("at", rect (c));
    l->setProperty ("opens", spriteOf ("open-" + file + ".png", c));
    o->setProperty ("live", juce::var (l.get()));
    live.push_back (&c);
    return v;
}

juce::var WebFaceplate::places()
{
    auto& face = editor.faceplate;
    auto& pre  = face.preamp;
    auto out = object();
    const auto withParamId = [] (juce::var v, const juce::String& id) { v.getDynamicObject()->setProperty ("param", id); return v; };

    // THE AMP.
    {
        auto a = object();
        a->setProperty ("block", rect (pre));
        {
            // The device's pill: its tint follows the channel, a sprite per channel (posters() takes
            // them as it stands each one).
            const auto box = (pre.device.getBoundsInParent().toFloat().expanded (BlockFrame::legendGap, 0.0f) * scale)
                                 .getSmallestIntegerContainer().toFloat() / scale;
            auto v = rect (pre.device);
            auto l = object();
            l->setProperty ("at", rect (pre, box));
            v.getDynamicObject()->setProperty ("live", juce::var (l.get()));
            a->setProperty ("device", v);
            live.push_back (&pre.device);
        }
        {
            auto row = rect (pre.channelRow);
            juce::Array<juce::var> keys;
            const auto* dev = pre.shelf.deviceOf (AmpChannels (amp).playing());
            for (int i = 0; i < (int) pre.channelRow.keys.size(); ++i)
            {
                const auto box = (pre.channelRow.keys[(size_t) i].expanded (1.0f) * scale).getSmallestIntegerContainer().toFloat() / scale;
                auto k = rect (pre.channelRow, box);
                k.getDynamicObject()->setProperty ("key", pre.channelRow.names[i]);
                if (dev != nullptr && juce::isPositiveAndBelow (i, dev->channels.size()))
                {
                    const auto rig = AmpChannels::keyOf (pre.block.packs.getReference (dev->channels[i]));
                    k.getDynamicObject()->setProperty ("pack", rig);
                    k.getDynamicObject()->setProperty ("lit", "sprites/key-" + slug (rig) + "-lit.png");
                    k.getDynamicObject()->setProperty ("unlit", "sprites/key-" + slug (rig) + "-unlit.png");
                    k.getDynamicObject()->setProperty ("opens", i == pre.channelRow.lit ? "lit" : "unlit");
                }
                keys.add (k);
            }
            row.getDynamicObject()->setProperty ("keys", keys);
            row.getDynamicObject()->setProperty ("live", true);
            a->setProperty ("channelRow", row);
            live.push_back (&pre.channelRow);
        }
        a->setProperty ("gain", knobVar (pre.gain, params::preampGain, "gain"));
        a->setProperty ("gainKnob", a->getProperty ("gain")["knob"]);
        a->setProperty ("smooth", withParamId (rect (pre.smoothTag), params::blockSmooth (pre.blk)));
        if (auto* scope = pre.scopes[(size_t) pre.vizPick].get())
        {
            // The well stays on the cabin, empty: no sound was playing. The site draws SHAPE's traces
            // in the band (look.shape).
            a->setProperty ("picture", rect (*scope));
            a->setProperty ("shapeBand", rect (*scope, scope->getLocalBounds().toFloat().reduced (6.0f)));
        }
        juce::Array<juce::var> tones;
        for (size_t i = 0; i < pre.eq.bandKnobs.size(); ++i)
        {
            auto t = knobVar (*pre.eq.bandKnobs[i], pre.eq.bands[i].param, slug (pre.eq.bands[i].label));
            t.getDynamicObject()->setProperty ("name", pre.eq.bands[i].label.toUpperCase());
            t.getDynamicObject()->setProperty ("marker", colourVar (pre.eq.bands[i].colour));
            t.getDynamicObject()->setProperty ("anchorHz", pre.eq.bands[i].anchorHz);
            tones.add (t);
        }
        a->setProperty ("tones", tones);
        {
            auto v = rect (pre.eq.curve);
            auto axis = object();
            axis->setProperty ("minHz", EqCurve::minHz);
            axis->setProperty ("maxHz", EqCurve::maxHz);
            axis->setProperty ("x", "log: minHz at the box's left edge, maxHz at its right");
            axis->setProperty ("rangeDb", EqCurve::rangeDb);
            axis->setProperty ("insetY", 6.0f);   // +-rangeDb at the centre -+ (h/2 - insetY); not clamped
            v.getDynamicObject()->setProperty ("axis", juce::var (axis.get()));
            auto l = object();
            l->setProperty ("at", rect (pre.eq.curve));
            l->setProperty ("opens", spriteOf ("open-eq-curve.png", pre.eq.curve));
            v.getDynamicObject()->setProperty ("live", juce::var (l.get()));
            a->setProperty ("eqCurve", v);
            live.push_back (&pre.eq.curve);
        }
        {
            // Inert, but it stands INSIDE the curve's box, over it: the site lays it again after the curve.
            auto v = rect (pre.eq.modeBtn);
            auto over = object();
            over->setProperty ("at", rect (pre.eq.modeBtn));
            over->setProperty ("sprite", spriteOf ("over-eq-mode.png", pre.eq.modeBtn));
            v.getDynamicObject()->setProperty ("over", juce::var (over.get()));
            a->setProperty ("eqMode", v);
        }
        a->setProperty ("inWall", rect (pre.inMeter));
        a->setProperty ("outWall", rect (pre.outMeter));
        {
            // Where the tag is written: the run of the border right of the device, right-aligned.
            const auto run = pre.borderSlotArea().toFloat();
            const auto from = (float) pre.borderSlotUsed.getRight() + BlockFrame::legendGap;
            a->setProperty ("loudnessTag", rect (pre, run.withLeft (from)));
        }
        out->setProperty ("amp", juce::var (a.get()));

        patch ("smooth", pre.smoothTag);
        patch ("inWall", pre.inMeter);
        patch ("outWall", pre.outMeter);
        patch ("eqMode", pre.eq.modeBtn);
        patch ("eqHpf", pre.eq.hpfSw);
        patch ("eqLpf", pre.eq.lpfSw);
        patch ("eqHpfLabel", pre.eq.hpfLabel);
        patch ("eqLpfLabel", pre.eq.lpfLabel);
        patch ("eqHpfSlope", pre.eq.hpfSlopeBox);
        patch ("eqLpfSlope", pre.eq.lpfSlopeBox);
        for (size_t i = 0; i < pre.expandTags.size(); ++i)
            patch ("pictureGlyph", pre.expandTags[i]);
        patch ("pictureGlyph", pre.screenTag);
        patch ("pictureGlyph", pre.halfTag);
    }

    // THE DELAY.
    {
        auto& dl = face.delay;
        auto d = object();
        d->setProperty ("block", rect (dl));
        d->setProperty ("time", rect (dl.timeSel));
        d->setProperty ("field", rect (dl.field));
        d->setProperty ("mix", knobVar (dl.mix, params::delayMix, "delay-mix"));
        d->setProperty ("repeats", withParamId (rect (dl.repeats), params::delayRepeats));
        d->setProperty ("dark", withParamId (rect (dl.dark), params::delayDark));
        d->setProperty ("offset", withParamId (rect (dl.offset), params::delayOffset));
        {
            // The comb's teeth stand on MIX: a picture at five of its places, the rest as the face froze.
            auto v = rect (dl.view);
            auto byMix = object();
            for (const int mix : { 0, 25, 50, 75, 100 })
                withParam (amp, params::delayMix, normalisedOf (amp, params::delayMix, (float) mix), [&]
                {
                    byMix->setProperty (juce::String (mix), spriteOf ("delay-comb-" + juce::String (mix) + ".png", dl.view));
                });
            v.getDynamicObject()->setProperty ("byMix", juce::var (byMix.get()));
            d->setProperty ("comb", v);
        }
        out->setProperty ("delay", juce::var (d.get()));
        patch ("delayTime", dl.timeSel);
        patch ("delayField", dl.field);
        patch ("delayRepeats", dl.repeats);
        patch ("delayDark", dl.dark);
        patch ("delayOffset", dl.offset);
    }

    // THE REVERB.
    {
        auto& rv = face.reverb;
        auto r = object();
        r->setProperty ("block", rect (rv));
        std::vector<int> rooms;
        for (int i = 0; i < params::reverbCharacters.size(); ++i)
            rooms.push_back (i);
        juce::StringArray names;
        r->setProperty ("character", pills (rv, rv.character, params::reverbType, "room", rooms, names));
        r->setProperty ("mix", knobVar (rv.mix, params::reverbMix, "reverb-mix"));
        r->setProperty ("decay", withParamId (rect (rv.decay), params::reverbDecay));
        r->setProperty ("pre", withParamId (rect (rv.pre), params::reverbPredelay));
        {
            // The tail's stamp is computed from the room, MIX, DECAY and PRE (ReverbBlock::refreshTail):
            // a picture per room at the frozen MIX, DECAY and PRE.
            auto v = rect (rv.view);
            auto byRoom = object();
            for (const int i : rooms)
                withParam (amp, params::reverbType, normalisedOf (amp, params::reverbType, (float) i), [&]
                {
                    byRoom->setProperty (names[i], spriteOf ("reverb-picture-" + slug (names[i]) + ".png", rv.view));
                });
            v.getDynamicObject()->setProperty ("byRoom", juce::var (byRoom.get()));
            r->setProperty ("picture", v);
        }
        out->setProperty ("reverb", juce::var (r.get()));
        patch ("reverbDecay", rv.decay);
        patch ("reverbPre", rv.pre);
    }

    // THE CABINET.
    {
        auto& cb = face.cabinet;
        auto c = object();
        c->setProperty ("block", rect (cb));
        std::vector<int> cabs;
        for (int i = 21; i <= 30 && i < params::cabIrNames.size(); ++i)
            cabs.push_back (i);
        juce::StringArray names;
        c->setProperty ("ir", pills (cb, cb.ir, params::cabIr, "ir", cabs, names));
        const auto switchVar = [&] (ZoneSwitch& sw, const char* id, const juce::String& accent)
        {
            auto v = rect (sw);
            auto* o = v.getDynamicObject();
            o->setProperty ("param", id);
            auto l = object();
            l->setProperty ("at", rect (sw));
            auto states = object();
            for (const bool on : { false, true })
                withParam (amp, id, on ? 1.0f : 0.0f, [&]
                {
                    states->setProperty (on ? "on" : "off", spriteOf (juce::String ("switch-") + (on ? "on" : "off") + "-" + accent + ".png", sw));
                });
            l->setProperty ("states", juce::var (states.get()));
            l->setProperty ("opens", states->getProperty (amp.apvts.getParameter (id)->getValue() > 0.5f ? "on" : "off"));
            o->setProperty ("live", juce::var (l.get()));
            live.push_back (&sw);
            return v;
        };
        c->setProperty ("hpf", switchVar (cb.switches[0].sw, params::cabHpfOn, "orange"));
        c->setProperty ("lpf", switchVar (cb.switches[1].sw, params::cabLpfOn, "violet"));
        c->setProperty ("trim", rect (cb.trimCombo));
        {
            // The IR's own picture — its wave and its facts — with the cuts drawn on it when they are on:
            // a picture per cabinet, per state of the two cuts.
            auto v = rect (cb.wave);
            auto byIr = object();
            for (size_t n = 0; n < cabs.size(); ++n)
                withParam (amp, params::cabIr, normalisedOf (amp, params::cabIr, (float) cabs[n]), [&]
                {
                    auto cuts = object();
                    for (const int k : { 0, 1, 2, 3 })
                        withParam (amp, params::cabHpfOn, (k & 1) ? 1.0f : 0.0f, [&]
                        {
                            withParam (amp, params::cabLpfOn, (k & 2) ? 1.0f : 0.0f, [&]
                            {
                                static const char* const cutNames[] = { "none", "hpf", "lpf", "both" };
                                cuts->setProperty (cutNames[k], spriteOf ("cab-picture-" + slug (names[(int) n]) + "-" + cutNames[k] + ".png", cb.wave));
                            });
                        });
                    byIr->setProperty (names[(int) n], juce::var (cuts.get()));
                });
            v.getDynamicObject()->setProperty ("byIr", juce::var (byIr.get()));
            c->setProperty ("picture", v);
        }
        out->setProperty ("cab", juce::var (c.get()));
        patch ("cabTrim", cb.trimCombo);
    }

    // AROUND THE FACEPLATE.
    out->setProperty ("inColumn", columnVar (editor.inColumn, "in-column"));
    out->setProperty ("outColumn", columnVar (editor.outColumn, "out-column"));
    {
        auto v = rect (editor.tunerStrip);
        auto l = object();
        l->setProperty ("at", rect (editor.tunerStrip));
        l->setProperty ("opens", spriteOf ("open-tuner.png", editor.tunerStrip));
        v.getDynamicObject()->setProperty ("live", juce::var (l.get()));
        out->setProperty ("tuner", v);
        live.push_back (&editor.tunerStrip);
    }
    {
        // The cat, as BrandHeader::paint draws it: a square at the header's left, two units in.
        const auto& b = editor.chrome.brand;
        const float h = (float) b.getHeight();
        out->setProperty ("emblem", rect (b, { 4.0f, 2.0f, h - 4.0f, h - 4.0f }));
    }
    out->setProperty ("chrome", rect (editor.chrome));
    out->setProperty ("layoutStrip", rect (*editor.layoutStrip));
    out->setProperty ("footer", rect (editor.footer));

    // THE REST OF THE WINDOW, inert.
    auto& ch = editor.chrome;
    {
        const auto& b = ch.brand;
        const float h = (float) b.getHeight();
        patch ("brand", b, b.getLocalBounds().toFloat().withTrimmedLeft (h));   // the mark and the words; not the cat
    }
    patch ("undo", ch.undo);
    patch ("redo", ch.redo);
    for (const auto& r : ch.registers)
        patch ("register", *r);
    patch ("preset", ch.preset);
    patch ("fullScreen", ch.fullScreen);
    patch ("setup", ch.gear);
    patch ("layoutStrip", *editor.layoutStrip);
    patch ("footer", editor.footer);
    return juce::var (out.get());
}

/** What the site paints itself, painted by the app at a few places, for the site's tests only. */
juce::var WebFaceplate::references()
{
    auto& pre = editor.faceplate.preamp;
    auto out = object();

    std::vector<std::tuple<juce::String, Knob*, juce::String>> knobs {
        { "gain", &pre.gain, params::preampGain },
        { "reverb-mix", &editor.faceplate.reverb.mix, params::reverbMix },
        { "delay-mix", &editor.faceplate.delay.mix, params::delayMix } };
    for (size_t i = 0; i < pre.eq.bandKnobs.size(); ++i)
        knobs.emplace_back (slug (pre.eq.bands[i].label), pre.eq.bandKnobs[i].get(), pre.eq.bands[i].param);
    for (auto& [name, k, id] : knobs)
    {
        auto at = object();
        auto* p = amp.apvts.getParameter (id);
        for (const auto& [where, v] : std::vector<std::pair<const char*, float>> { { "min", 0.0f }, { "default", p->getDefaultValue() }, { "max", 1.0f } })
            withParam (amp, id, v, [&] { at->setProperty (where, spriteOf ("ref-" + name + "-" + where + ".png", *k)); });
        out->setProperty (name, juce::var (at.get()));
    }

    // The columns at the floor, at -12 dB and at 0 dB, the hand at 0 dB: the reading stood straight
    // into the column, as its own tick would leave it with the hold on the level.
    for (auto* c : { &editor.inColumn, &editor.outColumn })
    {
        auto at = object();
        const float level = c->levelDb, hold = c->holdDb;
        for (const auto& [where, db] : std::vector<std::pair<const char*, float>> { { "floor", -90.0f }, { "-12", -12.0f }, { "0", 0.0f } })
        {
            c->levelDb = c->holdDb = db;
            const auto file = juce::String (where).replace ("-", "minus");
            at->setProperty (where, spriteOf (juce::String (c == &editor.inColumn ? "ref-in-column-" : "ref-out-column-") + file + ".png", *c));
        }
        c->levelDb = level;
        c->holdDb = hold;
        c->repaint();
        out->setProperty (c == &editor.inColumn ? "inColumn" : "outColumn", juce::var (at.get()));
    }

    // The console's curve with every tone at the bottom, the middle and the top of its travel.
    {
        auto at = object();
        for (const auto& [where, v] : std::vector<std::pair<const char*, float>> { { "min", 0.0f }, { "mid", 0.5f }, { "max", 1.0f } })
        {
            std::vector<float> was;
            for (const auto& b : pre.eq.bands)
            {
                auto* p = amp.apvts.getParameter (b.param);
                was.push_back (p->getValue());
                p->setValueNotifyingHost (v);
            }
            at->setProperty (where, spriteOf (juce::String ("ref-eq-curve-") + where + ".png", pre.eq.curve));
            for (size_t i = 0; i < pre.eq.bands.size(); ++i)
                amp.apvts.getParameter (pre.eq.bands[i].param)->setValueNotifyingHost (was[i]);
        }
        out->setProperty ("eqCurve", juce::var (at.get()));
    }
    return juce::var (out.get());
}

/** THE WINDOW ON EACH CHANNEL: the device's channels stood in the row's order, each played the way a hand
    picks it (AmpChannels), the whole window taken, and the keys and the pill as they stand on it; then
    the channel it opened on, played back. */
juce::var WebFaceplate::posters()
{
    auto& pre = editor.faceplate.preamp;
    juce::Array<juce::var> list;
    const int opened = AmpChannels (amp).playing();
    const auto* dev = pre.shelf.deviceOf (opened);
    if (dev == nullptr)
        return list;

    const auto channels = dev->channels;            // copied: playing rebuilds the shelf
    const auto deviceBox = (pre.device.getBoundsInParent().toFloat().expanded (BlockFrame::legendGap, 0.0f) * scale)
                               .getSmallestIntegerContainer().toFloat() / scale;
    for (int k = 0; k < channels.size(); ++k)
    {
        pre.playPack (channels[k]);
        const auto rig = AmpChannels::keyOf (pre.block.packs.getReference (channels[k]));
        auto p = object();
        p->setProperty ("pack", rig);
        p->setProperty ("key", pre.channelRow.names[k]);
        writePng ("poster-" + slug (rig) + ".png", shootEditor (scale));
        writePng ("poster-" + slug (rig) + "@1x.png", shootEditor (1.0f));
        p->setProperty ("poster", "poster-" + slug (rig) + ".png");
        p->setProperty ("poster1x", "poster-" + slug (rig) + "@1x.png");
        list.add (juce::var (p.get()));

        sprite ("device-" + slug (rig) + ".png", shootStage (onStage (pre, deviceBox)));
        for (int i = 0; i < (int) pre.channelRow.keys.size() && i < channels.size(); ++i)
        {
            const auto keyRig = AmpChannels::keyOf (pre.block.packs.getReference (channels[i]));
            const auto box = (pre.channelRow.keys[(size_t) i].expanded (1.0f) * scale).getSmallestIntegerContainer().toFloat() / scale;
            spriteOf ("key-" + slug (keyRig) + (i == k ? "-lit.png" : "-unlit.png"), pre.channelRow, box);
        }
    }
    pre.playPack (opened);
    return list;
}

juce::var WebFaceplate::deviceVar() const
{
    auto& pre = editor.faceplate.preamp;
    auto d = object();
    const auto* dev = pre.shelf.deviceOf (AmpChannels (amp).playing());
    if (dev == nullptr)
        return {};
    static const char* const toneTypes[] = { "clean", "edge", "crunch", "high-gain", "modern" };   // theme::heatColour's five places
    d->setProperty ("name", dev->name);
    juce::Array<juce::var> list;
    for (int i = 0; i < dev->channels.size(); ++i)
    {
        const auto& pack = pre.block.packs.getReference (dev->channels[i]);
        auto c = object();
        c->setProperty ("pack", AmpChannels::keyOf (pack));
        c->setProperty ("key", dev->channelNames[i]);
        c->setProperty ("toneType", toneTypes[juce::jlimit (0, 4, pack.character)]);
        c->setProperty ("name", pack.displayName());
        c->setProperty ("device", "sprites/device-" + slug (AmpChannels::keyOf (pack)) + ".png");
        list.add (juce::var (c.get()));
    }
    d->setProperty ("channels", list);
    return juce::var (d.get());
}

/** WHAT THE AMP PLAYS, assembled — OrbitAmp has no Lineup of its own: the device's channels and what each
    opens on, the channels' memory as AmpChannels keeps it, the tail's defaults, the cabinets the site
    steps through. */
juce::var WebFaceplate::lineupVar() const
{
    auto& pre = editor.faceplate.preamp;
    const auto plain = [this] (const juce::String& id)
    {
        auto* p = dynamic_cast<juce::RangedAudioParameter*> (amp.apvts.getParameter (id));
        return p != nullptr ? p->convertFrom0to1 (p->getValue()) : 0.0f;
    };
    const auto text = [this] (const juce::String& id)
    {
        auto* p = amp.apvts.getParameter (id);
        return p != nullptr ? p->getText (p->getValue(), 64) : juce::String();
    };

    auto l = object();
    const auto* dev = pre.shelf.deviceOf (AmpChannels (amp).playing());
    if (dev != nullptr)
    {
        l->setProperty ("device", dev->name);
        juce::Array<juce::var> list;
        static const char* const toneTypes[] = { "clean", "edge", "crunch", "high-gain", "modern" };
        for (int i = 0; i < dev->channels.size(); ++i)
        {
            const auto& pack = pre.block.packs.getReference (dev->channels[i]);
            auto c = object();
            c->setProperty ("pack", AmpChannels::keyOf (pack));
            c->setProperty ("key", dev->channelNames[i]);
            c->setProperty ("toneType", toneTypes[juce::jlimit (0, 4, pack.character)]);
            // What a first visit opens on: the pack's own tones; DRIVE, SMOOTH and the console carried
            // from the channel the hand left — here, the frozen face's.
            const auto opening = device::PackOpening::of (pack);
            auto o = object();
            o->setProperty ("gain", plain (params::preampGain));
            o->setProperty ("smooth", plain (params::blockSmooth (pre.blk)) > 0.5f);
            juce::Array<juce::var> tones;
            for (size_t t = 0; t < pre.eq.bands.size(); ++t)
                tones.add (opening.meas[t]);
            o->setProperty ("tones", tones);
            c->setProperty ("opening", juce::var (o.get()));
            list.add (juce::var (c.get()));
        }
        l->setProperty ("channels", list);
    }

    auto memory = object();
    memory->setProperty ("kind", "orbitamp");
    juce::Array<juce::var> kept;
    for (const auto& id : AmpChannels (amp).kept())
        kept.add (id);
    memory->setProperty ("keptPerChannel", kept);
    juce::Array<juce::var> carried;
    carried.add (params::preampGain);
    carried.add (params::blockSmooth (pre.blk));
    carried.add (params::eqId (0, "*"));
    memory->setProperty ("firstVisitCarries", carried);
    memory->setProperty ("firstVisitOpens", "the pack's own tones and switches (device::PackOpening)");
    memory->setProperty ("global", "the room, both mixes, the cabinet, the cuts, IN and OUT: one for every channel");
    l->setProperty ("memory", juce::var (memory.get()));

    auto reverb = object();
    reverb->setProperty ("type", text (params::reverbType));
    reverb->setProperty ("mix", plain (params::reverbMix));
    l->setProperty ("reverb", juce::var (reverb.get()));
    auto delay = object();
    delay->setProperty ("division", text (params::delayDiv));
    delay->setProperty ("bpm", plain (params::delayBpm));
    delay->setProperty ("mix", plain (params::delayMix));
    delay->setProperty ("repeats", plain (params::delayRepeats));
    l->setProperty ("delay", juce::var (delay.get()));

    auto cab = object();
    cab->setProperty ("irName", params::cabIrNames[juce::roundToInt (plain (params::cabIr))]);
    juce::Array<juce::var> catalog;
    const auto& shelf = device::shelf::irs (amp.product().name);
    for (int i = 21; i <= 30 && i < params::cabIrNames.size(); ++i)
    {
        auto c = object();
        c->setProperty ("index", i);
        c->setProperty ("name", params::cabIrNames[i]);
        c->setProperty ("file", juce::isPositiveAndBelow (i, (int) shelf.size()) ? shelf[(size_t) i].file : juce::String());
        c->setProperty ("sprite", "sprites/ir-" + slug (params::cabIrNames[i]) + ".png");
        catalog.add (juce::var (c.get()));
    }
    cab->setProperty ("catalog", catalog);
    l->setProperty ("cab", juce::var (cab.get()));
    auto cuts = object();
    cuts->setProperty ("hpfHz", plain (params::cabHpfHz));
    cuts->setProperty ("lpfHz", plain (params::cabLpfHz));
    l->setProperty ("cuts", juce::var (cuts.get()));
    l->setProperty ("turbo", "none");
    l->setProperty ("park", false);
    return juce::var (l.get());
}

/** HOW THIS FACE DRAWS what the site draws itself, read off the painters' own constants where they have
    them; where a painter writes a number inline, the number it writes, and the painter named. */
juce::var WebFaceplate::lookVar() const
{
    auto k = object();

    auto t = object();
    for (const auto& [name, c] : std::vector<std::pair<const char*, juce::Colour>> {
             { "orange", theme::orange }, { "violet", theme::violet }, { "lilac", theme::lilac }, { "tx", theme::tx },
             { "txDim", theme::txDim }, { "txFaint", theme::txFaint }, { "panel", theme::panel }, { "panel2", theme::panel2 },
             { "bezel", theme::bezel }, { "ground", theme::ground }, { "hair", theme::hair }, { "hair2", theme::hair2 },
             { "capTop", theme::capTop }, { "capBottom", theme::capBot }, { "spectrum", theme::spectrum } })
        t->setProperty (name, colourVar (c));
    juce::Array<juce::var> nodes;
    for (const auto& c : theme::eqNode)
        nodes.add (colourVar (c));
    t->setProperty ("eqNode", nodes);
    t->setProperty ("offAlpha", theme::offAlpha);
    k->setProperty ("theme", juce::var (t.get()));

    auto fonts = object();
    fonts->setProperty ("face", theme::displayFont (10.0f).getTypefaceName());
    fonts->setProperty ("tracking", "each glyph advanced by trackingEm x the font's height (theme::drawTracked)");
    k->setProperty ("fonts", juce::var (fonts.get()));

    // Knob.h's paint, as fractions of the dial's side d.
    auto knob = object();
    knob->setProperty ("startAngle", Knob::defaultStart);   // radians, clockwise from twelve o'clock — a knob's own, where it has one, on it
    knob->setProperty ("endAngle", Knob::defaultEnd);
    knob->setProperty ("ring", 0.42f);
    knob->setProperty ("ringStroke", 0.055f);
    knob->setProperty ("track", colourVar (theme::hair2));
    knob->setProperty ("notchOffset", 0.045f);
    knob->setProperty ("notchLength", 0.05f);
    knob->setProperty ("notchWidth", 0.018f);
    knob->setProperty ("notchLitAlpha", 0.75f);
    knob->setProperty ("body", 0.33f);
    knob->setProperty ("bodyTop", colourVar (juce::Colour (0xff23222d)));
    knob->setProperty ("bodyBottom", colourVar (juce::Colour (0xff0e0e14)));
    knob->setProperty ("bodyRim", colourVar (theme::hair2));
    knob->setProperty ("pointerFrom", 0.55f);   // of the body's radius
    knob->setProperty ("pointerTo", 0.94f);
    knob->setProperty ("pointerWidth", 0.035f);
    knob->setProperty ("pointerBrighter", 0.3f);   // the accent brighter by this; on a HEAT dial the heat at the place, by 0.15
    knob->setProperty ("valuePx", 0.19f);
    knob->setProperty ("valueTracking", 0.01f);
    knob->setProperty ("value", colourVar (juce::Colour (0xfff6f4fd)));
    knob->setProperty ("labelColour", colourVar (theme::txDim));
    knob->setProperty ("labelTracking", 0.15f);
    juce::Array<juce::var> heat;
    for (int i = 0; i <= 8; ++i)
        heat.add (colourVar (theme::heatColour ((float) i / 8.0f)));
    knob->setProperty ("heatRamp", heat);   // theme::heatColour at nine even places of the travel
    knob->setProperty ("heatPieces", 72);
    k->setProperty ("knob", juce::var (knob.get()));

    // EqCurve.h's paint.
    auto eq = object();
    eq->setProperty ("well", colourVar (theme::bezel));
    eq->setProperty ("radius", theme::radiusMd);
    eq->setProperty ("rim", colourVar (theme::hair2));
    eq->setProperty ("gridMajor", colourVar (theme::hair2.withAlpha (0.20f)));
    eq->setProperty ("gridMinor", colourVar (theme::hair.withAlpha (0.07f)));
    eq->setProperty ("labels", colourVar (theme::txFaint));
    eq->setProperty ("labelPx", 12.0f);
    eq->setProperty ("fill", colourVar (theme::violet.withAlpha (0.40f)));   // at 0 dB, fading to nothing at the edges
    juce::Array<juce::var> strokes;
    for (const auto& [w, a] : std::vector<std::pair<float, float>> { { 5.0f, 0.12f }, { 2.5f, 0.22f }, { 1.5f, 1.0f } })
    {
        auto s = object();
        s->setProperty ("width", w);
        s->setProperty ("colour", colourVar (theme::orange.withAlpha (a)));
        strokes.add (juce::var (s.get()));
    }
    eq->setProperty ("curve", strokes);
    eq->setProperty ("markerRadius", 3.0f);
    eq->setProperty ("markerHalo", 1.2f);   // in the well's colour, under the dot
    k->setProperty ("eqCurve", juce::var (eq.get()));

    // VolumeColumn.h and MeterRail.h.
    auto col = object();
    col->setProperty ("well", colourVar (theme::bezel));
    col->setProperty ("radius", theme::radiusSm);
    col->setProperty ("fill", "meterrail::paintFill, heat: the thermometer from the floor to 0 dBFS");
    juce::Array<juce::var> fill;
    for (const auto& [at, c] : std::vector<std::pair<float, juce::Colour>> {
             { 0.0f, theme::heatFloor.withAlpha (0.55f) }, { 0.40f, theme::violet.withAlpha (0.60f) },
             { 0.52f, theme::heatGreen.withAlpha (0.75f) }, { 0.70f, theme::heatGreen.withAlpha (0.78f) },
             { 0.80f, theme::heatYellow.withAlpha (0.85f) }, { 0.88f, theme::orange }, { 1.0f, theme::heatRed } })
    {
        auto s = object();
        s->setProperty ("at", at);
        s->setProperty ("colour", colourVar (c));
        fill.add (juce::var (s.get()));
    }
    col->setProperty ("fillStops", fill);   // from the lane's bottom (0) to its top (1)
    col->setProperty ("grip", colourVar (theme::orange));
    col->setProperty ("gripAlphaAtRest", 0.3f);
    col->setProperty ("gripSeat", colourVar (juce::Colours::black.withAlpha (0.45f)));
    col->setProperty ("gripPx", 11.0f);
    col->setProperty ("clipCap", colourVar (juce::Colour (0xffff3b30)));
    col->setProperty ("clipCapIdle", colourVar (juce::Colours::white.withAlpha (0.06f)));
    col->setProperty ("clipCapH", 3.0f);
    col->setProperty ("ticksPerSecond", 30);
    col->setProperty ("releaseDbPerTick", VolumeColumn::releasePerTick);
    col->setProperty ("holdTicks", VolumeColumn::holdTicks);
    col->setProperty ("holdReleaseDbPerTick", VolumeColumn::holdReleasePerTick);
    col->setProperty ("floorDb", VolumeColumn::floorDb);
    k->setProperty ("column", juce::var (col.get()));

    // ShapeView.h, as the consoles' Monitor::shapeForWeb gives it.
    auto shape = object();
    auto dry = object();
    dry->setProperty ("colour", colourVar (theme::lilac.withAlpha (scope::ShapeView::dryAlpha)));
    dry->setProperty ("width", scope::ShapeView::dryWidth);
    auto wet = object();
    wet->setProperty ("colour", colourVar (theme::orange));
    wet->setProperty ("width", scope::ShapeView::wetWidth);
    shape->setProperty ("dry", juce::var (dry.get()));
    shape->setProperty ("wet", juce::var (wet.get()));
    shape->setProperty ("samples", scope::ShapeView::samples);
    shape->setProperty ("fill", scope::ShapeView::fill);
    shape->setProperty ("peakFloor", scope::ShapeView::peakFloor);
    k->setProperty ("shape", juce::var (shape.get()));

    // TunerStrip.h's ink, in the consoles' keys where they mean the same.
    auto tuner = object();
    tuner->setProperty ("panel", colourVar (theme::panel));
    tuner->setProperty ("radius", theme::radiusMd);
    tuner->setProperty ("note", colourVar (theme::tx));
    tuner->setProperty ("inTune", colourVar (TunerStrip::inTune));
    tuner->setProperty ("quiet", colourVar (theme::txDim));     // the octave, the cents
    tuner->setProperty ("idle", colourVar (theme::txFaint));    // the dash while nothing is heard
    tuner->setProperty ("tick", colourVar (theme::orange.withAlpha (0.5f)));
    tuner->setProperty ("majorTick", colourVar (theme::orange.withAlpha (0.9f)));
    tuner->setProperty ("centreTick", colourVar (theme::lilac));
    tuner->setProperty ("needleWarmsToOrangeByCents", 25.0f);
    tuner->setProperty ("notePx", 24.0f);
    tuner->setProperty ("smallPx", 13.0f);
    k->setProperty ("tuner", juce::var (tuner.get()));
    return juce::var (k.get());
}

} // namespace orbitamp
