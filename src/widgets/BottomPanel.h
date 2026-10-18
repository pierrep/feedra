#pragma once

// The panel under the pad grid: the Waveform tab (playhead, seek, loop points), the Pad tab
// (delay, repeat, scene pad, effect sends) and the Info tab (the current sample's format).
//
// Collapsing changes the window's height, so the panel only asks (collapseRequested) and the
// window does it, calling setCollapsedState() in the middle.

#include "RegionRenderer.h"

#include <QPointer>
#include <QWidget>

class AppConfig;
class AudioSample;
class Pad;
class WaveformWidget;
class QCheckBox;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;
class QStackedWidget;

class BottomPanel : public QWidget
{
    Q_OBJECT
public:
    enum Tab { InfoTab = 0, PadTab = 1, WaveformTab = 2 };

    explicit BottomPanel(AppConfig* config, QWidget* parent = nullptr);

    void setPad(Pad* pad);
    Pad* pad() const { return m_pad; }

    // The Pad tab's controls, from the pad (or disabled without a loaded one).
    void refreshPadControls();
    // The send names follow the reverbs: "<preset> reverb send", "<impulse file> reverb send".
    void refreshSendLabels();
    // The scene pad box, after the pad switched it itself (Ctrl+click on the pad).
    void syncScenePad();
    // Info, the delay readout and the waveform. Once per UI tick while the pads show.
    void tick();
    void refreshWaveform();

    int currentTab() const;
    // Shows a tab. Doesn't uncollapse: the window does that (it resizes).
    void setCurrentTab(int tab);
    bool isCollapsed() const { return m_collapsed; }
    void setCollapsedState(bool collapsed);
    // The height of the pages, which the window grows and shrinks by when collapsing.
    int pageHeight() const { return m_pageHeight; }

signals:
    void collapseRequested(bool collapsed);
    // A sample's loop region changed on the Waveform tab.
    void loopRegionChanged(AudioSample* sample);

private:
    // The sample the Waveform tab shows: the one selected in the Editor (which follows the
    // playing sample while the pad plays), or the pad's current one.
    int waveformSampleIndex() const;
    AudioSample* waveformSample() const;
    void applyLoopRegion(AudioSample* sample, const LoopRegion& region);
    void refreshSampleInfo();
    void refreshDelayReadout();
    void tabClicked(int tab);
    void refreshTabButtons();

    AppConfig* m_config = nullptr;
    QPointer<Pad> m_pad;
    bool m_updating = false;
    bool m_collapsed = false;
    int m_pageHeight = 0;

    QStackedWidget* m_stack = nullptr;
    QPushButton* m_tabs[3] = {};
    QPushButton* m_collapse = nullptr;

    QLabel* m_info = nullptr;
    Pad* m_infoPad = nullptr; // the pad and sample the Info tab shows, so it's only redone on change
    int m_infoSound = -1;

    QSpinBox* m_minDelay = nullptr;
    QSpinBox* m_maxDelay = nullptr;
    // Effect sends: EAX reverb 1 and 2, then convolution reverb 1 and 2.
    QSlider* m_sendSliders[4] = {};
    QLabel* m_sendLabels[4] = {};
    QCheckBox* m_randomPlayback = nullptr;
    QCheckBox* m_repeat = nullptr;
    QCheckBox* m_delayOn = nullptr;
    QPushButton* m_resetDelay = nullptr;
    QCheckBox* m_scenePad = nullptr;
    QLabel* m_delayReadout = nullptr;

    WaveformWidget* m_waveform = nullptr;
    int m_loopDragSampleId = -1; // the sample whose loop handles are being dragged
};
