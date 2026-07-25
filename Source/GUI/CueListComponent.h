#pragma once

#include "Audio/AudioEngine.h"
#include "GUI/LookAndFeel.h"
#include "Model/CueList.h"

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

    void refresh();
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
        columnStatus = 1,
        columnNumber,
        columnName,
        columnFile,
        columnPreWait,
        columnLength,
        columnFades,
        columnLoop,
        columnLink
    };

    CueList& cueList;
    AudioEngine& audioEngine;
    juce::TableListBox table;
    std::vector<AudioEngine::ActiveCueInfo> activeSnapshot;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CueListComponent)
};

} // namespace cp
