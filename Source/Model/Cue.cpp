#include "Model/Cue.h"

namespace cp
{

//==============================================================================
juce::String toString (LinkMode mode)
{
    switch (mode)
    {
        case LinkMode::none:         return "none";
        case LinkMode::autoContinue: return "autoContinue";
        case LinkMode::autoFollow:   return "autoFollow";
        case LinkMode::crossfade:    return "crossfade";
    }

    return "none";
}

LinkMode linkModeFromString (const juce::String& s)
{
    if (s == "autoContinue") return LinkMode::autoContinue;
    if (s == "autoFollow")   return LinkMode::autoFollow;
    if (s == "crossfade")    return LinkMode::crossfade;

    return LinkMode::none;
}

juce::StringArray linkModeNames()
{
    return { "None", "Auto-continue", "Auto-follow", "Crossfade" };
}

//==============================================================================
juce::String toString (VampRelease r)
{
    return r == VampRelease::immediately ? "immediately" : "atEndOfPass";
}

VampRelease vampReleaseFromString (const juce::String& s)
{
    return s == "immediately" ? VampRelease::immediately : VampRelease::atEndOfPass;
}

//==============================================================================
juce::String toString (EndAction a)
{
    return a == EndAction::hardStop ? "hardStop" : "fadeOut";
}

EndAction endActionFromString (const juce::String& s)
{
    return s == "hardStop" ? EndAction::hardStop : EndAction::fadeOut;
}

juce::StringArray endActionNames()
{
    return { "Fade out", "Hard stop" };
}

juce::String toString (EndStepMode m)
{
    switch (m)
    {
        case EndStepMode::always: return "always";
        case EndStepMode::never:  return "never";
        case EndStepMode::automatic:
        default:                  return "automatic";
    }
}

EndStepMode endStepModeFromString (const juce::String& s)
{
    if (s == "always") return EndStepMode::always;
    if (s == "never")  return EndStepMode::never;

    return EndStepMode::automatic;
}

juce::StringArray endStepModeNames()
{
    return { "Only when it cannot end itself", "Always", "Never" };
}

//==============================================================================
juce::String toString (CueType t)
{
    switch (t)
    {
        case CueType::streaming: return "streaming";
        case CueType::control:   return "control";
        case CueType::audioFile:
        default:                 return "audioFile";
    }
}

CueType cueTypeFromString (const juce::String& s)
{
    if (s == "streaming") return CueType::streaming;
    if (s == "control")   return CueType::control;

    return CueType::audioFile;
}

//==============================================================================
Cue::Cue()
    : id (juce::Uuid()) {}

double Cue::resolvedEndTime() const noexcept
{
    if (endTime > 0.0)
        return fileDuration > 0.0 ? juce::jmin (endTime, fileDuration) : endTime;

    return fileDuration;
}

double Cue::trimmedLength() const noexcept
{
    return juce::jmax (0.0, resolvedEndTime() - startTime);
}

bool Cue::hasUsableVamp() const noexcept
{
    if (! vampEnabled)
        return false;

    const auto regionEnd = resolvedEndTime();

    return vampEnd > vampStart
        && vampStart >= startTime
        && (regionEnd <= 0.0 || vampEnd <= regionEnd);
}

bool Cue::isOpenEnded() const noexcept
{
    if (type == CueType::control)
        return false;                  // Fires its messages and is done.

    if (type == CueType::streaming)
        return true;                   // The service decides when it stops.

    if (hasUsableVamp())
        return true;

    return loopEnabled && loopCount <= 0;
}

double Cue::playbackLength() const noexcept
{
    // A control cue occupies no time; a streaming cue's length is decided by the service,
    // and a vamp is open-ended by definition.
    if (type == CueType::control || type == CueType::streaming || hasUsableVamp())
        return 0.0;

    const auto once = trimmedLength();

    if (! loopEnabled)
        return once;

    if (loopCount <= 0)
        return 0.0;   // endless

    return once * (double) loopCount;
}

std::vector<RoutePoint> Cue::effectiveRouting (int numFileChannels, int numDeviceOutputs) const
{
    if (! routing.empty())
    {
        // Drop anything that no longer fits the current file or device, rather than
        // silently writing past the end of a buffer later on.
        std::vector<RoutePoint> valid;
        valid.reserve (routing.size());

        for (const auto& r : routing)
            if (juce::isPositiveAndBelow (r.sourceChannel, numFileChannels)
                && juce::isPositiveAndBelow (r.outputChannel, numDeviceOutputs))
                valid.push_back (r);

        return valid;
    }

    // Default: straight through, file channel n to device output n.
    std::vector<RoutePoint> def;
    const auto n = juce::jmin (numFileChannels, numDeviceOutputs, limits::maxSourceChannels);

    for (int ch = 0; ch < n; ++ch)
        def.push_back ({ ch, ch, 1.0f });

    return def;
}

bool Cue::isPlayable() const noexcept
{
    if (type == CueType::control)
        return ! outputMessages.empty();

    if (type == CueType::streaming)
        return streaming.uri.isNotEmpty();

    return audioFile.existsAsFile() && fileChannels > 0 && fileSampleRate > 0.0;
}

//==============================================================================
namespace
{
    /** Stores a path relative to the show file when the two share a root, so that moving
        a show folder to another machine keeps the audio references intact. */
    juce::String encodePath (const juce::File& f, const juce::File& showDirectory)
    {
        if (f == juce::File())
            return {};

        if (showDirectory.isDirectory() && f.isAChildOf (showDirectory))
            return f.getRelativePathFrom (showDirectory);

        return f.getFullPathName();
    }

    juce::File decodePath (const juce::String& s, const juce::File& showDirectory)
    {
        if (s.isEmpty())
            return {};

        if (juce::File::isAbsolutePath (s))
            return juce::File (s);

        if (showDirectory.isDirectory())
            return showDirectory.getChildFile (s);

        return {};
    }
}

juce::var Cue::toVar (const juce::File& showDirectory) const
{
    auto* o = new juce::DynamicObject();

    o->setProperty ("id",     id.toDashedString());
    o->setProperty ("type",   toString (type));
    o->setProperty ("number", number);
    o->setProperty ("name",   name);
    o->setProperty ("notes",  notes);

    o->setProperty ("audioFile",      encodePath (audioFile, showDirectory));
    o->setProperty ("fileDuration",   fileDuration);
    o->setProperty ("fileChannels",   fileChannels);
    o->setProperty ("fileSampleRate", fileSampleRate);

    if (type == CueType::streaming)
    {
        auto* s = new juce::DynamicObject();
        s->setProperty ("uri",         streaming.uri);
        s->setProperty ("displayName", streaming.displayName);
        s->setProperty ("shuffle",     streaming.shuffle);
        s->setProperty ("repeat",      streaming.repeat);
        o->setProperty ("streaming", juce::var (s));
    }

    o->setProperty ("startTime", startTime);
    o->setProperty ("endTime",   endTime);
    o->setProperty ("gainDb",    gainDb);
    o->setProperty ("preWait",   preWait);

    o->setProperty ("fadeInTime",   fadeInTime);
    o->setProperty ("fadeInShape",  toString (fadeInShape));
    o->setProperty ("fadeOutTime",  fadeOutTime);
    o->setProperty ("fadeOutShape", toString (fadeOutShape));

    o->setProperty ("loopEnabled", loopEnabled);
    o->setProperty ("loopCount",   loopCount);

    o->setProperty ("vampEnabled", vampEnabled);
    o->setProperty ("vampStart",   vampStart);
    o->setProperty ("vampEnd",     vampEnd);
    o->setProperty ("vampRelease", toString (vampRelease));

    o->setProperty ("endAction",   toString (endAction));
    o->setProperty ("endFadeTime", endFadeTime);
    o->setProperty ("endStepMode", toString (endStepMode));

    {
        auto* l = new juce::DynamicObject();
        l->setProperty ("mode",     toString (link.mode));
        l->setProperty ("target",   link.target.isNull() ? juce::String()
                                                         : link.target.toDashedString());
        l->setProperty ("delay",    link.delay);
        l->setProperty ("duration", link.duration);
        l->setProperty ("shape",    toString (link.shape));
        o->setProperty ("link", juce::var (l));
    }

    {
        juce::Array<juce::var> arr;

        for (const auto& m : outputMessages)
            arr.add (m.toVar());

        o->setProperty ("outputMessages", arr);
    }

    {
        juce::Array<juce::var> arr;

        for (const auto& r : routing)
        {
            auto* p = new juce::DynamicObject();
            p->setProperty ("src",  r.sourceChannel);
            p->setProperty ("dst",  r.outputChannel);
            p->setProperty ("gain", (double) r.gain);
            arr.add (juce::var (p));
        }

        o->setProperty ("routing", arr);
    }

    return juce::var (o);
}

Cue Cue::fromVar (const juce::var& v, const juce::File& showDirectory)
{
    Cue c;

    if (! v.isObject())
        return c;

    const auto get = [&v] (const char* key) { return v.getProperty (key, {}); };

    if (const auto idStr = get ("id").toString(); idStr.isNotEmpty())
        c.id = juce::Uuid (idStr);

    c.type   = cueTypeFromString (get ("type").toString());
    c.number = get ("number").toString();
    c.name   = get ("name").toString();
    c.notes  = get ("notes").toString();

    c.audioFile      = decodePath (get ("audioFile").toString(), showDirectory);
    c.fileDuration   = (double) get ("fileDuration");
    c.fileChannels   = (int)    get ("fileChannels");
    c.fileSampleRate = (double) get ("fileSampleRate");

    if (const auto s = get ("streaming"); s.isObject())
    {
        // provider, audioPath and the capture channels used to live here. They are
        // installation settings now, so anything older simply drops those fields.
        c.streaming.uri         = s.getProperty ("uri", {}).toString();
        c.streaming.displayName = s.getProperty ("displayName", {}).toString();
        c.streaming.shuffle     = (bool) s.getProperty ("shuffle", false);
        c.streaming.repeat      = (bool) s.getProperty ("repeat", false);
    }

    c.startTime = (double) get ("startTime");
    c.endTime   = (double) get ("endTime");
    c.gainDb    = (double) get ("gainDb");
    c.preWait   = (double) get ("preWait");

    c.fadeInTime   = (double) get ("fadeInTime");
    c.fadeInShape  = fadeShapeFromString (get ("fadeInShape").toString());
    c.fadeOutTime  = (double) get ("fadeOutTime");
    c.fadeOutShape = fadeShapeFromString (get ("fadeOutShape").toString());

    c.loopEnabled = (bool) get ("loopEnabled");
    c.loopCount   = (int)  get ("loopCount");

    c.vampEnabled = (bool)   get ("vampEnabled");
    c.vampStart   = (double) get ("vampStart");
    c.vampEnd     = (double) get ("vampEnd");
    c.vampRelease = vampReleaseFromString (get ("vampRelease").toString());

    c.endAction   = endActionFromString (get ("endAction").toString());
    c.endFadeTime = juce::jlimit (0.0, 300.0, (double) v.getProperty ("endFadeTime", 3.0));
    c.endStepMode = endStepModeFromString (get ("endStepMode").toString());

    if (const auto l = get ("link"); l.isObject())
    {
        c.link.mode  = linkModeFromString (l.getProperty ("mode", {}).toString());

        if (const auto t = l.getProperty ("target", {}).toString(); t.isNotEmpty())
            c.link.target = juce::Uuid (t);

        c.link.delay    = (double) l.getProperty ("delay", 0.0);
        c.link.duration = (double) l.getProperty ("duration", 3.0);
        c.link.shape    = fadeShapeFromString (l.getProperty ("shape", {}).toString());
    }

    if (const auto* arr = get ("outputMessages").getArray())
        for (const auto& item : *arr)
            c.outputMessages.push_back (ControlMessage::fromVar (item));

    if (const auto* arr = get ("routing").getArray())
    {
        for (const auto& item : *arr)
            c.routing.push_back ({ (int)   item.getProperty ("src", 0),
                                   (int)   item.getProperty ("dst", 0),
                                   (float) (double) item.getProperty ("gain", 1.0) });
    }

    return c;
}

} // namespace cp
