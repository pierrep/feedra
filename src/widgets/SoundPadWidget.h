#pragma once

#include "Pad.h"

#include <QAbstractButton>
#include <QJsonObject>
#include <QPixmap>
#include <QVector>
#include <QPoint>
#include <QWidget>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

class AppConfig;
class QLabel;
class QLineEdit;
class QSlider;
class QVariantAnimation;

// Pointer with a round accent badge, shown while dragging onto pads: four arrows to move a
// pad, a plus to copy a pad or add files to one, two opposing arrows to replace a pad's
// sounds with dropped files.
enum class DragBadge { Move, Plus, Replace };
QPixmap feedraDragCursor(DragBadge badge, qreal dpr);

class GlyphButton : public QAbstractButton
{
public:
    enum class Kind { Load, Play, Stop, Loop };
    explicit GlyphButton(Kind kind, QWidget* parent = nullptr);
    ~GlyphButton() override;
    void setLoaded(bool loaded);
    void setPlaying(bool playing);
    void setArmed(bool armed);
    // Stop glyph opacity. Other kinds stay at 0. The load folder waits on this so it
    // does not take clicks while the stop glyph is still fading out.
    qreal fadeOpacity() const { return m_fadeOpacity; }
    void setOnFadeSettled(std::function<void()> callback) { m_onFadeSettled = std::move(callback); }

protected:
    void paintEvent(QPaintEvent* event) override;
    QSize sizeHint() const override;

private:
    void animateFade(qreal target);

    Kind m_kind;
    bool m_loaded = false;
    bool m_playing = false;
    bool m_armed = false;
    qreal m_fadeOpacity = 0.0;
    QVariantAnimation* m_fade = nullptr;
    std::function<void()> m_onFadeSettled;
};

// The pad's waveform strip: one bar per peak bin, lit up to the playhead. Click or drag to seek.
class PadPlayhead : public QWidget
{
public:
    static constexpr int kBars = 30;

    explicit PadPlayhead(QWidget* parent = nullptr);
    void setProgress(float pct);
    void setDelayMode(bool delay);
    void setPlaying(bool playing);
    void setTimeText(const QString& text);
    void setPeaks(const QVector<float>& peaks);
    bool hasPeaks() const { return !m_peaks.isEmpty(); }

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    friend class SoundPadWidget;
    float m_progress = 0.0f;
    bool m_delay = false;
    bool m_playing = false;
    QString m_time;
    QVector<float> m_peaks;
    std::function<void(float)> m_onScrub;
};

// One pad on the grid: draws a Pad and turns clicks, drags and drops into changes to it. The
// pad itself (samples, settings, player) lives in the Pad, owned by the scene.
class SoundPadWidget : public QWidget
{
    Q_OBJECT
public:
    // Smallest size the grid gives a pad. Contents are laid out on a 176x204 canvas and scaled.
    static constexpr int kDesignWidth = 112;
    static constexpr int kDesignHeight = 130;
    static constexpr int kLayoutWidth = 176;
    static constexpr int kLayoutHeight = 204;

    SoundPadWidget(AppConfig* config, Pad* pad, QWidget* parent = nullptr);
    ~SoundPadWidget() override;

    Pad* pad() const { return m_pad; }
    int padId() const;

    // Once per UI tick, after the pad's own tick. Does nothing while hidden.
    void updateView();
    void setSelected(bool selected);
    void setInteractive(bool enabled);
    // The key that plays this pad, drawn as a small key cap (empty: none).
    void setKeyHint(const QString& key);
    // Right-click offers MIDI learn and forget (when Feedra has MIDI).
    void setMidiMenu(bool on) { m_midiMenu = on; }

signals:
    void padClicked(int padId);
    void padDragStarted(int padId);
    // `copy` is true for Ctrl+drag (copy the pad), false for a plain drag (move it).
    void padDropped(int fromPadId, int toPadId, bool copy);
    void requestEdit();
    // `volume`: learn a fader for the pad's volume; otherwise a pad or key to play it.
    void midiLearnRequested(int padId, bool volume);
    void midiForgetRequested(int padId);
    void editStarting(Pad* pad);
    void edited(Pad* pad, const QString& what);


protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void showEvent(QShowEvent* event) override;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    void chooseFiles();
    void updateVolumeLabel();
    QString remainingTimeText() const;
    QString positionTimeText() const;
    void refreshPeaks();
    void refreshChrome();
    // Controls, playhead and readouts from the pad's state.
    void refreshUiState();
    void onPadChanged();
    void beginNameEdit();
    void endNameEdit();
    QRect padCardRect() const;
    qreal contentScale() const;
    void layoutContents();

    AppConfig* m_config = nullptr;
    Pad* m_pad = nullptr;
    bool m_ctrlClickPending = false; // Ctrl held at press: toggles the scene pad mark on release unless it becomes a drag
    bool m_selected = false;
    QString m_keyHint;
    bool m_midiMenu = false;
    bool m_uiLoaded = false;
    bool m_uiLoading = false;
    bool m_uiPlaying = false;
    bool m_uiDelay = false;
    int m_uiLayerLevel = -1; // the layer chip's brightness, in steps, so it repaints as the slider moves
    QString m_timeText;
    QString m_peakPath;
    QString m_volumeStyle;
    QString m_nameBeforeEdit;
    bool m_compactText = false; // shortened time and dB readouts on small pads

    QWidget* m_card = nullptr;
    GlyphButton* m_load = nullptr;
    GlyphButton* m_stop = nullptr;
    GlyphButton* m_loop = nullptr;
    GlyphButton* m_play = nullptr;
    PadPlayhead* m_playhead = nullptr;
    QSlider* m_volume = nullptr;
    QLabel* m_volumeValue = nullptr;
    QLineEdit* m_name = nullptr;
    QPoint m_dragStart;
    // True while a press is held on the volume bar outside the handle. That drag is
    // not a pad move; dragging the handle still changes the level.
    bool m_volumeDragIgnored = false;
    bool m_fileDragOk = false; // the file drag over this pad carries something playable
};
