#include "GUI/ActiveCuesComponent.h"

namespace cp
{

namespace
{
    constexpr int buttonWidth = 56;
    constexpr int buttonHeight = 18;
}

ActiveCuesComponent::ActiveCuesComponent (AudioEngine& engine)
    : audioEngine (engine)
{
    startTimerHz (20);
}

ActiveCuesComponent::~ActiveCuesComponent()
{
    stopTimer();
}

void ActiveCuesComponent::timerCallback()
{
    auto latest = audioEngine.getActiveCues();

    // Repaint on every tick while anything is running so the elapsed times count up
    // smoothly, but stay quiet once the stage is silent.
    const auto wasEmpty = entries.empty();
    entries = std::move (latest);

    if (! entries.empty() || ! wasEmpty)
        repaint();
}

juce::Rectangle<int> ActiveCuesComponent::boundsForEntry (int index) const
{
    return { 0, headerHeight + index * entryHeight, getWidth(), entryHeight };
}

ActiveCuesComponent::Hit ActiveCuesComponent::hitTest (juce::Point<float> p) const
{
    for (int i = 0; i < (int) entries.size(); ++i)
    {
        const auto row = boundsForEntry (i);

        if (! row.toFloat().contains (p))
            continue;

        auto controls = row.reduced (8, 0).removeFromBottom (buttonHeight + 6)
                                          .withTrimmedBottom (4);
        const auto stopBounds = controls.removeFromRight (buttonWidth);
        controls.removeFromRight (4);
        const auto vampBounds = controls.removeFromRight (buttonWidth + 12);

        if (stopBounds.toFloat().contains (p))
            return { i, Hit::Part::stop };

        if (entries[(size_t) i].vamping && vampBounds.toFloat().contains (p))
            return { i, Hit::Part::vamp };

        return { i, Hit::Part::none };
    }

    return {};
}

void ActiveCuesComponent::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);

    g.setColour (colours::panelLight);
    g.fillRect (0, 0, getWidth(), headerHeight);
    g.setColour (colours::textDim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("RUNNING  (" + juce::String (entries.size()) + ")",
                juce::Rectangle<int> (8, 0, getWidth() - 16, headerHeight),
                juce::Justification::centredLeft);

    if (entries.empty())
    {
        g.setColour (colours::textDim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("Nothing playing", getLocalBounds().withTrimmedTop (headerHeight),
                    juce::Justification::centred);
        return;
    }

    for (int i = 0; i < (int) entries.size(); ++i)
    {
        const auto& entry = entries[(size_t) i];
        auto row = boundsForEntry (i);

        g.setColour (i % 2 == 0 ? colours::panel : colours::panel.brighter (0.03f));
        g.fillRect (row);

        auto accent = colours::go;

        if (entry.paused)         accent = colours::textDim;
        else if (entry.inPreWait) accent = colours::preWait;
        else if (entry.vamping)   accent = colours::vamp;
        else if (entry.stopping)  accent = colours::stop;

        g.setColour (accent);
        g.fillRect (row.getX(), row.getY(), 3, row.getHeight());

        auto text = row.reduced (10, 4);
        auto topLine = text.removeFromTop (18);

        g.setColour (colours::text);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (entry.number, topLine.removeFromLeft (46), juce::Justification::centredLeft);

        g.setFont (juce::FontOptions (12.5f));
        g.drawText (entry.name, topLine.removeFromLeft (juce::jmax (40, topLine.getWidth() - 90)),
                    juce::Justification::centredLeft, true);

        g.setColour (accent);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (entry.inPreWait ? "WAITING"
                                     : (entry.vamping ? "VAMP " + juce::String (entry.vampPasses + 1)
                                                      : (entry.paused ? "PAUSED"
                                                                      : (entry.stopping ? "FADING" : ""))),
                    topLine, juce::Justification::centredRight);

        auto middle = text.removeFromTop (12);
        g.setColour (colours::textDim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (formatTime (entry.elapsed)
                        + (entry.remaining >= 0.0 ? "  /  -" + formatTime (entry.remaining)
                                                  : juce::String ("  /  open")),
                    middle, juce::Justification::centredLeft);

        // Progress bar, only where there is a known end to progress towards.
        if (entry.remaining >= 0.0 && (entry.elapsed + entry.remaining) > 0.0)
        {
            const auto proportion = (float) (entry.elapsed / (entry.elapsed + entry.remaining));
            auto track = middle.removeFromRight (juce::jmax (40, middle.getWidth() / 2)).withSizeKeepingCentre (
                             juce::jmax (40, middle.getWidth() / 2), 3);
            g.setColour (colours::panelLight);
            g.fillRect (track);
            g.setColour (accent);
            g.fillRect (track.withWidth (juce::roundToInt (proportion * track.getWidth())));
        }

        auto controls = text.removeFromBottom (buttonHeight);
        const auto drawButton = [&g, this, i] (juce::Rectangle<int> b, const juce::String& label,
                                               juce::Colour colour, Hit::Part part)
        {
            const auto isHovered = (hovered.entry == i && hovered.part == part);
            g.setColour (isHovered ? colour.brighter (0.3f) : colour);
            g.fillRoundedRectangle (b.toFloat(), 3.0f);
            g.setColour (colours::background);
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText (label, b, juce::Justification::centred);
        };

        drawButton (controls.removeFromRight (buttonWidth), "STOP", colours::stop, Hit::Part::stop);
        controls.removeFromRight (4);

        if (entry.vamping)
            drawButton (controls.removeFromRight (buttonWidth + 12), "RELEASE",
                        colours::vamp, Hit::Part::vamp);
    }
}

void ActiveCuesComponent::resized() {}

void ActiveCuesComponent::mouseDown (const juce::MouseEvent& e)
{
    const auto hit = hitTest (e.position);

    if (! juce::isPositiveAndBelow (hit.entry, (int) entries.size()))
        return;

    const auto& entry = entries[(size_t) hit.entry];

    if (hit.part == Hit::Part::stop)
        audioEngine.stopVoice (entry.voiceIndex, stopFade);
    else if (hit.part == Hit::Part::vamp)
        audioEngine.releaseVamp (entry.cueId);
}

void ActiveCuesComponent::mouseMove (const juce::MouseEvent& e)
{
    const auto hit = hitTest (e.position);

    if (hit.entry != hovered.entry || hit.part != hovered.part)
    {
        hovered = hit;
        repaint();
    }
}

void ActiveCuesComponent::mouseExit (const juce::MouseEvent&)
{
    hovered = {};
    repaint();
}

} // namespace cp
