#pragma once

#include <QWidget>

class QLineEdit;
class QPushButton;
class QToolButton;

class SceneRowWidget : public QWidget
{
    Q_OBJECT
public:
    explicit SceneRowWidget(int sceneId, const QString& name, QWidget* parent = nullptr);

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
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void beginEditing();
    void finishEditing();

    int m_id = 0;
    QLineEdit* m_name = nullptr;
    QPushButton* m_play = nullptr;
    QPushButton* m_stop = nullptr;
    QPushButton* m_remove = nullptr;
};
