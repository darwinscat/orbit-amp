// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace orbitamp::params
{

// Parameter IDs. Stable strings — a host stores these in its session, so renaming one breaks every
// saved project that used it.
inline constexpr const char* boostOn     = "boost_on";
inline constexpr const char* preampOn    = "preamp_on";
inline constexpr const char* reverbOn    = "reverb_on";

inline constexpr const char* boostGain  = "boost_gain";
inline constexpr const char* boostTone  = "boost_tone";

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

/** The noise gate — the second service link, right after the tuner: it keys off the raw guitar,
    and kills the hum before any dirt can multiply it. The threshold is the OPEN level in dBFS
    against that raw input; feel (attack, hold, hysteresis, floor) is fixed in the engine. */
inline constexpr const char* gateOn        = "gate_on";
inline constexpr const char* gateThreshold = "gate_threshold";

/** Mirrors the engine's Schmitt hysteresis (felitronics NoiseGate::Config default): the CLOSE
    level sits this far under the OPEN level. The meter draws both marks, so the number the
    display promises is the number the state machine runs. */
inline constexpr float gateHysteresisDb = 6.0f;

/** Where the gate MUTES. It always KEYS off the raw input — that is the whole dual-detection
    idea — but the VCA can stand at the chain's start or after the preamp, ahead of the reverb.
    Pre-Reverb is the G-String architecture and the default: it kills the hiss the boost and the
    preamp ADD (which a gate at the start never meets), the clean key never pumps on distortion
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

inline constexpr const char* boostId  = "boost";
inline constexpr const char* preampId = "preamp";

/** A captured block's own parameters, derived from its id prefix — the same shape for the boost,
    the preamp, and whatever captured block comes next. The named constants around them predate
    these and stay for call sites that mean a specific block. */
inline juce::String blockOn       (const char* blk) { return juce::String (blk) + "_on"; }
inline juce::String blockDevice   (const char* blk) { return juce::String (blk) + "_device"; }
inline juce::String blockGain     (const char* blk) { return juce::String (blk) + "_gain"; }
/** How the gain dial moves between the captured positions. SMOOTH: the two neighbouring captures
    play together, mixed by angle — a continuous dial, at the price of two models running. STEP:
    the dial lands only on the captured positions and one capture plays at a time. */
inline juce::String blockSmooth   (const char* blk) { return juce::String (blk) + "_smooth"; }
/** The block's INPUT trim — how hard the capture is fed, applied immediately before the model and
    metered right there. ONE per block, and there is deliberately no output volume beside it.

    A block's output and the next block's input are two multipliers in series with nothing between
    them: the same decibel written twice, and two places for it to hide from you. Of the two names
    for that one number, INPUT is the useful one — the meter beside it then reads what the capture
    is being fed, which is the question the green zone answers.

    The price, and it is the hardware's rather than ours: on a captured device this is a DRIVE
    control, so every loudness fix made HERE is also a tone change. Between a real pedal and a real
    amp there is exactly one knob and turning it up is both louder and dirtier. The last block's
    output is caught by the master volume, and what the next block is fed by its own DRIVE.

    …which is why the loudness fix stopped living here. A pack now states its own output level
    (`chain[].output_db`, namz schema 4) and the player applies it after everything: that is the
    number that makes a boost stop cooking the preamp it feeds, and it changes volume and nothing
    else. This hand is for drive; that one is for balance. */
inline juce::String blockIn (const char* blk) { return juce::String (blk) + "_in"; }

/** Asymmetric on purpose. Forty-eight down and twelve up: cutting is what gain staging mostly
    asks for — a hot interface, a hot pack, a boost feeding a preamp — and now that the hand
    rides a full-height wall there is room for a real cut; twelve decibels of boost is already
    more than a capture wants before it stops being the device that was captured. The SAME
    reach on both walls: the IN trim and the console's LEVEL move over one ladder. */
inline constexpr float blockTrimMinDb = -48.0f;
inline constexpr float blockTrimMaxDb =  12.0f;

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

/** A pedal's TONE controls — the ones a player computes rather than selects. How many a device has
    varies (SM7 has three: EQ-Lo, EQ-Hi and a two-position Edge; a Victory V4 preamp five), but a host
    needs a fixed set of parameters, so five slots are reserved and the loaded pack decides what each
    one drives. A slot with nothing behind it is hidden rather than shown doing nothing. */
inline constexpr int boostNumMeasured = 5;
inline juce::String boostMeasured (int i) { return "boost_meas" + juce::String (i + 1); }
/** Which device is loaded, as an index into the scanned list. An index rather than a choice list:
    a host fixes a Choice's names at construction, and this list is whatever the player has on disk
    the moment the plugin opens.

    THE INDEX IS THE HOST'S HANDLE, NOT THE IDENTITY. The list is sorted bundled-first and then
    along the gain ramp, so one new pack dropped into the Devices folder renumbers every device
    after it. What the state actually carries is the device's NAME, in a property beside this
    parameter (`AmpProcessor::deviceAimSuffix`), and the name is what decides on the way back. */
inline constexpr const char* boostDevice = "boost_device";
inline constexpr int maxDevices = 128;

/** The pedal tone control's sweep, as the descriptor models it: a first-order low pass whose corner
    moves. The measured example fitted 800 Hz at the dark end; wide open it is out of the way. */
inline constexpr float boostToneLoHz = 800.0f;
inline constexpr float boostToneHiHz = 20000.0f;

inline constexpr const char* preampDevice = "preamp_device";
inline constexpr const char* preampGain   = "preamp_gain";

/** The preamp's measured controls, same arrangement as the boost's: a fixed set of slots, and the
    loaded device decides what each one drives. */
inline constexpr int preampNumMeasured = 5;
inline juce::String preampMeasured (int i) { return "preamp_meas" + juce::String (i + 1); }

/** The EQ, one per captured block and BELONGING to it — index 0 is the boost's, index 1 the
    preamp's. Each sits after its block's nonlinearity, which is what makes it a colour control
    rather than a distortion-character one, and each goes dark with its block's switch.

    Their places in the chain are not arbitrary: the boost's lands in front of the preamp, which
    is where a real amplifier keeps its tone stack, so there is no PRE/POST switch — the
    nonlinearity downstream is already being fed by one. The preamp's stands after the last
    nonlinearity in the chain, shaping what the amp MADE before the room and the speaker have it.

    THERE IS NO ENABLE. The block's switch is the EQ's switch, and a flat link is bit-transparent
    anyway — `core::EqLink` skips a band sitting at exactly 0 dB — so "off" and "flat" were the same
    signal by two names, with Flat already in the presets menu. What the second name actually bought
    was a console you could turn while it was not in the circuit, which is the worst kind of hidden
    state and is now unrepresentable.

    The console grammar: HPF/LPF with a slope choice, LO/HI shelves with free corners (gain + freq,
    no Q), bells B1/B2 (gain + freq + Q) and the narrow switchable B3 — the surgical slot. Bells are
    indexed 0..2 for the id helpers. */
inline constexpr int numEqLinks = 2;

/** Which block a console belongs to. The two have always been the same question asked twice — the
    id helper below already answered it — and the DSP now has to ask it too, to know whose tone
    stack is in the signal. */
inline constexpr const char* eqBlockId (int l) { return l == 0 ? boostId : preampId; }

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
inline juce::String eqLevel    (int l) { return eqId (l, "level"); }

inline juce::String eqBellDb (int l, int b) { return eqId (l, "b") + juce::String (b + 1) + "_db"; }
inline juce::String eqBellHz (int l, int b) { return eqId (l, "b") + juce::String (b + 1) + "_hz"; }
inline juce::String eqBellQ  (int l, int b) { return eqId (l, "b") + juce::String (b + 1) + "_q"; }
inline juce::String eqB3On   (int l)        { return eqId (l, "b3_on"); }

/** The slope ladder both cut filters climb. 12 is the default — the amp-world's own. */
inline const juce::StringArray eqSlopes { "6", "12", "18", "24", "48" };
inline constexpr int eqSlopeValues[] = { 6, 12, 18, 24, 48 };
inline constexpr int eqSlopeDefault  = 1;

inline constexpr float eqLevelMinDb = -48.0f, eqLevelMaxDb = 12.0f;   // the walls' shared ladder

inline constexpr const char* cabOn = "cab_on";
inline constexpr const char* cabIr = "cab_ir";

// The cabinet IRs, orbitcab's set — display names derived from the files. big-bubba is the
// shipping default: the one he asked for by (approximately) name.
inline const juce::StringArray cabIrNames {
    "COOKIE MONSTER", "DARTH GENOCIDER", "KITTEN SLAYER", "KAIJU TAMER", "ICEBURN SUICIDE",
    "VERTICAL LIP STABBER", "MANSLAUGHTER JOE", "BIG BUBBA", "DEVILS CUNNILINGUS",
    "OCTOBER 32TH", "WUMBO", "WORLD COLLIDER", "CANNIBAL CHOIR", "CATHODE RAY FLESHBURN",
    "IMPALER JIM", "NACHO GUACAMOLE", "PICKLE PUNISHER", "WASABI WARRIOR", "PESTO PALADIN",
    "DON SPINACIO", "KILL DILL",
};
inline constexpr int cabIrDefault = 7;   // BIG BUBBA

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
    sound is made — boost, preamp, their consoles, one neural pass — and stereo from the delay on,
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

    The audio is NOT shipped: it is read at run time from the dev machine's private folder, and
    when that folder is absent the player and its menu entry are absent with it. The binary is the
    same either way — presence of the folder is the whole switch. */
inline const juce::StringArray demoLoops { "GGG", "Eleven Light Years", "Cats Hard Day",
                                           "Deep Space Is My Home", "Fifth Dimension" };
inline constexpr const char* demoLoopFiles[] = { "ggg.wav", "eleven-light-years.wav",
                                                 "cats-hard-day.wav", "deep-space-is-my-home.wav",
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
inline constexpr const char* delayRepeats = "delay_repeats";
inline constexpr const char* delayDark    = "delay_dark";
inline constexpr const char* delayOffset  = "delay_offset";
inline constexpr const char* delayMix     = "delay_mix";

/** The sync ladder, longest first, dotted and triplet beside each plain value. The table is
    the same fact in quarter notes: ms = beats x 60000 / BPM. */
inline const juce::StringArray delayDivisions { "1/1", "1/2.", "1/2", "1/2T", "1/4.", "1/4",
                                                "1/4T", "1/8.", "1/8", "1/8T", "1/16.", "1/16",
                                                "1/16T" };
inline constexpr float delayDivisionBeats[] = { 4.0f, 3.0f, 2.0f, 4.0f / 3.0f, 1.5f, 1.0f,
                                                2.0f / 3.0f, 0.75f, 0.5f, 1.0f / 3.0f, 0.375f,
                                                0.25f, 1.0f / 6.0f };
inline constexpr int delayDivDefault = 5;   // 1/4

/** The BPM the sync runs on when no host is conducting — the standalone's field. A host that
    reports a tempo outranks it. */
inline constexpr float delayBpmMin = 40.0f, delayBpmMax = 240.0f, delayBpmDefault = 120.0f;

inline constexpr float delayTimeMinMs = 20.0f, delayTimeMaxMs = 2000.0f;
inline constexpr float delayOffsetMaxMs = 30.0f;
/** DARK is a percent of darkness, so the knob's direction tells the truth: clockwise darkens.
    The engine maps it onto the loop's low pass corner, the bright ceiling falling log-evenly
    to the dark floor. */
inline constexpr float delayDarkLoHz = 800.0f, delayDarkHiHz = 16000.0f;
inline constexpr float delayDarkDefaultPct = 45.0f;   // ~4 kHz — the tape-ish middle

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

/** Reverb characters, in the order of the design's simple case. Size and damping follow from the
    character rather than being loose knobs — the design calls for Mix only. */
inline const juce::StringArray reverbCharacters { "Ambience", "Room", "Hall", "Plate", "Spring",
                                                  "Modulated" };

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


/** THE TWO SWITCHES EVERY LINK HAS.

    `*_present` — is this link in the rig at all. Set in the strip's checklist; a link that is not
    in the rig has no arrow in the strip, no face on the panel, no entry in the cost breakdown,
    and is not processed.

    `*_on` — STANDBY or ON. Set by the link's arrow in the strip; a link in STANDBY keeps its
    place but its action is IGNORED — a volume is not applied, a mute is not applied, a block is
    not processed.

    Both are ordinary automatable parameters, so both ride in the preset, the host's session, the
    A/B/C/D registers and undo without a line of code about storage. Two rather than one
    three-position control on purpose: bypassing a link is something players automate and removing
    it from the rig is not, and two actions of such different weight must not share one lane, where
    aiming at the middle of three and landing on the end takes a block out of the rig instead of
    stepping it aside.

    The fourth combination — not in the rig, switch up — is not a contradiction: it is the link
    remembering whether it was playing while it sits out.

    IN, OUT and TUNER get their own `*_on` here because they never had one: hiding a column used
    to be a machine preference that also wrote its trim to zero. Now STANDBY simply does not apply
    the volume, and the value the player set stays where they left it. */
inline constexpr const char* inPresent     = "in_present";
inline constexpr const char* inOn          = "in_on";
inline constexpr const char* tunerPresent  = "tuner_present";
inline constexpr const char* tunerOn       = "tuner_on";
inline constexpr const char* gatePresent   = "gate_present";
inline constexpr const char* boostPresent  = "boost_present";
inline constexpr const char* preampPresent = "preamp_present";
inline constexpr const char* delayPresent  = "delay_present";
inline constexpr const char* reverbPresent = "reverb_present";
inline constexpr const char* cabPresent    = "cab_present";
inline constexpr const char* limitPresent  = "limit_present";
inline constexpr const char* outPresent    = "out_present";
inline constexpr const char* outOn         = "out_on";

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
enum Stage { stTotal, stTuner, stGate, stBoost, stEq1, stPreamp, stEq2, stDelay, stReverb,
             stCab, stLimit, stOut, numStages };

/** What the breakdown calls a stage. TWO spellings, both ASCII on purpose: the copied report runs
    through `formatted ("%-8s")`, where a multi-byte character breaks the encoding and the column
    width together, while the drawn list has room for the longer word. */
struct StageName { const char* brief; const char* full; };

inline constexpr StageName stageNames[numStages] = {
    { "TOTAL",  "TOTAL"     },
    { "TUNER",  "TUNER"     },
    { "GATE",   "GATE"      },
    { "BOOST",  "BOOST"     },
    { "B-EQ",   "BOOST EQ"  },
    { "PREAMP", "PREAMP"    },
    { "P-EQ",   "PREAMP EQ" },
    { "DELAY",  "DELAY"     },
    { "REVERB", "REVERB"    },
    { "CAB",    "CAB"       },
    { "LIMIT",  "LIMIT"     },
    { "OUT",    "OUT"       },
};

/** A link's place in the chain — by NAME, not by arithmetic. The order is the chain's order and
    doubles as the index into `chainLinks`; naming the places is what lets one stand down without
    every row after it sliding under somebody's `i - first` sum. */
enum ChainRow { rowIn, rowTuner, rowGate, rowBoost, rowPreamp, rowDelay, rowReverb, rowCab,
                rowLimit, rowOut, numChainRows };

/** One link of the chain. */
struct ChainLink
{
    const char* name;       // what its arrow says
    bool        captured;   // wears the captured accent rather than ours
    bool        hasTile;    // a face on the panel; a link without one answers with its menu
    const char* onParam;      // STANDBY/ON — the switch its arrow writes
    const char* presentParam; // in the rig or not — the switch the strip's checklist writes
    Stage       stage;        // the cost entry it owns
    Stage       eqStage;    // a captured block's console; `numStages` for everyone else
};

inline constexpr ChainLink chainLinks[numChainRows] = {
    /* IN     */ { "IN",     false, true,  inOn,      inPresent,     numStages, numStages },
    /* TUNER  */ { "TUNER",  false, true,  tunerOn,   tunerPresent,  stTuner,  numStages },
    /* GATE   */ { "GATE",   false, false, gateOn,    gatePresent,   stGate,   numStages },
    /* BOOST  */ { "BOOST",  true,  true,  boostOn,   boostPresent,  stBoost,  stEq1     },
    /* PREAMP */ { "PREAMP", true,  true,  preampOn,  preampPresent, stPreamp, stEq2     },
    /* DELAY  */ { "DELAY",  false, true,  delayOn,   delayPresent,  stDelay,  numStages },
    /* REVERB */ { "REVERB", false, true,  reverbOn,  reverbPresent, stReverb, numStages },
    /* CAB IR */ { "CAB IR", true,  true,  cabOn,     cabPresent,    stCab,    numStages },
    /* LIMIT  */ { "LIMIT",  false, false, limiterOn, limitPresent,  stLimit,  numStages },
    /* OUT    */ { "OUT",    false, true,  outOn,     outPresent,    stOut,    numStages },
};

/** Whose cost a stage is. A captured block owns two — itself and its own console — and TOTAL
    belongs to nobody, so it is always shown. Walked rather than tabulated: ten links, twelve
    stages, and one table that cannot fall out of step with itself. */
inline constexpr int rowForStage (Stage s)
{
    for (int i = 0; i < numChainRows; ++i)
        if (chainLinks[(size_t) i].stage == s || chainLinks[(size_t) i].eqStage == s)
            return i;

    return numChainRows;
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

} // namespace orbitamp::params
