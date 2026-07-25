#include "GUI/CueInspector.h"

namespace cp
{

namespace
{
    constexpr int rowHeight = 24;
    constexpr int rowGap = 4;
    constexpr int labelWidth = 108;
    constexpr int sectionGap = 12;
    constexpr int waveformHeight = 118;
}

CueInspector::CueInspector (CueList& list, AudioEngine& engine, juce::AudioFormatManager& formatManager)
    : cueList (list), audioEngine (engine), formats (formatManager),
      waveform (formatManager)
{
    addAndMakeVisible (waveform);
    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);

    buildControls();

    waveform.onMarkerMoved = [this] (WaveformComponent::Marker marker, double time, bool)
    {
        editCue ([marker, time] (Cue& cue)
        {
            switch (marker)
            {
                case WaveformComponent::Marker::in:
                    cue.startTime = juce::jlimit (0.0, cue.resolvedEndTime(), time);
                    break;

                case WaveformComponent::Marker::out:
                    cue.endTime = juce::jlimit (cue.startTime, cue.fileDuration, time);
                    break;

                case WaveformComponent::Marker::vampStart:
                    cue.vampStart = juce::jlimit (cue.startTime, cue.vampEnd, time);
                    break;

                case WaveformComponent::Marker::vampEnd:
                    cue.vampEnd = juce::jlimit (cue.vampStart, cue.resolvedEndTime(), time);
                    break;

                case WaveformComponent::Marker::none:
                default:
                    break;
            }
        });
    };

    waveform.onScrubRequested = [this] (double time)
    {
        if (const auto* cue = cueList.get (cueIndex))
            audioEngine.audition (*cue, time);
    };

    cueList.addChangeListener (this);
}

CueInspector::~CueInspector()
{
    cueList.removeChangeListener (this);
}

void CueInspector::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (cueIndex != cueList.getSelectedIndex())
        setCueIndex (cueList.getSelectedIndex());
    else
        refresh();
}

//==============================================================================
juce::Label& CueInspector::addSection (const juce::String& title)
{
    auto label = std::make_unique<juce::Label> (juce::String(), title.toUpperCase());
    label->setFont (juce::FontOptions (11.0f, juce::Font::bold));
    label->setColour (juce::Label::textColourId, colours::textDim);
    content.addAndMakeVisible (*label);

    auto& ref = *label;
    sectionLabels.push_back (std::move (label));
    sectionRowStart.push_back (rows.size());
    return ref;
}

void CueInspector::addRow (const juce::String& labelText, juce::Component& control, int span)
{
    auto row = std::make_unique<Row>();
    row->label.setText (labelText, juce::dontSendNotification);
    row->label.setFont (juce::FontOptions (12.0f));
    row->label.setJustificationType (juce::Justification::centredRight);
    row->label.setColour (juce::Label::textColourId, colours::textDim);
    row->control = &control;
    row->span = span;

    content.addAndMakeVisible (row->label);
    content.addAndMakeVisible (control);
    rows.push_back (std::move (row));
}

void CueInspector::buildControls()
{
    const auto configureTimeSlider = [] (juce::Slider& s, double max, const juce::String& suffix)
    {
        s.setSliderStyle (juce::Slider::LinearBar);
        s.setRange (0.0, max, 0.001);
        s.setNumDecimalPlacesToDisplay (3);
        s.setTextValueSuffix (suffix);
        s.setColour (juce::Slider::trackColourId, colours::panelLight);
    };

    // --- Source ---------------------------------------------------------------
    addSection ("Source");

    numberEditor.setMultiLine (false);
    numberEditor.onTextChange = [this] { editCue ([this] (Cue& c) { c.number = numberEditor.getText(); }); };
    addRow ("Cue number", numberEditor);

    nameEditor.setMultiLine (false);
    nameEditor.onTextChange = [this] { editCue ([this] (Cue& c) { c.name = nameEditor.getText(); }); };
    addRow ("Name", nameEditor);

    chooseFileButton.onClick = [this]
    {
        if (onFileRequested != nullptr && cueIndex >= 0)
            onFileRequested (cueIndex);
    };
    addRow ("Audio file", chooseFileButton);
    fileOnly.push_back (&chooseFileButton);

    notesEditor.setMultiLine (true, true);
    notesEditor.setReturnKeyStartsNewLine (true);
    notesEditor.onFocusLost = [this] { editCue ([this] (Cue& c) { c.notes = notesEditor.getText(); }); };
    addRow ("Notes", notesEditor, 2);

    gainSlider.setSliderStyle (juce::Slider::LinearBar);
    gainSlider.setRange (-60.0, 12.0, 0.1);
    gainSlider.setTextValueSuffix (" dB");
    gainSlider.onValueChange = [this] { editCue ([this] (Cue& c) { c.gainDb = gainSlider.getValue(); }); };
    addRow ("Gain", gainSlider);

    configureTimeSlider (preWaitSlider, 600.0, " s");
    preWaitSlider.onValueChange = [this] { editCue ([this] (Cue& c) { c.preWait = preWaitSlider.getValue(); }); };
    addRow ("Pre-wait", preWaitSlider);

    auditionButton.onClick = [this]
    {
        if (const auto* cue = cueList.get (cueIndex))
            audioEngine.audition (*cue, cue->startTime);
    };
    addRow ("", auditionButton);

    // --- Streaming ------------------------------------------------------------
    addSection ("Streaming service");

    streamProviderBox.addItemList ({ "Spotify", "TIDAL", "Apple Music", "YouTube Music" }, 1);
    streamProviderBox.onChange = [this]
    {
        const juce::StringArray keys { "spotify", "tidal", "appleMusic", "youtubeMusic" };
        const auto index = streamProviderBox.getSelectedId() - 1;

        if (juce::isPositiveAndBelow (index, keys.size()))
            editCue ([&keys, index] (Cue& c) { c.streaming.provider = keys[index]; });
    };
    addRow ("Service", streamProviderBox);
    streamingOnly.push_back (&streamProviderBox);

    streamUriEditor.setMultiLine (false);
    streamUriEditor.setTextToShowWhenEmpty ("spotify:playlist:... or a pasted share link",
                                            colours::textDim);
    streamUriEditor.onFocusLost = [this]
    {
        editCue ([this] (Cue& c) { c.streaming.uri = streamUriEditor.getText().trim(); });
    };
    addRow ("Playlist / track", streamUriEditor, 2);
    streamingOnly.push_back (&streamUriEditor);

    streamNameEditor.setMultiLine (false);
    streamNameEditor.onTextChange = [this]
    {
        editCue ([this] (Cue& c) { c.streaming.displayName = streamNameEditor.getText(); });
    };
    addRow ("Shown as", streamNameEditor);
    streamingOnly.push_back (&streamNameEditor);

    streamPathBox.addItem ("Capture from a local loopback input", 1);
    streamPathBox.addItem ("Play on a remote Connect device", 2);
    streamPathBox.onChange = [this]
    {
        const auto path = streamPathBox.getSelectedId() == 2 ? StreamingAudioPath::remoteDevice
                                                             : StreamingAudioPath::localCapture;
        editCue ([path] (Cue& c) { c.streaming.audioPath = path; });
        updateEnablement();
    };
    addRow ("Audio path", streamPathBox, 2);
    streamingOnly.push_back (&streamPathBox);

    streamInputSlider.setSliderStyle (juce::Slider::LinearBar);
    streamInputSlider.setRange (1.0, (double) limits::maxOutputChannels, 1.0);
    streamInputSlider.onValueChange = [this]
    {
        editCue ([this] (Cue& c)
        {
            c.streaming.captureFirstInputChannel = (int) streamInputSlider.getValue() - 1;
        });
        updateRoutingMatrix();
    };
    addRow ("First input", streamInputSlider);
    streamingOnly.push_back (&streamInputSlider);

    streamChannelsSlider.setSliderStyle (juce::Slider::LinearBar);
    streamChannelsSlider.setRange (1.0, 8.0, 1.0);
    streamChannelsSlider.onValueChange = [this]
    {
        editCue ([this] (Cue& c)
        {
            c.streaming.captureNumChannels = (int) streamChannelsSlider.getValue();
        });
        updateRoutingMatrix();
    };
    addRow ("Channels", streamChannelsSlider);
    streamingOnly.push_back (&streamChannelsSlider);

    streamShuffleToggle.onClick = [this]
    {
        editCue ([this] (Cue& c) { c.streaming.shuffle = streamShuffleToggle.getToggleState(); });
    };
    addRow ("", streamShuffleToggle);
    streamingOnly.push_back (&streamShuffleToggle);

    streamRepeatToggle.onClick = [this]
    {
        editCue ([this] (Cue& c) { c.streaming.repeat = streamRepeatToggle.getToggleState(); });
    };
    addRow ("", streamRepeatToggle);
    streamingOnly.push_back (&streamRepeatToggle);

    // --- Trim -----------------------------------------------------------------
    addSection ("In and out points");

    configureTimeSlider (inPointSlider, 3600.0, " s");
    inPointSlider.onValueChange = [this]
    {
        editCue ([this] (Cue& c)
        {
            c.startTime = juce::jlimit (0.0, c.resolvedEndTime(), inPointSlider.getValue());
        });
    };
    addRow ("In point", inPointSlider);
    fileOnly.push_back (&inPointSlider);

    configureTimeSlider (outPointSlider, 3600.0, " s");
    outPointSlider.onValueChange = [this]
    {
        editCue ([this] (Cue& c)
        {
            c.endTime = juce::jlimit (c.startTime, c.fileDuration, outPointSlider.getValue());
        });
    };
    addRow ("Out point", outPointSlider);
    fileOnly.push_back (&outPointSlider);

    // --- Fades ----------------------------------------------------------------
    addSection ("Fades");

    configureTimeSlider (fadeInSlider, 120.0, " s");
    fadeInSlider.onValueChange = [this] { editCue ([this] (Cue& c) { c.fadeInTime = fadeInSlider.getValue(); }); };
    addRow ("Fade in", fadeInSlider);

    fadeInShapeBox.addItemList (fadeShapeNames(), 1);
    fadeInShapeBox.onChange = [this]
    {
        editCue ([this] (Cue& c) { c.fadeInShape = (FadeShape) (fadeInShapeBox.getSelectedId() - 1); });
    };
    addRow ("Shape", fadeInShapeBox);

    configureTimeSlider (fadeOutSlider, 120.0, " s");
    fadeOutSlider.onValueChange = [this] { editCue ([this] (Cue& c) { c.fadeOutTime = fadeOutSlider.getValue(); }); };
    addRow ("Fade out", fadeOutSlider);

    fadeOutShapeBox.addItemList (fadeShapeNames(), 1);
    fadeOutShapeBox.onChange = [this]
    {
        editCue ([this] (Cue& c) { c.fadeOutShape = (FadeShape) (fadeOutShapeBox.getSelectedId() - 1); });
    };
    addRow ("Shape", fadeOutShapeBox);

    // --- Repeat ---------------------------------------------------------------
    addSection ("Loop and vamp");

    loopToggle.onClick = [this]
    {
        editCue ([this] (Cue& c) { c.loopEnabled = loopToggle.getToggleState(); });
        updateEnablement();
    };
    addRow ("", loopToggle, 2);
    fileOnly.push_back (&loopToggle);

    loopCountSlider.setSliderStyle (juce::Slider::LinearBar);
    loopCountSlider.setRange (0.0, 999.0, 1.0);
    loopCountSlider.textFromValueFunction = [] (double v)
    {
        return v <= 0.0 ? juce::String ("forever") : juce::String ((int) v) + " times";
    };
    loopCountSlider.onValueChange = [this]
    {
        editCue ([this] (Cue& c) { c.loopCount = (int) loopCountSlider.getValue(); });
    };
    addRow ("Play", loopCountSlider);
    fileOnly.push_back (&loopCountSlider);

    vampToggle.onClick = [this]
    {
        editCue ([this] (Cue& c)
        {
            c.vampEnabled = vampToggle.getToggleState();

            // Give a freshly enabled vamp a sensible region rather than a zero-length one
            // sitting at the head of the file, which would look broken.
            if (c.vampEnabled && c.vampEnd <= c.vampStart)
            {
                const auto length = c.trimmedLength();
                c.vampStart = c.startTime + length * 0.25;
                c.vampEnd   = c.startTime + length * 0.75;
            }
        });
        updateEnablement();
    };
    addRow ("", vampToggle, 2);
    fileOnly.push_back (&vampToggle);

    configureTimeSlider (vampStartSlider, 3600.0, " s");
    vampStartSlider.onValueChange = [this]
    {
        editCue ([this] (Cue& c)
        {
            c.vampStart = juce::jlimit (c.startTime, c.resolvedEndTime(), vampStartSlider.getValue());
        });
    };
    addRow ("Vamp from", vampStartSlider);
    fileOnly.push_back (&vampStartSlider);

    configureTimeSlider (vampEndSlider, 3600.0, " s");
    vampEndSlider.onValueChange = [this]
    {
        editCue ([this] (Cue& c)
        {
            c.vampEnd = juce::jlimit (c.startTime, c.resolvedEndTime(), vampEndSlider.getValue());
        });
    };
    addRow ("Vamp to", vampEndSlider);
    fileOnly.push_back (&vampEndSlider);

    vampReleaseBox.addItem ("Finish the current pass", 1);
    vampReleaseBox.addItem ("Leave immediately", 2);
    vampReleaseBox.onChange = [this]
    {
        const auto release = vampReleaseBox.getSelectedId() == 2 ? VampRelease::immediately
                                                                 : VampRelease::atEndOfPass;
        editCue ([release] (Cue& c) { c.vampRelease = release; });
    };
    addRow ("On release", vampReleaseBox, 2);
    fileOnly.push_back (&vampReleaseBox);

    // --- Link -----------------------------------------------------------------
    addSection ("Link to the next cue");

    linkModeBox.addItemList (linkModeNames(), 1);
    linkModeBox.onChange = [this]
    {
        editCue ([this] (Cue& c) { c.link.mode = (LinkMode) (linkModeBox.getSelectedId() - 1); });
        updateEnablement();
    };
    addRow ("Mode", linkModeBox);

    linkTargetBox.onChange = [this]
    {
        const auto id = linkTargetBox.getSelectedId();

        if (id == 1)
        {
            editCue ([] (Cue& c) { c.link.target = juce::Uuid::null(); });
            return;
        }

        if (const auto* target = cueList.get (id - 2))
        {
            const auto targetId = target->id;
            editCue ([targetId] (Cue& c) { c.link.target = targetId; });
        }
    };
    addRow ("Target", linkTargetBox, 2);

    configureTimeSlider (linkDelaySlider, 600.0, " s");
    linkDelaySlider.onValueChange = [this]
    {
        editCue ([this] (Cue& c) { c.link.delay = linkDelaySlider.getValue(); });
    };
    addRow ("Delay", linkDelaySlider);

    configureTimeSlider (linkDurationSlider, 120.0, " s");
    linkDurationSlider.onValueChange = [this]
    {
        editCue ([this] (Cue& c) { c.link.duration = linkDurationSlider.getValue(); });
    };
    addRow ("Crossfade", linkDurationSlider);

    linkShapeBox.addItemList (fadeShapeNames(), 1);
    linkShapeBox.onChange = [this]
    {
        editCue ([this] (Cue& c) { c.link.shape = (FadeShape) (linkShapeBox.getSelectedId() - 1); });
    };
    addRow ("Curve", linkShapeBox);

    // --- Routing --------------------------------------------------------------
    addSection ("Output routing");

    routingMatrix.onRoutingChanged = [this] (const std::vector<RoutePoint>& routes)
    {
        editCue ([&routes] (Cue& c) { c.routing = routes; });
    };
    content.addAndMakeVisible (routingMatrix);
}

//==============================================================================
void CueInspector::editCue (const std::function<void (Cue&)>& fn)
{
    if (updating || cueIndex < 0)
        return;

    if (! cueList.modify (cueIndex, fn))
        return;

    if (onCueEdited != nullptr)
        onCueEdited();
}

void CueInspector::setCueIndex (int index)
{
    cueIndex = index;
    refresh();
}

void CueInspector::updateLinkTargets()
{
    const auto* cue = cueList.get (cueIndex);
    linkTargetBox.clear (juce::dontSendNotification);
    linkTargetBox.addItem ("The next cue in the list", 1);

    for (int i = 0; i < cueList.size(); ++i)
    {
        const auto* other = cueList.get (i);

        if (other == nullptr || (cue != nullptr && other->id == cue->id))
            continue;

        const auto label = (other->number.isNotEmpty() ? other->number + "  " : juce::String())
                         + (other->name.isNotEmpty() ? other->name : juce::String ("(untitled)"));
        linkTargetBox.addItem (label, i + 2);
    }

    if (cue == nullptr)
        return;

    if (cue->link.targetsNextCue())
    {
        linkTargetBox.setSelectedId (1, juce::dontSendNotification);
    }
    else
    {
        const auto targetIndex = cueList.indexOfID (cue->link.target);
        linkTargetBox.setSelectedId (targetIndex >= 0 ? targetIndex + 2 : 1, juce::dontSendNotification);
    }
}

void CueInspector::updateEnablement()
{
    const auto* cue = cueList.get (cueIndex);
    const auto isStreaming = cue != nullptr && cue->type == CueType::streaming;
    const auto isFile = cue != nullptr && cue->type == CueType::audioFile;

    for (auto* c : streamingOnly) c->setEnabled (isStreaming);
    for (auto* c : fileOnly)      c->setEnabled (isFile);

    const auto capturing = isStreaming
                        && cue->streaming.audioPath == StreamingAudioPath::localCapture;
    streamInputSlider.setEnabled (capturing);
    streamChannelsSlider.setEnabled (capturing);

    loopCountSlider.setEnabled (isFile && cue->loopEnabled);
    vampStartSlider.setEnabled (isFile && cue->vampEnabled);
    vampEndSlider.setEnabled (isFile && cue->vampEnabled);
    vampReleaseBox.setEnabled (isFile && cue->vampEnabled);

    const auto mode = cue != nullptr ? cue->link.mode : LinkMode::none;
    linkTargetBox.setEnabled (mode != LinkMode::none);
    linkDelaySlider.setEnabled (mode == LinkMode::autoContinue || mode == LinkMode::autoFollow);
    linkDurationSlider.setEnabled (mode == LinkMode::crossfade);
    linkShapeBox.setEnabled (mode == LinkMode::crossfade);

    auditionButton.setEnabled (cue != nullptr && cue->isPlayable());
}

void CueInspector::updateRoutingMatrix()
{
    const auto* cue = cueList.get (cueIndex);

    juce::StringArray sourceLabels;
    int numSourceChannels = 0;

    if (cue != nullptr)
    {
        numSourceChannels = cue->type == CueType::streaming
                                ? juce::jmax (1, cue->streaming.captureNumChannels)
                                : juce::jmax (0, cue->fileChannels);
    }

    for (int i = 0; i < numSourceChannels; ++i)
        sourceLabels.add (numSourceChannels == 2 ? (i == 0 ? "Left" : "Right")
                                                 : "Ch " + juce::String (i + 1));

    routingMatrix.setChannels (sourceLabels, audioEngine.getOutputChannelNames());

    if (cue != nullptr)
        routingMatrix.setRouting (cue->effectiveRouting (numSourceChannels,
                                                         audioEngine.getNumOutputChannels()),
                                  cue->routing.empty());
    else
        routingMatrix.setRouting ({}, true);
}

void CueInspector::pushSourceInfoToWaveform()
{
    waveform.setCue (cueList.get (cueIndex));
}

void CueInspector::refresh()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);

    const auto* cue = cueList.get (cueIndex);

    if (cue == nullptr)
    {
        pushSourceInfoToWaveform();
        updateEnablement();
        updateRoutingMatrix();
        repaint();
        return;
    }

    numberEditor.setText (cue->number, juce::dontSendNotification);
    nameEditor.setText (cue->name, juce::dontSendNotification);

    if (! notesEditor.hasKeyboardFocus (false))
        notesEditor.setText (cue->notes, juce::dontSendNotification);

    chooseFileButton.setButtonText (cue->audioFile == juce::File()
                                        ? "Choose file..." : cue->audioFile.getFileName());

    gainSlider.setValue (cue->gainDb, juce::dontSendNotification);
    preWaitSlider.setValue (cue->preWait, juce::dontSendNotification);

    const auto duration = juce::jmax (1.0, cue->fileDuration);
    inPointSlider.setRange (0.0, duration, 0.001);
    outPointSlider.setRange (0.0, duration, 0.001);
    vampStartSlider.setRange (0.0, duration, 0.001);
    vampEndSlider.setRange (0.0, duration, 0.001);

    inPointSlider.setValue (cue->startTime, juce::dontSendNotification);
    outPointSlider.setValue (cue->resolvedEndTime(), juce::dontSendNotification);

    fadeInSlider.setValue (cue->fadeInTime, juce::dontSendNotification);
    fadeInShapeBox.setSelectedId ((int) cue->fadeInShape + 1, juce::dontSendNotification);
    fadeOutSlider.setValue (cue->fadeOutTime, juce::dontSendNotification);
    fadeOutShapeBox.setSelectedId ((int) cue->fadeOutShape + 1, juce::dontSendNotification);

    loopToggle.setToggleState (cue->loopEnabled, juce::dontSendNotification);
    loopCountSlider.setValue (cue->loopCount, juce::dontSendNotification);
    vampToggle.setToggleState (cue->vampEnabled, juce::dontSendNotification);
    vampStartSlider.setValue (cue->vampStart, juce::dontSendNotification);
    vampEndSlider.setValue (cue->vampEnd, juce::dontSendNotification);
    vampReleaseBox.setSelectedId (cue->vampRelease == VampRelease::immediately ? 2 : 1,
                                  juce::dontSendNotification);

    linkModeBox.setSelectedId ((int) cue->link.mode + 1, juce::dontSendNotification);
    linkDelaySlider.setValue (cue->link.delay, juce::dontSendNotification);
    linkDurationSlider.setValue (cue->link.duration, juce::dontSendNotification);
    linkShapeBox.setSelectedId ((int) cue->link.shape + 1, juce::dontSendNotification);
    updateLinkTargets();

    const juce::StringArray providerKeys { "spotify", "tidal", "appleMusic", "youtubeMusic" };
    const auto providerIndex = providerKeys.indexOf (cue->streaming.provider);
    streamProviderBox.setSelectedId (providerIndex >= 0 ? providerIndex + 1 : 1,
                                     juce::dontSendNotification);

    if (! streamUriEditor.hasKeyboardFocus (false))
        streamUriEditor.setText (cue->streaming.uri, juce::dontSendNotification);

    streamNameEditor.setText (cue->streaming.displayName, juce::dontSendNotification);
    streamPathBox.setSelectedId (cue->streaming.audioPath == StreamingAudioPath::remoteDevice ? 2 : 1,
                                 juce::dontSendNotification);
    streamInputSlider.setValue (cue->streaming.captureFirstInputChannel + 1, juce::dontSendNotification);
    streamChannelsSlider.setValue (cue->streaming.captureNumChannels, juce::dontSendNotification);
    streamShuffleToggle.setToggleState (cue->streaming.shuffle, juce::dontSendNotification);
    streamRepeatToggle.setToggleState (cue->streaming.repeat, juce::dontSendNotification);

    pushSourceInfoToWaveform();
    updateEnablement();
    updateRoutingMatrix();
    resized();
    repaint();
}

//==============================================================================
void CueInspector::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
}

void CueInspector::resized()
{
    auto bounds = getLocalBounds();
    waveform.setBounds (bounds.removeFromTop (waveformHeight));
    viewport.setBounds (bounds);

    const auto width = juce::jmax (320, viewport.getMaximumVisibleWidth());
    const auto controlWidth = width - labelWidth - 24;

    int y = 8;

    for (size_t section = 0; section < sectionLabels.size(); ++section)
    {
        sectionLabels[section]->setBounds (8, y, width - 16, 16);
        y += 16 + rowGap;

        const auto first = sectionRowStart[section];
        const auto last  = section + 1 < sectionRowStart.size() ? sectionRowStart[section + 1]
                                                                : rows.size();

        for (auto i = first; i < last; ++i)
        {
            auto& row = *rows[i];
            const auto height = rowHeight * row.span + (row.span - 1) * rowGap;

            row.label.setBounds (8, y, labelWidth, rowHeight);
            row.control->setBounds (labelWidth + 16, y, controlWidth, height);
            y += height + rowGap;
        }

        y += sectionGap;
    }

    routingMatrix.setBounds (8, y, width - 16, routingMatrix.getPreferredHeight());
    y += routingMatrix.getHeight() + 16;

    content.setSize (width, y);
}

} // namespace cp
