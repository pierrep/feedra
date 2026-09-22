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
