#include "ReorderListHost.h"

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFrame>
#include <QMimeData>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>
#include <algorithm>

ReorderListHost::ReorderListHost(const QString& mimeType, QWidget* parent)
    : QWidget(parent)
    , m_mimeType(mimeType)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setAcceptDrops(true);

    m_indicator = new QFrame(this);
    m_indicator->setObjectName(QStringLiteral("ListDropIndicator"));
    m_indicator->setAttribute(Qt::WA_StyledBackground, true);
    m_indicator->setFixedHeight(3);
    m_indicator->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_indicator->hide();
}

int ReorderListHost::insertionIndexAt(const QPoint& pos) const
{
    const auto* box = qobject_cast<QVBoxLayout*>(layout());
    if (!box) {
        return 0;
    }
    int index = 0;
    for (int i = 0; i < box->count(); ++i) {
        const QWidget* row = box->itemAt(i)->widget();
        if (!row) {
            continue;
        }
        if (pos.y() < row->geometry().center().y()) {
            return index;
        }
        ++index;
    }
    return index;
}

void ReorderListHost::placeIndicator(int insertIndex)
{
    const auto* box = qobject_cast<QVBoxLayout*>(layout());
    if (!box) {
        return;
    }
    const QMargins margins = box->contentsMargins();
    int y = margins.top();
    int seen = 0;
    for (int i = 0; i < box->count(); ++i) {
        const QWidget* row = box->itemAt(i)->widget();
        if (!row) {
            continue;
        }
        if (seen == insertIndex) {
            y = row->geometry().top() - box->spacing() / 2 - m_indicator->height() / 2;
            break;
        }
        ++seen;
        y = row->geometry().bottom() + box->spacing() / 2 - m_indicator->height() / 2;
    }
    m_indicator->setGeometry(margins.left(), y,
        std::max(0, width() - margins.left() - margins.right()), m_indicator->height());
    m_indicator->raise();
    m_indicator->show();
}

void ReorderListHost::hideIndicator()
{
    m_indicator->hide();
}

void ReorderListHost::dragEnterEvent(QDragEnterEvent* event)
{
    if (!event->mimeData()->hasFormat(m_mimeType)) {
        event->ignore();
        return;
    }
    event->setDropAction(Qt::MoveAction);
    event->accept();
    placeIndicator(insertionIndexAt(event->position().toPoint()));
}

void ReorderListHost::dragMoveEvent(QDragMoveEvent* event)
{
    if (!event->mimeData()->hasFormat(m_mimeType)) {
        event->ignore();
        return;
    }
    event->setDropAction(Qt::MoveAction);
    event->accept();
    const QPoint pos = event->position().toPoint();
    placeIndicator(insertionIndexAt(pos));

    auto* scroll = qobject_cast<QScrollArea*>(parentWidget() ? parentWidget()->parentWidget() : nullptr);
    if (!scroll) {
        return;
    }
    const QPoint viewportPos = mapTo(scroll->viewport(), pos);
    auto* bar = scroll->verticalScrollBar();
    constexpr int kMargin = 28;
    constexpr int kStep = 16;
    if (viewportPos.y() < kMargin) {
        bar->setValue(bar->value() - kStep);
    } else if (viewportPos.y() > scroll->viewport()->height() - kMargin) {
        bar->setValue(bar->value() + kStep);
    }
}

void ReorderListHost::dragLeaveEvent(QDragLeaveEvent* event)
{
    hideIndicator();
    QWidget::dragLeaveEvent(event);
}

void ReorderListHost::dropEvent(QDropEvent* event)
{
    hideIndicator();
    if (!event->mimeData()->hasFormat(m_mimeType)) {
        event->ignore();
        return;
    }
    const int itemId = event->mimeData()->data(m_mimeType).toInt();
    const int insertIndex = insertionIndexAt(event->position().toPoint());
    event->setDropAction(Qt::MoveAction);
    event->accept();
    emit itemReordered(itemId, insertIndex);
}
