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

bool CueListComponent::isExpanded (const Cue& cue) const
{
    if (manuallyExpanded.count (cue.id) > 0)
        return true;

    // The standby cue always opens, so the operator can see what the next few GOs will do
    // before committing to the first of them - not only once they are already part-way
    // through and wondering what happens next.
    if (const auto* standby = cueList.getStandbyCue(); standby != nullptr && standby->id == cue.id)
        return true;

    return false;
}

void CueListComponent::toggleExpansion (const Cue& cue)
{
    if (manuallyExpanded.count (cue.id) > 0)
        manuallyExpanded.erase (cue.id);
    else
        manuallyExpanded.insert (cue.id);

    rebuildRows();
    table.updateContent();
    table.repaint();
}

void CueListComponent::rebuildRows()
{
    displayRows.clear();

    for (int i = 0; i < cueList.size(); ++i)
    {
        const auto* cue = cueList.get (i);

        if (cue == nullptr)
            continue;

        displayRows.push_back ({ i, -1 });

        // A cue with a single step has no lifecycle worth showing: Play is the whole story.
        const auto steps = cueList.stepsFor (i);

        if (steps.size() > 1 && isExpanded (*cue))
            for (int s = 0; s < (int) steps.size(); ++s)
                displayRows.push_back ({ i, s });
    }
}

void CueListComponent::refresh()
{
    activeSnapshot = audioEngine.getActiveCues();
    rebuildRows();

    table.updateContent();
    table.repaint();
}

void CueListComponent::selectRow (int index)
{
    for (int row = 0; row < (int) displayRows.size(); ++row)
    {
        if (displayRows[(size_t) row].cueIndex == index && displayRows[(size_t) row].isHeader())
        {
            table.selectRow (row, false, true);
            table.scrollToEnsureRowIsOnscreen (row);
            return;
        }
    }
}

juce::Rectangle<int> CueListComponent::getTwistyBounds (int width, int height) const
{
    juce::ignoreUnused (width);
    return juce::Rectangle<int> (4, (height - 12) / 2, 12, 12);
}

//==============================================================================
int CueListComponent::getNumRows()
{
    return (int) displayRows.size();
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
    if (! juce::isPositiveAndBelow (row, (int) displayRows.size()))
        return;

    const auto& display = displayRows[(size_t) row];
    const auto numSteps = (int) cueList.stepsFor (display.cueIndex).size();
    const auto expanded = numSteps > 1 && ! display.isHeader();

    // Step rows sit in a slightly recessed band so a cue and its lifecycle read as one
    // block rather than as a run of unrelated lines.
    if (display.isHeader())
        g.fillAll (display.cueIndex % 2 == 0 ? colours::panel : colours::panel.brighter (0.02f));
    else
        g.fillAll (colours::background.brighter (0.02f));

    if (rowIsSelected && display.isHeader())
        g.fillAll (colours::panelLight);

    // The standby marker goes on the exact row GO will act on: the cue header when the
    // lifecycle is collapsed or at its start, otherwise the step itself.
    const auto standbyCue = cueList.getStandbyIndex();
    const auto standbyStep = cueList.getStandbyStep();
    const auto stepsVisible = numSteps > 1
                           && std::any_of (displayRows.begin(), displayRows.end(),
                                           [&display] (const DisplayRow& r)
                                           { return r.cueIndex == display.cueIndex && ! r.isHeader(); });

    const auto isStandbyRow = display.cueIndex == standbyCue
                           && (stepsVisible ? (display.stepIndex == standbyStep)
                                            : display.isHeader());

    if (isStandbyRow)
    {
        g.setColour (colours::standby);
        g.fillRect (0, 0, 4, height);
    }

    juce::ignoreUnused (expanded);

    g.setColour (colours::outline.withAlpha (display.isHeader() ? 0.5f : 0.2f));
    g.drawHorizontalLine (height - 1, 0.0f, (float) width);
}

void CueListComponent::paintCell (juce::Graphics& g, int row, int columnId, int width, int height, bool)
{
    if (! juce::isPositiveAndBelow (row, (int) displayRows.size()))
        return;

    const auto& display = displayRows[(size_t) row];
    const auto* cue = cueList.get (display.cueIndex);

    if (cue == nullptr)
        return;

    // --- a lifecycle step -----------------------------------------------------
    if (! display.isHeader())
    {
        const auto steps = cueList.stepsFor (display.cueIndex);

        if (! juce::isPositiveAndBelow (display.stepIndex, (int) steps.size()))
            return;

        const auto& step = steps[(size_t) display.stepIndex];
        const auto stepArea = juce::Rectangle<int> (0, 0, width, height).reduced (6, 0);

        const auto stepColour = step.type == CueStepType::play   ? colours::go
                              : step.type == CueStepType::devamp ? colours::vamp
                                                                 : colours::stop;

        if (columnId == columnNumber)
        {
            // Indented and drawn as a branch, so the step is visibly part of the cue above.
            g.setColour (colours::outline);
            g.drawLine (24.0f, 0.0f, 24.0f, (float) height * 0.5f, 1.0f);
            g.drawLine (24.0f, (float) height * 0.5f, 34.0f, (float) height * 0.5f, 1.0f);

            auto dot = juce::Rectangle<int> (38, (height - 8) / 2, 8, 8);
            g.setColour (stepColour);
            g.fillEllipse (dot.toFloat());
        }
        else if (columnId == columnName)
        {
            g.setColour (stepColour);
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText (step.label, stepArea.withTrimmedLeft (10),
                        juce::Justification::centredLeft, true);
        }
        else if (columnId == columnFile)
        {
            g.setColour (colours::textDim);
            g.setFont (juce::FontOptions (11.5f));
            g.drawText (step.detail, stepArea, juce::Justification::centredLeft, true);
        }

        return;
    }

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
        {
            auto numberArea = area;

            if (cueList.stepsFor (display.cueIndex).size() > 1)
            {
                const auto twisty = getTwistyBounds (width, height);
                juce::Path triangle;

                if (isExpanded (*cue))
                {
                    triangle.addTriangle ((float) twisty.getX(), (float) twisty.getY() + 2.0f,
                                          (float) twisty.getRight(), (float) twisty.getY() + 2.0f,
                                          (float) twisty.getCentreX(), (float) twisty.getBottom() - 1.0f);
                }
                else
                {
                    triangle.addTriangle ((float) twisty.getX() + 2.0f, (float) twisty.getY(),
                                          (float) twisty.getX() + 2.0f, (float) twisty.getBottom(),
                                          (float) twisty.getRight() - 1.0f, (float) twisty.getCentreY());
                }

                g.setColour (colours::textDim);
                g.fillPath (triangle);
                numberArea = numberArea.withTrimmedLeft (14);
            }

            g.setColour (colours::text);
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText (cue->number, numberArea, juce::Justification::centredLeft, true);
            break;
        }

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
void CueListComponent::cellClicked (int row, int columnId, const juce::MouseEvent& e)
{
    if (! juce::isPositiveAndBelow (row, (int) displayRows.size()))
        return;

    const auto& display = displayRows[(size_t) row];
    const auto* cue = cueList.get (display.cueIndex);

    if (cue == nullptr)
        return;

    // Clicking a step stands that step by, so an operator can jump straight to "release
    // this vamp" without walking there.
    if (! display.isHeader())
    {
        cueList.setStandbyPosition (display.cueIndex, display.stepIndex);
        refresh();
        return;
    }

    if (columnId == columnNumber && cueList.stepsFor (display.cueIndex).size() > 1
        && getTwistyBounds (table.getHeader().getColumnWidth (columnNumber),
                            table.getRowHeight()).expanded (3, 3).contains (e.x, e.y))
    {
        toggleExpansion (*cue);
        return;
    }

    cueList.setSelectedIndex (display.cueIndex);

    if (onSelectionChanged != nullptr)
        onSelectionChanged (display.cueIndex);
}

void CueListComponent::cellDoubleClicked (int row, int columnId, const juce::MouseEvent&)
{
    if (! juce::isPositiveAndBelow (row, (int) displayRows.size()))
        return;

    const auto& display = displayRows[(size_t) row];
    const auto* cue = cueList.get (display.cueIndex);

    if (cue == nullptr || ! display.isHeader())
        return;

    if (columnId == columnFile && cue->type == CueType::audioFile)
    {
        if (onFileRequested != nullptr)
            onFileRequested (display.cueIndex);

        return;
    }

    if (onCueTriggered != nullptr)
        onCueTriggered (display.cueIndex);
}

void CueListComponent::selectedRowsChanged (int lastRowSelected)
{
    if (! juce::isPositiveAndBelow (lastRowSelected, (int) displayRows.size()))
        return;

    const auto& display = displayRows[(size_t) lastRowSelected];

    if (display.cueIndex == cueList.getSelectedIndex())
        return;

    cueList.setSelectedIndex (display.cueIndex);

    if (onSelectionChanged != nullptr)
        onSelectionChanged (display.cueIndex);
}

void CueListComponent::backgroundClicked (const juce::MouseEvent&) {}

} // namespace cp
