#include "PeakStore.h"

#include "OpenALSoundPlayer.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QSaveFile>
#include <QThread>
#include <QThreadPool>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>

namespace {
constexpr int kUrgentPriority = 1;
constexpr int kNormalPriority = 0;

// Cache file layout (little-endian):
//   char[4] "FDPK", uint16 version, uint16 flags (bit 0 = ok), uint32 bins,
//   int64 source size, int64 source modified (ms since epoch),
//   int16[bins] mins, int16[bins] maxs   (value * 32767)
// An undecodable file is saved with ok = 0 and bins = 0 so it isn't rescanned every launch.
constexpr char kMagic[4] = {'F', 'D', 'P', 'K'};
constexpr quint16 kVersion = 1;
constexpr quint16 kFlagOk = 1;
constexpr qint64 kHeaderBytes = 4 + 2 + 2 + 4 + 8 + 8;

struct SourceStamp {
    bool ok = false;
    qint64 size = 0;
    qint64 modified = 0;
};

SourceStamp stampOf(const QString& key)
{
    const QFileInfo info(key);
    SourceStamp stamp;
    if (info.exists() && info.isFile()) {
        stamp.ok = true;
        stamp.size = info.size();
        stamp.modified = info.lastModified().toMSecsSinceEpoch();
    }
    return stamp;
}

QString cacheFileFor(const QString& dir, const QString& key)
{
    const QByteArray hash = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QDir(dir).filePath(QString::fromLatin1(hash) + QStringLiteral(".peaks"));
}

// A valid cache file for `key`, or null (missing, damaged, other version, or the sample changed).
std::shared_ptr<const PeakData> readCache(const QString& dir, const QString& key)
{
    if (dir.isEmpty()) {
        return nullptr;
    }
    const SourceStamp stamp = stampOf(key);
    if (!stamp.ok) {
        return nullptr;
    }
    QFile file(cacheFileFor(dir, key));
    if (!file.open(QIODevice::ReadOnly)) {
        return nullptr;
    }
    const qint64 length = file.size();
    if (length < kHeaderBytes) {
        return nullptr;
    }
    QDataStream in(&file);
    in.setByteOrder(QDataStream::LittleEndian);
    char magic[4] = {};
    if (in.readRawData(magic, 4) != 4 || !std::equal(magic, magic + 4, kMagic)) {
        return nullptr;
    }
    quint16 version = 0;
    quint16 flags = 0;
    quint32 bins = 0;
    qint64 size = 0;
    qint64 modified = 0;
    in >> version >> flags >> bins >> size >> modified;
    const bool ok = (flags & kFlagOk) != 0;
    if (in.status() != QDataStream::Ok || version != kVersion || size != stamp.size || modified != stamp.modified
        || (ok && bins != static_cast<quint32>(PeakStore::kBins)) || (!ok && bins != 0)
        || length != kHeaderBytes + qint64(bins) * 2 * 2) {
        return nullptr;
    }

    auto data = std::make_shared<PeakData>();
    data->ok = ok;
    if (ok) {
        std::vector<qint16> raw(static_cast<size_t>(bins) * 2);
        const qint64 bytes = qint64(raw.size()) * 2;
        if (in.readRawData(reinterpret_cast<char*>(raw.data()), static_cast<int>(bytes)) != bytes) {
            return nullptr;
        }
        data->mins.resize(bins);
        data->maxs.resize(bins);
        for (quint32 i = 0; i < bins; ++i) {
            // Stored little-endian; qFromLittleEndian is a no-op on little-endian machines.
            data->mins[i] = qFromLittleEndian(raw[i]) / 32767.0f;
            data->maxs[i] = qFromLittleEndian(raw[bins + i]) / 32767.0f;
        }
    }
    return data;
}

void writeCache(const QString& dir, const QString& key, const SourceStamp& stamp, const PeakData& data)
{
    static std::atomic<bool> warned{false};
    if (dir.isEmpty() || !stamp.ok) {
        return;
    }
    const bool ok = data.ok && data.mins.size() == size_t(PeakStore::kBins) && data.maxs.size() == size_t(PeakStore::kBins);
    const quint32 bins = ok ? static_cast<quint32>(PeakStore::kBins) : 0;

    QSaveFile file(cacheFileFor(dir, key));
    if (!QDir().mkpath(dir) || !file.open(QIODevice::WriteOnly)) {
        if (!warned.exchange(true)) {
            qWarning() << "Waveform cache not writable:" << dir;
        }
        return;
    }
    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData(kMagic, 4);
    out << kVersion << quint16(ok ? kFlagOk : 0) << bins << qint64(stamp.size) << qint64(stamp.modified);
    if (ok) {
        std::vector<qint16> raw(static_cast<size_t>(bins) * 2);
        auto quantise = [](float v) {
            return qToLittleEndian(static_cast<qint16>(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f)));
        };
        for (quint32 i = 0; i < bins; ++i) {
            raw[i] = quantise(data.mins[i]);
            raw[bins + i] = quantise(data.maxs[i]);
        }
        out.writeRawData(reinterpret_cast<const char*>(raw.data()), static_cast<int>(raw.size() * 2));
    }
    if (out.status() != QDataStream::Ok || !file.commit()) {
        if (!warned.exchange(true)) {
            qWarning() << "Waveform cache write failed:" << file.fileName();
        }
    }
}

QString barsKey(const QString& key, int count)
{
    return QString::number(count) + QLatin1Char('|') + key;
}
}

PeakStore& PeakStore::instance()
{
    // Owned by the application so it goes away (and stops scanning) with it.
    static QPointer<PeakStore> store;
    if (!store) {
        store = new PeakStore(QCoreApplication::instance());
    }
    return *store;
}

QString PeakStore::keyFor(const QString& path)
{
    if (path.isEmpty()) {
        return {};
    }
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

PeakStore::PeakStore(QObject* parent)
    : QObject(parent)
    , m_pool(new QThreadPool(this))
{
    // One file at a time: this is display work and should never compete for cores.
    m_pool->setMaxThreadCount(1);
    if (QCoreApplication* app = QCoreApplication::instance()) {
        connect(app, &QCoreApplication::aboutToQuit, this, &PeakStore::shutdown);
    }
}

PeakStore::~PeakStore()
{
    shutdown();
}

void PeakStore::shutdown()
{
    m_quitting.store(true);
    m_cancel->store(true);
    m_pool->clear();
    m_pool->waitForDone();
}

void PeakStore::setCacheDir(const QString& dir)
{
    std::lock_guard<std::mutex> lock(m_dirMutex);
    m_cacheDir = dir;
}

QString PeakStore::cacheDir() const
{
    std::lock_guard<std::mutex> lock(m_dirMutex);
    return m_cacheDir;
}

std::shared_ptr<const PeakData> PeakStore::get(const QString& path, bool urgent)
{
    const QString key = keyFor(path);
    if (key.isEmpty()) {
        return nullptr;
    }
    const auto it = m_peaks.constFind(key);
    if (it != m_peaks.constEnd()) {
        return it.value();
    }
    request(key, urgent);
    // A cache hit in request() is stored straight away.
    const auto hit = m_peaks.constFind(key);
    return hit != m_peaks.constEnd() ? hit.value() : nullptr;
}

QVector<float> PeakStore::bars(const QString& path, int count)
{
    const QString key = keyFor(path);
    if (key.isEmpty() || count <= 0) {
        return {};
    }
    const QString cacheKey = barsKey(key, count);
    const auto cached = m_bars.constFind(cacheKey);
    if (cached != m_bars.constEnd()) {
        return cached.value();
    }
    const std::shared_ptr<const PeakData> data = get(key);
    if (!data) {
        return {};
    }

    // Each bar is the loudest bin in its share of the file, as a scan at `count` bins would give.
    QVector<float> out;
    const int bins = static_cast<int>(std::min(data->mins.size(), data->maxs.size()));
    if (data->ok && bins > 0) {
        out.reserve(count);
        float loudest = 0.0f;
        for (int i = 0; i < count; ++i) {
            const int b0 = static_cast<int>(static_cast<int64_t>(i) * bins / count);
            const int b1 = std::max(b0 + 1, static_cast<int>(static_cast<int64_t>(i + 1) * bins / count));
            float amp = 0.0f;
            for (int b = b0; b < b1 && b < bins; ++b) {
                amp = std::max({amp, std::abs(data->mins[static_cast<size_t>(b)]),
                    std::abs(data->maxs[static_cast<size_t>(b)])});
            }
            out.append(amp);
            loudest = std::max(loudest, amp);
        }
        if (loudest > 0.0f) {
            for (float& bar : out) {
                bar /= loudest;
            }
        }
    }
    if (out.isEmpty()) {
        out.fill(0.35f, count); // unreadable: keep a flat strip, don't retry forever
    }
    m_bars.insert(cacheKey, out);
    return out;
}

void PeakStore::preload(const QString& path)
{
    const QString key = keyFor(path);
    if (key.isEmpty() || m_quitting.load()) {
        return;
    }
    std::shared_ptr<const PeakData> data = readCache(cacheDir(), key);
    if (!data) {
        return; // no valid cache file: the first get() scans it
    }
    QMetaObject::invokeMethod(this, [this, key, data]() {
        store(key, data);
    }, Qt::QueuedConnection);
}

void PeakStore::request(const QString& key, bool urgent)
{
    const bool queued = m_pending.contains(key);
    if (queued && (!urgent || m_urgent.contains(key))) {
        return;
    }
    if (!queued) {
        // A cache file is small (~16 KB), so reading it here is quicker than queueing it
        // behind scans of other files.
        if (std::shared_ptr<const PeakData> cached = readCache(cacheDir(), key)) {
            store(key, std::move(cached));
            return;
        }
    }

    std::shared_ptr<std::atomic<bool>>& claim = m_claims[key];
    if (!claim) {
        claim = std::make_shared<std::atomic<bool>>(false);
    }
    m_pending.insert(key);
    if (urgent) {
        m_urgent.insert(key);
    }

    const std::shared_ptr<std::atomic<bool>> cancel = m_cancel;
    const std::shared_ptr<std::atomic<bool>> taken = claim;
    const QString dir = cacheDir();
    // `this` outlives every task: shutdown() waits for the pool before the store goes away,
    // and a result queued to a deleted store is dropped by Qt.
    m_pool->start([this, key, dir, cancel, taken]() {
        if (taken->exchange(true)) {
            return; // done already: the other queued copy ran, or the cache file arrived
        }
        QThread::currentThread()->setPriority(QThread::LowPriority);
        // Stamp before decoding, so a file that changes mid-scan is scanned again next time.
        const SourceStamp stamp = stampOf(key);
        qDebug() << "PeakStore: scanning" << key; // TEMP: remove once the cache is confirmed
        const WaveformPeaks raw = OpenALSoundPlayer::computePeaks(
            std::filesystem::path(key.toStdString()), kBins, cancel.get());
        if (cancel->load()) {
            return;
        }
        auto data = std::make_shared<PeakData>();
        data->ok = raw.ok;
        data->mins = raw.mins;
        data->maxs = raw.maxs;
        writeCache(dir, key, stamp, *data);
        std::shared_ptr<const PeakData> result = std::move(data);
        QMetaObject::invokeMethod(this, [this, key, result]() {
            store(key, result);
        }, Qt::QueuedConnection);
    }, urgent ? kUrgentPriority : kNormalPriority);
}

void PeakStore::store(const QString& key, std::shared_ptr<const PeakData> data)
{
    if (const auto claim = m_claims.constFind(key); claim != m_claims.constEnd() && claim.value()) {
        claim.value()->store(true); // any scan still queued for this file can skip it
    }
    m_pending.remove(key);
    m_urgent.remove(key);
    m_claims.remove(key);
    if (m_peaks.contains(key)) {
        return; // keep the copy callers already have
    }
    m_peaks.insert(key, std::move(data));
    emit peaksReady(key);
}

PeakStore::CacheStats PeakStore::cacheStats() const
{
    CacheStats stats;
    const QString dir = cacheDir();
    if (dir.isEmpty()) {
        return stats;
    }
    const QFileInfoList files = QDir(dir).entryInfoList({QStringLiteral("*.peaks")}, QDir::Files);
    for (const QFileInfo& file : files) {
        ++stats.files;
        stats.bytes += file.size();
    }
    return stats;
}

PeakStore::CacheStats PeakStore::clearCache()
{
    // Stop scans first so none writes a file after the folder is emptied. Scans check the
    // flag while decoding, so the one in progress stops quickly.
    m_cancel->store(true);
    m_pool->clear();
    m_pool->waitForDone();
    m_cancel = std::make_shared<std::atomic<bool>>(false);

    // Only our own files: the folder could be shared by something else one day.
    CacheStats removed;
    const QString dir = cacheDir();
    if (!dir.isEmpty()) {
        const QFileInfoList files = QDir(dir).entryInfoList({QStringLiteral("*.peaks")}, QDir::Files);
        for (const QFileInfo& file : files) {
            const qint64 size = file.size();
            if (QFile::remove(file.absoluteFilePath())) {
                ++removed.files;
                removed.bytes += size;
            }
        }
    }

    m_peaks.clear();
    m_bars.clear();
    m_pending.clear();
    m_urgent.clear();
    m_claims.clear();
    emit cacheCleared();
    return removed;
}
