#include "Model/ControlMessage.h"

namespace cp
{

juce::String toString (ControlMessageType t)
{
    switch (t)
    {
        case ControlMessageType::osc:                return "osc";
        case ControlMessageType::midiNoteOn:         return "midiNoteOn";
        case ControlMessageType::midiNoteOff:        return "midiNoteOff";
        case ControlMessageType::midiControlChange:  return "midiControlChange";
        case ControlMessageType::midiProgramChange:  return "midiProgramChange";
        case ControlMessageType::midiShowControl:    return "midiShowControl";
        case ControlMessageType::midiMachineControl: return "midiMachineControl";
    }

    return "osc";
}

ControlMessageType controlMessageTypeFromString (const juce::String& s)
{
    if (s == "midiNoteOn")         return ControlMessageType::midiNoteOn;
    if (s == "midiNoteOff")        return ControlMessageType::midiNoteOff;
    if (s == "midiControlChange")  return ControlMessageType::midiControlChange;
    if (s == "midiProgramChange")  return ControlMessageType::midiProgramChange;
    if (s == "midiShowControl")    return ControlMessageType::midiShowControl;
    if (s == "midiMachineControl") return ControlMessageType::midiMachineControl;

    return ControlMessageType::osc;
}

juce::StringArray controlMessageTypeNames()
{
    return { "OSC", "MIDI note on", "MIDI note off", "MIDI CC",
             "MIDI program change", "MIDI Show Control", "MIDI Machine Control" };
}

namespace msc
{
    juce::StringArray formatNames()
    {
        return { "Lighting", "Sound", "Machinery", "Video", "Projection", "All types" };
    }

    juce::Array<juce::uint8> formatValues()
    {
        return { formatLighting, formatSound, formatMachinery,
                 formatVideo, formatProjection, formatAllTypes };
    }

    juce::StringArray commandNames()
    {
        return { "Go", "Stop", "Resume", "Load", "All off", "Go off" };
    }

    juce::Array<juce::uint8> commandValues()
    {
        return { commandGo, commandStop, commandResume, commandLoad, commandAllOff, commandGoOff };
    }
}

namespace mmc
{
    juce::StringArray commandNames()
    {
        return { "Stop", "Play", "Deferred play", "Pause" };
    }

    juce::Array<juce::uint8> commandValues()
    {
        return { commandStop, commandPlay, commandDeferredPlay, commandPause };
    }
}

//==============================================================================
juce::String ControlMessage::describe() const
{
    const auto prefix = delay > 0.0 ? "+" + juce::String (delay, 2) + "s  " : juce::String();

    switch (type)
    {
        case ControlMessageType::osc:
            return prefix + oscAddress + (oscArguments.isNotEmpty() ? " " + oscArguments : juce::String())
                 + (oscTarget.isNotEmpty() ? "  -> " + oscTarget : juce::String());

        case ControlMessageType::midiNoteOn:
            return prefix + "Note on  ch" + juce::String (midiChannel)
                 + "  " + juce::MidiMessage::getMidiNoteName (midiData1, true, true, 4)
                 + "  vel " + juce::String (midiData2);

        case ControlMessageType::midiNoteOff:
            return prefix + "Note off  ch" + juce::String (midiChannel)
                 + "  " + juce::MidiMessage::getMidiNoteName (midiData1, true, true, 4);

        case ControlMessageType::midiControlChange:
            return prefix + "CC " + juce::String (midiData1) + " = " + juce::String (midiData2)
                 + "  ch" + juce::String (midiChannel);

        case ControlMessageType::midiProgramChange:
            return prefix + "Program " + juce::String (midiData1) + "  ch" + juce::String (midiChannel);

        case ControlMessageType::midiShowControl:
        {
            const auto formats = msc::formatValues();
            const auto commands = msc::commandValues();
            const auto formatIndex = formats.indexOf (mscCommandFormat);
            const auto commandIndex = commands.indexOf (mscCommand);

            return prefix + "MSC  dev " + juce::String (mscDeviceID) + "  "
                 + (formatIndex >= 0 ? msc::formatNames()[formatIndex] : juce::String ("format ?"))
                 + "  " + (commandIndex >= 0 ? msc::commandNames()[commandIndex] : juce::String ("cmd ?"))
                 + (mscCueNumber.isNotEmpty() ? "  cue " + mscCueNumber : juce::String());
        }

        case ControlMessageType::midiMachineControl:
        {
            const auto commands = mmc::commandValues();
            const auto index = commands.indexOf (mmcCommand);
            return prefix + "MMC  " + (index >= 0 ? mmc::commandNames()[index] : juce::String ("?"));
        }
    }

    return {};
}

juce::var ControlMessage::toVar() const
{
    auto* o = new juce::DynamicObject();

    o->setProperty ("type",  toString (type));
    o->setProperty ("delay", delay);

    o->setProperty ("oscTarget",    oscTarget);
    o->setProperty ("oscAddress",   oscAddress);
    o->setProperty ("oscArguments", oscArguments);

    o->setProperty ("midiTarget",  midiTarget);
    o->setProperty ("midiChannel", midiChannel);
    o->setProperty ("midiData1",   midiData1);
    o->setProperty ("midiData2",   midiData2);

    o->setProperty ("mscDeviceID",      mscDeviceID);
    o->setProperty ("mscCommandFormat", (int) mscCommandFormat);
    o->setProperty ("mscCommand",       (int) mscCommand);
    o->setProperty ("mscCueNumber",     mscCueNumber);
    o->setProperty ("mscCueList",       mscCueList);
    o->setProperty ("mmcCommand",       (int) mmcCommand);

    return juce::var (o);
}

ControlMessage ControlMessage::fromVar (const juce::var& v)
{
    ControlMessage m;

    if (! v.isObject())
        return m;

    m.type  = controlMessageTypeFromString (v.getProperty ("type", {}).toString());
    m.delay = juce::jmax (0.0, (double) v.getProperty ("delay", 0.0));

    m.oscTarget    = v.getProperty ("oscTarget", {}).toString();
    m.oscAddress   = v.getProperty ("oscAddress", "/cue/1/go").toString();
    m.oscArguments = v.getProperty ("oscArguments", {}).toString();

    m.midiTarget  = v.getProperty ("midiTarget", {}).toString();
    m.midiChannel = juce::jlimit (1, 16,  (int) v.getProperty ("midiChannel", 1));
    m.midiData1   = juce::jlimit (0, 127, (int) v.getProperty ("midiData1", 60));
    m.midiData2   = juce::jlimit (0, 127, (int) v.getProperty ("midiData2", 127));

    m.mscDeviceID      = juce::jlimit (0, 127, (int) v.getProperty ("mscDeviceID", 0));
    m.mscCommandFormat = (juce::uint8) juce::jlimit (0, 127, (int) v.getProperty ("mscCommandFormat", (int) msc::formatLighting));
    m.mscCommand       = (juce::uint8) juce::jlimit (0, 127, (int) v.getProperty ("mscCommand", (int) msc::commandGo));
    m.mscCueNumber     = v.getProperty ("mscCueNumber", {}).toString();
    m.mscCueList       = v.getProperty ("mscCueList", {}).toString();
    m.mmcCommand       = (juce::uint8) juce::jlimit (0, 127, (int) v.getProperty ("mmcCommand", (int) mmc::commandPlay));

    return m;
}

//==============================================================================
namespace
{
    /** MSC carries cue numbers as plain ASCII digits and dots. Anything else would make the
        receiving desk reject the whole message, so strip it rather than send a bad frame. */
    void appendAsciiField (juce::Array<juce::uint8>& bytes, const juce::String& text)
    {
        for (auto c : text)
            if ((c >= '0' && c <= '9') || c == '.')
                bytes.add ((juce::uint8) c);
    }
}

juce::MidiMessage buildShowControlMessage (const ControlMessage& message)
{
    if (message.type != ControlMessageType::midiShowControl)
        return {};

    juce::Array<juce::uint8> body;
    body.add (0x7f);                                                  // Universal real-time
    body.add ((juce::uint8) juce::jlimit (0, 127, message.mscDeviceID));
    body.add (0x02);                                                  // MSC sub-id
    body.add (message.mscCommandFormat);
    body.add (message.mscCommand);

    if (message.mscCueNumber.isNotEmpty())
    {
        appendAsciiField (body, message.mscCueNumber);

        if (message.mscCueList.isNotEmpty())
        {
            body.add (0x00);
            appendAsciiField (body, message.mscCueList);
        }
    }

    return juce::MidiMessage::createSysExMessage (body.getRawDataPointer(), body.size());
}

juce::MidiMessage buildMachineControlMessage (const ControlMessage& message)
{
    if (message.type != ControlMessageType::midiMachineControl)
        return {};

    const juce::uint8 body[] = { 0x7f,
                                 (juce::uint8) juce::jlimit (0, 127, message.mscDeviceID),
                                 0x06,                                // MMC command sub-id
                                 message.mmcCommand };

    return juce::MidiMessage::createSysExMessage (body, (int) juce::numElementsInArray (body));
}

bool parseShowControlMessage (const juce::MidiMessage& message, IncomingMsc& result)
{
    if (! message.isSysEx())
        return false;

    const auto* data = message.getSysExData();
    const auto size  = message.getSysExDataSize();

    // getSysExData() omits the leading F0 and trailing F7, so the body starts at 0x7f.
    if (data == nullptr || size < 5 || data[0] != 0x7f || data[2] != 0x02)
        return false;

    result = {};
    result.deviceID      = data[1];
    result.commandFormat = data[3];
    result.command       = data[4];

    // Cue number, then optionally cue list, separated by 0x00.
    juce::String* field = &result.cueNumber;

    for (int i = 5; i < size; ++i)
    {
        if (data[i] == 0x00)
        {
            if (field == &result.cueNumber)
                field = &result.cueList;
            else
                break;      // Cue path follows; we do not use it.

            continue;
        }

        *field += juce::String::charToString ((juce::juce_wchar) data[i]);
    }

    return true;
}

bool parseMachineControlMessage (const juce::MidiMessage& message, int& deviceID, juce::uint8& command)
{
    if (! message.isSysEx())
        return false;

    const auto* data = message.getSysExData();
    const auto size  = message.getSysExDataSize();

    if (data == nullptr || size < 4 || data[0] != 0x7f || data[2] != 0x06)
        return false;

    deviceID = data[1];
    command  = data[3];
    return true;
}

//==============================================================================
juce::Array<juce::var> parseOscArguments (const juce::String& text)
{
    juce::Array<juce::var> args;
    auto remaining = text.trim();

    while (remaining.isNotEmpty())
    {
        juce::String token;

        if (remaining.startsWithChar ('"'))
        {
            // Quoted: everything up to the closing quote stays one string argument, even
            // if it looks like a number or contains spaces.
            const auto closing = remaining.indexOfChar (1, '"');

            if (closing < 0)
            {
                args.add (remaining.substring (1));
                break;
            }

            args.add (remaining.substring (1, closing));
            remaining = remaining.substring (closing + 1).trimStart();
            continue;
        }

        const auto space = remaining.indexOfChar (' ');
        token     = space < 0 ? remaining : remaining.substring (0, space);
        remaining = space < 0 ? juce::String() : remaining.substring (space + 1).trimStart();

        if (token.isEmpty())
            continue;

        if (token.containsOnly ("0123456789+-") && token.containsAnyOf ("0123456789"))
            args.add (token.getIntValue());
        else if (token.containsOnly ("0123456789+-.eE") && token.containsAnyOf ("0123456789"))
            args.add ((float) token.getDoubleValue());
        else
            args.add (token);
    }

    return args;
}

} // namespace cp
