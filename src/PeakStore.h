#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVector>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

class QThreadPool;

// Min/max peaks for one audio file, kBins bins across the whole file.
struct PeakData {
    bool ok = false; // false: the file couldn't be decoded
    std::vector<float> mins;
    std::vector<float> maxs;
};

// The one place waveform peaks are computed and kept. Each file is decoded once, on a single
// low-priority background thread, and shared by the Waveform tab and every pad showing it.
// The pad strips' bars are derived from the same peaks rather than scanned separately.
//
// Peaks are also saved to a cache folder (setCacheDir), one small file per sample, and read
// back on the next launch instead of decoding again. A cache file records the sample's size
// and modified time; if either has changed the sample is scanned again and the file replaced.
//
// Use from the UI thread only, except preload(). Create it (instance()) on the UI thread
// before any other thread can reach it.
class PeakStore : public QObject
{
    Q_OBJECT
public:
    static constexpr int kBins = 4096; // resolution kept per file; ~16 KB on disk, ~32 KB in memory

    static PeakStore& instance();

    // The key peaks are stored under: the absolute, cleaned path. peaksReady() reports this.
    static QString keyFor(const QString& path);

    // Folder for cache files (created on first write). Empty turns the disk cache off.
    void setCacheDir(const QString& dir);

    // The peaks for `path`, or null while they're being read. The first call for a file checks
    // the disk cache, then queues a scan if there is no valid cache file; `urgent` puts it ahead
    // of queued files (for what's on screen in the Waveform tab).
    std::shared_ptr<const PeakData> get(const QString& path, bool urgent = false);

    // `count` bars of |peak|, normalised to the loudest, for the pad strips. Empty while the
    // file is being read; a file that can't be decoded gives a flat strip.
    QVector<float> bars(const QString& path, int count);

    // Any thread. Reads the cache file for `path` if there is a valid one and hands it to the
    // store; never scans. Called by the sample loader so the waveform arrives with the sample.
    void preload(const QString& path);

    struct CacheStats {
        int files = 0;
        qint64 bytes = 0;
    };
    // What the cache folder holds now.
    CacheStats cacheStats() const;
    // Deletes every cache file and forgets all peaks in memory, then emits cacheCleared().
    // Waveforms on screen are read again (and saved again) as they're next shown.
    // Returns what was deleted.
    CacheStats clearCache();

signals:
    // Peaks for `key` (see keyFor) are ready: get() and bars() now return them.
    void peaksReady(const QString& key);
    // Everything was forgotten (clearCache): views should ask for their peaks again.
    void cacheCleared();

private:
    explicit PeakStore(QObject* parent);
    ~PeakStore() override;

    QString cacheDir() const;
    void request(const QString& key, bool urgent);
    void store(const QString& key, std::shared_ptr<const PeakData> data);
    void shutdown();

    QHash<QString, std::shared_ptr<const PeakData>> m_peaks;
    QHash<QString, QVector<float>> m_bars; // key: count + '|' + key
    QSet<QString> m_pending;
    QSet<QString> m_urgent;
    // One flag per queued file. An urgent request queues a second task for the same file, and
    // a cache read can arrive while a scan is queued; whichever gets there first sets the flag
    // and queued tasks that find it set do nothing.
    QHash<QString, std::shared_ptr<std::atomic<bool>>> m_claims;
    QThreadPool* m_pool = nullptr;
    // Cancels scans in progress. Replaced (not reset) by clearCache so old tasks stay cancelled.
    std::shared_ptr<std::atomic<bool>> m_cancel = std::make_shared<std::atomic<bool>>(false);
    // Set on quit; read by preload() on loader threads (m_cancel itself is UI-thread only).
    std::atomic<bool> m_quitting{false};

    mutable std::mutex m_dirMutex;
    QString m_cacheDir;
};
