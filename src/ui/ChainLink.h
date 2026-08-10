#pragma once

namespace orbitamp
{

/** The chain, in signal order. The strip draws it as the map; the faceplate opens one link of it
    full width. One enum so the two can never disagree about what the chain is.

    The tuner is drawn where it LISTENS — the raw input, ahead of everything — because a map that
    drew it anywhere else would lie about what the needle hears. */
enum class ChainLink { tuner, eq1, boost, eq2, preamp, reverb, power, cab };

inline constexpr int numChainLinks = 8;

/** The strip lane a link deserves: the service links — the tuner, soon the gate — take half of
    what a tone link takes, which is the map saying "this is plumbing, not voice". */
inline constexpr float chainLinkWeight (ChainLink l)
{
    return l == ChainLink::tuner ? 0.5f : 1.0f;
}

} // namespace orbitamp
