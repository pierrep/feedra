#include "widgets/EditorPanel.h"

#include "AppConfig.h"
#include "AudioFormats.h"
#include "AudioSample.h"
#include "OpenALSoundPlayer.h"
#include "Pad.h"
#include "VolumeDb.h"
#include "widgets/ReorderListHost.h"
#include "widgets/SampleRowWidget.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

constexpr float kSampleGainMinDb = -12.0f;
constexpr float kSampleGainMaxDb = 24.0f;

} // namespace

EditorPanel::EditorPanel(AppConfig* config, QWidget* parent)
    : QWidget(parent)
    , m_config(config)
{
    auto* editLayout = new QVBoxLayout(this);
    editLayout->setContentsMargins(12, 12, 4, 12);
    editLayout->setSpacing(10);
    m_title = new QLabel(this);
    m_title->setObjectName(QStringLiteral("EditTitle"));
    m_title->setWordWrap(true);
    m_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_add = new QPushButton(tr("+  Add"), this);
    m_add->setObjectName(QStringLiteral("AddSample"));
    m_add->setFixedHeight(26); // matches the sample rows
    m_add->setCursor(Qt::PointingHandCursor);
    m_add->setToolTip(tr("Add samples to this pad"));
    auto* editTop = new QHBoxLayout();
    editTop->setContentsMargins(0, 0, 8, 0);
    editTop->addWidget(m_title, 1);
    editTop->addWidget(m_add);
    editLayout->addLayout(editTop);

    m_scroll = new QScrollArea(this);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scroll->viewport()->setAutoFillBackground(true);
    auto* sampleHost = new ReorderListHost(SampleRowWidget::dragMimeType(), m_scroll);
    sampleHost->setObjectName(QStringLiteral("SampleListHost"));
    m_listLayout = new QVBoxLayout(sampleHost);
    m_listLayout->setContentsMargins(0, 4, 8, 4);
    m_listLayout->setSpacing(3);
    m_listLayout->addStretch();
    m_scroll->setWidget(sampleHost);
    editLayout->addWidget(m_scroll, 1);
    sampleHost->setAcceptsFiles(true);
    connect(sampleHost, &ReorderListHost::filesDropped, this, [this](const QStringList& paths) {
        if (m_pad && Pad::confirmFileCount(this, paths.size())) {
            addFiles(paths);
        }
    }, Qt::QueuedConnection); // after the drag finishes, so a question box can open
    connect(sampleHost, &ReorderListHost::itemReordered, this, [this](int sampleId, int insertIndex) {
        if (!m_pad) {
            return;
        }
        const auto& players = m_pad->soundPlayer().player;
        for (int i = 0; i < static_cast<int>(players.size()); ++i) {
            if (players[i]->id == sampleId) {
                moveSample(i, insertIndex);
                return;
            }
        }
    }, Qt::QueuedConnection); // after QDrag::exec returns, so the source row is still alive

    m_pan = new QSlider(Qt::Horizontal, this);
    m_pan->setRange(0, 1000);
    m_pan->setValue(500);
    m_pitch = new QSlider(Qt::Horizontal, this);
    m_pitch->setRange(0, 3000);
    m_pitch->setValue(1000);
    m_gain = new QSlider(Qt::Horizontal, this);
    m_gain->setRange(0, VolumeDb::sliderSpan(kSampleGainMinDb, kSampleGainMaxDb));
    m_gain->setValue(VolumeDb::toSlider(VolumeDb::kUnityDb, kSampleGainMinDb));
    m_randomPan = new QCheckBox(tr("Random Pan"), this);
    m_width = new QSlider(Qt::Horizontal, this);
    m_width->setRange(0, 100);
    m_width->setValue(100);
    m_width->setToolTip(tr("Stereo width: 100% is the file's full stereo image, 0% plays it as mono at the pan position."));
    m_loop = new QCheckBox(tr("Loop"), this);
    m_loop->setToolTip(tr("Loop this sample seamlessly between its loop points (set them on the Waveform tab).\n"
                          "The pad's loop icon shows and switches this for the sample that is playing."));

    // Displayed value = slider value * scale + offset; the spin box range follows the slider range.
    auto makeValueBox = [this](QSlider* slider, double scale, double offset) {
        auto* box = new QDoubleSpinBox(this);
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

    m_controls = new QWidget(this);
    auto* controlsLayout = new QVBoxLayout(m_controls);
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    auto addLabeled = [&](const QString& label, QWidget* w, QWidget* value) {
        auto* row = new QHBoxLayout();
        row->setContentsMargins(0, 0, 8, 0);
        auto* caption = new QLabel(label, m_controls);
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
    checkLayout->addWidget(m_loop);
    controlsLayout->addLayout(checkLayout);
    editLayout->addWidget(m_controls);

    connect(m_loop, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updating) return;
        if (AudioSample* sample = selectedSample()) {
            LoopRegion region = sample->loopRegion();
            region.loop = on;
            sample->setLoopRegion(region);
            emit loopRegionChanged(sample);
        }
    });
    connect(m_pan, &QSlider::valueChanged, this, [this](int v) {
        if (m_updating) return;
        if (AudioSample* sample = selectedSample(); sample && sample->audioPlayer->canPan()) {
            sample->setPan((v / 1000.0f) * 2.0f - 1.0f);
        }
    });
    connect(m_pitch, &QSlider::valueChanged, this, [this](int v) {
        if (m_updating) return;
        if (AudioSample* sample = selectedSample()) {
            sample->setPitch(v / 1000.0f);
        }
    });
    connect(m_gain, &QSlider::valueChanged, this, [this](int v) {
        if (m_updating) return;
        if (AudioSample* sample = selectedSample()) {
            sample->setGain(VolumeDb::toLinear(VolumeDb::fromSlider(v, kSampleGainMinDb)));
        }
    });
    connect(m_randomPan, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updating) return;
        if (m_pad) {
            m_pad->soundPlayer().setRandomPan(on);
        }
    });
    connect(m_width, &QSlider::valueChanged, this, [this](int v) {
        if (m_updating) return;
        if (AudioSample* sample = selectedSample()) {
            sample->setWidth(v / 100.0f);
        }
    });
    connect(m_add, &QPushButton::clicked, this, [this]() {
        if (!m_pad) {
            return;
        }
        const QString start = m_config->loadDialogDir(m_pad->currentSamplePath());
        const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Load files"), start,
            AudioFormats::dialogFilters().join(QStringLiteral(";;")));
        if (!paths.isEmpty()) {
            addFiles(paths);
        }
    });
}

void EditorPanel::setPad(Pad* pad)
{
    if (pad == m_pad) {
        return;
    }
    m_pad = pad;
    // The rows belong to the old pad. refresh() builds the new pad's when the page is shown.
    qDeleteAll(m_rows);
    m_rows.clear();
}

AudioSample* EditorPanel::selectedSample() const
{
    if (!m_pad) {
        return nullptr;
    }
    const auto& players = m_pad->soundPlayer().player;
    const int idx = m_config->activeSampleIdx;
    if (idx < 0 || idx >= static_cast<int>(players.size())) {
        return nullptr;
    }
    return players[static_cast<size_t>(idx)];
}

void EditorPanel::refresh()
{
    if (!m_pad || m_pad->soundPlayer().player.empty()) {
        qDeleteAll(m_rows);
        m_rows.clear();
        m_controls->hide();
        m_title->setText(m_pad ? m_pad->soundName().toUpper() : QString());
        return;
    }
    if (m_rows.isEmpty()) {
        m_config->activeSampleIdx = 0;
        m_config->activeSampleId = m_pad->soundPlayer().player[0]->id;
    }
    m_controls->show();
    rebuildRows();
}

void EditorPanel::rebuildRows()
{
    qDeleteAll(m_rows);
    m_rows.clear();
    if (!m_pad) {
        emit samplesChanged();
        return;
    }
    for (AudioSample* sample : m_pad->soundPlayer().player) {
        auto* row = new SampleRowWidget(sample->id, sample->path, m_listLayout->parentWidget());
        m_listLayout->insertWidget(m_listLayout->count() - 1, row);
        m_rows.push_back(row);
        const auto selectById = [this](int id, bool play) {
            if (!m_pad) {
                return;
            }
            const auto& players = m_pad->soundPlayer().player;
            for (int i = 0; i < static_cast<int>(players.size()); ++i) {
                if (players[static_cast<size_t>(i)]->id == id) {
                    if (play) {
                        m_pad->soundPlayer().playSample(i);
                    }
                    selectSample(i);
                    break;
                }
            }
        };
        connect(row, &SampleRowWidget::clicked, this, [selectById](int id) { selectById(id, false); });
        connect(row, &SampleRowWidget::doubleClicked, this, [selectById](int id) { selectById(id, true); });
    }
    updateControls();
    emit samplesChanged();
}

void EditorPanel::selectSample(int index)
{
    if (!m_pad || m_pad->soundPlayer().player.empty()) {
        return;
    }
    const auto& players = m_pad->soundPlayer().player;
    index = std::clamp(index, 0, static_cast<int>(players.size()) - 1);
    m_config->activeSampleIdx = index;
    m_config->activeSampleId = players[static_cast<size_t>(index)]->id;
    updateControls();
}

void EditorPanel::updateControls()
{
    if (!m_pad || m_pad->soundPlayer().player.empty()) {
        return;
    }
    m_config->activeSampleIdx = std::clamp(m_config->activeSampleIdx, 0, static_cast<int>(m_pad->soundPlayer().player.size()) - 1);
    AudioSample* sample = m_pad->soundPlayer().player.at(static_cast<size_t>(m_config->activeSampleIdx));
    m_updating = true;
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
    m_randomPan->setChecked(m_pad->soundPlayer().isRandomPan());
    m_loop->setChecked(sample->loopRegion().loop);
    m_title->setText(m_pad->soundName().toUpper());
    m_updating = false;
}

void EditorPanel::updatePanControl(AudioSample* sample)
{
    const bool pannable = sample->audioPlayer->canPan();
    m_pan->setVisible(pannable);
    m_panValue->setVisible(pannable);
    m_panLabel->setVisible(pannable);
    m_randomPan->setVisible(pannable);
    const bool wasUpdating = m_updating;
    m_updating = true;
    m_pan->setValue(static_cast<int>(((sample->getPan() + 1.0f) / 2.0f) * 1000.0f));
    m_updating = wasUpdating;
}

void EditorPanel::syncLoop(AudioSample* sample)
{
    if (!sample || sample != selectedSample()) {
        return;
    }
    const bool wasUpdating = m_updating;
    m_updating = true;
    m_loop->setChecked(sample->loopRegion().loop);
    m_updating = wasUpdating;
}

void EditorPanel::syncPan(int sampleIndex)
{
    if (sampleIndex == m_config->activeSampleIdx) {
        if (AudioSample* sample = selectedSample()) {
            updatePanControl(sample);
        }
    }
}

void EditorPanel::moveSample(int fromIndex, int insertIndex)
{
    if (!m_pad) {
        return;
    }
    const int dest = m_pad->moveSample(fromIndex, insertIndex);
    if (dest < 0) {
        return;
    }
    m_config->activeSampleIdx = dest;
    if (dest < static_cast<int>(m_pad->soundPlayer().player.size())) {
        m_config->activeSampleId = m_pad->soundPlayer().player[static_cast<size_t>(dest)]->id;
    }
    rebuildRows();
}

void EditorPanel::addFiles(const QStringList& paths)
{
    m_pad->loadFiles(paths, false);
    rebuildRows();
}

void EditorPanel::tick()
{
    if (!m_pad) {
        return;
    }
    const auto& players = m_pad->soundPlayer().player;
    for (int i = 0; i < m_rows.size() && i < static_cast<int>(players.size()); ++i) {
        m_rows[i]->setProgress(players[static_cast<size_t>(i)]->audioPlayer->getPosition());
        m_rows[i]->setSelected(i == m_config->activeSampleIdx);
    }
    // The selected sample changed (arrow keys, or the pad moving on while it plays): keep its
    // row in view. After this tick, once a rebuilt list has been laid out.
    const int idx = m_config->activeSampleIdx;
    if ((m_pad != m_shownPad || idx != m_shownIdx) && idx >= 0 && idx < m_rows.size()) {
        m_shownPad = m_pad;
        m_shownIdx = idx;
        const QPointer<QWidget> row(m_rows[idx]);
        QTimer::singleShot(0, this, [this, row]() {
            if (row) {
                m_scroll->ensureWidgetVisible(row, 0, 6);
            }
        });
    }
}
