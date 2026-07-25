#pragma once

#include "Audio/AudioEngine.h"
#include "GUI/LookAndFeel.h"
#include "Model/CueList.h"

#include <set>

namespace cp
{

/** The cue list: the operator's main working surface.

    Shows the standby marker, which cues are sounding, and the loop/vamp/link settings that
    decide what happens after a GO — all of which have to be readable at a glance from a
    couple of feet away in the dark.
*/
class CueListComponent : public  juce::Component,
                         private juce::TableListBoxModel,
                         private juce::ChangeListener
{
public:
    CueListComponent (CueList& list, AudioEngine& engine);
    ~CueListComponent() override;

    void resized() override;

    /** Called when the operator picks a different cue to edit. */
    std::function<void (int index)> onSelectionChanged;

    /** Called on double-click / Enter: fire the cue directly. */
    std::function<void (int index)> onCueTriggered;

    /** Called when a cue's audio file should be chosen (double-click on an empty file). */
    std::function<void (int index)> onFileRequested;

    /** Called when the operator clicks a row's delete cross. */
    std::function<void (int cueIndex)> onCueDeleteRequested;

    void refresh();

    /** Scrolls to and selects the row for cue @p index (its header row). */
    void selectRow (int index);

private:
    //== TableListBoxModel =====================================================
    int  getNumRows() override;
    void paintRowBackground (juce::Graphics&, int row, int width, int height, bool rowIsSelected) override;
    void paintCell (juce::Graphics&, int row, int columnId, int width, int height, bool rowIsSelected) override;
    void cellClicked (int row, int columnId, const juce::MouseEvent&) override;
    void cellDoubleClicked (int row, int columnId, const juce::MouseEvent&) override;
    void selectedRowsChanged (int lastRowSelected) override;
    void backgroundClicked (const juce::MouseEvent&) override;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    juce::String describeLoop (const Cue&) const;
    juce::String describeLink (const Cue&) const;

    /** Playback status of @p cue, folded into one label and colour. */
    struct Status { juce::String label; juce::Colour colour; };
    Status statusFor (const Cue&) const;

    enum ColumnIds
    {
        columnDelete = 1,
        columnIcon,
        columnStatus,
        columnNumber,
        columnName,
        columnFile,
        columnPreWait,
        columnLength,
        columnFades,
        columnLoop,
        columnLink
    };


    /** One visible line: either a cue's header or one step of its lifecycle.

        The table is flat, so the tree is flattened into this list every refresh. That keeps
        expansion state and row identity in one obvious place instead of spread across the
        model. */
    struct DisplayRow
    {
        int cueIndex { -1 };
        int stepIndex { -1 };   ///< -1 for the cue's own header row.

        bool isHeader() const noexcept { return stepIndex < 0; }
    };

    void rebuildRows();
    bool isExpanded (const Cue& cue) const;
    void toggleExpansion (const Cue& cue);
    juce::Rectangle<int> getTwistyBounds (int width, int height) const;
    void drawDeleteCross (juce::Graphics&, juce::Rectangle<int>, bool highlighted) const;
    void drawTypeIcon (juce::Graphics&, juce::Rectangle<int>, const Cue&, bool isSubCue,
                       CueStepType stepType) const;

    /** Times read as 00:00.00 and drop back to a dim grey at zero, so the eye lands on the
        cues that actually wait rather than on a wall of identical zeroes. */
    void drawTimeCell (juce::Graphics&, juce::Rectangle<int>, double seconds) const;

    CueList& cueList;
    AudioEngine& audioEngine;
    juce::TableListBox table;
    std::vector<AudioEngine::ActiveCueInfo> activeSnapshot;
    std::vector<DisplayRow> displayRows;

    /** Cues the operator has opened by hand. The standby cue opens on its own while it is
        part-way through its lifecycle, so the steps are visible exactly when they matter. */
    std::set<juce::Uuid> manuallyExpanded;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CueListComponent)
};

} // namespace cp
