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

class UILabel : public UIWidget
{
    str                     label;
    str                     m_sOneShotSoundName;
    str                     m_sLoopingSoundName;
    qboolean                m_bOneShotTriggered;
    float                   m_fOneShotSoundTime;
    int                     m_iNextOneShotTime;
    float                   m_fLoopingSoundStartVolume;
    float                   m_fLoopingSoundHoldStartTime;
    float                   m_fLoopingSoundEndVolume;
    float                   m_fLoopingSoundRampTime;
    int                     m_iNextStateTime;
    int                     m_iSoundState;
    int                     m_iLastEnterTime;
    int                     m_iLastDrawTime;
    const char *LinkedText(void);

public:
    const char *CurrentText(void);
    UIRect2D    ContentBox(void) override;

protected:
    // Added: the linked cvar found once, its text localized once a change
    cvar_t                 *m_linkVar;
    int                     m_linkCount;
    str                     m_linkText;

public:
    CLASS_PROTOTYPE(UILabel);

private:
    void MouseEntered(Event *ev);
    void MouseExited(Event *ev);
    // Added in 2.0
    //====
    void OneShotSound(Event *ev);
    void LoopingSound(Event *ev);
    //====
    void FrameInitialized() override; // Added in 2.0
    void ProcessSounds();             // Added in 2.0

public:
    UILabel();

    void Draw(void) override;
};
