#include "GUI/WaveformComponent.h"

namespace cp
{

namespace
{
    constexpr float markerGrabPixels = 7.0f;
    constexpr int   rulerHeight = 16;
}

WaveformComponent::WaveformComponent (juce::AudioFormatManager& formatManager)
    : formats (formatManager),
      thumbnail (512, formatManager, thumbnailCache)
{
    thumbnail.addChangeListener (this);
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

WaveformComponent::~WaveformComponent()
{
    thumbnail.removeChangeListener (this);
}

void WaveformComponent::setCue (const Cue* cue)
{
    if (cue == nullptr || cue->type != CueType::audioFile)
    {
        hasCue = false;
        currentFile = juce::File();
        thumbnail.clear();
        repaint();
        return;
    }

    hasCue = true;

    if (cue->audioFile != currentFile)
    {
        currentFile = cue->audioFile;
        thumbnail.clear();

        if (currentFile.existsAsFile())
            thumbnail.setSource (new juce::FileInputSource (currentFile));
    }

    fileLength    = cue->fileDuration > 0.0 ? cue->fileDuration : thumbnail.getTotalLength();
    inTime        = cue->startTime;
    outTime       = cue->resolvedEndTime() > 0.0 ? cue->resolvedEndTime() : fileLength;
    vampVisible   = cue->vampEnabled;
    vampStartTime = cue->vampStart;
    vampEndTime   = cue->vampEnd;

    repaint();
}

void WaveformComponent::setPlayheadTime (double seconds)
{
    if (std::abs (seconds - playhead) < 0.005)
        return;

    playhead = seconds;
    repaint();
}

void WaveformComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (fileLength <= 0.0)
        fileLength = thumbnail.getTotalLength();

    repaint();
}

//==============================================================================
float WaveformComponent::timeToX (double seconds) const
{
    if (fileLength <= 0.0)
        return 0.0f;

    return (float) (seconds / fileLength) * (float) getWidth();
}

double WaveformComponent::xToTime (float x) const
{
    if (getWidth() <= 0)
        return 0.0;

    return juce::jlimit (0.0, fileLength, (double) (x / (float) getWidth()) * fileLength);
}

WaveformComponent::Marker WaveformComponent::markerAt (juce::Point<float> position) const
{
    if (! hasCue || fileLength <= 0.0)
        return Marker::none;

    struct Candidate { Marker marker; double time; bool active; };

    const Candidate candidates[] =
    {
        { Marker::vampStart, vampStartTime, vampVisible },
        { Marker::vampEnd,   vampEndTime,   vampVisible },
        { Marker::in,        inTime,        true },
        { Marker::out,       outTime,       true }
    };

    Marker best = Marker::none;
    float bestDistance = markerGrabPixels;

    for (const auto& c : candidates)
    {
        if (! c.active)
            continue;

        const auto distance = std::abs (position.x - timeToX (c.time));

        if (distance <= bestDistance)
        {
            bestDistance = distance;
            best = c.marker;
        }
    }

    return best;
}

//==============================================================================
void WaveformComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    g.setColour (colours::panel);
    g.fillRect (bounds);

    if (! hasCue)
    {
        g.setColour (colours::textDim);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText ("Select an audio cue to edit its in and out points",
                    bounds, juce::Justification::centred);
        return;
    }

    auto waveArea = bounds.withTrimmedTop (rulerHeight);

    if (! currentFile.existsAsFile())
    {
        g.setColour (colours::stop);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText ("Missing file: " + currentFile.getFileName(),
                    waveArea, juce::Justification::centred);
        return;
    }

    // Everything outside the in/out region is dimmed rather than hidden, so the operator
    // can still see what they trimmed away and drag it back.
    g.setColour (colours::panelLight.darker (0.5f));
    g.fillRect (waveArea);

    const auto inX  = timeToX (inTime);
    const auto outX = timeToX (outTime);

    g.setColour (colours::panelLight);
    g.fillRect (juce::Rectangle<float> (inX, (float) waveArea.getY(),
                                        juce::jmax (1.0f, outX - inX), (float) waveArea.getHeight()));

    if (vampVisible && vampEndTime > vampStartTime)
    {
        const auto vs = timeToX (vampStartTime);
        const auto ve = timeToX (vampEndTime);
        g.setColour (colours::vamp.withAlpha (0.14f));
        g.fillRect (juce::Rectangle<float> (vs, (float) waveArea.getY(),
                                            juce::jmax (1.0f, ve - vs), (float) waveArea.getHeight()));
    }

    if (thumbnail.getTotalLength() > 0.0)
    {
        g.setColour (colours::textDim.withAlpha (0.45f));
        thumbnail.drawChannels (g, waveArea.reduced (0, 4), 0.0, thumbnail.getTotalLength(), 1.0f);

        // Redraw only the live region at full brightness.
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (juce::Rectangle<int> ((int) inX, waveArea.getY(),
                                                  juce::jmax (1, (int) (outX - inX)), waveArea.getHeight()));
        g.setColour (colours::go.withAlpha (0.85f));
        thumbnail.drawChannels (g, waveArea.reduced (0, 4), 0.0, thumbnail.getTotalLength(), 1.0f);
    }
    else
    {
        g.setColour (colours::textDim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText ("Reading waveform...", waveArea, juce::Justification::centred);
    }

    // Ruler
    g.setColour (colours::background);
    g.fillRect (bounds.removeFromTop (rulerHeight));
    g.setColour (colours::textDim);
    g.setFont (juce::FontOptions (10.0f));

    if (fileLength > 0.0)
    {
        const auto step = fileLength > 600.0 ? 60.0 : (fileLength > 60.0 ? 10.0 : 1.0);

        for (double t = 0.0; t <= fileLength; t += step)
        {
            const auto x = timeToX (t);
            g.drawVerticalLine ((int) x, (float) rulerHeight - 4.0f, (float) rulerHeight);
            g.drawText (formatTime (t), (int) x + 2, 0, 60, rulerHeight - 3,
                        juce::Justification::centredLeft);
        }
    }

    if (playhead >= 0.0 && playhead <= fileLength)
    {
        g.setColour (juce::Colours::white);
        g.drawVerticalLine ((int) timeToX (playhead), (float) rulerHeight, (float) getHeight());
    }

    drawMarker (g, Marker::in,  inTime,  colours::go,   "IN");
    drawMarker (g, Marker::out, outTime, colours::stop, "OUT");

    if (vampVisible)
    {
        drawMarker (g, Marker::vampStart, vampStartTime, colours::vamp, "V<");
        drawMarker (g, Marker::vampEnd,   vampEndTime,   colours::vamp, ">V");
    }
}

void WaveformComponent::drawMarker (juce::Graphics& g, Marker marker, double time,
                                    juce::Colour colour, const juce::String& label)
{
    const auto x = timeToX (time);
    const auto emphasised = (marker == hovered || marker == dragging);

    g.setColour (emphasised ? colour.brighter (0.4f) : colour);
    g.fillRect (juce::Rectangle<float> (x - (emphasised ? 1.5f : 0.5f), (float) rulerHeight,
                                        emphasised ? 3.0f : 1.5f, (float) getHeight() - rulerHeight));

    const auto tagWidth = 26.0f;
    const auto flip = x > (float) getWidth() - tagWidth;
    juce::Rectangle<float> tag (flip ? x - tagWidth : x, (float) rulerHeight, tagWidth, 13.0f);

    g.setColour (colour);
    g.fillRect (tag);
    g.setColour (colours::background);
    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    g.drawText (label, tag, juce::Justification::centred);
}

void WaveformComponent::resized() { repaint(); }

//==============================================================================
void WaveformComponent::mouseMove (const juce::MouseEvent& e)
{
    const auto marker = markerAt (e.position);

    if (marker != hovered)
    {
        hovered = marker;
        setMouseCursor (marker == Marker::none ? juce::MouseCursor::NormalCursor
                                               : juce::MouseCursor::LeftRightResizeCursor);
        repaint();
    }
}

void WaveformComponent::mouseDown (const juce::MouseEvent& e)
{
    dragging = markerAt (e.position);
    repaint();
}

void WaveformComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging == Marker::none || onMarkerMoved == nullptr)
        return;

    onMarkerMoved (dragging, xToTime (e.position.x), false);
}

void WaveformComponent::mouseUp (const juce::MouseEvent& e)
{
    if (dragging != Marker::none && onMarkerMoved != nullptr)
        onMarkerMoved (dragging, xToTime (e.position.x), true);

    dragging = Marker::none;
    repaint();
}

void WaveformComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (markerAt (e.position) == Marker::none && onScrubRequested != nullptr)
        onScrubRequested (xToTime (e.position.x));
}

} // namespace cp
