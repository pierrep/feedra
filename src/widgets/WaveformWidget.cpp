#include "WaveformWidget.h"

#include "PeakStore.h"
#include "Theme.h"

#include <QFileInfo>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <algorithm>
#include <cmath>

namespace {
constexpr int kMarginX = 20;
constexpr int kHeaderHeight = 32;
constexpr int kMarginBottom = 12;
}

WaveformWidget::WaveformWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("Waveform"));
    setMouseTracking(true); // for the hand cursor over the waveform
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
    if (path == m_path) {
        return;
    }
    m_path = path;
    m_playhead = 0.0f; // a newly shown sample starts at the beginning, never "fully played"
    m_waveDirty = true;
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
    const float pct = std::clamp(static_cast<float>(x - area.left()) / static_cast<float>(area.width()), 0.0f, 1.0f);
    m_playhead = pct; // jump now; the next tick reports the real (already moved) position
    update();
    emit seekRequested(pct);
}

void WaveformWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && waveRect().adjusted(0, -kHeaderHeight, 0, 0).contains(event->position().toPoint())) {
        m_dragging = canSeek();
        seekAt(event->position().toPoint().x());
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void WaveformWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        seekAt(event->position().toPoint().x());
        event->accept();
        return;
    }
    setCursor(canSeek() && waveRect().contains(event->position().toPoint()) ? Qt::PointingHandCursor : Qt::ArrowCursor);
    QWidget::mouseMoveEvent(event);
}

void WaveformWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
    }
    QWidget::mouseReleaseEvent(event);
}

QRect WaveformWidget::waveRect() const
{
    return rect().adjusted(kMarginX, kHeaderHeight, -kMarginX, -kMarginBottom);
}

void WaveformWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    m_waveDirty = true;
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
    p.drawText(header.adjusted(0, 0, -timeWidth, 0), Qt::AlignLeft | Qt::AlignVCenter,
        p.fontMetrics().elidedText(m_path.isEmpty() ? tr("No sample on the selected pad") : QFileInfo(m_path).fileName(),
            Qt::ElideMiddle, std::max(0, header.width() - timeWidth)));
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

    if (m_playhead >= 0.0f) {
        QColor head = m_playing ? pal.playheadText : pal.playheadDelay;
        p.setPen(QPen(head, 2.0));
        p.drawLine(headX, area.top(), headX, area.bottom());
    }
}
