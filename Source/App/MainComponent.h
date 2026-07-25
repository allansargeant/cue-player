#pragma once

#include "App/CommandIDs.h"
#include "Audio/AudioEngine.h"
#include "Audio/SampleCache.h"
#include "GUI/ActiveCuesComponent.h"
#include "GUI/AudioSetupWindow.h"
#include "GUI/CueInspector.h"
#include "GUI/CueListComponent.h"
#include "GUI/TransportBar.h"
#include "Model/Show.h"

namespace cp
{

/** The application window's contents: owns the show, the engine and the whole UI. */
class MainComponent : public  juce::Component,
                      public  juce::MenuBarModel,
                      public  juce::ApplicationCommandTarget,
                      public  juce::FileDragAndDropTarget,
                      private juce::ChangeListener,
                      private juce::Timer
{
public:
    explicit MainComponent (juce::ApplicationProperties& properties);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Opens @p file, prompting first if the current show has unsaved changes. */
    void openShowFile (const juce::File& file);

    /** Calls @p callback with true when it is safe to throw the current show away. */
    void confirmDiscardChanges (std::function<void (bool)> callback);

    //== MenuBarModel ==========================================================
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int index, const juce::String& name) override;
    void menuItemSelected (int menuItemID, int topLevelMenuIndex) override;

    //== ApplicationCommandTarget ==============================================
    ApplicationCommandTarget* getNextCommandTarget() override;
    void getAllCommands (juce::Array<juce::CommandID>&) override;
    void getCommandInfo (juce::CommandID, juce::ApplicationCommandInfo&) override;
    bool perform (const InvocationInfo&) override;

    //== FileDragAndDropTarget =================================================
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void newShow();
    void openShow();
    void saveShow (bool forceChooseFile);
    void addCueFromFile (const juce::File& file, int insertAt = -1);
    void addStreamingCue();
    void chooseFileForCue (int index);
    void scanFileInto (Cue& cue, const juce::File& file);
    void deleteSelectedCue();
    void duplicateSelectedCue();
    void moveSelectedCue (int delta);
    void renumberCues();
    void showAudioSetup();
    void preloadShowAudio();
    void updateWindowTitle();
    void reportError (const juce::String& message);

    juce::ApplicationProperties& properties;

    SampleCache sampleCache;
    AudioEngine audioEngine { sampleCache };
    Show show;

    TransportBar transportBar { audioEngine };
    CueListComponent cueListComponent { show.getCueList(), audioEngine };
    CueInspector inspector { show.getCueList(), audioEngine, sampleCache.getFormatManager() };
    ActiveCuesComponent activeCues { audioEngine };

    juce::StretchableLayoutManager verticalLayout;
    juce::StretchableLayoutResizerBar verticalResizer { &verticalLayout, 1, false };

    juce::ApplicationCommandManager commandManager;
    std::unique_ptr<juce::FileChooser> fileChooser;
    std::unique_ptr<AudioSetupWindow> audioSetupWindow;
    juce::TooltipWindow tooltips { this, 700 };

    static constexpr int activeCuesWidth = 300;
    static constexpr int transportHeight = 68;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace cp
