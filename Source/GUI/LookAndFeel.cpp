#include "GUI/LookAndFeel.h"

namespace cp
{

SimpleCueLookAndFeel::SimpleCueLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, colours::background);
    setColour (juce::DocumentWindow::textColourId,        colours::text);

    setColour (juce::Label::textColourId,                 colours::text);
    setColour (juce::TextEditor::backgroundColourId,      colours::panelLight);
    setColour (juce::TextEditor::textColourId,            colours::text);
    setColour (juce::TextEditor::outlineColourId,         colours::outline);
    setColour (juce::TextEditor::highlightColourId,       colours::standby.withAlpha (0.4f));

    setColour (juce::TextButton::buttonColourId,          colours::panelLight);
    setColour (juce::TextButton::textColourOffId,         colours::text);
    setColour (juce::TextButton::textColourOnId,          colours::background);

    setColour (juce::ComboBox::backgroundColourId,        colours::panelLight);
    setColour (juce::ComboBox::textColourId,              colours::text);
    setColour (juce::ComboBox::outlineColourId,           colours::outline);
    setColour (juce::ComboBox::arrowColourId,             colours::textDim);

    setColour (juce::PopupMenu::backgroundColourId,       colours::panel);
    setColour (juce::PopupMenu::textColourId,             colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::standby);

    setColour (juce::ListBox::backgroundColourId,         colours::panel);
    setColour (juce::TableHeaderComponent::backgroundColourId, colours::panelLight);
    setColour (juce::TableHeaderComponent::textColourId,  colours::textDim);
    setColour (juce::TableHeaderComponent::outlineColourId, colours::outline);

    setColour (juce::Slider::backgroundColourId,          colours::panelLight);
    setColour (juce::Slider::thumbColourId,               colours::text);
    setColour (juce::Slider::trackColourId,               colours::standby);
    setColour (juce::Slider::textBoxTextColourId,         colours::text);
    setColour (juce::Slider::textBoxBackgroundColourId,   colours::panelLight);
    setColour (juce::Slider::textBoxOutlineColourId,      colours::outline);

    setColour (juce::ToggleButton::textColourId,          colours::text);
    setColour (juce::ToggleButton::tickColourId,          colours::go);
    setColour (juce::ToggleButton::tickDisabledColourId,  colours::outline);

    setColour (juce::TabbedComponent::backgroundColourId, colours::panel);
    setColour (juce::TabbedButtonBar::tabOutlineColourId, colours::outline);
    setColour (juce::TabbedButtonBar::frontTextColourId,  colours::text);
    setColour (juce::TabbedButtonBar::tabTextColourId,    colours::textDim);

    setColour (juce::ScrollBar::thumbColourId,            colours::outline);
    setColour (juce::TooltipWindow::backgroundColourId,   colours::panelLight);
    setColour (juce::TooltipWindow::textColourId,         colours::text);
    setColour (juce::AlertWindow::backgroundColourId,     colours::panel);
    setColour (juce::AlertWindow::textColourId,           colours::text);
    setColour (juce::AlertWindow::outlineColourId,        colours::outline);
}

void SimpleCueLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                                 const juce::Colour& backgroundColour,
                                                 bool shouldDrawAsHighlighted, bool shouldDrawAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    auto base = backgroundColour;

    if (shouldDrawAsDown)            base = base.brighter (0.25f);
    else if (shouldDrawAsHighlighted) base = base.brighter (0.12f);

    if (! button.isEnabled())
        base = base.withAlpha (0.4f);

    g.setColour (base);
    g.fillRoundedRectangle (bounds, 4.0f);

    g.setColour (colours::outline);
    g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
}

void SimpleCueLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                             float sliderPos, float rotaryStartAngle,
                                             float rotaryEndAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto centre = bounds.getCentre();
    const auto angle  = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const auto lineW  = juce::jmax (2.0f, radius * 0.14f);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius - lineW * 0.5f, radius - lineW * 0.5f,
                         0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (colours::panelLight);
    g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, radius - lineW * 0.5f, radius - lineW * 0.5f,
                         0.0f, rotaryStartAngle, angle, true);
    g.setColour (slider.findColour (juce::Slider::trackColourId));
    g.strokePath (value, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    juce::Path pointer;
    pointer.addRectangle (-lineW * 0.35f, -radius, lineW * 0.7f, radius * 0.45f);
    pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (centre));
    g.setColour (colours::text);
    g.fillPath (pointer);
}

//==============================================================================
ClickToAdjustSlider::ClickToAdjustSlider()
{
    // Clicking is what arms the wheel, so the slider has to be able to take focus.
    setWantsKeyboardFocus (true);
}

void ClickToAdjustSlider::mouseWheelMove (const juce::MouseEvent& e,
                                          const juce::MouseWheelDetails& wheel)
{
    if (! hasKeyboardFocus (false))
    {
        // Hand the gesture on so the panel underneath scrolls as the operator expects,
        // rather than swallowing it and appearing to do nothing.
        juce::Component::mouseWheelMove (e, wheel);
        return;
    }

    juce::Slider::mouseWheelMove (e, wheel);
}

void ClickToAdjustSlider::focusGained (FocusChangeType)
{
    repaint();
}

void ClickToAdjustSlider::focusLost (FocusChangeType)
{
    repaint();
}

//==============================================================================
juce::String formatTime (double seconds)
{
    if (seconds < 0.0 || std::isnan (seconds))
        return "--:--";

    const auto totalTenths = (juce::int64) (seconds * 10.0 + 0.5);
    const auto tenths  = (int) (totalTenths % 10);
    const auto total   = totalTenths / 10;
    const auto secs    = (int) (total % 60);
    const auto minutes = (int) ((total / 60) % 60);
    const auto hours   = (int) (total / 3600);

    if (hours > 0)
        return juce::String::formatted ("%d:%02d:%02d.%d", hours, minutes, secs, tenths);

    return juce::String::formatted ("%02d:%02d.%d", minutes, secs, tenths);
}

juce::String formatTimecode (double seconds)
{
    if (seconds < 0.0 || std::isnan (seconds))
        seconds = 0.0;

    const auto totalMs = (juce::int64) (seconds * 1000.0 + 0.5);
    const auto millis  = (int) (totalMs % 1000);
    const auto total   = totalMs / 1000;
    const auto secs    = (int) (total % 60);
    const auto minutes = (int) ((total / 60) % 60);
    const auto hours   = (int) (total / 3600);

    if (hours > 0)
        return juce::String::formatted ("%d:%02d:%02d.%03d", hours, minutes, secs, millis);

    return juce::String::formatted ("%02d:%02d.%03d", minutes, secs, millis);
}

double parseTimecode (const juce::String& text, double fallback)
{
    const auto trimmed = text.trim();

    if (trimmed.isEmpty())
        return fallback;

    // Colon-separated fields count from the right, so "5" is five seconds, "1:05" is a
    // minute and five, and "1:00:05" is an hour and five. That way an operator can type
    // whichever is quickest without having to pad the rest out.
    const auto parts = juce::StringArray::fromTokens (trimmed, ":", "");

    if (parts.size() > 3)
        return fallback;

    double seconds = 0.0;
    bool sawNumber = false;

    for (int i = 0; i < parts.size(); ++i)
    {
        const auto field = parts[i].trim();

        if (field.isEmpty() || ! field.containsAnyOf ("0123456789"))
            return fallback;

        seconds = seconds * 60.0 + field.getDoubleValue();
        sawNumber = true;
    }

    if (! sawNumber)
        return fallback;

    return juce::jmax (0.0, seconds);
}

} // namespace cp
