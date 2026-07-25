#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "GUI/LookAndFeel.h"
#include "Model/Cue.h"

namespace cp
{

/** Waveform display with draggable in, out and vamp markers.

    Marker times are in seconds from the start of the file, matching the cue model, so
    dragging the in point never shifts the vamp region under it.
*/
class WaveformComponent : public juce::Component,
                          private juce::ChangeListener
{
public:
    explicit WaveformComponent (juce::AudioFormatManager& formatManager);
    ~WaveformComponent() override;

    enum class Marker { none, in, out, vampStart, vampEnd };

    /** Points the display at a cue. Pass nullptr to clear it. */
    void setCue (const Cue* cue);

    /** Live play position in seconds within the file, or a negative value to hide it. */
    void setPlayheadTime (double seconds);

    /** Called while a marker is dragged and again when it is released.
        @p isFinal is true only on release, so callers can avoid spamming undo. */
    std::function<void (Marker, double timeSeconds, bool isFinal)> onMarkerMoved;

    /** Called when the user double-clicks the waveform to audition from that point. */
    std::function<void (double timeSeconds)> onScrubRequested;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    float timeToX (double seconds) const;
    double xToTime (float x) const;

    /** Pixels available for the audio itself, i.e. the width minus the two edge margins. */
    int getAudioWidth() const;
    Marker markerAt (juce::Point<float> position) const;
    void drawMarker (juce::Graphics&, Marker, double time, juce::Colour, const juce::String& label);

    juce::AudioFormatManager& formats;
    juce::AudioThumbnailCache thumbnailCache { 32 };
    juce::AudioThumbnail thumbnail;

    juce::File currentFile;
    double fileLength { 0.0 };

    bool   hasCue { false };
    double inTime { 0.0 };
    double outTime { 0.0 };
    bool   vampVisible { false };
    double vampStartTime { 0.0 };
    double vampEndTime { 0.0 };
    double playhead { -1.0 };

    Marker dragging { Marker::none };
    Marker hovered { Marker::none };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformComponent)
};

} // namespace cp
