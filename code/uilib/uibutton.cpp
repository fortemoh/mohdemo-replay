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

#include "ui_local.h"
#include "../qcommon/localization.h"

Event W_Button_Pressed
(
    "button_pressed",
    EV_DEFAULT,
    NULL,
    NULL,
    "Signal that the button was pressed"
);

CLASS_DECLARATION(UIWidget, UIButtonBase, NULL) {
    {&W_LeftMouseDown,           &UIButtonBase::Pressed              },
    {&W_LeftMouseUp,             &UIButtonBase::Released             },
    {&W_LeftMouseDragged,        &UIButtonBase::Dragged              },
    {&W_MouseEntered,            &UIButtonBase::MouseEntered         },
    {&W_MouseExited,             &UIButtonBase::MouseExited          },
    {NULL,                       NULL                                }
};

UIButtonBase::UIButtonBase()
{
    m_mouseState = M_NONE;
    AllowActivate(true);
}

void UIButtonBase::Pressed(Event *ev)
{
    m_mouseState           = M_DRAGGING;
    m_hovermaterial_active = qtrue;
    uWinMan.setFirstResponder(this);
}

void UIButtonBase::Released(Event *ev)
{
    if (m_mouseState != M_DRAGGING) {
        m_mouseState = M_NONE;
        return;
    }

    if (uWinMan.getFirstResponder() == this) {
        // make sure to not respond multiple times
        uWinMan.setFirstResponder(NULL);
    }

    m_mouseState = M_NONE;

    if (ev) {
        if (!getClientFrame().contains(MouseEventToClientPoint(ev))) {
            // not in the frame anymore
            m_hovermaterial_active = false;
            return;
        }
    }

    Action();

    if (ev) {
        if (getClientFrame().contains(MouseEventToClientPoint(ev))) {
            // handle the button click
            SendSignal(W_Button_Pressed);
        }
    }
}

void UIButtonBase::MouseEntered(Event *ev)
{
    m_hovermaterial_active = qtrue;
    uWinMan.ActivateControl(this);
}

void UIButtonBase::MouseExited(Event *ev)
{
    SetHovermaterialActive(false);
}

void UIButtonBase::Dragged(Event *ev)
{
    SetHovermaterialActive(getClientFrame().contains(MouseEventToClientPoint(ev)));
}

void UIButtonBase::Action(void)
{
    if (m_clicksound.length()) {
        uii.Snd_PlaySound(m_clicksound.c_str());
    }

    if (m_command.length()) {
        str stuff = m_command + "\n";
        uii.Cmd_Stuff(stuff.c_str());
    }
}

CLASS_DECLARATION(UIButtonBase, UIButton, NULL) {
    {NULL, NULL}
};

UIButton::UIButton()
{
    setBorderStyle(border_none);
}

void UIButton::Draw(void)
{
    if (m_mouseState == M_DRAGGING && m_screenframe.contains(uid.mouseX, uid.mouseY)) {
        DrawPressed();
    } else {
        DrawUnpressed();
    }
}

void UIButton::DrawPressed(void)
{
    UIRect2D clientFrame = getClientFrame();

    if (m_borderStyle != border_none) {
        Draw3DBox(clientFrame, true, m_border_color, m_local_alpha);
    }

    m_font->setColor(m_foreground_color);

    clientFrame.pos.x += 1;
    clientFrame.pos.y += 1;

    m_font->PrintJustified(
        clientFrame,
        m_iFontAlignmentHorizontal,
        m_iFontAlignmentVertical,
        Sys_LV_CL_ConvertString(m_title.c_str()),
        getVirtualScale()
    );
}

void UIButton::DrawUnpressed(void)
{
    m_font->setColor(m_foreground_color);
    m_font->setAlpha(m_local_alpha);

    UIRect2D clientFrame = getClientFrame();

    m_font->PrintJustified(
        clientFrame,
        m_iFontAlignmentHorizontal,
        m_iFontAlignmentVertical,
        Sys_LV_CL_ConvertString(m_title.c_str()),
        getVirtualScale()
    );
}

qboolean UIButton::KeyEvent(int key, unsigned int time)
{
    if (key != K_ENTER && key != K_KP_ENTER) {
        return false;
    }

    UIButtonBase::Action();
    return true;
}
