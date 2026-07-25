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

inline constexpr const char* preampType  = "preamp_type";
inline constexpr const char* preampVoice = "preamp_voice";
inline constexpr const char* preampGain  = "preamp_gain";

inline constexpr const char* eqLow    = "eq_low";
inline constexpr const char* eqMid    = "eq_mid";
inline constexpr const char* eqHigh   = "eq_high";
inline constexpr const char* eqHpfOn  = "eq_hpf_on";
inline constexpr const char* eqHpfHz  = "eq_hpf_hz";
inline constexpr const char* eqLpfOn  = "eq_lpf_on";
inline constexpr const char* eqLpfHz  = "eq_lpf_hz";

inline constexpr const char* reverbType = "reverb_type";
inline constexpr const char* reverbMix  = "reverb_mix";

/** Reverb characters, in the order of the design's simple case. Size and damping follow from the
    character rather than being loose knobs — the design calls for Mix only. */
inline const juce::StringArray reverbCharacters { "Room", "Hall", "Plate", "Spring" };

/** Tone range. +-12 dB is an amp tone control, not a mastering EQ — a wider range would invite
    corrective moves the block is not for. */
inline constexpr float toneRangeDb = 12.0f;

/** The voicing types, in the order the design lists them — clean through modern is a ramp of
    increasing gain, so the order is meaningful, not alphabetical. */
inline const juce::StringArray typeNames { "Clean", "Edge", "Crunch", "High-gain", "Modern" };

/** How many voices a type can hold. The voice parameter is an INDEX, not a choice, because the
    names differ per type and a host-visible choice list has to be fixed at construction. */
inline constexpr int maxVoicesPerType = 8;

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

} // namespace orbitamp::params
