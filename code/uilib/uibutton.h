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

extern Event W_Button_Pressed;

class UIButtonBase : public UIWidget
{
protected:
    mouseState_t m_mouseState;

public:
    CLASS_PROTOTYPE(UIButtonBase);

protected:
    void Pressed(Event *ev);
    void Released(Event *ev);
    void MouseEntered(Event *ev);
    void MouseExited(Event *ev);
    void Dragged(Event *ev);

public:
    UIButtonBase();

    void Action(void);
};

class UIButton : public UIButtonBase
{
public:
    CLASS_PROTOTYPE(UIButton);

private:
    void         Draw(void) override;
    virtual void DrawPressed(void);
    virtual void DrawUnpressed(void);

public:
    UIButton();

    qboolean KeyEvent(int key, unsigned int time) override;
};
