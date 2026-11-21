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
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110EV_DEFAULT301  USA
===========================================================================
*/

#include "cl_ui.h"

Event EV_Layout_RenderModel
(
    "rendermodel",
    EV_DEFAULT,
    "b",
    "bool",
    "Render the model specified by the cvar."
);
Event EV_Layout_RenderModelOffset
(
    "modeloffset",
    EV_DEFAULT,
    "v",
    "offset",
    "Render model offset"
);
Event EV_Layout_RenderModelRotateOffset
(
    "modelrotateoffset",
    EV_DEFAULT,
    "v",
    "offset",
    "Render model rotation offset"
);
Event EV_Layout_RenderModelAngles
(
    "modelangles",
    EV_DEFAULT,
    "v",
    "angles",
    "Render model angles"
);
Event EV_Layout_RenderModelScale
(
    "modelscale",
    EV_DEFAULT,
    "f",
    "scale",
    "Render model scale"
);
Event EV_Layout_RenderModelAnim
(
    "modelanim",
    EV_DEFAULT,
    "s",
    "anim",
    "Render model anim"
);
// Added
Event EV_Layout_RenderModelAspect
(
    "modelaspect",
    EV_DEFAULT,
    "b",
    "bool",
    "Render the model in a view as wide as the label (else the view's field is square: a frame that isn't stretches it)"
);
// Added
Event EV_Layout_RenderModelStudio
(
    "modelstudio",
    EV_DEFAULT,
    "b",
    "bool",
    "Light the model with a studio's light of its own (else with the map's where the player is)"
);
CLASS_DECLARATION(UILabel, UIFakkLabel, NULL) {
    {&EV_Layout_RenderModel,             &UIFakkLabel::LayoutRenderModel            },
    {&EV_Layout_RenderModelOffset,       &UIFakkLabel::LayoutRenderModelOffset      },
    {&EV_Layout_RenderModelRotateOffset, &UIFakkLabel::LayoutRenderModelRotateOffset},
    {&EV_Layout_RenderModelAngles,       &UIFakkLabel::LayoutRenderModelAngles      },
    {&EV_Layout_RenderModelScale,        &UIFakkLabel::LayoutRenderModelScale       },
    {&EV_Layout_RenderModelAnim,         &UIFakkLabel::LayoutRenderModelAnim        },
    {&EV_Layout_RenderModelAspect,       &UIFakkLabel::LayoutRenderModelAspect      },
    {&EV_Layout_RenderModelStudio,       &UIFakkLabel::LayoutRenderModelStudio      },
    {NULL,                               NULL                                       }
};

UIFakkLabel::UIFakkLabel()
{
    m_rendermodel = false;
    m_modelaspect = false;
    m_modelstudio = false;
}

void UIFakkLabel::LayoutRenderModel(Event *ev)
{
    m_rendermodel = ev->GetBoolean(1);
}

void UIFakkLabel::LayoutRenderModelOffset(Event *ev)
{
    Vector vec = ev->GetVector(1);
    VectorCopy(vec, m_offset);
}

void UIFakkLabel::LayoutRenderModelRotateOffset(Event *ev)
{
    Vector vec = ev->GetVector(1);
    VectorCopy(vec, m_rotateoffset);
}

void UIFakkLabel::LayoutRenderModelAngles(Event *ev)
{
    Vector vec = ev->GetVector(1);
    VectorCopy(vec, m_angles);
}

void UIFakkLabel::LayoutRenderModelScale(Event *ev)
{
    m_scale = ev->GetFloat(1);
}

void UIFakkLabel::LayoutRenderModelAnim(Event *ev)
{
    m_anim = ev->GetString(1);
}

void UIFakkLabel::LayoutRenderModelAspect(Event *ev)
{
    m_modelaspect = ev->GetBoolean(1);
}

void UIFakkLabel::LayoutRenderModelStudio(Event *ev)
{
    m_modelstudio = ev->GetBoolean(1);
}

void UIFakkLabel::Draw(void)
{
    qhandle_t handle;
    vec3_t    origin;
    vec4_t    color;
    str       sAnimName;

    if (!m_rendermodel) {
        UILabel::Draw();
        return;
    }

    VectorSet4(color, 255, 255, 255, 255);

    if (!m_cvarname.length()) {
        return;
    }

    handle = re.RegisterModel(Cvar_VariableString(m_cvarname));
    if (!handle) {
        return;
    }

    sAnimName = m_anim;

    // Added in 2.11
    //  Tunak easter egg
    if (Cvar_Get("tunak", "0", 0)->integer) {
        // Added in 2.15
        //  These animations start with 'zz' prefix
        if (strstr(m_anim, "american")) {
            sAnimName = "zzamericanspecialidle";
        } else {
            // Added in 2.15
            sAnimName = "zzgermanspecialidle";
        }
    }

    // (a figure's box: 32 across, 96 high, from its feet)
    origin[0] = 96.0f * m_scale * 0.5 / 0.268f;
    origin[1] = 0;
    origin[2] = -48;

    CL_Draw3DModel(
        m_screenframe.pos.x,
        m_screenframe.pos.y,
        m_screenframe.size.width,
        m_screenframe.size.height,
        handle,
        origin,
        m_rotateoffset,
        m_offset,
        m_angles,
        color,
        sAnimName,
        m_modelaspect,
        m_modelstudio
    );

    set2D();
}
