# SimpleCue interfaces

SimpleCue has two public interfaces:

1. **Control protocols** — OSC, MIDI, MSC, MMC, Art-Net and sACN, in and out.
   **See [`control.md`](control.md)** — that is the complete reference and this document does
   not duplicate it.
2. **The `.cueshow` file format** — documented below.

---

# The `.cueshow` format

A show is a single JSON file. It is plain text on purpose: readable, diffable, and
recoverable by hand if something goes wrong at a venue.

## Root object

```json
{
  "format": "simplecue-show",
  "version": 1,
  "masterGainDb": 0.0,
  "defaultFadeInTime": 0.0,
  "defaultFadeOutTime": 0.0,
  "defaultFadeShape": "<curve name>",
  "cues": [ ... ]
}
```

| Key | Type | Meaning |
|---|---|---|
| `format` | string | `"simplecue-show"` |
| `version` | int | Format version. Currently **1**. |
| `masterGainDb` | number | Show master gain, dB |
| `defaultFadeInTime` | number | Seconds, applied to new cues |
| `defaultFadeOutTime` | number | Seconds |
| `defaultFadeShape` | string | Default fade curve name |
| `cues` | array | The cue list |

### Compatibility rules

- **`"cue-player-show"` is also accepted** on load. That's the pre-rename format name, kept
  deliberately so old shows still open. It is not a stale reference — don't remove it.
- **A file whose `version` is greater than the running build's is refused**, rather than
  being loaded with unknown fields silently dropped. A newer show opened in an older
  SimpleCue would otherwise appear to load and then behave differently from what the operator
  built.
- Audio-file paths are stored **relative to the show file's directory**, so a show plus its
  audio folder can be moved or copied to another machine as a unit.

### Saving is atomic

The show is written to a sibling `.tmp` file and then moved into place, so a failure
part-way through cannot destroy the show already on disk. At a venue this file is often the
only copy.

---

## Cue object

| Key | Type | Meaning |
|---|---|---|
| `id` | string | Stable internal identifier |
| `number` | string | **Operator-facing cue number — free text.** `"12"`, `"12.5"`, `"PRE"` |
| `name` | string | Cue name |
| `notes` | string | Operator notes |
| `audioFile` | string | Path, relative to the show file |
| `startTime` | number | In point, seconds |
| `endTime` | number | Out point, seconds |
| `fileDuration` | number | Cached source length |
| `fileChannels` | int | Cached source channel count |
| `fileSampleRate` | number | Cached source rate |
| `gainDb` | number | Cue gain, dB |
| `preWait` | number | Seconds to wait before sounding |
| `fadeInTime` / `fadeOutTime` | number | Seconds |
| `fadeInShape` / `fadeOutShape` | string | Curve name (five shapes available) |
| `loopEnabled` | bool | |
| `loopCount` | int | `0` means loop forever |
| `vampEnabled` | bool | |
| `vampStart` / `vampEnd` | number | Vamp region, seconds |
| `vampRelease` | — | How the vamp is released |
| `endAction` | string | What happens at the end |
| `endFadeTime` | number | Seconds, default `3.0` |
| `firePlayWithCue` | bool | Default **true** — see below |
| `link` | object | Cue-to-cue handover |
| `routing` | array | Output routing matrix |
| `outputMessages` | array | Control messages this cue emits |
| `streaming` | object | Streaming settings |

`number` is free text because real shows use `12.5` and `PRE`, not just integers. **Control
protocols address cues by `number`** — with one exception, see below.

### `link` — how a cue hands over to the next

| Key | Meaning |
|---|---|
| `type` | The handover kind |
| `target` | Target cue. **Null means "the next cue in the list"** |
| `delay` | Seconds. **Ignored by crossfade.** |
| `duration` | Crossfade length, seconds |

Link types cover firing the next cue immediately, firing it at the end of this one, and
crossfading into it.

### `routing` — the output matrix

An array of route points:

```json
{ "src": 0, "dst": 0, "gain": 1.0 }
```

`src` is a channel of the source file, `dst` an output channel of the device, `gain` linear.
A cue with no explicit routing gets a sensible default derived from the file's channel count
and the device's output count.

### `firePlayWithCue`

When **true** (the default), firing the cue also plays it, and standby skips the Play
sub-cue after the header — so the same audio is never offered to the operator twice.

---

## Cue steps are derived, never stored

A cue expands into the steps GO walks through — **Play** always, **Devamp** only if the cue
has a vamp, **Fade/Stop** always. Those steps are **computed at runtime** and never written
to the file.

This matters if you are generating `.cueshow` files: **do not try to author a step list.**
Set the cue's properties and the steps follow. It also means removing a vamp automatically
removes its Devamp step, with no stale state to clean up.

---

## Generating show files externally

Reasonable, and the format is stable — but:

- Set `"format": "simplecue-show"` and `"version": 1`.
- Keep audio paths **relative to the show file**.
- Don't invent step lists (above).
- `loopCount: 0` means forever, not "no loops" — use `loopEnabled: false` for that.
- Cue `number` values are matched as text by the control protocols; keep them consistent
  with whatever your lighting desk or QLab-equivalent will send.
