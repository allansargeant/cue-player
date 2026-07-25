#include "Audio/SampleSource.h"

#include "Model/Cue.h"

#include <cmath>

namespace cp
{

std::shared_ptr<SampleSource> SampleSource::load (const juce::File& file,
                                                  double targetSampleRate,
                                                  juce::AudioFormatManager& formats,
                                                  juce::String& errorMessage)
{
    errorMessage.clear();

    if (! file.existsAsFile())
    {
        errorMessage = "File not found: " + file.getFullPathName();
        return {};
    }

    if (targetSampleRate <= 0.0)
    {
        errorMessage = "No audio device sample rate to load against.";
        return {};
    }

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr)
    {
        errorMessage = "Unsupported or unreadable audio file: " + file.getFileName();
        return {};
    }

    const auto srcRate     = reader->sampleRate;
    const auto srcFrames   = (juce::int64) reader->lengthInSamples;
    const auto numChannels = juce::jlimit (1, limits::maxSourceChannels, (int) reader->numChannels);

    if (srcRate <= 0.0 || srcFrames <= 0)
    {
        errorMessage = "Audio file is empty: " + file.getFileName();
        return {};
    }

    // A trailing pad of zeroes keeps the interpolator from reading past the end of the
    // decoded data, both for its normal look-ahead and for the extra input consumed while
    // priming away its algorithmic latency (see below).
    static constexpr int tailPad = 256;

    juce::AudioBuffer<float> decoded (numChannels, (int) juce::jmin (srcFrames + tailPad,
                                                                    (juce::int64) std::numeric_limits<int>::max()));
    decoded.clear();

    if (! reader->read (&decoded, 0, (int) srcFrames, 0, true, true))
    {
        errorMessage = "Failed to decode: " + file.getFileName();
        return {};
    }

    std::shared_ptr<SampleSource> result (new SampleSource());
    result->file               = file;
    result->sampleRate         = targetSampleRate;
    result->originalSampleRate = srcRate;

    const auto ratio = srcRate / targetSampleRate;

    if (std::abs (ratio - 1.0) < 1.0e-9)
    {
        // Already at the device rate — filtering it would only cost quality.
        result->buffer.setSize (numChannels, (int) srcFrames);

        for (int ch = 0; ch < numChannels; ++ch)
            result->buffer.copyFrom (ch, 0, decoded, ch, 0, (int) srcFrames);

        return result;
    }

    const auto dstFrames = (juce::int64) std::floor ((double) srcFrames / ratio);

    if (dstFrames <= 0)
    {
        errorMessage = "Audio file too short to resample: " + file.getFileName();
        return {};
    }

    result->buffer.setSize (numChannels, (int) dstFrames);
    result->buffer.clear();

    // The interpolator delays its output by a fixed number of input samples. Left alone,
    // that would start every resampled cue a couple of milliseconds late relative to a cue
    // whose file already matched the device rate — a constant, invisible offset between
    // the 44.1 kHz and 48 kHz halves of a show. Priming the filter with that many samples
    // and throwing the result away puts them back on the same timeline.
    const auto latencyInInputSamples = juce::Interpolators::WindowedSinc::getBaseLatency();
    const auto warmUpFrames = (int) std::ceil (latencyInInputSamples / ratio);

    juce::HeapBlock<float> warmUpScratch ((size_t) juce::jmax (1, warmUpFrames), true);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        // Windowed-sinc is the slowest of JUCE's interpolators and the only one worth
        // using here: this runs once, off the audio thread, and the result is what the
        // audience hears for the rest of the show.
        juce::Interpolators::WindowedSinc interpolator;
        interpolator.reset();

        const auto* in  = decoded.getReadPointer (ch);
        auto*       out = result->buffer.getWritePointer (ch);

        juce::int64 inPos = 0, outPos = 0;

        if (warmUpFrames > 0)
            inPos += interpolator.process (ratio, in, warmUpScratch.get(), warmUpFrames);

        while (outPos < dstFrames)
        {
            const auto outChunk = (int) juce::jmin (dstFrames - outPos, (juce::int64) 65536);
            const auto used = interpolator.process (ratio, in + inPos, out + outPos, outChunk);

            inPos  += used;
            outPos += outChunk;

            if (used <= 0)
                break;  // Defensive: never spin if the interpolator stops consuming.
        }
    }

    return result;
}

} // namespace cp
