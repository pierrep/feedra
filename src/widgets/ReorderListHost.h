#pragma once

#include <QWidget>

class QFrame;

// Hosts a QVBoxLayout of rows and accepts drags carrying mimeType (payload: the dragged item's id).
class ReorderListHost : public QWidget
{
    Q_OBJECT
public:
    explicit ReorderListHost(const QString& mimeType, QWidget* parent = nullptr);

signals:
    void itemReordered(int itemId, int insertIndex);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    int insertionIndexAt(const QPoint& pos) const;
    void placeIndicator(int insertIndex);
    void hideIndicator();

    QString m_mimeType;
    QFrame* m_indicator = nullptr;
};
