/*
===========================================================================
Copyright (C) 2026 the OpenMoHAA team

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

// cl_crosshair.c : crosshair_import and crosshair_export, CS2's crosshair share codes for the cgame's crosshair
// cvars (CG_UpdateCrosshair). Here and not in the cgame for the clipboard.
//
// The format CS2 writes since its 30 September 2026 update ("cs2-v1"), as described by akiver/csgo-sharecode
// (MIT, doc/code_layout.md); written from that description. "CS" and 44 characters of base 57, the least
// significant first, for a number read as 32 bytes, byte 0 the most significant; multi-byte fields little-endian:
//   0 checksum (bytes 1-31 added up), 1 version (1), 2-3 the screen height the sizes are pixels at,
//   4 style (bits 0-4, 6 Dot Only), follow recoil (5), center dot (6), T style (7), 5-8 RGBA, 9-12 the outline's,
//   13 thickness (bits 0-5), outline mode (6-7), 14-15 gap (signed), 16 length,
//   17-22 what MOHAA has no setting for (dynamic spread, splits, scope dot), 23-31 unused.
// MOHAA's client gets no weapon spread: every style but Dot Only draws as a static cross.

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

#define XC_BYTES     32
#define XC_CHARS     44
#define XC_DOT_ONLY  6
#define XC_STATIC    4 // Static Cross
#define XC_MINHEIGHT 240 // CS2 raises a lower screen height to it

static const char xcDigits[] = "ABCDEFGHJKLMNOPQRSTUVWXYZabcdefhijkmnopqrstuvwxyz23456789";

enum {
    XC_LENGTH,
    XC_GAP,
    XC_THICKNESS,
    XC_COLOR,
    XC_ALPHA,
    XC_OUTLINE,
    XC_OUTLINE_COLOR,
    XC_OUTLINE_ALPHA,
    XC_DOT,
    XC_T,
    XC_CVARS
};

static const char *const xcCvars[XC_CVARS] = {
    "cg_crosshair_length",
    "cg_crosshair_gap",
    "cg_crosshair_thickness",
    "cg_crosshair_color",
    "cg_crosshair_alpha",
    "cg_crosshair_outline",
    "cg_crosshair_outline_color",
    "cg_crosshair_outline_alpha",
    "cg_crosshair_dot",
    "cg_crosshair_t",
};

static int CL_XcChecksum(const byte *b)
{
    int i, sum = 0;

    for (i = 1; i < XC_BYTES; i++) {
        sum += b[i];
    }
    return sum & 0xFF;
}

static int CL_XcHeight(const byte *b)
{
    return b[2] | b[3] << 8;
}

// the code's bytes, or why it isn't one CS2 writes now
static const char *CL_XcDecode(const char *code, byte *b)
{
    int i, j, carry;

    if (!Q_strncmp(code, "CSGO", 4)) {
        return "an old CSGO- code: CS2 no longer takes those either, copy the crosshair's new code from CS2";
    }
    if (Q_strncmp(code, "CS", 2) || strlen(code) != 2 + XC_CHARS) {
        return "not a crosshair code (CS and 44 letters and digits)";
    }
    memset(b, 0, XC_BYTES);
    for (i = XC_CHARS - 1; i >= 0; i--) {
        const char *digit = strchr(xcDigits, code[2 + i]);

        if (!digit) {
            return "not a crosshair code (a character CS2 never writes: a typo?)";
        }
        // b = b * 57 + digit
        carry = digit - xcDigits;
        for (j = XC_BYTES - 1; j >= 0; j--) {
            carry += b[j] * 57;
            b[j] = carry & 0xFF;
            carry >>= 8;
        }
        if (carry) {
            return "not a crosshair code (a typo?)";
        }
    }
    if (b[0] != CL_XcChecksum(b)) {
        return "the code is garbled (its checksum doesn't match: a typo?)";
    }
    if (b[1] != 1) {
        return va("a crosshair code of a newer kind (version %d) than this game reads", b[1]);
    }
    if (!CL_XcHeight(b)) {
        return "the code has no screen height";
    }
    return NULL;
}

static void CL_XcEncode(const byte *bytes, char *code)
{
    byte b[XC_BYTES];
    int  i, j, rest;

    memcpy(b, bytes, XC_BYTES);
    b[0]    = CL_XcChecksum(b);
    code[0] = 'C';
    code[1] = 'S';
    for (i = 0; i < XC_CHARS; i++) {
        // b /= 57, the remainder is the next character
        rest = 0;
        for (j = 0; j < XC_BYTES; j++) {
            rest = rest << 8 | b[j];
            b[j] = rest / 57;
            rest %= 57;
        }
        code[2 + i] = xcDigits[rest];
    }
    code[2 + XC_CHARS] = 0;
}

// the cvars an import of the bytes sets: sizes in pixels at 1080 lines
static void CL_XcCvars(const byte *b, char values[XC_CVARS][MAX_QPATH])
{
    float scale   = 1080.0f / Q_max(CL_XcHeight(b), XC_MINHEIGHT);
    int   dotOnly = (b[4] & 31) == XC_DOT_ONLY;

    Com_sprintf(values[XC_LENGTH], MAX_QPATH, "%g", dotOnly ? 0.0f : b[16] * scale);
    Com_sprintf(values[XC_GAP], MAX_QPATH, "%g", (short)(b[14] | b[15] << 8) * scale);
    Com_sprintf(values[XC_THICKNESS], MAX_QPATH, "%g", (b[13] & 63) * scale);
    Com_sprintf(values[XC_COLOR], MAX_QPATH, "%02X%02X%02X", b[5], b[6], b[7]);
    Com_sprintf(values[XC_ALPHA], MAX_QPATH, "%d", b[8]);
    Com_sprintf(values[XC_OUTLINE], MAX_QPATH, "%d", b[13] >> 6);
    Com_sprintf(values[XC_OUTLINE_COLOR], MAX_QPATH, "%02X%02X%02X", b[9], b[10], b[11]);
    Com_sprintf(values[XC_OUTLINE_ALPHA], MAX_QPATH, "%d", b[12]);
    Com_sprintf(values[XC_DOT], MAX_QPATH, "%d", dotOnly || (b[4] >> 6 & 1));
    Com_sprintf(values[XC_T], MAX_QPATH, "%d", b[4] >> 7);
}

// archived, also before the cgame registers it (at the main menu): the empty reset value takes the cgame's default
static void CL_XcSet(const char *name, const char *value)
{
    Cvar_Get(name, "", CVAR_ARCHIVE);
    Cvar_Set(name, value);
}

// a cvar's size in pixels at the code's height, rounded, within lo to hi
static int CL_XcPixels(const cvar_t *var, const byte *b, int lo, int hi)
{
    float pixels = var->value * Q_max(CL_XcHeight(b), XC_MINHEIGHT) / 1080.0f;

    if (!(pixels >= lo)) {
        return lo; // NaN too
    }
    return pixels >= hi ? hi : (int)floor(pixels + 0.5f);
}

static void CL_XcColor(const cvar_t *var, unsigned int fallback, byte *rgb)
{
    unsigned int color = Com_HexColor(var->string, fallback);

    rgb[0] = color >> 16;
    rgb[1] = color >> 8;
    rgb[2] = color;
}

static int CL_XcClamp(const cvar_t *var, int hi)
{
    return Q_min(Q_max(var->integer, 0), hi);
}

/*
===============
CL_CrosshairImport_f

crosshair_import [code]: CS2's crosshair code (else the clipboard's: the console can't paste) to the cvars, and
kept in cg_crosshair_code for crosshair_export. A code CS2 doesn't take now changes nothing
===============
*/
void CL_CrosshairImport_f(void)
{
    char        code[64] = "";
    const char *text     = Cmd_Argc() > 1 ? Cmd_Argv(1) : Sys_GetWholeClipboard();
    const char *error;
    char        values[XC_CVARS][MAX_QPATH];
    byte        b[XC_BYTES];
    int         i;

    if (text) {
        // the first word, as copied with spaces or a line break around it
        text += strspn(text, " \t\r\n");
        Q_strncpyz(code, text, Q_min(strcspn(text, " \t\r\n") + 1, sizeof(code)));
    }
    if (!code[0]) {
        Com_Printf("crosshair_import: copy a crosshair code in CS2 first, or type crosshair_import CS...\n");
        return;
    }
    error = CL_XcDecode(code, b);
    if (error) {
        Com_Printf("crosshair_import: %s; nothing changed.\n", error);
        return;
    }

    CL_XcCvars(b, values);
    for (i = 0; i < XC_CVARS; i++) {
        CL_XcSet(xcCvars[i], values[i]);
    }
    CL_XcSet("cg_crosshair_code", code);
    Com_Printf("Crosshair imported.\n");
}

/*
===============
CL_CrosshairExport_f

crosshair_export: the crosshair as CS2's code, printed and copied to the clipboard. From the imported code, the
settings changed since written over it, so an unchanged crosshair gives back the very code it was imported from
===============
*/
void CL_CrosshairExport_f(void)
{
    char          values[XC_CVARS][MAX_QPATH];
    const cvar_t *cvars[XC_CVARS];
    byte          b[XC_BYTES];
    char          code[2 + XC_CHARS + 1];
    unsigned int  splits;
    int           i, gap;

    if (CL_XcDecode(Cvar_VariableString("cg_crosshair_code"), b)) {
        // none imported: the cvars' defaults at 1080 lines (as a setting not registered yet is), a Static Cross,
        // CS2's own values for what MOHAA has no setting for
        memset(b, 0, sizeof(b));
        b[1] = 1;
        b[2] = 1080 & 0xFF;
        b[3] = 1080 >> 8;
        b[4] = XC_STATIC;
        memset(&b[5], 0xFF, 4); // white
        b[12] = 255; // black outline, off
        b[13] = 2;   // thickness
        b[14] = 4;   // gap
        b[16] = 8;   // length
        b[17] = 255; // dynamic spread limit
        // split distance 3, inner split alpha 0, outer 1 (0.3 + 70 hundredths), split size ratio 1
        splits = 3 | 70 << 14 | 100 << 21;
        b[18]  = splits;
        b[19]  = splits >> 8;
        b[20]  = splits >> 16;
        b[21]  = splits >> 24;
        b[22]  = 90; // scope dot scale 1 (0.1 + 90 hundredths)
    }

    // a setting not registered yet (no map played, nothing imported) is at its default, so unchanged
    CL_XcCvars(b, values);
    for (i = 0; i < XC_CVARS; i++) {
        cvars[i] = Cvar_FindVar(xcCvars[i]);
        if (cvars[i] && !strcmp(cvars[i]->string, values[i])) {
            cvars[i] = NULL;
        }
    }

    if ((b[4] & 31) == XC_DOT_ONLY && (cvars[XC_LENGTH] || cvars[XC_DOT])) {
        // arms on, or the dot off: no longer Dot Only
        b[4]             = (b[4] & ~31) | XC_STATIC;
        cvars[XC_LENGTH] = Cvar_FindVar(xcCvars[XC_LENGTH]);
        cvars[XC_DOT]    = Cvar_FindVar(xcCvars[XC_DOT]);
    }
    if (cvars[XC_LENGTH]) {
        b[16] = CL_XcPixels(cvars[XC_LENGTH], b, 0, 255);
    }
    if (cvars[XC_GAP]) {
        gap   = CL_XcPixels(cvars[XC_GAP], b, -32768, 32767);
        b[14] = gap & 0xFF;
        b[15] = (gap >> 8) & 0xFF;
    }
    if (cvars[XC_THICKNESS]) {
        b[13] = (b[13] & ~63) | CL_XcPixels(cvars[XC_THICKNESS], b, 0, 63);
    }
    if (cvars[XC_OUTLINE]) {
        b[13] = (b[13] & 63) | CL_XcClamp(cvars[XC_OUTLINE], 2) << 6;
    }
    if (cvars[XC_COLOR]) {
        CL_XcColor(cvars[XC_COLOR], 0xFFFFFF, &b[5]);
    }
    if (cvars[XC_ALPHA]) {
        b[8] = CL_XcClamp(cvars[XC_ALPHA], 255);
    }
    if (cvars[XC_OUTLINE_COLOR]) {
        CL_XcColor(cvars[XC_OUTLINE_COLOR], 0x000000, &b[9]);
    }
    if (cvars[XC_OUTLINE_ALPHA]) {
        b[12] = CL_XcClamp(cvars[XC_OUTLINE_ALPHA], 255);
    }
    if (cvars[XC_DOT]) {
        b[4] = (b[4] & ~64) | (cvars[XC_DOT]->integer != 0) << 6;
    }
    if (cvars[XC_T]) {
        b[4] = (b[4] & ~128) | (cvars[XC_T]->integer != 0) << 7;
    }

    CL_XcEncode(b, code);
    Sys_SetClipboard(code);
    Com_Printf("%s\n(copied to the clipboard)\n", code);
}
