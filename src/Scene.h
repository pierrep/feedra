#pragma once

#include "widgets/SoundPadWidget.h"
#include "widgets/SceneRowWidget.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>
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
    QVector<SoundPadWidget*> pads;

    SoundPadWidget* padAt(int idx) const;
    void play();
    void pause();
    void stop();
    void update();
    void endFade();
    void setActive(bool active);
    void layoutGrid();
    void stopImmediate();

signals:
    void padSelected(int sceneId, int padId);

private:
    AppConfig* m_config = nullptr;
    QWidget* m_grid = nullptr;
    SceneRowWidget* m_row = nullptr;
    bool m_fading = false;
    int m_fadeDirection = 0;
    float m_fadeVolume = 1.0f;
    QElapsedTimer m_fadeTimer;
    std::function<void()> m_fadeCallback;
};
