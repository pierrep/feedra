#pragma once

#include "AppConfig.h"
#include "widgets/SoundPadWidget.h"

#include <QByteArray>
#include <QRect>
#include <QMainWindow>
#include <QVector>

#include <memory>
#include <thread>

class AudioSample;
class Scene;
class SampleRowWidget;
class SampleLoadQueue;
class WaveformWidget;
class FileBrowserWidget;
class QAbstractButton;
class QAction;
class QFrame;
class QHBoxLayout;
class QProgressBar;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QJsonObject;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QStackedWidget;
class QVBoxLayout;
class QWidget;
struct ExportState;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum class Page { Main, Settings, Theme };
    enum class SidebarView { Scenes, Editor, Files };

    void buildUi();
    void buildMenus();
    void buildSettingsPage();
    void buildThemePage();
    void syncSettingsPage();
    void refreshWaveformCacheInfo();
    void applyImpulsePath(const QString& path, int which);
    // Pad tab send names follow the reverbs: "<preset> reverb send", "<impulse file> reverb send".
    void refreshSendLabels();
    void refreshThemeSwatches();
    QWidget* buildPageHeader(QWidget* page, const QString& title, const QString& subtitle);
    void applyAppSettings(const QJsonObject& global);
    void connectScene(Scene* scene);
    void drainLoads(int budgetMs);
    void refreshLoadUi();
    void waitForLoads();
    SoundPadWidget* findPad(int sceneId, int padId) const;
    void createDefaultScenes();
    void addNewScene();
    int nextSceneId() const;
    void importScenes();
    void importScenesFrom(const QString& path);
    void deleteScene(int sceneId);
    void enableScene(int idx);
    void moveScene(int fromIndex, int insertIndex);
    void updateSceneListLayout();
    bool handleReorderKey(QKeyEvent* event);
    bool handlePadClipKey(QKeyEvent* event);
    void copyActivePad();
    void cutActivePad();
    void pasteActivePad();
    void updateMainControls();
    void refreshSampleInfo();
    void refreshDelayReadout();
    void refreshWaveform();
    void updateEditControls();
    void updatePanControl(AudioSample* sample);
    void rebuildEditSamples();
    void refreshEditorPage();
    void moveEditorSample(int fromIndex, int insertIndex);
    void setPage(Page page);
    void setSidebarView(SidebarView view);
    // Scenes focus: hides the pad grid, the bottom panel and the Editor tab so only the
    // scene list shows, and shrinks the window to a narrow strip (restored when turned off).
    void setScenesFocus(bool on);
    void refreshSettingsPathLabel();
    QString currentSettingsFilePath() const;
    QString resolvedSettingsPath() const;
    void saveConfig();
    void saveOnExit();
    // Export project: writes the scenes to a new file and copies every sample into a "files"
    // folder beside it. The copying runs on a background thread; the file is written when
    // it finishes.
    void exportProject();
    bool isExporting() const;
    void pollExport();
    void cancelExport();
    // `settingsOnly` rewrites just the app settings in the file and leaves its scenes as they are.
    // With `exportTo`, samples are pointed at a "files" folder beside `path`, the copies they
    // need are added to it, and the file itself is not written (it's left in `exportTo->json`).
    bool saveConfigTo(const QString& path, bool copyFiles, bool settingsOnly = false, ExportState* exportTo = nullptr);
    // The sample the Waveform tab shows: the selected pad's current sample.
    AudioSample* waveformSample() const;
    void applyLoopRegion(AudioSample* sample, const LoopRegion& region);
    void loadConfig();
    void loadConfigFrom(const QString& path);
    void saveWindowLayout(QJsonObject& global) const;
    void restoreWindowLayout(const QJsonObject& global);
    void clearActivePad();
    void copyPad(int fromIdx, int toIdx);
    void movePad(int fromIdx, int toIdx);
    void clearActiveSample();
    void tick();
    void checkAudioDevice();
    void onPadClicked(int sceneId, int padId);
    void setBottomTab(int index);
    void setBottomCollapsed(bool collapsed, bool resizeWindow = true);
    void refreshTabButton(QPushButton* button, bool active);
    SoundPadWidget* activePad() const;
    Scene* activeScene() const;

    AppConfig m_config;
    QVector<Scene*> m_scenes;
    SoundPadWidget::PadClip m_padClip;
    Page m_page = Page::Main;
    SidebarView m_sidebar = SidebarView::Scenes;
    bool m_scenesFocus = false;
    QWidget* m_sidebarWidget = nullptr;
    QHBoxLayout* m_bodyLayout = nullptr;
    QAbstractButton* m_fullLayoutButton = nullptr;
    QAbstractButton* m_scenesFocusButton = nullptr;
    QHBoxLayout* m_headerRow = nullptr;
    QLabel* m_volumeCaption = nullptr;
    // The window before it became a strip: put back when scenes focus is turned off, and
    // saved on quit in its place so the next launch opens full size.
    QByteArray m_expandedGeometry;
    QRect m_expandedRect;
    bool m_expandedMaximized = false;

    QStackedWidget* m_padStack = nullptr;
    QWidget* m_sceneListHost = nullptr;
    QVBoxLayout* m_sceneListLayout = nullptr;
    QPushButton* m_addScene = nullptr;
    QStackedWidget* m_pages = nullptr;
    QStackedWidget* m_sidebarStack = nullptr;
    QWidget* m_mainPage = nullptr;
    QWidget* m_scenesPage = nullptr;
    QWidget* m_editorPage = nullptr;
    QWidget* m_sampleControls = nullptr;
    QWidget* m_settingsPage = nullptr;
    QWidget* m_themePage = nullptr;
    QPushButton* m_scenesTab = nullptr;
    QPushButton* m_editorTab = nullptr;
    QPushButton* m_filesTab = nullptr;
    FileBrowserWidget* m_fileBrowser = nullptr;
    QVBoxLayout* m_sampleListLayout = nullptr;
    QVector<SampleRowWidget*> m_sampleRows;
    SoundPadWidget* m_followedPad = nullptr;
    int m_followedSound = -1;

    QSlider* m_mainVolume = nullptr;
    QLabel* m_mainVolumeValue = nullptr;
    QWidget* m_bottomPanel = nullptr;
    QStackedWidget* m_bottomStack = nullptr;
    QPushButton* m_sampleTab = nullptr;
    QPushButton* m_padTab = nullptr;
    QPushButton* m_waveTab = nullptr;
    WaveformWidget* m_waveform = nullptr;
    QPushButton* m_collapseBottom = nullptr;
    int m_bottomPageHeight = 0;
    QSpinBox* m_minDelay = nullptr;
    QSpinBox* m_maxDelay = nullptr;
    // Pad effect sends: EAX reverb 1 and 2, then convolution reverb 1 and 2.
    QSlider* m_sendSliders[4] = {};
    QLabel* m_sendLabels[4] = {};
    QCheckBox* m_randomPlayback = nullptr;
    QCheckBox* m_repeat = nullptr;
    QCheckBox* m_delayOn = nullptr;
    QPushButton* m_resetDelay = nullptr;
    QLabel* m_delayReadout = nullptr;
    QLabel* m_infoLabel = nullptr;
    SoundPadWidget* m_infoPad = nullptr;
    int m_infoSound = -1;
    bool m_bottomCollapsed = false;
    // Set while the panel toggles and the window resizes to match, so the pad grid
    // isn't painted at the in-between size.
    bool m_holdGridPaint = false;
    void releaseGridPaint();
    void settleLayouts();

    QSlider* m_pan = nullptr;
    QDoubleSpinBox* m_panValue = nullptr;
    QLabel* m_panLabel = nullptr;
    QSlider* m_pitch = nullptr;
    QDoubleSpinBox* m_pitchValue = nullptr;
    QSlider* m_gain = nullptr;
    QDoubleSpinBox* m_gainValue = nullptr;
    QCheckBox* m_randomPan = nullptr;
    QSlider* m_width = nullptr;
    QDoubleSpinBox* m_widthValue = nullptr;
    QLabel* m_widthLabel = nullptr;
    QCheckBox* m_sampleLoop = nullptr;
    QPushButton* m_addSample = nullptr;
    QLabel* m_editTitle = nullptr;

    QCheckBox* m_loadLastSettings = nullptr;
    QCheckBox* m_autosave = nullptr;
    int m_loopDragSampleId = -1; // sample whose loop handles are being dragged on the Waveform tab
    QSpinBox* m_sceneLimit = nullptr;
    QSpinBox* m_gridColumns = nullptr;
    QSpinBox* m_gridRows = nullptr;
    QLineEdit* m_libraryPath = nullptr;
    QComboBox* m_reverbPreset[2] = {};
    QSlider* m_convolutionGain[2] = {};
    QLabel* m_convolutionGainValue[2] = {};
    QLineEdit* m_impulsePath[2] = {};
    QPushButton* m_impulseBrowse[2] = {};
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

    QString m_curDevice;
    bool m_updatingControls = false;
    bool m_savedOnExit = false;

    SampleLoadQueue* m_loads = nullptr;
    QProgressBar* m_loadBar = nullptr;
    QLabel* m_loadLabel = nullptr;
    QLabel* m_settingsPathLabel = nullptr;
    QLabel* m_waveformCacheInfo = nullptr;
    QAction* m_saveAction = nullptr;
    QAction* m_exportAction = nullptr;
    std::shared_ptr<ExportState> m_export;
    std::thread m_exportThread;
};
