// The live controls' logic on its own: the talk-over duck's ramps and holders, and which key
// plays which pad.

#include "ControlMap.h"
#include "Duck.h"
#include "KeyLayout.h"

#include <QJsonArray>
#include <QString>

#include <cmath>
#include <cstdio>

namespace {

int g_failures = 0;
#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (!(cond)) {                                                \
            ++g_failures;                                             \
            std::printf("  FAIL %s:%d: ", __FILE__, __LINE__);        \
            std::printf(__VA_ARGS__);                                 \
            std::printf("\n");                                        \
        }                                                             \
    } while (0)

bool near(float a, float b, float tol = 0.05f)
{
    return std::fabs(a - b) <= tol;
}

void testDuck()
{
    std::printf("duck\n");
    Duck duck;
    CHECK(duck.update(0) == 1.0f && !duck.isActive(), "starts up");
    duck.setHeld(Duck::Key, true);
    duck.update(Duck::kAttackMs / 2);
    CHECK(near(duck.levelDb(), Duck::kDefaultDepthDb / 2), "half way down after half the attack: %f", duck.levelDb());
    duck.update(Duck::kAttackMs + 10);
    CHECK(near(duck.levelDb(), Duck::kDefaultDepthDb, 1e-4f), "at depth: %f", duck.levelDb());
    CHECK(near(duck.gain(), std::pow(10.0f, -12.0f / 20.0f), 1e-4f), "gain %f", duck.gain());

    // Two holders: it stays down until both let go.
    duck.setHeld(Duck::Button, true);
    duck.setHeld(Duck::Key, false);
    duck.update(1000);
    CHECK(near(duck.levelDb(), Duck::kDefaultDepthDb, 1e-4f), "came up with the button still held");
    CHECK(duck.isHeldBy(Duck::Button) && !duck.isHeldBy(Duck::Key), "holders");
    duck.setHeld(Duck::Button, false);
    duck.update(1000 + Duck::kReleaseMs / 2);
    CHECK(near(duck.levelDb(), Duck::kDefaultDepthDb / 2), "half way up after half the release: %f", duck.levelDb());
    duck.update(1000 + Duck::kReleaseMs + 10);
    CHECK(duck.levelDb() == 0.0f && duck.gain() == 1.0f && !duck.isActive(), "back up");

    // A long gap between ticks doesn't overshoot.
    duck.setHeld(Duck::Midi, true);
    duck.update(100000);
    CHECK(near(duck.levelDb(), Duck::kDefaultDepthDb, 1e-4f), "overshot: %f", duck.levelDb());
    // A shallower depth while down: comes up to it.
    duck.setDepthDb(-6.0f);
    duck.update(100000 + Duck::kReleaseMs);
    CHECK(near(duck.levelDb(), -6.0f, 1e-4f), "new depth %f", duck.levelDb());
    // Depth 0 turns it off at once.
    duck.setDepthDb(0.0f);
    duck.update(100001);
    CHECK(duck.gain() == 1.0f, "depth 0 still ducks");
    // Time going backwards (it shouldn't) does nothing odd.
    duck.setDepthDb(-12.0f);
    duck.update(50);
    CHECK(duck.levelDb() <= 0.0f && duck.levelDb() >= -12.0f, "backwards time: %f", duck.levelDb());
}

void testKeys()
{
    std::printf("keys\n");
    // The default 6x4 grid.
    CHECK(KeyLayout::padForKey('1', 6, 4) == 0, "1");
    CHECK(KeyLayout::padForKey('6', 6, 4) == 5, "6");
    CHECK(KeyLayout::padForKey('7', 6, 4) == -1, "7 is off the grid");
    CHECK(KeyLayout::padForKey('Q', 6, 4) == 6, "Q");
    CHECK(KeyLayout::padForKey('W', 6, 4) == 7, "W");
    CHECK(KeyLayout::padForKey('A', 6, 4) == 12, "A");
    CHECK(KeyLayout::padForKey('N', 6, 4) == 23, "N");
    CHECK(KeyLayout::padForKey('M', 6, 4) == -1, "M is off the grid");
    // Wide and short grids.
    CHECK(KeyLayout::padForKey('0', 12, 2) == 9, "0 in a 12-wide grid");
    CHECK(KeyLayout::padForKey('P', 12, 2) == 21, "P in a 12-wide grid");
    CHECK(KeyLayout::padForKey('A', 12, 2) == -1, "A in a 2-row grid");
    CHECK(KeyLayout::padForKey('/', 10, 8) == 39, "/");
    CHECK(KeyLayout::padForKey(';', 10, 4) == 29, ";");
    CHECK(KeyLayout::padForKey(' ', 6, 4) == -1 && KeyLayout::padForKey(0x01000000, 6, 4) == -1, "Space and Esc play nothing");
    // Labels match.
    for (int i = 0; i < 24; ++i) {
        const QString label = KeyLayout::keyLabel(i, 6);
        CHECK(label.size() == 1 && KeyLayout::padForKey(label.at(0).unicode(), 6, 4) == i, "label for %d", i);
    }
    CHECK(KeyLayout::keyLabel(40, 10).isEmpty() && KeyLayout::keyLabel(11, 12).isEmpty() && KeyLayout::keyLabel(-1, 6).isEmpty(),
          "pads without keys");
}

MidiMessage noteOn(int note, int velocity = 100, int channel = 0)
{
    return MidiMessage{ static_cast<uint8_t>(0x90 | channel), static_cast<uint8_t>(note), static_cast<uint8_t>(velocity) };
}

MidiMessage noteOff(int note, int channel = 0)
{
    return MidiMessage{ static_cast<uint8_t>(0x80 | channel), static_cast<uint8_t>(note), 0 };
}

MidiMessage cc(int number, int value, int channel = 0)
{
    return MidiMessage{ static_cast<uint8_t>(0xB0 | channel), static_cast<uint8_t>(number), static_cast<uint8_t>(value) };
}

bool one(const std::vector<ControlCommand>& commands, ControlAction action, int pad = -1)
{
    return commands.size() == 1 && commands[0].action == action && (pad < 0 || commands[0].pad == pad);
}

void testControls()
{
    std::printf("midi map\n");
    ControlMap map;
    // Out of the box: notes from 36 play pads in order, on any channel.
    CHECK(one(map.handle(noteOn(36)), ControlAction::PadPlay, 0), "note 36");
    CHECK(one(map.handle(noteOn(43, 90, 9)), ControlAction::PadPlay, 7), "note 43 on channel 10");
    CHECK(map.handle(noteOff(36)).empty(), "note off plays nothing");
    CHECK(map.handle(noteOn(36, 0)).empty(), "velocity 0 is a note off");
    CHECK(map.handle(noteOn(35)).empty(), "note 35");
    CHECK(map.handle(cc(1, 127)).empty(), "an unlearned CC");
    CHECK(map.handle(MidiMessage{ 0xE0, 0, 64 }).empty(), "pitch bend");
    map.setDefaultNotes(false);
    CHECK(map.handle(noteOn(36)).empty(), "default notes off");
    map.setDefaultNotes(true);

    // Learning a pad's play: the next note.
    bool learned = false;
    map.startLearn(ControlAction::PadPlay, 3);
    CHECK(map.isLearning(), "not learning");
    CHECK(map.handle(noteOn(50), &learned).empty() && learned && !map.isLearning(), "didn't learn note 50");
    CHECK(one(map.handle(noteOn(50)), ControlAction::PadPlay, 3), "note 50 plays pad 4");
    const ControlBinding* b = map.bindingFor(ControlAction::PadPlay, 3);
    CHECK(b && !b->cc && b->number == 50 && b->channel == 0, "binding");
    CHECK(b && ControlMap::describe(*b) == QStringLiteral("Note 50 · ch 1"), "describe: %s", b ? qPrintable(ControlMap::describe(*b)) : "");

    // A learned note takes over from the default layout.
    map.startLearn(ControlAction::Panic);
    map.handle(noteOn(36), &learned);
    CHECK(learned && one(map.handle(noteOn(36)), ControlAction::Panic), "note 36 should be Panic now");

    // A volume learns only from a CC: a note while learning is ignored.
    map.startLearn(ControlAction::PadVolume, 0);
    map.handle(noteOn(70), &learned);
    CHECK(!learned && map.isLearning(), "a volume learned a note");
    map.handle(cc(20, 33), &learned);
    CHECK(learned, "a volume didn't learn a CC");
    std::vector<ControlCommand> commands = map.handle(cc(20, 127));
    CHECK(one(commands, ControlAction::PadVolume, 0) && commands[0].value == 1.0f, "CC 20 at 127");
    commands = map.handle(cc(20, 0));
    CHECK(one(commands, ControlAction::PadVolume, 0) && commands[0].value == 0.0f, "CC 20 at 0");
    CHECK(map.handle(cc(20, 64, 1)).empty(), "the same CC on another channel");

    // A control does one thing: learning it for something else drops the old use.
    map.startLearn(ControlAction::MasterVolume);
    map.handle(cc(20, 10), &learned);
    CHECK(learned && !map.bindingFor(ControlAction::PadVolume, 0) && map.bindingFor(ControlAction::MasterVolume), "CC 20 moved");
    // And a thing has one control: learning again replaces it.
    map.startLearn(ControlAction::MasterVolume);
    map.handle(cc(7, 10), &learned);
    CHECK(map.bindingFor(ControlAction::MasterVolume)->number == 7 && map.handle(cc(20, 10)).empty(), "master moved to CC 7");

    // A CC as a button: presses going up through 64, lets go going down.
    map.startLearn(ControlAction::TalkOver);
    map.handle(cc(64, 127), &learned);
    CHECK(learned, "talk-over didn't learn CC 64");
    map.handle(cc(64, 0));
    commands = map.handle(cc(64, 127));
    CHECK(one(commands, ControlAction::TalkOver) && commands[0].pressed, "CC 64 press");
    CHECK(map.handle(cc(64, 120)).empty(), "staying up isn't another press");
    commands = map.handle(cc(64, 0));
    CHECK(one(commands, ControlAction::TalkOver) && !commands[0].pressed, "CC 64 let go");
    // A note as a held button.
    map.startLearn(ControlAction::TalkOver);
    map.handle(noteOn(80), &learned);
    commands = map.handle(noteOff(80));
    CHECK(one(commands, ControlAction::TalkOver) && !commands[0].pressed, "note off lets go");
    CHECK(map.handle(noteOff(50)).empty(), "a pad's note off does nothing");

    // Cancel.
    map.startLearn(ControlAction::NextScene);
    map.cancelLearn();
    CHECK(one(map.handle(noteOn(50)), ControlAction::PadPlay, 3), "cancel learned anyway");

    // Saved and read back.
    map.setDefaultNotes(false);
    ControlMap copy;
    copy.fromJson(map.toJson());
    CHECK(copy.toJson() == map.toJson() && !copy.defaultNotes() && copy.bindings().size() == map.bindings().size(), "JSON round trip");
    QJsonObject junk = map.toJson();
    QJsonArray list = junk.value("bindings").toArray();
    list.append(QJsonObject{{"action", "fromthefuture"}, {"type", "note"}, {"number", 1}});
    list.append(QJsonObject{{"action", "mastervolume"}, {"type", "note"}, {"number", 2}}); // a fader on a note
    junk.insert("bindings", list);
    copy.fromJson(junk);
    CHECK(copy.bindings().size() == map.bindings().size(), "junk entries kept: %d", int(copy.bindings().size()));

    map.clear(ControlAction::PadPlay, 3);
    CHECK(!map.bindingFor(ControlAction::PadPlay, 3), "clear");
    map.clearAll();
    CHECK(map.bindings().empty(), "clearAll");
}

} // namespace

int main()
{
    testDuck();
    testKeys();
    testControls();
    std::printf("%s: %d failure(s)\n", g_failures ? "FAILED" : "OK", g_failures);
    return g_failures ? 1 : 0;
}
