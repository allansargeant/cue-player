#include "App/MainComponent.h"

namespace cp
{

namespace
{
    const char* deviceStateKey = "audioDeviceState";
    const char* lastShowDirKey = "lastShowDirectory";
    const char* lastAudioDirKey = "lastAudioDirectory";
}

MainComponent::MainComponent (juce::ApplicationProperties& props)
    : properties (props)
{
    setOpaque (true);

    // ---- audio ---------------------------------------------------------------
    audioEngine.setCueList (&show.getCueList());

    std::unique_ptr<juce::XmlElement> savedState;

    if (auto* user = properties.getUserSettings())
        savedState = user->getXmlValue (deviceStateKey);

    if (const auto error = audioEngine.initialise (savedState.get()); error.isNotEmpty())
        reportError ("Audio device could not be opened.\n\n" + error);

    // ---- UI ------------------------------------------------------------------
    addAndMakeVisible (transportBar);
    addAndMakeVisible (cueListComponent);
    addAndMakeVisible (inspector);
    addAndMakeVisible (activeCues);
    addAndMakeVisible (verticalResizer);

    verticalLayout.setItemLayout (0, 120.0, -1.0, -0.55);   // cue list
    verticalLayout.setItemLayout (1, 6.0, 6.0, 6.0);        // resizer
    verticalLayout.setItemLayout (2, 160.0, -1.0, -0.45);   // inspector

    transportBar.onGo           = [this] { commandManager.invokeDirectly (CommandIDs::go, false); };
    transportBar.onStopAll      = [this] { audioEngine.stopAll (2.0); };
    transportBar.onPanic        = [this] { audioEngine.panic(); };
    transportBar.onPauseToggle  = [this] { audioEngine.setPaused (! audioEngine.isPaused()); };
    transportBar.onReleaseVamp  = [this] { audioEngine.releaseAllVamps(); };
    transportBar.onAudioSetup   = [this] { showAudioSetup(); };

    cueListComponent.onSelectionChanged = [this] (int index) { inspector.setCueIndex (index); };
    cueListComponent.onCueTriggered     = [this] (int index) { audioEngine.go (index); };
    cueListComponent.onFileRequested    = [this] (int index) { chooseFileForCue (index); };

    inspector.onCueEdited    = [this] { cueListComponent.refresh(); updateWindowTitle(); };
    inspector.onFileRequested = [this] (int index) { chooseFileForCue (index); };

    // ---- commands ------------------------------------------------------------
    commandManager.registerAllCommandsForTarget (this);
    addKeyListener (commandManager.getKeyMappings());
    setWantsKeyboardFocus (true);

    show.addChangeListener (this);
    sampleCache.addChangeListener (this);

    updateWindowTitle();
    startTimerHz (20);
    setSize (1280, 820);
}

MainComponent::~MainComponent()
{
    stopTimer();

    if (auto* user = properties.getUserSettings())
        if (auto state = audioEngine.createDeviceStateXml())
            user->setValue (deviceStateKey, state.get());

    sampleCache.removeChangeListener (this);
    show.removeChangeListener (this);
    removeKeyListener (commandManager.getKeyMappings());
    audioSetupWindow = nullptr;
    audioEngine.shutdown();
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
}

void MainComponent::resized()
{
    auto bounds = getLocalBounds();
    transportBar.setBounds (bounds.removeFromTop (transportHeight));
    activeCues.setBounds (bounds.removeFromRight (activeCuesWidth));

    Component* items[] = { &cueListComponent, &verticalResizer, &inspector };
    verticalLayout.layOutComponents (items, 3, bounds.getX(), bounds.getY(),
                                     bounds.getWidth(), bounds.getHeight(), true, true);
}

//==============================================================================
void MainComponent::timerCallback()
{
    const auto& list = show.getCueList();

    if (const auto* standby = list.getStandbyCue())
        transportBar.setStandbyText (standby->number,
                                     standby->name.isNotEmpty() ? standby->name : "(untitled)");
    else
        transportBar.setStandbyText ("--", list.isEmpty() ? "No cues" : "End of list");

    juce::StringArray status;
    status.add (show.getTitle() + (show.hasUnsavedChanges() ? " *" : ""));

    if (audioEngine.getSampleRate() > 0.0)
        status.add (juce::String (audioEngine.getSampleRate() / 1000.0, 1) + " kHz  "
                    + juce::String (audioEngine.getNumOutputChannels()) + " out");
    else
        status.add ("No audio device");

    if (const auto pending = sampleCache.getNumPending(); pending > 0)
        status.add ("loading " + juce::String (pending));
    else
        status.add (juce::String (sampleCache.getMemoryUsage() / (1024 * 1024)) + " MB loaded");

    transportBar.setShowStatus (status.joinIntoString ("   |   "));

    // Track the play head of the selected cue on the waveform, if it happens to be running.
    if (const auto* selected = list.get (list.getSelectedIndex()))
    {
        double playhead = -1.0;

        for (const auto& active : audioEngine.getActiveCues())
            if (active.cueId == selected->id && ! active.inPreWait)
                playhead = active.position;

        inspector.setPlayheadTime (playhead);
    }
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &show)
        updateWindowTitle();

    cueListComponent.refresh();
}

void MainComponent::updateWindowTitle()
{
    if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
        window->setName ("Cue Player  -  " + show.getTitle()
                         + (show.hasUnsavedChanges() ? " *" : ""));
}

void MainComponent::reportError (const juce::String& message)
{
    juce::NativeMessageBox::showAsync (
        juce::MessageBoxOptions()
            .withIconType (juce::MessageBoxIconType::WarningIcon)
            .withTitle ("Cue Player")
            .withMessage (message)
            .withButton ("OK")
            .withAssociatedComponent (this),
        nullptr);
}

//==============================================================================
void MainComponent::confirmDiscardChanges (std::function<void (bool)> callback)
{
    if (! show.hasUnsavedChanges())
    {
        callback (true);
        return;
    }

    juce::NativeMessageBox::showAsync (
        juce::MessageBoxOptions()
            .withIconType (juce::MessageBoxIconType::QuestionIcon)
            .withTitle ("Unsaved changes")
            .withMessage ("\"" + show.getTitle() + "\" has changes that have not been saved.")
            .withButton ("Save")
            .withButton ("Discard")
            .withButton ("Cancel")
            .withAssociatedComponent (this),
        [this, callback] (int result)
        {
            if (result == 1)          // Save
            {
                if (show.getFile() == juce::File())
                {
                    // Needs a filename first, so this cannot complete synchronously.
                    saveShow (true);
                    callback (false);
                    return;
                }

                if (const auto error = show.save(); error.isNotEmpty())
                {
                    reportError (error);
                    callback (false);
                    return;
                }

                callback (true);
            }
            else if (result == 2)     // Discard
            {
                callback (true);
            }
            else                      // Cancel
            {
                callback (false);
            }
        });
}

void MainComponent::newShow()
{
    confirmDiscardChanges ([this] (bool proceed)
    {
        if (! proceed)
            return;

        audioEngine.panic();
        show.createNewShow();
        sampleCache.clear();
        inspector.setCueIndex (-1);
        cueListComponent.refresh();
        updateWindowTitle();
    });
}

void MainComponent::openShow()
{
    confirmDiscardChanges ([this] (bool proceed)
    {
        if (! proceed)
            return;

        juce::File startIn;

        if (auto* user = properties.getUserSettings())
            startIn = juce::File (user->getValue (lastShowDirKey));

        fileChooser = std::make_unique<juce::FileChooser> ("Open show", startIn, Show::fileWildcard());
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                      | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& chooser)
            {
                const auto file = chooser.getResult();

                if (file.existsAsFile())
                    openShowFile (file);
            });
    });
}

void MainComponent::openShowFile (const juce::File& file)
{
    audioEngine.panic();

    if (const auto error = show.load (file); error.isNotEmpty())
    {
        reportError (error);
        return;
    }

    if (auto* user = properties.getUserSettings())
        user->setValue (lastShowDirKey, file.getParentDirectory().getFullPathName());

    audioEngine.setMasterGainDb (show.getMasterGainDb());
    sampleCache.retainOnly (show.collectAudioFiles());
    preloadShowAudio();

    inspector.setCueIndex (show.getCueList().getSelectedIndex());
    cueListComponent.refresh();
    updateWindowTitle();

    if (const auto missing = show.findMissingFiles(); ! missing.isEmpty())
        reportError (juce::String (missing.size())
                     + (missing.size() == 1 ? " cue refers to an audio file that is missing."
                                            : " cues refer to audio files that are missing.")
                     + "\n\nThey are marked MISSING in the cue list. Re-point them before the show.");
}

void MainComponent::saveShow (bool forceChooseFile)
{
    if (! forceChooseFile && show.getFile() != juce::File())
    {
        show.setMasterGainDb (audioEngine.getMasterGainDb());

        if (const auto error = show.save(); error.isNotEmpty())
            reportError (error);

        updateWindowTitle();
        return;
    }

    juce::File startIn;

    if (auto* user = properties.getUserSettings())
        startIn = juce::File (user->getValue (lastShowDirKey));

    fileChooser = std::make_unique<juce::FileChooser> ("Save show as", startIn, Show::fileWildcard());
    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                  | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
        [this] (const juce::FileChooser& chooser)
        {
            auto file = chooser.getResult();

            if (file == juce::File())
                return;

            if (! file.hasFileExtension (Show::fileExtension()))
                file = file.withFileExtension (Show::fileExtension());

            show.setMasterGainDb (audioEngine.getMasterGainDb());

            if (const auto error = show.save (file); error.isNotEmpty())
            {
                reportError (error);
                return;
            }

            if (auto* user = properties.getUserSettings())
                user->setValue (lastShowDirKey, file.getParentDirectory().getFullPathName());

            updateWindowTitle();
        });
}

//==============================================================================
void MainComponent::scanFileInto (Cue& cue, const juce::File& file)
{
    cue.audioFile = file;
    cue.fileDuration = 0.0;
    cue.fileChannels = 0;
    cue.fileSampleRate = 0.0;

    std::unique_ptr<juce::AudioFormatReader> reader (
        sampleCache.getFormatManager().createReaderFor (file));

    if (reader == nullptr)
        return;

    cue.fileSampleRate = reader->sampleRate;
    cue.fileChannels   = (int) reader->numChannels;
    cue.fileDuration   = reader->sampleRate > 0.0
                             ? (double) reader->lengthInSamples / reader->sampleRate : 0.0;

    // A fresh file gets the whole of itself as its region; trims are the operator's to make.
    cue.startTime = 0.0;
    cue.endTime   = 0.0;

    if (cue.name.isEmpty())
        cue.name = file.getFileNameWithoutExtension();
}

void MainComponent::addCueFromFile (const juce::File& file, int insertAt)
{
    Cue cue;
    cue.number = show.getCueList().suggestNextNumber();
    scanFileInto (cue, file);

    const auto index = show.getCueList().insert (std::move (cue), insertAt);
    show.getCueList().setSelectedIndex (index);

    if (show.getCueList().getStandbyIndex() < 0)
        show.getCueList().setStandbyIndex (0);

    sampleCache.request (file);
    inspector.setCueIndex (index);
    cueListComponent.selectRow (index);
}

void MainComponent::addStreamingCue()
{
    Cue cue;
    cue.type = CueType::streaming;
    cue.number = show.getCueList().suggestNextNumber();
    cue.name = "Streaming cue";
    cue.streaming.provider = "spotify";
    cue.streaming.audioPath = StreamingAudioPath::localCapture;

    const auto index = show.getCueList().insert (std::move (cue));
    show.getCueList().setSelectedIndex (index);
    inspector.setCueIndex (index);
    cueListComponent.selectRow (index);

    if (! audioEngine.areInputChannelsEnabled())
        reportError ("Streaming cues capture the service's audio from a loopback input.\n\n"
                     "Turn inputs on in Audio setup, point Spotify or TIDAL at a loopback "
                     "device (BlackHole, VB-Cable, or a PipeWire/JACK sink), then pick that "
                     "device's channels on the cue.");
}

void MainComponent::chooseFileForCue (int index)
{
    juce::File startIn;

    if (auto* user = properties.getUserSettings())
        startIn = juce::File (user->getValue (lastAudioDirKey));

    fileChooser = std::make_unique<juce::FileChooser> ("Choose audio for this cue", startIn,
                                                       sampleCache.getWildcardFilter());
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectFiles,
        [this, index] (const juce::FileChooser& chooser)
        {
            const auto file = chooser.getResult();

            if (! file.existsAsFile())
                return;

            if (auto* user = properties.getUserSettings())
                user->setValue (lastAudioDirKey, file.getParentDirectory().getFullPathName());

            show.getCueList().modify (index, [this, file] (Cue& cue) { scanFileInto (cue, file); });
            sampleCache.request (file);
            inspector.refresh();
            cueListComponent.refresh();
        });
}

void MainComponent::deleteSelectedCue()
{
    auto& list = show.getCueList();
    const auto index = list.getSelectedIndex();

    if (const auto* cue = list.get (index))
    {
        audioEngine.stopCue (cue->id, 0.0);
        list.remove (index);
        inspector.setCueIndex (list.getSelectedIndex());
        cueListComponent.refresh();
    }
}

void MainComponent::duplicateSelectedCue()
{
    auto& list = show.getCueList();
    const auto index = list.getSelectedIndex();

    if (const auto* original = list.get (index))
    {
        Cue copy = *original;
        copy.id = juce::Uuid();                     // A duplicate is a different cue.
        copy.number = list.suggestNextNumber();
        copy.name = original->name + " copy";

        const auto newIndex = list.insert (std::move (copy), index + 1);
        list.setSelectedIndex (newIndex);
        inspector.setCueIndex (newIndex);
        cueListComponent.selectRow (newIndex);
    }
}

void MainComponent::moveSelectedCue (int delta)
{
    auto& list = show.getCueList();
    const auto from = list.getSelectedIndex();
    const auto to = from + delta;

    if (from < 0 || ! juce::isPositiveAndBelow (to, list.size()))
        return;

    list.move (from, to);
    list.setSelectedIndex (to);
    inspector.setCueIndex (to);
    cueListComponent.selectRow (to);
}

void MainComponent::renumberCues()
{
    auto& list = show.getCueList();

    for (int i = 0; i < list.size(); ++i)
        list.modify (i, [i] (Cue& cue) { cue.number = juce::String (i + 1); });

    cueListComponent.refresh();
    inspector.refresh();
}

void MainComponent::showAudioSetup()
{
    if (audioSetupWindow != nullptr)
    {
        audioSetupWindow->toFront (true);
        return;
    }

    audioSetupWindow = std::make_unique<AudioSetupWindow> (audioEngine);
    audioSetupWindow->onClose = [this] { audioSetupWindow = nullptr; };
}

void MainComponent::preloadShowAudio()
{
    for (const auto& file : show.collectAudioFiles())
        sampleCache.request (file);
}

//==============================================================================
bool MainComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& path : files)
    {
        const juce::File file (path);

        if (file.hasFileExtension (Show::fileExtension()))
            return true;

        if (sampleCache.getFormatManager().findFormatForFileExtension (file.getFileExtension()) != nullptr)
            return true;
    }

    return false;
}

void MainComponent::filesDropped (const juce::StringArray& files, int, int)
{
    juce::StringArray audioFiles;

    for (const auto& path : files)
    {
        const juce::File file (path);

        if (file.hasFileExtension (Show::fileExtension()))
        {
            openShowFile (file);
            return;
        }

        if (sampleCache.getFormatManager().findFormatForFileExtension (file.getFileExtension()) != nullptr)
            audioFiles.add (path);
    }

    // Drop order is what the operator sees, so keep it rather than sorting.
    for (const auto& path : audioFiles)
        addCueFromFile (juce::File (path));
}

//==============================================================================
juce::StringArray MainComponent::getMenuBarNames()
{
    return { "File", "Cue", "Transport", "Audio" };
}

juce::PopupMenu MainComponent::getMenuForIndex (int index, const juce::String&)
{
    juce::PopupMenu menu;

    switch (index)
    {
        case 0:
            menu.addCommandItem (&commandManager, CommandIDs::newShow);
            menu.addCommandItem (&commandManager, CommandIDs::openShow);
            menu.addSeparator();
            menu.addCommandItem (&commandManager, CommandIDs::saveShow);
            menu.addCommandItem (&commandManager, CommandIDs::saveShowAs);
            break;

        case 1:
            menu.addCommandItem (&commandManager, CommandIDs::addCue);
            menu.addCommandItem (&commandManager, CommandIDs::addStreamingCue);
            menu.addSeparator();
            menu.addCommandItem (&commandManager, CommandIDs::duplicateCue);
            menu.addCommandItem (&commandManager, CommandIDs::deleteCue);
            menu.addSeparator();
            menu.addCommandItem (&commandManager, CommandIDs::moveCueUp);
            menu.addCommandItem (&commandManager, CommandIDs::moveCueDown);
            menu.addCommandItem (&commandManager, CommandIDs::renumberCues);
            break;

        case 2:
            menu.addCommandItem (&commandManager, CommandIDs::go);
            menu.addCommandItem (&commandManager, CommandIDs::releaseVamp);
            menu.addSeparator();
            menu.addCommandItem (&commandManager, CommandIDs::pauseResume);
            menu.addCommandItem (&commandManager, CommandIDs::stopAll);
            menu.addCommandItem (&commandManager, CommandIDs::panic);
            menu.addSeparator();
            menu.addCommandItem (&commandManager, CommandIDs::auditionCue);
            menu.addCommandItem (&commandManager, CommandIDs::setStandbyToSelected);
            menu.addCommandItem (&commandManager, CommandIDs::standbyPrevious);
            menu.addCommandItem (&commandManager, CommandIDs::standbyNext);
            break;

        case 3:
            menu.addCommandItem (&commandManager, CommandIDs::showAudioSetup);
            break;

        default:
            break;
    }

    return menu;
}

void MainComponent::menuItemSelected (int, int) {}

//==============================================================================
juce::ApplicationCommandTarget* MainComponent::getNextCommandTarget()
{
    return findFirstTargetParentComponent();
}

void MainComponent::getAllCommands (juce::Array<juce::CommandID>& commands)
{
    commands.addArray ({
        CommandIDs::newShow, CommandIDs::openShow, CommandIDs::saveShow, CommandIDs::saveShowAs,
        CommandIDs::addCue, CommandIDs::addStreamingCue, CommandIDs::duplicateCue,
        CommandIDs::deleteCue, CommandIDs::moveCueUp, CommandIDs::moveCueDown,
        CommandIDs::renumberCues,
        CommandIDs::go, CommandIDs::stopAll, CommandIDs::panic, CommandIDs::pauseResume,
        CommandIDs::releaseVamp, CommandIDs::auditionCue,
        CommandIDs::setStandbyToSelected, CommandIDs::standbyPrevious, CommandIDs::standbyNext,
        CommandIDs::showAudioSetup });
}

void MainComponent::getCommandInfo (juce::CommandID commandID, juce::ApplicationCommandInfo& info)
{
    using juce::KeyPress;
    const auto cmd = juce::ModifierKeys::commandModifier;
    const auto shiftCmd = juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier;

    const auto hasSelection = show.getCueList().getSelectedIndex() >= 0;

    switch (commandID)
    {
        case CommandIDs::newShow:
            info.setInfo ("New show", "Start an empty show", "File", 0);
            info.addDefaultKeypress ('n', cmd);
            break;

        case CommandIDs::openShow:
            info.setInfo ("Open show...", "Open an existing show", "File", 0);
            info.addDefaultKeypress ('o', cmd);
            break;

        case CommandIDs::saveShow:
            info.setInfo ("Save show", "Save the current show", "File", 0);
            info.addDefaultKeypress ('s', cmd);
            break;

        case CommandIDs::saveShowAs:
            info.setInfo ("Save show as...", "Save under a new name", "File", 0);
            info.addDefaultKeypress ('s', shiftCmd);
            break;

        case CommandIDs::addCue:
            info.setInfo ("Add audio cue...", "Add a cue from an audio file", "Cue", 0);
            info.addDefaultKeypress ('e', cmd);
            break;

        case CommandIDs::addStreamingCue:
            info.setInfo ("Add streaming cue", "Add a cue that plays from a streaming service", "Cue", 0);
            info.addDefaultKeypress ('e', shiftCmd);
            break;

        case CommandIDs::duplicateCue:
            info.setInfo ("Duplicate cue", "Copy the selected cue", "Cue", 0);
            info.addDefaultKeypress ('d', cmd);
            info.setActive (hasSelection);
            break;

        case CommandIDs::deleteCue:
            info.setInfo ("Delete cue", "Remove the selected cue", "Cue", 0);
            info.addDefaultKeypress (KeyPress::backspaceKey, cmd);
            info.setActive (hasSelection);
            break;

        case CommandIDs::moveCueUp:
            info.setInfo ("Move cue up", "Move the selected cue earlier", "Cue", 0);
            info.addDefaultKeypress (KeyPress::upKey, cmd);
            info.setActive (hasSelection);
            break;

        case CommandIDs::moveCueDown:
            info.setInfo ("Move cue down", "Move the selected cue later", "Cue", 0);
            info.addDefaultKeypress (KeyPress::downKey, cmd);
            info.setActive (hasSelection);
            break;

        case CommandIDs::renumberCues:
            info.setInfo ("Renumber all cues", "Number the cues 1, 2, 3 in list order", "Cue", 0);
            break;

        case CommandIDs::go:
            info.setInfo ("GO", "Fire the standby cue", "Transport", 0);
            info.addDefaultKeypress (KeyPress::spaceKey, 0);
            break;

        case CommandIDs::releaseVamp:
            info.setInfo ("Release vamp", "Let every vamping cue continue", "Transport", 0);
            info.addDefaultKeypress (KeyPress::returnKey, 0);
            info.setActive (audioEngine.isAnythingVamping());
            break;

        case CommandIDs::pauseResume:
            info.setInfo (audioEngine.isPaused() ? "Resume" : "Pause",
                          "Freeze or resume everything that is playing", "Transport", 0);
            info.addDefaultKeypress ('p', cmd);
            break;

        case CommandIDs::stopAll:
            info.setInfo ("Stop all", "Fade everything out over two seconds", "Transport", 0);
            info.addDefaultKeypress ('.', cmd);
            break;

        case CommandIDs::panic:
            info.setInfo ("PANIC", "Silence everything immediately", "Transport", 0);
            info.addDefaultKeypress (KeyPress::escapeKey, 0);
            break;

        case CommandIDs::auditionCue:
            info.setInfo ("Audition selected cue", "Listen to the selected cue without firing it",
                          "Transport", 0);
            info.addDefaultKeypress ('\'', 0);
            info.setActive (hasSelection);
            break;

        case CommandIDs::setStandbyToSelected:
            info.setInfo ("Standby the selected cue", "Point GO at the selected cue", "Transport", 0);
            info.addDefaultKeypress (KeyPress::returnKey, cmd);
            info.setActive (hasSelection);
            break;

        case CommandIDs::standbyPrevious:
            info.setInfo ("Standby previous cue", "Step the standby marker back", "Transport", 0);
            info.addDefaultKeypress (KeyPress::upKey, shiftCmd);
            break;

        case CommandIDs::standbyNext:
            info.setInfo ("Standby next cue", "Step the standby marker forward", "Transport", 0);
            info.addDefaultKeypress (KeyPress::downKey, shiftCmd);
            break;

        case CommandIDs::showAudioSetup:
            info.setInfo ("Audio setup...", "Choose the audio device and channels", "Audio", 0);
            info.addDefaultKeypress (',', cmd);
            break;

        default:
            break;
    }
}

bool MainComponent::perform (const InvocationInfo& info)
{
    auto& list = show.getCueList();

    switch (info.commandID)
    {
        case CommandIDs::newShow:     newShow(); return true;
        case CommandIDs::openShow:    openShow(); return true;
        case CommandIDs::saveShow:    saveShow (false); return true;
        case CommandIDs::saveShowAs:  saveShow (true); return true;

        case CommandIDs::addCue:
        {
            juce::File startIn;

            if (auto* user = properties.getUserSettings())
                startIn = juce::File (user->getValue (lastAudioDirKey));

            fileChooser = std::make_unique<juce::FileChooser> ("Add audio cues", startIn,
                                                               sampleCache.getWildcardFilter());
            fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                          | juce::FileBrowserComponent::canSelectFiles
                                          | juce::FileBrowserComponent::canSelectMultipleItems,
                [this] (const juce::FileChooser& chooser)
                {
                    for (const auto& file : chooser.getResults())
                    {
                        if (auto* user = properties.getUserSettings())
                            user->setValue (lastAudioDirKey, file.getParentDirectory().getFullPathName());

                        addCueFromFile (file);
                    }
                });

            return true;
        }

        case CommandIDs::addStreamingCue: addStreamingCue(); return true;
        case CommandIDs::duplicateCue:    duplicateSelectedCue(); return true;
        case CommandIDs::deleteCue:       deleteSelectedCue(); return true;
        case CommandIDs::moveCueUp:       moveSelectedCue (-1); return true;
        case CommandIDs::moveCueDown:     moveSelectedCue (1); return true;
        case CommandIDs::renumberCues:    renumberCues(); return true;

        case CommandIDs::go:
            if (! audioEngine.goStandby())
                if (const auto error = audioEngine.getLastError(); error.isNotEmpty())
                    reportError (error);

            cueListComponent.refresh();
            return true;

        case CommandIDs::stopAll:     audioEngine.stopAll (2.0); return true;
        case CommandIDs::panic:       audioEngine.panic(); return true;
        case CommandIDs::pauseResume: audioEngine.setPaused (! audioEngine.isPaused()); return true;
        case CommandIDs::releaseVamp: audioEngine.releaseAllVamps(); return true;

        case CommandIDs::auditionCue:
            if (const auto* cue = list.get (list.getSelectedIndex()))
                if (! audioEngine.audition (*cue, cue->startTime))
                    reportError (audioEngine.getLastError());

            return true;

        case CommandIDs::setStandbyToSelected:
            list.setStandbyIndex (list.getSelectedIndex());
            return true;

        case CommandIDs::standbyPrevious:
            list.setStandbyIndex (list.getStandbyIndex() - 1);
            return true;

        case CommandIDs::standbyNext:
            list.setStandbyIndex (list.getStandbyIndex() + 1);
            return true;

        case CommandIDs::showAudioSetup: showAudioSetup(); return true;

        default:
            return false;
    }
}

} // namespace cp
