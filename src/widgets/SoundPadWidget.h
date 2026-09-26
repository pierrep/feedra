#pragma once

#include "SoundPlayer.h"

#include <QAbstractButton>
#include <QJsonObject>
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
class SampleLoadQueue;
struct SampleLoadJob;

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

class SoundPadWidget : public QWidget
{
    Q_OBJECT
public:
    // Smallest size the grid gives a pad. Contents are laid out on a 176x204 canvas and scaled.
    static constexpr int kDesignWidth = 112;
    static constexpr int kDesignHeight = 130;
    static constexpr int kLayoutWidth = 176;
    static constexpr int kLayoutHeight = 204;

    SoundPadWidget(AppConfig* config, int sceneId, int padId, QWidget* parent = nullptr);
    ~SoundPadWidget() override;

    int padId() const { return m_padId; }
    int sceneId() const { return m_sceneId; }
    SoundPlayer& soundPlayer() { return m_player; }
    const SoundPlayer& soundPlayer() const { return m_player; }

    QString soundName() const;
    void setSoundName(const QString& name);
    float padVolume() const;
    void setPadVolume(float value);
    bool isLooping() const;
    void setLooping(bool looping);
    bool isLoaded() const;
    bool isPlaying() const;
    int sampleRate() const { return m_sampleRate; }
    int channels() const { return m_channels; }
    const std::vector<std::string>& soundPaths() const { return m_soundPaths; }

    void updateAudio();
    void setSelected(bool selected);
    void setInteractive(bool enabled);
    void setFadeVolume(float fade);
    void setLoadQueue(SampleLoadQueue* queue);
    bool isLoading() const;
    void submitDecoded(int index, int generation, DecodedAudio audio);
    void setSpatialisedStereo(int index, bool on);
    bool isSpatialisedStereo(int index) const;
    void submitReload(const SampleLoadJob& job, DecodedAudio audio);
    void loadFromJson(const QJsonObject& root);
    void saveToJson(QJsonObject& sceneObj) const;
    void loadFiles(const QStringList& paths, bool clearExisting);
    void setReverbSend(float send);
    void setReverbSend2(float send);
    void clearPad();
    void copyFrom(SoundPadWidget& other);
    void removeSampleAt(int index);
    int moveSample(int from, int insertIndex);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    // Path of the sample the pad would play next, or empty if it has none.
    QString currentSamplePath() const;

signals:
    void padClicked(int padId);
    void padDragStarted(int padId);
    // `copy` is true for Ctrl+drag (copy the pad), false for a plain drag (move it).
    void padDropped(int fromPadId, int toPadId, bool copy);
    void filesDropped();
    void requestEdit();
    void loadStateChanged();
    void loadingFinished();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    struct LoadSlot {
        QString path;
        float pitch = 1.0f;
        float gain = 1.0f;
        float pan = 0.0f;
        bool panRandom = false;
        bool spatialise = false;
        bool failed = false;
    };

    struct PendingReload {
        int serial = 0;
        bool spatialise = false;
    };

    void chooseFiles();
    void cancelLoading();
    void enqueueSample(const QString& path, float pitch, float gain, float pan, bool panRandom, bool spatialise);
    bool commitDecoded(int slotIndex, DecodedAudio audio);
    void flushIncoming();
    std::vector<LoadSlot> sampleSpecs() const;
    void setupLoadedSound(const QString& path);
    void applyVolume();
    void updateVolumeLabel();
    QString remainingTimeText() const;
    QString positionTimeText() const;
    void refreshPeaks();
    void refreshChrome();
    void beginNameEdit();
    void endNameEdit();
    QRect padCardRect() const;
    qreal contentScale() const;
    void layoutContents();

    AppConfig* m_config = nullptr;
    SampleLoadQueue* m_queue = nullptr;
    SoundPlayer m_player;
    std::shared_ptr<std::atomic<int>> m_loadGeneration = std::make_shared<std::atomic<int>>(0);
    std::vector<LoadSlot> m_slots;
    std::map<int, DecodedAudio> m_incoming;
    std::map<int, PendingReload> m_reloads; // keyed by AudioSample::id
    int m_reloadSerial = 0;
    int m_loadTotal = 0;
    int m_nextCommit = 0;
    bool m_finishSent = false;
    bool m_notifyWhenDone = false;
    float m_reverb = 0.0f;
    float m_reverb2 = 0.0f;
    int m_sceneId = 0;
    int m_padId = 0;
    bool m_stream = true;
    int m_sampleRate = 0;
    int m_channels = 0;
    float m_fadeVolume = 1.0f;
    bool m_selected = false;
    bool m_uiLoaded = false;
    bool m_uiLoading = false;
    bool m_uiPlaying = false;
    bool m_uiDelay = false;
    QString m_timeText;
    QString m_peakPath;
    QString m_volumeStyle;
    QString m_nameBeforeEdit;
    bool m_compactText = false; // shortened time and dB readouts on small pads
    std::vector<std::string> m_soundPaths;

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
};
