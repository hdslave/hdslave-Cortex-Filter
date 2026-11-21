# Cortex Filter

A stereo 3D morphing filter as a CLAP effect, built for the Akai MPC Sample running Melodee (64-bit ARM Linux,
Cortex-A55). Hosts list it as **hdslave Cortex Filter** (id `com.hdslave.cortex-filter`). It works in the style of
the "filter cube" morphing filters (E-mu's Z-plane filters, and Rossum Electro-Music's Morpheus module, which put them
in 3D with a screen that shows where you are in the cube): eight complex filters sit at the corners of a cube, and
three knobs pick a point inside it. The cubes here are original designs; no filter data or names are taken from those
instruments.

Since 0.2.0 it follows the module's whole way of working, rebuilt for the MPC: what you play through is a **filter**,
a cube with settings of its own (where the knobs' middle sits on each axis and how far they reach, fades out and in,
a level and a distortion for each side, Transform turned over to the distortion), kept in a bank of 64. A **filter
sequencer** steps through lists of filters and commands (Pause, Halt, Bumper, Fade Time Scale) on Inc, Dec and Reset
triggers: the module's trigger jacks become clocks on the song's grid, the envelope follower, three gate parameters and
the page. The factory's filters and sequences are original too.

In hdslaveOS it draws its own page on the MPC's screen: the cube in 3D with the filter's point moving through it, the
filter's response as a 3D waterfall, the module's readouts (the axes' control sums, input, distortion and output meters,
the sequence's place) and pages to edit filters and program sequences (see [The screen](#the-screen)).

## Signal flow

```
in (stereo) ──► Drive (soft saturation) ──► the filter: its cube, up to 7 bands in series, at the point
                                              Morph (X) · Freq (Y) · Transform (Z)
                                              moved by:  the knobs (glided by Glide)
                                                         + the LFO (three axes, phase-spread)
                                                         + the envelope follower (the input's level)
                                              each axis then scaled by the filter's gain and moved by its offset
                                              (or Transform's sum driving the distortion instead)
                                              Spread: the left and right channels at two points on Morph
                                              each side's distortion inside the bands, then its level
            ──► Mix with the dry input ──► Output ──► a soft limit above full scale (never past 2.0) ──► out (stereo)

the filter heard: Filter's, or the sequence's ◄── Inc · Dec · Reset ◄── clocks on the song's grid, the envelope
                                                                     follower, the Inc / Dec / Reset gates, the page
```

Each corner filter is up to seven bands in series. Each band is a state-variable filter whose low-pass, band-pass and
high-pass outputs are mixed with its input, so one band can be a peak or dip (in dB), a low or high pass, a band pass,
a notch or an all pass, or anything between. Inside the cube the corners' bands are blended (frequency and Q on a log
scale, the mix linearly, a bell's gain in dB) and the filter is built from the blend, so every point is one stable
filter and the morph moves smoothly. The point and the filter are worked out every 8 frames; gains glide every sample.

## Controls

| Control | Range | What it does |
|---|---|---|
| Filter | F01 – F64 | The filter heard while no sequence runs (below), and the one the edit filter's controls show. A change dips the filtered sound: down over the old filter's Exit Fade, the new one in with its filter emptied, up over its Enter Fade (3 ms each as the factory has them), so a ringing cube never bursts into the next |
| Morph | 0 – 100 % | X: the cube's first morph |
| Freq | 0 – 100 % | Y: in most cubes it moves the whole shape up or down in frequency (a cutoff knob for it) |
| Transform | 0 – 100 % | Z: the cube's second morph |
| Emphasis | ±100 % | Every band's Q, from half to four times as sharp; widened peaks come down as they widen, so the level holds |
| Glide | 1 ms – 2 s | How fast Morph, Freq and Transform follow their knobs |
| Drive | 0 – +24 dB | Into a soft saturator before the filter, blended in over its first 6 dB and level-compensated by half its gain; 0 dB is the input untouched |
| Mix | 0 – 100 % | The filtered sound against the dry input; 0 % with Output at 0 dB gives an input within full scale back to the bit |
| Output | −24 – +12 dB | After the mix |
| Spread | 0 – 100 % | Puts the left and right channels at different points on Morph, up to half its length apart |
| LFO Sync | Free, 8 Bars … 1/32 (with triplets) | Free runs at LFO Rate; the rest follow the song's place while it plays (the same part of the song gives the same movement) and run on at the tempo while it is stopped |
| LFO Rate | 0.02 – 20 Hz | The free LFO's rate |
| LFO Shape | Sine, Triangle, Saw Up, Saw Down, Square, S&H, Drift | S&H and Drift draw new values each cycle, the same for the same place in the song |
| LFO Phase | 0 – 360° | How far ahead of Morph's the LFO's phase is on Freq, and twice that on Transform: 0° moves all three together (a line), 90° turns it into an orbit |
| LFO > Morph, > Freq, > Transform | ±100 % | Depth on each axis: 100 % swings it half the axis each way |
| Env Gain | 0 – +24 dB | The envelope follower's input gain; it reads −48 dB to 0 dB as nothing to full |
| Env Attack | 0.1 – 200 ms | |
| Env Release | 5 ms – 2 s | |
| Env > Morph, > Freq, > Transform | ±100 % | Depth on each axis: 100 % moves it the axis's whole length at full level |

The point never leaves the cube: knobs plus modulation stop at its walls. Spread, the LFO and envelope depths and Env
Gain glide over 20 ms, as Emphasis does, so turning them moves the point smoothly. A preset glides in the same way; a
saved state (a project opening) lands at once, so it starts sounding as it was saved.

## Filters

A filter is a cube and the settings the module keeps with it. The bank holds 64 (F01 – F64): F01 – F16 are the
sixteen cubes as they are, F17 – F32 the factory's ways of using them (below), F33 – F64 empty for your own. An empty
filter is heard as Null. The **Edit Filter** controls show and change the filter **Filter** picks; choosing another
filter shows its settings. The bank, and every change to it, is saved with the project.

| Control | Range | What it does |
|---|---|---|
| Cube | Null, the 16 cubes | Null passes the sound untouched (its level still applies) |
| Morph / Freq / Transform Offset | ±100 % | Where the knobs' middle (and no modulation) puts the point: +100 % is the axis's end. With an offset the knobs alone no longer reach the whole axis |
| Morph / Freq / Transform Gain | −9.9x – +9.9x | How far the knobs and their modulation reach, around the middle: 2.0x crosses the axis in half the knob's travel, a negative gain turns it round |
| Enter Fade, Exit Fade | 1 ms – 4 s | How long the filter takes to come in, and to go, when the filter changes; a change takes the old filter's Exit plus the new one's Enter |
| Left Gain, Right Gain | −40 – +40 dB | The filter's level on each side, after it |
| Left Dist, Right Dist | −30 – +70 dB | Distortion inside the filter: the signal is driven into a saturator after every band, so how it breaks up depends on the cube. −30 dB is none at all; under 0 dB it only sets the headroom (clean until the signal inside reaches it); over 0 dB it drives into the saturators, scaled back by half as much, so it gets grittier and only a little louder |
| Xform > Dist | Off, On | On: the Transform knob, its modulation and Transform Gain (read as Dist Gain) drive the distortion, 10 dB for every 10 % of Transform from its middle, on top of Left and Right Dist; Transform sits where its offset puts it |

On the page, **Copy** puts the filter into the next empty slot (which Filter then picks) and **Restore** (pushed twice)
puts back the factory's filter, or empties one of yours.

## The sequencer

| Control | Range | What it does |
|---|---|---|
| Sequence | Off, 16 sequences | The sequence that picks the filter. Loading one (or starting the song) starts it at its first step |
| Inc Clock, Dec Clock | Off, 8 Bars … 1/32 (with triplets) | An Inc (or Dec) each time the playing song crosses that grid: on the beat, the same steps for the same part of the song. A jump or a loop into another cell of the grid counts too |
| Reset Clock | Off, 1 – 16 bars | A Reset on that grid. Inc and Dec on the same beat come first, so a sequence reset on the bar starts the bar on its first step |
| Env Trigger | Off, Inc, Dec, Reset | The envelope follower as a trigger: each time it rises through Trig Level, that input fires; it fires again once it has fallen 6 dB under it |
| Trig Level | −48 – 0 dB | |
| Inc, Dec, Reset | Low, High | Gates: each rise from Low to High fires (automation or another host's lanes); Push on the page fires them too |

A sequence is up to 32 steps, each a filter or a command, worked through as the module's manual has it:

- **Inc** and **Dec** move a step on or back, round the end, loading the filter there.
- **Pause**: Inc and Dec do nothing until a Reset, which ends the Pause and moves nothing.
- **Halt**: Inc and Dec do nothing until a Reset, which goes back to the first step.
- **Bumper**: a wall. Inc and Dec never cross it; at the first step it is a Pause already ended.
- **Fade x1/32 … x8**: scales the fades of every change after it (until the sequence starts again).
- **Reset** goes back to the first step (or ends a Pause).

The wheel on the CUBE page steps through the running sequence's filters, past its commands without their taking
effect. The clocks run only while the song plays; a sequence runs only while Sequence picks one with steps in it.

Factory sequences (S01 – S08; S09 – S16 are empty for your own; **Restore** on the page puts a factory sequence back):

| # | Sequence | Steps |
|---|---|---|
| 1 | Lowpasses | the low and band pass filters, to pick from with the wheel |
| 2 | Vowels | the vowels and wahs |
| 3 | Resonators | the combs, bells, plates and chords |
| 4 | Tone Shapers | the phasers, swirls, tubes, tilts and the rest |
| 5 | Acid Steps | eight steps between Acid Twins and its deep and bright filters, for sixteenths |
| 6 | Vowel Talk | Fade x1/4, then the slow vowels with the others |
| 7 | Bar Gate | Gate Tube and Hollow Tube in turn, then a Halt until the next Reset |
| 8 | Up And Down | five filters between two Bumpers: Inc and Dec clocks together make it wander |

## Cubes

Morph is X, Freq is Y, Transform is Z. Cubes keep their numbers forever (a filter stores its cube's number); new ones
go at the end. A host that keeps values scaled to a parameter's range (Melodee's knob bindings and recorded motion)
would read a recorded edit filter Cube differently once the list grows, so adding cubes is a change to make knowingly.
Filter's range (F01 – F64) and Sequence's (Off and 16) are fixed for the same reason. A project saved by 0.1.0 opens
with its Cube as the factory's filter for that cube, sounding as it did.

| # | Cube | Morph | Freq | Transform |
|---|---|---|---|---|
| 1 | Low To Band | low pass to band pass | cutoff, 31 Hz – 16 kHz | gentle 2-pole to resonant 4-pole |
| 2 | High To Band | high pass to band pass | cutoff | gentle to resonant |
| 3 | Vowels A E O U | A to E (O to U at the back) | the voice an octave down to an octave up | A/E to O/U |
| 4 | Vowels I U E A | I to U (E to A) | as above | |
| 5 | Phase Drift | a phaser's notches close together to far apart | sweeps them | sharpens them |
| 6 | Harmonic Comb | peaks on every harmonic to the odd ones | the root, 27 – 880 Hz | peaks to dips |
| 7 | Wah Talk | a wah to a three-peak talking mouth | the pedal, 300 Hz – 2.2 kHz | sharper |
| 8 | Bell Tower | ringing harmonics to a church bell's partials | pitch | rings longer |
| 9 | Radio Tuner | full range to a telephone band | moves the band | adds a honk and steep edges |
| 10 | Acid Twins | squelchy resonant low pass to resonant band pass | cutoff | splits the peak in two, a fifth apart |
| 11 | Hollow Tube | uneven notches to harmonic ones | moves them | adds peaks between them |
| 12 | Swirl | five peaks turning like a barber's pole (four climb, the top one falls through them) | shifts them | peaks to dips |
| 13 | Metal Plate | a struck plate's modes to a bar's | pitch | rings longer |
| 14 | Tilt Air | a tone control: dark to bright | shifts it | scoops the mids |
| 15 | Sub Thump | a sub boom to a punchy mid-bass with the boxiness cut | shifts it | resonates |
| 16 | Chord Bank | resonators on a major chord to a minor one | the root, 55 – 440 Hz | rings longer |

The cubes' levels are set so moving through one keeps its loudness: the resonating cubes' corners lose about 4 dB
on pink noise, and no point of any cube peaks above +18 dB anywhere up to 0.45 of the rate (`tools/levels.sh` prints
each corner's level and peak; the engine tests hold these at 44.1 and 48 kHz). A band that Freq pushes past 0.45 of
the rate stays there, its peak fading flat over the next half octave, so peaks never pile up at the top. Emphasis at −100 % stays within about 3.5 dB of the cube's level at 0; at +100 % the
resonant low and high pass cubes get louder, as resonance does (up to about 7 dB on Acid Twins).

## Factory filters

F01 – F16 are the cubes 1 – 16 as they are. The rest, each within about 1.5 dB of its cube's loudness:

| # | Filter | What it is |
|---|---|---|
| 17 | Deep Acid | Acid Twins, Freq −30 %, +6 dB of distortion |
| 18 | Bright Acid | Acid Twins, Freq +30 % |
| 19 | Vowel Crush | Vowels A E O U with Transform on the distortion |
| 20 | Fuzz Wah | Wah Talk, +12 dB of distortion |
| 21 | Wide Sweep | Low To Band, Freq Gain 2.0x |
| 22 | Narrow Band | High To Band, Morph +60 % |
| 23 | Bell Crush | Bell Tower with Transform on the distortion |
| 24 | Phase Invert | Phase Drift, Morph turned round |
| 25 | Slow Chords | Chord Bank, 250 ms fades |
| 26 | Slow Vowels | Vowels I U E A, 250 ms fades |
| 27 | Gate Tube | Hollow Tube, in at once, out over 80 ms |
| 28 | Telephone | Radio Tuner held at the telephone end of Morph |
| 29 | Sub Crush | Sub Thump, +18 dB of distortion |
| 30 | High Metal | Metal Plate, Freq +40 % |
| 31 | Split Swirl | Swirl, clean on the left, +6 dB of distortion on the right |
| 32 | Bypass | Null |

## Presets

Fifteen factory presets, inside the plug-in: Melodee lists them on the plug-in's PRESETS page (any host with CLAP preset
discovery does). A preset sets every control but the edit filter's: it picks a filter and a sequence from the bank as
it is, and leaves the bank alone. Each one's Output is set so it plays about as loud as the dry input (1 to 2 dB under
it on a loudness meter's weighting, `tools/balance.sh`).

| Preset | What it is |
|---|---|
| Init | Low To Band, still |
| Talking Loop | Vowels A E O U, a one-bar LFO talking across Morph and Transform |
| Phase Drift | The phaser, slowly sweeping (free LFO on Freq) |
| Envelope Wah | Wah Talk opened by the input's level (Env > Freq) |
| Harmonic Swirl | Harmonic Comb, a half-bar triangle LFO on Morph, the two sides apart (Spread) |
| Bell Ringer | Bell Tower with a little Drive, part dry |
| Acid Envelope | Acid Twins, Emphasis up, Drive, the cutoff following the input |
| Radio Drift | Radio Tuner wandering (Drift LFO on Freq and Transform) |
| Wide Space | Swirl orbiting over four bars, the two sides apart |
| Chord Glow | Chord Bank turning between major and minor over four bars, part dry |
| Acid Sequence | Acid Steps on sixteenths, back to its first step each bar, the cutoff following the input |
| Vowel Walk | Vowel Talk on eighths, two bars long, a one-bar LFO on Morph |
| Hit Steps | The resonators, a step on each hit (the envelope as Inc), part dry |
| Bar Gate | Bar Gate on eighths, reset each bar |
| Wander | Up And Down with Inc on quarters and Dec on quarter triplets: a walk between its walls |

## The screen

In hdslaveOS (hdslave's Melodee fork, its `hdslave` branch), Cortex Filter draws its own page in place of the
parameter list. In stock Melodee or another host, the parameters are listed as usual.

- **CUBE**, the main display. On the left, the cube in 3D, swaying slowly while sound runs through it: the filter's
  point glowing (the right channel's in magenta when Spread splits them), its trail, the path the LFO will take
  (dotted), lines dropped from the point to the back walls, and the eight corners lit by how much of each the filter
  takes. Above it, as the module shows them: F, M and T (D when Transform drives the distortion), the knobs and
  modulation summed on each axis before the filter scales them, and each side's input, distortion and output meters
  (green, yellow from −6 dBFS, red past full scale; the distortion's red once its saturators bend). On the right, the
  filter's response (20 Hz – 20 kHz, −36 to +18 dB) in 3D: in **TIME** view the last moments of it, newest in front,
  receding into the past; in **MORPH** view the whole Morph axis at the current Freq and Transform, with the response
  now lit where it sits. Every response drawn is worked out from the same numbers the sound uses, and the tests hold
  the sound to it within 0.05 dB. The wheel loads the next filter in use (with a sequence running, the sequence's next
  filter), Push changes the view, K1-K3 turn Morph, Freq and Transform.
- **FILTER** edits the filter Filter picks, with **Copy** and **Restore** at the end.
- **SEQ** programs the sequence Sequence picks: seven of its steps round the cursor, the one playing marked (P or H
  while it waits). The wheel moves along the steps; Push edits one (the wheel picks a filter, a command, Insert Step,
  which puts a Pause before it, or Delete Step; Minus / Plus jump eight; Push sets it); after the steps, **+ Add Step**
  adds the filter Filter picks, and **Restore** (pushed twice) puts the factory's sequence back. K1-K3 turn Sequence,
  Inc Clock and Reset Clock.
- **TRIG**: the sequencer's clocks and triggers, with the sequence's steps beside them as squares. Push on Inc, Dec or
  Reset fires it (and asks Melodee to run the sound, so a sleeping effect takes it).
- **KNOBS, LFO, ENV, OUT**: the controls, the cube small beside them with the response under it.

The header names the filter heard and the sequence's place (its step, or −P− and −H−, and a Fade scale other than x1),
lit yellow for a moment by each trigger. While no sound runs Melodee lets the effect sleep:
the page stops moving and shows where the knobs put the point, as far as the plug-in has been told them. Changes made
on the page show at once; a K1-K3 turn shows once Melodee passes it to the plug-in. MORPH view's landscape still
catches up with a change while the sound sleeps.

On a list the controls are those of hdslave's other pages: the wheel moves a row; Push edits it (the wheel then changes
it: a choice a step, a knob 1/128, 1/1024 with Shift; Push again leaves); Minus / Plus change group (while editing,
move the value a sixteenth); K1-K3 turn the cursor's row and the two under it, with Melodee's takeover. Hold Shift
for these.

## Cost

Instructions a 128-frame block takes, from `tools/bench.sh` (callgrind, the MPC's 1.8 GHz in-order core at about one
instruction a cycle):

| Case | Instructions | Share of one A55 core |
|---|---|---|
| Worst: a seven-band cube, both sides apart, a fast LFO on all three axes, the envelope, Drive | about 102,000 | 2.1 % |
| The same with the filter's distortion on both sides | about 129,000 | 2.7 % |
| The same distorted, Acid Steps stepping every sixteenth | about 78,000 | 1.6 % |
| Typical: a five-band cube moved by the LFO | about 43,000 | 0.9 % |
| Standing still | about 39,000 | 0.8 % |

The filter bank and the sequencer add about 3,000 – 4,000 instructions a block to 0.1.0's figures; the sound of the
factory's F01 – F16 is 0.1.0's to the bit (the fingerprints below). The page's input and output meters are worked
out only while Melodee paints the page.

The left and right channels run side by side in one vector register, through a loop unrolled for the cube's band
count. While the point moves, the filter is worked out again every 8 frames, with polynomial `exp2` and `tan`
(accurate to about 1e-6) and the corners blended four at a time; standing still, never. Those steps add images of a
tone about 80 dB under it while an LFO sweeps vowels (`tools/artifacts.sh`); only the most extreme moves (a 20 Hz LFO,
a 0.1 ms envelope attack, both crossing octaves in milliseconds) add a few dB to the high end the move makes anyway.

A frame of the CUBE page takes about 560,000 instructions (about 0.31 ms) on Melodee's screen thread in TIME view,
about 660,000 in MORPH view while the point moves (its landscape follows a few slices a frame), at most about 30
frames a second while it moves and none while nothing does.

## Build and test

Needs Docker with a Linux arm64 context (Melodee's `colima-melodee` and its `melodee-builder` image by default).

```sh
./build.sh          # build/CortexFilter.clap, then the tests, the sound's fingerprint, demo renders and screen pictures
./build.sh bench    # build/bench
tools/bench.sh      # instruction counts (Melodee's melodee-profiler image)
tools/levels.sh     # each cube's corner levels and loudest peak
tools/balance.sh    # how loud the presets, cubes, Emphasis and Drive are against the dry input
tools/artifacts.sh  # what the 8-frame control steps add to a tone under a moving filter (against a step every frame)
```

- `tests/engine.cpp` checks the engine from the inside: the cubes' data, the blend, every point of every cube stable at
  three rates and both ends of Emphasis, the levels, the response maths against textbook filters and against the
  filter's own measured impulse, the LFO (shapes, rate, in step with the song, stopped, jumps, phase spread), the
  envelope follower, Glide, Spread, the walls, Mix 0, mono, the limit, the times at 44.1, 48 and 96 kHz (LFO rate,
  Glide, envelope release, the gains' 20 ms, the synced LFO), texts at the units' edges reading back to themselves,
  the page without a host (and its MORPH
  landscape: whole after a new cube, caught up within four frames of a move), the presets; the sequencer's rules
  (Inc and Dec round the end, Pause, Halt, Bumper, a Bumper or Halt first, Fade, the wheel past commands, steps put in
  and taken out), its clocks on the song's grid (none at the start, Inc before Reset on the same beat, stopped and
  started again), the gates' rising edges, the envelope as a trigger, the fades' times and their scaling; the filters'
  settings (Null giving the input to the bit, each side's gain, offsets and gains moving the point, Transform > Dist
  sounding as the plain filter at its bottom, distortion on one side leaving the other to the bit, bounded on loud
  noise); the factory's bank; and the review's fixes,
  each against its failure: the right side correct after Spread goes to 0 and back across a cube change, an infinite
  and a NaN input sample leaving every output a number and the sound back within a second, Drive leaving 0 dB without
  a click on a loud tone, and a cube change while a resonator rings dipping to silence with no burst.
- `tests/host.cpp` loads the plug-in through the CLAP API: description and ports, every parameter's text there and
  back, state (and older, foreign and cut-off states; 0.1.0's Cube loading as its factory filter; the bank and the
  sequences saved and loaded back to the byte, a sequence programmed on the page; a state loaded into a running
  plug-in sounding to the bit as one loaded before activation), presets as preset discovery lists them (the edit
  filter's controls showing the filter each picks); the measured response
  against the screen's for every cube at several points, Emphasis settings and three rates (within 0.05 dB); the
  Butterworth low and high pass measured with no help from the plug-in's maths; a vowel's formant peaks; the same
  output to the bit in blocks of 1, 3, 8, 64, 128, 333, 1024 and 4096 frames, the sequencer running (clocks, the
  envelope, a gate, edit filter changes); Mix 0 giving the input back to the bit;
  a mono input, no input, the same buffers in and out; random settings changed every 64 frames on loud noise staying
  finite and under 2, then ringing down to nothing; and the page (it draws, follows the sound and the knobs, animates
  only while sound runs, drawing a new frame every 40 ms of it, its controls set the values the host hears, the wheel
  loading filters, an edit kept in its filter, Copy and Restore, a step edited on SEQ, Inc pressed on TRIG asking
  for the sound to run, MORPH view catching up by itself while the sound sleeps). It writes the presets through a test loop to
  `build/demo-*.wav` (`demo-dry.wav` is the loop itself, `demo-every-cube.wav` sweeps each cube in turn) and the page
  to `build/screen-*.png`.
- `tests/fingerprint.cpp` renders 128 fixed scenarios (each preset, each cube at three points, random settings changed
  as it plays, other rates, other block sizes, the song stopping and jumping under a synced LFO; from 0.2.0 the
  factory's filters F17 – F32 at three points, a sequence under the stopping and jumping song, gates and the envelope
  moving a sequence, the edit filter changed as it plays) and hashes every
  output sample against `tests/fingerprint.txt`: work meant to keep the sound, such as a speed-up, must leave every
  hash as it is. The other block sizes must also hash as 128 frames do, whatever the file says. A change meant to change the sound updates them with `make fingerprint-update` (inside the build
  image).

It has also been opened by Melodee's own CLAP host (hdslaveOS's `clap_check`: an effect, 46 parameters, stereo
in and out, 15 presets, plays and restores its state) and its page drawn by Melodee's `--melodee-plugin-screen`
(the wheel loading filters, K1 picking a sequence, SEQ, FILTER and TRIG naming their K1-K3). It has not yet been run
on the MPC.

## Install

Copy `build/CortexFilter.clap` to `Melodee/Plugins/hdslave Cortex Filter/hdslave Cortex Filter.clap` on the card, then
add it to a track's effects. It needs only libc and libm, glibc 2.35 or newer (the MPC has 2.39).

## Credits

- The screen's type: Departure Mono, copyright 2022–2024 Helena Zhang, SIL Open Font License 1.1
  (`licenses/OFL-departure-mono.txt`); its glyphs are rasterized into `src/ui/font.h`.
- The screen extension's header, `src/melodee_screen.h`, is hdslaveOS's, MIT.
- CLAP headers: MIT license, `third_party/clap/LICENSE`.
- The vowel formants are the standard published tenor values (five formants' frequency, level and bandwidth).
