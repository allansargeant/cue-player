#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace cp
{

/** Colours shared across the app.

    Chosen for a dark backstage position: low overall brightness so the screen does not
    light the operator, but with GO, stop and vamp far enough apart in hue that they are
    unmistakable at a glance under pressure.
*/
namespace colours
{
    const juce::Colour background   { 0xff14161a };
    const juce::Colour panel        { 0xff1c1f26 };
    const juce::Colour panelLight   { 0xff262b34 };
    const juce::Colour outline      { 0xff333945 };
    const juce::Colour text         { 0xffe6e9ef };
    const juce::Colour textDim      { 0xff8b93a3 };

    const juce::Colour go           { 0xff2ecc71 };
    const juce::Colour stop         { 0xffe74c3c };
    const juce::Colour vamp         { 0xfff1c40f };
    const juce::Colour loop         { 0xff5dade2 };
    const juce::Colour standby      { 0xff9b59b6 };
    const juce::Colour preWait      { 0xff7f8c8d };
    const juce::Colour meterLow     { 0xff2ecc71 };
    const juce::Colour meterHigh    { 0xffe67e22 };
    const juce::Colour meterClip    { 0xffe74c3c };
}

class CuePlayerLookAndFeel : public juce::LookAndFeel_V4
{
public:
    CuePlayerLookAndFeel();

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawAsHighlighted, bool shouldDrawAsDown) override;

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;
};

/** Formats seconds as mm:ss.t, or h:mm:ss.t past an hour. Negative input gives "--:--". */
juce::String formatTime (double seconds);

} // namespace cp
