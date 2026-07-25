#include "Model/CueStep.h"

#include "GUI/LookAndFeel.h"

namespace cp
{

std::vector<CueStep> buildCueSteps (const Cue& cue)
{
    std::vector<CueStep> steps;

    // --- Play -----------------------------------------------------------------
    {
        CueStep step;
        step.type = CueStepType::play;
        step.label = "Play";

        if (cue.preWait > 0.0)
            step.detail = "after " + juce::String (cue.preWait, 2) + "s pre-wait";
        else if (cue.fadeInTime > 0.0)
            step.detail = juce::String (cue.fadeInTime, 1) + "s fade in";

        steps.push_back (step);
    }

    // --- Devamp, one per vamp -------------------------------------------------
    if (cue.hasUsableVamp())
    {
        CueStep step;
        step.type = CueStepType::devamp;
        step.vampIndex = 0;
        step.label = "Devamp";
        step.detail = formatTime (cue.vampStart) + " to " + formatTime (cue.vampEnd)
                    + (cue.vampRelease == VampRelease::immediately ? ", leaves at once"
                                                                   : ", finishes the pass");
        steps.push_back (step);
    }

    // --- End ------------------------------------------------------------------
    const auto wantsEnd = cue.endStepMode == EndStepMode::always
                       || (cue.endStepMode == EndStepMode::automatic && cue.isOpenEnded());

    if (wantsEnd)
    {
        CueStep step;
        step.type = CueStepType::end;
        step.label = "End";
        step.detail = cue.endAction == EndAction::hardStop
                          ? "hard stop"
                          : juce::String (cue.endFadeTime, 1) + "s fade out";
        steps.push_back (step);
    }

    return steps;
}

} // namespace cp
