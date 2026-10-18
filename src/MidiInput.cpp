#include "MidiInput.h"

#include <QDebug>
#include <QMetaObject>
#include <QPointer>

#ifdef FEEDRA_WITH_MIDI
#include "RtMidi.h"
#endif

struct MidiInput::Impl {
#ifdef FEEDRA_WITH_MIDI
    std::unique_ptr<RtMidiIn> in;

    RtMidiIn* get()
    {
        if (!in) {
            try {
                in = std::make_unique<RtMidiIn>(RtMidi::UNSPECIFIED, "Feedra");
                in->ignoreTypes(true, true, true); // sysex, timing, active sensing
            } catch (const RtMidiError& e) {
                qWarning() << "MIDI unavailable:" << QString::fromStdString(e.getMessage());
            }
        }
        return in.get();
    }
#endif
};

MidiInput::MidiInput(QObject* parent)
    : QObject(parent)
    , m_impl(std::make_unique<Impl>())
{
}

MidiInput::~MidiInput()
{
    close();
}

bool MidiInput::available()
{
#ifdef FEEDRA_WITH_MIDI
    return true;
#else
    return false;
#endif
}

QStringList MidiInput::ports() const
{
    QStringList names;
#ifdef FEEDRA_WITH_MIDI
    if (RtMidiIn* in = m_impl->get()) {
        try {
            const unsigned count = in->getPortCount();
            for (unsigned i = 0; i < count; ++i) {
                names << QString::fromStdString(in->getPortName(i));
            }
        } catch (const RtMidiError& e) {
            qWarning() << "MIDI ports:" << QString::fromStdString(e.getMessage());
        }
    }
#endif
    return names;
}

bool MidiInput::open(const QString& name)
{
    close();
    if (name.isEmpty()) {
        return false;
    }
#ifdef FEEDRA_WITH_MIDI
    RtMidiIn* in = m_impl->get();
    if (!in) {
        return false;
    }
    const int index = ports().indexOf(name);
    if (index < 0) {
        return false;
    }
    try {
        in->openPort(static_cast<unsigned>(index), "Feedra in");
        // On RtMidi's thread: copy the message and hand it to the UI thread. The queued call
        // is dropped if this object has gone by then.
        in->setCallback([](double, std::vector<unsigned char>* bytes, void* user) {
            if (!bytes || bytes->empty()) {
                return;
            }
            MidiMessage message;
            message.status = (*bytes)[0];
            message.data1 = bytes->size() > 1 ? (*bytes)[1] : 0;
            message.data2 = bytes->size() > 2 ? (*bytes)[2] : 0;
            auto* self = static_cast<MidiInput*>(user);
            QMetaObject::invokeMethod(self, [self, message]() { self->deliver(message); }, Qt::QueuedConnection);
        }, this);
    } catch (const RtMidiError& e) {
        qWarning() << "Couldn't open MIDI input" << name << QString::fromStdString(e.getMessage());
        return false;
    }
    m_portName = name;
    qInfo() << "MIDI input" << name;
    return true;
#else
    return false;
#endif
}

void MidiInput::close()
{
#ifdef FEEDRA_WITH_MIDI
    if (m_impl->in && m_impl->in->isPortOpen()) {
        // After this returns RtMidi calls back no more.
        m_impl->in->cancelCallback();
        m_impl->in->closePort();
    }
#endif
    m_portName.clear();
}

bool MidiInput::isOpen() const
{
    return !m_portName.isEmpty();
}

void MidiInput::deliver(const MidiMessage& message)
{
    emit received(message);
}
