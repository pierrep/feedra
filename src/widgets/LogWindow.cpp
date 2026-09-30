#include "LogWindow.h"
#include "LogCapture.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QFont>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

LogWindow::LogWindow(QWidget* parent)
    : QWidget(parent, Qt::Window)
{
    setObjectName(QStringLiteral("LogWindow"));
    setWindowTitle(tr("Feedra — Logs"));
    resize(900, 480);

    m_text = new QPlainTextEdit(this);
    m_text->setObjectName(QStringLiteral("LogText"));
    m_text->setReadOnly(true);
    m_text->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_text->setMaximumBlockCount(20000);
    m_text->setUndoRedoEnabled(false);
    QFont mono(QStringLiteral("Geist Mono"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSizeF(9.0);
    m_text->setFont(mono);

    m_follow = new QCheckBox(tr("Follow new output"), this);
    m_follow->setChecked(true);
    connect(m_follow, &QCheckBox::toggled, this, [this](bool on) {
        if (on) {
            m_text->verticalScrollBar()->setValue(m_text->verticalScrollBar()->maximum());
        }
    });
    auto* copy = new QPushButton(tr("Copy all"), this);
    connect(copy, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(m_text->toPlainText());
    });
    auto* clear = new QPushButton(tr("Clear"), this);
    connect(clear, &QPushButton::clicked, this, [this]() { m_text->clear(); });

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(m_follow);
    buttons->addStretch(1);
    buttons->addWidget(copy);
    buttons->addWidget(clear);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);
    layout->addWidget(m_text, 1);
    layout->addLayout(buttons);

    m_timer = new QTimer(this);
    m_timer->setInterval(250);
    connect(m_timer, &QTimer::timeout, this, &LogWindow::pull);
}

void LogWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    pull();
    m_timer->start();
}

void LogWindow::hideEvent(QHideEvent* event)
{
    // New lines keep being captured while hidden and are added when the window is shown again.
    m_timer->stop();
    QWidget::hideEvent(event);
}

void LogWindow::pull()
{
    bool dropped = false;
    const QStringList lines = LogCapture::linesSince(m_serial, &dropped);
    if (lines.isEmpty()) {
        return;
    }
    QScrollBar* bar = m_text->verticalScrollBar();
    const int keep = bar->value();
    if (dropped) {
        m_text->appendPlainText(tr("… earlier lines no longer kept …"));
    }
    m_text->appendPlainText(lines.join(QLatin1Char('\n')));
    if (m_follow->isChecked()) {
        bar->setValue(bar->maximum());
    } else {
        bar->setValue(keep);
    }
}
