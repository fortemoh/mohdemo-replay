# MoH Demo Replay

Make videos of the frags in Medal of Honor: Allied Assault demos (`.dm3`): a
list of demos, filters for the frags you want, and Record. It needs OpenMoHAA
built from this repository, which adds the demo commands, Python 3 and PySide6.

    python3 replay/mohreplay.py [--exe PATH] [--game FOLDER] [--demos FOLDER] [DEMO]

On the first run, Settings asks for the game program (found by itself when
built in `.cmake/RelWithDebInfo/`) and the folder with the game files (the one
with `main/Pak0.pk3`), then it asks for the folder with the demos, chosen after
that with its button, whose folders are looked in too: the list shows them in their folders, which tell how many of
their demos are shown, and a demo there is named by its path in the folder
of demos (the search finds a folder's name too). The list shows each
demo's length (demos under 5 minutes aren't listed, unless they have frags and Short demos is ticked), recorder, levels and rules (default or realism servers, told by the
recorder's speeds with each weapon; blank when they spectated or played
too little), Rules shows only default or realism
demos, Recorder only the demos recorded by the players named (parts of
names separated by commas, as the search takes them; its menu lists the
recorders, those of the most demos first), and the search finds demos by
name, level or player, those with all of its words, and commas separate
searches (`pstn v2, tommeh` finds pstN's demos on v2 and tommeh's demos):
for a player, it shows their frags in each demo. In all of these, a `*`
stands for any one character: `gr*m` finds Grim and Gr1m (not Grm or
Grimm), as Fragger and Victim below and the game's `demofilter` take it. For that, `mohdemoindex`, built next to the
game, indexes the demos in the background the first time (about two minutes
for 700, and again once after it changes). The indexes are kept next to the
demos, in a hidden file in each of their folders (`.mohdemo-index`), so they go
where the demos go and count whichever folder above them you open; for a folder
that can't be written, in `~/.cache/mohdemo-replay`. Under the list, it tells how far indexing is, then
how many demos there are and how many are listed, with their frags and about
how long a video of them is ("53 of 5,139 demos listed, 412 frags, about 34
min", at most, as frags close together share a clip), its tip why the others
aren't listed.

Fragger, Victim and Weapon, under the search, choose the frags, those of
the demos listed, each on its own or together:
Fragger and Victim take parts of names, separated by commas, as the search does
(`pstn, tommeh` finds pstN's frags under any clan tag, and tommeh's), and
their menus add the players of the demos listed, those with the most frags
first. A player is who they are,
not the name they take: one who renames to another player's name (as a
team taking one player's name for fun) keeps their frags under their own,
while one who just renames goes by the new name; Weapon ticks sniper, rifle,
SMG, StG / BAR (the StG 44 and the BAR, which the game calls machine guns),
shotgun, pistol, bazooka, melee (a hit with a gun's butt) or other, as the
death messages tell (also the French, German, Spanish and Portuguese ones
seen; the French write a rifle frag as a sniper frag), and Obj maps
keeps the frags made on the objective maps (the levels in `obj/`, as
obj_team4 or harbor), not on the deathmatch ones (`dm/`), as a demo can
have both. Great leaves out the slow scoped snipes: sniper frags made
with the scope on for over a second while standing still, as the fragger's
view shows them; no-scopes, quick scopes, shots on the move and streaks
(frags of the same fragger at most 5 s apart, which one clip shows) stay,
and so do the frags of other weapons and those the demo doesn't show from
the fragger's view. It leaves out the frags of AFK players too, any weapon:
victims who had given no input for over 5 s, not moving, turning, firing,
crouching or leaning, as long as the demo showed them (demos indexed by an
older version are indexed again once for it). From says who the frags are
from, any of these together, and remembers it: Recorder is the recorder's own
frags, while they play; Specced those of the players the recorder spectated,
while they followed them; Unspecced those made while the demo shows someone
else, filmed from behind the fragger, when the demo has them for 3 s before and
their victim in view then (about half of those frags): the server only sends
the players the recorder could see. It may send a player without the gun in
their hand, so that one is worked out from what their body is doing, with its
shots and their sound: the gun last seen with those animations, else their
team's. The HUD of those is a spectator's, with
their name but no health. A door the demo doesn't have
there is drawn closed, from the map, until the demo has it. With none of them,
it's anyone's frags, as the demo shows them; with a Fragger, those made while
the demo shows them, and only while they're shown, so a video of them never
shows someone else. The list then shows only the demos with such frags, with
how many of each fragger.

Double-click a demo to watch it in the game, with the keys below.

Record makes MP4s of the demos selected, or in the folders selected, or of
all those listed when none is, as its button tells ("Record 148 demos…"):
the frags chosen, or all of each demo, in one video or one each, and of one
demo, a stretch of time too. Only the choices with something to record are offered; each tells how many
frags it holds and how long the video will be, and the last one is chosen
again next time. Number the clips puts a number on each clip of the
video (#1, #2 and on through all of its demos, at the bottom of the
screen), and the list of the clips next to it, a `.csv` of the same name:
each one's number, start in the video and demo, and the time, fragger,
victim and weapon of each of its frags. Split into parts of at most
(60 min, say), records a longer video as "<name> part 1.mp4", "<name> part
2.mp4" and on, each at most that long, cut between frags (a stretch longer
than a part, as all of a long demo, is cut in it), the clip numbers going on
from one part to the next, each with its list. Videos are recorded one after the
other by a second copy of the game, in the background, without a window on
Linux and faster than real time; FFmpeg joins the
clips of a video without encoding them again. A clip the game doesn't
record, as its demo's map isn't installed or the game quits in it, is tried
again once in a new game, then left out, the video going on without it
("Done, 1 clip left out", its tip which and why); three in a row stop the
video. The Videos list, under the demos, shows them with how far the one
recording is and how long it has left, in the order they're recorded in, and
how many there are of each kind. Pause holds those selected (the one recording
stops, keeping its clips made, and goes on from the next one), and turns into
Resume for those paused, failed or canceled, the arrows move them up and down,
Cancel, Open folder; under More, Record again records those done once more (the
same clips and settings, with the game as it is now, as after an update: a new
video next to the first, numbered "(2)"), Play live and Remove finished.
Double-click to watch one; one not made yet plays live. Play live, also a
button in the Record dialog, plays the video in a game window as it would be
recorded, with its settings, without recording anything: clip after clip, in
real time with sound, pausing after the last; close the game to stop. Closing the app
keeps them, the one recording with its clips made, and they go on when it
opens again (in `queue.json`, next to its settings). Only the first window
open keeps them: another one doesn't write over its list, and asks before
closing with videos left, which aren't kept. A video records from the
folder of demos it was queued from, also when another folder is opened
before its turn. Each lands in the folder chosen, named from a pattern
(`{demo}`, the demo's file name, `{start}`, `{player}`, `{date}`), with each
frag played from how long before it to how long after.

How it looks, in Record, is what the videos show: the cameras of the recorder
and of the player they follow, the model of the player followed, and only them,
one when they're Allies and one when they're Axis, from that team's player
models in the game files, or as in the demo (`cg_followedModel`,
`cg_followedGermanModel`), the gun in the player's hands, and Force models,
everyone as an airborne soldier or a winter German, whatever they wore. A demo
watched in the game looks the same.

Settings has what's rarely touched. Setup is where the game and FFmpeg are.
Video has the size, frames per second, quality, motion blur and sound, which
the Record dialog sums up in a line, with Change… to set them there; under
Advanced, the codec (H.264, H.265 or the graphics card's H.264 encoder, with
VAAPI) and FFmpeg output options to use instead. Censor has the words starred
out of the players' names and the messages, in the game and in videos (on, with
words a video can be flagged for, which you can change): however they're
spelled, any case, letters repeated, look-alike digits and signs (n1gg3r, f@g),
accents, anything between the letters (n.i.g.g.e.r) or letters spaced out
(g a y), in each word but not across words, a letter a word has twice having to
be there twice ("Nigeria" stays). Motion blur is the renderer's: what moves blurs
over all of a frame's time, as with a camera, along each pixel's motion.
Motion looks smoother, a cut between two frags stays clean, and recording
takes about 1.7 times as long with the graphics card's encoder, 1.1 times
with H.264 (at 1920x1080, 60 frames a second). The clips are written in a hidden
`.mohreplay-parts` folder next to the video, not in memory. It needs FFmpeg.

The game built here, on MoHAA Reloaded's Vulkan port, runs on Linux only:
that port has no macOS build.

The app starts the game with a throwaway home folder, sends it console
commands through its pipe (`com_pipefile`) and reads what the game reports
in `demostate.json` (`cl_demoFiles 1`). On Linux it
asks for Wayland first, then XWayland. The settings changed in the game (its
options, the console, key binds) are kept for the next games in the game
folder, as `main/configs/omconfig.cfg` and `main/omconfig.cfg`, which are
read first, and in `~/.config/mohdemo-replay/omconfig.cfg`, and videos are recorded with them. The
app's keys and its pipe aren't kept, so a game run from that folder without
the app doesn't get them. Some
are always the app's: the window size, the app's keys, a field of view of
80, the most detailed models (`r_uselod 0`), the viewmodel and the models
forced, and frames drawn at the screen's refresh rate (`r_swapInterval 1`,
`com_maxfps 0`).

## Keys in the game window

During a demo, only Esc brings up the menu and Tab shows the scores; the
other letters, numbers, Space and Enter do nothing. The app binds these keys:

| Key | Does |
| --- | --- |
| Pause | pause or play |
| Left / Right | back or forward 5 seconds |
| Down / Up | previous or next frag listed: the frags chosen in the app |
| PgDn / PgUp | previous or next round |

## Console commands

The game's commands call frags kills, and fraggers killers.

The buttons only send these, so they also work in the console or with key
binds, without the app:

| Command | Does |
| --- | --- |
| `demoseek <time>` | jump to a time (seconds or minutes:seconds), alone prints the time |
| `demoskip <time>` | move forward, or back with a negative time |
| `demopause [0\|1]` | pause or play, toggles without an argument |
| `demonextkill [player]`, `demoprevkill [player]` | jump to before the next or previous kill of `demofilter`, by that player while shown if given |
| `demonextshownkill`, `demoprevshownkill` | jump to before the next or previous kill of whoever is shown |
| `demonextround`, `demoprevround` | jump to the next or previous round |
| `demofilter [clear \| shown \| killer <name> \| victim <name> \| weapon <weapon>... \| level <name> \| great [<msec> [<speed> [<streak msec> [<idle msec>]]]] \| recorder \| specced \| unshown [only]]` | the kills the kill commands go to: by players whose names have one of the killers in them, while shown, or by whoever is shown, of players whose names have one of the victims (a `*` in a name stands for any one character), made with one of the weapons (`pistol`, `rifle`, `sniper`, `smg`, `mg`, `shotgun`, `rocket`, `melee`, `other`), in levels whose names have one of the levels in them, any case (`level obj/`: the objective maps), and `great`: not the sniper kills the killer's view shows made with the scope on for over `msec` (1000) at `speed` (30) or slower, but those with another kill of their killer within `streak msec` (5000), and not the kills of victims who had given no input for over `idle msec` (5000), `recorder`: only the recorder's own kills, while they play, `specced`: only the kills of the players the recorder followed as a spectator, while followed, and `unshown`: also the kills of the killers while they aren't shown, seen from behind them, when the demo has them for 3 seconds before and their victim in view then, with `unshown only` only those; each adds, but `clear`; alone, it prints them. A player given to a command is the killer instead, the victims, weapons and levels still apply |
| `demoonly kills [player]` | play only the kills of `demofilter`, by that player while shown if given |
| `demoonly multikills [player]` | play only the multi-kills, by that player while shown if given |
| `demoonly shownkills` | play only the kills of whoever is shown (the recorder playing, or the player they follow), while they're shown |
| `demoonly shownmultikills` | the same, of the multi-kills |
| `demoonly watched [player]` | play only while that player is shown, or one of `demofilter`'s killers |
| `demoonly` | play everything again |
| `timescale <speed>` | playback speed, 1 is normal |
| `demovideo <name> [end time]` | record into an MP4, see below |

`cl_demoKillBefore` and `cl_demoKillAfter` set how many seconds of each kill
are shown (4 and 2). A clip doesn't end while its fragger is firing: the time
after a kill counts from their last shot, each shot before it's over starting
it again, while they're the one shown (`cl_demoKeepShooting 0` turns that off).
A video can so run longer than planned, as the clip list's times are the planned
ones. Kills less than a second apart play as one, without a
jump, except for a player's kills or whoever's is shown when the player
shown changes in between: that stays a cut. A multi-kill is two kills or
more by a player, each at most `cl_demoMultiKill` seconds (3) after the one
before.

Team kills (the killer and the victim last seen on the same team, also when
everyone plays on one team) aren't kills: they aren't listed, counted, part
of multi-kills or gone to by the next and previous kill, and they only play,
and are only recorded, with a kill by the same player at most 2 seconds
before or after them: a teammate hit while shooting at an enemy. Grenade
kills (a grenade or its shrapnel, as the death message tells) aren't kills either, nor
is the death of the player shown at the time: the recorder killed while
playing, or the player they follow. A kill plays only inside its level: its
clip starts no earlier than the level and ends with it.

While the recorder follows a player, most servers with the 1.12 Reborn patch
put the camera in that player's head, where their own arms and gun are in
the way. `cg_followcamera 1` (the default) puts it behind them, where stock
servers do; `cg_followcamera 0` shows it as recorded; `cg_followcamera 2`
shows them from their own eyes, as if they had recorded the demo: where
they are, look, crouch and zoom are theirs in the demo, and their arms and
gun are drawn from the gun in their hand, firing, rechambering and
reloading as it does, with their lean from their body (their ammo isn't in
the demo), and their frags show "You killed <victim>", which the server told
only them. The HUD shows their health, which the spectator views don't, and
their hits kick their view and flash
the screen red as they did for them, made again from
their health dropping (the demo doesn't have them for a player followed) and
from the shot or explosion that came with it. A hit with neither, like a
fall, flashes without a kick. In a demo, a player leaning where their eye
would face a wall right in front of it, as next to a window, sees from where they stand
instead, as their own game showed them (the server fires from where their
game puts the eye, and the demo has the start of each shot). The other way,
`cg_recordercamera 1` shows the recorder from behind while they play, their
body moving against the camera as a followed player's does (`cg_recordersmooth
0` keeps it fixed in the view), where
the camera following a player is, as a spectator following them sees them:
without their scope, crosshair, weapons, ammo, health or hits on the screen.
Both have the name of who is shown on the left, as a spectator has. Record
chooses both, for videos too. Health only shows from someone's own eyes: the
recorder playing, or a player followed with `cg_followcamera 2`.

A demo doesn't tell whether a player it shows from behind while they aren't the
one shown (`demofilter unshown`) has a scope on. So one is made up for their
sniper kills from `cg_demoscopedist` units (1000) or more, of which nearly all
had one, from `cg_demoscopetime` msec (400) before the shot until just after it,
as a spectator following them sees it: the camera behind them zoomed in. So
are their shots of a rifle with a scope that kill nobody and go as far, to
where they hit. Where they stand, how their aim moves and where the shot lands
are the demo's. `cg_demoscope 0` turns it off.

Demos don't show the score and time left, top right, the compass, the weapons
bar that pops up as the player shown changes weapons, the icons over players'
heads, the stopwatch of planting
or defusing a bomb, the weapons players carry on their bodies, which only some
servers' mods put there (`cg_demoholsters 1` shows them), or the server's messages
about players coming and going
("<player> has entered the battle", which it tells everyone each time a
player enters, as at every round, has left the battle, has joined the
Allies or the Axis, is preparing for deployment), what the player picks
up (ammo, health and weapons, the yellow messages), the bomb's messages
(planted, defused, still set), "Will switch to new weapon next time you
respawn" or what the players write (their chat, with
the words of voice commands and the server console's): they fill the
message box; the frags' messages stay, in the middle at the top of the
screen. "You killed <victim>", which only the killer gets, shows from someone's
eyes, not from behind them, on the left at the same height. In the middle of the screen, only the
round results show ("Axis win!", in the server's language): not the server's
welcome ("Welcome <player>, have fun!") or ads, "Press fire to join the
battle!" or the warnings to the recorder about being idle. Of the texts some
servers add to the screen, their welcome and web addresses aren't shown,
also while they're typed out letter by letter. After a jump, the message
boxes start empty: the messages of the time jumped over aren't shown, nor
those from before it. Of the commands a server sent the one who recorded it
(the menus to choose a team or a weapon, settings it wanted, like
`cg_3rd_person` or `r_fastsky`), a demo only runs its sounds and music.

## Recording

`demovideo <name> [end time]` records the demo from now on into
`videos/<name>.mp4` in the game's home folder, through FFmpeg, until the end
time, `stopvideo`, the end of the demo or the end of what `demoonly` plays.
Seeks and pauses aren't recorded, so `demoseek 12:30; demovideo clip 12:45`
records exactly 15 seconds, and `demoonly kills t-; demovideo frags` all of
a player's kills one after the other. `cl_demoClipNumbers <n>` draws a
number on what plays: n on the first clip `demoonly` plays and the next
ones counting on, or n on all of the demo; 0 (the default) draws none.

The game steps exactly 1/`cl_aviFrameRate` of a second per frame, so videos
are smooth whatever the screen does; it records faster than real time when
it can. The sound is recorded when the game is started with
`+set s_loopback 1`: it is then rendered for the video and not played.
`cl_aviPipeFormat` holds FFmpeg's output options (H.264, CRF 20, AAC 192k by
default) and `cl_aviFFmpeg` the FFmpeg program; FFmpeg's errors go to
`<name>.mp4.log`. The fps counter (`fps 1`) isn't drawn in videos.

## Tests

    python3 replay/tests/test_mohreplay.py [-v]

They run the app without a window against a fake game
(`replay/tests/fakegame.py`), so they need neither game files nor a screen,
and run with the engine's tests (`ctest`). With `MOHREPLAY_TEST_GAME` (the
game files folder) and `MOHREPLAY_TEST_DEMOS` (a folder of real demos) set,
they also play and record a real demo with the game built in
`.cmake/RelWithDebInfo`, without a window.
