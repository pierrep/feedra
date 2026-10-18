#pragma once

#include "AppConfig.h"
#include "ControlMap.h"
#include "Duck.h"
#include "widgets/SoundPadWidget.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QRect>
#include <QMainWindow>
#include <QPointer>
#include <QVector>

#include <memory>
#include <thread>

class AudioSample;
class Scene;
class SampleLoadQueue;
class LogWindow;
class MidiInput;
class FileBrowserWidget;
class SettingsPage;
class EditorPanel;
class BottomPanel;
class ThemePage;
class QAbstractButton;
class QAction;
class QFrame;
class QHBoxLayout;
class QProgressBar;
class QScrollArea;
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
    void syncSettingsPage();
    void applyAppSettings(const QJsonObject& global);
    void connectScene(Scene* scene);
    void drainLoads(int budgetMs);
    void refreshLoadUi();
    void waitForLoads();
    Pad* findPad(int sceneId, int padId) const;
    void createDefaultScenes();
    void addNewScene();
    int nextSceneId() const;
    void importScenes();
    void importScenesFrom(const QString& path);
    void deleteScene(int sceneId);
    void enableScene(int idx);
    // Selects the scene and crossfades into it: every other scene that's playing (or has pads
    // playing) fades out and stops while this one fades in, over AppConfig::crossfadeSeconds.
    void goToScene(int idx);
    void moveScene(int fromIndex, int insertIndex);
    void updateSceneListLayout();
    bool handleReorderKey(QKeyEvent* event);
    bool handlePadClipKey(QKeyEvent* event);
    void copyActivePad();
    void cutActivePad();
    void pasteActivePad();
    void updateMainControls();
    void refreshEditorPage();
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
    void reportMissingSamples();
    void restoreBackup();
    // `settingsOnly` rewrites just the app settings in the file and leaves its scenes as they are.
    // With `exportTo`, samples are pointed at a "files" folder beside `path`, the copies they
    // need are added to it, and the file itself is not written (it's left in `exportTo->json`).
    bool saveConfigTo(const QString& path, bool copyFiles, bool settingsOnly = false, ExportState* exportTo = nullptr);
    void loadConfig();
    void loadConfigFrom(const QString& path);
    // This computer's settings (AppConfig::localSettingsPath): devices, window, MIDI.
    void saveLocalSettings();
    void saveWindowLayout(QJsonObject& global) const;
    void restoreWindowLayout(const QJsonObject& global);
    void clearActivePad();
    void copyPad(int fromIdx, int toIdx);
    void movePad(int fromIdx, int toIdx);
    // Adds the samples of one pad after those of another that already has some.
    void mergePad(int fromIdx, int toIdx, bool keepSource);
    void scrollToSceneRow(QWidget* row);
    void scrollToActiveScene();
    void clearActiveSample();
    void tick();
    void checkAudioDevice();
    // Fades every scene out over AppConfig::panicSeconds and stops the preview. Pressed again
    // while that fade runs, stops everything at once.
    void panic();
    // Space (talk-over, held), Esc (panic). True if the key was taken.
    bool handleLiveKey(QKeyEvent* event);
    // A text field, spin box, list or menu has the keyboard, so live keys leave it alone.
    bool keyboardIsBusy() const;
    void refreshTalkButton();
    // The key caps on the pads, for the keyboard setting.
    void refreshKeyHints();
    // MIDI: carries out what a message asks for (ControlMap), or finishes learning.
    void handleMidi(const MidiMessage& message);
    void startMidiLearn(ControlAction action, int pad = -1);
    void cancelMidiLearn();
    // A line in the header: what MIDI learn is waiting for, or what it learned.
    void showMidiStatus(const QString& text, int hideAfterMs = 0);
    // Opens the chosen MIDI input if it isn't open (it may have been plugged in since).
    void openMidiInput();
    // The output device and the preview (cue) device, from AppConfig.
    void applyOutputDevice();
    void applyPreviewDevice();
    bool moveOutputTo(const QString& device);
    void onPadClicked(int sceneId, int padId);
    void setBottomTab(int index);
    void setBottomCollapsed(bool collapsed, bool resizeWindow = true);
    Pad* activePad() const;
    Pad* syncPanelPad();
    Scene* activeScene() const;

    AppConfig m_config;
    QJsonObject m_local; // local.json as last read or written
    QVector<Scene*> m_scenes;
    Pad::PadClip m_padClip;
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
    QScrollArea* m_sceneScroll = nullptr;
    QPointer<QWidget> m_scrollToSceneRow; // a scene row to bring into view
    int m_scrollToSceneSerial = 0;
    QVBoxLayout* m_sceneListLayout = nullptr;
    QPushButton* m_addScene = nullptr;
    QStackedWidget* m_pages = nullptr;
    QStackedWidget* m_sidebarStack = nullptr;
    QWidget* m_mainPage = nullptr;
    QWidget* m_scenesPage = nullptr;
    SettingsPage* m_settingsPage = nullptr;
    ThemePage* m_themePage = nullptr;
    QPushButton* m_scenesTab = nullptr;
    QPushButton* m_editorTab = nullptr;
    QPushButton* m_filesTab = nullptr;
    FileBrowserWidget* m_fileBrowser = nullptr;
    Pad* m_followedPad = nullptr;
    int m_followedSound = -1;

    EditorPanel* m_editor = nullptr;
    BottomPanel* m_bottom = nullptr;
    Duck m_duck;
    MidiInput* m_midi = nullptr;
    ControlMap m_controls;
    QString m_midiPort; // the chosen input, open or not
    int m_midiRetry = 0;
    QLabel* m_midiStatus = nullptr;
    int m_midiStatusSerial = 0;
    QElapsedTimer m_clock;
    qint64 m_panicUntilMs = 0; // a second Panic before this stops everything at once
    QPushButton* m_talkButton = nullptr;
    QPushButton* m_panicButton = nullptr;
    QSlider* m_mainVolume = nullptr;
    QLabel* m_mainVolumeValue = nullptr;
    // Set while the panel toggles and the window resizes to match, so the pad grid
    // isn't painted at the in-between size.
    bool m_holdGridPaint = false;
    void releaseGridPaint();
    void settleLayouts();

    QString m_curDevice;
    QString m_failedDevice; // the last device a move to failed, so it's logged once
    QString m_lastDefaultDevice; // the system default when a chosen device was last tried
    bool m_savedOnExit = false;
    bool m_reportMissingAfterLoad = false;
    LogWindow* m_logWindow = nullptr;
    QAction* m_restoreAction = nullptr;
    bool m_saveDeclined = false; // the last save was cancelled at the "far fewer pads" question

    SampleLoadQueue* m_loads = nullptr;
    QProgressBar* m_loadBar = nullptr;
    QLabel* m_loadLabel = nullptr;
    QLabel* m_settingsPathLabel = nullptr;
    QAction* m_saveAction = nullptr;
    QAction* m_exportAction = nullptr;
    std::shared_ptr<ExportState> m_export;
    std::thread m_exportThread;
};
