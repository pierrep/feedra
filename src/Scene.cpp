#include "Scene.h"
#include "AppConfig.h"
#include "Fade.h"

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

    m_columns = std::max(1, config->gridWidth);
    m_rows = std::max(1, config->gridHeight);
    auto* host = new PadGridWidget(m_columns, m_rows);
    const int count = config->gridWidth * config->gridHeight;
    pads.reserve(count);
    for (int i = 0; i < count; ++i) {
        auto* pad = new Pad(config, id, i, this);
        auto* widget = new SoundPadWidget(config, pad, host);
        pads.push_back(pad);
        padWidgets.push_back(widget);
        host->addPad(widget);
        connect(widget, &SoundPadWidget::padClicked, this, [this](int padId) {
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
    connect(m_row, &SceneRowWidget::goRequested, this, [this]() {
        emit goRequested(this->id);
    });
    connect(m_row, &SceneRowWidget::stopRequested, this, [this]() {
        isPlaying = false;
        stop();
        m_row->setPlaying(false);
    });
}

Scene::~Scene()
{
    // The widgets first: they refer to the pads.
    delete m_grid;
    m_grid = nullptr;
    delete m_row;
    m_row = nullptr;
}

Pad* Scene::padAt(int idx) const
{
    if (idx < 0 || idx >= pads.size()) {
        return nullptr;
    }
    return pads[idx];
}

SoundPadWidget* Scene::padWidgetAt(int idx) const
{
    if (idx < 0 || idx >= padWidgets.size()) {
        return nullptr;
    }
    return padWidgets[idx];
}

float Scene::fadeGain(float phase)
{
    return Fade::equalPower(phase);
}

void Scene::play(int fadeMs, bool fadeInStarts)
{
    // Pads carrying on from a pause fade in, so they don't jump back in mid-sound. Pads
    // starting from the top start at full level, unless this is a crossfade.
    // Pressing play during a pause's fade-out carries on from the level reached.
    const bool wasFadingOut = m_fading && m_fadeDirection == 1;
    m_fadeCallback = {};
    m_resuming.clear();
    bool anyScenePads = false;
    for (Pad* pad : pads) {
        if (!pad->isLoading()) {
            anyScenePads = anyScenePads || (pad->isScenePad() && pad->isLoaded());
        }
    }
    QVector<Pad*> starting;
    for (Pad* pad : pads) {
        if (pad->isLoading()) {
            continue;
        }
        // Paused part-way (or still fading out from a pause): carries on, scene pad or not.
        // A pad already playing by itself is left as it is.
        const SoundPlayer& player = pad->soundPlayer();
        const bool resume = player.isResuming() && (wasFadingOut || !player.isPlaying());
        // Starting fresh: the scene pads if there are any, otherwise every pad with sounds.
        if (!resume && !(anyScenePads ? pad->isScenePad() : true)) {
            continue;
        }
        if (resume || (fadeInStarts && pad->hasSounds() && (!player.isPlaying() || wasFadingOut))) {
            m_resuming.insert(pad);
        }
        starting.push_back(pad);
    }
    m_fading = !m_resuming.isEmpty() && fadeMs > 0;
    m_crossfade = m_fading && fadeInStarts;
    m_fadeDirection = 0;
    m_fadeMs = std::max(1, fadeMs);
    m_phaseFrom = m_fading ? (wasFadingOut ? m_fadePhase : 0.0f) : 1.0f;
    m_fadePhase = m_phaseFrom;
    m_fadeVolume = fadeGain(m_fadePhase);
    m_fadeTimer.restart();
    for (Pad* pad : starting) {
        pad->setFadeVolume(m_resuming.contains(pad) ? m_fadeVolume : 1.0f);
        pad->play();
    }
    if (!m_fading) {
        m_resuming.clear();
    }
}

void Scene::pause(int fadeMs)
{
    // Carry on from the level reached if already fading out (e.g. pause during a stop), or
    // fading everything in (a crossfade reversed).
    m_phaseFrom = (m_fading && (m_fadeDirection == 1 || m_crossfade)) ? m_fadePhase : 1.0f;
    m_fading = true;
    m_crossfade = false;
    m_fadeDirection = 1;
    m_fadeMs = std::max(1, fadeMs);
    m_fadeTimer.restart();
    m_fadeCallback = [this]() {
        for (Pad* pad : pads) {
            pad->pauseNow();
        }
    };
}

void Scene::stop(int fadeMs, bool crossfade)
{
    // Carry on from the level reached if already fading out (e.g. stop during a pause), or
    // fading everything in (a crossfade reversed).
    m_phaseFrom = (m_fading && (m_fadeDirection == 1 || m_crossfade)) ? m_fadePhase : 1.0f;
    m_fading = true;
    m_crossfade = crossfade;
    m_fadeDirection = 1;
    m_fadeMs = std::max(1, fadeMs);
    m_fadeTimer.restart();
    m_fadeCallback = [this]() {
        for (Pad* pad : pads) {
            pad->stopNow();
        }
    };
}

void Scene::stopImmediate()
{
    endFade();
    isPlaying = false;
    for (Pad* pad : pads) {
        pad->stopNow();
    }
    m_row->setPlaying(false);
}

void Scene::update()
{
    if (m_fading) {
        const float duration = static_cast<float>(m_fadeMs);
        const float elapsed = static_cast<float>(m_fadeTimer.elapsed());
        const float t = std::min(1.0f, elapsed / duration);
        const float target = m_fadeDirection == 0 ? 1.0f : 0.0f;
        m_fadePhase = m_phaseFrom + (target - m_phaseFrom) * t;
        m_fadeVolume = fadeGain(m_fadePhase);
        if (elapsed >= duration) {
            endFade();
        }
    }
    bool audible = false;
    // A fade-in applies only to the pads resuming from a pause; a fade-out applies to all.
    const bool fadingIn = m_fading && m_fadeDirection == 0;
    for (Pad* pad : pads) {
        pad->setFadeVolume(fadingIn && !m_resuming.contains(pad) ? 1.0f : m_fadeVolume);
        pad->updateAudio();
        audible = audible || pad->isPlaying();
    }
    for (SoundPadWidget* widget : padWidgets) {
        widget->updateView();
    }
    m_audible = audible;
    m_row->setAudible(audible);

    int scenePads = 0;
    for (Pad* pad : pads) {
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
    m_crossfade = false;
    if (m_fadeCallback) {
        m_fadeCallback();
    }
    m_fadePhase = 1.0f;
    m_fadeVolume = 1.0f;
    m_resuming.clear();
}

void Scene::setActive(bool active)
{
    m_row->setActive(active);
    m_row->setInteractive(true);
    for (SoundPadWidget* widget : padWidgets) {
        widget->setInteractive(active);
        widget->setSelected(active && widget->padId() == activeSoundIdx);
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
