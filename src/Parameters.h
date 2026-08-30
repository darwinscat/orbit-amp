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
inline const juce::StringArray gatePositions { "Start", "Pre-Reverb" };

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
inline constexpr const char* powerId  = "power";    // the captured power amp: the same set of slots as the other two

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
    control, so every loudness fix between two blocks is also a tone change. Between a real pedal
    and a real amp there is exactly one knob and turning it up is both louder and dirtier. The last
    block's output is caught by the master volume, and what the power amp is fed by its own DRIVE. */
inline juce::String blockIn (const char* blk) { return juce::String (blk) + "_in"; }

/** Asymmetric on purpose. Twenty-four down and twelve up: cutting is what gain staging mostly asks
    for — a hot interface, a hot pack, a boost feeding a preamp — and twelve decibels of boost is
    already more than a capture wants before it stops being the device that was captured. */
inline constexpr float blockTrimMinDb = -24.0f;
inline constexpr float blockTrimMaxDb =  12.0f;

/** Where a captured device likes to be fed, in dBFS peak — the green zone on the IN meter.
    A CONVENTION, not a measurement: `FileEntry::inputDb` is the alias attenuation between two
    captures of one device, not the absolute level the hardware was driven at, so there is nothing
    in the pack to read. This is the window a guitar DI normally peaks in, and it is the honest
    default until the capture side writes down what it actually used. */
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
inline constexpr int preampNumMeasured = 5;
inline juce::String preampMeasured (int i) { return "preamp_meas" + juce::String (i + 1); }

/** The EQ, one per captured block and BELONGING to it — index 0 is the boost's, index 1 the
    preamp's. Each sits after its block's nonlinearity, which is what makes it a colour control
    rather than a distortion-character one, and each goes dark with its block's switch.

    Their places in the chain are not arbitrary: the boost's lands in front of the preamp and the
    preamp's in front of the power amp, which is where a real amplifier keeps its tone stack. So
    there is no PRE/POST switch — every nonlinearity downstream is already being fed by one.

    THERE IS NO ENABLE. The block's switch is the EQ's switch, and a flat link is bit-transparent
    anyway — `core::EqLink` skips a band sitting at exactly 0 dB — so "off" and "flat" were the same
    signal by two names, with Flat already in the presets menu. What the second name actually bought
    was a console you could turn while it was not in the circuit, which is the worst kind of hidden
    state and is now unrepresentable.

    The console grammar: HPF/LPF with a slope choice, LO/HI shelves with free corners (gain + freq,
    no Q), bells B1/B2 (gain + freq + Q) and the narrow switchable B3 — the surgical slot. Bells are
    indexed 0..2 for the id helpers. */
inline constexpr int numEqLinks = 2;

inline juce::String eqId (int l, const char* leaf)
{
    return juce::String (l == 0 ? boostId : preampId) + "_eq_" + leaf;
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

inline constexpr float eqLevelRangeDb = 12.0f;

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
/** How many channels the chain works, and from where. MONO: one signal end to end, the copy to the
    other channel after everything — the truth of a guitar chain, and one neural pass. STEREO: two
    takes on one bus, each through its own amp, everything twice. STEREO SPACE: mono where the
    sound is made — boost, preamp, their consoles, one neural pass — and stereo from the reverb on,
    where the space is: the reverb spreads one signal into two, and the power amp, the cabinet and
    the limiter follow it in stereo. */
inline constexpr const char* stereoMode = "stereo_mode";
inline const juce::StringArray stereoModes { "Mono", "Stereo", "Stereo Space" };
enum class StereoMode { mono, stereo, stereoSpace };

/** TEMPORARY — the audition loops, in the order the player offers them. Default is the first. */
inline const juce::StringArray demoLoops { "GGG", "Eleven Light Years", "Cats Hard Day",
                                           "Deep Space Is My Home", "Fifth Dimension" };

/** Oversampling for the nonlinear stages. Not a per-preset taste: it trades CPU for alias-free
    saturation, and which trade you want depends on the machine you are on. Lives in the footer with
    the other facts about the run. */

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
