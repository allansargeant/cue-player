#include "GUI/SettingsWindow.h"

namespace cp
{

namespace
{
    constexpr int rowHeight = 26;
    constexpr int rowGap = 6;
    constexpr int labelWidth = 110;
}

SettingsComponent::SettingsComponent (StreamingSettings initialStreaming,
                                      Show& showToEdit,
                                      std::function<void (const StreamingSettings&)> onStreamingChanged)
    : show (showToEdit),
      streaming (std::move (initialStreaming)),
      streamingChanged (std::move (onStreamingChanged))
{
    const auto styleHeader = [this] (juce::Label& label)
    {
        label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        label.setColour (juce::Label::textColourId, colours::textDim);
        addAndMakeVisible (label);
    };

    const auto styleLabel = [this] (juce::Label& label)
    {
        label.setFont (juce::FontOptions (12.0f));
        label.setColour (juce::Label::textColourId, colours::textDim);
        label.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (label);
    };

    const auto styleNote = [this] (juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (juce::FontOptions (11.5f));
        label.setColour (juce::Label::textColourId, colours::textDim);
        label.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (label);
    };

    styleHeader (streamingHeader);
    styleNote (streamingNote,
               "Applies to every show on this machine. No service will hand a desktop app "
               "decrypted audio, so SimpleCue drives the service's own player: either "
               "capturing it from a loopback input, where fades and routing work normally, "
               "or controlling a remote device, where they do not. Each service needs your "
               "own registered developer application.");

    styleLabel (providerLabel);
    providerBox.addItemList (StreamingSettings::providerNames(), 1);
    providerBox.onChange = [this] { commitStreaming(); };
    addAndMakeVisible (providerBox);

    styleLabel (clientIdLabel);
    clientIdEditor.setMultiLine (false);
    clientIdEditor.setTextToShowWhenEmpty ("from your developer dashboard", colours::textDim);
    clientIdEditor.onFocusLost = [this] { commitStreaming(); };
    addAndMakeVisible (clientIdEditor);

    styleLabel (pathLabel);
    pathBox.addItem ("Capture from a local loopback input", 1);
    pathBox.addItem ("Play on a remote Connect device", 2);
    pathBox.onChange = [this] { commitStreaming(); refresh(); };
    addAndMakeVisible (pathBox);

    styleLabel (inputLabel);
    inputSlider.setSliderStyle (juce::Slider::LinearBar);
    inputSlider.setRange (1.0, 64.0, 1.0);
    inputSlider.onValueChange = [this] { commitStreaming(); };
    addAndMakeVisible (inputSlider);

    styleLabel (channelsLabel);
    channelsSlider.setSliderStyle (juce::Slider::LinearBar);
    channelsSlider.setRange (1.0, 8.0, 1.0);
    channelsSlider.onValueChange = [this] { commitStreaming(); };
    addAndMakeVisible (channelsSlider);

    styleLabel (deviceLabel);
    deviceEditor.setMultiLine (false);
    deviceEditor.setTextToShowWhenEmpty ("blank = whatever is playing now", colours::textDim);
    deviceEditor.onFocusLost = [this] { commitStreaming(); };
    addAndMakeVisible (deviceEditor);

    styleHeader (defaultsHeader);
    styleNote (defaultsNote,
               "What a newly added cue starts with. Existing cues keep their own values, so "
               "changing this never reaches back and alters a show you have already built.");

    styleLabel (fadeInLabel);
    fadeInSlider.setSliderStyle (juce::Slider::LinearBar);
    fadeInSlider.setRange (0.0, 60.0, 0.1);
    fadeInSlider.setTextValueSuffix (" s");
    fadeInSlider.onValueChange = [this]
    {
        if (! updating)
            show.setDefaultFadeInTime (fadeInSlider.getValue());
    };
    addAndMakeVisible (fadeInSlider);

    styleLabel (fadeOutLabel);
    fadeOutSlider.setSliderStyle (juce::Slider::LinearBar);
    fadeOutSlider.setRange (0.0, 60.0, 0.1);
    fadeOutSlider.setTextValueSuffix (" s");
    fadeOutSlider.onValueChange = [this]
    {
        if (! updating)
            show.setDefaultFadeOutTime (fadeOutSlider.getValue());
    };
    addAndMakeVisible (fadeOutSlider);

    styleLabel (fadeShapeLabel);
    fadeShapeBox.addItemList (fadeShapeNames(), 1);
    fadeShapeBox.onChange = [this]
    {
        if (! updating)
            show.setDefaultFadeShape ((FadeShape) (fadeShapeBox.getSelectedId() - 1));
    };
    addAndMakeVisible (fadeShapeBox);

    refresh();
    setSize (520, 520);
}

void SettingsComponent::commitStreaming()
{
    if (updating)
        return;

    const auto keys = StreamingSettings::providerKeys();
    const auto index = providerBox.getSelectedId() - 1;

    if (juce::isPositiveAndBelow (index, keys.size()))
        streaming.provider = keys[index];

    streaming.clientId = clientIdEditor.getText().trim();
    streaming.audioPath = pathBox.getSelectedId() == 2 ? StreamingAudioPath::remoteDevice
                                                       : StreamingAudioPath::localCapture;
    streaming.captureFirstInputChannel = (int) inputSlider.getValue() - 1;
    streaming.captureNumChannels = (int) channelsSlider.getValue();
    streaming.targetDeviceId = deviceEditor.getText().trim();

    if (streamingChanged != nullptr)
        streamingChanged (streaming);
}

void SettingsComponent::refresh()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);

    const auto providerIndex = StreamingSettings::providerKeys().indexOf (streaming.provider);
    providerBox.setSelectedId (providerIndex >= 0 ? providerIndex + 1 : 1, juce::dontSendNotification);

    clientIdEditor.setText (streaming.clientId, juce::dontSendNotification);
    pathBox.setSelectedId (streaming.audioPath == StreamingAudioPath::remoteDevice ? 2 : 1,
                           juce::dontSendNotification);
    inputSlider.setValue (streaming.captureFirstInputChannel + 1, juce::dontSendNotification);
    channelsSlider.setValue (streaming.captureNumChannels, juce::dontSendNotification);
    deviceEditor.setText (streaming.targetDeviceId, juce::dontSendNotification);

    const auto capturing = streaming.audioPath == StreamingAudioPath::localCapture;
    inputSlider.setEnabled (capturing);
    channelsSlider.setEnabled (capturing);
    inputLabel.setEnabled (capturing);
    channelsLabel.setEnabled (capturing);
    deviceEditor.setEnabled (! capturing);
    deviceLabel.setEnabled (! capturing);

    fadeInSlider.setValue (show.getDefaultFadeInTime(), juce::dontSendNotification);
    fadeOutSlider.setValue (show.getDefaultFadeOutTime(), juce::dontSendNotification);
    fadeShapeBox.setSelectedId ((int) show.getDefaultFadeShape() + 1, juce::dontSendNotification);
}

void SettingsComponent::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
}

void SettingsComponent::resized()
{
    auto bounds = getLocalBounds().reduced (12);

    const auto row = [&bounds] (juce::Label& label, juce::Component& control)
    {
        auto line = bounds.removeFromTop (rowHeight);
        label.setBounds (line.removeFromLeft (labelWidth));
        line.removeFromLeft (8);
        control.setBounds (line);
        bounds.removeFromTop (rowGap);
    };

    streamingHeader.setBounds (bounds.removeFromTop (16));
    bounds.removeFromTop (2);
    streamingNote.setBounds (bounds.removeFromTop (62));
    bounds.removeFromTop (rowGap);

    row (providerLabel, providerBox);
    row (clientIdLabel, clientIdEditor);
    row (pathLabel, pathBox);
    row (inputLabel, inputSlider);
    row (channelsLabel, channelsSlider);
    row (deviceLabel, deviceEditor);

    bounds.removeFromTop (10);
    defaultsHeader.setBounds (bounds.removeFromTop (16));
    bounds.removeFromTop (2);
    defaultsNote.setBounds (bounds.removeFromTop (34));
    bounds.removeFromTop (rowGap);

    row (fadeInLabel, fadeInSlider);
    row (fadeOutLabel, fadeOutSlider);
    row (fadeShapeLabel, fadeShapeBox);
}

//==============================================================================
SettingsWindow::SettingsWindow (StreamingSettings streaming,
                                Show& show,
                                std::function<void (const StreamingSettings&)> onStreamingChanged)
    : DocumentWindow ("Settings", colours::background, DocumentWindow::closeButton)
{
    setUsingNativeTitleBar (true);
    setContentOwned (new SettingsComponent (std::move (streaming), show,
                                            std::move (onStreamingChanged)), true);
    setResizable (true, false);
    setResizeLimits (460, 440, 1000, 1000);
    centreWithSize (520, 520);
    setVisible (true);
}

void SettingsWindow::closeButtonPressed()
{
    if (onClose != nullptr)
        onClose();
}

} // namespace cp
