#include "Model/Show.h"

namespace cp
{

namespace
{
    constexpr int showFormatVersion = 1;
}

Show::Show()
{
    cueList.addChangeListener (this);
}

Show::~Show()
{
    cueList.removeChangeListener (this);
}

void Show::changeListenerCallback (juce::ChangeBroadcaster*)
{
    dirty = true;
    sendChangeMessage();
}

juce::String Show::getTitle() const
{
    return showFile.existsAsFile() ? showFile.getFileNameWithoutExtension()
                                   : juce::String ("Untitled show");
}

void Show::markClean()
{
    dirty = false;
    sendChangeMessage();
}

void Show::setMasterGainDb (double db)
{
    const auto clamped = juce::jlimit (-100.0, 12.0, db);

    if (std::abs (clamped - masterGainDb) < 1.0e-9)
        return;

    masterGainDb = clamped;
    dirty = true;
    sendChangeMessage();
}

void Show::createNewShow()
{
    cueList.clear();
    showFile = juce::File();
    masterGainDb = 0.0;
    dirty = false;
    sendChangeMessage();
}

juce::String Show::save (const juce::File& file)
{
    const auto target = file != juce::File() ? file : showFile;

    if (target == juce::File())
        return "No file to save to.";

    auto* root = new juce::DynamicObject();
    root->setProperty ("format",       "cue-player-show");
    root->setProperty ("version",      showFormatVersion);
    root->setProperty ("masterGainDb", masterGainDb);
    root->setProperty ("cues",         cueList.toVar (target.getParentDirectory()));

    const auto json = juce::JSON::toString (juce::var (root), false);

    // Write to a sibling first so a failure part-way through cannot destroy the show
    // that is already on disk — this file is often the only copy at a venue.
    auto temp = target.getSiblingFile (target.getFileName() + ".tmp");
    temp.deleteFile();

    if (! temp.replaceWithText (json))
        return "Could not write to " + temp.getFullPathName();

    if (! temp.moveFileTo (target))
    {
        temp.deleteFile();
        return "Could not replace " + target.getFullPathName();
    }

    showFile = target;
    dirty = false;
    sendChangeMessage();
    return {};
}

juce::String Show::load (const juce::File& file)
{
    if (! file.existsAsFile())
        return "Show file not found: " + file.getFullPathName();

    juce::var parsed;
    const auto result = juce::JSON::parse (file.loadFileAsString(), parsed);

    if (result.failed())
        return "Could not read show: " + result.getErrorMessage();

    if (parsed.getProperty ("format", {}).toString() != "cue-player-show")
        return "That does not look like a Cue Player show file.";

    if ((int) parsed.getProperty ("version", 0) > showFormatVersion)
        return "That show was saved by a newer version of Cue Player.";

    masterGainDb = juce::jlimit (-100.0, 12.0, (double) parsed.getProperty ("masterGainDb", 0.0));

    cueList.removeChangeListener (this);
    cueList.restoreFromVar (parsed.getProperty ("cues", {}), file.getParentDirectory());
    cueList.addChangeListener (this);

    showFile = file;
    dirty = false;
    sendChangeMessage();
    return {};
}

juce::Array<juce::File> Show::collectAudioFiles() const
{
    juce::Array<juce::File> files;

    for (const auto& cue : cueList.all())
        if (cue.type == CueType::audioFile && cue.audioFile != juce::File())
            files.addIfNotAlreadyThere (cue.audioFile);

    return files;
}

juce::Array<juce::Uuid> Show::findMissingFiles() const
{
    juce::Array<juce::Uuid> missing;

    for (const auto& cue : cueList.all())
        if (cue.type == CueType::audioFile && ! cue.audioFile.existsAsFile())
            missing.add (cue.id);

    return missing;
}

} // namespace cp
