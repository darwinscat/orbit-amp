#pragma once

namespace orbitamp
{

/** The chain, in signal order. The strip draws it as the map; the faceplate opens one link of it
    full width. One enum so the two can never disagree about what the chain is.

    The tuner is drawn where it LISTENS — the raw input, ahead of everything — because a map that
    drew it anywhere else would lie about what the needle hears. The gate follows it, not the other
    way round: a gate ahead of the tuner would blind the needle on a decaying note, which is
    exactly when you tune. */
enum class ChainLink { tuner, gate, eq1, boost, eq2, preamp, reverb, power, cab };

inline constexpr int numChainLinks = 9;

/** The strip lane a link deserves: the service links — the tuner and the gate — take half of what
    a tone link takes, which is the map saying "this is plumbing, not voice". */
inline constexpr float chainLinkWeight (ChainLink l)
{
    return l == ChainLink::tuner || l == ChainLink::gate ? 0.5f : 1.0f;
}

} // namespace orbitamp
