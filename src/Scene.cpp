#include "Scene.h"
#include "AppConfig.h"

#include <algorithm>
#include <QScrollArea>
#include <QSizePolicy>
#include <QVector>
#include <QWidget>
#include <cmath>

namespace {
constexpr int kGridMargin = 8;
constexpr int kGridHSpace = 16;
constexpr int kGridVSpace = 16;

class PadGridWidget : public QWidget
{
public:
    PadGridWidget(int cols, int rows, QWidget* parent = nullptr)
        : QWidget(parent)
        , m_cols(cols)
        , m_rows(rows)
    {
        setObjectName(QStringLiteral("PadGrid"));
        setAttribute(Qt::WA_StyledBackground, true);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void addPad(QWidget* pad)
    {
        pad->setParent(this);
        m_pads.push_back(pad);
    }

    QSize designSize() const
    {
        return QSize(
            2 * kGridMargin + m_cols * SoundPadWidget::kDesignWidth + (m_cols - 1) * kGridHSpace,
            2 * kGridMargin + m_rows * SoundPadWidget::kDesignHeight + (m_rows - 1) * kGridVSpace);
    }

    QSize sizeHint() const override { return designSize(); }
    QSize minimumSizeHint() const override { return designSize(); }
    void relayout() { layoutPads(); }

protected:
    void resizeEvent(QResizeEvent*) override { layoutPads(); }

private:
    void layoutPads()
    {
        const QSize design = designSize();
        const qreal s = std::max(1.0, std::min(
            width() / qreal(design.width()),
            height() / qreal(design.height())));
        const int padW = qRound(SoundPadWidget::kDesignWidth * s);
        const int padH = qRound(SoundPadWidget::kDesignHeight * s);
        const int hSpace = qRound(kGridHSpace * s);
        const int vSpace = qRound(kGridVSpace * s);
        const int margin = qRound(kGridMargin * s);
        const int gridW = 2 * margin + m_cols * padW + (m_cols - 1) * hSpace;
        const int gridH = 2 * margin + m_rows * padH + (m_rows - 1) * vSpace;
        const int ox = (width() - gridW) / 2;
        const int oy = 0;
        for (int i = 0; i < m_pads.size(); ++i) {
            const int col = i % m_cols;
            const int row = i / m_cols;
            m_pads[i]->setGeometry(
                ox + margin + col * (padW + hSpace),
                oy + margin + row * (padH + vSpace),
                padW,
                padH);
        }
    }

    int m_cols = 0;
    int m_rows = 0;
    QVector<QWidget*> m_pads;
};
}

Scene::Scene(AppConfig* config, int id, const QString& name, QWidget* gridParent, QWidget* listParent, QObject* parent)
    : QObject(parent)
    , id(id)
    , name(name)
    , m_config(config)
{
    if (this->name.isEmpty()) {
        this->name = QString("Scene %1").arg(id + 1);
    }

    auto* host = new PadGridWidget(config->gridWidth, config->gridHeight);
    const int count = config->gridWidth * config->gridHeight;
    pads.reserve(count);
    for (int i = 0; i < count; ++i) {
        auto* pad = new SoundPadWidget(config, id, i, host);
        pads.push_back(pad);
        host->addPad(pad);
        connect(pad, &SoundPadWidget::padClicked, this, [this](int padId) {
            activeSoundIdx = padId;
            emit padSelected(this->id, padId);
        });
    }

    auto* scroll = new QScrollArea(gridParent);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setAutoFillBackground(true);
    scroll->setWidget(host);
    m_grid = scroll;

    m_row = new SceneRowWidget(id, this->name, listParent);
    connect(m_row, &SceneRowWidget::selected, this, [this]() {
        selectRequested = true;
    });
    connect(m_row, &SceneRowWidget::nameChanged, this, [this](int, const QString& newName) {
        this->name = newName;
    });
    connect(m_row, &SceneRowWidget::playPauseRequested, this, [this]() {
        if (isPlaying) {
            isPlaying = false;
            pause();
            m_row->setPlaying(false);
        } else {
            isPlaying = true;
            play();
            m_row->setPlaying(true);
        }
    });
    connect(m_row, &SceneRowWidget::stopRequested, this, [this]() {
        isPlaying = false;
        stop();
        m_row->setPlaying(false);
    });
}

Scene::~Scene()
{
    delete m_grid;
    m_grid = nullptr;
    delete m_row;
    m_row = nullptr;
}

SoundPadWidget* Scene::padAt(int idx) const
{
    if (idx < 0 || idx >= pads.size()) {
        return nullptr;
    }
    return pads[idx];
}

void Scene::play()
{
    // Pads carrying on from a pause fade in, so they don't jump back in mid-sound. Pads
    // starting from the top start at full level: a fade would swallow their opening.
    // Pressing play during a pause's fade-out carries on from the level reached.
    const bool wasFadingOut = m_fading && m_fadeDirection == 1;
    m_fadeCallback = {};
    m_resuming.clear();
    bool anyScenePads = false;
    for (SoundPadWidget* pad : pads) {
        if (pad->isLoading()) {
            continue;
        }
        // Paused part-way (or still fading out from a pause): carries on, scene pad or not.
        // A pad already playing by itself is left as it is.
        const SoundPlayer& player = pad->soundPlayer();
        if (player.isResuming() && (wasFadingOut || !player.isPlaying())) {
            m_resuming.insert(pad);
        }
        anyScenePads = anyScenePads || (pad->isScenePad() && pad->isLoaded());
    }
    m_fading = !m_resuming.isEmpty();
    m_fadeDirection = 0;
    m_fadeFrom = m_fading ? (wasFadingOut ? m_fadeVolume : 0.0f) : 1.0f;
    m_fadeVolume = m_fadeFrom;
    m_fadeTimer.restart();
    for (SoundPadWidget* pad : pads) {
        if (pad->isLoading()) {
            continue;
        }
        const bool resume = m_resuming.contains(pad);
        // Starting fresh: the scene pads if there are any, otherwise every pad with sounds.
        const bool start = resume || (anyScenePads ? pad->isScenePad() : true);
        if (!start) {
            continue;
        }
        pad->setFadeVolume(resume ? m_fadeVolume : 1.0f);
        pad->soundPlayer().setPaused(false);
    }
}

void Scene::pause()
{
    // Carry on from the level reached if already fading out (e.g. stop during a pause).
    m_fadeFrom = (m_fading && m_fadeDirection == 1) ? m_fadeVolume : 1.0f;
    m_fading = true;
    m_fadeDirection = 1;
    m_fadeTimer.restart();
    m_fadeCallback = [this]() {
        for (SoundPadWidget* pad : pads) {
            pad->soundPlayer().setPaused(true);
        }
    };
}

void Scene::stop()
{
    // Carry on from the level reached if already fading out (e.g. stop during a pause).
    m_fadeFrom = (m_fading && m_fadeDirection == 1) ? m_fadeVolume : 1.0f;
    m_fading = true;
    m_fadeDirection = 1;
    m_fadeTimer.restart();
    m_fadeCallback = [this]() {
        for (SoundPadWidget* pad : pads) {
            pad->soundPlayer().stop();
        }
    };
}

void Scene::stopImmediate()
{
    endFade();
    isPlaying = false;
    for (SoundPadWidget* pad : pads) {
        pad->soundPlayer().stop();
    }
    m_row->setPlaying(false);
}

void Scene::update()
{
    if (m_fading) {
        constexpr float fadeDuration = 500.0f;
        const float elapsed = static_cast<float>(m_fadeTimer.elapsed());
        const float t = std::min(1.0f, elapsed / fadeDuration);
        const float target = m_fadeDirection == 0 ? 1.0f : 0.0f;
        m_fadeVolume = m_fadeFrom + (target - m_fadeFrom) * t;
        if (elapsed > fadeDuration) {
            endFade();
        }
    }
    bool audible = false;
    // A fade-in applies only to the pads resuming from a pause; a fade-out applies to all.
    const bool fadingIn = m_fading && m_fadeDirection == 0;
    for (SoundPadWidget* pad : pads) {
        pad->setFadeVolume(fadingIn && !m_resuming.contains(pad) ? 1.0f : m_fadeVolume);
        pad->updateAudio();
        audible = audible || pad->isPlaying();
    }
    m_row->setAudible(audible);

    int scenePads = 0;
    for (SoundPadWidget* pad : pads) {
        scenePads += pad->isScenePad() ? 1 : 0;
    }
    if (scenePads != m_scenePadCount) {
        m_scenePadCount = scenePads;
        m_row->setPlayToolTip(scenePads > 0
            ? tr("Play / pause scene: starts its %n scene pad(s)", nullptr, scenePads)
            : tr("Play / pause scene: starts every pad"));
    }
}

void Scene::endFade()
{
    if (!m_fading) {
        return;
    }
    m_fading = false;
    if (m_fadeCallback) {
        m_fadeCallback();
    }
    m_fadeVolume = 1.0f;
    m_resuming.clear();
}

void Scene::setActive(bool active)
{
    m_row->setActive(active);
    m_row->setInteractive(true);
    for (SoundPadWidget* pad : pads) {
        pad->setInteractive(active);
        pad->setSelected(active && pad->padId() == activeSoundIdx);
    }
}

void Scene::layoutGrid()
{
    auto* scroll = qobject_cast<QScrollArea*>(m_grid);
    if (!scroll || !scroll->widget() || scroll->viewport()->size().isEmpty()) {
        return;
    }
    auto* host = static_cast<PadGridWidget*>(scroll->widget());
    const QSize fitted = scroll->viewport()->size().expandedTo(host->minimumSizeHint());
    if (host->size() != fitted) {
        host->resize(fitted);
    }
    host->relayout();
}
