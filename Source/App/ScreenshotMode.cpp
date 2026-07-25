#include "App/ScreenshotMode.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>

namespace cp::screenshots
{

namespace
{
    constexpr double demoRate = 48000.0;

    /** A cheap deterministic noise source. Deterministic matters: regenerating the
        screenshots should produce the same waveforms, not a slightly different picture. */
    struct Rng
    {
        juce::uint32 state { 0x2545f491 };

        float next() noexcept
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return (float) ((double) state / (double) 0xffffffffu) * 2.0f - 1.0f;
        }
    };

    void writeWav (const juce::File& file, const juce::AudioBuffer<float>& buffer)
    {
        file.deleteFile();

        juce::WavAudioFormat wav;

        if (auto* stream = file.createOutputStream().release())
        {
            std::unique_ptr<juce::AudioFormatWriter> writer (
                wav.createWriterFor (stream, demoRate,
                                     (unsigned int) buffer.getNumChannels(), 24, {}, 0));

            if (writer != nullptr)
                writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
        }
    }

    /** Musical-ish bed: a slow chord with a breathing envelope. Gives the waveform display
        a shape with visible phrasing rather than a solid block. */
    juce::AudioBuffer<float> makeMusicBed (double seconds, float peak)
    {
        const auto frames = (int) (seconds * demoRate);
        juce::AudioBuffer<float> buffer (2, frames);

        const double partials[] = { 110.0, 164.81, 220.0, 277.18, 329.63 };

        for (int i = 0; i < frames; ++i)
        {
            const auto t = (double) i / demoRate;

            // Two slow envelopes at different rates so phrases do not line up predictably.
            const auto envelope = 0.45 + 0.35 * std::sin (t * 0.21) + 0.2 * std::sin (t * 0.07 + 1.3);

            double left = 0.0, right = 0.0;

            for (int p = 0; p < (int) juce::numElementsInArray (partials); ++p)
            {
                const auto weight = 1.0 / (double) (p + 2);
                left  += weight * std::sin (juce::MathConstants<double>::twoPi * partials[p] * t);
                right += weight * std::sin (juce::MathConstants<double>::twoPi * partials[p] * t * 1.002 + 0.4);
            }

            buffer.setSample (0, i, (float) (left  * envelope) * peak * 0.35f);
            buffer.setSample (1, i, (float) (right * envelope) * peak * 0.35f);
        }

        return buffer;
    }

    /** A swell that builds and breaks: the obvious thing to hang a vamp and a fade off. */
    juce::AudioBuffer<float> makeStorm (double seconds, float peak)
    {
        const auto frames = (int) (seconds * demoRate);
        juce::AudioBuffer<float> buffer (2, frames);
        Rng rng;

        float lowpassL = 0.0f, lowpassR = 0.0f;

        for (int i = 0; i < frames; ++i)
        {
            const auto position = (double) i / (double) frames;
            const auto build = std::pow (position, 1.6);

            // Rumble: filtered noise, opening up as the cue builds.
            const auto cutoff = (float) (0.004 + 0.03 * build);
            lowpassL += cutoff * (rng.next() - lowpassL);
            lowpassR += cutoff * (rng.next() - lowpassR);

            // Occasional cracks in the second half.
            auto crack = 0.0f;

            if (position > 0.45)
            {
                const auto phase = std::fmod (position * 9.0, 1.0);
                if (phase < 0.02)
                    crack = rng.next() * (float) (1.0 - phase / 0.02) * 0.8f;
            }

            const auto envelope = (float) (0.25 + 0.75 * build);
            buffer.setSample (0, i, (lowpassL * 6.0f + crack) * envelope * peak);
            buffer.setSample (1, i, (lowpassR * 6.0f + crack * 0.8f) * envelope * peak);
        }

        return buffer;
    }

    /** Short, sharp, and over: a transient with an obvious start. */
    juce::AudioBuffer<float> makeSlam (double seconds, float peak)
    {
        const auto frames = (int) (seconds * demoRate);
        juce::AudioBuffer<float> buffer (2, frames);
        Rng rng;

        for (int i = 0; i < frames; ++i)
        {
            const auto t = (double) i / demoRate;
            const auto envelope = std::exp (-t * 7.0);
            const auto body = std::sin (juce::MathConstants<double>::twoPi * 62.0 * t) * 0.7
                            + rng.next() * 0.5;

            buffer.setSample (0, i, (float) (body * envelope) * peak);
            buffer.setSample (1, i, (float) (body * envelope) * peak * 0.95f);
        }

        return buffer;
    }
}

//==============================================================================
juce::Array<juce::File> writeDemoAudio (const juce::File& directory)
{
    directory.createDirectory();

    const struct
    {
        const char* name;
        juce::AudioBuffer<float> (*make) (double, float);
        double seconds;
        float peak;
    } definitions[] =
    {
        { "preshow-bed.wav",   makeMusicBed, 96.0, 0.55f },
        { "storm-builds.wav",  makeStorm,    42.0, 0.75f },
        { "door-slam.wav",     makeSlam,      2.5, 0.9f  },
        { "interval-bed.wav",  makeMusicBed, 64.0, 0.5f  }
    };

    juce::Array<juce::File> files;

    for (const auto& definition : definitions)
    {
        auto file = directory.getChildFile (definition.name);
        writeWav (file, definition.make (definition.seconds, definition.peak));
        files.add (file);
    }

    return files;
}

//==============================================================================
void buildDemoShow (Show& show, const juce::Array<juce::File>& audioFiles)
{
    auto& list = show.getCueList();
    list.clear();

    // Give the Settings window something to show rather than a column of zeroes.
    show.setDefaultFadeInTime (2.0);
    show.setDefaultFadeOutTime (3.0);
    show.setDefaultFadeShape (FadeShape::equalPower);

    const auto fileAt = [&audioFiles] (int index)
    {
        return juce::isPositiveAndBelow (index, audioFiles.size()) ? audioFiles[index] : juce::File();
    };

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    const auto scan = [&formats] (Cue& cue, const juce::File& file)
    {
        cue.audioFile = file;

        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

        if (reader != nullptr)
        {
            cue.fileSampleRate = reader->sampleRate;
            cue.fileChannels   = (int) reader->numChannels;
            cue.fileDuration   = reader->sampleRate > 0.0
                                     ? (double) reader->lengthInSamples / reader->sampleRate : 0.0;
        }
    };

    // 1 - pre-show bed, looping forever under a fade-in.
    {
        Cue cue;
        cue.number = "1";
        cue.name = "Pre-show bed";
        cue.notes = "Runs from house open. Fade under the announcement.";
        scan (cue, fileAt (0));
        cue.fadeInTime = 6.0;
        cue.fadeInShape = FadeShape::equalPower;
        cue.gainDb = -6.0;
        cue.loopEnabled = true;
        cue.loopCount = 0;
        list.insert (cue);
    }

    // 2 - trimmed, with a crossfade into the next cue.
    {
        Cue cue;
        cue.number = "2";
        cue.name = "Interval music";
        scan (cue, fileAt (3));
        cue.startTime = 4.0;
        cue.endTime = 48.0;
        cue.fadeInTime = 2.0;
        cue.fadeOutTime = 5.0;
        cue.fadeOutShape = FadeShape::sCurve;
        cue.gainDb = -3.0;
        cue.link.mode = LinkMode::crossfade;
        cue.link.duration = 6.0;
        list.insert (cue);
    }

    // 3 - the vamp, which is the feature worth showing off.
    {
        Cue cue;
        cue.number = "3";
        cue.name = "Storm builds";
        cue.notes = "Vamp under the dialogue. Release on the door slam.";
        scan (cue, fileAt (1));
        cue.startTime = 1.5;
        cue.fadeInTime = 3.0;
        cue.vampEnabled = true;
        // Early in the cue, so the capture reaches the vamp within a couple of seconds and
        // the screenshot shows it actually circling rather than merely armed.
        cue.vampStart = 3.0;
        cue.vampEnd = 11.0;
        cue.vampRelease = VampRelease::atEndOfPass;
        cue.link.mode = LinkMode::autoFollow;
        list.insert (cue);
    }

    // 4 - short stab with a pre-wait, plus an outgoing MSC message to the lighting desk.
    {
        Cue cue;
        cue.number = "3.5";
        cue.name = "Door slam";
        scan (cue, fileAt (2));
        cue.preWait = 0.4;
        cue.gainDb = 1.5;

        ControlMessage message;
        message.type = ControlMessageType::midiShowControl;
        message.mscCommandFormat = msc::formatLighting;
        message.mscCommand = msc::commandGo;
        message.mscCueNumber = "58";
        cue.outputMessages.push_back (message);

        list.insert (cue);
    }

    // 5 - a control cue: no audio, just messages out.
    {
        Cue cue;
        cue.number = "4";
        cue.name = "Blackout - fly LX and video";
        cue.type = CueType::control;

        ControlMessage lighting;
        lighting.type = ControlMessageType::midiShowControl;
        lighting.mscCommandFormat = msc::formatLighting;
        lighting.mscCommand = msc::commandGo;
        lighting.mscCueNumber = "59";
        cue.outputMessages.push_back (lighting);

        ControlMessage video;
        video.type = ControlMessageType::osc;
        video.oscAddress = "/composition/layers/1/clips/3/connect";
        video.delay = 0.25;
        cue.outputMessages.push_back (video);

        list.insert (cue);
    }

    // 6 - streaming cue for the walk-out.
    {
        Cue cue;
        cue.number = "5";
        cue.name = "Walk-out playlist";
        cue.type = CueType::streaming;
        cue.streaming.uri = "spotify:playlist:37i9dQZF1DXcBWIGoYBM5M";
        cue.streaming.displayName = "House walk-out";
        cue.fadeInTime = 4.0;
        list.insert (cue);
    }

    // A real show is not six cues long, and a screenshot of a nearly empty list makes the
    // app look like a toy. Fill out Act One so the list reads the way an operator's does.
    const struct
    {
        const char* number;
        const char* name;
        int         file;
        double      fadeIn;
        double      fadeOut;
        double      preWait;
        LinkMode    link;
    } act[] =
    {
        { "6",    "Act 1 preset",        0, 3.0, 0.0, 0.0, LinkMode::none },
        { "7",    "Rain exterior",       1, 2.0, 4.0, 0.0, LinkMode::autoContinue },
        { "7.5",  "Distant bell",        2, 0.0, 0.0, 2.5, LinkMode::none },
        { "8",    "Kitchen radio",       3, 1.5, 2.0, 0.0, LinkMode::none },
        { "9",    "Radio off",           2, 0.0, 0.0, 0.0, LinkMode::none },
        { "10",   "Night ambience",      0, 8.0, 0.0, 0.0, LinkMode::none },
        { "11",   "Clock strikes three", 2, 0.0, 0.0, 0.0, LinkMode::autoFollow },
        { "12",   "Dream sequence",      1, 5.0, 5.0, 0.0, LinkMode::crossfade },
        { "13",   "Morning room tone",   3, 4.0, 3.0, 0.0, LinkMode::none },
        { "14",   "Interval bell",       2, 0.0, 0.0, 0.0, LinkMode::none }
    };

    for (const auto& entry : act)
    {
        Cue cue;
        cue.number = entry.number;
        cue.name = entry.name;
        scan (cue, fileAt (entry.file));
        cue.fadeInTime = entry.fadeIn;
        cue.fadeOutTime = entry.fadeOut;
        cue.preWait = entry.preWait;
        cue.link.mode = entry.link;
        cue.link.duration = 4.0;
        cue.gainDb = -4.0;
        list.insert (cue);
    }

    list.setStandbyIndex (2);
    list.setSelectedIndex (2);
}

//==============================================================================
ControlSettings demoControlSettings()
{
    ControlSettings settings;

    settings.oscInputEnabled = true;
    settings.oscInputPort = 53000;
    settings.oscFeedbackEnabled = true;
    settings.oscTargets.push_back ({ "Companion", "127.0.0.1", 12321, true });
    settings.oscTargets.push_back ({ "Resolume",  "10.0.0.24",  7000, true });

    settings.midiShowControlEnabled = true;
    settings.mscDeviceID = 2;

    MidiBinding go;
    go.kind = MidiTriggerKind::noteOn;
    go.channel = 1;
    go.number = 60;
    go.action = ControlActionType::go;
    settings.midiBindings.push_back (go);

    MidiBinding panic;
    panic.kind = MidiTriggerKind::noteOn;
    panic.channel = 1;
    panic.number = 61;
    panic.action = ControlActionType::panic;
    settings.midiBindings.push_back (panic);

    MidiBinding fader;
    fader.kind = MidiTriggerKind::controlChange;
    fader.channel = 1;
    fader.number = 7;
    fader.useValueAsLevel = true;
    fader.action = ControlActionType::masterLevel;
    settings.midiBindings.push_back (fader);

    // Deliberately left off. Art-Net is broadcast, so any console or stray application on
    // the network can put a frame on universe 1 and fire a cue mid-capture - which is the
    // control layer working correctly, but it makes the screenshot non-reproducible.
    settings.dmx.artNetEnabled = false;
    settings.dmx.universe = 1;
    settings.dmx.startAddress = 101;
    settings.dmx.numDirectCueChannels = 16;

    return settings;
}

//==============================================================================
bool capture (juce::Component& component, const juce::File& destination, float scale)
{
    if (component.getWidth() <= 0 || component.getHeight() <= 0)
        return false;

    destination.getParentDirectory().createDirectory();

    // Renders through JUCE's own software rasteriser into an offscreen image, so this is
    // exactly what the app draws and needs no screen-recording permission.
    const auto image = component.createComponentSnapshot (component.getLocalBounds(), true, scale);

    if (! image.isValid())
        return false;

    destination.deleteFile();

    if (auto* stream = destination.createOutputStream().release())
    {
        std::unique_ptr<juce::FileOutputStream> owned (stream);
        juce::PNGImageFormat png;
        return png.writeImageToStream (image, *owned);
    }

    return false;
}

} // namespace cp::screenshots
