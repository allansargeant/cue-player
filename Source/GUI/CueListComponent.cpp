#include "GUI/CueListComponent.h"

namespace cp
{

CueListComponent::CueListComponent (CueList& list, AudioEngine& engine)
    : cueList (list), audioEngine (engine)
{
    table.setModel (this);
    table.setColour (juce::ListBox::backgroundColourId, colours::panel);
    table.setRowHeight (26);
    table.setHeaderHeight (24);
    table.getViewport()->setScrollBarsShown (true, false);

    auto& header = table.getHeader();
    header.addColumn ("",        columnStatus,  74,  60,  120);
    header.addColumn ("Cue",     columnNumber,  62,  40,  120);
    header.addColumn ("Name",    columnName,    260, 120, 900);
    header.addColumn ("Source",  columnFile,    200, 100, 700);
    header.addColumn ("Pre",     columnPreWait, 56,  44,  90);
    header.addColumn ("Length",  columnLength,  84,  60,  140);
    header.addColumn ("Fades",   columnFades,   92,  60,  160);
    header.addColumn ("Repeat",  columnLoop,    108, 70,  200);
    header.addColumn ("Link",    columnLink,    150, 80,  300);
    header.setStretchToFitActive (true);

    addAndMakeVisible (table);

    cueList.addChangeListener (this);
    audioEngine.addChangeListener (this);
}

CueListComponent::~CueListComponent()
{
    cueList.removeChangeListener (this);
    audioEngine.removeChangeListener (this);
    table.setModel (nullptr);
}

void CueListComponent::resized()
{
    table.setBounds (getLocalBounds());
}

void CueListComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void CueListComponent::refresh()
{
    activeSnapshot = audioEngine.getActiveCues();

    const auto selected = cueList.getSelectedIndex();

    if (table.getSelectedRow() != selected)
        table.selectRow (selected, true, true);

    table.updateContent();
    table.repaint();
}

void CueListComponent::selectRow (int index)
{
    table.selectRow (index, false, true);
    table.scrollToEnsureRowIsOnscreen (index);
}

//==============================================================================
int CueListComponent::getNumRows()
{
    return cueList.size();
}

CueListComponent::Status CueListComponent::statusFor (const Cue& cue) const
{
    for (const auto& active : activeSnapshot)
    {
        if (active.cueId != cue.id)
            continue;

        if (active.paused)     return { "PAUSED",  colours::textDim };
        if (active.inPreWait)  return { "WAIT",    colours::preWait };
        if (active.vamping)    return { "VAMP " + juce::String (active.vampPasses + 1), colours::vamp };
        if (active.stopping)   return { "FADING",  colours::stop };

        return { "PLAYING", colours::go };
    }

    if (cue.type == CueType::audioFile && ! cue.audioFile.existsAsFile())
        return { "MISSING", colours::stop };

    return {};
}

void CueListComponent::paintRowBackground (juce::Graphics& g, int row, int width, int height, bool rowIsSelected)
{
    const auto isStandby = (row == cueList.getStandbyIndex());

    g.fillAll (row % 2 == 0 ? colours::panel : colours::panel.brighter (0.02f));

    if (rowIsSelected)
        g.fillAll (colours::panelLight);

    if (isStandby)
    {
        // A left bar rather than a full highlight: it survives being layered under the
        // selection and under a playing row without any of them becoming unreadable.
        g.setColour (colours::standby);
        g.fillRect (0, 0, 4, height);
    }

    g.setColour (colours::outline.withAlpha (0.5f));
    g.drawHorizontalLine (height - 1, 0.0f, (float) width);
}

void CueListComponent::paintCell (juce::Graphics& g, int row, int columnId, int width, int height, bool)
{
    const auto* cue = cueList.get (row);

    if (cue == nullptr)
        return;

    const auto area = juce::Rectangle<int> (0, 0, width, height).reduced (6, 0);
    g.setFont (juce::FontOptions (12.5f));
    g.setColour (colours::text);

    switch (columnId)
    {
        case columnStatus:
        {
            const auto status = statusFor (*cue);

            if (status.label.isEmpty())
                return;

            auto tag = area.withSizeKeepingCentre (juce::jmin (area.getWidth(), 66), 16);
            g.setColour (status.colour);
            g.fillRoundedRectangle (tag.toFloat(), 3.0f);
            g.setColour (colours::background);
            g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
            g.drawText (status.label, tag, juce::Justification::centred);
            break;
        }

        case columnNumber:
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText (cue->number, area, juce::Justification::centredLeft, true);
            break;

        case columnName:
            g.drawText (cue->name.isNotEmpty() ? cue->name : juce::String ("(untitled)"),
                        area, juce::Justification::centredLeft, true);
            break;

        case columnFile:
        {
            if (cue->type == CueType::streaming)
            {
                g.setColour (colours::loop);
                const auto label = cue->streaming.displayName.isNotEmpty()
                                       ? cue->streaming.displayName : cue->streaming.uri;
                g.drawText (audioEngine.getStreamingSettings().getProviderDisplayName()
                                + "   " + label,
                            area, juce::Justification::centredLeft, true);
                break;
            }

            if (cue->type == CueType::control)
            {
                g.setColour (colours::standby);
                g.drawText (juce::String (cue->outputMessages.size())
                                + (cue->outputMessages.size() == 1 ? " message" : " messages"),
                            area, juce::Justification::centredLeft, true);
                break;
            }

            if (cue->audioFile == juce::File())
            {
                g.setColour (colours::textDim);
                g.drawText ("double-click to choose a file", area, juce::Justification::centredLeft, true);
                break;
            }

            g.setColour (cue->audioFile.existsAsFile() ? colours::textDim : colours::stop);
            g.drawText (cue->audioFile.getFileName(), area, juce::Justification::centredLeft, true);
            break;
        }

        case columnPreWait:
            if (cue->preWait > 0.0)
                g.drawText (juce::String (cue->preWait, 1) + "s", area, juce::Justification::centredLeft, true);
            break;

        case columnLength:
        {
            const auto length = cue->playbackLength();
            g.setColour (colours::textDim);
            g.drawText (length > 0.0 ? formatTime (length) : juce::String ("open"),
                        area, juce::Justification::centredLeft, true);
            break;
        }

        case columnFades:
        {
            juce::StringArray parts;

            if (cue->fadeInTime  > 0.0) parts.add (juce::String (cue->fadeInTime, 1)  + "s in");
            if (cue->fadeOutTime > 0.0) parts.add (juce::String (cue->fadeOutTime, 1) + "s out");

            g.setColour (colours::textDim);
            g.drawText (parts.joinIntoString (" / "), area, juce::Justification::centredLeft, true);
            break;
        }

        case columnLoop:
            g.setColour (cue->vampEnabled ? colours::vamp
                                          : (cue->loopEnabled ? colours::loop : colours::textDim));
            g.drawText (describeLoop (*cue), area, juce::Justification::centredLeft, true);
            break;

        case columnLink:
            g.setColour (cue->link.mode == LinkMode::none ? colours::textDim : colours::standby);
            g.drawText (describeLink (*cue), area, juce::Justification::centredLeft, true);
            break;

        default:
            break;
    }
}

juce::String CueListComponent::describeLoop (const Cue& cue) const
{
    juce::StringArray parts;

    if (cue.loopEnabled)
        parts.add (cue.loopCount <= 0 ? juce::String ("loop") : "x" + juce::String (cue.loopCount));

    if (cue.vampEnabled)
        parts.add ("vamp");

    return parts.joinIntoString (" + ");
}

juce::String CueListComponent::describeLink (const Cue& cue) const
{
    if (cue.link.mode == LinkMode::none)
        return {};

    juce::String target = "next";

    if (! cue.link.targetsNextCue())
        if (const auto* t = cueList.findByID (cue.link.target))
            target = t->number.isNotEmpty() ? t->number : t->name;

    switch (cue.link.mode)
    {
        case LinkMode::autoContinue:
            return "continue -> " + target + (cue.link.delay > 0.0
                                                  ? " +" + juce::String (cue.link.delay, 1) + "s" : "");
        case LinkMode::autoFollow:
            return "follow -> " + target + (cue.link.delay > 0.0
                                                ? " +" + juce::String (cue.link.delay, 1) + "s" : "");
        case LinkMode::crossfade:
            return "xfade " + juce::String (cue.link.duration, 1) + "s -> " + target;
        case LinkMode::none:
        default:
            return {};
    }
}

//==============================================================================
void CueListComponent::cellClicked (int row, int, const juce::MouseEvent&)
{
    cueList.setSelectedIndex (row);

    if (onSelectionChanged != nullptr)
        onSelectionChanged (row);
}

void CueListComponent::cellDoubleClicked (int row, int columnId, const juce::MouseEvent&)
{
    const auto* cue = cueList.get (row);

    if (cue == nullptr)
        return;

    if (columnId == columnFile && cue->type == CueType::audioFile)
    {
        if (onFileRequested != nullptr)
            onFileRequested (row);

        return;
    }

    if (onCueTriggered != nullptr)
        onCueTriggered (row);
}

void CueListComponent::selectedRowsChanged (int lastRowSelected)
{
    if (lastRowSelected < 0 || lastRowSelected == cueList.getSelectedIndex())
        return;

    cueList.setSelectedIndex (lastRowSelected);

    if (onSelectionChanged != nullptr)
        onSelectionChanged (lastRowSelected);
}

void CueListComponent::backgroundClicked (const juce::MouseEvent&) {}

} // namespace cp
