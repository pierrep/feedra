#include "SampleRowWidget.h"

#include <QApplication>
#include <QDrag>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QProgressBar>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QStyle>
#include <algorithm>

QString SampleRowWidget::dragMimeType()
{
    return QStringLiteral("application/x-feedra-sample");
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
    mime->setData(dragMimeType(), QByteArray::number(m_id));
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
