#include "Control/ControlSettings.h"

namespace cp
{

//==============================================================================
juce::String toString (ControlActionType t)
{
    switch (t)
    {
        case ControlActionType::none:            return "none";
        case ControlActionType::go:              return "go";
        case ControlActionType::goCue:           return "goCue";
        case ControlActionType::stopAll:         return "stopAll";
        case ControlActionType::stopCue:         return "stopCue";
        case ControlActionType::panic:           return "panic";
        case ControlActionType::pause:           return "pause";
        case ControlActionType::resume:          return "resume";
        case ControlActionType::pauseToggle:     return "pauseToggle";
        case ControlActionType::releaseVamp:     return "releaseVamp";
        case ControlActionType::releaseVampCue:  return "releaseVampCue";
        case ControlActionType::standbyCue:      return "standbyCue";
        case ControlActionType::standbyNext:     return "standbyNext";
        case ControlActionType::standbyPrevious: return "standbyPrevious";
        case ControlActionType::selectCue:       return "selectCue";
        case ControlActionType::auditionCue:     return "auditionCue";
        case ControlActionType::masterLevel:     return "masterLevel";
    }

    return "none";
}

ControlActionType controlActionTypeFromString (const juce::String& s)
{
    static const std::pair<const char*, ControlActionType> map[] =
    {
        { "go",              ControlActionType::go },
        { "goCue",           ControlActionType::goCue },
        { "stopAll",         ControlActionType::stopAll },
        { "stopCue",         ControlActionType::stopCue },
        { "panic",           ControlActionType::panic },
        { "pause",           ControlActionType::pause },
        { "resume",          ControlActionType::resume },
        { "pauseToggle",     ControlActionType::pauseToggle },
        { "releaseVamp",     ControlActionType::releaseVamp },
        { "releaseVampCue",  ControlActionType::releaseVampCue },
        { "standbyCue",      ControlActionType::standbyCue },
        { "standbyNext",     ControlActionType::standbyNext },
        { "standbyPrevious", ControlActionType::standbyPrevious },
        { "selectCue",       ControlActionType::selectCue },
        { "auditionCue",     ControlActionType::auditionCue },
        { "masterLevel",     ControlActionType::masterLevel }
    };

    for (const auto& [name, type] : map)
        if (s == name)
            return type;

    return ControlActionType::none;
}

juce::StringArray controlActionTypeNames()
{
    return { "(none)", "GO", "GO cue", "Stop all", "Stop cue", "Panic",
             "Pause", "Resume", "Pause / resume", "Release vamp", "Release vamp of cue",
             "Standby cue", "Standby next", "Standby previous", "Select cue",
             "Audition cue", "Master level" };
}

bool actionNeedsCueNumber (ControlActionType t) noexcept
{
    switch (t)
    {
        case ControlActionType::goCue:
        case ControlActionType::stopCue:
        case ControlActionType::releaseVampCue:
        case ControlActionType::standbyCue:
        case ControlActionType::selectCue:
        case ControlActionType::auditionCue:
            return true;

        default:
            return false;
    }
}

bool actionUsesValue (ControlActionType t) noexcept
{
    return t == ControlActionType::masterLevel
        || t == ControlActionType::stopAll
        || t == ControlActionType::stopCue;
}

//==============================================================================
juce::String toString (MidiTriggerKind k)
{
    switch (k)
    {
        case MidiTriggerKind::noteOn:        return "noteOn";
        case MidiTriggerKind::controlChange: return "controlChange";
        case MidiTriggerKind::programChange: return "programChange";
    }

    return "noteOn";
}

MidiTriggerKind midiTriggerKindFromString (const juce::String& s)
{
    if (s == "controlChange") return MidiTriggerKind::controlChange;
    if (s == "programChange") return MidiTriggerKind::programChange;

    return MidiTriggerKind::noteOn;
}

juce::StringArray midiTriggerKindNames()
{
    return { "Note on", "Control change", "Program change" };
}

juce::String MidiBinding::describe() const
{
    const auto channelText = channel <= 0 ? juce::String ("any ch")
                                          : "ch" + juce::String (channel);

    juce::String trigger;

    switch (kind)
    {
        case MidiTriggerKind::noteOn:
            trigger = "Note " + juce::MidiMessage::getMidiNoteName (number, true, true, 4);
            break;
        case MidiTriggerKind::controlChange:
            trigger = "CC " + juce::String (number);
            break;
        case MidiTriggerKind::programChange:
            trigger = "Program " + juce::String (number);
            break;
    }

    const auto names = controlActionTypeNames();
    const auto actionName = names[(int) action];

    return trigger + "  " + channelText + "   ->   " + actionName
         + (actionNeedsCueNumber (action) && cueNumber.isNotEmpty() ? " " + cueNumber : juce::String())
         + (useValueAsLevel ? "  (value as level)" : juce::String());
}

//==============================================================================
juce::var ControlSettings::toVar() const
{
    auto* o = new juce::DynamicObject();

    o->setProperty ("oscInputEnabled",    oscInputEnabled);
    o->setProperty ("oscInputPort",       oscInputPort);
    o->setProperty ("oscFeedbackEnabled", oscFeedbackEnabled);

    {
        juce::Array<juce::var> arr;

        for (const auto& t : oscTargets)
        {
            auto* item = new juce::DynamicObject();
            item->setProperty ("name",    t.name);
            item->setProperty ("host",    t.host);
            item->setProperty ("port",    t.port);
            item->setProperty ("enabled", t.enabled);
            arr.add (juce::var (item));
        }

        o->setProperty ("oscTargets", arr);
    }

    o->setProperty ("enabledMidiInputs",  juce::var (enabledMidiInputs.joinIntoString ("\n")));
    o->setProperty ("enabledMidiOutputs", juce::var (enabledMidiOutputs.joinIntoString ("\n")));

    o->setProperty ("midiShowControlEnabled",     midiShowControlEnabled);
    o->setProperty ("mscDeviceID",                mscDeviceID);
    o->setProperty ("mscRespondToSoundFormat",    mscRespondToSoundFormat);
    o->setProperty ("mscRespondToAllTypesFormat", mscRespondToAllTypesFormat);
    o->setProperty ("midiMachineControlEnabled",  midiMachineControlEnabled);

    {
        juce::Array<juce::var> arr;

        for (const auto& b : midiBindings)
        {
            auto* item = new juce::DynamicObject();
            item->setProperty ("kind",            toString (b.kind));
            item->setProperty ("channel",         b.channel);
            item->setProperty ("number",          b.number);
            item->setProperty ("useValueAsLevel", b.useValueAsLevel);
            item->setProperty ("action",          toString (b.action));
            item->setProperty ("cueNumber",       b.cueNumber);
            arr.add (juce::var (item));
        }

        o->setProperty ("midiBindings", arr);
    }

    {
        auto* d = new juce::DynamicObject();
        d->setProperty ("artNetEnabled",        dmx.artNetEnabled);
        d->setProperty ("sacnEnabled",          dmx.sacnEnabled);
        d->setProperty ("universe",             dmx.universe);
        d->setProperty ("startAddress",         dmx.startAddress);
        d->setProperty ("triggerThreshold",     dmx.triggerThreshold);
        d->setProperty ("numDirectCueChannels", dmx.numDirectCueChannels);
        o->setProperty ("dmx", juce::var (d));
    }

    return juce::var (o);
}

ControlSettings ControlSettings::fromVar (const juce::var& v)
{
    ControlSettings s;

    if (! v.isObject())
        return createDefault();

    s.oscInputEnabled    = (bool) v.getProperty ("oscInputEnabled", false);
    s.oscInputPort       = juce::jlimit (1, 65535, (int) v.getProperty ("oscInputPort", 53000));
    s.oscFeedbackEnabled = (bool) v.getProperty ("oscFeedbackEnabled", true);

    if (const auto* arr = v.getProperty ("oscTargets", {}).getArray())
    {
        for (const auto& item : *arr)
        {
            OscTarget t;
            t.name    = item.getProperty ("name", "Target").toString();
            t.host    = item.getProperty ("host", "127.0.0.1").toString();
            t.port    = juce::jlimit (1, 65535, (int) item.getProperty ("port", 53001));
            t.enabled = (bool) item.getProperty ("enabled", true);
            s.oscTargets.push_back (t);
        }
    }

    s.enabledMidiInputs  = juce::StringArray::fromLines (v.getProperty ("enabledMidiInputs", {}).toString());
    s.enabledMidiOutputs = juce::StringArray::fromLines (v.getProperty ("enabledMidiOutputs", {}).toString());
    s.enabledMidiInputs.removeEmptyStrings();
    s.enabledMidiOutputs.removeEmptyStrings();

    s.midiShowControlEnabled     = (bool) v.getProperty ("midiShowControlEnabled", true);
    s.mscDeviceID                = juce::jlimit (0, 127, (int) v.getProperty ("mscDeviceID", 0));
    s.mscRespondToSoundFormat    = (bool) v.getProperty ("mscRespondToSoundFormat", true);
    s.mscRespondToAllTypesFormat = (bool) v.getProperty ("mscRespondToAllTypesFormat", true);
    s.midiMachineControlEnabled  = (bool) v.getProperty ("midiMachineControlEnabled", false);

    if (const auto* arr = v.getProperty ("midiBindings", {}).getArray())
    {
        for (const auto& item : *arr)
        {
            MidiBinding b;
            b.kind            = midiTriggerKindFromString (item.getProperty ("kind", {}).toString());
            b.channel         = juce::jlimit (0, 16, (int) item.getProperty ("channel", 1));
            b.number          = juce::jlimit (0, 127, (int) item.getProperty ("number", 60));
            b.useValueAsLevel = (bool) item.getProperty ("useValueAsLevel", false);
            b.action          = controlActionTypeFromString (item.getProperty ("action", {}).toString());
            b.cueNumber       = item.getProperty ("cueNumber", {}).toString();
            s.midiBindings.push_back (b);
        }
    }

    if (const auto d = v.getProperty ("dmx", {}); d.isObject())
    {
        s.dmx.artNetEnabled        = (bool) d.getProperty ("artNetEnabled", false);
        s.dmx.sacnEnabled          = (bool) d.getProperty ("sacnEnabled", false);
        s.dmx.universe             = juce::jlimit (0, 63999, (int) d.getProperty ("universe", 1));
        s.dmx.startAddress         = juce::jlimit (1, 512, (int) d.getProperty ("startAddress", 1));
        s.dmx.triggerThreshold     = juce::jlimit (1, 255, (int) d.getProperty ("triggerThreshold", 128));
        s.dmx.numDirectCueChannels = juce::jlimit (0, 505, (int) d.getProperty ("numDirectCueChannels", 24));
    }

    return s;
}

ControlSettings ControlSettings::createDefault()
{
    ControlSettings s;

    // Nothing listens until the operator turns it on: an audio player that silently opens
    // a network port on first run is not a good guest on a show network.
    s.oscInputEnabled = false;
    s.oscTargets.push_back ({ "Companion", "127.0.0.1", 12321, true });

    return s;
}

} // namespace cp
