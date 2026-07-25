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

/** Builds the step list for @p cue.

    A step only exists when the operator actually has to do something:

      - **Play** always.
      - **Devamp** once per vamp the cue has. (One today; the list is built so that adding
        more vamp regions later needs no change here or in anything that walks it.)
      - **End** only when the cue cannot end by itself - an infinite loop, an unreleased
        vamp, a streaming cue - or when the cue asks for one explicitly.

    That last rule matters. Giving every cue an End step would double the number of GOs in
    a show of one-shot stingers, which is exactly the sort of "helpful" design that gets an
    operator into trouble at speed.
*/
std::vector<CueStep> buildCueSteps (const Cue& cue);

} // namespace cp
