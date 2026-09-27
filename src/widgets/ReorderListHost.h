#pragma once

#include <QWidget>

class QFrame;
class QMimeData;

// Hosts a QVBoxLayout of rows and accepts drags carrying mimeType (payload: the dragged item's id).
class ReorderListHost : public QWidget
{
    Q_OBJECT
public:
    explicit ReorderListHost(const QString& mimeType, QWidget* parent = nullptr);

    // Also accept dragged audio files (from the Files tab or a file manager). They are
    // always added after the last row, and the drop line shows there.
    void setAcceptsFiles(bool accept) { m_acceptsFiles = accept; }

signals:
    void itemReordered(int itemId, int insertIndex);
    void filesDropped(const QStringList& paths);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    int insertionIndexAt(const QPoint& pos) const;
    void placeIndicator(int insertIndex);
    void hideIndicator();

    bool isFileDrag(const QMimeData* mime) const;
    int rowCount() const;

    QString m_mimeType;
    bool m_acceptsFiles = false;
    bool m_fileDragOk = false;
    QFrame* m_indicator = nullptr;
};
