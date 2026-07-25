#pragma once

#include "Control/DmxControl.h"
#include "Control/MidiControl.h"
#include "Control/OscControl.h"

namespace cp
{

/** Owns every way the outside world talks to the player, and every way it talks back.

    Incoming: OSC, MIDI (bindings, MSC, MMC) and DMX over Art-Net or sACN all resolve to a
    ControlAction and land on the handler, always on the message thread.

    Outgoing: the messages a cue carries are queued here and sent when they come due, and a
    status feed is published to the configured OSC targets so a Companion page can show
    what is standing by and what is playing.
*/
class ControlHub : private juce::Timer,
                   public  juce::ChangeBroadcaster
{
public:
    ControlHub();
    ~ControlHub() override;

    void setActionHandler (ControlActionHandler* handler) noexcept { actionHandler = handler; }

    const ControlSettings& getSettings() const noexcept { return settings; }

    /** Stores @p newSettings and opens/closes ports to match.
        Returns any problems, one per line, or an empty string. */
    juce::String applySettings (const ControlSettings& newSettings);

    /** Re-applies the current settings, e.g. after a MIDI device is plugged in. */
    juce::String reapplySettings() { return applySettings (settings); }

    //== Outgoing =============================================================
    /** Queues a cue's messages. @p secondsUntilAudio is the cue's pre-wait, so a message
        with no delay of its own lands with the first sample rather than at the GO. */
    void fireCueMessages (const std::vector<ControlMessage>& messages, double secondsUntilAudio);

    /** Drops everything still queued. Called on panic — a control message arriving after
        someone has hit panic is exactly what they were trying to stop. */
    void cancelPendingMessages();

    int getNumPendingMessages() const;

    /** Sends one message immediately, for the inspector's "test" button. */
    bool sendNow (const ControlMessage& message);

    //== Feedback =============================================================
    struct StatusSnapshot
    {
        juce::String standbyNumber, standbyName;
        int  numPlaying { 0 };
        bool paused { false };
        bool vamping { false };
        double masterDb { 0.0 };
        juce::StringArray playingCueNumbers;

        bool operator== (const StatusSnapshot&) const;
        bool operator!= (const StatusSnapshot& other) const { return ! operator== (other); }
    };

    /** Publishes state to the OSC targets, but only when something actually changed —
        or always, when @p force is set, which is what a /status/query asks for. */
    void publishStatus (const StatusSnapshot& snapshot, bool force = false);

    /** Set by the owner so a /status/query can pull a fresh snapshot. */
    std::function<StatusSnapshot()> onStatusRequested;

    //== Monitoring ===========================================================
    /** Recent traffic, newest last, for the setup window. */
    juce::StringArray getMonitorLines() const;
    void clearMonitor();

    juce::String getStatusSummary() const;

    OscControl&  getOsc() noexcept  { return osc; }
    MidiControl& getMidi() noexcept { return midi; }
    DmxControl&  getDmx() noexcept  { return dmx; }

private:
    void timerCallback() override;
    void dispatch (const ControlAction& action);
    void logActivity (const juce::String& description, bool recognised);

    struct PendingMessage
    {
        double dueAtMs { 0.0 };
        ControlMessage message;
    };

    ControlSettings settings;
    ControlActionHandler* actionHandler { nullptr };

    OscControl osc;
    MidiControl midi;
    DmxControl dmx;

    mutable juce::CriticalSection pendingLock;
    std::vector<PendingMessage> pending;

    mutable juce::CriticalSection monitorLock;
    juce::StringArray monitorLines;
    static constexpr int maxMonitorLines = 200;

    StatusSnapshot lastPublished;
    bool havePublished { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ControlHub)
};

} // namespace cp
