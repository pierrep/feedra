#include "SceneRowWidget.h"
#include "Theme.h"

#include <QHBoxLayout>
#include <QEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPushButton>
#include <QStyle>

namespace {
void restyle(QWidget* widget)
{
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}
}

SceneRowWidget::SceneRowWidget(int sceneId, const QString& name, QWidget* parent)
    : QWidget(parent)
    , m_id(sceneId)
{
    setObjectName("SceneRow");
    setAttribute(Qt::WA_StyledBackground, true);
    setAutoFillBackground(false);
    setMinimumHeight(44);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(6);

    m_name = new QLineEdit(name, this);
    m_name->setObjectName(QStringLiteral("SceneName"));
    m_name->setReadOnly(true);
    m_name->setFocusPolicy(Qt::NoFocus);
    m_name->installEventFilter(this);
    m_play = new QPushButton(this);
    m_play->setObjectName("ScenePlay");
    m_play->setFixedSize(22, 22);
    m_play->setCheckable(true);
    m_play->setText(QStringLiteral("\u25B6"));

    m_stop = new QPushButton(this);
    m_stop->setObjectName("SceneStop");
    m_stop->setFixedSize(22, 22);
    m_stop->setText(QStringLiteral("\u25A0"));

    m_remove = new QPushButton(this);
    m_remove->setObjectName("SceneDelete");
    m_remove->setFixedSize(22, 22);
    m_remove->setText(QStringLiteral("\u2013"));

    layout->addWidget(m_name, 1);
    layout->addWidget(m_play);
    layout->addWidget(m_stop);
    layout->addWidget(m_remove);

    connect(m_play, &QPushButton::clicked, this, [this]() {
        emit playPauseRequested(m_id);
    });
    connect(m_stop, &QPushButton::clicked, this, [this]() {
        emit stopRequested(m_id);
    });
    connect(m_remove, &QPushButton::clicked, this, [this]() {
        emit deleteRequested(m_id);
    });
    connect(m_name, &QLineEdit::editingFinished, this, &SceneRowWidget::finishEditing);
    connect(&Theme::instance(), &Theme::changed, this, [this]() { update(); });
}

QString SceneRowWidget::sceneName() const
{
    return m_name->text();
}

void SceneRowWidget::setSceneName(const QString& name)
{
    if (m_name->text() != name) {
        m_name->setText(name);
    }
}

void SceneRowWidget::setActive(bool active)
{
    setProperty("active", active);
    restyle(this);
    restyle(m_name);
}

void SceneRowWidget::paintEvent(QPaintEvent*)
{
    const bool active = property("active").toBool();
    const qreal stroke = active ? 4.0 : 3.0;
    const QRectF box = QRectF(rect()).adjusted(stroke / 2.0, stroke / 2.0, -stroke / 2.0, -stroke / 2.0);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const Theme::Palette& theme = Theme::instance().palette();
    painter.setPen(QPen(active ? theme.sceneActiveBorder : theme.sceneBorder, stroke));
    painter.setBrush(active ? theme.sceneActiveFill : theme.sceneFill);
    painter.drawRoundedRect(box, 5, 5);
}

void SceneRowWidget::setPlaying(bool playing)
{
    m_play->setChecked(playing);
    m_play->setText(playing ? QStringLiteral("\u23F8") : QStringLiteral("\u25B6"));
}

void SceneRowWidget::setInteractive(bool enabled)
{
    if (!enabled) {
        finishEditing();
    }
    m_name->setEnabled(enabled);
    m_play->setEnabled(true);
    m_stop->setEnabled(enabled);
    m_remove->setEnabled(enabled);
}

void SceneRowWidget::beginEditing()
{
    if (!m_name->isEnabled() || !m_name->isReadOnly()) {
        return;
    }
    m_name->setReadOnly(false);
    m_name->setFocusPolicy(Qt::StrongFocus);
    m_name->setProperty("editing", true);
    restyle(m_name);
    m_name->setFocus();
    m_name->selectAll();
}

void SceneRowWidget::finishEditing()
{
    if (m_name->isReadOnly()) {
        return;
    }
    m_name->setReadOnly(true);
    m_name->setFocusPolicy(Qt::NoFocus);
    m_name->setProperty("editing", false);
    restyle(m_name);
    m_name->clearFocus();
    emit nameChanged(m_id, m_name->text());
}

bool SceneRowWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_name) {
        if (event->type() == QEvent::MouseButtonDblClick) {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton) {
                beginEditing();
                return true;
            }
        } else if (event->type() == QEvent::MouseButtonPress) {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton && m_name->isReadOnly()) {
                emit selected(m_id);
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void SceneRowWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        emit selected(m_id);
    }
    QWidget::mousePressEvent(event);
}
