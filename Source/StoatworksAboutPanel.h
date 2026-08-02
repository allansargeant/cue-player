/*
 * Stoatworks Labs - About panel for the JUCE apps and plugins.
 *
 * The same six things every other Stoatworks Labs product shows: the name, the
 * version it is actually running, its user guide, its project page, its source,
 * and the four ways to fund the work - over the Stoatworks Labs mark.
 *
 * This file is the MASTER, in stoatworks-backend/about/juce. It is vendored into
 * each JUCE repo by ../../scripts/sync-about.py - edit it THERE and re-run the
 * sync, never the copies.
 *
 * The facts come from StoatworksAbout.h beside it, which is generated from the
 * website's projects.json. Nothing here is written down twice.
 *
 * ------------------------------------------------------------------ using it
 *
 * A member of the editor, hidden until asked for:
 *
 *     stoatworks::AboutPanel aboutPanel;                        // member
 *     juce::TextButton aboutButton { "i" };                     // member
 *
 *     addChildComponent(aboutPanel);                            // constructor
 *     addAndMakeVisible(aboutButton);
 *     aboutButton.onClick = [this] { aboutPanel.setVisible(true); };
 *
 *     aboutPanel.setBounds(getLocalBounds());                   // resized()
 *
 * A child rather than a DocumentWindow on purpose: a plugin editor is a view
 * inside somebody else's host, and a second top-level window from a plugin is
 * both bad manners and unreliable across hosts.
 *
 * ------------------------------------------------------------- the version
 *
 * Taken from the build, in this order, and never typed in:
 *   1. JucePlugin_VersionString - defined by juce_add_plugin.
 *   2. JUCE_APPLICATION_VERSION_STRING - the GUI apps define this themselves.
 *   3. versionFallback in StoatworksAbout.h, which is a copy read at sync time
 *      and the only one that can go stale.
 *
 * ------------------------------------------------------------------- ASCII
 *
 * Every string in here, and everything the generator writes into
 * StoatworksAbout.h, is ASCII. JUCE asserts on a non-ASCII char literal and
 * mangles the text it renders, and MSVC warns on a UTF-8 source without /utf-8.
 * No em dashes, however much the house style wants them.
 */

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "StoatworksAbout.h"
#include "StoatworksAboutMark.h"

namespace stoatworks
{

class AboutPanel : public juce::Component
{
public:
    AboutPanel()
    {
        /*
         * The mark is carried as base64 rather than as BinaryData so this drops
         * into a repo without touching its CMakeLists - juce_add_binary_data
         * would be a build-system change in every one of them for one image.
         */
        juce::MemoryOutputStream png;
        if (juce::Base64::convertFromBase64(png, aboutMarkPngBase64))
            mark = juce::ImageFileFormat::loadFrom(png.getData(), png.getDataSize());

        addAndMakeVisible(closeButton);
        closeButton.setButtonText("x");
        closeButton.onClick = [this] { setVisible(false); };

        addLink("User guide", about::guide);
        addLink("Project page", about::page);
        addLink("Source on GitHub", about::repo);

        for (const auto& f : about::funding)
            fundingLinks.add(makeLink(f.name, f.url));

        for (auto* l : fundingLinks)
            addAndMakeVisible(l);

        setInterceptsMouseClicks(true, true);
        setWantsKeyboardFocus(true);
    }

    /** The version this build actually is. See the header comment. */
    static juce::String version()
    {
       #if defined(JucePlugin_VersionString)
        return JucePlugin_VersionString;
       #elif defined(JUCE_APPLICATION_VERSION_STRING)
        return JUCE_APPLICATION_VERSION_STRING;
       #else
        return juce::String(about::versionFallback).trimCharactersAtStart("v");
       #endif
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xa8040a11));

        const auto card = cardBounds();
        g.setColour(juce::Colour(0xff0d1b2a));
        g.fillRoundedRectangle(card.toFloat(), 10.0f);
        g.setColour(juce::Colour(0xff223c56));
        g.drawRoundedRectangle(card.toFloat(), 10.0f, 1.0f);

        // Behind the text, not beside it: the dialog sits over the mark.
        if (mark.isValid())
        {
            const int width = juce::roundToInt((float) card.getWidth() * 0.78f);
            const int height = juce::roundToInt((float) width * (float) mark.getHeight()
                                                / (float) mark.getWidth());
            g.setOpacity(0.07f);
            g.drawImage(mark,
                        card.getCentreX() - width / 2,
                        card.getCentreY() - height / 2,
                        width, height,
                        0, 0, mark.getWidth(), mark.getHeight());
            g.setOpacity(1.0f);
        }

        auto body = card.reduced(padding);

        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(juce::FontOptions(21.0f, juce::Font::bold)));
        g.drawText(about::name, body.removeFromTop(28), juce::Justification::centredLeft);

        auto meta = body.removeFromTop(22);
        g.setColour(juce::Colour(0xff4cc9f0));
        g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain)));
        const auto v = "v" + version().trimCharactersAtStart("v");
        const auto vWidth = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), v) + 4;
        g.drawText(v, meta.removeFromLeft(vWidth), juce::Justification::centredLeft);

        if (juce::String(about::licence).isNotEmpty())
        {
            g.setColour(juce::Colour(0xff64798f));
            g.setFont(juce::Font(juce::FontOptions(12.0f)));
            g.drawText(juce::String(about::licence) + " licensed",
                       meta.withTrimmedLeft(10), juce::Justification::centredLeft);
        }

        body.removeFromTop(6);
        g.setColour(juce::Colour(0xff93a8bd));
        g.setFont(juce::Font(juce::FontOptions(13.0f)));
        g.drawText(about::hook, body.removeFromTop(20), juce::Justification::centredLeft);

        g.setColour(juce::Colour(0xff64798f));
        g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
        body.removeFromTop(14);
        if (! links.isEmpty())
        {
            g.drawText("DOCUMENTATION", body.removeFromTop(14), juce::Justification::centredLeft);
            body.removeFromTop(rowHeight * links.size() + 8);
        }
        g.drawText("SUPPORT THE WORK", body.removeFromTop(14), juce::Justification::centredLeft);

        // The footer, below the funding chips.
        auto footer = card.reduced(padding).removeFromBottom(34);
        g.setColour(juce::Colour(0xff1a2e44));
        g.drawHorizontalLine(footer.getY(), (float) footer.getX(), (float) footer.getRight());
        g.setColour(juce::Colour(0xff64798f));
        g.setFont(juce::Font(juce::FontOptions(11.5f)));
        g.drawText(juce::String(about::org) + " - " + about::tagline,
                   footer.withTrimmedTop(8), juce::Justification::topLeft);
    }

    void resized() override
    {
        const auto card = cardBounds();
        closeButton.setBounds(card.getRight() - 34, card.getY() + 8, 26, 26);

        auto body = card.reduced(padding);
        body.removeFromTop(28 + 22 + 6 + 20 + 14);   // title, meta, hook, gap

        if (! links.isEmpty())
        {
            body.removeFromTop(14);                   // DOCUMENTATION heading
            for (auto* l : links)
                l->setBounds(body.removeFromTop(rowHeight));
            body.removeFromTop(8);
        }

        body.removeFromTop(14);                       // SUPPORT THE WORK heading
        auto chips = body.removeFromTop(rowHeight);
        const int chipWidth = chips.getWidth() / juce::jmax(1, fundingLinks.size());
        for (auto* l : fundingLinks)
            l->setBounds(chips.removeFromLeft(chipWidth).reduced(2, 0));
    }

    /** Click outside the card, or press Escape, to dismiss. */
    void mouseUp(const juce::MouseEvent& e) override
    {
        if (! cardBounds().contains(e.getPosition()))
            setVisible(false);
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key != juce::KeyPress::escapeKey)
            return false;

        setVisible(false);
        return true;
    }

    void visibilityChanged() override
    {
        if (isVisible())
            grabKeyboardFocus();
    }

private:
    static constexpr int padding = 22;
    static constexpr int rowHeight = 24;

    juce::Rectangle<int> cardBounds() const
    {
        const int w = juce::jmin(420, getWidth() - 40);
        const int h = juce::jmin(cardHeight(), getHeight() - 40);
        return juce::Rectangle<int>(w, h).withCentre(getLocalBounds().getCentre());
    }

    int cardHeight() const
    {
        return padding * 2 + 28 + 22 + 6 + 20 + 14
             + (links.isEmpty() ? 0 : 14 + rowHeight * links.size() + 8)
             + 14 + rowHeight + 34;
    }

    void addLink(const char* label, const char* url)
    {
        // A guide that has not been written, or a repo that is still private,
        // has an empty string here rather than a plausible URL that 404s. The
        // row is left out entirely, and cardHeight() shrinks to match.
        if (juce::String(url).isEmpty())
            return;

        auto* l = makeLink(label, url);
        links.add(l);
        addAndMakeVisible(l);
    }

    static juce::HyperlinkButton* makeLink(const char* label, const char* url)
    {
        auto* l = new juce::HyperlinkButton(label, juce::URL(url));
        l->setJustificationType(juce::Justification::centredLeft);
        l->setColour(juce::HyperlinkButton::textColourId, juce::Colour(0xffe8eef5));
        l->setFont(juce::Font(juce::FontOptions(13.0f)), false, juce::Justification::centredLeft);
        return l;
    }

    juce::Image mark;
    juce::TextButton closeButton;
    juce::OwnedArray<juce::HyperlinkButton> links;
    juce::OwnedArray<juce::HyperlinkButton> fundingLinks;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AboutPanel)
};

} // namespace stoatworks
