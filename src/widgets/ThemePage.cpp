#include "ThemePage.h"

#include "SettingsCards.h"
#include "Theme.h"

#include <QAbstractButton>
#include <QColorDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPolygonF>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
// Preview of a theme preset: a tiny window with pads and a scene list in that preset's
// colours, named underneath. Checked when it is the current preset.
class ThemeTile : public QAbstractButton
{
public:
    ThemeTile(Theme::Id id, QWidget* parent)
        : QAbstractButton(parent)
        , m_id(id)
    {
        setCheckable(true);
        setFixedSize(132, 120);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        setText(Theme::idLabel(id));
        setToolTip(tr("Use the %1 theme").arg(Theme::idLabel(id)));
    }

    Theme::Id themeId() const { return m_id; }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const Theme::Palette& ui = Theme::instance().palette();
        const Theme::Palette t = Theme::instance().themePalette(m_id); // with your changes
        const QRectF tile = QRectF(rect()).adjusted(2, 2, -2, -2);

        p.setPen(QPen(isChecked() ? ui.focusBorder : (underMouse() ? ui.textMuted : ui.panelBorder),
            isChecked() ? 2.0 : 1.0));
        p.setBrush(ui.fieldBackground);
        p.drawRoundedRect(tile, 12, 12);

        // Mini window in the preset's own colours.
        const QRectF win(tile.left() + 8, tile.top() + 8, tile.width() - 16, 76);
        p.setPen(QPen(t.panelBorder, 1.0));
        p.setBrush(t.background);
        p.drawRoundedRect(win, 7, 7);

        const QRectF side(win.right() - 35, win.top() + 1, 34, win.height() - 2);
        p.setPen(Qt::NoPen);
        p.setBrush(t.panelBackground);
        p.drawRoundedRect(side, 6, 6);
        for (int i = 0; i < 3; ++i) {
            const QRectF sceneRow(side.left() + 4, side.top() + 7 + i * 14, side.width() - 8, 9);
            p.setBrush(i == 0 ? t.sceneActiveFill : t.sceneFill);
            p.setPen(QPen(i == 0 ? t.sceneActiveBorder : t.sceneBorder, 1.0));
            p.drawRoundedRect(sceneRow, 3, 3);
        }

        const qreal padW = 24;
        const qreal padH = 28;
        for (int i = 0; i < 4; ++i) {
            const int col = i % 2;
            const int r = i / 2;
            const QRectF pad(win.left() + 7 + col * (padW + 6), win.top() + 7 + r * (padH + 6), padW, padH);
            const bool live = i == 0;
            p.setBrush(t.padFill);
            p.setPen(QPen(live ? t.playLoaded : t.padBorder, live ? 1.4 : 1.0));
            p.drawRoundedRect(pad, 4, 4);
            p.setPen(Qt::NoPen);
            p.setBrush(live ? t.playLoaded : t.playEmpty);
            p.drawEllipse(QPointF(pad.center().x(), pad.top() + 11), 5.0, 5.0);
            p.setBrush(live ? t.playhead : t.playheadBorder);
            p.drawRoundedRect(QRectF(pad.left() + 4, pad.bottom() - 7, live ? padW * 0.55 : padW - 8, 2.5), 1, 1);
        }
        // Accent chip, so presets with similar grounds are easy to tell apart.
        p.setBrush(t.playLoaded);
        p.drawEllipse(QPointF(win.left() + 68, win.top() + 13), 3.5, 3.5);

        QFont f = font();
        f.setWeight(isChecked() ? QFont::DemiBold : QFont::Medium);
        p.setFont(f);
        p.setPen(ui.text);
        const QRectF label(tile.left() + 12, win.bottom() + 6, tile.width() - 24, tile.bottom() - win.bottom() - 10);
        p.drawText(label, Qt::AlignLeft | Qt::AlignVCenter, text());
        if (isChecked()) {
            p.setBrush(ui.focusBorder);
            p.setPen(Qt::NoPen);
            const QPointF c(label.right() - 6, label.center().y());
            p.drawEllipse(c, 7, 7);
            p.setPen(QPen(Theme::contrastOn(ui.focusBorder), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            QPolygonF tick;
            tick << QPointF(c.x() - 3, c.y()) << QPointF(c.x() - 0.8, c.y() + 2.3) << QPointF(c.x() + 3.2, c.y() - 2.2);
            p.drawPolyline(tick);
        }
    }

private:
    Theme::Id m_id;
};

// Round colour swatch for the accent picker. With no colour it draws a dashed "custom" circle.
class AccentSwatch : public QAbstractButton
{
public:
    AccentSwatch(const QColor& color, const QString& name, QWidget* parent)
        : QAbstractButton(parent)
        , m_color(color)
    {
        setCheckable(true);
        setFixedSize(34, 34);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        setToolTip(name);
        setAccessibleName(name);
    }

    QColor color() const { return m_color; }
    void setColor(const QColor& color)
    {
        m_color = color;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const Theme::Palette& ui = Theme::instance().palette();
        const QRectF outer = QRectF(rect()).adjusted(1, 1, -1, -1);
        const QRectF inner = outer.adjusted(4, 4, -4, -4);
        if (isChecked()) {
            p.setPen(QPen(ui.focusBorder, 2.0));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(outer);
        }
        if (!m_color.isValid() && isChecked()) {
            // Custom accent in use: show it, with a small plus to say "pick another".
            p.setPen(Qt::NoPen);
            p.setBrush(Theme::instance().accent());
            p.drawEllipse(inner);
            p.setPen(QPen(Theme::contrastOn(Theme::instance().accent()), 1.6, Qt::SolidLine, Qt::RoundCap));
            const QPointF c = inner.center();
            p.drawLine(QPointF(c.x() - 4, c.y()), QPointF(c.x() + 4, c.y()));
            p.drawLine(QPointF(c.x(), c.y() - 4), QPointF(c.x(), c.y() + 4));
            return;
        }
        if (!m_color.isValid()) {
            QPen dashed(underMouse() ? ui.text : ui.textMuted, 1.2);
            dashed.setDashPattern({2.5, 2.0});
            p.setPen(dashed);
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(inner);
            p.setPen(QPen(underMouse() ? ui.text : ui.textMuted, 1.6, Qt::SolidLine, Qt::RoundCap));
            const QPointF c = inner.center();
            p.drawLine(QPointF(c.x() - 4, c.y()), QPointF(c.x() + 4, c.y()));
            p.drawLine(QPointF(c.x(), c.y() - 4), QPointF(c.x(), c.y() + 4));
            return;
        }
        p.setPen(underMouse() ? QPen(ui.text, 1.0) : Qt::NoPen);
        p.setBrush(m_color);
        p.drawEllipse(inner);
    }

private:
    QColor m_color;
};

struct AccentChoice {
    const char* name;
    const char* hex;
};

constexpr AccentChoice kAccents[] = {
    {"Ember", "#ff7a3d"},
    {"Amber", "#f2c14e"},
    {"Teal", "#4fb6a8"},
    {"Sky", "#5aa9f0"},
    {"Violet", "#a78bfa"},
    {"Rose", "#ec6a8f"},
};
}

ThemePage::ThemePage(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("ThemePage"));
    setAttribute(Qt::WA_StyledBackground, true);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(32, 24, 20, 0);
    outer->setSpacing(20);
    QPushButton* back = nullptr;
    outer->addWidget(makePageHeader(this, tr("Theme"),
        tr("Pick a look, then an accent colour for everything that is playing."), &back));
    connect(back, &QPushButton::clicked, this, &ThemePage::backRequested);
    QVBoxLayout* cards = makeCardColumn(this, outer);
    QWidget* host = cards->parentWidget();

    // Presets
    SettingsCard presets = makeCard(host, tr("Presets"));
    auto* tiles = new QWidget(presets.frame);
    auto* tileGrid = new QGridLayout(tiles);
    tileGrid->setContentsMargins(0, 0, 0, 0);
    tileGrid->setHorizontalSpacing(12);
    tileGrid->setVerticalSpacing(12);
    for (int i = 0; i < Theme::kPresetCount; ++i) {
        auto* tile = new ThemeTile(static_cast<Theme::Id>(i), tiles);
        tileGrid->addWidget(tile, 0, i);
        m_themeTiles.append(tile);
        connect(tile, &QAbstractButton::clicked, this, [this, i]() {
            Theme::instance().setTheme(static_cast<Theme::Id>(i));
        });
    }
    tileGrid->setColumnStretch(Theme::kPresetCount, 1);
    presets.addFullRow(tiles);
    cards->addWidget(presets.frame);

    // Accent
    SettingsCard accent = makeCard(host, tr("Accent colour"),
        tr("Play buttons, loops, the played part of waveforms and progress bars."));
    auto* swatches = new QWidget(accent.frame);
    auto* swatchRow = new QHBoxLayout(swatches);
    swatchRow->setContentsMargins(0, 0, 0, 0);
    swatchRow->setSpacing(6);
    for (const AccentChoice& choice : kAccents) {
        auto* swatch = new AccentSwatch(QColor(QString::fromLatin1(choice.hex)), tr(choice.name), swatches);
        swatchRow->addWidget(swatch);
        m_accentSwatches.append(swatch);
        connect(swatch, &QAbstractButton::clicked, this, [swatch]() {
            Theme::instance().setAccent(swatch->color());
        });
    }
    auto* custom = new AccentSwatch(QColor(), tr("Custom colour..."), swatches);
    swatchRow->addWidget(custom);
    m_accentSwatches.append(custom);
    connect(custom, &QAbstractButton::clicked, this, [this]() {
        const QColor picked = QColorDialog::getColor(Theme::instance().accent(), this, tr("Accent colour"));
        if (picked.isValid()) {
            Theme::instance().setAccent(picked);
        } else {
            refresh(); // restore the checked state the click toggled
        }
    });
    swatchRow->addStretch();
    accent.addFullRow(swatches);
    cards->addWidget(accent.frame);

    // Advanced: every colour role, folded away by default.
    SettingsCard advanced = makeCard(host, tr("All colours"),
        tr("Fine-tune any single colour. Changes are saved with your settings."));
    m_advancedToggle = new QPushButton(advanced.frame);
    m_advancedToggle->setObjectName(QStringLiteral("DisclosureButton"));
    m_advancedToggle->setCursor(Qt::PointingHandCursor);
    m_advancedToggle->setCheckable(true);
    m_resetTheme = new QPushButton(tr("Reset to preset"), advanced.frame);
    m_resetTheme->setToolTip(tr("Undo accent and colour changes to this theme. Other themes keep theirs."));
    auto* advancedBar = new QWidget(advanced.frame);
    auto* advancedBarRow = new QHBoxLayout(advancedBar);
    advancedBarRow->setContentsMargins(0, 0, 0, 0);
    advancedBarRow->addWidget(m_advancedToggle);
    advancedBarRow->addStretch();
    advancedBarRow->addWidget(m_resetTheme);
    advanced.addFullRow(advancedBar);

    m_advancedColors = new QWidget(advanced.frame);
    auto* grid = new QGridLayout(m_advancedColors);
    grid->setContentsMargins(0, 4, 0, 0);
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(8);
    QString lastGroup;
    int row = 0;
    m_themeSwatches.resize(Theme::roleCount());
    for (int i = 0; i < Theme::roleCount(); ++i) {
        const QString group = Theme::roleGroup(i);
        if (group != lastGroup) {
            auto* section = new QLabel(group, m_advancedColors);
            section->setObjectName(QStringLiteral("ThemeSection"));
            grid->addWidget(section, row, 0, 1, 2, Qt::AlignLeft);
            ++row;
            lastGroup = group;
        }
        auto* caption = new QLabel(Theme::roleLabel(i), m_advancedColors);
        auto* swatch = new QPushButton(m_advancedColors);
        swatch->setFixedSize(44, 22);
        swatch->setCursor(Qt::PointingHandCursor);
        swatch->setFocusPolicy(Qt::NoFocus);
        swatch->setToolTip(tr("Change %1").arg(Theme::roleLabel(i)));
        auto* hex = new QLabel(m_advancedColors);
        hex->setObjectName(QStringLiteral("SwatchHex"));
        hex->setMinimumWidth(72);
        auto* field = new QWidget(m_advancedColors);
        auto* fieldLayout = new QHBoxLayout(field);
        fieldLayout->setContentsMargins(0, 0, 0, 0);
        fieldLayout->setSpacing(10);
        fieldLayout->addWidget(swatch);
        fieldLayout->addWidget(hex);
        fieldLayout->addStretch();
        grid->addWidget(caption, row, 0, Qt::AlignLeft | Qt::AlignVCenter);
        grid->addWidget(field, row, 1);
        m_themeSwatches[i] = {swatch, hex};
        connect(swatch, &QPushButton::clicked, this, [this, i]() {
            const QColor picked = QColorDialog::getColor(Theme::instance().colorAt(i), this, Theme::roleLabel(i));
            if (picked.isValid()) {
                Theme::instance().setColorAt(i, picked);
            }
        });
        ++row;
    }
    grid->setColumnMinimumWidth(0, 150);
    grid->setColumnStretch(1, 1);
    m_advancedColors->setVisible(false);
    advanced.addFullRow(m_advancedColors);
    cards->addWidget(advanced.frame);
    cards->addStretch(1);

    auto updateToggle = [this](bool open) {
        m_advancedToggle->setText(open ? tr("▾  Hide all colours") : tr("▸  Show all colours"));
        m_advancedColors->setVisible(open);
    };
    updateToggle(false);
    connect(m_advancedToggle, &QPushButton::toggled, this, updateToggle);
    connect(m_resetTheme, &QPushButton::clicked, this, []() {
        Theme::instance().resetToPreset();
    });
    connect(&Theme::instance(), &Theme::changed, this, &ThemePage::refresh);
}

void ThemePage::refresh()
{
    const Theme& theme = Theme::instance();
    for (QAbstractButton* tile : m_themeTiles) {
        tile->setChecked(static_cast<ThemeTile*>(tile)->themeId() == theme.id());
        tile->update();
    }
    // A preset accent is checked when it matches; anything else lights the custom swatch.
    bool matched = false;
    for (int i = 0; i < m_accentSwatches.size(); ++i) {
        auto* swatch = static_cast<AccentSwatch*>(m_accentSwatches[i]);
        const bool isCustom = !swatch->color().isValid();
        const bool on = isCustom ? !matched : swatch->color() == theme.accent();
        matched = matched || on;
        swatch->setChecked(on);
        swatch->update();
    }
    if (m_resetTheme) {
        m_resetTheme->setEnabled(theme.isModified());
    }
    const QString border = theme.palette().fieldBorder.name(QColor::HexRgb);
    for (int i = 0; i < m_themeSwatches.size(); ++i) {
        const QString color = theme.colorAt(i).name(QColor::HexRgb);
        m_themeSwatches[i].button->setStyleSheet(
            QStringLiteral("QPushButton { background: %1; border: 1px solid %2; border-radius: 6px; padding: 0; }")
                .arg(color, border));
        m_themeSwatches[i].hex->setText(color);
    }
}

