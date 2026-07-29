/*
    Exercise the crash path end to end.

    A crash handler that has never been fired is a guess, not a feature — so
    this deliberately dies, in the two ways the app can actually die:

        ./build/SimpleCueDiagCrash segv        (native signal)
        ./build/SimpleCueDiagCrash exception   (uncaught C++ exception)

    Read the JSON it leaves behind, and check that `api_token` came out
    <redacted>.
*/

#include <juce_core/juce_core.h>

#include "Diag/Diag.h"

int main (int argc, char* argv[])
{
    const juce::String mode (argc > 1 ? argv[1] : "segv");

    cp::diag::init ({ "diag-crash-example", "DIAG_EXAMPLE", "0.0.0", cp::diag::Level::debug });

    auto* config = new juce::DynamicObject();
    config->setProperty ("audio_device", "MOTU 828");
    config->setProperty ("sample_rate", 48000);
    config->setProperty ("osc_port", 53000);
    config->setProperty ("api_token", "should-not-appear");
    cp::diag::setConfig (juce::var (config));

    CP_LOG_INFO ("show loaded cues=42 path=gala.simplecue");
    CP_LOG_DEBUG ("audio device opened rate=48000 buffer=256");
    CP_LOG_WARN ("cue 17 references a missing file");
    CP_LOG_INFO ("GO pressed cue=18");

    if (mode == "exception")
    {
        // The app catches this through JUCEApplication::unhandledException; a
        // console app has no message loop, so raise it the plain way and let
        // the report be written from the catch.
        try
        {
            throw std::runtime_error ("sample rate changed under a playing cue");
        }
        catch (const std::exception& e)
        {
            cp::diag::writeCrashReport ("unhandled-exception", e.what(),
                                        juce::SystemStats::getStackBacktrace());
            return 1;
        }
    }

    CP_LOG_INFO ("about to touch a null cue voice");

    // A plausible fault: a voice freed on the message thread while the audio
    // thread still holds the pointer. This is what the native handler is for.
    volatile int* voice = nullptr;
    return *voice;
}
