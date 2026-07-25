#pragma once

#include <juce_core/juce_core.h>

#include "Model/ControlMessage.h"
#include "Model/FadeCurve.h"

namespace cp
{

/** Hard ceilings used to size the lock-free structures the audio thread reads.
    Raising these costs only memory; nothing in the engine iterates to the limit. */
namespace limits
{
    static constexpr int maxSourceChannels = 16;  ///< Channels read from one audio file.
    static constexpr int maxOutputChannels = 64;  ///< Device outputs a cue can be routed to.
    static constexpr int maxVoices         = 32;  ///< Simultaneously sounding cue instances.
}

//==============================================================================
/** How a cue hands over to the cue it is linked to.

    A link always has a target (an explicit cue, or "the next cue in the list" when the
    target id is null), a delay, and — for crossfade — a duration.
*/
enum class LinkMode
{
    none = 0,       ///< Nothing follows automatically.
    autoContinue,   ///< Fire the target when this cue *starts*, after `delay` seconds.
    autoFollow,     ///< Fire the target when this cue *finishes*, after `delay` seconds.
    crossfade       ///< Start the target `duration` seconds before this cue's out point,
                    ///< fading this cue out across the overlap.
};

juce::String toString (LinkMode mode);
LinkMode linkModeFromString (const juce::String& s);
juce::StringArray linkModeNames();

struct Link
{
    LinkMode  mode { LinkMode::none };
    juce::Uuid target { juce::Uuid::null() };   ///< Null means "the next cue in the list".
    double    delay { 0.0 };                    ///< Seconds. Ignored by crossfade.
    double    duration { 3.0 };                 ///< Crossfade length in seconds.
    FadeShape shape { FadeShape::equalPower };  ///< Curve used for the crossfade.

    bool targetsNextCue() const noexcept { return target.isNull(); }
};

//==============================================================================
/** When a vamp lets go of its loop after the operator calls for it. */
enum class VampRelease
{
    atEndOfPass = 0,  ///< Finish the current pass, then continue past the vamp out point.
    immediately       ///< Leave the loop at the next sample. Can click on tonal material.
};

juce::String toString (VampRelease r);
VampRelease vampReleaseFromString (const juce::String& s);

//==============================================================================
/** One routed connection: source channel -> device output channel, at a linear gain. */
struct RoutePoint
{
    int   sourceChannel { 0 };
    int   outputChannel { 0 };
    float gain { 1.0f };
};

//==============================================================================
/** What a cue actually plays. */
enum class CueType
{
    audioFile = 0,  ///< A file on disk, decoded and mixed by our own engine.
    streaming,      ///< A track/album/playlist on a streaming service. See StreamingRef.
    control         ///< No audio at all: only the MIDI/OSC messages in outputMessages.
};

juce::String toString (CueType t);
CueType cueTypeFromString (const juce::String& s);

/** Where the audio of a streaming cue physically comes from.

    No streaming service hands a desktop application decrypted PCM, so a streaming cue is
    never decoded by us. Instead we drive the service's own player over its web API and
    choose one of two paths for the sound itself.
*/
enum class StreamingAudioPath
{
    /** The service plays on one of its own Connect devices. We only send transport and
        volume commands; the audio never reaches our mixer, so fades are done with the
        service's volume endpoint and are coarse and network-latent. */
    remoteDevice = 0,

    /** The service's desktop app is pointed at a loopback device (BlackHole, VB-Cable,
        VoiceMeeter, a PipeWire/JACK sink) which we open as an *input*. The audio then
        runs through the normal voice path, so fade curves, gain and the routing matrix
        all behave exactly as they do for a file cue. */
    localCapture
};

juce::String toString (StreamingAudioPath p);
StreamingAudioPath streamingAudioPathFromString (const juce::String& s);

/** A reference to something playable on a streaming service. */
struct StreamingRef
{
    /** Provider key: "spotify", "tidal", "appleMusic", "youtubeMusic". */
    juce::String provider;

    /** Provider-native URI or a pasted share link — "spotify:playlist:37i9...", a TIDAL
        playlist uuid, a music.youtube.com URL. Normalised by the provider adapter. */
    juce::String uri;

    /** Cached human-readable label so the cue list reads sensibly offline. */
    juce::String displayName;

    bool shuffle { false };
    bool repeat  { false };

    /** Identifier of the Connect/playback device to target, when using remoteDevice.
        Empty means "whatever the account is currently playing on". */
    juce::String targetDeviceId;

    StreamingAudioPath audioPath { StreamingAudioPath::localCapture };

    /** First device *input* channel carrying the loopback feed, and how many channels of
        it to take. Only meaningful when audioPath == localCapture. */
    int captureFirstInputChannel { 0 };
    int captureNumChannels { 2 };
};

//==============================================================================
/** A cue: an audio file plus everything that decides how it plays and what follows it.

    Times are in seconds *within the source file*, not within the trimmed region, so
    moving the in point never invalidates the vamp markers.
*/
class Cue
{
public:
    Cue();

    //== Identity ==============================================================
    juce::Uuid   id;
    juce::String number;     ///< Operator-facing cue number. Free text: "12", "12.5", "PRE".
    juce::String name;
    juce::String notes;

    CueType      type { CueType::audioFile };

    //== Source ================================================================
    juce::File   audioFile;      ///< Used when type == audioFile.
    StreamingRef streaming;      ///< Used when type == streaming.

    /** In point, in seconds from the start of the file. */
    double startTime { 0.0 };

    /** Out point, in seconds from the start of the file. A value <= 0 means "end of file".
        Use resolvedEndTime() rather than reading this directly. */
    double endTime { 0.0 };

    /** File duration in seconds, cached when the file is scanned. 0 if not yet known. */
    double fileDuration { 0.0 };
    int    fileChannels { 0 };
    double fileSampleRate { 0.0 };

    //== Level =================================================================
    double gainDb { 0.0 };

    //== Timing ================================================================
    /** Silence inserted between GO and the first sample. Fades start after the pre-wait. */
    double preWait { 0.0 };

    //== Fades =================================================================
    double    fadeInTime { 0.0 };
    FadeShape fadeInShape { FadeShape::equalPower };
    double    fadeOutTime { 0.0 };
    FadeShape fadeOutShape { FadeShape::equalPower };

    //== Loop ==================================================================
    /** Repeats the whole in..out region. */
    bool loopEnabled { false };
    /** Number of times the region plays in total. 0 means loop forever until stopped. */
    int  loopCount { 0 };

    //== Vamp ==================================================================
    /** Loops the sub-region [vampStart, vampEnd] until the operator releases it, then
        carries on to the out point. */
    bool        vampEnabled { false };
    double      vampStart { 0.0 };
    double      vampEnd { 0.0 };
    VampRelease vampRelease { VampRelease::atEndOfPass };

    //== Link ==================================================================
    Link link;

    //== Outgoing control =====================================================
    /** MIDI and OSC messages sent when this cue fires. Available on every cue, so a sound
        cue can fly a lighting cue without a separate control cue beside it in the list;
        a cue of type `control` is simply one that has these and nothing else. */
    std::vector<ControlMessage> outputMessages;

    //== Routing ===============================================================
    /** Sparse source-channel -> output-channel matrix. Empty means "use the default",
        which is a straight 1:1 map of file channels onto the first device outputs. */
    std::vector<RoutePoint> routing;

    //==========================================================================
    /** Out point in seconds, resolving "<= 0 means end of file" against fileDuration. */
    double resolvedEndTime() const noexcept;

    /** Length of the trimmed region in seconds, before any looping or vamping. */
    double trimmedLength() const noexcept;

    /** Whether the vamp markers describe a usable region inside the trimmed range. */
    bool hasUsableVamp() const noexcept;

    /** Playing length in seconds *ignoring* vamp repeats (which are open-ended), including
        loop repeats when the loop count is finite. Returns 0 for an endless cue and for a
        control cue, which has no duration at all — check isOpenEnded() to tell them apart. */
    double playbackLength() const noexcept;

    /** True when nothing can predict when this cue finishes: a streaming cue, an armed
        vamp, or an infinite loop. Links from an open-ended cue cannot be pre-scheduled. */
    bool isOpenEnded() const noexcept;

    /** Builds the effective routing: `routing` when non-empty, otherwise a 1:1 default
        for @p numFileChannels onto @p numDeviceOutputs. */
    std::vector<RoutePoint> effectiveRouting (int numFileChannels, int numDeviceOutputs) const;

    /** True when the cue points at a file that exists and has been scanned. */
    bool isPlayable() const noexcept;

    //== Persistence ===========================================================
    /** Serialises to a JSON object. @p showDirectory, when valid, is used to store the
        audio path relative to the show file so shows stay portable. */
    juce::var toVar (const juce::File& showDirectory) const;

    /** Restores from a JSON object written by toVar(). */
    static Cue fromVar (const juce::var& v, const juce::File& showDirectory);
};

} // namespace cp
