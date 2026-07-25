#pragma once

#include <juce_osc/juce_osc.h>

#include "Control/ControlSettings.h"
#include "Model/ControlMessage.h"

namespace cp
{

/** OSC in and out.

    Incoming addresses follow a fixed scheme rather than a user-editable mapping table, so
    a Companion module or a tablet can be pointed at the player and simply work:

        /go                          fire the standby cue
        /cue/<number>/go             fire a cue by its number
        /cue/<number>/stop [fade]    stop one cue, optionally over `fade` seconds
        /cue/<number>/standby        make a cue the standby cue
        /cue/<number>/select         select a cue for editing
        /cue/<number>/audition
        /cue/<number>/releasevamp
        /stop [fade]                 stop everything
        /panic                       immediate silence
        /pause  /resume  /pause/toggle
        /releasevamp                 release every vamp
        /standby/next  /standby/previous  /standby/<number>
        /master/level <dB>
        /status/query                send the whole state back to the configured targets

    Addresses are matched case-insensitively, because half the OSC controllers in the world
    capitalise differently from the other half.
*/
class OscControl : private juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback>
{
public:
    OscControl();
    ~OscControl() override;

    /** Opens or closes the input port and rebuilds the senders to match @p settings.
        Returns an error message, or an empty string on success. */
    juce::String applySettings (const ControlSettings& settings);

    bool isReceiving() const noexcept { return receiving; }
    int  getInputPort() const noexcept { return currentPort; }

    /** Fired on the message thread for every recognised incoming message. */
    std::function<void (const ControlAction&)> onAction;

    /** Fired when a controller asks for the current state. */
    std::function<void()> onStateQuery;

    /** Fired for anything received, recognised or not, for the control monitor. */
    std::function<void (const juce::String& description, bool recognised)> onActivity;

    /** Sends a cue's outgoing OSC message to its target, or to all targets when it names
        none. Returns false if nothing was sent. */
    bool send (const ControlMessage& message);

    /** Sends @p address with @p arguments to every enabled target. */
    void broadcast (const juce::String& address, const juce::Array<juce::var>& arguments);

    /** Parses an incoming address and argument list into an action. Public and pure so the
        address scheme can be tested without opening a socket. */
    static ControlAction actionForAddress (const juce::String& address,
                                           const juce::Array<juce::var>& arguments);

private:
    void oscMessageReceived (const juce::OSCMessage&) override;
    void oscBundleReceived (const juce::OSCBundle&) override;
    void handleMessage (const juce::OSCMessage&);

    juce::OSCReceiver receiver;
    std::vector<OscTarget> targets;
    std::vector<std::unique_ptr<juce::OSCSender>> senders;

    bool receiving { false };
    int  currentPort { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OscControl)
};

} // namespace cp
