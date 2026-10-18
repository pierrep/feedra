#pragma once

// One MIDI input port, through RtMidi (ALSA, CoreMIDI or WinMM). Messages arrive on RtMidi's
// thread and are handed to the UI thread, where received() is emitted. Sysex, clock and active
// sensing are ignored.
//
// Built without a MIDI backend (FEEDRA_WITH_MIDI off, or no ALSA headers on Linux), it lists
// no ports and available() is false; the rest of Feedra works the same.

#include "ControlMap.h"

#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

class MidiInput : public QObject
{
    Q_OBJECT
public:
    explicit MidiInput(QObject* parent = nullptr);
    ~MidiInput() override;

    static bool available();

    // The input ports now. Asks the system, so call it when a list is shown.
    QStringList ports() const;
    // Opens a port by name, closing any other. False if there's no such port or it won't open.
    bool open(const QString& name);
    void close();
    bool isOpen() const;
    QString portName() const { return m_portName; }

    // Emits received() as if it came from the port. RtMidi's callback ends up here (on the UI
    // thread), and so can a test.
    void deliver(const MidiMessage& message);

signals:
    void received(const MidiMessage& message);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    QString m_portName;
};
