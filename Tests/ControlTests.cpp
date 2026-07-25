/*
    Tests for the control layer: the wire formats, the mappings they resolve to, and the
    edge detection that sits between a lighting desk and a GO.

    Everything here is exercised through the real production functions. The packet builders
    below construct genuine Art-Net and sACN frames byte by byte rather than reusing the
    parser's own idea of the layout, so a mistake in the parser cannot cancel out against a
    matching mistake in the test.
*/

#include "TestHarness.h"

#include "Control/ControlSettings.h"
#include "Control/DmxControl.h"
#include "Control/MidiControl.h"
#include "Control/OscControl.h"
#include "Model/Cue.h"
#include "Model/ControlMessage.h"

#include <cstring>
#include <vector>

using namespace cp;
using cptest::check;
using cptest::checkNear;
using cptest::checkEqual;

namespace
{

//==============================================================================
std::vector<juce::uint8> buildArtNetPacket (int universe, const std::vector<juce::uint8>& slots)
{
    std::vector<juce::uint8> packet (18, 0);

    std::memcpy (packet.data(), "Art-Net\0", 8);
    packet[8]  = 0x00;                       // OpCode low  (little-endian 0x5000)
    packet[9]  = 0x50;                       // OpCode high
    packet[10] = 0x00;                       // Protocol version high
    packet[11] = 14;                         // Protocol version low
    packet[12] = 0;                          // Sequence
    packet[13] = 0;                          // Physical
    packet[14] = (juce::uint8) (universe & 0xff);          // Sub-net + universe
    packet[15] = (juce::uint8) ((universe >> 8) & 0x7f);   // Net
    packet[16] = (juce::uint8) ((slots.size() >> 8) & 0xff);
    packet[17] = (juce::uint8) (slots.size() & 0xff);

    packet.insert (packet.end(), slots.begin(), slots.end());
    return packet;
}

std::vector<juce::uint8> buildSacnPacket (int universe, const std::vector<juce::uint8>& slots,
                                          juce::uint8 options = 0)
{
    std::vector<juce::uint8> packet (126, 0);

    packet[0] = 0x00; packet[1] = 0x10;      // Preamble size
    packet[2] = 0x00; packet[3] = 0x00;      // Post-amble size
    std::memcpy (packet.data() + 4, "ASC-E1.17\0\0\0", 12);

    // Root vector: E1.31 data.
    packet[18] = 0x00; packet[19] = 0x00; packet[20] = 0x00; packet[21] = 0x04;

    // Framing vector: data packet.
    packet[40] = 0x00; packet[41] = 0x00; packet[42] = 0x00; packet[43] = 0x02;

    packet[108] = 100;                       // Priority
    packet[111] = 0;                         // Sequence
    packet[112] = options;
    packet[113] = (juce::uint8) ((universe >> 8) & 0xff);
    packet[114] = (juce::uint8) (universe & 0xff);

    packet[117] = 0x02;                      // DMP vector
    packet[118] = 0xa1;                      // Address and data type
    packet[121] = 0x00; packet[122] = 0x01;  // Address increment

    const auto propertyCount = slots.size() + 1;   // Start code counts as a property
    packet[123] = (juce::uint8) ((propertyCount >> 8) & 0xff);
    packet[124] = (juce::uint8) (propertyCount & 0xff);
    packet[125] = 0x00;                      // DMX start code

    packet.insert (packet.end(), slots.begin(), slots.end());
    return packet;
}

/** A 512-slot frame with everything at zero except the addresses given. */
std::vector<juce::uint8> slotsWith (const std::map<int, juce::uint8>& levels)
{
    std::vector<juce::uint8> slots (512, 0);

    for (const auto& [address, level] : levels)
        if (juce::isPositiveAndBelow (address - 1, 512))
            slots[(size_t) (address - 1)] = level;

    return slots;
}

DmxFrame frameWith (int universe, const std::map<int, juce::uint8>& levels)
{
    DmxFrame frame;
    frame.universe = universe;
    frame.numSlots = 512;

    const auto slots = slotsWith (levels);
    std::memcpy (frame.slots, slots.data(), slots.size());
    return frame;
}

bool containsAction (const std::vector<ControlAction>& actions, ControlActionType type)
{
    for (const auto& a : actions)
        if (a.type == type)
            return true;

    return false;
}

const ControlAction* findAction (const std::vector<ControlAction>& actions, ControlActionType type)
{
    for (const auto& a : actions)
        if (a.type == type)
            return &a;

    return nullptr;
}

//==============================================================================
void testOscAddresses()
{
    cptest::section ("OSC address scheme");

    const juce::Array<juce::var> none;

    check (OscControl::actionForAddress ("/go", none).type == ControlActionType::go,
           "/go fires the standby cue");
    check (OscControl::actionForAddress ("/GO", none).type == ControlActionType::go,
           "addresses are matched case-insensitively");
    check (OscControl::actionForAddress ("/go/", none).type == ControlActionType::go,
           "a trailing slash is tolerated");
    check (OscControl::actionForAddress ("/cue/go", none).type == ControlActionType::go,
           "/cue/go is a synonym for /go");
    check (OscControl::actionForAddress ("/panic", none).type == ControlActionType::panic,
           "/panic maps to panic");
    check (OscControl::actionForAddress ("/pause", none).type == ControlActionType::pause,
           "/pause maps to pause");
    check (OscControl::actionForAddress ("/resume", none).type == ControlActionType::resume,
           "/resume maps to resume");
    check (OscControl::actionForAddress ("/pause/toggle", none).type == ControlActionType::pauseToggle,
           "/pause/toggle maps to a toggle");
    check (OscControl::actionForAddress ("/releasevamp", none).type == ControlActionType::releaseVamp,
           "/releasevamp releases every vamp");
    check (OscControl::actionForAddress ("/standby/next", none).type == ControlActionType::standbyNext,
           "/standby/next steps forward");
    check (OscControl::actionForAddress ("/standby/previous", none).type == ControlActionType::standbyPrevious,
           "/standby/previous steps back");

    {
        const auto action = OscControl::actionForAddress ("/cue/12.5/go", none);
        check (action.type == ControlActionType::goCue, "/cue/<n>/go fires a specific cue");
        checkEqual (action.cueNumber, "12.5", "the cue number is taken from the address");
    }

    {
        const auto action = OscControl::actionForAddress ("/cue/7/stop", { 4.5 });
        check (action.type == ControlActionType::stopCue, "/cue/<n>/stop stops one cue");
        checkEqual (action.cueNumber, "7", "cue number parsed for a stop");
        checkNear (action.value, 4.5, 1.0e-9, "the fade time comes from the first argument");
    }

    {
        // No argument at all must not become a 0-second fade by accident.
        const auto action = OscControl::actionForAddress ("/stop", none);
        check (action.type == ControlActionType::stopAll, "/stop stops everything");
        checkNear (action.value, 2.0, 1.0e-9, "stop-all defaults to a two-second fade");
    }

    {
        const auto action = OscControl::actionForAddress ("/stop", { 0 });
        checkNear (action.value, 0.0, 1.0e-9, "an explicit zero fade is honoured, not defaulted");
    }

    {
        const auto action = OscControl::actionForAddress ("/master/level", { -6.0 });
        check (action.type == ControlActionType::masterLevel, "/master/level sets the master");
        checkNear (action.value, -6.0, 1.0e-9, "the level comes from the argument");
    }

    {
        const auto action = OscControl::actionForAddress ("/standby/3", none);
        check (action.type == ControlActionType::standbyCue, "/standby/<n> stands by a cue");
        checkEqual (action.cueNumber, "3", "standby cue number parsed");
    }

    check (! OscControl::actionForAddress ("/something/else", none).isValid(),
           "an unknown address produces no action");
    check (! OscControl::actionForAddress ("/cue/12/frobnicate", none).isValid(),
           "an unknown cue verb produces no action");
    check (! OscControl::actionForAddress ("", none).isValid(),
           "an empty address produces no action");
}

void testOscArguments()
{
    cptest::section ("OSC argument parsing");

    {
        const auto args = parseOscArguments ("1 2.5 hello");
        check (args.size() == 3, "three arguments parsed");
        check (args[0].isInt(), "a whole number becomes an int");
        check (args[1].isDouble(), "a decimal becomes a float");
        check (args[2].isString(), "a word becomes a string");
        checkNear ((double) args[1], 2.5, 1.0e-6, "the float value is right");
    }

    {
        const auto args = parseOscArguments ("\"two words\" 3");
        check (args.size() == 2, "a quoted string counts as one argument");
        checkEqual (args[0].toString(), "two words", "quotes hold a space-containing string together");
        check (args[1].isInt(), "parsing continues after the closing quote");
    }

    {
        // Without quoting, "42" would be sent as an int; a receiver expecting a string
        // needs a way to force it.
        const auto args = parseOscArguments ("\"42\"");
        check (args.size() == 1 && args[0].isString(), "quoting forces a number to stay a string");
    }

    {
        const auto args = parseOscArguments ("  -3   +7  ");
        check (args.size() == 2, "extra whitespace is ignored");
        check ((int) args[0] == -3 && (int) args[1] == 7, "signs are parsed");
    }

    check (parseOscArguments ("").isEmpty(), "an empty argument string yields nothing");
}

//==============================================================================
void testShowControl()
{
    cptest::section ("MIDI Show Control");

    ControlMessage message;
    message.type = ControlMessageType::midiShowControl;
    message.mscDeviceID = 5;
    message.mscCommandFormat = msc::formatLighting;
    message.mscCommand = msc::commandGo;
    message.mscCueNumber = "12.5";
    message.mscCueList = "3";

    const auto sysex = buildShowControlMessage (message);
    check (sysex.isSysEx(), "an MSC message is sysex");

    IncomingMsc decoded;
    check (parseShowControlMessage (sysex, decoded), "the message we built parses back");
    check (decoded.deviceID == 5, "device id round trips");
    check (decoded.commandFormat == msc::formatLighting, "command format round trips");
    check (decoded.command == msc::commandGo, "command round trips");
    checkEqual (decoded.cueNumber, "12.5", "cue number round trips");
    checkEqual (decoded.cueList, "3", "cue list round trips");

    // A note-on is not MSC, and must not be mistaken for one.
    check (! parseShowControlMessage (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), decoded),
           "a channel message is not mistaken for MSC");

    // MMC shares the 0x7f universal prefix but uses sub-id 0x06 instead of 0x02.
    ControlMessage mmcMessage;
    mmcMessage.type = ControlMessageType::midiMachineControl;
    mmcMessage.mmcCommand = mmc::commandPlay;
    check (! parseShowControlMessage (buildMachineControlMessage (mmcMessage), decoded),
           "MMC is not mistaken for MSC");

    cptest::section ("MSC to action mapping");

    ControlSettings settings;
    settings.mscDeviceID = 5;
    settings.mscRespondToSoundFormat = true;
    settings.mscRespondToAllTypesFormat = true;

    const auto mscFor = [] (int deviceID, juce::uint8 format, juce::uint8 command,
                            const juce::String& cueNumber)
    {
        IncomingMsc m;
        m.deviceID = deviceID;
        m.commandFormat = format;
        m.command = command;
        m.cueNumber = cueNumber;
        return m;
    };

    {
        const auto action = MidiControl::actionForShowControl (
            mscFor (5, msc::formatSound, msc::commandGo, "8"), settings);
        check (action.type == ControlActionType::goCue, "a sound-format GO fires a cue");
        checkEqual (action.cueNumber, "8", "the cue number carries through");
    }

    {
        const auto action = MidiControl::actionForShowControl (
            mscFor (5, msc::formatSound, msc::commandGo, ""), settings);
        check (action.type == ControlActionType::go,
               "a GO with no cue number fires the standby cue");
    }

    {
        // 127 is the all-call device id and must be honoured whatever our own id is.
        const auto action = MidiControl::actionForShowControl (
            mscFor (127, msc::formatSound, msc::commandGo, "2"), settings);
        check (action.type == ControlActionType::goCue, "an all-call GO is accepted");
    }

    {
        const auto action = MidiControl::actionForShowControl (
            mscFor (9, msc::formatSound, msc::commandGo, "2"), settings);
        check (! action.isValid(), "a GO addressed to another device id is ignored");
    }

    {
        // Lighting-format traffic is for the lighting desk, not for us.
        const auto action = MidiControl::actionForShowControl (
            mscFor (5, msc::formatLighting, msc::commandGo, "2"), settings);
        check (! action.isValid(), "a command format we do not answer is ignored");
    }

    {
        const auto action = MidiControl::actionForShowControl (
            mscFor (5, msc::formatAllTypes, msc::commandGo, "2"), settings);
        check (action.type == ControlActionType::goCue, "all-types format is answered");
    }

    {
        auto strict = settings;
        strict.mscRespondToAllTypesFormat = false;
        const auto action = MidiControl::actionForShowControl (
            mscFor (5, msc::formatAllTypes, msc::commandGo, "2"), strict);
        check (! action.isValid(), "all-types can be switched off");
    }

    {
        const auto action = MidiControl::actionForShowControl (
            mscFor (5, msc::formatSound, msc::commandAllOff, ""), settings);
        check (action.type == ControlActionType::stopAll, "ALL OFF stops everything");
        checkNear (action.value, 0.0, 1.0e-9, "ALL OFF does not fade");
    }

    {
        const auto action = MidiControl::actionForShowControl (
            mscFor (5, msc::formatSound, msc::commandResume, ""), settings);
        check (action.type == ControlActionType::resume, "RESUME resumes");
    }

    {
        auto listenToEverything = settings;
        listenToEverything.mscDeviceID = 127;
        const auto action = MidiControl::actionForShowControl (
            mscFor (42, msc::formatSound, msc::commandGo, "1"), listenToEverything);
        check (action.type == ControlActionType::goCue,
               "device id 127 in our settings listens to every device");
    }
}

void testMachineControl()
{
    cptest::section ("MIDI Machine Control");

    ControlMessage message;
    message.type = ControlMessageType::midiMachineControl;
    message.mscDeviceID = 12;
    message.mmcCommand = mmc::commandStop;

    int deviceID = 0;
    juce::uint8 command = 0;
    check (parseMachineControlMessage (buildMachineControlMessage (message), deviceID, command),
           "an MMC message we built parses back");
    check (deviceID == 12, "MMC device id round trips");
    check (command == mmc::commandStop, "MMC command round trips");

    check (MidiControl::actionForMachineControl (mmc::commandPlay).type == ControlActionType::go,
           "MMC play fires a GO");
    check (MidiControl::actionForMachineControl (mmc::commandDeferredPlay).type == ControlActionType::go,
           "MMC deferred play fires a GO");
    check (MidiControl::actionForMachineControl (mmc::commandStop).type == ControlActionType::stopAll,
           "MMC stop stops everything");
    check (MidiControl::actionForMachineControl (mmc::commandPause).type == ControlActionType::pauseToggle,
           "MMC pause toggles");
    check (! MidiControl::actionForMachineControl (0x7d).isValid(),
           "an MMC command we do not handle produces no action");
}

void testMidiBindings()
{
    cptest::section ("MIDI note / CC bindings");

    ControlSettings settings;

    MidiBinding noteBinding;
    noteBinding.kind = MidiTriggerKind::noteOn;
    noteBinding.channel = 1;
    noteBinding.number = 60;
    noteBinding.action = ControlActionType::go;
    settings.midiBindings.push_back (noteBinding);

    MidiBinding anyChannel;
    anyChannel.kind = MidiTriggerKind::noteOn;
    anyChannel.channel = 0;                    // any
    anyChannel.number = 62;
    anyChannel.action = ControlActionType::panic;
    settings.midiBindings.push_back (anyChannel);

    MidiBinding fader;
    fader.kind = MidiTriggerKind::controlChange;
    fader.channel = 1;
    fader.number = 7;
    fader.useValueAsLevel = true;
    fader.action = ControlActionType::masterLevel;
    settings.midiBindings.push_back (fader);

    check (MidiControl::actionForChannelMessage (
               juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), settings).type
           == ControlActionType::go,
           "a bound note fires its action");

    check (! MidiControl::actionForChannelMessage (
               juce::MidiMessage::noteOn (2, 60, (juce::uint8) 100), settings).isValid(),
           "the same note on another channel does not fire");

    check (MidiControl::actionForChannelMessage (
               juce::MidiMessage::noteOn (9, 62, (juce::uint8) 100), settings).type
           == ControlActionType::panic,
           "a channel-0 binding matches any channel");

    // A note-on at velocity 0 is a note-off. Treating it as a trigger would fire every cue
    // twice on any controller that releases that way.
    check (! MidiControl::actionForChannelMessage (
               juce::MidiMessage::noteOn (1, 60, (juce::uint8) 0), settings).isValid(),
           "a zero-velocity note-on does not re-fire the cue");

    check (! MidiControl::actionForChannelMessage (
               juce::MidiMessage::noteOff (1, 60), settings).isValid(),
           "a note-off does not fire");

    check (! MidiControl::actionForChannelMessage (
               juce::MidiMessage::noteOn (1, 61, (juce::uint8) 100), settings).isValid(),
           "an unbound note does nothing");

    {
        const auto action = MidiControl::actionForChannelMessage (
            juce::MidiMessage::controllerEvent (1, 7, 127), settings);
        check (action.type == ControlActionType::masterLevel, "a bound CC drives the master level");
        checkNear (action.value, 0.0, 0.001, "a full fader is 0 dB");
    }

    {
        const auto action = MidiControl::actionForChannelMessage (
            juce::MidiMessage::controllerEvent (1, 7, 0), settings);
        checkNear (action.value, -100.0, 0.001, "a closed fader is silence, not -60 dB");
    }

    {
        const auto action = MidiControl::actionForChannelMessage (
            juce::MidiMessage::controllerEvent (1, 7, 64), settings);
        // -60 dB + (64/127) * 60 dB.
        checkNear (action.value, -29.76, 0.05, "the middle of the fader is halfway down the range");
    }
}

//==============================================================================
void testArtNetParsing()
{
    cptest::section ("Art-Net packet parsing");

    {
        auto packet = buildArtNetPacket (1, { 10, 20, 30, 40 });
        DmxFrame frame;

        check (parseArtNetPacket (packet.data(), (int) packet.size(), frame),
               "a well-formed ArtDmx packet parses");
        check (frame.universe == 1, "universe decoded");
        check (frame.numSlots == 4, "slot count decoded");
        check (frame.levelAt (1) == 10 && frame.levelAt (4) == 40,
               "slots are addressed the way a desk numbers them, from 1");
        check (frame.levelAt (5) == 0, "reading past the end of the frame gives zero");
        check (frame.levelAt (0) == 0, "address 0 is not a valid DMX slot");
    }

    {
        // Universe is 15 bits: the low byte is sub-net + universe, the high byte is Net.
        auto packet = buildArtNetPacket (0x0105, { 1 });
        DmxFrame frame;
        check (parseArtNetPacket (packet.data(), (int) packet.size(), frame), "high universe parses");
        check (frame.universe == 0x0105, "the Net byte contributes to the universe number");
    }

    {
        auto packet = buildArtNetPacket (1, { 1, 2, 3 });
        packet[0] = 'X';
        DmxFrame frame;
        check (! parseArtNetPacket (packet.data(), (int) packet.size(), frame),
               "a packet with the wrong identifier is rejected");
    }

    {
        // ArtPoll and friends share the port; only ArtDmx carries levels.
        auto packet = buildArtNetPacket (1, { 1, 2, 3 });
        packet[8] = 0x00; packet[9] = 0x20;
        DmxFrame frame;
        check (! parseArtNetPacket (packet.data(), (int) packet.size(), frame),
               "a non-ArtDmx opcode is rejected");
    }

    {
        auto packet = buildArtNetPacket (1, { 1, 2, 3 });
        packet[11] = 13;
        DmxFrame frame;
        check (! parseArtNetPacket (packet.data(), (int) packet.size(), frame),
               "an old protocol version is rejected");
    }

    {
        // A packet claiming more data than it carries must not read past its own buffer.
        auto packet = buildArtNetPacket (1, { 1, 2, 3 });
        packet[16] = 0x01; packet[17] = 0xf4;   // says 500 slots, carries 3
        DmxFrame frame;
        check (parseArtNetPacket (packet.data(), (int) packet.size(), frame),
               "an over-declared length still parses");
        check (frame.numSlots == 3, "the slot count is clamped to what actually arrived");
    }

    {
        DmxFrame frame;
        check (! parseArtNetPacket (nullptr, 0, frame), "a null packet is rejected");

        auto packet = buildArtNetPacket (1, {});
        check (! parseArtNetPacket (packet.data(), (int) packet.size(), frame),
               "a packet with no slots is rejected");
    }
}

void testSacnParsing()
{
    cptest::section ("sACN packet parsing");

    {
        auto packet = buildSacnPacket (3, { 5, 6, 7 });
        DmxFrame frame;

        check (parseSacnPacket (packet.data(), (int) packet.size(), frame),
               "a well-formed E1.31 data packet parses");
        check (frame.universe == 3, "universe decoded");
        check (frame.numSlots == 3, "slot count decoded from the property count");
        check (frame.levelAt (1) == 5 && frame.levelAt (3) == 7, "slot levels decoded");
    }

    {
        // Bit 7 of the options byte marks preview data, which by spec must not drive live
        // output — a designer working blind should not fire a sound cue.
        auto packet = buildSacnPacket (3, { 255, 255 }, 0x80);
        DmxFrame frame;
        check (! parseSacnPacket (packet.data(), (int) packet.size(), frame),
               "preview packets are ignored");
    }

    {
        auto packet = buildSacnPacket (3, { 255 }, 0x40);
        DmxFrame frame;
        check (! parseSacnPacket (packet.data(), (int) packet.size(), frame),
               "stream-terminated packets are ignored");
    }

    {
        auto packet = buildSacnPacket (3, { 1 });
        packet[4] = 'X';
        DmxFrame frame;
        check (! parseSacnPacket (packet.data(), (int) packet.size(), frame),
               "a wrong ACN identifier is rejected");
    }

    {
        // Synchronisation packets carry no levels of their own.
        auto packet = buildSacnPacket (3, { 1 });
        packet[43] = 0x01;
        DmxFrame frame;
        check (! parseSacnPacket (packet.data(), (int) packet.size(), frame),
               "a sync packet is rejected");
    }

    {
        auto packet = buildSacnPacket (3, { 1 });
        packet[125] = 0xdd;                     // Not the DMX start code
        DmxFrame frame;
        check (! parseSacnPacket (packet.data(), (int) packet.size(), frame),
               "a non-zero start code is rejected");
    }

    {
        auto packet = buildSacnPacket (3, { 1, 2, 3 });
        packet[123] = 0x02; packet[124] = 0x00;  // Claims 511 slots, carries 3
        DmxFrame frame;
        check (parseSacnPacket (packet.data(), (int) packet.size(), frame),
               "an over-declared property count still parses");
        check (frame.numSlots == 3, "the slot count is clamped to what actually arrived");
    }

    {
        DmxFrame frame;
        std::vector<juce::uint8> tooShort (40, 0);
        check (! parseSacnPacket (tooShort.data(), (int) tooShort.size(), frame),
               "a truncated packet is rejected rather than read past");
    }

    checkEqual (dmx::sacnMulticastAddress (1), "239.255.0.1", "multicast group for universe 1");
    checkEqual (dmx::sacnMulticastAddress (300), "239.255.1.44", "multicast group for universe 300");
}

//==============================================================================
void testDmxTriggering()
{
    cptest::section ("DMX triggering");

    DmxSettings settings;
    settings.universe = 1;
    settings.startAddress = 10;
    settings.triggerThreshold = 128;
    settings.numDirectCueChannels = 8;

    const auto go = settings.startAddress + DmxSettings::offsetGo;
    const auto panicAddress = settings.startAddress + DmxSettings::offsetPanic;
    const auto pause = settings.startAddress + DmxSettings::offsetPause;
    const auto master = settings.startAddress + DmxSettings::offsetMasterLevel;
    const auto standby = settings.startAddress + DmxSettings::offsetStandbySelect;
    const auto firstCue = settings.startAddress + DmxSettings::offsetFirstDirectCue;

    {
        DmxTriggerState state;

        // The very first frame only arms the detector. Connecting to a desk already
        // holding GO high must not fire a cue the moment the cable goes in.
        auto actions = state.processFrame (frameWith (1, { { go, 255 } }), settings);
        check (actions.empty(), "the first frame arms rather than fires");

        // Still held: no edge, so still nothing.
        actions = state.processFrame (frameWith (1, { { go, 255 } }), settings);
        check (actions.empty(), "a held channel does not re-fire every frame");

        actions = state.processFrame (frameWith (1, {}), settings);
        check (actions.empty(), "releasing does not fire");

        actions = state.processFrame (frameWith (1, { { go, 255 } }), settings);
        check (containsAction (actions, ControlActionType::go), "a fresh rise fires GO");
    }

    {
        DmxTriggerState state;
        state.processFrame (frameWith (1, {}), settings);

        auto actions = state.processFrame (frameWith (1, { { go, 127 } }), settings);
        check (actions.empty(), "a level just below the threshold does not fire");

        actions = state.processFrame (frameWith (1, { { go, 128 } }), settings);
        check (containsAction (actions, ControlActionType::go), "the threshold itself fires");
    }

    {
        DmxTriggerState state;
        state.processFrame (frameWith (1, {}), settings);

        auto actions = state.processFrame (frameWith (1, { { panicAddress, 255 } }), settings);
        check (containsAction (actions, ControlActionType::panic), "the panic channel fires panic");
    }

    {
        // Pause follows the channel rather than edge-triggering, so the desk holds it.
        DmxTriggerState state;
        state.processFrame (frameWith (1, {}), settings);

        auto actions = state.processFrame (frameWith (1, { { pause, 255 } }), settings);
        check (containsAction (actions, ControlActionType::pause), "raising the pause channel pauses");

        actions = state.processFrame (frameWith (1, { { pause, 255 } }), settings);
        check (! containsAction (actions, ControlActionType::pause),
               "holding pause does not repeat the action");

        actions = state.processFrame (frameWith (1, { { pause, 0 } }), settings);
        check (containsAction (actions, ControlActionType::resume), "dropping the channel resumes");
    }

    {
        DmxTriggerState state;
        state.processFrame (frameWith (1, { { master, 255 } }), settings);

        auto actions = state.processFrame (frameWith (1, { { master, 255 } }), settings);
        check (! containsAction (actions, ControlActionType::masterLevel),
               "an unchanged master level is not resent 40 times a second");

        actions = state.processFrame (frameWith (1, { { master, 128 } }), settings);
        const auto* level = findAction (actions, ControlActionType::masterLevel);
        check (level != nullptr, "moving the master channel reports a level");

        if (level != nullptr)
            checkNear (level->value, -60.0 + 128.0 / 255.0 * 60.0, 0.1,
                       "the level maps across a 60 dB range");

        actions = state.processFrame (frameWith (1, { { master, 0 } }), settings);
        level = findAction (actions, ControlActionType::masterLevel);
        check (level != nullptr && level->value < -99.0, "a closed master channel is silence");
    }

    {
        DmxTriggerState state;
        state.processFrame (frameWith (1, {}), settings);

        auto actions = state.processFrame (frameWith (1, { { standby, 4 } }), settings);
        const auto* action = findAction (actions, ControlActionType::standbyCue);
        check (action != nullptr, "the standby channel selects a cue");

        if (action != nullptr)
            check (action->cueIndex == 3, "a value of N stands by the Nth cue, counting from 1");

        actions = state.processFrame (frameWith (1, { { standby, 4 } }), settings);
        check (! containsAction (actions, ControlActionType::standbyCue),
               "an unchanged standby channel does not repeat");
    }

    {
        DmxTriggerState state;
        state.processFrame (frameWith (1, {}), settings);

        auto actions = state.processFrame (frameWith (1, { { firstCue + 2, 255 } }), settings);
        const auto* action = findAction (actions, ControlActionType::goCue);
        check (action != nullptr, "a direct cue channel fires a cue");

        if (action != nullptr)
            check (action->cueIndex == 2, "the channel offset selects the cue by position");
    }

    {
        // Channels beyond the configured count belong to other fixtures on the universe.
        DmxTriggerState state;
        state.processFrame (frameWith (1, {}), settings);

        auto actions = state.processFrame (
            frameWith (1, { { firstCue + settings.numDirectCueChannels, 255 } }), settings);
        check (! containsAction (actions, ControlActionType::goCue),
               "a channel past the configured range is left alone");
    }

    {
        DmxTriggerState state;
        state.processFrame (frameWith (7, {}), settings);
        auto actions = state.processFrame (frameWith (7, { { go, 255 } }), settings);
        check (actions.empty(), "frames from another universe are ignored");
    }

    {
        // Several things can move in one frame; all of them should be reported.
        DmxTriggerState state;
        state.processFrame (frameWith (1, {}), settings);

        auto actions = state.processFrame (
            frameWith (1, { { go, 255 }, { firstCue, 255 }, { master, 200 } }), settings);
        check (containsAction (actions, ControlActionType::go)
               && containsAction (actions, ControlActionType::goCue)
               && containsAction (actions, ControlActionType::masterLevel),
               "one frame can carry several changes at once");
    }

    {
        DmxTriggerState state;
        state.processFrame (frameWith (1, {}), settings);
        state.processFrame (frameWith (1, { { go, 255 } }), settings);
        state.reset();

        auto actions = state.processFrame (frameWith (1, { { go, 255 } }), settings);
        check (actions.empty(), "after a reset the next frame re-arms rather than firing");
    }
}

//==============================================================================
void testControlMessagePersistence()
{
    cptest::section ("control message and settings persistence");

    ControlMessage message;
    message.type = ControlMessageType::midiShowControl;
    message.delay = 1.75;
    message.oscTarget = "Companion";
    message.oscAddress = "/custom/address";
    message.oscArguments = "1 \"two words\" 3.5";
    message.midiTarget = "Interface A";
    message.midiChannel = 9;
    message.midiData1 = 42;
    message.midiData2 = 99;
    message.mscDeviceID = 17;
    message.mscCommandFormat = msc::formatVideo;
    message.mscCommand = msc::commandStop;
    message.mscCueNumber = "44.2";
    message.mscCueList = "1";
    message.mmcCommand = mmc::commandPause;

    const auto restored = ControlMessage::fromVar (message.toVar());

    check (restored.type == message.type, "message type round trips");
    checkNear (restored.delay, 1.75, 1.0e-9, "delay round trips");
    checkEqual (restored.oscTarget, "Companion", "OSC target round trips");
    checkEqual (restored.oscAddress, "/custom/address", "OSC address round trips");
    checkEqual (restored.oscArguments, "1 \"two words\" 3.5", "OSC arguments round trip");
    checkEqual (restored.midiTarget, "Interface A", "MIDI target round trips");
    check (restored.midiChannel == 9 && restored.midiData1 == 42 && restored.midiData2 == 99,
           "MIDI channel and data round trip");
    check (restored.mscDeviceID == 17, "MSC device id round trips");
    check (restored.mscCommandFormat == msc::formatVideo, "MSC format round trips");
    check (restored.mscCommand == msc::commandStop, "MSC command round trips");
    checkEqual (restored.mscCueNumber, "44.2", "MSC cue number round trips");
    check (restored.mmcCommand == mmc::commandPause, "MMC command round trips");

    // --- settings -------------------------------------------------------------
    ControlSettings settings;
    settings.oscInputEnabled = true;
    settings.oscInputPort = 9000;
    settings.oscFeedbackEnabled = false;
    settings.oscTargets.push_back ({ "Desk", "10.0.0.5", 8000, false });
    settings.enabledMidiInputs.add ("input-identifier-1");
    settings.enabledMidiOutputs.add ("output-identifier-1");
    settings.mscDeviceID = 33;
    settings.mscRespondToSoundFormat = false;
    settings.midiMachineControlEnabled = true;
    settings.dmx.artNetEnabled = true;
    settings.dmx.sacnEnabled = true;
    settings.dmx.universe = 42;
    settings.dmx.startAddress = 101;
    settings.dmx.triggerThreshold = 200;
    settings.dmx.numDirectCueChannels = 64;

    MidiBinding binding;
    binding.kind = MidiTriggerKind::programChange;
    binding.channel = 0;
    binding.number = 12;
    binding.action = ControlActionType::goCue;
    binding.cueNumber = "5";
    settings.midiBindings.push_back (binding);

    const auto restoredSettings = ControlSettings::fromVar (settings.toVar());

    check (restoredSettings.oscInputEnabled, "OSC input flag round trips");
    check (restoredSettings.oscInputPort == 9000, "OSC port round trips");
    check (! restoredSettings.oscFeedbackEnabled, "feedback flag round trips");
    check (restoredSettings.oscTargets.size() == 1, "OSC targets round trip");

    if (! restoredSettings.oscTargets.empty())
    {
        checkEqual (restoredSettings.oscTargets[0].host, "10.0.0.5", "target host round trips");
        check (restoredSettings.oscTargets[0].port == 8000, "target port round trips");
        check (! restoredSettings.oscTargets[0].enabled, "a disabled target stays disabled");
    }

    check (restoredSettings.enabledMidiInputs.contains ("input-identifier-1"),
           "enabled MIDI inputs round trip");
    check (restoredSettings.enabledMidiOutputs.contains ("output-identifier-1"),
           "enabled MIDI outputs round trip");
    check (restoredSettings.mscDeviceID == 33, "MSC device id round trips");
    check (! restoredSettings.mscRespondToSoundFormat, "MSC format flags round trip");
    check (restoredSettings.midiMachineControlEnabled, "MMC flag round trips");

    check (restoredSettings.dmx.artNetEnabled && restoredSettings.dmx.sacnEnabled,
           "DMX protocol flags round trip");
    check (restoredSettings.dmx.universe == 42, "DMX universe round trips");
    check (restoredSettings.dmx.startAddress == 101, "DMX start address round trips");
    check (restoredSettings.dmx.triggerThreshold == 200, "DMX threshold round trips");
    check (restoredSettings.dmx.numDirectCueChannels == 64, "DMX cue channel count round trips");

    check (restoredSettings.midiBindings.size() == 1, "MIDI bindings round trip");

    if (! restoredSettings.midiBindings.empty())
    {
        const auto& b = restoredSettings.midiBindings[0];
        check (b.kind == MidiTriggerKind::programChange, "binding kind round trips");
        check (b.channel == 0, "an any-channel binding stays any-channel");
        check (b.action == ControlActionType::goCue, "binding action round trips");
        checkEqual (b.cueNumber, "5", "binding cue number round trips");
    }
}

void testControlCue()
{
    cptest::section ("control cues");

    Cue cue;
    cue.type = CueType::control;

    check (! cue.isPlayable(), "a control cue with no messages has nothing to do");

    ControlMessage message;
    message.type = ControlMessageType::osc;
    message.oscAddress = "/lx/go";
    cue.outputMessages.push_back (message);

    check (cue.isPlayable(), "a control cue with a message is playable");

    // This distinction is what lets a link from a control cue be pre-scheduled: its
    // playbackLength is 0 because it takes no time, not because its end is unknowable.
    checkNear (cue.playbackLength(), 0.0, 1.0e-9, "a control cue occupies no time");
    check (! cue.isOpenEnded(), "a control cue is not open-ended");

    Cue streaming;
    streaming.type = CueType::streaming;
    check (streaming.isOpenEnded(), "a streaming cue is open-ended");

    Cue looping;
    looping.fileDuration = 10.0;
    looping.loopEnabled = true;
    looping.loopCount = 0;
    check (looping.isOpenEnded(), "an infinite loop is open-ended");

    looping.loopCount = 3;
    check (! looping.isOpenEnded(), "a finite loop has a knowable end");

    Cue vamping;
    vamping.fileDuration = 10.0;
    vamping.vampEnabled = true;
    vamping.vampStart = 2.0;
    vamping.vampEnd = 5.0;
    check (vamping.isOpenEnded(), "an armed vamp is open-ended");

    // Round trip through the show format.
    const auto restored = Cue::fromVar (cue.toVar (juce::File()), juce::File());
    check (restored.type == CueType::control, "a control cue's type round trips");
    check (restored.outputMessages.size() == 1, "its messages round trip");

    if (! restored.outputMessages.empty())
        checkEqual (restored.outputMessages[0].oscAddress, "/lx/go",
                    "the message address round trips");
}

} // namespace

//==============================================================================
void runControlTests();

void runControlTests()
{
    testOscAddresses();
    testOscArguments();
    testShowControl();
    testMachineControl();
    testMidiBindings();
    testArtNetParsing();
    testSacnParsing();
    testDmxTriggering();
    testControlMessagePersistence();
    testControlCue();
}
