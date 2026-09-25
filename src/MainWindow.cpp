#include "MainWindow.h"
#include "OpenALSoundPlayer.h"
#include "SampleLoadQueue.h"
#include "Scene.h"
#include "Theme.h"
#include "widgets/ReorderListHost.h"
#include "widgets/SampleRowWidget.h"
#include "widgets/SoundPadWidget.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QThread>
#include <QSaveFile>
#include <QScrollArea>
#include <QShortcut>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace {

QString defaultImpulsePath(const AppConfig& config)
{
    return QDir(config.dataDir()).filePath(QStringLiteral("ir/impulse.wav"));
}

std::filesystem::path toImpulsePath(const QString& path)
{
    return std::filesystem::path(path.toStdWString());
}

QString fromImpulsePath(const std::filesystem::path& path)
{
    if (path.empty()) {
        return {};
    }
    return QString::fromStdWString(path.wstring());
}

}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Feedra"));
    resize(1150, 800);
    m_config.setup();
    m_loads = new SampleLoadQueue(this);
    OpenALSoundPlayer::initialize();
    OpenALSoundPlayer::setConvolutionGain(OpenALSoundPlayer::defaultConvolutionGain());
    if (!OpenALSoundPlayer::setConvolutionImpulse(toImpulsePath(defaultImpulsePath(m_config)))) {
        qWarning() << "Convolution impulse not loaded" << defaultImpulsePath(m_config);
    }
    m_curDevice = QString::fromStdString(OpenALSoundPlayer::getDefaultDeviceString());

    buildMenus();
    buildUi();

    const QString settings = m_config.defaultSettingsPath();
    if (QFile::exists(settings)) {
        const QString backup = QDir(m_config.dataDir()).filePath(QStringLiteral("settings/settings_backup.json"));
        QFile::remove(backup);
        QFile::copy(settings, backup);
        loadConfigFrom(settings);
    } else {
        createDefaultScenes();
    }

    if (m_scenes.isEmpty()) {
        createDefaultScenes();
    }
    enableScene(std::clamp(m_config.activeSceneIdx, 0, std::max(0, static_cast<int>(m_scenes.size()) - 1)));
    updateMainControls();

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
    waitForLoads();
    saveConfigTo(m_config.defaultSettingsPath(), false);
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

    m_saveAction = fileMenu->addAction(tr("&Save"));
    m_saveAction->setShortcut(QKeySequence::Save);
    connect(m_saveAction, &QAction::triggered, this, [this]() { saveConfig(); });

    m_saveAsAction = fileMenu->addAction(tr("Save &As..."));
    m_saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(m_saveAsAction, &QAction::triggered, this, [this]() { saveConfigAs(); });

    fileMenu->addSeparator();
    auto* quitAct = fileMenu->addAction(tr("E&xit"));
    quitAct->setShortcut(QKeySequence::Quit);
    connect(quitAct, &QAction::triggered, this, &QWidget::close);

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
    auto* settingsAct = viewMenu->addAction(tr("Settings"));
    settingsAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+3")));
    connect(settingsAct, &QAction::triggered, this, [this]() { setPage(Page::Settings); });
    auto* themeAct = viewMenu->addAction(tr("Theme"));
    themeAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+4")));
    connect(themeAct, &QAction::triggered, this, [this]() { setPage(Page::Theme); });
}

void MainWindow::buildUi()
{
    auto* central = new QWidget(this);
    central->setObjectName(QStringLiteral("central"));
    setCentralWidget(central);
    auto* outer = new QVBoxLayout(central);

    m_pages = new QStackedWidget(central);

    m_mainPage = new QWidget(m_pages);
    auto* mainLayout = new QVBoxLayout(m_mainPage);
    m_mainVolume = new QSlider(Qt::Horizontal, m_mainPage);
    m_mainVolume->setRange(0, 1000);
    m_mainVolume->setValue(1000);
    m_mainVolume->setMaximumWidth(260);
    auto* volumeRow = new QHBoxLayout();
    volumeRow->addWidget(new QLabel(tr("Main Volume"), m_mainPage));
    volumeRow->addWidget(m_mainVolume);
    m_loadBar = new QProgressBar(m_mainPage);
    m_loadBar->setObjectName(QStringLiteral("LoadProgress"));
    m_loadBar->setTextVisible(false);
    m_loadBar->setFixedWidth(180);
    m_loadBar->setFixedHeight(14);
    m_loadBar->setRange(0, 1);
    m_loadBar->hide();
    m_loadLabel = new QLabel(m_mainPage);
    m_loadLabel->setObjectName(QStringLiteral("LoadProgressLabel"));
    m_loadLabel->hide();
    volumeRow->addSpacing(16);
    volumeRow->addWidget(m_loadBar);
    volumeRow->addWidget(m_loadLabel);
    volumeRow->addStretch();
    mainLayout->addLayout(volumeRow);

    auto* body = new QHBoxLayout();
    m_padStack = new QStackedWidget(m_mainPage);
    body->addWidget(m_padStack, 1);

    auto* side = new QWidget(m_mainPage);
    side->setMinimumWidth(320);
    auto* sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(0, 0, 0, 0);
    sideLayout->setSpacing(0);

    auto* sideTabBar = new QWidget(side);
    sideTabBar->setObjectName(QStringLiteral("BottomTabBar"));
    sideTabBar->setAttribute(Qt::WA_StyledBackground, true);
    auto* sideTabRow = new QHBoxLayout(sideTabBar);
    sideTabRow->setContentsMargins(8, 4, 8, 0);
    sideTabRow->setSpacing(4);
    m_scenesTab = new QPushButton(tr("Scenes"), sideTabBar);
    m_scenesTab->setObjectName(QStringLiteral("BottomTab"));
    m_scenesTab->setFocusPolicy(Qt::NoFocus);
    m_editorTab = new QPushButton(tr("Editor"), sideTabBar);
    m_editorTab->setObjectName(QStringLiteral("BottomTab"));
    m_editorTab->setFocusPolicy(Qt::NoFocus);
    sideTabRow->addWidget(m_scenesTab, 0, Qt::AlignBottom);
    sideTabRow->addWidget(m_editorTab, 0, Qt::AlignBottom);
    sideTabRow->addStretch();

    m_sidebarStack = new QStackedWidget(side);
    m_sidebarStack->setObjectName(QStringLiteral("SidebarPages"));
    m_sidebarStack->setAttribute(Qt::WA_StyledBackground, true);

    m_scenesPage = new QWidget(m_sidebarStack);
    auto* scenesLayout = new QVBoxLayout(m_scenesPage);
    scenesLayout->setContentsMargins(8, 8, 0, 8);
    auto* sceneScroll = new QScrollArea(m_scenesPage);
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
    m_addScene = new QPushButton(QStringLiteral("+"), m_scenesPage);
    m_addScene->setObjectName(QStringLiteral("AddScene"));
    m_addScene->setFixedSize(44, 44);
    scenesLayout->addWidget(sceneScroll, 1);
    auto* addSceneRow = new QHBoxLayout();
    addSceneRow->setContentsMargins(0, 0, 8, 0);
    addSceneRow->addWidget(m_addScene, 0, Qt::AlignLeft);
    addSceneRow->addStretch();
    scenesLayout->addLayout(addSceneRow);

    m_editorPage = new QWidget(m_sidebarStack);
    auto* editLayout = new QVBoxLayout(m_editorPage);
    editLayout->setContentsMargins(8, 8, 0, 8);
    m_editTitle = new QLabel(m_editorPage);
    m_editTitle->setWordWrap(true);
    m_editTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_addSample = new QPushButton(QStringLiteral("+"), m_editorPage);
    m_addSample->setObjectName(QStringLiteral("AddSample"));
    m_addSample->setFixedSize(50, 50);
    auto* editTop = new QHBoxLayout();
    editTop->setContentsMargins(0, 0, 8, 0);
    editTop->addWidget(m_editTitle, 1);
    editTop->addWidget(m_addSample);
    editLayout->addLayout(editTop);

    auto* sampleScroll = new QScrollArea(m_editorPage);
    sampleScroll->setFrameShape(QFrame::NoFrame);
    sampleScroll->setWidgetResizable(true);
    sampleScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sampleScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    sampleScroll->viewport()->setAutoFillBackground(true);
    auto* sampleHost = new ReorderListHost(SampleRowWidget::dragMimeType(), sampleScroll);
    sampleHost->setObjectName(QStringLiteral("SampleListHost"));
    m_sampleListLayout = new QVBoxLayout(sampleHost);
    m_sampleListLayout->setContentsMargins(0, 4, 8, 4);
    m_sampleListLayout->setSpacing(4);
    m_sampleListLayout->addStretch();
    sampleScroll->setWidget(sampleHost);
    editLayout->addWidget(sampleScroll, 1);
    connect(sampleHost, &ReorderListHost::itemReordered, this, [this](int sampleId, int insertIndex) {
        auto* pad = activePad();
        if (!pad) {
            return;
        }
        const auto& players = pad->soundPlayer().player;
        for (int i = 0; i < static_cast<int>(players.size()); ++i) {
            if (players[i]->id == sampleId) {
                moveEditorSample(i, insertIndex);
                return;
            }
        }
    }, Qt::QueuedConnection); // after QDrag::exec returns, so the source row is still alive

    m_pan = new QSlider(Qt::Horizontal, m_editorPage);
    m_pan->setRange(0, 1000);
    m_pan->setValue(500);
    m_pitch = new QSlider(Qt::Horizontal, m_editorPage);
    m_pitch->setRange(0, 3000);
    m_pitch->setValue(1000);
    m_gain = new QSlider(Qt::Horizontal, m_editorPage);
    m_gain->setRange(1000, 6000);
    m_gain->setValue(1000);
    m_randomPan = new QCheckBox(tr("Random Pan"), m_editorPage);
    m_spatialise = new QCheckBox(tr("Spatialise Stereo"), m_editorPage);

    auto addLabeled = [&](const QString& label, QWidget* w) {
        auto* row = new QHBoxLayout();
        row->setContentsMargins(0, 0, 8, 0);
        auto* caption = new QLabel(label, m_editorPage);
        caption->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
        row->addWidget(caption);
        row->addWidget(w, 1);
        editLayout->addLayout(row);
    };
    addLabeled(tr("Panning"), m_pan);
    addLabeled(tr("Pitch"), m_pitch);
    addLabeled(tr("Gain"), m_gain);
    auto* checkLayout = new QVBoxLayout();
    checkLayout->setContentsMargins(0, 0, 8, 0);
    checkLayout->addWidget(m_randomPan);
    checkLayout->addWidget(m_spatialise);
    editLayout->addLayout(checkLayout);

    m_sidebarStack->addWidget(m_scenesPage);
    m_sidebarStack->addWidget(m_editorPage);
    sideLayout->addWidget(sideTabBar);
    sideLayout->addWidget(m_sidebarStack, 1);
    body->addWidget(side);
    mainLayout->addLayout(body, 1);

    buildSettingsPage();
    buildThemePage();

    m_pages->addWidget(m_mainPage);
    m_pages->addWidget(m_settingsPage);
    m_pages->addWidget(m_themePage);
    outer->addWidget(m_pages, 1);

    m_bottomPanel = new QWidget(central);
    m_bottomPanel->setObjectName(QStringLiteral("BottomPanel"));
    m_bottomPanel->setAttribute(Qt::WA_StyledBackground, true);
    auto* bottomLayout = new QVBoxLayout(m_bottomPanel);
    bottomLayout->setContentsMargins(0, 0, 0, 0);
    bottomLayout->setSpacing(0);

    auto* tabBar = new QWidget(m_bottomPanel);
    tabBar->setObjectName(QStringLiteral("BottomTabBar"));
    tabBar->setAttribute(Qt::WA_StyledBackground, true);
    auto* tabRow = new QHBoxLayout(tabBar);
    tabRow->setContentsMargins(8, 4, 8, 0);
    tabRow->setSpacing(4);
    m_sampleTab = new QPushButton(tr("Sample"), tabBar);
    m_sampleTab->setObjectName(QStringLiteral("BottomTab"));
    m_sampleTab->setFocusPolicy(Qt::NoFocus);
    m_padTab = new QPushButton(tr("Pad"), tabBar);
    m_padTab->setObjectName(QStringLiteral("BottomTab"));
    m_padTab->setFocusPolicy(Qt::NoFocus);
    m_collapseBottom = new QPushButton(QStringLiteral("\u25B4"), tabBar);
    m_collapseBottom->setObjectName(QStringLiteral("BottomCollapse"));
    m_collapseBottom->setFocusPolicy(Qt::NoFocus);
    m_collapseBottom->setFixedSize(28, 22);
    m_collapseBottom->setToolTip(tr("Hide panel"));
    tabRow->addWidget(m_sampleTab, 0, Qt::AlignBottom);
    tabRow->addWidget(m_padTab, 0, Qt::AlignBottom);
    tabRow->addStretch();
    tabRow->addWidget(m_collapseBottom, 0, Qt::AlignBottom);

    m_bottomStack = new QStackedWidget(m_bottomPanel);
    m_bottomStack->setObjectName(QStringLiteral("BottomPages"));

    auto* samplePage = new QWidget(m_bottomStack);
    auto* sampleLayout = new QVBoxLayout(samplePage);
    sampleLayout->setContentsMargins(12, 8, 12, 8);
    m_infoLabel = new QLabel(samplePage);
    m_infoLabel->setObjectName(QStringLiteral("SampleInfo"));
    m_infoLabel->setWordWrap(true);
    m_infoLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_infoLabel->setMinimumHeight(m_infoLabel->fontMetrics().lineSpacing() * 6);
    sampleLayout->addWidget(m_infoLabel);
    sampleLayout->addStretch();

    auto* padPage = new QWidget(m_bottomStack);
    auto* padGrid = new QGridLayout(padPage);
    padGrid->setContentsMargins(12, 8, 12, 8);
    padGrid->setHorizontalSpacing(10);
    padGrid->setVerticalSpacing(8);
    m_minDelay = new QSpinBox(padPage);
    m_minDelay->setRange(0, 600);
    m_minDelay->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
    m_minDelay->setAlignment(Qt::AlignRight);
    m_minDelay->setMinimumWidth(72);
    m_maxDelay = new QSpinBox(padPage);
    m_maxDelay->setRange(0, 600);
    m_maxDelay->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
    m_maxDelay->setAlignment(Qt::AlignRight);
    m_maxDelay->setMinimumWidth(72);
    m_reverbSend = new QSlider(Qt::Horizontal, padPage);
    m_reverbSend->setRange(0, 1000);
    m_reverbSend2 = new QSlider(Qt::Horizontal, padPage);
    m_reverbSend2->setRange(0, 1000);
    m_reverbSend2->setToolTip(tr("Send into the convolution reverb"));
    m_randomPlayback = new QCheckBox(tr("Random Playback"), padPage);
    m_randomPlayback->hide();
    padGrid->addWidget(new QLabel(tr("Min delay"), padPage), 0, 0);
    padGrid->addWidget(m_minDelay, 0, 1);
    padGrid->addWidget(new QLabel(tr("Max delay"), padPage), 0, 2);
    padGrid->addWidget(m_maxDelay, 0, 3);
    padGrid->addWidget(new QLabel(tr("Reverb send"), padPage), 1, 0);
    padGrid->addWidget(m_reverbSend, 1, 1, 1, 3);
    padGrid->addWidget(new QLabel(tr("Convolution send"), padPage), 2, 0);
    padGrid->addWidget(m_reverbSend2, 2, 1, 1, 3);
    padGrid->addWidget(m_randomPlayback, 3, 0, 1, 4);
    padGrid->setRowStretch(4, 1);
    padGrid->setColumnStretch(1, 1);
    padGrid->setColumnStretch(3, 1);

    m_bottomStack->addWidget(samplePage);
    m_bottomStack->addWidget(padPage);
    m_bottomPageHeight = std::max(m_infoLabel->minimumHeight() + 16, 148);
    m_bottomStack->setFixedHeight(m_bottomPageHeight);

    bottomLayout->addWidget(tabBar);
    bottomLayout->addWidget(m_bottomStack);
    outer->addWidget(m_bottomPanel, 0);

    connect(m_sampleTab, &QPushButton::clicked, this, [this]() {
        if (!m_bottomCollapsed && m_bottomStack->currentIndex() == 0) {
            setBottomCollapsed(true);
            return;
        }
        setBottomTab(0);
    });
    connect(m_padTab, &QPushButton::clicked, this, [this]() {
        if (!m_bottomCollapsed && m_bottomStack->currentIndex() == 1) {
            setBottomCollapsed(true);
            return;
        }
        setBottomTab(1);
    });
    connect(m_collapseBottom, &QPushButton::clicked, this, [this]() {
        setBottomCollapsed(!m_bottomCollapsed);
    });
    setBottomTab(0);

    connect(m_addScene, &QPushButton::clicked, this, &MainWindow::addNewScene);
    connect(m_scenesTab, &QPushButton::clicked, this, [this]() {
        setSidebarView(SidebarView::Scenes);
    });
    connect(m_editorTab, &QPushButton::clicked, this, [this]() {
        setSidebarView(SidebarView::Editor);
    });
    connect(m_mainVolume, &QSlider::valueChanged, this, [this](int v) {
        m_config.setMasterVolume(v / 1000.0f);
    });
    connect(m_minDelay, qOverload<int>(&QSpinBox::valueChanged), this, [this](int v) {
        if (m_updatingControls) return;
        if (auto* pad = activePad()) {
            pad->soundPlayer().setMinDelay(v);
            if (v > pad->soundPlayer().getMaxDelay()) {
                pad->soundPlayer().setMaxDelay(v);
                m_maxDelay->setValue(v);
            }
            for (int i = 0; i < static_cast<int>(pad->soundPlayer().player.size()); ++i) {
                pad->soundPlayer().recalculateDelay(i);
            }
            updateMainControls();
        }
    });
    connect(m_maxDelay, qOverload<int>(&QSpinBox::valueChanged), this, [this](int v) {
        if (m_updatingControls) return;
        if (auto* pad = activePad()) {
            pad->soundPlayer().setMaxDelay(v);
            if (v < pad->soundPlayer().getMinDelay()) {
                pad->soundPlayer().setMinDelay(v);
                m_minDelay->setValue(v);
            }
            for (int i = 0; i < static_cast<int>(pad->soundPlayer().player.size()); ++i) {
                pad->soundPlayer().recalculateDelay(i);
            }
            updateMainControls();
        }
    });
    connect(m_reverbSend, &QSlider::valueChanged, this, [this](int v) {
        if (m_updatingControls) return;
        if (auto* pad = activePad()) {
            pad->setReverbSend(v / 1000.0f);
        }
    });
    connect(m_reverbSend2, &QSlider::valueChanged, this, [this](int v) {
        if (m_updatingControls) return;
        if (auto* pad = activePad()) {
            pad->setReverbSend2(v / 1000.0f);
        }
    });
    connect(m_randomPlayback, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updatingControls) return;
        if (auto* pad = activePad()) {
            pad->soundPlayer().setRandomPlayback(on);
        }
    });
    connect(m_pan, &QSlider::valueChanged, this, [this](int v) {
        if (m_updatingControls) return;
        if (auto* pad = activePad(); pad && pad->soundPlayer().player.size() > static_cast<size_t>(m_config.activeSampleIdx)) {
            const int channels = pad->soundPlayer().getNumChannels();
            if ((channels == 2 && m_spatialise->isChecked()) || channels == 1) {
                pad->soundPlayer().player.at(m_config.activeSampleIdx)->setPan((v / 1000.0f) * 2.0f - 1.0f);
            }
        }
    });
    connect(m_pitch, &QSlider::valueChanged, this, [this](int v) {
        if (m_updatingControls) return;
        if (auto* pad = activePad(); pad && pad->soundPlayer().player.size() > static_cast<size_t>(m_config.activeSampleIdx)) {
            pad->soundPlayer().player.at(m_config.activeSampleIdx)->setPitch(v / 1000.0f);
        }
    });
    connect(m_gain, &QSlider::valueChanged, this, [this](int v) {
        if (m_updatingControls) return;
        if (auto* pad = activePad(); pad && pad->soundPlayer().player.size() > static_cast<size_t>(m_config.activeSampleIdx)) {
            pad->soundPlayer().player.at(m_config.activeSampleIdx)->setGain(v / 1000.0f);
        }
    });
    connect(m_randomPan, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updatingControls) return;
        if (auto* pad = activePad()) {
            pad->soundPlayer().setRandomPan(on);
        }
    });
    connect(m_spatialise, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updatingControls) return;
        if (auto* pad = activePad(); pad && pad->soundPlayer().getNumChannels() == 2) {
            pad->soundPlayer().setSpatialisedStereo(m_config.activeSampleIdx, on);
            m_pan->setEnabled(on);
        }
    });
    connect(m_addSample, &QPushButton::clicked, this, [this]() {
        if (auto* pad = activePad()) {
            QString start = m_config.lastPath;
            if (start.isEmpty()) {
                start = m_config.libraryLocation();
            }
            const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Load files"), start,
                tr("Audio files (*.wav *.flac *.ogg *.mp3 *.aiff *.aif);;All files (*.*)"));
            if (!paths.isEmpty()) {
                pad->loadFiles(paths, false);
                rebuildEditSamples();
            }
        }
    });

    syncSettingsPage();

    new QShortcut(QKeySequence(QStringLiteral("Ctrl+D")), this, [this]() {
        if (m_page != Page::Main) {
            return;
        }
        if (m_sidebar == SidebarView::Scenes) {
            clearActivePad();
        } else {
            clearActiveSample();
        }
    });
    setSidebarView(SidebarView::Scenes);
}

void MainWindow::connectScene(Scene* scene)
{
    connect(scene, &Scene::padSelected, this, &MainWindow::onPadClicked);
    connect(scene->row(), &SceneRowWidget::deleteRequested, this, &MainWindow::deleteScene);
    for (SoundPadWidget* pad : scene->pads) {
        pad->setLoadQueue(m_loads);
        connect(pad, &SoundPadWidget::padDropped, this, [this](int from, int to) { copyPad(from, to); });
        connect(pad, &SoundPadWidget::filesDropped, this, [this]() { updateMainControls(); });
        connect(pad, &SoundPadWidget::loadStateChanged, this, [this, pad]() {
            refreshLoadUi();
            if (pad == activePad() && m_sidebar == SidebarView::Editor) {
                rebuildEditSamples();
            }
        });
        connect(pad, &SoundPadWidget::loadingFinished, this, [this, pad]() {
            for (Scene* scene : m_scenes) {
                if (scene->id == pad->sceneId() && scene->isPlaying && pad->isLoaded() && !pad->isLoading()) {
                    pad->soundPlayer().setPaused(false);
                }
            }
            updateMainControls();
            if (pad == activePad() && m_sidebar == SidebarView::Editor) {
                rebuildEditSamples();
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

void MainWindow::addNewScene()
{
    if (static_cast<unsigned int>(m_scenes.size()) >= m_config.maxScenes) {
        return;
    }
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

    auto* scene = new Scene(&m_config, newId, QString(), m_padStack, m_sceneListHost, this);
    m_scenes.push_back(scene);
    m_padStack->addWidget(scene->grid());
    m_sceneListLayout->insertWidget(m_sceneListLayout->count() - 1, scene->row());
    connectScene(scene);
    m_addScene->setEnabled(static_cast<unsigned int>(m_scenes.size()) < m_config.maxScenes);
    enableScene(m_scenes.size() - 1);
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
    const bool deletingActive = (idx == m_config.activeSceneIdx);
    m_padStack->removeWidget(scene->grid());
    m_scenes.removeAt(idx);
    delete scene;
    m_addScene->setEnabled(true);
    int nextIdx = m_config.activeSceneIdx;
    if (deletingActive) {
        nextIdx = qMin(idx, m_scenes.size() - 1);
    } else if (idx < m_config.activeSceneIdx) {
        nextIdx = m_config.activeSceneIdx - 1;
    }
    enableScene(nextIdx);
    updateSceneListLayout();
}

void MainWindow::enableScene(int idx)
{
    if (idx < 0 || idx >= m_scenes.size()) {
        return;
    }
    for (Scene* scene : m_scenes) {
        scene->endFade();
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
    auto* pad = activePad();
    m_updatingControls = true;
    if (pad && pad->isLoaded()) {
        m_minDelay->setValue(pad->soundPlayer().getMinDelay());
        m_maxDelay->setValue(pad->soundPlayer().getMaxDelay());
        m_reverbSend->setValue(static_cast<int>(pad->soundPlayer().getReverbSend() * 1000.0f));
        m_reverbSend2->setValue(static_cast<int>(pad->soundPlayer().getReverbSend2() * 1000.0f));
        m_randomPlayback->setChecked(pad->soundPlayer().isPlayingRandom());
        m_randomPlayback->setVisible(pad->soundPlayer().player.size() > 1);
        const int cur = pad->soundPlayer().getCurSound();
        QString path;
        if (cur >= 0 && cur < static_cast<int>(pad->soundPaths().size())) {
            path = QString::fromStdString(pad->soundPaths()[cur]);
        }
        QString format;
        QString sub;
        if (!pad->soundPlayer().player.empty()) {
            format = QString::fromStdString(pad->soundPlayer().player[0]->audioPlayer->getFormatString());
            sub = QString::fromStdString(pad->soundPlayer().player[0]->audioPlayer->getSubFormatString());
        }
        m_infoLabel->setText(tr("channels: %1\nformat: %2\nsub-format: %3\nsample rate: %4\npath: %5\nNum sounds: %6  Random delay: %7 secs")
            .arg(pad->channels())
            .arg(format)
            .arg(sub)
            .arg(pad->sampleRate())
            .arg(path)
            .arg(pad->soundPlayer().player.size())
            .arg(pad->soundPlayer().getTotalDelay(), 0, 'f', 2));
        m_minDelay->setEnabled(true);
        m_maxDelay->setEnabled(true);
        m_reverbSend->setEnabled(true);
        m_reverbSend2->setEnabled(OpenALSoundPlayer::convolutionAvailable());
    } else {
        m_infoLabel->setText(tr("channels: —\nformat: —\nsub-format: —\nsample rate: —\npath: —\nNum sounds: —  Random delay: — secs"));
        m_randomPlayback->hide();
        m_minDelay->setEnabled(false);
        m_maxDelay->setEnabled(false);
        m_reverbSend->setEnabled(false);
        m_reverbSend2->setEnabled(false);
    }
    m_updatingControls = false;

    for (Scene* scene : m_scenes) {
        for (SoundPadWidget* p : scene->pads) {
            p->setSelected(scene == activeScene() && p->padId() == m_config.activeSoundIdx);
        }
    }
}

void MainWindow::setBottomTab(int index)
{
    if (!m_bottomStack) {
        return;
    }
    m_bottomStack->setCurrentIndex(index);
    refreshTabButton(m_sampleTab, index == 0);
    refreshTabButton(m_padTab, index == 1);
    if (m_bottomCollapsed) {
        setBottomCollapsed(false);
    }
}

void MainWindow::setBottomCollapsed(bool collapsed, bool resizeWindow)
{
    const bool changed = m_bottomCollapsed != collapsed;
    m_bottomCollapsed = collapsed;
    if (m_bottomStack) {
        m_bottomStack->setVisible(!collapsed);
    }
    if (changed && resizeWindow && m_bottomPageHeight > 0 && !isMaximized() && !isFullScreen()) {
        resize(width(), height() + (collapsed ? -m_bottomPageHeight : m_bottomPageHeight));
    }
    if (m_collapseBottom) {
        m_collapseBottom->setText(collapsed ? QStringLiteral("\u25BE") : QStringLiteral("\u25B4"));
        m_collapseBottom->setToolTip(collapsed ? tr("Show panel") : tr("Hide panel"));
    }
    if (collapsed) {
        refreshTabButton(m_sampleTab, false);
        refreshTabButton(m_padTab, false);
    } else if (m_bottomStack) {
        refreshTabButton(m_sampleTab, m_bottomStack->currentIndex() == 0);
        refreshTabButton(m_padTab, m_bottomStack->currentIndex() == 1);
    }
}

void MainWindow::refreshTabButton(QPushButton* button, bool active)
{
    if (!button) {
        return;
    }
    button->setProperty("active", active);
    button->style()->unpolish(button);
    button->style()->polish(button);
    button->update();
}

void MainWindow::updateEditControls()
{
    auto* pad = activePad();
    if (!pad || pad->soundPlayer().player.empty()) {
        return;
    }
    m_config.activeSampleIdx = std::clamp(m_config.activeSampleIdx, 0, static_cast<int>(pad->soundPlayer().player.size()) - 1);
    auto* sample = pad->soundPlayer().player.at(m_config.activeSampleIdx);
    m_updatingControls = true;
    const int channels = pad->soundPlayer().getNumChannels();
    m_spatialise->setVisible(channels == 2);
    m_spatialise->setChecked(pad->soundPlayer().isSpatialisedStereo(m_config.activeSampleIdx));
    m_pan->setEnabled((channels == 2 && m_spatialise->isChecked()) || channels == 1);
    m_pan->setValue(static_cast<int>(((sample->getPan() + 1.0f) / 2.0f) * 1000.0f));
    m_pitch->setValue(static_cast<int>(sample->getPitch() * 1000.0f));
    m_gain->setValue(static_cast<int>(sample->getGain() * 1000.0f));
    m_randomPan->setChecked(pad->soundPlayer().isRandomPan());
    m_editTitle->setText(pad->soundName().toUpper());
    m_updatingControls = false;
}

void MainWindow::moveEditorSample(int fromIndex, int insertIndex)
{
    auto* pad = activePad();
    if (!pad) {
        return;
    }
    const int dest = pad->moveSample(fromIndex, insertIndex);
    if (dest < 0) {
        return;
    }
    m_config.activeSampleIdx = dest;
    if (dest < static_cast<int>(pad->soundPlayer().player.size())) {
        m_config.activeSampleId = pad->soundPlayer().player[static_cast<size_t>(dest)]->id;
    }
    rebuildEditSamples();
}

void MainWindow::rebuildEditSamples()
{
    qDeleteAll(m_sampleRows);
    m_sampleRows.clear();
    auto* pad = activePad();
    if (!pad) {
        updateMainControls();
        return;
    }
    for (AudioSample* sample : pad->soundPlayer().player) {
        auto* row = new SampleRowWidget(sample->id, QString::fromStdString(sample->sample_path), m_sampleListLayout->parentWidget());
        m_sampleListLayout->insertWidget(m_sampleListLayout->count() - 1, row);
        m_sampleRows.push_back(row);
        connect(row, &SampleRowWidget::clicked, this, [this, pad](int id) {
            for (int i = 0; i < static_cast<int>(pad->soundPlayer().player.size()); ++i) {
                if (pad->soundPlayer().player[i]->id == id) {
                    m_config.activeSampleId = id;
                    m_config.activeSampleIdx = i;
                    pad->soundPlayer().setPaused(true);
                    pad->soundPlayer().curSound = i;
                    pad->soundPlayer().setPaused(false);
                    updateEditControls();
                    break;
                }
            }
        });
    }
    updateEditControls();
    updateMainControls();
}

void MainWindow::buildSettingsPage()
{
    m_settingsPage = new QWidget(m_pages);
    m_settingsPage->setObjectName(QStringLiteral("SettingsPage"));
    m_settingsPage->setAttribute(Qt::WA_StyledBackground, true);
    auto* outer = new QVBoxLayout(m_settingsPage);
    outer->setContentsMargins(16, 16, 16, 16);
    auto* title = new QLabel(tr("Settings"), m_settingsPage);
    title->setObjectName(QStringLiteral("SettingsTitle"));
    outer->addWidget(title, 0, Qt::AlignLeft);

    auto* scroll = new QScrollArea(m_settingsPage);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* host = new QWidget(scroll);
    host->setObjectName(QStringLiteral("SettingsPage"));
    host->setAttribute(Qt::WA_StyledBackground, true);
    auto* grid = new QGridLayout(host);
    grid->setContentsMargins(0, 8, 16, 8);
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(10);
    int row = 0;
    auto addRow = [&](const QString& label, QWidget* field) {
        auto* caption = new QLabel(label, host);
        caption->setMinimumWidth(120);
        grid->addWidget(caption, row, 0, Qt::AlignLeft | Qt::AlignVCenter);
        grid->addWidget(field, row, 1);
        ++row;
    };

    m_loopByDefault = new QCheckBox(tr("Loop new pads"), host);
    grid->addWidget(m_loopByDefault, row, 0, 1, 2, Qt::AlignLeft);
    ++row;

    m_sceneLimit = new QSpinBox(host);
    m_sceneLimit->setRange(1, 64);
    m_sceneLimit->setMaximumWidth(120);
    addRow(tr("Scene limit"), m_sceneLimit);

    m_gridColumns = new QSpinBox(host);
    m_gridColumns->setRange(1, 12);
    m_gridColumns->setMaximumWidth(120);
    addRow(tr("Grid columns"), m_gridColumns);

    m_gridRows = new QSpinBox(host);
    m_gridRows->setRange(1, 8);
    m_gridRows->setMaximumWidth(120);
    addRow(tr("Grid rows"), m_gridRows);

    auto* gridNote = new QLabel(tr("Grid size applies to scenes added after the change."), host);
    gridNote->setWordWrap(true);
    grid->addWidget(gridNote, row, 0, 1, 2);
    ++row;

    m_libraryPath = new QLineEdit(host);
    auto* browse = new QPushButton(tr("Browse..."), host);
    auto* libraryRow = new QWidget(host);
    auto* libraryLayout = new QHBoxLayout(libraryRow);
    libraryLayout->setContentsMargins(0, 0, 0, 0);
    libraryLayout->addWidget(m_libraryPath, 1);
    libraryLayout->addWidget(browse);
    addRow(tr("Sample library"), libraryRow);

    m_reverbPreset = new QComboBox(host);
    m_reverbPreset->setMaxVisibleItems(24);
    for (int i = 0; i < OpenALSoundPlayer::reverbPresetCount(); ++i) {
        m_reverbPreset->addItem(
            QString::fromStdString(OpenALSoundPlayer::reverbPresetLabel(i)),
            QString::fromStdString(OpenALSoundPlayer::reverbPresetId(i)));
    }
    addRow(tr("Reverb preset"), m_reverbPreset);

    m_convolutionGain = new QSlider(Qt::Horizontal, host);
    m_convolutionGain->setRange(0, 1000);
    m_convolutionGain->setToolTip(tr("Output level of the convolution reverb. The bundled impulse is loud, so the default is low."));
    addRow(tr("Convolution gain"), m_convolutionGain);

    m_impulsePath = new QLineEdit(host);
    m_impulseBrowse = new QPushButton(tr("Browse..."), host);
    auto* impulseRow = new QWidget(host);
    auto* impulseLayout = new QHBoxLayout(impulseRow);
    impulseLayout->setContentsMargins(0, 0, 0, 0);
    impulseLayout->addWidget(m_impulsePath, 1);
    impulseLayout->addWidget(m_impulseBrowse);
    addRow(tr("Impulse response"), impulseRow);

    const bool convolution = OpenALSoundPlayer::convolutionAvailable();
    m_convolutionGain->setEnabled(convolution);
    m_impulsePath->setEnabled(convolution);
    m_impulseBrowse->setEnabled(convolution);

    grid->setColumnStretch(1, 1);
    grid->setRowStretch(row, 1);
    scroll->setWidget(host);
    outer->addWidget(scroll, 1);

    connect(m_loopByDefault, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updatingControls) {
            return;
        }
        m_config.loopByDefault = on;
    });
    connect(m_sceneLimit, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
        if (m_updatingControls) {
            return;
        }
        m_config.maxScenes = static_cast<unsigned int>(value);
        m_addScene->setEnabled(static_cast<unsigned int>(m_scenes.size()) < m_config.maxScenes);
    });
    connect(m_gridColumns, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
        if (!m_updatingControls) {
            m_config.gridWidth = value;
        }
    });
    connect(m_gridRows, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
        if (!m_updatingControls) {
            m_config.gridHeight = value;
        }
    });
    connect(m_libraryPath, &QLineEdit::editingFinished, this, [this]() {
        if (m_updatingControls) {
            return;
        }
        const QString path = m_libraryPath->text().trimmed();
        if (!path.isEmpty()) {
            m_config.defaultLibraryLocation = path;
        }
    });
    connect(browse, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getExistingDirectory(this, tr("Sample library"),
            m_config.defaultLibraryLocation);
        if (path.isEmpty()) {
            return;
        }
        m_libraryPath->setText(path);
        m_config.defaultLibraryLocation = path;
    });
    connect(m_reverbPreset, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (!m_updatingControls) {
            OpenALSoundPlayer::setReverbPreset(index);
        }
    });
    connect(m_convolutionGain, &QSlider::valueChanged, this, [this](int value) {
        if (!m_updatingControls) {
            OpenALSoundPlayer::setConvolutionGain(value / 1000.0f);
        }
    });
    connect(m_impulsePath, &QLineEdit::editingFinished, this, [this]() {
        if (m_updatingControls) {
            return;
        }
        applyImpulsePath(m_impulsePath->text().trimmed());
    });
    connect(m_impulseBrowse, &QPushButton::clicked, this, [this]() {
        const QString start = m_impulsePath->text().isEmpty()
            ? defaultImpulsePath(m_config)
            : m_impulsePath->text();
        const QString path = QFileDialog::getOpenFileName(this, tr("Impulse response"), start,
            tr("Audio (*.wav *.aif *.aiff *.flac *.ogg);;All files (*.*)"));
        if (path.isEmpty()) {
            return;
        }
        m_impulsePath->setText(path);
        applyImpulsePath(path);
    });
}

void MainWindow::buildThemePage()
{
    m_themePage = new QWidget(m_pages);
    m_themePage->setObjectName(QStringLiteral("ThemePage"));
    m_themePage->setAttribute(Qt::WA_StyledBackground, true);
    auto* outer = new QVBoxLayout(m_themePage);
    outer->setContentsMargins(16, 16, 16, 16);
    auto* title = new QLabel(tr("Theme"), m_themePage);
    title->setObjectName(QStringLiteral("SettingsTitle"));
    outer->addWidget(title, 0, Qt::AlignLeft);

    auto* scroll = new QScrollArea(m_themePage);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* host = new QWidget(scroll);
    host->setObjectName(QStringLiteral("ThemePage"));
    host->setAttribute(Qt::WA_StyledBackground, true);
    auto* grid = new QGridLayout(host);
    grid->setContentsMargins(0, 8, 16, 8);
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(8);

    m_themePreset = new QComboBox(host);
    m_themePreset->addItem(Theme::idLabel(Theme::Id::Parchment));
    m_themePreset->addItem(Theme::idLabel(Theme::Id::Night));
    m_themePreset->addItem(Theme::idLabel(Theme::Id::Forest));
    m_themePreset->addItem(Theme::idLabel(Theme::Id::Ink));
    m_themePreset->setMaximumWidth(280);
    auto* themeCaption = new QLabel(tr("Theme"), host);
    themeCaption->setMinimumWidth(160);
    grid->addWidget(themeCaption, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);
    grid->addWidget(m_themePreset, 0, 1, Qt::AlignLeft);

    QString lastGroup;
    int row = 1;
    m_themeSwatches.resize(Theme::roleCount());
    for (int i = 0; i < Theme::roleCount(); ++i) {
        const QString group = Theme::roleGroup(i);
        if (group != lastGroup) {
            auto* section = new QLabel(group, host);
            section->setObjectName(QStringLiteral("ThemeSection"));
            grid->addWidget(section, row, 0, 1, 2, Qt::AlignLeft);
            ++row;
            lastGroup = group;
        }
        auto* caption = new QLabel(Theme::roleLabel(i), host);
        auto* swatch = new QPushButton(host);
        swatch->setFixedSize(56, 24);
        swatch->setCursor(Qt::PointingHandCursor);
        swatch->setFocusPolicy(Qt::NoFocus);
        auto* hex = new QLabel(host);
        hex->setMinimumWidth(72);
        auto* field = new QWidget(host);
        auto* fieldLayout = new QHBoxLayout(field);
        fieldLayout->setContentsMargins(0, 0, 0, 0);
        fieldLayout->setSpacing(8);
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
    grid->setColumnStretch(1, 1);
    grid->setRowStretch(row, 1);
    scroll->setWidget(host);
    outer->addWidget(scroll, 1);

    connect(m_themePreset, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (m_updatingControls || index < 0) {
            return;
        }
        Theme::instance().setTheme(static_cast<Theme::Id>(index));
    });
    connect(&Theme::instance(), &Theme::changed, this, &MainWindow::refreshThemeSwatches);
}

void MainWindow::applyImpulsePath(const QString& path)
{
    const QString chosen = path.isEmpty() ? defaultImpulsePath(m_config) : path;
    if (toImpulsePath(chosen) == OpenALSoundPlayer::convolutionImpulsePath()) {
        m_impulsePath->setText(chosen);
        return;
    }
    if (!OpenALSoundPlayer::setConvolutionImpulse(toImpulsePath(chosen))) {
        QMessageBox::warning(this, tr("Feedra"),
            tr("Could not load the impulse response:\n%1").arg(chosen));
        m_impulsePath->setText(fromImpulsePath(OpenALSoundPlayer::convolutionImpulsePath()));
        return;
    }
    m_impulsePath->setText(fromImpulsePath(OpenALSoundPlayer::convolutionImpulsePath()));
}

void MainWindow::syncSettingsPage()
{
    m_updatingControls = true;
    m_loopByDefault->setChecked(m_config.loopByDefault);
    m_sceneLimit->setValue(static_cast<int>(m_config.maxScenes));
    m_gridColumns->setValue(m_config.gridWidth);
    m_gridRows->setValue(m_config.gridHeight);
    m_libraryPath->setText(m_config.defaultLibraryLocation);
    m_reverbPreset->setCurrentIndex(OpenALSoundPlayer::reverbPresetIndex());
    m_convolutionGain->setValue(static_cast<int>(OpenALSoundPlayer::convolutionGain() * 1000.0f + 0.5f));
    m_impulsePath->setText(fromImpulsePath(OpenALSoundPlayer::convolutionImpulsePath()));
    m_themePreset->setCurrentIndex(static_cast<int>(Theme::instance().id()));
    m_updatingControls = false;
    refreshThemeSwatches();
    if (m_addScene) {
        m_addScene->setEnabled(static_cast<unsigned int>(m_scenes.size()) < m_config.maxScenes);
    }
}

void MainWindow::refreshThemeSwatches()
{
    m_updatingControls = true;
    m_themePreset->setCurrentIndex(static_cast<int>(Theme::instance().id()));
    m_updatingControls = false;
    const QString border = Theme::instance().palette().fieldBorder.name(QColor::HexRgb);
    for (int i = 0; i < m_themeSwatches.size(); ++i) {
        const QString color = Theme::instance().colorAt(i).name(QColor::HexRgb);
        m_themeSwatches[i].button->setStyleSheet(
            QStringLiteral("QPushButton { background: %1; border: 1px solid %2; }").arg(color, border));
        m_themeSwatches[i].hex->setText(color);
    }
}

void MainWindow::applyAppSettings(const QJsonObject& global)
{
    if (global.contains(QStringLiteral("loopbydefault"))) {
        m_config.loopByDefault = global.value(QStringLiteral("loopbydefault")).toBool();
    }
    if (global.contains(QStringLiteral("scenelimit"))) {
        m_config.maxScenes = static_cast<unsigned int>(
            std::clamp(global.value(QStringLiteral("scenelimit")).toInt(static_cast<int>(m_config.maxScenes)), 1, 64));
    }
    if (global.contains(QStringLiteral("gridwidth"))) {
        m_config.gridWidth = std::clamp(global.value(QStringLiteral("gridwidth")).toInt(m_config.gridWidth), 1, 12);
    }
    if (global.contains(QStringLiteral("gridheight"))) {
        m_config.gridHeight = std::clamp(global.value(QStringLiteral("gridheight")).toInt(m_config.gridHeight), 1, 8);
    }
    const QString library = global.value(QStringLiteral("library")).toString();
    if (!library.isEmpty()) {
        m_config.defaultLibraryLocation = library;
    }
    const QString reverb = global.value(QStringLiteral("reverb")).toString();
    if (!reverb.isEmpty()) {
        OpenALSoundPlayer::setReverbPresetById(reverb.toStdString());
    }
    if (global.contains(QStringLiteral("convolutiongain"))) {
        OpenALSoundPlayer::setConvolutionGain(static_cast<float>(global.value(QStringLiteral("convolutiongain")).toDouble(
            OpenALSoundPlayer::defaultConvolutionGain())));
    }
    QString impulse = global.value(QStringLiteral("convolutionir")).toString();
    if (impulse.isEmpty() || !QFile::exists(impulse)) {
        impulse = defaultImpulsePath(m_config);
    }
    if (toImpulsePath(impulse) != OpenALSoundPlayer::convolutionImpulsePath()) {
        if (!OpenALSoundPlayer::setConvolutionImpulse(toImpulsePath(impulse))) {
            qWarning() << "Convolution impulse not loaded" << impulse;
        }
    }
    if (global.contains(QStringLiteral("theme")) || global.contains(QStringLiteral("themecolors"))) {
        Theme::instance().load(global.value(QStringLiteral("theme")).toString(),
            global.value(QStringLiteral("themecolors")).toObject());
    }
    syncSettingsPage();
}

void MainWindow::setPage(Page page)
{
    m_page = page;
    if (page == Page::Settings) {
        m_pages->setCurrentWidget(m_settingsPage);
    } else if (page == Page::Theme) {
        m_pages->setCurrentWidget(m_themePage);
    } else {
        m_pages->setCurrentWidget(m_mainPage);
        updateMainControls();
    }
}

void MainWindow::setSidebarView(SidebarView view)
{
    auto* pad = activePad();
    if (view == SidebarView::Editor && (!pad || pad->soundPlayer().player.empty())) {
        view = SidebarView::Scenes;
    }
    m_sidebar = view;
    const bool showScenes = view == SidebarView::Scenes;
    if (view == SidebarView::Editor) {
        m_config.activeSampleIdx = 0;
        if (pad && !pad->soundPlayer().player.empty()) {
            m_config.activeSampleId = pad->soundPlayer().player[0]->id;
        }
        rebuildEditSamples();
        m_sidebarStack->setCurrentWidget(m_editorPage);
    } else {
        m_sidebarStack->setCurrentWidget(m_scenesPage);
        updateMainControls();
    }
    m_scenesPage->setVisible(showScenes);
    m_editorPage->setVisible(!showScenes);
    m_addScene->setVisible(showScenes);
    refreshTabButton(m_scenesTab, showScenes);
    refreshTabButton(m_editorTab, !showScenes);
}

void MainWindow::saveConfig()
{
    if (!saveConfigTo(m_config.defaultSettingsPath(), false)) {
        QMessageBox::warning(this, tr("Feedra"),
            tr("Could not save settings to:\n%1").arg(m_config.defaultSettingsPath()));
    }
}

void MainWindow::saveConfigAs()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Save Feedra scenes"),
        m_config.defaultSettingsPath(), tr("JSON (*.json)"));
    if (path.isEmpty()) {
        return;
    }
    if (!saveConfigTo(path, true)) {
        QMessageBox::warning(this, tr("Feedra"), tr("Could not save settings to:\n%1").arg(path));
    }
}

bool MainWindow::saveConfigTo(const QString& path, bool copyFiles)
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

    QString filesDir;
    if (copyFiles) {
        filesDir = info.dir().filePath(QStringLiteral("files"));
        QDir().mkpath(filesDir);
    }

    QJsonObject root;
    QJsonObject global;
    global.insert(QStringLiteral("mainvolume"), m_config.masterVolume());
    global.insert(QStringLiteral("maxscenes"), m_scenes.size());
    global.insert(QStringLiteral("activesceneid"), m_config.activeSceneIdx);
    global.insert(QStringLiteral("loopbydefault"), m_config.loopByDefault);
    global.insert(QStringLiteral("scenelimit"), static_cast<int>(m_config.maxScenes));
    global.insert(QStringLiteral("gridwidth"), m_config.gridWidth);
    global.insert(QStringLiteral("gridheight"), m_config.gridHeight);
    global.insert(QStringLiteral("library"), m_config.defaultLibraryLocation);
    global.insert(QStringLiteral("reverb"),
        QString::fromStdString(OpenALSoundPlayer::reverbPresetId(OpenALSoundPlayer::reverbPresetIndex())));
    global.insert(QStringLiteral("convolutiongain"), static_cast<double>(OpenALSoundPlayer::convolutionGain()));
    global.insert(QStringLiteral("convolutionir"), fromImpulsePath(OpenALSoundPlayer::convolutionImpulsePath()));
    global.insert(QStringLiteral("theme"), Theme::instance().idName());
    global.insert(QStringLiteral("themecolors"), Theme::instance().colorsJson());
    saveWindowLayout(global);
    root.insert(QStringLiteral("global"), global);

    for (int i = 0; i < m_scenes.size(); ++i) {
        Scene* scene = m_scenes[i];
        QJsonObject sceneObj;
        sceneObj.insert(QStringLiteral("id"), scene->id);
        const QString sceneName = scene->row() ? scene->row()->sceneName() : scene->name;
        scene->name = sceneName;
        sceneObj.insert(QStringLiteral("name"), sceneName);
        sceneObj.insert(QStringLiteral("activesound"), scene->activeSoundIdx);
        for (SoundPadWidget* pad : scene->pads) {
            pad->saveToJson(sceneObj);
            if (copyFiles) {
                QJsonObject padObj = sceneObj.value(QString("%1-%2").arg(scene->id).arg(pad->padId())).toObject();
                if (!padObj.isEmpty()) {
                    QJsonObject samples = padObj.value(QStringLiteral("samples")).toObject();
                    for (auto it = samples.begin(); it != samples.end(); ++it) {
                        QJsonObject sample = it.value().toObject();
                        const QFileInfo src(sample.value(QStringLiteral("path")).toString());
                        const QString dest = QDir(filesDir).filePath(src.fileName());
                        if (src.exists() && QFileInfo(dest) != src) {
                            QFile::copy(src.absoluteFilePath(), dest);
                        }
                        sample.insert(QStringLiteral("path"), QDir::fromNativeSeparators(dest));
                        it.value() = sample;
                    }
                    padObj.insert(QStringLiteral("samples"), samples);
                    sceneObj.insert(QString("%1-%2").arg(scene->id).arg(pad->padId()), padObj);
                }
            }
        }
        root.insert(QString("scene%1").arg(i), sceneObj);
    }

    const QString dirPath = QFileInfo(savePath).absolutePath();
    if (!QDir().mkpath(dirPath)) {
        qWarning() << "Failed to create settings directory" << dirPath;
        return false;
    }

    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
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

void MainWindow::loadConfig()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Load Feedra scenes"),
        m_config.defaultSettingsPath(), tr("JSON (*.json)"));
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
    const QJsonObject root = m_config.json();
    const QJsonObject global = root.value(QStringLiteral("global")).toObject();
    applyAppSettings(global);
    m_mainVolume->setValue(static_cast<int>(global.value(QStringLiteral("mainvolume")).toDouble(1.0) * 1000.0));
    m_config.setMasterVolume(m_mainVolume->value() / 1000.0f);
    m_config.activeSceneIdx = global.value(QStringLiteral("activesceneid")).toInt();
    restoreWindowLayout(global);

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
        connectScene(scene);
        for (SoundPadWidget* pad : scene->pads) {
            pad->loadFromJson(sceneObj);
        }
        m_scenes.push_back(scene);
        m_padStack->addWidget(scene->grid());
        m_sceneListLayout->insertWidget(m_sceneListLayout->count() - 1, scene->row());
    }
    m_addScene->setEnabled(static_cast<unsigned int>(m_scenes.size()) < m_config.maxScenes);
}

void MainWindow::saveWindowLayout(QJsonObject& global) const
{
    const QRect box = (isMaximized() || isMinimized()) ? normalGeometry() : geometry();
    global.insert(QStringLiteral("windowx"), box.x());
    global.insert(QStringLiteral("windowy"), box.y());
    global.insert(QStringLiteral("windowwidth"), box.width());
    global.insert(QStringLiteral("windowheight"), box.height());
    global.insert(QStringLiteral("windowmaximized"), isMaximized());
    global.insert(QStringLiteral("windowgeometry"), QString::fromLatin1(saveGeometry().toBase64()));
    global.insert(QStringLiteral("bottomcollapsed"), m_bottomCollapsed);
    global.insert(QStringLiteral("bottomtab"), m_bottomStack ? m_bottomStack->currentIndex() : 0);
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
        const int tab = std::clamp(global.value(QStringLiteral("bottomtab")).toInt(0), 0, 1);
        setBottomTab(tab);
    }
    if (global.contains(QStringLiteral("bottomcollapsed"))) {
        setBottomCollapsed(global.value(QStringLiteral("bottomcollapsed")).toBool(), false);
    }
}

void MainWindow::clearActivePad()
{
    if (auto* pad = activePad()) {
        pad->clearPad();
        updateMainControls();
        if (m_sidebar == SidebarView::Editor) {
            setSidebarView(SidebarView::Scenes);
        }
    }
}

void MainWindow::copyPad(int fromIdx, int toIdx)
{
    Scene* scene = activeScene();
    if (!scene) {
        return;
    }
    SoundPadWidget* from = scene->padAt(fromIdx);
    SoundPadWidget* to = scene->padAt(toIdx);
    if (!from || !to || from == to) {
        return;
    }
    to->clearPad();
    to->copyFrom(*from);
    m_config.activeSoundIdx = toIdx;
    scene->activeSoundIdx = toIdx;
    updateMainControls();
}

void MainWindow::clearActiveSample()
{
    auto* pad = activePad();
    if (!pad || pad->soundPlayer().player.empty()) {
        return;
    }
    pad->removeSampleAt(m_config.activeSampleIdx);
    m_config.activeSampleIdx = std::max(0, m_config.activeSampleIdx - 1);
    if (pad->soundPlayer().player.empty()) {
        setSidebarView(SidebarView::Scenes);
    } else {
        rebuildEditSamples();
    }
}

void MainWindow::drainLoads(int budgetMs)
{
    if (!m_loads) {
        return;
    }
    m_loads->drain(budgetMs, [this](SampleLoadResult result) {
        if (SoundPadWidget* pad = findPad(result.job.sceneId, result.job.padId)) {
            pad->submitDecoded(result.job.sampleIndex, result.job.generation, std::move(result.audio));
        }
    });
}

void MainWindow::refreshLoadUi()
{
    const bool busy = m_loads && m_loads->isBusy();
    if (m_saveAction) {
        m_saveAction->setEnabled(!busy);
    }
    if (m_saveAsAction) {
        m_saveAsAction->setEnabled(!busy);
    }
    if (!m_loadBar || !m_loadLabel) {
        return;
    }
    if (!busy) {
        m_loadBar->hide();
        m_loadLabel->hide();
        if (windowTitle() != QStringLiteral("Feedra")) {
            setWindowTitle(QStringLiteral("Feedra"));
        }
        return;
    }
    const int total = std::max(1, m_loads->total());
    m_loadBar->setRange(0, total);
    m_loadBar->setValue(std::clamp(m_loads->settled(), 0, total));
    m_loadBar->show();
    QString label = tr("Loading %1 / %2").arg(m_loads->settled()).arg(m_loads->total());
    const QString file = m_loads->activeFile();
    if (!file.isEmpty()) {
        label += QStringLiteral(" · ") + file;
    }
    m_loadLabel->setText(label);
    m_loadLabel->show();
    setWindowTitle(tr("Feedra — Loading"));
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

SoundPadWidget* MainWindow::findPad(int sceneId, int padId) const
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
    refreshLoadUi();
    OpenALSoundPlayer::updateAll();
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
    if (m_page == Page::Main && m_sidebar == SidebarView::Editor) {
        auto* pad = activePad();
        if (pad) {
            for (int i = 0; i < m_sampleRows.size() && i < static_cast<int>(pad->soundPlayer().player.size()); ++i) {
                m_sampleRows[i]->setProgress(pad->soundPlayer().player[i]->audioPlayer->getPosition());
                m_sampleRows[i]->setSelected(i == m_config.activeSampleIdx);
            }
        }
    }
}

void MainWindow::checkAudioDevice()
{
    OpenALSoundPlayer::listDevices(false);
    const QString next = QString::fromStdString(OpenALSoundPlayer::getDefaultDeviceString());
    if (next != m_curDevice) {
        OpenALSoundPlayer::reopenDevice(next.toUtf8().constData());
        m_curDevice = next;
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
    if (m_sidebar != SidebarView::Editor) {
        return;
    }
    if (padChanged) {
        setSidebarView(SidebarView::Editor);
        return;
    }
    auto* pad = activePad();
    if (!pad || pad->soundPlayer().player.empty()) {
        setSidebarView(SidebarView::Scenes);
        return;
    }
    rebuildEditSamples();
}

SoundPadWidget* MainWindow::activePad() const
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

void MainWindow::closeEvent(QCloseEvent* event)
{
    waitForLoads();
    saveConfig();
    QMainWindow::closeEvent(event);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::KeyPress && watched->isWidgetType()
        && static_cast<QWidget*>(watched)->window() == this
        && handleReorderKey(static_cast<QKeyEvent*>(event))) {
        return true;
    }
    return QMainWindow::eventFilter(watched, event);
}

bool MainWindow::handleReorderKey(QKeyEvent* event)
{
    if (m_page != Page::Main || (event->modifiers() & Qt::ControlModifier)) {
        return false;
    }
    if (event->key() != Qt::Key_Up && event->key() != Qt::Key_Down) {
        return false;
    }
    QWidget* focus = QApplication::focusWidget();
    if (auto* edit = qobject_cast<QLineEdit*>(focus); edit && !edit->isReadOnly()) {
        return false;
    }
    if (qobject_cast<QAbstractSpinBox*>(focus) || qobject_cast<QComboBox*>(focus)) {
        return false;
    }

    const bool up = event->key() == Qt::Key_Up;
    if (m_sidebar == SidebarView::Scenes) {
        const int idx = m_config.activeSceneIdx;
        if (up && idx > 0) {
            moveScene(idx, idx - 1);
        } else if (!up && idx < m_scenes.size() - 1) {
            moveScene(idx, idx + 2);
        }
        return true;
    }
    auto* pad = activePad();
    if (!pad) {
        return false;
    }
    const int idx = m_config.activeSampleIdx;
    const int count = static_cast<int>(pad->soundPlayer().player.size());
    if (up && idx > 0) {
        moveEditorSample(idx, idx - 1);
    } else if (!up && idx < count - 1) {
        moveEditorSample(idx, idx + 2);
    }
    return true;
}
