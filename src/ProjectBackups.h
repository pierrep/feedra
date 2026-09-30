#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>

// Timestamped copies of project files, kept in Feedra's data folder (not beside the project,
// so moving or tidying a project folder can't take them with it).
//
// A copy of the file on disk is made before it's overwritten with new scenes (at most one
// every 10 minutes while working), whenever a project is opened, and on quit. A copy that
// would be identical to the newest one is skipped. Kept: the newest 20, plus the newest of
// each day for the last 30 days.
namespace ProjectBackups {

struct Counts {
    int scenes = 0;
    int pads = 0;
    int samples = 0;
};

struct Entry {
    QString path;
    QDateTime time;
    Counts counts;
    bool readable = false;
};

// Where backups go: <dataDir>/backups. Call once at startup.
void setRoot(const QString& dataDir);

// Scenes, pads with samples, and samples in a project's JSON.
Counts count(const QJsonObject& root);
// The same for a file; `ok` is false when it can't be read as a project.
Counts countFile(const QString& path, bool* ok = nullptr);

// Copies `projectPath` as it is on disk now. Without `force`, nothing is copied when the newest
// backup is less than 10 minutes old. Returns false only when a copy was due and failed.
bool backup(const QString& projectPath, bool force);

// This project's backups, newest first.
QList<Entry> list(const QString& projectPath);

// The folder holding this project's backups.
QString folderFor(const QString& projectPath);

}
