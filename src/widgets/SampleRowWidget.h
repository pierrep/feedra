#pragma once

#include <QWidget>

class QFrame;
class QLabel;
class QProgressBar;

class SampleRowWidget : public QWidget
{
    Q_OBJECT
public:
    explicit SampleRowWidget(int sampleId, const QString& path, QWidget* parent = nullptr);

    int sampleId() const { return m_id; }
    void setProgress(float pct);
    void setSelected(bool selected);

signals:
    void clicked(int sampleId);

protected:
    void mousePressEvent(QMouseEvent* event) override;
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

class SampleListHost : public QWidget
{
    Q_OBJECT
public:
    explicit SampleListHost(QWidget* parent = nullptr);

signals:
    void sampleReordered(int sampleId, int insertIndex);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    int insertionIndexAt(const QPoint& pos) const;
    void placeIndicator(int insertIndex);
    void hideIndicator();

    QFrame* m_indicator = nullptr;
};
