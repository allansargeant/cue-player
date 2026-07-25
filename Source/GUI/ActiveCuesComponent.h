#pragma once

#include "Audio/AudioEngine.h"
#include "GUI/LookAndFeel.h"

namespace cp
{

/** Live list of everything currently sounding, waiting or vamping.

    This is the panel the operator watches during a show, so each entry carries its own
    controls: a cue can be stopped or its vamp released without first hunting for it in
    the cue list.
*/
class ActiveCuesComponent : public  juce::Component,
                            private juce::Timer
{
public:
    explicit ActiveCuesComponent (AudioEngine& engine);
    ~ActiveCuesComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    /** Fade time applied when the operator stops a single cue from this panel. */
    void setStopFadeSeconds (double seconds) { stopFade = juce::jmax (0.0, seconds); }

private:
    void timerCallback() override;

    struct Hit { int entry { -1 }; enum class Part { none, stop, vamp } part { Part::none }; };
    Hit hitTest (juce::Point<float>) const;
    juce::Rectangle<int> boundsForEntry (int index) const;

    AudioEngine& audioEngine;
    std::vector<AudioEngine::ActiveCueInfo> entries;
    Hit hovered;
    double stopFade { 2.0 };

    static constexpr int entryHeight = 52;
    static constexpr int headerHeight = 22;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ActiveCuesComponent)
};

} // namespace cp
