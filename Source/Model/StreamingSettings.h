#pragma once

#include <juce_core/juce_core.h>

namespace cp
{

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

//==============================================================================
/** How this machine talks to a streaming service.

    Deliberately global to the installation rather than stored per cue or per show. Which
    service the account is on, which developer application it authenticates as, and which
    loopback input the audio arrives on are all facts about *this rig*. A show that opens
    in the rehearsal room should not carry the venue's capture patch with it, and an
    operator should not have to set the same account up again for every show file.
*/
struct StreamingSettings
{
    /** Provider key: "spotify", "tidal", "appleMusic", "youtubeMusic". */
    juce::String provider { "spotify" };

    /** The client id of the developer application the operator registered with the
        service. Every provider requires one; none of them can be shipped with the app. */
    juce::String clientId;

    StreamingAudioPath audioPath { StreamingAudioPath::localCapture };

    /** First device *input* channel carrying the loopback feed, and how many channels of
        it to take. Only meaningful when audioPath == localCapture. */
    int captureFirstInputChannel { 0 };
    int captureNumChannels { 2 };

    /** Identifier of the Connect/playback device to target, when using remoteDevice.
        Empty means "whatever the account is currently playing on". */
    juce::String targetDeviceId;

    /** Provider keys and their display names, in the same order. */
    static juce::StringArray providerKeys();
    static juce::StringArray providerNames();

    /** Display name for the configured provider, or the raw key if unrecognised. */
    juce::String getProviderDisplayName() const;

    juce::var toVar() const;
    static StreamingSettings fromVar (const juce::var&);
};

} // namespace cp
