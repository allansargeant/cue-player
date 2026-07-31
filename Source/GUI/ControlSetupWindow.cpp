#include "GUI/ControlSetupWindow.h"

namespace cp
{

namespace
{
    constexpr int rowHeight = 26;
    constexpr int gap = 6;

    void styleReadOnly (juce::TextEditor& editor, const juce::String& text)
    {
        editor.setMultiLine (true);
        editor.setReadOnly (true);
        editor.setScrollbarsShown (true);
        editor.setCaretVisible (false);
        editor.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 11.5f, juce::Font::plain));
        editor.setColour (juce::TextEditor::backgroundColourId, colours::background);
        editor.setColour (juce::TextEditor::textColourId, colours::textDim);
        editor.setText (text, false);
    }

    void styleNumberBox (juce::TextEditor& editor)
    {
        editor.setMultiLine (false);
        editor.setInputRestrictions (6, "0123456789");
        editor.setJustification (juce::Justification::centredLeft);
    }

    void styleSectionLabel (juce::Label& label)
    {
        label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        label.setColour (juce::Label::textColourId, colours::textDim);
    }
}

//==============================================================================
void ControlSetupComponent::StringListModel::paintListBoxItem (int row, juce::Graphics& g,
                                                               int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, items.size()))
        return;

    g.fillAll (selected ? colours::panelLight : colours::panel);

    auto area = juce::Rectangle<int> (0, 0, width, height).reduced (6, 0);

    if (showTicks)
    {
        auto box = area.removeFromLeft (18).withSizeKeepingCentre (13, 13);
        const auto isTicked = juce::isPositiveAndBelow (row, ticked.size()) && ticked[row];

        g.setColour (isTicked ? colours::go : colours::outline);
        g.drawRoundedRectangle (box.toFloat(), 2.0f, 1.2f);

        if (isTicked)
        {
            g.setColour (colours::go);
            g.fillRoundedRectangle (box.reduced (3).toFloat(), 1.5f);
        }

        area.removeFromLeft (6);
    }

    g.setColour (colours::text);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (items[row], area, juce::Justification::centredLeft, true);
}

void ControlSetupComponent::StringListModel::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    if (showTicks && onToggle != nullptr)
        onToggle (row);
}

void ControlSetupComponent::StringListModel::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    if (! showTicks && onDoubleClick != nullptr)
        onDoubleClick (row);
}

//==============================================================================
ControlSetupComponent::ControlSetupComponent (ControlHub& hub, std::function<void()> onSettingsChanged)
    : controlHub (hub), settingsChanged (std::move (onSettingsChanged))
{
    working = controlHub.getSettings();

    addAndMakeVisible (tabs);
    tabs.setOutline (0);
    tabs.setTabBarDepth (28);

    buildOscTab();
    buildMidiTab();
    buildDmxTab();
    buildMonitorTab();

    tabs.addTab ("OSC",     colours::panel, &oscTab,     false);
    tabs.addTab ("MIDI",    colours::panel, &midiTab,    false);
    tabs.addTab ("DMX",     colours::panel, &dmxTab,     false);
    tabs.addTab ("Monitor", colours::panel, &monitorTab, false);

    statusLabel.setFont (juce::FontOptions (11.5f));
    statusLabel.setColour (juce::Label::textColourId, colours::textDim);
    addAndMakeVisible (statusLabel);

    errorLabel.setFont (juce::FontOptions (11.5f));
    errorLabel.setColour (juce::Label::textColourId, colours::stop);
    errorLabel.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (errorLabel);

    refreshFromSettings();
    startTimerHz (4);
    setSize (640, 620);
}

ControlSetupComponent::~ControlSetupComponent()
{
    stopTimer();
}

void ControlSetupComponent::commit()
{
    const auto problems = controlHub.applySettings (working);
    errorLabel.setText (problems, juce::dontSendNotification);

    if (settingsChanged != nullptr)
        settingsChanged();
}

//==============================================================================
void ControlSetupComponent::buildOscTab()
{
    oscTab.addAndMakeVisible (oscInputToggle);
    oscInputToggle.onClick = [this]
    {
        working.oscInputEnabled = oscInputToggle.getToggleState();
        commit();
    };

    styleSectionLabel (oscPortLabel);
    oscTab.addAndMakeVisible (oscPortLabel);

    styleNumberBox (oscPortEditor);
    oscPortEditor.onFocusLost = [this]
    {
        working.oscInputPort = juce::jlimit (1, 65535, oscPortEditor.getText().getIntValue());
        oscPortEditor.setText (juce::String (working.oscInputPort), false);
        commit();
    };
    oscTab.addAndMakeVisible (oscPortEditor);

    oscTab.addAndMakeVisible (oscFeedbackToggle);
    oscFeedbackToggle.onClick = [this]
    {
        working.oscFeedbackEnabled = oscFeedbackToggle.getToggleState();
        commit();
    };

    oscTargetModel.onDoubleClick = [this] (int row) { editOscTarget (row); };
    oscTargetList.setModel (&oscTargetModel);
    oscTargetList.setRowHeight (rowHeight);
    oscTargetList.setColour (juce::ListBox::backgroundColourId, colours::panel);
    oscTab.addAndMakeVisible (oscTargetList);

    oscAddButton.onClick = [this]
    {
        working.oscTargets.push_back ({ "Target " + juce::String ((int) working.oscTargets.size() + 1),
                                        "127.0.0.1", 53001, true });
        refreshFromSettings();
        commit();
        editOscTarget ((int) working.oscTargets.size() - 1);
    };

    oscEditButton.onClick = [this] { editOscTarget (oscTargetList.getSelectedRow()); };

    oscRemoveButton.onClick = [this]
    {
        const auto row = oscTargetList.getSelectedRow();

        if (juce::isPositiveAndBelow (row, (int) working.oscTargets.size()))
        {
            working.oscTargets.erase (working.oscTargets.begin() + row);
            refreshFromSettings();
            commit();
        }
    };

    oscTab.addAndMakeVisible (oscAddButton);
    oscTab.addAndMakeVisible (oscEditButton);
    oscTab.addAndMakeVisible (oscRemoveButton);

    styleReadOnly (oscReference,
        "Incoming addresses (case-insensitive)\n"
        "  /go                          fire the standby cue\n"
        "  /cue/<number>/go             fire a cue by number\n"
        "  /cue/<number>/stop [fade]    stop one cue\n"
        "  /cue/<number>/standby        make it the standby cue\n"
        "  /cue/<number>/select         select it for editing\n"
        "  /cue/<number>/audition\n"
        "  /cue/<number>/releasevamp\n"
        "  /stop [fade]                 stop everything\n"
        "  /panic                       immediate silence\n"
        "  /pause   /resume   /pause/toggle\n"
        "  /releasevamp                 release every vamp\n"
        "  /standby/next   /standby/previous   /standby/<number>\n"
        "  /master/level <dB>\n"
        "  /status/query                send the whole state to the targets\n"
        "\n"
        "Outgoing status\n"
        "  /status/standby <number> <name>\n"
        "  /status/playing <count>       /status/playingCues <numbers>\n"
        "  /status/paused <0|1>          /status/vamping <0|1>\n"
        "  /status/master <dB>");
    oscTab.addAndMakeVisible (oscReference);
}

void ControlSetupComponent::buildMidiTab()
{
    styleSectionLabel (midiInputsLabel);
    styleSectionLabel (midiOutputsLabel);
    styleSectionLabel (bindingsLabel);
    midiTab.addAndMakeVisible (midiInputsLabel);
    midiTab.addAndMakeVisible (midiOutputsLabel);
    midiTab.addAndMakeVisible (bindingsLabel);

    midiInputModel.showTicks = true;
    midiInputModel.onToggle = [this] (int row)
    {
        if (! juce::isPositiveAndBelow (row, midiInputIdentifiers.size()))
            return;

        const auto& identifier = midiInputIdentifiers[row];

        if (working.enabledMidiInputs.contains (identifier))
            working.enabledMidiInputs.removeString (identifier);
        else
            working.enabledMidiInputs.add (identifier);

        refreshFromSettings();
        commit();
    };
    midiInputList.setModel (&midiInputModel);
    midiInputList.setRowHeight (rowHeight);
    midiInputList.setColour (juce::ListBox::backgroundColourId, colours::panel);
    midiTab.addAndMakeVisible (midiInputList);

    midiOutputModel.showTicks = true;
    midiOutputModel.onToggle = [this] (int row)
    {
        if (! juce::isPositiveAndBelow (row, midiOutputIdentifiers.size()))
            return;

        const auto& identifier = midiOutputIdentifiers[row];

        if (working.enabledMidiOutputs.contains (identifier))
            working.enabledMidiOutputs.removeString (identifier);
        else
            working.enabledMidiOutputs.add (identifier);

        refreshFromSettings();
        commit();
    };
    midiOutputList.setModel (&midiOutputModel);
    midiOutputList.setRowHeight (rowHeight);
    midiOutputList.setColour (juce::ListBox::backgroundColourId, colours::panel);
    midiTab.addAndMakeVisible (midiOutputList);

    mscToggle.onClick = [this]
    {
        working.midiShowControlEnabled = mscToggle.getToggleState();
        commit();
    };
    midiTab.addAndMakeVisible (mscToggle);

    styleSectionLabel (mscDeviceLabel);
    midiTab.addAndMakeVisible (mscDeviceLabel);

    styleNumberBox (mscDeviceEditor);
    mscDeviceEditor.onFocusLost = [this]
    {
        working.mscDeviceID = juce::jlimit (0, 127, mscDeviceEditor.getText().getIntValue());
        mscDeviceEditor.setText (juce::String (working.mscDeviceID), false);
        commit();
    };
    midiTab.addAndMakeVisible (mscDeviceEditor);

    mscSoundToggle.onClick = [this]
    {
        working.mscRespondToSoundFormat = mscSoundToggle.getToggleState();
        commit();
    };
    mscAllTypesToggle.onClick = [this]
    {
        working.mscRespondToAllTypesFormat = mscAllTypesToggle.getToggleState();
        commit();
    };
    midiTab.addAndMakeVisible (mscSoundToggle);
    midiTab.addAndMakeVisible (mscAllTypesToggle);

    mmcToggle.onClick = [this]
    {
        working.midiMachineControlEnabled = mmcToggle.getToggleState();
        commit();
    };
    midiTab.addAndMakeVisible (mmcToggle);

    bindingModel.onDoubleClick = [this] (int row) { editMidiBinding (row); };
    midiBindingList.setModel (&bindingModel);
    midiBindingList.setRowHeight (rowHeight);
    midiBindingList.setColour (juce::ListBox::backgroundColourId, colours::panel);
    midiTab.addAndMakeVisible (midiBindingList);

    midiAddButton.onClick = [this]
    {
        working.midiBindings.push_back ({});
        refreshFromSettings();
        commit();
        editMidiBinding ((int) working.midiBindings.size() - 1);
    };

    midiEditButton.onClick = [this] { editMidiBinding (midiBindingList.getSelectedRow()); };

    midiRemoveButton.onClick = [this]
    {
        const auto row = midiBindingList.getSelectedRow();

        if (juce::isPositiveAndBelow (row, (int) working.midiBindings.size()))
        {
            working.midiBindings.erase (working.midiBindings.begin() + row);
            refreshFromSettings();
            commit();
        }
    };

    midiTab.addAndMakeVisible (midiAddButton);
    midiTab.addAndMakeVisible (midiEditButton);
    midiTab.addAndMakeVisible (midiRemoveButton);
}

void ControlSetupComponent::buildDmxTab()
{
    artNetToggle.onClick = [this]
    {
        working.dmx.artNetEnabled = artNetToggle.getToggleState();
        commit();
    };
    sacnToggle.onClick = [this]
    {
        working.dmx.sacnEnabled = sacnToggle.getToggleState();
        commit();
    };
    dmxTab.addAndMakeVisible (artNetToggle);
    dmxTab.addAndMakeVisible (sacnToggle);

    for (auto* label : { &universeLabel, &startLabel, &thresholdLabel, &directLabel })
    {
        styleSectionLabel (*label);
        dmxTab.addAndMakeVisible (*label);
    }

    const auto hook = [this] (juce::TextEditor& editor, std::function<void (int)> apply)
    {
        styleNumberBox (editor);
        editor.onFocusLost = [this, &editor, apply]
        {
            apply (editor.getText().getIntValue());
            refreshFromSettings();
            commit();
        };
        dmxTab.addAndMakeVisible (editor);
    };

    hook (universeEditor,  [this] (int v) { working.dmx.universe = juce::jlimit (0, 63999, v); });
    hook (startEditor,     [this] (int v) { working.dmx.startAddress = juce::jlimit (1, 512, v); });
    hook (thresholdEditor, [this] (int v) { working.dmx.triggerThreshold = juce::jlimit (1, 255, v); });
    hook (directEditor,    [this] (int v) { working.dmx.numDirectCueChannels = juce::jlimit (0, 505, v); });

    styleReadOnly (dmxReference,
        "Channels, counted from the start address\n"
        "  +0   GO            rises past the trigger level to fire the standby cue\n"
        "  +1   Stop all\n"
        "  +2   Panic\n"
        "  +3   Pause         held above the trigger level = paused\n"
        "  +4   Release vamp\n"
        "  +5   Master level  0 = silence, 255 = 0 dB\n"
        "  +6   Standby       a value of N stands by the Nth cue in the list\n"
        "  +7.. Fire the 1st, 2nd, 3rd... cue in the list directly\n"
        "\n"
        "Triggers are edge-detected, so a desk holding a channel high fires once, not\n"
        "once per frame. sACN preview and stream-terminated packets are ignored, so a\n"
        "designer working blind cannot fire a sound cue by accident.");
    dmxTab.addAndMakeVisible (dmxReference);
}

void ControlSetupComponent::buildMonitorTab()
{
    monitorList.setModel (&monitorModel);
    monitorList.setRowHeight (18);
    monitorList.setColour (juce::ListBox::backgroundColourId, colours::panel);
    monitorTab.addAndMakeVisible (monitorList);

    clearMonitorButton.onClick = [this]
    {
        controlHub.clearMonitor();
        monitorLines.clear();
        monitorModel.items.clear();
        monitorList.updateContent();
    };
    monitorTab.addAndMakeVisible (clearMonitorButton);
}

//==============================================================================
void ControlSetupComponent::refreshFromSettings()
{
    oscInputToggle.setToggleState (working.oscInputEnabled, juce::dontSendNotification);
    oscPortEditor.setText (juce::String (working.oscInputPort), false);
    oscFeedbackToggle.setToggleState (working.oscFeedbackEnabled, juce::dontSendNotification);

    oscTargetModel.items.clear();

    for (const auto& target : working.oscTargets)
        oscTargetModel.items.add ((target.enabled ? "" : "(off)  ") + target.name + "   "
                                  + target.host + ":" + juce::String (target.port));

    oscTargetList.updateContent();
    oscTargetList.repaint();

    // MIDI devices are re-scanned every refresh: interfaces get plugged in mid-session.
    midiInputModel.items.clear();
    midiInputModel.ticked.clear();
    midiInputIdentifiers.clear();

    for (const auto& device : juce::MidiInput::getAvailableDevices())
    {
        midiInputIdentifiers.add (device.identifier);
        midiInputModel.items.add (device.name);
        midiInputModel.ticked.add (working.enabledMidiInputs.contains (device.identifier));
    }

    midiInputList.updateContent();
    midiInputList.repaint();

    midiOutputModel.items.clear();
    midiOutputModel.ticked.clear();
    midiOutputIdentifiers.clear();

    for (const auto& device : juce::MidiOutput::getAvailableDevices())
    {
        midiOutputIdentifiers.add (device.identifier);
        midiOutputModel.items.add (device.name);
        midiOutputModel.ticked.add (working.enabledMidiOutputs.contains (device.identifier));
    }

    midiOutputList.updateContent();
    midiOutputList.repaint();

    mscToggle.setToggleState (working.midiShowControlEnabled, juce::dontSendNotification);
    mscDeviceEditor.setText (juce::String (working.mscDeviceID), false);
    mscSoundToggle.setToggleState (working.mscRespondToSoundFormat, juce::dontSendNotification);
    mscAllTypesToggle.setToggleState (working.mscRespondToAllTypesFormat, juce::dontSendNotification);
    mmcToggle.setToggleState (working.midiMachineControlEnabled, juce::dontSendNotification);

    bindingModel.items.clear();

    for (const auto& binding : working.midiBindings)
        bindingModel.items.add (binding.describe());

    midiBindingList.updateContent();
    midiBindingList.repaint();

    artNetToggle.setToggleState (working.dmx.artNetEnabled, juce::dontSendNotification);
    sacnToggle.setToggleState (working.dmx.sacnEnabled, juce::dontSendNotification);
    universeEditor.setText (juce::String (working.dmx.universe), false);
    startEditor.setText (juce::String (working.dmx.startAddress), false);
    thresholdEditor.setText (juce::String (working.dmx.triggerThreshold), false);
    directEditor.setText (juce::String (working.dmx.numDirectCueChannels), false);

    resized();
}

void ControlSetupComponent::timerCallback()
{
    statusLabel.setText (controlHub.getStatusSummary(), juce::dontSendNotification);

    auto latest = controlHub.getMonitorLines();

    if (latest != monitorLines)
    {
        monitorLines = latest;
        monitorModel.items = latest;
        monitorList.updateContent();
        monitorList.scrollToEnsureRowIsOnscreen (latest.size() - 1);
        monitorList.repaint();
    }
}

//==============================================================================
void ControlSetupComponent::editOscTarget (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) working.oscTargets.size()))
        return;

    const auto& target = working.oscTargets[(size_t) index];

    auto* window = new juce::AlertWindow ("OSC target", {}, juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("name", target.name, "Name");
    window->addTextEditor ("host", target.host, "Host");
    window->addTextEditor ("port", juce::String (target.port), "Port");
    window->addComboBox ("enabled", { "Enabled", "Disabled" }, "State");
    window->getComboBoxComponent ("enabled")->setSelectedId (target.enabled ? 1 : 2);
    window->addButton ("Save",   1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true, juce::ModalCallbackFunction::create (
        [this, window, index] (int result)
        {
            if (result == 1 && juce::isPositiveAndBelow (index, (int) working.oscTargets.size()))
            {
                auto& t = working.oscTargets[(size_t) index];
                t.name    = window->getTextEditorContents ("name");
                t.host    = window->getTextEditorContents ("host").trim();
                t.port    = juce::jlimit (1, 65535, window->getTextEditorContents ("port").getIntValue());
                t.enabled = window->getComboBoxComponent ("enabled")->getSelectedId() == 1;

                refreshFromSettings();
                commit();
            }

            delete window;
        }), false);
}

void ControlSetupComponent::editMidiBinding (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) working.midiBindings.size()))
        return;

    const auto& binding = working.midiBindings[(size_t) index];

    auto* window = new juce::AlertWindow ("MIDI binding", {}, juce::MessageBoxIconType::NoIcon);

    window->addComboBox ("kind", midiTriggerKindNames(), "Trigger");
    window->getComboBoxComponent ("kind")->setSelectedItemIndex ((int) binding.kind);

    window->addTextEditor ("channel", juce::String (binding.channel), "Channel (0 = any)");
    window->addTextEditor ("number", juce::String (binding.number), "Note / CC / program number");

    window->addComboBox ("action", controlActionTypeNames(), "Action");
    window->getComboBoxComponent ("action")->setSelectedItemIndex ((int) binding.action);

    window->addTextEditor ("cue", binding.cueNumber, "Cue number (for cue actions)");

    window->addComboBox ("level", { "Trigger the action", "Use the value as a level" }, "Value");
    window->getComboBoxComponent ("level")->setSelectedId (binding.useValueAsLevel ? 2 : 1);

    window->addButton ("Save",   1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true, juce::ModalCallbackFunction::create (
        [this, window, index] (int result)
        {
            if (result == 1 && juce::isPositiveAndBelow (index, (int) working.midiBindings.size()))
            {
                auto& b = working.midiBindings[(size_t) index];
                b.kind    = (MidiTriggerKind) window->getComboBoxComponent ("kind")->getSelectedItemIndex();
                b.channel = juce::jlimit (0, 16, window->getTextEditorContents ("channel").getIntValue());
                b.number  = juce::jlimit (0, 127, window->getTextEditorContents ("number").getIntValue());
                b.action  = (ControlActionType) window->getComboBoxComponent ("action")->getSelectedItemIndex();
                b.cueNumber = window->getTextEditorContents ("cue").trim();
                b.useValueAsLevel = window->getComboBoxComponent ("level")->getSelectedId() == 2;

                refreshFromSettings();
                commit();
            }

            delete window;
        }), false);
}

//==============================================================================
void ControlSetupComponent::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
}

void ControlSetupComponent::resized()
{
    auto bounds = getLocalBounds().reduced (8);

    errorLabel.setBounds (bounds.removeFromBottom (34));
    statusLabel.setBounds (bounds.removeFromBottom (18));
    bounds.removeFromBottom (4);
    tabs.setBounds (bounds);

    // --- OSC ------------------------------------------------------------------
    {
        auto area = oscTab.getLocalBounds().reduced (10);

        auto row = area.removeFromTop (rowHeight);
        oscInputToggle.setBounds (row.removeFromLeft (150));
        oscPortLabel.setBounds (row.removeFromLeft (40));
        oscPortEditor.setBounds (row.removeFromLeft (80));
        area.removeFromTop (gap);

        oscFeedbackToggle.setBounds (area.removeFromTop (rowHeight));
        area.removeFromTop (gap);

        oscReference.setBounds (area.removeFromBottom (juce::jmax (140, area.getHeight() / 2)));
        area.removeFromBottom (gap);

        auto buttons = area.removeFromBottom (rowHeight);
        oscAddButton.setBounds (buttons.removeFromLeft (70));
        buttons.removeFromLeft (4);
        oscEditButton.setBounds (buttons.removeFromLeft (70));
        buttons.removeFromLeft (4);
        oscRemoveButton.setBounds (buttons.removeFromLeft (80));
        area.removeFromBottom (4);

        oscTargetList.setBounds (area);
    }

    // --- MIDI -----------------------------------------------------------------
    {
        auto area = midiTab.getLocalBounds().reduced (10);

        auto devices = area.removeFromTop (juce::jmax (90, area.getHeight() / 4));
        auto left = devices.removeFromLeft (devices.getWidth() / 2 - 4);
        midiInputsLabel.setBounds (left.removeFromTop (16));
        midiInputList.setBounds (left);

        devices.removeFromLeft (8);
        midiOutputsLabel.setBounds (devices.removeFromTop (16));
        midiOutputList.setBounds (devices);

        area.removeFromTop (gap);

        auto row = area.removeFromTop (rowHeight);
        mscToggle.setBounds (row.removeFromLeft (220));
        mscDeviceLabel.setBounds (row.removeFromLeft (90));
        mscDeviceEditor.setBounds (row.removeFromLeft (60));

        row = area.removeFromTop (rowHeight);
        row.removeFromLeft (20);
        mscSoundToggle.setBounds (row.removeFromLeft (140));
        mscAllTypesToggle.setBounds (row.removeFromLeft (160));

        mmcToggle.setBounds (area.removeFromTop (rowHeight));
        area.removeFromTop (gap);

        bindingsLabel.setBounds (area.removeFromTop (16));

        auto buttons = area.removeFromBottom (rowHeight);
        midiAddButton.setBounds (buttons.removeFromLeft (70));
        buttons.removeFromLeft (4);
        midiEditButton.setBounds (buttons.removeFromLeft (70));
        buttons.removeFromLeft (4);
        midiRemoveButton.setBounds (buttons.removeFromLeft (80));
        area.removeFromBottom (4);

        midiBindingList.setBounds (area);
    }

    // --- DMX ------------------------------------------------------------------
    {
        auto area = dmxTab.getLocalBounds().reduced (10);

        artNetToggle.setBounds (area.removeFromTop (rowHeight));
        sacnToggle.setBounds (area.removeFromTop (rowHeight));
        area.removeFromTop (gap);

        auto row = area.removeFromTop (rowHeight);
        universeLabel.setBounds (row.removeFromLeft (70));
        universeEditor.setBounds (row.removeFromLeft (70));
        row.removeFromLeft (12);
        startLabel.setBounds (row.removeFromLeft (90));
        startEditor.setBounds (row.removeFromLeft (70));

        row = area.removeFromTop (rowHeight);
        thresholdLabel.setBounds (row.removeFromLeft (70));
        thresholdEditor.setBounds (row.removeFromLeft (70));
        row.removeFromLeft (12);
        directLabel.setBounds (row.removeFromLeft (130));
        directEditor.setBounds (row.removeFromLeft (70));

        area.removeFromTop (gap);
        dmxReference.setBounds (area);
    }

    // --- Monitor --------------------------------------------------------------
    {
        auto area = monitorTab.getLocalBounds().reduced (10);
        clearMonitorButton.setBounds (area.removeFromBottom (rowHeight).removeFromLeft (80));
        area.removeFromBottom (4);
        monitorList.setBounds (area);
    }
}

//==============================================================================
ControlSetupWindow::ControlSetupWindow (ControlHub& hub, std::function<void()> onSettingsChanged)
    : DocumentWindow ("SimpleCue - Control setup", colours::background, DocumentWindow::closeButton)
{
    setUsingNativeTitleBar (true);
    setContentOwned (new ControlSetupComponent (hub, std::move (onSettingsChanged)), true);
    setResizable (true, false);
    setResizeLimits (560, 480, 1400, 1400);
    centreWithSize (640, 620);
    setVisible (true);
}

void ControlSetupWindow::closeButtonPressed()
{
    if (onClose != nullptr)
        onClose();
}

} // namespace cp
