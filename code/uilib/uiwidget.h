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

#include <vector>

#include "usignal.h"
#include "../qcommon/q_shared.h"
#include "../corepp/str.h"
#include "ui_public.h"
#include "uirect2d.h"
#include "ucolor.h"
#include "uifont.h"

class UIFont;

class UIReggedMaterial
{
    uihandle_t hMat;
    str        name;
    bool       isSet;
    bool       isGot;

public:
    UIReggedMaterial();

    uihandle_t GetMaterial(void);
    void       ReregisterMaterial(void);
    void       CleanupMaterial(void);
    void       SetMaterial(const str& name);
    str        GetName(void);
};

// widget flags
#define WF_DIRECTED           (1 << 2)
#define WF_ALWAYS_BOTTOM      (1 << 4)
#define WF_NOPARENTADJUST     (1 << 6)

// widget alignment flags
#define WA_LEFT    (1 << 0)
#define WA_RIGHT   (1 << 1)
#define WA_TOP     (1 << 3)
#define WA_BOTTOM  (1 << 4)
#define WA_CENTERX (1 << 5)
#define WA_CENTERY (1 << 6)
#define WA_FULL    (WA_LEFT | WA_RIGHT | WA_TOP | WA_BOTTOM | WA_CENTERX | WA_CENTERY)

typedef class UIReggedMaterial UIReggedMaterial;

typedef enum {
    border_none,
    border_3D,
    border_indent,
    border_outline
} borderstyle_t;

typedef enum {
    MOTION_IN,
    MOTION_OUT
} motion_t;

typedef enum {
    SEQUENCE_NONE,
    SEQUENCE_STARTING,
    SEQUENCE_FADEIN,
    SEQUENCE_HOLD,
    SEQUENCE_FADEOUT
} fadesequence_t;

// Added: the draw cache (drawcache): a cvar a widget drew from, something
// drawn that changes by itself (not to be kept)
void UI_DrawDep(cvar_t *cvar);
void UI_DrawLive(void);
void UI_DrawReset(void);
// ... a hash of what a widget draws from (DrawKey), h the hash so far
int UI_DrawHash(int h, const void *data, size_t size);

class UIWidget : public USignal
{
    friend class Menu;
    friend class UILayout;
    friend class UIWindowManager;

protected:
    UIRect2D              m_frame;
    vec2_t                m_vVirtualScale;
    UIPoint2D             m_origin;
    UIRect2D              m_screenframe;
    UIPoint2D             m_screenorigin;
    UIRect2D              m_clippedframe;
    UIPoint2D             m_clippedorigin;
    UColor                m_background_color;
    UColor                m_foreground_color;
    UBorderColor          m_border_color;
    borderstyle_t         m_borderStyle;
    bool                  m_visible;
    bool                  m_enabled;
    // Added: drawcache (the draw cache, below): beside what every frame's
    // Display reads of every widget, shown or not
    bool                  m_drawCache;
    UIFont               *m_font;
    class UIWidget       *m_parent;
    Container<UIWidget *> m_children;
    str                   m_name;
    bool                  m_canactivate;
    str                   m_cvarname;
    str                   m_cvarvalue;
    str                   m_command;
    str                   m_title;
    float                 m_indent;
    float                 m_bottomindent;
    UIReggedMaterial     *m_material;
    UIReggedMaterial     *m_hovermaterial;
    bool                  m_hovermaterial_active;
    motion_t              m_motiontype;
    float                 m_starttime;
    float                 m_fadetime;
    float                 m_alpha;
    float                 m_local_alpha;
    float                 m_motiontime;
    float                 m_fadeSequenceDelay;
    float                 m_fadeSequenceFadeIn;
    float                 m_fadeSequenceHold;
    float                 m_fadeSequenceFadeOut;
    float                 m_fadeSequenceRemainingTime;
    float                 m_fadeSequenceLastTime;
    int                   m_fadeSequenceState;
    str                   m_clicksound;
    int                   m_align;
    int                   m_flags;
    bool                  m_dying;
    int                   m_ordernum;
    fonthorzjustify_t     m_iFontAlignmentHorizontal;
    fontvertjustify_t     m_iFontAlignmentVertical;
    qboolean              m_bVirtual;
    str                   m_enabledCvar;
    // Added: enabledcvar's terms (a&b&!c), their cvars found once
    str                   m_enabledNames[8];
    cvar_t               *m_enabledVars[8];
    int                   m_numEnabled; // -1: not split yet
    //
    // Added in OPM
    //
    float lastShowTime;

    //
    // Added: a design-size menu's widgets (virtualres 1 W H) and baked shapes
    //
    vec2_t         m_designSize; // 0 0: none
    UIRect2D       m_designRect; // the rect as the URC gave it
    bool           m_designSet;
    Container<str> m_shapes;     // the "shape" lines, a $cvar in one its value
    str            m_rotateCvar; // the shapes turned by its degrees, clockwise
    str            m_alphaCvar;  // the widget faded by its value
    str            m_colorCvar;  // the text's and shape's color: the cvar's "r g b a"
    cvar_t        *m_cvars[4];   // colorcvar's, alphacvar's, rotatecvar's, widthcvar's, found once
    int            m_colorCount; // colorcvar's modificationCount read
    float          m_color[4];
    str            m_widthCvar;  // the shape drawn this fraction wide
    bool           m_widthLeft;  // ... from its left, else about its middle
    bool           m_shadow;     // the shapes with the faces' drop shadow under them (shadow)
    vec2_t         m_anchor;     // a menu's middle at these fractions of the screen (-1: its align)
    // the layout rules (flowafter flowbefore flowbelow fitbelow moveif):
    // applied in order to the URC's rect each frame, from
    // the siblings' boxes as they are then (Flow)
    struct uiRule_t {
        int       kind;
        str       name;
        UIWidget *sibling;
        float     a, b;
    };
    std::vector<uiRule_t> m_rules;
    UIRect2D       m_designBase; // the URC's rect
    bool           m_baseSet;
    bool           m_flowing;    // in its Flow (its own text measured: as it is)
    float          m_fitWidth;   // a longer text drawn smaller to fit (fitwidth), 0 none
    bool           m_shapeDynamic; // a $cvar in its lines
    str            m_shapeText;  // the lines as last baked
    int            m_shapeStamp;
    int            m_shapeW, m_shapeH;
    qhandle_t      m_shapeHandle;
    // the draw cache (drawcache): what it and its children drew, a 2D list,
    // drawn again while what it was drawn from stays: the cvars read
    // (UI_DrawDep), its frames, the alpha given, DrawKey
    int                                  m_drawList;
    bool                                 m_drawValid, m_drawLive;
    int                                  m_drawStamp, m_drawKey;
    float                                m_drawAlpha;
    UIRect2D                             m_drawRects[3];
    std::vector<std::pair<cvar_t *, int>> m_drawDeps;
    std::vector<std::pair<UIWidget *, int>> m_drawKeys; // its children drawing from more (DrawKey), their states

public:
    CLASS_PROTOTYPE(UIWidget);

protected:
    bool         addChild(UIWidget *widget);
    bool         removeChild(UIWidget *widget);
    void         PropogateCoordinateSystem(void);
    void         set2D(float margin = 0.0f);
    virtual void Draw(void);
    virtual void FrameInitialized(void);
    void         DrawTitle(float x, float y);
    void         Motion(void);
    virtual void AlignPosition(void);
    void         Hide(Event *ev);
    void         Activate(Event *ev);
    void         BringToFront(void);
    static void ArrangeWidgetList(Container<UIWidget *>& list);

public:
    UIWidget();
    virtual ~UIWidget();

    virtual void Shutdown(void);
    virtual void InitFrame(
        UIWidget   *parentview,
        float       x,
        float       y,
        float       width,
        float       height,
        int         border   = -1,
        const char *fontname = "verdana-12"
    );
    void InitFrame(UIWidget *parentview, const UIRect2D& r, int border, const char *fontname = "verdana-12");
    void LayoutRect(Event *ev);
    void LayoutName(Event *ev);
    void LayoutTitle(Event *ev);
    void LayoutFont(Event *ev);
    void LayoutBgColor(Event *ev);
    void LayoutFgColor(Event *ev);
    void LayoutBorderStyle(Event *ev);
    void LayoutShader(Event *ev);
    void LayoutHoverShader(Event *ev);
    void LayoutFadeIn(Event *ev);
    void LayoutClickSound(Event *ev);
    void LayoutVirtualRes(Event *ev);
    void LayoutAlign(Event *ev);
    void LayoutStuffCommand(Event *ev);
    void LayoutLinkCvar(Event *ev);
    void LayoutOrderNumber(Event *ev);
    void TextAlignment(Event *ev);
    void SetEnabledCvar(Event *ev);
    // Added
    void LayoutShape(Event *ev);
    void LayoutRotateCvar(Event *ev);
    void LayoutAlphaCvar(Event *ev);
    void LayoutColorCvar(Event *ev);
    void LayoutWidthCvar(Event *ev);
    void LayoutShadow(Event *ev);
    void LayoutFlowAfter(Event *ev);
    void LayoutAnchor(Event *ev);
    void Flow(void);
    virtual UIRect2D ContentBox(void); // its text's box (a label's), else its rect: design units
    bool             Shown(void);      // enabled and visible
    void             AddRule(int kind, Event *ev, int floats);
    void             LayoutFlowBefore(Event *ev);
    void             LayoutFlowBelow(Event *ev);
    void             LayoutFitBelow(Event *ev);
    void             LayoutMoveIf(Event *ev);
    void             LayoutFitWidth(Event *ev);
    cvar_t *CvarOf(int i, const str& name);
    bool DesignFrame(void);
    // a design-size menu's widget: its rect in design units (the URC's, or as set), and set anew while the game runs
    // (its menu drawn again)
    UIRect2D DesignRect(void);
    void     SetDesignRect(const UIRect2D& r);
    void DrawShapes(void);
    void BakeShapes(void); // its shape baked at its size now (a load), not at its first draw
    Container<str>& getShapes(void) { return m_shapes; }

    // Added in 2.0
    //====
    void EventFadeSequence(Event *ev);
    //====

    void            SetVirtualScale(vec2_t out);
    void            setParent(UIWidget *parent);
    class UIWidget *getParent(void);
    class UIWidget *getFirstChild(void);
    class UIWidget *getNextChild(UIWidget *child);
    class UIWidget *getPrevChild(UIWidget *child);
    class UIWidget *getPrevSibling(void);
    class UIWidget *getLastSibling(void);

    void               Enable(void);
    void               Disable(void);
    bool               isEnabled(void);
    bool               EnabledByCvars(void); // Added: isEnabled's enabledcvar terms
    bool               IsDying(void);
    class UIWidget    *FindResponder(const UIPoint2D& pos);
    void               setFont(const char *name);
    void               setFontHorizontalAlignment(fonthorzjustify_t alignment);
    void               setFontVerticalAlignment(fontvertjustify_t alignment);
    void               setShow(bool visible);
    bool               getShow(void);
    class UColor       getBackgroundColor(void);
    virtual void       setBackgroundColor(const UColor& color, bool setbordercolor);
    void               setBorderColor(const UBorderColor& color);
    class UColor       getForegroundColor(void);
    virtual void       setForegroundColor(const UColor& color);
    void               setBorderStyle(borderstyle_t style);
    class UISize2D     getSize(void);
    void               setSize(const UISize2D& newsize);
    class UIRect2D     getFrame(void);
    void               setFrame(const UIRect2D& newframe);
    void               setName(str name);
    const char        *getName(void);
    void               setTitle(str title);
    const char        *getTitle(void);
    motion_t           getMotionType(void);
    void               setMotionTime(float time);
    float              getMotionTime(void);
    void               setMaterial(UIReggedMaterial *mat);
    UIReggedMaterial  *getMaterial(void);
    void               setHoverMaterial(UIReggedMaterial *mat);
    virtual void       setBackgroundAlpha(float f);
    void               Display(const UIRect2D& drawframe, float parent_alpha);
    void               DisplayNow(const UIRect2D& drawframe, float parent_alpha);
    // Added: the draw cache: whether it draws only from its rect, colors,
    // shapes and the cvars it reads (UI_DrawDep), and what else it draws from
    virtual bool       CacheDraw(void);
    virtual int        DrawKey(void) { return 0; }
    virtual bool       DrawKeyed(void) { return false; } // DrawKey or CacheDraw change with more than cvars
    int                DrawState(void);
    void               AddDrawDep(cvar_t *cvar);
    void               LayoutDrawCache(Event *ev);
    virtual qboolean   KeyEvent(int key, unsigned int time);
    virtual void       CharEvent(int ch);
    bool               CanActivate(void);
    void               AllowActivate(bool canactivate);
    bool               IsActive(void);
    bool               IsVisible(void);
    virtual void       LinkCvar(str cvarname);
    void               LinkCommand(str cmd);
    class UIWidget    *FindWidget(str name);
    void               ResetMotion(motion_t type);
    virtual void       Realign(void);
    void               BringToFrontPropogated(void);
    class UIWidget    *IsThisOrChildActive(void);
    class UIPoint2D    MouseEventToClientPoint(Event *ev);
    class UIRect2D     getClientFrame(void);
    void               setAlwaysOnBottom(bool b);
    bool               getAlwaysOnBottom(void);
    bool               SendSignal(Event& event);
    void               SetHovermaterialActive(bool a);
    int                getOrderNum(void);
    void               ActivateOrder(void);
    void               EnableEvent(Event *ev);
    void               DisableEvent(Event *ev);
    bool               PassEventToWidget(str name, Event *ev);

    // Added in OPM
    UIFont       *getFont() const;
    const vec2_t& getVirtualScale() const;
    const vec2_t& getHighResScale() const;
};

class UIWidgetContainer : public UIWidget
{
    UColor   m_bgfill;
    qboolean m_fullscreen;
    int      m_currentwidnum;

public:
    class UILayout *m_layout;

    CLASS_PROTOTYPE(UIWidgetContainer);

private:
    void AlignPosition(void) override;
    void Draw(void) override;

public:
    UIWidgetContainer();

    void      Realign(void) override;
    void      SetBGFill(Event *ev);
    void      SetFullscreen(Event *ev);
    qboolean  isFullscreen(void);
    void      SetActiveWidgetOrderNum(UIWidget *wid);
    void      SetActiveWidgetOrderNum(int num);
    void      SetLastActiveWidgetOrderNum(void);
};

extern Event W_Destroyed;
extern Event W_SizeChanged;
extern Event W_FrameChanged;
extern Event W_OriginChanged;
extern Event W_Activated;
extern Event EV_Widget_Activate;
extern Event W_Deactivated;
extern Event EV_Widget_Hide;
extern Event EV_Widget_Enable;
extern Event EV_Widget_Disable;
extern Event W_RealignWidget;
extern Event W_Draw;
extern Event EV_Layout_Rect;
extern Event EV_Layout_Name;
extern Event EV_Layout_Title;
extern Event EV_Layout_Font;
extern Event EV_Layout_BGColor;
extern Event EV_Layout_FGColor;
extern Event EV_Layout_Borderstyle;
extern Event EV_Layout_AlphaCvar; // Added
extern Event EV_Layout_Shader;
extern Event EV_Layout_HoverShader;
extern Event EV_Layout_FadeIn;
extern Event EV_Layout_ClickSound;
extern Event EV_Layout_VirtualRes;
extern Event EV_Layout_Align;
extern Event EV_Layout_StuffCommand;
extern Event EV_Layout_LinkCvar;
extern Event EV_Widget_OrderNumber;
extern Event EV_Widget_TextAlignment;
extern Event EV_Widget_EnabledCvar;
extern Event EV_Layout_BGFill;
extern Event EV_Layout_Fullscreen;

void SetColor(const UColor& color, float alpha);
void DrawBox(const UIRect2D& rect, const UColor& color, float alpha);
void Draw3DBox(const UIRect2D& rect, bool indent, const UBorderColor& color, float alpha);
void DrawBox(float x, float y, float width, float height, const UColor& color, float alpha);
void Draw3DBox(float x, float y, float width, float height, bool indent, const UBorderColor& color, float alpha);
void DrawMac3DBox(const UIRect2D& rect, bool indent, const UBorderColor& color, int inset, float alpha);
void DrawBoxWithSolidBorder(
    const UIRect2D& rect, const UColor& inside, const UColor& outside, int size, int flags, float alpha
);
