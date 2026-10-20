// MULTIWRAP HOST — opens the one VST3 and the one CLAP the toy built and holds them to what they must be.
//
//     MultiwrapHost <bundle.vst3> <file.clap> list|single
//
// list    the toy's binary of three plug-ins (ToyPluginList.h). Its VST3 factory registers six classes,
//         each with the class id, the category and the name the amp's own 0.8.0 bundle shipped with
//         (the ids below, read off those bundles' moduleinfo.json). JUCE's VST3 host finds three plug-ins
//         in it and makes all three at once, plus a second OrbitAmp: each plays its own sum, block after
//         block in turn; a parameter set on one is in its state, and that state restores a second
//         instance of the same plug-in and is turned away by another; every one has its editor open at
//         the same time. Its CLAP factory counts three plug-ins with the amps' CLAP ids, makes all three
//         at once, and each plays its own sum; an id it does not hold makes nothing.
// single  the toy's OrbitAmp built alone, with no plug-in list: two classes with OrbitAmp's ids, one CLAP
//         plug-in — the build every amp is today, through the patched wrappers.
//
// It prints what it found and what it expected, and exits 1 at the first count that does not match, or
// after every check if any sum, id or state was wrong.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wnon-virtual-dtor", "-Wzero-as-null-pointer-constant", "-Wshadow")
#include <juce_audio_processors_headless/format_types/VST3_SDK/pluginterfaces/base/ipluginbase.h>
JUCE_END_IGNORE_WARNINGS_GCC_LIKE

#include <clap/clap.h>

#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

namespace
{

// What each amp's own 0.8.0 bundle registered, and what it plays in the toy (ToyProcessors.h).
struct Amp
{
    const char* name;
    const char* component;    // VST3 class ids, as moduleinfo.json writes them
    const char* controller;
    const char* clapId;
    float ratio;              // y / x on a signal that flips sign every sample
};

constexpr Amp amps[]
{
    { "OrbitAmp",  "ABCDEF019182FAEB446361744F616D70", "ABCDEF011234ABCD446361744F616D70", "com.darwinscat.orbitamp",   0.5f },
    { "CatpitAmp", "ABCDEF019182FAEB4463617443747074", "ABCDEF011234ABCD4463617443747074", "com.darwinscat.catpitamp",  1.5f },
    { "RedPawAmp", "ABCDEF019182FAEB4463617452647077", "ABCDEF011234ABCD4463617452647077", "com.darwinscat.redpawamp", -1.0f },
};

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 64;
constexpr float level = 0.5f;

bool allGood = true;

bool check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  ok    " : "  FAIL  ") << what << std::endl;
    allGood = allGood && ok;
    return ok;
}

// A class id as moduleinfo.json and the VST3 SDK's FUID::toString write it. A Windows TUID is laid out
// as a GUID — its first three fields little-endian — so its bytes are read in that order there.
juce::String classIdString (const Steinberg::TUID cid)
{
   #if COM_COMPATIBLE
    constexpr int order[] { 3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15 };
   #else
    constexpr int order[] { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
   #endif

    juce::String result;

    for (auto i : order)
        result << juce::String::toHexString ((int) (unsigned char) cid[i]).paddedLeft ('0', 2).toUpperCase();

    return result;
}

// The binary inside a VST3 bundle, where the bundle format puts it for this system.
juce::File vst3Binary (const juce::File& bundle)
{
   #if JUCE_WINDOWS
    return bundle.getChildFile ("Contents/x86_64-win").getChildFile (bundle.getFileName());
   #elif JUCE_LINUX
    return bundle.getChildFile ("Contents/x86_64-linux").getChildFile (bundle.getFileNameWithoutExtension() + ".so");
   #else
    return bundle.getChildFile ("Contents/MacOS").getChildFile (bundle.getFileNameWithoutExtension());
   #endif
}

// x = +level, -level, ... — the same every block, a block being of even length.
void fill (juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample (channel, i, (i % 2 == 0) ? level : -level);
}

// Does `out` hold `ratio` times the signal fill() makes, from sample `from` on, on both channels?
bool playsAs (const juce::AudioBuffer<float>& out, float ratio, int from)
{
    for (int channel = 0; channel < 2; ++channel)
        for (int i = from; i < out.getNumSamples(); ++i)
            if (std::abs (out.getSample (channel, i) - ratio * ((i % 2 == 0) ? level : -level)) > 1.0e-5f)
                return false;

    return true;
}

//==============================================================================
// THE FACTORY ITSELF, raw: what a host reads before it makes anything — every class, in order, with its id.
void checkVst3Factory (const juce::File& bundle, juce::Span<const Amp> want)
{
    std::cout << "VST3 factory of " << vst3Binary (bundle).getFullPathName() << std::endl;

    juce::DynamicLibrary library;

    if (! check (library.open (vst3Binary (bundle).getFullPathName()), "the binary opens"))
        return;

   #if JUCE_LINUX
    using ModuleEntry = bool (*) (void*);
    using ModuleExit  = bool (*)();
    if (auto entry = (ModuleEntry) library.getFunction ("ModuleEntry"))
        entry (library.getNativeHandle());
   #elif JUCE_WINDOWS
    using ModuleEntry = bool (*)();
    using ModuleExit  = bool (*)();
    if (auto entry = (ModuleEntry) library.getFunction ("InitDll"))
        entry();
   #endif

    using GetPluginFactory = Steinberg::IPluginFactory* (PLUGIN_API*)();
    const auto getFactory = (GetPluginFactory) library.getFunction ("GetPluginFactory");

    if (check (getFactory != nullptr, "GetPluginFactory is exported"))
    {
        auto* factory = getFactory();
        const auto count = factory->countClasses();

        check (count == (Steinberg::int32) (2 * want.size()),
               "it registers " + juce::String (count) + " classes, a component and a controller for each of "
                   + juce::String ((int) want.size()));

        for (Steinberg::int32 i = 0; i < count && (size_t) i < 2 * want.size(); ++i)
        {
            Steinberg::PClassInfo info {};
            factory->getClassInfo (i, &info);

            const auto& amp = want[(size_t) i / 2];
            const auto isComponent = i % 2 == 0;
            const auto id = classIdString (info.cid);

            check (id == (isComponent ? amp.component : amp.controller)
                       && juce::String (info.category) == (isComponent ? "Audio Module Class" : "Component Controller Class")
                       && juce::String (info.name) == amp.name,
                   "class " + juce::String (i) + ": " + id + "  " + juce::String (info.category) + "  " + juce::String (info.name)
                       + "  (shipped: " + (isComponent ? amp.component : amp.controller) + ")");
        }

        factory->release();
    }

   #if JUCE_LINUX
    if (auto exit = (ModuleExit) library.getFunction ("ModuleExit"))
        exit();
   #elif JUCE_WINDOWS
    if (auto exit = (ModuleExit) library.getFunction ("ExitDll"))
        exit();
   #endif
}

//==============================================================================
// THROUGH A HOST: JUCE's VST3 host finds the plug-ins, makes them all at once and plays them in turn.
void checkVst3Instances (const juce::File& bundle, juce::Span<const Amp> want)
{
    std::cout << "VST3 plug-ins in " << bundle.getFullPathName() << std::endl;

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> types;
    format.findAllTypesForFile (types, bundle.getFullPathName());

    if (! check (types.size() == (int) want.size(), "JUCE's host finds " + juce::String (types.size()) + " plug-ins"))
        return;

    struct Live
    {
        const Amp* amp;
        std::unique_ptr<juce::AudioPluginInstance> instance;
        bool fresh = true;
    };

    std::vector<Live> live;
    live.reserve (8);

    auto make = [&] (const Amp& amp) -> juce::AudioPluginInstance*
    {
        for (auto* type : types)
        {
            if (type->name == amp.name)
            {
                juce::String error;
                auto instance = format.createInstanceFromDescription (*type, sampleRate, blockSize, error);

                if (! check (instance != nullptr, juce::String ("made ") + amp.name + " " + error))
                    return nullptr;

                check (type->manufacturerName == "Darwin's Cat" && type->version == "0.8.0",
                       juce::String (amp.name) + ": " + type->manufacturerName + ", " + type->version);

                instance->enableAllBuses();
                instance->prepareToPlay (sampleRate, blockSize);
                live.push_back ({ &amp, std::move (instance) });
                return live.back().instance.get();
            }
        }

        check (false, juce::String ("no plug-in named ") + amp.name);
        return nullptr;
    };

    for (const auto& amp : want)
        if (make (amp) == nullptr)
            return;

    auto play = [&] (Live& one, float ratio, const juce::String& what)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        fill (buffer);
        one.instance->processBlock (buffer, midi);

        // CatpitAmp's first sample after a prepare has no previous one (ToyProcessors.h): from the second on.
        check (playsAs (buffer, ratio, one.fresh ? 1 : 0), what);
        one.fresh = false;
    };

    // Every plug-in of the binary alive at once, each playing its own sum, in turn, twice over.
    for (int round = 0; round < 2; ++round)
        for (auto& one : live)
            play (one, one.amp->ratio, juce::String (one.amp->name) + " plays y = " + juce::String (one.amp->ratio) + " x, round "
                                           + juce::String (round + 1) + " of " + juce::String ((int) live.size()) + " live");

    // State: a gain set on one OrbitAmp is in its state, which a second OrbitAmp — made now, beside the
    // others — takes, and which RedPawAmp turns away.
    auto& orbit = live.front();
    orbit.instance->getParameters()[0]->setValueNotifyingHost (0.625f);   // 1.25 on 0..2
    play (orbit, 1.25f, "OrbitAmp, its gain set to 1.25, plays y = 1.25 x");

    juce::MemoryBlock state;
    orbit.instance->getStateInformation (state);

    if (make (want[0]) != nullptr)
    {
        auto& second = live.back();
        second.instance->setStateInformation (state.getData(), (int) state.getSize());
        play (second, 1.25f, "a second OrbitAmp, given the first one's state, plays y = 1.25 x ("
                                 + juce::String ((int) live.size()) + " live)");
    }

    if (want.size() == 3)
    {
        auto& redPaw = live[2];
        redPaw.instance->setStateInformation (state.getData(), (int) state.getSize());
        play (redPaw, -1.0f, "RedPawAmp, handed OrbitAmp's state, turns it away and still plays y = -x");
        play (live[1], 1.5f, "CatpitAmp, beside them all, still plays y = 1.5 x");
    }

    // Every one's editor open at the same time.
    std::vector<std::unique_ptr<juce::AudioProcessorEditor>> editors;

    for (auto& one : live)
    {
        editors.emplace_back (one.instance->createEditorIfNeeded());
        check (editors.back() != nullptr, juce::String (one.amp->name) + " opens its editor, "
                                               + juce::String ((int) editors.size()) + " open");
    }

    editors.clear();

    for (auto& one : live)
        one.instance->releaseResources();
}

//==============================================================================
// THE CLAP FACTORY, raw, as a CLAP host reads it.
uint32_t noEvents (const clap_input_events*)                                 { return 0; }
const clap_event_header_t* noEvent (const clap_input_events*, uint32_t)     { return nullptr; }
bool dropEvent (const clap_output_events*, const clap_event_header_t*)      { return true; }

void checkClap (const juce::File& file, juce::Span<const Amp> want)
{
    std::cout << "CLAP factory of " << file.getFullPathName() << std::endl;

    juce::DynamicLibrary library;

    if (! check (library.open (file.getFullPathName()), "the binary opens"))
        return;

    const auto* entry = (const clap_plugin_entry*) library.getFunction ("clap_entry");

    if (! check (entry != nullptr && entry->init (file.getFullPathName().toRawUTF8()), "clap_entry is exported and inits"))
        return;

    const auto* factory = (const clap_plugin_factory*) entry->get_factory (CLAP_PLUGIN_FACTORY_ID);
    const auto count = factory->get_plugin_count (factory);

    if (check (count == want.size(), "it counts " + juce::String (count) + " plug-ins"))
    {
        for (uint32_t i = 0; i < count; ++i)
        {
            const auto* descriptor = factory->get_plugin_descriptor (factory, i);
            juce::StringArray features;

            for (auto* feature = descriptor->features; feature != nullptr && *feature != nullptr; ++feature)
                features.add (*feature);

            check (juce::String (descriptor->id) == want[i].clapId && juce::String (descriptor->name) == want[i].name
                       && juce::String (descriptor->vendor) == "Darwin's Cat" && juce::String (descriptor->version) == "0.8.0",
                   "plug-in " + juce::String (i) + ": " + descriptor->id + "  " + descriptor->name + "  "
                       + descriptor->vendor + " " + descriptor->version + "  [" + features.joinIntoString (", ") + "]");
        }

        // A list answers past its last entry with nothing; the unpatched wrapper hands its one descriptor
        // to any index, and the switched-off patch keeps that as it was.
        if (want.size() > 1)
            check (factory->get_plugin_descriptor (factory, count) == nullptr, "no descriptor past the last");

        clap_host host {};
        host.clap_version = CLAP_VERSION;
        host.name = "MultiwrapHost";
        host.vendor = "Darwin's Cat";
        host.url = "";
        host.version = "1";
        host.get_extension = [] (const clap_host*, const char*) -> const void* { return nullptr; };
        host.request_restart = [] (const clap_host*) {};
        host.request_process = [] (const clap_host*) {};
        host.request_callback = [] (const clap_host*) {};

        // Every plug-in of the binary alive at once.
        std::vector<const clap_plugin*> plugins;

        for (const auto& amp : want)
        {
            const auto* plugin = factory->create_plugin (factory, &host, amp.clapId);

            if (check (plugin != nullptr && plugin->init (plugin) && plugin->activate (plugin, sampleRate, 1, blockSize)
                           && plugin->start_processing (plugin)
                           && juce::String (plugin->desc->id) == amp.clapId,
                       juce::String ("made, activated and started ") + amp.clapId + ", "
                           + juce::String ((int) plugins.size() + 1) + " live"))
                plugins.push_back (plugin);
            else
                plugins.push_back (nullptr);
        }

        clap_input_events inEvents { nullptr, noEvents, noEvent };
        clap_output_events outEvents { nullptr, dropEvent };

        for (int round = 0; round < 2; ++round)
        {
            for (size_t i = 0; i < plugins.size(); ++i)
            {
                if (plugins[i] == nullptr)
                    continue;

                juce::AudioBuffer<float> input (2, blockSize), output (2, blockSize);
                fill (input);
                output.clear();

                clap_audio_buffer in {}, out {};
                in.data32 = const_cast<float**> (input.getArrayOfWritePointers());
                in.channel_count = 2;
                out.data32 = const_cast<float**> (output.getArrayOfWritePointers());
                out.channel_count = 2;

                clap_process process {};
                process.steady_time = -1;
                process.frames_count = blockSize;
                process.audio_inputs = &in;
                process.audio_outputs = &out;
                process.audio_inputs_count = 1;
                process.audio_outputs_count = 1;
                process.in_events = &inEvents;
                process.out_events = &outEvents;

                const auto status = plugins[i]->process (plugins[i], &process);
                check (status != CLAP_PROCESS_ERROR && playsAs (output, want[i].ratio, round == 0 ? 1 : 0),
                       juce::String (want[i].name) + " plays y = " + juce::String (want[i].ratio) + " x, round "
                           + juce::String (round + 1));
            }
        }

        for (auto* plugin : plugins)
        {
            if (plugin != nullptr)
            {
                plugin->stop_processing (plugin);
                plugin->deactivate (plugin);
                plugin->destroy (plugin);
            }
        }

        check (factory->create_plugin (factory, &host, "com.darwinscat.nosuchamp") == nullptr,
               "an id it does not hold makes nothing");
    }

    entry->deinit();
}

} // namespace

int main (int argc, char** argv)
{
    if (argc != 4 || (juce::String (argv[3]) != "list" && juce::String (argv[3]) != "single"))
    {
        std::cerr << "usage: MultiwrapHost <bundle.vst3> <file.clap> list|single" << std::endl;
        return 2;
    }

    const juce::ScopedJuceInitialiser_GUI juceLibrary;

    const auto bundle = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
    const auto clap = juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]);
    const auto want = juce::String (argv[3]) == "list" ? juce::Span<const Amp> (amps)
                                                       : juce::Span<const Amp> (amps, 1);

    checkVst3Factory (bundle, want);
    checkVst3Instances (bundle, want);
    checkClap (clap, want);

    std::cout << (allGood ? "ALL PASS" : "FAILED") << std::endl;
    return allGood ? 0 : 1;
}
