#pragma once

// What MIDI controls do: notes and CCs to pads, volumes and the live actions. No MIDI library
// here (MidiInput delivers the messages) and no widgets (MainWindow carries the commands out),
// so it can be tested on its own.
//
// Pads are by position in the selected scene, so one controller plays whatever scene is up.
// Out of the box, notes from 36 play the pads in order (36 is pad 1), on any channel: most pad
// controllers start their pads there. Anything can be learned instead: start learning an
// action, and the next suitable note or CC becomes its control.

#include <QJsonObject>
#include <QString>

#include <cstdint>
#include <map>
#include <vector>

struct MidiMessage {
    uint8_t status = 0;
    uint8_t data1 = 0;
    uint8_t data2 = 0;

    int type() const { return status & 0xF0; }
    int channel() const { return status & 0x0F; }
    bool isNoteOn() const { return type() == 0x90 && data2 > 0; }
    bool isNoteOff() const { return type() == 0x80 || (type() == 0x90 && data2 == 0); }
    bool isControlChange() const { return type() == 0xB0; }
};

enum class ControlAction {
    PadPlay,       // play / pause a pad (a button)
    PadVolume,     // a pad's volume (a fader or knob)
    MasterVolume,  // the main volume (a fader or knob)
    Intensity,     // the selected scene's Intensity slider (a fader or knob)
    NextScene,
    PreviousScene,
    GoToScene,     // crossfade to the selected scene
    Panic,
    TalkOver,      // held
};

struct ControlBinding {
    ControlAction action = ControlAction::PadPlay;
    int pad = -1;       // for the pad actions
    bool cc = false;    // a CC, or a note
    int channel = 0;    // 0-15
    int number = 0;     // note or CC number
};

struct ControlCommand {
    ControlAction action = ControlAction::PadPlay;
    int pad = -1;
    float value = 0.0f; // 0..1, for the volumes
    bool pressed = true; // for the buttons: pressed, or let go (only TalkOver uses let go)
};

class ControlMap
{
public:
    static constexpr int kDefaultFirstNote = 36;
    static constexpr int kDefaultNoteCount = 64;

    // Whether an action takes a fader (volumes) or a button (the rest).
    static bool isContinuous(ControlAction action);
    static QString actionName(ControlAction action, int pad = -1);
    // "Note 36 · ch 1", "CC 7 · ch 2".
    static QString describe(const ControlBinding& binding);

    // Notes from kDefaultFirstNote play pads in order, unless that note was learned for
    // something else.
    bool defaultNotes() const { return m_defaultNotes; }
    void setDefaultNotes(bool on) { m_defaultNotes = on; }

    const std::vector<ControlBinding>& bindings() const { return m_bindings; }
    const ControlBinding* bindingFor(ControlAction action, int pad = -1) const;
    void clear(ControlAction action, int pad = -1);
    void clearAll();

    // The next note or CC (a CC only, for a volume) becomes this action's control. It replaces
    // the action's old control, and the control's old action.
    void startLearn(ControlAction action, int pad = -1);
    void cancelLearn() { m_learning = false; }
    bool isLearning() const { return m_learning; }
    ControlAction learningAction() const { return m_learnAction; }
    int learningPad() const { return m_learnPad; }

    // What a message asks for. While learning, a suitable message is learned instead (nothing
    // is returned, and `learned` is set).
    std::vector<ControlCommand> handle(const MidiMessage& message, bool* learned = nullptr);

    QJsonObject toJson() const;
    void fromJson(const QJsonObject& json);

private:
    bool sameControl(const ControlBinding& binding, bool cc, int channel, int number) const;
    void learn(bool cc, int channel, int number);

    std::vector<ControlBinding> m_bindings;
    bool m_defaultNotes = true;
    bool m_learning = false;
    ControlAction m_learnAction = ControlAction::PadPlay;
    int m_learnPad = -1;
    // The last value of each CC (channel << 8 | number), so a CC button presses on the way up.
    std::map<int, int> m_ccValues;
};
