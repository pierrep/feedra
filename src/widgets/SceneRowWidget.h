#pragma once

#include <QWidget>

class QLineEdit;
class QAbstractButton;
class QTimer;

class SceneRowWidget : public QWidget
{
    Q_OBJECT
public:
    explicit SceneRowWidget(int sceneId, const QString& name, QWidget* parent = nullptr);

    static QString dragMimeType();

    int sceneId() const { return m_id; }
    QString sceneName() const;
    void setSceneName(const QString& name);
    void setActive(bool active);
    void setPlaying(bool playing);
    void setInteractive(bool enabled);

signals:
    void selected(int sceneId);
    void playPauseRequested(int sceneId);
    void stopRequested(int sceneId);
    void deleteRequested(int sceneId);
    void nameChanged(int sceneId, const QString& name);

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void beginEditing();
    void finishEditing();
    void startDragIfMoved(const QPoint& pos);
    void refreshTools();

    int m_id = 0;
    QPoint m_pressPos;
    bool m_playing = false;
    QLineEdit* m_name = nullptr;
    QAbstractButton* m_play = nullptr;
    QAbstractButton* m_stop = nullptr;
    QAbstractButton* m_remove = nullptr;
    QTimer* m_meterTimer = nullptr;
    int m_meterFrame = 0;
};
