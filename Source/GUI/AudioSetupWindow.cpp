#include "GUI/AudioSetupWindow.h"

namespace cp
{

AudioSetupComponent::AudioSetupComponent (AudioEngine& engine)
    : audioEngine (engine),
      selector (engine.getDeviceManager(),
                0, limits::maxOutputChannels,     // input channel range
                0, limits::maxOutputChannels,     // output channel range
                false,                            // no MIDI input list yet — Phase 2
                false,                            // no MIDI output list yet — Phase 2
                true,                             // show channel selector as stereo pairs? no:
                false)                            // ...advanced options are more useful here
{
    addAndMakeVisible (selector);

    inputsToggle.setToggleState (engine.areInputChannelsEnabled(), juce::dontSendNotification);
    inputsToggle.onClick = [this]
    {
        audioEngine.setInputChannelsEnabled (inputsToggle.getToggleState());
        updateSummary();
    };
    addAndMakeVisible (inputsToggle);

    summaryLabel.setFont (juce::FontOptions (11.5f));
    summaryLabel.setColour (juce::Label::textColourId, colours::textDim);
    summaryLabel.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (summaryLabel);

    audioEngine.addChangeListener (this);
    updateSummary();
    setSize (560, 620);
}

AudioSetupComponent::~AudioSetupComponent()
{
    audioEngine.removeChangeListener (this);
}

void AudioSetupComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateSummary();
}

void AudioSetupComponent::updateSummary()
{
    juce::StringArray lines;

    const auto rate = audioEngine.getSampleRate();
    const auto block = audioEngine.getBlockSize();

    if (rate > 0.0)
    {
        lines.add (juce::String (rate / 1000.0, 1) + " kHz, " + juce::String (block)
                   + " sample buffer ("
                   + juce::String (block * 1000.0 / rate, 1) + " ms)");
        lines.add (juce::String (audioEngine.getNumOutputChannels()) + " output channels, "
                   + juce::String (audioEngine.getNumInputChannels()) + " inputs");
    }
    else
    {
        lines.add ("No audio device is open.");
    }

    // Changing the rate means every cue has to be decoded again at the new rate, which is
    // worth saying out loud before someone does it two minutes before a show.
    lines.add ("Changing the sample rate reloads every cue's audio at the new rate.");

    if (const auto error = audioEngine.getLastError(); error.isNotEmpty())
        lines.add ("Last error: " + error);

    summaryLabel.setText (lines.joinIntoString ("\n"), juce::dontSendNotification);
}

void AudioSetupComponent::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
}

void AudioSetupComponent::resized()
{
    auto bounds = getLocalBounds().reduced (10);
    summaryLabel.setBounds (bounds.removeFromBottom (72));
    bounds.removeFromBottom (6);
    inputsToggle.setBounds (bounds.removeFromBottom (26));
    bounds.removeFromBottom (6);
    selector.setBounds (bounds);
}

//==============================================================================
AudioSetupWindow::AudioSetupWindow (AudioEngine& engine)
    : DocumentWindow ("SimpleCue - Audio setup", colours::background, DocumentWindow::closeButton)
{
    setUsingNativeTitleBar (true);
    setContentOwned (new AudioSetupComponent (engine), true);
    setResizable (true, false);
    setResizeLimits (460, 420, 1200, 1400);
    centreWithSize (560, 620);
    setVisible (true);
}

void AudioSetupWindow::closeButtonPressed()
{
    if (onClose != nullptr)
        onClose();
}

} // namespace cp
