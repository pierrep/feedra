#include "SettingsPage.h"

#include "AppConfig.h"
#include "AudioDevice.h"
#include "AudioLatency.h"
#include "CueOutput.h"
#include "MidiInput.h"
#include "FsPath.h"
#include "PeakStore.h"
#include "SettingsCards.h"
#include "VolumeDb.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

SettingsPage::SettingsPage(AppConfig& config, QWidget* parent)
    : QWidget(parent)
    , m_config(config)
{
    setObjectName(QStringLiteral("SettingsPage"));
    setAttribute(Qt::WA_StyledBackground, true);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(32, 24, 20, 0);
    outer->setSpacing(20);
    QPushButton* back = nullptr;
    outer->addWidget(makePageHeader(this, tr("Settings"),
        tr("How new scenes and pads start out, where samples live, the audio output and the reverb."), &back));
    connect(back, &QPushButton::clicked, this, &SettingsPage::backRequested);
    QVBoxLayout* cards = makeCardColumn(this, outer);
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

    m_crossfade = new QDoubleSpinBox(scenes.frame);
    m_crossfade->setRange(0.0, 30.0);
    m_crossfade->setDecimals(1);
    m_crossfade->setSingleStep(0.5);
    m_crossfade->setSuffix(tr(" s"));
    m_crossfade->setMaximumWidth(120);
    scenes.addRow(tr("Crossfade"), m_crossfade,
        tr("Going to a scene (its Go button, or Enter in the scene list) fades the playing scenes "
           "out over this time while it fades in. 0 cuts straight across."));
    connect(m_crossfade, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (!m_updating) {
            m_config.crossfadeSeconds = value;
        }
    });
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

    // Live controls
    SettingsCard live = makeCard(host, tr("Live controls"),
        tr("For running a session: talking over the scenes, and stopping everything."));
    m_talkOverDepth = new QDoubleSpinBox(live.frame);
    m_talkOverDepth->setRange(-40.0, -1.0);
    m_talkOverDepth->setDecimals(0);
    m_talkOverDepth->setSingleStep(1.0);
    m_talkOverDepth->setSuffix(tr(" dB"));
    m_talkOverDepth->setMaximumWidth(120);
    live.addRow(tr("Talk-over"), m_talkOverDepth,
        tr("Hold Space, or click Talk in the header, and every scene dips by this much so you can be "
           "heard. File previews aren't dipped."));
    m_talkFadeIn = new QDoubleSpinBox(live.frame);
    m_talkFadeOut = new QDoubleSpinBox(live.frame);
    m_talkFadeIn->setObjectName(QStringLiteral("TalkFadeIn"));
    m_talkFadeOut->setObjectName(QStringLiteral("TalkFadeOut"));
    for (QDoubleSpinBox* box : {m_talkFadeIn, m_talkFadeOut}) {
        box->setRange(0.0, 5.0);
        box->setDecimals(2);
        box->setSingleStep(0.05);
        box->setSuffix(tr(" s"));
        box->setMaximumWidth(120);
    }
    live.addRow(tr("Talk fade in"), m_talkFadeIn,
        tr("How long the scenes take to dip to the talk-over level once you hold Space. 0 is instant."));
    live.addRow(tr("Talk fade out"), m_talkFadeOut,
        tr("How long they take to come back up after you let go. 0 is instant."));
    m_panicTime = new QDoubleSpinBox(live.frame);
    m_panicTime->setRange(0.0, 10.0);
    m_panicTime->setDecimals(1);
    m_panicTime->setSingleStep(0.5);
    m_panicTime->setSuffix(tr(" s"));
    m_panicTime->setMaximumWidth(120);
    live.addRow(tr("Panic"), m_panicTime,
        tr("Esc, or Panic in the header, fades everything out over this time. Press it again to "
           "stop at once."));
    m_keyboardPads = new QCheckBox(tr("Play pads from the keyboard"), live.frame);
    live.addRow(tr("Keyboard"), m_keyboardPads,
        tr("The pad grid's rows are the keyboard's: 1-0, Q-P, A-; and Z-/, left to right. Hold Tab "
           "to see each pad's key. Enter goes to the selected scene; arrows move through the scene list."));
    connect(m_keyboardPads, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_updating) {
            m_config.keyboardPads = on;
            emit keyboardPadsChanged();
        }
    });
    // MIDI
    m_midiInput = new QComboBox(live.frame);
    m_midiInput->setMaximumWidth(420);
    m_midiInput->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_midiInput->setMinimumContentsLength(24);
    auto* midiRefresh = new QPushButton(tr("Refresh"), live.frame);
    midiRefresh->setToolTip(tr("List the MIDI inputs again, after plugging one in"));
    auto* midiRow = new QWidget(live.frame);
    auto* midiLayout = new QHBoxLayout(midiRow);
    midiLayout->setContentsMargins(0, 0, 0, 0);
    midiLayout->setSpacing(8);
    midiLayout->addWidget(m_midiInput, 1);
    midiLayout->addWidget(midiRefresh);
    live.addRow(tr("MIDI input"), midiRow,
        MidiInput::available()
            ? tr("A pad controller, keyboard or fader box. Right-click a pad, or the main volume, to "
                 "learn a control for it. Pads follow the selected scene.")
            : tr("This build of Feedra has no MIDI (it was built without ALSA)."));
    m_midiDefaultNotes = new QCheckBox(tr("Notes from 36 play the pads in order"), live.frame);
    live.addRow(QString(), m_midiDefaultNotes,
        tr("Note 36 is pad 1, 37 pad 2, and so on, on any channel: where most pad controllers start. "
           "A learned control comes first."));

    auto* actions = new QWidget(live.frame);
    auto* actionGrid = new QGridLayout(actions);
    actionGrid->setContentsMargins(0, 0, 0, 0);
    actionGrid->setHorizontalSpacing(10);
    actionGrid->setVerticalSpacing(6);
    const ControlAction globalActions[kGlobalMidiActions] = { ControlAction::MasterVolume, ControlAction::Intensity,
        ControlAction::NextScene, ControlAction::PreviousScene, ControlAction::GoToScene, ControlAction::Panic,
        ControlAction::TalkOver };
    for (int row = 0; row < kGlobalMidiActions; ++row) {
        const ControlAction action = globalActions[row];
        actionGrid->addWidget(new QLabel(ControlMap::actionName(action), actions), row, 0);
        m_midiBindings[row] = new QLabel(actions);
        m_midiBindings[row]->setObjectName(QStringLiteral("FieldHint"));
        actionGrid->addWidget(m_midiBindings[row], row, 1);
        auto* learn = new QPushButton(tr("Learn"), actions);
        auto* clear = new QPushButton(tr("Clear"), actions);
        learn->setEnabled(MidiInput::available());
        actionGrid->addWidget(learn, row, 2);
        actionGrid->addWidget(clear, row, 3);
        connect(learn, &QPushButton::clicked, this, [this, action]() { emit midiLearnRequested(action); });
        connect(clear, &QPushButton::clicked, this, [this, action]() {
            if (m_controls) {
                m_controls->clear(action);
                refreshMidi();
                emit midiMapChanged();
            }
        });
    }
    actionGrid->setColumnStretch(1, 1);
    live.addRow(tr("MIDI controls"), actions);

    auto* padsRow = new QWidget(live.frame);
    auto* padsLayout = new QHBoxLayout(padsRow);
    padsLayout->setContentsMargins(0, 0, 0, 0);
    m_midiPads = new QLabel(padsRow);
    m_midiPads->setWordWrap(true);
    auto* clearAll = new QPushButton(tr("Clear all MIDI"), padsRow);
    padsLayout->addWidget(m_midiPads, 1);
    padsLayout->addWidget(clearAll);
    live.addRow(tr("Pads"), padsRow);
    m_midiStatus = new QLabel(live.frame);
    m_midiStatus->setObjectName(QStringLiteral("FieldHint"));
    m_midiStatus->setWordWrap(true);
    m_midiStatus->hide();
    live.addFullRow(m_midiStatus);

    connect(midiRefresh, &QPushButton::clicked, this, &SettingsPage::refreshMidi);
    connect(m_midiInput, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (!m_updating && index >= 0) {
            emit midiInputChosen(m_midiInput->itemData(index).toString());
        }
    });
    connect(m_midiDefaultNotes, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_updating && m_controls) {
            m_controls->setDefaultNotes(on);
            emit midiMapChanged();
        }
    });
    connect(clearAll, &QPushButton::clicked, this, [this]() {
        if (m_controls) {
            m_controls->clearAll();
            refreshMidi();
            emit midiMapChanged();
        }
    });
    m_liveCard = live;
    cards->addWidget(live.frame);
    connect(m_talkOverDepth, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (!m_updating) {
            m_config.talkOverDepthDb = value;
        }
    });
    connect(m_talkFadeIn, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (!m_updating) {
            m_config.talkOverFadeInSeconds = value;
        }
    });
    connect(m_talkFadeOut, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (!m_updating) {
            m_config.talkOverFadeOutSeconds = value;
        }
    });
    connect(m_panicTime, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (!m_updating) {
            m_config.panicSeconds = value;
        }
    });

    // Output
    SettingsCard output = makeCard(host, tr("Audio output"),
        tr("Where the scenes play, and where the Files tab's previews go."));
    m_outputDevice = new QComboBox(output.frame);
    m_outputDevice->setMaximumWidth(420);
    m_outputDevice->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_outputDevice->setMinimumContentsLength(24);
    auto* refresh = new QPushButton(tr("Refresh"), output.frame);
    refresh->setToolTip(tr("List the devices again, after plugging one in"));
    auto* outputRow = new QWidget(output.frame);
    auto* outputLayout = new QHBoxLayout(outputRow);
    outputLayout->setContentsMargins(0, 0, 0, 0);
    outputLayout->setSpacing(8);
    outputLayout->addWidget(m_outputDevice, 1);
    outputLayout->addWidget(refresh);
    output.addRow(tr("Output"), outputRow,
        tr("System default follows the system's output as it changes. A chosen device is kept; "
           "if it's unplugged, sound moves to the default until it's back."));
    m_previewDevice = new QComboBox(output.frame);
    m_previewDevice->setMaximumWidth(420);
    m_previewDevice->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_previewDevice->setMinimumContentsLength(24);
    output.addRow(tr("Previews"), m_previewDevice,
        CueOutput::available()
            ? tr("Headphones here keep file previews to yourself: the table only hears the scenes.")
            : tr("Previews can't use a second device with this OpenAL library."));
    m_previewDevice->setEnabled(CueOutput::available());
    m_outputLatency = new QComboBox(output.frame);
    m_outputLatency->setObjectName(QStringLiteral("OutputLatency"));
    m_outputLatency->setMaximumWidth(260);
    for (int preset = 0; preset < AudioLatency::kPresetCount; ++preset) {
        m_outputLatency->addItem(AudioLatency::label(preset), AudioLatency::id(preset));
    }
    const double refreshHz = AudioDevice::outputRefreshHz();
    output.addRow(tr("Latency"), m_outputLatency,
        tr("The delay between a press and the sound: smaller is snappier, but too small crackles on a "
           "slow computer or with many pads and reverbs. Takes effect when Feedra next starts.%1")
            .arg(refreshHz > 0.0 ? tr(" Now: %1 ms per buffer.").arg(1000.0 / refreshHz, 0, 'f', 1) : QString()));
    m_deviceStatus = new QLabel(output.frame);
    m_deviceStatus->setObjectName(QStringLiteral("FieldHint"));
    m_deviceStatus->setWordWrap(true);
    m_deviceStatus->hide();
    output.addFullRow(m_deviceStatus);
    cards->addWidget(output.frame);
    connect(refresh, &QPushButton::clicked, this, &SettingsPage::refreshDevices);
    connect(m_outputDevice, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (m_updating || index < 0) {
            return;
        }
        m_config.outputDevice = m_outputDevice->itemData(index).toString();
        emit outputDeviceChanged();
    });
    connect(m_outputLatency, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (m_updating || index < 0) {
            return;
        }
        m_config.outputLatency = m_outputLatency->itemData(index).toString();
        emit outputLatencyChanged();
    });
    connect(m_previewDevice, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (m_updating || index < 0) {
            return;
        }
        m_config.previewDevice = m_previewDevice->itemData(index).toString();
        emit previewDeviceChanged();
    });
    refreshDevices();

    // Reverb
    SettingsCard reverb = makeCard(host, tr("Reverb"),
        tr("Each pad has a send into each of these four reverbs, on its Pad tab."));
    for (int which = 0; which < AudioDevice::kEaxReverbCount; ++which) {
        m_reverbPreset[which] = new QComboBox(reverb.frame);
        m_reverbPreset[which]->setMaxVisibleItems(24);
        m_reverbPreset[which]->setMaximumWidth(320);
        for (int i = 0; i < AudioDevice::reverbPresetCount(); ++i) {
            m_reverbPreset[which]->addItem(
                QString::fromStdString(AudioDevice::reverbPresetLabel(i)),
                QString::fromStdString(AudioDevice::reverbPresetId(i)));
        }
        reverb.addRow(tr("EAX reverb %1 preset").arg(which + 1), m_reverbPreset[which],
            tr("Used by each pad's EAX reverb %1 send, which is named after the preset.").arg(which + 1));
    }

    const bool convolution = AudioDevice::convolutionAvailable();
    for (int which = 0; which < AudioDevice::kConvolutionCount; ++which) {
        m_convolutionGain[which] = new QSlider(Qt::Horizontal, reverb.frame);
        m_convolutionGain[which]->setRange(0, VolumeDb::sliderSpan(VolumeDb::kFloorDb, VolumeDb::kUnityDb));
        m_convolutionGain[which]->setToolTip(tr("Output level of this convolution reverb. The bundled impulse is loud, so the default is low."));
        m_convolutionGainValue[which] = new QLabel(reverb.frame);
        m_convolutionGainValue[which]->setObjectName(QStringLiteral("MainVolumeValue"));
        m_convolutionGainValue[which]->setMinimumWidth(72);
        m_convolutionGainValue[which]->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_convolutionGain[which]->setValue(VolumeDb::toSliderClamped(
            AudioDevice::convolutionGain(which), VolumeDb::kFloorDb, VolumeDb::kUnityDb));
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
        if (m_updating) {
            return;
        }
        m_config.loadLastSettings = on;
    });
    connect(m_autosave, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updating) {
            return;
        }
        m_config.autosave = on;
    });
    connect(m_sceneLimit, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
        if (m_updating) {
            return;
        }
        m_config.maxScenes = static_cast<unsigned int>(value);
        emit sceneLimitChanged();
    });
    connect(m_gridColumns, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
        if (!m_updating) {
            m_config.gridWidth = value;
        }
    });
    connect(m_gridRows, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
        if (!m_updating) {
            m_config.gridHeight = value;
        }
    });
    connect(m_libraryPath, &QLineEdit::editingFinished, this, [this]() {
        if (m_updating) {
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
    for (int which = 0; which < AudioDevice::kEaxReverbCount; ++which) {
        connect(m_reverbPreset[which], qOverload<int>(&QComboBox::currentIndexChanged), this, [this, which](int index) {
            if (!m_updating) {
                AudioDevice::setReverbPreset(index, which);
                emit reverbChanged();
            }
        });
    }
    for (int which = 0; which < AudioDevice::kConvolutionCount; ++which) {
        connect(m_convolutionGain[which], &QSlider::valueChanged, this, [this, which](int value) {
            const float db = VolumeDb::fromSlider(value, VolumeDb::kFloorDb);
            m_convolutionGainValue[which]->setText(VolumeDb::format(db));
            if (!m_updating) {
                AudioDevice::setConvolutionGain(VolumeDb::toLinearMuted(db), which);
            }
        });
        connect(m_impulsePath[which], &QLineEdit::editingFinished, this, [this, which]() {
            if (m_updating) {
                return;
            }
            applyImpulsePath(m_impulsePath[which]->text().trimmed(), which);
        });
        connect(m_impulseBrowse[which], &QPushButton::clicked, this, [this, which]() {
            const QString start = m_impulsePath[which]->text().isEmpty()
                ? m_config.defaultImpulsePath()
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

void SettingsPage::applyImpulsePath(const QString& path, int which)
{
    const QString chosen = path.isEmpty() ? m_config.defaultImpulsePath() : path;
    if (FsPath::fromQString(chosen) == AudioDevice::convolutionImpulsePath(which)) {
        m_impulsePath[which]->setText(chosen);
        return;
    }
    if (!AudioDevice::setConvolutionImpulse(FsPath::fromQString(chosen), which)) {
        QMessageBox::warning(this, tr("Feedra"),
            tr("Could not load the impulse response:\n%1").arg(chosen));
    }
    m_impulsePath[which]->setText(FsPath::toQString(AudioDevice::convolutionImpulsePath(which)));
    emit reverbChanged();
}

void SettingsPage::refreshWaveformCacheInfo()
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

void SettingsPage::refreshDevices()
{
    const bool wasUpdating = m_updating;
    m_updating = true;
    const std::vector<std::string> devices = AudioDevice::outputDevices();
    const auto fill = [&devices](QComboBox* box, const QString& none) {
        box->clear();
        box->addItem(none, QString());
        for (const std::string& name : devices) {
            const QString text = QString::fromStdString(name);
            box->addItem(text, text);
        }
    };
    fill(m_outputDevice, tr("System default"));
    fill(m_previewDevice, tr("Same as the output"));
    selectDevice(m_outputDevice, m_config.outputDevice);
    selectDevice(m_previewDevice, m_config.previewDevice);
    m_updating = wasUpdating;
}

// A device that isn't plugged in stays chosen, shown as missing.
void SettingsPage::selectDevice(QComboBox* box, const QString& name)
{
    int index = box->findData(name);
    if (index < 0) {
        box->addItem(tr("%1 (not connected)").arg(name), name);
        index = box->count() - 1;
    }
    box->setCurrentIndex(index);
}

void SettingsPage::setMidi(MidiInput* input, ControlMap* map)
{
    m_midi = input;
    m_controls = map;
    refreshMidi();
}

void SettingsPage::refreshMidi()
{
    if (!m_midi || !m_controls) {
        return;
    }
    const bool wasUpdating = m_updating;
    m_updating = true;
    const QString chosen = m_midi->isOpen() ? m_midi->portName() : m_midiInput->currentData().toString();
    m_midiInput->clear();
    m_midiInput->addItem(tr("None"), QString());
    for (const QString& port : m_midi->ports()) {
        m_midiInput->addItem(port, port);
    }
    selectDevice(m_midiInput, chosen);
    m_midiInput->setEnabled(MidiInput::available());
    m_midiDefaultNotes->setChecked(m_controls->defaultNotes());
    const ControlAction globalActions[kGlobalMidiActions] = { ControlAction::MasterVolume, ControlAction::Intensity,
        ControlAction::NextScene, ControlAction::PreviousScene, ControlAction::GoToScene, ControlAction::Panic,
        ControlAction::TalkOver };
    for (int row = 0; row < kGlobalMidiActions; ++row) {
        const ControlBinding* binding = m_controls->bindingFor(globalActions[row]);
        m_midiBindings[row]->setText(binding ? ControlMap::describe(*binding) : tr("—"));
    }
    int pads = 0;
    for (const ControlBinding& binding : m_controls->bindings()) {
        pads += (binding.action == ControlAction::PadPlay || binding.action == ControlAction::PadVolume) ? 1 : 0;
    }
    m_midiPads->setText(pads == 0 ? tr("No pad controls learned. Right-click a pad to learn one.")
                                  : tr("%n pad control(s) learned. Right-click a pad to learn or forget.", nullptr, pads));
    m_updating = wasUpdating;
}

void SettingsPage::setMidiStatus(const QString& text)
{
    m_midiStatus->setText(text);
    m_midiStatus->setVisible(!text.isEmpty());
}

void SettingsPage::setDeviceStatus(const QString& text)
{
    m_deviceStatus->setText(text);
    m_deviceStatus->setVisible(!text.isEmpty());
}

void SettingsPage::sync()
{
    m_updating = true;
    selectDevice(m_outputDevice, m_config.outputDevice);
    selectDevice(m_previewDevice, m_config.previewDevice);
    m_outputLatency->setCurrentIndex(std::max(0, m_outputLatency->findData(m_config.outputLatency)));
    m_loadLastSettings->setChecked(m_config.loadLastSettings);
    m_autosave->setChecked(m_config.autosave);
    m_sceneLimit->setValue(static_cast<int>(m_config.maxScenes));
    m_gridColumns->setValue(m_config.gridWidth);
    m_gridRows->setValue(m_config.gridHeight);
    m_crossfade->setValue(m_config.crossfadeSeconds);
    m_talkOverDepth->setValue(m_config.talkOverDepthDb);
    m_talkFadeIn->setValue(m_config.talkOverFadeInSeconds);
    m_talkFadeOut->setValue(m_config.talkOverFadeOutSeconds);
    m_panicTime->setValue(m_config.panicSeconds);
    m_keyboardPads->setChecked(m_config.keyboardPads);
    m_libraryPath->setText(m_config.defaultLibraryLocation);
    for (int which = 0; which < AudioDevice::kEaxReverbCount; ++which) {
        m_reverbPreset[which]->setCurrentIndex(AudioDevice::reverbPresetIndex(which));
    }
    for (int which = 0; which < AudioDevice::kConvolutionCount; ++which) {
        m_convolutionGain[which]->setValue(VolumeDb::toSliderClamped(
            AudioDevice::convolutionGain(which), VolumeDb::kFloorDb, VolumeDb::kUnityDb));
        m_convolutionGainValue[which]->setText(VolumeDb::format(
            VolumeDb::fromSlider(m_convolutionGain[which]->value(), VolumeDb::kFloorDb)));
        m_impulsePath[which]->setText(FsPath::toQString(AudioDevice::convolutionImpulsePath(which)));
    }
    m_updating = false;
}

