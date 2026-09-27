#pragma once

#include <QPixmap>
#include <QString>
#include <QWidget>

class QCheckBox;
class QSpinBox;

// Whole-file waveform of one sample with a moving playhead, loop start/end handles and the
// sample's Loop and Play-from-start switches.
//
// Peaks come from PeakStore, which reads each file once on a low-priority background thread
// with its own decoder. The widget only reads positions the UI thread already polls, so
// drawing it never waits on, or holds up, the audio stream threads.
class WaveformWidget : public QWidget
{
    Q_OBJECT
public:
    enum class Handle { None, Start, End };

    explicit WaveformWidget(QWidget* parent = nullptr);
    ~WaveformWidget() override;

    // Empty path clears the display. `durationSec` is used for the time readout.
    void setSample(const QString& path, float durationSec);
    // `pct` is 0..1 of the file, or negative when the position is unknown.
    void setPlayhead(float pct, bool playing);

    // The shown sample's loop settings, as fractions of the file.
    struct LoopView {
        double startPct = 0.0;
        double endPct = 1.0;
        bool loop = false;
        bool playFromStart = true;
        bool playToEnd = true;
        int crossfadeMs = 10;      // the sample's setting
        double crossfadePct = 0.0; // effective crossfade length (at most half the loop)
        bool operator==(const LoopView& o) const
        {
            return startPct == o.startPct && endPct == o.endPct && loop == o.loop
                && playFromStart == o.playFromStart && playToEnd == o.playToEnd && crossfadeMs == o.crossfadeMs
                && crossfadePct == o.crossfadePct;
        }
    };
    // Ignored while a handle is being dragged, so the drag isn't pulled back.
    void setLoopView(const LoopView& view);
    bool isDraggingHandle() const { return m_dragHandle != Handle::None; }

signals:
    // Click or drag on the waveform: 0..1 of the file.
    void seekRequested(float pct);
    // A handle moved. `finished` is false during the drag and true on release (or reset).
    void loopPointsChanged(double startPct, double endPct, bool finished);
    void loopToggled(bool on);
    void playFromStartToggled(bool on);
    void playToEndToggled(bool on);
    void crossfadeChanged(int ms);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void rebuildPixmap();
    QRect waveRect() const;
    bool canSeek() const;
    void seekAt(int x);
    static QString formatTime(float seconds);
    static QString formatPrecise(double seconds);

    Handle handleAt(const QPoint& pos) const;
    int xFor(double pct) const;
    double pctAt(int x) const;
    double minLoopPct() const;
    void dragHandle(int x);
    void resetHandles(bool start, bool end);
    void layoutToggles();
    bool hasCustomRegion() const;

    QString m_path;
    float m_duration = 0.0f;
    float m_playhead = -1.0f;
    bool m_playing = false;
    bool m_dragging = false;
    Handle m_dragHandle = Handle::None;
    Handle m_hoverHandle = Handle::None;
    LoopView m_loop;

    QCheckBox* m_loopBox = nullptr;
    QCheckBox* m_fromStartBox = nullptr;
    QCheckBox* m_toEndBox = nullptr;
    QSpinBox* m_xfBox = nullptr;

    QPixmap m_wave;
    QPixmap m_waveDim;
    bool m_waveDirty = true;
};
