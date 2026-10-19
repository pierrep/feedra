#pragma once

#include "widgets/SoundPadWidget.h"
#include "widgets/SceneRowWidget.h"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QSet>
#include <QVector>
#include <functional>

class AppConfig;
class QWidget;

class Scene : public QObject
{
    Q_OBJECT
public:
    Scene(AppConfig* config, int id, const QString& name, QWidget* gridParent, QWidget* listParent, QObject* parent = nullptr);
    ~Scene() override;

    int id = 0;
    int activeSoundIdx = 0;
    QString name;
    bool isPlaying = false;
    bool selectRequested = false;

    QWidget* grid() const { return m_grid; }
    SceneRowWidget* row() const { return m_row; }
    // The pads, and the grid widgets that show them (same order, same ids).
    QVector<Pad*> pads;
    QVector<SoundPadWidget*> padWidgets;

    Pad* padAt(int idx) const;
    int columns() const { return m_columns; }
    int rows() const { return m_rows; }
    SoundPadWidget* padWidgetAt(int idx) const;
    // The fade for play, pause and stop from the scene's own buttons.
    static constexpr int kDefaultFadeMs = 500;

    // Pads resuming from a pause fade in; pads starting from the top start at full level (a
    // fade would swallow their opening). With `fadeInStarts`, as in a crossfade, every pad
    // that starts fades in too.
    void play(int fadeMs = kDefaultFadeMs, bool fadeInStarts = false);
    void pause(int fadeMs = kDefaultFadeMs);
    // `crossfade`: part of a crossfade, so selecting another scene doesn't cut it short.
    void stop(int fadeMs = kDefaultFadeMs, bool crossfade = false);
    void update();
    void endFade();
    // Fading in or out as part of a crossfade (MainWindow::goToScene).
    bool isCrossfading() const { return m_fading && m_crossfade; }
    // Any pad playing, as of the last update().
    bool isAudible() const { return m_audible; }
    // The fade's gain at `phase` (0 silent, 1 full): equal power, so two scenes crossing keep
    // the same loudness, and a fade sounds even all the way down.
    static float fadeGain(float phase);
    void setActive(bool active);
    void layoutGrid();

    // The Intensity slider (0 calmest .. 1 most intense), crossfading the pads that have a
    // layer (Intensity.h). What's heard glides to a new setting over Intensity::kGlideSeconds
    // for the full range; `glide` false jumps (opening a project).
    float intensity() const { return m_intensity; }
    void setIntensity(float intensity, bool glide = true);
    // Where the glide has got to: what's being heard.
    float heardIntensity() const { return m_heardIntensity; }
    // The layers the scene's pads use (bit Intensity::Layer), as of the last update().
    int layerMask() const { return m_layerMask; }
    // The scene's own settings in its project object ("intensity", written when not 0).
    void writeSettings(QJsonObject& sceneObj) const;
    void readSettings(const QJsonObject& sceneObj);
    void stopImmediate();

signals:
    void padSelected(int sceneId, int padId);
    void goRequested(int sceneId);

private:
    AppConfig* m_config = nullptr;
    int m_columns = 1;
    int m_rows = 1;
    QWidget* m_grid = nullptr;
    SceneRowWidget* m_row = nullptr;
    bool m_fading = false;
    bool m_crossfade = false;
    bool m_audible = false;
    int m_fadeDirection = 0;
    int m_fadeMs = kDefaultFadeMs;
    float m_fadePhase = 1.0f; // 0 silent .. 1 full; the gain is fadeGain(phase)
    float m_fadeVolume = 1.0f;
    float m_phaseFrom = 1.0f; // phase when the current fade began, so a reversed fade doesn't jump
    QElapsedTimer m_fadeTimer;
    std::function<void()> m_fadeCallback;
    // The pads that fade in when the scene plays: those carrying on from a pause, and in a
    // crossfade every pad that starts.
    QSet<Pad*> m_resuming;
    int m_scenePadCount = -1; // last count shown in the play button's tooltip
    float m_intensity = 0.0f;
    float m_heardIntensity = 0.0f;
    int m_layerMask = 0;
    QElapsedTimer m_intensityClock;
    void updateLayers();
};
