#pragma once

// The project file: its format version, paths kept relative to it, and exporting a project
// (the file plus a copy of every sample in a "files" folder beside it).

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

#include <atomic>
#include <mutex>
#include <vector>

namespace ProjectFile {

// Written as "version" in "global". Files without one are version 1 (before this was added).
// Bump it when the format changes in a way older versions would misread.
constexpr int kVersion = 2;

// `path` relative to `projectDir` ("files/x.wav") when it's inside that folder; otherwise
// unchanged. Always with forward slashes.
QString relativePath(const QString& path, const QString& projectDir);

} // namespace ProjectFile

// One project export: the copies to make, filled in by saveConfigTo on the UI thread, then
// worked through by the export thread. The counters are read by the UI thread for the bar.
struct ExportState {
    struct Copy {
        QString src;
        QString dest;
        qint64 bytes = 0;
    };
    QString jsonPath;
    QString filesDir;
    QByteArray json;
    std::vector<Copy> copies;
    QHash<QString, QString> destBySource; // absolute source path -> destination
    QSet<QString> usedNames;              // lower-cased names taken in the files folder
    QStringList missing;                  // sources that weren't found when exporting
    qint64 totalBytes = 0;

    std::atomic<qint64> doneBytes { 0 };
    std::atomic<int> doneFiles { 0 };
    std::atomic<bool> cancel { false };
    std::atomic<bool> finished { false };
    std::mutex failedMutex;
    QStringList failed;

    // The file a sample is copied to. The same source always gets the same copy; a different
    // source with a name already taken gets "name (2).ext", "name (3).ext" and so on.
    QString destinationFor(const QFileInfo& src)
    {
        const QString key = src.absoluteFilePath();
        const auto found = destBySource.constFind(key);
        if (found != destBySource.constEnd()) {
            return found.value();
        }
        const QString base = src.completeBaseName();
        const QString suffix = src.suffix().isEmpty() ? QString() : QStringLiteral(".") + src.suffix();
        QString name = src.fileName();
        for (int n = 2; usedNames.contains(name.toLower()); ++n) {
            name = QStringLiteral("%1 (%2)%3").arg(base).arg(n).arg(suffix);
        }
        usedNames.insert(name.toLower());
        const QString dest = QDir(filesDir).filePath(name);
        destBySource.insert(key, dest);
        if (QFileInfo(dest).absoluteFilePath() != key) {
            copies.push_back({ key, dest, src.size() });
            totalBytes += src.size();
        }
        return dest;
    }

    void fail(const QString& path)
    {
        std::lock_guard<std::mutex> lock(failedMutex);
        failed << QDir::toNativeSeparators(path);
    }

    // Runs on the export thread.
    void run();
};

