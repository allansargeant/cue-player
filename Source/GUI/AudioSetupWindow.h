#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "Audio/AudioEngine.h"
#include "GUI/LookAndFeel.h"

namespace cp
{

/** Device chooser plus the app-level audio options that do not belong to any one cue.

    The backend list JUCE offers here is what the platform build enabled: CoreAudio on
    macOS; ASIO (when built against the SDK), WASAPI and DirectSound on Windows; ALSA and
    JACK on Linux, which is also how PipeWire is reached.
*/
class AudioSetupComponent : public  juce::Component,
                            private juce::ChangeListener
{
public:
    explicit AudioSetupComponent (AudioEngine& engine);
    ~AudioSetupComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void updateSummary();

    AudioEngine& audioEngine;
    juce::AudioDeviceSelectorComponent selector;
    juce::ToggleButton inputsToggle { "Enable inputs (needed for streaming loopback capture)" };
    juce::Label summaryLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioSetupComponent)
};

/** Window wrapper so audio setup can be opened non-modally while a show is running. */
class AudioSetupWindow : public juce::DocumentWindow
{
public:
    explicit AudioSetupWindow (AudioEngine& engine);

    void closeButtonPressed() override;

    /** Called when the window is dismissed, so the owner can drop its pointer. */
    std::function<void()> onClose;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioSetupWindow)
};

} // namespace cp
