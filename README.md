# Cortex Filter

A stereo 3D morphing filter as a CLAP effect, built for the Akai MPC Sample running Melodee (64-bit ARM Linux,
Cortex-A55). Hosts list it as **hdslave Cortex Filter** (id `com.hdslave.cortex-filter`). It works in the style of
the "filter cube" morphing filters (E-mu's Z-plane filters, and Rossum Electro-Music's Morpheus module, which put them
in 3D with a screen that shows where you are in the cube): eight complex filters sit at the corners of a cube, and
three knobs pick a point inside it. The cubes here are original designs; no filter data or names are taken from those
instruments.

On Melodee's screen it draws its own page: the cube in 3D with the filter's point moving through it, and the filter's
response as a 3D waterfall (see [The screen](#the-screen)).

## Signal flow

```
in (stereo) ──► Drive (soft saturation) ──► the cube's filter, up to 7 bands in series, at the point
                                              Morph (X) · Freq (Y) · Transform (Z)
                                              moved by:  the knobs (glided by Glide)
                                                         + the LFO (three axes, phase-spread)
                                                         + the envelope follower (the input's level)
                                              Spread: the left and right channels at two points on Morph
            ──► Mix with the dry input ──► Output ──► a soft limit above full scale (never past 2.0) ──► out (stereo)
```

Each corner filter is up to seven bands in series. Each band is a state-variable filter whose low-pass, band-pass and
high-pass outputs are mixed with its input, so one band can be a peak or dip (in dB), a low or high pass, a band pass,
a notch or an all pass, or anything between. Inside the cube the corners' bands are blended (frequency and Q on a log
scale, the mix linearly, a bell's gain in dB) and the filter is built from the blend, so every point is one stable
filter and the morph moves smoothly. The point and the filter are worked out every 8 frames; gains glide every sample.

## Controls

| Control | Range | What it does |
|---|---|---|
| Cube | 16 cubes | Which eight filters (below). A change dips the filtered sound for about 6 ms (down, the new cube with its filter emptied, up), so a ringing cube never bursts into the next |
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

## Cubes

Morph is X, Freq is Y, Transform is Z. Cubes keep their numbers forever (saved projects store the number); new ones
go at the end. A host that keeps values scaled to a parameter's range (Melodee's knob bindings and recorded motion)
would read a recorded cube differently once the list grows, so adding cubes is a change to make knowingly.

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

## Presets

Ten factory presets, inside the plug-in: Melodee lists them on the plug-in's PRESETS page (any host with CLAP preset
discovery does). A preset sets every control. Each one's Output is set so it plays about as loud as the dry input
(1 to 2 dB under it on a loudness meter's weighting, `tools/balance.sh`).

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

## The screen

In Melodee with its plug-in screen (melodee-hdslave's `plugin-screen` branch), Cortex Filter draws its own page in
place of the parameter list. In another host, or a Melodee without that branch, the parameters are listed as usual.

- **CUBE**, the readout. On the left, the cube in 3D, swaying slowly while sound runs through it: the filter's point
  glowing (the right channel's in magenta when Spread splits them), its trail, the path the LFO will take (dotted),
  lines dropped from the point to the back walls, and the eight corners lit by how much of each the filter takes. On
  the right, the filter's response (20 Hz – 20 kHz, −36 to +18 dB) in 3D: in **TIME** view the last moments of it,
  newest in front, receding into the past; in **MORPH** view the whole Morph axis at the current Freq and Transform,
  with the response now lit where it sits. Every response drawn is worked out from the same numbers the sound uses,
  and the tests hold the sound to it within 0.05 dB. The wheel steps through the cubes, Push changes the view, K1-K3
  turn Morph, Freq and Transform.
- **FILTER, LFO, ENV, OUT**: the controls, the cube small beside them with the response under it.

The header shows the cube and where the point is (X, Y, Z in %). While no sound runs Melodee lets the effect sleep:
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
| Worst: a seven-band cube, both sides apart, a fast LFO on all three axes, the envelope, Drive | about 99,000 | 2.1 % |
| Typical: a five-band cube moved by the LFO | about 40,000 | 0.8 % |
| Standing still | about 35,500 | 0.7 % |

The left and right channels run side by side in one vector register, through a loop unrolled for the cube's band
count. While the point moves, the filter is worked out again every 8 frames, with polynomial `exp2` and `tan`
(accurate to about 1e-6) and the corners blended four at a time; standing still, never. Those steps add images of a
tone about 80 dB under it while an LFO sweeps vowels (`tools/artifacts.sh`); only the most extreme moves (a 20 Hz LFO,
a 0.1 ms envelope attack, both crossing octaves in milliseconds) add a few dB to the high end the move makes anyway.

A frame of the CUBE page takes about 470,000 instructions (about 0.26 ms) on Melodee's screen thread in TIME view,
about 560,000 in MORPH view while the point moves (its landscape follows a few slices a frame), at most about 30
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
  landscape: whole after a new cube, caught up within four frames of a move), the presets; and the review's fixes,
  each against its failure: the right side correct after Spread goes to 0 and back across a cube change, an infinite
  and a NaN input sample leaving every output a number and the sound back within a second, Drive leaving 0 dB without
  a click on a loud tone, and a cube change while a resonator rings dipping to silence with no burst.
- `tests/host.cpp` loads the plug-in through the CLAP API: description and ports, every parameter's text there and
  back, state (and older, foreign and cut-off states; a state loaded into a running plug-in sounding to the bit as
  one loaded before activation), presets as preset discovery lists them; the measured response
  against the screen's for every cube at several points, Emphasis settings and three rates (within 0.05 dB); the
  Butterworth low and high pass measured with no help from the plug-in's maths; a vowel's formant peaks; the same
  output to the bit in blocks of 1, 3, 8, 64, 128, 333, 1024 and 4096 frames; Mix 0 giving the input back to the bit;
  a mono input, no input, the same buffers in and out; random settings changed every 64 frames on loud noise staying
  finite and under 2, then ringing down to nothing; and the page (it draws, follows the sound and the knobs, animates
  only while sound runs, drawing a new frame every 40 ms of it, its controls set the values the host hears, MORPH
  view catching up by itself while the sound sleeps). It writes the presets through a test loop to
  `build/demo-*.wav` (`demo-dry.wav` is the loop itself, `demo-every-cube.wav` sweeps each cube in turn) and the page
  to `build/screen-*.png`.
- `tests/fingerprint.cpp` renders 72 fixed scenarios (each preset, each cube at three points, random settings changed
  as it plays, other rates, other block sizes, the song stopping and jumping under a synced LFO) and hashes every
  output sample against `tests/fingerprint.txt`: work meant to keep the sound, such as a speed-up, must leave every
  hash as it is. The other block sizes must also hash as 128 frames do, whatever the file says. A change meant to change the sound updates them with `make fingerprint-update` (inside the build
  image).

It has also been opened by Melodee's own CLAP host (melodee-hdslave's `clap_check`: an effect, 23 parameters, stereo
in and out, plays and restores its state) and its page drawn by Melodee's `--melodee-plugin-screen`. It has not yet
been run on the MPC.

## Install

Copy `build/CortexFilter.clap` to `Melodee/Plugins/hdslave Cortex Filter/hdslave Cortex Filter.clap` on the card, then
add it to a track's effects. It needs only libc and libm (glibc 2.35 or older).

## Credits

- The screen's type: Departure Mono, copyright 2022–2024 Helena Zhang, SIL Open Font License 1.1
  (`licenses/OFL-departure-mono.txt`); its glyphs are rasterized into `src/ui/font.h`.
- The screen extension's header, `src/melodee_screen.h`, is melodee-hdslave's, MIT.
- CLAP headers: MIT license, `third_party/clap/LICENSE`.
- The vowel formants are the standard published tenor values (five formants' frequency, level and bandwidth).
