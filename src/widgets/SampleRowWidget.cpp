#include "SampleRowWidget.h"

#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QProgressBar>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizePolicy>
#include <QStyle>
#include <QVBoxLayout>
#include <algorithm>

namespace {
const QString kSampleReorderMime = QStringLiteral("application/x-feedra-sample");
}

SampleRowWidget::SampleRowWidget(int sampleId, const QString& path, QWidget* parent)
    : QWidget(parent)
    , m_id(sampleId)
{
    setObjectName("SampleRow");
    setAttribute(Qt::WA_StyledBackground, true);
    setCursor(Qt::OpenHandCursor);
    setMinimumHeight(36);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(4);

    auto* grip = new QLabel(QStringLiteral("\u22EE\u22EE"), this);
    grip->setObjectName(QStringLiteral("SampleGrip"));
    grip->setAttribute(Qt::WA_TransparentForMouseEvents);
    grip->setFixedWidth(14);
    grip->setAlignment(Qt::AlignCenter);

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 1000);
    m_progress->setValue(0);
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(28);
    m_progress->setMinimumWidth(0);
    m_progress->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_progress->setAttribute(Qt::WA_TransparentForMouseEvents);

    m_text = QFileInfo(path).fileName() + QString("  id = %1").arg(sampleId);
    m_label = new QLabel(m_text, this);
    m_label->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);

    auto* overlay = new QHBoxLayout(m_progress);
    overlay->setContentsMargins(12, 0, 12, 0);
    overlay->addWidget(m_label);

    layout->addWidget(grip);
    layout->addWidget(m_progress);
}

void SampleRowWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateElidedText();
}

void SampleRowWidget::updateElidedText()
{
    const int width = std::max(0, m_progress->width() - 24);
    m_label->setText(m_label->fontMetrics().elidedText(m_text, Qt::ElideMiddle, width));
}

void SampleRowWidget::setProgress(float pct)
{
    m_progress->setValue(static_cast<int>(std::clamp(pct, 0.0f, 1.0f) * 1000.0f));
}

void SampleRowWidget::setSelected(bool selected)
{
    setProperty("selected", selected);
    style()->unpolish(this);
    style()->polish(this);
}

void SampleRowWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressPos = event->pos();
        m_dragging = false;
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void SampleRowWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (!(event->buttons() & Qt::LeftButton)) {
        return;
    }
    if ((event->pos() - m_pressPos).manhattanLength() < QApplication::startDragDistance()) {
        return;
    }

    m_dragging = true;
    auto* drag = new QDrag(this);
    auto* mime = new QMimeData();
    mime->setData(kSampleReorderMime, QByteArray::number(m_id));
    drag->setMimeData(mime);
    drag->setPixmap(grab());
    drag->setHotSpot(m_pressPos);
    drag->exec(Qt::MoveAction);
}

void SampleRowWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && !m_dragging) {
        emit clicked(m_id);
    }
    m_dragging = false;
    QWidget::mouseReleaseEvent(event);
}

SampleListHost::SampleListHost(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("SampleListHost"));
    setAttribute(Qt::WA_StyledBackground, true);
    setAcceptDrops(true);

    m_indicator = new QFrame(this);
    m_indicator->setObjectName(QStringLiteral("SampleDropIndicator"));
    m_indicator->setAttribute(Qt::WA_StyledBackground, true);
    m_indicator->setFixedHeight(3);
    m_indicator->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_indicator->hide();
}

int SampleListHost::insertionIndexAt(const QPoint& pos) const
{
    const auto* box = qobject_cast<QVBoxLayout*>(layout());
    if (!box) {
        return 0;
    }
    int index = 0;
    for (int i = 0; i < box->count(); ++i) {
        auto* row = qobject_cast<SampleRowWidget*>(box->itemAt(i)->widget());
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

void SampleListHost::placeIndicator(int insertIndex)
{
    const auto* box = qobject_cast<QVBoxLayout*>(layout());
    if (!box) {
        return;
    }
    const QMargins margins = box->contentsMargins();
    int y = margins.top();
    int seen = 0;
    for (int i = 0; i < box->count(); ++i) {
        auto* row = qobject_cast<SampleRowWidget*>(box->itemAt(i)->widget());
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

void SampleListHost::hideIndicator()
{
    m_indicator->hide();
}

void SampleListHost::dragEnterEvent(QDragEnterEvent* event)
{
    if (!event->mimeData()->hasFormat(kSampleReorderMime)) {
        event->ignore();
        return;
    }
    event->setDropAction(Qt::MoveAction);
    event->accept();
    placeIndicator(insertionIndexAt(event->position().toPoint()));
}

void SampleListHost::dragMoveEvent(QDragMoveEvent* event)
{
    if (!event->mimeData()->hasFormat(kSampleReorderMime)) {
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

void SampleListHost::dragLeaveEvent(QDragLeaveEvent* event)
{
    hideIndicator();
    QWidget::dragLeaveEvent(event);
}

void SampleListHost::dropEvent(QDropEvent* event)
{
    hideIndicator();
    if (!event->mimeData()->hasFormat(kSampleReorderMime)) {
        event->ignore();
        return;
    }
    const int sampleId = event->mimeData()->data(kSampleReorderMime).toInt();
    const int insertIndex = insertionIndexAt(event->position().toPoint());
    event->setDropAction(Qt::MoveAction);
    event->accept();
    emit sampleReordered(sampleId, insertIndex);
}
