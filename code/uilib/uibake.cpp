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

// uibake.cpp: what the UI bakes into pictures (Added). TrueType faces become
// font pages the renderer draws as it draws a RitualFont's; the URC's shapes
// (circles, rings, frames, rounded boxes, polygons, gradients, text, PNG
// pictures) are rasterized into one image a widget, at the size drawn. Each
// is baked once (RegisterImage2D) and again only after the renderer restarts;
// the images outlive map loads, their shaders are looked up again by name.
// stb_truetype reads the faces and fills every outline, glyph or shape.
//
// The texels are made on other threads where they can be ahead of their
// registration: the faces asked for while the .urc files are read
// (their pages registered after, in the order asked for) and the shapes a
// menu walk will bake (UI_BakeAhead: the HUD's, made while the map loads).
// The registrations stay on the main thread, in the same order as before, so
// every image has the same place in the renderer's atlas.

#include "ui_local.h"
#include "uibake.h"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <zlib.h>

#define stbtt_vertex_type float
typedef struct {
    float         x, y, cx, cy;
    unsigned char type, padding;
} uiVertex_t;
#define stbtt_vertex uiVertex_t
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "../thirdparty/stb/stb_truetype.h"

typedef struct {
    std::string    file;
    void          *data; // NULL: no such face
    stbtt_fontinfo info;
    float          capEm; // its capitals' height, a share of the em (0: no H)
} uiFace_t;

typedef struct {
    uiFace_t *face;
    float     px; // design pixels (absolute: screen pixels)
    qboolean  shadow, slant, absolute;
} uiFontSpec_t;

typedef struct {
    float scale;      // font units to pixels
    int   lineH;      // a line's pixels
    float base, capH; // the baseline from the line's top (the capitals' middle the line's), the capitals' height
} uiMetrics_t;

typedef struct {
    std::string      name;
    int              seq;
    float            pageScale; // the page's width over 256 (a texel a pixel at this scale)
    fontheader_t     header;
    fontheader_sgl_t sgl;
} uiBakedFont_t;

typedef struct {
    int       seq, gen;
    qhandle_t handle;
    char      image[32];
} uiBaked_t;

static std::vector<uiFace_t *>                    bake_faces; // (bake_lock: the threads find theirs)
static std::vector<uiBakedFont_t *>               bake_fonts;
// the names asked for, lowercased: their baked face, or NULL when the name
// isn't one (a RitualFont: not looked at again, as faces aren't, Bake_Face)
static std::unordered_map<std::string, uiBakedFont_t *> bake_fontNames;
static std::unordered_map<std::string, uiBaked_t> bake_shapes;
static thread_local std::vector<uiVertex_t>       bake_path; // the outline being filled
static int                                        bake_gen;
static std::mutex                                 bake_lock; // bake_faces, bake_pictures, bake_svgs

static const float SLANT = 0.21256f; // tan 12 degrees: CSS's text-skew -12deg

static float bake_designScale = 1.0f; // the design's scale times this (cg_hud_scale)
static int   bake_faceGen;             // its changes and the HUD font's (what the faces are made at)
static char  bake_hudFont[2][MAX_QPATH]; // the HUD's small text's files for Oswald Medium's and SemiBold's ("": Oswald's)

float UI_DesignScale(float w, float h)
{
    // the width counted as far as 4:3 of the height: a 4:3 screen's is as large as a 16:9 screen's of its height
    return Q_min(uid.vidWidth / Q_min(w, h * 4.0f / 3.0f), uid.vidHeight / h) * bake_designScale;
}

bool UI_SetDesignScale(float scale)
{
    if (scale == bake_designScale) {
        return false;
    }
    bake_designScale = scale;
    bake_faceGen++;
    bake_gen++;
    return true;
}

bool UI_SetHudFont(const char *medium, const char *semibold)
{
    if (!Q_stricmp(bake_hudFont[0], medium) && !Q_stricmp(bake_hudFont[1], semibold)) {
        return false;
    }
    Q_strncpyz(bake_hudFont[0], medium, sizeof(bake_hudFont[0]));
    Q_strncpyz(bake_hudFont[1], semibold, sizeof(bake_hudFont[1]));
    bake_faceGen++;
    bake_gen++;
    return true;
}

int UI_FontStamp(void)
{
    // the screen's size too: a face is baked at its pixel size there
    // (UI_DesignScale). A vid_restart's UI finds its fonts again before it
    // learns the new size (CL_Vid_Restart_f: UI_ResolutionChange, then
    // CL_BeginRegistration's glconfig), which baked them at the old one
    const unsigned size = (unsigned)uid.vidWidth * 8191u + (unsigned)uid.vidHeight;

    return (int)((size * 131u + (unsigned)bake_faceGen) * 4099u + (unsigned)uii.GetRefSequence());
}

void UI_BakeForget(void)
{
    bake_gen++;
}

int UI_BakeStamp(void)
{
    return (bake_gen << 16) ^ uii.GetRefSequence();
}

/*
====================================================================

THREADS

Texels made on other threads: a job reads only what it was given and what
the main thread read for it before (the faces, the pictures' files)

====================================================================
*/

static const std::thread::id bake_main = std::this_thread::get_id();

// the jobs, run in order by up to 4 threads (two cores left to the game and
// the renderer), made as jobs come; a thread leaves once no job has come for
// a moment, so none stays idle (the jobs come in bursts: the UI's start, a
// map's load). Never freed: a thread may still hold it as the process ends
static struct uiJobs_s {
    std::mutex                        lock;
    std::condition_variable           wake;
    std::deque<std::function<void()>> queue;
    int                               threads;
} *bake_jobs;

static void Bake_Work(void)
{
    std::unique_lock<std::mutex> l(bake_jobs->lock);

    while (bake_jobs->wake.wait_for(l, std::chrono::milliseconds(250), [] { return !bake_jobs->queue.empty(); })) {
        std::function<void()> job = std::move(bake_jobs->queue.front());

        bake_jobs->queue.pop_front();
        l.unlock();
        job();
        l.lock();
    }
    bake_jobs->threads--;
}

// f() on one of the threads (on the main thread only)
template <typename F> static std::future<decltype(std::declval<F>()())> Bake_Run(F f)
{
    // (the cores counted once: hardware_concurrency reads a file each time)
    static const int most = Q_max(1, Q_min(4, (int)std::thread::hardware_concurrency() - 2));
    auto             task = std::make_shared<std::packaged_task<decltype(f())()>>(f);

    if (!bake_jobs) {
        bake_jobs = new uiJobs_s();
    }
    std::lock_guard<std::mutex> l(bake_jobs->lock);

    bake_jobs->queue.push_back([task] { (*task)(); });
    if (bake_jobs->threads < most) {
        bake_jobs->threads++;
        std::thread(Bake_Work).detach();
    }
    bake_jobs->wake.notify_one();
    return task->get_future();
}

std::future<void> UI_BakeRun(std::function<void()> job)
{
    return Bake_Run(job);
}

/*
====================================================================

FACES

====================================================================
*/

static uiFace_t *Bake_Face(const char *file)
{
    std::lock_guard<std::mutex> l(bake_lock);
    void                       *buf;
    uiFace_t                   *f;

    for (uiFace_t *face : bake_faces) {
        if (!Q_stricmp(face->file.c_str(), file)) {
            return face->data ? face : NULL;
        }
    }
    if (std::this_thread::get_id() != bake_main) {
        return NULL; // (a shape made ahead has its faces read before, Bake_Prepare)
    }
    f       = new uiFace_t();
    f->file = file;
    if (uii.File_OpenFile(va("fonts/%s.ttf", file), &buf) > 0) {
        if (stbtt_InitFont(&f->info, (unsigned char *)buf, 0)) {
            int x0, y0, x1, y1;

            f->data  = buf;
            f->capEm = stbtt_GetCodepointBox(&f->info, 'H', &x0, &y0, &x1, &y1) ? y1 * stbtt_ScaleForMappingEmToPixels(&f->info, 1.0f) : 0.0f;
        } else {
            uii.File_FreeFile(buf);
        }
    }
    bake_faces.push_back(f);
    return f->data ? f : NULL;
}

// "<file>-<px>[s][i][p][h]"
static qboolean Bake_ParseFont(const char *name, uiFontSpec_t *spec)
{
    const char *dash = strrchr(name, '-');
    char        file[MAX_QPATH];
    char       *end;

    if (!dash || dash == name || dash - name >= (int)sizeof(file)) {
        return qfalse;
    }
    spec->px = (float)strtod(dash + 1, &end);
    if (end == dash + 1 || spec->px <= 0) {
        return qfalse;
    }
    spec->shadow = strchr(end, 's') ? qtrue : qfalse;
    spec->slant  = strchr(end, 'i') ? qtrue : qfalse;
    // (p: px screen pixels, whatever the design's scale: the menus' type, sized to the screen themselves)
    spec->absolute = strchr(end, 'p') ? qtrue : qfalse;
    Q_strncpyz(file, name, dash - name + 1);
    // the HUD's small text (its design-size faces, the HUD's menus'; h: one
    // drawn in the HUD at the screen's size) in the HUD's font
    if (strchr(end, 'h') || (!spec->absolute && spec->px <= UI_HUD_SMALL)) {
        for (int k = 0; k < 2; k++) {
            if (bake_hudFont[k][0] && !Q_stricmp(file, k ? "oswald-semibold" : "oswald-medium")) {
                Q_strncpyz(file, bake_hudFont[k], sizeof(file));
                break;
            }
        }
    }
    spec->face = Bake_Face(file);
    if (!spec->face) {
        return qfalse;
    }
    // a size is Oswald's: another face's capitals as tall as Oswald's at it
    if (Q_stricmpn(file, "oswald-", 7)) {
        const uiFace_t *oswald = Bake_Face("oswald-medium");

        if (oswald && oswald->capEm > 0.0f && spec->face->capEm > 0.0f) {
            spec->px *= oswald->capEm / spec->face->capEm;
        }
    }
    return qtrue;
}

static void Bake_Metrics(const uiFace_t *face, float pixels, uiMetrics_t *m)
{
    int ascent, descent, gap, x0, y0, x1, y1;

    m->scale = stbtt_ScaleForMappingEmToPixels(&face->info, pixels);
    stbtt_GetFontVMetrics(&face->info, &ascent, &descent, &gap);
    m->lineH = (int)ceilf((ascent - descent) * m->scale);
    m->capH  = stbtt_GetCodepointBox(&face->info, 'H', &x0, &y0, &x1, &y1) ? y1 * m->scale : ascent * m->scale * 0.7f;
    m->base  = floorf(m->lineH * 0.5f + m->capH * 0.5f + 0.5f);
}

// the game's text is Windows-1252
static int Bake_Unicode(int c)
{
    static const unsigned short high[32] = {
        0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
        0x2039, 0x0152, 0,      0x017D, 0,      0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
        0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178,
    };

    return c >= 0x80 && c < 0xA0 ? high[c - 0x80] : c;
}

// a glyph's outline into bake_path: its pen at x, its baseline at y, turned
// by (cs, sn) about (ox, oy)
static float Bake_GlyphPath(const uiFace_t *face, int c, float scale, float slant, float x, float y, float cs, float sn, float ox, float oy)
{
    const int   glyph = stbtt_FindGlyphIndex(&face->info, Bake_Unicode(c));
    uiVertex_t *v;
    int         adv, lsb, n, i;

    stbtt_GetGlyphHMetrics(&face->info, glyph, &adv, &lsb);
    n = stbtt_GetGlyphShape(&face->info, glyph, &v);
    for (i = 0; i < n; i++) {
        uiVertex_t p = v[i];
        float      px[2], py[2];

        px[0] = x + (p.x + p.y * slant) * scale - ox;
        py[0] = y - p.y * scale - oy;
        px[1] = x + (p.cx + p.cy * slant) * scale - ox;
        py[1] = y - p.cy * scale - oy;
        p.x   = ox + px[0] * cs - py[0] * sn;
        p.y   = oy + px[0] * sn + py[0] * cs;
        p.cx  = ox + px[1] * cs - py[1] * sn;
        p.cy  = oy + px[1] * sn + py[1] * cs;
        bake_path.push_back(p);
    }
    if (n > 0) {
        stbtt_FreeShape(&face->info, v);
    }
    return adv * scale;
}

// bake_path's coverage, w x h (y down)
static void Bake_Fill(byte *mask, int w, int h)
{
    stbtt__bitmap bm;

    memset(mask, 0, w * h);
    if (bake_path.empty()) {
        return;
    }
    bm.w      = w;
    bm.h      = h;
    bm.stride = w;
    bm.pixels = mask;
    stbtt_Rasterize(&bm, 0.25f, bake_path.data(), (int)bake_path.size(), 1.0f, 1.0f, 0.0f, 0.0f, 0, 0, 0, NULL);
    bake_path.clear();
}

static float Bake_Sample(const byte *m, int w, int h, float x, float y)
{
    const int   ix = (int)floorf(x), iy = (int)floorf(y);
    const float ax = x - ix, ay = y - iy;
    float       v = 0.0f;

    for (int k = 0; k < 4; k++) {
        const int sx = ix + (k & 1), sy = iy + (k >> 1);

        if (sx >= 0 && sy >= 0 && sx < w && sy < h) {
            v += ((k & 1) ? ax : 1.0f - ax) * ((k >> 1) ? ay : 1.0f - ay) * m[sy * w + sx];
        }
    }
    return v / 255.0f;
}

// the drop shadow's alpha under a mask (Omaha's: a pixel up, down, left and
// right at 0.5, 1.5 down and right at 0.33, pixels of 1080 lines, times s)
static float Bake_ShadowAt(const byte *m, int w, int h, int x, int y, float s)
{
    static const float passes[5][3] = {
        {0.0f, -1.0f, 0.5f}, {0.0f, 1.0f, 0.5f}, {-1.0f, 0.0f, 0.5f}, {1.0f, 0.0f, 0.5f}, {1.5f, 1.5f, 0.33f}
    };
    float keep = 1.0f;

    for (int p = 0; p < 5; p++) {
        keep *= 1.0f - passes[p][2] * Bake_Sample(m, w, h, x - passes[p][0] * s, y - passes[p][1] * s);
    }
    return 1.0f - keep;
}

int UI_ShadowMargin(float s)
{
    return (int)ceilf(1.5f * s) + 1;
}

/*
================
UI_BakeFont

The page: a cell per character, as wide as its advance and a line tall,
laid out 256 * scale texels across, so a cell's width in texture
coordinates times 256 (what the renderer's strings advance by) is its
width in design units, drawn a texel a pixel. The baseline sits where a
label's text, centered, has its capitals' middle on the label's
================
*/
// the pages asked for in a batch (UI_BakeBatch), registered at its end or before anything else is
typedef struct {
    uiBakedFont_t                     *f;
    int                                w, h;
    std::shared_ptr<byte>              page;
    std::vector<std::future<void>>     parts;
} uiPendingPage_t;

static bool                         bake_batch;
static std::vector<uiPendingPage_t> bake_pages;

static void Bake_FontPage(uiBakedFont_t *f, const uiFontSpec_t *spec)
{
    const float       ds     = UI_DesignScale(1920.0f, 1080.0f);
    // an absolute face's page: as wide as 256 units of px / 24 pixels (a page
    // for any size, the glyphs a texel a pixel drawn at its page scale)
    const float       s      = spec->absolute ? Q_max(1.0f, floorf(spec->px / 24.0f)) : ds;
    const float       slant  = spec->slant ? SLANT : 0.0f;
    const qboolean    shadow = spec->shadow;
    // the shadow's offset, read here: a bake thread runs after the caller's spec is gone
    const float       shOff  = spec->absolute ? 1.0f : s;
    const int         pageW  = Q_max(32, (int)(256.0f * s + 0.001f));
    const uiFace_t   *face   = spec->face;
    fontheader_sgl_t *sgl    = &f->sgl;
    uiMetrics_t       m;
    int               cells[256][3]; // x, y, w
    int               x = 0, y = 0, pageH, c;
    std::vector<byte> page;

    Bake_Metrics(face, spec->absolute ? spec->px : spec->px * s, &m);
    memset(sgl, 0, sizeof(*sgl));
    for (c = 0; c < 256; c++) {
        int adv, lsb, glyph = c < 32 ? 0 : stbtt_FindGlyphIndex(&face->info, Bake_Unicode(c));

        sgl->indirection[c] = -1;
        cells[c][2]         = 0;
        if (c < 32 || (c != ' ' && !glyph)) {
            continue;
        }
        stbtt_GetGlyphHMetrics(&face->info, glyph, &adv, &lsb);
        cells[c][2] = Q_min(pageW, Q_max(1, (int)(adv * m.scale + 0.5f)));
        if (x + cells[c][2] > pageW) {
            x = 0;
            y += m.lineH + 1;
        }
        cells[c][0] = x;
        cells[c][1] = y;
        x += cells[c][2] + 1;
    }
    pageH = y + m.lineH;
    for (c = 32; c < 256; c++) {
        if (cells[c][2]) {
            sgl->indirection[c]       = c;
            sgl->locations[c].pos[0]  = (float)cells[c][0] / pageW;
            sgl->locations[c].pos[1]  = (float)cells[c][1] / pageH;
            sgl->locations[c].size[0] = (float)cells[c][2] / pageW;
            sgl->locations[c].size[1] = (float)m.lineH / pageH;
        }
    }
    f->pageScale     = pageW / 256.0f;
    sgl->height      = spec->absolute ? m.lineH / f->pageScale : m.lineH / s;
    sgl->aspectRatio = 1.0f;
    sgl->trhandle    = -1;
    Q_strncpyz(sgl->name, f->name.c_str(), sizeof(sgl->name));
    memset(&f->header, 0, sizeof(f->header));
    f->header.sgl[0] = sgl;
    Q_strncpyz(f->header.name, f->name.c_str(), sizeof(f->header.name));

    // the texels of the characters from first to last (on any thread)
    auto texels = [=](byte *t, int first, int last) {
        std::vector<byte> mask(pageW * m.lineH);

        for (int k = first; k <= last; k++) {
            const int w = cells[k][2];

            if (!w) {
                continue;
            }
            // the slant leans about the capitals' middle
            Bake_GlyphPath(face, k, m.scale, slant, -m.capH * slant * 0.5f, m.base, 1.0f, 0.0f, 0.0f, 0.0f);
            Bake_Fill(mask.data(), w, m.lineH);
            for (int row = 0; row < m.lineH; row++) {
                byte *out = t + ((cells[k][1] + row) * pageW + cells[k][0]) * 4;

                for (int col = 0; col < w; col++, out += 4) {
                    const float a     = mask[row * w + col] / 255.0f;
                    const float sh    = shadow ? Bake_ShadowAt(mask.data(), w, m.lineH, col, row, shOff) : 0.0f;
                    const float alpha = a + sh * (1.0f - a);

                    out[0] = out[1] = out[2] = (byte)(alpha > 0.0f ? a / alpha * 255.0f + 0.5f : 255.0f);
                    out[3]                   = (byte)(alpha * 255.0f + 0.5f);
                }
            }
        }
    };
    // the renderer's font shader: gfx/fonts/<name>
    if (bake_batch) {
        // (in four parts, after the page is cleared on a thread too: the last ones asked for are waited for)
        std::shared_ptr<byte>    t((byte *)malloc(pageW * pageH * 4), free);
        std::shared_future<void> clear = Bake_Run([t, pageW, pageH] { memset(t.get(), 0, pageW * pageH * 4); }).share();

        bake_pages.push_back({f, pageW, pageH, t});
        for (c = 32; c < 256; c += 56) {
            bake_pages.back().parts.push_back(Bake_Run([texels, t, clear, c] { clear.wait(); texels(t.get(), c, c + 55); }));
        }
        return;
    }
    page.assign(pageW * pageH * 4, 0);
    texels(page.data(), 32, 255);
    uii.Rend_RegisterImage2D(va("gfx/fonts/%s", f->name.c_str()), page.data(), pageW, pageH);
}

// the batch's pages registered, in the order asked for
static void Bake_FlushPages(void)
{
    for (uiPendingPage_t& pp : bake_pages) {
        for (std::future<void>& part : pp.parts) {
            part.get();
        }
        // (none when the UI ends after the renderer: the game quit before its first frame)
        if (uii.IsRendererLoaded()) {
            uii.Rend_RegisterImage2D(va("gfx/fonts/%s", pp.f->name.c_str()), pp.page.get(), pp.w, pp.h);
        }
    }
    bake_pages.clear();
}

void UI_BakeBatch(bool begin)
{
    bake_batch = begin;
    if (!begin) {
        Bake_FlushPages();
    }
}

float UI_BakedFontScale(const fontheader_t *font)
{
    for (uiBakedFont_t *b : bake_fonts) {
        if (&b->header == font) {
            return b->pageScale;
        }
    }
    return 0.0f;
}

fontheader_t *UI_BakeFont(const char *name)
{
    // the last names asked for, as given (the UI makes fonts every frame:
    // View3D's sound and subtitle overlays theirs), before the map
    static struct {
        char           name[MAX_QPATH];
        uiBakedFont_t *f;
    } last[4];
    static int     lastNext;
    uiBakedFont_t *f = NULL;
    uiFontSpec_t   spec;
    auto           recent = [name](uiBakedFont_t *found) {
        Q_strncpyz(last[lastNext & 3].name, name, sizeof(last[0].name));
        last[lastNext & 3].f = found;
        lastNext++;
    };

    for (const auto& l : last) {
        if (l.name[0] && !strcmp(l.name, name) && (!l.f || l.f->seq == UI_FontStamp())) {
            return l.f ? &l.f->header : NULL;
        }
    }

    std::string key(name);

    for (char& c : key) {
        c = tolower((unsigned char)c);
    }
    auto it = bake_fontNames.find(key);
    if (it != bake_fontNames.end()) {
        f = it->second;
        if (!f) {
            recent(NULL);
            return NULL;
        }
        if (f->seq == UI_FontStamp()) {
            recent(f);
            return &f->header;
        }
    }
    if (!Bake_ParseFont(name, &spec)) {
        bake_fontNames[key] = NULL;
        return NULL;
    }
    if (!f) {
        f       = new uiBakedFont_t();
        f->name = name;
        bake_fonts.push_back(f);
        bake_fontNames[key] = f;
    }
    f->seq = UI_FontStamp();
    Bake_FontPage(f, &spec);
    return &f->header;
}

/*
====================================================================

SHAPES

====================================================================
*/

typedef struct {
    std::vector<float> px; // premultiplied RGBA
    int                w, h;
    float              scale;
} uiCanvas_t;

static void Bake_Over(float *px, const float *c, float cover)
{
    const float a = c[3] * cover;

    px[0] = c[0] * a + px[0] * (1.0f - a);
    px[1] = c[1] * a + px[1] * (1.0f - a);
    px[2] = c[2] * a + px[2] * (1.0f - a);
    px[3] = a + px[3] * (1.0f - a);
}

static void Bake_Mask(uiCanvas_t *cv, const byte *mask, const float *c)
{
    for (int i = 0; i < cv->w * cv->h; i++) {
        if (mask[i]) {
            Bake_Over(&cv->px[i * 4], c, mask[i] / 255.0f);
        }
    }
}

// the drop shadow under what the canvas has (its alpha the mask, so a faint
// shape casts as faint a shadow), as a face's under its glyphs
static void Bake_Shadow(uiCanvas_t *cv)
{
    std::vector<byte> mask(cv->w * cv->h);

    for (int i = 0; i < cv->w * cv->h; i++) {
        mask[i] = (byte)(Q_min(1.0f, cv->px[i * 4 + 3]) * 255.0f + 0.5f);
    }
    for (int y = 0; y < cv->h; y++) {
        for (int x = 0; x < cv->w; x++) {
            float *p = &cv->px[(y * cv->w + x) * 4];

            // (black under it: premultiplied, only its alpha grows)
            p[3] += Bake_ShadowAt(mask.data(), cv->w, cv->h, x, y, cv->scale) * (1.0f - p[3]);
        }
    }
}

static void Bake_MoveTo(float x, float y, int type)
{
    uiVertex_t v;

    memset(&v, 0, sizeof(v));
    v.x    = x;
    v.y    = y;
    v.type = type;
    bake_path.push_back(v);
}

static void Bake_Arc(float cx, float cy, float r, float a0, float a1, qboolean first)
{
    const int n = Q_max(4, (int)(fabsf(a1 - a0) * r / 2.0f) + 4);

    for (int i = 0; i <= n; i++) {
        const float a = a0 + (a1 - a0) * i / n;

        Bake_MoveTo(cx + r * cosf(a), cy + r * sinf(a), first && !i ? STBTT_vmove : STBTT_vline);
    }
}

static void Bake_Circle(float cx, float cy, float r, qboolean hole)
{
    Bake_Arc(cx, cy, r, 0.0f, hole ? -2.0f * M_PI : 2.0f * M_PI, qtrue);
}

static void Bake_RoundBox(float cx, float cy, float w, float h, float r, float angle, qboolean hole)
{
    std::vector<float> pts;
    const float        cs = cosf(DEG2RAD(angle)), sn = sinf(DEG2RAD(angle));
    int                i, k, n;

    r = Q_min(r, Q_min(w, h) * 0.5f);
    for (k = 0; k < 4; k++) {
        // the corners' arcs, clockwise from the top left
        const float ax = (k == 1 || k == 2) ? w * 0.5f - r : -w * 0.5f + r;
        const float ay = (k >= 2) ? h * 0.5f - r : -h * 0.5f + r;

        for (i = 0; i <= 8; i++) {
            const float a = DEG2RAD(180.0f + 90.0f * k + 90.0f * i / 8.0f);

            pts.push_back(ax + r * cosf(a));
            pts.push_back(ay + r * sinf(a));
        }
    }
    n = (int)pts.size() / 2;
    for (i = 0; i < n; i++) {
        const int   j = hole ? n - 1 - i : i;
        const float x = pts[j * 2], y = pts[j * 2 + 1];

        Bake_MoveTo(cx + x * cs - y * sn, cy + x * sn + y * cs, i ? STBTT_vline : STBTT_vmove);
    }
    Bake_MoveTo(bake_path[bake_path.size() - n].x, bake_path[bake_path.size() - n].y, STBTT_vline);
}

/*
====================================================================

PICTURES

A picture's file is read on the main thread; an RGBA PNG (the HUD's) is
decoded by whichever thread first needs its texels, the same texels as the
renderer's loader gives (zlib's inflate, much faster than the loader's); any
other picture is loaded by the renderer at once

====================================================================
*/

typedef struct {
    int                                   w, h; // 0: none
    std::shared_future<std::vector<byte>> rgba;
} uiPicture_t;

static std::unordered_map<std::string, std::shared_ptr<uiPicture_t>> bake_pictures; // (bake_lock)

static unsigned Bake_Be32(const byte *p)
{
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3];
}

// a PNG decoded here (RGBA, 8 bits a channel, not interlaced: the HUD's):
// its size, and with rgba its texels; false: another kind
static bool Bake_Png(const std::vector<byte>& f, int *w, int *h, std::vector<byte> *rgba)
{
    static const byte sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    std::vector<byte> z, raw;
    z_stream          zs = {};
    int               r;

    if (f.size() < 33 || memcmp(f.data(), sig, 8) || memcmp(&f[12], "IHDR", 4) || f[24] != 8 || f[25] != 6 || f[26] || f[27] || f[28]) {
        return false;
    }
    *w = (int)Bake_Be32(&f[16]);
    *h = (int)Bake_Be32(&f[20]);
    if (*w <= 0 || *h <= 0 || *w > 16384 || *h > 16384 || !rgba) {
        return *w > 0 && *h > 0 && *w <= 16384 && *h <= 16384;
    }
    for (size_t p = 8, len = 0; p + 12 <= f.size() && memcmp(&f[p + 4], "IEND", 4); p += 12 + len) {
        if ((len = Bake_Be32(&f[p])) > f.size() - p - 12) {
            return false;
        } else if (!memcmp(&f[p + 4], "IDAT", 4)) {
            z.insert(z.end(), &f[p + 8], &f[p + 8] + len);
        }
    }
    const int stride = *w * 4;

    raw.resize((size_t)(stride + 1) * *h);
    if (inflateInit(&zs) != Z_OK) {
        return false;
    }
    zs.next_in   = z.data();
    zs.avail_in  = (uInt)z.size();
    zs.next_out  = raw.data();
    zs.avail_out = (uInt)raw.size();
    r            = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);
    if (r != Z_STREAM_END || zs.avail_out) {
        return false;
    }
    // the rows unfiltered
    rgba->resize((size_t)stride * *h);
    for (int y = 0; y < *h; y++) {
        byte       *cur    = &raw[(size_t)y * (stride + 1) + 1];
        const byte *up     = y ? cur - stride - 1 : NULL;
        const int   filter = cur[-1];

        for (int i = 0; i < stride && filter <= 4; i++) {
            const int a = i >= 4 ? cur[i - 4] : 0, b = up ? up[i] : 0, c = up && i >= 4 ? up[i - 4] : 0;
            const int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);

            cur[i] += filter == 1 ? a : filter == 2 ? b : filter == 3 ? (a + b) >> 1 : filter == 4 ? (pa <= pb && pa <= pc ? a : pb <= pc ? b : c) : 0;
        }
        if (filter > 4) {
            return false;
        }
        memcpy(&(*rgba)[(size_t)y * stride], cur, stride);
    }
    return true;
}

// a picture: on the main thread found, or read and its texels begun (a PNG
// decoded on the threads, another kind by the renderer now); on another
// thread only found (a shape made ahead has its pictures read before)
static std::shared_ptr<uiPicture_t> Bake_PictureEntry(const char *file)
{
    std::lock_guard<std::mutex>  l(bake_lock);
    std::shared_ptr<uiPicture_t> p = bake_pictures[file];
    std::vector<byte>            f, rgba;
    byte                        *buf;
    int                          len;

    if (p || std::this_thread::get_id() != bake_main) {
        return p;
    }
    p = bake_pictures[file] = std::make_shared<uiPicture_t>();
    if ((len = uii.File_OpenFile(file, (void **)&buf)) > 0) {
        f.assign(buf, buf + len);
        uii.File_FreeFile(buf);
    }
    if (Bake_Png(f, &p->w, &p->h, NULL)) {
        p->rgba = Bake_Run([f] {
            std::vector<byte> t;
            int               w, h;

            return Bake_Png(f, &w, &h, &t) ? t : std::vector<byte>();
        }).share();
        return p;
    }
    p->w = p->h = 0;
    if (uii.Rend_LoadRawImage(file, &buf, &p->w, &p->h) && buf) {
        rgba.assign(buf, buf + p->w * p->h * 4);
        uii.Rend_FreeRawImage(buf);
    } else {
        Com_DPrintf("UI: no picture %s\n", file);
    }
    p->rgba = std::async(std::launch::deferred, [rgba] { return rgba; }).share();
    return p;
}

// a PNG fitted in the canvas (align: 0 left, 1 center, 2 right), its texels
// averaged over each pixel
static void Bake_Picture(uiCanvas_t *cv, const char *file, const float *c, int align, qboolean mirror)
{
    std::shared_ptr<uiPicture_t>          picture = Bake_PictureEntry(file);
    std::shared_future<std::vector<byte>> texels  = picture ? picture->rgba : std::shared_future<std::vector<byte>>();
    const byte                           *pic;
    int                                   sw, sh, tw, th, x0, y0, x, y;

    if (!texels.valid() || texels.get().size() < (size_t)picture->w * picture->h * 4 || !picture->w || !picture->h) {
        return;
    }
    pic = texels.get().data();
    sw  = picture->w;
    sh  = picture->h;
    {
        const float        fit = Q_min((float)cv->w / sw, (float)cv->h / sh);
        std::vector<float> rows; // across first: tw x sh, premultiplied

        tw = Q_max(1, (int)(sw * fit + 0.5f));
        th = Q_max(1, (int)(sh * fit + 0.5f));
        x0 = align == 0 ? 0 : align == 1 ? (cv->w - tw) / 2 : cv->w - tw;
        y0 = (cv->h - th) / 2;
        rows.assign(tw * sh * 4, 0.0f);
        for (y = 0; y < sh; y++) {
            for (x = 0; x < tw; x++) {
                const float a = x * (float)sw / tw, b = (x + 1) * (float)sw / tw;
                float      *o = &rows[(y * tw + x) * 4];

                for (int i = (int)a; i < b && i < sw; i++) {
                    const byte *p  = pic + (y * sw + i) * 4;
                    const float wt = (Q_min(b, (float)i + 1) - Q_max(a, (float)i)) * (p[3] / 255.0f) / (b - a);

                    o[0] += p[0] / 255.0f * wt;
                    o[1] += p[1] / 255.0f * wt;
                    o[2] += p[2] / 255.0f * wt;
                    o[3] += wt;
                }
            }
        }
        for (y = 0; y < th; y++) {
            const float a = y * (float)sh / th, b = (y + 1) * (float)sh / th;

            for (x = 0; x < tw; x++) {
                float sum[4] = {0, 0, 0, 0};

                for (int i = (int)a; i < b && i < sh; i++) {
                    const float  wt = (Q_min(b, (float)i + 1) - Q_max(a, (float)i)) / (b - a);
                    const float *p  = &rows[(i * tw + x) * 4];

                    for (int k = 0; k < 4; k++) {
                        sum[k] += p[k] * wt;
                    }
                }
                if (sum[3] > 0.0f && x0 + x >= 0 && y0 + y >= 0 && x0 + x < cv->w && y0 + y < cv->h) {
                    const float tint[4] = {c[0] * sum[0] / sum[3], c[1] * sum[1] / sum[3], c[2] * sum[2] / sum[3], c[3]};

                    Bake_Over(&cv->px[((y0 + y) * cv->w + x0 + (mirror ? tw - 1 - x : x)) * 4], tint, sum[3]);
                }
            }
        }
    }
}

/*
====================================================================

OUTLINES (SVG)

The moh-icons outlines (Added): an <svg viewBox="X Y W H"> of <path
fill="#rrggbb" d="..."> (M L H V C Q Z, absolute or relative; a hole
wound against the shape it cuts) and <circle cx cy r fill>, nothing
else of SVG. Read once on the main thread and kept (like the pictures);
filled at the size drawn with the faces' rasterizer, each element in its
fill times the layer's color (a white outline is tinted as the white
pictures are)

====================================================================
*/

typedef struct {
    char  op; // M L C Q Z (absolute)
    float p[6];
} uiSvgCmd_t;

typedef struct {
    bool                    circle;
    float                   c[3];   // a circle's cx cy r
    float                   rgb[3];
    std::vector<uiSvgCmd_t> cmds;
} uiSvgElem_t;

typedef struct {
    float                    vb[4]; // the viewBox; vb[2] 0: not read
    std::vector<uiSvgElem_t> elems;
} uiSvg_t;

static std::unordered_map<std::string, std::shared_ptr<uiSvg_t>> bake_svgs; // (bake_lock)

// an attribute's value in a tag's text, "" none
static std::string Svg_Attr(const std::string& tag, const char *name)
{
    const std::string key = std::string(" ") + name + "=\"";
    size_t            at  = tag.find(key);

    if (at == std::string::npos) {
        return "";
    }
    at += key.size();
    const size_t end = tag.find('"', at);
    return tag.substr(at, end == std::string::npos ? std::string::npos : end - at);
}

static void Svg_Color(const std::string& fill, float *rgb)
{
    unsigned v = 0xFFFFFF;

    if (fill.size() == 7 && fill[0] == '#') {
        v = (unsigned)strtoul(fill.c_str() + 1, NULL, 16);
    } else if (fill.size() == 4 && fill[0] == '#') {
        const unsigned s = (unsigned)strtoul(fill.c_str() + 1, NULL, 16);

        v = ((s >> 8) & 15) * 0x110000 + ((s >> 4) & 15) * 0x1100 + (s & 15) * 0x11;
    }
    rgb[0] = ((v >> 16) & 255) / 255.0f;
    rgb[1] = ((v >> 8) & 255) / 255.0f;
    rgb[2] = (v & 255) / 255.0f;
}

// a path's d into absolute commands
static void Svg_Path(const char *d, std::vector<uiSvgCmd_t>& out)
{
    char  op = 0;
    float cx = 0, cy = 0, sx = 0, sy = 0; // the pen, the subpath's start

    while (*d) {
        while (*d == ' ' || *d == ',' || *d == '\t' || *d == '\n' || *d == '\r') {
            d++;
        }
        if (!*d) {
            break;
        }
        if (isalpha((unsigned char)*d)) {
            op = *d++;
            if (op == 'Z' || op == 'z') {
                uiSvgCmd_t c = {'Z', {0}};

                out.push_back(c);
                cx = sx;
                cy = sy;
            }
            continue;
        }
        // the numbers this command takes
        // (no command yet: strchr would find 0 at a list's end)
        const int  n   = !op ? 0 : strchr("MmLlTt", op) ? 2 : strchr("HhVv", op) ? 1 : strchr("Cc", op) ? 6 : strchr("QqSs", op) ? 4 : 0;
        const bool rel = op >= 'a';
        float      v[6];
        int        i;

        if (!n) {
            return; // an unknown command (or numbers before any): the rest dropped
        }
        for (i = 0; i < n; i++) {
            char *end;

            while (*d == ' ' || *d == ',' || *d == '\t' || *d == '\n' || *d == '\r') {
                d++;
            }
            v[i] = strtof(d, &end);
            if (end == d) {
                return;
            }
            d = end;
        }
        uiSvgCmd_t c;

        memset(&c, 0, sizeof(c));
        switch (op) {
        case 'M':
        case 'm':
            c.op   = 'M';
            c.p[0] = v[0] + (rel ? cx : 0);
            c.p[1] = v[1] + (rel ? cy : 0);
            sx = c.p[0];
            sy = c.p[1];
            op = rel ? 'l' : 'L'; // more pairs after a move are lines
            break;
        case 'L':
        case 'l':
        case 'T':
        case 't':
            c.op   = 'L';
            c.p[0] = v[0] + (rel ? cx : 0);
            c.p[1] = v[1] + (rel ? cy : 0);
            break;
        case 'H':
        case 'h':
            c.op   = 'L';
            c.p[0] = v[0] + (rel ? cx : 0);
            c.p[1] = cy;
            break;
        case 'V':
        case 'v':
            c.op   = 'L';
            c.p[0] = cx;
            c.p[1] = v[0] + (rel ? cy : 0);
            break;
        case 'C':
        case 'c':
            c.op = 'C';
            for (i = 0; i < 6; i++) {
                c.p[i] = v[i] + (rel ? (i & 1 ? cy : cx) : 0);
            }
            break;
        case 'Q':
        case 'q':
        case 'S':
        case 's':
            // (S's first control point taken as the pen: a smooth curve's reflection isn't kept)
            c.op = op == 'Q' || op == 'q' ? 'Q' : 'C';
            if (c.op == 'Q') {
                for (i = 0; i < 4; i++) {
                    c.p[i] = v[i] + (rel ? (i & 1 ? cy : cx) : 0);
                }
            } else {
                c.p[0] = cx;
                c.p[1] = cy;
                for (i = 0; i < 4; i++) {
                    c.p[2 + i] = v[i] + (rel ? (i & 1 ? cy : cx) : 0);
                }
            }
            break;
        }
        out.push_back(c);
        // the pen: the command's last point
        {
            const int last = c.op == 'C' ? 4 : c.op == 'Q' ? 2 : 0;

            cx = c.p[last];
            cy = c.p[last + 1];
        }
    }
}

static std::shared_ptr<uiSvg_t> Svg_Parse(const char *text)
{
    std::shared_ptr<uiSvg_t> svg = std::make_shared<uiSvg_t>();
    const std::string        s(text);
    size_t                   at;

    memset(svg->vb, 0, sizeof(svg->vb));
    at = s.find("<svg");
    if (at != std::string::npos) {
        const std::string tag = s.substr(at, s.find('>', at) - at);

        sscanf(Svg_Attr(tag, "viewBox").c_str(), "%f %f %f %f", &svg->vb[0], &svg->vb[1], &svg->vb[2], &svg->vb[3]);
        if (svg->vb[2] <= 0 || svg->vb[3] <= 0) {
            svg->vb[2] = (float)atof(Svg_Attr(tag, "width").c_str());
            svg->vb[3] = (float)atof(Svg_Attr(tag, "height").c_str());
        }
    }
    for (at = s.find('<'); at != std::string::npos; at = s.find('<', at + 1)) {
        const bool path = !s.compare(at, 6, "<path ");
        const bool circ = !s.compare(at, 8, "<circle ");

        if (!path && !circ) {
            continue;
        }
        const std::string tag = s.substr(at, s.find('>', at) - at);
        uiSvgElem_t       e;
        std::string       fill = Svg_Attr(tag, "fill");

        e.circle = circ;
        memset(e.c, 0, sizeof(e.c));
        if (fill == "none") {
            continue;
        }
        Svg_Color(fill.empty() ? "#000000" : fill, e.rgb);
        if (circ) {
            e.c[0] = (float)atof(Svg_Attr(tag, "cx").c_str());
            e.c[1] = (float)atof(Svg_Attr(tag, "cy").c_str());
            e.c[2] = (float)atof(Svg_Attr(tag, "r").c_str());
        } else {
            Svg_Path(Svg_Attr(tag, "d").c_str(), e.cmds);
        }
        svg->elems.push_back(e);
    }
    return svg;
}

// an outline's file, read on the main thread (on another, only one read before: Bake_Prepare)
static std::shared_ptr<uiSvg_t> Bake_Svg(const char *file)
{
    std::lock_guard<std::mutex> l(bake_lock);
    auto                        it = bake_svgs.find(file);
    void                       *buf;
    long                        len;

    if (it != bake_svgs.end()) {
        return it->second;
    }
    if (std::this_thread::get_id() != bake_main) {
        return NULL;
    }
    len = uii.File_OpenFile(file, &buf);
    if (len <= 0) {
        bake_svgs[file] = NULL;
        Com_DPrintf("UI: no outline %s\n", file);
        return NULL;
    }
    std::string text((const char *)buf, len);
    uii.File_FreeFile(buf);
    return bake_svgs[file] = Svg_Parse(text.c_str());
}

bool UI_OutlineSize(const char *file, float *w, float *h)
{
    std::shared_ptr<uiSvg_t> svg = Bake_Svg(file);

    if (!svg || svg->vb[2] <= 0 || svg->vb[3] <= 0) {
        return false;
    }
    *w = svg->vb[2];
    *h = svg->vb[3];
    return true;
}

// a cubic's points after the pen into bake_path, as lines a pixel or two long
static void Bake_Cubic(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3)
{
    const float len = sqrtf((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0)) + sqrtf((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1))
                    + sqrtf((x3 - x2) * (x3 - x2) + (y3 - y2) * (y3 - y2));
    const int   n   = Q_max(2, Q_min(64, (int)(len / 1.5f) + 1));

    for (int i = 1; i <= n; i++) {
        const float t = (float)i / n, u = 1.0f - t;
        const float a = u * u * u, b = 3 * u * u * t, c = 3 * u * t * t, d = t * t * t;

        Bake_MoveTo(a * x0 + b * x1 + c * x2 + d * x3, a * y0 + b * y1 + c * y2 + d * y3, STBTT_vline);
    }
}

// an outline fitted in the canvas (its aspect kept: align 0 left, 1 middle, 2 right; up and down in the
// middle); grey: each part in its fill's brightness
static void Bake_Outline(uiCanvas_t *cv, const char *file, const float *color, int align, qboolean mirror, qboolean grey)
{
    std::shared_ptr<uiSvg_t> svg = Bake_Svg(file);
    std::vector<byte>        mask(cv->w * cv->h);
    float                    k, ox, oy;

    if (!svg || svg->vb[2] <= 0 || svg->vb[3] <= 0) {
        return;
    }
    k  = Q_min(cv->w / svg->vb[2], cv->h / svg->vb[3]);
    ox = align == 0 ? 0.0f : align == 1 ? (cv->w - svg->vb[2] * k) * 0.5f : cv->w - svg->vb[2] * k;
    oy = (cv->h - svg->vb[3] * k) * 0.5f;
    // the viewBox's point on the canvas
    auto X = [&](float x) { return ox + (mirror ? svg->vb[0] + svg->vb[2] - x : x - svg->vb[0]) * k; };
    auto Y = [&](float y) { return oy + (y - svg->vb[1]) * k; };

    for (const uiSvgElem_t& e : svg->elems) {
        const float lum  = 0.299f * e.rgb[0] + 0.587f * e.rgb[1] + 0.114f * e.rgb[2];
        float       c[4] = {(grey ? lum : e.rgb[0]) * color[0], (grey ? lum : e.rgb[1]) * color[1], (grey ? lum : e.rgb[2]) * color[2], color[3]};

        if (e.circle) {
            Bake_Circle(X(e.c[0]), Y(e.c[1]), e.c[2] * k, qfalse);
        } else {
            float px = 0, py = 0, sx = 0, sy = 0;
            bool  open = false;

            for (const uiSvgCmd_t& m : e.cmds) {
                switch (m.op) {
                case 'M':
                    if (open) {
                        Bake_MoveTo(sx, sy, STBTT_vline); // (closed: the rasterizer's edges come in pairs)
                    }
                    px = sx = X(m.p[0]);
                    py = sy = Y(m.p[1]);
                    Bake_MoveTo(px, py, STBTT_vmove);
                    open = true;
                    break;
                case 'L':
                    px = X(m.p[0]);
                    py = Y(m.p[1]);
                    Bake_MoveTo(px, py, STBTT_vline);
                    break;
                case 'Q':
                    // (as a cubic)
                    {
                        const float qx = X(m.p[0]), qy = Y(m.p[1]), ex = X(m.p[2]), ey = Y(m.p[3]);

                        Bake_Cubic(px, py, px + (qx - px) * 2 / 3, py + (qy - py) * 2 / 3, ex + (qx - ex) * 2 / 3, ey + (qy - ey) * 2 / 3, ex, ey);
                        px = ex;
                        py = ey;
                    }
                    break;
                case 'C':
                    Bake_Cubic(px, py, X(m.p[0]), Y(m.p[1]), X(m.p[2]), Y(m.p[3]), X(m.p[4]), Y(m.p[5]));
                    px = X(m.p[4]);
                    py = Y(m.p[5]);
                    break;
                case 'Z':
                    if (open) {
                        Bake_MoveTo(sx, sy, STBTT_vline);
                    }
                    px = sx;
                    py = sy;
                    open = false;
                    break;
                }
            }
            if (open) {
                Bake_MoveTo(sx, sy, STBTT_vline);
            }
        }
        Bake_Fill(mask.data(), cv->w, cv->h);
        Bake_Mask(cv, mask.data(), c);
    }
}

// the tokens of a layer, a quoted string one
static int Bake_Tokens(const char *text, std::vector<std::string>& out)
{
    out.clear();
    while (*text) {
        while (*text == ' ' || *text == '\t') {
            text++;
        }
        if (!*text) {
            break;
        }
        if (*text == '"') {
            const char *end = strchr(text + 1, '"');

            out.push_back(std::string(text + 1, end ? end - text - 1 : strlen(text + 1)));
            text = end ? end + 1 : text + strlen(text);
        } else {
            const char *start = text;

            while (*text && *text != ' ' && *text != '\t') {
                text++;
            }
            out.push_back(std::string(start, text - start));
        }
    }
    return (int)out.size();
}

/*
================
Bake_Layer

A layer: what it is, then its color (r g b a, 0 to 1), then the rest.
Lengths are design units (times the canvas's scale); positions are
fractions of the box. The shapes:
  fill COLOR
  vgradient Y0 Y1 COLOR COLOR / hgradient X0 X1 COLOR COLOR
  circle COLOR [R [X Y]]        (R: of half the box, 1 by default; X Y its middle)
  ring W COLOR                  (W thick, inside the circle's edge)
  frame W COLOR                 (W thick, inside the box's edge)
  roundframe R W COLOR          (W thick, inside the edge of the box rounded R)
  roundbox R COLOR [W H ANGLE]  (the box, or W x H in its middle turned ANGLE)
  poly COLOR x y x y ...
  crescent COLOR X0 Y0 RF T0 T1 T2 T3  (Omaha's damage crescent: the box's
                                circle from X0 Y0 over the top and back by a
                                circle of RF, seen through a radial band in
                                from T0 to T1, out from T2 to T3)
  text COLOR FONT X Y ANGLE STRING   (FONT a face's name; its capitals'
                                middle at X Y, turned ANGLE about the middle)
  picture COLOR FILE [left|center|right] [mirror]
  svg COLOR FILE [left|center|right] [mirror] [grey]  (an outline, each part
                                in its fill, or its fill's brightness, times COLOR)
  shadow                        (the faces' drop shadow under the layers
                                before it: the box leaves it room, UI_ShadowMargin)
A layer can start with "in X0 Y0 X1 Y1": drawn in that part of the box
(fractions, or design units with "px", from the right or bottom edge when
negative: "in -2px 0 1 1" the box's 2 units at its right).
================
*/
static void Bake_Layer(uiCanvas_t *cv, const char *text)
{
    std::vector<std::string> t;

    if (!strcmp(text, "shadow")) {
        Bake_Shadow(cv);
        return;
    }

    // in X0 Y0 X1 Y1 <layer>: the layer in that part of the box, over the rest
    if (!strncmp(text, "in ", 3)) {
        float      b[4];
        int        used = 0;
        uiCanvas_t sub;

        {
            const char *p = text + 3;

            for (int k = 0; k < 4; k++) {
                char *end;

                b[k] = strtof(p, &end);
                if (end == p) {
                    return;
                }
                if (!strncmp(end, "px", 2)) {
                    // design units, from the far edge when negative
                    const float px = b[k] * cv->scale, side = (k & 1) ? cv->h : cv->w;

                    b[k] = (px < 0.0f || (px == 0.0f && *p == '-') ? side + px : px) / side;
                    end += 2;
                }
                p = end;
            }
            while (*p == ' ') {
                p++;
            }
            used = (int)(p - (text + 3));
        }
        {
            const int x0 = (int)floorf(b[0] * cv->w + 0.5f), y0 = (int)floorf(b[1] * cv->h + 0.5f);

            sub.w     = (int)floorf(b[2] * cv->w + 0.5f) - x0;
            sub.h     = (int)floorf(b[3] * cv->h + 0.5f) - y0;
            sub.scale = cv->scale;
            if (sub.w <= 0 || sub.h <= 0) {
                return;
            }
            sub.px.assign(sub.w * sub.h * 4, 0.0f);
            Bake_Layer(&sub, text + 3 + used);
            for (int y = 0; y < sub.h; y++) {
                for (int x = 0; x < sub.w; x++) {
                    const float *p = &sub.px[(y * sub.w + x) * 4];

                    if (x0 + x < 0 || y0 + y < 0 || x0 + x >= cv->w || y0 + y >= cv->h) {
                        continue;
                    }
                    float *d = &cv->px[((y0 + y) * cv->w + x0 + x) * 4];

                    for (int k = 0; k < 4; k++) {
                        d[k] = p[k] + d[k] * (1.0f - p[3]);
                    }
                }
            }
        }
        return;
    }
    const int                n  = Bake_Tokens(text, t);
    const float              w  = (float)cv->w, h = (float)cv->h;
    const float              cx = w * 0.5f, cy = h * 0.5f, r = Q_min(w, h) * 0.5f;
    std::vector<byte>        mask(cv->w * cv->h);
    float                    c[4];
    int                      a = 5; // the first token after the color
    std::vector<float>       v(n);
    // (the tokens' numbers read once: the gradients and crescents read them for each texel)
    for (int i = 0; i < n; i++) {
        v[i] = (float)atof(t[i].c_str());
    }
    auto num = [&](int i) { return i < n ? v[i] : 0.0f; };

    if (n < 5) {
        return;
    }
    // the gradients' stops come first, their colors after
    if (t[0] == "vgradient" || t[0] == "hgradient") {
        const qboolean v  = t[0] == "vgradient";
        const float    e0 = num(1) * (v ? h : w), e1 = num(2) * (v ? h : w);

        for (int y = 0; y < cv->h; y++) {
            for (int x = 0; x < cv->w; x++) {
                const float at = (v ? y : x) + 0.5f;
                const float f  = (at - e0) / Q_max(0.001f, e1 - e0);
                float       col[4];

                if (at < e0 || at >= e1) {
                    continue;
                }
                // premultiplied between the stops, back to straight
                col[3] = num(6) + (num(10) - num(6)) * f;
                for (int k = 0; k < 3; k++) {
                    col[k] = col[3] > 0.0f ? (num(3 + k) * num(6) + (num(7 + k) * num(10) - num(3 + k) * num(6)) * f) / col[3] : 0.0f;
                }
                Bake_Over(&cv->px[(y * cv->w + x) * 4], col, 1.0f);
            }
        }
        return;
    }
    // the rest: COLOR after the kind, or after the kind's one number
    if (t[0] == "ring" || t[0] == "frame" || t[0] == "roundbox") {
        a = 6;
    } else if (t[0] == "roundframe") {
        a = 7;
    }
    for (int k = 0; k < 4; k++) {
        c[k] = num(a - 4 + k);
    }

    if (t[0] == "fill") {
        for (int i = 0; i < cv->w * cv->h; i++) {
            Bake_Over(&cv->px[i * 4], c, 1.0f);
        }
        return;
    } else if (t[0] == "circle") {
        // [R [X Y]]: of half the box, its middle
        Bake_Circle(n > a + 2 ? num(a + 1) * w : cx, n > a + 2 ? num(a + 2) * h : cy, r * (n > a ? num(a) : 1.0f), qfalse);
    } else if (t[0] == "ring") {
        // (edges on whole pixels, at least one)
        Bake_Circle(cx, cy, r, qfalse);
        Bake_Circle(cx, cy, r - Q_max(1.0f, floorf(num(1) * cv->scale + 0.5f)), qtrue);
    } else if (t[0] == "frame") {
        const float p = Q_max(1.0f, floorf(num(1) * cv->scale + 0.5f));

        Bake_RoundBox(cx, cy, w, h, 0.0f, 0.0f, qfalse);
        Bake_RoundBox(cx, cy, w - p * 2.0f, h - p * 2.0f, 0.0f, 0.0f, qtrue);
    } else if (t[0] == "roundframe") {
        // (its width on whole pixels, at least one)
        const float p = Q_max(1.0f, floorf(num(2) * cv->scale + 0.5f)), rr = num(1) * cv->scale;

        Bake_RoundBox(cx, cy, w, h, rr, 0.0f, qfalse);
        Bake_RoundBox(cx, cy, w - p * 2.0f, h - p * 2.0f, Q_max(0.0f, rr - p), 0.0f, qtrue);
    } else if (t[0] == "roundbox") {
        if (n > a + 2) {
            Bake_RoundBox(cx, cy, num(a) * cv->scale, num(a + 1) * cv->scale, num(1) * cv->scale, num(a + 2), qfalse);
        } else {
            Bake_RoundBox(cx, cy, w, h, num(1) * cv->scale, 0.0f, qfalse);
        }
    } else if (t[0] == "poly") {
        for (int i = a; i + 1 < n; i += 2) {
            Bake_MoveTo(num(i) * w, num(i + 1) * h, i == a ? STBTT_vmove : STBTT_vline);
        }
        Bake_MoveTo(num(a) * w, num(a + 1) * h, STBTT_vline);
    } else if (t[0] == "crescent") {
        const float x0 = num(a) * w, y0 = num(a + 1) * w, rf = num(a + 2) * w;
        const float hh = sqrtf(rf * rf - (cx - x0) * (cx - x0)), fcy = y0 + hh;

        Bake_Arc(cx, cx, cx, atan2f(y0 - cx, x0 - cx), atan2f(y0 - cx, w - x0 - cx), qtrue);
        Bake_Arc(cx, fcy, rf, atan2f(y0 - fcy, w - x0 - cx), atan2f(y0 - fcy, x0 - cx), qfalse);
        Bake_Fill(mask.data(), cv->w, cv->h);
        for (int y = 0; y < cv->h; y++) {
            for (int x = 0; x < cv->w; x++) {
                const float d = sqrtf((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cx) * (y + 0.5f - cx)) / cx;
                const float m = d < num(a + 3)   ? 0.0f
                              : d < num(a + 4) ? (d - num(a + 3)) / (num(a + 4) - num(a + 3))
                              : d < num(a + 5) ? 1.0f
                              : d < num(a + 6) ? (num(a + 6) - d) / (num(a + 6) - num(a + 5))
                                               : 0.0f;

                mask[y * cv->w + x] = (byte)(mask[y * cv->w + x] * m + 0.5f);
            }
        }
        Bake_Mask(cv, mask.data(), c);
        return;
    } else if (t[0] == "text" && n > a + 4) {
        uiFontSpec_t spec;
        uiMetrics_t  m;
        const float  ang = DEG2RAD(num(a + 3));
        float        width = 0.0f, pen;

        if (!Bake_ParseFont(t[a].c_str(), &spec)) {
            return;
        }
        Bake_Metrics(spec.face, spec.px * cv->scale, &m);
        for (const char *p = t[a + 4].c_str(); *p; p++) {
            int adv, lsb;

            stbtt_GetGlyphHMetrics(&spec.face->info, stbtt_FindGlyphIndex(&spec.face->info, Bake_Unicode((byte)*p)), &adv, &lsb);
            width += adv * m.scale;
        }
        pen = num(a + 1) * w - width * 0.5f;
        for (const char *p = t[a + 4].c_str(); *p; p++) {
            pen += Bake_GlyphPath(spec.face, (byte)*p, m.scale, spec.slant ? SLANT : 0.0f, pen, num(a + 2) * h + m.capH * 0.5f, cosf(ang), sinf(ang), cx, cy);
        }
        Bake_Fill(mask.data(), cv->w, cv->h);
        if (spec.shadow) {
            static const float black[4] = {0, 0, 0, 1};

            for (int y = 0; y < cv->h; y++) {
                for (int x = 0; x < cv->w; x++) {
                    Bake_Over(&cv->px[(y * cv->w + x) * 4], black, Bake_ShadowAt(mask.data(), cv->w, cv->h, x, y, cv->scale) * c[3]);
                }
            }
        }
        Bake_Mask(cv, mask.data(), c);
        return;
    } else if (t[0] == "picture") {
        const int align = n > a + 1 ? (t[a + 1] == "left" ? 0 : t[a + 1] == "center" ? 1 : 2) : 1;

        Bake_Picture(cv, t[a].c_str(), c, align, n > a + 2 && t[a + 2] == "mirror");
        return;
    } else if (t[0] == "svg") {
        int      align  = 1;
        qboolean mirror = qfalse, grey = qfalse;

        for (int i = a + 1; i < n; i++) {
            align  = t[i] == "left" ? 0 : t[i] == "center" ? 1 : t[i] == "right" ? 2 : align;
            mirror = mirror || t[i] == "mirror" ? qtrue : qfalse;
            grey   = grey || t[i] == "grey" ? qtrue : qfalse;
        }
        Bake_Outline(cv, t[a].c_str(), c, align, mirror, grey);
        return;
    } else {
        if (std::this_thread::get_id() == bake_main) {
            Com_DPrintf("UI: unknown shape '%s'\n", t[0].c_str()); // (made ahead: told by Bake_Prepare)
        }
        return;
    }
    Bake_Fill(mask.data(), cv->w, cv->h);
    Bake_Mask(cv, mask.data(), c);
}

// a shape's texels (on any thread)
static void Bake_ShapeTexels(const std::vector<std::string>& layers, int w, int h, float scale, std::vector<byte>& out)
{
    uiCanvas_t cv;
    int        i;

    out.assign(w * h * 4, 0);
    cv.w     = w;
    cv.h     = h;
    cv.scale = scale;
    cv.px.assign(w * h * 4, 0.0f);
    for (const std::string& l : layers) {
        Bake_Layer(&cv, l.c_str());
    }
    for (i = 0; i < w * h; i++) {
        const float *p = &cv.px[i * 4];
        const float  a = p[3];

        for (int k = 0; k < 3; k++) {
            out[i * 4 + k] = (byte)(a > 0.0f ? Q_min(1.0f, p[k] / a) * 255.0f + 0.5f : 255.0f);
        }
        out[i * 4 + 3] = (byte)(Q_min(1.0f, a) * 255.0f + 0.5f);
    }
    // a texel left clear takes the color of one beside it: no dark fringe when drawn turned
    for (i = 0; i < w * h; i++) {
        static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};

        if (out[i * 4 + 3]) {
            continue;
        }
        for (int k = 0; k < 4; k++) {
            const int x = i % w + dx[k], y = i / w + dy[k];

            if (x >= 0 && y >= 0 && x < w && y < h && out[(y * w + x) * 4 + 3]) {
                memcpy(&out[i * 4], &out[(y * w + x) * 4], 3);
                break;
            }
        }
    }
}

// what a layer reads, read now (on the main thread): the faces, the
// pictures and the outlines it names (its tokens "<file>-<px>...", "....png"
// and "....svg"); an unknown kind told here, as Bake_Layer tells it on the main thread
static void Bake_Prepare(const char *text)
{
    static const char *const kinds[] = {
        "fill", "vgradient", "hgradient", "circle", "ring", "frame", "roundframe", "roundbox", "poly", "crescent", "text", "picture", "svg",
        "shadow"
    };
    std::vector<std::string> t;
    uiFontSpec_t             spec;
    size_t                   kind = 0, k = 0;

    Bake_Tokens(text, t);
    // (past "in X0 Y0 X1 Y1")
    while (kind < t.size() && t[kind] == "in") {
        kind += 5;
    }
    while (kind < t.size() && k < ARRAY_LEN(kinds) && t[kind] != kinds[k]) {
        k++;
    }
    if (kind < t.size() && k == ARRAY_LEN(kinds)) {
        Com_DPrintf("UI: unknown shape '%s'\n", t[kind].c_str());
    }
    for (const std::string& s : t) {
        if (s.size() > 4 && !Q_stricmp(s.c_str() + s.size() - 4, ".png")) {
            Bake_PictureEntry(s.c_str());
        } else if (s.size() > 4 && !Q_stricmp(s.c_str() + s.size() - 4, ".svg")) {
            Bake_Svg(s.c_str());
        } else if (strchr(s.c_str(), '-')) {
            Bake_ParseFont(s.c_str(), &spec);
        }
    }
}

// the shapes made ahead (UI_BakeAhead), by their keys, until baked
static bool                                                            bake_ahead;
static std::unordered_map<std::string, std::future<std::vector<byte>>> bake_made;

bool UI_BakeAhead(int on)
{
    return bake_ahead = on < 0 ? bake_ahead : on != 0;
}

void UI_BakeRelease(void)
{
    // (the jobs not begun dropped: theirs; the faces' pages made, and registered, first)
    Bake_FlushPages();
    bake_made.clear();
    if (bake_jobs) {
        std::lock_guard<std::mutex> l(bake_jobs->lock);

        bake_jobs->queue.clear();
    }
    std::lock_guard<std::mutex> l(bake_lock);
    bake_pictures.clear();
}

static qhandle_t Bake_Shape(const std::string& key, const char *image, const char *const *layers, int numLayers, int w, int h, float scale)
{
    auto              made = bake_made.find(key);
    std::vector<byte> texels;

    // (after the faces asked for before it: the atlas is filled in the order asked for)
    Bake_FlushPages();
    if (made != bake_made.end()) {
        texels = made->second.get();
        bake_made.erase(made);
    } else {
        Bake_ShapeTexels(std::vector<std::string>(layers, layers + numLayers), w, h, scale, texels);
    }
    return uii.Rend_RegisterImage2D(image, texels.data(), w, h);
}

void UI_BakeTexels(const char *const *layers, int numLayers, int w, int h, float scale, byte *out)
{
    const std::vector<std::string> lines(layers, layers + numLayers);
    std::vector<byte>              t;

    for (const std::string& l : lines) {
        Bake_Prepare(l.c_str());
    }
    Bake_ShapeTexels(lines, w, h, scale, t);
    memcpy(out, t.data(), t.size());
}

qhandle_t UI_BakeShape(const char *key, const char *const *layers, int numLayers, int w, int h, float scale)
{
    std::string k = key;
    uiBaked_t  *b;

    if (w <= 0 || h <= 0 || numLayers <= 0) {
        return 0;
    }
    k += va(" %dx%d %g", w, h, scale);
    for (int i = 0; i < numLayers; i++) {
        k += '|';
        k += layers[i];
    }
    auto it = bake_shapes.find(k);
    if (bake_ahead) {
        // its texels begun now, unless baked or begun
        if ((it == bake_shapes.end() || it->second.seq != uii.GetRefSequence()) && !bake_made.count(k)) {
            const std::vector<std::string> lines(layers, layers + numLayers);

            for (const std::string& l : lines) {
                Bake_Prepare(l.c_str());
            }
            bake_made[k] = Bake_Run([lines, w, h, scale] {
                std::vector<byte> t;

                Bake_ShapeTexels(lines, w, h, scale, t);
                return t;
            });
        }
        return 0;
    }
    if (it == bake_shapes.end()) {
        uiBaked_t nb;

        memset(&nb, 0, sizeof(nb));
        nb.seq = -1;
        Com_sprintf(nb.image, sizeof(nb.image), "ui_bake/%d", (int)bake_shapes.size());
        it = bake_shapes.emplace(k, nb).first;
    }
    b = &it->second;
    if (b->seq != uii.GetRefSequence()) {
        b->seq    = uii.GetRefSequence();
        b->gen    = bake_gen;
        b->handle = Bake_Shape(k, b->image, layers, numLayers, w, h, scale);
    } else if (b->gen != bake_gen) {
        b->gen    = bake_gen;
        b->handle = uii.Rend_RegisterMaterial(b->image);
    }
    return b->handle;
}
