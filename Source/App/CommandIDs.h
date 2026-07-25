#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace cp
{

/** Application command ids. Kept in one place so the menu bar, the key mappings and the
    forthcoming OSC/MIDI control layer all fire the same actions. */
namespace CommandIDs
{
    enum
    {
        newShow = 0x2000,
        openShow,
        saveShow,
        saveShowAs,

        addCue,
        addStreamingCue,
        deleteCue,
        duplicateCue,
        moveCueUp,
        moveCueDown,
        renumberCues,

        go,
        stopAll,
        panic,
        pauseResume,
        releaseVamp,
        auditionCue,

        setStandbyToSelected,
        standbyPrevious,
        standbyNext,

        showAudioSetup,
        showAbout
    };
}

} // namespace cp
