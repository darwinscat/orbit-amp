// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// THE FAMILY'S PLUG-INS AS A HOST OPENS THEM — the one VST3 and the one CLAP that hold every amp of the
// release (CMakeLists.txt: THE FAMILY'S PLUG-INS), held to what they must be:
//
//     orbitamp_bundle_host <family.vst3> <family.clap> [<build-dir>]
//
//   - the VST3 factory registers, for every amp installer/family.ini names (ORBITAMP_FAMILY_AMPS) and in
//     that order, a component and a controller named as the amp, with the class ids that amp's own bundle
//     shipped with in 0.8.0 (below, read off those bundles' moduleinfo.json) — what a saved session looks
//     the amp up by;
//   - JUCE's VST3 host finds one plug-in for each amp and makes them all at once; each is the amp it is
//     named — its own parameters, no two amps' alike — and all of them play, block after block in turn;
//   - given a build directory where each amp's own VST3 was built too (`--target <Amp>_VST3`, the bundle
//     every amp had before), each amp of the family is held to its own, opened in a process of its own: the
//     same parameters — names, ids, labels, defaults, steps — the same channels and latency, and a session
//     saved by either opens in the other with every value it held;
//   - the CLAP factory counts the same amps under their CLAP ids, com.darwinscat.<name>, the family's
//     vendor and version; it makes them all at once, each with the parameters its VST3 has, and all of them
//     play; an id it does not hold makes nothing;
//   - on the Mac, the AU beside the VST3 (<artefacts>/AU/<bundle>.component): its Info.plist lists every
//     amp as the amp's own 0.8.0 AU did — aufx, its code, Dcat, "Darwin's Cat: <Amp>", described <Amp>, the
//     same resource usage — but for the factory function, the one the binary exports, and the version, the
//     build's; JUCE's AU host makes them all at once, each the amp it is named with its VST3's parameters,
//     and all of them play; a component of the binary under a code the list does not hold makes nothing.
//
// The CLAP, the AU and every own bundle are opened in a process of their own (runSelf, which says why); what
// the processes hand each other is left in the temporary folder, orbitamp-bundle-host.
//
// It prints what it found and what it expected, and exits 1 if anything was not as it must be. Not a gate:
// it opens built bundles, which no gate waits for. An amp starting writes where a player's amp would, so
// run it in a home of its own.

#include <juce_audio_processors/juce_audio_processors.h>

#if JUCE_MAC
 #include <AudioToolbox/AudioToolbox.h>
#endif

JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wnon-virtual-dtor", "-Wzero-as-null-pointer-constant", "-Wshadow")
#include <juce_audio_processors_headless/format_types/VST3_SDK/pluginterfaces/base/ipluginbase.h>
JUCE_END_IGNORE_WARNINGS_GCC_LIKE

#include <clap/clap.h>

#include <cmath>
#include <iostream>
#include <map>
#include <memory>
#include <vector>

namespace
{

// What each amp's own 0.8.0 bundle registered: its VST3 component's and controller's class ids, and its
// AU's subtype (read off those bundles' Info.plist: aufx, the subtype, Dcat).
struct Shipped
{
    const char* name;
    const char* component;
    const char* controller;
    const char* auSubtype;
};

constexpr Shipped shipped[]
{
    { "OrbitAmp",  "ABCDEF019182FAEB446361744F616D70", "ABCDEF011234ABCD446361744F616D70", "Oamp" },
    { "CatpitAmp", "ABCDEF019182FAEB4463617443747074", "ABCDEF011234ABCD4463617443747074", "Ctpt" },
    { "RedPawAmp", "ABCDEF019182FAEB4463617452647077", "ABCDEF011234ABCD4463617452647077", "Rdpw" },
};

const Shipped* shippedOf (const juce::String& name)
{
    for (const auto& one : shipped)
        if (name == one.name)
            return &one;

    return nullptr;
}

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 256;

bool allGood = true;

bool check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  ok    " : "  FAIL  ") << what << std::endl;
    allGood = allGood && ok;
    return ok;
}

void note (const juce::String& what)
{
    std::cout << "  --    " << what << std::endl;
}

void pump (int milliseconds)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil (milliseconds);
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

// The binary inside a VST3 bundle, where the bundle format puts it for this system and architecture.
juce::File vst3Binary (const juce::File& bundle)
{
   #if JUCE_MAC
    return bundle.getChildFile ("Contents/MacOS").getChildFile (bundle.getFileNameWithoutExtension());
   #else
    #if JUCE_WINDOWS
     const juce::String system ("-win"), file (bundle.getFileName());
    #else
     const juce::String system ("-linux"), file (bundle.getFileNameWithoutExtension() + ".so");
    #endif

    for (const auto& folder : bundle.getChildFile ("Contents").findChildFiles (juce::File::findDirectories, false))
        if (folder.getFileName().endsWith (system) && folder.getChildFile (file).existsAsFile())
            return folder.getChildFile (file);

    return bundle.getChildFile ("Contents").getChildFile (file);
   #endif
}

//==============================================================================
// THE VST3 FACTORY ITSELF, raw: what a host reads before it makes anything — every class, in order.
void checkVst3Factory (const juce::File& bundle, const juce::StringArray& amps)
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
   #elif JUCE_MAC
    using ModuleEntry = bool (*) (void*);
    using ModuleExit  = bool (*)();
    if (auto entry = (ModuleEntry) library.getFunction ("bundleEntry"))
        entry (nullptr);
   #endif

    using GetPluginFactory = Steinberg::IPluginFactory* (PLUGIN_API*)();
    const auto getFactory = (GetPluginFactory) library.getFunction ("GetPluginFactory");

    if (check (getFactory != nullptr, "GetPluginFactory is exported"))
    {
        auto* factory = getFactory();
        const auto count = factory->countClasses();

        check (count == 2 * amps.size(),
               "it registers " + juce::String (count) + " classes, a component and a controller for each of "
                   + juce::String (amps.size()) + " amps");

        Steinberg::PFactoryInfo factoryInfo {};
        factory->getFactoryInfo (&factoryInfo);
        check (juce::String (factoryInfo.vendor) == "Darwin's Cat", "its vendor is " + juce::String (factoryInfo.vendor));

        for (Steinberg::int32 i = 0; i < count && i < 2 * amps.size(); ++i)
        {
            Steinberg::PClassInfo info {};
            factory->getClassInfo (i, &info);

            const auto& amp = amps[i / 2];
            const auto isComponent = i % 2 == 0;
            const auto id = classIdString (info.cid);
            const auto category = juce::String (isComponent ? "Audio Module Class" : "Component Controller Class");
            const auto line = "class " + juce::String (i) + ": " + id + "  " + juce::String (info.category) + "  "
                              + juce::String (info.name);

            if (const auto* own = shippedOf (amp))
            {
                const juce::String want (isComponent ? own->component : own->controller);
                check (id == want && juce::String (info.category) == category && juce::String (info.name) == amp,
                       line + "  (" + amp + " 0.8.0 shipped: " + want + ")");
            }
            else
            {
                check (juce::String (info.category) == category && juce::String (info.name) == amp,
                       line + "  (" + amp + " shipped in no release before: no ids to hold it to)");
            }
        }

        factory->release();
    }

   #if JUCE_LINUX || JUCE_WINDOWS || JUCE_MAC
    const char* exitName =
       #if JUCE_LINUX
        "ModuleExit";
       #elif JUCE_WINDOWS
        "ExitDll";
       #else
        "bundleExit";
       #endif
    if (auto exit = (ModuleExit) library.getFunction (exitName))
        exit();
   #endif
}

//==============================================================================
// WHAT AN AMP IS, TO A HOST: its parameters, its channels, its latency.
struct Param
{
    juce::String name, id, label;
    float defaultValue = 0.0f;
    int steps = 0;
    bool discrete = false;
};

std::vector<Param> layoutOf (juce::AudioPluginInstance& instance)
{
    std::vector<Param> layout;

    for (auto* parameter : instance.getParameters())
    {
        Param p;
        p.name = parameter->getName (1024);
        p.label = parameter->getLabel();
        p.defaultValue = parameter->getDefaultValue();
        p.steps = parameter->getNumSteps();
        p.discrete = parameter->isDiscrete();

        if (auto* hosted = dynamic_cast<juce::HostedAudioProcessorParameter*> (parameter))
            p.id = hosted->getParameterID();

        layout.push_back (p);
    }

    return layout;
}

juce::StringArray namesOf (const std::vector<Param>& layout)
{
    juce::StringArray names;

    for (const auto& p : layout)
        names.add (p.name);

    return names;
}

// Where two layouts first part, or empty where they are the same.
juce::String differenceOf (const std::vector<Param>& a, const std::vector<Param>& b)
{
    if (a.size() != b.size())
        return juce::String ((int) a.size()) + " parameters against " + juce::String ((int) b.size());

    for (size_t i = 0; i < a.size(); ++i)
    {
        const auto& x = a[i];
        const auto& y = b[i];

        if (x.name != y.name || x.id != y.id || x.label != y.label || std::abs (x.defaultValue - y.defaultValue) > 1.0e-6f
            || x.steps != y.steps || x.discrete != y.discrete)
            return "parameter " + juce::String ((int) i) + ": '" + x.name + "' id " + x.id + " default "
                   + juce::String (x.defaultValue) + " steps " + juce::String (x.steps) + " against '" + y.name + "' id "
                   + y.id + " default " + juce::String (y.defaultValue) + " steps " + juce::String (y.steps);
    }

    return {};
}

std::vector<float> valuesOf (juce::AudioPluginInstance& instance)
{
    std::vector<float> values;

    for (auto* parameter : instance.getParameters())
        values.push_back (parameter->getValue());

    return values;
}

juce::String valuesDifference (const std::vector<float>& a, const std::vector<float>& b, juce::AudioPluginInstance& named)
{
    if (a.size() != b.size())
        return "a different number of values";

    for (size_t i = 0; i < a.size(); ++i)
        if (std::abs (a[i] - b[i]) > 1.0e-4f)
            return "'" + named.getParameters()[(int) i]->getName (256) + "' " + juce::String (a[i]) + " against "
                   + juce::String (b[i]);

    return {};
}

// A block of a quiet sine through it: does every sample come out a number?
bool playsABlock (juce::AudioPluginInstance& instance, int block)
{
    const auto channels = juce::jmax (instance.getTotalNumInputChannels(), instance.getTotalNumOutputChannels(), 1);
    juce::AudioBuffer<float> buffer (channels, blockSize);
    juce::MidiBuffer midi;

    for (int c = 0; c < channels; ++c)
        for (int i = 0; i < blockSize; ++i)
            buffer.setSample (c, i, 0.1f * std::sin (2.0f * juce::MathConstants<float>::pi * 110.0f
                                                     * (float) (block * blockSize + i) / (float) sampleRate));

    instance.processBlock (buffer, midi);

    for (int c = 0; c < channels; ++c)
        for (int i = 0; i < blockSize; ++i)
            if (! std::isfinite (buffer.getSample (c, i)))
                return false;

    return true;
}

struct Live
{
    juce::String amp;
    std::unique_ptr<juce::AudioPluginInstance> instance;
};

std::unique_ptr<juce::AudioPluginInstance> make (juce::AudioPluginFormat& format, const juce::PluginDescription& type)
{
    juce::String error;
    auto instance = format.createInstanceFromDescription (type, sampleRate, blockSize, error);

    if (instance != nullptr)
    {
        instance->enableAllBuses();
        instance->prepareToPlay (sampleRate, blockSize);
    }
    else
    {
        note ("could not make " + type.name + ": " + error);
    }

    return instance;
}

// An amp to a host, as lines of text — its parameters, then its channels and latency — the way a process of
// its own hands it over.
juce::String layoutText (juce::AudioPluginInstance& instance)
{
    juce::String text;

    for (const auto& p : layoutOf (instance))
        text << p.name << '\t' << p.id << '\t' << p.label << '\t' << juce::String (p.defaultValue, 6) << '\t'
             << p.steps << '\t' << (p.discrete ? 1 : 0) << '\n';

    text << "channels " << instance.getTotalNumInputChannels() << " in, " << instance.getTotalNumOutputChannels()
         << " out, latency " << instance.getLatencySamples() << '\n';
    return text;
}

juce::String valuesText (juce::AudioPluginInstance& instance)
{
    juce::String text;

    for (auto value : valuesOf (instance))
        text << juce::String (value, 6) << '\n';

    return text;
}

std::vector<float> valuesFrom (const juce::String& text)
{
    std::vector<float> values;

    for (const auto& line : juce::StringArray::fromLines (text))
        if (line.isNotEmpty())
            values.push_back (line.getFloatValue());

    return values;
}

// The first line two texts part at, or empty where they are the same.
juce::String firstDifference (const juce::String& a, const juce::String& b)
{
    const auto x = juce::StringArray::fromLines (a.trimEnd());
    const auto y = juce::StringArray::fromLines (b.trimEnd());

    for (int i = 0; i < juce::jmax (x.size(), y.size()); ++i)
        if (x[i] != y[i])
            return "line " + juce::String (i) + ": '" + x[i].replaceCharacter ('\t', '|') + "' against '"
                   + y[i].replaceCharacter ('\t', '|') + "'";

    return {};
}

// Three of its values moved, spread over its continuous parameters — a switch would snap the value a host
// sets it to, and the host would go on reading the value it set — and played once so that the processor has
// them.
void moveValues (juce::AudioPluginInstance& instance, std::initializer_list<float> values)
{
    juce::Array<juce::AudioProcessorParameter*> continuous;

    for (auto* parameter : instance.getParameters())
        if (! parameter->isDiscrete() && parameter->getName (64) != "Bypass")
            continuous.add (parameter);

    const int n = continuous.size();
    int k = 0;

    for (auto value : values)
    {
        if (n < 3)
            break;

        auto* parameter = continuous[k++ * n / 3];
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (value);
        parameter->endChangeGesture();
    }

    playsABlock (instance, 0);
    pump (300);
}

void loadState (juce::AudioPluginInstance& instance, const juce::MemoryBlock& state)
{
    instance.setStateInformation (state.getData(), (int) state.getSize());
    pump (300);
    playsABlock (instance, 0);
    pump (300);
}

// THE OWN BUNDLE'S SIDE, in a process of its own (main: --own): its one amp made, what it is written down,
// and a session saved — three values moved — or the family's session opened, its values written down.
int ownSide (const juce::File& bundle, const juce::File& handover, bool load)
{
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> types;
    format.findAllTypesForFile (types, bundle.getFullPathName());

    if (types.size() != 1)
    {
        std::cout << bundle.getFullPathName() << " holds " << types.size() << " plug-ins, not one" << std::endl;
        return 1;
    }

    auto instance = make (format, *types[0]);

    if (instance == nullptr)
        return 1;

    const auto amp = types[0]->name;
    handover.getChildFile (amp + ".ids.txt").replaceWithText (juce::String (types[0]->uniqueId) + "\t" + juce::String (types[0]->deprecatedUid));
    handover.getChildFile (amp + ".layout.txt").replaceWithText (layoutText (*instance));

    if (load)
    {
        juce::MemoryBlock state;
        handover.getChildFile (amp + ".family-state.bin").loadFileAsData (state);
        loadState (*instance, state);
        handover.getChildFile (amp + ".own-values-after-load.txt").replaceWithText (valuesText (*instance));
    }
    else
    {
        moveValues (*instance, { 0.31f, 0.77f, 0.54f });
        juce::MemoryBlock state;
        instance->getStateInformation (state);
        handover.getChildFile (amp + ".own-state.bin").replaceWithData (state.getData(), state.getSize());
        loadState (*instance, state);   // its values as a session gives them back: a value on a grid, on the grid
        handover.getChildFile (amp + ".own-values.txt").replaceWithText (valuesText (*instance));
    }

    bool finite = true;
    for (int block = 0; block < 16; ++block)
        finite = playsABlock (*instance, block) && finite;

    instance->releaseResources();
    return finite ? 0 : 1;
}

// THIS HOST AGAIN, IN A PROCESS OF ITS OWN: two binaries with an amp in them never share a process here.
// On Linux their engines' registries are one for the whole process (the dynamic linker unifies them), and
// the second binary to load finds its own already registered and stops the process: the family's VST3 and
// its CLAP, or an amp's own bundle and the family's, are opened each in a process of its own, as a host that
// is given one of them opens it. What the child says comes back as its output, and whether all was well.
bool runSelf (juce::StringArray args, juce::String& output)
{
    args.insert (0, juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName());

    juce::ChildProcess child;

    if (! child.start (args))
        return false;

    output = child.readAllProcessOutput();
    return child.waitForProcessToFinish (180000) && child.getExitCode() == 0;
}

bool runOwn (const juce::File& bundle, const juce::File& handover, const char* what)
{
    juce::String output;
    const auto ok = runSelf ({ "--own", bundle.getFullPathName(), handover.getFullPathName(), what }, output);

    if (! ok)
        note ("the own bundle's process did not end well: " + output.trim());

    return ok;
}

// Where the processes hand each other what they found: in the temporary folder, kept.
juce::File handoverFolder()
{
    auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("orbitamp-bundle-host");
    folder.createDirectory();
    return folder;
}

#if JUCE_MAC
//==============================================================================
// THE AU, as a Mac host finds it. A host finds an Audio Unit among the components the system registered from
// the plug-in folders, by its bundle's Info.plist; this test installs nothing there. It registers the AU in
// this process alone (AudioComponentRegister), each AudioComponents entry with the factory function it names,
// and goes no further where the system has another component first under an amp's id — an amp's own AU
// installed on this Mac — since JUCE's host would open that one.

// Four characters as the OSType a component description holds, and back.
OSType osTypeOf (const juce::String& code)
{
    return code.length() == 4 ? (OSType) juce::ByteOrder::bigEndianInt (code.toRawUTF8()) : 0;
}

// The version as an AU's Info.plist writes it: 0x00MMmmpp.
int auVersionOf (const juce::String& version)
{
    const auto parts = juce::StringArray::fromTokens (version, ".", "");
    return (parts[0].getIntValue() << 16) | (parts[1].getIntValue() << 8) | parts[2].getIntValue();
}

// One value of an Info.plist dictionary, as text: a string as it is, a number in decimal, a boolean as
// true / false, a dictionary as its keys and values in order — what the 0.8.0 entries are held to.
juce::String plistText (CFTypeRef value)
{
    if (value == nullptr)
        return "(none)";

    if (CFGetTypeID (value) == CFStringGetTypeID())
        return juce::String::fromCFString ((CFStringRef) value);

    if (CFGetTypeID (value) == CFBooleanGetTypeID())
        return CFBooleanGetValue ((CFBooleanRef) value) ? "true" : "false";

    if (CFGetTypeID (value) == CFNumberGetTypeID())
    {
        long long number = 0;
        CFNumberGetValue ((CFNumberRef) value, kCFNumberLongLongType, &number);
        return juce::String (number);
    }

    if (CFGetTypeID (value) == CFDictionaryGetTypeID())
    {
        const auto dictionary = (CFDictionaryRef) value;
        const auto count = CFDictionaryGetCount (dictionary);
        std::vector<const void*> keys ((size_t) count), values ((size_t) count);
        CFDictionaryGetKeysAndValues (dictionary, keys.data(), values.data());

        juce::StringArray entries;
        for (size_t i = 0; i < keys.size(); ++i)
            entries.add (plistText ((CFTypeRef) keys[i]) + "=" + plistText ((CFTypeRef) values[i]));

        entries.sort (false);
        return "{" + entries.joinIntoString (", ") + "}";
    }

    return "(something else)";
}

juce::String plistValue (CFDictionaryRef dictionary, const char* key)
{
    const auto cfKey = CFStringCreateWithCString (nullptr, key, kCFStringEncodingUTF8);
    const auto text = plistText (CFDictionaryGetValue (dictionary, cfKey));
    CFRelease (cfKey);
    return text;
}

void checkAu (const juce::File& bundle, const juce::StringArray& amps, const std::map<juce::String, juce::StringArray>& vst3Names)
{
    std::cout << "AU of " << bundle.getFullPathName() << std::endl;

    // The bundle and its binary, loaded for the rest of the process: what is registered below makes its
    // instances with the binary's factory function.
    const auto path = bundle.getFullPathName();
    const auto url = CFURLCreateFromFileSystemRepresentation (nullptr, (const UInt8*) path.toRawUTF8(),
                                                              (CFIndex) path.getNumBytesAsUTF8(), true);
    const auto cfBundle = url != nullptr ? CFBundleCreate (nullptr, url) : nullptr;

    if (url != nullptr)
        CFRelease (url);

    if (! check (cfBundle != nullptr && CFBundleLoadExecutable (cfBundle), "the bundle and its binary load"))
        return;

    const auto info = CFBundleGetInfoDictionary (cfBundle);
    const auto components = info != nullptr ? (CFArrayRef) CFDictionaryGetValue (info, CFSTR ("AudioComponents")) : nullptr;
    const auto count = components != nullptr && CFGetTypeID (components) == CFArrayGetTypeID() ? (int) CFArrayGetCount (components) : 0;

    if (! check (count == amps.size(), "Info.plist: " + juce::String (count) + " AudioComponents, one per amp of family.ini ("
                                           + juce::String (amps.size()) + ")"))
        return;

    // EVERY ENTRY THE AMP'S OWN 0.8.0 AU, but for the factory function and the version.
    const juce::String keys0_8_0 ("description factoryFunction manufacturer name resourceUsage subtype type version");
    const juce::String resourceUsage0_8_0 ("{network.client=true, temporary-exception.files.all.read-write=true}");
    const auto version = auVersionOf (ORBITAMP_VERSION);
    juce::StringArray factories;

    for (int i = 0; i < count; ++i)
    {
        const auto& amp = amps[i];
        const auto* own = shippedOf (amp);
        const auto entry = (CFDictionaryRef) CFArrayGetValueAtIndex (components, i);

        if (! check (own != nullptr && CFGetTypeID (entry) == CFDictionaryGetTypeID(), amp + ": an entry, and the id its own AU shipped with known here"))
            return;

        std::vector<const void*> keys ((size_t) CFDictionaryGetCount (entry));
        CFDictionaryGetKeysAndValues (entry, keys.data(), nullptr);
        juce::StringArray names;
        for (auto* key : keys)
            names.add (plistText ((CFTypeRef) key));
        names.sort (false);

        const auto said = plistValue (entry, "type") + " " + plistValue (entry, "subtype") + " " + plistValue (entry, "manufacturer")
                          + ", \"" + plistValue (entry, "name") + "\", described \"" + plistValue (entry, "description")
                          + "\", version " + plistValue (entry, "version") + ", resource usage " + plistValue (entry, "resourceUsage");

        check (names.joinIntoString (" ") == keys0_8_0
                   && plistValue (entry, "type") == "aufx" && plistValue (entry, "subtype") == own->auSubtype
                   && plistValue (entry, "manufacturer") == "Dcat" && plistValue (entry, "name") == "Darwin's Cat: " + amp
                   && plistValue (entry, "description") == amp && plistValue (entry, "version") == juce::String (version)
                   && plistValue (entry, "resourceUsage") == resourceUsage0_8_0,
               "entry " + juce::String (i + 1) + ", " + said + " — " + amp + "'s own 0.8.0 AU, but for the factory function"
                   + (names.joinIntoString (" ") == keys0_8_0 ? juce::String() : " (keys: " + names.joinIntoString (" ") + ")"));

        factories.addIfNotAlreadyThere (plistValue (entry, "factoryFunction"));
    }

    const auto factoryName = factories[0].toCFString();
    const auto factory = (AudioComponentFactoryFunction) CFBundleGetFunctionPointerForName (cfBundle, factoryName);
    CFRelease (factoryName);

    if (! check (factories.size() == 1 && factory != nullptr,
                 "every entry names one factory function, " + factories.joinIntoString (", ") + ", and the binary exports it"))
        return;

    // REGISTERED IN THIS PROCESS, and the first a host finds under each id.
    for (int i = 0; i < count; ++i)
    {
        const auto& amp = amps[i];
        const auto entry = (CFDictionaryRef) CFArrayGetValueAtIndex (components, i);
        AudioComponentDescription description { osTypeOf (plistValue (entry, "type")), osTypeOf (plistValue (entry, "subtype")),
                                                osTypeOf (plistValue (entry, "manufacturer")), 0, 0 };
        const auto name = plistValue (entry, "name").toCFString();
        const auto component = AudioComponentRegister (&description, name, (UInt32) version, factory);
        CFRelease (name);

        if (! check (component != nullptr && AudioComponentFindNext (nullptr, &description) == component,
                     amp + ": registered in this process, and the first component under its id a host finds"))
        {
            note ("this Mac has another component under " + plistValue (entry, "type") + " " + plistValue (entry, "subtype")
                  + " " + plistValue (entry, "manufacturer") + " ahead of it — not opened");
            return;
        }
    }

    // A component of the binary under a code its list does not hold makes nothing: the wrapper finds no
    // entry for it and the host is told so, rather than given an amp under another one's id.
    {
        AudioComponentDescription stranger { kAudioUnitType_Effect, osTypeOf ("Dcam"), osTypeOf ("Dcat"), 0, 0 };
        const auto component = AudioComponentRegister (&stranger, CFSTR ("Darwin's Cat: none"), (UInt32) version, factory);
        AudioComponentInstance instance = nullptr;
        const auto status = component != nullptr ? AudioComponentInstanceNew (component, &instance) : noErr;

        check (component != nullptr && status != noErr,
               "a component of the binary under a code the list does not hold, aufx Dcam Dcat, makes nothing (" + juce::String ((int) status) + ")");

        if (status == noErr && instance != nullptr)
            AudioComponentInstanceDispose (instance);
    }

    // THROUGH JUCE'S AU HOST: every amp found by its id, made, all of them alive at once.
    juce::AudioUnitPluginFormat format;
    std::vector<Live> family;

    for (const auto& amp : amps)
    {
        const auto identifier = "AudioUnit:Effects/aufx," + juce::String (shippedOf (amp)->auSubtype) + ",Dcat";
        juce::OwnedArray<juce::PluginDescription> types;
        format.findAllTypesForFile (types, identifier);

        if (! check (types.size() == 1 && types[0]->name == amp && types[0]->manufacturerName == "Darwin's Cat"
                         && types[0]->version == ORBITAMP_VERSION && ! types[0]->isInstrument,
                     identifier + ": " + (types.isEmpty() ? juce::String ("nothing") : types[0]->manufacturerName + ": " + types[0]->name
                                                                                     + ", " + types[0]->version) + ", an effect"))
            return;

        auto instance = make (format, *types[0]);

        if (! check (instance != nullptr && instance->getName() == amp,
                     "made " + amp + ", " + juce::String ((int) family.size() + 1) + " of the family's alive"))
            return;

        family.push_back ({ amp, std::move (instance) });
    }

    // Each its own amp: its VST3's parameters, no two amps' alike. In the AU's own order, which is not the
    // VST3's: JUCE's AU wrapper lists them by their AU ids (GetParameterList), an amp's own AU as the family's.
    std::vector<std::vector<Param>> layouts;
    for (auto& one : family)
    {
        layouts.push_back (layoutOf (*one.instance));
        auto names = namesOf (layouts.back());
        names.sort (false);

        // JUCE's VST3 wrapper adds a Bypass where the processor has none; the AU wrapper does not.
        auto vst3 = vst3Names.count (one.amp) != 0 ? vst3Names.at (one.amp) : juce::StringArray();
        if (vst3.size() == names.size() + 1 && vst3[vst3.size() - 1] == "Bypass")
            vst3.remove (vst3.size() - 1);
        vst3.sort (false);

        int parted = 0;
        while (parted < names.size() && parted < vst3.size() && names[parted] == vst3[parted])
            ++parted;

        check (! names.isEmpty() && names == vst3,
               one.amp + ": its " + juce::String (names.size()) + " AU parameters are its VST3's"
                   + (names == vst3 ? juce::String()
                                    : " (" + juce::String (vst3.size()) + " there; parameter " + juce::String (parted) + " '"
                                          + names[parted] + "' against '" + vst3[parted] + "')") + ", "
                   + juce::String (one.instance->getTotalNumInputChannels()) + " in / "
                   + juce::String (one.instance->getTotalNumOutputChannels()) + " out, latency "
                   + juce::String (one.instance->getLatencySamples()));
    }

    for (size_t a = 0; a < family.size(); ++a)
        for (size_t b = a + 1; b < family.size(); ++b)
            check (differenceOf (layouts[a], layouts[b]).isNotEmpty(),
                   family[a].amp + " and " + family[b].amp + " have parameters of their own (" + differenceOf (layouts[a], layouts[b]) + ")");

    bool finite = true;

    for (int block = 0; block < 16; ++block)
        for (auto& one : family)
            finite = playsABlock (*one.instance, block) && finite;

    check (finite, juce::String ((int) family.size()) + " amps alive at once, each played 16 blocks in turn, every sample a number");

    for (auto& one : family)
        one.instance->releaseResources();

    family.clear();
    pump (300);
}
#endif

//==============================================================================
// THROUGH A HOST: JUCE's VST3 host finds the amps, makes them all at once, plays them in turn, and holds each
// to its own bundle where there is one. Returns each amp's parameters, by name, for the CLAP to be held to.
std::map<juce::String, juce::StringArray> checkVst3Instances (const juce::File& bundle, const juce::StringArray& amps,
                                                              const juce::File& build)
{
    std::cout << "VST3 plug-ins in " << bundle.getFullPathName() << std::endl;
    std::map<juce::String, juce::StringArray> names;

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> types;
    format.findAllTypesForFile (types, bundle.getFullPathName());

    juce::StringArray found;
    for (auto* type : types)
        found.add (type->name);

    if (! check (found == amps, "JUCE's host finds " + found.joinIntoString (", ") + "  (family.ini: " + amps.joinIntoString (", ") + ")"))
        return names;

    std::vector<Live> family;

    for (auto* type : types)
    {
        check (type->manufacturerName == "Darwin's Cat" && type->version == ORBITAMP_VERSION && ! type->isInstrument,
               type->name + ": " + type->manufacturerName + ", " + type->version + ", an effect");

        auto instance = make (format, *type);

        if (! check (instance != nullptr && instance->getName() == type->name,
                     "made " + type->name + ", " + juce::String ((int) family.size() + 1) + " of the family's alive"))
            return names;

        family.push_back ({ type->name, std::move (instance) });
    }

    // Each its own amp: a layout of its own, no two alike.
    std::vector<std::vector<Param>> layouts;
    for (auto& one : family)
    {
        layouts.push_back (layoutOf (*one.instance));
        names[one.amp] = namesOf (layouts.back());
        note (one.amp + ": " + juce::String ((int) layouts.back().size()) + " parameters, "
              + juce::String (one.instance->getTotalNumInputChannels()) + " in / "
              + juce::String (one.instance->getTotalNumOutputChannels()) + " out, latency "
              + juce::String (one.instance->getLatencySamples()));
        check (! layouts.back().empty(), one.amp + " has parameters");
    }

    for (size_t a = 0; a < family.size(); ++a)
        for (size_t b = a + 1; b < family.size(); ++b)
            check (differenceOf (layouts[a], layouts[b]).isNotEmpty(),
                   family[a].amp + " and " + family[b].amp + " have parameters of their own (" + differenceOf (layouts[a], layouts[b]) + ")");

    // Every one alive at once, playing in turn.
    bool finite = true;

    for (int block = 0; block < 16; ++block)
        for (auto& one : family)
            finite = playsABlock (*one.instance, block) && finite;

    check (finite, juce::String ((int) family.size()) + " amps alive at once, each played 16 blocks in turn, every sample a number");

    // HELD TO ITS OWN BUNDLE — the build of the amp alone, every amp's plug-in before the family's — opened in a
    // process of its own, as the session it saved was: two binaries with the same amp in them do not share a
    // process here (on Linux their engines' registries collide at load), and need not.
    if (build != juce::File())
    {
        const auto handover = handoverFolder();

        for (size_t i = 0; i < family.size(); ++i)
        {
            const auto& amp = family[i].amp;
            auto& theirs = *family[i].instance;
            const auto ownBundle = build.getChildFile (amp + "_artefacts/Release/VST3/" + amp + ".vst3");

            if (! ownBundle.isDirectory())
            {
                note ("no own bundle of " + amp + " at " + ownBundle.getFullPathName() + " — not held to it");
                continue;
            }

            if (! check (runOwn (ownBundle, handover, "save"), amp + "'s own bundle, in a process of its own, made it, moved three values and saved"))
                continue;

            const auto said = [&] (const juce::String& what) { return handover.getChildFile (amp + what).loadFileAsString(); };

            check (said (".ids.txt") == juce::String (types[(int) i]->uniqueId) + "\t" + juce::String (types[(int) i]->deprecatedUid),
                   amp + "'s own bundle holds it under the ids the family's holds it (" + said (".ids.txt").replaceCharacter ('\t', ' ') + ")");

            const auto difference = firstDifference (said (".layout.txt"), layoutText (theirs));
            check (difference.isEmpty(), amp + ": its parameters, channels and latency are its own bundle's — names, ids, labels, defaults, steps"
                                             + (difference.isEmpty() ? juce::String() : " — " + difference));

            juce::MemoryBlock ownState;
            handover.getChildFile (amp + ".own-state.bin").loadFileAsData (ownState);
            loadState (theirs, ownState);
            const auto opened = valuesDifference (valuesFrom (said (".own-values.txt")), valuesOf (theirs), theirs);
            check (ownState.getSize() > 0 && opened.isEmpty(),
                   amp + ": a session its own bundle saved opens in the family's, every value as it was ("
                       + juce::String (theirs.getParameters().size()) + " compared)" + (opened.isEmpty() ? juce::String() : " — " + opened));

            moveValues (theirs, { 0.62f, 0.18f, 0.93f });
            juce::MemoryBlock familyState;
            theirs.getStateInformation (familyState);
            handover.getChildFile (amp + ".family-state.bin").replaceWithData (familyState.getData(), familyState.getSize());
            loadState (theirs, familyState);   // as the own side does: the values a session gives back

            if (check (runOwn (ownBundle, handover, "load"), amp + "'s own bundle, in a process of its own, opened the family's session"))
            {
                const auto back = valuesDifference (valuesOf (theirs), valuesFrom (said (".own-values-after-load.txt")), theirs);
                check (back.isEmpty(), amp + ": a session the family's saved opens in its own bundle, every value as it was"
                                           + (back.isEmpty() ? juce::String() : " — " + back));
            }
        }
    }

    for (auto& one : family)
        one.instance->releaseResources();

    return names;
}

//==============================================================================
// THE CLAP FACTORY, raw, as a CLAP host reads it.
uint32_t noEvents (const clap_input_events*)                                 { return 0; }
const clap_event_header_t* noEvent (const clap_input_events*, uint32_t)     { return nullptr; }
bool dropEvent (const clap_output_events*, const clap_event_header_t*)      { return true; }

void checkClap (const juce::File& file, const juce::StringArray& amps, const std::map<juce::String, juce::StringArray>& vst3Names)
{
    std::cout << "CLAP factory of " << file.getFullPathName() << std::endl;

   #if JUCE_MAC
    const auto binary = file.getChildFile ("Contents/MacOS").getChildFile (file.getFileNameWithoutExtension());
   #else
    const auto binary = file;
   #endif

    juce::DynamicLibrary library;

    if (! check (library.open (binary.getFullPathName()), "the binary opens"))
        return;

    const auto* entry = (const clap_plugin_entry*) library.getFunction ("clap_entry");

    if (! check (entry != nullptr && entry->init (file.getFullPathName().toRawUTF8()), "clap_entry is exported and inits"))
        return;

    const auto* factory = (const clap_plugin_factory*) entry->get_factory (CLAP_PLUGIN_FACTORY_ID);
    const auto count = factory->get_plugin_count (factory);

    if (check (count == (uint32_t) amps.size(), "it counts " + juce::String (count) + " plug-ins"))
    {
        for (uint32_t i = 0; i < count; ++i)
        {
            const auto* descriptor = factory->get_plugin_descriptor (factory, i);
            const auto& amp = amps[(int) i];
            juce::StringArray features;

            for (auto* feature = descriptor->features; feature != nullptr && *feature != nullptr; ++feature)
                features.add (*feature);

            check (juce::String (descriptor->id) == "com.darwinscat." + amp.toLowerCase() && juce::String (descriptor->name) == amp
                       && juce::String (descriptor->vendor) == "Darwin's Cat" && juce::String (descriptor->version) == ORBITAMP_VERSION
                       && features == juce::StringArray { "audio-effect", "guitar", "amplifier" },
                   "plug-in " + juce::String (i) + ": " + descriptor->id + "  " + descriptor->name + "  "
                       + descriptor->vendor + " " + descriptor->version + "  [" + features.joinIntoString (", ") + "]");
        }

        check (factory->get_plugin_descriptor (factory, count) == nullptr, "no descriptor past the last");

        clap_host host {};
        host.clap_version = CLAP_VERSION;
        host.name = "OrbitAmpBundleHost";
        host.vendor = "Darwin's Cat";
        host.url = "";
        host.version = "1";
        host.get_extension = [] (const clap_host*, const char*) -> const void* { return nullptr; };
        host.request_restart = [] (const clap_host*) {};
        host.request_process = [] (const clap_host*) {};
        host.request_callback = [] (const clap_host*) {};

        struct Made
        {
            const clap_plugin* plugin = nullptr;
            std::vector<uint32_t> inputs, outputs;   // each port's channels
        };

        // Every amp of the binary alive at once, each with its VST3's parameters.
        std::vector<Made> made;

        for (const auto& amp : amps)
        {
            const auto id = "com.darwinscat." + amp.toLowerCase();
            Made one;
            one.plugin = factory->create_plugin (factory, &host, id.toRawUTF8());

            if (! check (one.plugin != nullptr && one.plugin->init (one.plugin) && juce::String (one.plugin->desc->id) == id,
                         "made " + id + ", " + juce::String ((int) made.size() + 1) + " alive"))
            {
                made.push_back ({});
                continue;
            }

            juce::StringArray names;
            if (const auto* params = (const clap_plugin_params*) one.plugin->get_extension (one.plugin, CLAP_EXT_PARAMS))
            {
                for (uint32_t p = 0; p < params->count (one.plugin); ++p)
                {
                    clap_param_info info {};
                    params->get_info (one.plugin, p, &info);
                    names.add (info.name);
                }
            }

            // JUCE's VST3 wrapper adds a Bypass where the processor has none; the CLAP wrapper does not.
            auto vst3 = vst3Names.count (amp) != 0 ? vst3Names.at (amp) : juce::StringArray();
            if (vst3.size() == names.size() + 1 && vst3[vst3.size() - 1] == "Bypass")
                vst3.remove (vst3.size() - 1);

            check (! names.isEmpty() && names == vst3,
                   amp + ": its " + juce::String (names.size()) + " CLAP parameters are its VST3's"
                       + (names == vst3 ? juce::String() : " (" + juce::String (vst3.size()) + " there)"));

            if (const auto* ports = (const clap_plugin_audio_ports*) one.plugin->get_extension (one.plugin, CLAP_EXT_AUDIO_PORTS))
            {
                for (const bool input : { true, false })
                {
                    for (uint32_t p = 0; p < ports->count (one.plugin, input); ++p)
                    {
                        clap_audio_port_info info {};
                        ports->get (one.plugin, p, input, &info);
                        (input ? one.inputs : one.outputs).push_back (info.channel_count);
                    }
                }
            }

            check (one.plugin->activate (one.plugin, sampleRate, 1, blockSize) && one.plugin->start_processing (one.plugin),
                   amp + " activates and starts, " + juce::String ((int) made.size() + 1) + " alive");
            made.push_back (std::move (one));
        }

        clap_input_events inEvents { nullptr, noEvents, noEvent };
        clap_output_events outEvents { nullptr, dropEvent };

        bool played = true;

        for (int block = 0; block < 16; ++block)
        {
            for (auto& one : made)
            {
                if (one.plugin == nullptr)
                    continue;

                std::vector<juce::AudioBuffer<float>> inBuffers, outBuffers;
                std::vector<clap_audio_buffer> ins, outs;
                inBuffers.reserve (one.inputs.size());
                outBuffers.reserve (one.outputs.size());

                for (auto channels : one.inputs)
                {
                    auto& buffer = inBuffers.emplace_back ((int) channels, blockSize);
                    for (uint32_t c = 0; c < channels; ++c)
                        for (int i = 0; i < blockSize; ++i)
                            buffer.setSample ((int) c, i, 0.1f * std::sin (2.0f * juce::MathConstants<float>::pi * 110.0f
                                                                           * (float) (block * blockSize + i) / (float) sampleRate));
                }

                for (auto channels : one.outputs)
                    outBuffers.emplace_back ((int) channels, blockSize).clear();

                for (auto& buffer : inBuffers)
                {
                    clap_audio_buffer b {};
                    b.data32 = const_cast<float**> (buffer.getArrayOfWritePointers());
                    b.channel_count = (uint32_t) buffer.getNumChannels();
                    ins.push_back (b);
                }

                for (auto& buffer : outBuffers)
                {
                    clap_audio_buffer b {};
                    b.data32 = const_cast<float**> (buffer.getArrayOfWritePointers());
                    b.channel_count = (uint32_t) buffer.getNumChannels();
                    outs.push_back (b);
                }

                clap_process process {};
                process.steady_time = block * blockSize;
                process.frames_count = blockSize;
                process.audio_inputs = ins.data();
                process.audio_outputs = outs.data();
                process.audio_inputs_count = (uint32_t) ins.size();
                process.audio_outputs_count = (uint32_t) outs.size();
                process.in_events = &inEvents;
                process.out_events = &outEvents;

                played = one.plugin->process (one.plugin, &process) != CLAP_PROCESS_ERROR && played;

                for (auto& buffer : outBuffers)
                    for (int c = 0; c < buffer.getNumChannels(); ++c)
                        for (int i = 0; i < blockSize; ++i)
                            played = played && std::isfinite (buffer.getSample (c, i));
            }
        }

        check (played, juce::String ((int) made.size()) + " amps alive at once, each played 16 blocks in turn, every sample a number");

        for (auto& one : made)
        {
            if (one.plugin != nullptr)
            {
                one.plugin->stop_processing (one.plugin);
                one.plugin->deactivate (one.plugin);
                one.plugin->destroy (one.plugin);
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
    if (argc == 4 && juce::String (argv[1]) == "--clap")
    {
        const juce::ScopedJuceInitialiser_GUI juceLibrary;
        const auto handover = juce::File (juce::CharPointer_UTF8 (argv[3]));
        const auto amps = juce::StringArray::fromTokens (ORBITAMP_FAMILY_AMPS, " ", "");
        std::map<juce::String, juce::StringArray> names;

        for (const auto& amp : amps)
            names[amp] = juce::StringArray::fromLines (handover.getChildFile (amp + ".vst3-names.txt").loadFileAsString());

        checkClap (juce::File (juce::CharPointer_UTF8 (argv[2])), amps, names);
        return allGood ? 0 : 1;
    }

   #if JUCE_MAC
    if (argc == 4 && juce::String (argv[1]) == "--au")
    {
        const juce::ScopedJuceInitialiser_GUI juceLibrary;
        const auto handover = juce::File (juce::CharPointer_UTF8 (argv[3]));
        const auto amps = juce::StringArray::fromTokens (ORBITAMP_FAMILY_AMPS, " ", "");
        std::map<juce::String, juce::StringArray> names;

        for (const auto& amp : amps)
            names[amp] = juce::StringArray::fromLines (handover.getChildFile (amp + ".vst3-names.txt").loadFileAsString());

        checkAu (juce::File (juce::CharPointer_UTF8 (argv[2])), amps, names);
        return allGood ? 0 : 1;
    }
   #endif

    if (argc == 5 && juce::String (argv[1]) == "--own")
    {
        const juce::ScopedJuceInitialiser_GUI juceLibrary;
        return ownSide (juce::File (juce::CharPointer_UTF8 (argv[2])), juce::File (juce::CharPointer_UTF8 (argv[3])),
                        juce::String (argv[4]) == "load");
    }

    if (argc != 3 && argc != 4)
    {
        std::cerr << "usage: orbitamp_bundle_host <family.vst3> <family.clap> [<build-dir>]" << std::endl;
        return 2;
    }

    const juce::ScopedJuceInitialiser_GUI juceLibrary;

    const auto cwd = juce::File::getCurrentWorkingDirectory();
    const auto vst3 = cwd.getChildFile (argv[1]);
    const auto clap = cwd.getChildFile (argv[2]);
    const auto build = argc == 4 ? cwd.getChildFile (argv[3]) : juce::File();
    const auto amps = juce::StringArray::fromTokens (ORBITAMP_FAMILY_AMPS, " ", "");

    std::cout << "The family's plug-ins, holding " << amps.joinIntoString (", ") << " (installer/family.ini)" << std::endl;

    checkVst3Factory (vst3, amps);
    const auto names = checkVst3Instances (vst3, amps, build);

    // The CLAP in a process of its own (runSelf), handed each amp's VST3 parameters.
    const auto handover = handoverFolder();
    for (const auto& [amp, parameters] : names)
        handover.getChildFile (amp + ".vst3-names.txt").replaceWithText (parameters.joinIntoString ("\n"));

    juce::String output;
    const auto clapOk = runSelf ({ "--clap", clap.getFullPathName(), handover.getFullPathName() }, output);
    std::cout << output;
    check (clapOk, "the CLAP, in a process of its own, as it must be");

   #if JUCE_MAC
    // The AU, beside the VST3 where the build puts it, in a process of its own too, handed the same.
    const auto au = vst3.getParentDirectory().getSiblingFile ("AU").getChildFile (vst3.getFileNameWithoutExtension() + ".component");
    output.clear();
    const auto auOk = au.isDirectory() && runSelf ({ "--au", au.getFullPathName(), handover.getFullPathName() }, output);
    std::cout << output;
    check (auOk, "the AU beside the VST3, " + au.getFullPathName() + ", in a process of its own, as it must be");
   #endif

    std::cout << (allGood ? "ALL PASS" : "FAILED") << std::endl;
    return allGood ? 0 : 1;
}
