#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include "Audio/SampleSource.h"

namespace cp
{

/** Loads audio files in the background and keeps one decoded copy per file.

    Because SampleSource holds audio at the *device* rate, changing the audio device's
    sample rate invalidates every entry. setSampleRate() handles that by dropping the
    cache and re-queuing everything it held, so a device change mid-session recovers on
    its own rather than leaving cues silently unplayable.
*/
class SampleCache : private juce::Thread,
                    public  juce::ChangeBroadcaster
{
public:
    SampleCache();
    ~SampleCache() override;

    enum class Status { notLoaded, loading, loaded, failed };

    struct Entry
    {
        Status status { Status::notLoaded };
        std::shared_ptr<SampleSource> source;
        juce::String error;
    };

    /** Sets the rate every file is decoded to. Drops and re-queues the cache if changed. */
    void setSampleRate (double newRate);
    double getSampleRate() const noexcept { return sampleRate.load(); }

    /** Queues @p file for loading if it isn't already cached or in flight. */
    void request (const juce::File& file);

    /** Loads @p file on the calling thread, bypassing the queue. Used when a cue must be
        playable *now* — it blocks, so never call it from the audio thread. */
    std::shared_ptr<SampleSource> loadImmediately (const juce::File& file, juce::String& error);

    /** The decoded source, or nullptr when not loaded (yet, or at all). */
    std::shared_ptr<SampleSource> get (const juce::File& file) const;

    Entry getEntry (const juce::File& file) const;

    /** Drops anything not in @p keep, freeing its memory. */
    void retainOnly (const juce::Array<juce::File>& keep);

    void clear();

    juce::int64 getMemoryUsage() const;
    int  getNumPending() const;

    juce::AudioFormatManager& getFormatManager() noexcept { return formats; }

    /** Formats supported for the file chooser, e.g. "*.wav;*.aiff;*.flac". */
    juce::String getWildcardFilter();

private:
    void run() override;
    static juce::String keyFor (const juce::File& f) { return f.getFullPathName(); }

    juce::AudioFormatManager formats;
    std::atomic<double> sampleRate { 0.0 };

    mutable juce::CriticalSection lock;
    std::map<juce::String, Entry> entries;
    juce::Array<juce::File> queue;

    juce::WaitableEvent workAvailable;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleCache)
};

} // namespace cp
