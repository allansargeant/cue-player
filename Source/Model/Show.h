#pragma once

#include "Model/CueList.h"

namespace cp
{

/** A show file: the cue list plus the handful of settings that travel with it.

    Audio device selection is deliberately *not* stored here. A show should open on the
    house rig, the rehearsal room laptop and the designer's desk without dragging a device
    name from another machine with it, so device and routing setup live in the
    application's own settings instead.
*/
class Show : private juce::ChangeListener,
             public  juce::ChangeBroadcaster
{
public:
    Show();
    ~Show() override;

    static juce::String fileExtension()  { return ".cueshow"; }
    static juce::String fileWildcard()   { return "*.cueshow"; }

    CueList&       getCueList() noexcept       { return cueList; }
    const CueList& getCueList() const noexcept { return cueList; }

    const juce::File& getFile() const noexcept { return showFile; }
    juce::String getTitle() const;

    bool hasUnsavedChanges() const noexcept    { return dirty; }
    void markClean();

    double getMasterGainDb() const noexcept    { return masterGainDb; }
    void   setMasterGainDb (double db);

    //== Defaults for new cues =================================================
    /** Fade times a newly added cue starts with. Stored with the show rather than with the
        application, because how long a fade should be is a decision about *this* show, not
        about the machine it happens to be running on. Every cue keeps its own values once
        created, so changing the default never reaches back and alters existing cues. */
    double getDefaultFadeInTime() const noexcept   { return defaultFadeInTime; }
    double getDefaultFadeOutTime() const noexcept  { return defaultFadeOutTime; }
    FadeShape getDefaultFadeShape() const noexcept { return defaultFadeShape; }

    void setDefaultFadeInTime (double seconds);
    void setDefaultFadeOutTime (double seconds);
    void setDefaultFadeShape (FadeShape shape);

    /** Stamps the show's defaults onto @p cue. Called when a cue is created. */
    void applyDefaultsTo (Cue& cue) const;

    /** Empties the show. */
    void createNewShow();

    /** Writes to @p file (or the current file when @p file is invalid).
        Returns an error message, or an empty string on success. */
    juce::String save (const juce::File& file = {});

    /** Replaces the current show with the contents of @p file.
        Returns an error message, or an empty string on success. */
    juce::String load (const juce::File& file);

    /** Every distinct audio file the show references, for preloading. */
    juce::Array<juce::File> collectAudioFiles() const;

    /** Cues whose audio file has gone missing since the show was saved. */
    juce::Array<juce::Uuid> findMissingFiles() const;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    CueList cueList;
    juce::File showFile;
    bool dirty { false };
    double masterGainDb { 0.0 };
    double defaultFadeInTime { 0.0 };
    double defaultFadeOutTime { 0.0 };
    FadeShape defaultFadeShape { FadeShape::equalPower };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Show)
};

} // namespace cp
