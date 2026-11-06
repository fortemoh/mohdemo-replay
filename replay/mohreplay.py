#!/usr/bin/env python3
"""mohdemo-replay: makes videos of the frags in Medal of Honor: Allied Assault
demos, from a list of demos with filters.

Records with the game (OpenMoHAA built from this repository), started with a
throwaway home folder, sending it console commands through its pipe
(com_pipefile) and reading what it reports in demostate.json. A double-click
on a demo plays it in the game, with the keys of BINDS.

    python3 mohreplay.py [--exe PATH] [--game FOLDER] [--demos FOLDER] [DEMO]
"""

import argparse
import bisect
import collections
import csv
import errno
import functools
import hashlib
import html
import json
import os
import posixpath
import re
import shutil
import sys
import zipfile
import tempfile

from PySide6.QtCore import (
    QDateTime, QElapsedTimer, QFileSystemWatcher, QLockFile, QObject, QProcess, QProcessEnvironment,
    QSettings, QStandardPaths, Qt, QTimer, QUrl, Signal,
)
from PySide6.QtGui import QActionGroup, QColor, QDesktopServices, QIcon, QPainter, QPalette, QPixmap
from PySide6.QtWidgets import (
    QAbstractItemView, QApplication, QButtonGroup, QCheckBox, QComboBox, QDialog, QDialogButtonBox,
    QDockWidget, QDoubleSpinBox, QFileDialog, QFormLayout, QGridLayout, QHBoxLayout, QHeaderView, QLabel, QLineEdit,
    QListWidget, QListWidgetItem, QMainWindow, QMenu, QMessageBox, QPushButton, QRadioButton, QSizePolicy, QSlider,
    QSpinBox, QStyle, QTabWidget, QToolBar, QToolButton, QTreeWidget, QTreeWidgetItem, QVBoxLayout, QWidget,
    QWidgetAction,
)

PIPE = "replay_pipe"
# the game's settings in its home folder, which is thrown away after each game
GAME_CONFIG = os.path.join("main", "configs", "omconfig.cfg")
KILL_BEFORE = 4000  # msec of a kill shown before it, as cl_demoKillBefore
KILL_AFTER = 2000  # and after it, as cl_demoKillAfter
# msec at most between a team kill and a kill of its killer that plays, for
# the team kill to play too, as the engine's DEMO_TEAMKILL_NEAR
TEAM_KILL_NEAR = 2000
MIN_LENGTH = 5 * 60000  # msec: shorter demos aren't listed, once indexed
# the weapons kills are filtered by, as the demo index names them, and in
# words, but grenades, whose kills aren't kills; other is anything else,
# and the death messages not known
WEAPONS = {"sniper": "Sniper", "rifle": "Rifle", "smg": "SMG", "mg": "StG / BAR", "shotgun": "Shotgun",
           "pistol": "Pistol", "rocket": "Bazooka", "melee": "Melee", "other": "Other"}
WEAPON_WORDS = {"sniper": "sniper", "rifle": "rifle", "smg": "SMG", "mg": "StG / BAR", "shotgun": "shotgun",
                "pistol": "pistol", "rocket": "bazooka", "melee": "melee", "other": "other weapons"}
WEAPON_TIPS = {"sniper": "Springfield and KAR98 sniper: \"was sniped by\"",
               "rifle": "M1 Garand and KAR98: \"was rifled by\"",
               "smg": "Thompson and MP40: \"was perforated by\"",
               "mg": "StG 44 and BAR, the game's machine guns: \"was machine-gunned by\"",
               "shotgun": "\"was hunted down by\", \"was pumped full of buckshot by\"",
               "pistol": "Colt 45, P38 and the silenced pistol: \"was gunned down by\"",
               "rocket": "Bazooka and panzerschreck: \"took …'s rocket\"",
               "melee": "Hit with a gun's butt: \"was bashed by\", \"was clubbed by\"",
               "other": "Explosions, falls, being crushed…, and death messages not known"}
# the demo list's columns
DEMO_COLUMNS = ["Demo", "Player", "Length", "Map", "Recorder", "Rules", "Date"]
COL_DEMO, COL_PLAYER, COL_LENGTH, COL_MAP, COL_RECORDER, COL_RULES, COL_DATE = range(len(DEMO_COLUMNS))
# the rules of a demo's levels, from mohdemoindex: realism servers change
# the speeds and the damage of weapons
RULES = {"default": "Default", "realism": "Realism", "both": "Both"}
# keys above 127 only: during a demo, the others only run +buttons
BINDS = {
    "PAUSE": "demopause",
    "LEFTARROW": "demoskip -5",
    "RIGHTARROW": "demoskip 5",
    "UPARROW": "demonextkill",
    "DOWNARROW": "demoprevkill",
    "PGUP": "demonextround",
    "PGDN": "demoprevround",
}
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MACOS = sys.platform == "darwin"
# FFmpeg's video options, {crf} from the quality slider
CODECS = {
    "H.264": "-c:v libx264 -preset medium -crf {crf} -pix_fmt yuv420p",
    "H.265": "-c:v libx265 -preset medium -crf {crf} -pix_fmt yuv420p -tag:v hvc1",
    "Hardware H.264": ("-c:v h264_videotoolbox -q:v {quality}" if MACOS else
                       "-vaapi_device /dev/dri/renderD128 -vf format=nv12,hwupload -c:v h264_vaapi -qp {crf}"),
}
# with motion blur, the renderer's: what moves blurs over all of a frame's
# time, at any frame rate, and the colors are as without it (r_temporal
# would make them stronger)
BLUR_CVARS = ["+set", "r_temporal", "1", "+set", "r_motionBlur", "1", "+set", "r_motionBlurShutter", "1",
              "+set", "r_motionBlurFps", "0", "+set", "r_saturation", "1"]


def clock(msec):
    seconds = max(0, msec) // 1000
    return f"{seconds // 60}:{seconds % 60:02d}"


def seconds(msec):
    return f"{max(0, msec) / 1000:.3f}"


def quoted(name):
    return '"' + name.replace('"', "'") + '"'


def parse_clock(text):
    """msec from "90", "1:30" or "1:30.5", None if it isn't a time."""
    try:
        parts = [float(p) for p in text.strip().split(":")]
    except ValueError:
        return None
    if not 1 <= len(parts) <= 3 or any(p < 0 for p in parts):
        return None
    total = 0
    for part in parts:
        total = total * 60 + part
    return int(total * 1000)


def game_program(path):
    """The program in a macOS app bundle (openmohaa.app), or path."""
    if path.rstrip("/").endswith(".app") and os.path.isdir(path):
        name = os.path.basename(path.rstrip("/"))[:-4]
        return os.path.join(path, "Contents", "MacOS", name)
    return path


def find_indexer(exe):
    """mohdemoindex, built next to the game or to its app bundle."""
    folders = [os.path.dirname(exe)]
    if os.path.basename(os.path.dirname(os.path.dirname(exe))) == "Contents":
        folders.append(os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(exe)))))
    return next((p for p in (os.path.join(f, "mohdemoindex") for f in folders) if os.path.isfile(p)),
                os.path.join(folders[0], "mohdemoindex"))


def find_ffmpeg():
    for path in (shutil.which("ffmpeg"), "/opt/homebrew/bin/ffmpeg", "/usr/local/bin/ffmpeg"):
        if path and os.path.isfile(path):
            return path
    return "ffmpeg"


def player_models(game):
    """The player models in the game files, by name in lower case as the
    game takes any, sorted: the models/player/*.tik in its main folder's
    pk3s or on their own, but the first-person arms (_fps)."""
    main = os.path.join(game or "", "main")
    names = []
    try:
        files = sorted(os.listdir(main))
    except OSError:
        return []
    for file in files:
        if file.lower().endswith(".pk3"):
            try:
                with zipfile.ZipFile(os.path.join(main, file)) as pk3:
                    names += pk3.namelist()
            except (OSError, zipfile.BadZipFile):
                pass
    for root, _, found in os.walk(os.path.join(main, "models", "player")):
        names += ["models/player/" + name for name in found]
    models = set()
    for name in names:
        folder, _, file = name.lower().rpartition("/")
        if folder == "models/player" and file.endswith(".tik") and not file.endswith("_fps.tik"):
            models.add(file[:-4])
    return sorted(models)


# the words starred out of the players' names and the messages in the game
# and in videos (cl_censor), however they're spelled: words a video can be
# flagged for
CENSOR_WORDS = ("nigger, nigga, nlgger, nlgga, negro, chink, gook, kike, wetback, beaner, paki, raghead, towelhead, "
                "faggot, fag, dyke, tranny, gay, homo, nazi, hitler, heil, swastika, holocaust, auschwitz, kkk, jew, "
                "retard")


def censored_words(settings):
    """The words starred out, as cl_censor takes them, "" for none."""
    if not is_on(settings.value("censor", "1")):
        return ""
    return " ".join(w for w in re.split(r"[\s,]+", settings.value("censor_words", CENSOR_WORDS) or "") if w)


# where the camera is for a demo's recorder and for the player they follow:
# {key: (label, cg_recordercamera or cg_followcamera)}, the first the default
RECORDER_CAMERAS = {"eyes": ("In their eyes", "0"), "behind": ("Behind them", "1")}
FOLLOWED_CAMERAS = {"behind": ("Behind them", "1"), "eyes": ("In their eyes", "2"),
                    "server": ("As the server shows them", "0")}


def cameras(settings):
    """The cameras chosen, [recorder's, followed player's], keys of
    RECORDER_CAMERAS and FOLLOWED_CAMERAS."""
    recorder = settings.value("camera_recorder", "eyes")
    followed = settings.value("camera_followed", "behind")
    return [recorder if recorder in RECORDER_CAMERAS else "eyes",
            followed if followed in FOLLOWED_CAMERAS else "behind"]


def camera_cvars(chosen):
    """The game's settings for cameras chosen (cameras())."""
    return [("cg_recordercamera", RECORDER_CAMERAS[chosen[0]][1]), ("cg_followcamera", FOLLOWED_CAMERAS[chosen[1]][1])]


# how a player model's name tells its team, as the game goes by it
# (Player::InitModel)
ALLIES_MODELS = ("american", "allied")
AXIS_MODELS = ("german", "axis", "it", "sc")


def team_models(models, axis):
    """The ones of models for a team, Axis if axis, else Allies, by their
    names: one named for neither team, someone's own, can be either's."""
    other = ALLIES_MODELS if axis else AXIS_MODELS
    return [model for model in models if not model.startswith(other)]


def followed_models(settings):
    """The models chosen for the player followed when they're allies, and
    axis: "" for theirs."""
    return [settings.value("followed_allies", "") or "", settings.value("followed_axis", "") or ""]


def without_colors(name):
    """name without its colour codes, ^ and a letter or digit."""
    out, i = [], 0
    while i < len(name):
        if name[i] == "^" and i + 1 < len(name) and name[i + 1].isascii() and name[i + 1].isalnum():
            i += 2
            continue
        out.append(name[i])
        i += 1
    return "".join(out)


@functools.lru_cache(maxsize=4096)
def clean_name(name):
    """The engine's rule for names: without ^ and a letter or digit, only
    printable ASCII, any case (Q_CleanStr), spaces as one."""
    printable = "".join(c for c in without_colors(name) if " " <= c <= "~")
    return " ".join(word for word in printable.lower().split(" ") if word)


@functools.lru_cache(maxsize=256)
def part_pattern(part):
    """A search's part as a pattern: a * stands for any one character."""
    return re.compile(re.escape(part).replace(r"\*", "."))


def has_part(text, part):
    """Whether text has part in it, a * in part standing for any one
    character (gr*m: grim, gr1m), as demofilter takes it."""
    return part in text if "*" not in part else part_pattern(part).search(text) is not None


# what the names of the objective maps' levels have, any case (obj/obj_team4,
# OBJ/obj_Team2, obj/harbor...), and not those of the deathmatch maps (dm/)
OBJ_LEVELS = "obj/"
# what Great leaves out: sniper frags seen from their fragger's
# view with the scope on for longer than this, in msec, and the fragger at
# this speed or slower, the slow scoped shots of someone standing still,
# but not those of a streak: with another frag of the fragger at most
# GREAT_STREAK msec before or after, as one clip shows them; and the frags
# of victims who had given no input for over GREAT_IDLE msec, AFK
GREAT_SCOPE, GREAT_SPEED, GREAT_STREAK, GREAT_IDLE = 1000, 30, 5000, 5000


def kill_levels(index):
    """Gives each kill of index the name of its level, "level", lower case:
    the last one loaded before it, "" before the first, as the engine looks
    it up. Returns index."""
    maps = index.get("maps", [])
    times = [m["time"] for m in maps]
    for kill in index.get("kills", []):
        i = bisect.bisect_right(times, kill["time"]) - 1
        kill["level"] = maps[i]["map"].lower() if i >= 0 else ""
    return index


def kill_streaks(index):
    """Tells each kill of index whether it's in a streak, "streak": with
    another kill of its killer at most GREAT_STREAK msec before or after it,
    as the engine looks them up (CL_DemoInMultiKill); team kills, grenade
    kills, the deaths of the player shown and the kills of idle victims
    don't count. Returns index."""
    deaths = deaths_shown(index)

    def counts(kill):
        return (kill["killerName"] and not kill.get("teamKill") and not is_grenade(kill) and id(kill) not in deaths
                and not idle_victim(kill))
    times = {}
    for kill in index.get("kills", []):
        if counts(kill):
            times.setdefault(clean_name(kill["killerName"]), []).append(kill["time"])
    for when in times.values():
        when.sort()
    for kill in index.get("kills", []):
        when = times.get(clean_name(kill["killerName"]), []) if kill["killerName"] and not is_grenade(kill) else []
        t = kill["time"]
        near = bisect.bisect_right(when, t + GREAT_STREAK) - bisect.bisect_left(when, t - GREAT_STREAK)
        # other than itself
        kill["streak"] = near > (1 if counts(kill) else 0)
    return index


def kill_specced(index):
    """Tells each kill of index whether its killer is the player shown then,
    "shown", the one the recorder follows as a spectator, "specced", as the
    engine looks them up (demofilter recorder and specced), and whether it can
    be seen from behind a killer not shown, "behind" (demofilter unshown).
    Returns index."""
    at = watching(index)
    for kill in index.get("kills", []):
        watch = at(kill["time"]) if kill["killerName"] else None
        kill["shown"] = bool(watch and is_shown(watch[0], kill["killer"], kill["killerName"]))
        kill["specced"] = kill["shown"] and bool(watch[0].get("followed"))
        kill["behind"] = not kill["shown"] and unshown_ok(kill)
    return index


def slow_snipe(kill):
    """Whether kill is one Great leaves out: a sniper frag its
    index saw from the fragger's view, made with the scope on for over
    GREAT_SCOPE msec at GREAT_SPEED or slower, not in a streak (given by
    kill_streaks). Not one when not seen, as in an index made before scopes
    were."""
    return (weapon_of(kill) == "sniper" and kill.get("scope", -1) > GREAT_SCOPE and kill.get("speed", 0) <= GREAT_SPEED
            and not kill.get("streak"))


def idle_victim(kill):
    """Whether kill's victim had given no input for over GREAT_IDLE msec
    before it, AFK, as its index saw them. Not when not seen."""
    return kill.get("idle", -1) > GREAT_IDLE


def poor_frag(kill):
    """Whether Great leaves kill out: a slow snipe or the frag of an
    idle victim."""
    return slow_snipe(kill) or idle_victim(kill)


def same_player(a, b):
    return clean_name(a) == clean_name(b)


def is_grenade(kill):
    """Whether kill was made with a grenade or shrapnel: as its index tells,
    or its message for an index made before weapons were."""
    if "weapon" in kill:
        return bool(kill.get("grenade"))
    return bool(kill.get("grenade")) or "grenade" in kill.get("text", "").lower() or "shrapnel" in kill.get("text", "").lower()


def weapon_of(kill):
    """What kill was made with, a key of WEAPONS: other for what isn't
    known, as in indexes made before weapons were."""
    weapon = kill.get("weapon", "")
    return weapon if weapon in WEAPONS else "other"


def or_list(words):
    """"a, b or c"."""
    words = list(words)
    return ", ".join(words[:-1]) + " or " + words[-1] if len(words) > 1 else "".join(words)


class KillFilter:
    """The kills listed and recorded: by players whose names have one of
    killers in them, while they're shown, as a player's kills are, of players
    whose names have one of victims, made with one of weapons (keys of
    WEAPONS), on the objective maps only if obj, great ones only if great (not
    slow snipes nor frags of AFK players). Who they're from, any of these: the
    recorder's own if recorder, those of the players the recorder spectates if
    specced, those of players not shown, seen from behind them (unshown_ok), if
    unshown. Names are parts of names as the search takes its words, a *
    standing for any one character; none is anyone, no weapons any weapon. The
    game is told it with demofilter."""

    def __init__(self, killers=(), victims=(), weapons=(), obj=False, great=False, recorder=False, specced=False,
                 unshown=False):
        self.obj = bool(obj)
        self.great = bool(great)
        self.recorder = bool(recorder)
        self.specced = bool(specced)
        self.unshown = bool(unshown)
        # as given, for the game and the labels
        self.names = [n.strip() for n in killers if clean_name(n)]
        self.victim_names = [n.strip() for n in victims if clean_name(n)]
        self.killers = [clean_name(n) for n in self.names]
        self.victims = [clean_name(n) for n in self.victim_names]
        self.weapons = [w for w in WEAPONS if w in weapons]

    def __bool__(self):
        return bool(self.killers or self.victims or self.weapons or self.obj or self.great or self.who_chosen())

    def who_chosen(self):
        """Whether it says who its kills are from."""
        return self.recorder or self.specced or self.unshown

    def by_shown(self):
        """Whether its kills are only those of players shown or seen from
        behind them, not anyone's as the demo shows them."""
        return bool(self.killers) or self.who_chosen()

    def has_killer(self, killer):
        """Whether a killer's clean name is one of its, anyone if none."""
        return not self.killers or any(has_part(killer, part) for part in self.killers)

    def has_who(self, shown, specced, behind):
        """Whether a kill made while its killer is shown or not, specced or not
        (kill_specced), that can be seen from behind them or not (unshown_ok),
        is from who it's from. Of its killers only while shown, without any."""
        if self.who_chosen():
            return (self.specced if specced else self.recorder) if shown else self.unshown and bool(behind)
        return shown or not self.killers

    def has(self, victim, weapon, level="", poor=False):
        """Whether a kill of a victim (clean name) made with weapon in a level
        (lower case), poor (poor_frag) or not, is one of its, whoever the
        killer."""
        return ((not self.weapons or weapon in self.weapons)
                and (not self.victims or any(has_part(victim, part) for part in self.victims))
                and (not self.obj or OBJ_LEVELS in level) and not (self.great and poor))

    def keeps(self, kill, killer=True):
        """Whether kill, given its level (kill_levels) and who it's from
        (kill_specced), is one of its, whoever the killer unless killer."""
        return ((not killer or self.has_killer(clean_name(kill["killerName"])))
                and self.has(clean_name(kill.get("victimName", "")), weapon_of(kill), kill.get("level", ""),
                             poor_frag(kill))
                and self.has_who(kill.get("shown", True), kill.get("specced", False), kill.get("behind", False)))

    def commands(self):
        """The demofilter commands telling it to the game."""
        return (["demofilter clear"]
                + ["demofilter killer " + quoted(name) for name in self.names]
                + ["demofilter victim " + quoted(name) for name in self.victim_names]
                + (["demofilter weapon " + " ".join(self.weapons)] if self.weapons else [])
                + (["demofilter level " + OBJ_LEVELS] if self.obj else [])
                + ([f"demofilter great {GREAT_SCOPE} {GREAT_SPEED} {GREAT_STREAK} {GREAT_IDLE}"] if self.great else [])
                + (["demofilter recorder"] if self.recorder else [])
                + (["demofilter specced"] if self.specced else [])
                + ((["demofilter unshown"] if self.recorder or self.specced else ["demofilter unshown only"])
                   if self.unshown else []))

    def words(self):
        """What it keeps after "frags": " by pstn or Phil of Bob with sniper",
        "" for all."""
        text = " by " + or_list(self.names) if self.names else ""
        text += " of " + or_list(self.victim_names) if self.victim_names else ""
        text += " with " + or_list(WEAPON_WORDS[w] for w in self.weapons) if self.weapons else ""
        text += " on obj maps" if self.obj else ""
        who = [word for chosen, word in ((self.recorder, "the recorder"), (self.specced, "specced"),
                                         (self.unshown, "unspecced")) if chosen]
        text += " from " + or_list(who).replace(" or ", " and ") if who else ""
        return text + (", great ones" if self.great else "")


def watching(index):
    """The player shown at a time, in its level, as the engine looks them up
    (CL_DemoWatchFor): a function of msec giving (watched entry, from, to),
    None when no one is."""
    watched = index.get("watched", [])
    times = [w["time"] for w in watched]
    map_times = [m["time"] for m in index.get("maps", [])]

    def at(time):
        i = bisect.bisect_right(times, time) - 1
        mi = bisect.bisect_right(map_times, time) - 1
        map_start = map_times[mi] if mi >= 0 else 0
        if i < 0 or watched[i]["time"] < map_start:
            return None
        end = watched[i + 1]["time"] if i + 1 < len(watched) else index.get("duration", 0)
        if mi + 1 < len(map_times):
            end = min(end, map_times[mi + 1])
        return watched[i], max(watched[i]["time"], map_start), end
    return at


def is_shown(watch, client, name):
    """Whether a player, by client number or name, is the one watched: one
    who changed names since is still the same client."""
    return (watch["client"] >= 0 and watch["client"] == client) or bool(name) and same_player(watch["name"], name)


def shown_kills(index):
    """The kills made while their killer is the player shown, as demoonly
    plays a player's kills (at other times the demo shows someone else):
    [(kill, from, to)], with when they're shown around it."""
    at = watching(index)
    shown = []
    for kill in index.get("kills", []):
        watch = at(kill["time"]) if kill["killerName"] else None
        if watch and is_shown(watch[0], kill["killer"], kill["killerName"]):
            shown.append((kill, watch[1], watch[2]))
    return shown


# how long the killer of a kill not shown is in the demo before it to see
# it from behind them (demofilter unshown, DEMOFILTER_UNSHOWN_SEEN)
UNSHOWN_SEEN = 3000


def unshown_ok(kill):
    """Whether a kill whose killer isn't shown can be seen from behind them
    (demofilter unshown): their entity is in the demo for UNSHOWN_SEEN msec
    before it (seen), and their victim in view then (idle)."""
    return kill.get("killer", -1) >= 0 and kill.get("seen", -1) >= UNSHOWN_SEEN and kill.get("idle", -1) >= 0


def unshown_kills(index, shown):
    """The kills not in shown (ids of kills of shown_kills) that unshown_ok
    keeps, [(kill, from, to)], as shown_kills: from when their killer is in
    the demo to the end of their level."""
    levels = [level["time"] for level in index.get("maps", [])]
    unshown = []
    for kill in index.get("kills", []):
        if id(kill) not in shown and unshown_ok(kill):
            i = bisect.bisect_right(levels, kill["time"])
            end = levels[i] if i < len(levels) else index.get("duration", 0)
            unshown.append((kill, kill["time"] - kill["seen"], end))
    return unshown


def filter_shown_kills(index, kill_filter):
    """As shown_kills, the kills of a filter with its killers while shown:
    with the others unshown_kills keeps, in their order, or only those when
    it only wants them."""
    shown = shown_kills(index)
    if not kill_filter.unshown:
        return shown
    unshown = unshown_kills(index, {id(k) for k, _, _ in shown})
    if not (kill_filter.recorder or kill_filter.specced):
        return unshown
    return sorted(shown + unshown, key=lambda x: x[0]["time"])


def deaths_shown(index):
    """The ids of the kills whose victim is the player shown at the time:
    the recorder's deaths, or the followed player's, which aren't kills
    (CL_DemoDeathShown)."""
    at = watching(index)
    deaths = set()
    for kill in index.get("kills", []):
        watch = at(kill["time"])
        if watch and is_shown(watch[0], kill.get("victim", -1), kill.get("victimName", "")):
            deaths.add(id(kill))
    return deaths


def team_kills_with(kills, frags):
    """The team kills among kills, [(kill, from, to)], that play with frags,
    as demoonly plays them: those at most TEAM_KILL_NEAR from a kill by
    their killer in frags, like a teammate hit while shooting at an enemy."""
    times = {}
    for frag in frags:
        times.setdefault(clean_name(frag["killerName"]), []).append(frag["time"])
    for when in times.values():
        when.sort()
    kept = []
    for kill in kills:
        if not kill[0].get("teamKill") or not kill[0]["killerName"]:
            continue
        when = times.get(clean_name(kill[0]["killerName"]), [])
        i = bisect.bisect_left(when, kill[0]["time"] - TEAM_KILL_NEAR)
        if i < len(when) and when[i] <= kill[0]["time"] + TEAM_KILL_NEAR:
            kept.append(kill)
    return kept


def player_kills(player):
    """"name (kills)" from a summary's [name, kills]."""
    name, kills = player
    return f"{name} ({kills})"


def walk_demos(folder):
    """{name: path} of the demos in folder and the folders in it, a name
    being the path in folder without .dm3, with / between folders as the
    game takes it, and the folders looked in, but hidden ones."""
    demos, folders = {}, []
    for root, dirs, files in os.walk(folder):
        dirs[:] = [d for d in dirs if not d.startswith(".")]
        folders.append(root)
        sub = os.path.relpath(root, folder).replace(os.sep, "/")
        for n in files:
            if n.lower().endswith(".dm3"):
                demos[n[:-4] if sub == "." else sub + "/" + n[:-4]] = os.path.join(root, n)
    return demos, folders


def list_demos(folder):
    return walk_demos(folder)[0]


def summarize(index):
    """What the demo list shows of an index: its maps, length, recorder and
    players, {clean name: [name, kills]}, counting the kills made while
    they're shown, and its kills for the kill filter: [(killer, clean
    killer, clean victim, weapon, whether made while the killer is shown,
    level, whether poor (poor_frag), whether specced (kill_specced),
    whether unshown_ok)]."""
    kill_specced(kill_streaks(kill_levels(index)))
    players = {}

    def add(name, kills=0):
        if name:
            players.setdefault(clean_name(name), [name, 0])[1] += kills
    add(index.get("recorder", {}).get("name", ""))
    for watched in index.get("watched", []):
        add(watched["name"])
    for kill in index.get("kills", []):
        add(kill["killerName"])
        add(kill["victimName"])
    deaths = deaths_shown(index)
    shown = {id(kill) for kill, _, _ in shown_kills(index)}
    kills = [kill for kill in index.get("kills", [])
             if not kill.get("teamKill") and not is_grenade(kill) and id(kill) not in deaths]
    for kill in kills:
        if id(kill) in shown:
            add(kill["killerName"], 1)
    # "realism" or "default" when its levels tell one, "both", or ""
    kinds = {m.get("rules") for m in index.get("maps", [])} & {"realism", "default"}
    rules = "both" if len(kinds) == 2 else kinds.pop() if kinds else ""
    return {"maps": [m["map"] for m in index.get("maps", [])], "duration": index.get("duration", 0),
            "recorder": index.get("recorder", {}).get("name", ""), "players": players, "rules": rules,
            "kills": [(kill["killerName"], clean_name(kill["killerName"]), clean_name(kill.get("victimName", "")),
                       weapon_of(kill), id(kill) in shown, kill["level"], poor_frag(kill), kill["specced"],
                       unshown_ok(kill))
                      for kill in kills]}


def filter_kills(summary, kill_filter):
    """The kills of a demo the filter keeps, from its summary: how many, and
    their killers, {clean name: [name, kills]}."""
    count, killers = 0, {}
    for name, killer, victim, weapon, shown, level, poor, specced, seen in summary.get("kills", []):
        if (kill_filter.has_who(shown, specced, seen) and kill_filter.has_killer(killer)
                and kill_filter.has(victim, weapon, level, poor)):
            count += 1
            if name:
                killers.setdefault(killer, [name, 0])[1] += 1
    return count, killers


def search_parts(text):
    """The searches of the demo filter: the words of each part separated by
    commas, cleaned like names. A demo is found by any search with all of its
    words."""
    parts = ([w for w in (clean_name(w) for w in part.split()) if w] for part in text.split(","))
    return [words for words in parts if words]


def search_demo(name, summary, searches):
    """The players of a demo the searches find, {clean name: [name, kills]},
    or None when no search has all its words in the demo's name, a level or a
    player's name."""
    players = summary.get("players", {})
    maps = " ".join(summary.get("maps", [])).lower()
    found, any_found = {}, False
    for words in searches:
        hits = {}
        for word in words:
            if has_part(name.lower(), word) or has_part(maps, word):
                continue
            keys = [key for key in players if has_part(key, word)]
            if not keys:
                break
            hits.update((key, players[key]) for key in keys)
        else:
            any_found = True
            found.update(hits)
    return found if any_found else None


class Library(QObject):
    """What's in every demo of the folder, without playing them: mohdemoindex
    (built next to the game) indexes them in the background, and the indexes
    are kept in the cache folder until the demo or mohdemoindex changes."""

    BATCH = 20  # demos per mohdemoindex run
    WORKERS = max(1, min(4, (os.cpu_count() or 2) // 2))

    def __init__(self, on_change, parent=None):
        super().__init__(parent)
        self.on_change = on_change
        self.demos = {}  # name: summarize()
        self.paths = {}  # name: path of the demos to index
        self.folder = self.cache = self.tool = None
        self.waiting = []
        self.busy = set()
        self.failed = set()
        self.workers = []
        self.total = 0
        self.watcher = QFileSystemWatcher(self)
        self.rescan_timer = QTimer(self, singleShot=True, interval=1000, timeout=self.rescan)
        self.watcher.directoryChanged.connect(lambda _: self.rescan_timer.start())
        self.changed_timer = QTimer(self, singleShot=True, interval=300, timeout=lambda: self.on_change())

    def open(self, folder, exe):
        """The demos of folder, indexed with the mohdemoindex next to exe."""
        self.stop()
        self.demos, self.failed = {}, set()
        if self.watcher.directories():
            self.watcher.removePaths(self.watcher.directories())
        self.folder = os.path.abspath(folder) if folder else None
        self.tool = find_indexer(exe) if exe else None
        if not self.folder or not os.path.isdir(self.folder):
            self.on_change()
            return
        # a folder of indexes for each folder of demos
        key = hashlib.sha1(self.folder.encode()).hexdigest()[:12]
        base = QStandardPaths.writableLocation(QStandardPaths.GenericCacheLocation)
        self.cache = os.path.join(base, "mohdemo-replay", "index", key)
        self.rescan()

    def rescan(self):
        """Loads the indexes kept, and indexes the demos without one."""
        if not self.folder or not os.path.isdir(self.folder):
            return
        demos, folders = walk_demos(self.folder)
        # the folders in it too, for demos added there
        watched = set(self.watcher.directories())
        if set(folders) - watched:
            self.watcher.addPaths(sorted(set(folders) - watched))
        if watched - set(folders):
            self.watcher.removePaths(sorted(watched - set(folders)))
        try:
            made = os.path.getmtime(self.tool) if self.tool and os.path.isfile(self.tool) else None
        except OSError:
            made = None
        stale = []
        for name, path in demos.items():
            try:
                mtime = os.path.getmtime(path)
                kept = os.path.getmtime(os.path.join(self.cache, name + ".json"))
            except OSError:
                mtime, kept = 0, None
            if kept is not None and kept >= mtime and (made is None or kept >= made):
                if name not in self.demos:
                    self.load(name)
            elif made is not None and name not in self.busy and name not in self.failed:
                stale.append((mtime, name))
            elif kept is not None and name not in self.demos:
                self.load(name)  # out of date, but better than nothing
        for name in list(self.demos):
            if name not in demos:
                del self.demos[name]
                try:
                    os.remove(os.path.join(self.cache, name + ".json"))
                except OSError:
                    pass
        # newest first
        self.paths = demos
        self.waiting = [name for _, name in sorted(stale, reverse=True)]
        self.total = len(self.waiting) + len(self.busy)
        self.start_workers()
        self.on_change()

    def index(self, name):
        """The index kept of a demo, None if there's none."""
        try:
            with open(os.path.join(self.cache, name + ".json"), encoding="utf-8") as f:
                return json.load(f)
        except (OSError, TypeError, ValueError):
            return None

    def load(self, name):
        try:
            with open(os.path.join(self.cache, name + ".json"), encoding="utf-8") as f:
                self.demos[name] = summarize(json.load(f))
        except (OSError, ValueError, KeyError, TypeError, AttributeError):
            self.demos.pop(name, None)

    def start_workers(self):
        while self.waiting and len(self.workers) < self.WORKERS:
            # the demos of one folder, as mohdemoindex names an index after
            # the demo's file, in the folder of indexes for it
            folder = posixpath.dirname(self.waiting[0])
            batch = [n for n in self.waiting if posixpath.dirname(n) == folder][:self.BATCH]
            names = set(batch)
            self.waiting = [n for n in self.waiting if n not in names]
            self.busy |= names
            output = os.path.join(self.cache, folder)
            os.makedirs(output, exist_ok=True)
            worker = QProcess(self)
            worker.readyReadStandardOutput.connect(lambda w=worker, f=folder: self.indexed(w, f))
            worker.finished.connect(lambda *_, w=worker, n=names, f=folder: self.worker_done(w, n, f))
            worker.errorOccurred.connect(lambda error, w=worker, n=names, f=folder:
                                         error == QProcess.FailedToStart and self.worker_done(w, n, f))
            self.workers.append(worker)
            worker.start(self.tool, [output, *(self.paths[n] for n in batch)])
            try:
                # behind the game
                os.setpriority(os.PRIO_PROCESS, worker.processId(), 10)
            except (OSError, AttributeError):
                pass

    def indexed(self, worker, folder):
        for line in bytes(worker.readAllStandardOutput()).decode("utf-8", "replace").splitlines():
            if line:
                self.load(posixpath.join(folder, line))
        self.changed_timer.start()

    def worker_done(self, worker, names, folder):
        if worker not in self.workers:
            return
        self.indexed(worker, folder)
        self.workers.remove(worker)
        self.busy -= names
        # not indexed: not tried again until the folder is opened again
        self.failed |= {name for name in names if name not in self.demos}
        worker.deleteLater()
        self.start_workers()
        self.changed_timer.start()

    def progress(self):
        """(indexed, to index) while indexing, None when done."""
        left = len(self.waiting) + len(self.busy)
        return (self.total - left, self.total) if left else None

    def stop(self):
        workers, self.workers = self.workers, []
        self.waiting, self.busy = [], set()
        for worker in workers:
            worker.kill()
            worker.waitForFinished(1000)


class SortItem(QTreeWidgetItem):
    """Sorts by the number kept in a column's UserRole, if any; folders
    first, by name, whichever way the rest is sorted."""

    def __lt__(self, other):
        tree = self.treeWidget()
        column = tree.sortColumn() if tree else 0
        if isinstance(self, FolderItem) or isinstance(other, FolderItem):
            if isinstance(self, FolderItem) and isinstance(other, FolderItem):
                less = self.folder.lower() < other.folder.lower()
            else:
                less = isinstance(self, FolderItem)
            # sorting the other way asks other < self
            return less if not tree or tree.header().sortIndicatorOrder() == Qt.AscendingOrder else not less
        mine, theirs = self.data(column, Qt.UserRole), other.data(column, Qt.UserRole)
        if isinstance(mine, (int, float)) and isinstance(theirs, (int, float)):
            return mine < theirs
        return self.text(column).lower() < other.text(column).lower()


class FolderItem(SortItem):
    """A folder of demos in the list, with those in it."""

    def __init__(self, folder):
        super().__init__([""] * len(DEMO_COLUMNS))
        self.folder = folder


def kept_config():
    """Where the game's settings are kept from one game to the next."""
    return os.path.join(QStandardPaths.writableLocation(QStandardPaths.GenericConfigLocation),
                        "mohdemo-replay", "omconfig.cfg")


def app_setting(line):
    """Whether a line of the game's config is the app's own, set again at each
    start: its keys' binds and its pipe. They aren't kept, so other games run
    from the game folder don't get them."""
    words = line.split(None, 2)
    return len(words) > 1 and (words[0].lower() == "bind" and words[1].upper() in BINDS
                               or words[0].lower() == "seta" and words[1].lower() == "com_pipefile")


class Game:
    """The game process, its throwaway home folder and its command pipe. It
    starts with the settings kept from earlier games, and keeps those changed
    in it unless keep_config is False."""

    QUIT_WAIT = 3000  # msec given to quit, then to SIGTERM
    TERM_WAIT = 2000

    def __init__(self, on_output, on_exit, keep_config=True):
        self.on_output = on_output
        self.on_exit = on_exit
        self.keep_config = keep_config
        self.process = None
        self.home = None
        self.queue = []
        self.driver = None
        self.args = None
        self.started = QElapsedTimer()
        self.flusher = QTimer(interval=100, timeout=self.flush)

    def running(self):
        return self.process is not None and self.process.state() != QProcess.NotRunning

    def start(self, exe, game, demos, width, height, extra=(), driver=None, viewmodel=None, forcemodel=None,
              kill_before=None, kill_after=None, videos=None, followed=("", ""), censor="", camera=("eyes", "behind")):
        """videos: the folder its videos go to, instead of its home; followed:
        the models of the player followed when allies and axis, "" for
        theirs; censor: the words starred out (cl_censor); camera: the
        cameras (cameras())."""
        self.cleanup()
        self.game = game
        self.home = tempfile.mkdtemp(prefix="mohreplay-")
        main = os.path.join(self.home, "main")
        os.makedirs(main)
        os.symlink(os.path.abspath(demos), os.path.join(main, "demos"))
        if videos:
            os.symlink(os.path.abspath(videos), os.path.join(main, "videos"))
        config = os.path.join(self.home, GAME_CONFIG)
        os.makedirs(os.path.dirname(config))
        game_cfg = os.path.join(game, GAME_CONFIG)
        src = game_cfg if os.path.isfile(game_cfg) else (
            os.path.join(game, "main", "omconfig.cfg") if os.path.isfile(os.path.join(game, "main", "omconfig.cfg")) else kept_config()
        )
        try:
            shutil.copyfile(src, config)
        except OSError:
            pass  # none kept yet
        vm = "1" if (viewmodel is None or str(viewmodel) in ("1", "true", "True")) else "0"
        fm = "1" if (forcemodel is None or str(forcemodel) in ("1", "true", "True")) else "0"
        kb = str(kill_before if kill_before is not None else "4.0")
        ka = str(kill_after if kill_after is not None else "2.0")
        with open(os.path.join(main, "replay.cfg"), "w") as f:
            f.writelines(f'bind {key} "{command}"\n' for key, command in BINDS.items())

        # the settings forced: set after the ones kept are read, they win,
        # and extra comes after, as the recorder's own
        self.args = [exe, "+set", "fs_basepath", game, "+set", "fs_homepath", self.home,
                     "+set", "com_pipefile", PIPE, "+set", "cl_demoFiles", "1",
                     "+set", "cg_drawviewmodel", vm, "+set", "cg_fov", "80",
                     "+set", "r_uselod", "0",
                     "+set", "cg_forceModel", fm,
                     "+set", "dm_playermodel", "allied_airborne",
                     "+set", "dm_playergermanmodel", "german_winter_1",
                     *(arg for cvar, model in zip(("cg_followedModel", "cg_followedGermanModel"), followed) if model
                       for arg in ("+set", cvar, model)),
                     *(("+set", "cl_censor", censor) if censor else ()),
                     *(arg for cvar, value in camera_cvars(camera) for arg in ("+set", cvar, value)),
                     "+set", "cl_demoKillBefore", kb,
                     "+set", "cl_demoKillAfter", ka,
                     "+set", "r_swapInterval", "1", "+set", "com_maxfps", "0",
                     "+set", "r_fullscreen", "0", "+set", "r_mode", "-1",
                     "+set", "r_customwidth", str(width), "+set", "r_customheight", str(height),
                     "+exec", "replay.cfg", *extra]
        # Wayland first, then XWayland if it doesn't start
        self.driver = driver or os.environ.get("SDL_VIDEODRIVER") or ("wayland" if sys.platform.startswith("linux") else "")
        self.launch()

    def launch(self):
        self.process = QProcess()
        self.process.setProcessChannelMode(QProcess.MergedChannels)
        env = QProcessEnvironment.systemEnvironment()
        if self.driver:
            env.insert("SDL_VIDEODRIVER", self.driver)
        self.process.setProcessEnvironment(env)
        self.process.setWorkingDirectory(os.path.dirname(self.args[0]))
        self.process.readyReadStandardOutput.connect(self.read_output)
        self.process.finished.connect(self.finished)
        self.process.errorOccurred.connect(self.failed)
        self.process.start(self.args[0], self.args[1:])
        self.started.start()
        self.flusher.start()

    def read_output(self):
        text = bytes(self.process.readAllStandardOutput()).decode("latin-1")
        for line in text.splitlines():
            self.on_output(line)

    def failed(self, error):
        if error == QProcess.FailedToStart:
            self.on_output("ERROR: couldn't start " + self.args[0])
            self.flusher.stop()
            self.on_exit()

    def finished(self):
        quick = self.started.elapsed() < 5000
        if quick and self.driver == "wayland" and not os.environ.get("SDL_VIDEODRIVER") and self.process.exitCode():
            self.on_output("The game didn't start on Wayland, trying XWayland")
            self.driver = "x11"
            self.launch()
            return
        self.flusher.stop()
        self.queue.clear()
        self.save_config()
        self.on_exit()

    def send(self, *commands):
        """Commands sent together are written at once, so the game runs them
        in the same frame."""
        if self.running():
            self.queue.extend(commands)
            self.flush()

    def flush(self):
        """Writes the queued commands without ever waiting for the game."""
        if not self.queue or not self.home:
            return
        try:
            fd = os.open(os.path.join(self.home, "main", PIPE), os.O_WRONLY | os.O_NONBLOCK)
        except OSError:
            return  # the game isn't reading yet
        try:
            data = "".join(c + "\n" for c in self.queue).encode("latin-1", "replace")
            written = os.write(fd, data)
            if written == len(data):
                self.queue.clear()
            else:
                # the pipe is full: keep what didn't fit, whole commands only
                done = data[:written].count(b"\n")
                del self.queue[:done]
        except OSError as e:
            if e.errno not in (errno.EAGAIN, errno.EPIPE):
                raise
        finally:
            os.close(fd)

    def read_json(self, name):
        if not self.home:
            return None
        try:
            with open(os.path.join(self.home, "main", name), encoding="utf-8") as f:
                return json.load(f)
        except (OSError, ValueError):
            return None

    def stop(self):
        """Asks the game to quit, then makes it, as it may not answer."""
        if self.running():
            # quitting now, no retry and no message
            self.process.finished.disconnect(self.finished)
            self.send("quit")
            if not self.process.waitForFinished(self.QUIT_WAIT):
                self.process.terminate()
                if not self.process.waitForFinished(self.TERM_WAIT):
                    self.process.kill()
                    self.process.waitForFinished(2000)
        self.cleanup()

    def save_config(self):
        """Keeps the settings the game wrote (it does as soon as one changes)
        for the next games and in the game main folder."""
        if not self.keep_config or not self.home:
            return
        try:
            with open(os.path.join(self.home, GAME_CONFIG), encoding="latin-1", newline="") as f:
                text = "".join(line for line in f if not app_setting(line))
        except OSError:
            return
        paths = [kept_config()]
        if getattr(self, "game", None):
            paths += [os.path.join(self.game, GAME_CONFIG), os.path.join(self.game, "main", "omconfig.cfg")]
        for path in paths:
            try:
                os.makedirs(os.path.dirname(path), exist_ok=True)
                with open(path + ".new", "w", encoding="latin-1", newline="") as f:
                    f.write(text)
                os.replace(path + ".new", path)
            except OSError:
                pass

    def cleanup(self):
        if self.home:
            self.save_config()
            for name in ("demos", "videos"):
                link = os.path.join(self.home, "main", name)
                if os.path.islink(link):
                    os.unlink(link)
            shutil.rmtree(self.home, ignore_errors=True)
            self.home = None


def theme_icon(name, fallback=None):
    """The desktop's icon of that name, else fallback (a QStyle standard
    pixmap or a QIcon), else none."""
    if isinstance(fallback, QStyle.StandardPixmap):
        fallback = QApplication.style().standardIcon(fallback)
    return QIcon.fromTheme(name, fallback or QIcon())


def record_icon():
    """A red dot."""
    pixmap = QPixmap(32, 32)
    pixmap.fill(Qt.transparent)
    painter = QPainter(pixmap)
    painter.setRenderHint(QPainter.Antialiasing)
    painter.setPen(Qt.NoPen)
    painter.setBrush(QColor("#da4453"))
    painter.drawEllipse(5, 5, 22, 22)
    painter.end()
    return QIcon(pixmap)


def heading(text):
    """A label in bold."""
    label = QLabel(text)
    font = label.font()
    font.setBold(True)
    label.setFont(font)
    return label


def faint(label):
    """label, in the color of placeholder text."""
    palette = label.palette()
    palette.setColor(QPalette.WindowText, palette.color(QPalette.PlaceholderText))
    label.setPalette(palette)
    return label


def toggle_button(text, tip):
    """A button that stays on or off, on tinted with the desktop's color."""
    button = QPushButton(text)
    button.setCheckable(True)
    button.setToolTip(tip)
    on = QApplication.palette().highlight().color()
    off = QApplication.palette().windowText().color()
    tint = f"{on.red()},{on.green()},{on.blue()}"
    button.setStyleSheet(
        f"QPushButton {{padding: 5px 10px; border: 1px solid rgba({off.red()},{off.green()},{off.blue()},70); border-radius: 3px;}}"
        f"QPushButton:hover {{border-color: rgba({tint},160);}}"
        f"QPushButton:checked {{background: rgba({tint},70); border-color: rgba({tint},200);}}")
    return button


def plain_tip(text):
    """A tooltip showing text as it is: names like <KoS>Bob aren't HTML."""
    return "<p style='white-space:pre'>" + html.escape(text) + "</p>"


class StayOpenMenu(QMenu):
    """A menu whose checkable actions are ticked without closing it."""

    def mouseReleaseEvent(self, event):
        action = self.activeAction()
        if action and action.isCheckable() and action.isEnabled():
            action.trigger()
            return
        super().mouseReleaseEvent(event)


class NamesField(QLineEdit):
    """Parts of players' names, separated by commas, anyone if none. Its menu
    adds or takes off players (with labels if given, from fill() when it opens
    if given, as (players, labels)), in a list that scrolls."""

    edited = Signal()
    ROWS = 15  # players the menu shows at once: a long list fits the screen

    def __init__(self, tip, menu_tip="", empty="No demos indexed yet", fill=None):
        super().__init__(placeholderText="Anyone")
        self.setClearButtonEnabled(True)
        self.setToolTip(tip)
        self.players = []
        self.labels = {}  # player: what the menu shows
        self.empty = empty
        self.fill = fill
        self.menu = StayOpenMenu(self)
        menu_action = self.addAction(theme_icon("go-down", QStyle.SP_ArrowDown), QLineEdit.TrailingPosition)
        menu_action.setToolTip(menu_tip)
        menu_action.triggered.connect(self.open_menu)
        self.textChanged.connect(lambda _: self.edited.emit())

    def names(self):
        return [part.strip() for part in self.text().split(",") if clean_name(part)]

    def open_menu(self):
        if self.fill:
            self.players, self.labels = self.fill()
        self.menu.clear()
        if self.players:
            self.menu.addAction(self.player_list())
        else:
            self.menu.addAction(self.empty).setEnabled(False)
        self.menu.popup(self.mapToGlobal(self.rect().bottomLeft()))

    def player_list(self):
        """The players, ticked if named, as a menu's action."""
        chosen = {clean_name(name) for name in self.names()}
        self.list = QListWidget()
        self.list.setFrameShape(QListWidget.NoFrame)
        for player in self.players:
            item = QListWidgetItem(self.labels.get(player, player))
            item.setData(Qt.UserRole, player)
            # ticked by a click anywhere on it, not only on its box
            item.setFlags(Qt.ItemIsEnabled)
            item.setCheckState(Qt.Checked if clean_name(player) in chosen else Qt.Unchecked)
            self.list.addItem(item)
        self.list.itemClicked.connect(self.list_clicked)
        self.list.setFixedHeight(min(len(self.players), self.ROWS) * self.list.sizeHintForRow(0))
        self.list.setMinimumWidth(self.list.sizeHintForColumn(0) + self.list.verticalScrollBar().sizeHint().width())
        action = QWidgetAction(self.menu)
        action.setDefaultWidget(self.list)
        return action

    def list_clicked(self, item):
        checked = item.checkState() != Qt.Checked
        item.setCheckState(Qt.Checked if checked else Qt.Unchecked)
        self.toggle(item.data(Qt.UserRole), checked)

    def toggle(self, player, checked):
        names = [name for name in self.names() if clean_name(name) != clean_name(player)]
        self.setText(", ".join(names + [player] if checked else names))


def is_on(value):
    return value in ("1", "true", True)


def parse_size(text):
    """(width, height) from "1920x1080", None if it isn't a size."""
    try:
        width, height = (int(v) for v in str(text).lower().replace("×", "x").split("x"))
    except ValueError:
        return None
    return (width, height) if width > 0 and height > 0 else None


def seconds_box(settings, key, default, tip, fallback=None):
    """A box for a time in seconds, as kept in key, else in fallback."""
    box = QDoubleSpinBox()
    box.setRange(0.5, 30.0)
    box.setSingleStep(0.5)
    box.setSuffix(" s")
    box.setToolTip(tip)
    try:
        box.setValue(float(settings.value(key, settings.value(fallback, default) if fallback else default)))
    except (ValueError, TypeError):
        box.setValue(default)
    return box


def lead_row(before, after, text):
    """Before and after boxes in a row: "4.00 s before, 2.00 s after"."""
    row = QHBoxLayout()
    row.addWidget(before)
    row.addWidget(QLabel("before,"))
    row.addWidget(after)
    row.addWidget(QLabel("after " + text))
    row.addStretch(1)
    return row


def browse_row(edit, browse):
    row = QHBoxLayout()
    row.addWidget(edit, 1)
    button = QPushButton("Browse…")
    button.clicked.connect(browse)
    row.addWidget(button)
    return row


def video_settings(settings):
    """How videos are recorded, from the settings: a job's FFmpeg options,
    size, frames per second, sound, motion blur, viewmodel, models forced
    and FFmpeg."""
    try:
        crf = int(settings.value("rec/crf", 20))
    except (ValueError, TypeError):
        crf = 20
    codec = settings.value("rec/codec", "H.264")
    sound = settings.value("rec/sound", "true") == "true"
    options = settings.value("rec/advanced", "").strip() or " ".join([
        CODECS.get(codec, CODECS["H.264"]).format(crf=crf, quality=max(1, min(100, 120 - 3 * crf))),
        f"-c:a aac -b:a {settings.value('rec/bitrate', '192k')}" if sound else "-an",
        "-movflags +faststart"])
    width, height = parse_size(settings.value("rec/size", "1920x1080")) or (1920, 1080)
    return {"options": options, "width": width, "height": height, "fps": str(int(settings.value("rec/fps", "60"))),
            "sound": sound, "blur": settings.value("rec/blur", "false") == "true",
            "viewmodel": "1" if is_on(settings.value("rec/viewmodel", settings.value("viewmodel", "1"))) else "0",
            "forcemodel": "1" if is_on(settings.value("rec/forcemodel", settings.value("forcemodel", "1"))) else "0",
            "followed": followed_models(settings),
            "censor": censored_words(settings),
            "camera": cameras(settings),
            # the folder its clips' demos are in, whichever is opened later
            "demos": settings.value("demos", ""),
            "ffmpeg": settings.value("rec/ffmpeg", "").strip() or find_ffmpeg()}


def video_summary(settings):
    """The video settings in a line: "1920x1080, 60 fps, H.264 (CRF 20),
    sound 192k"."""
    parts = [settings.value("rec/size", "1920x1080"), settings.value("rec/fps", "60") + " fps"
             + (" with motion blur" if settings.value("rec/blur", "false") == "true" else "")]
    if settings.value("rec/advanced", "").strip():
        parts.append("FFmpeg options of your own")
    else:
        parts.append(f"{settings.value('rec/codec', 'H.264')} (CRF {settings.value('rec/crf', 20)})")
        parts.append(f"sound {settings.value('rec/bitrate', '192k')}" if settings.value("rec/sound", "true") == "true"
                     else "no sound")
    if not is_on(settings.value("rec/viewmodel", settings.value("viewmodel", "1"))):
        parts.append("no viewmodel")
    if is_on(settings.value("rec/forcemodel", settings.value("forcemodel", "1"))):
        parts.append("models forced")
    allies, axis = followed_models(settings)
    if allies or axis:
        parts.append(f"the player followed as {allies or 'theirs'} / {axis or 'theirs'}")
    if censored_words(settings):
        parts.append("words censored")
    recorder, followed = cameras(settings)
    if (recorder, followed) != ("eyes", "behind"):
        parts.append(f"the recorder {RECORDER_CAMERAS[recorder][0].lower()}, "
                     f"the player followed {FOLLOWED_CAMERAS[followed][0].lower()}")
    return ", ".join(parts)


class VideoSettings(QWidget):
    """How videos are recorded, kept as rec/*: Settings' Videos tab, and
    the Record dialog's Change…."""

    def __init__(self, settings, parent=None):
        super().__init__(parent)
        self.settings = settings
        form = QFormLayout(self)
        self.size = QComboBox()
        self.size.addItems(["1280x720", "1920x1080", "2560x1440", "3840x2160"])
        self.size.setEditable(True)
        self.size.setCurrentText(settings.value("rec/size", "1920x1080"))
        form.addRow("Size", self.size)
        self.fps = QComboBox()
        self.fps.addItems(["30", "60", "120"])
        self.fps.setCurrentText(settings.value("rec/fps", "60"))
        self.blur = QCheckBox("Motion blur")
        self.blur.setToolTip("What moves blurs over each frame's time, as with a camera: "
                             "smoother motion, slower recording")
        self.blur.setChecked(settings.value("rec/blur", "false") == "true")
        row = QHBoxLayout()
        row.addWidget(self.fps)
        row.addWidget(self.blur)
        row.addStretch(1)
        form.addRow("Frames per second", row)
        self.quality = QSlider(Qt.Horizontal)
        self.quality.setRange(14, 32)
        self.quality.setInvertedAppearance(True)
        self.quality.setValue(int(settings.value("rec/crf", 20)))
        quality_label = QLabel()
        self.quality.valueChanged.connect(lambda v: quality_label.setText(f"CRF {v}"))
        self.quality.valueChanged.emit(self.quality.value())
        row = QHBoxLayout()
        row.addWidget(QLabel("Smaller"))
        row.addWidget(self.quality, 1)
        row.addWidget(QLabel("Better"))
        row.addWidget(quality_label)
        form.addRow("Quality", row)
        self.codec = QComboBox()
        self.codec.addItems(list(CODECS))
        self.codec.setCurrentText(settings.value("rec/codec", "H.264"))
        form.addRow("Codec", self.codec)
        self.sound = QCheckBox("Sound")
        self.sound.setChecked(settings.value("rec/sound", "true") == "true")
        self.bitrate = QComboBox()
        self.bitrate.addItems(["128k", "192k", "256k", "320k"])
        self.bitrate.setCurrentText(settings.value("rec/bitrate", "192k"))
        self.sound.toggled.connect(self.bitrate.setEnabled)
        self.bitrate.setEnabled(self.sound.isChecked())
        row = QHBoxLayout()
        row.addWidget(self.sound)
        row.addWidget(self.bitrate)
        row.addStretch(1)
        form.addRow("Audio", row)
        self.viewmodel = QCheckBox("The gun in the player's hands (viewmodel)")
        self.viewmodel.setChecked(is_on(settings.value("rec/viewmodel", settings.value("viewmodel", "1"))))
        self.forcemodel = QCheckBox("Everyone as an airborne soldier or a winter German")
        self.forcemodel.setChecked(is_on(settings.value("rec/forcemodel", settings.value("forcemodel", "1"))))
        shown = QVBoxLayout()
        shown.addWidget(self.viewmodel)
        shown.addWidget(self.forcemodel)
        form.addRow("Show", shown)
        self.advanced = QLineEdit(settings.value("rec/advanced", ""))
        self.advanced.setPlaceholderText("FFmpeg output options, instead of the ones above")
        form.addRow("Advanced", self.advanced)
        self.ffmpeg = QLineEdit(settings.value("rec/ffmpeg", "") or find_ffmpeg())
        form.addRow("FFmpeg", self.ffmpeg)

    def error(self):
        """What's wrong with what's entered, None if nothing."""
        if not parse_size(self.size.currentText()):
            return "The video size should be like 1920x1080."
        return None

    def save(self):
        width, height = parse_size(self.size.currentText())
        for key, value in (("size", f"{width}x{height}"), ("fps", self.fps.currentText()),
                           ("blur", "true" if self.blur.isChecked() else "false"),
                           ("crf", self.quality.value()), ("codec", self.codec.currentText()),
                           ("sound", "true" if self.sound.isChecked() else "false"),
                           ("bitrate", self.bitrate.currentText()),
                           ("viewmodel", "1" if self.viewmodel.isChecked() else "0"),
                           ("forcemodel", "1" if self.forcemodel.isChecked() else "0"),
                           ("advanced", self.advanced.text().strip()), ("ffmpeg", self.ffmpeg.text().strip())):
            self.settings.setValue("rec/" + key, value)


class SettingsDialog(QDialog):
    """The game, its files and demos, and how videos are recorded, in tabs
    ("game" and "videos")."""

    def __init__(self, settings, parent=None, tab="game"):
        super().__init__(parent)
        self.setWindowTitle("Settings")
        self.setMinimumWidth(760)
        self.settings = settings
        self.tabs = QTabWidget()
        game = QWidget()
        form = QFormLayout(game)
        self.fields = {}
        for key, label, folder in (("exe", "Game program", False),
                                   ("game", "Game files folder", True),
                                   ("demos", "Demos folder", True)):
            edit = QLineEdit(settings.value(key, ""))
            edit.setToolTip({"exe": "OpenMoHAA built from this repository",
                             "game": "The folder with main/Pak0.pk3",
                             "demos": "The folders in it too"}[key])
            form.addRow(label, browse_row(edit, lambda _=False, e=edit, d=folder: self.browse(e, d)))
            self.fields[key] = edit
        self.size = QComboBox()
        self.size.addItems(["1280x720", "1600x900", "1920x1080", "2560x1440"])
        self.size.setEditable(True)
        self.size.setCurrentText(settings.value("size", "1280x720"))
        form.addRow("Game window size", self.size)
        self.kill_before = seconds_box(settings, "kill_before", 4.0, "Time shown before each frag (cl_demoKillBefore)")
        self.kill_after = seconds_box(settings, "kill_after", 2.0, "Time shown after each frag (cl_demoKillAfter)")
        form.addRow("Each frag plays", lead_row(self.kill_before, self.kill_after, "it"))
        self.censor = QCheckBox("Star out these words in the players' names and the messages, in videos too")
        self.censor.setChecked(is_on(settings.value("censor", "1")))
        self.censor_words = QLineEdit(settings.value("censor_words", CENSOR_WORDS))
        self.censor_words.setToolTip("Separated by commas, found however they're spelled: any case, letters "
                                     "repeated, look-alike digits and signs (n1gg3r, f@g), accents, anything between "
                                     "the letters (n.i.g.g.e.r) or letters spaced out (g a y); in each word, not "
                                     "across words")
        self.censor_words.setEnabled(self.censor.isChecked())
        self.censor.toggled.connect(self.censor_words.setEnabled)
        reset = QPushButton("Default words")
        reset.clicked.connect(lambda: self.censor_words.setText(CENSOR_WORDS))
        censor = QVBoxLayout()
        censor.addWidget(self.censor)
        row = QHBoxLayout()
        row.addWidget(self.censor_words, 1)
        row.addWidget(reset)
        censor.addLayout(row)
        form.addRow("Censor", censor)
        self.tabs.addTab(game, "Game")
        self.videos = VideoSettings(settings)
        self.tabs.addTab(self.videos, "Videos")
        self.tabs.setCurrentIndex(1 if tab == "videos" else 0)
        buttons = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        box = QVBoxLayout(self)
        box.addWidget(self.tabs)
        box.addWidget(buttons)

    def browse(self, edit, folder):
        if folder:
            path = QFileDialog.getExistingDirectory(self, "Choose a folder", edit.text())
        else:
            path, _ = QFileDialog.getOpenFileName(self, "Choose the game program", edit.text())
        if path:
            edit.setText(path)

    def accept(self):
        error = self.videos.error()
        if error:
            self.tabs.setCurrentWidget(self.videos)
            QMessageBox.warning(self, "Settings", error)
            return
        for key, edit in self.fields.items():
            value = edit.text().strip()
            self.settings.setValue(key, game_program(value) if key == "exe" else value)
        self.settings.setValue("size", self.size.currentText().strip())
        self.settings.setValue("kill_before", str(self.kill_before.value()))
        self.settings.setValue("kill_after", str(self.kill_after.value()))
        self.settings.setValue("censor", "1" if self.censor.isChecked() else "0")
        self.settings.setValue("censor_words", self.censor_words.text().strip())
        self.videos.save()
        super().accept()


class VideoSettingsDialog(QDialog):
    """The Videos tab of Settings on its own."""

    def __init__(self, settings, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Video settings")
        self.videos = VideoSettings(settings)
        buttons = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        box = QVBoxLayout(self)
        box.addWidget(self.videos)
        box.addWidget(buttons)

    def accept(self):
        error = self.videos.error()
        if error:
            QMessageBox.warning(self, "Video settings", error)
            return
        self.videos.save()
        super().accept()


def range_clip(demo, start, end):
    """A clip of demo from start to end, in msec. {part} in the commands is
    the name of the video the recorder writes, and spans what it plays."""
    return {"demo": demo, "start": start, "end": end, "spans": [(start, end)],
            "commands": ["demoonly", f"demoseek {seconds(start)}", "demopause 0", f"demovideo {{part}} {seconds(end)}"]}


def only_clip(demo, only="", spans=None, kill_filter=None):
    """A clip of the kills demoonly plays, if only ("kills"), with kill_filter,
    or of all of demo; spans: what that is, from demoonly_plan(), if known."""
    start = spans[0][0] if (spans and spans[0][0] > 0) else 0
    told = (kill_filter or KillFilter()).commands() if only else []
    return {"demo": demo, "start": start, "end": None, "spans": spans,
            "commands": [*told, f"demoseek {seconds(start)}", "demopause 0", f"demoonly {only}".strip(), "demovideo {part}"]}


def numbered(clips, numbers, first=1):
    """The clips of a video, with the number the game draws on the first of
    each (cl_demoClipNumbers), from first, the next ones counting on through
    the video, or none: a clip has one for each stretch it plays, and one
    going on with the stretch of the clip before (split_clips) keeps its
    number."""
    number = first
    for clip in clips:
        number -= bool(clip.get("continued"))
        clip["number"] = number if numbers else 0
        clip["commands"] = [f"set cl_demoClipNumbers {clip['number']}", *clip["commands"]]
        number += len(clip["spans"] or [None])
    return clips


def clip_part(clip, taken, last):
    """clip playing only some of its stretches, taken: [(start, end, frags,
    continued)], to its own end if last, else to the end of the last."""
    start, end = taken[0][0], taken[-1][1]

    def command(text):
        if text.startswith("demoseek "):
            return f"demoseek {seconds(start)}"
        if text.startswith("demovideo ") and not last:
            return f"demovideo {{part}} {seconds(end)}"
        return text
    part = dict(clip, start=start, end=clip["end"] if last else end, spans=[(s, e) for s, e, _, _ in taken],
                commands=[command(text) for text in clip["commands"]], continued=taken[0][3])
    if "frags" in clip:
        part["frags"] = [frags for _, _, frags, _ in taken]
    return part


def split_clips(clips, most):
    """The clips of a video in parts at most most msec long, cut between the
    stretches they play: [[clip]], each clip playing some of the stretches
    of one given. A stretch longer than a part, as all of a long demo, starts
    a part and goes on in the next ones, their first clip "continued".
    Clips whose stretches aren't known stay whole."""
    parts, part, room = [], [], most
    for clip in clips:
        if clip["spans"] is None:
            part.append(clip)
            continue
        taken = []
        frags = clip.get("frags") or [[] for _ in clip["spans"]]
        for (start, end), kills in zip(clip["spans"], frags):
            continued = False
            while True:
                if end - start > room and (part or taken):
                    if taken:
                        part.append(clip_part(clip, taken, False))
                    parts.append(part)
                    part, taken, room = [], [], most
                if end - start <= room:
                    taken.append((start, end, kills, continued))
                    room -= end - start
                    break
                cut = start + room
                taken.append((start, cut, [k for k in kills if k["time"] < cut], continued))
                kills = [k for k in kills if k["time"] >= cut]
                start, room, continued = cut, 0, True
        if taken:
            whole = [(start, end) for start, end, _, _ in taken] == [tuple(span) for span in clip["spans"]]
            part.append(clip if whole else clip_part(clip, taken, True))
    return parts + [part] if part else parts


CLIP_LIST = ["Clip", "Video", "Demo", "Time", "Fragger", "Victim", "Weapon"]


def clip_rows(clips):
    """The list of a video's numbered clips, as CLIP_LIST: each one's number,
    start in the video and demo, and the time, fragger, victim and weapon of
    each of its frags, a row each (one without frags for a clip with none).
    The start in the video is the planned one, unknown after a demo not
    indexed. The clips left out of it (skipped) aren't."""
    rows, video = [], 0
    for clip in clips:
        if clip.get("skipped"):
            continue
        spans = clip["spans"] or [(clip["start"], None)]
        frags = clip.get("frags") or [[] for _ in spans]
        for i, ((start, end), kills) in enumerate(zip(spans, frags)):
            at = clock(video) if video is not None else ""
            for kill in kills or [None]:
                rows.append([clip["number"] + i, at, clip["demo"], clock(kill["time"] if kill else start)]
                            + ([without_colors(kill["killerName"]), without_colors(kill["victimName"]),
                                WEAPONS[weapon_of(kill)]] if kill else ["", "", ""]))
            video = video + end - start if video is not None and end is not None else None
    return rows


def write_clip_list(clips, video):
    """The list of the numbered clips of video, next to it: video.csv."""
    with open(os.path.splitext(video)[0] + ".csv", "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(CLIP_LIST)
        writer.writerows(clip_rows(clips))


def stretches(spans, switches=()):
    """(start, end) spans in msec, joined as demoonly does: when less than a
    second apart, unless the player shown changes in between (switches: the
    times it does, in order), which stays a cut."""
    joined = []
    for start, end in sorted(spans):
        if joined and start > joined[-1][1]:
            i = bisect.bisect_left(switches, joined[-1][1])
            cut = start > joined[-1][1] + 1000 or (i < len(switches) and switches[i] <= start)
        else:
            cut = not joined
        if cut:
            joined.append([max(0, start), end])
        else:
            joined[-1][1] = max(joined[-1][1], end)
    return [tuple(span) for span in joined]


def kill_span(index, kill, start, end, before=KILL_BEFORE, after=KILL_AFTER):
    """When kill plays, in msec: from before it to after it, while it can be
    played (start to end), inside its level, as the engine clamps it."""
    levels = [level["time"] for level in index.get("maps", [])]
    i = bisect.bisect_right(levels, kill["time"])
    first = levels[i - 1] if i else 0
    last = levels[i] if i < len(levels) else index.get("duration", 0)
    return max(start, first, kill["time"] - before), min(end, last, kill["time"] + after)


def demoonly_plan(index, only="", kill_before=None, kill_after=None, kill_filter=None):
    """What demoonly plays in a demo, as the engine does, from its index: with
    only ("kills"), demofilter's kill_filter, (how many kills, [(start, end)] in
    msec), else all of the demo. Team kills aren't counted, and only play with
    a kill of their killer near them; the deaths of the player shown aren't
    kills."""
    kill_filter = kill_filter or KillFilter()
    if not only:
        return 0, [(0, index.get("duration", 0))]
    switches = [w["time"] for w in index.get("watched", [])]
    frags, kills, cut = planned_kills(index, kill_filter)
    kb = KILL_BEFORE if kill_before is None else kill_before
    ka = KILL_AFTER if kill_after is None else kill_after
    spans = [kill_span(index, k, start, end, kb, ka) for k, start, end in kills]
    return len(frags), stretches([span for span in spans if span[1] > span[0]], switches if cut else [])


def planned_kills(index, kill_filter=None):
    """The kills demoonly kills plays, as demoonly_plan tells: (frags, kills,
    cut), [(kill, start, end)] with when each can play: the frags it counts,
    those and the team kills that play with them, and whether the player shown
    changing cuts joined clips (when they're someone's)."""
    kill_filter = kill_filter or KillFilter()
    kill_specced(kill_streaks(kill_levels(index)))
    if kill_filter.by_shown():
        kills = [(k, start, end) for k, start, end in filter_shown_kills(index, kill_filter)
                 if kill_filter.has_killer(clean_name(k["killerName"]))]
        cut = True
    else:
        # anyone's, whoever is shown
        kills, cut = [(k, 0, index.get("duration", 0)) for k in index.get("kills", [])], False
    deaths = deaths_shown(index)
    frags = [x for x in kills if not x[0].get("teamKill") and not is_grenade(x[0]) and id(x[0]) not in deaths
             and kill_filter.keeps(x[0], killer=False)]
    return frags, frags + team_kills_with(kills, [k for k, _, _ in frags]), cut


def clip_frags(index, spans, only="", kill_filter=None):
    """The frags in each of spans, the clips demoonly plays: [[kill]], none for
    all of the demo."""
    if not only:
        return [[] for _ in spans]
    frags = [k for k, _, _ in planned_kills(index, kill_filter)[0]]
    return [[k for k in frags if start <= k["time"] < end] for start, end in spans]


def the(count, one, many):
    """"The 3 frags", "The frag" or "No frags"."""
    return f"The {count:,} {many}" if count > 1 else f"The {one}" if count else f"No {many}"


def about(msec):
    """How long, roughly: "about 40 min"."""
    minutes = round(msec / 60000)
    if minutes < 1:
        return "under a minute"
    return f"about {minutes // 60} h {minutes % 60} min" if minutes >= 60 else f"about {minutes} min"


def length(spans):
    return sum(end - start for start, end in spans)


class RecordDialog(QDialog):
    """What to record and how, kept in the settings.

    choices: [(key, label, enabled)], what can be recorded, and a stretch of
    time from time when it isn't None. join: offer one video for all.
    relabel(before, after): the choices again for that lead-in and out, in
    msec, as their lengths change with them."""

    def __init__(self, settings, choices, time=None, join=False, parent=None, relabel=None):
        super().__init__(parent)
        self.setWindowTitle("Record a video")
        self.settings = settings
        form = QFormLayout(self)

        group = QButtonGroup(self)
        what = QVBoxLayout()
        self.choices = {}
        for key, label, enabled in choices:
            button = self.choices[key] = QRadioButton(label)
            group.addButton(button)
            what.addWidget(button)
        self.range = None
        if time is not None:
            self.range = QRadioButton("From")
            self.start = QLineEdit(clock(time))
            self.end = QLineEdit(clock(time + 30000))
            row = QHBoxLayout()
            for widget in (self.range, self.start, QLabel("to"), self.end):
                row.addWidget(widget)
            what.addLayout(row)
            group.addButton(self.range)
        # only what there is to record
        self.nothing = QLabel("Nothing to record")
        what.addWidget(self.nothing)
        self.relabel(choices)
        # the last choice, if it's offered
        last = self.range if settings.value("rec/what") == "range" else self.choices.get(settings.value("rec/what"))
        first = next((b for b in self.choices.values() if b.isEnabled()), self.range)
        if last and last.isEnabled():
            last.setChecked(True)
        elif first:
            first.setChecked(True)
        form.addRow("Record", what)
        self.join = QCheckBox("One video for all")
        self.join.setChecked(settings.value("rec/join", "true") == "true")
        self.offers_join = join
        if join:
            form.addRow("", self.join)
        else:
            self.join.hide()

        self.kill_before = seconds_box(settings, "rec/kill_before", 4.0,
                                       "Time recorded before each frag (cl_demoKillBefore)", "kill_before")
        self.kill_after = seconds_box(settings, "rec/kill_after", 2.0,
                                      "Time recorded after each frag (cl_demoKillAfter)", "kill_after")
        form.addRow("Each frag", lead_row(self.kill_before, self.kill_after, "it"))
        self.numbers = QCheckBox("Number them")
        self.numbers.setToolTip("A number on each clip of the video, and next to it the list of the clips with "
                                "their frags (a .csv of the same name)")
        self.numbers.setChecked(settings.value("rec/numbers", "false") == "true")
        form.addRow("Clips", self.numbers)
        self.split = QCheckBox("Split into parts of at most")
        self.split.setToolTip("A video longer than this is recorded in parts, \"<name> part 1.mp4\", \"<name> part "
                              "2.mp4\"…, cut between frags; a stretch longer than a part, as all of a long demo, is "
                              "cut in it. Clip numbers go on from one part to the next, and each has its list")
        self.split.setChecked(settings.value("rec/split", "false") == "true")
        self.split_minutes = QSpinBox()
        self.split_minutes.setRange(1, 999)
        self.split_minutes.setSuffix(" min")
        self.split_minutes.setValue(int(settings.value("rec/split_minutes", 60)))
        self.split_minutes.setEnabled(self.split.isChecked())
        self.split.toggled.connect(self.split_minutes.setEnabled)
        row = QHBoxLayout()
        row.addWidget(self.split)
        row.addWidget(self.split_minutes)
        row.addStretch(1)
        form.addRow("Parts", row)
        if relabel:
            # once they stop changing, as a choice of many demos takes a while
            timer = QTimer(self, singleShot=True, interval=150)
            timer.timeout.connect(lambda: self.relabel(relabel(*self.kill_times())))
            self.kill_before.valueChanged.connect(lambda _: timer.start())
            self.kill_after.valueChanged.connect(lambda _: timer.start())

        self.pattern = QLineEdit(settings.value("rec/pattern", "{demo} {start}"))
        self.pattern.setToolTip("{demo} (the demo's file name), {start}, {player} and {date} are replaced")
        form.addRow("File name", self.pattern)
        self.folder = QLineEdit(settings.value("rec/folder", os.path.expanduser("~/Videos")))
        form.addRow("Folder", browse_row(self.folder, self.browse))
        self.summary = QLabel(video_summary(settings))
        self.summary.setWordWrap(True)
        change = QPushButton("Change…")
        change.setToolTip("The video settings, also in Settings")
        change.clicked.connect(self.change_video)
        row = QHBoxLayout()
        row.addWidget(self.summary, 1)
        row.addWidget(change)
        form.addRow("Video", row)

        buttons = QDialogButtonBox(QDialogButtonBox.Cancel)
        buttons.addButton("Record", QDialogButtonBox.AcceptRole)
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        form.addRow(buttons)

    def browse(self):
        path = QFileDialog.getExistingDirectory(self, "Choose a folder", self.folder.text())
        if path:
            self.folder.setText(path)

    def change_video(self):
        if VideoSettingsDialog(self.settings, self).exec():
            self.summary.setText(video_summary(self.settings))

    def kill_times(self):
        """The lead-in and out chosen, in msec."""
        return int(self.kill_before.value() * 1000), int(self.kill_after.value() * 1000)

    def relabel(self, choices):
        for key, label, enabled in choices:
            self.choices[key].setText(label)
            self.choices[key].setEnabled(enabled)
            self.choices[key].setHidden(not enabled)
        self.nothing.setHidden(bool(self.range) or any(enabled for _, _, enabled in choices))

    def what(self):
        """"range" or the key of the choice made."""
        if self.range and self.range.isChecked():
            return "range"
        return next((key for key, button in self.choices.items() if button.isChecked()), None)

    def times(self):
        """The stretch of time chosen, in msec."""
        return parse_clock(self.start.text()), parse_clock(self.end.text())

    def accept(self):
        if self.what() is None:
            QMessageBox.warning(self, "Record a video", "There's nothing to record.")
            return
        if self.what() == "range":
            start, end = self.times()
            if start is None or end is None or end <= start:
                QMessageBox.warning(self, "Record a video", "The start and end times aren't right.")
                return
        for key, value in (("folder", self.folder.text()), ("pattern", self.pattern.text()),
                           ("kill_before", str(self.kill_before.value())),
                           ("kill_after", str(self.kill_after.value())),
                           ("numbers", "true" if self.numbers.isChecked() else "false"),
                           ("split", "true" if self.split.isChecked() else "false"),
                           ("split_minutes", str(self.split_minutes.value()))):
            self.settings.setValue("rec/" + key, value)
        if self.offers_join:
            self.settings.setValue("rec/join", "true" if self.join.isChecked() else "false")
        if self.range or len(self.choices) > 1:
            self.settings.setValue("rec/what", self.what())
        super().accept()

    def jobs(self, label, clips, demo, player=""):
        """The video of clips for the recorder, with the settings chosen, or
        its parts when it's split: [job]. demo and player name it."""
        # a demo's file name, without its folders
        demo = demo.rsplit("/", 1)[-1]
        try:
            name = self.pattern.text().format_map({
                "demo": demo, "start": clock(clips[0]["start"]).replace(":", "-"), "player": player or "",
                "date": QDateTime.currentDateTime().toString("yyyy-MM-dd hh-mm")})
        except (KeyError, ValueError, IndexError):
            name = demo
        name = "".join(c for c in name if c not in '/\\:*?"<>|').strip() or demo
        parts = split_clips(clips, self.split_minutes.value() * 60000) if self.split.isChecked() else [clips]
        jobs, first = [], 1
        for number, part in enumerate(parts, 1):
            suffix = f" part {number}" if len(parts) > 1 else ""
            jobs.append({"label": label + (f" (part {number} of {len(parts)})" if suffix else ""),
                         "clips": numbered(part, self.numbers.isChecked(), first),
                         "numbers": self.numbers.isChecked(), **video_settings(self.settings),
                         "kill_before": str(self.kill_before.value()),
                         "kill_after": str(self.kill_after.value()),
                         "output": os.path.join(self.folder.text(), name + suffix + ".mp4"),
                         "status": "waiting"})
            first += sum(len(clip["spans"] or [None]) - bool(clip.get("continued")) for clip in part)
        return jobs


class Recorder(QObject):
    """Records a job in a second game, offscreen, rendering the sound: its
    clips one after the other, loading their demos, then FFmpeg joins them
    when there are several. A clip the game doesn't record, as its demo
    doesn't load, an error stops it or the game quits, is tried again once
    in a new game, then left out, its "skipped" telling why, unless
    LEFT_OUT clips in a row are. The job keeps the clips made so far
    (parts_dir, made, next_clip), so that a recorder paused, or stopped as
    the app closes, goes on from the next clip. on_progress(job, text) tells
    how it goes, and on_done(job, error, video) when it's over, error being
    "paused" or "canceled" then."""

    LEFT_OUT = 3

    def __init__(self, window, job, on_progress, on_done):
        super().__init__(window)
        self.window = window
        self.job = job
        self.on_progress = on_progress
        self.on_done = on_done
        self.phase = "loading"
        self.error = ""
        self.clip = -1
        self.retried = False  # this clip
        self.left_out = 0  # clips in a row
        self.last_lines = collections.deque(maxlen=3)  # of the game, for why it quit
        self.loaded = None  # the demo the recorder plays
        self.parts = []
        self.joiner = None
        self.parts_dir = None
        self.started_at = None  # how much of the video was made before
        # with the settings of the game watched, but changing none
        self.game = Game(self.output, self.exited, keep_config=False)
        self.clock = QElapsedTimer()
        self.elapsed = QElapsedTimer()
        self.timer = QTimer(self, interval=200, timeout=self.poll)

    def start_game(self):
        settings = self.window.settings
        job = self.job
        self.last_lines.clear()
        extra = ["+set", "s_loopback", "1" if job["sound"] else "0", "+set", "s_khz", "44",
                 "+set", "s_volume", "1", "+set", "cl_aviFrameRate", job["fps"],
                 "+set", "cl_aviFFmpeg", job["ffmpeg"], "+set", "cl_aviPipeFormat", job["options"],
                 "+set", "com_maxfps", "0", "+set", "r_swapInterval", "0",
                 *(BLUR_CVARS if job.get("blur") else [])]
        # offscreen where SDL can, the recorder doesn't need a window
        driver = "offscreen" if sys.platform.startswith("linux") else None
        viewmodel = job.get("viewmodel", settings.value("rec/viewmodel", settings.value("viewmodel", "1")))
        forcemodel = job.get("forcemodel", settings.value("rec/forcemodel", settings.value("forcemodel", "1")))
        kill_before = job.get("kill_before", settings.value("rec/kill_before", settings.value("kill_before", "4.0")))
        kill_after = job.get("kill_after", settings.value("rec/kill_after", settings.value("kill_after", "2.0")))
        self.game.start(settings.value("exe"), settings.value("game"), job.get("demos") or settings.value("demos"),
                        job["width"], job["height"], extra, driver,
                        viewmodel=viewmodel, forcemodel=forcemodel,
                        kill_before=kill_before, kill_after=kill_after, videos=self.parts_dir,
                        followed=job.get("followed", followed_models(settings)),
                        censor=job.get("censor", censored_words(settings)),
                        camera=job.get("camera", cameras(settings)))

    def start(self):
        job = self.job
        made = job.get("made", [])
        if job.get("parts_dir") and os.path.isdir(job["parts_dir"]) and all(os.path.isfile(p) for p in made):
            # going on after the clips made before
            self.parts_dir, self.parts = job["parts_dir"], list(made)
            self.clip = job.get("next_clip", 0) - 1
        else:
            self.forget()
            for clip in job["clips"]:
                clip.pop("skipped", None)
            # the clips are written next to the video, as the game's home may
            # be in memory (/tmp), and the video then only moves
            try:
                folder = os.path.dirname(job["output"]) or "."
                os.makedirs(folder, exist_ok=True)
                self.parts_dir = tempfile.mkdtemp(prefix=".mohreplay-parts-", dir=folder)
            except OSError:
                self.parts_dir = tempfile.mkdtemp(prefix="mohreplay-parts-")
        self.started_at = sum(length(clip["spans"] or []) for clip in job["clips"][:self.clip + 1])
        self.start_game()
        self.elapsed.start()
        self.on_progress(self.job, "Starting the recorder…")
        self.timer.start()
        self.next_clip()

    def output(self, line):
        if line.startswith("ERROR:") or "Couldn't write" in line or "Couldn't run" in line:
            self.error = line.replace("^1", "")
        if line.strip():
            self.last_lines.append(line.strip())

    def game_error(self):
        """Whether the error is the game's, as a demo it can't play, not
        FFmpeg's, which no clip would get past."""
        return self.error.startswith("ERROR:")

    def part(self):
        """The name of the video of this clip."""
        return f"part{self.clip}"

    def made(self):
        """The video the recorder writes."""
        return os.path.join(self.parts_dir, self.part() + ".mp4")

    def next_clip(self):
        self.clip += 1
        self.clock.start()
        if self.clip == len(self.job["clips"]):
            self.join()
            return
        demo = self.job["clips"][self.clip]["demo"]
        if demo != self.loaded:
            if self.loaded is not None:
                self.phase = "restarting"
                self.game.stop()
                self.error = ""
                self.start_game()
            self.game.send("demo " + quoted(demo))
            self.loaded = demo
            self.phase = "loading"
        else:
            self.send_clip()

    def send_clip(self):
        commands = self.job["clips"][self.clip]["commands"]
        self.game.send(*(command.replace("{part}", self.part()) for command in commands))
        self.phase = "starting"

    def poll(self):
        # looked at first: the game says it records before FFmpeg makes the
        # file, and that it's done once FFmpeg has quit
        made = os.path.isfile(self.made())
        state = self.game.read_json("demostate.json") or {}
        if self.phase == "loading":
            if state.get("demo") == self.loaded and state.get("time", 0) > 0 and not state.get("seeking"):
                self.send_clip()
            elif self.clock.elapsed() > 120000 or self.game_error():
                self.clip_failed(f"The recorder didn't load {self.loaded}. {self.error}")
        elif self.phase == "starting":
            if state.get("recording"):
                self.phase = "recording"
            elif made:
                self.clip_done()  # over between two polls
            elif self.game_error() or self.clock.elapsed() > 180000 and not self.error:
                self.clip_failed("The recording didn't start. " + self.error)
            elif self.error:
                self.finish("The recording didn't start. " + self.error)
        elif self.phase == "recording":
            text = "Recording " + self.how_far(state.get("time", 0))
            if len(self.job["clips"]) > 1:
                text += f" (clip {self.clip + 1} of {len(self.job['clips'])})"
            self.on_progress(self.job, text + "…")
            if not state.get("recording"):
                self.clip_done()

    def recorded(self, time=None):
        """How much of the video the clips before this one play, and this one
        up to time in its demo, if given; None when not known."""
        clips = self.job["clips"]
        if any(clip["spans"] is None for clip in clips):
            return None
        done = sum(length(clip["spans"]) for clip in clips[:max(0, self.clip)])
        if time is not None:
            done += sum(max(0, min(end, time) - start) for start, end in clips[self.clip]["spans"])
        return done

    def how_far(self, time):
        """How much of the video is done, and how long the rest will take,
        from what the clips play, else the time in the demo."""
        total = sum(length(clip["spans"] or []) for clip in self.job["clips"])
        done = self.recorded(time)
        if not total or done is None:
            return clock(time)
        share = min(0.99, done / total)
        text = f"{int(share * 100)}%"
        # once it's going, at the pace since this recorder started
        since = done - (self.started_at or 0)
        if share >= 0.02 and since > 0.01 * total:
            text += ", " + about(self.elapsed.elapsed() * (total - done) / since) + " left"
        return text

    def clip_done(self):
        log_file = self.made() + ".log"
        log_content = ""
        if os.path.isfile(log_file):
            try:
                with open(log_file, errors="replace") as f:
                    log_content = f.read()
            except OSError:
                pass

        if self.game_error():
            self.clip_failed("The recording stopped. " + self.error)
        elif self.error:
            # FFmpeg quit midway: what it wrote isn't the whole video
            self.finish("The recording stopped. " + self.error)
        elif not os.path.isfile(self.made()) or os.path.getsize(self.made()) == 0:
            err = "FFmpeg didn't write the video."
            if log_content:
                err += "\n" + log_content[-1500:]
            self.finish(err)
        elif "No space left on device" in log_content:
            self.finish("FFmpeg ran out of disk space while writing the clip.\n" + log_content[-1500:])
        else:
            self.parts.append(self.made())
            self.retried, self.left_out = False, 0
            self.keep()
            self.next_clip()

    def keep(self):
        """The clips made so far, in the job, to go on from the next one."""
        self.job.update(parts_dir=self.parts_dir, made=list(self.parts), next_clip=self.clip + 1)
        self.window.save_queue()

    def forget(self):
        for key in ("parts_dir", "made", "next_clip"):
            self.job.pop(key, None)

    def clip_failed(self, why):
        """The clip didn't record: once more in a new game, then without it."""
        if self.retried:
            self.left_out += 1
            if self.left_out >= self.LEFT_OUT:
                self.finish(f"{self.left_out} clips in a row didn't record. {why}")
                return
            self.job["clips"][self.clip]["skipped"] = why.strip()
            self.keep()
        self.game.stop()
        for path in (self.made(), self.made() + ".log"):
            try:
                os.remove(path)
            except OSError:
                pass
        if not self.retried:
            self.clip -= 1
        self.retried = not self.retried
        self.error = ""
        self.phase = "restarting"
        self.start_game()
        self.loaded = None
        self.next_clip()

    def join(self):
        self.phase = "joining"
        # each clip was checked when it was done: all of them go in
        if not self.parts:
            self.finish("There was nothing to record.")
            return
        if len(self.parts) == 1:
            self.finish(video=self.parts[0])
            return
        self.on_progress(self.job, f"Joining {len(self.parts)} clips…")
        folder = self.parts_dir
        with open(os.path.join(folder, "parts.txt"), "w") as f:
            f.writelines("file '" + part.replace("'", "'\\''") + "'\n" for part in self.parts)
        self.joiner = QProcess(self)
        self.joiner.setProcessChannelMode(QProcess.MergedChannels)
        self.joiner.finished.connect(self.joined)
        self.joiner.errorOccurred.connect(
            lambda error: error == QProcess.FailedToStart and self.finish("Couldn't run FFmpeg to join the clips."))
        self.joiner.start(self.job["ffmpeg"], ["-loglevel", "warning", "-y", "-f", "concat", "-safe", "0",
                                               "-i", os.path.join(folder, "parts.txt"), "-c", "copy",
                                               "-movflags", "+faststart", os.path.join(folder, "joined.mp4")])

    def joined(self, code=0, status=None):
        if self.phase != "joining":
            return
        video = os.path.join(self.parts_dir, "joined.mp4")
        if code or not os.path.isfile(video) or os.path.getsize(video) == 0:
            text = bytes(self.joiner.readAll()).decode("utf-8", "replace")
            self.finish("FFmpeg couldn't join the clips.\n" + text[-1500:])
        else:
            self.finish(video=video)

    def finish(self, error="", video=None):
        """Over: the video goes to the folder chosen, or the error is told,
        with FFmpeg's log."""
        self.timer.stop()
        self.phase = "done"
        log = self.made() + ".log"
        if not os.path.isfile(log) and self.parts:
            log = self.parts[-1] + ".log"
        if error and os.path.isfile(log):
            with open(log, errors="replace") as f:
                error += "\n" + f.read()[-1500:]
        output = self.job["output"]
        if video:
            try:
                os.makedirs(os.path.dirname(output) or ".", exist_ok=True)
                base, number = output[:-4], 2
                while os.path.exists(output):
                    output = f"{base} ({number}).mp4"
                    number += 1
                shutil.move(video, output)
            except OSError as e:
                error, video = f"Couldn't move the video to {output}: {e}", None
        self.game.stop()
        if self.parts_dir:
            shutil.rmtree(self.parts_dir, ignore_errors=True)
        self.forget()
        self.on_done(self.job, error.strip(), output if video else None)

    def stop(self):
        self.phase = "done"
        self.timer.stop()
        if self.joiner:
            self.joiner.kill()
            self.joiner.waitForFinished(1000)
        self.game.stop()

    def cancel(self):
        if self.phase != "done":
            self.stop()
            if self.parts_dir:
                shutil.rmtree(self.parts_dir, ignore_errors=True)
            self.forget()
            self.on_done(self.job, "canceled", None)

    def pause(self):
        """Stops, keeping the clips made: the one recording is made again."""
        if self.phase != "done":
            self.stop()
            if self.made() not in self.parts:
                for path in (self.made(), self.made() + ".log"):
                    try:
                        os.remove(path)
                    except OSError:
                        pass
            self.on_done(self.job, "paused", None)

    def exited(self):
        if self.phase == "joining":
            return  # the clips are made
        if self.phase != "done":
            self.clip_failed("The recorder quit. " + (self.error or "Its last lines: " + " / ".join(self.last_lines)))


class Window(QMainWindow):
    MOST_PLAYERS = 1000  # the most the menus of Fragger and Victim list

    def __init__(self, settings):
        super().__init__()
        self.settings = settings
        self.game = Game(self.game_output, self.game_exited)
        self.pending_demo = None
        self.recorder = None
        self.jobs = []
        self.library = Library(self.library_changed, self)
        self.demo_items = {}  # name: item
        self.folder_items = {}  # folder in the demos folder: item
        self.demo_frags = {}  # name of a demo listed: how many frags the filter keeps in it

        self.setWindowTitle("MoH Demo Replay")
        self.resize(1200, 720)

        # demos
        self.filter = QLineEdit(placeholderText="Search demos by name, map or player")
        self.filter.setClearButtonEnabled(True)
        self.filter.setToolTip("Demos with all of these words in their name, a level or a player's name; commas "
                               "separate searches, so \"pstn, tommeh\" finds the demos of either")
        self.filter.textChanged.connect(self.filter_demos)
        self.rules = QComboBox()
        self.rules.addItem("All", "")
        self.rules.addItem("Default", "default")
        self.rules.addItem("Realism", "realism")
        self.rules.setToolTip("Default or realism servers, told by the speeds of the recorder's weapons")
        self.rules.setCurrentIndex(max(0, self.rules.findData(settings.value("rules", ""))))
        self.rules.currentIndexChanged.connect(self.rules_changed)
        self.recorded_by = NamesField("Demos recorded by players whose names have one of these, separated by commas",
                                      menu_tip="The recorders of the demos, those of the most first")
        self.recorded_by.edited.connect(self.filter_demos)
        self.demos = self.make_list(DEMO_COLUMNS, fit=False)
        self.demos.setSortingEnabled(True)
        self.demos.sortByColumn(COL_DATE, Qt.DescendingOrder)
        self.demos.itemActivated.connect(lambda item: item.data(COL_DEMO, Qt.UserRole)
                                         and self.play_demo(item.data(COL_DEMO, Qt.UserRole)))
        self.demos.setSelectionMode(QAbstractItemView.ExtendedSelection)
        self.demos.itemSelectionChanged.connect(self.show_demo_count)
        self.demos.header().setStretchLastSection(False)
        self.demos.header().setSectionResizeMode(COL_DEMO, QHeaderView.Stretch)
        # the kills of the demos listed
        menu_tip = "The players of the demos listed, those with the most frags first"
        self.killer = NamesField("Frags by players whose names have one of these, separated by commas, while "
                                 "they're shown", menu_tip=menu_tip, fill=self.players_listed)
        self.victim = NamesField("Frags of players whose names have one of these, separated by commas",
                                 menu_tip=menu_tip, fill=self.players_listed)
        self.weapon = QToolButton()
        self.weapon.setPopupMode(QToolButton.InstantPopup)
        self.weapon.setToolTip("Frags made with these weapons, as the death messages tell")
        menu = StayOpenMenu(self.weapon)
        menu.setToolTipsVisible(True)
        self.weapon_actions = {}
        for key, label in WEAPONS.items():
            action = self.weapon_actions[key] = menu.addAction(label)
            action.setCheckable(True)
            action.setToolTip(WEAPON_TIPS[key])
            action.toggled.connect(self.weapons_changed)
        menu.addSeparator()
        menu.addAction("Any weapon", lambda: [a.setChecked(False) for a in self.weapon_actions.values()])
        self.weapon.setMenu(menu)
        # applied once typing stops
        self.kill_filter_timer = QTimer(self, singleShot=True, interval=250, timeout=self.apply_kill_filter)
        self.killer.edited.connect(self.kill_filter_timer.start)
        self.victim.edited.connect(self.kill_filter_timer.start)
        self.weapons_changed()
        self.obj = QCheckBox("Obj maps")
        self.obj.setToolTip("Only the frags made on the objective maps (obj/, as obj_team4 or harbor), "
                            "not on the deathmatch ones (dm/)")
        self.obj.toggled.connect(self.kill_filter_timer.start)
        self.great = QCheckBox("Great")
        self.great.setToolTip("Leaves out the sniper frags made with the scope on for over a second while standing "
                              "still, as the fragger's view shows them: no-scopes, quick scopes, shots on the move and "
                              "streaks (frags at most 5 s apart) stay, and so do the frags of other weapons and those "
                              "the demo doesn't show from the fragger's view. Leaves out the frags of AFK players too, "
                              "who had given no input for over 5 s")
        self.great.toggled.connect(self.kill_filter_timer.start)
        # who they're from, any of them, none being anyone
        self.from_buttons = {}
        from_row = QHBoxLayout()
        from_row.setSpacing(0)
        for key, label, tip in (
                ("recorder", "Recorder", "The recorder's own frags, while they play"),
                ("specced", "Specced", "The frags of the players the recorder spectated, while they followed them"),
                ("unshown", "Unspecced", "The frags made while the demo shows someone else, seen from behind the "
                                         "fragger: when the demo has them for 3 s before, and their victim (about half "
                                         "of those frags)")):
            button = self.from_buttons[key] = toggle_button(label, tip)
            button.setChecked(is_on(settings.value("from_" + key, "0")))
            button.toggled.connect(lambda on, key=key: self.settings.setValue("from_" + key, "1" if on else "0"))
            button.toggled.connect(self.kill_filter_timer.start)
            from_row.addWidget(button)
        filters = QGridLayout()
        filters.setHorizontalSpacing(8)
        filters.setVerticalSpacing(6)
        filters.addWidget(heading("Demos"), 0, 0)
        row = QHBoxLayout()
        row.setSpacing(6)
        row.addWidget(self.filter, 4)
        row.addSpacing(6)
        row.addWidget(QLabel("Recorder"))
        row.addWidget(self.recorded_by, 2)
        row.addSpacing(6)
        row.addWidget(QLabel("Rules"))
        row.addWidget(self.rules)
        filters.addLayout(row, 0, 1)
        filters.addWidget(heading("Frags"), 1, 0)
        row = QHBoxLayout()
        row.setSpacing(6)
        for label, widget, stretch in (("Fragger", self.killer, 3), ("Victim", self.victim, 2), ("Weapon", self.weapon, 0)):
            row.addWidget(QLabel(label))
            row.addWidget(widget, stretch)
            row.addSpacing(6)
        row.addWidget(QLabel("From"))
        row.addLayout(from_row)
        row.addSpacing(6)
        row.addWidget(self.great)
        row.addWidget(self.obj)
        filters.addLayout(row, 1, 1)

        # how many there are, what's going on, and recording them
        self.demo_count = QLabel()
        self.message = faint(QLabel())
        # a long one doesn't make the window wider
        self.message.setSizePolicy(QSizePolicy.Ignored, QSizePolicy.Preferred)
        self.message_timer = QTimer(self, singleShot=True, timeout=self.message.clear)
        self.record_button = QPushButton(theme_icon("media-record", record_icon()), "Record…")
        self.record_button.setToolTip("The demos selected, or all those listed if none is")
        self.record_button.clicked.connect(self.record_demos)
        footer = QHBoxLayout()
        footer.addWidget(self.demo_count)
        footer.addSpacing(12)
        footer.addWidget(self.message, 1)
        footer.addWidget(self.record_button)

        central = QWidget()
        box = QVBoxLayout(central)
        box.addLayout(filters)
        box.addWidget(self.demos, 1)
        box.addLayout(footer)
        self.setCentralWidget(central)

        # the videos recorded and to record
        self.queue = self.make_list(["Video", "What", "Status"])
        self.queue.setSelectionMode(QAbstractItemView.ExtendedSelection)
        self.queue.itemActivated.connect(self.open_video)
        buttons = QHBoxLayout()
        for text, action, tip in (
                ("Pause", self.pause_jobs, "Holds the videos selected; the one recording stops and keeps its "
                                           "clips made, to go on from there"),
                ("Resume", self.resume_jobs, "Records the videos selected that are paused, failed or canceled "
                                             "again, in their turn"),
                ("Record again", self.record_jobs_again, "Records the videos selected that are done once more, in "
                                                         "their turn, with the game as it is now: a new video, "
                                                         "numbered, next to the one made"),
                ("Up", lambda: self.move_jobs(-1), "Records the videos selected sooner"),
                ("Down", lambda: self.move_jobs(1), "Records the videos selected later"),
                ("Cancel", self.cancel_jobs, ""), ("Remove finished", self.remove_finished, ""),
                ("Open folder", self.open_folder, "")):
            button = QPushButton(text)
            button.setToolTip(tip)
            button.clicked.connect(action)
            buttons.addWidget(button)
        buttons.addStretch(1)
        videos = QWidget()
        box = QVBoxLayout(videos)
        box.setContentsMargins(0, 0, 0, 0)
        box.addWidget(self.queue)
        box.addLayout(buttons)
        self.queue_dock = QDockWidget("Videos", self)
        self.queue_dock.setObjectName("videos")
        self.queue_dock.setWidget(videos)
        self.addDockWidget(Qt.BottomDockWidgetArea, self.queue_dock)
        self.queue_dock.hide()

        toolbar = QToolBar("Toolbar")
        toolbar.setObjectName("toolbar")
        toolbar.setMovable(False)
        toolbar.setToolButtonStyle(Qt.ToolButtonTextBesideIcon)
        self.addToolBar(toolbar)
        self.folder_action = toolbar.addAction(theme_icon("folder-open", QStyle.SP_DirOpenIcon), "")
        self.folder_action.triggered.connect(self.choose_folder)
        toolbar.addSeparator()
        self.viewmodel_action = toolbar.addAction("Viewmodel")
        self.viewmodel_action.setToolTip("The gun in the player's hands")
        self.viewmodel_action.setCheckable(True)
        self.viewmodel_action.setChecked(is_on(settings.value("viewmodel", "1")))
        self.viewmodel_action.toggled.connect(self.toggle_viewmodel)
        self.forcemodel_action = toolbar.addAction("Force models")
        self.forcemodel_action.setToolTip("Everyone as an airborne soldier or a winter German")
        self.forcemodel_action.setCheckable(True)
        self.forcemodel_action.setChecked(is_on(settings.value("forcemodel", "1")))
        self.forcemodel_action.toggled.connect(self.toggle_forcemodel)
        self.followed_button = QToolButton()
        self.followed_button.setText("Followed player")
        self.followed_button.setPopupMode(QToolButton.InstantPopup)
        self.followed_button.setMenu(QMenu(self.followed_button))
        self.followed_button.menu().aboutToShow.connect(self.fill_followed_menu)
        self.show_followed()
        toolbar.addWidget(self.followed_button)
        self.camera_button = QToolButton()
        self.camera_button.setText("Camera")
        self.camera_button.setPopupMode(QToolButton.InstantPopup)
        self.camera_button.setMenu(QMenu(self.camera_button))
        self.camera_button.menu().aboutToShow.connect(self.fill_camera_menu)
        self.show_camera()
        toolbar.addWidget(self.camera_button)
        spacer = QWidget()
        spacer.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Preferred)
        toolbar.addWidget(spacer)
        videos_action = self.queue_dock.toggleViewAction()
        videos_action.setIcon(theme_icon("video-x-generic", QStyle.SP_FileDialogContentsView))
        toolbar.addAction(videos_action)
        settings_action = toolbar.addAction(theme_icon("configure", QStyle.SP_FileDialogDetailedView), "Settings…")
        settings_action.triggered.connect(lambda: self.edit_settings())
        self.show_folder()

        self.filter.setFocus()

        self.fill_demos()
        # once the window is up: the indexes kept take a moment to load
        self.library_opener = QTimer(self, singleShot=True, interval=0, timeout=lambda: self.library.open(
            settings.value("demos", ""), settings.value("exe", "")))
        self.library_opener.start()
        # the videos left when it last closed, which go on; only the first
        # window open keeps them, as another would write over its list
        self.closing = False
        os.makedirs(os.path.dirname(self.queue_file()), exist_ok=True)
        self.queue_lock = QLockFile(self.queue_file() + ".lock")
        # left by a window that crashed: its process is gone
        self.queue_lock.setStaleLockTime(0)
        self.keeps_queue = self.queue_lock.tryLock(0)
        if self.keeps_queue:
            self.load_queue()
        else:
            self.tell("Another window keeps the videos to record: this one's aren't kept when it closes")

    def make_list(self, columns, fit=True):
        """fit: columns fit their contents, which costs a look at every row
        each time one changes; else fit_columns() does it."""
        tree = QTreeWidget()
        tree.setHeaderLabels(columns)
        tree.setRootIsDecorated(False)
        tree.setAlternatingRowColors(True)
        tree.header().setSectionResizeMode(QHeaderView.ResizeToContents if fit else QHeaderView.Interactive)
        tree.header().setStretchLastSection(True)
        return tree

    def fit_columns(self, tree):
        """The columns fit their contents, but the first, which takes what's
        left."""
        for column in range(1, tree.columnCount()):
            tree.resizeColumnToContents(column)

    def tell(self, text, msec=0):
        """Shows text under the demos, for msec if given."""
        self.message.setText(text)
        self.message.setToolTip(text)
        if msec:
            self.message_timer.start(msec)
        else:
            self.message_timer.stop()

    # settings and game

    def ready(self):
        exe, game, demos = (self.settings.value(k, "") for k in ("exe", "game", "demos"))
        return os.path.isfile(exe) and os.path.isdir(game) and os.path.isdir(demos)

    def show_folder(self):
        folder = self.settings.value("demos", "")
        self.folder_action.setText(os.path.basename(folder.rstrip("/\\")) or "Choose the demos folder")
        self.folder_action.setToolTip((folder + "\n" if folder else "") + "Choose another folder of demos")

    def choose_folder(self):
        folder = QFileDialog.getExistingDirectory(self, "Choose the demos folder", self.settings.value("demos", ""))
        if not folder or folder == self.settings.value("demos", ""):
            return
        self.settings.setValue("demos", folder)
        self.show_folder()
        self.fill_demos()
        self.library.open(folder, self.settings.value("exe", ""))
        # the game has the folder it started with
        self.game.stop()

    def toggle_viewmodel(self, checked):
        val = "1" if checked else "0"
        self.settings.setValue("viewmodel", val)
        if self.game.running():
            self.game.send(f"set cg_drawviewmodel {val}")
        self.tell(f"Viewmodel: {'ON' if checked else 'OFF'}", 2000)

    def toggle_forcemodel(self, checked):
        val = "1" if checked else "0"
        self.settings.setValue("forcemodel", val)
        if self.game.running():
            self.game.send(f"set cg_forceModel {val}")
        self.tell(f"Force models: {'ON' if checked else 'OFF'} (Airborne / Winter 1)", 2000)

    def fill_followed_menu(self):
        """The models of the game files for the player followed, allies and
        axis, each with their team's only, the one chosen checked."""
        menu = self.followed_button.menu()
        menu.clear()
        models = player_models(self.settings.value("game", ""))
        for team, (key, chosen) in zip(("Allies", "Axis"), zip(("followed_allies", "followed_axis"),
                                                               followed_models(self.settings))):
            sub = menu.addMenu(team)
            group = QActionGroup(sub)
            own = team_models(models, team == "Axis")
            for model in ["", *own, *([chosen] if chosen and chosen not in own else [])]:
                action = sub.addAction(model or "As in the demo")
                action.setCheckable(True)
                action.setChecked(model == chosen)
                group.addAction(action)
                action.triggered.connect(lambda _=False, key=key, model=model: self.choose_followed(key, model))

    def choose_followed(self, key, model):
        self.settings.setValue(key, model)
        allies, axis = followed_models(self.settings)
        if self.game.running():
            self.game.send(f'set cg_followedModel "{allies}"', f'set cg_followedGermanModel "{axis}"')
        self.show_followed()
        self.tell(f"The player followed: {allies or 'theirs'} / {axis or 'theirs'}", 3000)

    def fill_camera_menu(self):
        """Where the camera is for the recorder and for the player followed,
        the one chosen checked."""
        menu = self.camera_button.menu()
        menu.clear()
        recorder, followed = cameras(self.settings)
        for title, key, choices, chosen in (("Recorder", "camera_recorder", RECORDER_CAMERAS, recorder),
                                            ("Player followed", "camera_followed", FOLLOWED_CAMERAS, followed)):
            menu.addSection(title)
            group = QActionGroup(menu)
            for choice, (label, _) in choices.items():
                action = menu.addAction(label)
                action.setCheckable(True)
                action.setChecked(choice == chosen)
                group.addAction(action)
                action.triggered.connect(lambda _=False, key=key, choice=choice: self.choose_camera(key, choice))

    def choose_camera(self, key, choice):
        self.settings.setValue(key, choice)
        if self.game.running():
            self.game.send(*(f"set {cvar} {value}" for cvar, value in camera_cvars(cameras(self.settings))))
        self.show_camera()
        recorder, followed = cameras(self.settings)
        self.tell(f"The recorder {RECORDER_CAMERAS[recorder][0].lower()}, the player followed "
                  f"{FOLLOWED_CAMERAS[followed][0].lower()}", 3000)

    def show_camera(self):
        recorder, followed = cameras(self.settings)
        self.camera_button.setToolTip(
            "Where the camera is, in videos too: for the recorder, in their eyes as recorded or behind them as when "
            "following a player; for the player they follow, behind them, in their eyes (made from their gun and "
            f"body, as their own view isn't recorded) or as the server showed them. Now: the recorder "
            f"{RECORDER_CAMERAS[recorder][0].lower()}, the player followed {FOLLOWED_CAMERAS[followed][0].lower()}")

    def show_followed(self):
        allies, axis = followed_models(self.settings)
        self.followed_button.setToolTip("The models of the player you spectate, and only them, when they're allies "
                                        f"and axis, in videos too: now {allies or 'theirs'} / {axis or 'theirs'}")

    def get_rec_kill_times(self):
        """Pre-kill and post-kill times in msec from settings for recording."""
        try:
            kb = int(float(self.settings.value("rec/kill_before", self.settings.value("kill_before", 4.0))) * 1000)
        except (ValueError, TypeError):
            kb = KILL_BEFORE
        try:
            ka = int(float(self.settings.value("rec/kill_after", self.settings.value("kill_after", 2.0))) * 1000)
        except (ValueError, TypeError):
            ka = KILL_AFTER
        return max(500, kb), max(500, ka)

    def edit_settings(self, tab="game"):
        if SettingsDialog(self.settings, self, tab).exec():
            self.show_folder()
            self.fill_demos()
            self.library.open(self.settings.value("demos", ""), self.settings.value("exe", ""))
            # the next one starts with them
            self.game.stop()

    def start_game(self):
        if not self.ready():
            self.tell("Set the game program, game files and demos folders in Settings")
            return
        self.game.stop()
        try:
            width, height = (int(v) for v in self.settings.value("size", "1280x720").split("x"))
        except ValueError:
            width, height = 1280, 720
        vm = self.settings.value("viewmodel", "1")
        fm = self.settings.value("forcemodel", "1")
        kb = self.settings.value("kill_before", "4.0")
        ka = self.settings.value("kill_after", "2.0")
        self.game.start(self.settings.value("exe"), self.settings.value("game"), self.settings.value("demos"), width, height,
                        viewmodel=vm, forcemodel=fm, kill_before=kb, kill_after=ka,
                        followed=followed_models(self.settings), censor=censored_words(self.settings),
                        camera=cameras(self.settings))
        self.tell("Starting the game…", 5000)
        # for its keys that go to frags
        self.game.send(*self.kill_filter().commands())
        if self.pending_demo:
            self.game.send("demo " + quoted(self.pending_demo))

    def game_output(self, line):
        if line.startswith("ERROR:") or "Couldn't load" in line or line.startswith("The game didn't start"):
            self.tell(line.replace("ERROR: ", ""))

    def game_exited(self):
        self.tell("The game quit. Double-click a demo to start it again.", 5000)

    # demos

    def fill_demos(self):
        """Lists the demos of the folder, in the folders they're in, with
        what the library knows of them, keeping the items there."""
        demos = list_demos(self.settings.value("demos", ""))
        for name in [n for n in self.demo_items if n not in demos]:
            item = self.demo_items.pop(name)
            (item.parent() or self.demos.invisibleRootItem()).removeChild(item)
        # the folders left empty, those in them first
        for folder in sorted(self.folder_items, key=len, reverse=True):
            item = self.folder_items[folder]
            if not item.childCount():
                (item.parent() or self.demos.invisibleRootItem()).removeChild(item)
                del self.folder_items[folder]
        self.demos.setSortingEnabled(False)
        for name, path in demos.items():
            item = self.demo_items.get(name)
            if item is None:
                try:
                    mtime = int(os.path.getmtime(path))
                except OSError:
                    mtime = 0
                date = QDateTime.fromSecsSinceEpoch(mtime).toString("yyyy-MM-dd hh:mm")
                item = self.demo_items[name] = SortItem([""] * len(DEMO_COLUMNS))
                item.setText(COL_DATE, date)
                item.setText(COL_DEMO, name.rsplit("/", 1)[-1])
                item.setData(COL_DEMO, Qt.UserRole, name)
                self.folder_item(posixpath.dirname(name)).addChild(item)
            summary = self.library.demos.get(name)
            if summary and item.data(COL_LENGTH, Qt.UserRole) != summary["duration"]:
                item.setText(COL_LENGTH, clock(summary["duration"]))
                item.setData(COL_LENGTH, Qt.UserRole, summary["duration"])
                item.setText(COL_RULES, RULES.get(summary["rules"], ""))
                item.setText(COL_RECORDER, summary["recorder"])
                item.setText(COL_MAP, ", ".join(m.rsplit("/", 1)[-1] for m in summary["maps"]))
                players = sorted(summary["players"].values(), key=lambda p: (-p[1], p[0].lower()))
                rules = f", {RULES[summary['rules']].lower()}" if summary["rules"] else ""
                tip = (f"<b>{html.escape(', '.join(summary['maps']))}</b> — {clock(summary['duration'])}{rules}<br>"
                       + html.escape(", ".join(player_kills(p) for p in players)))
                for column in range(len(DEMO_COLUMNS)):
                    item.setToolTip(column, tip)
        self.demos.setRootIsDecorated(bool(self.folder_items))
        self.demos.setSortingEnabled(True)
        self.filter_demos()

    def folder_item(self, folder):
        """The item of a folder in the demos folder, with those it's in; the
        list itself for none."""
        if not folder:
            return self.demos.invisibleRootItem()
        item = self.folder_items.get(folder)
        if item is None:
            parent = self.folder_item(posixpath.dirname(folder))
            item = self.folder_items[folder] = FolderItem(folder)
            item.setIcon(COL_DEMO, theme_icon("folder", QStyle.SP_DirIcon))
            item.setToolTip(COL_DEMO, plain_tip(folder))
            parent.addChild(item)
            item.setExpanded(True)
        return item

    def demos_of(self, items):
        """The names of the demos shown among items, and in the folders among
        them."""
        names = []
        for item in items:
            if item.isHidden():
                continue
            if isinstance(item, FolderItem):
                names += self.demos_of(item.child(i) for i in range(item.childCount()))
            elif item.data(COL_DEMO, Qt.UserRole):
                names.append(item.data(COL_DEMO, Qt.UserRole))
        return list(dict.fromkeys(names))

    def rules_changed(self):
        self.settings.setValue("rules", self.rules.currentData())
        self.filter_demos()

    def weapons_changed(self, _=None):
        chosen = [label for key, label in WEAPONS.items() if self.weapon_actions[key].isChecked()]
        self.weapon.setText(", ".join(chosen) if chosen else "Any weapon")
        self.kill_filter_timer.start()

    def kill_filter(self):
        """The kills chosen with the filters of the Frags row."""
        return KillFilter(self.killer.names(), self.victim.names(),
                          [key for key, action in self.weapon_actions.items() if action.isChecked()],
                          self.obj.isChecked(), self.great.isChecked(),
                          *(self.from_buttons[key].isChecked() for key in ("recorder", "specced", "unshown")))

    def apply_kill_filter(self):
        """The demos with kills the filter keeps. The game playing one is told
        it too, for its keys that go to frags."""
        self.kill_filter_timer.stop()
        self.filter_demos()
        self.game.send(*self.kill_filter().commands())

    def filter_demos(self):
        """Shows the demos with each word of one of the searches (separated
        by commas) in the name, a map or a player's name, and the players
        found; with the kill filter, only those with kills it keeps, and
        their killers; only those recorded by the recorders named, and of the
        rules chosen, if any, and not those shorter than MIN_LENGTH."""
        searches = search_parts(self.filter.text())
        rules = self.rules.currentData()
        recorders = [clean_name(name) for name in self.recorded_by.names()]
        kill_filter = self.kill_filter()
        found_any = False
        self.demo_frags = {}
        for name, item in self.demo_items.items():
            summary = self.library.demos.get(name) or {}
            found, shown = {}, not rules or summary.get("rules") in (rules, "both")
            shown = shown and summary.get("duration", MIN_LENGTH) >= MIN_LENGTH
            if shown and recorders:
                recorder = clean_name(summary.get("recorder", ""))
                shown = bool(recorder) and any(has_part(recorder, part) for part in recorders)
            if shown and searches:
                hits = search_demo(name, summary, searches)
                shown, found = hits is not None, hits or {}
            count = len(summary.get("kills", []))
            if shown and kill_filter:
                count, found = filter_kills(summary, kill_filter)
                shown = count > 0
            item.setHidden(not shown)
            if shown:
                self.demo_frags[name] = count
            best = sorted(found.values(), key=lambda p: (-p[1], p[0].lower()))
            item.setText(COL_PLAYER, ", ".join(player_kills(p) for p in best))
            found_any |= shown and bool(best)
        # a folder shows how many of its demos are shown, those in it first
        counts = {}
        for folder in sorted(self.folder_items, key=len, reverse=True):
            item = self.folder_items[folder]
            children = [item.child(i) for i in range(item.childCount())]
            count = counts[folder] = sum(counts[c.folder] if isinstance(c, FolderItem) else not c.isHidden()
                                         for c in children)
            item.setText(COL_DEMO, f"{posixpath.basename(folder)} ({count})")
            item.setHidden(not count)
            if (searches or recorders or kill_filter) and count:
                item.setExpanded(True)
        self.demos.setColumnHidden(COL_PLAYER, not found_any)
        self.fit_columns(self.demos)
        self.show_demo_count()

    def show_demo_count(self):
        """How many demos there are, how many are listed and their frags, or
        how far indexing them is; what Record is for."""
        chosen = self.chosen_demos()
        self.record_button.setText(f"Record {len(chosen):,} demo{'s' if len(chosen) != 1 else ''}…" if chosen else "Record…")
        self.record_button.setEnabled(bool(chosen))
        progress = self.library.progress()
        if progress:
            self.demo_count.setText(f"Indexing demos: {progress[0]:,} of {progress[1]:,}")
            self.demo_count.setToolTip("")
            return
        found = len(self.demo_items)
        listed = sum(not item.isHidden() for item in self.demo_items.values())
        summaries = [self.library.demos.get(name) for name in self.demo_items]
        indexed = sum(summary is not None for summary in summaries)
        short = sum(summary is not None and summary["duration"] < MIN_LENGTH for summary in summaries)
        if self.demos.selectedItems():
            text = f"{len(chosen):,} of the {listed:,} demos selected"
        else:
            text = f"{found:,} demo{'s' if found != 1 else ''}" if listed == found else f"{listed:,} of {found:,} demos listed"
        # at most what the clips take, as those of frags close together are one
        frags = sum(self.demo_frags.get(name, 0) for name in chosen)
        if frags:
            before, after = self.get_rec_kill_times()
            text += f", {frags:,} frag{'s' if frags != 1 else ''}, {about(frags * (before + after))}"
        self.demo_count.setText(text)
        tip = [f"{found:,} demos in the folder, {indexed:,} of them indexed"]
        if found > indexed:
            tip.append(f"{found - indexed:,} couldn't be indexed")
        if short:
            tip.append(f"{short:,} shorter than {MIN_LENGTH // 60000} minutes aren't listed")
        if found - short - listed > 0:
            tip.append(f"{found - short - listed:,} left out by the search and the filters")
        self.demo_count.setToolTip("; ".join(tip))

    def chosen_demos(self):
        """The names of the demos Record is for: those selected, else all
        those listed, as they're listed."""
        root = self.demos.invisibleRootItem()
        return (self.demos_of(self.demos.selectedItems())
                or self.demos_of(root.child(i) for i in range(root.childCount())))

    def players_listed(self):
        """The players of the demos listed, those with the most frags first,
        for the menus of Fragger and Victim: (players, labels)."""
        players = {}
        for name in self.demo_frags:
            for key, (player, kills) in (self.library.demos.get(name) or {}).get("players", {}).items():
                players.setdefault(key, [player, 0])[1] += kills
        ranked = sorted(players.values(), key=lambda p: (-p[1], p[0].lower()))[:self.MOST_PLAYERS]
        return [player for player, _ in ranked], {player: f"{player} ({kills})" for player, kills in ranked}

    def library_changed(self):
        # those who recorded the most first
        recorders = {}
        for summary in self.library.demos.values():
            if summary["recorder"]:
                recorders.setdefault(clean_name(summary["recorder"]), [summary["recorder"], 0])[1] += 1
        ranked = sorted(recorders.values(), key=lambda r: (-r[1], r[0].lower()))
        self.recorded_by.players = [name for name, _ in ranked]
        self.recorded_by.labels = {name: f"{name} ({count})" for name, count in ranked}
        self.fill_demos()

    def play_demo(self, name):
        self.pending_demo = name
        if self.game.running():
            self.game.send("demo " + quoted(name))
        else:
            self.start_game()
        self.tell("Playing " + name, 5000)

    # recording

    def record_demos(self):
        """The demos chosen (chosen_demos): the kills of the filter, or all of
        them, in one video or one each; of one demo, a stretch of it too."""
        names = self.chosen_demos()
        if not names:
            return
        kill_filter = self.kill_filter()
        indexes = {name: self.library.index(name) for name in names}

        def plans_for(before, after):
            """What each choice records of each demo with that lead-in and
            out, from the indexes kept: {key: {name: (count, spans or None)}},
            the demos with some."""
            plans = {"kills": {}, "everything": {}}
            for name, index in indexes.items():
                plans["everything"][name] = demoonly_plan(index) if index else (0, None)
                plan = demoonly_plan(index, "kills", before, after, kill_filter) if index else (0, [])
                if plan[0]:
                    plans["kills"][name] = plan
            return plans

        def choices(plans):
            count = sum(plan[0] for plan in plans["kills"].values())
            spans = [plan[1] for plan in plans["kills"].values()]
            label = the(count, "frag listed", "frags listed") + (f", in {len(spans):,} demos" if len(spans) > 1 else "")
            whole = [plan[1] for plan in plans["everything"].values()]
            return [("kills", label + (", " + about(sum(length(s) for s in spans)) if spans else ""), bool(spans)),
                    ("everything", ("All of the demo" if len(names) == 1 else f"All of the {len(names):,} demos")
                     + (", " + about(sum(length(s) for s in whole)) if None not in whole else ""), True)]

        dialog = RecordDialog(self.settings, choices(plans_for(*self.get_rec_kill_times())),
                              0 if len(names) == 1 else None, join=len(names) > 1, parent=self,
                              relabel=lambda before, after: choices(plans_for(before, after)))
        if not dialog.exec():
            return
        what = dialog.what()
        # who's in the videos' names
        player = ", ".join(kill_filter.names)
        if what == "range":
            start, end = dialog.times()
            self.add_jobs(dialog.jobs(f"{names[0]}, {clock(start)} to {clock(end)}", [range_clip(names[0], start, end)],
                                      names[0], player))
            return
        plans = plans_for(*dialog.kill_times())
        chosen = list(plans[what])
        only, kind = ("kills", "frags" + kill_filter.words()) if what == "kills" else ("", "")
        player = player if only else ""
        clips = [only_clip(name, only, plans[what][name][1], kill_filter) for name in chosen]
        for clip in clips:
            if clip["spans"]:
                clip["frags"] = clip_frags(indexes[clip["demo"]], clip["spans"], only, kill_filter)
        if dialog.join.isChecked() or len(clips) == 1:
            demo = chosen[0] if len(chosen) == 1 else f"{len(chosen)} demos"
            self.add_jobs(dialog.jobs(demo + (f", {kind}" if kind else ""), clips, demo, player))
        else:
            for name, clip in zip(chosen, clips):
                self.add_jobs(dialog.jobs(name + (f", {kind}" if kind else ""), [clip], name, player))

    def add_jobs(self, jobs):
        for job in jobs:
            self.add_job(job)

    def add_job(self, job, start=True):
        """Records it after the ones before it."""
        job["item"] = QTreeWidgetItem(["", "", ""])
        self.jobs.append(job)
        self.queue.addTopLevelItem(job["item"])
        self.show_job(job)
        if self.queue_dock.isHidden():
            self.queue_dock.show()
            self.resizeDocks([self.queue_dock], [160], Qt.Vertical)
        if start:
            self.save_queue()
            self.next_job()

    def show_job(self, job):
        """Its line in the Videos panel."""
        item, status = job["item"], job["status"]
        item.setText(0, os.path.basename(job["output"]))
        item.setText(1, job["label"])
        # the clips that didn't record, and why
        skipped = [clip for clip in job["clips"] if clip.get("skipped")]
        tip = "\n".join(f"{clip['demo']}: {clip['skipped']}" for clip in skipped)
        if status == "done":
            several = "s" if len(skipped) > 1 else ""
            text = f"Done, {len(skipped)} clip{several} left out" if skipped else "Done"
        elif status == "failed":
            tip = job.get("error") or "It didn't record."
            text = "Failed: " + tip.splitlines()[0]
        elif status == "paused" and job.get("next_clip"):
            text = f"Paused after clip {job['next_clip']} of {len(job['clips'])}"
        else:
            text = {"waiting": "Waiting", "recording": "Recording…", "paused": "Paused"}.get(status, "Canceled")
        item.setText(2, text)
        item.setToolTip(2, tip)
        item.setToolTip(0, job["output"] if status == "done" else "")
        item.setForeground(2, QColor(Qt.red) if status == "failed" else self.queue.palette().text())

    def queue_file(self):
        """Where the videos are kept when the app closes: next to the
        settings."""
        return os.path.join(os.path.dirname(self.settings.fileName()), "queue.json")

    def save_queue(self):
        if not self.keeps_queue:
            return
        path = self.queue_file()
        try:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path + ".new", "w", encoding="utf-8") as f:
                json.dump([{key: value for key, value in job.items() if key != "item"} for job in self.jobs], f)
            os.replace(path + ".new", path)
        except OSError as e:
            self.tell(f"Couldn't keep the videos to record: {e}")

    def load_queue(self):
        """The videos kept: the one recording goes on, in its turn."""
        try:
            with open(self.queue_file(), encoding="utf-8") as f:
                jobs = json.load(f)
        except (OSError, ValueError):
            return
        for job in jobs:
            if job.get("status") == "recording":
                job["status"] = "waiting"
            self.add_job(job, start=False)
        QTimer.singleShot(0, self.next_job)

    def next_job(self):
        if self.recorder or self.closing:
            return
        job = next((j for j in self.jobs if j["status"] == "waiting"), None)
        if job:
            job["status"] = "recording"
            self.show_job(job)
            self.save_queue()
            self.recorder = Recorder(self, job, self.job_progress, self.job_done)
            self.recorder.start()

    def job_progress(self, job, text):
        job["item"].setText(2, text)

    def job_done(self, job, error, video):
        self.recorder = None
        if video:
            job["status"], job["output"] = "done", video
            self.tell("Recorded " + video)
            if job.get("numbers"):
                try:
                    write_clip_list(job["clips"], video)
                except OSError as e:
                    self.tell(f"Recorded {video}, but couldn't write its list of clips: {e}")
        elif error in ("paused", "canceled"):
            job["status"] = error
        else:
            job["status"], job["error"] = "failed", error
            self.tell(f"Couldn't record {os.path.basename(job['output'])}: {error.splitlines()[0]}")
        self.show_job(job)
        self.save_queue()
        self.next_job()

    def selected_jobs(self):
        return [j for j in self.jobs if j["item"].isSelected()]

    def pause_jobs(self):
        for job in self.selected_jobs():
            if job["status"] == "waiting":
                job["status"] = "paused"
                self.show_job(job)
            elif job["status"] == "recording" and self.recorder:
                self.recorder.pause()
        self.save_queue()

    def resume_jobs(self):
        for job in self.selected_jobs():
            if job["status"] in ("paused", "failed", "canceled"):
                job["status"] = "waiting"
                job.pop("error", None)
                self.show_job(job)
        self.save_queue()
        self.next_job()

    def record_jobs_again(self):
        """The videos selected that are done are recorded again, in their
        turn, by a new game (with the game files as they are now): the same
        clips with the same settings, into a new video next to the one made,
        named as it was first, numbered (2), (3)..."""
        for job in self.selected_jobs():
            if job["status"] == "done":
                base, ext = os.path.splitext(job["output"])
                job["output"] = re.sub(r" \(\d+\)$", "", base) + ext
                job["status"] = "waiting"
                self.show_job(job)
        self.save_queue()
        self.next_job()

    def move_jobs(self, step):
        """The videos selected one place up (-1) or down (1) the list, which
        is the order they're recorded in."""
        chosen = {id(job) for job in self.selected_jobs()}
        order = list(self.jobs)
        places = range(len(order)) if step < 0 else range(len(order) - 1, -1, -1)
        for i in places:
            j = i + step
            if id(order[i]) in chosen and 0 <= j < len(order) and id(order[j]) not in chosen:
                order[i], order[j] = order[j], order[i]
        self.jobs = order
        for job in order:
            self.queue.takeTopLevelItem(self.queue.indexOfTopLevelItem(job["item"]))
        for job in order:
            self.queue.addTopLevelItem(job["item"])
            job["item"].setSelected(id(job) in chosen)
        self.save_queue()

    def cancel_jobs(self):
        for job in self.selected_jobs():
            if job["status"] in ("waiting", "paused"):
                if job.get("parts_dir"):
                    shutil.rmtree(job["parts_dir"], ignore_errors=True)
                for key in ("parts_dir", "made", "next_clip"):
                    job.pop(key, None)
                job["status"] = "canceled"
                self.show_job(job)
            elif job["status"] == "recording" and self.recorder:
                self.recorder.cancel()
        self.save_queue()

    def remove_finished(self):
        for job in [j for j in self.jobs if j["status"] in ("done", "failed", "canceled")]:
            self.jobs.remove(job)
            self.queue.takeTopLevelItem(self.queue.indexOfTopLevelItem(job["item"]))
        self.save_queue()

    def open_video(self, item):
        job = next((j for j in self.jobs if j["item"] is item), None)
        if job and job["status"] == "done":
            QDesktopServices.openUrl(QUrl.fromLocalFile(job["output"]))

    def open_folder(self):
        job = next((j for j in self.jobs if j["item"].isSelected()), None)
        folder = os.path.dirname(job["output"]) if job else self.settings.value("rec/folder", "")
        if folder and os.path.isdir(folder):
            QDesktopServices.openUrl(QUrl.fromLocalFile(folder))

    def closeEvent(self, event):
        """The videos left are kept, the one recording with its clips made,
        to go on when it opens again; not when another window keeps them,
        which asks first."""
        if self.closing:
            super().closeEvent(event)
            return
        left = sum(1 for j in self.jobs if j["status"] in ("waiting", "recording", "paused"))
        if not self.keeps_queue and left and QMessageBox.question(
                self, "Quit", f"{left} video{'s are' if left > 1 else ' is'} still to record, and won't be kept: "
                "another window keeps the videos to record. Quit anyway?",
                QMessageBox.Yes | QMessageBox.No, QMessageBox.No) != QMessageBox.Yes:
            event.ignore()
            return
        self.closing = True
        self.library_opener.stop()
        self.library.stop()
        if self.recorder and self.keeps_queue:
            job = self.recorder.job
            self.recorder.pause()
            job["status"] = "waiting"
        elif self.recorder:
            self.recorder.cancel()
        self.save_queue()
        if self.keeps_queue:
            self.queue_lock.unlock()
        self.game.stop()
        super().closeEvent(event)


def main():
    parser = argparse.ArgumentParser(description="Rewatch Medal of Honor: Allied Assault demos.")
    parser.add_argument("--exe", help="the openmohaa program")
    parser.add_argument("--game", help="the folder with the game files (main/Pak0.pk3)")
    parser.add_argument("--demos", help="the folder with the demos (.dm3)")
    parser.add_argument("demo", nargs="?", help="a demo to play: its path in the demos folder, without .dm3")
    args = parser.parse_args()

    app = QApplication(sys.argv)
    app.setApplicationName("mohdemo-replay")
    settings = QSettings("mohdemo-replay", "mohdemo-replay")
    for key in ("exe", "game", "demos"):
        if getattr(args, key):
            settings.setValue(key, os.path.abspath(getattr(args, key)))
    if not settings.value("exe"):
        built = [os.path.join(REPO, ".cmake", "RelWithDebInfo", name)
                 for name in ("openmohaa", os.path.join("openmohaa.app", "Contents", "MacOS", "openmohaa"))]
        settings.setValue("exe", next((p for p in built if os.path.isfile(p)), shutil.which("openmohaa") or ""))
    settings.setValue("exe", game_program(settings.value("exe")))

    window = Window(settings)
    window.show()
    if not window.ready():
        window.edit_settings()
    elif args.demo:
        window.play_demo(args.demo)
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
