#include "widgets/BottomPanel.h"

#include "AppConfig.h"
#include "AudioDevice.h"
#include "AudioSample.h"
#include "FsPath.h"
#include "Intensity.h"
#include "OpenALSoundPlayer.h"
#include "Pad.h"
#include "widgets/TabButton.h"
#include "widgets/WaveformWidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

BottomPanel::BottomPanel(AppConfig* config, QWidget* parent)
    : QWidget(parent)
    , m_config(config)
{
    setObjectName(QStringLiteral("BottomPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    auto* bottomLayout = new QVBoxLayout(this);
    bottomLayout->setContentsMargins(0, 0, 0, 0);
    bottomLayout->setSpacing(0);

    auto* tabBar = new QWidget(this);
    tabBar->setObjectName(QStringLiteral("BottomTabBar"));
    tabBar->setAttribute(Qt::WA_StyledBackground, true);
    auto* tabRow = new QHBoxLayout(tabBar);
    tabRow->setContentsMargins(8, 0, 8, 0);
    tabRow->setSpacing(0);
    // Shown in this order; the indices are the Tab values (which the settings file saves).
    const QString tabNames[3] = { tr("Info"), tr("Pad"), tr("Waveform") };
    for (int tab = 0; tab < 3; ++tab) {
        m_tabs[tab] = new QPushButton(tabNames[tab], tabBar);
        m_tabs[tab]->setObjectName(QStringLiteral("BottomTab"));
        m_tabs[tab]->setFocusPolicy(Qt::NoFocus);
        connect(m_tabs[tab], &QPushButton::clicked, this, [this, tab]() { tabClicked(tab); });
    }
    m_collapse = new QPushButton(QStringLiteral("▴"), tabBar);
    m_collapse->setObjectName(QStringLiteral("BottomCollapse"));
    m_collapse->setFocusPolicy(Qt::NoFocus);
    m_collapse->setFixedSize(28, 24);
    m_collapse->setToolTip(tr("Hide panel"));
    tabRow->addWidget(m_tabs[WaveformTab], 0, Qt::AlignBottom);
    tabRow->addWidget(m_tabs[PadTab], 0, Qt::AlignBottom);
    tabRow->addWidget(m_tabs[InfoTab], 0, Qt::AlignBottom);
    tabRow->addStretch();
    tabRow->addWidget(m_collapse, 0, Qt::AlignVCenter);
    connect(m_collapse, &QPushButton::clicked, this, [this]() { emit collapseRequested(!m_collapsed); });

    m_stack = new QStackedWidget(this);
    m_stack->setObjectName(QStringLiteral("BottomPages"));

    auto* samplePage = new QWidget(m_stack);
    auto* sampleLayout = new QVBoxLayout(samplePage);
    sampleLayout->setContentsMargins(20, 12, 20, 12);
    m_info = new QLabel(samplePage);
    m_info->setObjectName(QStringLiteral("SampleInfo"));
    m_info->setWordWrap(true);
    m_info->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_info->setMinimumHeight(m_info->fontMetrics().lineSpacing() * 6);
    sampleLayout->addWidget(m_info);
    sampleLayout->addStretch();

    auto* padPage = new QWidget(m_stack);
    // Delay and playback options on the left, the four effect sends on the right.
    auto* padRow = new QHBoxLayout(padPage);
    padRow->setContentsMargins(20, 12, 20, 12);
    padRow->setSpacing(36);
    auto* padGrid = new QGridLayout();
    padGrid->setContentsMargins(0, 0, 0, 0);
    padGrid->setHorizontalSpacing(14);
    padGrid->setVerticalSpacing(10);
    auto makeDelayBox = [padPage]() {
        auto* box = new QSpinBox(padPage);
        box->setRange(0, 600);
        box->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
        box->setAlignment(Qt::AlignRight);
        box->setMinimumWidth(72);
        box->setMaximumWidth(140);
        box->setSuffix(tr(" s"));
        return box;
    };
    m_minDelay = makeDelayBox();
    m_maxDelay = makeDelayBox();
    for (int bus = 0; bus < OpenALSoundPlayer::kSendCount; ++bus) {
        m_sendSliders[bus] = new QSlider(Qt::Horizontal, padPage);
        m_sendSliders[bus]->setRange(0, 1000);
        m_sendSliders[bus]->setMinimumWidth(160);
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

    // The pad's own fade in and out.
    auto* playGrid = new QGridLayout();
    playGrid->setContentsMargins(0, 0, 0, 0);
    playGrid->setHorizontalSpacing(10);
    playGrid->setVerticalSpacing(10);
    const auto makeAmount = [padPage](double max, double step, const QString& suffix, int decimals) {
        auto* box = new QDoubleSpinBox(padPage);
        box->setRange(0.0, max);
        box->setSingleStep(step);
        box->setDecimals(decimals);
        box->setSuffix(suffix);
        box->setAlignment(Qt::AlignRight);
        box->setKeyboardTracking(false);
        box->setMinimumWidth(90);
        box->setMaximumWidth(120);
        box->setEnabled(false);
        return box;
    };
    m_fadeIn = makeAmount(10.0, 0.1, tr(" s"), 1);
    m_fadeIn->setToolTip(tr("Playing the pad (its button, key or MIDI, or its scene) fades it in over this time"));
    m_fadeOut = makeAmount(10.0, 0.1, tr(" s"), 1);
    m_fadeOut->setToolTip(tr("Pausing or stopping the pad from its button, key or MIDI fades it out over this time"));
    playGrid->addWidget(new QLabel(tr("Fade in"), padPage), 0, 0);
    playGrid->addWidget(m_fadeIn, 0, 1);
    playGrid->addWidget(new QLabel(tr("Fade out"), padPage), 1, 0);
    playGrid->addWidget(m_fadeOut, 1, 1);
    // The pad's intensity layer, for the scene's Intensity slider in the header.
    m_layer = new QComboBox(padPage);
    for (int layer = Intensity::None; layer <= Intensity::Combat; ++layer) {
        m_layer->addItem(Intensity::layerName(layer), layer);
    }
    m_layer->setMinimumWidth(90);
    m_layer->setMaximumWidth(120);
    m_layer->setEnabled(false);
    m_layer->setToolTip(tr("Calm, Tense or Combat: the scene's Intensity slider (in the header) crossfades "
                           "between its layered pads, which all start with the scene and play together.\n"
                           "None: the slider leaves this pad alone."));
    playGrid->addWidget(new QLabel(tr("Layer"), padPage), 2, 0);
    playGrid->addWidget(m_layer, 2, 1);
    playGrid->setRowStretch(3, 1);

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
    padRow->addLayout(playGrid, 0);
    padRow->addLayout(sendGrid, 1);
    refreshSendLabels();

    m_stack->addWidget(samplePage);
    m_stack->addWidget(padPage);
    m_waveform = new WaveformWidget(m_stack);
    m_stack->addWidget(m_waveform);

    // Tall enough for every page, so nothing (like the Random Playback box) is clipped.
    m_pageHeight = std::max({m_info->minimumHeight() + 24, padPage->sizeHint().height(), 148});
    m_stack->setFixedHeight(m_pageHeight);
    bottomLayout->addWidget(tabBar);
    bottomLayout->addWidget(m_stack);

    connect(m_waveform, &WaveformWidget::seekRequested, this, [this](float pct) {
        const int shown = waveformSampleIndex();
        if (m_pad && shown >= 0) {
            m_pad->soundPlayer().seekSample(shown, pct);
        }
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
            if (m_pad) {
                for (AudioSample* candidate : m_pad->soundPlayer().player) {
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

    // Min and max push each other, and every sample's delay is picked again.
    const auto delayChanged = [this](int v, bool isMin) {
        if (m_updating || !m_pad) return;
        emit editStarting(m_pad);
        SoundPlayer& player = m_pad->soundPlayer();
        if (isMin) {
            player.setMinDelay(v);
            if (v > player.getMaxDelay()) {
                player.setMaxDelay(v);
            }
        } else {
            player.setMaxDelay(v);
            if (v < player.getMinDelay()) {
                player.setMinDelay(v);
            }
        }
        for (int i = 0; i < static_cast<int>(player.player.size()); ++i) {
            player.recalculateDelay(i);
        }
        m_pad->notifyChanged(); // the delay border comes and goes with the delay
        emit edited(m_pad, tr("Delay"));
        refreshPadControls();
    };
    connect(m_minDelay, qOverload<int>(&QSpinBox::valueChanged), this, [delayChanged](int v) { delayChanged(v, true); });
    connect(m_maxDelay, qOverload<int>(&QSpinBox::valueChanged), this, [delayChanged](int v) { delayChanged(v, false); });
    for (int bus = 0; bus < OpenALSoundPlayer::kSendCount; ++bus) {
        connect(m_sendSliders[bus], &QSlider::valueChanged, this, [this, bus](int v) {
            if (m_updating) return;
            edit(tr("Effect send"), [&](Pad* pad) { pad->setSend(bus, v / 1000.0f); });
        });
    }
    connect(m_randomPlayback, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updating) return;
        edit(tr("Random playback"), [&](Pad* pad) { pad->soundPlayer().setRandomPlayback(on); });
    });
    connect(m_scenePad, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updating) return;
        edit(tr("Plays with scene"), [&](Pad* pad) { pad->setScenePad(on); });
    });
    connect(m_resetDelay, &QPushButton::clicked, this, [this]() {
        // Through the spin boxes, so the pad, its samples' delays and the border all follow.
        m_minDelay->setValue(0);
        m_maxDelay->setValue(0);
        refreshDelayReadout();
    });
    connect(m_delayOn, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updating) return;
        edit(tr("Delay"), [&](Pad* pad) {
            pad->soundPlayer().setDelayEnabled(on);
            pad->notifyChanged(); // the delay border comes and goes with the delay
        });
        m_minDelay->setEnabled(on);
        m_maxDelay->setEnabled(on);
        refreshDelayReadout();
    });
    connect(m_repeat, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updating) return;
        edit(tr("Repeat"), [&](Pad* pad) { pad->setRepeating(on); });
    });
    connect(m_fadeIn, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double v) {
        if (m_updating || !m_pad) return;
        edit(tr("Fade time"), [&](Pad* pad) { pad->setFadeInTime(static_cast<float>(v)); });
    });
    connect(m_layer, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (m_updating || !m_pad) return;
        const int layer = m_layer->itemData(index).toInt();
        edit(tr("Layer"), [&](Pad* pad) { pad->setLayer(layer); });
    });
    connect(m_fadeOut, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double v) {
        if (m_updating || !m_pad) return;
        edit(tr("Fade time"), [&](Pad* pad) { pad->setFadeOutTime(static_cast<float>(v)); });
    });

    setCurrentTab(WaveformTab);
}

void BottomPanel::setPad(Pad* pad)
{
    m_pad = pad;
}

void BottomPanel::refreshPadControls()
{
    Pad* pad = m_pad;
    m_updating = true;
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
            m_sendSliders[bus]->setEnabled(AudioDevice::sendAvailable(bus));
        }
        m_fadeIn->setValue(pad->fadeInTime());
        m_fadeOut->setValue(pad->fadeOutTime());
        m_fadeIn->setEnabled(true);
        m_fadeOut->setEnabled(true);
        m_layer->setCurrentIndex(m_layer->findData(pad->layer()));
        m_layer->setEnabled(true);
    } else {
        m_info->setText(tr("channels: —\nformat: —\nsub-format: —\nsample rate: —\npath: —\nNum sounds: —"));
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
        m_fadeIn->setEnabled(false);
        m_fadeOut->setEnabled(false);
        m_layer->setCurrentIndex(0);
        m_layer->setEnabled(false);
    }
    m_updating = false;
}

void BottomPanel::syncScenePad()
{
    if (!m_pad) {
        return;
    }
    const bool was = m_updating;
    m_updating = true;
    m_scenePad->setChecked(m_pad->isScenePad());
    m_updating = was;
}

void BottomPanel::refreshSendLabels()
{
    for (int which = 0; which < OpenALSoundPlayer::kEaxReverbCount; ++which) {
        const QString preset = QString::fromStdString(
            AudioDevice::reverbPresetLabel(AudioDevice::reverbPresetIndex(which)));
        m_sendLabels[which]->setText(tr("%1 reverb send").arg(preset));
        m_sendLabels[which]->setToolTip(tr("Send into EAX reverb %1 (%2). Choose the preset in Settings.").arg(which + 1).arg(preset));
    }
    for (int which = 0; which < OpenALSoundPlayer::kConvolutionCount; ++which) {
        QLabel* label = m_sendLabels[OpenALSoundPlayer::kEaxReverbCount + which];
        const QString file = FsPath::toQString(AudioDevice::convolutionImpulsePath(which));
        const QString name = file.isEmpty() ? tr("Convolution %1").arg(which + 1) : QFileInfo(file).completeBaseName();
        label->setText(tr("%1 reverb send").arg(name));
        label->setToolTip(tr("Send into convolution reverb %1 (%2). Choose the impulse in Settings.").arg(which + 1).arg(file));
    }
}

void BottomPanel::tick()
{
    refreshSampleInfo();
    refreshDelayReadout();
    refreshWaveform();
}

void BottomPanel::refreshSampleInfo()
{
    Pad* pad = m_pad;
    if (!pad || !pad->isLoaded() || pad->soundPlayer().player.empty()) {
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
    if (cur < static_cast<int>(pad->samplePaths().size())) {
        path = pad->samplePath(cur);
    }
    m_info->setText(tr("channels: %1\nformat: %2\nsub-format: %3\nsample rate: %4\npath: %5\nNum sounds: %6")
        .arg(audio->getNumChannels())
        .arg(QString::fromStdString(audio->getFormatString()))
        .arg(QString::fromStdString(audio->getSubFormatString()))
        .arg(audio->getSampleRate())
        .arg(path)
        .arg(player.player.size()));
}

void BottomPanel::refreshDelayReadout()
{
    Pad* pad = m_pad;
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

void BottomPanel::refreshWaveform()
{
    // Only does work while the tab is on screen. Reads positions the same way the sample rows
    // do (no stream locks), so it cannot hold up playback.
    if (m_collapsed || m_stack->currentWidget() != m_waveform || !isVisible()) {
        return;
    }
    Pad* pad = m_pad;
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
    if (cur < static_cast<int>(pad->samplePaths().size())) {
        path = pad->samplePath(cur);
    } else {
        path = FsPath::toQString(audio->getFilePath());
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

int BottomPanel::waveformSampleIndex() const
{
    Pad* pad = m_pad;
    if (!pad || !pad->isLoaded() || pad->soundPlayer().player.empty()) {
        return -1;
    }
    const SoundPlayer& player = pad->soundPlayer();
    const int count = static_cast<int>(player.player.size());
    if (m_config->activeSampleIdx >= 0 && m_config->activeSampleIdx < count) {
        return m_config->activeSampleIdx;
    }
    return std::clamp(player.getCurSound(), 0, count - 1);
}

AudioSample* BottomPanel::waveformSample() const
{
    const int index = waveformSampleIndex();
    return index < 0 ? nullptr : m_pad->soundPlayer().player[static_cast<size_t>(index)];
}

void BottomPanel::applyLoopRegion(AudioSample* sample, const LoopRegion& region)
{
    edit(tr("Loop points"), [&](Pad*) { sample->setLoopRegion(region); });
    emit loopRegionChanged(sample);
    refreshWaveform();
}

int BottomPanel::currentTab() const
{
    return m_stack->currentIndex();
}

void BottomPanel::setCurrentTab(int tab)
{
    m_stack->setCurrentIndex(std::clamp(tab, 0, 2));
    refreshTabButtons();
}

void BottomPanel::tabClicked(int tab)
{
    // The open tab's button closes the panel; any other opens that tab.
    if (!m_collapsed && currentTab() == tab) {
        emit collapseRequested(true);
        return;
    }
    setCurrentTab(tab);
    if (m_collapsed) {
        emit collapseRequested(false);
    }
}

void BottomPanel::setCollapsedState(bool collapsed)
{
    m_collapsed = collapsed;
    m_stack->setVisible(!collapsed);
    m_collapse->setText(collapsed ? QStringLiteral("▾") : QStringLiteral("▴"));
    m_collapse->setToolTip(collapsed ? tr("Show panel") : tr("Hide panel"));
    refreshTabButtons();
}

void BottomPanel::refreshTabButtons()
{
    for (int tab = 0; tab < 3; ++tab) {
        setTabButtonActive(m_tabs[tab], !m_collapsed && currentTab() == tab);
    }
}
