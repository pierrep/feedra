# Feedra
An interactive audio atmosphere generator.

This software is in development and new features will be incoming.

It is built with [Qt 6](https://www.qt.io/) and [OpenAL Soft](https://www.openal-soft.org/).
It offers the ability to load in samples, as loops or one-shots, and organise them into scenes.

This is meant to be used as an audio tool for TTRPGs, as well as live theatre sound design, sound installations, etc.

## Build

Requires Qt 6 (Widgets), CMake 3.21+, a C++17 compiler, OpenAL Soft, libsndfile, and mpg123.

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

- `region_renderer`: the loop, crossfade and fade logic, on its own.
- `engine`: the audio engine on OpenAL Soft's silent `null` device (no sound card needed).
- `project_roundtrip`: opens a project in the window (offscreen), quits, and checks the saved file.

CI (`.github/workflows/build.yml`) builds on Linux, macOS and Windows and runs them.
Configure with `-DFEEDRA_WARNINGS_AS_ERRORS=ON` to fail on warnings, `-DFEEDRA_BUILD_TESTS=OFF` to skip the tests.

## Source layout

- `AudioDevice`: the OpenAL device, effect buses, scheduled starts.
- `AudioCodec`: reading audio files.
- `RegionRenderer`: loop regions and crossfades.
- `OpenALSoundPlayer`: one voice: its source, queue and the stream service.
- `SoundPlayer`: a pad's list of samples, delays, random order and handovers.
- `Pad`: one pad's samples and settings. `Scene` owns a scene's pads; `SoundPadWidget` draws one.
- `ProjectFile`, `ProjectBackups`: the project file's format, export and backups.
- `MainWindow`, plus `widgets/SettingsPage` and `widgets/ThemePage`: the UI.

