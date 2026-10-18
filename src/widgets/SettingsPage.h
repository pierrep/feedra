#pragma once

// The Settings page: startup and autosave, grid defaults, the sample library and waveform
// cache, and the four reverbs. Edits AppConfig and AudioDevice directly.

#include <QWidget>

#include "AudioDevice.h"

class AppConfig;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QSpinBox;

class SettingsPage : public QWidget
{
    Q_OBJECT
public:
    SettingsPage(AppConfig& config, QWidget* parent = nullptr);

    // Shows what the config and the audio device hold now.
    void sync();
    void refreshWaveformCacheInfo();

signals:
    void backRequested();
    void sceneLimitChanged();
    // A reverb preset or impulse changed: the pad tab's send names follow them.
    void reverbChanged();

private:
    void applyImpulsePath(const QString& path, int which);

    AppConfig& m_config;
    bool m_updating = false;
    QCheckBox* m_loadLastSettings = nullptr;
    QCheckBox* m_autosave = nullptr;
    QSpinBox* m_sceneLimit = nullptr;
    QSpinBox* m_gridColumns = nullptr;
    QSpinBox* m_gridRows = nullptr;
    QLineEdit* m_libraryPath = nullptr;
    QLabel* m_waveformCacheInfo = nullptr;
    QComboBox* m_reverbPreset[AudioDevice::kEaxReverbCount] = {};
    QSlider* m_convolutionGain[AudioDevice::kConvolutionCount] = {};
    QLabel* m_convolutionGainValue[AudioDevice::kConvolutionCount] = {};
    QLineEdit* m_impulsePath[AudioDevice::kConvolutionCount] = {};
    QPushButton* m_impulseBrowse[AudioDevice::kConvolutionCount] = {};
};
