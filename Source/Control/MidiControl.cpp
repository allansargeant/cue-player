#include "Control/MidiControl.h"

namespace cp
{

namespace
{
    constexpr int maxQueuedMessages = 256;
}

MidiControl::MidiControl() = default;

MidiControl::~MidiControl()
{
    cancelPendingUpdate();

    for (auto& input : inputs)
        if (input != nullptr)
            input->stop();

    inputs.clear();
    outputs.clear();
}

//==============================================================================
juce::String MidiControl::applySettings (const ControlSettings& settings)
{
    settingsCopy = settings;

    juce::StringArray problems;

    // --- inputs ---------------------------------------------------------------
    for (auto& input : inputs)
        if (input != nullptr)
            input->stop();

    inputs.clear();

    const auto availableInputs = juce::MidiInput::getAvailableDevices();

    for (const auto& identifier : settings.enabledMidiInputs)
    {
        const auto found = std::find_if (availableInputs.begin(), availableInputs.end(),
                                         [&identifier] (const juce::MidiDeviceInfo& d)
                                         { return d.identifier == identifier; });

        if (found == availableInputs.end())
        {
            // The device was in the saved settings but is not plugged in. Worth saying so
            // rather than silently not listening to the desk someone is about to use.
            problems.add ("MIDI input not found: " + identifier);
            continue;
        }

        if (auto opened = juce::MidiInput::openDevice (identifier, this))
        {
            opened->start();
            inputs.push_back (std::move (opened));
        }
        else
        {
            problems.add ("Could not open MIDI input \"" + found->name + "\".");
        }
    }

    // --- outputs --------------------------------------------------------------
    outputs.clear();
    outputNames.clear();

    const auto availableOutputs = juce::MidiOutput::getAvailableDevices();

    for (const auto& identifier : settings.enabledMidiOutputs)
    {
        const auto found = std::find_if (availableOutputs.begin(), availableOutputs.end(),
                                         [&identifier] (const juce::MidiDeviceInfo& d)
                                         { return d.identifier == identifier; });

        if (found == availableOutputs.end())
        {
            problems.add ("MIDI output not found: " + identifier);
            continue;
        }

        if (auto opened = juce::MidiOutput::openDevice (identifier))
        {
            outputNames.add (found->name);
            outputs.push_back (std::move (opened));
        }
        else
        {
            problems.add ("Could not open MIDI output \"" + found->name + "\".");
        }
    }

    return problems.joinIntoString ("\n");
}

juce::StringArray MidiControl::getOpenInputNames() const
{
    juce::StringArray names;

    for (const auto& input : inputs)
        if (input != nullptr)
            names.add (input->getName());

    return names;
}

juce::StringArray MidiControl::getOpenOutputNames() const
{
    return outputNames;
}

//==============================================================================
void MidiControl::handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message)
{
    {
        const juce::ScopedLock sl (queueLock);

        // A flood of clock or active-sensing traffic must never grow this without bound.
        if ((int) queue.size() >= maxQueuedMessages)
            return;

        queue.push_back ({ message, source != nullptr ? source->getName() : juce::String() });
    }

    triggerAsyncUpdate();
}

void MidiControl::handleAsyncUpdate()
{
    std::vector<Pending> batch;

    {
        const juce::ScopedLock sl (queueLock);
        batch.swap (queue);
    }

    for (const auto& pending : batch)
    {
        const auto& message = pending.message;

        // Never worth reporting, and they arrive constantly.
        if (message.isMidiClock() || message.isActiveSense()
            || message.isMidiStart() || message.isMidiContinue() || message.isMidiStop()
            || message.isSongPositionPointer())
            continue;

        ControlAction action;
        juce::String description;

        if (message.isSysEx())
        {
            IncomingMsc msc;
            int deviceID = 0;
            juce::uint8 command = 0;

            if (settingsCopy.midiShowControlEnabled && parseShowControlMessage (message, msc))
            {
                description = "MSC dev " + juce::String (msc.deviceID)
                            + " cmd 0x" + juce::String::toHexString (msc.command)
                            + (msc.cueNumber.isNotEmpty() ? " cue " + msc.cueNumber : juce::String());
                action = actionForShowControl (msc, settingsCopy);
            }
            else if (settingsCopy.midiMachineControlEnabled
                     && parseMachineControlMessage (message, deviceID, command))
            {
                description = "MMC cmd 0x" + juce::String::toHexString (command);
                action = actionForMachineControl (command);
            }
            else
            {
                description = "SysEx (" + juce::String (message.getSysExDataSize()) + " bytes)";
            }
        }
        else
        {
            description = message.getDescription();
            action = actionForChannelMessage (message, settingsCopy);
        }

        if (onActivity != nullptr)
            onActivity (pending.deviceName + ": " + description, action.isValid());

        if (action.isValid() && onAction != nullptr)
            onAction (action);
    }
}

//==============================================================================
ControlAction MidiControl::actionForShowControl (const IncomingMsc& msc, const ControlSettings& settings)
{
    ControlAction action;
    action.origin = "MSC";

    // 127 is the all-call device id: a receiver must answer it whatever its own id.
    const auto addressedToUs = msc.deviceID == settings.mscDeviceID
                            || msc.deviceID == 127
                            || settings.mscDeviceID == 127;

    if (! addressedToUs)
        return action;

    const auto formatAccepted =
        (msc.commandFormat == msc::formatSound    && settings.mscRespondToSoundFormat)
     || (msc.commandFormat == msc::formatAllTypes && settings.mscRespondToAllTypesFormat);

    if (! formatAccepted)
        return action;

    action.cueNumber = msc.cueNumber;

    switch (msc.command)
    {
        case msc::commandGo:
            // A GO with no cue number means "the next one", which is our standby cue.
            action.type = msc.cueNumber.isNotEmpty() ? ControlActionType::goCue
                                                     : ControlActionType::go;
            break;

        case msc::commandStop:
            action.type = msc.cueNumber.isNotEmpty() ? ControlActionType::stopCue
                                                     : ControlActionType::pause;
            break;

        case msc::commandResume:
            action.type = ControlActionType::resume;
            break;

        case msc::commandGoOff:
            action.type = msc.cueNumber.isNotEmpty() ? ControlActionType::stopCue
                                                     : ControlActionType::stopAll;
            action.value = 0.0;
            break;

        case msc::commandAllOff:
            action.type = ControlActionType::stopAll;
            action.value = 0.0;
            break;

        case msc::commandLoad:
            action.type = ControlActionType::standbyCue;
            break;

        default:
            break;
    }

    return action;
}

ControlAction MidiControl::actionForMachineControl (juce::uint8 command)
{
    ControlAction action;
    action.origin = "MMC";

    switch (command)
    {
        case mmc::commandPlay:
        case mmc::commandDeferredPlay: action.type = ControlActionType::go; break;
        case mmc::commandStop:         action.type = ControlActionType::stopAll;
                                       action.value = 0.0; break;
        case mmc::commandPause:        action.type = ControlActionType::pauseToggle; break;
        default: break;
    }

    return action;
}

ControlAction MidiControl::actionForChannelMessage (const juce::MidiMessage& message,
                                                    const ControlSettings& settings)
{
    ControlAction action;

    MidiTriggerKind kind;
    int number = 0;
    int value = 127;

    if (message.isNoteOn())
    {
        kind = MidiTriggerKind::noteOn;
        number = message.getNoteNumber();
        value = message.getVelocity();
    }
    else if (message.isController())
    {
        kind = MidiTriggerKind::controlChange;
        number = message.getControllerNumber();
        value = message.getControllerValue();
    }
    else if (message.isProgramChange())
    {
        kind = MidiTriggerKind::programChange;
        number = message.getProgramChangeNumber();
    }
    else
    {
        return action;
    }

    const auto channel = message.getChannel();

    for (const auto& binding : settings.midiBindings)
    {
        if (binding.kind != kind || binding.number != number)
            continue;

        if (binding.channel > 0 && binding.channel != channel)
            continue;

        if (binding.useValueAsLevel)
        {
            // A fader driving a level, not a button firing a cue: map 0-127 onto
            // -60 dB..0 dB, with the bottom of the fader as true silence.
            action.type = binding.action;
            action.cueNumber = binding.cueNumber;
            action.value = value <= 0 ? -100.0 : -60.0 + (double) value / 127.0 * 60.0;
        }
        else
        {
            // A note-on with zero velocity is a note-off by another name; ignore it so a
            // key release does not fire the cue a second time.
            if (kind == MidiTriggerKind::noteOn && value == 0)
                continue;

            action.type = binding.action;
            action.cueNumber = binding.cueNumber;
        }

        action.origin = "MIDI " + binding.describe();
        return action;
    }

    return action;
}

//==============================================================================
bool MidiControl::send (const ControlMessage& message)
{
    juce::MidiMessage out;

    switch (message.type)
    {
        case ControlMessageType::midiNoteOn:
            out = juce::MidiMessage::noteOn (message.midiChannel, message.midiData1,
                                             (juce::uint8) message.midiData2);
            break;

        case ControlMessageType::midiNoteOff:
            out = juce::MidiMessage::noteOff (message.midiChannel, message.midiData1);
            break;

        case ControlMessageType::midiControlChange:
            out = juce::MidiMessage::controllerEvent (message.midiChannel, message.midiData1,
                                                      message.midiData2);
            break;

        case ControlMessageType::midiProgramChange:
            out = juce::MidiMessage::programChange (message.midiChannel, message.midiData1);
            break;

        case ControlMessageType::midiShowControl:
            out = buildShowControlMessage (message);
            break;

        case ControlMessageType::midiMachineControl:
            out = buildMachineControlMessage (message);
            break;

        case ControlMessageType::osc:
        default:
            return false;
    }

    if (out.getRawDataSize() <= 0)
        return false;

    bool sentAnything = false;

    for (size_t i = 0; i < outputs.size(); ++i)
    {
        if (message.midiTarget.isNotEmpty()
            && i < (size_t) outputNames.size()
            && outputNames[(int) i] != message.midiTarget)
            continue;

        outputs[i]->sendMessageNow (out);
        sentAnything = true;
    }

    return sentAnything;
}

} // namespace cp
