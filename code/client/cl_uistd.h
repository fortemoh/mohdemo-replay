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

class UIFakkLabel : public UILabel
{
protected:
    qboolean m_rendermodel;
    vec3_t   m_offset;
    vec3_t   m_rotateoffset;
    vec3_t   m_angles;
    float    m_scale;
    str      m_anim;
    bool     m_modelaspect; // Added: the model's view as wide as the label (modelaspect)
    bool     m_modelstudio; // Added: lit by a studio's light, not the map's (modelstudio)

public:
    CLASS_PROTOTYPE(UIFakkLabel);

    UIFakkLabel();

    void LayoutRenderModel(Event *ev);
    void LayoutRenderModelOffset(Event *ev);
    void LayoutRenderModelRotateOffset(Event *ev);
    void LayoutRenderModelAngles(Event *ev);
    void LayoutRenderModelScale(Event *ev);
    void LayoutRenderModelAnim(Event *ev);
    void LayoutRenderModelAspect(Event *ev); // Added
    void LayoutRenderModelStudio(Event *ev); // Added

    void Draw(void) override;
    // Added: kept by the draw cache when drawn as a UILabel (no model)
    bool CacheDraw(void) override { return !m_rendermodel; }
};
