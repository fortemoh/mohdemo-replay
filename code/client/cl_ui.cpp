/*
===========================================================================
Copyright (C) 2024 the OpenMoHAA team

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

#include "client.h"

#include "../server/server.h"
#include "../renderercommon/tr_public.h"
#include "../corepp/tiki.h"
#include "../qcommon/localization.h"
#include "../qcommon/q_version.h"

#include "cl_ui.h"
#include "../uilib/uibake.h"
#include "cl_uicomp.h"
#include "cl_uimenus.h"
#include "cl_uiextras.h"

#include <chrono>

// Changed in OPM
//  On by default, and kept across map and demo loads
static qboolean             ui_hud = qtrue;
static UIFloatingConsole   *fakk_console;
static UIFloatingDMConsole *dm_console;
static View3D              *view3d;
qboolean                    server_loading;
static qboolean             server_loading_waiting;
static str                 *s_intermediateconsole;
Menu                       *ui_pLoadingMenu;
static Menu                *ui_pConnectingMenu;

static_media_t ui_static_materials;
cvar_t        *cl_greenfps;
cvar_t        *ui_GunneryEvaluation;
cvar_t        *ui_GroinShots;
cvar_t        *ui_RightArmShots;
cvar_t        *ui_LeftArmShots;
cvar_t        *ui_RightLegShots;
cvar_t        *ui_LeftLegShots;
cvar_t        *ui_TorsoShots;
cvar_t        *ui_HeadShots;
cvar_t        *ui_NumEnemysKilled;
cvar_t        *ui_NumObjectsDestroyed;
cvar_t        *ui_NumHitsTaken;
cvar_t        *ui_PreferredWeapon;
cvar_t        *ui_Accuracy;
cvar_t        *ui_NumObjectives;
cvar_t        *ui_NumComplete;
cvar_t        *ui_NumHits;
cvar_t        *ui_NumShotsFired;
cvar_t        *ui_gotmedal;
cvar_t        *ui_success;
cvar_t        *ui_failed;
cvar_t        *ui_drawcoords;
cvar_t        *ui_consoleposition;
cvar_t        *ui_console;
// Added: the lines the ~ console keeps
static cvar_t *con_scrollback;
cvar_t        *ui_newvidmode;
cvar_t        *ui_debugload;
cvar_t        *sound_overlay;

static char          server_mapname[64];
static Menu         *scoreboard_menu;
// Added: the scoreboard is up (the Reloaded scoreboard)
static qboolean      scoreboard_up;

static const float maxWidthRes  = 1920;
static const float maxHeightRes = 1080;

static str               scoreboard_menuname;
static str               ui_sCurrentLoadingMenu;

const UColor UWhiteChatMessageColor(0.75, 0.75, 0.75);
const UColor URedChatMessageColor(1.0, 0.333, 0.333);
const UColor UGreenChatMessageColor(0.0, 1.0, 0.333, 1.0);

class ConsoleHider : public Listener
{
public:
    CLASS_PROTOTYPE(ConsoleHider);

    void HideConsole(Event *ev);
    void DeadConsole(Event *ev);
};

CLASS_DECLARATION(Listener, ConsoleHider, NULL) {
    {&UIFloatingWindow::W_ClosePressed, &ConsoleHider::HideConsole},
    {&W_Destroyed,                      &ConsoleHider::DeadConsole},
    {NULL,                              NULL                      }
};

void ConsoleHider::HideConsole(Event *ev)
{
    UI_CloseConsole();
}

void ConsoleHider::DeadConsole(Event *ev)
{
    if (fakk_console && !fakk_console->IsDying()) {
        delete fakk_console;
        fakk_console = NULL;
    }
}

static ConsoleHider s_consolehider;

/*
====================
ConsoleCommandHandler
====================
*/
static void ConsoleCommandHandler(const char *txt)
{
    Cbuf_AddText(txt);
}

/*
====================
getDefaultConsoleRectangle
====================
*/
static UIRect2D getDefaultConsoleRectangle(void)
{
    float    f[4];
    int      i;
    UIRect2D rect;

    if (sscanf(ui_consoleposition->string, "%f %f %f %f", &f[0], &f[1], &f[2], &f[3]) == 4) {
        for (i = 0; i < 4; i++) {
            f[i] = floor(f[i]);
        }

        rect.pos.x       = f[0] - uid.vidWidth;
        rect.pos.y       = f[1] - uid.vidHeight;
        rect.size.width  = f[2] + 50.0;
        rect.size.height = f[3] + 50.0;
    } else {
        rect.pos.x       = 25.0;
        rect.pos.y       = 25.0;
        rect.size.width  = (uid.vidWidth - 50);
        rect.size.height = (uid.vidHeight / 2);
    }

    return rect;
}

class DMConsoleHider : public Listener
{
public:
    CLASS_PROTOTYPE(DMConsoleHider);

    void HideDMConsole(Event *ev);
    void DeadDMConsole(Event *ev);
};

CLASS_DECLARATION(Listener, DMConsoleHider, NULL) {
    {&UIFloatingWindow::W_ClosePressed, &DMConsoleHider::HideDMConsole},
    {&W_Deactivated,                    &DMConsoleHider::HideDMConsole},
    {&W_Destroyed,                      &DMConsoleHider::DeadDMConsole},
    {NULL,                              NULL                          }
};

void DMConsoleHider::HideDMConsole(Event *ev)
{
    UI_CloseDMConsole();
}

void DMConsoleHider::DeadDMConsole(Event *ev)
{
    if (dm_console && !dm_console->IsDying()) {
        delete dm_console;
        dm_console = NULL;
    }
}

static DMConsoleHider s_dmconsolehider;

/*
====================
UI_CompleteLine

Added: Tab in the ~ console. The engine's completion (Field_AutoComplete, as in the
dedicated server's console): the line's last word, a command's argument by the command's
own completion (set and the others a cvar, exec a config, map and the others a map); the
matches listed when more than one; a cvar's value shown when one is completed
====================
*/
static void UI_CompleteLine(char *line, int size)
{
    field_t field;
    char   *word;
    cvar_t *var;
    size_t  len;

    // (Field_CompleteCommand reads the character before an empty line)
    if (!line[strspn(line, " \"")] || strlen(line) >= sizeof(field.buffer)) {
        return;
    }

    Field_Clear(&field);
    Q_strncpyz(field.buffer, line, sizeof(field.buffer));
    field.cursor = strlen(field.buffer);
    Field_AutoComplete(&field);

    if (!strcmp(field.buffer, line)) {
        return;
    }
    Q_strncpyz(line, field.buffer, size);

    // a word completed whole (a space after it): a cvar's value
    len = strlen(field.buffer);
    if (field.buffer[len - 1] != ' ') {
        return;
    }
    field.buffer[len - 1] = 0;
    for (word = field.buffer + len - 1; word > field.buffer && !strchr(" ;\"", word[-1]); word--) {}
    if (*word == '\\' || *word == '/') {
        word++;
    }
    var = Cvar_FindVar(word);
    if (var) {
        Com_Printf("    %s = \"%s\"\n", var->name, var->string);
    }
}

/*
====================
getDefaultConsoleRectangle
====================
*/
static UIRect2D getDefaultDMConsoleRectangle(void)
{
    float    f[4];
    int      i;
    UIRect2D rect;

    if (sscanf(ui_consoleposition->string, "%f %f %f %f", &f[0], &f[1], &f[2], &f[3]) == 4) {
        for (i = 0; i < 4; i++) {
            f[i] = floor(f[i]);
        }

        rect.pos.x       = f[0] - uid.vidWidth;
        rect.pos.y       = f[1] - uid.vidHeight;
        rect.size.width  = f[2] + 50.0;
        rect.size.height = f[3] + 50.0;
    } else {
        rect.pos.x       = 0;
        rect.pos.y       = uid.vidHeight * 0.58;
        rect.size.width  = uid.vidWidth;
        rect.size.height = uid.vidHeight * 0.415;
    }

    return rect;
}

/*
====================
getDefaultConsoleRectangle
====================
*/
static UIRect2D getQuickMessageDMConsoleRectangle(void)
{
    float    f[4];
    int      i;
    UIRect2D rect;

    if (sscanf(ui_consoleposition->string, "%f %f %f %f", &f[0], &f[1], &f[2], &f[3]) == 4) {
        for (i = 0; i < 4; i++) {
            f[i] = floor(f[i]);
        }

        rect.pos.x       = f[0] - uid.vidWidth;
        rect.pos.y       = f[1] - uid.vidHeight;
        rect.size.width  = f[2] + 50.0;
        rect.size.height = f[3] + 50.0;
    } else {
        rect.pos.x      = 0;
        rect.pos.y      = uid.vidHeight * 0.66;
        rect.size.width = uid.vidWidth;
        // Fixed in 2.0
        //  Was 38.0 in 1.11 and below
        //  This prevents characters to be seen from the DM console
        //  in the quick message console
        rect.size.height = 36.0 * uid.scaleRes[1];
    }

    return rect;
}

/*
====================
UI_DMMessageModesMatch
====================
*/
static qboolean UI_DMMessageModesMatch(int iMode)
{
    qboolean bQuickMessage = qfalse;

    if (iMode && (iMode != 300 || dm_console->GetQuickMessageMode())) {
        if (iMode < 0) {
            bQuickMessage = qtrue;
            iMode         = -iMode;
        }

        if (dm_console->GetQuickMessageMode() == bQuickMessage) {
            return dm_console->GetMessageMode() == iMode;
        } else {
            return qfalse;
        }
    } else {
        return qtrue;
    }
}

/*
====================
UI_SetDMConsoleMode
====================
*/
static void UI_SetDMConsoleMode(int iMode)
{
    qboolean bQuickMessage;

    if (UI_DMMessageModesMatch(iMode)) {
        return;
    }

    bQuickMessage = qfalse;

    if (iMode < 0) {
        bQuickMessage = qtrue;
        iMode         = -iMode;
    }

    if (iMode == 300) {
        iMode = dm_console->GetMessageMode();
    }

    if (dm_console->GetQuickMessageMode()) {
        if (!bQuickMessage) {
            dm_console->setFrame(getDefaultDMConsoleRectangle());
            dm_console->SetQuickMessageMode(qfalse);
        }
    } else {
        if (bQuickMessage) {
            dm_console->setFrame(getQuickMessageDMConsoleRectangle());
            dm_console->SetQuickMessageMode(bQuickMessage);
        }
    }

    dm_console->SetMessageMode(iMode);

    if (bQuickMessage) {
        if (iMode == 100) {
            dm_console->setTitle(Sys_LV_CL_ConvertString("Quick Chat (Press Enter to send) : Messaging to All"));
        } else if (iMode == 200) {
            dm_console->setTitle(Sys_LV_CL_ConvertString("Quick Chat (Press Enter to send) : Messaging to Team"));
        } else {
            dm_console->setTitle(
                va("%s %i", Sys_LV_CL_ConvertString("Quick Chat (Press Enter to send) : Messaging to Client"), iMode)
            );
        }
    } else {
        if (iMode == 100) {
            dm_console->setTitle(Sys_LV_CL_ConvertString(
                "Chat Window (Enter to send/close) (all|team|private to change mode) : Messaging to All"
            ));
        } else if (iMode == 200) {
            dm_console->setTitle(Sys_LV_CL_ConvertString(
                "Chat Window (Enter to send/close) (all|team|private to change mode) : Messaging to Team"
            ));
        } else {
            dm_console->setTitle(
                va("%s %i",
                   Sys_LV_CL_ConvertString(
                       "Chat Window (Enter to send/close) (all|team|private to change mode) : Messaging to Client"
                   ),
                   iMode)
            );
        }
    }
}

static void DMConsoleCommandHandler(const char *txt)
{
    int  iMode;
    char szStringOut[1024];

    iMode = 0;

    if (dm_console->GetMessageMode() != 100) {
        if (dm_console->GetMessageMode() == 200) {
            iMode = -1;
        } else {
            iMode = dm_console->GetMessageMode();
        }
    }

    Com_sprintf(szStringOut, sizeof(szStringOut), "dmmessage %i %s\n", iMode, txt);
    CL_AddReliableCommand(szStringOut, qfalse);
}

/*
====================
getNewConsole
====================
*/
UIFloatingConsole *getNewConsole()
{
    const char *consoleName = va("%s console version %s", PRODUCT_NAME, PRODUCT_VERSION);

    UIFloatingConsole *console = new UIFloatingConsole;
    console->Create(NULL, getDefaultConsoleRectangle(), consoleName, UWindowColor, UHudColor);
    console->setConsoleHandler(ConsoleCommandHandler);
    console->setConsoleBackground(UBlack, 0.8f);
    // Added: read here, so a change takes effect when the console is made again (the next start)
    Cvar_CheckRange(con_scrollback, MAX_CONSOLE_ITEMS, 4000, qtrue);
    console->setScrollback(con_scrollback->integer);
    console->setShow(false);
    console->Connect(&s_consolehider, UIFloatingWindow::W_ClosePressed, UIFloatingWindow::W_ClosePressed);
    console->Connect(&s_consolehider, W_Destroyed, W_Destroyed);

    return console;
}

/*
====================
getNewDMConsole
====================
*/
UIFloatingDMConsole *getNewDMConsole()
{
    UIFloatingDMConsole *console = new UIFloatingDMConsole;
    Event               *event;

    console->Create(
        NULL,
        getDefaultDMConsoleRectangle(),
        "Chat Window (Enter to send/close) (all|team|private to change message mode) : Messaging to All",
        UWindowColor,
        UHudColor
    );

    console->setConsoleHandler(DMConsoleCommandHandler);
    console->setConsoleBackground(UBlack, 0.8f);
    console->setShow(false);
    // Added: faded by ui_hud_dmconsole (0: not drawn, the chat's typing line instead)
    event = new Event(EV_Layout_AlphaCvar);
    event->AddString("ui_hud_dmconsole");
    console->ProcessEvent(event);
    console->Connect(&s_dmconsolehider, UIFloatingWindow::W_ClosePressed, UIFloatingWindow::W_ClosePressed);
    console->Connect(&s_dmconsolehider, W_Destroyed, W_Destroyed);

    event = new Event(EV_Widget_Disable);
    console->PassEventToWidget("closebutton", event);

    event = new Event(EV_Widget_Disable);
    console->PassEventToWidget("minimizebutton", event);

    return console;
}

/*
====================
UI_GetHighResolutionScale
====================
*/
void UI_GetHighResolutionScale(vec2_t scale)
{
    if (uid.bHighResScaling) {
        scale[0] = uid.scaleRes[0];
        scale[1] = uid.scaleRes[1];
    } else {
        scale[0] = scale[1] = 1.0;
    }
}

/*
====================
UI_CompHuds

Added: the Competitive HUD's menus (cg_hudstyle 1 to 3, ui/hud_comp_*.urc),
shown in place of the stock HUD's
====================
*/
static void UI_CompHuds(bool show, bool messages)
{
    static Menu     *menus[UI_COMPHUD_MENUS];
    static UIWidget *containers[UI_COMPHUD_MENUS]; // (asked if shown every frame: the widget, not through the menu)
    static bool      kept[UI_COMPHUD_MENUS];       // shown with the messages only
    static int       generation = -1;

    // found again when the menus change
    if (generation != menuManager.Generation()) {
        // (messages only: the menus with the game messages, the chat, the kills and the vote)
        static const char *const messageMenus[] = {
            "hud_comp_topleft_compass", "hud_comp_killfeed", "hud_comp_chat", "hud_comp_vote", "hud_comp_chattop"
        };

        generation = menuManager.Generation();
        for (size_t i = 0; i < UI_COMPHUD_MENUS; i++) {
            menus[i]      = menuManager.FindMenu(ui_compHudMenus[i]);
            containers[i] = menus[i] ? menus[i]->GetContainerWidget() : NULL;
            kept[i]       = false;
            for (const char *name : messageMenus) {
                kept[i] = kept[i] || !strcmp(ui_compHudMenus[i], name);
            }
        }
    }
    for (size_t i = 0; i < UI_COMPHUD_MENUS; i++) {
        Menu      *hud = menus[i];
        // (the bottom's health and ammo: cg_hudstyle's preset's)
        const bool on  = (show && ui_compHudInLayout[i]) || (messages && kept[i]);

        if (containers[i] && on != containers[i]->IsVisible()) {
            if (on) {
                hud->ForceShow();
            } else {
                hud->ForceHide();
            }
        }
    }
}

/*
====================
UI_PrintConsole
====================
*/
void UI_PrintConsole(const char *msg)
{
    const UColor *pColor = NULL;
    const char   *pszString;
    char          szBlah[1024];

    pszString = msg;

    if (*pszString > 0 && (unsigned char)*pszString < MESSAGE_MAX) {
        qboolean bNormalMessage = qfalse;
        qboolean bDMMessage     = qfalse;
        qboolean bBold          = qfalse;
        qboolean bDeathMessage  = qfalse;

        // Added: the Competitive HUD's chat, game messages and kills
        // Changed in OPM: skip when ui_hud is disabled
        if (ui_hud) {
            UI_CompHud_Print(msg);
        }

        switch (*pszString) {
        case MESSAGE_YELLOW:
            bNormalMessage = qtrue;
            pColor         = &UHudColor;
            break;
        case MESSAGE_CHAT_WHITE:
            bDMMessage = qtrue;
            pColor     = &UWhiteChatMessageColor;
            break;
        case MESSAGE_WHITE:
            bBold  = qtrue;
            pColor = &UWhite;
            break;
        case MESSAGE_CHAT_RED:
            bDeathMessage = MESSAGE_CHAT_RED;
            pColor        = &URedChatMessageColor;
            break;
        case MESSAGE_CHAT_GREEN:
            bDeathMessage = MESSAGE_CHAT_GREEN;
            pColor        = &UGreenChatMessageColor;
            break;
        }

        pszString++;

        //
        // print to the deathmatch console
        // Changed in OPM: skip when ui_hud is disabled
        //
        if (dm_console && !bNormalMessage && ui_hud) {
            if (bDMMessage) {
                dm_console->AddDMMessageText(pszString, pColor);
            } else {
                dm_console->AddText(pszString, pColor);
            }
        }

        //
        // a game message (the Competitive HUD shows it): its sound if bold,
        // marked as one in the console
        //
        if (!bDMMessage && !bDeathMessage) {
            if (bBold) {
                uii.Snd_PlaySound("objective_text");
            }

            Q_strncpyz(szBlah, "Game Message: ", sizeof(szBlah));
            Q_strcat(szBlah, sizeof(szBlah), pszString);
            pszString = szBlah;
        }
    }

    // (a line for FAKK's mini console, which the client no longer has: the console's, as other lines)
    if (*pszString == '\a') {
        pszString++;
    }

    if (fakk_console) {
        if (s_intermediateconsole) {
            fakk_console->AddText(*s_intermediateconsole, NULL);
            delete s_intermediateconsole;
            s_intermediateconsole = NULL;
        }

        fakk_console->AddText(pszString, pColor);
    } else {
        if (!s_intermediateconsole) {
            s_intermediateconsole = new str;
        }

        // Fixed: every message of the UI's start kept until the console is there (only the last one was); after
        // the start, with no console (ui_console 0), only the last, as before (not a whole session's)
        if (cls.uiStarted) {
            *s_intermediateconsole = pszString;
        } else {
            *s_intermediateconsole += pszString;
        }
    }
}

/*
====================
UI_MenuActive
====================
*/
qboolean UI_MenuActive(void)
{
    return menuManager.CurrentMenu() != NULL;
}

/*
====================
UI_FocusMenuIfExists
====================
*/
void UI_FocusMenuIfExists(void)
{
    Menu *currentMenu;
    int   r_mode;

    currentMenu = menuManager.CurrentMenu();
    r_mode      = Cvar_VariableValue("r_mode");

    if (currentMenu) {
        IN_MouseOn();

        if (view3d->IsActive()) {
            uWinMan.DeactivateCurrentControl();
        }

        currentMenu->ActivateMenu();

        if (ui_newvidmode->integer == -1) {
            Cvar_Get("ui_newvidmode", va("%d", r_mode), CVAR_RESETSTRING);
            Cvar_SetValue("ui_newvidmode", r_mode);
        }
    } else {
        UI_ActivateView3D();
        IN_MouseOff();
        Cvar_SetValue("ui_newvidmode", -1);
    }
}

/*
====================
UI_OpenConsole
====================
*/
void UI_OpenConsole(void)
{
    if (!fakk_console) {
        return;
    }

    fakk_console->setShow(true);
    uWinMan.ActivateControl(fakk_console);
    IN_MouseOn();
}

/*
====================
UI_OpenConsole
====================
*/
qboolean UI_ConsoleIsVisible(void)
{
    return fakk_console && fakk_console->IsVisible();
}

/*
====================
UI_ConsoleIsOpen
====================
*/
qboolean UI_ConsoleIsOpen(void)
{
    return fakk_console && fakk_console->IsVisible() && fakk_console->IsActive();
}

/*
====================
UI_CloseConsole
====================
*/
void UI_CloseConsole(void)
{
    if (fakk_console && fakk_console->getShow()) {
        fakk_console->setShow(false);
    }
    UI_FocusMenuIfExists();
}

/*
====================
UI_ToggleConsole
====================
*/
void UI_ToggleConsole(void)
{
    if (!fakk_console) {
        return;
    }

    if (fakk_console->IsVisible()) {
        UI_CloseConsole();
    } else {
        UI_OpenConsole();
    }
}

/*
====================
UI_OpenDMConsole
====================
*/
void UI_OpenDMConsole(int iMode)
{
    if (!dm_console) {
        return;
    }

    // Added: the chat opens empty: a line typed and not sent (the chat left with Esc, or closed by a menu,
    // the console or a change of its mode) isn't there again. Never while it is being typed in
    if (dm_console->getConsole() && !dm_console->getConsole()->IsThisOrChildActive()) {
        dm_console->getConsole()->ClearCurrentLine();
    }

    UI_SetDMConsoleMode(iMode);
    dm_console->setShow(true);
    uWinMan.ActivateControl(dm_console);
    IN_MouseOff();
}

/*
====================
UI_CloseDMConsole
====================
*/
void UI_CloseDMConsole(void)
{
    if (dm_console && dm_console->getShow()) {
        dm_console->setShow(false);
    }
    UI_FocusMenuIfExists();
}

/*
====================
UI_DMConsoleState
====================
*/
qboolean UI_DMConsoleState(int *mode, const char **line, int *caret)
{
    UIDMConsole *console;

    if (!dm_console || !dm_console->getShow() || !(console = dm_console->getConsole())) {
        return qfalse;
    }
    // (UIDMConsole::Draw's check: its window isn't drawn)
    if (!console->IsThisOrChildActive()) {
        UI_CloseDMConsole();
        return qfalse;
    }
    *mode  = console->GetMessageMode();
    *line  = console->getCurrentLine().c_str();
    *caret = (int)console->getCaret();
    return qtrue;
}

/*
====================
UI_ToggleDMConsole
====================
*/
void UI_ToggleDMConsole(int iMode)
{
    if (!dm_console) {
        return;
    }

    if (dm_console->IsVisible()) {
        if (UI_DMMessageModesMatch(iMode)) {
            UI_CloseDMConsole();
        } else {
            UI_OpenDMConsole(iMode);
        }
    } else {
        UI_OpenDMConsole(iMode);
    }
}

/*
====================
UI_KeyEvent
====================
*/
void UI_KeyEvent(int key, unsigned int time)
{
    uWinMan.KeyEvent(key, time);
}

/*
====================
UI_CharEvent
====================
*/
void UI_CharEvent(int ch)
{
    uWinMan.CharEvent(ch);
}

/*
====================
UI_ClearBackground
====================
*/
void UI_ClearBackground(void)
{
    re.Set2DWindow(0, 0, uid.vidWidth, uid.vidHeight, 0, uid.vidWidth, uid.vidHeight, 0, -1, 1);
    re.Scissor(0, 0, uid.vidWidth, uid.vidHeight);
    re.SetColor(g_color_table[0]);
    re.DrawBox(0, 0, uid.vidWidth, uid.vidHeight);
    re.SetColor(NULL);
}

/*
====================
UI_ActivateView3D
====================
*/
void UI_ActivateView3D(void)
{
    if (!view3d) {
        return;
    }

    view3d->setShow(true);
    uWinMan.ActivateControl(view3d);
}

/*
====================
UI_MenuUp
====================
*/
qboolean UI_MenuUp()
{
    return menuManager.CurrentMenu() != NULL;
}

/*
====================
HDR calibration (r_hdrOutputTest)

Two Reloaded menus (ui/hdr_calibrate_*.urc, built in; cl_uimain.cpp draws
them): the black level's (r_hdrBlack, NEXT) and the peak's (r_hdrPeak,
FINISH). NEXT pushes the peak's menu on top of the black level's, as the
stock menus chain theirs (a pushmenu right after a popmenu is dropped: both
lock the menus for their motion time), so Escape there goes back to the
black level; FINISH sets r_hdrOutputTest 0, and UI_HdrCalibrationUpdate
closes them. They draw in the bottom fifth; the renderer draws the
screen's pattern above them (r_hdrOutputTestPage, vk_output.c)
====================
*/
#define UI_HDR_MENU_BLACK "hdr_calibrate_black"
#define UI_HDR_MENU_PEAK  "hdr_calibrate_peak"

/*
====================
UI_HdrCalibrationUpdate

Every frame: r_hdrOutputTest 1 opens the black level's menu; when no
calibration menu is on top any more (Escape on the black level's)
r_hdrOutputTest goes back to 0, and while it is 0 (FINISH, the console,
the renderer refusing) the calibration menu on top is closed, one a frame
(popping waits for the menus' lock). r_hdrOutputTestPage tells the
renderer which pattern to draw: 1 black, 2 peak, 0 none
====================
*/
static void UI_HdrCalibrationUpdate(void)
{
    static cvar_t  *test, *page;
    static qboolean wasOpen;
    Menu           *menu;
    int             onPage;

    if (!test) {
        test = Cvar_Get("r_hdrOutputTest", "0", 0);
        page = Cvar_Get("r_hdrOutputTestPage", "0", CVAR_ROM);
    }

    menu   = menuManager.CurrentMenu();
    onPage = 0;
    if (menu && !str::icmp(menu->m_name, UI_HDR_MENU_BLACK)) {
        onPage = 1;
    } else if (menu && !str::icmp(menu->m_name, UI_HDR_MENU_PEAK)) {
        onPage = 2;
    }

    if (test->integer && !onPage) {
        if (wasOpen) {
            // closed (Escape)
            Cvar_Set("r_hdrOutputTest", "0");
        } else if (menuManager.FindMenu(UI_HDR_MENU_BLACK)) {
            UI_PushMenu(UI_HDR_MENU_BLACK);
            onPage = 1;
        }
    } else if (!test->integer && onPage) {
        UI_PopMenu(qfalse);
        onPage = 0;
    }

    wasOpen = onPage != 0;
    if (page->integer != onPage) {
        Cvar_Set("r_hdrOutputTestPage", va("%i", onPage));
    }
}

/*
====================
UI_StopBind

Added: a key's box waiting for a key (Options' KEYS) given up, as Esc
does, when a connect or a map's load takes the screen
====================
*/
static void UI_StopBind(void)
{
    if (uWinMan.BindActive()) {
        uWinMan.BindActive()->KeyEvent(K_ESCAPE, 0);
    }
}

/*
====================
UI_ForgetLostSave

The last save (g_lastsave: a mission's death loads it, the end screen's
TRY AGAIN) forgotten when its file is gone; asked when the Esc menu opens
====================
*/
static void UI_ForgetLostSave(void)
{
    const char *name = Cvar_Get("g_lastsave", "", 0)->string;

    if (*name && FS_ReadFileEx(Com_GetArchiveFileName(name, "sav"), NULL, qtrue) == -1) {
        Cvar_Set("g_lastsave", "");
    }
}

/*
====================
UI_Update

Updates the UI.
====================
*/
void UI_Update(void)
{
    Menu    *currentMenu;
    UIRect2D frame;

    // Added: what an error in mid-draw left of the draw cache undone
    UI_DrawReset();
    // Added: the faces asked for at the UI's start registered (their texels made meanwhile), before anything is drawn
    UI_BakeBatch(false);
    re.SetRenderTime(cls.realtime);
    CL_FillUIDef();
    uWinMan.ServiceEvents();
    UI_HdrCalibrationUpdate();

    //
    // draw the base HUD when in-game
    //
    if (cls.no_menus && clc.state == CA_ACTIVE) {
        view3d->setShow(true);
        frame = uWinMan.getFrame();
        view3d->Display(frame, 1.0);

        return;
    }

    // Added: no HUD under the Reloaded weapon and Esc menus (cl_uimenus.cpp), the
    // cgame's own told through ui_menucover (the stock HUD's spectator keys and vote, the crosshair)
    const qboolean covered = UI_Menus_Covering();
    {
        static cvar_t *ui_menucover = Cvar_Get("ui_menucover", "0", CVAR_ROM);

        if (ui_menucover->integer != covered) {
            Cvar_Set("ui_menucover", covered ? "1" : "0");
        }
    }

    currentMenu = menuManager.CurrentMenu();

    if (!server_loading && (clc.state == CA_CONNECTING || clc.state == CA_CHALLENGING) && ui_pConnectingMenu) {
        view3d->setShow(false);
        UI_ClearBackground();
        UI_StopBind();

        if (clc.connectStartTime >= cls.realtime - 1000 * cl_connect_timeout->integer) {
            // Changed: the menu looked up now (the Reloaded connect's box shown for its name)
            if (currentMenu != menuManager.FindMenu("connecting")) {
                UI_ForceMenuOff(true);
                UI_DeactiveFloatingWindows();
                UI_ForceMenu("connecting");
                uWinMan.showCursor(true);
            }
        } else {
            Com_Printf("\nConnect to server timed out\n");
            UI_ForceMenuOff(true);
            Cbuf_AddText("disconnect;pushmenu servertimeout");
        }
    } else {
        if (currentMenu && currentMenu->isFullscreen() && (!server_loading || !ui_pLoadingMenu)) {
            if (com_sv_running->integer && clc.state == CA_ACTIVE) {
                Com_FakePause();
            }

            view3d->setShow(false);
        } else if (!server_loading) {
            if (clc.state <= CA_PRIMED) {
                view3d->setShow(false);
                UI_ClearBackground();
            } else if (clc.state == CA_ACTIVE) {
                // Changed: the Reloaded Esc menu holds a mission as the stock menu did, the game shown under it
                if (covered && com_sv_running->integer && !cg_gametype->integer) {
                    Com_FakePause();
                } else {
                    Com_FakeUnpause();
                }
                view3d->setShow(true);
            } else {
                UI_ClearBackground();
                view3d->setShow(false);
            }
        } else {
            view3d->setShow(false);
            UI_ClearBackground();
            UI_StopBind();

            if (ui_pLoadingMenu) {
                if (currentMenu != ui_pLoadingMenu) {
                    UI_ForceMenuOff(true);
                    UI_DeactiveFloatingWindows();
                    UI_ForceMenu(ui_sCurrentLoadingMenu);
                    uWinMan.showCursor(false);
                }

                if (!developer->integer && UI_ConsoleIsVisible()) {
                    UI_CloseConsole();
                }
            } else if (ui_static_materials.loading) {
                ui_static_materials.loading->GetMaterial();
            }
        }
    }

    // Hide the HUD when necessary
    if (!ui_hud || covered || clc.state != CA_ACTIVE || view3d->LetterboxActive() || (currentMenu && currentMenu->isFullscreen())
        || server_loading) {
        UI_CompHuds(false, false);
    } else {
        // Changed: the Competitive HUD's menus (cl_uicomp.cpp)
        // Added: through the intermission its messages, chat, kills and vote stay
        const bool messages = (cl.snap.ps.pm_flags & (PMF_NO_HUD | PMF_INTERMISSION)) != 0;

        UI_CompHud_Frame();
        UI_CompHuds(!messages, messages);
    }

    //
    // show the scoreboard
    //
    if (scoreboard_menu) {
        if (UI_ScoreboardShown()) {
            scoreboard_menu->ForceShow();
        } else {
            scoreboard_menu->ForceHide();
        }
    }

    uWinMan.UpdateViews();
}

/*
====================
UI_ToggleMenu

Toggle a menu
====================
*/
void UI_ToggleMenu(str name)
{
    Menu *menu;

    menu = menuManager.CurrentMenu();

    if (menu) {
        UI_ForceMenuOff(false);

        // (the menu shown for the name: another's, cl_uimenus.cpp)
        if (menu != menuManager.FindMenu(name)) {
            UI_PushMenu(name);
        }
    } else {
        UI_PushMenu(name);
    }

    UI_FocusMenuIfExists();
}

/*
====================
UI_ToggleMenu_f
====================
*/
void UI_ToggleMenu_f()
{
    if (Cmd_Argc() <= 1) {
        Com_Printf("Usage: togglemenu <menuname>\n");
        return;
    }

    UI_ToggleMenu(Cmd_Argv(1));
}

/*
====================
UI_PopMenu
====================
*/
void UI_PopMenu(qboolean restore_cvars)
{
    //
    //  Added in OPM
    //   Close any active floating windows (e.g. maplist)
    //
    UI_DeactiveFloatingWindows();

    if (menuManager.CurrentMenu()) {
        menuManager.PopMenu(restore_cvars);
    }

    UI_FocusMenuIfExists();
}

/*
====================
UI_DeactiveFloatingWindows
====================
*/
void UI_DeactiveFloatingWindows(void)
{
    uWinMan.DeactiveFloatingWindows();
}

/*
====================
UI_PushMenu
====================
*/
void UI_PushMenu(const char *name)
{
    Menu    *menu  = menuManager.CurrentMenu();
    qboolean bDiff = qfalse;

    // Added: the Reloaded menus told what they are opened as (and the weapon
    // menu the server pushes after a side picked in it not shown again)
    if (!UI_Menus_Pushing(name)) {
        return;
    }

    if (menu) {
        bDiff = strcmp(menu->m_name, name) != 0;
    }

    if (!bDiff || !ui_pLoadingMenu || menu != ui_pLoadingMenu) {
        // (Added: the Esc menu's weapon or main menu in its place, the keys to it at once)
        const bool replace = UI_Menus_Replacing(name) != qfalse;

        if (menuManager.PushMenu(name, replace) && replace) {
            UI_Menus_TakeKeys();
        }
    }

    UI_FocusMenuIfExists();

    // (the Esc menu)
    if (!str::cmp(name, "dm_main")) {
        UI_ForgetLostSave();
    }
}

/*
====================
UI_ForceMenu
====================
*/
void UI_ForceMenu(const char *name)
{
    Menu    *menu  = menuManager.CurrentMenu();
    qboolean bDiff = qfalse;

    // Added: the Reloaded menus told what they are forced as (the cgame's vote menus: their page)
    if (!UI_Menus_Pushing(name)) {
        return;
    }

    if (menu) {
        bDiff = strcmp(menu->m_name, name) != 0;
    }

    if (!menu || bDiff) {
        menuManager.ForceMenu(name);
    }

    UI_FocusMenuIfExists();
}

/*
====================
UI_ShowMenu
====================
*/
void UI_ShowMenu(const char *name, qboolean bForce)
{
    Menu *pMenu;

    pMenu = menuManager.FindMenu(name);

    // Added: the Reloaded end of a mission's level pushed (cl_uimenus.cpp)
    if (pMenu && UI_Menus_End(name, pMenu, qtrue)) {
        return;
    }

    if (pMenu) {
        if (bForce) {
            pMenu->ForceShow();
        } else if (!pMenu->isVisible()) {
            Event *event = new Event(EV_ShowMenu);
            event->AddInteger(false);
            pMenu->ProcessEvent(event);
        }
    }
}

/*
====================
UI_HideMenu
====================
*/
void UI_HideMenu(const char *name, qboolean bForce)
{
    Menu *pMenu;

    pMenu = menuManager.FindMenu(name);

    // Added: the Reloaded end of a mission's level popped (cl_uimenus.cpp)
    if (pMenu && UI_Menus_End(name, pMenu, qfalse)) {
        return;
    }

    if (pMenu) {
        if (bForce) {
            pMenu->ForceHide();
        } else if (pMenu->isVisible()) {
            pMenu->ProcessEvent(EV_HideMenu);
        }
    }
}

/*
====================
UI_PushMenu_f
====================
*/
void UI_PushMenu_f(void)
{
    if (Cmd_Argc() <= 1) {
        Com_Printf("Usage: pushmenu <menuname>\n");
        return;
    }

    UI_PushMenu(Cmd_Argv(1));
}

/*
====================
UI_PushMenuSP_f
====================
*/
void UI_PushMenuSP_f(void)
{
    if (Cmd_Argc() <= 1) {
        Com_Printf("Usage: pushmenu_sp <menuname>\n");
        return;
    }

    if ((!com_cl_running || !com_cl_running->integer || clc.state == CA_DISCONNECTED || !cg_gametype->integer)
        && (!com_sv_running || !com_sv_running->integer || g_gametype->integer == GT_SINGLE_PLAYER)) {
        UI_PushMenu(Cmd_Argv(1));
    }
}

/*

====================
UI_PushMenuMP_f
====================
*/
void UI_PushMenuMP_f(void)
{
    const char *cmd;

    if (Cmd_Argc() <= 1) {
        Com_Printf("Usage: pushmenu_mp <menuname>\n");
        return;
    }

    if (com_cl_running && com_cl_running->integer && clc.state != CA_DISCONNECTED && cg_gametype->integer
        && com_sv_running && com_sv_running->integer && g_gametype->integer != GT_SINGLE_PLAYER) {
        cmd = Cmd_Argv(1);

        //
        // Push the right menu depending on the gametype
        //
        if (!Q_stricmp(cmd, "SelectTeam")) {
            switch (cg_gametype->integer) {
            case GT_FFA:
                UI_PushMenu("SelectFFAModel");
                break;
            case GT_OBJECTIVE:
                UI_PushMenu("ObjSelectTeam");
                break;
            default:
                UI_PushMenu("SelectTeam");
            }
        } else {
            UI_PushMenu(cmd);
        }
    }
}

/*
====================
UI_ForceMenu_f
====================
*/
void UI_ForceMenu_f(void)
{
    if (Cmd_Argc() <= 1) {
        Com_Printf("Usage: forcemenu <menuname>\n");
        return;
    }

    UI_ForceMenu(Cmd_Argv(1));
}

/*
====================
UI_PopMenu_f
====================
*/
void UI_PopMenu_f(void)
{
    if (Cmd_Argc() <= 1) {
        Com_Printf("Usage: popmenu <restore_cvars - set to 1 if you want all cvars restored to their original values>");
        return;
    }

    UI_PopMenu(atoi(Cmd_Argv(1)));
}

/*
====================
UI_ShowMenu_f
====================
*/
void UI_ShowMenu_f(void)
{
    qboolean    bForce;
    const char *name;

    if (Cmd_Argc() <= 1) {
        Com_Printf("Usage: showmenu <menuname>\n");
        return;
    }

    name = Cmd_Argv(1);

    if (Cmd_Argc() > 2) {
        bForce = atoi(Cmd_Argv(2));
    } else {
        bForce = qfalse;
    }

    UI_ShowMenu(name, bForce);
}

/*
====================
UI_HideMenu_f
====================
*/
void UI_HideMenu_f(void)
{
    qboolean    bForce;
    const char *name;

    if (Cmd_Argc() <= 1) {
        Com_Printf("Usage: hidemenu <menuname>\n");
        return;
    }

    name = Cmd_Argv(1);

    if (Cmd_Argc() > 2) {
        bForce = atoi(Cmd_Argv(2));
    } else {
        bForce = qfalse;
    }

    UI_HideMenu(name, bForce);
}

/*
====================
UI_WidgetCommand_f
====================
*/
void UI_WidgetCommand_f(void)
{
    str name;
    str commandstring;

    if (Cmd_Argc() <= 2) {
        Com_Printf("Usage: widgetcommand <name> <args1...argsN>\n");
        return;
    }

    name          = Cmd_Argv(1);
    commandstring = Cmd_Argv(2);
    Event *event  = new Event(commandstring);

    for (int i = 3; i < Cmd_Argc(); i++) {
        event->AddToken(Cmd_Argv(i));
    }

    menuManager.PassEventToWidget(name, event);
}

/*
====================
UI_GlobalWidgetCommand_f
====================
*/
void UI_GlobalWidgetCommand_f(void)
{
    str name;
    str commandstring;

    if (Cmd_Argc() <= 2) {
        Com_Printf("Usage: globalwidgetcommand <name> <args1...argsN>\n");
        return;
    }

    name          = Cmd_Argv(1);
    commandstring = Cmd_Argv(2);
    Event *event  = new Event(commandstring);

    for (int i = 3; i < Cmd_Argc(); i++) {
        event->AddToken(Cmd_Argv(i));
    }

    // Added: freed when no widget has the name
    if (!uWinMan.PassEventToWidget(name, event)) {
        delete event;
    }
}

/*
====================
UI_ListMenus_f
====================
*/
void UI_ListMenus_f()
{
    menuManager.ListMenus();
}

// a widget for menustate: its class, its name, the menu it is in
static const char *UI_WidgetDesc(UIWidget *w, char *buf, size_t size)
{
    Menu *const m = w ? menuManager.MenuOf(w) : NULL;

    if (!w) {
        return "nothing";
    }
    Com_sprintf(buf, size, "%s%s%s%s%s", w->getClassname(), w->getName()[0] ? " " : "", w->getName(), m ? " of " : "",
                m ? m->m_name.c_str() : "");
    return buf;
}

/*
====================
UI_MenuState_f

Added: menustate, what the menus and the keys and mouse are doing, printed and added to menustate.txt in the home's
game folder: typed in the console over a menu that takes no click or Esc
====================
*/
void UI_MenuState_f(void)
{
    static const char *const teams[] = {"none", "spectator", "free-for-all", "allies", "axis"};
    UIPoint2D                pos((float)uid.mouseX, (float)uid.mouseY);
    const int                catcher = Key_GetCatcher();
    const int                team    = cl.snap.valid ? cl.snap.ps.stats[STAT_TEAM] : -1;
    char                     a[128], b[128], c[128];
    qtime_t                  t;
    str                      text;
    fileHandle_t             f;

    Com_RealTime(&t);
    text = va("menustate %04d-%02d-%02d %02d:%02d:%02d\n", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
    text += "  menus: ";
    text += menuManager.DescribeState();
    text += va("\n  keys: %s, key capture %s, catcher%s%s%s%s%s\n", UI_WidgetDesc(uWinMan.ActiveControl(), a, sizeof(a)),
               uWinMan.BindActive() ? UI_WidgetDesc(uWinMan.BindActive(), b, sizeof(b)) : "none", catcher ? "" : " none",
               catcher & KEYCATCH_CONSOLE ? " console" : "", catcher & KEYCATCH_UI ? " UI" : "", catcher & KEYCATCH_MESSAGE ? " message" : "",
               catcher & KEYCATCH_CGAME ? " cgame" : "");
    text += va("  mouse: %s, %s, at %d %d, buttons %u, a click goes to %s\n", in_guimouse ? "the UI's" : "the game's",
               IN_IsCursorActive() ? "captured" : "free", uid.mouseX, uid.mouseY, cl.mouseButtons, UI_WidgetDesc(uWinMan.ClickTarget(pos), c, sizeof(c)));
    text += va("  game: state %d, team %s, loading %s; %s\n", clc.state, team >= 0 && team < (int)ARRAY_LEN(teams) ? teams[team] : "unknown",
               server_loading ? "yes" : "no", UI_Menus_State());
    Com_Printf("%s", text.c_str());

    f = FS_FOpenFileAppend_HomeData("menustate.txt");
    if (f) {
        FS_Write(text.c_str(), text.length(), f);
        FS_FCloseFile(f);
        Com_Printf("(added to menustate.txt)\n");
    }
}

/*
====================
UI_BindActive
====================
*/
qboolean UI_BindActive(void)
{
    return uWinMan.BindActive() != NULL;
}

/*
====================
UI_MenuEscape
====================
*/
void UI_MenuEscape(const char *name)
{
    if (server_loading) {
        return;
    }

    if (uWinMan.BindActive()) {
        UI_KeyEvent(K_ESCAPE, qfalse);
        return;
    }

    // Changed: the main menu looked up now (the Reloaded one shown for its name)
    if (menuManager.CurrentMenu() == menuManager.FindMenu("main") && clc.state == CA_DISCONNECTED) {
        if (developer->integer) {
            if (UI_ConsoleIsVisible()) {
                UI_CloseConsole();
            } else {
                UI_OpenConsole();
            }
        }
        return;
    }

    if (menuManager.CurrentMenu()) {
        menuManager.PopMenu(qtrue);
    } else if (!Q_stricmp(name, "main") && clc.state > CA_PRIMED) {
        // in a game (Changed: single player's too, the Reloaded Esc menu shown for dm_main has a mission's own tiles)
        UI_PushMenu("dm_main");
    } else {
        UI_PushMenu(name);
    }

    UI_FocusMenuIfExists();
}

/*
====================
UI_ForceMenuOff
====================
*/
void UI_ForceMenuOff(bool force)
{
    menuManager.ClearMenus(force);
    UI_FocusMenuIfExists();
}

/*
====================
UI_DrawConnect
====================
*/
void UI_DrawConnect(void)
{
    Com_Printf("UI_DrawConnect called\n");
}

/*
====================
UI_StartDMMap_f
====================
*/
void UI_StartDMMap_f(void)
{
    int         iDedicated;
    int         iMaxClients;
    int         iUseGameSpy;
    int         iGameType = GT_FFA;
    int         iFragLimit;
    int         iTimeLimit;
    int         iTeamDamage;
    int         iInactiveSpectate;
    int         iInactiveKick;
    const char *pszTemp;
    const char *pszGameTypeString = "Free-For-All";
    const char *pszMapName;
    const char *pszMapListCvar = "sv_maplist";
    const char *pszMapListString;
    char        szHostName[32];

    if (Cmd_Argc() > 1) {
        iGameType = atoi(Cmd_Argv(1));
        if (Cmd_Argc() > 2) {
            pszGameTypeString = Cmd_Argv(2);

            if (Cmd_Argc() > 3) {
                pszMapListCvar = Cmd_Argv(3);
            }
        }
    }

    pszMapName = CvarGetForUI("ui_dmmap", "");

    if (!*pszMapName) {
        return;
    }

    iDedicated = atoi(CvarGetForUI("ui_dedicated", "0"));

    if (iDedicated < 0) {
        iDedicated = 0;
    } else if (iDedicated > 1) {
        iDedicated = 1;
    }

    Cvar_SetValue("ui_dedicated", iDedicated);

    iMaxClients = atoi(CvarGetForUI("ui_maxclients", "0"));

    if (iMaxClients < 1) {
        iMaxClients = 1;
    } else if (iMaxClients > MAX_CLIENTS) {
        iMaxClients = MAX_CLIENTS;
    }

    Cvar_SetValue("ui_maxclients", iMaxClients);

    iUseGameSpy = atoi(CvarGetForUI("ui_gamespy", "0"));

    if (iUseGameSpy < 0) {
        iUseGameSpy = 0;
    } else if (iUseGameSpy > 1) {
        iUseGameSpy = 1;
    }

    Cvar_SetValue("ui_gamespy", iUseGameSpy);

    iFragLimit = atoi(CvarGetForUI("ui_fraglimit", "0"));

    if (iFragLimit < 0) {
        iFragLimit = 0;
    }

    Cvar_SetValue("ui_fraglimit", iFragLimit);

    iTimeLimit = atoi(CvarGetForUI("ui_timelimit", "0"));

    if (iTimeLimit < 0) {
        iTimeLimit = 0;
    }

    Cvar_SetValue("ui_timelimit", iTimeLimit);

    iTeamDamage = atoi(CvarGetForUI("ui_teamdamage", "0"));

    if (iTeamDamage < 0) {
        iTeamDamage = 0;
    } else if (iTeamDamage > 1) {
        iTeamDamage = 1;
    }

    Cvar_SetValue("ui_teamdamage", iTeamDamage);

    iInactiveSpectate = atoi(CvarGetForUI("ui_inactivespectate", "0"));

    if (iInactiveSpectate < 0) {
        iInactiveSpectate = 0;
    }

    Cvar_SetValue("ui_inactivespectate", iInactiveSpectate);

    iInactiveKick = atoi(CvarGetForUI("ui_inactivekick", "0"));

    if (iInactiveKick < 0) {
        iInactiveKick = 0;
    }

    Cvar_SetValue("ui_inactivekick", iInactiveKick);

    pszMapListString = CvarGetForUI(pszMapListCvar, "");

    pszTemp = CvarGetForUI("ui_hostname", "Nameless Battle");
    Q_strncpyz(szHostName, pszTemp, sizeof(szHostName));

    for (int i = 0; i < sizeof(szHostName); i++) {
        if (szHostName[i] == ';' || szHostName[i] == '"' || szHostName[i] == '\\') {
            szHostName[i] = '_';
        }
    }

    Cvar_Set("ui_hostname", szHostName);

    Cbuf_AddText(
        va("set dedicated %i;"
           "set sv_maxclients %i;"
           "set sv_gamespy %i;"
           "set g_gametype %i;"
           "set g_gametypestring \"%s\";"
           "set fraglimit %i;"
           "set timelimit %i;"
           "set g_teamdamage %i;"
           "set g_inactivespectate %i;"
           "set g_inactivekick %i;"
           "set sv_maplist \"%s\";"
           "set sv_hostname \"%s\";"
           "set cheats 0;"
           "wait;"
           "map \"%s\"\n",
           iDedicated,
           iMaxClients,
           iUseGameSpy,
           iGameType,
           pszGameTypeString,
           iFragLimit,
           iTimeLimit,
           iTeamDamage,
           iInactiveSpectate,
           iInactiveKick,
           pszMapListString,
           szHostName,
           pszMapName)
    );
}

/*
====================
UI_GetPlayerModel_f
====================
*/
void UI_GetPlayerModel_f(void)
{
    const char *pszUIPlayerModel;
    char        donotshowssindeorfr[64];

    //
    // Allies
    //
    pszUIPlayerModel = CvarGetForUI("dm_playermodel", "american_army");
    Cvar_Set("ui_dm_playermodel", PM_FilenameToDisplayname(pszUIPlayerModel));
    Cvar_Set("ui_dm_playermodel_set", pszUIPlayerModel);
    Cvar_Set("ui_disp_playermodel", va("models/player/%s.tik", pszUIPlayerModel));

    //
    // Axis
    //
    pszUIPlayerModel = CvarGetForUI("dm_playergermanmodel", "german_wehrmacht_soldier");
    Cvar_Set("ui_dm_playergermanmodel", PM_FilenameToDisplayname(pszUIPlayerModel));
    Cvar_Set("ui_dm_playergermanmodel_set", pszUIPlayerModel);

    if (!Q_stricmpn(pszUIPlayerModel, "german_waffen_", 14)) {
        Q_strncpyz(donotshowssindeorfr, "german_waffenss_", sizeof(donotshowssindeorfr));
        Q_strcat(donotshowssindeorfr, sizeof(donotshowssindeorfr), pszUIPlayerModel + 14);
        Cvar_Set("ui_disp_playergermanmodel", va("models/player/%s.tik", donotshowssindeorfr));
    } else {
        Cvar_Set("ui_disp_playergermanmodel", va("models/player/%s.tik", pszUIPlayerModel));
    }

    Cvar_Set("ui_name", CvarGetForUI("name", "UnnamedSoldier"));
}

/*
====================
UI_LoadMenu
====================
*/
void UI_LoadMenu(const char *name)
{
    char buffer[256];

    Com_sprintf(buffer, sizeof(buffer), "ui/%s", name);
    COM_StripExtension(buffer, buffer, sizeof(buffer));
    Q_strcat(buffer, 256, ".urc");

    new UILayout(buffer);
    uWinMan.CreateMenus();
    UI_FocusMenuIfExists();
}

/*
====================
UI_LoadMenu_f
====================
*/
void UI_LoadMenu_f(void)
{
    UI_LoadMenu(Cmd_Argv(1));
}

/*
====================
CvarGetForUI
====================
*/
const char *CvarGetForUI(const char *name, const char *defval)
{
    cvar_t *cvar = Cvar_FindVar(name);

    if (cvar) {
        if (cvar->latchedString && *cvar->latchedString) {
            return cvar->latchedString;
        } else {
            return cvar->string;
        }
    } else {
        return defval;
    }
}

/*
====================
ListFilesForUI
====================
*/
void ListFilesForUI(const char *filespec)
{
    int    nfiles;
    char **filenames;

    filenames = FS_ListFiles("", filespec, qfalse, &nfiles);

    for (int i = 0; i < nfiles; i++) {
        uie.AddFileToList(filenames[i]);
    }

    FS_FreeFileList(filenames);
}

/*
====================
IsKeyDown
====================
*/
int IsKeyDown(int key)
{
    return keys[key].down;
}

/*
====================
UI_WantsKeyboard
====================
*/
void UI_WantsKeyboard()
{
    Key_SetCatcher(Key_GetCatcher() | KEYCATCH_UI);
}

struct widgettrans_s {
    const char *src;
    const char *dst;
};

static widgettrans_s s_widgettrans[2] = {
    {"Label", "FakkLabel"},
    {NULL,    NULL       }
};

/*
====================
TranslateWidgetName
====================
*/
const char *TranslateWidgetName(const char *widget)
{
    widgettrans_s *trans = &s_widgettrans[0];

    for (int i = 0; trans->src != NULL; i++, trans++) {
        if (!strcmp(trans->src, widget)) {
            return trans->dst;
        }
    }

    return NULL;
}

/*
====================
UI_Cvar_Set
====================
*/
void UI_Cvar_Set(const char *var_name, const char *value)
{
    Cvar_Set2(var_name, value, qfalse);
}

/*
====================
CL_FillUIDef
====================
*/
void CL_FillUIDef(void)
{
    CL_GetMouseState(&uid.mouseX, &uid.mouseY, &uid.mouseFlags);
    uid.time       = cls.realtime;
    uid.vidHeight  = cls.glconfig.vidHeight;
    uid.vidWidth   = cls.glconfig.vidWidth;
    uid.uiHasMouse = in_guimouse != qfalse;
}

/*
====================
UI_RegisterSound
====================
*/
sfxHandle_t UI_RegisterSound(const char *sample, qboolean streamed)
{
    return S_RegisterSound(sample, streamed, qfalse);
}

/*
====================
UI_FadeSound
====================
*/
void UI_FadeSound(float fTime)
{
    S_FadeSound(fTime);
}

/*
====================
UI_StopAll
====================
*/
void UI_StopAll()
{
    S_StopAllSounds2(qtrue);
}

/*
====================
UI_StartLocalSound
====================
*/
void UI_StartLocalSound(const char *sound_name)
{
    S_StartLocalSound(sound_name, qtrue);
}

/*
====================
UI_StartLocalSoundDialog
====================
*/
void UI_StartLocalSoundDialog(const char *sound_name)
{
    S_StartLocalSoundChannel(sound_name, qtrue, CHAN_DIALOG);
}

/*
====================
UI_PickFile

Changed: nothing, the file picker (a developer's tool) is gone; the UI
import table keeps its slot (uii.File_PickFile)
====================
*/
static void UI_PickFile(const char *name, Listener *obj, Event& event) {}

/*
====================
CL_FillUIImports
====================
*/
void CL_FillUIImports(void)
{
    uii.Rend_DrawBox           = re.DrawBox;
    uii.Rend_DrawPicStretched  = re.DrawStretchPic;
    uii.Rend_DrawPicStretched2 = re.DrawStretchPic2;
    uii.Rend_DrawPicTiled      = re.DrawTilePic;
    uii.Rend_GetShaderHeight   = re.GetShaderHeight;
    uii.Rend_GetShaderWidth    = re.GetShaderWidth;
    uii.Rend_DrawString        = re.DrawString;
    uii.Rend_LoadFont          = re.LoadFont;
    uii.Rend_RegisterMaterial  = re.RegisterShaderNoMip;
    uii.Rend_RefreshMaterial   = re.RefreshShaderNoMip;
    uii.Rend_Scissor           = re.Scissor;
    uii.Rend_Set2D             = re.Set2DWindow;
    uii.Rend_SetColor          = re.SetColor;
    uii.Rend_ImageExists       = re.ImageExists;

    uii.Cmd_Stuff                   = Cbuf_AddText;
    uii.Cvar_GetString              = CvarGetForUI;
    uii.Cvar_Reset                  = Cvar_Reset;
    uii.Cvar_Find                   = Cvar_FindVar;
    uii.Cvar_Set                    = UI_Cvar_Set;
    uii.File_PickFile               = UI_PickFile;
    uii.File_FreeFile               = FS_FreeFile;
    uii.File_ListFiles              = ListFilesForUI;
    uii.File_OpenFile               = FS_ReadFile;
    uii.File_WriteFile              = FS_WriteTextFile;
    uii.Snd_PlaySound               = UI_StartLocalSound;
    uii.Snd_PlaySoundDialog         = UI_StartLocalSoundDialog;
    uii.Snd_RegisterSound           = UI_RegisterSound;
    uii.Snd_FadeSound               = UI_FadeSound;
    uii.Snd_StopAllSound            = UI_StopAll;
    uii.Alias_Add                   = Alias_Add;
    uii.Alias_FindRandom            = Alias_FindRandom;
    uii.Sys_Error                   = Com_Error;
    uii.Sys_IsKeyDown               = IsKeyDown;
    uii.Sys_Milliseconds            = Sys_Milliseconds;
    uii.Sys_Printf                  = Com_Printf;
    uii.Sys_DPrintf                 = Com_DPrintf;
    uii.Sys_GetClipboard            = Sys_GetWholeClipboard;
    uii.Sys_SetClipboard            = Sys_SetClipboard;
    uii.Cmd_CompleteCommandByNumber = Cmd_CompleteCommandByNumber;
    uii.Cvar_CompleteCvarByNumber   = Cvar_CompleteVariableByNumber;
    uii.UI_WantsKeyboard            = UI_WantsKeyboard;
    uii.Client_TranslateWidgetName  = TranslateWidgetName;
    uii.Connect                     = CL_Connect;
    uii.Key_GetKeynameForCommand    = Key_GetKeynameForCommand;
    uii.Key_GetCommandForKey        = Key_GetBinding;
    uii.Key_SetBinding              = Key_SetBinding;
    uii.Key_GetKeysForCommand       = Key_GetKeysForCommand;
    uii.Key_KeynumToString          = Key_KeynumToBindString;

    uii.GetConfigstring   = CL_ConfigString;
    uii.UI_CloseDMConsole = UI_CloseDMConsole;

    uii.GetRefSequence    = CL_GetRefSequence;
    uii.IsRendererLoaded  = CL_IsRendererLoaded;
    uii.Rend_LoadRawImage = re.LoadRawImage;
    uii.Rend_FreeRawImage = re.FreeRawImage;
    uii.Rend_RegisterImage2D = re.RegisterImage2D;
    uii.Rend_Begin2DList     = re.Begin2DList;
    uii.Rend_End2DList       = re.End2DList;
    uii.Rend_Draw2DList      = re.Draw2DList;
    uii.Rend_Free2DList      = re.Free2DList;
    uii.Cmd_CompleteLine     = UI_CompleteLine;

    uii.Rend_Set2DStartTime = re.Set2DInitialShaderTime;
}

/*
====================
CL_BeginRegistration
====================
*/
void CL_BeginRegistration(void)
{
    // init console stuff
    re.BeginRegistration(&cls.glconfig);
    uWinMan.CleanupShadersFromList();
    // Added: the baked pictures' shaders are looked up again
    UI_BakeForget();
}

/*
====================
CL_EndRegistration
====================
*/
void CL_EndRegistration(void)
{
    int start, end;

    start = Sys_Milliseconds();

    S_EndRegistration();
    re.EndRegistration();

    if (!Sys_LowPhysicalMemory()) {
        Com_TouchMemory();
    }

    end = Sys_Milliseconds();

    Com_Printf("CL_EndRegistration: %5.2f seconds\n", (float)(start - end) / 1000.0);
}

/*
====================
UI_ResolutionChange
====================
*/
void UI_ResolutionChange(void)
{
    UIRect2D frame;

    CL_FillUIImports();
    CL_FillUIDef();

    // Added in OPM
    //  Scaling for high resolutions
    if (uid.vidWidth > maxWidthRes && uid.vidHeight > maxHeightRes) {
        const float vidRatio = (float)uid.vidWidth / (float)uid.vidHeight;

        uid.scaleRes[0] = (float)uid.vidWidth / (maxHeightRes * vidRatio);
        uid.scaleRes[1] = (float)uid.vidHeight / maxHeightRes;
        //uid.scaleRes[0] = (float)uid.vidWidth / maxWidthRes;
        //uid.scaleRes[1] = (float)uid.vidHeight / maxHeightRes;
        uid.bHighResScaling = qtrue;
    } else {
        uid.scaleRes[0]     = 1;
        uid.scaleRes[1]     = 1;
        uid.bHighResScaling = qfalse;
    }

    if (!uie.ResolutionChange) {
        return;
    }

    if (fakk_console) {
        frame = getDefaultConsoleRectangle();
        fakk_console->setFrame(frame);
    }

    if (dm_console) {
        if (dm_console->GetQuickMessageMode()) {
            frame = getQuickMessageDMConsoleRectangle();
        } else {
            frame = getDefaultDMConsoleRectangle();
        }

        dm_console->setFrame(frame);
    }

    uie.ResolutionChange();
    menuManager.RealignMenus();

    if (view3d) {
        frame = UIRect2D(0, 0, uid.vidWidth, uid.vidHeight);
        view3d->setFrame(frame);
    }

    // Added: the Competitive HUD baked at the new size
    UI_CompHud_PreBake(qfalse);
    // Added: the Reloaded menus too
    UI_Menus_PreBake();
    // Added: the HUD's extras too (the zoom overlays' masks)
    UI_Extras_PreBake(qfalse);
}

/*
====================
UI_FinishLoadingScreen_f
====================
*/
void UI_FinishLoadingScreen_f(void)
{
    Com_Unpause();
    UI_ForceMenuOff(qtrue);

    ui_pLoadingMenu        = NULL;
    ui_sCurrentLoadingMenu = "";
    server_loading         = qfalse;
    server_loading_waiting = qfalse;

    UI_ActivateView3D();
}

/*
====================
S_ServerLoaded
====================
*/
void S_ServerLoaded(void)
{
    if (!svs.soundsNeedLoad) {
        return;
    }
    svs.soundsNeedLoad = qfalse;

    Com_DPrintf("Loading Previous Sound State.\n");
    S_StopAllSounds2(qfalse);

    S_TriggeredMusic_Stop();
    s_bSoundPaused = qtrue;
    S_ReLoad(&svs.soundSystem);

    if (svs.tm_filename[0]) {
        S_TriggeredMusic_SetupHandle(svs.tm_filename, svs.tm_loopcount, svs.tm_offset, 0);
    }
}

/*
====================
UI_ServerLoaded
====================
*/
void UI_ServerLoaded(void)
{
    Menu   *pCurrentMenu;
    cvar_t *pMaxClients;
    Event  *event;

    CL_UpdateSnapFlags();
    SV_ServerLoaded();
    S_FadeSound(0);
    S_ServerLoaded();

    if (!server_loading) {
        return;
    }

    UI_EndLoad();
    pMaxClients = Cvar_Get("sv_maxclients", "1", 0);

    if (com_sv_running->integer && pMaxClients->integer > 1) {
        SV_Heartbeat_f();
    }

    uWinMan.showCursor(true);

    if (!ui_pLoadingMenu) {
        UI_FinishLoadingScreen_f();
        return;
    }

    pCurrentMenu = menuManager.CurrentMenu();

    // Changed: the Reloaded load screen has no continuebutton, so a level starts once loaded;
    // ui_holdload 1 holds it until finishloadingscreen as the stock CONTINUE did (port/shot.sh's
    // fixed views: the same game time in every run)
    static cvar_t *ui_holdload = Cvar_Get("ui_holdload", "0", 0);

    if (pCurrentMenu != ui_pLoadingMenu || (!pCurrentMenu->GetNamedWidget("continuebutton") && !ui_holdload->integer)
        || !com_sv_running->integer || pMaxClients->integer > 1) {
        UI_FinishLoadingScreen_f();
        return;
    }

    if (ui_sCurrentLoadingMenu == "loading_default") {
        UI_FinishLoadingScreen_f();
        return;
    }

    server_loading_waiting = qtrue;

    event = new Event(EV_Widget_Enable);
    ui_pLoadingMenu->PassEventToWidget("continuebutton", event);

    event = new Event(EV_Widget_Disable);
    ui_pLoadingMenu->PassEventToWidget("loadingflasher", event);

    event = new Event(EV_Widget_Disable);
    ui_pLoadingMenu->PassEventToWidget("loadingbar", event);

    event = new Event(EV_Widget_Disable);
    ui_pLoadingMenu->PassEventToWidget("loadingbar_border", event);

    event = new Event(EV_Widget_Activate);
    ui_pLoadingMenu->PassEventToWidget("continuebutton", event);

    Com_FakePause();
}

/*
====================
UI_ClearCenterPrint
====================
*/
void UI_ClearCenterPrint(void)
{
    if (view3d) {
        view3d->ClearCenterPrint();
    }
}

/*
====================
UI_UpdateCenterPrint
====================
*/
void UI_UpdateCenterPrint(const char *s, float alpha)
{
    view3d->UpdateCenterPrint(s, alpha);
}

/*
====================
UI_UpdateLocationPrint
====================
*/
void UI_UpdateLocationPrint(int x, int y, const char *s, float alpha)
{
    view3d->UpdateLocationPrint(x, y, s, alpha);
}

/*
====================
UI_CenterPrint_f
====================
*/
void UI_CenterPrint_f(void)
{
    const char *s;
    float       alpha;

    if (Cmd_Argc() <= 1) {
        Com_Printf("Usage: centerprintf string [alpha]\n");
        return;
    }

    s = Cmd_Argv(1);

    if (Cmd_Argc() > 2) {
        alpha = atof(Cmd_Argv(2));
    } else {
        alpha = 1.0;
    }

    UI_UpdateCenterPrint(s, alpha);
}

/*
====================
UI_LocationPrint_f
====================
*/
void UI_LocationPrint_f(void)
{
    int         x, y;
    const char *s;
    float       alpha;

    if (Cmd_Argc() <= 3) {
        Com_Printf("Usage: locatinprint x y string [alpha]\n");
        return;
    }

    x = atoi(Cmd_Argv(1));
    y = atoi(Cmd_Argv(2));

    s = Cmd_Argv(3);

    if (Cmd_Argc() > 4) {
        alpha = atof(Cmd_Argv(4));
    } else {
        alpha = 1.0;
    }

    UI_UpdateLocationPrint(x, y, s, alpha);
}

/*
====================
UI_ClearState
====================
*/
/*
====================
UI_PrintOutlinedCentered

Added in OPM
One line of white text outlined in black, centered across the bottom of rect,
as the stock UIFont::PrintOutlinedJustified (gone with the stock menus) drew
it: the outline twelve copies two virtual pixels round it
====================
*/
static void UI_PrintOutlinedCentered(UIFont *font, const UIRect2D& rect, const char *text, const float *vVirtualScale)
{
    static const int offsets[12][2] = {
        {1,  2 },
        {2,  1 },
        {-1, 2 },
        {-2, 1 },
        {-1, -2},
        {-2, -1},
        {1,  -2},
        {2,  -1},
        {2,  0 },
        {-2, 0 },
        {0,  2 },
        {0,  -2}
    };
    float x, y, width, height;
    int   i;

    x      = rect.pos.x / vVirtualScale[0];
    y      = rect.pos.y / vVirtualScale[1];
    width  = rect.size.width / vVirtualScale[0];
    height = rect.size.height / vVirtualScale[1];
    x += (width - font->getWidth(text, -1)) * 0.5f;
    y += height - font->getHeight(text, -1) - 2.0f;

    font->setColor(UBlack);
    for (i = 0; i < 12; i++) {
        font->Print(x + offsets[i][0], y + offsets[i][1], text, -1, vVirtualScale);
    }
    font->setColor(UWhite);
    font->Print(x, y, text, -1, vVirtualScale);
}

/*
====================
UI_DrawClipNumber

Added in OPM
The number of the clip of the demo playing (cl_demoClipNumbers), at the
bottom of the screen, between the health and the ammo and above the
weapons, over all the rest
====================
*/
void UI_DrawClipNumber(void)
{
    static UIFont *font;
    int            number;
    vec2_t         scale;

    number = CL_DemoClipNumber();
    if (!number || clc.demoSeeking) {
        return;
    }

    if (!font) {
        font = new UIFont("facfont-20");
    }

    // the whole screen, as the widgets drawn before leave theirs
    re.Set2DWindow(0, 0, uid.vidWidth, uid.vidHeight, 0, uid.vidWidth, uid.vidHeight, 0, -1, 1);
    re.Scissor(0, 0, uid.vidWidth, uid.vidHeight);
    // twice the size of the HUD's
    scale[0] = uid.scaleRes[0] * 2;
    scale[1] = uid.scaleRes[1] * 2;
    UI_PrintOutlinedCentered(
        font, UIRect2D(0, 0, uid.vidWidth, uid.vidHeight - 44.0f * uid.scaleRes[1]), va("#%d", number), scale
    );
}

/*
====================
UI_ClearMessages

Added in OPM
Empties the HUD's messages, chat and kills (after a demo jumps), with what
it keeps of the time before (the damage's arcs)
====================
*/
void UI_ClearMessages(void)
{
    UI_CompHud_Clear();
}

void UI_ClearState(void)
{
    S_FadeSound(0.0);
    UI_ClearCenterPrint();
    UI_ClearMessages();
}

/*
====================
UI_ShowMouse_f
====================
*/
void UI_ShowMouse_f(void)
{
    if (view3d->IsActive()) {
        uWinMan.DeactivateCurrentControl();
    }
}

/*
====================
UI_HideMouse_f
====================
*/
void UI_HideMouse_f(void)
{
    if (!view3d->IsActive()) {
        uWinMan.ActivateControl(view3d);
        IN_MouseOff();
    }
}

static str loadName;

/*
====================
UI_ShowScoreboard_f
====================
*/
void UI_ShowScoreboard_f(const char *pszMenuName)
{
    if (pszMenuName) {
        if (scoreboard_menuname.length() && str::icmp(scoreboard_menuname, pszMenuName) && scoreboard_menu) {
            scoreboard_menu->ForceHide();
        }

        scoreboard_menuname = pszMenuName;
    }

    if (UI_MenuActive()) {
        if (scoreboard_menuname.length()) {
            scoreboard_menu = menuManager.FindMenu(scoreboard_menuname);

            if (scoreboard_menu) {
                scoreboard_menu->ForceHide();
            }
        }

        scoreboard_up = qfalse;
    } else {
        if (scoreboard_menuname.length()) {
            scoreboard_menu = menuManager.FindMenu(scoreboard_menuname);

            if (scoreboard_menu) {
                UIWidget *widget = scoreboard_menu->GetContainerWidget();
                if (widget) {
                    widget->BringToFrontPropogated();
                }
                scoreboard_menu->ForceShow();
            }
        }

        scoreboard_up = qtrue;
    }
}

/*
====================
UI_ScoreboardShown
====================
*/
qboolean UI_ScoreboardShown(void)
{
    return UI_Menus_Scoreboard(scoreboard_menuname.c_str()) && scoreboard_up && scoreboard_menu ? qtrue : qfalse;
}

/*
====================
UI_HideScoreboard_f
====================
*/
void UI_HideScoreboard_f(void)
{
    scoreboard_up = qfalse;

    if (scoreboard_menuname.length()) {
        // Fixed in 2.30 (scoreboard_menu check)
        if (scoreboard_menu) {
            scoreboard_menu->ForceHide();
        }
    }
}

/*
====================
UI_SetScoreBoardItem
====================
*/
void UI_SetScoreBoardItem(
    int          iItemNumber,
    const char  *pszData1,
    const char  *pszData2,
    const char  *pszData3,
    const char  *pszData4,
    const char  *pszData5,
    const char  *pszData6,
    const char  *pszData7,
    const char  *pszData8,
    const vec4_t pTextColor,
    const vec4_t pBackColor,
    qboolean     bIsHeader
)
{
    // nothing: the stock scoreboard list is gone (kept for the cgame, cgi.UI_SetScoreBoardItem)
}

/*
====================
UI_DeleteScoreBoardItems
====================
*/
void UI_DeleteScoreBoardItems(int iMaxIndex)
{
    // nothing: as UI_SetScoreBoardItem
}

/*
====================
UI_Hud_f
====================
*/
void UI_Hud_f(void)
{
    if (Cmd_Argc() != 2) {
        Com_Printf("Usage: ui_hud [1|0]");
        return;
    }

    ui_hud = atoi(Cmd_Argv(1));

    if (ui_hud) {
        Cvar_Set("cg_hud", "1");
    } else {
        Cvar_Set("cg_hud", "0");
    }
}

/*
====================
UI_ClearConsole_f
====================
*/
void UI_ClearConsole_f(void)
{
    if (fakk_console) {
        fakk_console->Clear();
    }

    if (dm_console) {
        dm_console->Clear();
    }

}

/*
====================
SaveConsoleRectangle
====================
*/
static void SaveConsoleRectangle(void)
{
    bool     wasMinimized;
    UIRect2D r;

    if (!fakk_console) {
        return;
    }

    wasMinimized = fakk_console->IsMinimized();

    if (wasMinimized) {
        fakk_console->ProcessEvent(UIFloatingWindow::W_MinimizePressed);
    }

    if (!fakk_console->IsMinimized()) {
        r = fakk_console->getFrame();

        Cvar_Set(
            "ui_consoleposition", va("%d %d %d %d", (int)r.pos.x, (int)r.pos.y, (int)r.size.width, (int)r.size.height)
        );

        if (wasMinimized) {
            fakk_console->ProcessEvent(UIFloatingWindow::W_MinimizePressed);
        }
    }
}

/*
====================
LoadConsoleRectangle
====================
*/
static void LoadConsoleRectangle(void)
{
    bool wasMinimized;

    if (!fakk_console) {
        return;
    }

    wasMinimized = fakk_console->IsMinimized();

    if (wasMinimized) {
        fakk_console->ProcessEvent(UIFloatingWindow::W_MinimizePressed);
    }

    if (!fakk_console->IsMinimized() && wasMinimized) {
        fakk_console->ProcessEvent(UIFloatingWindow::W_MinimizePressed);
    }
}

/*
====================
CL_ShutdownUI
====================
*/
void CL_ShutdownUI(void)
{
    UI_CompHud_Shutdown();
    if (!cls.uiStarted) {
        return;
    }

    if (s_intermediateconsole) {
        delete s_intermediateconsole;
        s_intermediateconsole = NULL;
    }

    // remove all UI commands
    Cmd_RemoveCommand("pushmenu");
    Cmd_RemoveCommand("forcemenu");
    Cmd_RemoveCommand("popmenu");
    Cmd_RemoveCommand("showmenu");
    Cmd_RemoveCommand("widgetcommand");
    Cmd_RemoveCommand("listmenus");
    Cmd_RemoveCommand("menustate");
    Cmd_RemoveCommand("togglemenu");
    Cmd_RemoveCommand("loadmenu");
    Cmd_RemoveCommand("ui_startdmmap");
    Cmd_RemoveCommand("ui_showmouse");
    Cmd_RemoveCommand("ui_hidemouse");
    Cmd_RemoveCommand("ui_saveconsolepos");
    Cmd_RemoveCommand("ui_loadconsolepos");
    Cmd_RemoveCommand("clear");
    Cmd_RemoveCommand("ui_hud");
    Cmd_RemoveCommand("centerprint");
    Cmd_RemoveCommand("locationprint");
    Cmd_RemoveCommand("finishloadingscreen");
    Cmd_RemoveCommand("ui_getplayermodel");

    ui_pConnectingMenu = menuManager.FindMenu("connecting");

    // delete base hud
    if (view3d) {
        delete view3d;
        view3d = NULL;
    }
    if (fakk_console) {
        delete fakk_console;
        fakk_console = NULL;
    }
    if (dm_console) {
        delete dm_console;
        dm_console = NULL;
    }

    // shutdown the UI
    uie.Shutdown();

    // delete all menus
    menuManager.DeleteAllMenus();

    cls.uiStarted = false;
}

/*
====================
UI_StockMenuGone

Added: a stock menu file nothing shows any more (the Reloaded menus and
the URC HUD are its screens): not loaded. Named one by one, the games'
own (Allied Assault's, Spearhead's and Breakthrough's pk3s): a map's or a
mod's menus load, under these names too (FS_FileInGamePak). What still loads: the load screens (the Reloaded load
screen shows their pictures), the end of a level's (StatsScreen_*,
mission_*: the end screen's) and what the game scripts show (the
briefings, the title cards, credits1, dday1 and 2); none of the developer
tools' menus
====================
*/
static qboolean UI_StockMenuGone(const char *file)
{
    static const char *const gone[] = {
        // the stock HUD's (the URC HUD in its place)
        "hud_ammo_15cmc36", "hud_ammo_aagun", "hud_ammo_abturretmain", "hud_ammo_abturretmini", "hud_ammo_bar",
        "hud_ammo_bazooka", "hud_ammo_beretta", "hud_ammo_bohler", "hud_ammo_bomba", "hud_ammo_bombabreda",
        "hud_ammo_breda", "hud_ammo_carcano", "hud_ammo_colt45", "hud_ammo_delisle", "hud_ammo_empty",
        "hud_ammo_flak88", "hud_ammo_g43", "hud_ammo_garand", "hud_ammo_granatwerfer", "hud_ammo_kar98",
        "hud_ammo_kar98mortar", "hud_ammo_kar98sniper", "hud_ammo_l42a1", "hud_ammo_landmineallies",
        "hud_ammo_landmineaxis", "hud_ammo_lee-enfield", "hud_ammo_m2grenade", "hud_ammo_mg42",
        "hud_ammo_mills_grenade", "hud_ammo_minedetector", "hud_ammo_minedetector_axis", "hud_ammo_modello",
        "hud_ammo_moschetto", "hud_ammo_mosinnagantrifle", "hud_ammo_mp40", "hud_ammo_mp44",
        "hud_ammo_nagantrevolver", "hud_ammo_nebelhandgranate", "hud_ammo_nebelwerfer", "hud_ammo_p38",
        "hud_ammo_panzercannon", "hud_ammo_panzerschreck", "hud_ammo_piat", "hud_ammo_portableturret",
        "hud_ammo_ppsh", "hud_ammo_pzb41", "hud_ammo_rdg-1smokegrenade", "hud_ammo_russian_f1_grenade",
        "hud_ammo_shotgun", "hud_ammo_silencedpistol", "hud_ammo_smokegrenade", "hud_ammo_springfield",
        "hud_ammo_steilhandgranate", "hud_ammo_sten", "hud_ammo_svt", "hud_ammo_t34_dtm", "hud_ammo_t34cannon",
        "hud_ammo_thompson", "hud_ammo_vickers", "hud_ammo_webleyrevolver", "hud_compass", "hud_fraglimit",
        "hud_fuse", "hud_fuse_wet", "hud_health", "hud_items", "hud_score", "hud_stopwatch", "hud_timelimit",
        "hud_weapons",
        // the main menu's pages, Options' and the old Campaign's (the Reloaded menus' screens; Training's end, its
        // briefing room, Campaign's missions)
        "main", "options_main", "advancedoptions", "audio options", "video options", "controls", "calibrate_monitor",
        "loadsave", "loadsave_mp", "medals", "war_records", "briefing_room", "mission1difficulty", "mission2difficulty",
        "mission3difficulty", "mission4difficulty", "mission5difficulty", "mission6difficulty", "missiont1difficulty",
        "mission1_briefing", "mission2_briefing", "mission3_briefing", "mission4_briefing", "mission5_briefing",
        "mission6_briefing",
        // Multiplayer's: the browsers, Host, the map rotation, Instant Action
        "multiplayer", "multiplayeroptions", "multiplayerstart", "multiplayerstart_ffa", "multiplayerstart_lib",
        "multiplayerstart_obj", "multiplayerstart_round", "multiplayerstart_team", "multiplayerstart_tow",
        "joingame", "joininternetgame", "joinlangame", "joinnetgame", "filtermenu", "map_rotation_setup",
        "instant_action_menu",
        // a game's: the Esc menu, the team and weapon menus, the scoreboards, the votes
        "dm_main", "dm_teamselect", "obj_teamselect", "dm_ffamodelselect", "dm_primaryselect",
        "dm_primaryselect_british", "dm_primaryselect_german", "dm_primaryselect_italian",
        "dm_primaryselect_russian", "dm_scoreboard", "dm_round_scoreboard", "obj_scoreboard", "tow_scoreboard",
        "lib_scoreboard", "votemain", "votecast", "votesubclient", "votesubfloat", "votesubinteger", "votesublist",
        "votesubtext",
        // the connect's boxes
        "connecting", "errormessage", "badserveraddy", "disconnected", "serverdisconnected", "servertimeout",
        "serverfull", "wrongversion",
        // screens no game shows: the startup's logo, the demo's help, the update check's, the stock crosshair, briefing
        // slides no briefing uses
        "eascreen", "patch_available", "demohelp", "crosshair", "briefing2c", "briefing2d", "briefing3f", "briefing3g",
        "briefing3h",
        // the developer tools whose button only ran a removed file picker: the animation viewer's (viewspawnlist), the
        // LOD tool's (lod_spawnlist)
        "animate2", "lodtool",
        // the developer tools made of the stock pull-down menus (removed): the decal, emitter and smoke editors, the
        // sound editor (its soundpicker removed too)
        "decals", "emitter", "smoke", "zound",
        // the last developer tools, the only users of the stock field, checkbox, list and list box (removed): the
        // camera tool and its quick menu, the level designers' F1 menu, a test page
        "cinematicmenu", "cinemaquickmenu", "leveldesign", "greytest",
    };
    char name[MAX_QPATH];

    COM_StripExtension(file, name, sizeof(name));
    for (size_t i = 0; i < ARRAY_LEN(gone); i++) {
        if (!Q_stricmp(name, gone[i])) {
            // Fixed: only the games' own file (a mod's or a map's of the same name was skipped too)
            return FS_FileInGamePak(va("ui/%s", file));
        }
    }
    return qfalse;
}

/*
====================
CL_InitUI
====================
*/
#define UI_OLD_API_VERSION 4

void CL_InitializeUI(void)
{
    int    nfiles;
    char **filenames;
    int    i;

    // Register all variables
    ui_consoleposition = Cvar_Get("ui_consoleposition", "", 1);
    // the console key works by default (MoHAA Reloaded's default, upstream's is 0)
    ui_console         = Cvar_Get("ui_console", "1", 1);
    con_scrollback     = Cvar_Get("con_scrollback", va("%d", MAX_CONSOLE_ITEMS), CVAR_ARCHIVE);
    sound_overlay      = Cvar_Get("soundoverlay", "0", 0);
    ui_debugload       = Cvar_Get("ui_debugload", "0", 0);
    Cvar_Get("ui_signshader", "", 0);
    Cvar_Get("ui_compass", "1", 0); // (the Competitive HUD's compass, shown with no radar)
    ui_newvidmode          = Cvar_Get("ui_newvidmode", "-1", 0);
    ui_drawcoords          = Cvar_Get("ui_drawcoords", "0", 0);
    Cvar_Get("ui_health_start", "0", 0);
    Cvar_Get("ui_health_end", "0", 0);
    ui_GunneryEvaluation   = Cvar_Get("ui_GunneryEvaluation", "0", 0);
    ui_GroinShots          = Cvar_Get("ui_GroinShots", "0", 0);
    ui_RightArmShots       = Cvar_Get("ui_RightArmShots", "0", 0);
    ui_LeftArmShots        = Cvar_Get("ui_LeftArmShots", "0", 0);
    ui_RightLegShots       = Cvar_Get("ui_RightLegShots", "0", 0);
    ui_LeftLegShots        = Cvar_Get("ui_LeftLegShots", "0", 0);
    ui_TorsoShots          = Cvar_Get("ui_TorsoShots", "0", 0);
    ui_HeadShots           = Cvar_Get("ui_HeadShots", "0", 0);
    ui_NumEnemysKilled     = Cvar_Get("ui_NumEnemysKilled", "0", 0);
    ui_NumObjectsDestroyed = Cvar_Get("ui_NumObjectsDestroyed", "0", 0);
    ui_NumHitsTaken        = Cvar_Get("ui_NumHitsTaken", "0", 0);
    ui_PreferredWeapon     = Cvar_Get("ui_PreferredWeapon", "none", 0);
    ui_Accuracy            = Cvar_Get("ui_Accuracy", "0", 0);
    ui_NumObjectives       = Cvar_Get("ui_NumObjectives", "0", 0);
    ui_NumComplete         = Cvar_Get("ui_NumComplete", "0", 0);
    ui_NumHits             = Cvar_Get("ui_NumHits", "0", 0);
    ui_NumShotsFired       = Cvar_Get("ui_NumShotsFired", "0", 0);
    UI_CompHud_Init();
    UI_Menus_Init();
    UI_Extras_Init();
    ui_gotmedal            = Cvar_Get("ui_gotmedal", "0", 0);
    ui_success             = Cvar_Get("ui_success", "0", 0);
    ui_failed              = Cvar_Get("ui_failed", "0", 0);
    cl_greenfps            = Cvar_Get("cl_greenfps", "0", 1);

    Cvar_Set("cg_hud", ui_hud ? "1" : "0");

    CL_FillUIDef();
    CL_FillUIImports();
    UI_InitExports();
    // Added: the head icons' pictures (the outlines baked)
    UI_CompHud_HeadIcons();
    uie.Init();

    // New since mohta
    // Version number
    Cvar_Set("game_version", va("v%s", com_target_shortversion->string));

    // Add all commands
    Cmd_AddCommand("pushmenu", UI_PushMenu_f);
    Cmd_AddCommand("pushmenu_sp", UI_PushMenuSP_f);
    Cmd_AddCommand("pushmenu_dm", UI_PushMenuMP_f);
    Cmd_AddCommand("forcemenu", UI_ForceMenu_f);
    Cmd_AddCommand("popmenu", UI_PopMenu_f);
    Cmd_AddCommand("showmenu", UI_ShowMenu_f);
    Cmd_AddCommand("hidemenu", UI_HideMenu_f);
    Cmd_AddCommand("widgetcommand", UI_WidgetCommand_f);
    Cmd_AddCommand("globalwidgetcommand", UI_GlobalWidgetCommand_f);
    Cmd_AddCommand("listmenus", UI_ListMenus_f);
    Cmd_AddCommand("menustate", UI_MenuState_f);
    Cmd_AddCommand("togglemenu", UI_ToggleMenu_f);
    Cmd_AddCommand("loadmenu", UI_LoadMenu_f);
    Cmd_AddCommand("ui_startdmmap", UI_StartDMMap_f);
    Cmd_AddCommand("ui_showmouse", UI_ShowMouse_f);
    Cmd_AddCommand("ui_hidemouse", UI_HideMouse_f);
    Cmd_AddCommand("ui_saveconsolepos", SaveConsoleRectangle);
    Cmd_AddCommand("ui_loadconsolepos", LoadConsoleRectangle);
    Cmd_AddCommand("clear", UI_ClearConsole_f);
    Cmd_AddCommand("ui_hud", UI_Hud_f);
    Cmd_AddCommand("centerprint", UI_CenterPrint_f);
    Cmd_AddCommand("locationprint", UI_LocationPrint_f);
    Cmd_AddCommand("finishloadingscreen", UI_FinishLoadingScreen_f);
    Cmd_AddCommand("ui_getplayermodel", UI_GetPlayerModel_f);

    // Create the 3D view
    view3d = new View3D;
    view3d->setAlwaysOnBottom(true);
    view3d->InitFrame(NULL, 0, 0, uid.vidWidth, uid.vidHeight, -1, "facfont-20");
    view3d->setName("view3d");
    view3d->InitSubtitle();

    // Register the loading material
    ui_static_materials.loading = uWinMan.RegisterShader("textures/menu/loading");

    if (ui_console->integer || developer->integer > 0) {
        // Create the console
        fakk_console = getNewConsole();
    }

    // Create the dm console
    if (!dm_console) {
        dm_console = getNewDMConsole();
    }

    // Load all urc files
    // Added: their faces baked meanwhile on worker threads (registered at the first frame, in the order asked for)
    UI_BakeBatch(true);
    filenames = FS_ListFiles("ui/", "urc", qfalse, &nfiles);

    for (i = 0; i < nfiles; i++) {
        char szFilename[MAX_QPATH];

        if (UI_StockMenuGone(filenames[i])) {
            continue;
        }
        Com_sprintf(szFilename, sizeof(szFilename), "ui/%s", filenames[i]);
        new UILayout(szFilename);
    }

    FS_FreeFileList(filenames);

    uWinMan.CreateMenus();

    // find the connection menu
    ui_pConnectingMenu = menuManager.FindMenu("connecting");

    IN_MouseOn();

    // realign menus
    menuManager.RealignMenus();

    // Added: the Competitive HUD's pictures begun ahead of the first map's load
    UI_CompHud_PreBake(qtrue);

    // clear input
    CL_ClearButtons();

    cls.uiStarted = qtrue;

    // the console opens at startup in developer mode (upstream opened it whenever no intro played)
    if (!com_dedicated->integer && developer->integer) {
        UI_ToggleConsole();
    }
}

//
// Map info load information
//

static constexpr unsigned int MAP_INFO_VERSION = 3;

#if 0
static unsigned int  startCountLow;
static unsigned int  startCountHigh;
static unsigned int  loadCountLow;
static unsigned int  loadCountHigh;
#endif
std::chrono::time_point<std::chrono::steady_clock> startCount;

static unsigned int  loadCount;
static unsigned int  lastTime   = 0;
static unsigned int  updateTime = 0;
static unsigned int  loadNumber;
static unsigned int  totalLoadTime;
static unsigned int  currentLoadTime;

static char        **loadStrings;
static unsigned int *loadTimes;
static char         *loadStringsBuffer;
// Added: the start of the name of the resource loading (UI_LoadProgress)
static char          ui_loadWhat[4];

class LoadResourceInfo
{
public:
    class LoadResourceInfo *next;
    unsigned int            loadCount;
    char                    name[1];

public:
    LoadResourceInfo();
};

static LoadResourceInfo *loadHead;

/*
====================
UI_BeginLoadResource
====================
*/
void UI_BeginLoadResource(void)
{
#if 0
    clock_t time = clock();

    startCountHigh = time >> 32;
    startCountLow  = time;
#endif

    // Fixed in OPM
    //  Use reliable cross-platform CPU clock
    startCount = std::chrono::steady_clock::now();
}

/*
====================
UI_EndLoadResource
====================
*/
void UI_EndLoadResource(void)
{
#if 0
    clock_t time;

    time =
        clock() - (((clock_t)startCountHigh << 32) | startCountLow) + (((clock_t)loadCountHigh << 32) | loadCountLow);

    loadCountHigh = time >> 32;
    loadCountLow  = time;
    loadCount     = time >> 25;
#endif

    // Fixed in OPM
    //  Correctly calculate the load time for a resource
    std::chrono::nanoseconds deltaTime;

    deltaTime = std::chrono::steady_clock::now() - startCount;

    // This will take into account resources that take at least 1 millisecond
    loadCount = std::chrono::duration_cast<std::chrono::milliseconds>(deltaTime).count();
}

/*
====================
UI_EndLoadResource
====================
*/
void UI_EndLoadResource(const char *name)
{
    LoadResourceInfo *newLoadHead;

    if (!loadCount) {
        return;
    }

    newLoadHead            = (LoadResourceInfo *)Z_Malloc(sizeof(LoadResourceInfo) + strlen(name));
    newLoadHead->next      = loadHead;
    newLoadHead->loadCount = loadCount;
    strcpy(newLoadHead->name, name);
    loadHead = newLoadHead;

    loadNumber++;

#if 0
    loadCountLow  = 0;
    loadCountHigh = 0;
#endif
}

/*
====================
UI_IsResourceLoaded
====================
*/
qboolean UI_IsResourceLoaded(const char *name)
{
    switch (*name) {
    case 97:
    case 98:
    case 99:
    case 100:
    case 101:
    case 104:
        return TIKI_FindTikiAnim(name + 1) != NULL;
    case 103:
        return SkeletorCacheFindFilename(name + 1, NULL);
    case 107:
        return S_IsSoundRegistered(name + 1);
    case 110:
        return uii.Rend_ImageExists(name + 1);
    default:
        return qfalse;
    }
}

/*
====================
UI_RegisterLoadResource
====================
*/
void UI_RegisterLoadResource(const char *name)
{
    int i, j;
    int low, high;

    low  = 0;
    high = loadNumber - 1;

    while (low <= high) {
        i = (low + high) / 2;
        j = strcmp(name, loadStrings[i]);

        if (j < 0) {
            high = i - 1;
        } else if (j > 0) {
            low = i + 1;
        } else {
            currentLoadTime += loadTimes[i];
            loadTimes[i] = 0;
            break;
        }
    }
}

/*
====================
UI_TestUpdateScreen
====================
*/
void UI_TestUpdateScreen(unsigned int timeout)
{
    unsigned int newTime = Sys_Milliseconds();
    unsigned int startRenderTime, endRenderTime;

    if (timeout > 0 && (newTime - lastTime) < (timeout + updateTime)) {
        return;
    }

    startRenderTime = Sys_Milliseconds();
    Sys_PumpMessageLoop();
    SCR_UpdateScreen();
    endRenderTime = Sys_Milliseconds();

    updateTime = Q_min(endRenderTime - startRenderTime, 1000);
    lastTime   = endRenderTime;
}

/*
====================
UI_ClearResource
====================
*/
void UI_ClearResource(void)
{
    if (cls.loading == LOAD_PROGRESS_FALSE || cls.loading != LOAD_PROGRESS_SAVE) {
        return;
    }

    UI_BeginLoadResource();
    UI_TestUpdateScreen(0);
}

/*
====================
UI_LoadResource
====================
*/
void UI_LoadResource(const char *name)
{
    if (cls.loading == LOAD_PROGRESS_FALSE) {
        return;
    }
    // Added: what loads (the Reloaded load screen's line)
    Q_strncpyz(ui_loadWhat, name, sizeof(ui_loadWhat));

    if (cls.loading == LOAD_PROGRESS_SAVE) {
        UI_EndLoadResource();
        UI_EndLoadResource(name);
        UI_BeginLoadResource();
    } else if (cls.loading == LOAD_PROGRESS_LOAD) {
        UI_RegisterLoadResource(name);
        Cvar_SetValue("loadingbar", (float)currentLoadTime / (float)totalLoadTime);
    }

    UI_TestUpdateScreen(33);
}

/*
====================
UI_LoadProgress
====================
*/
float UI_LoadProgress(const char **what)
{
    *what = ui_loadWhat;
    switch (cls.loading) {
    case LOAD_PROGRESS_LOAD:
        return totalLoadTime ? (float)currentLoadTime / (float)totalLoadTime : 0.0f;
    case LOAD_PROGRESS_FALSE:
        return 1.0f;
    default:
        return -1.0f;
    }
}

/*
====================
UI_FreeLoadStrings
====================
*/
void UI_FreeLoadStrings()
{
    if (loadStrings) {
        Z_Free(loadStrings);
        loadStrings = NULL;
    }
}

/*
====================
UI_DeleteLoadInfo
====================
*/
void UI_DeleteLoadInfo()
{
    LoadResourceInfo *ptr;
    LoadResourceInfo *nextPtr;

    for (ptr = loadHead; ptr != NULL; ptr = nextPtr) {
        nextPtr = ptr->next;
        Z_Free(ptr);
    }
}

/*
====================
UI_ParseLoadMapinfo
====================
*/
bool UI_ParseLoadMapinfo(Script *scr)
{
    int         size, index;
    int         allocLen;
    char       *pos;
    const char *token;
    char       *newPos;
    int         time;

    loadNumber = scr->GetInteger(false);
    size       = scr->GetInteger(false);
    scr->SkipWhiteSpace(true);

    allocLen          = size + sizeof(loadStrings[0]) * loadNumber + sizeof(loadTimes[0]) * loadNumber;
    loadStrings       = (char **)Z_Malloc(allocLen);
    loadTimes         = (unsigned int *)(loadStrings + loadNumber);
    loadStringsBuffer = (char *)(loadTimes + loadNumber);
    pos               = loadStringsBuffer;

    totalLoadTime = 0;
    index         = 0;

    do {
        token  = scr->GetToken(true);
        newPos = &pos[strlen(token) + 1];
        strcpy(pos, token);
        loadStrings[index] = pos;

        time = scr->GetInteger(true);

        if (UI_IsResourceLoaded(pos)) {
            loadTimes[index] = 0;
        } else {
            loadTimes[index] = time;
            totalLoadTime += time;
        }

        pos = newPos;
        index++;
    } while (index != loadNumber);

    assert(pos <= (char *)loadStrings + allocLen);

    if (newPos != loadStringsBuffer + size) {
        UI_FreeLoadStrings();
        return false;
    }

    return true;
}

/*
====================
UI_ArchiveLoadMapinfo
====================
*/
bool UI_ArchiveLoadMapinfo(const char *mapname)
{
    Script scr;
    int    version;
    byte  *tempbuf;
    int    length;
    bool   success;

    length = FS_ReadFile(loadName, (void **)&tempbuf);

    if (length <= 0) {
        return false;
    }

    scr.LoadFile(loadName, length, (const char *)tempbuf);
    FS_FreeFile(tempbuf);

    version = scr.GetInteger(false);
    scr.SkipWhiteSpace(true);

    if (version == MAP_INFO_VERSION) {
        success = UI_ParseLoadMapinfo(&scr);
    } else {
        Com_Printf("Expecting version %d map info file.  Map info is version %d.\n", MAP_INFO_VERSION, version);
        success = false;
    }

    scr.Close();
    return success;
}

/*
====================
UI_BeginLoad
====================
*/
void UI_BeginLoad(const char *pszMapName)
{
    str mapfile;

    if (cls.loading != LOAD_PROGRESS_FALSE) {
        if (cls.loading == LOAD_PROGRESS_SAVE) {
            UI_DeleteLoadInfo();
        }

        UI_FreeLoadStrings();
        cls.loading = LOAD_PROGRESS_FALSE;
    }

    server_loading         = qtrue;
    server_loading_waiting = qfalse;
    strcpy(server_mapname, pszMapName);

    if (str::icmp(ui_sCurrentLoadingMenu, server_mapname)) {
        ui_sCurrentLoadingMenu = server_mapname;
        ui_pLoadingMenu        = menuManager.FindMenu(ui_sCurrentLoadingMenu);
    }

    if (!ui_pLoadingMenu) {
        ui_sCurrentLoadingMenu = "loading_default";
        ui_pLoadingMenu        = menuManager.FindMenu(ui_sCurrentLoadingMenu);
    }

    // Added: the Reloaded load screen, the stock one's pictures in it (cl_uimenus.cpp)
    {
        const char *ours = UI_Menus_Loading(ui_pLoadingMenu, pszMapName);

        if (ours) {
            ui_sCurrentLoadingMenu = ours;
            ui_pLoadingMenu        = menuManager.FindMenu(ui_sCurrentLoadingMenu);
        }
    }
    ui_loadWhat[0] = 0;

    UI_ForceMenuOff(true);
    UI_DeactiveFloatingWindows();

    if (ui_pLoadingMenu) {
        if (!developer->integer && UI_ConsoleIsVisible()) {
            UI_CloseConsole();
        }

        ui_pLoadingMenu->PassEventToWidget("continuebutton", new Event(EV_Widget_Disable));

        loadName = "maps/";
        loadName += pszMapName;
        mapfile = loadName;
        loadName += ".min";

        if (CL_UseLargeLightmap(pszMapName)) {
            mapfile += ".bsp";
        } else {
            // Added in 2.0
            mapfile += "_sml.bsp";
        }

        if (UI_ArchiveLoadMapinfo(mapfile)) {
            cls.loading = LOAD_PROGRESS_LOAD;
        } else {
            cls.loading = LOAD_PROGRESS_SAVE;
        }

        if (cls.loading == LOAD_PROGRESS_LOAD) {
            ui_pLoadingMenu->PassEventToWidget("loadingflasher", new Event(EV_Widget_Disable));
            ui_pLoadingMenu->PassEventToWidget("loadingbar", new Event(EV_Widget_Enable));
            ui_pLoadingMenu->PassEventToWidget("loadingbar_border", new Event(EV_Widget_Enable));

            currentLoadTime = 0;
        } else {
            ui_pLoadingMenu->PassEventToWidget("loadingflasher", new Event(EV_Widget_Enable));
            ui_pLoadingMenu->PassEventToWidget("loadingbar", new Event(EV_Widget_Disable));
            ui_pLoadingMenu->PassEventToWidget("loadingbar_border", new Event(EV_Widget_Disable));

            UI_BeginLoadResource();
#if 0
            loadCountLow  = 0;
            loadCountHigh = 0;
#endif
            loadHead   = NULL;
            loadNumber = 0;
        }

        Cvar_SetValue("loadingbar", 0);

        L_ProcessPendingEvents();

        lastTime = Sys_Milliseconds();
    } else {
        cls.loading = LOAD_PROGRESS_DEFAULT;

        // process all events
        L_ProcessPendingEvents();

        lastTime = Sys_Milliseconds();
    }

    if (com_sv_running->integer) {
        SCR_UpdateScreen();
    }
}

/*
====================
UI_CompareLoadResources
====================
*/
int UI_CompareLoadResources(const void *elem1, const void *elem2)
{
    LoadResourceInfo *e1 = *(LoadResourceInfo **)elem1;
    LoadResourceInfo *e2 = *(LoadResourceInfo **)elem2;

    return strcmp(e1->name, e2->name);
}

/*
====================
UI_EndLoad
====================
*/
void UI_EndLoad(void)
{
    LoadResourceInfo  *ptr;
    LoadResourceInfo **base;
    int                i;
    fileHandle_t       file;
    int                size;
    char               buf[1024];

    if (cls.loading == LOAD_PROGRESS_FALSE) {
        return;
    }

    UI_LoadResource("*end");

    switch(cls.loading)
    {
    case LOAD_PROGRESS_SAVE:
        // Fixed in OPM
        //  Don't save progress if there are no resources.
        //  NOTE:
        //   The original game can crash when there is no load number, because it attempts to free a NULL pointer.
        //   If all resources load very fast (less than 1 millisecond), no load number is generated.
        //   This can occur on high-end systems with very fast NVMe SSDs.
        //   This means custom maps without a .min file can crash the original game.
        if (loadNumber) {
            base = (LoadResourceInfo **)Z_Malloc(loadNumber * sizeof(LoadResourceInfo *));

            i = 0;
            for (ptr = loadHead; ptr != NULL; ptr = ptr->next) {
                base[i] = ptr;
                i++;
            }

            qsort(base, loadNumber, sizeof(LoadResourceInfo *), UI_CompareLoadResources);

            //
            // Save the total length of all names
            //
            size = 0;
            for (i = 0; i < loadNumber; i++) {
                ptr = base[i];
                size += strlen(ptr->name) + 1;
            }

            file = FS_FOpenFileWrite_HomeData(loadName);
            Com_sprintf(buf, sizeof(buf), "%d\n%d %d\n", MAP_INFO_VERSION, loadNumber, size);
            FS_Write(buf, strlen(buf), file);

            //
            // Save individual resources (name and time)
            //
            for (i = 0; i < loadNumber; i++) {
                Com_sprintf(buf, sizeof(buf), "%s\n%d\n", base[i]->name, base[i]->loadCount);
                FS_Write(buf, strlen(buf), file);
            }

            FS_FCloseFile(file);
            Z_Free(base);
        }
        UI_DeleteLoadInfo();
        break;
    case LOAD_PROGRESS_LOAD:
        if (ui_debugload->integer) {
            Com_Printf("Following resources not accounted for on load:\n");
        }

        for (i = 0; i < loadNumber; i++) {
            if (loadTimes[i]) {
                if (ui_debugload->integer) {
                    Com_Printf("%s\n", loadStrings[i]);
                }

                loadTimes[i] = 0;
            }
        }

        currentLoadTime = totalLoadTime;
        break;
    }

    UI_FreeLoadStrings();

    cls.loading = LOAD_PROGRESS_FALSE;
}

/*
====================
CL_FontStringWidth
====================
*/
int CL_FontStringWidth(fontheader_t *pFont, const char *pszString, int iMaxLen)
{
    return uie.FontStringWidth(pFont, pszString, iMaxLen);
}
