#include "Control/ControlHub.h"

namespace cp
{

namespace
{
    constexpr int schedulerIntervalMs = 5;
}

ControlHub::ControlHub()
{
    const auto action = [this] (const ControlAction& a) { dispatch (a); };
    const auto activity = [this] (const juce::String& d, bool recognised) { logActivity (d, recognised); };

    osc.onAction = action;
    osc.onActivity = activity;
    osc.onStateQuery = [this]
    {
        if (onStatusRequested != nullptr)
            publishStatus (onStatusRequested(), true);
    };

    midi.onAction = action;
    midi.onActivity = activity;

    dmx.onAction = action;
    dmx.onActivity = activity;

    startTimer (schedulerIntervalMs);
}

ControlHub::~ControlHub()
{
    stopTimer();
    osc.onAction = nullptr;
    midi.onAction = nullptr;
    dmx.onAction = nullptr;
}

//==============================================================================
juce::String ControlHub::applySettings (const ControlSettings& newSettings)
{
    settings = newSettings;

    juce::StringArray problems;

    if (const auto e = osc.applySettings (settings); e.isNotEmpty())  problems.add (e);
    if (const auto e = midi.applySettings (settings); e.isNotEmpty()) problems.add (e);
    if (const auto e = dmx.applySettings (settings); e.isNotEmpty())  problems.add (e);

    havePublished = false;   // Force the next status out to a freshly connected target.
    sendChangeMessage();

    return problems.joinIntoString ("\n");
}

void ControlHub::dispatch (const ControlAction& action)
{
    if (actionHandler != nullptr && action.isValid())
        actionHandler->performControlAction (action);
}

void ControlHub::logActivity (const juce::String& description, bool recognised)
{
    const juce::ScopedLock sl (monitorLock);

    monitorLines.add (juce::Time::getCurrentTime().toString (false, true, true, true)
                      + (recognised ? "  " : "  (ignored) ") + description);

    while (monitorLines.size() > maxMonitorLines)
        monitorLines.remove (0);
}

juce::StringArray ControlHub::getMonitorLines() const
{
    const juce::ScopedLock sl (monitorLock);
    return monitorLines;
}

void ControlHub::clearMonitor()
{
    const juce::ScopedLock sl (monitorLock);
    monitorLines.clear();
}

juce::String ControlHub::getStatusSummary() const
{
    juce::StringArray parts;

    parts.add (osc.isReceiving() ? "OSC in " + juce::String (osc.getInputPort())
                                 : juce::String ("OSC in off"));

    const auto midiInputs = midi.getOpenInputNames().size();
    const auto midiOutputs = midi.getOpenOutputNames().size();
    parts.add ("MIDI " + juce::String (midiInputs) + " in / " + juce::String (midiOutputs) + " out");

    juce::StringArray dmxParts;

    if (dmx.isListeningToArtNet()) dmxParts.add ("Art-Net");
    if (dmx.isListeningToSacn())   dmxParts.add ("sACN");

    parts.add (dmxParts.isEmpty()
                   ? juce::String ("DMX off")
                   : dmxParts.joinIntoString (" + ") + " u" + juce::String (settings.dmx.universe)
                       + " (" + juce::String (dmx.getFrameCount()) + " frames)");

    return parts.joinIntoString ("   |   ");
}

//==============================================================================
void ControlHub::fireCueMessages (const std::vector<ControlMessage>& messages, double secondsUntilAudio)
{
    if (messages.empty())
        return;

    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto offset = juce::jmax (0.0, secondsUntilAudio) * 1000.0;

    const juce::ScopedLock sl (pendingLock);

    for (const auto& message : messages)
        pending.push_back ({ now + offset + juce::jmax (0.0, message.delay) * 1000.0, message });
}

void ControlHub::cancelPendingMessages()
{
    const juce::ScopedLock sl (pendingLock);
    pending.clear();
}

int ControlHub::getNumPendingMessages() const
{
    const juce::ScopedLock sl (pendingLock);
    return (int) pending.size();
}

bool ControlHub::sendNow (const ControlMessage& message)
{
    const auto sent = message.type == ControlMessageType::osc ? osc.send (message)
                                                              : midi.send (message);

    logActivity ("sent  " + message.describe() + (sent ? "" : "   (no target reached)"), sent);
    return sent;
}

void ControlHub::timerCallback()
{
    std::vector<ControlMessage> due;

    {
        const juce::ScopedLock sl (pendingLock);

        if (pending.empty())
            return;

        const auto now = juce::Time::getMillisecondCounterHiRes();

        for (auto it = pending.begin(); it != pending.end();)
        {
            if (it->dueAtMs <= now)
            {
                due.push_back (it->message);
                it = pending.erase (it);
            }
            else
            {
                ++it;
            }
        }
    }

    // Sent outside the lock: a MIDI write can block, and holding the queue lock across it
    // would stall whichever thread is queueing the next cue's messages.
    for (const auto& message : due)
        sendNow (message);
}

//==============================================================================
bool ControlHub::StatusSnapshot::operator== (const StatusSnapshot& other) const
{
    return standbyNumber == other.standbyNumber
        && standbyName == other.standbyName
        && numPlaying == other.numPlaying
        && paused == other.paused
        && vamping == other.vamping
        && std::abs (masterDb - other.masterDb) < 0.05
        && playingCueNumbers == other.playingCueNumbers;
}

void ControlHub::publishStatus (const StatusSnapshot& snapshot, bool force)
{
    if (! settings.oscFeedbackEnabled)
        return;

    if (! force && havePublished && snapshot == lastPublished)
        return;

    lastPublished = snapshot;
    havePublished = true;

    osc.broadcast ("/status/standby", { snapshot.standbyNumber, snapshot.standbyName });
    osc.broadcast ("/status/playing", { snapshot.numPlaying });
    osc.broadcast ("/status/paused",  { snapshot.paused ? 1 : 0 });
    osc.broadcast ("/status/vamping", { snapshot.vamping ? 1 : 0 });
    osc.broadcast ("/status/master",  { snapshot.masterDb });
    osc.broadcast ("/status/playingCues",
                   { snapshot.playingCueNumbers.joinIntoString (" ") });
}

} // namespace cp
