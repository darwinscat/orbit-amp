// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// THE WINDSCREEN ON ITS OWN. A window the windscreen's shape with the view in it and a line under it
// saying what it is doing — no audio, no dashboard. Keys 1 2 3 are the gears, T is TURBO, 0 or P is PARK, the arrows
// steer (or drag across the glass), M cycles GPU / CPU / STILL, S cycles the render scale, F the
// frame-rate cap. For looking at the view and measuring it. The same can be set from the command line
// — --mode=gpu|cpu|still --gear=1..3 --turbo --park --scale=0.5 --cap=30 --steer=-1..1 — and --report prints
// the status line to stdout once a second, so a run can be measured without a hand on it.

#include "Windscreen.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <iostream>

namespace
{
    constexpr int glassWidth = 1440, glassHeight = 270, lineHeight = 26;

    class Stand final : public juce::Component,
                        private juce::Timer
    {
    public:
        explicit Stand (const juce::StringArray& args)
        {
            for (const auto& a : args)
            {
                const auto value = a.fromFirstOccurrenceOf ("=", false, false);
                if (a.startsWith ("--mode="))
                    view.setMode (value == "cpu" ? Mode::cpu : value == "still" ? Mode::still : Mode::gpu);
                else if (a.startsWith ("--gear="))  view.setGear (value.getIntValue());
                else if (a == "--turbo")            view.setTurbo (true);
                else if (a == "--park")             view.setParked (true);
                else if (a.startsWith ("--scale=")) view.setRenderScale (value.getFloatValue());
                else if (a.startsWith ("--cap="))   view.setFrameRateCap (value.getIntValue());
                else if (a.startsWith ("--steer=")) view.steer (value.getFloatValue());
                else if (a == "--report")           report = true;
            }
            addAndMakeVisible (view);
            addAndMakeVisible (line);
            line.setFont (juce::FontOptions (13.0f, juce::Font::plain));
            line.setColour (juce::Label::textColourId, juce::Colour (0xffd9cbbd));
            line.setColour (juce::Label::backgroundColourId, juce::Colour (0xff0b0608));
            setSize (glassWidth, glassHeight + lineHeight);
            setWantsKeyboardFocus (true);
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
            if (c >= '1' && c <= '3') { view.setGear (c - '0'); return true; }
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
        using Mode = orbitamp::redpaw::Windscreen::Mode;
        orbitamp::redpaw::Windscreen view;
        juce::Label line;
        bool report = false;
        int ticks = 0;

        void timerCallback() override
        {
            const auto status = view.statusLine() + "   cap " + juce::String (view.frameRateCap());
            line.setText (status + "      1 2 3 gear   T turbo   0/P park   arrows / drag steer   M mode   S scale   F cap", juce::dontSendNotification);
            if (report && (++ticks % 4) == 0)
                std::cout << status << std::endl;
        }
    };

    class Demo final : public juce::JUCEApplication
    {
    public:
        const juce::String getApplicationName() override    { return "RedPawAmp windscreen"; }
        const juce::String getApplicationVersion() override { return "0"; }

        void initialise (const juce::String&) override
        {
            window = std::make_unique<Window> (getApplicationName(), juce::Colours::black, juce::DocumentWindow::allButtons);
            window->setUsingNativeTitleBar (true);
            window->setContentOwned (new Stand (juce::StringArray::fromTokens (getCommandLineParameters(), true)), true);
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
