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

/** The noise gate — the second service link, right after the tuner: it keys off the raw guitar,
    and kills the hum before any dirt can multiply it. The threshold is the OPEN level in dBFS
    against that raw input; feel (attack, hold, hysteresis, floor) is fixed in the engine. */
inline constexpr const char* gateOn        = "gate_on";
inline constexpr const char* gateThreshold = "gate_threshold";

/** Mirrors the engine's Schmitt hysteresis (felitronics NoiseGate::Config default): the CLOSE
    level sits this far under the OPEN level. The meter draws both marks, so the number the
    display promises is the number the state machine runs. */
inline constexpr float gateHysteresisDb = 6.0f;

/** TEMPORARY — hear the capture with nothing of ours on it.

    Everything a block adds comes off: the measured curves the pack shipped, rebuilt as filters, and
    our own EQ. What stays is what picks WHICH model plays — the gain dial and the device's selecting
    controls, an octave switch among them — because those are not treatments, they are the choice of
    capture. Goes when the question it answers stops being asked. */
inline juce::String rawId (const char* blk) { return juce::String (blk) + "_raw"; }

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
inline juce::String blockMeasured (const char* blk, int i) { return juce::String (blk) + "_meas" + juce::String (i + 1); }

/** A pedal's MEASURED controls — the ones a player computes rather than selects. How many a device
    has varies (SM7 has three: EQ-Lo, EQ-Hi and a two-position Edge), but a host needs a fixed set of
    parameters, so three slots are reserved and the loaded pack decides what each one drives. A slot
    with nothing behind it is hidden rather than shown doing nothing. */
inline constexpr int boostNumMeasured = 3;
inline juce::String boostMeasured (int i) { return "boost_meas" + juce::String (i + 1); }
/** Which device is loaded, as an index into the scanned list. An index rather than a choice list:
    a host fixes a Choice's names at construction, and this list is whatever the player has on disk
    the moment the plugin opens. */
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
inline constexpr int preampNumMeasured = 3;
inline juce::String preampMeasured (int i) { return "preamp_meas" + juce::String (i + 1); }

/** The EQ links — standalone links of the chain, not a section of any block. Exactly two, fixed:
    `eq1` ahead of the boost and `eq2` between the boost and the preamp. Fixed because a host needs
    an unchanging parameter list; two because those are the places that MEAN something — eq1 decides
    what reaches the first nonlinearity, so it changes the kind of distortion, and eq2 colours what
    the boost made before the preamp distorts it again. A link's place in the chain answers "pre or
    post", which is why no placement switch exists. `l` is the link index, 0-based. */
inline constexpr int numEqLinks = 2;

inline juce::String eqId       (int l, const char* leaf) { return "eq" + juce::String (l + 1) + "_" + leaf; }
inline juce::String eqOn       (int l) { return eqId (l, "on"); }
inline juce::String eqLow      (int l) { return eqId (l, "low"); }
inline juce::String eqMid      (int l) { return eqId (l, "mid"); }
inline juce::String eqHigh     (int l) { return eqId (l, "high"); }
inline juce::String eqPresence (int l) { return eqId (l, "presence"); }
inline juce::String eqMidHz    (int l) { return eqId (l, "mid_hz"); }
inline juce::String eqHpfOn    (int l) { return eqId (l, "hpf_on"); }
inline juce::String eqHpfHz    (int l) { return eqId (l, "hpf_hz"); }
inline juce::String eqLpfOn    (int l) { return eqId (l, "lpf_on"); }
inline juce::String eqLpfHz    (int l) { return eqId (l, "lpf_hz"); }

inline constexpr const char* cabOn = "cab_on";

/** Two mic slots, each with its own switch, its own pick from the SAME full list, and its own place
    on the grille. Two mics on one cabinet is how the sound is actually made — one close and bright,
    one back and thick — so the second is not an extra, it is the other half. */
inline constexpr int cabNumMics = 2;

inline juce::String cabMicOn   (int i) { return "cab_mic" + juce::String (i + 1) + "_on"; }
inline juce::String cabMicType (int i) { return "cab_mic" + juce::String (i + 1) + "_type"; }
inline juce::String cabMicPos  (int i) { return "cab_mic" + juce::String (i + 1) + "_pos"; }
inline juce::String cabMicDist (int i) { return "cab_mic" + juce::String (i + 1) + "_dist"; }
inline juce::String cabMicAngle (int i) { return "cab_mic" + juce::String (i + 1) + "_angle"; }

/** Mic angle, as three named positions rather than a sweep. Angle comes from captures, and captures
    exist at the angles they were taken at — a slider in 15-degree steps would offer thirteen
    positions where three have anything behind them. */
inline const juce::StringArray cabAngles { "-30", "0", "+30" };

/** The mics, by the two everyone actually reaches for first. More arrive with the captures. */
inline const juce::StringArray cabMics { "SM57", "MD421" };

/** The speaker's radial zones, from the rim inwards — the rows of the placement grid. They are the
    driver's own geometry, which is why the grid draws the speaker beside them. */
inline const juce::StringArray cabPositions { "Cone Edge", "Center Cone", "Cap Edge", "Center Cap" };

inline constexpr const char* powerOn   = "power_on";
inline constexpr const char* powerType  = "power_type";
inline constexpr const char* powerDrive = "power_drive";
inline constexpr const char* powerSag   = "power_sag";
inline constexpr const char* powerTube  = "power_tube";
inline constexpr const char* powerCount = "power_count";
inline constexpr const char* oversample = "oversample";

/** TEMPORARY — the audition loops, in the order the player offers them. Default is the first. */
inline const juce::StringArray demoLoops { "Eleven Light Years", "Cats Hard Day",
                                           "Deep Space Is My Home", "Fifth Dimension" };

/** Oversampling for the nonlinear stages. Not a per-preset taste: it trades CPU for alias-free
    saturation, and which trade you want depends on the machine you are on. Lives in the footer with
    the other facts about the run. */
inline const juce::StringArray oversampleFactors { "2x", "4x", "8x", "16x" };
inline constexpr int oversampleValues[] = { 2, 4, 8, 16 };

/** Output bottles, ordered by headroom — least first, which is also the order they break up in.
    Measured across the Drive sweep an EL84 loses 12.5 dB to compression where a KT88 loses 4.5. */
inline const juce::StringArray powerTubes { "EL84", "EL34", "6L6", "KT88" };

/** One bottle or two. Not decoration: one output tube IS single-ended class A and two are push-pull,
    which the stage takes as a first-class parameter and which measures differently. */
inline const juce::StringArray powerCounts { "1", "2" };

/** What drives the power amp. The block's own switch is the "none" — a second way to turn it off
    inside the list would be two controls for one state. Simulation is our white-box tube stage;
    the NAM slots are captures. */
inline const juce::StringArray powerTypes { "Simulation", "NAM 1", "NAM 2" };

inline constexpr const char* reverbType = "reverb_type";
inline constexpr const char* reverbMix  = "reverb_mix";

/** Reverb characters, in the order of the design's simple case. Size and damping follow from the
    character rather than being loose knobs — the design calls for Mix only. */
inline const juce::StringArray reverbCharacters { "Room", "Hall", "Plate", "Spring" };

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

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

} // namespace orbitamp::params
