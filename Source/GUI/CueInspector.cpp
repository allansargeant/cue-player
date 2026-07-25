#include "GUI/CueInspector.h"

namespace cp
{

namespace
{
    constexpr int rowHeight = 24;
    constexpr int rowGap = 4;
    constexpr int labelWidth = 108;
    constexpr int sectionGap = 12;
    constexpr int waveformHeight = 118;
    constexpr int timeFieldBarHeight = 26;
}

CueInspector::CueInspector (CueList& list, AudioEngine& engine, ControlHub& hub,
                            juce::AudioFormatManager& formatManager)
    : cueList (list), audioEngine (engine), controlHub (hub), formats (formatManager),
      waveform (formatManager)
{
    addAndMakeVisible (waveform);
    buildTimeFields();
    addAndMakeVisible (timeFieldBar);
    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);

    buildControls();

    waveform.onMarkerMoved = [this] (WaveformComponent::Marker marker, double time, bool)
    {
        editCue ([marker, time] (Cue& cue)
        {
            switch (marker)
            {
                case WaveformComponent::Marker::in:
                    cue.startTime = juce::jlimit (0.0, cue.resolvedEndTime(), time);
                    break;

                case WaveformComponent::Marker::out:
                    cue.endTime = juce::jlimit (cue.startTime, cue.fileDuration, time);
                    break;

                case WaveformComponent::Marker::vampStart:
                    cue.vampStart = juce::jlimit (cue.startTime, cue.vampEnd, time);
                    break;

                case WaveformComponent::Marker::vampEnd:
                    cue.vampEnd = juce::jlimit (cue.vampStart, cue.resolvedEndTime(), time);
                    break;

                case WaveformComponent::Marker::none:
                default:
                    break;
            }
        });
    };

    waveform.onScrubRequested = [this] (double time)
    {
        if (const auto* cue = cueList.get (cueIndex))
            audioEngine.audition (*cue, time);
    };

    cueList.addChangeListener (this);
}

CueInspector::~CueInspector()
{
    cueList.removeChangeListener (this);
}

void CueInspector::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (cueIndex != cueList.getSelectedIndex())
        setCueIndex (cueList.getSelectedIndex());
    else
        refresh();
}

//==============================================================================
juce::Label& CueInspector::addSection (const juce::String& title)
{
    auto label = std::make_unique<juce::Label> (juce::String(), title.toUpperCase());
    label->setFont (juce::FontOptions (11.0f, juce::Font::bold));
    label->setColour (juce::Label::textColourId, colours::textDim);
    content.addAndMakeVisible (*label);

    auto& ref = *label;
    sectionLabels.push_back (std::move (label));
    sectionRowStart.push_back (rows.size());
    return ref;
}

void CueInspector::addRow (const juce::String& labelText, juce::Component& control, int span)
{
    auto row = std::make_unique<Row>();
    row->label.setText (labelText, juce::dontSendNotification);
    row->label.setFont (juce::FontOptions (12.0f));
    row->label.setJustificationType (juce::Justification::centredRight);
    row->label.setColour (juce::Label::textColourId, colours::textDim);
    row->control = &control;
    row->span = span;

    content.addAndMakeVisible (row->label);
    content.addAndMakeVisible (control);
    rows.push_back (std::move (row));
}

void CueInspector::buildControls()
{
    const auto configureTimeSlider = [] (juce::Slider& s, double max, const juce::String& suffix)
    {
        s.setSliderStyle (juce::Slider::LinearBar);
        s.setRange (0.0, max, 0.001);
        s.setNumDecimalPlacesToDisplay (3);
        s.setTextValueSuffix (suffix);
        s.setColour (juce::Slider::trackColourId, colours::panelLight);
    };

    // --- Source ---------------------------------------------------------------
    addSection ("Source");

    numberEditor.setMultiLine (false);
    numberEditor.onTextChange = [this] { editCue ([this] (Cue& c) { c.number = numberEditor.getText(); }); };
    addRow ("Cue number", numberEditor);

    nameEditor.setMultiLine (false);
    nameEditor.onTextChange = [this] { editCue ([this] (Cue& c) { c.name = nameEditor.getText(); }); };
    addRow ("Name", nameEditor);

    chooseFileButton.onClick = [this]
    {
        if (onFileRequested != nullptr && cueIndex >= 0)
            onFileRequested (cueIndex);
    };
    addRow ("Audio file", chooseFileButton);
    fileOnly.push_back (&chooseFileButton);

    notesEditor.setMultiLine (true, true);
    notesEditor.setReturnKeyStartsNewLine (true);
    notesEditor.onFocusLost = [this] { editCue ([this] (Cue& c) { c.notes = notesEditor.getText(); }); };
    addRow ("Notes", notesEditor, 2);

    gainSlider.setSliderStyle (juce::Slider::LinearBar);
    gainSlider.setRange (-60.0, 12.0, 0.1);
    gainSlider.setTextValueSuffix (" dB");
    gainSlider.onValueChange = [this] { editCue ([this] (Cue& c) { c.gainDb = gainSlider.getValue(); }); };
    addRow ("Gain", gainSlider);

    configureTimeSlider (preWaitSlider, 600.0, " s");
    preWaitSlider.onValueChange = [this] { editCue ([this] (Cue& c) { c.preWait = preWaitSlider.getValue(); }); };
    addRow ("Pre-wait", preWaitSlider);

    auditionButton.onClick = [this]
    {
        if (const auto* cue = cueList.get (cueIndex))
            audioEngine.audition (*cue, cue->startTime);
    };
    addRow ("", auditionButton);

    // --- Streaming ------------------------------------------------------------
    // Only what belongs to this cue. The account, the provider and the capture patch are
    // properties of the installation and live in Settings.
    addSection ("Streaming");
    streamingSectionIndex = sectionLabels.size() - 1;

    streamUriEditor.setMultiLine (false);
    streamUriEditor.setTextToShowWhenEmpty ("spotify:playlist:... or a pasted share link",
                                            colours::textDim);
    streamUriEditor.onFocusLost = [this]
    {
        editCue ([this] (Cue& c) { c.streaming.uri = streamUriEditor.getText().trim(); });
    };
    addRow ("Playlist / track", streamUriEditor, 2);
    streamingOnly.push_back (&streamUriEditor);

    streamNameEditor.setMultiLine (false);
    streamNameEditor.onTextChange = [this]
    {
        editCue ([this] (Cue& c) { c.streaming.displayName = streamNameEditor.getText(); });
    };
    addRow ("Shown as", streamNameEditor);
    streamingOnly.push_back (&streamNameEditor);

    streamShuffleToggle.onClick = [this]
    {
        editCue ([this] (Cue& c) { c.streaming.shuffle = streamShuffleToggle.getToggleState(); });
    };
    addRow ("", streamShuffleToggle);
    streamingOnly.push_back (&streamShuffleToggle);

    streamRepeatToggle.onClick = [this]
    {
        editCue ([this] (Cue& c) { c.streaming.repeat = streamRepeatToggle.getToggleState(); });
    };
    addRow ("", streamRepeatToggle);
    streamingOnly.push_back (&streamRepeatToggle);

    streamAccountLabel.setFont (juce::FontOptions (11.5f, juce::Font::italic));
    streamAccountLabel.setColour (juce::Label::textColourId, colours::textDim);
    addRow ("", streamAccountLabel, 2);
    streamingOnly.push_back (&streamAccountLabel);

    // --- Trim -----------------------------------------------------------------
    addSection ("In and out points");
    trimSectionIndex = sectionLabels.size() - 1;

    configureTimeSlider (inPointSlider, 3600.0, " s");
    inPointSlider.onValueChange = [this]
    {
        editCue ([this] (Cue& c)
        {
            c.startTime = juce::jlimit (0.0, c.resolvedEndTime(), inPointSlider.getValue());
        });
    };
    addRow ("In point", inPointSlider);
    fileOnly.push_back (&inPointSlider);

    configureTimeSlider (outPointSlider, 3600.0, " s");
    outPointSlider.onValueChange = [this]
    {
        editCue ([this] (Cue& c)
        {
            c.endTime = juce::jlimit (c.startTime, c.fileDuration, outPointSlider.getValue());
        });
    };
    addRow ("Out point", outPointSlider);
    fileOnly.push_back (&outPointSlider);

    // --- Fades ----------------------------------------------------------------
    addSection ("Fades");

    configureTimeSlider (fadeInSlider, 120.0, " s");
    fadeInSlider.onValueChange = [this] { editCue ([this] (Cue& c) { c.fadeInTime = fadeInSlider.getValue(); }); };
    addRow ("Fade in", fadeInSlider);

    fadeInShapeBox.addItemList (fadeShapeNames(), 1);
    fadeInShapeBox.onChange = [this]
    {
        editCue ([this] (Cue& c) { c.fadeInShape = (FadeShape) (fadeInShapeBox.getSelectedId() - 1); });
    };
    addRow ("Shape", fadeInShapeBox);

    configureTimeSlider (fadeOutSlider, 120.0, " s");
    fadeOutSlider.onValueChange = [this] { editCue ([this] (Cue& c) { c.fadeOutTime = fadeOutSlider.getValue(); }); };
    addRow ("Fade out", fadeOutSlider);

    fadeOutShapeBox.addItemList (fadeShapeNames(), 1);
    fadeOutShapeBox.onChange = [this]
    {
        editCue ([this] (Cue& c) { c.fadeOutShape = (FadeShape) (fadeOutShapeBox.getSelectedId() - 1); });
    };
    addRow ("Shape", fadeOutShapeBox);

    // --- Repeat ---------------------------------------------------------------
    addSection ("Loop and vamp");
    loopSectionIndex = sectionLabels.size() - 1;

    loopToggle.onClick = [this]
    {
        editCue ([this] (Cue& c) { c.loopEnabled = loopToggle.getToggleState(); });
        updateEnablement();
    };
    addRow ("", loopToggle, 2);
    fileOnly.push_back (&loopToggle);

    loopCountSlider.setSliderStyle (juce::Slider::LinearBar);
    loopCountSlider.setRange (0.0, 999.0, 1.0);
    loopCountSlider.textFromValueFunction = [] (double v)
    {
        return v <= 0.0 ? juce::String ("forever") : juce::String ((int) v) + " times";
    };
    loopCountSlider.onValueChange = [this]
    {
        editCue ([this] (Cue& c) { c.loopCount = (int) loopCountSlider.getValue(); });
    };
    addRow ("Play", loopCountSlider);
    fileOnly.push_back (&loopCountSlider);

    vampToggle.onClick = [this]
    {
        editCue ([this] (Cue& c)
        {
            c.vampEnabled = vampToggle.getToggleState();

            // Give a freshly enabled vamp a sensible region rather than a zero-length one
            // sitting at the head of the file, which would look broken.
            if (c.vampEnabled && c.vampEnd <= c.vampStart)
            {
                const auto length = c.trimmedLength();
                c.vampStart = c.startTime + length * 0.25;
                c.vampEnd   = c.startTime + length * 0.75;
            }
        });
        updateEnablement();
    };
    addRow ("", vampToggle, 2);
    fileOnly.push_back (&vampToggle);

    configureTimeSlider (vampStartSlider, 3600.0, " s");
    vampStartSlider.onValueChange = [this]
    {
        editCue ([this] (Cue& c)
        {
            c.vampStart = juce::jlimit (c.startTime, c.resolvedEndTime(), vampStartSlider.getValue());
        });
    };
    addRow ("Vamp from", vampStartSlider);
    fileOnly.push_back (&vampStartSlider);

    configureTimeSlider (vampEndSlider, 3600.0, " s");
    vampEndSlider.onValueChange = [this]
    {
        editCue ([this] (Cue& c)
        {
            c.vampEnd = juce::jlimit (c.startTime, c.resolvedEndTime(), vampEndSlider.getValue());
        });
    };
    addRow ("Vamp to", vampEndSlider);
    fileOnly.push_back (&vampEndSlider);

    vampReleaseBox.addItem ("Finish the current pass", 1);
    vampReleaseBox.addItem ("Leave immediately", 2);
    vampReleaseBox.onChange = [this]
    {
        const auto release = vampReleaseBox.getSelectedId() == 2 ? VampRelease::immediately
                                                                 : VampRelease::atEndOfPass;
        editCue ([release] (Cue& c) { c.vampRelease = release; });
    };
    addRow ("On release", vampReleaseBox, 2);
    fileOnly.push_back (&vampReleaseBox);

    // --- End of life ----------------------------------------------------------
    addSection ("Ending this cue");
    endSectionIndex = sectionLabels.size() - 1;

    endStepModeBox.addItemList (endStepModeNames(), 1);
    endStepModeBox.onChange = [this]
    {
        editCue ([this] (Cue& c)
                 { c.endStepMode = (EndStepMode) (endStepModeBox.getSelectedId() - 1); });
        updateEnablement();

        if (onCueEdited != nullptr)
            onCueEdited();
    };
    addRow ("End step", endStepModeBox, 2);

    endActionBox.addItemList (endActionNames(), 1);
    endActionBox.onChange = [this]
    {
        editCue ([this] (Cue& c) { c.endAction = (EndAction) (endActionBox.getSelectedId() - 1); });
        updateEnablement();
    };
    addRow ("Ends by", endActionBox);

    configureTimeSlider (endFadeSlider, 120.0, " s");
    endFadeSlider.onValueChange = [this]
    {
        editCue ([this] (Cue& c) { c.endFadeTime = endFadeSlider.getValue(); });
    };
    addRow ("Fade over", endFadeSlider);

    // --- Link -----------------------------------------------------------------
    addSection ("Link to the next cue");

    linkModeBox.addItemList (linkModeNames(), 1);
    linkModeBox.onChange = [this]
    {
        editCue ([this] (Cue& c) { c.link.mode = (LinkMode) (linkModeBox.getSelectedId() - 1); });
        updateEnablement();
    };
    addRow ("Mode", linkModeBox);

    linkTargetBox.onChange = [this]
    {
        const auto id = linkTargetBox.getSelectedId();

        if (id == 1)
        {
            editCue ([] (Cue& c) { c.link.target = juce::Uuid::null(); });
            return;
        }

        if (const auto* target = cueList.get (id - 2))
        {
            const auto targetId = target->id;
            editCue ([targetId] (Cue& c) { c.link.target = targetId; });
        }
    };
    addRow ("Target", linkTargetBox, 2);

    configureTimeSlider (linkDelaySlider, 600.0, " s");
    linkDelaySlider.onValueChange = [this]
    {
        editCue ([this] (Cue& c) { c.link.delay = linkDelaySlider.getValue(); });
    };
    addRow ("Delay", linkDelaySlider);

    configureTimeSlider (linkDurationSlider, 120.0, " s");
    linkDurationSlider.onValueChange = [this]
    {
        editCue ([this] (Cue& c) { c.link.duration = linkDurationSlider.getValue(); });
    };
    addRow ("Crossfade", linkDurationSlider);

    linkShapeBox.addItemList (fadeShapeNames(), 1);
    linkShapeBox.onChange = [this]
    {
        editCue ([this] (Cue& c) { c.link.shape = (FadeShape) (linkShapeBox.getSelectedId() - 1); });
    };
    addRow ("Curve", linkShapeBox);

    // --- Outgoing messages ----------------------------------------------------
    addSection ("Messages this cue sends");
    messageSectionIndex = sectionLabels.size() - 1;

    messageModel.onDoubleClick = [this] (int row) { editMessage (row); };
    messageList.setModel (&messageModel);
    messageList.setRowHeight (20);
    messageList.setColour (juce::ListBox::backgroundColourId, colours::panelLight);
    messagePanel.addAndMakeVisible (messageList);

    messageAddButton.onClick = [this]
    {
        editCue ([] (Cue& cue) { cue.outputMessages.push_back ({}); });
        updateMessageList();

        if (const auto* cue = cueList.get (cueIndex); cue != nullptr && ! cue->outputMessages.empty())
        {
            const auto last = (int) cue->outputMessages.size() - 1;
            messageList.selectRow (last);
            editMessage (last);
        }
    };

    messageEditButton.onClick = [this] { editMessage (messageList.getSelectedRow()); };

    messageRemoveButton.onClick = [this]
    {
        const auto row = messageList.getSelectedRow();

        editCue ([row] (Cue& cue)
        {
            if (juce::isPositiveAndBelow (row, (int) cue.outputMessages.size()))
                cue.outputMessages.erase (cue.outputMessages.begin() + row);
        });

        updateMessageList();
    };

    // Fires the message right now, so a patch can be proved before the show rather than
    // discovered to be wrong during it.
    messageTestButton.onClick = [this]
    {
        const auto row = messageList.getSelectedRow();

        if (const auto* cue = cueList.get (cueIndex);
            cue != nullptr && juce::isPositiveAndBelow (row, (int) cue->outputMessages.size()))
            controlHub.sendNow (cue->outputMessages[(size_t) row]);
    };

    messagePanel.addAndMakeVisible (messageAddButton);
    messagePanel.addAndMakeVisible (messageEditButton);
    messagePanel.addAndMakeVisible (messageRemoveButton);
    messagePanel.addAndMakeVisible (messageTestButton);
    content.addAndMakeVisible (messagePanel);

    // --- Routing --------------------------------------------------------------
    addSection ("Output routing");
    routingSectionIndex = sectionLabels.size() - 1;
    routingSectionIndexForVisibility = routingSectionIndex;

    routingMatrix.onRoutingChanged = [this] (const std::vector<RoutePoint>& routes)
    {
        editCue ([&routes] (Cue& c) { c.routing = routes; });
    };
    content.addAndMakeVisible (routingMatrix);
}

//==============================================================================
void CueInspector::MessageListModel::paintListBoxItem (int row, juce::Graphics& g,
                                                       int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, items.size()))
        return;

    g.fillAll (selected ? colours::panelLight : colours::panel);
    g.setColour (colours::text);
    g.setFont (juce::FontOptions (11.5f));
    g.drawText (items[row], juce::Rectangle<int> (0, 0, width, height).reduced (6, 0),
                juce::Justification::centredLeft, true);
}

void CueInspector::MessageListModel::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    if (onDoubleClick != nullptr)
        onDoubleClick (row);
}

void CueInspector::updateMessageList()
{
    messageModel.items.clear();

    if (const auto* cue = cueList.get (cueIndex))
        for (const auto& message : cue->outputMessages)
            messageModel.items.add (message.describe());

    messageList.updateContent();
    messageList.repaint();

    const auto hasSelection = juce::isPositiveAndBelow (messageList.getSelectedRow(),
                                                        messageModel.items.size());
    messageEditButton.setEnabled (hasSelection);
    messageRemoveButton.setEnabled (hasSelection);
    messageTestButton.setEnabled (hasSelection);
}

void CueInspector::editMessage (int index)
{
    const auto* cue = cueList.get (cueIndex);

    if (cue == nullptr || ! juce::isPositiveAndBelow (index, (int) cue->outputMessages.size()))
        return;

    const auto message = cue->outputMessages[(size_t) index];

    // One window with every field rather than a form that reshapes itself: a cue player is
    // configured between shows, and a predictable layout beats a clever one.
    auto* window = new juce::AlertWindow ("Outgoing message", {}, juce::MessageBoxIconType::NoIcon);

    window->addComboBox ("type", controlMessageTypeNames(), "Type");
    window->getComboBoxComponent ("type")->setSelectedItemIndex ((int) message.type);

    window->addTextEditor ("delay", juce::String (message.delay, 3), "Delay after the cue starts (s)");

    window->addTextEditor ("oscAddress", message.oscAddress, "OSC address");
    window->addTextEditor ("oscArgs", message.oscArguments, "OSC arguments");
    window->addTextEditor ("oscTarget", message.oscTarget, "OSC target (blank = all)");

    window->addTextEditor ("midiTarget", message.midiTarget, "MIDI output (blank = all)");
    window->addTextEditor ("midiChannel", juce::String (message.midiChannel), "MIDI channel");
    window->addTextEditor ("midiData1", juce::String (message.midiData1), "Note / CC / program number");
    window->addTextEditor ("midiData2", juce::String (message.midiData2), "Velocity / value");

    window->addTextEditor ("mscDevice", juce::String (message.mscDeviceID), "MSC / MMC device ID");

    window->addComboBox ("mscFormat", msc::formatNames(), "MSC command format");
    if (const auto i = msc::formatValues().indexOf (message.mscCommandFormat); i >= 0)
        window->getComboBoxComponent ("mscFormat")->setSelectedItemIndex (i);

    window->addComboBox ("mscCommand", msc::commandNames(), "MSC command");
    if (const auto i = msc::commandValues().indexOf (message.mscCommand); i >= 0)
        window->getComboBoxComponent ("mscCommand")->setSelectedItemIndex (i);

    window->addTextEditor ("mscCue", message.mscCueNumber, "MSC cue number");
    window->addTextEditor ("mscList", message.mscCueList, "MSC cue list");

    window->addComboBox ("mmcCommand", mmc::commandNames(), "MMC command");
    if (const auto i = mmc::commandValues().indexOf (message.mmcCommand); i >= 0)
        window->getComboBoxComponent ("mmcCommand")->setSelectedItemIndex (i);

    window->addButton ("Save",   1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true, juce::ModalCallbackFunction::create (
        [this, window, index] (int result)
        {
            if (result == 1)
            {
                ControlMessage m;
                m.type  = (ControlMessageType) window->getComboBoxComponent ("type")->getSelectedItemIndex();
                m.delay = juce::jmax (0.0, window->getTextEditorContents ("delay").getDoubleValue());

                m.oscAddress   = window->getTextEditorContents ("oscAddress").trim();
                m.oscArguments = window->getTextEditorContents ("oscArgs");
                m.oscTarget    = window->getTextEditorContents ("oscTarget").trim();

                m.midiTarget  = window->getTextEditorContents ("midiTarget").trim();
                m.midiChannel = juce::jlimit (1, 16,  window->getTextEditorContents ("midiChannel").getIntValue());
                m.midiData1   = juce::jlimit (0, 127, window->getTextEditorContents ("midiData1").getIntValue());
                m.midiData2   = juce::jlimit (0, 127, window->getTextEditorContents ("midiData2").getIntValue());

                m.mscDeviceID = juce::jlimit (0, 127, window->getTextEditorContents ("mscDevice").getIntValue());

                const auto formatIndex = window->getComboBoxComponent ("mscFormat")->getSelectedItemIndex();
                const auto commandIndex = window->getComboBoxComponent ("mscCommand")->getSelectedItemIndex();
                const auto mmcIndex = window->getComboBoxComponent ("mmcCommand")->getSelectedItemIndex();

                if (formatIndex >= 0)  m.mscCommandFormat = msc::formatValues()[formatIndex];
                if (commandIndex >= 0) m.mscCommand = msc::commandValues()[commandIndex];
                if (mmcIndex >= 0)     m.mmcCommand = mmc::commandValues()[mmcIndex];

                m.mscCueNumber = window->getTextEditorContents ("mscCue").trim();
                m.mscCueList   = window->getTextEditorContents ("mscList").trim();

                editCue ([index, m] (Cue& cue)
                {
                    if (juce::isPositiveAndBelow (index, (int) cue.outputMessages.size()))
                        cue.outputMessages[(size_t) index] = m;
                });

                updateMessageList();
            }

            delete window;
        }), false);
}


//==============================================================================
void CueInspector::buildTimeFields()
{
    struct FieldSetup
    {
        juce::Label* label;
        juce::TextEditor* field;
    };

    const FieldSetup setups[] =
    {
        { &inFieldLabel,        &inField },
        { &outFieldLabel,       &outField },
        { &vampStartFieldLabel, &vampStartField },
        { &vampEndFieldLabel,   &vampEndField }
    };

    for (const auto& setup : setups)
    {
        setup.label->setFont (juce::FontOptions (10.5f, juce::Font::bold));
        setup.label->setColour (juce::Label::textColourId, colours::textDim);
        setup.label->setJustificationType (juce::Justification::centredLeft);
        timeFieldBar.addAndMakeVisible (*setup.label);

        setup.field->setMultiLine (false);
        setup.field->setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                                 12.0f, juce::Font::plain));
        setup.field->setJustification (juce::Justification::centredLeft);
        setup.field->setTooltip ("Type mm:ss.mmm, m:ss, or plain seconds.");
        timeFieldBar.addAndMakeVisible (*setup.field);
    }

    // Committed on Enter or on losing focus, never per keystroke: typing "1" on the way to
    // "1:02" would otherwise move the marker to one second and drag the others with it.
    const auto commit = [this] (juce::TextEditor& field, void (*apply) (Cue&, double))
    {
        const auto* cue = cueList.get (cueIndex);

        if (cue == nullptr)
            return;

        const auto seconds = parseTimecode (field.getText(), -1.0);

        if (seconds < 0.0)
        {
            refreshTimeFields();     // Unparseable: put the real value back.
            return;
        }

        editCue ([apply, seconds] (Cue& c) { apply (c, seconds); });
        refreshTimeFields();
    };

    inField.onReturnKey = inField.onFocusLost = [this, commit]
    {
        commit (inField, [] (Cue& c, double s)
                { c.startTime = juce::jlimit (0.0, c.resolvedEndTime(), s); });
    };

    outField.onReturnKey = outField.onFocusLost = [this, commit]
    {
        commit (outField, [] (Cue& c, double s)
                { c.endTime = juce::jlimit (c.startTime, juce::jmax (c.startTime, c.fileDuration), s); });
    };

    vampStartField.onReturnKey = vampStartField.onFocusLost = [this, commit]
    {
        commit (vampStartField, [] (Cue& c, double s)
                { c.vampStart = juce::jlimit (c.startTime, c.resolvedEndTime(), s); });
    };

    vampEndField.onReturnKey = vampEndField.onFocusLost = [this, commit]
    {
        commit (vampEndField, [] (Cue& c, double s)
                { c.vampEnd = juce::jlimit (c.startTime, c.resolvedEndTime(), s); });
    };
}

void CueInspector::refreshTimeFields()
{
    const auto* cue = cueList.get (cueIndex);
    const auto isFile = cue != nullptr && cue->type == CueType::audioFile;

    const auto setField = [] (juce::TextEditor& field, double seconds, bool enabled)
    {
        field.setEnabled (enabled);

        // Never overwrite a field somebody is part-way through typing into.
        if (! field.hasKeyboardFocus (false))
            field.setText (enabled ? formatTimecode (seconds) : juce::String(),
                           juce::dontSendNotification);
    };

    setField (inField,  cue != nullptr ? cue->startTime : 0.0, isFile);
    setField (outField, cue != nullptr ? cue->resolvedEndTime() : 0.0, isFile);

    const auto vampUsable = isFile && cue->vampEnabled;
    setField (vampStartField, cue != nullptr ? cue->vampStart : 0.0, vampUsable);
    setField (vampEndField,   cue != nullptr ? cue->vampEnd : 0.0, vampUsable);
}

void CueInspector::updateSectionVisibility()
{
    const auto* cue = cueList.get (cueIndex);
    const auto type = cue != nullptr ? cue->type : CueType::audioFile;
    const auto isFile = cue != nullptr && type == CueType::audioFile;
    const auto isStreaming = type == CueType::streaming;

    const auto setSectionVisible = [this] (size_t section, bool visible)
    {
        if (section >= sectionLabels.size())
            return;

        sectionLabels[section]->setVisible (visible);

        const auto first = sectionRowStart[section];
        const auto last  = section + 1 < sectionRowStart.size() ? sectionRowStart[section + 1]
                                                                : rows.size();

        for (auto i = first; i < last; ++i)
        {
            rows[i]->label.setVisible (visible);
            rows[i]->control->setVisible (visible);
        }
    };

    setSectionVisible (streamingSectionIndex, isStreaming);
    setSectionVisible (trimSectionIndex, isFile);
    setSectionVisible (loopSectionIndex, isFile);

    // A control cue has no audio, so it has nothing to route.
    const auto hasAudio = isFile || isStreaming;
    setSectionVisible (endSectionIndex, hasAudio);
    setSectionVisible (routingSectionIndexForVisibility, hasAudio);
    routingMatrix.setVisible (hasAudio);

    // The waveform and its time fields only mean anything for a file cue.
    waveform.setVisible (isFile);
    timeFieldBar.setVisible (isFile);
}

//==============================================================================
void CueInspector::editCue (const std::function<void (Cue&)>& fn)
{
    if (updating || cueIndex < 0)
        return;

    if (! cueList.modify (cueIndex, fn))
        return;

    if (onCueEdited != nullptr)
        onCueEdited();
}

void CueInspector::setCueIndex (int index)
{
    cueIndex = index;
    refresh();
}

void CueInspector::updateLinkTargets()
{
    const auto* cue = cueList.get (cueIndex);
    linkTargetBox.clear (juce::dontSendNotification);
    linkTargetBox.addItem ("The next cue in the list", 1);

    for (int i = 0; i < cueList.size(); ++i)
    {
        const auto* other = cueList.get (i);

        if (other == nullptr || (cue != nullptr && other->id == cue->id))
            continue;

        const auto label = (other->number.isNotEmpty() ? other->number + "  " : juce::String())
                         + (other->name.isNotEmpty() ? other->name : juce::String ("(untitled)"));
        linkTargetBox.addItem (label, i + 2);
    }

    if (cue == nullptr)
        return;

    if (cue->link.targetsNextCue())
    {
        linkTargetBox.setSelectedId (1, juce::dontSendNotification);
    }
    else
    {
        const auto targetIndex = cueList.indexOfID (cue->link.target);
        linkTargetBox.setSelectedId (targetIndex >= 0 ? targetIndex + 2 : 1, juce::dontSendNotification);
    }
}

void CueInspector::updateEnablement()
{
    const auto* cue = cueList.get (cueIndex);
    const auto isStreaming = cue != nullptr && cue->type == CueType::streaming;
    const auto isFile = cue != nullptr && cue->type == CueType::audioFile;

    for (auto* c : streamingOnly) c->setEnabled (isStreaming);
    for (auto* c : fileOnly)      c->setEnabled (isFile);

    loopCountSlider.setEnabled (isFile && cue->loopEnabled);
    vampStartSlider.setEnabled (isFile && cue->vampEnabled);
    vampEndSlider.setEnabled (isFile && cue->vampEnabled);
    vampReleaseBox.setEnabled (isFile && cue->vampEnabled);

    const auto mode = cue != nullptr ? cue->link.mode : LinkMode::none;
    linkTargetBox.setEnabled (mode != LinkMode::none);
    linkDelaySlider.setEnabled (mode == LinkMode::autoContinue || mode == LinkMode::autoFollow);
    linkDurationSlider.setEnabled (mode == LinkMode::crossfade);
    linkShapeBox.setEnabled (mode == LinkMode::crossfade);

    endFadeSlider.setEnabled (cue != nullptr && cue->endAction == EndAction::fadeOut);

    auditionButton.setEnabled (cue != nullptr && cue->isPlayable());
}

void CueInspector::updateRoutingMatrix()
{
    const auto* cue = cueList.get (cueIndex);

    juce::StringArray sourceLabels;
    int numSourceChannels = 0;

    if (cue != nullptr)
    {
        numSourceChannels = cue->type == CueType::streaming
                                ? juce::jmax (1, audioEngine.getStreamingSettings().captureNumChannels)
                                : juce::jmax (0, cue->fileChannels);
    }

    for (int i = 0; i < numSourceChannels; ++i)
        sourceLabels.add (numSourceChannels == 2 ? (i == 0 ? "Left" : "Right")
                                                 : "Ch " + juce::String (i + 1));

    routingMatrix.setChannels (sourceLabels, audioEngine.getOutputChannelNames());

    if (cue != nullptr)
        routingMatrix.setRouting (cue->effectiveRouting (numSourceChannels,
                                                         audioEngine.getNumOutputChannels()),
                                  cue->routing.empty());
    else
        routingMatrix.setRouting ({}, true);
}

void CueInspector::pushSourceInfoToWaveform()
{
    waveform.setCue (cueList.get (cueIndex));
}

void CueInspector::refresh()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);

    const auto* cue = cueList.get (cueIndex);

    if (cue == nullptr)
    {
        pushSourceInfoToWaveform();
        updateEnablement();
        updateSectionVisibility();
        updateRoutingMatrix();
        updateMessageList();
        refreshTimeFields();
        repaint();
        return;
    }

    numberEditor.setText (cue->number, juce::dontSendNotification);
    nameEditor.setText (cue->name, juce::dontSendNotification);

    if (! notesEditor.hasKeyboardFocus (false))
        notesEditor.setText (cue->notes, juce::dontSendNotification);

    chooseFileButton.setButtonText (cue->audioFile == juce::File()
                                        ? "Choose file..." : cue->audioFile.getFileName());

    gainSlider.setValue (cue->gainDb, juce::dontSendNotification);
    preWaitSlider.setValue (cue->preWait, juce::dontSendNotification);

    const auto duration = juce::jmax (1.0, cue->fileDuration);
    inPointSlider.setRange (0.0, duration, 0.001);
    outPointSlider.setRange (0.0, duration, 0.001);
    vampStartSlider.setRange (0.0, duration, 0.001);
    vampEndSlider.setRange (0.0, duration, 0.001);

    inPointSlider.setValue (cue->startTime, juce::dontSendNotification);
    outPointSlider.setValue (cue->resolvedEndTime(), juce::dontSendNotification);

    fadeInSlider.setValue (cue->fadeInTime, juce::dontSendNotification);
    fadeInShapeBox.setSelectedId ((int) cue->fadeInShape + 1, juce::dontSendNotification);
    fadeOutSlider.setValue (cue->fadeOutTime, juce::dontSendNotification);
    fadeOutShapeBox.setSelectedId ((int) cue->fadeOutShape + 1, juce::dontSendNotification);

    loopToggle.setToggleState (cue->loopEnabled, juce::dontSendNotification);
    loopCountSlider.setValue (cue->loopCount, juce::dontSendNotification);
    vampToggle.setToggleState (cue->vampEnabled, juce::dontSendNotification);
    vampStartSlider.setValue (cue->vampStart, juce::dontSendNotification);
    vampEndSlider.setValue (cue->vampEnd, juce::dontSendNotification);
    vampReleaseBox.setSelectedId (cue->vampRelease == VampRelease::immediately ? 2 : 1,
                                  juce::dontSendNotification);

    endStepModeBox.setSelectedId ((int) cue->endStepMode + 1, juce::dontSendNotification);
    endActionBox.setSelectedId ((int) cue->endAction + 1, juce::dontSendNotification);
    endFadeSlider.setValue (cue->endFadeTime, juce::dontSendNotification);

    linkModeBox.setSelectedId ((int) cue->link.mode + 1, juce::dontSendNotification);
    linkDelaySlider.setValue (cue->link.delay, juce::dontSendNotification);
    linkDurationSlider.setValue (cue->link.duration, juce::dontSendNotification);
    linkShapeBox.setSelectedId ((int) cue->link.shape + 1, juce::dontSendNotification);
    updateLinkTargets();

    if (! streamUriEditor.hasKeyboardFocus (false))
        streamUriEditor.setText (cue->streaming.uri, juce::dontSendNotification);

    streamNameEditor.setText (cue->streaming.displayName, juce::dontSendNotification);
    streamShuffleToggle.setToggleState (cue->streaming.shuffle, juce::dontSendNotification);
    streamRepeatToggle.setToggleState (cue->streaming.repeat, juce::dontSendNotification);

    {
        const auto& streamingSettings = audioEngine.getStreamingSettings();
        const auto path = streamingSettings.audioPath == StreamingAudioPath::localCapture
                              ? "captured from inputs "
                                  + juce::String (streamingSettings.captureFirstInputChannel + 1)
                                  + "-" + juce::String (streamingSettings.captureFirstInputChannel
                                                        + streamingSettings.captureNumChannels)
                              : juce::String ("played on a remote device");

        streamAccountLabel.setText (streamingSettings.getProviderDisplayName() + ", " + path
                                        + "   (change in Settings)",
                                    juce::dontSendNotification);
    }

    pushSourceInfoToWaveform();
    updateEnablement();
    updateSectionVisibility();
    updateRoutingMatrix();
    updateMessageList();
    refreshTimeFields();
    resized();
    repaint();
}

//==============================================================================
void CueInspector::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
}

void CueInspector::resized()
{
    auto bounds = getLocalBounds();

    if (waveform.isVisible())
    {
        waveform.setBounds (bounds.removeFromTop (waveformHeight));

        auto barArea = bounds.removeFromTop (timeFieldBarHeight);
        timeFieldBar.setBounds (barArea);

        // Four evenly spaced label-and-field pairs directly under the timeline.
        auto inner = timeFieldBar.getLocalBounds().reduced (8, 3);
        const auto cellWidth = inner.getWidth() / 4;

        juce::Label* labels[] = { &inFieldLabel, &outFieldLabel,
                                  &vampStartFieldLabel, &vampEndFieldLabel };
        juce::TextEditor* fields[] = { &inField, &outField, &vampStartField, &vampEndField };

        for (int i = 0; i < 4; ++i)
        {
            auto cell = inner.removeFromLeft (cellWidth).reduced (3, 0);
            labels[i]->setBounds (cell.removeFromLeft (i < 2 ? 22 : 66));
            fields[i]->setBounds (cell);
        }
    }

    viewport.setBounds (bounds);

    const auto width = juce::jmax (320, viewport.getMaximumVisibleWidth());
    const auto controlWidth = width - labelWidth - 24;

    int y = 8;

    for (size_t section = 0; section < sectionLabels.size(); ++section)
    {
        // A hidden section takes up no space at all, so the panel closes up around it
        // rather than leaving a gap where the wrong cue type's controls would have been.
        if (! sectionLabels[section]->isVisible())
            continue;

        sectionLabels[section]->setBounds (8, y, width - 16, 16);
        y += 16 + rowGap;

        const auto first = sectionRowStart[section];
        const auto last  = section + 1 < sectionRowStart.size() ? sectionRowStart[section + 1]
                                                                : rows.size();

        for (auto i = first; i < last; ++i)
        {
            auto& row = *rows[i];
            const auto height = rowHeight * row.span + (row.span - 1) * rowGap;

            row.label.setBounds (8, y, labelWidth, rowHeight);
            row.control->setBounds (labelWidth + 16, y, controlWidth, height);
            y += height + rowGap;
        }

        if (section == messageSectionIndex)
        {
            const auto visibleRows = juce::jmax (3, messageModel.items.size());
            const auto panelHeight = visibleRows * 20 + rowHeight + 10;
            messagePanel.setBounds (8, y, width - 16, panelHeight);

            auto area = messagePanel.getLocalBounds();
            auto buttons = area.removeFromBottom (rowHeight);
            messageAddButton.setBounds (buttons.removeFromLeft (60).reduced (1));
            messageEditButton.setBounds (buttons.removeFromLeft (60).reduced (1));
            messageRemoveButton.setBounds (buttons.removeFromLeft (76).reduced (1));
            messageTestButton.setBounds (buttons.removeFromLeft (60).reduced (1));
            area.removeFromBottom (4);
            messageList.setBounds (area);

            y += panelHeight;
        }

        if (section == routingSectionIndex && routingMatrix.isVisible())
        {
            routingMatrix.setBounds (8, y, width - 16, routingMatrix.getPreferredHeight());
            y += routingMatrix.getHeight();
        }

        y += sectionGap;
    }

    y += 16;

    content.setSize (width, y);
}

} // namespace cp
