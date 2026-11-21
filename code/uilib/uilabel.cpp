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

// Added in 2.0
//====
Event EV_UILabel_OneShotSound
(
    "oneshotsound",
    EV_DEFAULT,
    "sf",
    "soundName soundTime",
    "Set the sound to play"
);
Event EV_UILabel_LoopingSound
(
    "loopingsound",
    EV_DEFAULT,
    "sffff",
    "soundName start_volume hold_start_time ending_volume ramp_time",
    "Set the sound to play"

);
//====

CLASS_DECLARATION(UIWidget, UILabel, NULL) {
    {&W_MouseEntered,              &UILabel::MouseEntered         },
    {&W_MouseExited,               &UILabel::MouseExited          },
    {&EV_UILabel_OneShotSound,     &UILabel::OneShotSound         },
    {&EV_UILabel_LoopingSound,     &UILabel::LoopingSound         },
    {NULL,                         NULL                           }
};

UILabel::UILabel()
{
    AllowActivate(false);
    m_bOneShotTriggered = false;
    m_iNextOneShotTime  = 0;
    m_iNextStateTime    = 0;
    m_iLastDrawTime     = 0;
    m_linkVar           = NULL;
    m_linkCount         = -1;
}

void UILabel::MouseEntered(Event *ev)
{
    SetHovermaterialActive(true);
}

void UILabel::MouseExited(Event *ev)
{
    SetHovermaterialActive(false);
}

void UILabel::OneShotSound(Event *ev)
{
    m_sOneShotSoundName = ev->GetString(1);
    m_fOneShotSoundTime = ev->GetFloat(2);
}

void UILabel::LoopingSound(Event *ev)
{
    m_sLoopingSoundName = ev->GetString(1);

    switch (ev->NumArgs()) {
    case 1:
        m_fLoopingSoundHoldStartTime = 0.0;
        m_fLoopingSoundStartVolume   = 1.0;
        m_fLoopingSoundEndVolume     = 1.0;
        m_fLoopingSoundRampTime      = 0.0;
        break;
    case 2:
    case 3:
        m_fLoopingSoundStartVolume   = ev->GetFloat(2);
        m_fLoopingSoundHoldStartTime = 0.0;
        m_fLoopingSoundEndVolume     = ev->GetFloat(2);
        m_fLoopingSoundRampTime      = 0.0;
        break;
    case 4:
        m_fLoopingSoundStartVolume   = ev->GetFloat(2);
        m_fLoopingSoundHoldStartTime = ev->GetFloat(3);
        m_fLoopingSoundEndVolume     = ev->GetFloat(4);
        m_fLoopingSoundRampTime      = 4.0;
        break;
    case 5:
        m_fLoopingSoundStartVolume   = ev->GetFloat(2);
        m_fLoopingSoundHoldStartTime = ev->GetFloat(3);
        m_fLoopingSoundEndVolume     = ev->GetFloat(4);
        m_fLoopingSoundRampTime      = ev->GetFloat(5);
        break;
    default:
        break;
    }
}

// Added: the linked cvar's text: the cvar found once, its text made again
// only when it changed
const char *UILabel::LinkedText(void)
{
    const char *string;

    if (!m_linkVar || Q_stricmp(m_linkVar->name, m_cvarname)) {
        m_linkVar   = UI_FindCvar(m_cvarname);
        m_linkCount = -1;
    }
    if (!m_linkVar) {
        UI_DrawLive();
        return "";
    }
    UI_DrawDep(m_linkVar);
    if (m_linkVar->modificationCount != m_linkCount) {
        string = m_linkVar->latchedString && *m_linkVar->latchedString ? m_linkVar->latchedString : m_linkVar->string;

        m_linkText  = string;
        m_linkCount = m_linkVar->modificationCount;
    }
    return m_linkText.c_str();
}

// Added: the text it draws
const char *UILabel::CurrentText(void)
{
    if (m_cvarname.length()) {
        return LinkedText();
    }
    return label.length() ? label.c_str() : m_title.length() ? m_title.c_str() : "";
}

// Added: its text's box (empty: none at its top left; no text at all: its
// rect), design units
UIRect2D UILabel::ContentBox(void)
{
    const char *text;
    float       w = 0.0f, h, fit;
    int         lines = 1;
    UIRect2D    r;

    if (!m_cvarname.length() && !label.length() && !m_title.length()) {
        return UIWidget::ContentBox();
    }
    Flow();
    r    = m_designRect;
    text = CurrentText();
    if (!*text || !m_font) {
        return UIRect2D(r.pos.x, r.pos.y, 0.0f, 0.0f);
    }
    fit = m_font->FitScale(text, m_fitWidth);
    for (const char *p = text, *e; *p; p = *e ? e + 1 : e) {
        char line[1024];

        e = strchr(p, '\n');
        e = e ? e : p + strlen(p);
        Q_strncpyz(line, p, Q_min((int)sizeof(line), (int)(e - p) + 1));
        w = Q_max(w, m_font->getWidth(line, -1));
        lines += *e == '\n';
    }
    w *= fit;
    h = lines * m_font->getHeight((const float *)NULL) * fit;
    if (m_iFontAlignmentHorizontal == FONT_JUSTHORZ_CENTER) {
        r.pos.x += (r.size.width - w) * 0.5f;
    } else if (m_iFontAlignmentHorizontal == FONT_JUSTHORZ_RIGHT) {
        r.pos.x += r.size.width - w;
    }
    if (m_iFontAlignmentVertical == FONT_JUSTVERT_CENTER) {
        r.pos.y += (r.size.height - h) * 0.5f;
    } else if (m_iFontAlignmentVertical == FONT_JUSTVERT_BOTTOM) {
        r.pos.y += r.size.height - h;
    }
    return UIRect2D(r.pos.x, r.pos.y, w, h);
}

void UILabel::Draw(void)
{
    const char *string;

    ProcessSounds();
    // (sounds: not kept by the draw cache)
    if (m_sOneShotSoundName.length() || m_sLoopingSoundName.length()) {
        UI_DrawLive();
    }

    m_iLastDrawTime = uii.Sys_Milliseconds();

    string = CurrentText();

    if (*string) {
        float nr, ng, nb;

        nr = m_foreground_color.r;
        ng = m_foreground_color.g;
        nb = m_foreground_color.b;

        m_font->setColor(UColor(nr, ng, nb, m_foreground_color.a * m_local_alpha));
        // Added: a long text smaller to fit (fitwidth)
        m_font->setFit(m_font->FitScale(string, m_fitWidth));

        // print the text
        m_font->PrintJustified(
            getClientFrame(), m_iFontAlignmentHorizontal, m_iFontAlignmentVertical, string, getVirtualScale()
        );
        m_font->setFit(1.0f);
    }
}

void UILabel::FrameInitialized()
{
    if (m_sOneShotSoundName.length() || m_sLoopingSoundName.length()) {
        uii.Cmd_Stuff("tmstop\n");
        m_bOneShotTriggered = false;
        m_iNextOneShotTime  = uii.Sys_Milliseconds() + 2000;
        m_iNextStateTime    = 0;
    }

    m_iLastDrawTime = 0;

    UIWidget::FrameInitialized();
}

void UILabel::ProcessSounds()
{
    char  buffer[64];
    float frac;

    if (paused->integer) {
        if (m_iNextOneShotTime) {
            m_iNextOneShotTime += uii.Sys_Milliseconds() - m_iLastDrawTime;
        }

        if (m_iNextStateTime) {
            m_iNextStateTime += uii.Sys_Milliseconds() - m_iLastDrawTime;
        }
    }

    if (m_iNextOneShotTime && uii.Sys_Milliseconds() > m_iNextOneShotTime) {
        if (m_sOneShotSoundName.length() && !m_bOneShotTriggered) {
            //
            // use the looping sound if the oneshot sound was triggered is non-existent
            //
            uii.Snd_StopAllSound();
            uii.Snd_FadeSound(0);
            uii.Snd_PlaySoundDialog(m_sOneShotSoundName);

            m_bOneShotTriggered = true;
            m_iNextOneShotTime  = Sys_Milliseconds() + m_fOneShotSoundTime * 1000.0f;
        } else if (m_sLoopingSoundName.length()) {
            uii.Cmd_Stuff("tmstartloop " + m_sLoopingSoundName + '\n');

            m_iNextOneShotTime = 0;
            m_iSoundState      = 0;
            m_iNextStateTime   = uii.Sys_Milliseconds();
        } else {
            m_iNextOneShotTime = 0;
        }
    }

    if (m_iNextStateTime && uii.Sys_Milliseconds() >= m_iNextStateTime) {
        switch (m_iSoundState) {
        case 0:
            Com_sprintf(buffer, sizeof(buffer), "tmvolume %4.2f\n", m_fLoopingSoundStartVolume);
            uii.Cmd_Stuff(buffer);
            m_iNextStateTime = uii.Sys_Milliseconds() + m_fLoopingSoundHoldStartTime * 1000;
            m_iSoundState    = 1;
            break;
        case 1:
            m_iLastEnterTime = m_iNextStateTime;
            m_iSoundState    = 2;
            m_iNextStateTime = uii.Sys_Milliseconds() + 10;
            break;
        case 2:
            if (m_fLoopingSoundRampTime > 0) {
                frac = (uii.Sys_Milliseconds() - m_iLastEnterTime) / (m_fLoopingSoundRampTime * 1000);
            } else {
                frac = 1.0;
            }

            Com_sprintf(
                buffer,
                sizeof(buffer),
                "tmvolume %4.2f\n",
                frac * (m_fLoopingSoundEndVolume - m_fLoopingSoundStartVolume) + m_fLoopingSoundStartVolume
            );
            uii.Cmd_Stuff(buffer);

            m_iNextStateTime = uii.Sys_Milliseconds() + 10;
            if (frac > 1.0) {
                m_iSoundState = 3;
            }
            break;
        case 3:
            m_iNextStateTime = 0;
            break;
        default:
            break;
        }
    }
}
