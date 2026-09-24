#pragma once

#include <QColor>
#include <QJsonObject>
#include <QObject>
#include <QString>

class Theme : public QObject
{
    Q_OBJECT
public:
    enum class Id { Parchment, Night, Forest, Ink };

    struct Palette {
        QColor background;
        QColor text;
        QColor menuBackground;
        QColor menuText;
        QColor panelBackground;
        QColor panelBorder;
        QColor tabBackground;
        QColor tabText;
        QColor tabBorder;
        QColor fieldBackground;
        QColor fieldBorder;
        QColor fieldText;
        QColor focusBorder;
        QColor selection;
        QColor selectionText;
        QColor sliderGroove;
        QColor sliderHandle;
        QColor accentButton;
        QColor accentButtonText;
        QColor accentButtonBorder;
        QColor padFill;
        QColor padBorder;
        QColor padSelected;
        QColor playLoaded;
        QColor playEmpty;
        QColor playOutline;
        QColor loopOff;
        QColor loopOn;
        QColor loadButton;
        QColor stopArmed;
        QColor playhead;
        QColor playheadDelay;
        QColor playheadBorder;
        QColor playheadText;
        QColor sceneFill;
        QColor sceneActiveFill;
        QColor sceneBorder;
        QColor sceneActiveBorder;
        QColor sceneText;
        QColor sceneActiveText;
        QColor scenePlayingText;
        QColor sceneEditingBackground;
        QColor sceneEditingText;
        QColor sampleBackground;
        QColor sampleBorder;
        QColor sampleText;
        QColor sampleGrip;
        QColor sampleSelected;
        QColor dropIndicator;
        QColor progressTrack;
        QColor progressChunk;
    };

    static Theme& instance();

    Id id() const { return m_id; }
    QString idName() const;
    static QString idLabel(Id id);
    const Palette& palette() const { return m_palette; }

    static int roleCount();
    static QString roleKey(int index);
    static QString roleGroup(int index);
    static QString roleLabel(int index);
    QColor colorAt(int index) const;

    void setTheme(Id id);
    void setColorAt(int index, const QColor& color);
    void load(const QString& themeId, const QJsonObject& colors);
    QJsonObject colorsJson() const;
    void apply();
    QString styleSheet() const;

signals:
    void changed();

private:
    Theme();
    static Palette preset(Id id);

    Id m_id = Id::Parchment;
    Palette m_palette;
};
