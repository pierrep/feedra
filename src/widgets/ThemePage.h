#pragma once

// The Theme page: preset tiles, the accent colour, and every colour role folded away.

#include <QVector>
#include <QWidget>

class QAbstractButton;
class QLabel;
class QPushButton;

class ThemePage : public QWidget
{
    Q_OBJECT
public:
    explicit ThemePage(QWidget* parent = nullptr);

    // Shows the current theme: checked tile and swatch, colour values. Also runs on Theme::changed.
    void refresh();

signals:
    void backRequested();

private:
    QVector<QAbstractButton*> m_themeTiles;
    QVector<QAbstractButton*> m_accentSwatches;
    QWidget* m_advancedColors = nullptr;
    QPushButton* m_advancedToggle = nullptr;
    QPushButton* m_resetTheme = nullptr;
    struct ThemeSwatch {
        QPushButton* button = nullptr;
        QLabel* hex = nullptr;
    };
    QVector<ThemeSwatch> m_themeSwatches;
};
