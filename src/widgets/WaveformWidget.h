#pragma once

#include <QHash>
#include <QPixmap>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <atomic>
#include <memory>
#include <vector>

class QThreadPool;

// Whole-file waveform of one sample with a moving playhead.
//
// Peaks are computed once per file on a low-priority background thread with its own
// decoder, then cached. The widget only reads positions the UI thread already polls,
// so drawing it never waits on, or holds up, the audio stream threads.
class WaveformWidget : public QWidget
{
    Q_OBJECT
public:
    explicit WaveformWidget(QWidget* parent = nullptr);
    ~WaveformWidget() override;

    // Empty path clears the display. `durationSec` is used for the time readout.
    void setSample(const QString& path, float durationSec);
    // `pct` is 0..1 of the file, or negative when the position is unknown.
    void setPlayhead(float pct, bool playing);

signals:
    // Click or drag on the waveform: 0..1 of the file.
    void seekRequested(float pct);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    struct Peaks {
        bool ok = false;
        std::vector<float> mins;
        std::vector<float> maxs;
    };

    void requestPeaks(const QString& path);
    void onPeaksReady(const QString& path, std::shared_ptr<const Peaks> peaks);
    void rebuildPixmap();
    QRect waveRect() const;
    bool canSeek() const;
    void seekAt(int x);
    static QString formatTime(float seconds);

    QString m_path;
    float m_duration = 0.0f;
    float m_playhead = -1.0f;
    bool m_playing = false;
    bool m_dragging = false;

    QHash<QString, std::shared_ptr<const Peaks>> m_cache;
    QStringList m_cacheOrder;
    QStringList m_pending;

    QPixmap m_wave;
    bool m_waveDirty = true;

    QThreadPool* m_pool = nullptr;
    std::shared_ptr<std::atomic<bool>> m_cancel = std::make_shared<std::atomic<bool>>(false);
};
