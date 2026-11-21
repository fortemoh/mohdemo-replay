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
#include "uibake.h"

UIFont::UIFont()
{
    m_font = uii.Rend_LoadFont("verdana-14");
    if (!m_font) {
        uii.Sys_Error(ERR_DROP, "Couldn't load font Verdana\n");
    }

    color = UBlack;
    setColor(color);

    refHandle  = UI_FontStamp();
    name       = "verdana-14";
    widthRatio = 1.0f;
    m_fit      = 1.0f;
    m_hasVirtualScale = false;
}

UIFont::UIFont(const char *fn)
{
    setFont(fn);
    widthRatio = 1.0f;
    m_fit      = 1.0f;
    m_hasVirtualScale = false;
}

void UIFont::setVirtualScale(const float *virtualScale)
{
    const float page = UI_BakedFontScale(m_font);

    m_virtualScale[0] = virtualScale[0];
    m_virtualScale[1] = virtualScale[1];
    m_hasVirtualScale = true;

    // the renderer scales glyphs across no more than down: on a wide screen,
    // text is narrower than its 640x480 layout width
    widthRatio = virtualScale[0] > virtualScale[1] ? virtualScale[1] / virtualScale[0] : 1.0f;
    // Added: a baked face is drawn across at its page's scale (Print)
    if (page > 0.0f) {
        widthRatio = page / virtualScale[0];
    }
}

void UIFont::setColor(UColor col)
{
    color = col;
}

void UIFont::setAlpha(float alpha)
{
    color.a = alpha;
}

void UIFont::setFont(const char *fontname)
{
    // Added: a TrueType face baked at the size drawn (uibake.cpp), else a RitualFont
    m_font = UI_BakeFont(fontname);
    if (!m_font) {
        m_font = uii.Rend_LoadFont(fontname);
    }
    if (!m_font) {
        uii.Sys_Error(ERR_DROP, "Couldn't load font %s\n", fontname);
    }

    refHandle = UI_FontStamp();
    name      = fontname;
}

void UIFont::Print(float x, float y, const char *text, size_t maxlen, const float *virtualScreen)
{
    CheckRefreshFont();

    if (!m_font) {
        return;
    }

    uii.Rend_SetColor(color);
    // Added: a baked face across at its page's scale, a texel a pixel (its
    // start where the virtual screen puts it; the renderer rounds it)
    if (virtualScreen && UI_BakedFontScale(m_font) > 0.0f) {
        const float page  = UI_BakedFontScale(m_font) * m_fit;
        const float vs[2] = {page, virtualScreen[1] * m_fit};

        uii.Rend_DrawString(m_font, text, x * virtualScreen[0] / page, y / m_fit, maxlen, vs);
        return;
    }
    if (virtualScreen && m_fit != 1.0f) {
        const float vs[2] = {virtualScreen[0] * m_fit, virtualScreen[1] * m_fit};

        uii.Rend_DrawString(m_font, text, x / m_fit, y / m_fit, maxlen, vs);
        return;
    }
    // Round to integer to avoid blurry text
    uii.Rend_DrawString(m_font, text, (int)x, (int)y, maxlen, virtualScreen);
}

void UIFont::PrintJustified(
    const UIRect2D& rect, fonthorzjustify_t horz, fontvertjustify_t vert, const char *text, const float *vVirtualScale
)
{
    float       newx, newy;
    float       textwidth;
    int         textheight;
    UIRect2D    sizedRect;
    const char *source;
    char       *dest;
    char        string[2048];

    if (vVirtualScale) {
        sizedRect.pos.x       = rect.pos.x / vVirtualScale[0];
        sizedRect.pos.y       = rect.pos.y / vVirtualScale[1];
        sizedRect.size.width  = rect.size.width / vVirtualScale[0];
        sizedRect.size.height = rect.size.height / vVirtualScale[1];
    } else {
        sizedRect = rect;
    }

    if (horz == FONT_JUSTHORZ_LEFT && vert == FONT_JUSTVERT_TOP) {
        // no need to justify
        Print(sizedRect.pos.x, sizedRect.pos.y, text, -1, vVirtualScale);
        return;
    }

    textheight = getHeight(text, -1);

    switch (vert) {
    case FONT_JUSTVERT_TOP:
        newy = sizedRect.pos.y;
        break;
    case FONT_JUSTVERT_CENTER:
        newy = sizedRect.pos.y + (sizedRect.size.height - textheight) * 0.5;
        break;
    case FONT_JUSTVERT_BOTTOM:
        newy = sizedRect.pos.y + sizedRect.size.height - textheight;
        break;
    default:
        newy = 0;
        break;
    }

    source = text;
    while (*source) {
        // skip new lines
        while (*source == '\n') {
            source++;
        }

        if (!*source) {
            // don't print an empty string
            return;
        }

        dest = string;

        do {
            *dest++ = *source++;
        } while (*source && *source != '\n');

        *dest = 0;

        switch (horz) {
        case FONT_JUSTHORZ_CENTER:
            textwidth = getWidth(string, -1);
            newx      = sizedRect.pos.x + (sizedRect.size.width - textwidth) * 0.5;
            break;
        case FONT_JUSTHORZ_LEFT:
            newx = sizedRect.pos.x;
            break;
        case FONT_JUSTHORZ_RIGHT:
            textwidth = getWidth(string, -1);
            newx      = sizedRect.pos.x + sizedRect.size.width - textwidth;
            break;
        default:
            newx = 0.0;
            break;
        }

        Print(newx, newy, string, -1, vVirtualScale);

        // expand for newline
        newy += getHeight(" ", -1);
    }
}

float UIFont::FitScale(const char *text, float width)
{
    float w;

    m_fit = 1.0f;
    if (width <= 0.0f || !*text) {
        return 1.0f;
    }
    w = getWidth(text, -1);
    return w > width ? Q_max(0.5f, width / w) : 1.0f;
}

float UIFont::getWidth(const char *text, int maxlen)
{
    CheckRefreshFont();

    if (!m_font) {
        return 0;
    }

    return UI_FontStringWidth(m_font, text, maxlen) * widthRatio * m_fit;
}

float UIFont::getCharWidth(unsigned char ch)
{
    int   indirected;
    float widthMul = 1.0f; // Added in OPM for easier readability

    CheckRefreshFont();

    if (!m_font) {
        return 4;
    }

    if (ch == '\t') {
        ch       = ' ';
        widthMul = 3.0f;
    }

    indirected = m_font->sgl[0]->indirection[ch];
    if (indirected < 0 || indirected > 255) {
        return 0;
    }

    return m_font->sgl[0]->locations[indirected].size[0] * 256.0 * widthMul * widthRatio * m_fit;
}

int UIFont::getHeight(const char *text, int maxlen, const float *virtualScale)
{
    float height;
    int   i;

    CheckRefreshFont();

    if (!m_font) {
        return 0;
    }

    height = getHeight(virtualScale);

    for (i = 0; text[i]; i++) {
        if (maxlen != -1 && i > maxlen) {
            break;
        }

        if (text[i] == '\n') {
            height += getHeight(virtualScale);
        }
    }

    return height;
}

int UIFont::getHeight(const float *virtualScale)
{
    CheckRefreshFont();

    if (virtualScale) {
        if (m_font) {
            return (m_font->sgl[0]->height * virtualScale[0] * m_fit);
        } else {
            return (16.0 * virtualScale[1] * m_fit);
        }
    } else {
        if (m_font) {
            return m_font->sgl[0]->height * m_fit;
        } else {
            return 16 * m_fit;
        }
    }
}

float UI_FontgetCharWidthf(fontheader_t *font, unsigned short uch)
{
    int   indirected;
    float widthMul = 1.0f; // Added in OPM for easier readability

    if (!font) {
        return 0.f;
    }

    if (uch == '\t') {
        uch      = ' ';
        widthMul = 3.0f;
    }

    indirected = font->sgl[0]->indirection[uch];
    if (indirected < 0 || indirected > 255) {
        return 0.f;
    }

    return font->sgl[0]->locations[indirected].size[0] * widthMul;
}

int UI_FontStringWidth(fontheader_t *pFont, const char *pszString, int iMaxLen)
{
    float widths    = 0.0;
    float maxwidths = 0.0;

    if (!pFont) {
        return 0;
    }

    for (int i = 0; pszString[i] && (iMaxLen == -1 || i < iMaxLen); i++) {
        unsigned short uch = (unsigned char)pszString[i];

        if (uch == '\n') {
            widths = 0.0;
        } else {
            widths += UI_FontgetCharWidthf(pFont, uch);

            if (maxwidths < widths) {
                maxwidths = widths;
            }
        }
    }

    return maxwidths * 256.0;
}

void UIFont::CheckRefreshFont()
{
    if (refHandle != UI_FontStamp()) {
        setFont(name);
        // Added: a baked face found anew may have another page scale (baked at
        // the screen's new size): the width ratio made again for it now, so
        // what this frame measures (and a 2D list keeps) has the new one
        if (m_hasVirtualScale) {
            setVirtualScale(m_virtualScale);
        }
    } else if (!uii.IsRendererLoaded()) {
        m_font = NULL;
    }
}