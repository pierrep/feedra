#include "AudioFormats.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QMimeData>
#include <QSet>

#include <sndfile.h>

#include <algorithm>

namespace {

struct FormatLists {
    QStringList extensions;
    QStringList patterns;
    QSet<QString> lookup;
};

// What libsndfile says it can read. Extensions come from SFC_GET_FORMAT_MAJOR, which gives
// one "main" extension per container; common alternative spellings are added alongside.
QSet<QString> sndfileExtensions()
{
    QSet<QString> out;
    int majorCount = 0;
    sf_command(nullptr, SFC_GET_FORMAT_MAJOR_COUNT, &majorCount, sizeof(majorCount));
    for (int i = 0; i < majorCount; ++i) {
        SF_FORMAT_INFO info{};
        info.format = i;
        if (sf_command(nullptr, SFC_GET_FORMAT_MAJOR, &info, sizeof(info)) != 0 || !info.extension) {
            continue;
        }
        const QString ext = QString::fromLatin1(info.extension).toLower();
        // Headerless raw PCM can't be played without being told its layout.
        if (ext.isEmpty() || ext == QLatin1String("raw")) {
            continue;
        }
        out.insert(ext);
    }

    bool opus = false;
    int subCount = 0;
    sf_command(nullptr, SFC_GET_FORMAT_SUBTYPE_COUNT, &subCount, sizeof(subCount));
    for (int i = 0; i < subCount; ++i) {
        SF_FORMAT_INFO info{};
        info.format = i;
        if (sf_command(nullptr, SFC_GET_FORMAT_SUBTYPE, &info, sizeof(info)) == 0 && info.name
            && QString::fromLatin1(info.name).contains(QLatin1String("opus"), Qt::CaseInsensitive)) {
            opus = true;
        }
    }

    if (out.contains(QStringLiteral("aiff"))) {
        out << QStringLiteral("aif") << QStringLiteral("aifc");
    }
    if (out.contains(QStringLiteral("ogg"))) {
        out << QStringLiteral("oga");
        if (opus) {
            out << QStringLiteral("opus");
        }
    }
    if (out.contains(QStringLiteral("wav"))) {
        out << QStringLiteral("wave");
    }
    return out;
}

const FormatLists& lists()
{
    static const FormatLists built = []() {
        QSet<QString> exts = sndfileExtensions();
        if (exts.isEmpty()) {
            // libsndfile gave no list (it always should): fall back to the formats every
            // build of it has read for years.
            exts = { QStringLiteral("wav"), QStringLiteral("aiff"), QStringLiteral("aif"),
                     QStringLiteral("flac"), QStringLiteral("ogg"), QStringLiteral("oga") };
        }
#ifdef FEEDRA_USING_MPG123
        exts.insert(QStringLiteral("mp3"));
#else
        // The decoder sends .mp3 to mpg123 only, so without it they can't play.
        exts.remove(QStringLiteral("mp3"));
#endif
        FormatLists l;
        l.extensions = QStringList(exts.begin(), exts.end());
        std::sort(l.extensions.begin(), l.extensions.end());
        for (const QString& ext : l.extensions) {
            l.patterns << QStringLiteral("*.") + ext << QStringLiteral("*.") + ext.toUpper();
            l.lookup.insert(ext);
        }
        return l;
    }();
    return built;
}

} // namespace

namespace AudioFormats {

const QStringList& extensions()
{
    return lists().extensions;
}

const QStringList& patterns()
{
    return lists().patterns;
}

bool isPlayable(const QString& path)
{
    return lists().lookup.contains(QFileInfo(path).suffix().toLower());
}

QStringList dialogFilters()
{
    QStringList globs;
    for (const QString& ext : extensions()) {
        globs << QStringLiteral("*.") + ext;
    }
    return {
        QStringLiteral("Audio files (%1)").arg(globs.join(QLatin1Char(' '))),
        QStringLiteral("All files (*.*)")
    };
}

QStringList playableFiles(const QList<QUrl>& urls)
{
    QStringList out;
    for (const QUrl& url : urls) {
        if (!url.isLocalFile()) {
            continue;
        }
        const QString path = url.toLocalFile();
        const QFileInfo info(path);
        if (info.isDir()) {
            const QDir dir(path);
            // Every file, checked by extension case-insensitively (so ".Wav" counts too).
            const QStringList names = dir.entryList(QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
            for (const QString& name : names) {
                if (isPlayable(name)) {
                    out << dir.filePath(name);
                }
            }
        } else if (info.isFile() && isPlayable(path)) {
            out << path;
        }
    }
    out.removeDuplicates();
    return out;
}

bool hasPlayableFiles(const QMimeData* mime)
{
    if (!mime || !mime->hasUrls()) {
        return false;
    }
    for (const QUrl& url : mime->urls()) {
        if (!url.isLocalFile()) {
            continue;
        }
        const QString path = url.toLocalFile();
        const QFileInfo info(path);
        if (info.isDir()) {
            QDirIterator it(path, QDir::Files | QDir::Readable);
            while (it.hasNext()) {
                if (isPlayable(it.next())) {
                    return true;
                }
            }
        } else if (isPlayable(path)) {
            return true;
        }
    }
    return false;
}

} // namespace AudioFormats
