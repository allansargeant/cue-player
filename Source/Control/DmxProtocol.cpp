#include "Control/DmxProtocol.h"

#include <cstring>

namespace cp
{

namespace dmx
{
    juce::String sacnMulticastAddress (int universe)
    {
        const auto u = juce::jlimit (1, 63999, universe);
        return "239.255." + juce::String ((u >> 8) & 0xff) + "." + juce::String (u & 0xff);
    }
}

namespace
{
    juce::uint16 readBigEndian16 (const juce::uint8* p) noexcept
    {
        return (juce::uint16) ((p[0] << 8) | p[1]);
    }
}

//==============================================================================
bool parseArtNetPacket (const void* data, int numBytes, DmxFrame& result)
{
    const auto* bytes = static_cast<const juce::uint8*> (data);

    // Header is 18 bytes; a packet with no slots at all is not useful to us.
    if (bytes == nullptr || numBytes < 19)
        return false;

    if (std::memcmp (bytes, "Art-Net\0", 8) != 0)
        return false;

    // OpCode is little-endian, unlike everything else in the packet. ArtDmx is 0x5000.
    const auto opCode = (juce::uint16) (bytes[8] | (bytes[9] << 8));

    if (opCode != 0x5000)
        return false;

    // Protocol version is big-endian and must be at least 14.
    if (readBigEndian16 (bytes + 10) < 14)
        return false;

    // Universe is 15 bits: Net in byte 15, Sub-Net and Universe packed into byte 14.
    result.universe = ((bytes[15] & 0x7f) << 8) | bytes[14];

    const auto declaredLength = (int) readBigEndian16 (bytes + 16);
    const auto available = numBytes - 18;

    result.numSlots = juce::jlimit (0, 512, juce::jmin (declaredLength, available));

    if (result.numSlots <= 0)
        return false;

    std::memset (result.slots, 0, sizeof (result.slots));
    std::memcpy (result.slots, bytes + 18, (size_t) result.numSlots);
    return true;
}

//==============================================================================
bool parseSacnPacket (const void* data, int numBytes, DmxFrame& result)
{
    const auto* bytes = static_cast<const juce::uint8*> (data);

    // Root (38) + framing (77) + DMP (11) headers, then at least the start code.
    if (bytes == nullptr || numBytes < 126)
        return false;

    // --- Root layer ----------------------------------------------------------
    if (readBigEndian16 (bytes) != 0x0010)                 // Preamble size
        return false;

    if (readBigEndian16 (bytes + 2) != 0x0000)             // Post-amble size
        return false;

    static const juce::uint8 acnIdentifier[12] =
        { 'A', 'S', 'C', '-', 'E', '1', '.', '1', '7', 0x00, 0x00, 0x00 };

    if (std::memcmp (bytes + 4, acnIdentifier, sizeof (acnIdentifier)) != 0)
        return false;

    // Root vector: 0x00000004 is E1.31 data. Anything else is discovery or extended.
    if (juce::ByteOrder::bigEndianInt (bytes + 18) != 0x00000004)
        return false;

    // --- Framing layer -------------------------------------------------------
    // Framing vector: 0x00000002 is a data packet. 0x00000001 is sync, which carries no
    // levels of its own.
    if (juce::ByteOrder::bigEndianInt (bytes + 40) != 0x00000002)
        return false;

    // Options bit 7 marks a preview stream, which by spec must not drive real output.
    // Honouring it means a designer's blind cue cannot fire a sound cue by accident.
    if ((bytes[112] & 0x80) != 0)
        return false;

    // Bit 6 is the stream-terminated flag: the source is going away, not sending levels.
    if ((bytes[112] & 0x40) != 0)
        return false;

    result.universe = (int) readBigEndian16 (bytes + 113);

    // --- DMP layer -----------------------------------------------------------
    if (bytes[117] != 0x02)                                // DMP vector: set property
        return false;

    if (bytes[118] != 0xa1)                                // Address and data type
        return false;

    const auto propertyCount = (int) readBigEndian16 (bytes + 123);

    // The count includes the start code byte, which is not a DMX slot.
    if (propertyCount < 1)
        return false;

    if (bytes[125] != 0x00)                                // DMX start code
        return false;

    const auto available = numBytes - 126;
    result.numSlots = juce::jlimit (0, 512, juce::jmin (propertyCount - 1, available));

    if (result.numSlots <= 0)
        return false;

    std::memset (result.slots, 0, sizeof (result.slots));
    std::memcpy (result.slots, bytes + 126, (size_t) result.numSlots);
    return true;
}

} // namespace cp
