#pragma once

// The sidebar's Editor page: the pad's sample list (reorder, drop files, add) and the selected
// sample's controls (pan, width, pitch, gain, random pan, loop).
//
// The selected sample is AppConfig::activeSampleIdx / activeSampleId, which the Waveform tab
// and the window's arrow keys share.

#include <QPointer>
#include <QVector>
#include <QWidget>

class AppConfig;
class AudioSample;
class Pad;
class SampleRowWidget;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QScrollArea;
class QSlider;
class QVBoxLayout;

class EditorPanel : public QWidget
{
    Q_OBJECT
public:
    explicit EditorPanel(AppConfig* config, QWidget* parent = nullptr);

    // The pad to edit. A different pad drops the old pad's rows; refresh() builds the new ones.
    void setPad(Pad* pad);
    Pad* pad() const { return m_pad; }

    // Rebuilds the page for the pad: its rows and controls, or just its title when it's empty.
    void refresh();
    // Rebuilds the rows, keeping the selection.
    void rebuildRows();
    // The selected sample's controls, from the sample.
    void updateControls();
    // Selects a sample by index (clamped) and shows its controls.
    void selectSample(int index);
    // The loop box, for when the selected sample's loop was changed elsewhere (Waveform tab).
    void syncLoop(AudioSample* sample);
    // The pan control, after the player picked a random pan for this sample.
    void syncPan(int sampleIndex);
    // Playback progress on the rows and the selected row kept in view. Call while shown.
    void tick();

signals:
    // Samples were added, removed or moved: the pad's other views need refreshing.
    void samplesChanged();
    // A sample's loop region changed from here.
    void loopRegionChanged(AudioSample* sample);

private:
    AudioSample* selectedSample() const;
    void updatePanControl(AudioSample* sample);
    void moveSample(int fromIndex, int insertIndex);
    void addFiles(const QStringList& paths);

    AppConfig* m_config = nullptr;
    QPointer<Pad> m_pad;
    bool m_updating = false;

    QLabel* m_title = nullptr;
    QPushButton* m_add = nullptr;
    QScrollArea* m_scroll = nullptr;
    QVBoxLayout* m_listLayout = nullptr;
    QVector<SampleRowWidget*> m_rows;
    Pad* m_shownPad = nullptr; // the pad and sample last scrolled into view
    int m_shownIdx = -1;

    QWidget* m_controls = nullptr;
    QSlider* m_pan = nullptr;
    QDoubleSpinBox* m_panValue = nullptr;
    QLabel* m_panLabel = nullptr;
    QSlider* m_pitch = nullptr;
    QDoubleSpinBox* m_pitchValue = nullptr;
    QSlider* m_gain = nullptr;
    QDoubleSpinBox* m_gainValue = nullptr;
    QCheckBox* m_randomPan = nullptr;
    QSlider* m_width = nullptr;
    QDoubleSpinBox* m_widthValue = nullptr;
    QLabel* m_widthLabel = nullptr;
    QCheckBox* m_loop = nullptr;
};
