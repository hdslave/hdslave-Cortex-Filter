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
- **Sleep.** hdslaveOS skips a quiet effect's process calls altogether (no transport). Since 0.3.4 the sequence and the LFO catch up on waking: hdslaveOS from 0.8.3-hdslave.4 counts in `steady_time` every frame a plug-in is not run for (asleep, bypassed, its track frozen or empty), so process gives `Engine::slept()` the gap (steady_time minus the last call's plus its frames), the waking block's transport and frames, and whether the gap can only have been the idle gate's sleep (`gated`: input and output under -100 dB for the gate's 10 s first, counted in process as the host counts it), before anything else of the block. It steps the control steps, the clocks and the LFO as a run every block would have, up to a place the waking transport bears out: the song's place now, the place before the loop went round (`CLAP_TRANSPORT_IS_LOOP_ACTIVE`, at this block), or the place a Stop holds. Gated, hdslaveOS ran it for any transport change, so a place not borne out is a jump at this block and the clocks are counted to its own frames' place; not gated, nothing is counted then (0.3.3's behaviour). Limits: a tempo change then a locate back in one rest, more than one tempo change with the song behind both tempos told, a locate in a bypass, a long MTC-trimmed rest; glides and fades are not moved on. A host whose steady_time stands still tells no gap: 0.3.3's behaviour. `runClocks()` holds on a step back under a 64th note (jitter, not a locate), so no boundary fires twice. The envelope follower cannot wait (it cannot be settled to the bit afterwards): while it falls with something reading it (`Engine::keepAwake`), process calls `request_process` every block. `testIdleGate` in `tests/host.cpp` models the host's gate block by block (its 10 s span from the reported tail, the transport wake, steady_time through sleep and bypass, the loop; each switchable); `testSlept` in `tests/engine.cpp` holds `slept()` to a run every block.
