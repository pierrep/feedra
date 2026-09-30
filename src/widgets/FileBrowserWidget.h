#pragma once

#include <QAbstractButton>
#include <QTreeView>
#include <QWidget>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

class AppConfig;
class OpenALSoundPlayer;
class QCheckBox;
class QFileSystemModel;
class QJsonObject;
class QLabel;
class QLineEdit;
class QSlider;
class QSortFilterProxyModel;
class QStyledItemDelegate;
class QStandardItemModel;
class QTimer;
class FileBrowserWidget;
struct DecodedAudio;

// Small flat icon button for the Files tab's bar and preview strip.
class FileIconButton : public QAbstractButton
{
    Q_OBJECT
public:
    enum class Kind { Up, GoTo, Play, Stop, Volume };
    explicit FileIconButton(Kind kind, QWidget* parent = nullptr);
    void setKind(Kind kind);
    Kind kind() const { return m_kind; }

protected:
    void paintEvent(QPaintEvent* event) override;
    QSize sizeHint() const override { return QSize(26, 26); }

private:
    Kind m_kind;
};

// The file list. Paints each row itself (see FileRowDelegate), shows a play button on the
// row under the mouse, and drags the selected files out as a list of file URLs.
class FileTreeView : public QTreeView
{
    Q_OBJECT
public:
    explicit FileTreeView(FileBrowserWidget* browser, QWidget* parent = nullptr);

    // Where a row's play button sits (viewport coordinates).
    QRect playButtonRect(const QModelIndex& index) const;

signals:
    void playClicked(const QModelIndex& index);
    void activated2(const QModelIndex& index); // double-click or Enter
    void upRequested();
    void spacePressed();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void startDrag(Qt::DropActions supportedActions) override;

private:
    FileBrowserWidget* m_browser;
};

// The waveform of the file being previewed, with the playhead; click or drag to seek.
class PreviewWave : public QWidget
{
    Q_OBJECT
public:
    explicit PreviewWave(QWidget* parent = nullptr);
    void setPath(const QString& path);
    void setPosition(float pct); // 0..1, or < 0 for none

signals:
    void seekRequested(float pct);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    QString m_path;
    float m_position = -1.0f;
};

// The Files tab: a folder tree that only lists audio Feedra can play, with a preview
// player at the bottom. Files are dragged from here onto pads or the Editor's sample list.
class FileBrowserWidget : public QWidget
{
    Q_OBJECT
public:
    explicit FileBrowserWidget(AppConfig* config, QWidget* parent = nullptr);
    ~FileBrowserWidget() override;

    // --- Browsing ---
    QString currentDir() const { return m_dir; }
    void setCurrentDir(const QString& dir);
    void goUp();
    // Folders the loaded scenes' samples live in, for the "Go to" menu. Supplied by the window.
    void setProjectFoldersProvider(std::function<QStringList()> provider) { m_projectFolders = std::move(provider); }
    QString filePath(const QModelIndex& viewIndex) const;
    bool isDir(const QModelIndex& viewIndex) const;
    QStringList selectedPaths() const;
    FileTreeView* view() const { return m_view; }
    void setSearchText(const QString& text);
    // True while the search box has text: the list shows matching audio files from the
    // current folder and every folder below it, best match first.
    bool isSearching() const { return m_searching; }

    // --- Preview ---
    void previewFile(const QString& path);
    void togglePreview(const QString& path);
    void stopPreview();
    // True from the moment a preview is asked for until it ends or is stopped.
    bool isPreviewing() const { return !m_previewPath.isEmpty() && !m_previewFailed; }
    QString previewPath() const { return m_previewPath; }
    bool previewFailed() const { return m_previewFailed; }
    bool autoPreview() const;
    void setAutoPreview(bool on);

    // Called by the window's timer: playhead, time, volume, end of file.
    void tick();

    void saveSettings(QJsonObject& global) const;
    void applySettings(const QJsonObject& global);

signals:
    // "Use this folder as the sample library" in the Go to menu.
    void libraryFolderChosen(const QString& dir);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildGoToMenu();
    void refreshPathLabel();
    void refreshPreviewUi();
    void startDecoded(std::shared_ptr<DecodedAudio> decoded);
    void applyPreviewVolume();
    void onCurrentChanged(const QModelIndex& current);
    void openIndex(const QModelIndex& index);

    // --- Recursive fuzzy search ---
    struct SearchEntry {
        QString path;   // full path
        QString rel;    // relative to the searched folder, '/' separated
        QString lower;  // rel, lower case
        int nameStart = 0; // where the file name starts in rel
    };
    void setSearching(bool on);
    void startSearchIndex();
    void addSearchBatch(std::vector<SearchEntry> batch, bool done);
    void refreshSearchResults();
    void connectSelection();

    AppConfig* m_config;
    std::function<QStringList()> m_projectFolders;
    QString m_dir;

    QFileSystemModel* m_model = nullptr;
    QSortFilterProxyModel* m_filter = nullptr;
    FileTreeView* m_view = nullptr;
    QLabel* m_pathLabel = nullptr;
    FileIconButton* m_upButton = nullptr;
    FileIconButton* m_goToButton = nullptr;
    QLineEdit* m_search = nullptr;
    QLabel* m_searchInfo = nullptr;
    QStandardItemModel* m_results = nullptr;
    QTimer* m_searchTimer = nullptr;
    bool m_searching = false;
    bool m_indexing = false;
    QString m_indexRoot;
    std::vector<SearchEntry> m_index;
    std::shared_ptr<std::atomic<bool>> m_scanCancel;

    QWidget* m_previewBox = nullptr;
    FileIconButton* m_previewButton = nullptr;
    QLabel* m_previewName = nullptr;
    QLabel* m_previewTime = nullptr;
    PreviewWave* m_previewWave = nullptr;
    QCheckBox* m_autoPreview = nullptr;
    QSlider* m_previewVolume = nullptr;

    std::unique_ptr<OpenALSoundPlayer> m_player;
    QString m_previewPath;
    bool m_previewFailed = false;
    bool m_sawPlaying = false;
    int m_previewGeneration = 0;
    qint64 m_previewStartedMs = 0;
};
