#include "FileBrowserWidget.h"

#include "AppConfig.h"
#include "AudioFormats.h"
#include "OpenALSoundPlayer.h"
#include "PeakStore.h"
#include "SoundPadWidget.h"
#include "Theme.h"
#include "VolumeDb.h"

#include <QAbstractFileIconProvider>
#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDrag>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QSlider>
#include <QSortFilterProxyModel>
#include <QStandardPaths>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QThreadPool>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace {

constexpr int kRowHeight = 26;      // matches the Editor's sample rows
constexpr int kPlayButton = 22;     // the round play button on a row
constexpr int kRowRightInset = 4;
constexpr float kDefaultPreviewDb = -6.0f;

QColor withAlpha(QColor color, qreal alpha)
{
    color.setAlphaF(static_cast<float>(std::clamp(alpha, 0.0, 1.0)));
    return color;
}

QString formatTime(double seconds)
{
    if (!(seconds >= 0.0) || !std::isfinite(seconds)) {
        seconds = 0.0;
    }
    const int total = static_cast<int>(seconds);
    return QStringLiteral("%1:%2").arg(total / 60).arg(total % 60, 2, 10, QLatin1Char('0'));
}

// Home shown as "~", with native separators.
QString displayPath(const QString& path)
{
    const QString home = QDir::homePath();
    QString shown = path;
    if (shown == home) {
        shown = QStringLiteral("~");
    } else if (shown.startsWith(home + QLatin1Char('/'))) {
        shown = QStringLiteral("~") + shown.mid(home.size());
    }
    return QDir::toNativeSeparators(shown);
}

// Folder outline, in a 14x14 box.
void drawFolderGlyph(QPainter& p, const QRectF& r, const QColor& color)
{
    QPainterPath path;
    const qreal x = r.left();
    const qreal y = r.top() + 1.5;
    const qreal w = r.width();
    const qreal h = r.height() - 3.0;
    path.moveTo(x + 1, y + 2);
    path.lineTo(x + 1, y + h - 1);
    path.quadTo(x + 1, y + h, x + 2, y + h);
    path.lineTo(x + w - 2, y + h);
    path.quadTo(x + w - 1, y + h, x + w - 1, y + h - 1);
    path.lineTo(x + w - 1, y + 3.5);
    path.quadTo(x + w - 1, y + 2.5, x + w - 2, y + 2.5);
    path.lineTo(x + w * 0.46, y + 2.5);
    path.lineTo(x + w * 0.36, y + 0.5);
    path.lineTo(x + 2, y + 0.5);
    path.quadTo(x + 1, y + 0.5, x + 1, y + 2);
    p.setPen(QPen(color, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
}

// Four waveform bars, in a 14x14 box.
void drawAudioGlyph(QPainter& p, const QRectF& r, const QColor& color)
{
    static const qreal heights[4] = {0.45, 1.0, 0.7, 0.35};
    const qreal barW = 2.0;
    const qreal gap = (r.width() - 4 * barW) / 3.0;
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    for (int i = 0; i < 4; ++i) {
        const qreal h = r.height() * heights[i];
        const QRectF bar(r.left() + i * (barW + gap), r.center().y() - h / 2.0, barW, h);
        p.drawRoundedRect(bar, 1.0, 1.0);
    }
}

void drawPlayTriangle(QPainter& p, const QPointF& c, qreal size, const QColor& color)
{
    QPolygonF tri;
    tri << QPointF(c.x() - size * 0.38, c.y() - size * 0.5)
        << QPointF(c.x() - size * 0.38, c.y() + size * 0.5)
        << QPointF(c.x() + size * 0.52, c.y());
    p.setPen(QPen(color, 1.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(color);
    p.drawPolygon(tri);
}

void drawStopSquare(QPainter& p, const QPointF& c, qreal size, const QColor& color)
{
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawRoundedRect(QRectF(c.x() - size / 2, c.y() - size / 2, size, size), 1.5, 1.5);
}

// No per-file icons: rows draw their own, and skipping the icon lookup keeps big folders fast.
class NoFileIcons : public QAbstractFileIconProvider
{
public:
    QIcon icon(IconType) const override { return {}; }
    QIcon icon(const QFileInfo&) const override { return {}; }
};

// Shows folders and playable audio only. Inside the browsed folder, the search text keeps
// just the files whose names contain it (and hides folders); outside it everything is kept
// so the chain of parent folders down to the browsed one stays in the model.
class AudioFileFilter : public QSortFilterProxyModel
{
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    void setRoot(const QString& root)
    {
        m_rootPrefix = root.endsWith(QLatin1Char('/')) ? root : root + QLatin1Char('/');
        invalidateFilter();
    }

    void setSearch(const QString& search)
    {
        if (search == m_search) {
            return;
        }
        m_search = search;
        invalidateFilter();
    }

protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override
    {
        const auto* fs = static_cast<const QFileSystemModel*>(sourceModel());
        const QModelIndex index = fs->index(row, 0, parent);
        const QString path = fs->filePath(index);
        const bool inside = path.startsWith(m_rootPrefix);
        if (fs->isDir(index)) {
            return !inside || m_search.isEmpty();
        }
        const QString name = fs->fileName(index);
        if (!AudioFormats::isPlayable(name)) {
            return false;
        }
        return m_search.isEmpty() || name.contains(m_search, Qt::CaseInsensitive);
    }

private:
    QString m_rootPrefix;
    QString m_search;
};

// Paints a row: folder or audio glyph, the name (elided in the middle so the end and the
// extension stay visible), and on the right either the file type or, under the mouse or
// while previewing, a round play/stop button.
class FileRowDelegate : public QStyledItemDelegate
{
public:
    FileRowDelegate(FileBrowserWidget* browser, QObject* parent)
        : QStyledItemDelegate(parent)
        , m_browser(browser)
    {
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const override
    {
        return QSize(option.rect.width(), kRowHeight);
    }

    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const Theme::Palette& theme = Theme::instance().palette();
        const QColor accent = Theme::instance().accent();
        const bool dir = m_browser->isDir(index);
        const QString path = m_browser->filePath(index);
        const bool previewing = !dir && m_browser->isPreviewing() && path == m_browser->previewPath();
        const bool hover = option.state & QStyle::State_MouseOver;
        const bool selected = option.state & QStyle::State_Selected;

        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        const QRectF r = QRectF(option.rect).adjusted(0, 1, -kRowRightInset, -1);
        if (selected) {
            p->setPen(Qt::NoPen);
            p->setBrush(withAlpha(theme.selection, 0.30));
            p->drawRoundedRect(r, 6, 6);
        } else if (previewing) {
            p->setPen(Qt::NoPen);
            p->setBrush(withAlpha(accent, 0.14));
            p->drawRoundedRect(r, 6, 6);
        } else if (hover) {
            p->setPen(Qt::NoPen);
            p->setBrush(theme.fieldBackground);
            p->drawRoundedRect(r, 6, 6);
        }

        const QRectF glyph(r.left() + 4, r.center().y() - 7, 14, 14);
        if (dir) {
            drawFolderGlyph(*p, glyph, theme.textMuted);
        } else {
            drawAudioGlyph(*p, glyph.adjusted(1, 1, -1, -1), previewing ? accent : theme.textMuted);
        }

        const bool showButton = !dir && (hover || previewing);
        const qreal rightZone = dir ? 6 : (kPlayButton + 6);
        QRectF textRect(glyph.right() + 8, r.top(), r.right() - rightZone - glyph.right() - 8, r.height());

        QFont font = option.font;
        font.setPixelSize(12);
        if (previewing) {
            font.setWeight(QFont::DemiBold);
        }
        p->setFont(font);
        p->setPen(theme.text); // accent text is too faint on the light themes; the glyph and tint carry it
        const QString name = index.data(Qt::DisplayRole).toString();
        p->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
            QFontMetrics(font).elidedText(name, Qt::ElideMiddle, static_cast<int>(textRect.width())));

        if (!dir) {
            const QRectF button(r.right() - kPlayButton, r.center().y() - kPlayButton / 2.0, kPlayButton, kPlayButton);
            if (showButton) {
                p->setPen(Qt::NoPen);
                p->setBrush(previewing ? accent : theme.accentButton);
                p->drawEllipse(button.adjusted(1, 1, -1, -1));
                const QColor mark = Theme::contrastOn(previewing ? accent : theme.accentButton);
                if (previewing) {
                    drawStopSquare(*p, button.center(), 7, mark);
                } else {
                    drawPlayTriangle(*p, button.center() + QPointF(0.6, 0), 8, mark);
                }
            } else {
                QFont small = option.font;
                small.setPixelSize(10);
                small.setLetterSpacing(QFont::PercentageSpacing, 104);
                p->setFont(small);
                p->setPen(theme.textMuted);
                p->drawText(button.adjusted(-10, 0, 0, 0), Qt::AlignVCenter | Qt::AlignRight,
                    QFileInfo(name).suffix().toUpper().left(4));
            }
        }
        p->restore();
    }

private:
    FileBrowserWidget* m_browser;
};

// The drag image: a pill with the first file's name, and "+N" for the rest.
QPixmap fileDragGhost(const QStringList& paths, qreal dpr)
{
    const Theme::Palette& theme = Theme::instance().palette();
    const QColor accent = Theme::instance().accent();
    QFont font = QApplication::font();
    font.setPixelSize(12);
    font.setWeight(QFont::Medium);
    const QFontMetrics fm(font);
    const bool firstIsDir = QFileInfo(paths.front()).isDir();
    const QString name = fm.elidedText(QFileInfo(paths.front()).fileName(), Qt::ElideMiddle, 220);
    const QString more = paths.size() > 1 ? QStringLiteral("+%1").arg(paths.size() - 1) : QString();

    QFont badgeFont = font;
    badgeFont.setPixelSize(11);
    badgeFont.setWeight(QFont::DemiBold);
    const QFontMetrics bfm(badgeFont);
    const int badgeW = more.isEmpty() ? 0 : bfm.horizontalAdvance(more) + 12;

    const int h = 30;
    const int w = 10 + 14 + 8 + fm.horizontalAdvance(name) + (badgeW ? 8 + badgeW : 0) + 12;
    QPixmap pix(QSize(w, h) * dpr);
    pix.setDevicePixelRatio(dpr);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setOpacity(0.94);
    const QRectF pill(0.5, 0.5, w - 1, h - 1);
    p.setPen(QPen(theme.fieldBorder, 1));
    p.setBrush(theme.panelBackground);
    p.drawRoundedRect(pill, h / 2.0, h / 2.0);
    const QRectF glyph(10, h / 2.0 - 7, 14, 14);
    if (firstIsDir) {
        drawFolderGlyph(p, glyph, accent);
    } else {
        drawAudioGlyph(p, glyph.adjusted(1, 1, -1, -1), accent);
    }
    p.setFont(font);
    p.setPen(theme.text);
    const QRectF text(glyph.right() + 8, 0, fm.horizontalAdvance(name) + 2, h);
    p.drawText(text, Qt::AlignVCenter | Qt::AlignLeft, name);
    if (badgeW) {
        const QRectF badge(text.right() + 6, h / 2.0 - 9, badgeW, 18);
        p.setPen(Qt::NoPen);
        p.setBrush(accent);
        p.drawRoundedRect(badge, 9, 9);
        p.setFont(badgeFont);
        p.setPen(Theme::contrastOn(accent));
        p.drawText(badge, Qt::AlignCenter, more);
    }
    return pix;
}

} // namespace

// ---------------------------------------------------------------------------------------
// FileIconButton

FileIconButton::FileIconButton(Kind kind, QWidget* parent)
    : QAbstractButton(parent)
    , m_kind(kind)
{
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_Hover, true);
    setFixedSize(26, 26);
}

void FileIconButton::setKind(Kind kind)
{
    if (kind != m_kind) {
        m_kind = kind;
        update();
    }
}

void FileIconButton::paintEvent(QPaintEvent*)
{
    const Theme::Palette& theme = Theme::instance().palette();
    const QColor accent = Theme::instance().accent();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
    const bool on = isEnabled();
    const bool hover = on && underMouse();

    if (m_kind == Kind::Play || m_kind == Kind::Stop) {
        const QColor fill = !on ? theme.fieldBackground : (m_kind == Kind::Stop ? accent : theme.accentButton);
        p.setPen(Qt::NoPen);
        p.setBrush(hover ? fill.lighter(112) : fill);
        p.drawEllipse(r);
        const QColor mark = on ? Theme::contrastOn(fill) : theme.textMuted;
        if (m_kind == Kind::Stop) {
            drawStopSquare(p, r.center(), 8, mark);
        } else {
            drawPlayTriangle(p, r.center() + QPointF(0.7, 0), 9, mark);
        }
        return;
    }

    if (hover) {
        p.setPen(Qt::NoPen);
        p.setBrush(theme.fieldBackground);
        p.drawRoundedRect(r, 6, 6);
    }
    const QColor ink = !on ? withAlpha(theme.textMuted, 0.45) : (hover ? theme.text : theme.textMuted);
    p.setPen(QPen(ink, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    const QPointF c = r.center();
    switch (m_kind) {
    case Kind::Up: {
        // Arrow up and to the left, as out of a folder.
        p.drawLine(QPointF(c.x(), c.y() + 5), QPointF(c.x(), c.y() - 5));
        p.drawLine(QPointF(c.x() - 4, c.y() - 1), QPointF(c.x(), c.y() - 5));
        p.drawLine(QPointF(c.x() + 4, c.y() - 1), QPointF(c.x(), c.y() - 5));
        break;
    }
    case Kind::GoTo: {
        drawFolderGlyph(p, QRectF(c.x() - 8, c.y() - 7, 13, 14), ink);
        QPolygonF caret;
        caret << QPointF(c.x() + 6, c.y() - 1) << QPointF(c.x() + 10, c.y() - 1) << QPointF(c.x() + 8, c.y() + 1.8);
        p.setPen(Qt::NoPen);
        p.setBrush(ink);
        p.drawPolygon(caret);
        break;
    }
    case Kind::Volume: {
        QPolygonF speaker;
        speaker << QPointF(c.x() - 6, c.y() - 2) << QPointF(c.x() - 3.5, c.y() - 2) << QPointF(c.x(), c.y() - 5)
                << QPointF(c.x(), c.y() + 5) << QPointF(c.x() - 3.5, c.y() + 2) << QPointF(c.x() - 6, c.y() + 2);
        p.setBrush(ink);
        p.setPen(QPen(ink, 1.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPolygon(speaker);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(ink, 1.3, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(QRectF(c.x() - 3, c.y() - 3.5, 7, 7), -60 * 16, 120 * 16);
        p.drawArc(QRectF(c.x() - 4.5, c.y() - 6.5, 12, 13), -60 * 16, 120 * 16);
        break;
    }
    default:
        break;
    }
}

// ---------------------------------------------------------------------------------------
// FileTreeView

FileTreeView::FileTreeView(FileBrowserWidget* browser, QWidget* parent)
    : QTreeView(parent)
    , m_browser(browser)
{
}

QRect FileTreeView::playButtonRect(const QModelIndex& index) const
{
    const QRect row = visualRect(index);
    const int right = row.right() - kRowRightInset;
    return QRect(right - kPlayButton + 1, row.center().y() - kPlayButton / 2, kPlayButton, kPlayButton);
}

void FileTreeView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        const QModelIndex index = indexAt(event->position().toPoint());
        if (index.isValid() && !m_browser->isDir(index)
            && playButtonRect(index).contains(event->position().toPoint())) {
            emit playClicked(index);
            event->accept();
            return;
        }
    }
    QTreeView::mousePressEvent(event);
}

void FileTreeView::mouseDoubleClickEvent(QMouseEvent* event)
{
    const QModelIndex index = indexAt(event->position().toPoint());
    if (event->button() == Qt::LeftButton && index.isValid()) {
        if (!m_browser->isDir(index) && playButtonRect(index).contains(event->position().toPoint())) {
            emit playClicked(index); // a fast second click on the button still toggles
        } else {
            emit activated2(index);
        }
        event->accept();
        return;
    }
    QTreeView::mouseDoubleClickEvent(event);
}

void FileTreeView::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Space:
        emit spacePressed();
        event->accept();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (currentIndex().isValid()) {
            emit activated2(currentIndex());
        }
        event->accept();
        return;
    case Qt::Key_Backspace:
        emit upRequested();
        event->accept();
        return;
    default:
        break;
    }
    QTreeView::keyPressEvent(event);
}

void FileTreeView::startDrag(Qt::DropActions)
{
    const QStringList paths = m_browser->selectedPaths();
    if (paths.isEmpty()) {
        return;
    }
    auto* mime = new QMimeData();
    QList<QUrl> urls;
    for (const QString& path : paths) {
        urls << QUrl::fromLocalFile(path);
    }
    mime->setUrls(urls);

    auto* drag = new QDrag(this);
    drag->setMimeData(mime);
    const qreal dpr = devicePixelRatioF();
    const QPixmap ghost = fileDragGhost(paths, dpr);
    drag->setPixmap(ghost);
    // The pill sits just right of the pointer's badge, centred on it vertically.
    drag->setHotSpot(QPoint(-40, qRound(ghost.height() / dpr / 2.0) - 27));
    // Copy = replace the pad's sounds (the default); Link = add to them (Shift).
    drag->setDragCursor(feedraDragCursor(DragBadge::Replace, dpr), Qt::CopyAction);
    drag->setDragCursor(feedraDragCursor(DragBadge::Plus, dpr), Qt::LinkAction);
    drag->exec(Qt::CopyAction | Qt::LinkAction, Qt::CopyAction);
}

// ---------------------------------------------------------------------------------------
// PreviewWave

PreviewWave::PreviewWave(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(34);
    setCursor(Qt::PointingHandCursor);
    connect(&PeakStore::instance(), &PeakStore::peaksReady, this, [this](const QString& key) {
        if (!m_path.isEmpty() && key == PeakStore::keyFor(m_path)) {
            update();
        }
    });
    connect(&PeakStore::instance(), &PeakStore::cacheCleared, this, [this]() { update(); });
}

void PreviewWave::setPath(const QString& path)
{
    if (path != m_path) {
        m_path = path;
        m_position = -1.0f;
        update();
    }
}

void PreviewWave::setPosition(float pct)
{
    if (std::abs(pct - m_position) > 0.0005f) {
        m_position = pct;
        update();
    }
}

void PreviewWave::paintEvent(QPaintEvent*)
{
    const Theme::Palette& theme = Theme::instance().palette();
    const QColor accent = Theme::instance().accent();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF r = QRectF(rect()).adjusted(0, 2, 0, -2);
    const qreal mid = r.center().y();

    std::shared_ptr<const PeakData> peaks;
    if (!m_path.isEmpty()) {
        peaks = PeakStore::instance().get(m_path, true);
    }
    if (!peaks || !peaks->ok || peaks->maxs.empty()) {
        // Nothing to show yet (or no file): a row of dots, like a pad waiting on its delay.
        p.setPen(Qt::NoPen);
        p.setBrush(withAlpha(theme.textMuted, 0.5));
        for (qreal x = r.left() + 2; x < r.right(); x += 6) {
            p.drawEllipse(QPointF(x, mid), 1.1, 1.1);
        }
        return;
    }

    const int bins = static_cast<int>(peaks->maxs.size());
    float loudest = 0.0f;
    for (int i = 0; i < bins; ++i) {
        loudest = std::max({loudest, std::abs(peaks->mins[i]), std::abs(peaks->maxs[i])});
    }
    const float scale = loudest > 1.0e-4f ? 1.0f / loudest : 1.0f;
    const int columns = std::max(1, static_cast<int>(r.width() / 3.0));
    const qreal step = r.width() / columns;
    const qreal played = m_position >= 0.0f ? r.left() + r.width() * m_position : r.left() - 1;
    p.setPen(Qt::NoPen);
    for (int c = 0; c < columns; ++c) {
        const int from = c * bins / columns;
        const int to = std::max(from + 1, (c + 1) * bins / columns);
        float hi = 0.0f;
        for (int i = from; i < to && i < bins; ++i) {
            hi = std::max({hi, std::abs(peaks->mins[i]), std::abs(peaks->maxs[i])});
        }
        const qreal h = std::max<qreal>(1.5, hi * scale * (r.height() / 2.0));
        const qreal x = r.left() + c * step;
        p.setBrush(x + step / 2 <= played ? accent : withAlpha(theme.textMuted, 0.65));
        p.drawRoundedRect(QRectF(x, mid - h, std::max<qreal>(1.0, step - 1.0), h * 2), 0.8, 0.8);
    }
    if (m_position >= 0.0f) {
        p.setPen(QPen(theme.text, 1.2));
        p.drawLine(QPointF(played, r.top()), QPointF(played, r.bottom()));
    }
}

void PreviewWave::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && !m_path.isEmpty() && width() > 0) {
        emit seekRequested(std::clamp(static_cast<float>(event->position().x() / width()), 0.0f, 1.0f));
    }
}

void PreviewWave::mouseMoveEvent(QMouseEvent* event)
{
    if ((event->buttons() & Qt::LeftButton) && !m_path.isEmpty() && width() > 0) {
        emit seekRequested(std::clamp(static_cast<float>(event->position().x() / width()), 0.0f, 1.0f));
    }
}

// ---------------------------------------------------------------------------------------
// FileBrowserWidget

FileBrowserWidget::FileBrowserWidget(AppConfig* config, QWidget* parent)
    : QWidget(parent)
    , m_config(config)
{
    setObjectName(QStringLiteral("FileBrowser"));
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(12, 12, 4, 12); // same as the Scenes and Editor pages
    outer->setSpacing(8);

    // Folder bar: up, where we are, and a "Go to" menu.
    auto* bar = new QHBoxLayout();
    bar->setContentsMargins(0, 0, 8, 0);
    bar->setSpacing(4);
    m_upButton = new FileIconButton(FileIconButton::Kind::Up, this);
    m_upButton->setToolTip(tr("Up one folder (Backspace)"));
    m_pathLabel = new QLabel(this);
    m_pathLabel->setObjectName(QStringLiteral("FilePath"));
    m_pathLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_pathLabel->installEventFilter(this);
    m_goToButton = new FileIconButton(FileIconButton::Kind::GoTo, this);
    m_goToButton->setToolTip(tr("Go to a folder"));
    bar->addWidget(m_upButton);
    bar->addWidget(m_pathLabel, 1);
    bar->addWidget(m_goToButton);
    outer->addLayout(bar);

    auto* searchRow = new QHBoxLayout();
    searchRow->setContentsMargins(0, 0, 8, 0);
    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("FileSearch"));
    m_search->setPlaceholderText(tr("Filter this folder"));
    m_search->setClearButtonEnabled(true);
    searchRow->addWidget(m_search);
    outer->addLayout(searchRow);

    // The tree.
    m_model = new QFileSystemModel(this);
    m_model->setIconProvider(new NoFileIcons());
    m_model->setReadOnly(true);
    m_model->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Drives);
    m_model->setOption(QFileSystemModel::DontUseCustomDirectoryIcons, true);
    auto* filter = new AudioFileFilter(this);
    filter->setSourceModel(m_model);
    filter->setDynamicSortFilter(true);
    m_filter = filter;

    m_view = new FileTreeView(this, this);
    m_view->setObjectName(QStringLiteral("FileTree"));
    m_view->setModel(m_filter);
    m_view->setItemDelegate(new FileRowDelegate(this, m_view));
    m_view->setHeaderHidden(true);
    for (int c = 1; c < m_model->columnCount(); ++c) {
        m_view->hideColumn(c);
    }
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setUniformRowHeights(true);
    m_view->setIndentation(14);
    m_view->setAnimated(false);
    m_view->setExpandsOnDoubleClick(false);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setDragEnabled(true);
    m_view->setDragDropMode(QAbstractItemView::DragOnly);
    m_view->setDefaultDropAction(Qt::CopyAction);
    m_view->setMouseTracking(true);
    m_view->viewport()->setAttribute(Qt::WA_Hover, true);
    m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_view->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_view->viewport()->setAutoFillBackground(false);
    auto* treeRow = new QHBoxLayout();
    treeRow->setContentsMargins(0, 0, 8, 0);
    treeRow->addWidget(m_view);
    outer->addLayout(treeRow, 1);

    // Preview strip.
    m_previewBox = new QWidget(this);
    m_previewBox->setObjectName(QStringLiteral("FilePreview"));
    m_previewBox->setAttribute(Qt::WA_StyledBackground, true);
    auto* box = new QVBoxLayout(m_previewBox);
    box->setContentsMargins(10, 8, 10, 8);
    box->setSpacing(6);
    auto* top = new QHBoxLayout();
    top->setSpacing(8);
    m_previewButton = new FileIconButton(FileIconButton::Kind::Play, m_previewBox);
    m_previewName = new QLabel(m_previewBox);
    m_previewName->setObjectName(QStringLiteral("PreviewName"));
    m_previewName->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_previewTime = new QLabel(m_previewBox);
    m_previewTime->setObjectName(QStringLiteral("PreviewTime"));
    top->addWidget(m_previewButton);
    top->addWidget(m_previewName, 1);
    top->addWidget(m_previewTime);
    box->addLayout(top);
    m_previewWave = new PreviewWave(m_previewBox);
    box->addWidget(m_previewWave);
    auto* bottom = new QHBoxLayout();
    bottom->setSpacing(4);
    m_autoPreview = new QCheckBox(tr("Auto-preview"), m_previewBox);
    m_autoPreview->setObjectName(QStringLiteral("AutoPreview"));
    m_autoPreview->setToolTip(tr("Play each file as it's selected, so you can arrow through a folder"));
    auto* speaker = new FileIconButton(FileIconButton::Kind::Volume, m_previewBox);
    speaker->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    speaker->setCursor(Qt::ArrowCursor);
    m_previewVolume = new QSlider(Qt::Horizontal, m_previewBox);
    m_previewVolume->setObjectName(QStringLiteral("PreviewVolume"));
    m_previewVolume->setRange(0, VolumeDb::sliderSpan(VolumeDb::kFloorDb, VolumeDb::kUnityDb));
    m_previewVolume->setValue(VolumeDb::toSlider(kDefaultPreviewDb, VolumeDb::kFloorDb));
    m_previewVolume->setFixedWidth(96);
    m_previewVolume->setFocusPolicy(Qt::NoFocus);
    bottom->addWidget(m_autoPreview);
    bottom->addStretch();
    bottom->addWidget(speaker);
    bottom->addWidget(m_previewVolume);
    box->addLayout(bottom);
    auto* previewRow = new QHBoxLayout();
    previewRow->setContentsMargins(0, 0, 8, 0);
    previewRow->addWidget(m_previewBox);
    outer->addLayout(previewRow);

    auto updateVolumeTip = [this](int v) {
        m_previewVolume->setToolTip(tr("Preview volume: %1").arg(
            VolumeDb::format(VolumeDb::fromSlider(v, VolumeDb::kFloorDb))));
    };
    updateVolumeTip(m_previewVolume->value());
    connect(m_previewVolume, &QSlider::valueChanged, this, [this, updateVolumeTip](int v) {
        updateVolumeTip(v);
        applyPreviewVolume();
    });

    connect(m_upButton, &QAbstractButton::clicked, this, &FileBrowserWidget::goUp);
    connect(m_goToButton, &QAbstractButton::clicked, this, &FileBrowserWidget::buildGoToMenu);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString& text) {
        static_cast<AudioFileFilter*>(m_filter)->setSearch(text.trimmed());
    });
    connect(m_view, &FileTreeView::playClicked, this, [this](const QModelIndex& index) {
        togglePreview(filePath(index));
    });
    connect(m_view, &FileTreeView::activated2, this, &FileBrowserWidget::openIndex);
    connect(m_view, &FileTreeView::upRequested, this, &FileBrowserWidget::goUp);
    connect(m_view, &FileTreeView::spacePressed, this, [this]() {
        const QModelIndex index = m_view->currentIndex();
        if (index.isValid() && !isDir(index)) {
            togglePreview(filePath(index));
        } else if (isPreviewing()) {
            stopPreview();
        }
    });
    connect(m_view->selectionModel(), &QItemSelectionModel::currentChanged, this,
        [this](const QModelIndex& current) { onCurrentChanged(current); });
    connect(m_previewButton, &QAbstractButton::clicked, this, [this]() {
        if (isPreviewing()) {
            stopPreview();
            return;
        }
        const QModelIndex index = m_view->currentIndex();
        if (index.isValid() && !isDir(index)) {
            previewFile(filePath(index));
        } else if (!m_previewPath.isEmpty()) {
            previewFile(m_previewPath); // try the last one again
        }
    });
    connect(m_previewWave, &PreviewWave::seekRequested, this, [this](float pct) {
        if (m_player && isPreviewing()) {
            m_player->seekTo(pct);
            m_previewWave->setPosition(pct);
        }
    });

    setCurrentDir(m_config->loadDialogDir());
    refreshPreviewUi();
}

FileBrowserWidget::~FileBrowserWidget()
{
    stopPreview();
}

bool FileBrowserWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_pathLabel && event->type() == QEvent::Resize) {
        refreshPathLabel();
    }
    return QWidget::eventFilter(watched, event);
}

QString FileBrowserWidget::filePath(const QModelIndex& viewIndex) const
{
    return m_model->filePath(m_filter->mapToSource(viewIndex));
}

bool FileBrowserWidget::isDir(const QModelIndex& viewIndex) const
{
    return m_model->isDir(m_filter->mapToSource(viewIndex));
}

QStringList FileBrowserWidget::selectedPaths() const
{
    QModelIndexList rows = m_view->selectionModel()->selectedRows(0);
    // In list order, whatever order they were clicked in.
    std::sort(rows.begin(), rows.end(), [this](const QModelIndex& a, const QModelIndex& b) {
        return m_view->visualRect(a).top() < m_view->visualRect(b).top();
    });
    QStringList out;
    for (const QModelIndex& index : rows) {
        out << filePath(index);
    }
    return out;
}

void FileBrowserWidget::setSearchText(const QString& text)
{
    m_search->setText(text);
}

void FileBrowserWidget::setCurrentDir(const QString& dir)
{
    QString clean = QDir::cleanPath(QFileInfo(dir).absoluteFilePath());
    if (dir.isEmpty() || !QFileInfo(clean).isDir()) {
        clean = QDir::homePath();
    }
    m_dir = clean;
    static_cast<AudioFileFilter*>(m_filter)->setRoot(clean);
    const QModelIndex source = m_model->setRootPath(clean);
    m_view->setRootIndex(m_filter->mapFromSource(source));
    m_view->selectionModel()->clearSelection();
    m_view->scrollToTop();
    m_upButton->setEnabled(!QDir(clean).isRoot());
    refreshPathLabel();
}

void FileBrowserWidget::goUp()
{
    QDir dir(m_dir);
    if (dir.isRoot() || !dir.cdUp()) {
        return;
    }
    const QString from = m_dir;
    setCurrentDir(dir.absolutePath());
    // Land on the folder we just left, so Backspace then Enter goes straight back.
    const QModelIndex index = m_filter->mapFromSource(m_model->index(from));
    if (index.isValid()) {
        m_view->setCurrentIndex(index);
        m_view->scrollTo(index);
    }
}

void FileBrowserWidget::openIndex(const QModelIndex& index)
{
    if (!index.isValid()) {
        return;
    }
    if (isDir(index)) {
        setCurrentDir(filePath(index));
    } else {
        togglePreview(filePath(index));
    }
}

void FileBrowserWidget::onCurrentChanged(const QModelIndex& current)
{
    if (!m_autoPreview->isChecked() || !current.isValid() || isDir(current)) {
        return;
    }
    const QString path = filePath(current);
    if (!(isPreviewing() && path == m_previewPath)) {
        previewFile(path);
    }
}

void FileBrowserWidget::refreshPathLabel()
{
    const QString shown = displayPath(m_dir);
    const int width = std::max(0, m_pathLabel->width());
    m_pathLabel->setText(m_pathLabel->fontMetrics().elidedText(shown, Qt::ElideLeft, width));
    m_pathLabel->setToolTip(QDir::toNativeSeparators(m_dir));
}

void FileBrowserWidget::buildGoToMenu()
{
    QMenu menu(this);
    menu.setToolTipsVisible(true);
    auto addFolder = [this](QMenu* into, const QString& label, const QString& dir) {
        if (dir.isEmpty() || !QFileInfo(dir).isDir()) {
            return;
        }
        QAction* act = into->addAction(label);
        act->setToolTip(QDir::toNativeSeparators(dir));
        connect(act, &QAction::triggered, this, [this, dir]() { setCurrentDir(dir); });
    };

    addFolder(&menu, tr("Sample library"), m_config->libraryLocation());
    const QString settingsFile = m_config->settingsPath.isEmpty() ? m_config->defaultSettingsPath() : m_config->settingsPath;
    addFolder(&menu, tr("Project files"), QFileInfo(settingsFile).absoluteDir().filePath(QStringLiteral("files")));

    if (m_projectFolders) {
        QStringList folders = m_projectFolders();
        folders.removeDuplicates();
        folders.erase(std::remove_if(folders.begin(), folders.end(),
                          [](const QString& f) { return !QFileInfo(f).isDir(); }),
            folders.end());
        std::sort(folders.begin(), folders.end(), [](const QString& a, const QString& b) {
            return a.compare(b, Qt::CaseInsensitive) < 0;
        });
        if (!folders.isEmpty()) {
            QMenu* used = menu.addMenu(tr("Folders these scenes use"));
            used->setToolTipsVisible(true);
            constexpr int kMaxFolders = 20;
            for (int i = 0; i < folders.size() && i < kMaxFolders; ++i) {
                addFolder(used, displayPath(folders[i]), folders[i]);
            }
        }
    }

    menu.addSeparator();
    addFolder(&menu, tr("Home"), QDir::homePath());
    addFolder(&menu, tr("Music"), QStandardPaths::writableLocation(QStandardPaths::MusicLocation));
    for (const QFileInfo& drive : QDir::drives()) {
        // Windows drive letters; on Linux and macOS this is just "/".
        if (QDir::drives().size() > 1) {
            addFolder(&menu, QDir::toNativeSeparators(drive.absoluteFilePath()), drive.absoluteFilePath());
        }
    }

    menu.addSeparator();
    QAction* choose = menu.addAction(tr("Choose folder..."));
    connect(choose, &QAction::triggered, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Browse folder"), m_dir);
        if (!dir.isEmpty()) {
            setCurrentDir(dir);
        }
    });
    QAction* useAsLibrary = menu.addAction(tr("Use this folder as the sample library"));
    useAsLibrary->setToolTip(tr("Load buttons on pads open here when a pad has no sound yet"));
    useAsLibrary->setEnabled(QDir::cleanPath(m_config->libraryLocation()) != m_dir);
    connect(useAsLibrary, &QAction::triggered, this, [this]() {
        m_config->defaultLibraryLocation = m_dir;
        emit libraryFolderChosen(m_dir);
    });

    menu.exec(m_goToButton->mapToGlobal(QPoint(m_goToButton->width() - menu.sizeHint().width(), m_goToButton->height() + 2)));
}

// --- Preview -------------------------------------------------------------------------

bool FileBrowserWidget::autoPreview() const
{
    return m_autoPreview->isChecked();
}

void FileBrowserWidget::setAutoPreview(bool on)
{
    m_autoPreview->setChecked(on);
}

void FileBrowserWidget::previewFile(const QString& path)
{
    stopPreview();
    if (path.isEmpty()) {
        return;
    }
    m_previewPath = path;
    m_previewFailed = false;
    m_sawPlaying = false;
    m_previewStartedMs = QDateTime::currentMSecsSinceEpoch();
    const int generation = ++m_previewGeneration;

    // Open the file off the UI thread (a stream only reads its first few buffers, but a slow
    // disk or a big mp3's header scan shouldn't stall the window), then start it here.
    QPointer<FileBrowserWidget> self(this);
    QThreadPool::globalInstance()->start([self, generation, path]() {
        auto decoded = std::make_shared<DecodedAudio>(
            OpenALSoundPlayer::decodeFile(std::filesystem::path(path.toStdString()), true));
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, generation, decoded]() {
            if (self && self->m_previewGeneration == generation) {
                self->startDecoded(decoded);
            }
        }, Qt::QueuedConnection);
    });
    refreshPreviewUi();
    m_view->viewport()->update();
}

void FileBrowserWidget::startDecoded(std::shared_ptr<DecodedAudio> decoded)
{
    if (!decoded || !decoded->ok) {
        m_previewFailed = true;
        refreshPreviewUi();
        m_view->viewport()->update();
        return;
    }
    m_player = std::make_unique<OpenALSoundPlayer>();
    if (!m_player->uploadDecoded(std::move(*decoded))) {
        m_player.reset();
        m_previewFailed = true;
        refreshPreviewUi();
        m_view->viewport()->update();
        return;
    }
    m_player->setLoop(false);
    applyPreviewVolume();
    m_player->play();
    m_previewStartedMs = QDateTime::currentMSecsSinceEpoch();
    refreshPreviewUi();
}

void FileBrowserWidget::togglePreview(const QString& path)
{
    if (isPreviewing() && path == m_previewPath) {
        stopPreview();
    } else {
        previewFile(path);
    }
}

void FileBrowserWidget::stopPreview()
{
    ++m_previewGeneration; // a file still opening is dropped when it arrives
    if (m_player) {
        m_player->stop();
        m_player.reset();
    }
    const bool had = !m_previewPath.isEmpty();
    m_previewPath.clear();
    m_previewFailed = false;
    m_sawPlaying = false;
    if (had && m_view) {
        refreshPreviewUi();
        m_view->viewport()->update();
    }
}

void FileBrowserWidget::applyPreviewVolume()
{
    if (!m_player) {
        return;
    }
    // Its own level, under the main fader. Scene fades don't touch it: it isn't part of a scene.
    const float own = VolumeDb::toLinearMuted(VolumeDb::fromSlider(m_previewVolume->value(), VolumeDb::kFloorDb));
    m_player->setVolume(own * m_config->masterVolume());
}

void FileBrowserWidget::tick()
{
    if (!m_player) {
        return;
    }
    applyPreviewVolume();
    if (m_player->isPlaying()) {
        m_sawPlaying = true;
    } else if (m_sawPlaying || QDateTime::currentMSecsSinceEpoch() - m_previewStartedMs > 3000) {
        stopPreview(); // reached the end (or never started)
        return;
    }
    float pos = m_player->getAudiblePosition();
    if (pos < 0.0f) {
        pos = m_player->getPosition();
    }
    pos = std::clamp(pos, 0.0f, 1.0f);
    m_previewWave->setPosition(pos);
    const double duration = m_player->getDuration();
    m_previewTime->setText(formatTime(pos * duration) + QStringLiteral(" / ") + formatTime(duration));
}

void FileBrowserWidget::refreshPreviewUi()
{
    const bool active = isPreviewing();
    m_previewButton->setKind(active ? FileIconButton::Kind::Stop : FileIconButton::Kind::Play);
    m_previewButton->setToolTip(active ? tr("Stop preview (Space)") : tr("Preview the selected file (Space)"));

    QString name;
    bool idle = false;
    if (m_previewPath.isEmpty()) {
        name = tr("Select a file and press Space to hear it");
        idle = true;
    } else if (m_previewFailed) {
        name = tr("Can't play %1").arg(QFileInfo(m_previewPath).fileName());
        idle = true;
    } else {
        name = QFileInfo(m_previewPath).fileName();
    }
    m_previewName->setProperty("idle", idle);
    m_previewName->style()->unpolish(m_previewName);
    m_previewName->style()->polish(m_previewName);
    m_previewName->setText(m_previewName->fontMetrics().elidedText(name, Qt::ElideMiddle, std::max(40, m_previewName->width())));
    m_previewName->setToolTip(m_previewPath.isEmpty() ? QString() : QDir::toNativeSeparators(m_previewPath));
    if (!active) {
        m_previewTime->clear();
    }
    m_previewWave->setPath(active ? m_previewPath : QString());
}

// --- Settings ------------------------------------------------------------------------

void FileBrowserWidget::saveSettings(QJsonObject& global) const
{
    global.insert(QStringLiteral("filesdir"), m_dir);
    global.insert(QStringLiteral("previewvolume"),
        static_cast<double>(VolumeDb::fromSlider(m_previewVolume->value(), VolumeDb::kFloorDb)));
    global.insert(QStringLiteral("autopreview"), m_autoPreview->isChecked());
}

void FileBrowserWidget::applySettings(const QJsonObject& global)
{
    // The folder this file was last browsing, or else where its load buttons would open.
    const QString dir = global.value(QStringLiteral("filesdir")).toString();
    setCurrentDir(!dir.isEmpty() && QFileInfo(dir).isDir() ? dir : m_config->loadDialogDir());
    if (global.contains(QStringLiteral("previewvolume"))) {
        const float db = std::clamp(static_cast<float>(global.value(QStringLiteral("previewvolume")).toDouble(kDefaultPreviewDb)),
            VolumeDb::kFloorDb, VolumeDb::kUnityDb);
        m_previewVolume->setValue(VolumeDb::toSlider(db, VolumeDb::kFloorDb));
    }
    if (global.contains(QStringLiteral("autopreview"))) {
        m_autoPreview->setChecked(global.value(QStringLiteral("autopreview")).toBool());
    }
}
