# Cue Player

> **AI-assisted project.** This codebase was created with [Claude Code](https://claude.com/claude-code)
> (Anthropic), directed and reviewed by a human author. The playback engine is verified
> numerically — a console harness pushes a ramp signal, whose sample values encode their own
> index, through the real `CueVoice` and checks in/out points, fades, loops, vamps, routing
> and crossfade timing sample by sample (130 checks, all passing). It has **not** yet been
> run on a live show, and only the macOS/CoreAudio build has been exercised on real hardware.

A platform-independent audio cue player for theatre, live events and installation, built
around cues, fades, links, loops and vamps.

- **Cues** — an audio file with adjustable in and out points, gain and pre-wait.
- **Fades** — automatic fade-in and fade-out with five curve shapes.
- **Links** — how a cue hands over to the next: auto-continue, auto-follow, or a crossfade.
- **Loops** — repeat a cue a set number of times, or forever.
- **Vamps** — circle a section of a cue until the operator calls for it to continue.
- **Routing** — a per-cue crosspoint matrix onto any of the device's output channels.

## Audio backends

| Platform | Backends |
|---|---|
| macOS | CoreAudio (always), JACK if its headers are installed |
| Windows | WASAPI shared, WASAPI exclusive, DirectSound, and **ASIO** when built against the SDK |
| Linux | ALSA, and JACK when its headers are installed |

PipeWire needs no backend of its own on Linux — it is reached through its ALSA and JACK
compatibility layers and appears there as an ordinary device.

### ASIO

The Steinberg ASIO SDK licence forbids redistribution, so it is not vendored here. Download
it from [Steinberg](https://www.steinberg.net/developers/) and point CMake at the unpacked
folder:

```bash
cmake -B build -DCUEPLAYER_ASIO_SDK_PATH=C:/SDKs/asiosdk_2.3.3
```

Without it, Windows builds still get WASAPI (shared and exclusive) and DirectSound.

## Building

Requires CMake 3.22+ and a C++20 compiler. JUCE 8.0.6 is fetched automatically.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
```

On Linux you will also need the usual JUCE dependencies:

```bash
sudo apt install libasound2-dev libjack-jackd2-dev libfreetype6-dev libfontconfig1-dev libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev libxrender-dev libcurl4-openssl-dev
```

## Running the engine tests

```bash
./build/CuePlayerTests_artefacts/Release/CuePlayerTests
```

## Keyboard

| Key | Action |
|---|---|
| `Space` | GO — fire the standby cue |
| `Return` | Release every vamp |
| `Esc` | PANIC — instant silence |
| `Cmd/Ctrl` + `.` | Stop all, with a two-second fade |
| `Cmd/Ctrl` + `P` | Pause / resume |
| `'` | Audition the selected cue |
| `Cmd/Ctrl` + `Return` | Standby the selected cue |
| `Cmd/Ctrl` + `↑` / `↓` | Move the selected cue |
| `Shift` + `Cmd/Ctrl` + `↑` / `↓` | Step the standby marker |
| `Cmd/Ctrl` + `E` | Add audio cue |
| `Cmd/Ctrl` + `,` | Audio setup |

Drag audio files onto the window to add them as cues; drag a `.cueshow` file to open it.

## How playback works

Audio is decoded to memory and resampled to the device's rate when a cue is loaded, so
every seek, loop and vamp boundary is exact integer sample arithmetic at run time and a GO
never waits on a disk. The cost is RAM — roughly 11.5 MB per stereo minute at 48 kHz — and
the loaded total is shown in the status bar.

Links are scheduled in samples, not by a UI timer. When a cue is fired, the length of what
it will play is usually known, so the cue it links to starts immediately with a pre-wait
measured in samples, which makes auto-follows and crossfades sample-accurate. Only cues
with no determinate end — an infinite loop, an unreleased vamp, a streaming cue — fall back
to the message-thread timer, because nothing can predict when those finish.

## Streaming services

Cue Player can drive **Spotify, TIDAL, Apple Music and YouTube Music** for background music
and playlists, with an important caveat:

> **No streaming service will hand a desktop application decrypted audio.** DRM (Widevine)
> prevents it, libspotify has been dead since 2015, and the Web Playback SDK is
> browser-only. So a streaming cue never plays *through* our decoder.

Two ways to work with that, both modelled on the cue:

1. **Local capture (recommended)** — point the service's desktop app at a loopback device
   (BlackHole on macOS, VB-Cable or VoiceMeeter on Windows, a PipeWire/JACK sink on Linux)
   and open that device's inputs in Cue Player. The audio then runs through the normal
   voice path, so fade curves, gain and the routing matrix behave exactly as they do for a
   file cue. Enable inputs in **Audio setup** first.
2. **Remote device** — the service plays on one of its own Connect devices and we send only
   transport and volume commands. Fades go through the service's volume endpoint: coarse,
   network-latent, and never touching our routing matrix.

Each service needs you to register your own developer application and supply its client ID;
Spotify Connect control also requires Premium. The provider adapters are Phase 3 — the cue
type, audio path and capture routing are modelled and persist today, and the transport hook
(`AudioEngine::streamingTransport`) is in place for them to plug into.

Whether streaming-service audio may be played to an audience is a licensing question
between you and the service (and your local performing-rights body), not a technical one.

## Status

Phase 1 is complete: engine, cue model, show file, cue list, inspector, waveform editor,
device selection and channel routing.

Not yet built:

- **Phase 2 — control surface.** OSC, MIDI in (MSC / note / MMC), MIDI and OSC output on
  cues, Art-Net and sACN triggering, and a Companion-facing surface.
- **Phase 3 — streaming provider adapters.** OAuth PKCE and transport for the four services
  above.
- Disk streaming for very long files (everything is memory-resident today).
- Undo/redo.

## Licence

MIT — see [LICENSE](LICENSE).
