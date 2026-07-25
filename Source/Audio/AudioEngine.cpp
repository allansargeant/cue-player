#include "Audio/AudioEngine.h"

#include <limits>

namespace cp
{

namespace
{
    constexpr int maxLinkChainDepth = 32;
    constexpr float peakDecayPerTick = 0.82f;
}

AudioEngine::AudioEngine (SampleCache& sampleCache)
    : cache (sampleCache)
{
    for (auto& v : voices)
        v = std::make_unique<CueVoice>();

    for (auto& p : outputPeaks)
        p.store (0.0f);

    startTimerHz (30);
}

AudioEngine::~AudioEngine()
{
    stopTimer();
    shutdown();
}

//==============================================================================
juce::String AudioEngine::initialise (const juce::XmlElement* savedState)
{
    const auto numInputs = inputsEnabled ? limits::maxOutputChannels : 0;

    auto error = deviceManager.initialise (numInputs, limits::maxOutputChannels,
                                           savedState, true);

    if (error.isNotEmpty())
    {
        setError (error);
        return error;
    }

    deviceManager.addAudioCallback (this);
    return {};
}

void AudioEngine::shutdown()
{
    panic();
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
}

std::unique_ptr<juce::XmlElement> AudioEngine::createDeviceStateXml() const
{
    return deviceManager.createStateXml();
}

void AudioEngine::setInputChannelsEnabled (bool shouldBeEnabled)
{
    if (inputsEnabled == shouldBeEnabled)
        return;

    inputsEnabled = shouldBeEnabled;

    // Re-opening is the only way to change the channel count JUCE asked the driver for.
    auto state = deviceManager.createStateXml();
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();

    const auto numInputs = inputsEnabled ? limits::maxOutputChannels : 0;
    auto error = deviceManager.initialise (numInputs, limits::maxOutputChannels, state.get(), true);

    if (error.isNotEmpty())
        setError (error);

    deviceManager.addAudioCallback (this);
    sendChangeMessage();
}

juce::StringArray AudioEngine::getOutputChannelNames() const
{
    juce::StringArray names;

    if (auto* device = deviceManager.getCurrentAudioDevice())
        names = device->getOutputChannelNames();

    const auto required = numOutputChannels.load();

    while (names.size() < required)
        names.add ("Out " + juce::String (names.size() + 1));

    names.removeRange (required, names.size() - required);
    return names;
}

juce::StringArray AudioEngine::getInputChannelNames() const
{
    juce::StringArray names;

    if (auto* device = deviceManager.getCurrentAudioDevice())
        names = device->getInputChannelNames();

    const auto required = numInputChannels.load();

    while (names.size() < required)
        names.add ("In " + juce::String (names.size() + 1));

    names.removeRange (required, names.size() - required);
    return names;
}

float AudioEngine::getOutputPeak (int channel) const
{
    if (! juce::isPositiveAndBelow (channel, (int) outputPeaks.size()))
        return 0.0f;

    return outputPeaks[(size_t) channel].load (std::memory_order_relaxed);
}

//==============================================================================
void AudioEngine::setError (const juce::String& message)
{
    const juce::ScopedLock sl (errorLock);
    lastError = message;
}

juce::String AudioEngine::getLastError() const
{
    const juce::ScopedLock sl (errorLock);
    return lastError;
}

juce::int64 AudioEngine::secondsToSamples (double seconds) const noexcept
{
    if (currentSampleRate <= 0.0 || seconds <= 0.0)
        return 0;

    return (juce::int64) std::llround (seconds * currentSampleRate);
}

//==============================================================================
int AudioEngine::findFreeVoice() const
{
    for (int i = 0; i < (int) voices.size(); ++i)
        if (voices[(size_t) i]->getState() == CueVoice::State::idle)
            return i;

    return -1;
}

bool AudioEngine::buildSpec (const Cue& cue, VoiceSpec& spec,
                             std::shared_ptr<SampleSource>& hold,
                             juce::int64 extraPreWaitSamples,
                             double overrideStartSeconds)
{
    if (currentSampleRate <= 0.0 || numOutputChannels.load() <= 0)
    {
        setError ("No audio device is open.");
        return false;
    }

    spec = VoiceSpec {};
    spec.cueId          = cue.id;
    spec.gain           = juce::Decibels::decibelsToGain ((float) cue.gainDb, -100.0f);
    spec.fadeInSamples  = secondsToSamples (cue.fadeInTime);
    spec.fadeInShape    = cue.fadeInShape;
    spec.fadeOutSamples = secondsToSamples (cue.fadeOutTime);
    spec.fadeOutShape   = cue.fadeOutShape;
    spec.preWaitSamples = secondsToSamples (cue.preWait) + juce::jmax ((juce::int64) 0, extraPreWaitSamples);

    int numSourceChannels = 0;

    if (cue.type == CueType::streaming)
    {
        // Which path the audio takes, and which inputs it arrives on, are properties of the
        // installation rather than of the cue - see StreamingSettings.
        if (streamingSettings.audioPath != StreamingAudioPath::localCapture)
        {
            setError ("Streaming is set to play on a remote device, so it has no audio to mix.");
            return false;
        }

        if (! inputsEnabled || numInputChannels.load() <= 0)
        {
            setError ("Streaming capture needs device inputs. Turn inputs on in Audio setup.");
            return false;
        }

        spec.fromDeviceInput   = true;
        spec.inputFirstChannel = juce::jmax (0, streamingSettings.captureFirstInputChannel);
        spec.inputNumChannels  = juce::jlimit (1, limits::maxSourceChannels,
                                               streamingSettings.captureNumChannels);
        numSourceChannels      = spec.inputNumChannels;

        spec.regionStart = 0;
        spec.regionEnd   = std::numeric_limits<juce::int64>::max();

        // A live input has no end, so there is nothing for a fade-out to be measured from.
        spec.fadeOutSamples = 0;
    }
    else
    {
        juce::String error;
        hold = cache.get (cue.audioFile);

        if (hold == nullptr)
            hold = cache.loadImmediately (cue.audioFile, error);

        if (hold == nullptr)
        {
            setError (error.isNotEmpty() ? error
                                         : "Could not load " + cue.audioFile.getFileName());
            return false;
        }

        spec.source       = hold.get();
        numSourceChannels = hold->getNumChannels();

        const auto frames = hold->getNumFrames();
        const auto startSeconds = overrideStartSeconds >= 0.0 ? overrideStartSeconds : cue.startTime;

        spec.regionStart = juce::jlimit ((juce::int64) 0, frames, secondsToSamples (startSeconds));

        const auto end = cue.resolvedEndTime();
        spec.regionEnd = end > 0.0 ? juce::jlimit (spec.regionStart, frames, secondsToSamples (end))
                                   : frames;

        if (spec.regionEnd <= spec.regionStart)
        {
            setError ("Cue \"" + cue.name + "\" has no audio between its in and out points.");
            return false;
        }

        spec.loopEnabled = cue.loopEnabled;
        spec.loopCount   = cue.loopCount;

        if (cue.hasUsableVamp())
        {
            const auto vs = juce::jlimit (spec.regionStart, spec.regionEnd, secondsToSamples (cue.vampStart));
            const auto ve = juce::jlimit (spec.regionStart, spec.regionEnd, secondsToSamples (cue.vampEnd));

            if (ve > vs)
            {
                spec.vampEnabled = true;
                spec.vampStart   = vs;
                spec.vampEnd     = ve;
                spec.vampRelease = cue.vampRelease;
            }
        }

        const auto regionLength = spec.regionEnd - spec.regionStart;
        spec.fadeInSamples  = juce::jmin (spec.fadeInSamples,  regionLength);
        spec.fadeOutSamples = juce::jmin (spec.fadeOutSamples, regionLength);
    }

    const auto routes = cue.effectiveRouting (numSourceChannels, numOutputChannels.load());

    if (routes.empty())
    {
        setError ("Cue \"" + cue.name + "\" is not routed to any output.");
        return false;
    }

    spec.numRoutes = juce::jmin ((int) routes.size(), maxRoutesPerVoice);

    for (int i = 0; i < spec.numRoutes; ++i)
        spec.routes[i] = routes[(size_t) i];

    return true;
}

//==============================================================================
bool AudioEngine::go (int cueIndex)
{
    juce::Array<juce::Uuid> visited;
    return fireCue (cueIndex, 0, -1, 0, 0, visited);
}

bool AudioEngine::audition (const Cue& cue, double fromSeconds)
{
    const auto voiceIndex = findFreeVoice();

    if (voiceIndex < 0)
    {
        setError ("All " + juce::String (limits::maxVoices) + " voices are in use.");
        return false;
    }

    // Auditioning ignores pre-wait and links: it is a listen, not a rehearsal of the show.
    Cue preview = cue;
    preview.preWait = 0.0;
    preview.link = {};

    VoiceSpec spec;
    std::shared_ptr<SampleSource> hold;

    if (! buildSpec (preview, spec, hold, 0, fromSeconds))
        return false;

    auto& record = records[(size_t) voiceIndex];
    record = {};
    record.hold       = std::move (hold);
    record.cueId      = cue.id;
    record.cueIndex   = -1;
    record.generation = nextGeneration++;
    record.linkScheduled = true;   // Never links onwards.

    voices[(size_t) voiceIndex]->setSpec (spec);
    pushCommand ({ Command::Type::start, voiceIndex, 0, 0, 0.0f, FadeShape::linear });

    sendChangeMessage();
    return true;
}

bool AudioEngine::fireCue (int cueIndex, juce::int64 extraPreWaitSamples,
                           int parentVoice, juce::uint32 parentGeneration,
                           int depth, juce::Array<juce::Uuid>& visited)
{
    if (cueList == nullptr)
        return false;

    const auto* cue = cueList->get (cueIndex);

    if (cue == nullptr)
        return false;

    if (depth > maxLinkChainDepth || visited.contains (cue->id))
    {
        // A ring of links, or a chain longer than any real show. Stop rather than spin.
        setError ("Link chain from cue " + cue->number + " loops back on itself; stopped there.");
        return false;
    }

    visited.add (cue->id);

    const auto preWaitSeconds = cue->preWait
                              + (currentSampleRate > 0.0
                                     ? (double) extraPreWaitSamples / currentSampleRate : 0.0);

    // --- a cue that only sends messages ---------------------------------------
    if (cue->type == CueType::control)
    {
        if (onCueFired != nullptr)
            onCueFired (*cue, preWaitSeconds);

        scheduleLink (cueIndex, -1, secondsToSamples (preWaitSeconds), depth, visited);
        sendChangeMessage();
        return true;
    }

    // --- a streaming cue playing on the service's own device -------------------
    if (cue->type == CueType::streaming
        && streamingSettings.audioPath == StreamingAudioPath::remoteDevice)
    {
        juce::String error;

        if (streamingTransport != nullptr && streamingTransport (*cue, error))
        {
            if (onCueFired != nullptr)
                onCueFired (*cue, preWaitSeconds);

            scheduleLink (cueIndex, -1, secondsToSamples (preWaitSeconds), depth, visited);
            sendChangeMessage();
            return true;
        }

        setError (error.isNotEmpty()
                      ? error
                      : "No streaming account is connected for cue " + cue->number + ".");
        return false;
    }

    const auto voiceIndex = findFreeVoice();

    if (voiceIndex < 0)
    {
        setError ("All " + juce::String (limits::maxVoices) + " voices are in use.");
        return false;
    }

    VoiceSpec spec;
    std::shared_ptr<SampleSource> hold;

    if (! buildSpec (*cue, spec, hold, extraPreWaitSamples))
        return false;

    auto& record = records[(size_t) voiceIndex];
    record = {};
    record.hold             = std::move (hold);
    record.cueId            = cue->id;
    record.cueIndex         = cueIndex;
    record.generation       = nextGeneration++;
    record.parentIndex      = parentVoice;
    record.parentGeneration = parentGeneration;

    voices[(size_t) voiceIndex]->setSpec (spec);
    pushCommand ({ Command::Type::start, voiceIndex, 0, 0, 0.0f, FadeShape::linear });

    if (onCueFired != nullptr)
        onCueFired (*cue, currentSampleRate > 0.0
                              ? (double) spec.preWaitSamples / currentSampleRate : 0.0);

    scheduleLink (cueIndex, voiceIndex, spec.preWaitSamples, depth, visited);

    sendChangeMessage();
    return true;
}

void AudioEngine::scheduleLink (int cueIndex, int sourceVoice, juce::int64 basePreWaitSamples,
                                int depth, juce::Array<juce::Uuid>& visited)
{
    if (cueList == nullptr)
        return;

    const auto* cue = cueList->get (cueIndex);

    if (cue == nullptr || cue->link.mode == LinkMode::none)
        return;

    const auto* target = cueList->resolveLinkTarget (cueIndex);

    if (target == nullptr)
        return;

    const auto targetIndex = cueList->indexOfID (target->id);

    if (targetIndex < 0 || targetIndex == cueIndex)
        return;

    if (sourceVoice >= 0)
        records[(size_t) sourceVoice].linkScheduled = true;

    const auto sourceGeneration = sourceVoice >= 0 ? records[(size_t) sourceVoice].generation : 0u;
    const auto delaySamples     = secondsToSamples (cue->link.delay);
    const auto playLength       = cue->playbackLength();

    // A control cue also has a playbackLength of 0, but for the opposite reason: it takes
    // no time at all rather than an unknowable amount. isOpenEnded() is what separates
    // "cannot be predicted" from "finishes immediately".
    const auto openEnded = cue->isOpenEnded();

    switch (cue->link.mode)
    {
        case LinkMode::autoContinue:
        {
            // Fires relative to this cue's own start, so the pre-wait carries through.
            fireCue (targetIndex, basePreWaitSamples + delaySamples,
                     sourceVoice, sourceGeneration, depth + 1, visited);
            break;
        }

        case LinkMode::autoFollow:
        {
            if (! openEnded)
            {
                fireCue (targetIndex,
                         basePreWaitSamples + secondsToSamples (playLength) + delaySamples,
                         sourceVoice, sourceGeneration, depth + 1, visited);
            }
            else if (sourceVoice >= 0)
            {
                // Infinite loop, live vamp or streaming input: nothing can predict the end,
                // so watch for it instead. Timing is then bounded by the UI timer, ~33 ms.
                pendingFollows.push_back ({ sourceVoice, sourceGeneration, targetIndex,
                                            cue->link.delay });
            }

            break;
        }

        case LinkMode::crossfade:
        {
            if (openEnded)
            {
                // Same problem as an open-ended auto-follow, and a crossfade "before the
                // end" is meaningless without an end. Degrade to following it.
                if (sourceVoice >= 0)
                    pendingFollows.push_back ({ sourceVoice, sourceGeneration, targetIndex, 0.0 });

                break;
            }

            const auto crossfade = juce::jlimit (0.0, playLength, cue->link.duration);
            const auto overlapStart = secondsToSamples (playLength - crossfade);

            fireCue (targetIndex, basePreWaitSamples + overlapStart,
                     sourceVoice, sourceGeneration, depth + 1, visited);

            if (sourceVoice >= 0)
                pushCommand ({ Command::Type::scheduleStop, sourceVoice,
                               overlapStart, secondsToSamples (crossfade),
                               0.0f, cue->link.shape });

            break;
        }

        case LinkMode::none:
        default:
            break;
    }
}

//==============================================================================
void AudioEngine::cancelChildrenOf (int voiceIndex, juce::uint32 generation)
{
    for (int i = 0; i < (int) voices.size(); ++i)
    {
        auto& record = records[(size_t) i];

        if (record.parentIndex != voiceIndex || record.parentGeneration != generation)
            continue;

        auto& voice = *voices[(size_t) i];

        // Only cancel what has not been heard yet. A successor already sounding is the
        // operator's problem to stop, not something we should cut off behind their back.
        if (const auto st = voice.getState();
            st == CueVoice::State::preWait || st == CueVoice::State::reserved)
        {
            const auto childGeneration = record.generation;
            pushCommand ({ Command::Type::stop, i, 0, 0, 0.0f, FadeShape::linear });
            cancelChildrenOf (i, childGeneration);
        }
    }

    for (auto it = pendingFollows.begin(); it != pendingFollows.end();)
        it = (it->sourceVoice == voiceIndex && it->sourceGeneration == generation)
                 ? pendingFollows.erase (it) : it + 1;
}

void AudioEngine::stopVoice (int voiceIndex, double fadeSeconds)
{
    if (! juce::isPositiveAndBelow (voiceIndex, (int) voices.size()))
        return;

    const auto generation = records[(size_t) voiceIndex].generation;

    pushCommand ({ Command::Type::stop, voiceIndex, secondsToSamples (fadeSeconds), 0,
                   0.0f, FadeShape::equalPower });

    cancelChildrenOf (voiceIndex, generation);
    sendChangeMessage();
}

void AudioEngine::stopCue (const juce::Uuid& cueId, double fadeSeconds)
{
    for (int i = 0; i < (int) voices.size(); ++i)
        if (voices[(size_t) i]->isActive() && records[(size_t) i].cueId == cueId)
            stopVoice (i, fadeSeconds);
}

void AudioEngine::stopAll (double fadeSeconds)
{
    const auto fadeSamples = secondsToSamples (fadeSeconds);

    for (int i = 0; i < (int) voices.size(); ++i)
        if (voices[(size_t) i]->isActive())
            pushCommand ({ Command::Type::stop, i, fadeSamples, 0, 0.0f, FadeShape::equalPower });

    pendingFollows.clear();
    sendChangeMessage();
}

void AudioEngine::panic()
{
    for (int i = 0; i < (int) voices.size(); ++i)
        pushCommand ({ Command::Type::stop, i, 0, 0, 0.0f, FadeShape::linear });

    pendingFollows.clear();
    globallyPaused = false;
    sendChangeMessage();
}

void AudioEngine::releaseVamp (const juce::Uuid& cueId)
{
    for (int i = 0; i < (int) voices.size(); ++i)
        if (voices[(size_t) i]->isActive() && records[(size_t) i].cueId == cueId)
            voices[(size_t) i]->requestVampRelease();   // Atomic — safe from here.

    sendChangeMessage();
}

void AudioEngine::releaseAllVamps()
{
    for (auto& v : voices)
        if (v->isVamping())
            v->requestVampRelease();

    sendChangeMessage();
}

bool AudioEngine::isAnythingVamping() const
{
    for (const auto& v : voices)
        if (v->isVamping())
            return true;

    return false;
}

void AudioEngine::setPaused (bool shouldBePaused)
{
    globallyPaused = shouldBePaused;

    for (auto& v : voices)
        if (v->isActive())
            v->setPaused (shouldBePaused);

    sendChangeMessage();
}

void AudioEngine::setMasterGainDb (double db)
{
    masterGainDb = juce::jlimit (-100.0, 12.0, db);
    masterGain.setTargetValue (masterGainDb <= -99.9
                                   ? 0.0f
                                   : juce::Decibels::decibelsToGain ((float) masterGainDb));
}

//==============================================================================
std::vector<AudioEngine::ActiveCueInfo> AudioEngine::getActiveCues() const
{
    std::vector<ActiveCueInfo> result;
    const auto rate = currentSampleRate > 0.0 ? currentSampleRate : 48000.0;

    for (int i = 0; i < (int) voices.size(); ++i)
    {
        const auto& voice = *voices[(size_t) i];
        const auto state = voice.getState();

        if (state == CueVoice::State::idle || state == CueVoice::State::finished)
            continue;

        ActiveCueInfo info;
        info.voiceIndex = i;
        info.cueId      = records[(size_t) i].cueId;
        info.inPreWait  = state == CueVoice::State::preWait || state == CueVoice::State::reserved;
        info.stopping   = state == CueVoice::State::stopping;
        info.vamping    = voice.isVamping();
        info.vampPasses = voice.getVampPassCount();
        info.playPasses = voice.getPlayPassCount();
        info.paused     = voice.isPaused();
        info.gain       = voice.getCurrentGain();
        info.elapsed    = (double) voice.getSoundedSamples() / rate;
        info.position   = (double) voice.getPositionSamples() / rate;

        if (cueList != nullptr)
        {
            if (const auto* cue = cueList->findByID (info.cueId))
            {
                info.number = cue->number;
                info.name   = cue->name;

                const auto length = cue->playbackLength();
                info.remaining = length > 0.0 ? juce::jmax (0.0, length - info.elapsed) : -1.0;
            }
        }

        result.push_back (info);
    }

    return result;
}

int AudioEngine::getNumActiveVoices() const
{
    int count = 0;

    for (const auto& v : voices)
        if (v->isActive())
            ++count;

    return count;
}

//==============================================================================
void AudioEngine::pushCommand (const Command& c)
{
    const auto scope = commandFifo.write (1);

    if (scope.blockSize1 > 0)
        commandStorage[(size_t) scope.startIndex1] = c;
    else if (scope.blockSize2 > 0)
        commandStorage[(size_t) scope.startIndex2] = c;
    else
        setError ("Command queue full - a transport action was dropped.");
}

void AudioEngine::drainCommands() noexcept
{
    const auto ready = commandFifo.getNumReady();

    if (ready <= 0)
        return;

    const auto scope = commandFifo.read (ready);

    const auto apply = [this] (int start, int count)
    {
        for (int n = 0; n < count; ++n)
        {
            const auto& c = commandStorage[(size_t) (start + n)];

            if (! juce::isPositiveAndBelow (c.voiceIndex, (int) voices.size()))
                continue;

            auto& voice = *voices[(size_t) c.voiceIndex];

            switch (c.type)
            {
                case Command::Type::start:        voice.triggerStart(); break;
                case Command::Type::stop:         voice.requestStop (c.i0, c.shape); break;
                case Command::Type::scheduleStop: voice.scheduleStop (c.i0, c.i1, c.shape); break;
                case Command::Type::gainRamp:     voice.requestGainRamp (c.f0, c.i0, c.shape); break;
            }
        }
    };

    apply (scope.startIndex1, scope.blockSize1);
    apply (scope.startIndex2, scope.blockSize2);
}

//==============================================================================
void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    currentSampleRate = device->getCurrentSampleRate();
    currentBlockSize  = device->getCurrentBufferSizeSamples();

    numOutputChannels.store (juce::jmin (limits::maxOutputChannels,
                                         device->getActiveOutputChannels().countNumberOfSetBits()));
    numInputChannels.store (juce::jmin (limits::maxOutputChannels,
                                        device->getActiveInputChannels().countNumberOfSetBits()));

    // Generously oversized so the audio thread never has to reallocate it, even if a
    // driver hands us a larger block than the one it reported here.
    silentScratch.setSize (1, juce::jmax (8192, currentBlockSize * 4));
    silentScratch.clear();
    outputPointers.assign ((size_t) limits::maxOutputChannels, nullptr);

    for (auto& v : voices)
        v->prepare (currentSampleRate, juce::jmax (256, currentBlockSize));

    masterGain.reset (currentSampleRate, 0.02);
    masterGain.setCurrentAndTargetValue (masterGainDb <= -99.9
                                             ? 0.0f
                                             : juce::Decibels::decibelsToGain ((float) masterGainDb));

    cache.setSampleRate (currentSampleRate);

    juce::MessageManager::callAsync ([this] { sendChangeMessage(); });
}

void AudioEngine::audioDeviceStopped()
{
    currentSampleRate = 0.0;
    currentBlockSize  = 0;
    numOutputChannels.store (0);
    numInputChannels.store (0);
}

void AudioEngine::audioDeviceError (const juce::String& errorMessage)
{
    setError (errorMessage);
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                                    int numInputChannelData,
                                                    float* const* outputChannelData,
                                                    int numOutputChannelData,
                                                    int numSamples,
                                                    const juce::AudioIODeviceCallbackContext&)
{
    drainCommands();

    const auto numOuts = juce::jmin (numOutputChannelData, limits::maxOutputChannels);

    // Drivers may hand us null pointers for channels that are not active. Point those at
    // a scratch buffer so downstream code can index channels without checking.
    if ((int) outputPointers.size() < numOuts)
        outputPointers.resize ((size_t) numOuts, nullptr);

    // Never resize here — that would allocate on the audio thread. If a driver overruns
    // the block size it declared, fall back to silence rather than writing out of bounds.
    if (silentScratch.getNumSamples() < numSamples)
    {
        for (int ch = 0; ch < numOutputChannelData; ++ch)
            if (outputChannelData[ch] != nullptr)
                juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);

        return;
    }

    silentScratch.clear();

    for (int ch = 0; ch < numOuts; ++ch)
    {
        outputPointers[(size_t) ch] = outputChannelData[ch] != nullptr
                                          ? outputChannelData[ch]
                                          : silentScratch.getWritePointer (0);

        if (outputChannelData[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);
    }

    juce::AudioBuffer<float> output (outputPointers.data(), numOuts, numSamples);

    for (auto& v : voices)
        v->render (output, inputChannelData, numInputChannelData, numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto g = masterGain.getNextValue();

        for (int ch = 0; ch < numOuts; ++ch)
            output.getWritePointer (ch)[i] *= g;
    }

    for (int ch = 0; ch < numOuts; ++ch)
    {
        if (outputChannelData[ch] == nullptr)
            continue;

        const auto peak = output.getMagnitude (ch, 0, numSamples);
        auto& slot = outputPeaks[(size_t) ch];
        const auto previous = slot.load (std::memory_order_relaxed);
        slot.store (juce::jmax (peak, previous * peakDecayPerTick), std::memory_order_relaxed);
    }

    // Anything the driver gave us beyond maxOutputChannels must still be silenced.
    for (int ch = numOuts; ch < numOutputChannelData; ++ch)
        if (outputChannelData[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputChannelData[ch], numSamples);
}

//==============================================================================
void AudioEngine::timerCallback()
{
    bool changed = false;

    for (int i = 0; i < (int) voices.size(); ++i)
    {
        auto& voice = *voices[(size_t) i];
        auto& record = records[(size_t) i];

        if (voice.isFinished())
        {
            const auto finishedGeneration = record.generation;

            // Fire anything that was waiting for this voice to end before reclaiming it.
            for (auto it = pendingFollows.begin(); it != pendingFollows.end();)
            {
                if (it->sourceVoice == i && it->sourceGeneration == finishedGeneration)
                {
                    juce::Array<juce::Uuid> visited;
                    fireCue (it->targetCueIndex, secondsToSamples (it->delaySeconds),
                             -1, 0, 0, visited);
                    it = pendingFollows.erase (it);
                }
                else
                {
                    ++it;
                }
            }

            record.hold.reset();
            record.cueId = juce::Uuid::null();
            record.cueIndex = -1;
            voice.recycle();
            changed = true;
            continue;
        }

        if (! record.hasSounded && voice.hasStartedSounding())
        {
            record.hasSounded = true;
            changed = true;
        }
    }

    // Decay the meters even when the device is idle, so they fall rather than freeze.
    for (int ch = 0; ch < numOutputChannels.load(); ++ch)
    {
        auto& slot = outputPeaks[(size_t) ch];
        slot.store (slot.load (std::memory_order_relaxed) * peakDecayPerTick,
                    std::memory_order_relaxed);
    }

    if (changed || getNumActiveVoices() > 0)
        sendChangeMessage();
}

} // namespace cp
