#pragma once

#include "SoundPlayer.h"

#include <QAbstractButton>
#include <QJsonObject>
#include <QPoint>
#include <QWidget>
#include <functional>
#include <string>
#include <vector>

class AppConfig;
class QLabel;
class QLineEdit;
class QSlider;

class GlyphButton : public QAbstractButton
{
public:
    enum class Kind { Load, Play, Stop, Loop };
    explicit GlyphButton(Kind kind, QWidget* parent = nullptr);
    void setLoaded(bool loaded);
    void setPlaying(bool playing);
    void setArmed(bool armed);

protected:
    void paintEvent(QPaintEvent* event) override;
    QSize sizeHint() const override;

private:
    Kind m_kind;
    bool m_loaded = false;
    bool m_playing = false;
    bool m_armed = false;
};

class PadPlayhead : public QWidget
{
public:
    explicit PadPlayhead(QWidget* parent = nullptr);
    void setProgress(float pct);
    void setDelayMode(bool delay);
    void setTimeText(const QString& text);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    friend class SoundPadWidget;
    float m_progress = 0.0f;
    bool m_delay = false;
    QString m_time;
    std::function<void(float)> m_onScrub;
};

class SoundPadWidget : public QWidget
{
    Q_OBJECT
public:
    static constexpr int kDesignWidth = 124;
    static constexpr int kDesignHeight = 142;

    SoundPadWidget(AppConfig* config, int sceneId, int padId, QWidget* parent = nullptr);

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
    void loadFromJson(const QJsonObject& sceneObj);
    void saveToJson(QJsonObject& sceneObj) const;
    void loadFiles(const QStringList& paths, bool clearExisting);
    void clearPad();
    void copyFrom(SoundPadWidget& other);
    void removeSampleAt(int index);
    int moveSample(int from, int insertIndex);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void padClicked(int padId);
    void padDragStarted(int padId);
    void padDropped(int fromPadId, int toPadId);
    void filesDropped();
    void requestEdit();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void chooseFiles();
    bool loadSingleSound(const QString& path, bool clearExisting);
    void setupLoadedSound(const QString& path);
    void applyVolume();
    void updateVolumeLabel();
    QString remainingTimeText() const;
    QRect padCardRect() const;
    qreal contentScale() const;
    void layoutContents();

    AppConfig* m_config = nullptr;
    SoundPlayer m_player;
    int m_sceneId = 0;
    int m_padId = 0;
    bool m_stream = true;
    int m_sampleRate = 0;
    int m_channels = 0;
    float m_fadeVolume = 1.0f;
    bool m_selected = false;
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
};
