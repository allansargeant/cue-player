#pragma once

#include "Audio/AudioEngine.h"
#include "Control/ControlHub.h"
#include "GUI/RoutingMatrixComponent.h"
#include "GUI/WaveformComponent.h"
#include "Model/CueList.h"

namespace cp
{

/** Editor for the selected cue: source, trim, fades, repeat behaviour, link and routing.

    Laid out as one scrolling panel rather than tabs. An operator adjusting a fade wants to
    see the loop and link settings that fade interacts with at the same time, and hiding
    them behind a tab is how cues end up doing something nobody expected on the night.
*/
class CueInspector : public  juce::Component,
                     private juce::ChangeListener
{
public:
    CueInspector (CueList& list, AudioEngine& engine, ControlHub& hub,
                  juce::AudioFormatManager& formats);
    ~CueInspector() override;

    void resized() override;
    void paint (juce::Graphics&) override;

    /** Shows the cue at @p index, or clears the panel when out of range. */
    void setCueIndex (int index);
    int  getCueIndex() const noexcept { return cueIndex; }

    /** Refreshes the values shown without rebuilding anything. */
    void refresh();

    /** Moves the waveform play head. Pass a negative value to hide it. */
    void setPlayheadTime (double seconds) { waveform.setPlayheadTime (seconds); }

    /** Called after any edit, so the show can be marked dirty and the list redrawn. */
    std::function<void()> onCueEdited;

    /** Called when the operator asks to pick an audio file for the current cue. */
    std::function<void (int index)> onFileRequested;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    /** Applies @p fn to the current cue and pushes the change through the model. */
    void editCue (const std::function<void (Cue&)>& fn);

    void buildControls();
    void updateLinkTargets();
    void updateEnablement();
    void updateRoutingMatrix();
    void updateMessageList();
    void editMessage (int index);
    void pushSourceInfoToWaveform();

    /** A labelled control on one row of the layout. */
    struct Row
    {
        juce::Label label;
        juce::Component* control { nullptr };
        int span { 1 };
    };

    juce::Label& addSection (const juce::String& title);
    void addRow (const juce::String& labelText, juce::Component& control, int span = 1);

    CueList& cueList;
    AudioEngine& audioEngine;
    ControlHub& controlHub;
    juce::AudioFormatManager& formats;
    int cueIndex { -1 };
    bool updating { false };

    juce::Viewport viewport;
    juce::Component content;

    WaveformComponent waveform;
    RoutingMatrixComponent routingMatrix;

    juce::TextButton auditionButton { "Audition" };
    juce::TextButton chooseFileButton { "Choose file..." };

    juce::TextEditor numberEditor, nameEditor, notesEditor;
    juce::Slider gainSlider, preWaitSlider;
    juce::Slider inPointSlider, outPointSlider;
    juce::Slider fadeInSlider, fadeOutSlider;
    juce::ComboBox fadeInShapeBox, fadeOutShapeBox;
    juce::ToggleButton loopToggle { "Loop the whole cue" };
    juce::Slider loopCountSlider;
    juce::ToggleButton vampToggle { "Vamp a section until released" };
    juce::Slider vampStartSlider, vampEndSlider;
    juce::ComboBox vampReleaseBox;
    juce::ComboBox linkModeBox, linkTargetBox, linkShapeBox;
    juce::Slider linkDelaySlider, linkDurationSlider;

    // Streaming cues
    juce::ComboBox streamProviderBox, streamPathBox;
    juce::TextEditor streamUriEditor, streamNameEditor;
    juce::Slider streamInputSlider, streamChannelsSlider;
    juce::ToggleButton streamShuffleToggle { "Shuffle" };
    juce::ToggleButton streamRepeatToggle { "Repeat" };

    /** Outgoing MIDI/OSC messages for the current cue. */
    struct MessageListModel : public juce::ListBoxModel
    {
        juce::StringArray items;
        std::function<void (int)> onDoubleClick;

        int getNumRows() override { return items.size(); }
        void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    };

    MessageListModel messageModel;
    juce::ListBox messageList;
    juce::TextButton messageAddButton { "Add" }, messageEditButton { "Edit" },
                     messageRemoveButton { "Remove" }, messageTestButton { "Test" };
    juce::Component messagePanel;

    std::vector<std::unique_ptr<juce::Label>> sectionLabels;
    /** Index into `rows` of the first row belonging to each section, so resized() never
        has to carry a hand-maintained tally that silently rots when a control is added. */
    std::vector<size_t> sectionRowStart;

    /** Which section each free-standing panel belongs under, so resized() can place them
        inline with their heading rather than after every other section. */
    size_t messageSectionIndex { 0 };
    size_t routingSectionIndex { 0 };
    std::vector<std::unique_ptr<Row>> rows;
    std::vector<juce::Component*> streamingOnly, fileOnly;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CueInspector)
};

} // namespace cp
