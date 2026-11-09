#!/usr/bin/env python3
"""Tests for mohreplay.py. A fake game (fakegame.py) stands in for openmohaa,
so they run anywhere, without game files or a screen:

    python3 replay/tests/test_mohreplay.py [-v] [name of a test]

With MOHREPLAY_TEST_GAME (the folder with main/Pak0.pk3) and
MOHREPLAY_TEST_DEMOS (a folder of real demos) set, the RealGame tests also
play and record a real demo with the game built in .cmake/RelWithDebInfo (or
MOHREPLAY_TEST_EXE), without a window; MOHREPLAY_TEST_DEMO picks the demo.
Without PySide6, it exits with 77, which ctest takes as skipped.
"""

import csv
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
import zipfile
from unittest import mock

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
# nothing here may reach the settings and indexes of whoever runs the tests
TEST_HOME = tempfile.TemporaryDirectory(prefix="mohreplay-test-home-")
os.environ["XDG_CONFIG_HOME"] = os.path.join(TEST_HOME.name, "config")
os.environ["XDG_CACHE_HOME"] = os.path.join(TEST_HOME.name, "cache")
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

try:
    from PySide6.QtCore import QEvent, QPointF, QSettings, Qt
    from PySide6.QtGui import QMouseEvent
    from PySide6.QtWidgets import QApplication, QDialog, QMessageBox, QStatusBar, QToolBar
except ImportError:
    if __name__ == "__main__":
        print("Skipped: PySide6 isn't installed")
        sys.exit(77)
    raise unittest.SkipTest("PySide6 isn't installed")

import mohreplay  # noqa: E402

app = QApplication.instance() or QApplication(sys.argv[:1])
FAKEGAME = os.path.join(HERE, "fakegame.py")

DEMO = {
    "duration": 600000,
    "truncated": False,
    "recorder": {"client": 0, "name": "t-"},
    "maps": [{"time": 0, "map": "obj/obj_team1"}],
    "watched": [{"time": 0, "client": 0, "name": "t-", "followed": False},
                {"time": 300000, "client": 1, "name": "^1Phil", "followed": True}],
    "kills": [
        {"time": 10000, "killer": 0, "killerName": "t-", "victim": 1, "victimName": "^1Phil",
         "text": "Phil was rifled by t-", "weapon": "rifle"},
        {"time": 12000, "killer": 0, "killerName": "t-", "victim": 2, "victimName": "Bob",
         "text": "Bob was rifled by t-", "weapon": "rifle"},
        {"time": 14500, "killer": 0, "killerName": "t-", "victim": 3, "victimName": "Al",
         "text": "Al was shot by t-", "weapon": "other"},
        {"time": 200000, "killer": 1, "killerName": "^1Phil", "victim": 0, "victimName": "t-",
         "text": "t- was machine-gunned by Phil", "weapon": "mg"},
        {"time": 250000, "killer": -1, "killerName": "", "victim": 2, "victimName": "Bob",
         "text": "Bob took himself out of commision", "weapon": "other"},
        {"time": 400000, "killer": 0, "killerName": "t-", "victim": 1, "victimName": "^1Phil",
         "text": "Phil was rifled by t-", "weapon": "rifle"},
    ],
    "rounds": [{"time": 195000}, {"time": 420000}],
    "roundEnds": [{"time": 191000, "text": "Axis win!"}, {"time": 416000, "text": "Allies win!"}],
}


def followed_demo():
    """DEMO where Phil, followed from 5:00, kills twice in 2.5 s, a second
    and 3.5 seconds after he's shown."""
    demo = json.loads(json.dumps(DEMO))
    demo["kills"][5:5] = [
        {"time": 301000, "killer": 1, "killerName": "^1Phil", "victim": 0, "victimName": "t-",
         "text": "t- was rifled by Phil", "weapon": "rifle"},
        {"time": 303500, "killer": 1, "killerName": "^1Phil", "victim": 3, "victimName": "Al",
         "text": "Al was sniped by Phil", "weapon": "sniper"},
    ]
    return demo


def team_demo():
    """DEMO with three team kills: t-'s 1.5 s after their third kill and
    alone at 1:40, and Phil's 1.5 s after their kill at 3:20."""
    demo = json.loads(json.dumps(DEMO))

    def team_kill(time, killer, name):
        return {"time": time, "killer": killer, "killerName": name, "victim": 4, "victimName": "Joe",
                "text": "Joe was rifled by " + name, "teamKill": True, "weapon": "rifle"}
    demo["kills"] += [team_kill(16000, 0, "t-"), team_kill(100000, 0, "t-"), team_kill(201500, 1, "^1Phil")]
    demo["kills"].sort(key=lambda kill: kill["time"])
    return demo


def grenade_demo():
    """DEMO with two grenade kills by t-: 2.5 s after their third kill and
    alone at 0:50, the second as an index made before kills were marked."""
    demo = json.loads(json.dumps(DEMO))
    demo["kills"] += [
        {"time": 17000, "killer": 0, "killerName": "t-", "victim": 4, "victimName": "Joe",
         "text": "Joe tripped on t-'s grenade", "grenade": True, "weapon": "grenade"},
        {"time": 50000, "killer": 0, "killerName": "t-", "victim": 4, "victimName": "Joe",
         "text": "Joe is picking t-'s shrapnel out of his teeth"},
    ]
    demo["kills"].sort(key=lambda kill: kill["time"])
    return demo


def wait_until(condition, timeout=10.0):
    """Runs Qt's events until condition() is true, False after the timeout."""
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        app.processEvents()
        if condition():
            return True
        time.sleep(0.01)
    app.processEvents()
    return bool(condition())


def unspecced_demo():
    """A demo of its recorder, who follows Ace for a while, and of frags of
    Ace and Cid while not shown: in the demo for long enough or not, their
    victim in view or not."""
    def kill(time, killer, name, **seen):
        return {"time": time, "killer": killer, "killerName": name, "victim": 4, "victimName": "Bob",
                "text": f"Bob was shot by {name}", "weapon": "smg", **seen}
    return {"duration": 100000, "truncated": False, "recorder": {"client": 0, "name": "rec"},
            "maps": [{"time": 0, "map": "obj/obj_team1"}, {"time": 90000, "map": "obj/obj_team2"}],
            "watched": [{"time": 0, "client": 0, "name": "rec", "followed": False},
                        {"time": 30000, "client": 1, "name": "Ace", "followed": True},
                        {"time": 35000, "client": 0, "name": "rec", "followed": False}],
            "kills": [kill(20000, 1, "Ace", seen=5000, idle=0), kill(32000, 1, "Ace", seen=2000, idle=0),
                      kill(40000, 1, "Ace", seen=2000, idle=0), kill(50000, 1, "Ace", seen=9000),
                      kill(60000, 1, "Ace"), kill(70000, 3, "Cid", seen=9000, idle=0),
                      kill(89000, 1, "Ace", seen=3000, idle=0)],
            "rounds": [], "roundEnds": []}



def plan_4s(*args, **kwargs):
    """demoonly_plan with 4 s before each kill, as the tests of what plays
    and joins were written for (the default is 2 s)."""
    return mohreplay.demoonly_plan(*args, kill_before=4000, **kwargs)

class Messages:
    """Takes the place of QMessageBox's static functions, which would wait."""

    def __init__(self):
        self.shown = []

    def __call__(self, parent, title, text, *args, **kwargs):
        self.shown.append((title, text))
        return QMessageBox.Ok


class FakeGameCase(unittest.TestCase):
    """A game folder with openmohaa and mohdemoindex being the fake game, a
    folder of demos and settings of their own."""

    env = {"FAKEGAME_SPEED": "20"}

    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="mohreplay-test-")
        # cleanups run last in, first out: a game a test leaves to its own
        # cleanup still saves its settings in the test's folders
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)
        self.bin = os.path.join(self.tmp, "bin")
        self.game_dir = os.path.join(self.tmp, "game")
        self.demos = os.path.join(self.tmp, "demos")
        self.videos = os.path.join(self.tmp, "videos")
        for folder in (self.bin, self.game_dir, self.demos):
            os.makedirs(folder)
        self.exe = os.path.join(self.bin, "openmohaa")
        os.symlink(FAKEGAME, self.exe)
        os.symlink(FAKEGAME, os.path.join(self.bin, "mohdemoindex"))
        self.add_demo("first", DEMO)
        self.settings = QSettings(os.path.join(self.tmp, "settings.ini"), QSettings.IniFormat)
        for key, value in (("exe", self.exe), ("game", self.game_dir), ("demos", self.demos),
                           ("rec/folder", self.videos)):
            self.settings.setValue(key, value)
        self.messages = Messages()
        patches = [mock.patch.object(QMessageBox, name, self.messages) for name in ("warning", "information")]
        # quitting while videos are recorded
        patches.append(mock.patch.object(QMessageBox, "question", lambda *args, **kwargs: QMessageBox.Yes))
        patches.append(mock.patch.dict(os.environ, {**self.env, "SDL_VIDEODRIVER": "offscreen"}))
        patches.append(mock.patch.dict(os.environ, {"XDG_CACHE_HOME": os.path.join(self.tmp, "cache"),
                                                    "XDG_CONFIG_HOME": os.path.join(self.tmp, "config")}))
        for patch in patches:
            patch.start()
            self.addCleanup(patch.stop)
        self.windows = []

    def tearDown(self):
        for window in self.windows:
            window.close()
        # what's left to do still runs in the test's folders and settings
        app.processEvents()

    def short_demos(self):
        """Short demos are listed too, to record quickly."""
        patch = mock.patch.object(mohreplay, "MIN_LENGTH", 0)
        patch.start()
        self.addCleanup(patch.stop)

    def add_demo(self, name, index):
        with open(os.path.join(self.demos, name + ".dm3"), "w") as f:
            json.dump(index, f)

    def window(self):
        window = mohreplay.Window(self.settings)
        self.windows.append(window)
        return window

    def commands(self, game):
        try:
            with open(os.path.join(game.home, "main", "commands.log")) as f:
                return f.read().splitlines()
        except (OSError, TypeError):
            return []

    def game_state(self, window):
        """What the game playing a demo tells."""
        return window.game.read_json("demostate.json") or {}

    def playing(self, window, name="first"):
        window.play_demo(name)
        self.assertTrue(wait_until(lambda: self.game_state(window).get("demo") == name), "the demo didn't load")

    def choose(self, window, killer=None, victim=None, weapons=None, obj=None, great=None, recorder=None, specced=None,
               unshown=None):
        """The kill filter, as typed and ticked, applied at once."""
        if obj is not None:
            window.obj.setChecked(obj)
        if great is not None:
            window.great.setChecked(great)
        for key, value in (("recorder", recorder), ("specced", specced), ("unshown", unshown)):
            if value is not None:
                window.from_buttons[key].setChecked(value)
        if killer is not None:
            window.killer.setText(killer)
        if victim is not None:
            window.victim.setText(victim)
        if weapons is not None:
            for key, action in window.weapon_actions.items():
                action.setChecked(key in weapons)
        window.apply_kill_filter()

    def tick(self, field, label):
        """Clicks a player in the menu of a names field."""
        item = next(field.list.item(i) for i in range(field.list.count()) if field.list.item(i).text() == label)
        point = QPointF(field.list.visualItemRect(item).center())
        viewport = field.list.viewport()
        for kind, buttons in ((QEvent.MouseButtonPress, Qt.LeftButton), (QEvent.MouseButtonRelease, Qt.NoButton)):
            QApplication.sendEvent(viewport, QMouseEvent(kind, point, viewport.mapToGlobal(point), Qt.LeftButton, buttons,
                                                         Qt.NoModifier))

    def players(self, field):
        """The players in the menu of a names field, and those ticked."""
        items = [field.list.item(i) for i in range(field.list.count())]
        return [i.text() for i in items], [i.text() for i in items if i.checkState() == Qt.Checked]


class TestFunctions(unittest.TestCase):
    def test_clock(self):
        self.assertEqual(mohreplay.clock(0), "0:00")
        self.assertEqual(mohreplay.clock(61999), "1:01")
        self.assertEqual(mohreplay.clock(-5), "0:00")
        self.assertEqual(mohreplay.clock(3600000), "60:00")

    def test_seconds(self):
        self.assertEqual(mohreplay.seconds(12345), "12.345")
        self.assertEqual(mohreplay.seconds(-1), "0.000")

    def test_parse_clock(self):
        self.assertEqual(mohreplay.parse_clock("90"), 90000)
        self.assertEqual(mohreplay.parse_clock("1:30"), 90000)
        self.assertEqual(mohreplay.parse_clock(" 1:30.5 "), 90500)
        self.assertEqual(mohreplay.parse_clock("1:00:00"), 3600000)
        for bad in ("", "x", "1:-2", "1:2:3:4", "1::2"):
            self.assertIsNone(mohreplay.parse_clock(bad), bad)

    def test_same_player(self):
        self.assertTrue(mohreplay.same_player("^1Phil", "phil"))
        self.assertTrue(mohreplay.same_player("T-", "t-"))
        self.assertTrue(mohreplay.same_player("a^", "A^"))
        # as a key bind passes it
        self.assertTrue(mohreplay.same_player("|Fatal-Finns|  Granu ", "|fatal-finns| granu"))
        self.assertFalse(mohreplay.same_player("phil", "phill"))

    def test_macos_bundle(self):
        tmp = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, tmp)
        app = os.path.join(tmp, "openmohaa.app")
        os.makedirs(os.path.join(app, "Contents", "MacOS"))
        exe = os.path.join(app, "Contents", "MacOS", "openmohaa")
        open(exe, "w").close()
        self.assertEqual(mohreplay.game_program(app), exe)
        self.assertEqual(mohreplay.game_program(app + "/"), exe)
        self.assertEqual(mohreplay.game_program(exe), exe)
        # mohdemoindex is built next to the bundle
        self.assertEqual(mohreplay.find_indexer(exe), os.path.join(app, "Contents", "MacOS", "mohdemoindex"))
        open(os.path.join(tmp, "mohdemoindex"), "w").close()
        self.assertEqual(mohreplay.find_indexer(exe), os.path.join(tmp, "mohdemoindex"))
        plain = os.path.join(tmp, "openmohaa")
        self.assertEqual(mohreplay.find_indexer(plain), os.path.join(tmp, "mohdemoindex"))

    def test_quoted(self):
        self.assertEqual(mohreplay.quoted('say "hi"'), "\"say 'hi'\"")

    def test_demoonly_plan(self):
        plan, kf = plan_4s, mohreplay.KillFilter
        self.assertEqual(plan(DEMO), (0, [(0, 600000)]))
        # anyone's kills, wherever the recorder looks, but the deaths of the
        # player shown: t-'s at 3:20, Phil's at 6:40
        self.assertEqual(plan(DEMO, "kills"), (4, [(6000, 16500), (246000, 252000)]))
        # a player's while shown, whoever's is shown
        self.assertEqual(plan(DEMO, "kills", kill_filter=kf(["T-"])), (3, [(6000, 16500)]))
        self.assertEqual(plan(followed_demo(), "kills", kill_filter=kf(recorder=True, specced=True)),
                         (5, [(6000, 16500), (300000, 305500)]))

    def test_demoonly_plan_with_a_kill_filter(self):
        plan, kf = plan_4s, mohreplay.KillFilter
        # anyone's, whoever is shown
        self.assertEqual(plan(DEMO, "kills", kill_filter=kf(weapons=["rifle"])), (2, [(6000, 14000)]))
        self.assertEqual(plan(DEMO, "kills", kill_filter=kf(victims=["BOB"])), (2, [(8000, 14000), (246000, 252000)]))
        # killers by a part of their names, while they're shown
        self.assertEqual(plan(DEMO, "kills", kill_filter=kf(["t"])), (3, [(6000, 16500)]))
        self.assertEqual(plan(DEMO, "kills", kill_filter=kf(["phil", "T-"], weapons=["other"])), (1, [(10500, 16500)]))
        self.assertEqual(plan(followed_demo(), "kills", kill_filter=kf(["t-", "phil"], weapons=["sniper"])),
                         (1, [(300000, 305500)]))
        # the same client under another name
        renamed = json.loads(json.dumps(DEMO))
        renamed["kills"][0]["killerName"] = "t-.fi"
        self.assertEqual(plan(renamed, "kills", kill_filter=kf(["t-.fi"]))[0], 1)

    def test_cut_where_the_player_shown_changes(self):
        demo = json.loads(json.dumps(DEMO))
        # t- kills 0.8 s apart, then 0.5 s on each side of half a second of Phil
        demo["watched"] = [{"time": 0, "client": 0, "name": "t-"}, {"time": 40000, "client": 1, "name": "^1Phil"},
                           {"time": 40500, "client": 0, "name": "t-"}]
        demo["kills"] = [{"time": time, "killer": 0, "killerName": "t-", "victim": 1, "victimName": "^1Phil",
                          "text": "Phil was rifled by t-"} for time in (20000, 26800, 38000, 44000)]
        plan, kf = plan_4s, mohreplay.KillFilter
        cut = [(16000, 28800), (34000, 40000), (40500, 46000)]
        self.assertEqual(plan(demo, "kills", kill_filter=kf(recorder=True, specced=True))[1], cut)
        self.assertEqual(plan(demo, "kills", kill_filter=kf(["t-"]))[1], cut)
        # all the kills, whoever is shown
        self.assertEqual(plan(demo, "kills")[1], [(16000, 28800), (34000, 46000)])

    def test_kill_filter(self):
        kill_filter = mohreplay.KillFilter(["^1Phil ", " ", "T-"], ["bob"], ["mg", "sniper", "grenade"])
        self.assertEqual((kill_filter.names, kill_filter.killers), (["^1Phil", "T-"], ["phil", "t-"]))
        # as Weapon lists them; grenade kills aren't kills
        self.assertEqual(kill_filter.weapons, ["sniper", "mg"])
        self.assertTrue(kill_filter and kill_filter.by_shown())
        self.assertEqual(kill_filter.commands(), ["demofilter clear", 'demofilter killer "^1Phil"', 'demofilter killer "T-"',
                                                  'demofilter victim "bob"', "demofilter weapon sniper mg"])
        self.assertEqual(kill_filter.words(), " by ^1Phil or T- of bob with sniper or StG / BAR")
        self.assertTrue(kill_filter.keeps({"killerName": "^4phil", "victimName": "Bob!", "weapon": "mg"}))
        # of t-, with a rifle
        self.assertFalse(kill_filter.keeps(DEMO["kills"][3]) or kill_filter.keeps(DEMO["kills"][1]))
        self.assertTrue(kill_filter.keeps(dict(DEMO["kills"][1], weapon="sniper"), killer=False))
        nothing = mohreplay.KillFilter()
        self.assertFalse(nothing or nothing.by_shown())
        self.assertEqual(nothing.commands(), ["demofilter clear"])
        self.assertEqual(mohreplay.or_list(["a", "b", "c"]), "a, b or c")

    def test_obj_maps(self):
        """Obj maps only: the frags made in the levels of obj/, any case, as
        the last level loaded before each tells."""
        mixed = json.loads(json.dumps(DEMO))
        mixed["maps"] = [{"time": 0, "map": "dm/mohdm6"}, {"time": 100000, "map": "OBJ/obj_Team4"}]
        mohreplay.kill_levels(mixed)
        self.assertEqual([k["level"] for k in mixed["kills"]], ["dm/mohdm6"] * 3 + ["obj/obj_team4"] * 3)
        self.assertEqual(mohreplay.kill_levels({"maps": [{"time": 5, "map": "dm/x"}], "kills": [{"time": 1}]})["kills"],
                         [{"time": 1, "level": ""}])
        obj = mohreplay.KillFilter(["t-"], obj=True)
        self.assertTrue(mohreplay.KillFilter(obj=True))
        self.assertEqual(obj.commands()[-1], "demofilter level obj/")
        self.assertEqual(obj.words(), " by t- on obj maps")
        # t-'s 3 frags while shown are in the dm level, and then in the obj one
        summary = mohreplay.summarize(mixed)
        self.assertEqual(mohreplay.filter_kills(summary, mohreplay.KillFilter(["t-"]))[0], 3)
        self.assertEqual(mohreplay.filter_kills(summary, obj), (0, {}))
        self.assertEqual(mohreplay.demoonly_plan(mixed, "kills", kill_filter=obj), (0, []))
        mixed = json.loads(json.dumps(DEMO))
        mixed["maps"] = [{"time": 0, "map": "obj/obj_team1"}, {"time": 100000, "map": "dm/mohdm6"}]
        self.assertEqual(mohreplay.filter_kills(mohreplay.summarize(mixed), obj), (3, {"t-": ["t-", 3]}))
        self.assertEqual(mohreplay.demoonly_plan(mixed, "kills", kill_filter=obj)[0], 3)
        self.assertFalse(obj.keeps(dict(mixed["kills"][5], level="dm/mohdm6")))

    def test_great_frags(self):
        """Great frags: not the sniper frags seen scoped for over a
        second while standing still, but those of a streak, nor the frags of
        AFK victims."""
        slow = mohreplay.slow_snipe
        self.assertTrue(slow({"weapon": "sniper", "scope": 1001, "speed": 30}))
        # quick scopes, no-scopes, shots on the move, other weapons, frags not
        # seen from the fragger's view and indexes made before scopes were
        for kill in ({"weapon": "sniper", "scope": 1000, "speed": 0}, {"weapon": "sniper", "scope": 0, "speed": 0},
                     {"weapon": "sniper", "scope": 5000, "speed": 31}, {"weapon": "rifle", "scope": 5000, "speed": 0},
                     {"weapon": "sniper"}):
            self.assertFalse(slow(kill), kill)
        snipes = json.loads(json.dumps(DEMO))
        # 10 s apart, not a streak
        for kill, time, scope, speed in zip(snipes["kills"], (10000, 20000, 30000), (1500, 2500, 400), (0, 10, 0)):
            kill.update(time=time, weapon="sniper", scope=scope, speed=speed)
        great = mohreplay.KillFilter(["t-"], great=True)
        self.assertTrue(mohreplay.KillFilter(great=True))
        self.assertEqual(great.commands()[-1], "demofilter great 1000 30 5000 5000")
        self.assertEqual(great.words(), " by t-, great ones")
        # t-'s 3 frags while shown, the first 2 slow snipes
        summary = mohreplay.summarize(snipes)
        self.assertEqual(mohreplay.filter_kills(summary, mohreplay.KillFilter(["t-"]))[0], 3)
        self.assertEqual(mohreplay.filter_kills(summary, great), (1, {"t-": ["t-", 1]}))
        self.assertEqual(mohreplay.demoonly_plan(snipes, "kills", kill_filter=great)[0], 1)
        self.assertEqual([great.keeps(kill) for kill in snipes["kills"][:3]], [False, False, True])
        # the second slow snipe in a streak with the third frag, at most 5 s
        # before it, and not when that's a team kill or a grenade kill
        for time, change, streak in ((24000, {}, True), (25000, {}, True), (25001, {}, False),
                                     (24000, {"teamKill": True}, False),
                                     (24000, {"weapon": "grenade", "grenade": True}, False)):
            streaks = json.loads(json.dumps(snipes))
            streaks["kills"][2].update(time=time, **change)
            mohreplay.kill_streaks(streaks)
            self.assertEqual(great.keeps(streaks["kills"][1]), streak, (time, change))
            self.assertFalse(great.keeps(streaks["kills"][0]))
        streaks = json.loads(json.dumps(snipes))
        streaks["kills"][2]["time"] = 24000
        self.assertEqual(mohreplay.filter_kills(mohreplay.summarize(streaks), great), (2, {"t-": ["t-", 2]}))
        self.assertEqual(mohreplay.demoonly_plan(streaks, "kills", kill_filter=great)[0], 2)
        # a victim idle for over 5 s: never great, whatever the weapon, and
        # makes no streak
        self.assertTrue(mohreplay.idle_victim({"weapon": "rifle", "idle": 5001}))
        self.assertFalse(mohreplay.idle_victim({"idle": 5000}) or mohreplay.idle_victim({}))
        streaks["kills"][2]["idle"] = 5001
        mohreplay.kill_streaks(streaks)
        self.assertEqual([great.keeps(kill) for kill in streaks["kills"][:3]], [False, False, False])
        self.assertEqual(mohreplay.filter_kills(mohreplay.summarize(streaks), great)[0], 0)
        self.assertEqual(mohreplay.filter_kills(mohreplay.summarize(streaks), mohreplay.KillFilter(["t-"]))[0], 3)
        self.assertEqual(mohreplay.demoonly_plan(streaks, "kills", kill_filter=great)[0], 0)

    def test_from(self):
        """Recorder, Specced and Unspecced, any of them: who the frags are
        from, as the game is told; those not shown are seen from behind their
        fragger, when the demo has them for 3 s before and their victim in
        view."""
        kf = mohreplay.KillFilter
        demo = unspecced_demo()
        demo["kills"].insert(0, {"time": 10000, "killer": 0, "killerName": "rec", "victim": 4, "victimName": "Bob",
                                 "text": "Bob was shot by rec", "weapon": "smg", "seen": 9000, "idle": 0})
        planned = lambda kill_filter: [k["time"] for k, _, _ in mohreplay.planned_kills(demo, kill_filter)[0]]
        summary = mohreplay.summarize(json.loads(json.dumps(demo)))
        for who, times, commands, words in (
                (dict(recorder=True), [10000], ["demofilter recorder"], " from the recorder"),
                (dict(specced=True), [32000], ["demofilter specced"], " from specced"),
                (dict(unshown=True), [20000, 70000, 89000], ["demofilter unshown only"], " from unspecced"),
                (dict(recorder=True, specced=True), [10000, 32000], ["demofilter recorder", "demofilter specced"],
                 " from the recorder and specced"),
                (dict(recorder=True, unshown=True), [10000, 20000, 70000, 89000],
                 ["demofilter recorder", "demofilter unshown"], " from the recorder and unspecced"),
                (dict(specced=True, unshown=True), [20000, 32000, 70000, 89000],
                 ["demofilter specced", "demofilter unshown"], " from specced and unspecced"),
                (dict(recorder=True, specced=True, unshown=True), [10000, 20000, 32000, 70000, 89000],
                 ["demofilter recorder", "demofilter specced", "demofilter unshown"],
                 " from the recorder, specced and unspecced")):
            kill_filter = kf(**who)
            self.assertTrue(kill_filter and kill_filter.by_shown() and kill_filter.who_chosen())
            self.assertEqual(planned(kill_filter), times, who)
            self.assertEqual(kill_filter.commands(), ["demofilter clear", *commands], who)
            self.assertEqual(kill_filter.words(), words)
            # the list of demos counts the same
            self.assertEqual(mohreplay.filter_kills(summary, kill_filter)[0], len(times), who)
        # none: anyone's, as the demo shows them
        self.assertEqual(len(planned(kf())), 8)
        self.assertEqual(mohreplay.filter_kills(summary, kf())[0], 8)
        # with a fragger, theirs of those; alone, theirs while shown
        self.assertEqual(planned(kf(["ace"])), [32000])
        self.assertEqual(planned(kf(["ace"], unshown=True)), [20000, 89000])
        self.assertEqual(planned(kf(["a*e"], specced=True, unshown=True)), [20000, 32000, 89000])
        self.assertEqual(planned(kf(["cid"], unshown=True)), [70000])
        self.assertEqual(planned(kf(["rec"], recorder=True)), [10000])
        self.assertEqual(planned(kf(["rec"], specced=True)), [])
        self.assertEqual(planned(kf(["ace"], recorder=True)), [])
        self.assertEqual(kf(["ace"], weapons=["smg"], specced=True, great=True).words(),
                         " by ace with SMG from specced, great ones")
        self.assertEqual(mohreplay.filter_kills(summary, kf(["ace"], unshown=True)), (2, {"ace": ["Ace", 2]}))
        self.assertEqual(mohreplay.filter_kills(summary, kf(unshown=True)), (3, {"ace": ["Ace", 2], "cid": ["Cid", 1]}))
        # from when they're in the demo at most, and inside the level
        self.assertEqual(plan_4s(demo, "kills", kill_filter=kf(specced=True, unshown=True)),
                         (4, [(16000, 22000), (30000, 34000), (66000, 72000), (86000, 90000)]))
        # indexes from before: not known to be in the demo
        old = json.loads(json.dumps(demo))
        for kill in old["kills"]:
            kill.pop("seen", None)
        self.assertEqual(mohreplay.demoonly_plan(old, "kills", kill_filter=kf(unshown=True))[0], 0)
        self.assertEqual(mohreplay.filter_kills(mohreplay.summarize(old), kf(unshown=True))[0], 0)

    def test_specced_frags(self):
        """Specced frags: only those of the players the recorder follows,
        Phil's two from 5:00 in followed_demo, not t-'s own."""
        demo, kf = followed_demo(), mohreplay.KillFilter
        specced = kf(specced=True)
        self.assertEqual(kf(["phil"], weapons=["sniper"], specced=True).words(), " by phil with sniper from specced")
        mohreplay.kill_specced(demo)
        self.assertEqual([kill["time"] for kill in demo["kills"] if kill["specced"]], [301000, 303500])
        self.assertEqual([specced.keeps(kill) for kill in demo["kills"][5:7]], [True, True])
        # t-'s, shown but not followed; t-'s at 6:40, while Phil is shown
        self.assertFalse(specced.keeps(demo["kills"][0]) or specced.keeps(demo["kills"][7]))
        summary = mohreplay.summarize(demo)
        self.assertEqual(mohreplay.filter_kills(summary, specced), (2, {"phil": ["^1Phil", 2]}))
        self.assertEqual(mohreplay.filter_kills(summary, kf(["t-"], specced=True))[0], 0)
        self.assertEqual(mohreplay.filter_kills(summary, kf(weapons=["sniper"], specced=True))[0], 1)
        self.assertEqual(mohreplay.demoonly_plan(demo, "kills", kill_filter=specced), (2, [(300000, 305500)]))
        self.assertEqual(mohreplay.demoonly_plan(demo, "kills", kill_filter=kf(["t-"], specced=True))[0], 0)
        # an index made before followed was: none
        for watch in demo["watched"]:
            del watch["followed"]
        self.assertEqual(mohreplay.filter_kills(mohreplay.summarize(demo), specced)[0], 0)

    def test_player_models(self):
        """The player models of the game files, in pk3s or on their own,
        but the first-person arms."""
        game = tempfile.mkdtemp(prefix="mohreplay-test-")
        self.addCleanup(shutil.rmtree, game, True)
        os.makedirs(os.path.join(game, "main", "models", "player"))
        with zipfile.ZipFile(os.path.join(game, "main", "pak0.pk3"), "w") as pk3:
            for name in ("models/player/German_Winter_1.tik", "models/player/german_winter_1_fps.tik",
                         "models/player/allied_airborne.tik", "models/weapons/kar98.tik"):
                pk3.writestr(name, "")
        with open(os.path.join(game, "main", "broken.pk3"), "w") as f:
            f.write("not a zip")
        open(os.path.join(game, "main", "models", "player", "my_skin.tik"), "w").close()
        self.assertEqual(mohreplay.player_models(game), ["allied_airborne", "german_winter_1", "my_skin"])
        self.assertEqual(mohreplay.player_models(os.path.join(game, "nothing")), [])

    def test_weapons(self):
        self.assertEqual([mohreplay.weapon_of(kill) for kill in DEMO["kills"]],
                         ["rifle", "rifle", "other", "mg", "other", "rifle"])
        # indexes made before weapons were, messages not known
        self.assertEqual([mohreplay.weapon_of(kill) for kill in ({}, {"weapon": ""}, {"weapon": "laser"})],
                         ["other"] * 3)
        # the index tells grenade kills, whatever the names
        self.assertFalse(mohreplay.is_grenade({"text": "Bob was rifled by Grenadier", "grenade": False, "weapon": "rifle"}))
        self.assertTrue(mohreplay.is_grenade({"text": "Bob is picking t-'s shrapnel out of his teeth"}))

    def test_filter_kills(self):
        """The kills of a demo the filter keeps, from its summary, as the demo
        list counts them."""
        summary, kf = mohreplay.summarize(DEMO), mohreplay.KillFilter
        self.assertEqual(mohreplay.filter_kills(summary, kf(weapons=["other"])), (2, {"t-": ["t-", 1]}))
        self.assertEqual(mohreplay.filter_kills(summary, kf(["T-"])), (3, {"t-": ["t-", 3]}))
        self.assertEqual(mohreplay.filter_kills(summary, kf(["phil"])), (0, {}))
        self.assertEqual(mohreplay.filter_kills(summary, kf(victims=["bob"])), (2, {"t-": ["t-", 1]}))
        self.assertEqual(mohreplay.filter_kills(summary, kf(recorder=True, specced=True, weapons=["rifle"])),
                         (2, {"t-": ["t-", 2]}))

    def test_numbered_clips(self):
        """The game numbers each stretch a clip plays, counting on through the
        video, from the number it's given."""
        clips = [mohreplay.only_clip("a", "kills", [(0, 5000), (9000, 15000)]),
                 mohreplay.range_clip("b", 1000, 2000), mohreplay.only_clip("c")]
        mohreplay.numbered(clips, True)
        self.assertEqual([clip["number"] for clip in clips], [1, 3, 4])
        self.assertEqual([clip["commands"][0] for clip in clips],
                         ["set cl_demoClipNumbers 1", "set cl_demoClipNumbers 3", "set cl_demoClipNumbers 4"])
        clip = mohreplay.numbered([mohreplay.range_clip("b", 1000, 2000)], False)[0]
        self.assertEqual(clip["commands"][0], "set cl_demoClipNumbers 0")

    def test_split_clips(self):
        """A video in parts of a length, cut between the stretches it plays,
        a stretch longer than a part in pieces, less than half a part left at
        the end in the last part."""
        split = mohreplay.split_clips
        kills = mohreplay.only_clip("a", "kills", [(0, 10000), (20000, 30000), (40000, 50000)])
        kills["frags"] = [[{"time": 4000}], [{"time": 24000}], [{"time": 44000}]]
        rest = mohreplay.only_clip("b")
        # short enough: as they are
        part, = split([kills, rest], 30000)
        self.assertIs(part[0], kills)
        self.assertIs(part[1], rest)
        # the first two stretches, then the last, where the video stops: half
        # a part, not less
        first, second = split([kills], 20000)
        self.assertEqual(len(first), 1)
        self.assertEqual((first[0]["spans"], first[0]["start"], first[0]["end"]), ([(0, 10000), (20000, 30000)], 0, 30000))
        self.assertEqual(first[0]["commands"][-4:], ["demoseek 0.000", "demopause 0", "demoonly kills",
                                                     "demovideo {part} 30.000"])
        self.assertEqual(first[0]["frags"], [[{"time": 4000}], [{"time": 24000}]])
        self.assertEqual((second[0]["spans"], second[0]["end"], second[0]["frags"]), ([(40000, 50000)], None,
                                                                                      [[{"time": 44000}]]))
        self.assertEqual(second[0]["commands"][-4:], ["demoseek 40.000", "demopause 0", "demoonly kills",
                                                      "demovideo {part}"])
        self.assertEqual(kills["spans"], [(0, 10000), (20000, 30000), (40000, 50000)])
        # less than half a part left: in the part before
        part, = split([kills], 25000)
        self.assertIs(part[0], kills)
        parts = split([mohreplay.range_clip("e", 0, (8 * 60 + 14) * 60000)], 60 * 60000)
        self.assertEqual([c["spans"][0][1] - c["spans"][0][0] for part in parts for c in part],
                         [60 * 60000] * 7 + [74 * 60000])
        # a stretch longer than a part starts one and goes on in the next
        long = mohreplay.range_clip("c", 60000, 210000)
        parts = split([mohreplay.range_clip("d", 0, 20000), long], 60000)
        self.assertEqual([[(c["demo"], c["spans"], c.get("continued", False)) for c in part] for part in parts],
                         [[("d", [(0, 20000)], False)], [("c", [(60000, 120000)], False)],
                          [("c", [(120000, 180000)], True)], [("c", [(180000, 210000)], True)]])
        self.assertEqual([part[0]["commands"][-1] for part in parts[1:]],
                         ["demovideo {part} 120.000", "demovideo {part} 180.000", "demovideo {part} 210.000"])
        self.assertEqual(parts[2][0]["commands"][1], "demoseek 120.000")
        # numbered on from part to part, a stretch going on keeping its number
        first = 1
        for part in parts:
            mohreplay.numbered(part, True, first)
            first += sum(len(c["spans"]) - bool(c.get("continued")) for c in part)
        self.assertEqual([part[0]["number"] for part in parts], [1, 2, 2, 2])
        # the length of a clip not indexed isn't known: it stays whole, with
        # the clip after it
        part, = split([rest, mohreplay.range_clip("d", 0, 5000)], 10000)
        self.assertIs(part[0], rest)

    def test_clip_rows(self):
        """The list of a video's clips: a row for each frag, the clip's
        number, start in the video and the demo, and the frag."""
        t = mohreplay.KillFilter(["t-"])
        count, spans = mohreplay.demoonly_plan(DEMO, "kills", 4000, 2000, t)
        frags = mohreplay.clip_frags(DEMO, spans, "kills", t)
        self.assertEqual(sum(len(f) for f in frags), count)
        clip = mohreplay.only_clip("first", "kills", spans, t)
        clip["frags"] = frags
        other = mohreplay.range_clip("other", 60000, 70000)
        rows = mohreplay.clip_rows(mohreplay.numbered([clip, other], True))
        self.assertEqual(rows[0][:3], [1, "0:00", "first"])
        killed = [k for k in DEMO["kills"] if k["time"] == spans[0][0] + 4000][0]
        self.assertEqual(rows[0][3:], [mohreplay.clock(killed["time"]), "t-",
                                       mohreplay.without_colors(killed["victimName"]),
                                       mohreplay.WEAPONS[mohreplay.weapon_of(killed)]])
        self.assertEqual([row[0] for row in rows[:-1]], sorted(row[0] for row in rows[:-1]))
        self.assertEqual(len(rows) - 1, count)
        # a clip without frags has a row, after the time of those before
        self.assertEqual(rows[-1], [len(spans) + 1, mohreplay.clock(mohreplay.length(spans)), "other", "1:00",
                                    "", "", ""])
        self.assertEqual(mohreplay.without_colors("^1Phil^^7 x"), "Phil^ x")

    def test_search_demo(self):
        """Words separated by spaces must all be in a demo, commas separate
        searches that each find demos."""
        parts = mohreplay.search_parts
        self.assertEqual(parts(" T-  ^1Phil , team1 ,, "), [["t-", "phil"], ["team1"]])
        self.assertEqual(parts(" , "), [])
        summary = mohreplay.summarize(DEMO)
        search = lambda text, name="clan/first": mohreplay.search_demo(name, summary, parts(text))
        self.assertEqual(search("phil"), {"phil": ["^1Phil", 0]})
        self.assertEqual(search("phil nobody"), None)
        self.assertEqual(search("phil nobody, bob"), {"bob": ["Bob", 0]})
        self.assertEqual(search("nobody, team1"), {})
        self.assertEqual(search("clan bob, phil, al"), {"bob": ["Bob", 0], "phil": ["^1Phil", 0], "al": ["Al", 0]})
        self.assertEqual(search("nobody, mohdm6"), None)

    def test_one_character_wildcard(self):
        """A * in a search stands for any one character, in the names of
        fraggers, victims and recorders and the demo search."""
        has = mohreplay.has_part
        self.assertTrue(has("[clan] snipe", "sn*pe") and has("sn1pe-", "sn*pe") and has("sn*pe", "sn*pe"))
        self.assertFalse(has("snpe", "sn*pe") or has("sn11pe", "sn*pe") or has("sn", "sn*"))
        self.assertTrue(has("snipee", "sn*pe") and has("a.b", "a.b") and not has("axb", "a.b"))
        self.assertTrue(has("tommeh", "t*m*e") and not has("tomeh", "t*m*e"))
        kill_filter = mohreplay.KillFilter(["Sn*pe"], ["*o*"])
        self.assertEqual(kill_filter.commands(), ["demofilter clear", 'demofilter killer "Sn*pe"', 'demofilter victim "*o*"'])
        self.assertTrue(kill_filter.keeps({"killerName": "^2Sn1pe-", "victimName": "Bob", "weapon": "mg"}))
        self.assertTrue(kill_filter.keeps({"killerName": "snipe", "victimName": "Bob", "weapon": "mg"}))
        self.assertFalse(kill_filter.keeps({"killerName": "Snpe", "victimName": "Bob", "weapon": "mg"}))
        self.assertFalse(kill_filter.keeps({"killerName": "snipe", "victimName": "Al", "weapon": "mg"}))
        summary = mohreplay.summarize(DEMO)
        search = lambda text: mohreplay.search_demo("clan/first", summary, mohreplay.search_parts(text))
        self.assertEqual(search("ph*l"), {"phil": ["^1Phil", 0]})
        self.assertEqual(search("c*an b*b"), {"bob": ["Bob", 0]})
        self.assertEqual(search("ph**l"), None)

    def test_deaths_shown(self):
        # t- killed at 3:20 while shown, Phil at 6:40 while followed
        deaths = mohreplay.deaths_shown(DEMO)
        self.assertEqual([kill["time"] for kill in DEMO["kills"] if id(kill) in deaths], [200000, 400000])
        # the client tells, whatever the name
        demo = json.loads(json.dumps(DEMO))
        demo["kills"][3]["victimName"] = "t- (renamed)"
        self.assertEqual(len(mohreplay.deaths_shown(demo)), 2)
        demo["kills"][3]["victim"] = -1
        self.assertEqual(len(mohreplay.deaths_shown(demo)), 1)
        # a player killing themselves while shown made no frag
        demo = json.loads(json.dumps(DEMO))
        demo["kills"][1].update(victim=0, victimName="t-", text="t- blew himself up")
        self.assertEqual(mohreplay.demoonly_plan(demo, "kills", kill_filter=mohreplay.KillFilter(["t-"]))[0], 2)
        self.assertEqual(mohreplay.summarize(demo)["players"]["t-"], ["t-", 2])

    def test_team_kills(self):
        plan, demo = plan_4s, team_demo()
        # not counted, and only played with a kill of their killer at most 2 s
        # away: not Phil's 1.5 s after killing t-, who's shown, at 3:20
        self.assertEqual(plan(demo, "kills"), (4, [(6000, 18000), (246000, 252000)]))
        self.assertEqual(plan(demo, "kills", kill_filter=mohreplay.KillFilter(["T-"])), (3, [(6000, 18000)]))
        self.assertEqual(mohreplay.summarize(demo)["players"]["t-"], ["t-", 3])

    def test_grenade_kills(self):
        plan, demo = plan_4s, grenade_demo()
        self.assertEqual([mohreplay.is_grenade(kill) for kill in demo["kills"]][2:5], [False, True, True])
        # not counted, not played
        self.assertEqual(plan(demo, "kills", kill_filter=mohreplay.KillFilter(["T-"])), (3, [(6000, 16500)]))
        self.assertEqual(mohreplay.summarize(demo)["players"]["t-"], ["t-", 3])

    def test_levels(self):
        """A kill's clip stays inside its level, and who is shown is looked
        up on its level, which starts with a watch event."""
        demo = json.loads(json.dumps(DEMO))
        demo["maps"].append({"time": 256000, "map": "obj/obj_team2"})
        demo["watched"][1:1] = [{"time": 256000, "client": 0, "name": "t-"}]
        demo["kills"].append({"time": 257000, "killer": 0, "killerName": "t-", "victim": 2, "victimName": "Bob",
                              "text": "Bob was rifled by t-"})
        demo["kills"].sort(key=lambda kill: kill["time"])
        self.assertEqual(plan_4s(demo, "kills", kill_filter=mohreplay.KillFilter(["T-"])),
                         (4, [(6000, 16500), (256000, 259000)]))
        kill = {"time": 255000}
        self.assertEqual(mohreplay.kill_span(demo, kill, 0, 600000, 4000), (251000, 256000))
        self.assertEqual(mohreplay.kill_span(demo, dict(kill, time=257000), 0, 600000, 4000), (256000, 259000))
        # and the demo's end
        self.assertEqual(mohreplay.kill_span(demo, dict(kill, time=599000), 0, 602000, 4000), (595000, 600000))

    def test_fights(self):
        """2 s before and after a kill, or before the fight that led to it
        and after the fragger's shots going on with it, 5 s from it at most."""
        demo = json.loads(json.dumps(DEMO))
        span = lambda **kill: mohreplay.kill_span(demo, dict(time=100000, **kill), 0, 600000)
        self.assertEqual(span(), (98000, 102000))
        self.assertEqual(span(fightStart=98500, fightEnd=101500), (96500, 103500))
        # 5 s from it at most, and never less than the lead set
        self.assertEqual(span(fightStart=97000, fightEnd=103500), (95000, 105000))
        self.assertEqual(span(fightStart=80000, fightEnd=120000), (95000, 105000))
        self.assertEqual(mohreplay.kill_span(demo, dict(time=100000, fightStart=80000), 0, 600000, 10000, 9000),
                         (90000, 109000))

    def test_lead_in_and_out(self):
        plan, t = mohreplay.demoonly_plan, mohreplay.KillFilter(["T-"])
        self.assertEqual(plan(DEMO, "kills", 1000, 400, t), (3, [(9000, 12400), (13500, 14900)]))
        self.assertEqual(plan(DEMO, "kills", 10000, 5000, t), (3, [(0, 19500)]))

    def test_wording(self):
        self.assertEqual([mohreplay.the(n, "frag", "frags") for n in (0, 1, 3)], ["No frags", "The frag", "The 3 frags"])
        self.assertEqual([mohreplay.about(msec) for msec in (20000, 291000, 3900000)],
                         ["under a minute", "about 5 min", "about 1 h 5 min"])

    def test_stretches(self):
        spans = [(6000, 12000), (-500, 1500), (8000, 14000), (14800, 20000), (25000, 30000), (30600, 32000)]
        # overlapping or less than a second apart
        self.assertEqual(mohreplay.stretches(spans), [(0, 1500), (6000, 20000), (25000, 32000)])
        # but not when the player shown changes in between, even right at the end
        self.assertEqual(mohreplay.stretches(spans, [10000, 30000]),
                         [(0, 1500), (6000, 20000), (25000, 30000), (30600, 32000)])
        self.assertEqual(mohreplay.stretches(spans, [14500]), [(0, 1500), (6000, 14000), (14800, 20000), (25000, 32000)])


class TestGame(FakeGameCase):
    def start(self, driver=None):
        self.output, self.exits = [], []
        game = mohreplay.Game(self.output.append, lambda: self.exits.append(True))
        self.addCleanup(game.stop)
        game.start(self.exe, self.game_dir, self.demos, 640, 480, driver=driver)
        return game

    def test_commands_arrive_in_order(self):
        game = self.start()
        # sent before the game opens its pipe: they wait in the queue
        for number in range(50):
            game.send(f"echo {number}")
        self.assertTrue(wait_until(lambda: len(self.commands(game)) == 50))
        self.assertEqual(self.commands(game), [f"echo {n}" for n in range(50)])

    def test_home_folder(self):
        game = self.start()
        home = game.home
        self.assertTrue(os.path.islink(os.path.join(home, "main", "demos")))
        with open(os.path.join(home, "main", "replay.cfg")) as f:
            self.assertIn('bind PAUSE "demopause"', f.read())
        game.stop()
        self.assertFalse(os.path.exists(home))
        # the demos are only linked, never removed
        self.assertTrue(os.path.isfile(os.path.join(self.demos, "first.dm3")))

    def test_settings_kept(self):
        game = self.start()
        self.assertFalse(os.path.exists(os.path.join(game.home, mohreplay.GAME_CONFIG)))
        # what the game writes when a setting changes
        with open(os.path.join(game.home, mohreplay.GAME_CONFIG), "w") as f:
            f.write('seta cg_fov "90"\n')
        game.stop()
        with open(mohreplay.kept_config()) as f:
            self.assertEqual(f.read(), 'seta cg_fov "90"\n')
        # the next game starts with them
        game.start(self.exe, self.game_dir, self.demos, 640, 480)
        with open(os.path.join(game.home, mohreplay.GAME_CONFIG)) as f:
            self.assertEqual(f.read(), 'seta cg_fov "90"\n')
        # and when it quits by itself, they're kept already
        with open(os.path.join(game.home, mohreplay.GAME_CONFIG), "w") as f:
            f.write('seta cg_fov "100"\n')
        game.send("quit")
        self.assertTrue(wait_until(lambda: self.exits))
        with open(mohreplay.kept_config()) as f:
            self.assertEqual(f.read(), 'seta cg_fov "100"\n')

    def test_app_settings_not_kept(self):
        game = self.start()
        with open(os.path.join(game.home, mohreplay.GAME_CONFIG), "w") as f:
            f.write('unbindall\nbind w "+forward"\nbind UPARROW "demonextkill t-"\nbind PAUSE "demopause"\n'
                    'seta com_pipefile "replay_pipe"\nseta cg_fov "90"\n')
        game.stop()
        # nor in the game folder, where other games would get them
        for path in (mohreplay.kept_config(), os.path.join(self.game_dir, mohreplay.GAME_CONFIG),
                     os.path.join(self.game_dir, "main", "omconfig.cfg")):
            with open(path) as f:
                self.assertEqual(f.read(), 'unbindall\nbind w "+forward"\nseta cg_fov "90"\n')

    def test_recorder_settings_not_kept(self):
        with open(os.path.join(self.tmp, "kept.cfg"), "w") as f:
            f.write('seta cg_fov "90"\n')
        os.makedirs(os.path.dirname(mohreplay.kept_config()))
        shutil.copyfile(os.path.join(self.tmp, "kept.cfg"), mohreplay.kept_config())
        game = mohreplay.Game(lambda line: None, lambda: None, keep_config=False)
        self.addCleanup(game.stop)
        game.start(self.exe, self.game_dir, self.demos, 640, 480)
        with open(os.path.join(game.home, mohreplay.GAME_CONFIG), "r+") as f:
            self.assertEqual(f.read(), 'seta cg_fov "90"\n')
            f.write('seta cg_fov "120"\n')
        game.stop()
        with open(mohreplay.kept_config()) as f:
            self.assertEqual(f.read(), 'seta cg_fov "90"\n')

    def test_xwayland_when_wayland_fails(self):
        with mock.patch.dict(os.environ, {"FAKEGAME_FAIL_DRIVER": "wayland"}):
            os.environ.pop("SDL_VIDEODRIVER")
            game = self.start(driver="wayland")
            self.assertTrue(wait_until(lambda: game.driver == "x11" and game.running()))
            self.assertIn("The game didn't start on Wayland, trying XWayland", self.output)
            game.send("echo hi")
            self.assertTrue(wait_until(lambda: self.commands(game) == ["echo hi"]))
            self.assertFalse(self.exits)

    def test_exit_is_reported(self):
        game = self.start()
        game.send("quit")
        self.assertTrue(wait_until(lambda: self.exits))
        self.assertFalse(game.running())

    def test_stop_kills_a_hung_game(self):
        with mock.patch.dict(os.environ, {"FAKEGAME_HANG": "1"}), \
                mock.patch.multiple(mohreplay.Game, QUIT_WAIT=300, TERM_WAIT=300):
            game = self.start()
            self.assertTrue(wait_until(lambda: os.path.exists(os.path.join(game.home, "main", mohreplay.PIPE))))
            started = time.monotonic()
            game.stop()
            self.assertFalse(game.running())
            self.assertLess(time.monotonic() - started, 3)

    def test_missing_program(self):
        self.output, self.exits = [], []
        game = mohreplay.Game(self.output.append, lambda: self.exits.append(True))
        game.start(os.path.join(self.tmp, "nothing"), self.game_dir, self.demos, 640, 480)
        self.assertTrue(wait_until(lambda: self.exits))
        self.assertTrue(any(line.startswith("ERROR: couldn't start") for line in self.output))
        game.stop()


class TestWindow(FakeGameCase):
    def test_choose_folder(self):
        window = self.window()
        self.assertEqual(window.folder_button.text(), "demos")
        other = os.path.join(self.tmp, "more demos")
        os.makedirs(other)
        shutil.copyfile(os.path.join(self.demos, "first.dm3"), os.path.join(other, "again.dm3"))
        with mock.patch.object(mohreplay.QFileDialog, "getExistingDirectory", lambda *args: other):
            window.choose_folder()
        self.assertEqual(self.settings.value("demos"), other)
        self.assertEqual(window.folder_button.text(), "more demos")
        self.assertTrue(wait_until(lambda: list(window.demo_items) == ["again"]))

    def test_double_click_plays(self):
        """A demo plays in the game, told the filter, for its keys that go to
        frags; a folder doesn't."""
        window = self.window()
        self.choose(window, killer="t-")
        window.demos.itemActivated.emit(window.demo_items["first"], 0)
        self.assertTrue(wait_until(lambda: self.game_state(window).get("demo") == "first"))
        self.assertEqual(self.commands(window.game)[:3], ["demofilter clear", 'demofilter killer "t-"', 'demo "first"'])
        self.assertEqual((mohreplay.BINDS["UPARROW"], mohreplay.BINDS["DOWNARROW"]), ("demonextkill", "demoprevkill"))
        # told when it changes too
        self.choose(window, killer="bob")
        self.assertTrue(wait_until(lambda: self.commands(window.game)[-2:] == ["demofilter clear", 'demofilter killer "bob"']))

    def test_weapon_menu(self):
        """It takes several, and any."""
        window = self.window()
        menu = window.weapon.menu()
        self.assertEqual([a.text() for a in menu.actions() if a.text()], list(mohreplay.WEAPONS.values()) + ["Any weapon"])
        window.weapon_actions["sniper"].trigger()
        window.weapon_actions["mg"].trigger()
        self.assertEqual(window.weapon.text(), "Sniper, StG / BAR")
        next(a for a in menu.actions() if a.text() == "Any weapon").trigger()
        self.assertEqual(window.weapon.text(), "Any weapon")

    def test_no_panels_left(self):
        """A list of demos to make videos of: nothing to watch them with."""
        window = self.window()
        labels = {label.text() for label in window.findChildren(mohreplay.QLabel)}
        self.assertTrue({"Demos", "Frags", "Recorder", "Rules", "Fragger", "Victim", "Weapon"} <= labels)
        self.assertFalse(hasattr(window, "slider") or hasattr(window, "kills") or hasattr(window, "tabs"))
        self.assertEqual(window.findChildren(QToolBar) + window.findChildren(QStatusBar), [])

    def shown(self, window):
        """The names of the demos shown, in folders shown."""
        def shown(item):
            return not item.isHidden() and (not item.parent() or shown(item.parent()))
        return sorted(name for name, item in window.demo_items.items() if shown(item))

    def test_filter_demos(self):
        self.add_demo("second-obj_team2", DEMO)
        window = self.window()
        self.assertEqual(self.shown(window), ["first", "second-obj_team2"])
        window.filter.setText("TEAM2")
        self.assertEqual(self.shown(window), ["second-obj_team2"])

    def test_missing_demo(self):
        window = self.window()
        window.start_game()
        window.game.send('demo "nothing"')
        self.assertTrue(wait_until(lambda: "Couldn't load" in window.message.text()))

    def test_game_quitting(self):
        window = self.window()
        self.playing(window)
        window.game.send("quit")
        self.assertTrue(wait_until(lambda: "The game quit" in window.message.text()))
        # and it starts again with a demo
        self.playing(window)


class TestSettings(FakeGameCase):
    def test_tabs(self):
        # on the window, as the app opens it: left alone, PySide may delete
        # it twice when Python quits
        window = self.window()
        dialog = mohreplay.SettingsDialog(self.settings, window, "video")
        self.assertEqual([dialog.tabs.tabText(i) for i in range(dialog.tabs.count())], ["Setup", "Video", "Censor"])
        self.assertEqual(dialog.tabs.tabText(dialog.tabs.currentIndex()), "Video")
        self.assertEqual(sorted(dialog.fields), ["exe", "game", "rec/ffmpeg"])
        other = os.path.join(self.tmp, "other game")
        dialog.fields["game"].setText(other)
        dialog.fields["rec/ffmpeg"].setText("/my/ffmpeg")
        dialog.videos.size.setCurrentText("big")
        dialog.videos.blur.setChecked(True)
        dialog.accept()
        # a size that isn't one: nothing kept
        self.assertEqual(self.messages.shown, [("Settings", "The video size should be like 1920x1080.")])
        self.assertIsNone(self.settings.value("rec/size"))
        self.assertEqual(self.settings.value("game"), self.game_dir)
        dialog.videos.size.setCurrentText("1280×720")
        dialog.accept()
        self.assertEqual(dialog.result(), QDialog.Accepted)
        self.assertEqual((self.settings.value("game"), self.settings.value("rec/ffmpeg")), (other, "/my/ffmpeg"))
        self.assertEqual((self.settings.value("rec/size"), self.settings.value("rec/blur")), ("1280x720", "true"))
        self.assertEqual(mohreplay.video_settings(self.settings)["ffmpeg"], "/my/ffmpeg")
        self.assertEqual(mohreplay.video_summary(self.settings),
                         "1280x720, 60 fps with motion blur, H.264 (CRF 20), sound 192k, words censored")
        again = mohreplay.SettingsDialog(self.settings, window)
        self.assertEqual(again.tabs.currentIndex(), 0)
        self.assertTrue(again.videos.blur.isChecked())

    def test_advanced_folded(self):
        """Folded, unless something in it is in use."""
        window = self.window()
        dialog = mohreplay.SettingsDialog(self.settings, window, "video")
        dialog.show()
        videos = dialog.videos
        self.assertFalse(videos.fold.isChecked() or videos.folded.isVisible())
        videos.fold.click()
        self.assertTrue(videos.folded.isVisible())
        videos.fold.click()
        self.assertFalse(videos.folded.isVisible())
        dialog.close()
        for key, value in (("rec/codec", "H.265"), ("rec/advanced", "-c:v libx264")):
            self.settings.setValue(key, value)
            dialog = mohreplay.SettingsDialog(self.settings, window, "video")
            dialog.show()
            self.assertTrue(dialog.videos.fold.isChecked() and dialog.videos.folded.isVisible(), key)
            dialog.close()
            self.settings.remove(key)
        # what's in it is kept when folded
        dialog = mohreplay.SettingsDialog(self.settings, window, "video")
        dialog.videos.codec.setCurrentText("H.265")
        dialog.accept()
        self.assertEqual(self.settings.value("rec/codec"), "H.265")

    def test_censor(self):
        """The words starred out, on by default, as the game and the videos
        are told them."""
        window = self.window()
        dialog = mohreplay.SettingsDialog(self.settings, window, "censor")
        self.assertEqual(dialog.tabs.currentIndex(), 2)
        self.assertTrue(dialog.censor.isChecked())
        self.assertEqual(dialog.censor_words.toPlainText(), mohreplay.CENSOR_WORDS)
        self.assertEqual(mohreplay.censored_words(self.settings).split()[:3], ["nigger", "nigga", "nlgger"])
        dialog.censor_words.setPlainText("gay, nazi,jew\nkkk")
        dialog.accept()
        self.assertEqual(mohreplay.censored_words(self.settings), "gay nazi jew kkk")
        self.assertEqual(mohreplay.video_settings(self.settings)["censor"], "gay nazi jew kkk")
        again = mohreplay.SettingsDialog(self.settings, window)
        self.assertEqual(again.censor_words.toPlainText(), "gay, nazi,jew\nkkk")
        again.censor.setChecked(False)
        self.assertFalse(again.censor_words.isEnabled())
        again.accept()
        self.assertEqual(mohreplay.censored_words(self.settings), "")
        self.assertNotIn("words censored", mohreplay.video_summary(self.settings))
        again = mohreplay.SettingsDialog(self.settings, window)
        again.censor_words.setPlainText("x")
        next(button for button in again.findChildren(mohreplay.QPushButton) if button.text() == "Default words").click()
        self.assertEqual(again.censor_words.toPlainText(), mohreplay.CENSOR_WORDS)

    def test_settings_applied(self):
        """A game playing stops, as the next one starts with them; the demos
        are indexed again only for another game program."""
        window = self.window()
        self.playing(window)
        opened = []
        with mock.patch.object(mohreplay.SettingsDialog, "exec", lambda dialog: 0):
            self.assertFalse(window.edit_settings())
        self.assertTrue(window.game.running())
        with mock.patch.object(window.library, "open", lambda *args: opened.append(args)), \
                mock.patch.object(mohreplay.SettingsDialog, "exec", lambda dialog: (dialog.accept(), dialog.result())[1]):
            self.assertTrue(window.edit_settings())
            self.assertFalse(opened)
            self.assertFalse(window.game.running())

            def other_program(dialog):
                dialog.fields["exe"].setText(os.path.join(self.bin, "other"))
                dialog.accept()
                return dialog.result()
            with mock.patch.object(mohreplay.SettingsDialog, "exec", other_program):
                self.assertTrue(window.edit_settings())
            self.assertEqual(len(opened), 1)

    def test_video_settings(self):
        video = mohreplay.video_settings(self.settings)
        self.assertEqual((video["width"], video["height"], video["fps"], video["sound"]), (1920, 1080, "60", True))
        self.assertEqual(video["options"], "-c:v libx264 -preset medium -crf 20 -pix_fmt yuv420p "
                                           "-c:a aac -b:a 192k -movflags +faststart")
        # watching without the gun: videos too, unless they're told otherwise
        self.settings.setValue("viewmodel", "0")
        self.assertEqual(mohreplay.video_settings(self.settings)["viewmodel"], "0")
        self.settings.setValue("rec/viewmodel", "1")
        self.assertEqual(mohreplay.video_settings(self.settings)["viewmodel"], "1")


class TestLibrary(FakeGameCase):
    def setUp(self):
        super().setUp()
        other = json.loads(json.dumps(DEMO))
        other["maps"] = [{"time": 0, "map": "dm/mohdm6", "rules": "realism", "realismTicks": 900, "defaultTicks": 0}]
        other["duration"] = 361000
        for kill in other["kills"]:
            kill["killerName"] = kill["killerName"].replace("t-", "<KoS>Bob")
        self.add_demo("other", other)
        with open(os.path.join(self.demos, "broken.dm3"), "w") as f:
            f.write("not a demo")

    def indexed(self, window, names=("first", "other")):
        self.assertTrue(wait_until(lambda: set(names) <= set(window.library.demos)
                                   and not window.library.progress(), 20), "not indexed")
        wait_until(lambda: False, 0.4)  # the list is updated after them

    def shown(self, window):
        return TestWindow.shown(self, window)

    def lines(self, folder):
        """The lines of the file of indexes next to the demos of a folder."""
        with open(os.path.join(folder, mohreplay.INDEX_FILE), "rb") as f:
            return f.read().splitlines()

    def test_closed_before_opening(self):
        window = self.window()
        window.close()
        app.processEvents()
        self.assertIsNone(window.library.folder)
        self.assertFalse(os.path.exists(os.path.join(self.tmp, "cache")))

    def test_index_and_cache(self):
        window = self.window()
        self.indexed(window)
        self.assertEqual(window.demo_items["first"].text(mohreplay.COL_LENGTH), "10:00")
        self.assertEqual(window.demo_items["other"].text(mohreplay.COL_LENGTH), "6:01")
        self.assertEqual(window.demo_items["broken"].text(mohreplay.COL_LENGTH), "")
        self.assertIn("broken", window.library.failed)
        # how many there are, and how many are listed
        self.assertEqual(window.demo_count.text(), "3 demos, 8 frags, about 1 min")
        self.assertEqual(window.demo_count.toolTip(), "3 demos in the folder, 2 of them indexed; 1 couldn't be indexed")
        window.filter.setText("mohdm6")
        self.assertEqual(window.demo_count.text(), "1 of 3 demos listed, 4 frags, under a minute")
        self.assertIn("2 left out by the search and the filters", window.demo_count.toolTip())
        window.filter.clear()
        window.demo_items["other"].setSelected(True)
        self.assertEqual(window.demo_count.text(), "1 of the 3 demos selected, 4 frags, under a minute")
        window.demos.clearSelection()
        summary = window.library.demos["first"]
        self.assertEqual(summary["maps"], ["obj/obj_team1"])
        # the kills made while they're shown
        self.assertEqual(summary["players"]["t-"], ["t-", 3])
        self.assertEqual(summary["players"]["phil"], ["^1Phil", 0])
        self.assertIn("&lt;KoS&gt;Bob (3)", window.demo_items["other"].toolTip(mohreplay.COL_DEMO))
        self.assertEqual(window.demo_items["first"].text(mohreplay.COL_MAP), "obj_team1")
        self.assertEqual(window.demo_items["first"].text(mohreplay.COL_RULES), "")
        self.assertEqual(window.demo_items["other"].text(mohreplay.COL_RULES), "Realism")
        self.assertEqual(window.demo_items["first"].text(mohreplay.COL_RECORDER), "t-")
        self.assertEqual(window.demo_items["broken"].text(mohreplay.COL_RECORDER), "")
        self.assertIn("6:01, realism", window.demo_items["other"].toolTip(mohreplay.COL_DEMO))
        # next to the demos, in one file
        self.assertEqual(sorted(json.loads(line.split(b"\t")[0])["demo"] for line in self.lines(self.demos)),
                         ["first", "other"])
        self.assertEqual(sorted(os.listdir(self.demos)), [mohreplay.INDEX_FILE, "broken.dm3", "first.dm3", "other.dm3"])
        self.assertEqual(window.library.index("first")["duration"], 600000)
        self.assertIsNone(window.library.index("broken"))

        # kept: nothing is indexed again
        again = self.window()
        self.assertTrue(wait_until(lambda: again.library.demos))
        self.assertEqual(set(again.library.demos), {"first", "other"})
        self.assertEqual(again.library.waiting, [])
        self.assertEqual(again.library.busy, {"broken"})  # it's tried once more

    def test_redone_when_the_demo_changes(self):
        window = self.window()
        self.indexed(window)
        changed = dict(DEMO, duration=125000)
        self.add_demo("first", changed)
        future = time.time() + 5
        os.utime(os.path.join(self.demos, "first.dm3"), (future, future))
        window.library.rescan()
        self.assertTrue(wait_until(lambda: window.library.demos["first"]["duration"] == 125000, 20))
        self.assertTrue(wait_until(lambda: window.demo_items["first"].text(mohreplay.COL_LENGTH) == "2:05"))

    def test_new_and_removed_demos(self):
        window = self.window()
        self.indexed(window)
        self.add_demo("new", DEMO)
        os.remove(os.path.join(self.demos, "other.dm3"))
        # the folder is watched
        self.assertTrue(wait_until(lambda: "new" in window.demo_items and window.demo_items["new"].text(mohreplay.COL_LENGTH), 20))
        self.assertNotIn("other", window.demo_items)
        self.assertNotIn("other", window.library.demos)
        self.assertIsNone(window.library.index("other"))

    def test_demos_in_folders(self):
        # named by their path, so the same file name in two folders is two
        # demos; hidden folders are left out
        for name in ("clan/first", "clan/war 2/first", ".trash/first"):
            os.makedirs(os.path.dirname(os.path.join(self.demos, name)), exist_ok=True)
            self.add_demo(name, DEMO)
        window = self.window()
        self.indexed(window, ("first", "other", "clan/first", "clan/war 2/first"))
        self.assertEqual(self.shown(window), ["broken", "clan/first", "clan/war 2/first", "first", "other"])
        self.assertEqual(window.demo_items["clan/war 2/first"].text(mohreplay.COL_LENGTH), "10:00")
        self.assertEqual(len(self.lines(os.path.join(self.demos, "clan", "war 2"))), 1)
        self.assertEqual(len(self.lines(os.path.join(self.demos, "clan"))), 1)
        # in their folders, by their file name; folders first
        clan = window.folder_items["clan"]
        self.assertEqual(clan.text(mohreplay.COL_DEMO), "clan (2)")
        self.assertEqual([clan.child(i).text(mohreplay.COL_DEMO) for i in range(clan.childCount())], ["war 2 (1)", "first"])
        self.assertIs(window.demo_items["clan/war 2/first"].parent(), window.folder_items["clan/war 2"])
        self.assertEqual(window.demos.topLevelItem(0), clan)
        self.assertTrue(window.demos.rootIsDecorated())
        window.filter.setText("WAR")
        self.assertEqual(self.shown(window), ["clan/war 2/first"])
        self.assertEqual(clan.text(mohreplay.COL_DEMO), "clan (1)")
        window.filter.setText("first")
        self.assertEqual(window.demos_of([clan]), ["clan/war 2/first", "clan/first"])
        window.filter.setText("")
        # a double-click on a folder plays nothing
        window.demos.itemActivated.emit(clan, 0)
        self.assertIsNone(window.pending_demo)
        window.demos.itemActivated.emit(window.demo_items["clan/war 2/first"], 0)
        self.assertEqual(window.pending_demo, "clan/war 2/first")
        self.playing(window, "clan/war 2/first")
        # the folders are watched, new ones too
        os.makedirs(os.path.join(self.demos, "clan", "new"))
        self.assertTrue(wait_until(lambda: os.path.join(self.demos, "clan", "new") in window.library.watcher.directories(), 20))
        self.add_demo("clan/new/late", DEMO)
        self.assertTrue(wait_until(lambda: "clan/new/late" in window.demo_items
                                   and window.demo_items["clan/new/late"].text(mohreplay.COL_LENGTH), 20))
        os.remove(os.path.join(self.demos, "clan", "first.dm3"))
        self.assertTrue(wait_until(lambda: "clan/first" not in window.demo_items, 20))
        self.assertIsNone(window.library.index("clan/first"))
        # a folder left empty goes
        shutil.rmtree(os.path.join(self.demos, "clan", "new"))
        self.assertTrue(wait_until(lambda: "clan/new" not in window.folder_items, 20))
        self.assertIn("clan", window.folder_items)

    def indexing_runs(self, window):
        """How many demos it had to index."""
        return window.library.total

    def test_indexes_shared_between_folders(self):
        """The same indexes count whichever folder above the demos is opened,
        and they go with the demos."""
        os.makedirs(os.path.join(self.demos, "clan"))
        self.add_demo("clan/war", DEMO)
        window = self.window()
        self.indexed(window, ("first", "other", "clan/war"))
        window.close()
        # only that folder: nothing to do
        self.settings.setValue("demos", os.path.join(self.demos, "clan"))
        again = self.window()
        self.assertTrue(wait_until(lambda: "war" in again.library.demos))
        self.assertEqual(self.indexing_runs(again), 0)
        again.close()
        # moved elsewhere, with its file
        moved = os.path.join(self.tmp, "moved")
        shutil.copytree(os.path.join(self.demos, "clan"), moved, copy_function=shutil.copy2)
        self.settings.setValue("demos", moved)
        third = self.window()
        self.assertTrue(wait_until(lambda: "war" in third.library.demos))
        self.assertEqual(self.indexing_runs(third), 0)

    def test_folder_that_cant_be_written(self):
        """Its indexes are kept in the cache folder instead, also shared."""
        locked = os.path.join(self.tmp, "locked")
        os.makedirs(locked)
        shutil.copyfile(os.path.join(self.demos, "first.dm3"), os.path.join(locked, "first.dm3"))
        os.chmod(locked, 0o555)
        self.addCleanup(os.chmod, locked, 0o755)
        self.settings.setValue("demos", locked)
        window = self.window()
        self.assertTrue(wait_until(lambda: "first" in window.library.demos and not window.library.progress(), 20))
        self.assertEqual(os.listdir(locked), ["first.dm3"])
        self.assertEqual(window.library.index("first")["duration"], 600000)
        window.close()
        again = self.window()
        self.assertTrue(wait_until(lambda: "first" in again.library.demos))
        self.assertEqual(self.indexing_runs(again), 0)

    def test_old_indexes_moved(self):
        """The indexes kept in the cache folder before, a file for each demo
        in a folder for each folder opened, move next to the demos and aren't
        made again; those of other folders opened go, as they're the same."""
        os.makedirs(os.path.join(self.demos, "clan"))
        self.add_demo("clan/war", DEMO)
        future = time.time() + 5
        for name in ("first", "other", "clan/war"):
            os.utime(os.path.join(self.demos, name + ".dm3"), (future - 100, future - 100))
        os.utime(os.path.join(self.bin, "mohdemoindex"), (future - 100, future - 100), follow_symlinks=False)
        cache = os.path.join(self.tmp, "cache", "mohdemo-replay", "index")
        old = []
        for folder, name in ((self.demos, "first"), (self.demos, "other"), (self.demos, "clan/war"),
                             (os.path.join(self.demos, "clan"), "war")):
            path = os.path.join(cache, mohreplay.folder_key(folder), name + ".json")
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w") as f:
                json.dump(dict(DEMO, duration=123000), f, indent=2)
            os.utime(path, (future, future))
            old.append(path)
        window = self.window()
        self.assertTrue(wait_until(lambda: {"first", "other", "clan/war"} <= set(window.library.demos)))
        self.assertEqual(self.indexing_runs(window), 1)  # broken, tried once more
        self.assertEqual(window.library.demos["clan/war"]["duration"], 123000)
        self.assertEqual(window.library.index("first")["duration"], 123000)
        self.assertEqual([os.path.exists(path) for path in old], [False] * 4)
        self.assertEqual(len(self.lines(os.path.join(self.demos, "clan"))), 1)
        self.assertTrue(wait_until(lambda: not window.library.progress() and not window.library.work, 20))
        self.assertEqual(os.listdir(cache), [])

    def test_index_file_mended_and_tidied(self):
        """A line cut short doesn't count, and what's after it does; when lines
        replaced are most of the file, it's rewritten."""
        store = mohreplay.DemoIndexes(self.demos, os.path.join(self.tmp, "fallback"))
        self.assertEqual(store.kept, {})
        self.assertTrue(store.add("a", 5.0, b'{"x": 1,\n "y": 2}'))
        self.assertTrue(store.add("b", 6.0, b'{"x": 2}'))
        path = os.path.join(self.demos, mohreplay.INDEX_FILE)
        with open(path, "ab") as f:
            f.write(b'{"demo": "c", "made": 7}\t{"x"')
        store = mohreplay.DemoIndexes(self.demos, os.path.join(self.tmp, "fallback"))
        self.assertEqual((store.index("a"), store.index("b"), store.index("c")), ({"x": 1, "y": 2}, {"x": 2}, None))
        self.assertEqual((store.made("a"), store.made("c")), (5.0, None))
        self.assertTrue(store.cut_short)
        store.add("c", 8.0, b'{"x": 3}')
        store = mohreplay.DemoIndexes(self.demos, os.path.join(self.tmp, "fallback"))
        self.assertEqual(store.index("c"), {"x": 3})
        # the last one counts
        for n in range(10):
            store.add("c", 9.0 + n, b'{"x": %d, "padding": "%s"}' % (n, b"." * 50))
        self.assertEqual(store.index("c")["x"], 9)
        before = os.path.getsize(path)
        store.tidy()
        self.assertLess(os.path.getsize(path), before / 2)
        self.assertEqual((store.index("a"), store.index("b"), store.index("c")["x"], store.made("c")),
                         ({"x": 1, "y": 2}, {"x": 2}, 9, 18.0))
        # another window rewrote it: read again
        other = mohreplay.DemoIndexes(self.demos, os.path.join(self.tmp, "fallback"))
        with open(path, "rb") as f:
            lines = f.readlines()
        with open(path, "wb") as f:
            f.writelines(reversed(lines))
        self.assertEqual((other.index("a"), other.index("c")["x"]), ({"x": 1, "y": 2}, 9))

    def test_without_mohdemoindex(self):
        os.remove(os.path.join(self.bin, "mohdemoindex"))
        window = self.window()
        wait_until(lambda: False, 0.5)
        self.assertEqual(window.library.demos, {})
        self.assertIsNone(window.library.progress())
        self.assertEqual(self.shown(window), ["broken", "first", "other"])

    def test_filter_by_player(self):
        window = self.window()
        self.indexed(window)
        self.assertTrue(window.demos.isColumnHidden(mohreplay.COL_PLAYER))
        window.filter.setText("PHIL")
        self.assertEqual(self.shown(window), ["first", "other"])
        self.assertFalse(window.demos.isColumnHidden(mohreplay.COL_PLAYER))
        self.assertEqual(window.demo_items["first"].text(mohreplay.COL_PLAYER), "^1Phil (0)")
        window.filter.setText("kos")
        self.assertEqual(self.shown(window), ["other"])
        self.assertEqual(window.demo_items["other"].text(mohreplay.COL_PLAYER), "<KoS>Bob (3)")
        window.filter.setText("mohdm6 bob")
        self.assertEqual(self.shown(window), ["other"])
        window.filter.setText("obj_team1")
        self.assertEqual(self.shown(window), ["first"])
        self.assertTrue(window.demos.isColumnHidden(mohreplay.COL_PLAYER))
        window.filter.setText("nobody")
        self.assertEqual(self.shown(window), [])
        # commas separate searches: the demos of either
        window.filter.setText("obj_team1, kos")
        self.assertEqual(self.shown(window), ["first", "other"])
        self.assertEqual(window.demo_items["first"].text(mohreplay.COL_PLAYER), "")
        self.assertEqual(window.demo_items["other"].text(mohreplay.COL_PLAYER), "<KoS>Bob (3)")
        window.filter.setText("mohdm6 phil, nobody")
        self.assertEqual(self.shown(window), ["other"])
        window.filter.setText("mohdm6 kos, team1 al")
        self.assertEqual(self.shown(window), ["first", "other"])
        self.assertEqual(window.demo_items["first"].text(mohreplay.COL_PLAYER), "Al (0)")
        self.assertEqual(window.demo_items["other"].text(mohreplay.COL_PLAYER), "<KoS>Bob (3)")

    def test_filter_by_rules(self):
        mixed = json.loads(json.dumps(DEMO))
        mixed["maps"] = [{"time": 0, "map": "obj/obj_team1", "rules": "default"},
                         {"time": 300000, "map": "obj/obj_team2", "rules": "realism"},
                         {"time": 590000, "map": "obj/obj_team4", "rules": ""}]
        self.add_demo("mixed", mixed)
        window = self.window()
        self.indexed(window, ("first", "other", "mixed"))
        self.assertEqual(window.demo_items["mixed"].text(mohreplay.COL_RULES), "Both")
        self.assertEqual(self.shown(window), ["broken", "first", "mixed", "other"])
        window.rules.setCurrentIndex(window.rules.findData("realism"))
        self.assertEqual(self.shown(window), ["mixed", "other"])
        window.filter.setText("kos")
        self.assertEqual(self.shown(window), ["other"])
        window.filter.setText("")
        window.rules.setCurrentIndex(window.rules.findData("default"))
        self.assertEqual(self.shown(window), ["mixed"])
        # kept for next time
        self.assertEqual(self.settings.value("rules"), "default")
        again = self.window()
        self.assertEqual(again.rules.currentData(), "default")

    def test_old_indexes(self):
        # indexes made before the rules were: no rules, as unknown
        self.assertEqual(mohreplay.summarize(DEMO)["rules"], "")

    def test_kill_filter(self):
        """The demos with kills the filter keeps, with their killers and how
        many each."""
        window = self.window()
        self.indexed(window)
        self.choose(window, killer="kos")
        self.assertEqual(self.shown(window), ["other"])
        self.assertFalse(window.demos.isColumnHidden(mohreplay.COL_PLAYER))
        self.assertEqual(window.demo_items["other"].text(mohreplay.COL_PLAYER), "<KoS>Bob (3)")
        # Phil's kill at 3:20 is while t- is shown: not his
        self.choose(window, killer="phil")
        self.assertEqual(self.shown(window), [])
        self.choose(window, killer="t-, bob")
        self.assertEqual(self.shown(window), ["first", "other"])
        self.assertEqual(window.demo_items["first"].text(mohreplay.COL_PLAYER), "t- (3)")
        self.choose(window, killer="", victim="phil")
        self.assertEqual(window.demo_items["first"].text(mohreplay.COL_PLAYER), "t- (1)")
        self.assertEqual(window.demo_items["other"].text(mohreplay.COL_PLAYER), "<KoS>Bob (1)")
        self.choose(window, victim="", weapons=["rifle", "other"])
        # Bob's suicide counts, but has no killer to tell
        self.assertEqual(window.demo_items["first"].text(mohreplay.COL_PLAYER), "t- (3)")
        self.choose(window, weapons=["mg"])
        self.assertEqual(self.shown(window), [])
        # whoever is shown
        self.choose(window, weapons=[], recorder=True, specced=True)
        self.assertEqual(self.shown(window), ["first", "other"])
        # with the search's words
        window.filter.setText("mohdm6")
        self.assertEqual(self.shown(window), ["other"])
        window.filter.setText("")
        # on the obj maps: other is on a dm one
        self.choose(window, obj=True)
        self.assertEqual(self.shown(window), ["first"])
        self.choose(window, obj=False, recorder=False, specced=False)
        self.assertEqual(self.shown(window), ["broken", "first", "other"])
        self.assertTrue(window.demos.isColumnHidden(mohreplay.COL_PLAYER))

    def test_great_frags(self):
        """Great frags, in the list: snipes has two slow snipes of t-'s 3
        frags, 10 s apart."""
        snipes = json.loads(json.dumps(DEMO))
        for kill, time, scope in zip(snipes["kills"], (10000, 20000, 30000), (1500, 2500, 400)):
            kill.update(time=time, weapon="sniper", scope=scope, speed=0)
        self.add_demo("snipes", snipes)
        window = self.window()
        self.indexed(window, ("first", "other", "snipes"))
        self.choose(window, killer="t-")
        self.assertEqual(window.demo_items["snipes"].text(mohreplay.COL_PLAYER), "t- (3)")
        self.choose(window, great=True)
        self.assertEqual(self.shown(window), ["first", "snipes"])
        self.assertEqual(window.demo_items["snipes"].text(mohreplay.COL_PLAYER), "t- (1)")
        self.assertEqual(window.demo_items["first"].text(mohreplay.COL_PLAYER), "t- (3)")
        self.choose(window, weapons=["sniper"])
        self.assertEqual(self.shown(window), ["snipes"])

    def test_from_buttons(self):
        """Any of them together, in the list of demos, kept for next time."""
        self.add_demo("followed", followed_demo())
        window = self.window()
        self.indexed(window, ("first", "other", "followed"))
        self.assertEqual([window.from_buttons[key].text() for key in ("recorder", "specced", "unshown")],
                         ["Recorder", "Specced", "Unspecced"])
        self.assertFalse(any(button.isChecked() for button in window.from_buttons.values()))
        self.assertFalse(window.kill_filter())
        self.choose(window, recorder=True)
        self.assertEqual(self.shown(window), ["first", "followed", "other"])
        self.assertEqual(window.demo_items["followed"].text(mohreplay.COL_PLAYER), "t- (3)")
        self.choose(window, recorder=False, specced=True)
        self.assertEqual(self.shown(window), ["followed"])
        self.choose(window, recorder=True)
        self.assertEqual(sorted(window.demo_items["followed"].text(mohreplay.COL_PLAYER).split(", ")),
                         ["^1Phil (2)", "t- (3)"])
        again = self.window()
        self.assertEqual([again.from_buttons[key].isChecked() for key in ("recorder", "specced", "unshown")],
                         [True, True, False])
        self.assertEqual(again.kill_filter().commands(), ["demofilter clear", "demofilter recorder", "demofilter specced"])
        # applied once a click has stopped, as the other filters are
        again.from_buttons["recorder"].click()
        self.assertTrue(again.kill_filter_timer.isActive())
        self.assertEqual(self.settings.value("from_recorder"), "0")

    def test_specced_frags(self):
        """Specced frags, in the list: Phil's two in followed, while the
        recorder follows him."""
        self.add_demo("followed", followed_demo())
        window = self.window()
        self.indexed(window, ("first", "other", "followed"))
        self.choose(window, specced=True)
        self.assertEqual(self.shown(window), ["followed"])
        self.assertEqual(window.demo_items["followed"].text(mohreplay.COL_PLAYER), "^1Phil (2)")

    def test_filter_fields(self):
        window = self.window()
        self.indexed(window)
        # applied once typing stops
        window.killer.setText("ph")
        window.killer.setText("phil")
        self.assertEqual(self.shown(window), ["broken", "first", "other"])
        self.assertTrue(wait_until(lambda: self.shown(window) == [], 2))
        window.killer.setText("")
        self.assertTrue(wait_until(lambda: self.shown(window) == ["broken", "first", "other"], 2))
        # the menu adds and takes off the players of the demos listed, those
        # with the most frags first, a click on one at a time, staying open
        window.killer.open_menu()
        everyone = ["<KoS>Bob (3)", "t- (3)", "^1Phil (0)", "Al (0)", "Bob (0)"]
        self.assertEqual(self.players(window.killer), (everyone, []))
        self.tick(window.killer, "t- (3)")
        self.assertEqual(window.killer.text(), "t-")
        self.tick(window.killer, "^1Phil (0)")
        self.assertEqual(window.killer.text(), "t-, ^1Phil")
        self.tick(window.killer, "^1Phil (0)")
        self.assertEqual(window.killer.text(), "t-")
        self.assertEqual(self.players(window.killer)[1], ["t- (3)"])
        self.assertTrue(window.killer.menu.isVisible())
        window.killer.menu.close()
        # only those of the demos listed then
        self.assertTrue(wait_until(lambda: self.shown(window) == ["first"], 2))
        window.victim.open_menu()
        self.assertEqual(self.players(window.victim), (["t- (3)", "^1Phil (0)", "Al (0)", "Bob (0)"], []))
        self.assertEqual(len(window.victim.menu.actions()), 1)
        window.victim.menu.close()
        # a long list scrolls instead of leaving the screen
        window.victim.fill = lambda: ([f"player {n}" for n in range(100)], {})
        window.victim.open_menu()
        self.assertEqual(window.victim.list.height(), mohreplay.NamesField.ROWS * window.victim.list.sizeHintForRow(0))
        self.assertLess(window.victim.menu.sizeHint().height(), 600)
        window.victim.menu.close()
        # no demos indexed: nothing to choose
        window.victim.fill = lambda: ([], {})
        window.victim.open_menu()
        self.assertEqual([a.text() for a in window.victim.menu.actions()], ["No demos indexed yet"])
        window.victim.menu.close()

    def test_filter_by_recorder(self):
        bobs = json.loads(json.dumps(DEMO))
        bobs["recorder"] = {"client": 0, "name": "<KoS>Bob"}
        os.makedirs(os.path.join(self.demos, "clan"))
        self.add_demo("clan/bobs", bobs)
        window = self.window()
        self.indexed(window, ("first", "other", "clan/bobs"))
        # those who recorded the most first, in the menu
        self.assertEqual(window.recorded_by.players, ["t-", "<KoS>Bob"])
        window.recorded_by.open_menu()
        self.assertEqual(self.players(window.recorded_by), (["t- (2)", "<KoS>Bob (1)"], []))
        self.tick(window.recorded_by, "<KoS>Bob (1)")
        window.recorded_by.menu.close()
        self.assertEqual(window.recorded_by.text(), "<KoS>Bob")
        self.assertEqual(self.shown(window), ["clan/bobs"])
        # several, by parts of their names; a demo not indexed has no recorder
        window.recorded_by.setText("kos, T-")
        self.assertEqual(self.shown(window), ["clan/bobs", "first", "other"])
        # with the search and the frag filters
        window.filter.setText("mohdm6")
        self.assertEqual(self.shown(window), ["other"])
        window.filter.setText("")
        self.choose(window, killer="kos")
        self.assertEqual(self.shown(window), ["other"])
        window.recorded_by.setText("bob")
        self.assertEqual(self.shown(window), [])
        self.choose(window, killer="")
        self.assertEqual(self.shown(window), ["clan/bobs"])
        window.recorded_by.setText("")
        self.assertEqual(self.shown(window), ["broken", "clan/bobs", "first", "other"])

    def test_short_demos_hidden(self):
        self.add_demo("short", dict(DEMO, duration=mohreplay.MIN_LENGTH - 1))
        self.add_demo("long enough", dict(DEMO, duration=mohreplay.MIN_LENGTH))
        window = self.window()
        # shown until their length is known
        self.assertIn("short", self.shown(window))
        self.indexed(window, ("first", "other", "short", "long enough"))
        self.assertEqual(window.demo_items["short"].text(mohreplay.COL_LENGTH), "4:59")
        self.assertEqual(self.shown(window), ["broken", "first", "long enough", "other"])
        # whatever the filter
        window.filter.setText("t-")
        self.assertNotIn("short", self.shown(window))

    def test_short_demos_box(self):
        self.add_demo("short", dict(DEMO, duration=mohreplay.MIN_LENGTH - 1))
        self.add_demo("no frags", dict(DEMO, duration=mohreplay.MIN_LENGTH - 1, kills=[]))
        self.add_demo("cant be read", {"maps": [], "duration": 0, "kills": []})
        window = self.window()
        names = ("first", "other", "short", "no frags", "cant be read")
        self.indexed(window, names)
        self.assertEqual(self.shown(window), ["broken", "first", "other"])
        self.assertIn("3 shorter than 5 minutes aren't listed", window.demo_count.toolTip())

        # those with frags too, not the others
        window.short_demos.setChecked(True)
        self.assertEqual(self.shown(window), ["broken", "first", "other", "short"])
        self.assertIn("2 shorter than 5 minutes with no frags aren't listed", window.demo_count.toolTip())
        self.assertEqual(window.record_button.text(), "Record 4 demos…")
        # the other filters still apply
        window.filter.setText("short")
        self.assertEqual(self.shown(window), ["short"])
        window.filter.setText("no frags")
        self.assertEqual(self.shown(window), [])
        window.filter.setText("")

        # remembered
        again = self.window()
        self.indexed(again, names)
        self.assertTrue(again.short_demos.isChecked())
        self.assertEqual(self.shown(again), ["broken", "first", "other", "short"])

        again.short_demos.setChecked(False)
        self.assertEqual(self.shown(again), ["broken", "first", "other"])
        self.assertEqual(self.settings.value("short_demos"), "0")

    def test_sort_by_length(self):
        window = self.window()
        self.indexed(window)
        window.demos.sortByColumn(mohreplay.COL_LENGTH, Qt.AscendingOrder)
        order = [window.demos.topLevelItem(i).text(mohreplay.COL_DEMO) for i in range(3)]
        self.assertEqual(order, ["broken", "other", "first"])


class TestRecording(FakeGameCase):
    def setUp(self):
        super().setUp()
        os.symlink(FAKEGAME, os.path.join(self.bin, "ffmpeg"))
        self.settings.setValue("rec/ffmpeg", os.path.join(self.bin, "ffmpeg"))

    def ready(self, window, *names):
        """Those demos indexed and selected, first by default."""
        names = names or ("first",)
        TestLibrary.indexed(self, window, names)
        window.demos.clearSelection()
        for name in names:
            window.demo_items[name].setSelected(True)

    def record(self, window, setup=None, action=None):
        """Records with the Record dialog, as if its button were pressed."""
        dialogs = []

        def exec_dialog(dialog):
            dialogs.append(dialog)
            if setup:
                setup(dialog)
            dialog.accept()
            return dialog.result()
        with mock.patch.object(mohreplay.RecordDialog, "exec", exec_dialog):
            (action or window.record_button.click)()
        return dialogs[0] if dialogs else None

    def video(self, name):
        with open(os.path.join(self.videos, name)) as f:
            return json.load(f)

    def done(self, window, count=1, timeout=20):
        """Waits for the jobs to be over, done or not."""
        self.assertTrue(wait_until(lambda: len(window.jobs) >= count and all(
            j["status"] in ("done", "failed", "canceled") for j in window.jobs), timeout), "not recorded")
        return window.jobs

    def test_record_a_stretch(self):
        window = self.window()
        self.ready(window)

        def setup(dialog):
            dialog.range.setChecked(True)
            dialog.start.setText("1:00")
            dialog.end.setText("1:10")
            dialog.pattern.setText("{demo} {start}")
        self.record(window, setup)
        job, = self.done(window)
        self.assertEqual(job["status"], "done")
        self.assertEqual(job["item"].text(2), "Done")
        self.assertEqual(job["item"].text(1), "first, 1:00 to 1:10")
        video = self.video("first 1-00.mp4")
        self.assertEqual((video["start"], video["end"]), (60000, 70000))
        self.assertGreaterEqual(video["stop"], 70000)
        cvars = video["cvars"]
        self.assertEqual(cvars["s_loopback"], "1")
        self.assertEqual(cvars["cl_aviFrameRate"], "60")
        self.assertEqual((cvars["r_customwidth"], cvars["r_customheight"]), ("1920", "1080"))
        self.assertIn("-crf 20", cvars["cl_aviPipeFormat"])
        self.assertNotIn("r_motionBlur", cvars)
        self.assertIn("Recorded", window.message.text())
        self.assertEqual(window.message.toolTip(), window.message.text())
        self.assertIsNone(window.recorder)

    def test_record_a_demo_in_a_folder(self):
        os.makedirs(os.path.join(self.demos, "clan", "war 2"))
        self.add_demo("clan/war 2/first", DEMO)
        window = self.window()
        self.ready(window, "clan/war 2/first")

        def setup(dialog):
            dialog.range.setChecked(True)
            dialog.start.setText("1:00")
            dialog.end.setText("1:10")
            dialog.pattern.setText("{demo} {start}")
        self.record(window, setup)
        job, = self.done(window)
        self.assertEqual(job["status"], "done")
        self.assertEqual(job["item"].text(1), "clan/war 2/first, 1:00 to 1:10")
        # named after the demo's file
        video = self.video("first 1-00.mp4")
        self.assertEqual((video["start"], video["end"]), (60000, 70000))

    def test_record_in_parts(self):
        """A video longer than the parts' length is recorded in parts, each
        with its list of clips, the numbers going on."""
        window = self.window()
        self.ready(window)

        def setup(dialog):
            dialog.range.setChecked(True)
            dialog.start.setText("1:00")
            dialog.end.setText("3:30")
            dialog.pattern.setText("{demo} {start}")
            dialog.numbers.setChecked(True)
            self.assertFalse(dialog.split_minutes.isEnabled())
            dialog.split.setChecked(True)
            dialog.split_minutes.setValue(1)
        self.record(window, setup)
        jobs = self.done(window, 3, timeout=40)
        self.assertEqual([job["status"] for job in jobs], ["done"] * 3)
        self.assertEqual([job["item"].text(1) for job in jobs],
                         [f"first, 1:00 to 3:30 (part {n} of 3)" for n in (1, 2, 3)])
        videos = [self.video(f"first 1-00 part {n}.mp4") for n in (1, 2, 3)]
        self.assertEqual([(v["start"], v["end"]) for v in videos], [(60000, 120000), (120000, 180000), (180000, 210000)])
        self.assertFalse(os.path.exists(os.path.join(self.videos, "first 1-00.mp4")))
        # the stretch goes on: the same clip number in each part
        for n, start in ((1, "1:00"), (3, "3:00")):
            with open(os.path.join(self.videos, f"first 1-00 part {n}.csv"), encoding="utf-8") as f:
                self.assertEqual(f.read().splitlines()[1], f"1,0:00,first,{start},,,")
        self.assertEqual((self.settings.value("rec/split"), self.settings.value("rec/split_minutes")), ("true", "1"))
        # not split when short enough
        self.record(window, lambda dialog: (dialog.range.setChecked(True), dialog.start.setText("4:00"), dialog.end.setText("4:30"),
                                            dialog.pattern.setText("short")))
        self.done(window, 4)
        self.assertEqual(self.video("short.mp4")["end"], 270000)

    def change_video(self, dialog, change):
        """Changes the video settings with the Record dialog's Change…, on
        Settings' Video tab."""
        def exec_dialog(settings):
            self.assertEqual(settings.tabs.tabText(settings.tabs.currentIndex()), "Video")
            change(settings.videos)
            settings.accept()
            return settings.result()
        with mock.patch.object(mohreplay.SettingsDialog, "exec", exec_dialog):
            dialog.change_video()

    def test_motion_blur(self):
        window = self.window()
        self.ready(window)

        def setup(dialog):
            self.assertEqual(dialog.summary.text(), "1920x1080, 60 fps, H.264 (CRF 20), sound 192k, words censored")
            self.change_video(dialog, lambda videos: (self.assertFalse(videos.blur.isChecked()),
                                                      videos.blur.setChecked(True)))
            self.assertEqual(dialog.summary.text(),
                             "1920x1080, 60 fps with motion blur, H.264 (CRF 20), sound 192k, words censored")
            dialog.pattern.setText("blur")
        self.record(window, setup)
        self.done(window)
        cvars = self.video("blur.mp4")["cvars"]
        # the renderer's, over all of each frame's time, the colors unchanged
        self.assertEqual([cvars[name] for name in ("r_temporal", "r_motionBlur", "r_motionBlurShutter",
                                                   "r_motionBlurFps", "r_saturation")], ["1", "1", "1", "0", "1"])
        self.assertEqual(cvars["cl_aviFrameRate"], "60")
        self.assertTrue(cvars["cl_aviPipeFormat"].startswith("-c:v libx264 "))

        # kept, and the same in Settings
        self.settings.setValue("rec/fps", "30")
        self.settings.setValue("rec/advanced", "-vf scale=640:-2 -c:v libx264")

        def advanced(dialog):
            self.assertIn("with motion blur, FFmpeg options of your own", dialog.summary.text())
            dialog.pattern.setText("advanced")
        self.record(window, advanced)
        self.done(window, 2)
        cvars = self.video("advanced.mp4")["cvars"]
        self.assertEqual((cvars["r_motionBlur"], cvars["cl_aviFrameRate"]), ("1", "30"))
        self.assertEqual(cvars["cl_aviPipeFormat"], "-vf scale=640:-2 -c:v libx264")

    def test_record_button(self):
        """Record is for the demos selected, those in the folders selected,
        else all those listed, as they're listed; a stretch only of one."""
        self.add_demo("second", DEMO)
        os.makedirs(os.path.join(self.demos, "clan"))
        self.add_demo("clan/third", DEMO)
        self.add_demo("clan/fourth", dict(DEMO, kills=[]))
        window = self.window()
        TestLibrary.indexed(self, window, ("first", "second", "clan/third", "clan/fourth"))
        self.assertEqual(window.record_button.text(), "Record 4 demos…")
        root = window.demos.invisibleRootItem()
        shown = window.demos_of(root.child(i) for i in range(root.childCount()))
        self.assertEqual(window.chosen_demos(), shown)
        dialogs = []
        with mock.patch.object(mohreplay.RecordDialog, "exec", lambda dialog: dialogs.append(dialog) or 0):
            window.record_button.click()
            self.assertIsNone(dialogs[-1].range)
            self.assertTrue(dialogs[-1].join.isVisibleTo(dialogs[-1]))
            window.demo_items["first"].setSelected(True)
            self.assertEqual(window.record_button.text(), "Record 1 demo…")
            window.record_button.click()
            self.assertIsNotNone(dialogs[-1].range)
        self.assertEqual(window.jobs, [])
        # a folder's are its demos; the filter's only those it lists
        window.folder_items["clan"].setSelected(True)
        self.assertEqual(window.record_button.text(), "Record 3 demos…")
        self.choose(window, killer="t-")
        self.assertEqual(window.record_button.text(), "Record 2 demos…")
        window.demos.clearSelection()
        self.assertEqual(window.record_button.text(), "Record 3 demos…")
        # nothing listed, nothing to record
        window.filter.setText("nobody")
        self.assertEqual(window.record_button.text(), "Record…")
        self.assertFalse(window.record_button.isEnabled())

    def test_record_kills_of_a_player_without_sound(self):
        window = self.window()
        self.ready(window)
        self.choose(window, killer="t-")

        self.settings.setValue("rec/sound", "false")

        def setup(dialog):
            dialog.choices["kills"].setChecked(True)
            dialog.pattern.setText("{player} frags")
        dialog = self.record(window, setup)
        self.assertEqual(dialog.choices["kills"].text(), "The 3 frags listed, under a minute")
        self.assertFalse(dialog.join.isVisible())
        self.done(window)
        video = self.video("t- frags.mp4")
        self.assertEqual((video["only"], video["player"]), ("kills", ""))
        self.assertEqual(video["filter"], {"shown": False, "killers": ["t-"], "victims": [], "weapons": []})
        self.assertEqual(video["cvars"]["s_loopback"], "0")
        self.assertIn("-an", video["cvars"]["cl_aviPipeFormat"])

    def test_lead_in_and_out_not_a_choice(self):
        """Each frag plays 2 s around the fight it ends, whatever a lead kept
        from when it was a choice says."""
        window = self.window()
        self.ready(window)
        self.choose(window, killer="t-")
        self.settings.setValue("rec/kill_before", "30.0")
        self.settings.setValue("rec/kill_after", "30.0")

        def setup(dialog):
            self.assertFalse(hasattr(dialog, "kill_before") or hasattr(dialog, "kill_after"))
            self.assertEqual(dialog.choices["kills"].text(), "The 3 frags listed, under a minute")
            dialog.choices["kills"].setChecked(True)
            dialog.pattern.setText("lead")
        self.record(window, setup)
        job, = self.done(window)
        self.assertNotIn("kill_before", job)
        video = self.video("lead.mp4")
        self.assertEqual((video["start"], video["cvars"]["cl_demoKillBefore"], video["cvars"]["cl_demoKillAfter"]),
                         (8000, "2.0", "2.0"))

    def test_record_recorder_frags(self):
        window = self.window()
        self.ready(window)
        self.choose(window, recorder=True)

        def setup(dialog):
            dialog.choices["kills"].setChecked(True)
            dialog.pattern.setText("{demo} recfrags")
        dialog = self.record(window, setup)
        self.assertEqual(list(dialog.choices), ["kills", "everything"])
        self.assertEqual(dialog.choices["kills"].text(), "The 3 frags listed, under a minute")
        self.assertFalse(dialog.choices["kills"].isHidden())
        self.assertTrue(dialog.nothing.isHidden())
        job, = self.done(window)
        self.assertEqual(job["item"].text(1), "first, frags from the recorder")
        video = self.video("first recfrags.mp4")
        # from just before the first kill, at 10 s
        self.assertEqual((video["start"], video["only"], video["player"], video["filter"].get("recorder")),
                         (8000, "kills", "", True))

    def test_record_kills_with_a_weapon(self):
        window = self.window()
        self.ready(window)
        self.choose(window, victim="bob, phil", weapons=["rifle"])

        def setup(dialog):
            dialog.pattern.setText("rifle")
        dialog = self.record(window, setup)
        self.assertEqual(dialog.choices["kills"].text(), "The 2 frags listed, under a minute")
        job, = self.done(window)
        self.assertEqual(job["item"].text(1), "first, frags of bob or phil with rifle")
        video = self.video("rifle.mp4")
        self.assertEqual((video["start"], video["only"], video["player"]), (8000, "kills", ""))
        self.assertEqual(video["filter"], {"shown": False, "killers": [], "victims": ["bob", "phil"], "weapons": ["rifle"]})

    def test_record_nothing_listed(self):
        """Only what there is to record is offered."""
        self.add_demo("second", DEMO)
        window = self.window()
        self.ready(window, "first", "second")
        self.choose(window, victim="nobody")
        self.assertEqual(window.chosen_demos(), [])
        self.assertFalse(window.record_button.isEnabled())
        self.choose(window, victim="")
        self.choose(window, killer="t-")
        dialogs = []
        with mock.patch.object(mohreplay.RecordDialog, "exec", lambda dialog: dialogs.append(dialog) or 0):
            self.choose(window, victim="al")
            window.record_button.click()
        self.assertEqual(dialogs[-1].choices["kills"].text(), "The 2 frags listed, in 2 demos, under a minute")
        self.assertTrue(dialogs[-1].choices["kills"].isChecked())

    def test_record_unspecced_frags(self):
        """Unspecced frags: the game told to play them."""
        self.short_demos()
        self.add_demo("unspecced", unspecced_demo())
        window = self.window()
        self.ready(window, "unspecced")
        self.choose(window, killer="ace", unshown=True)

        def setup(dialog):
            dialog.choices["kills"].setChecked(True)
            dialog.pattern.setText("unspecced")
        self.record(window, setup)
        job, = self.done(window)
        self.assertEqual(job["status"], "done")
        self.assertEqual([[kill["time"] for kill in frags] for frags in job["clips"][0]["frags"]], [[20000], [89000]])
        video = self.video("unspecced.mp4")
        self.assertEqual((video["only"], video["filter"].get("unshown")), ("kills", "only"))

    def test_record_specced_and_unspecced(self):
        """Specced and Unspecced together: the game is told both."""
        self.short_demos()
        self.add_demo("unspecced", unspecced_demo())
        window = self.window()
        self.ready(window, "unspecced")
        self.choose(window, specced=True, unshown=True)

        def setup(dialog):
            self.assertEqual(dialog.choices["kills"].text(), "The 4 frags listed, under a minute")
            dialog.choices["kills"].setChecked(True)
            dialog.pattern.setText("both")
        self.record(window, setup)
        job, = self.done(window)
        self.assertEqual(job["status"], "done")
        self.assertEqual(job["item"].text(1), "unspecced, frags from specced and unspecced")
        self.assertEqual([[kill["time"] for kill in frags] for frags in job["clips"][0]["frags"]],
                         [[20000], [32000], [70000], [89000]])
        video = self.video("both.mp4")
        self.assertEqual((video["only"], video["filter"].get("specced"), video["filter"].get("unshown")),
                         ("kills", True, True))

    def test_record_specced_frags(self):
        """Specced: those of the players followed, not the recorder's."""
        self.add_demo("followed", followed_demo())
        window = self.window()
        self.ready(window, "followed")
        self.choose(window, specced=True)

        def setup(dialog):
            dialog.choices["kills"].setChecked(True)
            dialog.pattern.setText("specced")
        dialog = self.record(window, setup)
        self.assertEqual(dialog.choices["kills"].text(), "The 2 frags listed, under a minute")
        job, = self.done(window)
        self.assertEqual(job["item"].text(1), "followed, frags from specced")
        video = self.video("specced.mp4")
        self.assertEqual((video["only"], video["filter"].get("specced")), ("kills", True))

    def test_last_choice_remembered(self):
        self.short_demos()
        self.add_demo("first", dict(DEMO, duration=20000))
        window = self.window()
        self.ready(window)
        self.record(window, lambda d: (d.choices["everything"].setChecked(True), d.pattern.setText("a")))
        self.done(window)
        # offered: chosen again, else the first
        dialog = self.record(window, lambda d: d.pattern.setText("b"))
        self.assertTrue(dialog.choices["everything"].isChecked())
        self.done(window, 2)
        self.assertEqual(self.video("b.mp4")["only"], "")
        # not offered any more, as nothing's there to record: a stretch
        with mock.patch.object(self.settings, "value", lambda key, default=None, s=self.settings.value: (
                "kills" if key == "rec/what" else s(key, default))):
            dialog = mohreplay.RecordDialog(self.settings, [("kills", "No frags", False)], 0, parent=window)
        self.assertTrue(dialog.range.isChecked())

    def test_progress(self):
        window = self.window()
        job = {"clips": [{"spans": [(0, 10000)]}, {"spans": [(5000, 15000), (20000, 30000)]}]}
        recorder = mohreplay.Recorder(window, job, None, None)
        recorder.clip = 1
        with mock.patch.object(recorder.elapsed, "elapsed", lambda: 30000):
            # 10 s of the first clip and 7 of the second, in 30 s
            self.assertEqual(recorder.how_far(12000), "56%, under a minute left")
            self.assertEqual(recorder.how_far(5000), "33%, about 1 min left")
        job["clips"][0]["spans"] = None
        self.assertEqual(recorder.how_far(12000), "0:12")

    def test_no_overwrite(self):
        window = self.window()
        self.ready(window)
        os.makedirs(self.videos)
        with open(os.path.join(self.videos, "clip.mp4"), "w") as f:
            f.write("mine")

        def setup(dialog):
            dialog.pattern.setText("clip")
        self.record(window, setup)
        job, = self.done(window)
        self.assertEqual(job["output"], os.path.join(self.videos, "clip (2).mp4"))
        self.assertEqual(job["item"].text(0), "clip (2).mp4")
        with open(os.path.join(self.videos, "clip.mp4")) as f:
            self.assertEqual(f.read(), "mine")

    def test_ffmpeg_failing(self):
        window = self.window()
        self.ready(window)
        with mock.patch.dict(os.environ, {"FAKEGAME_FFMPEG_FAIL": "1"}):
            self.record(window)
            job, = self.done(window)
        self.assertEqual(job["status"], "failed")
        self.assertTrue(job["item"].text(2).startswith("Failed: "))
        self.assertIn("no FFmpeg", job["item"].toolTip(2))
        self.assertIn("Couldn't record", window.message.text())
        self.assertIsNone(window.recorder)

    def test_ffmpeg_quitting_midway(self):
        window = self.window()
        self.ready(window)
        with mock.patch.dict(os.environ, {"FAKEGAME_FFMPEG_FAIL": "mid", "FAKEGAME_SPEED": "1"}):
            self.record(window)
            job, = self.done(window)
        text = job["item"].toolTip(2)
        self.assertTrue(text.startswith("The recording stopped. Couldn't write"), text)
        self.assertIn("FFmpeg quit", text)
        # half a video isn't kept
        self.assertFalse(os.path.exists(self.videos) and os.listdir(self.videos))

    def test_bad_times(self):
        window = self.window()
        self.ready(window)

        def setup(dialog):
            dialog.range.setChecked(True)
            dialog.start.setText("2:00")
            dialog.end.setText("1:00")
        self.record(window, setup)
        self.assertIn("aren't right", self.messages.shown[-1][1])
        self.assertEqual(window.jobs, [])

    def test_queue(self):
        window = self.window()
        self.ready(window)
        for start in ("1:00", "2:00", "3:00"):
            def setup(dialog, start=start):
                dialog.range.setChecked(True)
                dialog.start.setText(start)
                dialog.end.setText(start.replace(":00", ":05"))
            self.record(window, setup)
        statuses = [j["status"] for j in window.jobs]
        self.assertEqual(statuses, ["recording", "waiting", "waiting"])
        self.assertEqual(window.jobs[1]["item"].text(2), "Waiting")
        # the second is canceled before its turn
        window.jobs[1]["item"].setSelected(True)
        window.cancel_jobs()
        self.done(window, 3)
        self.assertEqual([j["status"] for j in window.jobs], ["done", "canceled", "done"])
        self.assertEqual(sorted(os.listdir(self.videos)), ["first 1-00.mp4", "first 3-00.mp4"])
        window.remove_finished()
        self.assertEqual(window.jobs, [])
        self.assertEqual(window.queue.topLevelItemCount(), 0)

    def test_cancel(self):
        window = self.window()
        self.ready(window)
        with mock.patch.dict(os.environ, {"FAKEGAME_SPEED": "0.01"}):
            self.record(window)
            recorder = window.recorder
            self.assertTrue(wait_until(lambda: window.jobs[0]["item"].text(2).startswith("Recording "), 20))
            window.jobs[0]["item"].setSelected(True)
            window.cancel_jobs()
        self.assertIsNone(window.recorder)
        self.assertEqual(window.jobs[0]["status"], "canceled")
        self.assertFalse(recorder.game.running())
        self.assertFalse(os.path.exists(self.videos) and os.listdir(self.videos))

    def test_join_failing(self):
        self.add_demo("second", DEMO)
        window = self.window()
        self.ready(window, "first", "second")
        with mock.patch.dict(os.environ, {"FAKEGAME_FFMPEG_FAIL": "join"}):
            self.record(window, lambda d: d.join.setChecked(True))
            job, = self.done(window)
        self.assertEqual(job["status"], "failed")
        self.assertIn("can't join", job["item"].toolTip(2))
        self.assertFalse(os.path.exists(self.videos) and os.listdir(self.videos))

    def test_game_closed_while_joining(self):
        self.add_demo("second", DEMO)
        window = self.window()
        self.ready(window, "first", "second")
        with mock.patch.dict(os.environ, {"FAKEGAME_JOIN_WAIT": "2"}):
            self.record(window, lambda d: d.join.setChecked(True))
            self.assertTrue(wait_until(lambda: window.recorder and window.recorder.phase == "joining", 20))
            self.assertFalse(window.recorder.game.running())
            job, = self.done(window)
        self.assertEqual(job["status"], "done")
        self.assertEqual(len(self.video(os.path.basename(job["output"]))["joined"]), 2)

    def test_joining_tells_how_far(self):
        self.add_demo("second", DEMO)
        window = self.window()
        self.ready(window, "first", "second")
        with mock.patch.dict(os.environ, {"FAKEGAME_JOIN_WAIT": "3"}):
            self.record(window, lambda d: d.join.setChecked(True))
            self.assertTrue(wait_until(lambda: window.recorder and window.recorder.phase == "joining", 20))
            job, recorder = window.jobs[0], window.recorder
            self.assertGreater(recorder.join_size, 0)

            def shows(written, text):
                with mock.patch.object(mohreplay, "written_by", lambda pid: written):
                    return wait_until(lambda: job["item"].text(2) == text, 2)
            # the clips once, then as much again as the video's index moves
            self.assertTrue(shows(recorder.join_size, "Joining 2 clips: 50%, under a minute left…"))
            self.assertTrue(shows(recorder.join_size * 5, "Joining 2 clips: 99%, under a minute left…"))
            # when it can't be told, as it was
            self.assertTrue(shows(None, "Joining 2 clips: 99%, under a minute left…"))
            self.done(window)
        self.assertEqual(job["status"], "done")

    def test_written_by(self):
        before = mohreplay.written_by(os.getpid())
        if before is None:
            self.skipTest("this system doesn't tell")
        with open(os.path.join(self.demos, "written"), "wb", buffering=0) as f:
            f.write(b"x" * 100000)
        self.assertGreaterEqual(mohreplay.written_by(os.getpid()) - before, 100000)
        self.assertIsNone(mohreplay.written_by(0))

    def record_failing(self, names, numbers=False, wait=True):
        """Records t-'s frags in demos, one video for all: the job, and the
        commands the games were sent, till it's done if wait."""
        sent, send = [], mohreplay.Game.send
        window = self.window()
        TestLibrary.indexed(self, window, names)
        self.choose(window, killer="t-")
        for name in names:
            window.demo_items[name].setSelected(True)

        def setup(dialog):
            dialog.choices["kills"].setChecked(True)
            dialog.join.setChecked(True)
            dialog.numbers.setChecked(numbers)
            dialog.pattern.setText("failing")
        spy = mock.patch.object(mohreplay.Game, "send", lambda game, *commands: (sent.extend(commands),
                                                                                send(game, *commands)))
        spy.start()
        self.addCleanup(spy.stop)
        self.record(window, setup, window.record_demos)
        if not wait:
            return window.jobs[0], sent
        job, = self.done(window, timeout=60)
        return job, sent

    def test_clips_left_out(self):
        """A clip that doesn't record is tried again in a new game, then
        left out, and the video is made without it."""
        # the game quits in the middle of the first try
        self.add_demo("crashing", dict(DEMO, crashAt=7000, crashOnce=True))
        # its map is missing
        self.add_demo("unloadable", dict(DEMO, loadFails=True))
        job, sent = self.record_failing(("first", "crashing", "unloadable"), numbers=True)
        self.assertEqual(job["status"], "done", job["item"].toolTip(2))
        self.assertEqual(job["item"].text(2), "Done, 1 clip left out")
        self.assertEqual(job["item"].toolTip(2), "unloadable: The recorder didn't load unloadable. "
                                                 "ERROR: Couldn't load maps/obj/missing.bsp")
        self.assertEqual(len(self.video("failing.mp4")["joined"]), 2)
        with open(os.path.join(self.videos, "failing.csv"), encoding="utf-8") as f:
            self.assertEqual({row.split(",")[2] for row in f.read().splitlines()[1:]}, {"first", "crashing"})
        # each tried twice
        demos = [c for c in sent if c.startswith("demo ")]
        self.assertEqual(sorted(set(demos)), ['demo "crashing"', 'demo "first"', 'demo "unloadable"'])
        self.assertEqual((demos.count('demo "crashing"'), demos.count('demo "unloadable"')), (2, 2))

    def test_clip_quitting_the_game(self):
        """A clip the game quits in each time is left out, with its last
        lines."""
        self.add_demo("crashing", dict(DEMO, crashAt=7000))
        job, _ = self.record_failing(("first", "crashing"))
        self.assertEqual(job["item"].text(2), "Done, 1 clip left out")
        tip = job["item"].toolTip(2)
        self.assertTrue(tip.startswith("crashing: The recorder quit. Its last lines: "), tip)
        self.assertTrue(tip.endswith(" / Received signal 11, exiting..."), tip)

    def test_clips_left_out_in_a_row(self):
        """Three clips in a row that don't record stop the video."""
        for name in ("a", "b", "c"):
            self.add_demo(name, dict(DEMO, loadFails=True))
        job, _ = self.record_failing(("first", "a", "b", "c"))
        self.assertEqual(job["status"], "failed")
        self.assertTrue(job["item"].toolTip(2).startswith("3 clips in a row didn't record. The recorder didn't load"),
                        job["item"].toolTip(2))
        self.assertFalse(os.path.exists(self.videos) and os.listdir(self.videos))

    def test_player_in_several_demos(self):
        other = json.loads(json.dumps(DEMO))
        other["recorder"]["name"] = other["watched"][0]["name"] = "^2T-"
        for kill in other["kills"]:
            kill["killerName"] = kill["killerName"].replace("t-", "^2T-")
            kill["victimName"] = kill["victimName"].replace("t-", "^2T-")
        self.add_demo("other", other)
        nokills = dict(DEMO, kills=[k for k in DEMO["kills"] if k["killerName"] != "t-"])
        self.add_demo("none", nokills)
        window = self.window()
        TestLibrary.indexed(self, window, ("first", "other", "none"))
        for name in ("first", "other", "none"):
            window.demo_items[name].setSelected(True)
        # the search finds t- in the three
        window.filter.setText("t-")
        self.assertEqual(sorted(window.demos_of(window.demos.selectedItems())), ["first", "none", "other"])
        # t-'s kills, in two
        self.choose(window, killer="t-")
        self.assertEqual(TestWindow.shown(self, window), ["first", "other"])
        self.assertEqual(window.demo_items["other"].text(mohreplay.COL_PLAYER), "^2T- (3)")

        def setup(dialog):
            dialog.choices["kills"].setChecked(True)
            dialog.join.setChecked(True)
            dialog.pattern.setText("{player} in {demo}")
        dialog = self.record(window, setup, window.record_demos)
        self.assertEqual(dialog.choices["kills"].text(), "The 6 frags listed, in 2 demos, under a minute")
        job, = self.done(window)
        self.assertEqual(job["status"], "done", job["item"].toolTip(2))
        self.assertEqual(job["item"].text(1), "2 demos, frags by t-")
        parts = self.video("t- in 2 demos.mp4")["joined"]
        # a part of the name, whatever each demo's spelling
        self.assertEqual([(p["only"], p["player"], p["filter"]["killers"]) for p in parts],
                         [("kills", "", ["t-"]), ("kills", "", ["t-"])])

    def test_frags_from_in_several_demos(self):
        """Who they're from in each demo: only those with some are in the
        video, one for all or one each."""
        self.add_demo("followed", followed_demo())
        # t-, shown till 5:00, kills only after
        self.add_demo("unseen", dict(DEMO, kills=[k for k in DEMO["kills"] if k["time"] > 100000]))
        window = self.window()
        TestLibrary.indexed(self, window, ("first", "followed", "unseen"))
        window.filter.setText("t-")
        for name in ("first", "followed", "unseen"):
            window.demo_items[name].setSelected(True)
        self.choose(window, recorder=True, specced=True)

        def setup(dialog):
            dialog.choices["kills"].setChecked(True)
            dialog.join.setChecked(True)
            dialog.pattern.setText("{demo} {player}")
        dialog = self.record(window, setup)
        self.assertEqual(dialog.choices["kills"].text(), "The 8 frags listed, in 2 demos, under a minute")
        job, = self.done(window)
        self.assertEqual(job["status"], "done", job["item"].toolTip(2))
        self.assertEqual(job["item"].text(1), "2 demos, frags from the recorder and specced")
        parts = self.video("2 demos.mp4")["joined"]
        self.assertEqual([(p["only"], p["player"], p["filter"]["specced"]) for p in parts],
                         [("kills", "", True), ("kills", "", True)])

        def each(dialog):
            dialog.choices["kills"].setChecked(True)
            dialog.join.setChecked(False)
            dialog.pattern.setText("{demo} each")
        self.record(window, each)
        jobs = self.done(window, 3)
        self.assertEqual(sorted(job["item"].text(1) for job in jobs[1:]),
                         ["first, frags from the recorder and specced", "followed, frags from the recorder and specced"])
        self.assertEqual(sorted(n for n in os.listdir(self.videos) if "each" in n),
                         ["first each.mp4", "followed each.mp4"])

        # with a weapon: only the demos listed
        self.choose(window, weapons=["sniper"])
        self.assertEqual(TestWindow.shown(self, window), ["followed"])
        self.assertEqual(window.demo_items["followed"].text(mohreplay.COL_PLAYER), "^1Phil (1)")
        window.demos.clearSelection()
        window.demo_items["followed"].setSelected(True)
        dialog = self.record(window, lambda d: d.pattern.setText("{demo} sniper"))
        self.assertEqual(list(dialog.choices), ["kills", "everything"])
        self.assertEqual(dialog.choices["kills"].text(), "The frag listed, under a minute")
        self.assertTrue(dialog.choices["kills"].isChecked())
        self.done(window, 4)
        video = self.video("followed sniper.mp4")
        self.assertEqual((video["only"], video["filter"]["weapons"]), ("kills", ["sniper"]))

    def test_numbered_clips(self):
        self.add_demo("followed", followed_demo())
        window = self.window()
        TestLibrary.indexed(self, window, ("first", "followed"))
        for name in ("first", "followed"):
            window.demo_items[name].setSelected(True)
        self.choose(window, recorder=True)

        def setup(dialog):
            dialog.choices["kills"].setChecked(True)
            dialog.join.setChecked(True)
            dialog.numbers.setChecked(True)
            dialog.pattern.setText("{demo} numbered")
        self.record(window, setup, window.record_demos)
        job, = self.done(window)
        self.assertEqual(job["status"], "done", job["item"].toolTip(2))
        self.assertEqual(self.settings.value("rec/numbers"), "true")
        # each demo's first clip has the number after the last of the one before
        parts = self.video("2 demos numbered.mp4")["joined"]
        first, second = (len(clip["spans"]) for clip in job["clips"])
        self.assertEqual([p["cvars"]["cl_demoClipNumbers"] for p in parts], ["1", str(1 + first)])
        with open(os.path.join(self.videos, "2 demos numbered.csv"), encoding="utf-8") as f:
            rows = list(csv.reader(f))
        self.assertEqual(rows[0], mohreplay.CLIP_LIST)
        self.assertEqual(sorted({int(row[0]) for row in rows[1:]}), list(range(1, first + second + 1)))
        self.assertEqual({row[2] for row in rows[1:]}, {"first", "followed"})
        self.assertEqual({row[4] for row in rows[1:]}, {"t-"})
        self.assertEqual(len(rows) - 1, 6)

        # not numbered: no number drawn, no list
        def plain(dialog):
            setup(dialog)
            dialog.numbers.setChecked(False)
            dialog.pattern.setText("{demo} plain")
        self.record(window, plain, window.record_demos)
        self.done(window, 2)
        self.assertEqual([p["cvars"]["cl_demoClipNumbers"] for p in self.video("2 demos plain.mp4")["joined"]],
                         ["0", "0"])
        self.assertFalse(os.path.exists(os.path.join(self.videos, "2 demos plain.csv")))

    def test_whole_demos_one_video_each(self):
        # short, to record quickly, so listed anyway
        self.short_demos()
        self.add_demo("second", dict(DEMO, duration=30000))
        window = self.window()
        TestLibrary.indexed(self, window, ("first", "second"))
        window.demo_items["second"].setSelected(True)

        def setup(dialog):
            dialog.choices["everything"].setChecked(True)
            dialog.pattern.setText("{demo}")
        dialog = self.record(window, setup, window.record_demos)
        self.assertEqual(dialog.choices["everything"].text(), "All of the demo, under a minute")
        self.assertEqual(dialog.choices["kills"].text(), "The 4 frags listed, under a minute")
        job, = self.done(window)
        self.assertEqual(job["item"].text(1), "second")
        video = self.video("second.mp4")
        self.assertEqual((video["start"], video["only"]), (0, ""))
        self.assertEqual(video["stop"], 30000)

    def stretch(self, window, start, name):
        """Records 3 seconds of the demo playing from start, a video named
        name."""
        def setup(dialog):
            dialog.range.setChecked(True)
            dialog.start.setText(start)
            dialog.end.setText(start[:-1] + "3")
            dialog.pattern.setText(name)
        self.record(window, setup)

    def labels(self, window):
        return [window.queue.topLevelItem(i).text(0) for i in range(window.queue.topLevelItemCount())]

    def test_censor_recorded(self):
        """A video has the words starred out when it was queued, none when
        it's off."""
        self.settings.setValue("censor_words", "gay, jew")
        window = self.window()
        self.ready(window)
        self.stretch(window, "1:00", "on")
        self.settings.setValue("censor", "0")
        self.stretch(window, "1:10", "off")
        self.done(window, 2)
        self.assertEqual(self.video("on.mp4")["cvars"]["cl_censor"], "gay jew")
        self.assertNotIn("cl_censor", self.video("off.mp4")["cvars"])

    def test_queue_kept_by_one_window(self):
        """The first window open keeps the videos to record; another one
        doesn't write over its list, and asks before closing with videos
        left, which aren't kept."""
        first = self.window()
        self.ready(first)
        with mock.patch.dict(os.environ, {"FAKEGAME_SPEED": "0.01"}):
            self.stretch(first, "1:00", "first")
            second = self.window()
            self.assertFalse(second.keeps_queue)
            self.assertIn("Another window keeps", second.message.text())
            self.ready(second)
            self.stretch(second, "2:00", "second")
            with open(first.queue_file()) as f:
                self.assertEqual([j["output"] for j in json.load(f)], [os.path.join(self.videos, "first.mp4")])
            asked = []
            with mock.patch.object(QMessageBox, "question", lambda *a, **k: asked.append(a[2]) or QMessageBox.No):
                second.close()
            self.assertEqual(asked, ["1 video is still to record, and won't be kept: another window keeps the "
                                     "videos to record. Quit anyway?"])
            self.assertTrue(second.recorder.game.running())
            recorder = second.recorder
            second.close()
            self.assertFalse(recorder.game.running())
            first.close()
        with open(first.queue_file()) as f:
            self.assertEqual([(j["output"], j["status"]) for j in json.load(f)],
                             [(os.path.join(self.videos, "first.mp4"), "waiting")])
        # once it's closed, the next window keeps them
        third = self.window()
        self.assertTrue(third.keeps_queue)
        self.assertEqual(self.labels(third), ["first.mp4"])

    def test_how_it_looks(self):
        """Record has the cameras, the models of the player followed, each
        team's only, the gun and the models forced; videos have them, and
        they're kept."""
        os.makedirs(os.path.join(self.game_dir, "main"))
        with zipfile.ZipFile(os.path.join(self.game_dir, "main", "pak0.pk3"), "w") as pk3:
            for model in ("allied_pilot", "american_army", "german_scientist", "IT_soldier", "own_skin"):
                pk3.writestr(f"models/player/{model}.tik", "")
        window = self.window()
        self.ready(window)
        items = lambda menu: [menu.itemText(i) for i in range(menu.count())]

        def setup(dialog):
            self.assertEqual(items(dialog.recorder_camera), ["In their eyes", "Behind them"])
            self.assertEqual(items(dialog.followed_camera), ["Behind them", "In their eyes", "As the server shows them"])
            self.assertEqual((dialog.recorder_camera.currentData(), dialog.followed_camera.currentData()), ("eyes", "behind"))
            # someone's own in both
            self.assertEqual(items(dialog.followed_allies), ["As in the demo", "allied_pilot", "american_army", "own_skin"])
            self.assertEqual(items(dialog.followed_axis), ["As in the demo", "german_scientist", "it_soldier", "own_skin"])
            self.assertEqual((dialog.followed_allies.currentData(), dialog.followed_axis.currentData()), ("", ""))
            self.assertTrue(dialog.viewmodel.isChecked() and dialog.forcemodel.isChecked())
            dialog.recorder_camera.setCurrentIndex(1)
            dialog.followed_camera.setCurrentIndex(1)
            dialog.followed_axis.setCurrentText("german_scientist")
            dialog.viewmodel.setChecked(False)
            dialog.forcemodel.setChecked(False)
            dialog.pattern.setText("looks")
            # not in the line of what Settings sets
            self.assertEqual(dialog.summary.text(), "1920x1080, 60 fps, H.264 (CRF 20), sound 192k, words censored")
        self.record(window, setup)
        self.done(window)
        cvars = self.video("looks.mp4")["cvars"]
        self.assertEqual([cvars[name] for name in ("cg_recordercamera", "cg_followcamera", "cg_followedGermanModel",
                                                   "cg_drawviewmodel", "cg_forceModel")],
                         ["1", "2", "german_scientist", "0", "0"])
        self.assertNotIn("cg_followedModel", cvars)
        self.assertEqual(mohreplay.cameras(self.settings), ["behind", "eyes"])
        self.assertEqual(mohreplay.followed_models(self.settings), ["", "german_scientist"])

        # kept; one of the other team or no more in the game files still shows
        self.settings.setValue("followed_allies", "my_own")
        self.settings.setValue("followed_axis", "allied_pilot")
        dialogs = []
        with mock.patch.object(mohreplay.RecordDialog, "exec", lambda dialog: dialogs.append(dialog) or 0):
            window.record_button.click()
        dialog = dialogs[0]
        self.assertEqual((dialog.recorder_camera.currentData(), dialog.followed_camera.currentData()), ("behind", "eyes"))
        self.assertEqual((items(dialog.followed_allies)[-1], dialog.followed_allies.currentData()), ("my_own", "my_own"))
        self.assertEqual((items(dialog.followed_axis)[-1], dialog.followed_axis.currentData()), ("allied_pilot", "allied_pilot"))
        self.assertFalse(dialog.viewmodel.isChecked() or dialog.forcemodel.isChecked())

        # a game watching a demo looks the same, with the lead of the videos,
        # not one kept from when it was a choice
        self.settings.setValue("rec/kill_before", "3.0")
        self.playing(window)
        args = window.game.args
        self.assertEqual([args[args.index(name) + 1] for name in ("cg_recordercamera", "cg_followcamera",
                                                                  "cg_drawviewmodel", "cg_forceModel", "cl_demoKillBefore")],
                         ["1", "2", "0", "0", "2.0"])

    def test_cameras_recorded(self):
        """A video has the cameras chosen when it was queued; by default, the
        recorder in their eyes and the player followed behind them. The game
        makes up a scope by itself, whatever a video queued before says."""
        window = self.window()
        self.ready(window)
        with mock.patch.dict(os.environ, {"FAKEGAME_SPEED": "1"}):
            self.stretch(window, "1:00", "default")
            self.settings.setValue("camera_recorder", "behind")
            self.settings.setValue("camera_followed", "eyes")
            self.stretch(window, "1:10", "chosen")
            self.settings.setValue("camera_followed", "server")
            self.done(window, 2)
        cvars = [self.video(name)["cvars"] for name in ("default.mp4", "chosen.mp4")]
        self.assertEqual([(c["cg_recordercamera"], c["cg_followcamera"]) for c in cvars], [("0", "1"), ("1", "2")])
        for c in cvars:
            self.assertNotIn("cg_demoscope", c)

    def test_followed_models_recorded(self):
        """A video has the models of the player followed chosen when it was
        queued."""
        self.settings.setValue("followed_axis", "german_scientist")
        window = self.window()
        self.ready(window)
        with mock.patch.dict(os.environ, {"FAKEGAME_SPEED": "1"}):
            self.stretch(window, "1:00", "a")
            self.stretch(window, "1:10", "b")
            self.settings.setValue("followed_axis", "")
            self.done(window, 2)
        for name in ("a.mp4", "b.mp4"):
            cvars = self.video(name)["cvars"]
            self.assertEqual(cvars["cg_followedGermanModel"], "german_scientist")
            self.assertNotIn("cg_followedModel", cvars)

    def test_pause_and_move(self):
        """A video paused waits till it's resumed; the order of the list is
        the order they're recorded in."""
        window = self.window()
        self.ready(window)
        with mock.patch.dict(os.environ, {"FAKEGAME_SPEED": "1"}):
            for start, name in (("1:00", "a"), ("1:10", "b"), ("1:20", "c")):
                self.stretch(window, start, name)
            a, b, c = window.jobs
            b["item"].setSelected(True)
            window.pause_jobs()
            self.assertEqual((b["status"], b["item"].text(2)), ("paused", "Paused"))
            b["item"].setSelected(False)
            c["item"].setSelected(True)
            window.move_jobs(-1)
            self.assertEqual(self.labels(window), ["a.mp4", "c.mp4", "b.mp4"])
            self.assertEqual(window.jobs, [a, c, b])
            self.assertTrue(c["item"].isSelected())
            # a and c, not b
            self.assertTrue(wait_until(lambda: c["status"] == "done", 30))
            self.assertEqual(a["status"], "done")
            wait_until(lambda: False, 0.5)
            self.assertEqual(b["status"], "paused")
            self.assertIsNone(window.recorder)
            c["item"].setSelected(False)
            b["item"].setSelected(True)
            window.resume_jobs()
            self.assertTrue(wait_until(lambda: b["status"] == "done", 30))
        self.assertEqual(self.video("b.mp4")["start"], 70000)
        # down, past the end
        b["item"].setSelected(True)
        window.move_jobs(1)
        window.move_jobs(1)
        self.assertEqual(self.labels(window), ["a.mp4", "c.mp4", "b.mp4"])

    def test_video_keeps_its_folder(self):
        """A video records from the folder of demos it was queued from, also
        when another folder is opened before its turn."""
        window = self.window()
        self.ready(window)
        with mock.patch.dict(os.environ, {"FAKEGAME_SPEED": "1"}):
            self.stretch(window, "1:00", "one")
            self.stretch(window, "1:10", "two")
            self.assertEqual([job["demos"] for job in window.jobs], [self.demos, self.demos])
            other = os.path.join(self.tmp, "other")
            os.makedirs(other)
            self.settings.setValue("demos", other)
            self.done(window, 2, 60)
        self.assertEqual([job["status"] for job in window.jobs], ["done", "done"], window.jobs[1]["item"].text(2))
        self.assertFalse([clip for job in window.jobs for clip in job["clips"] if clip.get("skipped")])
        self.assertEqual(self.video("two.mp4")["start"], 70000)

    def test_record_again(self):
        """A video done is recorded again into a new one next to it,
        numbered from the name it was first given."""
        window = self.window()
        self.ready(window)
        with mock.patch.dict(os.environ, {"FAKEGAME_SPEED": "1"}):
            self.stretch(window, "1:00", "again")
            job = window.jobs[0]
            self.assertTrue(wait_until(lambda: job["status"] == "done", 30))
            for numbered in ("again (2).mp4", "again (3).mp4"):
                window.queue.clearSelection()
                window.record_jobs_again()
                self.assertEqual(job["status"], "done")
                job["item"].setSelected(True)
                window.record_jobs_again()
                self.assertIn(job["status"], ("waiting", "recording"))
                self.assertTrue(wait_until(lambda: job["status"] == "done", 30), job["item"].text(2))
                self.assertEqual(os.path.basename(job["output"]), numbered)
                self.assertEqual(self.labels(window), [numbered])
        self.assertEqual(sorted(os.listdir(self.videos)), ["again (2).mp4", "again (3).mp4", "again.mp4"])
        self.assertEqual(self.video("again (3).mp4")["start"], 60000)

    def test_pause_recording(self):
        """A video paused while it records keeps its clips made, and goes on
        from the next one when it's resumed."""
        self.add_demo("second", DEMO)
        self.add_demo("third", DEMO)
        with mock.patch.dict(os.environ, {"FAKEGAME_SPEED": "1"}):
            job, sent = self.record_failing(("first", "second", "third"), wait=False)
            window = self.windows[-1]
            self.assertTrue(wait_until(lambda: job.get("next_clip") == 1, 30))
            job["item"].setSelected(True)
            window.pause_jobs()
            self.assertEqual((job["status"], job["item"].text(2)), ("paused", "Paused after clip 1 of 3"))
            self.assertFalse(window.recorder)
            demos = [c for c in sent if c.startswith("demo ")]
            window.resume_jobs()
            self.assertTrue(wait_until(lambda: job["status"] == "done", 60), job["item"].text(2))
        self.assertEqual(len(self.video("failing.mp4")["joined"]), 3)
        # the first clip wasn't made again
        again = [c for c in sent[len(demos):] if c.startswith("demo ")]
        self.assertNotIn(demos[0], again)
        self.assertFalse([name for name in os.listdir(self.videos) if name.startswith(".mohreplay-parts")])

    def test_videos_panel(self):
        """Always there: how many there are, and one button that pauses or
        resumes those selected, as they are; the rest under More."""
        window = self.window()
        self.ready(window)
        buttons = {button.text() for button in window.findChildren(mohreplay.QPushButton)}
        self.assertTrue({"Pause", "Cancel", "Open folder"} <= buttons)
        more = next(button for button in window.findChildren(mohreplay.QToolButton) if button.text() == "More")
        self.assertEqual([action.text() for action in more.menu().actions()], ["Record again", "Play live", "Remove finished"])
        self.assertEqual(window.queue_summary.text(), "")
        self.assertFalse(window.pause_button.isEnabled())
        with mock.patch.dict(os.environ, {"FAKEGAME_SPEED": "1"}):
            for start, name in (("1:00", "a"), ("1:10", "b"), ("1:20", "c")):
                self.stretch(window, start, name)
            a, b, c = window.jobs
            self.assertEqual(window.queue_summary.text(), "1 recording, 2 waiting")
            b["item"].setSelected(True)
            self.assertEqual((window.pause_button.text(), window.pause_button.isEnabled()), ("Pause", True))
            window.pause_button.click()
            self.assertEqual(b["status"], "paused")
            self.assertEqual(window.pause_button.text(), "Resume")
            self.assertEqual(window.queue_summary.text(), "1 recording, 1 waiting, 1 paused")
            # some of each: pause first, those paused resumed on their own
            c["item"].setSelected(True)
            self.assertEqual(window.pause_button.text(), "Pause")
            c["item"].setSelected(False)
            window.pause_button.click()
            self.assertEqual(b["status"], "waiting")
            self.assertEqual(window.pause_button.text(), "Pause")
            b["item"].setSelected(False)
            buttons = {b.toolTip(): b for b in window.findChildren(mohreplay.QToolButton)}
            c["item"].setSelected(True)
            buttons["Records the videos selected sooner"].click()
            self.assertEqual(self.labels(window), ["a.mp4", "c.mp4", "b.mp4"])
            buttons["Records the videos selected later"].click()
            self.assertEqual(self.labels(window), ["a.mp4", "b.mp4", "c.mp4"])
            self.assertTrue(wait_until(lambda: all(job["status"] == "done" for job in window.jobs), 60))
        self.assertEqual(window.queue_summary.text(), "3 done")
        self.assertFalse(window.pause_button.isEnabled())
        next(action for action in more.menu().actions() if action.text() == "Remove finished").trigger()
        self.assertEqual((window.jobs, window.queue_summary.text()), ([], ""))

    def test_queue_kept_when_closing(self):
        """Closing keeps the videos left, the one recording with its clips
        made: they go on when it opens again."""
        self.add_demo("second", DEMO)
        with mock.patch.dict(os.environ, {"FAKEGAME_SPEED": "1"}):
            job, sent = self.record_failing(("first", "second"), wait=False)
            window = self.windows[-1]
            self.ready(window)
            self.stretch(window, "1:00", "later")
            self.stretch(window, "2:00", "held")
            window.jobs[2]["item"].setSelected(True)
            window.pause_jobs()
            self.assertTrue(wait_until(lambda: job.get("next_clip") == 1, 30))
            recorder = window.recorder
            window.close()
            self.assertFalse(recorder.game.running())
            self.assertEqual([j["status"] for j in window.jobs], ["waiting", "waiting", "paused"])
            again = self.window()
            self.assertEqual(self.labels(again), ["failing.mp4", "later.mp4", "held.mp4"])
            self.assertEqual(again.jobs[2]["item"].text(2), "Paused")
            first = len(sent)
            self.assertTrue(wait_until(lambda: [j["status"] for j in again.jobs] == ["done", "done", "paused"], 60),
                            [j["item"].text(2) for j in again.jobs])
        self.assertEqual(len(self.video("failing.mp4")["joined"]), 2)
        self.assertEqual(self.video("later.mp4")["start"], 60000)
        # it went on from the second clip
        self.assertEqual(next(c for c in sent[first:] if c.startswith("demo ")), 'demo "second"')


REAL_GAME = os.environ.get("MOHREPLAY_TEST_GAME")
REAL_DEMOS = os.environ.get("MOHREPLAY_TEST_DEMOS")


def game_folder(real, folder):
    """A game folder of its own in folder, with links to the files of real
    but none to its settings, which the game watched writes there."""
    os.makedirs(os.path.join(folder, "main"))
    for name in os.listdir(real):
        if name != "main":
            os.symlink(os.path.join(real, name), os.path.join(folder, name))
    for name in os.listdir(os.path.join(real, "main")):
        if name not in ("configs", "omconfig.cfg"):
            os.symlink(os.path.join(real, "main", name), os.path.join(folder, "main", name))
    return folder


@unittest.skipUnless(REAL_GAME and REAL_DEMOS, "set MOHREPLAY_TEST_GAME and MOHREPLAY_TEST_DEMOS")
class TestRealGame(FakeGameCase):
    """Plays and records a real demo with the real game, without a window."""

    env = {}

    def setUp(self):
        super().setUp()
        exe = os.environ.get("MOHREPLAY_TEST_EXE") or os.path.join(mohreplay.REPO, ".cmake", "RelWithDebInfo", "openmohaa")
        self.settings.setValue("exe", exe)
        self.settings.setValue("game", game_folder(REAL_GAME, os.path.join(self.tmp, "real")))
        self.settings.setValue("size", "640x360")
        self.demo = os.environ.get("MOHREPLAY_TEST_DEMO") or "d482684ec556d1c3-obj-obj_team1"
        # a link to it: indexes are kept next to the demos, not to be left there
        linked = os.path.join(self.tmp, "real demos")
        os.makedirs(linked)
        os.symlink(os.path.join(REAL_DEMOS, self.demo + ".dm3"), os.path.join(linked, self.demo + ".dm3"))
        self.settings.setValue("demos", linked)
        # quiet: the recorder's own volume comes after
        start = mohreplay.Game.start

        def quiet(game, exe, game_dir, demos, width, height, extra=(), driver=None, **kwargs):
            return start(game, exe, game_dir, demos, width, height, ("+set", "s_volume", "0", *extra), driver,
                         **kwargs)
        patch = mock.patch.object(mohreplay.Game, "start", quiet)
        patch.start()
        self.addCleanup(patch.stop)

    def real_demo(self, window):
        """Plays the demo: its index."""
        window.play_demo(self.demo)
        self.assertTrue(wait_until(lambda: self.game_state(window).get("demo") == self.demo
                                   and (window.game.read_json("demoindex.json") or {}).get("demo") == self.demo, 60))
        return window.game.read_json("demoindex.json")

    def test_frags_shown(self):
        """Only these kills of whoever is shown plays inside their clips, as
        Record plans them."""
        window = self.window()
        index = self.real_demo(window)
        kill_filter = mohreplay.KillFilter(recorder=True, specced=True)
        frags = mohreplay.planned_kills(index, kill_filter)[0]
        spans = [mohreplay.kill_span(index, k, start, end) for k, start, end in frags]
        clips = mohreplay.demoonly_plan(index, "kills", kill_filter=kill_filter)[1]
        # from the kill before the first one by a player the recorder follows
        recorder = index["recorder"]["name"]
        followed = next(i for i, (k, _, _) in enumerate(frags) if not mohreplay.same_player(k["killerName"], recorder))
        # as planned: a clip going on while its fragger fires is another test
        window.game.send("set cl_demoKeepShooting 0", *kill_filter.commands(), "timescale 4",
                         "demoseek " + mohreplay.seconds(spans[max(0, followed - 1)][0]), "demoonly kills")
        samples, end = [], time.monotonic() + 20
        while time.monotonic() < end:
            state = self.game_state(window)
            if state.get("only") == "kills" and not state.get("seeking") and not state.get("paused"):
                samples.append(state["time"])
            time.sleep(0.02)
        self.assertGreater(len(set(samples)), 20)
        # a frame may be drawn past a clip before demoonly jumps
        self.assertEqual([t for t in samples if not any(s - 150 <= t <= e + 150 for s, e in clips)], [])
        start, stop = spans[followed]
        self.assertTrue(any(start <= t <= stop for t in samples), "the followed player's kill wasn't played")

    def test_clips_go_round_the_fights(self):
        """A frag's clip starts before the fight that led to it, as planned
        from the index's shots: demoonly jumps to the starts planned, some of
        them pulled back by a fight."""
        window = self.window()
        index = self.real_demo(window)
        kills = index["kills"]
        self.assertTrue(all(k.get("fightStart", k["time"]) <= k["time"] <= k.get("fightEnd", k["time"]) for k in kills))
        lines, output = [], window.game.on_output
        window.game.on_output = lambda line: (lines.append(line), output(line))
        kill_filter = mohreplay.KillFilter(recorder=True, specced=True)
        spans = mohreplay.demoonly_plan(index, "kills", kill_filter=kill_filter)[1]
        plain = json.loads(json.dumps(index))
        for kill in plain["kills"]:
            kill.pop("fightStart", None)
            kill.pop("fightEnd", None)
        plain_spans = mohreplay.demoonly_plan(plain, "kills", kill_filter=kill_filter)[1]
        self.assertGreater(mohreplay.length(spans), mohreplay.length(plain_spans), "no fight made a clip longer")
        window.game.send(*kill_filter.commands(), "timescale 4", "demoseek " + mohreplay.seconds(spans[0][0]),
                         "demoonly kills")

        def msec(text):
            minutes, seconds = text.split(":")
            return round(int(minutes) * 60000 + float(seconds) * 1000)

        def seeks():
            return [msec(match.group(1)) for match in (re.search(r"Seeking to ([\d:.]+)", line) for line in lines)
                    if match]
        wanted = [start for start, _ in spans[1:4]]
        # after the demoseek (and demoonly's to the first), from one stretch to the next
        later = lambda: [time for time in seeks() if time > spans[0][0]]
        self.assertTrue(wait_until(lambda: len(later()) >= len(wanted), 40), (seeks(), spans[:4]))
        self.assertEqual(later()[:len(wanted)], wanted)

    def test_scope_on_long_sniper_frags(self):
        """A scope is made up by itself, only for sniper frags from far
        enough, and for their shots that kill nobody going far enough."""
        demo = "ff7c4057553458d4-obj-obj_team4"
        path = os.path.join(REAL_DEMOS, demo + ".dm3")
        if not os.path.isfile(path):
            self.skipTest(f"no {path}")
        os.symlink(path, os.path.join(self.settings.value("demos"), demo + ".dm3"))
        self.demo = demo
        window = self.window()
        index = self.real_demo(window)
        lines, output = [], window.game.on_output
        window.game.on_output = lambda line: (lines.append(line), output(line))
        kill_filter = mohreplay.KillFilter(unshown=True, weapons=["sniper"])
        spans = mohreplay.demoonly_plan(index, "kills", kill_filter=kill_filter)[1]
        window.game.send("set developer 1", *kill_filter.commands(), "timescale 4",
                         "demoseek " + mohreplay.seconds(spans[0][0]), "demoonly kills")

        def scopes(what=r"kill (\d+) units away"):
            matches = (re.search(rf"Scope for a sniper {what}: (made up|none)", line) for line in lines)
            return [(int(match.group(1)), match.group(2)) for match in matches if match]

        def shots():
            return scopes(r"shot (\d+) units long")

        def both():
            # a fragger misses twice from over 3,000 units before their frag at 1:58
            return {made for _, made in scopes()} == {"made up", "none"} and ("made up" in {made for _, made in shots()})
        self.assertTrue(wait_until(both, 30), (scopes(), shots()))
        for distance, made in scopes() + shots():
            self.assertEqual(made, "made up" if distance >= 1000 else "none", distance)

    def test_kill_filter(self):
        """demonextkill goes where Record plans each kill to start, with the
        filter's killers, victims and weapons."""
        window = self.window()
        index = self.real_demo(window)
        window.game.send("demopause 1")
        frags = [k for k in index["kills"] if not k.get("teamKill") and not k.get("grenade")]
        self.assertTrue(all(k.get("weapon") for k in frags), "kills without a weapon")

        def most(names):
            return max(set(names), key=names.count)
        recorder = index["recorder"]["name"]
        weapon = most([mohreplay.weapon_of(k) for k in frags if mohreplay.same_player(k["killerName"], recorder)])
        # killed by whoever is shown
        victim = most([k["victimName"] for k, _, _ in mohreplay.shown_kills(index) if k in frags and k["victimName"]
                       and not mohreplay.same_player(k["victimName"], k["killerName"])])
        cases = ((dict(killer=recorder[:3], weapons=[weapon]), ), (dict(recorder=True, specced=True, victim=victim), ),
                 (dict(weapons=["mg", "sniper"]), ), (dict(recorder=True), ), (dict(specced=True), ), (dict(unshown=True), ),
                 (dict(recorder=True, unshown=True), ), (dict(specced=True, unshown=True), ),
                 (dict(recorder=True, specced=True, unshown=True), ))
        if any(mohreplay.OBJ_LEVELS in m["map"].lower() for m in index["maps"]):
            cases += ((dict(killer=recorder[:3], obj=True), ), )
        if any(mohreplay.slow_snipe(k) for k in frags):
            cases += ((dict(weapons=["sniper"], great=True), ), )
        state = lambda: self.game_state(window)
        for case, in cases:
            self.choose(window, **{"killer": "", "victim": "", "weapons": [], "recorder": False, "specced": False,
                                   "unshown": False, "obj": False, "great": False, **case})
            planned = mohreplay.planned_kills(index, window.kill_filter())[0]
            starts = [mohreplay.kill_span(index, k, start, end)[0] for k, start, end in planned]
            self.assertTrue(starts, case)
            # as demonextkill goes, from the start: the next more than 0.1 s on
            wanted, now = [], 0
            for start in starts:
                if start > now + 100:
                    wanted.append(start)
                    now = start
            wanted = wanted[:8]
            window.game.send("demoseek 0")
            self.assertTrue(wait_until(lambda: state().get("time", 1) < 500 and not state().get("seeking"), 30))
            landed = []
            for _ in wanted:
                last = state()["time"]
                window.game.send("demonextkill")
                # each more than 0.1 s on
                self.assertTrue(wait_until(lambda: not state().get("seeking") and state()["time"] > last + 100, 30),
                                (case, landed))
                landed.append(state()["time"])
            # the game's time comes in snapshots
            self.assertEqual(len(landed), len(wanted))
            self.assertTrue(all(abs(a - b) <= 100 for a, b in zip(landed, wanted)), (case, landed, wanted))

    def test_play_seek_and_record(self):
        window = self.window()
        self.real_demo(window)
        window.game.send("demoseek 2:00")
        self.assertTrue(wait_until(lambda: 120000 <= self.game_state(window).get("time", 0) < 125000
                                   and not self.game_state(window).get("seeking"), 30))

        self.settings.setValue("rec/size", "640x360")

        def setup(dialog):
            dialog.range.setChecked(True)
            dialog.start.setText("2:00")
            dialog.end.setText("2:03")
            dialog.pattern.setText("real")
        window.demo_items[self.demo].setSelected(True)
        with mock.patch.object(mohreplay.RecordDialog, "exec", lambda d: (setup(d), d.accept(), d.result())[-1]):
            window.record_demos()
        output = os.path.join(self.videos, "real.mp4")
        self.assertTrue(wait_until(lambda: window.jobs[0]["status"] not in ("waiting", "recording"), 120), "no video")
        self.assertEqual(window.jobs[0]["status"], "done", window.jobs[0]["item"].toolTip(2))
        probe = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "stream=codec_type,width,height,duration",
                                "-of", "json", output], capture_output=True, text=True, check=True)
        streams = json.loads(probe.stdout)["streams"]
        video = next(s for s in streams if s["codec_type"] == "video")
        self.assertEqual((video["width"], video["height"]), (640, 360))
        self.assertAlmostEqual(float(video["duration"]), 3, delta=0.1)
        self.assertTrue(any(s["codec_type"] == "audio" for s in streams))

    def test_record_with_motion_blur(self):
        """The renderer blurs, the game drawing the video's 60 frames a
        second."""
        window = self.window()
        self.real_demo(window)

        for key, value in (("size", "640x360"), ("fps", "60"), ("blur", "true")):
            self.settings.setValue("rec/" + key, value)

        def setup(dialog):
            dialog.range.setChecked(True)
            dialog.start.setText("2:00")
            dialog.end.setText("2:03")
            dialog.pattern.setText("blur")
        window.demo_items[self.demo].setSelected(True)
        with mock.patch.object(mohreplay.RecordDialog, "exec", lambda d: (setup(d), d.accept(), d.result())[-1]):
            window.record_demos()
        output = os.path.join(self.videos, "blur.mp4")
        self.assertTrue(wait_until(lambda: window.jobs[0]["status"] not in ("waiting", "recording"), 120), "no video")
        self.assertEqual(window.jobs[0]["status"], "done", window.jobs[0]["item"].toolTip(2))
        probe = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v", "-count_frames", "-show_entries",
                                "stream=r_frame_rate,nb_read_frames,duration", "-of", "json", output],
                               capture_output=True, text=True, check=True)
        video, = json.loads(probe.stdout)["streams"]
        self.assertEqual(video["r_frame_rate"], "60/1")
        self.assertAlmostEqual(int(video["nb_read_frames"]), 180, delta=2)
        self.assertAlmostEqual(float(video["duration"]), 3, delta=0.1)


if __name__ == "__main__":
    unittest.main()
