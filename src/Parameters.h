#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace orbitamp::params
{

// Parameter IDs. Stable strings — a host stores these in its session, so renaming one breaks every
// saved project that used it.
inline constexpr const char* boostOn     = "boost_on";
inline constexpr const char* preampOn    = "preamp_on";
inline constexpr const char* eqOn        = "eq_on";
inline constexpr const char* reverbOn    = "reverb_on";

inline constexpr const char* boostGain  = "boost_gain";
inline constexpr const char* boostTone  = "boost_tone";

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

inline constexpr const char* preampType  = "preamp_type";
inline constexpr const char* preampVoice = "preamp_voice";
inline constexpr const char* preampGain  = "preamp_gain";

inline constexpr const char* eqLow      = "eq_low";
inline constexpr const char* eqMid      = "eq_mid";
inline constexpr const char* eqHigh     = "eq_high";
inline constexpr const char* eqPresence = "eq_presence";
inline constexpr const char* eqMidHz    = "eq_mid_hz";
inline constexpr const char* eqHpfOn  = "eq_hpf_on";
inline constexpr const char* eqHpfHz  = "eq_hpf_hz";
inline constexpr const char* eqLpfOn  = "eq_lpf_on";
inline constexpr const char* eqLpfHz  = "eq_lpf_hz";

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
