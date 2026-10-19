#include "ControlMap.h"

#include <QCoreApplication>
#include <QJsonArray>

#include <algorithm>

namespace {

const char* actionId(ControlAction action)
{
    switch (action) {
    case ControlAction::PadPlay: return "padplay";
    case ControlAction::PadVolume: return "padvolume";
    case ControlAction::MasterVolume: return "mastervolume";
    case ControlAction::Intensity: return "intensity";
    case ControlAction::NextScene: return "nextscene";
    case ControlAction::PreviousScene: return "previousscene";
    case ControlAction::GoToScene: return "gotoscene";
    case ControlAction::Panic: return "panic";
    case ControlAction::TalkOver: return "talkover";
    }
    return "";
}

bool actionFromId(const QString& id, ControlAction& out)
{
    const ControlAction all[] = { ControlAction::PadPlay, ControlAction::PadVolume, ControlAction::MasterVolume,
        ControlAction::Intensity, ControlAction::NextScene, ControlAction::PreviousScene, ControlAction::GoToScene,
        ControlAction::Panic, ControlAction::TalkOver };
    for (ControlAction action : all) {
        if (id == QLatin1String(actionId(action))) {
            out = action;
            return true;
        }
    }
    return false;
}

bool usesPad(ControlAction action)
{
    return action == ControlAction::PadPlay || action == ControlAction::PadVolume;
}

} // namespace

bool ControlMap::isContinuous(ControlAction action)
{
    return action == ControlAction::PadVolume || action == ControlAction::MasterVolume
        || action == ControlAction::Intensity;
}

QString ControlMap::actionName(ControlAction action, int pad)
{
    const auto tr = [](const char* text) { return QCoreApplication::translate("ControlMap", text); };
    switch (action) {
    case ControlAction::PadPlay: return tr("Pad %1 play").arg(pad + 1);
    case ControlAction::PadVolume: return tr("Pad %1 volume").arg(pad + 1);
    case ControlAction::MasterVolume: return tr("Main volume");
    case ControlAction::Intensity: return tr("Scene intensity");
    case ControlAction::NextScene: return tr("Next scene");
    case ControlAction::PreviousScene: return tr("Previous scene");
    case ControlAction::GoToScene: return tr("Go to selected scene");
    case ControlAction::Panic: return tr("Panic");
    case ControlAction::TalkOver: return tr("Talk-over");
    }
    return QString();
}

QString ControlMap::describe(const ControlBinding& binding)
{
    return QCoreApplication::translate("ControlMap", "%1 %2 · ch %3")
        .arg(binding.cc ? QStringLiteral("CC") : QCoreApplication::translate("ControlMap", "Note"))
        .arg(binding.number)
        .arg(binding.channel + 1);
}

const ControlBinding* ControlMap::bindingFor(ControlAction action, int pad) const
{
    for (const ControlBinding& binding : m_bindings) {
        if (binding.action == action && (!usesPad(action) || binding.pad == pad)) {
            return &binding;
        }
    }
    return nullptr;
}

void ControlMap::clear(ControlAction action, int pad)
{
    m_bindings.erase(std::remove_if(m_bindings.begin(), m_bindings.end(), [&](const ControlBinding& b) {
        return b.action == action && (!usesPad(action) || b.pad == pad);
    }), m_bindings.end());
}

void ControlMap::clearAll()
{
    m_bindings.clear();
    m_learning = false;
}

void ControlMap::startLearn(ControlAction action, int pad)
{
    m_learning = true;
    m_learnAction = action;
    m_learnPad = usesPad(action) ? pad : -1;
}

bool ControlMap::sameControl(const ControlBinding& binding, bool cc, int channel, int number) const
{
    return binding.cc == cc && binding.channel == channel && binding.number == number;
}

void ControlMap::learn(bool cc, int channel, int number)
{
    // One control does one thing, and one thing has one control.
    clear(m_learnAction, m_learnPad);
    m_bindings.erase(std::remove_if(m_bindings.begin(), m_bindings.end(), [&](const ControlBinding& b) {
        return sameControl(b, cc, channel, number);
    }), m_bindings.end());
    ControlBinding binding;
    binding.action = m_learnAction;
    binding.pad = m_learnPad;
    binding.cc = cc;
    binding.channel = channel;
    binding.number = number;
    m_bindings.push_back(binding);
    m_learning = false;
}

std::vector<ControlCommand> ControlMap::handle(const MidiMessage& message, bool* learned)
{
    if (learned) {
        *learned = false;
    }
    std::vector<ControlCommand> commands;
    const int channel = message.channel();
    const int number = message.data1;

    bool cc = false;
    bool press = false;   // a button going down
    bool release = false; // and coming up
    float value = 0.0f;
    if (message.isControlChange()) {
        cc = true;
        const int key = channel << 8 | number;
        const auto last = m_ccValues.find(key);
        const int before = last == m_ccValues.end() ? 0 : last->second;
        m_ccValues[key] = message.data2;
        press = before < 64 && message.data2 >= 64;
        release = before >= 64 && message.data2 < 64;
        value = float(message.data2) / 127.0f;
    } else if (message.isNoteOn()) {
        press = true;
    } else if (message.isNoteOff()) {
        release = true;
    } else {
        return commands;
    }

    if (m_learning) {
        // A fader can't be learned from a note, and a button is learned as it goes down.
        const bool suitable = isContinuous(m_learnAction) ? cc : press;
        if (suitable) {
            learn(cc, channel, number);
            if (learned) {
                *learned = true;
            }
        }
        return commands;
    }

    bool bound = false;
    for (const ControlBinding& binding : m_bindings) {
        if (!sameControl(binding, cc, channel, number)) {
            continue;
        }
        bound = true;
        ControlCommand command;
        command.action = binding.action;
        command.pad = binding.pad;
        if (isContinuous(binding.action)) {
            if (!cc) {
                continue;
            }
            command.value = value;
            commands.push_back(command);
        } else if (press || (release && binding.action == ControlAction::TalkOver)) {
            command.pressed = press;
            commands.push_back(command);
        }
    }
    if (!bound && !cc && m_defaultNotes && press && number >= kDefaultFirstNote
        && number < kDefaultFirstNote + kDefaultNoteCount) {
        ControlCommand command;
        command.action = ControlAction::PadPlay;
        command.pad = number - kDefaultFirstNote;
        commands.push_back(command);
    }
    return commands;
}

QJsonObject ControlMap::toJson() const
{
    QJsonArray bindings;
    for (const ControlBinding& binding : m_bindings) {
        QJsonObject b{
            {QStringLiteral("action"), QLatin1String(actionId(binding.action))},
            {QStringLiteral("type"), binding.cc ? QStringLiteral("cc") : QStringLiteral("note")},
            {QStringLiteral("channel"), binding.channel + 1},
            {QStringLiteral("number"), binding.number},
        };
        if (usesPad(binding.action)) {
            b.insert(QStringLiteral("pad"), binding.pad + 1);
        }
        bindings.append(b);
    }
    return QJsonObject{
        {QStringLiteral("defaultnotes"), m_defaultNotes},
        {QStringLiteral("bindings"), bindings},
    };
}

void ControlMap::fromJson(const QJsonObject& json)
{
    m_bindings.clear();
    m_learning = false;
    m_defaultNotes = json.value(QStringLiteral("defaultnotes")).toBool(true);
    for (const QJsonValue& value : json.value(QStringLiteral("bindings")).toArray()) {
        const QJsonObject b = value.toObject();
        ControlBinding binding;
        if (!actionFromId(b.value(QStringLiteral("action")).toString(), binding.action)) {
            continue; // from a newer Feedra
        }
        binding.cc = b.value(QStringLiteral("type")).toString() == QLatin1String("cc");
        binding.channel = std::clamp(b.value(QStringLiteral("channel")).toInt(1) - 1, 0, 15);
        binding.number = std::clamp(b.value(QStringLiteral("number")).toInt(0), 0, 127);
        binding.pad = usesPad(binding.action) ? std::max(0, b.value(QStringLiteral("pad")).toInt(1) - 1) : -1;
        if (isContinuous(binding.action) && !binding.cc) {
            continue;
        }
        m_bindings.push_back(binding);
    }
}
