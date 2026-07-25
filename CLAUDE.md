# cue-player (Cue Player)

Platform-independent audio cue player for theatre and live events. Cues (audio files with
in/out points), fades, links (cue-to-cue transitions), loops and vamps. JUCE 8 / C++20,
CMake. Public repo. Phase 1 complete; Phases 2 (control protocols) and 3 (streaming
adapters) not started.

## Commands
- Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Release`
- Build: `cmake --build build -j8`
- Tests: `./build/CuePlayerTests_artefacts/<config>/CuePlayerTests`

## Architecture
- `Source/Model` — `Cue`, `CueList`, `Show` (JSON `.cueshow`), `FadeCurve`. Message thread only.
- `Source/Audio` — `AudioEngine` (device + voice pool + link scheduling), `CueVoice` (one
  sounding instance), `SampleSource`/`SampleCache` (decode + resample to device rate).
- `Source/GUI`, `Source/App` — UI and the command/menu target.

## Rules that matter here
- **The audio thread never allocates, locks, or touches a reference count.** Decoded audio is
  kept alive by a `shared_ptr` held on the message thread in `AudioEngine::records` for as
  long as the voice is not idle; the voice holds a raw pointer.
- A voice is claimed by `setSpec()` (state `reserved`) *before* its start command is queued.
  Without that, link scheduling — which runs synchronously right after — would be handed the
  same voice twice.
- Audio is resampled to the device rate **at load time**, so all loop/vamp maths is exact
  integer sample arithmetic. Changing the device rate reloads every cue.
- JUCE's `WindowedSinc` interpolator has 100 input samples of latency; `SampleSource::load`
  primes it away so resampled cues stay aligned with cues that already matched the rate.
  It also has ~1% passband gain error — that is the filter, not a bug.
- **No non-ASCII characters in string literals.** JUCE's `String(const char*)` asserts on
  them and mangles the text. Em dashes in comments are fine; in literals they are not.

## Verifying playback changes
Extend `Tests/EngineTests.cpp` rather than listening. The stimulus is a ramp whose sample
values encode their own index, so "played the right region", "played the wrong region" and
"looped a sample early" are three different numbers instead of three identical waveforms.
Measure timing with a step, not a ramp — the interpolator's gain error and its delay are
indistinguishable on a ramp.
