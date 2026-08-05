# Attributions

SimpleCue is built on other people's work. This file lists what that work is, who did
it, and what it is doing here.

It is generated — the master lists live in the `stoatworks-backend` repo and are
pushed out by `scripts/sync-attributions.py`. Edit it there, not here.

## Licensing: The source here is MIT. The released binaries are not.

JUCE's modules are licensed **AGPLv3 or commercially**, with no permissive option. Nothing here is licensed commercially, so the AGPLv3 arm is the one that applies — and that makes every binary in this repo's Releases (VST3, AU, standalone, installer) a **combined work conveyed under the AGPLv3**, not under MIT.

There is no conflict between the two: MIT code may be combined into an AGPL work, which is exactly what happens at link time. But the distinction changes what you may do:

- **Using the plugin** — nothing changes. Install it and run it on any show, commercial or not. The AGPL constrains distribution, not use.
- **Reusing this repo's source** — MIT, exactly as `LICENSE` says. If you hold a JUCE commercial licence you can build this source into a closed product.
- **Redistributing our binaries, or shipping something derived from them** — the AGPLv3 applies, including making the corresponding source available. That source is this repo at the tagged commit plus JUCE at the pinned tag, both public.

AGPLv3 §13, the network clause that distinguishes the AGPL from the plain GPL, does not bite here: this is a desktop plugin and nobody interacts with it over a network.

Note also that building VST3 pulls Steinberg's VST3 SDK in through JUCE, and that SDK is itself proprietary-or-GPLv3. Under the AGPL route it is the GPLv3 arm that applies.

This section states what these licences already require. It is not an extra restriction, and it is not legal advice.

## Third-party code this project uses

Libraries, SDKs and frameworks the project is built on or bundles.

### JUCE 8

<https://juce.com>  
Licence: AGPLv3 or commercial (JUCE 8 licensing terms)  
Copyright: Raw Material Software Limited

Fetched at configure time by CMake FetchContent, pinned to tag 8.0.6. Not vendored in the repo.

The audio plugin and application framework — format wrappers, DSP primitives and the GUI toolkit that make one codebase build as VST3, AU and a standalone app.

## Getting this wrong

If your work is here and the description is inaccurate, the licence is wrong, or you would rather not be listed — open an issue and it will be fixed.
