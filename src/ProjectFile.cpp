#include "ProjectFile.h"

#include <QFile>
#include <QSaveFile>

QString ProjectFile::relativePath(const QString& path, const QString& projectDir)
{
    if (path.isEmpty() || projectDir.isEmpty() || QDir::isRelativePath(path)) {
        return QDir::fromNativeSeparators(path);
    }
    const QString rel = QDir(projectDir).relativeFilePath(QDir::cleanPath(path));
    if (rel.isEmpty() || QDir::isAbsolutePath(rel) || rel == QLatin1String("..") || rel.startsWith(QLatin1String("../"))) {
        return QDir::fromNativeSeparators(path);
    }
    return rel;
}

void ExportState::run()
{
    constexpr qint64 kBlock = 1 << 20;
    QByteArray block;
    for (const Copy& copy : copies) {
        if (cancel.load()) {
            break;
        }
        const QFileInfo srcInfo(copy.src);
        const QFileInfo destInfo(copy.dest);
        // Already there from an earlier export of the same file: leave it.
        if (destInfo.exists() && destInfo.size() == copy.bytes && destInfo.lastModified() >= srcInfo.lastModified()) {
            doneBytes += copy.bytes;
            ++doneFiles;
            continue;
        }
        QFile in(copy.src);
        QSaveFile out(copy.dest); // written to a temporary file, so a stopped copy leaves nothing behind
        qint64 copied = 0;
        bool ok = in.open(QIODevice::ReadOnly) && out.open(QIODevice::WriteOnly);
        while (ok && !cancel.load()) {
            block = in.read(kBlock);
            if (block.isEmpty()) {
                ok = in.atEnd();
                break;
            }
            if (out.write(block) != block.size()) {
                ok = false;
                break;
            }
            copied += block.size();
            doneBytes += block.size();
        }
        if (cancel.load()) {
            out.cancelWriting();
            break;
        }
        if (!ok || !out.commit()) {
            fail(copy.src);
        }
        // Keep the bar honest when a file turned out a different size or failed part way.
        doneBytes += copy.bytes - copied;
        ++doneFiles;
    }
    finished.store(true);
}

