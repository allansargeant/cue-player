#pragma once

#include <juce_core/juce_core.h>

namespace cp
{

/** Something an external controller can ask the player to do.

    External control addresses cues by their *number* — the string an operator, a lighting
    desk or a Companion button knows them by — never by internal id. Renumbering a show
    therefore renumbers what the outside world triggers, which is what anyone would expect.
*/
enum class ControlActionType
{
    none = 0,
    go,                 ///< Fire the standby cue and advance.
    goCue,              ///< Fire a specific cue by number.
    stopAll,            ///< Fade everything out over `value` seconds.
    stopCue,
    panic,              ///< Immediate silence.
    pause,
    resume,
    pauseToggle,
    releaseVamp,        ///< Every vamping cue.
    releaseVampCue,
    standbyCue,
    standbyNext,
    standbyPrevious,
    selectCue,
    auditionCue,
    masterLevel         ///< `value` is dB.
};

juce::String toString (ControlActionType);
ControlActionType controlActionTypeFromString (const juce::String&);
juce::StringArray controlActionTypeNames();

/** True when the action needs a cue number to mean anything. */
bool actionNeedsCueNumber (ControlActionType) noexcept;

/** True when the action's `value` field is meaningful. */
bool actionUsesValue (ControlActionType) noexcept;

struct ControlAction
{
    ControlActionType type { ControlActionType::none };
    juce::String cueNumber;      ///< For cue-specific actions.

    /** Position in the list, used instead of `cueNumber` when >= 0. DMX has only numbers
        to work with, so its channels address cues by position; every other transport
        addresses them by the number printed on the cue sheet. */
    int cueIndex { -1 };

    double value { 0.0 };        ///< Master level in dB, or a stop fade time in seconds.

    /** Where it came from, for the control monitor in the setup window. */
    juce::String origin;

    bool isValid() const noexcept { return type != ControlActionType::none; }
};

/** Implemented by whatever owns the show and the engine. Always called on the message
    thread — the transports marshal to it, because acting on a cue list from a socket
    thread would be a data race. */
struct ControlActionHandler
{
    virtual ~ControlActionHandler() = default;
    virtual void performControlAction (const ControlAction& action) = 0;
};

} // namespace cp
