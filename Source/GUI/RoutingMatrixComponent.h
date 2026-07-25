#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "GUI/LookAndFeel.h"
#include "Model/Cue.h"

namespace cp
{

/** Crosspoint grid mapping a cue's source channels onto device output channels.

    Rows are the channels of the cue's own audio; columns are the outputs the device is
    giving us. Click a crosspoint to patch it at unity, click again to clear it,
    double-click to type a trim in dB.
*/
class RoutingMatrixComponent : public juce::Component
{
public:
    RoutingMatrixComponent();

    /** @p sourceLabels names the cue's channels, @p outputLabels the device outputs. */
    void setChannels (juce::StringArray sourceLabels, juce::StringArray outputLabels);

    void setRouting (const std::vector<RoutePoint>& routes, bool isDefaultRouting);

    /** Fires whenever the operator changes a crosspoint. */
    std::function<void (const std::vector<RoutePoint>&)> onRoutingChanged;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    /** Height the grid needs for the current channel counts. */
    int getPreferredHeight() const;

private:
    struct Cell { int row { -1 }; int column { -1 }; bool isValid() const { return row >= 0 && column >= 0; } };

    Cell cellAt (juce::Point<float>) const;
    juce::Rectangle<float> boundsForCell (int row, int column) const;
    const RoutePoint* find (int source, int output) const;
    void setGain (int source, int output, float gain);
    void toggle (int source, int output);

    juce::StringArray sources, outputs;
    std::vector<RoutePoint> routing;
    bool showingDefault { true };

    Cell hovered;
    static constexpr int headerWidth = 92;
    static constexpr int headerHeight = 58;
    static constexpr int cellSize = 26;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RoutingMatrixComponent)
};

} // namespace cp
