#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Control/ControlAction.h"

namespace cp
{

/** An OSC destination messages can be sent to. */
struct OscTarget
{
    juce::String name { "Main" };
    juce::String host { "127.0.0.1" };
    int          port { 53001 };
    bool         enabled { true };
};

/** How an incoming MIDI message maps onto an action.

    Note, CC and program-change bindings are matched on channel and number. A CC binding
    with `useValueAsLevel` set feeds the controller value into the action's value instead
    of triggering it, which is how a fader ends up driving the master level.
*/
enum class MidiTriggerKind { noteOn = 0, controlChange, programChange };

juce::String toString (MidiTriggerKind);
MidiTriggerKind midiTriggerKindFromString (const juce::String&);
juce::StringArray midiTriggerKindNames();

struct MidiBinding
{
    MidiTriggerKind kind { MidiTriggerKind::noteOn };
    int channel { 1 };          ///< 1-16, or 0 for "any channel".
    int number { 60 };          ///< Note, controller or program number.
    bool useValueAsLevel { false };

    ControlActionType action { ControlActionType::go };
    juce::String cueNumber;

    juce::String describe() const;
};

/** DMX control, over Art-Net or sACN.

    A fixed block of channels starting at `startAddress`, so a lighting desk can drive the
    sound player without either operator maintaining a mapping table:

        +0  GO (rises past the threshold to fire the standby cue)
        +1  Stop all
        +2  Panic
        +3  Pause while held above the threshold
        +4  Release vamp
        +5  Master level (0 = silence, 255 = 0 dB)
        +6  Standby select: a value of N stands by the Nth cue in the list
        +7  onwards: fire the 1st, 2nd, 3rd... cue in the list directly
*/
struct DmxSettings
{
    bool artNetEnabled { false };
    bool sacnEnabled { false };
    int  universe { 1 };
    int  startAddress { 1 };
    int  triggerThreshold { 128 };   ///< A channel counts as "on" at or above this.
    int  numDirectCueChannels { 24 };

    static constexpr int offsetGo = 0;
    static constexpr int offsetStopAll = 1;
    static constexpr int offsetPanic = 2;
    static constexpr int offsetPause = 3;
    static constexpr int offsetReleaseVamp = 4;
    static constexpr int offsetMasterLevel = 5;
    static constexpr int offsetStandbySelect = 6;
    static constexpr int offsetFirstDirectCue = 7;
};

//==============================================================================
/** Everything about how this machine talks to the outside world.

    These are deliberately *not* stored in the show file: ports, MIDI device names and
    DMX universes belong to the rig, and a show that opens in the rehearsal room should not
    drag the venue's network layout along with it.
*/
struct ControlSettings
{
    //== OSC ===================================================================
    bool oscInputEnabled { false };
    int  oscInputPort { 53000 };

    /** Send status back to whoever last subscribed, so Companion buttons can show state. */
    bool oscFeedbackEnabled { true };

    std::vector<OscTarget> oscTargets;

    //== MIDI ==================================================================
    juce::StringArray enabledMidiInputs;    ///< Device identifiers, not names.
    juce::StringArray enabledMidiOutputs;

    bool midiShowControlEnabled { true };
    /** Only act on MSC addressed to this device id, or to an all-call. 127 means listen
        to everything. */
    int  mscDeviceID { 0 };
    /** MSC command formats we respond to. Sound is the obvious one; All-types is required
        by the spec to be honoured by everything. */
    bool mscRespondToSoundFormat { true };
    bool mscRespondToAllTypesFormat { true };

    bool midiMachineControlEnabled { false };

    std::vector<MidiBinding> midiBindings;

    //== DMX ===================================================================
    DmxSettings dmx;

    //== Persistence ===========================================================
    juce::var toVar() const;
    static ControlSettings fromVar (const juce::var&);

    /** Sensible starting point: a couple of common OSC targets and no inputs open, so the
        app never listens on a network port until someone asks it to. */
    static ControlSettings createDefault();
};

} // namespace cp
