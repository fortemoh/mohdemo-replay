/*
===========================================================================
Copyright (C) 2015-2024 the OpenMoHAA team

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

#include "ui_local.h"

Event W_MouseExited
(
    "mouse_exited",
    EV_DEFAULT,
    "ffi",
    "xpos ypos buttons",
    "Mouse exiting a widget event"
);
Event W_MouseEntered
(
    "mouse_entered",
    EV_DEFAULT,
    "ffi",
    "xpos ypos buttons",
    "Mouse Entered a widget event"
);
Event W_LeftMouseDragged
(
    "left_mousedragged",
    EV_DEFAULT,
    "ffi",
    "xpos ypos buttons",
    "Mouse was moved in a widget with the left button down"
);
Event W_RightMouseDragged
(
    "right_mousedragged",
    EV_DEFAULT,
    "ffi",
    "xpos ypos buttons",
    "Mouse was moved in a widget with the right button down"
);
Event W_CenterMouseDragged
(
    "center_mousedragged",
    EV_DEFAULT,
    "ffi",
    "xpos ypos buttons",
    "Mouse was moved in a widget with the center button down"
);
Event W_MouseMoved
(
    "mouse_moved",
    EV_DEFAULT,
    "ffi",
    "xpos ypos buttons",
    "Mouse was moved in a widget with no buttons down"
);
Event W_LeftMouseDown
(
    "left_mouse_down",
    EV_DEFAULT,
    "ffi",
    "xpos ypos buttons",
    "Left mouse button has been pressed down"
);
Event W_LeftMouseUp
(
    "left_mouse_up",
    EV_DEFAULT,
    "ffi",
    "xpos ypos buttons",
    "Left mouse button has been released"
);
Event W_RightMouseDown
(
    "right_mouse_down",
    EV_DEFAULT,
    "ffi",
    "xpos ypos buttons",
    "Right mouse button has been pressed down"
);
Event W_RightMouseUp
(
    "right_mouse_up",
    EV_DEFAULT,
    "ffi",
    "xpos ypos buttons",
    "Right mouse button has been released"
);
Event W_CenterMouseDown
(
    "center_mouse_down",
    EV_DEFAULT,
    "ffi",
    "xpos ypos buttons",
    "Center mouse button has been pressed down"
);
Event W_CenterMouseUp
(
    "center_mouse_up",
    EV_DEFAULT,
    "ffi",
    "xpos ypos buttons",
    "Center mouse button has been released"
);
Event W_AnyMouseDown
(
    "any_mouse_down",
    EV_DEFAULT,
    "ffi",
    "xpos ypos buttons",
    "Any mouse button has been pressed"
);
Event W_AnyMouseUp
(
    "any_mouse_up",
    EV_DEFAULT,
    "ffi",
    "xpos ypos buttons",
    "Any mouse button has been released"
);

UIWindowManager uWinMan;

CLASS_DECLARATION(UIWidget, UIWindowManager, NULL) {
    {NULL, NULL}
};

const byte *(*ui_cursorPicture)(int *w, int *h, uihandle_t *shader);

UIWindowManager::UIWindowManager()
{
    m_amidead          = false;
    m_lastbuttons      = 0;
    m_oldview          = NULL;
    m_firstResponder   = NULL;
    m_cursor           = NULL;
    m_showcursor       = true;
    m_cursorPic        = NULL;
    m_cursorW          = 0;
    m_cursorH          = 0;
    m_font             = NULL;
    m_bindactive       = NULL;
    setBorderStyle(border_none);
}

UIWindowManager::~UIWindowManager()
{
    m_amidead = true;
}

void UIWindowManager::ViewEvent(UIWidget *view, Event& event, UIPoint2D& pos, int buttons)
{
    if (!view) {
        // use the first responder instead
        view = m_firstResponder;
        if (!view) {
            return;
        }
    }

    if (!view->isEnabled()) {
        return;
    }

    Event *ev = new Event(event);
    ev->AddFloat(pos.x);
    ev->AddFloat(pos.y);
    ev->AddInteger(buttons);
    // send the event to the view
    view->ProcessEvent(ev);
}

UIWidget *UIWindowManager::getResponder(UIPoint2D& pos)
{
    UIWidget *subview;
    UIWidget *responder;
    int       i;

    if (!m_firstResponder) {
        m_firstResponder = this;
    }

    responder = m_firstResponder;

    if (responder != this &&
        // Fixed in OPM
        //  Ignore the first responder if it's disabled
        //  this prevents the UI system from being blocked
        //  trying to activate the control
        responder->CanActivate() && responder->isEnabled()) {
        return responder;
    }

    for (i = m_children.NumObjects(); i > 0; i--) {
        subview   = m_children.ObjectAt(i);
        responder = subview->FindResponder(pos);
        if (responder) {
            return responder;
        }
    }

    return this;
}

void UIWindowManager::Init(const UIRect2D& frame, const char *fontname)
{
    setFrame(frame);
    m_font = new UIFont(fontname);
}

void UIWindowManager::UpdateViews(void)
{
    int x, y;

    Display(m_frame, 1.0);

    if (m_cursor && m_showcursor && uid.uiHasMouse) {
        vec4_t     col;
        int        w = 0, h = 0;
        uihandle_t shader = 0;
        // Added: another picture (ui_cursorPicture), the system's cursor set from it when it changes
        const byte *pic = ui_cursorPicture ? ui_cursorPicture(&w, &h, &shader) : NULL;

        if (pic != m_cursorPic || w != m_cursorW || h != m_cursorH) {
            if (pic) {
                IN_SetCursorFromImage(pic, w, h, NULL);
                m_cursorPic = pic;
                m_cursorW   = w;
                m_cursorH   = h;
            } else {
                refreshCursor();
            }
        }

        set2D();

        // Added in OPM
        //  Draw the mouse if it has been grabbed
        if (IN_IsCursorActive()) {
            VectorSet4(col, 1, 1, 1, 1);
            uii.Rend_SetColor(col);

            if (pic) {
                uii.Rend_DrawPicStretched(uid.mouseX, uid.mouseY, w, h, 0, 0, 1, 1, shader);
            } else {
                uii.Rend_DrawPicStretched(uid.mouseX, uid.mouseY, 0, 0, 0, 0, 1, 1, m_cursor->GetMaterial());
            }
        }

        m_font->setColor(UWhite);

        if (UI_GetCvarInt("ui_drawcoords", 0)) {
            x = uid.mouseX + 10;
            y = uid.mouseY - 10;
            m_font->Print(x, y, va("%d:%d", uid.mouseX, uid.mouseY), -1, m_bVirtual ? m_vVirtualScale : NULL);
        }
    }
}

void UIWindowManager::ServiceEvents(void)
{
    UIPoint2D         pos;
    unsigned int      buttons;
    SafePtr<UIWidget> view;

    pos.x   = uid.mouseX;
    pos.y   = uid.mouseY;
    buttons = uid.mouseFlags;

    if (m_bindactive) {
        view = m_bindactive;
    } else {
        view = getResponder(pos);
    }

    SafePtr<UIWidget> b = m_oldview;

    if (b != view) {
        ViewEvent(m_oldview, W_MouseExited, pos, buttons);
        ViewEvent(view, W_MouseEntered, pos, buttons);
    }

    if (m_oldpos != pos) {
        if ((buttons & 1) && (m_lastbuttons & 1)) {
            ViewEvent(view, W_LeftMouseDragged, pos, buttons);
        }
        if ((buttons & 2) && (m_lastbuttons & 2)) {
            ViewEvent(view, W_RightMouseDragged, pos, buttons);
        }
        if ((buttons & 4) && (m_lastbuttons & 4)) {
            ViewEvent(view, W_CenterMouseDragged, pos, buttons);
        }
        if (!buttons) {
            ViewEvent(view, W_MouseMoved, pos, 0);
        }
    }

    if ((buttons & 1) != (m_lastbuttons & 1)) {
        if (buttons & 1) {
            uii.UI_WantsKeyboard();
            ActivateControl(view);
            ViewEvent(view, W_LeftMouseDown, pos, buttons);
        } else {
            ViewEvent(view, W_LeftMouseUp, pos, buttons);
        }
    }
    if ((buttons & 2) != (m_lastbuttons & 2)) {
        if (buttons & 2) {
            ViewEvent(view, W_RightMouseDown, pos, buttons);
        } else {
            ViewEvent(view, W_RightMouseUp, pos, buttons);
        }
    }
    if ((buttons & 4) != (m_lastbuttons & 4)) {
        if (buttons & 4) {
            ViewEvent(view, W_CenterMouseDown, pos, buttons);
        } else {
            ViewEvent(view, W_CenterMouseUp, pos, buttons);
        }
    }
    if (buttons != m_lastbuttons) {
        ViewEvent(view, W_AnyMouseDown, pos, buttons);
        ViewEvent(view, W_AnyMouseUp, pos, buttons);
    }

    m_lastbuttons = buttons;
    m_oldpos      = pos;
    m_oldview     = view;

    if (!m_cursor) {
        // initialize the cursor
        setCursor("gfx/2d/mouse_cursor.tga");
    }
}

void UIWindowManager::setFirstResponder(UIWidget *responder)
{
    if (!responder) {
        m_firstResponder = this;
    } else {
        m_firstResponder = responder;
    }
}

UIWidget *UIWindowManager::getFirstResponder(void)
{
    if (m_firstResponder != this) {
        return m_firstResponder;
    }
    return NULL;
}

UIWidget *UIWindowManager::ActiveControl(void)
{
    return m_activeControl;
}

UIWidget *UIWindowManager::ClickTarget(UIPoint2D& pos)
{
    return m_bindactive ? m_bindactive : getResponder(pos);
}

void UIWindowManager::ActivateControl(UIWidget *control)
{
    UIWidget *wid;

    for (wid = control; wid && wid != this; wid = wid->getParent()) {
        if (wid->CanActivate() && wid->isEnabled()) {
            break;
        }
    }

    if (!wid || wid == this) {
        return;
    }

    if (wid->CanActivate() && wid->isEnabled()) {
        UIWidget *active = ActiveControl();

        if (wid != active) {
            if (active) {
                m_activeControl = NULL;
                active->SendSignal(W_Deactivated);
                active->SetHovermaterialActive(false);
            }

            if (!active || !ActiveControl()) {
                m_activeControl = wid;
                wid->SetHovermaterialActive(true);
                wid->ActivateOrder();
                wid->BringToFrontPropogated();
                wid->SendSignal(W_Activated);
            }
        }
    }
}

void UIWindowManager::DeactivateCurrentControl(void)
{
    UIWidget *wid = m_activeControl;

    if (wid) {
        m_activeControl = NULL;
        wid->SendSignal(W_Deactivated);
    }
}

void UIWindowManager::setCursor(const char *string)
{
    m_cursor = RegisterShader(string);

    if (m_cursor) {
        m_cursorname = string;
    } else {
        uii.Sys_Printf("Could not register shader '%s'\n", string);
    }

    // Added in OPM
    refreshCursor();
}

void UIWindowManager::showCursor(bool show)
{
    m_showcursor = show;
}

void UIWindowManager::CharEvent(int ch)
{
    if (m_activeControl) {
        m_activeControl->CharEvent(ch);
    }
}

qboolean UIWindowManager::KeyEvent(int key, unsigned int time)
{
    // Changed: Ctrl+Tab no longer moves the keys to the next top-level window (the console, the chat, a menu: often a
    // hidden one, and the chat's line was lost), nor Up and Down a control doesn't take to a menu's next or previous
    // widget by ordernumber (no menu the games load has two such widgets; with none, the menu's first widget was
    // activated and brought to the front: the Reloaded main menu's photo then hid the profile's figures)
    if (m_bindactive) {
        m_bindactive->KeyEvent(key, time);
    } else if (m_activeControl) {
        m_activeControl->KeyEvent(key, time);
    }

    return true;
}

void UIWindowManager::CreateMenus(void)
{
    int i;
    int n;

    menuManager.DeleteAllMenus();

    n = uWinMan.m_children.NumObjects();
    for (i = 1; i <= n; i++) {
        UIWidget *w = uWinMan.m_children.ObjectAt(i);
        if (w->isSubclassOf(UIWidgetContainer)) {
            UIWidgetContainer *wid = (UIWidgetContainer *)w;
            Menu              *m   = new Menu(w->getName());

            m->setFullscreen(wid->isFullscreen());
            m->AddMenuItem(w);
            m->ForceHide();
        }
    }
}

UIReggedMaterial *UIWindowManager::RegisterShader(const str& name)
{
    int               i;
    UIReggedMaterial *regged;

    for (i = 1; i <= m_materiallist.NumObjects(); i++) {
        regged = m_materiallist.ObjectAt(i);
        if (regged->GetName() == name) {
            return regged;
        }
    }

    regged = new UIReggedMaterial;
    regged->SetMaterial(name);

    m_materiallist.AddObject(regged);
    return regged;
}

void UIWindowManager::CleanupShadersFromList(void)
{
    int i;
    int num;

    num = m_materiallist.NumObjects();
    for (i = 1; i <= num; i++) {
        UIReggedMaterial *regged = m_materiallist.ObjectAt(i);
        regged->CleanupMaterial();
    }
}

void UIWindowManager::RefreshShadersFromList(void)
{
    int i;
    int num;

    num = m_materiallist.NumObjects();
    for (i = 1; i <= num; i++) {
        UIReggedMaterial *regged = m_materiallist.ObjectAt(i);
        regged->ReregisterMaterial();
    }

    refreshCursor();
}

void UIWindowManager::DeactivateCurrentSmart(void)
{
    UIWidget *toset;
    UIWidget *checking;
    UIWidget *sibling;

    toset = NULL;

    if (!m_activeControl) {
        return;
    }

    for (checking = m_activeControl; checking; checking = checking->getParent()) {
        if (!checking->getParent() || (checking->getParent() && checking->getParent()->IsDying())) {
            continue;
        }

        for (sibling = checking->getPrevSibling(); sibling && !toset; sibling = sibling->getPrevSibling()) {
            if (sibling->CanActivate()) {
                toset = sibling;
            }
        }

        if (toset) {
            break;
        }

        for (sibling = checking->getLastSibling(); sibling && sibling != checking && !toset;
             sibling = sibling->getPrevSibling()) {
            if (sibling->CanActivate()) {
                toset = sibling;
            }
        }

        if (toset) {
            break;
        }
    }

    if (!toset) {
        uWinMan.DeactivateCurrentControl();
        return;
    }

    if (toset == &uWinMan) {
        Menu     *menu;
        UIWidget *newWid = NULL;

        menu = menuManager.CurrentMenu();

        if (menu) {
            newWid = menu->GetContainerWidget();
        }

        if (newWid) {
            toset = newWid;
        }
    }

    uWinMan.ActivateControl(toset);
}

bool UIWindowManager::IsDead(void)
{
    return m_amidead;
}

UIWidget *UIWindowManager::BindActive(void)
{
    return m_bindactive;
}

void UIWindowManager::SetBindActive(UIWidget *w)
{
    m_bindactive = w;
}

void UIWindowManager::Shutdown(void)
{
    int i;

    for (i = m_materiallist.NumObjects(); i > 0; i--) {
        UIReggedMaterial *mat = m_materiallist.ObjectAt(i);
        m_materiallist.RemoveObjectAt(i);

        delete mat;
    }

    m_cursor = NULL;
    IN_FreeCursor();

    UIWidget::Shutdown();
}

void UIWindowManager::DeactiveFloatingWindows(void)
{
    int       i;
    UIWidget *pWidg;

    for (i = m_children.NumObjects(); i > 0; i--) {
        pWidg = m_children.ObjectAt(i);

        if (pWidg->isSubclassOf(UIFloatingWindow)) {
            pWidg->SendSignal(W_Deactivated);
        }
    }
}

void UIWindowManager::refreshCursor()
{
    byte       *pic;
    int         width, height;
    // Added: another picture (ui_cursorPicture) is the system's cursor at once,
    // not the stock one until a menu draws (a vid_restart, Alt+Enter, a map's
    // load: the stock cursor showed over a window); its texels alone (no
    // shader: nothing registered with the renderer here)
    const byte *other = ui_cursorPicture ? ui_cursorPicture(&width, &height, NULL) : NULL;

    if (other) {
        IN_SetCursorFromImage(other, width, height, NULL);
        m_cursorPic = other;
        m_cursorW   = width;
        m_cursorH   = height;
        return;
    }

    // Added: the stock picture's (UpdateViews sets another again)
    m_cursorPic = NULL;
    m_cursorW = m_cursorH = 0;

    // Added in OPM
    //  Load cursor image and use it
    if (!m_cursorname.length()) {
        return;
    }

    if (uii.Rend_LoadRawImage(m_cursorname.c_str(), &pic, &width, &height)) {
        IN_SetCursorFromImage(pic, width, height, uii.Rend_FreeRawImage);
    }
}
