# Diagnostics

Three artefacts: a log an operator can read, a crash report that survives a
failure nobody was watching, and one file that can be sent afterwards.

All of it is in `Source/Diag/` and needs only `juce_core`, so it copies into
the other JUCE repos unchanged — with one caveat for plugins, below.

## Where things are written

| Platform | Directory |
| --- | --- |
| macOS | `~/Library/Logs/SimpleCue/` |
| Windows | `%APPDATA%\SimpleCue\` |
| Linux | `~/.config/SimpleCue/` |

That is `juce::FileLogger::getSystemLogFileFolder()`, which is the platform
convention on each. `SIMPLECUE_LOG_DIR` overrides it.

**Help → Open Log Folder** reveals it in the file manager, because telling an
operator to navigate to `~/Library/Logs` is telling them to give up.

## 1. The human log

`SimpleCue.YYYY-MM-DD.log`, one per day, seven kept:

```
2026-07-29T14:56:31.714+01:00 INFO  SimpleCue: show loaded cues=42 path=gala.simplecue
2026-07-29T14:56:31.714+01:00 WARN  SimpleCue: cue 17 references a missing file
```

Level comes from `SIMPLECUE_LOG` (`TRACE`…`FATAL`), default `INFO`. `WARN` and
above are also echoed to stderr.

**Every line is flushed as it is written.** Buffered output is lost when a
crash terminates the process — which is precisely the run whose log you
needed. A cue player writes a handful of lines a minute, so the cost is
nothing next to losing the evidence.

Log through the macros (`CP_LOG_INFO` and friends), not `DBG` or `std::cout`:
they take the level check before building the string, so a trace call on the
audio path costs a comparison rather than a `juce::String` construction.

## 2. The crash report

A JUCE app dies in two distinct ways and both are covered:

| Path | Hook |
| --- | --- |
| Signals — bad pointer, stack overflow | `juce::SystemStats::setApplicationCrashHandler`, installed by `diag::init` |
| An uncaught C++ `throw` from a message-loop callback | `JUCEApplication::unhandledException`, overridden in `Main.cpp` |

Without the second, JUCE terminates with no record of why.

The native handler is **necessarily best-effort**. Allocating and touching the
filesystem from a signal handler is not async-signal-safe, and if the heap is
what got corrupted it will fail. That is exactly why every log line is flushed
as it is written: when the report cannot be produced, the log file alone still
tells the story.

`SimpleCue-crash-<timestamp>.json` carries the app version and git revision,
the platform (OS, CPU model, core count, RAM, JUCE version), the redacted
config, the fault with a symbolicated backtrace, and the last 500 log lines
from an in-memory ring.

### Plugins

`Options::installCrashHandler` exists for this. A plugin lives inside someone
else's process — a DAW — and must not install a process-wide signal handler:
it would intercept crashes that are not its own and interfere with the host's
own handling. **Set it to `false` in zero-eq and resolume-luma-keyer.** They
still get the log, the ring and the diagnostics bundle.

## 3. The diagnostics bundle

**Help → Collect Diagnostics...** writes one JSON file, copies its path to the
clipboard, reveals it in the file manager and explains what to do with it.
There is also a headless form, for a support engineer on the phone:

```bash
SimpleCue --collect-diagnostics
```

It holds the identity and config blocks, the last three log files (tail-capped
at 5000 lines), the five most recent crash reports embedded whole, and
`collection_warnings` for anything unreadable.

## Redaction

Keys matching `password`, `passwd`, `passphrase`, `secret`, `token`, `apikey`,
`credential`, `auth` or `private` — case-insensitive, `-`/`_` ignored — are
replaced at any depth. Deliberately over-eager.

## Build identity

`DIAG_GIT_REV` is baked in by CMake at configure time, because a compiled
binary cannot read its own git revision at runtime. `-dirty` means the tree had
uncommitted changes, so the sha alone does not identify what was built.

## Schema

`"schema": "stoatworks.diagnostics/1"`, `kind` of `crash-report` or
`diagnostics-bundle`. Treat the schema string as the contract.

## Trying it

A crash handler that has never been fired is a guess, not a feature:

```bash
cmake --build build --target SimpleCueDiagCrash
./build/SimpleCueDiagCrash_artefacts/Debug/SimpleCueDiagCrash segv
./build/SimpleCueDiagCrash_artefacts/Debug/SimpleCueDiagCrash exception
```

Both leave a report behind. Check that `api_token` came out `<redacted>`.
