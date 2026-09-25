#pragma once

#include "AppConfig.h"

#include <QMainWindow>
#include <QVector>

class AudioSample;
class Scene;
class SoundPadWidget;
class SampleRowWidget;
class SampleLoadQueue;
class QAction;
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

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum class Page { Main, Settings, Theme };
    enum class SidebarView { Scenes, Editor };

    void buildUi();
    void buildMenus();
    void buildSettingsPage();
    void buildThemePage();
    void syncSettingsPage();
    void applyImpulsePath(const QString& path);
    void refreshThemeSwatches();
    void applyAppSettings(const QJsonObject& global);
    void connectScene(Scene* scene);
    void drainLoads(int budgetMs);
    void refreshLoadUi();
    void waitForLoads();
    SoundPadWidget* findPad(int sceneId, int padId) const;
    void createDefaultScenes();
    void addNewScene();
    void deleteScene(int sceneId);
    void enableScene(int idx);
    void moveScene(int fromIndex, int insertIndex);
    void updateSceneListLayout();
    bool handleReorderKey(QKeyEvent* event);
    void updateMainControls();
    void refreshSampleInfo();
    void updateEditControls();
    void updatePanControl(AudioSample* sample);
    void rebuildEditSamples();
    void refreshEditorPage();
    void moveEditorSample(int fromIndex, int insertIndex);
    void setPage(Page page);
    void setSidebarView(SidebarView view);
    void saveConfig();
    void saveConfigAs();
    bool saveConfigTo(const QString& path, bool copyFiles);
    void loadConfig();
    void loadConfigFrom(const QString& path);
    void saveWindowLayout(QJsonObject& global) const;
    void restoreWindowLayout(const QJsonObject& global);
    void clearActivePad();
    void copyPad(int fromIdx, int toIdx);
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
    Page m_page = Page::Main;
    SidebarView m_sidebar = SidebarView::Scenes;

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
    QVBoxLayout* m_sampleListLayout = nullptr;
    QVector<SampleRowWidget*> m_sampleRows;
    SoundPadWidget* m_followedPad = nullptr;
    int m_followedSound = -1;

    QSlider* m_mainVolume = nullptr;
    QWidget* m_bottomPanel = nullptr;
    QStackedWidget* m_bottomStack = nullptr;
    QPushButton* m_sampleTab = nullptr;
    QPushButton* m_padTab = nullptr;
    QPushButton* m_collapseBottom = nullptr;
    int m_bottomPageHeight = 0;
    QSpinBox* m_minDelay = nullptr;
    QSpinBox* m_maxDelay = nullptr;
    QSlider* m_reverbSend = nullptr;
    QSlider* m_reverbSend2 = nullptr;
    QCheckBox* m_randomPlayback = nullptr;
    QLabel* m_infoLabel = nullptr;
    SoundPadWidget* m_infoPad = nullptr;
    int m_infoSound = -1;
    bool m_bottomCollapsed = false;

    QSlider* m_pan = nullptr;
    QDoubleSpinBox* m_panValue = nullptr;
    QLabel* m_panLabel = nullptr;
    QSlider* m_pitch = nullptr;
    QDoubleSpinBox* m_pitchValue = nullptr;
    QSlider* m_gain = nullptr;
    QDoubleSpinBox* m_gainValue = nullptr;
    QCheckBox* m_randomPan = nullptr;
    QCheckBox* m_spatialise = nullptr;
    QPushButton* m_addSample = nullptr;
    QLabel* m_editTitle = nullptr;

    QCheckBox* m_loopByDefault = nullptr;
    QSpinBox* m_sceneLimit = nullptr;
    QSpinBox* m_gridColumns = nullptr;
    QSpinBox* m_gridRows = nullptr;
    QLineEdit* m_libraryPath = nullptr;
    QComboBox* m_reverbPreset = nullptr;
    QSlider* m_convolutionGain = nullptr;
    QLineEdit* m_impulsePath = nullptr;
    QPushButton* m_impulseBrowse = nullptr;
    QComboBox* m_themePreset = nullptr;
    struct ThemeSwatch {
        QPushButton* button = nullptr;
        QLabel* hex = nullptr;
    };
    QVector<ThemeSwatch> m_themeSwatches;

    QString m_curDevice;
    bool m_updatingControls = false;

    SampleLoadQueue* m_loads = nullptr;
    QProgressBar* m_loadBar = nullptr;
    QLabel* m_loadLabel = nullptr;
    QAction* m_saveAction = nullptr;
    QAction* m_saveAsAction = nullptr;
};
