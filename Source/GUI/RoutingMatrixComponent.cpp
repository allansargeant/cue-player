#include "GUI/RoutingMatrixComponent.h"

namespace cp
{

RoutingMatrixComponent::RoutingMatrixComponent()
{
    setWantsKeyboardFocus (false);
}

void RoutingMatrixComponent::setChannels (juce::StringArray sourceLabels, juce::StringArray outputLabels)
{
    sources = std::move (sourceLabels);
    outputs = std::move (outputLabels);
    setSize (getWidth(), getPreferredHeight());
    repaint();
}

void RoutingMatrixComponent::setRouting (const std::vector<RoutePoint>& routes, bool isDefaultRouting)
{
    routing = routes;
    showingDefault = isDefaultRouting;
    repaint();
}

int RoutingMatrixComponent::getPreferredHeight() const
{
    return headerHeight + juce::jmax (1, sources.size()) * cellSize + 8;
}

juce::Rectangle<float> RoutingMatrixComponent::boundsForCell (int row, int column) const
{
    return { (float) (headerWidth + column * cellSize),
             (float) (headerHeight + row * cellSize),
             (float) cellSize, (float) cellSize };
}

RoutingMatrixComponent::Cell RoutingMatrixComponent::cellAt (juce::Point<float> p) const
{
    if (p.x < headerWidth || p.y < headerHeight)
        return {};

    const auto column = (int) ((p.x - headerWidth) / cellSize);
    const auto row    = (int) ((p.y - headerHeight) / cellSize);

    if (! juce::isPositiveAndBelow (column, outputs.size())
        || ! juce::isPositiveAndBelow (row, sources.size()))
        return {};

    return { row, column };
}

const RoutePoint* RoutingMatrixComponent::find (int source, int output) const
{
    for (const auto& r : routing)
        if (r.sourceChannel == source && r.outputChannel == output)
            return &r;

    return nullptr;
}

void RoutingMatrixComponent::setGain (int source, int output, float gain)
{
    for (auto& r : routing)
    {
        if (r.sourceChannel == source && r.outputChannel == output)
        {
            r.gain = gain;
            showingDefault = false;

            if (onRoutingChanged != nullptr)
                onRoutingChanged (routing);

            repaint();
            return;
        }
    }

    routing.push_back ({ source, output, gain });
    showingDefault = false;

    if (onRoutingChanged != nullptr)
        onRoutingChanged (routing);

    repaint();
}

void RoutingMatrixComponent::toggle (int source, int output)
{
    for (auto it = routing.begin(); it != routing.end(); ++it)
    {
        if (it->sourceChannel == source && it->outputChannel == output)
        {
            routing.erase (it);
            showingDefault = false;

            if (onRoutingChanged != nullptr)
                onRoutingChanged (routing);

            repaint();
            return;
        }
    }

    setGain (source, output, 1.0f);
}

//==============================================================================
void RoutingMatrixComponent::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);

    if (sources.isEmpty() || outputs.isEmpty())
    {
        g.setColour (colours::textDim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (outputs.isEmpty() ? "No audio device open"
                                      : "Select a cue with an audio file",
                    getLocalBounds(), juce::Justification::centred);
        return;
    }

    // Column headers, rotated so 64 outputs still fit across a sane width.
    g.setFont (juce::FontOptions (10.0f));

    for (int c = 0; c < outputs.size(); ++c)
    {
        juce::Graphics::ScopedSaveState state (g);
        const auto x = (float) (headerWidth + c * cellSize + cellSize * 0.5f);

        g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi,
                                                         x, (float) headerHeight - 4.0f));
        g.setColour (hovered.column == c ? colours::text : colours::textDim);
        g.drawText (outputs[c],
                    juce::Rectangle<float> (x - 2.0f, (float) headerHeight - 14.0f, 52.0f, 12.0f),
                    juce::Justification::centredLeft);
    }

    for (int r = 0; r < sources.size(); ++r)
    {
        g.setColour (hovered.row == r ? colours::text : colours::textDim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (sources[r],
                    juce::Rectangle<int> (4, headerHeight + r * cellSize, headerWidth - 10, cellSize),
                    juce::Justification::centredRight);
    }

    for (int r = 0; r < sources.size(); ++r)
    {
        for (int c = 0; c < outputs.size(); ++c)
        {
            const auto cell = boundsForCell (r, c).reduced (1.5f);
            const auto* route = find (r, c);
            const auto isHovered = (hovered.row == r && hovered.column == c);

            g.setColour (isHovered ? colours::panelLight.brighter (0.25f) : colours::panelLight);
            g.fillRoundedRectangle (cell, 3.0f);

            if (route == nullptr)
                continue;

            const auto db = juce::Decibels::gainToDecibels (route->gain, -100.0f);
            const auto atUnity = std::abs (db) < 0.05;

            g.setColour (showingDefault ? colours::standby.withAlpha (0.55f) : colours::go);
            g.fillRoundedRectangle (cell, 3.0f);

            if (! atUnity)
            {
                g.setColour (colours::background);
                g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
                g.drawText (juce::String (juce::roundToInt (db)), cell, juce::Justification::centred);
            }
        }
    }

    if (showingDefault)
    {
        g.setColour (colours::standby);
        g.setFont (juce::FontOptions (10.5f, juce::Font::italic));
        g.drawText ("Default 1:1 routing - click any crosspoint to take manual control",
                    juce::Rectangle<int> (4, 2, getWidth() - 8, 14), juce::Justification::centredLeft);
    }
}

void RoutingMatrixComponent::resized() {}

void RoutingMatrixComponent::mouseDown (const juce::MouseEvent& e)
{
    if (const auto cell = cellAt (e.position); cell.isValid())
    {
        // The first edit of a default routing has to materialise it, otherwise clicking a
        // second crosspoint would silently drop the implicit 1:1 map.
        if (showingDefault)
        {
            showingDefault = false;

            if (onRoutingChanged != nullptr)
                onRoutingChanged (routing);
        }

        toggle (cell.row, cell.column);
    }
}

void RoutingMatrixComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto cell = cellAt (e.position);

    if (! cell.isValid())
        return;

    const auto* route = find (cell.row, cell.column);
    const auto current = route != nullptr ? juce::Decibels::gainToDecibels (route->gain, -100.0f) : 0.0;

    auto* window = new juce::AlertWindow ("Crosspoint trim",
                                          sources[cell.row] + "  to  " + outputs[cell.column],
                                          juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("gain", juce::String (current, 1), "Gain (dB)");
    window->addButton ("Set",    1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true, juce::ModalCallbackFunction::create (
        [this, window, cell] (int result)
        {
            if (result == 1)
            {
                const auto db = juce::jlimit (-100.0, 12.0, window->getTextEditorContents ("gain").getDoubleValue());
                setGain (cell.row, cell.column, juce::Decibels::decibelsToGain ((float) db, -100.0f));
            }

            delete window;
        }), false);
}

void RoutingMatrixComponent::mouseMove (const juce::MouseEvent& e)
{
    const auto cell = cellAt (e.position);

    if (cell.row != hovered.row || cell.column != hovered.column)
    {
        hovered = cell;
        repaint();
    }
}

void RoutingMatrixComponent::mouseExit (const juce::MouseEvent&)
{
    hovered = {};
    repaint();
}

} // namespace cp
