// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "StandaloneInputs.h"

#include "PluginProcessor.h"

#include <felitronics/appkit/InputList.h>

#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <memory>

#if defined (JucePlugin_Build_Standalone) && JucePlugin_Build_Standalone
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
 #define INPUTS_STAND_ALONE 1
#else
 #define INPUTS_STAND_ALONE 0
#endif

namespace orbitamp::standaloneInputs
{

namespace appkit = felitronics::appkit;

#if INPUTS_STAND_ALONE
namespace
{

/** The picker JUCE's holder hands every open input to, and what follows the interface and the amp's mode on the
    message thread. It outlives the holder (DeletedAtShutdown goes after the window), so the holder never calls into a
    picker gone; it reaches the holder and the amp through getInstance() each time, never a pointer kept. */
class Picker final : public juce::StandalonePluginHolder::InputPicker,
                     public juce::DeletedAtShutdown,
                     private juce::Timer
{
public:
    static inline Picker* instance = nullptr;
    static constexpr int listItem = 0x7f000000;   // the list's and its line's answers, which no menu reads

    explicit Picker (juce::StandalonePluginHolder& holder)
    {
        instance = this;
        memory.read  = [] (const std::string& key) -> std::string
        {
            auto* s = settings();
            return s != nullptr ? s->getValue (juce::String (key)).toStdString() : std::string();
        };
        memory.write = [] (const std::string& key, const std::string& value)
        {
            if (auto* s = settings())
                s->setValue (juce::String (key), juce::String (value));
        };
        follow();                        // the interface as it stands, before the first block hears the pick
        holder.setInputPicker (this);
        startTimerHz (30);
    }

    ~Picker() override { instance = nullptr; }

    // The audio side (JUCE's holder, under its audio callback lock).
    void inputsOpened (juce::AudioIODevice& io) override
    {
        picker.prepare (io.getCurrentSampleRate(), appkit::openInputBits (io.getActiveInputChannels()));
    }

    void pick (const float* const* in, int numIn, float* const* out, int numOut, int numSamples) noexcept override
    {
        picker.process (in, numIn, out, numOut, numSamples);
    }

    bool listed() const { return count > 0; }
    InputChoice playing() const { return applied; }

    void addTo (juce::PopupMenu& menu, bool capitals)
    {
        appkit::InputList::Feed feed;
        feed.lit   = [] (int in) { return instance != nullptr && instance->heard (in); };
        feed.tag   = [] (int in) { return instance != nullptr ? instance->tagOf (in) : juce::String(); };
        feed.level = [] (int in) { return instance != nullptr && in >= 0 && in < (int) held.size() ? held[(size_t) in] : 0.0f; };
        feed.picked = [] (int in, const juce::ModifierKeys& mods)
        {
            if (instance != nullptr)
                instance->choose (in, mods.isShiftDown());
        };

        menu.addCustomItem (listItem, std::make_unique<ListItem> (std::make_unique<appkit::InputList> (device, count, appkit::InputListStyle {},
                                                                                                std::move (feed))));
        const juce::String how = stereo ? "Click: the left amp's input, Shift-click: the right one's"
                                        : "Shift-click adds a second input: the two together, which combs a DI with a mic";
        menu.addItem (listItem + 1, capitals ? how.toUpperCase() : how, false, false);
    }

private:
    /** The list as a menu's item, which a click leaves open. */
    struct ListItem final : juce::PopupMenu::CustomComponent
    {
        explicit ListItem (std::unique_ptr<appkit::InputList> l) : juce::PopupMenu::CustomComponent (false), list (std::move (l))
        {
            addAndMakeVisible (*list);
        }
        void getIdealSize (int& w, int& h) override { w = list->getWidth(); h = list->getHeight(); }
        void resized() override { list->setTopLeftPosition (0, 0); }
        std::unique_ptr<appkit::InputList> list;
    };

    appkit::InputPick picker;
    appkit::InputMemory memory;
    static inline std::array<float, InputChoice::maxInputs> held {};   // each input's meter as drawn, held and falling
    juce::String device;                                                // the interface's name, as its input
    int count = 0;                                                      // ...its inputs, open or not
    std::uint64_t open = 0;                                             // ...those open, input k's bit
    InputChoice mono, pair = InputChoice::paired (0, 1), applied;
    bool stereo = false;

    static juce::PropertySet* settings()
    {
        auto* holder = juce::StandalonePluginHolder::getInstance();
        return holder != nullptr ? holder->settings.get() : nullptr;
    }

    static AmpProcessor* amp()
    {
        auto* holder = juce::StandalonePluginHolder::getInstance();
        return holder != nullptr ? dynamic_cast<AmpProcessor*> (holder->processor.get()) : nullptr;
    }

    static std::string stereoKey (const juce::String& name) { return (name + " (STEREO)").toStdString(); }

    void timerCallback() override
    {
        for (int in = 0; in < count; ++in)
            held[(size_t) in] = std::max (picker.takeLevel (in), held[(size_t) in] * 0.85f);
        follow();
    }

    /** The interface and the amp's mode as they stand now, and the choice they ask for into the pick. */
    void follow()
    {
        auto* holder = juce::StandalonePluginHolder::getInstance();
        auto* io = holder != nullptr ? holder->deviceManager.getCurrentAudioDevice() : nullptr;
        if (io == nullptr)
        {
            count = 0;
            return;
        }

        const auto name = holder->deviceManager.getAudioDeviceSetup().inputDeviceName;
        count = std::min ((int) io->getInputChannelNames().size(), InputChoice::maxInputs);
        open  = appkit::openInputBits (io->getActiveInputChannels());

        if (name != device)
        {
            device = name;
            const auto key = name.toStdString();
            if (! memory.knows (key))
            {
                // Seen for the first time: it keeps playing what it played, and every input opens for the list.
                const auto first = seed (open, amp() != nullptr ? amp()->inputSource() : params::InputSource::left);
                memory.remember (key, first.mono);
                memory.remember (stereoKey (name), first.stereo);
                openInputs (InputChoice::firstInputs (count));
            }
            mono = InputChoice::fromText (memory.read (appkit::InputMemory::keyOf (key))).value_or (InputChoice::single (0));
            pair = InputChoice::fromText (memory.read (appkit::InputMemory::keyOf (stereoKey (name)))).value_or (InputChoice::paired (0, 1));
        }

        stereo = amp() != nullptr && ! amp()->choosesInput();
        const auto want = (stereo ? pair : mono).fittedTo (open);
        if (want != applied || picker.chosen() != want)
        {
            applied = want;
            picker.choose (want);
        }
    }

    /** These inputs open besides those open already: the device opens them at once. */
    void openInputs (std::uint64_t bits)
    {
        auto* holder = juce::StandalonePluginHolder::getInstance();
        if (holder == nullptr || (open | bits) == open)
            return;
        auto setup = holder->deviceManager.getAudioDeviceSetup();
        setup.useDefaultInputChannels = false;
        for (int in = 0; in < count; ++in)
            if (((bits >> in) & 1u) != 0)
                setup.inputChannels.setBit (in);
        holder->deviceManager.setAudioDeviceSetup (setup, true);
        holder->saveAudioDeviceState();
    }

    bool heard (int in) const
    {
        return in == applied.a || (applied.kind != InputChoice::Kind::one && in == applied.b);
    }

    juce::String tagOf (int in) const
    {
        if (applied.kind == InputChoice::Kind::pair)
            return applied.a == applied.b ? (in == applied.a ? "LR" : "") : in == applied.a ? "L" : in == applied.b ? "R" : "";
        return applied.kind == InputChoice::Kind::sum && heard (in) ? "+" : "";
    }

    void choose (int in, bool shift)
    {
        if (in < 0 || in >= count)
            return;
        openInputs (std::uint64_t (1) << in);      // a closed one opens to be heard
        auto& now = stereo ? pair : mono;
        now = clicked (now.fittedTo (open), in, shift, stereo);
        memory.remember (stereo ? stereoKey (device) : device.toStdString(), now);
        follow();
    }
};

} // namespace
#endif

void start()
{
   #if INPUTS_STAND_ALONE
    if (Picker::instance == nullptr)
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
            new Picker (*holder);            // DeletedAtShutdown owns it
   #endif
}

bool listed()
{
   #if INPUTS_STAND_ALONE
    return Picker::instance != nullptr && Picker::instance->listed();
   #else
    return false;
   #endif
}

juce::String state()
{
   #if INPUTS_STAND_ALONE
    if (listed())
        return words (Picker::instance->playing());
   #endif
    return {};
}

bool addTo ([[maybe_unused]] juce::PopupMenu& menu, [[maybe_unused]] bool capitals)
{
   #if INPUTS_STAND_ALONE
    if (listed())
    {
        Picker::instance->addTo (menu, capitals);
        return true;
    }
   #endif
    return false;
}

} // namespace orbitamp::standaloneInputs
