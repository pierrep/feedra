#include "SettingsPage.h"

#include "AppConfig.h"
#include "AudioDevice.h"
#include "FsPath.h"
#include "PeakStore.h"
#include "SettingsCards.h"
#include "VolumeDb.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
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
        tr("How new scenes and pads start out, where samples live, and the reverb."), &back));
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

void SettingsPage::sync()
{
    m_updating = true;
    m_loadLastSettings->setChecked(m_config.loadLastSettings);
    m_autosave->setChecked(m_config.autosave);
    m_sceneLimit->setValue(static_cast<int>(m_config.maxScenes));
    m_gridColumns->setValue(m_config.gridWidth);
    m_gridRows->setValue(m_config.gridHeight);
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

