#pragma once

#include "Audio/AudioEngine.h"
#include "GUI/LookAndFeel.h"

namespace cp
{

/** The always-visible control strip: GO, stop, panic, pause, vamp release, master level.

    GO is deliberately the largest target on screen and sits hard left, where the operator's
    hand rests. Panic is far from it so the two can never be confused mid-show.
*/
class TransportBar : public  juce::Component,
                     private juce::Timer
{
public:
    explicit TransportBar (AudioEngine& engine);
    ~TransportBar() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onGo;
    std::function<void()> onStopAll;
    std::function<void()> onPanic;
    std::function<void()> onPauseToggle;
    std::function<void()> onReleaseVamp;
    std::function<void()> onAudioSetup;

    /** The cue GO will fire, shown next to the button. */
    void setStandbyText (const juce::String& number, const juce::String& name);
    void setShowStatus (const juce::String& text);

private:
    void timerCallback() override;
    void drawMeters (juce::Graphics&, juce::Rectangle<int>);

    AudioEngine& audioEngine;

    juce::TextButton goButton { "GO" };
    juce::TextButton stopButton { "Stop all" };
    juce::TextButton panicButton { "PANIC" };
    juce::TextButton pauseButton { "Pause" };
    juce::TextButton vampButton { "Release vamp" };
    juce::TextButton setupButton { "Audio setup" };

    juce::Slider masterSlider;
    juce::Label standbyNumberLabel, standbyNameLabel, statusLabel;

    juce::Rectangle<int> meterArea;
    int meterChannels { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportBar)
};

} // namespace cp
