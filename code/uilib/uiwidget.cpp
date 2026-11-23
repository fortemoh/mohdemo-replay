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
#include "uibake.h"

Event W_Destroyed
(
    "widget_destroyed",
    EV_DEFAULT,
    NULL,
    NULL,
    "Specifies that the widget has been destroyed." 
);

Event W_SizeChanged
(
    "size_changed",
    EV_DEFAULT,
    NULL,
    NULL,
    "Specifies that the size of the widget has changed." 
);

Event W_FrameChanged
(
    "frame_changed",
    EV_DEFAULT,
    NULL,
    NULL,
    "Specifies that the frame has changed." 
);

Event W_OriginChanged
(
    "origin_changed",
    EV_DEFAULT,
    NULL,
    NULL,
    "Specifies that the origin f the widget has changed." 
);

Event W_Activated
(
    "activated",
    EV_DEFAULT,
    NULL,
    NULL,
    "Signals that the widget has been activated." 
);

Event EV_Widget_Activate
(
    "activate",
    EV_DEFAULT,
    NULL,
    NULL,
    "Activate this widget." 
);

Event W_Deactivated
(
    "deactivated",
    EV_DEFAULT,
    NULL,
    NULL,
    "Signals that the widget has been deactivated." 
);

Event EV_Widget_Hide
(
    "hide",
    EV_DEFAULT,
    NULL,
    NULL,
    "Hides the widget." 
);

Event EV_Widget_Enable
(
    "enable",
    EV_DEFAULT,
    NULL,
    NULL,
    "enable the widget." 
);

Event EV_Widget_Disable
(
    "disable",
    EV_DEFAULT,
    NULL,
    NULL,
    "enable the widget." 
);

Event W_RealignWidget
(
    "realign_widget",
    EV_DEFAULT,
    NULL,
    NULL,
    "Specifies that the widget has been realigned." 
);

Event W_Draw
(
    "draw_widget",
    EV_DEFAULT,
    NULL,
    NULL,
    "Draw the widget." 
);

Event EV_Layout_Rect
(
    "rect",
    EV_DEFAULT,
    "ffff",
    "x y width height",
    "Set the rect of the widget" 
);

Event EV_Layout_Name
(
    "name",
    EV_DEFAULT,
    "s",
    "name",
    "Set the name of the widget"
);

Event EV_Layout_Title
(
    "title",
    EV_DEFAULT,
    "s",
    "title",
    "Set the title of the widget to be displayed in the center of it"
);

Event EV_Layout_Font
(
    "font",
    EV_DEFAULT,
    "s",
    "title",
    "Set the font of the widget"
);

Event EV_Layout_BGColor
(
    "bgcolor",
    EV_DEFAULT,
    "ffff",
    "red green blue alpha",
    "Set the background color of the widget"
);

Event EV_Layout_FGColor
(
    "fgcolor",
    EV_DEFAULT,
    "ffff",
    "red green blue alpha",
    "Set the foreground color of the widget"
);

Event EV_Layout_Borderstyle
(
    "borderstyle",
    EV_DEFAULT,
    "s",
    "style",
    "Set the borderstyle of the widget(NONE, 3D_BORDER, INDENT_BORDER) "
);

Event EV_Layout_Shader
(
    "shader",
    EV_DEFAULT,
    "s",
    "shaderName",
    "Set the shader to draw on the background of the widget"
);

Event EV_Layout_HoverShader
(
    "hovershader",
    EV_DEFAULT,
    "s",
    "shaderName",
    "Set the shader to on the background of the widget when the mouse is over the widget"
);

Event EV_Layout_FadeIn
(
    "fadein",
    EV_DEFAULT,
    "f",
    "fadetime",
    "Set the amount of time it takes for the widget to fade in"
);

Event EV_Layout_ClickSound
(
    "clicksound",
    EV_DEFAULT,
    "s",
    "soundName",
    "Set the sound to be played when the widget is clicked"
);

Event EV_Layout_VirtualRes
(
    "virtualres",
    EV_DEFAULT,
    "iFF",
    "usevirtualres design_width design_height",
    "Set the widget to be modified horizontally or vertically across the screen to account for resolutions != 640x480"
    " (Added: with a design's size, scaled alike both ways to fit the screen, on whole pixels)"
);

Event EV_Layout_Align
(
    "align",
    EV_DEFAULT,
    "s",
    "alignment",
    "Set the side of the screen to align the widget(left right top bottom centerx centery)"
);

Event EV_Layout_StuffCommand
(
    "stuffcommand",
    EV_DEFAULT,
    "s",
    "command",
    "Set the command to be stuffed when the widget is clicked"
);

Event EV_Layout_LinkCvar
(
    "linkcvar",
    EV_DEFAULT,
    "s",
    "cvarname",
    "Set the cvar that will be updated when the widget is changed"
);

Event EV_Widget_OrderNumber
(
    "ordernumber",
    EV_DEFAULT,
    "d",
    "num",
    "Set the order the widget should be activated in"
);

Event EV_Widget_TextAlignment
(
    "textalign",
    EV_DEFAULT,
    "s",
    "alignment",
    "Sets the alignment of text for this widget"
);

Event EV_Widget_EnabledCvar
(
    "enabledcvar",
    EV_DEFAULT,
    "s",
    "cvar_name",
    "Set the cvar to check to see if this button is enabled or not"
);

// Added
Event EV_Layout_Shape
(
    "shape",
    EV_DEFAULT,
    "sSSSSSSSSSSSSSSS",
    "kind color ...",
    "Adds a layer to the shape baked at the widget's size (uibake.cpp, Bake_Layer); $name: cvar name's value"
);

Event EV_Layout_RotateCvar
(
    "rotatecvar",
    EV_DEFAULT,
    "s",
    "cvar_name",
    "Turns the widget's shape by the cvar's degrees, clockwise"
);

Event EV_Layout_AlphaCvar
(
    "alphacvar",
    EV_DEFAULT,
    "s",
    "cvar_name",
    "Fades the widget by the cvar's value (0 to 1)"
);

Event EV_Layout_ColorCvar
(
    "colorcvar",
    EV_DEFAULT,
    "s",
    "cvar_name",
    "The widget's text and shape colored by the cvar's \"r g b a\""
);

Event EV_Layout_WidthCvar
(
    "widthcvar",
    EV_DEFAULT,
    "sS",
    "cvar_name align",
    "The widget's shape drawn the cvar's fraction of its width, about its middle (or from its left: left)"
);

Event EV_Layout_Shadow
(
    "shadow",
    EV_DEFAULT,
    NULL,
    NULL,
    "The widget's shape with the drop shadow of the faces' s under it"
);

Event EV_Layout_FlowAfter
(
    "flowafter",
    EV_DEFAULT,
    "sf",
    "widget_name gap",
    "Placed gap design units after where a sibling's text ends (or its right edge)"
);

Event EV_Layout_FlowBefore
(
    "flowbefore",
    EV_DEFAULT,
    "sf",
    "widget_name gap",
    "Its right edge gap design units before where a sibling's text starts (or its left edge)"
);

Event EV_Layout_FlowBelow
(
    "flowbelow",
    EV_DEFAULT,
    "sf",
    "widget_name gap",
    "Its top gap design units under a sibling's text (or its bottom edge)"
);

Event EV_Layout_FitBelow
(
    "fitbelow",
    EV_DEFAULT,
    "sf",
    "widget_name pad",
    "Its bottom pad design units under a sibling's text (or its bottom edge)"
);

Event EV_Layout_MoveIf
(
    "moveif",
    EV_DEFAULT,
    "sff",
    "widget_name dx dy",
    "Moved dx dy design units while a sibling is shown"
);

Event EV_Layout_FitWidth
(
    "fitwidth",
    EV_DEFAULT,
    "f",
    "width",
    "Its text drawn smaller when wider than this (to half its size)"
);

Event EV_Layout_DrawCache
(
    "drawcache",
    EV_DEFAULT,
    "i",
    "on",
    "What it and its children draw kept and drawn again while the cvars they read, their frames and alpha stay"
);

Event EV_Layout_Anchor
(
    "anchor",
    EV_DEFAULT,
    "ff",
    "x y",
    "A menu's middle at these fractions of the screen's width and height"
);

// Added in 2.0
Event EV_Layout_FadeSequence
(
    "fadesequence",
    EV_DEFAULT,
    "ffff",
    "delay fadein hold fadeout",
    "Set up for a delay/fadein/hold/fadeout"
);

Event EV_Layout_BGFill
(
    "bgfill",
    EV_DEFAULT,
    "ffff",
    "red green blue alpha",
    "Set a color to fill the entire background"
);

Event EV_Layout_Fullscreen
(
    "fullscreen",
    EV_DEFAULT,
    "b",
    "fullscreen",
    "Set container to be fullscreen.  This will disable the game rendering if it's active"
);

CLASS_DECLARATION(USignal, UIWidget, NULL) {
    {&EV_Widget_Hide,               &UIWidget::Hide                    },
    {&EV_Widget_Activate,           &UIWidget::Activate                },
    {&EV_Layout_Rect,               &UIWidget::LayoutRect              },
    {&EV_Layout_Name,               &UIWidget::LayoutName              },
    {&EV_Layout_Title,              &UIWidget::LayoutTitle             },
    {&EV_Layout_Font,               &UIWidget::LayoutFont              },
    {&EV_Layout_BGColor,            &UIWidget::LayoutBgColor           },
    {&EV_Layout_FGColor,            &UIWidget::LayoutFgColor           },
    {&EV_Layout_Borderstyle,        &UIWidget::LayoutBorderStyle       },
    {&EV_Layout_Shader,             &UIWidget::LayoutShader            },
    {&EV_Layout_HoverShader,        &UIWidget::LayoutHoverShader       },
    {&EV_Layout_FadeIn,             &UIWidget::LayoutFadeIn            },
    {&EV_Layout_ClickSound,         &UIWidget::LayoutClickSound        },
    {&EV_Layout_VirtualRes,         &UIWidget::LayoutVirtualRes        },
    {&EV_Layout_Align,              &UIWidget::LayoutAlign             },
    {&EV_Layout_StuffCommand,       &UIWidget::LayoutStuffCommand      },
    {&EV_Layout_LinkCvar,           &UIWidget::LayoutLinkCvar          },
    {&EV_Widget_OrderNumber,        &UIWidget::LayoutOrderNumber       },
    {&EV_Widget_TextAlignment,      &UIWidget::TextAlignment           },
    {&EV_Widget_Enable,             &UIWidget::EnableEvent             },
    {&EV_Widget_Disable,            &UIWidget::DisableEvent            },
    {&EV_Widget_EnabledCvar,        &UIWidget::SetEnabledCvar          },

    //
    // Added in 2.0
    //

    {&EV_Layout_FadeSequence,       &UIWidget::EventFadeSequence       },

    // Added
    {&EV_Layout_Shape,              &UIWidget::LayoutShape             },
    {&EV_Layout_RotateCvar,         &UIWidget::LayoutRotateCvar        },
    {&EV_Layout_AlphaCvar,          &UIWidget::LayoutAlphaCvar         },
    {&EV_Layout_ColorCvar,          &UIWidget::LayoutColorCvar         },
    {&EV_Layout_WidthCvar,          &UIWidget::LayoutWidthCvar         },
    {&EV_Layout_Shadow,             &UIWidget::LayoutShadow            },
    {&EV_Layout_FlowAfter,          &UIWidget::LayoutFlowAfter         },
    {&EV_Layout_FlowBefore,         &UIWidget::LayoutFlowBefore        },
    {&EV_Layout_FlowBelow,          &UIWidget::LayoutFlowBelow         },
    {&EV_Layout_FitBelow,           &UIWidget::LayoutFitBelow          },
    {&EV_Layout_MoveIf,             &UIWidget::LayoutMoveIf            },
    {&EV_Layout_FitWidth,           &UIWidget::LayoutFitWidth          },
    {&EV_Layout_Anchor,             &UIWidget::LayoutAnchor            },
    {&EV_Layout_DrawCache,          &UIWidget::LayoutDrawCache         },

    {NULL,                          NULL                               }
};

void SetColor(const UColor& color, float alpha)
{
    const vec4_t col = {color.r, color.g, color.b, color.a * alpha};

    uii.Rend_SetColor(col);
}

void DrawBox(const UIRect2D& rect, const UColor& color, float alpha)
{
    DrawBox(rect.pos.x, rect.pos.y, rect.size.width, rect.size.height, color, alpha);
}

void Draw3DBox(const UIRect2D& rect, bool indent, const UBorderColor& color, float alpha)
{
    Draw3DBox(rect.pos.x, rect.pos.y, rect.size.width, rect.size.height, indent, color, alpha);
}

void DrawBox(float x, float y, float width, float height, const UColor& color, float alpha)
{
    SetColor(color, alpha);
    uii.Rend_DrawBox(x, y, width, height);
}

void Draw3DBox(float x, float y, float width, float height, bool indent, const UBorderColor& color, float alpha)
{
    if (indent) {
        DrawBox(x, y, 1.0, height - 1.0, color.dark, alpha);
        DrawBox(x + 1.0, y + 1.0, 1.0, height - 3.0, color.reallydark, alpha);
        DrawBox(x, y, width - 1.0, 1.0, color.dark, alpha);
        DrawBox(x + 1.0, y + 1.0, width - 1.0, 1.0, color.reallydark, alpha);
        DrawBox(x + width - 1.0, y, 1.0, height, color.light, alpha);
        DrawBox(x, y + height - 1.0, width, 1.0, color.light, alpha);
    } else {
        DrawBox(x + width - 2.0, y + 1.0, 1.0, height - 2.0, color.dark, alpha);
        DrawBox(x + width - 1.0, y, 1.0, height, color.reallydark, alpha);
        DrawBox(x + 1.0, y + height - 2.0, width - 2.0, 1.0, color.dark, alpha);
        DrawBox(x, y + height - 1.0, width, 1.0, color.reallydark, alpha);
        DrawBox(x, y, 1.0, height - 1.0, color.light, alpha);
        DrawBox(x, y, width - 1.0, 1.0, color.light, alpha);
    }
}

void DrawMac3DBox(const UIRect2D& rect, bool indent, const UBorderColor& color, int inset, float alpha)
{
    float x, y;
    float width, height;

    x      = rect.pos.x + inset;
    y      = rect.pos.y + inset;
    width  = rect.size.width - inset * 2;
    height = rect.size.height - inset * 2;

    if (indent) {
        DrawBox(x + 1.0, y + 1.0, width - 2.0, height - 2.0, color.dark, alpha);
        DrawBox(x, y, width, 1.0, color.reallydark, alpha);
        DrawBox(x, y + 1.0, 1.0, width - 2.0, color.reallydark, alpha);
        DrawBox(x + width - 1.0, y + 1.0, 1.0, height - 1.0, color.light, alpha);
        DrawBox(x, y + height - 1.0, width - 1.0, 1.0, color.light, alpha);
    } else {
        DrawBox(x, y, width, 1.0, color.light, alpha);
        DrawBox(x, y + 1.0, 1.0, height - 2.0, color.light, alpha);
        DrawBox(x + width - 1.0, y + 1.0, 1.0, height - 1.0, color.dark, alpha);
        DrawBox(x, y + height - 1.0, width - 1.0, 1.0, color.dark, alpha);
    }
}

void DrawBoxWithSolidBorder(
    const UIRect2D& rect, const UColor& inside, const UColor& outside, int size, int flags, float alpha
)
{
    if (flags & 1) {
        DrawBox(
            rect.pos.x + size, rect.pos.y + size, rect.size.width - size * 2, rect.size.height - size * 2, inside, alpha
        );
    }

    if (size && flags & 2) {
        DrawBox(rect.pos.x, rect.pos.y, size, rect.size.height - size, outside, alpha);
        DrawBox(rect.pos.x + size, rect.pos.y, rect.size.width - size, size, outside, alpha);
        DrawBox(rect.pos.x + rect.size.width - size, rect.pos.y + size, size, rect.size.height - size, outside, alpha);
        DrawBox(rect.pos.x, rect.pos.y + rect.size.height - size, rect.size.width - size, size, outside, alpha);
    }
}

static bool scaleFrameVirtualRes(UIRect2D& frame, vec3_t outScale, const vec3_t newScale)
{
    if (!VectorCompare2D(outScale, newScale)) {
        frame.pos.x       = frame.pos.x / outScale[0] * newScale[0];
        frame.pos.y       = frame.pos.y / outScale[1] * newScale[1];
        frame.size.width  = frame.size.width / outScale[0] * newScale[0];
        frame.size.height = frame.size.height / outScale[1] * newScale[1];
        outScale[0]       = newScale[0];
        outScale[1]       = newScale[1];

        return true;
    }

    return false;
}

UIReggedMaterial::UIReggedMaterial()
{
    hMat  = 0;
    isSet = true;
    isGot = false;
}

uihandle_t UIReggedMaterial::GetMaterial()
{
    if (!isGot) {
        isGot      = true;
        this->hMat = uii.Rend_RegisterMaterial(name.c_str());
    }
    return hMat;
}

void UIReggedMaterial::ReregisterMaterial(void)
{
    if (isGot) {
        this->hMat = uii.Rend_RegisterMaterial(name.c_str());
    }
}

void UIReggedMaterial::CleanupMaterial(void)
{
    isGot = false;
}

void UIReggedMaterial::SetMaterial(const str& name)
{
    this->name = name;
}

str UIReggedMaterial::GetName(void)
{
    return name;
}

UIWidget::UIWidget()
{
    VectorSet2D(m_vVirtualScale, 1, 1);
    VectorSet2D(m_designSize, 0, 0);
    m_designSet   = false;
    m_shapeStamp  = -1;
    m_shapeW      = 0;
    m_shapeH      = 0;
    m_shapeHandle = 0;
    m_shapeDynamic = false;
    m_widthLeft   = false;
    m_shadow      = false;
    memset(m_cvars, 0, sizeof(m_cvars));
    m_colorCount = -1;
    VectorSet2D(m_anchor, -1, -1);
    m_baseSet     = false;
    m_flowing     = false;
    m_fitWidth    = 0;
    m_parent = NULL;
    m_background_color.set(0.075f, 0.097f, 0.139f, 1.0f);
    m_foreground_color         = UColor(UHudColor);
    m_border_color             = UColor(0.15f, 0.195f, 0.278f, 1.0f);
    m_indent                   = 10.0f;
    m_bottomindent             = 5.0f;
    m_borderStyle              = border_indent;
    m_visible                  = true;
    m_enabled                  = true;
    m_canactivate              = false;
    m_font                     = NULL;
    m_material                 = NULL;
    m_hovermaterial            = NULL;
    m_hovermaterial_active     = false;
    m_fadetime                 = 0.0f;
    m_alpha                    = 1.0f;
    m_motiontime               = 0.0f;
    m_starttime                = 0.0f;
    m_motiontype               = MOTION_IN;
    m_align                    = 0;
    m_flags                    = 0;
    m_dying                    = false;
    m_ordernum                 = 0;
    m_iFontAlignmentHorizontal = FONT_JUSTHORZ_CENTER;
    m_iFontAlignmentVertical   = FONT_JUSTVERT_CENTER;
    m_bVirtual                 = false;
    m_enabledCvar              = "";
    m_numEnabled               = -1;
    m_drawCache                = false;
    m_drawList                 = 0;
    m_drawRef                  = 0;
    m_drawValid                = false;
    m_drawLive                 = false;
    m_drawStamp                = 0;
    m_drawKey                  = 0;
    m_drawAlpha                = 0;

    m_fadeSequenceDelay    = 0;
    m_fadeSequenceFadeIn   = 0;
    m_fadeSequenceHold     = 0;
    m_fadeSequenceFadeOut  = 0;
    m_fadeSequenceState    = fadesequence_t::SEQUENCE_NONE;
    m_fadeSequenceLastTime = 0;

    UIRect2D frame(6.0f, 6.0f, 100.0f, 13.0f);
    setFrame(frame);

    lastShowTime = -1;
}

static void UI_DrawForget(UIWidget *widget);

UIWidget::~UIWidget()
{
    //int i;

    m_dying = true;
    // Added: not the widget whose list is being made any more (an error in mid-draw left it there)
    UI_DrawForget(this);

    // (a handle of an earlier renderer may name another widget's list now)
    if (m_drawList && m_drawRef == uii.GetRefSequence() && uii.Rend_Free2DList && uii.IsRendererLoaded()) {
        uii.Rend_Free2DList(m_drawList);
    }

    if (IsActive()) {
        uWinMan.DeactivateCurrentSmart();
    }

    //for( i = m_children.NumObjects(); i > 0; i-- ) {
    //
    //}

    // remove this child from our parent
    if (m_parent) {
        m_parent->removeChild(this);
    }

    // delete font
    if (m_font) {
        delete m_font;
        m_font = NULL;
    }

    SendSignal(W_Destroyed);
}

bool UIWidget::addChild(UIWidget *widget)
{
    if (m_children.ObjectInList(widget)) {
        return false;
    }

    m_children.AddObject(widget);
    if (m_children.NumObjects() != 1) {
        UIWidget *child = m_children.ObjectAt(m_children.NumObjects() - 1);
        if ((child->m_flags & WF_ALWAYS_BOTTOM) != (widget->m_flags & WF_ALWAYS_BOTTOM)) {
            ArrangeWidgetList(m_children);
        }
    }

    return true;
}

bool UIWidget::removeChild(UIWidget *widget)
{
    if (m_children.ObjectInList(widget)) {
        m_children.RemoveObject(widget);
        return true;
    }

    return false;
}

void UIWidget::PropogateCoordinateSystem(void)
{
    int       i;
    int       n;
    UIWidget *subview;

    if (!m_parent || (m_flags & WF_NOPARENTADJUST)) {
        m_screenorigin  = m_origin;
        m_clippedorigin = m_origin;
        m_screenframe   = m_frame;
        m_clippedframe  = m_frame;
    } else {
        m_screenframe.pos.x = m_frame.pos.x + m_parent->m_screenorigin.x;
        m_screenframe.pos.y = m_frame.pos.y + m_parent->m_screenorigin.y;
        m_screenframe.size  = m_frame.size;
        m_screenorigin.x    = m_screenframe.pos.x - m_origin.x;
        m_screenorigin.y    = m_screenframe.pos.y - m_origin.y;
        m_clippedframe      = m_screenframe;
        m_clippedorigin.x   = -m_origin.x;
        m_clippedorigin.y   = -m_origin.y;
    }

    if (m_parent && !(m_flags & WF_DIRECTED)) {
        if (m_parent->m_clippedframe.pos.x > m_clippedframe.pos.x) {
            m_clippedframe.size.width -= (m_parent->m_clippedframe.pos.x - m_clippedframe.pos.x);

            if (m_clippedframe.size.width < 0.0) {
                m_clippedframe.size.width = 0.0;
            }

            m_clippedframe.pos.x = m_parent->m_clippedframe.pos.x;
        }

        if (m_parent->m_clippedframe.pos.y > m_clippedframe.pos.y) {
            m_clippedframe.size.height -= (m_parent->m_clippedframe.pos.y - m_clippedframe.pos.y);

            if (m_clippedframe.size.height < 0.0) {
                m_clippedframe.size.height = 0.0;
            }

            m_clippedframe.pos.y = m_parent->m_clippedframe.pos.y;
        }

        if (m_parent->m_clippedframe.getMaxX() < m_clippedframe.getMaxX()) {
            m_clippedframe.size.width = m_parent->m_clippedframe.getMaxX() - m_clippedframe.pos.x;

            if (m_clippedframe.size.width < 0.0) {
                m_clippedframe.size.width = 0.0;
            }
        }

        if (m_parent->m_clippedframe.getMaxY() < m_clippedframe.getMaxY()) {
            m_clippedframe.size.height = m_parent->m_clippedframe.getMaxY() - m_clippedframe.pos.y;

            if (m_clippedframe.size.height < 0.0) {
                m_clippedframe.size.height = 0.0;
            }
        }
    }

    n = m_children.NumObjects();
    for (i = 1; i <= n; i++) {
        subview = m_children.ObjectAt(i);
        subview->PropogateCoordinateSystem();
    }
}

// Changed: margin, the window and the scissor that many pixels wider all
// round, its place on the screen the same (a shape's shadow past its box)
void UIWidget::set2D(float margin)
{
    const float m = margin;

    uii.Rend_Set2D(
        m_screenframe.pos.x - m,
        uid.vidHeight - (int)(m_screenframe.size.height + m_screenframe.pos.y) - m,
        m_screenframe.size.width + 2 * m,
        m_screenframe.size.height + 2 * m,
        m_clippedorigin.x - m,
        m_clippedorigin.x + m_screenframe.size.width + m,
        m_clippedorigin.y + m_screenframe.size.height + m,
        m_clippedorigin.y - m,
        -1.0,
        1.0
    );

    uii.Rend_Scissor(
        m_clippedframe.pos.x - m,
        uid.vidHeight - (int)(m_clippedframe.size.height + m_clippedframe.pos.y) - m,
        m_clippedframe.size.width + 2 * m,
        m_clippedframe.size.height + 2 * m
    );

    //
    // Added in OPM
    //

    if (lastShowTime == -1) {
        lastShowTime = uii.Sys_Milliseconds() / 1000.0;
    }

    uii.Rend_Set2DStartTime(lastShowTime);
}

void UIWidget::Draw(void) {}

void UIWidget::FrameInitialized(void)
{
    if (m_fadeSequenceDelay || m_fadeSequenceFadeIn || m_fadeSequenceHold || m_fadeSequenceFadeOut) {
        m_fadeSequenceRemainingTime = m_fadeSequenceDelay;
        m_fadeSequenceState         = fadesequence_t::SEQUENCE_STARTING;
    }
}

void UIWidget::DrawTitle(float x, float y)
{
    m_font->setColor(m_foreground_color);
    m_font->Print(x, y, Sys_LV_CL_ConvertString(m_title), -1, getVirtualScale());
}

void UIWidget::Motion(void)
{
    float t;
    float frac;

    t = uid.time / 1000.0;

    if (m_fadeSequenceState != fadesequence_t::SEQUENCE_NONE) {
        // Added in 2.0
        //  Process fade sequence
        if (!paused->integer) {
            switch (m_fadeSequenceState) {
            case fadesequence_t::SEQUENCE_STARTING:
                m_alpha = 0.0;

                m_fadeSequenceRemainingTime -= t - m_fadeSequenceLastTime;
                if (m_fadeSequenceRemainingTime < 0) {
                    m_fadeSequenceState         = fadesequence_t::SEQUENCE_FADEIN;
                    m_fadeSequenceRemainingTime = m_fadeSequenceFadeIn;
                }
                break;
            case fadesequence_t::SEQUENCE_FADEIN:
                m_alpha = 1.0 - m_fadeSequenceRemainingTime / m_fadeSequenceFadeIn;

                m_fadeSequenceRemainingTime -= t - m_fadeSequenceLastTime;
                if (m_fadeSequenceRemainingTime < 0) {
                    m_fadeSequenceState         = fadesequence_t::SEQUENCE_HOLD;
                    m_fadeSequenceRemainingTime = m_fadeSequenceHold;
                }
                break;
            case fadesequence_t::SEQUENCE_HOLD:
                m_alpha = 1.0;

                m_fadeSequenceRemainingTime -= t - m_fadeSequenceLastTime;
                if (m_fadeSequenceRemainingTime < 0) {
                    m_fadeSequenceState         = fadesequence_t::SEQUENCE_FADEOUT;
                    m_fadeSequenceRemainingTime = m_fadeSequenceFadeOut;
                }
                break;
            case fadesequence_t::SEQUENCE_FADEOUT:
                m_alpha = m_fadeSequenceRemainingTime / m_fadeSequenceFadeOut;

                m_fadeSequenceRemainingTime -= (t - m_fadeSequenceLastTime);
                if (m_fadeSequenceRemainingTime < 0) {
                    // Fade sequence has finished fading
                    m_alpha             = 0;
                    m_fadeSequenceState = fadesequence_t::SEQUENCE_NONE;
                }
                break;
            default:
                break;
            }
        }

        m_fadeSequenceLastTime = t;
    } else if (m_fadetime >= 0.0) {
        frac = (t - m_starttime) / m_fadetime;

        m_alpha = frac;
        if (m_alpha > 1.0) {
            m_alpha = 1.0;
        }

        if (m_motiontype == MOTION_OUT) {
            m_alpha = 1.0 - m_alpha;
        }
    }
}

void UIWidget::AlignPosition(void)
{
    if (DesignFrame()) {
        // Added: a design-size menu's
    } else if (m_bVirtual) {
        vec2_t vNewVirtualScale;

        SetVirtualScale(vNewVirtualScale);

        scaleFrameVirtualRes(m_frame, m_vVirtualScale, vNewVirtualScale);
    } else if (uid.bHighResScaling) {
        scaleFrameVirtualRes(m_frame, m_vVirtualScale, uid.scaleRes);
    } else {
        vec2_t vNewVirtualScale = {1.0, 1.0};
        scaleFrameVirtualRes(m_frame, m_vVirtualScale, vNewVirtualScale);
    }

    if (m_align & WA_CENTERX) {
        m_frame.pos.x = uid.vidWidth * 0.5 - m_frame.size.width * 0.5;
    }

    if (m_align & WA_RIGHT) {
        m_frame.pos.x = uid.vidWidth - m_frame.size.width;
    } else if (m_align & WA_LEFT) {
        m_frame.pos.x = 0;
    }

    if (m_align & WA_CENTERY) {
        m_frame.pos.y = uid.vidHeight * 0.5 - m_frame.size.height * 0.5;
    }

    if (m_align & WA_BOTTOM) {
        m_frame.pos.y = uid.vidHeight - m_frame.size.height;
    } else if (m_align & WA_TOP) {
        m_frame.pos.y = 0;
    }

    setFrame(m_frame);
}

void UIWidget::Hide(Event *ev)
{
    setShow(false);
}

void UIWidget::Activate(Event *ev)
{
    uWinMan.ActivateControl(this);
}

void UIWidget::BringToFront(void)
{
    UIWidget *me;

    if (m_parent->m_children.NumObjects() > 1) {
        me = m_parent->m_children.ObjectAt(m_parent->m_children.NumObjects());
        if (me != this) {
            m_parent->m_children.RemoveObject(this);
            m_parent->m_children.AddObject(this);

            if ((m_parent->m_children.ObjectAt(m_parent->m_children.NumObjects() - 1)->m_flags & WF_ALWAYS_BOTTOM)
                != (m_flags & WF_ALWAYS_BOTTOM)) {
                ArrangeWidgetList(m_parent->m_children);
            }
        }
    }
}

// Changed: the always-on-bottom widgets first, the rest after, each in their order (the always-on-top layer is gone:
// nothing set it)
void UIWidget::ArrangeWidgetList(Container<UIWidget *>& list)
{
    Container<UIWidget *> bottom;
    Container<UIWidget *> normal;
    int                   i;

    for (i = 1; i <= list.NumObjects(); i++) {
        UIWidget *wid = list.ObjectAt(i);

        if (wid->m_flags & WF_ALWAYS_BOTTOM) {
            bottom.AddObject(wid);
        } else {
            normal.AddObject(wid);
        }
    }

    list.ClearObjectList();
    for (i = 1; i <= bottom.NumObjects(); i++) {
        list.AddObject(bottom.ObjectAt(i));
    }
    for (i = 1; i <= normal.NumObjects(); i++) {
        list.AddObject(normal.ObjectAt(i));
    }
}

void UIWidget::Shutdown(void)
{
    int i;

    if (IsActive()) {
        uWinMan.DeactivateCurrentSmart();
    }

    // notify childs
    for (i = m_children.NumObjects(); i > 0; i--) {
        UIWidget *wid = m_children.ObjectAt(i);

        wid->Shutdown();
        delete wid;
    }

    if (m_font) {
        delete m_font;
        m_font = NULL;
    }
}

void UIWidget::InitFrame(
    UIWidget *parentview, float x, float y, float width, float height, int border, const char *fontname
)
{
    if (border != -1) {
        setBorderStyle((borderstyle_t)border);
    }

    setFrame(UIRect2D(x, y, width, height));

    if (parentview) {
        setParent(parentview);
    } else if (this != &uWinMan) {
        setParent(&uWinMan);
    }

    setFont(fontname);

    FrameInitialized();
}

void UIWidget::InitFrame(UIWidget *parentview, const UIRect2D& r, int border, const char *fontname)
{
    InitFrame(parentview, r.pos.x, r.pos.y, r.size.width, r.size.height, border, fontname);
}

void UIWidget::LayoutRect(Event *ev)
{
    UIPoint2D pos;
    UISize2D  size;

    if (ev->NumArgs() == 4) {
        pos.x       = ev->GetFloat(1);
        pos.y       = ev->GetFloat(2);
        size.width  = ev->GetFloat(3);
        size.height = ev->GetFloat(4);

        setFrame(UIRect2D(pos, size));
    }
}

void UIWidget::LayoutName(Event *ev)
{
    setName(ev->GetString(1));
}

void UIWidget::LayoutTitle(Event *ev)
{
    setTitle(ev->GetString(1));
}

void UIWidget::LayoutFont(Event *ev)
{
    setFont(ev->GetString(1));
}

void UIWidget::LayoutBgColor(Event *ev)
{
    setBackgroundColor(UColor(ev->GetFloat(1), ev->GetFloat(2), ev->GetFloat(3), ev->GetFloat(4)), true);
}

void UIWidget::LayoutFgColor(Event *ev)
{
    setForegroundColor(UColor(ev->GetFloat(1), ev->GetFloat(2), ev->GetFloat(3), ev->GetFloat(4)));
}

void UIWidget::LayoutBorderStyle(Event *ev)
{
    str style = ev->GetString(1);

    if (!stricmp(style, "NONE")) {
        setBorderStyle(border_none);
    } else if (!stricmp(style, "3D_BORDER")) {
        setBorderStyle(border_3D);
    } else if (!stricmp(style, "INDENT_BORDER")) {
        setBorderStyle(border_indent);
    }
}

void UIWidget::LayoutShader(Event *ev)
{
    setMaterial(uWinMan.RegisterShader(ev->GetString(1)));
}

void UIWidget::LayoutHoverShader(Event *ev)
{
    setHoverMaterial(uWinMan.RegisterShader(ev->GetString(1)));
}

void UIWidget::LayoutFadeIn(Event *ev)
{
    m_fadetime = ev->GetFloat(1);
    if (m_motiontime != 0.0) {
        setMotionTime(m_fadetime);
    }
}

void UIWidget::LayoutClickSound(Event *ev)
{
    m_clicksound = ev->GetString(1);
    uii.Snd_RegisterSound(m_clicksound, false);
}

void UIWidget::LayoutVirtualRes(Event *ev)
{
    if (ev->GetInteger(1)) {
        m_bVirtual = true;
        // Added: a design's size
        if (ev->NumArgs() >= 3) {
            m_designSize[0] = ev->GetFloat(2);
            m_designSize[1] = ev->GetFloat(3);
        }
    }
}

/*
================
DesignFrame

Added: a widget of a design-size menu (virtualres 1 W H): its frame the
URC's rect times the scale the design fits the screen with, on whole pixels
================
*/
bool UIWidget::DesignFrame(void)
{
    float s;

    if (m_designSize[0] <= 0 || m_designSize[1] <= 0) {
        return false;
    }
    if (!m_designSet) {
        m_designRect = UIRect2D(
            m_frame.pos.x / m_vVirtualScale[0],
            m_frame.pos.y / m_vVirtualScale[1],
            m_frame.size.width / m_vVirtualScale[0],
            m_frame.size.height / m_vVirtualScale[1]
        );
        m_designSet = true;
    }
    s = UI_DesignScale(m_designSize[0], m_designSize[1]);
    VectorSet2D(m_vVirtualScale, s, s);
    m_frame.pos.x       = floorf(m_designRect.pos.x * s + 0.5f);
    m_frame.pos.y       = floorf(m_designRect.pos.y * s + 0.5f);
    m_frame.size.width  = floorf((m_designRect.pos.x + m_designRect.size.width) * s + 0.5f) - m_frame.pos.x;
    m_frame.size.height = floorf((m_designRect.pos.y + m_designRect.size.height) * s + 0.5f) - m_frame.pos.y;
    return true;
}

UIRect2D UIWidget::DesignRect(void)
{
    DesignFrame();
    return m_baseSet ? m_designBase : m_designRect;
}

void UIWidget::SetDesignRect(const UIRect2D& r)
{
    UIRect2D frame;

    if (!DesignFrame()) {
        return;
    }
    m_designRect = r;
    if (m_baseSet) {
        m_designBase = r;
    }
    // the frame as the design's (DesignFrame), its children's with it
    frame.pos.x       = floorf(r.pos.x * m_vVirtualScale[0] + 0.5f);
    frame.pos.y       = floorf(r.pos.y * m_vVirtualScale[1] + 0.5f);
    frame.size.width  = floorf((r.pos.x + r.size.width) * m_vVirtualScale[0] + 0.5f) - frame.pos.x;
    frame.size.height = floorf((r.pos.y + r.size.height) * m_vVirtualScale[1] + 0.5f) - frame.pos.y;
    setFrame(frame);
    // (the draw cache keeps a menu's list while its own frames stay: drawn again)
    for (UIWidget *w = this; w; w = w->m_parent) {
        w->m_drawValid = false;
    }
}

void UIWidget::LayoutShape(Event *ev)
{
    str line;

    for (int i = 1; i <= ev->NumArgs(); i++) {
        str token = ev->GetString(i);

        if (i > 1) {
            line += " ";
        }
        // (a string with spaces keeps its quotes)
        if (strchr(token.c_str(), ' ')) {
            line += "\"" + token + "\"";
        } else {
            line += token;
        }
    }
    m_shapes.AddObject(line);
    m_shapeStamp   = -1;
    m_shapeDynamic = m_shapeDynamic || strchr(line.c_str(), '$');
}

void UIWidget::LayoutRotateCvar(Event *ev)
{
    m_rotateCvar = ev->GetString(1);
}

void UIWidget::LayoutAlphaCvar(Event *ev)
{
    m_alphaCvar = ev->GetString(1);
}

void UIWidget::LayoutColorCvar(Event *ev)
{
    m_colorCvar = ev->GetString(1);
}

void UIWidget::LayoutWidthCvar(Event *ev)
{
    m_widthCvar = ev->GetString(1);
    m_widthLeft = ev->NumArgs() > 1 && !str::icmp(ev->GetString(2), "left");
}

void UIWidget::LayoutShadow(Event *ev)
{
    m_shadow = true;
}

/*
================
CvarOf

Added: the cvar named by colorcvar (0), alphacvar (1), rotatecvar (2) or
widthcvar (3), found once (a cvar lives as long as the engine)
================
*/
cvar_t *UIWidget::CvarOf(int i, const str& name)
{
    if (!m_cvars[i]) {
        m_cvars[i] = uii.Cvar_Find(name.c_str());
    }
    if (m_cvars[i]) {
        UI_DrawDep(m_cvars[i]);
    } else {
        UI_DrawLive();
    }
    return m_cvars[i];
}

void UIWidget::LayoutAnchor(Event *ev)
{
    m_anchor[0] = ev->GetFloat(1);
    m_anchor[1] = ev->GetFloat(2);
}

enum { RULE_FLOWAFTER, RULE_FLOWBEFORE, RULE_FLOWBELOW, RULE_FITBELOW, RULE_MOVEIF };

void UIWidget::AddRule(int kind, Event *ev, int floats)
{
    uiRule_t r    = {kind, ev->GetString(1), NULL, 0, 0};
    float   *v[2] = {&r.a, &r.b};

    for (int i = 0; i < floats && 2 + i <= ev->NumArgs(); i++) {
        *v[i] = ev->GetFloat(2 + i);
    }
    m_rules.push_back(r);
}

void UIWidget::LayoutFlowAfter(Event *ev) { AddRule(RULE_FLOWAFTER, ev, 1); }
void UIWidget::LayoutFlowBefore(Event *ev) { AddRule(RULE_FLOWBEFORE, ev, 1); }
void UIWidget::LayoutFlowBelow(Event *ev) { AddRule(RULE_FLOWBELOW, ev, 1); }
void UIWidget::LayoutFitBelow(Event *ev) { AddRule(RULE_FITBELOW, ev, 1); }
void UIWidget::LayoutMoveIf(Event *ev) { AddRule(RULE_MOVEIF, ev, 2); }
void UIWidget::LayoutFitWidth(Event *ev) { m_fitWidth = ev->GetFloat(1); }

bool UIWidget::Shown(void)
{
    return isEnabled() && (m_enabledCvar.length() || IsVisible());
}

UIRect2D UIWidget::ContentBox(void)
{
    Flow();
    return m_designRect;
}

void UIWidget::Flow(void)
{
    static int depth; // (rules naming each other: no deeper)
    UIRect2D   r, frame;

    if (!m_designSet || m_rules.empty() || depth > 8 || m_flowing) {
        return;
    }
    depth++;
    m_flowing = true;
    if (!m_baseSet) {
        m_designBase = m_designRect;
        m_baseSet    = true;
    }
    r = m_designBase;
    for (uiRule_t& rule : m_rules) {
        UIRect2D b;

        if (!rule.sibling) {
            rule.sibling = m_parent ? m_parent->FindWidget(rule.name) : NULL;
            if (!rule.sibling || rule.sibling == this) {
                rule.sibling = NULL;
                continue;
            }
        }
        if (rule.kind == RULE_MOVEIF) {
            if (rule.sibling->Shown()) {
                r.pos.x += rule.a;
                r.pos.y += rule.b;
            }
            continue;
        }
        // (a gap after a sibling only when it has something)
        b = rule.sibling->ContentBox();
        switch (rule.kind) {
        case RULE_FLOWAFTER:
            r.pos.x = b.pos.x + b.size.width + (b.size.width > 0.0f ? rule.a : 0.0f);
            break;
        case RULE_FLOWBEFORE:
            r.pos.x = b.pos.x - (b.size.width > 0.0f ? rule.a : 0.0f) - r.size.width;
            break;
        case RULE_FLOWBELOW:
            r.pos.y = b.pos.y + b.size.height + (b.size.height > 0.0f ? rule.a : 0.0f);
            break;
        case RULE_FITBELOW:
            r.size.height = Q_max(0.0f, b.pos.y + b.size.height + rule.a - r.pos.y);
            break;
        }
    }
    depth--;
    m_flowing    = false;
    m_designRect = r;
    // the frame as the design's (DesignFrame)
    frame.pos.x       = floorf(r.pos.x * m_vVirtualScale[0] + 0.5f);
    frame.pos.y       = floorf(r.pos.y * m_vVirtualScale[1] + 0.5f);
    frame.size.width  = floorf((r.pos.x + r.size.width) * m_vVirtualScale[0] + 0.5f) - frame.pos.x;
    frame.size.height = floorf((r.pos.y + r.size.height) * m_vVirtualScale[1] + 0.5f) - frame.pos.y;
    if (frame.pos.x != m_frame.pos.x || frame.pos.y != m_frame.pos.y || frame.size.width != m_frame.size.width
        || frame.size.height != m_frame.size.height) {
        setFrame(frame);
    }
}

/*
================
DrawShapes

Added: the shape's layers baked at the widget's size (made again when a
$cvar in them changes, as a new picture), drawn, turned by rotatecvar
================
*/
void UIWidget::DrawShapes(void)
{
    const int w = (int)(m_frame.size.width + 0.5f), h = (int)(m_frame.size.height + 0.5f);
    // (with the shadow: baked m pixels wider all round, drawn past the box)
    const int m = m_shadow ? UI_ShadowMargin(m_vVirtualScale[0]) : 0;

    BakeShapes();
    if (!m_shapeHandle) {
        return;
    }
    {
        vec4_t color = {1.0f, 1.0f, 1.0f, 1.0f};

        // tinted by colorcvar
        if (m_colorCvar.length() && m_colorCount != -1) {
            Vector4Copy(m_color, color);
        }
        color[3] *= m_local_alpha;
        uii.Rend_SetColor(color);
    }
    if (m) {
        set2D(m);
    }
    if (m_widthCvar.length()) {
        // a fraction of its width, about its middle or from its left
        const float fw = CvarOf(3, m_widthCvar) ? floorf(w * Q_clamp_float(m_cvars[3]->value, 0.0f, 1.0f) + 0.5f) : 0.0f;
        const float x  = m_widthLeft ? 0 : floorf((w - fw) * 0.5f);

        if (fw >= 1.0f && !m) {
            uii.Rend_DrawPicStretched(x, 0, fw, h, 0, 0, 1, 1, m_shapeHandle);
        } else if (fw >= 1.0f) {
            // the shadow's ends as baked, the middle stretched
            const float e = m / (float)(w + 2 * m);

            uii.Rend_DrawPicStretched(x - m, -m, m, h + 2 * m, 0, 0, e, 1, m_shapeHandle);
            uii.Rend_DrawPicStretched(x, -m, fw, h + 2 * m, e, 0, 1 - e, 1, m_shapeHandle);
            uii.Rend_DrawPicStretched(x + fw, -m, m, h + 2 * m, 1 - e, 0, 1, 1, m_shapeHandle);
        }
    } else if (m_rotateCvar.length()) {
        const float a = CvarOf(2, m_rotateCvar) ? DEG2RAD(m_cvars[2]->value) : 0.0f;

        uii.Rend_DrawPicStretched2(-m, -m, w + 2 * m, h + 2 * m, 0, 0, 1, 1, cosf(a), sinf(a), m_shapeHandle);
    } else {
        uii.Rend_DrawPicStretched(-m, -m, w + 2 * m, h + 2 * m, 0, 0, 1, 1, m_shapeHandle);
    }
    if (m) {
        set2D();
    }
}

void UIWidget::BakeShapes(void)
{
    const int   w = (int)(m_frame.size.width + 0.5f), h = (int)(m_frame.size.height + 0.5f);
    // (the shadow: the layers in the box, m pixels in from the picture's edges, the shadow under them)
    const int   m = m_shadow ? UI_ShadowMargin(m_vVirtualScale[0]) : 0;
    const char *layers[33];
    str         text, resolved[33];
    int         i, n = Q_min(m_shapes.NumObjects(), 32);

    // (the same lines, size and pictures: what was baked)
    if (!m_shapeDynamic && m_shapeStamp == UI_BakeStamp() && w == m_shapeW && h == m_shapeH) {
        n = 0;
    }
    for (i = 0; i < n; i++) {
        const char *p = m_shapes.ObjectAt(i + 1).c_str();

        if (m) {
            const float u = m / m_vVirtualScale[0];

            resolved[i] = va("in %gpx %gpx -%gpx -%gpx ", u, u, u, u);
        }
        // $name: the cvar's value
        while (*p) {
            if (*p == '$') {
                char name[MAX_QPATH];
                int  k = 0;

                for (p++; (isalnum((byte)*p) || *p == '_') && k < MAX_QPATH - 1; p++) {
                    name[k++] = *p;
                }
                name[k] = 0;
                cvar_t *v = uii.Cvar_Find(name);

                if (v) {
                    UI_DrawDep(v);
                    resolved[i] += v->latchedString && *v->latchedString ? v->latchedString : v->string;
                } else {
                    UI_DrawLive();
                }
            } else {
                resolved[i] += *p++;
            }
        }
        layers[i] = resolved[i].c_str();
        text += resolved[i];
        text += "\n";
    }
    if (n && m) {
        layers[n++] = "shadow";
    }
    if (n && UI_BakeAhead()) {
        // (made ahead: baked, and kept, when asked for again)
        UI_BakeShape("w", layers, n, w + 2 * m, h + 2 * m, m_vVirtualScale[0]);
    } else if (n && (m_shapeStamp != UI_BakeStamp() || w != m_shapeW || h != m_shapeH || text != m_shapeText)) {
        m_shapeHandle = UI_BakeShape("w", layers, n, w + 2 * m, h + 2 * m, m_vVirtualScale[0]);
        m_shapeStamp  = UI_BakeStamp();
        m_shapeW      = w;
        m_shapeH      = h;
        m_shapeText   = text;
    }
}

void UIWidget::LayoutAlign(Event *ev)
{
    for (int i = 1; i <= ev->NumArgs(); i++) {
        str align = ev->GetString(i);

        if (!stricmp(align, "left")) {
            m_align |= WA_LEFT;
        } else if (!stricmp(align, "right")) {
            m_align |= WA_RIGHT;
        } else if (!stricmp(align, "top")) {
            m_align |= WA_TOP;
        } else if (!stricmp(align, "bottom")) {
            m_align |= WA_BOTTOM;
        } else if (!stricmp(align, "centerx")) {
            m_align |= WA_CENTERX;
        } else if (!stricmp(align, "centery")) {
            m_align |= WA_CENTERY;
        }
    }

    if (!isSubclassOf(UIWidgetContainer)) {
        m_flags |= WF_NOPARENTADJUST | WF_DIRECTED;
    }
}

void UIWidget::LayoutStuffCommand(Event *ev)
{
    LinkCommand(ev->GetString(1));
}

void UIWidget::LayoutLinkCvar(Event *ev)
{
    LinkCvar(ev->GetString(1));
}

void UIWidget::LayoutOrderNumber(Event *ev)
{
    m_ordernum = ev->GetInteger(1);
}

void UIWidget::TextAlignment(Event *ev)
{
    for (int i = 1; i <= ev->NumArgs(); i++) {
        str align = ev->GetString(1);

        if (!stricmp(align, "left")) {
            setFontHorizontalAlignment(FONT_JUSTHORZ_LEFT);
        } else if (!stricmp(align, "right")) {
            setFontHorizontalAlignment(FONT_JUSTHORZ_RIGHT);
        } else if (!stricmp(align, "top")) {
            setFontVerticalAlignment(FONT_JUSTVERT_TOP);
        } else if (!stricmp(align, "bottom")) {
            setFontVerticalAlignment(FONT_JUSTVERT_BOTTOM);
        } else if (!stricmp(align, "centerx")) {
            setFontHorizontalAlignment(FONT_JUSTHORZ_CENTER);
        } else if (!stricmp(align, "centery")) {
            setFontVerticalAlignment(FONT_JUSTVERT_CENTER);
        }
    }
}

void UIWidget::setParent(UIWidget *parent)
{
    if (m_parent) {
        m_parent->removeChild(this);
    }

    m_parent = parent;

    if (m_parent) {
        m_parent->addChild(this);
    }

    PropogateCoordinateSystem();
}

UIWidget *UIWidget::getParent(void)
{
    return m_parent;
}

UIWidget *UIWidget::getFirstChild(void)
{
    if (m_children.NumObjects() > 0) {
        return m_children.ObjectAt(1);
    }

    return NULL;
}

UIWidget *UIWidget::getNextChild(UIWidget *child)
{
    int index = m_children.IndexOfObject(child);

    if (index && m_children.NumObjects() != index) {
        return m_children.ObjectAt(index + 1);
    }

    return NULL;
}

UIWidget *UIWidget::getPrevChild(UIWidget *child)
{
    int index;

    if (child) {
        index = m_children.IndexOfObject(child);
    } else {
        index = m_children.NumObjects();
    }

    if (index > 1) {
        return m_children.ObjectAt(index - 1);
    }

    return NULL;
}

UIWidget *UIWidget::getPrevSibling(void)
{
    if (m_parent) {
        return m_parent->getPrevChild(this);
    }

    return NULL;
}

UIWidget *UIWidget::getLastSibling(void)
{
    if (m_parent && m_children.NumObjects()) {
        UIWidget *wid = m_parent->m_children.ObjectAt(m_parent->m_children.NumObjects());
        if (wid == this) {
            return getPrevSibling();
        } else {
            return wid;
        }
    } else {
        return NULL;
    }
}

void UIWidget::Enable(void)
{
    int n;
    int i;

    setShow(true);
    m_enabled = true;

    n = m_children.NumObjects();
    for (i = 1; i <= n; i++) {
        m_children.ObjectAt(i)->Enable();
    }
}

void UIWidget::Disable(void)
{
    int n;
    int i;

    PostEvent(EV_Widget_Hide, 0);
    m_enabled = false;

    n = m_children.NumObjects();
    for (i = 1; i <= n; i++) {
        m_children.ObjectAt(i)->Disable();
    }

    // Added in OPM
    lastShowTime = -1;
}

bool UIWidget::isEnabled(void)
{
    // (small, to be inlined: every frame's Display asks every widget)
    if (!m_enabledCvar.length() || !m_enabled) {
        return m_enabled;
    }
    return EnabledByCvars();
}

bool UIWidget::EnabledByCvars(void)
{
    // Added: the terms split once (a&b&!c, each must hold; !a, added in
    // 2.30: true if the cvar is 0), each cvar found once
    if (m_numEnabled < 0) {
        const char *p = m_enabledCvar.c_str();

        for (m_numEnabled = 0; *p && m_numEnabled < 8; m_numEnabled++) {
            const char *end = strchr(p, '&');
            const int   len = end ? (int)(end - p) : (int)strlen(p);

            m_enabledNames[m_numEnabled]  = str(p, 0, len);
            m_enabledVars[m_numEnabled]   = NULL;
            p += len + (end ? 1 : 0);
        }
    }
    for (int i = 0; i < m_numEnabled; i++) {
        const char *name = m_enabledNames[i].c_str();
        const bool  neg  = name[0] == '!';
        cvar_t     *cvar = m_enabledVars[i];
        int         value;

        if (!cvar) {
            cvar = m_enabledVars[i] = uii.Cvar_Find(name + neg);
        }
        if (cvar) {
            UI_DrawDep(cvar);
        } else {
            UI_DrawLive();
        }
        value = !cvar ? 0 : cvar->latchedString ? atoi(cvar->latchedString) : cvar->integer;
        if ((value != 0) == neg) {
            return false;
        }
    }
    return true;
}

bool UIWidget::IsDying(void)
{
    return m_dying;
}

UIWidget *UIWidget::FindResponder(const UIPoint2D& pos)
{
    UIWidget *subview;
    UIWidget *responder;
    int       i;

    if (!m_visible || !m_clippedframe.contains(pos)) {
        return NULL;
    }

    for (i = m_children.NumObjects(); i >= 1; i--) {
        subview   = m_children.ObjectAt(i);
        responder = subview->FindResponder(pos);
        if (responder) {
            return responder;
        }
    }

    return this;
}

void UIWidget::setFont(const char *name)
{
    if (m_font) {
        delete m_font;
    }

    m_font = new UIFont(name);
}

void UIWidget::setFontHorizontalAlignment(fonthorzjustify_t alignment)
{
    m_iFontAlignmentHorizontal = alignment;
}

void UIWidget::setFontVerticalAlignment(fontvertjustify_t alignment)
{
    m_iFontAlignmentVertical = alignment;
}

void UIWidget::setShow(bool visible)
{
    m_visible = visible;

    if (!visible) {
        if (IsThisOrChildActive()) {
            uWinMan.DeactivateCurrentSmart();
        }

        // Added in OPM
        lastShowTime = -1;
    }
}

bool UIWidget::getShow(void)
{
    return m_visible;
}

UColor UIWidget::getBackgroundColor(void)
{
    return m_background_color;
}

void UIWidget::setBackgroundColor(const UColor& color, bool setbordercolor)
{
    m_background_color = color;

    if (setbordercolor) {
        setBorderColor(color);
    }
}

void UIWidget::setBorderColor(const UBorderColor& color)
{
    m_border_color = color;
}

UColor UIWidget::getForegroundColor(void)
{
    return m_foreground_color;
}

void UIWidget::setForegroundColor(const UColor& color)
{
    m_foreground_color = color;
}

void UIWidget::setBorderStyle(borderstyle_t style)
{
    m_borderStyle = style;
}

UISize2D UIWidget::getSize(void)
{
    return m_frame.size;
}

void UIWidget::setSize(const UISize2D& newsize)
{
    if (newsize.width != m_frame.size.width || newsize.height != m_frame.size.height) {
        m_frame.size = newsize;
        PropogateCoordinateSystem();
        SendSignal(W_SizeChanged);
    }
}

UIRect2D UIWidget::getFrame(void)
{
    return m_frame;
}

void UIWidget::setFrame(const UIRect2D& newframe)
{
    bool bNewSize;

    bNewSize = newframe.size != m_frame.size;

    m_frame = newframe;

    PropogateCoordinateSystem();
    SendSignal(W_FrameChanged);

    if (bNewSize) {
        SendSignal(W_SizeChanged);
    }
}

void UIWidget::setName(str name)
{
    m_name = name;
}

const char *UIWidget::getName(void)
{
    return m_name;
}

void UIWidget::setTitle(str title)
{
    m_title = title;
}

const char *UIWidget::getTitle(void)
{
    return m_title;
}

motion_t UIWidget::getMotionType(void)
{
    return m_motiontype;
}

void UIWidget::setMotionTime(float time)
{
    m_motiontime = time;
}

float UIWidget::getMotionTime(void)
{
    return m_motiontime;
}

void UIWidget::setMaterial(UIReggedMaterial *mat)
{
    m_material = mat;
}

UIReggedMaterial *UIWidget::getMaterial(void)
{
    return m_material;
}

void UIWidget::setHoverMaterial(UIReggedMaterial *mat)
{
    m_hovermaterial = mat;
}

void UIWidget::setBackgroundAlpha(float f)
{
    m_alpha = f;
}

/*
====================================================================

THE DRAW CACHE

A widget with drawcache, and its children, keep what they drew as a 2D list
(the renderer's): drawn again as one command while what it was drawn from
stays, instead of being drawn widget by widget. That is the cvars read
(UI_DrawDep: the enabled terms, a label's linked cvar, colorcvar, alphacvar,
rotatecvar, widthcvar, a shape's $cvar), its frames, the alpha its parent
gives, DrawKey, the baked images and the screen. A class drawing from
anything else isn't kept (CacheDraw), nor what moves or fades by itself;
their parents draw as before, their other children each from its list. One
list is made at once: the children of one being made are drawn into it (or
their own lists copied in). A handle is used only with the renderer that
gave it (m_drawRef): a vid_restart loads the renderer again, which hands out
the same handles anew: before, a widget that still held one replayed
whatever list had that handle now, another widget's, frozen (the old game
messages, kill rows and chat under the new ones)

====================================================================
*/

static bool UI_SameRect(const UIRect2D& a, const UIRect2D& b)
{
    return a.pos.x == b.pos.x && a.pos.y == b.pos.y && a.size.width == b.size.width && a.size.height == b.size.height;
}

static UIWidget *ui_drawMaking; // the widget whose list is being made
static int       ui_drawScope;  // within a drawcache widget
static int       ui_drawLive;   // live draws so far

void UI_DrawDep(cvar_t *cvar)
{
    if (ui_drawMaking) {
        ui_drawMaking->AddDrawDep(cvar);
    }
}

void UI_DrawLive(void)
{
    ui_drawLive++;
}

static void UI_DrawForget(UIWidget *widget)
{
    if (ui_drawMaking == widget) {
        ui_drawMaking = NULL;
    }
}

// Added: before a frame's UI is drawn, what an error left in mid-draw undone (Com_Error is a longjmp: Display's
// own undoing didn't run): the scope, the widget whose list was being made (not touched: it may be gone; it
// makes its list again, not valid), and the renderer's list (ended as it is)
void UI_DrawReset(void)
{
    ui_drawScope = 0;
    if (ui_drawMaking) {
        ui_drawMaking = NULL;
        uii.Rend_End2DList();
    }
}

int UI_DrawHash(int h, const void *data, size_t size)
{
    const byte *p = (const byte *)data;
    size_t      i = 0;

    // (a word at a time: every kept widget's state, every frame)
    for (; i + 4 <= size; i += 4) {
        int w;

        memcpy(&w, p + i, 4);
        h = (h ^ w) * 16777619;
    }
    for (; i < size; i++) {
        h = (h ^ p[i]) * 16777619;
    }
    return h;
}

void UIWidget::AddDrawDep(cvar_t *cvar)
{
    for (auto& d : m_drawDeps) {
        if (d.first == cvar) {
            return;
        }
    }
    m_drawDeps.push_back({cvar, cvar->modificationCount});
}

void UIWidget::LayoutDrawCache(Event *ev)
{
    m_drawCache = ev->GetInteger(1) != 0;
}

// what of its own state it draws from (shown, alpha, colors, material), and DrawKey
int UIWidget::DrawState(void)
{
    struct {
        bool        visible, enabled;
        float       alpha, colors[8];
        const void *material;
        int         key;
    } st;

    memset(&st, 0, sizeof(st));
    st.visible = m_visible;
    st.enabled = m_enabled;
    st.alpha   = m_alpha;
    for (int i = 0; i < 2; i++) {
        const UColor& c = i ? m_background_color : m_foreground_color;

        st.colors[i * 4 + 0] = c.r;
        st.colors[i * 4 + 1] = c.g;
        st.colors[i * 4 + 2] = c.b;
        st.colors[i * 4 + 3] = c.a;
    }
    st.material = m_material;
    st.key      = DrawKey();
    return UI_DrawHash(0, &st, sizeof(st));
}

bool UIWidget::CacheDraw(void)
{
    return classinfo() == &UIWidget::ClassInfo || classinfo() == &UILabel::ClassInfo || classinfo() == &UIWidgetContainer::ClassInfo;
}

void UIWidget::Display(const UIRect2D& drawframe, float parent_alpha)
{
    int  stamp, live, state;
    bool make;

    // (every widget's every frame, most of them hidden menus: what isn't
    // shown is left as DisplayNow leaves it, before anything of the cache is
    // read, so it costs what it did before the cache)
    if (!ui_drawScope) {
        if (!isEnabled()) {
            lastShowTime = -1;
            return;
        }
        if (!m_enabledCvar.length() && !IsVisible()) {
            return;
        }
    }
    if ((!ui_drawScope && !m_drawCache) || !uii.Rend_Begin2DList) {
        DisplayNow(drawframe, parent_alpha);
        return;
    }
    stamp = UI_BakeStamp() ^ (uid.vidWidth << 3) ^ (uid.vidHeight << 17);
    live  = ui_drawLive;
    ui_drawScope++;
    if (!m_rules.empty()) {
        Flow();
    }
    state = DrawState();
    // a class drawing from more than cvars: its state to the list being made
    if (ui_drawMaking && DrawKeyed()) {
        ui_drawMaking->m_drawKeys.push_back({this, state});
    }
    if (m_drawValid && m_drawRef == uii.GetRefSequence() && m_drawStamp == stamp && m_drawAlpha == parent_alpha
        && UI_SameRect(m_drawRects[0], drawframe)
        && UI_SameRect(m_drawRects[1], m_screenframe) && UI_SameRect(m_drawRects[2], m_clippedframe) && m_drawKey == state
        && CacheDraw()) {
        size_t i, k;

        for (i = 0; i < m_drawDeps.size() && m_drawDeps[i].first->modificationCount == m_drawDeps[i].second; i++) {
        }
        for (k = 0; k < m_drawKeys.size() && m_drawKeys[k].first->CacheDraw() && m_drawKeys[k].first->DrawState() == m_drawKeys[k].second; k++) {
        }
        if (i == m_drawDeps.size() && k == m_drawKeys.size()) {
            // the list again; what it was drawn from to the lists being made
            for (auto& d : m_drawDeps) {
                UI_DrawDep(d.first);
            }
            if (ui_drawMaking) {
                ui_drawMaking->m_drawKeys.insert(ui_drawMaking->m_drawKeys.end(), m_drawKeys.begin(), m_drawKeys.end());
            }
            uii.Rend_Draw2DList(m_drawList);
            ui_drawScope--;
            return;
        }
    }

    // a list made unless one is being made or it drew live last time
    make = !ui_drawMaking && !m_drawLive;
    if (make) {
        // (a handle of an earlier renderer: none, a new one)
        m_drawList = uii.Rend_Begin2DList(m_drawRef == uii.GetRefSequence() ? m_drawList : 0);
        m_drawRef  = uii.GetRefSequence();
        make       = m_drawList != 0;
    }
    if (make) {
        m_drawDeps.clear();
        m_drawKeys.clear();
        // Added: not valid until made (an error in mid-draw ends it before: UI_DrawReset)
        m_drawValid   = false;
        ui_drawMaking = this;
    }
    // (a class drawing from more than its cvars: live while shown)
    if (!CacheDraw() && Shown()) {
        UI_DrawLive();
    }
    DisplayNow(drawframe, parent_alpha);
    if (make) {
        ui_drawMaking = NULL;
        uii.Rend_End2DList();
    }
    m_drawLive  = ui_drawLive != live;
    m_drawValid = make && !m_drawLive;
    m_drawStamp = stamp;
    m_drawAlpha = parent_alpha;
    m_drawRects[0] = drawframe;
    m_drawRects[1] = m_screenframe;
    m_drawRects[2] = m_clippedframe;
    m_drawKey      = state;
    ui_drawScope--;
}

void UIWidget::DisplayNow(const UIRect2D& drawframe, float parent_alpha)
{
    int    i;
    int    n;
    vec4_t col = {1, 1, 1, 1};

    if (!isEnabled()) {
        lastShowTime = -1;
        return;
    }

    if (!m_enabledCvar.length() && !IsVisible()) {
        return;
    }

    if (m_fadetime || m_fadeSequenceState != fadesequence_t::SEQUENCE_NONE) {
        UI_DrawLive();
        Motion();
    }

    // Added: laid out by its rules, colored by a cvar
    if (!m_rules.empty()) {
        Flow();
    }
    if (m_colorCvar.length() && CvarOf(0, m_colorCvar) && m_cvars[0]->modificationCount != m_colorCount) {
        m_color[0] = m_color[1] = m_color[2] = m_color[3] = 1.0f;
        sscanf(m_cvars[0]->string, "%f %f %f %f", &m_color[0], &m_color[1], &m_color[2], &m_color[3]);
        m_colorCount       = m_cvars[0]->modificationCount;
        m_foreground_color = UColor(m_color[0], m_color[1], m_color[2], m_color[3]);
    }
    m_local_alpha = m_alpha * parent_alpha;
    // Added: faded by a cvar, not drawn when clear
    if (m_alphaCvar.length()) {
        m_local_alpha *= CvarOf(1, m_alphaCvar) ? Q_clamp_float(m_cvars[1]->value, 0.0f, 1.0f) : 0.0f;
        if (m_local_alpha <= 0.0f) {
            return;
        }
    }
    set2D();

    if (m_font) {
        m_font->setVirtualScale(getVirtualScale());
    }

    col[3] = m_local_alpha;
    uii.Rend_SetColor(col);

    if (m_background_color.a != 0.0) {
        DrawBox(
            0, 0, m_frame.size.width, m_frame.size.height, m_background_color, m_background_color.a * m_local_alpha
        );
    }

    if (m_borderStyle == border_outline) {
        DrawBoxWithSolidBorder(
            UIRect2D(0, 0, m_frame.size.width, m_frame.size.height),
            UWhite,
            m_border_color.original,
            2,
            2,
            m_local_alpha
        );
    } else if (m_borderStyle) {
        Draw3DBox(
            0, 0, m_frame.size.width, m_frame.size.height, m_borderStyle == border_indent, m_border_color, m_local_alpha
        );
    }

    if (m_material) {
        col[0] = m_foreground_color.r;
        col[1] = m_foreground_color.g;
        col[2] = m_foreground_color.b;
        col[3] = m_foreground_color.a * m_local_alpha;
        uii.Rend_SetColor(col);

        uii.Rend_DrawPicStretched(
            0, 0, m_frame.size.width, m_frame.size.height, 0, 0, 1, 1, m_material->GetMaterial()
        );
    }

    if (m_hovermaterial_active && m_hovermaterial != NULL) {
        uii.Rend_DrawPicStretched(
            0, 0, m_frame.size.width, m_frame.size.height, 0, 0, 1, 1, m_hovermaterial->GetMaterial()
        );
    }

    // Added: its shape
    if (m_shapes.NumObjects()) {
        DrawShapes();
    }

    Draw();

    n = m_children.NumObjects();
    for (i = 1; i <= m_children.NumObjects(); i++) {
        m_children.ObjectAt(i)->Display(m_frame, m_local_alpha);
    }
}

qboolean UIWidget::KeyEvent(int key, unsigned int time)
{
    return qfalse;
}

void UIWidget::CharEvent(int ch) {}

bool UIWidget::CanActivate(void)
{
    if (!m_canactivate || m_dying) {
        return false;
    }

    for (UIWidget *wid = this; wid != NULL; wid = wid->getParent()) {
        if (!wid->getShow()) {
            return false;
        }
    }

    return true;
}

void UIWidget::AllowActivate(bool canactivate)
{
    m_canactivate = canactivate;
}

bool UIWidget::IsActive(void)
{
    return uWinMan.ActiveControl() == this;
}

bool UIWidget::IsVisible(void)
{
    return m_visible;
}

void UIWidget::LinkCvar(str cvarname)
{
    m_cvarname = cvarname;
}

void UIWidget::LinkCommand(str cmd)
{
    m_command = cmd;
}

UIWidget *UIWidget::FindWidget(str name)
{
    int i;
    int n;

    n = m_children.NumObjects();
    for (i = 1; i <= n; i++) {
        UIWidget *wid = m_children.ObjectAt(i);
        if (wid->getName() == name) {
            return wid;
        }

        UIWidget *w = wid->FindWidget(name);
        if (w) {
            return w;
        }
    }

    return NULL;
}

void UIWidget::ResetMotion(motion_t type)
{
    AlignPosition();

    m_motiontype = type;

    if (type == MOTION_IN) {
        setFrame(m_frame);
        FrameInitialized();
    }

    m_starttime = uid.time / 1000.0;
    // Added in 2.0
    m_fadeSequenceLastTime = uid.time / 1000.0;
}

void UIWidget::Realign(void)
{
    bool bScaled = false;

    if (DesignFrame()) {
        // Added: a design-size menu's
        bScaled = true;
    } else if (m_bVirtual) {
        vec2_t vNewVirtualScale;

        SetVirtualScale(vNewVirtualScale);

        scaleFrameVirtualRes(m_frame, m_vVirtualScale, vNewVirtualScale);
        bScaled = true;
    } else if (uid.bHighResScaling) {
        scaleFrameVirtualRes(m_frame, m_vVirtualScale, uid.scaleRes);
        bScaled = true;
    } else {
        vec2_t vNewVirtualScale = {1.0, 1.0};
        bScaled                 = scaleFrameVirtualRes(m_frame, m_vVirtualScale, vNewVirtualScale);
    }

    if (m_align & WA_CENTERX) {
        m_frame.pos.x = uid.vidWidth * 0.5 - m_frame.size.width * 0.5;
    }

    if (m_align & WA_RIGHT) {
        m_frame.pos.x = uid.vidWidth - m_frame.size.width;
    } else if (m_align & WA_LEFT) {
        m_frame.pos.x = 0;
    }

    if (m_align & WA_CENTERY) {
        m_frame.pos.y = uid.vidHeight * 0.5 - m_frame.size.height * 0.5;
    }

    if (m_align & WA_BOTTOM) {
        m_frame.pos.y = uid.vidHeight - m_frame.size.height;
    } else if (m_align & WA_TOP) {
        m_frame.pos.y = 0;
    }

    if ((m_align & WA_FULL) || bScaled) {
        setFrame(m_frame);
    }
}

void UIWidget::BringToFrontPropogated(void)
{
    for (UIWidget *at = this; at->getParent() != NULL; at = at->getParent()) {
        at->BringToFront();
    }
}

UIWidget *UIWidget::IsThisOrChildActive(void)
{
    UIWidget *child = this;

    if (child->IsActive()) {
        return child;
    }

    for (int i = 1; i <= m_children.NumObjects(); i++) {
        child = m_children.ObjectAt(i);
        if (child->IsThisOrChildActive()) {
            return child;
        }
    }

    return NULL;
}

UIPoint2D UIWidget::MouseEventToClientPoint(Event *ev)
{
    UIPoint2D p;

    p.x = ev->GetFloat(1) - m_screenframe.pos.x;
    p.y = ev->GetFloat(2) - m_screenframe.pos.y;

    return p;
}

UIRect2D UIWidget::getClientFrame(void)
{
    // don't return the position
    return UIRect2D(UIPoint2D(0, 0), m_frame.size);
}

void UIWidget::setAlwaysOnBottom(bool b)
{
    if (getAlwaysOnBottom() != b) {
        if (b) {
            m_flags |= WF_ALWAYS_BOTTOM;
        } else {
            m_flags &= ~WF_ALWAYS_BOTTOM;
        }

        if (m_parent) {
            ArrangeWidgetList(m_parent->m_children);
        }
    }
}

bool UIWidget::getAlwaysOnBottom(void)
{
    return (m_flags & WF_ALWAYS_BOTTOM) ? true : false;
}

bool UIWidget::SendSignal(Event& event)
{
    if (uWinMan.IsDead()) {
        return false;
    } else {
        return USignal::SendSignal(event);
    }
}

void UIWidget::SetHovermaterialActive(bool a)
{
    m_hovermaterial_active = a;
}

int UIWidget::getOrderNum(void)
{
    return m_ordernum;
}

void UIWidget::ActivateOrder(void)
{
    UIWidget *selwidget;

    for (selwidget = this; selwidget != NULL; selwidget = selwidget->getParent()) {
        if (selwidget->getParent() == &uWinMan) {
            if (selwidget->isSubclassOf(UIWidgetContainer)) {
                UIWidgetContainer *widcon = (UIWidgetContainer *)selwidget;
                widcon->SetActiveWidgetOrderNum(this);
            }

            return;
        }
    }
}

void UIWidget::EnableEvent(Event *ev)
{
    Enable();
}

void UIWidget::DisableEvent(Event *ev)
{
    Disable();
}

bool UIWidget::PassEventToWidget(str name, Event *ev)
{
    int i;
    int n;

    if (!isEnabled()) {
        return false;
    }

    if (!str::cmp(name, m_name)) {
        ProcessEvent(ev);
        return true;
    }

    n = m_children.NumObjects();
    for (i = 1; i <= n; i++) {
        if (m_children.ObjectAt(i)->PassEventToWidget(name, ev)) {
            return true;
        }
    }

    return false;
}

UIFont *UIWidget::getFont() const
{
    return m_font;
}

const vec2_t& UIWidget::getVirtualScale() const
{
    if (m_bVirtual) {
        return m_vVirtualScale;
    } else {
        return uid.scaleRes;
    }
}

const vec2_t& UIWidget::getHighResScale() const
{
    return uid.scaleRes;
}

void UIWidget::SetEnabledCvar(Event *ev)
{
    m_enabledCvar = ev->GetString(1);
    m_numEnabled  = -1;
}

void UIWidget::SetVirtualScale(vec2_t out)
{
    out[0] = uid.vidWidth / 640.0;
    out[1] = uid.vidHeight / 480.0;
}

void UIWidget::EventFadeSequence(Event *ev)
{
    m_fadeSequenceDelay         = ev->GetFloat(1);
    m_fadeSequenceFadeIn        = ev->GetFloat(2);
    m_fadeSequenceHold          = ev->GetFloat(3);
    m_fadeSequenceFadeOut       = ev->GetFloat(4);
    m_fadeSequenceRemainingTime = m_fadeSequenceDelay;
    m_fadeSequenceState         = fadesequence_t::SEQUENCE_STARTING;
}

CLASS_DECLARATION(UIWidget, UIWidgetContainer, NULL) {
    {&EV_Layout_BGFill,     &UIWidgetContainer::SetBGFill    },
    {&EV_Layout_Fullscreen, &UIWidgetContainer::SetFullscreen},
    {NULL,                  NULL                             }
};

UIWidgetContainer::UIWidgetContainer()
{
    m_bgfill        = UColor(0, 0, 0, 0);
    m_fullscreen    = false;
    m_currentwidnum = 0;
    m_layout        = NULL;
    AllowActivate(false);
}

void UIWidgetContainer::AlignPosition(void)
{
    if (DesignFrame()) {
        // Added: a design-size menu
    } else if (m_bVirtual) {
        vec2_t vNewVirtualScale;

        SetVirtualScale(vNewVirtualScale);

        scaleFrameVirtualRes(m_frame, m_vVirtualScale, vNewVirtualScale);
    } else if (uid.bHighResScaling) {
        scaleFrameVirtualRes(m_frame, m_vVirtualScale, uid.scaleRes);
    } else {
        vec2_t vNewVirtualScale = {1.0, 1.0};
        scaleFrameVirtualRes(m_frame, m_vVirtualScale, vNewVirtualScale);
    }

    if (m_align & WA_LEFT) {
        m_frame.pos.x = 0;
    } else if (m_align & WA_RIGHT) {
        m_frame.pos.x = uid.vidWidth - m_frame.size.width;
    } else {
        // default to center (Added: on a whole pixel)
        m_frame.pos.x = floorf((uid.vidWidth - m_frame.size.width) * 0.5f);
    }

    if (m_align & WA_BOTTOM) {
        m_frame.pos.y = uid.vidHeight - m_frame.size.height;
    } else if (m_align & WA_TOP) {
        m_frame.pos.y = 0;
    } else {
        // default to center (Added: on a whole pixel)
        m_frame.pos.y = floorf((uid.vidHeight - m_frame.size.height) * 0.5f);
    }

    // Added: its middle at fractions of the screen
    if (m_anchor[0] >= 0) {
        m_frame.pos.x = floorf(uid.vidWidth * m_anchor[0] - m_frame.size.width * 0.5f + 0.5f);
        m_frame.pos.y = floorf(uid.vidHeight * m_anchor[1] - m_frame.size.height * 0.5f + 0.5f);
    }

    setFrame(m_frame);
}

void UIWidgetContainer::Draw(void)
{
    uii.Rend_Set2D(0, 0, uid.vidWidth, uid.vidHeight, 0, uid.vidWidth, uid.vidHeight, 0, -1.0, 1.0);
    uii.Rend_Scissor(0, 0, uid.vidWidth, uid.vidHeight);

    if (m_bgfill.a != 0.0) {
        DrawBox(0, 0, uid.vidWidth, uid.vidHeight, m_bgfill, m_local_alpha);
    }

    set2D();
}

void UIWidgetContainer::Realign(void)
{
    AlignPosition();
}

void UIWidgetContainer::SetBGFill(Event *ev)
{
    m_bgfill.r = ev->GetFloat(1);
    m_bgfill.b = ev->GetFloat(2);
    m_bgfill.g = ev->GetFloat(3);
    m_bgfill.a = ev->GetFloat(4);
}

void UIWidgetContainer::SetFullscreen(Event *ev)
{
    m_fullscreen = ev->GetBoolean(1);
}

qboolean UIWidgetContainer::isFullscreen(void)
{
    return m_fullscreen;
}

void UIWidgetContainer::SetActiveWidgetOrderNum(UIWidget *wid)
{
    int i;

    for (i = 1; i <= m_children.NumObjects(); i++) {
        if (wid == m_children.ObjectAt(i)) {
            m_currentwidnum = wid->getOrderNum();
        }
    }
}

void UIWidgetContainer::SetActiveWidgetOrderNum(int num)
{
    UIWidget *wid;
    int       i;

    for (i = 1; i <= m_children.NumObjects(); i++) {
        wid = m_children.ObjectAt(i);

        if (wid->getOrderNum() == num) {
            uWinMan.ActivateControl(wid);
        } else {
            wid->SetHovermaterialActive(false);
        }
    }
}

void UIWidgetContainer::SetLastActiveWidgetOrderNum(void)
{
    if (!m_currentwidnum) {
        m_currentwidnum = 1;
    }

    SetActiveWidgetOrderNum(m_currentwidnum);
}
