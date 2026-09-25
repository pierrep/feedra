#pragma once

#include <QWidget>

class QLabel;
class QProgressBar;

class SampleRowWidget : public QWidget
{
    Q_OBJECT
public:
    explicit SampleRowWidget(int sampleId, const QString& path, QWidget* parent = nullptr);

    static QString dragMimeType();

    int sampleId() const { return m_id; }
    void setProgress(float pct);
    void setSelected(bool selected);

signals:
    void clicked(int sampleId);
    void doubleClicked(int sampleId);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void updateElidedText();

    int m_id = 0;
    QPoint m_pressPos;
    bool m_dragging = false;
    QString m_text;
    QLabel* m_label = nullptr;
    QProgressBar* m_progress = nullptr;
};
