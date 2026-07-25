#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Control/ControlSettings.h"
#include "Model/Show.h"

namespace cp
{

/** Builds the demo show used by `SimpleCue --screenshots`, and the audio it points at.

    Kept apart from the app proper so the screenshots in the README can be regenerated from
    a known state after a UI change, instead of being a set of one-off captures that slowly
    stop resembling the software.
*/
namespace screenshots
{
    /** Writes a handful of short demo files into @p directory and returns them in the order
        the demo show expects. The content is synthesised, not sampled, so nothing here
        carries anyone's copyright — and it is shaped to give the waveform display something
        recognisable rather than a featureless block. */
    juce::Array<juce::File> writeDemoAudio (const juce::File& directory);

    /** Fills @p show with cues covering the features worth showing: trims, fades, a loop, a
        vamp, links of each kind, a streaming cue and a control cue. */
    void buildDemoShow (Show& show, const juce::Array<juce::File>& audioFiles);

    /** Control settings that make the setup window worth photographing: OSC listening,
        Art-Net on, and a couple of MIDI bindings. */
    ControlSettings demoControlSettings();

    /** Saves @p component as a PNG. Renders offscreen through JUCE's own software
        rasteriser, so this needs no screen-recording permission and captures exactly what
        the app draws. Returns false if the file could not be written. */
    bool capture (juce::Component& component, const juce::File& destination, float scale = 2.0f);
}

} // namespace cp
