#include "SceneRowWidget.h"
#include "Theme.h"

#include <QAbstractButton>
#include <QApplication>
#include <QDrag>
#include <QHBoxLayout>
#include <QEvent>
#include <QLineEdit>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPolygonF>
#include <QPushButton>
#include <QStyle>

namespace {
void restyle(QWidget* widget)
{
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

class ScenePlayButton : public QAbstractButton
{
public:
    explicit ScenePlayButton(QWidget* parent = nullptr)
        : QAbstractButton(parent)
    {
        setObjectName(QStringLiteral("ScenePlay"));
        setFixedSize(22, 22);
        setFocusPolicy(Qt::NoFocus);
        setCursor(Qt::PointingHandCursor);
    }

    void setPlaying(bool playing)
    {
        if (m_playing == playing) {
            return;
        }
        m_playing = playing;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const Theme::Palette& theme = Theme::instance().palette();
        p.setPen(Qt::NoPen);
        p.setBrush(theme.sceneFill);
        p.drawRect(rect());

        p.setBrush(theme.sceneText);
        const QRectF r = QRectF(rect()).adjusted(6, 5, -6, -5);
        if (m_playing) {
            const qreal barW = qMax(2.0, r.width() / 3.5);
            p.drawRect(QRectF(r.left(), r.top(), barW, r.height()));
            p.drawRect(QRectF(r.right() - barW, r.top(), barW, r.height()));
        } else {
            QPolygonF tri;
            tri << r.topLeft() << r.bottomLeft() << QPointF(r.right(), r.center().y());
            p.drawPolygon(tri);
        }
    }

private:
    bool m_playing = false;
};
}

QString SceneRowWidget::dragMimeType()
{
    return QStringLiteral("application/x-feedra-scene");
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
    m_play = new ScenePlayButton(this);

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

    connect(m_play, &QAbstractButton::clicked, this, [this]() {
        emit playPauseRequested(m_id);
    });
    connect(m_stop, &QPushButton::clicked, this, [this]() {
        emit stopRequested(m_id);
    });
    connect(m_remove, &QPushButton::clicked, this, [this]() {
        emit deleteRequested(m_id);
    });
    connect(m_name, &QLineEdit::editingFinished, this, &SceneRowWidget::finishEditing);
    connect(&Theme::instance(), &Theme::changed, this, [this]() {
        update();
        if (m_play) {
            m_play->update();
        }
    });
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
    QColor fill = theme.sceneFill;
    QColor border = theme.sceneBorder;
    if (m_playing) {
        fill = theme.padSelected;
        border = active ? theme.sceneActiveBorder : theme.padSelected;
    } else if (active) {
        fill = theme.sceneActiveFill;
        border = theme.sceneActiveBorder;
    }
    painter.setPen(QPen(border, stroke));
    painter.setBrush(fill);
    painter.drawRoundedRect(box, 5, 5);
}

void SceneRowWidget::setPlaying(bool playing)
{
    if (m_playing != playing) {
        m_playing = playing;
        setProperty("playing", playing);
        restyle(this);
        restyle(m_name);
    }
    static_cast<ScenePlayButton*>(m_play)->setPlaying(playing);
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
                m_pressPos = m_name->mapTo(this, mouse->position().toPoint());
                emit selected(m_id);
            }
        } else if (event->type() == QEvent::MouseMove) {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            if ((mouse->buttons() & Qt::LeftButton) && m_name->isReadOnly()) {
                startDragIfMoved(m_name->mapTo(this, mouse->position().toPoint()));
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void SceneRowWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressPos = event->pos();
        emit selected(m_id);
    }
    QWidget::mousePressEvent(event);
}

void SceneRowWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (event->buttons() & Qt::LeftButton) {
        startDragIfMoved(event->pos());
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void SceneRowWidget::startDragIfMoved(const QPoint& pos)
{
    if ((pos - m_pressPos).manhattanLength() < QApplication::startDragDistance()) {
        return;
    }
    auto* drag = new QDrag(this);
    auto* mime = new QMimeData();
    mime->setData(dragMimeType(), QByteArray::number(m_id));
    drag->setMimeData(mime);
    drag->setPixmap(grab());
    drag->setHotSpot(m_pressPos);
    drag->exec(Qt::MoveAction);
}
