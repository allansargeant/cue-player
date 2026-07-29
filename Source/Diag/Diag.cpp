#include "Diag.h"

#include <ctime>
#include <iostream>

namespace cp::diag
{

namespace
{

/** Identifies the document shape to anything reading it later. */
constexpr const char* schema = "stoatworks.diagnostics/1";

/** Lines held in memory for crash reports. */
constexpr int ringCapacity = 500;
/** Days of rotated logs kept on disk. */
constexpr int keepLogFiles = 7;

/** Bundle caps, so one runaway log cannot make a bundle unusable. */
constexpr int maxLogFiles = 3;
constexpr int maxLinesPerFile = 5000;
constexpr int maxCrashReports = 5;

const char* levelName (Level level) noexcept
{
    switch (level)
    {
        case Level::trace: return "TRACE";
        case Level::debug: return "DEBUG";
        case Level::info:  return "INFO";
        case Level::warn:  return "WARN";
        case Level::error: return "ERROR";
        case Level::fatal: return "FATAL";
    }
    return "?";
}

Level levelFromName (const juce::String& name, Level fallback) noexcept
{
    const auto upper = name.toUpperCase();
    if (upper == "TRACE") return Level::trace;
    if (upper == "DEBUG") return Level::debug;
    if (upper == "INFO")  return Level::info;
    if (upper == "WARN" || upper == "WARNING") return Level::warn;
    if (upper == "ERROR") return Level::error;
    if (upper == "FATAL") return Level::fatal;
    return fallback;
}

/** `20260729T141500Z` — safe in a filename on Windows, where `:` is not.

    UTC, and via strftime rather than juce::Time, because juce::Time::formatted
    renders local time and a "Z" on a local timestamp is a lie that will be
    believed later. */
juce::String stampCompact()
{
    const auto now = std::time (nullptr);
    std::tm utc {};

   #if JUCE_WINDOWS
    gmtime_s (&utc, &now);
   #else
    gmtime_r (&now, &utc);
   #endif

    char buffer[32] {};
    std::strftime (buffer, sizeof (buffer), "%Y%m%dT%H%M%SZ", &utc);
    return juce::String (buffer);
}

juce::String nowIso()
{
    return juce::Time::getCurrentTime().toISO8601 (true);
}

bool isSensitive (const juce::String& key)
{
    static const char* words[] = { "password", "passwd", "passphrase", "secret",
                                   "token",    "apikey", "credential", "auth",
                                   "private" };

    const auto flat = key.toLowerCase().removeCharacters ("-_");

    for (auto* word : words)
        if (flat.contains (word))
            return true;

    return false;
}

//==============================================================================
/**
    Everything the crash handler and the bundler both need.

    A single global, guarded by a lock, because the crash handler may run on
    any thread and cannot go looking for an owner.
*/
struct State
{
    juce::CriticalSection lock;

    juce::String appName, version, gitRev;
    juce::File dir;
    Level level = Level::info;
    juce::var config;
    juce::StringArray ring;
    juce::String startedAt;
    juce::int64 startedMs = 0;

    juce::String currentDay;
    std::unique_ptr<juce::FileOutputStream> stream;
};

State& state()
{
    static State s;
    return s;
}

std::atomic<bool> initialised { false };

//==============================================================================
juce::File resolveLogDirectory (const juce::String& appName, const juce::String& envPrefix)
{
    // {PREFIX}_LOG_DIR overrides, which is how you point a whole rack at one
    // collected location.
    const auto override_ = juce::SystemStats::getEnvironmentVariable (envPrefix + "_LOG_DIR", {});

    if (override_.isNotEmpty())
        return juce::File (override_);

    // getSystemLogFileFolder is ~/Library/Logs on macOS and the per-user app
    // data folder elsewhere — the platform convention in both cases.
    return juce::FileLogger::getSystemLogFileFolder().getChildFile (appName);
}

void pruneOldLogs (const juce::File& dir, const juce::String& appName)
{
    auto files = dir.findChildFiles (juce::File::findFiles, false, appName + ".*.log");

    if (files.size() <= keepLogFiles)
        return;

    // Names carry an ISO date, so lexical order is chronological order.
    files.sort();

    for (int i = 0; i < files.size() - keepLogFiles; ++i)
        files[i].deleteFile();
}

/** Opens today's log file, rotating if the day has turned.

    Caller must hold the lock. */
void ensureStream (State& s)
{
    const auto today = juce::Time::getCurrentTime().formatted ("%Y-%m-%d");

    if (s.stream != nullptr && today == s.currentDay)
        return;

    s.stream.reset();
    s.currentDay = today;
    s.dir.createDirectory();

    auto file = s.dir.getChildFile (s.appName + "." + today + ".log");
    auto stream = std::make_unique<juce::FileOutputStream> (file);

    if (stream->failedToOpen())
        return; // A log we cannot write must not take the app down with it.

    stream->setPosition (file.getSize());
    s.stream = std::move (stream);
    pruneOldLogs (s.dir, s.appName);
}

//==============================================================================
juce::var buildAppInfo (State& s)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("name", s.appName);
    o->setProperty ("version", s.version);
    o->setProperty ("git_rev", s.gitRev);
    return { o };
}

juce::var buildPlatformInfo()
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("os", juce::SystemStats::getOperatingSystemName());
    o->setProperty ("arch", juce::SystemStats::isOperatingSystem64Bit() ? "64-bit" : "32-bit");
    o->setProperty ("cpu", juce::SystemStats::getCpuModel());
    o->setProperty ("cpus", juce::SystemStats::getNumCpus());
    o->setProperty ("memory_mb", juce::SystemStats::getMemorySizeInMegabytes());
    o->setProperty ("hostname", juce::SystemStats::getComputerName());
    o->setProperty ("juce_version", juce::SystemStats::getJUCEVersion());
    return { o };
}

juce::var buildProcessInfo (State& s)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("started_at", s.startedAt);
    o->setProperty ("uptime_seconds",
                    (int) ((juce::Time::currentTimeMillis() - s.startedMs) / 1000));
    return { o };
}

juce::var ringAsArray (State& s)
{
    juce::Array<juce::var> lines;

    for (const auto& line : s.ring)
        lines.add (line);

    return lines;
}

juce::File writeJson (const juce::File& dir, const juce::String& name, const juce::var& value)
{
    // Falls back to temp: a report that cannot be written because the log
    // directory vanished is the one case where writing somewhere unexpected
    // beats writing nowhere.
    for (auto candidate : { dir, juce::File::getSpecialLocation (juce::File::tempDirectory) })
    {
        candidate.createDirectory();
        auto file = candidate.getChildFile (name);

        if (file.replaceWithText (juce::JSON::toString (value, false)))
            return file;
    }

    return {};
}

} // namespace

//==============================================================================
juce::var redact (const juce::var& value)
{
    if (auto* obj = value.getDynamicObject())
    {
        auto* out = new juce::DynamicObject();

        for (const auto& prop : obj->getProperties())
            out->setProperty (prop.name,
                              isSensitive (prop.name.toString()) ? juce::var ("<redacted>")
                                                                 : redact (prop.value));

        return { out };
    }

    if (const auto* array = value.getArray())
    {
        juce::Array<juce::var> out;

        for (const auto& item : *array)
            out.add (redact (item));

        return out;
    }

    return value;
}

//==============================================================================
void write (Level level, const juce::String& message)
{
    auto& s = state();

    if (! initialised.load() || level < s.level)
        return;

    const auto line = nowIso() + " " + juce::String (levelName (level)).paddedRight (' ', 5)
                    + " " + s.appName + ": " + message;

    const juce::ScopedLock sl (s.lock);

    s.ring.add (line);
    while (s.ring.size() > ringCapacity)
        s.ring.remove (0);

    ensureStream (s);

    if (s.stream != nullptr)
    {
        s.stream->writeText (line + juce::newLine, false, false, nullptr);
        // Flush every line. Buffered output is lost when a crash handler
        // terminates the process, which is exactly the run whose log you
        // needed. A cue player writes a handful of lines a minute; the cost is
        // irrelevant next to losing the evidence.
        s.stream->flush();
    }

    if (level >= Level::warn)
        std::cerr << line << std::endl;
}

//==============================================================================
namespace
{

/**
    Native crash handler: SIGSEGV and friends.

    Necessarily best-effort. Allocating and touching the filesystem from a
    signal handler is not async-signal-safe, and if the heap is what got
    corrupted this will fail — which is precisely why every log line is
    flushed as it is written, so the log file alone still tells the story.
*/
void crashHandler (void*)
{
    auto& s = state();
    const auto backtrace = juce::SystemStats::getStackBacktrace();

    writeCrashReport ("native-crash", "the application stopped unexpectedly", backtrace);

    if (s.stream != nullptr)
        s.stream->flush();
}

} // namespace

juce::File writeCrashReport (const juce::String& trigger,
                             const juce::String& message,
                             const juce::String& backtrace)
{
    auto& s = state();

    if (! initialised.load())
        return {};

    auto* error = new juce::DynamicObject();
    error->setProperty ("message", message);

    juce::Array<juce::var> frames;
    for (const auto& frame : juce::StringArray::fromLines (backtrace))
        if (frame.trim().isNotEmpty())
            frames.add (frame.trim());
    error->setProperty ("backtrace", frames);

    auto* report = new juce::DynamicObject();
    report->setProperty ("schema", schema);
    report->setProperty ("kind", "crash-report");
    report->setProperty ("generated_at", nowIso());
    report->setProperty ("trigger", trigger);
    report->setProperty ("app", buildAppInfo (s));
    report->setProperty ("platform", buildPlatformInfo());
    report->setProperty ("process", buildProcessInfo (s));
    report->setProperty ("config", s.config);
    report->setProperty ("error", juce::var (error));
    report->setProperty ("recent_log", ringAsArray (s));

    const auto file = writeJson (s.dir, s.appName + "-crash-" + stampCompact() + ".json",
                                 juce::var (report));

    if (file != juce::File())
        std::cerr << std::endl
                  << s.appName << " crashed (" << trigger << "). A diagnostic report was written to:"
                  << std::endl
                  << "  " << file.getFullPathName() << std::endl
                  << "Send that file with your bug report." << std::endl
                  << std::endl;

    return file;
}

//==============================================================================
void init (const Options& options)
{
    auto& s = state();
    const juce::ScopedLock sl (s.lock);

    s.appName = options.appName;
    s.version = options.version;
    // A compiled binary cannot read its own git revision at runtime, so this
    // comes from the build. See the CMake DIAG_GIT_REV definition.
#if defined(DIAG_GIT_REV)
    s.gitRev = DIAG_GIT_REV;
#else
    s.gitRev = "unknown";
#endif
    s.dir = resolveLogDirectory (options.appName, options.envPrefix);
    s.level = levelFromName (
        juce::SystemStats::getEnvironmentVariable (options.envPrefix + "_LOG", {}),
        options.defaultLevel);
    s.startedAt = nowIso();
    s.startedMs = juce::Time::currentTimeMillis();
    s.dir.createDirectory();

    initialised.store (true);

    if (options.installCrashHandler)
        juce::SystemStats::setApplicationCrashHandler (crashHandler);

    write (Level::info,
           "logging started version=" + s.version + " git_rev=" + s.gitRev
               + " log_dir=" + s.dir.getFullPathName()
               + " level=" + levelName (s.level));
}

void shutdown()
{
    auto& s = state();
    const juce::ScopedLock sl (s.lock);

    if (s.stream != nullptr)
        s.stream->flush();

    s.stream.reset();
    initialised.store (false);
}

void setConfig (const juce::var& config)
{
    auto& s = state();
    const juce::ScopedLock sl (s.lock);
    s.config = redact (config);
}

juce::File logDirectory()
{
    return state().dir;
}

//==============================================================================
juce::File collectDiagnostics()
{
    auto& s = state();

    if (! initialised.load())
        return {};

    const juce::ScopedLock sl (s.lock);

    if (s.stream != nullptr)
        s.stream->flush();

    juce::StringArray warnings;

    juce::Array<juce::var> crashReports;
    auto reportFiles = s.dir.findChildFiles (juce::File::findFiles, false,
                                             s.appName + "-crash-*.json");
    reportFiles.sort();

    for (int i = reportFiles.size() - 1;
         i >= 0 && crashReports.size() < maxCrashReports;
         --i)
    {
        const auto parsed = juce::JSON::parse (reportFiles[i]);

        if (parsed.isVoid())
            warnings.add (reportFiles[i].getFileName() + ": could not be parsed");
        else
            crashReports.add (parsed);
    }

    juce::Array<juce::var> logs;
    auto logFiles = s.dir.findChildFiles (juce::File::findFiles, false, s.appName + ".*.log");
    logFiles.sort();

    for (int i = logFiles.size() - 1; i >= 0 && logs.size() < maxLogFiles; --i)
    {
        const auto& file = logFiles[i];
        auto allLines = juce::StringArray::fromLines (file.loadFileAsString());
        const bool truncated = allLines.size() > maxLinesPerFile;

        // Keep the tail: whatever went wrong happened at the end.
        while (allLines.size() > maxLinesPerFile)
            allLines.remove (0);

        juce::Array<juce::var> lines;
        for (const auto& line : allLines)
            lines.add (line);

        auto* entry = new juce::DynamicObject();
        entry->setProperty ("file", file.getFileName());
        entry->setProperty ("bytes", (int) file.getSize());
        entry->setProperty ("truncated", truncated);
        entry->setProperty ("lines", lines);
        logs.add (juce::var (entry));
    }

    juce::Array<juce::var> warningVars;
    for (const auto& warning : warnings)
        warningVars.add (warning);

    auto* bundle = new juce::DynamicObject();
    bundle->setProperty ("schema", schema);
    bundle->setProperty ("kind", "diagnostics-bundle");
    bundle->setProperty ("generated_at", nowIso());
    bundle->setProperty ("app", buildAppInfo (s));
    bundle->setProperty ("platform", buildPlatformInfo());
    bundle->setProperty ("process", buildProcessInfo (s));
    bundle->setProperty ("config", s.config);
    bundle->setProperty ("log_dir", s.dir.getFullPathName());
    bundle->setProperty ("crash_reports", crashReports);
    bundle->setProperty ("logs", logs);
    bundle->setProperty ("recent_log", ringAsArray (s));
    bundle->setProperty ("collection_warnings", warningVars);

    return writeJson (s.dir, s.appName + "-diagnostics-" + stampCompact() + ".json",
                      juce::var (bundle));
}

} // namespace cp::diag
