#include "Theme.h"

#include <QApplication>

namespace {

struct Role {
    const char* key;
    const char* group;
    const char* label;
    QColor Theme::Palette::* color;
};

const Role kRoles[] = {
    {"background", "Window", "Background", &Theme::Palette::background},
    {"text", "Window", "Text", &Theme::Palette::text},
    {"menuBackground", "Window", "Menu bar", &Theme::Palette::menuBackground},
    {"menuText", "Window", "Menu text", &Theme::Palette::menuText},
    {"panelBackground", "Window", "Panel", &Theme::Palette::panelBackground},
    {"panelBorder", "Window", "Panel border", &Theme::Palette::panelBorder},
    {"tabBackground", "Window", "Tab", &Theme::Palette::tabBackground},
    {"tabText", "Window", "Tab text", &Theme::Palette::tabText},
    {"tabBorder", "Window", "Tab border", &Theme::Palette::tabBorder},

    {"fieldBackground", "Controls", "Field", &Theme::Palette::fieldBackground},
    {"fieldBorder", "Controls", "Field border", &Theme::Palette::fieldBorder},
    {"fieldText", "Controls", "Field text", &Theme::Palette::fieldText},
    {"focusBorder", "Controls", "Focus border", &Theme::Palette::focusBorder},
    {"selection", "Controls", "Selection", &Theme::Palette::selection},
    {"selectionText", "Controls", "Selection text", &Theme::Palette::selectionText},
    {"sliderGroove", "Controls", "Slider track", &Theme::Palette::sliderGroove},
    {"sliderHandle", "Controls", "Slider handle", &Theme::Palette::sliderHandle},
    {"accentButton", "Controls", "Button", &Theme::Palette::accentButton},
    {"accentButtonText", "Controls", "Button text", &Theme::Palette::accentButtonText},
    {"accentButtonBorder", "Controls", "Button border", &Theme::Palette::accentButtonBorder},

    {"padFill", "Pads", "Pad", &Theme::Palette::padFill},
    {"padBorder", "Pads", "Pad border", &Theme::Palette::padBorder},
    {"padSelected", "Pads", "Pad selection", &Theme::Palette::padSelected},
    {"playLoaded", "Pads", "Play", &Theme::Palette::playLoaded},
    {"playEmpty", "Pads", "Play empty", &Theme::Palette::playEmpty},
    {"playOutline", "Pads", "Play outline", &Theme::Palette::playOutline},
    {"loopOff", "Pads", "Loop", &Theme::Palette::loopOff},
    {"loopOn", "Pads", "Loop active", &Theme::Palette::loopOn},
    {"loadButton", "Pads", "Load", &Theme::Palette::loadButton},
    {"stopArmed", "Pads", "Stop", &Theme::Palette::stopArmed},
    {"playhead", "Pads", "Playhead", &Theme::Palette::playhead},
    {"playheadDelay", "Pads", "Playhead delay", &Theme::Palette::playheadDelay},
    {"playheadBorder", "Pads", "Playhead border", &Theme::Palette::playheadBorder},
    {"playheadText", "Pads", "Playhead text", &Theme::Palette::playheadText},

    {"sceneFill", "Scenes", "Scene", &Theme::Palette::sceneFill},
    {"sceneActiveFill", "Scenes", "Active scene", &Theme::Palette::sceneActiveFill},
    {"sceneBorder", "Scenes", "Scene border", &Theme::Palette::sceneBorder},
    {"sceneActiveBorder", "Scenes", "Active scene border", &Theme::Palette::sceneActiveBorder},
    {"sceneText", "Scenes", "Scene text", &Theme::Palette::sceneText},
    {"sceneActiveText", "Scenes", "Active scene text", &Theme::Palette::sceneActiveText},
    {"scenePlayingText", "Scenes", "Playing scene text", &Theme::Palette::scenePlayingText},
    {"sceneEditingBackground", "Scenes", "Scene name edit", &Theme::Palette::sceneEditingBackground},
    {"sceneEditingText", "Scenes", "Scene name text", &Theme::Palette::sceneEditingText},

    {"sampleBackground", "Samples", "Sample", &Theme::Palette::sampleBackground},
    {"sampleBorder", "Samples", "Sample border", &Theme::Palette::sampleBorder},
    {"sampleText", "Samples", "Sample text", &Theme::Palette::sampleText},
    {"sampleGrip", "Samples", "Grip", &Theme::Palette::sampleGrip},
    {"sampleSelected", "Samples", "Sample selection", &Theme::Palette::sampleSelected},
    {"dropIndicator", "Samples", "Drop indicator", &Theme::Palette::dropIndicator},
    {"progressTrack", "Samples", "Progress track", &Theme::Palette::progressTrack},
    {"progressChunk", "Samples", "Progress", &Theme::Palette::progressChunk},
};

QString hex(const QColor& color)
{
    return color.name(QColor::HexRgb);
}

Theme::Palette parchmentPalette()
{
    Theme::Palette p;
    p.background = QColor(QStringLiteral("#9a8e84"));
    p.text = QColor(QStringLiteral("#1c1410"));
    p.menuBackground = QColor(QStringLiteral("#7d7168"));
    p.menuText = QColor(QStringLiteral("#fbe9d8"));
    p.panelBackground = QColor(QStringLiteral("#7d7168"));
    p.panelBorder = QColor(QStringLiteral("#5a524c"));
    p.tabBackground = QColor(QStringLiteral("#685e56"));
    p.tabText = QColor(QStringLiteral("#fbe9d8"));
    p.tabBorder = QColor(QStringLiteral("#4a433e"));
    p.fieldBackground = QColor(QStringLiteral("#f4ebe3"));
    p.fieldBorder = QColor(QStringLiteral("#68533e"));
    p.fieldText = QColor(QStringLiteral("#1c1410"));
    p.focusBorder = QColor(QStringLiteral("#404040"));
    p.selection = QColor(QStringLiteral("#d08331"));
    p.selectionText = QColor(QStringLiteral("#1c1410"));
    p.sliderGroove = QColor(QStringLiteral("#c4b8ae"));
    p.sliderHandle = QColor(QStringLiteral("#68533e"));
    p.accentButton = QColor(QStringLiteral("#7b2800"));
    p.accentButtonText = QColor(QStringLiteral("#fbe9d8"));
    p.accentButtonBorder = QColor(QStringLiteral("#3a1400"));
    p.padFill = QColor(QStringLiteral("#fbe9d8"));
    p.padBorder = QColor(QStringLiteral("#202020"));
    p.padSelected = QColor(QStringLiteral("#65aecd"));
    p.playLoaded = QColor(QStringLiteral("#d08331"));
    p.playEmpty = QColor(QStringLiteral("#998c84"));
    p.playOutline = QColor(QStringLiteral("#c0c0c0"));
    p.loopOff = QColor(QStringLiteral("#9b6f42"));
    p.loopOn = QColor(QStringLiteral("#ff3232"));
    p.loadButton = QColor(QStringLiteral("#404040"));
    p.stopArmed = QColor(QStringLiteral("#faa078"));
    p.playhead = QColor(QStringLiteral("#bfe2ac"));
    p.playheadDelay = QColor(QStringLiteral("#808080"));
    p.playheadBorder = QColor(QStringLiteral("#404040"));
    p.playheadText = QColor(QStringLiteral("#000000"));
    p.sceneFill = QColor(QStringLiteral("#404040"));
    p.sceneActiveFill = QColor(QStringLiteral("#d8d8d8"));
    p.sceneBorder = QColor(QStringLiteral("#202020"));
    p.sceneActiveBorder = QColor(QStringLiteral("#65aecd"));
    p.sceneText = QColor(QStringLiteral("#f2f2f2"));
    p.sceneActiveText = QColor(QStringLiteral("#f2f2f2"));
    p.scenePlayingText = QColor(QStringLiteral("#1c1410"));
    p.sceneEditingBackground = QColor(QStringLiteral("#f4ebe3"));
    p.sceneEditingText = QColor(QStringLiteral("#1c1410"));
    p.sampleBackground = QColor(QStringLiteral("#404040"));
    p.sampleBorder = QColor(QStringLiteral("#2a2a2a"));
    p.sampleText = QColor(QStringLiteral("#f4ebe3"));
    p.sampleGrip = QColor(QStringLiteral("#a89888"));
    p.sampleSelected = QColor(QStringLiteral("#ff6464"));
    p.dropIndicator = QColor(QStringLiteral("#ff6464"));
    p.progressTrack = QColor(QStringLiteral("#2a2a2a"));
    p.progressChunk = QColor(QStringLiteral("#68533e"));
    return p;
}

Theme::Palette nightPalette()
{
    Theme::Palette p;
    p.background = QColor(QStringLiteral("#241f1c"));
    p.text = QColor(QStringLiteral("#f3e6d8"));
    p.menuBackground = QColor(QStringLiteral("#1a1614"));
    p.menuText = QColor(QStringLiteral("#f6e7d6"));
    p.panelBackground = QColor(QStringLiteral("#1a1614"));
    p.panelBorder = QColor(QStringLiteral("#3d342c"));
    p.tabBackground = QColor(QStringLiteral("#3a312b"));
    p.tabText = QColor(QStringLiteral("#f6e7d6"));
    p.tabBorder = QColor(QStringLiteral("#2a231e"));
    p.fieldBackground = QColor(QStringLiteral("#3a312b"));
    p.fieldBorder = QColor(QStringLiteral("#8d7360"));
    p.fieldText = QColor(QStringLiteral("#f3e6d8"));
    p.focusBorder = QColor(QStringLiteral("#d08331"));
    p.selection = QColor(QStringLiteral("#d08331"));
    p.selectionText = QColor(QStringLiteral("#1c1410"));
    p.sliderGroove = QColor(QStringLiteral("#4a3f37"));
    p.sliderHandle = QColor(QStringLiteral("#e0a15a"));
    p.accentButton = QColor(QStringLiteral("#8f3a12"));
    p.accentButtonText = QColor(QStringLiteral("#fbe9d8"));
    p.accentButtonBorder = QColor(QStringLiteral("#4a1c08"));
    p.padFill = QColor(QStringLiteral("#2c2622"));
    p.padBorder = QColor(QStringLiteral("#0e0c0b"));
    p.padSelected = QColor(QStringLiteral("#7ec4d6"));
    p.playLoaded = QColor(QStringLiteral("#e09a45"));
    p.playEmpty = QColor(QStringLiteral("#6a5c52"));
    p.playOutline = QColor(QStringLiteral("#d9cbbd"));
    p.loopOff = QColor(QStringLiteral("#c4a06a"));
    p.loopOn = QColor(QStringLiteral("#ff5a4a"));
    p.loadButton = QColor(QStringLiteral("#3a3a3a"));
    p.stopArmed = QColor(QStringLiteral("#e88862"));
    p.playhead = QColor(QStringLiteral("#8fbf78"));
    p.playheadDelay = QColor(QStringLiteral("#6e6e6e"));
    p.playheadBorder = QColor(QStringLiteral("#2a2a2a"));
    p.playheadText = QColor(QStringLiteral("#f3e6d8"));
    p.sceneFill = QColor(QStringLiteral("#2a2724"));
    p.sceneActiveFill = QColor(QStringLiteral("#4a4038"));
    p.sceneBorder = QColor(QStringLiteral("#0e0c0b"));
    p.sceneActiveBorder = QColor(QStringLiteral("#7ec4d6"));
    p.sceneText = QColor(QStringLiteral("#f2ebe4"));
    p.sceneActiveText = QColor(QStringLiteral("#f3e6d8"));
    p.scenePlayingText = QColor(QStringLiteral("#1a1614"));
    p.sceneEditingBackground = QColor(QStringLiteral("#3a312b"));
    p.sceneEditingText = QColor(QStringLiteral("#f3e6d8"));
    p.sampleBackground = QColor(QStringLiteral("#2e2a27"));
    p.sampleBorder = QColor(QStringLiteral("#1a1715"));
    p.sampleText = QColor(QStringLiteral("#f3e6d8"));
    p.sampleGrip = QColor(QStringLiteral("#a89888"));
    p.sampleSelected = QColor(QStringLiteral("#ff6b6b"));
    p.dropIndicator = QColor(QStringLiteral("#ff6b6b"));
    p.progressTrack = QColor(QStringLiteral("#1a1715"));
    p.progressChunk = QColor(QStringLiteral("#e0a15a"));
    return p;
}

Theme::Palette forestPalette()
{
    Theme::Palette p;
    p.background = QColor(QStringLiteral("#6d7b5e"));
    p.text = QColor(QStringLiteral("#172016"));
    p.menuBackground = QColor(QStringLiteral("#3e4c36"));
    p.menuText = QColor(QStringLiteral("#f4f7ec"));
    p.panelBackground = QColor(QStringLiteral("#3e4c36"));
    p.panelBorder = QColor(QStringLiteral("#2c3826"));
    p.tabBackground = QColor(QStringLiteral("#55664a"));
    p.tabText = QColor(QStringLiteral("#f4f7ec"));
    p.tabBorder = QColor(QStringLiteral("#2c3826"));
    p.fieldBackground = QColor(QStringLiteral("#eef3e4"));
    p.fieldBorder = QColor(QStringLiteral("#3e4c36"));
    p.fieldText = QColor(QStringLiteral("#172016"));
    p.focusBorder = QColor(QStringLiteral("#2f6f4e"));
    p.selection = QColor(QStringLiteral("#c47a28"));
    p.selectionText = QColor(QStringLiteral("#172016"));
    p.sliderGroove = QColor(QStringLiteral("#c5d2b4"));
    p.sliderHandle = QColor(QStringLiteral("#3e4c36"));
    p.accentButton = QColor(QStringLiteral("#6a3a14"));
    p.accentButtonText = QColor(QStringLiteral("#f4f7ec"));
    p.accentButtonBorder = QColor(QStringLiteral("#3a220c"));
    p.padFill = QColor(QStringLiteral("#f4f7ec"));
    p.padBorder = QColor(QStringLiteral("#172016"));
    p.padSelected = QColor(QStringLiteral("#2f7f86"));
    p.playLoaded = QColor(QStringLiteral("#d0893a"));
    p.playEmpty = QColor(QStringLiteral("#8b987c"));
    p.playOutline = QColor(QStringLiteral("#d5deca"));
    p.loopOff = QColor(QStringLiteral("#6d5330"));
    p.loopOn = QColor(QStringLiteral("#d24b3a"));
    p.loadButton = QColor(QStringLiteral("#3a4034"));
    p.stopArmed = QColor(QStringLiteral("#e59a72"));
    p.playhead = QColor(QStringLiteral("#a6cf78"));
    p.playheadDelay = QColor(QStringLiteral("#7d8774"));
    p.playheadBorder = QColor(QStringLiteral("#3a4034"));
    p.playheadText = QColor(QStringLiteral("#172016"));
    p.sceneFill = QColor(QStringLiteral("#2f3b2a"));
    p.sceneActiveFill = QColor(QStringLiteral("#d7e2c8"));
    p.sceneBorder = QColor(QStringLiteral("#172016"));
    p.sceneActiveBorder = QColor(QStringLiteral("#2f7f86"));
    p.sceneText = QColor(QStringLiteral("#f4f7ec"));
    p.sceneActiveText = QColor(QStringLiteral("#172016"));
    p.scenePlayingText = QColor(QStringLiteral("#172016"));
    p.sceneEditingBackground = QColor(QStringLiteral("#eef3e4"));
    p.sceneEditingText = QColor(QStringLiteral("#172016"));
    p.sampleBackground = QColor(QStringLiteral("#2f3b2a"));
    p.sampleBorder = QColor(QStringLiteral("#1c2419"));
    p.sampleText = QColor(QStringLiteral("#f4f7ec"));
    p.sampleGrip = QColor(QStringLiteral("#b7c4a4"));
    p.sampleSelected = QColor(QStringLiteral("#e15b4a"));
    p.dropIndicator = QColor(QStringLiteral("#e15b4a"));
    p.progressTrack = QColor(QStringLiteral("#1c2419"));
    p.progressChunk = QColor(QStringLiteral("#d0893a"));
    return p;
}

Theme::Palette inkPalette()
{
    Theme::Palette p;
    p.background = QColor(QStringLiteral("#d7d2c8"));
    p.text = QColor(QStringLiteral("#1b1e24"));
    p.menuBackground = QColor(QStringLiteral("#243044"));
    p.menuText = QColor(QStringLiteral("#f7f4ee"));
    p.panelBackground = QColor(QStringLiteral("#243044"));
    p.panelBorder = QColor(QStringLiteral("#1a2333"));
    p.tabBackground = QColor(QStringLiteral("#31445e"));
    p.tabText = QColor(QStringLiteral("#f7f4ee"));
    p.tabBorder = QColor(QStringLiteral("#1a2333"));
    p.fieldBackground = QColor(QStringLiteral("#fbf9f4"));
    p.fieldBorder = QColor(QStringLiteral("#243044"));
    p.fieldText = QColor(QStringLiteral("#1b1e24"));
    p.focusBorder = QColor(QStringLiteral("#2b6cb0"));
    p.selection = QColor(QStringLiteral("#c4493a"));
    p.selectionText = QColor(QStringLiteral("#fbf9f4"));
    p.sliderGroove = QColor(QStringLiteral("#c9c3b8"));
    p.sliderHandle = QColor(QStringLiteral("#243044"));
    p.accentButton = QColor(QStringLiteral("#1b2838"));
    p.accentButtonText = QColor(QStringLiteral("#f7f4ee"));
    p.accentButtonBorder = QColor(QStringLiteral("#0e1622"));
    p.padFill = QColor(QStringLiteral("#fbf9f4"));
    p.padBorder = QColor(QStringLiteral("#1b1e24"));
    p.padSelected = QColor(QStringLiteral("#2b6cb0"));
    p.playLoaded = QColor(QStringLiteral("#c4493a"));
    p.playEmpty = QColor(QStringLiteral("#b7b1a6"));
    p.playOutline = QColor(QStringLiteral("#8d93a0"));
    p.loopOff = QColor(QStringLiteral("#7a6248"));
    p.loopOn = QColor(QStringLiteral("#d64545"));
    p.loadButton = QColor(QStringLiteral("#3c4250"));
    p.stopArmed = QColor(QStringLiteral("#e08a78"));
    p.playhead = QColor(QStringLiteral("#7eb07a"));
    p.playheadDelay = QColor(QStringLiteral("#8d93a0"));
    p.playheadBorder = QColor(QStringLiteral("#3c4250"));
    p.playheadText = QColor(QStringLiteral("#1b1e24"));
    p.sceneFill = QColor(QStringLiteral("#243044"));
    p.sceneActiveFill = QColor(QStringLiteral("#e7eef6"));
    p.sceneBorder = QColor(QStringLiteral("#1b1e24"));
    p.sceneActiveBorder = QColor(QStringLiteral("#2b6cb0"));
    p.sceneText = QColor(QStringLiteral("#f7f4ee"));
    p.sceneActiveText = QColor(QStringLiteral("#1b1e24"));
    p.scenePlayingText = QColor(QStringLiteral("#1b1e24"));
    p.sceneEditingBackground = QColor(QStringLiteral("#fbf9f4"));
    p.sceneEditingText = QColor(QStringLiteral("#1b1e24"));
    p.sampleBackground = QColor(QStringLiteral("#243044"));
    p.sampleBorder = QColor(QStringLiteral("#1a2333"));
    p.sampleText = QColor(QStringLiteral("#f7f4ee"));
    p.sampleGrip = QColor(QStringLiteral("#9aa6b8"));
    p.sampleSelected = QColor(QStringLiteral("#e15d4a"));
    p.dropIndicator = QColor(QStringLiteral("#e15d4a"));
    p.progressTrack = QColor(QStringLiteral("#1a2333"));
    p.progressChunk = QColor(QStringLiteral("#c4493a"));
    return p;
}

} // namespace

Theme& Theme::instance()
{
    static Theme theme;
    return theme;
}

Theme::Theme()
    : m_palette(parchmentPalette())
{
}

QString Theme::idName() const
{
    switch (m_id) {
    case Id::Night:
        return QStringLiteral("night");
    case Id::Forest:
        return QStringLiteral("forest");
    case Id::Ink:
        return QStringLiteral("ink");
    case Id::Parchment:
        break;
    }
    return QStringLiteral("parchment");
}

QString Theme::idLabel(Id id)
{
    switch (id) {
    case Id::Night:
        return QStringLiteral("Night");
    case Id::Forest:
        return QStringLiteral("Forest");
    case Id::Ink:
        return QStringLiteral("Ink");
    case Id::Parchment:
        break;
    }
    return QStringLiteral("Parchment");
}

Theme::Palette Theme::preset(Id id)
{
    switch (id) {
    case Id::Night:
        return nightPalette();
    case Id::Forest:
        return forestPalette();
    case Id::Ink:
        return inkPalette();
    case Id::Parchment:
        break;
    }
    return parchmentPalette();
}

int Theme::roleCount()
{
    return static_cast<int>(sizeof(kRoles) / sizeof(kRoles[0]));
}

QString Theme::roleKey(int index)
{
    return QString::fromLatin1(kRoles[index].key);
}

QString Theme::roleGroup(int index)
{
    return QString::fromLatin1(kRoles[index].group);
}

QString Theme::roleLabel(int index)
{
    return QString::fromLatin1(kRoles[index].label);
}

QColor Theme::colorAt(int index) const
{
    return m_palette.*(kRoles[index].color);
}

void Theme::setTheme(Id id)
{
    m_id = id;
    m_palette = preset(id);
    apply();
}

void Theme::setColorAt(int index, const QColor& color)
{
    if (!color.isValid() || index < 0 || index >= roleCount()) {
        return;
    }
    m_palette.*(kRoles[index].color) = color;
    apply();
}

void Theme::load(const QString& themeId, const QJsonObject& colors)
{
    Id id = Id::Parchment;
    if (themeId == QLatin1String("night")) {
        id = Id::Night;
    } else if (themeId == QLatin1String("forest")) {
        id = Id::Forest;
    } else if (themeId == QLatin1String("ink")) {
        id = Id::Ink;
    }
    m_id = id;
    m_palette = preset(id);
    for (int i = 0; i < roleCount(); ++i) {
        const QString key = roleKey(i);
        if (!colors.contains(key)) {
            continue;
        }
        const QColor color(colors.value(key).toString());
        if (color.isValid()) {
            m_palette.*(kRoles[i].color) = color;
        }
    }
    apply();
}

QJsonObject Theme::colorsJson() const
{
    QJsonObject colors;
    for (int i = 0; i < roleCount(); ++i) {
        colors.insert(roleKey(i), hex(colorAt(i)));
    }
    return colors;
}

void Theme::apply()
{
    if (qApp) {
        qApp->setStyleSheet(styleSheet());
    }
    emit changed();
}

QString Theme::styleSheet() const
{
    const Palette& p = m_palette;
    QString qss;
    qss += QStringLiteral(
        "QMainWindow, QWidget#central, QStackedWidget,\n"
        "QScrollArea, QAbstractScrollArea::viewport,\n"
        "QWidget#PadGrid, QWidget#SceneListHost, QWidget#SampleListHost, QWidget#CollapseFill,\n"
        "QWidget#SettingsPage, QWidget#ThemePage {\n"
        "    background-color: %1;\n"
        "    color: %2;\n"
        "}\n"
        "QMenuBar, QMenu {\n"
        "    background: %3;\n"
        "    color: %4;\n"
        "}\n"
        "QMenu {\n"
        "    border: 1px solid %5;\n"
        "}\n"
        "QMenu::item:selected {\n"
        "    background: %6;\n"
        "    color: %7;\n"
        "}\n"
        "QCheckBox { color: %2; }\n"
        "QLabel { color: %2; }\n"
        "QMessageBox {\n"
        "    background-color: %1;\n"
        "    color: %2;\n"
        "}\n"
        "QMessageBox QLabel { color: %2; }\n")
        .arg(hex(p.background), hex(p.text), hex(p.menuBackground), hex(p.menuText),
            hex(p.panelBorder), hex(p.selection), hex(p.selectionText));

    qss += QStringLiteral(
        "QWidget#SoundPad, QWidget#PadCard { background: transparent; border: none; }\n"
        "QPushButton#ScenePlay, QPushButton#ScenePlay:hover, QPushButton#ScenePlay:pressed,\n"
        "QPushButton#ScenePlay:checked, QPushButton#ScenePlay:focus {\n"
        "    background: %7;\n"
        "    color: %8;\n"
        "    border: none;\n"
        "}\n"
        "QPushButton#SceneStop { background: %1; color: %2; border: none; }\n"
        "QLineEdit#PadName { background: transparent; border: none; color: %2; padding: 0; }\n"
        "QLineEdit#PadName:focus { background: %3; border: 1px solid %4; }\n"
        "QLabel#PadVolumeValue { background: transparent; color: %2; padding: 0; }\n"
        "QSlider#PadVolume::groove:horizontal { height: 6px; background: %5; border-radius: 3px; }\n"
        "QSlider#PadVolume::handle:horizontal { background: %6; width: 12px; margin: -4px 0; border-radius: 6px; }\n")
        .arg(hex(p.stopArmed), hex(p.text), hex(p.fieldBackground), hex(p.focusBorder),
            hex(p.sliderGroove), hex(p.sliderHandle), hex(p.sceneFill), hex(p.sceneText));

    qss += QStringLiteral(
        "QWidget#SceneRow { background: transparent; color: %1; border: none; }\n"
        "QLineEdit#SceneName { background: transparent; border: none; color: %1; padding: 0; }\n"
        "QWidget#SceneRow[active=\"true\"] QLineEdit#SceneName { color: %2; }\n"
        "QWidget#SceneRow[playing=\"true\"] QLineEdit#SceneName { color: %6; }\n"
        "QLineEdit#SceneName[editing=\"true\"],\n"
        "QWidget#SceneRow[active=\"true\"] QLineEdit#SceneName[editing=\"true\"],\n"
        "QWidget#SceneRow[playing=\"true\"] QLineEdit#SceneName[editing=\"true\"] {\n"
            "    background: %3;\n"
            "    border: 1px solid %4;\n"
            "    color: %5;\n"
            "    padding: 2px 4px;\n"
            "}\n")
        .arg(hex(p.sceneText), hex(p.sceneActiveText), hex(p.sceneEditingBackground),
            hex(p.fieldBorder), hex(p.sceneEditingText), hex(p.scenePlayingText));

    qss += QStringLiteral(
        "QPushButton#AddScene, QPushButton#AddSample, QPushButton#SceneDelete {\n"
        "    background: %1;\n"
        "    color: %2;\n"
        "    border: 1px solid %3;\n"
        "    font-size: 20px;\n"
        "}\n"
        "QWidget#SampleRow { background: %4; border: 1px solid %5; border-radius: 4px; }\n"
        "QWidget#SampleRow QLabel { color: %6; background: transparent; }\n")
        .arg(hex(p.accentButton), hex(p.accentButtonText), hex(p.accentButtonBorder),
            hex(p.sampleBackground), hex(p.sampleBorder), hex(p.sampleText));
    qss += QStringLiteral(
        "QWidget#SampleRow QLabel#SampleGrip { color: %1; font-size: 14px; }\n"
        "QFrame#ListDropIndicator { background: %2; border: none; }\n"
        "QWidget#SampleRow QProgressBar { background: %3; border: none; border-radius: 3px; }\n"
        "QWidget#SampleRow QProgressBar::chunk { background: %4; border-radius: 3px; }\n"
        "QProgressBar#LoadProgress { background: %3; border: none; border-radius: 3px; }\n"
        "QProgressBar#LoadProgress::chunk { background: %4; border-radius: 3px; }\n"
        "QWidget#SampleRow[selected=\"true\"] { border: 2px solid %5; }\n")
        .arg(hex(p.sampleGrip), hex(p.dropIndicator), hex(p.progressTrack),
            hex(p.progressChunk), hex(p.sampleSelected));

    qss += QStringLiteral(
        "QSlider::groove:horizontal { height: 8px; background: %1; border-radius: 4px; }\n"
        "QSlider::handle:horizontal { background: %2; width: 14px; margin: -4px 0; border-radius: 7px; }\n"
        "QLineEdit, QComboBox {\n"
        "    background: %3;\n"
        "    border: 1px solid %4;\n"
        "    padding: 2px 4px;\n"
        "    color: %5;\n"
        "}\n"
        "QSpinBox {\n"
        "    background: %3;\n"
        "    border: 1px solid %4;\n"
        "    padding: 2px 18px 2px 4px;\n"
        "    color: %5;\n"
        "    min-height: 22px;\n"
        "}\n"
        "QSpinBox::up-button, QSpinBox::down-button { subcontrol-origin: border; width: 16px; }\n"
        "QSpinBox::up-button { subcontrol-position: top right; }\n"
        "QSpinBox::down-button { subcontrol-position: bottom right; }\n"
        "QComboBox QAbstractItemView {\n"
        "    background: %3;\n"
        "    color: %5;\n"
        "    selection-background-color: %6;\n"
        "    selection-color: %7;\n"
        "}\n")
        .arg(hex(p.sliderGroove), hex(p.sliderHandle), hex(p.fieldBackground), hex(p.fieldBorder),
            hex(p.fieldText), hex(p.selection), hex(p.selectionText));

    qss += QStringLiteral(
        "QScrollArea { border: none; background-color: %1; }\n"
        "QWidget#BottomPanel, QWidget#BottomTabBar, QStackedWidget#BottomPages {\n"
        "    background-color: %2;\n"
        "    color: %3;\n"
        "}\n"
        "QStackedWidget#BottomPages, QStackedWidget#SidebarPages {\n"
        "    background-color: %1;\n"
        "    border-top: 1px solid %4;\n"
        "}\n"
        "QLabel#LoadProgressLabel { color: %3; }\n"
        "QLabel#SettingsTitle { font-size: 18px; }\n"
        "QLabel#ThemeSection { font-size: 15px; margin-top: 10px; }\n"
        "QPushButton#BottomTab {\n"
        "    background: %5;\n"
        "    color: %6;\n"
        "    border: 1px solid %7;\n"
        "    border-bottom: none;\n"
        "    padding: 4px 14px;\n"
        "    min-width: 72px;\n"
        "}\n"
        "QPushButton#BottomTab[active=\"true\"] { background: %1; color: %3; }\n"
        "QPushButton#BottomCollapse {\n"
        "    background: %5;\n"
        "    color: %6;\n"
        "    border: 1px solid %7;\n"
        "    padding: 0;\n"
        "    font-size: 14px;\n"
        "}\n"
        "QLabel#SampleInfo { color: %3; }\n")
        .arg(hex(p.background), hex(p.panelBackground), hex(p.text), hex(p.panelBorder),
            hex(p.tabBackground), hex(p.tabText), hex(p.tabBorder));
    return qss;
}
