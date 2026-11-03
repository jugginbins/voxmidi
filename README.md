# VoxMidi - live voice-to-MIDI plugin (free, open source)

Sing, hum, whistle or beatbox into a microphone and get MIDI out in real time - a Dubler-style
instrument you can build yourself on Windows and macOS for free. Everything is C++17 on top of
[JUCE](https://juce.com) (AGPLv3); the DSP engine (`engine/`) is plain header-only C++ with no dependencies.

## What it does

| Feature | How it works |
|---|---|
| **Pitch to MIDI** | McLeod Pitch Method on a 16 kHz decimated copy of the mic signal, ~4 ms hop |
| **Note logic** | notes start only when pitch is stable (no firing mid-scoop), octave glitches are ignored, a *stationary* change of pitch re-triggers while a *moving* pitch becomes pitch bend, a loudness dip (new syllable) re-articulates |
| **Scales** | 15 scales + custom note mask, any root, snaps your pitch to the scale. Auto key suggestion from what you sing |
| **Pitch bend** | Off / **IntelliBend** (anchored to the note until you really bend away) / **TruBend** (follows exactly), range 1-24 semitones |
| **Chords** | diatonic triads/7ths (follow the scale), power, octave, sus2/4, major, minor - on their own MIDI channel |
| **Beatbox triggers** | 8 pads. Train each with your own sounds (6-10 hits); classified by spectral shape (8 bands + centroid + flatness). Kick / snare / hat / clap / click all work. ~7 ms latency |
| **Vowel controllers** | LPC formant tracking -> amount of "ah" / "ee" / "oo" as CC 20/21/22, plus loudness CC 23. Re-learn the vowels for your own voice |
| **Velocity** | follows how loud you sing / hit, or fixed |

## Get the plugins for free (no compiler needed)

1. Create a free GitHub account and a new repository.
2. Upload this whole folder to it.
3. Open the repository's **Actions** tab -> **Build VoxMidi** -> wait ~10 minutes.
4. Open the finished run and download **VoxMidi-Windows** and/or **VoxMidi-macOS** under *Artifacts*.

GitHub builds Windows, macOS (Intel + Apple Silicon in one binary) and Linux for you, and runs the DSP tests.

Or build locally: install CMake and Visual Studio (Windows, "Desktop development with C++") or Xcode (macOS), then

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

The first configure downloads JUCE automatically (needs internet + git).

## Install

* **Windows:** copy `VoxMidi.vst3` and/or `VoxMidi FX.vst3` to `C:\Program Files\Common Files\VST3`
* **macOS:** copy them to `/Library/Audio/Plug-Ins/VST3` (or `~/Library/Audio/Plug-Ins/VST3`).
  Downloaded files are quarantined, so run once in Terminal:
  `xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/VST3/VoxMidi.vst3"`
* In FL Studio: **Options -> Manage plugins -> Find plugins** (make sure your VST3 folder is in the scan list).

## Two plugins - which one to use

* **VoxMidi** (instrument). FL Studio's documented way of getting MIDI *out of a plugin* is from an instrument
  (generator), and FL gives instruments no audio input, so this build opens the microphone **itself**
  (Audio source = *Direct*; "Audio input..." button picks the device).
* **VoxMidi FX** (effect). Takes audio from a mixer insert like any effect. Image-Line's manual does not say
  whether FL routes MIDI out of *effect* plugins, so treat this one as an experiment in FL. It is the right
  choice in hosts that do (Reaper, Bitwig, ...).

### FL Studio setup (instrument build)

1. Add **VoxMidi** to the Channel rack.
2. Click the plugin's wrapper gear -> **Settings** tab -> **MIDI** -> set **Output port** to e.g. 1.
3. Add the synth you want to play (any VST/FL synth). In *its* wrapper Settings tab set **Input port** to the same number (1).
4. Sing. Pitch-bend range in the target synth must equal VoxMidi's *Bend range* (default 2).

(The port names are taken from FL's manual; it describes matching port numbers but gives no step-by-step for
plugin-to-plugin MIDI, so if something differs in your FL version, the idea is: same port number out and in.)

**Microphone notes.** Windows: if your interface uses an ASIO driver that FL has opened exclusively, the plugin's own
audio device may not be able to share it - pick a different input (laptop mic, USB mic) in *Audio input...*, or use
the FX build. macOS: FL needs microphone permission (System Settings -> Privacy -> Microphone).

### Fallback that works in any DAW: the Standalone app

Both targets also build a **Standalone** app. Run it, choose your mic under *Options -> Audio/MIDI Settings*, and pick
a MIDI output. On macOS enable the built-in *IAC Driver* (Audio MIDI Setup); on Windows install the free
[loopMIDI](https://www.tobias-erichsen.de/software/loopmidi.html) and select its port. In FL, enable that port as a MIDI input
and assign it to your synth (this is how Dubler itself works).

## Getting a good result

1. Set **Input gain** until the level bar peaks around the top third while singing. The yellow tick is the gate.
2. Pick a **Scale** (or press *Use key* after singing a few notes). Scales make everything more forgiving.
3. **Stickiness** is the main trade-off: low = fastest note start (~30 ms), high = ignores scoops and wobble.
4. Sing "wah"/"da" rather than bare vowels - the consonant gives a clean onset. Hum steady pitches for pads.
5. Triggers: train 2-3 clearly different sounds first (e.g. "boom", "ts", "pf"). Short, sharp sounds work best.
   Raise *Match tolerance* if it misses hits, lower it if it fires on the wrong sound.
6. Delay (hops) trades trigger speed for accuracy; 2 is a good start.

## What was verified, and what was not

Verified here: the DSP engine compiled and passed its tests with synthetic voices at 44.1, 48 and 96 kHz
(`tests/engine_tests.cpp`, run by `ctest`): note detection and latency (~32 ms), scoop/vibrato/octave-glitch handling,
bend modes, chords, scale snapping, key detection, trigger training and classification (36/36 correct, ~7 ms),
vowel tracking, and CPU (<1 % of one core). The JUCE plugin code was syntax-checked against JUCE 8.0.15 headers.

**Not verified:** the plugin has not been *run* - it was not linked or loaded in FL Studio, no real microphone was used,
and the GitHub workflow has not run yet. Real voices are messier than test signals, so expect to tune *Stickiness*,
*Pitch confidence* and *Gate*. If the CI build fails, send me the log.

## Layout

```
engine/   DSP: PitchTracker, NoteTracker, Scales, TriggerEngine, VowelTracker, VoxEngine
plugin/   JUCE processor + editor
tests/    synthetic-voice engine tests
.github/  CI that builds Windows / macOS / Linux
```

## Publish it and share it

1. Push this folder to GitHub (a public repository).
2. The `LICENSE` file (full GNU AGPL v3 text) is already included.
3. **Turn on the website:** *Settings -> Pages -> Build and deployment -> Deploy from a branch -> `main` / `/docs`*.
   The site (`docs/index.html`) appears at `https://YOUR-NAME.github.io/REPO/` and fixes its own download links.
4. **Publish downloads:** `git tag v0.1.0 && git push --tags`. The workflow builds everything and attaches
   `VoxMidi-Windows.zip`, `VoxMidi-macOS.zip` and `VoxMidi-Linux.zip` to a Release; the site's Download buttons
   point at those.

## Licence

VoxMidi is open source under the **GNU AGPL v3** (see `NOTICE.md`). JUCE is dual-licensed AGPLv3/commercial, and
using it under the AGPL means VoxMidi must be AGPL too. You can share it freely; if you hand out builds, make the
matching source available (a public repo with Releases does that).
