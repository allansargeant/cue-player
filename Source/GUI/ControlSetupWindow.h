#pragma once

#include "Control/ControlHub.h"
#include "GUI/LookAndFeel.h"

namespace cp
{

/** Settings for every way the outside world can drive the player, plus a live monitor.

    The monitor matters more than it looks: when a lighting desk is not firing a cue, the
    question is always "is anything arriving at all?", and the answer is either right here
    or it is a packet capture.
*/
class ControlSetupComponent : public  juce::Component,
                              private juce::Timer
{
public:
    ControlSetupComponent (ControlHub& hub, std::function<void()> onSettingsChanged);
    ~ControlSetupComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void commit();
    void refreshFromSettings();

    void buildOscTab();
    void buildMidiTab();
    void buildDmxTab();
    void buildMonitorTab();

    void editOscTarget (int index);
    void editMidiBinding (int index);

    ControlHub& controlHub;
    std::function<void()> settingsChanged;
    ControlSettings working;

    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    juce::Label statusLabel, errorLabel;

    //== OSC ==================================================================
    juce::Component oscTab;
    juce::ToggleButton oscInputToggle { "Listen for OSC" };
    juce::Label oscPortLabel { {}, "Port" };
    juce::TextEditor oscPortEditor;
    juce::ToggleButton oscFeedbackToggle { "Send status back to the targets below" };
    juce::ListBox oscTargetList;
    juce::TextButton oscAddButton { "Add" }, oscEditButton { "Edit" }, oscRemoveButton { "Remove" };
    juce::TextEditor oscReference;

    //== MIDI =================================================================
    juce::Component midiTab;
    juce::Label midiInputsLabel { {}, "Inputs" }, midiOutputsLabel { {}, "Outputs" };
    juce::ListBox midiInputList, midiOutputList;
    juce::ToggleButton mscToggle { "Respond to MIDI Show Control" };
    juce::Label mscDeviceLabel { {}, "MSC device ID" };
    juce::TextEditor mscDeviceEditor;
    juce::ToggleButton mscSoundToggle { "Sound format" }, mscAllTypesToggle { "All-types format" };
    juce::ToggleButton mmcToggle { "Respond to MIDI Machine Control" };
    juce::Label bindingsLabel { {}, "Note / CC / program bindings" };
    juce::ListBox midiBindingList;
    juce::TextButton midiAddButton { "Add" }, midiEditButton { "Edit" }, midiRemoveButton { "Remove" };

    //== DMX ==================================================================
    juce::Component dmxTab;
    juce::ToggleButton artNetToggle { "Art-Net (UDP 6454)" };
    juce::ToggleButton sacnToggle { "sACN / E1.31 (UDP 5568)" };
    juce::Label universeLabel { {}, "Universe" }, startLabel { {}, "Start address" };
    juce::Label thresholdLabel { {}, "Trigger at" }, directLabel { {}, "Direct cue channels" };
    juce::TextEditor universeEditor, startEditor, thresholdEditor, directEditor;
    juce::TextEditor dmxReference;

    //== Monitor ==============================================================
    juce::Component monitorTab;
    juce::ListBox monitorList;
    juce::TextButton clearMonitorButton { "Clear" };
    juce::StringArray monitorLines;

    /** Small adapter so several plain string lists can share one ListBoxModel. */
    struct StringListModel : public juce::ListBoxModel
    {
        juce::StringArray items;
        juce::Array<bool> ticked;
        bool showTicks { false };
        std::function<void (int)> onToggle;
        std::function<void (int)> onDoubleClick;

        int getNumRows() override { return items.size(); }
        void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
        void listBoxItemClicked (int row, const juce::MouseEvent&) override;
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    };

    StringListModel oscTargetModel, midiInputModel, midiOutputModel, bindingModel, monitorModel;
    juce::StringArray midiInputIdentifiers, midiOutputIdentifiers;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ControlSetupComponent)
};

/** Window wrapper, so control setup can stay open while a show runs. */
class ControlSetupWindow : public juce::DocumentWindow
{
public:
    ControlSetupWindow (ControlHub& hub, std::function<void()> onSettingsChanged);

    void closeButtonPressed() override;
    std::function<void()> onClose;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ControlSetupWindow)
};

} // namespace cp
