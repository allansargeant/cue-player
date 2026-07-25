#include <juce_gui_extra/juce_gui_extra.h>

#include <cstdio>

#include "App/MainComponent.h"
#include "GUI/LookAndFeel.h"

namespace cp
{

class SimpleCueApplication : public juce::JUCEApplication
{
public:
    SimpleCueApplication() = default;

    const juce::String getApplicationName() override    { return "SimpleCue"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String& commandLine) override
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        juce::PropertiesFile::Options options;
        options.applicationName     = "SimpleCue";
        options.filenameSuffix      = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        options.folderName          = "SimpleCue";
        properties.setStorageParameters (options);

        mainWindow = std::make_unique<MainWindow> (getApplicationName(), properties);

        // `--screenshots <dir>` loads a demo show and writes PNGs of the app for the README,
        // then quits. Rendering happens offscreen through JUCE's own rasteriser, so it needs
        // no screen-recording permission and captures exactly what the app draws.
        if (const auto arguments = juce::StringArray::fromTokens (commandLine, true);
            arguments.contains ("--screenshots"))
        {
            const auto target = arguments[arguments.indexOf ("--screenshots") + 1].unquoted().trim();

            if (target.isEmpty())
            {
                std::fprintf (stderr, "--screenshots needs an output directory\n");
                quit();
                return;
            }

            mainWindow->setSize (1440, 900);
            mainWindow->getMainComponent().captureScreenshots (juce::File (target),
                                                               [this] { quit(); });
            return;
        }

        if (commandLine.isNotEmpty())
        {
            const juce::File showFile (commandLine.unquoted().trim());

            if (showFile.existsAsFile())
                mainWindow->getMainComponent().openShowFile (showFile);
        }
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        properties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (mainWindow == nullptr)
        {
            quit();
            return;
        }

        // Never drop a show on the floor because someone hit Cmd-Q between cues.
        mainWindow->getMainComponent().confirmDiscardChanges ([this] (bool proceed)
        {
            if (proceed)
                quit();
        });
    }

    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        if (mainWindow == nullptr)
            return;

        const juce::File showFile (commandLine.unquoted().trim());

        if (showFile.existsAsFile())
            mainWindow->getMainComponent().openShowFile (showFile);
    }

    //==========================================================================
    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, juce::ApplicationProperties& props)
            : DocumentWindow (name, colours::background, DocumentWindow::allButtons)
        {
            auto* component = new MainComponent (props);
            mainComponent = component;

            setUsingNativeTitleBar (true);
            setContentOwned (component, true);
            setResizable (true, false);
            setResizeLimits (960, 600, 10000, 10000);

           #if JUCE_MAC
            juce::MenuBarModel::setMacMainMenu (component, nullptr);
           #else
            setMenuBar (component);
           #endif

            if (auto* user = props.getUserSettings())
                restoreWindowStateFromString (user->getValue ("windowState"));
            else
                centreWithSize (1280, 820);

            if (getWidth() < 960 || getHeight() < 600)
                centreWithSize (1280, 820);

            setVisible (true);
            component->grabKeyboardFocus();
        }

        ~MainWindow() override
        {
           #if JUCE_MAC
            juce::MenuBarModel::setMacMainMenu (nullptr);
           #else
            setMenuBar (nullptr);
           #endif
        }

        MainComponent& getMainComponent() { return *mainComponent; }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        juce::Component::SafePointer<MainComponent> mainComponent;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    SimpleCueLookAndFeel lookAndFeel;
    juce::ApplicationProperties properties;
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace cp

START_JUCE_APPLICATION (cp::SimpleCueApplication)
