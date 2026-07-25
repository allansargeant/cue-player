#include "Model/StreamingSettings.h"

namespace cp
{

juce::String toString (StreamingAudioPath p)
{
    return p == StreamingAudioPath::remoteDevice ? "remoteDevice" : "localCapture";
}

StreamingAudioPath streamingAudioPathFromString (const juce::String& s)
{
    return s == "remoteDevice" ? StreamingAudioPath::remoteDevice
                               : StreamingAudioPath::localCapture;
}

juce::StringArray StreamingSettings::providerKeys()
{
    return { "spotify", "tidal", "appleMusic", "youtubeMusic" };
}

juce::StringArray StreamingSettings::providerNames()
{
    return { "Spotify", "TIDAL", "Apple Music", "YouTube Music" };
}

juce::String StreamingSettings::getProviderDisplayName() const
{
    const auto index = providerKeys().indexOf (provider);
    return index >= 0 ? providerNames()[index] : provider;
}

juce::var StreamingSettings::toVar() const
{
    auto* o = new juce::DynamicObject();

    o->setProperty ("provider",  provider);
    o->setProperty ("clientId",  clientId);
    o->setProperty ("audioPath", toString (audioPath));
    o->setProperty ("captureFirstInputChannel", captureFirstInputChannel);
    o->setProperty ("captureNumChannels",       captureNumChannels);
    o->setProperty ("targetDeviceId", targetDeviceId);

    return juce::var (o);
}

StreamingSettings StreamingSettings::fromVar (const juce::var& v)
{
    StreamingSettings s;

    if (! v.isObject())
        return s;

    if (const auto p = v.getProperty ("provider", {}).toString(); p.isNotEmpty())
        s.provider = p;

    s.clientId  = v.getProperty ("clientId", {}).toString();
    s.audioPath = streamingAudioPathFromString (v.getProperty ("audioPath", {}).toString());
    s.captureFirstInputChannel = juce::jmax (0, (int) v.getProperty ("captureFirstInputChannel", 0));
    s.captureNumChannels       = juce::jlimit (1, 16, (int) v.getProperty ("captureNumChannels", 2));
    s.targetDeviceId           = v.getProperty ("targetDeviceId", {}).toString();

    return s;
}

} // namespace cp
