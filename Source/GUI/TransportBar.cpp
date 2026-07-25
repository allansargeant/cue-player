#include "GUI/TransportBar.h"

namespace cp
{

TransportBar::TransportBar (AudioEngine& engine)
    : audioEngine (engine)
{
    const auto styleButton = [this] (juce::TextButton& b, juce::Colour colour, juce::Colour textColour)
    {
        b.setColour (juce::TextButton::buttonColourId, colour);
        b.setColour (juce::TextButton::textColourOffId, textColour);
        addAndMakeVisible (b);
    };

    styleButton (goButton,    colours::go,         colours::background);
    styleButton (stopButton,  colours::panelLight, colours::text);
    styleButton (panicButton, colours::stop,       colours::background);
    styleButton (pauseButton, colours::panelLight, colours::text);
    styleButton (vampButton,  colours::vamp,       colours::background);
    styleButton (setupButton, colours::panelLight, colours::textDim);

    // The key is printed on the button, so the shortcut is learnable from the surface
    // rather than only from a menu nobody opens mid-show.
    goButton.setButtonText ("GO  (space)");
    stopButton.setButtonText ("Stop all (S)");
    panicButton.setButtonText ("PANIC (esc)");
    vampButton.setButtonText ("Release vamp (enter)");

    goButton.onClick    = [this] { if (onGo) onGo(); };
    stopButton.onClick  = [this] { if (onStopAll) onStopAll(); };
    panicButton.onClick = [this] { if (onPanic) onPanic(); };
    pauseButton.onClick = [this] { if (onPauseToggle) onPauseToggle(); };
    vampButton.onClick  = [this] { if (onReleaseVamp) onReleaseVamp(); };
    setupButton.onClick = [this] { if (onAudioSetup) onAudioSetup(); };

    masterSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    masterSlider.setRange (-60.0, 12.0, 0.1);
    masterSlider.setValue (0.0, juce::dontSendNotification);
    masterSlider.setTextValueSuffix (" dB");
    masterSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 20);
    masterSlider.onValueChange = [this] { audioEngine.setMasterGainDb (masterSlider.getValue()); };
    addAndMakeVisible (masterSlider);

    standbyNumberLabel.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    standbyNumberLabel.setColour (juce::Label::textColourId, colours::standby);
    addAndMakeVisible (standbyNumberLabel);

    standbyNameLabel.setFont (juce::FontOptions (14.0f));
    standbyNameLabel.setColour (juce::Label::textColourId, colours::text);
    addAndMakeVisible (standbyNameLabel);

    statusLabel.setFont (juce::FontOptions (11.0f));
    statusLabel.setColour (juce::Label::textColourId, colours::textDim);
    statusLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (statusLabel);

    startTimerHz (25);
}

TransportBar::~TransportBar()
{
    stopTimer();
}

void TransportBar::setStandbyText (const juce::String& number, const juce::String& name)
{
    standbyNumberLabel.setText (number, juce::dontSendNotification);
    standbyNameLabel.setText (name, juce::dontSendNotification);
}

void TransportBar::setShowStatus (const juce::String& text)
{
    statusLabel.setText (text, juce::dontSendNotification);
}

void TransportBar::timerCallback()
{
    pauseButton.setButtonText (audioEngine.isPaused() ? "Resume (P)" : "Pause (P)");

    const auto vamping = audioEngine.isAnythingVamping();
    vampButton.setEnabled (vamping);
    vampButton.setColour (juce::TextButton::buttonColourId,
                          vamping ? colours::vamp : colours::panelLight);

    const auto active = audioEngine.getNumActiveVoices();
    stopButton.setEnabled (active > 0);
    pauseButton.setEnabled (active > 0);

    if (std::abs (masterSlider.getValue() - audioEngine.getMasterGainDb()) > 0.05)
        masterSlider.setValue (audioEngine.getMasterGainDb(), juce::dontSendNotification);

    meterChannels = juce::jmin (16, audioEngine.getNumOutputChannels());
    repaint (meterArea);
}

void TransportBar::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
    g.setColour (colours::outline);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

    g.setColour (colours::textDim);
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("STANDBY", standbyNumberLabel.getX(), 4, 90, 10, juce::Justification::centredLeft);
    g.drawText ("MASTER", masterSlider.getX(), 4, 90, 10, juce::Justification::centredLeft);

    drawMeters (g, meterArea);
}

void TransportBar::drawMeters (juce::Graphics& g, juce::Rectangle<int> area)
{
    if (meterChannels <= 0 || area.isEmpty())
        return;

    g.setColour (colours::textDim);
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("OUTPUTS", area.getX(), 4, 90, 10, juce::Justification::centredLeft);

    auto meters = area.withTrimmedTop (16);
    const auto barWidth = juce::jmax (3, (meters.getWidth() - (meterChannels - 1) * 2) / meterChannels);

    for (int ch = 0; ch < meterChannels; ++ch)
    {
        auto bar = juce::Rectangle<int> (meters.getX() + ch * (barWidth + 2), meters.getY(),
                                         barWidth, meters.getHeight());

        g.setColour (colours::panelLight);
        g.fillRect (bar);

        const auto peak = audioEngine.getOutputPeak (ch);

        if (peak <= 0.0001f)
            continue;

        // dBFS mapped over a 60 dB window; clipping is called out in red rather than
        // being left to the operator to infer from a bar that has simply stopped moving.
        const auto db = juce::Decibels::gainToDecibels (peak, -60.0f);
        const auto proportion = juce::jlimit (0.0f, 1.0f, (float) ((db + 60.0) / 60.0));
        const auto height = juce::roundToInt (proportion * bar.getHeight());

        g.setColour (peak >= 0.999f ? colours::meterClip
                                    : (db > -6.0 ? colours::meterHigh : colours::meterLow));
        g.fillRect (bar.removeFromBottom (height));
    }
}

void TransportBar::resized()
{
    auto bounds = getLocalBounds().reduced (8, 6);

    goButton.setBounds (bounds.removeFromLeft (124).reduced (0, 2));
    bounds.removeFromLeft (10);

    auto standby = bounds.removeFromLeft (224).withTrimmedTop (12);
    standbyNumberLabel.setBounds (standby.removeFromTop (26));
    standbyNameLabel.setBounds (standby);
    bounds.removeFromLeft (10);

    // Wide enough for the label *and* its bracketed shortcut on one line. A button that
    // wraps to two lines looks broken, and these are read at a glance under pressure.
    auto buttons = bounds.removeFromLeft (424).withTrimmedTop (14);
    stopButton.setBounds (buttons.removeFromLeft (100).reduced (2, 0));
    pauseButton.setBounds (buttons.removeFromLeft (86).reduced (2, 0));
    vampButton.setBounds (buttons.removeFromLeft (146).reduced (2, 0));
    panicButton.setBounds (buttons.removeFromLeft (92).reduced (2, 0));

    auto right = bounds.removeFromRight (300);
    setupButton.setBounds (right.removeFromTop (22).removeFromRight (100));
    statusLabel.setBounds (right.removeFromBottom (16));

    auto master = bounds.removeFromRight (240).withTrimmedTop (14);
    masterSlider.setBounds (master.removeFromTop (24));

    meterArea = bounds.reduced (10, 0);
}

} // namespace cp
