#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

class QMimeData;

// The audio file types Feedra can play, worked out once at start-up: every format the
// linked libsndfile says it can read, plus mp3 when mpg123 is built in. The file dialogs,
// the Files tab and the pads' drop check all use this list, so they always agree.
namespace AudioFormats {

// Lower-case extensions without the dot, e.g. "wav", "flac", "mp3". Sorted.
const QStringList& extensions();

// Glob patterns for the extensions, e.g. "*.wav", "*.WAV" (both cases, for case-sensitive
// file systems).
const QStringList& patterns();

// True when `path` has one of the extensions (case-insensitive). Does not open the file.
bool isPlayable(const QString& path);

// Filters for QFileDialog: "Audio files (...)" then "All files (*.*)".
QStringList dialogFilters();

// The playable files in a drop: playable local files as they are, and for a folder the
// playable files directly inside it (not in its subfolders), sorted by name. Other files
// are left out.
QStringList playableFiles(const QList<QUrl>& urls);

// True when a drag carries at least one playable file (or a folder holding one).
// Stops at the first one found, so it's cheap enough for drag-enter.
bool hasPlayableFiles(const QMimeData* mime);

} // namespace AudioFormats
