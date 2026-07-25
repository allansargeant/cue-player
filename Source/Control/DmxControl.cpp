#include "Control/DmxControl.h"

#include <cstring>

namespace cp
{

namespace
{
    constexpr int receiveBufferSize = 1600;   // Comfortably over a full 512-slot packet.
    constexpr int maxQueuedActions = 128;
}

//==============================================================================
void DmxTriggerState::reset()
{
    havePreviousFrame = false;
    std::memset (previous, 0, sizeof (previous));
    lastMasterValue = -1;
    lastStandbySelect = -1;
    pausedByDmx = false;
}

juce::uint8 DmxTriggerState::previousLevel (int address) const
{
    const auto index = address - 1;
    return juce::isPositiveAndBelow (index, 512) ? previous[index] : (juce::uint8) 0;
}

bool DmxTriggerState::rose (int address, const DmxFrame& frame, int threshold) const
{
    const auto now = (int) frame.levelAt (address);
    const auto before = (int) previousLevel (address);
    return now >= threshold && before < threshold;
}

std::vector<ControlAction> DmxTriggerState::processFrame (const DmxFrame& frame,
                                                          const DmxSettings& settings)
{
    std::vector<ControlAction> actions;

    if (frame.universe != settings.universe)
        return actions;

    const auto base = settings.startAddress;
    const auto threshold = juce::jlimit (1, 255, settings.triggerThreshold);

    const auto add = [&actions] (ControlActionType type, const juce::String& origin,
                                 double value = 0.0, int cueIndex = -1)
    {
        ControlAction action;
        action.type = type;
        action.value = value;
        action.cueIndex = cueIndex;
        action.origin = origin;
        actions.push_back (action);
    };

    // The first frame after a reset only arms the edge detector. Without this, connecting
    // to a desk that is already holding a channel high would fire that cue immediately.
    if (! havePreviousFrame)
    {
        havePreviousFrame = true;
        std::memcpy (previous, frame.slots, sizeof (previous));
        lastMasterValue = frame.levelAt (base + DmxSettings::offsetMasterLevel);
        lastStandbySelect = frame.levelAt (base + DmxSettings::offsetStandbySelect);
        pausedByDmx = frame.levelAt (base + DmxSettings::offsetPause) >= threshold;
        return actions;
    }

    if (rose (base + DmxSettings::offsetGo, frame, threshold))
        add (ControlActionType::go, "DMX GO");

    if (rose (base + DmxSettings::offsetStopAll, frame, threshold))
        add (ControlActionType::stopAll, "DMX stop all", 2.0);

    if (rose (base + DmxSettings::offsetPanic, frame, threshold))
        add (ControlActionType::panic, "DMX panic");

    if (rose (base + DmxSettings::offsetReleaseVamp, frame, threshold))
        add (ControlActionType::releaseVamp, "DMX release vamp");

    // Pause follows the channel rather than edge-triggering, so the desk holds the state.
    {
        const auto wantsPause = frame.levelAt (base + DmxSettings::offsetPause) >= threshold;

        if (wantsPause != pausedByDmx)
        {
            pausedByDmx = wantsPause;
            add (wantsPause ? ControlActionType::pause : ControlActionType::resume,
                 wantsPause ? "DMX pause" : "DMX resume");
        }
    }

    // Master level is continuous. Only report real movement, or a desk sending 40 frames a
    // second would queue 40 level changes a second forever.
    {
        const auto level = (int) frame.levelAt (base + DmxSettings::offsetMasterLevel);

        if (level != lastMasterValue)
        {
            lastMasterValue = level;
            const auto db = level <= 0 ? -100.0 : -60.0 + (double) level / 255.0 * 60.0;
            add (ControlActionType::masterLevel, "DMX master level", db);
        }
    }

    // Standby select: a value of N stands by the Nth cue, and 0 means "leave it alone".
    {
        const auto select = (int) frame.levelAt (base + DmxSettings::offsetStandbySelect);

        if (select != lastStandbySelect)
        {
            lastStandbySelect = select;

            if (select > 0)
                add (ControlActionType::standbyCue, "DMX standby " + juce::String (select),
                     0.0, select - 1);
        }
    }

    for (int i = 0; i < settings.numDirectCueChannels; ++i)
    {
        const auto address = base + DmxSettings::offsetFirstDirectCue + i;

        if (address > 512)
            break;

        if (rose (address, frame, threshold))
            add (ControlActionType::goCue, "DMX cue " + juce::String (i + 1), 0.0, i);
    }

    std::memcpy (previous, frame.slots, sizeof (previous));
    return actions;
}

//==============================================================================
DmxControl::DmxControl()
    : juce::Thread ("DMX listener")
{
}

DmxControl::~DmxControl()
{
    signalThreadShouldExit();
    stopThread (2000);
    cancelPendingUpdate();
}

juce::String DmxControl::applySettings (const ControlSettings& settings)
{
    signalThreadShouldExit();
    stopThread (2000);

    artNetSocket.reset();
    sacnSocket.reset();
    artNetOpen = false;
    sacnOpen = false;
    frameCount.store (0);
    triggerState.reset();

    {
        const juce::ScopedLock sl (settingsLock);
        dmxSettings = settings.dmx;
    }

    juce::StringArray problems;

    if (settings.dmx.artNetEnabled)
    {
        artNetSocket = std::make_unique<juce::DatagramSocket> (true);
        artNetSocket->setEnablePortReuse (true);

        if (artNetSocket->bindToPort (dmx::artNetPort))
            artNetOpen = true;
        else
        {
            artNetSocket.reset();
            problems.add ("Could not listen for Art-Net on UDP " + juce::String (dmx::artNetPort)
                          + ". Another application may already be bound to it.");
        }
    }

    if (settings.dmx.sacnEnabled)
    {
        sacnSocket = std::make_unique<juce::DatagramSocket> (true);
        sacnSocket->setEnablePortReuse (true);

        if (sacnSocket->bindToPort (dmx::sacnPort))
        {
            sacnOpen = true;

            // sACN is normally multicast, one group per universe. Unicast senders reach us
            // on the same bound port either way, so failing to join is not fatal.
            const auto group = dmx::sacnMulticastAddress (settings.dmx.universe);

            if (! sacnSocket->joinMulticast (group))
                problems.add ("Listening for sACN, but could not join multicast group "
                              + group + ". Unicast senders will still be received.");
        }
        else
        {
            sacnSocket.reset();
            problems.add ("Could not listen for sACN on UDP " + juce::String (dmx::sacnPort) + ".");
        }
    }

    if (artNetOpen || sacnOpen)
        startThread (juce::Thread::Priority::high);

    return problems.joinIntoString ("\n");
}

void DmxControl::run()
{
    juce::HeapBlock<char> buffer (receiveBufferSize);

    while (! threadShouldExit())
    {
        bool gotSomething = false;

        const auto poll = [&] (juce::DatagramSocket* socket, bool isArtNet)
        {
            if (socket == nullptr)
                return;

            // Short timeout so the loop keeps checking threadShouldExit() promptly.
            if (socket->waitUntilReady (true, 20) != 1)
                return;

            const auto bytesRead = socket->read (buffer.get(), receiveBufferSize, false);

            if (bytesRead <= 0)
                return;

            DmxFrame frame;
            const auto parsed = isArtNet ? parseArtNetPacket (buffer.get(), bytesRead, frame)
                                         : parseSacnPacket (buffer.get(), bytesRead, frame);

            if (! parsed)
                return;

            gotSomething = true;
            handleFrame (frame, isArtNet ? "Art-Net" : "sACN");
        };

        poll (artNetSocket.get(), true);
        poll (sacnSocket.get(), false);

        if (! gotSomething)
            wait (5);
    }
}

void DmxControl::handleFrame (const DmxFrame& frame, const char* protocolName)
{
    DmxSettings settings;

    {
        const juce::ScopedLock sl (settingsLock);
        settings = dmxSettings;
    }

    if (frame.universe != settings.universe)
        return;

    frameCount.fetch_add (1);

    auto actions = triggerState.processFrame (frame, settings);

    {
        const juce::ScopedLock sl (queueLock);

        lastActivity = juce::String (protocolName) + " universe " + juce::String (frame.universe)
                     + ", " + juce::String (frame.numSlots) + " slots";

        for (auto& action : actions)
        {
            if ((int) queue.size() >= maxQueuedActions)
                break;

            queue.push_back (std::move (action));
        }
    }

    triggerAsyncUpdate();
}

void DmxControl::handleAsyncUpdate()
{
    std::vector<ControlAction> batch;
    juce::String activity;

    {
        const juce::ScopedLock sl (queueLock);
        batch.swap (queue);
        activity = lastActivity;
    }

    if (onActivity != nullptr && activity.isNotEmpty())
        onActivity (activity, ! batch.empty());

    if (onAction == nullptr)
        return;

    for (const auto& action : batch)
        onAction (action);
}

} // namespace cp
