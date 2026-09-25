#include "SoundPadWidget.h"
#include "AppConfig.h"
#include "AudioSample.h"
#include "OpenALSoundPlayer.h"
#include "SampleLoadQueue.h"
#include "Theme.h"

#include <QApplication>
#include <QColor>
#include <QDebug>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QMouseEvent>
#include <QAbstractButton>
#include <QPainter>
#include <QPaintEvent>
#include <QPixmap>
#include <QPolygonF>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QSlider>
#include <QUrl>
#include <QWidget>
#include <algorithm>
#include <filesystem>
#include <utility>

namespace {
QStringList audioNameFilters()
{
    return {
        QStringLiteral("Audio files (*.wav *.flac *.ogg *.mp3 *.aiff *.aif *.wma)"),
        QStringLiteral("All files (*.*)")
    };
}

QPixmap loopIcon()
{
    static const QPixmap icon(QStringLiteral(":/images/loopicon.png"));
    return icon;
}

QPixmap tintedLoopIcon(const QColor& color)
{
    QPixmap src = loopIcon();
    if (src.isNull()) {
        return src;
    }
    QPixmap tinted(src.size());
    tinted.fill(Qt::transparent);
    QPainter p(&tinted);
    p.drawPixmap(0, 0, src);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(tinted.rect(), color);
    return tinted;
}
}

GlyphButton::GlyphButton(Kind kind, QWidget* parent)
    : QAbstractButton(parent)
    , m_kind(kind)
{
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
    if (kind == Kind::Loop) {
        setCheckable(true);
    }
    if (kind == Kind::Stop) {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }
}

void GlyphButton::setLoaded(bool loaded)
{
    if (m_loaded == loaded) {
        return;
    }
    m_loaded = loaded;
    update();
}

void GlyphButton::setPlaying(bool playing)
{
    if (m_playing == playing) {
        return;
    }
    m_playing = playing;
    update();
}

void GlyphButton::setArmed(bool armed)
{
    if (m_armed == armed) {
        return;
    }
    m_armed = armed;
    // Unarmed stop is invisible, so let clicks fall through and select the pad.
    if (m_kind == Kind::Stop) {
        setAttribute(Qt::WA_TransparentForMouseEvents, !armed);
    }
    update();
}

QSize GlyphButton::sizeHint() const
{
    return m_kind == Kind::Play ? QSize(50, 50) : QSize(16, 16);
}

void GlyphButton::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

    const Theme::Palette& theme = Theme::instance().palette();

    if (m_kind == Kind::Load) {
        p.setBrush(theme.loadButton);
        p.setPen(QPen(theme.loadButton, 1));
        p.drawRoundedRect(r, 6, 6);
        return;
    }

    if (m_kind == Kind::Stop) {
        if (m_armed) {
            p.setPen(Qt::NoPen);
            p.setBrush(theme.stopArmed);
            p.drawRect(r);
        }
        return;
    }

    if (m_kind == Kind::Loop) {
        const QColor color = isChecked() ? theme.loopOn : theme.loopOff;
        const QPixmap icon = tintedLoopIcon(color);
        if (!icon.isNull()) {
            p.drawPixmap(rect(), icon);
        } else {
            p.setPen(QPen(color, 2));
            p.drawEllipse(r.adjusted(2, 2, -2, -2));
        }
        return;
    }

    p.setBrush(m_loaded ? theme.playLoaded : theme.playEmpty);
    p.setPen(Qt::NoPen);
    if (m_playing) {
        const qreal barW = r.width() / 3.0;
        p.drawRect(QRectF(r.left(), r.top(), barW, r.height()));
        p.drawRect(QRectF(r.left() + r.width() * 2.0 / 3.0, r.top(), barW, r.height()));
    } else {
        QPolygonF tri;
        tri << r.topLeft() << r.bottomLeft() << QPointF(r.right(), r.center().y());
        p.drawPolygon(tri);
    }
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(theme.playOutline, 1));
    p.drawRect(r);
}

PadPlayhead::PadPlayhead(QWidget* parent)
    : QWidget(parent)
{
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
}

void PadPlayhead::setProgress(float pct)
{
    m_progress = std::clamp(pct, 0.0f, 1.0f);
    update();
}

void PadPlayhead::setDelayMode(bool delay)
{
    if (m_delay == delay) {
        return;
    }
    m_delay = delay;
    update();
}

void PadPlayhead::setTimeText(const QString& text)
{
    if (m_time == text) {
        return;
    }
    m_time = text;
    update();
}

void PadPlayhead::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const QRect r = rect().adjusted(0, 0, -1, -1);
    const Theme::Palette& theme = Theme::instance().palette();
    p.fillRect(QRect(r.x(), r.y(), static_cast<int>(r.width() * m_progress), r.height()),
        m_delay ? theme.playheadDelay : theme.playhead);
    p.setPen(QPen(theme.playheadBorder, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(r);
    if (!m_time.isEmpty()) {
        p.setPen(theme.playheadText);
        QFont f = font();
        f.setPixelSize(std::max(6, height() - 4));
        p.setFont(f);
        p.drawText(r, Qt::AlignCenter, m_time);
    }
}

void PadPlayhead::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && width() > 0 && m_onScrub) {
        m_onScrub(std::clamp(static_cast<float>(event->position().x() / width()), 0.0f, 1.0f));
    }
}

void PadPlayhead::mouseMoveEvent(QMouseEvent* event)
{
    if ((event->buttons() & Qt::LeftButton) && width() > 0 && m_onScrub) {
        m_onScrub(std::clamp(static_cast<float>(event->position().x() / width()), 0.0f, 1.0f));
    }
}

SoundPadWidget::SoundPadWidget(AppConfig* config, int sceneId, int padId, QWidget* parent)
    : QWidget(parent)
    , m_config(config)
    , m_player(this)
    , m_sceneId(sceneId)
    , m_padId(padId)
{
    setObjectName("SoundPad");
    setAcceptDrops(true);
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
    setMinimumSize(kDesignWidth, kDesignHeight);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_player.setup(config, padId);

    m_volume = new QSlider(Qt::Horizontal, this);
    m_volume->setRange(0, 1000);
    m_volume->setValue(700);
    m_volume->setObjectName("PadVolume");

    m_volumeValue = new QLabel(this);
    m_volumeValue->setObjectName("PadVolumeValue");
    m_volumeValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_volumeValue->setAttribute(Qt::WA_TransparentForMouseEvents);

    m_card = new QWidget(this);
    m_card->setObjectName("PadCard");
    m_card->setAttribute(Qt::WA_TranslucentBackground);
    m_card->setAutoFillBackground(false);

    m_load = new GlyphButton(GlyphButton::Kind::Load, m_card);
    m_loop = new GlyphButton(GlyphButton::Kind::Loop, m_card);
    m_stop = new GlyphButton(GlyphButton::Kind::Stop, m_card);
    m_play = new GlyphButton(GlyphButton::Kind::Play, m_card);

    m_playhead = new PadPlayhead(m_card);
    m_playhead->setVisible(false);
    m_playhead->m_onScrub = [this](float pct) {
        if (m_player.isLoaded()) {
            m_player.setPosition(pct);
        }
    };

    m_name = new QLineEdit(m_card);
    m_name->setObjectName("PadName");
    m_name->setMaxLength(17);
    m_name->setAlignment(Qt::AlignCenter);
    m_name->setPlaceholderText(QStringLiteral("name"));

    layoutContents();

    // The card forwards unhandled presses to mousePressEvent, so watching it too would select twice.
    const QList<QWidget*> controls{m_volume, m_load, m_loop, m_stop, m_play, m_playhead, m_name};
    for (QWidget* control : controls) {
        control->installEventFilter(this);
    }

    connect(m_load, &QAbstractButton::clicked, this, &SoundPadWidget::chooseFiles);
    connect(m_play, &QAbstractButton::clicked, this, [this]() {
        if (isLoading() || !m_player.isLoaded()) {
            return;
        }
        if (m_player.isPlaying()) {
            m_player.setPaused(true);
        } else {
            m_player.setPaused(false);
        }
    });
    connect(m_stop, &QAbstractButton::clicked, this, [this]() {
        m_player.stop();
    });
    connect(m_loop, &QAbstractButton::toggled, this, [this](bool on) {
        m_player.setLoop(on);
    });
    connect(m_volume, &QSlider::valueChanged, this, [this]() {
        applyVolume();
        updateVolumeLabel();
    });

    setInteractive(false);
    updateVolumeLabel();

    connect(&Theme::instance(), &Theme::changed, this, [this]() {
        layoutContents();
        update();
        const auto kids = findChildren<QWidget*>();
        for (QWidget* kid : kids) {
            kid->update();
        }
    });
}

SoundPadWidget::~SoundPadWidget()
{
    if (m_loadGeneration) {
        m_loadGeneration->fetch_add(1);
    }
}

QString SoundPadWidget::soundName() const
{
    return m_name->text();
}

void SoundPadWidget::setSoundName(const QString& name)
{
    m_name->setText(name);
}

float SoundPadWidget::padVolume() const
{
    return m_volume->value() / 1000.0f;
}

void SoundPadWidget::setPadVolume(float value)
{
    m_volume->setValue(static_cast<int>(std::clamp(value, 0.0f, 1.0f) * 1000.0f));
}

bool SoundPadWidget::isLooping() const
{
    return m_loop->isChecked();
}

void SoundPadWidget::setLooping(bool looping)
{
    m_loop->setChecked(looping);
    m_player.setLoop(looping);
}

bool SoundPadWidget::isLoaded() const
{
    return m_player.isLoaded();
}

bool SoundPadWidget::isPlaying() const
{
    return m_player.isPlaying();
}

void SoundPadWidget::updateAudio()
{
    m_player.update();
    applyVolume();

    const bool loaded = !isLoading() && m_player.isLoaded();
    const bool playing = m_player.isPlaying();
    // Play does nothing on an empty pad, so let clicks through to select the pad.
    const bool empty = !isLoading() && !m_player.isLoaded();
    if (m_play->testAttribute(Qt::WA_TransparentForMouseEvents) != empty) {
        m_play->setAttribute(Qt::WA_TransparentForMouseEvents, empty);
    }
    m_play->setLoaded(loaded);
    m_play->setPlaying(playing);
    m_stop->setArmed(playing);
    m_playhead->setProgress(loaded ? m_player.getPosition() : 0.0f);
    m_playhead->setDelayMode(m_player.isPlayingDelay());
    m_playhead->setTimeText(loaded ? remainingTimeText() : QString());
    m_playhead->setVisible(loaded);
}

void SoundPadWidget::setSelected(bool selected)
{
    if (m_selected == selected) {
        return;
    }
    m_selected = selected;
    update();
}

void SoundPadWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const qreal s = contentScale();
    const qreal inset = 1.5 * s;
    const QRectF card = QRectF(padCardRect()).adjusted(inset, inset, -inset, -inset);
    const Theme::Palette& theme = Theme::instance().palette();
    p.setBrush(theme.padFill);
    p.setPen(QPen(m_selected ? theme.padSelected : theme.padBorder, (m_selected ? 4 : 3) * s));
    p.drawRoundedRect(card, 5 * s, 5 * s);
    if (isLoading() && m_loadTotal > 0) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 80));
        p.drawRoundedRect(card, 5 * s, 5 * s);
        const qreal barH = std::max(3.0, 4.0 * s);
        const QRectF track(card.left() + 8 * s, card.bottom() - barH - 8 * s, card.width() - 16 * s, barH);
        p.setBrush(theme.progressTrack);
        p.drawRoundedRect(track, barH / 2, barH / 2);
        QRectF chunk = track;
        chunk.setWidth(track.width() * (static_cast<qreal>(m_nextCommit) / static_cast<qreal>(m_loadTotal)));
        p.setBrush(theme.progressChunk);
        p.drawRoundedRect(chunk, barH / 2, barH / 2);
    }
}

QSize SoundPadWidget::sizeHint() const
{
    return QSize(kDesignWidth, kDesignHeight);
}

QSize SoundPadWidget::minimumSizeHint() const
{
    return sizeHint();
}

qreal SoundPadWidget::contentScale() const
{
    if (width() <= 0 || height() <= 0) {
        return 1.0;
    }
    return std::min(width() / qreal(kDesignWidth), height() / qreal(kDesignHeight));
}

void SoundPadWidget::layoutContents()
{
    const qreal s = contentScale();
    auto scaled = [s](int x, int y, int w, int h) {
        return QRect(qRound(x * s), qRound(y * s), qRound(w * s), qRound(h * s));
    };

    m_volume->setGeometry(scaled(2, 2, 92, 15));
    m_volumeValue->setGeometry(scaled(96, 1, 26, 16));
    m_card->setGeometry(scaled(2, 20, 120, 120));
    m_load->setGeometry(scaled(10, 10, 15, 15));
    m_loop->setGeometry(scaled(52, 10, 15, 15));
    m_stop->setGeometry(scaled(95, 10, 15, 15));
    m_play->setGeometry(scaled(35, 35, 50, 50));
    m_playhead->setGeometry(scaled(35, 86, 50, 10));
    m_name->setGeometry(scaled(10, 100, 100, 16));

    QFont nameFont = m_name->font();
    nameFont.setPixelSize(std::max(1, qRound(11 * s)));
    m_name->setFont(nameFont);

    QFont volFont = m_volumeValue->font();
    volFont.setPixelSize(std::max(1, qRound(10 * s)));
    m_volumeValue->setFont(volFont);

    const int groove = std::max(2, qRound(6 * s));
    const int handle = std::max(8, qRound(12 * s));
    const int margin = std::max(2, qRound(4 * s));
    const Theme::Palette& theme = Theme::instance().palette();
    m_volume->setStyleSheet(QStringLiteral(
        "QSlider#PadVolume::groove:horizontal {"
        " height: %1px; background: %2; border-radius: %3px; }"
        "QSlider#PadVolume::handle:horizontal {"
        " background: %4; width: %5px; margin: -%6px 0; border-radius: %7px; }")
        .arg(groove)
        .arg(theme.sliderGroove.name(QColor::HexRgb))
        .arg(groove / 2)
        .arg(theme.sliderHandle.name(QColor::HexRgb))
        .arg(handle)
        .arg(margin)
        .arg(handle / 2));
}

void SoundPadWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    layoutContents();
}

QRect SoundPadWidget::padCardRect() const
{
    return m_card ? m_card->geometry() : rect().adjusted(2, 18, -2, -2);
}

void SoundPadWidget::setInteractive(bool enabled)
{
    setEnabled(enabled);
}

void SoundPadWidget::setFadeVolume(float fade)
{
    m_fadeVolume = fade;
}

void SoundPadWidget::setLoadQueue(SampleLoadQueue* queue)
{
    m_queue = queue;
}

bool SoundPadWidget::isLoading() const
{
    return m_loadTotal > 0 && m_nextCommit < m_loadTotal;
}

void SoundPadWidget::cancelLoading()
{
    m_loadGeneration->fetch_add(1);
    m_incoming.clear();
    m_reloads.clear();
    m_slots.clear();
    m_loadTotal = 0;
    m_nextCommit = 0;
    m_finishSent = false;
    m_notifyWhenDone = false;
    update();
}

void SoundPadWidget::enqueueSample(const QString& path, float pitch, float gain, float pan, bool panRandom, bool spatialise)
{
    if (path.isEmpty()) {
        return;
    }
    if (m_nextCommit >= m_loadTotal) {
        m_finishSent = false;
    }
    LoadSlot slot;
    slot.path = path;
    slot.pitch = pitch;
    slot.gain = gain;
    slot.pan = pan;
    slot.panRandom = panRandom;
    slot.spatialise = spatialise;
    m_slots.push_back(slot);
    const int index = m_loadTotal++;
    m_config->lastPath = QFileInfo(path).absolutePath();

    if (!m_queue) {
        DecodedAudio audio = OpenALSoundPlayer::decodeFile(std::filesystem::path(path.toStdString()), m_stream);
        submitDecoded(index, m_loadGeneration->load(), std::move(audio));
        return;
    }

    SampleLoadJob job;
    job.sceneId = m_sceneId;
    job.padId = m_padId;
    job.sampleIndex = index;
    job.generation = m_loadGeneration->load();
    job.generationToken = m_loadGeneration;
    job.path = path;
    job.stream = m_stream;
    m_queue->enqueue(job);
    update();
    emit loadStateChanged();
}

void SoundPadWidget::submitDecoded(int index, int generation, DecodedAudio audio)
{
    if (!m_loadGeneration || generation != m_loadGeneration->load()) {
        return;
    }
    if (index < 0 || index >= static_cast<int>(m_slots.size())) {
        return;
    }
    m_incoming.insert_or_assign(index, std::move(audio));
    flushIncoming();
}

void SoundPadWidget::setSpatialisedStereo(int index, bool on)
{
    if (index < 0 || index >= static_cast<int>(m_player.player.size())) {
        return;
    }
    AudioSample* sample = m_player.player[static_cast<size_t>(index)];
    if (sample->audioPlayer->getNumChannels() != 2) {
        return;
    }
    if (sample->isSpatialisedStereo() == on) {
        m_reloads.erase(sample->id);
        return;
    }

    const int serial = ++m_reloadSerial;
    m_reloads[sample->id] = PendingReload{ serial, on };

    SampleLoadJob job;
    job.sceneId = m_sceneId;
    job.padId = m_padId;
    job.generation = m_loadGeneration->load();
    job.generationToken = m_loadGeneration;
    job.path = QString::fromStdString(sample->sample_path);
    job.stream = m_stream;
    job.reloadSampleId = sample->id;
    job.reloadSerial = serial;

    if (!m_queue) {
        submitReload(job, OpenALSoundPlayer::decodeFile(std::filesystem::path(job.path.toStdString()), job.stream));
        return;
    }
    m_queue->enqueue(job);
    emit loadStateChanged();
}

bool SoundPadWidget::isSpatialisedStereo(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_player.player.size())) {
        return false;
    }
    const AudioSample* sample = m_player.player[static_cast<size_t>(index)];
    const auto pending = m_reloads.find(sample->id);
    return pending != m_reloads.end() ? pending->second.spatialise : sample->isSpatialisedStereo();
}

void SoundPadWidget::submitReload(const SampleLoadJob& job, DecodedAudio audio)
{
    if (!m_loadGeneration || job.generation != m_loadGeneration->load()) {
        return;
    }
    const auto pending = m_reloads.find(job.reloadSampleId);
    if (pending == m_reloads.end() || pending->second.serial != job.reloadSerial) {
        return;
    }
    const bool spatialise = pending->second.spatialise;
    m_reloads.erase(pending);

    int index = -1;
    for (int i = 0; i < static_cast<int>(m_player.player.size()); ++i) {
        if (m_player.player[static_cast<size_t>(i)]->id == job.reloadSampleId) {
            index = i;
            break;
        }
    }
    if (index < 0 || !audio.ok) {
        emit loadStateChanged();
        return;
    }

    AudioSample* sample = m_player.player[static_cast<size_t>(index)];
    OpenALSoundPlayer* player = sample->audioPlayer;
    const bool resume = index == m_player.curSound && player->isPlaying();
    const int positionMs = player->getPositionMS();
    const float reverb = player->getReverbSend();
    const float reverb2 = player->getReverbSend2();

    if (!player->uploadDecoded(std::move(audio), spatialise)) {
        qWarning() << "SoundPadWidget: couldn't reload" << job.path;
        emit loadStateChanged();
        return;
    }
    sample->setPitch(sample->getPitch());
    player->setReverbSend(reverb);
    player->setReverbSend2(reverb2);
    if (resume) {
        player->setPositionMS(positionMs);
        player->setPaused(false);
    }
    update();
    emit loadStateChanged();
}

bool SoundPadWidget::commitDecoded(int slotIndex, DecodedAudio audio)
{
    if (slotIndex < 0 || slotIndex >= static_cast<int>(m_slots.size()) || !audio.ok) {
        return false;
    }
    auto* sample = new AudioSample();
    sample->audioPlayer = new OpenALSoundPlayer();
    int maxId = 0;
    for (AudioSample* existing : m_player.player) {
        maxId = std::max(maxId, existing->id);
    }
    sample->id = m_player.player.empty() ? 0 : maxId + 1;
    const LoadSlot& slot = m_slots[static_cast<size_t>(slotIndex)];
    if (!sample->audioPlayer->uploadDecoded(std::move(audio), slot.spatialise)) {
        delete sample;
        return false;
    }
    sample->sample_path = slot.path.toStdString();
    sample->setPitch(slot.pitch);
    sample->setGain(slot.gain);
    sample->setPan(slot.pan);
    if (slot.panRandom) {
        m_player.setRandomPan(true);
    }
    m_player.player.push_back(sample);
    setupLoadedSound(slot.path);
    m_player.recalculateDelay(static_cast<int>(m_player.player.size()) - 1);
    m_player.setReverbSend(m_reverb);
    m_player.setReverbSend2(m_reverb2);
    return true;
}

void SoundPadWidget::flushIncoming()
{
    while (m_incoming.find(m_nextCommit) != m_incoming.end()) {
        DecodedAudio audio = std::move(m_incoming[m_nextCommit]);
        m_incoming.erase(m_nextCommit);
        const int slotIndex = m_nextCommit++;
        if (!commitDecoded(slotIndex, std::move(audio))) {
            m_slots[static_cast<size_t>(slotIndex)].failed = true;
        }
    }
    update();
    emit loadStateChanged();
    if (m_loadTotal > 0 && m_nextCommit >= m_loadTotal && !m_finishSent) {
        m_finishSent = true;
        emit loadingFinished();
        if (m_notifyWhenDone) {
            m_notifyWhenDone = false;
            emit filesDropped();
        }
    }
}

std::vector<SoundPadWidget::LoadSlot> SoundPadWidget::sampleSpecs() const
{
    if (!isLoading()) {
        std::vector<LoadSlot> specs;
        const int count = std::min(static_cast<int>(m_player.player.size()), static_cast<int>(m_soundPaths.size()));
        for (int i = 0; i < count; ++i) {
            LoadSlot spec;
            spec.path = QString::fromStdString(m_soundPaths[static_cast<size_t>(i)]);
            spec.pitch = m_player.player[static_cast<size_t>(i)]->getPitch();
            spec.gain = m_player.player[static_cast<size_t>(i)]->getGain();
            spec.pan = m_player.player[static_cast<size_t>(i)]->getPan();
            spec.panRandom = m_player.isRandomPan();
            spec.spatialise = isSpatialisedStereo(i);
            specs.push_back(spec);
        }
        return specs;
    }

    std::vector<LoadSlot> specs;
    int playerIndex = 0;
    for (int i = 0; i < static_cast<int>(m_slots.size()); ++i) {
        if (m_slots[static_cast<size_t>(i)].failed) {
            continue;
        }
        LoadSlot spec = m_slots[static_cast<size_t>(i)];
        if (i < m_nextCommit && playerIndex < static_cast<int>(m_player.player.size())) {
            AudioSample* sample = m_player.player[static_cast<size_t>(playerIndex)];
            spec.pitch = sample->getPitch();
            spec.gain = sample->getGain();
            spec.pan = sample->getPan();
            spec.panRandom = m_player.isRandomPan();
            spec.spatialise = isSpatialisedStereo(playerIndex);
            ++playerIndex;
        }
        specs.push_back(spec);
    }
    return specs;
}

void SoundPadWidget::loadFromJson(const QJsonObject& root)
{
    // Older files store pads under "scene<id>" but scene names under "scene<position>", so search every scene.
    const QString prefix = QString("%1-%2").arg(m_sceneId).arg(m_padId);
    QJsonObject pad;
    for (auto it = root.begin(); it != root.end() && pad.isEmpty(); ++it) {
        pad = it.value().toObject().value(prefix).toObject();
    }
    if (pad.isEmpty()) {
        return;
    }

    clearPad();
    m_notifyWhenDone = false;
    m_stream = pad.value(QStringLiteral("isstream")).toBool(true);
    m_player.minDelay = pad.value(QStringLiteral("mindelay")).toInt();
    m_player.maxDelay = pad.value(QStringLiteral("maxdelay")).toInt();
    m_player.bRandomPlayback = pad.value(QStringLiteral("playrandom")).toBool();
    setLooping(pad.value(QStringLiteral("loop")).toBool(m_config->loopByDefault));
    setSoundName(pad.value(QStringLiteral("soundname")).toString());
    setPadVolume(static_cast<float>(pad.value(QStringLiteral("volume")).toDouble(0.7)));
    m_sampleRate = pad.value(QStringLiteral("samplerate")).toInt();
    m_channels = pad.value(QStringLiteral("channels")).toInt();
    m_reverb = static_cast<float>(pad.value(QStringLiteral("reverbsend")).toDouble());
    m_reverb2 = static_cast<float>(pad.value(QStringLiteral("reverbsend2")).toDouble());
    m_player.setup(m_config, m_padId);
    m_player.setReverbSend(m_reverb);
    m_player.setReverbSend2(m_reverb2);

    const QJsonObject samples = pad.value(QStringLiteral("samples")).toObject();
    for (int i = 0;; ++i) {
        const QJsonObject sample = samples.value(QString("sample-%1").arg(i)).toObject();
        if (sample.isEmpty()) {
            break;
        }
        const QString path = sample.value(QStringLiteral("path")).toString();
        if (path.isEmpty()) {
            continue;
        }
        enqueueSample(path,
            static_cast<float>(sample.value(QStringLiteral("pitch")).toDouble(1.0)),
            static_cast<float>(sample.value(QStringLiteral("gain")).toDouble(1.0)),
            static_cast<float>(sample.value(QStringLiteral("pan")).toDouble()),
            sample.value(QStringLiteral("panrandom")).toBool(),
            sample.value(QStringLiteral("spatialise")).toBool());
    }
}

void SoundPadWidget::saveToJson(QJsonObject& sceneObj) const
{
    if (m_soundPaths.empty()) {
        return;
    }

    const QString prefix = QString("%1-%2").arg(m_sceneId).arg(m_padId);
    QJsonObject pad;
    pad.insert(QStringLiteral("isstream"), m_stream);
    pad.insert(QStringLiteral("loop"), isLooping());
    pad.insert(QStringLiteral("soundname"), soundName());
    pad.insert(QStringLiteral("volume"), padVolume());
    pad.insert(QStringLiteral("samplerate"), m_sampleRate);
    pad.insert(QStringLiteral("channels"), m_channels);
    pad.insert(QStringLiteral("playrandom"), m_player.bRandomPlayback);
    pad.insert(QStringLiteral("panrandom"), m_player.isRandomPan());
    pad.insert(QStringLiteral("reverbsend"), m_player.getReverbSend());
    pad.insert(QStringLiteral("reverbsend2"), m_player.player.empty() ? m_reverb2 : m_player.getReverbSend2());
    pad.insert(QStringLiteral("mindelay"), m_player.minDelay);
    pad.insert(QStringLiteral("maxdelay"), m_player.maxDelay);

    QJsonObject samples;
    const int count = std::min(static_cast<int>(m_player.player.size()), static_cast<int>(m_soundPaths.size()));
    for (int i = 0; i < count; ++i) {
        QJsonObject sample;
        QString path = QString::fromStdString(m_soundPaths[i]);
        if (path.isEmpty()) {
            path = QString::fromStdString(m_player.player[i]->sample_path);
        }
        sample.insert(QStringLiteral("path"), path);
        sample.insert(QStringLiteral("gain"), m_player.player[i]->getGain());
        sample.insert(QStringLiteral("pitch"), m_player.player[i]->getPitch());
        sample.insert(QStringLiteral("pan"), m_player.player[i]->getPan());
        sample.insert(QStringLiteral("panrandom"), m_player.isRandomPan());
        sample.insert(QStringLiteral("spatialise"), isSpatialisedStereo(i));
        sample.insert(QStringLiteral("duration"), m_player.player[i]->audioPlayer->getDuration());
        samples.insert(QString("sample-%1").arg(i), sample);
    }
    pad.insert(QStringLiteral("samples"), samples);
    sceneObj.insert(prefix, pad);
}

void SoundPadWidget::loadFiles(const QStringList& paths, bool clearExisting)
{
    if (paths.isEmpty()) {
        return;
    }
    if (clearExisting) {
        clearPad();
    }
    m_notifyWhenDone = true;
    if (soundName().isEmpty()) {
        setSoundName(QFileInfo(paths.front()).completeBaseName());
    }
    for (const QString& path : paths) {
        enqueueSample(path, 1.0f, 1.0f, 0.0f, false, false);
    }
}

void SoundPadWidget::clearPad()
{
    cancelLoading();
    m_player.stop();
    m_player.close();
    m_soundPaths.clear();
    m_sampleRate = 0;
    m_channels = 0;
    m_name->clear();
    setLooping(m_config->loopByDefault);
    setPadVolume(0.7f);
    m_player.minDelay = 0;
    m_player.maxDelay = 0;
    m_player.bRandomPlayback = false;
    m_player.bRandomPan = false;
    m_playhead->setProgress(0.0f);
    m_playhead->setTimeText(QString());
    m_playhead->setVisible(false);
    m_reverb = 0.0f;
    m_reverb2 = 0.0f;
}

void SoundPadWidget::setReverbSend(float send)
{
    m_reverb = send;
    m_player.setReverbSend(send);
}

void SoundPadWidget::setReverbSend2(float send)
{
    m_reverb2 = send;
    m_player.setReverbSend2(send);
}

void SoundPadWidget::copyFrom(SoundPadWidget& other)
{
    const std::vector<LoadSlot> specs = other.sampleSpecs();
    const bool stream = other.m_stream;
    const int sampleRate = other.m_sampleRate;
    const int channels = other.m_channels;
    const bool looping = other.isLooping();
    const int minDelay = other.m_player.minDelay;
    const int maxDelay = other.m_player.maxDelay;
    const bool randomPlayback = other.m_player.bRandomPlayback;
    const bool randomPan = other.m_player.isRandomPan();
    const QString name = other.soundName();
    const float volume = other.padVolume();
    const float reverb = (other.isLoading() || other.m_player.player.empty())
        ? other.m_reverb
        : other.m_player.getReverbSend();
    const float reverb2 = (other.isLoading() || other.m_player.player.empty())
        ? other.m_reverb2
        : other.m_player.getReverbSend2();

    clearPad();
    m_stream = stream;
    m_sampleRate = sampleRate;
    m_channels = channels;
    setLooping(looping);
    m_player.minDelay = minDelay;
    m_player.maxDelay = maxDelay;
    m_player.bRandomPlayback = randomPlayback;
    m_player.setRandomPan(randomPan);
    setSoundName(name);
    setPadVolume(volume);
    m_reverb = reverb;
    m_reverb2 = reverb2;
    m_notifyWhenDone = true;
    for (const LoadSlot& spec : specs) {
        enqueueSample(spec.path, spec.pitch, spec.gain, spec.pan, spec.panRandom || randomPan, spec.spatialise);
    }
}

int SoundPadWidget::moveSample(int from, int insertIndex)
{
    if (isLoading()) {
        return -1;
    }
    const int count = static_cast<int>(m_player.player.size());
    if (from < 0 || from >= count || insertIndex < 0 || insertIndex > count) {
        return -1;
    }
    if (insertIndex == from || insertIndex == from + 1) {
        return -1;
    }

    AudioSample* sample = m_player.player[static_cast<size_t>(from)];
    m_player.player.erase(m_player.player.begin() + from);
    const int dest = insertIndex > from ? insertIndex - 1 : insertIndex;
    m_player.player.insert(m_player.player.begin() + dest, sample);

    if (from < static_cast<int>(m_soundPaths.size())) {
        std::string path = std::move(m_soundPaths[static_cast<size_t>(from)]);
        m_soundPaths.erase(m_soundPaths.begin() + from);
        const int pathDest = std::min(dest, static_cast<int>(m_soundPaths.size()));
        m_soundPaths.insert(m_soundPaths.begin() + pathDest, std::move(path));
    }

    const auto adjust = [from, dest](int idx) {
        if (idx == from) {
            return dest;
        }
        if (from < idx && dest >= idx) {
            return idx - 1;
        }
        if (from > idx && dest <= idx) {
            return idx + 1;
        }
        return idx;
    };
    m_player.curSound = adjust(m_player.curSound);
    return dest;
}

void SoundPadWidget::removeSampleAt(int index)
{
    if (isLoading()) {
        return;
    }
    if (index < 0 || index >= static_cast<int>(m_player.player.size())) {
        return;
    }
    m_player.stop();
    m_reloads.erase(m_player.player[index]->id);
    delete m_player.player[index];
    m_player.player.erase(m_player.player.begin() + index);
    m_soundPaths.erase(m_soundPaths.begin() + index);
    if (m_player.player.empty()) {
        clearPad();
    }
}

bool SoundPadWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress && static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
        emit padClicked(m_padId);
    }
    return QWidget::eventFilter(watched, event);
}

void SoundPadWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragStart = event->pos();
        emit padClicked(m_padId);
    }
    QWidget::mousePressEvent(event);
}

void SoundPadWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (!(event->buttons() & Qt::LeftButton)) {
        return;
    }
    if ((event->pos() - m_dragStart).manhattanLength() < QApplication::startDragDistance()) {
        return;
    }
    if (isLoading() || !isLoaded()) {
        return;
    }
    emit padDragStarted(m_padId);
    auto* drag = new QDrag(this);
    auto* mime = new QMimeData();
    mime->setData(QStringLiteral("application/x-feedra-pad"), QByteArray::number(m_padId));
    drag->setMimeData(mime);
    drag->exec(Qt::CopyAction);
}

void SoundPadWidget::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls() || event->mimeData()->hasFormat(QStringLiteral("application/x-feedra-pad"))) {
        event->acceptProposedAction();
    }
}

void SoundPadWidget::dropEvent(QDropEvent* event)
{
    if (event->mimeData()->hasFormat(QStringLiteral("application/x-feedra-pad"))) {
        const int from = event->mimeData()->data(QStringLiteral("application/x-feedra-pad")).toInt();
        emit padDropped(from, m_padId);
        event->acceptProposedAction();
        return;
    }
    if (event->mimeData()->hasUrls()) {
        QStringList paths;
        for (const QUrl& url : event->mimeData()->urls()) {
            if (url.isLocalFile()) {
                paths << url.toLocalFile();
            }
        }
        loadFiles(paths, true);
        emit padClicked(m_padId);
        event->acceptProposedAction();
    }
}

void SoundPadWidget::chooseFiles()
{
    QString start = m_config->lastPath;
    if (start.isEmpty()) {
        start = m_config->libraryLocation();
    }
    const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Load files"), start, audioNameFilters().join(QStringLiteral(";;")));
    if (!paths.isEmpty()) {
        loadFiles(paths, true);
        emit padClicked(m_padId);
    }
}

void SoundPadWidget::setupLoadedSound(const QString& path)
{
    m_soundPaths.push_back(path.toStdString());
    m_config->lastPath = QFileInfo(path).absolutePath();
    m_player.setLoop(isLooping());
    m_sampleRate = m_player.getSampleRate();
    m_channels = m_player.getNumChannels();
}

void SoundPadWidget::applyVolume()
{
    if (!m_player.isLoaded() || m_player.player.empty()) {
        return;
    }
    // SoundPlayer multiplies in each sample's own gain, and also applies this to a sample
    // right before it starts, so a newly selected sample never starts at the wrong volume.
    m_player.setBaseVolume(padVolume() * m_config->masterVolume() * m_config->masterFade() * m_fadeVolume);
}

void SoundPadWidget::updateVolumeLabel()
{
    if (!m_volumeValue) {
        return;
    }
    m_volumeValue->setText(QString::number(padVolume(), 'f', 2));
}

QString SoundPadWidget::remainingTimeText() const
{
    float timeLeft = 0.0f;
    if (m_player.isPlayingDelay()) {
        timeLeft = (1.0f - m_player.getPosition()) * m_player.getTotalDelay();
    } else {
        timeLeft = (1.0f - m_player.getPosition()) * m_player.getDuration();
    }
    const int total = static_cast<int>(timeLeft);
    const int minutes = (total / 60) % 60;
    const int seconds = total % 60;
    const int hours = total / 3600;
    if (hours > 0) {
        return QString("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QChar('0'))
            .arg(seconds, 2, 10, QChar('0'));
    }
    return QString("%1:%2").arg(minutes, 2, 10, QChar('0')).arg(seconds, 2, 10, QChar('0'));
}
