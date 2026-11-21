/*
===========================================================================
Copyright (C) 2015 the OpenMoHAA team

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

class UIWindowManager : public UIWidget
{
    unsigned int                  m_lastbuttons;
    UIPoint2D                     m_oldpos;
    SafePtr<UIWidget>             m_oldview;
    SafePtr<UIWidget>             m_firstResponder;
    class UIWidget               *m_activeControl;
    class UIReggedMaterial       *m_cursor;
    str                           m_cursorname;
    bool                          m_showcursor;
    // Added: the picture the system's cursor was last set from
    // (ui_cursorPicture's, NULL the stock one), its size
    const byte                   *m_cursorPic;
    int                           m_cursorW, m_cursorH;
    Container<UIReggedMaterial *> m_materiallist;
    bool                          m_amidead;
    class UIWidget               *m_bindactive;

public:
    CLASS_PROTOTYPE(UIWindowManager);

private:
    void      ViewEvent(UIWidget *view, Event& event, UIPoint2D& pos, int buttons);
    UIWidget *getResponder(UIPoint2D& pos);

public:
    UIWindowManager();
    ~UIWindowManager();

    void              Init(const UIRect2D& frame, const char *fontname);
    void              UpdateViews(void);
    void              ServiceEvents(void);
    void              setFirstResponder(UIWidget *responder);
    UIWidget         *getFirstResponder(void);
    UIWidget         *ActiveControl(void);
    // Added: what a left click at pos goes to (ServiceEvents': the key capture's widget, else the one under it)
    UIWidget         *ClickTarget(UIPoint2D& pos);
    void              ActivateControl(UIWidget *control);
    void              DeactivateCurrentControl(void);
    void              setCursor(const char *string);
    void              showCursor(bool show);
    virtual void      CharEvent(int ch) override;
    virtual qboolean  KeyEvent(int key, unsigned int time) override;
    void              CreateMenus(void);
    UIReggedMaterial *RegisterShader(const str& name);
    void              CleanupShadersFromList(void);
    void              RefreshShadersFromList(void);
    void              DeactivateCurrentSmart(void);
    bool              IsDead(void);
    UIWidget         *BindActive(void);
    void              SetBindActive(UIWidget *w);
    virtual void      Shutdown(void) override;
    void              DeactiveFloatingWindows(void);

private:
    // Added in OPM
    void refreshCursor();
};

// Added: the cursor's picture in place of the stock one (the Reloaded UI's,
// cl_uimenus.cpp): RGBA texels w x h, the hot spot their top-left, and their
// 2D shader (shader NULL: the texels alone, nothing registered). NULL: the
// stock picture
extern const byte *(*ui_cursorPicture)(int *w, int *h, uihandle_t *shader);

extern Event W_MouseExited;
extern Event W_MouseEntered;
extern Event W_LeftMouseDragged;
extern Event W_RightMouseDragged;
extern Event W_CenterMouseDragged;
extern Event W_MouseMoved;
extern Event W_LeftMouseDown;
extern Event W_LeftMouseUp;
extern Event W_RightMouseDown;
extern Event W_RightMouseUp;
extern Event W_CenterMouseDown;
extern Event W_CenterMouseUp;
extern Event W_AnyMouseDown;
extern Event W_AnyMouseUp;

extern UIWindowManager uWinMan;
