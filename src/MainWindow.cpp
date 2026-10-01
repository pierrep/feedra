#include "MainWindow.h"
#include "AudioFormats.h"
#include "OpenALSoundPlayer.h"
#include "PeakStore.h"
#include "ProjectBackups.h"
#include "SampleLoadQueue.h"
#include "Scene.h"
#include "Theme.h"
#include "widgets/ReorderListHost.h"
#include "widgets/SampleRowWidget.h"
#include "widgets/SoundPadWidget.h"
#include "widgets/WaveformWidget.h"
#include "widgets/FileBrowserWidget.h"
#include "widgets/LogWindow.h"
#include "VolumeDb.h"

#include <QAction>
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

constexpr float kSampleGainMinDb = -12.0f;
constexpr float kSampleGainMaxDb = 24.0f;

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

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Feedra"));
    resize(1150, 800);
    m_config.setup();
    PeakStore::instance().setCacheDir(m_config.waveformCacheDir());
    m_loads = new SampleLoadQueue(this);
    OpenALSoundPlayer::initialize();
    for (int which = 0; which < OpenALSoundPlayer::kConvolutionCount; ++which) {
        OpenALSoundPlayer::setConvolutionGain(OpenALSoundPlayer::defaultConvolutionGain(), which);
        if (!OpenALSoundPlayer::setConvolutionImpulse(toImpulsePath(defaultImpulsePath(m_config)), which)) {
            qWarning() << "Convolution impulse not loaded" << defaultImpulsePath(m_config);
        }
    }
    m_curDevice = QString::fromStdString(OpenALSoundPlayer::getDefaultDeviceString());

    buildMenus();
    buildUi();

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
    enableScene(std::clamp(m_config.activeSceneIdx, 0, std::max(0, static_cast<int>(m_scenes.size()) - 1)));
    updateMainControls();
    refreshSettingsPathLabel();
    refreshLoadUi();

    auto* tickTimer = new QTimer(this);
    connect(tickTimer, &QTimer::timeout, this, &MainWindow::tick);
    tickTimer->start(33);

    auto* deviceTimer = new QTimer(this);
    connect(deviceTimer, &QTimer::timeout, this, &MainWindow::checkAudioDevice);
    deviceTimer->start(1000);

    // Arrow keys are otherwise consumed by the focused scroll area, slider or button before reaching this window.
    qApp->installEventFilter(this);
}

// `path` relative to `projectDir` ("files/x.wav") when it's inside that folder; otherwise
// unchanged. Always with forward slashes.
static QString projectRelativePath(const QString& path, const QString& projectDir)
{
    if (path.isEmpty() || projectDir.isEmpty() || QDir::isRelativePath(path)) {
        return QDir::fromNativeSeparators(path);
    }
    const QString rel = QDir(projectDir).relativeFilePath(QDir::cleanPath(path));
    if (rel.isEmpty() || QDir::isAbsolutePath(rel) || rel == QLatin1String("..") || rel.startsWith(QLatin1String("../"))) {
        return QDir::fromNativeSeparators(path);
    }
    return rel;
}

// One project export: the copies to make, filled in by saveConfigTo on the UI thread, then
// worked through by the export thread. The counters are read by the UI thread for the bar.
struct ExportState {
    struct Copy {
        QString src;
        QString dest;
        qint64 bytes = 0;
    };
    QString jsonPath;
    QString filesDir;
    QByteArray json;
    std::vector<Copy> copies;
    QHash<QString, QString> destBySource; // absolute source path -> destination
    QSet<QString> usedNames;              // lower-cased names taken in the files folder
    QStringList missing;                  // sources that weren't found when exporting
    qint64 totalBytes = 0;

    std::atomic<qint64> doneBytes { 0 };
    std::atomic<int> doneFiles { 0 };
    std::atomic<bool> cancel { false };
    std::atomic<bool> finished { false };
    std::mutex failedMutex;
    QStringList failed;

    // The file a sample is copied to. The same source always gets the same copy; a different
    // source with a name already taken gets "name (2).ext", "name (3).ext" and so on.
    QString destinationFor(const QFileInfo& src)
    {
        const QString key = src.absoluteFilePath();
        const auto found = destBySource.constFind(key);
        if (found != destBySource.constEnd()) {
            return found.value();
        }
        const QString base = src.completeBaseName();
        const QString suffix = src.suffix().isEmpty() ? QString() : QStringLiteral(".") + src.suffix();
        QString name = src.fileName();
        for (int n = 2; usedNames.contains(name.toLower()); ++n) {
            name = QStringLiteral("%1 (%2)%3").arg(base).arg(n).arg(suffix);
        }
        usedNames.insert(name.toLower());
        const QString dest = QDir(filesDir).filePath(name);
        destBySource.insert(key, dest);
        if (QFileInfo(dest).absoluteFilePath() != key) {
            copies.push_back({ key, dest, src.size() });
            totalBytes += src.size();
        }
        return dest;
    }

    void fail(const QString& path)
    {
        std::lock_guard<std::mutex> lock(failedMutex);
        failed << QDir::toNativeSeparators(path);
    }

    // Runs on the export thread.
    void run()
    {
        constexpr qint64 kBlock = 1 << 20;
        QByteArray block;
        for (const Copy& copy : copies) {
            if (cancel.load()) {
                break;
            }
            const QFileInfo srcInfo(copy.src);
            const QFileInfo destInfo(copy.dest);
            // Already there from an earlier export of the same file: leave it.
            if (destInfo.exists() && destInfo.size() == copy.bytes && destInfo.lastModified() >= srcInfo.lastModified()) {
                doneBytes += copy.bytes;
                ++doneFiles;
                continue;
            }
            QFile in(copy.src);
            QSaveFile out(copy.dest); // written to a temporary file, so a stopped copy leaves nothing behind
            qint64 copied = 0;
            bool ok = in.open(QIODevice::ReadOnly) && out.open(QIODevice::WriteOnly);
            while (ok && !cancel.load()) {
                block = in.read(kBlock);
                if (block.isEmpty()) {
                    ok = in.atEnd();
                    break;
                }
                if (out.write(block) != block.size()) {
                    ok = false;
                    break;
                }
                copied += block.size();
                doneBytes += block.size();
            }
            if (cancel.load()) {
                out.cancelWriting();
                break;
            }
            if (!ok || !out.commit()) {
                fail(copy.src);
            }
            // Keep the bar honest when a file turned out a different size or failed part way.
            doneBytes += copy.bytes - copied;
            ++doneFiles;
        }
        finished.store(true);
    }
};

MainWindow::~MainWindow()
{
    cancelExport();
    waitForLoads();
    saveOnExit();
    if (m_fileBrowser) {
        m_fileBrowser->stopPreview();
    }
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
    volumeRow->addSpacing(16);
    volumeRow->addWidget(m_loadBar);
    volumeRow->addWidget(m_loadLabel);
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

    m_editorPage = new QWidget(m_sidebarStack);
    auto* editLayout = new QVBoxLayout(m_editorPage);
    editLayout->setContentsMargins(12, 12, 4, 12);
    editLayout->setSpacing(10);
    m_editTitle = new QLabel(m_editorPage);
    m_editTitle->setObjectName(QStringLiteral("EditTitle"));
    m_editTitle->setWordWrap(true);
    m_editTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_addSample = new QPushButton(tr("+  Add"), m_editorPage);
    m_addSample->setObjectName(QStringLiteral("AddSample"));
    m_addSample->setFixedHeight(26); // matches the sample rows
    m_addSample->setCursor(Qt::PointingHandCursor);
    m_addSample->setToolTip(tr("Add samples to this pad"));
    auto* editTop = new QHBoxLayout();
    editTop->setContentsMargins(0, 0, 8, 0);
    editTop->addWidget(m_editTitle, 1);
    editTop->addWidget(m_addSample);
    editLayout->addLayout(editTop);

    auto* sampleScroll = new QScrollArea(m_editorPage);
    m_sampleScroll = sampleScroll;
    sampleScroll->setFrameShape(QFrame::NoFrame);
    sampleScroll->setWidgetResizable(true);
    sampleScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sampleScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    sampleScroll->viewport()->setAutoFillBackground(true);
    auto* sampleHost = new ReorderListHost(SampleRowWidget::dragMimeType(), sampleScroll);
    sampleHost->setObjectName(QStringLiteral("SampleListHost"));
    m_sampleListLayout = new QVBoxLayout(sampleHost);
    m_sampleListLayout->setContentsMargins(0, 4, 8, 4);
    m_sampleListLayout->setSpacing(3);
    m_sampleListLayout->addStretch();
    sampleScroll->setWidget(sampleHost);
    editLayout->addWidget(sampleScroll, 1);
    sampleHost->setAcceptsFiles(true);
    connect(sampleHost, &ReorderListHost::filesDropped, this, [this](const QStringList& paths) {
        auto* pad = activePad();
        if (!pad || !SoundPadWidget::confirmFileCount(this, paths.size())) {
            return;
        }
        pad->loadFiles(paths, false);
        rebuildEditSamples();
    }, Qt::QueuedConnection); // after the drag finishes, so a question box can open
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
    m_gain->setRange(0, VolumeDb::sliderSpan(kSampleGainMinDb, kSampleGainMaxDb));
    m_gain->setValue(VolumeDb::toSlider(VolumeDb::kUnityDb, kSampleGainMinDb));
    m_randomPan = new QCheckBox(tr("Random Pan"), m_editorPage);
    m_width = new QSlider(Qt::Horizontal, m_editorPage);
    m_width->setRange(0, 100);
    m_width->setValue(100);
    m_width->setToolTip(tr("Stereo width: 100% is the file's full stereo image, 0% plays it as mono at the pan position."));
    m_sampleLoop = new QCheckBox(tr("Loop"), m_editorPage);
    m_sampleLoop->setToolTip(tr("Loop this sample seamlessly between its loop points (set them on the Waveform tab).\n"
                                "The pad's loop icon shows and switches this for the sample that is playing."));

    // Displayed value = slider value * scale + offset; the spin box range follows the slider range.
    auto makeValueBox = [&](QSlider* slider, double scale, double offset) {
        auto* box = new QDoubleSpinBox(m_editorPage);
        box->setDecimals(3);
        box->setSingleStep(scale * slider->singleStep() * 10.0);
        box->setRange(slider->minimum() * scale + offset, slider->maximum() * scale + offset);
        box->setValue(slider->value() * scale + offset);
        box->setAlignment(Qt::AlignRight);
        box->setKeyboardTracking(false);
        box->setMinimumWidth(72);
        connect(slider, &QSlider::valueChanged, box, [box, scale, offset](int v) {
            const QSignalBlocker blocker(box);
            box->setValue(v * scale + offset);
        });
        connect(box, qOverload<double>(&QDoubleSpinBox::valueChanged), slider, [slider, scale, offset](double d) {
            slider->setValue(static_cast<int>(std::lround((d - offset) / scale)));
        });
        return box;
    };
    m_panValue = makeValueBox(m_pan, 0.002, -1.0);
    m_pitchValue = makeValueBox(m_pitch, 0.001, 0.0);
    m_gainValue = makeValueBox(m_gain, 0.1, kSampleGainMinDb);
    m_gainValue->setDecimals(1);
    m_gainValue->setSuffix(QStringLiteral(" dB"));
    m_gainValue->setMinimumWidth(88);
    m_widthValue = makeValueBox(m_width, 1.0, 0.0);
    m_widthValue->setDecimals(0);
    m_widthValue->setSuffix(QStringLiteral(" %"));

    m_sampleControls = new QWidget(m_editorPage);
    auto* controlsLayout = new QVBoxLayout(m_sampleControls);
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    auto addLabeled = [&](const QString& label, QWidget* w, QWidget* value) {
        auto* row = new QHBoxLayout();
        row->setContentsMargins(0, 0, 8, 0);
        auto* caption = new QLabel(label, m_sampleControls);
        caption->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
        row->addWidget(caption);
        row->addWidget(w, 1);
        row->addWidget(value);
        controlsLayout->addLayout(row);
        return caption;
    };
    m_panLabel = addLabeled(tr("Panning"), m_pan, m_panValue);
    m_widthLabel = addLabeled(tr("Width"), m_width, m_widthValue);
    addLabeled(tr("Pitch"), m_pitch, m_pitchValue);
    addLabeled(tr("Gain"), m_gain, m_gainValue);
    auto* checkLayout = new QVBoxLayout();
    checkLayout->setContentsMargins(0, 0, 8, 0);
    checkLayout->addWidget(m_randomPan);
    checkLayout->addWidget(m_sampleLoop);
    controlsLayout->addLayout(checkLayout);
    editLayout->addWidget(m_sampleControls);

    m_fileBrowser = new FileBrowserWidget(&m_config, m_sidebarStack);
    m_fileBrowser->setProjectFoldersProvider([this]() {
        QStringList folders;
        for (Scene* scene : m_scenes) {
            for (SoundPadWidget* pad : scene->pads) {
                for (const QString& path : pad->samplePaths()) {
                    folders << QFileInfo(path).absolutePath();
                }
            }
        }
        return folders;
    });
    connect(m_fileBrowser, &FileBrowserWidget::libraryFolderChosen, this, [this](const QString& dir) {
        m_updatingControls = true;
        m_libraryPath->setText(dir);
        m_updatingControls = false;
    });

    m_sidebarStack->addWidget(m_scenesPage);
    m_sidebarStack->addWidget(m_editorPage);
    m_sidebarStack->addWidget(m_fileBrowser);
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
    tabRow->setContentsMargins(8, 0, 8, 0);
    tabRow->setSpacing(0);
    m_waveTab = new QPushButton(tr("Waveform"), tabBar);
    m_waveTab->setObjectName(QStringLiteral("BottomTab"));
    m_waveTab->setFocusPolicy(Qt::NoFocus);
    m_padTab = new QPushButton(tr("Pad"), tabBar);
    m_padTab->setObjectName(QStringLiteral("BottomTab"));
    m_padTab->setFocusPolicy(Qt::NoFocus);
    m_sampleTab = new QPushButton(tr("Info"), tabBar);
    m_sampleTab->setObjectName(QStringLiteral("BottomTab"));
    m_sampleTab->setFocusPolicy(Qt::NoFocus);
    m_collapseBottom = new QPushButton(QStringLiteral("\u25B4"), tabBar);
    m_collapseBottom->setObjectName(QStringLiteral("BottomCollapse"));
    m_collapseBottom->setFocusPolicy(Qt::NoFocus);
    m_collapseBottom->setFixedSize(28, 24);
    m_collapseBottom->setToolTip(tr("Hide panel"));
    tabRow->addWidget(m_waveTab, 0, Qt::AlignBottom);
    tabRow->addWidget(m_padTab, 0, Qt::AlignBottom);
    tabRow->addWidget(m_sampleTab, 0, Qt::AlignBottom);
    tabRow->addStretch();
    tabRow->addWidget(m_collapseBottom, 0, Qt::AlignVCenter);

    m_bottomStack = new QStackedWidget(m_bottomPanel);
    m_bottomStack->setObjectName(QStringLiteral("BottomPages"));

    auto* samplePage = new QWidget(m_bottomStack);
    auto* sampleLayout = new QVBoxLayout(samplePage);
    sampleLayout->setContentsMargins(20, 12, 20, 12);
    m_infoLabel = new QLabel(samplePage);
    m_infoLabel->setObjectName(QStringLiteral("SampleInfo"));
    m_infoLabel->setWordWrap(true);
    m_infoLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_infoLabel->setMinimumHeight(m_infoLabel->fontMetrics().lineSpacing() * 6);
    sampleLayout->addWidget(m_infoLabel);
    sampleLayout->addStretch();

    auto* padPage = new QWidget(m_bottomStack);
    // Delay and playback options on the left, the four effect sends on the right.
    auto* padRow = new QHBoxLayout(padPage);
    padRow->setContentsMargins(20, 12, 20, 12);
    padRow->setSpacing(36);
    auto* padGrid = new QGridLayout();
    padGrid->setContentsMargins(0, 0, 0, 0);
    padGrid->setHorizontalSpacing(14);
    padGrid->setVerticalSpacing(10);
    m_minDelay = new QSpinBox(padPage);
    m_minDelay->setRange(0, 600);
    m_minDelay->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
    m_minDelay->setAlignment(Qt::AlignRight);
    m_minDelay->setMinimumWidth(72);
    m_minDelay->setMaximumWidth(140);
    m_minDelay->setSuffix(tr(" s"));
    m_maxDelay = new QSpinBox(padPage);
    m_maxDelay->setRange(0, 600);
    m_maxDelay->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
    m_maxDelay->setAlignment(Qt::AlignRight);
    m_maxDelay->setMinimumWidth(72);
    m_maxDelay->setMaximumWidth(140);
    m_maxDelay->setSuffix(tr(" s"));
    for (int bus = 0; bus < OpenALSoundPlayer::kSendCount; ++bus) {
        m_sendSliders[bus] = new QSlider(Qt::Horizontal, padPage);
        m_sendSliders[bus]->setRange(0, 1000);
        m_sendSliders[bus]->setMinimumWidth(240);
        m_sendSliders[bus]->setEnabled(false);
        m_sendLabels[bus] = new QLabel(padPage);
    }
    m_randomPlayback = new QCheckBox(tr("Random Playback"), padPage);
    m_randomPlayback->hide();
    m_repeat = new QCheckBox(tr("Repeat"), padPage);
    m_repeat->setToolTip(tr("After the last sample, start the list again after the delay.\n"
                            "To loop one sample seamlessly, use the pad's loop icon instead."));
    m_repeat->setEnabled(false);
    m_delayOn = new QCheckBox(tr("Delay"), padPage);
    m_delayOn->setToolTip(tr("Wait a random time between min and max before each sample.\n"
                             "Off: samples follow on straight away; min and max are kept."));
    m_delayOn->setEnabled(false);
    m_resetDelay = new QPushButton(tr("Reset delay"), padPage);
    m_resetDelay->setToolTip(tr("Set min and max delay to 0"));
    m_resetDelay->setEnabled(false);
    m_delayReadout = new QLabel(padPage);
    m_delayReadout->setObjectName(QStringLiteral("FieldHint"));
    m_delayReadout->setToolTip(tr("The delay picked for the current sample, and the time left while it counts down"));
    padGrid->addWidget(m_delayOn, 0, 0);
    padGrid->addWidget(m_resetDelay, 0, 1);
    padGrid->addWidget(m_delayReadout, 0, 2, 1, 2);
    padGrid->addWidget(new QLabel(tr("Min delay"), padPage), 1, 0);
    padGrid->addWidget(m_minDelay, 1, 1);
    padGrid->addWidget(new QLabel(tr("Max delay"), padPage), 1, 2);
    padGrid->addWidget(m_maxDelay, 1, 3);
    padGrid->addWidget(m_repeat, 2, 0, 1, 1);
    padGrid->addWidget(m_randomPlayback, 2, 1, 1, 3);
    m_scenePad = new QCheckBox(tr("Plays with scene"), padPage);
    m_scenePad->setToolTip(tr("When the scene is played from the scene list, only its scene pads start "
                              "(or every pad, if it has none).\nCtrl+click a pad to switch this on or off."));
    m_scenePad->setEnabled(false);
    padGrid->addWidget(m_scenePad, 3, 0, 1, 4);
    padGrid->setRowStretch(4, 1);
    padGrid->setColumnMinimumWidth(0, 130);

    // EAX reverbs on top, convolution reverbs below.
    auto* sendGrid = new QGridLayout();
    sendGrid->setContentsMargins(0, 0, 0, 0);
    sendGrid->setHorizontalSpacing(14);
    sendGrid->setVerticalSpacing(10);
    for (int bus = 0; bus < OpenALSoundPlayer::kSendCount; ++bus) {
        sendGrid->addWidget(m_sendLabels[bus], bus, 0);
        sendGrid->addWidget(m_sendSliders[bus], bus, 1);
    }
    sendGrid->setColumnStretch(1, 1);
    sendGrid->setRowStretch(OpenALSoundPlayer::kSendCount, 1);
    padRow->addLayout(padGrid, 0);
    padRow->addLayout(sendGrid, 1);
    refreshSendLabels();

    m_bottomStack->addWidget(samplePage);
    m_bottomStack->addWidget(padPage);
    m_waveform = new WaveformWidget(m_bottomStack);
    m_bottomStack->addWidget(m_waveform);
    connect(m_waveform, &WaveformWidget::seekRequested, this, [this](float pct) {
        // Same sample the waveform is showing: the one selected in the Editor.
        auto* pad = activePad();
        const int shown = waveformSampleIndex();
        if (!pad || shown < 0) {
            return;
        }
        pad->soundPlayer().seekSample(shown, pct);
    });
    connect(m_waveform, &WaveformWidget::loopPointsChanged, this, [this](double startPct, double endPct, bool finished) {
        // Applied on release: the waveform shows the drag itself, and the audio changes once.
        // The sample is the one shown when the drag began, even if the pad has moved on since.
        if (!finished) {
            if (m_loopDragSampleId < 0) {
                if (AudioSample* shown = waveformSample()) {
                    m_loopDragSampleId = shown->id;
                }
            }
            return;
        }
        AudioSample* sample = waveformSample();
        if (m_loopDragSampleId >= 0) {
            sample = nullptr;
            if (auto* pad = activePad()) {
                for (AudioSample* candidate : pad->soundPlayer().player) {
                    if (candidate->id == m_loopDragSampleId) {
                        sample = candidate;
                    }
                }
            }
            m_loopDragSampleId = -1;
        }
        if (!sample) {
            return;
        }
        const double duration = sample->audioPlayer->getDuration();
        if (duration <= 0.0) {
            return;
        }
        LoopRegion region = sample->loopRegion();
        region.start = startPct <= 0.0 ? 0.0 : startPct * duration;
        region.end = endPct >= 1.0 ? -1.0 : endPct * duration;
        applyLoopRegion(sample, region);
    });
    connect(m_waveform, &WaveformWidget::loopToggled, this, [this](bool on) {
        if (AudioSample* sample = waveformSample()) {
            LoopRegion region = sample->loopRegion();
            region.loop = on;
            applyLoopRegion(sample, region);
        }
    });
    connect(m_waveform, &WaveformWidget::crossfadeChanged, this, [this](int ms) {
        if (AudioSample* sample = waveformSample()) {
            LoopRegion region = sample->loopRegion();
            region.crossfadeMs = ms;
            applyLoopRegion(sample, region);
        }
    });
    connect(m_waveform, &WaveformWidget::playToEndToggled, this, [this](bool on) {
        if (AudioSample* sample = waveformSample()) {
            LoopRegion region = sample->loopRegion();
            region.playToEnd = on;
            applyLoopRegion(sample, region);
        }
    });
    connect(m_waveform, &WaveformWidget::playFromStartToggled, this, [this](bool on) {
        if (AudioSample* sample = waveformSample()) {
            LoopRegion region = sample->loopRegion();
            region.playFromStart = on;
            applyLoopRegion(sample, region);
        }
    });
    // Tall enough for every page, so nothing (like the Random Playback box) is clipped.
    m_bottomPageHeight = std::max({m_infoLabel->minimumHeight() + 24, padPage->sizeHint().height(), 148});
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
    connect(m_waveTab, &QPushButton::clicked, this, [this]() {
        if (!m_bottomCollapsed && m_bottomStack->currentIndex() == 2) {
            setBottomCollapsed(true);
            return;
        }
        setBottomTab(2);
    });
    connect(m_collapseBottom, &QPushButton::clicked, this, [this]() {
        setBottomCollapsed(!m_bottomCollapsed);
    });
    setBottomTab(2);

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
    connect(m_mainVolume, &QSlider::valueChanged, this, [this](int v) {
        const float db = VolumeDb::fromSlider(v, VolumeDb::kFloorDb);
        m_config.setMasterVolume(VolumeDb::toLinearMuted(db));
        m_mainVolumeValue->setText(VolumeDb::format(db));
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
            pad->update(); // the delay border comes and goes with the delay
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
            pad->update(); // the delay border comes and goes with the delay
            updateMainControls();
        }
    });
    for (int bus = 0; bus < OpenALSoundPlayer::kSendCount; ++bus) {
        connect(m_sendSliders[bus], &QSlider::valueChanged, this, [this, bus](int v) {
            if (m_updatingControls) return;
            if (auto* pad = activePad()) {
                pad->setSend(bus, v / 1000.0f);
            }
        });
    }
    connect(m_randomPlayback, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updatingControls) return;
        if (auto* pad = activePad()) {
            pad->soundPlayer().setRandomPlayback(on);
        }
    });
    connect(m_scenePad, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updatingControls) return;
        if (auto* pad = activePad()) {
            pad->setScenePad(on);
        }
    });
    connect(m_resetDelay, &QPushButton::clicked, this, [this]() {
        // Through the spin boxes, so the pad, its samples' delays and the border all follow.
        m_minDelay->setValue(0);
        m_maxDelay->setValue(0);
        refreshDelayReadout();
    });
    connect(m_delayOn, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updatingControls) return;
        if (auto* pad = activePad()) {
            pad->soundPlayer().setDelayEnabled(on);
            m_minDelay->setEnabled(on);
            m_maxDelay->setEnabled(on);
            pad->update(); // the delay border comes and goes with the delay
            refreshDelayReadout();
        }
    });
    connect(m_repeat, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updatingControls) return;
        if (auto* pad = activePad()) {
            pad->setRepeating(on);
        }
    });
    connect(m_sampleLoop, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updatingControls) return;
        if (auto* pad = activePad(); pad && pad->soundPlayer().player.size() > static_cast<size_t>(m_config.activeSampleIdx)) {
            AudioSample* sample = pad->soundPlayer().player.at(m_config.activeSampleIdx);
            LoopRegion region = sample->loopRegion();
            region.loop = on;
            applyLoopRegion(sample, region);
        }
    });
    connect(m_pan, &QSlider::valueChanged, this, [this](int v) {
        if (m_updatingControls) return;
        if (auto* pad = activePad(); pad && pad->soundPlayer().player.size() > static_cast<size_t>(m_config.activeSampleIdx)) {
            auto* sample = pad->soundPlayer().player.at(m_config.activeSampleIdx);
            if (sample->audioPlayer->canPan()) {
                sample->setPan((v / 1000.0f) * 2.0f - 1.0f);
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
            pad->soundPlayer().player.at(m_config.activeSampleIdx)->setGain(
                VolumeDb::toLinear(VolumeDb::fromSlider(v, kSampleGainMinDb)));
        }
    });
    connect(m_randomPan, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updatingControls) return;
        if (auto* pad = activePad()) {
            pad->soundPlayer().setRandomPan(on);
        }
    });
    connect(m_width, &QSlider::valueChanged, this, [this](int v) {
        if (m_updatingControls) return;
        if (auto* pad = activePad(); pad && pad->soundPlayer().player.size() > static_cast<size_t>(m_config.activeSampleIdx)) {
            pad->soundPlayer().player.at(m_config.activeSampleIdx)->setWidth(v / 100.0f);
        }
    });
    connect(m_addSample, &QPushButton::clicked, this, [this]() {
        if (auto* pad = activePad()) {
            const QString start = m_config.loadDialogDir(pad->currentSamplePath());
            const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Load files"), start,
                AudioFormats::dialogFilters().join(QStringLiteral(";;")));
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
        } else if (m_sidebar == SidebarView::Editor) {
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
        connect(pad, &SoundPadWidget::padDropped, this, [this, pad](int from, int to, bool copy) {
            if (pad->hasSounds()) {
                // Onto a pad with sounds: add to them rather than replace them.
                mergePad(from, to, copy);
            } else if (copy) {
                copyPad(from, to);
            } else {
                movePad(from, to);
            }
        });
        connect(pad, &SoundPadWidget::filesDropped, this, [this]() { updateMainControls(); });
        connect(pad, &SoundPadWidget::scenePadChanged, this, [this, pad]() {
            if (pad == activePad() && m_scenePad) {
                const bool was = m_updatingControls;
                m_updatingControls = true;
                m_scenePad->setChecked(pad->isScenePad());
                m_updatingControls = was;
            }
        });
        connect(pad, &SoundPadWidget::sampleLoopChanged, this, [this, pad]() {
            if (pad == activePad()) {
                updateEditControls();
            }
        });
        connect(&pad->soundPlayer(), &SoundPlayer::panRandomised, this, [this, pad](int sampleIndex, float) {
            if (pad == activePad() && sampleIndex == m_config.activeSampleIdx
                && sampleIndex < static_cast<int>(pad->soundPlayer().player.size())) {
                updatePanControl(pad->soundPlayer().player.at(sampleIndex));
            }
        });
        connect(pad, &SoundPadWidget::loadStateChanged, this, [this, pad]() {
            refreshLoadUi();
            if (pad == activePad() && m_sidebar == SidebarView::Editor) {
                refreshEditorPage();
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
    // Opening a project, restoring a backup, adding or deleting a scene, or stepping through
    // scenes with the keys: the active scene may be below or above what the list shows.
    scrollToSceneRow(m_scenes[idx]->row());
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
        for (int bus = 0; bus < OpenALSoundPlayer::kSendCount; ++bus) {
            m_sendSliders[bus]->setValue(static_cast<int>(pad->sendLevel(bus) * 1000.0f));
        }
        m_randomPlayback->setChecked(pad->soundPlayer().isPlayingRandom());
        m_randomPlayback->setVisible(pad->soundPlayer().player.size() > 1);
        m_repeat->setChecked(pad->isRepeating());
        m_repeat->setEnabled(true);
        m_infoPad = nullptr;
        m_infoSound = -1;
        refreshSampleInfo();
        const bool delayOn = pad->soundPlayer().isDelayEnabled();
        m_delayOn->setChecked(delayOn);
        m_delayOn->setEnabled(true);
        m_resetDelay->setEnabled(true);
        m_scenePad->setChecked(pad->isScenePad());
        m_scenePad->setEnabled(true);
        m_minDelay->setEnabled(delayOn);
        m_maxDelay->setEnabled(delayOn);
        for (int bus = 0; bus < OpenALSoundPlayer::kSendCount; ++bus) {
            m_sendSliders[bus]->setEnabled(OpenALSoundPlayer::sendAvailable(bus));
        }
    } else {
        m_infoLabel->setText(tr("channels: —\nformat: —\nsub-format: —\nsample rate: —\npath: —\nNum sounds: —"));
        m_randomPlayback->hide();
        m_repeat->setChecked(false);
        m_repeat->setEnabled(false);
        m_delayOn->setEnabled(false);
        m_resetDelay->setEnabled(false);
        m_scenePad->setChecked(false);
        m_scenePad->setEnabled(false);
        m_minDelay->setEnabled(false);
        m_maxDelay->setEnabled(false);
        for (QSlider* slider : m_sendSliders) {
            slider->setEnabled(false);
        }
    }
    m_updatingControls = false;

    for (Scene* scene : m_scenes) {
        for (SoundPadWidget* p : scene->pads) {
            p->setSelected(scene == activeScene() && p->padId() == m_config.activeSoundIdx);
        }
    }
}

void MainWindow::refreshSampleInfo()
{
    auto* pad = activePad();
    if (!m_infoLabel || !pad || !pad->isLoaded() || pad->soundPlayer().player.empty()) {
        return;
    }
    const SoundPlayer& player = pad->soundPlayer();
    const int cur = std::clamp(player.getCurSound(), 0, static_cast<int>(player.player.size()) - 1);
    if (pad == m_infoPad && cur == m_infoSound) {
        return;
    }
    m_infoPad = pad;
    m_infoSound = cur;
    auto* audio = player.player[static_cast<size_t>(cur)]->audioPlayer;
    QString path;
    if (cur < static_cast<int>(pad->soundPaths().size())) {
        path = QString::fromStdString(pad->soundPaths()[static_cast<size_t>(cur)]);
    }
    m_infoLabel->setText(tr("channels: %1\nformat: %2\nsub-format: %3\nsample rate: %4\npath: %5\nNum sounds: %6")
        .arg(audio->getNumChannels())
        .arg(QString::fromStdString(audio->getFormatString()))
        .arg(QString::fromStdString(audio->getSubFormatString()))
        .arg(audio->getSampleRate())
        .arg(path)
        .arg(player.player.size()));
}

void MainWindow::refreshDelayReadout()
{
    if (!m_delayReadout) {
        return;
    }
    auto* pad = activePad();
    QString text;
    if (!pad || !pad->isLoaded() || pad->soundPlayer().player.empty()) {
        text = QStringLiteral("—");
    } else {
        const SoundPlayer& player = pad->soundPlayer();
        if (!player.isDelayEnabled()) {
            text = tr("Off");
        } else if (!player.hasDelay()) {
            text = tr("No delay set");
        } else if (player.isPlayingDelay()) {
            text = tr("Next in %1 s of %2 s").arg(player.getRemainingDelay(), 0, 'f', 1).arg(player.getTotalDelay(), 0, 'f', 2);
        } else {
            text = tr("Current delay %1 s").arg(player.getTotalDelay(), 0, 'f', 2);
        }
    }
    if (m_delayReadout->text() != text) {
        m_delayReadout->setText(text);
    }
}

void MainWindow::refreshWaveform()
{
    // Only does work while the tab is on screen. Reads positions the same way the
    // sample rows do (no stream locks), so it cannot hold up playback.
    if (!m_waveform || m_bottomCollapsed || !m_bottomStack || m_bottomStack->currentWidget() != m_waveform) {
        return;
    }
    auto* pad = activePad();
    const int cur = waveformSampleIndex();
    if (!pad || cur < 0) {
        m_waveform->setSample(QString(), 0.0f);
        return;
    }
    const SoundPlayer& player = pad->soundPlayer();
    const OpenALSoundPlayer* audio = player.player[static_cast<size_t>(cur)]->audioPlayer;
    if (!audio || !audio->isLoaded()) {
        m_waveform->setSample(QString(), 0.0f);
        return;
    }
    QString path;
    if (cur < static_cast<int>(pad->soundPaths().size())) {
        path = QString::fromStdString(pad->soundPaths()[static_cast<size_t>(cur)]);
    } else {
        path = QString::fromStdString(audio->getFilePath().string());
    }
    m_waveform->setSample(path, audio->getDuration());
    if (!m_waveform->isDraggingHandle()) {
        m_loopDragSampleId = -1; // a drag that ended without a release (the sample changed)
    }
    // A selected sample that isn't the one playing shows a still playhead.
    const bool playing = cur == player.getCurSound() && player.isPlaying() && !player.isPlayingDelay();
    m_waveform->setPlayhead(audio->getAudiblePosition(), playing);

    const double duration = audio->getDuration();
    const LoopRegion region = audio->getLoopRegion();
    WaveformWidget::LoopView view;
    if (duration > 0.0) {
        view.startPct = std::clamp(region.start / duration, 0.0, 1.0);
        view.endPct = region.end > 0.0 ? std::clamp(region.end / duration, view.startPct, 1.0) : 1.0;
        // Clamped to half the loop, as the player does.
        const double xf = std::min(std::clamp(region.crossfadeMs, 0.0, LoopRegion::kMaxCrossfadeMs) / 1000.0,
                                   (view.endPct - view.startPct) * duration / 2.0);
        view.crossfadePct = std::max(0.0, xf / duration);
    }
    view.loop = region.loop;
    view.playFromStart = region.playFromStart;
    view.playToEnd = region.playToEnd;
    view.crossfadeMs = static_cast<int>(std::lround(region.crossfadeMs));
    m_waveform->setLoopView(view);
}

// The sample selected in the Editor (which follows the playing sample while the pad plays),
// or the pad's current one if the selection is out of range.
int MainWindow::waveformSampleIndex() const
{
    auto* pad = const_cast<MainWindow*>(this)->activePad();
    if (!pad || !pad->isLoaded() || pad->soundPlayer().player.empty()) {
        return -1;
    }
    const SoundPlayer& player = pad->soundPlayer();
    const int count = static_cast<int>(player.player.size());
    if (m_config.activeSampleIdx >= 0 && m_config.activeSampleIdx < count) {
        return m_config.activeSampleIdx;
    }
    return std::clamp(player.getCurSound(), 0, count - 1);
}

AudioSample* MainWindow::waveformSample() const
{
    const int index = waveformSampleIndex();
    if (index < 0) {
        return nullptr;
    }
    return const_cast<MainWindow*>(this)->activePad()->soundPlayer().player[static_cast<size_t>(index)];
}

void MainWindow::applyLoopRegion(AudioSample* sample, const LoopRegion& region)
{
    if (!sample) {
        return;
    }
    sample->setLoopRegion(region);
    // The Editor shows the same settings for its selected sample.
    auto* pad = activePad();
    if (pad && m_config.activeSampleIdx >= 0
        && m_config.activeSampleIdx < static_cast<int>(pad->soundPlayer().player.size())
        && pad->soundPlayer().player[static_cast<size_t>(m_config.activeSampleIdx)] == sample) {
        const bool wasUpdating = m_updatingControls;
        m_updatingControls = true;
        m_sampleLoop->setChecked(region.loop);
        m_updatingControls = wasUpdating;
    }
    refreshWaveform();
}

void MainWindow::setBottomTab(int index)
{
    if (!m_bottomStack) {
        return;
    }
    m_bottomStack->setCurrentIndex(index);
    refreshTabButton(m_sampleTab, index == 0);
    refreshTabButton(m_padTab, index == 1);
    refreshTabButton(m_waveTab, index == 2);
    if (m_bottomCollapsed) {
        setBottomCollapsed(false);
    }
}

void MainWindow::setBottomCollapsed(bool collapsed, bool resizeWindow)
{
    const bool changed = m_bottomCollapsed != collapsed;
    m_bottomCollapsed = collapsed;
    const bool resizing = changed && resizeWindow && m_bottomPageHeight > 0 && isVisible()
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
    if (m_bottomStack) {
        m_bottomStack->setVisible(!collapsed);
    }
    if (resizing) {
        // Apply the panel change to the layout now, while painting is held, so the window
        // resize below is the only layout pass anyone sees.
        settleLayouts();
        resize(width(), height() + (collapsed ? -m_bottomPageHeight : m_bottomPageHeight));
    }
    if (m_collapseBottom) {
        m_collapseBottom->setText(collapsed ? QStringLiteral("\u25BE") : QStringLiteral("\u25B4"));
        m_collapseBottom->setToolTip(collapsed ? tr("Show panel") : tr("Hide panel"));
    }
    if (collapsed) {
        refreshTabButton(m_sampleTab, false);
        refreshTabButton(m_padTab, false);
        refreshTabButton(m_waveTab, false);
    } else if (m_bottomStack) {
        refreshTabButton(m_sampleTab, m_bottomStack->currentIndex() == 0);
        refreshTabButton(m_padTab, m_bottomStack->currentIndex() == 1);
        refreshTabButton(m_waveTab, m_bottomStack->currentIndex() == 2);
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
    const bool stereo = sample->audioPlayer->getNumChannels() == 2;
    m_width->setVisible(stereo);
    m_widthValue->setVisible(stereo);
    m_widthLabel->setVisible(stereo);
    m_width->setValue(static_cast<int>(std::lround(sample->getWidth() * 100.0f)));
    updatePanControl(sample);
    m_pitch->setValue(static_cast<int>(sample->getPitch() * 1000.0f));
    m_gain->setValue(std::clamp(
        VolumeDb::toSlider(VolumeDb::toDb(sample->getGain()), kSampleGainMinDb),
        m_gain->minimum(),
        m_gain->maximum()));
    m_randomPan->setChecked(pad->soundPlayer().isRandomPan());
    const LoopRegion region = sample->loopRegion();
    m_sampleLoop->setChecked(region.loop);
    m_editTitle->setText(pad->soundName().toUpper());
    m_updatingControls = false;
}

void MainWindow::updatePanControl(AudioSample* sample)
{
    const bool pannable = sample->audioPlayer->canPan();
    m_pan->setVisible(pannable);
    m_panValue->setVisible(pannable);
    m_panLabel->setVisible(pannable);
    m_randomPan->setVisible(pannable);
    const bool wasUpdating = m_updatingControls;
    m_updatingControls = true;
    m_pan->setValue(static_cast<int>(((sample->getPan() + 1.0f) / 2.0f) * 1000.0f));
    m_updatingControls = wasUpdating;
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
        const auto selectSample = [this, pad](int id, bool play) {
            for (int i = 0; i < static_cast<int>(pad->soundPlayer().player.size()); ++i) {
                if (pad->soundPlayer().player[i]->id == id) {
                    m_config.activeSampleId = id;
                    m_config.activeSampleIdx = i;
                    if (play) {
                        pad->soundPlayer().playSample(i);
                    }
                    updateEditControls();
                    break;
                }
            }
        };
        connect(row, &SampleRowWidget::clicked, this, [selectSample](int id) { selectSample(id, false); });
        connect(row, &SampleRowWidget::doubleClicked, this, [selectSample](int id) { selectSample(id, true); });
    }
    updateEditControls();
    updateMainControls();
}

namespace {
// A rounded "card" grouping related settings: title, optional one-line hint, then
// label/field rows in a two-column grid.
struct SettingsCard {
    QFrame* frame = nullptr;
    QGridLayout* grid = nullptr;
    int row = 0;

    void addRow(const QString& label, QWidget* field, const QString& hint = QString())
    {
        auto* caption = new QLabel(label, frame);
        grid->addWidget(caption, row, 0, Qt::AlignLeft | Qt::AlignVCenter);
        grid->addWidget(field, row, 1);
        ++row;
        if (!hint.isEmpty()) {
            auto* note = new QLabel(hint, frame);
            note->setObjectName(QStringLiteral("FieldHint"));
            note->setWordWrap(true);
            grid->addWidget(note, row, 1);
            ++row;
        }
    }

    void addFullRow(QWidget* widget)
    {
        grid->addWidget(widget, row, 0, 1, 2);
        ++row;
    }
};

SettingsCard makeCard(QWidget* parent, const QString& title, const QString& hint = QString())
{
    SettingsCard card;
    card.frame = new QFrame(parent);
    card.frame->setObjectName(QStringLiteral("SettingsCard"));
    card.frame->setAttribute(Qt::WA_StyledBackground, true);
    auto* layout = new QVBoxLayout(card.frame);
    layout->setContentsMargins(20, 18, 20, 20);
    layout->setSpacing(4);
    auto* heading = new QLabel(title, card.frame);
    heading->setObjectName(QStringLiteral("CardTitle"));
    layout->addWidget(heading);
    if (!hint.isEmpty()) {
        auto* note = new QLabel(hint, card.frame);
        note->setObjectName(QStringLiteral("CardHint"));
        note->setWordWrap(true);
        layout->addWidget(note);
    }
    layout->addSpacing(12);
    card.grid = new QGridLayout();
    card.grid->setContentsMargins(0, 0, 0, 0);
    card.grid->setHorizontalSpacing(16);
    card.grid->setVerticalSpacing(12);
    card.grid->setColumnMinimumWidth(0, 150);
    card.grid->setColumnStretch(1, 1);
    layout->addLayout(card.grid);
    return card;
}

// A scrolling column of cards, kept to a readable width and left-aligned.
QVBoxLayout* makeCardColumn(QWidget* page, QVBoxLayout* pageLayout)
{
    auto* scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* wrapper = new QWidget(scroll);
    wrapper->setObjectName(QStringLiteral("SettingsPage"));
    wrapper->setAttribute(Qt::WA_StyledBackground, true);
    auto* row = new QHBoxLayout(wrapper);
    row->setContentsMargins(0, 0, 12, 0);
    auto* column = new QWidget(wrapper);
    column->setObjectName(QStringLiteral("SettingsPage"));
    column->setMaximumWidth(760);
    auto* cards = new QVBoxLayout(column);
    cards->setContentsMargins(0, 4, 0, 16);
    cards->setSpacing(16);
    row->addWidget(column, 1);
    row->addStretch(0);
    scroll->setWidget(wrapper);
    pageLayout->addWidget(scroll, 1);
    return cards;
}

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

QWidget* MainWindow::buildPageHeader(QWidget* page, const QString& title, const QString& subtitle)
{
    auto* header = new QWidget(page);
    auto* row = new QHBoxLayout(header);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(16);
    auto* text = new QVBoxLayout();
    text->setSpacing(2);
    auto* heading = new QLabel(title, header);
    heading->setObjectName(QStringLiteral("SettingsTitle"));
    text->addWidget(heading);
    auto* sub = new QLabel(subtitle, header);
    sub->setObjectName(QStringLiteral("PageSubtitle"));
    text->addWidget(sub);
    row->addLayout(text, 1);
    auto* back = new QPushButton(tr("Back to scenes"), header);
    back->setCursor(Qt::PointingHandCursor);
    back->setToolTip(tr("Return to the pad grid (Ctrl+1)"));
    connect(back, &QPushButton::clicked, this, [this]() {
        setPage(Page::Main);
        setSidebarView(SidebarView::Scenes);
    });
    row->addWidget(back, 0, Qt::AlignTop);
    return header;
}

void MainWindow::buildSettingsPage()
{
    m_settingsPage = new QWidget(m_pages);
    m_settingsPage->setObjectName(QStringLiteral("SettingsPage"));
    m_settingsPage->setAttribute(Qt::WA_StyledBackground, true);
    auto* outer = new QVBoxLayout(m_settingsPage);
    outer->setContentsMargins(32, 24, 20, 0);
    outer->setSpacing(20);
    outer->addWidget(buildPageHeader(m_settingsPage, tr("Settings"),
        tr("How new scenes and pads start out, where samples live, and the reverb.")));
    QVBoxLayout* cards = makeCardColumn(m_settingsPage, outer);
    QWidget* host = cards->parentWidget();

    SettingsCard startup = makeCard(host, tr("Settings file"),
        tr("The file open when Feedra quits is remembered for the next launch."));
    m_loadLastSettings = new QCheckBox(tr("Load the last settings file on startup"), startup.frame);
    startup.addRow(tr("On startup"), m_loadLastSettings);
    m_autosave = new QCheckBox(tr("Save scenes and pads on quit"), startup.frame);
    startup.addRow(tr("Autosave"), m_autosave,
        tr("Off: changes made while playing, such as switching a sample's loop from its pad, are "
           "kept only when you save. App settings are still saved on quit."));
    cards->addWidget(startup.frame);

    // Scenes and pads
    SettingsCard scenes = makeCard(host, tr("Scenes and pads"),
        tr("Defaults for new scenes and pads. Existing scenes keep their grid."));

    m_sceneLimit = new QSpinBox(scenes.frame);
    m_sceneLimit->setRange(1, 64);
    m_sceneLimit->setMaximumWidth(120);
    scenes.addRow(tr("Scene limit"), m_sceneLimit);

    m_gridColumns = new QSpinBox(scenes.frame);
    m_gridColumns->setRange(1, 12);
    m_gridColumns->setMaximumWidth(120);
    m_gridColumns->setSuffix(tr(" columns"));
    m_gridRows = new QSpinBox(scenes.frame);
    m_gridRows->setRange(1, 8);
    m_gridRows->setMaximumWidth(120);
    m_gridRows->setSuffix(tr(" rows"));
    auto* gridSize = new QWidget(scenes.frame);
    auto* gridSizeRow = new QHBoxLayout(gridSize);
    gridSizeRow->setContentsMargins(0, 0, 0, 0);
    gridSizeRow->setSpacing(8);
    gridSizeRow->addWidget(m_gridColumns);
    auto* times = new QLabel(QStringLiteral("×"), gridSize);
    times->setObjectName(QStringLiteral("FieldHint"));
    gridSizeRow->addWidget(times);
    gridSizeRow->addWidget(m_gridRows);
    gridSizeRow->addStretch();
    scenes.addRow(tr("Pad grid"), gridSize, tr("Applies to scenes added after the change."));
    cards->addWidget(scenes.frame);

    // Library
    SettingsCard library = makeCard(host, tr("Sample library"),
        tr("Where the Load button starts looking for audio files."));
    m_libraryPath = new QLineEdit(library.frame);
    m_libraryPath->setPlaceholderText(tr("Choose a folder"));
    auto* browse = new QPushButton(tr("Browse..."), library.frame);
    auto* libraryRow = new QWidget(library.frame);
    auto* libraryLayout = new QHBoxLayout(libraryRow);
    libraryLayout->setContentsMargins(0, 0, 0, 0);
    libraryLayout->setSpacing(8);
    libraryLayout->addWidget(m_libraryPath, 1);
    libraryLayout->addWidget(browse);
    library.addRow(tr("Folder"), libraryRow);

    auto* cacheRow = new QWidget(library.frame);
    auto* cacheLayout = new QHBoxLayout(cacheRow);
    cacheLayout->setContentsMargins(0, 0, 0, 0);
    cacheLayout->setSpacing(8);
    m_waveformCacheInfo = new QLabel(cacheRow);
    auto* clearCache = new QPushButton(tr("Clear"), cacheRow);
    clearCache->setToolTip(tr("Delete the saved waveforms"));
    cacheLayout->addWidget(m_waveformCacheInfo, 1);
    cacheLayout->addWidget(clearCache);
    library.addRow(tr("Waveform cache"), cacheRow,
        tr("Saved waveforms, so they show as soon as a sample loads. Clearing is safe: "
           "each is worked out and saved again the next time its sample is shown."));
    connect(clearCache, &QPushButton::clicked, this, [this]() {
        const PeakStore::CacheStats removed = PeakStore::instance().clearCache();
        const QString size = QLocale().formattedDataSize(removed.bytes);
        m_waveformCacheInfo->setText(removed.files == 0 ? tr("Nothing to clear")
            : removed.files == 1                        ? tr("Cleared 1 waveform, %1").arg(size)
                                                        : tr("Cleared %1 waveforms, %2").arg(removed.files).arg(size));
    });
    cards->addWidget(library.frame);

    // Reverb
    SettingsCard reverb = makeCard(host, tr("Reverb"),
        tr("Each pad has a send into each of these four reverbs, on its Pad tab."));
    for (int which = 0; which < OpenALSoundPlayer::kEaxReverbCount; ++which) {
        m_reverbPreset[which] = new QComboBox(reverb.frame);
        m_reverbPreset[which]->setMaxVisibleItems(24);
        m_reverbPreset[which]->setMaximumWidth(320);
        for (int i = 0; i < OpenALSoundPlayer::reverbPresetCount(); ++i) {
            m_reverbPreset[which]->addItem(
                QString::fromStdString(OpenALSoundPlayer::reverbPresetLabel(i)),
                QString::fromStdString(OpenALSoundPlayer::reverbPresetId(i)));
        }
        reverb.addRow(tr("EAX reverb %1 preset").arg(which + 1), m_reverbPreset[which],
            tr("Used by each pad's EAX reverb %1 send, which is named after the preset.").arg(which + 1));
    }

    const bool convolution = OpenALSoundPlayer::convolutionAvailable();
    for (int which = 0; which < OpenALSoundPlayer::kConvolutionCount; ++which) {
        m_convolutionGain[which] = new QSlider(Qt::Horizontal, reverb.frame);
        m_convolutionGain[which]->setRange(0, VolumeDb::sliderSpan(VolumeDb::kFloorDb, VolumeDb::kUnityDb));
        m_convolutionGain[which]->setToolTip(tr("Output level of this convolution reverb. The bundled impulse is loud, so the default is low."));
        m_convolutionGainValue[which] = new QLabel(reverb.frame);
        m_convolutionGainValue[which]->setObjectName(QStringLiteral("MainVolumeValue"));
        m_convolutionGainValue[which]->setMinimumWidth(72);
        m_convolutionGainValue[which]->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_convolutionGain[which]->setValue(VolumeDb::toSliderClamped(
            OpenALSoundPlayer::convolutionGain(which), VolumeDb::kFloorDb, VolumeDb::kUnityDb));
        m_convolutionGainValue[which]->setText(VolumeDb::format(
            VolumeDb::fromSlider(m_convolutionGain[which]->value(), VolumeDb::kFloorDb)));
        auto* convolutionRow = new QWidget(reverb.frame);
        auto* convolutionLayout = new QHBoxLayout(convolutionRow);
        convolutionLayout->setContentsMargins(0, 0, 0, 0);
        convolutionLayout->addWidget(m_convolutionGain[which], 1);
        convolutionLayout->addWidget(m_convolutionGainValue[which]);
        reverb.addRow(tr("Convolution %1 level").arg(which + 1), convolutionRow);

        m_impulsePath[which] = new QLineEdit(reverb.frame);
        m_impulseBrowse[which] = new QPushButton(tr("Browse..."), reverb.frame);
        auto* impulseRow = new QWidget(reverb.frame);
        auto* impulseLayout = new QHBoxLayout(impulseRow);
        impulseLayout->setContentsMargins(0, 0, 0, 0);
        impulseLayout->setSpacing(8);
        impulseLayout->addWidget(m_impulsePath[which], 1);
        impulseLayout->addWidget(m_impulseBrowse[which]);
        reverb.addRow(tr("Convolution %1 impulse").arg(which + 1), impulseRow,
            convolution ? tr("A recording of a space. Used by each pad's convolution %1 send, which is named after the file.").arg(which + 1)
                        : tr("Convolution reverb isn't available with the current audio device."));
        m_convolutionGain[which]->setEnabled(convolution);
        m_impulsePath[which]->setEnabled(convolution);
        m_impulseBrowse[which]->setEnabled(convolution);
    }
    cards->addWidget(reverb.frame);
    cards->addStretch(1);

    connect(m_loadLastSettings, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updatingControls) {
            return;
        }
        m_config.loadLastSettings = on;
    });
    connect(m_autosave, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updatingControls) {
            return;
        }
        m_config.autosave = on;
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
        // Clearing the field is allowed: the load dialogs then fall back to the settings folder.
        m_config.defaultLibraryLocation = m_libraryPath->text().trimmed();
    });
    connect(browse, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getExistingDirectory(this, tr("Sample library"),
            m_config.loadDialogDir());
        if (path.isEmpty()) {
            return;
        }
        m_libraryPath->setText(path);
        m_config.defaultLibraryLocation = path;
    });
    for (int which = 0; which < OpenALSoundPlayer::kEaxReverbCount; ++which) {
        connect(m_reverbPreset[which], qOverload<int>(&QComboBox::currentIndexChanged), this, [this, which](int index) {
            if (!m_updatingControls) {
                OpenALSoundPlayer::setReverbPreset(index, which);
                refreshSendLabels();
            }
        });
    }
    for (int which = 0; which < OpenALSoundPlayer::kConvolutionCount; ++which) {
        connect(m_convolutionGain[which], &QSlider::valueChanged, this, [this, which](int value) {
            const float db = VolumeDb::fromSlider(value, VolumeDb::kFloorDb);
            m_convolutionGainValue[which]->setText(VolumeDb::format(db));
            if (!m_updatingControls) {
                OpenALSoundPlayer::setConvolutionGain(VolumeDb::toLinearMuted(db), which);
            }
        });
        connect(m_impulsePath[which], &QLineEdit::editingFinished, this, [this, which]() {
            if (m_updatingControls) {
                return;
            }
            applyImpulsePath(m_impulsePath[which]->text().trimmed(), which);
        });
        connect(m_impulseBrowse[which], &QPushButton::clicked, this, [this, which]() {
            const QString start = m_impulsePath[which]->text().isEmpty()
                ? defaultImpulsePath(m_config)
                : m_impulsePath[which]->text();
            const QString path = QFileDialog::getOpenFileName(this, tr("Impulse response"), start,
                tr("Audio (*.wav *.aif *.aiff *.flac *.ogg);;All files (*.*)"));
            if (path.isEmpty()) {
                return;
            }
            m_impulsePath[which]->setText(path);
            applyImpulsePath(path, which);
        });
    }
}

void MainWindow::buildThemePage()
{
    m_themePage = new QWidget(m_pages);
    m_themePage->setObjectName(QStringLiteral("ThemePage"));
    m_themePage->setAttribute(Qt::WA_StyledBackground, true);
    auto* outer = new QVBoxLayout(m_themePage);
    outer->setContentsMargins(32, 24, 20, 0);
    outer->setSpacing(20);
    outer->addWidget(buildPageHeader(m_themePage, tr("Theme"),
        tr("Pick a look, then an accent colour for everything that is playing.")));
    QVBoxLayout* cards = makeCardColumn(m_themePage, outer);
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
            refreshThemeSwatches(); // restore the checked state the click toggled
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
    connect(&Theme::instance(), &Theme::changed, this, &MainWindow::refreshThemeSwatches);
}

void MainWindow::applyImpulsePath(const QString& path, int which)
{
    const QString chosen = path.isEmpty() ? defaultImpulsePath(m_config) : path;
    if (toImpulsePath(chosen) == OpenALSoundPlayer::convolutionImpulsePath(which)) {
        m_impulsePath[which]->setText(chosen);
        return;
    }
    if (!OpenALSoundPlayer::setConvolutionImpulse(toImpulsePath(chosen), which)) {
        QMessageBox::warning(this, tr("Feedra"),
            tr("Could not load the impulse response:\n%1").arg(chosen));
    }
    m_impulsePath[which]->setText(fromImpulsePath(OpenALSoundPlayer::convolutionImpulsePath(which)));
    refreshSendLabels();
}

void MainWindow::refreshSendLabels()
{
    for (int which = 0; which < OpenALSoundPlayer::kEaxReverbCount; ++which) {
        if (!m_sendLabels[which]) {
            continue;
        }
        const QString preset = QString::fromStdString(
            OpenALSoundPlayer::reverbPresetLabel(OpenALSoundPlayer::reverbPresetIndex(which)));
        m_sendLabels[which]->setText(tr("%1 reverb send").arg(preset));
        m_sendLabels[which]->setToolTip(tr("Send into EAX reverb %1 (%2). Choose the preset in Settings.").arg(which + 1).arg(preset));
    }
    for (int which = 0; which < OpenALSoundPlayer::kConvolutionCount; ++which) {
        QLabel* label = m_sendLabels[OpenALSoundPlayer::kEaxReverbCount + which];
        if (!label) {
            continue;
        }
        const QString file = fromImpulsePath(OpenALSoundPlayer::convolutionImpulsePath(which));
        const QString name = file.isEmpty() ? tr("Convolution %1").arg(which + 1) : QFileInfo(file).completeBaseName();
        label->setText(tr("%1 reverb send").arg(name));
        label->setToolTip(tr("Send into convolution reverb %1 (%2). Choose the impulse in Settings.").arg(which + 1).arg(file));
    }
}

void MainWindow::refreshWaveformCacheInfo()
{
    if (!m_waveformCacheInfo) {
        return;
    }
    const PeakStore::CacheStats stats = PeakStore::instance().cacheStats();
    const QString size = QLocale().formattedDataSize(stats.bytes);
    m_waveformCacheInfo->setText(stats.files == 0 ? tr("Empty")
        : stats.files == 1                        ? tr("1 waveform, %1").arg(size)
                                                  : tr("%1 waveforms, %2").arg(stats.files).arg(size));
}

void MainWindow::syncSettingsPage()
{
    m_updatingControls = true;
    m_loadLastSettings->setChecked(m_config.loadLastSettings);
    m_autosave->setChecked(m_config.autosave);
    m_sceneLimit->setValue(static_cast<int>(m_config.maxScenes));
    m_gridColumns->setValue(m_config.gridWidth);
    m_gridRows->setValue(m_config.gridHeight);
    m_libraryPath->setText(m_config.defaultLibraryLocation);
    for (int which = 0; which < OpenALSoundPlayer::kEaxReverbCount; ++which) {
        m_reverbPreset[which]->setCurrentIndex(OpenALSoundPlayer::reverbPresetIndex(which));
    }
    for (int which = 0; which < OpenALSoundPlayer::kConvolutionCount; ++which) {
        m_convolutionGain[which]->setValue(VolumeDb::toSliderClamped(
            OpenALSoundPlayer::convolutionGain(which), VolumeDb::kFloorDb, VolumeDb::kUnityDb));
        m_convolutionGainValue[which]->setText(VolumeDb::format(
            VolumeDb::fromSlider(m_convolutionGain[which]->value(), VolumeDb::kFloorDb)));
        m_impulsePath[which]->setText(fromImpulsePath(OpenALSoundPlayer::convolutionImpulsePath(which)));
    }
    refreshSendLabels();
    m_updatingControls = false;
    refreshThemeSwatches();
    if (m_addScene) {
        m_addScene->setEnabled(static_cast<unsigned int>(m_scenes.size()) < m_config.maxScenes);
    }
}

void MainWindow::refreshThemeSwatches()
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

void MainWindow::applyAppSettings(const QJsonObject& global)
{
    if (global.contains(QStringLiteral("autosave"))) {
        m_config.autosave = global.value(QStringLiteral("autosave")).toBool(true);
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
    if (global.contains(QStringLiteral("library"))) {
        m_config.defaultLibraryLocation = global.value(QStringLiteral("library")).toString().trimmed();
    }
    // Keys for the first reverbs are the original ones; the second ones add a "2".
    for (int which = 0; which < OpenALSoundPlayer::kEaxReverbCount; ++which) {
        const QString suffix = which == 0 ? QString() : QString::number(which + 1);
        const QString reverb = global.value(QStringLiteral("reverb") + suffix).toString();
        if (!reverb.isEmpty()) {
            OpenALSoundPlayer::setReverbPresetById(reverb.toStdString(), which);
        }
    }
    for (int which = 0; which < OpenALSoundPlayer::kConvolutionCount; ++which) {
        const QString suffix = which == 0 ? QString() : QString::number(which + 1);
        if (global.contains(QStringLiteral("convolutiongain") + suffix)) {
            OpenALSoundPlayer::setConvolutionGain(static_cast<float>(global.value(QStringLiteral("convolutiongain") + suffix).toDouble(
                OpenALSoundPlayer::defaultConvolutionGain())), which);
        }
        QString impulse = global.value(QStringLiteral("convolutionir") + suffix).toString();
        if (impulse.isEmpty() || !QFile::exists(impulse)) {
            impulse = defaultImpulsePath(m_config);
        }
        if (toImpulsePath(impulse) != OpenALSoundPlayer::convolutionImpulsePath(which)) {
            if (!OpenALSoundPlayer::setConvolutionImpulse(toImpulsePath(impulse), which)) {
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
    if (m_bottomPanel) {
        m_bottomPanel->setVisible(page == Page::Main && !m_scenesFocus);
    }
    if (page == Page::Settings) {
        refreshWaveformCacheInfo();
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
    if (m_bottomPanel) {
        m_bottomPanel->setVisible(!on && m_page == Page::Main);
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
    refreshTabButton(m_scenesTab, view == SidebarView::Scenes);
    refreshTabButton(m_editorTab, view == SidebarView::Editor);
    refreshTabButton(m_filesTab, view == SidebarView::Files);
}

void MainWindow::refreshEditorPage()
{
    auto* pad = activePad();
    m_sidebarStack->setCurrentWidget(m_editorPage);
    if (!pad || pad->soundPlayer().player.empty()) {
        qDeleteAll(m_sampleRows);
        m_sampleRows.clear();
        m_sampleControls->hide();
        m_editTitle->setText(pad ? pad->soundName().toUpper() : QString());
        return;
    }
    if (m_sampleRows.isEmpty()) {
        m_config.activeSampleIdx = 0;
        m_config.activeSampleId = pad->soundPlayer().player[0]->id;
    }
    m_sampleControls->show();
    rebuildEditSamples();
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
        for (SoundPadWidget* pad : scene->pads) {
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
            for (SoundPadWidget* pad : scene->pads) {
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
    global.insert(QStringLiteral("mainvolume"), m_config.masterVolume());
    global.insert(QStringLiteral("maxscenes"), m_scenes.size());
    global.insert(QStringLiteral("activesceneid"), m_config.activeSceneIdx);
    global.insert(QStringLiteral("autosave"), m_config.autosave);
    global.insert(QStringLiteral("scenelimit"), static_cast<int>(m_config.maxScenes));
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
            QString::fromStdString(OpenALSoundPlayer::reverbPresetId(OpenALSoundPlayer::reverbPresetIndex(which))));
    }
    for (int which = 0; which < OpenALSoundPlayer::kConvolutionCount; ++which) {
        const QString suffix = which == 0 ? QString() : QString::number(which + 1);
        global.insert(QStringLiteral("convolutiongain") + suffix, static_cast<double>(OpenALSoundPlayer::convolutionGain(which)));
        global.insert(QStringLiteral("convolutionir") + suffix, fromImpulsePath(OpenALSoundPlayer::convolutionImpulsePath(which)));
    }
    global.insert(QStringLiteral("theme"), Theme::instance().idName());
    global.insert(QStringLiteral("themecolors"), Theme::instance().colorsJson());
    global.insert(QStringLiteral("themecolorsets"), Theme::instance().colorSetsJson());
    saveWindowLayout(global);
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
        for (SoundPadWidget* pad : scene->pads) {
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
                sample.insert(QStringLiteral("path"), projectRelativePath(path, projectDir));
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
        connectScene(scene);
        for (SoundPadWidget* pad : scene->pads) {
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
    // A copy as it was when opened, whatever happens to it later.
    ProjectBackups::backup(path, true);
    m_config.settingsPath = QFileInfo(path).absoluteFilePath();
    refreshSettingsPathLabel();
    const QJsonObject root = m_config.json();
    const QString projectDir = QFileInfo(path).absolutePath();
    m_reportMissingAfterLoad = true;
    const QJsonObject global = root.value(QStringLiteral("global")).toObject();
    applyAppSettings(global);
    m_mainVolume->setValue(VolumeDb::toSliderClamped(
        static_cast<float>(global.value(QStringLiteral("mainvolume")).toDouble(1.0)),
        VolumeDb::kFloorDb,
        VolumeDb::kUnityDb));
    m_config.setMasterVolume(VolumeDb::toLinearMuted(VolumeDb::fromSlider(m_mainVolume->value(), VolumeDb::kFloorDb)));
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
            pad->loadFromJson(root, projectDir);
        }
        m_scenes.push_back(scene);
        m_padStack->addWidget(scene->grid());
        m_sceneListLayout->insertWidget(m_sceneListLayout->count() - 1, scene->row());
    }
    m_addScene->setEnabled(static_cast<unsigned int>(m_scenes.size()) < m_config.maxScenes);
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
        pad->clearPad();
        updateMainControls();
        if (m_sidebar == SidebarView::Editor) {
            refreshEditorPage();
        }
    }
}

void MainWindow::copyActivePad()
{
    SoundPadWidget* pad = activePad();
    if (!pad || pad->isLoading() || !pad->isLoaded()) {
        return;
    }
    const SoundPadWidget::PadClip clip = pad->clip();
    if (clip.valid) {
        m_padClip = clip;
    }
}

void MainWindow::cutActivePad()
{
    SoundPadWidget* pad = activePad();
    if (!pad || pad->isLoading() || !pad->isLoaded()) {
        return;
    }
    const SoundPadWidget::PadClip clip = pad->clip();
    if (!clip.valid) {
        return;
    }
    m_padClip = clip;
    pad->clearPad();
    updateMainControls();
    if (m_sidebar == SidebarView::Editor) {
        refreshEditorPage();
    }
}

void MainWindow::pasteActivePad()
{
    SoundPadWidget* pad = activePad();
    if (!pad || !m_padClip.valid) {
        return;
    }
    pad->pasteClip(m_padClip);
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

// Dragging a pad onto another moves it: the target takes the pad's sounds and settings
// and the source is cleared.
void MainWindow::movePad(int fromIdx, int toIdx)
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
    copyPad(fromIdx, toIdx); // reads the source's samples before it is cleared
    from->clearPad();
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
    SoundPadWidget* from = scene->padAt(fromIdx);
    SoundPadWidget* to = scene->padAt(toIdx);
    if (!from || !to || from == to) {
        return;
    }
    const SoundPadWidget::PadClip clip = from->clip();
    if (!clip.valid) {
        return; // an empty pad adds nothing, and the target is left alone
    }
    to->appendClip(clip);
    if (!keepSource) {
        from->clearPad();
    }
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
    pad->removeSampleAt(m_config.activeSampleIdx);
    m_config.activeSampleIdx = std::max(0, m_config.activeSampleIdx - 1);
    refreshEditorPage();
}

void MainWindow::drainLoads(int budgetMs)
{
    if (!m_loads) {
        return;
    }
    m_loads->drain(budgetMs, [this](SampleLoadResult result) {
        if (SoundPadWidget* pad = findPad(result.job.sceneId, result.job.padId)) {
            if (result.job.reloadSampleId >= 0) {
                pad->submitReload(result.job, std::move(result.audio));
            } else {
                pad->submitDecoded(result.job.sampleIndex, result.job.generation, std::move(result.audio));
            }
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
    pollExport();
    reportMissingSamples();
    refreshLoadUi();
    OpenALSoundPlayer::updateAll();
    m_fileBrowser->tick();
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
    if (m_page == Page::Main) {
        // The selected sample follows the pad as it moves on to another sample, in every
        // view, so the Editor and the Waveform tab show the one playing.
        if (auto* pad = activePad()) {
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
                    m_config.activeSampleIdx = cur;
                    m_config.activeSampleId = player.player[static_cast<size_t>(cur)]->id;
                    updateEditControls();
                }
            }
        }
        refreshSampleInfo();
        refreshDelayReadout();
        refreshWaveform();
    }
    if (m_page == Page::Main && m_sidebar == SidebarView::Editor) {
        auto* pad = activePad();
        if (pad) {
            for (int i = 0; i < m_sampleRows.size() && i < static_cast<int>(pad->soundPlayer().player.size()); ++i) {
                m_sampleRows[i]->setProgress(pad->soundPlayer().player[i]->audioPlayer->getPosition());
                m_sampleRows[i]->setSelected(i == m_config.activeSampleIdx);
            }
            // The selected sample changed (arrow keys, or the pad moving on while it plays):
            // keep its row in view. After this tick, once a rebuilt list has been laid out.
            const int idx = m_config.activeSampleIdx;
            if ((pad != m_shownSamplePad || idx != m_shownSampleIdx) && idx >= 0 && idx < m_sampleRows.size()) {
                m_shownSamplePad = pad;
                m_shownSampleIdx = idx;
                const QPointer<QWidget> row(m_sampleRows[idx]);
                QTimer::singleShot(0, this, [this, row]() {
                    if (row && m_sampleScroll) {
                        m_sampleScroll->ensureWidgetVisible(row, 0, 6);
                    }
                });
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
    if (m_sidebar == SidebarView::Editor && padChanged) {
        setSidebarView(SidebarView::Editor);
    }
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
    if (m_bottomPanel && m_bottomPanel->layout()) {
        m_bottomPanel->layout()->invalidate();
        m_bottomPanel->layout()->activate();
        m_bottomPanel->updateGeometry();
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

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::KeyPress && watched->isWidgetType()
        && static_cast<QWidget*>(watched)->window() == this) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (handlePadClipKey(key) || handleReorderKey(key)) {
            return true;
        }
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

    if (m_sidebar == SidebarView::Files) {
        return false; // the arrows move through the file list
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
        m_config.activeSampleIdx = next;
        m_config.activeSampleId = pad->soundPlayer().player[static_cast<size_t>(next)]->id;
        updateEditControls();
    }
    return true;
}
