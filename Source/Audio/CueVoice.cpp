#include "Audio/CueVoice.h"

#include <limits>

namespace cp
{

void CueVoice::prepare (double deviceSampleRate, int maxBlockSize)
{
    sampleRate  = deviceSampleRate > 0.0 ? deviceSampleRate : 48000.0;
    scratchSize = juce::jmax (64, maxBlockSize);
    envelopeScratch.allocate ((size_t) scratchSize, true);

    state.store (State::idle, std::memory_order_release);
}

void CueVoice::setSpec (const VoiceSpec& newSpec)
{
    jassert (getState() == State::idle);
    spec = newSpec;

    // Claim the voice now. render() ignores a reserved voice, so its stale position and
    // envelope state from the previous playback can never leak into the output.
    state.store (State::reserved, std::memory_order_release);
}

//==============================================================================
void CueVoice::triggerStart() noexcept
{
    position         = spec.regionStart;
    preWaitRemaining = juce::jmax ((juce::int64) 0, spec.preWaitSamples);
    samplesPlayed    = 0;
    soundedSamples   = 0;
    passIndex        = 0;
    vampPassIndex    = 0;

    pendingStopAt   = -1;
    pendingStopFade = 0;
    paused.store (false, std::memory_order_relaxed);

    vampReleaseRequested = false;
    vampReleaseFlag.store (false, std::memory_order_relaxed);

    // Only arm the vamp if its markers actually sit inside the trimmed region. A vamp
    // whose end is behind the play head would otherwise loop the boundary handler.
    vampActive = spec.vampEnabled
              && ! spec.fromDeviceInput
              && spec.vampEnd > spec.vampStart
              && spec.vampStart >= spec.regionStart
              && spec.vampEnd   <= spec.regionEnd;

    actionFrom = actionTo = actionCurrent = 1.0f;
    actionPos = actionLen = 0;
    actionShape = FadeShape::equalPower;
    stopWhenActionCompletes = false;

    reportedPosition.store (position, std::memory_order_relaxed);
    reportedSounded.store (0, std::memory_order_relaxed);
    vampingNow.store (vampActive, std::memory_order_relaxed);
    vampPasses.store (0, std::memory_order_relaxed);
    playPasses.store (0, std::memory_order_relaxed);
    reportedGain.store (0.0f, std::memory_order_relaxed);

    state.store (preWaitRemaining > 0 ? State::preWait : State::playing,
                 std::memory_order_release);
}

void CueVoice::requestStop (juce::int64 fadeSamples, FadeShape shape) noexcept
{
    const auto st = getState();

    if (st == State::idle || st == State::finished)
        return;

    // Nothing has been heard yet, so there is nothing to fade.
    if (st == State::reserved || st == State::preWait || fadeSamples <= 0)
    {
        finish();
        return;
    }

    actionFrom  = actionCurrent;
    actionTo    = 0.0f;
    actionPos   = 0;
    actionLen   = fadeSamples;
    actionShape = shape;
    stopWhenActionCompletes = true;

    // A cue on its way out should play through rather than keep circling its vamp.
    vampReleaseRequested = true;
    vampActive = false;

    state.store (State::stopping, std::memory_order_release);
}

void CueVoice::requestVampRelease() noexcept
{
    vampReleaseFlag.store (true, std::memory_order_relaxed);
}

void CueVoice::requestGainRamp (float targetGain, juce::int64 fadeSamples, FadeShape shape) noexcept
{
    const auto st = getState();

    if (st == State::idle || st == State::finished)
        return;

    actionFrom  = actionCurrent;
    actionTo    = juce::jlimit (0.0f, 4.0f, targetGain);
    actionPos   = 0;
    actionLen   = juce::jmax ((juce::int64) 0, fadeSamples);
    actionShape = shape;
    stopWhenActionCompletes = false;
}

void CueVoice::scheduleStop (juce::int64 atSoundedSample, juce::int64 fadeSamples, FadeShape shape) noexcept
{
    pendingStopAt    = atSoundedSample;
    pendingStopFade  = juce::jmax ((juce::int64) 0, fadeSamples);
    pendingStopShape = shape;
}

void CueVoice::finish() noexcept
{
    reportedGain.store (0.0f, std::memory_order_relaxed);
    vampingNow.store (false, std::memory_order_relaxed);
    state.store (State::finished, std::memory_order_release);
}

bool CueVoice::isFinalPass() const noexcept
{
    if (spec.fromDeviceInput)
        return false;               // Endless by nature; the cue fade-out never applies.

    if (vampActive)
        return false;               // Still circling — the end is not in sight yet.

    if (spec.loopEnabled && (spec.loopCount <= 0 || passIndex + 1 < spec.loopCount))
        return false;

    return true;
}

void CueVoice::handleBoundary() noexcept
{
    // --- end of a vamp pass ---------------------------------------------------
    if (vampActive && position >= spec.vampEnd)
    {
        ++vampPassIndex;
        vampPasses.store (vampPassIndex, std::memory_order_relaxed);

        if (vampReleaseRequested)
            vampActive = false;                 // Carry on towards the out point.
        else
            position = spec.vampStart;          // Round again.

        return;
    }

    // --- end of the trimmed region --------------------------------------------
    ++passIndex;
    playPasses.store (passIndex, std::memory_order_relaxed);

    const bool moreLoops = spec.loopEnabled
                        && (spec.loopCount <= 0 || passIndex < spec.loopCount);

    if (! moreLoops)
    {
        finish();
        return;
    }

    position      = spec.regionStart;
    samplesPlayed = 0;   // Re-runs the fade-in on every loop pass, matching the fade-out.

    // Re-arm the vamp for the new pass unless the operator has already let it go.
    vampActive = spec.vampEnabled
              && ! vampReleaseRequested
              && spec.vampEnd > spec.vampStart
              && spec.vampStart >= spec.regionStart
              && spec.vampEnd   <= spec.regionEnd;

    vampPassIndex = 0;
}

//==============================================================================
void CueVoice::render (juce::AudioBuffer<float>& output,
                       const float* const* inputs,
                       int numInputChannels,
                       int numSamples) noexcept
{
    auto st = state.load (std::memory_order_acquire);

    // `reserved` is deliberately silent: the spec is loaded but triggerStart() has not run,
    // so position and envelope still hold whatever the previous playback left behind.
    if (st == State::idle || st == State::reserved || st == State::finished || numSamples <= 0)
        return;

    if (spec.source == nullptr && ! spec.fromDeviceInput)
    {
        finish();
        return;
    }

    if (paused.load (std::memory_order_relaxed))
        return;   // Hold position, output nothing. Timers, fades and stops freeze too.

    if (vampReleaseFlag.exchange (false, std::memory_order_relaxed))
    {
        vampReleaseRequested = true;

        if (spec.vampRelease == VampRelease::immediately)
            vampActive = false;
    }

    int remaining = numSamples;
    int outOffset = 0;

    while (remaining > 0)
    {
        st = state.load (std::memory_order_relaxed);

        if (st == State::idle || st == State::finished)
            break;

        if (preWaitRemaining > 0)
        {
            const auto n = (int) juce::jmin ((juce::int64) remaining, preWaitRemaining);
            preWaitRemaining -= n;
            outOffset += n;
            remaining -= n;

            if (preWaitRemaining == 0 && st == State::preWait)
                state.store (State::playing, std::memory_order_release);

            continue;
        }

        auto boundary = spec.fromDeviceInput ? std::numeric_limits<juce::int64>::max()
                                             : spec.regionEnd;

        // Note the absence of a `position < vampEnd` guard here. With one, the clamp
        // switches off at the exact sample the vamp should wrap on, the run extends to the
        // region end instead, and handleBoundary() never sees the vamp boundary at all.
        // Wrapping is safe without it: handleBoundary() always moves the position back to
        // vampStart, which is below vampEnd, so the loop cannot spin.
        if (vampActive)
            boundary = juce::jmin (boundary, spec.vampEnd);

        const auto available = boundary - position;

        if (available <= 0)
        {
            handleBoundary();
            continue;
        }

        auto n = juce::jmin ((juce::int64) remaining, available, (juce::int64) scratchSize);

        // An armed stop has to land on its exact sample, so cut the run short at it.
        if (pendingStopAt >= 0)
        {
            const auto until = pendingStopAt - soundedSamples;

            if (until <= 0)
            {
                pendingStopAt = -1;
                requestStop (pendingStopFade, pendingStopShape);
                continue;
            }

            n = juce::jmin (n, until);
        }

        // Likewise, end the run where an action fade completes. Without this the voice
        // renders on to the end of the block before noticing it is done — silent, but it
        // reports itself finished up to a block late and its sounded-sample count drifts.
        if (actionPos < actionLen)
            n = juce::jmin (n, actionLen - actionPos);

        renderRun (output, inputs, numInputChannels, outOffset, (int) n);

        position       += n;
        samplesPlayed  += n;
        soundedSamples += n;
        outOffset      += (int) n;
        remaining      -= (int) n;
    }

    reportedPosition.store (position, std::memory_order_relaxed);
    reportedSounded.store (soundedSamples, std::memory_order_relaxed);
    vampingNow.store (vampActive, std::memory_order_relaxed);
}

void CueVoice::renderRun (juce::AudioBuffer<float>& output,
                          const float* const* inputs,
                          int numInputChannels,
                          int outOffset,
                          int numSamples) noexcept
{
    auto* env = envelopeScratch.get();

    const auto finalPass     = isFinalPass();
    const auto fadeOutStart  = spec.regionEnd - spec.fadeOutSamples;
    const auto applyFadeOut  = finalPass && spec.fadeOutSamples > 0 && ! spec.fromDeviceInput;

    for (int i = 0; i < numSamples; ++i)
    {
        float g = spec.gain;

        if (spec.fadeInSamples > 0)
        {
            const auto played = samplesPlayed + i;

            if (played < spec.fadeInSamples)
                g *= evaluateFadeIn (spec.fadeInShape,
                                     (float) ((double) played / (double) spec.fadeInSamples));
        }

        if (applyFadeOut)
        {
            const auto p = position + i;

            if (p >= fadeOutStart)
                g *= evaluateFadeOut (spec.fadeOutShape,
                                      (float) ((double) (p - fadeOutStart) / (double) spec.fadeOutSamples));
        }

        if (actionPos < actionLen)
        {
            const auto t = (float) ((double) actionPos / (double) actionLen);
            actionCurrent = actionFrom + (actionTo - actionFrom) * evaluateFadeIn (actionShape, t);
            ++actionPos;
        }
        else
        {
            actionCurrent = actionTo;
        }

        env[i] = g * actionCurrent;
    }

    const auto numOutputChannels = output.getNumChannels();

    for (int r = 0; r < spec.numRoutes; ++r)
    {
        const auto& route = spec.routes[r];

        if (! juce::isPositiveAndBelow (route.outputChannel, numOutputChannels))
            continue;

        const float* src = nullptr;

        if (spec.fromDeviceInput)
        {
            const auto ch = spec.inputFirstChannel + route.sourceChannel;

            if (inputs == nullptr || ! juce::isPositiveAndBelow (ch, numInputChannels)
                || inputs[ch] == nullptr)
                continue;

            src = inputs[ch] + outOffset;
        }
        else
        {
            const auto ch = juce::jlimit (0, spec.source->getNumChannels() - 1, route.sourceChannel);
            src = spec.source->getReadPointer (ch) + position;
        }

        auto* dst = output.getWritePointer (route.outputChannel) + outOffset;
        const auto routeGain = route.gain;

        for (int i = 0; i < numSamples; ++i)
            dst[i] += src[i] * env[i] * routeGain;
    }

    reportedGain.store (env[numSamples - 1], std::memory_order_relaxed);

    if (stopWhenActionCompletes && actionPos >= actionLen)
        finish();
}

} // namespace cp
