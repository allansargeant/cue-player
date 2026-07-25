#pragma once

#include "GUI/LookAndFeel.h"
#include "Model/Show.h"
#include "Model/StreamingSettings.h"

namespace cp
{

/** Preferences: the streaming account, and the show's defaults for new cues.

    The split matters. Streaming settings describe *this installation* - which service the
    account is on, which developer application it authenticates as, which loopback input
    carries the audio - and are kept in the application's own preferences so they survive
    between shows. The fade defaults describe *this show* and travel inside the show file.
*/
class SettingsComponent : public juce::Component
{
public:
    SettingsComponent (StreamingSettings streaming,
                       Show& show,
                       std::function<void (const StreamingSettings&)> onStreamingChanged);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void commitStreaming();
    void refresh();

    Show& show;
    StreamingSettings streaming;
    std::function<void (const StreamingSettings&)> streamingChanged;
    bool updating { false };

    juce::Label streamingHeader { {}, "STREAMING SERVICE" };
    juce::Label streamingNote;
    juce::Label providerLabel { {}, "Service" };
    juce::ComboBox providerBox;
    juce::Label clientIdLabel { {}, "Client ID" };
    juce::TextEditor clientIdEditor;
    juce::Label pathLabel { {}, "Audio path" };
    juce::ComboBox pathBox;
    juce::Label inputLabel { {}, "First input" };
    ClickToAdjustSlider inputSlider;
    juce::Label channelsLabel { {}, "Channels" };
    ClickToAdjustSlider channelsSlider;
    juce::Label deviceLabel { {}, "Connect device" };
    juce::TextEditor deviceEditor;

    juce::Label defaultsHeader { {}, "NEW CUE DEFAULTS (SAVED WITH THE SHOW)" };
    juce::Label defaultsNote;
    juce::Label fadeInLabel { {}, "Fade in" };
    ClickToAdjustSlider fadeInSlider;
    juce::Label fadeOutLabel { {}, "Fade out" };
    ClickToAdjustSlider fadeOutSlider;
    juce::Label fadeShapeLabel { {}, "Curve" };
    juce::ComboBox fadeShapeBox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsComponent)
};

/** Window wrapper, so settings can stay open while a show runs. */
class SettingsWindow : public juce::DocumentWindow
{
public:
    SettingsWindow (StreamingSettings streaming,
                    Show& show,
                    std::function<void (const StreamingSettings&)> onStreamingChanged);

    void closeButtonPressed() override;
    std::function<void()> onClose;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsWindow)
};

} // namespace cp
