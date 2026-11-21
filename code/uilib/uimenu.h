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

#pragma once

#include "../corepp/listener.h"
#include "../corepp/stack.h"

class Menu : public Listener
{
    qboolean m_fullscreen;

public:
    CLASS_PROTOTYPE(Menu);

public:
    Container<UIWidget *> m_itemlist;
    str                   m_name;

public:
    Menu();
    Menu(str name);

    void      AddMenuItem(UIWidget *item);
    void      setName(str name);
    void      ShowMenu(Event *ev);
    void      HideMenu(Event *ev);
    void      ForceShow(void);
    void      ForceHide(void);
    UIWidget *GetContainerWidget(void);
    UIWidget *GetNamedWidget(const char *pszName);
    void      RealignWidgets(void);
    float     GetMaxMotionTime(void);
    void      ActivateMenu(void);
    qboolean  isFullscreen(void);
    void      setFullscreen(qboolean bFullScreen);
    qboolean  isVisible(void);
    void      SaveCVars(void);
    void      RestoreCVars(void);
    void      PassEventToWidget(str name, Event *ev);
};

class MenuManager : public Listener
{
    Container<Menu *> m_menulist;
    Stack<Menu *>     m_showmenustack;
    Stack<Menu *>     m_menustack;
    bool              m_lock;
    // FindMenu's cache: the menus found by name, dropped when the list changes
    struct {
        Menu *menu;
        int   generation;
    } m_found[64];
    int m_generation;

public:
    CLASS_PROTOTYPE(MenuManager);

public:
    MenuManager();

    // Added: the menu of this name, not another shown in its place (ui_menuAlias)
    Menu *FindMenuNamed(str name);

    void  RealignMenus(void);
    void  AddMenu(Menu *m);
    void  DeleteAllMenus(void);
    Menu *FindMenu(str name);
    int   Generation(void) const { return m_generation; } // changes when the list does
    // Added: replace, the menu on top taken off the stack, this one in its place (one menu opening another in one step,
    // no frame without a menu between them)
    bool  PushMenu(str name, bool replace = false);
    bool  ShowMenu(str name);
    void  PushMenu(Event *ev);
    void  PopMenu(qboolean restore_cvars);
    Menu *CurrentMenu(void);
    bool  ClearMenus(bool force);
    void  ListMenus(void);
    void  Lock(Event *ev);
    void  Unlock(Event *ev);
    void  PassEventToWidget(str name, Event *ev);
    bool  ForceMenu(str name);
    // Added: the console's menustate (cl_ui.cpp): the shown and the pushed menus (the top first), the visible ones,
    // the lock; and the menu whose widgets hold w (NULL none)
    str   DescribeState(void);
    Menu *MenuOf(UIWidget *w);
};

extern MenuManager menuManager;

extern Event EV_PushMenu;
extern Event EV_LockMenus;
extern Event EV_UnlockMenus;
extern Event EV_ShowMenu;
extern Event EV_HideMenu;
