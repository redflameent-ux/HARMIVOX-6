# HARMIVOX-6

A vocal tuning and harmony plugin (VST3 for FL Studio and Ableton, plus a standalone app).
Capture a take, clean it, tune it, stack doubles and harmonies, tighten the timing, finish the tone,
then drag the whole stack onto your playlist.

## Get it

Every push to `main` builds Windows automatically. Download **HARMIVOX-6-Windows.zip** from the
[**latest release**](../../releases/tag/latest) and follow `INSTALL-WINDOWS.txt` inside the zip.

## The five steps

| Step | What it does |
|---|---|
| **1 Import** | *Arm capture* records what plays through the mixer insert while FL plays, lined up with your song. Or load / drop WAV, AIFF, FLAC, MP3. First take = LEAD. |
| **2 Clean** | Noise reduce (learns the room from quiet parts), breath control, gate between phrases, de-click. |
| **3 Tune** | Auto key & scale, tuning presets (Natural / Pop / Hard tune), strength, retune speed, drift fix, vibrato. *Add voices* builds doubles, harmonies and stacks. Chord lane (auto or set per bar) for chord-following harmonies. |
| **4 Time** | Quantize to the grid, align takes and voices to the lead, warp markers you can drag. |
| **5 Finish** | Vocal chain (low cut, de-plosive, mud cut, de-esser, presence, air, compressor) with Natural / Pop / Rap / Airy starting points, plus a stereo imager and stereoize. |

**Notes view** (top toolbar) is the piano roll: drag notes for pitch (Alt = cents), drag edges for timing,
right-drag for drift, drag the join between two notes to shape the transition, double-click to snap.
Per-note volume, formant, drift fix, vibrato, added vibrato and rate are in the bar underneath.

**Add voices**: presets (Double L/R, Quad doubles, Pop stack, Thirds & fifths, Wide choir, Chord pad,
Octave layers, Air layer) and harmony ideas (Classic 3rd, Spicy 7th, Dreamy 9th, Gospel stack, Power,
Dissonant), or pick intervals yourself above and below. Follow key, follow chords, or exact semitones.
Every voice gets its own timing, pitch drift and tone so a stack sounds like several takes, not copies.
*Reroll take* gives a voice a new performance.

**Voice lab**: a guided ~10 minute session (20 short phrases, each checked). It learns how your tone changes
across your range plus your vibrato, drift and timing feel, and new voices use that so harmonies keep
sounding like you. Saved in `Documents/HARMIVOX-6/Voices`.

**Output**: *Replace insert vocals* (default), *Blend*, or *Input only*. *Drag to playlist* exports one file
per track (or one mix, right-click) that lines up when dropped at bar 1.

Shortcuts: Space play/stop, Ctrl+Z / Ctrl+Shift+Z undo/redo, Tab switch views, 1-5 jump to a step.

## How it works (for whoever touches the code)

- `Source/engine` has no UI code. `Analysis` (YIN pitch tracking, pitch marks, note detection, key finding),
  `Renderer` (builds a per-frame plan, then TD-PSOLA resynthesis with formant control and time warping),
  `Harmony`, `Aligner` (quantize + DTW alignment), `Cleaner`, `Finish`, `VoiceProfile`, and `Session`
  (undo history, background rendering, save/restore with the FL project, export).
- Tracks render in the background whenever their settings change; playback in `processBlock` just mixes
  the rendered audio at the host position, so there is no added latency.
- `Tests/EngineTests.cpp` sings synthetic melodies and checks detection, tuning accuracy, harmonies,
  quantize, alignment, cleaning and the voice profile. CI runs it before the Windows build.

## Build it yourself

```
cmake -S . -B build
cmake --build build --config Release --target Harmivox6_VST3
```
JUCE 8 is downloaded automatically. Fonts: Barlow and Share Tech Mono (SIL Open Font License, in `Resources/fonts`).
