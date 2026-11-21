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

// cl_uicomp.cpp: the Competitive HUD (Added, cg_hudstyle 1 or 2), Project: Omaha's
// design drawn by URC menus (ui/hud_comp_*.urc). What the menus show is
// published here as cvars (ui_hud_*) their labels link to; what changes in
// number or width has a widget: the kill feed, the chat and the game
// messages (HudFeed), the players' cards (HudCards), the player followed
// (HudFollow), the weapons carried (HudWeapons), the radar's map (HudRadar)
// and, with no radar, the compass's tracker (HudMates).

#include "client.h"
#include "cl_ui.h"
#include "cl_uicomp.h"
#include "cl_uiextras.h"
#include "cl_uimenus.h"
#include "../uilib/uibake.h"
#include "../qcommon/localization.h"

#include <atomic>
#include <new>
#include <thread>
#include <vector>

#define COMP_PICTURES "ui/competitive/"
#define COMP_OUTLINES "ui/outlines/weapons/"

// the teammates the cgame tracks (CL_InitRadar), and the player's own place in them
static radarClient_t *g_radarClients;
static int            g_radarClientNum;
#define COMP_CARD_H   64 // a team card's height: the score box's (hud_comp_score.urc)
#define COMP_CARD_W   64 // its most width (square; narrower for a bigger team)
#define COMP_LINES    8
#define COMP_KILLS    6

static cvar_t *cg_hudstyle;

const char *const ui_compHudMenus[UI_COMPHUD_MENUS] = {
    "hud_comp_topleft",     "hud_comp_topleft_compass", "hud_comp_score",      "hud_comp_killfeed", "hud_comp_chat",
    "hud_comp_strip",       "hud_comp_weapons",         "hud_comp_damage",     "hud_comp_bars",     "hud_comp_vote",
    "hud_comp_csgo_health", "hud_comp_csgo_ammo",       "hud_comp_follow",     "hud_comp_chattop",
};

// cg_hudstyle's presets, the bottom's health and ammo: 1 CS:GO's corners
// (the default), 2 CS2's strip along the middle (the team's badge between
// them), 3 Classic: CS:GO's with the chat along the top as the stock HUD's
// and the top bar in the top right corner; each menu's presets (a bit each,
// 1 << (preset - 1); 0 all's). The others' menus are hidden (cl_ui.cpp
// UI_CompHuds), not drawn; all are baked (a change of preset bakes nothing)
#define COMP_CSGO    1
#define COMP_CS2     2
#define COMP_CLASSIC 4
static const signed char comp_menuLayout[UI_COMPHUD_MENUS] = {
    0, 0, 0, 0, 0, COMP_CS2, 0, 0, 0, 0, COMP_CSGO | COMP_CLASSIC, COMP_CSGO | COMP_CLASSIC, 0, COMP_CLASSIC,
};

bool ui_compHudInLayout[UI_COMPHUD_MENUS];

// the elements one can turn off (as cgame's Competitive HUD); the menus' enabledcvar
static const char *const comp_toggles[] = {
    "cg_hud_radar", "cg_hud_killfeed", "cg_hud_chat", "cg_hud_messages", "cg_hud_health", "cg_hud_ammo",
    "cg_hud_grenades", "cg_hud_grenadecharge", "cg_hud_score", "cg_hud_timer", "cg_hud_alive", "cg_hud_teamicon",
    "cg_hud_badge", "cg_hud_targetinfo", "cg_hud_vote", "cg_hud_following", "cg_hud_attacker",
    "cg_hud_stopwatch", "cg_hud_damage",
};

// what the menus (ui/hud_comp_*.urc) read and this file sets
static const char *const comp_menuCvars[] = {
    "ui_hud_acc", "ui_hud_acc_line", "ui_hud_acc_radar", "ui_hud_acc_ring",
    "ui_hud_allies", "ui_hud_ammo", "ui_hud_ammo_on", "ui_hud_arc0_alpha", "ui_hud_arc0_angle", "ui_hud_arc1_alpha",
    "ui_hud_arc1_angle", "ui_hud_arc2_alpha", "ui_hud_arc2_angle", "ui_hud_axis", "ui_hud_badge_on", "ui_hud_charge",
    "ui_hud_charge_on", "ui_hud_clip", "ui_hud_compass_on", "ui_hud_corners", "ui_hud_count_allies",
    "ui_hud_count_axis", "ui_hud_count_on", "ui_hud_dmg_alpha", "ui_hud_dmg_angle", "ui_hud_follow",
    "ui_hud_follow_col", "ui_hud_follow_on", "ui_hud_health", "ui_hud_health_col", "ui_hud_health_frac",
    "ui_hud_health_on", "ui_hud_hurt", "ui_hud_hurt_col", "ui_hud_hurt_on", "ui_hud_jail_allies", "ui_hud_jail_axis",
    "ui_hud_jail_on", "ui_hud_msgs_c", "ui_hud_msgs_r",
    "ui_hud_radar_on", "ui_hud_score_l", "ui_hud_score_lc", "ui_hud_score_on", "ui_hud_score_r",
    "ui_hud_score_rc", "ui_hud_stopwatch", "ui_hud_stopwatch_on", "ui_hud_timer", "ui_hud_vote_on",
    "ui_hud_vote_prompt", "ui_hud_vote_tally", "ui_hud_vote_text", "ui_hud_vote_time", "ui_hud_weapons_on",
    "ui_hud_cards_allies", "ui_hud_cards_axis", "ui_hud_cards_on", "ui_hud_feed_under", "ui_hud_follow_team",
    "ui_hud_never", "ui_hud_classic", "ui_hud_chat_low", "ui_hud_kf_cs", "ui_hud_kf_beside", "ui_hud_kf_text",
    "ui_hud_kf_low",
};

// the accent (cg_hud_accent, the HUD's pale yellow by default) and its
// shades: each moves with the accent as much as it has of the accent's color
// (its spread between its brightest and darkest channel against the
// accent's), so a paler shade stays a paler shade of a new accent. The menus
// read them as ui_hud_acc* (a shape's $cvar, colorcvar)
enum { ACC_MAIN, ACC_LINE, ACC_RING, ACC_RADAR, ACC_WEAPON };

static const struct {
    const char *cvar;
    float       color[3]; // with the default accent (the first)
} comp_shades[] = {
    {"ui_hud_acc",        {0.965f, 0.965f, 0.478f}}, // health, ammo, the bullet, the grenades' counts, the weapon's name
    {"ui_hud_acc_line",   {0.933f, 0.941f, 0.659f}}, // the badge's lines, the ammo's separator
    {"ui_hud_acc_ring",   {0.91f, 0.922f, 0.643f} }, // the badge's ring
    {"ui_hud_acc_radar",  {0.863f, 0.894f, 0.643f}}, // the tracker's ring (the radar has none)
    {NULL,                {0.98f, 0.98f, 0.565f}  }, // the weapons carried
};

static float comp_accent[ARRAY_LEN(comp_shades)][3];
static int   comp_accentStamp;

typedef struct {
    int      time;
    char     text[160];
    float    color[3];
    qboolean bold;
    qboolean kill; // a kill printed as the stock HUD's, in the chat (Classic's along the top only)
} compLine_t;

typedef struct {
    int         time;
    char        killer[MAX_NAME_LENGTH], victim[MAX_NAME_LENGTH];
    int         killerTeam, victimTeam;
    const char *pic;
    float       picW, picH; // design pixels
    qboolean    suicide, headshot;
    qboolean    myKill, myDeath; // the player's kill (black, a red edge), the player's death (dark red)
} compKill_t;

static struct {
    compLine_t chat[COMP_LINES], msgs[COMP_LINES];
    int        numChat, numMsgs;
    compKill_t kills[COMP_KILLS];
    int        numKills;

    // who is alive (round modes): the clients' teams, the scoreboard's and
    // the kills' deaths
    int      team[MAX_CLIENTS];
    qboolean dead[MAX_CLIENTS];
    char     roundStart[64];
    int      sbCount;           // ui_hud_sb's modificationCount read
    int      sbScore[2];        // the scoreboard's teams' scores, -1 unknown
    int      scoreTime;         // the last silent score request
    qboolean wantScores, wasPlaying;
    char     cards[2][MAX_CLIENTS + 1]; // a card each: b alive with a bar, a alive, d dead

    // the weapons carried (names as the server's configstrings have them)
    char     primary[64], sidearm[64], pending[64];
    int      follow, loaded[2], frags, smokes, nation;
    char     wbRound[64];
    qboolean charging, chargeSecondary;
    int      chargeStart;

    // the damage's arcs
    int arcStat[3], arcTime[3];
    int lastDmg, lastHealth, flashTime;
} comp;

/*
====================================================================

WEAPONS AND KILLS

====================================================================
*/

enum { WK_PRIMARY, WK_SIDEARM, WK_FRAG, WK_SMOKE };
enum { NAT_ANY, NAT_US, NAT_UK, NAT_RU, NAT_DE, NAT_IT };

typedef struct {
    const char *name; // the weapon's configstring
    const char *pic;  // COMP_OUTLINES<pic>.svg
    int         kind, nation;
} compWeapon_t;

static const compWeapon_t comp_weapons[] = {
    {"M1 Garand",              "m1_garand",            WK_PRIMARY, NAT_US },
    {"Thompson",               "thompsonsmg",          WK_PRIMARY, NAT_US },
    {"BAR",                    "bar",                  WK_PRIMARY, NAT_ANY},
    {"Springfield '03 Sniper", "springfield",          WK_PRIMARY, NAT_ANY},
    {"Shotgun",                "shotgun",              WK_PRIMARY, NAT_ANY},
    {"Bazooka",                "bazooka",              WK_PRIMARY, NAT_ANY},
    {"Colt 45",                "colt45",               WK_SIDEARM, NAT_US },
    {"Hi-Standard Silenced",   "silencedpistol",       WK_SIDEARM, NAT_ANY},
    {"Frag Grenade",           "m2frag_grenade",       WK_FRAG,    NAT_US },
    {"M18 Smoke Grenade",      "m18_smoke_grenade",    WK_SMOKE,   NAT_ANY},
    {"Lee-Enfield",            "enfield",              WK_PRIMARY, NAT_UK },
    {"Sten Mark II",           "sten",                 WK_PRIMARY, NAT_UK },
    {"Enfield L42A1",          "uk_w_l42a1",           WK_PRIMARY, NAT_UK },
    {"DeLisle",                "delisle",              WK_PRIMARY, NAT_UK },
    {"Vickers-Berthier",       "uk_w_vickers",         WK_PRIMARY, NAT_UK },
    {"PIAT",                   "uk_w_piat",            WK_PRIMARY, NAT_UK },
    {"Webley Revolver",        "webley_revolver",      WK_SIDEARM, NAT_UK },
    {"Mills Grenade",          "mills_grenade",        WK_FRAG,    NAT_UK },
    {"Mosin Nagant Rifle",     "mosin_nagant_rifle",   WK_PRIMARY, NAT_RU },
    {"PPSH SMG",               "ppsh_smg",             WK_PRIMARY, NAT_RU },
    {"SVT 40",                 "svt_rifle",            WK_PRIMARY, NAT_RU },
    {"Nagant Revolver",        "nagant_revolver",      WK_SIDEARM, NAT_RU },
    {"Russian F1 Grenade",     "russian_f1_grenade",   WK_FRAG,    NAT_RU },
    {"RDG-1 Smoke Grenade",    "rdg-1_smoke_grenade",  WK_SMOKE,   NAT_RU },
    {"Mauser KAR 98K",         "kar98",                WK_PRIMARY, NAT_DE },
    {"KAR98 - Sniper",         "kar98sniper",          WK_PRIMARY, NAT_ANY},
    {"G 43",                   "g43",                  WK_PRIMARY, NAT_DE },
    {"MP40",                   "mp40",                 WK_PRIMARY, NAT_DE },
    {"StG 44",                 "mp44",                 WK_PRIMARY, NAT_DE },
    {"Panzerschreck",          "panzerschreck",        WK_PRIMARY, NAT_ANY},
    {"Gewehrgranate",          "kar98_mortar",         WK_PRIMARY, NAT_DE },
    {"Walther P38",            "p38",                  WK_SIDEARM, NAT_DE },
    {"Stielhandgranate",       "steilhandgranate",     WK_FRAG,    NAT_DE },
    {"Nebelhandgranate",       "nebelhandgranate",     WK_SMOKE,   NAT_DE },
    {"Carcano",                "it_w_carcano",         WK_PRIMARY, NAT_IT },
    {"Moschetto",              "it_w_moschetto",       WK_PRIMARY, NAT_IT },
    {"Breda",                  "it_w_breda",           WK_PRIMARY, NAT_IT },
    {"Beretta",                "it_w_beretta",         WK_SIDEARM, NAT_IT },
    {"Bomba A Mano",           "it_w_bomba",           WK_FRAG,    NAT_IT },
    {"Bomba A Mano Breda",     "it_w_bombabreda",      WK_SMOKE,   NAT_IT },
};

// a nation's sidearm, frag and smoke (by NAT_*)
static const char *const comp_issued[6][3] = {
    {"Colt 45",         "Frag Grenade",       "M18 Smoke Grenade"  },
    {"Colt 45",         "Frag Grenade",       "M18 Smoke Grenade"  },
    {"Webley Revolver", "Mills Grenade",      "M18 Smoke Grenade"  },
    {"Nagant Revolver", "Russian F1 Grenade", "RDG-1 Smoke Grenade"},
    {"Walther P38",     "Stielhandgranate",   "Nebelhandgranate"   },
    {"Beretta",         "Bomba A Mano",       "Bomba A Mano Breda" },
};

static const compWeapon_t *Comp_Weapon(const char *name)
{
    for (size_t i = 0; name && name[0] && i < ARRAY_LEN(comp_weapons); i++) {
        if (!Q_stricmp(name, comp_weapons[i].name)) {
            return &comp_weapons[i];
        }
    }
    return NULL;
}

// the name of the weapon in hand ("": none, the server's -1)
static const char *Comp_HandWeapon(void)
{
    const int w = cl.snap.ps.activeItems[ITEM_WEAPON];

    return w >= 0 && w < MAX_WEAPONS ? CL_ConfigString(CS_WEAPONS + w) : "";
}

const char *UI_CompHud_WeaponOutline(const char *name)
{
    const compWeapon_t *w = Comp_Weapon(name);

    return w ? va(COMP_OUTLINES "%s.svg", w->pic) : NULL;
}

// the obituaries' English keys (fgame's, and the stock games' own spellings:
// their global/localization.txt, Allied Assault's in Pak0, Breakthrough's
// petard in its pak4), the weapon each says: s2 the text after the attacker
// it needs ("" none at all), kind p a player's, s a suicide, w the world's.
// In order: a 'p' before any 's' or 'w' its words contain
typedef struct {
    const char *s1, *s2, *cls;
    char        kind;
} compPhrase_t;

static const compPhrase_t comp_phrases[] = {
    {"was gunned down by",             "",                             "pistol",   'p'},
    {"was rifled by",                  "",                             "rifle",    'p'},
    {"was sniped by",                  "",                             "sniper",   'p'},
    {"was perforated by",              "'s' SMG",                      "smg",      'p'},
    {"was perforated by",              "'s SMG",                       "smg",      'p'},
    {"was machine-gunned by",          "",                             "mg",       'p'},
    {"was hunted down by",             "",                             "shotgun",  'p'},
    {"was pumped full of buckshot by", "",                             "shotgun",  'p'},
    {"tripped on",                     "'s grenade",                   "grenade",  'p'},
    {"is picking",                     "'s shrapnel out of his teeth", "grenade",  'p'},
    {"took",                           "'s rocket right in the kisser", "rocket",  'p'},
    {"took",                           "'s rocket in the face",        "rocket",   'p'},
    {"stepped on",                     "'s landmine",                  "none",     'p'},
    {"was bashed by",                  "",                             "bash",     'p'},
    {"was clubbed by",                 "",                             "bash",     'p'},
    {"was shot by",                    "",                             "rifle",    'p'},
    {"was pushed over the edge by",    "",                             "none",     'p'},
    {"was burned up by",               "",                             "none",     'p'},
    {"was melted by",                  "",                             "none",     'p'},
    {"was knocked out by",             "",                             "none",     'p'},
    {"was run over by",                "",                             "none",     'p'},
    {"was impaled by",                 "",                             "none",     'p'},
    {"was blown away by",              "",                             "none",     'p'},
    {"was crushed by",                 "",                             "none",     'p'},
    {"was crushes by",                 "",                             "none",     'p'},
    {"was telefragged by",             "",                             "none",     'p'},
    {"was killed by",                  "",                             "none",     'p'},
    {"played catch with himself",      NULL,                           "grenade",  's'},
    {"tripped on his own grenade",     NULL,                           "grenade",  's'},
    {"rocketed himself",               NULL,                           "rocket",   's'},
    {"was hoist on his own pitard",    NULL,                           "none",     's'},
    {"was hoist on his own petard",    NULL,                           "none",     's'},
    {"took himself out of commision",  NULL,                           "none",     's'},
    {"blew himself up",                NULL,                           "none",     's'},
    {"shot himself",                   NULL,                           "none",     's'},
    {"caught some shrapnel",           NULL,                           "grenade",  'w'},
    {"caught a rocket",                NULL,                           "rocket",   'w'},
    {"stepped on a land mine",         NULL,                           "none",     'w'},
    {"cratered",                       NULL,                           "none",     's'},
    {"was burned to a crisp",          NULL,                           "none",     's'},
    {"was melted to nothing",          NULL,                           "none",     's'},
    {"blew up",                        NULL,                           "none",     'w'},
    {"was shot",                       NULL,                           "none",     'w'},
    {"died",                           NULL,                           "none",     's'},
};

// a weapon class's pictures and their sizes in design pixels, pointing left: the Allies', the Axis'
typedef struct {
    const char *cls, *allies, *axis;
    float       aw, ah, xw, xh;
} compKillPic_t;

// the kill feed's look (CS2's, as a 1080-line screen's design pixels): a
// row 36 tall, its names' capitals a third of it (the URC's face), the
// weapon a half; padding, gaps, the rows' spacing; the boxes' corners and
// the player's kill's red edge
#define KF_ROW    36.0f
#define KF_ICON   18.0f
#define KF_PAD    12.0f
#define KF_GAP    6.0f
#define KF_SPACE  3.0f
#define KF_RADIUS 3.0f
#define KF_EDGE   2.0f

// the chat's and the game messages' space between two lines (a wrapped
// line's rows touch)
#define FEED_GAP 2.0f

static const compKillPic_t comp_killPics[] = {
    {"rifle",   "m1_garand",      "kar98",            90.0f,  16.2f, 90.5f,  16.3f},
    {"pistol",  "colt45",         "p38",              33.9f,  21.9f, 33.9f,  22.2f},
    {"sniper",  "springfield",    "kar98sniper",      89.9f,  17.5f, 90.5f,  17.6f},
    {"smg",     "thompsonsmg",    "mp40",             75.1f,  24.2f, 74.8f,  26.0f},
    {"mg",      "bar",            "mp44",             95.5f,  15.3f, 70.9f,  26.0f},
    {"shotgun", "shotgun",        "shotgun",          85.0f,  15.9f, 85.0f,  15.9f},
    {"grenade", "m2frag_grenade", "steilhandgranate", 12.5f,  22.0f, 5.6f,   26.0f},
    {"rocket",  "bazooka",        "panzerschreck",    110.6f, 20.0f, 113.5f, 26.0f},
    {"bash",    "glove_50",              "glove_50",                20.0f,  20.0f, 20.0f,  20.0f},
    {"none",    "skull_50",              "skull_50",                20.0f,  20.0f, 20.0f,  20.0f},
};

/*
====================================================================

PLAYERS

====================================================================
*/

// what the HUD reads of the configstrings, parsed again only after one
// changed (UI_CompHud_ConfigString), not each frame
static struct {
    int      changes, parsed;
    int      gametype;
    qboolean floodProtect;
    char     map[MAX_QPATH];
    char     name[MAX_CLIENTS][MAX_NAME_LENGTH];
    int      team[MAX_CLIENTS]; // the "team" key's, -1 none (stock servers)
    int      objFlags[MAX_OBJECTIVES], objCurrent;
    vec3_t   objLoc[MAX_OBJECTIVES];
    qboolean teamScoresSent;
    int      teamScores[2]; // the server's (CS_TEAMSCORES)
} cs = {1};

void UI_CompHud_ConfigString(void)
{
    cs.changes++;
}

static void Comp_ConfigStrings(void)
{
    const char *info = CL_ConfigString(CS_SERVERINFO);

    if (cs.parsed == cs.changes) {
        return;
    }
    cs.parsed       = cs.changes;
    cs.gametype     = atoi(Info_ValueForKey(info, "g_gametype"));
    cs.floodProtect = atoi(Info_ValueForKey(info, "sv_floodProtect")) != 0;
    Q_strncpyz(cs.map, Info_ValueForKey(info, "mapname"), sizeof(cs.map));
    for (int i = 0; i < MAX_CLIENTS; i++) {
        const char *team = Info_ValueForKey(CL_ConfigString(CS_PLAYERS + i), "team");

        Q_strncpyz(cs.name[i], Info_ValueForKey(CL_ConfigString(CS_PLAYERS + i), "name"), sizeof(cs.name[i]));
        cs.team[i] = team[0] ? atoi(team) : -1;
    }
    for (int i = 0; i < MAX_OBJECTIVES; i++) {
        const char *o = CL_ConfigString(CS_OBJECTIVES + i);

        cs.objFlags[i] = atoi(Info_ValueForKey(o, "flags"));
        if (sscanf(Info_ValueForKey(o, "loc"), "%f %f %f", &cs.objLoc[i][0], &cs.objLoc[i][1], &cs.objLoc[i][2]) != 3) {
            cs.objFlags[i] = 0;
        }
    }
    cs.objCurrent = atoi(CL_ConfigString(CS_CURRENT_OBJECTIVE));
    cs.teamScoresSent = sscanf(CL_ConfigString(CS_TEAMSCORES), "%d %d", &cs.teamScores[0], &cs.teamScores[1]) == 2;
}

static int Comp_Gametype(void)
{
    Comp_ConfigStrings();
    return cs.gametype;
}

static const char *Comp_Name(int client)
{
    Comp_ConfigStrings();
    return client >= 0 && client < MAX_CLIENTS ? cs.name[client] : "";
}

// a player's team: the configstring's "team" (OpenMoHAA's servers), else
// the latest "scores" row's (stock servers: the stock scoreboard's source)
static int Comp_Team(int client)
{
    Comp_ConfigStrings();
    if (client < 0 || client >= MAX_CLIENTS) {
        return 0;
    }
    return cs.team[client] >= 0 ? cs.team[client] : UI_Menus_ScoreTeam(client);
}

// the one client called name, -1 none or more
static int Comp_FindName(const char *name)
{
    int found = -1;

    for (int i = 0; name[0] && i < MAX_CLIENTS; i++) {
        if (!strcmp(Comp_Name(i), name)) {
            if (found >= 0) {
                return -1;
            }
            found = i;
        }
    }
    return found;
}

static const char *Comp_TeamColor(int team)
{
    return team == TEAM_AXIS ? "0.8 0.533 0.533 1" : team == TEAM_ALLIES ? "0.533 0.569 0.8 1" : "1 1 1 1";
}

static void Comp_Trim(const char *src, int len, char *out, int size)
{
    while (len > 0 && *src == ' ') {
        src++;
        len--;
    }
    while (len > 0 && (src[len - 1] == ' ' || src[len - 1] == '\n')) {
        len--;
    }
    Q_strncpyz(out, src, Q_min(len + 1, size));
}

// a kill picture's size in the feed: its box, no taller than KF_ICON
static void Comp_KillIconSize(const compKillPic_t *p, qboolean axis, float *w, float *h)
{
    *w = axis ? p->xw : p->aw;
    *h = axis ? p->xh : p->ah;
    if (*h > KF_ICON) {
        *w *= KF_ICON / *h;
        *h = KF_ICON;
    }
}

/*
================
Comp_AddKill

A kill as the server printed it, "<victim> <s1> <attacker><s2>": its
phrase gives its weapon, the case of the line's color its victim's team
(lower, MESSAGE_CHAT_GREEN: ours). qfalse: no phrase matches, no kill
================
*/
static qboolean Comp_AddKill(const char *text, qboolean ourVictim)
{
    const compPhrase_t  *ph = NULL;
    const compKillPic_t *pic = &comp_killPics[0];
    compKill_t          *k;
    const char          *hit = NULL, *after, *end = NULL;
    int                  client;
    qboolean             axis;

    for (size_t i = 0; i < ARRAY_LEN(comp_phrases) && !ph; i++) {
        hit = Q_stristr(text, comp_phrases[i].s1);
        if (!hit) {
            continue;
        }
        after = hit + strlen(comp_phrases[i].s1);
        if (comp_phrases[i].kind == 'p' && comp_phrases[i].s2[0]) {
            end = Q_stristr(after, comp_phrases[i].s2);
            if (!end) {
                continue;
            }
        }
        ph = &comp_phrases[i];
    }
    if (!ph) {
        return qfalse;
    }
    if (comp.numKills == COMP_KILLS) {
        memmove(comp.kills, comp.kills + 1, sizeof(comp.kills[0]) * (COMP_KILLS - 1));
        comp.numKills--;
    }
    k = &comp.kills[comp.numKills++];
    memset(k, 0, sizeof(*k));
    k->time = cls.realtime;
    Comp_Trim(text, (int)(hit - text), k->victim, sizeof(k->victim));
    if (ph->kind == 'p') {
        after = hit + strlen(ph->s1);
        if (!end) {
            end = Q_stristr(after, " in the ");
        }
        Comp_Trim(after, end ? (int)(end - after) : (int)strlen(after), k->killer, sizeof(k->killer));
        k->headshot = end && (Q_stristr(end, " in the head") || Q_stristr(end, " in the helmet") || Q_stristr(end, " in the neck"));
    }
    k->suicide = ph->kind == 's';
    for (size_t i = 0; i < ARRAY_LEN(comp_killPics); i++) {
        if (!strcmp(comp_killPics[i].cls, ph->cls)) {
            pic = &comp_killPics[i];
        }
    }
    client        = Comp_FindName(k->victim);
    k->victimTeam = client >= 0 ? Comp_Team(client) : 0;
    if (!k->victimTeam && Comp_Gametype() >= GT_TEAM) {
        const int mine = cl.snap.ps.stats[STAT_TEAM];

        k->victimTeam = ourVictim == (mine != TEAM_AXIS) ? TEAM_ALLIES : TEAM_AXIS;
    }
    k->killerTeam = Comp_Team(Comp_FindName(k->killer));
    // the killer's weapon: of the victim's side without one
    axis    = (k->killer[0] ? k->killerTeam : k->victimTeam) == TEAM_AXIS;
    k->pic  = axis ? pic->axis : pic->allies;
    Comp_KillIconSize(pic, axis, &k->picW, &k->picH);
    // (a suicide is the player's death)
    k->myDeath = client == cl.snap.ps.clientNum;
    k->myKill  = !k->myDeath && k->killer[0] && Comp_FindName(k->killer) == cl.snap.ps.clientNum;
    if (client >= 0) {
        comp.dead[client] = qtrue;
    }
    return qtrue;
}

static void Comp_AddLine(compLine_t *lines, int *count, const char *text, float r, float g, float b, qboolean bold, qboolean kill = qfalse)
{
    compLine_t *l;

    if (*count == COMP_LINES) {
        memmove(lines, lines + 1, sizeof(lines[0]) * (COMP_LINES - 1));
        (*count)--;
    }
    l = &lines[(*count)++];
    Comp_Trim(text, (int)strlen(text), l->text, sizeof(l->text));
    for (char *p = l->text; *p; p++) {
        if (*p == '\n') {
            *p = ' ';
        }
    }
    l->time     = cls.realtime;
    l->color[0] = r;
    l->color[1] = g;
    l->color[2] = b;
    l->bold     = bold;
    l->kill     = kill;
}

void UI_CompHud_Print(const char *msg)
{
    switch ((byte)msg[0]) {
    case MESSAGE_YELLOW:
        Comp_AddLine(comp.msgs, &comp.numMsgs, msg + 1, 0.702f, 0.6f, 0.051f, qfalse);
        // a weapon picked up: what it is, before it is in hand
        if (!Q_stricmpn(msg + 1, "Picked Up ", 10)) {
            Comp_Trim(msg + 11, (int)strlen(msg + 11), comp.pending, sizeof(comp.pending));
        }
        break;
    case MESSAGE_WHITE:
        Comp_AddLine(comp.msgs, &comp.numMsgs, msg + 1, 1.0f, 1.0f, 1.0f, qtrue);
        break;
    case MESSAGE_CHAT_WHITE:
        // without OpenMoHAA servers' "(all) " (what isn't marked is to all,
        // as on stock servers); (team), (dead) and (private) kept
        Comp_AddLine(comp.chat, &comp.numChat, msg + 1 + (strncmp(msg + 1, "(all) ", 6) ? 0 : 6), 1.0f, 1.0f, 1.0f, qfalse);
        break;
    case MESSAGE_CHAT_RED:
    case MESSAGE_CHAT_GREEN:
        {
            const UColor&  c    = (byte)msg[0] == MESSAGE_CHAT_GREEN ? UGreenChatMessageColor : URedChatMessageColor;
            const qboolean text = Cvar_VariableIntegerValue("ui_hud_kf_text") != 0;

            // (a line no phrase matches: a game message in its color, as the stock HUD prints it; with the kills
            // as text, ui_hud_kf_text, every such line in the chat in its color, as the stock HUD's chat has it)
            if (!Comp_AddKill(msg + 1, (byte)msg[0] == MESSAGE_CHAT_GREEN) && !text) {
                Comp_AddLine(comp.msgs, &comp.numMsgs, msg + 1, c.r, c.g, c.b, qfalse);
            }
            if (text) {
                Comp_AddLine(comp.chat, &comp.numChat, msg + 1, c.r, c.g, c.b, qfalse, qtrue);
            }
        }
        break;
    }
}

void UI_CompHud_Clear(void)
{
    cs.changes++;
    memset(&comp, 0, sizeof(comp));
    comp.sbScore[0] = comp.sbScore[1] = -1;
    comp.lastDmg                      = -999999;
}

/*
====================================================================

THE FRAME: the ui_hud_* cvars

====================================================================
*/

// a cvar by name, found once: the names are the callers' literals, a table
// by their addresses
static cvar_t *Comp_Cvar(const char *name)
{
    static struct {
        const char *name;
        cvar_t     *cvar;
    } found[512];
    unsigned i = (unsigned)((uintptr_t)name >> 2) % ARRAY_LEN(found);

    while (found[i].name && found[i].name != name) {
        i = (i + 1) % ARRAY_LEN(found);
    }
    if (!found[i].name) {
        found[i].name = name;
        found[i].cvar = Cvar_Get(name, "", 0);
    }
    return found[i].cvar;
}

static int Comp_Int(const char *name)
{
    return Comp_Cvar(name)->integer;
}

// cg_hud_font read when it changed: the small text's files given to the
// baker; true when they changed (its faces to be baked again)
static bool Comp_HudFont(void)
{
    static int      read = -1;
    cvar_t         *cv   = Comp_Cvar("cg_hud_font");
    const rlFont_t *font;

    if (cv->modificationCount == read) {
        return false;
    }
    read = cv->modificationCount;
    font = RL_FindFont(cv->string);
    return UI_SetHudFont(font->medium, font->semibold);
}

static void Comp_Set(const char *name, const char *value)
{
    cvar_t *v = Comp_Cvar(name);

    if (strcmp(v->string, value)) {
        Cvar_Set(v->name, value);
    }
}

static void Comp_SetI(const char *name, int value)
{
    cvar_t *v = Comp_Cvar(name);

    if (v->integer != value || !v->string[0]) {
        Comp_Set(name, va("%d", value));
    }
}

static void Comp_SetF(const char *name, float value)
{
    cvar_t *v = Comp_Cvar(name);

    // (the text made only when the value changed: "%.3f")
    if (fabsf(v->value - value) >= 0.0005f || !v->string[0]) {
        const int k = (int)(fabsf(value) * 1000.0f + 0.5f);
        char      text[32];

        Com_sprintf(text, sizeof(text), "%s%d.%03d", value < 0 && k ? "-" : "", k / 1000, k % 1000);
        Comp_Set(name, text);
    }
}

// a color as "f6f67a", "#f6f67a" or "R G B" (0 to 1), to three decimals
static qboolean Comp_ParseColor(const char *text, float out[3])
{
    int n = 0;

    while (*text == ' ') {
        text++;
    }
    if (*text == '#') {
        text++;
    }
    while (isxdigit((byte)text[n])) {
        n++;
    }
    if (n == 6 && (!text[n] || text[n] == ' ')) {
        const unsigned long v = strtoul(text, NULL, 16);

        for (int k = 0; k < 3; k++) {
            out[k] = ((v >> (16 - 8 * k)) & 255) / 255.0f;
        }
    } else if (sscanf(text, "%f %f %f", &out[0], &out[1], &out[2]) != 3) {
        return qfalse;
    }
    for (int k = 0; k < 3; k++) {
        out[k] = roundf(Com_Clamp(0.0f, 1.0f, out[k]) * 1000.0f) / 1000.0f;
    }
    return qtrue;
}

// the accent's shades from cg_hud_accent (the default's when it can't be read)
static void Comp_Accent(void)
{
    const float *base = comp_shades[ACC_MAIN].color;
    const float  spread = Q_max(base[0], Q_max(base[1], base[2])) - Q_min(base[0], Q_min(base[1], base[2]));
    float        accent[3];

    if (!Comp_ParseColor(Comp_Cvar("cg_hud_accent")->string, accent)) {
        VectorCopy(base, accent);
    }
    for (size_t i = 0; i < ARRAY_LEN(comp_shades); i++) {
        const float *c     = comp_shades[i].color;
        const float  share = (Q_max(c[0], Q_max(c[1], c[2])) - Q_min(c[0], Q_min(c[1], c[2]))) / spread;

        for (int k = 0; k < 3; k++) {
            comp_accent[i][k] = roundf(Com_Clamp(0.0f, 1.0f, c[k] + share * (accent[k] - base[k])) * 1000.0f) / 1000.0f;
        }
        if (comp_shades[i].cvar) {
            Comp_Set(comp_shades[i].cvar, va("%g %g %g", comp_accent[i][0], comp_accent[i][1], comp_accent[i][2]));
        }
    }
    comp_accentStamp++;
}

// cg_hudstyle's menus shown (2 CS2's, 3 Classic's, else CS:GO's); the chat
// above the health in CS:GO's corners (ui_hud_corners), Classic's along the
// top (ui_hud_classic: the bottom's hidden, ui_hud_chat_low)
static void Comp_Layout(void)
{
    const int layout = cg_hudstyle->integer == 2 || cg_hudstyle->integer == 3 ? cg_hudstyle->integer : 1;

    for (size_t i = 0; i < UI_COMPHUD_MENUS; i++) {
        ui_compHudInLayout[i] = !comp_menuLayout[i] || (comp_menuLayout[i] & (1 << (layout - 1)));
    }
    Comp_Set("ui_hud_corners", layout != 2 ? "1" : "0");
    Comp_Set("ui_hud_classic", layout == 3 ? "1" : "0");
    Comp_Set("ui_hud_chat_low", layout != 3 ? "1" : "0");
}

// who is alive and the cards (round modes), and the silent score requests:
// a new round, a death, a team joined, and every 2 s without flood protection
static void Comp_Roster(int gametype)
{
    const playerState_t *ps      = &cl.snap.ps;
    const qboolean       playing = !(ps->pm_flags & PMF_SPECTATING) && ps->stats[STAT_HEALTH] > 0;
    const char          *roundStart = CL_ConfigString(CS_WARMUP);
    cvar_t              *sb = Comp_Cvar("ui_hud_sb");
    int                  i, t;

    if (strcmp(roundStart, comp.roundStart)) {
        Q_strncpyz(comp.roundStart, roundStart, sizeof(comp.roundStart));
        memset(comp.dead, 0, sizeof(comp.dead));
        comp.wantScores = qtrue;
    }
    // the scoreboard: "<allies score> <axis score> <a 0 or 1 a client: dead>"
    if (sb->modificationCount != comp.sbCount) {
        char dead[MAX_CLIENTS + 1] = "";

        comp.sbCount = sb->modificationCount;
        sscanf(sb->string, "%d %d %64s", &comp.sbScore[0], &comp.sbScore[1], dead);
        for (i = 0; dead[i] && i < MAX_CLIENTS; i++) {
            comp.dead[i] = dead[i] == '1';
        }
    }
    for (i = 0; i < MAX_CLIENTS; i++) {
        const int team = Comp_Team(i);

        if (team != comp.team[i]) {
            // a switch waits for the next round dead; a team the scores
            // just told (a stock server's) asks for no more
            comp.dead[i] = comp.team[i] && (team == TEAM_ALLIES || team == TEAM_AXIS);
            if (comp.team[i] || cs.team[i] >= 0) {
                comp.wantScores = qtrue;
            }
            comp.team[i] = team;
        }
    }
    // the players the snapshot shows (not our own entity: a dead player's
    // spectator is sent, not dead), and the player watched
    for (i = 0; i < cl.snap.numEntities; i++) {
        const entityState_t *es = &cl.parseEntities[(cl.snap.parseEntitiesNum + i) & (MAX_PARSE_ENTITIES - 1)];

        if (es->eType == ET_PLAYER && es->number >= 0 && es->number < MAX_CLIENTS && es->number != ps->clientNum) {
            comp.dead[es->number] = (es->eFlags & EF_DEAD) != 0;
        }
    }
    if (!(ps->pm_flags & PMF_SPECTATING)) {
        comp.dead[ps->clientNum] = ps->stats[STAT_HEALTH] <= 0;
    }
    if (comp.wasPlaying && !playing) {
        comp.wantScores = qtrue;
    }
    comp.wasPlaying = playing;
    if (gametype >= GT_TEAM && (comp.wantScores || !cs.floodProtect)
        && (unsigned)(cls.realtime - comp.scoreTime) >= 2000) {
        comp.wantScores = qfalse;
        comp.scoreTime  = cls.realtime;
        CL_AddReliableCommand("score", qfalse);
    }
    // in the clients' order, a dead player's card in its place; the bars
    // under the player's own team (both while spectating)
    for (t = 0; t < 2; t++) {
        const int side = t ? TEAM_AXIS : TEAM_ALLIES;
        const int own  = ps->stats[STAT_TEAM];
        const int bars = own == side || (own != TEAM_ALLIES && own != TEAM_AXIS);
        char     *c    = comp.cards[t];
        int       n    = 0;

        for (i = 0; i < MAX_CLIENTS; i++) {
            if (comp.team[i] == side) {
                c[n++] = comp.dead[i] ? 'd' : bars ? 'b' : 'a';
            }
        }
        c[n] = 0;
    }
}

// the weapons carried: the primary's and the sidearm's names (as seen in
// hand or picked up), the grenades, the grenade's charge
static void Comp_Weapons(const char *inHand)
{
    const playerState_t *ps       = &cl.snap.ps;
    const int            owned    = ps->stats[STAT_WEAPONS] & 0x3F;
    const int            equipped = ps->stats[STAT_EQUIPPED_WEAPON] & 0x3F;
    const compWeapon_t  *w        = Comp_Weapon(inHand);
    const qboolean       grenade  = (equipped & WEAPON_CLASS_GRENADE) != 0;
    const int            follow   = (ps->pm_flags & PMF_SPECTATING) ? ps->stats[STAT_INFOCLIENT] : -2;
    int                  frags = 0, smokes = 0, i;

    if (follow != comp.follow) {
        comp.primary[0] = comp.sidearm[0] = comp.pending[0] = 0;
        comp.loaded[0] = comp.loaded[1] = -1;
        comp.follow                     = follow;
    }
    if (w && w->nation != NAT_ANY) {
        comp.nation = w->nation;
    } else if (com_target_game->integer < TG_MOHTA || !comp.nation) {
        comp.nation = ps->stats[STAT_TEAM] == TEAM_AXIS ? NAT_DE : NAT_US;
    }
    if (Comp_Weapon(comp.pending)) {
        Q_strncpyz(Comp_Weapon(comp.pending)->kind == WK_SIDEARM ? comp.sidearm : comp.primary, comp.pending, 64);
        comp.pending[0] = 0;
    }
    if (w && w->kind == WK_PRIMARY) {
        Q_strncpyz(comp.primary, w->name, sizeof(comp.primary));
    } else if (w && w->kind == WK_SIDEARM) {
        Q_strncpyz(comp.sidearm, w->name, sizeof(comp.sidearm));
    }
    if (!(owned & ~(WEAPON_CLASS_PISTOL | WEAPON_CLASS_GRENADE))) {
        comp.primary[0] = 0;
    }
    if (!(owned & WEAPON_CLASS_PISTOL)) {
        comp.sidearm[0] = 0;
    } else if (!comp.sidearm[0]) {
        Q_strncpyz(comp.sidearm, comp_issued[comp.nation][0], sizeof(comp.sidearm));
    }

    // the grenades: the pouch's (by the ammo's name), and the one a grenade
    // keeps loaded, which a spawn or a round gives back. The name's
    // configstring: OpenMoHAA's servers send its number, stock 1.11 its
    // place among the weapons' (CS_WEAPONS + it; the two never overlap)
    for (i = 0; i < MAX_AMMO; i++) {
        static char kinds[MAX_CONFIGSTRINGS]; // 1 none, 2 smoke, 3 frag (0: not read since a change)
        static int  read = -1;
        const int   sent = ps->ammo_name_index[i];
        const int   idx  = sent > 0 && sent < MAX_WEAPONS ? CS_WEAPONS + sent : sent;

        Comp_ConfigStrings();
        if (read != cs.parsed) {
            memset(kinds, 0, sizeof(kinds));
            read = cs.parsed;
        }
        if (idx <= 0 || idx >= MAX_CONFIGSTRINGS) {
            continue;
        }
        if (!kinds[idx]) {
            const char *name = CL_ConfigString(idx);

            kinds[idx] = Q_stristr(name, "smoke") ? 2
                      : (Q_stristr(name, "grenade") && !Q_stristr(name, "riflegrenade")) || !Q_stricmp(name, "agrenade") ? 3 : 1;
        }
        if (kinds[idx] == 2) {
            smokes = Q_max(smokes, ps->ammo_amount[i]);
        } else if (kinds[idx] == 3) {
            frags = Q_max(frags, ps->ammo_amount[i]);
        }
    }
    if (ps->stats[STAT_HEALTH] <= 0 || (ps->pm_flags & PMF_RESPAWNED) || strcmp(comp.wbRound, CL_ConfigString(CS_WARMUP))) {
        comp.loaded[0] = comp.loaded[1] = -1;
        Q_strncpyz(comp.wbRound, CL_ConfigString(CS_WARMUP), sizeof(comp.wbRound));
    } else if (grenade) {
        comp.loaded[w && w->kind == WK_SMOKE] = ps->stats[STAT_CLIPAMMO] > 0;
    }
    // Allied Assault's start ammo loads one, Spearhead's and Breakthrough's none
    frags += comp.loaded[0] >= 0 ? comp.loaded[0] : (owned & WEAPON_CLASS_GRENADE) && com_target_game->integer < TG_MOHTA;
    smokes += Q_max(0, comp.loaded[1]);
    if (grenade) {
        // in hand, the ammo stat is the pouch and the loaded one
        int *n = w && w->kind == WK_SMOKE ? &smokes : &frags;

        *n = Q_max(*n, ps->stats[STAT_AMMO]);
    }
    comp.frags  = (owned & WEAPON_CLASS_GRENADE) ? frags : 0;
    comp.smokes = (owned & WEAPON_CLASS_GRENADE) ? smokes : 0;

    // the charge: the charge animation's rising edge with a grenade in hand
    if (grenade && ps->iViewModelAnim == VM_ANIM_CHARGE) {
        if (!comp.charging) {
            const usercmd_t *cmd = &cl.cmds[cl.cmdNumber & CMD_MASK];

            comp.charging        = qtrue;
            comp.chargeStart     = cl.serverTime;
            comp.chargeSecondary = (cmd->buttons & BUTTON_ATTACKRIGHT) && !(cmd->buttons & BUTTON_ATTACKLEFT);
        }
    } else {
        comp.charging = qfalse;
    }
}

// text broken between words into lines of a width (design units), up to
// lines of them
static str Comp_Wrap(UIFont *font, const char *p, float width, int lines)
{
    str text;

    for (int line = 0; *p && line < lines; line++) {
        const char *brk = NULL, *q = p;

        for (; *q; q++) {
            if (*q == ' ') {
                brk = q;
            }
            if (q > p && font->getWidth(p, (int)(q - p + 1)) > width) {
                break;
            }
        }
        if (*q && brk && brk > p && line + 1 < lines) {
            q = brk;
        }
        text += str(p, 0, (int)(q - p));
        text += line + 1 < lines && *q ? "\n" : "";
        for (p = q; *p == ' '; p++) {
        }
    }
    return text;
}

/*
================
Comp_RadarSize

cg_hud_radar_size (0.5 to 1.5): the radar's layers (hud_comp_topleft: its
disc and map, the damage's wedge) their URC's size times it from
the same top left, the game messages under it (hud_comp_topleft_compass)
moved down by what it grew. The same ground shown at any size (HudRadar: its
range is to its edge, its marks sized with it). Set again when the menus
are loaded again
================
*/
static void Comp_RadarSize(void)
{
    static const char *const layers[2] = {"radar", "radarwedge"};
    static int               generation = -1, read = -1;
    static UIWidget         *widgets[3]; // the layers, the messages
    static UIRect2D          urc[3];
    cvar_t                  *cv = Comp_Cvar("cg_hud_radar_size");
    float                    k;

    if (generation != menuManager.Generation()) {
        Menu     *radar    = menuManager.FindMenu("hud_comp_topleft");
        Menu     *messages = menuManager.FindMenu("hud_comp_topleft_compass");
        UIWidget *rc       = radar ? radar->GetContainerWidget() : NULL;
        UIWidget *mc       = messages ? messages->GetContainerWidget() : NULL;

        generation = menuManager.Generation();
        read       = -1;
        for (int i = 0; i < 3; i++) {
            widgets[i] = i < 2 ? (rc ? rc->FindWidget(layers[i]) : NULL) : (mc ? mc->FindWidget("messages") : NULL);
            if (widgets[i]) {
                urc[i] = widgets[i]->DesignRect();
            }
        }
    }
    if (cv->modificationCount == read || !widgets[0]) {
        return;
    }
    read = cv->modificationCount;
    k    = Com_Clamp(0.5f, 1.5f, cv->value);
    for (int i = 0; i < 2; i++) {
        const UIRect2D& r = urc[i];

        if (widgets[i]) {
            widgets[i]->SetDesignRect(UIRect2D(r.pos.x, r.pos.y, r.size.width * k, r.size.height * k));
        }
    }
    if (widgets[2]) {
        const UIRect2D& r    = urc[2];
        const float     down = urc[0].size.height * (k - 1.0f);

        widgets[2]->SetDesignRect(UIRect2D(r.pos.x, r.pos.y + down, r.size.width, r.size.height));
    }
}

// Classic's places (cg_hudstyle 3; else the files' own): the top bar
// (hud_comp_score: the timer and the scores; no cards, counts or jail) in
// the top right corner, 25 from the screen's top and right as the radar
// from its top and left (the kill feed by it: hud_comp_killfeed.urc); the
// chat (hud_comp_chattop) along the top right of the radar or the compass,
// as the stock HUD's beside its compass
#define COMP_CLASSIC_MARGIN 25.0f
static void Comp_ClassicPlace(void)
{
    static int                     generation = -1;
    static std::vector<UIWidget *> widgets;
    static std::vector<UIRect2D>   urc;
    static UIWidget               *chat;
    static UIRect2D                chatUrc;
    static float                   placed[3] = {-1.0f, -1.0f, -1.0f}; // the bar's shift across and down, the chat's left
    const bool                     classic   = Comp_Int("ui_hud_classic") != 0;
    const float                    screenW   = uid.vidWidth / UI_DesignScale(1920.0f, 1080.0f);
    float                          dx, dy, x;

    if (generation != menuManager.Generation()) {
        Menu     *score = menuManager.FindMenu("hud_comp_score");
        Menu     *top   = menuManager.FindMenu("hud_comp_chattop");
        UIWidget *c     = score ? score->GetContainerWidget() : NULL;

        generation = menuManager.Generation();
        widgets.clear();
        urc.clear();
        for (UIWidget *w = c ? c->getFirstChild() : NULL; w; w = c->getNextChild(w)) {
            widgets.push_back(w);
            urc.push_back(w->DesignRect());
        }
        chat = top ? top->GetNamedWidget("chat") : NULL;
        if (chat) {
            chatUrc = chat->DesignRect();
        }
        placed[0] = placed[1] = placed[2] = -1.0f;
    }
    // the bar: the box's right edge (the file's 1006, the menu's middle 960) and its top (0) the margin from the screen's
    dx = classic ? floorf(960.0f + screenW * 0.5f - COMP_CLASSIC_MARGIN - 1006.0f) : 0.0f;
    dy = classic ? COMP_CLASSIC_MARGIN : 0.0f;
    if (dx != placed[0] || dy != placed[1]) {
        placed[0] = dx;
        placed[1] = dy;
        for (size_t i = 0; i < widgets.size(); i++) {
            const UIRect2D& r = urc[i];

            widgets[i]->SetDesignRect(UIRect2D(r.pos.x + dx, r.pos.y + dy, r.size.width, r.size.height));
        }
    }
    // the chat: 25 right of the radar (its size: cg_hud_radar_size) or the compass
    if (Comp_Int("ui_hud_radar_on")) {
        x = 25.0f + 248.0f * Com_Clamp(0.5f, 1.5f, Comp_Cvar("cg_hud_radar_size")->value) + 25.0f;
    } else {
        x = Comp_Int("ui_hud_compass_on") ? 141.0f + 25.0f : 25.0f;
    }
    if (chat && x != placed[2]) {
        placed[2] = x;
        chat->SetDesignRect(UIRect2D(x, chatUrc.pos.y, chatUrc.size.width, chatUrc.size.height));
    }
}

static const char *const comp_arcAlpha[3] = {"ui_hud_arc0_alpha", "ui_hud_arc1_alpha", "ui_hud_arc2_alpha"};
static const char *const comp_arcAngle[3] = {"ui_hud_arc0_angle", "ui_hud_arc1_angle", "ui_hud_arc2_angle"};
// the teammates the radar and the tracker show (g_radarClients, the
// cgame's: never enemies): not oneself nor the player watched (through
// their eyes the entry is the view's own place, from behind the camera's
// ahead: STAT_INFOCLIENT, which playing is the player aimed at), not the
// dead, not a place no longer sent. The cgame's entries are of the snapshot
// (0-1 snapshots old) or, on Spearhead's and Breakthrough's protocol, also
// of the server's radar (a teammate out of sight a snapshot in turn); it
// keeps a last seen place 5 s
typedef struct {
    int followed, fresh;
} compMates_t;

static compMates_t Comp_Mates(void)
{
    const playerState_t *ps = &cl.snap.ps;

    return {(ps->pm_flags & PMF_SPECTATING) ? ps->stats[STAT_INFOCLIENT] : -1, com_target_game->integer < TG_MOHTA ? 250 : 2000};
}

static bool Comp_MateShown(const compMates_t& m, int i)
{
    const radarClient_t *c = &g_radarClients[i];

    return i != g_radarClientNum && i != cl.snap.ps.clientNum && i != m.followed && !comp.dead[i] && c->time
        && cl.snap.serverTime - c->time < m.fresh;
}

// what the stock compass points at (the compass with no radar, HudMates):
// shown, an objective (not a teammate), its place from this frame's view
// (units right and ahead)
static struct {
    qboolean on, objective;
    float    right, ahead;
} comp_mate;

void UI_CompHud_Frame(void)
{
    const playerState_t *ps       = &cl.snap.ps;
    const int            gametype = Comp_Gametype();
    const qboolean       teams    = gametype >= GT_TEAM;
    const qboolean       rounds   = gametype == GT_TEAM_ROUNDS || gametype == GT_OBJECTIVE;
    const qboolean       spectating = (ps->pm_flags & PMF_SPECTATING) != 0;
    // following a player from behind (the camera view) or through their eyes
    // (g_spectatefollow_firstperson: the snapshot is that player's number)
    const qboolean       following = spectating && ((ps->pm_flags & PMF_CAMERA_VIEW) || ps->clientNum != clc.clientNum)
                                && ps->stats[STAT_INFOCLIENT] >= 0 && ps->stats[STAT_INFOCLIENT] < MAX_CLIENTS;
    // Added in OPM
    //  The player a demo shows from behind, or from the eyes of one it doesn't
    //  show (CL_DemoShownClient): named and badged as one followed, with the
    //  health and ammo the client put in the snapshot for them; a demo
    //  shows no score, time left or stopwatch, nor who hurt the recorder
    //  while it shows someone else or the recorder spectates
    const int            demoShown = CL_DemoShownClient();
    const char *weapon = Comp_HandWeapon();
    // the ammo shown for what has a clip or ammo: not for an item with
    // neither (the binoculars, the papers: -1 and 0), as the stock HUD
    const bool  ammo   = weapon[0] && (ps->stats[STAT_CLIPAMMO] >= 0 || ps->stats[STAT_AMMO] > 0);
    const int   team   = ps->stats[STAT_TEAM];
    const int   attacker = ps->stats[STAT_ATTACKERCLIENT];
    vec3_t      angles;
    float       viewYaw;
    int         health = ps->stats[STAT_HEALTH], maxHealth = Q_max(1, ps->stats[STAT_MAXHEALTH]);
    int         i;

    static int  lastSnap = -1, lastChange = -1, lastFull;

    if (!cl.snap.valid) {
        return;
    }
    // cg_hud_font: the small text's faces baked again in it
    if (Comp_HudFont()) {
        UI_CompHud_PreBake(qfalse);
    }
    // cg_hud_scale: the menus at the new size, their faces and shapes baked again
    {
        static int  read = -1;
        cvar_t     *cv = Comp_Cvar("cg_hud_scale");

        if (cv->modificationCount != read) {
            const char *v = cv->string;
            char       *end;
            float       scale = strtof(v, &end);

            read  = cv->modificationCount;
            scale = end == v || scale != scale ? 1.0f : Com_Clamp(0.25f, 2.0f, scale);
            if (UI_SetDesignScale(scale)) {
                menuManager.RealignMenus();
                UI_CompHud_PreBake(qfalse);
            }
        }
    }
    // cg_hud_radar_size: the radar's size, the messages under it
    Comp_RadarSize();
    // cg_hud_accent: the shades again (what reads them is drawn and baked
    // again), the health's color with them
    {
        static int read = -1;

        if (Comp_Cvar("cg_hud_accent")->modificationCount != read) {
            read = Comp_Cvar("cg_hud_accent")->modificationCount;
            Comp_Accent();
            lastChange = -1;
        }
    }
    // cg_hudstyle's preset: the menus shown
    {
        static int read = -1;

        if (cg_hudstyle->modificationCount != read) {
            read = cg_hudstyle->modificationCount;
            Comp_Layout();
            // (the zoom overlays' masks begun, unless made: turned on mid-match)
            UI_Extras_PreBake(qtrue);
        }
    }
    cge->CG_EyeAngles(&angles);
    viewYaw = angles[YAW];

    // what comes with a snapshot or a configstring: made again with a new
    // one, and every 100 ms (the toggles, the keys); the view's and time's each frame
    const bool full = cl.snap.messageNum != lastSnap || cs.changes != lastChange || (unsigned)(cls.realtime - lastFull) >= 100;

    if (full) {
    lastSnap   = cl.snap.messageNum;
    lastChange = cs.changes;
    lastFull   = cls.realtime;
    Comp_Roster(gametype);
    Comp_Weapons(weapon);

    // the strip: health, badge, ammo, weapons (not while free spectating); the
    // badge the followed player's team (from behind the snapshot's is ours)
    {
        const qboolean icon =
            teams && (!spectating || following || demoShown >= 0) && Comp_Int("cg_hud_badge") && Comp_Int("cg_hud_teamicon");
        const int shown = demoShown >= 0 ? Comp_Team(demoShown) : following ? Comp_Team(ps->stats[STAT_INFOCLIENT]) : team;

        Comp_Set("ui_hud_allies", icon && shown == TEAM_ALLIES ? "1" : "0");
        Comp_Set("ui_hud_axis", icon && shown == TEAM_AXIS ? "1" : "0");
    }
    if (following && demoShown < 0 && !(ps->stats[STAT_WEAPONS] & 0x3F) && ps->stats[STAT_INFOCLIENT_HEALTH] > 0) {
        health    = ps->stats[STAT_INFOCLIENT_HEALTH];
        maxHealth = 100;
    }
    Comp_SetI("ui_hud_health", health);
    Comp_SetF("ui_hud_health_frac", Com_Clamp(0.0f, 1.0f, (float)health / maxHealth));
    Comp_Set("ui_hud_health_col", health < Comp_Cvar("cg_hud_lowhealth")->value * maxHealth ? "0.753 0.188 0.129 1" : Comp_Cvar("ui_hud_acc")->string);
    Comp_SetI("ui_hud_clip", ps->stats[STAT_CLIPAMMO]);
    Comp_SetI("ui_hud_ammo", ps->stats[STAT_AMMO]);

    // the score box: Allies | Axis under the timer, or self | the leader
    // ("-" while a score isn't known; a score can be below 0). The teams'
    // scores: the server's (CS_TEAMSCORES, a server of this code); else our
    // team's and the highest (the server's stats, every frame), the
    // scoreboard's (asked for at times, -1 not read yet) for the other team's
    // below ours, and 0 for it in round modes while both are 0 (a team's
    // score there is its rounds won)
    {
        int       score[2] = {ps->stats[STAT_KILLS], ps->stats[STAT_HIGHEST_SCORE]};
        qboolean  known[2] = {qtrue, qtrue};

        if (teams && cs.teamScoresSent) {
            score[0] = cs.teamScores[0];
            score[1] = cs.teamScores[1];
        } else if (teams) {
            const int mine = team == TEAM_AXIS;

            score[0] = comp.sbScore[0];
            score[1] = comp.sbScore[1];
            known[0] = score[0] >= 0;
            known[1] = score[1] >= 0;
            if (team == TEAM_ALLIES || team == TEAM_AXIS) {
                score[mine] = ps->stats[STAT_KILLS];
                known[mine] = qtrue;
                if (ps->stats[STAT_HIGHEST_SCORE] > ps->stats[STAT_KILLS]) {
                    score[!mine] = ps->stats[STAT_HIGHEST_SCORE];
                    known[!mine] = qtrue;
                } else if (gametype >= GT_TEAM_ROUNDS && !ps->stats[STAT_HIGHEST_SCORE]) {
                    score[!mine] = 0;
                    known[!mine] = qtrue;
                }
            }
        }
        Comp_Set("ui_hud_score_l", known[0] ? va("%d", score[0]) : "-");
        Comp_Set("ui_hud_score_r", known[1] ? va("%d", score[1]) : "-");
        Comp_Set("ui_hud_score_lc", teams ? "0.588 0.729 0.871 1" : "0.922 0.933 0.949 1");
        Comp_Set("ui_hud_score_rc", teams ? "0.902 0.478 0.408 1" : "0.922 0.933 0.949 1");
        // the players' cards, or (cg_hud_alivecount, CS2's) how many of each
        // team are alive in their place: no cards then (ui_hud_cards_on
        // says whether either shows; Classic's top bar has neither)
        const qboolean count = rounds && Comp_Int("cg_hud_alivecount");

        Comp_Set("ui_hud_cards_allies", rounds && !count ? comp.cards[0] : "");
        Comp_Set("ui_hud_cards_axis", rounds && !count ? comp.cards[1] : "");
        Comp_Set("ui_hud_count_on", count ? "1" : "0");
        for (i = 0; i < 2; i++) {
            int alive = 0;

            for (const char *c = comp.cards[i]; *c; c++) {
                alive += *c != 'd';
            }
            Comp_SetI(i ? "ui_hud_count_axis" : "ui_hud_count_allies", alive);
        }
        // Liberation's jail: how many of each team are locked up or dead, of
        // how many, in the count tiles' places (the map script's huddraw text,
        // global/lib_dm.scr's: "jailed(all)", Axis 101, Allies 102, its lock
        // 105; the cgame doesn't draw them then: CG_HudDrawElements)
        {
            const hdelement_t *e = cls.HudDrawElements;
            int                j[4];
            const qboolean     jail = gametype == GT_LIBERATION && !Q_stricmp(e[105].shaderName, "textures/hud/lock_icon")
                               && sscanf(e[102].string, "%d(%d)", &j[0], &j[1]) == 2 && sscanf(e[101].string, "%d(%d)", &j[2], &j[3]) == 2;

            Comp_Set("ui_hud_jail_on", jail && !Comp_Int("ui_hud_classic") ? "1" : "0");
            if (jail) {
                Comp_Set("ui_hud_jail_allies", va("%d/%d", j[0], j[1]));
                Comp_Set("ui_hud_jail_axis", va("%d/%d", j[2], j[3]));
            }
        }
    }
    }

    // the timer: its seconds
    {
        static int  shown = -2;
        const int   matchEnd = atoi(CL_ConfigString(CS_MATCHEND));
        const int   left = atoi(CL_ConfigString(CS_WARMUP)) != -1 && matchEnd && gametype != GT_LIBERATION && matchEnd > cl.serverTime
                         ? (matchEnd - cl.serverTime) / 1000 : -1;

        if (left != shown || !Comp_Cvar("ui_hud_timer")->modificationCount) {
            shown = left;
            Comp_Set("ui_hud_timer", left >= 0 && !clc.demoplaying ? va("%d:%02d", left / 60, left % 60) : "");
        }
    }

    // the stopwatch (Omaha's bar outlasts the wait by a quarter second) and the grenade's charge
    {
        const stopWatch_t *sw = &cls.stopwatch;
        float              frac = 0.0f;

        if (sw->iStartTime && sw->iStartTime < sw->iEndTime && ps->stats[STAT_HEALTH] > 0) {
            frac = sw->eType >= SWT_FUSE_WET ? (sw->iEndTime > cl.serverTime)
                 : Com_Clamp(0.0f, 1.0f, (float)(sw->iEndTime + 250 - cl.serverTime) / (sw->iEndTime - sw->iStartTime + 250));
        }
        Comp_SetF("ui_hud_stopwatch", frac);
        Comp_Set("ui_hud_stopwatch_on", frac > 0.0f && Comp_Int("cg_hud_stopwatch") && !clc.demoplaying ? "1" : "0");
        if (comp.charging) {
            // the stock scripts' charge: 0.6 to 4 s, 0.5 to 1.5 for the secondary throw
            const float lo = comp.chargeSecondary ? 0.5f : 0.6f, hi = comp.chargeSecondary ? 1.5f : 4.0f;

            Comp_SetF("ui_hud_charge", Com_Clamp(0.0f, 1.0f, ((cl.serverTime - comp.chargeStart) / 1000.0f - lo) / (hi - lo)));
        }
    }

    // who you follow, who last hurt you (the player aimed at: the cgame's tag
    // over their head, UI_DrawTeamTag; who killed you: the kill feed)
    if (full) {
        const int      info   = demoShown >= 0 ? demoShown : ps->stats[STAT_INFOCLIENT];
        const qboolean named  = following || demoShown >= 0;
        const int      fteam  = named ? Comp_Team(info) : 0;
        const qboolean hurtOk = !clc.demoplaying || (!spectating && demoShown < 0);

        Comp_Set("ui_hud_follow", named ? Comp_Name(info) : "");
        Comp_Set("ui_hud_follow_col", named ? Comp_TeamColor(Comp_Team(info)) : "1 1 1 1");
        Comp_Set("ui_hud_follow_team", fteam == TEAM_AXIS ? "axis" : fteam == TEAM_ALLIES ? "allies" : "");
        Comp_Set("ui_hud_hurt", hurtOk && attacker >= 0 && attacker < MAX_CLIENTS ? Comp_Name(attacker) : "");
        Comp_Set("ui_hud_hurt_col", Comp_TeamColor(Comp_Team(attacker)));
    }

    // a vote
    if (full) {
        const int voteTime = atoi(CL_ConfigString(CS_VOTE_TIME));

        if (voteTime) {
            Comp_Set("ui_hud_vote_time", va("%ds", Q_max(0, (30000 - (cl.serverTime - voteTime)) / 1000 + 1)));
            static UIFont *font, *small;
            const float    vs[2] = {UI_DesignScale(1920.0f, 1080.0f), UI_DesignScale(1920.0f, 1080.0f)};

            if (!font) {
                font  = new UIFont("oswald-medium-16s");
                small = new UIFont("oswald-medium-12s");
            }
            font->setVirtualScale(vs);
            small->setVirtualScale(vs);
            // the subject and the prompt wrapped in the panel (up to six lines each)
            Comp_Set("ui_hud_vote_text", Comp_Wrap(font, CL_ConfigString(CS_VOTE_STRING), 252.0f, 6));
            Comp_Set("ui_hud_vote_tally", va("%s  %s     %s  %s", Sys_LV_CL_ConvertString("Yes"), CL_ConfigString(CS_VOTE_YES),
                                             Sys_LV_CL_ConvertString("No"), CL_ConfigString(CS_VOTE_NO)));
            Comp_Set("ui_hud_vote_prompt", ps->voted ? "" : Comp_Wrap(small, Sys_LV_CL_ConvertString("To vote Yes, press F1. To vote No, press F2."), 252.0f, 6));
        }
    }

    // the damage: three crescents round the crosshair, a hit renewing the one
    // within 30 degrees or the oldest, 1.5 s each; the radar's and compass's
    // wedge. cg_hud_damage 0 (Options' HUD tab): no crescents, in every
    // frame (the wedge stays)
    {
        const int      dmg   = ps->stats[STAT_DAMAGEDIR];
        const qboolean shown = Comp_Int("cg_hud_damage") != 0;

        if (dmg != comp.lastDmg || (dmg > 0 && ps->stats[STAT_HEALTH] < comp.lastHealth)) {
            int slot = 0;

            for (i = 0; i < 3; i++) {
                if (comp.arcTime[i] && fabs(AngleSubtract(dmg / 10.0f, comp.arcStat[i] / 10.0f)) < 30.0f) {
                    slot = i;
                    break;
                }
                if (comp.arcTime[i] < comp.arcTime[slot]) {
                    slot = i;
                }
            }
            if (comp.lastDmg != -999999) {
                comp.arcStat[slot] = dmg;
                comp.arcTime[slot] = comp.flashTime = cls.realtime;
            }
        }
        comp.lastDmg    = dmg;
        comp.lastHealth = ps->stats[STAT_HEALTH];
        // (a world yaw's angle on screen, clockwise from up the view: the view's yaw less it)
        for (i = 0; i < 3; i++) {
            const int age = cls.realtime - comp.arcTime[i];

            Comp_SetF(comp_arcAlpha[i], shown && comp.arcTime[i] && age < 1500 ? 1.0f - age / 1500.0f : 0.0f);
            if (shown && comp.arcTime[i] && age < 1500) {
                Comp_SetF(comp_arcAngle[i], AngleSubtract(viewYaw, comp.arcStat[i] / 10.0f + 180.0f));
            }
        }
        Comp_SetF("ui_hud_dmg_alpha", dmg > 0 && comp.flashTime ? Com_Clamp(0.0f, 1.0f, 1.0f - (cls.realtime - comp.flashTime) / 1500.0f) : 0.0f);
        if (Comp_Cvar("ui_hud_dmg_alpha")->value > 0.0f) {
            Comp_SetF("ui_hud_dmg_angle", AngleSubtract(viewYaw, dmg / 10.0f + 180.0f));
        }
    }

    // the elements' flags (enabledcvar): shown and turned on
    if (full) {
        // through the intermission (and with no HUD) only the messages, the
        // chat, the kills and the vote (cl_ui.cpp: the other menus hidden)
        const qboolean show  = !(ps->pm_flags & (PMF_NO_HUD | PMF_INTERMISSION));
        const qboolean strip = (!spectating || following || demoShown >= 0) && show;
        const qboolean radar = Comp_Int("cg_hud_radar") != 0;
        const qboolean msgs  = Comp_Int("cg_hud_messages") != 0;
        auto           flag  = [](const char *name, qboolean on) { Comp_Set(name, on ? "1" : "0"); };

        flag("ui_hud_radar_on", radar && show);
        flag("ui_hud_compass_on", !radar && Comp_Int("ui_compass") && show);
        flag("ui_hud_msgs_r", msgs && radar);
        flag("ui_hud_msgs_c", msgs && !radar);
        flag("ui_hud_score_on", Comp_Int("cg_hud_score") && gametype != GT_SINGLE_PLAYER && !clc.demoplaying);
        // Classic's kills (cg_hud_classic_kills): 0 the stock HUD's lines in the chat, 1 the feed beside the
        // top bar, 2 under it; else the feed in its place
        const qboolean classic = Comp_Int("ui_hud_classic") != 0;
        const int      kills   = classic ? Comp_Int("cg_hud_classic_kills") : -1;

        flag("ui_hud_kf_text", kills == 0);
        flag("ui_hud_kf_cs", kills != 0);
        flag("ui_hud_kf_beside", kills == 1);
        flag("ui_hud_kf_low", kills == 2);
        flag("ui_hud_cards_on", rounds && Comp_Int("cg_hud_alive") && !classic);
        // the kill feed under the cards where the screen has no room for both in a row (a 4:3 screen, a large
        // cg_hud_scale): the cards' box ends 476 right of the middle, the feed's is the last 445
        flag(
            "ui_hud_feed_under",
            rounds && Comp_Int("cg_hud_alive") && !Comp_Int("cg_hud_alivecount") && !classic
                && uid.vidWidth / UI_DesignScale(1920.0f, 1080.0f) < 2 * (476.0f + 445.0f)
        );
        flag("ui_hud_health_on", strip && Comp_Int("cg_hud_health") && !CL_DemoHidesHealth());
        flag("ui_hud_badge_on", strip && Comp_Int("cg_hud_badge"));
        flag("ui_hud_ammo_on", strip && ammo && Comp_Int("cg_hud_ammo"));
        flag("ui_hud_weapons_on", strip && Comp_Int("cg_hud_ammo"));
        flag("ui_hud_follow_on", (following || demoShown >= 0) && Comp_Int("cg_hud_following") && show);
        // (not while you lie dead: the kill feed has who killed you)
        flag("ui_hud_hurt_on", Comp_Cvar("ui_hud_hurt")->string[0] && Comp_Int("cg_hud_attacker") && show && (health > 0 || spectating));
        flag("ui_hud_charge_on", comp.charging && Comp_Int("cg_hud_grenadecharge"));
        flag("ui_hud_vote_on", atoi(CL_ConfigString(CS_VOTE_TIME)) && Comp_Int("cg_hud_vote"));
        if (!Comp_Int("cg_hud_stopwatch")) {
            Comp_Set("ui_hud_stopwatch", "0");
        }
        Comp_ClassicPlace();
    }

    // the compass: what the stock compass points at, and its distance. The
    // server points it at the current objective (a map that sets one: its
    // location is the objective's configstring) or, on Allied Assault's
    // servers, at the nearest living teammate; STAT_OBJECTIVECENTER is its
    // bearing from the view (+ 180, tenths of a degree), LEFT and RIGHT that
    // bearing -+ 60 - atan(300 / distance) degrees (at least 7), so the gap
    // gives the distance; the game's own 52 units a metre (the battle
    // language's; the gap's least, 7 degrees, is 4.3 m or nearer). Its
    // bearing turned from the server's view to this frame's
    if (Comp_Int("ui_hud_compass_on")) {
        const int center = ps->stats[STAT_OBJECTIVECENTER], left = ps->stats[STAT_OBJECTIVELEFT], right = ps->stats[STAT_OBJECTIVERIGHT];

        comp_mate.on = !(center == 1800 && left == 1730 && right == 1870);
        if (comp_mate.on) {
            const float half  = anglemod((right - left) / 10.0f) * 0.5f;
            const float units = half < 59.95f ? 300.0f / tanf(DEG2RAD(60.0f - half)) : 999.0f * 52.0f;
            const float a     = DEG2RAD((center - 1800) / 10.0f + AngleSubtract(viewYaw, ps->viewangles[YAW]));
            const int   obj   = (Comp_ConfigStrings(), cs.objCurrent);

            comp_mate.right     = sinf(a) * units;
            comp_mate.ahead     = cosf(a) * units;
            comp_mate.objective = com_target_game->integer >= TG_MOHTA
                               || (obj >= 0 && obj < MAX_OBJECTIVES && !VectorCompare(cs.objLoc[obj], vec3_origin));
        }
    }
}

/*
====================================================================

THE TEAM TAG

The player aimed at, over their head (the cgame's, in place of their head
icon), CS2's way: their name, their health under it and an arrow down to
their head, in their team's color (the health red under cg_hud_lowhealth),
with the HUD's drop shadow, the same size whatever the distance

====================================================================
*/

#define TTAG_NAME    22.0f  // the name's size (bold)
#define TTAG_HEALTH  18.0f  // the health's (bold)
#define TTAG_ARROW_W 14.0f  // the arrow's width and height
#define TTAG_ARROW_H 9.0f
#define TTAG_GAP     4.0f   // between the arrow and the health, the health and the name (over their capitals)
#define TTAG_MAXW    220.0f // a longer name cut with "…"

static const char *const ttag_arrow = "poly 1 1 1 1 0 0 1 0 0.5 1";
// the teams' colors (the HUD's names'), the health's under cg_hud_lowhealth
static const rlColor_t ttag_allies = {0.533f, 0.569f, 0.8f, 1.0f}, ttag_axis = {0.8f, 0.533f, 0.533f, 1.0f};
static const rlColor_t ttag_low    = {0.753f, 0.188f, 0.129f, 1.0f};

// the HUD's units (cg_hud_scale)
static rlView_t Comp_HudView(void)
{
    rlView_t v;

    v.s    = UI_DesignScale(1920.0f, 1080.0f);
    v.dw   = uid.vidWidth / v.s;
    v.dh   = uid.vidHeight / v.s;
    v.hud  = true;
    v.font = NULL;
    return v;
}

// the arrow's box in pixels, its tip at (cx, tip) in units
static void TTag_ArrowBox(const rlView_t& v, float cx, float tip, int *x, int *y, int *w, int *h)
{
    *w = RL_P(v, TTAG_ARROW_W);
    *h = RL_P(v, TTAG_ARROW_H);
    *x = RL_P(v, cx) - *w / 2;
    *y = RL_P(v, tip) - *h;
}

void UI_DrawTeamTag(int client, float x, float y, int health)
{
    const rlView_t    v     = Comp_HudView();
    const int         team  = Comp_Team(client);
    const rlColor_t   c     = team == TEAM_AXIS ? ttag_axis : team == TEAM_ALLIES ? ttag_allies : rlColor_t{1, 1, 1, 1};
    const rlColor_t   hc    = health < Comp_Cvar("cg_hud_lowhealth")->value * 100.0f ? ttag_low : c;
    const std::string name  = RL_Ellipsize(v, Comp_Name(client), TTAG_NAME, true, TTAG_MAXW);
    const float       capN  = TTAG_NAME * 0.81f, capH = TTAG_HEALTH * 0.81f; // (Oswald's capitals)
    const float       half  = Q_max(RL_TextW(v, name.c_str(), TTAG_NAME, true), TTAG_ARROW_W) * 0.5f + 4.0f;
    float             cx    = x / v.s, tip = y / v.s, base;
    char              hs[8];
    int               ax, ay, aw, ah, o;

    // (the head off the screen: looked away while the server keeps who was aimed at)
    if (!name[0] || cx < 0.0f || cx > v.dw || tip > v.dh) {
        return;
    }
    Com_sprintf(hs, sizeof(hs), "%d", health);
    // kept on the screen: a team mate close by, aimed at below the head, has it at the top
    tip = Q_max(tip, 8.0f + capN + TTAG_GAP + capH + TTAG_GAP + TTAG_ARROW_H);
    cx  = Com_Clamp(half, Q_max(half, v.dw - half), cx);

    // the arrow, its shadow under it
    TTag_ArrowBox(v, cx, tip, &ax, &ay, &aw, &ah);
    o = Q_max(1, RL_P(v, 1.5f));
    RL_Shape(ax + o, ay + o, aw, ah, ttag_arrow, {0.0f, 0.0f, 0.0f, 0.55f}, v.s);
    RL_Shape(ax, ay, aw, ah, ttag_arrow, c, v.s);
    // the health over it, the name over that (baselines)
    base = tip - TTAG_ARROW_H - TTAG_GAP;
    // Changed in OPM: no health line for -1 (a demo: the cgame's CG_DrawTeamTag)
    if (health >= 0) {
        RL_Text(v, cx, base, hs, TTAG_HEALTH, hc, true, RL_CENTER, 0.0f, RL_BASE, 0.0f, true);
        base -= capH + TTAG_GAP;
    }
    RL_Text(v, cx, base, name.c_str(), TTAG_NAME, c, true, RL_CENTER, 0.0f, RL_BASE, 0.0f, true);
}

// the HUD's menus, faces, pictures, outlines and shader script compiled in (cmake/utils/embed_files.cmake)
extern "C" const builtinFile_t builtin_files[];

void UI_CompHud_Files(void)
{
    // (a file on disk or in a pak of the name is read instead)
    FS_SetBuiltinFiles(builtin_files);
}

static std::vector<builtinFile_t> comp_files;

/*
================
UI_CompHud_AddFile

A file made while the game runs, given to the filesystem with the files
compiled in (the head icons, the weapon menu's figures); its name and data
are the caller's, kept until the client quits. The list has room made once,
so it never moves, and an entry is written before the list's end moves past
it: a file read on another thread meanwhile finds the list whole
================
*/
qboolean UI_CompHud_AddFile(const char *name, const void *data, int size)
{
    // (room for the files made: the head icons, a figure a player model)
    static const size_t room = 256;
    size_t              n;

    if (comp_files.empty()) {
        n = 0;
        for (const builtinFile_t *f = builtin_files; f->name; f++) {
            n++;
        }
        comp_files.reserve(n + room + 1);
        comp_files.assign(builtin_files, builtin_files + n);
        comp_files.push_back({NULL, NULL, 0});
        FS_SetBuiltinFiles(comp_files.data());
    }
    if (comp_files.size() == comp_files.capacity()) {
        return qfalse;
    }
    // the new end first, then the entry over the old end, its name last
    n = comp_files.size() - 1;
    comp_files.push_back({NULL, NULL, 0});
    comp_files[n].data = (const unsigned char *)data;
    comp_files[n].size = size;
    std::atomic_thread_fence(std::memory_order_release);
    comp_files[n].name = name;
    return qtrue;
}

/*
================
UI_CompHud_HeadIcons

The pictures of the team icons above team mates' heads (scripts/
reloaded_hud.shader, cgame's): the HUD's team outlines
baked at 128 px into TGAs, given to the filesystem with the files compiled
in. The discs fill as much of the picture as the game's 32 px allies.tga
(28 px) and axis.tga (30 px) do; the renderer mipmaps them
================
*/
static std::vector<byte> comp_headIcons[2];

void UI_CompHud_HeadIcons(void)
{
    static const char *const icons[2][2] = {
        {"textures/hud/reloaded_allies.tga", "in 0.0625 0.0625 0.9375 0.9375 svg 1 1 1 1 ui/outlines/team/allies.svg center"   },
        {"textures/hud/reloaded_axis.tga",   "in 0.03125 0.03125 0.96875 0.96875 svg 1 1 1 1 ui/outlines/team/axis.svg center"},
    };
    const int         size = 128;
    std::vector<byte> rgba(size * size * 4);

    if (!comp_headIcons[0].empty()) {
        return;
    }
    for (int i = 0; i < 2; i++) {
        std::vector<byte>& tga = comp_headIcons[i];

        UI_BakeTexels(&icons[i][1], 1, size, size, 1.0f, rgba.data());
        // uncompressed, 32 bits, 8 of alpha, the rows from the bottom up
        tga.assign(18, 0);
        tga[2]  = 2;
        tga[12] = size & 255;
        tga[13] = size >> 8;
        tga[14] = size & 255;
        tga[15] = size >> 8;
        tga[16] = 32;
        tga[17] = 8;
        for (int y = size - 1; y >= 0; y--) {
            for (int x = 0; x < size; x++) {
                const byte *p = &rgba[(y * size + x) * 4];

                tga.push_back(p[2]);
                tga.push_back(p[1]);
                tga.push_back(p[0]);
                tga.push_back(p[3]);
            }
        }
        UI_CompHud_AddFile(icons[i][0], tga.data(), (int)tga.size());
    }
}

void UI_CompHud_Init(void)
{
    // the HUD: 1 as CS:GO's (the default), 2 as CS2's, 3 Classic (CS:GO's, the
    // chat along the top: Comp_Layout) (an old config's 0, the stock HUD's,
    // made 1)
    cg_hudstyle = Cvar_Get("cg_hudstyle", "1", CVAR_ARCHIVE);
    Cvar_CheckRange(cg_hudstyle, 1, 3, qtrue);
    for (size_t i = 0; i < ARRAY_LEN(comp_toggles); i++) {
        Cvar_Get(comp_toggles[i], "1", CVAR_ARCHIVE);
    }
    Cvar_Get("cg_hud_lowhealth", "0.4", CVAR_ARCHIVE);
    // the HUD's size: its 1080 lines fitted in the screen's (a screen narrower than 4:3: its 1440 across), times this
    // (0.25 to 2)
    Cvar_Get("cg_hud_scale", "1", CVAR_ARCHIVE);
    // the font of the HUD's small text (up to UI_HUD_SMALL): a value of rl_fonts (cl_uimenus.cpp), else Oswald's
    Cvar_Get("cg_hud_font", "oswald", CVAR_ARCHIVE);
    // the radar's size alone, times its own (0.5 to 1.5; Comp_RadarSize)
    Cvar_CheckRange(Cvar_Get("cg_hud_radar_size", "1", CVAR_ARCHIVE), 0.5f, 1.5f, qfalse);
    // the accent: "f6f67a", "#f6f67a" or "R G B" (0 to 1)
    Cvar_Get("cg_hud_accent", "f6f67a", CVAR_ARCHIVE);
    // the top's teams, beside the score: 0 a card per player, 1 how many are alive (CS2's)
    Cvar_Get("cg_hud_alivecount", "0", CVAR_ARCHIVE);
    // Classic's kills (cg_hudstyle 3): 0 lines in the chat as the stock HUD's, 1 the kill feed beside the top bar,
    // 2 under it
    Cvar_CheckRange(Cvar_Get("cg_hud_classic_kills", "0", CVAR_ARCHIVE), 0, 2, qtrue);
    // what the menus read, made now, empty (ui_hud_never stays so: the never
    // shown widgets, the teams' icons and the cards' shapes): a widget
    // reading a cvar that isn't there draws every frame (the draw cache
    // can't tell when it comes), and some are set only now and then (a
    // vote's, the compass's, a charge)
    for (size_t i = 0; i < ARRAY_LEN(comp_menuCvars); i++) {
        Cvar_Get(comp_menuCvars[i], "", 0);
    }
    Comp_Accent();
    Comp_Layout();
    UI_CompHud_Clear();
}

/*
====================================================================

WIDGETS

====================================================================
*/

// a weapon's outline (COMP_OUTLINES<pic>.svg), not one of the kill feed's
// pictures (the glove, the headshot and the skull: COMP_PICTURES<pic>.png)
static qboolean Comp_Outline(const char *pic)
{
    return strcmp(pic, "glove_50") && strcmp(pic, "headshot_50") && strcmp(pic, "skull_50") ? qtrue : qfalse;
}

// a picture baked at pw x ph pixels
static qhandle_t Comp_PictureShader(const char *pic, int pw, int ph, float s, qboolean mirror)
{
    char        layer[MAX_QPATH * 2];
    const char *layers[1] = {layer};

    if (Comp_Outline(pic)) {
        Com_sprintf(layer, sizeof(layer), "svg 1 1 1 1 " COMP_OUTLINES "%s.svg center%s", pic, mirror ? " mirror" : "");
    } else {
        Com_sprintf(layer, sizeof(layer), "picture 1 1 1 1 " COMP_PICTURES "%s.png center%s", pic, mirror ? " mirror" : "");
    }
    return UI_BakeShape("comp", layers, 1, pw, ph, s);
}

// a picture baked at its size (w x h design pixels at x y), tinted
static void Comp_Picture(const char *pic, float x, float y, float w, float h, float s, qboolean mirror, const UColor& c)
{
    const int    pw = (int)(w * s + 0.5f), ph = (int)(h * s + 0.5f);
    const vec4_t color = {c.r, c.g, c.b, c.a};
    qhandle_t    shader = Comp_PictureShader(pic, pw, ph, s, mirror);

    uii.Rend_SetColor(color);
    uii.Rend_DrawPicStretched(floorf(x * s + 0.5f), floorf(y * s + 0.5f), pw, ph, 0, 0, 1, 1, shader);
}

// a weapon's picture fitted in a box of design pixels: its size there
static void Comp_Fit(const char *pic, float boxW, float boxH, float s, float *w, float *h)
{
    float sw, sh, fit;

    if (!UI_OutlineSize(va(COMP_OUTLINES "%s.svg", pic), &sw, &sh)) {
        sw = sh = 1.0f;
    }
    fit = Q_min(boxW / sw, boxH / sh);
    *w  = floorf(fit * sw * s + 0.5f) / s;
    *h  = floorf(fit * sh * s + 0.5f) / s;
}

// the dot of a weapon list's row with nothing left in it (CS2's empty
// grenade slots): d x d pixels, white (tinted when drawn)
static qhandle_t Comp_EmptyShader(float s, int *d)
{
    const char *layers[1] = {"circle 1 1 1 1 1"};

    *d = (int)(6.0f * s + 0.5f);
    return UI_BakeShape("comp_empty", layers, 1, *d, *d, s);
}

// a text's width, smaller to fit a width
static float Comp_TextW(UIFont *font, const char *text, float fit)
{
    const float k = font->FitScale(text, fit);

    return font->getWidth(text, -1) * k;
}

// text with its capitals' middle on cy (the baked faces' lines are centered
// so), smaller to fit a width; its width
static float Comp_Text(UIFont *font, float x, float cy, const char *text, const UColor& c, const float *scale, float fit = 0.0f)
{
    float w;

    font->setFit(font->FitScale(text, fit));
    font->setColor(c);
    font->Print(x, cy - font->getHeight((const float *)NULL) * 0.5f, text, -1, scale);
    w = font->getWidth(text, -1);
    font->setFit(1.0f);
    return w;
}

static float Comp_Fade(int time, int life, int fade)
{
    return Com_Clamp(0.0f, 1.0f, (float)(life - (cls.realtime - time)) / fade);
}

/*
================
UIHudFeed

The kill feed (feed kills: right aligned pad px before the widget's right
edge, the newest at the bottom, 6 s, fading in the last 1.5; each line in a
box reaching pad px either side of it, KF_ROW tall, KF_SPACE apart: dark,
dark red for the player's death, black with a red edge for the player's
kill), the chat (feed chat: from
the bottom up) and the game messages (feed messages: from the top), 8 s
fading in the last 2; lines wrapped between words, the newest that fit
whole in the part of the box on screen (none cut at its edges).
boldfont: the white game messages'
================
*/
class UIHudFeed : public UIWidget
{
public:
    CLASS_PROTOTYPE(UIHudFeed);

    int     m_kind; // 0 kills, 1 chat, 2 messages
    UIFont *m_bold;
    bool    m_top; // the chat's lines from the box's top down (Classic's along the top), else up from its bottom

    UIHudFeed() : m_kind(2), m_bold(NULL), m_top(false) {}
    void SetFeed(Event *ev);
    void SetBoldFont(Event *ev);
    void SetFromTop(Event *ev) { m_top = true; }
    void Draw(void) override;
    // kept while no line fades: the lines shown
    bool Lines(int *key);
    bool CacheDraw(void) override { return Lines(NULL); }
    bool DrawKeyed(void) override { return true; }
    int  DrawKey(void) override
    {
        int key = 0;

        Lines(&key);
        return key;
    }
    void DrawKills(float w, float h, float s);
};

Event EV_HudFeed_Feed("feed", EV_DEFAULT, "s", "kind", "kills, chat or messages");
Event EV_HudFeed_BoldFont("boldfont", EV_DEFAULT, "s", "font", "the bold lines' font");
Event EV_HudFeed_FromTop("fromtop", EV_DEFAULT, NULL, NULL, "the chat's lines from the box's top down");

CLASS_DECLARATION(UIWidget, UIHudFeed, NULL) {
    {&EV_HudFeed_Feed,     &UIHudFeed::SetFeed    },
    {&EV_HudFeed_BoldFont, &UIHudFeed::SetBoldFont},
    {&EV_HudFeed_FromTop,  &UIHudFeed::SetFromTop },
    {NULL,                 NULL                   }
};

bool UIHudFeed::Lines(int *key)
{
    const int  life = m_kind ? 8000 : 6000, fade = m_kind ? 2000 : 1500;
    const int  count = m_kind == 0 ? comp.numKills : m_kind == 1 ? comp.numChat : comp.numMsgs;
    const bool all = m_kind == 1 && UI_Extras_Typing(); // (the chat's lines, all of them, while one is typed)
    bool       steady = true;
    int        h = all ? 1 : 0;

    for (int i = 0; i < count; i++) {
        const int time = m_kind == 0 ? comp.kills[i].time : m_kind == 1 ? comp.chat[i].time : comp.msgs[i].time;

        if (m_kind == 1 && comp.chat[i].kill && !(m_top && Comp_Int("ui_hud_kf_text"))) {
            continue;
        }
        if (all) {
            h = UI_DrawHash(h, &time, sizeof(time));
        } else if (cls.realtime - time < life) {
            steady = steady && cls.realtime - time < life - fade;
            h      = UI_DrawHash(h, &time, sizeof(time));
        }
    }
    if (key) {
        *key = h;
    }
    return steady;
}

void UIHudFeed::SetFeed(Event *ev)
{
    const str kind = ev->GetString(1);

    m_kind = kind == "kills" ? 0 : kind == "chat" ? 1 : 2;
}

void UIHudFeed::SetBoldFont(Event *ev)
{
    m_bold = new UIFont(ev->GetString(1));
}

// a box of design pixels, its edges on whole pixels
static void Comp_Box(float x0, float y0, float x1, float y1, float s)
{
    const float px = floorf(x0 * s + 0.5f), py = floorf(y0 * s + 0.5f);

    uii.Rend_DrawBox(px, py, floorf(x1 * s + 0.5f) - px, floorf(y1 * s + 0.5f) - py);
}

// the kill feed's box: a small rounded box (or its edge, KF_EDGE wide) baked
// at the HUD's scale, its corners as they are, its middle texel stretched
static qhandle_t Comp_FeedBoxShader(float s, qboolean edge, int *corner, int *size)
{
    char        layer[64];
    const char *layers[1] = {layer};
    const int   c = Q_max((int)ceilf(KF_RADIUS * s), edge ? Q_max(1, (int)floorf(KF_EDGE * s + 0.5f)) : 1);

    *corner = c;
    *size   = 2 * c + 2;
    if (edge) {
        Com_sprintf(layer, sizeof(layer), "roundframe %g %g 1 1 1 1", KF_RADIUS, KF_EDGE);
    } else {
        Com_sprintf(layer, sizeof(layer), "roundbox %g 1 1 1 1", KF_RADIUS);
    }
    return UI_BakeShape("comp_kf", layers, 1, *size, *size, s);
}

// a box of design pixels with KF_RADIUS corners (or its edge), in nine parts
static void Comp_FeedBox(float x0, float y0, float x1, float y1, float s, qboolean edge)
{
    int             c, n;
    const qhandle_t shader = Comp_FeedBoxShader(s, edge, &c, &n);
    const float     px[4]  = {floorf(x0 * s + 0.5f), floorf(x0 * s + 0.5f) + c, floorf(x1 * s + 0.5f) - c, floorf(x1 * s + 0.5f)};
    const float     py[4]  = {floorf(y0 * s + 0.5f), floorf(y0 * s + 0.5f) + c, floorf(y1 * s + 0.5f) - c, floorf(y1 * s + 0.5f)};
    // the texture's parts: a corner, the middle texel (its centre: no neighbour bleeds in), the other corner
    const float     t[3][2] = {{0.0f, (float)c / n}, {(c + 0.5f) / n, (c + 1.5f) / n}, {(c + 2.0f) / n, 1.0f}};

    if (!shader || px[2] < px[1] || py[2] < py[1]) {
        if (!edge) {
            Comp_Box(x0, y0, x1, y1, s);
        }
        return;
    }
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            if (edge && row == 1 && col == 1) {
                continue; // (the edge's middle is clear)
            }
            if (px[col + 1] > px[col] && py[row + 1] > py[row]) {
                uii.Rend_DrawPicStretched(px[col], py[row], px[col + 1] - px[col], py[row + 1] - py[row], t[col][0], t[row][0], t[col][1], t[row][1], shader);
            }
        }
    }
}

// a name's color in the feed: the Allies' CS2's blue, the Axis' the HUD's
// red (Comp_TeamColor's, as the top bar's), else white
static UColor Comp_FeedNameColor(int team, float alpha)
{
    return team == TEAM_ALLIES ? UColor(0.435f, 0.612f, 0.902f, alpha) : team == TEAM_AXIS ? UColor(0.8f, 0.533f, 0.533f, alpha) : UColor(1, 1, 1, alpha);
}

void UIHudFeed::DrawKills(float w, float h, float s)
{
    float used = 0.0f, y = 0.0f;
    int   first, i, count = comp.numKills, oldest = 0;

    while (oldest < count && cls.realtime - comp.kills[oldest].time >= 6000) {
        oldest++;
    }
    // the newest that fit, the oldest on top
    for (first = count; first > oldest; first--) {
        const float rowH = KF_ROW + (used > 0.0f ? KF_SPACE : 0.0f);

        if (used + rowH > h) {
            break;
        }
        used += rowH;
    }
    for (i = first; i < count; i++) {
        const compKill_t *k     = &comp.kills[i];
        const float       alpha = Comp_Fade(k->time, 6000, 1500);
        const float       mid   = y + KF_ROW * 0.5f;
        const qboolean    mark  = k->suicide && strcmp(k->pic, "skull_50");
        const UColor      kc = Comp_FeedNameColor(k->killerTeam, alpha), vc = Comp_FeedNameColor(k->victimTeam, alpha), white(1, 1, 1, alpha);
        // a long name smaller, to fit 150 px
        const float       kw = k->killer[0] ? Comp_TextW(m_font, k->killer, 150.0f) + KF_GAP : 0.0f;
        const float       inner = kw + k->picW + KF_GAP + (mark ? KF_ICON + KF_GAP : 0.0f) + (k->headshot ? KF_ICON + KF_GAP : 0.0f)
                                + Comp_TextW(m_font, k->victim, 150.0f);
        const float       x0 = w - inner - 2 * KF_PAD;
        float             x  = x0 + KF_PAD;

        // the box: the player's death dark red, the player's kill black with a red edge, the rest dark
        {
            const vec4_t death = {0.384f, 0.016f, 0.016f, 0.80f * alpha}, kill = {0.0f, 0.0f, 0.0f, 0.90f * alpha};
            const vec4_t other = {0.0f, 0.0f, 0.0f, 0.64f * alpha}, edge = {0.882f, 0.0f, 0.0f, alpha};

            uii.Rend_SetColor(k->myDeath ? death : k->myKill ? kill : other);
            Comp_FeedBox(x0, y, w, y + KF_ROW, s, qfalse);
            if (k->myKill) {
                uii.Rend_SetColor(edge);
                Comp_FeedBox(x0, y, w, y + KF_ROW, s, qtrue);
            }
        }
        if (k->killer[0]) {
            x += Comp_Text(m_font, x, mid, k->killer, kc, getVirtualScale(), 150.0f) + KF_GAP;
        }
        // the outlines point left: the feed's at the victim
        Comp_Picture(k->pic, x, mid - k->picH * 0.5f, k->picW, k->picH, s, Comp_Outline(k->pic), white);
        x += k->picW + KF_GAP;
        if (mark) {
            Comp_Picture("skull_50", x, mid - KF_ICON * 0.5f, KF_ICON, KF_ICON, s, qfalse, white);
            x += KF_ICON + KF_GAP;
        }
        if (k->headshot) {
            Comp_Picture("headshot_50", x, mid - KF_ICON * 0.5f, KF_ICON, KF_ICON, s, qfalse, white);
            x += KF_ICON + KF_GAP;
        }
        Comp_Text(m_font, x, mid, k->victim, vc, getVirtualScale(), 150.0f);
        y += KF_ROW + KF_SPACE;
    }
}

void UIHudFeed::Draw(void)
{
    const float       s = m_vVirtualScale[0], w = m_frame.size.width / s, h = m_frame.size.height / s;
    const compLine_t *lines = m_kind == 1 ? comp.chat : comp.msgs;
    const int         count = m_kind == 1 ? comp.numChat : comp.numMsgs;
    std::vector<str>  rows;
    std::vector<int>  rowLine;
    float             lineH = m_font->getHeight((const float *)NULL), y;
    int               i;

    if (m_bold) {
        m_bold->setVirtualScale(getVirtualScale());
    }
    if (!m_kind) {
        DrawKills(w, h, s);
        return;
    }
    // the newest lines that fit, wrapped (Added: all of them while a line is typed: the chat's history)
    const bool all = m_kind == 1 && UI_Extras_Typing();
    // the part of the box on screen (a box moved past its menu's edge is cut
    // there), the rows' height in it so far
    const float top    = Q_max(0.0f, (m_clippedframe.pos.y - m_screenframe.pos.y) / s);
    const float bottom = h - Q_max(0.0f, (m_screenframe.getMaxY() - m_clippedframe.getMaxY()) / s);
    float       used   = 0.0f;

    for (i = count - 1; i >= 0 && (all || cls.realtime - lines[i].time < 8000); i--) {
        if (lines[i].kill && !(m_top && Comp_Int("ui_hud_kf_text"))) {
            continue;
        }
        UIFont          *font = lines[i].bold && m_bold ? m_bold : m_font;
        std::vector<str> wrapped;
        const char      *p = lines[i].text;

        while (*p) {
            const char *brk = NULL, *q = p;
            float       width = 0.0f;

            for (; *q; q++) {
                if (*q == ' ') {
                    brk = q;
                }
                width += font->getCharWidth((byte)*q);
                if (q > p && width > w) {
                    break;
                }
            }
            if (*q && brk && brk > p) {
                q = brk;
            }
            wrapped.push_back(str(p, 0, (int)(q - p)));
            for (p = q; *p == ' '; p++) {
            }
        }
        const float need = used + wrapped.size() * lineH + (rows.empty() ? 0.0f : FEED_GAP);

        if (need > bottom - top) {
            break;
        }
        used = need;
        rows.insert(rows.begin(), wrapped.begin(), wrapped.end());
        rowLine.insert(rowLine.begin(), wrapped.size(), i);
    }
    y = m_kind == 1 && !m_top ? bottom - used : top;
    for (size_t r = 0; r < rows.size(); r++) {
        const compLine_t *l = &lines[rowLine[r]];
        const UColor      c(l->color[0], l->color[1], l->color[2], all ? 1.0f : Comp_Fade(l->time, 8000, 2000));

        Comp_Text(l->bold && m_bold ? m_bold : m_font, 0.0f, y + lineH * 0.5f, rows[r], c, getVirtualScale());
        y += lineH;
        if (r + 1 < rows.size() && rowLine[r + 1] != rowLine[r]) {
            y += FEED_GAP;
        }
    }
}

// a shape's layer with its color grey: the color's luminance times k (the
// color after the kind, or after the kind's one number, as uibake reads it)
static str Comp_Grey(const char *layer, float k)
{
    char        kind[32];
    const char *p = layer;
    float       c[4], v;
    int         n;
    str         out;

    if (sscanf(p, "%31s%n", kind, &n) != 1) {
        return layer;
    }
    p += n;
    out = kind;
    if (!strcmp(kind, "ring") || !strcmp(kind, "frame") || !strcmp(kind, "roundbox")) {
        if (sscanf(p, "%f%n", &v, &n) != 1) {
            return layer;
        }
        out += va(" %g", v);
        p += n;
    }
    if (sscanf(p, "%f %f %f %f%n", &c[0], &c[1], &c[2], &c[3], &n) != 4) {
        return layer;
    }
    v = (0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2]) * k;
    out += va(" %g %g %g %g", v, v, v, c[3]);
    out += p + n;
    // (an outline's parts greyed each by its own brightness, as a shape's layers are)
    if (!strcmp(kind, "svg")) {
        out += " grey";
    }
    return out;
}

// a team's cards (alive, alive with the bar under it, dead: greyed and
// darker) size design pixels wide, COMP_CARD_H high (the bar 8 more): the
// box, its frame, the team's icon (icon: the widget whose shapes it is)
static void Comp_CardShaders(int team, UIWidget *icon, int size, float s, qhandle_t out[3])
{
    const int   h  = COMP_CARD_H;
    const float f  = h / (h + 8.0f), bar = (h + 4.0f) / (h + 8.0f);
    const float px = 5.0f / size, py = 5.0f / (h + 8.0f);

    for (int k = 0; k < 3; k++) {
        auto             grey = [k](const char *layer) { return k == 2 ? Comp_Grey(layer, 0.42f) : str(layer); };
        std::vector<str> lines;
        const char      *layers[40];
        int              numLayers = 0;

        lines.push_back(va("in 0 0 1 %g %s", f, grey(team == TEAM_AXIS ? "fill 0.549 0.333 0.294 0.851" : "fill 0.294 0.435 0.549 0.851").c_str()));
        for (int l = 1; size >= 16 && icon && l <= icon->getShapes().NumObjects() && l < 30; l++) {
            lines.push_back(va("in %g %g %g %g %s", px, py, 1.0f - px, f - py, grey(icon->getShapes().ObjectAt(l).c_str()).c_str()));
        }
        lines.push_back(va("in 0 0 1 %g %s", f, grey("frame 1 1 1 1 0.2").c_str()));
        if (k == 1) {
            lines.push_back(va("in 0 %g 1 1 fill 0.988 0.988 0.988 1", bar));
        }
        for (str& l : lines) {
            layers[numLayers++] = l.c_str();
        }
        out[k] = UI_BakeShape("comp_card", layers, numLayers, (int)(size * s + 0.5f), (int)((h + 8) * s + 0.5f), s);
    }
}

/*
================
UIHudCards

A card for each player of a team (team allies or axis: ui_hud_cards_<team>,
in the clients' order), laid out from the widget's right edge leftward
(allies) or its left edge rightward (axis): the team's icon (icons: the
widgets whose shapes are the Allies' and the Axis' icons), as high as the
score box beside them (COMP_CARD_H, from the widget's top), a white bar under
it while alive on the player's own team, greyed out once dead. They narrow
so the bigger team fits in about 400 px and in the widget, the rest a count
================
*/
class UIHudCards : public UIWidget
{
public:
    CLASS_PROTOTYPE(UIHudCards);

    int       m_team;
    str       m_icons[2];
    int       m_size, m_stamp;
    qhandle_t m_cards[3]; // alive, alive with a bar, dead

    UIHudCards() : m_team(TEAM_ALLIES), m_size(0), m_stamp(-1) {}
    void SetTeam(Event *ev) { m_team = !str::icmp(ev->GetString(1), "axis") ? TEAM_AXIS : TEAM_ALLIES; }
    void SetIcons(Event *ev)
    {
        m_icons[0] = ev->GetString(1);
        m_icons[1] = ev->GetString(2);
    }
    void Draw(void) override;
    bool CacheDraw(void) override { return true; }
};

Event EV_HudCards_Team("team", EV_DEFAULT, "s", "team", "allies or axis");
Event EV_HudCards_Icons("icons", EV_DEFAULT, "ss", "allies axis", "the widgets whose shapes are the teams' icons");

CLASS_DECLARATION(UIWidget, UIHudCards, NULL) {
    {&EV_HudCards_Team,  &UIHudCards::SetTeam },
    {&EV_HudCards_Icons, &UIHudCards::SetIcons},
    {NULL,               NULL                 }
};

void UIHudCards::Draw(void)
{
    static cvar_t *const allies = Cvar_Get("ui_hud_cards_allies", "", 0), *const axis = Cvar_Get("ui_hud_cards_axis", "", 0);
    const float          s      = m_vVirtualScale[0], w = m_frame.size.width / s;
    const char          *cards  = (m_team == TEAM_AXIS ? axis : allies)->string;
    const int            n      = (int)strlen(cards);
    const int            most   = Q_max(1, Q_max((int)strlen(allies->string), (int)strlen(axis->string)));

    UI_DrawDep(allies);
    UI_DrawDep(axis);
    int         size  = Q_max(20, Q_min(COMP_CARD_W, 400 / most - 4)), fit, i;

    if (!n) {
        return;
    }
    if ((size + 4) * most > w) {
        size = Q_max(8, (int)(w / most) - 4);
    }
    fit = Q_min(n, Q_max(1, (int)(w / (size + 4))));
    if (fit < n) {
        fit--;
    }
    // a card of each kind baked once at its size
    if (size != m_size || m_stamp != UI_BakeStamp()) {
        Comp_CardShaders(m_team, m_parent ? m_parent->FindWidget(m_icons[m_team == TEAM_AXIS]) : NULL, size, s, m_cards);
        m_size  = size;
        m_stamp = UI_BakeStamp();
    }
    {
        const vec4_t white = {1, 1, 1, 1};

        uii.Rend_SetColor(white);
    }
    for (i = 0; i < fit; i++) {
        const float x = m_team == TEAM_AXIS ? 2.0f + i * (size + 4) : w - 2.0f - size - i * (size + 4);

        uii.Rend_DrawPicStretched(floorf(x * s + 0.5f), 0, (int)(size * s + 0.5f), (int)((COMP_CARD_H + 8) * s + 0.5f), 0, 0, 1, 1,
                                  m_cards[cards[i] == 'd' ? 2 : cards[i] == 'b' ? 1 : 0]);
    }
    if (fit < n) {
        const char *more = va("+%d", n - fit);

        Comp_Text(
            m_font,
            m_team == TEAM_AXIS ? 2.0f + fit * (size + 4) : w - 2.0f - fit * (size + 4) - m_font->getWidth(more, -1),
            COMP_CARD_H * 0.5f, more, UColor(0.988f, 0.988f, 0.988f, 1.0f), getVirtualScale()
        );
    }
}

/*
================
UIHudFollow

The player followed (ui_hud_follow, their team's color ui_hud_follow_col,
their team ui_hud_follow_team: allies or axis), CS2's way: a plate at the
bottom's middle, SPECTATING over their name, their team's icon at its left
(CS:GO's corners, ui_hud_corners: CS2's strip has it in its badge under the
plate), a line of their team's color under it; as wide as the name. Over
the strip, else the HUD's margin, from the widget's bottom (the screen's)
================
*/
#define FP_NAME    26.0f  // the name's size (bold)
#define FP_CAPTION 14.0f  // SPECTATING's
#define FP_TRACK   1.0f   // SPECTATING's spacing
#define FP_ICON    38.0f  // the team's icon
#define FP_LINE    3.0f   // the line under it
#define FP_PAD     18.0f  // the plate's padding left and right
#define FP_GAP     12.0f  // between the icon and the text
#define FP_MAXW    320.0f // a longer name cut with "…"

class UIHudFollow : public UIWidget
{
public:
    CLASS_PROTOTYPE(UIHudFollow);

    void Draw(void) override;
    bool CacheDraw(void) override { return true; }
};

CLASS_DECLARATION(UIWidget, UIHudFollow, NULL) {
    {NULL, NULL}
};

void UIHudFollow::Draw(void)
{
    static cvar_t *const name    = Cvar_Get("ui_hud_follow", "", 0);
    static cvar_t *const col     = Cvar_Get("ui_hud_follow_col", "", 0);
    static cvar_t *const team    = Cvar_Get("ui_hud_follow_team", "", 0);
    static cvar_t *const corners = Cvar_Get("ui_hud_corners", "", 0);
    static const rlColor_t fill = {0.047f, 0.055f, 0.071f, 0.78f}, caption = {1.0f, 1.0f, 1.0f, 0.612f};
    rlView_t               v;
    rlColor_t              c = {1.0f, 1.0f, 1.0f, 1.0f};

    UI_DrawDep(name);
    UI_DrawDep(col);
    UI_DrawDep(team);
    UI_DrawDep(corners);
    if (!name->string[0]) {
        return;
    }
    v.s  = m_vVirtualScale[0];
    v.dw = m_frame.size.width / v.s;
    v.dh = m_frame.size.height / v.s;
    sscanf(col->string, "%f %f %f %f", &c.r, &c.g, &c.b, &c.a);
    {
        const int         t     = !strcmp(team->string, "axis") ? 1 : !strcmp(team->string, "allies") ? 0 : -1;
        const bool        icon  = corners->integer && t >= 0;
        const std::string shown = RL_Ellipsize(v, name->string, FP_NAME, true, FP_MAXW);
        const float       nw    = RL_TextW(v, shown.c_str(), FP_NAME, true);
        const float       tw    = Q_max(nw, RL_TextW(v, "SPECTATING", FP_CAPTION, false, FP_TRACK));
        const float       w     = FP_PAD + (icon ? FP_ICON + FP_GAP : 0.0f) + tw + FP_PAD;
        const float       h     = COMP_FOLLOW_H - FP_LINE;
        const float       x     = floorf((v.dw - w) * 0.5f);
        const float       y     = v.dh - (corners->integer ? 24.0f : 88.0f) - COMP_FOLLOW_H;
        const float       tx    = x + FP_PAD + (icon ? FP_ICON + FP_GAP : 0.0f);

        RL_Box(v, x, y, w, h, fill);
        RL_Box(v, x, y + h, w, FP_LINE, c);
        if (icon) {
            RL_TeamIcon(v, t, x + FP_PAD + FP_ICON * 0.5f, y + h * 0.5f, FP_ICON);
        }
        RL_Text(v, tx, y + 9.0f, "SPECTATING", FP_CAPTION, caption, false, RL_LEFT, FP_TRACK, RL_TOP);
        RL_Text(v, tx, y + h - 10.0f, shown.c_str(), FP_NAME, c, true, RL_LEFT, 0.0f, RL_BASE);
    }
}

/*
================
UIHudWeapons

The weapons carried at the widget's bottom right, CS:GO's order from the
top: the primary, the sidearm, the grenades (frags then smokes, each with
how many; with none left their row kept, a dot where they were: CS2's),
each with room for its name under it so that a switch or the last grenade
thrown moves nothing (CS:GO's, CS2's); the one in hand bright and named.
countfont: the grenades' counts'
================
*/
class UIHudWeapons : public UIWidget
{
public:
    CLASS_PROTOTYPE(UIHudWeapons);

    UIFont *m_countFont;

    UIHudWeapons() : m_countFont(NULL) {}
    void SetCountFont(Event *ev) { m_countFont = new UIFont(ev->GetString(1)); }
    void Item(float right, float *bottom, const char **pics, const int *counts, int n, int hand, float boxW, float boxH);
    void Draw(void) override;
    bool CacheDraw(void) override { return true; }
    bool DrawKeyed(void) override { return true; }
    int  DrawKey(void) override
    {
        const playerState_t *ps   = &cl.snap.ps;
        const int            v[7] = {comp.frags, comp.smokes, comp.nation, ps->stats[STAT_WEAPONS], ps->stats[STAT_EQUIPPED_WEAPON],
                                     Comp_Int("cg_hud_grenades"), comp_accentStamp};
        const char          *hand = Comp_HandWeapon();
        int                  h    = UI_DrawHash(0, v, sizeof(v));

        h = UI_DrawHash(h, comp.primary, strlen(comp.primary));
        h = UI_DrawHash(h, comp.sidearm, strlen(comp.sidearm));
        return UI_DrawHash(h, hand, strlen(hand));
    }
};

Event EV_HudWeapons_CountFont("countfont", EV_DEFAULT, "s", "font", "the grenades' counts' font");

CLASS_DECLARATION(UIWidget, UIHudWeapons, NULL) {
    {&EV_HudWeapons_CountFont, &UIHudWeapons::SetCountFont},
    {NULL,                     NULL                       }
};

void UIHudWeapons::Item(float right, float *bottom, const char **pics, const int *counts, int n, int hand, float boxW, float boxH)
{
    const float  s      = m_vVirtualScale[0];
    const float  lineH  = m_font->getHeight((const float *)NULL);
    const UColor accent(comp_accent[ACC_MAIN][0], comp_accent[ACC_MAIN][1], comp_accent[ACC_MAIN][2], 1.0f);
    float        pw[2], phi[2], ph = 0.0f, w = 0.0f, top, x;
    int          i;

    // each picture's size fitted in the box, the row's width and height
    for (i = 0; i < n; i++) {
        Comp_Fit(pics[i], boxW, boxH, s, &pw[i], &phi[i]);
        ph = Q_max(ph, phi[i]);
        w += pw[i] + (counts ? 4.0f + m_countFont->getWidth(va("%d", counts[i]), -1) : 0.0f) + (i + 1 < n ? (counts ? 8.0f : 0.0f) + 4.0f : 0.0f);
    }
    // the name's line kept in hand or not, so the list stays put; as tight as
    // reads well: the line just under the pictures, a little more room up to
    // the next weapon
    top = *bottom - ph - 2.0f - lineH;
    x = right - w;
    for (i = 0; i < n; i++) {
        const float alpha = hand == i ? 1.0f : 0.55f;

        // a count left of its picture, so the pictures line up with the weapons above
        if (counts) {
            x += Comp_Text(m_countFont, x, top + ph - m_countFont->getHeight((const float *)NULL) * 0.5f, va("%d", counts[i]),
                           UColor(comp_accent[ACC_MAIN][0], comp_accent[ACC_MAIN][1], comp_accent[ACC_MAIN][2], alpha), getVirtualScale());
            x += 4.0f;
        }
        Comp_Picture(pics[i], x, top + floorf((ph - phi[i]) * 0.5f * s) / s, pw[i], phi[i], s, qfalse,
                     UColor(comp_accent[ACC_WEAPON][0], comp_accent[ACC_WEAPON][1], comp_accent[ACC_WEAPON][2], alpha));
        x += pw[i] + (counts ? 8.0f : 0.0f) + 4.0f;
    }
    if (hand >= 0) {
        const char *name = Comp_HandWeapon();

        // in the accent (as the ammo under it), a long name smaller, to fit 200 px
        Comp_Text(m_font, right - Comp_TextW(m_font, name, 200.0f), top + ph + 2.0f + lineH * 0.5f, name, accent, getVirtualScale(), 200.0f);
    }
    *bottom = top - 8.0f;
}

void UIHudWeapons::Draw(void)
{
    static const int     slots[6] = {WEAPON_CLASS_PISTOL, WEAPON_CLASS_RIFLE, WEAPON_CLASS_SMG, WEAPON_CLASS_MG, WEAPON_CLASS_GRENADE, WEAPON_CLASS_HEAVY};
    const playerState_t *ps       = &cl.snap.ps;
    const int            owned    = ps->stats[STAT_WEAPONS] & 0x3F, equipped = ps->stats[STAT_EQUIPPED_WEAPON] & 0x3F;
    const float          s        = m_vVirtualScale[0];
    const float          right    = m_frame.size.width / s - 18.0f;
    const compWeapon_t  *inHand   = Comp_Weapon(Comp_HandWeapon());
    float                bottom   = m_frame.size.height / s;
    int                  handSlot = 0, primarySlot = 0, i;

    if (!m_countFont) {
        m_countFont = m_font;
    }
    m_countFont->setVirtualScale(getVirtualScale());
    for (i = 0; i < 6; i++) {
        if (!handSlot && (equipped & slots[i])) {
            handSlot = i + 1;
        }
        if (!primarySlot && (owned & slots[i]) && i != 0 && i != 4) {
            primarySlot = i + 1;
        }
    }
    // laid out from the bottom up: the grenades, the sidearm, the primary
    if (Comp_Int("cg_hud_grenades") && (comp.frags > 0 || comp.smokes > 0)) {
        const char *pics[2];
        int         counts[2], n = 0, hand = -1;

        for (i = 0; i < 2; i++) {
            if (i ? comp.smokes > 0 : comp.frags > 0) {
                if ((equipped & WEAPON_CLASS_GRENADE) && inHand && (inHand->kind == WK_SMOKE) == (i == 1)) {
                    hand = n;
                }
                pics[n]     = Comp_Weapon(comp_issued[comp.nation][1 + i])->pic;
                counts[n++] = i ? comp.smokes : comp.frags;
            }
        }
        Item(right, &bottom, pics, counts, n, hand, 200.0f, 38.0f);
    } else if (Comp_Int("cg_hud_grenades") && (owned & ~WEAPON_CLASS_GRENADE)) {
        // none left: the row as the frag's, a dot at its middle
        const float    *acc   = comp_accent[ACC_WEAPON];
        const float     lineH = m_font->getHeight((const float *)NULL);
        const vec4_t    color = {acc[0], acc[1], acc[2], 0.55f};
        float           pw, ph;
        int             d;
        const qhandle_t dot = Comp_EmptyShader(s, &d);

        Comp_Fit(Comp_Weapon(comp_issued[comp.nation][1])->pic, 200.0f, 38.0f, s, &pw, &ph);
        bottom -= ph + 2.0f + lineH;
        uii.Rend_SetColor(color);
        uii.Rend_DrawPicStretched(floorf((right - pw * 0.5f) * s - d * 0.5f), floorf((bottom + ph * 0.5f) * s - d * 0.5f), d, d, 0, 0, 1,
                                  1, dot);
        bottom -= 8.0f;
    }
    if (comp.sidearm[0] && Comp_Weapon(comp.sidearm)) {
        const char *pic = Comp_Weapon(comp.sidearm)->pic;

        Item(right, &bottom, &pic, NULL, 1, handSlot == 1 ? 0 : -1, 110.0f, 30.0f);
    }
    if (primarySlot && Comp_Weapon(comp.primary)) {
        const char *pic = Comp_Weapon(comp.primary)->pic;

        Item(right, &bottom, &pic, NULL, 1, handSlot == primarySlot ? 0 : -1, 160.0f, 34.0f);
    }
}

/*
====================================================================

THE RADAR'S MAP

The floors a player can reach, walked over a grid of 16 units (box traces
down from a jump's height above each floor, on the world and the brush
entities: crates, bridges) from the spawn points and the tops of the
ladders at the map's load, and again from where a player stands that the
walk didn't reach (every 250 ms, a few thousand cells a frame). Drawn top
down into a picture: each cell's floor nearest the player's height filled,
brighter the higher, its walls and ledges only the shade's edge (no
outlines, as CS2's radar), that edge smoothed where it meets no floor (a
slanted wall a clean line, not the grid's steps: RM_Paint), stairs and
ramps striped across the way up, the player's storey's stripes bright.
The cells' floors are listed on a thread of their own (at the load, on
this one) and given to the renderer, which makes the picture on the GPU:
a change of storey is then a number (re.Radar2DHeight). A renderer without
it (RegisterRadar2D NULL): the picture painted on that thread from the
same list and uploaded, again at each change of storey

====================================================================
*/

#define RM_CELL     16.0f
#define RM_RANGE    1000.0f // units from the radar's middle to its edge
#define RM_STAIR    7.0f    // a floor this much higher beside: a stair (to a ledge's 40)
#define RM_PER_STEP 2000    // cells walked a frame after the load
#define RM_PAGE     16384   // the floors' memory in whole pages: the renderer reads it in place

typedef struct {
    float z;
    int   next; // the column's next floor, -1 none
} rmNode_t;

typedef struct {
    clipHandle_t model;
    vec3_t       origin, mins, maxs; // its bounds in the world
} rmSolid_t;

typedef struct {
    float    top, bottom;
    vec3_t   at;
    qboolean ok;
} rmDown_t;

static struct {
    char                   map[MAX_QPATH];
    float                  x0, y0, zMin, zMax;
    int                    cw, ch;
    std::vector<int>       head; // a column's first floor, -1 none
    std::vector<rmNode_t>  nodes;
    std::vector<rmSolid_t> solids;          // the brush entities stood on
    std::vector<rmDown_t>  down;            // each column's last trace down (asked again: its answer)
    std::vector<int>       queue, queueCell; // the floors still to walk out from
    size_t                 queued;
    int                    walked, seedTime; // the floors the picture has, the players' last check
    // the picture
    int       texels; // a cell's, across
    float     focusZ;
    qhandle_t shader;
    int       stamp;
    float     width;     // the radar's, pixels across (the last seen: the load's picture is made for it)
    qboolean  onGpu;     // the picture is the renderer's (RegisterRadar2D): a storey's change is a number
    qboolean  gpuFailed; // the renderer's failed: the UI's own picture, this map
    qboolean  giveAtEnd; // a new map's walk: its floors to the renderer at the load's end (UI_CompHud_LoadEnd)
    // ... made on its thread (the floors, then the UI's own picture of them, or the floors for the renderer's)
    std::thread       worker;
    std::atomic<bool> done{false};
    qboolean          gpu;    // the floors for the renderer, not painted here
    float             paintZ; // the height painted here
    radar2D_t         floors; // memory NULL: none
    std::vector<byte> px;
    int               pxW, pxH;
} rm;

// the first floor straight down from top to bottom (the world's or a brush
// entity's, the higher), not too steep, not one of column c's (within 24
// units: found again it adds nothing), with room for a crouched player. A
// column's trace down asked again (its neighbours on a flat floor ask the
// same) gives the answer it gave
static qboolean RM_Floor(int c, float x, float y, float top, float bottom, float *z)
{
    static const vec3_t mins = {-7, -7, 0}, maxs = {7, 7, 1}, cmins = {-15, -15, 18}, cmaxs = {15, 15, 50};
    const vec3_t        start = {x, y, top}, end = {x, y, bottom};
    trace_t             tr, t;
    rmDown_t            own, *d = c < (int)rm.down.size() ? &rm.down[c] : &own;

    if (d == &own || d->top != top || d->bottom != bottom) {
        CM_BoxTrace(&tr, start, end, mins, maxs, 0, CONTENTS_SOLID | CONTENTS_PLAYERCLIP, qfalse);
        for (const rmSolid_t& s : rm.solids) {
            if (x < s.mins[0] - 8 || x > s.maxs[0] + 8 || y < s.mins[1] - 8 || y > s.maxs[1] + 8 || s.mins[2] > top || s.maxs[2] < bottom) {
                continue;
            }
            CM_TransformedBoxTrace(&t, start, end, mins, maxs, s.model, CONTENTS_SOLID | CONTENTS_PLAYERCLIP, s.origin, vec3_origin, qfalse);
            if (t.startsolid || t.fraction < tr.fraction) {
                tr = t;
            }
        }
        *d = {top, bottom, {tr.endpos[0], tr.endpos[1], tr.endpos[2]}, !(tr.startsolid || tr.allsolid || tr.fraction >= 1.0f || tr.plane.normal[2] < 0.7f)};
    }
    if (!d->ok) {
        return qfalse;
    }
    *z = d->at[2];
    for (int n = rm.head[c]; n >= 0; n = rm.nodes[n].next) {
        if (fabsf(rm.nodes[n].z - *z) < 24.0f) {
            return qfalse;
        }
    }
    // a crouched player has room there
    CM_BoxTrace(&t, d->at, d->at, cmins, cmaxs, 0, CONTENTS_SOLID | CONTENTS_PLAYERCLIP, qfalse);
    for (const rmSolid_t& s : rm.solids) {
        if (t.startsolid || t.allsolid) {
            break;
        }
        if (x >= s.mins[0] - 16 && x <= s.maxs[0] + 16 && y >= s.mins[1] - 16 && y <= s.maxs[1] + 16) {
            CM_TransformedBoxTrace(&t, d->at, d->at, cmins, cmaxs, s.model, CONTENTS_SOLID | CONTENTS_PLAYERCLIP, s.origin, vec3_origin, qfalse);
        }
    }
    return !t.startsolid && !t.allsolid;
}

// the column's floor within 24 units of z, else a new one; -1 when full
static int RM_Node(int c, float z, qboolean *added)
{
    *added = qfalse;
    for (int n = rm.head[c]; n >= 0; n = rm.nodes[n].next) {
        if (fabsf(rm.nodes[n].z - z) < 24.0f) {
            return n;
        }
    }
    if (rm.nodes.size() >= 400000) {
        return -1;
    }
    rm.nodes.push_back({z, rm.head[c]});
    rm.head[c] = (int)rm.nodes.size() - 1;
    *added     = qtrue;
    return rm.head[c];
}

// a floor to walk out from: the one under x y below z
static void RM_Seed(float x, float y, float top, float bottom)
{
    const int cx = (int)((x - rm.x0) / RM_CELL), cy = (int)((y - rm.y0) / RM_CELL);
    qboolean  added;
    float     z;

    if (cx >= 0 && cy >= 0 && cx < rm.cw && cy < rm.ch
        && RM_Floor(cy * rm.cw + cx, rm.x0 + (cx + 0.5f) * RM_CELL, rm.y0 + (cy + 0.5f) * RM_CELL, top, bottom, &z)) {
        const int n = RM_Node(cy * rm.cw + cx, z, &added);

        if (added) {
            rm.queue.push_back(n);
            rm.queueCell.push_back(cy * rm.cw + cx);
        }
    }
}

// out from the floors queued: onto what a jump reaches, and down from any height
static void RM_Step(size_t most)
{
    for (; rm.queued < rm.queue.size() && most; rm.queued++, most--) {
        const int   c = rm.queueCell[rm.queued], cx = c % rm.cw, cy = c / rm.cw;
        const float z = rm.nodes[rm.queue[rm.queued]].z;
        qboolean    added;

        rm.zMin = Q_min(rm.zMin, z);
        rm.zMax = Q_max(rm.zMax, z);
        for (int d = 0; d < 4; d++) {
            const int nx = cx + (d == 0) - (d == 1), ny = cy + (d == 2) - (d == 3);
            float     nz;

            if (nx < 0 || ny < 0 || nx >= rm.cw || ny >= rm.ch
                || !RM_Floor(ny * rm.cw + nx, rm.x0 + (nx + 0.5f) * RM_CELL, rm.y0 + (ny + 0.5f) * RM_CELL, z + 72.0f, z - 4096.0f, &nz)) {
                continue;
            }
            const int n = RM_Node(ny * rm.cw + nx, nz, &added);

            if (added) {
                rm.queue.push_back(n);
                rm.queueCell.push_back(ny * rm.cw + nx);
            }
        }
    }
    if (rm.queued == rm.queue.size()) {
        rm.queue.clear();
        rm.queueCell.clear();
        rm.queued = 0;
    }
}

static void RM_Walk(void)
{
    const char *p;
    vec3_t      mins, maxs;
    const int   start = Sys_Milliseconds();

    CM_ModelBounds(CM_InlineModel(0), mins, maxs);
    rm.x0 = mins[0];
    rm.y0 = mins[1];
    rm.cw = Q_min(2048, (int)((maxs[0] - mins[0]) / RM_CELL) + 1);
    rm.ch = Q_min(2048, (int)((maxs[1] - mins[1]) / RM_CELL) + 1);
    rm.head.assign(rm.cw * rm.ch, -1);
    // (up to a million columns: 24 MB)
    rm.down.assign(rm.cw * rm.ch <= (1 << 20) ? rm.cw * rm.ch : 0, {NAN, NAN, {0, 0, 0}, qfalse});
    rm.nodes.clear();
    rm.solids.clear();
    rm.queue.clear();
    rm.queueCell.clear();
    rm.queued = 0;
    rm.zMin   = 99999.0f;
    rm.zMax   = -99999.0f;
    // the brush entities one stands on (not the triggers, ladders, doors or
    // windows) first, then the spawn points and the tops of the ladders
    for (int pass = 0; pass < 2; pass++) {
        p = CM_EntityString();
        for (const char *tok = COM_Parse((char **)&p); p && tok[0] == '{'; tok = COM_Parse((char **)&p)) {
            char   classname[64] = "", model[16] = "";
            vec3_t origin        = {0, 0, 0};

            for (tok = COM_Parse((char **)&p); p && tok[0] != '}'; tok = COM_Parse((char **)&p)) {
                char key[64];

                Q_strncpyz(key, tok, sizeof(key));
                tok = COM_Parse((char **)&p);
                if (!Q_stricmp(key, "classname")) {
                    Q_strncpyz(classname, tok, sizeof(classname));
                } else if (!Q_stricmp(key, "origin")) {
                    sscanf(tok, "%f %f %f", &origin[0], &origin[1], &origin[2]);
                } else if (!Q_stricmp(key, "model")) {
                    Q_strncpyz(model, tok, sizeof(model));
                }
            }
            const int index = model[0] == '*' ? atoi(model + 1) : 0;

            if (index > 0 && index < CM_NumInlineModels()) {
                rmSolid_t s;

                s.model = CM_InlineModel(index);
                VectorCopy(origin, s.origin);
                CM_ModelBounds(s.model, s.mins, s.maxs);
                VectorAdd(s.mins, origin, s.mins);
                VectorAdd(s.maxs, origin, s.maxs);
                if (!pass && Q_stricmpn(classname, "trigger_", 8) && Q_stricmp(classname, "func_ladder") && !Q_stristr(classname, "door")
                    && Q_stricmp(classname, "func_window") && Q_stricmp(classname, "func_areaportal")) {
                    rm.solids.push_back(s);
                } else if (pass && !Q_stricmp(classname, "func_ladder")) {
                    // its top: the floors beside it
                    for (float x = s.mins[0] - RM_CELL; x <= s.maxs[0] + RM_CELL; x += RM_CELL) {
                        for (float y = s.mins[1] - RM_CELL; y <= s.maxs[1] + RM_CELL; y += RM_CELL) {
                            RM_Seed(x, y, s.maxs[2] + 72.0f, s.maxs[2] - 64.0f);
                        }
                    }
                }
            }
            if (pass && !Q_stricmpn(classname, "info_player_", 12) && Q_stricmp(classname, "info_player_intermission")) {
                RM_Seed(origin[0], origin[1], origin[2] + 40.0f, origin[2] - 200.0f);
            }
        }
    }
    RM_Step((size_t)-1);
    rm.walked = (int)rm.nodes.size();
    Com_DPrintf("Radar: %d floors (%d brush entities) in %d ms\n", (int)rm.nodes.size(), (int)rm.solids.size(), Sys_Milliseconds() - start);
}

// where a player stands that the walk didn't reach: walked out from too
static void RM_SeedPlayers(const vec3_t eye)
{
    if ((unsigned)(cls.realtime - rm.seedTime) < 250) {
        return;
    }
    rm.seedTime = cls.realtime;
    RM_Seed(eye[0], eye[1], eye[2] - 32.0f, eye[2] - 160.0f);
    // the teammates the snapshot has, on the floor under their feet: the
    // radar's own positions are x and y only (radarClient_t; its origin[2]
    // read past them, into axis[0], a height near 0), the others not known
    for (int i = 0; g_radarClients && i < cl.snap.numEntities; i++) {
        const entityState_t *e = &cl.parseEntities[(cl.snap.parseEntitiesNum + i) & (MAX_PARSE_ENTITIES - 1)];
        const radarClient_t *c;

        if (e->number < 0 || e->number >= MAX_CLIENTS) {
            continue;
        }
        c = &g_radarClients[e->number];
        if (c->time && cl.snap.serverTime - c->time < 5000) {
            RM_Seed(e->origin[0], e->origin[1], e->origin[2] + 32.0f, e->origin[2] - 96.0f);
        }
    }
}

// a column's floor nearest z, -1 none
static int RM_Nearest(int cx, int cy, float z)
{
    int best = -1;

    if (cx < 0 || cy < 0 || cx >= rm.cw || cy >= rm.ch) {
        return -1;
    }
    for (int n = rm.head[cy * rm.cw + cx]; n >= 0; n = rm.nodes[n].next) {
        if (best < 0 || fabsf(rm.nodes[n].z - z) < fabsf(rm.nodes[best].z - z)) {
            best = n;
        }
    }
    return best;
}

static void RM_Release(void *memory)
{
    ::operator delete(memory, std::align_val_t(RM_PAGE));
}

// a column's first floor, -1 none (outside the walk none)
static int RM_Head(int cx, int cy)
{
    return cx < 0 || cy < 0 || cx >= rm.cw || cy >= rm.ch ? -1 : rm.head[cy * rm.cw + cx];
}

// the corners q (bit 0 east, bit 1 south) of cell cx, cy's floors RM_Paint
// cuts: no floor on either side of it nor across it, a bit each
static int RM_Cuts(int cx, int cy)
{
    int cuts = 0;

    for (int q = 0; q < 4; q++) {
        const int x = cx + ((q & 1) ? 1 : -1), y = cy + ((q & 2) ? -1 : 1);

        if (RM_Head(x, cy) < 0 && RM_Head(cx, y) < 0 && RM_Head(x, y) < 0) {
            cuts |= 1 << q;
        }
    }
    return cuts;
}

// cell cx, cy without a floor has a corner filled (RM_Paint: floors on both
// sides of it) by a cell of more than one floor: a height can change it
static qboolean RM_FillChanges(int cx, int cy)
{
    for (int q = 0; q < 4; q++) {
        const int h = RM_Head(cx + ((q & 1) ? 1 : -1), cy), v = RM_Head(cx, cy + ((q & 2) ? -1 : 1));

        if (h >= 0 && v >= 0 && (rm.nodes[h].next >= 0 || rm.nodes[v].next >= 0)) {
            return qtrue;
        }
    }
    return qfalse;
}

// each cell's floors in the order RM_Nearest walks them (its first of equals
// drawn), their shade and stairs, and the cells a height can change: more
// than one floor, a stair, or no floor and a corner filled by a cell of more
// than one (on the worker thread, or at the load: the walk doesn't change
// meanwhile)
static radar2D_t RM_Floors(void)
{
    const int cells = rm.cw * rm.ch, n = (int)rm.nodes.size();
    int       most  = 0; // the cells that can change, at most

    for (int cy = 0; cy < rm.ch; cy++) {
        for (int cx = 0; cx < rm.cw; cx++) {
            most += rm.head[cy * rm.cw + cx] >= 0 || RM_FillChanges(cx, cy);
        }
    }
    const size_t floorsAt = ((size_t)(cells + 1) * sizeof(unsigned int) + 15) & ~(size_t)15;
    const size_t changingAt = floorsAt + (size_t)n * sizeof(radarFloor_t);
    const size_t bytes = (changingAt + (size_t)most * sizeof(unsigned int) + RM_PAGE - 1) & ~(size_t)(RM_PAGE - 1);
    byte        *memory = (byte *)::operator new(bytes, std::align_val_t(RM_PAGE));
    unsigned int *cellFloors = (unsigned int *)memory, *changing = (unsigned int *)(memory + changingAt);
    radarFloor_t *floors = (radarFloor_t *)(memory + floorsAt);
    radar2D_t     r;
    int           f = 0, m = 0;

    for (int cy = 0; cy < rm.ch; cy++) {
        for (int cx = 0; cx < rm.cw; cx++) {
            const int c     = cy * rm.cw + cx;
            const int cuts  = rm.head[c] >= 0 ? RM_Cuts(cx, cy) : 0;
            int       sides = 0;

            cellFloors[c] = f;
            for (int node = rm.head[c]; node >= 0; node = rm.nodes[node].next) {
                const float   z    = rm.nodes[node].z;
                const byte    fill = (byte)((0.2f + 0.24f * (rm.zMax > rm.zMin ? (z - rm.zMin) / (rm.zMax - rm.zMin) : 1.0f)) * 255.0f);
                radarFloor_t *fl   = &floors[f++];

                fl->z     = z;
                fl->fill  = fill / 255.0f;
                fl->alpha = fill;
                fl->sides = cuts;
                fl->pad   = 0;
                // a stair: a floor beside a little higher, within a step (40),
                // striped every other cell; a wall, a drop or a ledge is only
                // the shade's edge (no outlines, as CS2's radar: a 1-texel
                // line turned with the view broke into crawling dots)
                for (int k = 0; k < 4; k++) {
                    if (((k < 2 ? cx : cy) & 1) != 0) {
                        continue;
                    }
                    const int   nb = RM_Nearest(cx + (k == 1) - (k == 0), cy + (k == 2) - (k == 3), z);
                    const float dz = nb < 0 ? 0.0f : rm.nodes[nb].z - z;

                    if (dz >= RM_STAIR && dz <= 40.0f) {
                        fl->sides |= 16 << k;
                    }
                }
                sides |= fl->sides & 0xf0;
            }
            if (f - (int)cellFloors[c] > 1 || sides) {
                changing[m++] = c;
            }
        }
    }
    cellFloors[cells] = f;
    // then those without a floor (after the floors': a GPU's thread groups
    // each one kind)
    for (int cy = 0; cy < rm.ch; cy++) {
        for (int cx = 0; cx < rm.cw; cx++) {
            if (rm.head[cy * rm.cw + cx] < 0 && RM_FillChanges(cx, cy)) {
                changing[m++] = cy * rm.cw + cx;
            }
        }
    }

    r.cw          = rm.cw;
    r.ch          = rm.ch;
    r.texels      = rm.texels;
    r.cellFloors  = cellFloors;
    r.floors      = floors;
    r.changing    = changing;
    r.numFloors   = f;
    r.numChanging = m;
    r.memory      = memory;
    r.memoryBytes = (int)bytes;
    r.release     = RM_Release;
    return r;
}

// the picture's texels for height z, from the floors (the renderer's
// mb_radar_full_cs and mb_radar_cs make the same bytes). The floors' edge
// against no floor is smoothed as marching squares draws it: at each cell
// corner a line from one side's middle to the next side's, so a floor's
// corner with no floor on either side or across it is cut, a corner without
// a floor between floors on both sides filled (a diagonal passage of cells
// a clean diagonal band), each a triangle of half a cell's sides; a wall on
// the grid stays where it is. A texel's alpha is its floor's times how much
// of the texel the shape covers, in exact eighths (the cut's edge at 45
// degrees through texel corners and middles); a filled corner the mean of
// the two floors beside it. A shape doesn't change with the height: only
// the shades do
static void RM_Paint(const radar2D_t *r, float z, std::vector<byte> *px)
{
    static const int under[5] = {0, 1, 4, 7, 8}; // eighths of a texel under a corner's triangle, by its reach
    const int        t = r->texels, cw = r->cw, ch = r->ch, w = cw * t;
    std::vector<int> shown((size_t)cw * ch, -1); // each cell's floor nearest z, -1 none

    for (int c = 0; c < cw * ch; c++) {
        const int first = r->cellFloors[c], last = r->cellFloors[c + 1];
        int       n     = first;

        if (first == last) {
            continue;
        }
        for (int i = first + 1; i < last; i++) {
            if (fabsf(r->floors[i].z - z) < fabsf(r->floors[n].z - z)) {
                n = i;
            }
        }
        shown[c] = n;
    }
    // a cell's floor's alpha, -1 none (outside the picture none)
    auto alphaAt = [&](int x, int y) {
        return x < 0 || y < 0 || x >= cw || y >= ch || shown[y * cw + x] < 0 ? -1 : (int)r->floors[shown[y * cw + x]].alpha;
    };

    // every texel 235 240 245 (a clear one keeps its colour, so a shade's
    // edge blends into clear, not into black)
    px->resize((size_t)w * ch * t * 4);
    for (int cy = 0; cy < ch; cy++) {
        for (int cx = 0; cx < cw; cx++) {
            const int n = shown[cy * cw + cx];
            int       corner[4]; // q (bit 0 east, bit 1 south): a floor's 1 cut, else the two floors' alphas filling it
            int       stairLine = 0;

            for (int q = 0; q < 4; q++) {
                const int x = cx + ((q & 1) ? 1 : -1), y = cy + ((q & 2) ? -1 : 1);
                const int h = alphaAt(x, cy), v = alphaAt(cx, y);

                corner[q] = n >= 0 ? h < 0 && v < 0 && alphaAt(x, y) < 0 : h >= 0 && v >= 0 ? h + v : 0;
            }
            if (n >= 0) {
                const radarFloor_t *f     = &r->floors[n];
                const float         focus = Com_Clamp(0.0f, 1.0f, (120.0f - fabsf(f->z - z)) / 56.0f);

                stairLine = (byte)((f->fill + (0.22f + 0.3f * focus - f->fill) * 0.8f) * 255.0f);
            }
            // the picture's top row is the map's north (y up): a texel on
            // the cell's left, right, top or bottom row is that side's stair
            // stripe, if it has one (a corner on two sides, a cell a texel
            // across on all four), else the floor's shade; then its share of
            // the cut corners taken off, or of the filled ones given
            for (int j = 0; j < t; j++) {
                byte *o = &(*px)[(((size_t)(ch - 1 - cy) * t + j) * w + cx * t) * 4];

                for (int i = 0; i < t; i++, o += 4) {
                    const int at      = (i == 0) | (i == t - 1) << 1 | (j == 0) << 2 | (j == t - 1) << 3;
                    int       covered = 0;

                    for (int q = 0; q < 4; q++) {
                        const int reach = t - 2 * (((q & 1) ? t - 1 - i : i) + ((q & 2) ? t - 1 - j : j));

                        if (reach > 0) {
                            covered += under[Q_min(reach, 4)] * corner[q];
                        }
                    }
                    o[0] = 235;
                    o[1] = 240;
                    o[2] = 245;
                    if (n < 0) {
                        o[3] = (byte)((covered + 8) >> 4);
                    } else {
                        const int alpha = (r->floors[n].sides & at << 4) ? stairLine : r->floors[n].alpha;

                        o[3] = (byte)((alpha * (8 - covered) + 4) >> 3);
                    }
                }
            }
        }
    }
}

// the worker: the floors, and without the renderer's picture the UI's own
static void RM_Draw(void)
{
    rm.floors = RM_Floors();
    if (!rm.gpu) {
        RM_Paint(&rm.floors, rm.paintZ, &rm.px);
        rm.pxW = rm.floors.cw * rm.floors.texels;
        rm.pxH = rm.floors.ch * rm.floors.texels;
        RM_Release(rm.floors.memory);
        rm.floors.memory = NULL;
    }
    rm.done = true;
}

// a picture begun on its thread (none while one is being made)
static void RM_Picture(void)
{
    if (rm.worker.joinable()) {
        return;
    }
    rm.gpu       = re.RegisterRadar2D && re.Radar2DHeight && !rm.gpuFailed;
    rm.paintZ    = rm.focusZ;
    rm.giveAtEnd = qfalse; // (these floors given instead)
    rm.done      = false;
    rm.worker    = std::thread(RM_Draw);
}

// the floors given to the renderer (its memory from now on): its picture for
// rm.focusZ, a storey's change from then a number
static void RM_GiveFloors(void)
{
    if (re.RegisterRadar2D) {
        rm.shader = re.RegisterRadar2D("ui_bake/radar", &rm.floors, rm.focusZ);
    } else {
        RM_Release(rm.floors.memory);
        rm.shader = 0;
    }
    rm.floors.memory = NULL;
    rm.onGpu         = rm.shader != 0;
    rm.stamp         = rm.shader ? UI_BakeStamp() : -1;
    rm.gpuFailed     = !rm.shader; // (the UI's own picture next)
}

void UI_CompHud_Shutdown(void)
{
    if (rm.worker.joinable()) {
        rm.worker.join();
    }
    if (rm.floors.memory) {
        RM_Release(rm.floors.memory);
        rm.floors.memory = NULL;
    }
    // (no picture's texels made after the UI)
    UI_BakeRelease();
}

// a picture made: the radar's (the main thread's: the renderer)
static void RM_PictureDone(void)
{
    if (!rm.worker.joinable() || !rm.done) {
        return;
    }
    rm.worker.join();
    if (rm.floors.memory) {
        RM_GiveFloors();
        return;
    }
    rm.shader = uii.Rend_RegisterImage2D("ui_bake/radar", rm.px.data(), rm.pxW, rm.pxH);
    rm.stamp  = UI_BakeStamp();
    rm.onGpu  = qfalse;
}

// a cell's texels across in the picture for a radar width pixels across (a
// very large map's fewer, its picture at most 4096 across)
static int RM_Texels(float width)
{
    const float upp = RM_RANGE / (width * 0.5f); // world units a pixel

    return Q_min(Q_max(1, Q_min(6, (int)(RM_CELL / upp + 0.5f))), 4096 / Q_max(rm.cw, rm.ch));
}

// a mark's shader and size kept while its scale and the baked images stay
typedef struct {
    int       stamp, d;
    float     k, s;
    qhandle_t h;
} compMark_t;

static bool Comp_MarkKept(compMark_t *m, float k, float s, int *d)
{
    if (m->h && m->stamp == UI_BakeStamp() && m->k == k && m->s == s) {
        *d = m->d;
        return true;
    }
    return false;
}

static qhandle_t Comp_MarkKeep(compMark_t *m, float k, float s, int d, qhandle_t h)
{
    *m = {UI_BakeStamp(), d, k, s, h};
    return h;
}

// an objective's diamond (white for the current one), its box d pixels
static qhandle_t Comp_ObjectiveShader(qboolean current, float k, float s, int *d)
{
    static compMark_t kept[2];

    if (Comp_MarkKept(&kept[current], k, s, d)) {
        return kept[current].h;
    }
    const char *layers[2] = {"poly 0.047 0.055 0.071 0.9 0.5 0 1 0.5 0.5 1 0 0.5",
                             current ? "poly 1 1 1 0.95 0.5 0.12 0.88 0.5 0.5 0.88 0.12 0.5" : "poly 0.75 0.75 0.75 0.8 0.5 0.12 0.88 0.5 0.5 0.88 0.12 0.5"};

    *d = (int)(((current ? 5.5f : 4.25f) + 1.75f) * k * 2.0f * s + 0.5f);
    return Comp_MarkKeep(&kept[current], k, s, *d, UI_BakeShape("comp_obj", layers, 2, *d, *d, s));
}

// a teammate's dot, its dark edge and (on the radar, not at its edge) its
// pointer up, in a box of 2 x 2.6 radii, d pixels
static qhandle_t Comp_DotShader(int axis, qboolean clamped, float k, float s, int *d)
{
    static compMark_t kept[2][2];

    if (Comp_MarkKept(&kept[axis][clamped], k, s, d)) {
        return kept[axis][clamped].h;
    }
    const char *color = axis ? "0.8 0.533 0.533 1" : "0.533 0.569 0.8 1";
    const float rad = (clamped ? 3.9f : 5.5f) * k, rim = rad + 1.25f * k, half = Q_max(2.6f * rad, rim) + 1.0f;
    char        lines[3][96];
    const char *layers[3] = {lines[0], lines[1], lines[2]};

    Com_sprintf(lines[0], 96, clamped ? "fill 0 0 0 0" : "poly %s 0.5 %g %g %g %g %g", color, 0.5f - 2.6f * rad / half * 0.5f,
                0.5f + 0.85f * rim / half * 0.5f, 0.5f - 0.55f * rim / half * 0.5f, 0.5f - 0.85f * rim / half * 0.5f, 0.5f - 0.55f * rim / half * 0.5f);
    Com_sprintf(lines[1], 96, "circle 0.047 0.055 0.071 0.9 %g", rim / half);
    Com_sprintf(lines[2], 96, "circle %s %g", color, rad / half);
    *d = (int)(half * 2.0f * s + 0.5f);
    return Comp_MarkKeep(&kept[axis][clamped], k, s, *d, UI_BakeShape("comp_dot", layers, 3, *d, *d, s));
}

// the player, CS2's way: a dot in the HUD's accent with a white tip up (where
// they look), dark edged, the dot in the box's middle
static qhandle_t Comp_PlayerShader(float k, float s, int *d)
{
    static compMark_t kept;
    static int        accent = -1;

    if (accent == comp_accentStamp && Comp_MarkKept(&kept, k, s, d)) {
        return kept.h;
    }
    char        dot[64];
    const char *layers[4] = {"circle 0.047 0.055 0.071 0.9 0.5", "poly 0.047 0.055 0.071 0.9 0.5 0.04 0.74 0.4 0.26 0.4",
                             "poly 1 1 1 1 0.5 0.12 0.66 0.38 0.34 0.38", dot};

    Com_sprintf(dot, sizeof(dot), "circle %g %g %g 1 0.38", comp_accent[ACC_MAIN][0], comp_accent[ACC_MAIN][1], comp_accent[ACC_MAIN][2]);
    accent = comp_accentStamp;
    *d     = (int)(16.0f * 1.4f * k * s + 0.5f);
    return Comp_MarkKeep(&kept, k, s, *d, UI_BakeShape("comp_player", layers, 4, *d, *d, s));
}

static void Comp_PreBakeWidget(UIWidget *w);

/*
================
UI_CompHud_PreBake

At a map's load and after the renderer's restart: every picture the HUD can
draw baked now (the menus' shapes and faces, the kill feed's and the weapons'
pictures, the cards, the radar's marks), none at its first draw. Ahead (at
the UI's start and a map load's): their texels begun on threads of their
own, made while the rest loads, so the bake at the load's end finds them made
================
*/
void UI_CompHud_PreBake(qboolean ahead)
{
    const float s = UI_DesignScale(1920.0f, 1080.0f), k = 248.0f / 220.0f;
    const int   start = Sys_Milliseconds();
    UIWidget   *icons[2] = {NULL, NULL};
    qhandle_t   cards[3];
    int         d;
    size_t      i;

    if (!uii.IsRendererLoaded()) {
        return;
    }
    UI_BakeAhead(ahead);
    // (the small text in its font)
    Comp_HudFont();
    // (the radar at its size: its layers baked at it)
    Comp_RadarSize();
    for (i = 0; i < UI_COMPHUD_MENUS; i++) {
        Menu *m = menuManager.FindMenu(ui_compHudMenus[i]);

        if (m && m->GetContainerWidget()) {
            Comp_PreBakeWidget(m->GetContainerWidget());
        }
    }
    for (i = 0; i < ARRAY_LEN(comp_killPics); i++) {
        const compKillPic_t *p = &comp_killPics[i];
        const qboolean       mirror = Comp_Outline(p->allies);
        float                w, h;

        Comp_KillIconSize(p, qfalse, &w, &h);
        Comp_PictureShader(p->allies, (int)(w * s + 0.5f), (int)(h * s + 0.5f), s, mirror);
        Comp_KillIconSize(p, qtrue, &w, &h);
        Comp_PictureShader(p->axis, (int)(w * s + 0.5f), (int)(h * s + 0.5f), s, mirror);
    }
    Comp_PictureShader("skull_50", (int)(KF_ICON * s + 0.5f), (int)(KF_ICON * s + 0.5f), s, qfalse);
    Comp_PictureShader("headshot_50", (int)(KF_ICON * s + 0.5f), (int)(KF_ICON * s + 0.5f), s, qfalse);
    {
        int c, n;

        Comp_FeedBoxShader(s, qfalse, &c, &n);
        Comp_FeedBoxShader(s, qtrue, &c, &n);
    }
    for (i = 0; i < ARRAY_LEN(comp_weapons); i++) {
        const int kind = comp_weapons[i].kind;
        float     w, h;

        Comp_Fit(comp_weapons[i].pic, kind == WK_SIDEARM ? 110.0f : kind == WK_PRIMARY ? 160.0f : 200.0f,
                 kind == WK_SIDEARM ? 30.0f : kind == WK_PRIMARY ? 34.0f : 38.0f, s, &w, &h);
        Comp_PictureShader(comp_weapons[i].pic, (int)(w * s + 0.5f), (int)(h * s + 0.5f), s, qfalse);
    }
    Comp_EmptyShader(s, &d);
    if (menuManager.FindMenu("hud_comp_score")) {
        UIWidget *score = menuManager.FindMenu("hud_comp_score")->GetContainerWidget();

        icons[0] = score ? score->FindWidget("iconallies") : NULL;
        icons[1] = score ? score->FindWidget("iconaxis") : NULL;
    }
    Comp_CardShaders(TEAM_ALLIES, icons[0], COMP_CARD_W, s, cards);
    Comp_CardShaders(TEAM_AXIS, icons[1], COMP_CARD_W, s, cards);
    for (int axis = 0; axis < 2; axis++) {
        Comp_DotShader(axis, qfalse, k, s, &d);
        Comp_DotShader(axis, qtrue, k, s, &d);
    }
    Comp_PlayerShader(k, s, &d);
    Comp_ObjectiveShader(qtrue, k, s, &d);
    Comp_ObjectiveShader(qfalse, k, s, &d);
    // the team tag's faces and arrow (UI_DrawTeamTag)
    {
        const rlView_t v = Comp_HudView();
        int            ax, ay, aw, ah;

        RL_Face(v, TTAG_NAME, true, true);
        RL_Face(v, TTAG_HEALTH, true, true);
        // (and the followed player's plate's, HudFollow)
        RL_Face(v, FP_NAME, true);
        RL_Face(v, FP_CAPTION, false);
        TTag_ArrowBox(v, 0.0f, 0.0f, &ax, &ay, &aw, &ah);
        rl_bakeOnly = true;
        RL_Shape(ax, ay, aw, ah, ttag_arrow, {1.0f, 1.0f, 1.0f, 1.0f}, v.s);
        RL_TeamIcon(v, 0, 0.0f, 0.0f, FP_ICON);
        RL_TeamIcon(v, 1, 0.0f, 0.0f, FP_ICON);
        rl_bakeOnly = false;
    }
    UI_BakeAhead(0);
    Com_DPrintf("Competitive HUD: %s in %d ms\n", ahead ? "begun ahead" : "baked", Sys_Milliseconds() - start);
}

void UI_CompHud_MapLoaded(void)
{
    const char *map = (Comp_ConfigStrings(), cs.map);

    UI_CompHud_PreBake(qfalse);
    UI_BakeRelease();
    if (!Comp_Int("cg_hud_radar") || !strcmp(map, rm.map)) {
        return;
    }
    if (rm.worker.joinable()) {
        rm.worker.join();
    }
    if (rm.floors.memory) {
        RM_Release(rm.floors.memory); // (the last map's)
        rm.floors.memory = NULL;
    }
    Q_strncpyz(rm.map, map, sizeof(rm.map));
    RM_Walk();
    rm.stamp     = -1;
    rm.onGpu     = qfalse;
    rm.gpuFailed = qfalse;
    rm.giveAtEnd = qtrue;
}

/*
================
UI_CompHud_LoadEnd

The load's end (CL_InitCGame, after every picture the load registers): the
renderer's picture of a new map's radar made now, for the radar's size (its
first frame then gives the height), at the lowest floor till then. Last of
the load's pictures, so it takes the place in the renderer's 2D atlas that
the UI's own picture takes a frame later (renderervk: the same pixels)
================
*/
void UI_CompHud_LoadEnd(void)
{
    if (!rm.giveAtEnd) {
        return;
    }
    rm.giveAtEnd = qfalse;
    if (re.RegisterRadar2D && re.Radar2DHeight && rm.width > 0.0f && !rm.nodes.empty() && !rm.worker.joinable() && !rm.floors.memory
        && rm.stamp == -1) {
        rm.texels = RM_Texels(rm.width);
        rm.focusZ = rm.zMin;
        rm.walked = (int)rm.nodes.size();
        rm.floors = RM_Floors();
        RM_GiveFloors();
    }
}

void CL_InitRadar(radarClient_t *radars, int clientNum)
{
    g_radarClients   = radars;
    g_radarClientNum = clientNum;
}

/*
================
UIHudRadar

The radar: the map turned with the view (up is where the player looks),
the objectives, the teammates the cgame tracks (the snapshot's and the
server's radar's: never enemies), the player's dot (no N E S W: the
user's choice, session 98). Its disc and ring are its URC shapes
================
*/
class UIHudRadar : public UIWidget
{
public:
    CLASS_PROTOTYPE(UIHudRadar);
    void Draw(void) override;
};

CLASS_DECLARATION(UIWidget, UIHudRadar, NULL) {
    {NULL, NULL}
};

/*
================
RM_StoreyZ

The storey the radar shows (its floors brightest): from the feet of the
player or of the one followed, while on the ground, where a standing eye
puts it (DEFAULT_VIEWHEIGHT); a free spectator's from the eye. qfalse
while it isn't known (in the air, the followed player not sent): the last
kept. (From the eye always, a crouch, a jump or the view's bob, the
followed player's too, made the picture again every few seconds, its
upload ~1 ms of this thread each time.)
================
*/
static qboolean RM_StoreyZ(const vec3_t eye, float *z)
{
    const playerState_t *ps     = &cl.snap.ps;
    const int            follow = ps->stats[STAT_INFOCLIENT];

    if (!(ps->pm_flags & PMF_SPECTATING)) {
        *z = ps->origin[2] + DEFAULT_VIEWHEIGHT - 64.0f;
        return ps->groundEntityNum != ENTITYNUM_NONE;
    }
    // (following as the HUD tells it: from behind, or through their eyes, the snapshot theirs)
    if (!((ps->pm_flags & PMF_CAMERA_VIEW) || ps->clientNum != clc.clientNum) || follow < 0 || follow >= MAX_CLIENTS) {
        *z = eye[2] - 64.0f;
        return qtrue;
    }
    if (ps->clientNum == follow) {
        *z = ps->origin[2] + DEFAULT_VIEWHEIGHT - 64.0f;
        return ps->groundEntityNum != ENTITYNUM_NONE;
    }
    for (int i = 0; i < cl.snap.numEntities; i++) {
        const entityState_t *es = &cl.parseEntities[(cl.snap.parseEntitiesNum + i) & (MAX_PARSE_ENTITIES - 1)];

        if (es->number == follow) {
            *z = es->origin[2] + DEFAULT_VIEWHEIGHT - 64.0f;
            return es->groundEntityNum != ENTITYNUM_NONE;
        }
    }
    return qfalse;
}

void UIHudRadar::Draw(void)
{
    const float s = m_vVirtualScale[0], r = m_frame.size.width * 0.5f, k = m_frame.size.width / s / 220.0f;
    const float upp = RM_RANGE / r; // world units a pixel
    vec3_t      angles, eye;
    float       sn, cn;
    const char *map = (Comp_ConfigStrings(), cs.map);

    if (!cl.snap.valid) {
        return;
    }
    cge->CG_EyeAngles(&angles);
    cge->CG_EyePosition(&eye);
    sn = sinf(DEG2RAD(angles[YAW]));
    cn = cosf(DEG2RAD(angles[YAW]));
    // world offset to screen: up the view's forward, right its right
    auto place = [&](float wx, float wy, float *x, float *y) {
        const float dx = wx - eye[0], dy = wy - eye[1];

        *x = (dx * sn - dy * cn) / upp;
        *y = -(dx * cn + dy * sn) / upp;
    };

    // the map: walked once a map (at its load: UI_CompHud_MapLoaded), drawn
    // again a storey away or after a restart
    if (strcmp(map, rm.map)) {
        UI_CompHud_MapLoaded();
    }
    if (!rm.nodes.empty()) {
        // (tested as kept, or it is made again each time)
        const int      texels = RM_Texels(m_frame.size.width);
        float          focusZ;
        const qboolean known = RM_StoreyZ(eye, &focusZ);
        const qboolean moved = known && fabsf(focusZ - rm.focusZ) > 24.0f;

        rm.width = m_frame.size.width;
        // (the walk changes only while no picture is being made of it)
        RM_PictureDone();
        if (!rm.worker.joinable()) {
            RM_SeedPlayers(eye);
            RM_Step(RM_PER_STEP);
        }
        // the renderer's picture: another storey is a number (also while the
        // floors are listed again: that picture is made for this height)
        if (moved && rm.onGpu && rm.stamp == UI_BakeStamp()) {
            rm.focusZ = focusZ;
            re.Radar2DHeight(rm.shader, focusZ);
        }
        if (!rm.worker.joinable()
            && (rm.stamp != UI_BakeStamp() || texels != rm.texels || (moved && !rm.onGpu)
                || (rm.walked != (int)rm.nodes.size() && rm.queue.empty()))) {
            rm.texels = texels;
            if (known) {
                rm.focusZ = focusZ;
            }
            rm.walked = (int)rm.nodes.size();
            RM_Picture();
        }
    }
    if (!rm.nodes.empty() && rm.shader) {
        // the circle's points, their texels the world's under them
        {
            static float circle[48][2];
            const float  tw = rm.cw * RM_CELL, th = rm.ch * RM_CELL;
            const vec4_t white = {1, 1, 1, 1};
            float        pts[48][4];

            if (!circle[0][0]) {
                for (int i = 0; i < 48; i++) {
                    circle[i][0] = cosf(i * 2.0f * M_PI / 48.0f);
                    circle[i][1] = sinf(i * 2.0f * M_PI / 48.0f);
                }
            }
            for (int i = 0; i < 48; i++) {
                const float px = circle[i][0] * r, py = circle[i][1] * r;
                // the screen offset back to the world
                const float wx = eye[0] + (px * sn - py * cn) * upp, wy = eye[1] + (-px * cn - py * sn) * upp;

                pts[i][0] = r + px;
                pts[i][1] = r + py;
                pts[i][2] = (wx - rm.x0) / tw;
                pts[i][3] = 1.0f - (wy - rm.y0) / th;
            }
            uii.Rend_SetColor(white);
            re.DrawPolygon2D(pts, 48, rm.shader);
        }
    }

    // the objectives: a diamond, white for the current one
    for (int i = 0; i < MAX_OBJECTIVES; i++) {
        const int flags = cs.objFlags[i];
        float     x, y;

        if (!(flags & OBJ_FLAG_CURRENT) || (flags & (OBJ_FLAG_HIDDEN | OBJ_FLAG_COMPLETED))) {
            continue;
        }
        {
            int             d;
            const qhandle_t sh = Comp_ObjectiveShader(i == cs.objCurrent, k, s, &d);

            place(cs.objLoc[i][0], cs.objLoc[i][1], &x, &y);
            if (sqrtf(x * x + y * y) > r - 7.0f * k * s) {
                const float f = (r - 7.0f * k * s) / sqrtf(x * x + y * y);

                x *= f;
                y *= f;
            }
            uii.Rend_DrawPicStretched(floorf(r + x - d * 0.5f), floorf(r + y - d * 0.5f), d, d, 0, 0, 1, 1, sh);
        }
    }

    // the teammates: a dot with a dark edge and a point where they look
    if (g_radarClients && g_radarClientNum >= 0) {
        const int         axis  = cl.snap.ps.stats[STAT_TEAM] == TEAM_AXIS;
        const compMates_t mates = Comp_Mates();

        for (int i = 0; i < MAX_CLIENTS; i++) {
            const radarClient_t *c = &g_radarClients[i];
            float                x, y, dist;
            qboolean             clamped;

            if (!Comp_MateShown(mates, i)) {
                continue;
            }
            place(c->origin[0], c->origin[1], &x, &y);
            dist    = sqrtf(x * x + y * y);
            clamped = dist > r - 7.0f * k * s;
            if (clamped) {
                x *= (r - 7.0f * k * s) / dist;
                y *= (r - 7.0f * k * s) / dist;
            }
            {
                int             d;
                const qhandle_t sh  = Comp_DotShader(axis, clamped, k, s, &d);
                const float     a   = DEG2RAD(AngleSubtract(angles[YAW], RAD2DEG(atan2f(c->axis[1], c->axis[0]))));
                const vec4_t    white = {1, 1, 1, 1};

                uii.Rend_SetColor(white);
                uii.Rend_DrawPicStretched2(floorf(r + x - d * 0.5f), floorf(r + y - d * 0.5f), d, d, 0, 0, 1, 1, cosf(a), sinf(a), sh);
            }
        }
    }

    // the player: a dot in the middle, its tip up
    {
        int             d;
        const qhandle_t sh = Comp_PlayerShader(k, s, &d);

        uii.Rend_DrawPicStretched(floorf(r - d * 0.5f), floorf(r - d * 0.5f), d, d, 0, 0, 1, 1, sh);
    }
}

/*
================
UIHudMates

With no radar, the compass's tracker: a radar with no map (the ring's line
15 units inside this box, you a white dot in its middle). Each teammate
the radar shows (Comp_MateShown) a gold dot in its direction, the nearer
the middle the nearer it is (the line at 60 m and on, 25 m at 0.7 of it,
10 m at 0.43, 5 m at 0.27); what the stock compass points at too where
none of them is (the nearest teammate out of sight; an objective as the
radar's white diamond)
================
*/
class UIHudMates : public UIWidget
{
public:
    CLASS_PROTOTYPE(UIHudMates);
    void Draw(void) override;
};

CLASS_DECLARATION(UIWidget, UIHudMates, NULL) {
    {NULL, NULL}
};

// a teammate's dot: the accent, dark edged, 12 units across
static qhandle_t Comp_MateShader(float s, int *d)
{
    static compMark_t kept;
    static int        accent = -1;

    if (accent == comp_accentStamp && Comp_MarkKept(&kept, 1.0f, s, d)) {
        return kept.h;
    }
    char        dot[64];
    const char *layers[2] = {"circle 0.047 0.055 0.071 0.9 1", dot};

    Com_sprintf(dot, sizeof(dot), "circle %g %g %g 1 0.8", comp_accent[ACC_MAIN][0], comp_accent[ACC_MAIN][1], comp_accent[ACC_MAIN][2]);
    accent = comp_accentStamp;
    *d     = (int)(12.0f * s + 0.5f);
    return Comp_MarkKeep(&kept, 1.0f, s, *d, UI_BakeShape("comp_mate", layers, 2, *d, *d, s));
}

void UIHudMates::Draw(void)
{
    const float  s = m_vVirtualScale[0], r = m_frame.size.width * 0.5f, reach = r - 19.0f * s;
    const vec4_t white = {1, 1, 1, 1};
    vec3_t       angles, eye;
    qboolean     shown = qfalse; // the compass's teammate among them
    int          d;

    if (!cl.snap.valid) {
        return;
    }
    cge->CG_EyeAngles(&angles);
    cge->CG_EyePosition(&eye);
    // sh at a place right and ahead of the view (units)
    auto mark = [&](float right, float ahead, qhandle_t sh) {
        const float units = sqrtf(right * right + ahead * ahead) + 0.001f;
        const float f     = reach * Q_min(1.0f, logf(1.0f + units / (52.0f * 5.0f)) / logf(13.0f)) / units;

        uii.Rend_DrawPicStretched(floorf(r + right * f - d * 0.5f), floorf(r - ahead * f - d * 0.5f), d, d, 0, 0, 1, 1, sh);
    };
    const qhandle_t dot = Comp_MateShader(s, &d);

    uii.Rend_SetColor(white);
    if (g_radarClients && g_radarClientNum >= 0) {
        const float sn = sinf(DEG2RAD(angles[YAW])), cn = cosf(DEG2RAD(angles[YAW]));
        // (the compass says 4.3 m for any nearer: its teammate within 5 m or 30% of its place)
        const float       near2 = Square(Q_max(260.0f, 0.3f * sqrtf(Square(comp_mate.right) + Square(comp_mate.ahead))));
        const compMates_t mates = Comp_Mates();

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!Comp_MateShown(mates, i)) {
                continue;
            }
            const float dx = g_radarClients[i].origin[0] - eye[0], dy = g_radarClients[i].origin[1] - eye[1];
            const float right = dx * sn - dy * cn, ahead = dx * cn + dy * sn;

            shown |= comp_mate.on && !comp_mate.objective && Square(right - comp_mate.right) + Square(ahead - comp_mate.ahead) < near2;
            mark(right, ahead, dot);
        }
    }
    if (comp_mate.on && !shown) {
        const qhandle_t sh = comp_mate.objective ? Comp_ObjectiveShader(qtrue, 0.85f, s, &d) : dot;

        mark(comp_mate.right, comp_mate.ahead, sh);
    }
}

// a menu's widgets: their shapes and faces (the widgets' own faces too)
static void Comp_PreBakeWidget(UIWidget *w)
{
    const bool faces = !UI_BakeAhead(); // (baked where they are now, not made ahead)

    w->BakeShapes();
    if (w->getFont() && faces) {
        w->getFont()->getHeight((const float *)NULL);
    }
    if (w->isSubclassOf(UIHudFeed) && ((UIHudFeed *)w)->m_bold && faces) {
        ((UIHudFeed *)w)->m_bold->getHeight((const float *)NULL);
    }
    if (w->isSubclassOf(UIHudWeapons) && ((UIHudWeapons *)w)->m_countFont && faces) {
        ((UIHudWeapons *)w)->m_countFont->getHeight((const float *)NULL);
    }
    if (w->isSubclassOf(UIHudRadar)) {
        rm.width = w->getFrame().size.width; // (its picture made with the map's load for it)
    }
    for (UIWidget *c = w->getFirstChild(); c; c = w->getNextChild(c)) {
        Comp_PreBakeWidget(c);
    }
}
