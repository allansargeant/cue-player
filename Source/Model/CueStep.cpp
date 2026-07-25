#include "Model/CueStep.h"

#include "GUI/LookAndFeel.h"

namespace cp
{

std::vector<CueStep> buildCueSteps (const Cue& cue)
{
    std::vector<CueStep> steps;

    // A control cue is a single event: there is nothing to hold and nothing to fade.
    if (cue.type == CueType::control)
    {
        CueStep step;
        step.type = CueStepType::play;
        step.label = "Fire messages";
        step.detail = juce::String ((int) cue.outputMessages.size())
                        + (cue.outputMessages.size() == 1 ? " message" : " messages");
        steps.push_back (step);
        return steps;
    }

    // --- Play -----------------------------------------------------------------
    {
        CueStep step;
        step.type = CueStepType::play;
        step.label = "Play cue";

        if (cue.preWait > 0.0)
            step.detail = "after " + juce::String (cue.preWait, 2) + "s pre-wait";
        else if (cue.fadeInTime > 0.0)
            step.detail = juce::String (cue.fadeInTime, 1) + "s fade in";

        steps.push_back (step);
    }

    // --- Devamp ---------------------------------------------------------------
    // Only when there is a vamp to release. A devamp row on a cue with no vamp would be a
    // step that does nothing, sitting in the operator's way on every cue in the show.
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

    // --- Fade / Stop ----------------------------------------------------------
    // Always. Even a cue that would end by itself can be wanted out early.
    {
        CueStep step;
        step.type = CueStepType::end;
        step.label = "Fade/Stop";
        step.detail = cue.endAction == EndAction::hardStop
                          ? juce::String ("hard stop")
                          : juce::String (cue.endFadeTime, 1) + "s fade out";
        steps.push_back (step);
    }

    return steps;
}

} // namespace cp
