/*
===========================================================================
Copyright (C) 2026 the OpenMoHAA team

This file is part of OpenMoHAA source code.

OpenMoHAA source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

OpenMoHAA source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with OpenMoHAA source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

// cl_uimain.cpp: the Reloaded main menu (Added), ui/reloaded_main.urc, shown
// for the stock "main" (cl_uimenus.cpp's aliases), its confirmations (Quit,
// Disconnect, Delete), ui/reloaded_confirm.urc, shown over it, the
// connect's box, ui/reloaded_connect.urc (CONNECTING), and the HDR
// calibration's screens, ui/hdr_calibrate_*.urc. Project: Omaha's
// screens, stripped down, in our look: a side rail (the star and the
// wordmark, MULTIPLAYER / CAMPAIGN / HOST / OPTIONS, QUIT at its foot) over
// maps' photos blurred once to the screen's shape (no map loaded behind
// it); MULTIPLAYER is Omaha's server browser (the game's master server
// lists, queried only while the menu is up) and its profile (the name, each
// side's model in a 3D view, the views being the menu's labels "allies" and
// "axis"); CAMPAIGN the single player missions and saved games.
//
// Sizes are design units (cl_uidraw.h): a 1080-line screen, the width what
// the screen's shape gives; the screens beside the rail from x 480 (420
// under 1700 units across), 48 from the edges, 1640 across at most.

#include "client.h"
#include "cl_ui.h"
#include "cl_uiwidget.h"
#include "cl_uimenus.h"
#include "../uilib/uibake.h"
#include "../gamespy/goaceng.h"
#include "../gamespy/sv_gamespy.h"
#include "../fgame/bg_voteoptions.h"
#include "../qcommon/localization.h"

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

// the palette's panels (Omaha's surfaces, ours)
static const rlColor_t RL_PANEL = {10 / 255.0f, 11 / 255.0f, 12 / 255.0f, 218 / 255.0f};
static const rlColor_t RL_HEAD  = {0, 0, 0, 70 / 255.0f};
static const rlColor_t RL_LINE  = {1, 1, 1, 24 / 255.0f};
static const rlColor_t RL_SOFT  = {200 / 255.0f, 196 / 255.0f, 186 / 255.0f, 1};
static const rlColor_t RL_SEL   = {222 / 255.0f, 178 / 255.0f, 80 / 255.0f, 44 / 255.0f};

#define RL_GLYPH(name) RL_OUTLINES "glyphs/" name ".svg"

// the lines things line up on: the screens 48 from the screen's edges, 16
// inside their panels, their foot a 64-unit row of controls 42 high
// (REFRESH, JOIN, the model pickers) whose middle the rail's foot shares;
// the rail's buttons from x 54 (the logo's left edge too), their labels from 72
#define RL_EDGE   48.0f
#define RL_PAD    16.0f
#define RL_FOOT   64.0f
#define RL_CTRL   42.0f
#define RL_RAILX  54.0f
#define RL_LABELX 72.0f

enum { PAGE_PLAY, PAGE_CAMPAIGN, PAGE_HOST, PAGE_OPTIONS, PAGE_VOTE }; // (the vote's only over the Esc menu)
enum { CT_MISSIONS, CT_SAVES }; // Campaign's tabs
enum { HF_TIME, HF_FRAG, HF_PLAYERS, HF_BOTS, HF_NAME, HF_NUM }; // Host's fields
// the field typed in: the browser's search, the name, a crosshair code, a slider's value, a vote's number or text,
// Host's (F_HOST + its)
enum { F_NONE, F_SEARCH, F_NAME, F_CODE, F_VALUE, F_VOTE, F_HOST };
enum { OT_CONTROLS, OT_VIDEO, OT_AUDIO, OT_GAMEPLAY, OT_HUD, OT_NUM }; // Options' tabs (the mouse and the keys: CONTROLS)

// the browser's gametype tabs (Omaha's; round-based, Tug of War and Liberation under OTHER) and sortable columns
enum { TAB_ALL, TAB_FFA, TAB_TDM, TAB_OBJ, TAB_OTHER, TAB_NUM };
static const char *const rl_tabs[TAB_NUM] = {"ALL", "FFA", "TDM", "OBJ", "OTHER"};
enum { COL_NAME, COL_MAP, COL_PLAYERS, COL_MODE, COL_PING, COL_NUM };
static const char *const rl_colNames[COL_NUM] = {"SERVER NAME", "MAP", "PLAYERS", "GAMETYPE", "PING"};

// the items (the hit ids); from ID_ROW on, a list's rows
enum {
    ID_PLAY,
    ID_CAMPAIGN,
    ID_HOST,
    ID_OPTIONS,
    ID_QUIT,
    ID_DISCONNECT,
    ID_TAB     = 10,
    ID_SORT    = ID_TAB + TAB_NUM,
    ID_REFRESH = ID_SORT + COL_NUM,
    ID_SEARCH,
    ID_JOIN,
    ID_NAME,
    ID_APPLY,
    ID_SELECT, // + the side
    ID_CANCEL = ID_SELECT + 2,
    ID_CONFIRM,
    ID_MODE,               // + Host's game type
    ID_FIELD = ID_MODE + 6, // + Host's field
    ID_LOCAL = ID_FIELD + HF_NUM,
    ID_ONLINE,
    ID_START,
    ID_OTAB, // + Options' tab
    ID_DEFAULTS = ID_OTAB + OT_NUM,
    ID_OAPPLY,
    ID_CTAB,                // + Campaign's tab
    ID_CARD = ID_CTAB + 2,  // + the card: Continue, Training, Missions 1-6
    ID_DIFF = ID_CARD + 8,  // + the difficulty
    ID_CSTART = ID_DIFF + 3,
    ID_CDELETE,
    ID_CSAVE,
    ID_CLOAD,
    ID_KEYMOVE, // (what the confirmation asks: a key another command has, moved)
    ID_BACK,    // Options over the game: back to the Esc menu
    ID_VTAB,    // the vote page's (its head's tab, YES, NO, CALL VOTE, the field)
    ID_VYES,
    ID_VNO,
    ID_VCALL,
    ID_VFIELD,
    ID_HDRSLIDE, // the HDR calibration's slider (its button: ID_CONFIRM)
    ID_ROW  = 1000, // + the row shown
    ID_FAV  = ID_ROW + 1000,
    ID_PICK = ID_FAV + 1000, // + the model
    ID_MAP  = ID_PICK + 1000, // + Host's map shown
    ID_SAVE = ID_MAP + 1000,  // + the saved game shown
    ID_OPT  = ID_SAVE + 1000, // + Options' setting * RL_OPT_IDS + its control's part
    ID_KEY  = ID_OPT + 8000,  // + the action * 2 + its box (KEYS)
    ID_VOPT = ID_KEY + 1000,  // + the vote shown
    ID_VCHOICE = ID_VOPT + 1000, // + its choice (a list's place, a player's client)
};

/*
====================================================================

SERVERS

The game's master server lists (the stock browser's were the same:
Allied Assault's, or Breakthrough's and Spearhead's unless
dm_omit_spearhead), each server queried for its status; a row each that
answered. Shown through the tab and the search, sorted by a column, the
favourites (ui_favoriteservers: their addresses) first, other versions last

====================================================================
*/

typedef struct {
    unsigned int ip; // the query's address and port: the server's key
    int          queryPort;
    std::string  addr; // ip:port, connected to
    std::string  name, map, mode;
    std::string  file; // the map's ("dm/mohdm6"; map: its name)
    int          players, maxPlayers, ping;
    int          tab;
    bool         otherVersion; // can't be joined
} rlServer_t;

static struct {
    GServerList             list[2];
    std::vector<rlServer_t> rows;
    int                     serial; // changes with the rows and the lists' states
    bool                    failed; // a master couldn't be reached
} rl_sv;

static struct {
    int         page;
    int         edit; // F_*
    std::string search, name;
    int         tab, sort;
    bool        asc;
    std::string sel;  // the server picked: its address
    int         top;  // the first row shown
    int         fit;  // the rows the list shows (from the layout)
    int         pick; // the side whose model list is open, -1 none
    int         pickTop;
    std::string note; // a word on the status row in place of the lists' state
    int         confirm; // what the confirmation asks: ID_QUIT, ID_DISCONNECT, ID_CDELETE or ID_KEYMOVE
    int         clickAt;
    int         serial; // changes with all of the above
} rl_mm = {PAGE_PLAY, F_NONE, "", "", TAB_ALL, COL_PING, true, "", 0, 1, -1, 0, "", ID_QUIT, 0, 0};

static std::set<std::string> rl_favs;
static cvar_t               *ui_favoriteservers;

// the rows shown, in their order (rows' indices); made again when dirty
static struct {
    std::vector<int> rows;
    int              players;
    bool             dirty;
} rl_vis;

static void RL_Changed(void)
{
    rl_mm.serial++;
}

static const char *RL_SvString(GServer s, const char *key)
{
    return ServerGetStringValue(s, (char *)key, (char *)"");
}

static int RL_SvInt(GServer s, const char *key, int def)
{
    return ServerGetIntValue(s, (char *)key, def);
}

// a map's file and display name: its loading screen's (the dm and obj maps' are named with their folder)
static const std::pair<std::string, std::string>& RL_SvMap(const char *map)
{
    static std::map<std::string, std::pair<std::string, std::string>> maps;
    auto                                                              it = maps.find(map);

    if (it == maps.end()) {
        std::string file = map;

        if (!strchr(map, '/')) {
            for (const char *dir : {"dm/", "obj/"}) {
                if (menuManager.FindMenu(va("%s%s", dir, map))) {
                    file = std::string(dir) + map;
                    break;
                }
            }
        }
        it = maps.emplace(map, std::make_pair(file, std::string(RL_MapName(file.c_str())))).first;
    }
    return it->second;
}

// the tab a server shows under: its g_gametype (OpenMoHAA's gametype_i), else its mode's name
static int RL_SvTab(GServer s)
{
    switch (RL_SvInt(s, "gametype_i", -1)) {
    case GT_FFA:
        return TAB_FFA;
    case GT_TEAM:
        return TAB_TDM;
    case GT_OBJECTIVE:
        return TAB_OBJ;
    case -1:
        break;
    default:
        return TAB_OTHER;
    }
    const char *mode = RL_SvString(s, "gametype");

    return Q_stristr(mode, "free-for") || !Q_stricmp(mode, "ffa") ? TAB_FFA
         : Q_stristr(mode, "round")                               ? TAB_OTHER
         : Q_stristr(mode, "obj")                                 ? TAB_OBJ
         : Q_stristr(mode, "team")                                ? TAB_TDM
                                                                  : TAB_OTHER;
}

// a list's news (ServerListThink's): a server answered (its row made or
// updated) or failed, the list's state changed. instance: the game it lists
static void RL_SvCallback(GServerList list, int msg, void *instance, void *param1, void *param2)
{
    const int game = (int)(intptr_t)instance;
    GServer   s    = (GServer)param1;

    rl_sv.serial++;
    if (msg != LIST_PROGRESS || param2 == (void *)-1) {
        return;
    }
    const unsigned int ip = ServerGetInetAddress(s);
    const int          qp = ServerGetQueryPort(s);
    const char        *ver = RL_SvString(s, "gamever");
    float              v;
    rlServer_t        *row = NULL;

    for (rlServer_t& r : rl_sv.rows) {
        if (r.ip == ip && r.queryPort == qp) {
            row = &r;
            break;
        }
    }
    if (!row) {
        rl_sv.rows.push_back(rlServer_t());
        row            = &rl_sv.rows.back();
        row->ip        = ip;
        row->queryPort = qp;
    }
    row->addr       = va("%s:%i", ServerGetAddress(s), RL_SvInt(s, "hostport", PORT_SERVER));
    row->name       = RL_SvString(s, "hostname");
    // (without the spaces round it some names have)
    row->name.erase(0, row->name.find_first_not_of(' '));
    row->name.erase(row->name.find_last_not_of(' ') + 1);
    std::tie(row->file, row->map) = RL_SvMap(RL_SvString(s, "mapname"));
    row->mode       = RL_SvString(s, "gametype");
    row->players    = RL_SvInt(s, "numplayers", 0);
    row->maxPlayers = RL_SvInt(s, "maxplayers", 0);
    row->ping       = ServerGetPing(s);
    row->tab        = RL_SvTab(s);
    // (the stock browser's test: Breakthrough's and Spearhead's from 2.3 and
    // 2.1, Allied Assault's its own version; a demo's "d" skipped)
    v = (float)atof(ver[0] == 'd' ? ver + 1 : ver);
    row->otherVersion = game >= TG_MOHTA ? v < (game == TG_MOHTT ? 2.3f : 2.1f) : fabsf(v - com_target_shortversion->value) > 0.1f;
    rl_vis.dirty      = true;
}

static bool RL_SvBusy(void)
{
    for (GServerList l : rl_sv.list) {
        if (l && ServerListState(l) != sl_idle) {
            return true;
        }
    }
    return false;
}

// the lists asked of the masters again (not while they are)
static void RL_SvRefresh(void)
{
    const bool tt = com_target_game->integer >= TG_MOHTT;
    const int  games[2] = {tt ? TG_MOHTT : com_target_game->integer, tt && !Cvar_VariableIntegerValue("dm_omit_spearhead") ? TG_MOHTA : -1};
    char       filter[1] = "";

    if (RL_SvBusy()) {
        return;
    }
    rl_sv.rows.clear();
    rl_sv.failed = false;
    rl_vis.dirty = true;
    rl_mm.note.clear();
    for (int i = 0; i < 2; i++) {
        if (rl_sv.list[i]) {
            ServerListClear(rl_sv.list[i]);
            ServerListFree(rl_sv.list[i]);
            rl_sv.list[i] = NULL;
        }
        if (games[i] < 0) {
            continue;
        }
        const char *name = tt ? GS_GetGameName(games[i]) : GS_GetCurrentGameName();
        const char *key  = tt ? GS_GetGameKey(games[i]) : GS_GetCurrentGameKey();

        // (32 queries at once, shared by the lists)
        rl_sv.list[i] = ServerListNew(name, name, key, games[1] < 0 ? 32 : 16, (void *)&RL_SvCallback, GCALLBACK_FUNCTION,
                                      (void *)(intptr_t)games[i]);
        if (ServerListUpdate2(rl_sv.list[i], true, filter, qt_status) != GE_NOERROR) {
            rl_sv.failed = true;
        }
    }
    rl_sv.serial++;
}

static void RL_SvHalt(void)
{
    for (GServerList l : rl_sv.list) {
        if (l) {
            ServerListHalt(l);
        }
    }
}

// the rows shown: the tab's, the search's, sorted
static void RL_SvShown(void)
{
    if (!rl_vis.dirty) {
        return;
    }
    rl_vis.dirty = false;
    rl_vis.rows.clear();
    rl_vis.players = 0;
    for (int i = 0; i < (int)rl_sv.rows.size(); i++) {
        const rlServer_t& r = rl_sv.rows[i];

        if ((rl_mm.tab != TAB_ALL && r.tab != rl_mm.tab) || !Q_stristr(r.name.c_str(), rl_mm.search.c_str())) {
            continue;
        }
        rl_vis.rows.push_back(i);
        rl_vis.players += r.players;
    }
    std::sort(rl_vis.rows.begin(), rl_vis.rows.end(), [](int a, int b) {
        const rlServer_t &x = rl_sv.rows[a], &y = rl_sv.rows[b];
        const bool        fx = rl_favs.count(x.addr) > 0, fy = rl_favs.count(y.addr) > 0;
        int               c  = 0;

        if (fx != fy) {
            return fx;
        }
        if (x.otherVersion != y.otherVersion) {
            return y.otherVersion;
        }
        switch (rl_mm.sort) {
        case COL_NAME:
            c = Q_stricmp(x.name.c_str(), y.name.c_str());
            break;
        case COL_MAP:
            c = Q_stricmp(x.map.c_str(), y.map.c_str());
            break;
        case COL_PLAYERS:
            c = x.players - y.players;
            break;
        case COL_MODE:
            c = Q_stricmp(x.mode.c_str(), y.mode.c_str());
            break;
        default:
            c = x.ping - y.ping;
            break;
        }
        if (!c) {
            c = Q_stricmp(x.name.c_str(), y.name.c_str());
        }
        if (!c) {
            c = x.addr.compare(y.addr);
        }
        return rl_mm.asc ? c < 0 : c > 0;
    });
    rl_mm.top = Q_max(0, Q_min(rl_mm.top, (int)rl_vis.rows.size() - rl_mm.fit));
}

// the place shown of the server picked, -1 not shown
static int RL_SvPicked(void)
{
    for (int i = 0; i < (int)rl_vis.rows.size(); i++) {
        if (rl_sv.rows[rl_vis.rows[i]].addr == rl_mm.sel) {
            return i;
        }
    }
    return -1;
}

static void RL_SvFavorite(const std::string& addr)
{
    std::string list;

    if (!rl_favs.erase(addr)) {
        rl_favs.insert(addr);
    }
    for (const std::string& a : rl_favs) {
        list += (list.empty() ? "" : " ") + a;
    }
    Cvar_Set("ui_favoriteservers", list.c_str());
    rl_vis.dirty = true;
}

/*
====================================================================

CONNECTING

The connect's box (shown for the stock "connecting" and the menus its end
opens: cl_uimenus.cpp's aliases): the server's name (its address while
not known), its map's photo, its mode, players and ping, from the
browser's row on its JOIN, else asked of the server once as the connect
begins (getinfo); the steps and CANCEL. A refusal or a drop in the same
box: what happened in plain words, RETRY and BACK, the password typed
there when the server wants one. Whatever ends a connect, the main menu
shows Multiplayer next. Over the main menu's backdrop; black for the
game's own server (a mission's, Host's), joined in a moment

====================================================================
*/

// how it ended: refused (the server's word), dropped, no answer, a bad address
enum { CE_NONE, CE_FULL, CE_PASSWORD, CE_BANNED, CE_KICKED, CE_VERSION, CE_TIMEOUT, CE_ADDRESS, CE_DROPPED, CE_OTHER };

static struct {
    std::string addr;                      // as connected to
    std::string name, map, mode;           // the server's name, its map's file ("dm/mohdm6"), its mode: "" not known
    int         players, maxPlayers, ping; // -1 not known
    bool        given;                     // the browser's row gave them: not asked
    bool        joined;                    // a map of it loaded: its end a drop
    netadr_t    asked;                     // asked for its info there
    int         askedAt;                   // Sys_Milliseconds then, 0 no answer awaited
    char        challenge[16];             // the question's, echoed
    int         end;                       // CE_*
    std::string said;                      // the server's word (a refusal, a drop's reason)
    std::string typed;                     // the password typed
} rl_cn = {"", "", "", "", -1, -1, -1};

// another server: nothing known of it
static void RL_ConnectTo(const char *addr)
{
    rl_cn.addr = addr;
    rl_cn.name.clear();
    rl_cn.map.clear();
    rl_cn.mode.clear();
    rl_cn.players = rl_cn.maxPlayers = rl_cn.ping = -1;
    rl_cn.given = rl_cn.joined = false;
    rl_cn.askedAt = 0;
}

// what the server says it is: its name, its map (its file's; a spawn point after a $), its mode ("" not said)
static void RL_ConnectNamed(std::string name, std::string map, const std::string& mode)
{
    name.erase(0, name.find_first_not_of(' '));
    name.erase(name.find_last_not_of(' ') + 1);
    map.erase(std::min(map.find('$'), map.size()));
    if (!name.empty()) {
        rl_cn.name = name;
    }
    if (!map.empty()) {
        rl_cn.map = RL_SvMap(map.c_str()).first;
    }
    if (!mode.empty()) {
        rl_cn.mode = mode;
    }
}

void UI_Menus_Connect(const char *server, const netadr_t *adr)
{
    if (rl_cn.addr != server) {
        RL_ConnectTo(server);
    }
    rl_cn.end    = CE_NONE;
    rl_cn.joined = false;
    rl_cn.typed.clear();
    rl_cn.askedAt = 0;
    // (whatever ends it, the main menu shows Multiplayer next)
    rl_mm.page = PAGE_PLAY;
    // its name, map, players and ping asked for once (unless the browser's row gave them; the game's own server not)
    if (!rl_cn.given && !NET_IsLocalAddress(*adr)) {
        Com_sprintf(rl_cn.challenge, sizeof(rl_cn.challenge), "%d", rand());
        rl_cn.asked   = *adr;
        rl_cn.askedAt = Q_max(1, Sys_Milliseconds());
        CL_NET_OutOfBandPrint(*adr, "getinfo %s", rl_cn.challenge);
    }
    rl_cn.given = false;
    RL_Changed();
}

qboolean UI_Menus_ServerInfo(const netadr_t *from, const char *info)
{
    if (!rl_cn.askedAt || !NET_CompareAdr(*from, rl_cn.asked) || strcmp(Info_ValueForKey(info, "challenge"), rl_cn.challenge)) {
        return qfalse;
    }
    // (copied one at a time: Info_ValueForKey's buffers are two)
    const std::string name = Info_ValueForKey(info, "hostname"), map = Info_ValueForKey(info, "mapname");

    RL_ConnectNamed(name, map, Info_ValueForKey(info, "gametypestring"));
    rl_cn.players    = atoi(Info_ValueForKey(info, "clients"));
    rl_cn.maxPlayers = atoi(Info_ValueForKey(info, "sv_maxclients"));
    rl_cn.ping       = Sys_Milliseconds() - rl_cn.askedAt;
    rl_cn.askedAt    = 0;
    RL_Changed();
    return qtrue;
}

void RL_ConnectLoaded(const char *info, const char *map)
{
    const std::string name = Info_ValueForKey(info, "sv_hostname"), mode = Info_ValueForKey(info, "g_gametypestring");

    // (a local game's server: localhost)
    if (rl_cn.addr != clc.servername) {
        RL_ConnectTo(clc.servername);
    }
    rl_cn.joined = true;
    RL_ConnectNamed(name, map, mode);
}

void RL_ConnectDropped(const char *reason)
{
    rl_cn.said = reason;
}

// the server's word: the refusal or the drop it names, else other
static int RL_ConnectKind(const char *said, int other)
{
    static const struct {
        const char *start;
        int         end;
    } kinds[] = {
        {"Server is full",       CE_FULL    },
        {"Invalid password",     CE_PASSWORD},
        {"You are banned",       CE_BANNED  },
        {"You have been banned", CE_BANNED  },
        {"Banned",               CE_BANNED  },
        {"Kicked",               CE_KICKED  },
        {"was kicked",           CE_KICKED  },
        {"Server uses protocol", CE_VERSION },
        {"Server is version",    CE_VERSION },
    };

    for (const auto& k : kinds) {
        if (!Q_stricmpn(said, k.start, strlen(k.start))) {
            return k.end;
        }
    }
    return other;
}

void RL_ConnectError(const char *stock)
{
    if (!Q_stricmp(stock, "badserveraddy")) {
        // (no connect began: the address the game was given)
        if (rl_cn.addr != clc.servername) {
            RL_ConnectTo(clc.servername);
        }
        rl_cn.end = CE_ADDRESS;
    } else if (!Q_stricmp(stock, "servertimeout")) {
        rl_cn.end = CE_TIMEOUT;
    } else if (!Q_stricmp(stock, "serverdisconnected")) {
        rl_cn.said.clear();
        rl_cn.end = CE_DROPPED;
    } else if (!Q_stricmp(stock, "disconnected")) {
        // (the server's command's reason: RL_ConnectDropped)
        rl_cn.end = RL_ConnectKind(rl_cn.said.c_str(), CE_DROPPED);
    } else {
        // errormessage, wrongversion: the server's word
        rl_cn.said = Cvar_VariableString("com_errorMessage");
        rl_cn.end  = RL_ConnectKind(rl_cn.said.c_str(), CE_OTHER);
    }
    rl_cn.typed.clear();
    rl_mm.page = PAGE_PLAY;
    RL_Changed();
}

static void RL_SvJoin(void)
{
    const int at = RL_SvPicked();

    if (at < 0) {
        rl_mm.note = "PICK A SERVER";
        return;
    }
    const rlServer_t& r = rl_sv.rows[rl_vis.rows[at]];

    if (r.otherVersion) {
        rl_mm.note = "ANOTHER VERSION'S SERVER";
        return;
    }
    RL_SvHalt();
    // (the connect's box shows the row: nothing asked of the server)
    RL_ConnectTo(r.addr.c_str());
    rl_cn.name       = r.name;
    rl_cn.map        = r.file;
    rl_cn.mode       = r.mode;
    rl_cn.players    = r.players;
    rl_cn.maxPlayers = r.maxPlayers;
    rl_cn.ping       = r.ping;
    rl_cn.given      = true;
    Cbuf_AddText(va("connect %s\n", r.addr.c_str()));
}

/*
====================================================================

HOST

Omaha's host panel: a map for the game type, as the stock host menu offered
them (Allied Assault's dm maps for the deathmatch types and its obj maps
for objective; Spearhead's and Breakthrough's dm, obj and lib maps but
obj_team2 and obj_team4 for the deathmatch types, the obj and lib maps
named with an obj or ship part for objective, a tow part for Tug of War, a
lib part for Liberation), the limits, the players and bots, the server's
name, who can join. START sets
the stock host menu's cvars (as it left them, read the first time) and
runs its ui_startdmmap, the map the rotation's only one; the bots as
sv_maxbots and sv_numbots

====================================================================
*/

// (Allied Assault's first four, Spearhead's five, Breakthrough's six: RL_HostModes)
static const char *const rl_modeLabels[] = {"FFA", "TDM", "RBM", "OBJ", "TOW", "LIB"};
// (g_gametype, g_gametypestring as the stock host menu names them)
static const int         rl_modeTypes[]  = {GT_FFA, GT_TEAM, GT_TEAM_ROUNDS, GT_OBJECTIVE, GT_TOW, GT_LIBERATION};
static const char *const rl_modeNames[]  = {"Free-For-All", "Team-Match", "Round-Based-Match", "Objective-Based-Match", "Tug-Of-War",
                                            "Liberation"};
static const char *const rl_joinLabels[] = {"LOCAL", "ONLINE"};

static int RL_HostModes(void)
{
    return com_target_game->integer >= TG_MOHTT ? 6 : com_target_game->integer >= TG_MOHTA ? 5 : 4;
}

static struct {
    bool                     ready;  // read from the cvars
    int                      mode;   // rl_mode*'s
    std::vector<std::string> maps;   // the mode's ("dm/mohdm6"), by the names shown
    std::string              map;    // picked
    int                      top;    // the first shown
    std::string              field[HF_NUM];
    bool                     online;
} rl_host;

// a map's file's name has the part (between underscores)
static bool RL_NamePart(const char *name, const char *part)
{
    const size_t n = strlen(part);

    for (const char *p = name; p; p = strchr(p, '_')) {
        p += *p == '_';
        if (!Q_stricmpn(p, part, n) && (!p[n] || p[n] == '_')) {
            return true;
        }
    }
    return false;
}

// a map fits the game type (dir: its folder in maps/, name: its file's)
static bool RL_HostFits(const char *dir, const char *name, int gametype)
{
    if (com_target_game->integer < TG_MOHTA) {
        return !strcmp(dir, gametype == GT_OBJECTIVE ? "obj" : "dm");
    }
    if (gametype < GT_OBJECTIVE) {
        return !strcmp(dir, "dm") || (Q_stricmp(name, "obj_team2") && Q_stricmp(name, "obj_team4"));
    }
    return strcmp(dir, "dm")
        && (gametype == GT_TOW          ? RL_NamePart(name, "tow")
            : gametype == GT_LIBERATION ? RL_NamePart(name, "lib")
                                        : RL_NamePart(name, "obj") || RL_NamePart(name, "ship"));
}

// the mode's maps listed (those with a screen of their own, the stock ones,
// first, by the names shown); the one picked kept if it fits, else the first
static void RL_HostList(void)
{
    const int gametype = rl_modeTypes[rl_host.mode];

    rl_host.maps.clear();
    for (const char *dir : {"dm", "obj", "lib"}) {
        int    n;
        char **files = FS_ListFiles(va("maps/%s", dir), ".bsp", qfalse, &n);

        for (int i = 0; i < n; i++) {
            std::string name = files[i];

            name.erase(name.size() - 4);
            if (RL_HostFits(dir, name.c_str(), gametype)) {
                rl_host.maps.push_back(std::string(dir) + "/" + name);
            }
        }
        FS_FreeFileList(files);
    }
    std::sort(rl_host.maps.begin(), rl_host.maps.end(), [](const std::string& a, const std::string& b) {
        const std::string na = RL_MapName(a.c_str()), nb = RL_MapName(b.c_str());
        // (a map without a screen shows its file's name)
        const bool        sa = na != a.substr(a.find('/') + 1), sb = nb != b.substr(b.find('/') + 1);

        return sa != sb ? sa : Q_stricmp(na.c_str(), nb.c_str()) < 0;
    });
    if (std::find(rl_host.maps.begin(), rl_host.maps.end(), rl_host.map) == rl_host.maps.end()) {
        rl_host.map = rl_host.maps.empty() ? "" : rl_host.maps[0];
    }
    rl_host.top = 0;
}

// a character a server's name may have: printable, not one that ends its
// quoted string or the command, or expands in it
static bool RL_NameChar(int ch)
{
    return ch >= ' ' && ch <= '~' && !strchr("\";\\$%", ch);
}

// the first time Host shows: the settings the stock host menu left
static void RL_HostRead(void)
{
    std::string& name = rl_host.field[HF_NAME];

    const int gametype = atoi(CvarGetForUI("g_gametype", "2"));

    rl_host.ready  = true;
    rl_host.mode   = 1;
    for (int i = 0; i < RL_HostModes(); i++) {
        if (rl_modeTypes[i] == gametype) {
            rl_host.mode = i;
        }
    }
    rl_host.map                 = CvarGetForUI("ui_dmmap", "");
    rl_host.field[HF_TIME]      = CvarGetForUI("ui_timelimit", "20");
    rl_host.field[HF_FRAG]      = CvarGetForUI("ui_fraglimit", "0");
    rl_host.field[HF_PLAYERS]   = CvarGetForUI("ui_maxclients", "16");
    rl_host.field[HF_BOTS]      = CvarGetForUI("sv_numbots", "0");
    name                        = CvarGetForUI("ui_hostname", "Reloaded Server");
    name.erase(std::remove_if(name.begin(), name.end(), [](char c) { return !RL_NameChar((unsigned char)c); }), name.end());
    name.resize(Q_min(name.size(), (size_t)31));
    rl_host.online              = atoi(CvarGetForUI("ui_gamespy", "1")) != 0;
    RL_HostList();
}

static void RL_HostStart(void)
{
    const int players = Q_max(1, Q_min(MAX_CLIENTS, atoi(rl_host.field[HF_PLAYERS].c_str())));
    const int bots    = Q_max(0, Q_min(MAX_CLIENTS, atoi(rl_host.field[HF_BOTS].c_str())));

    if (rl_host.map.empty()) {
        return;
    }
    // (the name typed without what ends a quoted string or a command)
    Cbuf_AddText(va("set ui_dmmap \"%s\";set ui_dedicated 0;set ui_maxclients %d;set ui_gamespy %d;set ui_timelimit %d;"
                    "set ui_fraglimit %d;set ui_hostname \"%s\";set sv_maxbots %d;set sv_numbots %d;ui_startdmmap %d %s ui_dmmap\n",
                    rl_host.map.c_str(), players, rl_host.online, Q_max(0, atoi(rl_host.field[HF_TIME].c_str())),
                    Q_max(0, atoi(rl_host.field[HF_FRAG].c_str())), rl_host.field[HF_NAME].c_str(), bots, bots,
                    rl_modeTypes[rl_host.mode], rl_modeNames[rl_host.mode]));
}

/*
----------------
The photos: the maps' (their stock load screens'), the missions' and the
saved games' (CAMPAIGN), registered whole, one a frame while their page
shows; made again after a renderer's restart, looked up again after a
map's load
----------------
*/
static struct {
    std::map<std::string, qhandle_t> shot; // by map, or CAMPAIGN's key (RL_MissionKey, RL_SaveKey); 0 none
    int                              stamp, refSeq;
} rl_shots;

static std::string RL_ShotName(const std::string& map)
{
    std::string name = "*rl_shot_" + map;

    std::replace(name.begin(), name.end(), '/', '_');
    return name;
}

// a photo, 0 none, -1 not made yet
static qhandle_t RL_Shot(const std::string& map)
{
    if (rl_shots.refSeq != uii.GetRefSequence()) {
        rl_shots.shot.clear();
        rl_shots.refSeq = uii.GetRefSequence();
        rl_shots.stamp  = UI_BakeStamp();
    } else if (rl_shots.stamp != UI_BakeStamp()) {
        for (auto& s : rl_shots.shot) {
            s.second = s.second ? re.RegisterShaderNoMip(RL_ShotName(s.first).c_str()) : 0;
        }
        rl_shots.stamp = UI_BakeStamp();
    }
    auto it = rl_shots.shot.find(map);

    return it == rl_shots.shot.end() ? -1 : it->second;
}

// a photo kept (pic RGBA, freed; NULL none)
static void RL_ShotKeep(const std::string& key, byte *pic, int w, int h)
{
    rl_shots.shot[key] = pic ? re.RegisterTexture2D(RL_ShotName(key).c_str(), pic, w, h) : 0;
    if (pic) {
        uii.Rend_FreeRawImage(pic);
    }
}

static void RL_ShotMake(const std::string& map)
{
    int   w, h;
    byte *pic = RL_MenuPhoto(menuManager.FindMenu(map.c_str()), &w, &h);

    RL_ShotKeep(map, pic, w, h);
}

/*
====================================================================

CAMPAIGN

The missions of the game running, as its stock briefing room offers them
(Allied Assault: Training, then Missions 1-6; Spearhead and Breakthrough:
Missions 1-3), each open once the one before is finished (g_mNl1, set by
Allied Assault's levels' scripts; g_t2l1 and g_t3l1, by the cgame at a
mission's end); START runs the stock difficulty menus' commands (the
difficulty, then the mission's briefing film, which goes on to its first
level). The saved games of the game running (save/<config>/*.ssv),
newest first; the newest is Continue

====================================================================
*/

typedef struct {
    const char *tag, *name, *place, *year;
    const char *photo;   // its picture, in textures/mohmenu/
    float       crop[4]; // the part of it shown (x0 y0 x1 y1, parts of its size)
    float       aspect;  // that part's shape
} rlMission_t;

// Allied Assault's: the briefing room's signs' names, its dossiers' places and years, the photos its slides show
static const rlMission_t rl_aaMissions[] = {
    {"TRAINING",  "Training Course",      "Basic training",          "",     "briefing/bootcamp",    {0, 0, 1, 1},             4.0f / 3},
    {"MISSION 1", "Lighting the Torch",   "Algeria, North Africa",   "1942", "briefing/portphoto",   {0, 0, 1, 1},             4.0f / 3},
    {"MISSION 2", "Scuttling the U-529",  "Trondheim, Norway",       "1943", "briefing/subdiving",   {0, 0, 1, 1},             4.0f / 3},
    {"MISSION 3", "Operation Overlord",   "Normandy, France",        "1944", "briefing/higginsboat", {0, 0, 1, 1},             4.0f / 3},
    {"MISSION 4", "Behind Enemy Lines",   "Normandy, France",        "1944", "briefing/manorhouse",  {0, 0, 1, 1},             4.0f / 3},
    {"MISSION 5", "The Day of the Tiger", "Brittany, France",        "1944", "briefing/shoreguns",   {0, 0, 1, 1},             4.0f / 3},
    // (its white border cut)
    {"MISSION 6", "Return to Schmerzen",  "Siegfried Line, Germany", "1945", "briefing/train",       {0.06f, 0.06f, 0.94f, 0.94f}, 4.0f / 3},
};
// Spearhead's and Breakthrough's: the briefing room's signs' names, the
// places and years of their first levels' title cards (but Berlin's year,
// which they don't give), the paintings of their load screens (the left
// 512 of 640, in a black band; on torn paper)
static const rlMission_t rl_taMissions[] = {
    {"MISSION 1", "Operation Overlord",  "Normandy, France",          "1944", "loadscreens/t1l1_1", {0, 75 / 512.0f, 1, 404 / 512.0f}, 512 / 329.0f},
    {"MISSION 2", "The Ardennes Forest", "Ardennes Forest, Belgium",  "1944", "loadscreens/t2l1_1", {0, 75 / 512.0f, 1, 404 / 512.0f}, 512 / 329.0f},
    {"MISSION 3", "Road to Berlin",      "Berlin, Germany",           "1945", "loadscreens/t3l1_1", {0, 75 / 512.0f, 1, 404 / 512.0f}, 512 / 329.0f},
};
static const rlMission_t rl_ttMissions[] = {
    {"MISSION 1", "Operation Torch", "Kasserine Pass, North Africa", "1943", "loadscreens/e1l1_1", {30 / 512.0f, 92 / 512.0f, 1, 388 / 512.0f}, 482 / 296.0f},
    {"MISSION 2", "Operation Husky", "Sicily, Italy",                "1943", "loadscreens/e2l1_1", {30 / 512.0f, 92 / 512.0f, 1, 388 / 512.0f}, 482 / 296.0f},
    {"MISSION 3", "Battle Mountain", "Monte Cassino, Italy",         "1944", "loadscreens/e3l1_1", {30 / 512.0f, 92 / 512.0f, 1, 388 / 512.0f}, 482 / 296.0f},
};
#define RL_MOSTMISSIONS 7

// the games' (com_target_game's)
static const struct {
    const char        *title;
    const rlMission_t *missions;
    int                n;
    int                first;    // the first's number (Allied Assault's Training 0)
    char               level;    // its levels' letter ("m1l1")
    const char        *briefing; // its briefing films' maps but their number
    const char        *open;     // the cvar a mission opens with (but the first's), and the first number with one
    int                openFrom;
} rl_games[] = {
    {"ALLIED ASSAULT", rl_aaMissions, ARRAY_LEN(rl_aaMissions), 0, 'm', "briefing/briefing",  "g_m%dl1", 1},
    {"SPEARHEAD",      rl_taMissions, ARRAY_LEN(rl_taMissions), 1, 't', "briefing/briefingt", "g_t%dl1", 2},
    {"BREAKTHROUGH",   rl_ttMissions, ARRAY_LEN(rl_ttMissions), 1, 'e', "briefing/briefinge", "g_t%dl1", 2},
};

static const char *const rl_campTabs[] = {"MISSIONS", "SAVED GAMES"};
static const char *const rl_diffs[]    = {"EASY", "MEDIUM", "HARD"};
static const char *const rl_diffSays[] = {"You can take many more hits.", "The game as it was made.", "A few hits and you are down."};

typedef struct {
    std::string name; // its files' (loadgame's)
    std::string comment, map;
    int         time;
    bool        autosave;
} rlSave_t;

static struct {
    int                   tab;
    int                   card;  // picked: 0 Continue, else 1 + the mission; -1 not yet
    int                   diff;  // -1 not read yet (skill)
    std::vector<rlSave_t> saves; // newest first
    std::string           save;  // the one picked: its name
    int                   top, fit; // the first shown, how many show
    bool                  scanned;
    int                   clickAt;
    cvar_t               *open[RL_MOSTMISSIONS]; // the cvars that open them
} rl_camp = {CT_MISSIONS, -1, -1, {}, "", 0, 1, false, 0, {}};

// the game running's
#define RL_GAME (rl_games[Q_min(com_target_game->integer, TG_MOHTT)])

// in a single player game
static bool RL_InMission(void)
{
    return clc.state > CA_PRIMED && !cg_gametype->integer;
}

// a mission's number (Allied Assault's Training 0)
static int RL_MissionNumber(int k)
{
    return RL_GAME.first + k;
}

// Allied Assault's Training: no difficulty, no briefing film
static bool RL_IsTraining(int k)
{
    return RL_MissionNumber(k) == 0;
}

// the mission a map is of (Allied Assault's Training: training; their
// briefing films: briefing/briefingN; their levels: mNl..., tNl..., eNl...),
// its place in the game's, -1 none
static int RL_MissionOf(const char *map)
{
    const size_t b = strlen(RL_GAME.briefing);
    int          n = -1;

    if (RL_GAME.first == 0 && !Q_stricmp(map, "training")) {
        n = 0;
    } else if (!Q_stricmpn(map, RL_GAME.briefing, b) && isdigit((unsigned char)map[b]) && !map[b + 1]) {
        n = map[b] - '0';
    } else if (tolower(map[0]) == RL_GAME.level && isdigit((unsigned char)map[1]) && tolower(map[2]) == 'l') {
        n = map[1] - '0';
    }
    n -= RL_GAME.first;
    return n >= 0 && n < RL_GAME.n ? n : -1;
}

// a mission open (the stock defaults: Allied Assault's Mission 1 open; the first open)
static bool RL_MissionOpen(int k)
{
    const int n = RL_MissionNumber(k);

    if (n < RL_GAME.openFrom) {
        return true;
    }
    if (!rl_camp.open[k]) {
        rl_camp.open[k] = Cvar_Get(va(RL_GAME.open, n), n == 1 ? "1" : "0", CVAR_ARCHIVE);
    }
    return rl_camp.open[k]->integer != 0;
}

// a picture of textures/mohmenu/ (photo), the part of it a mission's shows (m's crop), RGBA, opaque; NULL none;
// freed with uii.Rend_FreeRawImage
static byte *RL_MissionPhoto(const rlMission_t& m, const char *photo, int *w, int *h)
{
    byte *pic;

    if (!uii.Rend_LoadRawImage(va("textures/mohmenu/%s.tga", photo), &pic, w, h)) {
        return NULL;
    }
    {
        // (the rows moved up in place: each lands before where it was)
        const int x0 = (int)(*w * m.crop[0]), y0 = (int)(*h * m.crop[1]), cw = (int)(*w * m.crop[2]) - x0, ch = (int)(*h * m.crop[3]) - y0;

        for (int y = 0; y < ch; y++) {
            memmove(pic + y * cw * 4, pic + ((y + y0) * *w + x0) * 4, cw * 4);
        }
        *w = cw;
        *h = ch;
    }
    for (int i = 0; i < *w * *h; i++) {
        pic[i * 4 + 3] = 255;
    }
    return pic;
}

// the photos' keys: a mission's (grey while it is locked), a saved game's shot
static std::string RL_MissionKey(int k)
{
    return std::string(RL_MissionOpen(k) ? "brief/" : "grey/") + RL_GAME.missions[k].photo;
}

static std::string RL_SaveKey(const rlSave_t& s)
{
    return "save/" + s.name;
}

static void RL_MissionShot(int k)
{
    const bool grey = !RL_MissionOpen(k);
    int        w, h;
    byte      *pic = RL_MissionPhoto(RL_GAME.missions[k], RL_GAME.missions[k].photo, &w, &h);

    for (int i = 0; pic && grey && i < w * h; i++) {
        byte *p = pic + i * 4;

        p[0] = p[1] = p[2] = (byte)((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8);
    }
    RL_ShotKeep(RL_MissionKey(k), pic, w, h);
}

qhandle_t RL_LevelPainting(const char *map, float *aspect)
{
    const int         k   = RL_MissionOf(map);
    const std::string key = std::string("level/") + map;
    qhandle_t         shot;

    if (k < 0 || RL_GAME.first == 0) {
        return 0;
    }
    shot = RL_Shot(key);
    if (shot < 0) {
        // (its own painting, else its mission's)
        const rlMission_t& m = RL_GAME.missions[k];
        int                w, h;
        byte              *pic = RL_MissionPhoto(m, va("loadscreens/%s_1", map), &w, &h);

        if (!pic) {
            pic = RL_MissionPhoto(m, m.photo, &w, &h);
        }
        RL_ShotKeep(key, pic, w, h);
        shot = RL_Shot(key);
    }
    *aspect = RL_GAME.missions[k].aspect;
    return shot;
}

// a saved game's shot (256 x 256, the screen squeezed); none when too dark
// to tell (a level's autosave, taken as it fades in: its mission's photo shown)
static void RL_SaveShot(const rlSave_t& s)
{
    byte *pic = NULL;
    int   w = 0, h = 0, light = 0;

    if (uii.Rend_LoadRawImage(Com_GetArchiveFileName(s.name.c_str(), "tga"), &pic, &w, &h)) {
        for (int i = 0; i < w * h; i++) {
            light += (pic[i * 4] * 77 + pic[i * 4 + 1] * 150 + pic[i * 4 + 2] * 29) >> 8;
            pic[i * 4 + 3] = 255;
        }
        if (light < 10 * w * h) {
            uii.Rend_FreeRawImage(pic);
            pic = NULL;
        }
    }
    RL_ShotKeep(RL_SaveKey(s), pic, w, h);
}

// a saved game's photo: its shot (as wide as the screen), else its mission's
static qhandle_t RL_SavePhoto(const rlSave_t& s, float *aspect)
{
    const qhandle_t shot = RL_Shot(RL_SaveKey(s));
    const int       k    = RL_MissionOf(s.map.c_str());

    *aspect = (float)uid.vidWidth / (float)uid.vidHeight;
    if (shot || k < 0) {
        return shot;
    }
    *aspect = RL_GAME.missions[k].aspect;
    return RL_Shot(RL_MissionKey(k));
}

// a saved game's mission's name (else its map's)
static const char *RL_SaveMission(const rlSave_t& s)
{
    const int k = RL_MissionOf(s.map.c_str());

    return k < 0 ? s.map.c_str() : RL_GAME.missions[k].name;
}

// the saved games read again; a shot made again whose game was saved again
static void RL_CampScan(void)
{
    const char           *folder = Com_GetArchiveFolder();
    std::vector<rlSave_t> old;
    int                   n;
    char                **files = FS_ListFiles(folder, "ssv", qfalse, &n);
    const std::string     starting = std::string(" - ") + Sys_LV_CL_ConvertString("Starting");

    old.swap(rl_camp.saves);
    for (int i = 0; i < n; i++) {
        fileHandle_t     f;
        savegamestruct_t save;
        rlSave_t         s;
        char             name[MAX_QPATH];
        int              got;

        FS_FOpenFileRead(va("%s/%s", folder, files[i]), &f, qfalse, qtrue);
        if (!f) {
            continue;
        }
        got = FS_Read(&save, sizeof(save), f);
        FS_FCloseFile(f);
        Com_SwapSaveStruct(&save);
        if (got != sizeof(save) || save.version != SAVEGAME_STRUCT_VERSION || save.type != com_target_game->integer) {
            continue;
        }
        COM_StripExtension(files[i], name, sizeof(name));
        save.comment[sizeof(save.comment) - 1] = save.mapName[sizeof(save.mapName) - 1] = 0;
        s.name     = name;
        s.comment  = save.comment;
        s.map      = save.mapName;
        s.time     = save.time;
        // (an autosave's comment is the level's name and "Starting"; a briefing film's level has the map's)
        s.autosave = s.comment.size() > starting.size() && !s.comment.compare(s.comment.size() - starting.size(), starting.size(), starting);
        if (s.autosave && !Q_stricmpn(save.mapName, RL_GAME.briefing, strlen(RL_GAME.briefing)) && RL_MissionOf(save.mapName) >= 0) {
            s.comment = std::string(RL_GAME.missions[RL_MissionOf(save.mapName)].name) + " - Briefing";
        }
        rl_camp.saves.push_back(s);
    }
    FS_FreeFileList(files);
    std::sort(rl_camp.saves.begin(), rl_camp.saves.end(), [](const rlSave_t& a, const rlSave_t& b) {
        return a.time != b.time ? a.time > b.time : a.name < b.name;
    });
    for (const rlSave_t& s : rl_camp.saves) {
        auto was = std::find_if(old.begin(), old.end(), [&s](const rlSave_t& o) { return o.name == s.name; });

        if (was == old.end() || was->time != s.time) {
            rl_shots.shot.erase(RL_SaveKey(s));
        }
    }
    // (the one picked kept, else the newest; the card Continue when there is one, else Training)
    if (std::none_of(rl_camp.saves.begin(), rl_camp.saves.end(), [](const rlSave_t& s) { return s.name == rl_camp.save; })) {
        rl_camp.save = rl_camp.saves.empty() ? "" : rl_camp.saves[0].name;
    }
    if (rl_camp.card < 0 || (!rl_camp.card && rl_camp.saves.empty())) {
        rl_camp.card = rl_camp.saves.empty() ? 1 : 0;
    }
    rl_camp.scanned = true;
}

// the saved game picked, NULL none; at its place
static const rlSave_t *RL_CampPicked(int *at = NULL)
{
    for (size_t i = 0; i < rl_camp.saves.size(); i++) {
        if (rl_camp.saves[i].name == rl_camp.save) {
            if (at) {
                *at = (int)i;
            }
            return &rl_camp.saves[i];
        }
    }
    return NULL;
}

// the mission whose photo is behind the page: the card picked's (Continue: the newest saved game's), the saved game picked's
static int RL_CampBackdrop(void)
{
    const rlSave_t *s = rl_camp.tab == CT_SAVES ? RL_CampPicked() : rl_camp.saves.empty() ? NULL : &rl_camp.saves[0];

    if (rl_camp.tab == CT_MISSIONS && rl_camp.card > 0) {
        return rl_camp.card - 1;
    }
    return s ? RL_MissionOf(s->map.c_str()) : -1;
}

// when a game was saved: "Today 18:57", "Yesterday 18:57", "14 Nov 18:57"; full: "Sunday 15 November, 18:57"
static std::string RL_When(int time, bool full)
{
    static const char *const days[]   = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    static const char *const months[] = {"January", "February", "March",     "April",   "May",      "June",
                                         "July",    "August",   "September", "October", "November", "December"};
    const time_t             t = time, now = ::time(NULL);
    struct tm                at = *localtime(&t), day = at, today = *localtime(&now);
    int                      ago;

    if (full) {
        return va("%s %d %s, %02d:%02d", days[at.tm_wday], at.tm_mday, months[at.tm_mon], at.tm_hour, at.tm_min);
    }
    // (the days between their midnights)
    day.tm_hour = day.tm_min = day.tm_sec = today.tm_hour = today.tm_min = today.tm_sec = 0;
    day.tm_isdst = today.tm_isdst = -1;
    ago          = (int)floor(difftime(mktime(&today), mktime(&day)) / 86400 + 0.5);
    if (ago == 0 || ago == 1) {
        return va("%s %02d:%02d", ago ? "Yesterday" : "Today", at.tm_hour, at.tm_min);
    }
    return va("%d %.3s %02d:%02d", at.tm_mday, months[at.tm_mon], at.tm_hour, at.tm_min);
}

// (out of a multiplayer game first: no loadgame there)
static void RL_CampLoad(const std::string& name)
{
    if (clc.state != CA_DISCONNECTED && cg_gametype->integer) {
        Cbuf_AddText("disconnect\n");
    }
    Cbuf_AddText(va("loadgame \"%s\"\n", name.c_str()));
}

// START: the card picked
static void RL_CampStart(void)
{
    const int k = rl_camp.card - 1;

    if (k < 0) {
        if (!rl_camp.saves.empty()) {
            RL_CampLoad(rl_camp.saves[0].name);
        }
        return;
    }
    if (!RL_MissionOpen(k)) {
        return;
    }
    if (RL_IsTraining(k)) {
        Cbuf_AddText("spmap training\n");
    } else {
        static const char *const commands[] = {"difficultyeasy", "difficultymedium", "difficultyhard"};

        Cbuf_AddText(va("%s;spmap %s%d\n", commands[rl_camp.diff], RL_GAME.briefing, RL_MissionNumber(k)));
    }
}

// DELETE confirmed: the saved game picked's files gone (the next one picked)
static void RL_CampDelete(void)
{
    int             at;
    const rlSave_t *s = RL_CampPicked(&at);

    if (!s) {
        return;
    }
    if (!strcmp(Cvar_VariableString("g_lastsave"), s->name.c_str())) {
        // (not the one loadlastgame loads)
        Cvar_Set("g_lastsave", "");
    }
    Com_WipeSavegame(s->name.c_str());
    rl_camp.save    = at + 1 < (int)rl_camp.saves.size() ? rl_camp.saves[at + 1].name : at ? rl_camp.saves[at - 1].name : "";
    rl_camp.scanned = false;
}

std::string RL_MissionLine(const char *map)
{
    const int k = RL_MissionOf(map), skill = Q_max(0, Q_min(2, Cvar_VariableIntegerValue("skill")));

    if (k < 0) {
        return std::string("SINGLE PLAYER  \xb7  ") + rl_diffs[skill];
    }
    if (RL_IsTraining(k)) {
        return RL_Upper(RL_GAME.missions[k].name);
    }
    return std::string(RL_GAME.missions[k].tag) + "  \xb7  " + RL_Upper(RL_GAME.missions[k].name) + "  \xb7  " + rl_diffs[skill];
}

/*
====================================================================

DRAWING

====================================================================
*/

// a frame t pixels thick inside a box's edge
static void RL_Frame(const rlView_t& v, float x, float y, float w, float h, int t, rlColor_t c)
{
    const int x0 = RL_P(v, x), y0 = RL_P(v, y), x1 = RL_P(v, x + w), y1 = RL_P(v, y + h);

    RL_BoxPx(x0, y0, x1, y0 + t, c);
    RL_BoxPx(x0, y1 - t, x1, y1, c);
    RL_BoxPx(x0, y0 + t, x0 + t, y1 - t, c);
    RL_BoxPx(x1 - t, y0 + t, x1, y1 - t, c);
}

// a panel: dark, a line round it (square: rounded, a panel this size would be baked as large)
static void RL_Panel(const rlView_t& v, float x, float y, float w, float h, rlColor_t fill)
{
    RL_Box(v, x, y, w, h, fill);
    RL_Frame(v, x, y, w, h, 1, RL_LINE);
}

// a panel's header band, h high, a line under it
static void RL_Head(const rlView_t& v, float x, float y, float w, float h)
{
    RL_Box(v, x + 1, y + 1, w - 2, h - 1, RL_HEAD);
    RL_Box(v, x, y + h, w, 1, RL_LINE);
}

// the divider between tabs: a line a pixel wide at the tabs' edge, x + 6
static void RL_Divider(const rlView_t& v, float x, float y, float h)
{
    const float m  = h * 0.18f;
    const int   x0 = RL_P(v, x + 6);

    RL_BoxPx(x0, RL_P(v, y + m), x0 + 1, RL_P(v, y + h - m), RL_Rgb(255, 255, 255, 97));
}

void RL_Button(UIReloadedMenu *m, const rlView_t& v, float x, float y, float w, float h, const char *label, bool primary, int id, bool off)
{
    const bool      hot  = m->Hot(id);
    const rlColor_t fill = primary ? (hot ? RL_BRASS_HOT : RL_BRASS) : RL_Rgb(255, 255, 255, hot ? 44 : 26);

    RL_Box(v, x, y, w, h, off ? RL_Rgb(255, 255, 255, 14) : fill);
    RL_Text(v, x + w / 2, y + h / 2, label, 18, off ? RL_FAINT : primary ? RL_INK : RL_TEXT, true, RL_CENTER, 2, RL_MID);
    m->Hit(v, x, y, w, h, id);
}

// a text field: the text (typed in: its end, should it not fit, and a caret; else cut), the placeholder when empty
static void RL_Field(const rlView_t& v, float x, float y, float w, float h, const std::string& text, const char *placeholder, bool editing,
                     bool hot)
{
    const float tx = x + 16, room = w - 32;
    std::string shown = editing ? text : RL_Ellipsize(v, text.c_str(), 17, false, room);

    RL_Box(v, x, y, w, h, RL_Rgb(255, 255, 255, editing ? 70 : hot ? 48 : 30));
    RL_Box(v, x + 1, y + 1, w - 2, h - 2, RL_Rgb(0, 0, 0, 120));
    if (text.empty() && !editing) {
        RL_Text(v, tx, y + h / 2, placeholder, 17, RL_MUTED, false, RL_LEFT, 0, RL_MID, room);
        return;
    }
    while (editing && !shown.empty() && RL_TextW(v, shown.c_str(), 17, false) > room) {
        shown.erase(0, 1);
    }
    {
        const float tw = shown.empty() ? 0.0f : RL_Text(v, tx, y + h / 2, shown.c_str(), 17, RL_TEXT, false, RL_LEFT, 0, RL_MID);

        if (editing && (cls.realtime / 530) % 2 == 0) {
            RL_Box(v, tx + tw + 2, y + h / 2 - 11, 2, 22, RL_BRASS);
        }
    }
}

// a five-pointed star (the favourites') in a box d across
static void RL_Star(const rlView_t& v, float cx, float cy, float d, rlColor_t c)
{
    static std::string poly;
    const int          pd = RL_P(v, d);

    if (poly.empty()) {
        poly = "poly 1 1 1 1";
        for (int i = 0; i < 10; i++) {
            const float a = -M_PI / 2 + i * M_PI / 5, r = i % 2 ? 0.21f : 0.5f;

            poly += va(" %.3f %.3f", 0.5f + r * cosf(a), 0.52f + r * sinf(a));
        }
    }
    RL_Shape(RL_P(v, cx) - pd / 2, RL_P(v, cy) - pd / 2, pd, pd, poly.c_str(), c, v.s);
}

// a small triangle, pointing down (or up), in w x h
static void RL_Arrow(const rlView_t& v, float x, float y, float w, float h, bool up, rlColor_t c)
{
    const int x0 = RL_P(v, x), y0 = RL_P(v, y);

    RL_Shape(x0, y0, RL_P(v, x + w) - x0, RL_P(v, y + h) - y0, up ? "poly 1 1 1 1 0.5 0 1 1 0 1" : "poly 1 1 1 1 0 0 1 0 0.5 1", c, v.s);
}

// a photo (shown 4:3, as the stock load screens show theirs; a saved game's
// shot as the screen it was taken of) filling a box, cut to its shape
static void RL_Photo(const rlView_t& v, qhandle_t shot, float x, float y, float w, float h, float aspect = 4.0f / 3.0f)
{
    const int   x0 = RL_P(v, x), y0 = RL_P(v, y), x1 = RL_P(v, x + w), y1 = RL_P(v, y + h);
    const float k  = (w / h) / aspect; // over 1: the photo's top and bottom cut
    const float cs = k < 1.0f ? (1.0f - k) / 2 : 0.0f, ct = k > 1.0f ? (1.0f - 1.0f / k) / 2 : 0.0f;

    if (shot <= 0) {
        RL_Box(v, x, y, w, h, RL_Rgb(22, 24, 26));
        return;
    }
    if (!rl_bakeOnly) {
        RL_SetColor(RL_Rgb(255, 255, 255));
        uii.Rend_DrawPicStretched(x0, y0, x1 - x0, y1 - y0, cs, ct, 1.0f - cs, 1.0f - ct, shot);
    }
}

// Omaha's segmented choice: a box a label, the one chosen brass, h high (right: x its right edge; off: dimmed,
// none chosen, not hit); its right edge
static float RL_Segments(UIReloadedMenu *m, const rlView_t& v, float x, float y, float h, const char *const *labels, int n, int on, int id,
                         bool right = false, bool off = false)
{
    for (int i = 0; right && i < n; i++) {
        x -= RL_TextW(v, labels[i], 15, true, 1.6f) + 36 + (i ? 4 : 0);
    }
    for (int i = 0; i < n; i++) {
        const float w = RL_TextW(v, labels[i], 15, true, 1.6f) + 36;
        const bool  hot = !off && m->Hot(id + i), chosen = !off && i == on;

        RL_Box(v, x, y, w, h, chosen ? RL_BRASS : RL_Rgb(255, 255, 255, off ? 10 : hot ? 36 : 18));
        RL_Text(v, x + w / 2, y + h / 2, labels[i], 15, chosen ? RL_INK : off ? RL_FAINT : hot ? RL_TEXT : RL_SOFT, true, RL_CENTER, 1.6f, RL_MID);
        if (!off) {
            m->Hit(v, x, y, w, h, id + i);
        }
        x += w + 4;
    }
    return x - 4;
}

// Omaha's tabs along a panel's head (48 high from y, x 4 in from its edge), the one on (-1 none) brass: square, from
// the frame's inside, under its line; a divider only between two tabs neither on nor hot (none touching a box)
static void RL_Tabs(UIReloadedMenu *m, const rlView_t& v, float x, float y, const char *const *labels, int n, int on, int id, float size,
                    float pad)
{
    x -= 3;
    for (int i = 0; i < n; i++) {
        const float tw = RL_TextW(v, labels[i], size, true, 2) + 2 * pad;
        const bool  hot = m->Hot(id + i);

        if (i == on || hot) {
            RL_Box(v, x, y + 1, tw, 47, i == on ? RL_BRASS : RL_Rgb(255, 255, 255, 16));
        }
        RL_Text(v, x + tw / 2, y + 24, labels[i], size, i == on ? RL_INK : hot ? RL_TEXT : RL_MUTED, true, RL_CENTER, 2, RL_MID);
        m->Hit(v, x, y, tw, 48, id + i);
        x += tw;
        if (i < n - 1 && i != on && i + 1 != on && !hot && !m->Hot(id + i + 1)) {
            RL_Divider(v, x - 6, y, 48);
        }
    }
}

// a label over its control: the label's middle at y, the control 16 under it
static void RL_Label(const rlView_t& v, float x, float y, const char *label)
{
    RL_Text(v, x, y, label, 14, RL_MUTED, true, RL_LEFT, 1.6f, RL_MID);
}

// a list's row's mark: picked brass-marked, else hot (and its hit)
static void RL_RowMark(UIReloadedMenu *m, const rlView_t& v, float x, float y, float w, float h, bool sel, int id)
{
    if (sel) {
        RL_Box(v, x + 2, y, w - 3, h, RL_SEL);
        RL_Box(v, x + 2, y, 3, h, RL_BRASS);
    } else if (m->Hot(id)) {
        RL_Box(v, x + 2, y, w - 3, h, RL_Rgb(255, 255, 255, 14));
    }
    m->Hit(v, x + 2, y, w - 3, h, id);
}

// a list's row (Host's maps, the saved games) across a panel's left part w
// wide: picked brass-marked (else hot), a 16:9 photo, the title over its caption
static void RL_ListRow(UIReloadedMenu *m, const rlView_t& v, float x, float y, float w, float h, qhandle_t shot, float aspect,
                       const char *title, const char *caption, bool sel, int id)
{
    const float th = h - 16, tw = floorf(th * 16 / 9 + 0.5f), tx = x + RL_PAD + tw + 16, room = x + w - RL_PAD - tx;

    RL_RowMark(m, v, x, y, w, h, sel, id);
    RL_Photo(v, shot, x + RL_PAD, y + 8, tw, th, aspect);
    RL_Text(v, tx, y + h / 2 - 11, RL_Ellipsize(v, title, 19, sel, room).c_str(), 19, RL_TEXT, sel, RL_LEFT, 0, RL_MID);
    RL_Text(v, tx, y + h / 2 + 15, caption, 14, RL_MUTED, false, RL_LEFT, 0.6f, RL_MID, room);
}

// the scroll bar of a list over y h, its right edge at x: shown of total, from at
static void RL_ScrollBar(const rlView_t& v, float x, float y, float h, float at, float shown, float total)
{
    RL_Box(v, x - 8, y + 4 + (h - 8) * at / total, 3, Q_max(24.0f, (h - 8) * shown / total), RL_Rgb(255, 255, 255, 60));
}

// a panel's foot row (its top at foot): its band, a line over it; its controls' top
static float RL_FootRow(const rlView_t& v, float x, float foot, float w)
{
    RL_Box(v, x + 1, foot, w - 2, RL_FOOT - 1, RL_HEAD);
    RL_Box(v, x, foot, w, 1, RL_LINE);
    return foot + (RL_FOOT - RL_CTRL) / 2;
}

// the photo picked large, its name (NULL none) over its foot on a shade
static void RL_Picture(const rlView_t& v, qhandle_t shot, float aspect, float x, float y, float w, float h, const char *name)
{
    RL_Photo(v, shot, x, y, w, h, aspect);
    if (name) {
        const int x0 = RL_P(v, x), y0 = RL_P(v, y + h - 72);

        RL_Shape(x0, y0, RL_P(v, x + w) - x0, RL_P(v, y + h) - y0, "vgradient 0 1 0 0 0 0 0 0 0 0.75", RL_Rgb(255, 255, 255), v.s);
        RL_Text(v, x + RL_PAD, y + h - RL_PAD - 8, name, 28, RL_TEXT, true, RL_LEFT, 0, RL_BASE, w - 2 * RL_PAD);
    }
}

/*
================
The background: the stock load screens' photos (dm and obj maps,
Stalingrad first), each 8 s, then a 2 s fade into the next. Each is
blurred once to the screen's shape, 54 texels high, its light 0.55 (the
load screen's 36 and 0.42: RL_Blur), into one of two textures: the one
shown, and the next, made while the one shown holds. Made again at a new
screen size or a renderer's restart, looked up again after a map's load;
the photo shown again from its start when the menu opens. CAMPAIGN shows
the mission picked's photo instead, blurred so into a third texture
================
*/
static const char *const rl_photos[] = {"dmloading/mohdm6", "dmloading/mohdm1", "objloading/objdm2", "dmloading/mohdm4",
                                        "objloading/objdm1", "dmloading/mohdm7", "dmloading/mohdm2", "objloading/objdm5",
                                        "dmloading/mohdm3", "objloading/objdm4", "dmloading/mohdm5"};
#define RL_BGPHOTOS ((int)ARRAY_LEN(rl_photos))

#define RL_HOLD 8000
#define RL_FADE 2000

static struct {
    qhandle_t shader[3]; // the slots' textures ("*rl_menublur0", "1"; "2" CAMPAIGN's), 0 none
    int       made[3];   // the photo each holds (rl_photos', then the missions'), -1 none, -2 to be made again
    int       cur;       // the slot shown
    int       photo;     // the photo shown
    int       since;     // cls.realtime it began showing
    int       w, h;          // the screen's when made
    int       stamp, refSeq; // UI_BakeStamp and the renderer's sequence then
} rl_bg = {{0, 0, 0}, {-1, -1, -1}, 0, 0, 0, 0, 0, 0, 0};

// a slot holding a photo (-1 none)
static void RL_BgMake(int slot, int photo)
{
    byte *pic    = NULL;
    float aspect = 4.0f / 3.0f;
    int   pw, ph;

    if (rl_bg.made[slot] == photo) {
        return;
    }
    rl_bg.made[slot]   = photo;
    rl_bg.shader[slot] = 0;
    if (photo >= RL_BGPHOTOS) {
        const rlMission_t& m = RL_GAME.missions[photo - RL_BGPHOTOS];

        pic    = RL_MissionPhoto(m, m.photo, &pw, &ph);
        aspect = m.aspect;
    } else if (photo >= 0 && !uii.Rend_LoadRawImage(va("textures/mohmenu/%s.tga", rl_photos[photo]), &pic, &pw, &ph)) {
        pic = NULL;
    }
    if (pic) {
        rl_bg.shader[slot] = RL_Blur(va("*rl_menublur%d", slot), pic, pw, ph, 54, 0.55f, aspect);
        uii.Rend_FreeRawImage(pic);
    }
}

// CAMPAIGN's photo shown
static bool RL_BgCampaign(void)
{
    return rl_mm.page == PAGE_CAMPAIGN && rl_bg.shader[2];
}

// each frame the menu shows: the photos moved on, made
static void RL_BgTick(bool opened)
{
    const int n = RL_BGPHOTOS;

    if (rl_bg.w != uid.vidWidth || rl_bg.h != uid.vidHeight || rl_bg.refSeq != uii.GetRefSequence()) {
        rl_bg.w       = uid.vidWidth;
        rl_bg.h       = uid.vidHeight;
        rl_bg.refSeq  = uii.GetRefSequence();
        rl_bg.stamp   = UI_BakeStamp();
        rl_bg.made[0] = rl_bg.made[1] = rl_bg.made[2] = -2;
    } else if (rl_bg.stamp != UI_BakeStamp()) {
        for (int i = 0; i < 3; i++) {
            rl_bg.shader[i] = rl_bg.shader[i] ? re.RegisterShaderNoMip(va("*rl_menublur%d", i)) : 0;
        }
        rl_bg.stamp = UI_BakeStamp();
    }
    if (rl_mm.page == PAGE_CAMPAIGN) {
        const int k = RL_CampBackdrop();

        RL_BgMake(2, k < 0 ? -1 : RL_BGPHOTOS + k);
    }
    if (opened || cls.realtime - rl_bg.since >= 2 * (RL_HOLD + RL_FADE)) {
        rl_bg.since = cls.realtime;
    } else if (cls.realtime - rl_bg.since >= RL_HOLD + RL_FADE) {
        rl_bg.cur ^= 1;
        rl_bg.photo = (rl_bg.photo + 1) % n;
        rl_bg.since += RL_HOLD + RL_FADE;
    }
    RL_BgMake(rl_bg.cur, rl_bg.photo);
    RL_BgMake(!rl_bg.cur, (rl_bg.photo + 1) % n);
}

// how far the next photo has faded in, in 64ths (the back layer's draw key)
static int RL_BgFade(void)
{
    return Q_max(0, Q_min(64, (cls.realtime - rl_bg.since - RL_HOLD) * 64 / RL_FADE));
}

// the photos over x y w h as the screen shows them there (all of it, or
// the crosshair's preview: what is under Options' panel)
static void RL_BgDraw(const rlView_t& v, float x, float y, float w, float h)
{
    const int   x0 = RL_P(v, x), y0 = RL_P(v, y), x1 = RL_P(v, x + w), y1 = RL_P(v, y + h);
    const float t = RL_BgFade() / 64.0f, sw = (float)uid.vidWidth, sh = (float)uid.vidHeight;

    if (rl_bakeOnly) {
        return;
    }
    for (int i = 0; i < (RL_BgCampaign() ? 1 : 2); i++) {
        const qhandle_t shader = RL_BgCampaign() ? rl_bg.shader[2] : rl_bg.shader[i ? !rl_bg.cur : rl_bg.cur];
        // (eased in and out)
        const float     a      = i ? t * t * (3 - 2 * t) : 1.0f;

        if (shader && a > 0.0f) {
            RL_SetColor(RL_Rgb(255, 255, 255, (int)(a * 255)));
            uii.Rend_DrawPicStretched(x0, y0, x1 - x0, y1 - y0, x0 / sw, y0 / sh, x1 / sw, y1 / sh, shader);
        }
    }
}

static void RL_Background(const rlView_t& v)
{
    if (!rl_bg.shader[rl_bg.cur] && !RL_BgCampaign()) {
        RL_Box(v, 0, 0, v.dw, v.dh, RL_Rgb(8, 9, 10));
    }
    RL_BgDraw(v, 0, 0, v.dw, v.dh);
}

/*
================
The rail (the main menu's, the Esc menu's): 440 units, dark, fading out
past 400; the star and the wordmark (92 high) at its head, level with the
screens' top and the buttons' left edge; the Esc menu's match under them
(the title, two lines; a title too wide on two lines), on the labels' line;
the items from 380 down (the main menu's MULTIPLAYER / CAMPAIGN / HOST /
OPTIONS, the page shown filled brass); the foot's up from its foot, QUIT's
level with the screens' foot row, their labels under the buttons' and
their icons ending at their edge (DISCONNECT, LEAVE MISSION in single
player, over QUIT while in a game: level with the browser's status row)
================
*/
static const rlRailItem_t rl_pages[] = {
    {ID_PLAY,     "MULTIPLAYER", NULL},
    {ID_CAMPAIGN, "CAMPAIGN",    NULL},
    {ID_HOST,     "HOST",        NULL},
    {ID_OPTIONS,  "OPTIONS",     NULL},
};

void RL_Rail(UIReloadedMenu *m, const rlView_t& v, const rlRailItem_t *items, int n, int on, const rlRailItem_t *foot, int nf,
             const std::string *head)
{
    const float qy = v.dh - RL_EDGE - RL_FOOT / 2, room = RL_RAILX + 300 - RL_LABELX;

    RL_Box(v, 0, 0, 400, v.dh, RL_Rgb(10, 11, 12, 232));
    RL_HGradient(v, 400, 0, 240, v.dh, RL_Rgb(10, 11, 12, 232), RL_Rgb(10, 11, 12, 0));
    {
        // the lockup, the game's: its icon 92 across (Allied Assault's star), a gap of a fifth, its wordmark as high
        static const char *const icons[3] = {RL_GLYPH("star"), RL_GLYPH("spearhead"), RL_GLYPH("breakthrough")};
        static const char *const marks[3] = {RL_GLYPH("wordmark"), RL_GLYPH("wordmark-spearhead"), RL_GLYPH("wordmark-breakthrough")};
        const int                g        = Q_max(0, Q_min(2, com_target_game->integer));
        float                    sw, sh;

        RL_Outline(v, icons[g], RL_RAILX + 46, RL_EDGE + 46, 92, 92, RL_Rgb(255, 255, 255));
        if (UI_OutlineSize(marks[g], &sw, &sh)) {
            const float ww = 92 * sw / sh;

            RL_Outline(v, marks[g], RL_RAILX + 92 * 1.2f + ww / 2, RL_EDGE + 46, ww, 92, RL_Rgb(255, 255, 255));
        }
    }
    if (head) {
        std::string title = head[0], more;
        float       y     = 218;

        if (RL_TextW(v, title.c_str(), 34, true) > room) {
            // (at the last space that leaves the first line fitting)
            for (size_t at = title.rfind(' '); at != std::string::npos && at > 0; at = title.rfind(' ', at - 1)) {
                if (RL_TextW(v, title.substr(0, at).c_str(), 34, true) <= room) {
                    more  = title.substr(at + 1);
                    title = title.substr(0, at);
                    break;
                }
            }
        }
        if (!more.empty()) {
            RL_Text(v, RL_LABELX, y - 19, title.c_str(), 34, RL_TEXT, true, RL_LEFT, 0, RL_MID, room);
            title = more;
            y += 19;
        }
        RL_Text(v, RL_LABELX, y, title.c_str(), 34, RL_TEXT, true, RL_LEFT, 0, RL_MID, room);
        RL_Text(v, RL_LABELX, y + 44, head[1].c_str(), 15, RL_MUTED, false, RL_LEFT, 2.4f, RL_MID, room);
        RL_Text(v, RL_LABELX, y + 72, head[2].c_str(), 15, RL_MUTED, false, RL_LEFT, 2.4f, RL_MID, room);
    }
    // (78 apart, 64 high; closer where the foot leaves them too little room: the Esc menu's six at 900 lines)
    const float limit = qy - (nf - 1) * 52 - 32, step = 380 + (n - 1) * 78.0f + 64 <= limit ? 78.0f : (limit - 366) / n, ih = step - 14;

    for (int i = 0; i < n; i++) {
        const float y = 380 + i * step;
        const bool  lit = items[i].id == on, hot = m->Hot(items[i].id);

        if (lit || hot) {
            RL_Box(v, RL_RAILX, y, 300, ih, lit ? RL_BRASS : RL_Rgb(255, 255, 255, 18));
        }
        RL_Text(v, RL_LABELX, y + ih / 2, items[i].label, 28, lit ? RL_INK : hot ? RL_TEXT : RL_SOFT, true, RL_LEFT, 3, RL_MID);
        m->Hit(v, RL_RAILX, y, 300, ih, items[i].id);
    }
    for (int i = 0; i < nf; i++) {
        // (the last at the foot, muted; those over it red)
        const rlRailItem_t& f = foot[i];
        const float         y = qy - (nf - 1 - i) * 52;
        const rlColor_t     c = m->Hot(f.id) ? RL_TEXT : i == nf - 1 ? RL_MUTED : RL_Rgb(214, 120, 100);

        RL_Outline(v, f.glyph, RL_RAILX - 12, y, 24, 24, c);
        RL_Text(v, RL_LABELX, y, f.label, 20, c, true, RL_LEFT, 2.4f, RL_MID);
        m->Hit(v, RL_RAILX - 24, y - 24, 260, 48, f.id);
    }
}

/*
================
MULTIPLAYER: Omaha's server browser. The gametype tabs, the columns (the one
sorted by marked, clicked again for the other way), a row a server (a
star to keep it a favourite; picked: brass; joined with a double click
or JOIN), the status row (servers and players shown, the lists' state),
REFRESH, the search, JOIN. Narrower than 800 units (4:3), no GAMETYPE
================
*/
static void RL_Browser(UIReloadedMenu *m, const rlView_t& v, float x, float y, float w, float h)
{
    const bool  narrow = w < 800;
    // the columns' shares of the width, the star's first
    const float wide[COL_NUM + 1] = {0.05f, 0.42f, 0.19f, 0.11f, 0.14f, 0.09f}, small[COL_NUM + 1] = {0.06f, 0.46f, 0.25f, 0.12f, 0, 0.11f};
    const float *share = narrow ? small : wide;
    const float iw = w - 2 * RL_PAD, hy = y + 49, rh = 36, foot = 40 + RL_FOOT, ry = hy + 41;
    float       cx[COL_NUM + 1];

    RL_Panel(v, x, y, w, h, RL_PANEL);
    RL_Head(v, x, y, w, 48);
    RL_Tabs(m, v, x + 4, y, rl_tabs, TAB_NUM, rl_mm.tab, ID_TAB, narrow ? 15.0f : 17.0f, narrow ? 12.0f : 20.0f);
    // the columns' heads
    cx[0] = x + RL_PAD;
    for (int c = 1; c <= COL_NUM; c++) {
        cx[c] = cx[c - 1] + iw * share[c - 1];
    }
    RL_Box(v, x + 1, hy, w - 2, 40, RL_Rgb(0, 0, 0, 50));
    RL_Box(v, x, hy + 40, w, 1, RL_LINE);
    RL_Star(v, cx[0] + iw * 0.025f, hy + 20, 18, RL_Rgb(255, 255, 255, 90));
    for (int c = 0; c < COL_NUM; c++) {
        const bool hot = m->Hot(ID_SORT + c);

        if (!share[c + 1]) {
            continue;
        }
        const float lw = RL_Text(v, cx[c + 1], hy + 20, rl_colNames[c], 14, hot ? RL_TEXT : RL_MUTED, true, RL_LEFT, 1.6f, RL_MID);

        if (rl_mm.sort == c) {
            // (down: from the least, Omaha's)
            RL_Arrow(v, cx[c + 1] + lw + 8, hy + 16, 10, 8, !rl_mm.asc, RL_BRASS);
        }
        m->Hit(v, cx[c + 1] - 6, hy, iw * share[c + 1], 40, ID_SORT + c);
    }
    // the rows
    RL_SvShown();
    rl_mm.fit = Q_max(1, (int)((y + h - foot - ry) / rh));
    rl_mm.top = Q_max(0, Q_min(rl_mm.top, (int)rl_vis.rows.size() - rl_mm.fit));
    for (int i = 0; i < rl_mm.fit && rl_mm.top + i < (int)rl_vis.rows.size(); i++) {
        const int         at  = rl_mm.top + i;
        const rlServer_t& r   = rl_sv.rows[rl_vis.rows[at]];
        const bool        sel = r.addr == rl_mm.sel, fav = rl_favs.count(r.addr) > 0;
        const float       yy = ry + i * rh, mid = yy + rh / 2;
        const rlColor_t   soft = r.otherVersion ? RL_FAINT : RL_SOFT;

        if (sel) {
            RL_Box(v, x + 2, yy, w - 4, rh, RL_SEL);
            RL_Box(v, x + 2, yy, 3, rh, RL_BRASS);
        } else if (m->Hot(ID_ROW + i) || m->Hot(ID_FAV + i)) {
            RL_Box(v, x + 2, yy, w - 4, rh, RL_Rgb(255, 255, 255, 14));
        } else if (at % 2) {
            RL_Box(v, x + 2, yy, w - 4, rh, RL_Rgb(255, 255, 255, 6));
        }
        RL_Star(v, cx[0] + iw * 0.025f, mid, 18, fav ? RL_BRASS : m->Hot(ID_FAV + i) ? RL_Rgb(255, 255, 255, 120) : RL_Rgb(255, 255, 255, 40));
        RL_Text(v, cx[1], mid, RL_Ellipsize(v, r.name.empty() ? "?" : r.name.c_str(), 17, sel, iw * share[1] - 12).c_str(), 17,
                r.otherVersion ? RL_FAINT : RL_TEXT, sel, RL_LEFT, 0, RL_MID);
        RL_Text(v, cx[2], mid, RL_Ellipsize(v, r.map.c_str(), 17, false, iw * share[2] - 12).c_str(), 17, soft, false, RL_LEFT, 0, RL_MID);
        RL_Text(v, cx[3], mid, va("%d/%d", r.players, r.maxPlayers), 17, soft, false, RL_LEFT, 0, RL_MID);
        if (share[4]) {
            RL_Text(v, cx[4], mid, RL_Ellipsize(v, r.mode.c_str(), 17, false, iw * share[4] - 12).c_str(), 17, soft, false, RL_LEFT, 0, RL_MID);
        }
        RL_Text(v, cx[5], mid, va("%d", r.ping), 17,
                r.otherVersion ? RL_FAINT
                : r.ping < 60  ? RL_Rgb(150, 196, 120)
                : r.ping < 100 ? RL_Rgb(222, 178, 80)
                               : RL_Rgb(214, 120, 100),
                false, RL_LEFT, 0, RL_MID);
        m->Hit(v, x + 2, yy, w - 4, rh, ID_ROW + i);
        m->Hit(v, cx[0], yy, iw * share[0], rh, ID_FAV + i);
    }
    if (rl_vis.rows.empty()) {
        const char *empty = RL_SvBusy()            ? "Looking for servers\x85"
                          : rl_sv.failed           ? "The master server can't be reached."
                          : !rl_sv.rows.empty()    ? "No server matches."
                                                   : "No servers.";

        RL_Text(v, x + w / 2, ry + 3 * rh, empty, 17, RL_MUTED, false, RL_CENTER, 0, RL_MID);
    } else if ((int)rl_vis.rows.size() > rl_mm.fit) {
        RL_ScrollBar(v, x + w, ry, y + h - foot - ry, rl_mm.top, rl_mm.fit, rl_vis.rows.size());
    }
    {
        // the status row: the servers and players shown, the lists' state (or a word)
        const float sy = y + h - foot;
        const char *state;
        int         answered = 0, total = 0;

        for (GServerList l : rl_sv.list) {
            int ok, late;

            if (l) {
                ServerListGetQueryStats(l, &ok, &late);
                answered += ok + late;
                total += ServerListCount(l);
            }
        }
        if (!rl_mm.note.empty()) {
            state = rl_mm.note.c_str();
        } else if (rl_sv.failed && rl_sv.rows.empty()) {
            state = "NO MASTER SERVER";
        } else if (RL_SvBusy()) {
            state = total ? va("QUERYING  %d / %d", answered, total) : "GETTING THE LIST";
        } else {
            state = "READY";
        }
        RL_Box(v, x + 1, sy, w - 2, 40, RL_Rgb(255, 255, 255, 12));
        RL_Box(v, x, sy, w, 1, RL_LINE);
        RL_Text(v, x + RL_PAD, sy + 20, va("%d SERVERS", (int)rl_vis.rows.size()), 15, RL_TEXT, true, RL_LEFT, 1.6f, RL_MID);
        RL_Text(v, x + w / 2, sy + 20, state, 15, RL_BRASS, true, RL_CENTER, 2, RL_MID);
        RL_Text(v, x + w - RL_PAD, sy + 20, va("%d PLAYERS", rl_vis.players), 15, RL_TEXT, true, RL_RIGHT, 1.6f, RL_MID);
    }
    {
        // the actions
        const float ay = y + h - RL_FOOT, bh = RL_CTRL, by = ay + (RL_FOOT - RL_CTRL) / 2, jw = 130, fx = x + RL_PAD + 154;
        const float fw = w - 2 * RL_PAD - 150 - jw - 8;
        const bool  busy = RL_SvBusy();

        RL_Box(v, x + 1, ay, w - 2, RL_FOOT - 1, RL_HEAD);
        RL_Button(m, v, x + RL_PAD, by, 150, bh, "REFRESH", false, ID_REFRESH, busy);
        RL_Field(v, fx, by, fw, bh, rl_mm.search, "Search", rl_mm.edit == F_SEARCH, m->Hot(ID_SEARCH));
        m->Hit(v, fx, by, fw, bh, ID_SEARCH);
        RL_Button(m, v, x + w - RL_PAD - jw, by, jw, bh, "JOIN", true, ID_JOIN);
    }
}

/*
================
MULTIPLAYER's profile: the name (APPLY sets it), each side's model in a 3D view
(the back layer places the views over the columns) under the side's
emblem, and a list to pick it from (open: over the view, in the front
layer), the pickers on the foot row's line
================
*/
static void RL_Profile(UIReloadedMenu *m, const rlView_t& v, float x, float y, float w, float h)
{
    static cvar_t *views[2];
    const float    aw = 110, cw = (w - 2 * RL_PAD - 10) / 2, fy = y + h - RL_FOOT + (RL_FOOT - RL_CTRL) / 2;
    // the views: as tall as the figures fit the columns' width (arms out), on the pickers
    // (the emblems 44 across, 26 over the views, 16 under the name's line at least)
    const float    vh = Q_min(fy - 14 - (y + 148 + 16 + 70), cw * 2.5f), vy = fy - 14 - vh;

    if (!views[0]) {
        views[0] = Cvar_Get("ui_reloaded_allies", "", 0);
        views[1] = Cvar_Get("ui_reloaded_axis", "", 0);
    }
    if (m->m_back) {
        RL_Panel(v, x, y, w, h, RL_PANEL);
        RL_Head(v, x, y, w, 48);
        for (int t = 0; t < 2; t++) {
            const std::string figure = RL_Figure(RL_OwnModel(t));

            if (Q_stricmp(views[t]->string, figure.c_str())) {
                Cvar_Set(views[t]->name, figure.c_str());
            }
            m->PlaceView(v, t ? "axis" : "allies", x + RL_PAD + t * (cw + 10), vy, cw, vh);
        }
        return;
    }
    RL_Text(v, x + RL_PAD, y + 24, "PROFILE", 20, RL_TEXT, true, RL_LEFT, 2, RL_MID);
    RL_Text(v, x + RL_PAD, y + 76, "NAME", 14, RL_MUTED, true, RL_LEFT, 1.6f, RL_MID);
    RL_Field(v, x + RL_PAD, y + 92, w - 2 * RL_PAD - aw - 4, 40, rl_mm.name, "", rl_mm.edit == F_NAME, m->Hot(ID_NAME));
    m->Hit(v, x + RL_PAD, y + 92, w - 2 * RL_PAD - aw - 4, 40, ID_NAME);
    RL_Button(m, v, x + w - RL_PAD - aw, y + 92, aw, 40, "APPLY", true, ID_APPLY);
    RL_Box(v, x, y + 148, w, 1, RL_LINE);
    for (int t = 0; t < 2; t++) {
        const float     cx  = x + RL_PAD + t * (cw + 10), fh = RL_CTRL;
        const int       own = RL_OwnIndex(t);
        const bool      hot = m->Hot(ID_SELECT + t) || rl_mm.pick == t;
        const rlColor_t edge = hot ? RL_Rgb(255, 255, 255, 70) : RL_Rgb(255, 255, 255, 30);

        RL_TeamIcon(v, t, cx + cw / 2, vy - 48, 44);
        RL_Box(v, cx, fy, cw, fh, RL_Rgb(0, 0, 0, 110));
        RL_BoxPx(RL_P(v, cx), RL_P(v, fy), RL_P(v, cx + cw), RL_P(v, fy) + 1, edge);
        RL_BoxPx(RL_P(v, cx), RL_P(v, fy + fh) - 1, RL_P(v, cx + cw), RL_P(v, fy + fh), edge);
        RL_BoxPx(RL_P(v, cx), RL_P(v, fy), RL_P(v, cx) + 1, RL_P(v, fy + fh), edge);
        RL_BoxPx(RL_P(v, cx + cw) - 1, RL_P(v, fy), RL_P(v, cx + cw), RL_P(v, fy + fh), edge);
        RL_Text(v, cx + 14, fy + fh / 2,
                RL_Ellipsize(v, own >= 0 ? rl_models[t][own].name.c_str() : RL_ModelName(RL_OwnModel(t).c_str()).c_str(), 16, false, cw - 48).c_str(),
                16, RL_TEXT, false, RL_LEFT, 0, RL_MID);
        RL_Arrow(v, cx + cw - 26, fy + fh / 2 - 4, 12, 8, rl_mm.pick == t, hot ? RL_TEXT : RL_MUTED);
        m->Hit(v, cx, fy, cw, fh, ID_SELECT + t);
    }
    if (rl_mm.pick >= 0) {
        // the side's models, over its view (scrolled by the wheel when they don't fit)
        const std::vector<rlModel_t>& list = rl_models[rl_mm.pick];
        const float                   ih = 32, lx = x + RL_PAD + rl_mm.pick * (cw + 10), lw = Q_max(cw, 200.0f);
        const int                     n  = (int)list.size(), fit = Q_max(1, Q_min(n, (int)((fy - 8 - (y + 160)) / ih)));
        const float                   lh = fit * ih + 8, ly = fy - 4 - lh, px = Q_min(lx, x + w - RL_PAD - lw);
        const int                     own = RL_OwnIndex(rl_mm.pick);

        rl_mm.pickTop = Q_max(0, Q_min(rl_mm.pickTop, n - fit));
        RL_Panel(v, px, ly, lw, lh, RL_Rgb(12, 13, 15, 250));
        for (int i = 0; i < fit && rl_mm.pickTop + i < n; i++) {
            const int   k  = rl_mm.pickTop + i;
            const float iy = ly + 4 + i * ih;

            if (k == own) {
                RL_Box(v, px + 2, iy, lw - 4, ih, RL_SEL);
                RL_Box(v, px + 2, iy, 3, ih, RL_BRASS);
            } else if (m->Hot(ID_PICK + k)) {
                RL_Box(v, px + 2, iy, lw - 4, ih, RL_Rgb(255, 255, 255, 18));
            }
            RL_Text(v, px + 14, iy + ih / 2, RL_Ellipsize(v, list[k].name.c_str(), 16, k == own, lw - 28).c_str(), 16,
                    k == own ? RL_BRASS : RL_TEXT, k == own, RL_LEFT, 0, RL_MID);
            m->Hit(v, px + 2, iy, lw - 4, ih, ID_PICK + k);
        }
    }
}

/*
================
HOST: Omaha's host panel. The maps (the mode's) on the left, a row each:
its photo, its name, its file; the one picked large on the right with its
name over it, then a row each: the game type, the time and frag limits,
the players and bots, the server's name, who can join; START on the foot
row
================
*/
static void RL_Host(UIReloadedMenu *m, const rlView_t& v, float x, float y, float w, float h)
{
    // (the rows on the right 74 apart: a label, its control 16 under it, 40 high)
    const float lw = floorf(w * 0.40f + 0.5f), rh = 76, ly = y + 96, foot = y + h - RL_FOOT;
    const float rx = x + lw + RL_PAD, rw = w - lw - 2 * RL_PAD, col = (rw - RL_PAD) / 2;
    // (the picture's top on the first map's photo's)
    const float py = ly + 8, ph = floorf(Q_min(Q_min(rw * 9 / 16, h * 0.42f), foot - py - 398));
    const int   fit = Q_max(1, (int)((foot - ly) / rh)), n = (int)rl_host.maps.size();
    float       fy  = py + ph + RL_PAD + 14;

    RL_Panel(v, x, y, w, h, RL_PANEL);
    RL_Head(v, x, y, w, 48);
    RL_Text(v, x + RL_PAD, y + 24, "HOST A GAME", 20, RL_TEXT, true, RL_LEFT, 2, RL_MID);
    RL_Box(v, x + lw, y + 49, 1, foot - (y + 49), RL_LINE);
    // the maps
    RL_Label(v, x + RL_PAD, y + 76, "MAP");
    rl_host.top = Q_max(0, Q_min(rl_host.top, n - fit));
    for (int i = 0; i < fit && rl_host.top + i < n; i++) {
        const std::string& map = rl_host.maps[rl_host.top + i];

        RL_ListRow(m, v, x, ly + i * rh, lw, rh, RL_Shot(map), 4.0f / 3.0f, RL_MapName(map.c_str()), map.c_str(), map == rl_host.map, ID_MAP + i);
    }
    if (!n) {
        RL_Text(v, x + RL_PAD, ly + rh / 2, "No maps for this mode.", 17, RL_MUTED, false, RL_LEFT, 0, RL_MID);
    } else if (n > fit) {
        RL_ScrollBar(v, x + lw, ly, foot - ly, rl_host.top, fit, n);
    }
    // the map picked, its name over its foot
    RL_Picture(v, rl_host.map.empty() ? 0 : RL_Shot(rl_host.map), 4.0f / 3.0f, rx, py, rw, ph,
               rl_host.map.empty() ? NULL : RL_MapName(rl_host.map.c_str()));
    // the settings
    RL_Label(v, rx, fy, "GAMETYPE");
    RL_Segments(m, v, rx, fy + 16, 40, rl_modeLabels, RL_HostModes(), rl_host.mode, ID_MODE);
    fy += 74;
    for (int f = HF_TIME; f < HF_NUM; f++) {
        static const char *const labels[HF_NUM] = {"TIME LIMIT", "FRAG LIMIT", "PLAYERS", "BOTS", "SERVER NAME"};
        // (two a row; the name the whole row)
        const float fx = rx + (f % 2) * (col + RL_PAD), fw = f == HF_NAME ? rw : col;

        RL_Label(v, fx, fy, labels[f]);
        RL_Field(v, fx, fy + 16, fw, 40, rl_host.field[f], "", rl_mm.edit == F_HOST + f, m->Hot(ID_FIELD + f));
        m->Hit(v, fx, fy + 16, fw, 40, ID_FIELD + f);
        fy += f % 2 || f == HF_NAME ? 74 : 0;
    }
    RL_Label(v, rx, fy, "WHO CAN JOIN");
    RL_Segments(m, v, rx, fy + 16, 40, rl_joinLabels, 2, rl_host.online, ID_LOCAL);
    {
        const float by = RL_FootRow(v, x, foot, w);

        RL_Button(m, v, x + w - RL_PAD - 190, by, 190, RL_CTRL, "START", true, ID_START, rl_host.map.empty());
    }
}

/*
================
CAMPAIGN: the MISSIONS tab, load screen A's cards (the name and its tag
over the photo, a bar and two words under it), Continue first, four across
while a card is 300 wide (else three; Spearhead's and Breakthrough's four
two across), the one picked framed brass, a
locked one grey with a padlock; the difficulty and START on the foot row.
The SAVED GAMES tab: Host's list and the one picked large, its mission,
kind and time; DELETE, SAVE (in a mission) and LOAD. The game's name on
the head's right
================
*/
typedef struct {
    std::string name, tag, left, right;
    qhandle_t   photo;
    float       aspect;
    bool        dim;    // can't be picked (no saved game, locked)
    bool        locked; // grey (its photo made so), a padlock
} rlCard_t;

static void RL_Card(UIReloadedMenu *m, const rlView_t& v, const rlCard_t& c, float x, float y, float w, float ph, bool on, int id)
{
    const float tagw = RL_TextW(v, c.tag.c_str(), 14, true, 2.6f), room = w - tagw - 16, py = y + 44, by = py + ph + 14;
    const float rw   = c.right.empty() ? 0.0f : RL_TextW(v, c.right.c_str(), 16, true);
    float       size = 24;

    // the name smaller (to 20), then cut, to leave its tag room
    while (size > 20 && RL_TextW(v, c.name.c_str(), size, true) > room) {
        size--;
    }
    RL_Text(v, x, y + 30, RL_Ellipsize(v, c.name.c_str(), size, true, room).c_str(), size, c.dim ? RL_FAINT : RL_TEXT, true, RL_LEFT, 0, RL_BASE);
    RL_Text(v, x + w, y + 30, c.tag.c_str(), 14, c.dim ? RL_FAINT : RL_BRASS, true, RL_RIGHT, 2.6f, RL_BASE);
    RL_Photo(v, c.photo, x, py, w, ph, c.aspect);
    if (c.locked) {
        RL_Box(v, x, py, w, ph, RL_Rgb(8, 9, 10, 158));
        RL_Outline(v, RL_GLYPH("lock"), x + w / 2, py + ph / 2, 34, 34, RL_Rgb(238, 234, 224, 210));
    }
    RL_Frame(v, x, py, w, ph, on ? Q_max(2, RL_P(v, 2)) : 1, on ? RL_BRASS : RL_Rgb(255, 255, 255, m->Hot(id) ? 120 : 40));
    RL_Box(v, x, by, w, 4, on ? RL_BRASS : RL_Rgb(255, 255, 255, 30));
    RL_Text(v, x, by + 24, RL_Ellipsize(v, c.left.c_str(), 16, false, w - rw - 12).c_str(), 16, c.dim ? RL_FAINT : RL_MUTED, false, RL_LEFT, 0,
            RL_MID);
    if (rw) {
        RL_Text(v, x + w, by + 24, c.right.c_str(), 16, RL_TEXT, true, RL_RIGHT, 0, RL_MID);
    }
    if (!c.dim) {
        m->Hit(v, x, y, w, by + 36 - y, id);
    }
}

static void RL_CampMissions(UIReloadedMenu *m, const rlView_t& v, float x, float y, float w, float foot)
{
    const int   cards = 1 + RL_GAME.n;
    const float gap = 24, top = y + 49 + RL_PAD, bot = foot - RL_PAD;
    const int   cols = cards <= 4 ? 2 : (w - 2 * RL_PAD - 3 * gap) / 4 >= 300 ? 4 : 3, rows = (cards + cols - 1) / cols;
    const float cw = floorf((w - 2 * RL_PAD - (cols - 1) * gap) / cols), ph = floorf((bot - top - (rows - 1) * gap) / rows - 96);

    for (int k = 0; k < cards; k++) {
        rlCard_t c;

        c.aspect = 4.0f / 3.0f;
        if (!k) {
            // Continue: the newest saved game
            const rlSave_t *s = rl_camp.saves.empty() ? NULL : &rl_camp.saves[0];

            c.name   = "Continue";
            c.tag    = "NEWEST SAVE";
            c.photo  = s ? RL_SavePhoto(*s, &c.aspect) : 0;
            c.left   = s ? s->comment : "No saved game yet";
            c.right  = s ? RL_When(s->time, false) : "";
            c.dim    = !s;
            c.locked = false;
        } else {
            const rlMission_t& mi   = RL_GAME.missions[k - 1];
            const bool         open = RL_MissionOpen(k - 1);
            const int          before = RL_MissionNumber(k - 1) - 1;

            c.name   = mi.name;
            c.tag    = mi.tag;
            c.photo  = RL_Shot(RL_MissionKey(k - 1));
            c.aspect = mi.aspect;
            c.left   = open ? mi.place : before ? va("Finish Mission %d to unlock", before) : "Finish Training to unlock";
            c.right  = open ? mi.year : "";
            c.dim = c.locked = !open;
        }
        RL_Card(m, v, c, x + RL_PAD + (k % cols) * (cw + gap), top + (k / cols) * (ph + 96 + gap), cw, ph, k == rl_camp.card, ID_CARD + k);
    }
    // the foot row: the difficulty (Continue's the saved game's, Training's none) and its meaning, START
    {
        const float by   = RL_FootRow(v, x, foot, w);
        const bool  training = rl_camp.card > 0 && RL_IsTraining(rl_camp.card - 1);
        const float lw       = RL_TextW(v, "DIFFICULTY", 14, true, 1.6f);
        const float ex = RL_Segments(m, v, x + RL_PAD + lw + 16, by, RL_CTRL, rl_diffs, 3, rl_camp.diff, ID_DIFF, false, !rl_camp.card || training);
        const char *says = !rl_camp.card ? "The saved game keeps its own." : training ? "Training is the same for everyone." : rl_diffSays[rl_camp.diff];

        RL_Label(v, x + RL_PAD, foot + RL_FOOT / 2, "DIFFICULTY");
        RL_Text(v, ex + 16, foot + RL_FOOT / 2, RL_Ellipsize(v, says, 17, false, x + w - RL_PAD - 190 - 16 - (ex + 16)).c_str(), 17, RL_SOFT,
                false, RL_LEFT, 0, RL_MID);
        RL_Button(m, v, x + w - RL_PAD - 190, by, 190, RL_CTRL, "START", true, ID_CSTART, !rl_camp.card && rl_camp.saves.empty());
    }
}

static void RL_CampSaves(UIReloadedMenu *m, const rlView_t& v, float x, float y, float w, float foot)
{
    // (Host's: the list 40 % across, its rows 76 high; the picture's top on the first row's photo's, the details under it)
    const float     lw = floorf(w * 0.40f + 0.5f), rx = x + lw + RL_PAD, rw = w - lw - 2 * RL_PAD, rh = 76, ly = y + 96;
    const float     py = ly + 8, ph = foot - py - 172, col = (rw - RL_PAD) / 2;
    const int       n  = (int)rl_camp.saves.size();
    const rlSave_t *s  = RL_CampPicked();

    rl_camp.fit = Q_max(1, (int)((foot - ly) / rh));
    rl_camp.top = Q_max(0, Q_min(rl_camp.top, n - rl_camp.fit));
    RL_Box(v, x + lw, y + 49, 1, foot - (y + 49), RL_LINE);
    RL_Label(v, x + RL_PAD, y + 76, "NEWEST FIRST");
    for (int i = 0; i < rl_camp.fit && rl_camp.top + i < n; i++) {
        const rlSave_t& r = rl_camp.saves[rl_camp.top + i];
        float           aspect;
        const qhandle_t shot = RL_SavePhoto(r, &aspect);

        RL_ListRow(m, v, x, ly + i * rh, lw, rh, shot, aspect, r.comment.c_str(), (RL_When(r.time, false) + "  \xb7  " + RL_SaveMission(r)).c_str(),
                   &r == s, ID_SAVE + i);
    }
    if (!n) {
        RL_Text(v, x + RL_PAD, ly + rh / 2, "No saved games yet.", 17, RL_MUTED, false, RL_LEFT, 0, RL_MID);
    } else if (n > rl_camp.fit) {
        RL_ScrollBar(v, x + lw, ly, foot - ly, rl_camp.top, rl_camp.fit, n);
    }
    // the one picked: its mission, its kind, when
    if (s) {
        const std::string rows[3][2] = {
            {"MISSION", RL_SaveMission(*s)},
            {"KIND", s->name == "quick" ? "Quick save" : s->autosave ? "Autosave" : "Saved by you"},
            {"SAVED", RL_When(s->time, true)},
        };
        float           aspect, fy = py + ph + RL_PAD + 14;
        const qhandle_t shot = RL_SavePhoto(*s, &aspect);

        RL_Picture(v, shot, aspect, rx, py, rw, ph, s->comment.c_str());
        for (int k = 0; k < 3; k++) {
            const float fx = rx + (k % 2) * (col + RL_PAD);

            RL_Label(v, fx, fy, rows[k][0].c_str());
            RL_Text(v, fx, fy + 34, rows[k][1].c_str(), 20, RL_TEXT, false, RL_LEFT, 0, RL_MID, col);
            fy += k % 2 ? 74 : 0;
        }
    } else {
        RL_Picture(v, 0, 1, rx, py, rw, ph, NULL);
    }
    // the foot row: DELETE, SAVE (in a mission) beside LOAD
    {
        const float by = RL_FootRow(v, x, foot, w);

        RL_Button(m, v, x + RL_PAD, by, 150, RL_CTRL, "DELETE", false, ID_CDELETE, !s);
        if (RL_InMission()) {
            RL_Button(m, v, x + w - RL_PAD - 190 - 8 - 150, by, 150, RL_CTRL, "SAVE", false, ID_CSAVE);
        }
        RL_Button(m, v, x + w - RL_PAD - 190, by, 190, RL_CTRL, "LOAD", true, ID_CLOAD, !s);
    }
}

static void RL_Campaign(UIReloadedMenu *m, const rlView_t& v, float x, float y, float w, float h)
{
    if (rl_camp.diff < 0) {
        rl_camp.diff = Q_max(0, Q_min(2, Cvar_VariableIntegerValue("skill")));
    }
    RL_Panel(v, x, y, w, h, RL_PANEL);
    RL_Head(v, x, y, w, 48);
    RL_Tabs(m, v, x + 4, y, rl_campTabs, 2, rl_camp.tab, ID_CTAB, 17, 22);
    RL_Text(v, x + w - RL_PAD, y + 24, RL_GAME.title, 15, RL_MUTED, true, RL_RIGHT, 2.6f, RL_MID);
    if (rl_camp.tab == CT_MISSIONS) {
        RL_CampMissions(m, v, x, y, w, y + h - RL_FOOT);
    } else {
        RL_CampSaves(m, v, x, y, w, y + h - RL_FOOT);
    }
}

/*
====================================================================

OPTIONS

Omaha's settings: tabs, sections titled in brass, a row a setting: its
label and a line describing it on the left, its control on the right (a
choice of segments, a slider with its value, a switch, colour swatches, a
text field). Two columns (the right one the crosshair's), one
under 1100 units across, scrolled when it doesn't fit. A setting is set as
it changes; those the renderer or the sound take at a restart wait for
APPLY; DEFAULTS resets the settings shown

====================================================================
*/

static const char *const rl_optTabs[OT_NUM] = {"CONTROLS", "VIDEO", "AUDIO", "GAMEPLAY", "HUD"};

enum { OC_SECTION, OC_SEG, OC_CYCLE, OC_SLIDER, OC_TOGGLE, OC_SWATCH, OC_PREVIEW, OC_CODE, OC_KEYS };

#define OF_RIGHT   1   // a section in the right column (the second)
#define OF_PCT     2   // a slider's value shown as a percentage
#define OF_NOT     8   // a switch on at 0
#define OF_NEG     16  // a switch on below 0, the value's sign (the mouse inverted: m_pitch)
#define OF_DISPLAY 64  // r_fullscreen and r_noborder: fullscreen, windowed, borderless
#define OF_ANISO   128 // r_ext_texture_filter_anisotropic and r_ext_max_anisotropy: off, or how much
#define OF_RES     256 // r_mode: the desktop's, or a size of the display's (r_customwidth, r_customheight)
#define OF_THIRD   512 // a section in the third column (CONTROLS' alone have three; else the right one)
#define OF_LEFT2   2048 // a section in the left column where there are two (else its own)
#define OF_FONT    4096 // cg_hud_font: a cycle of the HUD's fonts (rl_fonts), each name in its own font

typedef struct {
    int                kind, tab, flags; // the tab: a section's
    const char        *label, *desc;     // a section's title
    const char        *cvar, *def;
    const char *const *choices; // segments, a cycle: their labels; swatches: their colours (RRGGBB); NULL-ended
    const char *const *values;  // segments, a cycle: their values (NULL: their places)
    float              lo, hi, step; // a slider's; swatches: step the swatches a row (0: one row)
} rlOpt_t;

#define O_SECTION(tab, title, flags)                             {OC_SECTION, tab, flags, title, "", NULL, NULL, NULL, NULL, 0, 0, 0}
#define O_SEG(label, desc, cvar, def, choices, values, flags)    {OC_SEG, 0, flags, label, desc, cvar, def, choices, values, 0, 0, 0}
#define O_CYCLE(label, desc, cvar, def, choices, values, flags)  {OC_CYCLE, 0, flags, label, desc, cvar, def, choices, values, 0, 0, 0}
#define O_SLIDER(label, desc, cvar, def, lo, hi, step, flags)    {OC_SLIDER, 0, flags, label, desc, cvar, def, NULL, NULL, lo, hi, step}
#define O_TOGGLE(label, desc, cvar, def, flags)                  {OC_TOGGLE, 0, flags, label, desc, cvar, def, NULL, NULL, 0, 0, 0}
#define O_SWATCH(label, desc, cvar, def, colours, across)        {OC_SWATCH, 0, 0, label, desc, cvar, def, colours, NULL, 0, 0, across}
// a row of keys a stock action (ui/bind.scr's), from the action of command from up to the next O_KEYS's
#define O_KEYS(from)                                             {OC_KEYS, 0, 0, from, "", NULL, NULL, NULL, NULL, 0, 0, 0}

static const char *const rl_oDisplay[]     = {"FULLSCREEN", "WINDOWED", "BORDERLESS", NULL};
static const char *const rl_oTextures[]    = {"LOW", "MEDIUM", "HIGH", NULL};
static const char *const rl_oTextureMips[] = {"2", "1", "0", NULL}; // r_picmip: the mip levels left out
static const char *const rl_oFilters[]     = {"BILINEAR", "TRILINEAR", NULL};
static const char *const rl_oFilterModes[] = {"GL_LINEAR_MIPMAP_NEAREST", "GL_LINEAR_MIPMAP_LINEAR", NULL};
static const char *const rl_oAniso[]       = {"OFF", "2X", "4X", "8X", "16X", NULL};
static const char *const rl_oAnisoValues[] = {"0", "2", "4", "8", "16", NULL};
static const char *const rl_oMsaa[]        = {"OFF", "2X", "4X", "8X", NULL};
static const char *const rl_oMsaaValues[]  = {"0", "2", "4", "8", NULL};
static const char *const rl_oShadows[]     = {"OFF", "BLOB", "FULL", NULL};
static const char *const rl_oDlights[]     = {"4", "8", "16", "32", "64", NULL};
static const char *const rl_oDetail[]      = {"LOW", "MEDIUM", "HIGH", NULL};
static const char *const rl_oDetailValues[] = {"0.2", "0.6", "1", NULL};
static const char *const rl_oSpeakers[]    = {"TWO SPEAKERS", "HEADPHONES", "SURROUND 5.1", "QUAD", NULL};
static const char *const rl_oKhz[]         = {"22 KHZ", "44 KHZ", "48 KHZ", NULL};
static const char *const rl_oKhzValues[]   = {"22", "44", "48", NULL};
static const char *const rl_oWeapon[]      = {"OFF", "WEAPON", "WEAPON & HANDS", NULL};
static const char *const rl_oMoveRates[]   = {"STOCK", "100", "125", NULL};
static const char *const rl_oMoveValues[]  = {"0", "100", "125", NULL};
static const char *const rl_oHudStyles[]   = {"CS:GO", "CS2", "CLASSIC", NULL};
static const char *const rl_oHudValues[]   = {"1", "2", "3", NULL};
static const char *const rl_oClassicKills[] = {"CHAT", "BESIDE", "UNDER", NULL}; // cg_hud_classic_kills 0, 1, 2
static const char *const rl_oLagometer[]   = {"OFF", "GRAPH", "COMPACT", NULL};
// the accent's: 12 hues (red to pink) and a grey across, pastel, vivid and deep down; with the five before
// (f6f67a, deb250, 78c8f0, 96dc8c, f0f0f0) in their places
static const char *const rl_oAccents[] = {
    "f78181", "f7ac81", "f7d081", "f6f67a", "d0f781", "96dc8c", "81f7d4", "81ebf7", "78c8f0", "81a4f7", "b881f7", "f781d4", "f0f0f0",
    "f74040", "f78340", "deb250", "f7eb40", "baf740", "40f750", "40f7c0", "40e5f7", "40b4f7", "4077f7", "9640f7", "f740c0", "bebebe",
    "d11d1d", "d15f1d", "d1951d", "d1c51d", "95d11d", "1dd12c", "1dd19b", "1dbfd1", "1d8fd1", "1d53d1", "711dd1", "d11d9b", "8c8c8c",
    NULL,
};
static const char *const rl_oHitSounds[]      = {"OFF", "THUMP", "CLASSIC", "BEEP", NULL}; // the files of sound/prom/hitmarkers/
static const char *const rl_oHitSoundValues[] = {"none", "thump", "classic", "beep", NULL};
// (the launcher's)
static const char *const rl_oColours[]     = {"FFFFFF", "00FF00", "FFFF00", "00FFFF", "FF0000", "FF00FF", "000000", NULL};
static const char *const rl_oOutlines[]    = {"OFF", "FULL", "HALF", NULL};

// (the defaults the cvars' own, set here for those the game makes later, at a map's load)
static const rlOpt_t rl_opts[] = {
    O_SECTION(OT_CONTROLS, "MOUSE", 0),
    O_SLIDER("Sensitivity", "How far the view turns.", "sensitivity", "5", 0.1f, 20, 0.05f, 0),
    O_TOGGLE("Invert Mouse", "Moving the mouse up looks down.", "m_pitch", "0.022", OF_NEG),
    O_SECTION(OT_CONTROLS, "ATTACK", OF_RIGHT | OF_LEFT2),
    O_KEYS("+attackprimary"),
    O_SECTION(OT_CONTROLS, "MOVEMENT", 0),
    O_KEYS("+forward"),
    O_SECTION(OT_CONTROLS, "WEAPONS", OF_RIGHT),
    O_KEYS("+use"),
    O_SECTION(OT_CONTROLS, "GAME", OF_THIRD | OF_LEFT2),
    O_KEYS("+scores"),
    O_SECTION(OT_CONTROLS, "MULTIPLAYER", OF_THIRD),
    O_KEYS("weapdrop"),
    O_SECTION(OT_VIDEO, "DISPLAY", 0),
    O_SEG("Display Mode", "Borderless: a window without its frame.", "r_fullscreen", "1", rl_oDisplay, NULL, OF_DISPLAY),
    O_CYCLE("Resolution", "Desktop: the display's own.", "r_mode", "-2", NULL, NULL, OF_RES),
    O_TOGGLE("Vertical Sync", "No tearing, by waiting for the display.", "r_swapInterval", "0", 0),
    O_TOGGLE("No Tearing", "The newest frame each refresh, less waiting.", "r_fastSync", "0", 0),
    O_TOGGLE("Submit Thread", "Frames sent by a thread of their own.", "r_vkSubmitThread", "0", 0),
    O_SLIDER("Max FPS", "Frames a second at most. 0: no limit.", "com_maxfps", "125", 0, 1000, 1, 0),
    O_SLIDER("Max FPS in Background", "With another window in front. 0: as above.", "com_maxfpsUnfocused", "0", 0, 1000, 1, 0),
    O_TOGGLE("Show FPS", "The frame rate in a corner.", "fps", "0", 0),
    O_SECTION(OT_VIDEO, "LIGHTING", 0),
    O_SEG("Shadows", "", "cg_shadows", "0", rl_oShadows, NULL, 0),
    O_SEG("Dynamic Lights", "The most at once.", "r_maxDlights", "4", rl_oDlights, rl_oDlights, 0),
    O_TOGGLE("Fast Dynamic Lights", "Cheaper, a little coarser.", "r_fastdlights", "0", 0),
    O_TOGGLE("Flat Model Lighting", "On is recommended. Off makes players and guns much darker.", "r_fastentlight", "1", 0),
    O_SECTION(OT_VIDEO, "PICTURE", OF_RIGHT),
    O_SLIDER("Brightness", "", "r_gamma", "1", 0.5f, 3, 0.05f, 0),
    O_SEG("Texture Quality", "", "r_picmip", "0", rl_oTextures, rl_oTextureMips, 0),
    O_SEG("Filtering", "", "r_textureMode", "GL_LINEAR_MIPMAP_LINEAR", rl_oFilters, rl_oFilterModes, 0),
    O_SEG("Anisotropic", "", "r_ext_texture_filter_anisotropic", "0", rl_oAniso, rl_oAnisoValues, OF_ANISO),
    O_SEG("Multisampling", "Smoother edges.", "r_ext_multisample", "0", rl_oMsaa, rl_oMsaaValues, 0),
    O_SECTION(OT_VIDEO, "EFFECTS", OF_RIGHT),
    O_SEG("Effect Detail", "Smoke, sparks, debris.", "cg_effectdetail", "1.0", rl_oDetail, rl_oDetailValues, 0),
    O_TOGGLE("Volumetric Smoke", "Smoke grenades' clouds.", "vss_draw", "1", 0),
    O_TOGGLE("Weather", "Rain and snow.", "cg_rain", "1", 0),
    O_TOGGLE("Bullet Holes", "The marks shots leave.", "cg_marks_add", "1", 0),
    O_SECTION(OT_AUDIO, "VOLUME", 0),
    O_SLIDER("Master", "Every sound.", "s_volume", "0.9", 0, 1, 0.01f, OF_PCT),
    O_SLIDER("Music", "The music the maps' scripts play.", "s_musicvolume", "0.9", 0, 1, 0.01f, OF_PCT),
    O_SLIDER("Soundtrack", "The songs. 0: none loaded.", "s_ambientvolume", "1", 0, 1, 0.01f, OF_PCT),
    O_SECTION(OT_AUDIO, "SPEAKERS", OF_RIGHT),
    O_CYCLE("Speaker Setup", "Headphones: sounds behind, above, below.", "s_speaker_type", "0", rl_oSpeakers, NULL, 0),
    O_SEG("Sound Quality", "Headphones: always 48.", "s_khz", "44", rl_oKhz, rl_oKhzValues, 0),
    O_SECTION(OT_AUDIO, "SOUNDS", OF_RIGHT),
    O_TOGGLE("Taunts From Other Players", "Their voices; the chat line stays.", "cg_tauntsounds", "1", 0),
    O_SEG("Hit Sound", "Played when you hit someone.", "cg_hitmarker_sound", "none", rl_oHitSounds, rl_oHitSoundValues, 0),
    O_SECTION(OT_GAMEPLAY, "GENERAL", 0),
    O_SLIDER("Field of View", "How wide you see, in degrees.", "cg_fov", "80", 65, 120, 1, 0),
    O_SEG("Show Weapon", "", "cg_drawviewmodel", "2", rl_oWeapon, NULL, 0),
    O_TOGGLE("Left-Handed", "The weapon in the left hand.", "cg_righthand", "1", OF_NOT),
    O_SLIDER("Weapon Dip", "How far it dips as you run: none (CS2's) to the game's own.", "cg_viewmodel_movedrop", "1", 0, 1, 0.05f, OF_PCT),
    O_TOGGLE("Always Run", "Walk while the walk key is held.", "cl_run", "1", 0),
    O_TOGGLE("Server Lag Icon", "Shown while the server stops answering.", "cg_drawsvlag", "1", 0),
    O_SEG("Lagometer", "The connection: ping, loss, updates.", "cg_lagometer", "0", rl_oLagometer, NULL, 0),
    O_TOGGLE("Remote Prediction", "Players drawn where your shots will find them.", "cg_remotePrediction", "1", 0),
    O_SECTION(OT_GAMEPLAY, "MOVEMENT", OF_RIGHT),
    O_SEG("Movement Rate", "Moves a second, apart from the frame rate.", "cl_movefps", "100", rl_oMoveRates, rl_oMoveValues, 0),
    O_TOGGLE("Shots and Jumps Sooner", "Sent half a move sooner.", "cl_movesubgrid", "1", 0),
    O_TOGGLE("Newest Key Wins", "Opposite keys: the one pressed last counts.", "cl_nullbinds", "0", 0),
    O_SECTION(OT_GAMEPLAY, "DEMOS", OF_RIGHT),
    O_TOGGLE("Record Every Match", "A demo of each match you play.", "cl_autoRecordDemo", "0", 0),
    // Changed in OPM: demos have a video export here, so the row is back
    O_SLIDER("Video Export FPS", "A demo's video, frames a second.", "cl_aviFrameRate", "25", 10, 120, 1, 0),
    O_SECTION(OT_HUD, "HUD", 0),
    O_SEG("HUD Style", "Classic: the chat along the top, as the original.", "cg_hudstyle", "1", rl_oHudStyles, rl_oHudValues, 0),
    O_SEG("Classic Kills", "Classic only: in the chat, or beside or under the timer.", "cg_hud_classic_kills", "0", rl_oClassicKills, NULL, 0),
    O_CYCLE("HUD Font", "Chat, messages, kill feed and names.", "cg_hud_font", "oswald", NULL, NULL, OF_FONT),
    O_SLIDER("HUD Scale", "Bigger or smaller, the whole HUD.", "cg_hud_scale", "1", 0.25f, 2, 0.05f, 0),
    O_SLIDER("Radar Size", "Bigger or smaller, the radar alone.", "cg_hud_radar_size", "1", 0.5f, 1.5f, 0.05f, OF_PCT),
    O_SWATCH("Accent Colour", "The HUD's highlight.", "cg_hud_accent", "f6f67a", rl_oAccents, 13),
    O_SLIDER("Low Health", "Health turns red below this.", "cg_hud_lowhealth", "0.4", 0, 1, 0.05f, OF_PCT),
    O_TOGGLE("Players Alive", "A count of each side by the score, not a card each.", "cg_hud_alivecount", "0", 0),
    O_TOGGLE("Damage Indicator", "Red marks round the crosshair toward a hit.", "cg_hud_damage", "1", 0),
    O_TOGGLE("Hitmarker", "A white X round the crosshair when you hit.", "cg_hitmarker", "0", 0),
    O_SECTION(OT_HUD, "CROSSHAIR", OF_RIGHT),
    {OC_PREVIEW, 0, 0, "", "", NULL, NULL, NULL, NULL, 0, 0, 0},
    O_SLIDER("Length", "", "cg_crosshair_length", "8", 0, 20, 0.5f, 0),
    O_SLIDER("Gap", "", "cg_crosshair_gap", "4", -5, 10, 0.5f, 0),
    O_SLIDER("Thickness", "", "cg_crosshair_thickness", "2", 0, 6, 0.5f, 0),
    O_SWATCH("Colour", "", "cg_crosshair_color", "FFFFFF", rl_oColours, 0),
    O_SEG("Outline", "", "cg_crosshair_outline", "0", rl_oOutlines, NULL, 0),
    O_TOGGLE("Centre Dot", "", "cg_crosshair_dot", "0", 0),
    O_TOGGLE("T-Style", "No top arm.", "cg_crosshair_t", "0", 0),
    {OC_CODE, 0, 0, "Share Code", "", NULL, NULL, NULL, NULL, 0, 0, 0},
};

#define RL_OPTS ((int)ARRAY_LEN(rl_opts))
#define RL_OPT_IDS 64 // a setting's ids: its control's parts (the accent's swatches)
static_assert(ID_OPT + RL_OPTS * RL_OPT_IDS <= ID_KEY, "the settings' ids reach the keys'");

// what the renderer (and, the last, the sound) takes at a restart, APPLY's: the latched settings, and the
// brightness but on Windows (elsewhere it brightens the textures)
static const char *const rl_optRestart[] = {
    "r_mode", "r_customwidth", "r_customheight", "r_fullscreen", "r_noborder", "r_swapInterval", "r_fastSync", "r_vkSubmitThread",
#ifndef _WIN32
    "r_gamma",
#endif
    "r_picmip", "r_ext_texture_filter_anisotropic", "r_ext_max_anisotropy", "r_ext_multisample", "r_maxDlights", "r_fastdlights",
    "s_khz",
};

#define RL_RESTARTS ((int)ARRAY_LEN(rl_optRestart))

static struct {
    bool        ready; // the cvars read
    int         tab;
    std::string code, value;         // typed: a crosshair code, a slider's value
    int         row;                 // the slider whose value is typed
    float       scroll, most;        // units down (a setting's top), the most (the first whose top shows the end)
    int         drag;                // the slider dragged (its id), -1 none
    bool        down;                // the mouse's button, last frame
    std::string note;                // a word on the foot row
    int         sec[RL_OPTS];        // a setting's section
    cvar_t     *cv[RL_OPTS];
    std::string applied[RL_RESTARTS]; // the restart's settings as the renderer and the sound took them
    int         refSeq;               // the renderer's start then
    std::vector<std::string> sizes;   // the resolutions offered: "DESKTOP", then the display's ("1920 x 1080")
} rl_opt = {false, OT_CONTROLS, "", "", 0, 0, 0, -1, false, "", {}, {}, {}, -1, {}};

// a place in the layout: the setting (a row of keys: its O_KEYS and its action), its box (the content's top y 0)
typedef struct {
    int   opt, act; // act: a row of keys' action, -1 none
    float x, y, w, h;
} rlOptPlace_t;

static std::vector<rlOptPlace_t> rl_optPlaces; // as last drawn

/*
----------------
KEYS: the stock list's actions (ui/bind.scr, the game's or an expansion's),
two boxes each. A box keeps its key while it stays bound to the action's
command; an empty one takes another key bound to it, in the keys' order. A
box clicked waits for a key: the keys come to the menu (as to the stock
list's buttons, SetBindActive), Esc's too, and the mouse's buttons
(MainButtons)
----------------
*/
typedef struct {
    std::string name, cmd; // its name (the stock list's "DM " dropped), its command
    int         key[2];    // its boxes' keys: the primary, the alternate (-1 none)
} rlAct_t;

static struct {
    std::vector<rlAct_t> acts;
    int                  from[RL_OPTS], to[RL_OPTS]; // an O_KEYS's actions
    int                  synced;  // the bindings (key_serial) the boxes were synced to
    int                  wait;    // the box waiting for a key: its action * 2 + the box, -1 none
    int                  ask[2];  // the move KEY IN USE asks about: the box, the key
    bool                 swallow; // the left button bound: its release no click
} rl_keys = {{}, {}, {}, -1, -1, {0, 0}, false};

// a cvar's value: the one waiting for a restart, else its own
static const char *RL_Latched(const cvar_t *cv)
{
    return cv->latchedString ? cv->latchedString : cv->string;
}

// a setting's cvar, made if it isn't there. Not got again while a value waits for a restart: Cvar_Get takes a
// latched value at once, and the setting would change before APPLY's restart
static cvar_t *RL_OptCvar(const char *name, const char *def, int flags)
{
    cvar_t *cv = Cvar_FindVar(name);

    return cv && cv->latchedString ? cv : Cvar_Get(name, def, flags);
}

// a setting's cvar set. One the renderer takes at a restart (latched) keeps its value until APPLY's, the new one
// waiting: Cvar_Set would force it in at once
static void RL_OptCvarSet(const char *name, const char *value)
{
    const cvar_t *cv = Cvar_FindVar(name);

    if (cv && (cv->flags & CVAR_LATCH)) {
        Cvar_SetLatched(name, value);
    } else {
        Cvar_Set(name, value);
    }
}

// another cvar a setting draws from
static cvar_t *RL_OptOther(const char *name)
{
    cvar_t *cv = RL_OptCvar(name, "0", 0);

    UI_DrawDep(cv);
    return cv;
}

// a config file's lines, each tokenized as a command (Cmd_Argv) for fn
template <typename F>
static void RL_EachLine(const char *file, F fn)
{
    char *text;

    if (FS_ReadFile(file, (void **)&text) <= 0) {
        return;
    }
    for (const char *p = text; *p;) {
        const size_t n = strcspn(p, "\r\n");
        char         line[MAX_STRING_CHARS];

        Q_strncpyz(line, p, (int)Q_min(n + 1, sizeof(line)));
        Cmd_TokenizeString(line);
        fn();
        p += n + (p[n] != 0);
    }
    FS_FreeFile(text);
}

// the stock list read; an O_KEYS's actions: from its command's to the next O_KEYS's found (the first found from the list's start)
static void RL_KeysRead(void)
{
    std::vector<rlAct_t>& acts  = rl_keys.acts;
    int                   first = INT_MAX;

    RL_EachLine("ui/bind.scr", [&acts] {
        const char *name = Cmd_Argv(1);

        if (Cmd_Argc() >= 3 && !Q_stricmp(Cmd_Argv(0), "binditem")) {
            acts.push_back({Q_stricmpn(name, "DM ", 3) ? name : name + 3, Cmd_Argv(2), {-1, -1}});
        }
    });
    for (int i = 0; i < RL_OPTS; i++) {
        rl_keys.from[i] = -1;
        for (int a = 0; rl_opts[i].kind == OC_KEYS && a < (int)acts.size() && rl_keys.from[i] < 0; a++) {
            if (!Q_stricmp(acts[a].cmd.c_str(), rl_opts[i].label)) {
                rl_keys.from[i] = a;
                first           = Q_min(first, a);
            }
        }
    }
    for (int i = 0; i < RL_OPTS; i++) {
        rl_keys.to[i] = rl_keys.from[i] < 0 ? 0 : (int)acts.size();
        for (int j = 0; j < RL_OPTS; j++) {
            if (rl_keys.from[j] > rl_keys.from[i] && rl_keys.from[i] >= 0) {
                rl_keys.to[i] = Q_min(rl_keys.to[i], rl_keys.from[j]);
            }
        }
    }
    for (int i = 0; i < RL_OPTS; i++) {
        rl_keys.from[i] = rl_keys.from[i] == first ? 0 : Q_max(rl_keys.from[i], 0);
    }
}

/*
----------------
RL_OptRead

The first time Options shows (and after a renderer's start): the cvars
(the game's own made before it does), the resolutions offered (the
display's, r_availableModes, and the one set if not one of them), the
restart's settings as taken; the stock list of keys, once
----------------
*/
static void RL_OptRead(void)
{
    const char *modes = Cvar_VariableString("r_availableModes");

    if (rl_keys.acts.empty()) {
        RL_KeysRead();
    }
    rl_opt.ready = true;
    for (int i = 0, sec = 0; i < RL_OPTS; i++) {
        sec           = rl_opts[i].kind == OC_SECTION ? i : sec;
        rl_opt.sec[i] = sec;
        if (rl_opts[i].cvar) {
            rl_opt.cv[i] = RL_OptCvar(rl_opts[i].cvar, rl_opts[i].def, CVAR_ARCHIVE);
        }
    }
    // the hitmarker sounds 1, 2 and 3, saved by the build that shipped them, are gone: thump (it plays), lit
    {
        const char *hs = Cvar_VariableString("cg_hitmarker_sound");

        if (!strcmp(hs, "1") || !strcmp(hs, "2") || !strcmp(hs, "3")) {
            Cvar_Set("cg_hitmarker_sound", "thump");
        }
    }
    rl_opt.refSeq = uii.GetRefSequence();
    for (int k = 0; k < RL_RESTARTS; k++) {
        rl_opt.applied[k] = Cvar_VariableString(rl_optRestart[k]);
    }
    {
        // (the largest first, as the launcher lists them)
        std::vector<std::pair<int, int>> sizes;
        int                              w, h, n;

        while (sscanf(modes, "%dx%d%n", &w, &h, &n) == 2) {
            sizes.push_back({w, h});
            modes += n + strspn(modes + n, " ");
        }
        w = Cvar_VariableIntegerValue("r_customwidth");
        h = Cvar_VariableIntegerValue("r_customheight");
        if (Cvar_VariableIntegerValue("r_mode") == -1 && std::find(sizes.begin(), sizes.end(), std::make_pair(w, h)) == sizes.end()) {
            sizes.push_back({w, h});
        }
        std::sort(sizes.begin(), sizes.end(), std::greater<std::pair<int, int>>());
        rl_opt.sizes.assign(1, "DESKTOP");
        for (const auto& size : sizes) {
            rl_opt.sizes.push_back(va("%d x %d", size.first, size.second));
        }
    }
}

// a setting shown: its tab's (an O_KEYS with actions)
static bool RL_OptShown(int i)
{
    const rlOpt_t& o = rl_opts[i];

    if (o.kind == OC_KEYS && rl_keys.from[i] >= rl_keys.to[i]) {
        return false;
    }
    return rl_opts[rl_opt.sec[i]].tab == rl_opt.tab;
}

/*
----------------
RL_OptLayout

The settings shown placed: the sections in their column (fewer columns:
the later ones' in the last, OF_LEFT2's in the first of two), a
section's title 40 high, 14 over it past the first, a row 66 (keys 44;
swatches 34 more a row past the first), the preview 0.32 of its column
(220 at most) and 12 under it. The
columns: CONTROLS' alone (the mouse and the keys) alike, three as the
width has room for (two, one); else 0.55 and 0.45 of it, one below 1100.
The content's height; the columns' lines (their x)
----------------
*/
// swatches' rows (step a row's swatches), else 1
static int RL_SwatchRows(int i)
{
    const rlOpt_t& o = rl_opts[i];
    int            n = 0;

    if (o.kind != OC_SWATCH || o.step <= 0) {
        return 1;
    }
    while (o.choices[n]) {
        n++;
    }
    return (n + (int)o.step - 1) / (int)o.step;
}

static float RL_OptLayout(float x, float w, std::vector<rlOptPlace_t>& out, std::vector<float>& lines)
{
    std::vector<int> secs; // the sections shown
    float            cx[3], cw[3], cy[3] = {0, 0, 0};

    for (int s = 0; s < RL_OPTS; s++) {
        bool any = false;

        for (int i = s + 1; rl_opts[s].kind == OC_SECTION && i < RL_OPTS && rl_opts[i].kind != OC_SECTION && !any; i++) {
            any = RL_OptShown(i);
        }
        if (any) {
            secs.push_back(s);
        }
    }
    // CONTROLS' alone (the mouse and the keys)
    const bool  alone = rl_opt.tab == OT_CONTROLS && !secs.empty();
    const int   n  = alone ? (w >= 1300 ? 3 : w >= 860 ? 2 : 1) : w < 1100 ? 1 : 2;
    const float lw = floorf(w * 0.55f + 0.5f);

    lines.clear();
    for (int c = 0; c < n; c++) {
        const float l = alone ? floorf(w * c / n + 0.5f) : c ? lw : 0, r = alone ? floorf(w * (c + 1) / n + 0.5f) : c || n == 1 ? w : lw;

        cx[c] = x + l + RL_PAD;
        cw[c] = r - l - 2 * RL_PAD;
        if (c) {
            lines.push_back(x + l);
        }
    }
    out.clear();
    for (int col = 0; col < 3; col++) {
        for (const int s : secs) {
            const int c = Q_min(col, n - 1), f = rl_opts[s].flags;

            if ((n == 2 && (f & OF_LEFT2) ? 0 : f & OF_THIRD ? 2 : f & OF_RIGHT ? 1 : 0) != col) {
                continue;
            }
            cy[c] += cy[c] > 0 ? 14 : 0;
            out.push_back({s, -1, cx[c], cy[c], cw[c], 40});
            cy[c] += 40;
            for (int i = s + 1; i < RL_OPTS && rl_opts[i].kind != OC_SECTION; i++) {
                const float h = rl_opts[i].kind == OC_PREVIEW ? floorf(Q_min(cw[c] * 0.32f, 220.0f)) + 12 : 66 + 34 * (RL_SwatchRows(i) - 1);

                for (int a = rl_keys.from[i]; rl_opts[i].kind == OC_KEYS && a < rl_keys.to[i]; a++) {
                    out.push_back({i, a, cx[c], cy[c], cw[c], 44});
                    cy[c] += 44;
                }
                if (rl_opts[i].kind != OC_KEYS && RL_OptShown(i)) {
                    out.push_back({i, -1, cx[c], cy[c], cw[c], h});
                    cy[c] += h;
                }
            }
        }
    }
    return Q_max(cy[0], Q_max(cy[1], cy[2]));
}

static const char *RL_OptValue(int i)
{
    return RL_Latched(rl_opt.cv[i]);
}

static float RL_OptNumber(int i)
{
    return (float)atof(RL_OptValue(i));
}

// a value is a choice's: as numbers when both are ("1", "1.0"), else as words
static bool RL_OptIs(const char *value, const char *choice)
{
    char *e1, *e2;
    const double a = strtod(value, &e1), b = strtod(choice, &e2);

    return *value && *choice && !*e1 && !*e2 ? a == b : !Q_stricmp(value, choice);
}

// a switch on
static bool RL_OptOn(int i)
{
    const int flags = rl_opts[i].flags;

    return flags & OF_NEG ? atof(RL_OptValue(i)) < 0 : (atoi(RL_OptValue(i)) != 0) != ((flags & OF_NOT) != 0);
}

// the choice a setting has (segments, a cycle), -1 none of them
static int RL_OptChoice(int i)
{
    const rlOpt_t&    o = rl_opts[i];
    const char *const value = RL_OptValue(i);

    if (o.flags & OF_DISPLAY) {
        return atoi(value) ? 0 : atoi(RL_Latched(RL_OptOther("r_noborder"))) ? 2 : 1;
    }
    if (o.flags & OF_RES) {
        const std::string own = va("%d x %d", atoi(RL_Latched(RL_OptOther("r_customwidth"))), atoi(RL_Latched(RL_OptOther("r_customheight"))));

        const auto it = std::find(rl_opt.sizes.begin() + 1, rl_opt.sizes.end(), own);

        // (a numbered mode: none of them)
        return atoi(value) == -2 ? 0 : atoi(value) == -1 && it != rl_opt.sizes.end() ? (int)(it - rl_opt.sizes.begin()) : -1;
    }
    if (o.flags & OF_FONT) {
        return (int)(RL_FindFont(value) - rl_fonts);
    }
    if (o.flags & OF_ANISO && !atoi(value)) {
        return 0;
    }
    for (int k = 0; o.choices[k]; k++) {
        const char *const is = o.flags & OF_ANISO ? RL_Latched(RL_OptOther("r_ext_max_anisotropy")) : value;

        if ((o.flags & OF_ANISO) ? k && RL_OptIs(is, o.values[k]) : o.values ? RL_OptIs(is, o.values[k]) : RL_OptIs(is, va("%d", k))) {
            return k;
        }
    }
    return -1;
}

// the choices a setting has
static int RL_OptChoices(int i)
{
    int n = 0;

    if (rl_opts[i].flags & OF_RES) {
        return (int)rl_opt.sizes.size();
    }
    if (rl_opts[i].flags & OF_FONT) {
        return rl_numFonts;
    }
    while (rl_opts[i].choices[n]) {
        n++;
    }
    return n;
}

// a choice's label
static const char *RL_OptLabel(int i, int k)
{
    if (rl_opts[i].flags & OF_RES) {
        return k >= 0 ? rl_opt.sizes[k].c_str() : va("MODE %d", atoi(RL_OptValue(i)));
    }
    if (rl_opts[i].flags & OF_FONT) {
        return k >= 0 ? rl_fonts[k].name : "";
    }
    return k >= 0 ? rl_opts[i].choices[k] : "";
}

static void RL_OptSet(int i, const char *value)
{
    const rlOpt_t& o = rl_opts[i];

    if (!strcmp(RL_OptValue(i), value)) {
        return;
    }
    RL_OptCvarSet(o.cvar, value);
}

// a slider set to a value (snapped to its steps, within its ends)
static void RL_OptSlide(int i, float value)
{
    const rlOpt_t& o = rl_opts[i];

    value = Q_max(o.lo, Q_min(o.hi, value));
    RL_OptSet(i, va("%g", o.lo + floorf((value - o.lo) / o.step + 0.5f) * o.step));
}

// a choice picked (segments, a cycle): its value, or the cvars it stands for
static void RL_OptPick(int i, int k)
{
    const rlOpt_t& o = rl_opts[i];

    if (o.flags & OF_DISPLAY) {
        RL_OptCvarSet("r_fullscreen", k ? "0" : "1");
        RL_OptCvarSet("r_noborder", k == 2 ? "1" : "0");
    } else if (o.flags & OF_ANISO) {
        RL_OptCvarSet("r_ext_texture_filter_anisotropic", k ? "1" : "0");
        if (k) {
            RL_OptCvarSet("r_ext_max_anisotropy", o.values[k]);
        }
    } else if (o.flags & OF_RES) {
        int w, h;

        RL_OptCvarSet("r_mode", k ? "-1" : "-2");
        if (k && sscanf(rl_opt.sizes[k].c_str(), "%d x %d", &w, &h) == 2) {
            RL_OptCvarSet("r_customwidth", va("%d", w));
            RL_OptCvarSet("r_customheight", va("%d", h));
        }
    } else if (o.flags & OF_FONT) {
        RL_OptSet(i, rl_fonts[k].value);
    } else {
        RL_OptSet(i, o.values ? o.values[k] : va("%d", k));
    }
}

// a switch turned over (inverted: the sign)
static void RL_OptFlip(int i)
{
    if (rl_opts[i].flags & OF_NEG) {
        RL_OptSet(i, va("%g", -atof(RL_OptValue(i))));
    } else {
        RL_OptSet(i, atoi(RL_OptValue(i)) ? "0" : "1");
    }
}

// a slider's value typed in, set
static void RL_OptTyped(void)
{
    if (rl_mm.edit == F_VALUE && !rl_opt.value.empty()) {
        const float typed = (float)atof(rl_opt.value.c_str());

        RL_OptSlide(rl_opt.row, rl_opts[rl_opt.row].flags & OF_PCT ? typed / 100 : typed);
    }
    rl_mm.edit = F_NONE;
}

// a slider's value as shown: a percentage, or the decimals its steps have
static const char *RL_OptShow(int i)
{
    const rlOpt_t& o = rl_opts[i];
    const float    value = RL_OptNumber(i);

    if (o.flags & OF_PCT) {
        return va("%d%%", (int)floorf(value * 100 + 0.5f));
    }
    return va("%.*f", o.step < 0.01f ? 3 : o.step < 0.1f ? 2 : o.step < 1 ? 1 : 0, value);
}

// the crosshair code typed (none: the clipboard's) imported by crosshair_import, which keeps it in cg_crosshair_code
static void RL_OptImport(void)
{
    cvar_t   *kept = Cvar_Get("cg_crosshair_code", "", CVAR_ARCHIVE);
    const int was  = kept->modificationCount;

    Cmd_ExecuteString(va("crosshair_import %s", rl_opt.code.c_str()));
    rl_opt.note = kept->modificationCount != was || (!rl_opt.code.empty() && !strcmp(kept->string, rl_opt.code.c_str())) ? "CROSSHAIR IMPORTED"
                                                                                                                       : "NOT A CS2 CROSSHAIR CODE";
    rl_mm.edit = F_NONE;
}

// the boxes synced to the bindings when they changed (key_serial): a box's key bound to another command let go, an
// empty box given another key bound to its action
static void RL_KeysSync(void)
{
    if (rl_keys.synced == key_serial) {
        return;
    }
    rl_keys.synced = key_serial;
    for (rlAct_t& a : rl_keys.acts) {
        for (int& k : a.key) {
            if (k >= 0 && (!keys[k].binding || Q_stricmp(keys[k].binding, a.cmd.c_str()))) {
                k = -1;
            }
        }
    }
    for (int k = 0; k < MAX_KEYS; k++) {
        const char *on = keys[k].binding;

        for (size_t i = 0; on && *on && i < rl_keys.acts.size(); i++) {
            rlAct_t& a = rl_keys.acts[i];

            if (!Q_stricmp(on, a.cmd.c_str())) {
                if (a.key[0] != k && a.key[1] != k && (a.key[0] < 0 || a.key[1] < 0)) {
                    a.key[a.key[0] < 0 ? 0 : 1] = k;
                }
                break;
            }
        }
    }
}

// a key's name as the boxes show it ("W", "MOUSE1")
static const char *RL_KeyName(int key)
{
    static char name[32];

    Q_strncpyz(name, Key_KeynumToString(key), sizeof(name));
    return Q_strupr(name);
}

// the box waiting for a key no more: the keys to the menus as before
static void RL_KeyStop(void)
{
    rl_keys.wait = -1;
    uWinMan.SetBindActive(NULL);
}

// a key bound to a box (the key it had let go)
static void RL_KeyBind(int box, int key)
{
    rlAct_t& a = rl_keys.acts[box / 2];
    int&     k = a.key[box % 2];

    if (k >= 0 && k != key) {
        Key_SetBinding(k, "");
    }
    Key_SetBinding(key, a.cmd.c_str());
    k = key;
}

// the key pressed for the box waiting: Esc (and Pause, as for the stock list) none, Backspace or Delete the box
// cleared, the action's other box's key the two swapped; one another command has, asked about first (KEY IN USE)
static void RL_KeyPressed(int key)
{
    const int   box   = rl_keys.wait;
    rlAct_t&    a     = rl_keys.acts[box / 2];
    int&        k     = a.key[box % 2];
    int&        other = a.key[1 - box % 2];
    const char *on    = keys[key].binding;

    RL_KeyStop();
    RL_KeysSync();
    if (key == K_ESCAPE || key == K_PAUSE) {
        return;
    }
    if (key == K_BACKSPACE || key == K_DEL) {
        if (k >= 0) {
            Key_SetBinding(k, "");
        }
        k = -1;
    } else if (key == other) {
        std::swap(k, other);
    } else if (on && *on && Q_stricmp(on, a.cmd.c_str())) {
        rl_keys.ask[0] = box;
        rl_keys.ask[1] = key;
        rl_mm.confirm  = ID_KEYMOVE;
        menuManager.ShowMenu("reloaded_confirm");
    } else {
        RL_KeyBind(box, key);
    }
}

// what KEY IN USE asks: "CTRL is on Crouch. Move it to Jump/Stand Up?" (a command not listed as it is)
static std::string RL_KeyQuestion(void)
{
    const char *on   = keys[rl_keys.ask[1]].binding;
    std::string what = on ? on : "";

    for (const rlAct_t& a : rl_keys.acts) {
        if (!Q_stricmp(a.cmd.c_str(), what.c_str())) {
            what = a.name;
            break;
        }
    }
    return std::string(RL_KeyName(rl_keys.ask[1])) + " is on " + what + ". Move it to " + rl_keys.acts[rl_keys.ask[0] / 2].name + "?";
}

// DEFAULTS' keys: the actions shown bound as the game's default.cfg binds them (their keys let go first; other
// commands' keys stay, but for those it binds)
static void RL_KeysDefaults(void)
{
    std::vector<int> shown;
    const auto       is = [&shown](const char *cmd) {
        for (const int a : shown) {
            if (!Q_stricmp(rl_keys.acts[a].cmd.c_str(), cmd)) {
                return true;
            }
        }
        return false;
    };

    for (int i = 0; i < RL_OPTS; i++) {
        for (int a = rl_keys.from[i]; rl_opts[i].kind == OC_KEYS && RL_OptShown(i) && a < rl_keys.to[i]; a++) {
            shown.push_back(a);
            rl_keys.acts[a].key[0] = rl_keys.acts[a].key[1] = -1;
        }
    }
    if (shown.empty()) {
        return;
    }
    for (int k = 0; k < MAX_KEYS; k++) {
        if (keys[k].binding && is(keys[k].binding)) {
            Key_SetBinding(k, "");
        }
    }
    RL_EachLine("default.cfg", [&is] {
        if (Cmd_Argc() >= 3 && !Q_stricmp(Cmd_Argv(0), "bind") && is(Cmd_ArgsFrom(2))) {
            Key_SetBinding(Key_StringToKeynum(Cmd_Argv(1)), Cmd_ArgsFrom(2));
        }
    });
}

// DEFAULTS: the settings shown back to their defaults (and the cvars a choice stands for with them), the keys shown too
static void RL_OptDefaults(void)
{
    for (int i = 0; i < RL_OPTS; i++) {
        const int flags = rl_opts[i].flags;

        if (!rl_opts[i].cvar || !RL_OptShown(i)) {
            continue;
        }
        Cvar_Reset(rl_opts[i].cvar);
        if (flags & (OF_DISPLAY | OF_ANISO)) {
            Cvar_Reset(flags & OF_DISPLAY ? "r_noborder" : "r_ext_max_anisotropy");
        }
    }
    RL_KeysDefaults();
    rl_opt.note = "DEFAULTS SET";
}

/*
----------------
The controls (a row 66 high, its control's middle 32 down): Omaha's
slider (a track 190 across, the part to the value brass, a knob, the value
in a box 56 wide that takes a typed value), its switch (56 by 28), the
swatches (28 across, 6 apart, in rows of the setting's step, the one set
framed)
----------------
*/
static void RL_Slider(UIReloadedMenu *m, const rlView_t& v, int i, float x, float y, int id)
{
    const rlOpt_t& o    = rl_opts[i];
    const float    frac = Q_max(0.0f, Q_min(1.0f, (RL_OptNumber(i) - o.lo) / (o.hi - o.lo))), tw = 190, bx = x + 204;
    const bool     hot = m->Hot(id) || rl_opt.drag == id, typing = rl_mm.edit == F_VALUE && rl_opt.row == i;

    RL_Box(v, x, y + 12, tw, 4, RL_Rgb(255, 255, 255, 40));
    RL_Box(v, x, y + 12, tw * frac, 4, RL_BRASS);
    RL_Round(v, x + tw * frac - 8, y + 6, 16, 16, 8, hot ? RL_BRASS : RL_TEXT);
    m->Hit(v, x, y, tw, 28, id);
    // the value
    RL_Panel(v, bx, y, 56, 28, RL_Rgb(0, 0, 0, typing ? 160 : 120));
    if (typing) {
        const float tw2 = RL_Text(v, bx + 6, y + 14, rl_opt.value.c_str(), 16, RL_TEXT, false, RL_LEFT, 0, RL_MID);

        if ((cls.realtime / 530) % 2 == 0) {
            RL_Box(v, bx + 6 + tw2 + 1, y + 4, 2, 20, RL_BRASS);
        }
    } else {
        RL_Text(v, bx + 28, y + 14, RL_OptShow(i), 16, m->Hot(id + 1) ? RL_BRASS : RL_TEXT, false, RL_CENTER, 0, RL_MID);
    }
    m->Hit(v, bx, y, 56, 28, id + 1);
}

// Omaha's cycle: the choice between two arrows (the left one back, the rest on), 260 by 32
static void RL_Cycle(UIReloadedMenu *m, const rlView_t& v, float x, float y, const char *label, int id)
{
    RL_Panel(v, x, y, 260, 32, RL_Rgb(0, 0, 0, 120));
    for (int k = 0; k < 2; k++) {
        const float ax = k ? x + 260 - 32 : x;
        const int   x0 = RL_P(v, ax + 12), y0 = RL_P(v, y + 10);

        if (m->Hot(id + k)) {
            RL_Box(v, ax + 1, y + 1, 30, 30, RL_Rgb(255, 255, 255, 18));
        }
        RL_Shape(x0, y0, RL_P(v, ax + 20) - x0, RL_P(v, y + 22) - y0, k ? "poly 1 1 1 1 0 0 1 0.5 0 1" : "poly 1 1 1 1 1 0 1 1 0 0.5",
                 m->Hot(id + k) ? RL_BRASS : RL_MUTED, v.s);
    }
    RL_Text(v, x + 130, y + 16, label, 16, RL_TEXT, false, RL_CENTER, 0, RL_MID, 260 - 72);
    m->Hit(v, x, y, 32, 32, id);
    m->Hit(v, x + 32, y, 228, 32, id + 1);
}

static void RL_Toggle(const rlView_t& v, float x, float y, bool on, bool hot)
{
    RL_Round(v, x, y, 56, 28, 14, on ? (hot ? RL_BRASS_HOT : RL_BRASS) : RL_Rgb(255, 255, 255, hot ? 64 : 40));
    RL_Round(v, x + (on ? 31 : 3), y + 3, 22, 22, 11, on ? RL_INK : RL_Rgb(220, 216, 206));
}

static void RL_Swatches(UIReloadedMenu *m, const rlView_t& v, int i, float right, float top, int id)
{
    const char *const *c = rl_opts[i].choices;
    const char        *set = RL_OptValue(i);
    const int          n = RL_OptChoices(i), across = rl_opts[i].step > 0 ? (int)rl_opts[i].step : n;

    set += *set == '#';
    for (int k = 0; k < n; k++) {
        const float        x   = right - (across - k % across) * 34 + 6, y = top + k / across * 34;
        const unsigned int rgb = Com_HexColor(c[k], 0);

        // (a faint edge: black shows on the panel)
        RL_Round(v, x, y, 28, 28, 3, RL_Rgb(rgb >> 16, (rgb >> 8) & 0xFF, rgb & 0xFF));
        RL_RoundFrame(v, x, y, 28, 28, 3, 1, RL_Rgb(255, 255, 255, 40));
        if (!Q_stricmp(set, c[k]) || m->Hot(id + k)) {
            RL_RoundFrame(v, x - 3, y - 3, 34, 34, 5, 2, Q_stricmp(set, c[k]) ? RL_Rgb(255, 255, 255, 110) : RL_TEXT);
        }
        m->Hit(v, x, y, 28, 28, id + k);
    }
}

// the crosshair as CG_UpdateCrosshair lays it out at this screen's height, a pixel of it 2 here, centred in x y w h
static void RL_Crosshair(const rlView_t& v, float x, float y, float w, float h)
{
    static cvar_t *outline[2];
    const int      cx = RL_P(v, x + w / 2), cy = RL_P(v, y + h / 2);
    int            px[3], piece[5][4], n = 0;

    if (!outline[0]) {
        outline[0] = Cvar_Get("cg_crosshair_outline_color", "000000", CVAR_ARCHIVE);
        outline[1] = Cvar_Get("cg_crosshair_outline_alpha", "255", CVAR_ARCHIVE);
    }
    UI_DrawDep(outline[0]);
    UI_DrawDep(outline[1]);
    UI_DrawDep(Cvar_Get("cg_crosshair_alpha", "255", CVAR_ARCHIVE));
    // the length, the thickness, the gap in pixels (CG_CrosshairPixels)
    for (int k = 0; k < 3; k++) {
        const float value = Cvar_VariableValue(k == 0 ? "cg_crosshair_length" : k == 1 ? "cg_crosshair_thickness" : "cg_crosshair_gap");

        px[k] = value > 0.0f ? Q_max((int)(Q_min(value, 4096.0f) * uid.vidHeight / 1080.0f + 0.5f), 1) : 0;
    }
    {
        const int len = px[0], t = px[1], gap = px[2], before = (t + 1) / 2, odd = t & 1;
        const int pieces[5][4] = {
            {-gap - len, -before, len, t}, {gap - odd, -before, len, t}, {-before, gap - odd, t, len}, {-before, -gap - len, t, len},
            {-before, -before, t, t}
        };

        for (int k = 0; k < 5; k++) {
            const bool on = k < 4 ? t && len && (k < 3 || !Cvar_VariableIntegerValue("cg_crosshair_t")) : t && Cvar_VariableIntegerValue("cg_crosshair_dot");

            if (on) {
                memcpy(piece[n++], pieces[k], sizeof(pieces[k]));
            }
        }
    }
    {
        const int          mode = Q_max(0, Q_min(2, Cvar_VariableIntegerValue("cg_crosshair_outline")));
        const unsigned int rgb = Com_HexColor(Cvar_VariableString("cg_crosshair_color"), 0xFFFFFF);
        const unsigned int ol  = Com_HexColor(outline[0]->string, 0);
        const rlColor_t    c   = RL_Rgb(rgb >> 16, (rgb >> 8) & 0xFF, rgb & 0xFF, Q_max(0, Q_min(255, Cvar_VariableIntegerValue("cg_crosshair_alpha"))));
        const rlColor_t    oc  = RL_Rgb(ol >> 16, (ol >> 8) & 0xFF, ol & 0xFF, Q_max(0, Q_min(255, outline[1]->integer)));

        // the outline a pixel round each piece (half: above and left), then the pieces
        for (int k = 0; mode && k < n; k++) {
            const int *p = piece[k];

            RL_BoxPx(cx + 2 * (p[0] - 1), cy + 2 * (p[1] - 1), cx + 2 * (p[0] + p[2] + (mode == 1)), cy + 2 * p[1], oc);
            RL_BoxPx(cx + 2 * (p[0] - 1), cy + 2 * p[1], cx + 2 * p[0], cy + 2 * (p[1] + p[3]), oc);
            if (mode == 1) {
                RL_BoxPx(cx + 2 * (p[0] - 1), cy + 2 * (p[1] + p[3]), cx + 2 * (p[0] + p[2] + 1), cy + 2 * (p[1] + p[3] + 1), oc);
                RL_BoxPx(cx + 2 * (p[0] + p[2]), cy + 2 * p[1], cx + 2 * (p[0] + p[2] + 1), cy + 2 * (p[1] + p[3]), oc);
            }
        }
        for (int k = 0; k < n; k++) {
            const int *p = piece[k];

            RL_BoxPx(cx + 2 * p[0], cy + 2 * p[1], cx + 2 * (p[0] + p[2]), cy + 2 * (p[1] + p[3]), c);
        }
    }
}

// a row of keys' box (the primary 0, the alternate 1): its left edge, 110 wide, 6 between the two at the right edge
static float RL_KeyX(float x, float w, int box)
{
    return x + w - (2 - box) * 110 - (1 - box) * 6;
}

// an action's row (44 high): its name, its boxes (32 high; waiting: brass, the row marked), a line under it
static void RL_KeyRow(UIReloadedMenu *m, const rlView_t& v, int a, float x, float y, float w, float h)
{
    const rlAct_t& act = rl_keys.acts[a];
    const float    mid = y + h / 2;

    if (rl_keys.wait >= 0 && rl_keys.wait / 2 == a) {
        RL_Box(v, x - RL_PAD + 2, y, w + 2 * RL_PAD - 3, h - 1, RL_SEL);
        RL_Box(v, x - RL_PAD + 2, y, 3, h - 1, RL_BRASS);
    }
    RL_Text(v, x, mid, act.name.c_str(), 19, RL_TEXT, false, RL_LEFT, 0, RL_MID, RL_KeyX(x, w, 0) - x - 18);
    for (int k = 0; k < 2; k++) {
        const float bx = RL_KeyX(x, w, k);
        const int   id = ID_KEY + a * 2 + k;
        const bool  wait = rl_keys.wait == a * 2 + k, hot = m->Hot(id);

        RL_Box(v, bx, mid - 16, 110, 32, RL_Rgb(0, 0, 0, wait ? 160 : 120));
        RL_Frame(v, bx, mid - 16, 110, 32, wait ? RL_P(v, 2) : 1, wait ? RL_BRASS : hot ? RL_Rgb(255, 255, 255, 70) : RL_LINE);
        if (wait) {
            RL_Text(v, bx + 55, mid, "PRESS A KEY", 13, RL_BRASS, true, RL_CENTER, 1.2f, RL_MID);
        } else {
            RL_Text(v, bx + 55, mid, act.key[k] >= 0 ? RL_KeyName(act.key[k]) : "\x97", 16, hot ? RL_BRASS : act.key[k] >= 0 ? RL_TEXT : RL_FAINT,
                    false, RL_CENTER, 0, RL_MID, 98);
        }
        m->Hit(v, bx, mid - 16, 110, 32, id);
    }
    RL_Box(v, x, y + h - 2, w, 1, RL_LINE);
}

// a setting's row: its label (and its description), its control at the right edge, a line under it
static void RL_OptRow(UIReloadedMenu *m, const rlView_t& v, int i, float x, float y, float w, float h)
{
    const rlOpt_t& o   = rl_opts[i];
    const int      id  = ID_OPT + i * RL_OPT_IDS;
    const float    mid = y + 32, right = x + w;

    if (o.kind == OC_SECTION) {
        // (keys: their boxes' names over them)
        RL_Text(v, x, y + 18, o.label, 15, RL_BRASS, true, RL_LEFT, 2.6f, RL_MID);
        for (int k = 0; rl_opts[i + 1].kind == OC_KEYS && k < 2; k++) {
            RL_Text(v, RL_KeyX(x, w, k) + 55, y + 18, k ? "ALTERNATE" : "PRIMARY", 12, RL_MUTED, true, RL_CENTER, 1.4f, RL_MID);
        }
        return;
    }
    if (o.kind == OC_PREVIEW) {
        RL_Frame(v, x, y, w, h - 12, 1, RL_LINE);
        RL_Crosshair(v, x, y, w, h - 12);
        RL_Text(v, x + 12, y + h - 12 - 18, "PREVIEW, 2X", 12, RL_Rgb(255, 255, 255, 150), true, RL_LEFT, 1.6f, RL_MID);
        return;
    }
    float cw = o.kind == OC_TOGGLE ? 56 : o.kind == OC_CODE ? 364 : 260; // the control's width

    if (o.cvar) {
        UI_DrawDep(rl_opt.cv[i]);
    }
    if (o.kind == OC_SEG || o.kind == OC_SWATCH) {
        cw = -4;
        for (int k = 0; o.choices[k] && (o.kind != OC_SWATCH || o.step <= 0 || k < o.step); k++) {
            cw += o.kind == OC_SWATCH ? 34 : RL_TextW(v, o.choices[k], 15, true, 1.6f) + 40;
        }
    }
    RL_Text(v, x, o.desc[0] ? y + 21 : mid, o.label, 19, RL_TEXT, false, RL_LEFT, 0, RL_MID, w - cw - 24);
    if (o.desc[0]) {
        RL_Text(v, x, y + 45, o.desc, 14, RL_MUTED, false, RL_LEFT, 0, RL_MID, w - cw - 24);
    }
    switch (o.kind) {
    case OC_SEG:
        RL_Segments(m, v, right, mid - 16, 32, o.choices, RL_OptChoices(i), RL_OptChoice(i), id, true);
        break;
    case OC_CYCLE:
        {
            // (a font: its name in it)
            rlView_t fv = v;

            fv.font = o.flags & OF_FONT ? &rl_fonts[Q_max(0, RL_OptChoice(i))] : NULL;
            RL_Cycle(m, fv, right - 260, mid - 16, RL_OptLabel(i, RL_OptChoice(i)), id);
        }
        break;
    case OC_SLIDER:
        RL_Slider(m, v, i, right - 260, mid - 14, id);
        break;
    case OC_TOGGLE:
        RL_Toggle(v, right - 56, mid - 14, RL_OptOn(i), m->Hot(id));
        m->Hit(v, right - 56, mid - 14, 56, 28, id);
        break;
    case OC_SWATCH:
        RL_Swatches(m, v, i, right, mid - 14, id);
        break;
    case OC_CODE:
        RL_Field(v, right - 364, mid - 18, 260, 36, rl_opt.code, "CS\x85, or the clipboard's", rl_mm.edit == F_CODE, m->Hot(id));
        m->Hit(v, right - 364, mid - 18, 260, 36, id);
        RL_Button(m, v, right - 100, mid - 18, 100, 36, "IMPORT", false, id + 1);
        break;
    }
    RL_Box(v, x, y + h - 2, w, 1, RL_LINE);
}

// the restarts APPLY waits to make: 1 the renderer's (a setting it took otherwise), 2 the sound's (s_khz, the last)
static int RL_OptPending(void)
{
    int pending = 0;

    for (int k = 0; k < RL_RESTARTS; k++) {
        const cvar_t *cv = RL_OptOther(rl_optRestart[k]);

        if (cv->latchedString || rl_opt.applied[k] != cv->string) {
            pending |= k < RL_RESTARTS - 1 ? 1 : 2;
        }
    }
    return pending;
}

// APPLY: the restarts made, the settings as they will be taken
static void RL_OptApply(void)
{
    const int pending = RL_OptPending();

    if (!pending) {
        return;
    }
    for (int k = 0; k < RL_RESTARTS; k++) {
        rl_opt.applied[k] = RL_Latched(RL_OptOther(rl_optRestart[k]));
    }
    Cbuf_AddText(pending & 1 ? "vid_restart\n" : "");
    Cbuf_AddText(pending & 2 ? "snd_restart\n" : "");
}

/*
================
OPTIONS: the back layer the panel and, under the crosshair's preview, the
photos as they are under the panel (over the game: the game, the panel
round it); the front the rest, BACK before DEFAULTS over the game. The
rows wholly in the content (between the head and the foot row) drawn
================
*/
static void RL_Options(UIReloadedMenu *m, const rlView_t& v, float x, float y, float w, float h)
{
    std::vector<rlOptPlace_t>& places = rl_optPlaces;
    const float                top = y + 48 + RL_PAD, foot = y + h - RL_FOOT, band = foot - RL_PAD - top;

    // (again after a renderer's start: the sizes offered, the settings it took)
    if (!rl_opt.ready || rl_opt.refSeq != uii.GetRefSequence()) {
        RL_OptRead();
    }
    std::vector<float> lines;
    const float        height = RL_OptLayout(x, w, places, lines);

    rl_opt.most = 0;
    for (const rlOptPlace_t& p : places) {
        if (height > band && p.y >= height - band && (!rl_opt.most || p.y < rl_opt.most)) {
            rl_opt.most = p.y;
        }
    }
    // (the scroll as far as the layout goes, here: both layers alike)
    const float scroll = Q_max(0.0f, Q_min(rl_opt.scroll, rl_opt.most));
    if (m->m_back) {
        const rlOptPlace_t *pv = NULL; // the preview, shown

        for (const rlOptPlace_t& p : places) {
            if (rl_opts[p.opt].kind == OC_PREVIEW && p.y >= scroll && p.y + p.h <= scroll + band) {
                pv = &p;
            }
        }
        if (pv && m->m_screen == SCR_OPTIONS) {
            const int x0 = RL_P(v, x), y0 = RL_P(v, y), x1 = RL_P(v, x + w), y1 = RL_P(v, y + h);
            const int px0 = RL_P(v, pv->x), py0 = RL_P(v, top + pv->y - scroll), px1 = RL_P(v, pv->x + pv->w);
            const int py1 = RL_P(v, top + pv->y - scroll + pv->h - 12);

            RL_BoxPx(x0, y0, x1, py0, RL_PANEL);
            RL_BoxPx(x0, py1, x1, y1, RL_PANEL);
            RL_BoxPx(x0, py0, px0, py1, RL_PANEL);
            RL_BoxPx(px1, py0, x1, py1, RL_PANEL);
            RL_Frame(v, x, y, w, h, 1, RL_LINE);
            return;
        }
        RL_Panel(v, x, y, w, h, RL_PANEL);
        if (pv) {
            RL_BgDraw(v, pv->x, top + pv->y - scroll, pv->w, pv->h - 12);
        }
        return;
    }
    RL_Head(v, x, y, w, 48);
    RL_Tabs(m, v, x + 4, y, rl_optTabs, OT_NUM, rl_opt.tab, ID_OTAB, 17, 22);
    for (const float lx : lines) {
        RL_Box(v, lx, y + 49, 1, foot - (y + 49), RL_LINE);
    }
    RL_KeysSync();
    for (size_t k = 0; k < places.size(); k++) {
        const rlOptPlace_t& p = places[k];

        // (a section's title with its first row, never alone at the foot)
        if (p.y < scroll || p.y + p.h > scroll + band
            || (rl_opts[p.opt].kind == OC_SECTION && places[k + 1].y + places[k + 1].h > scroll + band)) {
            continue;
        }
        if (p.act >= 0) {
            RL_KeyRow(m, v, p.act, p.x, top + p.y - scroll, p.w, p.h);
        } else {
            RL_OptRow(m, v, p.opt, p.x, top + p.y - scroll, p.w, p.h);
        }
    }
    if (places.empty()) {
        RL_Text(v, x + RL_PAD, top + 20, "No setting matches.", 17, RL_MUTED, false, RL_LEFT, 0, RL_MID);
    } else if (height > band) {
        RL_ScrollBar(v, x + w, top, band, scroll, band, height);
    }
    {
        const float by = RL_FootRow(v, x, foot, w);
        const int   pending = RL_OptPending();
        const char *restart[4] = {rl_opt.note.c_str(), "APPLY RESTARTS THE VIDEO", "APPLY RESTARTS THE SOUND", "APPLY RESTARTS THE VIDEO AND THE SOUND"};
        const char *say = rl_keys.wait >= 0 ? "PRESS A KEY  \xb7  ESC CANCELS  \xb7  BACKSPACE CLEARS" : restart[pending];
        const float dx  = m->m_screen == SCR_OPTIONS ? 150 + 8 : 0; // (BACK's room)

        if (dx) {
            RL_Button(m, v, x + RL_PAD, by, 150, RL_CTRL, "BACK", false, ID_BACK);
        }
        RL_Button(m, v, x + RL_PAD + dx, by, 150, RL_CTRL, "DEFAULTS", false, ID_DEFAULTS);
        // (between the buttons)
        RL_Text(v, x + w / 2 + dx / 2, foot + RL_FOOT / 2, say, 15, RL_BRASS, true, RL_CENTER, 2, RL_MID, w - 2 * (RL_PAD + 190 + 16) - dx);
        RL_Button(m, v, x + w - RL_PAD - 190, by, 190, RL_CTRL, "APPLY", true, ID_OAPPLY, !pending);
    }
}

// the wheel: the next setting's top (a row's or a section's) at the content's, or the one before
static void RL_OptScroll(bool down)
{
    float to = down ? rl_opt.most : 0;

    for (const rlOptPlace_t& p : rl_optPlaces) {
        if (down ? p.y > rl_opt.scroll && p.y < to : p.y < rl_opt.scroll && p.y > to) {
            to = p.y;
        }
    }
    rl_opt.scroll = to;
}

// a click on a setting's control (a slider's track is dragged: MainTick)
static void RL_OptChoose(int id)
{
    const int      i = (id - ID_OPT) / RL_OPT_IDS, part = (id - ID_OPT) % RL_OPT_IDS;
    const rlOpt_t& o = rl_opts[i];

    switch (o.kind) {
    case OC_SEG:
        RL_OptPick(i, part);
        break;
    case OC_CYCLE:
        {
            const int n = RL_OptChoices(i), k = RL_OptChoice(i);

            RL_OptPick(i, part ? (k + 1) % n : k <= 0 ? n - 1 : k - 1);
        }
        break;
    case OC_SWATCH:
        RL_OptSet(i, o.choices[part]);
        break;
    case OC_TOGGLE:
        RL_OptFlip(i);
        break;
    case OC_SLIDER:
        if (part == 1) {
            rl_mm.edit   = F_VALUE;
            rl_opt.row   = i;
            rl_opt.value.clear();
        }
        break;
    case OC_CODE:
        if (part) {
            RL_OptImport();
        } else {
            rl_mm.edit = F_CODE;
        }
        break;
    }
}

/*
================
VOTE: Spearhead's and Breakthrough's votes (their stock Esc menu's CALL
VOTE and VOTE, the cgame's vote menus), the page over the Esc menu
Options' is. The vote running, if one is: what for, its time left, how it
stands, YES and NO (as F1 and F2) until the player has voted. The
server's votes down the left (its callvote.cfg, sent when asked: gvo),
the one picked's choices on the right (a list's, the players for a
player's, a field for a number or a text); CALL VOTE (or a choice
clicked twice) calls it as the cgame's menus do, back in the game. The
stock vote menus' pushes show it (cl_uimenus.cpp)
================
*/

typedef struct {
    std::string              name;
    voteoptiontype_t         type;
    std::vector<std::string> choices; // a list's
} rlVote_t;

static struct {
    std::vector<rlVote_t> votes;   // the server's
    std::string           text;    // as its parts come
    bool                  got;     // for the level (cl.serverId) of
    int                   level;
    int                   askedAt;
    int                   pick;    // the vote picked, -1 none
    int                   choice;  // its choice: a list's place, a player's client; -1 none
    std::string           entry;   // a number's, a text's
    int                   top, ctop; // the lists' first shown
    int                   split;   // the columns' edge (pixels in the menu)
    int                   clickAt;
} rl_vote = {{}, "", false, 0, 0, -1, -1, "", 0, 0, 0, 0};

static int  rl_over;        // the page over the Esc menu (RL_OVER_*)
static bool rl_overPending; // asked for before the Esc menu was up: shown once it is (RL_OverTick)

void RL_VoteRead(int part, const char *text)
{
    if (!part) {
        rl_vote.text.clear();
    }
    if (rl_vote.text.size() >= MAX_VOTEOPTIONS_BUFFER_LENGTH) {
        return;
    }
    rl_vote.text += text;
    if (part < 2) {
        return;
    }
    rl_vote.text += "\n";
    // (none: asked too soon, asked again in a while)
    if (rl_vote.text != "\n") {
        VoteOptions      options;
        str              name, command;
        voteoptiontype_t type;

        std::replace(rl_vote.text.begin(), rl_vote.text.end(), '\x01', '"');
        options.SetupVoteOptions("ServerVoteOptions", rl_vote.text.length(), rl_vote.text.c_str());
        rl_vote.votes.clear();
        for (int i = 1; options.GetVoteOptionMainName(i, &name) && options.GetVoteOptionsMain(i, &command, &type); i++) {
            rlVote_t vote = {name.c_str(), type, {}};

            for (int k = 1; type == VOTE_OPTION_LIST && options.GetVoteOptionSubName(i, k, &name); k++) {
                vote.choices.push_back(name.c_str());
            }
            rl_vote.votes.push_back(vote);
        }
        rl_vote.got    = true;
        rl_vote.level  = cl.serverId;
        rl_vote.pick   = rl_vote.choice = -1;
        rl_vote.top    = rl_vote.ctop = 0;
        RL_Changed();
    }
    rl_vote.text.clear();
}

// the players a player's vote offers (the cgame's list: in client order, not the player himself for clientnotself)
static std::vector<std::pair<int, std::string>> RL_VotePlayers(voteoptiontype_t type)
{
    std::vector<std::pair<int, std::string>> out;
    const int                                self = cl.snap.valid ? cl.snap.ps.clientNum : clc.clientNum;

    for (int i = 0; i < MAX_CLIENTS; i++) {
        const char *name = Info_ValueForKey(CL_ConfigString(CS_PLAYERS + i), "name");

        if (name[0] && (type != VOTE_OPTION_CLIENT_NOT_SELF || i != self)) {
            out.emplace_back(i, name);
        }
    }
    return out;
}

// the vote picked, NULL none (none while one runs or voting is off)
static const rlVote_t *RL_VotePicked(void)
{
    if (!rl_vote.got || rl_vote.level != cl.serverId || rl_vote.pick < 0 || rl_vote.pick >= (int)rl_vote.votes.size()
        || atoi(CL_ConfigString(CS_VOTE_TIME)) || !Cvar_VariableIntegerValue("cg_allowvote")) {
        return NULL;
    }
    return &rl_vote.votes[rl_vote.pick];
}

// the vote picked can be called (its choice picked or typed)
static bool RL_VoteReady(void)
{
    const rlVote_t *vote = RL_VotePicked();

    if (!vote) {
        return false;
    }
    switch (vote->type) {
    case VOTE_NO_CHOICES:
        return true;
    case VOTE_OPTION_LIST:
        return rl_vote.choice >= 0 && rl_vote.choice < (int)vote->choices.size();
    case VOTE_OPTION_CLIENT:
    case VOTE_OPTION_CLIENT_NOT_SELF:
        return rl_vote.choice >= 0 && rl_vote.choice < MAX_CLIENTS && *Info_ValueForKey(CL_ConfigString(CS_PLAYERS + rl_vote.choice), "name");
    default:
        return !rl_vote.entry.empty() && rl_vote.entry != "-" && rl_vote.entry != ".";
    }
}

// the vote picked called (the server says what came of it), back in the game
static void RL_VoteCall(void)
{
    const rlVote_t *vote = RL_VotePicked();
    const int       n    = rl_vote.pick + 1;

    if (!RL_VoteReady()) {
        return;
    }
    Cbuf_AddText("popmenu 0\npopmenu 0\n");
    Cbuf_AddText(vote->type == VOTE_NO_CHOICES                                  ? va("callvote %d\n", n)
                 : vote->type == VOTE_OPTION_LIST || vote->type >= VOTE_OPTION_CLIENT ? va("callvote %d %d\n", n, rl_vote.choice + (vote->type == VOTE_OPTION_LIST))
                                                                                    : va("callvote %d \"%s\"\n", n, rl_vote.entry.c_str()));
}

// the vote page's state drawn (the vote running: its time's seconds, how it stands, voted)
static int RL_VoteKey(void)
{
    const int at = atoi(CL_ConfigString(CS_VOTE_TIME));

    return at ? (cl.serverTime - at) / 1000 * 2 + cl.snap.ps.voted : -1;
}

static void RL_VoteChoose(int id)
{
    RL_Changed();
    rl_mm.edit = id == ID_VFIELD && RL_VotePicked() ? F_VOTE : F_NONE;
    if (id == ID_BACK) {
        Cbuf_AddText("popmenu 0\n");
    } else if (id == ID_VYES || id == ID_VNO) {
        Cbuf_AddText(id == ID_VYES ? "popmenu 0\npopmenu 0\nvote y\n" : "popmenu 0\npopmenu 0\nvote n\n");
    } else if (id == ID_VCALL) {
        RL_VoteCall();
    } else if (id >= ID_VCHOICE) {
        // a choice picked; clicked again (a double click), called
        const bool again = rl_vote.choice == id - ID_VCHOICE && cls.realtime - rl_vote.clickAt < 400;

        rl_vote.choice  = id - ID_VCHOICE;
        rl_vote.clickAt = cls.realtime;
        if (again) {
            RL_VoteCall();
        }
    } else if (id >= ID_VOPT && id - ID_VOPT != rl_vote.pick) {
        // another vote: its choices from the top, a number's or a text's field typed in
        rl_vote.pick   = id - ID_VOPT;
        rl_vote.choice = -1;
        rl_vote.ctop   = 0;
        rl_vote.entry.clear();
        if (RL_VotePicked() && RL_VotePicked()->type >= VOTE_OPTION_TEXT && RL_VotePicked()->type <= VOTE_OPTION_FLOAT) {
            rl_mm.edit = F_VOTE;
        }
    }
}

// a row of the vote page's lists: its text, a caption on its right (NULL none); off: faint, not hit
static void RL_VoteRow(UIReloadedMenu *m, const rlView_t& v, float x, float y, float w, float h, const char *text, const char *caption,
                       bool sel, bool off, int id)
{
    const float cw = caption ? RL_TextW(v, caption, 13, true, 1.6f) + 16 : 0.0f;

    if (!off) {
        RL_RowMark(m, v, x, y, w, h, sel, id);
    }
    RL_Text(v, x + RL_PAD + 8, y + h / 2, text, 18, off ? RL_FAINT : sel ? RL_TEXT : RL_SOFT, sel, RL_LEFT, 0, RL_MID, w - 2 * RL_PAD - 8 - cw);
    if (caption) {
        RL_Text(v, x + w - RL_PAD, y + h / 2, caption, 13, RL_MUTED, true, RL_RIGHT, 1.6f, RL_MID);
    }
}

static void RL_Vote(UIReloadedMenu *m, const rlView_t& v, float x, float y, float w, float h)
{
    static const char *const tab = "VOTE";
    const int                at  = atoi(CL_ConfigString(CS_VOTE_TIME));
    const bool               on  = Cvar_VariableIntegerValue("cg_allowvote") != 0;
    const bool               got = rl_vote.got && rl_vote.level == cl.serverId;
    const rlVote_t          *pick = RL_VotePicked();
    const float              foot = y + h - RL_FOOT, lw = floorf(w * 0.40f + 0.5f), rh = 44;
    float                    top = y + 48;
    const char              *say = "";

    if (m->m_back) {
        RL_Panel(v, x, y, w, h, RL_PANEL);
        return;
    }
    RL_Head(v, x, y, w, 48);
    RL_Tabs(m, v, x + 4, y, &tab, 1, 0, ID_VTAB, 17, 22);
    // the vote running: what for, its time left, how it stands; YES and NO until voted
    if (at) {
        const float cx = x + RL_PAD, cy = top + RL_PAD, cw = w - 2 * RL_PAD, ch = 132, bx = cx + cw - 24 - 150;
        const int   left = Q_max(0, (30000 - (cl.serverTime - at)) / 1000 + 1);
        const float lx   = RL_Text(v, cx + 24, cy + 30, "VOTE IN PROGRESS", 14, RL_BRASS, true, RL_LEFT, 1.6f, RL_MID);

        RL_Box(v, cx, cy, cw, ch, RL_SEL);
        RL_Box(v, cx, cy, 3, ch, RL_BRASS);
        RL_Text(v, cx + 24 + lx + 16, cy + 30, va("%d S LEFT", left), 14, RL_MUTED, true, RL_LEFT, 1.6f, RL_MID);
        RL_Text(v, cx + 24, cy + 68, CL_ConfigString(CS_VOTE_STRING), 26, RL_TEXT, true, RL_LEFT, 0, RL_MID, cw - 24 - 360);
        RL_Text(v, cx + 24, cy + 104,
                va("YES  %s      NO  %s      UNDECIDED  %s", CL_ConfigString(CS_VOTE_YES), CL_ConfigString(CS_VOTE_NO), CL_ConfigString(CS_VOTE_UNDECIDED)),
                15, RL_SOFT, true, RL_LEFT, 1.6f, RL_MID);
        if (cl.snap.ps.voted) {
            RL_Text(v, cx + cw - 24, cy + ch / 2, "YOUR VOTE IS IN", 15, RL_BRASS, true, RL_RIGHT, 2, RL_MID);
        } else {
            RL_Button(m, v, bx - 8 - 150, cy + (ch - RL_CTRL) / 2, 150, RL_CTRL, "NO", false, ID_VNO);
            RL_Button(m, v, bx, cy + (ch - RL_CTRL) / 2, 150, RL_CTRL, "YES", true, ID_VYES);
        }
        top = cy + ch + RL_PAD;
        RL_Box(v, x, top, w, 1, RL_LINE);
        say = "ONE VOTE AT A TIME";
    }
    if (!on || !got) {
        RL_Text(v, x + RL_PAD + 8, top + 40, !on ? "Voting is off on this server." : "Asking the server for its votes\x85", 17, RL_MUTED, false, RL_LEFT, 0,
                RL_MID);
    } else {
        // the server's votes on the left, the one picked's choices on the right
        const float ly = top + 56, rx = x + lw;
        // (the rows a list shows)
        const int   n = (int)rl_vote.votes.size(), fit = Q_max(1, (int)((foot - ly) / rh));

        RL_Box(v, rx, top + 1, 1, foot - top - 1, RL_LINE);
        RL_Label(v, x + RL_PAD + 8, top + 28, "THE SERVER'S VOTES");
        rl_vote.top   = Q_max(0, Q_min(rl_vote.top, n - fit));
        rl_vote.split = RL_P(v, rx);
        for (int i = 0; i < fit && rl_vote.top + i < n; i++) {
            const rlVote_t& o = rl_vote.votes[rl_vote.top + i];
            // (what is picked for it, but a list's: its choices)
            const char     *caption = o.type >= VOTE_OPTION_CLIENT ? "A PLAYER"
                                    : o.type == VOTE_OPTION_TEXT   ? "A TEXT"
                                    : o.type == VOTE_OPTION_INTEGER || o.type == VOTE_OPTION_FLOAT ? "A NUMBER"
                                                                                                    : NULL;

            RL_VoteRow(m, v, x, ly + i * rh, lw, rh, o.name.c_str(), caption, rl_vote.pick == rl_vote.top + i && !at, at != 0, ID_VOPT + rl_vote.top + i);
        }
        if (n > fit) {
            RL_ScrollBar(v, rx, ly, foot - ly, rl_vote.top, fit, n);
        }
        if (pick) {
            const float rw = w - lw;
            const bool  typed = pick->type >= VOTE_OPTION_TEXT && pick->type <= VOTE_OPTION_FLOAT;

            RL_Label(v, rx + RL_PAD + 8, top + 28, RL_Upper(pick->name.c_str()).c_str());
            if (pick->type == VOTE_NO_CHOICES) {
                RL_Text(v, rx + RL_PAD + 8, ly + rh / 2, "Nothing to choose: CALL VOTE calls it.", 17, RL_MUTED, false, RL_LEFT, 0, RL_MID);
            } else if (typed) {
                const float fw = Q_min(360.0f, rw - 2 * RL_PAD - 16);

                RL_Field(v, rx + RL_PAD + 8, ly + 2, fw, 40, rl_vote.entry, pick->type == VOTE_OPTION_TEXT ? "Type it\x85" : "Type a number\x85",
                         rl_mm.edit == F_VOTE, m->Hot(ID_VFIELD));
                m->Hit(v, rx + RL_PAD + 8, ly + 2, fw, 40, ID_VFIELD);
            } else {
                // a list's choices, or the players
                const auto players = pick->type == VOTE_OPTION_LIST ? std::vector<std::pair<int, std::string>>() : RL_VotePlayers(pick->type);
                const int  cn      = pick->type == VOTE_OPTION_LIST ? (int)pick->choices.size() : (int)players.size();

                rl_vote.ctop = Q_max(0, Q_min(rl_vote.ctop, cn - fit));
                for (int i = 0; i < fit && rl_vote.ctop + i < cn; i++) {
                    const int k = rl_vote.ctop + i, c = pick->type == VOTE_OPTION_LIST ? k : players[k].first;

                    RL_VoteRow(m, v, rx - 1, ly + i * rh, rw + 1, rh, pick->type == VOTE_OPTION_LIST ? pick->choices[k].c_str() : players[k].second.c_str(),
                               NULL, rl_vote.choice == c, false, ID_VCHOICE + c);
                }
                if (!cn) {
                    RL_Text(v, rx + RL_PAD + 8, ly + rh / 2, "No one to choose.", 17, RL_MUTED, false, RL_LEFT, 0, RL_MID);
                } else if (cn > fit) {
                    RL_ScrollBar(v, x + w, ly, foot - ly, rl_vote.ctop, fit, cn);
                }
            }
        } else if (!at) {
            RL_Text(v, rx + RL_PAD + 8, ly + rh / 2, "Pick a vote on the left.", 17, RL_MUTED, false, RL_LEFT, 0, RL_MID);
        }
    }
    // the foot row: BACK, CALL VOTE
    {
        const float by    = RL_FootRow(v, x, foot, w);
        const bool  ready = RL_VoteReady();

        RL_Button(m, v, x + RL_PAD, by, 150, RL_CTRL, "BACK", false, ID_BACK);
        RL_Text(v, x + w / 2, foot + RL_FOOT / 2, say, 15, RL_BRASS, true, RL_CENTER, 2, RL_MID, w - 2 * (RL_PAD + 190 + 16));
        RL_Button(m, v, x + w - RL_PAD - 190, by, 190, RL_CTRL, "CALL VOTE", true, ID_VCALL, !ready);
    }
}

void RL_OverShow(int page)
{
    Menu *const over = menuManager.FindMenu("reloaded_options");

    if (!over || (over->isVisible() && rl_over == page)) {
        return;
    }
    rl_over    = page;
    rl_mm.edit = F_NONE;
    RL_Changed();
    if (over->isVisible()) {
        return;
    }
    // (over the Esc menu; else that opened first, the page once the push's lock is gone)
    if (menuManager.CurrentMenu() == menuManager.FindMenu("dm_main")) {
        menuManager.ShowMenu("reloaded_options");
    } else {
        UI_PushMenu("dm_main");
        rl_overPending = menuManager.CurrentMenu() == menuManager.FindMenu("dm_main");
    }
}

void RL_OverTick(void)
{
    if (rl_overPending && menuManager.ShowMenu("reloaded_options")) {
        rl_overPending = false;
    }
}

int RL_OverPage(void)
{
    Menu *const over = menuManager.FindMenu("reloaded_options");

    return over && over->isVisible() ? rl_over : -1;
}

void RL_MainShow(int what)
{
    if (what != RL_SHOW_KEYS) {
        rl_mm.page  = what == RL_SHOW_PLAY ? PAGE_PLAY : PAGE_CAMPAIGN;
        rl_camp.tab = what == RL_SHOW_SAVES ? CT_SAVES : CT_MISSIONS;
    } else {
        rl_opt.tab    = OT_CONTROLS;
        rl_opt.scroll = 0;
        // (in a game the Options over the Esc menu's: the main menu's page kept, unless it is up)
        if (clc.state <= CA_PRIMED || menuManager.CurrentMenu() == menuManager.FindMenu("main")) {
            rl_mm.page = PAGE_OPTIONS;
        }
    }
    RL_Changed();
}

/*
================
DrawMain: the back layer the photo and what is under the model views (the
profile's panel) or the crosshair's preview (Options' panel), the front
the rest
================
*/
// the screens beside the rail: their left edge, their width
static void RL_PageX(const rlView_t& v, float *x, float *w)
{
    *x = v.dw >= 1700 ? 480.0f : 420.0f;
    *w = Q_min(v.dw - *x - RL_EDGE, 1640.0f);
}

void UIReloadedMenu::DrawMain(const rlView_t& v)
{
    const float ay = RL_EDGE, ah = v.dh - 2 * RL_EDGE;
    float       ax, aw;

    RL_PageX(v, &ax, &aw);
    const float pw = Q_max(270.0f, floorf(aw * 0.28f + 0.5f));

    // (the cvars it draws from: the cache draws again when one changes)
    UI_DrawDep(Cvar_Get("dm_playermodel", "", 0));
    UI_DrawDep(Cvar_Get("dm_playergermanmodel", "", 0));
    if (m_back) {
        RL_Background(v);
        if (rl_mm.page == PAGE_PLAY) {
            RL_Profile(this, v, ax + aw - pw, ay, pw, ah);
        } else {
            PlaceView(v, "allies", 0, 0, 0, 0);
            PlaceView(v, "axis", 0, 0, 0, 0);
        }
        if (rl_mm.page == PAGE_OPTIONS) {
            RL_Options(this, v, ax, ay, aw, ah);
        }
        return;
    }
    {
        // (DISCONNECT over QUIT while in a game)
        const rlRailItem_t foot[2] = {{ID_DISCONNECT, RL_InMission() ? "LEAVE MISSION" : "DISCONNECT", RL_GLYPH("disconnect")},
                                      {ID_QUIT, "QUIT", RL_GLYPH("quit")}};
        const int          in      = clc.state > CA_PRIMED;

        RL_Rail(this, v, rl_pages, (int)ARRAY_LEN(rl_pages), ID_PLAY + rl_mm.page, foot + 1 - in, 1 + in);
    }
    if (rl_mm.page == PAGE_PLAY) {
        RL_Browser(this, v, ax, ay, aw - pw - 20, ah);
        RL_Profile(this, v, ax + aw - pw, ay, pw, ah);
    } else if (rl_mm.page == PAGE_CAMPAIGN) {
        RL_Campaign(this, v, ax, ay, aw, ah);
    } else if (rl_mm.page == PAGE_HOST) {
        RL_Host(this, v, ax, ay, aw, ah);
    } else {
        RL_Options(this, v, ax, ay, aw, ah);
    }
}

/*
================
DrawOptions: Options over the game (the Esc menu's OPTIONS, shown over it),
or the vote's page (its VOTE): the page beside the Esc menu's rail where
the main menu shows it; the game running under them, blurred and dimmed by
the Esc menu
================
*/
void UIReloadedMenu::DrawOptions(const rlView_t& v)
{
    float x, w;

    RL_PageX(v, &x, &w);
    (rl_over == RL_OVER_VOTE ? RL_Vote : RL_Options)(this, v, x, RL_EDGE, w, v.dh - 2 * RL_EDGE);
}

/*
================
DrawConfirm: Omaha's confirmation, over the main menu: Quit's,
Disconnect's in a game (Leave mission's in single player), Campaign's
Delete, KEYS' move of a key another command has
================
*/
void UIReloadedMenu::DrawConfirm(const rlView_t& v)
{
    const float w = 560, h = 230, x = (v.dw - w) / 2, y = (v.dh - h) / 2, by = y + h - 24 - 48;
    // (the title, the question, the button)
    const char *const says[5][3] = {
        {"QUIT",          "Close the game?",         "QUIT"      },
        {"DISCONNECT",    "Leave the game?",         "DISCONNECT"},
        {"LEAVE MISSION", "Leave the mission?",      "LEAVE"     },
        {"DELETE",        "Delete this saved game?", "DELETE"    },
        {"KEY IN USE",    "",                        "MOVE"      },
    };
    const int k = rl_mm.confirm == ID_QUIT ? 0 : rl_mm.confirm == ID_CDELETE ? 3 : rl_mm.confirm == ID_KEYMOVE ? 4 : RL_InMission() ? 2 : 1;

    if (m_back) {
        return;
    }
    RL_Box(v, 0, 0, v.dw, v.dh, RL_Rgb(0, 0, 0, 140));
    RL_Panel(v, x, y, w, h, RL_Rgb(12, 13, 15, 250));
    RL_Head(v, x, y, w, 56);
    RL_Text(v, x + 24, y + 28, says[k][0], 22, RL_TEXT, true, RL_LEFT, 2, RL_MID);
    RL_Text(v, x + 24, y + 100, k == 4 ? RL_KeyQuestion().c_str() : says[k][1], 22, RL_SOFT, false, RL_LEFT, 0, RL_MID, w - 48);
    RL_Button(this, v, x + w - 24 - 328, by, 150, 48, "CANCEL", false, ID_CANCEL);
    RL_Button(this, v, x + w - 24 - 170, by, 170, 48, says[k][2], true, ID_CONFIRM);
}

/*
================
The HDR calibration (r_hdrOutputTest; cl_ui.cpp's UI_HdrCalibrationUpdate
opens and closes them): the black level's screen (r_hdrBlack, NEXT) and
the peak's (r_hdrPeak, FINISH), what NEXT and FINISH run the menu's
stuffcommand (ui/hdr_calibrate_*.urc). The renderer draws its pattern in
the top four fifths, where the menu left black (output.frag); in the
bottom fifth a band the screen wide, a line along its top, and in it,
1104 across in its middle, the title (the step on its right) over what to
do (Escape's word on its right) over the slider: Omaha's (Options') made
wide, its track, the part to the value brass, the knob, the value in a
box, the button beside it. The track is clicked or dragged; the arrow keys
and the wheel step it; Enter is the button. The HDR look's (r_hdrOutput 2)
peak says what its pattern shows
================
*/

typedef struct {
    const char *cvar, *def; // as vk_output.c has them
    float       lo, hi, step;
} rlHdrSlider_t;

static const rlHdrSlider_t rl_hdrSliders[2] = {
    {"r_hdrBlack", "0",    -0.5f,  1.0f,     0.05f},
    {"r_hdrPeak",  "1000", 100.0f, 10000.0f, 50.0f},
};

static struct {
    bool drag; // the slider dragged
    bool down; // the mouse's button, last frame
} rl_hdr;

static cvar_t *RL_HdrCvar(int page)
{
    return Cvar_Get(rl_hdrSliders[page].cvar, rl_hdrSliders[page].def, CVAR_ARCHIVE);
}

// the slider set to a value (snapped to its steps, within its ends)
static void RL_HdrSlide(int page, float value)
{
    const rlHdrSlider_t& s = rl_hdrSliders[page];

    value = Q_max(s.lo, Q_min(s.hi, value));
    Cvar_Set(s.cvar, va("%g", s.lo + floorf((value - s.lo) / s.step + 0.5f) * s.step));
}

void UIReloadedMenu::DrawHdr(const rlView_t& v)
{
    const int            page = m_screen == SCR_HDRPEAK;
    const rlHdrSlider_t& s    = rl_hdrSliders[page];
    cvar_t *const        cv   = RL_HdrCvar(page);
    cvar_t *const        out  = Cvar_FindVar("r_hdrOutput");
    const bool           look = out && out->integer == 2;
    // the band (the pattern above it: output.frag's uv.y < 0.8), the content 116 high in its middle
    const float top = v.dh * 0.8f, w = Q_min(v.dw - 2 * RL_EDGE, 1104.0f), x = (v.dw - w) / 2, y = top + (v.dh - top - 116) / 2;
    const float ry = y + 74, bw = 190, vw = 96, tw = w - bw - vw - 2 * RL_PAD - 8, vx = x + tw + RL_PAD + 8;
    const float frac = Q_max(0.0f, Q_min(1.0f, (cv->value - s.lo) / (s.hi - s.lo)));
    const bool  hot  = Hot(ID_HDRSLIDE) || rl_hdr.drag;
    const char *say  = !page ? "Move the slider until the box in the middle is barely visible"
                     : look  ? "Raise the slider to lift the box toward the white around it"
                             : "Raise the slider until the box disappears into the background";

    UI_DrawDep(cv);
    if (out) {
        UI_DrawDep(out);
    }
    RL_Box(v, 0, top, v.dw, v.dh - top, RL_Rgb(12, 13, 15));
    RL_Box(v, 0, top, v.dw, 1, RL_LINE);
    RL_Text(v, x, y + 12, page ? "PEAK BRIGHTNESS" : "BLACK LEVEL", 24, RL_TEXT, true, RL_LEFT, 2, RL_MID);
    RL_Text(v, x + w, y + 12, va("HDR CALIBRATION  \xb7  %d OF 2", page + 1), 15, RL_BRASS, true, RL_RIGHT, 2.6f, RL_MID);
    RL_Text(v, x, y + 46, say, 18, RL_SOFT, false, RL_LEFT, 0, RL_MID, w - 160);
    RL_Text(v, x + w, y + 46, page ? "ESC  BACK" : "ESC  CLOSES", 15, RL_MUTED, true, RL_RIGHT, 2, RL_MID);
    // the slider, its value, the button
    RL_Box(v, x, ry + 19, tw, 4, RL_Rgb(255, 255, 255, 40));
    RL_Box(v, x, ry + 19, tw * frac, 4, RL_BRASS);
    RL_Round(v, x + tw * frac - 10, ry + 11, 20, 20, 10, hot ? RL_BRASS : RL_TEXT);
    Hit(v, x - 10, ry, tw + 20, RL_CTRL, ID_HDRSLIDE);
    RL_Panel(v, vx, ry, vw, RL_CTRL, RL_Rgb(0, 0, 0, 120));
    RL_Text(v, vx + vw / 2, ry + RL_CTRL / 2, page ? va("%.0f", cv->value) : va("%.2f", cv->value), 18, RL_TEXT, false, RL_CENTER, 0, RL_MID);
    RL_Button(this, v, x + w - bw, ry, bw, RL_CTRL, page ? "FINISH" : "NEXT", true, ID_CONFIRM);
}

// NEXT, FINISH: the menu's command (NEXT pushes the peak's menu over the black level's, FINISH sets r_hdrOutputTest 0)
void UIReloadedMenu::ChooseHdr(int id)
{
    if (id == ID_CONFIRM && m_command.length()) {
        Cbuf_AddText((m_command + "\n").c_str());
    }
}

qboolean UIReloadedMenu::HdrKey(int key)
{
    const int   page = m_screen == SCR_HDRPEAK;
    const float step = key == K_RIGHTARROW || key == K_MWHEELUP ? 1.0f : key == K_LEFTARROW || key == K_MWHEELDOWN ? -1.0f : 0.0f;

    if (key == K_ENTER || key == K_KP_ENTER) {
        ChooseHdr(ID_CONFIRM);
        return qtrue;
    }
    if (step) {
        RL_HdrSlide(page, RL_HdrCvar(page)->value + step * rl_hdrSliders[page].step);
        return qtrue;
    }
    return qfalse;
}

// each frame: the slider pressed dragged to the mouse while its button is down
void UIReloadedMenu::HdrTick(void)
{
    const bool down = (uid.mouseFlags & 1) != 0;
    const int  page = m_screen == SCR_HDRPEAK;

    if (down && !rl_hdr.down && m_focus == ID_HDRSLIDE) {
        rl_hdr.drag = true;
    }
    rl_hdr.down = down;
    if (!down) {
        rl_hdr.drag = false;
    }
    for (const rlHit_t& h : m_hits) {
        if (rl_hdr.drag && h.id == ID_HDRSLIDE) {
            // (the hit reaches the knob's half past the track's ends)
            const float pad = RL_View().s * 10, frac = (uid.mouseX - (int)m_screenframe.pos.x - h.x - pad) / (h.w - 2 * pad);
            const rlHdrSlider_t& s = rl_hdrSliders[page];

            RL_HdrSlide(page, s.lo + (s.hi - s.lo) * frac);
        }
    }
}

/*
================
DrawConnect: the connect's box (CONNECTING), Omaha's confirmation's
frame 640 wide: the head (what goes on), the server (its map's photo
176 by 132, its name over its address, its map, its mode, players and
ping, the last line on the photo's foot), then the steps over what goes
on beside CANCEL, or what happened (the password's field under it) over
BACK and RETRY (JOIN); 24 in from its edges, 24 between its parts
================
*/

// the server's word on one line, without the spaces round it
static std::string RL_OneLine(std::string s)
{
    std::replace(s.begin(), s.end(), '\n', ' ');
    s.erase(0, s.find_first_not_of(' '));
    s.erase(s.find_last_not_of(' ') + 1);
    return s;
}

// what the server's word gives as its reason ("Reason: ...", "for:\n...", "was kicked for ..."), "" none
static std::string RL_Reason(const std::string& said)
{
    for (const char *mark : {"Reason: ", ":\n", " for "}) {
        const size_t at = said.find(mark);

        if (at != std::string::npos) {
            return RL_OneLine(said.substr(at + strlen(mark)));
        }
    }
    return "";
}

// the game's own server (a mission's, Host's): joined in a moment, nothing to show
static bool RL_ConnectOwn(void)
{
    return rl_cn.end == CE_NONE && com_sv_running->integer;
}

// what the box says of the connect's end: its head, what happened, a word more
static const char *RL_ConnectWords(std::string& what, std::string& more)
{
    const std::string said = RL_OneLine(rl_cn.said);

    switch (rl_cn.end) {
    case CE_FULL:
        what = "Server is full";
        more = rl_cn.maxPlayers > 1    ? va("All %d places are taken. Try again in a moment.", rl_cn.maxPlayers)
             : rl_cn.maxPlayers == 1 ? "Its one place is taken. Try again in a moment."
                                     : "Try again in a moment.";
        break;
    case CE_PASSWORD:
        // (one sent already: the launcher's, or typed here)
        what = Cvar_VariableString("password")[0] ? "Wrong password" : "This server needs a password";
        more = "Type it and press JOIN.";
        return "PASSWORD";
    case CE_BANNED:
        what = "You are banned from this server";
        more = RL_Reason(rl_cn.said);
        break;
    case CE_KICKED:
        what = "Kicked from the server";
        more = RL_Reason(rl_cn.said);
        break;
    case CE_VERSION:
        what = "Wrong version";
        more = said;
        break;
    case CE_TIMEOUT:
        what = "Timed out";
        more = rl_cn.joined ? "The server stopped answering." : va("No answer from %s.", rl_cn.addr.c_str());
        break;
    case CE_ADDRESS:
        what = "Bad address";
        more = va("Couldn't find %s.", rl_cn.addr.c_str());
        break;
    case CE_DROPPED:
        what = "Disconnected";
        more = said.empty()                       ? "The server dropped the connection."
             : !Q_stricmp(said.c_str(), "Server quit") || !Q_stricmp(said.c_str(), "killserver")
                     || !Q_stricmp(said.c_str(), "Server was killed")
                                                  ? "The server shut down."
                                                  : said;
        break;
    default:
        // (its first line, then the rest)
        what = RL_OneLine(rl_cn.said.substr(0, rl_cn.said.find('\n')));
        more = rl_cn.said.find('\n') == std::string::npos ? "" : RL_OneLine(rl_cn.said.substr(rl_cn.said.find('\n') + 1));
        if (what.empty()) {
            what = "The server refused the connection";
        }
        break;
    }
    return rl_cn.joined ? "DISCONNECTED" : "CAN'T JOIN";
}

void UIReloadedMenu::DrawConnect(const rlView_t& v)
{
    static const char *const steps[] = {"CONTACT", "JOIN", "WAIT FOR THE SERVER"};
    const bool  ended = rl_cn.end != CE_NONE, password = rl_cn.end == CE_PASSWORD;
    const float w = Q_min(640.0f, v.dw - 2 * RL_EDGE), pad = 24, pw = 176, ph = 132;
    const float part = ended ? (password ? 152.0f : 92.0f) : 30.0f;
    const float h = 56 + pad + ph + pad + part + pad + 48 + pad, x = (v.dw - w) / 2, y = (v.dh - h) / 2;
    const float top = y + 56 + pad, ix = x + pad + pw + 20, iw = x + w - pad - ix, sy = top + ph + pad, by = y + h - pad - 48;
    const rlColor_t red = RL_Rgb(226, 104, 84);
    std::string what, more;
    const char *head = ended ? RL_ConnectWords(what, more) : "CONNECTING";

    if (RL_ConnectOwn()) {
        RL_Box(v, 0, 0, v.dw, v.dh, RL_Rgb(0, 0, 0));
        return;
    }
    RL_Background(v);
    RL_Box(v, 0, 0, v.dw, v.dh, RL_Rgb(0, 0, 0, 90));
    RL_Panel(v, x, y, w, h, RL_Rgb(12, 13, 15, 250));
    RL_Head(v, x, y, w, 56);
    RL_Text(v, x + pad, y + 28, head, 22, ended && !password ? red : RL_TEXT, true, RL_LEFT, 2, RL_MID);

    // the server: its map's photo (none known yet: a frame), its name over its address (none: the address), its map, the rest
    if (!rl_cn.map.empty()) {
        RL_Photo(v, RL_Shot(rl_cn.map), x + pad, top, pw, ph);
    } else {
        RL_Box(v, x + pad, top, pw, ph, RL_Rgb(255, 255, 255, 10));
        RL_Text(v, x + pad + pw / 2, top + ph / 2, ended ? "MAP NOT KNOWN" : "MAP NOT KNOWN YET", 12, RL_FAINT, true, RL_CENTER, 2, RL_MID);
    }
    RL_Frame(v, x + pad, top, pw, ph, 1, RL_Rgb(255, 255, 255, 30));
    {
        const bool named = !rl_cn.name.empty();
        float      fx    = ix;

        RL_Text(v, ix, top, RL_Ellipsize(v, (named ? rl_cn.name : rl_cn.addr).c_str(), 28, true, iw).c_str(), 28, RL_TEXT, true, RL_LEFT, 0,
                RL_TOP);
        if (named || (rl_cn.askedAt && !ended)) {
            RL_Text(v, ix, top + 42, named ? rl_cn.addr.c_str() : "Asking the server for its name and map\x85", 16, RL_MUTED, false,
                    RL_LEFT, 0, RL_TOP, iw);
        }
        if (!rl_cn.map.empty()) {
            RL_Text(v, ix, top + 80, RL_MapName(rl_cn.map.c_str()), 20, RL_TEXT, false, RL_LEFT, 0, RL_TOP, iw);
        }
        // the mode, players and ping (a drop's: its mode, the rest of then)
        if (!rl_cn.mode.empty()) {
            fx += RL_Text(v, fx, top + ph, RL_Upper(rl_cn.mode.c_str()).c_str(), 14, RL_MUTED, true, RL_LEFT, 1.5f, RL_BASE);
        }
        if (!rl_cn.joined && rl_cn.maxPlayers > 0) {
            fx += fx > ix ? RL_Text(v, fx, top + ph, "  \xb7  ", 14, RL_MUTED, true, RL_LEFT, 1.5f, RL_BASE) : 0.0f;
            fx += RL_Text(v, fx, top + ph, va("%d / %d", rl_cn.players, rl_cn.maxPlayers), 14, rl_cn.end == CE_FULL ? red : RL_MUTED, true,
                          RL_LEFT, 1.5f, RL_BASE);
        }
        if (!rl_cn.joined && rl_cn.ping >= 0) {
            fx += fx > ix ? RL_Text(v, fx, top + ph, "  \xb7  ", 14, RL_MUTED, true, RL_LEFT, 1.5f, RL_BASE) : 0.0f;
            RL_Text(v, fx, top + ph, va("%d MS", rl_cn.ping), 14, RL_MUTED, true, RL_LEFT, 1.5f, RL_BASE);
        }
    }

    if (!ended) {
        // the steps: done brass, the one going on a dash sweeping, the rest faint; what goes on (the server's word) beside CANCEL
        const int   at = clc.state >= CA_CONNECTED ? 2 : clc.state == CA_CHALLENGING ? 1 : 0;
        const float sw = (w - 2 * pad - 16) / 3;
        std::string said = RL_OneLine(clc.serverMessage);

        for (int i = 0; i < 3; i++) {
            const float sx = x + pad + i * (sw + 8);

            RL_Text(v, sx, sy, steps[i], 13, i < at ? RL_TEXT : i == at ? RL_BRASS : RL_FAINT, true, RL_LEFT, 2, RL_TOP);
            RL_Box(v, sx, sy + 26, sw, 4, RL_Rgb(255, 255, 255, 30));
            if (i < at) {
                RL_Box(v, sx, sy + 26, sw, 4, RL_BRASS);
            } else if (i == at) {
                const float dw = sw * 0.3f, dx = (sw + dw) * (cls.realtime % 1400) / 1400.0f - dw;

                RL_Box(v, sx + Q_max(dx, 0.0f), sy + 26, Q_min(dx + dw, sw) - Q_max(dx, 0.0f), 4, RL_BRASS);
            }
        }
        if (said.empty()) {
            said = at == 0 ? "Contacting the server\x85" : at == 1 ? "Asking to join\x85" : "Waiting for the server\x85";
        }
        RL_Text(v, x + pad, by + 24, RL_Ellipsize(v, said.c_str(), 18, false, w - 3 * pad - 150).c_str(), 18,
                clc.serverMessage[0] ? RL_TEXT : RL_MUTED, false, RL_LEFT, 0, RL_MID);
        RL_Button(this, v, x + w - pad - 150, by, 150, 48, "CANCEL", false, ID_CANCEL);
        return;
    }
    // what happened, a word more; the password's field
    RL_Box(v, x + pad, sy, w - 2 * pad, 1, RL_LINE);
    RL_Text(v, x + pad, sy + 24, RL_Ellipsize(v, what.c_str(), 24, true, w - 2 * pad).c_str(), 24, RL_TEXT, true, RL_LEFT, 0, RL_TOP);
    RL_Text(v, x + pad, sy + 62, RL_Ellipsize(v, more.c_str(), 17, false, w - 2 * pad).c_str(), 17, RL_SOFT, false, RL_LEFT, 0, RL_TOP);
    if (password) {
        RL_Field(v, x + pad, sy + 96, w - 2 * pad, 44, std::string(rl_cn.typed.size(), '\x95'), "Password", true, false);
    }
    RL_Button(this, v, x + w - pad - 328, by, 150, 48, "BACK", false, ID_CANCEL);
    RL_Button(this, v, x + w - pad - 170, by, 170, 48, password ? "JOIN" : "RETRY", true, ID_CONFIRM);
}

/*
====================================================================

INPUT

====================================================================
*/

// the text of the field typed in, NULL none
static std::string *RL_Edited(void)
{
    switch (rl_mm.edit) {
    case F_NONE:
        return NULL;
    case F_SEARCH:
        return &rl_mm.search;
    case F_NAME:
        return &rl_mm.name;
    case F_CODE:
        return &rl_opt.code;
    case F_VALUE:
        return &rl_opt.value;
    case F_VOTE:
        return &rl_vote.entry;
    }
    return &rl_host.field[rl_mm.edit - F_HOST];
}

static void RL_ApplyName(void)
{
    if (!rl_mm.name.empty()) {
        Cvar_Set("name", rl_mm.name.c_str());
    }
    rl_mm.edit = F_NONE;
}

// the server picked moved to (the list scrolled to show it)
static void RL_SvPick(int at)
{
    const int n = (int)rl_vis.rows.size();

    if (!n) {
        return;
    }
    at        = Q_max(0, Q_min(at, n - 1));
    rl_mm.sel = rl_sv.rows[rl_vis.rows[at]].addr;
    rl_mm.top = Q_min(rl_mm.top, at);
    rl_mm.top = Q_max(rl_mm.top, at - rl_mm.fit + 1);
    rl_mm.note.clear();
}

void UIReloadedMenu::ChooseMain(int id)
{
    // (the left button's release, bound to a box: no click)
    if (rl_keys.swallow) {
        rl_keys.swallow = false;
        return;
    }
    // Options over the Esc menu: a click off its page the Esc menu's
    if (m_screen == SCR_OPTIONS && id < 0 && RL_EscClick(uid.mouseX - (int)m_screenframe.pos.x, uid.mouseY - (int)m_screenframe.pos.y)) {
        return;
    }
    if (m_screen == SCR_OPTIONS && rl_over == RL_OVER_VOTE) {
        RL_VoteChoose(id);
        return;
    }
    if (m_screen == SCR_CONNECT) {
        if (id == ID_CANCEL) {
            // CANCEL, BACK: out of the game, the main menu's Multiplayer
            Cbuf_AddText("popmenu 0\ndisconnect\npushmenu main\n");
        } else if (id == ID_CONFIRM && rl_cn.end != CE_NONE) {
            // RETRY, JOIN (the password typed sent with it)
            if (rl_cn.end == CE_PASSWORD) {
                Cvar_Set("password", rl_cn.typed.c_str());
            }
            Cbuf_AddText("reconnect\n");
        }
        return;
    }
    if (m_screen == SCR_CONFIRM) {
        if (id == ID_CONFIRM) {
            if (rl_mm.confirm == ID_CDELETE) {
                RL_CampDelete();
                RL_Changed();
            } else if (rl_mm.confirm == ID_KEYMOVE) {
                RL_KeyBind(rl_keys.ask[0], rl_keys.ask[1]);
            }
            // (the confirmation closed first: the main menu stays, out of the game)
            Cbuf_AddText(rl_mm.confirm == ID_QUIT                                        ? "quit\n"
                         : rl_mm.confirm == ID_CDELETE || rl_mm.confirm == ID_KEYMOVE ? "popmenu 0\n"
                                                                                       : "popmenu 0\ndisconnect\n");
        } else if (id == ID_CANCEL) {
            Cbuf_AddText("popmenu 0\n");
        }
        return;
    }
    RL_Changed();
    // a click elsewhere closes the model list and ends the typing
    if (rl_mm.pick >= 0 && (id < ID_PICK || id >= ID_PICK + (int)rl_models[rl_mm.pick].size())) {
        const int was = rl_mm.pick;

        rl_mm.pick = -1;
        if (id == ID_SELECT + was) {
            return;
        }
    }
    // (a value typed in a slider's box set as the typing ends)
    if (rl_mm.edit == F_VALUE && id != ID_OPT + rl_opt.row * RL_OPT_IDS + 1) {
        RL_OptTyped();
    }
    if (id != ID_SEARCH && id != ID_NAME && id != ID_APPLY && (id < ID_FIELD || id >= ID_FIELD + HF_NUM) && id < ID_OPT) {
        rl_mm.edit = F_NONE;
    }
    rl_opt.note.clear();
    if (id == ID_BACK) {
        // Options over the game closed: the Esc menu under it
        Cbuf_AddText("popmenu 0\n");
        return;
    }
    if (id >= ID_KEY) {
        // a box: it waits for a key (the keys to the menu, Esc's too, as to the stock list's buttons)
        rl_keys.wait = id - ID_KEY;
        rl_mm.edit   = F_NONE;
        uWinMan.SetBindActive(this);
        return;
    }
    if (id >= ID_OPT) {
        RL_OptChoose(id);
        return;
    }
    if (id >= ID_SAVE) {
        // a saved game picked; clicked again (a double click), loaded
        const int at = rl_camp.top + id - ID_SAVE;

        if (at < (int)rl_camp.saves.size()) {
            const bool again = rl_camp.saves[at].name == rl_camp.save && cls.realtime - rl_camp.clickAt < 400;

            rl_camp.save    = rl_camp.saves[at].name;
            rl_camp.clickAt = cls.realtime;
            if (again) {
                RL_CampLoad(rl_camp.save);
            }
        }
        return;
    }
    if (id >= ID_MAP) {
        if (rl_host.top + id - ID_MAP < (int)rl_host.maps.size()) {
            rl_host.map = rl_host.maps[rl_host.top + id - ID_MAP];
        }
        return;
    }
    if (id >= ID_PICK) {
        const int t = rl_mm.pick, k = id - ID_PICK;

        Cvar_Set(t ? "dm_playergermanmodel" : "dm_playermodel", rl_models[t][k].file.c_str());
        rl_mm.pick = -1;
        return;
    }
    if (id >= ID_FAV) {
        if (rl_mm.top + id - ID_FAV < (int)rl_vis.rows.size()) {
            RL_SvFavorite(rl_sv.rows[rl_vis.rows[rl_mm.top + id - ID_FAV]].addr);
        }
        return;
    }
    if (id >= ID_ROW) {
        const int  at    = rl_mm.top + id - ID_ROW;
        const bool again = at < (int)rl_vis.rows.size() && rl_sv.rows[rl_vis.rows[at]].addr == rl_mm.sel;

        RL_SvPick(at);
        // a double click joins
        if (again && cls.realtime - rl_mm.clickAt < 400) {
            RL_SvJoin();
        }
        rl_mm.clickAt = cls.realtime;
        return;
    }
    if (id >= ID_TAB && id < ID_TAB + TAB_NUM) {
        rl_mm.tab    = id - ID_TAB;
        rl_mm.top    = 0;
        rl_vis.dirty = true;
        return;
    }
    if (id >= ID_SORT && id < ID_SORT + COL_NUM) {
        // the same column the other way; another its own way first (players: the most first)
        rl_mm.asc    = rl_mm.sort == id - ID_SORT ? !rl_mm.asc : id - ID_SORT != COL_PLAYERS;
        rl_mm.sort   = id - ID_SORT;
        rl_vis.dirty = true;
        return;
    }
    if (id >= ID_MODE && id < ID_MODE + RL_HostModes()) {
        rl_host.mode = id - ID_MODE;
        RL_HostList();
        return;
    }
    if (id >= ID_FIELD && id < ID_FIELD + HF_NUM) {
        rl_mm.edit = F_HOST + id - ID_FIELD;
        return;
    }
    if (id >= ID_CTAB && id < ID_CTAB + 2) {
        rl_camp.tab = id - ID_CTAB;
        return;
    }
    if (id >= ID_CARD && id < ID_CARD + 1 + RL_GAME.n) {
        // a card picked; clicked again (a double click), started
        const bool again = rl_camp.card == id - ID_CARD && cls.realtime - rl_camp.clickAt < 400;

        rl_camp.card    = id - ID_CARD;
        rl_camp.clickAt = cls.realtime;
        if (again) {
            RL_CampStart();
        }
        return;
    }
    if (id >= ID_DIFF && id < ID_DIFF + 3) {
        rl_camp.diff = id - ID_DIFF;
        return;
    }
    if (id >= ID_OTAB && id < ID_OTAB + OT_NUM) {
        rl_opt.tab    = id - ID_OTAB;
        rl_opt.scroll = 0;
        return;
    }
    switch (id) {
    case ID_LOCAL:
    case ID_ONLINE:
        rl_host.online = id == ID_ONLINE;
        break;
    case ID_START:
        RL_HostStart();
        break;
    case ID_DEFAULTS:
        RL_OptDefaults();
        break;
    case ID_OAPPLY:
        RL_OptApply();
        break;
    case ID_CSTART:
        RL_CampStart();
        break;
    case ID_CLOAD:
        if (RL_CampPicked()) {
            RL_CampLoad(rl_camp.save);
        }
        break;
    case ID_CSAVE:
        // (back in the mission: the shot taken of it, not of the menu)
        Cbuf_AddText("popmenu 0\nsavegame\n");
        break;
    case ID_PLAY:
    case ID_CAMPAIGN:
    case ID_HOST:
    case ID_OPTIONS:
        if (rl_mm.page != id - ID_PLAY) {
            rl_mm.page = id - ID_PLAY;
            if (rl_mm.page == PAGE_PLAY && !rl_sv.list[0]) {
                RL_SvRefresh();
            }
        }
        break;
    case ID_CDELETE:
        if (!RL_CampPicked()) {
            break;
        }
        // fall through
    case ID_QUIT:
    case ID_DISCONNECT:
        rl_mm.confirm = id;
        menuManager.ShowMenu("reloaded_confirm");
        break;
    case ID_REFRESH:
        RL_SvRefresh();
        break;
    case ID_SEARCH:
        rl_mm.edit = F_SEARCH;
        break;
    case ID_NAME:
        rl_mm.edit = F_NAME;
        break;
    case ID_APPLY:
        RL_ApplyName();
        break;
    case ID_JOIN:
        RL_SvJoin();
        break;
    case ID_SELECT:
    case ID_SELECT + 1:
        {
            // open at its model
            const int t = id - ID_SELECT;

            rl_mm.pick    = t;
            rl_mm.pickTop = Q_max(0, RL_OwnIndex(t) - 3);
        }
        break;
    }
}

// Campaign's keys: Enter starts (MISSIONS) or loads (SAVED GAMES); the arrows pick a saved game, Delete deletes it
static qboolean RL_CampKey(int key)
{
    int at;

    if (key == K_ENTER || key == K_KP_ENTER) {
        if (rl_camp.tab == CT_MISSIONS) {
            RL_CampStart();
        } else if (RL_CampPicked()) {
            RL_CampLoad(rl_camp.save);
        }
        return qtrue;
    }
    if (rl_camp.tab != CT_SAVES || !RL_CampPicked(&at)) {
        return qfalse;
    }
    if (key == K_DEL) {
        rl_mm.confirm = ID_CDELETE;
        menuManager.ShowMenu("reloaded_confirm");
        return qtrue;
    }
    if (key == K_UPARROW || key == K_DOWNARROW) {
        at           = Q_max(0, Q_min(at + (key == K_UPARROW ? -1 : 1), (int)rl_camp.saves.size() - 1));
        rl_camp.save = rl_camp.saves[at].name;
        rl_camp.top  = Q_max(at - rl_camp.fit + 1, Q_min(rl_camp.top, at));
        RL_Changed();
        return qtrue;
    }
    return qfalse;
}

qboolean UIReloadedMenu::MainKey(int key)
{
    const int step = key == K_MWHEELUP ? -3 : key == K_MWHEELDOWN ? 3 : 0;
    const int page = m_screen != SCR_OPTIONS ? rl_mm.page : rl_over == RL_OVER_VOTE ? PAGE_VOTE : PAGE_OPTIONS;

    if (m_screen == SCR_CONFIRM || m_screen == SCR_CONNECT) {
        if (m_screen == SCR_CONNECT && key == K_BACKSPACE && !rl_cn.typed.empty()) {
            rl_cn.typed.erase(rl_cn.typed.size() - 1);
            RL_Changed();
            return qtrue;
        }
        if (key == K_ENTER || key == K_KP_ENTER) {
            ChooseMain(ID_CONFIRM);
            return qtrue;
        }
        return qfalse;
    }
    if (rl_keys.wait >= 0) {
        // (the wheel too: a key for the box waiting)
        RL_KeyPressed(key);
        RL_Changed();
        return qtrue;
    }
    if (step) {
        // the wheel: the model list open, else the page's list
        if (page == PAGE_OPTIONS) {
            RL_OptScroll(step > 0);
        } else if (page == PAGE_VOTE) {
            // (the list under the mouse)
            (uid.mouseX - (int)m_screenframe.pos.x >= rl_vote.split ? rl_vote.ctop : rl_vote.top) += step;
        } else if (rl_mm.pick >= 0) {
            rl_mm.pickTop += step;
        } else if (page == PAGE_HOST) {
            rl_host.top += step;
        } else if (page == PAGE_CAMPAIGN) {
            rl_camp.top += step;
        } else {
            rl_mm.top += step;
        }
        RL_Changed();
        return qtrue;
    }
    if (RL_Edited()) {
        std::string& text = *RL_Edited();

        if (key == K_BACKSPACE) {
            if (!text.empty()) {
                text.erase(text.size() - 1);
                if (rl_mm.edit == F_SEARCH) {
                    rl_vis.dirty = true;
                }
            }
            RL_Changed();
            return qtrue;
        }
        if (key == K_ENTER || key == K_KP_ENTER) {
            if (rl_mm.edit == F_NAME) {
                RL_ApplyName();
            } else if (rl_mm.edit == F_VALUE) {
                RL_OptTyped();
            } else if (rl_mm.edit == F_CODE) {
                RL_OptImport();
            } else if (rl_mm.edit == F_VOTE) {
                RL_VoteCall();
            } else {
                rl_mm.edit = F_NONE;
            }
            RL_Changed();
            return qtrue;
        }
    }
    if (page == PAGE_CAMPAIGN) {
        return RL_CampKey(key);
    }
    if (page == PAGE_VOTE && (key == K_ENTER || key == K_KP_ENTER)) {
        RL_VoteCall();
        return qtrue;
    }
    if (page != PAGE_PLAY) {
        return qfalse;
    }
    switch (key) {
    case K_UPARROW:
    case K_DOWNARROW:
    case K_PGUP:
    case K_PGDN:
        {
            const int at = RL_SvPicked(), d = key == K_UPARROW ? -1 : key == K_DOWNARROW ? 1 : key == K_PGUP ? -rl_mm.fit : rl_mm.fit;

            RL_SvShown();
            RL_SvPick(at < 0 ? rl_mm.top : at + d);
            RL_Changed();
        }
        return qtrue;
    case K_ENTER:
    case K_KP_ENTER:
        RL_SvJoin();
        RL_Changed();
        return qtrue;
    }
    return qfalse;
}

// the mouse's buttons down (a bit each, MOUSE1's the first): one a key for the box waiting (the left one's release
// then no click)
void UIReloadedMenu::MainButtons(int buttons)
{
    for (int j = 0; rl_keys.wait >= 0 && j < 5; j++) {
        if (buttons & (1 << j)) {
            rl_keys.swallow = j == 0;
            RL_KeyPressed(K_MOUSE1 + j);
            RL_Changed();
        }
    }
}

void UIReloadedMenu::MainChar(int ch)
{
    // the connect's password (what a userinfo value can hold)
    if (m_screen == SCR_CONNECT) {
        if (rl_cn.end == CE_PASSWORD && ch >= ' ' && ch <= '~' && ch != '\\' && ch != '"' && ch != ';' && rl_cn.typed.size() < 48) {
            rl_cn.typed += (char)ch;
            RL_Changed();
        }
        return;
    }
    std::string *text = RL_Edited();
    // (the printable ones; the player's name as long as the game keeps it, a
    // server's as the stock host menu's, Host's numbers three digits, a
    // slider's value a number, a crosshair code CS2's letters and digits, a
    // vote's number a number, its text what a callvote's quotes hold)
    const bool      number = rl_mm.edit >= F_HOST && rl_mm.edit != F_HOST + HF_NAME;
    const rlVote_t *vote   = rl_mm.edit == F_VOTE ? RL_VotePicked() : NULL;
    const int       most   = rl_mm.edit == F_NAME             ? MAX_NAME_LENGTH - 1
                           : rl_mm.edit == F_HOST + HF_NAME   ? 31
                           : number                           ? 3
                           : rl_mm.edit == F_VALUE            ? 6
                           : rl_mm.edit == F_CODE             ? 46
                           : vote                             ? 24
                                                              : 40;

    if (!text || ch < ' ' || ch > '~' || (int)text->size() >= most || (number && !isdigit(ch))
        || (rl_mm.edit == F_HOST + HF_NAME && !RL_NameChar(ch)) || (rl_mm.edit == F_VALUE && !isdigit(ch) && ch != '.' && ch != '-')
        || (rl_mm.edit == F_CODE && !isalnum(ch)) || (rl_mm.edit == F_VOTE && (!vote || ch == '"' || ch == ';' || ch == '\\'))
        || (vote && vote->type != VOTE_OPTION_TEXT && !isdigit(ch) && ch != '-' && (ch != '.' || vote->type == VOTE_OPTION_INTEGER))) {
        return;
    }
    *text += (char)ch;
    if (rl_mm.edit == F_SEARCH) {
        rl_mm.top    = 0;
        rl_vis.dirty = true;
    }
    RL_Changed();
}

/*
================
MainTick

Each frame the menu shows: the back layer's photos moved on; the front's
lists' queries run on, Host's maps' photos made, Campaign's saved games
read and its photos made, Options' slider dragged. Opened: the models
listed, the name as it is, the favourites read, the servers asked for the
first time, the saved games to be read again. Options over the game: its
slider dragged; opened, nothing typed in or waiting for a key
================
*/
void UIReloadedMenu::MainTick(bool opened)
{
    const int page = m_screen != SCR_OPTIONS ? rl_mm.page : rl_over == RL_OVER_VOTE ? PAGE_VOTE : PAGE_OPTIONS;

    if (m_screen == SCR_CONNECT) {
        // the connect's box: the backdrop, its map's photo made
        if (!RL_ConnectOwn()) {
            RL_BgTick(opened);
            if (!rl_cn.map.empty() && RL_Shot(rl_cn.map) < 0) {
                RL_ShotMake(rl_cn.map);
                RL_Changed();
            }
        }
        return;
    }
    if (m_back) {
        if (m_screen == SCR_MAIN) {
            RL_BgTick(opened);
        }
        return;
    }
    if (opened) {
        // (nothing typed in, no box waiting for a key)
        rl_mm.edit = F_NONE;
        rl_mm.note.clear();
        rl_opt.note.clear();
        rl_keys.swallow = false;
        if (rl_keys.wait >= 0) {
            RL_KeyStop();
        }
        RL_Changed();
    }
    if (opened && m_screen == SCR_MAIN) {
        const char *favs;

        if (!ui_favoriteservers) {
            // Allied Assault's starts with Forte FFA starred (the launcher's built-in server); its star takes it off
            // as any (the list saved empty)
            ui_favoriteservers = Cvar_Get("ui_favoriteservers", com_target_game->integer == TG_MOH ? "151.80.234.90:12203" : "", CVAR_ARCHIVE);
        }
        RL_ListModels();
        rl_mm.name = Cvar_VariableString("name");
        rl_mm.pick = -1;
        rl_favs.clear();
        favs = ui_favoriteservers->string;
        while (*favs) {
            const char *end = strchr(favs, ' ');
            const size_t n  = end ? (size_t)(end - favs) : strlen(favs);

            if (n) {
                rl_favs.insert(std::string(favs, n));
            }
            favs += n + (end ? 1 : 0);
        }
        rl_vis.dirty    = true;
        rl_camp.scanned = false;
        if (rl_mm.page == PAGE_PLAY && !rl_sv.list[0]) {
            RL_SvRefresh();
        }
    }
    for (GServerList l : rl_sv.list) {
        if (l && ServerListState(l) != sl_idle) {
            ServerListThink(l);
        }
    }
    if (page == PAGE_OPTIONS) {
        // the slider pressed dragged to the mouse while its button is down
        const bool down = (uid.mouseFlags & 1) != 0;

        if (down && !rl_opt.down && m_focus >= ID_OPT && m_focus < ID_KEY && rl_opts[(m_focus - ID_OPT) / RL_OPT_IDS].kind == OC_SLIDER && (m_focus - ID_OPT) % RL_OPT_IDS == 0) {
            rl_opt.drag = m_focus;
        }
        rl_opt.down = down;
        if (!down) {
            rl_opt.drag = -1;
        }
        for (const rlHit_t& h : m_hits) {
            if (h.id == rl_opt.drag) {
                const int i = (h.id - ID_OPT) / RL_OPT_IDS;

                RL_OptSlide(i, rl_opts[i].lo + (rl_opts[i].hi - rl_opts[i].lo) * (uid.mouseX - (int)m_screenframe.pos.x - h.x) / (float)h.w);
            }
        }
    }
    if (page == PAGE_VOTE && !(rl_vote.got && rl_vote.level == cl.serverId) && clc.state == CA_ACTIVE && !clc.demoplaying
        && (!rl_vote.askedAt || (unsigned)(cls.realtime - rl_vote.askedAt) >= 3000)) {
        // the server's votes asked for (again a while on: a server asked too soon sends none)
        rl_vote.askedAt = cls.realtime;
        CL_AddReliableCommand("gvo", qfalse);
    }
    if (m_screen == SCR_OPTIONS) {
        return;
    }
    if (rl_mm.page == PAGE_CAMPAIGN) {
        // the saved games read, the photos made one a frame: the missions', then the saved games' (the newest first)
        if (!rl_camp.scanned) {
            RL_CampScan();
            RL_Changed();
        }
        for (int k = 0; k < RL_GAME.n; k++) {
            if (RL_Shot(RL_MissionKey(k)) < 0) {
                RL_MissionShot(k);
                RL_Changed();
                return;
            }
        }
        for (const rlSave_t& s : rl_camp.saves) {
            if (RL_Shot(RL_SaveKey(s)) < 0) {
                RL_SaveShot(s);
                RL_Changed();
                return;
            }
        }
    }
    if (rl_mm.page == PAGE_HOST) {
        // Host's settings read the first time, the maps' photos made one a frame (the one picked first)
        if (!rl_host.ready) {
            RL_HostRead();
            RL_Changed();
        }
        if (!rl_host.map.empty() && RL_Shot(rl_host.map) < 0) {
            RL_ShotMake(rl_host.map);
            RL_Changed();
            return;
        }
        for (const std::string& map : rl_host.maps) {
            if (RL_Shot(map) < 0) {
                RL_ShotMake(map);
                RL_Changed();
                break;
            }
        }
    }
}

int UIReloadedMenu::MainDrawKey(void)
{
    if (m_screen == SCR_CONNECT) {
        // (the backdrop's photos and fade; while it connects, its step and the dash sweeping; the caret's blink)
        return rl_mm.serial * 31 + ((RL_BgFade() * 2 + rl_bg.cur) * 4099 + rl_bg.shader[0] * 31 + rl_bg.shader[1]) * 7
             + (rl_cn.end == CE_NONE ? clc.state * 65599 + cls.realtime / 16 : rl_cn.end == CE_PASSWORD ? (cls.realtime / 530) % 2 : 0);
    }
    if (m_back && m_screen == SCR_OPTIONS) {
        // (Options' layout: the crosshair's preview)
        return rl_mm.serial;
    }
    if (m_back) {
        // the photos (made, faded; Campaign's), the profile's page, Options' layout (the crosshair's preview)
        if (RL_BgCampaign()) {
            return rl_bg.made[2] * 31 + rl_bg.shader[2] * 7 + PAGE_CAMPAIGN;
        }
        return (RL_BgFade() * 2 + rl_bg.cur) * 4099 + rl_bg.shader[0] * 31 + rl_bg.shader[1] * 7 + rl_mm.page
             + (rl_mm.page == PAGE_OPTIONS ? rl_mm.serial * 131 : 0);
    }
    // (the caret's blink while typed in; in a game, DISCONNECT; the bindings, KEYS'; the vote running's)
    return rl_mm.serial * 31 + rl_sv.serial * 7 + (rl_mm.edit != F_NONE ? (cls.realtime / 530) % 2 : 0) * 3 + (clc.state > CA_PRIMED)
         + key_serial * 131 + (m_screen == SCR_OPTIONS && rl_over == RL_OVER_VOTE ? RL_VoteKey() * 257 : 0);
}
