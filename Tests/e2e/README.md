# End-to-end control tests

These drive a **running** SimpleCue over real UDP sockets and read its status feed back,
so they exercise the whole path — socket, parser, action dispatch, engine, feedback —
rather than the parsers that `SimpleCueTests` already covers on their own.

They are not wired into CMake or CI, because they need an app instance with the right
control settings and a free pair of ports. Run them by hand when changing the control
layer.

## Setting up

Launch SimpleCue, open **Control setup**, and set:

- **OSC** — listen on port `53000`, feedback enabled, with one target at `127.0.0.1:53099`
- **DMX** (for the Art-Net test) — Art-Net enabled, universe `1`, start address `1`

## Running

```bash
python3 Tests/e2e/osc_e2e.py
```

```bash
python3 Tests/e2e/artnet_e2e.py
```

Both print one line per check and exit non-zero on failure. No third-party packages: the
OSC and Art-Net packets are built byte by byte, which also keeps them honest — a mistake in
the app's parser cannot be cancelled out by a matching mistake in a shared library.

The Art-Net test uses the **master level** channel as its observable rather than a trigger
channel. A trigger only proves that something arrived; a continuous level proves the frame
was received, parsed, mapped and applied with the right value.
