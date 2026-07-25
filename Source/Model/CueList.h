#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "Model/CueStep.h"

namespace cp
{

/** The ordered list of cues in a show, plus the two positions an operator cares about:
    which cue is *selected* for editing, and which cue is *standing by* to be fired.

    Everything here lives on the message thread. The audio engine never touches a CueList;
    it is handed immutable snapshots instead.
*/
class CueList : public juce::ChangeBroadcaster
{
public:
    CueList() = default;

    //== Access ================================================================
    int size() const noexcept                       { return (int) cues.size(); }
    bool isEmpty() const noexcept                   { return cues.empty(); }

    Cue*       get (int index) noexcept;
    const Cue* get (int index) const noexcept;

    Cue*       findByID (const juce::Uuid& id) noexcept;
    const Cue* findByID (const juce::Uuid& id) const noexcept;
    int        indexOfID (const juce::Uuid& id) const noexcept;

    const std::vector<Cue>& all() const noexcept    { return cues; }

    //== Editing ===============================================================
    /** Inserts at @p index (or appends when out of range) and returns the new index. */
    int insert (Cue cue, int index = -1);

    void remove (int index);
    void removeByID (const juce::Uuid& id);

    /** Moves the cue at @p fromIndex so that it ends up at @p toIndex. */
    void move (int fromIndex, int toIndex);

    /** Replaces the cue at @p index, keeping its position. Fires a change message. */
    void replace (int index, Cue cue);

    /** Applies @p fn to the cue at @p index and fires a change message. Returns false if
        the index is out of range. */
    bool modify (int index, const std::function<void (Cue&)>& fn);
    bool modifyByID (const juce::Uuid& id, const std::function<void (Cue&)>& fn);

    void clear();

    /** Assigns the next free integer cue number, keeping the list tidy after an insert. */
    juce::String suggestNextNumber() const;

    //== Standby / selection ===================================================
    int  getSelectedIndex() const noexcept          { return selectedIndex; }
    void setSelectedIndex (int index);

    int  getStandbyIndex() const noexcept           { return standbyIndex; }

    /** Which step of the standby cue's lifecycle GO would perform next. */
    int  getStandbyStep() const noexcept            { return standbyStep; }

    /** Moves standby to a cue, at the first step of its lifecycle. */
    void setStandbyIndex (int index);

    /** Moves standby to a particular step of a particular cue. */
    void setStandbyPosition (int index, int step);

    /** Advances to the next step of the standby cue, or to the first step of the next cue
        when its lifecycle is finished. Stops at the end of the list. */
    void advanceStandby();

    /** The cue that GO would act on, or nullptr when the list has run out. */
    const Cue* getStandbyCue() const noexcept;

    /** The step list for the cue at @p index. Empty if there is no such cue. */
    std::vector<CueStep> stepsFor (int index) const;

    /** The step GO would perform, or nullopt when there is nothing standing by. */
    std::optional<CueStep> getStandbyStepInfo() const;

    /** Resolves a link's target: the explicit cue if set, otherwise the one after
        @p fromIndex. Returns nullptr when nothing follows. */
    const Cue* resolveLinkTarget (int fromIndex) const noexcept;

    //== Persistence ===========================================================
    juce::var toVar (const juce::File& showDirectory) const;
    void      restoreFromVar (const juce::var& v, const juce::File& showDirectory);

private:
    std::vector<Cue> cues;
    int selectedIndex { -1 };
    int standbyIndex  { -1 };
    int standbyStep   { 0 };

    void clampPositions();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CueList)
};

} // namespace cp
