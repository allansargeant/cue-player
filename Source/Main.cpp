#include <juce_gui_extra/juce_gui_extra.h>

#include "App/MainComponent.h"
#include "GUI/LookAndFeel.h"

namespace cp
{

class CuePlayerApplication : public juce::JUCEApplication
{
public:
    CuePlayerApplication() = default;

    const juce::String getApplicationName() override    { return "Cue Player"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String& commandLine) override
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        juce::PropertiesFile::Options options;
        options.applicationName     = "Cue Player";
        options.filenameSuffix      = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        options.folderName          = "CuePlayer";
        properties.setStorageParameters (options);

        mainWindow = std::make_unique<MainWindow> (getApplicationName(), properties);

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
    CuePlayerLookAndFeel lookAndFeel;
    juce::ApplicationProperties properties;
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace cp

START_JUCE_APPLICATION (cp::CuePlayerApplication)
