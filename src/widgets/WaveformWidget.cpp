#include "WaveformWidget.h"

#include "OpenALSoundPlayer.h"
#include "PeakStore.h"
#include "Theme.h"

#include <QCheckBox>
#include <QContextMenuEvent>
#include <QFileInfo>
#include <QMenu>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <algorithm>
#include <cmath>

namespace {
constexpr int kMarginX = 20;
constexpr int kHeaderHeight = 32;
constexpr int kMarginBottom = 12;
constexpr int kHandleGrab = 6;      // px either side of a handle line that picks it up
constexpr int kTabWidth = 14;
constexpr int kTabHeight = 13;
constexpr double kMinLoopSec = 0.05; // shortest loop the handles allow
}

WaveformWidget::WaveformWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("Waveform"));
    setMouseTracking(true); // for the hand cursor over the waveform
    m_loopBox = new QCheckBox(tr("Loop"), this);
    m_loopBox->setObjectName(QStringLiteral("WaveToggle"));
    m_loopBox->setToolTip(tr("Loop this sample between its start and end points"));
    m_fromStartBox = new QCheckBox(tr("Play from start"), this);
    m_fromStartBox->setObjectName(QStringLiteral("WaveToggle"));
    m_fromStartBox->setToolTip(tr("On: play from 0:00, so anything before the loop start is an intro.\n"
                                  "Off: begin at the loop start."));
    m_toEndBox = new QCheckBox(tr("Play to end"), this);
    m_toEndBox->setObjectName(QStringLiteral("WaveToggle"));
    m_toEndBox->setToolTip(tr("When not looping: on, play on past the loop end to the end of the file (an outro).\n"
                              "Off: stop at the loop end."));
    m_toEndBox->setChecked(true);
    m_toEndBox->setEnabled(false);
    connect(m_toEndBox, &QCheckBox::clicked, this, [this](bool on) {
        m_loop.playToEnd = on;
        update();
        emit playToEndToggled(on);
    });
    m_xfBox = new QSpinBox(this);
    m_xfBox->setObjectName(QStringLiteral("WaveCrossfade"));
    m_xfBox->setPrefix(tr("Crossfade "));
    m_xfBox->setSuffix(tr(" ms"));
    m_xfBox->setRange(0, static_cast<int>(LoopRegion::kMaxCrossfadeMs));
    m_xfBox->setSingleStep(10);
    m_xfBox->setAccelerated(true);
    m_xfBox->setKeyboardTracking(false); // typed values apply on Enter or leaving the box
    m_xfBox->setToolTip(tr("This sample's loop crossfade: blends the end of the loop into its start.\n"
                           "Up to 10 s or half the sample, and never more than half the loop.\n"
                           "Starting at the loop start and stopping at a moved end use a short 10 ms fade."));
    m_xfBox->setValue(10);
    m_loopBox->setEnabled(false);
    m_fromStartBox->setEnabled(false);
    m_xfBox->setEnabled(false);
    connect(m_xfBox, qOverload<int>(&QSpinBox::valueChanged), this, [this](int ms) {
        m_loop.crossfadeMs = ms;
        emit crossfadeChanged(ms);
    });
    connect(m_loopBox, &QCheckBox::clicked, this, [this](bool on) {
        m_loop.loop = on;
        update();
        emit loopToggled(on);
    });
    connect(m_fromStartBox, &QCheckBox::clicked, this, [this](bool on) {
        m_loop.playFromStart = on;
        update();
        emit playFromStartToggled(on);
    });
    connect(&Theme::instance(), &Theme::changed, this, [this]() {
        m_waveDirty = true;
        update();
    });
    connect(&PeakStore::instance(), &PeakStore::cacheCleared, this, [this]() {
        m_waveDirty = true;
        update();
    });
    connect(&PeakStore::instance(), &PeakStore::peaksReady, this, [this](const QString& key) {
        if (!m_path.isEmpty() && key == PeakStore::keyFor(m_path)) {
            m_waveDirty = true;
            update();
        }
    });
}

WaveformWidget::~WaveformWidget() = default;

void WaveformWidget::setSample(const QString& path, float durationSec)
{
    m_duration = durationSec;
    {
        // Up to 10 s, or half the sample, whichever is shorter.
        const int limit = durationSec > 0.0f
            ? std::min(static_cast<int>(LoopRegion::kMaxCrossfadeMs), static_cast<int>(durationSec * 500.0f))
            : static_cast<int>(LoopRegion::kMaxCrossfadeMs);
        if (m_xfBox->maximum() != limit) {
            const QSignalBlocker blocker(m_xfBox);
            m_xfBox->setMaximum(std::max(0, limit));
        }
    }
    if (path == m_path) {
        return;
    }
    m_path = path;
    m_playhead = 0.0f; // a newly shown sample starts at the beginning, never "fully played"
    m_waveDirty = true;
    m_dragHandle = Handle::None;
    m_dragging = false;
    m_loopBox->setEnabled(!m_path.isEmpty());
    m_fromStartBox->setEnabled(!m_path.isEmpty());
    m_toEndBox->setEnabled(!m_path.isEmpty());
    m_xfBox->setEnabled(!m_path.isEmpty());
    if (m_path.isEmpty()) {
        setLoopView(LoopView{});
    }
    // Queue it ahead of the pad strips' files: this one is on screen.
    PeakStore::instance().get(m_path, true);
    update();
}

void WaveformWidget::setPlayhead(float pct, bool playing)
{
    if (pct < 0.0f) {
        // Position momentarily unknown (e.g. mid-refill): keep the last one rather than blink.
        pct = m_playhead;
    } else {
        pct = std::clamp(pct, 0.0f, 1.0f);
    }
    const int oldX = m_playhead < 0.0f ? -1 : static_cast<int>(m_playhead * waveRect().width());
    const int newX = pct < 0.0f ? -1 : static_cast<int>(pct * waveRect().width());
    const bool changed = oldX != newX || playing != m_playing;
    m_playhead = pct;
    m_playing = playing;
    if (changed) {
        update();
    }
}

void WaveformWidget::setLoopView(const LoopView& view)
{
    if (m_dragHandle != Handle::None) {
        return;
    }
    if (m_loopBox->isChecked() != view.loop) {
        m_loopBox->setChecked(view.loop);
    }
    if (m_fromStartBox->isChecked() != view.playFromStart) {
        m_fromStartBox->setChecked(view.playFromStart);
    }
    if (m_toEndBox->isChecked() != view.playToEnd) {
        m_toEndBox->setChecked(view.playToEnd);
    }
    if (m_xfBox->value() != view.crossfadeMs && !m_xfBox->hasFocus()) {
        const QSignalBlocker blocker(m_xfBox);
        m_xfBox->setValue(view.crossfadeMs);
    }
    if (view == m_loop) {
        return;
    }
    m_loop = view;
    update();
}

bool WaveformWidget::hasCustomRegion() const
{
    return m_loop.startPct > 0.0 || m_loop.endPct < 1.0;
}

int WaveformWidget::xFor(double pct) const
{
    const QRect area = waveRect();
    return area.left() + static_cast<int>(std::lround(std::clamp(pct, 0.0, 1.0) * area.width()));
}

double WaveformWidget::pctAt(int x) const
{
    const QRect area = waveRect();
    if (area.width() <= 0) {
        return 0.0;
    }
    return std::clamp(double(x - area.left()) / double(area.width()), 0.0, 1.0);
}

double WaveformWidget::minLoopPct() const
{
    if (m_duration <= 0.0f) {
        return 0.01;
    }
    // The crossfade shrinks to fit (half the loop), so only the minimum loop length applies.
    return kMinLoopSec / m_duration;
}

WaveformWidget::Handle WaveformWidget::handleAt(const QPoint& pos) const
{
    if (!canSeek()) {
        return Handle::None;
    }
    const QRect area = waveRect();
    if (pos.y() < area.top() - 2 || pos.y() > area.bottom()) {
        return Handle::None;
    }
    const int sx = xFor(m_loop.startPct);
    const int ex = xFor(m_loop.endPct);
    const int ds = std::abs(pos.x() - sx);
    const int de = std::abs(pos.x() - ex);
    // The grab tabs hang inside the loop from the top edge, so they stay apart even when
    // the handles sit at the file edges.
    const bool onStartTab = pos.y() <= area.top() + kTabHeight && pos.x() >= sx && pos.x() <= sx + kTabWidth;
    const bool onEndTab = pos.y() <= area.top() + kTabHeight && pos.x() <= ex && pos.x() >= ex - kTabWidth;
    if (onStartTab) {
        return Handle::Start;
    }
    if (onEndTab) {
        return Handle::End;
    }
    if (ds <= kHandleGrab && ds <= de) {
        return Handle::Start;
    }
    if (de <= kHandleGrab) {
        return Handle::End;
    }
    return Handle::None;
}

void WaveformWidget::dragHandle(int x)
{
    const double pct = pctAt(x);
    const double gap = minLoopPct();
    if (m_dragHandle == Handle::Start) {
        m_loop.startPct = std::clamp(pct, 0.0, std::max(0.0, m_loop.endPct - gap));
    } else if (m_dragHandle == Handle::End) {
        m_loop.endPct = std::clamp(pct, std::min(1.0, m_loop.startPct + gap), 1.0);
    }
    update();
    emit loopPointsChanged(m_loop.startPct, m_loop.endPct, false);
}

void WaveformWidget::resetHandles(bool start, bool end)
{
    if (start) {
        m_loop.startPct = 0.0;
    }
    if (end) {
        m_loop.endPct = 1.0;
    }
    update();
    emit loopPointsChanged(m_loop.startPct, m_loop.endPct, true);
}

bool WaveformWidget::canSeek() const
{
    const std::shared_ptr<const PeakData> peaks = PeakStore::instance().get(m_path, true);
    return peaks && peaks->ok;
}

void WaveformWidget::seekAt(int x)
{
    const QRect area = waveRect();
    if (!canSeek() || area.width() <= 0) {
        return;
    }
    double target = std::clamp(double(x - area.left()) / double(area.width()), 0.0, 1.0);
    // Same limits the player applies: before the end point, and not in the intro when
    // playback begins at the loop start.
    if (m_loop.loop || !m_loop.playToEnd) {
        target = std::min(target, m_loop.endPct);
    }
    if (!m_loop.playFromStart) {
        target = std::max(target, m_loop.startPct);
    }
    const float pct = static_cast<float>(target);
    m_playhead = pct; // jump now; the next tick reports the real (already moved) position
    update();
    emit seekRequested(pct);
}

void WaveformWidget::mousePressEvent(QMouseEvent* event)
{
    const QPoint pos = event->position().toPoint();
    if (event->button() == Qt::LeftButton) {
        const Handle handle = handleAt(pos);
        if (handle != Handle::None) {
            m_dragHandle = handle;
            dragHandle(pos.x());
            event->accept();
            return;
        }
    }
    if (event->button() == Qt::LeftButton && waveRect().contains(pos)) {
        m_dragging = canSeek();
        seekAt(event->position().toPoint().x());
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void WaveformWidget::mouseMoveEvent(QMouseEvent* event)
{
    const QPoint pos = event->position().toPoint();
    if (m_dragHandle != Handle::None && (event->buttons() & Qt::LeftButton)) {
        dragHandle(pos.x());
        event->accept();
        return;
    }
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        seekAt(pos.x());
        event->accept();
        return;
    }
    const Handle hover = handleAt(pos);
    if (hover != m_hoverHandle) {
        m_hoverHandle = hover;
        update();
    }
    if (hover != Handle::None) {
        setCursor(Qt::SizeHorCursor);
    } else {
        setCursor(canSeek() && waveRect().contains(pos) ? Qt::PointingHandCursor : Qt::ArrowCursor);
    }
    QWidget::mouseMoveEvent(event);
}

void WaveformWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        if (m_dragHandle != Handle::None) {
            m_dragHandle = Handle::None;
            emit loopPointsChanged(m_loop.startPct, m_loop.endPct, true);
            update();
        }
    }
    QWidget::mouseReleaseEvent(event);
}

void WaveformWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    // Double-click a handle to put it back at the edge of the file.
    const Handle handle = event->button() == Qt::LeftButton ? handleAt(event->position().toPoint()) : Handle::None;
    if (handle != Handle::None) {
        m_dragHandle = Handle::None;
        resetHandles(handle == Handle::Start, handle == Handle::End);
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void WaveformWidget::contextMenuEvent(QContextMenuEvent* event)
{
    if (!canSeek()) {
        return;
    }
    QMenu menu(this);
    QAction* reset = menu.addAction(tr("Reset loop points"));
    reset->setEnabled(hasCustomRegion());
    QAction* startHere = menu.addAction(tr("Set loop start here"));
    QAction* endHere = menu.addAction(tr("Set loop end here"));
    const double at = pctAt(event->pos().x());
    startHere->setEnabled(at < m_loop.endPct - minLoopPct());
    endHere->setEnabled(at > m_loop.startPct + minLoopPct());
    QAction* chosen = menu.exec(event->globalPos());
    if (chosen == reset) {
        resetHandles(true, true);
    } else if (chosen == startHere) {
        m_loop.startPct = at;
        resetHandles(false, false);
    } else if (chosen == endHere) {
        m_loop.endPct = at;
        resetHandles(false, false);
    }
}

void WaveformWidget::leaveEvent(QEvent* event)
{
    if (m_hoverHandle != Handle::None) {
        m_hoverHandle = Handle::None;
        update();
    }
    QWidget::leaveEvent(event);
}

void WaveformWidget::layoutToggles()
{
    // Right side of the header, left of the time readout.
    QFont mono(QStringLiteral("Geist Mono"));
    mono.setStyleHint(QFont::Monospace);
    const int timeReserve = QFontMetrics(mono).horizontalAdvance(QStringLiteral("88:88 / 88:88")) + 24;
    int right = width() - kMarginX - timeReserve;
    const int h = kHeaderHeight;
    for (QWidget* box : { static_cast<QWidget*>(m_toEndBox), static_cast<QWidget*>(m_fromStartBox), static_cast<QWidget*>(m_xfBox),
                          static_cast<QWidget*>(m_loopBox) }) {
        QSize hint = box->sizeHint();
        hint.setHeight(std::min(hint.height(), h - 4));
        right -= hint.width();
        box->setGeometry(right, (h - hint.height()) / 2, hint.width(), hint.height());
        right -= 14;
    }
}

QRect WaveformWidget::waveRect() const
{
    return rect().adjusted(kMarginX, kHeaderHeight, -kMarginX, -kMarginBottom);
}

void WaveformWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    m_waveDirty = true;
    layoutToggles();
}

void WaveformWidget::rebuildPixmap()
{
    m_waveDirty = false;
    const QRect area = waveRect();
    const std::shared_ptr<const PeakData> data = PeakStore::instance().get(m_path, true);
    if (area.width() <= 0 || area.height() <= 0 || !data || !data->ok) {
        m_wave = QPixmap();
        m_waveDim = QPixmap();
        return;
    }
    const PeakData& peaks = *data;
    const qreal dpr = devicePixelRatioF();
    const int w = std::max(1, static_cast<int>(std::lround(area.width() * dpr)));
    const int h = std::max(1, static_cast<int>(std::lround(area.height() * dpr)));
    QPixmap pix(w, h);
    pix.fill(Qt::transparent);
    QPixmap dim(w, h);
    dim.fill(Qt::transparent);

    const int bins = static_cast<int>(peaks.mins.size());
    const float mid = h * 0.5f;
    const float half = h * 0.5f - 1.0f;
    // Two copies: the accent one is drawn behind the playhead, the neutral one ahead of it.
    QPainter p(&pix);
    QPainter pd(&dim);
    p.setPen(QPen(Theme::instance().palette().playhead, 1.0));
    pd.setPen(QPen(Theme::instance().palette().playheadDelay, 1.0));
    for (int x = 0; x < w; ++x) {
        const int b0 = static_cast<int>(static_cast<int64_t>(x) * bins / w);
        const int b1 = std::max(b0 + 1, static_cast<int>(static_cast<int64_t>(x + 1) * bins / w));
        float lo = 0.0f;
        float hi = 0.0f;
        for (int b = b0; b < b1 && b < bins; ++b) {
            lo = std::min(lo, peaks.mins[static_cast<size_t>(b)]);
            hi = std::max(hi, peaks.maxs[static_cast<size_t>(b)]);
        }
        const int y0 = static_cast<int>(std::floor(mid - hi * half));
        const int y1 = static_cast<int>(std::ceil(mid - lo * half));
        p.drawLine(x, y0, x, std::max(y0, y1));
        pd.drawLine(x, y0, x, std::max(y0, y1));
    }
    p.end();
    pd.end();
    pix.setDevicePixelRatio(dpr);
    dim.setDevicePixelRatio(dpr);
    m_wave = pix;
    m_waveDim = dim;
}

QString WaveformWidget::formatPrecise(double seconds)
{
    const double s = std::max(0.0, seconds);
    const int minutes = static_cast<int>(s / 60.0);
    const double rest = s - minutes * 60.0;
    return QStringLiteral("%1:%2").arg(minutes).arg(rest, 6, 'f', 3, QLatin1Char('0'));
}

QString WaveformWidget::formatTime(float seconds)
{
    const int total = std::max(0, static_cast<int>(seconds));
    return QStringLiteral("%1:%2").arg(total / 60).arg(total % 60, 2, 10, QLatin1Char('0'));
}

void WaveformWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    if (m_waveDirty) {
        rebuildPixmap();
    }
    const Theme::Palette& pal = Theme::instance().palette();
    QPainter p(this);
    const QRect area = waveRect();

    // Header: file name on the left, position / length on the right.
    p.setPen(m_path.isEmpty() ? pal.textMuted : pal.text);
    const QRect header(kMarginX, 0, width() - 2 * kMarginX, kHeaderHeight);
    QString timeText;
    if (!m_path.isEmpty() && m_duration > 0.0f) {
        const float pos = m_playhead >= 0.0f ? m_playhead * m_duration : 0.0f;
        timeText = formatTime(pos) + QStringLiteral(" / ") + formatTime(m_duration);
    }
    const int timeWidth = timeText.isEmpty() ? 0 : p.fontMetrics().horizontalAdvance(timeText) + 12;
    // The name (and the loop range, when set) share what the toggles leave free.
    const int nameRight = std::min(header.right() - timeWidth, m_loopBox->geometry().left() - 12);
    const QRect nameRect(header.left(), header.top(), std::max(0, nameRight - header.left()), header.height());
    QString name = m_path.isEmpty() ? tr("No sample on the selected pad") : QFileInfo(m_path).fileName();
    QString range;
    if (!m_path.isEmpty() && m_duration > 0.0f && hasCustomRegion()) {
        const double a = m_loop.startPct * m_duration;
        const double b = m_loop.endPct * m_duration;
        range = tr("%1 – %2 (%3 s)").arg(formatPrecise(a), formatPrecise(b)).arg(b - a, 0, 'f', 3);
    }
    const int rangeWidth = range.isEmpty() ? 0 : p.fontMetrics().horizontalAdvance(range) + 16;
    const int nameWidth = std::max(0, nameRect.width() - rangeWidth);
    const QString shownName = p.fontMetrics().elidedText(name, Qt::ElideMiddle, nameWidth);
    p.drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, shownName);
    if (!range.isEmpty() && nameRect.width() > rangeWidth + 40) {
        p.save();
        p.setPen(pal.textMuted);
        const int x = nameRect.left() + p.fontMetrics().horizontalAdvance(shownName) + 16;
        p.drawText(QRect(x, nameRect.top(), nameRect.right() - x, nameRect.height()), Qt::AlignLeft | Qt::AlignVCenter, range);
        p.restore();
    }
    if (!timeText.isEmpty()) {
        QFont mono(QStringLiteral("Geist Mono"));
        mono.setStyleHint(QFont::Monospace);
        mono.setPixelSize(std::max(9, font().pixelSize() > 0 ? font().pixelSize() - 1 : 12));
        p.save();
        p.setFont(mono);
        p.setPen(pal.textMuted);
        p.drawText(header, Qt::AlignRight | Qt::AlignVCenter, timeText);
        p.restore();
    }

    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(pal.progressTrack);
    p.drawRoundedRect(area, 8, 8);
    p.setRenderHint(QPainter::Antialiasing, false);

    if (m_path.isEmpty()) {
        return;
    }
    if (m_wave.isNull()) {
        p.setPen(pal.textMuted);
        const std::shared_ptr<const PeakData> data = PeakStore::instance().get(m_path, true);
        const bool failed = data && !data->ok;
        p.drawText(area, Qt::AlignCenter, failed ? tr("Waveform unavailable") : tr("Reading waveform…"));
        return;
    }

    // Centre line, then the waveform: dimmed ahead of the playhead, full strength behind it.
    QColor centre = pal.playheadBorder;
    p.setPen(centre);
    p.drawLine(area.left(), area.center().y(), area.right(), area.center().y());

    const int headX = m_playhead >= 0.0f ? area.left() + static_cast<int>(std::lround(m_playhead * area.width())) : area.left();
    p.save();
    p.setClipRect(QRect(headX, area.top(), area.right() - headX + 1, area.height()));
    p.drawPixmap(area.topLeft(), m_waveDim);
    p.restore();
    if (headX > area.left()) {
        p.save();
        p.setClipRect(QRect(area.left(), area.top(), headX - area.left(), area.height()));
        p.drawPixmap(area.topLeft(), m_wave);
        p.restore();
    }

    // Loop region: what never plays is dimmed and hatched; an intro (before the start, with
    // "play from start" on) is only dimmed a little.
    const int sx = xFor(m_loop.startPct);
    const int ex = xFor(m_loop.endPct);
    QColor shade = pal.progressTrack;
    shade.setAlphaF(0.78);
    QColor hatch = pal.textMuted;
    hatch.setAlphaF(0.22);
    auto shadeOut = [&](const QRect& r, bool silent) {
        if (r.width() <= 0) {
            return;
        }
        QColor c = shade;
        if (!silent) {
            c.setAlphaF(0.4);
        }
        p.fillRect(r, c);
        if (silent) {
            p.fillRect(r, QBrush(hatch, Qt::BDiagPattern));
        }
    };
    // After the end: an outro when it plays on past E (not looping, "play to end").
    const bool outro = m_loop.playToEnd;
    shadeOut(QRect(ex, area.top(), area.right() - ex + 1, area.height()), !outro);
    if (outro && area.right() - ex > 40) {
        p.save();
        p.setPen(pal.textMuted);
        QFont f = font();
        f.setPixelSize(std::max(9, f.pixelSize() > 0 ? f.pixelSize() - 2 : 10));
        p.setFont(f);
        p.drawText(QRect(ex + 6, area.bottom() - 18, area.right() - ex - 10, 16), Qt::AlignRight | Qt::AlignVCenter, tr("outro"));
        p.restore();
    }
    shadeOut(QRect(area.left(), area.top(), sx - area.left(), area.height()), !m_loop.playFromStart);
    if (m_loop.playFromStart && sx - area.left() > 40) {
        p.save();
        p.setPen(pal.textMuted);
        QFont f = font();
        f.setPixelSize(std::max(9, f.pixelSize() > 0 ? f.pixelSize() - 2 : 10));
        p.setFont(f);
        p.drawText(QRect(area.left() + 6, area.bottom() - 18, sx - area.left() - 8, 16), Qt::AlignLeft | Qt::AlignVCenter, tr("intro"));
        p.restore();
    }
    // The crossfade: the last stretch before the end blends into the start.
    if (m_loop.loop && m_loop.crossfadePct > 0.0) {
        const int fx = xFor(m_loop.endPct - m_loop.crossfadePct);
        QColor tint = pal.loopOn;
        tint.setAlphaF(0.22);
        p.fillRect(QRect(fx, area.top(), std::max(1, ex - fx), area.height()), tint);
    }

    if (m_playhead >= 0.0f) {
        QColor head = m_playing ? pal.playheadText : pal.playheadDelay;
        p.setPen(QPen(head, 2.0));
        p.drawLine(headX, area.top(), headX, area.bottom());
    }

    // Handles, drawn last so they stay grabbable over the playhead.
    auto drawHandle = [&](int x, bool isStart, bool active) {
        QColor c = pal.loopOn;
        if (!active) {
            c.setAlphaF(m_loop.loop ? 0.9 : 0.65);
        }
        p.setPen(QPen(c, active ? 2.0 : 1.5));
        p.drawLine(x, area.top(), x, area.bottom());
        const QRect tab = isStart ? QRect(x, area.top(), kTabWidth, kTabHeight)
                                  : QRect(x - kTabWidth, area.top(), kTabWidth, kTabHeight);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawRoundedRect(tab, 3, 3);
        p.setRenderHint(QPainter::Antialiasing, false);
        QFont f = font();
        f.setPixelSize(9);
        f.setBold(true);
        p.setFont(f);
        p.setPen(pal.background);
        p.drawText(tab, Qt::AlignCenter, isStart ? QStringLiteral("S") : QStringLiteral("E"));
        if (active && m_duration > 0.0f) {
            // Time next to the handle while it is dragged.
            const QString t = formatPrecise((isStart ? m_loop.startPct : m_loop.endPct) * m_duration);
            const int w = p.fontMetrics().horizontalAdvance(t) + 10;
            QRect label(isStart ? x + 4 : x - w - 4, area.top() + kTabHeight + 4, w, 16);
            label.moveLeft(std::clamp(label.left(), area.left(), area.right() - w));
            p.setPen(Qt::NoPen);
            QColor bg = pal.panelBackground;
            bg.setAlphaF(0.9);
            p.setBrush(bg);
            p.drawRect(label);
            p.setPen(pal.text);
            p.drawText(label, Qt::AlignCenter, t);
        }
    };
    drawHandle(sx, true, m_dragHandle == Handle::Start || m_hoverHandle == Handle::Start);
    drawHandle(ex, false, m_dragHandle == Handle::End || m_hoverHandle == Handle::End);
}
