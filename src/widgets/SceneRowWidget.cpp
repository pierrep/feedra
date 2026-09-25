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
#include <QPainterPath>
#include <QPaintEvent>
#include <QTimer>
#include <cmath>
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

QColor mixColors(const QColor& a, const QColor& b, qreal t)
{
    return QColor::fromRgbF(
        static_cast<float>(a.redF() + (b.redF() - a.redF()) * t),
        static_cast<float>(a.greenF() + (b.greenF() - a.greenF()) * t),
        static_cast<float>(a.blueF() + (b.blueF() - a.blueF()) * t));
}

// Small round-cornered icon button for the scene row. Stop and delete can be hidden
// without changing the row's layout, so the name never jumps when they appear.
class SceneIconButton : public QAbstractButton
{
public:
    enum class Kind { Play, Stop, Delete };

    SceneIconButton(Kind kind, QWidget* parent = nullptr)
        : QAbstractButton(parent)
        , m_kind(kind)
    {
        setObjectName(kind == Kind::Play ? QStringLiteral("ScenePlay")
            : kind == Kind::Stop         ? QStringLiteral("SceneStop")
                                         : QStringLiteral("SceneDelete"));
        setFixedSize(28, 28);
        setFocusPolicy(Qt::NoFocus);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        setToolTip(kind == Kind::Play ? tr("Play / pause scene")
            : kind == Kind::Stop      ? tr("Stop scene")
                                      : tr("Delete scene"));
    }

    void setPlaying(bool playing)
    {
        if (m_playing == playing) {
            return;
        }
        m_playing = playing;
        update();
    }

    void setShown(bool shown)
    {
        if (m_shown == shown) {
            return;
        }
        m_shown = shown;
        setAttribute(Qt::WA_TransparentForMouseEvents, !shown);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        if (!m_shown) {
            return;
        }
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const Theme::Palette& theme = Theme::instance().palette();
        const bool hover = underMouse() && isEnabled();
        const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
        QColor fg = hover ? theme.sceneText : theme.textMuted;
        if (m_kind == Kind::Delete && hover) {
            fg = theme.dropIndicator;
        }
        if (m_kind == Kind::Play && m_playing) {
            p.setPen(Qt::NoPen);
            p.setBrush(hover ? theme.playLoaded.lighter(110) : theme.playLoaded);
            p.drawEllipse(r);
            fg = Theme::contrastOn(theme.playLoaded);
        } else if (hover) {
            p.setPen(Qt::NoPen);
            p.setBrush(theme.sceneEditingBackground);
            p.drawRoundedRect(r, 7, 7);
        } else if (m_kind == Kind::Play) {
            fg = theme.sceneText;
        }

        const QPointF c = r.center();
        p.setPen(Qt::NoPen);
        p.setBrush(fg);
        if (m_kind == Kind::Play) {
            if (m_playing) {
                p.drawRoundedRect(QRectF(c.x() - 4.5, c.y() - 5, 3.2, 10), 1, 1);
                p.drawRoundedRect(QRectF(c.x() + 1.3, c.y() - 5, 3.2, 10), 1, 1);
            } else {
                QPolygonF tri;
                tri << QPointF(c.x() - 3.5, c.y() - 5.5) << QPointF(c.x() - 3.5, c.y() + 5.5)
                    << QPointF(c.x() + 5.5, c.y());
                p.drawPolygon(tri);
            }
        } else if (m_kind == Kind::Stop) {
            p.drawRoundedRect(QRectF(c.x() - 4.5, c.y() - 4.5, 9, 9), 1.5, 1.5);
        } else {
            // Trash can.
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(fg, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawLine(QPointF(c.x() - 5.5, c.y() - 3.5), QPointF(c.x() + 5.5, c.y() - 3.5));
            p.drawLine(QPointF(c.x() - 1.8, c.y() - 5.5), QPointF(c.x() + 1.8, c.y() - 5.5));
            QPainterPath can;
            can.moveTo(c.x() - 4, c.y() - 3.5);
            can.lineTo(c.x() - 3.3, c.y() + 5.5);
            can.lineTo(c.x() + 3.3, c.y() + 5.5);
            can.lineTo(c.x() + 4, c.y() - 3.5);
            p.drawPath(can);
        }
    }

private:
    Kind m_kind;
    bool m_playing = false;
    bool m_shown = true;
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
    setMinimumHeight(40);
    setAttribute(Qt::WA_Hover);

    auto* layout = new QHBoxLayout(this);
    // The left margin leaves room for the live meter painted beside the name.
    layout->setContentsMargins(30, 4, 6, 4);
    layout->setSpacing(2);

    m_name = new QLineEdit(name, this);
    m_name->setObjectName(QStringLiteral("SceneName"));
    m_name->setReadOnly(true);
    m_name->setFocusPolicy(Qt::NoFocus);
    m_name->installEventFilter(this);
    m_name->setCursorPosition(0);
    m_play = new SceneIconButton(SceneIconButton::Kind::Play, this);
    m_stop = new SceneIconButton(SceneIconButton::Kind::Stop, this);
    m_remove = new SceneIconButton(SceneIconButton::Kind::Delete, this);

    m_meterTimer = new QTimer(this);
    m_meterTimer->setInterval(90);
    connect(m_meterTimer, &QTimer::timeout, this, [this]() {
        ++m_meterFrame;
        update(QRect(0, 0, 30, height()));
    });

    layout->addWidget(m_name, 1);
    layout->addWidget(m_play);
    layout->addWidget(m_stop);
    layout->addWidget(m_remove);

    connect(m_play, &QAbstractButton::clicked, this, [this]() {
        emit playPauseRequested(m_id);
    });
    connect(m_stop, &QAbstractButton::clicked, this, [this]() {
        emit stopRequested(m_id);
    });
    connect(m_remove, &QAbstractButton::clicked, this, [this]() {
        emit deleteRequested(m_id);
    });
    connect(m_name, &QLineEdit::editingFinished, this, &SceneRowWidget::finishEditing);
    connect(&Theme::instance(), &Theme::changed, this, [this]() {
        update();
        m_play->update();
        m_stop->update();
        m_remove->update();
    });
    refreshTools();
}

// Stop shows while the scene plays; stop and delete also show while the pointer is over the row.
void SceneRowWidget::refreshTools()
{
    const bool hover = underMouse();
    static_cast<SceneIconButton*>(m_stop)->setShown(m_playing || hover);
    static_cast<SceneIconButton*>(m_remove)->setShown(hover && m_remove->isEnabled());
}

bool SceneRowWidget::event(QEvent* event)
{
    if (event->type() == QEvent::HoverEnter || event->type() == QEvent::HoverLeave) {
        const bool result = QWidget::event(event);
        refreshTools();
        update();
        return result;
    }
    return QWidget::event(event);
}

QString SceneRowWidget::sceneName() const
{
    return m_name->text();
}

void SceneRowWidget::setSceneName(const QString& name)
{
    if (m_name->text() != name) {
        m_name->setText(name);
        m_name->setCursorPosition(0); // show the start of long names
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
    const bool hover = underMouse();
    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const Theme::Palette& theme = Theme::instance().palette();
    const QColor accent = theme.playLoaded;
    QColor fill = active ? theme.sceneActiveFill : theme.sceneFill;
    QColor border = active ? theme.sceneActiveBorder : theme.sceneBorder;
    if (m_playing) {
        fill = mixColors(theme.sceneFill, accent, 0.14);
        if (!active) {
            border = mixColors(theme.sceneBorder, accent, 0.6);
        }
    } else if (hover && !active) {
        fill = mixColors(theme.sceneFill, theme.sceneActiveFill, 0.6);
    }
    painter.setPen(QPen(border, 1.0));
    painter.setBrush(fill);
    painter.drawRoundedRect(box, 10, 10);

    // Live meter: three bars that bounce while the scene plays, a quiet dot otherwise.
    const qreal cx = 16.0;
    const qreal cy = height() / 2.0;
    painter.setPen(Qt::NoPen);
    if (m_playing) {
        painter.setBrush(accent);
        static const qreal kPhase[3] = {0.0, 2.1, 4.2};
        for (int i = 0; i < 3; ++i) {
            const qreal t = m_meterFrame * 0.55 + kPhase[i];
            const qreal level = 0.35 + 0.65 * std::abs(std::sin(t) * std::cos(t * 0.37 + i));
            const qreal h = 4.0 + level * 10.0;
            painter.drawRoundedRect(QRectF(cx - 5.5 + i * 4.0, cy + 7.0 - h, 3.0, h), 1.0, 1.0);
        }
    } else {
        painter.setBrush(active ? theme.sceneActiveBorder : theme.sceneBorder);
        painter.drawEllipse(QPointF(cx, cy), 3.0, 3.0);
    }
}

void SceneRowWidget::setPlaying(bool playing)
{
    if (m_playing != playing) {
        m_playing = playing;
        setProperty("playing", playing);
        restyle(this);
        restyle(m_name);
        if (playing) {
            m_meterTimer->start();
        } else {
            m_meterTimer->stop();
        }
        refreshTools();
    }
    static_cast<SceneIconButton*>(m_play)->setPlaying(playing);
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
    refreshTools();
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
    m_name->setCursorPosition(0);
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
