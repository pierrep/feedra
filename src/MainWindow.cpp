#include "MainWindow.h"
#include "Intensity.h"
#include "AudioFormats.h"
#include "CueOutput.h"
#include "FsPath.h"
#include "KeyLayout.h"
#include "MidiInput.h"
#include "OpenALSoundPlayer.h"
#include "PeakStore.h"
#include "ProjectFile.h"
#include "ProjectBackups.h"
#include "SampleLoadQueue.h"
#include "Scene.h"
#include "Theme.h"
#include "widgets/ReorderListHost.h"
#include "widgets/SoundPadWidget.h"
#include "widgets/BottomPanel.h"
#include "widgets/EditorPanel.h"
#include "widgets/TabButton.h"
#include "widgets/SettingsPage.h"
#include "widgets/ThemePage.h"
#include "widgets/FileBrowserWidget.h"
#include "widgets/LogWindow.h"
#include "VolumeDb.h"

#include <QAction>
#include <QUndoCommand>
#include <QUndoStack>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDebug>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHash>
#include <QRegularExpression>
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QPolygonF>
#include <QPainter>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QThread>
#include <QSaveFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QTreeWidget>
#include <QUrl>
#include <QSet>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>
#include <cmath>
#include <filesystem>

namespace {

// Shows a file path in whatever width the header has left, keeping the file name visible.
class SettingsPathLabel : public QLabel
{
public:
    explicit SettingsPathLabel(QWidget* parent = nullptr)
        : QLabel(parent)
    {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    }

    void setFullText(const QString& text)
    {
        m_full = text;
        setToolTip(text);
        applyElision();
    }

    QSize sizeHint() const override
    {
        return QSize(160, QLabel::sizeHint().height());
    }

    QSize minimumSizeHint() const override
    {
        return QSize(0, QLabel::minimumSizeHint().height());
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QLabel::resizeEvent(event);
        applyElision();
    }

private:
    void applyElision()
    {
        const int width = contentsRect().width();
        const QString shown = width > 0 ? fontMetrics().elidedText(m_full, Qt::ElideMiddle, width) : m_full;
        if (text() != shown) {
            QLabel::setText(shown);
        }
    }

    QString m_full;
};

// The startup choice lives in the default settings file, which is the only path known
// before any project is opened.
void readStartupFilePrefs(const QString& settingsPath, bool& loadLast, QString& lastPath)
{
    loadLast = false;
    lastPath.clear();
    QFile file(settingsPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        return;
    }
    const QJsonObject global = doc.object().value(QStringLiteral("global")).toObject();
    loadLast = global.value(QStringLiteral("loadlastsettings")).toBool(false);
    lastPath = global.value(QStringLiteral("lastsettingspath")).toString();
}

// Header layout switch: a small icon button, raised while its layout is the one showing.
// Neutral colours only; ember stays for things that are live.
class LayoutButton : public QAbstractButton
{
public:
    enum class Glyph { FullLayout, ScenesOnly };

    LayoutButton(Glyph glyph, const QString& tip, QWidget* parent)
        : QAbstractButton(parent)
        , m_glyph(glyph)
    {
        setCheckable(true);
        setFixedSize(30, 28);
        setFocusPolicy(Qt::NoFocus);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        setToolTip(tip);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const Theme::Palette& theme = Theme::instance().palette();
        const bool hover = underMouse() && isEnabled();
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        if (isChecked()) {
            p.setPen(QPen(theme.fieldBorder, 1.0));
            p.setBrush(theme.fieldBackground);
            p.drawRoundedRect(r, 6, 6);
        } else if (hover) {
            p.setPen(Qt::NoPen);
            p.setBrush(theme.tabBackground);
            p.drawRoundedRect(r, 6, 6);
        }
        const QColor fg = (isChecked() || hover) ? theme.text : theme.textMuted;
        const QPointF c = r.center();
        // A 16x13 window outline in both glyphs.
        const QRectF frame(c.x() - 8.0, c.y() - 6.5, 16.0, 13.0);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(fg, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawRoundedRect(frame, 2.0, 2.0);
        p.setPen(Qt::NoPen);
        p.setBrush(fg);
        if (m_glyph == Glyph::FullLayout) {
            // Pads on the left (2x2), a divider, the sidebar on the right.
            const qreal cell = 3.0;
            const qreal x0 = frame.left() + 2.5;
            const qreal y0 = frame.top() + 2.5;
            for (int row = 0; row < 2; ++row) {
                for (int col = 0; col < 2; ++col) {
                    p.drawRoundedRect(QRectF(x0 + col * (cell + 1.5), y0 + row * (cell + 1.5), cell, cell), 0.6, 0.6);
                }
            }
            p.drawRect(QRectF(frame.right() - 5.5, frame.top() + 1.5, 1.0, frame.height() - 3.0));
        } else {
            // Three list rows: the scene list on its own.
            for (int i = 0; i < 3; ++i) {
                p.drawRoundedRect(QRectF(frame.left() + 3.0, frame.top() + 2.8 + i * 3.0, frame.width() - 6.0, 1.6), 0.8, 0.8);
            }
        }
    }

private:
    Glyph m_glyph;
};

}

// One undo step: the pads it changed, as they were before and after. Pads are found by scene
// and pad id, so a step still applies after its pad's scene was deleted and put back.
class PadEditCommand : public QUndoCommand
{
public:
    struct Entry {
        int sceneId = 0;
        int padId = 0;
        Pad::PadClip before;
        Pad::PadClip after;
    };

    PadEditCommand(MainWindow* window, std::vector<Entry> entries, const QString& text, bool merge)
        : QUndoCommand(text)
        , m_window(window)
        , m_entries(std::move(entries))
        , m_merge(merge)
    {
        m_time.start();
    }

    void undo() override { apply(false); }
    void redo() override
    {
        if (m_first) {
            m_first = false; // it has just happened
            return;
        }
        apply(true);
    }
    int id() const override { return m_merge ? 1 : -1; }
    bool mergeWith(const QUndoCommand* other) override
    {
        const auto* next = static_cast<const PadEditCommand*>(other);
        if (!next->m_merge || next->text() != text() || m_entries.size() != 1 || next->m_entries.size() != 1
            || next->m_entries[0].sceneId != m_entries[0].sceneId || next->m_entries[0].padId != m_entries[0].padId
            || m_time.elapsed() > 2000) {
            return false;
        }
        m_entries[0].after = next->m_entries[0].after;
        m_time.restart();
        // Dragged back to where it started: nothing to undo.
        setObsolete(m_entries[0].after == m_entries[0].before);
        return true;
    }

private:
    void apply(bool forward)
    {
        for (const Entry& entry : m_entries) {
            if (Pad* pad = m_window->findPad(entry.sceneId, entry.padId)) {
                pad->restoreClip(forward ? entry.after : entry.before);
                m_window->afterUndoChange(pad);
            }
        }
    }

    MainWindow* m_window;
    std::vector<Entry> m_entries;
    bool m_merge = false;
    bool m_first = true;
    QElapsedTimer m_time;
};

// Deleting a scene: undo puts it back where it was, with its pads.
class SceneDeleteCommand : public QUndoCommand
{
public:
    SceneDeleteCommand(MainWindow* window, int index, int sceneId, QJsonObject scene, const QString& text)
        : QUndoCommand(text)
        , m_window(window)
        , m_index(index)
        , m_sceneId(sceneId)
        , m_scene(std::move(scene))
    {
    }

    void undo() override { m_window->insertSceneFromJson(m_index, m_scene); }
    void redo() override
    {
        if (m_first) {
            m_first = false;
            return;
        }
        for (int i = 0; i < m_window->m_scenes.size(); ++i) {
            if (m_window->m_scenes[i]->id == m_sceneId) {
                m_window->removeSceneAt(i);
                return;
            }
        }
    }

private:
    MainWindow* m_window;
    int m_index;
    int m_sceneId;
    QJsonObject m_scene;
    bool m_first = true;
};

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    m_undo = new QUndoStack(this);
    m_undo->setUndoLimit(200);
    setWindowTitle(QStringLiteral("Feedra"));
    resize(1150, 800);
    m_config.setup();
    m_local = m_config.loadLocalSettings();
    PeakStore::instance().setCacheDir(m_config.waveformCacheDir());
    m_loads = new SampleLoadQueue(this);
    AudioDevice::initialize();
    for (int which = 0; which < OpenALSoundPlayer::kConvolutionCount; ++which) {
        AudioDevice::setConvolutionGain(AudioDevice::defaultConvolutionGain(), which);
        if (!AudioDevice::setConvolutionImpulse(FsPath::fromQString(m_config.defaultImpulsePath()), which)) {
            qWarning() << "Convolution impulse not loaded" << m_config.defaultImpulsePath();
        }
    }
    m_curDevice = QString::fromStdString(AudioDevice::defaultDeviceName());

    m_midi = new MidiInput(this);
    connect(m_midi, &MidiInput::received, this, &MainWindow::handleMidi);
    const QJsonObject midi = m_local.value(QStringLiteral("midi")).toObject();
    m_controls.fromJson(midi);
    m_midiPort = midi.value(QStringLiteral("input")).toString();

    buildMenus();
    buildUi();
    openMidiInput();

    const QString settings = m_config.defaultSettingsPath();
    bool loadLast = false;
    QString lastPath;
    readStartupFilePrefs(settings, loadLast, lastPath);
    m_config.loadLastSettings = loadLast;
    m_config.lastSettingsPath = lastPath;

    QString toLoad = settings;
    if (loadLast && !lastPath.isEmpty() && QFile::exists(lastPath)) {
        toLoad = QFileInfo(lastPath).absoluteFilePath();
    }
    // Opening a project backs it up (see ProjectBackups).
    ProjectBackups::setRoot(m_config.dataDir());
    if (QFile::exists(toLoad)) {
        loadConfigFrom(toLoad);
    } else {
        createDefaultScenes();
    }

    if (m_scenes.isEmpty()) {
        createDefaultScenes();
    }
    // The window is this computer's: from local.json, or the project file it used to be
    // saved in, the first time.
    restoreWindowLayout(m_local.contains(QStringLiteral("windowwidth"))
            ? m_local
            : m_config.json().value(QStringLiteral("global")).toObject());
    enableScene(std::clamp(m_config.activeSceneIdx, 0, std::max(0, static_cast<int>(m_scenes.size()) - 1)));
    updateMainControls();
    refreshSettingsPathLabel();
    refreshLoadUi();

    m_clock.start();
    auto* tickTimer = new QTimer(this);
    connect(tickTimer, &QTimer::timeout, this, &MainWindow::tick);
    tickTimer->start(33);

    auto* deviceTimer = new QTimer(this);
    connect(deviceTimer, &QTimer::timeout, this, &MainWindow::checkAudioDevice);
    deviceTimer->start(1000);

    // Arrow keys are otherwise consumed by the focused scroll area, slider or button before reaching this window.
    qApp->installEventFilter(this);
}

MainWindow::~MainWindow()
{
    cancelExport();
    waitForLoads();
    saveOnExit();
    if (m_fileBrowser) {
        m_fileBrowser->stopPreview();
    }
    CueOutput::close();
    if (m_loads) {
        m_loads->shutdown();
    }
    qDeleteAll(m_scenes);
    m_scenes.clear();
}

void MainWindow::buildMenus()
{
    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    auto* openAct = fileMenu->addAction(tr("&Open..."));
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, [this]() { loadConfig(); });

    auto* importAct = fileMenu->addAction(tr("&Import Scenes..."));
    importAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+I")));
    importAct->setToolTip(tr("Add the scenes from another Feedra file after the current ones"));
    connect(importAct, &QAction::triggered, this, [this]() { importScenes(); });

    m_saveAction = fileMenu->addAction(tr("&Save"));
    m_saveAction->setShortcut(QKeySequence::Save);
    connect(m_saveAction, &QAction::triggered, this, [this]() { saveConfig(); });

    m_exportAction = fileMenu->addAction(tr("Export &Project..."));
    m_exportAction->setShortcut(QKeySequence::SaveAs);
    connect(m_exportAction, &QAction::triggered, this, [this]() { exportProject(); });

    m_restoreAction = fileMenu->addAction(tr("Restore &Backup..."));
    m_restoreAction->setToolTip(tr("Go back to an earlier copy of the open project"));
    connect(m_restoreAction, &QAction::triggered, this, [this]() { restoreBackup(); });

    fileMenu->addSeparator();
    auto* quitAct = fileMenu->addAction(tr("E&xit"));
    quitAct->setShortcut(QKeySequence::Quit);
    connect(quitAct, &QAction::triggered, this, &QWidget::close);

    auto* editMenu = menuBar()->addMenu(tr("&Edit"));
    QAction* undoAct = m_undo->createUndoAction(this, tr("&Undo"));
    undoAct->setShortcut(QKeySequence::Undo);
    QAction* redoAct = m_undo->createRedoAction(this, tr("&Redo"));
    redoAct->setShortcuts({ QKeySequence::Redo, QKeySequence(QStringLiteral("Ctrl+Y")) });
    editMenu->addAction(undoAct);
    editMenu->addAction(redoAct);

    auto* viewMenu = menuBar()->addMenu(tr("&View"));
    auto* scenesAct = viewMenu->addAction(tr("Scenes"));
    scenesAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+1")));
    connect(scenesAct, &QAction::triggered, this, [this]() {
        setPage(Page::Main);
        setSidebarView(SidebarView::Scenes);
    });
    auto* editorAct = viewMenu->addAction(tr("Editor"));
    editorAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+2")));
    connect(editorAct, &QAction::triggered, this, [this]() {
        setPage(Page::Main);
        setSidebarView(SidebarView::Editor);
    });
    auto* filesAct = viewMenu->addAction(tr("Files"));
    filesAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+3")));
    connect(filesAct, &QAction::triggered, this, [this]() {
        setPage(Page::Main);
        setSidebarView(SidebarView::Files);
    });
    auto* settingsAct = viewMenu->addAction(tr("Settings"));
    settingsAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+4")));
    connect(settingsAct, &QAction::triggered, this, [this]() { setPage(Page::Settings); });
    auto* themeAct = viewMenu->addAction(tr("Theme"));
    themeAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+5")));
    connect(themeAct, &QAction::triggered, this, [this]() { setPage(Page::Theme); });
    viewMenu->addSeparator();
    auto* logsAct = viewMenu->addAction(tr("Logs"));
    logsAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+L")));
    connect(logsAct, &QAction::triggered, this, [this]() {
        if (!m_logWindow) {
            m_logWindow = new LogWindow(this);
        }
        m_logWindow->show();
        m_logWindow->raise();
        m_logWindow->activateWindow();
    });
}

void MainWindow::buildUi()
{
    auto* central = new QWidget(this);
    central->setObjectName(QStringLiteral("central"));
    setCentralWidget(central);
    auto* outer = new QVBoxLayout(central);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    m_pages = new QStackedWidget(central);

    m_mainPage = new QWidget(m_pages);
    auto* mainLayout = new QVBoxLayout(m_mainPage);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    m_mainVolume = new QSlider(Qt::Horizontal, m_mainPage);
    m_mainVolume->setRange(0, VolumeDb::sliderSpan(VolumeDb::kFloorDb, VolumeDb::kUnityDb));
    m_mainVolume->setValue(VolumeDb::sliderSpan(VolumeDb::kFloorDb, VolumeDb::kUnityDb));
    m_mainVolume->setObjectName(QStringLiteral("MasterVolume"));
    m_mainVolume->setFixedWidth(240);
    m_mainVolumeValue = new QLabel(m_mainPage);
    m_mainVolumeValue->setObjectName(QStringLiteral("MainVolumeValue"));
    m_mainVolumeValue->setMinimumWidth(72);
    m_mainVolumeValue->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_mainVolumeValue->setText(VolumeDb::format(VolumeDb::kUnityDb));
    auto* header = new QWidget(m_mainPage);
    header->setObjectName(QStringLiteral("Header"));
    header->setAttribute(Qt::WA_StyledBackground, true);
    auto* volumeRow = new QHBoxLayout(header);
    m_headerRow = volumeRow;
    volumeRow->setContentsMargins(20, 12, 20, 10);
    volumeRow->setSpacing(12);
    auto* volumeCaption = new QLabel(tr("Main Volume"), header);
    volumeCaption->setObjectName(QStringLiteral("HeaderLabel"));
    m_volumeCaption = volumeCaption;
    volumeRow->addWidget(volumeCaption);
    volumeRow->addWidget(m_mainVolume);
    volumeRow->addWidget(m_mainVolumeValue);
    // Live: talk-over (latches here; Space holds it) and Panic.
    m_talkButton = new QPushButton(tr("Talk"), header);
    m_talkButton->setObjectName(QStringLiteral("TalkOver"));
    m_talkButton->setCheckable(true);
    m_talkButton->setFocusPolicy(Qt::NoFocus);
    m_talkButton->setCursor(Qt::PointingHandCursor);
    m_talkButton->setToolTip(tr("Talk-over: dips every scene so you can be heard. Click to keep it down, "
                                "or hold Space."));
    m_panicButton = new QPushButton(tr("Panic"), header);
    m_panicButton->setObjectName(QStringLiteral("Panic"));
    m_panicButton->setFocusPolicy(Qt::NoFocus);
    m_panicButton->setCursor(Qt::PointingHandCursor);
    m_panicButton->setToolTip(tr("Fade everything out (Esc). Press again to stop at once."));
    volumeRow->addSpacing(8);
    volumeRow->addWidget(m_talkButton);
    volumeRow->addWidget(m_panicButton);
    connect(m_talkButton, &QPushButton::toggled, this, [this](bool on) {
        m_duck.setHeld(Duck::Button, on);
    });
    connect(m_panicButton, &QPushButton::clicked, this, &MainWindow::panic);

    // Intensity: the selected scene's slider between its Calm, Tense and Combat pads.
    m_intensityBox = new QWidget(header);
    auto* intensityRow = new QHBoxLayout(m_intensityBox);
    intensityRow->setContentsMargins(0, 0, 0, 0);
    intensityRow->setSpacing(8);
    auto* intensityCaption = new QLabel(tr("Intensity"), m_intensityBox);
    intensityCaption->setObjectName(QStringLiteral("HeaderLabel"));
    m_intensity = new QSlider(Qt::Horizontal, m_intensityBox);
    m_intensity->setObjectName(QStringLiteral("Intensity"));
    m_intensity->setRange(0, 1000);
    m_intensity->setPageStep(250);
    m_intensity->setFixedWidth(160);
    m_intensity->setFocusPolicy(Qt::NoFocus); // the arrow keys and Space stay with the pads
    m_intensity->setToolTip(tr("Crossfades the selected scene between its Calm, Tense and Combat pads "
                               "(set a pad's layer on the Pad tab). Right-click to learn a MIDI control."));
    m_intensityValue = new QLabel(m_intensityBox);
    m_intensityValue->setObjectName(QStringLiteral("MainVolumeValue"));
    m_intensityValue->setMinimumWidth(110);
    intensityRow->addWidget(intensityCaption);
    intensityRow->addWidget(m_intensity);
    intensityRow->addWidget(m_intensityValue);
    m_intensityBox->hide();
    volumeRow->addSpacing(8);
    volumeRow->addWidget(m_intensityBox);
    connect(m_intensity, &QSlider::valueChanged, this, [this](int v) {
        if (Scene* scene = activeScene()) {
            scene->setIntensity(static_cast<float>(v) / 1000.0f);
            m_intensityValue->setText(Intensity::describe(scene->layerMask(), scene->intensity()));
        }
    });
    m_intensity->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_intensity, &QWidget::customContextMenuRequested, this, [this](const QPoint& at) {
        QMenu menu(this);
        QAction* learn = menu.addAction(tr("MIDI learn"));
        QAction* forget = menu.addAction(tr("Forget MIDI"));
        forget->setEnabled(m_controls.bindingFor(ControlAction::Intensity) != nullptr);
        QAction* chosen = menu.exec(m_intensity->mapToGlobal(at));
        if (chosen == learn) {
            startMidiLearn(ControlAction::Intensity);
        } else if (chosen == forget) {
            m_controls.clear(ControlAction::Intensity);
            saveLocalSettings();
            m_settingsPage->refreshMidi();
        }
    });
    m_loadBar = new QProgressBar(m_mainPage);
    m_loadBar->setObjectName(QStringLiteral("LoadProgress"));
    m_loadBar->setTextVisible(false);
    m_loadBar->setFixedWidth(220);
    m_loadBar->setFixedHeight(4);
    m_loadBar->setRange(0, 1);
    m_loadBar->hide();
    m_loadLabel = new QLabel(m_mainPage);
    m_loadLabel->setObjectName(QStringLiteral("LoadProgressLabel"));
    m_loadLabel->hide();
    m_midiStatus = new QLabel(header);
    m_midiStatus->setObjectName(QStringLiteral("MidiStatus"));
    m_midiStatus->hide();
    volumeRow->addSpacing(16);
    volumeRow->addWidget(m_loadBar);
    volumeRow->addWidget(m_loadLabel);
    volumeRow->addWidget(m_midiStatus);
    m_settingsPathLabel = new SettingsPathLabel(header);
    m_settingsPathLabel->setObjectName(QStringLiteral("SettingsPath"));
    m_settingsPathLabel->hide();
    volumeRow->addWidget(m_settingsPathLabel, 1);

    // Layout switch, right-justified: the full layout, or the scene list on its own.
    auto* layoutSwitch = new QWidget(header);
    auto* layoutRow = new QHBoxLayout(layoutSwitch);
    layoutRow->setContentsMargins(0, 0, 0, 0);
    layoutRow->setSpacing(2);
    m_fullLayoutButton = new LayoutButton(LayoutButton::Glyph::FullLayout, tr("Pads and scenes"), layoutSwitch);
    m_scenesFocusButton = new LayoutButton(LayoutButton::Glyph::ScenesOnly, tr("Scenes only"), layoutSwitch);
    m_fullLayoutButton->setChecked(true);
    layoutRow->addWidget(m_fullLayoutButton);
    layoutRow->addWidget(m_scenesFocusButton);
    // Stretch 0: takes the spare width only while the settings path label (stretch 1) is hidden,
    // so the switch stays at the right edge either way.
    volumeRow->addStretch(0);
    volumeRow->addWidget(layoutSwitch, 0, Qt::AlignVCenter);
    connect(m_fullLayoutButton, &QAbstractButton::clicked, this, [this]() { setScenesFocus(false); });
    connect(m_scenesFocusButton, &QAbstractButton::clicked, this, [this]() { setScenesFocus(true); });
    mainLayout->addWidget(header);

    auto* body = new QHBoxLayout();
    m_bodyLayout = body;
    body->setContentsMargins(12, 0, 0, 0);
    body->setSpacing(12);
    m_padStack = new QStackedWidget(m_mainPage);
    body->addWidget(m_padStack, 1);

    auto* side = new QWidget(m_mainPage);
    m_sidebarWidget = side;
    side->setObjectName(QStringLiteral("Sidebar"));
    side->setAttribute(Qt::WA_StyledBackground, true);
    side->setMinimumWidth(300);
    auto* sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(0, 0, 0, 0);
    sideLayout->setSpacing(0);

    auto* sideTabBar = new QWidget(side);
    sideTabBar->setObjectName(QStringLiteral("BottomTabBar"));
    sideTabBar->setAttribute(Qt::WA_StyledBackground, true);
    auto* sideTabRow = new QHBoxLayout(sideTabBar);
    sideTabRow->setContentsMargins(8, 0, 8, 0);
    sideTabRow->setSpacing(0);
    m_scenesTab = new QPushButton(tr("Scenes"), sideTabBar);
    m_scenesTab->setObjectName(QStringLiteral("BottomTab"));
    m_scenesTab->setFocusPolicy(Qt::NoFocus);
    m_editorTab = new QPushButton(tr("Editor"), sideTabBar);
    m_editorTab->setObjectName(QStringLiteral("BottomTab"));
    m_editorTab->setFocusPolicy(Qt::NoFocus);
    m_filesTab = new QPushButton(tr("Files"), sideTabBar);
    m_filesTab->setObjectName(QStringLiteral("BottomTab"));
    m_filesTab->setFocusPolicy(Qt::NoFocus);
    m_filesTab->setToolTip(tr("Browse and preview audio files, and drag them onto pads (Ctrl+3)"));
    sideTabRow->addWidget(m_scenesTab, 0, Qt::AlignBottom);
    sideTabRow->addWidget(m_editorTab, 0, Qt::AlignBottom);
    sideTabRow->addWidget(m_filesTab, 0, Qt::AlignBottom);
    sideTabRow->addStretch();

    m_sidebarStack = new QStackedWidget(side);
    m_sidebarStack->setObjectName(QStringLiteral("SidebarPages"));
    m_sidebarStack->setAttribute(Qt::WA_StyledBackground, true);

    m_scenesPage = new QWidget(m_sidebarStack);
    auto* scenesLayout = new QVBoxLayout(m_scenesPage);
    scenesLayout->setContentsMargins(12, 12, 4, 12);
    scenesLayout->setSpacing(10);
    auto* sceneScroll = new QScrollArea(m_scenesPage);
    m_sceneScroll = sceneScroll;
    // The list grows a moment after a scene is added; scroll once the new length is known.
    connect(sceneScroll->verticalScrollBar(), &QScrollBar::rangeChanged, this, [this]() {
        if (m_scrollToSceneRow && m_sceneScroll) {
            m_sceneScroll->ensureWidgetVisible(m_scrollToSceneRow, 0, 8);
        }
    });
    sceneScroll->setFrameShape(QFrame::NoFrame);
    sceneScroll->setWidgetResizable(true);
    sceneScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sceneScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    sceneScroll->viewport()->setAutoFillBackground(true);
    auto* sceneHost = new ReorderListHost(SceneRowWidget::dragMimeType(), sceneScroll);
    sceneHost->setObjectName(QStringLiteral("SceneListHost"));
    m_sceneListHost = sceneHost;
    m_sceneListLayout = new QVBoxLayout(m_sceneListHost);
    m_sceneListLayout->setContentsMargins(0, 0, 8, 0);
    m_sceneListLayout->setSpacing(4);
    m_sceneListLayout->addStretch();
    sceneScroll->setWidget(m_sceneListHost);
    connect(sceneHost, &ReorderListHost::itemReordered, this, [this](int sceneId, int insertIndex) {
        for (int i = 0; i < m_scenes.size(); ++i) {
            if (m_scenes[i]->id == sceneId) {
                moveScene(i, insertIndex);
                return;
            }
        }
    }, Qt::QueuedConnection); // after QDrag::exec returns, so the source row is still alive
    m_addScene = new QPushButton(tr("+  New scene"), m_scenesPage);
    m_addScene->setObjectName(QStringLiteral("AddScene"));
    m_addScene->setFixedHeight(30); // matches the scene rows
    m_addScene->setCursor(Qt::PointingHandCursor);
    scenesLayout->addWidget(sceneScroll, 1);
    auto* addSceneRow = new QHBoxLayout();
    addSceneRow->setContentsMargins(0, 0, 8, 0);
    addSceneRow->addWidget(m_addScene, 1);
    scenesLayout->addLayout(addSceneRow);

    m_editor = new EditorPanel(&m_config, m_sidebarStack);
    connect(m_editor, &EditorPanel::samplesChanged, this, &MainWindow::updateMainControls);
    connect(m_editor, &EditorPanel::editStarting, this, &MainWindow::padEditStarting);
    connect(m_editor, &EditorPanel::edited, this, &MainWindow::padEdited);
    connect(m_editor, &EditorPanel::loopRegionChanged, this, [this](AudioSample*) { m_bottom->refreshWaveform(); });

    m_fileBrowser = new FileBrowserWidget(&m_config, m_sidebarStack);
    m_fileBrowser->setProjectFoldersProvider([this]() {
        QStringList folders;
        for (Scene* scene : m_scenes) {
            for (Pad* pad : scene->pads) {
                for (const QString& path : pad->samplePaths()) {
                    folders << QFileInfo(path).absolutePath();
                }
            }
        }
        return folders;
    });
    connect(m_fileBrowser, &FileBrowserWidget::libraryFolderChosen, this, [this](const QString&) {
        m_settingsPage->sync(); // the browser has set the library already
    });

    m_sidebarStack->addWidget(m_scenesPage);
    m_sidebarStack->addWidget(m_editor);
    m_sidebarStack->addWidget(m_fileBrowser);
    sideLayout->addWidget(sideTabBar);
    sideLayout->addWidget(m_sidebarStack, 1);
    body->addWidget(side);
    mainLayout->addLayout(body, 1);

    m_settingsPage = new SettingsPage(m_config, m_pages);
    connect(m_settingsPage, &SettingsPage::backRequested, this, [this]() {
        setPage(Page::Main);
        setSidebarView(SidebarView::Scenes);
    });
    connect(m_settingsPage, &SettingsPage::sceneLimitChanged, this, [this]() {
        m_addScene->setEnabled(static_cast<unsigned int>(m_scenes.size()) < m_config.maxScenes);
    });
    connect(m_settingsPage, &SettingsPage::outputDeviceChanged, this, [this]() {
        applyOutputDevice();
        saveLocalSettings();
    });
    connect(m_settingsPage, &SettingsPage::previewDeviceChanged, this, [this]() {
        applyPreviewDevice();
        saveLocalSettings();
    });
    connect(m_settingsPage, &SettingsPage::keyboardPadsChanged, this, &MainWindow::refreshKeyHints);
    m_settingsPage->setMidi(m_midi, &m_controls);
    connect(m_settingsPage, &SettingsPage::midiInputChosen, this, [this](const QString& port) {
        m_midiPort = port;
        m_midi->close();
        openMidiInput();
        saveLocalSettings();
    });
    connect(m_settingsPage, &SettingsPage::midiLearnRequested, this, [this](ControlAction action) {
        startMidiLearn(action);
    });
    connect(m_settingsPage, &SettingsPage::midiMapChanged, this, &MainWindow::saveLocalSettings);
    connect(m_settingsPage, &SettingsPage::reverbChanged, this, [this]() { m_bottom->refreshSendLabels(); });
    m_themePage = new ThemePage(m_pages);
    connect(m_themePage, &ThemePage::backRequested, this, [this]() {
        setPage(Page::Main);
        setSidebarView(SidebarView::Scenes);
    });

    m_pages->addWidget(m_mainPage);
    m_pages->addWidget(m_settingsPage);
    m_pages->addWidget(m_themePage);
    outer->addWidget(m_pages, 1);

    m_bottom = new BottomPanel(&m_config, central);
    outer->addWidget(m_bottom, 0);
    connect(m_bottom, &BottomPanel::collapseRequested, this, [this](bool collapsed) { setBottomCollapsed(collapsed); });
    connect(m_bottom, &BottomPanel::editStarting, this, &MainWindow::padEditStarting);
    connect(m_bottom, &BottomPanel::edited, this, &MainWindow::padEdited);
    connect(m_bottom, &BottomPanel::loopRegionChanged, this, [this](AudioSample* sample) { m_editor->syncLoop(sample); });

    connect(m_addScene, &QPushButton::clicked, this, &MainWindow::addNewScene);
    connect(m_scenesTab, &QPushButton::clicked, this, [this]() {
        setSidebarView(SidebarView::Scenes);
    });
    connect(m_editorTab, &QPushButton::clicked, this, [this]() {
        setSidebarView(SidebarView::Editor);
    });
    connect(m_filesTab, &QPushButton::clicked, this, [this]() {
        setSidebarView(SidebarView::Files);
    });
    m_mainVolume->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_mainVolume, &QWidget::customContextMenuRequested, this, [this](const QPoint& at) {
        QMenu menu(this);
        QAction* learn = menu.addAction(tr("MIDI learn"));
        QAction* forget = menu.addAction(tr("Forget MIDI"));
        forget->setEnabled(m_controls.bindingFor(ControlAction::MasterVolume) != nullptr);
        QAction* chosen = menu.exec(m_mainVolume->mapToGlobal(at));
        if (chosen == learn) {
            startMidiLearn(ControlAction::MasterVolume);
        } else if (chosen == forget) {
            m_controls.clear(ControlAction::MasterVolume);
            saveLocalSettings();
            m_settingsPage->refreshMidi();
        }
    });
    connect(m_mainVolume, &QSlider::valueChanged, this, [this](int v) {
        const float db = VolumeDb::fromSlider(v, VolumeDb::kFloorDb);
        m_config.setMasterVolume(VolumeDb::toLinearMuted(db));
        m_mainVolumeValue->setText(VolumeDb::format(db));
    });

    syncSettingsPage();

    new QShortcut(QKeySequence(QStringLiteral("Ctrl+D")), this, [this]() {
        if (m_page != Page::Main) {
            return;
        }
        if (m_sidebar == SidebarView::Scenes) {
            clearActivePad();
        } else if (m_sidebar == SidebarView::Editor) {
            clearActiveSample();
        }
    });
    setSidebarView(SidebarView::Scenes);
}

void MainWindow::connectScene(Scene* scene)
{
    for (int i = 0; i < scene->padWidgets.size(); ++i) {
        scene->padWidgets[i]->setKeyHint(m_config.keyboardPads ? KeyLayout::keyLabel(i, scene->columns()) : QString());
    }
    connect(scene, &Scene::padSelected, this, &MainWindow::onPadClicked);
    connect(scene, &Scene::goRequested, this, [this](int sceneId) {
        for (int i = 0; i < m_scenes.size(); ++i) {
            if (m_scenes[i]->id == sceneId) {
                goToScene(i);
                return;
            }
        }
    });
    connect(scene->row(), &SceneRowWidget::deleteRequested, this, &MainWindow::deleteScene);
    for (int i = 0; i < scene->pads.size(); ++i) {
        Pad* pad = scene->pads[i];
        pad->setLoadQueue(m_loads);
        connect(scene->padWidgets[i], &SoundPadWidget::padDropped, this, [this, pad](int from, int to, bool copy) {
            if (pad->hasSounds()) {
                // Onto a pad with sounds: add to them rather than replace them.
                mergePad(from, to, copy);
            } else if (copy) {
                copyPad(from, to);
            } else {
                movePad(from, to);
            }
        });
        connect(pad, &Pad::filesDropped, this, [this]() { updateMainControls(); });
        scene->padWidgets[i]->setMidiMenu(MidiInput::available());
        connect(scene->padWidgets[i], &SoundPadWidget::editStarting, this, &MainWindow::padEditStarting);
        connect(scene->padWidgets[i], &SoundPadWidget::edited, this, &MainWindow::padEdited);
        connect(scene->padWidgets[i], &SoundPadWidget::midiLearnRequested, this, [this](int padId, bool volume) {
            startMidiLearn(volume ? ControlAction::PadVolume : ControlAction::PadPlay, padId);
        });
        connect(scene->padWidgets[i], &SoundPadWidget::midiForgetRequested, this, [this](int padId) {
            m_controls.clear(ControlAction::PadPlay, padId);
            m_controls.clear(ControlAction::PadVolume, padId);
            saveLocalSettings();
            m_settingsPage->refreshMidi();
        });
        connect(pad, &Pad::scenePadChanged, this, [this, pad]() {
            if (pad == activePad()) {
                syncPanelPad();
                m_bottom->syncScenePad();
            }
        });
        connect(pad, &Pad::sampleLoopChanged, this, [this, pad]() {
            if (pad == activePad()) {
                syncPanelPad();
                m_editor->updateControls();
            }
        });
        connect(&pad->soundPlayer(), &SoundPlayer::panRandomised, this, [this, pad](int sampleIndex, float) {
            if (pad == activePad()) {
                syncPanelPad();
                m_editor->syncPan(sampleIndex);
            }
        });
        connect(pad, &Pad::loadStateChanged, this, [this, pad]() {
            refreshLoadUi();
            if (pad == activePad() && m_sidebar == SidebarView::Editor) {
                refreshEditorPage();
            }
        });
        connect(pad, &Pad::loadingFinished, this, [this, pad]() {
            for (Scene* scene : m_scenes) {
                if (scene->id == pad->sceneId() && scene->isPlaying && pad->isLoaded() && !pad->isLoading()) {
                    pad->play();
                }
            }
            updateMainControls();
            if (pad == activePad() && m_sidebar == SidebarView::Editor) {
                refreshEditorPage();
            }
        });
    }
}

void MainWindow::createDefaultScenes()
{
    for (int i = 0; i < 4; ++i) {
        auto* scene = new Scene(&m_config, i, QString(), m_padStack, m_sceneListHost, this);
        m_scenes.push_back(scene);
        m_padStack->addWidget(scene->grid());
        m_sceneListLayout->insertWidget(m_sceneListLayout->count() - 1, scene->row());
        connectScene(scene);
    }
    m_config.activeSceneIdx = 0;
    m_config.activeSceneId = 0;
}

// The lowest scene id not in use, filling gaps left by deleted scenes.
int MainWindow::nextSceneId() const
{
    QVector<int> ids;
    for (Scene* s : m_scenes) {
        ids.push_back(s->id);
    }
    std::sort(ids.begin(), ids.end());
    int newId = 0;
    if (!ids.isEmpty() && ids.front() == 0) {
        newId = ids.back() + 1;
        for (int i = 1; i < ids.size(); ++i) {
            if (ids[i] - ids[i - 1] > 1) {
                newId = ids[i - 1] + 1;
                break;
            }
        }
    }
    return newId;
}

void MainWindow::addNewScene()
{
    if (static_cast<unsigned int>(m_scenes.size()) >= m_config.maxScenes) {
        return;
    }
    auto* scene = new Scene(&m_config, nextSceneId(), QString(), m_padStack, m_sceneListHost, this);
    m_scenes.push_back(scene);
    m_padStack->addWidget(scene->grid());
    m_sceneListLayout->insertWidget(m_sceneListLayout->count() - 1, scene->row());
    connectScene(scene);
    m_addScene->setEnabled(static_cast<unsigned int>(m_scenes.size()) < m_config.maxScenes);
    enableScene(m_scenes.size() - 1); // also scrolls it into view
}

// Scrolls the scene list so `row` is in view, once the list has laid out its new size.
void MainWindow::scrollToSceneRow(QWidget* row)
{
    if (!m_sceneScroll || !row) {
        return;
    }
    // The scroll range only grows once the scroll area has resized the list, which happens
    // after this returns (and at startup, after the window first appears); rangeChanged
    // (connected in buildUi) scrolls again when it does.
    m_scrollToSceneRow = row;
    const int serial = ++m_scrollToSceneSerial;
    QTimer::singleShot(0, this, [this]() {
        if (m_scrollToSceneRow && m_sceneScroll) {
            m_sceneScroll->ensureWidgetVisible(m_scrollToSceneRow, 0, 8);
        }
    });
    // Only for a moment: later changes to the list's length shouldn't pull it back.
    QTimer::singleShot(1500, this, [this, serial]() {
        if (serial == m_scrollToSceneSerial) {
            m_scrollToSceneRow = nullptr;
        }
    });
}

void MainWindow::scrollToActiveScene()
{
    if (Scene* active = activeScene()) {
        scrollToSceneRow(active->row());
    }
}

void MainWindow::deleteScene(int sceneId)
{
    int idx = -1;
    for (int i = 0; i < m_scenes.size(); ++i) {
        if (m_scenes[i]->id == sceneId) {
            idx = i;
            break;
        }
    }
    Scene* scene = idx >= 0 ? m_scenes[idx] : nullptr;
    if (!scene || m_scenes.size() == 1) {
        QMessageBox::warning(this, tr("Feedra"), tr("Cannot delete scene - must have at least one scene in project"));
        return;
    }
    QString sceneName = scene->row() ? scene->row()->sceneName().trimmed() : scene->name.trimmed();
    if (sceneName.isEmpty()) {
        sceneName = scene->name.trimmed();
    }
    if (sceneName.isEmpty()) {
        sceneName = tr("Scene %1").arg(scene->id + 1);
    }
    QMessageBox confirm(this);
    confirm.setIcon(QMessageBox::Question);
    confirm.setWindowTitle(tr("Delete scene"));
    confirm.setText(tr("Are you sure you want to delete"));
    confirm.setInformativeText(sceneName);
    confirm.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    confirm.setDefaultButton(QMessageBox::No);
    if (confirm.exec() != QMessageBox::Yes) {
        return;
    }
    const QJsonObject saved = sceneToJson(scene);
    removeSceneAt(idx);
    m_undo->push(new SceneDeleteCommand(this, idx, sceneId, saved, tr("Delete scene \"%1\"").arg(sceneName)));
}

void MainWindow::removeSceneAt(int idx)
{
    if (idx < 0 || idx >= m_scenes.size() || m_scenes.size() == 1) {
        return;
    }
    Scene* scene = m_scenes[idx];
    const bool deletingActive = (idx == m_config.activeSceneIdx);
    for (Pad* pad : scene->pads) {
        m_pendingEdits.erase(pad);
    }
    m_padStack->removeWidget(scene->grid());
    m_scenes.removeAt(idx);
    delete scene;
    m_addScene->setEnabled(static_cast<unsigned int>(m_scenes.size()) < m_config.maxScenes);
    int nextIdx = m_config.activeSceneIdx;
    if (deletingActive) {
        nextIdx = qMin(idx, m_scenes.size() - 1);
    } else if (idx < m_config.activeSceneIdx) {
        nextIdx = m_config.activeSceneIdx - 1;
    }
    enableScene(nextIdx);
    updateSceneListLayout();
}

QJsonObject MainWindow::sceneToJson(const Scene* scene) const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), scene->id);
    obj.insert(QStringLiteral("name"), scene->row() ? scene->row()->sceneName() : scene->name);
    obj.insert(QStringLiteral("activesound"), scene->activeSoundIdx);
    scene->writeSettings(obj);
    for (const Pad* pad : scene->pads) {
        pad->saveToJson(obj);
    }
    return obj;
}

void MainWindow::insertSceneFromJson(int index, const QJsonObject& obj)
{
    const int id = obj.value(QStringLiteral("id")).toInt();
    for (Scene* existing : m_scenes) {
        if (existing->id == id) {
            return; // already there
        }
    }
    auto* scene = new Scene(&m_config, id, obj.value(QStringLiteral("name")).toString(), m_padStack, m_sceneListHost, this);
    scene->activeSoundIdx = obj.value(QStringLiteral("activesound")).toInt(0);
    scene->readSettings(obj);
    connectScene(scene);
    // Saved with absolute paths, so no folder to resolve against.
    const QJsonObject root{ { QStringLiteral("scene"), obj } };
    for (Pad* pad : scene->pads) {
        pad->loadFromJson(root);
    }
    index = std::clamp(index, 0, static_cast<int>(m_scenes.size()));
    m_scenes.insert(index, scene);
    m_padStack->addWidget(scene->grid());
    updateSceneListLayout();
    m_addScene->setEnabled(static_cast<unsigned int>(m_scenes.size()) < m_config.maxScenes);
    enableScene(index);
}

MainWindow::PadSnapshot MainWindow::snapshotPads(std::initializer_list<Pad*> pads) const
{
    PadSnapshot snapshot;
    for (Pad* pad : pads) {
        if (pad) {
            snapshot.entries.push_back({ pad->sceneId(), pad->padId(), pad->clip() });
        }
    }
    return snapshot;
}

void MainWindow::recordPadEdit(const PadSnapshot& snapshot, const QString& text, bool merge)
{
    std::vector<PadEditCommand::Entry> entries;
    for (const PadSnapshot::Entry& e : snapshot.entries) {
        Pad* pad = findPad(e.sceneId, e.padId);
        if (!pad) {
            continue;
        }
        Pad::PadClip after = pad->clip();
        if (after != e.before) {
            entries.push_back({ e.sceneId, e.padId, e.before, std::move(after) });
        }
    }
    if (!entries.empty()) {
        m_undo->push(new PadEditCommand(this, std::move(entries), text, merge));
    }
}

void MainWindow::padEditStarting(Pad* pad)
{
    // A drag sends many in a row: the first one holds the "before".
    if (pad && m_pendingEdits.find(pad) == m_pendingEdits.end()) {
        m_pendingEdits.emplace(pad, snapshotPads({ pad }));
    }
}

void MainWindow::padEdited(Pad* pad, const QString& what)
{
    const auto it = m_pendingEdits.find(pad);
    if (it == m_pendingEdits.end()) {
        return;
    }
    const PadSnapshot snapshot = it->second;
    m_pendingEdits.erase(it);
    recordPadEdit(snapshot, what, true);
}

void MainWindow::afterUndoChange(Pad* pad)
{
    if (pad == activePad()) {
        updateMainControls();
        if (m_sidebar == SidebarView::Editor) {
            refreshEditorPage();
        }
        m_bottom->refreshWaveform();
    }
}


void MainWindow::enableScene(int idx)
{
    if (idx < 0 || idx >= m_scenes.size()) {
        return;
    }
    // Selecting a scene finishes its fades, except a crossfade, which carries on.
    for (Scene* scene : m_scenes) {
        if (!scene->isCrossfading()) {
            scene->endFade();
        }
    }
    m_config.prevSceneIdx = m_config.activeSceneIdx;
    m_config.activeSceneIdx = idx;
    m_config.activeSceneId = m_scenes[idx]->id;
    m_config.activeSoundIdx = m_scenes[idx]->activeSoundIdx;
    for (int i = 0; i < m_scenes.size(); ++i) {
        m_scenes[i]->setActive(i == idx);
    }
    m_padStack->setCurrentWidget(m_scenes[idx]->grid());
    if (!m_padStack->contentsRect().isEmpty()) {
        if (QLayout* stackLayout = m_padStack->layout()) {
            stackLayout->invalidate();
            stackLayout->activate();
        }
        m_scenes[idx]->layoutGrid();
    }
    updateMainControls();
    if (m_sidebar == SidebarView::Editor) {
        setSidebarView(SidebarView::Editor);
    }
    // Opening a project, restoring a backup, adding or deleting a scene, or stepping through
    // scenes with the keys: the active scene may be below or above what the list shows.
    scrollToSceneRow(m_scenes[idx]->row());
}

void MainWindow::goToScene(int idx)
{
    if (idx < 0 || idx >= m_scenes.size()) {
        return;
    }
    const int fadeMs = static_cast<int>(std::lround(std::clamp(m_config.crossfadeSeconds, 0.0, 30.0) * 1000.0));
    Scene* target = m_scenes[idx];
    if (idx != m_config.activeSceneIdx) {
        enableScene(idx);
    }
    for (Scene* scene : m_scenes) {
        if (scene == target || !(scene->isPlaying || scene->isAudible())) {
            continue;
        }
        scene->isPlaying = false;
        if (fadeMs > 0) {
            scene->stop(fadeMs, true);
        } else {
            scene->stopImmediate();
        }
        scene->row()->setPlaying(false);
    }
    if (!target->isPlaying) {
        target->isPlaying = true;
        target->play(fadeMs, true);
        target->row()->setPlaying(true);
    }
}

void MainWindow::moveScene(int fromIndex, int insertIndex)
{
    if (fromIndex < 0 || fromIndex >= m_scenes.size()) {
        return;
    }
    insertIndex = std::clamp(insertIndex, 0, static_cast<int>(m_scenes.size()));
    const int dest = insertIndex > fromIndex ? insertIndex - 1 : insertIndex;
    if (dest == fromIndex) {
        return;
    }
    Scene* active = activeScene();
    m_scenes.move(fromIndex, dest);
    if (active) {
        m_config.activeSceneIdx = static_cast<int>(m_scenes.indexOf(active));
    }
    updateSceneListLayout();
}

void MainWindow::updateSceneListLayout()
{
    for (Scene* scene : m_scenes) {
        m_sceneListLayout->removeWidget(scene->row());
    }
    for (Scene* scene : m_scenes) {
        m_sceneListLayout->insertWidget(m_sceneListLayout->count() - 1, scene->row());
    }
}

void MainWindow::updateMainControls()
{
    syncPanelPad();
    m_bottom->refreshPadControls();
    for (Scene* scene : m_scenes) {
        for (SoundPadWidget* w : scene->padWidgets) {
            w->setSelected(scene == activeScene() && w->padId() == m_config.activeSoundIdx);
        }
    }
}

// Both panels edit the selected pad. Called wherever it may have changed, and every tick.
Pad* MainWindow::syncPanelPad()
{
    Pad* pad = activePad();
    m_editor->setPad(pad);
    m_bottom->setPad(pad);
    return pad;
}

void MainWindow::setBottomTab(int index)
{
    m_bottom->setCurrentTab(index);
    if (m_bottom->isCollapsed()) {
        setBottomCollapsed(false);
    }
}

void MainWindow::setBottomCollapsed(bool collapsed, bool resizeWindow)
{
    const bool changed = m_bottom->isCollapsed() != collapsed;
    const int pageHeight = m_bottom->pageHeight();
    const bool resizing = changed && resizeWindow && pageHeight > 0 && isVisible()
        && !isMaximized() && !isFullScreen();
    if (resizing) {
        // Toggling the panel first hands its height to the pad grid, then the window resize
        // takes it back: two different grid sizes in a row, seen as a flicker. Hold painting
        // until the window has its new size (released in resizeEvent, or by the timer if the
        // window manager doesn't resize us).
        m_holdGridPaint = true;
        centralWidget()->setUpdatesEnabled(false);
        QTimer::singleShot(250, this, &MainWindow::releaseGridPaint);
    }
    m_bottom->setCollapsedState(collapsed);
    if (resizing) {
        // Apply the panel change to the layout now, while painting is held, so the window
        // resize below is the only layout pass anyone sees.
        settleLayouts();
        resize(width(), height() + (collapsed ? -pageHeight : pageHeight));
    }
}

void MainWindow::syncSettingsPage()
{
    m_settingsPage->sync();
    m_bottom->refreshSendLabels();
    m_themePage->refresh();
    if (m_addScene) {
        m_addScene->setEnabled(static_cast<unsigned int>(m_scenes.size()) < m_config.maxScenes);
    }
}

void MainWindow::applyAppSettings(const QJsonObject& global)
{
    if (global.contains(QStringLiteral("autosave"))) {
        m_config.autosave = global.value(QStringLiteral("autosave")).toBool(true);
    }
    if (global.contains(QStringLiteral("scenelimit"))) {
        m_config.maxScenes = static_cast<unsigned int>(
            std::clamp(global.value(QStringLiteral("scenelimit")).toInt(static_cast<int>(m_config.maxScenes)), 1, 64));
    }
    if (global.contains(QStringLiteral("keyboardpads"))) {
        m_config.keyboardPads = global.value(QStringLiteral("keyboardpads")).toBool(true);
        refreshKeyHints();
    }
    if (global.contains(QStringLiteral("talkoverdepth"))) {
        m_config.talkOverDepthDb = std::clamp(global.value(QStringLiteral("talkoverdepth")).toDouble(m_config.talkOverDepthDb), -40.0, 0.0);
    }
    if (global.contains(QStringLiteral("panictime"))) {
        m_config.panicSeconds = std::clamp(global.value(QStringLiteral("panictime")).toDouble(m_config.panicSeconds), 0.0, 10.0);
    }
    if (global.contains(QStringLiteral("crossfadetime"))) {
        m_config.crossfadeSeconds = std::clamp(global.value(QStringLiteral("crossfadetime")).toDouble(m_config.crossfadeSeconds), 0.0, 30.0);
    }
    if (global.contains(QStringLiteral("gridwidth"))) {
        m_config.gridWidth = std::clamp(global.value(QStringLiteral("gridwidth")).toInt(m_config.gridWidth), 1, 12);
    }
    if (global.contains(QStringLiteral("gridheight"))) {
        m_config.gridHeight = std::clamp(global.value(QStringLiteral("gridheight")).toInt(m_config.gridHeight), 1, 8);
    }
    if (global.contains(QStringLiteral("library"))) {
        m_config.defaultLibraryLocation = global.value(QStringLiteral("library")).toString().trimmed();
    }
    // The devices are this computer's (local.json). A project saved before that carries them
    // in its own settings: used only until this computer has its own.
    const QJsonObject& devices = m_local.contains(QStringLiteral("outputdevice")) ? m_local : global;
    if (devices.contains(QStringLiteral("outputdevice"))) {
        m_config.outputDevice = devices.value(QStringLiteral("outputdevice")).toString();
        applyOutputDevice();
    }
    if (devices.contains(QStringLiteral("previewdevice"))) {
        m_config.previewDevice = devices.value(QStringLiteral("previewdevice")).toString();
        applyPreviewDevice();
    }
    // Keys for the first reverbs are the original ones; the second ones add a "2".
    for (int which = 0; which < OpenALSoundPlayer::kEaxReverbCount; ++which) {
        const QString suffix = which == 0 ? QString() : QString::number(which + 1);
        const QString reverb = global.value(QStringLiteral("reverb") + suffix).toString();
        if (!reverb.isEmpty()) {
            AudioDevice::setReverbPresetById(reverb.toStdString(), which);
        }
    }
    for (int which = 0; which < OpenALSoundPlayer::kConvolutionCount; ++which) {
        const QString suffix = which == 0 ? QString() : QString::number(which + 1);
        if (global.contains(QStringLiteral("convolutiongain") + suffix)) {
            AudioDevice::setConvolutionGain(static_cast<float>(global.value(QStringLiteral("convolutiongain") + suffix).toDouble(
                AudioDevice::defaultConvolutionGain())), which);
        }
        QString impulse = global.value(QStringLiteral("convolutionir") + suffix).toString();
        if (impulse.isEmpty() || !QFile::exists(impulse)) {
            impulse = m_config.defaultImpulsePath();
        }
        if (FsPath::fromQString(impulse) != AudioDevice::convolutionImpulsePath(which)) {
            if (!AudioDevice::setConvolutionImpulse(FsPath::fromQString(impulse), which)) {
                qWarning() << "Convolution impulse not loaded" << impulse;
            }
        }
    }
    if (global.contains(QStringLiteral("theme")) || global.contains(QStringLiteral("themecolors"))) {
        Theme::instance().load(global.value(QStringLiteral("theme")).toString(),
            global.value(QStringLiteral("themecolors")).toObject(),
            global.value(QStringLiteral("themecolorsets")));
    }
    if (m_fileBrowser) {
        m_fileBrowser->applySettings(global);
    }
    syncSettingsPage();
}

void MainWindow::setPage(Page page)
{
    if (page != Page::Main && m_scenesFocus) {
        setScenesFocus(false); // Settings and Theme need the full window
    }
    m_page = page;
    // The Sample / Pad / Waveform panel belongs to the pad grid, not to Settings or Theme.
    if (m_bottom) {
        m_bottom->setVisible(page == Page::Main && !m_scenesFocus);
    }
    if (page == Page::Settings) {
        m_settingsPage->refreshWaveformCacheInfo();
        m_settingsPage->refreshDevices();
        m_pages->setCurrentWidget(m_settingsPage);
    } else if (page == Page::Theme) {
        m_pages->setCurrentWidget(m_themePage);
    } else {
        m_pages->setCurrentWidget(m_mainPage);
        updateMainControls();
    }
}

void MainWindow::setScenesFocus(bool on)
{
    if (on == m_scenesFocus) {
        return;
    }
    if (on) {
        // Remember the full-size window before anything changes.
        m_expandedMaximized = isMaximized();
        m_expandedRect = (isMaximized() || isMinimized()) ? normalGeometry() : geometry();
        m_expandedGeometry = saveGeometry();
        if (m_sidebar != SidebarView::Scenes) {
            setSidebarView(SidebarView::Scenes);
        }
    }
    m_scenesFocus = on;

    m_padStack->setVisible(!on);
    m_editorTab->setVisible(!on);
    m_filesTab->setVisible(!on); // files are dragged onto pads, which this mode hides
    if (m_bottom) {
        m_bottom->setVisible(!on && m_page == Page::Main);
    }
    // With the pads hidden the scene list takes the full width.
    m_bodyLayout->setStretchFactor(m_sidebarWidget, on ? 1 : 0);
    m_bodyLayout->setContentsMargins(on ? 0 : 12, 0, 0, 0);

    // Header: just the fader and the layout switch in the strip.
    m_volumeCaption->setVisible(!on);
    if (on) {
        m_mainVolume->setMinimumWidth(100);
        m_mainVolume->setMaximumWidth(QWIDGETSIZE_MAX);
        m_headerRow->setStretchFactor(m_mainVolume, 1);
    } else {
        m_headerRow->setStretchFactor(m_mainVolume, 0);
        m_mainVolume->setFixedWidth(240);
    }
    refreshLoadUi(); // hides or shows the settings path and the load bar

    const QSignalBlocker blockFull(m_fullLayoutButton);
    const QSignalBlocker blockFocus(m_scenesFocusButton);
    m_fullLayoutButton->setChecked(!on);
    m_scenesFocusButton->setChecked(on);
    m_fullLayoutButton->update();
    m_scenesFocusButton->update();

    // Apply the hidden/shown panels to the layouts now, so the window's minimum size is
    // right before it is resized.
    if (QLayout* layout = m_mainPage->layout()) {
        layout->invalidate();
        layout->activate();
    }
    if (QLayout* layout = centralWidget()->layout()) {
        layout->invalidate();
        layout->activate();
    }
    if (QLayout* layout = this->layout()) {
        layout->invalidate();
        layout->activate();
    }

    if (on) {
        // A narrow strip the same height, keeping the right edge where it was.
        constexpr int kStripWidth = 340;
        if (isMaximized() || isFullScreen()) {
            showNormal();
        }
        const QRect before = m_expandedRect;
        const int width = std::max(kStripWidth, minimumSizeHint().width());
        setGeometry(before.x() + before.width() - width, before.y(), width, before.height());
    } else {
        if (!m_expandedGeometry.isEmpty()) {
            restoreGeometry(m_expandedGeometry); // also restores maximised
        }
        // The grid was hidden while the window changed size: lay it out again once it's
        // back in the layout.
        QTimer::singleShot(0, this, [this]() {
            if (Scene* active = activeScene()) {
                active->layoutGrid();
            }
        });
    }
    // The list changes height with the window either way.
    scrollToActiveScene();
}

void MainWindow::setSidebarView(SidebarView view)
{
    if (view != SidebarView::Scenes && m_scenesFocus) {
        // The Editor needs a pad and files are dragged onto pads, so bring the pads back
        // (e.g. Ctrl+2 or Ctrl+3).
        setScenesFocus(false);
    }
    m_sidebar = view;
    switch (view) {
    case SidebarView::Editor:
        m_config.activeSampleIdx = 0;
        if (auto* pad = activePad(); pad && !pad->soundPlayer().player.empty()) {
            m_config.activeSampleId = pad->soundPlayer().player[0]->id;
        }
        refreshEditorPage();
        break;
    case SidebarView::Files:
        m_sidebarStack->setCurrentWidget(m_fileBrowser);
        m_fileBrowser->view()->setFocus();
        break;
    case SidebarView::Scenes:
        m_sidebarStack->setCurrentWidget(m_scenesPage);
        updateMainControls();
        break;
    }
    m_addScene->setVisible(view == SidebarView::Scenes);
    if (view == SidebarView::Scenes) {
        scrollToActiveScene(); // the list may have been laid out while hidden
    }
    setTabButtonActive(m_scenesTab, view == SidebarView::Scenes);
    setTabButtonActive(m_editorTab, view == SidebarView::Editor);
    setTabButtonActive(m_filesTab, view == SidebarView::Files);
}

void MainWindow::refreshEditorPage()
{
    m_sidebarStack->setCurrentWidget(m_editor);
    syncPanelPad();
    m_editor->refresh();
}

QString MainWindow::currentSettingsFilePath() const
{
    const QString path = m_config.settingsPath.isEmpty() ? m_config.defaultSettingsPath() : m_config.settingsPath;
    return QFileInfo(path).absoluteFilePath();
}

void MainWindow::refreshSettingsPathLabel()
{
    const QString shown = QDir::toNativeSeparators(currentSettingsFilePath());
    static_cast<SettingsPathLabel*>(m_settingsPathLabel)->setFullText(shown);
}

QString MainWindow::resolvedSettingsPath() const
{
    // Prefer the file already open. A missing file falls back to data/settings/settings.json.
    const QString path = currentSettingsFilePath();
    return QFile::exists(path) ? path : m_config.defaultSettingsPath();
}

void MainWindow::saveConfig()
{
    const QString path = resolvedSettingsPath();
    m_saveDeclined = false;
    if (!saveConfigTo(path, false)) {
        if (!m_saveDeclined) {
            QMessageBox::warning(this, tr("Feedra"),
                tr("Could not save settings to:\n%1").arg(path));
        }
        return;
    }
    const QString saved = QFileInfo(path).absoluteFilePath();
    if (m_config.settingsPath != saved) {
        m_config.settingsPath = saved;
        refreshSettingsPathLabel();
    }
}

void MainWindow::saveOnExit()
{
    if (m_savedOnExit) {
        return;
    }
    m_savedOnExit = true;
    saveLocalSettings();
    m_config.lastSettingsPath = currentSettingsFilePath();
    const QString path = currentSettingsFilePath();
    ProjectBackups::backup(path, true);
    // Autosave off: keep the file's scenes as they were last saved, update only app settings.
    saveConfigTo(path, false, !m_config.autosave);

    // The session lives in the open file. The default file only keeps the startup
    // choice when that file is somewhere else.
    const QString defaults = QFileInfo(m_config.defaultSettingsPath()).absoluteFilePath();
    if (QFileInfo(path).absoluteFilePath() == defaults) {
        return;
    }

    QJsonObject root;
    QFile existing(defaults);
    if (existing.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(existing.readAll());
        if (doc.isObject()) {
            root = doc.object();
        }
    }
    QJsonObject global = root.value(QStringLiteral("global")).toObject();
    global.insert(QStringLiteral("loadlastsettings"), m_config.loadLastSettings);
    global.insert(QStringLiteral("lastsettingspath"), m_config.lastSettingsPath);
    root.insert(QStringLiteral("global"), global);

    const QString dirPath = QFileInfo(defaults).absolutePath();
    if (!QDir().mkpath(dirPath)) {
        qWarning() << "Failed to create settings directory" << dirPath;
        return;
    }
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    QSaveFile file(defaults);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        qWarning() << "Failed to record startup settings in" << defaults << file.errorString();
    }
}

void MainWindow::exportProject()
{
    if (isExporting() || (m_loads && m_loads->isBusy())) {
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, tr("Export project"),
        currentSettingsFilePath(), tr("JSON (*.json)"));
    if (path.isEmpty()) {
        return;
    }
    auto state = std::make_shared<ExportState>();
    if (!saveConfigTo(path, true, false, state.get())) {
        QMessageBox::warning(this, tr("Export project"), tr("Could not export the project to:\n%1").arg(QDir::toNativeSeparators(path)));
        return;
    }
    m_export = state;
    m_exportThread = std::thread([state]() { state->run(); });
    refreshLoadUi();
}

// Once a project has finished loading, says which samples couldn't be found. They are kept
// in the project, so they come back when the files are put back and the project reopened.
void MainWindow::reportMissingSamples()
{
    if (!m_reportMissingAfterLoad || (m_loads && m_loads->isBusy())) {
        return;
    }
    m_reportMissingAfterLoad = false;
    QStringList missing;
    for (Scene* scene : m_scenes) {
        for (Pad* pad : scene->pads) {
            for (const QString& path : pad->missingPaths()) {
                if (!missing.contains(path)) {
                    missing << path;
                }
            }
        }
    }
    if (missing.isEmpty()) {
        return;
    }
    constexpr int kShown = 15;
    QStringList shown = missing.mid(0, kShown);
    if (missing.size() > kShown) {
        shown << tr("…and %1 more").arg(missing.size() - kShown);
    }
    const QString text = tr("%n sample file(s) couldn't be loaded. They are kept in the project, "
                            "so they'll load again once the files are back where the project expects them.", nullptr, int(missing.size()))
        + QStringLiteral("\n\n") + shown.join(QLatin1Char('\n'));
    // Shown after this tick, not inside it.
    QTimer::singleShot(0, this, [this, text]() { QMessageBox::warning(this, tr("Missing files"), text); });
}

// Lists the open project's backups and puts the chosen one back in place of the project file.
void MainWindow::restoreBackup()
{
    if (isExporting() || (m_loads && m_loads->isBusy())) {
        return;
    }
    const QString project = QFileInfo(currentSettingsFilePath()).absoluteFilePath();
    const QString name = QFileInfo(project).fileName();
    const QList<ProjectBackups::Entry> entries = ProjectBackups::list(project);
    const QString folder = ProjectBackups::folderFor(project);
    if (entries.isEmpty()) {
        QMessageBox::information(this, tr("Restore backup"),
            tr("There are no backups of %1 yet.\n\nBackups are made when a project is opened, "
               "as it's saved (at most every 10 minutes) and on quit.").arg(name));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Restore backup — %1").arg(name));
    dialog.resize(560, 420);
    auto* layout = new QVBoxLayout(&dialog);
    auto* intro = new QLabel(tr("Pick the copy of %1 to go back to. Newest first.").arg(name), &dialog);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* tree = new QTreeWidget(&dialog);
    tree->setRootIsDecorated(false);
    tree->setUniformRowHeights(true);
    tree->setHeaderLabels({ tr("Saved"), tr("Scenes"), tr("Pads"), tr("Samples") });
    for (int i = 0; i < entries.size(); ++i) {
        const ProjectBackups::Entry& entry = entries[i];
        auto* item = new QTreeWidgetItem(tree);
        item->setText(0, QLocale().toString(entry.time, QStringLiteral("ddd d MMM yyyy, HH:mm:ss")));
        if (entry.readable) {
            item->setText(1, QString::number(entry.counts.scenes));
            item->setText(2, QString::number(entry.counts.pads));
            item->setText(3, QString::number(entry.counts.samples));
        } else {
            item->setText(1, tr("unreadable"));
            item->setDisabled(true);
        }
        for (int c = 1; c < 4; ++c) {
            item->setTextAlignment(c, Qt::AlignRight | Qt::AlignVCenter);
        }
        item->setData(0, Qt::UserRole, i);
    }
    tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < 4; ++c) {
        tree->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    }
    layout->addWidget(tree, 1);

    auto* buttons = new QDialogButtonBox(&dialog);
    QPushButton* restore = buttons->addButton(tr("Restore"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Cancel);
    QPushButton* showFolder = buttons->addButton(tr("Show folder"), QDialogButtonBox::ActionRole);
    restore->setEnabled(false);
    layout->addWidget(buttons);
    connect(tree, &QTreeWidget::currentItemChanged, &dialog, [restore](QTreeWidgetItem* current) {
        restore->setEnabled(current && !current->isDisabled());
    });
    connect(tree, &QTreeWidget::itemDoubleClicked, &dialog, [&dialog](QTreeWidgetItem* item) {
        if (item && !item->isDisabled()) {
            dialog.accept();
        }
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(showFolder, &QPushButton::clicked, &dialog, [folder]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
    });
    if (dialog.exec() != QDialog::Accepted || !tree->currentItem() || tree->currentItem()->isDisabled()) {
        return;
    }
    const ProjectBackups::Entry chosen = entries[tree->currentItem()->data(0, Qt::UserRole).toInt()];

    QString question = tr("Replace %1 with the copy from %2?\n\nThe project as it is now is backed up first, "
                          "so this can be undone from the same list.")
        .arg(name, QLocale().toString(chosen.time, QStringLiteral("ddd d MMM yyyy, HH:mm")));
    if (!m_config.autosave) {
        question += QStringLiteral("\n\n") + tr("Autosave is off: changes since you last saved will be lost.");
    }
    if (QMessageBox::question(this, tr("Restore backup"), question,
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) {
        return;
    }

    QFile source(chosen.path);
    if (!source.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Restore backup"), tr("Couldn't read the backup:\n%1").arg(QDir::toNativeSeparators(chosen.path)));
        return;
    }
    const QByteArray bytes = source.readAll();
    if (!QJsonDocument::fromJson(bytes).isObject()) {
        QMessageBox::warning(this, tr("Restore backup"), tr("That backup isn't a readable project file."));
        return;
    }

    // Keep what's there now: with autosave on, the current scenes are saved first.
    if (m_config.autosave) {
        m_saveDeclined = false;
        saveConfigTo(project, false);
    }
    ProjectBackups::backup(project, true);

    QSaveFile out(project);
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit()) {
        QMessageBox::warning(this, tr("Restore backup"),
            tr("Couldn't write %1:\n%2").arg(QDir::toNativeSeparators(project), out.errorString()));
        return;
    }
    loadConfigFrom(project);
    enableScene(std::clamp(m_config.activeSceneIdx, 0, std::max(0, static_cast<int>(m_scenes.size()) - 1)));
}

bool MainWindow::isExporting() const
{
    return m_export != nullptr;
}

// Called from tick(): once the copies are done, writes the project file.
void MainWindow::pollExport()
{
    if (!m_export || !m_export->finished.load()) {
        return;
    }
    if (m_exportThread.joinable()) {
        m_exportThread.join();
    }
    const std::shared_ptr<ExportState> state = std::move(m_export);
    m_export.reset();
    refreshLoadUi();

    const QString jsonPath = QDir::toNativeSeparators(state->jsonPath);
    QSaveFile file(state->jsonPath);
    if (!file.open(QIODevice::WriteOnly) || file.write(state->json) != state->json.size() || !file.commit()) {
        QMessageBox::warning(this, tr("Export project"),
            tr("The files were copied, but the project file couldn't be written:\n%1\n%2").arg(jsonPath, file.errorString()));
        return;
    }
    QStringList problems;
    if (!state->failed.isEmpty()) {
        problems << tr("These files couldn't be copied:") << state->failed;
    }
    if (!state->missing.isEmpty()) {
        problems << tr("These files weren't found, so the project still points at them:") << state->missing;
    }
    if (!problems.isEmpty()) {
        QMessageBox::warning(this, tr("Export project"),
            tr("Exported to %1, with problems.").arg(jsonPath) + QStringLiteral("\n\n") + problems.join(QLatin1Char('\n')));
    }
}

// Stops a running export and waits for its thread. Files already copied stay; the one being
// copied is dropped and the project file isn't written.
void MainWindow::cancelExport()
{
    if (!m_export) {
        return;
    }
    m_export->cancel.store(true);
    if (m_exportThread.joinable()) {
        m_exportThread.join();
    }
    m_export.reset();
    refreshLoadUi();
}

bool MainWindow::saveConfigTo(const QString& path, bool copyFiles, bool settingsOnly, ExportState* exportTo)
{
    if (path.isEmpty() || (m_loads && m_loads->isBusy())) {
        return false;
    }
    QString savePath = path;
    QFileInfo info(savePath);
    if (info.suffix().toLower() != QLatin1String("json")) {
        savePath = info.dir().filePath(info.completeBaseName() + QStringLiteral(".json"));
        info.setFile(savePath);
    }

    // Sample paths inside this folder are written relative to it, so the project folder
    // can be moved or renamed as a whole.
    const QString projectDir = info.absolutePath();

    // Copying files is only done as part of an export.
    copyFiles = copyFiles && exportTo;
    if (copyFiles) {
        exportTo->jsonPath = info.absoluteFilePath();
        exportTo->filesDir = info.dir().filePath(QStringLiteral("files"));
        if (!QDir().mkpath(exportTo->filesDir)) {
            qWarning() << "Failed to create" << exportTo->filesDir;
            return false;
        }
        // Samples already in the folder (exporting over an earlier export) stay where they
        // are, and keep their names from being given to other files.
        const QString filesAbs = QFileInfo(exportTo->filesDir).absoluteFilePath();
        for (Scene* scene : m_scenes) {
            for (Pad* pad : scene->pads) {
                for (const QString& samplePath : pad->samplePaths()) {
                    const QFileInfo sample(samplePath);
                    if (sample.isFile() && sample.absolutePath() == filesAbs) {
                        exportTo->usedNames.insert(sample.fileName().toLower());
                        exportTo->destBySource.insert(sample.absoluteFilePath(), sample.absoluteFilePath());
                    }
                }
            }
        }
    }

    QJsonObject root;
    QJsonObject global;
    global.insert(QStringLiteral("version"), ProjectFile::kVersion);
    global.insert(QStringLiteral("mainvolume"), m_config.masterVolume());
    global.insert(QStringLiteral("maxscenes"), m_scenes.size());
    global.insert(QStringLiteral("activesceneid"), m_config.activeSceneIdx);
    global.insert(QStringLiteral("autosave"), m_config.autosave);
    global.insert(QStringLiteral("scenelimit"), static_cast<int>(m_config.maxScenes));
    global.insert(QStringLiteral("crossfadetime"), m_config.crossfadeSeconds);
    global.insert(QStringLiteral("talkoverdepth"), m_config.talkOverDepthDb);
    global.insert(QStringLiteral("keyboardpads"), m_config.keyboardPads);
    global.insert(QStringLiteral("panictime"), m_config.panicSeconds);
    global.insert(QStringLiteral("gridwidth"), m_config.gridWidth);
    global.insert(QStringLiteral("gridheight"), m_config.gridHeight);
    global.insert(QStringLiteral("library"), m_config.defaultLibraryLocation);
    if (QFileInfo(savePath).absoluteFilePath() == QFileInfo(m_config.defaultSettingsPath()).absoluteFilePath()) {
        global.insert(QStringLiteral("loadlastsettings"), m_config.loadLastSettings);
        global.insert(QStringLiteral("lastsettingspath"), m_config.lastSettingsPath);
    }
    for (int which = 0; which < OpenALSoundPlayer::kEaxReverbCount; ++which) {
        const QString suffix = which == 0 ? QString() : QString::number(which + 1);
        global.insert(QStringLiteral("reverb") + suffix,
            QString::fromStdString(AudioDevice::reverbPresetId(AudioDevice::reverbPresetIndex(which))));
    }
    for (int which = 0; which < OpenALSoundPlayer::kConvolutionCount; ++which) {
        const QString suffix = which == 0 ? QString() : QString::number(which + 1);
        global.insert(QStringLiteral("convolutiongain") + suffix, static_cast<double>(AudioDevice::convolutionGain(which)));
        global.insert(QStringLiteral("convolutionir") + suffix, FsPath::toQString(AudioDevice::convolutionImpulsePath(which)));
    }
    global.insert(QStringLiteral("theme"), Theme::instance().idName());
    global.insert(QStringLiteral("themecolors"), Theme::instance().colorsJson());
    global.insert(QStringLiteral("themecolorsets"), Theme::instance().colorSetsJson());
    m_fileBrowser->saveSettings(global);

    if (settingsOnly) {
        // Everything but "global" comes from the file as it is, and so do the parts of
        // "global" that describe its scenes (count, active scene, main volume).
        QJsonObject existing;
        QFile current(savePath);
        if (current.exists()) {
            // Never replace scenes we couldn't read with nothing.
            if (!current.open(QIODevice::ReadOnly)) {
                qWarning() << "Settings not saved: couldn't read" << savePath << current.errorString();
                return false;
            }
            QJsonParseError parseError;
            const QJsonDocument doc = QJsonDocument::fromJson(current.readAll(), &parseError);
            if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
                qWarning() << "Settings not saved: couldn't parse" << savePath << parseError.errorString();
                return false;
            }
            existing = doc.object();
        }
        const QJsonObject oldGlobal = existing.value(QStringLiteral("global")).toObject();
        for (const QString& key : { QStringLiteral("maxscenes"), QStringLiteral("activesceneid"), QStringLiteral("mainvolume") }) {
            if (oldGlobal.contains(key)) {
                global.insert(key, oldGlobal.value(key));
            } else {
                global.remove(key);
            }
        }
        root = existing;
        root.insert(QStringLiteral("global"), global);
    } else {
        root.insert(QStringLiteral("global"), global);
    }

    for (int i = 0; i < m_scenes.size() && !settingsOnly; ++i) {
        Scene* scene = m_scenes[i];
        QJsonObject sceneObj;
        sceneObj.insert(QStringLiteral("id"), scene->id);
        const QString sceneName = scene->row() ? scene->row()->sceneName() : scene->name;
        scene->name = sceneName;
        sceneObj.insert(QStringLiteral("name"), sceneName);
        sceneObj.insert(QStringLiteral("activesound"), scene->activeSoundIdx);
        scene->writeSettings(sceneObj);
        for (Pad* pad : scene->pads) {
            pad->saveToJson(sceneObj);
            const QString padKey = QString("%1-%2").arg(scene->id).arg(pad->padId());
            QJsonObject padObj = sceneObj.value(padKey).toObject();
            if (padObj.isEmpty()) {
                continue;
            }
            QJsonObject samples = padObj.value(QStringLiteral("samples")).toObject();
            for (auto it = samples.begin(); it != samples.end(); ++it) {
                QJsonObject sample = it.value().toObject();
                QString path = sample.value(QStringLiteral("path")).toString();
                if (copyFiles && !path.isEmpty()) {
                    const QFileInfo src(path);
                    if (src.isFile()) {
                        path = exportTo->destinationFor(src);
                    } else if (!exportTo->missing.contains(QDir::toNativeSeparators(path))) {
                        exportTo->missing << QDir::toNativeSeparators(path);
                    }
                }
                sample.insert(QStringLiteral("path"), ProjectFile::relativePath(path, projectDir));
                it.value() = sample;
            }
            padObj.insert(QStringLiteral("samples"), samples);
            sceneObj.insert(padKey, padObj);
        }
        root.insert(QString("scene%1").arg(i), sceneObj);
    }

    const QString dirPath = QFileInfo(savePath).absolutePath();
    if (!QDir().mkpath(dirPath)) {
        qWarning() << "Failed to create settings directory" << dirPath;
        return false;
    }

    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (exportTo) {
        // Written by pollExport once the files are copied.
        exportTo->json = bytes;
        return true;
    }
    if (!settingsOnly && QFile::exists(savePath)) {
        // Never quietly replace a project with one that has far fewer pads in it.
        bool readable = false;
        const ProjectBackups::Counts before = ProjectBackups::countFile(savePath, &readable);
        const ProjectBackups::Counts after = ProjectBackups::count(root);
        if (readable && before.pads >= 4 && after.pads * 2 < before.pads) {
            ProjectBackups::backup(savePath, true); // keep the fuller one whatever the answer
            const auto answer = QMessageBox::question(this, tr("Save project"),
                tr("%1 has %2 pads with sounds. Saving now would leave %3.\n\n"
                   "Save anyway? The file as it is now has been backed up (File > Restore Backup...).")
                    .arg(QFileInfo(savePath).fileName()).arg(before.pads).arg(after.pads),
                QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Cancel);
            if (answer != QMessageBox::Save) {
                m_saveDeclined = true;
                return false;
            }
        } else {
            ProjectBackups::backup(savePath, false);
        }
    }
    QSaveFile file(savePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Failed to open settings file" << savePath << file.errorString();
        return false;
    }
    if (file.write(bytes) != bytes.size()) {
        qWarning() << "Failed to write settings file" << savePath << file.errorString();
        return false;
    }
    if (!file.commit()) {
        qWarning() << "Failed to commit settings file" << savePath << file.errorString();
        return false;
    }
    return true;
}

void MainWindow::importScenes()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Import scenes"),
        m_config.defaultSettingsPath(), tr("JSON (*.json)"));
    if (!path.isEmpty()) {
        importScenesFrom(path);
    }
}

// Appends every scene in another settings file after the current ones. The file's global
// options (volume, theme, window, reverb, grid size...) are ignored. Pads keep their row and
// column; if the file's grid is larger than this one, its extra rows and columns are dropped.
void MainWindow::importScenesFrom(const QString& path)
{
    m_undo->clear(); // new scene ids could meet old steps

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Import scenes"), tr("Could not open %1.").arg(QDir::toNativeSeparators(path)));
        return;
    }
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (doc.isNull() || !doc.isObject()) {
        QMessageBox::warning(this, tr("Import scenes"),
            tr("%1 is not a Feedra settings file:\n%2").arg(QFileInfo(path).fileName(), error.errorString()));
        return;
    }
    const QJsonObject root = doc.object();
    const QJsonObject global = root.value(QStringLiteral("global")).toObject();
    // Only the grid shape is read from the file's global options, to place its pads.
    const int fromCols = std::max(1, global.value(QStringLiteral("gridwidth")).toInt(m_config.gridWidth));
    const int toCols = std::max(1, m_config.gridWidth);
    const int toRows = std::max(1, m_config.gridHeight);

    // Gather scenes ("scene<N>", in N order) and every pad object ("<sceneId>-<padId>").
    // Older files keep a scene's pads under a different "scene<N>" entry, so pads are looked
    // up across the whole file rather than inside each scene's own object.
    QVector<QPair<int, QJsonObject>> incoming;
    QHash<QString, QJsonObject> pads;
    static const QRegularExpression sceneKey(QStringLiteral("^scene(\\d+)$"));
    static const QRegularExpression padKey(QStringLiteral("^(\\d+)-(\\d+)$"));
    for (auto it = root.begin(); it != root.end(); ++it) {
        const QRegularExpressionMatch match = sceneKey.match(it.key());
        if (!match.hasMatch() || !it.value().isObject()) {
            continue;
        }
        const QJsonObject sceneObj = it.value().toObject();
        for (auto p = sceneObj.begin(); p != sceneObj.end(); ++p) {
            if (p.value().isObject() && padKey.match(p.key()).hasMatch()) {
                pads.insert(p.key(), p.value().toObject());
            }
        }
        if (sceneObj.contains(QStringLiteral("id"))) {
            incoming.append({match.captured(1).toInt(), sceneObj});
        }
    }
    std::sort(incoming.begin(), incoming.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    if (incoming.isEmpty()) {
        QMessageBox::information(this, tr("Import scenes"), tr("%1 has no scenes to import.").arg(QFileInfo(path).fileName()));
        return;
    }

    // Maps a pad index in the file's grid to this grid, or -1 when it falls outside.
    auto mapPad = [&](int index) {
        const int row = index / fromCols;
        const int col = index % fromCols;
        return (col < toCols && row < toRows) ? row * toCols + col : -1;
    };

    int imported = 0;
    int skippedScenes = 0;
    int droppedPads = 0;
    for (const auto& entry : incoming) {
        const QJsonObject& sceneObj = entry.second;
        if (static_cast<unsigned int>(m_scenes.size()) >= m_config.maxScenes) {
            ++skippedScenes;
            continue;
        }
        const int oldId = sceneObj.value(QStringLiteral("id")).toInt();
        const int newId = nextSceneId();
        const QString prefix = QStringLiteral("%1-").arg(oldId);

        QJsonObject remapped;
        for (auto p = pads.cbegin(); p != pads.cend(); ++p) {
            if (!p.key().startsWith(prefix)) {
                continue;
            }
            const int to = mapPad(p.key().mid(prefix.size()).toInt());
            if (to < 0) {
                ++droppedPads;
                continue;
            }
            remapped.insert(QStringLiteral("%1-%2").arg(newId).arg(to), p.value());
        }
        QJsonObject padRoot;
        padRoot.insert(QStringLiteral("scene"), remapped);

        auto* scene = new Scene(&m_config, newId, sceneObj.value(QStringLiteral("name")).toString(),
            m_padStack, m_sceneListHost, this);
        scene->activeSoundIdx = std::max(0, mapPad(sceneObj.value(QStringLiteral("activesound")).toInt(0)));
        scene->readSettings(sceneObj);
        connectScene(scene);
        for (Pad* pad : scene->pads) {
            pad->loadFromJson(padRoot, QFileInfo(path).absolutePath());
            m_reportMissingAfterLoad = true;
        }
        m_scenes.push_back(scene);
        m_padStack->addWidget(scene->grid());
        m_sceneListLayout->insertWidget(m_sceneListLayout->count() - 1, scene->row());
        ++imported;
    }
    m_addScene->setEnabled(static_cast<unsigned int>(m_scenes.size()) < m_config.maxScenes);
    refreshLoadUi();

    if (skippedScenes > 0 || droppedPads > 0) {
        QStringList notes;
        const QString file = QFileInfo(path).fileName();
        notes << (imported == 1 ? tr("Imported 1 scene from %1.").arg(file)
                                : tr("Imported %1 scenes from %2.").arg(imported).arg(file));
        if (skippedScenes > 0) {
            notes << (skippedScenes == 1 ? tr("1 scene was left out") : tr("%1 scenes were left out").arg(skippedScenes))
                     + tr(" because the scene limit (%1) was reached. You can raise it in Settings.").arg(m_config.maxScenes);
        }
        if (droppedPads > 0) {
            notes << (droppedPads == 1 ? tr("1 pad was dropped") : tr("%1 pads were dropped").arg(droppedPads))
                     + (droppedPads == 1 ? tr(" because it falls outside this %1-column, %2-row pad grid.")
                                       : tr(" because they fall outside this %1-column, %2-row pad grid.")).arg(toCols).arg(toRows);
        }
        QMessageBox::information(this, tr("Import scenes"), notes.join(QStringLiteral("\n\n")));
    }
}

void MainWindow::loadConfig()
{
    // Start in the folder of the settings file that's open now (or data/settings).
    QString startDir = QFileInfo(currentSettingsFilePath()).absolutePath();
    if (!QDir(startDir).exists()) {
        startDir = QFileInfo(m_config.defaultSettingsPath()).absolutePath();
    }
    const QString path = QFileDialog::getOpenFileName(this, tr("Load Feedra scenes"),
        startDir, tr("JSON (*.json)"));
    if (!path.isEmpty()) {
        loadConfigFrom(path);
        enableScene(0);
    }
}

void MainWindow::loadConfigFrom(const QString& path)
{
    if (!m_config.loadJson(path)) {
        return;
    }
    // The steps were about the scenes that are going.
    m_undo->clear();
    m_pendingEdits.clear();
    // A copy as it was when opened, whatever happens to it later.
    ProjectBackups::backup(path, true);
    m_config.settingsPath = QFileInfo(path).absoluteFilePath();
    refreshSettingsPathLabel();
    const QJsonObject root = m_config.json();
    const QString projectDir = QFileInfo(path).absolutePath();
    m_reportMissingAfterLoad = true;
    const QJsonObject global = root.value(QStringLiteral("global")).toObject();
    const int version = global.value(QStringLiteral("version")).toInt(1);
    if (version > ProjectFile::kVersion) {
        qWarning() << path << "was saved by a newer Feedra (format" << version << "), this one reads"
                   << ProjectFile::kVersion << "- some settings may be lost if it's saved here";
    }
    applyAppSettings(global);
    m_mainVolume->setValue(VolumeDb::toSliderClamped(
        static_cast<float>(global.value(QStringLiteral("mainvolume")).toDouble(1.0)),
        VolumeDb::kFloorDb,
        VolumeDb::kUnityDb));
    m_config.setMasterVolume(VolumeDb::toLinearMuted(VolumeDb::fromSlider(m_mainVolume->value(), VolumeDb::kFloorDb)));
    m_config.activeSceneIdx = global.value(QStringLiteral("activesceneid")).toInt();

    for (Scene* scene : m_scenes) {
        m_padStack->removeWidget(scene->grid());
    }
    qDeleteAll(m_scenes);
    m_scenes.clear();

    const int maxScenes = global.value(QStringLiteral("maxscenes")).toInt();
    for (int i = 0; i < maxScenes; ++i) {
        const QJsonObject sceneObj = root.value(QString("scene%1").arg(i)).toObject();
        if (sceneObj.value(QStringLiteral("id")).isUndefined() && sceneObj.isEmpty()) {
            continue;
        }
        if (!sceneObj.contains(QStringLiteral("id"))) {
            continue;
        }
        const int id = sceneObj.value(QStringLiteral("id")).toInt(i);
        const QString name = sceneObj.value(QStringLiteral("name")).toString();
        auto* scene = new Scene(&m_config, id, name, m_padStack, m_sceneListHost, this);
        scene->activeSoundIdx = sceneObj.value(QStringLiteral("activesound")).toInt(0);
        scene->readSettings(sceneObj);
        connectScene(scene);
        for (Pad* pad : scene->pads) {
            pad->loadFromJson(root, projectDir);
        }
        m_scenes.push_back(scene);
        m_padStack->addWidget(scene->grid());
        m_sceneListLayout->insertWidget(m_sceneListLayout->count() - 1, scene->row());
    }
    m_addScene->setEnabled(static_cast<unsigned int>(m_scenes.size()) < m_config.maxScenes);
}

void MainWindow::saveLocalSettings()
{
    QJsonObject midi = m_controls.toJson();
    midi.insert(QStringLiteral("input"), m_midiPort);
    m_local.insert(QStringLiteral("midi"), midi);
    m_local.insert(QStringLiteral("outputdevice"), m_config.outputDevice);
    m_local.insert(QStringLiteral("previewdevice"), m_config.previewDevice);
    saveWindowLayout(m_local);
    m_config.saveLocalSettings(m_local);
}

void MainWindow::saveWindowLayout(QJsonObject& global) const
{
    // In the scenes strip, save the window it came from so the next launch opens full size.
    const bool strip = m_scenesFocus && !m_expandedGeometry.isEmpty();
    const QRect box = strip ? m_expandedRect
        : (isMaximized() || isMinimized()) ? normalGeometry() : geometry();
    global.insert(QStringLiteral("windowx"), box.x());
    global.insert(QStringLiteral("windowy"), box.y());
    global.insert(QStringLiteral("windowwidth"), box.width());
    global.insert(QStringLiteral("windowheight"), box.height());
    global.insert(QStringLiteral("windowmaximized"), strip ? m_expandedMaximized : isMaximized());
    global.insert(QStringLiteral("windowgeometry"),
        QString::fromLatin1((strip ? m_expandedGeometry : saveGeometry()).toBase64()));
    global.insert(QStringLiteral("bottomcollapsed"), m_bottom->isCollapsed());
    global.insert(QStringLiteral("bottomtab"), m_bottom->currentTab());
}

void MainWindow::restoreWindowLayout(const QJsonObject& global)
{
    const QByteArray stored = QByteArray::fromBase64(
        global.value(QStringLiteral("windowgeometry")).toString().toLatin1());
    bool restored = false;
    if (!stored.isEmpty()) {
        restored = restoreGeometry(stored);
    }
    if (!restored) {
        const int w = global.value(QStringLiteral("windowwidth")).toInt();
        const int h = global.value(QStringLiteral("windowheight")).toInt();
        if (w >= 400 && h >= 300) {
            resize(w, h);
        }
        if (global.contains(QStringLiteral("windowx")) && global.contains(QStringLiteral("windowy"))) {
            move(global.value(QStringLiteral("windowx")).toInt(),
                global.value(QStringLiteral("windowy")).toInt());
        }
        if (global.value(QStringLiteral("windowmaximized")).toBool()) {
            setWindowState(windowState() | Qt::WindowMaximized);
        }
    }
    if (windowState() & Qt::WindowMinimized) {
        setWindowState(windowState() & ~Qt::WindowMinimized);
    }

    if (global.contains(QStringLiteral("bottomtab"))) {
        const int tab = std::clamp(global.value(QStringLiteral("bottomtab")).toInt(0), 0, 2);
        setBottomTab(tab);
    }
    if (global.contains(QStringLiteral("bottomcollapsed"))) {
        setBottomCollapsed(global.value(QStringLiteral("bottomcollapsed")).toBool(), false);
    }
}

void MainWindow::clearActivePad()
{
    if (auto* pad = activePad()) {
        const PadSnapshot before = snapshotPads({ pad });
        pad->clearPad();
        recordPadEdit(before, tr("Clear pad"));
        updateMainControls();
        if (m_sidebar == SidebarView::Editor) {
            refreshEditorPage();
        }
    }
}

void MainWindow::copyActivePad()
{
    Pad* pad = activePad();
    if (!pad || pad->isLoading() || !pad->isLoaded()) {
        return;
    }
    const Pad::PadClip clip = pad->clip();
    if (clip.valid) {
        m_padClip = clip;
    }
}

void MainWindow::cutActivePad()
{
    Pad* pad = activePad();
    if (!pad || pad->isLoading() || !pad->isLoaded()) {
        return;
    }
    const Pad::PadClip clip = pad->clip();
    if (!clip.valid) {
        return;
    }
    m_padClip = clip;
    const PadSnapshot before = snapshotPads({ pad });
    pad->clearPad();
    recordPadEdit(before, tr("Cut pad"));
    updateMainControls();
    if (m_sidebar == SidebarView::Editor) {
        refreshEditorPage();
    }
}

void MainWindow::pasteActivePad()
{
    Pad* pad = activePad();
    if (!pad || !m_padClip.valid) {
        return;
    }
    const PadSnapshot before = snapshotPads({ pad });
    pad->pasteClip(m_padClip);
    recordPadEdit(before, tr("Paste pad"));
    updateMainControls();
    if (m_sidebar == SidebarView::Editor) {
        refreshEditorPage();
    }
}

bool MainWindow::handlePadClipKey(QKeyEvent* event)
{
    if (m_page != Page::Main || event->isAutoRepeat()) {
        return false;
    }
    if ((event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier))
        != Qt::ControlModifier) {
        return false;
    }
    if (event->key() != Qt::Key_C && event->key() != Qt::Key_X && event->key() != Qt::Key_V) {
        return false;
    }
    QWidget* focus = QApplication::focusWidget();
    if (auto* edit = qobject_cast<QLineEdit*>(focus); edit && !edit->isReadOnly()) {
        return false;
    }
    if (qobject_cast<QAbstractSpinBox*>(focus) || qobject_cast<QComboBox*>(focus)) {
        return false;
    }

    if (event->key() == Qt::Key_C) {
        copyActivePad();
    } else if (event->key() == Qt::Key_X) {
        cutActivePad();
    } else {
        pasteActivePad();
    }
    return true;
}

void MainWindow::copyPad(int fromIdx, int toIdx)
{
    Scene* scene = activeScene();
    if (!scene) {
        return;
    }
    Pad* from = scene->padAt(fromIdx);
    Pad* to = scene->padAt(toIdx);
    if (!from || !to || from == to) {
        return;
    }
    const PadSnapshot before = snapshotPads({ to });
    to->clearPad();
    to->copyFrom(*from);
    if (m_recordSteps) {
        recordPadEdit(before, tr("Copy pad"));
    }
    m_config.activeSoundIdx = toIdx;
    scene->activeSoundIdx = toIdx;
    updateMainControls();
}

// Dragging a pad onto another moves it: the target takes the pad's sounds and settings
// and the source is cleared.
void MainWindow::movePad(int fromIdx, int toIdx)
{
    Scene* scene = activeScene();
    if (!scene) {
        return;
    }
    Pad* from = scene->padAt(fromIdx);
    Pad* to = scene->padAt(toIdx);
    if (!from || !to || from == to) {
        return;
    }
    // One step for both pads (the copy inside doesn't record its own).
    const PadSnapshot before = snapshotPads({ from, to });
    m_recordSteps = false;
    copyPad(fromIdx, toIdx); // reads the source's samples before it is cleared
    m_recordSteps = true;
    from->clearPad();
    recordPadEdit(before, tr("Move pad"));
    if (m_sidebar == SidebarView::Editor) {
        refreshEditorPage();
    }
}

// Dragging a pad onto one that already has sounds adds the dragged pad's samples after the
// target's own, with their editor settings. The target's name, volume, delay, repeat, sends and
// so on stay as they are. A plain drag then clears the dragged pad; Ctrl/Alt keeps it.
void MainWindow::mergePad(int fromIdx, int toIdx, bool keepSource)
{
    Scene* scene = activeScene();
    if (!scene) {
        return;
    }
    Pad* from = scene->padAt(fromIdx);
    Pad* to = scene->padAt(toIdx);
    if (!from || !to || from == to) {
        return;
    }
    const Pad::PadClip clip = from->clip();
    if (!clip.valid) {
        return; // an empty pad adds nothing, and the target is left alone
    }
    const PadSnapshot before = snapshotPads({ from, to });
    to->appendClip(clip);
    if (!keepSource) {
        from->clearPad();
    }
    recordPadEdit(before, tr("Merge pads"));
    m_config.activeSoundIdx = toIdx;
    scene->activeSoundIdx = toIdx;
    updateMainControls();
    if (m_sidebar == SidebarView::Editor) {
        refreshEditorPage();
    }
}

void MainWindow::clearActiveSample()
{
    auto* pad = activePad();
    if (!pad || pad->soundPlayer().player.empty()) {
        return;
    }
    const PadSnapshot before = snapshotPads({ pad });
    pad->removeSampleAt(m_config.activeSampleIdx);
    recordPadEdit(before, tr("Remove sample"));
    m_config.activeSampleIdx = std::max(0, m_config.activeSampleIdx - 1);
    refreshEditorPage();
}

void MainWindow::drainLoads(int budgetMs)
{
    if (!m_loads) {
        return;
    }
    m_loads->drain(budgetMs, [this](SampleLoadResult result) {
        if (Pad* pad = findPad(result.job.sceneId, result.job.padId)) {
            pad->submitDecoded(result.job.sampleIndex, result.job.generation, std::move(result.audio));
        }
    });
}

void MainWindow::refreshLoadUi()
{
    const bool loading = m_loads && m_loads->isBusy();
    const bool exporting = isExporting();
    const bool busy = loading || exporting;
    if (m_saveAction) {
        m_saveAction->setEnabled(!busy);
    }
    if (m_exportAction) {
        m_exportAction->setEnabled(!busy);
    }
    if (m_restoreAction) {
        m_restoreAction->setEnabled(!busy);
    }
    if (!m_loadBar || !m_loadLabel) {
        return;
    }
    if (!busy) {
        m_loadBar->hide();
        m_loadLabel->hide();
        if (m_settingsPathLabel) {
            // No room in the scenes strip; the full path is in the normal layout.
            m_settingsPathLabel->setVisible(!m_scenesFocus);
        }
        if (windowTitle() != QStringLiteral("Feedra")) {
            setWindowTitle(QStringLiteral("Feedra"));
        }
        return;
    }
    if (m_settingsPathLabel) {
        m_settingsPathLabel->hide();
    }
    const QString title = loading ? tr("Feedra — Loading") : tr("Feedra — Exporting");
    if (windowTitle() != title) {
        setWindowTitle(title);
    }
    if (m_scenesFocus) {
        // No room in the strip's header; the window title still says it's loading.
        m_loadBar->hide();
        m_loadLabel->hide();
        return;
    }
    if (!loading) {
        // Export: the bar follows bytes copied, the label counts files.
        const qint64 totalBytes = std::max<qint64>(1, m_export->totalBytes);
        const qint64 doneBytes = std::clamp<qint64>(m_export->doneBytes.load(), 0, totalBytes);
        m_loadBar->setRange(0, 1000);
        m_loadBar->setValue(static_cast<int>(doneBytes * 1000 / totalBytes));
        m_loadBar->show();
        const int files = static_cast<int>(m_export->copies.size());
        m_loadLabel->setText(tr("Exporting %1 / %2").arg(std::min(m_export->doneFiles.load() + 1, files)).arg(files));
        m_loadLabel->show();
        return;
    }
    const int total = std::max(1, m_loads->total());
    m_loadBar->setRange(0, total);
    m_loadBar->setValue(std::clamp(m_loads->settled(), 0, total));
    m_loadBar->show();
    m_loadLabel->setText(tr("Loading %1 / %2").arg(m_loads->settled()).arg(m_loads->total()));
    m_loadLabel->show();
}

void MainWindow::waitForLoads()
{
    while (m_loads && m_loads->isBusy()) {
        const int before = m_loads->settled();
        drainLoads(30);
        refreshLoadUi();
        if (m_loads->isBusy() && m_loads->settled() == before) {
            QThread::msleep(2);
        }
    }
    refreshLoadUi();
}

Pad* MainWindow::findPad(int sceneId, int padId) const
{
    for (Scene* scene : m_scenes) {
        if (scene->id == sceneId) {
            return scene->padAt(padId);
        }
    }
    return nullptr;
}

void MainWindow::tick()
{
    drainLoads(8);
    pollExport();
    reportMissingSamples();
    refreshLoadUi();
    OpenALSoundPlayer::updateAll();
    m_fileBrowser->tick();
    // Before the scenes update, so every pad takes the new level this tick.
    m_duck.setDepthDb(static_cast<float>(m_config.talkOverDepthDb));
    m_config.setMasterFade(m_duck.update(m_clock.elapsed()));
    refreshTalkButton();
    for (Scene* scene : m_scenes) {
        if (scene->selectRequested) {
            scene->selectRequested = false;
            for (int i = 0; i < m_scenes.size(); ++i) {
                if (m_scenes[i] == scene) {
                    enableScene(i);
                    break;
                }
            }
        }
        scene->update();
    }
    refreshIntensityControl();
    Pad* pad = syncPanelPad();
    if (m_page == Page::Main) {
        // The selected sample follows the pad as it moves on to another sample, in every
        // view, so the Editor and the Waveform tab show the one playing.
        if (pad) {
            const SoundPlayer& player = pad->soundPlayer();
            const int cur = player.getCurSound();
            if (pad != m_followedPad) {
                m_followedPad = pad;
                m_followedSound = -1;
            }
            if (!player.isPlaying()) {
                m_followedSound = -1;
            } else if (cur != m_followedSound && cur >= 0 && cur < static_cast<int>(player.player.size())) {
                m_followedSound = cur;
                if (cur != m_config.activeSampleIdx) {
                    m_editor->selectSample(cur);
                }
            }
        }
        m_bottom->tick();
        if (m_sidebar == SidebarView::Editor) {
            m_editor->tick();
        }
    }
}

bool MainWindow::moveOutputTo(const QString& device)
{
    if (device.isEmpty()) {
        return false;
    }
    if (device == m_curDevice && AudioDevice::isConnected()) {
        return true;
    }
    const int result = AudioDevice::reopen(device.toUtf8().constData());
    if (result == 0) {
        qInfo() << "Audio output moved to" << device;
        m_curDevice = device;
        m_failedDevice.clear();
        return true;
    }
    // Logged once per device: the default is tried again every second.
    if (device != m_failedDevice) {
        qWarning() << "Couldn't move audio output to" << device << "(code" << result << ")";
        m_failedDevice = device;
    }
    return false;
}

void MainWindow::applyOutputDevice()
{
    const QString chosen = m_config.outputDevice;
    const QString fallback = QString::fromStdString(AudioDevice::defaultDeviceName());
    m_lastDefaultDevice = fallback;
    if (chosen.isEmpty()) {
        moveOutputTo(fallback);
        m_settingsPage->setDeviceStatus(QString());
        return;
    }
    if (moveOutputTo(chosen)) {
        m_settingsPage->setDeviceStatus(QString());
        return;
    }
    // Kept as the choice, and tried again whenever the system's devices change.
    moveOutputTo(fallback);
    m_settingsPage->setDeviceStatus(tr("%1 isn't available: playing on the system default until it is.").arg(chosen));
}

void MainWindow::applyPreviewDevice()
{
    const QString chosen = m_config.previewDevice;
    if (QString::fromStdString(CueOutput::deviceName()) == chosen && (CueOutput::isOpen() || chosen.isEmpty())) {
        return;
    }
    // A preview playing on one output isn't moved to the other: stop it.
    if (m_fileBrowser) {
        m_fileBrowser->stopPreview();
    }
    if (chosen.isEmpty()) {
        CueOutput::close();
        m_settingsPage->setDeviceStatus(QString());
        return;
    }
    if (CueOutput::open(chosen.toStdString())) {
        m_settingsPage->setDeviceStatus(QString());
    } else {
        m_settingsPage->setDeviceStatus(tr("Couldn't open %1 for previews: they play on the main output.").arg(chosen));
    }
}

void MainWindow::checkAudioDevice()
{
    // A chosen MIDI input that wasn't there: look again every few seconds.
    if (!m_midiPort.isEmpty() && !m_midi->isOpen() && ++m_midiRetry >= 3) {
        m_midiRetry = 0;
        openMidiInput();
    }
    // No device enumeration here: this runs every second on the UI thread, and listing
    // devices can take a while on PipeWire/Pulse.
    const QString def = QString::fromStdString(AudioDevice::defaultDeviceName());
    const QString chosen = m_config.outputDevice;
    if (chosen.isEmpty()) {
        // Follows the system's default output.
        if (!def.isEmpty() && def != m_curDevice) {
            moveOutputTo(def);
        }
        return;
    }
    // A chosen device. If it went away, play on the default meanwhile.
    if (m_curDevice == chosen && !AudioDevice::isConnected() && !def.isEmpty()) {
        qWarning() << "Audio output" << chosen << "disconnected";
        if (moveOutputTo(def)) {
            m_settingsPage->setDeviceStatus(tr("%1 was disconnected: playing on the system default until it's back.").arg(chosen));
        }
        m_lastDefaultDevice = def;
        return;
    }
    // A change of default is the sign that devices came or went: try the chosen one again.
    if (m_curDevice != chosen && def != m_lastDefaultDevice) {
        m_lastDefaultDevice = def;
        if (moveOutputTo(chosen)) {
            m_settingsPage->setDeviceStatus(QString());
        }
    }
}

void MainWindow::onPadClicked(int sceneId, int padId)
{
    Q_UNUSED(sceneId);
    const bool padChanged = m_config.activeSoundIdx != padId;
    m_config.activeSoundIdx = padId;
    if (Scene* scene = activeScene()) {
        scene->activeSoundIdx = padId;
    }
    updateMainControls();
    if (m_sidebar == SidebarView::Editor && padChanged) {
        setSidebarView(SidebarView::Editor);
    }
}

Pad* MainWindow::activePad() const
{
    Scene* scene = activeScene();
    return scene ? scene->padAt(m_config.activeSoundIdx) : nullptr;
}

Scene* MainWindow::activeScene() const
{
    if (m_config.activeSceneIdx < 0 || m_config.activeSceneIdx >= m_scenes.size()) {
        return nullptr;
    }
    return m_scenes[m_config.activeSceneIdx];
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    if (m_holdGridPaint) {
        // The window has its new size: settle every layout at it, then paint once.
        settleLayouts();
        releaseGridPaint();
    }
}

// Hiding a widget only posts a layout request, so a resize that follows would still lay
// out with the old panel height. Push the change up through the layouts right away.
void MainWindow::settleLayouts()
{
    if (m_bottom && m_bottom->layout()) {
        m_bottom->layout()->invalidate();
        m_bottom->layout()->activate();
        m_bottom->updateGeometry();
    }
    if (QLayout* layout = centralWidget()->layout()) {
        layout->invalidate();
        layout->activate();
    }
}

void MainWindow::releaseGridPaint()
{
    if (!m_holdGridPaint) {
        return;
    }
    m_holdGridPaint = false;
    centralWidget()->setUpdatesEnabled(true);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (isExporting()) {
        const auto answer = QMessageBox::question(this, tr("Export project"),
            tr("The project is still being exported. Stop the export and quit?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        cancelExport();
    }
    waitForLoads();
    saveOnExit();
    QMainWindow::closeEvent(event);
}

namespace {
// The line edit that has focus, or the spin box that owns it. Spin boxes focus an inner line edit.
QWidget* focusedTextField(QWidget* focus, QLineEdit** editOut)
{
    *editOut = qobject_cast<QLineEdit*>(focus);
    if (*editOut) {
        if (auto* spin = qobject_cast<QAbstractSpinBox*>((*editOut)->parentWidget())) {
            return spin;
        }
        return *editOut;
    }
    if (auto* spin = qobject_cast<QAbstractSpinBox*>(focus)) {
        *editOut = spin->findChild<QLineEdit*>(QString(), Qt::FindDirectChildrenOnly);
        return spin;
    }
    return nullptr;
}
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    // Most of the window does not accept focus, so a click there would leave a text field selected.
    if (event->type() == QEvent::MouseButtonPress) {
        auto* clicked = qobject_cast<QWidget*>(watched);
        QWidget* focus = QApplication::focusWidget();
        QLineEdit* edit = nullptr;
        QWidget* field = focus && focus->window() == this ? focusedTextField(focus, &edit) : nullptr;
        // Tested by position because a press the field ignores is delivered again to its parents.
        const QPoint globalPos = static_cast<QMouseEvent*>(event)->globalPosition().toPoint();
        const bool insideField = field && field->rect().contains(field->mapFromGlobal(globalPos));
        QWidget* popup = QApplication::activePopupWidget();
        const bool insidePopup = popup && clicked && (clicked == popup || popup->isAncestorOf(clicked));
        if (field && !insideField && !insidePopup) {
            if (edit) {
                edit->deselect();
            }
            // clearFocus() on the inner line edit hands focus straight back to the spin box.
            setFocus(Qt::MouseFocusReason);
        }
    }
    if (event->type() == QEvent::KeyPress && watched->isWidgetType()
        && static_cast<QWidget*>(watched)->window() == this) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (handleLiveKey(key) || handlePadClipKey(key) || handleReorderKey(key)) {
            return true;
        }
    }
    if (event->type() == QEvent::KeyRelease && watched->isWidgetType()
        && static_cast<QWidget*>(watched)->window() == this) {
        if (handleLiveKey(static_cast<QKeyEvent*>(event))) {
            return true;
        }
    }
    // A held Space whose release goes to another window would hold the duck for ever.
    if (watched == this && event->type() == QEvent::WindowDeactivate) {
        m_duck.setHeld(Duck::Key, false);
    }
    return QMainWindow::eventFilter(watched, event);
}

bool MainWindow::keyboardIsBusy() const
{
    QWidget* focus = QApplication::focusWidget();
    if (auto* edit = qobject_cast<QLineEdit*>(focus); edit && !edit->isReadOnly()) {
        return true;
    }
    if (qobject_cast<QAbstractSpinBox*>(focus) || qobject_cast<QComboBox*>(focus)
        || qobject_cast<QAbstractItemView*>(focus)) {
        return true;
    }
    return QApplication::activePopupWidget() != nullptr;
}

bool MainWindow::handleLiveKey(QKeyEvent* event)
{
    if (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
        return false;
    }
    const bool press = event->type() == QEvent::KeyPress;
    if (event->key() == Qt::Key_Space) {
        if (event->isAutoRepeat()) {
            return m_duck.isHeldBy(Duck::Key); // swallow the repeats of a held Space
        }
        if (!press) {
            if (m_duck.isHeldBy(Duck::Key)) {
                m_duck.setHeld(Duck::Key, false);
                return true;
            }
            return false;
        }
        // The Files tab previews with Space.
        if (m_page != Page::Main || m_sidebar == SidebarView::Files || keyboardIsBusy()) {
            return false;
        }
        m_duck.setHeld(Duck::Key, true);
        return true;
    }
    // The pads, by keyboard row (KeyLayout). Shift is left for later use.
    if (m_config.keyboardPads && !(event->modifiers() & Qt::ShiftModifier)) {
        Scene* scene = activeScene();
        const int index = scene ? KeyLayout::padForKey(event->key(), scene->columns(), scene->rows()) : -1;
        if (index >= 0) {
            if (m_page != Page::Main || keyboardIsBusy()) {
                return false;
            }
            if (press && !event->isAutoRepeat()) {
                if (Pad* pad = scene->padAt(index)) {
                    pad->togglePlay();
                }
            }
            return true; // a held key's repeats and its release are taken too
        }
    }
    if (event->key() == Qt::Key_Escape && press && !event->isAutoRepeat()) {
        if (m_controls.isLearning()) {
            cancelMidiLearn();
            return true;
        }
        if (m_page != Page::Main || keyboardIsBusy()) {
            return false;
        }
        panic();
        return true;
    }
    return false;
}

void MainWindow::panic()
{
    const qint64 now = m_clock.elapsed();
    const bool again = now < m_panicUntilMs;
    const int fadeMs = static_cast<int>(std::lround(std::clamp(m_config.panicSeconds, 0.0, 10.0) * 1000.0));
    for (Scene* scene : m_scenes) {
        if (again || fadeMs <= 0) {
            scene->stopImmediate();
        } else if (scene->isPlaying || scene->isAudible()) {
            scene->isPlaying = false;
            scene->stop(fadeMs, true); // carries on if another scene is selected meanwhile
            scene->row()->setPlaying(false);
        }
    }
    if (m_fileBrowser) {
        m_fileBrowser->stopPreview();
    }
    m_panicUntilMs = (again || fadeMs <= 0) ? 0 : now + fadeMs;
}

void MainWindow::openMidiInput()
{
    if (m_midiPort.isEmpty()) {
        m_midi->close();
        return;
    }
    if (m_midi->isOpen() && m_midi->portName() == m_midiPort) {
        return;
    }
    m_midi->open(m_midiPort);
    if (m_settingsPage) {
        m_settingsPage->refreshMidi();
    }
}

void MainWindow::showMidiStatus(const QString& text, int hideAfterMs)
{
    m_midiStatus->setText(text);
    m_midiStatus->setVisible(!text.isEmpty());
    m_settingsPage->setMidiStatus(text); // learning can start from there too
    const int serial = ++m_midiStatusSerial;
    if (hideAfterMs > 0) {
        QTimer::singleShot(hideAfterMs, this, [this, serial]() {
            if (serial == m_midiStatusSerial) {
                m_midiStatus->hide();
                m_settingsPage->setMidiStatus(QString());
            }
        });
    }
}

void MainWindow::startMidiLearn(ControlAction action, int pad)
{
    if (!MidiInput::available()) {
        showMidiStatus(tr("This build of Feedra has no MIDI."), 4000);
        return;
    }
    if (!m_midi->isOpen()) {
        showMidiStatus(tr("No MIDI input: choose one in Settings, under Live controls."), 5000);
        return;
    }
    m_controls.startLearn(action, pad);
    showMidiStatus(ControlMap::isContinuous(action)
            ? tr("MIDI learn, %1: move a fader or knob (Esc cancels)").arg(ControlMap::actionName(action, pad))
            : tr("MIDI learn, %1: press a pad, key or button (Esc cancels)").arg(ControlMap::actionName(action, pad)));
}

void MainWindow::cancelMidiLearn()
{
    m_controls.cancelLearn();
    showMidiStatus(QString());
}

void MainWindow::handleMidi(const MidiMessage& message)
{
    const ControlAction learningAction = m_controls.learningAction();
    const int learningPad = m_controls.learningPad();
    bool learned = false;
    const std::vector<ControlCommand> commands = m_controls.handle(message, &learned);
    if (learned) {
        if (const ControlBinding* binding = m_controls.bindingFor(learningAction, learningPad)) {
            showMidiStatus(tr("%1: %2").arg(ControlMap::actionName(learningAction, learningPad), ControlMap::describe(*binding)), 3000);
        }
        saveLocalSettings();
        m_settingsPage->refreshMidi();
        return;
    }
    const auto sliderValue = [](float v) {
        return static_cast<int>(std::lround(v * VolumeDb::sliderSpan(VolumeDb::kFloorDb, VolumeDb::kUnityDb)));
    };
    for (const ControlCommand& command : commands) {
        Scene* scene = activeScene();
        switch (command.action) {
        case ControlAction::PadPlay:
            if (Pad* pad = scene ? scene->padAt(command.pad) : nullptr) {
                pad->togglePlay();
            }
            break;
        case ControlAction::PadVolume:
            if (Pad* pad = scene ? scene->padAt(command.pad) : nullptr) {
                pad->setPadVolume(VolumeDb::toLinearMuted(VolumeDb::fromSlider(sliderValue(command.value), VolumeDb::kFloorDb)));
            }
            break;
        case ControlAction::MasterVolume:
            m_mainVolume->setValue(sliderValue(command.value));
            break;
        case ControlAction::Intensity:
            if (scene) {
                scene->setIntensity(command.value);
                refreshIntensityControl();
            }
            break;
        case ControlAction::NextScene:
            if (m_config.activeSceneIdx + 1 < m_scenes.size()) {
                enableScene(m_config.activeSceneIdx + 1);
            }
            break;
        case ControlAction::PreviousScene:
            if (m_config.activeSceneIdx > 0) {
                enableScene(m_config.activeSceneIdx - 1);
            }
            break;
        case ControlAction::GoToScene:
            goToScene(m_config.activeSceneIdx);
            break;
        case ControlAction::Panic:
            panic();
            break;
        case ControlAction::TalkOver:
            m_duck.setHeld(Duck::Midi, command.pressed);
            break;
        }
    }
}

void MainWindow::refreshKeyHints()
{
    for (Scene* scene : m_scenes) {
        for (int i = 0; i < scene->padWidgets.size(); ++i) {
            scene->padWidgets[i]->setKeyHint(m_config.keyboardPads ? KeyLayout::keyLabel(i, scene->columns()) : QString());
        }
    }
}

void MainWindow::refreshIntensityControl()
{
    Scene* scene = activeScene();
    const int mask = scene ? scene->layerMask() : 0;
    if (m_intensityBox->isHidden() == (mask != 0)) {
        m_intensityBox->setVisible(mask != 0);
    }
    if (!scene || mask == 0) {
        return;
    }
    const int value = static_cast<int>(std::lround(scene->intensity() * 1000.0f));
    if (m_intensity->value() != value && !m_intensity->isSliderDown()) {
        const QSignalBlocker block(m_intensity);
        m_intensity->setValue(value);
    }
    const QString text = Intensity::describe(mask, scene->intensity());
    if (m_intensityValue->text() != text) {
        m_intensityValue->setText(text);
    }
}

void MainWindow::refreshTalkButton()
{
    if (!m_talkButton) {
        return;
    }
    // Lit while ducked, however it's held (Space or MIDI as well as the button).
    const bool live = m_duck.isActive();
    if (m_talkButton->property("live").toBool() != live) {
        m_talkButton->setProperty("live", live);
        m_talkButton->style()->unpolish(m_talkButton);
        m_talkButton->style()->polish(m_talkButton);
    }
}

bool MainWindow::handleReorderKey(QKeyEvent* event)
{
    if (m_page != Page::Main || (event->modifiers() & Qt::ControlModifier)) {
        return false;
    }
    // Enter in the scene list crossfades to the selected scene.
    const bool isGo = (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && !event->isAutoRepeat();
    if (event->key() != Qt::Key_Up && event->key() != Qt::Key_Down && !isGo) {
        return false;
    }
    QWidget* focus = QApplication::focusWidget();
    if (auto* edit = qobject_cast<QLineEdit*>(focus); edit && !edit->isReadOnly()) {
        return false;
    }
    if (qobject_cast<QAbstractSpinBox*>(focus) || qobject_cast<QComboBox*>(focus)) {
        return false;
    }

    if (m_sidebar == SidebarView::Files) {
        return false; // the arrows move through the file list
    }
    if (isGo) {
        if (m_sidebar != SidebarView::Scenes) {
            return false;
        }
        goToScene(m_config.activeSceneIdx);
        return true;
    }
    const bool up = event->key() == Qt::Key_Up;
    if (m_sidebar == SidebarView::Scenes) {
        const int idx = m_config.activeSceneIdx;
        if (up && idx > 0) {
            enableScene(idx - 1);
        } else if (!up && idx < m_scenes.size() - 1) {
            enableScene(idx + 1);
        }
        return true;
    }
    auto* pad = activePad();
    if (!pad) {
        return false;
    }
    const int idx = m_config.activeSampleIdx;
    const int count = static_cast<int>(pad->soundPlayer().player.size());
    int next = idx;
    if (up && idx > 0) {
        next = idx - 1;
    } else if (!up && idx < count - 1) {
        next = idx + 1;
    }
    if (next != idx) {
        syncPanelPad();
        m_editor->selectSample(next);
    }
    return true;
}
