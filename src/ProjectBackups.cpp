#include "ProjectBackups.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

#include <algorithm>

namespace {

QString g_root;

constexpr int kKeepRecent = 20;
constexpr int kKeepDays = 30;
constexpr qint64 kMinGapSecs = 10 * 60;
const QString kStampFormat = QStringLiteral("yyyy-MM-dd HH-mm-ss");

QDateTime stampOf(const QFileInfo& file)
{
    // "<name> yyyy-MM-dd HH-mm-ss.json"; the file's own time if the name doesn't have one.
    static const QRegularExpression stamp(QStringLiteral("(\\d{4}-\\d{2}-\\d{2} \\d{2}-\\d{2}-\\d{2})$"));
    const QRegularExpressionMatch match = stamp.match(file.completeBaseName());
    if (match.hasMatch()) {
        const QDateTime time = QDateTime::fromString(match.captured(1), kStampFormat);
        if (time.isValid()) {
            return time;
        }
    }
    return file.lastModified();
}

QFileInfoList backupsIn(const QString& folder)
{
    QFileInfoList files = QDir(folder).entryInfoList({ QStringLiteral("*.json") }, QDir::Files);
    std::sort(files.begin(), files.end(), [](const QFileInfo& a, const QFileInfo& b) {
        return stampOf(a) > stampOf(b);
    });
    return files;
}

void prune(const QString& folder)
{
    const QFileInfoList files = backupsIn(folder);
    const QDate today = QDate::currentDate();
    QSet<QDate> daysKept;
    for (int i = 0; i < files.size(); ++i) {
        const QDate day = stampOf(files[i]).date();
        bool keep = i < kKeepRecent;
        if (!keep && day.daysTo(today) <= kKeepDays && !daysKept.contains(day)) {
            keep = true; // the newest backup of that day
        }
        if (keep) {
            daysKept.insert(day);
        } else {
            QFile::remove(files[i].absoluteFilePath());
        }
    }
}

} // namespace

namespace ProjectBackups {

void setRoot(const QString& dataDir)
{
    g_root = QDir(dataDir).filePath(QStringLiteral("backups"));
}

QString folderFor(const QString& projectPath)
{
    const QString absolute = QFileInfo(projectPath).absoluteFilePath();
    // The name for people, a short hash so projects with the same name don't share a folder.
    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(absolute.toUtf8(), QCryptographicHash::Md5).toHex().left(8));
    return QDir(g_root).filePath(QFileInfo(absolute).completeBaseName() + QStringLiteral(" ") + hash);
}

Counts count(const QJsonObject& root)
{
    static const QRegularExpression sceneKey(QStringLiteral("^scene\\d+$"));
    static const QRegularExpression padKey(QStringLiteral("^\\d+-\\d+$"));
    Counts counts;
    QSet<QString> padsSeen;
    for (auto it = root.begin(); it != root.end(); ++it) {
        if (!it.value().isObject()) {
            continue;
        }
        const QJsonObject object = it.value().toObject();
        if (sceneKey.match(it.key()).hasMatch() && object.contains(QStringLiteral("id"))) {
            ++counts.scenes;
        }
        // Pads live inside scene objects ("<sceneId>-<padId>").
        for (auto p = object.begin(); p != object.end(); ++p) {
            if (!padKey.match(p.key()).hasMatch() || padsSeen.contains(p.key())) {
                continue;
            }
            const QJsonObject samples = p.value().toObject().value(QStringLiteral("samples")).toObject();
            if (samples.isEmpty()) {
                continue;
            }
            padsSeen.insert(p.key());
            ++counts.pads;
            counts.samples += static_cast<int>(samples.size());
        }
    }
    return counts;
}

Counts countFile(const QString& path, bool* ok)
{
    if (ok) {
        *ok = false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        return {};
    }
    if (ok) {
        *ok = true;
    }
    return count(doc.object());
}

bool backup(const QString& projectPath, bool force)
{
    if (g_root.isEmpty() || projectPath.isEmpty()) {
        return true;
    }
    QFile source(projectPath);
    if (!source.exists()) {
        return true; // nothing on disk yet
    }
    if (!source.open(QIODevice::ReadOnly)) {
        qWarning() << "Backup: couldn't read" << projectPath << source.errorString();
        return false;
    }
    const QByteArray bytes = source.readAll();
    source.close();
    if (bytes.isEmpty()) {
        return true;
    }

    const QString folder = folderFor(projectPath);
    const QFileInfoList existing = backupsIn(folder);
    if (!existing.isEmpty()) {
        const QFileInfo& newest = existing.front();
        if (!force && stampOf(newest).secsTo(QDateTime::currentDateTime()) < kMinGapSecs) {
            return true;
        }
        QFile last(newest.absoluteFilePath());
        if (last.open(QIODevice::ReadOnly) && last.size() == bytes.size() && last.readAll() == bytes) {
            return true; // unchanged since the newest backup
        }
    }

    if (!QDir().mkpath(folder)) {
        qWarning() << "Backup: couldn't create" << folder;
        return false;
    }
    // Which project these belong to, for anyone looking in the folder.
    QFile note(QDir(folder).filePath(QStringLiteral("project.txt")));
    if (!note.exists() && note.open(QIODevice::WriteOnly | QIODevice::Text)) {
        note.write(QDir::toNativeSeparators(QFileInfo(projectPath).absoluteFilePath()).toUtf8() + '\n');
    }

    const QString base = QFileInfo(projectPath).completeBaseName();
    QString target = QDir(folder).filePath(base + QStringLiteral(" ")
        + QDateTime::currentDateTime().toString(kStampFormat) + QStringLiteral(".json"));
    for (int n = 2; QFile::exists(target); ++n) { // two within the same second
        target = QDir(folder).filePath(base + QStringLiteral(" ")
            + QDateTime::currentDateTime().toString(kStampFormat) + QStringLiteral(" (%1).json").arg(n));
    }
    QSaveFile out(target);
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit()) {
        qWarning() << "Backup: couldn't write" << target << out.errorString();
        return false;
    }
    qInfo() << "Backed up" << projectPath << "to" << target;
    prune(folder);
    return true;
}

QList<Entry> list(const QString& projectPath)
{
    QList<Entry> out;
    for (const QFileInfo& file : backupsIn(folderFor(projectPath))) {
        Entry entry;
        entry.path = file.absoluteFilePath();
        entry.time = stampOf(file);
        entry.counts = countFile(entry.path, &entry.readable);
        out << entry;
    }
    return out;
}

}
