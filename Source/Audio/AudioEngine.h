#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include "Audio/CueVoice.h"
#include "Audio/SampleCache.h"
#include "Model/CueList.h"

namespace cp
{

/** Owns the audio device and everything that sounds.

    Threading contract:
      - Every public method here is message-thread only unless it says otherwise.
      - The audio thread reads voices and a lock-free command queue, and nothing else.
        It never allocates, never locks, and never touches a reference count.
      - Decoded audio is kept alive by a shared_ptr held *here*, on the message thread,
        for as long as the voice using it is not idle. The voice holds a raw pointer.

    Link timing:
      When a cue is fired, the length of what it will play is usually known, so the cue it
      links to is started immediately with a pre-wait measured in samples. That makes
      auto-follows and crossfades sample-accurate rather than landing wherever a UI timer
      happened to tick. Only cues with no determinate end — an infinite loop, an unreleased
      vamp, a streaming cue — fall back to the message-thread timer, because nothing can
      predict when those finish.
*/
class AudioEngine : public  juce::AudioIODeviceCallback,
                    private juce::Timer,
                    public  juce::ChangeBroadcaster
{
public:
    explicit AudioEngine (SampleCache& sampleCache);
    ~AudioEngine() override;

    //== Device ================================================================
    /** Opens the audio device, restoring @p savedState if given.
        Returns an empty string on success, or the error JUCE reported. */
    juce::String initialise (const juce::XmlElement* savedState);

    void shutdown();

    juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }
    std::unique_ptr<juce::XmlElement> createDeviceStateXml() const;

    /** Inputs are off by default so the app never triggers a microphone-permission prompt
        it does not need. Turning them on is what makes streaming loopback capture work. */
    void setInputChannelsEnabled (bool shouldBeEnabled);
    bool areInputChannelsEnabled() const noexcept { return inputsEnabled; }

    double getSampleRate() const noexcept        { return currentSampleRate; }
    int    getBlockSize() const noexcept         { return currentBlockSize; }
    int    getNumOutputChannels() const noexcept { return numOutputChannels.load(); }
    int    getNumInputChannels() const noexcept  { return numInputChannels.load(); }

    /** Names for the routing matrix, one per output channel the callback delivers. */
    juce::StringArray getOutputChannelNames() const;
    juce::StringArray getInputChannelNames() const;

    /** Peak level seen on @p channel since the last read, already decayed for display. */
    float getOutputPeak (int channel) const;

    //== Wiring ===============================================================
    void setCueList (CueList* list) noexcept { cueList = list; }

    /** Streaming is configured once for the installation, not per cue. The engine keeps a
        copy so building a voice never has to reach back into the application's settings. */
    void setStreamingSettings (const StreamingSettings& s) { streamingSettings = s; }
    const StreamingSettings& getStreamingSettings() const noexcept { return streamingSettings; }

    /** Hook for streaming cues that play on a remote Connect device. Phase 3 installs the
        provider adapter here; until then such a cue reports that it has no transport. */
    std::function<bool (const Cue&, juce::String& error)> streamingTransport;

    /** Called on the message thread whenever a cue is fired, with the time until its first
        sample sounds. The control layer uses it to schedule the cue's outgoing MIDI and
        OSC messages so they land with the audio rather than with the GO. */
    std::function<void (const Cue&, double secondsUntilAudio)> onCueFired;

    //== Transport ============================================================
    /** Fires the cue at @p cueIndex and schedules whatever it links to.
        Returns false and sets getLastError() if it could not be played. */
    bool go (int cueIndex);

    /** Fires the standby cue and advances standby to the next one. */
    bool goStandby();

    /** Plays @p cue without touching the cue list — used to audition from the inspector. */
    bool audition (const Cue& cue, double fromSeconds);

    void stopCue (const juce::Uuid& cueId, double fadeSeconds);
    void stopVoice (int voiceIndex, double fadeSeconds);
    void stopAll (double fadeSeconds);

    /** Instant silence. Cancels pending pre-waits as well as anything sounding. */
    void panic();

    void releaseVamp (const juce::Uuid& cueId);
    void releaseAllVamps();

    /** True when any voice is currently circling a vamp. */
    bool isAnythingVamping() const;

    void setPaused (bool shouldBePaused);
    bool isPaused() const noexcept { return globallyPaused; }

    void setMasterGainDb (double db);
    double getMasterGainDb() const noexcept { return masterGainDb; }

    //== State for the UI =====================================================
    struct ActiveCueInfo
    {
        int          voiceIndex { -1 };
        juce::Uuid   cueId;
        juce::String number;
        juce::String name;
        double       elapsed { 0.0 };     ///< Seconds sounded.
        double       remaining { 0.0 };   ///< Seconds left, or -1 when open-ended.
        double       position { 0.0 };    ///< Play head, in seconds from the start of the file.
        bool         inPreWait { false };
        bool         vamping { false };
        int          vampPasses { 0 };
        int          playPasses { 0 };
        bool         stopping { false };
        bool         paused { false };
        float        gain { 0.0f };
    };

    std::vector<ActiveCueInfo> getActiveCues() const;
    int  getNumActiveVoices() const;

    juce::String getLastError() const;

    //== AudioIODeviceCallback ================================================
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannelData,
                                           float* const* outputChannelData,
                                           int numOutputChannelData,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceError (const juce::String& errorMessage) override;

private:
    //== Commands (message thread -> audio thread) =============================
    struct Command
    {
        enum class Type { start, stop, scheduleStop, gainRamp };

        Type        type { Type::start };
        int         voiceIndex { -1 };
        juce::int64 i0 { 0 };
        juce::int64 i1 { 0 };
        float       f0 { 0.0f };
        FadeShape   shape { FadeShape::equalPower };
    };

    void pushCommand (const Command& c);
    void drainCommands() noexcept;

    //== Voice bookkeeping (message thread) ====================================
    struct VoiceRecord
    {
        std::shared_ptr<SampleSource> hold;   ///< Keeps decoded audio alive for the voice.
        juce::Uuid    cueId;
        int           cueIndex { -1 };
        juce::uint32  generation { 0 };
        int           parentIndex { -1 };
        juce::uint32  parentGeneration { 0 };
        bool          hasSounded { false };
        bool          linkScheduled { false };
    };

    /** A link that could not be pre-scheduled because its source has no known end. */
    struct PendingFollow
    {
        int          sourceVoice { -1 };
        juce::uint32 sourceGeneration { 0 };
        int          targetCueIndex { -1 };
        double       delaySeconds { 0.0 };
    };

    int  findFreeVoice() const;
    bool buildSpec (const Cue& cue, VoiceSpec& spec,
                    std::shared_ptr<SampleSource>& hold,
                    juce::int64 extraPreWaitSamples,
                    double overrideStartSeconds = -1.0);

    /** Fires one cue and schedules whatever it links to. Returns false if it could not be
        played; a control cue succeeds without occupying a voice. */
    bool fireCue (int cueIndex, juce::int64 extraPreWaitSamples,
                  int parentVoice, juce::uint32 parentGeneration,
                  int depth, juce::Array<juce::Uuid>& visited);

    void scheduleLink (int cueIndex, int sourceVoice, juce::int64 basePreWaitSamples,
                       int depth, juce::Array<juce::Uuid>& visited);

    void cancelChildrenOf (int voiceIndex, juce::uint32 generation);

    juce::int64 secondsToSamples (double seconds) const noexcept;

    void timerCallback() override;

    //== Members ===============================================================
    SampleCache& cache;
    juce::AudioDeviceManager deviceManager;
    CueList* cueList { nullptr };
    StreamingSettings streamingSettings;

    std::array<std::unique_ptr<CueVoice>, limits::maxVoices> voices;
    std::array<VoiceRecord, limits::maxVoices> records;
    juce::uint32 nextGeneration { 1 };

    std::vector<PendingFollow> pendingFollows;

    static constexpr int commandQueueSize = 512;
    juce::AbstractFifo commandFifo { commandQueueSize };
    std::array<Command, commandQueueSize> commandStorage;

    double currentSampleRate { 0.0 };
    int    currentBlockSize { 0 };
    std::atomic<int> numOutputChannels { 0 };
    std::atomic<int> numInputChannels { 0 };

    bool inputsEnabled { false };
    bool globallyPaused { false };

    double masterGainDb { 0.0 };
    juce::LinearSmoothedValue<float> masterGain { 1.0f };

    juce::AudioBuffer<float> silentScratch;   ///< Stands in for null output pointers.
    std::vector<float*> outputPointers;

    std::array<std::atomic<float>, limits::maxOutputChannels> outputPeaks {};

    mutable juce::CriticalSection errorLock;
    juce::String lastError;
    void setError (const juce::String& message);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};

} // namespace cp
