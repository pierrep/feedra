#include "SoundPadWidget.h"
#include "AppConfig.h"
#include "AudioSample.h"
#include "OpenALSoundPlayer.h"
#include "SampleLoadQueue.h"
#include "Theme.h"
#include "VolumeDb.h"

#include <QApplication>
#include <QColor>
#include <QDebug>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QHash>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLinearGradient>
#include <QLineEdit>
#include <QMimeData>
#include <QMouseEvent>
#include <QAbstractButton>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPointer>
#include <QPolygonF>
#include <QResizeEvent>
#include <QSet>
#include <QSizePolicy>
#include <QSlider>
#include <QThread>
#include <QThreadPool>
#include <QUrl>
#include <QValidator>
#include <QWidget>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <utility>

namespace {
QStringList audioNameFilters()
{
    return {
        QStringLiteral("Audio files (*.wav *.flac *.ogg *.mp3 *.aiff *.aif *.wma)"),
        QStringLiteral("All files (*.*)")
    };
}

QColor withAlpha(QColor color, qreal alpha)
{
    color.setAlphaF(static_cast<float>(std::clamp(alpha, 0.0, 1.0)));
    return color;
}

// Icons are drawn on a 24x24 grid (the same geometry as the mockup) and scaled into `r`.
void mapToIconBox(QPainter& p, const QRectF& r)
{
    const qreal s = std::min(r.width(), r.height()) / 24.0;
    p.translate(r.center().x() - 12.0 * s, r.center().y() - 12.0 * s);
    p.scale(s, s);
}

void drawLoopIcon(QPainter& p, const QRectF& r, const QColor& color)
{
    p.save();
    mapToIconBox(p, r);
    QPen pen(color, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    QPainterPath path;
    path.moveTo(17, 2); path.lineTo(21, 6); path.lineTo(17, 10);
    path.moveTo(3, 11); path.lineTo(3, 10);
    path.arcTo(QRectF(3, 6, 8, 8), 180, -90);
    path.lineTo(21, 6);
    path.moveTo(7, 22); path.lineTo(3, 18); path.lineTo(7, 14);
    path.moveTo(21, 13); path.lineTo(21, 14);
    path.arcTo(QRectF(13, 10, 8, 8), 0, -90);
    path.lineTo(3, 18);
    p.drawPath(path);
    p.restore();
}

void drawFolderIcon(QPainter& p, const QRectF& r, const QColor& color)
{
    p.save();
    mapToIconBox(p, r);
    p.setPen(QPen(color, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    QPainterPath path;
    path.moveTo(3, 7);
    path.quadTo(3, 5, 5, 5);
    path.lineTo(9, 5); path.lineTo(11, 7); path.lineTo(19, 7);
    path.quadTo(21, 7, 21, 9);
    path.lineTo(21, 17);
    path.quadTo(21, 19, 19, 19);
    path.lineTo(5, 19);
    path.quadTo(3, 19, 3, 17);
    path.closeSubpath();
    p.drawPath(path);
    p.restore();
}

void drawPlusIcon(QPainter& p, const QRectF& r, const QColor& color)
{
    p.save();
    mapToIconBox(p, r);
    p.setPen(QPen(color, 2.0, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(12, 5), QPointF(12, 19));
    p.drawLine(QPointF(5, 12), QPointF(19, 12));
    p.restore();
}

void drawPlayIcon(QPainter& p, const QRectF& r, const QColor& color)
{
    p.save();
    mapToIconBox(p, r);
    p.setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(color);
    QPolygonF tri;
    tri << QPointF(8.5, 5.5) << QPointF(8.5, 18.5) << QPointF(19.0, 12.0);
    p.drawPolygon(tri);
    p.restore();
}

void drawPauseIcon(QPainter& p, const QRectF& r, const QColor& color)
{
    p.save();
    mapToIconBox(p, r);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawRoundedRect(QRectF(6, 5, 4, 14), 1, 1);
    p.drawRoundedRect(QRectF(14, 5, 4, 14), 1, 1);
    p.restore();
}

// 0..1 breathing value shared by every playing pad, so glows pulse together.
qreal pulsePhase()
{
    static QElapsedTimer clock;
    if (!clock.isValid()) {
        clock.start();
    }
    const qreal t = static_cast<qreal>(clock.elapsed()) / 2400.0;
    constexpr qreal kTau = 6.283185307179586;
    return 0.5 + 0.5 * std::sin(t * kTau);
}

QString formatClock(float seconds)
{
    const int total = std::max(0, static_cast<int>(seconds));
    const int hours = total / 3600;
    const int minutes = (total / 60) % 60;
    const int secs = total % 60;
    if (hours > 0) {
        return QString("%1:%2:%3").arg(hours).arg(minutes, 2, 10, QChar('0')).arg(secs, 2, 10, QChar('0'));
    }
    return QString("%1:%2").arg(minutes).arg(secs, 2, 10, QChar('0'));
}

// Waveform strips for pads: a few peak bins per file, computed once on one low-priority
// background thread and shared by every pad that shows that file.
struct PadPeakCache {
    QHash<QString, QVector<float>> peaks;
    QSet<QString> pending;
    QThreadPool* pool = nullptr;
    std::atomic<bool> cancel{false};
};

PadPeakCache& peakCache()
{
    static PadPeakCache cache;
    if (!cache.pool) {
        cache.pool = new QThreadPool(qApp);
        cache.pool->setMaxThreadCount(1);
        QObject::connect(qApp, &QCoreApplication::aboutToQuit, []() {
            peakCache().cancel.store(true);
            peakCache().pool->clear();
            peakCache().pool->waitForDone();
        });
    }
    return cache;
}

// Returns the cached strip for `path`, or an empty vector after queueing the scan.
QVector<float> padPeaks(const QString& path)
{
    PadPeakCache& cache = peakCache();
    const auto it = cache.peaks.constFind(path);
    if (it != cache.peaks.constEnd()) {
        return it.value();
    }
    if (path.isEmpty() || cache.pending.contains(path)) {
        return {};
    }
    cache.pending.insert(path);
    cache.pool->start([path]() {
        QThread::currentThread()->setPriority(QThread::LowPriority);
        PadPeakCache& c = peakCache();
        const WaveformPeaks raw = OpenALSoundPlayer::computePeaks(
            std::filesystem::path(path.toStdString()), PadPlayhead::kBars, &c.cancel);
        if (c.cancel.load()) {
            return;
        }
        QVector<float> bars;
        if (raw.ok) {
            float loudest = 0.0f;
            for (size_t i = 0; i < raw.maxs.size() && i < raw.mins.size(); ++i) {
                const float amp = std::max(std::abs(raw.mins[i]), std::abs(raw.maxs[i]));
                bars.append(amp);
                loudest = std::max(loudest, amp);
            }
            if (loudest > 0.0f) {
                for (float& bar : bars) {
                    bar /= loudest;
                }
            }
        }
        if (bars.isEmpty()) {
            bars.fill(0.35f, PadPlayhead::kBars); // unreadable: keep a flat strip, don't retry forever
        }
        QMetaObject::invokeMethod(qApp, [path, bars]() {
            PadPeakCache& c = peakCache();
            c.pending.remove(path);
            c.peaks.insert(path, bars);
        }, Qt::QueuedConnection);
    });
    return {};
}
}

namespace {
// Accepts a pad name only while it fits the name box at the font and size it is shown in,
// so the limit is "what fits" rather than a character count. Shortening is always allowed,
// so a longer name from a file name or an older project can still be edited down.
class NameFitValidator : public QValidator
{
public:
    explicit NameFitValidator(QLineEdit* edit)
        : QValidator(edit)
        , m_edit(edit)
        , m_accepted(edit->text())
    {
        // While validating, the line edit already holds the candidate text, so remember the
        // last accepted name ourselves (setText() updates it too).
        connect(edit, &QLineEdit::textChanged, this, [this](const QString& text) {
            m_accepted = text;
        });
    }

    State validate(QString& input, int&) const override
    {
        const QMargins margins = m_edit->textMargins();
        // QLineEdit keeps a 2px inner margin each side and needs 1px for the cursor.
        const int room = m_edit->contentsRect().width() - margins.left() - margins.right() - 5;
        if (room <= 0) {
            return Acceptable; // not laid out yet
        }
        const QFontMetrics metrics(m_edit->font());
        const int width = metrics.horizontalAdvance(input);
        if (width <= room || width <= metrics.horizontalAdvance(m_accepted)) {
            return Acceptable;
        }
        return Invalid;
    }

private:
    QLineEdit* m_edit;
    QString m_accepted;
};
}

GlyphButton::GlyphButton(Kind kind, QWidget* parent)
    : QAbstractButton(parent)
    , m_kind(kind)
{
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_Hover);
    setAutoFillBackground(false);
    if (kind == Kind::Loop) {
        setCheckable(true);
        setToolTip(tr("Loop"));
    }
    if (kind == Kind::Load) {
        setToolTip(tr("Load sounds"));
    }
    if (kind == Kind::Stop) {
        setToolTip(tr("Stop"));
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }
}

void GlyphButton::setLoaded(bool loaded)
{
    if (m_loaded == loaded) {
        return;
    }
    m_loaded = loaded;
    update();
}

void GlyphButton::setPlaying(bool playing)
{
    if (m_playing == playing) {
        return;
    }
    m_playing = playing;
    update();
}

void GlyphButton::setArmed(bool armed)
{
    if (m_armed == armed) {
        return;
    }
    m_armed = armed;
    // Unarmed stop is invisible, so let clicks fall through and select the pad.
    if (m_kind == Kind::Stop) {
        setAttribute(Qt::WA_TransparentForMouseEvents, !armed);
    }
    update();
}

QSize GlyphButton::sizeHint() const
{
    return m_kind == Kind::Play ? QSize(64, 64) : QSize(24, 24);
}

void GlyphButton::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const Theme::Palette& theme = Theme::instance().palette();
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal radius = std::min(r.width(), r.height()) * 0.25;
    const bool hover = underMouse() && isEnabled();

    if (m_kind == Kind::Load) {
        if (!m_loaded) {
            // Empty pad: a dashed drop circle with a plus.
            const qreal d = std::min(r.width(), r.height()) - 2.0;
            const QRectF circle(r.center().x() - d / 2.0, r.center().y() - d / 2.0, d, d);
            QPen pen(hover ? theme.textMuted : theme.playOutline, std::max(1.0, d / 30.0));
            pen.setDashPattern({3.0, 2.5});
            p.setPen(pen);
            p.setBrush(hover ? withAlpha(theme.playEmpty, 0.6) : Qt::transparent);
            p.drawEllipse(circle);
            drawPlusIcon(p, circle.adjusted(d * 0.3, d * 0.3, -d * 0.3, -d * 0.3), hover ? theme.text : theme.textMuted);
            return;
        }
        p.setPen(Qt::NoPen);
        p.setBrush(hover ? theme.playOutline : theme.loadButton);
        p.drawRoundedRect(r, radius, radius);
        drawFolderIcon(p, r.adjusted(r.width() * 0.2, r.height() * 0.2, -r.width() * 0.2, -r.height() * 0.2),
            hover ? theme.text : theme.stopArmed);
        return;
    }

    if (m_kind == Kind::Stop) {
        if (!m_armed) {
            return;
        }
        p.setPen(Qt::NoPen);
        p.setBrush(hover ? theme.playOutline : theme.loadButton);
        p.drawRoundedRect(r, radius, radius);
        const qreal side = r.width() * 0.36;
        p.setBrush(hover ? theme.text : theme.stopArmed);
        p.drawRoundedRect(QRectF(r.center().x() - side / 2.0, r.center().y() - side / 2.0, side, side),
            side * 0.15, side * 0.15);
        return;
    }

    if (m_kind == Kind::Loop) {
        const bool on = isChecked();
        const QRectF box = r.adjusted(0.5, 0.5, -0.5, -0.5);
        if (on) {
            p.setBrush(withAlpha(theme.loopOn, hover ? 0.24 : 0.16));
            p.setPen(QPen(withAlpha(theme.loopOn, 0.45), 1.0));
        } else {
            p.setBrush(hover ? withAlpha(theme.playEmpty, 0.8) : Qt::transparent);
            p.setPen(QPen(theme.padBorder, 1.0));
        }
        p.drawRoundedRect(box, radius, radius);
        drawLoopIcon(p, box.adjusted(box.width() * 0.2, box.height() * 0.2, -box.width() * 0.2, -box.height() * 0.2),
            on ? theme.loopOn : (hover ? theme.text : theme.loopOff));
        return;
    }

    // Play: a round button, filled with the accent while the pad is live.
    if (!m_loaded) {
        return;
    }
    const qreal d = std::min(r.width(), r.height()) * 0.875;
    const QRectF circle(r.center().x() - d / 2.0, r.center().y() - d / 2.0, d, d);
    if (m_playing) {
        QRadialGradient halo(circle.center(), r.width() / 2.0);
        halo.setColorAt(0.70, withAlpha(theme.playLoaded, 0.40));
        halo.setColorAt(1.00, withAlpha(theme.playLoaded, 0.0));
        p.setPen(Qt::NoPen);
        p.setBrush(halo);
        p.drawEllipse(r);
        p.setBrush(hover ? theme.playLoaded.lighter(110) : theme.playLoaded);
        p.drawEllipse(circle);
        drawPauseIcon(p, circle.adjusted(d * 0.3, d * 0.3, -d * 0.3, -d * 0.3), Theme::contrastOn(theme.playLoaded));
    } else {
        p.setBrush(hover ? theme.playOutline : theme.playEmpty);
        p.setPen(QPen(hover ? theme.textMuted : theme.playOutline, 1.0));
        p.drawEllipse(circle);
        drawPlayIcon(p, circle.adjusted(d * 0.3, d * 0.3, -d * 0.3, -d * 0.3), theme.text);
    }
}

PadPlayhead::PadPlayhead(QWidget* parent)
    : QWidget(parent)
{
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
}

void PadPlayhead::setProgress(float pct)
{
    pct = std::clamp(pct, 0.0f, 1.0f);
    const int oldBar = static_cast<int>(m_progress * kBars * 4);
    const int newBar = static_cast<int>(pct * kBars * 4);
    m_progress = pct;
    if (oldBar != newBar) {
        update();
    }
}

void PadPlayhead::setDelayMode(bool delay)
{
    if (m_delay == delay) {
        return;
    }
    m_delay = delay;
    update();
}

void PadPlayhead::setPlaying(bool playing)
{
    if (m_playing == playing) {
        return;
    }
    m_playing = playing;
    update();
}

void PadPlayhead::setTimeText(const QString& text)
{
    m_time = text;
}

void PadPlayhead::setPeaks(const QVector<float>& peaks)
{
    m_peaks = peaks;
    update();
}

void PadPlayhead::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const Theme::Palette& theme = Theme::instance().palette();
    const qreal w = width();
    const qreal h = height();
    const qreal gap = std::max(1.0, w / 76.0);
    const qreal barW = (w - gap * (kBars - 1)) / kBars;
    const qreal minH = std::max(2.0, h * 0.12);
    const qreal fillTo = m_progress * kBars;
    const int headBar = static_cast<int>(fillTo);

    p.setPen(Qt::NoPen);
    if (m_delay) {
        // Counting down to the next play there is no sound, so the strip becomes a flat
        // row of dots (a silent, constant signal) that fills in as the delay runs out.
        const qreal d = std::min(barW, std::max(2.0, h * 0.14));
        for (int i = 0; i < kBars; ++i) {
            p.setBrush(i < headBar ? theme.playheadDelay : theme.playheadBorder);
            p.drawEllipse(QPointF(i * (barW + gap) + barW / 2.0, h / 2.0), d / 2.0, d / 2.0);
        }
        return;
    }
    for (int i = 0; i < kBars; ++i) {
        // No peaks yet: a quiet flat strip until the background scan lands.
        const float amp = m_peaks.size() == kBars ? m_peaks[i] : 0.18f;
        const qreal bh = std::max(minH, static_cast<qreal>(amp) * h);
        QColor color = theme.playheadBorder;
        if (m_playing) {
            if (i < headBar) {
                color = theme.playhead;
            } else if (i == headBar) {
                color = theme.playheadText;
            }
        } else if (m_progress > 0.0f && i < headBar) {
            color = theme.playheadDelay; // paused part-way
        }
        p.setBrush(color);
        const QRectF bar(i * (barW + gap), (h - bh) / 2.0, barW, bh);
        p.drawRoundedRect(bar, std::min(barW / 2.0, 2.0), std::min(barW / 2.0, 2.0));
    }
}

void PadPlayhead::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && width() > 0 && m_onScrub) {
        m_onScrub(std::clamp(static_cast<float>(event->position().x() / width()), 0.0f, 1.0f));
    }
}

void PadPlayhead::mouseMoveEvent(QMouseEvent* event)
{
    if ((event->buttons() & Qt::LeftButton) && width() > 0 && m_onScrub) {
        m_onScrub(std::clamp(static_cast<float>(event->position().x() / width()), 0.0f, 1.0f));
    }
}

SoundPadWidget::SoundPadWidget(AppConfig* config, int sceneId, int padId, QWidget* parent)
    : QWidget(parent)
    , m_config(config)
    , m_player(this)
    , m_sceneId(sceneId)
    , m_padId(padId)
{
    setObjectName("SoundPad");
    setAcceptDrops(true);
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
    setMinimumSize(kDesignWidth, kDesignHeight);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_player.setup(config, padId);

    m_volume = new QSlider(Qt::Horizontal, this);
    m_volume->setRange(0, VolumeDb::sliderSpan(VolumeDb::kFloorDb, VolumeDb::kUnityDb));
    m_volume->setValue(VolumeDb::toSliderClamped(0.7f, VolumeDb::kFloorDb, VolumeDb::kUnityDb));
    m_volume->setObjectName("PadVolume");

    m_volumeValue = new QLabel(this);
    m_volumeValue->setObjectName("PadVolumeValue");
    m_volumeValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_volumeValue->setAttribute(Qt::WA_TransparentForMouseEvents);

    m_card = new QWidget(this);
    m_card->setObjectName("PadCard");
    m_card->setAttribute(Qt::WA_TranslucentBackground);
    m_card->setAutoFillBackground(false);

    m_load = new GlyphButton(GlyphButton::Kind::Load, m_card);
    m_loop = new GlyphButton(GlyphButton::Kind::Loop, m_card);
    m_stop = new GlyphButton(GlyphButton::Kind::Stop, m_card);
    m_play = new GlyphButton(GlyphButton::Kind::Play, m_card);

    m_playhead = new PadPlayhead(m_card);
    m_playhead->setVisible(false);
    m_playhead->m_onScrub = [this](float pct) {
        if (m_player.isLoaded()) {
            m_player.setPosition(pct);
        }
    };

    m_name = new QLineEdit(m_card);
    m_name->setObjectName("PadName");
    // Names may be as long as fits the box in the pad's current font (see NameFitValidator).
    m_name->setMaxLength(64);
    m_name->setValidator(new NameFitValidator(m_name));
    // The title is read-only until double-clicked (see beginNameEdit). Enter or clicking away
    // finishes editing; Esc puts the old name back.
    connect(m_name, &QLineEdit::returnPressed, m_name, [this]() {
        m_name->clearFocus();
    });
    connect(m_name, &QLineEdit::editingFinished, this, &SoundPadWidget::endNameEdit);
    endNameEdit();
    m_name->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_name->setPlaceholderText(tr("Empty pad"));
    m_name->setTextMargins(0, 0, 0, 0);
    // Small pads can clip a long name; the tooltip always has all of it.
    connect(m_name, &QLineEdit::textChanged, m_name, [this](const QString& text) {
        m_name->setToolTip(text);
    });

    refreshChrome();

    // The card forwards unhandled presses to mousePressEvent, so watching it too would select twice.
    const QList<QWidget*> controls{m_volume, m_load, m_loop, m_stop, m_play, m_playhead, m_name};
    for (QWidget* control : controls) {
        control->installEventFilter(this);
    }

    connect(m_load, &QAbstractButton::clicked, this, &SoundPadWidget::chooseFiles);
    connect(m_play, &QAbstractButton::clicked, this, [this]() {
        if (isLoading() || !m_player.isLoaded()) {
            return;
        }
        if (m_player.isPlaying()) {
            m_player.setPaused(true);
        } else {
            m_player.setPaused(false);
        }
    });
    connect(m_stop, &QAbstractButton::clicked, this, [this]() {
        m_player.stop();
    });
    connect(m_loop, &QAbstractButton::toggled, this, [this](bool on) {
        m_player.setLoop(on);
    });
    connect(m_volume, &QSlider::valueChanged, this, [this]() {
        applyVolume();
        updateVolumeLabel();
    });

    setInteractive(false);
    updateVolumeLabel();

    connect(&Theme::instance(), &Theme::changed, this, [this]() {
        layoutContents();
        update();
        const auto kids = findChildren<QWidget*>();
        for (QWidget* kid : kids) {
            kid->update();
        }
    });
}

SoundPadWidget::~SoundPadWidget()
{
    if (m_loadGeneration) {
        m_loadGeneration->fetch_add(1);
    }
}

QString SoundPadWidget::soundName() const
{
    return m_name->text();
}

void SoundPadWidget::setSoundName(const QString& name)
{
    m_name->setText(name);
    m_name->setCursorPosition(0);
}

float SoundPadWidget::padVolume() const
{
    return VolumeDb::toLinearMuted(VolumeDb::fromSlider(m_volume->value(), VolumeDb::kFloorDb));
}

void SoundPadWidget::setPadVolume(float value)
{
    const float linear = std::clamp(value, 0.0f, 1.0f);
    m_volume->setValue(VolumeDb::toSliderClamped(linear, VolumeDb::kFloorDb, VolumeDb::kUnityDb));
}

bool SoundPadWidget::isLooping() const
{
    return m_loop->isChecked();
}

void SoundPadWidget::setLooping(bool looping)
{
    m_loop->setChecked(looping);
    m_player.setLoop(looping);
}

bool SoundPadWidget::isLoaded() const
{
    return m_player.isLoaded();
}

bool SoundPadWidget::isPlaying() const
{
    return m_player.isPlaying();
}

void SoundPadWidget::updateAudio()
{
    m_player.update();
    applyVolume();

    const bool loaded = !isLoading() && m_player.isLoaded();
    const bool playing = m_player.isPlaying();
    const bool delay = loaded && m_player.isPlayingDelay();
    // Play does nothing on an empty pad, so let clicks through to select the pad.
    const bool empty = !isLoading() && !m_player.isLoaded();
    if (m_play->testAttribute(Qt::WA_TransparentForMouseEvents) != empty) {
        m_play->setAttribute(Qt::WA_TransparentForMouseEvents, empty);
    }
    m_play->setLoaded(loaded);
    m_play->setPlaying(playing && !delay);
    m_load->setLoaded(loaded || isLoading());
    m_playhead->setProgress(loaded ? m_player.getPosition() : 0.0f);
    m_playhead->setDelayMode(delay);
    m_playhead->setPlaying(playing && !delay);
    m_playhead->setVisible(loaded);
    if (loaded) {
        refreshPeaks();
    }

    const QString nextIn = m_compactText ? tr("Next %1") : tr("Next in %1");
    const QString time = loaded ? (delay ? nextIn.arg(remainingTimeText()) : positionTimeText()) : QString();
    const bool loading = isLoading();
    const bool stateChanged = loaded != m_uiLoaded || playing != m_uiPlaying || delay != m_uiDelay
        || loading != m_uiLoading;
    m_uiLoading = loading;
    m_uiLoaded = loaded;
    m_uiPlaying = playing;
    m_uiDelay = delay;
    if (stateChanged) {
        refreshChrome();
    }
    if (stateChanged || time != m_timeText || (playing && !delay)) {
        // Playing pads repaint every tick for the breathing glow.
        m_timeText = time;
        update();
    }
}

void SoundPadWidget::setSelected(bool selected)
{
    if (m_selected == selected) {
        return;
    }
    m_selected = selected;
    refreshChrome();
    update();
}

QString SoundPadWidget::currentSamplePath() const
{
    if (m_soundPaths.empty()) {
        return {};
    }
    const int cur = std::clamp(m_player.getCurSound(), 0, static_cast<int>(m_soundPaths.size()) - 1);
    return QString::fromStdString(m_soundPaths[static_cast<size_t>(cur)]);
}

void SoundPadWidget::refreshPeaks()
{
    const QString path = currentSamplePath();
    if (path == m_peakPath && m_playhead->hasPeaks()) {
        return;
    }
    const QVector<float> peaks = padPeaks(path);
    if (path != m_peakPath || !peaks.isEmpty()) {
        m_peakPath = path;
        m_playhead->setPeaks(peaks);
    }
}

// Which controls a pad shows depends on its state; the load and stop tools only appear
// on the selected pad (or, for load, on an empty one, where it becomes the drop circle).
void SoundPadWidget::refreshChrome()
{
    const bool loading = isLoading();
    const bool empty = !loading && !m_uiLoaded;
    const bool hasSound = m_uiLoaded;
    // One tool slot beside play: stop while the pad plays (or counts down its delay),
    // the folder to load sounds while it is stopped. Empty pads show the big drop circle.
    m_load->setVisible(empty || (hasSound && !m_uiPlaying));
    m_loop->setVisible(hasSound);
    m_stop->setArmed(hasSound && m_uiPlaying);
    m_volume->setVisible(hasSound);
    m_volumeValue->setVisible(hasSound);
    layoutContents();
}

void SoundPadWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    const qreal s = contentScale();
    const Theme::Palette& theme = Theme::instance().palette();
    const QRectF card = QRectF(padCardRect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal radius = 12.0 * s;
    const bool loading = isLoading() && m_loadTotal > 0;
    const bool empty = !loading && !m_uiLoaded;
    const bool live = m_uiPlaying && !m_uiDelay;
    const QColor accent = theme.playLoaded;

    auto at = [s](qreal x, qreal y, qreal w, qreal h) {
        return QRectF(x * s, y * s, w * s, h * s);
    };

    // Selection ring sits in the margin, with a gap so it reads against any card colour.
    if (m_selected) {
        p.setPen(QPen(theme.padSelected, 2.0 * s));
        p.setBrush(Qt::NoBrush);
        const qreal o = 3.0 * s;
        p.drawRoundedRect(card.adjusted(-o, -o, o, o), radius + o, radius + o);
    } else if (live) {
        const qreal pulse = 0.55 + 0.45 * pulsePhase();
        p.setBrush(Qt::NoBrush);
        for (int i = 3; i >= 1; --i) {
            const qreal o = i * s;
            p.setPen(QPen(withAlpha(accent, (0.30 / i) * pulse), 1.2 * s));
            p.drawRoundedRect(card.adjusted(-o, -o, o, o), radius + o, radius + o);
        }
    }

    if (empty) {
        QPen dashed(theme.padBorder, 1.0);
        dashed.setDashPattern({4.0, 3.0});
        p.setPen(dashed);
        p.setBrush(theme.padFill.darker(112));
    } else if (live) {
        QLinearGradient tint(card.topLeft(), card.bottomLeft());
        tint.setColorAt(0.0, withAlpha(accent, 0.14));
        tint.setColorAt(0.62, withAlpha(accent, 0.0));
        p.setPen(Qt::NoPen);
        p.setBrush(theme.padFill);
        p.drawRoundedRect(card, radius, radius);
        p.setBrush(tint);
        p.setPen(QPen(withAlpha(accent, 0.75), 1.0));
    } else {
        p.setPen(QPen(theme.padBorder, 1.0));
        p.setBrush(theme.padFill);
    }
    p.drawRoundedRect(card, radius, radius);

    QFont captionFont = font();
    captionFont.setPixelSize(std::max(9, qRound(12 * s)));
    QFont monoFont(QStringLiteral("Geist Mono"));
    monoFont.setStyleHint(QFont::Monospace);
    monoFont.setPixelSize(std::max(9, qRound(11 * s)));

    if (empty) {
        p.setFont(captionFont);
        p.setPen(theme.textMuted);
        p.drawText(at(14, 110, 148, 18), Qt::AlignHCenter | Qt::AlignVCenter, tr("Drop audio or click"));
        return;
    }

    if (loading) {
        p.setFont(captionFont);
        p.setPen(theme.textMuted);
        p.drawText(at(14, 64, 148, 18), Qt::AlignHCenter | Qt::AlignVCenter,
            tr("Loading %1 of %2").arg(std::min(m_nextCommit + 1, m_loadTotal)).arg(m_loadTotal));
        const QRectF track = at(14, 90, 148, 4);
        p.setPen(Qt::NoPen);
        p.setBrush(theme.progressTrack);
        p.drawRoundedRect(track, track.height() / 2, track.height() / 2);
        QRectF chunk = track;
        chunk.setWidth(track.width() * (static_cast<qreal>(m_nextCommit) / static_cast<qreal>(m_loadTotal)));
        if (chunk.width() > 0) {
            p.setBrush(theme.progressChunk);
            p.drawRoundedRect(chunk, track.height() / 2, track.height() / 2);
        }
        return;
    }

    if (!m_timeText.isEmpty()) {
        p.setFont(monoFont);
        p.setPen(live ? theme.text : (m_uiDelay ? accent : theme.textMuted));
        const QRectF timeRect = m_compactText ? at(14, 150, 94, 16) : at(14, 150, 96, 16);
        p.drawText(timeRect, Qt::AlignLeft | Qt::AlignVCenter,
            QFontMetrics(monoFont).elidedText(m_timeText, Qt::ElideRight, qRound(timeRect.width())));
    }
}

QSize SoundPadWidget::sizeHint() const
{
    return QSize(kDesignWidth, kDesignHeight);
}

QSize SoundPadWidget::minimumSizeHint() const
{
    return sizeHint();
}

qreal SoundPadWidget::contentScale() const
{
    if (width() <= 0 || height() <= 0) {
        return kDesignWidth / qreal(kLayoutWidth);
    }
    return std::min(width() / qreal(kLayoutWidth), height() / qreal(kLayoutHeight));
}

void SoundPadWidget::layoutContents()
{
    const qreal s = contentScale();
    auto scaled = [s](int x, int y, int w, int h) {
        return QRect(qRound(x * s), qRound(y * s), qRound(w * s), qRound(h * s));
    };

    // Everything below is placed on the 176x204 mockup canvas; the card itself is inset
    // 4px so the selection ring and live glow have room around it.
    const bool empty = !isLoading() && !m_uiLoaded;

    m_card->setGeometry(scaled(4, 4, 168, 196));
    // Child geometry below is in pad coordinates; the card starts at (4,4).
    auto inCard = [&](int x, int y, int w, int h) {
        QRect r = scaled(x, y, w, h);
        r.translate(-m_card->x(), -m_card->y());
        return r;
    };

    // The name owns the whole top row. Play sits in the middle between two matching tool
    // slots: loop on the left, and stop or load (never both) on the right.
    m_name->setGeometry(inCard(14, 14, 152, 24));
    if (empty) {
        m_load->setGeometry(inCard(62, 54, 52, 52));
    } else {
        m_load->setGeometry(inCard(130, 64, 28, 28));
    }
    m_play->setGeometry(inCard(56, 46, 64, 64));
    m_stop->setGeometry(inCard(130, 64, 28, 28));
    m_loop->setGeometry(inCard(18, 64, 28, 28));
    m_playhead->setGeometry(inCard(14, 120, 148, 24));
    // Small pads keep both readouts by shortening them ("0:44/1:11", "-6 dB").
    const bool compact = s < 0.9;
    m_volumeValue->setGeometry(compact ? scaled(110, 150, 52, 16) : scaled(92, 150, 70, 16));
    if (compact != m_compactText) {
        m_compactText = compact;
        updateVolumeLabel();
    }
    m_volume->setGeometry(scaled(14, 170, 148, 16));
    m_volume->raise();
    m_volumeValue->raise();

    QFont nameFont = m_name->font();
    nameFont.setPixelSize(std::max(10, qRound(13 * s)));
    nameFont.setWeight(QFont::DemiBold);
    m_name->setFont(nameFont);
    if (!m_name->hasFocus()) {
        m_name->setCursorPosition(0); // show the start of long names, not the scrolled end
    }

    QFont volFont(QStringLiteral("Geist Mono"));
    volFont.setStyleHint(QFont::Monospace);
    volFont.setPixelSize(std::max(9, qRound(11 * s)));
    m_volumeValue->setFont(volFont);

    const int groove = std::max(2, qRound(3 * s));
    const int margin = std::max(3, qRound(3.5 * s));
    // Qt drops a radius larger than half the handle, so size the handle from the groove.
    const int handle = groove + 2 * margin;
    const Theme::Palette& theme = Theme::instance().palette();
    const QString volumeStyle = QStringLiteral(
        "QSlider#PadVolume { background: transparent; }"
        "QSlider#PadVolume::groove:horizontal {"
        " height: %1px; background: %2; border-radius: %3px; }"
        "QSlider#PadVolume::sub-page:horizontal {"
        " background: %4; border-radius: %3px; }"
        "QSlider#PadVolume::handle:horizontal {"
        " background: %5; width: %6px; margin: -%7px 0; border-radius: %8px; }")
        .arg(groove)
        .arg(theme.sliderGroove.name(QColor::HexRgb))
        .arg(std::max(1, groove / 2))
        .arg(theme.playheadDelay.name(QColor::HexRgb))
        .arg(theme.sliderHandle.name(QColor::HexRgb))
        .arg(handle)
        .arg(margin)
        .arg(handle / 2);
    // Re-polishing a stylesheet is slow; during a window resize every pad lands here many
    // times with the same result, so only apply it when it actually changes.
    if (volumeStyle != m_volumeStyle) {
        m_volumeStyle = volumeStyle;
        m_volume->setStyleSheet(volumeStyle);
    }
}

void SoundPadWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    refreshChrome(); // visibility depends on size as well as state; this also lays out
}

QRect SoundPadWidget::padCardRect() const
{
    return m_card ? m_card->geometry() : rect().adjusted(4, 4, -4, -4);
}

void SoundPadWidget::setInteractive(bool enabled)
{
    setEnabled(enabled);
}

void SoundPadWidget::setFadeVolume(float fade)
{
    m_fadeVolume = fade;
}

void SoundPadWidget::setLoadQueue(SampleLoadQueue* queue)
{
    m_queue = queue;
}

bool SoundPadWidget::isLoading() const
{
    return m_loadTotal > 0 && m_nextCommit < m_loadTotal;
}

void SoundPadWidget::cancelLoading()
{
    m_loadGeneration->fetch_add(1);
    m_incoming.clear();
    m_reloads.clear();
    m_slots.clear();
    m_loadTotal = 0;
    m_nextCommit = 0;
    m_finishSent = false;
    m_notifyWhenDone = false;
    update();
}

void SoundPadWidget::enqueueSample(const QString& path, float pitch, float gain, float pan, bool panRandom, bool spatialise)
{
    if (path.isEmpty()) {
        return;
    }
    if (m_nextCommit >= m_loadTotal) {
        m_finishSent = false;
    }
    LoadSlot slot;
    slot.path = path;
    slot.pitch = pitch;
    slot.gain = gain;
    slot.pan = pan;
    slot.panRandom = panRandom;
    slot.spatialise = spatialise;
    m_slots.push_back(slot);
    const int index = m_loadTotal++;
    m_config->lastPath = QFileInfo(path).absolutePath();

    if (!m_queue) {
        DecodedAudio audio = OpenALSoundPlayer::decodeFile(std::filesystem::path(path.toStdString()), m_stream);
        submitDecoded(index, m_loadGeneration->load(), std::move(audio));
        return;
    }

    SampleLoadJob job;
    job.sceneId = m_sceneId;
    job.padId = m_padId;
    job.sampleIndex = index;
    job.generation = m_loadGeneration->load();
    job.generationToken = m_loadGeneration;
    job.path = path;
    job.stream = m_stream;
    m_queue->enqueue(job);
    update();
    emit loadStateChanged();
}

void SoundPadWidget::submitDecoded(int index, int generation, DecodedAudio audio)
{
    if (!m_loadGeneration || generation != m_loadGeneration->load()) {
        return;
    }
    if (index < 0 || index >= static_cast<int>(m_slots.size())) {
        return;
    }
    m_incoming.insert_or_assign(index, std::move(audio));
    flushIncoming();
}

void SoundPadWidget::setSpatialisedStereo(int index, bool on)
{
    if (index < 0 || index >= static_cast<int>(m_player.player.size())) {
        return;
    }
    AudioSample* sample = m_player.player[static_cast<size_t>(index)];
    if (sample->audioPlayer->getNumChannels() != 2) {
        return;
    }
    if (sample->isSpatialisedStereo() == on) {
        m_reloads.erase(sample->id);
        return;
    }

    const int serial = ++m_reloadSerial;
    m_reloads[sample->id] = PendingReload{ serial, on };

    SampleLoadJob job;
    job.sceneId = m_sceneId;
    job.padId = m_padId;
    job.generation = m_loadGeneration->load();
    job.generationToken = m_loadGeneration;
    job.path = QString::fromStdString(sample->sample_path);
    job.stream = m_stream;
    job.reloadSampleId = sample->id;
    job.reloadSerial = serial;

    if (!m_queue) {
        submitReload(job, OpenALSoundPlayer::decodeFile(std::filesystem::path(job.path.toStdString()), job.stream));
        return;
    }
    m_queue->enqueue(job);
    emit loadStateChanged();
}

bool SoundPadWidget::isSpatialisedStereo(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_player.player.size())) {
        return false;
    }
    const AudioSample* sample = m_player.player[static_cast<size_t>(index)];
    const auto pending = m_reloads.find(sample->id);
    return pending != m_reloads.end() ? pending->second.spatialise : sample->isSpatialisedStereo();
}

void SoundPadWidget::submitReload(const SampleLoadJob& job, DecodedAudio audio)
{
    if (!m_loadGeneration || job.generation != m_loadGeneration->load()) {
        return;
    }
    const auto pending = m_reloads.find(job.reloadSampleId);
    if (pending == m_reloads.end() || pending->second.serial != job.reloadSerial) {
        return;
    }
    const bool spatialise = pending->second.spatialise;
    m_reloads.erase(pending);

    int index = -1;
    for (int i = 0; i < static_cast<int>(m_player.player.size()); ++i) {
        if (m_player.player[static_cast<size_t>(i)]->id == job.reloadSampleId) {
            index = i;
            break;
        }
    }
    if (index < 0 || !audio.ok) {
        emit loadStateChanged();
        return;
    }

    AudioSample* sample = m_player.player[static_cast<size_t>(index)];
    OpenALSoundPlayer* player = sample->audioPlayer;
    const bool resume = index == m_player.curSound && player->isPlaying();
    const int positionMs = player->getPositionMS();
    const float reverb = player->getReverbSend();
    const float reverb2 = player->getReverbSend2();

    if (!player->uploadDecoded(std::move(audio), spatialise)) {
        qWarning() << "SoundPadWidget: couldn't reload" << job.path;
        emit loadStateChanged();
        return;
    }
    sample->setPitch(sample->getPitch());
    player->setReverbSend(reverb);
    player->setReverbSend2(reverb2);
    if (resume) {
        player->setPositionMS(positionMs);
        player->setPaused(false);
    }
    update();
    emit loadStateChanged();
}

bool SoundPadWidget::commitDecoded(int slotIndex, DecodedAudio audio)
{
    if (slotIndex < 0 || slotIndex >= static_cast<int>(m_slots.size()) || !audio.ok) {
        return false;
    }
    auto* sample = new AudioSample();
    sample->audioPlayer = new OpenALSoundPlayer();
    int maxId = 0;
    for (AudioSample* existing : m_player.player) {
        maxId = std::max(maxId, existing->id);
    }
    sample->id = m_player.player.empty() ? 0 : maxId + 1;
    const LoadSlot& slot = m_slots[static_cast<size_t>(slotIndex)];
    if (!sample->audioPlayer->uploadDecoded(std::move(audio), slot.spatialise)) {
        delete sample;
        return false;
    }
    sample->sample_path = slot.path.toStdString();
    sample->setPitch(slot.pitch);
    sample->setGain(slot.gain);
    sample->setPan(slot.pan);
    if (slot.panRandom) {
        m_player.setRandomPan(true);
    }
    m_player.player.push_back(sample);
    setupLoadedSound(slot.path);
    m_player.recalculateDelay(static_cast<int>(m_player.player.size()) - 1);
    m_player.setReverbSend(m_reverb);
    m_player.setReverbSend2(m_reverb2);
    return true;
}

void SoundPadWidget::flushIncoming()
{
    while (m_incoming.find(m_nextCommit) != m_incoming.end()) {
        DecodedAudio audio = std::move(m_incoming[m_nextCommit]);
        m_incoming.erase(m_nextCommit);
        const int slotIndex = m_nextCommit++;
        if (!commitDecoded(slotIndex, std::move(audio))) {
            m_slots[static_cast<size_t>(slotIndex)].failed = true;
        }
    }
    update();
    emit loadStateChanged();
    if (m_loadTotal > 0 && m_nextCommit >= m_loadTotal && !m_finishSent) {
        m_finishSent = true;
        emit loadingFinished();
        if (m_notifyWhenDone) {
            m_notifyWhenDone = false;
            emit filesDropped();
        }
    }
}

std::vector<SoundPadWidget::LoadSlot> SoundPadWidget::sampleSpecs() const
{
    if (!isLoading()) {
        std::vector<LoadSlot> specs;
        const int count = std::min(static_cast<int>(m_player.player.size()), static_cast<int>(m_soundPaths.size()));
        for (int i = 0; i < count; ++i) {
            LoadSlot spec;
            spec.path = QString::fromStdString(m_soundPaths[static_cast<size_t>(i)]);
            spec.pitch = m_player.player[static_cast<size_t>(i)]->getPitch();
            spec.gain = m_player.player[static_cast<size_t>(i)]->getGain();
            spec.pan = m_player.player[static_cast<size_t>(i)]->getPan();
            spec.panRandom = m_player.isRandomPan();
            spec.spatialise = isSpatialisedStereo(i);
            specs.push_back(spec);
        }
        return specs;
    }

    std::vector<LoadSlot> specs;
    int playerIndex = 0;
    for (int i = 0; i < static_cast<int>(m_slots.size()); ++i) {
        if (m_slots[static_cast<size_t>(i)].failed) {
            continue;
        }
        LoadSlot spec = m_slots[static_cast<size_t>(i)];
        if (i < m_nextCommit && playerIndex < static_cast<int>(m_player.player.size())) {
            AudioSample* sample = m_player.player[static_cast<size_t>(playerIndex)];
            spec.pitch = sample->getPitch();
            spec.gain = sample->getGain();
            spec.pan = sample->getPan();
            spec.panRandom = m_player.isRandomPan();
            spec.spatialise = isSpatialisedStereo(playerIndex);
            ++playerIndex;
        }
        specs.push_back(spec);
    }
    return specs;
}

void SoundPadWidget::loadFromJson(const QJsonObject& root)
{
    // Older files store pads under "scene<id>" but scene names under "scene<position>", so search every scene.
    const QString prefix = QString("%1-%2").arg(m_sceneId).arg(m_padId);
    QJsonObject pad;
    for (auto it = root.begin(); it != root.end() && pad.isEmpty(); ++it) {
        pad = it.value().toObject().value(prefix).toObject();
    }
    if (pad.isEmpty()) {
        return;
    }

    clearPad();
    m_notifyWhenDone = false;
    m_stream = pad.value(QStringLiteral("isstream")).toBool(true);
    m_player.minDelay = pad.value(QStringLiteral("mindelay")).toInt();
    m_player.maxDelay = pad.value(QStringLiteral("maxdelay")).toInt();
    m_player.bRandomPlayback = pad.value(QStringLiteral("playrandom")).toBool();
    setLooping(pad.value(QStringLiteral("loop")).toBool(m_config->loopByDefault));
    setSoundName(pad.value(QStringLiteral("soundname")).toString());
    setPadVolume(static_cast<float>(pad.value(QStringLiteral("volume")).toDouble(0.7)));
    m_sampleRate = pad.value(QStringLiteral("samplerate")).toInt();
    m_channels = pad.value(QStringLiteral("channels")).toInt();
    m_reverb = static_cast<float>(pad.value(QStringLiteral("reverbsend")).toDouble());
    m_reverb2 = static_cast<float>(pad.value(QStringLiteral("reverbsend2")).toDouble());
    m_player.setup(m_config, m_padId);
    m_player.setReverbSend(m_reverb);
    m_player.setReverbSend2(m_reverb2);

    const QJsonObject samples = pad.value(QStringLiteral("samples")).toObject();
    for (int i = 0;; ++i) {
        const QJsonObject sample = samples.value(QString("sample-%1").arg(i)).toObject();
        if (sample.isEmpty()) {
            break;
        }
        const QString path = sample.value(QStringLiteral("path")).toString();
        if (path.isEmpty()) {
            continue;
        }
        enqueueSample(path,
            static_cast<float>(sample.value(QStringLiteral("pitch")).toDouble(1.0)),
            static_cast<float>(sample.value(QStringLiteral("gain")).toDouble(1.0)),
            static_cast<float>(sample.value(QStringLiteral("pan")).toDouble()),
            sample.value(QStringLiteral("panrandom")).toBool(),
            sample.value(QStringLiteral("spatialise")).toBool());
    }
}

void SoundPadWidget::saveToJson(QJsonObject& sceneObj) const
{
    if (m_soundPaths.empty()) {
        return;
    }

    const QString prefix = QString("%1-%2").arg(m_sceneId).arg(m_padId);
    QJsonObject pad;
    pad.insert(QStringLiteral("isstream"), m_stream);
    pad.insert(QStringLiteral("loop"), isLooping());
    pad.insert(QStringLiteral("soundname"), soundName());
    pad.insert(QStringLiteral("volume"), padVolume());
    pad.insert(QStringLiteral("samplerate"), m_sampleRate);
    pad.insert(QStringLiteral("channels"), m_channels);
    pad.insert(QStringLiteral("playrandom"), m_player.bRandomPlayback);
    pad.insert(QStringLiteral("panrandom"), m_player.isRandomPan());
    pad.insert(QStringLiteral("reverbsend"), m_player.getReverbSend());
    pad.insert(QStringLiteral("reverbsend2"), m_player.player.empty() ? m_reverb2 : m_player.getReverbSend2());
    pad.insert(QStringLiteral("mindelay"), m_player.minDelay);
    pad.insert(QStringLiteral("maxdelay"), m_player.maxDelay);

    QJsonObject samples;
    const int count = std::min(static_cast<int>(m_player.player.size()), static_cast<int>(m_soundPaths.size()));
    for (int i = 0; i < count; ++i) {
        QJsonObject sample;
        QString path = QString::fromStdString(m_soundPaths[i]);
        if (path.isEmpty()) {
            path = QString::fromStdString(m_player.player[i]->sample_path);
        }
        sample.insert(QStringLiteral("path"), path);
        sample.insert(QStringLiteral("gain"), m_player.player[i]->getGain());
        sample.insert(QStringLiteral("pitch"), m_player.player[i]->getPitch());
        sample.insert(QStringLiteral("pan"), m_player.player[i]->getPan());
        sample.insert(QStringLiteral("panrandom"), m_player.isRandomPan());
        sample.insert(QStringLiteral("spatialise"), isSpatialisedStereo(i));
        sample.insert(QStringLiteral("duration"), m_player.player[i]->audioPlayer->getDuration());
        samples.insert(QString("sample-%1").arg(i), sample);
    }
    pad.insert(QStringLiteral("samples"), samples);
    sceneObj.insert(prefix, pad);
}

void SoundPadWidget::loadFiles(const QStringList& paths, bool clearExisting)
{
    if (paths.isEmpty()) {
        return;
    }
    if (clearExisting) {
        clearPad();
    }
    m_notifyWhenDone = true;
    if (soundName().isEmpty()) {
        setSoundName(QFileInfo(paths.front()).completeBaseName());
    }
    for (const QString& path : paths) {
        enqueueSample(path, 1.0f, 1.0f, 0.0f, false, false);
    }
}

void SoundPadWidget::clearPad()
{
    cancelLoading();
    m_player.stop();
    m_player.close();
    m_soundPaths.clear();
    m_sampleRate = 0;
    m_channels = 0;
    m_name->clear();
    setLooping(m_config->loopByDefault);
    setPadVolume(0.7f);
    m_player.minDelay = 0;
    m_player.maxDelay = 0;
    m_player.bRandomPlayback = false;
    m_player.bRandomPan = false;
    m_playhead->setProgress(0.0f);
    m_playhead->setTimeText(QString());
    m_playhead->setVisible(false);
    m_reverb = 0.0f;
    m_reverb2 = 0.0f;
}

void SoundPadWidget::setReverbSend(float send)
{
    m_reverb = send;
    m_player.setReverbSend(send);
}

void SoundPadWidget::setReverbSend2(float send)
{
    m_reverb2 = send;
    m_player.setReverbSend2(send);
}

void SoundPadWidget::copyFrom(SoundPadWidget& other)
{
    const std::vector<LoadSlot> specs = other.sampleSpecs();
    const bool stream = other.m_stream;
    const int sampleRate = other.m_sampleRate;
    const int channels = other.m_channels;
    const bool looping = other.isLooping();
    const int minDelay = other.m_player.minDelay;
    const int maxDelay = other.m_player.maxDelay;
    const bool randomPlayback = other.m_player.bRandomPlayback;
    const bool randomPan = other.m_player.isRandomPan();
    const QString name = other.soundName();
    const float volume = other.padVolume();
    const float reverb = (other.isLoading() || other.m_player.player.empty())
        ? other.m_reverb
        : other.m_player.getReverbSend();
    const float reverb2 = (other.isLoading() || other.m_player.player.empty())
        ? other.m_reverb2
        : other.m_player.getReverbSend2();

    clearPad();
    m_stream = stream;
    m_sampleRate = sampleRate;
    m_channels = channels;
    setLooping(looping);
    m_player.minDelay = minDelay;
    m_player.maxDelay = maxDelay;
    m_player.bRandomPlayback = randomPlayback;
    m_player.setRandomPan(randomPan);
    setSoundName(name);
    setPadVolume(volume);
    m_reverb = reverb;
    m_reverb2 = reverb2;
    m_notifyWhenDone = true;
    for (const LoadSlot& spec : specs) {
        enqueueSample(spec.path, spec.pitch, spec.gain, spec.pan, spec.panRandom || randomPan, spec.spatialise);
    }
}

int SoundPadWidget::moveSample(int from, int insertIndex)
{
    if (isLoading()) {
        return -1;
    }
    const int count = static_cast<int>(m_player.player.size());
    if (from < 0 || from >= count || insertIndex < 0 || insertIndex > count) {
        return -1;
    }
    if (insertIndex == from || insertIndex == from + 1) {
        return -1;
    }

    AudioSample* sample = m_player.player[static_cast<size_t>(from)];
    m_player.player.erase(m_player.player.begin() + from);
    const int dest = insertIndex > from ? insertIndex - 1 : insertIndex;
    m_player.player.insert(m_player.player.begin() + dest, sample);

    if (from < static_cast<int>(m_soundPaths.size())) {
        std::string path = std::move(m_soundPaths[static_cast<size_t>(from)]);
        m_soundPaths.erase(m_soundPaths.begin() + from);
        const int pathDest = std::min(dest, static_cast<int>(m_soundPaths.size()));
        m_soundPaths.insert(m_soundPaths.begin() + pathDest, std::move(path));
    }

    const auto adjust = [from, dest](int idx) {
        if (idx == from) {
            return dest;
        }
        if (from < idx && dest >= idx) {
            return idx - 1;
        }
        if (from > idx && dest <= idx) {
            return idx + 1;
        }
        return idx;
    };
    m_player.curSound = adjust(m_player.curSound);
    return dest;
}

void SoundPadWidget::removeSampleAt(int index)
{
    if (isLoading()) {
        return;
    }
    if (index < 0 || index >= static_cast<int>(m_player.player.size())) {
        return;
    }
    m_player.stop();
    m_reloads.erase(m_player.player[index]->id);
    delete m_player.player[index];
    m_player.player.erase(m_player.player.begin() + index);
    m_soundPaths.erase(m_soundPaths.begin() + index);
    if (m_player.player.empty()) {
        clearPad();
    }
}

void SoundPadWidget::beginNameEdit()
{
    if (!m_name->isReadOnly()) {
        return;
    }
    m_nameBeforeEdit = m_name->text();
    m_name->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    m_name->setReadOnly(false);
    m_name->setFocusPolicy(Qt::StrongFocus);
    m_name->setCursor(Qt::IBeamCursor);
    m_name->setFocus(Qt::MouseFocusReason);
    m_name->selectAll();
}

void SoundPadWidget::endNameEdit()
{
    // Read-only and see-through to the mouse, so a single click selects the pad and a drag
    // that starts on the title picks up the pad like anywhere else on it.
    m_name->setReadOnly(true);
    m_name->setFocusPolicy(Qt::NoFocus);
    m_name->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_name->unsetCursor();
    m_name->deselect();
    m_name->setCursorPosition(0);
    if (m_name->hasFocus()) {
        m_name->clearFocus();
    }
}

void SoundPadWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_name->isReadOnly()) {
        const QRect nameRect(m_name->mapTo(this, QPoint(0, 0)), m_name->size());
        if (nameRect.contains(event->position().toPoint())) {
            emit padClicked(m_padId);
            beginNameEdit();
            return;
        }
    }
    QWidget::mouseDoubleClickEvent(event);
}

bool SoundPadWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_name && event->type() == QEvent::KeyPress && !m_name->isReadOnly()
        && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
        m_name->setText(m_nameBeforeEdit);
        m_name->clearFocus(); // finishes editing through editingFinished
        return true;
    }
    if (event->type() == QEvent::MouseButtonPress && static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
        emit padClicked(m_padId);
    }
    return QWidget::eventFilter(watched, event);
}

void SoundPadWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragStart = event->pos();
        emit padClicked(m_padId);
    }
    QWidget::mousePressEvent(event);
}

namespace {
// The pad as it looks now, shrunk and faded, for the drag image.
QPixmap padDragGhost(QWidget* pad, qreal scale)
{
    const QPixmap full = pad->grab();
    const qreal dpr = full.devicePixelRatio();
    const QSize logical(qRound(pad->width() * scale), qRound(pad->height() * scale));
    QPixmap ghost(logical * dpr);
    ghost.setDevicePixelRatio(dpr);
    ghost.fill(Qt::transparent);
    QPainter p(&ghost);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setOpacity(0.85);
    p.drawPixmap(QRect(QPoint(0, 0), logical), full);
    return ghost;
}

// Pointer with a round accent badge: four arrows for move, a plus for copy.
QPixmap padDragCursor(bool copy, qreal dpr)
{
    const Theme::Palette& theme = Theme::instance().palette();
    const int size = 40;
    QPixmap pix(QSize(size, size) * dpr);
    pix.setDevicePixelRatio(dpr);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Arrow pointer, tip at (1,1) so the hot spot stays where the system puts it.
    QPolygonF arrow;
    arrow << QPointF(1, 1) << QPointF(1, 18) << QPointF(5.5, 14) << QPointF(8.5, 21)
          << QPointF(11.5, 19.8) << QPointF(8.6, 13) << QPointF(14.5, 13);
    p.setPen(QPen(QColor(0, 0, 0), 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(QColor(255, 255, 255));
    p.drawPolygon(arrow);

    // Badge.
    const QPointF c(27, 27);
    const qreal r = 10.5;
    p.setPen(QPen(theme.background, 2.0));
    p.setBrush(theme.playLoaded);
    p.drawEllipse(c, r, r);
    const QColor mark = Theme::contrastOn(theme.playLoaded);
    p.setPen(QPen(mark, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    if (copy) {
        p.drawLine(QPointF(c.x() - 5, c.y()), QPointF(c.x() + 5, c.y()));
        p.drawLine(QPointF(c.x(), c.y() - 5), QPointF(c.x(), c.y() + 5));
    } else {
        // Cross with solid arrowheads on all four ends.
        const qreal a = 4.0;   // half-length of the cross bars
        const qreal tip = 7.0; // distance from centre to each arrow tip
        const qreal w = 3.0;   // half-width of each arrowhead
        p.drawLine(QPointF(c.x() - a, c.y()), QPointF(c.x() + a, c.y()));
        p.drawLine(QPointF(c.x(), c.y() - a), QPointF(c.x(), c.y() + a));
        p.setPen(Qt::NoPen);
        p.setBrush(mark);
        const QPointF dirs[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const QPointF& d : dirs) {
            const QPointF side(d.y(), d.x());
            QPolygonF head;
            head << c + d * tip << c + d * (tip - 3.2) + side * w << c + d * (tip - 3.2) - side * w;
            p.drawPolygon(head);
        }
    }
    return pix;
}
}

void SoundPadWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (!(event->buttons() & Qt::LeftButton)) {
        return;
    }
    if ((event->pos() - m_dragStart).manhattanLength() < QApplication::startDragDistance()) {
        return;
    }
    if (isLoading() || !isLoaded()) {
        return;
    }
    emit padDragStarted(m_padId);
    auto* drag = new QDrag(this);
    auto* mime = new QMimeData();
    mime->setData(QStringLiteral("application/x-feedra-pad"), QByteArray::number(m_padId));
    drag->setMimeData(mime);
    // A small see-through copy of the pad rides under the pointer, and the pointer carries a
    // badge saying what the drop will do: four arrows to move, a plus to copy.
    const qreal dpr = devicePixelRatioF();
    const QPixmap ghost = padDragGhost(this, 0.6);
    drag->setPixmap(ghost);
    drag->setHotSpot(QPoint(qRound(event->position().x() * 0.6), qRound(event->position().y() * 0.6)));
    drag->setDragCursor(padDragCursor(false, dpr), Qt::MoveAction);
    drag->setDragCursor(padDragCursor(true, dpr), Qt::CopyAction);
    // A plain drag moves the pad; holding Ctrl (or Alt, the macOS habit) copies it instead.
    drag->exec(Qt::MoveAction | Qt::CopyAction, Qt::MoveAction);
}

namespace {
bool isPadDrag(const QMimeData* mime)
{
    return mime->hasFormat(QStringLiteral("application/x-feedra-pad"));
}

// Choose move or copy from the keys held right now, so the cursor updates as Ctrl is
// pressed or released mid-drag.
void choosePadDropAction(QDropEvent* event)
{
    const bool copy = event->modifiers() & (Qt::ControlModifier | Qt::AltModifier);
    event->setDropAction(copy ? Qt::CopyAction : Qt::MoveAction);
    event->accept();
}
}

void SoundPadWidget::dragEnterEvent(QDragEnterEvent* event)
{
    if (isPadDrag(event->mimeData())) {
        choosePadDropAction(event);
    } else if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void SoundPadWidget::dragMoveEvent(QDragMoveEvent* event)
{
    if (isPadDrag(event->mimeData())) {
        choosePadDropAction(event);
    } else if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void SoundPadWidget::dropEvent(QDropEvent* event)
{
    if (event->mimeData()->hasFormat(QStringLiteral("application/x-feedra-pad"))) {
        const int from = event->mimeData()->data(QStringLiteral("application/x-feedra-pad")).toInt();
        choosePadDropAction(event);
        emit padDropped(from, m_padId, event->dropAction() == Qt::CopyAction);
        return;
    }
    if (event->mimeData()->hasUrls()) {
        QStringList paths;
        for (const QUrl& url : event->mimeData()->urls()) {
            if (url.isLocalFile()) {
                paths << url.toLocalFile();
            }
        }
        loadFiles(paths, true);
        emit padClicked(m_padId);
        event->acceptProposedAction();
    }
}

void SoundPadWidget::chooseFiles()
{
    QString start = m_config->lastPath;
    if (start.isEmpty()) {
        start = m_config->libraryLocation();
    }
    const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Load files"), start, audioNameFilters().join(QStringLiteral(";;")));
    if (!paths.isEmpty()) {
        loadFiles(paths, true);
        emit padClicked(m_padId);
    }
}

void SoundPadWidget::setupLoadedSound(const QString& path)
{
    m_soundPaths.push_back(path.toStdString());
    m_config->lastPath = QFileInfo(path).absolutePath();
    m_player.setLoop(isLooping());
    m_sampleRate = m_player.getSampleRate();
    m_channels = m_player.getNumChannels();
}

void SoundPadWidget::applyVolume()
{
    if (!m_player.isLoaded() || m_player.player.empty()) {
        return;
    }
    // SoundPlayer multiplies in each sample's own gain, and also applies this to a sample
    // right before it starts, so a newly selected sample never starts at the wrong volume.
    m_player.setBaseVolume(padVolume() * m_config->masterVolume() * m_config->masterFade() * m_fadeVolume);
}

void SoundPadWidget::updateVolumeLabel()
{
    if (!m_volumeValue) {
        return;
    }
    const float db = VolumeDb::fromSlider(m_volume->value(), VolumeDb::kFloorDb);
    m_volumeValue->setText(m_compactText
            ? QString::number(static_cast<int>(std::lround(db))) + QStringLiteral(" dB")
            : VolumeDb::format(db));
    m_volumeValue->setToolTip(VolumeDb::format(db));
}

QString SoundPadWidget::positionTimeText() const
{
    const float duration = m_player.getDuration();
    return QString(m_compactText ? QStringLiteral("%1/%2") : QStringLiteral("%1 / %2"))
        .arg(formatClock(m_player.getPosition() * duration), formatClock(duration));
}

QString SoundPadWidget::remainingTimeText() const
{
    const float total = m_player.isPlayingDelay() ? m_player.getTotalDelay() : m_player.getDuration();
    return formatClock((1.0f - m_player.getPosition()) * total);
}
