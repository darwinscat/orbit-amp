#pragma once

namespace orbitamp
{

/** The chain, in signal order. The strip draws it as the map; the faceplate opens one link of it
    full width. One enum so the two can never disagree about what the chain is. */
enum class ChainLink { eq1, boost, eq2, preamp, reverb, power, cab };

inline constexpr int numChainLinks = 7;

} // namespace orbitamp
