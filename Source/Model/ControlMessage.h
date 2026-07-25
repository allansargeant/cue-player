#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

namespace cp
{

/** A message a cue sends out to other gear when it fires.

    Every cue can carry a list of these, so a sound cue can also fly a lighting cue or a
    projector without needing a separate control cue sitting next to it in the list. A cue
    with no audio and only messages is just CueType::control.
*/
enum class ControlMessageType
{
    osc = 0,          ///< An OSC message to one of the configured targets.
    midiNoteOn,
    midiNoteOff,
    midiControlChange,
    midiProgramChange,
    midiShowControl,  ///< MSC over sysex — the usual way to talk to a lighting desk.
    midiMachineControl
};

juce::String toString (ControlMessageType);
ControlMessageType controlMessageTypeFromString (const juce::String&);
juce::StringArray controlMessageTypeNames();

/** MSC command formats, per MIDI Show Control 1.1. Only the ones a cue player realistically
    addresses are listed; the raw byte is kept so anything else still round-trips. */
namespace msc
{
    static constexpr juce::uint8 formatLighting  = 0x01;
    static constexpr juce::uint8 formatSound     = 0x10;
    static constexpr juce::uint8 formatMachinery = 0x20;
    static constexpr juce::uint8 formatVideo     = 0x30;
    static constexpr juce::uint8 formatProjection = 0x40;
    static constexpr juce::uint8 formatAllTypes  = 0x7f;

    static constexpr juce::uint8 commandGo       = 0x01;
    static constexpr juce::uint8 commandStop     = 0x02;
    static constexpr juce::uint8 commandResume   = 0x03;
    static constexpr juce::uint8 commandLoad     = 0x05;
    static constexpr juce::uint8 commandAllOff   = 0x08;
    static constexpr juce::uint8 commandGoOff    = 0x0b;

    juce::StringArray formatNames();
    juce::Array<juce::uint8> formatValues();
    juce::StringArray commandNames();
    juce::Array<juce::uint8> commandValues();
}

/** MMC commands, per the MIDI Machine Control spec. */
namespace mmc
{
    static constexpr juce::uint8 commandStop          = 0x01;
    static constexpr juce::uint8 commandPlay          = 0x02;
    static constexpr juce::uint8 commandDeferredPlay  = 0x03;
    static constexpr juce::uint8 commandPause         = 0x09;

    juce::StringArray commandNames();
    juce::Array<juce::uint8> commandValues();
}

//==============================================================================
struct ControlMessage
{
    ControlMessageType type { ControlMessageType::osc };

    /** Seconds after the cue begins sounding. Pre-wait is already accounted for, so a
        message with no delay lands with the first sample of audio. */
    double delay { 0.0 };

    //== OSC ===================================================================
    /** Name of a configured OSC target. Empty means "every configured target". */
    juce::String oscTarget;
    juce::String oscAddress { "/cue/1/go" };

    /** Whitespace-separated arguments. Anything that parses as a number is sent as one
        (int if it has no decimal point, float otherwise); everything else is sent as a
        string. Wrap a token in quotes to force it to stay a string. */
    juce::String oscArguments;

    //== MIDI ==================================================================
    /** Name of a configured MIDI output device. Empty means "every enabled output". */
    juce::String midiTarget;
    int midiChannel { 1 };      ///< 1-16
    int midiData1 { 60 };       ///< Note number, controller number, or program number.
    int midiData2 { 127 };      ///< Velocity or controller value.

    //== MSC / MMC =============================================================
    int          mscDeviceID { 0 };          ///< 0-111 individual, 112-126 group, 127 all-call.
    juce::uint8  mscCommandFormat { msc::formatLighting };
    juce::uint8  mscCommand { msc::commandGo };
    juce::String mscCueNumber;               ///< ASCII, e.g. "12.5". Empty is allowed.
    juce::String mscCueList;
    juce::uint8  mmcCommand { mmc::commandPlay };

    /** One-line summary for the inspector list. */
    juce::String describe() const;

    juce::var toVar() const;
    static ControlMessage fromVar (const juce::var&);
};

//==============================================================================
/** Encodes @p message as a MIDI Show Control sysex message.
    Returns an invalid (empty) message if the type is not midiShowControl. */
juce::MidiMessage buildShowControlMessage (const ControlMessage& message);

/** Encodes @p message as a MIDI Machine Control sysex message. */
juce::MidiMessage buildMachineControlMessage (const ControlMessage& message);

/** A decoded incoming MSC message. */
struct IncomingMsc
{
    int          deviceID { 0 };
    juce::uint8  commandFormat { 0 };
    juce::uint8  command { 0 };
    juce::String cueNumber;
    juce::String cueList;
};

/** Decodes an MSC sysex message. Returns false if @p message is not valid MSC. */
bool parseShowControlMessage (const juce::MidiMessage& message, IncomingMsc& result);

/** Decodes an MMC sysex message into its command byte. Returns false if not valid MMC. */
bool parseMachineControlMessage (const juce::MidiMessage& message, int& deviceID, juce::uint8& command);

/** Splits an OSC argument string into typed values, honouring quoted strings. */
juce::Array<juce::var> parseOscArguments (const juce::String& text);

} // namespace cp
