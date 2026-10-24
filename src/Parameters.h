// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <felitronics/delay/DelayLaw.h>
#include <felitronics/reverb/AmpReverb.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <CabinetCatalog.h>   // written by CMake from resources/cabinets.txt

namespace orbitamp::params
{

// Parameter IDs. Stable strings — a host stores these in its session, so renaming one breaks every
// saved project that used it.
inline constexpr const char* preampOn    = "preamp_on";
inline constexpr const char* reverbOn    = "reverb_on";

/** The input trim — the interface knob the interface may not have: ±24 dB ahead of EVERYTHING,
    the tuner's ear and the gate's key included, so the IN meter closes the gain-staging loop:
    drag the trim, watch the same column answer. */
inline constexpr const char* inTrim  = "in_trim";
inline constexpr const char* outTrim = "out_trim";
inline constexpr float inTrimRangeDb = 24.0f;   // one range for both trims

// The safety at the door: on by default — it exists to protect, and protection you must
// remember to arm is not protection.
inline constexpr const char* limiterOn      = "limiter_on";
inline constexpr const char* limiterCeiling = "limiter_ceiling";
inline constexpr float limiterCeilingMin = -3.0f;
inline constexpr float limiterCeilingMax = -0.1f;

/** THE BOOST'S LEVEL: how much louder the amp leaves while its boost is on. A boost a player stamps on
    for a solo is meant to stand out of the band, and a capture of an amp's boost is levelled like any
    other capture — so the lift is given back here, at the end of the chain: after the cabinet, the
    reverb and the delay, ahead of the master's hand and of the limiter, which keeps the last word.

    The boost is OrbitAmp's BOOST key and TURBO on the cockpit and the rovers, whatever it does to the
    capture: the playing pack's own switch of that name where it has one — the rovers' TURBO holds it
    on — and preampBoost, the capture played higher up its dial, where it has none. With the AMP out of
    the chain there is no boost, and the level is not heard.

    ONE FOR THE WHOLE AMP, like the master's hand and the limiter beside it, not one per channel: the
    rovers' TURBO is one key for every gear, and a boost
    that is louder on one channel than on the next is a level nobody can find again. It is the sound's,
    not the machine's: a session and a preset keep it, and undo steps over it. Picked on the boost's own
    right-click menu, out of boostLevels; +3 out of the box. Whole decibels, so the five places on the
    menu are exactly the numbers the engine plays.

    Its id is the output's, `out_boost`, beside `out_trim`: `boost_` is the prefix of the boost BLOCK that
    once stood ahead of the amp, and no parameter of the engine's carries it any more. */
inline constexpr const char* boostLevel = "out_boost";
inline constexpr float boostLevelMin     = -3.0f;
inline constexpr float boostLevelMax     =  9.0f;
inline constexpr float boostLevelDefault =  3.0f;
inline constexpr int   boostLevels[] { -3, 0, 3, 6, 9 };

/** The noise gate — the second service link, right after the tuner: it keys off the raw guitar,
    and kills the hum before any dirt can multiply it. The threshold is the OPEN level in dBFS
    against that raw input; feel (attack, hold, hysteresis, floor) is fixed in the engine. */
inline constexpr const char* gateOn        = "gate_on";
inline constexpr const char* gateThreshold = "gate_threshold";

/** Where the threshold may stand, dBFS — the parameter's span (Parameters.cpp says why the ceiling is
    -10), and with it every other way in: the faces' menu, and where LEARN may land. */
inline constexpr float gateThresholdMin = -80.0f;
inline constexpr float gateThresholdMax = -10.0f;

/** Mirrors the engine's Schmitt hysteresis (felitronics NoiseGate::Config default): the CLOSE
    level sits this far under the OPEN level. The meter draws both marks, so the number the
    display promises is the number the state machine runs. */
inline constexpr float gateHysteresisDb = 6.0f;

/** Where the gate MUTES. It always KEYS off the raw input — that is the whole dual-detection
    idea — but the VCA can stand at the chain's start or after the preamp, ahead of the reverb.
    Pre-Reverb is the G-String architecture and the default: it kills the hiss the preamp ADDS
    (which a gate at the start never meets), the clean key never pumps on distortion
    sustain, and the reverb tail rings out past it. Start remains for the player who wants the
    nonlinearity itself to fall silent between notes. */
inline constexpr const char* gatePos = "gate_pos";
/** WHERE the gate mutes: at the head of the chain, or at its tail. Named for the CHAIN, not for
    one block in it — "Pre-Reverb" was a strange thing to read on a rig with no reverb in it, and
    the reverb is one of the links a player can take out. START and END are true whatever is
    standing. The position itself has not moved. */
inline const juce::StringArray gatePositions { "Start", "End" };

/** The gate's one FEEL control: how the VCA closes once the hold has run out. Two ways, not a
    dial — the choice is between two intents, not along an axis: NORMAL (the OrbitCab-shipped
    100 ms — a note dies naturally) and METAL (35 ms — the chop). The rest of the voicing
    (attack, hold, hysteresis) stays fixed; an attack knob on a gate is a knob for eating pick
    transients. */
inline constexpr const char* gateDecay = "gate_decay";
inline const juce::StringArray gateDecayModes { "Normal", "Metal" };
inline constexpr float gateDecayModeMs[] = { 100.0f, 35.0f };

/** WHOSE tone controls a captured block wears — and, because it is the same question, whether the
    pack's measured curves are in the signal at all.

    NATIVE plays the device's own: the curves it was measured with, rebuilt as filters, driven by
    knobs named after its own controls. OURS bypasses them and puts our parametric there instead.
    They are alternatives, not layers — the one thing that must never happen is a device wearing its
    own tone stack and ours at the same time and no way to tell which decibel came from where.

    A pack that measured nothing has no NATIVE to offer, and the block falls to OURS on its own
    rather than showing an empty row.

    This replaces a temporary `raw` switch that meant "play the capture with nothing of ours on it".
    That was the same fact asked as a debugging question; asked as a choice it is a control. */
inline juce::String blockEqMode (const char* blk) { return juce::String (blk) + "_eq_mode"; }
inline const juce::StringArray eqModes { "Device Tone", "Universal EQ" };
enum class EqMode { native, ours };

/** A device's OTHER selecting controls — the ones that pick a file and are not the gain dial. Two
    slots, like the measured ones: a fixed set a host can see, filled by whatever the pack declares.
    Fur Coat needs one of them for its octave; most pedals need none. */
inline constexpr int numSelectors = 2;
inline juce::String selectorId (const char* blk, int i)
{
    return juce::String (blk) + "_sel" + juce::String (i + 1);
}

/** THE BOOST OF A PACK THAT HAS NO BOOST SWITCH — the X88-IR's four, and any pack like them: OrbitAmp's
    BOOST key and the cockpit's TURBO on such a pack. On, the capture plays two captured positions up its
    gain dial from where DRIVE stands, the knob staying where it is, and a step the dial has not got above
    its top capture feeds the top capture 3 dB harder instead (core::CapturedBlock::setGain says the
    rule). A pack WITH a Boost switch is boosted by its switch, as it always was, and this is not read.
    The sound's: a session, a preset and the channel's memory keep it, and undo steps over it. */
inline constexpr const char* preampBoost = "preamp_boost";

inline constexpr const char* preampId = "preamp";

/** What a player reads where the preamp is named — its arrow, its face, its lanes in a host. To a
    player the block is the AMP; its ids keep the `preamp` they were born with, because a host
    stores those and a new spelling buys nothing. */
inline constexpr const char* preampName = "Amp";

/** A captured block's own parameters, derived from its id prefix — the same shape for the preamp
    and whatever captured block comes next. The named constants around them predate these and stay
    for call sites that mean a specific block. */
inline juce::String blockOn       (const char* blk) { return juce::String (blk) + "_on"; }
inline juce::String blockDevice   (const char* blk) { return juce::String (blk) + "_device"; }
inline juce::String blockGain     (const char* blk) { return juce::String (blk) + "_gain"; }
/** How the gain dial moves between the captured positions. SMOOTH: the two neighbouring captures
    play together, mixed by angle — a continuous dial, at the price of two models running. STEP:
    the dial lands only on the captured positions and one capture plays at a time. */
inline juce::String blockSmooth   (const char* blk) { return juce::String (blk) + "_smooth"; }
/** Where a captured device likes to be fed, in dBFS peak — the green zone on the IN meter.
    Still a CONVENTION: it is the window a guitar DI normally peaks in, not a level anyone measured.

    What HAS changed is what the zone is read against. `FileEntry::inputDb` is the alias attenuation
    between two captures of one device and never said how hard the hardware was driven — but a pack
    now states its own working point in the manifest (`chain[].input_db`, namz schema 4), and the
    player applies it before the network. So the level entering the block and the level the network
    eats are two different numbers, and the IN meter shows BOTH: this zone belongs to the second of
    them, the one that is actually the network's input. */
inline constexpr float captureHotDb  = -6.0f;
inline constexpr float captureColdDb = -18.0f;

inline juce::String blockMeasured (const char* blk, int i) { return juce::String (blk) + "_meas" + juce::String (i + 1); }

/** Which device is loaded, as an index into the scanned list. An index rather than a choice list:
    a host fixes a Choice's names at construction, and this list is whatever the player has on disk
    the moment the plugin opens.

    THE INDEX IS THE HOST'S HANDLE, NOT THE IDENTITY. The list is in the AMP menu's order, so one
    new pack dropped into the Devices folder renumbers every device
    after it. What the state actually carries is the device's NAME, in a property beside this
    parameter (`AmpProcessor::deviceAimSuffix`), and the name is what decides on the way back. */
inline constexpr int maxDevices = 128;

inline constexpr const char* preampDevice = "preamp_device";
inline constexpr const char* preampGain   = "preamp_gain";

/** The preamp's TONE controls — the ones a player computes rather than selects. How many a device
    has varies (SM7 has three: EQ-Lo, EQ-Hi and a two-position Edge; a Victory V4 preamp five), but a
    host needs a fixed set of parameters, so five slots are reserved and the loaded pack decides what
    each one drives. A slot with nothing behind it is hidden rather than shown doing nothing. */
inline constexpr int preampNumMeasured = 5;
inline juce::String preampMeasured (int i) { return "preamp_meas" + juce::String (i + 1); }

/** The EQ, one per captured block and BELONGING to it — there is one captured block, the preamp,
    so there is one console, index 0. It sits after the block's nonlinearity, which is what makes it
    a colour control rather than a distortion-character one, and it goes dark with the block's
    switch. Standing after the last nonlinearity in the chain, it shapes what the amp MADE before
    the room and the speaker have it.

    THERE IS NO ENABLE. The block's switch is the EQ's switch, and a flat link is bit-transparent
    anyway — `core::EqLink` skips a band sitting at exactly 0 dB — so "off" and "flat" were the same
    signal by two names, with Flat already in the presets menu. What the second name actually bought
    was a console you could turn while it was not in the circuit, which is the worst kind of hidden
    state and is now unrepresentable.

    The console grammar: HPF/LPF with a slope choice, LO/HI shelves with free corners (gain + freq,
    no Q), bells B1/B2 (gain + freq + Q) and the narrow switchable B3 — the surgical slot. Bells are
    indexed 0..2 for the id helpers. */
inline constexpr int numEqLinks = 1;

/** Which block a console belongs to. The id helper below answers it, and the DSP asks it too, to
    know whose tone stack is in the signal. */
inline constexpr const char* eqBlockId (int) { return preampId; }

inline juce::String eqId (int l, const char* leaf)
{
    return juce::String (eqBlockId (l)) + "_eq_" + leaf;
}

inline juce::String eqHpfOn    (int l) { return eqId (l, "hpf_on"); }
inline juce::String eqHpfHz    (int l) { return eqId (l, "hpf_hz"); }
inline juce::String eqHpfSlope (int l) { return eqId (l, "hpf_slope"); }
inline juce::String eqLoDb     (int l) { return eqId (l, "lo_db"); }
inline juce::String eqLoHz     (int l) { return eqId (l, "lo_hz"); }
inline juce::String eqHiDb     (int l) { return eqId (l, "hi_db"); }
inline juce::String eqHiHz     (int l) { return eqId (l, "hi_hz"); }
inline juce::String eqLpfOn    (int l) { return eqId (l, "lpf_on"); }
inline juce::String eqLpfHz    (int l) { return eqId (l, "lpf_hz"); }
inline juce::String eqLpfSlope (int l) { return eqId (l, "lpf_slope"); }

inline juce::String eqBellDb (int l, int b) { return eqId (l, "b") + juce::String (b + 1) + "_db"; }
inline juce::String eqBellHz (int l, int b) { return eqId (l, "b") + juce::String (b + 1) + "_hz"; }
inline juce::String eqBellQ  (int l, int b) { return eqId (l, "b") + juce::String (b + 1) + "_q"; }
inline juce::String eqB3On   (int l)        { return eqId (l, "b3_on"); }

/** The slope ladder both cut filters climb. 12 is the default — the amp-world's own. */
inline const juce::StringArray eqSlopes { "6", "12", "18", "24", "48" };
inline constexpr int eqSlopeValues[] = { 6, 12, 18, 24, 48 };
inline constexpr int eqSlopeDefault  = 1;


inline constexpr const char* cabOn = "cab_on";
inline constexpr const char* cabIr = "cab_ir";

// The cabinet IRs on the factory shelf — the family's (device/FactoryShelf.h), each said once, with its
// number, its name and its file, in the cabinet catalog (resources/cabinets.txt), which CMake writes into
// the table read here. What a session keeps of `cabIr` is an INDEX into this list — the IR's number in
// the catalog, which is for good: a new cabinet takes the next one, and nothing already here ever moves,
// so a session saved on WUMBO opens on WUMBO. The menu does not show them in this order; it shows them by
// the shelf's folders, and inside each by cabinet (cabShelves, below). The catalog's default, RED GIANT,
// is the shipping default, the cabinet every face's channels open on too.
inline const juce::StringArray cabIrNames = []
{
    juce::StringArray names;
    for (const auto& ir : catalog::irs)
        names.add (juce::String::fromUTF8 (ir.name));
    return names;
}();
inline constexpr int cabIrDefault = catalog::defaultIr;

/** THE SHELF BY CABINET, as the IR menu lays it out: inside the submenu of the shelf's folder its IRs
    lie in (device/FactoryShelf.h — a folder may hold more than one cabinet), each in this order, as
    `about` — a line that says what its IRs are: the cabinet, its speakers, where they come from — and
    then `count` names of cabIrNames from `first` on. Between them they hold every name on the shelf
    once. The catalog says them, a `cabinet` line each with its IRs under it. `about` is UTF-8. */
using CabShelf = catalog::Cabinet;
inline constexpr auto& cabShelves = catalog::cabinets;

/** A cabinet IR of the player's OWN — not a parameter, because it is not a number: three
    properties on the state tree. The key names the bytes in `device::EmbeddedIrs`, which carries
    the IR itself into presets and sessions; the name is what the border shows; `from` is where in
    the IR library it was picked, so the menu can tick it — and nothing else depends on it. While
    the key names an IR the store holds, it plays and `cabIr` waits underneath; picking a factory
    IR clears all three. */
inline constexpr const char* cabIrUserKey  = "cab_ir_user";
inline constexpr const char* cabIrUserName = "cab_ir_name";
inline constexpr const char* cabIrUserFrom = "cab_ir_from";

/** What a player does to the one IR: cuts its bottom and top (a second-order high-pass and
    low-pass baked INTO the IR, so the convolution costs nothing more), trims its tail (a fraction
    of its length kept — the room's decay is often more than a guitar wants), and flips it. Each
    has a switch, so the IR itself is never touched until asked. Ranges are OrbitCab's. */
inline constexpr const char* cabHpfOn    = "cab_hpf_on";
inline constexpr const char* cabHpfHz    = "cab_hpf_hz";
inline constexpr const char* cabHpfSlope = "cab_hpf_slope";   // the consoles' ladder, baked into the IR
inline constexpr const char* cabLpfOn    = "cab_lpf_on";
inline constexpr const char* cabLpfHz    = "cab_lpf_hz";
inline constexpr const char* cabLpfSlope = "cab_lpf_slope";
inline constexpr const char* cabTrimOn = "cab_trim_on";
inline constexpr const char* cabTrim   = "cab_trim";      // the fraction kept, 0.02..1
inline constexpr const char* cabPhase  = "cab_phase";
inline constexpr float cabHpfMinHz = 0.0f,    cabHpfMaxHz = 1000.0f,  cabHpfDefaultHz = 80.0f;
inline constexpr float cabLpfMinHz = 1200.0f, cabLpfMaxHz = 20000.0f, cabLpfDefaultHz = 7000.0f;
inline constexpr float cabTrimMin  = 0.001f;   // the ms floor lives in the picture (5 ms) — the fraction only guards zero

/** How many channels the chain works, and from where. MONO: one signal end to end, the copy to the
    other channel after everything — the truth of a guitar chain, and one neural pass. STEREO: two
    takes on one bus, each through its own amp, everything twice. STEREO SPACE: mono where the
    sound is made — the preamp, its console, one neural pass — and stereo from the delay on,
    where the space is: the delay's OFFSET spreads one signal into two, the reverb rooms what it
    made, and the cabinet, the master and the limiter follow in stereo. */
/** A BYPASS FOR THE ALIAS TRIMS, and for nothing else. `files[].input_db` is what a pack says about
    ONE borrowed setting — the bottom notches of a gain dial, where a linked capture is played softer
    than the model it borrows — and for a library captured at one honest level those trims only push
    a capture's drive around. Off, every model eats exactly what the chain feeds it, which is a
    diagnostic position rather than a way to play.

    It does NOT touch the pack's own two levels. `chain[].input_db` and `chain[].output_db` are the
    author's statement about the whole device and are applied always, by the player, with no switch
    anywhere: see felitronics::rigplayer's README. Nor does it touch normalization, which is a
    contract and has no switch here either. */
inline constexpr const char* packLevelComp = "pack_level_comp";

inline constexpr const char* stereoMode = "stereo_mode";
inline const juce::StringArray stereoModes { "Mono", "Stereo", "Stereo Space" };
enum class StereoMode { mono, stereo, stereoSpace };

/** TEMPORARY — the audition loops, in the order the player offers them. Default is the first.
    The site's demo loops, the same takes under the same names and in its order.

    The audio is NOT shipped: it is read at run time from the dev machine's private folder, and
    when that folder is absent the player and its menu entry are absent with it. The binary is the
    same either way — presence of the folder is the whole switch. */
inline const juce::StringArray demoLoops { "Eleven Light Years 1", "Eleven Light Years 2", "Eleven Light Years 3",
                                           "Eleven Light Years 4", "Deep Space Is My Home 1",
                                           "Deep Space Is My Home 2", "Cat's Hard Day", "I Am Projection",
                                           "Fifth Dimension" };
inline constexpr const char* demoLoopFiles[] = { "eleven-light-years-1.wav", "eleven-light-years-2.wav",
                                                 "eleven-light-years-3.wav", "eleven-light-years-4.wav",
                                                 "deep-space-is-my-home-1.wav", "deep-space-is-my-home-2.wav",
                                                 "cats-hard-day.wav", "i-am-projection.wav",
                                                 "fifth-dimension.wav" };

inline juce::File demoLoopsDir()
{
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
        .getChildFile ("IdeaProjects/orbit-amp/.private/loops");
}

inline bool demoLoopsPresent()
{
    for (auto* name : demoLoopFiles)
        if (demoLoopsDir().getChildFile (name).existsAsFile())
            return true;

    return false;
}

/** Oversampling for the nonlinear stages. Not a per-preset taste: it trades CPU for alias-free
    saturation, and which trade you want depends on the machine you are on. Lives in the footer with
    the other facts about the run. */

/** The delay — the echo between the preamp's console and the reverb, repeats the room then
    rooms. Repitch by design: TIME is a destination the line glides toward and the glide bends
    pitch, tape-fashion. The loop is dark (DARK is its low pass corner — every pass darkens
    again) and pressed by a fixed saturator; OFFSET holds one channel's repeats back against
    the other's, the stereo of the block; MIX adds the wet over an untouched dry. */
inline constexpr const char* delayOn      = "delay_on";
inline constexpr const char* delaySync    = "delay_sync";
inline constexpr const char* delayTimeMs  = "delay_time_ms";
inline constexpr const char* delayDiv     = "delay_div";
inline constexpr const char* delayBpm     = "delay_bpm";
/** Whose tempo the sync counts in when a host is conducting: the HOST's (the default — a delay in a
    session plays in the session's time) or the block's OWN BPM field, for a player who wants the
    echo on a grid of its own. With no tempo from anyone — the standalone — the field conducts. */
inline constexpr const char* delayHostTempo = "delay_host_tempo";
inline constexpr const char* delayRepeats = "delay_repeats";
inline constexpr const char* delayDark    = "delay_dark";
inline constexpr const char* delayOffset  = "delay_offset";
inline constexpr const char* delayMix     = "delay_mix";

/** The sync ladder, longest first, dotted and triplet beside each plain value — the delay's own
    (felitronics::delay::kDivisionNames), so the choice a session stores is the step the engine
    counts. Its length in quarter notes is the engine's too: felitronics::delay::divisionMs. */
inline const juce::StringArray delayDivisions { felitronics::delay::kDivisionNames,
                                                felitronics::delay::kNumDivisions };
inline constexpr int delayDivDefault = felitronics::delay::kDivisionDefault;   // 1/4

/** The BPM the sync runs on when no host is conducting — the standalone's field — or when the
    player has told the delay to keep its own (`delayHostTempo` off). */
inline constexpr float delayBpmMin     = felitronics::delay::kBpmMin,       // 40
                       delayBpmMax     = felitronics::delay::kBpmMax,       // 240
                       delayBpmDefault = felitronics::delay::kBpmDefault;   // 120

inline constexpr float delayTimeMinMs   = felitronics::delay::kTimeMinMs,   // 20
                       delayTimeMaxMs   = felitronics::delay::kTimeMaxMs;   // 2000
inline constexpr float delayOffsetMaxMs = felitronics::delay::kOffsetMaxMs; // 30
/** DARK is a percent of darkness, so the knob's direction tells the truth: clockwise darkens.
    The engine maps it onto the loop's low pass corner, the bright ceiling falling log-evenly
    to the dark floor (felitronics::delay::darkHzForPercent, 16 kHz down to 800 Hz). */
inline constexpr float delayDarkDefaultPct = felitronics::delay::kDarkDefaultPct;   // ~4 kHz — the tape-ish middle

inline constexpr const char* reverbType     = "reverb_type";
inline constexpr const char* reverbMix      = "reverb_mix";

/** The tail's late refinements. DECAY scales the character's own breath (×0.5..×2 — the character
    stays the voice); PREDELAY holds the tail back so the attack stays dry (0..100 ms); the HPF
    cleans the WET only and is ALWAYS in — a low tail muddies everything downstream of it and
    nothing downstream takes it back out. At its 40 Hz floor it is as good as air; there is
    nothing to switch. */
inline constexpr const char* reverbDecay    = "reverb_decay";
inline constexpr const char* reverbPredelay = "reverb_predelay";
inline constexpr const char* reverbHpfHz    = "reverb_hpf_hz";

/** Reverb characters, in the order of the design's simple case — the room's own
    (felitronics::reverb::kCharacterNames), so the index a session stores is the character the engine
    plays. Size and damping follow from the character rather than being loose knobs — the design
    calls for Mix only. */
inline const juce::StringArray reverbCharacters { felitronics::reverb::kCharacterNames,
                                                  felitronics::reverb::kNumCharacters };

/** Tone range. The measured hardware spanned about -10..+8 per control and up to 20 dB of travel on
    presence; the survey's conclusion on the narrower first pass was that the ranges were too small.
    +-15 covers every device measured with room either side. */
inline constexpr float toneRangeDb = 15.0f;

/** The voicing types, in the order the design lists them — clean through modern is a ramp of
    increasing gain, so the order is meaningful, not alphabetical. */
inline const juce::StringArray typeNames { "Clean", "Edge", "Crunch", "High-gain", "Modern" };

/** How many voices a type can hold. The voice parameter is an INDEX, not a choice, because the
    names differ per type and a host-visible choice list has to be fixed at construction. */
inline constexpr int maxVoicesPerType = 8;


/** EVERY LINK STANDS IN THE CHAIN FOR GOOD. There used to be a second switch beside each one's ON
    — in the rig or out of it, with a checklist that took a block off the panel and the panel
    re-splitting under the hands. Gone: a link's one switch is ON or STANDBY, set by its arrow in the
    strip, and a link in STANDBY keeps its place on the panel, dimmed, its action IGNORED — a block
    is not fed, a guard does not press. The two ends, IN and OUT, have no switch at all: their
    volumes always apply. */
inline constexpr const char* tunerOn       = "tuner_on";
/** The tuner's MUTE: silence at the jack while the tuner works, so a string can be tuned without
    the room hearing it. The tuner keeps listening — it reads the input before anything — and a
    tuner standing by does not mute, like any link's action in STANDBY. */
inline constexpr const char* tunerMute     = "tuner_mute";

//==============================================================================
// THE CHAIN, AS ONE LIST.
//
// The same links used to be written out in four places — the strip's own table in the editor, the
// cost stages in the processor, and the breakdown's names TWICE in the footer. Four copies of one
// list are four chances to disagree, and they had already started to. This is the list; all four
// read it now.
//
// Nothing visual lives here: a colour and a face on the panel are the editor's business. What a
// link states about ITSELF — whether it is a captured device, whether it has a face at all, which
// switch it answers to, what it costs — belongs with the parameters, not with the paint.

/** Which link a cost entry belongs to, or `numChainRows` for one that belongs to nobody (TOTAL,
    and any stage no link has claimed). Declared after the table below — see `rowForStage`. */

/** The DSP cost entries, in the order the chain runs them: a captured block is followed by its own
    console. Here rather than in the processor so a link can name its stage and the footer can
    label one, without either of them having to know about the other. */
enum Stage { stTotal, stTuner, stGate, stPreamp, stPreampEq, stDelay, stReverb,
             stCab, stLimit, stOut, numStages };

/** What the breakdown calls a stage. TWO spellings, both ASCII on purpose: the copied report runs
    through `formatted ("%-8s")`, where a multi-byte character breaks the encoding and the column
    width together, while the drawn list has room for the longer word. */
struct StageName { const char* brief; const char* full; };

inline constexpr StageName stageNames[numStages] = {
    { "TOTAL",  "TOTAL"     },
    { "TUNER",  "TUNER"     },
    { "GATE",   "GATE"      },
    { "AMP",    "AMP"       },
    { "AMP EQ", "AMP EQ"    },
    { "DELAY",  "DELAY"     },
    { "REVERB", "REVERB"    },
    { "CAB",    "CAB"       },
    { "LIMIT",  "LIMIT"     },
    { "OUT",    "OUT"       },
};

/** A link's place in the chain — by NAME, not by arithmetic. The order is the chain's order and
    doubles as the index into `chainLinks`; naming the places is what lets one stand down without
    every row after it sliding under somebody's `i - first` sum. */
enum ChainRow { rowIn, rowTuner, rowGate, rowPreamp, rowDelay, rowReverb, rowCab,
                rowLimit, rowOut, numChainRows };

/** One link of the chain. */
struct ChainLink
{
    const char* name;       // what its arrow says
    bool        captured;   // wears the captured accent rather than ours
    bool        hasTile;    // a face on the panel; a link without one answers with its menu
    const char* onParam;      // STANDBY/ON — the switch its arrow writes; none for the two ends
    Stage       stage;        // the cost entry it owns
    Stage       eqStage;    // a captured block's console; `numStages` for everyone else
};

inline constexpr ChainLink chainLinks[numChainRows] = {
    /* IN     */ { "IN",     false, true,  nullptr,   numStages, numStages },
    /* TUNER  */ { "TUNER",  false, true,  tunerOn,   stTuner,  numStages },
    /* GATE   */ { "GATE",   false, false, gateOn,    stGate,   numStages },
    /* AMP    */ { "AMP",    true,  true,  preampOn,  stPreamp, stPreampEq },
    /* DELAY  */ { "DELAY",  false, true,  delayOn,   stDelay,  numStages },
    /* REVERB */ { "REVERB", false, true,  reverbOn,  stReverb, numStages },
    /* CAB IR */ { "CAB IR", true,  true,  cabOn,     stCab,    numStages },
    /* LIMIT  */ { "LIMIT",  false, false, limiterOn, stLimit,  numStages },
    /* OUT    */ { "OUT",    false, true,  nullptr,   stOut,    numStages },
};

/** Whose cost a stage is. A captured block owns two — itself and its own console — and TOTAL
    belongs to nobody, so it is always shown. Walked rather than tabulated: nine links, ten
    stages, and one table that cannot fall out of step with itself. */
inline constexpr int rowForStage (Stage s)
{
    for (int i = 0; i < numChainRows; ++i)
        if (chainLinks[(size_t) i].stage == s || chainLinks[(size_t) i].eqStage == s)
            return i;

    return numChainRows;
}

/** THE ENGINE'S OWN PACK, by its name in a product's list: the device a fresh OrbitAmp plays, and the one
    whose knobs and switches give every amp's their defaults (Parameters.cpp, blockDefaults). */
inline constexpr const char* enginePack = "Soldano X88-IR Crunch";

/** THE CHANNEL A DEVICE OPENS ON where it is not its first — the first time the hand picks the device,
    before it was ever left on one (AmpChannels::openingOf) — by the pack's `rig_id`. The Friedman IR-X
    opens on its Channel 2 (Oleh, 24.10). */
inline constexpr const char* openingChannels[] = { "ir-x-ch2-poweramp-67e9" };

/** WHERE A FRESH INSTANCE STANDS, wherever that is not where the engine stands: the defaults createLayout
    gives the few parameters a product, or the place it runs in, opens elsewhere. A parameter's default
    is what a fresh instance IS — what a host shows as its default, resets it to, and holds a new instance
    to (CLAP's params; clap-validator's param-default-values) — so a product that opens on a channel of
    its own declares that channel's pack, room and delay (channels::Engine::opening), and the channel
    mode is declared where the environment puts it (AmpProcessor::modeOfEnvironment). Default-made it
    is the engine's own: OrbitAmp's, in a plug-in on a mono bus — and the one place its values are said,
    which is where the amp itself puts these parameters back (engineDefault). */
struct Opening
{
    /** The pack it plays, by the `rig_id` its manifest carries, or none: the engine's, the Soldano X88-IR Crunch.
        Only the device — the knobs and switches keep the defaults the engine's pack gives them, which is
        where a channel the hand has never been on finds them (channels/Channels.cpp, defaultOf). */
    const char* capture = nullptr;
    const char* reverb  = "Plate";      // the room, one of reverbCharacters
    float reverbMix     = 30.0f;        // its share, percent
    float delayMix      = 0.0f;         // the delay's, percent
    StereoMode stereoMode = StereoMode::mono;
};

/** The engine's parameters, for the product of that name: the devices a parameter offers are the ones
    it plays, and a fresh instance stands where `opening` says. */
juce::AudioProcessorValueTreeState::ParameterLayout createLayout (const juce::String& product, const Opening& opening = {});

/** WHERE THE AMP ITSELF PUTS A PARAMETER BACK — INIT, the face's "Reset to default", a double-click on a
    control, a value healed from NaN — as normalised for `p`. The parameters an Opening moves go back
    where the engine's own opening has them (a default-made Opening; the device to `enginePlace`, where
    enginePack stood in the product's list as the processor was made), as they always have; every other
    parameter to its declared default, which is the same thing. A host's own reset follows the declared
    default — that is the format's contract, and where a fresh instance stands. */
float engineDefault (const juce::RangedAudioParameter& p, int enginePlace);


} // namespace orbitamp::params
