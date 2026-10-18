#pragma once

// The Settings page: startup and autosave, grid defaults and the crossfade time, the sample
// library and waveform cache, the output and preview devices, and the four reverbs. Edits AppConfig and AudioDevice directly.

#include <QWidget>

#include "AudioDevice.h"
#include "ControlMap.h"
#include "SettingsCards.h"

class AppConfig;
class MidiInput;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
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
    // Lists the output devices again (they come and go). When the page is shown.
    void refreshDevices();
    // A line under the output pickers: what the window made of the choice.
    void setDeviceStatus(const QString& text);
    // The MIDI rows edit this map and show this input's ports. Set once, before it's shown.
    void setMidi(MidiInput* input, ControlMap* map);
    // Ports, learned controls and the chosen input, as they are now.
    void refreshMidi();
    void setMidiStatus(const QString& text);

signals:
    void backRequested();
    void sceneLimitChanged();
    // A reverb preset or impulse changed: the pad tab's send names follow them.
    void reverbChanged();
    // AppConfig::outputDevice or previewDevice was changed here: the window applies it.
    void outputDeviceChanged();
    void keyboardPadsChanged();
    void midiInputChosen(const QString& port);
    void midiLearnRequested(ControlAction action);
    // Controls were cleared, or default notes switched: save.
    void midiMapChanged();
    void previewDeviceChanged();

private:
    void applyImpulsePath(const QString& path, int which);
    void selectDevice(QComboBox* box, const QString& name);

    AppConfig& m_config;
    bool m_updating = false;
    QCheckBox* m_loadLastSettings = nullptr;
    QCheckBox* m_autosave = nullptr;
    QSpinBox* m_sceneLimit = nullptr;
    QSpinBox* m_gridColumns = nullptr;
    QSpinBox* m_gridRows = nullptr;
    QDoubleSpinBox* m_crossfade = nullptr;
    SettingsCard m_liveCard; // keyboard and MIDI rows go in here too
    QDoubleSpinBox* m_talkOverDepth = nullptr;
    QDoubleSpinBox* m_panicTime = nullptr;
    QCheckBox* m_keyboardPads = nullptr;
    MidiInput* m_midi = nullptr;
    ControlMap* m_controls = nullptr;
    QComboBox* m_midiInput = nullptr;
    QCheckBox* m_midiDefaultNotes = nullptr;
    QLabel* m_midiBindings[6] = {};
    QLabel* m_midiPads = nullptr;
    QLabel* m_midiStatus = nullptr;
    QLineEdit* m_libraryPath = nullptr;
    QLabel* m_waveformCacheInfo = nullptr;
    QComboBox* m_outputDevice = nullptr;
    QComboBox* m_previewDevice = nullptr;
    QLabel* m_deviceStatus = nullptr;
    QComboBox* m_reverbPreset[AudioDevice::kEaxReverbCount] = {};
    QSlider* m_convolutionGain[AudioDevice::kConvolutionCount] = {};
    QLabel* m_convolutionGainValue[AudioDevice::kConvolutionCount] = {};
    QLineEdit* m_impulsePath[AudioDevice::kConvolutionCount] = {};
    QPushButton* m_impulseBrowse[AudioDevice::kConvolutionCount] = {};
};
