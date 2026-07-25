#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

namespace cp
{

/** A whole audio file decoded into memory, already resampled to the device sample rate.

    Cue players live or die on seek accuracy: loops and vamps have to land on the same
    sample every pass, and a GO has to start on the first sample with no disk latency.
    Both fall out for free if the file is resident in RAM at the device's own rate, so
    that is what we do — the conversion cost is paid once, on a background thread, and
    the audio thread then does nothing but integer indexing.

    The trade is memory: roughly 11.5 MB per stereo minute at 48 kHz. Sources are shared
    between cues that reference the same file, and the cache reports the total so the
    operator can see it.
*/
class SampleSource
{
public:
    /** Decodes and resamples @p file to @p targetSampleRate.
        Returns nullptr on failure, with the reason in @p errorMessage. */
    static std::shared_ptr<SampleSource> load (const juce::File& file,
                                               double targetSampleRate,
                                               juce::AudioFormatManager& formats,
                                               juce::String& errorMessage);

    const juce::File& getFile() const noexcept          { return file; }

    /** Sample rate of the resident buffer — always the device rate it was loaded for. */
    double getSampleRate() const noexcept               { return sampleRate; }

    /** Sample rate the file was stored at, for display. */
    double getOriginalSampleRate() const noexcept       { return originalSampleRate; }

    int    getNumChannels() const noexcept              { return buffer.getNumChannels(); }
    juce::int64 getNumFrames() const noexcept           { return buffer.getNumSamples(); }
    double getLengthSeconds() const noexcept            { return sampleRate > 0.0 ? (double) getNumFrames() / sampleRate : 0.0; }

    /** Bytes of RAM held by the decoded buffer. */
    juce::int64 getMemoryUsage() const noexcept
    {
        return (juce::int64) buffer.getNumChannels() * buffer.getNumSamples() * (juce::int64) sizeof (float);
    }

    /** Read pointer for @p channel. Never null for a channel in range. */
    const float* getReadPointer (int channel) const noexcept
    {
        return buffer.getReadPointer (juce::jlimit (0, buffer.getNumChannels() - 1, channel));
    }

private:
    SampleSource() = default;

    juce::File file;
    juce::AudioBuffer<float> buffer;
    double sampleRate { 0.0 };
    double originalSampleRate { 0.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleSource)
};

} // namespace cp
