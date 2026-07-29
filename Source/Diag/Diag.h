#pragma once

#include <juce_core/juce_core.h>

/**
    Logging and crash diagnostics.

    Three artefacts, because a failure on site needs different things at
    different moments:

    1. A rotating human-readable log, so an operator can see what happened.
    2. A machine-readable crash report written when the app dies, carrying the
       build identity, the platform, the redacted config, the last few hundred
       log lines and a backtrace.
    3. A single-file diagnostics bundle on demand, so "send me your
       diagnostics" is one instruction.

    Self-contained apart from juce_core, so it copies into the other JUCE repos
    unchanged. Note that a *plugin* must not install the crash handler — see
    installCrashHandler().
*/
namespace cp::diag
{

enum class Level
{
    trace,
    debug,
    info,
    warn,
    error,
    fatal
};

struct Options
{
    /** Names the log files, e.g. "SimpleCue". */
    juce::String appName;
    /** Scopes the environment variables: {PREFIX}_LOG, {PREFIX}_LOG_DIR. */
    juce::String envPrefix;
    juce::String version;
    Level defaultLevel = Level::info;
    /** False for plugins: a plugin lives in someone else's process and must
        not install a process-wide crash handler. */
    bool installCrashHandler = true;
};

/** Installs logging and, unless disabled, the crash handler. Call once, as
    early as possible — before anything that can fail. */
void init (const Options& options);

/** Flushes and closes the log. Call from the app's shutdown(). */
void shutdown();

/** Attaches the effective configuration to crash reports and bundles.

    Separate from init() so logging is up before settings are read. Keys that
    look like secrets are redacted here, once. */
void setConfig (const juce::var& config);

void write (Level level, const juce::String& message);

/** The directory logs, crash reports and bundles are written to. */
juce::File logDirectory();

/** Assembles a single-file diagnostics bundle. Returns the file, or a
    non-existent File if it could not be written. */
juce::File collectDiagnostics();

/** Writes a crash report for a C++ exception or other non-signal fault. */
juce::File writeCrashReport (const juce::String& trigger,
                             const juce::String& message,
                             const juce::String& backtrace);

/** Replaces values whose key looks like a secret, at any depth. */
juce::var redact (const juce::var& value);

} // namespace cp::diag

// Deliberately macros: they capture the message only when the level is
// enabled, so a trace call in the audio path costs a comparison rather than a
// String construction.
#define CP_LOG(level, text)                                                    \
    ::cp::diag::write (level, juce::String (text))

#define CP_LOG_TRACE(text) CP_LOG (::cp::diag::Level::trace, text)
#define CP_LOG_DEBUG(text) CP_LOG (::cp::diag::Level::debug, text)
#define CP_LOG_INFO(text)  CP_LOG (::cp::diag::Level::info,  text)
#define CP_LOG_WARN(text)  CP_LOG (::cp::diag::Level::warn,  text)
#define CP_LOG_ERROR(text) CP_LOG (::cp::diag::Level::error, text)
#define CP_LOG_FATAL(text) CP_LOG (::cp::diag::Level::fatal, text)
