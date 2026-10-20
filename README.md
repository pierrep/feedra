# Feedra
An interactive audio atmosphere generator.

This software is in development and new features will be incoming.

It is built with [Qt 6](https://www.qt.io/) and [OpenAL Soft](https://www.openal-soft.org/).
It offers the ability to load in samples, as loops or one-shots, and organise them into scenes.

This is meant to be used as an audio tool for TTRPGs, as well as live theatre sound design, sound installations, etc.

## Build

Requires Qt 6 (Widgets), CMake 3.21+, a C++17 compiler, OpenAL Soft, libsndfile, and mpg123.
On Linux, MIDI input also needs the ALSA development files (`libasound2-dev` / `alsa-lib-devel`);
without them Feedra builds without MIDI. RtMidi itself is bundled in `third_party/rtmidi`.

```
cmake -S . -B build -DCMAKE_PREFIX_PATH=<path-to-Qt6>
cmake --build build --config Release
```

On Windows, the bundled `libs/Win64` import libraries and the DLLs in `bin/` are used.

Currently targeted at Windows, with Linux and macOS supported through the same CMake file.

## Tests

```
cmake -S . -B build -DCMAKE_PREFIX_PATH=<path-to-Qt6>
cmake --build build
ctest --test-dir build --output-on-failure
```

- `region_renderer`: the loop, crossfade and fade logic (including a sample's own fade in and out), on its own.
- `engine`: the audio engine on OpenAL Soft's silent `null` device (no sound card needed).
- `cue`: the preview output on a second (null) device: play, seek, end, a bad file, close.
- `project_roundtrip`: opens a project in the window (offscreen), quits, and checks the saved file.
- `panels`: the Editor and bottom panel driven directly, and a Files-tab preview on the cue output.
- `scene_fades`: the equal-power fade curve and a crossfade between two scenes.
- `live`: the talk-over duck (and its fade times), which key plays which pad, the latency presets, and the MIDI map (learning included).
- `live_window`: pad keys, the Tab key caps, Space, Esc and MIDI messages in the real window.
- `pad_play`: play, pause and stop, and a sample's own fades surviving save, load, copy and paste.
- `intensity`: the layer crossfade, and the Intensity slider, MIDI and the Pad tab's layer in the window.
- `undo`: undo and redo of settings, cut and paste, moving pads, deleting a scene and dragging a sample's fades on the waveform, in the window.

CI (`.github/workflows/build.yml`) builds on Linux, macOS and Windows and runs them.
Configure with `-DFEEDRA_WARNINGS_AS_ERRORS=ON` to fail on warnings, `-DFEEDRA_BUILD_TESTS=OFF` to skip the tests.

## Source layout

- `AudioDevice`: the OpenAL device, effect buses, scheduled starts.
- `AudioLatency`: the Settings "Latency" presets, which size the output buffer through an OpenAL Soft config file written before the device opens.
- `CueOutput`: a second device for the Files tab's previews (headphones), on its own thread.
- `Duck`, `KeyLayout`, `ControlMap`, `MidiInput`: the live controls: talk-over, pad keys, MIDI
  mappings and MIDI input.
- `AudioCodec`: reading audio files.
- `RegionRenderer`: loop regions and crossfades.
- `OpenALSoundPlayer`: one voice: its source, queue and the stream service.
- `SoundPlayer`: a pad's list of samples, delays, random order and handovers.
- `Pad`: one pad's samples and settings. `Scene` owns a
  scene's pads; `SoundPadWidget` draws one. `Fade`: the equal-power fade curve.
  `Intensity`: a scene's Calm, Tense and Combat layers and the crossfade between them.
- `ProjectFile`, `ProjectBackups`: the project file's format, export and backups.
- `MainWindow`: the layout, menus and the routing between the panels: `widgets/EditorPanel`
  (the sample list and controls), `widgets/BottomPanel` (Waveform, Pad and Info tabs),
  `widgets/SettingsPage` and `widgets/ThemePage`.

