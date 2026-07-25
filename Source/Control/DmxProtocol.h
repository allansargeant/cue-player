#pragma once

#include <juce_core/juce_core.h>

namespace cp
{

/** One received DMX universe frame, however it arrived. */
struct DmxFrame
{
    int universe { 0 };
    int numSlots { 0 };                     ///< Slots actually present, 1-512.
    juce::uint8 slots[512] {};              ///< Slot 1 is at index 0, as the desk numbers it.

    /** Level of DMX slot @p address (1-based), or 0 when the frame is too short. */
    juce::uint8 levelAt (int address) const noexcept
    {
        const auto index = address - 1;
        return juce::isPositiveAndBelow (index, numSlots) ? slots[index] : (juce::uint8) 0;
    }
};

/** Default ports and addresses for the two protocols. */
namespace dmx
{
    static constexpr int artNetPort = 6454;
    static constexpr int sacnPort   = 5568;

    /** Multicast group sACN uses for @p universe: 239.255.<hi>.<lo>. */
    juce::String sacnMulticastAddress (int universe);
}

/** Decodes an Art-Net ArtDmx packet.

    Returns false for anything else on the port — ArtPoll, ArtSync and the rest of the
    opcode space are simply not our business here.
*/
bool parseArtNetPacket (const void* data, int numBytes, DmxFrame& result);

/** Decodes an E1.31 (sACN) data packet.

    Returns false for sync and discovery packets, for previews (which are explicitly not
    meant to drive anything live), and for anything that fails its layer checks.
*/
bool parseSacnPacket (const void* data, int numBytes, DmxFrame& result);

} // namespace cp
