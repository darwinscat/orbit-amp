// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// THE WINDSCREEN ON ITS OWN. A window the windscreen's shape with the view in it and a line under it
// saying what it is doing — no audio, no dashboard. Keys 1 2 ... are the gears, T is TURBO, 0 or P is PARK, the arrows
// steer (or drag across the glass), M cycles GPU / CPU / STILL, S cycles the render scale, F the
// frame-rate cap. For looking at the view and measuring it. The same can be set from the command line
// — --mode=gpu|cpu|still --gears=1..4 --gear=1..gears --turbo --park --scale=0.5 --cap=30 --steer=-1..1 — and --report prints
// the status line to stdout once a second, so a run can be measured without a hand on it.
//
// For a shot without a hand: --at=x,z,heading (world metres, degrees) puts the rover somewhere first,
// --clock=seconds,metres sets the world's clock, --drive=seconds runs the drive that long at 60 steps a
// second with the steering held before the first frame, --still=out.png writes the STILL to a file once
// it has arrived and quits, --stress=n draws the scene n times a frame for the timer (and holds the
// quality ladder, as --fixed does alone), --quit=seconds ends the run. Built three times: as RedPaw's windscreen over the Plains, (ORBITAMP_VIEW_DEMO_ICE) as
// FrostPaw's over the ice, and (ORBITAMP_VIEW_DEMO_TITAN) as the canopy over Titan's sea — each in its product's gears unless --gears says otherwise.

#include "Windscreen.h"
#if ORBITAMP_VIEW_DEMO_TITAN
 #include "../../titan/view/TitanDrive.h"
#elif ORBITAMP_VIEW_DEMO_ICE
 #include "../../frostpaw/view/IceDrive.h"
#else
 #include "PlainsDrive.h"
#endif

#include <juce_gui_extra/juce_gui_extra.h>

#include <iostream>

namespace
{
    constexpr int glassWidth = 1440, glassHeight = 270, lineHeight = 26;

    using Mode = orbitamp::redpaw::Windscreen::Mode;

    /** The product's gears, as its lever has them (redpaw/RedPawLineup.h): the demo has no lineup. */
   #if ORBITAMP_VIEW_DEMO_TITAN
    constexpr int productGears = 4;     // the Titan face
   #elif ORBITAMP_VIEW_DEMO_ICE
    constexpr int productGears = 3;     // FrostPawAmp
   #else
    constexpr int productGears = 2;     // RedPawAmp
   #endif

    struct Args
    {
        Mode mode = Mode::gpu;
        int gears = productGears, gear = 1; bool turbo = false, park = false;
        float scale = 1.0f, steer = 0.0f; int cap = 60, stress = 1;
        bool report = false, placed = false, clocked = false, fixed = false;
        double x = 0, z = 0, heading = 0, clockSeconds = 0, clockMetres = 0, driveSeconds = 0, quitSeconds = 0, findMetres = 0;
        juce::String stillFile;

        explicit Args (const juce::StringArray& args)
        {
            for (const auto& a : args)
            {
                const auto value = a.fromFirstOccurrenceOf ("=", false, false);
                const auto parts = juce::StringArray::fromTokens (value, ",", "");
                if (a.startsWith ("--mode="))        mode = value == "cpu" ? Mode::cpu : value == "still" ? Mode::still : Mode::gpu;
                else if (a.startsWith ("--gears="))  gears = value.getIntValue();
                else if (a.startsWith ("--gear="))   gear = value.getIntValue();
                else if (a == "--turbo")             turbo = true;
                else if (a == "--park")              park = true;
                else if (a.startsWith ("--scale="))  scale = value.getFloatValue();
                else if (a.startsWith ("--cap="))    cap = value.getIntValue();
                else if (a.startsWith ("--steer="))  steer = value.getFloatValue();
                else if (a == "--report")            report = true;
                else if (a.startsWith ("--at=") && parts.size() >= 3) { placed = true; x = parts[0].getDoubleValue(); z = parts[1].getDoubleValue(); heading = parts[2].getDoubleValue() * juce::MathConstants<double>::pi / 180; }
                else if (a.startsWith ("--clock=") && parts.size() >= 2) { clocked = true; clockSeconds = parts[0].getDoubleValue(); clockMetres = parts[1].getDoubleValue(); }
                else if (a.startsWith ("--drive="))  driveSeconds = value.getDoubleValue();
                else if (a.startsWith ("--still="))  stillFile = value;
                else if (a.startsWith ("--stress=")) stress = value.getIntValue();
                else if (a.startsWith ("--quit="))   quitSeconds = value.getDoubleValue();
                else if (a == "--fixed")             fixed = true;
                else if (a.startsWith ("--find="))   findMetres = value.getDoubleValue();
            }
            if (stress > 1)
                fixed = true;
        }

        /** The drive, placed and driven as asked before the view takes it. */
        std::unique_ptr<orbitamp::redpaw::Drive> makeDrive() const
        {
           #if ORBITAMP_VIEW_DEMO_TITAN
            std::unique_ptr<orbitamp::redpaw::Drive> d = std::make_unique<orbitamp::titan::TitanDrive> (gears);
           #elif ORBITAMP_VIEW_DEMO_ICE
            std::unique_ptr<orbitamp::redpaw::Drive> d = std::make_unique<orbitamp::frostpaw::IceDrive> (gears);
           #else
            std::unique_ptr<orbitamp::redpaw::Drive> d = std::make_unique<orbitamp::redpaw::PlainsDrive> (gears);
           #endif
            d->setGear (gear); d->setTurbo (turbo); d->setParked (park);
            if (placed)
                d->placeAt (x, z, heading);
            if (clocked)
                d->setClock (clockSeconds, clockMetres);
            if (driveSeconds > 0)
            {
                if (! placed)
                    d->placeAt (31500, 3000, 0);
                d->setSteer (steer);
                for (int i = 0, n = juce::roundToInt (driveSeconds * 60); i < n; ++i)
                    d->step (1.0 / 60);
            }
            d->setRepeats (stress);
            if (findMetres > 0)
            {
                if (! placed && driveSeconds <= 0)
                    d->placeAt (31500, 3000, 0);
                std::cout << d->landmarks (findMetres);
            }
            return d;
        }
    };

    class Stand final : public juce::Component,
                        private juce::Timer
    {
    public:
        explicit Stand (const Args& a)
            : args (a), view (a.makeDrive())
        {
            view.setGear (args.gear);
            view.setTurbo (args.turbo);
            view.setParked (args.park);
            view.setRenderScale (args.scale);
            view.setFrameRateCap (args.cap);
            view.steer (args.steer);
            view.setAdaptive (! args.fixed);
            view.setMode (args.mode);
            addAndMakeVisible (view);
            addAndMakeVisible (line);
            line.setFont (juce::FontOptions (13.0f, juce::Font::plain));
            line.setColour (juce::Label::textColourId, juce::Colour (0xffd9cbbd));
            line.setColour (juce::Label::backgroundColourId, juce::Colour (0xff0b0608));
            setSize (glassWidth, glassHeight + lineHeight);
            setWantsKeyboardFocus (true);
            started = juce::Time::getMillisecondCounterHiRes();
            startTimerHz (4);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            line.setBounds (r.removeFromBottom (lineHeight).reduced (8, 0));
            view.setBounds (r);
        }

        void parentHierarchyChanged() override { view.grabKeyboardFocus(); }

        bool keyPressed (const juce::KeyPress& k) override
        {
            const auto c = k.getTextCharacter();
            if (c >= '1' && c < '1' + view.numGears()) { view.setGear (c - '0'); return true; }
            if (c == 't' || c == 'T') { view.setTurbo (! view.isTurbo()); return true; }
            if (c == '0' || c == 'p' || c == 'P') { view.setParked (! view.isParked()); return true; }
            if (c == 'm' || c == 'M')
            {
                view.setMode (view.mode() == Mode::gpu ? Mode::cpu : view.mode() == Mode::cpu ? Mode::still : Mode::gpu);
                return true;
            }
            if (c == 's' || c == 'S')
            {
                const float sc = view.renderScale();
                view.setRenderScale (sc > 0.76f ? 0.75f : sc > 0.51f ? 0.5f : 1.0f);
                return true;
            }
            if (c == 'f' || c == 'F') { view.setFrameRateCap (view.frameRateCap() == 60 ? 30 : 60); return true; }
            return false;
        }

    private:
        Args args;
        orbitamp::redpaw::Windscreen view;
        juce::Label line;
        int ticks = 0;
        double started = 0;

        void timerCallback() override
        {
            const auto status = view.statusLine() + "   cap " + juce::String (view.frameRateCap());
            line.setText (status + "      1.." + juce::String (view.numGears()) + " gear   T turbo   0/P park   arrows / drag steer   M mode   S scale   F cap", juce::dontSendNotification);
            if (args.report && (++ticks % 4) == 0)
                std::cout << status << std::endl;
            if (args.stillFile.isNotEmpty() && view.mode() == Mode::still && view.stillImage().isValid())
            {
                const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (args.stillFile);
                out.deleteFile();
                juce::FileOutputStream stream (out);
                if (stream.openedOk())
                    juce::PNGImageFormat().writeImageToStream (view.stillImage(), stream);
                std::cout << "still " << out.getFullPathName() << " " << view.stillImage().getWidth() << "x" << view.stillImage().getHeight() << std::endl;
                juce::JUCEApplication::getInstance()->systemRequestedQuit();
            }
            if (args.quitSeconds > 0 && juce::Time::getMillisecondCounterHiRes() - started > args.quitSeconds * 1000)
                juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

    class Demo final : public juce::JUCEApplication
    {
    public:
       #if ORBITAMP_VIEW_DEMO_TITAN
        const juce::String getApplicationName() override    { return "Titan canopy"; }
       #elif ORBITAMP_VIEW_DEMO_ICE
        const juce::String getApplicationName() override    { return "FrostPawAmp windscreen"; }
       #else
        const juce::String getApplicationName() override    { return "RedPawAmp windscreen"; }
       #endif
        const juce::String getApplicationVersion() override { return "0"; }

        void initialise (const juce::String&) override
        {
            window = std::make_unique<Window> (getApplicationName(), juce::Colours::black, juce::DocumentWindow::allButtons);
            window->setUsingNativeTitleBar (true);
            window->setContentOwned (new Stand (Args (juce::StringArray::fromTokens (getCommandLineParameters(), true))), true);
            window->setResizable (true, false);
            window->centreWithSize (window->getWidth(), window->getHeight());
            window->setVisible (true);
        }

        void shutdown() override { window.reset(); }
        void systemRequestedQuit() override { quit(); }

    private:
        struct Window final : juce::DocumentWindow
        {
            using DocumentWindow::DocumentWindow;
            void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
        };
        std::unique_ptr<juce::DocumentWindow> window;
    };
}

START_JUCE_APPLICATION (Demo)
