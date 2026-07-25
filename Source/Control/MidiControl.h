#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include "Control/ControlSettings.h"
#include "Model/ControlMessage.h"

namespace cp
{

/** MIDI in and out: note/CC/program bindings, MIDI Show Control, MIDI Machine Control.

    Incoming MIDI arrives on a high-priority driver thread. Nothing here touches the show
    from that thread — messages are queued and handed to the message thread, because acting
    on a cue list from the MIDI callback would be a data race with the UI.
*/
class MidiControl : private juce::MidiInputCallback,
                    private juce::AsyncUpdater
{
public:
    MidiControl();
    ~MidiControl() override;

    /** Opens the enabled devices named in @p settings and closes the rest.
        Returns an error message, or an empty string on success. */
    juce::String applySettings (const ControlSettings& settings);

    /** Fired on the message thread for every message that maps onto an action. */
    std::function<void (const ControlAction&)> onAction;

    /** Fired on the message thread for everything received, for the control monitor. */
    std::function<void (const juce::String& description, bool recognised)> onActivity;

    /** Sends a cue's outgoing MIDI message. Returns false if nothing was sent. */
    bool send (const ControlMessage& message);

    juce::StringArray getOpenInputNames() const;
    juce::StringArray getOpenOutputNames() const;

    /** Maps a decoded MSC message onto an action, given the filters in @p settings.
        Pure and public so the mapping can be tested without a MIDI interface. */
    static ControlAction actionForShowControl (const IncomingMsc& msc, const ControlSettings& settings);

    /** Maps an MMC command byte onto an action. */
    static ControlAction actionForMachineControl (juce::uint8 command);

    /** Matches a channel-voice message against @p settings' bindings. */
    static ControlAction actionForChannelMessage (const juce::MidiMessage& message,
                                                  const ControlSettings& settings);

private:
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void handleAsyncUpdate() override;

    struct Pending
    {
        juce::MidiMessage message;
        juce::String deviceName;
    };

    juce::CriticalSection queueLock;
    std::vector<Pending> queue;

    std::vector<std::unique_ptr<juce::MidiInput>> inputs;
    std::vector<std::unique_ptr<juce::MidiOutput>> outputs;
    juce::StringArray outputNames;

    ControlSettings settingsCopy;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiControl)
};

} // namespace cp
