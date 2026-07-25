#pragma once

#include "Model/Cue.h"

namespace cp
{

/** One thing an operator does to a cue during its life.

    A cue is not a single event. A vamped cue is played, held, released and eventually
    ended, and each of those is a separate GO. Modelling that as a short list of steps
    inside the cue means the operator walks the cue's lifecycle with the same key they use
    for everything else, instead of hunting for a Release button while the scene runs.
*/
enum class CueStepType
{
    play = 0,   ///< Fire the cue.
    devamp,     ///< Let one vamp go, so playback carries on past it.
    end         ///< Stop it, either fading or hard.
};

struct CueStep
{
    CueStepType  type { CueStepType::play };
    int          vampIndex { 0 };   ///< Which vamp a devamp step releases.
    juce::String label;
    juce::String detail;            ///< Secondary text, e.g. the fade time.
};

/** Builds the sub-cue list for @p cue.

      - **Play cue** always.
      - **Devamp** only when the cue actually has a vamp to release; there is nothing to
        show otherwise. (One today. The list is built so that several vamp regions per cue
        would need no change here or in anything that walks it.)
      - **Fade/Stop** always, so every cue can be ended from the list whether or not it
        would have stopped by itself.

    A control cue is a single event and gets one step.
*/
std::vector<CueStep> buildCueSteps (const Cue& cue);

/** Standby sits on the cue itself, not on one of its sub-cues. */
static constexpr int cueHeaderStep = -1;

} // namespace cp
