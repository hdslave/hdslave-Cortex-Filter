# Notes for coding agents

**hdslave Cortex Filter** is a stereo 3D morphing filter effect with a filter bank and filter sequencer: a CLAP plug-in for the Akai MPC Sample. It is made to run in hdslaveOS, the hdslave fork of Kerem Kiliç's Melodee. Its CLAP id is `com.hdslave.cortex-filter`, and it builds to `build/CortexFilter.clap`.

## Read the workspace guide first

On the author's machine, this repository sits in one folder beside the other hdslave projects. That folder's `../AGENTS.md` and `../Workflow/` hold the shared workflow:
- building in the Linux VM;
- what hdslaveOS does to plug-ins;
- screen pages;
- reviews;
- publishing, the hub repository and staging for the SD card.

Read `../AGENTS.md`, then `../Workflow/plugins.md` and `../Workflow/host-facts.md`, before you change anything. The rules below hold either way.

## Build and test

- `./build.sh` builds the plug-in in a Linux arm64 container (image `melodee-builder`, Colima VM `melodee`). It runs `make all test`: the host tests in `tests/host.cpp` and the sound's fingerprint. Pass make targets to run others.
- `tools/bench.sh` counts the instructions of a 128-frame block with callgrind. A block is about 4.8 M cycles on one of the MPC's Cortex-A55 cores.
- Check the build's own exit code: `./build.sh | grep …` hides a failure.

## Rules

- **Fixed for saved projects.** Parameter ids, preset keys and choice orders never change once released, because saved projects and presets refer to them.
  - Add new ones at the end.
  - Ranges stay as they are too: the host stores bindings normalized to the range.
- **The sound is held to the bit** by `tests/fingerprint.txt`.
  - A refactor or speed-up must leave every hash unchanged. With `-ffp-contract=fast`, even reshaping a loop can change the last bit, so check after every edit.
  - Only a change meant to change the sound runs `make fingerprint-update`, and its commit says which hashes moved and why.
- **Real-time and determinism.** The output is the same at every block size. No allocation, locks or file I/O on the audio thread.
- **Notes** must survive how hdslaveOS sends them:
  - every MIDI event of a block arrives at frame 0;
  - a re-pressed key arrives as on, on, off;
  - CC 120 and 123 arrive at 0 on every channel when the host silences.
- **Commits** are made as `hdslave <111106843+hdslave@users.noreply.github.com>`, with Conventional Commits. Never push, publish or stage for the card without the owner's word.
- **Other agents' work.** Others may be working here. Check `git status` and `git worktree list`, and never edit, commit or reset files you did not change.
- **Names and art.** No trademarked names in the plug-in's names, presets, UI text or art. Art is original; fonts are OFL, with their licences in `licenses/`.
- **Read the README** before you change the sound. It lists the sources, the inferred values and the deliberate deviations. Keep it current: version, costs, and "not yet run on the MPC" until it has been.

## This plug-in

- **It is an effect.** hdslaveOS sends effects no MIDI, so the sequencer runs from the song grid, an envelope trigger and gate parameters.
- **Append only:** cube numbers, bank slots and parameter ids. Id 0 (Cube) is retired, and states saved by 0.1 map it onto Filter.
- **The response curve is exact.** `responseDb` in `src/morph.h` is exact for the filter, and `tests/host.cpp` holds the measured response to it, so the page draws what is heard.
- `tests/engine.cpp` holds the engine-level checks. `tools/levels.sh`, `balance.sh` and `artifacts.sh` measure levels, preset balance and control-rate artifacts.
- Glides and gain smoothers snap once a step no longer moves the float. After a state load, the next block settles them.

## Release packaging

Every release ZIP includes the full repository `LICENSE.txt` beside the third-party notices, outside signed plug-in bundles. Preserve both when packaging the plug-in for another project.
