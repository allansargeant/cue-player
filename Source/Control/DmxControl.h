#pragma once

#include <juce_events/juce_events.h>

#include "Control/ControlSettings.h"
#include "Control/DmxProtocol.h"

namespace cp
{

/** Turns a stream of DMX frames into actions.

    Trigger channels are edge-detected: an action fires on the frame where a channel first
    rises to the threshold, not on every frame it stays there. Lighting desks send the same
    universe 40 times a second, so level-triggering would fire a cue 40 times a second.

    Separated from the sockets so the whole mapping can be tested by feeding it frames.
*/
class DmxTriggerState
{
public:
    /** Frames from a universe other than the configured one are ignored. */
    std::vector<ControlAction> processFrame (const DmxFrame& frame, const DmxSettings& settings);

    /** Forgets the previous frame, so the next one re-arms rather than firing. Call when
        the settings change or a stream drops. */
    void reset();

private:
    bool rose (int address, const DmxFrame& frame, int threshold) const;
    juce::uint8 previousLevel (int address) const;

    bool havePreviousFrame { false };
    juce::uint8 previous[512] {};
    int  lastMasterValue { -1 };
    int  lastStandbySelect { -1 };
    bool pausedByDmx { false };
};

//==============================================================================
/** Listens for Art-Net and sACN and reports the actions they ask for.

    Sockets are read on a background thread; actions are handed to the message thread,
    because a DMX packet must not touch the cue list from a socket read.
*/
class DmxControl : private juce::Thread,
                   private juce::AsyncUpdater
{
public:
    DmxControl();
    ~DmxControl() override;

    /** Opens or closes the sockets to match @p settings.
        Returns an error message, or an empty string on success. */
    juce::String applySettings (const ControlSettings& settings);

    std::function<void (const ControlAction&)> onAction;
    std::function<void (const juce::String& description, bool recognised)> onActivity;

    bool isListeningToArtNet() const noexcept { return artNetOpen; }
    bool isListeningToSacn() const noexcept   { return sacnOpen; }

    /** Frames seen since the last settings change, for the setup window's status line. */
    int getFrameCount() const noexcept { return frameCount.load(); }

private:
    void run() override;
    void handleAsyncUpdate() override;
    void handleFrame (const DmxFrame& frame, const char* protocolName);

    std::unique_ptr<juce::DatagramSocket> artNetSocket, sacnSocket;
    bool artNetOpen { false }, sacnOpen { false };

    DmxSettings dmxSettings;
    juce::CriticalSection settingsLock;

    DmxTriggerState triggerState;

    juce::CriticalSection queueLock;
    std::vector<ControlAction> queue;
    juce::String lastActivity;
    std::atomic<int> frameCount { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DmxControl)
};

} // namespace cp
